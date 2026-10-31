package com.invisiblex.loader;

import android.util.Log;

import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * KernelUtil — detects the device's running kernel version (e.g. "6.6",
 * "5.15") so the Driver section can pick the asset file built for THIS
 * device instead of a generic one.
 */
public final class KernelUtil {

    private static final String TAG = "IX_Kernel";
    private static final Pattern VERSION = Pattern.compile("^(\\d+\\.\\d+)");

    private KernelUtil() {}

    /**
     * Returns the major.minor kernel version, e.g. "6.6", "5.15", "4.19".
     * Falls back to "" if it can't be read (caller should treat that as
     * "unknown — use the generic/unversioned asset instead").
     */
    public static String detect() {
        String raw = System.getProperty("os.version"); // JVM-level uname -r equivalent
        if (raw != null) {
            Matcher m = VERSION.matcher(raw.trim());
            if (m.find()) return m.group(1);
        }
        // Fallback: actually run uname -r (no root needed — it's a public syscall)
        try {
            Process p = Runtime.getRuntime().exec(new String[]{"uname", "-r"});
            java.io.BufferedReader br = new java.io.BufferedReader(
                new java.io.InputStreamReader(p.getInputStream()));
            String line = br.readLine();
            p.waitFor();
            if (line != null) {
                Matcher m = VERSION.matcher(line.trim());
                if (m.find()) return m.group(1);
            }
        } catch (Exception e) {
            Log.w(TAG, "uname -r failed: " + e.getMessage());
        }
        return "";
    }
}
