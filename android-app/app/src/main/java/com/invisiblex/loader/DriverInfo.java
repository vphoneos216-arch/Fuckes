package com.invisiblex.loader;

/**
 * Simple model for a driver file in assets/drivers/.
 */
public class DriverInfo {
    public final String name;
    public final long   sizeBytes;
    public final String sizeLabel;

    public DriverInfo(String name, long sizeBytes) {
        this.name      = name;
        this.sizeBytes = sizeBytes;
        this.sizeLabel = formatSize(sizeBytes);
    }

    private static String formatSize(long bytes) {
        if (bytes < 1024)       return bytes + " B";
        if (bytes < 1024*1024)  return String.format("%.1f KB", bytes / 1024f);
        return                         String.format("%.2f MB", bytes / (1024f*1024f));
    }
}
