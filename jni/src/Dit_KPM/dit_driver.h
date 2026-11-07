#pragma once
// Dit_KPM/dit_driver.h — wraps the uapi.h command ABI.
//
// One physical kernel node, two possible wire protocols:
//   KDitPro ("ditpro_kpm") → syscall(__NR_syscall_, cmd, ...)
//   KDits   ("dits_ko")    → ioctl(fd, cmd, ...)
// FindAllDevice() probes VERSION once; bit 63 of the returned value
// says which protocol is live, and every later call routes through
// Call() to the matching one automatically — the caller never needs
// to know or choose which.
//
// This is a SEPARATE driver ABI from the project's existing option 7
// (proKpm in wanbai.h, a different flag-based syscall interface) —
// nothing in wanbai.h's proKpm/KMA/LDK/Paradise classes is touched or
// reused here.

#include "uapi.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include <utility>

enum class DitKpmSubtype : int {
    KNull   = -1,
    KDitPro = 0,   // syscall ABI — node name "ditpro_kpm"
    KDits   = 1,   // ioctl ABI   — node name "dits_ko"
};

class DitKpmDriver {
public:
    pid_t     pid_    { -1 };
    uintptr_t sohead  { 0  };

    DitKpmSubtype subtype() const { return subtype_; }
    const char   *subtype_name() const {
        return subtype_ == DitKpmSubtype::KDits ? "dits_ko" :
               subtype_ == DitKpmSubtype::KDitPro ? "ditpro_kpm" : "none";
    }

    // ── Probe + attach to a process/module in one call ──────────
    bool Init(const char *packageName, const char *moduleName) {
        if (!StartDrivers()) return false;

        auto pid_opt = GetPid(packageName);
        if (!pid_opt) {
            printf("[Dit-KPM] Process not found: %s\n", packageName);
            return false;
        }
        pid_ = *pid_opt;

        auto base_opt = GetBase(pid_, moduleName);
        if (!base_opt) {
            printf("[Dit-KPM] Module not found: %s\n", moduleName);
            return false;
        }
        sohead = *base_opt;
        printf("[Dit-KPM] (%s) pid=%d  %s base=0x%lx\n",
               subtype_name(), (int)pid_, moduleName, (unsigned long)sohead);
        return true;
    }

    // Probe only — tells you whether either sub-node is loaded,
    // without needing a target package yet.
    bool StartDrivers() {
        auto [stype, fd, ver] = FindAllDevice();
        if (stype == DitKpmSubtype::KNull) {
            printf("[Dit-KPM] Neither ditpro_kpm nor dits_ko is loaded\n");
            return false;
        }
        printf("[Dit-KPM] Hello %s  version=%u\n",
               (stype == DitKpmSubtype::KDits ? "dits_ko" : "ditpro_kpm"), ver);
        subtype_ = stype;
        fd_      = fd;
        return true;
    }

    void InitPid(pid_t pid) { pid_ = pid; }

    // ── Memory ops ───────────────────────────────────────────────
    long Read(uintptr_t address, void *buffer, size_t size) {
        AccessPvmStruct cm{ pid_, address, buffer, size, (no_cache_ ? 1UL << 1 : 0) };
        return Call(KCmd(SyscallCmd::MEM_READ), &cm);
    }

    long ReadChain(std::initializer_list<uint64_t> offset, void *buffer, size_t size,
                   uintptr_t *ret_addr = nullptr) {
        uintptr_t tmp{};
        uint64_t flags = no_cache_ ? 1UL << 1 : 0;
        AccessPvmcStruct cm{ pid_, (ret_addr ? ret_addr : &tmp), offset, buffer, size, flags };
        return Call(KCmd(SyscallCmd::MEM_READ_CHAIN), &cm);
    }

    long ReadHeadArray(std::initializer_list<uint64_t> offset, unsigned int array_count,
                       uintptr_t array_addr, size_t object_single_size, void *buffer) {
        uint64_t flags = no_cache_ ? 1UL << 1 : 0;
        AccessPvmArrObjStruct cm{ pid_, array_count, array_addr, offset, object_single_size, buffer, flags };
        return Call(KCmd(SyscallCmd::MEM_READ_ARROBJ), &cm);
    }

    template<typename T> T Read(uintptr_t address) {
        T res{}; Read(address, &res, sizeof(T)); return res;
    }
    template<typename T> T Read(std::initializer_list<unsigned long> offset) {
        T res{}; ReadChain(offset, &res, sizeof(T)); return res;
    }
    template<typename T> long Write(uintptr_t address, T value) {
        AccessPvmStruct cm{ pid_, address, &value, sizeof(T), 0 };
        return Call(KCmd(SyscallCmd::MEM_WRITE), &cm);
    }
    // Raw write of an arbitrary-size buffer (non-templated callers)
    long WriteRaw(uintptr_t address, void *buffer, size_t size) {
        AccessPvmStruct cm{ pid_, address, buffer, size, 0 };
        return Call(KCmd(SyscallCmd::MEM_WRITE), &cm);
    }

    // ── Process / module ─────────────────────────────────────────
    [[nodiscard]] std::optional<pid_t> GetPid(std::string_view name) {
        auto r = Call(KCmd(SyscallCmd::PROC_PID), name.data());
        if (r > 2) return (pid_t)r;
        return std::nullopt;
    }
    [[nodiscard]] std::optional<uintptr_t> GetBase(pid_t pid, std::string_view name) {
        AccessModuleStruct cm{ pid, name.data(), 0, 0 };
        if (Call(KCmd(SyscallCmd::PROC_MODULE), &cm) == 0) return cm.addr_start;
        return std::nullopt;
    }
    [[nodiscard]] std::optional<std::pair<uintptr_t, uintptr_t>>
    GetBaseRange(pid_t pid, std::string_view name) {
        AccessModuleStruct cm{ pid, name.data(), 0, 0 };
        if (Call(KCmd(SyscallCmd::PROC_MODULE), &cm) == 0)
            return std::pair{ cm.addr_start, cm.addr_end };
        return std::nullopt;
    }

