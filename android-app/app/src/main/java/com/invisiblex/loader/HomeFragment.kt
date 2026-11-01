package com.invisiblex.loader

import android.app.StatusBarManager
import android.content.ComponentName
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.drawable.Icon
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.fragment.app.Fragment
import com.invisiblex.loader.databinding.FragmentHomeBinding

/**
 * HomeFragment — main dashboard after login.
 *
 * Sections:
 *   1. LAUNCH GAME card — opens Free Fire MAX, then attaches the engine.
 *   2. Status card — auth status dot, expiry date, key name.
 *   3. Quick Settings tile card — "Add Tile" button.
 *   4. App Visibility card — Hide / Show app icon in launcher.
 *   5. Logout button.
 */
class HomeFragment : Fragment() {

    private var binding: FragmentHomeBinding? = null
    private val mainHandler = Handler(Looper.getMainLooper())

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        binding = FragmentHomeBinding.inflate(inflater, container, false)
        return binding!!.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        val b = binding ?: return

        refreshStatus()

        // ── Launch Game → attach engine ────────────────────────
        b.btnLaunchGame.setOnClickListener { attemptLaunchGame() }

        // ── Add Quick Settings Tile ───────────────────────────
        b.btnAddTile.setOnClickListener { addQuickSettingsTile() }

        // ── Hide / Show from launcher ─────────────────────────
        b.btnHideApp.setOnClickListener { setLauncherVisibility(false) }
        b.btnShowApp.setOnClickListener { setLauncherVisibility(true) }

        // Init button states based on saved pref
        val hidden = AppPrefs.isAppHidden()
        b.btnHideApp.isEnabled = !hidden
        b.btnShowApp.isEnabled = hidden

