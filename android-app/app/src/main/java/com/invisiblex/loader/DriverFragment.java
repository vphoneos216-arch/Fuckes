package com.invisiblex.loader;

import android.content.Intent;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import com.invisiblex.loader.databinding.FragmentDriverBinding;

import java.util.List;

/**
 * DriverFragment — lets the user pick which native backend mode
 * (wanbai.h / c_driver) the InvisibleX binary uses on its next launch.
 *
 * These are the 11 fixed in-binary modes (AUTO-DETECT + 10 named backends),
 * NOT files — see DriverInfo.wanbaiModes() and DriverConfig for exactly how
 * a selection here reaches the native side via config.json.
 *
 * Layout IDs → ViewBinding field names:
 *   tv_no_drivers        → binding.tvNoDrivers   (unused now — list is never empty)
 *   lv_drivers           → binding.lvDrivers
 *   card_selected_driver → binding.cardSelectedDriver
 *   tv_selected_driver   → binding.tvSelectedDriver
 *   tv_driver_size       → binding.tvDriverSize
 *   btn_load_driver      → binding.btnLoadDriver
 *   progress_driver      → binding.progressDriver
 *   tv_driver_status     → binding.tvDriverStatus
 *
 * Select flow:
 *   1. Show the fixed 11-mode list → user taps a row → selectedMode set.
 *   2. User taps "Load Driver" → security gate (NEVER bypassed) → write
 *      config.json → binary picks it up via ix_get_driver_choice() next run.
 */
public class DriverFragment extends Fragment {

    private FragmentDriverBinding binding;
    private DriverAdapter          adapter;
    private final List<DriverInfo> driverList = DriverInfo.wanbaiModes();
    private DriverInfo             selectedMode;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
                             @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        binding = FragmentDriverBinding.inflate(inflater, container, false);
        return binding.getRoot();
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);

        showKernelVersion();
        setupList();

        binding.lvDrivers.setOnItemClickListener((parent, v, position, id) -> {
            selectedMode = driverList.get(position);
            AppPrefs.setSelectedDriver(selectedMode.configValue);
            adapter.setSelected(selectedMode.configValue);
            updateSelectedCard();
            adapter.notifyDataSetChanged();
        });

        binding.btnLoadDriver.setOnClickListener(v -> attemptApplyDriver());
    }

    // ── Kernel version ──────────────────────────────────────────

    private void showKernelVersion() {
        // KernelUtil.detect() can shell out (uname -r) — keep it off the UI thread.
        new Thread(() -> {
            String kernel = KernelUtil.detect();
            if (!isAdded()) return;
            requireActivity().runOnUiThread(() -> {
                if (binding == null) return;
                binding.tvKernelVersion.setText(
                    "Kernel: " + (kernel.isEmpty() ? "unknown" : kernel));
            });
        }).start();
    }

    // ── Driver list ───────────────────────────────────────────

    private void setupList() {
        binding.tvNoDrivers.setVisibility(View.GONE);
        binding.lvDrivers.setVisibility(View.VISIBLE);

        String savedValue = AppPrefs.getSelectedDriver();
        if (savedValue == null || savedValue.isEmpty()) {
            savedValue = DriverConfig.readCurrentChoice();
        }
        selectedMode = findByConfigValue(savedValue);

        adapter = new DriverAdapter(requireContext(), driverList,
            selectedMode != null ? selectedMode.configValue : "auto");
        binding.lvDrivers.setAdapter(adapter);
        updateSelectedCard();
    }

    private DriverInfo findByConfigValue(String value) {
        if (value == null) return driverList.get(0);
        for (DriverInfo info : driverList) {
            if (info.configValue.equals(value)) return info;
        }
        return driverList.get(0); // fall back to AUTO-DETECT
    }

    private void updateSelectedCard() {
        if (selectedMode == null) {
            binding.cardSelectedDriver.setVisibility(View.GONE);
            binding.btnLoadDriver.setEnabled(false);
            return;
        }
        binding.cardSelectedDriver.setVisibility(View.VISIBLE);
        binding.tvSelectedDriver.setText(selectedMode.name);
        binding.tvDriverSize.setText(selectedMode.configValue);
        binding.btnLoadDriver.setEnabled(true);
    }

    // ── Apply (security-gated) ─────────────────────────────────

    private void attemptApplyDriver() {
        if (selectedMode == null) {
            showStatus(getString(R.string.select_driver_first), false);
            return;
        }

        setLoading(true);
        showStatus(getString(R.string.verifying_auth), null);

        // Security gate — mandatory before every driver/binary operation.
        // NEVER bypass this, regardless of how the fragment is reached.
        AuthManager.get().securityGate(requireContext(), (authOk, authMsg) -> {
            if (!isAdded()) return;
            if (!authOk) {
                setLoading(false);
                showStatus(getString(R.string.auth_required_driver), false);
                return;
            }
            // Auth passed — write the backend choice for the next binary launch,
            // then load the kernel-matched module for this mode (if one is
            // bundled in assets/drivers/ for the device's running kernel).
            String configValue = selectedMode.configValue;
            DriverConfig.applyDriverChoice(configValue, (configOk, configMsg) -> {
                if (!configOk) {
                    if (!isAdded()) return;
                    requireActivity().runOnUiThread(() -> {
                        setLoading(false);
                        showStatus(configMsg, false);
                    });
                    return;
                }
                DriverConfig.loadMatchingAsset(requireContext().getApplicationContext(),
                    configValue, (assetOk, assetMsg) -> {
                        if (!isAdded()) return;
                        requireActivity().runOnUiThread(() -> {
                            setLoading(false);
                            showStatus(assetOk ? assetMsg : configMsg + " — " + assetMsg, assetOk);
                            if (assetOk) openLaunchMenu();
                        });
                    });
            });
        });
    }

    // ── Post-launch menu ──────────────────────────────────────

    private void openLaunchMenu() {
        if (!isAdded()) return;
        Intent intent = new Intent(requireContext(), LaunchMenuActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP);
        startActivity(intent);
    }

    // ── UI helpers ────────────────────────────────────────────

    private void setLoading(boolean loading) {
        if (!isAdded()) return;
        binding.progressDriver.setVisibility(loading ? View.VISIBLE : View.GONE);
        binding.btnLoadDriver.setEnabled(!loading && selectedMode != null);
    }

    private void showStatus(String msg, Boolean success) {
        binding.tvDriverStatus.setVisibility(View.VISIBLE);
        binding.tvDriverStatus.setText(msg);
        if (success == null) {
            binding.tvDriverStatus.setTextColor(
                requireContext().getColor(R.color.text_secondary));
        } else if (success) {
            binding.tvDriverStatus.setTextColor(
                requireContext().getColor(R.color.status_ok));
        } else {
            binding.tvDriverStatus.setTextColor(
                requireContext().getColor(R.color.status_fail));
        }
    }

    @Override
    public void onDestroyView() {
        super.onDestroyView();
        binding = null;
    }
}