    // ── Hide / unhide (this process only, same as upstream sample) ──
    int HideProc()   { return (int)Call(KCmd(SyscallCmd::PROC_PID_HIDE), (long)getpid()); }
    void UnHideProc() { Call(KCmd(SyscallCmd::PROC_PID_UNHIDE)); }

    // ── Generic dispatch — routes by detected subtype ────────────
    template<typename... Args>
    long Call(Args &&...args) {
        switch (subtype_) {
            case DitKpmSubtype::KDitPro:
                return syscall(__NR_syscall_, std::forward<decltype(args)>(args)...);
            case DitKpmSubtype::KDits:
                return ioctl(fd_, std::forward<decltype(args)>(args)...);
            default:
                return -1;
        }
    }

    ~DitKpmDriver() {
        if (fd_ > 0) { close(fd_); fd_ = -1; }
    }

private:
    bool          no_cache_ = true;
    DitKpmSubtype subtype_{ DitKpmSubtype::KNull };
    int           fd_      { -1 };

    std::tuple<DitKpmSubtype, int, uint32_t> FindAllDevice() {
        std::tuple<DitKpmSubtype, int, unsigned int> msg{ DitKpmSubtype::KNull, -1, 0 };
        unsigned long version_type = 0;
        if (syscall(__NR_syscall_, KCmd(SyscallCmd::VERSION), &version_type) != 0)
            return msg;
        // Low bits = version, bit 63 set => dits_ko (ioctl), clear => ditpro_kpm (syscall)
        std::get<0>(msg) = ((version_type >> 63UL) & 1UL)
                          ? DitKpmSubtype::KDits : DitKpmSubtype::KDitPro;
        std::get<1>(msg) = (int)syscall(__NR_syscall_, KCmd(SyscallCmd::LINK));
        std::get<2>(msg) = static_cast<uint32_t>(version_type);
        return msg;
    }
};

// ── Hardware breakpoint helper (Dit-KPM only, optional use) ───────
class DitKpmHwbp {
public:
    explicit DitKpmHwbp(DitKpmDriver &d) : driver_(d) { memset(&hbq_, 0, sizeof hbq_); }

    DitKpmHwbp &Install(pid_t tid, virt_addr_t address) {
        cmd_ = KCmd(SyscallCmd::HWBP_INSTALL); hbr_.tid = (uint32_t)tid; hbr_.address = address; return *this;
    }
    DitKpmHwbp &Remove(pid_t tid, virt_addr_t address) {
        cmd_ = KCmd(SyscallCmd::HWBP_UNINSTALL); hbr_.tid = (uint32_t)tid; hbr_.address = address; return *this;
    }
    DitKpmHwbp &Enable(pid_t tid, virt_addr_t address) {
        cmd_ = KCmd(SyscallCmd::HWBP_ENABLE); hbr_.tid = (uint32_t)tid; hbr_.address = address; return *this;
    }
    DitKpmHwbp &SetTypeLen(uint32_t bp_type, uint32_t bp_len) { hbr_.bp_type = bp_type; hbr_.bp_len = bp_len; return *this; }
    DitKpmHwbp &OnlyRegister() { hbr_.flags = HWBP_FLAG_ONLY_REGISTER; return *this; }
    DitKpmHwbp &CaptureFp()    { hbr_.flags |= HWBP_FLAG_CAPTURE_FP;   return *this; }
    DitKpmHwbp &Override(pid_t tid, virt_addr_t address) {
        cmd_ = KCmd(SyscallCmd::HWBP_OVERRIDE); hbo_.tid = (uint32_t)tid; hbo_.address = address; return *this;
    }
    DitKpmHwbp &SetReg(int i, uint64_t v)  { hbo_.reg_mask |= (1u << i); hbo_.regs[i] = v; return *this; }
    DitKpmHwbp &SetFpReg(int i, float v) {
        uint32_t u; memcpy(&u, &v, sizeof u);
        hbo_.fp_mask |= (1u << i); hbo_.fp.lo[i] = u; return *this;
    }
    DitKpmHwbp &Query(pid_t tid, virt_addr_t address) {
        cmd_ = KCmd(SyscallCmd::HWBP_QUERY); hbq_.tid = (uint32_t)tid; hbq_.address = address; return *this;
    }
    long Update() {
        if (cmd_ == KCmd(SyscallCmd::HWBP_OVERRIDE)) { HwbpOverrideStruct c = hbo_; return driver_.Call(cmd_, &c); }
        if (cmd_ == KCmd(SyscallCmd::HWBP_INSTALL))  { HwbpRequestStruct  c = hbr_; return driver_.Call(cmd_, &c); }
        HwbpQueryStruct c = hbq_; return driver_.Call(cmd_, &c);
    }
    static std::vector<pid_t> GetAllTids(pid_t pid) {
        std::vector<pid_t> tids;
        char path[64]; snprintf(path, sizeof(path), "/proc/%d/task", (int)pid);
        DIR *d = opendir(path);
        if (!d) return tids;
        struct dirent *e;
        while ((e = readdir(d)) != nullptr) {
            if (e->d_name[0] == '.') continue;
            tids.push_back((pid_t)atoi(e->d_name));
        }
        closedir(d);
        return tids;
    }

private:
    DitKpmDriver       &driver_;
    HwbpRequestStruct   hbr_{};
    HwbpOverrideStruct  hbo_{};
    HwbpQueryStruct     hbq_{};
    uint32_t            cmd_{ 0 };
};
