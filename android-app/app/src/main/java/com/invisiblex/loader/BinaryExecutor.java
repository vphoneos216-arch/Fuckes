package com.invisiblex.loader;

import android.content.Context;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.concurrent.TimeUnit;

/**
 * BinaryExecutor — launches the INX.SH engine, packaged as a native
 * library (libINX.so under jniLibs/arm64-v8a) and executed with root.
 *
 * Root execution path:
 *   su -c "<nativeLibraryDir>/libINX.so"
 *
 * Why root is required:
 *   The INX engine performs process injection into a separate target
 *   process. This requires ptrace, /proc/<pid>/mem write access, and
 *   kernel namespace operations — none of which are available to an
 *   unprivileged Android app process. Root (via Magisk/KSU/APatch su)
 *   is the required execution context.
 *
 * Root availability detection:
 *   We probe 'su' with a no-op 'id' command before attempting engine
 *   launch. A failed probe shows a clear "Root required" error rather
 *   than a cryptic permission denial.
 *
 * Security contract (NEVER bypass):
 *   - NEVER call execute() without passing through AuthManager.securityGate() first.
 *   - Binary lives under nativeLibraryDir (installer-managed, not world-writable).
 *   - No external paths accepted.
 *   - Root is gained only for the engine process; this app's own process stays unprivileged.
 */
public class BinaryExecutor {

    private static final String TAG     = "IX_Exec";
    private static final String LIBNAME = "libINX.so";

    /** Timeout for the root-probe 'id' command (milliseconds). */
    private static final long ROOT_PROBE_TIMEOUT_MS = 4_000;

    /** Timeout waiting for the engine process to confirm it started (milliseconds). */
    private static final long ENGINE_LAUNCH_TIMEOUT_MS = 5_000;

    public interface ExecCallback {
        void onResult(boolean success, String message);
    }

    // ── Public API ─────────────────────────────────────────────

    /**
     * Launch the engine from its installer-managed native library path.
     *
     * Must be called from a BACKGROUND thread.
     * NEVER call without prior AuthManager.securityGate() confirmation.
     *
     * Flow:
     *   1. Locate libINX.so in nativeLibraryDir.
     *   2. Probe root availability — fail fast with clear error if absent.
     *   3. Execute via:  su -c "<absolute_path>"
     *   4. Report success/failure via callback on main thread.
     */
    public static void extractAndExecute(Context ctx, ExecCallback cb) {
        try {
            // ── 1. Locate binary ──────────────────────────────
            File binFile = locateBinary(ctx);
            if (binFile == null || !binFile.exists()) {
                postResult(cb, false,
                    "Native engine not found — rebuild required");
                return;
            }

            // ── 2. Root probe ─────────────────────────────────
            RootStatus rootStatus = probeRoot();
            if (rootStatus == RootStatus.UNAVAILABLE) {
                postResult(cb, false,
                    "Root access required — install Magisk, KernelSU, or APatch");
                return;
            }
            if (rootStatus == RootStatus.TIMEOUT) {
                postResult(cb, false,
                    "Root prompt timed out — grant SU access and retry");
                return;
            }

            // ── 3. Execute with su ────────────────────────────
            String binPath = binFile.getAbsolutePath();
            Log.i(TAG, "Launching engine with root: " + binPath);

            ProcessBuilder pb = new ProcessBuilder("su", "-c", binPath);
            pb.directory(binFile.getParentFile());
            pb.redirectErrorStream(true);

            Process proc = pb.start();

            // Drain stdout/stderr in background to prevent buffer deadlock
            Thread drainer = new Thread(() -> {
                try (BufferedReader br = new BufferedReader(
                        new InputStreamReader(proc.getInputStream()))) {
                    String line;
                    while ((line = br.readLine()) != null) {
                        Log.d(TAG, "[engine] " + line);
                    }
                } catch (IOException ignored) {}
            }, "IX-engine-drainer");
            drainer.setDaemon(true);
            drainer.start();

            // Give the process ENGINE_LAUNCH_TIMEOUT_MS to either confirm start
            // or exit with an error. The engine is a daemon so it won't exit
            // on success — we treat "still running after timeout" as a launch success.
            boolean exited = proc.waitFor(ENGINE_LAUNCH_TIMEOUT_MS, TimeUnit.MILLISECONDS);
            if (exited) {
                int code = proc.exitValue();
                if (code != 0) {
                    Log.e(TAG, "Engine exited early with code " + code);
                    postResult(cb, false,
                        "Engine exited (code " + code + ") — check root + driver");
                    return;
                }
                // code == 0 — ran and completed (unlikely for a daemon, treat as OK)
            }
            // Process still running after timeout → daemon launched successfully
            Log.i(TAG, "Engine running (pid=" + proc.pid() + ")");
            postResult(cb, true, "InvisibleX launched");

        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            postResult(cb, false, "Launch interrupted");
        } catch (IOException e) {
            Log.e(TAG, "IO error launching engine", e);
            postResult(cb, false, "IO error: " + e.getMessage());
        } catch (Exception e) {
            Log.e(TAG, "Unexpected error", e);
            postResult(cb, false, "Error: " + e.getMessage());
        }
    }

    /**
     * Quick synchronous check — can root be obtained right now?
     * Returns true if 'su -c id' succeeds, false otherwise.
     * Safe to call from background thread.
     */
    public static boolean isRootAvailable() {
        return probeRoot() == RootStatus.AVAILABLE;
    }

    // ── Root probe ──────────────────────────────────────────────

    private enum RootStatus { AVAILABLE, UNAVAILABLE, TIMEOUT }

    /**
     * Run 'su -c id' with a short timeout to verify SU grant.
     * 'id' is a standard POSIX command guaranteed on all Android devices.
     */
    private static RootStatus probeRoot() {
        try {
            Process probe = Runtime.getRuntime().exec(new String[]{"su", "-c", "id"});
            boolean finished = probe.waitFor(ROOT_PROBE_TIMEOUT_MS, TimeUnit.MILLISECONDS);
            if (!finished) {
                probe.destroyForcibly();
                return RootStatus.TIMEOUT;
            }
            int code = probe.exitValue();
            // Read output — uid=0(root) confirms we have root
            String out = "";
            try (BufferedReader br = new BufferedReader(
                    new InputStreamReader(probe.getInputStream()))) {
                out = br.readLine();
                if (out == null) out = "";
            } catch (IOException ignored) {}

            if (code == 0 && out.contains("uid=0")) {
                Log.i(TAG, "Root confirmed: " + out);
                return RootStatus.AVAILABLE;
            }
            Log.w(TAG, "SU probe failed — code=" + code + " out=" + out);
            return RootStatus.UNAVAILABLE;

        } catch (IOException | InterruptedException e) {
            Log.w(TAG, "Root probe exception: " + e.getMessage());
            if (e instanceof InterruptedException) Thread.currentThread().interrupt();
            return RootStatus.UNAVAILABLE;
        }
    }

    // ── Helpers ────────────────────────────────────────────────

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
