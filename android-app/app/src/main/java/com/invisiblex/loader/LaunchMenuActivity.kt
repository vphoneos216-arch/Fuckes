package com.invisiblex.loader

import android.app.Activity
import android.app.StatusBarManager
import android.content.ComponentName
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.drawable.Icon
import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.WindowManager
import android.view.animation.DecelerateInterpolator
import android.view.animation.TranslateAnimation
import android.widget.Button
import android.widget.ListView
import android.widget.TextView

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
class LaunchMenuActivity : Activity() {

    private lateinit var lvDrivers: ListView
    private lateinit var tvNoDrivers: TextView
    private lateinit var btnLoadDriver: Button
    private lateinit var tvDriverStatus: TextView
    private lateinit var tvEngineStatus: TextView
    private lateinit var tvMenuKey: TextView
    private lateinit var tvMenuExpiry: TextView
    private lateinit var tvKernelVersion: TextView
    private lateinit var btnMergeTile: Button
    private lateinit var btnHideIcon: Button
    private lateinit var btnClose: Button
    private lateinit var tvTileMergeStatus: TextView
    private lateinit var panelMenu: View

    private lateinit var adapter: DriverAdapter
    private val driverList: List<DriverInfo> = DriverInfo.wanbaiModes()
    private var selectedMode: DriverInfo? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Transparent overlay — show above everything including lock screen edge
        window.addFlags(
            WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
        )
        setContentView(R.layout.activity_launch_menu)

