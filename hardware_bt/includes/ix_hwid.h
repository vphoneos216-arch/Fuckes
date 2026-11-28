#pragma once
/* ============================================================
   ix_hwid.h — device-bound HWID (anti copy / anti swap)

   Bug fixed:
     Old version trusted ONLY /data/adb/.../.ix_hwid file.
     Copying that file to another phone reused the same HWID
     and the same license key.

   New rules:
     1. Fingerprint is built from REAL hardware sources
        (eMMC/UFS CID, SoC serial, boot serial, product).
     2. Stored file is only a cache — it must MATCH current
        hardware fingerprint or it is ignored and regenerated.
     3. Each physical device gets a stable unique IX-… id.
     4. Factory-reset-safe as long as eMMC CID / SoC serial
        stay the same; ROM flash usually still keeps CID.

   login.h: kd_get_hwid() should call ix_shared_hwid().
   ============================================================ */

#include <string>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#define IX_HWID_FILE        "/data/adb/modules/invisiblex_loader/config/.ix_hwid"
#define IX_HWID_FP_FILE     "/data/adb/modules/invisiblex_loader/config/.ix_hwid_fp"
#define IX_HWID_FALLBACK    "/data/local/tmp/.ix_hwid_fp"
#define IX_HWID_FIELDS_FILE "/data/adb/modules/invisiblex_loader/config/.ix_hwid_fields"

static inline std::string ix_trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static inline std::string ix_read_file_raw(const char* path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return "";
    char buf[512];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return "";
    buf[n] = 0;
    return ix_trim(std::string(buf));
}

static inline std::string ix_getprop(const char* key) {
    std::string cmd = std::string("getprop ") + key + " 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "";
    char buf[256] = {0};
    if (!fgets(buf, sizeof(buf), p)) { pclose(p); return ""; }
    pclose(p);
    return ix_trim(std::string(buf));
}

/* FNV-1a 64-bit → 16 hex chars */
static inline std::string ix_hash16(const std::string& in) {
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned char c : in) { h ^= c; h *= 1099511628211ULL; }
    char out[17];
    snprintf(out, sizeof(out), "%016llX", (unsigned long long)h);
    return std::string(out);
}

/* ── Per-field cache (bug fix: "max devices reached" on the SAME phone) ──
 *
 * Root cause this fixes: ix_collect_hw_raw() used to concatenate several
 * hardware fields fresh on EVERY call. Any one of them going transiently
 * unreadable — a CID sysfs path missing on a given kernel build, getprop
 * timing out, a driver-mode switch temporarily changing what /sys exposes —
 * silently changed the WHOLE concatenated string, so ix_hw_fingerprint()
 * produced a different hash, the cached HWID no longer matched, and this
 * genuinely-the-same physical device got re-registered with the server as
 * a brand-new device. That eats into max_devices on every such hiccup,
 * which is exactly the "max device reached" reports on a device that never
 * actually changed. A reseller's freshly-issued key showing "expired" is
 * very often this same symptom one step downstream — the server's own
 * expired/limit message surfaces once the device-slot budget is gone.
 *
 * Fix: cache each field INDIVIDUALLY. A fresh successful read updates its
 * own cached value; a fresh read that fails/returns empty falls back to
 * that field's last known-good cached value instead of dropping out of
 * the fingerprint. A genuinely different phone has no cache file at all,
 * so every field there is a true fresh read — detection of an actually
 * different device is unaffected. Only transient same-device flakiness is
 * smoothed out.
 *
 * NOTE: ix_mkdir_parent() / ix_write_secret() are defined right here
 * (moved up from below ix_hw_fingerprint()) because ix_collect_hw_raw()
 * below needs ix_write_secret() for the new per-field cache file, and a
 * static-inline helper must be defined before first use in this header.
 */
static inline void ix_mkdir_parent() {
    ::mkdir("/data/adb", 0755);
    ::mkdir("/data/adb/modules", 0755);
    ::mkdir("/data/adb/modules/invisiblex_loader", 0755);
    ::mkdir("/data/adb/modules/invisiblex_loader/config", 0755);
}

static inline void ix_write_secret(const char* path, const std::string& v) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return;
    (void)write(fd, v.data(), v.size());
    close(fd);
    ::chmod(path, 0600);
}

static inline std::string ix_fields_cache_get(const std::string& all, const char* key) {
    std::string marker = std::string(key) + "=";
    size_t p = 0;
    while (p < all.size()) {
        size_t nl = all.find('\n', p);
        std::string line = all.substr(p, nl == std::string::npos ? std::string::npos : nl - p);
        if (line.rfind(marker, 0) == 0) return line.substr(marker.size());
        if (nl == std::string::npos) break;
        p = nl + 1;
    }
    return "";
}

static inline std::string ix_stable_field(const char* key,
                                           const std::string& cacheRaw,
                                           const std::string& freshValue,
                                           std::string& outUpdatedCache) {
    std::string value = freshValue;
    if (value.empty() || value == "unknown") {
        value = ix_fields_cache_get(cacheRaw, key); // fall back to last-known-good
    }
    if (!value.empty()) {
        outUpdatedCache += std::string(key) + "=" + value + "\n";
    }
    return value;
}

