package com.invisiblex.loader

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import com.invisiblex.loader.databinding.FragmentDriverBinding

/**
 * DriverFragment — lets the user pick which native backend mode
 * (wanbai.h / c_driver) the InvisibleX binary uses on its next launch.
 *
 * These are the 11 fixed in-binary modes (AUTO-DETECT + 10 named backends),
 * NOT files — see DriverInfo.wanbaiModes() and DriverConfig for exactly how
 * a selection here reaches the native side via config.json.
 *
 * Select flow:
 *   1. Show the fixed 11-mode list → user taps a row → selectedMode set.
 *   2. User taps "Load Driver" → security gate (NEVER bypassed) → write
 *      config.json → binary picks it up via ix_get_driver_choice() next run.
 *   3. loadMatchingAsset() loads the kernel-matched module for THAT SAME
 *      mode before the config write, so the extracted driver file and the
 *      config.json "driver" value the binary reads can never disagree.
 */
class DriverFragment : Fragment() {

    private var binding: FragmentDriverBinding? = null
    private lateinit var adapter: DriverAdapter
    private val driverList: List<DriverInfo> = DriverInfo.wanbaiModes()
    private var selectedMode: DriverInfo? = null

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        binding = FragmentDriverBinding.inflate(inflater, container, false)
        return binding!!.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        showKernelVersion()
        setupList()

        binding?.lvDrivers?.setOnItemClickListener { _, _, position, _ ->
            val mode = driverList[position]
            selectedMode = mode
            AppPrefs.setSelectedDriver(mode.configValue)
            adapter.setSelected(mode.configValue)
            updateSelectedCard()
            adapter.notifyDataSetChanged()
        }

        binding?.btnLoadDriver?.setOnClickListener { attemptApplyDriver() }
    }

    // ── Kernel version ──────────────────────────────────────────

    private fun showKernelVersion() {
        // KernelUtil.detect() can shell out (uname -r) — keep it off the UI thread.
        Thread {
            val kernel = KernelUtil.detect()
            if (!isAdded) return@Thread
            activity?.runOnUiThread {
                val b = binding ?: return@runOnUiThread
                b.tvKernelVersion.text = "Kernel: " + kernel.ifEmpty { "unknown" }
            }
        }.start()
    }

    // ── Driver list ───────────────────────────────────────────

    private fun setupList() {
        val b = binding ?: return
        b.tvNoDrivers.visibility = View.GONE
        b.lvDrivers.visibility = View.VISIBLE

        var savedValue = AppPrefs.getSelectedDriver()
        if (savedValue.isEmpty()) {
            savedValue = DriverConfig.readCurrentChoice()
        }
        val mode = findByConfigValue(savedValue)
        selectedMode = mode

        adapter = DriverAdapter(requireContext(), driverList, mode.configValue)
        b.lvDrivers.adapter = adapter
        updateSelectedCard()
    }

    private fun findByConfigValue(value: String?): DriverInfo {
        if (value == null) return driverList[0]
        return driverList.firstOrNull { it.configValue == value } ?: driverList[0] // fall back to AUTO-DETECT
    }

    private fun updateSelectedCard() {
        val b = binding ?: return
        val mode = selectedMode
        if (mode == null) {
            b.cardSelectedDriver.visibility = View.GONE
            b.btnLoadDriver.isEnabled = false
            return
        }
        b.cardSelectedDriver.visibility = View.VISIBLE
        b.tvSelectedDriver.text = mode.name
        b.tvDriverSize.text = mode.configValue
        b.btnLoadDriver.isEnabled = true
    }

    // ── Apply (security-gated) ─────────────────────────────────

    private fun attemptApplyDriver() {
        val mode = selectedMode
        if (mode == null) {
            showStatus(getString(R.string.select_driver_first), false)
            return
        }

        setLoading(true)
        showStatus(getString(R.string.verifying_auth), null)

        // Security gate — mandatory before every driver/binary operation.
        // NEVER bypass this, regardless of how the fragment is reached.
        AuthManager.get().securityGate(requireContext()) { authOk, _ ->
            if (!isAdded) return@securityGate
            if (!authOk) {
                setLoading(false)
                showStatus(getString(R.string.auth_required_driver), false)
                return@securityGate
            }
            // Auth passed — write the backend choice for the next binary launch,
            // then load the kernel-matched module for this mode (if one is
            // bundled in assets/drivers/ for the device's running kernel).
            val configValue = mode.configValue
            DriverConfig.applyDriverChoice(configValue) { configOk, configMsg ->
                if (!configOk) {
                    if (!isAdded) return@applyDriverChoice
                    activity?.runOnUiThread {
                        setLoading(false)
                        showStatus(configMsg, false)
                    }
                    return@applyDriverChoice
                }
                DriverConfig.loadMatchingAsset(
                    requireContext().applicationContext,
                    configValue
                ) { assetOk, assetMsg ->
                    if (!isAdded) return@loadMatchingAsset
                    activity?.runOnUiThread {
                        setLoading(false)
                        showStatus(if (assetOk) assetMsg else "$configMsg — $assetMsg", assetOk)
                        if (assetOk) openLaunchMenu()
                    }
                }
            }
        }
    }

    // ── Post-launch menu ──────────────────────────────────────

    private fun openLaunchMenu() {
        if (!isAdded) return
        val intent = Intent(requireContext(), LaunchMenuActivity::class.java)
        intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP)
        startActivity(intent)
    }

    // ── UI helpers ────────────────────────────────────────────

    private fun setLoading(loading: Boolean) {
        if (!isAdded) return
        val b = binding ?: return
        b.progressDriver.visibility = if (loading) View.VISIBLE else View.GONE
        b.btnLoadDriver.isEnabled = !loading && selectedMode != null
    }

    private fun showStatus(msg: String, success: Boolean?) {
        val b = binding ?: return
        b.tvDriverStatus.visibility = View.VISIBLE
        b.tvDriverStatus.text = msg
        val colorRes = when (success) {
            null -> R.color.text_secondary
            true -> R.color.status_ok
            false -> R.color.status_fail
        }
        b.tvDriverStatus.setTextColor(requireContext().getColor(colorRes))
    }

    override fun onDestroyView() {
        super.onDestroyView()
        binding = null
    }
}
