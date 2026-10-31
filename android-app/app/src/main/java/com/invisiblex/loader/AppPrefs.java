package com.invisiblex.loader;

import android.content.Context;
import android.content.SharedPreferences;

import androidx.security.crypto.EncryptedSharedPreferences;
import androidx.security.crypto.MasterKey;

/**
 * Encrypted SharedPreferences wrapper.
 * All sensitive values (key, session token) are stored encrypted via Jetpack Security.
 */
public class AppPrefs {

    private static final String PREFS_FILE       = "ix_secure_prefs";
    private static final String KEY_LICENSE      = "k_lic";
    private static final String KEY_SESSION_TOK  = "k_sess_tok";
    private static final String KEY_SESSION_EXP  = "k_sess_exp";
    private static final String KEY_EXPIRY_DATE  = "k_expiry_date";
    private static final String KEY_KEY_NAME     = "k_key_name";
    private static final String KEY_APP_NAME     = "k_app_name";
    private static final String KEY_SERVER_URL   = "k_server_url";
    private static final String KEY_CUSTOM_LOGO  = "k_custom_logo_path";
    private static final String KEY_APP_HIDDEN   = "k_app_hidden";
    private static final String KEY_DRIVER       = "k_driver";

    private static final String DEFAULT_SERVER   =
        "https://adminshop.myvipsite.fun/keydock_connect.php";

    private static SharedPreferences sPrefs;

    public static synchronized void init(Context ctx) {
        if (sPrefs != null) return;
        try {
            MasterKey masterKey = new MasterKey.Builder(ctx)
                .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
                .build();
            sPrefs = EncryptedSharedPreferences.create(
                ctx,
                PREFS_FILE,
                masterKey,
                EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
                EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM
            );
        } catch (Exception e) {
            // Fallback to plain prefs if encryption init fails (shouldn't happen on API 26+)
            sPrefs = ctx.getSharedPreferences(PREFS_FILE + "_plain", Context.MODE_PRIVATE);
        }
    }

    // ── License key ───────────────────────────────────────────
    public static void saveLicenseKey(String key) {
        sPrefs.edit().putString(KEY_LICENSE, key.trim().toUpperCase()).apply();
    }
    public static String getLicenseKey() {
        return sPrefs.getString(KEY_LICENSE, "");
    }
    public static void clearLicenseKey() {
        sPrefs.edit().remove(KEY_LICENSE).apply();
    }

    // ── Session token (from server auth response) ────────────
    public static void saveSession(String token, long expiresAt) {
        sPrefs.edit()
            .putString(KEY_SESSION_TOK, token)
            .putLong(KEY_SESSION_EXP, expiresAt)
            .apply();
    }
    public static String getSessionToken() {
        return sPrefs.getString(KEY_SESSION_TOK, "");
    }
    public static long getSessionExpiry() {
        return sPrefs.getLong(KEY_SESSION_EXP, 0L);
    }
    public static boolean isSessionValid() {
        String tok = getSessionToken();
        if (tok == null || tok.isEmpty()) return false;
        long exp = getSessionExpiry();
        if (exp <= 0) return true;  // no expiry set — treat as valid until key check fails
        return System.currentTimeMillis() / 1000L < exp;
    }
    public static void clearSession() {
        sPrefs.edit()
            .remove(KEY_SESSION_TOK)
            .remove(KEY_SESSION_EXP)
            .apply();
    }

    // ── Expiry display info ──────────────────────────────────
    public static void saveExpiryInfo(String expiryDate, String keyName) {
        sPrefs.edit()
            .putString(KEY_EXPIRY_DATE, expiryDate)
            .putString(KEY_KEY_NAME, keyName)
            .apply();
    }
    public static String getExpiryDate() {
        return sPrefs.getString(KEY_EXPIRY_DATE, "N/A");
    }
    public static String getKeyName() {
        return sPrefs.getString(KEY_KEY_NAME, "");
    }

    // ── App settings ─────────────────────────────────────────
    public static void setAppName(String name) {
        sPrefs.edit().putString(KEY_APP_NAME, name).apply();
    }
    public static String getAppName() {
        return sPrefs.getString(KEY_APP_NAME, "InvisibleX");
    }

    public static void setServerUrl(String url) {
        sPrefs.edit().putString(KEY_SERVER_URL, url).apply();
    }
    public static String getServerUrl() {
        return sPrefs.getString(KEY_SERVER_URL, DEFAULT_SERVER);
    }

    public static void setCustomLogoPath(String path) {
        sPrefs.edit().putString(KEY_CUSTOM_LOGO, path).apply();
    }
    public static String getCustomLogoPath() {
        return sPrefs.getString(KEY_CUSTOM_LOGO, "");
    }

    public static void setAppHidden(boolean hidden) {
        sPrefs.edit().putBoolean(KEY_APP_HIDDEN, hidden).apply();
    }
    public static boolean isAppHidden() {
        return sPrefs.getBoolean(KEY_APP_HIDDEN, false);
    }

    public static void setSelectedDriver(String driverName) {
        sPrefs.edit().putString(KEY_DRIVER, driverName).apply();
    }
    public static String getSelectedDriver() {
        return sPrefs.getString(KEY_DRIVER, "");
    }

    // ── Full logout ──────────────────────────────────────────
    public static void logout() {
        sPrefs.edit()
            .remove(KEY_LICENSE)
            .remove(KEY_SESSION_TOK)
            .remove(KEY_SESSION_EXP)
            .remove(KEY_EXPIRY_DATE)
            .remove(KEY_KEY_NAME)
            .apply();
    }
}
