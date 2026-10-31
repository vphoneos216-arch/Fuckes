package com.invisiblex.loader;

import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.provider.MediaStore;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import com.invisiblex.loader.databinding.ActivitySettingsBinding;

/**
 * SettingsFragment — embedded in bottom nav.
 *
 * Options:
 *   - Custom logo: gallery picker → save URI to AppPrefs
 *   - App name: editable text → saved to AppPrefs
 *   - Server URL: editable text → saved to AppPrefs
 *   - Package name: display-only with rebuild notice
 *   - Save / Reset buttons
 */
public class SettingsFragment extends Fragment {

    private ActivitySettingsBinding binding;

    private final ActivityResultLauncher<Intent> logoPicker =
        registerForActivityResult(
            new ActivityResultContracts.StartActivityForResult(),
            result -> {
                if (result.getResultCode() == android.app.Activity.RESULT_OK
                    && result.getData() != null) {
                    Uri uri = result.getData().getData();
                    if (uri != null) handleLogoPicked(uri);
                }
            }
        );

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
                             @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        binding = ActivitySettingsBinding.inflate(inflater, container, false);
        return binding.getRoot();
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);

        // Hide back button — we're in fragment nav, not a standalone activity
        binding.btnBack.setVisibility(View.GONE);

        // Populate current values
        binding.etAppName.setText(AppPrefs.getAppName());
        binding.etServerUrl.setText(AppPrefs.getServerUrl());

        // Show current package name (read-only)
        String pkg = requireContext().getPackageName();
        binding.tvCurrentPkg.setText("Current: " + pkg);

        // Load saved custom logo
        String logoPath = AppPrefs.getCustomLogoPath();
        if (!logoPath.isEmpty()) {
            try {
                binding.ivCustomLogo.setImageURI(Uri.parse(logoPath));
            } catch (Exception ignored) {}
        }

        binding.btnChangeLogo.setOnClickListener(v -> openLogoPicker());
        binding.btnSaveSettings.setOnClickListener(v -> saveSettings());
        binding.btnResetSettings.setOnClickListener(v -> resetSettings());
    }

    private void openLogoPicker() {
        Intent intent = new Intent(Intent.ACTION_PICK,
            MediaStore.Images.Media.EXTERNAL_CONTENT_URI);
        intent.setType("image/*");
        logoPicker.launch(intent);
    }

    private void handleLogoPicked(Uri uri) {
        try {
            requireContext().getContentResolver().takePersistableUriPermission(
                uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception ignored) {}

        AppPrefs.setCustomLogoPath(uri.toString());
        try {
            binding.ivCustomLogo.setImageURI(uri);
        } catch (Exception ignored) {}
        showToast(getString(R.string.logo_updated));
    }

    private void saveSettings() {
        String appName   = binding.etAppName.getText().toString().trim();
        String serverUrl = binding.etServerUrl.getText().toString().trim();

        if (!appName.isEmpty()) {
            AppPrefs.setAppName(appName);
        }
        if (!serverUrl.isEmpty()) {
            if (!serverUrl.startsWith("https://")) {
                showToast(getString(R.string.server_url_https_required));
                return;
            }
            AppPrefs.setServerUrl(serverUrl);
        }
        showToast(getString(R.string.settings_saved));
    }

    private void resetSettings() {
        AppPrefs.setAppName("InvisibleX");
        AppPrefs.setServerUrl("https://adminshop.myvipsite.fun/keydock_connect.php");
        AppPrefs.setCustomLogoPath("");

        binding.etAppName.setText("InvisibleX");
        binding.etServerUrl.setText(
            "https://adminshop.myvipsite.fun/keydock_connect.php");
        binding.ivCustomLogo.setImageResource(R.drawable.ic_app_icon);

        showToast(getString(R.string.settings_reset_done));
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
