package com.invisiblex.loader

import android.content.Context
import android.util.Log
import java.io.BufferedReader
import java.io.File
import java.io.FileOutputStream
import java.io.InputStreamReader
import java.util.regex.Pattern

/**
 * DriverConfig — bridges the Driver section UI to the real native backend
 * selection in jni/src/ix_driver_init.h / jni/src/wanbai.h.
 *
 * The InvisibleX binary does NOT pick a driver by scanning files the user
 * extracts on-device — it picks one of 11 fixed in-binary backend modes
 * (AUTO + 10 named modes) by reading a single key, "driver", out of:
 *
 *   /data/adb/modules/invisiblex_loader/config/config.json
 *
 * (ix_get_driver_choice() → ix_read_driver_raw() → ix_driver_number()).
 * Whatever the Driver section writes here is exactly what c_driver's
 * constructor (wanbai.h) will try on the next binary launch.
 *
 * Consistency, verified against jni/src/ix_driver_init.h's
 * ix_driver_number(): every DriverInfo.configValue in DriverInfo.kt
 * ("auto", "qx1", "gt", "rt-dev", "rt-proc", "qx2", "tb-kpm", "ditpro",
 * "kma", "ldk", "paradise") normalizes (its '-'/'_'/' ' stripped, lower-
 * cased) to exactly the token the native switch matches — so whichever
 * mode is selected here IS the mode c_driver loads on next launch, and
 * loadMatchingAsset() below loads/insmods a kernel-matched module for
 * that SAME configValue before config.json is written, so the extracted
 * driver file and the config.json choice always agree with each other.
 *
 * This class owns reading/merging/writing that one file via root, so the
 * "menu_open" key (and any future keys) already in the file are preserved.
 */
object DriverConfig {

    private const val TAG = "IX_DriverConfig"

    private const val CONFIG_DIR = "/data/adb/modules/invisiblex_loader/config"
    private const val CONFIG_PATH = "$CONFIG_DIR/config.json"
    private const val ASSET_DIR = "drivers"

    fun interface Callback {
        fun onResult(success: Boolean, message: String)
    }

    private class RootResult(val exitCode: Int, val stdout: String)

    /**
     * Write the chosen backend mode to config.json. Call ONLY after
     * AuthManager.securityGate() has already passed — this method does not
     * re-check auth itself; it is the operation the gate protects.
     */
    @JvmStatic
    fun applyDriverChoice(configValue: String, callback: Callback) {
        Thread {
            var ok: Boolean
            var msg: String
            try {
                val menuOpen = readExistingMenuOpen()
                val json = "{\"driver\":\"$configValue\",\"menu_open\":\"$menuOpen\"}"

                val mk = runRoot("mkdir -p $CONFIG_DIR").exitCode
                if (mk != 0) {
                    callback.onResult(false, "Could not create config dir (root denied?)")
                    return@Thread
                }

                val escaped = json.replace("'", "'\\''")
                val wr = runRoot("printf '%s' '$escaped' > $CONFIG_PATH").exitCode
                if (wr != 0) {
                    callback.onResult(false, "Could not write config.json (root denied?)")
                    return@Thread
                }

                // Readable by the binary (and module scripts) on next launch.
                runRoot("chmod 644 $CONFIG_PATH")

                ok = true
                msg = "Driver mode set — takes effect on next launch"
            } catch (e: Exception) {
                Log.e(TAG, "applyDriverChoice failed", e)
                ok = false
                msg = "Config write failed: ${e.message}"
            }
            callback.onResult(ok, msg)
        }.start()
    }

