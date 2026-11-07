#ifndef _UAPI_H_
#define _UAPI_H_

#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <cstring>
#include <type_traits>

#define __NR_syscall_ 18
/* 魔数 */
#define __Syscall_MAGIC   0x61618

typedef std::uintptr_t virt_addr_t;
typedef std::uintptr_t phys_addr_t;
#include <sys/types.h>   /* already defines pid_t as int on Android/Linux — don't redefine */
typedef std::int64_t offset_t;

/* ditpro 是 syscall，dits 是 ioctl */
enum class SyscallCmd : std::uint32_t {
    LINK = 0x10, /* 获取版本的时候建议获取一下这个，进程退出可以自动释放 */
    VERSION = 0x11, /* 获取版本 */
    MEM_READ = 0x100, /* 读 */
    MEM_READ_CHAIN = 0x110, /* 读链条 */
    MEM_READ_ARROBJ = 0x120, /* 读世界列表对象 */
    MEM_WRITE = 0x200, /* 写 */
    PROC_PID = 0x300, /* 获取进程id */
    PROC_MODULE = 0x400, /* 获取模块 */
    PROC_MAPS = 0x500, /* 获取内存区域 */
    PROC_PID_HIDE = 0x600, /* 隐藏进程 */
    PROC_PID_UNHIDE = 0x601,
    DIRENTRY_HIDE = 0x700, /* 隐藏目录 */
    DIRENTRY_UNHIDE = 0x701,
    
    GYRO_INIT = 0x1100, /* 陀螺仪初始化 */
    GYRO_RUN = 0x1101, /* 开始 */
    GYRO_DESTROY = 0x1103, /* 释放 */
    HWBP_INSTALL = 0x2001,
    HWBP_UNINSTALL = 0x2002,
    HWBP_ENABLE = 0x2003, /* 对已安装的断点重新生效 */
    HWBP_DISABLE = 0x2004,
    HWBP_QUERY = 0x2005,
    HWBP_OVERRIDE = 0x2006,
    INPUT_FD = 0x10000, /* 获取真实触摸交互标识符 */
};

/* 不管是ditpro还是dits，这些命令都是ioctl交互 */
enum class InputIoctlCmd : std::uint32_t  {
    INPUT_READ_ABORT = 0x1, //睡觉时叫起来，这个除外，这个是read
    INPUT_INIT = 0x100,
    INPUT_RELEASE = 0x101,
    INPUT_DESTROY = 0x200,
    INPUT_DOWN = 0X300,
    INPUT_MOVE = 0x301,
    INPUT_UP = 0x302,
    
    INPUT_TARGET_DISABLE = 0x400,
    INPUT_TARGET_ENABLE = 0X401,
    INPUT_TARGET_COORD_SET = 0x402,
    INPUT_TARGET_COORD_RESET = 0x403,
};

constexpr std::uint32_t operator+(std::uint32_t lsh, SyscallCmd rsh) {
    return lsh + static_cast<std::uint32_t>(rsh);
}  
constexpr std::uint32_t operator+(std::uint32_t lsh, InputIoctlCmd rsh) {
    return lsh + static_cast<std::uint32_t>(rsh);
}

template<typename T, std::enable_if_t< std::is_same_v<std::decay_t<T>, SyscallCmd> || std::is_same_v<std::decay_t<T>, InputIoctlCmd>, int> = 0>
constexpr std::uint32_t KCmd(T cmd) {
    return static_cast<std::uint32_t>(__Syscall_MAGIC + cmd);
}
  
/* 访问模块 */
struct AccessModuleStruct {
    pid_t pid;
    const char* name;
    virt_addr_t addr_start;
    virt_addr_t addr_end;
};

/* 访问内存区域 */
struct AccessMapsStruct {
    pid_t pid;
    std::uint32_t count;
    char *lists;
};

#define MEM_FLAG_NOCACHE  (1UL << 1)

/* 读取链条允许的最大深度 */
inline constexpr std::size_t KMemListMax = 10;
/* 读取世界允许的每个对象的最大深度 */
inline constexpr std::size_t KMemArrObjListMax = 3;

/* 单地址内存操作 */
struct AccessPvmStruct
{
    pid_t pid;
    virt_addr_t addr;
    void *buffer;
    std::size_t size;
    std::uint64_t flags; /* 非0无缓存，目前是 */
};

/* 链条内存操作 */
struct AccessPvmcStruct
{
    pid_t pid;
    virt_addr_t *ret_addr;
    offset_t offset[KMemListMax];
    void *buffer;
    std::size_t size;
    std::uint64_t flags; /* 非0无缓存，目前是 */

