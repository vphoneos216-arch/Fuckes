package com.invisiblex.loader

import android.content.Context
import org.json.JSONObject

/**
 * AuthClient — result shape for the native KeyDock login.
 *
 * The actual network call + crypto protocol live in NativeAuth / ix_auth.cpp
 * (see NativeAuth's class doc for why). This class only:
 *   1. Calls NativeAuth.performLogin() (AuthManager runs this on a
 *      background executor; this class is a dumb blocking call).
 *   2. Parses the JSON string it returns into an AuthResult object.
 *   3. Exposes the shared device HWID (same value the binary computes).
 *
 * There is intentionally no "validateSession" call here — the real
 * server protocol has no separate session-check endpoint; the ONLY
 * check that exists is a full re-login (same as the binary's own
 * perform_hwid_auth, which just re-runs perform_auth with the saved
 * key). AuthManager re-authenticates with the saved key for both the
 * initial session restore and the pre-execute security gate.
 */
class AuthClient(@Suppress("UNUSED_PARAMETER") ignoredServerUrl: String) {

    class AuthResult {
        @JvmField var success: Boolean = false
        @JvmField var message: String = "Unknown error"
        @JvmField var keyName: String? = null
        @JvmField var expiryDate: String? = null
        @JvmField var sessionToken: String? = null
        @JvmField var sessionExpiresAt: Long = 0L
        @JvmField var seller: String? = null
        @JvmField var devicesUsed: Int = 0
        @JvmField var maxDevices: Int = 0
        @JvmField var hwid: String? = null
    }

    /**
     * Performs one full license login against the KeyDock server.
     * BLOCKING — must be called from a background thread.
     */
    fun authenticate(ctx: Context, licenseKey: String): AuthResult {
        val result = AuthResult()
        try {
            val filesDir = ctx.filesDir.absolutePath
            val raw = NativeAuth.performLogin(licenseKey, filesDir)
            parseNativeResult(raw, result)
        } catch (t: Throwable) {
            result.message = "Native auth error: " + t.message
        }
        return result
    }

    private fun parseNativeResult(raw: String?, out: AuthResult) {
        if (raw.isNullOrEmpty()) {
            out.message = "Empty native auth response"
            return
        }
        val json = JSONObject(raw)
        out.success = json.optBoolean("success", false)
        out.message = json.optString("message", if (out.success) "Authenticated" else "Auth failed")
        out.hwid = json.optString("hwid", "")

        if (out.success) {
            out.keyName = json.optString("keyName", "")
            out.expiryDate = json.optString("expiryDate", "N/A")
            out.sessionToken = json.optString("sessionToken", "")
            out.sessionExpiresAt = json.optLong("sessionExpiresAt", 0L)
            out.seller = json.optString("sellerName", "N/A")
            out.devicesUsed = json.optInt("devicesUsed", 0)
            out.maxDevices = json.optInt("maxDevices", 0)
        }
    }

    companion object {
        // ── HWID ──────────────────────────────────────────────────

        /**
         * The device-bound HWID computed natively (same algorithm the
         * INX.SH binary uses — jni/src/ix_hwid.h, vendored in app/src/main/cpp).
         * The [ctx] parameter is unused; kept so existing call sites
         * (AuthManager) don't need to change.
         */
        @JvmStatic
        fun getHWID(@Suppress("UNUSED_PARAMETER") ctx: Context): String {
            return try {
                val hwid = NativeAuth.getSharedHwid()
                if (!hwid.isNullOrEmpty()) hwid else "UNKNOWN_HWID"
            } catch (t: Throwable) {
                "UNKNOWN_HWID"
            }
        }
    }
}
