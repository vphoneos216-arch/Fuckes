#include "main.h"
#include "ix_driver_init.h"
#include "login.h"
#include "ix_features.h"
#include "ix_rt_offsets.h"
#include "collider_ipc.h"
#include "Scarecrow/ix_inject.h"
#include <string>
#include <map>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#include <ctime>
#include <cerrno>
#include <elf.h>
#include <thread>
#include <chrono>
#include <linux/input.h>
#include <poll.h>
#include <sys/stat.h>
#include <cmath>
#include <random>
#include <atomic>
#include <mutex>
#include <ctime>
#include "Scarecrow/oxorany_include.h"
#include "Scarecrow/oxorany.h"
// [NEW — internal collider aim-lock] Shared-memory handle to the
// collider_stub.so injected into the game process. nullptr until the
// injection in main() below succeeds (or if it fails/is skipped) —
// hook.h's AimLock code checks this and falls back to its existing
// direct in-process HWBP call whenever it's null, so the feature
// never silently stops working because the stub wasn't loaded.
IxColliderIpc* g_ColliderIpc = nullptr;
// [NEW] Menu toggle — "Internal Collider Lock" on the Settings page.
// On by default (matches what was asked: collider-lock should run via
// the injected stub whenever possible). Switching it off makes
// hook.h's AimLock code use the direct in-process HWBP call even if
// the stub is alive, with no other effect on anything else.
bool g_ColliderInternalEnabled = true;

// Forward-declared here, defined further down (needs game_pid, which
// isn't known yet at this point in the file) — SourceMenu.h's "Inject
// Collider Stub" button calls this directly.
void ix_retry_collider_injection();

#include "wanbai.h"
#include "Scarecrow/hook.h"
#include "UI/bar.hpp"

// WebUI Settings → menu_open: "vol" | "bar" (loader exports IX_MENU_OPEN)
static std::string g_MenuOpenMode = "bar";

static void LoadMenuOpenMode() {
    // Reads the WebUI module's config.json ("menu_open": "vol"|"bar"),
    // same file/key the driver choice already comes from — see
    // ix_driver_init.h's ix_get_menu_open_mode(). Defaults to "bar".
    g_MenuOpenMode = ix_get_menu_open_mode();
}

// =====================================================================
// [NEW] Crash logging — "Driver attached ... (signal 11)" told us WHERE
// the run got to, but nothing about WHY: no fault address, no idea
// whether the bad access was inside this binary, inside the loaded
// driver's kernel interface, or inside the game's own libraries. This
// does not fix the Android 17 crash (no log evidence exists yet that
// points at any one specific cause — guessing at a fix with nothing to
// go on would risk hiding the real bug instead of catching it), but it
// means the NEXT crash on that device writes down the one thing that
// turns "it crashed" into something fixable: the fault address, which
// region of /proc/self/maps it falls in, and which binary/library that
// is. Deliberately only async-signal-safe calls here (write/open/
// read/close, no malloc, no printf/FILE*, no std::string formatting) —
// the process is already in an undefined state when this runs.
// =====================================================================
static int g_crashLogFd = -1;

// [FIX — terminal must show the exact reason directly, no hunting for
// a separate file] write() is async-signal-safe, so the same raw
// bytes go to both the log file AND to the terminal (STDOUT_FILENO) —
// whichever fds are valid. stdout is always open on a standalone run,
// so this always shows.
static void ix_write_raw_to(int fd, const char* s, size_t len) {
    if (fd < 0) return;
    ssize_t off = 0;
    while (off < (ssize_t)len) {
        ssize_t w = write(fd, s + off, len - off);
        if (w <= 0) break;
        off += w;
    }
}

static void ix_write_raw(const char* s) {
    if (!s) return;
    size_t len = 0;
    while (s[len]) ++len;
    ix_write_raw_to(g_crashLogFd, s, len);
    ix_write_raw_to(STDOUT_FILENO, s, len);
}

// Async-signal-safe unsigned hex formatter (no snprintf/sprintf — those
// are not guaranteed async-signal-safe, and this runs inside the
// handler itself).
static void ix_write_hex(uintptr_t v) {
    char buf[2 + sizeof(uintptr_t) * 2 + 1];
    buf[0] = '0'; buf[1] = 'x';
    const char* digits = "0123456789abcdef";
    int n = (int)(sizeof(uintptr_t) * 2);
    for (int i = 0; i < n; ++i) {
        buf[2 + i] = digits[(v >> ((n - 1 - i) * 4)) & 0xF];
    }
    buf[2 + n] = '\0';
    ix_write_raw(buf);
}

static void ix_write_int(long v) {
    char buf[24];
    int i = 23;
    buf[i--] = '\0';
    bool neg = v < 0;
    unsigned long uv = neg ? (unsigned long)(-v) : (unsigned long)v;
    if (uv == 0) buf[i--] = '0';
    while (uv > 0) { buf[i--] = '0' + (uv % 10); uv /= 10; }
    if (neg) buf[i--] = '-';
    ix_write_raw(&buf[i + 1]);
}

static const char* ix_signal_name(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (bad memory access)";
        case SIGABRT: return "SIGABRT (abort)";
        case SIGBUS:  return "SIGBUS (bad memory alignment/mapping)";
        case SIGILL:  return "SIGILL (illegal instruction)";
        case SIGFPE:  return "SIGFPE (arithmetic error)";
        default:      return "unknown signal";
    }
}

static const char* ix_sigcode_name(int sig, int code) {
    if (sig == SIGSEGV || sig == SIGBUS) {
        switch (code) {
            case SEGV_MAPERR: return "SEGV_MAPERR (address not mapped)";
            case SEGV_ACCERR: return "SEGV_ACCERR (no permission for that address)";
            default: return "";
        }
    }
    return "";
}

static void ix_crash_handler(int sig, siginfo_t* info, void* /*ucontext*/) {
    // Always runs even if the log file failed to open — ix_write_raw()
    // mirrors to STDOUT_FILENO regardless, which is the whole point of
    // this change: the exact reason has to show on screen right there,
    // not only in a file you have to go find afterward.
    ix_write_raw("\n=== CRASH ===\n");
    ix_write_raw("signal: ");
    ix_write_raw(ix_signal_name(sig));
    ix_write_raw("\nreason: ");
    ix_write_raw(ix_sigcode_name(sig, info ? info->si_code : 0));
    ix_write_raw("\nsignal_num=");
    ix_write_int(sig);
    ix_write_raw(" si_code=");
    ix_write_int(info ? info->si_code : 0);
    ix_write_raw(" fault_addr=");
    ix_write_hex(info ? (uintptr_t)info->si_addr : 0);
    ix_write_raw(" pid=");
    ix_write_int((long)getpid());
    ix_write_raw(" tid=");
    ix_write_int((long)gettid());
    ix_write_raw("\n--- /proc/self/maps (full dump written to crash.log) ---\n");

    int mapsFd = open("/proc/self/maps", O_RDONLY);
    if (mapsFd >= 0) {
        char buf[4096];
        ssize_t n;
        while ((n = read(mapsFd, buf, sizeof(buf))) > 0) {
            // Maps dump is long — only the log file gets the full dump,
            // not the terminal, so the terminal output above (signal,
            // reason, fault address) stays the thing you actually read
            // at a glance.
            ix_write_raw_to(g_crashLogFd, buf, (size_t)n);
        }
        close(mapsFd);
    }
    ix_write_raw("--- end maps ---\n");
    if (g_crashLogFd >= 0) fsync(g_crashLogFd);

    // Restore the default handler and re-raise, so the process still
    // terminates/core-dumps the normal way — this handler only logs,
    // it never tries to recover from a corrupted process.
    signal(sig, SIG_DFL);
    raise(sig);
}

static void ix_install_crash_handler() {
#ifdef IX_INTERACTIVE_CLI
    std::string logPath = kd_exe_dir() + "/crash.log";
#else
    std::string logPath = std::string(INX_MODULE_DIR) + "/webroot/cheat.log";
#endif
    g_crashLogFd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);

#ifdef IX_INTERACTIVE_CLI
    // Printed so the exact path is on-screen instead of something to
    // guess at after a crash.
    if (g_crashLogFd >= 0) {
        printf("[INX] crash log: %s\n", logPath.c_str());
    } else {
        printf("[INX] WARNING: could not open crash log at %s (errno=%d)\n",
               logPath.c_str(), errno);
    }
    fflush(stdout);
#endif

    // [FIX] A handler installed on the normal stack goes silent on a
    // stack-overflow-class SIGSEGV — the fault IS the stack, so the
    // signal delivery itself has nowhere to push a frame, and the
    // process dies with nothing written, which is indistinguishable
    // from "handler never ran". A dedicated alt stack (SA_ONSTACK)
    // means the handler still runs even then, so an empty/missing
    // crash.log after this build means the crash is NOT a stack
    // overflow, not that logging silently failed.
    static char altStack[SIGSTKSZ * 4];
    stack_t ss;
    ss.ss_sp = altStack;
    ss.ss_size = sizeof(altStack);
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = ix_crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
}

#ifdef IX_INTERACTIVE_CLI
// [NEW — "lib ko bhi binary mein daal do, only binary do"] The
// standalone build no longer ships libcollider_stub.so as a second
// file — build.yml's "Embed collider_stub.so into inx_standalone"
// step adds it as an extra ELF section (named "collider_stub") inside
// this very binary. This reads that section back out of this
// process's own /proc/self/exe and writes it to a short-lived temp
// file next to the binary — the injector still needs an actual path
// on disk to remote-dlopen, but nothing the user has to keep paired
// with inx_standalone; the temp file is deleted right after the
// injection attempt, successful or not.
// [NEW — root cause of "could not open /proc/self/exe" on lobby test]
// Two independent ways the PREVIOUS fopen("/proc/self/exe") could fail
// without any further diagnostic: (a) some hardened SELinux policies
// specifically deny traversing the /proc/<pid>/exe magic symlink even
// when the real file underneath is perfectly readable to that process
// (this is a known anti-introspection hardening some OEM security
// layers add — it denies the symlink *class*, not the file), or
// (b) the symlink's target has gone stale for some other reason. The
// previous code also never printed errno, so there was no way to
// tell which. This resolves the real absolute path two ways and
// opens THAT directly instead of going through the symlink, with
// errno printed at every step so a repeat failure is diagnosable
// instead of another dead end.
static std::string ix_resolve_own_exe_path() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        // A path ending in " (deleted)" means the backing file was
        // unlinked after exec — the inode is still valid and openable
        // via the symlink in that case, but not via this resolved
        // string, so don't return it.
        std::string resolved(buf);
        if (resolved.size() < 10 || resolved.substr(resolved.size() - 10) != " (deleted)") {
            return resolved;
        }
        printf("[INX] collider-stub: /proc/self/exe resolved to a deleted backing file: %s\n", resolved.c_str());
    } else {
        printf("[INX] collider-stub: readlink(/proc/self/exe) failed, errno=%d (%s)\n", errno, strerror(errno));
    }

    // Fallback: the executable's own mapping is always present in
    // /proc/self/maps (the crash handler already reads this file
    // successfully elsewhere, so this path is known to work even when
    // the /proc/self/exe symlink itself is blocked) — its first
    // r-xp-class entry with an absolute path is this binary itself.
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        printf("[INX] collider-stub: fallback fopen(/proc/self/maps) also failed, errno=%d (%s)\n", errno, strerror(errno));
        return "";
    }
    char line[1024];
    std::string found;
    while (fgets(line, sizeof(line), maps)) {
        char* slash = strchr(line, '/');
        if (!slash) continue;
        char* nl = strchr(slash, '\n');
        if (nl) *nl = '\0';
        if (strstr(slash, "(deleted)")) continue;
        found = slash;
        break; // first mapped absolute path is the executable's own load segment
    }
    fclose(maps);
    if (found.empty()) {
        printf("[INX] collider-stub: no usable path found in /proc/self/maps either\n");
    }
    return found;
}

static std::string ix_extract_embedded_collider_stub() {
    std::string exePath = ix_resolve_own_exe_path();
    if (exePath.empty()) {
        printf("[INX] collider-stub: could not determine this binary's own path by any method\n");
        return "";
    }

    FILE* f = fopen(exePath.c_str(), "rb");
    if (!f) {
        printf("[INX] collider-stub: could not open %s, errno=%d (%s)\n", exePath.c_str(), errno, strerror(errno));
        return "";
    }

    Elf64_Ehdr ehdr{};
    if (fread(&ehdr, sizeof(ehdr), 1, f) != 1 ||
        memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
        printf("[INX] collider-stub: not a readable ELF image\n");
        fclose(f);
        return "";
    }

    std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
    if (fseek(f, (long)ehdr.e_shoff, SEEK_SET) != 0 ||
        fread(shdrs.data(), sizeof(Elf64_Shdr), ehdr.e_shnum, f) != ehdr.e_shnum) {
        printf("[INX] collider-stub: failed reading section header table\n");
        fclose(f);
        return "";
    }

    const Elf64_Shdr& shstrtabHdr = shdrs[ehdr.e_shstrndx];
    std::vector<char> shstrtab(shstrtabHdr.sh_size);
    if (fseek(f, (long)shstrtabHdr.sh_offset, SEEK_SET) != 0 ||
        fread(shstrtab.data(), 1, shstrtabHdr.sh_size, f) != shstrtabHdr.sh_size) {
        printf("[INX] collider-stub: failed reading section name table\n");
        fclose(f);
        return "";
    }

    const Elf64_Shdr* target = nullptr;
    for (const auto& sh : shdrs) {
        if (sh.sh_name < shstrtab.size() &&
            strcmp(&shstrtab[sh.sh_name], "collider_stub") == 0) {
            target = &sh;
            break;
        }
    }
    if (!target || target->sh_size == 0) {
        printf("[INX] collider-stub: no embedded \"collider_stub\" section found in this binary "
               "(built without the CI embed step?)\n");
        fclose(f);
        return "";
    }

    std::vector<uint8_t> blob(target->sh_size);
    if (fseek(f, (long)target->sh_offset, SEEK_SET) != 0 ||
        fread(blob.data(), 1, target->sh_size, f) != target->sh_size) {
        printf("[INX] collider-stub: failed reading embedded section data\n");
        fclose(f);
        return "";
    }
    fclose(f);

    std::string outPath = kd_exe_dir() + "/.collider_stub_tmp.so";
    FILE* out = fopen(outPath.c_str(), "wb");
    if (!out) {
        printf("[INX] collider-stub: could not write temp file at %s (errno=%d)\n",
               outPath.c_str(), errno);
        return "";
    }
    size_t wrote = fwrite(blob.data(), 1, blob.size(), out);
    fclose(out);
    if (wrote != blob.size()) {
        printf("[INX] collider-stub: short write extracting embedded .so\n");
        unlink(outPath.c_str());
        return "";
    }
    // [FIX — root cause of "couldn't map ... segment 1: Permission
    // denied" from dlerror()] This was chmod'ing the extracted .so to
    // 0644 — no execute bit for anyone. dlopen() maps a shared
    // library's code segments with PROT_EXEC, and the Linux dynamic
    // linker refuses that mapping outright on a file with no execute
    // permission bit set, independent of SELinux — this alone is
    // enough to produce exactly the dlerror() text just reported.
    // 0755 is what every other .so on the system ships with.
    if (chmod(outPath.c_str(), 0755) != 0) {
        printf("[INX] collider-stub: chmod 0755 on %s failed, errno=%d (%s)\n",
               outPath.c_str(), errno, strerror(errno));
    }

    // Best-effort second layer: some OEM SELinux policies additionally
    // deny mapping PROT_EXEC from a file labeled shell_data_file (the
    // label files written under /data/local/tmp normally get) inside
    // an app's own domain. Since this process is already running as
    // root to have attached the driver, relabelling to system_file —
    // the context every app domain is already allowed to execute code
    // from, since every process loads system libraries — costs nothing
    // if it's not needed and fixes it if it is. Logged either way so
    // a repeat failure after this still shows the real reason instead
    // of guessing further blind.
    {
        std::string chconCmd = "chcon u:object_r:system_file:s0 '" + outPath + "' 2>&1";
        FILE* p = popen(chconCmd.c_str(), "r");
        if (p) {
            char out[256] = {0};
            size_t got = fread(out, 1, sizeof(out) - 1, p);
            int rc = pclose(p);
            if (rc != 0) {
                printf("[INX] collider-stub: chcon to system_file failed (rc=%d)%s%s\n",
                       rc, got ? ": " : "", got ? out : "");
            }
        } else {
            printf("[INX] collider-stub: could not run chcon, errno=%d (%s)\n", errno, strerror(errno));
        }
    }

    return outPath;
}
#endif

#ifdef IX_INTERACTIVE_CLI
// [NEW — "error terminal mein pura dikhna chahiye"] Terminal apps
// (Termux etc.) have limited scrollback, so a long run's early output
// can be gone by the time something crashes near the end. This tees
// everything written to stdout/stderr to a persistent log file as
// well as the real terminal — same text, both places — so there's
// always a complete copy to send over instead of relying on what the
// terminal still has on screen.
static void ix_install_output_tee() {
    std::string logPath = kd_exe_dir() + "/run.log";
    int logFd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (logFd < 0) return; // best-effort — a tee failure is not worth stopping for

    int pipeFds[2];
    if (pipe(pipeFds) != 0) { close(logFd); return; }

    int realStdout = dup(STDOUT_FILENO);
    if (realStdout < 0) { close(logFd); close(pipeFds[0]); close(pipeFds[1]); return; }

    // Both stdout AND stderr feed the same pipe, so crash-handler output
    // (which also goes to STDOUT_FILENO directly — see ix_crash_handler)
    // and anything printed via stderr end up in the one log too.
    dup2(pipeFds[1], STDOUT_FILENO);
    dup2(pipeFds[1], STDERR_FILENO);
    close(pipeFds[1]);
    setvbuf(stdout, nullptr, _IOLBF, 0); // line-buffered, same as a normal terminal

    std::thread([pipeFds, realStdout, logFd]() {
        char buf[4096];
        ssize_t n;
        while ((n = read(pipeFds[0], buf, sizeof(buf))) > 0) {
            ssize_t off = 0;
            while (off < n) { ssize_t w = write(realStdout, buf + off, n - off); if (w <= 0) break; off += w; }
            off = 0;
            while (off < n) { ssize_t w = write(logFd,     buf + off, n - off); if (w <= 0) break; off += w; }
        }
    }).detach();

    printf("[INX] full run log: %s\n", logPath.c_str());
    fflush(stdout);
}
#endif

