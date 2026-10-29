#ifndef IX_ANTIDEBUG_H
#define IX_ANTIDEBUG_H
/* ============================================================
   ix_antidebug.h — runtime anti-tamper detection (Android/arm64)

   Detects the common dynamic-analysis / hooking tools a cracker
   reaches for FIRST:
     - a native debugger attached (gdb/lldb/ptrace)       -> ix_dbg_*
     - Frida (frida-server, frida-gadget, default port)   -> ix_frida_*
     - Xposed / Substrate instrumentation frameworks       -> ix_hookfw_*
     - an inline hook planted on a critical function       -> ix_fn_hooked()

   HONEST SCOPE — read this:
   * This RAISES the cost of dynamic analysis. It does NOT make the
     binary un-analyzable. A skilled reverse engineer who opens this
     in Ghidra can locate each check and patch it out of the ELF.
     The point is to stop automated tools and the 95% who run stock
     Frida, not to be an absolute wall.
   * It deliberately does NOT flag root / Magisk / su. Rooted devices
     are normal for this userbase; flagging them would lock out
     legitimate users. Only ACTIVE instrumentation is flagged.
   * Every detector is best-effort and fails OPEN on its own I/O
     errors (can't read /proc, etc.) so a sandboxed-but-legit
     environment is not killed by a false positive.

   USAGE:
     #include "ix_antidebug.h"
     if (ix_tamper_detected()) { ... fail closed ... }
   The caller (login.h) wires the response into its existing
   fail-closed path (wipe secret + deny + optional hard-exit).
   ============================================================ */

#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <string>
#include <dirent.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// ---- small helper: read an entire small /proc file into a std::string ----
static inline std::string ix_read_file(const char* path, size_t cap = 256 * 1024) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return std::string();
    std::string out;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        out.append(buf, (size_t)n);
        if (out.size() > cap) break;   // bound it
    }
    close(fd);
    return out;
}

// ============================================================
// 1) DEBUGGER — TracerPid in /proc/self/status is nonzero when a
//    debugger (ptrace) is attached. Reliable, low false positive.
// ============================================================
static inline bool ix_dbg_tracerpid() {
    const std::string s = ix_read_file("/proc/self/status", 8192);
    if (s.empty()) return false;                   // fail open
    const size_t p = s.find("TracerPid:");
    if (p == std::string::npos) return false;
    size_t i = p + 10;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    // nonzero tracer pid = someone is ptrace-ing us
    return (i < s.size() && s[i] != '0' && s[i] != '\n');
}

// ============================================================
// 2) FRIDA — three independent signals, OR'd:
//    (a) frida / gum / gadget strings in the memory map
//    (b) frida's worker thread names in /proc/self/task/<tid>/comm
//    (c) frida-server listening on its default port 27042
//    Any one is a strong signal in THIS context.
// ============================================================
static inline bool ix_frida_in_maps() {
    const std::string m = ix_read_file("/proc/self/maps");
    if (m.empty()) return false;
    static const char* sig[] = {
        "frida", "frida-agent", "frida-gadget",
        "gum-js-loop", "libgadget", nullptr
    };
    for (int i = 0; sig[i]; ++i)
        if (m.find(sig[i]) != std::string::npos) return true;
    return false;
}

static inline bool ix_frida_thread_names() {
    DIR* d = opendir("/proc/self/task");
    if (!d) return false;                          // fail open
    bool hit = false;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        char path[256];
        snprintf(path, sizeof(path), "/proc/self/task/%s/comm", e->d_name);
        const std::string c = ix_read_file(path, 256);
        // Frida-specific worker threads only (NOT generic "gmain", which
        // legit GLib apps also use → would false-positive).
        if (c.find("gum-js-loop") != std::string::npos ||
            c.find("pool-frida")  != std::string::npos) {
            hit = true; break;
        }
    }
    closedir(d);
    return hit;
}

static inline bool ix_frida_default_port() {
    // frida-server default control port. Random-port / stealth modes
    // won't be caught here — that's fine, the maps/thread checks cover
    // the common cases. This is just one more cheap signal.
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;
    struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 200000; // 200ms
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(27042);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    bool open_port = (connect(s, (struct sockaddr*)&addr, sizeof(addr)) == 0);
    close(s);
    return open_port;
}

static inline bool ix_frida_present() {
    return ix_frida_in_maps() || ix_frida_thread_names() || ix_frida_default_port();
}

// ============================================================
// 3) XPOSED / SUBSTRATE — classloader/library instrumentation frameworks.
//    (NOT root — only the hooking frameworks themselves.)
// ============================================================
static inline bool ix_hookfw_present() {
    const std::string m = ix_read_file("/proc/self/maps");
    if (m.empty()) return false;
    static const char* sig[] = {
        "XposedBridge", "libxposed", "app_process_xposed",
        "substrate", "libsubstrate", "ElderDrivers", "lspd", nullptr
    };
    for (int i = 0; sig[i]; ++i)
        if (m.find(sig[i]) != std::string::npos) return true;
    return false;
}

// ============================================================
// 4) INLINE HOOK on a critical function — a hook usually overwrites the
//    function's first instruction with a branch/trampoline. On arm64 a
//    freshly-loaded function should NOT begin with an unconditional
//    branch to a far target as its very first instruction in this code.
//    We check the prologue of the passed function pointer.
//
//    arm64 encodings:
//      B  (unconditional branch)  : bits [31:26] == 000101   (0x14000000 mask 0xFC000000)
//      BR (branch to register)    : 0xD61F0000 mask 0xFFFFFC1F
//      BLR                        : 0xD63F0000 mask 0xFFFFFC1F
//    A trampoline hook commonly starts with LDR x16,#imm ; BR x16, or a
//    direct B. We flag a leading B/BR/BLR as a likely inline hook.
// ============================================================
static inline bool ix_fn_hooked(const void* fn) {
    if (!fn) return false;
    uint32_t insn;
    memcpy(&insn, fn, sizeof(insn));           // first 4 bytes = first instruction
    if ((insn & 0xFC000000u) == 0x14000000u) return true;   // B (unconditional)
    if ((insn & 0xFFFFFC1Fu) == 0xD61F0000u) return true;   // BR Xn
    if ((insn & 0xFFFFFC1Fu) == 0xD63F0000u) return true;   // BLR Xn
    return false;
}

// ============================================================
// AGGREGATE — any strong tamper signal.
// Debugger + Frida + hook frameworks. (Inline-hook checks on specific
// functions are called separately by the caller, passing &perform_auth
// etc., since taking a function address there keeps this header
// dependency-free.)
// ============================================================
static inline bool ix_tamper_detected() {
    if (ix_dbg_tracerpid())  return true;
    if (ix_frida_present())  return true;
    if (ix_hookfw_present()) return true;
    return false;
}

#endif // IX_ANTIDEBUG_H
