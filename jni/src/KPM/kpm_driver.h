#pragma once
// KPM/kpm_driver.h — Option 16 ("kpm"): userspace client for the custom
// inx_mem KPM (kpm/inx_mem/inx_mem.c), loaded under APatch via its
// supercall ABI.
//
// Authentication: the KernelPatch supercall ABI's own doc comment
// (vendored jni/include/external/kernelpatch/user/supercall.h) states it
// plainly for every sc_* call:
//
//   "@param key : superkey or 'su' string if caller uid is su allowed"
//
// i.e. if our process's uid/identity has been granted root via APatch's
// own su-allow list (the same "grant this app root" step APatch's manager
// app already does for any su-using app — this binary almost certainly
// already needs that grant today, since it talks to several other
// kernel drivers that require root), the literal string "su" works as the
// key with no secret involved at all — nothing to type, nothing to
// configure. That is what "no login, just load and it works" means here
// in concrete terms, and it is the ONLY reason the earlier blocking
// question about the superkey value is no longer a blocker: we try "su"
// first. It is only if that specific uid/grant isn't in place that a real
// superkey becomes necessary at all, and even then it is read from
// config.json at runtime (never hardcoded) as a fallback — see
// resolve_key() below.
//
// Scope (unchanged from what was already decided and explained): this
// driver does ONLY cross-process memory read/write through the KPM.
// GET_PID and MODULE_BASE stay exactly where every other driver in this
// project already does them — userspace, via GetModuleBase()'s existing
// /proc/<pid>/maps parsing (Scarecrow/MemoryTools.h) — reused here, not
// reimplemented.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <fstream>
#include <sys/stat.h>

#include "../ix_controls.h" // ix_controls_base_dir()
// NOT the full vendored user/supercall.h here -- it pulls in KernelPatch's
// own kernel-build type system (ktypes.h), which collides with the NDK's
// real libc headers (confirmed via a real CI failure, not guessed -- see
// supercall_min.h's own header comment for the exact errors). This project
// uses only sc_hello/sc_ready/sc_kpm_load/sc_kpm_control, transcribed
// byte-identical in body from that same vendored, pinned-commit file.
#include "../../include/external/kernelpatch/user/supercall_min.h"

// GetModuleBase() lives in Scarecrow/MemoryTools.h, already included by
// wanbai.h before this header — declared here only so this file can be
// read/compiled standalone too.
extern long int GetModuleBase(char *module, char *flags, int MapSize);

class KpmDriver {
public:
    explicit KpmDriver() {}

    ~KpmDriver() {
        // Deliberately does NOT unload the KPM on destruction: unloading
        // and reloading a KPM on every process restart is unnecessary
        // churn and (per the KPM's own init log) a few milliseconds of
        // kernel-symbol re-resolution each time. It stays loaded across
        // runs, same as this project's other kernel drivers.
    }

    bool initialize(pid_t target_pid) {
        pid_ = target_pid;

        if (!resolve_key()) {
            fprintf(stderr,
                "[-] KPM: no working key found. Neither \"su\" (uid-allowed root) "
                "nor a \"superkey\" field in config.json authenticated against "
                "APatch's supercall. Grant this binary root via the APatch app's "
                "Superuser list, OR add a real superkey to config.json's "
                "\"kpm_superkey\" field.\n");
            return false;
        }

        if (!ensure_loaded()) {
            fprintf(stderr, "[-] KPM: inx-mem failed to load (see dmesg for the "
                            "kernel-side reason — most likely one of "
                            "find_get_task_by_vpid/access_process_vm/"
                            "put_task_struct wasn't resolvable via kallsyms on "
                            "this kernel build; inx_mem.c logs which one).\n");
            return false;
        }

        char reply[16] = {0};
        long rc = sc_kpm_control(key_.c_str(), "hypervisor", "status", reply, sizeof(reply));
        if (rc < 0 || strncmp(reply, "ready", 5) != 0) {
            fprintf(stderr, "[-] KPM: inx-mem loaded but reports \"%s\" (rc=%ld) — "
                            "its own init log in dmesg names the missing kernel "
                            "symbol.\n", reply, rc);
            ready_ = false;
            return false;
        }

        ready_ = true;
        printf("\033[41m\033[37m[+] KPM Mode Active (key=%s)\033[0m\n",
               key_ == "su" ? "su (uid-allowed, no secret)" : "config superkey");
        return true;
    }

    bool is_ready() const { return ready_; }