        // ── Logout ────────────────────────────────────────────
        b.btnLogout.setOnClickListener {
            (activity as? MainActivity)?.onLogout()
        }
    }

    private fun refreshStatus() {
        val b = binding ?: return
        val valid = AuthManager.get().isAuthenticated()

        // Status dot — id: dot_auth
        b.dotAuth.background = requireContext().getDrawable(
            if (valid) R.drawable.dot_status else R.drawable.dot_status_dim
        )

        // Status text — id: tv_auth_status
        b.tvAuthStatus.text = getString(if (valid) R.string.auth_active else R.string.auth_inactive)
        b.tvAuthStatus.setTextColor(
            requireContext().getColor(if (valid) R.color.status_ok else R.color.status_fail)
        )

        // Expiry — id: tv_expiry_home
        val expiry = AppPrefs.getExpiryDate()
        b.tvExpiryHome.text = getString(R.string.expiry_format, expiry.ifEmpty { "N/A" })

        // Key name — id: tv_key_name
        val keyName = AppPrefs.getKeyName()
        b.tvKeyName.text = getString(R.string.key_format, keyName.ifEmpty { "—" })
    }

    // ── Launch Game → attach engine ────────────────────────────

    /**
     * LAUNCH GAME: open Free Fire MAX, give it a moment to come up, then
     * run through the exact same mandatory gate as every other entry
     * point (Tile, Driver, LaunchMenu) — AuthManager.securityGate() —
     * before BinaryExecutor.extractAndExecute() ever runs. The game
     * launching first does NOT skip or weaken that gate in any way; it
     * only decides *when* the already-gated execute call fires.
     */
    private fun attemptLaunchGame() {
        val b = binding ?: return

        val launchIntent = requireContext().packageManager
            .getLaunchIntentForPackage(GAME_PACKAGE)
        if (launchIntent == null) {
            showGameStatus(getString(R.string.game_not_installed), false)
            return
        }

        b.btnLaunchGame.isEnabled = false
        b.progressLaunchGame.visibility = View.VISIBLE
        showGameStatus(getString(R.string.game_launching), null)

        try {
            startActivity(launchIntent)
        } catch (e: Exception) {
            b.btnLaunchGame.isEnabled = true
            b.progressLaunchGame.visibility = View.GONE
            showGameStatus(getString(R.string.game_launch_failed), false)
            return
        }

        // Give the game a moment to actually come up before attaching —
        // mirrors the same launch-confirmation window BinaryExecutor
        // itself uses for the engine process.
        mainHandler.postDelayed({
            if (!isAdded) return@postDelayed
            showGameStatus(getString(R.string.engine_attaching), null)
            runSecurityGatedExecute()
        }, GAME_ATTACH_DELAY_MS)
    }

    /** Never call BinaryExecutor without this gate passing first. */
    private fun runSecurityGatedExecute() {
        AuthManager.get().securityGate(requireContext()) { authOk, authMsg ->
            if (!isAdded) return@securityGate
            if (!authOk) {
                finishGameLaunch(getString(R.string.auth_required_driver) + " — " + authMsg, false)
                return@securityGate
            }
            BinaryExecutor.extractAndExecute(requireContext().applicationContext) { execOk, execMsg ->
                if (!isAdded) return@extractAndExecute
                finishGameLaunch(
                    if (execOk) getString(R.string.engine_attach_success) else execMsg,
                    execOk
                )
            }
        }
    }

    private fun finishGameLaunch(msg: String, ok: Boolean) {
        val b = binding ?: return
        b.btnLaunchGame.isEnabled = true
        b.progressLaunchGame.visibility = View.GONE
        showGameStatus(msg, ok)
    }

    private fun showGameStatus(msg: String, ok: Boolean?) {
        val b = binding ?: return
        b.tvLaunchGameStatus.visibility = View.VISIBLE
        b.tvLaunchGameStatus.text = msg
        b.tvLaunchGameStatus.setTextColor(
            requireContext().getColor(
                when (ok) {
                    null -> R.color.text_secondary
                    true -> R.color.status_ok
                    false -> R.color.status_fail
                }
            )
        )
    }

    // ── Quick Settings Tile ───────────────────────────────────

    private fun addQuickSettingsTile() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val sbm = requireContext().getSystemService(Context.STATUS_BAR_SERVICE) as? StatusBarManager
            sbm?.requestAddTileService(
                ComponentName(requireContext(), InvisibleXTileService::class.java),
                getString(R.string.tile_label),
                Icon.createWithResource(requireContext(), R.drawable.ic_tile),
                requireContext().mainExecutor
            ) { resultCode ->
                val msg = when (resultCode) {
                    StatusBarManager.TILE_ADD_REQUEST_RESULT_TILE_ADDED -> getString(R.string.tile_added)
                    StatusBarManager.TILE_ADD_REQUEST_RESULT_TILE_ALREADY_ADDED -> getString(R.string.tile_already_added)
                    else -> getString(R.string.tile_error)
                }
                showToast(msg)
            }
        } else {
            // Android 7–12: user must add tile manually from quick-settings edit panel
            showToast(getString(R.string.tile_manual_hint))
        }
    }

    // ── Hide / Show from launcher ─────────────────────────────

    private fun setLauncherVisibility(visible: Boolean) {
        val b = binding ?: return
        val pm = requireContext().packageManager
        val launcher = ComponentName(requireContext(), MainActivity::class.java)

        val state = if (visible) PackageManager.COMPONENT_ENABLED_STATE_ENABLED
        else PackageManager.COMPONENT_ENABLED_STATE_DISABLED

        try {
            pm.setComponentEnabledSetting(launcher, state, PackageManager.DONT_KILL_APP)
            AppPrefs.setAppHidden(!visible)
            b.btnHideApp.isEnabled = visible
            b.btnShowApp.isEnabled = !visible
            showToast(getString(if (visible) R.string.app_shown else R.string.app_hidden))
        } catch (e: Exception) {
            showToast(getString(R.string.visibility_error))
        }
    }

    private fun showToast(msg: String) {
        if (!isAdded) return
        Toast.makeText(requireContext(), msg, Toast.LENGTH_SHORT).show()
    }

    override fun onDestroyView() {
        super.onDestroyView()
        mainHandler.removeCallbacksAndMessages(null)
        binding = null
    }

    companion object {
        /** Garena Free Fire MAX — the target game for the Launch Game button. */
        private const val GAME_PACKAGE = "com.dts.freefiremax"

        /** How long to let the game settle before attaching the engine. */
        private const val GAME_ATTACH_DELAY_MS = 4_000L
    }
}
