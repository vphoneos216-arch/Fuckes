// Standalone root-run executable: ptrace-based remote dlopen() injector for
// arm64 Android. Built as its own BUILD_EXECUTABLE module (see Android.mk),
// deliberately separate from libmain.so's FILE_LIST so it doesn't get
// swept into the module build.
//
// Usage (run as root, e.g. `su -c`):
//   injector <pid> <absolute path to .so>
//   injector <process name> <absolute path to .so>   (resolves pid via /proc)
//
// What it does, honestly: attaches to the target process with ptrace,
// finds dlopen's real address by computing dlopen's offset inside OUR OWN
// libc.so (via /proc/self/maps + dlsym) and adding that same offset to the
// target's libc.so base (via /proc/<pid>/maps) — this works because ASLR
// only shifts a module's base address, not offsets within it. It then
// writes the .so path onto the target's stack, points the target's PC at
// dlopen with x0/x1 set as its arguments, lets it run until it traps, and
// reads the returned handle out of x0.
//
// This is standard, well-documented Android ptrace-injection technique
// (not game-specific data — nothing here is guessed about Free Fire
// itself). What's NOT guaranteed, and I'm not going to pretend otherwise:
//   - dlopen() can still fail inside the target (returns NULL / x0 == 0)
//     if SELinux denies that process from executing/mapping the given
//     path. That's a device policy matter, not a bug in this tool — see
//     the "SELINUX NOTE" section below for what to try if it fails.
//   - This has been compiled on CI (confirmed it builds and links for
//     arm64-v8a) but NOT run against a live device by me — I have no
//     device here to test on. Pull /sdcard/gsmodz_debug.log after a run;
//     this tool appends its own step-by-step result lines to it too.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstdarg>
#include <unistd.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <time.h>

// NT_PRSTATUS is normally pulled in via <elf.h>/<linux/elf.h>, but pulling
// those in on top of <sys/user.h> on the NDK's bionic headers risks
// conflicting redefinitions of the same register structs. It's a fixed,
// well-known kernel constant, so define it directly instead of fighting
// header order for one integer.
#ifndef NT_PRSTATUS
#define NT_PRSTATUS 1
#endif

static const char *LOG_PATH = "/sdcard/gsmodz_debug.log";

static void logmsg(const char *level, const char *fmt, ...) {
    char msg[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    char line[640];
    time_t now = time(nullptr);
    struct tm tmNow;
    localtime_r(&now, &tmNow);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmNow);
    snprintf(line, sizeof(line), "[%s] [%s] [INJECTOR] %s\n", tbuf, level, msg);

    fprintf(stderr, "%s", line);
    FILE *f = fopen(LOG_PATH, "a");
    if (f) {
        fputs(line, f);
        fclose(f);
    }
}

// Finds a process's pid by exact process name (comm), via /proc/<pid>/cmdline.
static pid_t find_pid_by_name(const char *name) {
    DIR *d = opendir("/proc");
    if (!d) return -1;
    struct dirent *entry;
    pid_t found = -1;
    while ((entry = readdir(d)) != nullptr) {
        if (entry->d_type != DT_DIR) continue;
        bool numeric = true;
        for (const char *p = entry->d_name; *p; ++p) {
            if (*p < '0' || *p > '9') { numeric = false; break; }
        }
        if (!numeric) continue;

        char cmdlinePath[64];
        snprintf(cmdlinePath, sizeof(cmdlinePath), "/proc/%s/cmdline", entry->d_name);
        FILE *f = fopen(cmdlinePath, "r");
        if (!f) continue;
        char buf[256] = {0};
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        if (n == 0) continue;
        buf[n] = 0;
        // cmdline is NUL-separated; the process name is the first token.
        if (strcmp(buf, name) == 0) {
            found = (pid_t) atoi(entry->d_name);
            break;
        }
    }
    closedir(d);
    return found;
}