// [NEW — internal collider aim-lock] Creates the shared IPC region and
// injects collider_stub.so into game_pid. Called once automatically
// right after driver attach, and again on demand from the "Inject
// Collider Stub" button on the Settings page — both call sites just
// need g_ColliderIpc to end up non-null (and the stub to mark itself
// alive shortly after) for hook.h to start using it; either failing
// leaves the existing direct in-process HWBP path running, unchanged.
void ix_retry_collider_injection() {
    if (game_pid <= 0) {
        printf("[INX] collider-stub inject: no game PID yet\n");
        fflush(stdout);
        return;
    }

    g_ColliderIpc = ix_collider_ipc_open(/*creator=*/true);
    if (!g_ColliderIpc) {
        printf("[INX] collider IPC region unavailable — using direct in-process HWBP path\n");
        fflush(stdout);
        return;
    }

#ifdef IX_INTERACTIVE_CLI
    std::string stubPath = ix_extract_embedded_collider_stub();
    if (stubPath.empty()) {
        g_ColliderIpc = nullptr;
        return; // extractor already printed exactly why
    }
#else
    std::string stubPath = std::string(INX_MODULE_DIR) + "/libcollider_stub.so";
#endif
    bool injected = ix_inject_shared_library((pid_t)game_pid, stubPath, "ix_collider_stub_init");
#ifdef IX_INTERACTIVE_CLI
    // Only ever needed for the remote dlopen() call just above — once
    // that call has returned (either the library is mapped into the
    // target now, or it failed and nothing references this path),
    // this temp file has no further purpose.
    unlink(stubPath.c_str());
#endif
    printf("[INX] collider-stub injection %s\n",
           injected ? "requested (waiting for stub to come up)"
                    : "failed — using direct in-process HWBP path");
    fflush(stdout);
    if (!injected) {
        g_ColliderIpc = nullptr; // nothing will ever poll this region
        return;
    }

    // [NEW — "same issue, game crashed" after the FP-register fix did
    // NOT stop it] That ruled out register corruption as the cause.
    // The remaining realistic explanation, now that dlopen/dlsym/entry
    // all reported a clean return with no abnormal signal during the
    // call itself, is the game's own anti-cheat reacting to being
    // ptrace-attached / having a foreign library mapped in — which
    // looks identical to "it just crashed" from this side unless we
    // actually check. Since this process is already root (the driver
    // is attached), /data/tombstones/ is readable here — give the game
    // a moment, then check if it's still alive, and if not, dump the
    // newest tombstone so the real signal/backtrace is visible instead
    // of guessing between "our bug" and "anti-cheat kill" again.
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    if (kill((pid_t)game_pid, 0) != 0 && errno == ESRCH) {
        printf("[INX] game process (pid %d) is gone after injection — checking /data/tombstones ...\n", game_pid);
        fflush(stdout);
        DIR* d = opendir("/data/tombstones");
        std::string newestPath;
        time_t newestMtime = 0;
        if (d) {
            struct dirent* ent;
            while ((ent = readdir(d)) != nullptr) {
                if (ent->d_name[0] == '.') continue;
                std::string full = std::string("/data/tombstones/") + ent->d_name;
                struct stat st{};
                if (stat(full.c_str(), &st) == 0 && st.st_mtime > newestMtime) {
                    newestMtime = st.st_mtime;
                    newestPath = full;
                }
            }
            closedir(d);
        } else {
            printf("[INX] could not open /data/tombstones, errno=%d (%s)\n", errno, strerror(errno));
        }
        if (!newestPath.empty()) {
            printf("[INX] --- %s ---\n", newestPath.c_str());
            FILE* tf = fopen(newestPath.c_str(), "r");
            if (tf) {
                char tbuf[512];
                while (fgets(tbuf, sizeof(tbuf), tf)) printf("%s", tbuf);
                fclose(tf);
            } else {
                printf("[INX] could not open %s, errno=%d (%s)\n", newestPath.c_str(), errno, strerror(errno));
            }
            printf("[INX] --- end tombstone ---\n");
        } else {
            printf("[INX] no tombstone found — a bare SIGKILL (e.g. the OS or an anti-cheat "
                   "watchdog killing the process outright) produces no tombstone at all, which "
                   "would look exactly like this\n");
        }
        fflush(stdout);
    }
}

#ifdef IX_INTERACTIVE_CLI
// [STANDALONE BUILD] Terminal driver + bar/vol prompt, run once after
// login succeeds and before LoadMenuOpenMode()/ix_get_driver_choice() are
// called a few lines later in main() — this only has to set the env vars
// those two already read (IX_DRIVER_NAME, IX_MENU_OPEN; see
// ix_driver_init.h), nothing else changes downstream. Names/aliases below
// match ix_driver_init.h's ix_driver_number() exactly, so whatever is
// typed here is guaranteed understood.
//
// Loading the chosen driver's .ko.sh (when it needs one) reproduces
// daemon_service.cpp's load_driver(): chmod 0755 + chown root (a driver
// script is about to run AS root, so it must not stay writable by
// whatever put it on disk), detached fork+exec, half a second to settle.
// Resolved relative to this binary's own location (login.h's
// kd_exe_dir()) since a standalone run has no module directory.
static void ix_interactive_driver_and_menu_prompt() {
    // [NARROWED] Only Paradise and DitPro offered here — every other
    // driver name ix_driver_init.h understands still works if set via
    // IX_DRIVER_NAME directly, this just keeps the interactive menu to
    // the two the user actually wants shown.
    printf("\nStep - Driver\n");
    static const char* names[] = { "paradise", "ditpro" };
    static const char* labels[] = { "Paradise (recommended)", "DitPro KPM" };
    for (int i = 0; i < 2; ++i) printf("  %d) %s\n", i + 1, labels[i]);
    printf("\nSelect [1-2, default=1]: ");
    fflush(stdout);

    char line[32] = {0};
    std::cin.getline(line, sizeof(line));
    int sel = atoi(line);
    if (sel < 1 || sel > 2) sel = 1;
    const std::string driver = names[sel - 1];
    setenv("IX_DRIVER_NAME", driver.c_str(), 1);
    printf("-> Driver: %s\n", driver.c_str());

    if (driver == "paradise") {
        std::string rel;
        FILE* p = popen("uname -r 2>/dev/null", "r");
        if (p) {
            char buf[256];
            if (fgets(buf, sizeof(buf), p)) rel = buf;
            pclose(p);
        }
        while (!rel.empty() && (rel.back() == '\n' || rel.back() == '\r')) rel.pop_back();

        int major = 0, minor = 0;
        if (!rel.empty()) sscanf(rel.c_str(), "%d.%d", &major, &minor);
        const int kv = major * 1000 + minor;

        std::string sub;
        if      (kv >= 5010 && kv <= 6006) sub = "paradise_old";
        else if (kv >= 5010 && kv <= 6018) sub = "paradise_new";

        if (sub.empty()) {
            printf("[WARN] Kernel %s is outside Paradise's supported range "
                   "(5.10-6.18) or could not be read -- skipping driver load.\n",
                   rel.empty() ? "?" : rel.c_str());
        } else {
            char verbuf[32];
            snprintf(verbuf, sizeof(verbuf), "%d.%d", major, minor);
            const std::string path = kd_exe_dir() + "/drivers/" + sub + "/" + verbuf + ".ko.sh";
            if (access(path.c_str(), F_OK) != 0) {
                printf("[WARN] No driver script for kernel %s at:\n  %s\n"
                       "       Continuing without loading a driver.\n",
                       rel.c_str(), path.c_str());
            } else {
                chmod(path.c_str(), 0755);
                chown(path.c_str(), 0, 0);
                printf("Loading driver: %s\n", path.c_str());
                pid_t child = fork();
                if (child == 0) {
                    setsid();
                    int devnull = open("/dev/null", O_RDWR);
                    if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); }
                    execl("/system/bin/sh", "sh", path.c_str(), (char*)nullptr);
                    execl(path.c_str(), path.c_str(), (char*)nullptr);
                    _exit(127);
                } else if (child > 0) {
                    usleep(500 * 1000);
                } else {
                    printf("[WARN] fork() failed for driver load: %s\n", strerror(errno));
                }
            }
        }
    }

    printf("\nStep - Menu\n  1) Bar (on-screen tap bar)\n"
           "  2) Volume (volume-key toggle)\n\nSelect [1-2, default=1]: ");
    fflush(stdout);
    char mline[16] = {0};
    std::cin.getline(mline, sizeof(mline));
    const std::string mode = (mline[0] == '2') ? "vol" : "bar";
    setenv("IX_MENU_OPEN", mode.c_str(), 1);
    printf("-> Menu mode: %s\n", mode.c_str());
}
#endif

// ── Source UI Assets ─────────────────────────────────────────
#include "stb_image.h"
#include "UI/images/Background.h"
#include "ix_audio.h"

// Helper: create OpenGL texture from PNG bytes in memory
// Switch sounds. ImGui exposes a plain function pointer so it keeps no
// dependency on the audio code; it is filled in once, here.
extern void (*GImGuiToggleSound)(bool on);
static void ix_toggle_sound(bool on) { ix_sound_play(on ? IX_SND_ON : IX_SND_OFF); }
static const bool ix_toggle_sound_installed = (GImGuiToggleSound = ix_toggle_sound, true);

static ImTextureID CreateTextureFromMemory(const unsigned char* data, int len) {
    int w=0, h=0, n=0;
    unsigned char* img = stbi_load_from_memory(data, len, &w, &h, &n, STBI_rgb_alpha);
    if (!img || w<=0 || h<=0) return nullptr;
    GLuint tex=0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    stbi_image_free(img);
    return (ImTextureID)(intptr_t)tex;
}

static ImTextureID g_BgTex     = nullptr;
static bool        g_TexLoaded = false;

static void LoadEmbeddedTextures() {
    if (g_TexLoaded) return;
    g_BgTex     = CreateTextureFromMemory(背景图_data,  sizeof(背景图_data));
    g_TexLoaded = true;
}

c_driver *g_driver = nullptr;
using namespace std;
using namespace ImGui;

// ── INX globals ───────────────────────────────────────────────
int selectedBoneIndex = 0;
int aimBotBoneIndex   = 0;
int chestRate         = 1;

struct _SrvData {
    std::string key_masked  = "FREE";      // <-- No login: static dummy
    std::string seller      = "N/A";
    std::string expiry      = "N/A";
    int         days_remain = 0;
    int         days_total  = 0;
    int         dev_used    = 0;
    int         dev_max     = 0;
    bool        valid       = true;        // Always valid
};
static _SrvData g_Srv;
static std::mutex g_SrvMtx;

bool g_ShowFovOverlay = false;
static bool  g_MenuVis       = true;
static bool  g_GameIsRunning = false;
bool         g_MasterEnabled = true;
static chrono::steady_clock::time_point g_LastRenderTime;
static float g_DeltaTime  = 0.016f;
static int   g_FrameCount = 0;
static float g_FPS        = 60.f;
static bool  g_LowPerf    = false;
static float g_MenuAlpha  = 0.f;
static float g_TabAlpha   = 1.f;
static bool  g_TabSwitch  = false;
static int   g_TabNext    = 0;
static int   g_Tab        = 0;
static float g_ScrTarget  = 0.f;
static float g_ScrCur     = 0.f;

struct SavedSettings {
    bool  showLine=false,showBox=false,showHealth=false;
    bool  showDistance=false,showSkeleton=false,showName=false;
    bool  Aimbot=false; int aimbotBone=0; bool aimSilent=false; bool hardwareBT=false;
    bool  brutalBT=false;   // BRUTAL BT — OG silent aim
    bool  aimCollider=false; // Aimbot Collider — OG aim lock
    bool  showFovCircle=false; float aimFovUI=10.f; int chestRateSave=1;
    bool  showAimLine=false; int selectedBone=0;
    float AimFovGlobal=150.f; int textSz=15; bool is3600=false;
    // Body Kill — deal fixed HP damage on aim-silent hit
    bool  bodyKill=false;
    float bodyKillHP=100.f;   // slider 1–200; write this HP to target's health
    // [NEW] "Volume Off" — mutes the menu's own click/toggle sounds
    // (ix_audio.h), not game audio; see ix_sound_set_muted().
    bool  soundMuted=false;
};
static SavedSettings g_Cfg;
static bool g_SettingsLoaded = false;

static bool   g_FloatingSystemReady  = true;
static bool   g_FloatingSystemInit   = false;
static bool   g_IsLandscape          = false;
static bool   g_Landscape2SecDone    = true;
static chrono::steady_clock::time_point g_LandscapeDetectTime;
static bool   g_LandscapeTimerActive = false;

// ── Xmod UI state ─────────────────────────────────────────────
bool 悬浮窗    = true;
static bool 悬浮球   = true;
static bool 窗口状态 = false;
static bool IsBall   = false;
static bool show_ChildMenu1 = true;
static bool show_ChildMenu2 = false;
static bool show_ChildMenu3 = false;
static bool show_ChildMenu4 = false;
static bool show_ChildMenu5 = false;

// TextureInfo — required by particle/bg headers
struct TextureInfo { ImTextureID textureId = nullptr; };

// ── Background image — loaded from embedded PNG bytes ────────────
// Actual texture creation happens in LoadEmbeddedTextures() called from RenderMenu
void 绘制背景图(float r=25.f) {
    if(!g_BgTex) return;
    ImDrawList* dl=ImGui::GetWindowDrawList();
    ImVec2 wp=ImGui::GetWindowPos(),ws=ImGui::GetWindowSize();
    dl->AddImageRounded(g_BgTex, wp,
        ImVec2(wp.x+ws.x,wp.y+ws.y),ImVec2(0,0),ImVec2(1,1),IM_COL32_WHITE,r);
}
void 程序退出清理(){}

// Stub for leaf particle texture loading from runtime files
// (leaf textures are optional — particle system works without them)
static bool 加载本地图片(const char* p, TextureInfo* t) {
    (void)p; (void)t; return false;
}

// ── PARTICLE SYSTEM ─────────────────────────────────────────
/* 
 * by币子
 
 
 */

enum ParticleStyle {
    PARTICLE_SNOW = 0,    
    PARTICLE_COLORFUL,    
    PARTICLE_LEAF         
};

static int currentParticleStyle = PARTICLE_SNOW;
static constexpr int 最大粒子数 = 200;
static struct 雪花粒子 {
    ImVec2 位置;
    ImVec2 速度;
    float 大小;
    ImColor 颜色;
    float 生命周期;
    
} 
粒子数组[最大粒子数];
static bool 粒子系统已初始化 = false;
static float 粒子生成计时器 = 0.0f;
static constexpr int 最大落叶粒子数 = 15; 

static struct 落叶粒子 {
    ImVec2 位置;
    ImVec2 速度;
    float 大小;           
    float 旋转角度;
    float 旋转速度;
    float 生命周期;
    int 纹理类型;         
    bool 已落地;
    float 摆动幅度;       
    float 摆动频率;
}

落叶数组[最大落叶粒子数];
static TextureInfo 落叶纹理[3] = {0}; 
static bool 落叶纹理已加载 = false;
static bool 落叶纹理查找完成 = false;

// Embers in the menu's red family, with a few pale sparks so the drift is
// not a single flat colour. The original rainbow (pure green, blue, cyan,
// yellow) had nothing to do with the theme.
ImColor 随机彩色() {
    switch (rand() % 8) {
        case 0: return ImColor(232,  38,  58, 190);  // #E8263A core red
        case 1: return ImColor(255,  74,  92, 170);  // #FF4A5C red light
        case 2: return ImColor(255, 110,  80, 175);  // #FF6E50 ember
        case 3: return ImColor(255, 160, 130, 150);  // warm spark
        case 4: return ImColor(150,  18,  32, 180);  // #961220 deep
        case 5: return ImColor(228, 228, 232, 130);  // pale spark
        case 6: return ImColor(255, 200, 190, 140);  // faint ember
        case 7: return ImColor(255, 255, 255, 110);  // highlight
        default: return ImColor(232, 38, 58, 180);
    }
}

