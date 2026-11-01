package com.invisiblex.loader

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import java.io.BufferedReader
import java.io.File
import java.io.IOException
import java.io.InputStreamReader
import java.util.concurrent.TimeUnit

/**
 * BinaryExecutor — launches the INX.SH engine, packaged as a native
 * library (libINX.so under jniLibs/arm64-v8a) and executed with root.
 *
 * Root execution path:
 *   su -c "<nativeLibraryDir>/libINX.so" "<license key>"
 *
 * [ONE LOGIN, NOT TWO] The engine's own main() used to read its license
 * key interactively from stdin — fine for a human at a real terminal,
 * but this process is started headlessly via ProcessBuilder with no TTY,
 * so that prompt could never actually be answered: with no key saved
 * yet, login silently failed every single launch and the key the app
 * had just validated was never handed to (or saved by) the engine. The
 * app now passes its already-validated AppPrefs.getLicenseKey() as the
 * engine's first argument — the user still only ever types the key ONCE,
 * in the app — and the engine runs its own full, independent server
 * round-trip against that same key (still the real, separate "binary
 * login" required by the dual-check security contract; the app is not
 * trusted blindly) before saving it for next time.
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
object BinaryExecutor {

    private const val TAG = "IX_Exec"
    private const val LIBNAME = "libINX.so"

    /** Timeout for the root-probe 'id' command (milliseconds). */
    private const val ROOT_PROBE_TIMEOUT_MS = 4_000L

    /** Timeout waiting for the engine process to confirm it started (milliseconds). */
    private const val ENGINE_LAUNCH_TIMEOUT_MS = 5_000L

    fun interface ExecCallback {
        fun onResult(success: Boolean, message: String)
    }

    private enum class RootStatus { AVAILABLE, UNAVAILABLE, TIMEOUT }

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
    @JvmStatic
    fun extractAndExecute(ctx: Context, cb: ExecCallback?) {
        try {
            // ── 1. Locate binary ──────────────────────────────
            val binFile = locateBinary(ctx)
            if (binFile == null || !binFile.exists()) {
                postResult(cb, false, "Native engine not found — rebuild required")
                return
            }

            // ── 2. Root probe ─────────────────────────────────
            when (probeRoot()) {
                RootStatus.UNAVAILABLE -> {
                    postResult(cb, false, "Root access required — install Magisk, KernelSU, or APatch")
                    return
                }
                RootStatus.TIMEOUT -> {
                    postResult(cb, false, "Root prompt timed out — grant SU access and retry")
                    return
                }
                RootStatus.AVAILABLE -> { /* continue */ }
            }

            // ── 3. Execute with su ────────────────────────────
            val binPath = binFile.absolutePath
            Log.i(TAG, "Launching engine with root: $binPath")

            // Hand the already-validated license key to the engine as its
            // first argument (see class doc above) — one login, not two.
            // Falls back to no argument (engine reuses its own saved key,
            // if any) when the app has no key cached, which should not
            // normally happen since securityGate() already required one.
            val savedKey = AppPrefs.getLicenseKey()
            val cmd = if (savedKey.isNotBlank()) {
                // Single-quoted and escaped for the "su -c '<shell line>'"
                // wrapper — AppPrefs.getLicenseKey() is always server-format
                // (kd_key_format_ok: "X-" + A-Z0-9 only), so this is just a
                // defensive guard, not a format assumption.
                val escapedPath = binPath.replace("'", "'\\''")
                val escapedKey = savedKey.replace("'", "'\\''")
                "'$escapedPath' '$escapedKey'"
            } else {
                binPath
            }

            val pb = ProcessBuilder("su", "-c", cmd)
            pb.directory(binFile.parentFile)
            pb.redirectErrorStream(true)

            val proc = pb.start()

            // Drain stdout/stderr in background to prevent buffer deadlock
            val drainer = Thread({
                try {
                    BufferedReader(InputStreamReader(proc.inputStream)).use { br ->
                        var line: String?
                        while (br.readLine().also { line = it } != null) {
                            Log.d(TAG, "[engine] $line")
                        }
                    }
                } catch (ignored: IOException) {
                }
            }, "IX-engine-drainer")
            drainer.isDaemon = true
            drainer.start()

            // Give the process ENGINE_LAUNCH_TIMEOUT_MS to either confirm start
            // or exit with an error. The engine is a daemon so it won't exit
            // on success — we treat "still running after timeout" as a launch success.
            val exited = proc.waitFor(ENGINE_LAUNCH_TIMEOUT_MS, TimeUnit.MILLISECONDS)
            if (exited) {
                val code = proc.exitValue()
                if (code != 0) {
                    Log.e(TAG, "Engine exited early with code $code")
                    postResult(cb, false, "Engine exited (code $code) — check root + driver")
                    return
                }
                // code == 0 — ran and completed (unlikely for a daemon, treat as OK)
            }
            // Process still running after timeout → daemon launched successfully.
            // Note: proc here is the "su" wrapper process, not the engine's own
            // PID, so there's nothing useful to report beyond "still running".
            Log.i(TAG, "Engine running (launched via su, still alive after ${ENGINE_LAUNCH_TIMEOUT_MS}ms probe window)")
            postResult(cb, true, "InvisibleX launched")
        } catch (e: InterruptedException) {
            Thread.currentThread().interrupt()
            postResult(cb, false, "Launch interrupted")
        } catch (e: IOException) {
            Log.e(TAG, "IO error launching engine", e)
            postResult(cb, false, "IO error: ${e.message}")
        } catch (e: Exception) {
            Log.e(TAG, "Unexpected error", e)
            postResult(cb, false, "Error: ${e.message}")
        }
    }

    /**
     * Quick synchronous check — can root be obtained right now?
     * Returns true if 'su -c id' succeeds, false otherwise.
     * Safe to call from background thread.
     */
    @JvmStatic
    fun isRootAvailable(): Boolean = probeRoot() == RootStatus.AVAILABLE

    // ── Root probe ──────────────────────────────────────────────

    /**
     * Run 'su -c id' with a short timeout to verify SU grant.
     * 'id' is a standard POSIX command guaranteed on all Android devices.
     */
    private fun probeRoot(): RootStatus {
        return try {
            val probe = Runtime.getRuntime().exec(arrayOf("su", "-c", "id"))
            val finished = probe.waitFor(ROOT_PROBE_TIMEOUT_MS, TimeUnit.MILLISECONDS)
            if (!finished) {
                probe.destroyForcibly()
                return RootStatus.TIMEOUT
            }
            val code = probe.exitValue()
            // Read output — uid=0(root) confirms we have root
            var out = ""
            try {
                BufferedReader(InputStreamReader(probe.inputStream)).use { br ->
                    out = br.readLine() ?: ""
                }
            } catch (ignored: IOException) {
            }

            if (code == 0 && out.contains("uid=0")) {
                Log.i(TAG, "Root confirmed: $out")
                RootStatus.AVAILABLE
            } else {
                Log.w(TAG, "SU probe failed — code=$code out=$out")
                RootStatus.UNAVAILABLE
            }
        } catch (e: IOException) {
            Log.w(TAG, "Root probe exception: ${e.message}")
            RootStatus.UNAVAILABLE
        } catch (e: InterruptedException) {
            Thread.currentThread().interrupt()
            Log.w(TAG, "Root probe exception: ${e.message}")
            RootStatus.UNAVAILABLE
        }
    }

    // ── Helpers ────────────────────────────────────────────────

    /**
     * Resolve libINX.so inside this app's installer-managed native
     * library directory (set at install time from jniLibs/<abi>/).
     */
    private fun locateBinary(ctx: Context): File? {
        val nativeDir = ctx.applicationInfo.nativeLibraryDir ?: return null
        val f = File(nativeDir, LIBNAME)
        return if (f.exists()) f else null
    }

    private fun postResult(cb: ExecCallback?, ok: Boolean, msg: String) {
        if (cb == null) return
        Handler(Looper.getMainLooper()).post { cb.onResult(ok, msg) }
    }
}
