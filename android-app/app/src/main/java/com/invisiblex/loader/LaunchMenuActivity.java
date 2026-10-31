package com.invisiblex.loader;

import android.app.Activity;
import android.app.StatusBarManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;
import android.view.animation.TranslateAnimation;
import android.widget.Button;
import android.widget.ListView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * LaunchMenuActivity — post-launch overlay menu.
 *
 * Shown immediately after the InvisibleX engine executes successfully
 * (called from InvisibleXTileService or DriverFragment after BinaryExecutor succeeds).
 *
 * Features:
 *   1. Engine status indicator + saved session info (key name, expiry).
 *   2. Full driver list with permission chips — same model as DriverFragment.
 *   3. "Load Driver" — with auth gate before insmod / execute.
 *   4. "Merge Tile" — merge InvisibleX tile next to another quick-settings tile (Android 13+).
 *   5. "Hide Icon" — toggle launcher visibility.
 *   6. "Close" — dismiss overlay, binary stays running in background.
 *
 * Security contract (NEVER bypass):
 *   - Auth gate (AuthManager.securityGate) is ALWAYS called before driver load.
 *   - If session is invalid the overlay shows error and driver load is blocked.
 *   - This activity must never weaken the dual auth check enforced everywhere else.
 */
public class LaunchMenuActivity extends Activity {

    private static final String TAG       = "IX_LaunchMenu";
    private static final String ASSET_DIR = "drivers";

    private ListView        lvDrivers;
    private TextView        tvNoDrivers;
    private Button          btnLoadDriver;
    private TextView        tvDriverStatus;
    private TextView        tvEngineStatus;
    private TextView        tvMenuKey;
    private TextView        tvMenuExpiry;
    private Button          btnMergeTile;
    private Button          btnHideIcon;
    private Button          btnClose;
    private TextView        tvTileMergeStatus;
    private View            panelMenu;

    private DriverAdapter       adapter;
    private final List<DriverInfo> driverList = new ArrayList<>();
    private String              selectedDriver;
    private final ExecutorService executor   = Executors.newSingleThreadExecutor();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Transparent overlay — show above everything including lock screen edge
        getWindow().addFlags(
            WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN |
            WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL);
        setContentView(R.layout.activity_launch_menu);

