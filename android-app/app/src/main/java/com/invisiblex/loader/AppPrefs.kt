package com.invisiblex.loader

import android.content.Context
import android.content.SharedPreferences
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey

/**
 * Encrypted SharedPreferences wrapper.
 * All sensitive values (key, session token) are stored encrypted via Jetpack Security.
 *
 * Every member below is @JvmStatic: several call sites (AuthManager,
 * DriverFragment, etc.) are still Java during this gradual migration and
 * call these as plain static methods — AppPrefs.getServerUrl() etc. —
 * exactly as they did when this was a Java class. Without @JvmStatic,
 * Kotlin would only expose them as AppPrefs.INSTANCE.getServerUrl(),
 * breaking every existing Java caller.
 */
object AppPrefs {

    private const val PREFS_FILE      = "ix_secure_prefs"
    private const val KEY_LICENSE     = "k_lic"
    private const val KEY_SESSION_TOK = "k_sess_tok"
    private const val KEY_SESSION_EXP = "k_sess_exp"
    private const val KEY_EXPIRY_DATE = "k_expiry_date"
    private const val KEY_KEY_NAME    = "k_key_name"
    private const val KEY_APP_NAME    = "k_app_name"
    private const val KEY_CUSTOM_LOGO = "k_custom_logo_path"
    private const val KEY_APP_HIDDEN  = "k_app_hidden"
    private const val KEY_DRIVER      = "k_driver"

    private const val DEFAULT_SERVER =
        "https://adminshop.myvipsite.fun/keydock_connect.php"

    private lateinit var sPrefs: SharedPreferences

    @Synchronized
    @JvmStatic
    fun init(ctx: Context) {
        if (::sPrefs.isInitialized) return
        sPrefs = try {
            val masterKey = MasterKey.Builder(ctx)
                .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
                .build()
            EncryptedSharedPreferences.create(
                ctx,
                PREFS_FILE,
                masterKey,
                EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
                EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM
            )
        } catch (e: Exception) {
            // Fallback to plain prefs if encryption init fails (shouldn't happen on API 26+)
            ctx.getSharedPreferences(PREFS_FILE + "_plain", Context.MODE_PRIVATE)
        }
    }

    // ── License key ───────────────────────────────────────────
    @JvmStatic
    fun saveLicenseKey(key: String) {
        sPrefs.edit().putString(KEY_LICENSE, key.trim().uppercase()).apply()
    }
    @JvmStatic
    fun getLicenseKey(): String = sPrefs.getString(KEY_LICENSE, "") ?: ""
    @JvmStatic
    fun clearLicenseKey() {
        sPrefs.edit().remove(KEY_LICENSE).apply()
    }

    // ── Session token (from server auth response) ────────────
    @JvmStatic
    fun saveSession(token: String, expiresAt: Long) {
        sPrefs.edit()
            .putString(KEY_SESSION_TOK, token)
            .putLong(KEY_SESSION_EXP, expiresAt)
            .apply()
    }
    @JvmStatic
    fun getSessionToken(): String = sPrefs.getString(KEY_SESSION_TOK, "") ?: ""
    @JvmStatic
    fun getSessionExpiry(): Long = sPrefs.getLong(KEY_SESSION_EXP, 0L)
    @JvmStatic
    fun isSessionValid(): Boolean {
        val tok = getSessionToken()
        if (tok.isEmpty()) return false
        val exp = getSessionExpiry()
        if (exp <= 0) return true // no expiry set — treat as valid until key check fails
        return System.currentTimeMillis() / 1000L < exp
    }
    @JvmStatic
    fun clearSession() {
        sPrefs.edit()
            .remove(KEY_SESSION_TOK)
            .remove(KEY_SESSION_EXP)
            .apply()
    }

    // ── Expiry display info ──────────────────────────────────
    @JvmStatic
    fun saveExpiryInfo(expiryDate: String, keyName: String) {
        sPrefs.edit()
            .putString(KEY_EXPIRY_DATE, expiryDate)
            .putString(KEY_KEY_NAME, keyName)
            .apply()
    }
    @JvmStatic
    fun getExpiryDate(): String = sPrefs.getString(KEY_EXPIRY_DATE, "N/A") ?: "N/A"
    @JvmStatic
    fun getKeyName(): String = sPrefs.getString(KEY_KEY_NAME, "") ?: ""

    // ── App settings ─────────────────────────────────────────
    @JvmStatic
    fun setAppName(name: String) {
        sPrefs.edit().putString(KEY_APP_NAME, name).apply()
    }
    @JvmStatic
    fun getAppName(): String = sPrefs.getString(KEY_APP_NAME, "InvisibleX") ?: "InvisibleX"

    // [SECURITY FIX] getServerUrl() used to read a user-writable pref
    // (KEY_SERVER_URL), with the Settings screen's input field merely
    // UI-disabled as the only thing stopping someone from pointing the
    // app's login traffic at a server they control (e.g. via a direct
    // write to EncryptedSharedPreferences, which a disabled EditText does
    // nothing to prevent). That would let a captured key get silently
    // replayed against a fake "connect" endpoint, or let an attacker
    // bypass the real activation/expiry checks entirely — this is
    // exactly the kind of login compromise that must never be possible.
    // The auth server URL is now a hardcoded constant, period: nothing
    // reads or writes a server-url pref any more, and no code path — not
    // even a rooted device writing the prefs file directly — can change
    // where AuthManager sends the login request. setServerUrl() is kept
    // as a harmless no-op only so any existing call site still compiles.
    @JvmStatic
    fun setServerUrl(url: String) {
        // Intentionally does nothing — see note above.
    }
    @JvmStatic
    fun getServerUrl(): String = DEFAULT_SERVER

    @JvmStatic
    fun setCustomLogoPath(path: String) {
        sPrefs.edit().putString(KEY_CUSTOM_LOGO, path).apply()
    }
    @JvmStatic
    fun getCustomLogoPath(): String = sPrefs.getString(KEY_CUSTOM_LOGO, "") ?: ""

    @JvmStatic
    fun setAppHidden(hidden: Boolean) {
        sPrefs.edit().putBoolean(KEY_APP_HIDDEN, hidden).apply()
    }
    @JvmStatic
    fun isAppHidden(): Boolean = sPrefs.getBoolean(KEY_APP_HIDDEN, false)

    @JvmStatic
    fun setSelectedDriver(driverName: String) {
        sPrefs.edit().putString(KEY_DRIVER, driverName).apply()
    }
    @JvmStatic
    fun getSelectedDriver(): String = sPrefs.getString(KEY_DRIVER, "") ?: ""

    // ── Full logout ──────────────────────────────────────────
    @JvmStatic
    fun logout() {
        sPrefs.edit()
            .remove(KEY_LICENSE)
            .remove(KEY_SESSION_TOK)
            .remove(KEY_SESSION_EXP)
            .remove(KEY_EXPIRY_DATE)
            .remove(KEY_KEY_NAME)
            .apply()
    }
}
