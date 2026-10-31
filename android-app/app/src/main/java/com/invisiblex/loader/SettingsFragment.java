package com.invisiblex.loader;

import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
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

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * SettingsFragment — embedded in bottom nav.
 *
 * Logo persistence fix (crash fix):
 *   Android may revoke persistable URI permission across restarts/reboots.
 *   Storing a content URI in AppPrefs and calling setImageURI() on cold
 *   start reliably crashes once permission is gone.
 *   Fix: copy the image bytes to internal storage on pick and store the
 *   absolute file path instead. File paths under getFilesDir() never need
 *   URI permission and survive across any number of restarts.
 */
public class SettingsFragment extends Fragment {

    private static final String LOGO_FILENAME = "custom_logo.png";

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

        // Server URL is pinned inside the native auth module (ix_auth.cpp)
        // alongside its Ed25519/X25519 trust keys — display-only.
        binding.etServerUrl.setEnabled(false);
        binding.etServerUrl.setFocusable(false);

        // Show current package name (read-only)
        String pkg = requireContext().getPackageName();
        binding.tvCurrentPkg.setText("Current: " + pkg);

        // Load saved custom logo — use file path, never a content URI
        loadSavedLogoIntoView();

        binding.btnChangeLogo.setOnClickListener(v -> openLogoPicker());
        binding.btnSaveSettings.setOnClickListener(v -> saveSettings());
        binding.btnResetSettings.setOnClickListener(v -> resetSettings());
    }

    // ── Logo loading ──────────────────────────────────────────

    /**
     * Load custom logo from internal storage file path.
     * Content URIs are NOT used — they become invalid after reboot/permission revoke.
     * Falls back silently to the default icon if the file is missing or unreadable.
     */
    private void loadSavedLogoIntoView() {
        String logoPath = AppPrefs.getCustomLogoPath();
        if (logoPath.isEmpty()) {
            binding.ivCustomLogo.setImageResource(R.drawable.ic_app_icon);
            return;
        }
        try {
            File f = new File(logoPath);
            if (f.exists() && f.canRead()) {
                Bitmap bm = BitmapFactory.decodeFile(f.getAbsolutePath());
                if (bm != null) {
                    binding.ivCustomLogo.setImageBitmap(bm);
                    return;
                }
            }
        } catch (Exception ignored) {}
        // File gone or unreadable — reset prefs and show default
        AppPrefs.setCustomLogoPath("");
        binding.ivCustomLogo.setImageResource(R.drawable.ic_app_icon);
    }

    private void openLogoPicker() {
        Intent intent = new Intent(Intent.ACTION_PICK,
            MediaStore.Images.Media.EXTERNAL_CONTENT_URI);
        intent.setType("image/*");
        logoPicker.launch(intent);
    }

    /**
     * Copy picked image into internal storage as custom_logo.png.
     * Stores the absolute file path — zero URI permission dependency.
     */
    private void handleLogoPicked(Uri uri) {
        try {
            // Copy bytes to internal storage
            File logoFile = new File(requireContext().getFilesDir(), LOGO_FILENAME);
            try (InputStream in  = requireContext().getContentResolver().openInputStream(uri);
                 OutputStream out = new FileOutputStream(logoFile)) {
                if (in == null) throw new Exception("Could not open picked image");
                byte[] buf = new byte[8192];
                int len;
                while ((len = in.read(buf)) != -1) out.write(buf, 0, len);
                out.flush();
            }

            // Save path (not content URI) to prefs
            AppPrefs.setCustomLogoPath(logoFile.getAbsolutePath());

            // Decode and display
            Bitmap bm = BitmapFactory.decodeFile(logoFile.getAbsolutePath());
            if (bm != null) binding.ivCustomLogo.setImageBitmap(bm);

            showToast(getString(R.string.logo_updated));

        } catch (Exception e) {
            showToast("Logo update failed — try again");
        }
    }

    // ── Settings save / reset ─────────────────────────────────

    private void saveSettings() {
        String appName = binding.etAppName.getText().toString().trim();
        if (!appName.isEmpty()) {
            AppPrefs.setAppName(appName);
        }
        // Server URL field is disabled — nothing to validate/save
        showToast(getString(R.string.settings_saved));
    }

    private void resetSettings() {
        AppPrefs.setAppName("InvisibleX");
        AppPrefs.setServerUrl("https://adminshop.myvipsite.fun/keydock_connect.php");

        // Delete copied logo file if it exists
        String oldPath = AppPrefs.getCustomLogoPath();
        if (!oldPath.isEmpty()) {
            try { new File(oldPath).delete(); } catch (Exception ignored) {}
        }
        AppPrefs.setCustomLogoPath("");

        binding.etAppName.setText("InvisibleX");
        binding.etServerUrl.setText(
            "https://adminshop.myvipsite.fun/keydock_connect.php");
        binding.ivCustomLogo.setImageResource(R.drawable.ic_app_icon);

        showToast(getString(R.string.settings_reset_done));
    }

    // ── Helpers ────────────────────────────────────────────────

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
