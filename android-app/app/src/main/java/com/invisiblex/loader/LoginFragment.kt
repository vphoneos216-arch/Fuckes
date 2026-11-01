package com.invisiblex.loader

import android.content.Context
import android.graphics.BitmapFactory
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.Editable
import android.text.TextWatcher
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import androidx.fragment.app.Fragment
import com.invisiblex.loader.databinding.FragmentLoginBinding
import java.io.File
import java.util.concurrent.Executors

/**
 * LoginFragment — license key auth screen (Kotlin).
 *
 * Flow, exactly as requested: key entered -> native integrity check runs
 * -> only if that passes does the actual login/auth call ("app integrity
 * check karga phir open hoga") ever reach the server. The integrity check
 * is app-side only (NativeAuth.integrityCheck() in ix_auth.cpp) — it
 * guards this screen; AuthManager.securityGate() still guards every
 * driver/binary operation independently, same as before.
 *
 * Layout IDs -> ViewBinding field names:
 *   iv_logo        -> binding.ivLogo
 *   tv_login_title -> binding.tvLoginTitle
 *   et_key         -> binding.etKey
 *   tv_status      -> binding.tvStatus
 *   progress_bar   -> binding.progressBar
 *   btn_login      -> binding.btnLogin
 *   tv_expiry      -> binding.tvExpiry
 */
class LoginFragment : Fragment() {

    private var binding: FragmentLoginBinding? = null
    private val bg = Executors.newSingleThreadExecutor()
    private val main = Handler(Looper.getMainLooper())

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        binding = FragmentLoginBinding.inflate(inflater, container, false)
        return binding!!.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        val b = binding ?: return

        // Custom app name, if set in Settings.
        val appName = AppPrefs.getAppName()
        if (appName.isNotEmpty()) {
            b.tvLoginTitle.text = appName
        }

        // Custom logo — read from an internal-storage file path, never a
        // content URI (content URIs are revoked after reboot; file paths
        // under filesDir are permanent).
        loadCustomLogo()

        // Clear any stale error/status the moment the user edits the key.
        b.etKey.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, st: Int, c: Int, a: Int) {}
            override fun onTextChanged(s: CharSequence?, st: Int, before: Int, count: Int) {
                b.tvStatus.visibility = View.GONE
                b.tvStatus.text = ""
            }
            override fun afterTextChanged(s: Editable?) {}
        })

        // IME "done" = authenticate.
        b.etKey.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_DONE) {
                attemptLogin()
                true
            } else {
                false
            }
        }

        b.btnLogin.setOnClickListener { attemptLogin() }

        // Saved-key hint, if a key was already stored from a previous session.
        val savedKey = AppPrefs.getLicenseKey()
        if (savedKey.isNotEmpty()) {
            val keyName = AppPrefs.getKeyName()
            b.tvExpiry.text = getString(
                R.string.saved_key_hint,
                if (keyName.isEmpty()) "saved" else keyName
            )
            b.tvExpiry.visibility = View.VISIBLE
        }
    }

    /** Loads the custom logo file, falling back to the default app icon on any failure. */
    private fun loadCustomLogo() {
        val b = binding ?: return
        val logoPath = AppPrefs.getCustomLogoPath()
        if (logoPath.isNotEmpty()) {
            try {
                val f = File(logoPath)
                if (f.exists() && f.canRead()) {
                    val bm = BitmapFactory.decodeFile(f.absolutePath)
                    if (bm != null) {
                        b.ivLogo.setImageBitmap(bm)
                        return
                    }
                }
                // File gone — clear the stale path so we don't retry forever.
                AppPrefs.setCustomLogoPath("")
            } catch (ignored: Exception) {
                AppPrefs.setCustomLogoPath("")
            }
        }
        b.ivLogo.setImageResource(R.drawable.ic_app_icon)
    }

    /**
     * key entered -> integrity check (background, native) -> on pass, the
     * real login call runs; on fail, login is refused outright and the key
     * the user typed is never sent anywhere.
     */
    private fun attemptLogin() {
        val b = binding ?: return
        val key = b.etKey.text?.toString()?.trim().orEmpty()

        if (key.isEmpty()) {
            showStatus(getString(R.string.enter_license_key), false)
            return
        }

        val imm = requireContext().getSystemService(Context.INPUT_METHOD_SERVICE)
                as? InputMethodManager
        imm?.hideSoftInputFromWindow(b.etKey.windowToken, 0)

        setLoading(true)
        showStatus(getString(R.string.integrity_checking), null)

        bg.execute {
            val genuine = try {
                NativeAuth.integrityCheck()
            } catch (t: Throwable) {
                false
            }
            main.post {
                if (!isAdded) return@post
                if (!genuine) {
                    setLoading(false)
                    showStatus(getString(R.string.integrity_failed), false)
                    return@post
                }
                showStatus(getString(R.string.verifying), null)
                runServerLogin(key)
            }
        }
    }

    private fun runServerLogin(key: String) {
        AuthManager.get().login(requireContext(), key) { success, message ->
            if (!isAdded) return@login
            setLoading(false)
            if (success) {
                showStatus(message, true)
                (activity as? MainActivity)?.onLoginSuccess()
            } else {
                showStatus(message, false)
            }
        }
    }

    private fun setLoading(loading: Boolean) {
        val b = binding ?: return
        b.progressBar.visibility = if (loading) View.VISIBLE else View.GONE
        b.btnLogin.isEnabled = !loading
        b.etKey.isEnabled = !loading
    }

    /** success: null = neutral (info), true = green, false = red. */
    private fun showStatus(message: String, success: Boolean?) {
        val b = binding ?: return
        b.tvStatus.visibility = View.VISIBLE
        b.tvStatus.text = message
        val colorRes = when (success) {
            null -> R.color.text_secondary
            true -> R.color.status_ok
            false -> R.color.status_fail
        }
        b.tvStatus.setTextColor(requireContext().getColor(colorRes))
    }

    override fun onDestroyView() {
        super.onDestroyView()
        binding = null
    }
}
