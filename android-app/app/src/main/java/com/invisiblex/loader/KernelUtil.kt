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
    private val VERSION: Pattern = Pattern.compile("^(\\d+\\.\\d+)")

    /**
     * Returns the major.minor kernel version, e.g. "6.6", "5.15", "4.19".
     * Falls back to "" if it can't be read (caller should treat that as
     * "unknown — use the generic/unversioned asset instead").
     */
    @JvmStatic
    fun detect(): String {
        val raw = System.getProperty("os.version") // JVM-level uname -r equivalent
        if (raw != null) {
            val m = VERSION.matcher(raw.trim())
            if (m.find()) return m.group(1)
        }
        // Fallback: actually run uname -r (no root needed — it's a public syscall)
        try {
            val p = Runtime.getRuntime().exec(arrayOf("uname", "-r"))
            val br = BufferedReader(InputStreamReader(p.inputStream))
            val line = br.readLine()
            p.waitFor()
            if (line != null) {
                val m = VERSION.matcher(line.trim())
                if (m.find()) return m.group(1)
            }
        } catch (e: Exception) {
            Log.w(TAG, "uname -r failed: " + e.message)
        }
        return ""
    }
}
