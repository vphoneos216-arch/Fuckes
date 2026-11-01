package com.invisiblex.loader

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import java.util.concurrent.Executors

/**
 * AuthManager — central authentication gate.
 *
 * Security model (mirrors binary's double-check):
 *   1. On app launch: check if local session token exists + not expired locally.
 *   2. Background verify: send token to server for server-side validation.
 *   3. On tile tap / binary execute: MUST pass both local + server check before launching.
 *   4. Key login: full auth flow → save token → enable binary execution.
 *
 * Only when BOTH local AND server confirm validity does auth count as valid.
 */
class AuthManager private constructor() {

    // Triplicate auth state — mirrors binary's g_authOk_plain/inverted/xor pattern
    @Volatile private var authValid = false
    @Volatile private var authInverted = true // must always be !authValid
    @Volatile private var authVerified = false // server confirmed this session

    private val executor = Executors.newSingleThreadExecutor()

    // fun interface (SAM) so both LoginFragment.kt's trailing-lambda call
    // (login(ctx, key) { success, message -> ... }) and any remaining Java
    // caller using an anonymous class still compile against this.
    fun interface AuthCallback {
        fun onResult(success: Boolean, message: String)
    }

    /**
     * isAuthenticated() — triple-check guard.
     * Returns true ONLY if all three states agree.
     */
    fun isAuthenticated(): Boolean {
        val plain = authValid
        val inverted = authInverted
        val verified = authVerified
        // Tamper detection: if inverted doesn't match plain, something's wrong
        if (inverted == plain) {
            Log.e(TAG, "Auth state tamper detected — forcing logout")
            forceLogout()
            return false
        }
        return plain && verified
    }

    /** setAuthState — internal only; sets all three state vars atomically. */
    @Synchronized
    private fun setAuthState(valid: Boolean, serverVerified: Boolean) {
        authValid = valid
        authInverted = !valid
        authVerified = valid && serverVerified
    }

    /**
     * Attempt login with a license key.
     * Runs server auth on background thread, calls callback on main thread.
     */
    fun login(ctx: Context, licenseKey: String, callback: AuthCallback) {
        executor.execute {
            val authClient = AuthClient(AppPrefs.getServerUrl())
            val result = authClient.authenticate(ctx, licenseKey)

            if (result.success) {
                AppPrefs.saveLicenseKey(licenseKey)
                AppPrefs.saveSession(result.sessionToken ?: "", result.sessionExpiresAt)
                AppPrefs.saveExpiryInfo(result.expiryDate ?: "N/A", result.keyName ?: "")
                setAuthState(true, true)
                postCallback(
                    callback, true,
                    if (result.message.isEmpty()) "Authenticated — ${result.expiryDate}" else result.message
                )
            } else {
                setAuthState(false, false)
                postCallback(callback, false, result.message)
            }
        }
    }

    /**
     * Try to restore session from saved prefs without user input.
     * Quick local check first; then background server verify.
     */
    fun tryRestoreSession(ctx: Context, callback: AuthCallback) {
        executor.execute {
            val savedKey = AppPrefs.getLicenseKey()
            val savedTok = AppPrefs.getSessionToken()

            if (savedKey.isEmpty() || savedTok.isEmpty()) {
                setAuthState(false, false)
                postCallback(callback, false, "No saved session")
                return@execute
            }

            // Local token expiry check
            if (!AppPrefs.isSessionValid()) {
                setAuthState(false, false)
                AppPrefs.clearSession()
                postCallback(callback, false, getString(ctx, R.string.session_expired))
                return@execute
            }

            // No separate session-validation endpoint exists server-side —
            // the only real check is a full re-login with the saved key
            // (mirrors the binary's own perform_hwid_auth()).
            val authClient = AuthClient(AppPrefs.getServerUrl())
            val result = authClient.authenticate(ctx, savedKey)
            if (result.success) {
                AppPrefs.saveSession(result.sessionToken ?: "", result.sessionExpiresAt)
                AppPrefs.saveExpiryInfo(result.expiryDate ?: "N/A", result.keyName ?: "")
                setAuthState(true, true)
                postCallback(callback, true, getString(ctx, R.string.login_saved))
            } else {
                // Token invalid on server — force full re-login
                setAuthState(false, false)
                AppPrefs.clearSession()
                postCallback(callback, false, getString(ctx, R.string.session_expired))
            }
        }
    }

    /**
     * Called right before binary execute — final security gate.
     * Re-validates session in background; only calls callback(true) if fully valid.
     */
    fun securityGate(ctx: Context, callback: AuthCallback) {
        // NOTE: securityGate must NOT depend on in-memory authValid/authInverted
        // already being set — those fields live on this process's AuthManager
        // singleton and reset to their defaults whenever the app process is
        // freshly started (e.g. a Quick Settings tile tap after the app was
        // killed in the background). Gating on them here caused every
        // tile-triggered launch to fail instantly with "Verifying…" → error,
        // because a brand-new process never had a chance to set authValid=true
        // before the tile called straight into securityGate().
        //
        // securityGate() is the FINAL gate before any driver/binary operation,
        // so it re-derives truth itself, every time, from durable state
        // (AppPrefs) plus a mandatory server round-trip — never from stale
        // process-local flags. This is strictly stronger than before, not
        // weaker: it still requires both local validity AND a fresh server
        // confirmation on every single call.
        executor.execute {
            val savedTok = AppPrefs.getSessionToken()
            val savedKey = AppPrefs.getLicenseKey()
            if (savedTok.isEmpty() || savedKey.isEmpty() || !AppPrefs.isSessionValid()) {
                setAuthState(false, false)
                postCallback(callback, false, getString(ctx, R.string.session_expired))
                return@execute
            }
            // Final pre-execute gate — same re-login check as above, since
            // the server has no lighter "just validate" endpoint.
            val authClient = AuthClient(AppPrefs.getServerUrl())
            val result = authClient.authenticate(ctx, savedKey)
            if (result.success) {
                AppPrefs.saveSession(result.sessionToken ?: "", result.sessionExpiresAt)
                setAuthState(true, true)
                postCallback(callback, true, "OK")
            } else {
                setAuthState(false, false)
                AppPrefs.clearSession()
                postCallback(callback, false, getString(ctx, R.string.session_expired))
            }
        }
    }

    fun logout() {
        setAuthState(false, false)
        AppPrefs.logout()
    }

    fun forceLogout() {
        authValid = false
        authInverted = true
        authVerified = false
        AppPrefs.logout()
    }

    private fun postCallback(cb: AuthCallback, success: Boolean, msg: String) {
        Handler(Looper.getMainLooper()).post { cb.onResult(success, msg) }
    }

    private fun getString(ctx: Context, resId: Int): String {
        return try { ctx.getString(resId) } catch (e: Exception) { "" }
    }

    companion object {
        private const val TAG = "IX_Auth"

        @Volatile private var sInstance: AuthManager? = null

        @JvmStatic
        fun get(): AuthManager {
            return sInstance ?: synchronized(this) {
                sInstance ?: AuthManager().also { sInstance = it }
            }
        }
    }
}
