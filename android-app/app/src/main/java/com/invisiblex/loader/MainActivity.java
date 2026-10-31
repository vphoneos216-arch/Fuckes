package com.invisiblex.loader;

import android.os.Bundle;
import android.view.View;

import androidx.appcompat.app.AppCompatActivity;
import androidx.fragment.app.Fragment;
import androidx.fragment.app.FragmentTransaction;

import com.google.android.material.bottomnavigation.BottomNavigationView;
import com.invisiblex.loader.databinding.ActivityMainBinding;

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
public class MainActivity extends AppCompatActivity {

    private ActivityMainBinding binding;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Init encrypted prefs before anything else
        AppPrefs.init(getApplicationContext());

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        setupBottomNav();

        // Splash spinner only — no login flash. Saved key (if any) is
        // checked silently; only the outcome decides what the user sees.
        binding.progressSplash.setVisibility(View.VISIBLE);

        AuthManager.get().tryRestoreSession(this, (success, message) -> {
            binding.progressSplash.setVisibility(View.GONE);
            if (success) {
                onLoginSuccess();
            } else {
                showLogin();
            }
        });
    }

    // ── Navigation ────────────────────────────────────────────

    private void setupBottomNav() {
        binding.bottomNav.setVisibility(View.GONE);
        binding.bottomNav.setOnItemSelectedListener(item -> {
            int id = item.getItemId();
            if (id == R.id.nav_home) {
                loadFragment(new HomeFragment());
                return true;
            } else if (id == R.id.nav_driver) {
                loadFragment(new DriverFragment());
                return true;
            } else if (id == R.id.nav_settings) {
                loadFragment(new SettingsFragment());
                return true;
            }
            return false;
        });
    }

    /**
     * Called by LoginFragment after successful authentication.
     */
    public void onLoginSuccess() {
        binding.bottomNav.setVisibility(View.VISIBLE);
        binding.bottomNav.setSelectedItemId(R.id.nav_home);
        loadFragment(new HomeFragment());
    }

    /**
     * Called by HomeFragment on logout.
     */
    public void onLogout() {
        AuthManager.get().logout();
        binding.bottomNav.setVisibility(View.GONE);
        showLogin();
    }

    private void showLogin() {
        binding.bottomNav.setVisibility(View.GONE);
        FragmentTransaction ft = getSupportFragmentManager().beginTransaction();
        ft.replace(R.id.fragment_container, new LoginFragment());
        ft.commit();
    }

    private void loadFragment(Fragment fragment) {
        getSupportFragmentManager().beginTransaction()
            .replace(R.id.fragment_container, fragment)
            .commit();
    }
}
