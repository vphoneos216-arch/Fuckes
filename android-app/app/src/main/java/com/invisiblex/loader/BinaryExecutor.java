package com.invisiblex.loader;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * BinaryExecutor — extracts INX.SH from assets and executes it.
 *
 * Security contract:
 *   - NEVER call execute() without passing through AuthManager.securityGate() first.
 *   - Binary is extracted to app-private internal storage (not world-readable).
 *   - chmod +x applied before execution.
 *   - No external paths accepted — always extract from trusted assets bundle.
 */
public class BinaryExecutor {

    private static final String TAG      = "IX_Exec";
    private static final String ASSET    = "INX.SH";
    private static final String BIN_DIR  = "bin";

    public interface ExecCallback {
        void onResult(boolean success, String message);
    }

    /**
     * Extract the binary from assets to internal storage and execute it.
     * Must be called from a background thread.
     * NEVER call without prior AuthManager.securityGate() confirmation.
     */
    public static void extractAndExecute(Context ctx, ExecCallback cb) {
        try {
            File binFile = extractBinary(ctx);
            if (binFile == null) {
                postResult(cb, false, "Failed to extract binary");
                return;
            }

            // chmod +x
            if (!binFile.setExecutable(true, true)) {
                Log.w(TAG, "setExecutable returned false — trying chmod via shell");
                Process chmod = Runtime.getRuntime().exec(
                    new String[]{"chmod", "700", binFile.getAbsolutePath()});
                chmod.waitFor();
            }

            // Execute
            ProcessBuilder pb = new ProcessBuilder(binFile.getAbsolutePath());
            pb.directory(binFile.getParentFile());
            pb.redirectErrorStream(true);
            Process proc = pb.start();
            Log.i(TAG, "Binary launched — PID obtained");
            postResult(cb, true, "InvisibleX launched");

        } catch (IOException e) {
            Log.e(TAG, "IO error executing binary", e);
            postResult(cb, false, "IO error: " + e.getMessage());
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            postResult(cb, false, "Interrupted");
        } catch (Exception e) {
            Log.e(TAG, "Unexpected error", e);
            postResult(cb, false, "Error: " + e.getMessage());
        }
    }

    /**
     * Extract binary from assets to app-private internal storage.
     * Returns the extracted File, or null on failure.
     */
    private static File extractBinary(Context ctx) throws IOException {
        File binDir = new File(ctx.getFilesDir(), BIN_DIR);
        if (!binDir.exists()) binDir.mkdirs();

        File binFile = new File(binDir, ASSET);

        // Always re-extract to ensure integrity (can be optimized with hash check later)
        InputStream in = null;
        OutputStream out = null;
        try {
            in  = ctx.getAssets().open(ASSET);
            out = new FileOutputStream(binFile);
            byte[] buf = new byte[8192];
            int len;
            while ((len = in.read(buf)) != -1) {
                out.write(buf, 0, len);
            }
            out.flush();
            Log.i(TAG, "Binary extracted to: " + binFile.getAbsolutePath()
                + " (" + binFile.length() + " bytes)");
            return binFile;
        } finally {
            if (in  != null) try { in.close();  } catch (Exception ignored) {}
            if (out != null) try { out.close(); } catch (Exception ignored) {}
        }
    }

    private static void postResult(ExecCallback cb, boolean ok, String msg) {
        if (cb == null) return;
        new android.os.Handler(android.os.Looper.getMainLooper())
            .post(() -> cb.onResult(ok, msg));
    }
}