void 初始化落叶纹理系统() {
    if (落叶纹理查找完成) return;
    const char* 可能路径[] = {
        "/data/币子/飘落.png",
        "/data/币子/飘落.jpg",
        "/data/币子/飘落.jpeg",
    };
    int 加载成功数量 = 0;
    for (int i = 0; i < sizeof(可能路径) / sizeof(可能路径[0]) && 加载成功数量 < 3; i++) {
        TextureInfo tempTexture = {0};
        if (加载本地图片(可能路径[i], &tempTexture)) {
            落叶纹理[加载成功数量] = tempTexture;
            加载成功数量++;
            if (加载成功数量 == 1) {
                for (int j = 1; j < 3; j++) {
                    落叶纹理[j] = tempTexture;
                }
                加载成功数量 = 3;
                break;
            }
        }
    }
    if (加载成功数量 > 0) {
        落叶纹理已加载 = true;
    }
    落叶纹理查找完成 = true;
}
void 初始化粒子系统() {
    for (int i = 0; i < 最大粒子数; ++i) {
        粒子数组[i].位置 = ImVec2(0, 0);
        粒子数组[i].速度 = ImVec2(0, 0);
        粒子数组[i].大小 = 0;
        粒子数组[i].颜色 = 随机彩色();
        粒子数组[i].生命周期 = 0.0f;
    }
    粒子系统已初始化 = true;
}
void 初始化彩色粒子系统() {
    for (int i = 0; i < 最大粒子数; ++i) {
        粒子数组[i].位置 = ImVec2(rand() % (int)abs_ScreenX, rand() % (int)abs_ScreenY);
        粒子数组[i].速度 = ImVec2((rand() % 80 - 50) * 0.05f, (rand() % 30 + 10) * 0.3f);
        粒子数组[i].大小 = (rand() % 5 + 5) * 0.1f;
        粒子数组[i].颜色 = 随机彩色();
        粒子数组[i].生命周期 = (rand() % 100 + 100) * 0.01f;
    }
}
void 初始化落叶粒子系统() {
    if (!落叶纹理查找完成) {
        初始化落叶纹理系统();
    }
    if (!落叶纹理已加载) {
        return;
    }
    for (int i = 0; i < 最大落叶粒子数; ++i) {
        落叶数组[i].位置 = ImVec2(rand() % (int)abs_ScreenX,rand() % (int)(abs_ScreenY * 0.3f));
        落叶数组[i].速度 = ImVec2((rand() % 40 - 20) * 0.1f, (rand() % 30 + 15) * 0.1f);
        落叶数组[i].大小 = (rand() % 20 + 25) * 0.1f; 
        落叶数组[i].旋转角度 = (rand() % 360) * IM_PI / 180.0f;
        落叶数组[i].旋转速度 = (rand() % 15 - 7) * 0.01f; 
        落叶数组[i].生命周期 = (rand() % 300 + 200) * 0.01f; 
        落叶数组[i].纹理类型 = rand() % 3;
        落叶数组[i].已落地 = false;
        落叶数组[i].摆动幅度 = (rand() % 10 + 5) * 0.1f; 
        落叶数组[i].摆动频率 = (rand() % 5 + 3) * 0.1f;  
    }
}
void 切换粒子样式(int newStyle) {
    currentParticleStyle = newStyle;
    if (粒子系统已初始化) {
        switch (currentParticleStyle) {
            case PARTICLE_SNOW:
                初始化粒子系统();
                break;
            case PARTICLE_COLORFUL:
                初始化彩色粒子系统();
                break;
            case PARTICLE_LEAF:
                初始化落叶粒子系统();
                break;
        }
    }
}
void 更新落叶粒子系统(ImVec2 窗口位置, ImVec2 窗口尺寸) {
    if (!落叶纹理已加载) {
        return;
    }
    float 底部边界 = 窗口位置.y + 窗口尺寸.y;
    float 当前时间 = ImGui::GetTime();
    static float 落叶生成计时器 = 0.0f;
    落叶生成计时器 += ImGui::GetIO().DeltaTime;
    if (落叶生成计时器 >= 0.1f) { 
        落叶生成计时器 = 0.0f;
        for (int i = 0; i < 最大落叶粒子数; ++i) {
            if (落叶数组[i].生命周期 <= 0.0f) {
                落叶数组[i].位置 = ImVec2(
                    窗口位置.x + (rand() % (int)窗口尺寸.x),
                    窗口位置.y - 50 
                );
                落叶数组[i].速度 = ImVec2(
                    (rand() % 40 - 20) * 0.1f,
                    (rand() % 30 + 15) * 0.1f
                );
                落叶数组[i].大小 = (rand() % 20 + 25) * 0.1f; 
                落叶数组[i].旋转角度 = 0;
                落叶数组[i].旋转速度 = (rand() % 15 - 7) * 0.01f;
                落叶数组[i].生命周期 = (rand() % 300 + 200) * 0.01f; 
                落叶数组[i].纹理类型 = rand() % 3;
                落叶数组[i].已落地 = false;
                落叶数组[i].摆动幅度 = (rand() % 10 + 5) * 0.1f;
                落叶数组[i].摆动频率 = (rand() % 5 + 3) * 0.1f;
                break;
            }
        }
    }
    for (int i = 0; i < 最大落叶粒子数; ++i) {
        if (落叶数组[i].生命周期 > 0.0f) {
            if (!落叶数组[i].已落地) {
                float 摆动偏移 = sinf(当前时间 * 落叶数组[i].摆动频率 + i) * 落叶数组[i].摆动幅度;
                落叶数组[i].位置.x += (落叶数组[i].速度.x + 摆动偏移) * ImGui::GetIO().DeltaTime * 60.0f;
                落叶数组[i].位置.y += 落叶数组[i].速度.y * ImGui::GetIO().DeltaTime * 60.0f;
                落叶数组[i].旋转角度 += 落叶数组[i].旋转速度;
                if (落叶数组[i].位置.y >= 底部边界) {
                    落叶数组[i].已落地 = true;
                    落叶数组[i].生命周期 = 0.5f; 
                }
                落叶数组[i].生命周期 -= ImGui::GetIO().DeltaTime * 0.3f; 
            } else {
                落叶数组[i].生命周期 -= ImGui::GetIO().DeltaTime * 1.0f;
            }
            if (落叶数组[i].生命周期 <= 0.0f) {
                落叶数组[i].生命周期 = 0.0f;
            }
        }
    }
}
void 绘制落叶粒子系统() {
    if (!落叶纹理已加载) {
        return;
    }
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (int i = 0; i < 最大落叶粒子数; ++i) {
        if (落叶数组[i].生命周期 > 0.0f && 落叶纹理[落叶数组[i].纹理类型].textureId) {
            float 透明度 = 落叶数组[i].生命周期;
            if (透明度 > 1.0f) 透明度 = 1.0f;
            float 纹理大小 = 落叶数组[i].大小 * 25.0f; 
            float 余弦 = cosf(落叶数组[i].旋转角度);
            float 正弦 = sinf(落叶数组[i].旋转角度);
            ImVec2 顶点[4];
            float 半尺寸 = 纹理大小 / 2.0f;
            顶点[0] = ImVec2(-半尺寸, -半尺寸);
            顶点[1] = ImVec2(半尺寸, -半尺寸);
            顶点[2] = ImVec2(半尺寸, 半尺寸);
            顶点[3] = ImVec2(-半尺寸, 半尺寸);
            for (int j = 0; j < 4; j++) {
                float x = 顶点[j].x * 余弦 - 顶点[j].y * 正弦;
                float y = 顶点[j].x * 正弦 + 顶点[j].y * 余弦;
                顶点[j] = ImVec2(落叶数组[i].位置.x + x, 落叶数组[i].位置.y + y);
            }
            int 最终透明度 = (int)(透明度 * 255);
            最终透明度 = ImClamp(最终透明度, 0, 255);
            drawList->AddImageQuad(落叶纹理[落叶数组[i].纹理类型].textureId,顶点[0], 顶点[1], 顶点[2], 顶点[3],ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1),IM_COL32(255, 255, 255, 最终透明度));
        }
    }
}
void 更新粒子系统(ImVec2 窗口位置, ImVec2 窗口尺寸, bool 窗口打开状态) {
    if (!粒子系统已初始化) {
        初始化粒子系统();
    }
    if (!窗口打开状态) {
        return;
    }
    float 当前时间 = ImGui::GetTime();
    粒子生成计时器 += ImGui::GetIO().DeltaTime;
    switch (currentParticleStyle) {
        case PARTICLE_SNOW:
            if (粒子生成计时器 >= 0.01f) {
                粒子生成计时器 = 0.0f;
                for (int i = 0; i < 最大粒子数; ++i) {
                    if (粒子数组[i].生命周期 <= 0.0f) {
                        粒子数组[i].位置 = ImVec2(
                            窗口位置.x + (rand() % (int)窗口尺寸.x),
                            窗口位置.y - 10
                        );
                        粒子数组[i].速度 = ImVec2((rand() % 100 - 50) * 0.1f * 0.7f,(rand() % 50 + 50) * 0.5f * 0.7f);
                        粒子数组[i].大小 = (rand() % 10 + 5) * 0.5f;
                        粒子数组[i].颜色 = 随机彩色();
                        粒子数组[i].生命周期 = (rand() % 100 + 50) * 0.01f;
                        break;
                    }
                }
            }
            break;
        case PARTICLE_COLORFUL:
            if (粒子生成计时器 >= 0.02f) {
                粒子生成计时器 = 0.0f;
                for (int i = 0; i < 最大粒子数; ++i) {
                    if (粒子数组[i].生命周期 <= 0.0f) {
                        粒子数组[i].位置 = ImVec2(窗口位置.x + (rand() % (int)窗口尺寸.x),窗口位置.y + (rand() % (int)窗口尺寸.y));
                        粒子数组[i].速度 = ImVec2((rand() % 90 - 80) * 0.1f,(rand() % 90 - 50) * 0.1f);
                        粒子数组[i].大小 = (rand() % 10 + 8) * 0.4f;
                        粒子数组[i].颜色 = 随机彩色();
                        粒子数组[i].生命周期 = (rand() % 150 + 100) * 0.01f;
                        break;
                    }
                }
            }
            break;
        case PARTICLE_LEAF:
            更新落叶粒子系统(窗口位置, 窗口尺寸);
            break;
    }
    if (currentParticleStyle != PARTICLE_LEAF) {
        for (int i = 0; i < 最大粒子数; ++i) {
            if (粒子数组[i].生命周期 > 0.0f) {
                float speedMultiplier = (currentParticleStyle == PARTICLE_COLORFUL) ? 0.6f : 0.7f;
                粒子数组[i].位置.x += 粒子数组[i].速度.x * ImGui::GetIO().DeltaTime * 60.0f * speedMultiplier;
                粒子数组[i].位置.y += 粒子数组[i].速度.y * ImGui::GetIO().DeltaTime * 60.0f * speedMultiplier;
                粒子数组[i].生命周期 -= ImGui::GetIO().DeltaTime;
                if (粒子数组[i].位置.y > 窗口位置.y + 窗口尺寸.y || 
                    粒子数组[i].位置.x < 窗口位置.x || 
                    粒子数组[i].位置.x > 窗口位置.x + 窗口尺寸.x ||
                    粒子数组[i].生命周期 <= 0.0f) {
                    粒子数组[i].生命周期 = 0.0f;
                }
            }
        }
    }
}
void 绘制粒子系统(bool 窗口打开状态) {    
    if (!窗口打开状态) {
        return;
    }
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    switch (currentParticleStyle) {
        case PARTICLE_SNOW:
            for (int i = 0; i < 最大粒子数; ++i) {
                if (粒子数组[i].生命周期 > 0.0f) {
                    float 透明度 = 粒子数组[i].生命周期;
                    if (透明度 > 1.0f) 透明度 = 1.0f;
                    ImColor 最终颜色 = 粒子数组[i].颜色;
                    最终颜色.Value.w = 透明度 * 0.8f;
                    drawList->AddCircleFilled(粒子数组[i].位置, 粒子数组[i].大小, 最终颜色);
                }
            }
            break;
        case PARTICLE_COLORFUL:
            for (int i = 0; i < 最大粒子数; ++i) {
                if (粒子数组[i].生命周期 > 0.0f) {
                    float 透明度 = 粒子数组[i].生命周期;
                    if (透明度 > 1.0f) 透明度 = 1.0f;
                    ImColor 最终颜色 = 粒子数组[i].颜色;
                    最终颜色.Value.w = 透明度 * 0.9f;
                    drawList->AddCircleFilled(粒子数组[i].位置, 粒子数组[i].大小, 最终颜色);
                }
            }
            break;
        case PARTICLE_LEAF:
            绘制落叶粒子系统();
            break;
    }
}
void 清理落叶纹理资源() {
    for (int i = 0; i < 3; i++) {
        if (落叶纹理[i].textureId) {
            GLuint 纹理ID = (GLuint)(intptr_t)落叶纹理[i].textureId;
            glDeleteTextures(1, &纹理ID);
            落叶纹理[i].textureId = 0;
        }
    }
    落叶纹理已加载 = false;
    落叶纹理查找完成 = false;
    printf("落叶纹理资源已清理\n");
}


// ── XMOD UI WIDGETS ─────────────────────────────────────────
float tap页[150];
float speed = 1.5f;
void Lerp(float& a, const float& b, float t) {
    a += (b - a) * (t * speed);
}
template<typename T>
void Lerp(T& current, const T& target, float factor) {
    current = current + (target - current) * factor;
}

template<typename T>
void Lerp(T& current, const T& from, const T& to, float t) {
    current = from + (to - from) * t;
}


struct wc {
int Tab;
float beg, end;
float 动画, 动画目前, 动画目标, 动画增加速度, 动画减少速度, 动画最小值, 动画展开百分比;
bool 动画开关, 显示内容;
ImColor color;
};

map<string, wc> wh;
extern map<string, ImColor> Styles;
struct TabAnimationState {
    float animationPosition;
    float targetPosition;
    int lastSelectedIndex;
    float buttonWidth;
    float targetButtonWidth;
    bool isAnimating;
    std::vector<float> buttonPositions;
};

void VerticalTabBar(const vector<string>& names, float& selectedIndex, float buttonWidth = 180, float buttonHeight = 85, float highlightRounding = 20.0f, const char* uniqueId = "default") {
    ImGuiStyle& style = ImGui::GetStyle();
    const float extraSpacing = 0.0f;
    float totalHeight = names.size() * buttonHeight + (names.size() - 1) * (style.ItemSpacing.y + extraSpacing);
    ImGui::BeginChild("VerticalTabs", ImVec2(buttonWidth, 730), false, ImGuiWindowFlags_NoBackground);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f,0.9f,0.9f,0.1f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f,0.8f,0.8f,0.2f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    static std::map<std::string, TabAnimationState> animationStates;
    if (animationStates.find(uniqueId) == animationStates.end()) {
        TabAnimationState newState;
        newState.animationPosition = 0;
        newState.targetPosition = 0;
        newState.lastSelectedIndex = -1;
        newState.buttonWidth = buttonWidth;
        newState.targetButtonWidth = buttonWidth;
        newState.isAnimating = false;
        animationStates[uniqueId] = newState;
    }
    TabAnimationState& state = animationStates[uniqueId];
    if (fabs(state.buttonWidth - state.targetButtonWidth) > 0.1f) {
        Lerp(state.buttonWidth, state.targetButtonWidth, 0.15f);
        state.isAnimating = true;
    } else {
        state.buttonWidth = state.targetButtonWidth;
        state.isAnimating = false;
    }
    if (fabs(state.targetButtonWidth - buttonWidth) > 0.1f) {
        state.targetButtonWidth = buttonWidth;
    }
    float actualButtonWidth = state.buttonWidth;
    vector<float> buttonPositions(names.size());
    if (state.lastSelectedIndex == -1) {
        state.animationPosition = 0;
        state.targetPosition = 0;
    }
    Lerp(state.animationPosition, state.targetPosition, 0.08f);
    float dynamicSliderWidth = actualButtonWidth;
    if (actualButtonWidth < 150.0f) {
        dynamicSliderWidth = actualButtonWidth * 0.9f; 
    } else if (actualButtonWidth > 200.0f) {
        dynamicSliderWidth = actualButtonWidth * 0.95f; 
    }
    float sliderOffsetX = (actualButtonWidth - dynamicSliderWidth) * 0.5f;
    ImVec2 rectMin = ImGui::GetWindowPos();
    rectMin.x += sliderOffsetX; 
    rectMin.y += state.animationPosition;
    ImVec2 rectMax = ImVec2(rectMin.x + dynamicSliderWidth, rectMin.y + buttonHeight);
    // ImGui::GetWindowDrawList()->AddRectFilled(rectMin, rectMax, ImColor(255, 0, 0, 200), highlightRounding);
    ImGui::GetWindowDrawList()->AddRectFilled(rectMin, rectMax, ImColor(75, 190, 255, 255), highlightRounding);
    for (int i = 0; i < names.size(); i++) {
        buttonPositions[i] = ImGui::GetCursorPosY();
        ImVec2 buttonSize(actualButtonWidth, buttonHeight);
        if (ImGui::InvisibleButton(names[i].c_str(), buttonSize)) {
            if ((int)selectedIndex != i) {
                state.targetPosition = buttonPositions[i];
                state.lastSelectedIndex = (int)selectedIndex;
                selectedIndex = (float)i;
            }
        }
        ImVec2 buttonMin = ImGui::GetItemRectMin();
        ImVec2 buttonMax = ImGui::GetItemRectMax();
        ImGui::SetWindowFontScale(1.02f);
        const float textPadding = 8.0f;
        ImU32 textColor;
        if (i == selectedIndex) {
            textColor = IM_COL32(0, 0, 0, 255); 
         }
        ImVec2 textSize = ImGui::CalcTextSize(names[i].c_str());
        ImVec2 textPos(buttonMin.x + (actualButtonWidth - textSize.x) * 0.5f,buttonMin.y + (buttonHeight - textSize.y) * 0.5f);
        ImGui::GetWindowDrawList()->AddText(textPos, textColor, names[i].c_str());
        if (i < names.size() - 1) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + style.ItemSpacing.y);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + extraSpacing);
        }
    }
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::EndChild();
}

bool M_CustomSliderFloatvv(const char* label, float* v, float v_min, float v_max, const char* format = "%.1f", int slider_type = 0) {
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    ImGuiID id = window->GetID(label);
    ImVec2 pos = window->DC.CursorPos;
    ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    float total_height = 75.0f;
    float total_width = -1; 
    float horizontal_margin = 20.0f; 
    float vertical_margin = 15.0f;   
    float slider_height = 12.0f;
    float grab_radius = 15.0f;
    float available_width = ImGui::GetContentRegionAvail().x;
    float slider_width = available_width - horizontal_margin * 2;
    ImRect total_bb(pos, ImVec2(pos.x + available_width, pos.y + total_height));
    float slider_vertical_offset = 6.5f; 
    ImVec2 slider_pos = ImVec2(
        pos.x + horizontal_margin,
        pos.y + total_height - vertical_margin - slider_height + slider_vertical_offset
    );
    ImRect slider_bb(slider_pos, ImVec2(slider_pos.x + slider_width, slider_pos.y + slider_height));
    ImRect touch_bb = slider_bb;
    touch_bb.Expand(ImVec2(0, 10.0f));
    ImGui::ItemSize(total_bb, g.Style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id, &touch_bb))
        return false;
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(touch_bb, id, &hovered, &held);
    if (held) {
        float normalized_pos = ImClamp((g.IO.MousePos.x - slider_bb.Min.x) / slider_bb.GetWidth(), 0.0f, 1.0f);
        *v = ImLerp(v_min, v_max, normalized_pos);
    }
    float normalized_value = ImClamp((*v - v_min) / (v_max - v_min), 0.0f, 1.0f);
    float grab_pos = ImLerp(slider_bb.Min.x + grab_radius, slider_bb.Max.x - grab_radius, normalized_value);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImU32 background_color = IM_COL32(245, 245, 245, 200);
    float corner_radius = 12.0f;
    draw_list->AddRectFilled(total_bb.Min, total_bb.Max, background_color, corner_radius);
    ImU32 track_bg_color = IM_COL32(180, 180, 180, 255);
    ImU32 track_fill_color = IM_COL32(75, 190, 255, 200); // 修改后的滑块填充色
