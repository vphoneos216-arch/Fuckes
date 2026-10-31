package com.invisiblex.loader;

import android.content.Context;
import android.util.Log;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * AuthManager — central authentication gate.
 *
 * Security model (mirrors binary's double-check):
 *   1. On app launch: check if local session token exists + not expired locally.
 *   2. Background verify: send token to server for server-side validation.
 *   3. On tile tap / binary execute: MUST pass both local + server check before launching.
 *   4. Key login: full auth flow → save token → enable binary execution.
 *
 * Only when BOTH local AND server confirm validity does g_authValid = true.
 */
public class AuthManager {

    private static final String TAG = "IX_Auth";

    // Triplicate auth state — mirrors binary's g_authOk_plain/inverted/xor pattern
    private volatile boolean authValid      = false;
    private volatile boolean authInverted   = true;   // must always be !authValid
    private volatile boolean authVerified   = false;  // server confirmed this session

    private static AuthManager sInstance;
    private final ExecutorService executor = Executors.newSingleThreadExecutor();

    public interface AuthCallback {
        void onResult(boolean success, String message);
    }

    public static synchronized AuthManager get() {
        if (sInstance == null) sInstance = new AuthManager();
        return sInstance;
    }

    private AuthManager() {}

    /**
     * isAuthenticated() — triple-check guard.
     * Returns true ONLY if all three states agree.
     */
    public boolean isAuthenticated() {
        boolean plain    = authValid;
        boolean inverted = authInverted;
        boolean verified = authVerified;
        // Tamper detection: if inverted doesn't match plain, something's wrong
        if (inverted == plain) {
            Log.e(TAG, "Auth state tamper detected — forcing logout");
            forceLogout();
            return false;
        }
        return plain && verified;
    }

    /**
     * setAuthState — internal only; sets all three state vars atomically.
     */
    private synchronized void setAuthState(boolean valid, boolean serverVerified) {
        authValid    = valid;
        authInverted = !valid;
        authVerified = valid && serverVerified;
    }

    /**
     * Attempt login with a license key.
     * Runs server auth on background thread, calls callback on main thread.
     */
    public void login(Context ctx, String licenseKey, AuthCallback callback) {
        executor.execute(() -> {
            AuthClient authClient = new AuthClient(AppPrefs.getServerUrl());
            AuthClient.AuthResult result = authClient.authenticate(ctx, licenseKey);

            if (result.success) {
                // Save everything
                AppPrefs.saveLicenseKey(licenseKey);
                AppPrefs.saveSession(result.sessionToken, result.sessionExpiresAt);
                AppPrefs.saveExpiryInfo(result.expiryDate, result.keyName);
                setAuthState(true, true);
                postCallback(callback, true, result.message.isEmpty()
                    ? "Authenticated — " + result.expiryDate : result.message);
            } else {
                setAuthState(false, false);
                postCallback(callback, false, result.message);
            }
        });
    }

    /**
     * Try to restore session from saved prefs without user input.
     * Quick local check first; then background server verify.
     */
    public void tryRestoreSession(Context ctx, AuthCallback callback) {
        executor.execute(() -> {
            String savedKey = AppPrefs.getLicenseKey();
            String savedTok = AppPrefs.getSessionToken();

            if (savedKey.isEmpty() || savedTok.isEmpty()) {
                setAuthState(false, false);
                postCallback(callback, false, "No saved session");
                return;
            }

            // Local token expiry check
            if (!AppPrefs.isSessionValid()) {
                setAuthState(false, false);
                AppPrefs.clearSession();
                postCallback(callback, false, getString(ctx, R.string.session_expired));
                return;
            }

            // No separate session-validation endpoint exists server-side —
            // the only real check is a full re-login with the saved key
            // (mirrors the binary's own perform_hwid_auth()).
            AuthClient authClient = new AuthClient(AppPrefs.getServerUrl());
            AuthClient.AuthResult result = authClient.authenticate(ctx, savedKey);
            if (result.success) {
                AppPrefs.saveSession(result.sessionToken, result.sessionExpiresAt);
                AppPrefs.saveExpiryInfo(result.expiryDate, result.keyName);
                setAuthState(true, true);
                postCallback(callback, true,
                    getString(ctx, R.string.login_saved));
            } else {
                // Token invalid on server — force full re-login
                setAuthState(false, false);
                AppPrefs.clearSession();
                postCallback(callback, false,
                    getString(ctx, R.string.session_expired));
            }
        });
    }

    /**
     * Called right before binary execute — final security gate.
     * Re-validates session in background; only calls callback(true) if fully valid.
     */
    public void securityGate(Context ctx, AuthCallback callback) {
        if (!authValid || authInverted == authValid) {
            postCallback(callback, false, getString(ctx, R.string.auth_double_check));
            return;
        }
        executor.execute(() -> {
            String savedTok = AppPrefs.getSessionToken();
            String savedKey = AppPrefs.getLicenseKey();
            if (savedTok.isEmpty() || savedKey.isEmpty() || !AppPrefs.isSessionValid()) {
                setAuthState(false, false);
                postCallback(callback, false, getString(ctx, R.string.session_expired));
                return;
            }
            // Final pre-execute gate — same re-login check as above, since
            // the server has no lighter "just validate" endpoint.
            AuthClient authClient = new AuthClient(AppPrefs.getServerUrl());
            AuthClient.AuthResult result = authClient.authenticate(ctx, savedKey);
            if (result.success) {
                AppPrefs.saveSession(result.sessionToken, result.sessionExpiresAt);
                setAuthState(true, true);
                postCallback(callback, true, "OK");
            } else {
                setAuthState(false, false);
                AppPrefs.clearSession();
                postCallback(callback, false, getString(ctx, R.string.session_expired));
            }
        });
    }

    public void logout() {
        setAuthState(false, false);
        AppPrefs.logout();
    }

    public void forceLogout() {
        authValid    = false;
        authInverted = true;
        authVerified = false;
        AppPrefs.logout();
    }

    private void postCallback(AuthCallback cb, boolean success, String msg) {
        android.os.Handler main = new android.os.Handler(
            android.os.Looper.getMainLooper());
        main.post(() -> cb.onResult(success, msg));
    }

    private String getString(Context ctx, int resId) {
        try { return ctx.getString(resId); }
        catch (Exception e) { return ""; }
    }
}