/* Collect durable hardware material (order matters for stability). */
static inline std::string ix_collect_hw_raw() {
    ix_mkdir_parent(); // ensure config dir exists before the field-cache write below
    std::string cacheRaw = ix_read_file_raw(IX_HWID_FIELDS_FILE);
    std::string newCache; // rebuilt below, written back once at the end

    std::string raw;
    auto add = [&](const std::string& v) {
        if (v.empty() || v == "unknown") return;
        if (!raw.empty()) raw.push_back('|');
        raw += v;
    };

    /* 1) eMMC / UFS CID — survives factory reset on most devices */
    std::string cidFresh;
    {
        static const char* kCidPaths[] = {
            "/sys/block/mmcblk0/device/cid",
            "/sys/class/block/mmcblk0/device/cid",
            "/sys/block/sda/device/serial",
            "/sys/block/sda/device/cid",
            "/sys/devices/platform/soc/1d84000.ufshc/host0/target0:0:0/0:0:0:0/scsi_generic/sg0/device/serial",
            nullptr
        };
        for (int i = 0; kCidPaths[i]; ++i) {
            std::string v = ix_read_file_raw(kCidPaths[i]);
            if (!v.empty()) { cidFresh = v; break; }
        }
    }
    add(ix_stable_field("cid", cacheRaw, cidFresh, newCache));

    /* 2) SoC / chip serial */
    std::string socFresh;
    {
        static const char* kSocPaths[] = {
            "/sys/devices/soc0/serial_number",
            "/sys/firmware/devicetree/base/serial-number",
            "/proc/device-tree/serial-number",
            nullptr
        };
        for (int i = 0; kSocPaths[i]; ++i) {
            std::string v = ix_read_file_raw(kSocPaths[i]);
            if (!v.empty()) { socFresh = v; break; }
        }
    }
    add(ix_stable_field("soc", cacheRaw, socFresh, newCache));

    /* 3) Kernel cmdline androidboot.serialno */
    std::string cmdlineFresh;
    {
        std::string cmd = ix_read_file_raw("/proc/cmdline");
        const char* key = "androidboot.serialno=";
        size_t p = cmd.find(key);
        if (p != std::string::npos) {
            p += strlen(key);
            size_t e = cmd.find_first_of(" \t\n", p);
            cmdlineFresh = cmd.substr(p, e == std::string::npos ? std::string::npos : e - p);
        }
    }
    add(ix_stable_field("cmdline", cacheRaw, cmdlineFresh, newCache));

    /* 4) getprop serials (last resort among serials) */
    std::string serFresh = ix_getprop("ro.boot.serialno");
    if (serFresh.empty()) serFresh = ix_getprop("ro.serialno");
    add(ix_stable_field("serial", cacheRaw, serFresh, newCache));

    /* 5) Brand + model (weak alone, stabilizes combo) */
    add(ix_stable_field("brand",  cacheRaw, ix_getprop("ro.product.brand"),  newCache));
    add(ix_stable_field("model",  cacheRaw, ix_getprop("ro.product.model"),  newCache));
    add(ix_stable_field("device", cacheRaw, ix_getprop("ro.product.device"), newCache));

    /* 6) If still empty — rare; use boot_id once is BAD (changes). Prefer nothing. */
    if (raw.empty()) {
        add(ix_stable_field("verifiedboot", cacheRaw, ix_getprop("ro.boot.verifiedbootstate"), newCache));
        add(ix_stable_field("buildfp",      cacheRaw, ix_getprop("ro.build.fingerprint"),       newCache));
    }

    if (!newCache.empty()) {
        ix_write_secret(IX_HWID_FIELDS_FILE, newCache);
    }
    return raw;
}

/* Fingerprint of THIS hardware (not the display HWID). */
static inline std::string ix_hw_fingerprint() {
    return ix_hash16(ix_collect_hw_raw());
}

static inline std::string ix_format_hwid(const std::string& h16) {
    /* IX-XXXX-XXXXXX-XXXXXX  from 16 hex */
    std::string h = h16;
    if (h.size() < 16) h.append(16 - h.size(), '0');
    char buf[40];
    snprintf(buf, sizeof(buf), "IX-%s-%s-%s",
             h.substr(0, 4).c_str(),
             h.substr(4, 6).c_str(),
             h.substr(10, 6).c_str());
    return std::string(buf);
}

/*
 * Public API — same name as before so login.h needs no logic change.
 *
 * - Always compute fingerprint from hardware.
 * - Cached HWID is accepted ONLY if fingerprint file matches current HW.
 * - Copied .ix_hwid from another phone → fingerprint mismatch → new ID.
 */
static inline std::string ix_shared_hwid() {
    const std::string fp = ix_hw_fingerprint();
    if (fp.size() < 8) {
        /* Absolute last resort — still device-ish */
        return ix_format_hwid(ix_hash16("fallback|" + std::to_string((long long)getpid())));
    }

    std::string cached_fp = ix_read_file_raw(IX_HWID_FP_FILE);
    if (cached_fp.empty())
        cached_fp = ix_read_file_raw(IX_HWID_FALLBACK);

    std::string cached_id = ix_read_file_raw(IX_HWID_FILE);

    /* Accept cache only when fingerprint matches THIS device */
    if (!cached_id.empty() && cached_id.size() >= 8 &&
        !cached_fp.empty() && cached_fp == fp) {
        return cached_id;
    }

    /* Mismatch or missing → bind to this hardware (cannot reuse other phone's file) */
    std::string hwid = ix_format_hwid(fp);

    ix_mkdir_parent();
    ix_write_secret(IX_HWID_FILE, hwid);
    ix_write_secret(IX_HWID_FP_FILE, fp);
    ix_write_secret(IX_HWID_FALLBACK, fp);

    return hwid;
}

/* Optional: force regenerate (debug). Not used by login. */
static inline void ix_hwid_reset() {
    unlink(IX_HWID_FILE);
    unlink(IX_HWID_FP_FILE);
    unlink(IX_HWID_FALLBACK);
}
