package com.invisiblex.loader;

import android.app.StatusBarManager;
import android.content.ComponentName;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import com.invisiblex.loader.databinding.FragmentHomeBinding;

/**
 * HomeFragment — main dashboard after login.
 *
 * Sections:
 *   1. Status card — auth status dot, expiry date, key name.
 *   2. Quick Settings tile card — "Add Tile" button.
 *   3. App Visibility card — Hide / Show app icon in launcher.
 *   4. Logout button.
 */
public class HomeFragment extends Fragment {

    private FragmentHomeBinding binding;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
                             @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        binding = FragmentHomeBinding.inflate(inflater, container, false);
        return binding.getRoot();
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);

        refreshStatus();

        // ── Add Quick Settings Tile ───────────────────────────
        binding.btnAddTile.setOnClickListener(v -> addQuickSettingsTile());

        // ── Hide / Show from launcher ─────────────────────────
        binding.btnHideApp.setOnClickListener(v -> setLauncherVisibility(false));
        binding.btnShowApp.setOnClickListener(v -> setLauncherVisibility(true));

        // Init button states based on saved pref
        boolean hidden = AppPrefs.isAppHidden();
        binding.btnHideApp.setEnabled(!hidden);
        binding.btnShowApp.setEnabled(hidden);

        // ── Logout ────────────────────────────────────────────
        binding.btnLogout.setOnClickListener(v -> {
            if (getActivity() instanceof MainActivity) {
                ((MainActivity) getActivity()).onLogout();
            }
        });
    }

    private void refreshStatus() {
        boolean valid = AuthManager.get().isAuthenticated();

        // Status dot — id: dot_auth
        binding.dotAuth.setBackground(requireContext().getDrawable(
            valid ? R.drawable.dot_status : R.drawable.dot_status_dim));

        // Status text — id: tv_auth_status
        binding.tvAuthStatus.setText(valid
            ? getString(R.string.auth_active)
            : getString(R.string.auth_inactive));
        binding.tvAuthStatus.setTextColor(requireContext().getColor(
            valid ? R.color.status_ok : R.color.status_fail));

        // Expiry — id: tv_expiry_home
        String expiry = AppPrefs.getExpiryDate();
        binding.tvExpiryHome.setText(
            getString(R.string.expiry_format, expiry.isEmpty() ? "N/A" : expiry));

        // Key name — id: tv_key_name
        String keyName = AppPrefs.getKeyName();
        binding.tvKeyName.setText(
            getString(R.string.key_format, keyName.isEmpty() ? "—" : keyName));
    }

    // ── Quick Settings Tile ───────────────────────────────────

    private void addQuickSettingsTile() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            StatusBarManager sbm = (StatusBarManager)
                requireContext().getSystemService(android.content.Context.STATUS_BAR_SERVICE);
            if (sbm != null) {
                sbm.requestAddTileService(
                    new ComponentName(requireContext(), InvisibleXTileService.class),
                    getString(R.string.tile_label),
                    requireContext().getDrawable(R.drawable.ic_tile),
                    requireContext().getMainExecutor(),
                    resultCode -> {
                        String msg;
                        switch (resultCode) {
                            case StatusBarManager.TILE_ADD_REQUEST_RESULT_TILE_ADDED:
                                msg = getString(R.string.tile_added); break;
                            case StatusBarManager.TILE_ADD_REQUEST_RESULT_TILE_ALREADY_ADDED:
                                msg = getString(R.string.tile_already_added); break;
                            default:
                                msg = getString(R.string.tile_error); break;
                        }
                        showToast(msg);
                    }
                );
            }
        } else {
            // Android 7–12: user must add tile manually from quick-settings edit panel
            showToast(getString(R.string.tile_manual_hint));
        }
    }

    // ── Hide / Show from launcher ─────────────────────────────

    private void setLauncherVisibility(boolean visible) {
        PackageManager pm = requireContext().getPackageManager();
        ComponentName launcher = new ComponentName(
            requireContext(), MainActivity.class);

        int state = visible
            ? PackageManager.COMPONENT_ENABLED_STATE_ENABLED
            : PackageManager.COMPONENT_ENABLED_STATE_DISABLED;

        try {
            pm.setComponentEnabledSetting(launcher, state,
                PackageManager.DONT_KILL_APP);
            AppPrefs.setAppHidden(!visible);
            binding.btnHideApp.setEnabled(visible);
            binding.btnShowApp.setEnabled(!visible);
            showToast(visible
                ? getString(R.string.app_shown)
                : getString(R.string.app_hidden));
        } catch (Exception e) {
            showToast(getString(R.string.visibility_error));
        }
    }

    private void showToast(String msg) {
        if (!isAdded()) return;
        android.widget.Toast.makeText(requireContext(), msg,
            android.widget.Toast.LENGTH_SHORT).show();
    }

    @Override
    public void onDestroyView() {
        super.onDestroyView();
        binding = null;
    }
}