        bindViews()
        setupDismiss()
        populateSessionInfo()
        showKernelVersion()
        loadDriverList()
        setupActions()
        animatePanelIn()
    }

    // ── View binding ──────────────────────────────────────────

    private fun bindViews() {
        panelMenu = findViewById(R.id.panel_menu)
        tvEngineStatus = findViewById(R.id.tv_engine_status)
        tvMenuKey = findViewById(R.id.tv_menu_key)
        tvMenuExpiry = findViewById(R.id.tv_menu_expiry)
        tvKernelVersion = findViewById(R.id.tv_kernel_version)
        lvDrivers = findViewById(R.id.lv_menu_drivers)
        tvNoDrivers = findViewById(R.id.tv_menu_no_drivers)
        btnLoadDriver = findViewById(R.id.btn_menu_load_driver)
        tvDriverStatus = findViewById(R.id.tv_menu_driver_status)
        btnMergeTile = findViewById(R.id.btn_menu_merge_tile)
        btnHideIcon = findViewById(R.id.btn_menu_hide_icon)
        btnClose = findViewById(R.id.btn_menu_close)
        tvTileMergeStatus = findViewById(R.id.tv_tile_merge_status)
    }

    private fun setupDismiss() {
        // Tap outside the panel to close
        findViewById<View>(R.id.touch_dismiss).setOnClickListener { finish() }
        btnClose.setOnClickListener { finish() }
    }

    // ── Session info ──────────────────────────────────────────

    private fun populateSessionInfo() {
        val valid = AuthManager.get().isAuthenticated()

        tvEngineStatus.text = if (valid) "Engine Active" else "Auth Required"
        tvEngineStatus.setTextColor(getColor(if (valid) R.color.status_ok else R.color.status_fail))

        val keyName = AppPrefs.getKeyName()
        tvMenuKey.text = "Key: " + keyName.ifEmpty { "—" }

        val expiry = AppPrefs.getExpiryDate()
        tvMenuExpiry.text = "Expires: " + expiry.ifEmpty { "—" }
    }

    // ── Kernel version ──────────────────────────────────────────

    private fun showKernelVersion() {
        Thread {
            val kernel = KernelUtil.detect()
            runOnUiThread {
                tvKernelVersion.text = "Kernel: " + kernel.ifEmpty { "unknown" }
            }
        }.start()
    }

    // ── Driver list ───────────────────────────────────────────

    private fun loadDriverList() {
        var savedValue = AppPrefs.getSelectedDriver()
        if (savedValue.isEmpty()) {
            savedValue = DriverConfig.readCurrentChoice()
        }
        val mode = findByConfigValue(savedValue)
        selectedMode = mode

        tvNoDrivers.visibility = View.GONE
        lvDrivers.visibility = View.VISIBLE

        adapter = DriverAdapter(this, driverList, mode.configValue)
        lvDrivers.adapter = adapter

        lvDrivers.setOnItemClickListener { _, _, position, _ ->
            val picked = driverList[position]
            selectedMode = picked
            AppPrefs.setSelectedDriver(picked.configValue)
            adapter.setSelected(picked.configValue)
            adapter.notifyDataSetChanged()
            btnLoadDriver.isEnabled = true
            tvDriverStatus.visibility = View.GONE
        }

        btnLoadDriver.isEnabled = true
    }

    private fun findByConfigValue(value: String?): DriverInfo {
        if (value != null) {
            driverList.firstOrNull { it.configValue == value }?.let { return it }
        }
        return driverList[0] // AUTO-DETECT
    }

    // ── Button actions ────────────────────────────────────────

    private fun setupActions() {
        btnLoadDriver.setOnClickListener { attemptLoadDriver() }
        btnMergeTile.setOnClickListener { mergeTile() }
        btnHideIcon.setOnClickListener { toggleHideIcon() }

        // Reflect current hide state
        val hidden = AppPrefs.isAppHidden()
        btnHideIcon.text = if (hidden) "SHOW ICON" else "HIDE ICON"
    }

    // ── Driver load — full security gate ─────────────────────

    private fun attemptLoadDriver() {
        val mode = selectedMode
        if (mode == null) {
            showDriverStatus("Select a driver first", false)
            return
        }

        btnLoadDriver.isEnabled = false
        showDriverStatus("Verifying authentication…", null)

        // Security gate — mandatory before every binary/driver operation.
        // NEVER bypass this, regardless of how the overlay is reached.
        AuthManager.get().securityGate(this) { authOk, authMsg ->
            if (!authOk) {
                showDriverStatus("Auth failed — $authMsg", false)
                btnLoadDriver.isEnabled = true
                return@securityGate
            }
            // Auth passed — write the chosen backend, then load the
            // kernel-matched module for it (if one is bundled).
            val configValue = mode.configValue
            DriverConfig.applyDriverChoice(configValue) { configOk, configMsg ->
                if (!configOk) {
                    runOnUiThread {
                        postDriverStatus(configMsg, false)
                        btnLoadDriver.isEnabled = true
                    }
                    return@applyDriverChoice
                }
                DriverConfig.loadMatchingAsset(applicationContext, configValue) { assetOk, assetMsg ->
                    runOnUiThread {
                        postDriverStatus(if (assetOk) assetMsg else "$configMsg — $assetMsg", assetOk)
                        btnLoadDriver.isEnabled = true
                    }
                }
            }
        }
    }

    // ── Tile merge ────────────────────────────────────────────

    /**
     * Merge tile — adds InvisibleX to the quick-settings panel.
     * On Android 13+ uses StatusBarManager.requestAddTileService
     * (the same as HomeFragment but exposed from the post-launch overlay too).
     * User can then drag it next to any other tile they use frequently.
     */
    private fun mergeTile() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val sbm = getSystemService(Context.STATUS_BAR_SERVICE) as? StatusBarManager
            if (sbm == null) {
                showTileStatus("Quick Settings not available", false)
                return
            }
            sbm.requestAddTileService(
                ComponentName(this, InvisibleXTileService::class.java),
                getString(R.string.tile_label),
                Icon.createWithResource(this, R.drawable.ic_tile),
                mainExecutor
            ) { resultCode ->
                when (resultCode) {
                    StatusBarManager.TILE_ADD_REQUEST_RESULT_TILE_ADDED ->
                        showTileStatus("Tile added — drag to position it next to another tile", true)
                    StatusBarManager.TILE_ADD_REQUEST_RESULT_TILE_ALREADY_ADDED ->
                        showTileStatus("Tile already added — open Quick Settings edit to reorder", true)
                    else ->
                        showTileStatus("Could not add tile — add manually from Quick Settings edit", false)
                }
            }
        } else {
            // Android 7–12: manual drag required
            showTileStatus("Open Quick Settings, tap edit, then drag InvisibleX tile to position", null)
        }
    }

    // ── Hide / Show icon ──────────────────────────────────────

    private fun toggleHideIcon() {
        val currentlyHidden = AppPrefs.isAppHidden()
        val wantVisible = currentlyHidden // toggle

        val pm = packageManager
        val launcher = ComponentName(this, MainActivity::class.java)
        val state = if (wantVisible) PackageManager.COMPONENT_ENABLED_STATE_ENABLED
        else PackageManager.COMPONENT_ENABLED_STATE_DISABLED

        try {
            pm.setComponentEnabledSetting(launcher, state, PackageManager.DONT_KILL_APP)
            AppPrefs.setAppHidden(!wantVisible)
            btnHideIcon.text = if (wantVisible) "HIDE ICON" else "SHOW ICON"
            showTileStatus(if (wantVisible) "App icon visible" else "App icon hidden", true)
        } catch (e: Exception) {
            showTileStatus("Failed — root may be required", false)
        }
    }

    // ── UI helpers ────────────────────────────────────────────

    private fun showDriverStatus(msg: String, ok: Boolean?) {
        tvDriverStatus.visibility = View.VISIBLE
        tvDriverStatus.text = msg
        tvDriverStatus.setTextColor(
            when (ok) {
                null -> getColor(R.color.text_secondary)
                true -> getColor(R.color.status_ok)
                false -> getColor(R.color.status_fail)
            }
        )
    }

    private fun postDriverStatus(msg: String, ok: Boolean) {
        runOnUiThread { showDriverStatus(msg, ok) }
    }

    private fun showTileStatus(msg: String, ok: Boolean?) {
        tvTileMergeStatus.visibility = View.VISIBLE
        tvTileMergeStatus.text = msg
        tvTileMergeStatus.setTextColor(
            when (ok) {
                null -> getColor(R.color.text_secondary)
                true -> getColor(R.color.status_ok)
                false -> getColor(R.color.status_fail)
            }
        )
    }

    // ── Slide-up animation ────────────────────────────────────

    private fun animatePanelIn() {
        panelMenu.post {
            val anim = TranslateAnimation(0f, 0f, panelMenu.height.toFloat(), 0f)
            anim.duration = 280
            anim.interpolator = DecelerateInterpolator(2f)
            panelMenu.startAnimation(anim)
        }
    }

    override fun onDestroy() {
        super.onDestroy()
    }
}
