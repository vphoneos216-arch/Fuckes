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
                "AUTO-DETECT", "auto",
                "Scans RT drive → Paradise → DitPro KPM, picks the first that answers",
                perms(Permission.READ, Permission.EXECUTE)
            ),
            DriverInfo(
                "QX1", "qx1",
                "Matches a /data/<6-char> process fd against a deleted /dev node, re-creates it",
                perms(Permission.READ, Permission.WRITE, Permission.EXECUTE, Permission.ROOT)
            ),
            DriverInfo(
                "GT", "gt",
                "Scans /dev for a known driver node name (wanbai / CheckMe / Ckanri / lanran / video188)",
                perms(Permission.READ, Permission.WRITE, Permission.EXECUTE, Permission.ROOT)
            ),
            DriverInfo(
                "RT-DEV", "rt-dev",
                "Opens a known /dev driver node directly — fastest path when the node already exists",
                perms(Permission.READ, Permission.WRITE, Permission.EXECUTE, Permission.ROOT)
            ),
            DriverInfo(
                "RT-PROC", "rt-proc",
                "Scans /proc for a world-writable (0666) driver file instead of /dev",
                perms(Permission.READ, Permission.WRITE, Permission.EXECUTE, Permission.ROOT)
            ),
            DriverInfo(
                "QX2", "qx2",
                "Resolves a deleted-exe fd, reads its major/minor, builds the device node itself",
                perms(Permission.READ, Permission.WRITE, Permission.EXECUTE, Permission.ROOT)
            ),
            DriverInfo(
                "TB-KPM", "tb-kpm",
                "Memory read/write via prctl() — “Lag Free” KPM path",
                perms(Permission.READ, Permission.WRITE, Permission.EXECUTE, Permission.ROOT)
            ),
            DriverInfo(
                "DitPro-KPM", "ditpro",
                "Latest syscall-based KPM backend, targets kernel 6.12, supports pointer-chain reads",
                perms(Permission.READ, Permission.WRITE, Permission.EXECUTE, Permission.ROOT)
            ),
            DriverInfo(
                "KMA-KPM", "kma",
                "KMA KPM v6.12 — gid-based handle, initialises with a 90fps sync target",
                perms(Permission.READ, Permission.WRITE, Permission.EXECUTE, Permission.ROOT)
            ),
            DriverInfo(
                "LDK-KPM", "ldk",
                "LDK KPM — read-only backend, no memory-write support",
                perms(Permission.READ, Permission.EXECUTE, Permission.ROOT)
            ),
            DriverInfo(
                "Paradise", "paradise",
                "Paradise driver API — verified with a live process-alive probe before use",
                perms(Permission.READ, Permission.WRITE, Permission.EXECUTE, Permission.ROOT)
            )
        )
    }
}
