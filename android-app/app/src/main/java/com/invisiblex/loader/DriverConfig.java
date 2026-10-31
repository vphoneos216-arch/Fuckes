package com.invisiblex.loader;

import android.content.Context;
import android.content.res.AssetManager;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

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
 * This class owns reading/merging/writing that one file via root, so the
 * "menu_open" key (and any future keys) already in the file are preserved.
 */
public final class DriverConfig {

    private static final String TAG = "IX_DriverConfig";

    private static final String CONFIG_DIR  = "/data/adb/modules/invisiblex_loader/config";
    private static final String CONFIG_PATH = CONFIG_DIR + "/config.json";

    private DriverConfig() {}

    public interface Callback {
        void onResult(boolean success, String message);
    }

    /**
     * Write the chosen backend mode to config.json. Call ONLY after
     * AuthManager.securityGate() has already passed — this method does not
     * re-check auth itself; it is the operation the gate protects.
     */
    public static void applyDriverChoice(String configValue, Callback callback) {
        new Thread(() -> {
            boolean ok;
            String msg;
            try {
                String menuOpen = readExistingMenuOpen();
                String json = "{\"driver\":\"" + configValue + "\",\"menu_open\":\"" + menuOpen + "\"}";

                int mk = runRoot("mkdir -p " + CONFIG_DIR).exitCode;
                if (mk != 0) {
                    callback.onResult(false, "Could not create config dir (root denied?)");
                    return;
                }

                String escaped = json.replace("'", "'\\''");
                int wr = runRoot("printf '%s' '" + escaped + "' > " + CONFIG_PATH).exitCode;
                if (wr != 0) {
                    callback.onResult(false, "Could not write config.json (root denied?)");
                    return;
                }

                // Readable by the binary (and module scripts) on next launch.
                runRoot("chmod 644 " + CONFIG_PATH);

                ok = true;
                msg = "Driver mode set — takes effect on next launch";
            } catch (Exception e) {
                Log.e(TAG, "applyDriverChoice failed", e);
                ok = false;
                msg = "Config write failed: " + e.getMessage();
            }
            callback.onResult(ok, msg);
        }).start();
    }

    private static final String ASSET_DIR = "drivers";

    /**
     * Some backend modes (ditpro, kma, ldk, tb-kpm, paradise) need an actual
     * kernel module/KPM file inserted before the binary can use them, and
     * that file MUST match the device's running kernel. This looks in
     * assets/drivers/ for a file matching "<configValue>_<kernel>.<ext>"
     * (e.g. "paradise_6.6.ko" on a 6.6 kernel), falling back to a generic
     * "<configValue>.<ext>" (no kernel suffix) if present, and loads it via
     * insmod (or direct execute as a fallback) before config.json is
     * written. If nothing matches, this is NOT an error — several modes
     * (auto, qx1, gt, rt-dev, rt-proc, qx2) work purely from config.json
     * with no bundled module at all.
     *
     * Call ONLY after AuthManager.securityGate() has already passed.
     */
    public static void loadMatchingAsset(Context ctx, String configValue, Callback callback) {
        new Thread(() -> {
            try {
                String kernel = KernelUtil.detect();
                AssetManager am = ctx.getAssets();
                String[] files = am.list(ASSET_DIR);
                if (files == null) files = new String[0];

                String exact = null, generic = null;
                for (String f : files) {
                    if (f.isEmpty() || f.equals(".keep") || f.equalsIgnoreCase("README.md")) continue;
                    if (!kernel.isEmpty() && f.startsWith(configValue + "_" + kernel + ".")) {
                        exact = f;
                        break;
                    }
                    if (f.startsWith(configValue + ".")) generic = f;
                }

                String chosen = exact != null ? exact : generic;
                if (chosen == null) {
                    callback.onResult(true, "No bundled kernel module for \"" + configValue + "\""
                        + (kernel.isEmpty() ? "" : " on kernel " + kernel)
                        + " — config.json alone will drive it");
                    return;
                }

                File driversDir = new File(ctx.getFilesDir(), ASSET_DIR);
                if (!driversDir.exists()) driversDir.mkdirs();
                File out = new File(driversDir, chosen);
                try (InputStream in = am.open(ASSET_DIR + "/" + chosen);
                     OutputStream os = new FileOutputStream(out)) {
                    byte[] buf = new byte[8192];
                    int len;
                    while ((len = in.read(buf)) != -1) os.write(buf, 0, len);
                }
                out.setExecutable(true, true);
                runRoot("chmod 700 " + out.getAbsolutePath());

                RootResult insmod = runRoot("insmod " + out.getAbsolutePath());
                if (insmod.exitCode == 0) {
                    callback.onResult(true, "Loaded " + chosen
                        + (kernel.isEmpty() ? "" : " (kernel " + kernel + ")"));
                    return;
                }
                // Not every bundled file is a .ko module — some are plain
                // executables. Fall back to direct execute, same pattern
                // used everywhere else in this app for driver files.
                runRoot(out.getAbsolutePath() + " >/dev/null 2>&1 &");
                callback.onResult(true, "Executed " + chosen
                    + (kernel.isEmpty() ? "" : " (kernel " + kernel + ")"));

            } catch (Exception e) {
                Log.e(TAG, "loadMatchingAsset failed", e);
                callback.onResult(false, "Kernel-module load failed: " + e.getMessage());
            }
        }).start();
    }

    /** Read back whichever mode is currently active, for display purposes. */
    public static String readCurrentChoice() {
        try {
            String raw = readFile(CONFIG_PATH);
            if (raw == null || raw.isEmpty()) return "auto";
            Matcher m = Pattern.compile("\"driver\"\\s*:\\s*\"([^\"]*)\"").matcher(raw);
            if (m.find()) {
                String v = m.group(1);
                return v == null || v.isEmpty() ? "auto" : v;
            }
        } catch (Exception ignored) {}
        return "auto";
    }

    private static String readExistingMenuOpen() {
        try {
            String raw = readFile(CONFIG_PATH);
            if (raw != null) {
                Matcher m = Pattern.compile("\"menu_open\"\\s*:\\s*\"([^\"]*)\"").matcher(raw);
                if (m.find()) {
                    String v = m.group(1);
                    if ("vol".equals(v) || "bar".equals(v)) return v;
                }
            }
        } catch (Exception ignored) {}
        return "bar";
    }

    private static String readFile(String path) {
        RootResult r = runRoot("cat " + path);
        if (r.exitCode != 0) return null;
        return r.stdout;
    }

    private static RootResult runRoot(String shellCmd) {
        try {
            Process p = Runtime.getRuntime().exec(new String[]{"su", "-c", shellCmd});
            StringBuilder out = new StringBuilder();
            try (BufferedReader br = new BufferedReader(new InputStreamReader(p.getInputStream()))) {
                String line;
                while ((line = br.readLine()) != null) out.append(line).append('\n');
            }
            int code = p.waitFor();
            return new RootResult(code, out.toString());
        } catch (Exception e) {
            Log.e(TAG, "runRoot failed: " + shellCmd, e);
            return new RootResult(-1, "");
        }
    }

    private static final class RootResult {
        final int exitCode;
        final String stdout;
        RootResult(int exitCode, String stdout) {
            this.exitCode = exitCode;
            this.stdout = stdout;
        }
    }
}