ImU32 grab_border_color = IM_COL32(75, 190, 255, 200); // 修改后的抓取点边框色
    ImU32 grab_color;
    if (held) {
        grab_color = IM_COL32(255, 0, 0, 200); 
    } else {
        grab_color = hovered ? IM_COL32(220, 220, 220, 255) : IM_COL32(240, 240, 240, 255);
    }
    
    if (held) {
        grab_color = IM_COL32(255, 0, 0, 200); 
    } else {
        grab_color = hovered ? IM_COL32(220, 220, 220, 255) : IM_COL32(240, 240, 240, 255);
    }
    float track_rounding = slider_height * 0.5f;
    draw_list->AddRectFilled(slider_bb.Min, slider_bb.Max, track_bg_color, track_rounding);
    if (normalized_value > 0.0f) {
        ImRect filled_bb(slider_bb.Min, ImVec2(grab_pos, slider_bb.Max.y));
        draw_list->AddRectFilled(filled_bb.Min, filled_bb.Max, track_fill_color, track_rounding);
    }
    ImVec2 grab_center = ImVec2(grab_pos, (slider_bb.Min.y + slider_bb.Max.y) * 0.5f);
    draw_list->AddCircleFilled(grab_center, grab_radius, grab_border_color);
    float inner_radius = grab_radius * 0.6f;
    draw_list->AddCircleFilled(grab_center, inner_radius, IM_COL32(255, 255, 255, 255));
    if (label_size.x > 0.0f) {
        ImVec2 label_pos = ImVec2(pos.x + horizontal_margin,pos.y + 5.0f  );
        ImU32 text_color = IM_COL32(0, 0, 0, 255);
        draw_list->AddText(label_pos, text_color, label);
    }
    // char value_buf[64];
    // ImFormatString(value_buf, IM_ARRAYSIZE(value_buf), format, *v);
    // ImVec2 value_size = ImGui::CalcTextSize(value_buf);
    // ImVec2 value_pos = ImVec2(slider_bb.Max.x - value_size.x,pos.y + 5.0f);
    // ImU32 value_color = IM_COL32(0, 0, 0, 255);
    // draw_list->AddText(value_pos, value_color, value_buf);
    char value_buf[64];
    ImFormatString(value_buf, IM_ARRAYSIZE(value_buf), format, *v);
    ImVec2 value_size = ImGui::CalcTextSize(value_buf);
    
    // 添加圆角矩形背景
    float bg_padding_x = 10.0f; // 水平内边距
    float bg_padding_y = 4.0f;  // 垂直内边距
    float bg_rounding = 50.0f;  // 圆角半径
    ImVec2 bg_min = ImVec2(slider_bb.Max.x - value_size.x - bg_padding_x, pos.y + 5.0f - bg_padding_y);
    ImVec2 bg_max = ImVec2(slider_bb.Max.x + bg_padding_x, pos.y + 5.0f + value_size.y + bg_padding_y);
    // ImU32 bg_color = IM_COL32(0, 0, 0, 128); // 淡透明黑色，RGBA(0,0,0,128)
    ImU32 bg_color = IM_COL32(0, 0, 0, 8); // 淡透明黑色，RGBA(0,0,0,128)
    draw_list->AddRectFilled(bg_min, bg_max, bg_color, bg_rounding);
    
    // 调整文本位置至背景中心
    ImVec2 value_pos = ImVec2(
        bg_min.x + (bg_max.x - bg_min.x - value_size.x) * 0.5f,
        bg_min.y + (bg_max.y - bg_min.y - value_size.y) * 0.5f
    );
    ImU32 value_color = IM_COL32(255, 255, 255, 255); // 将文本颜色改为白色以提高在黑色背景上的可读性
    draw_list->AddText(value_pos, value_color, value_buf);
    return pressed;
}
string getBeijingTimeString() {
std::time_t now = std::time(nullptr);
std::tm* now_tm = std::gmtime(&now); 
now_tm->tm_hour += 8;
now_tm->tm_isdst = 0; 
if (now_tm->tm_hour >= 24) {
now_tm->tm_hour -= 24;
now_tm->tm_mday += 1;
}
char buffer[20];
std::strftime(buffer, sizeof(buffer), "%Y%m%d%H%M%S", now_tm); 
return std::string(buffer);
}
bool M_CircularButton(const char* label, const ImVec2& size_arg = ImVec2(0 , 0), ImGuiButtonFlags flags = 0) {
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    ImGuiID id = window->GetID(label);
    ImVec2 pos = window->DC.CursorPos;
    float window_available_width = ImGui::GetContentRegionAvail().x;
    float horizontal_margin = 20.0f;
    float total_width = window_available_width - 2 * horizontal_margin;
    total_width = ImMax(total_width, 300.0f);
    ImVec2 size = ImGui::CalcItemSize(size_arg, total_width, 61.0f);
    float total_height = size.y;
    float vertical_padding = 15.0f;
    ImRect total_bb(pos, ImVec2(pos.x + total_width, pos.y + total_height));
    ImGui::ItemSize(total_bb, vertical_padding);
    if (!ImGui::ItemAdd(total_bb, id))
        return false;
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held, flags);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    float shadow_offset_x = 2.0f;
    float shadow_offset_y = 2.0f;
    float shadow_spread = 1.0f;
    float corner_radius = 12.0f;
    ImVec2 shadow_min = ImVec2(total_bb.Min.x + shadow_offset_x, total_bb.Min.y + shadow_offset_y);
    ImVec2 shadow_max = ImVec2(total_bb.Max.x + shadow_offset_x, total_bb.Max.y + shadow_offset_y);
    shadow_min.x -= shadow_spread;
    shadow_min.y -= shadow_spread;
    shadow_max.x += shadow_spread;
    shadow_max.y += shadow_spread;
    draw_list->AddRectFilled(shadow_min, shadow_max, IM_COL32(0, 0, 0, 20), corner_radius);
    ImVec2 shadow2_min = ImVec2(shadow_min.x + 0.5f, shadow_min.y + 0.5f);
    ImVec2 shadow2_max = ImVec2(shadow_max.x + 0.5f, shadow_max.y + 0.5f);
    draw_list->AddRectFilled(shadow2_min, shadow2_max, IM_COL32(0, 0, 0, 15), corner_radius);
    ImVec2 shadow3_min = ImVec2(shadow2_min.x + 0.5f, shadow2_min.y + 0.5f);
    ImVec2 shadow3_max = ImVec2(shadow_max.x + 0.5f, shadow_max.y + 0.5f);
    draw_list->AddRectFilled(shadow3_min, shadow3_max, IM_COL32(0, 0, 0, 8), corner_radius);
    ImU32 background_color = IM_COL32(255, 255, 255, 255);
    draw_list->AddRectFilled(total_bb.Min, total_bb.Max, background_color, corner_radius);
    ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    ImVec2 label_pos = ImVec2(
        pos.x + horizontal_margin + (total_width - 2 * horizontal_margin - label_size.x) * 0.5f,
        pos.y + (total_height - label_size.y) * 0.5f
    );
    ImU32 text_color = pressed ? IM_COL32(255, 255, 255, 255) : 
                     hovered ? IM_COL32(0, 120, 215, 255) : 
                     IM_COL32(0, 0, 0, 255);
    if (pressed) {
        ImU32 pressed_color = IM_COL32(0, 120, 215, 255);
        draw_list->AddRectFilled(total_bb.Min, total_bb.Max, pressed_color, corner_radius);
    } else if (hovered) {
        draw_list->AddRect(total_bb.Min, total_bb.Max, IM_COL32(0, 120, 215, 100), corner_radius, 0, 1.5f);
    }
    draw_list->AddText(label_pos, text_color, label);
    if (hovered && !held) {
        draw_list->AddRect(total_bb.Min, total_bb.Max, IM_COL32(240, 240, 240, 255), corner_radius, 0, 1.5f);
    }
    return pressed;
}
void fanhua(const vector<string>& names, float& 参数, float 居中 = 0, float 按钮x = 130, float 按钮y = 75, float 水平间距 = 10) {
    ImVec2 按钮 = ImVec2(按钮x, 按钮y);
    ImVec2 差距 = ImVec2(水平间距, 0);
    float 总宽度 = 按钮.x * names.size() + 水平间距 * (names.size() - 1);
    ImVec2 窗口(总宽度, 按钮.y);
    wc& name = wh[names[0].c_str()];
    if (name.动画开关 == false) {
        name.动画开关 = true;
        name.动画目前 = 0;
        name.动画目标 = 0;
        name.beg = 0;
        name.end = 0;
        name.Tab = 0;
        参数 = 0;
    }
    if (居中) {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetWindowPos().x + (ImGui::GetWindowSize().x - 总宽度)/2 + 110.0f, ImGui::GetWindowPos().y + ImGui::GetCursorPos().y)); 
    } else {
        ImVec2 currentPos = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(currentPos.x + 110.0f, currentPos.y));
    }
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.85f, 0.85f, 0.85f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.9f, 0.92f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.85f, 0.87f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 9999.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::BeginChild(names[0].c_str(), 窗口, true);
    ImVec2 childMin = ImGui::GetWindowPos();
    ImVec2 childMax = ImVec2(childMin.x + 总宽度, childMin.y + 窗口.y);
    ImGui::GetWindowDrawList()->AddRectFilled(childMin, childMax, IM_COL32(75, 190, 255, 255), 9999.0f);
    ImGui::GetWindowDrawList()->AddRect(childMin, childMax, IM_COL32(75, 190, 255, 255), 999.0f, 0, 1.0f);
    vector<ImVec2> buttonMins(names.size());
    vector<ImVec2> buttonMaxs(names.size());
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
    for (int i = 0; i < names.size(); i++) {
        if (i > 0) {
            ImGui::SameLine(0, 水平间距);
        }
        if (ImGui::Button(names[i].c_str(), ImVec2(按钮.x, 按钮.y))) {
            if (name.Tab != i) {
                name.Tab = i;
                参数 = i;
                name.end = i * (按钮.x + 水平间距);
            }
        }
        buttonMins[i] = ImGui::GetItemRectMin();
        buttonMaxs[i] = ImGui::GetItemRectMax();
    }
    ImGui::PopStyleColor();
    float 动画速度 = 0.15f;
    name.beg = ImLerp(name.beg, name.end, 动画速度);
    if (fabs(name.beg - name.end) < 0.5f) {
        name.beg = name.end;
    }
    if (name.Tab >= 0 && name.Tab < names.size()) {
        float indicatorX = childMin.x + name.beg;
        float indicatorY = childMin.y;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(indicatorX, indicatorY),ImVec2(indicatorX + 按钮.x, indicatorY + 按钮.y),ImColor(255, 0, 0, 255),9999.0f);
    }
    for (int i = 0; i < names.size(); i++) {
        string bynh = names[i];
        ImVec2 min = buttonMins[i];
        ImVec2 max = buttonMaxs[i];
        float px = min.x + (max.x - min.x) / 2;
        float py = min.y + (max.y - min.y) / 2;
        auto textSize_bynh = ImGui::GetFont()->CalcTextSizeA(32, FLT_MAX, -1, bynh.c_str(), NULL, NULL);
        ImVec2 textPos = ImVec2(px - (textSize_bynh.x / 2), py - (textSize_bynh.y / 2));
        ImU32 textColor;
        if (name.Tab == i) {
            textColor = IM_COL32(255, 255, 255, 255); 
        } else {
            textColor = IM_COL32(128, 128, 128, 255); 
        }
        ImGui::GetWindowDrawList()->AddText(NULL, 32, textPos, textColor, bynh.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(4);
}

bool M_CircularCheckbox(const char* label, bool* v) {
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    ImGuiID id = window->GetID(label);
    ImVec2 pos = window->DC.CursorPos;
    ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    float total_width = 315.0f;  
    float total_height = 84.0f;  
    float switch_width = 85.0f;      
    float switch_height = 42.0f;     
    float switch_knob_radius = 15.0f; 
    float horizontal_margin = 20.0f;
    float vertical_padding = 15.0f;
    float spacing = 15.0f;
    ImRect total_bb(pos, ImVec2(pos.x + total_width, pos.y + total_height));
    ImGui::ItemSize(total_bb, vertical_padding);
    if (!ImGui::ItemAdd(total_bb, id))
        return false;
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);
    if (pressed) {
        *v = !(*v);
        ImGui::MarkItemEdited(id);
    }
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImU32 background_color = IM_COL32(245, 245, 245, 200);
    float corner_radius = 12.0f;
    draw_list->AddRectFilled(total_bb.Min, total_bb.Max, background_color, corner_radius);
    ImU32 border_color = IM_COL32(200, 200, 200, 255);
    draw_list->AddRect(total_bb.Min, total_bb.Max, border_color, corner_radius, 0, 1.5f);
    if (hovered) {
        draw_list->AddRect(total_bb.Min, total_bb.Max, IM_COL32(150, 150, 150, 255), corner_radius, 0, 1.5f);
    }
    ImVec2 switch_pos = ImVec2(pos.x + total_width - horizontal_margin - switch_width,pos.y + (total_height - switch_height) * 0.5f);
    ImRect switch_bb(switch_pos, ImVec2(switch_pos.x + switch_width, switch_pos.y + switch_height));
    ImVec2 label_pos = ImVec2(pos.x + horizontal_margin,pos.y + (total_height - label_size.y) * 0.5f);
    ImU32 track_color, knob_color;
    if (*v) {
        track_color = IM_COL32(135, 190, 235, 255); // 修改后的激活轨迹颜色
        knob_color = IM_COL32(75, 190, 255, 255);  // 修改后的激活旋钮颜色
    } else {
        track_color = IM_COL32(180, 180, 180, 255);
        knob_color = IM_COL32(220, 220, 220, 255);
    }
    float track_rounding = switch_height * 0.5f;
    draw_list->AddRectFilled(switch_bb.Min, switch_bb.Max, track_color, track_rounding);
    float knob_x;
    if (*v) {
        knob_x = switch_bb.Max.x - switch_knob_radius - 6.0f; 
    } else {
        knob_x = switch_bb.Min.x + switch_knob_radius + 6.0f; 
    }
    ImVec2 knob_center = ImVec2(knob_x, (switch_bb.Min.y + switch_bb.Max.y) * 0.5f);
    draw_list->AddCircleFilled(knob_center, switch_knob_radius, knob_color);
    ImU32 text_color = IM_COL32(0, 0, 0, 255);
    draw_list->AddText(label_pos, text_color, label);
    return pressed;
}
bool M_CircularCheckbox3(const char* label, bool* v) {
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    ImGuiID id = window->GetID(label);
    ImVec2 pos = window->DC.CursorPos;
    ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    float total_width = 210.0f;  
    float total_height = 84.0f;  
    float switch_width = 85.0f;      
    float switch_height = 42.0f;     
    float switch_knob_radius = 15.0f; 
    float horizontal_margin = 20.0f;
    float vertical_padding = 15.0f;
    float spacing = 15.0f;
    ImRect total_bb(pos, ImVec2(pos.x + total_width, pos.y + total_height));
    ImGui::ItemSize(total_bb, vertical_padding);
    if (!ImGui::ItemAdd(total_bb, id))
        return false;
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);
    if (pressed) {
        *v = !(*v);
        ImGui::MarkItemEdited(id);
    }
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImU32 background_color = IM_COL32(245, 245, 245, 200);
    float corner_radius = 12.0f;
    draw_list->AddRectFilled(total_bb.Min, total_bb.Max, background_color, corner_radius);
    ImU32 border_color = IM_COL32(200, 200, 200, 255);
    draw_list->AddRect(total_bb.Min, total_bb.Max, border_color, corner_radius, 0, 1.5f);
    if (hovered) {
        draw_list->AddRect(total_bb.Min, total_bb.Max, IM_COL32(150, 150, 150, 255), corner_radius, 0, 1.5f);
    }
    ImVec2 switch_pos = ImVec2(pos.x + total_width - horizontal_margin - switch_width,pos.y + (total_height - switch_height) * 0.5f);
    ImRect switch_bb(switch_pos, ImVec2(switch_pos.x + switch_width, switch_pos.y + switch_height));
    ImVec2 label_pos = ImVec2(pos.x + horizontal_margin,pos.y + (total_height - label_size.y) * 0.5f);
    ImU32 track_color, knob_color;
    if (*v) {
        track_color = IM_COL32(135, 190, 235, 255); // 修改后的激活轨迹颜色
        knob_color = IM_COL32(75, 190, 255, 255);  // 修改后的激活旋钮颜色
    } else {
        track_color = IM_COL32(180, 180, 180, 255);
        knob_color = IM_COL32(220, 220, 220, 255);
    }
    float track_rounding = switch_height * 0.5f;
    draw_list->AddRectFilled(switch_bb.Min, switch_bb.Max, track_color, track_rounding);
    float knob_x;
    if (*v) {
        knob_x = switch_bb.Max.x - switch_knob_radius - 6.0f; 
    } else {
        knob_x = switch_bb.Min.x + switch_knob_radius + 6.0f; 
    }
    ImVec2 knob_center = ImVec2(knob_x, (switch_bb.Min.y + switch_bb.Max.y) * 0.5f);
    draw_list->AddCircleFilled(knob_center, switch_knob_radius, knob_color);
    ImU32 text_color = IM_COL32(0, 0, 0, 255);
    draw_list->AddText(label_pos, text_color, label);
    return pressed;
}
bool M_CircularCheckbox1(const char* label, bool* v) {
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    ImGuiID id = window->GetID(label);
    ImVec2 pos = window->DC.CursorPos;
    ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    // float total_width = 210.0f;  
    // 替换：float total_width = 315.0f;
float total_width = ImGui::GetContentRegionAvail().x; // 直接使用全部可用宽度
    float total_height = 84.0f;  
    float switch_width = 85.0f;      
    float switch_height = 42.0f;     
    float switch_knob_radius = 15.0f; 
    float horizontal_margin = 20.0f;
    float vertical_padding = 15.0f;
    float spacing = 15.0f;
    ImRect total_bb(pos, ImVec2(pos.x + total_width, pos.y + total_height));
    ImGui::ItemSize(total_bb, vertical_padding);
    if (!ImGui::ItemAdd(total_bb, id))
        return false;
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);
    if (pressed) {
        *v = !(*v);
        ImGui::MarkItemEdited(id);
    }
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImU32 background_color = IM_COL32(245, 245, 245, 200);
    float corner_radius = 12.0f;
    draw_list->AddRectFilled(total_bb.Min, total_bb.Max, background_color, corner_radius);
    ImU32 border_color = IM_COL32(200, 200, 200, 255);
    draw_list->AddRect(total_bb.Min, total_bb.Max, border_color, corner_radius, 0, 1.5f);
    if (hovered) {
        draw_list->AddRect(total_bb.Min, total_bb.Max, IM_COL32(150, 150, 150, 255), corner_radius, 0, 1.5f);
    }
    ImVec2 switch_pos = ImVec2(pos.x + total_width - horizontal_margin - switch_width,pos.y + (total_height - switch_height) * 0.5f);
    ImRect switch_bb(switch_pos, ImVec2(switch_pos.x + switch_width, switch_pos.y + switch_height));
    ImVec2 label_pos = ImVec2(pos.x + horizontal_margin,pos.y + (total_height - label_size.y) * 0.5f);
    ImU32 track_color, knob_color;
    if (*v) {
        track_color = IM_COL32(135, 190, 235, 255); // 修改后的激活轨迹颜色
        knob_color = IM_COL32(75, 190, 255, 255);  // 修改后的激活旋钮颜色
    } else {
        track_color = IM_COL32(180, 180, 180, 255);
        knob_color = IM_COL32(220, 220, 220, 255);
    }
    float track_rounding = switch_height * 0.5f;
    draw_list->AddRectFilled(switch_bb.Min, switch_bb.Max, track_color, track_rounding);
    float knob_x;
    if (*v) {
        knob_x = switch_bb.Max.x - switch_knob_radius - 6.0f; 
    } else {
        knob_x = switch_bb.Min.x + switch_knob_radius + 6.0f; 
    }
    ImVec2 knob_center = ImVec2(knob_x, (switch_bb.Min.y + switch_bb.Max.y) * 0.5f);
    draw_list->AddCircleFilled(knob_center, switch_knob_radius, knob_color);
    ImU32 text_color = IM_COL32(0, 0, 0, 255);
    draw_list->AddText(label_pos, text_color, label);
    return pressed;
}
float ImLerp(float a, float b, float t) { 
    return a + (b - a) * t; 
}
void CenteredText(const char* text, float customScale = 1.0f) {
    ImGui::SetWindowFontScale(customScale);
    ImVec2 textSize = ImGui::CalcTextSize(text);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - textSize.x) * 0.5f);
    ImGui::Text("%s", text);
    ImGui::SetWindowFontScale(1.0f);
}
// 预设颜色结构
struct 预设颜色 {
    const char* 名称;
    ImVec4 颜色值;
    ImU32 颜色值32;
};