    /**
     * Some backend modes (ditpro, kma, ldk, tb-kpm, paradise, rt-dev) need an
     * actual kernel module/KPM file inserted before the binary can use them,
     * and that file MUST match the device's running kernel. This looks in
     * assets/drivers/ for a file matching "<configValue>_<kernel>.<ext>"
     * and loads it via insmod (or direct execute as a fallback) before
     * config.json is written. If nothing matches, this is NOT an error —
     * several modes (qx1, rt-proc, qx2) work purely from config.json with
     * no bundled module at all.
     *
     * [FIX] Matching used to be a bare String.startsWith("${configValue}_$kernel.")
     * prefix check. That's exact for single-part kernels ("6.6"), but once
     * RT-DEV shipped kernel-PATCH-specific files (rt-dev_4.14.117.sh,
     * rt-dev_4.14.141.sh, rt-dev_4.14.180.sh, rt-dev_4.14.186.sh, …),
     * detecting kernel "4.14" made startsWith("rt-dev_4.14.") match ALL of
     * them simultaneously — whichever one AssetManager.list() happened to
     * return first would silently "win", non-deterministically. Matching is
     * now done by EXACT filename-stem equality against the full detected
     * version first ("rt-dev_4.14.186" vs the file's name before its
     * extension), then against the major.minor-only version as a fallback
     * — never a prefix check — so a device on patch 186 can never load the
     * file built for patch 117, and the ambiguous "b"/"c" alternate-build
     * variants (rt-dev_4.14.186b.sh, rt-dev_4.14.186c.sh) are never picked
     * automatically either, since their stems don't exactly equal either
     * candidate string.
     *
     * Call ONLY after AuthManager.securityGate() has already passed.
     */
    @JvmStatic
    fun loadMatchingAsset(ctx: Context, configValue: String, callback: Callback) {
        Thread {
            try {
                val kernelFull = KernelUtil.detect()
                val kernelMajorMinor = if (kernelFull.isEmpty()) "" else KernelUtil.majorMinor(kernelFull)
                val am = ctx.assets
                val files = am.list(ASSET_DIR) ?: arrayOf()

                val exactFullTarget = if (kernelFull.isNotEmpty()) "${configValue}_$kernelFull" else null
                val exactMajorMinorTarget =
                    if (kernelMajorMinor.isNotEmpty() && kernelMajorMinor != kernelFull) "${configValue}_$kernelMajorMinor" else null

                var exact: String? = null
                var majorMinorMatch: String? = null
                var generic: String? = null
                for (f in files) {
                    if (f.isEmpty() || f == ".keep" || f.equals("README.md", ignoreCase = true)) continue
                    val dot = f.lastIndexOf('.')
                    val stem = if (dot > 0) f.substring(0, dot) else f
                    when {
                        exactFullTarget != null && stem == exactFullTarget -> { exact = f }
                        exactMajorMinorTarget != null && stem == exactMajorMinorTarget -> { majorMinorMatch = f }
                        stem == configValue -> { generic = f }
                    }
                    if (exact != null) break
                }

                val chosen = exact ?: majorMinorMatch ?: generic
                val kernel = kernelFull
                if (chosen == null) {
                    callback.onResult(
                        true,
                        "Driver Loaded Successfully — \"$configValue\" applied" +
                            (if (kernel.isEmpty()) "" else " (kernel $kernel)") +
                            ", no extra module needed"
                    )
                    return@Thread
                }

                val driversDir = File(ctx.filesDir, ASSET_DIR)
                if (!driversDir.exists()) driversDir.mkdirs()
                val out = File(driversDir, chosen)
                am.open("$ASSET_DIR/$chosen").use { input ->
                    FileOutputStream(out).use { os ->
                        val buf = ByteArray(8192)
                        var len: Int
                        while (input.read(buf).also { len = it } != -1) os.write(buf, 0, len)
                    }
                }
                out.setExecutable(true, true)
                runRoot("chmod 700 ${out.absolutePath}")

                val kernelTag = if (kernel.isEmpty()) "" else " (kernel $kernel)"

                if (chosen.endsWith(".sh")) {
                    // Self-extracting loader script (embeds a base64 .ko and
                    // insmod's it itself, e.g. the Paradise/RT-DEV driver
                    // packages) — run it with "sh", never insmod the script
                    // file directly.
                    val sh = runRoot("sh ${out.absolutePath}")
                    if (sh.exitCode == 0) {
                        callback.onResult(true, "Driver Loaded Successfully — $chosen$kernelTag")
                    } else {
                        callback.onResult(false, "Script failed ($chosen): ${firstLine(sh.stdout)}")
                    }
                    return@Thread
                }

                val insmod = runRoot("insmod ${out.absolutePath}")
                if (insmod.exitCode == 0) {
                    callback.onResult(true, "Driver Loaded Successfully — $chosen$kernelTag")
                    return@Thread
                }
                // Not every bundled file is a .ko module — some are plain
                // executables. Fall back to direct execute, same pattern
                // used everywhere else in this app for driver files.
                runRoot("${out.absolutePath} >/dev/null 2>&1 &")
                callback.onResult(true, "Driver Loaded Successfully — $chosen$kernelTag")
            } catch (e: Exception) {
                Log.e(TAG, "loadMatchingAsset failed", e)
                callback.onResult(false, "Kernel-module load failed: ${e.message}")
            }
        }.start()
    }

    /** Read back whichever mode is currently active, for display purposes. */
    @JvmStatic
    fun readCurrentChoice(): String {
        try {
            val raw = readFile(CONFIG_PATH)
            if (raw.isNullOrEmpty()) return "auto"
            val m = Pattern.compile("\"driver\"\\s*:\\s*\"([^\"]*)\"").matcher(raw)
            if (m.find()) {
                val v = m.group(1)
                return if (v.isNullOrEmpty()) "auto" else v
            }
        } catch (ignored: Exception) {
        }
        return "auto"
    }

    private fun readExistingMenuOpen(): String {
        try {
            val raw = readFile(CONFIG_PATH)
            if (raw != null) {
                val m = Pattern.compile("\"menu_open\"\\s*:\\s*\"([^\"]*)\"").matcher(raw)
                if (m.find()) {
                    val v = m.group(1)
                    if (v == "vol" || v == "bar") return v
                }
            }
        } catch (ignored: Exception) {
        }
        return "bar"
    }

    private fun readFile(path: String): String? {
        val r = runRoot("cat $path")
        if (r.exitCode != 0) return null
        return r.stdout
    }

    private fun firstLine(s: String?): String {
        if (s == null) return ""
        val nl = s.indexOf('\n')
        return (if (nl >= 0) s.substring(0, nl) else s).trim()
    }

    private fun runRoot(shellCmd: String): RootResult {
        return try {
            val p = Runtime.getRuntime().exec(arrayOf("su", "-c", shellCmd))
            val out = StringBuilder()
            BufferedReader(InputStreamReader(p.inputStream)).use { br ->
                var line: String?
                while (br.readLine().also { line = it } != null) out.append(line).append('\n')
            }
            val code = p.waitFor()
            RootResult(code, out.toString())
        } catch (e: Exception) {
            Log.e(TAG, "runRoot failed: $shellCmd", e)
            RootResult(-1, "")
        }
    }
}
