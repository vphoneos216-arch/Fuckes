package com.invisiblex.loader;

/**
 * NativeAuth — thin JNI boundary into libixauth.so (app/src/main/cpp/ix_auth.cpp).
 *
 * WHY this exists: the KeyDock server (keydock_connect.php) only accepts
 * its double-encrypted v2 wire protocol (X25519 key exchange, nested
 * XChaCha20-Poly1305 / XSalsa20-Poly1305 encryption, Ed25519-signed
 * response). That protocol is already implemented and proven correct
 * in jni/src/login.h for the INX.SH binary — ix_auth.cpp reimplements
 * the SAME protocol natively for the app, rather than hand-porting the
 * crypto to Java (error-prone) or sending plain JSON (which the server
 * correctly rejects with HTTP 400 — the bug this replaces).
 *
 * This class has no logic of its own; see AuthClient for the Java-side
 * result shape and AuthManager for the double-auth gate that calls it.
 */
final class NativeAuth {

    static {
        System.loadLibrary("ixauth");
    }

    private NativeAuth() {}

    /**
     * Performs one full license login against the KeyDock server.
     * BLOCKING — must be called from a background thread.
     *
     * @param licenseKey the key the user typed (or the saved key, for a
     *                   session re-check — the server has no separate
     *                   "validate session" call; re-login is the only
     *                   check that exists, same as the binary itself).
     * @param filesDir   app's private files dir (for the cached CA bundle).
     * @return a JSON string — see AuthClient#parseNativeResult for the shape.
     */
    static native String performLogin(String licenseKey, String filesDir);

    /**
     * The same device-bound HWID the native binary computes
     * (jni/src/ix_hwid.h, vendored byte-for-byte) — so the app's login
     * and the binary's later, independent login are recognized by the
     * server as the same device.
     */
    static native String getSharedHwid();

    /**
     * App-side integrity gate — must be called and must return true
     * BEFORE the login screen is shown. Verifies the compiled-in server
     * URL + Ed25519 public key are still the genuine deployment values
     * and that libsodium initializes. See ix_auth.cpp's comment on this
     * function for exactly what it does and does not check, and why.
     */
    static native boolean integrityCheck();
}