    AccessPvmcStruct(pid_t pid_, virt_addr_t *ret_addr_,
                     std::initializer_list<std::uint64_t> offset_,
                     void *buffer_, std::size_t size_, std::uint64_t flags_)
        : pid(pid_)
        , ret_addr(ret_addr_)
        , buffer(buffer_)
        , size(size_)
        , flags(flags_)
    {
        for (auto& o : offset) o = offset_t(-1);
        std::size_t i = 0;
        for (std::uint64_t val : offset_) {
            if (i >= KMemListMax) break;
            offset[i++] = val;
        }
    }
};

/* 世界对象 */
struct AccessPvmArrObjStruct
{
    pid_t pid;
    std::uint32_t array_count; // 数组数量
    virt_addr_t array_addr; // 数组地址
    offset_t offset[KMemArrObjListMax]; //偏移
    std::size_t object_single_size; //对象列表页大小
    void *buffer; //缓冲区，缓冲区大小为 array_count * object_single_size
    std::uint64_t flags;
    AccessPvmArrObjStruct(pid_t pid_, std::uint32_t array_count_,
                     virt_addr_t array_addr_, std::initializer_list<std::uint64_t> offset_,
                     std::size_t object_single_size_, void *buffer_, std::uint64_t flags_)
        : pid(pid_)
        , array_count(array_count_)
        , array_addr(array_addr_)
        , object_single_size(object_single_size_)
        , buffer(buffer_)
        , flags(flags_)
    {
        for (auto& o : offset) o = offset_t(-1);
        std::size_t i = 0;
        for (std::uint64_t val : offset_) {
            if (i >= KMemListMax) break;
            offset[i++] = val;
        }
    }
};

#define HWBP_FLAG_ONLY_REGISTER (1u << 0) // 仅注册
#define HWBP_FLAG_CAPTURE_FP    (1u << 9) // 捕获浮点
#define SH_GPR_COUNT            31
#define SH_FP_VREGS             32

#define HWBP_TYPE_R             1u // 读
#define HWBP_TYPE_W             2u // 写
#define HWBP_TYPE_RW            3u // 读写
#define HWBP_TYPE_X             4u // 执行

struct HwbpRequestStruct {
    std::uintptr_t address;
    std::uint32_t tid; // 线程id而不是进程id
    std::uint32_t flags;
    std::uint32_t bp_type;
    std::uint32_t bp_len;
};

struct FpRegs {
    std::uintptr_t lo[SH_FP_VREGS];
    std::uintptr_t hi[SH_FP_VREGS];
    std::uint32_t fpsr;
    std::uint32_t fpcr;
};

struct HwbpQueryStruct {
    std::uintptr_t address;
    std::uint32_t tid; // 线程id而不是进程id
    std::uint32_t hit_count;
    std::uint32_t is_disabled;
    std::uint32_t fp_valid;
    std::uint64_t regs[SH_GPR_COUNT];
    std::uintptr_t sp;
    std::uintptr_t pc;
    std::uintptr_t pstate;
    struct FpRegs fp;
};

struct HwbpOverrideStruct {
    std::uintptr_t address;
    std::uint32_t tid; // 线程id而不是进程id
    std::uint32_t reg_mask;
    std::uint64_t regs[SH_GPR_COUNT];
    std::uint32_t fp_mask;
    std::uint32_t fp_control_mask;
    struct FpRegs fp;
};

struct GyroArgs {
    int x, y, z;
};

#define INPUT_DEV_LEN 32
struct PhysInputMsg {
    unsigned int max_x;
    unsigned int max_y;
    char dev_name[INPUT_DEV_LEN];
}; 

struct FingerOps {
    int slot;
    int phys_x;
    int phys_y;
};   

enum ERRCODE {
    NOERR = 0, //没有错误
    ESYMBOL = -2, // 找不到符号/偏移等
    EPTM = -3, // 找不到进程相关 pid/taks/mm等
    ERES = -11, // 资源暂时不可用，或者拿不到锁，暂时的，建议重试
    ECOPY = -14, // copy相关失败
    EDEV = -19, // 没有这个设备或者找不到
    EARG = -22, // 参数问题，比如大了或小了不对等等等
    ENOSYSCALL = -38, // 没有找到相关命令(cmd)
    ESTD = -95, // 系统被魔改已经牛鬼蛇神的问题，不考虑适配
};

#endif
