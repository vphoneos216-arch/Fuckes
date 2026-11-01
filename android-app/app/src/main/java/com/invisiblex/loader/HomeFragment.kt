package com.invisiblex.loader

import android.app.StatusBarManager
import android.content.ComponentName
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.drawable.Icon
import android.os.Build
import android.os.Bundle
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
 *   1. Status card — auth status dot, expiry date, key name.
 *   2. Quick Settings tile card — "Add Tile" button.
 *   3. App Visibility card — Hide / Show app icon in launcher.
 *   4. Logout button.
 */
class HomeFragment : Fragment() {

    private var binding: FragmentHomeBinding? = null

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
        binding = null
    }
}
