package com.invisiblex.loader;

import android.util.Log;

import java.io.BufferedReader;
import java.io.InputStreamReader;
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
