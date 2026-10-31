package com.invisiblex.loader;

import android.os.Bundle;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.content.Context;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import com.invisiblex.loader.databinding.FragmentLoginBinding;

/**
 * LoginFragment — license key auth screen.
 *
 * Layout IDs → ViewBinding field names:
 *   iv_logo        → binding.ivLogo
 *   tv_login_title → binding.tvLoginTitle
 *   et_key         → binding.etKey
 *   tv_status      → binding.tvStatus
 *   progress_bar   → binding.progressBar
 *   btn_login      → binding.btnLogin
 *   tv_expiry      → binding.tvExpiry
 */
public class LoginFragment extends Fragment {

    private FragmentLoginBinding binding;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
                             @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        binding = FragmentLoginBinding.inflate(inflater, container, false);
        return binding.getRoot();
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);

        // Load custom app name if set
        String appName = AppPrefs.getAppName();
        if (!appName.isEmpty()) {
            binding.tvLoginTitle.setText(appName);
        }

        // Load custom logo if set
        String logoPath = AppPrefs.getCustomLogoPath();
        if (!logoPath.isEmpty()) {
            try {
                binding.ivLogo.setImageURI(android.net.Uri.parse(logoPath));
            } catch (Exception ignored) {}
        }

        // Input: clear error state on type
        binding.etKey.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int st, int c, int a) {}
            @Override public void onTextChanged(CharSequence s, int st, int b, int c) {
                binding.tvStatus.setVisibility(View.GONE);
                binding.tvStatus.setText("");
            }
            @Override public void afterTextChanged(Editable s) {}
        });

        // IME action = authenticate
        binding.etKey.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_DONE) {
                attemptLogin();
                return true;
            }
            return false;
        });

        binding.btnLogin.setOnClickListener(v -> attemptLogin());

        // Show saved key hint if exists
        String savedKey = AppPrefs.getLicenseKey();
        if (!savedKey.isEmpty()) {
            String keyName = AppPrefs.getKeyName();
            binding.tvExpiry.setText(
                getString(R.string.saved_key_hint, keyName.isEmpty() ? "saved" : keyName));
            binding.tvExpiry.setVisibility(View.VISIBLE);
        }
    }

    private void attemptLogin() {
        String key = binding.etKey.getText() != null
            ? binding.etKey.getText().toString().trim()
            : "";

        if (key.isEmpty()) {
            showStatus(getString(R.string.enter_license_key), false);
            return;
        }

        // Dismiss keyboard
        InputMethodManager imm = (InputMethodManager)
            requireContext().getSystemService(Context.INPUT_METHOD_SERVICE);
        if (imm != null) imm.hideSoftInputFromWindow(
            binding.etKey.getWindowToken(), 0);

        setLoading(true);
        showStatus(getString(R.string.verifying), null);

        AuthManager.get().login(requireContext(), key, (success, message) -> {
            if (!isAdded()) return;
            setLoading(false);
            if (success) {
                showStatus(message, true);
                // Navigate to home via MainActivity
                if (getActivity() instanceof MainActivity) {
                    ((MainActivity) getActivity()).onLoginSuccess();
                }
            } else {
                showStatus(message, false);
            }
        });
    }

    private void setLoading(boolean loading) {
        binding.progressBar.setVisibility(loading ? View.VISIBLE : View.GONE);
        binding.btnLogin.setEnabled(!loading);
        binding.etKey.setEnabled(!loading);
    }

    /**
     * @param success null = neutral (info), true = green, false = red
     */
    private void showStatus(String message, Boolean success) {
        binding.tvStatus.setVisibility(View.VISIBLE);
        binding.tvStatus.setText(message);
        if (success == null) {
            binding.tvStatus.setTextColor(
                requireContext().getColor(R.color.text_secondary));
        } else if (success) {
            binding.tvStatus.setTextColor(
                requireContext().getColor(R.color.status_ok));
        } else {
            binding.tvStatus.setTextColor(
                requireContext().getColor(R.color.status_fail));
        }
    }

    @Override
    public void onDestroyView() {
        super.onDestroyView();
        binding = null;
    }
}