// 预设颜色数组
static std::vector<预设颜色> 预设调色球 = {
    {"紫罗兰", ImVec4(0.54f, 0.17f, 0.89f, 1.0f), IM_COL32(138, 43, 226, 255)},
    {"克莱因蓝", ImVec4(0.0f, 0.18f, 0.65f, 1.0f), IM_COL32(0, 47, 167, 255)},
    {"爱马仕橙", ImVec4(0.96f, 0.33f, 0.09f, 1.0f), IM_COL32(245, 85, 23, 255)},
    {"蒂芙尼蓝", ImVec4(0.51f, 0.85f, 0.82f, 1.0f), IM_COL32(129, 216, 208, 255)},
    {"马尔斯绿", ImVec4(0.0f, 0.27f, 0.23f, 1.0f), IM_COL32(0, 69, 59, 255)},
    {"勃艮第红", ImVec4(0.51f, 0.0f, 0.13f, 1.0f), IM_COL32(130, 0, 33, 255)},
    {"普鲁士蓝", ImVec4(0.0f, 0.19f, 0.33f, 1.0f), IM_COL32(0, 49, 83, 255)},
    {"提香红", ImVec4(0.75f, 0.22f, 0.17f, 1.0f), IM_COL32(191, 56, 44, 255)},
    {"凡戴克棕", ImVec4(0.37f, 0.26f, 0.13f, 1.0f), IM_COL32(94, 66, 33, 255)},
    {"申布伦黄", ImVec4(0.93f, 0.78f, 0.0f, 1.0f), IM_COL32(237, 199, 0, 255)},
    {"松绿", ImVec4(0.18f, 0.36f, 0.34f, 1.0f), IM_COL32(46, 92, 87, 255)},
    {"玉色", ImVec4(0.62f, 0.85f, 0.71f, 1.0f), IM_COL32(158, 217, 181, 255)},
    {"海红", ImVec4(0.91f, 0.29f, 0.42f, 1.0f), IM_COL32(232, 74, 107, 255)},
    {"箬竹", ImVec4(0.47f, 0.56f, 0.42f, 1.0f), IM_COL32(120, 143, 107, 255)},
    {"柑宇", ImVec4(0.98f, 0.55f, 0.19f, 1.0f), IM_COL32(250, 140, 49, 255)},
    {"烟墨色", ImVec4(0.16f, 0.14f, 0.13f, 1.0f), IM_COL32(41, 36, 33, 255)},
    {"青雀头黛色", ImVec4(0.27f, 0.42f, 0.51f, 1.0f), IM_COL32(69, 107, 130, 255)},
    {"浅松绿", ImVec4(0.67f, 0.83f, 0.74f, 1.0f), IM_COL32(171, 212, 189, 255)},
    {"东方既白", ImVec4(0.85f, 0.92f, 0.96f, 1.0f), IM_COL32(217, 235, 245, 255)},
    {"浅云", ImVec4(0.93f, 0.94f, 0.95f, 1.0f), IM_COL32(237, 240, 242, 255)},
    {"栀子", ImVec4(0.98f, 0.91f, 0.71f, 1.0f), IM_COL32(250, 232, 181, 255)},
    {"金黄", ImVec4(1.0f, 0.84f, 0.0f, 1.0f), IM_COL32(255, 215, 0, 255)},
    {"桔梗", ImVec4(0.6f, 0.4f, 0.8f, 1.0f), IM_COL32(153, 102, 204, 255)},
    {"桃天", ImVec4(0.98f, 0.75f, 0.85f, 1.0f), IM_COL32(250, 191, 217, 255)},
    {"鹤顶红", ImVec4(0.9f, 0.2f, 0.2f, 1.0f), IM_COL32(230, 51, 51, 255)},
    {"乌黑", ImVec4(0.2f, 0.2f, 0.2f, 1.0f), IM_COL32(51, 51, 51, 255)},
    {"茶白", ImVec4(0.96f, 0.96f, 0.92f, 1.0f), IM_COL32(245, 245, 235, 255)},
    {"番茄红", ImVec4(1.0f, 0.39f, 0.28f, 1.0f), IM_COL32(255, 99, 71, 255)},
    {"藕白", ImVec4(0.98f, 0.92f, 0.84f, 1.0f), IM_COL32(250, 235, 215, 255)},
    {"烟雾蓝", ImVec4(0.55f, 0.78f, 0.94f, 1.0f), IM_COL32(140, 200, 240, 255)},
    {"奶酪黄", ImVec4(1.0f, 0.94f, 0.71f, 1.0f), IM_COL32(255, 240, 181, 255)},
    {"棕红", ImVec4(0.65f, 0.16f, 0.16f, 1.0f), IM_COL32(166, 41, 41, 255)},
    {"孔雀蓝", ImVec4(0.0f, 0.6f, 0.73f, 1.0f), IM_COL32(0, 153, 186, 255)},
    {"靛青", ImVec4(0.0f, 0.5f, 0.5f, 1.0f), IM_COL32(0, 128, 128, 255)},
    {"卡其", ImVec4(0.76f, 0.69f, 0.57f, 1.0f), IM_COL32(195, 176, 145, 255)},
    {"梅子青", ImVec4(0.47f, 0.78f, 0.65f, 1.0f), IM_COL32(120, 200, 166, 255)},
    {"透黄", ImVec4(1.0f, 0.94f, 0.35f, 1.0f), IM_COL32(255, 240, 89, 255)},
    {"珊瑚", ImVec4(1.0f, 0.5f, 0.31f, 1.0f), IM_COL32(255, 127, 80, 255)},
    {"纯白", ImVec4(1.0f, 1.0f, 1.0f, 1.0f), IM_COL32(255, 255, 255, 255)},
    {"纯黑", ImVec4(0.0f, 0.0f, 0.0f, 1.0f), IM_COL32(0, 0, 0, 255)}
};

static int 当前预设颜色索引 = 0;
static bool 显示预设调色板 = false;
bool 币子の调色器(const char* label, float color[4], int 颜色配置类型, bool 显示预设) {
    bool 已修改 = false;
    ImGui::PushID(label);
    static std::map<std::string, bool> 各标签显示状态;
    static std::map<std::string, int> 各标签选中索引;
    static std::map<std::string, float> 各标签动画进度;
    static std::map<std::string, float> 各标签目标高度;
    static std::map<std::string, float> 各标签当前高度;
    static std::map<std::string, float> 各标签滑入动画; 
    if (各标签显示状态.find(label) == 各标签显示状态.end()) {
        各标签显示状态[label] = false;
    }
    if (各标签选中索引.find(label) == 各标签选中索引.end()) {
        各标签选中索引[label] = -1;
    }
    if (各标签动画进度.find(label) == 各标签动画进度.end()) {
        各标签动画进度[label] = 0.0f;
    }
    if (各标签目标高度.find(label) == 各标签目标高度.end()) {
        各标签目标高度[label] = 0.0f;
    }
    if (各标签当前高度.find(label) == 各标签当前高度.end()) {
        各标签当前高度[label] = 0.0f;
    }
    if (各标签滑入动画.find(label) == 各标签滑入动画.end()) {
        各标签滑入动画[label] = 0.0f; 
    }
    bool& 当前标签显示状态 = 各标签显示状态[label];
    int& 当前标签选中索引 = 各标签选中索引[label];
    float& 当前标签动画进度 = 各标签动画进度[label];
    float& 当前标签目标高度 = 各标签目标高度[label];
    float& 当前标签当前高度 = 各标签当前高度[label];
    float& 当前标签滑入进度 = 各标签滑入动画[label]; 
    float 目标进度 = 当前标签显示状态 ? 1.0f : 0.0f;
    float 动画速度 = 8.0f;
    if (fabs(当前标签动画进度 - 目标进度) > 0.001f) {
        Lerp(当前标签动画进度, 目标进度, ImGui::GetIO().DeltaTime * 动画速度);
    } else {
        当前标签动画进度 = 目标进度;
    }
    float 目标滑入进度 = 当前标签显示状态 ? 1.0f : 0.0f;
    if (fabs(当前标签滑入进度 - 目标滑入进度) > 0.001f) {
        Lerp(当前标签滑入进度, 目标滑入进度, ImGui::GetIO().DeltaTime * (动画速度 * 0.8f));
    } else {
        当前标签滑入进度 = 目标滑入进度;
    }
    float 动画系数 = 0.0f;
    if (当前标签动画进度 <= 0.5f) {
        动画系数 = 2.0f * 当前标签动画进度 * 当前标签动画进度;
    } else {
        动画系数 = 1.0f - 2.0f * (1.0f - 当前标签动画进度) * (1.0f - 当前标签动画进度);
    }
    float 滑入系数 = 0.0f;
    if (当前标签滑入进度 <= 0.5f) {
        滑入系数 = 2.0f * 当前标签滑入进度 * 当前标签滑入进度;
    } else {
        滑入系数 = 1.0f - 2.0f * (1.0f - 当前标签滑入进度) * (1.0f - 当前标签滑入进度);
    }
    ImGui::BeginGroup();
    float 信息区域偏移 = 50.0f * (1.0f - 滑入系数);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 信息区域偏移);
    ImGui::Text("%s", label);
    if (当前标签选中索引 >= 0 && 当前标签选中索引 < 预设调色球.size()) {
        static std::map<std::string, float> 文字动画进度;
        if (文字动画进度.find(label) == 文字动画进度.end()) {
            文字动画进度[label] = 0.0f;
        }
        float& 文字进度 = 文字动画进度[label];
        if (fabs(文字进度 - 滑入系数) > 0.01f) {
            Lerp(文字进度, 滑入系数, ImGui::GetIO().DeltaTime * 10.0f);
        } else {
            文字进度 = 滑入系数;
        }
        std::string 当前文本 = "当前: " + std::string(预设调色球[当前标签选中索引].名称);
        int 显示字符数 = (int)(当前文本.length() * 文字进度);
        ImGui::Text("%s", 当前文本.substr(0, 显示字符数).c_str());
    } else {
        static std::map<std::string, float> 自定义文字动画;
        if (自定义文字动画.find(label) == 自定义文字动画.end()) {
            自定义文字动画[label] = 0.0f;
        }
        float& 文字进度 = 自定义文字动画[label];
        if (fabs(文字进度 - 滑入系数) > 0.01f) {
            Lerp(文字进度, 滑入系数, ImGui::GetIO().DeltaTime * 10.0f);
        } else {
            文字进度 = 滑入系数;
        }
        std::string 当前文本 = "当前: 自定义";
        int 显示字符数 = (int)(当前文本.length() * 文字进度);
        ImGui::Text("%s", 当前文本.substr(0, 显示字符数).c_str());
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::PushItemWidth(80); 
    float 选择器缩放 = 0.8f + 0.2f * 滑入系数;
    ImGui::SetWindowFontScale(选择器缩放);
    if (ImGui::ColorEdit4("##颜色选择器", color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoTooltip)) {
        已修改 = true;
        当前标签选中索引 = -1;
        for (int i = 0; i < 预设调色球.size(); i++) {
            if (fabs(color[0] - 预设调色球[i].颜色值.x) < 0.01f &&fabs(color[1] - 预设调色球[i].颜色值.y) < 0.01f &&
                fabs(color[2] - 预设调色球[i].颜色值.z) < 0.01f &&fabs(color[3] - 预设调色球[i].颜色值.w) < 0.01f) {
                当前标签选中索引 = i;
                break;
            }
        }
    }
    ImGui::SetWindowFontScale(1.0f); 
    ImGui::PopItemWidth();
    if (显示预设) {
        ImGui::SameLine();
        const char* 按钮文字 = 当前标签显示状态 ? "隐藏预设" : "显示预设";
        float 按钮浮动偏移 = sinf(ImGui::GetTime() * 3.0f) * 2.0f * 滑入系数;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 按钮浮动偏移);
        ImVec4 原始按钮颜色 = ImGui::GetStyle().Colors[ImGuiCol_Button];
        ImVec4 目标按钮颜色 = 原始按钮颜色;
        if (当前标签动画进度 > 0.5f) {
            目标按钮颜色.x *= 1.1f;
            目标按钮颜色.y *= 1.1f;
            目标按钮颜色.z *= 1.1f;
        } else {
            目标按钮颜色.x *= 0.9f;
            目标按钮颜色.y *= 0.9f;
            目标按钮颜色.z *= 0.9f;
        }
        ImVec4 当前按钮颜色;
        Lerp(当前按钮颜色.x, 原始按钮颜色.x, 目标按钮颜色.x, 动画系数);
        Lerp(当前按钮颜色.y, 原始按钮颜色.y, 目标按钮颜色.y, 动画系数);
        Lerp(当前按钮颜色.z, 原始按钮颜色.z, 目标按钮颜色.z, 动画系数);
        当前按钮颜色.w = 原始按钮颜色.w;
        ImGui::PushStyleColor(ImGuiCol_Button, 当前按钮颜色);
        if (ImGui::Button(按钮文字)) {
            当前标签显示状态 = !当前标签显示状态;
        }
        ImGui::PopStyleColor();
    }
    if (显示预设 && 当前标签滑入进度 > 0.01f) {
        ImGui::Separator();
        float 滑入偏移量 = 300.0f * (1.0f - 滑入系数); 
        ImGui::BeginGroup();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 滑入偏移量);
        int 总颜色数量 = 预设调色球.size();
        int 每行数量 = 5;
        int 行数 = (总颜色数量 + 每行数量 - 1) / 每行数量;
        float 颜色按钮大小 = 93.0f;
        float 水平间隔 = 35.0f;
        float 垂直间隔 = 40.0f;
        float 文本区域高度 = 25.0f;
        float 计算目标高度 = 行数 * (颜色按钮大小 + 文本区域高度 + 垂直间隔) + 50.0f;
        计算目标高度 = ImClamp(计算目标高度, 200.0f, 1200.0f);
        if (fabs(当前标签目标高度 - 计算目标高度) > 0.1f) {
            Lerp(当前标签目标高度, 计算目标高度, 0.1f);
        }
        if (fabs(当前标签当前高度 - 当前标签目标高度 * 滑入系数) > 0.1f) {
            Lerp(当前标签当前高度, 当前标签目标高度 * 滑入系数, 0.15f);
        } else {
            当前标签当前高度 = 当前标签目标高度 * 滑入系数;
        }
        ImGui::BeginChild("##预设调色板", ImVec2(0, 当前标签当前高度), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize);
        float 标题偏移 = 20.0f * (1.0f - 滑入系数);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 标题偏移);
        ImGui::Text("%s 颜色预设:", label);
        float 实际总宽度 = 每行数量 * 颜色按钮大小 + (每行数量 - 1) * 水平间隔;
        float 起始偏移 = (ImGui::GetContentRegionAvail().x - 实际总宽度) * 0.5f;
        if (起始偏移 > 0) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 起始偏移);
        }
        for (int i = 0; i < 预设调色球.size(); i++) {
            float 块延迟 = i * 0.02f; 
            float 块动画进度 = ImClamp((滑入系数 - 块延迟) / (1.0f - 块延迟), 0.0f, 1.0f);
            if (块动画进度 > 0.0f) {
                float 块缩放 = 0.5f + 0.5f * 块动画进度;
                if (i % 每行数量 != 0) {
                    ImGui::SameLine(0, 水平间隔);
                } else if (i > 0) {
                    ImGui::Spacing();
                    ImGui::Spacing();
                    if (起始偏移 > 0) {
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 起始偏移);
                    }
                }
                ImGui::BeginGroup();
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 25.5f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
                bool 当前选中 = (当前标签选中索引 == i);
                if (当前选中) {
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(预设调色球[i].颜色值.x * 0.7f,预设调色球[i].颜色值.y * 0.7f,预设调色球[i].颜色值.z * 0.7f,预设调色球[i].颜色值.w));
                } else {
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                    ImGui::PushStyleColor(ImGuiCol_Button, 预设调色球[i].颜色值);
                }
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(预设调色球[i].颜色值.x * 1.2f, 预设调色球[i].颜色值.y * 1.2f, 预设调色球[i].颜色值.z * 1.2f, 预设调色球[i].颜色值.w));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(预设调色球[i].颜色值.x * 0.8f, 预设调色球[i].颜色值.y * 0.8f, 预设调色球[i].颜色值.z * 0.8f, 预设调色球[i].颜色值.w));
                ImVec2 按钮尺寸 = ImVec2(颜色按钮大小 * 块缩放, 颜色按钮大小 * 块缩放);
                ImGui::PushID(i);
                if (ImGui::Button("##颜色按钮", 按钮尺寸)) {
                    color[0] = 预设调色球[i].颜色值.x;
                    color[1] = 预设调色球[i].颜色值.y;
                    color[2] = 预设调色球[i].颜色值.z;
                    color[3] = 预设调色球[i].颜色值.w;
                    当前标签选中索引 = i;
                    已修改 = true;
                }
                if (当前选中) {
                    ImDrawList* draw_list = ImGui::GetWindowDrawList();
                    ImVec2 按钮位置 = ImGui::GetItemRectMin();
                    ImVec2 按钮实际尺寸 = ImGui::GetItemRectSize();
                    float 边框大小 = 3.0f * 块缩放;
                    float 边框扩展 = 边框大小;
                    ImVec2 边框开始位置 = ImVec2(按钮位置.x - 边框扩展, 按钮位置.y - 边框扩展);
                    ImVec2 边框结束位置 = ImVec2(按钮位置.x + 按钮实际尺寸.x + 边框扩展, 按钮位置.y + 按钮实际尺寸.y + 边框扩展);
                    float 边框圆角 = 25.5f + 边框扩展 * 0.5f;
                    draw_list->AddRect(边框开始位置, 边框结束位置, IM_COL32(0, 120, 255, (int)(255 * 块动画进度)), 边框圆角, ImDrawFlags_RoundCornersAll, 8.0f);
                }
                ImGui::PopID();
                if (当前选中) {
                    ImGui::PopStyleColor(3);
                    ImGui::PopStyleVar(1);
                } else {
                    ImGui::PopStyleColor(3);
                    ImGui::PopStyleVar(1);
                }
                ImGui::PopStyleVar(2);
                ImVec2 当前光标位置 = ImGui::GetCursorPos();
                float 文本宽度 = ImGui::CalcTextSize(预设调色球[i].名称).x;
                float 最大允许宽度 = 颜色按钮大小;
                const char* 显示文本 = 预设调色球[i].名称;
                if (文本宽度 > 最大允许宽度) {
                    float 缩放比例 = 最大允许宽度 / 文本宽度;
                    ImGui::SetWindowFontScale(缩放比例 * 0.9f);
                    文本宽度 = ImGui::CalcTextSize(显示文本).x;
                }
                float 水平偏移量 = (颜色按钮大小 - 文本宽度) * 0.5f;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, 水平偏移量));
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 块动画进度);
                ImGui::Text("%s", 显示文本);
                ImGui::PopStyleVar();
                if (文本宽度 > 最大允许宽度) {
                    ImGui::SetWindowFontScale(1.0f);
                }
                ImGui::SetCursorPosY(当前光标位置.y + 文本区域高度);
                ImGui::EndGroup();
            }
        }
        ImGui::EndChild();
        ImGui::EndGroup(); 
        ImGui::Separator();
    }
    ImGui::PopID();
    return 已修改;
}

