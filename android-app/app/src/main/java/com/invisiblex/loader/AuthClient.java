package com.invisiblex.loader;

import android.content.Context;

import org.json.JSONObject;

/**
 * AuthClient — Java-side result shape for the native KeyDock login.
 *
 * The actual network call + crypto protocol live in NativeAuth / ix_auth.cpp
 * (see NativeAuth's class doc for why). This class only:
 *   1. Calls NativeAuth.performLogin() off the calling thread's caller
 *      (AuthManager already runs this on a background executor).
 *   2. Parses the JSON string it returns into an AuthResult Java object.
 *   3. Exposes the shared device HWID (same value the binary computes).
 *
 * There is intentionally no "validateSession" call here — the real
 * server protocol has no separate session-check endpoint; the ONLY
 * check that exists is a full re-login (same as the binary's own
 * perform_hwid_auth, which just re-runs perform_auth with the saved
 * key). AuthManager re-authenticates with the saved key for both the
 * initial session restore and the pre-execute security gate.
 */
public class AuthClient {

    public static class AuthResult {
        public boolean success;
        public String  message;
        public String  keyName;
        public String  expiryDate;
        public String  sessionToken;
        public long    sessionExpiresAt;
        public String  seller;
        public int     devicesUsed;
        public int     maxDevices;
        public String  hwid;
    }

    public AuthClient(String ignoredServerUrl) {
        // The server URL is now compiled into ix_auth.cpp (same constant
        // the binary uses, see login.h's CONNECT_URL) rather than passed
        // at runtime — it is a fixed deployment value, not configuration.
        // Constructor kept for call-site compatibility with AuthManager.
    }

    /**
     * Performs one full license login against the KeyDock server.
     * BLOCKING — must be called from a background thread.
     */
    public AuthResult authenticate(Context ctx, String licenseKey) {
        AuthResult result = new AuthResult();
        result.success = false;
        result.message = "Unknown error";

        try {
            String filesDir = ctx.getFilesDir().getAbsolutePath();
            String raw = NativeAuth.performLogin(licenseKey, filesDir);
            parseNativeResult(raw, result);
        } catch (Throwable t) {
            result.message = "Native auth error: " + t.getMessage();
        }
        return result;
    }

    private void parseNativeResult(String raw, AuthResult out) throws Exception {
        if (raw == null || raw.isEmpty()) {
            out.message = "Empty native auth response";
            return;
        }
        JSONObject json = new JSONObject(raw);
        out.success = json.optBoolean("success", false);
        out.message = json.optString("message", out.success ? "Authenticated" : "Auth failed");
        out.hwid = json.optString("hwid", "");

        if (out.success) {
            out.keyName          = json.optString("keyName", "");
            out.expiryDate       = json.optString("expiryDate", "N/A");
            out.sessionToken     = json.optString("sessionToken", "");
            out.sessionExpiresAt = json.optLong("sessionExpiresAt", 0L);
            out.seller           = json.optString("sellerName", "N/A");
            out.devicesUsed      = json.optInt("devicesUsed", 0);
            out.maxDevices       = json.optInt("maxDevices", 0);
        }
    }

    // ── HWID ──────────────────────────────────────────────────

    /**
     * The device-bound HWID computed natively (same algorithm the
     * INX.SH binary uses — jni/src/ix_hwid.h, vendored in app/src/main/cpp).
     * The {@code ctx} parameter is unused; kept so existing call sites
     * (AuthManager) don't need to change.
     */
    public static String getHWID(Context ctx) {
        try {
            String hwid = NativeAuth.getSharedHwid();
            return (hwid != null && !hwid.isEmpty()) ? hwid : "UNKNOWN_HWID";
        } catch (Throwable t) {
            return "UNKNOWN_HWID";
        }
    }
}
