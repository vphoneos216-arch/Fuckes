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

#define IX_HWID_FILE      "/data/adb/modules/invisiblex_loader/config/.ix_hwid"
#define IX_HWID_FP_FILE   "/data/adb/modules/invisiblex_loader/config/.ix_hwid_fp"
#define IX_HWID_FALLBACK  "/data/local/tmp/.ix_hwid_fp"

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

/* Collect durable hardware material (order matters for stability). */
static inline std::string ix_collect_hw_raw() {
    std::string raw;
    auto add = [&](const std::string& v) {
        if (v.empty() || v == "unknown") return;
        if (!raw.empty()) raw.push_back('|');
        raw += v;
    };

    /* 1) eMMC / UFS CID — survives factory reset on most devices */
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
        if (!v.empty()) { add(v); break; }
    }

    /* 2) SoC / chip serial */
    static const char* kSocPaths[] = {
        "/sys/devices/soc0/serial_number",
        "/sys/firmware/devicetree/base/serial-number",
        "/proc/device-tree/serial-number",
        nullptr
    };
    for (int i = 0; kSocPaths[i]; ++i) {
        std::string v = ix_read_file_raw(kSocPaths[i]);
        if (!v.empty()) { add(v); break; }
    }

    /* 3) Kernel cmdline androidboot.serialno */
    {
        std::string cmd = ix_read_file_raw("/proc/cmdline");
        const char* key = "androidboot.serialno=";
        size_t p = cmd.find(key);
        if (p != std::string::npos) {
            p += strlen(key);
            size_t e = cmd.find_first_of(" \t\n", p);
            add(cmd.substr(p, e == std::string::npos ? std::string::npos : e - p));
        }
    }

    /* 4) getprop serials (last resort among serials) */
    std::string ser = ix_getprop("ro.boot.serialno");
    if (ser.empty()) ser = ix_getprop("ro.serialno");
    add(ser);

    /* 5) Brand + model (weak alone, stabilizes combo) */
    add(ix_getprop("ro.product.brand"));
    add(ix_getprop("ro.product.model"));
    add(ix_getprop("ro.product.device"));

    /* 6) If still empty — rare; use boot_id once is BAD (changes). Prefer nothing. */
    if (raw.empty()) {
        add(ix_getprop("ro.boot.verifiedbootstate"));
        add(ix_getprop("ro.build.fingerprint"));
    }
    return raw;
}

/* Fingerprint of THIS hardware (not the display HWID). */
static inline std::string ix_hw_fingerprint() {
    return ix_hash16(ix_collect_hw_raw());
}

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
