package com.invisiblex.loader

/**
 * NativeAuth — thin JNI boundary into libixauth.so (app/src/main/cpp/ix_auth.cpp).
 *
 * WHY this exists: the KeyDock server (keydock_connect.php) only accepts
 * its double-encrypted v2 wire protocol (X25519 key exchange, nested
 * XChaCha20-Poly1305 / XSalsa20-Poly1305 encryption, Ed25519-signed
 * response). That protocol is already implemented and proven correct
 * in jni/src/login.h for the INX.SH binary — ix_auth.cpp reimplements
 * the SAME protocol natively for the app, rather than hand-porting the
 * crypto to Kotlin/Java (error-prone) or sending plain JSON (which the
 * server correctly rejects with HTTP 400 — the bug this replaces).
 *
 * This object has no logic of its own; see AuthClient for the result
 * shape and AuthManager for the double-auth gate that calls it.
 */
// NOTE: deliberately NOT `internal` — Kotlin mangles internal member
// names at the bytecode level (a module-safety measure), which would
// silently break every remaining Java caller (AuthClient, LoginFragment,
// and any .java file still calling NativeAuth.performLogin(...) by that
// exact method name). Plain top-level visibility here matches the
// original package-private Java class closely enough and keeps every
// existing call site compiling unchanged.
object NativeAuth {

    init {
        System.loadLibrary("ixauth")
    }

    /**
     * Performs one full license login against the KeyDock server.
     * BLOCKING — must be called from a background thread.
     *
     * @param licenseKey the key the user typed (or the saved key, for a
     *                   session re-check — the server has no separate
     *                   "validate session" call; re-login is the only
     *                   check that exists, same as the binary itself).
     * @param filesDir   app's private files dir (for the cached CA bundle).
     * @return a JSON string — see AuthClient's parse logic for the shape.
     */
    @JvmStatic
    external fun performLogin(licenseKey: String, filesDir: String): String

    /**
     * The same device-bound HWID the native binary computes
     * (jni/src/ix_hwid.h, vendored byte-for-byte) — so the app's login
     * and the binary's later, independent login are recognized by the
     * server as the same device.
     */
    @JvmStatic
    external fun getSharedHwid(): String

    /**
     * App-side integrity gate — must be called and must return true
     * BEFORE the login screen is shown. Verifies the compiled-in server
     * URL + Ed25519 public key are still the genuine deployment values
     * and that libsodium initializes. See ix_auth.cpp's comment on this
     * function for exactly what it does and does not check, and why.
     */
    @JvmStatic
    external fun integrityCheck(): Boolean
}
