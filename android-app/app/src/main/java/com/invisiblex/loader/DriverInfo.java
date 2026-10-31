package com.invisiblex.loader;

import java.util.ArrayList;
import java.util.List;

/**
 * Model for a driver file in assets/drivers/.
 *
 * Each driver now declares which permission chips to display in the
 * selector UI — READ, WRITE, EXECUTE, ROOT — so the user knows exactly
 * what privileges are required before loading.
 */
public class DriverInfo {

    /** Permission types shown as colored chips in the driver selector. */
    public enum Permission {
        /** Driver reads kernel memory / device state. */
        READ,
        /** Driver writes to kernel / device state. */
        WRITE,
        /** Driver executable code runs in kernel space. */
        EXECUTE,
        /** Root (uid=0) is required to load this driver. */
        ROOT
    }

    public final String          name;
    public final long            sizeBytes;
    public final String          sizeLabel;
    public final List<Permission> permissions;
    public final boolean         requiresRoot;

    /**
     * Full constructor — use for drivers with known permission requirements.
     */
    public DriverInfo(String name, long sizeBytes, List<Permission> permissions) {
        this.name         = name;
        this.sizeBytes    = sizeBytes;
        this.sizeLabel    = formatSize(sizeBytes);
        this.permissions  = permissions != null ? permissions : defaultPermissions(name);
        this.requiresRoot = this.permissions.contains(Permission.ROOT);
    }

    /**
     * Convenience constructor — permissions inferred from filename.
     */
    public DriverInfo(String name, long sizeBytes) {
        this(name, sizeBytes, null);
    }

    // ── Heuristic permission inference ───────────────────────────────────

    /**
     * Guess required permissions from the driver filename.
     * All kernel drivers need at minimum READ + EXECUTE.
     * KO / ELF kernel modules almost always need ROOT.
     * Userspace ELF executables need EXECUTE but not necessarily ROOT.
     */
    static List<Permission> defaultPermissions(String filename) {
        List<Permission> perms = new ArrayList<>();
        String lower = filename != null ? filename.toLowerCase() : "";

        perms.add(Permission.READ);

        // Kernel module or raw ELF driver — write capability to insmod registers
        if (lower.endsWith(".ko")
                || lower.contains("kernel")
                || lower.contains("driver")
                || lower.contains("drv")
                || lower.contains("kmod")) {
            perms.add(Permission.WRITE);
            perms.add(Permission.EXECUTE);
            perms.add(Permission.ROOT);
        } else if (lower.endsWith(".so")
                || lower.endsWith(".elf")
                || lower.startsWith("lib")) {
            // Shared library / ELF — needs execute, typically needs root via su -c
            perms.add(Permission.EXECUTE);
            perms.add(Permission.ROOT);
        } else {
            // Generic binary — execute + root by default
            perms.add(Permission.EXECUTE);
            perms.add(Permission.ROOT);
        }
        return perms;
    }

    // ── Helpers ──────────────────────────────────────────────────────────

    private static String formatSize(long bytes) {
        if (bytes < 1024)        return bytes + " B";
        if (bytes < 1024 * 1024) return String.format("%.1f KB", bytes / 1024f);
        return                          String.format("%.2f MB", bytes / (1024f * 1024f));
    }
}
