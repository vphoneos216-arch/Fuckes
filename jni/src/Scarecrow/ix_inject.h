#ifndef IX_INJECT_H
#define IX_INJECT_H

// =====================================================================
// Ptrace-based remote shared-library injector (ARM64 only — this
// project's only target ABI). Used for exactly one thing right now:
// loading collider_stub.so into the game process and calling its
// ix_collider_stub_init() export, so the collider aim-lock HWBP
// arm/disarm can run from inside the game's own PID per request.
//
// Technique: classic "call dlopen, then dlsym, then the loaded
// function, using return-address-zero as the stop signal" ptrace
// injection. Standard, widely-used pattern for Android/ARM64 — not
// improvised for this file — but genuinely fragile by nature (depends
// on matching libdl.so/libc.so layout between this process and the
// target, on the target not already being ptrace-attached by
// something else such as an anti-cheat, and on no SELinux denial on
// ptrace(PTRACE_ATTACH) against that target). Not built or tested
// in-game — delivered as asked, not reported working.
// =====================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <fstream>
#include <sstream>
#include <dlfcn.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/elf.h>
#include <sys/user.h>
#include <cerrno>

#ifndef SYS_process_vm_writev
#define SYS_process_vm_writev 271
#endif
#ifndef NT_PRSTATUS
#define NT_PRSTATUS 1
#endif

namespace ix_inject_detail {

inline uintptr_t FindModuleBase(pid_t pid, const char* moduleName) {
    char mapsPath[64];
    snprintf(mapsPath, sizeof(mapsPath), "/proc/%d/maps", pid);
    std::ifstream maps(mapsPath);
    if (!maps.is_open()) return 0;

    std::string line;
    while (std::getline(maps, line)) {
        if (line.find(moduleName) != std::string::npos) {
            uintptr_t base = 0;
            std::istringstream iss(line);
            std::string addrRange;
            iss >> addrRange;
            size_t dash = addrRange.find('-');
            if (dash == std::string::npos) continue;
            base = (uintptr_t)strtoull(addrRange.substr(0, dash).c_str(), nullptr, 16);
            return base;
        }
    }
    return 0;
}

inline bool WaitForStop(pid_t pid) {
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFSTOPPED(status);
}

// Writes `len` bytes to the target's memory at `remoteAddr`. Uses
// process_vm_writev — same mechanism this codebase already uses
// elsewhere (Scarecrow/bool.h) for external memory writes — rather
// than ptrace POKEDATA word loops.
inline bool WriteRemote(pid_t pid, uintptr_t remoteAddr, const void* data, size_t len) {
    struct iovec local{ (void*)data, len };
    struct iovec remote{ (void*)remoteAddr, len };
    ssize_t n = syscall(SYS_process_vm_writev, pid, &local, 1, &remote, 1, 0);
    return n == (ssize_t)len;
}

#if defined(__aarch64__)
inline bool GetRegs(pid_t pid, struct user_pt_regs* regs) {
    struct iovec iov{ regs, sizeof(*regs) };
    return ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &iov) == 0;
}
inline bool SetRegs(pid_t pid, struct user_pt_regs* regs) {
    struct iovec iov{ regs, sizeof(*regs) };
    return ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, &iov) == 0;
}

// Makes the target call `funcAddr(args...)`, stopping it with LR
// forced to 0 so the function's own `ret` faults immediately and
// waitpid() gives us a deterministic "the call returned" signal. Up
// to 4 integer/pointer args, which is all dlopen/dlsym/our init
// function ever need.
inline bool RemoteCall(pid_t pid, uintptr_t funcAddr, uint64_t a0, uint64_t a1,
                        uint64_t a2, uint64_t a3, uint64_t* outRet) {
    struct user_pt_regs regs{};
    if (!GetRegs(pid, &regs)) return false;
    struct user_pt_regs saved = regs;

    regs.regs[0] = a0;
    regs.regs[1] = a1;
    regs.regs[2] = a2;
    regs.regs[3] = a3;
    regs.regs[30] = 0;         // LR = 0 -> the "return" trap
    regs.sp = saved.sp & ~0xFULL; // 16-byte align, keep original stack
    regs.pc = funcAddr;

    if (!SetRegs(pid, &regs)) return false;
    if (ptrace(PTRACE_CONT, pid, nullptr, 0) != 0) return false;
    if (!WaitForStop(pid)) return false;

    struct user_pt_regs after{};
    if (!GetRegs(pid, &after)) return false;

    if (outRet) *outRet = after.regs[0];

    // Restore exactly what was there before this call, whatever the
    // function did to the rest of its own stack frame below sp — we
    // never touched below sp ourselves.
    return SetRegs(pid, &saved);
}
#endif // __aarch64__

} // namespace ix_inject_detail

