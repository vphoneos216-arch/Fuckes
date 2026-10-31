package com.invisiblex.loader;

import android.content.Intent;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import com.invisiblex.loader.databinding.FragmentDriverBinding;

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
 * DriverFragment — lists driver files from assets/drivers/,
 * lets user select one by kernel, and loads it.
 *
 * Layout IDs → ViewBinding field names:
 *   tv_no_drivers       → binding.tvNoDrivers
 *   lv_drivers          → binding.lvDrivers
 *   card_selected_driver→ binding.cardSelectedDriver
 *   tv_selected_driver  → binding.tvSelectedDriver
 *   tv_driver_size      → binding.tvDriverSize
 *   btn_load_driver     → binding.btnLoadDriver
 *   progress_driver     → binding.progressDriver
 *   tv_driver_status    → binding.tvDriverStatus
 *
 * Driver load flow:
 *   1. List assets/drivers/ → build DriverInfo list.
 *   2. User taps a row → selectedDriver set.
 *   3. User taps "Load Driver" → auth gate → extract → insmod/load.
 */
public class DriverFragment extends Fragment {

    private static final String TAG       = "IX_Driver";
    private static final String ASSET_DIR = "drivers";

    private FragmentDriverBinding binding;
    private DriverAdapter          adapter;
    private final List<DriverInfo> driverList = new ArrayList<>();
    private String                 selectedDriver;
    private final ExecutorService  executor   = Executors.newSingleThreadExecutor();

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

        selectedDriver = AppPrefs.getSelectedDriver();

        loadDriverList();

        binding.lvDrivers.setOnItemClickListener((parent, v, position, id) -> {
            DriverInfo info = driverList.get(position);
            selectedDriver = info.name;
            AppPrefs.setSelectedDriver(selectedDriver);
            adapter.setSelected(selectedDriver);
            updateSelectedCard();
            adapter.notifyDataSetChanged();
        });

        binding.btnLoadDriver.setOnClickListener(v -> attemptLoadDriver());
    }

    // ── Driver list ───────────────────────────────────────────

    private void loadDriverList() {
        try {
            AssetManager am = requireContext().getAssets();
            String[] files  = am.list(ASSET_DIR);
            driverList.clear();

            if (files == null || files.length == 0) {
                showEmpty();
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
                showEmpty();
                return;
            }

            binding.tvNoDrivers.setVisibility(View.GONE);
            binding.lvDrivers.setVisibility(View.VISIBLE);

            adapter = new DriverAdapter(requireContext(), driverList, selectedDriver);
            binding.lvDrivers.setAdapter(adapter);
            updateSelectedCard();

        } catch (IOException e) {
            Log.e(TAG, "Failed to list drivers", e);
            showStatus(getString(R.string.driver_list_error), false);
        }
    }

    private void showEmpty() {
        binding.tvNoDrivers.setVisibility(View.VISIBLE);
        binding.lvDrivers.setVisibility(View.GONE);
        binding.btnLoadDriver.setEnabled(false);
    }

    private void updateSelectedCard() {
        if (selectedDriver == null || selectedDriver.isEmpty()) {
            binding.cardSelectedDriver.setVisibility(View.GONE);
            binding.btnLoadDriver.setEnabled(false);
            return;
        }
        binding.cardSelectedDriver.setVisibility(View.VISIBLE);
        binding.tvSelectedDriver.setText(selectedDriver);
        binding.btnLoadDriver.setEnabled(true);
    }

    // ── Driver load ───────────────────────────────────────────

    private void attemptLoadDriver() {
        if (selectedDriver == null || selectedDriver.isEmpty()) {
            showStatus(getString(R.string.select_driver_first), false);
            return;
        }

        setLoading(true);
        showStatus(getString(R.string.verifying_auth), null);

        // Security gate — double auth check before executing anything
        AuthManager.get().securityGate(requireContext(), (authOk, authMsg) -> {
            if (!isAdded()) return;
            if (!authOk) {
                setLoading(false);
                showStatus(getString(R.string.auth_required_driver), false);
                return;
            }
            // Auth passed — extract and load driver in background
            executor.execute(this::extractAndLoadDriver);
        });
    }

    private void extractAndLoadDriver() {
        String driverName = selectedDriver;
        if (driverName == null || driverName.isEmpty()) return;

        try {
            File driverFile = extractDriver(driverName);
            if (driverFile == null) {
                postStatus(getString(R.string.driver_extract_error), false);
                return;
            }

            // chmod
            driverFile.setExecutable(true, true);
            Runtime.getRuntime()
                .exec(new String[]{"chmod", "700", driverFile.getAbsolutePath()})
                .waitFor();

            // Load via insmod (requires root)
            Process proc = Runtime.getRuntime().exec(
                new String[]{"su", "-c", "insmod " + driverFile.getAbsolutePath()});
            int exitCode = proc.waitFor();

            if (exitCode == 0) {
                postStatus(getString(R.string.driver_loaded, driverName), true);
                openLaunchMenu();
            } else {
                // Try direct execute as fallback (some drivers are executables, not .ko modules)
                ProcessBuilder pb = new ProcessBuilder(driverFile.getAbsolutePath());
                pb.redirectErrorStream(true);
                pb.start();
                postStatus(getString(R.string.driver_executed, driverName), true);
                openLaunchMenu();
            }

        } catch (IOException | InterruptedException e) {
            Log.e(TAG, "Driver load error", e);
            postStatus(getString(R.string.driver_load_error, e.getMessage()), false);
        }

        if (isAdded()) {
            requireActivity().runOnUiThread(() -> setLoading(false));
        }
    }

    private File extractDriver(String name) throws IOException {
        File driversDir = new File(requireContext().getFilesDir(), "drivers");
        if (!driversDir.exists()) driversDir.mkdirs();

        File out = new File(driversDir, name);
        InputStream in  = requireContext().getAssets().open(ASSET_DIR + "/" + name);
        OutputStream os = new FileOutputStream(out);
        byte[] buf = new byte[8192];
        int len;
        while ((len = in.read(buf)) != -1) os.write(buf, 0, len);
        os.flush(); os.close(); in.close();

        Log.i(TAG, "Driver extracted: " + out.getAbsolutePath());
        return out;
    }

    // ── Post-launch menu ──────────────────────────────────────

    private void openLaunchMenu() {
        if (!isAdded()) return;
        requireActivity().runOnUiThread(() -> {
            Intent intent = new Intent(requireContext(), LaunchMenuActivity.class);
            intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP);
            startActivity(intent);
        });
    }

    // ── UI helpers ────────────────────────────────────────────

    private void setLoading(boolean loading) {
        if (!isAdded()) return;
        binding.progressDriver.setVisibility(loading ? View.VISIBLE : View.GONE);
        binding.btnLoadDriver.setEnabled(!loading && selectedDriver != null
            && !selectedDriver.isEmpty());
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

    private void postStatus(String msg, boolean ok) {
        if (!isAdded()) return;
        requireActivity().runOnUiThread(() -> showStatus(msg, ok));
    }

    @Override
    public void onDestroyView() {
        super.onDestroyView();
        binding = null;
    }
}