// Finds the base address of a loaded module (matched by substring, e.g.
// "/libc.so") in the given pid's memory map. Returns 0 if not found.
static uintptr_t find_module_base(pid_t pid, const char *moduleSubstr) {
    char mapsPath[64];
    snprintf(mapsPath, sizeof(mapsPath), "/proc/%d/maps", pid);
    FILE *f = fopen(mapsPath, "r");
    if (!f) return 0;

    char line[512];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, moduleSubstr)) {
            unsigned long start = 0;
            sscanf(line, "%lx-", &start);
            base = (uintptr_t) start;
            break;
        }
    }
    fclose(f);
    return base;
}

static bool ptrace_getregs(pid_t pid, struct user_pt_regs *regs) {
    struct iovec iov;
    iov.iov_base = regs;
    iov.iov_len = sizeof(*regs);
    return ptrace(PTRACE_GETREGSET, pid, (void *) NT_PRSTATUS, &iov) == 0;
}

static bool ptrace_setregs(pid_t pid, struct user_pt_regs *regs) {
    struct iovec iov;
    iov.iov_base = regs;
    iov.iov_len = sizeof(*regs);
    return ptrace(PTRACE_SETREGSET, pid, (void *) NT_PRSTATUS, &iov) == 0;
}

// Writes `len` bytes into the target's memory at `addr`, word (8-byte) at a
// time via PTRACE_POKEDATA. Pads the final partial word with zero bytes.
static bool ptrace_write(pid_t pid, uintptr_t addr, const void *data, size_t len) {
    size_t words = (len + sizeof(long) - 1) / sizeof(long);
    for (size_t i = 0; i < words; i++) {
        long word = 0;
        size_t remain = len - i * sizeof(long);
        size_t chunk = remain < sizeof(long) ? remain : sizeof(long);
        memcpy(&word, (const uint8_t *) data + i * sizeof(long), chunk);
        errno = 0;
        if (ptrace(PTRACE_POKEDATA, pid, (void *) (addr + i * sizeof(long)), (void *) word) == -1 && errno != 0) {
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: injector <pid|process-name> <absolute path to .so>\n");
        return 1;
    }

    if (geteuid() != 0) {
        logmsg("ERROR", "not running as root (euid=%d) — run via `su -c`", geteuid());
        return 1;
    }

    pid_t targetPid;
    char *endp;
    long parsedPid = strtol(argv[1], &endp, 10);
    if (*endp == '\0' && parsedPid > 0) {
        targetPid = (pid_t) parsedPid;
    } else {
        targetPid = find_pid_by_name(argv[1]);
        if (targetPid <= 0) {
            logmsg("ERROR", "process '%s' not found (is the game running?)", argv[1]);
            return 1;
        }
    }

    const char *soPath = argv[2];
    if (access(soPath, F_OK) != 0) {
        logmsg("ERROR", "library path does not exist: %s", soPath);
        return 1;
    }

    logmsg("INFO", "target pid=%d, library=%s", targetPid, soPath);

    // Resolve dlopen's offset inside OUR OWN libc.so.
    void *localDlopen = (void *) dlopen; // real dlopen in our own process
    uintptr_t localLibcBase = find_module_base(getpid(), "/libc.so");
    if (!localLibcBase) {
        logmsg("ERROR", "could not find local libc.so base — unexpected on this device");
        return 1;
    }
    uintptr_t dlopenOffset = (uintptr_t) localDlopen - localLibcBase;
    logmsg("INFO", "local libc base=0x%lx, dlopen offset=0x%lx", (unsigned long) localLibcBase, (unsigned long) dlopenOffset);

    if (ptrace(PTRACE_ATTACH, targetPid, nullptr, nullptr) != 0) {
        logmsg("ERROR", "PTRACE_ATTACH failed on pid %d: %s", targetPid, strerror(errno));
        return 1;
    }
    int status;
    waitpid(targetPid, &status, 0);

    uintptr_t remoteLibcBase = find_module_base(targetPid, "/libc.so");
    if (!remoteLibcBase) {
        logmsg("ERROR", "could not find libc.so in target process maps");
        ptrace(PTRACE_DETACH, targetPid, nullptr, nullptr);
        return 1;
    }
    uintptr_t remoteDlopen = remoteLibcBase + dlopenOffset;
    logmsg("INFO", "remote libc base=0x%lx, remote dlopen=0x%lx", (unsigned long) remoteLibcBase, (unsigned long) remoteDlopen);

    struct user_pt_regs regsBackup{};
    if (!ptrace_getregs(targetPid, &regsBackup)) {
        logmsg("ERROR", "PTRACE_GETREGSET failed: %s", strerror(errno));
        ptrace(PTRACE_DETACH, targetPid, nullptr, nullptr);
        return 1;
    }
    struct user_pt_regs regs = regsBackup;

    // Reserve stack space for the path string, 16-byte aligned, well below
    // the current SP with a red-zone gap.
    size_t pathLen = strlen(soPath) + 1;
    uintptr_t stackTop = regs.sp - 1024;
    uintptr_t pathAddr = (stackTop - pathLen) & ~0xFUL;

    if (!ptrace_write(targetPid, pathAddr, soPath, pathLen)) {
        logmsg("ERROR", "failed writing library path into target memory");
        ptrace(PTRACE_DETACH, targetPid, nullptr, nullptr);
        return 1;
    }

    // arm64 AAPCS64: x0 = filename, x1 = flags (RTLD_NOW = 2).
    regs.regs[0] = pathAddr;
    regs.regs[1] = 2; // RTLD_NOW
    regs.sp = pathAddr & ~0xFUL;
    regs.pc = remoteDlopen;
    // Set LR to 0 — dlopen() returning there faults, which is how we detect
    // completion (classic ptrace-injection trick: catch the fault instead
    // of needing a real return trampoline).
    regs.regs[30] = 0;

    if (!ptrace_setregs(targetPid, &regs)) {
        logmsg("ERROR", "PTRACE_SETREGSET failed: %s", strerror(errno));
        ptrace(PTRACE_DETACH, targetPid, nullptr, nullptr);
        return 1;
    }

    if (ptrace(PTRACE_CONT, targetPid, nullptr, nullptr) != 0) {
        logmsg("ERROR", "PTRACE_CONT failed: %s", strerror(errno));
        ptrace(PTRACE_DETACH, targetPid, nullptr, nullptr);
        return 1;
    }

    waitpid(targetPid, &status, 0);
    if (!WIFSTOPPED(status)) {
        logmsg("ERROR", "target did not stop as expected after dlopen call (status=0x%x) — "
                         "it may have crashed instead of faulting cleanly at LR=0", status);
        ptrace(PTRACE_DETACH, targetPid, nullptr, nullptr);
        return 1;
    }

    struct user_pt_regs afterRegs{};
    if (!ptrace_getregs(targetPid, &afterRegs)) {
        logmsg("ERROR", "PTRACE_GETREGSET (after call) failed: %s", strerror(errno));
        ptrace(PTRACE_DETACH, targetPid, nullptr, nullptr);
        return 1;
    }
    uintptr_t handle = afterRegs.regs[0];

    // Restore the target's original registers so its execution resumes
    // exactly where it was, unaffected.
    ptrace_setregs(targetPid, &regsBackup);
    ptrace(PTRACE_DETACH, targetPid, nullptr, nullptr);

    if (handle == 0) {
        logmsg("ERROR", "dlopen() returned NULL inside target process — library did not load. "
                         "SELINUX NOTE: this usually means the target process's SELinux domain "
                         "isn't allowed to open/execute a file at that path. Try placing the .so "
                         "somewhere already labeled for that app (its own data dir) or run "
                         "`setenforce 0` as root first if your device/kernel allows it.");
        return 1;
    }

    logmsg("INFO", "dlopen() succeeded in target process, handle=0x%lx — injection complete", (unsigned long) handle);
    printf("OK handle=0x%lx\n", (unsigned long) handle);
    return 0;
}