// ── INX BACKEND ──────────────────────────────────────────────
void UpdatePerformanceMetrics(){
    auto now = chrono::steady_clock::now();
    g_DeltaTime = chrono::duration_cast<chrono::duration<float>>(now-g_LastRenderTime).count();
    if(g_DeltaTime<0.001f) g_DeltaTime=0.016f;
    if(g_DeltaTime>0.05f)  g_DeltaTime=0.05f;
    g_FrameCount++;
    static auto lf=now;
    if(chrono::duration_cast<chrono::milliseconds>(now-lf).count()>=1000){
        g_FPS=(float)g_FrameCount; g_FrameCount=0; lf=now;
        g_LowPerf=(g_FPS<45.f);
    }
    g_LastRenderTime=now;
}
bool ShouldOptimize(){ return g_LowPerf||!g_MenuVis; }

// =============================================================
//  SAVE / LOAD — DISABLED: all settings default to OFF on start
// =============================================================
void SaveSettings(){
    // Intentionally empty — no persistence
}

void LoadSettings(){
    if(g_SettingsLoaded) return;
    // Do not load from disk — use struct defaults (all false/zero)
    g_SettingsLoaded=true;
}

// Sync g_Cfg values → hook.h globals (called after every toggle/slider change)
void UpdateAndSaveSettings(){
    showLine          = g_Cfg.showLine;
    showBox           = g_Cfg.showBox;
    showName          = g_Cfg.showName;
    showHealth        = g_Cfg.showHealth;
    showDistance      = g_Cfg.showDistance;
    showSkeleton      = g_Cfg.showSkeleton;
    is3600            = g_Cfg.is3600;
    Aimbot            = false; // Aimbot feature removed — never loaded from saved config
    aimBotBoneIndex   = g_Cfg.aimbotBone;
    AimSilent         = g_Cfg.aimSilent;
    HardwareBT        = g_Cfg.hardwareBT;
    BrutalBT          = g_Cfg.brutalBT;
    AimLock           = false; // AimLock feature removed — never loaded from saved config
    showAimLine       = false;   // aim line removed
    selectedBoneIndex = g_Cfg.selectedBone;
    AimFov            = g_Cfg.aimFovUI;
    textSize          = g_Cfg.textSz;
    chestRate         = g_Cfg.chestRateSave;
    if(chestRate<1) chestRate=1;
    if(chestRate>3) chestRate=3;
    BodyKill          = g_Cfg.bodyKill;
    BodyKillHP        = g_Cfg.bodyKillHP;
}

bool IsScreenLandscape(){
    screen_config();
    return (displayInfo.width>displayInfo.height);
}

// =============================================================
//  VOLUME KEY THREAD
// =============================================================
static void VolumeKeyThread(){
    struct pollfd fds[20]; int nfds=0;
    for(int i=0;i<20;i++){
        char p[32]; snprintf(p,sizeof(p),"/dev/input/event%d",i);
        int fd=open(p,O_RDONLY|O_NONBLOCK);
        if(fd>=0){ fds[nfds].fd=fd; fds[nfds].events=POLLIN; nfds++; }
    }
    while(main_thread_flag){
        // Bar mode: volume must not toggle menu
        if(g_MenuOpenMode != "vol"){
            this_thread::sleep_for(chrono::milliseconds(200));
            continue;
        }
        if(nfds==0){ this_thread::sleep_for(chrono::milliseconds(100)); continue; }
        if(poll(fds,nfds,50)>0){
            for(int i=0;i<nfds;i++){
                if(!(fds[i].revents&POLLIN)) continue;
                struct input_event ev;
                while(read(fds[i].fd,&ev,sizeof(ev))==sizeof(ev))
                    if(ev.type==EV_KEY&&ev.code==115&&ev.value==1)
                        g_MenuVis=!g_MenuVis;
            }
        }
    }
    for(int i=0;i<nfds;i++) close(fds[i].fd);
}

// =============================================================
//  COLOR HELPERS
// =============================================================
static ImU32 Ac(float a=1.f)   { return IM_COL32(217,0,0,(int)(255*a)); }
static ImU32 AcDim(float a=1.f){ return IM_COL32(100,0,0,(int)(255*a)); }
static float lrp(float a,float b,float t){ return a+(b-a)*t; }

static void shadow(ImVec2 p,ImVec2 s,float a){
    ImDrawList* bg=ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(ImVec2(p.x-4, p.y-4),  ImVec2(p.x+s.x+4, p.y+s.y+4),  IM_COL32(0,0,0,(int)(70*a)),10.f);
    bg->AddRectFilled(ImVec2(p.x-10,p.y-10), ImVec2(p.x+s.x+10,p.y+s.y+10), IM_COL32(0,0,0,(int)(40*a)),16.f);
    bg->AddRectFilled(ImVec2(p.x-18,p.y-18), ImVec2(p.x+s.x+18,p.y+s.y+18), IM_COL32(0,0,0,(int)(20*a)),22.f);
}

// =============================================================
//  WIDGETS
// =============================================================
static bool toggleRow(const char* lbl,bool* v,float a){
    ImGuiWindow* win=ImGui::GetCurrentWindow();
    if(win->SkipItems) return false;
    ImGuiContext& gc=*GImGui;
    const ImGuiID id=win->GetID(lbl);
    const float H=62.f;
    float W=ImGui::GetContentRegionAvail().x;
    ImRect bb(win->DC.CursorPos,ImVec2(win->DC.CursorPos.x+W,win->DC.CursorPos.y+H));
    ImGui::ItemSize(bb,gc.Style.FramePadding.y);
    if(!ImGui::ItemAdd(bb,id)) return false;
    bool pressed=ImGui::ButtonBehavior(bb,id,nullptr,nullptr);
    if(pressed){ *v=!(*v); UpdateAndSaveSettings(); }
    ImDrawList* dl=win->DrawList;
    ImVec2 p=bb.Min;
    if(ImGui::IsItemHovered())
        dl->AddRectFilled(p,bb.Max,IM_COL32(60,0,0,(int)(80*a)));
    ImVec2 ls=ImGui::CalcTextSize(lbl,nullptr,true);
    dl->AddText(ImVec2(p.x+14.f,p.y+(H-ls.y)*0.5f),IM_COL32(220,220,220,(int)(240*a)),lbl);
    const float ABW=6.f;
    float abX=p.x+W-ABW;
    dl->AddRectFilled(ImVec2(abX,p.y+12.f),ImVec2(abX+ABW,p.y+H-12.f),
                      *v?Ac(a):IM_COL32(40,0,0,(int)(160*a)),3.f);
    const float TH=30.f,TW=58.f;
    float tx=abX-TW-14.f,ty=p.y+(H-TH)*0.5f;
    dl->AddRectFilled(ImVec2(tx,ty),ImVec2(tx+TW,ty+TH),
                      *v?Ac(a):IM_COL32(22,0,0,(int)(255*a)),TH*0.5f);
    float kr=TH*0.40f;
    float kx=*v?(tx+TW-kr-4.f):(tx+kr+4.f);
    float ky=ty+TH*0.5f;
    dl->AddCircleFilled(ImVec2(kx,ky),kr+2.f,IM_COL32(255,255,255,(int)(15*a)),24);
    dl->AddCircleFilled(ImVec2(kx,ky),kr,    IM_COL32(255,255,255,(int)(255*a)),24);
    return pressed;
}

// toggleRow variant for Show FOV — directly controls g_ShowFovOverlay
static bool toggleRowFov(const char* lbl, float a){
    ImGuiWindow* win=ImGui::GetCurrentWindow();
    if(win->SkipItems) return false;
    ImGuiContext& gc=*GImGui;
    const ImGuiID id=win->GetID(lbl);
    const float H=62.f;
    float W=ImGui::GetContentRegionAvail().x;
    ImRect bb(win->DC.CursorPos,ImVec2(win->DC.CursorPos.x+W,win->DC.CursorPos.y+H));
    ImGui::ItemSize(bb,gc.Style.FramePadding.y);
    if(!ImGui::ItemAdd(bb,id)) return false;
    bool pressed=ImGui::ButtonBehavior(bb,id,nullptr,nullptr);
    if(pressed){
        g_ShowFovOverlay = !g_ShowFovOverlay;
        g_Cfg.showFovCircle = g_ShowFovOverlay;
    }
    bool v = g_ShowFovOverlay;
    ImDrawList* dl=win->DrawList;
    ImVec2 p=bb.Min;
    if(ImGui::IsItemHovered())
        dl->AddRectFilled(p,bb.Max,IM_COL32(60,0,0,(int)(80*a)));
    ImVec2 ls=ImGui::CalcTextSize(lbl,nullptr,true);
    dl->AddText(ImVec2(p.x+14.f,p.y+(H-ls.y)*0.5f),IM_COL32(220,220,220,(int)(240*a)),lbl);
    const float ABW=6.f;
    float abX=p.x+W-ABW;
    dl->AddRectFilled(ImVec2(abX,p.y+12.f),ImVec2(abX+ABW,p.y+H-12.f),
                      v?Ac(a):IM_COL32(40,0,0,(int)(160*a)),3.f);
    const float TH=30.f,TW=58.f;
    float tx=abX-TW-14.f,ty=p.y+(H-TH)*0.5f;
    dl->AddRectFilled(ImVec2(tx,ty),ImVec2(tx+TW,ty+TH),
                      v?Ac(a):IM_COL32(22,0,0,(int)(255*a)),TH*0.5f);
    float kr=TH*0.40f;
    float kx=v?(tx+TW-kr-4.f):(tx+kr+4.f);
    float ky=ty+TH*0.5f;
    dl->AddCircleFilled(ImVec2(kx,ky),kr+2.f,IM_COL32(255,255,255,(int)(15*a)),24);
    dl->AddCircleFilled(ImVec2(kx,ky),kr,    IM_COL32(255,255,255,(int)(255*a)),24);
    return pressed;
}

static bool sliderF(const char* lbl,float* v,float vmin,float vmax,float a,
                    const char* fmt="%.0f"){
    ImGuiWindow* win=ImGui::GetCurrentWindow();
    if(win->SkipItems) return false;
    ImGuiContext& gc=*GImGui;
    const ImGuiID id=win->GetID(lbl);
    const float LH=22.f,TH=12.f,KNOB=14.f;
    const float TOT=LH+14.f+TH+KNOB+10.f;
    float W=ImGui::GetContentRegionAvail().x;
    ImVec2 itemPos=win->DC.CursorPos;
    ImRect bb(itemPos,ImVec2(itemPos.x+W,itemPos.y+TOT));
    ImGui::ItemSize(bb,gc.Style.FramePadding.y);
    if(!ImGui::ItemAdd(bb,id)) return false;
    if(*v<vmin)*v=vmin; if(*v>vmax)*v=vmax;
    float ty2=bb.Min.y+LH+14.f,tx2=bb.Min.x+10.f,tw2=W-20.f;
    bool hovered,held;
    ImGui::ButtonBehavior(bb,id,&hovered,&held,ImGuiButtonFlags_PressedOnClick);
    bool changed=false;
    if(held&&ImGui::IsMouseDown(0)){
        float mx=ImGui::GetIO().MousePos.x;
        float nt=ImClamp((mx-tx2)/tw2,0.f,1.f);
        float nv=vmin+nt*(vmax-vmin);
        if(*v!=nv){ *v=nv; changed=true; UpdateAndSaveSettings(); }
    }
    ImDrawList* dl=win->DrawList;
    ImVec2 p=bb.Min;
    dl->AddText(ImVec2(p.x+8.f,p.y+4.f),IM_COL32(200,200,200,(int)(240*a)),lbl);
    char buf[32]; snprintf(buf,sizeof(buf),fmt,*v);
    ImVec2 vs=ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(p.x+W-vs.x-8.f,p.y+4.f),Ac(a),buf);
    dl->AddRectFilled(ImVec2(tx2,ty2),ImVec2(tx2+tw2,ty2+TH),IM_COL32(10,0,0,255),TH*0.5f);
    float t=(*v-vmin)/(vmax-vmin),fw=tw2*t;
    if(fw>0.f) dl->AddRectFilled(ImVec2(tx2,ty2),ImVec2(tx2+fw,ty2+TH),Ac(a),TH*0.5f);
    float kx2=tx2+fw,ky2=ty2+TH*0.5f;
    dl->AddCircleFilled(ImVec2(kx2,ky2),KNOB,       IM_COL32(220,220,220,255),24);
    dl->AddCircleFilled(ImVec2(kx2,ky2),KNOB*0.55f, IM_COL32(170,170,170,255),24);
    return changed;
}

// Chest Rate slider — works exactly like sliderF but snaps to int 1/2/3
// Same ButtonBehavior + held drag as sliderF — guaranteed to work
static bool chestRateSlider(const char* lbl, int* v, float a){
    ImGuiWindow* win=ImGui::GetCurrentWindow();
    if(win->SkipItems) return false;
    ImGuiContext& gc=*GImGui;
    const ImGuiID id=win->GetID(lbl);
    const float LH=22.f,TH=12.f,KNOB=14.f;
    const float TOT=LH+14.f+TH+KNOB+10.f;
    float W=ImGui::GetContentRegionAvail().x;
    ImVec2 itemPos=win->DC.CursorPos;
    ImRect bb(itemPos,ImVec2(itemPos.x+W,itemPos.y+TOT));
    ImGui::ItemSize(bb,gc.Style.FramePadding.y);
    if(!ImGui::ItemAdd(bb,id)) return false;
    if(*v<1)*v=1; if(*v>3)*v=3;
    float ty2=bb.Min.y+LH+14.f,tx2=bb.Min.x+10.f,tw2=W-20.f;
    bool hovered,held;
    ImGui::ButtonBehavior(bb,id,&hovered,&held,ImGuiButtonFlags_PressedOnClick);
    bool changed=false;
    if(held&&ImGui::IsMouseDown(0)){
        float mx=ImGui::GetIO().MousePos.x;
        float nt=ImClamp((mx-tx2)/tw2,0.f,1.f);
        // Map 0..1 → 1..3 snapping to nearest integer
        int nv=1+(int)roundf(nt*2.f);
        if(nv<1)nv=1; if(nv>3)nv=3;
        if(*v!=nv){ *v=nv; changed=true; }
    }
    ImDrawList* dl=win->DrawList;
    ImVec2 p=bb.Min;
    dl->AddText(ImVec2(p.x+8.f,p.y+4.f),IM_COL32(200,200,200,(int)(240*a)),lbl);
    char buf[8]; snprintf(buf,sizeof(buf),"%d",*v);
    ImVec2 vs=ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(p.x+W-vs.x-8.f,p.y+4.f),Ac(a),buf);
    // Track
    dl->AddRectFilled(ImVec2(tx2,ty2),ImVec2(tx2+tw2,ty2+TH),IM_COL32(10,0,0,255),TH*0.5f);
    // Fill — map value 1..3 to 0..1
    float t=((float)(*v-1))/2.f;
    float fw=tw2*t;
    if(fw>0.f) dl->AddRectFilled(ImVec2(tx2,ty2),ImVec2(tx2+fw,ty2+TH),Ac(a),TH*0.5f);
    // 3 tick marks so user sees discrete steps
    for(int i=0;i<=2;i++){
        float tx3=tx2+(tw2*((float)i/2.f));
        dl->AddLine(ImVec2(tx3,ty2-3.f),ImVec2(tx3,ty2+TH+3.f),
                    IM_COL32(60,0,0,180),1.f);
    }
    // Knob
    float kx2=tx2+fw,ky2=ty2+TH*0.5f;
    dl->AddCircleFilled(ImVec2(kx2,ky2),KNOB,      IM_COL32(220,220,220,255),24);
    dl->AddCircleFilled(ImVec2(kx2,ky2),KNOB*0.55f,IM_COL32(170,170,170,255),24);
    return changed;
}

// Selector row — 2 options (like Head/Chest button pair)
static void selectorRow(const char* lbl, int* val, const char* opt0, const char* opt1, float a){
    ImGuiWindow* win=ImGui::GetCurrentWindow();
    if(win->SkipItems) return;
    float W=ImGui::GetContentRegionAvail().x;
    ImVec2 p=win->DC.CursorPos;
    const float H=50.f,LW=W*0.38f,BtnW=(W-LW-8.f)*0.5f-4.f;
    ImDrawList* dl=win->DrawList;

    // Label
    ImVec2 ls=ImGui::CalcTextSize(lbl,nullptr,true);
    dl->AddText(ImVec2(p.x+10.f,p.y+(H-ls.y)*0.5f),
                IM_COL32(200,200,200,(int)(240*a)),lbl);

    // Button 0
    float bx0=p.x+LW;
    ImRect bb0(ImVec2(bx0,p.y+8.f),ImVec2(bx0+BtnW,p.y+H-8.f));
    bool h0=ImGui::IsMouseHoveringRect(bb0.Min,bb0.Max);
    bool sel0=(*val==0);
    dl->AddRectFilled(bb0.Min,bb0.Max,
        sel0?Ac(a):(h0?IM_COL32(60,0,0,(int)(200*a)):IM_COL32(22,0,0,(int)(200*a))),6.f);
    dl->AddRect(bb0.Min,bb0.Max,sel0?Ac(a):AcDim(a),6.f,0,1.2f);
    ImVec2 ts0=ImGui::CalcTextSize(opt0);
    dl->AddText(ImVec2(bb0.Min.x+(BtnW-ts0.x)*0.5f,bb0.Min.y+(bb0.Max.y-bb0.Min.y-ts0.y)*0.5f),
                IM_COL32(220,220,220,(int)(240*a)),opt0);
    if(h0&&ImGui::IsMouseClicked(0)&&*val!=0){ *val=0; UpdateAndSaveSettings(); }

    // Button 1
    float bx1=bx0+BtnW+8.f;
    ImRect bb1(ImVec2(bx1,p.y+8.f),ImVec2(bx1+BtnW,p.y+H-8.f));
    bool h1=ImGui::IsMouseHoveringRect(bb1.Min,bb1.Max);
    bool sel1=(*val==1);
    dl->AddRectFilled(bb1.Min,bb1.Max,
        sel1?Ac(a):(h1?IM_COL32(60,0,0,(int)(200*a)):IM_COL32(22,0,0,(int)(200*a))),6.f);
    dl->AddRect(bb1.Min,bb1.Max,sel1?Ac(a):AcDim(a),6.f,0,1.2f);
    ImVec2 ts1=ImGui::CalcTextSize(opt1);
    dl->AddText(ImVec2(bb1.Min.x+(BtnW-ts1.x)*0.5f,bb1.Min.y+(bb1.Max.y-bb1.Min.y-ts1.y)*0.5f),
                IM_COL32(220,220,220,(int)(240*a)),opt1);
    if(h1&&ImGui::IsMouseClicked(0)&&*val!=1){ *val=1; UpdateAndSaveSettings(); }

    ImGui::Dummy(ImVec2(0,H));
}

