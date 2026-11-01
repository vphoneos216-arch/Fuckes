package com.invisiblex.loader

import android.content.Intent
import android.graphics.BitmapFactory
import android.net.Uri
import android.os.Bundle
import android.provider.MediaStore
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.fragment.app.Fragment
import com.invisiblex.loader.databinding.ActivitySettingsBinding
import java.io.File
import java.io.FileOutputStream

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
 *
 * The "AUTH SERVER URL" card that used to live on this screen is gone —
 * it only ever displayed a disabled field (the URL is a hardcoded native
 * constant in ix_auth.cpp / login.h, never user-editable data), and it
 * was flagged as confusing to have on a settings screen at all.
 */
class SettingsFragment : Fragment() {

    private var binding: ActivitySettingsBinding? = null

    private val logoPicker = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == android.app.Activity.RESULT_OK) {
            result.data?.data?.let { uri -> handleLogoPicked(uri) }
        }
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        binding = ActivitySettingsBinding.inflate(inflater, container, false)
        return binding!!.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        val b = binding ?: return

        // Hide back button — we're in fragment nav, not a standalone activity
        b.btnBack.visibility = View.GONE

        // Populate current values
        b.etAppName.setText(AppPrefs.getAppName())

        // Show current package name (read-only)
        val pkg = requireContext().packageName
        b.tvCurrentPkg.text = "Current: $pkg"

        // Load saved custom logo — use file path, never a content URI
        loadSavedLogoIntoView()

        b.btnChangeLogo.setOnClickListener { openLogoPicker() }
        b.btnSaveSettings.setOnClickListener { saveSettings() }
        b.btnResetSettings.setOnClickListener { resetSettings() }
    }

    // ── Logo loading ──────────────────────────────────────────

    /**
     * Load custom logo from internal storage file path.
     * Content URIs are NOT used — they become invalid after reboot/permission revoke.
     * Falls back silently to the default icon if the file is missing or unreadable.
     */
    private fun loadSavedLogoIntoView() {
        val b = binding ?: return
        val logoPath = AppPrefs.getCustomLogoPath()
        if (logoPath.isEmpty()) {
            b.ivCustomLogo.setImageResource(R.drawable.ic_app_icon)
            return
        }
        try {
            val f = File(logoPath)
            if (f.exists() && f.canRead()) {
                val bm = BitmapFactory.decodeFile(f.absolutePath)
                if (bm != null) {
                    b.ivCustomLogo.setImageBitmap(bm)
                    return
                }
            }
        } catch (ignored: Exception) {
        }
        // File gone or unreadable — reset prefs and show default
        AppPrefs.setCustomLogoPath("")
        b.ivCustomLogo.setImageResource(R.drawable.ic_app_icon)
    }

    private fun openLogoPicker() {
        val intent = Intent(Intent.ACTION_PICK, MediaStore.Images.Media.EXTERNAL_CONTENT_URI)
        intent.type = "image/*"
        logoPicker.launch(intent)
    }

    /**
     * Copy picked image into internal storage as custom_logo.png.
     * Stores the absolute file path — zero URI permission dependency.
     */
    private fun handleLogoPicked(uri: Uri) {
        val b = binding ?: return
        try {
            // Copy bytes to internal storage
            val logoFile = File(requireContext().filesDir, LOGO_FILENAME)
            requireContext().contentResolver.openInputStream(uri).use { input ->
                if (input == null) throw Exception("Could not open picked image")
                FileOutputStream(logoFile).use { out ->
                    val buf = ByteArray(8192)
                    var len: Int
                    while (input.read(buf).also { len = it } != -1) out.write(buf, 0, len)
                    out.flush()
                }
            }

            // Save path (not content URI) to prefs
            AppPrefs.setCustomLogoPath(logoFile.absolutePath)

            // Decode and display
            val bm = BitmapFactory.decodeFile(logoFile.absolutePath)
            if (bm != null) b.ivCustomLogo.setImageBitmap(bm)

            showToast(getString(R.string.logo_updated))
        } catch (e: Exception) {
            showToast("Logo update failed — try again")
        }
    }

    // ── Settings save / reset ─────────────────────────────────

    private fun saveSettings() {
        val b = binding ?: return
        val appName = b.etAppName.text.toString().trim()
        if (appName.isNotEmpty()) {
            AppPrefs.setAppName(appName)
        }
        showToast(getString(R.string.settings_saved))
    }

    private fun resetSettings() {
        val b = binding ?: return
        AppPrefs.setAppName("InvisibleX")

        // Delete copied logo file if it exists
        val oldPath = AppPrefs.getCustomLogoPath()
        if (oldPath.isNotEmpty()) {
            try { File(oldPath).delete() } catch (ignored: Exception) {}
        }
        AppPrefs.setCustomLogoPath("")

        b.etAppName.setText("InvisibleX")
        b.ivCustomLogo.setImageResource(R.drawable.ic_app_icon)

        showToast(getString(R.string.settings_reset_done))
    }

    // ── Helpers ────────────────────────────────────────────────

    private fun showToast(msg: String) {
        if (!isAdded) return
        Toast.makeText(requireContext(), msg, Toast.LENGTH_SHORT).show()
    }

    override fun onDestroyView() {
        super.onDestroyView()
        binding = null
    }

    companion object {
        private const val LOGO_FILENAME = "custom_logo.png"
    }
}
