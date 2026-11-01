package com.invisiblex.loader;

import android.app.Activity;
import android.app.StatusBarManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;
import android.view.animation.TranslateAnimation;
import android.widget.Button;
import android.widget.ListView;
import android.widget.TextView;

import java.util.List;

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

    private static final String TAG = "IX_LaunchMenu";

    private ListView        lvDrivers;
    private TextView        tvNoDrivers;
    private Button          btnLoadDriver;
    private TextView        tvDriverStatus;
    private TextView        tvEngineStatus;
    private TextView        tvMenuKey;
    private TextView        tvMenuExpiry;
    private TextView        tvKernelVersion;
    private Button          btnMergeTile;
    private Button          btnHideIcon;
    private Button          btnClose;
    private TextView        tvTileMergeStatus;
    private View            panelMenu;

    private DriverAdapter       adapter;
    private final List<DriverInfo> driverList = DriverInfo.wanbaiModes();
    private DriverInfo          selectedMode;

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
        showKernelVersion();
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
        tvKernelVersion   = findViewById(R.id.tv_kernel_version);
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

    // ── Kernel version ──────────────────────────────────────────

    private void showKernelVersion() {
        new Thread(() -> {
            String kernel = KernelUtil.detect();
            runOnUiThread(() -> tvKernelVersion.setText(
                "Kernel: " + (kernel.isEmpty() ? "unknown" : kernel)));
        }).start();
    }

    // ── Driver list ───────────────────────────────────────────

    private void loadDriverList() {
        String savedValue = AppPrefs.getSelectedDriver();
        if (savedValue == null || savedValue.isEmpty()) {
            savedValue = DriverConfig.readCurrentChoice();
        }
        selectedMode = findByConfigValue(savedValue);

        tvNoDrivers.setVisibility(View.GONE);
        lvDrivers.setVisibility(View.VISIBLE);

        adapter = new DriverAdapter(this, driverList, selectedMode.configValue);
        lvDrivers.setAdapter(adapter);

        lvDrivers.setOnItemClickListener((parent, v, position, id) -> {
            selectedMode = driverList.get(position);
            AppPrefs.setSelectedDriver(selectedMode.configValue);
            adapter.setSelected(selectedMode.configValue);
            adapter.notifyDataSetChanged();
            btnLoadDriver.setEnabled(true);
            tvDriverStatus.setVisibility(View.GONE);
        });

        btnLoadDriver.setEnabled(true);
    }

    private DriverInfo findByConfigValue(String value) {
        if (value != null) {
            for (DriverInfo info : driverList) {
                if (info.configValue.equals(value)) return info;
            }
        }
        return driverList.get(0); // AUTO-DETECT
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
        if (selectedMode == null) {
            showDriverStatus("Select a driver first", false);
            return;
        }

        btnLoadDriver.setEnabled(false);
        showDriverStatus("Verifying authentication…", null);

        // Security gate — mandatory before every binary/driver operation.
        // NEVER bypass this, regardless of how the overlay is reached.
        AuthManager.get().securityGate(this, (authOk, authMsg) -> {
            if (!authOk) {
                showDriverStatus("Auth failed — " + authMsg, false);
                btnLoadDriver.setEnabled(true);
                return;
            }
            // Auth passed — write the chosen backend, then load the
            // kernel-matched module for it (if one is bundled).
            String configValue = selectedMode.configValue;
            DriverConfig.applyDriverChoice(configValue, (configOk, configMsg) -> {
                if (!configOk) {
                    runOnUiThread(() -> {
                        postDriverStatus(configMsg, false);
                        btnLoadDriver.setEnabled(true);
                    });
                    return;
                }
                DriverConfig.loadMatchingAsset(getApplicationContext(), configValue,
                    (assetOk, assetMsg) -> runOnUiThread(() -> {
                        postDriverStatus(assetOk ? assetMsg : configMsg + " — " + assetMsg, assetOk);
                        btnLoadDriver.setEnabled(true);
                    }));
            });
        });
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
    }
}