        bindViews();
        setupDismiss();
        populateSessionInfo();
        loadDriverList();
        setupActions();
        animatePanelIn();
    }

    // ── View binding ──────────────────────────────────────────

    private void bindViews() {
        panelMenu         = findViewById(R.id.panel_menu);
        tvEngineStatus    = findViewById(R.id.tv_engine_status);
        tvMenuKey         = findViewById(R.id.tv_menu_key);
        tvMenuExpiry      = findViewById(R.id.tv_menu_expiry);
        lvDrivers         = findViewById(R.id.lv_menu_drivers);
        tvNoDrivers       = findViewById(R.id.tv_menu_no_drivers);
        btnLoadDriver     = findViewById(R.id.btn_menu_load_driver);
        tvDriverStatus    = findViewById(R.id.tv_menu_driver_status);
        btnMergeTile      = findViewById(R.id.btn_menu_merge_tile);
        btnHideIcon       = findViewById(R.id.btn_menu_hide_icon);
        btnClose          = findViewById(R.id.btn_menu_close);
        tvTileMergeStatus = findViewById(R.id.tv_tile_merge_status);
    }

    private void setupDismiss() {
        // Tap outside the panel to close
        findViewById(R.id.touch_dismiss).setOnClickListener(v -> finish());
        btnClose.setOnClickListener(v -> finish());
    }

    // ── Session info ──────────────────────────────────────────

    private void populateSessionInfo() {
        boolean valid = AuthManager.get().isAuthenticated();

        tvEngineStatus.setText(valid ? "Engine Active" : "Auth Required");
        tvEngineStatus.setTextColor(getColor(valid ? R.color.status_ok : R.color.status_fail));

        String keyName = AppPrefs.getKeyName();
        tvMenuKey.setText("Key: " + (keyName.isEmpty() ? "—" : keyName));

        String expiry = AppPrefs.getExpiryDate();
        tvMenuExpiry.setText("Expires: " + (expiry.isEmpty() ? "—" : expiry));
    }

    // ── Driver list ───────────────────────────────────────────

    private void loadDriverList() {
        try {
            AssetManager am = getAssets();
            String[] files  = am.list(ASSET_DIR);
            driverList.clear();

            if (files == null || files.length == 0) {
                showNoDrivers();
                return;
            }

            for (String f : files) {
                if (f.equals(".keep") || f.isEmpty()) continue;
                long size = 0;
                try {
                    InputStream is = am.open(ASSET_DIR + "/" + f);
                    size = is.available();
                    is.close();
                } catch (Exception ignored) {}
                driverList.add(new DriverInfo(f, size));
            }

            if (driverList.isEmpty()) {
                showNoDrivers();
                return;
            }

            selectedDriver = AppPrefs.getSelectedDriver();
            tvNoDrivers.setVisibility(View.GONE);
            lvDrivers.setVisibility(View.VISIBLE);

            adapter = new DriverAdapter(this, driverList, selectedDriver);
            lvDrivers.setAdapter(adapter);

            lvDrivers.setOnItemClickListener((parent, v, position, id) -> {
                DriverInfo info = driverList.get(position);
                selectedDriver = info.name;
                AppPrefs.setSelectedDriver(selectedDriver);
                adapter.setSelected(selectedDriver);
                adapter.notifyDataSetChanged();
                btnLoadDriver.setEnabled(true);
                tvDriverStatus.setVisibility(View.GONE);
            });

            // Re-select previously saved driver
            if (selectedDriver != null && !selectedDriver.isEmpty()) {
                btnLoadDriver.setEnabled(true);
            }

        } catch (IOException e) {
            Log.e(TAG, "Failed to list drivers", e);
            showNoDrivers();
        }
    }

    private void showNoDrivers() {
        tvNoDrivers.setVisibility(View.VISIBLE);
        lvDrivers.setVisibility(View.GONE);
        btnLoadDriver.setEnabled(false);
    }

    // ── Button actions ────────────────────────────────────────

    private void setupActions() {
        btnLoadDriver.setOnClickListener(v -> attemptLoadDriver());
        btnMergeTile.setOnClickListener(v -> mergeTile());
        btnHideIcon.setOnClickListener(v -> toggleHideIcon());

        // Reflect current hide state
        boolean hidden = AppPrefs.isAppHidden();
        btnHideIcon.setText(hidden ? "SHOW ICON" : "HIDE ICON");
    }

    // ── Driver load — full security gate ─────────────────────

    private void attemptLoadDriver() {
        if (selectedDriver == null || selectedDriver.isEmpty()) {
            showDriverStatus("Select a driver first", false);
            return;
        }

        btnLoadDriver.setEnabled(false);
        showDriverStatus("Verifying authentication…", null);

        // Security gate — mandatory before every binary/driver operation
        AuthManager.get().securityGate(this, (authOk, authMsg) -> {
            if (!authOk) {
                showDriverStatus("Auth failed — " + authMsg, false);
                btnLoadDriver.setEnabled(true);
                return;
            }
            // Auth passed
            executor.execute(this::extractAndLoadDriver);
        });
    }

    private void extractAndLoadDriver() {
        String name = selectedDriver;
        if (name == null || name.isEmpty()) return;

        try {
            File out = extractDriver(name);
            if (out == null) {
                postDriverStatus("Extract failed", false);
                return;
            }

            out.setExecutable(true, true);
            Runtime.getRuntime()
                .exec(new String[]{"chmod", "700", out.getAbsolutePath()})
                .waitFor();

            // Try insmod first (kernel modules)
            Process proc = Runtime.getRuntime().exec(
                new String[]{"su", "-c", "insmod " + out.getAbsolutePath()});
            int code = proc.waitFor();

            if (code == 0) {
                postDriverStatus("Driver loaded: " + name, true);
            } else {
                // Fallback: direct execute (plain binary drivers)
                ProcessBuilder pb = new ProcessBuilder(out.getAbsolutePath());
                pb.redirectErrorStream(true);
                pb.start();
                postDriverStatus("Driver executed: " + name, true);
            }
        } catch (IOException | InterruptedException e) {
            Log.e(TAG, "Driver load error", e);
            postDriverStatus("Load failed: " + e.getMessage(), false);
        }

        runOnUiThread(() -> btnLoadDriver.setEnabled(true));
    }

    private File extractDriver(String name) throws IOException {
        File driversDir = new File(getFilesDir(), "drivers");
        if (!driversDir.exists()) driversDir.mkdirs();

        File out = new File(driversDir, name);
        try (InputStream in = getAssets().open(ASSET_DIR + "/" + name);
             OutputStream os = new FileOutputStream(out)) {
            byte[] buf = new byte[8192];
            int len;
            while ((len = in.read(buf)) != -1) os.write(buf, 0, len);
            os.flush();
        }
        return out;
    }

    // ── Tile merge ────────────────────────────────────────────

    /**
     * Merge tile — adds InvisibleX to the quick-settings panel.
     * On Android 13+ uses StatusBarManager.requestAddTileService
     * (the same as HomeFragment but exposed from the post-launch overlay too).
     * User can then drag it next to any other tile they use frequently.
     */
    private void mergeTile() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            StatusBarManager sbm =
                (StatusBarManager) getSystemService(Context.STATUS_BAR_SERVICE);
            if (sbm == null) {
                showTileStatus("Quick Settings not available", false);
                return;
            }
            sbm.requestAddTileService(
                new ComponentName(this, InvisibleXTileService.class),
                getString(R.string.tile_label),
                android.graphics.drawable.Icon.createWithResource(this, R.drawable.ic_tile),
                getMainExecutor(),
                resultCode -> {
                    switch (resultCode) {
                        case StatusBarManager.TILE_ADD_REQUEST_RESULT_TILE_ADDED:
                            showTileStatus("Tile added — drag to position it next to another tile", true);
                            break;
                        case StatusBarManager.TILE_ADD_REQUEST_RESULT_TILE_ALREADY_ADDED:
                            showTileStatus("Tile already added — open Quick Settings edit to reorder", true);
                            break;
                        default:
                            showTileStatus("Could not add tile — add manually from Quick Settings edit", false);
                    }
                }
            );
        } else {
            // Android 7–12: manual drag required
            showTileStatus("Open Quick Settings, tap edit, then drag InvisibleX tile to position", null);
        }
    }

    // ── Hide / Show icon ──────────────────────────────────────

    private void toggleHideIcon() {
        boolean currentlyHidden = AppPrefs.isAppHidden();
        boolean wantVisible     = currentlyHidden; // toggle

        PackageManager pm       = getPackageManager();
        ComponentName launcher  = new ComponentName(this, MainActivity.class);
        int state = wantVisible
            ? PackageManager.COMPONENT_ENABLED_STATE_ENABLED
            : PackageManager.COMPONENT_ENABLED_STATE_DISABLED;

        try {
            pm.setComponentEnabledSetting(launcher, state, PackageManager.DONT_KILL_APP);
            AppPrefs.setAppHidden(!wantVisible);
            btnHideIcon.setText(wantVisible ? "HIDE ICON" : "SHOW ICON");
            showTileStatus(wantVisible ? "App icon visible" : "App icon hidden", true);
        } catch (Exception e) {
            showTileStatus("Failed — root may be required", false);
        }
    }

    // ── UI helpers ────────────────────────────────────────────

    private void showDriverStatus(String msg, Boolean ok) {
        tvDriverStatus.setVisibility(View.VISIBLE);
        tvDriverStatus.setText(msg);
        if (ok == null) tvDriverStatus.setTextColor(getColor(R.color.text_secondary));
        else tvDriverStatus.setTextColor(getColor(ok ? R.color.status_ok : R.color.status_fail));
    }

    private void postDriverStatus(String msg, boolean ok) {
        runOnUiThread(() -> showDriverStatus(msg, ok));
    }

    private void showTileStatus(String msg, Boolean ok) {
        tvTileMergeStatus.setVisibility(View.VISIBLE);
        tvTileMergeStatus.setText(msg);
        if (ok == null) tvTileMergeStatus.setTextColor(getColor(R.color.text_secondary));
        else tvTileMergeStatus.setTextColor(getColor(ok ? R.color.status_ok : R.color.status_fail));
    }

    // ── Slide-up animation ────────────────────────────────────

    private void animatePanelIn() {
        panelMenu.post(() -> {
            TranslateAnimation anim = new TranslateAnimation(
                0, 0, panelMenu.getHeight(), 0);
            anim.setDuration(280);
            anim.setInterpolator(new android.view.animation.DecelerateInterpolator(2f));
            panelMenu.startAnimation(anim);
        });
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        executor.shutdown();
    }
}
