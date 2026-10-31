package com.invisiblex.loader;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.IOException;

/**
 * BinaryExecutor — launches the INX.SH engine, now packaged as a real
 * native library (libINX.so under jniLibs/arm64-v8a) instead of an asset
 * that had to be extracted and chmod +x'd at runtime.
 *
 * Why this is the "pura native" fix: a file under jniLibs/<abi>/ is
 * extracted, placed and made executable by the Android package
 * installer itself (nativeLibraryDir), the same mechanism every app's
 * own .so libraries use — there is no manual asset copy, no runtime
 * chmod, and none of the newer Android W^X restrictions that can block
 * executing a file an app extracted into its own storage at runtime.
 * The engine itself still runs as its own process (it performs process
 * injection into a separate target process, which cannot be collapsed
 * into this app's own process) — only its packaging/extraction became
 * native-managed.
 *
 * Security contract (unchanged):
 *   - NEVER call execute() without passing through AuthManager.securityGate() first.
 *   - The binary now lives under the app's own nativeLibraryDir
 *     (installer-managed, not world-writable).
 *   - No external paths accepted.
 */
public class BinaryExecutor {

    private static final String TAG     = "IX_Exec";
    private static final String LIBNAME = "libINX.so";

    public interface ExecCallback {
        void onResult(boolean success, String message);
    }

    /**
     * Launch the engine from its installer-managed native library path.
     * Must be called from a background thread.
     * NEVER call without prior AuthManager.securityGate() confirmation.
     */
    public static void extractAndExecute(Context ctx, ExecCallback cb) {
        try {
            File binFile = locateBinary(ctx);
            if (binFile == null || !binFile.exists()) {
                postResult(cb, false, "Native engine library not found");
                return;
            }

            // The installer already lays this out executable under
            // nativeLibraryDir; this is just a defensive re-assert in case
            // of OEM quirks — no extraction, no copy, no chmod shelling out.
            if (!binFile.canExecute()) {
                binFile.setExecutable(true, true);
            }

            ProcessBuilder pb = new ProcessBuilder(binFile.getAbsolutePath());
            pb.directory(binFile.getParentFile());
            pb.redirectErrorStream(true);
            Process proc = pb.start();
            Log.i(TAG, "Engine launched from " + binFile.getAbsolutePath());
            postResult(cb, true, "InvisibleX launched");

        } catch (IOException e) {
            Log.e(TAG, "IO error launching engine", e);
            postResult(cb, false, "IO error: " + e.getMessage());
        } catch (Exception e) {
            Log.e(TAG, "Unexpected error", e);
            postResult(cb, false, "Error: " + e.getMessage());
        }
    }

    /**
     * Resolve libINX.so inside this app's installer-managed native
     * library directory (set at install time from jniLibs/<abi>/).
     */
    private static File locateBinary(Context ctx) {
        String nativeDir = ctx.getApplicationInfo().nativeLibraryDir;
        if (nativeDir == null) return null;
        File f = new File(nativeDir, LIBNAME);
        return f.exists() ? f : null;
    }

    private static void postResult(ExecCallback cb, boolean ok, String msg) {
        if (cb == null) return;
        new android.os.Handler(android.os.Looper.getMainLooper())
            .post(() -> cb.onResult(ok, msg));
    }
}
