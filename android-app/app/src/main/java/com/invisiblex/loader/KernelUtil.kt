package com.invisiblex.loader

import android.util.Log
import java.io.BufferedReader
import java.io.InputStreamReader
import java.util.regex.Pattern

/**
 * KernelUtil — detects the device's running kernel version (e.g. "6.6",
 * "5.15") so the Driver section can pick the asset file built for THIS
 * device instead of a generic one.
 */
object KernelUtil {

    private const val TAG = "IX_Kernel"

    // Full patch version first (e.g. "4.14.186"), major.minor as a fallback
    // capture group (e.g. "4.14") for devices whose uname has no 3rd part.
    private val VERSION: Pattern = Pattern.compile("^(\\d+\\.\\d+)(?:\\.(\\d+))?")

    /**
     * Returns the fullest kernel version string available, e.g. "4.14.186"
     * when a patch level is present, or "6.6" when it isn't. Falls back to
     * "" if it can't be read (caller should treat that as "unknown — use
     * the generic/unversioned asset instead").
     *
     * [FIX] Previously this only ever returned major.minor ("4.14"), which
     * is ambiguous against the RT-DEV asset set: multiple bundled files
     * ("rt-dev_4.14.117.sh", "rt-dev_4.14.141.sh", "rt-dev_4.14.180.sh",
     * "rt-dev_4.14.186.sh", …) all share that same major.minor. Capturing
     * the patch level too lets DriverConfig.loadMatchingAsset() match the
     * SPECIFIC file for this device instead of picking an arbitrary one.
     */
    @JvmStatic
    fun detect(): String {
        val raw = System.getProperty("os.version") // JVM-level uname -r equivalent
        if (raw != null) {
            full(raw)?.let { return it }
        }
        // Fallback: actually run uname -r (no root needed — it's a public syscall)
        try {
            val p = Runtime.getRuntime().exec(arrayOf("uname", "-r"))
            val br = BufferedReader(InputStreamReader(p.inputStream))
            val line = br.readLine()
            p.waitFor()
            if (line != null) {
                full(line)?.let { return it }
            }
        } catch (e: Exception) {
            Log.w(TAG, "uname -r failed: " + e.message)
        }
        return ""
    }

    /** Returns "major.minor.patch" if a patch level was found, else "major.minor", else null. */
    private fun full(raw: String): String? {
        val m = VERSION.matcher(raw.trim())
        if (!m.find()) return null
        val majorMinor = m.group(1) ?: return null
        val patch = m.group(2)
        return if (patch != null) "$majorMinor.$patch" else majorMinor
    }

    /** "4.14.186" → "4.14". Used by callers that want to fall back to the major.minor-only asset. */
    @JvmStatic
    fun majorMinor(fullVersion: String): String {
        val parts = fullVersion.split(".")
        return if (parts.size >= 2) "${parts[0]}.${parts[1]}" else fullVersion
    }
}
