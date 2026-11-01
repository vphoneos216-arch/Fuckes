package com.invisiblex.loader

/**
 * DriverInfo — one selectable native backend mode from wanbai.h / c_driver.
 *
 * These are NOT files — the InvisibleX binary has 10 fixed in-binary driver
 * backends (+ AUTO-DETECT) compiled into c_driver (jni/src/wanbai.h). Which
 * one it uses on next launch is controlled by a single "driver" key in
 * config.json (jni/src/ix_driver_init.h → ix_get_driver_choice()), which
 * this app writes via DriverConfig.applyDriverChoice(). Selecting a row here
 * IS selecting the real kernel driver backend — nothing is extracted or
 * insmod'd from app assets.
 *
 * [TRIMMED SELECTION] wanbaiModes() exposes only 4 of the binary's modes:
 * RT-DEV, QX, Paradise, and DitPro-KPM — every other KPM backend
 * (TB-KPM, KMA-KPM, LDK-KPM) and the legacy GT/RT-PROC/AUTO-DETECT modes
 * have been removed from the selectable list entirely, per request. Among
 * the KPM-class backends specifically, DitPro-KPM is the ONLY one that
 * remains loadable/selectable at all — this is the sole KPM kept. "QX"
 * keeps QX1 only (of the binary's two QX variants) to match "rt-dev, qx,
 * paradise" as exactly 3 non-KPM entries; the native binary itself is
 * unchanged and still supports every mode — this is purely a UI-level
 * trim of what's offered in the picker.
 */
class DriverInfo(
    @JvmField val name: String,         // display label, e.g. "DitPro-KPM"
    @JvmField val configValue: String,  // written to config.json "driver" key
    @JvmField val hint: String,         // one-line description of how it works
    @JvmField val permissions: List<Permission>
) {
    /** Permission types shown as colored chips in the driver selector. */
    enum class Permission { READ, WRITE, EXECUTE, ROOT }

    @JvmField
    val requiresRoot: Boolean = permissions.contains(Permission.ROOT)

    companion object {
        private fun perms(vararg p: Permission): List<Permission> = p.toList()

        /**
         * The full, fixed list of backend modes c_driver's constructor can pick
         * — in the same order and with the same config values as
         * ix_driver_init.h / wanbai.h's g_DriverModeName().
         */
        @JvmStatic
        fun wanbaiModes(): List<DriverInfo> = listOf(
            DriverInfo(
                "RT-DEV", "rt-dev",
                "Opens a known /dev driver node directly — fastest path when the node already exists",
                perms(Permission.READ, Permission.WRITE, Permission.EXECUTE, Permission.ROOT)
            ),
            DriverInfo(
                "QX", "qx1",
                "Matches a /data/<6-char> process fd against a deleted /dev node, re-creates it",
                perms(Permission.READ, Permission.WRITE, Permission.EXECUTE, Permission.ROOT)
            ),
            DriverInfo(
                "Paradise", "paradise",
                "Paradise driver API — verified with a live process-alive probe before use",
                perms(Permission.READ, Permission.WRITE, Permission.EXECUTE, Permission.ROOT)
            ),
            DriverInfo(
                "DitPro-KPM", "ditpro",
                "Latest syscall-based KPM backend, targets kernel 6.12, supports pointer-chain reads",
                perms(Permission.READ, Permission.WRITE, Permission.EXECUTE, Permission.ROOT)
            )
        )
    }
}