    bool read(uintptr_t addr, void *buffer, size_t size) {
        if (!ready_ || size == 0 || size > kMaxCopy) return false;
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "r %d %lx %zu", (int)pid_, (unsigned long)addr, size);
        char hex[kMaxCopy * 2 + 1] = {0};
        long rc = sc_kpm_control(key_.c_str(), "hypervisor", cmd, hex, sizeof(hex));
        if (rc != (long)size) return false;
        return hex_decode(hex, (unsigned char *)buffer, size);
    }

    bool write(uintptr_t addr, const void *buffer, size_t size) {
        if (!ready_ || size == 0 || size > kMaxCopy) return false;
        char hex[kMaxCopy * 2 + 1];
        hex_encode((const unsigned char *)buffer, size, hex);
        char cmd[16 + kMaxCopy * 2 + 32];
        snprintf(cmd, sizeof(cmd), "w %d %lx %s", (int)pid_, (unsigned long)addr, hex);
        long rc = sc_kpm_control(key_.c_str(), "hypervisor", cmd, nullptr, 0);
        return rc == (long)size;
    }

    // Deliberately NOT implemented in-kernel (see file header + repo
    // notes). Delegates to the same userspace /proc/<pid>/maps parser
    // every other driver in this project already uses.
    uintptr_t get_module_base(const char *name) {
        char mod[256];
        snprintf(mod, sizeof(mod), "%s", name);
        return (uintptr_t)GetModuleBase(mod, (char *)"r-xp", sizeof(long) * 4);
    }

private:
    static constexpr size_t kMaxCopy = 512; // matches inx_mem.c's INX_MAX_COPY

    pid_t pid_ = 0;
    std::string key_;
    bool ready_ = false;

    // "su" first (no secret, works whenever this process's uid is already
    // su-allowed under APatch — see file header). Falls back to
    // config.json's "kpm_superkey" field, read fresh here (never cached,
    // never hardcoded) only if "su" doesn't authenticate.
    bool resolve_key() {
        if (sc_ready("su")) {
            key_ = "su";
            return true;
        }

        std::string cfg_key = read_superkey_from_config();
        if (!cfg_key.empty() && sc_ready(cfg_key.c_str())) {
            key_ = cfg_key;
            return true;
        }

        return false;
    }

    static std::string read_superkey_from_config() {
        std::string path = ix_controls_base_dir() + "/webroot/config.json";
        std::ifstream f(path);
        if (!f.good()) return "";
        std::string all((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
        size_t k = all.find("\"kpm_superkey\"");
        if (k == std::string::npos) return "";
        size_t c = all.find(':', k);
        if (c == std::string::npos) return "";
        size_t q1 = all.find('"', c);
        if (q1 == std::string::npos) return "";
        size_t q2 = all.find('"', q1 + 1);
        if (q2 == std::string::npos) return "";
        return all.substr(q1 + 1, q2 - q1 - 1);
    }

    bool ensure_loaded() {
        // Already loaded from a previous run / another process? "ping"
        // tells us without trying to load it twice.
        char reply[8] = {0};
        if (sc_kpm_control(key_.c_str(), "hypervisor", "ping", reply, sizeof(reply)) == 0 &&
            strncmp(reply, "pong", 4) == 0) {
            return true;
        }

        std::string kpm_path = ix_controls_base_dir() + "/inx_mem.kpm";
        struct stat st;
        if (stat(kpm_path.c_str(), &st) != 0) {
            fprintf(stderr, "[-] KPM: %s not found (expected it shipped next to "
                            "the module, not just this binary).\n", kpm_path.c_str());
            return false;
        }

        long rc = sc_kpm_load(key_.c_str(), kpm_path.c_str(), "", nullptr);
        return rc == 0;
    }

    static void hex_encode(const unsigned char *in, size_t len, char *out) {
        static const char hx[] = "0123456789abcdef";
        for (size_t i = 0; i < len; i++) {
            out[i * 2] = hx[(in[i] >> 4) & 0xF];
            out[i * 2 + 1] = hx[in[i] & 0xF];
        }
        out[len * 2] = '\0';
    }

    static bool hex_decode(const char *hex, unsigned char *out, size_t expected_len) {
        size_t n = 0;
        while (hex[0] && hex[1] && n < expected_len) {
            int hi = hexval(hex[0]), lo = hexval(hex[1]);
            if (hi < 0 || lo < 0) return false;
            out[n++] = (unsigned char)((hi << 4) | lo);
            hex += 2;
        }
        return n == expected_len;
    }

    static int hexval(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
};
