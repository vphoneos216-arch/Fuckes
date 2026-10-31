package com.invisiblex.loader;

import android.content.Context;
import android.os.Build;

import org.json.JSONObject;

import java.io.IOException;
import java.security.MessageDigest;
import java.util.concurrent.TimeUnit;

import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;

/**
 * Auth client — mirrors the keydock_connect.php protocol used in login.h.
 * Sends key + HWID to the server and validates the response.
 *
 * Security:
 *  - HTTPS only (enforced via network_security_config.xml)
 *  - Sends device fingerprint as HWID
 *  - Server must return { "success": true, "session_token": "...", "expiry_date": "...", ... }
 *  - Double-check: app checks session token validity locally PLUS verifies server on each launch
 */
public class AuthClient {

    public static final MediaType JSON_TYPE = MediaType.get("application/json; charset=utf-8");

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
    }

    private final OkHttpClient client;
    private final String       serverUrl;

    public AuthClient(String serverUrl) {
        this.serverUrl = serverUrl;
        this.client = new OkHttpClient.Builder()
            .connectTimeout(15, TimeUnit.SECONDS)
            .readTimeout(20, TimeUnit.SECONDS)
            .writeTimeout(10, TimeUnit.SECONDS)
            .build();
    }

    /**
     * Perform key authentication against the KeyDock server.
     * Runs on the calling thread — must be called from a background thread.
     */
    public AuthResult authenticate(String licenseKey, String hwid) {
        AuthResult result = new AuthResult();
        result.success = false;
        result.message = "Unknown error";

        try {
            JSONObject body = new JSONObject();
            body.put("mode", "key");
            body.put("key_name", licenseKey.trim().toUpperCase());
            body.put("hwid", hwid);
            body.put("app", "invisiblex_android");
            body.put("version", BuildConfig.VERSION_NAME);

            RequestBody reqBody = RequestBody.create(body.toString(), JSON_TYPE);
            Request request = new Request.Builder()
                .url(serverUrl)
                .post(reqBody)
                .addHeader("Content-Type", "application/json")
                .addHeader("Accept", "application/json")
                .addHeader("X-App-ID", "invisiblex")
                .build();

            try (Response response = client.newCall(request).execute()) {
                if (!response.isSuccessful()) {
                    result.message = "Server error: " + response.code();
                    return result;
                }
                String respBody = response.body() != null ? response.body().string() : "";
                if (respBody.isEmpty()) {
                    result.message = "Empty server response";
                    return result;
                }
                parseResponse(respBody, result);
            }
        } catch (IOException e) {
            result.message = "Network error: " + e.getMessage();
        } catch (Exception e) {
            result.message = "Error: " + e.getMessage();
        }
        return result;
    }

    /**
     * Quick session re-validation — sends saved session token to server.
     * Returns true if server says session is still valid.
     */
    public boolean validateSession(String sessionToken, String hwid) {
        try {
            JSONObject body = new JSONObject();
            body.put("mode", "validate_session");
            body.put("session_token", sessionToken);
            body.put("hwid", hwid);
            body.put("app", "invisiblex_android");

            RequestBody reqBody = RequestBody.create(body.toString(), JSON_TYPE);
            Request request = new Request.Builder()
                .url(serverUrl)
                .post(reqBody)
                .addHeader("Content-Type", "application/json")
                .addHeader("Accept", "application/json")
                .addHeader("X-App-ID", "invisiblex")
                .build();

            try (Response response = client.newCall(request).execute()) {
                if (!response.isSuccessful()) return false;
                String respBody = response.body() != null ? response.body().string() : "";
                JSONObject json = new JSONObject(respBody);
                return json.optBoolean("success", false)
                    && json.optBoolean("valid", false);
            }
        } catch (Exception e) {
            return false;
        }
    }

    private void parseResponse(String raw, AuthResult out) throws Exception {
        JSONObject json = new JSONObject(raw);
        out.success = json.optBoolean("success", false);
        out.message = json.optString("message", out.success ? "Authenticated" : "Auth failed");

        if (out.success) {
            JSONObject payload = json.optJSONObject("payload");
            if (payload == null) payload = json;  // flat response fallback

            out.keyName       = payload.optString("key_name", "");
            out.expiryDate    = payload.optString("expiry_date", "N/A");
            out.sessionToken  = payload.optString("session_token", "");
            out.sessionExpiresAt = payload.optLong("session_expires_at", 0L);
            out.seller        = payload.optString("seller", "");
            out.devicesUsed   = payload.optInt("devices_used", 0);
            out.maxDevices    = payload.optInt("max_devices", 0);
        }
    }

    // ── HWID generation ──────────────────────────────────────

    /**
     * Generates a stable hardware ID from device identifiers.
     * Mirrors the HWID logic in the binary — same value on both sides.
     */
    public static String getHWID(Context ctx) {
        try {
            String raw = Build.BOARD + Build.BRAND + Build.DEVICE +
                         Build.HARDWARE + Build.MANUFACTURER +
                         Build.MODEL + Build.PRODUCT +
                         android.provider.Settings.Secure.getString(
                             ctx.getContentResolver(),
                             android.provider.Settings.Secure.ANDROID_ID);

            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            byte[] hash = digest.digest(raw.getBytes("UTF-8"));
            StringBuilder sb = new StringBuilder();
            for (byte b : hash) {
                sb.append(String.format("%02X", b));
            }
            // Return first 32 chars — same format as binary
            return sb.toString().substring(0, 32);
        } catch (Exception e) {
            return "UNKNOWN_HWID_00000000000000000000";
        }
    }
}