// Injects `soPath` into `targetPid` and calls the exported, no-argument
// C function named `entrySymbol` inside it. Returns false on ANY
// failure — caller must treat that as "stub not loaded, fall back to
// the direct in-process path", never crash or assume success.
inline bool ix_inject_shared_library(pid_t targetPid, const std::string& soPath,
                                      const char* entrySymbol) {
#if !defined(__aarch64__)
    (void)targetPid; (void)soPath; (void)entrySymbol;
    printf("[INX] collider-stub injection only implemented for arm64\n");
    return false;
#else
    using namespace ix_inject_detail;

    if (ptrace(PTRACE_ATTACH, targetPid, nullptr, 0) != 0) {
        printf("[INX] inject: PTRACE_ATTACH failed (errno=%d)\n", errno);
        return false;
    }
    if (!WaitForStop(targetPid)) {
        printf("[INX] inject: target did not stop after PTRACE_ATTACH\n");
        ptrace(PTRACE_DETACH, targetPid, nullptr, 0);
        return false;
    }

    bool ok = false;
    do {
        // Resolve dlopen/dlsym locally, then translate to the target's
        // address space via the module-base offset (same system libdl.so
        // build on both sides — same device, same OS image).
        void* localDlopen = (void*)dlopen;
        void* localDlsym  = (void*)dlsym;

        Dl_info info{};
        if (!dladdr(localDlopen, &info) || !info.dli_fname) {
            printf("[INX] inject: dladdr(dlopen) failed\n");
            break;
        }
        std::string libName = info.dli_fname;
        size_t slash = libName.find_last_of('/');
        if (slash != std::string::npos) libName = libName.substr(slash + 1);

        uintptr_t localBase  = FindModuleBase(getpid(), libName.c_str());
        uintptr_t remoteBase = FindModuleBase(targetPid, libName.c_str());
        if (!localBase || !remoteBase) {
            printf("[INX] inject: could not find %s base in self (%zu) or target (%zu)\n",
                   libName.c_str(), (size_t)localBase, (size_t)remoteBase);
            break;
        }

        uintptr_t remoteDlopen = remoteBase + ((uintptr_t)localDlopen - localBase);
        uintptr_t remoteDlsym  = remoteBase + ((uintptr_t)localDlsym  - localBase);

        // Scratch space below the target thread's current stack pointer
        // for the two strings we need to pass by pointer (so path, then
        // the entry symbol name).
        struct user_pt_regs regs0{};
        if (!GetRegs(targetPid, &regs0)) { printf("[INX] inject: GETREGSET failed\n"); break; }

        uintptr_t scratch = (regs0.sp - 1024) & ~0xFULL;
        if (!WriteRemote(targetPid, scratch, soPath.c_str(), soPath.size() + 1)) {
            printf("[INX] inject: failed writing .so path into target\n");
            break;
        }

        uint64_t handle = 0;
        // dlopen(path, RTLD_NOW)
        if (!RemoteCall(targetPid, remoteDlopen, scratch, RTLD_NOW, 0, 0, &handle) || !handle) {
            printf("[INX] inject: remote dlopen failed (handle=0x%llx)\n", (unsigned long long)handle);
            break;
        }

        uintptr_t symScratch = scratch + 512;
        if (!WriteRemote(targetPid, symScratch, entrySymbol, strlen(entrySymbol) + 1)) {
            printf("[INX] inject: failed writing symbol name into target\n");
            break;
        }

        uint64_t symAddr = 0;
        // dlsym(handle, entrySymbol)
        if (!RemoteCall(targetPid, remoteDlsym, handle, symScratch, 0, 0, &symAddr) || !symAddr) {
            printf("[INX] inject: remote dlsym(\"%s\") failed\n", entrySymbol);
            break;
        }

        uint64_t unused = 0;
        if (!RemoteCall(targetPid, (uintptr_t)symAddr, 0, 0, 0, 0, &unused)) {
            printf("[INX] inject: remote call to %s failed\n", entrySymbol);
            break;
        }

        ok = true;
    } while (false);

    ptrace(PTRACE_DETACH, targetPid, nullptr, 0);
    return ok;
#endif
}

#endif // IX_INJECT_H