static bool actionButton(const char* lbl,float a,bool danger=false){
    ImGuiWindow* win=ImGui::GetCurrentWindow();
    if(win->SkipItems) return false;
    ImGuiContext& gc=*GImGui;
    const ImGuiID id=win->GetID(lbl);
    const float H=60.f;
    float W=ImGui::GetContentRegionAvail().x;
    ImRect bb(win->DC.CursorPos,ImVec2(win->DC.CursorPos.x+W,win->DC.CursorPos.y+H));
    ImGui::ItemSize(bb,gc.Style.FramePadding.y);
    if(!ImGui::ItemAdd(bb,id)) return false;
    bool hov,held,pressed=ImGui::ButtonBehavior(bb,id,&hov,&held);
    ImDrawList* dl=win->DrawList;
    ImVec2 p=bb.Min;
    ImU32 bg=danger
        ?(hov?IM_COL32(200,0,0,(int)(240*a)):IM_COL32(130,0,0,(int)(220*a)))
        :(hov?IM_COL32(50,0,0,(int)(230*a)) :IM_COL32(22,0,0,(int)(210*a)));
    dl->AddRectFilled(p,bb.Max,bg,8.f);
    dl->AddRect(p,bb.Max,danger?Ac(a):AcDim(a),8.f,0,1.2f);
    ImVec2 ts=ImGui::CalcTextSize(lbl);
    dl->AddText(ImVec2(p.x+(W-ts.x)*0.5f,p.y+(H-ts.y)*0.5f),
                IM_COL32(220,220,220,(int)(248*a)),lbl);
    return pressed;
}

// =============================================================
//  ICONS
// =============================================================
static void IconCube(ImDrawList* dl,float cx,float cy,float s,bool act,float a){
    ImVec2 FL(cx-s,cy-s*0.32f),FR(cx+s,cy-s*0.32f),BR(cx+s,cy+s*0.60f);
    float ox=s*0.36f,oy=s*0.28f;
    ImU32 f1=act?IM_COL32(200,0,0,(int)(230*a)):IM_COL32(80,80,80,(int)(172*a));
    ImU32 f2=act?IM_COL32(230,30,30,(int)(210*a)):IM_COL32(100,100,100,(int)(152*a));
    ImU32 f3=act?IM_COL32(140,0,0,(int)(210*a)):IM_COL32(60,60,60,(int)(152*a));
    ImU32 ed=act?Ac(a):IM_COL32(130,130,130,(int)(190*a));
    ImVec2 BL(cx-s,cy+s*0.60f);
    dl->AddRectFilled(FL,BR,f1,2.f);
    dl->AddRect(FL,BR,ed,2.f,0,1.4f);
    ImVec2 tp[4]={FL,{FL.x+ox,FL.y-oy},{FR.x+ox,FR.y-oy},FR};
    dl->AddConvexPolyFilled(tp,4,f2);
    dl->AddPolyline(tp,4,ed,ImDrawFlags_Closed,1.4f);
    ImVec2 rp[4]={FR,{FR.x+ox,FR.y-oy},{BR.x+ox,BR.y-oy},BR};
    dl->AddConvexPolyFilled(rp,4,f3);
    dl->AddPolyline(rp,4,ed,ImDrawFlags_Closed,1.4f);
    (void)BL;
}
static void IconShield(ImDrawList* dl,float cx,float cy,float s,bool act,float a){
    ImU32 ic=act?Ac(a):IM_COL32(120,120,120,(int)(200*a));
    float hw=s*0.75f,hh2=s*0.95f;
    ImVec2 pts[6]={{cx,cy-hh2},{cx+hw,cy-hh2*0.5f},{cx+hw,cy+hh2*0.1f},
                   {cx,cy+hh2},{cx-hw,cy+hh2*0.1f},{cx-hw,cy-hh2*0.5f}};
    dl->AddPolyline(pts,6,ic,ImDrawFlags_Closed,2.2f);
    dl->AddLine(ImVec2(cx,cy-hh2*0.4f),ImVec2(cx,cy+hh2*0.4f),ic,1.8f);
    dl->AddLine(ImVec2(cx-hw*0.45f,cy),ImVec2(cx+hw*0.45f,cy),ic,1.8f);
}
static void IconInfo(ImDrawList* dl,float cx,float cy,float s,bool act,float a){
    ImU32 ic=act?Ac(a):IM_COL32(120,120,120,(int)(200*a));
    dl->AddCircle(ImVec2(cx,cy),s,ic,32,2.2f);
    dl->AddCircleFilled(ImVec2(cx,cy-s*0.32f),s*0.13f,ic,12);
    dl->AddLine(ImVec2(cx,cy-s*0.10f),ImVec2(cx,cy+s*0.42f),ic,2.f);
}
static void IconGear(ImDrawList* dl,float cx,float cy,float s,bool act,float a){
    ImU32 ic=act?Ac(a):IM_COL32(120,120,120,(int)(200*a));
    float tooth=s*0.42f;
    for(int j=0;j<8;j++){
        float a1=(float)j*3.14159f*2.f/8.f,a2=(float)(j+1)*3.14159f*2.f/8.f;
        float am=(a1+a2)*0.5f,da=(a2-a1)*0.28f;
        ImVec2 gp[4]={
            {cx+cosf(am-da)*s,        cy+sinf(am-da)*s},
            {cx+cosf(am-da)*(s+tooth),cy+sinf(am-da)*(s+tooth)},
            {cx+cosf(am+da)*(s+tooth),cy+sinf(am+da)*(s+tooth)},
            {cx+cosf(am+da)*s,        cy+sinf(am+da)*s}
        };
        dl->AddConvexPolyFilled(gp,4,ic);
    }
    dl->AddCircleFilled(ImVec2(cx,cy),s,      ic,32);
    dl->AddCircleFilled(ImVec2(cx,cy),s*0.50f,IM_COL32(0,0,0,255),20);
}

// =============================================================
//  CHEST RATE HINT TEXT
//  Returns description string for current chestRate value
// =============================================================
static const char* ChestRateHint(){
    switch(chestRate){
        case 1: return "Headshot Mode";
        case 2: return "Body Shot Mode";
        case 3: return "Legit Mode";
        default: return "";
    }
}

// =============================================================
//  AIM LOCK — Coming Soon row (inline in aim tab, toggle always OFF)
// =============================================================
static void aimLockComingSoonRow(float a){
    ImGuiWindow* win=ImGui::GetCurrentWindow();
    if(win->SkipItems) return;
    const float H=62.f;
    float W=ImGui::GetContentRegionAvail().x;
    ImVec2 p=win->DC.CursorPos;
    // Reserve space
    ImGui::Dummy(ImVec2(W,H));
    ImDrawList* dl=win->DrawList;

    // Row background (slightly dimmer to signal disabled)
    dl->AddRectFilled(p,ImVec2(p.x+W,p.y+H),IM_COL32(10,0,0,(int)(120*a)));

    // Label "Aim Lock"
    const char* lbl="Aim Lock";
    ImVec2 ls=ImGui::CalcTextSize(lbl);
    dl->AddText(ImVec2(p.x+14.f,p.y+(H-ls.y)*0.5f),
                IM_COL32(130,130,130,(int)(220*a)),lbl);

    // "Coming Soon" badge right of label
    const char* cs="(Coming Soon)";
    ImVec2 cs_sz=ImGui::CalcTextSize(cs);
    dl->AddText(ImVec2(p.x+14.f+ls.x+8.f,p.y+(H-cs_sz.y)*0.5f),
                Ac(a),cs);

    // Permanently OFF toggle (visual only, not interactive)
    const float TH=30.f,TW=58.f;
    float abX=p.x+W-6.f;
    float tx=abX-TW-14.f,ty=p.y+(H-TH)*0.5f;
    // Track background — always dark (OFF)
    dl->AddRectFilled(ImVec2(tx,ty),ImVec2(tx+TW,ty+TH),
                      IM_COL32(16,0,0,(int)(200*a)),TH*0.5f);
    // Knob at left (OFF position)
    float kr=TH*0.40f;
    float kx=tx+kr+4.f;
    float ky=ty+TH*0.5f;
    dl->AddCircleFilled(ImVec2(kx,ky),kr+2.f,IM_COL32(255,255,255,(int)(10*a)),24);
    dl->AddCircleFilled(ImVec2(kx,ky),kr,    IM_COL32(90,90,90,(int)(200*a)),24);
    // Accent bar — dimmed (OFF)
    dl->AddRectFilled(ImVec2(abX-6.f,p.y+12.f),ImVec2(abX,p.y+H-12.f),
                      IM_COL32(40,0,0,(int)(100*a)),3.f);
}

// =============================================================
//  TAB CONTENT  —  xmod light theme
// =============================================================

// ── AIM TAB ──────────────────────────────────────────────────
static void aimbot_tab(float /*a*/){
    ImGui::Dummy(ImVec2(0,8.f));

    M_CircularCheckbox1("Aimbot",         &g_Cfg.Aimbot);
    M_CircularCheckbox1("Aim Silent",     &g_Cfg.aimSilent);
    M_CircularCheckbox1("Show Aim Line",  &g_Cfg.showAimLine);
    M_CircularCheckbox1("Show FOV",       &g_Cfg.showFovCircle);

    ImGui::Dummy(ImVec2(0,6.f));

    M_CustomSliderFloatvv("Aim FOV",     &g_Cfg.aimFovUI, 1.f, 180.f, "%.0f");

    ImGui::Dummy(ImVec2(0,6.f));

    // ── Body Kill ────────────────────────────────────────────
    // Toggle + HP slider (1-200). When on and Aim Silent has a
    // locked target, writes this HP into that enemy's health
    // chain every tick — works whether or not the bullet lands.
    M_CircularCheckbox1("Body Kill",      &g_Cfg.bodyKill);
    if(g_Cfg.bodyKill){
        M_CustomSliderFloatvv("Kill HP", &g_Cfg.bodyKillHP, 1.f, 200.f, "%.0f");
    }

    ImGui::Dummy(ImVec2(0,6.f));

    // Aim Bone selector
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.94f,0.94f,0.94f,0.98f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.f);
        ImGui::BeginChild("##aimbone", ImVec2(ImGui::GetContentRegionAvail().x, 60.f), false,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::SetCursorPos(ImVec2(20.f, 10.f));
        ImGui::TextColored(ImVec4(0,0,0,1), "Aim Bone");
        ImGui::SameLine();
        fanhua({"Head","Chest"}, tap页[10], 0, 130.f, 38.f, 8.f);
        g_Cfg.aimbotBone = (int)tap页[10];
        ImGui::EndChild();
    }

    ImGui::Dummy(ImVec2(0,6.f));

    // Chest Rate
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.94f,0.94f,0.94f,0.98f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.f);
        ImGui::BeginChild("##cr", ImVec2(ImGui::GetContentRegionAvail().x, 60.f), false,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::SetCursorPos(ImVec2(20.f, 10.f));
        ImGui::TextColored(ImVec4(0,0,0,1), "Chest Rate");
        ImGui::SameLine();
        fanhua({"Headshot","Body","Legit"}, tap页[11], 0, 95.f, 38.f, 6.f);
        g_Cfg.chestRateSave = (int)tap页[11] + 1;
        ImGui::EndChild();
    }

    ImGui::Dummy(ImVec2(0,6.f));

    // Sync to hook globals every frame
    g_ShowFovOverlay   = g_Cfg.showFovCircle;
    Aimbot             = false; // Aimbot feature removed — never loaded from saved config
    aimBotBoneIndex    = g_Cfg.aimbotBone;
    AimSilent          = g_Cfg.aimSilent;
    HardwareBT         = g_Cfg.hardwareBT;
    BrutalBT           = g_Cfg.brutalBT;
    AimLock            = false; // AimLock feature removed — never loaded from saved config
    AimFov             = g_Cfg.aimFovUI;
    showAimLine        = g_Cfg.showAimLine;
    BodyKill           = g_Cfg.bodyKill;
    BodyKillHP         = g_Cfg.bodyKillHP;
    chestRate          = g_Cfg.chestRateSave;
    if(chestRate<1)chestRate=1;
    if(chestRate>3)chestRate=3;
}

// ── PLAYER / ESP TAB ─────────────────────────────────────────
static void player_tab(float /*a*/){
    ImGui::Dummy(ImVec2(0,8.f));

    M_CircularCheckbox1("Show Lines",    &g_Cfg.showLine);
    M_CircularCheckbox1("Show Boxes",    &g_Cfg.showBox);
    M_CircularCheckbox1("Show Health",   &g_Cfg.showHealth);
    M_CircularCheckbox1("Show Distance", &g_Cfg.showDistance);
    M_CircularCheckbox1("Show Skeleton", &g_Cfg.showSkeleton);
    M_CircularCheckbox1("Show Name",     &g_Cfg.showName);
    M_CircularCheckbox1("360 Alert",     &g_Cfg.is3600);

    ImGui::Dummy(ImVec2(0,8.f));

    float ts = (float)textSize;
    if(M_CustomSliderFloatvv("Text Size", &ts, 10.f, 50.f, "%.0f")) textSize=(int)ts;

    // Sync to hook globals
    showLine     = g_Cfg.showLine;
    showBox      = g_Cfg.showBox;
    showSkeleton = g_Cfg.showSkeleton;
    showName     = g_Cfg.showName;
    showHealth   = g_Cfg.showHealth;
    showDistance = g_Cfg.showDistance;
    is3600       = g_Cfg.is3600;
}

// ── INFO TAB ─────────────────────────────────────────────────
static void info_tab(float a){
    ImGuiWindow* w=ImGui::GetCurrentWindow();
    ImDrawList* dl=w->DrawList;
    ImGui::Dummy(ImVec2(0,12.f));
    float ww=ImGui::GetContentRegionAvail().x;
    const float BH=52.f,GAP=10.f,PAD=18.f;
    auto row=[&](const char* label,const char* value,ImU32 valCol){
        ImVec2 p=w->DC.CursorPos;
        dl->AddRectFilled(p,ImVec2(p.x+ww,p.y+BH),IM_COL32(245,245,245,230),10.f);
        dl->AddRect(p,ImVec2(p.x+ww,p.y+BH),IM_COL32(200,200,200,200),10.f,0,1.f);
        ImVec2 ls=ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(p.x+PAD,p.y+(BH-ls.y)*0.5f),IM_COL32(80,80,80,230),label);
        ImVec2 vs=ImGui::CalcTextSize(value);
        dl->AddText(ImVec2(p.x+ww-vs.x-PAD,p.y+(BH-vs.y)*0.5f),valCol,value);
        ImGui::Dummy(ImVec2(0,BH+GAP));
    };
    char buf[80];
    {
        bool driverOk=(g_driver!=nullptr&&g_GameIsRunning);
        row("Driver :", driverOk?"Active":"Inactive",
            driverOk?IM_COL32(0,180,80,255):IM_COL32(220,60,60,255));
    }
    _SrvData sd; { std::lock_guard<std::mutex> lk(g_SrvMtx); sd=g_Srv; }
    row("License :",  sd.key_masked.c_str(),  IM_COL32(75,190,255,255));
    snprintf(buf,sizeof(buf),"%d Days",sd.days_total);
    row("Validity :", buf,                     IM_COL32(75,190,255,255));
    snprintf(buf,sizeof(buf),"%d/%d",sd.dev_used,sd.dev_max);
    row("Usage :",    buf,                     IM_COL32(100,100,100,220));
    snprintf(buf,sizeof(buf),"%d Days remain",sd.days_remain);
    row("Remain :",   buf,                     IM_COL32(75,190,255,255));
    row("Expires :",  sd.expiry.c_str(),       IM_COL32(75,190,255,255));
    row("Seller :",   sd.seller.c_str(),       IM_COL32(75,190,255,255));
    (void)a;
}

// ── SETTINGS TAB ─────────────────────────────────────────────
static void settings_tab(float a){
    ImGui::Dummy(ImVec2(0,10.f));
    ImGuiWindow* w=ImGui::GetCurrentWindow();
    ImDrawList* dl=w->DrawList;
    float ww=ImGui::GetContentRegionAvail().x;

    // Status row
    {
        ImVec2 p=w->DC.CursorPos;
        const float BH=46.f;
        dl->AddRectFilled(p,ImVec2(p.x+ww,p.y+BH),IM_COL32(245,245,245,230),10.f);
        dl->AddRect(p,ImVec2(p.x+ww,p.y+BH),IM_COL32(200,200,200,180),10.f,0,1.f);
        char buf[80]; snprintf(buf,sizeof(buf),"Game: %s    FPS: %.0f",
                               g_GameIsRunning?"RUNNING":"NOT FOUND",g_FPS);
        ImVec2 ts=ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2(p.x+16.f,p.y+(BH-ts.y)*0.5f),
                    g_GameIsRunning?IM_COL32(0,180,80,255):IM_COL32(150,150,150,200),buf);
        ImGui::Dummy(ImVec2(0,BH+12.f));
    }

    ImGui::Dummy(ImVec2(0,8.f));

    // Particle style selector
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.94f,0.94f,0.94f,0.98f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.f);
        ImGui::BeginChild("##ps", ImVec2(ww, 60.f), false, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar(); ImGui::PopStyleColor();
        ImGui::SetCursorPos(ImVec2(20.f, 10.f));
        ImGui::TextColored(ImVec4(0,0,0,1), "Particles");
        ImGui::SameLine();
        static float pstyle_sel = 0.f;
        fanhua({"Snow","Color","Leaf"}, pstyle_sel, 0, 90.f, 38.f, 6.f);
        int newStyle = (int)pstyle_sel;
        if(newStyle != currentParticleStyle) 切换粒子样式(newStyle);
        ImGui::EndChild();
    }

    ImGui::Dummy(ImVec2(0,10.f));

    // Hide menu button
    if(M_CircularButton("HIDE MENU  (Vol+ to toggle)")){
        g_MenuVis=false;
    }
    ImGui::Dummy(ImVec2(0,10.f));

    // Exit button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f,0.1f,0.1f,0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.f,0.2f,0.2f,1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f,0.05f,0.05f,1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.f);
    float bw = ww - 40.f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.f);
    if(ImGui::Button("TERMINATE & EXIT", ImVec2(bw, 60.f))){
        UpdateAndSaveSettings();
        main_thread_flag=false; shutdown(); Touch_Close(); _exit(0);
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::Dummy(ImVec2(0,8.f));
    (void)a;
}

// =============================================================
//  RENDER MENU
// =============================================================

// ── Source project NEW UI ────────────────────────────────────
#include "UI/SourceMenu.h"
#include "ix_controls.h"

