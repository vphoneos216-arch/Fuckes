package com.invisiblex.loader

import android.os.Bundle
import android.view.View
import androidx.appcompat.app.AppCompatActivity
import androidx.fragment.app.Fragment
import com.invisiblex.loader.databinding.ActivityMainBinding

/**
 * MainActivity — root activity.
 *
 * Auth gate:
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

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Init encrypted prefs before anything else
        AppPrefs.init(applicationContext)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setupBottomNav()

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
}
