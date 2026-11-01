package com.invisiblex.loader

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.View
import android.view.WindowManager
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.fragment.app.Fragment
import com.invisiblex.loader.databinding.ActivityMainBinding

/**
 * MainActivity — root activity.
 *
 * [SECURITY] Root gate (checked BEFORE the auth gate, on every launch):
 *   - The whole engine is useless without root (BinaryExecutor needs
 *     ptrace/proc-mem access only su grants), so rather than letting the
 *     user log in, pick a driver, and only discover "no root" at the very
 *     last step (where it used to surface as an unhandled crash), this
 *     checks root FIRST, before any UI beyond the splash spinner is shown.
 *   - No root → a non-cancelable "Please grant root permission" dialog is
 *     shown, with a visible 3-second countdown, and the app then closes
 *     itself via finishAffinity() — never a crash, always a clean,
 *     intentional close.
 *   - Root present → proceeds straight into the normal auth gate below,
 *     completely unchanged.
 *
 * Auth gate (only reached once root is confirmed):
 *   - On start, a splash spinner shows (no fragment yet) while
 *     AuthManager.tryRestoreSession() checks the saved license key.
 *   - If a key was saved from a previous login and the server still
 *     confirms it: session restores silently, straight to HomeFragment —
 *     the user never sees the login screen again on this device.
 *   - Only if there's no saved key, or the server rejects it, does
 *     LoginFragment ever appear.
 *   - LoginFragment calls onLoginSuccess() here to flip state.
 */
class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private val mainHandler = Handler(Looper.getMainLooper())
    private var rootGateDialog: AlertDialog? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // [APP PROTECTION] Block screenshots/screen-recording and hide this
        // activity's content from the Recents thumbnail — login key, auth
        // status, and session details never land in a screenshot, a
        // recording, or the task switcher's preview image.
        window.setFlags(WindowManager.LayoutParams.FLAG_SECURE, WindowManager.LayoutParams.FLAG_SECURE)

        // Init encrypted prefs before anything else
        AppPrefs.init(applicationContext)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setupBottomNav()

        binding.progressSplash.visibility = View.VISIBLE
        checkRootThenProceed()
    }

    // ── Root gate ───────────────────────────────────────────────

    private fun checkRootThenProceed() {
        Thread {
            val rootOk = try {
                BinaryExecutor.isRootAvailable()
            } catch (e: Exception) {
                // Never let a probe exception crash app startup — treat any
                // unexpected failure here as "no root" and show the gate.
                Log.e(TAG, "Root probe threw during startup", e)
                false
            }
            mainHandler.post {
                if (isFinishing || isDestroyed) return@post
                if (rootOk) {
                    proceedToAuthGate()
                } else {
                    showRootRequiredAndClose()
                }
            }
        }.start()
    }

    private fun showRootRequiredAndClose() {
        binding.progressSplash.visibility = View.GONE

        var secondsLeft = 3
        val dialog = AlertDialog.Builder(this)
            .setTitle(R.string.root_gate_title)
            .setMessage(getString(R.string.root_gate_message, secondsLeft))
            .setCancelable(false)
            .create()
        rootGateDialog = dialog
        dialog.show()

        val countdown = object : Runnable {
            override fun run() {
                secondsLeft -= 1
                if (isFinishing || isDestroyed) return
                if (secondsLeft <= 0) {
                    dialog.dismiss()
                    // Clean, intentional close — never a crash.
                    finishAffinity()
                    return
                }
                dialog.setMessage(getString(R.string.root_gate_message, secondsLeft))
                mainHandler.postDelayed(this, 1000)
            }
        }
        mainHandler.postDelayed(countdown, 1000)
    }

    private fun proceedToAuthGate() {
        // Splash spinner only — no login flash. Saved key (if any) is
        // checked silently; only the outcome decides what the user sees.
        binding.progressSplash.visibility = View.VISIBLE

        AuthManager.get().tryRestoreSession(this) { success, _ ->
            binding.progressSplash.visibility = View.GONE
            if (success) {
                onLoginSuccess()
            } else {
                showLogin()
            }
        }
    }

    // ── Navigation ────────────────────────────────────────────

    private fun setupBottomNav() {
        binding.bottomNav.visibility = View.GONE
        binding.bottomNav.setOnItemSelectedListener { item ->
            when (item.itemId) {
                R.id.nav_home -> {
                    loadFragment(HomeFragment())
                    true
                }
                R.id.nav_driver -> {
                    loadFragment(DriverFragment())
                    true
                }
                R.id.nav_settings -> {
                    loadFragment(SettingsFragment())
                    true
                }
                else -> false
            }
        }
    }

    /** Called by LoginFragment after successful authentication. */
    fun onLoginSuccess() {
        binding.bottomNav.visibility = View.VISIBLE
        binding.bottomNav.selectedItemId = R.id.nav_home
        loadFragment(HomeFragment())
    }

    /** Called by HomeFragment on logout. */
    fun onLogout() {
        AuthManager.get().logout()
        binding.bottomNav.visibility = View.GONE
        showLogin()
    }

    private fun showLogin() {
        binding.bottomNav.visibility = View.GONE
        val ft = supportFragmentManager.beginTransaction()
        ft.replace(R.id.fragment_container, LoginFragment())
        ft.commit()
    }

    private fun loadFragment(fragment: Fragment) {
        supportFragmentManager.beginTransaction()
            .replace(R.id.fragment_container, fragment)
            .commit()
    }

    override fun onDestroy() {
        super.onDestroy()
        mainHandler.removeCallbacksAndMessages(null)
        rootGateDialog?.dismiss()
        rootGateDialog = null
    }

    companion object {
        private const val TAG = "IX_Main"
    }
}