// ── Remote control ──────────────────────────────────────────
// The server itself lives in the daemon now, because the overlay only exists
// while the game is open: switching to a browser backgrounds the game, and a
// game Android decides to reclaim takes the server with it. That was the
// "connection refused" — a valid link with nothing left listening.
//
// Here we only read what the daemon wrote and copy it into the variables the
// features actually use. Parsed only when the file's mtime moves, so an idle
// frame costs one stat().
static void ix_pull_remote_controls() {
    if (!ix_controls_changed()) return;

    IxControls c;
    ix_controls_from_json(ix_controls_to_json(c), c);   // start from defaults
    if (!ix_controls_load(c)) return;

    g_Cfg.showBox       = c.esp_box;
    g_Cfg.showSkeleton  = c.esp_skeleton;
    g_Cfg.showName      = c.esp_name;
    g_Cfg.showHealth    = c.esp_health;
    g_Cfg.showDistance  = c.esp_distance;
    g_Cfg.showLine      = c.esp_line;
    g_Cfg.is3600        = c.esp_360;
    textSize            = c.esp_textsize;

    AimSilent           = c.aim_silent;
    // [FIX] aim_hwbt / aim_mode were already fields on IxControls the whole
    // time (ix_controls.h serializes and deserializes both) — an earlier
    // comment here claimed they weren't, which was wrong. They just weren't
    // being read into g_Cfg by this function, so the WebUI's remote-control
    // page had no effect on Hardware BT even though the wire format already
    // carried it end to end. Wired through now, same as every other field
    // here.
    g_Cfg.hardwareBT    = c.aim_hwbt;
    HardwareBT          = c.aim_hwbt;
    if (c.aim_mode >= 1 && c.aim_mode <= 3) g_Cfg.chestRateSave = c.aim_mode;
    g_Cfg.showFovCircle = c.aim_fovcircle;
    g_Cfg.chestRateSave = 1;
    g_Cfg.aimFovUI      = c.aim_fov;

    // [REMOVED] Pull / Float-Pull — force false regardless of remote-control
    // state (c.exp_pull / c.exp_floatpull) so the WebUI/daemon control path
    // can't silently re-enable either feature now that it's removed.
    AimMagnet           = false;
    GhostHack           = c.exp_ghost;
    NoRecoil            = c.exp_norecoil;
    NoReload            = c.exp_noreload;
    g_ExploitFloatUI    = false;
    g_ExploitFloatGhost = c.exp_floatghost;
    MagnetMaxDist       = c.exp_pullrange;
}

// The menu runs these every frame inside whichever page is open. With the
// menu not drawn at all they have to run here, or a change made from the
// page would never reach the feature that reads it.
static void ix_apply_control_sync() {
    showLine     = g_Cfg.showLine;
    showBox      = g_Cfg.showBox;
    showSkeleton = g_Cfg.showSkeleton;
    showName     = g_Cfg.showName;
    showHealth   = g_Cfg.showHealth;
    showDistance = g_Cfg.showDistance;
    is3600       = g_Cfg.is3600;
    Enable = (showLine || showBox || showSkeleton || showName ||
              showHealth || showDistance || showCount);

    g_ShowFovOverlay  = g_Cfg.showFovCircle;
    AimFov            = g_Cfg.aimFovUI;
    g_Cfg.aimSilent   = AimSilent;
    AimLock           = false;
    g_Cfg.aimCollider = false;
    AimLockNew        = false;
    BrutalBT          = g_Cfg.brutalBT;
    aimBotBoneIndex   = 0;
    showAimLine       = g_Cfg.showAimLine;
    chestRate         = g_Cfg.chestRateSave;
    if (chestRate < 1) chestRate = 1;
    if (chestRate > 3) chestRate = 3;
    // [RE-ADDED] Without this, Hardware BT only ever synced inside the AIM
    // page's own block in SourceMenu.h — if that page was never opened this
    // run, HardwareBT would stay stuck at whatever it was on the previous
    // run (or false on a fresh process), ignoring the checkbox entirely.
    HardwareBT        = g_Cfg.hardwareBT;
    StartSilentAimThread();
    StartHardwareBTThread();
    // Body Kill globals synced from config — no separate thread: the
    // threshold check (BodyKillThresholdReached) runs inline inside the
    // FOV loop and UpdateSilentAimFast, both already driven by
    // StartSilentAimThread above.
    BodyKill   = g_Cfg.bodyKill;
    BodyKillHP = g_Cfg.bodyKillHP;
}


void RenderMenu(){
    if(!g_FloatingSystemReady||!g_Landscape2SecDone) return;
    if(!g_SettingsLoaded) LoadSettings();
    if (!ix_rt_ready()) {
        /* settings file cannot unlock cheats without server secret */
        g_Cfg.showLine=g_Cfg.showBox=g_Cfg.showHealth=false;
        g_Cfg.showDistance=g_Cfg.showSkeleton=g_Cfg.showName=false;
        g_Cfg.Aimbot=g_Cfg.aimSilent=g_Cfg.hardwareBT=g_Cfg.brutalBT=g_Cfg.aimCollider=false;
        g_Cfg.bodyKill=false;
        g_Cfg.showFovCircle=false;
        AimSilent=HardwareBT=AimLock=Aimbot=BrutalBT=false;
        showLine=showBox=showHealth=showDistance=showSkeleton=showName=false;
        Enable=false;
    }
    LoadEmbeddedTextures();

    // Screen metrics for OG bar
    ui::bar::g_sw = (float)displayInfo.width;
    ui::bar::g_sh = (float)displayInfo.height;
    if(ui::bar::g_sw < 1.f){
        ui::bar::g_sw = (float)abs_ScreenX;
        ui::bar::g_sh = (float)abs_ScreenY;
    }

    // ── Remote control ──────────────────────────────────────
    // Read once: whether the module asked for it. While it is on the menu is
    // not drawn at all — no bar, no panel, no volume path — and the values
    // come from the file the daemon writes.
    static const bool remoteOn = ix_get_remote_control();

    if (remoteOn) {
        ix_pull_remote_controls();   // whatever the page changed
        ix_apply_control_sync();     // the syncs the menu would have run
        HideMenuWindow();
        return;
    }

    if(g_MenuOpenMode == "bar"){
        // OG white bar bottom-center — tap toggles menu
        ui::bar::set_game_alpha(1.f);
        ui::bar::render();

        static bool wasBarOpen = false;
        if(ui::bar::g_open && !wasBarOpen){
            g_MenuVis = true;
            ShowMenuWindow();
        }
        if(!ui::bar::g_open && wasBarOpen){
            g_MenuVis = false;
            HideMenuWindow();
        }
        wasBarOpen = ui::bar::g_open;

        // Closed via menu X — sync bar when fully hidden
        if(!IsMenuWindowVisible() && ui::bar::g_open){
            ui::bar::g_open   = false;
            ui::bar::g_closed = true;
            wasBarOpen        = false;
            g_MenuVis         = false;
        }
    } else {
        // Volume mode — existing path only (no bar drawn)
        static bool lastMenuVis = false;
        if(g_MenuVis != lastMenuVis){
            if(g_MenuVis) ShowMenuWindow();
            else          HideMenuWindow();
            lastMenuVis = g_MenuVis;
        }
    }

    RenderSingleMenuWindow();
}

int main(int argc,char* argv[]){
    // Installed before anything else — the Android 17 crash happens
    // after the driver attaches, well past this point, so this needs
    // to already be in place by then. See ix_install_crash_handler()
    // above for why this is the right fix to make here: we don't have
    // enough information yet to know WHAT in the crash to fix, so this
    // makes the next occurrence self-diagnosing instead of guessing.
    ix_install_crash_handler();
#ifdef IX_INTERACTIVE_CLI
    ix_install_output_tee();
#endif

    // =============================================================
    // [FIX — ONE LOGIN, NOT TWO SEPARATE ONES] This used to call
    // auth.run() unconditionally — the fully INTERACTIVE terminal
    // login, which reads the key from stdin and only calls
    // kd_save_key() after a key is typed and accepted right here.
    // That is correct for a human at a real terminal (kd_ctl's
    // manual/debug path still has one), but this binary is normally
    // started by the app via ProcessBuilder with no TTY attached —
    // exactly the "no stdin" situation already called out in the
    // driver-selection comment below. With no saved key yet, run()'s
    // std::cin.getline() hits EOF immediately, the 3-try loop breaks
    // on the first attempt, login "fails" every single time, and the
    // success-only kd_save_key() call is never reached — so a key
    // launched this way could NEVER get saved, and the app's own
    // already-successful login and this binary's internal login could
    // disagree about whether the device is authenticated at all.
    //
    // Fix: the app now passes its own already-validated license key
    // as argv[1] (see AuthManager.securityGate()/BinaryExecutor on the
    // app side) — ONE login the user types, in the app. This binary
    // still runs its OWN full, independent server round-trip against
    // that same key (run_silent() → perform_auth(), unchanged, still
    // the real "binary login") — the dual-check security contract is
    // intact, only the broken interactive re-prompt is gone. On
    // success the key is saved exactly as before; if the binary is
    // ever launched again with NO argument (e.g. directly from the
    // extracted lib path, outside the app), run_silent("") picks the
    // saved key straight back up via kd_load_saved_key() — no stdin
    // needed either way.
    // =============================================================
    Authentication auth;
#ifdef IX_INTERACTIVE_CLI
    // [STANDALONE BUILD — IX_INTERACTIVE_CLI] This binary (inx_standalone)
    // is the SAME main.cpp as the module's optimum_bar, just compiled with
    // this one extra flag (see jni/Android.mk) -- one binary, one codebase,
    // nothing duplicated or re-implemented. The ONLY difference is which
    // already-existing Authentication method runs: run() instead of
    // run_silent(argKey). run() is the pre-existing interactive terminal
    // login (banner, HWID, saved-key check, 3 tries, format check, save on
    // success) that was already written and already used elsewhere (the
    // comment above explains why optimum_bar normally can't use it — no
    // stdin when launched by the daemon). Run by hand from a real
    // terminal, stdin exists, so the original reason it was swapped out
    // doesn't apply here, and nothing about the login itself changes.
    (void)argc; (void)argv;
    bool ok = auth.run();
#else
    const std::string argKey = (argc >= 2 && argv[1] != nullptr) ? std::string(argv[1]) : std::string();
    bool ok = auth.run_silent(argKey);
#endif
    if (!ok || !g_AuthKernelOk || !ix_rt_ready() || !ix_feat_ok()) {
        ix_features_lock();
        ix_rt_clear();
        printf("%sAuth/runtime gate failed — functions locked.%s\n", "\033[91m", "\033[0m");
        sleep(3);
        return 0;
    }

    {
        std::lock_guard<std::mutex> lk(g_SrvMtx);
        g_Srv.key_masked  = auth.getLicenseKey();
        g_Srv.seller      = auth.getSellerName();
        g_Srv.expiry      = auth.getExpiryDate();
        g_Srv.days_remain = auth.getDaysRemain();
        g_Srv.days_total  = auth.getTotalDays();
        g_Srv.dev_used    = auth.getDevicesUsed();
        g_Srv.dev_max     = auth.getMaxDevices();
        g_Srv.valid       = true;
    }

    printf("\n[LAUNCHING InvisibleX Reborn]\n");

#ifdef IX_INTERACTIVE_CLI
    ix_interactive_driver_and_menu_prompt();
#endif

    LoadMenuOpenMode();
    printf("[INX] menu open mode: %s\n", g_MenuOpenMode.c_str());
    fflush(stdout);

    // ── DRIVER SELECTION (WebUI) ─────────────────────────────────
    // c_driver::g_driver_choice ships as -1, which makes the
    // constructor print its interactive menu and block on stdin.
    // Started by the module daemon there is no stdin, so the loader
    // would authenticate and then hang here forever. Setting the
    // choice first skips the menu entirely.
    //   1 QX1  2 GT  3 rt-dev  4 rt-proc  5 QX2
    //   6 TB-KPM  7 DitPro-KPM  8 KMA-KPM  9 LDK-KPM
    c_driver::g_driver_choice = ix_get_driver_choice();
    printf("[INX] driver mode %d (%s)\n",
           c_driver::g_driver_choice, ix_get_selected_driver().c_str());
    fflush(stdout);

    g_driver = new c_driver();
    printf("[+] Ready\n");

    screen_config();
    bool lms=false,pms=false;
    while(main_thread_flag){
        screen_config();
        if(IsScreenLandscape()){
            if(!lms){lms=true;pms=false;}
            auto t0=chrono::steady_clock::now(); bool done=false;
            while(!done&&main_thread_flag){
                usleep(100000); screen_config();
                if(!IsScreenLandscape()){lms=false;break;}
                if(chrono::duration_cast<chrono::milliseconds>(
                    chrono::steady_clock::now()-t0).count()>=2000) done=true;
            }
            if(done) break;
        } else {
            if(!pms){printf("[PLEASE OPEN FREE FIRE MAX]\n");pms=true;lms=false;}
            usleep(500000);
        }
    }
    if(!main_thread_flag) return 0;

    g_LastRenderTime=chrono::steady_clock::now();

    thread vkT(VolumeKeyThread); vkT.detach();

    // All threads detached — no join in game loop (crash fix)
    StartSilentAimThread();
    // Body Kill has no separate thread — its threshold check runs inline
    // inside StartSilentAimThread's loop (UpdateSilentAimFast) and the FOV
    // targeting loop.
    // Aimbot feature removed — thread never started.
    StartEspThread();

    game_pid=getProcessID(packageName);
    if(game_pid<=0) g_GameIsRunning=false;
    else {
        lib_base  =get_module_base(targetaLibName, PERM_EXEC);
        lib_base2 =get_module_base(targetaLibName2,PERM_EXEC);
        g_GameIsRunning=true;
        if(g_driver && g_AuthKernelOk){
            g_driver->initialize((pid_t)game_pid);
            g_driver->initKey("InvisibleX");
            printf("[+] Driver attached to PID: %d\n",game_pid);

            // [CHANGED — no longer automatic] The ptrace attach this
            // needs stops a thread in the live game process; doing that
            // unconditionally on every launch was reported crashing the
            // app. Injection now only happens when the "Inject Collider
            // Stub" button on the Settings page is pressed — g_ColliderIpc
            // stays null until then, so hook.h's direct in-process HWBP
            // call is what actually runs by default.
        }
    }

    screen_config();
    ::abs_ScreenX=(displayInfo.height>displayInfo.width?displayInfo.height:displayInfo.width);
    ::abs_ScreenY=(displayInfo.height<displayInfo.width?displayInfo.height:displayInfo.width);
    ::native_window_screen_x=::abs_ScreenX;
    ::native_window_screen_y=::abs_ScreenX;

    if(!initGUI_draw(native_window_screen_x,native_window_screen_y,true)) return -1;
    Touch_Init(displayInfo.width,displayInfo.height,displayInfo.orientation,true);

    g_FloatingSystemInit=true; g_FloatingSystemReady=true;
    g_Landscape2SecDone=true;  g_IsLandscape=true;

    bool lrms=false,lpms=false;

    while(main_thread_flag){
        UpdatePerformanceMetrics();
        drawBegin();
        {
            // Anti-crash dummy window
            ImGui::SetNextWindowPos(ImVec2(-200,-200));
            ImGui::SetNextWindowSize(ImVec2(10,10));
            ImGui::Begin("##AC",nullptr,
                ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoBackground|
                ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|
                ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoSavedSettings);
            ImGui::End();

            // ── LANDSCAPE DEBOUNCE (BLINK FIX) ───────────────
            // screen_config() is polled ~71x/sec. A single stale or
            // zero-sized reading used to flip g_FloatingSystemReady
            // false, which blanked the ENTIRE overlay (menu + ESP)
            // and then required a full 2000ms to come back. One bad
            // read = one very visible blink.
            // Now portrait must be seen on several consecutive polls
            // before we tear anything down, and readings with bogus
            // dimensions are ignored outright.
            // screen_config() is now throttled (~4 Hz), so a poll count no
            // longer means a time span. Portrait must persist for PORTRAIT_CONFIRM_MS
            // before anything is torn down.
            static std::chrono::steady_clock::time_point s_PortraitSince{};
            static bool                                  s_PortraitActive = false;
            static bool s_LastGoodLS     = true;
            const auto  PORTRAIT_CONFIRM_MS = std::chrono::milliseconds(1500);

            bool rawLS = IsScreenLandscape();
            bool dimsValid = (displayInfo.width > 0 && displayInfo.height > 0);

            if(!dimsValid){
                rawLS = s_LastGoodLS;           // ignore garbage reading
            } else {
                s_LastGoodLS = rawLS;
            }

            const auto nowLS = chrono::steady_clock::now();
            if(rawLS){
                s_PortraitActive = false;
            } else if(!s_PortraitActive){
                s_PortraitActive = true;
                s_PortraitSince  = nowLS;
            }

            bool curLS = !(s_PortraitActive && (nowLS - s_PortraitSince) >= PORTRAIT_CONFIRM_MS);
            if(!curLS){
                if(g_FloatingSystemReady){
                    if(!lpms){lpms=true;lrms=false;}
                    g_FloatingSystemReady=false; g_Landscape2SecDone=false;
                    g_IsLandscape=false;         g_MenuVis=false;
                }
            } else {
                if(!g_FloatingSystemReady){
                    if(!g_LandscapeTimerActive){
                        g_LandscapeDetectTime=chrono::steady_clock::now();
                        g_LandscapeTimerActive=true; g_IsLandscape=true;
                    }
                    auto el=chrono::duration_cast<chrono::milliseconds>(
                        chrono::steady_clock::now()-g_LandscapeDetectTime).count();
                    if(el>=2000){
                        g_FloatingSystemReady=true; g_Landscape2SecDone=true;
                        g_LandscapeTimerActive=false; g_MenuVis=true;
                        lpms=false; lrms=true;
                    }
                }
            }

            if(!g_GameIsRunning){
                game_pid=getProcessID(packageName);
                if(game_pid>0){
                    lib_base =get_module_base(targetaLibName, PERM_EXEC);
                    lib_base2=get_module_base(targetaLibName2,PERM_EXEC);
                    g_GameIsRunning=true;
                    if(g_driver && g_AuthKernelOk){
                        g_driver->initialize((pid_t)game_pid);
                        g_driver->initKey("InvisibleX");
                    }
                }
            }

            if(g_FloatingSystemReady){
                RenderMenu();
                if((g_GameIsRunning||g_EspAlive.load())&&lib_base!=0&&g_MasterEnabled&&ix_rt_ready()){
                    g_ShowFovOverlay=g_Cfg.showFovCircle && ix_feat_aim();
                    DrawAll(abs_ScreenX,abs_ScreenY,lib_base);
                }
            }
        }
        drawEnd();

        // Frame cap: 60 fps while the menu is open, 30 fps otherwise. The
        // full-rate ImGui + EGL swap loop running all the time was the main
        // source of heat while playing.
        static auto lft=chrono::steady_clock::now();
        const long long frameUs = g_MenuVis ? 16000LL : 33000LL;
        auto fd=chrono::duration_cast<chrono::microseconds>(
            chrono::steady_clock::now()-lft).count();
        if(fd<frameUs) usleep((useconds_t)(frameUs-fd));
        lft=chrono::steady_clock::now();
    }

    StopEspThread();
    StopSilentAimThread();
    // Body Kill has no separate thread; StopSilentAimThread above covers it.
    StopAimbotThread();
    UpdateAndSaveSettings();
    shutdown(); Touch_Close();
    // [CRASH FIX - SIGBUS on teardown] Same race as inx_login.cpp: the
    // integrity watchdog in login.h reads this binary's own file-backed
    // .rodata every tick, so returning from main() and letting the
    // runtime unmap underneath it can fault on a dead page (SIGBUS).
    // Park the watchdog, flush, and leave via _exit() so no userspace
    // teardown runs at all. The other exit at the top of this file
    // already used _exit() and was never affected.
    kd_stop_integrity_watchdog();
    fflush(stdout);
    fflush(stderr);
    _exit(0);
}