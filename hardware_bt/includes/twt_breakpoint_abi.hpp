#pragma once
#include <cstdint>
#include <cstddef>
#include <sys/ioctl.h>

// Wire layout from DRIV/twt/twt.h. Never include its global c_driver or macros.
namespace twt_bp {
struct Request {int32_t pid;uintptr_t addr;void* buffer;size_t size;};
static_assert(sizeof(Request)==32 && offsetof(Request,addr)==8 && offsetof(Request,buffer)==16 && offsetof(Request,size)==24);
#pragma pack(push, 1)
struct Registers {
    uint64_t x[31], sp, pc, pstate, orig_x0, syscallno;
    __uint128_t v[32];
};
struct Hit { uint64_t tid, address, time; Registers regs; };
struct Counts { uint64_t handle, total, retained; };
struct Items { uint64_t handle, buffer, max_bytes, copied; };
struct Install {
    int32_t pid;
    uint64_t address;
    uint32_t length, type;
    uint64_t gp_mask, fp_mask, registers;
    uint32_t flags;
};
struct Modify { uint64_t handle, gp_mask, fp_mask, registers; };
#pragma pack(pop)
constexpr unsigned long init = _IO('T',19), check = _IO('T',30);
constexpr unsigned long num_brps = _IO('T',20), num_wrps = _IO('T',21);
constexpr unsigned long install = _IOWR('T',22,char*), remove = _IOW('T',23,char*);
constexpr unsigned long counts = _IOWR('T',26,char*), modify = _IOW('T',27,char*);
constexpr unsigned long items = _IOWR('T',28,char*);
static_assert(sizeof(void*)==8, "TWT ABI requires a 64-bit client");
static_assert(sizeof(Registers)==800 && offsetof(Registers,v)==288);
static_assert(sizeof(Hit)==824 && sizeof(Install)==48 && offsetof(Install,address)==4);
static_assert(sizeof(Modify)==32 && sizeof(Counts)==24 && sizeof(Items)==32);
}
