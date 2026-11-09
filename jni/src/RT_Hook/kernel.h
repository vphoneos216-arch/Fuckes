
#ifndef RT_HOOK_KERNEL_H
#define RT_HOOK_KERNEL_H

// RT_Hook/kernel.h — RT Hook driver backend (Option 13)
//
// Source: user-provided kernel.h, integrated unchanged except for one
// rename. The original file defined `class c_driver`, which collides with
// wanbai.h's own, unrelated `class c_driver` (the project's main driver
// dispatcher that this file plugs into as one more backend) -- including
// both as-is would either fail to compile (duplicate class) or silently
// construct the wrong one. Renamed here to RtHookDriver; every ioctl
// command number, struct layout, and ABI static_assert is untouched.
//
// This header only defines the class -- no global singleton, no helper
// globals (getPID/GetModuleBaseAddr/ReadValue/etc.) from the original file.
// Those duplicated functionality wanbai.h's c_driver already provides
// through its own read()/write()/getModuleBase() dispatch, and some of
// their names (getPID in particular) collide with existing project globals
// (Scarecrow/MemoryTools.h) -- pulling them in would add risk for no
// benefit, since wanbai.h's RT Hook branch (Option 13) calls directly
// into RtHookDriver's own read()/write() methods below instead.


#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>


#define OP_CMD_READ                 601
#define OP_CMD_WRITE                602
#define OP_CMD_BASE                 603
#define OP_CMD_GETPID               604
#define OP_CMD_HIDE_PROCESS         605
#define OP_CMD_UN_HOOK              606
#define OP_CMD_RECOVER_PROCESS      607
#define OP_CMD_GYRO                 608
#define OP_CMD_HWBP_ADD             609
#define OP_CMD_HWBP_GET_HITS        610
#define OP_CMD_HWBP_ENABLE          611
#define OP_CMD_HWBP_CLEAR           612
#define OP_CMD_HWBP_DISABLE         613
#define OP_CMD_UPDATE_TRACKING      614
#define OP_CMD_HOOK_ATTACH          615
#define OP_CMD_HOOK_DETACH          616
#define OP_CMD_HOOK_SET_ROT         617
#define OP_CMD_HOOK_STATUS          618
#define OP_CMD_HOOK_READREG         619
#define OP_CMD_READ_SAFE            620     /* 与 READ 行为一致，安全读入口 */
#define OP_CMD_WRITE_SAFE           621
#define OP_CMD_BRK_ADD              622
#define OP_CMD_BRK_GET_HITS         623
#define OP_CMD_BRK_ENABLE           624
#define OP_CMD_BRK_CLEAR            625
#define OP_CMD_BRK_DISABLE          626
#define OP_CMD_BRK_UPDATE_TRACKING  627
#define OP_CMD_TOUCH_INIT           628
#define OP_CMD_TOUCH_EVENT          629
#define OP_CMD_TOUCH_DESTROY        630


#define MAX_MODIFY_REGS     10
#define BRK_MAX_HIT_RECORDS 16
#define STACK_ROT_MAX       12

#define HW_BP_TYPE_R    1
#define HW_BP_TYPE_W    2
#define HW_BP_TYPE_RW   3
#define HW_BP_TYPE_X    4
#define BRK_BP_TYPE_X   4

#define GYRO_MASK_GYRO  (1u << 0)
#define GYRO_MASK_UNCAL (1u << 1)
#define GYRO_MASK_ALL   (GYRO_MASK_GYRO | GYRO_MASK_UNCAL)

#define NB9_TOUCH_SLOTS 10
#define NB9_TOUCH_MASK  ((1U << NB9_TOUCH_SLOTS) - 1U)


typedef struct _TRACKING_DATA {
    bool      is_active;
    uintptr_t bp_addr;
    float     x;
    float     y;
    float     z;
} TRACKING_DATA;

typedef struct _HW_BP_INFO {
    pid_t     pid;
    uintptr_t addr;
    int       type;
    int       len;

    bool     is_write_gp_regs;
    int      gp_reg_count;
    int      gp_reg_indices[MAX_MODIFY_REGS];
    uint64_t gp_reg_values[MAX_MODIFY_REGS];

    bool     is_write_fp_regs;
    int      fp_reg_count;
    int      fp_reg_indices[MAX_MODIFY_REGS];
    uint64_t fp_reg_values[MAX_MODIFY_REGS][2];
} HW_BP_INFO;

typedef struct _BRK_BP_INFO {
    pid_t     pid;
    uintptr_t addr;
    int       type;
    int       len;

    bool     is_write_gp_regs;
    int      gp_reg_count;
    int      gp_reg_indices[MAX_MODIFY_REGS];
    uint64_t gp_reg_values[MAX_MODIFY_REGS];

    bool     is_write_fp_regs;
    int      fp_reg_count;
    int      fp_reg_indices[MAX_MODIFY_REGS];
    uint64_t fp_reg_values[MAX_MODIFY_REGS][2];
} BRK_BP_INFO;

struct REGS_INFO {
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
};

struct HWBP_HIT_ITEM {
    pid_t     task_id;
    uintptr_t hit_addr;
    uint64_t  hit_time;
    struct REGS_INFO regs_info;
};

typedef struct _HWBP_HIT_ARGS {
    pid_t     pid;
    uintptr_t addr;
    void     *out_buf;
    int       out_len;      /* 元素个数 */
    int       real_count;   /* [out] */
} HWBP_HIT_ARGS;

struct BRK_HIT_ITEM {
    pid_t     task_id;
    uintptr_t hit_addr;
    uint64_t  hit_time;
    struct REGS_INFO regs_info;
};

typedef struct _BRK_HIT_ARGS {
    pid_t     pid;
    uintptr_t addr;
    void     *out_buf;
    int       out_len;
    int       real_count;
} BRK_HIT_ARGS;

typedef struct _COPY_MEMORY {
    pid_t     pid;
    uintptr_t addr;
    void     *buffer;
    size_t    size;
} COPY_MEMORY;

typedef struct _MODULE_BASE {
    pid_t     pid;
    char     *name;
    uintptr_t base;
    short     index;
} MODULE_BASE;

typedef struct _HOOK_ATTACH {
    pid_t         pid;
    unsigned long target_addr;
} HOOK_ATTACH;

typedef struct _HOOK_SET_ROT {
    uint32_t rot[3];
    uint32_t stack_rot[STACK_ROT_MAX];
    uint32_t stack_count;
    uint32_t stack_offset;
} HOOK_SET_ROT;

typedef struct _HOOK_STATUS {
    uint8_t       active;
    uint8_t       armed;
    pid_t         pid;
    unsigned long vaddr;
    unsigned long target_addr;
    uint32_t      rot[3];
    uint32_t      stack_rot[3];
    uint32_t      stack_count;
    uint32_t      stack_offset;
    uint64_t      hit_count;
    uint64_t      captured_x0;
} HOOK_STATUS;

typedef struct _HOOK_READ_REG {
    uint32_t reg_idx;
    uint64_t value;
    uint64_t hit_count;
} HOOK_READ_REG;

struct gyro_config_cmd {
    int      enable;
    uint32_t type_mask;
    float    x;
    float    y;
    float    z;
};

struct touch_init {
    int32_t max_x;
    int32_t max_y;
};

enum touch_command {
    TOUCH_DOWN = 1,
    TOUCH_MOVE = 2,
    TOUCH_UP   = 3,
};

struct touch_event {
    uint32_t command;
    uint32_t x;
    uint32_t y;
    uint16_t slot_mask;
    uint16_t reserved;
};

static_assert(sizeof(void *) == 8,        "必须编 aarch64");
static_assert(sizeof(COPY_MEMORY) == 32,  "COPY_MEMORY ABI");
static_assert(sizeof(MODULE_BASE) == 32,  "MODULE_BASE ABI");
static_assert(sizeof(TRACKING_DATA) == 32,"TRACKING_DATA ABI");
static_assert(sizeof(HW_BP_INFO) == 360,  "HW_BP_INFO ABI");
static_assert(sizeof(BRK_BP_INFO) == 360, "BRK_BP_INFO ABI");
static_assert(sizeof(HWBP_HIT_ITEM) == 296,"HWBP_HIT_ITEM ABI");
static_assert(sizeof(BRK_HIT_ITEM) == 296, "BRK_HIT_ITEM ABI");
static_assert(sizeof(HWBP_HIT_ARGS) == 32,"HWBP_HIT_ARGS ABI");
static_assert(sizeof(BRK_HIT_ARGS) == 32, "BRK_HIT_ARGS ABI");
static_assert(sizeof(HOOK_ATTACH) == 16,  "HOOK_ATTACH ABI");
static_assert(sizeof(HOOK_SET_ROT) == 68, "HOOK_SET_ROT ABI");
static_assert(sizeof(HOOK_STATUS) == 72,  "HOOK_STATUS ABI");
static_assert(sizeof(HOOK_READ_REG) == 24,"HOOK_READ_REG ABI");
static_assert(sizeof(gyro_config_cmd) == 20, "gyro ABI");
static_assert(sizeof(touch_init) == 8,    "touch_init ABI");
static_assert(sizeof(touch_event) == 16,  "touch_event ABI");


class RtHookDriver {
private:
    pid_t pid_;

    /* 驱动 hook 的是 sys_call_table[__NR_ioctl]，按 cmd 匹配，与 fd 无关。
     * 所以这里不打开任何文件，直接用 0 号 fd（stdin）发命令即可。 */
    int call(unsigned long cmd, void *arg) {
        return ::ioctl(0, cmd, arg);
    }

public:
    RtHookDriver() : pid_(0) {}
    ~RtHookDriver() = default;

    /* 驱动是否已加载：HOOK_STATUS 成功返回 0；未加载会返回 -1/ENOTTY */
    bool Probe() {
        HOOK_STATUS st{};
        return call(OP_CMD_HOOK_STATUS, &st) == 0;
    }

    int   fd_handle()    const { return 0; }
    pid_t current_pid()  const { return pid_; }
    int   last_error()   const { return errno; }
    bool  connected()    const { return pid_ > 0; }
    void  initialize(pid_t target_pid) { pid_ = target_pid; }
    void  shutdown()     { pid_ = 0; }

    /* ---------------- 内存 ---------------- */
    bool read(uintptr_t addr, void *buffer, size_t size) {
        if (pid_ <= 0 || !addr || !buffer || !size) { errno = EINVAL; return false; }
        COPY_MEMORY cm{};
        cm.pid = pid_; cm.addr = addr; cm.buffer = buffer; cm.size = size;
        return call(OP_CMD_READ, &cm) == 0;
    }

    bool write(uintptr_t addr, const void *buffer, size_t size) {
        if (pid_ <= 0 || !addr || !buffer || !size) { errno = EINVAL; return false; }
        COPY_MEMORY cm{};
        cm.pid = pid_; cm.addr = addr;
        cm.buffer = const_cast<void *>(buffer); cm.size = size;
        return call(OP_CMD_WRITE, &cm) == 0;
    }

    /* 安全读写（驱动 620/621，行为与普通读写一致） */
    bool ReadSafe(uintptr_t addr, void *buffer, size_t size) {
        if (pid_ <= 0 || !addr || !buffer || !size) { errno = EINVAL; return false; }
        COPY_MEMORY cm{};
        cm.pid = pid_; cm.addr = addr; cm.buffer = buffer; cm.size = size;
        return call(OP_CMD_READ_SAFE, &cm) == 0;
    }

    bool WriteSafe(uintptr_t addr, const void *buffer, size_t size) {
        if (pid_ <= 0 || !addr || !buffer || !size) { errno = EINVAL; return false; }
        COPY_MEMORY cm{};
        cm.pid = pid_; cm.addr = addr;
        cm.buffer = const_cast<void *>(buffer); cm.size = size;
        return call(OP_CMD_WRITE_SAFE, &cm) == 0;
    }

    template <typename T>
    T read(uintptr_t addr) {
        T value{};
        read(addr, &value, sizeof(value));
        return value;
    }

    template <typename T>
    bool write(uintptr_t addr, T value) {
        return write(addr, &value, sizeof(value));
    }

    /* ---------------- 模块 / 进程 ---------------- */
    uintptr_t get_module_base(const char *module_name, short index = 0) {
        if (pid_ <= 0 || !module_name || !*module_name) { errno = EINVAL; return 0; }
        MODULE_BASE mb{};
        mb.pid = pid_;
        mb.name = const_cast<char *>(module_name);
        mb.index = index;
        if (call(OP_CMD_BASE, &mb) != 0)
            return 0;
        return mb.base;
    }

    void hide_process()    { call(OP_CMD_HIDE_PROCESS, nullptr); }
    void recover_process() { call(OP_CMD_RECOVER_PROCESS, nullptr); }

    /* 卸载驱动 hook（危险：卸载后驱动不再响应任何命令，一般别调） */
    bool Unhook() { return call(OP_CMD_UN_HOOK, nullptr) == 0; }

    /* ---------------- 硬件断点 ---------------- */
    bool AddHwBp(HW_BP_INFO *info) {
        if (!info) { errno = EINVAL; return false; }
        return call(OP_CMD_HWBP_ADD, info) == 0;
    }
    bool UpdateAndEnableHwBp(HW_BP_INFO *info) {
        if (!info) { errno = EINVAL; return false; }
        return call(OP_CMD_HWBP_ENABLE, info) == 0;
    }
    bool DisableHwBp(pid_t target_pid, uintptr_t addr) {
        HW_BP_INFO info{};
        info.pid = target_pid; info.addr = addr;
        return call(OP_CMD_HWBP_DISABLE, &info) == 0;
    }
    bool ClearHwBp() { return call(OP_CMD_HWBP_CLEAR, nullptr) == 0; }
    bool GetHwBpHits(HWBP_HIT_ARGS *args) {
        if (!args || !args->out_buf || args->out_len <= 0) { errno = EINVAL; return false; }
        return call(OP_CMD_HWBP_GET_HITS, args) == 0;
    }

    bool UpdateTrackingData(bool active, uintptr_t bp_addr,
                            float x, float y, float z) {
        TRACKING_DATA data{};
        data.is_active = active;
        data.bp_addr = bp_addr;
        data.x = x; data.y = y; data.z = z;
        return call(OP_CMD_UPDATE_TRACKING, &data) == 0;
    }

    /* ---------------- 陀螺仪 ---------------- */
    bool UpdateGyroXYZ(float x, float y, float z,
                       uint32_t type_mask = GYRO_MASK_ALL, bool enable = true) {
        gyro_config_cmd cmd{};
        cmd.enable = enable ? 1 : 0;
        cmd.type_mask = type_mask;
        cmd.x = x; cmd.y = y; cmd.z = z;
        return call(OP_CMD_GYRO, &cmd) == 0;
    }
    bool UpdateGyro(float x, float y,
                    uint32_t type_mask = GYRO_MASK_ALL, bool enable = true) {
        return UpdateGyroXYZ(x, y, 0.0f, type_mask, enable);
    }

    /* ---------------- inline hook ---------------- */
    bool HookAttach(pid_t target_pid, uintptr_t addr) {
        if (target_pid <= 0 || !addr) { errno = EINVAL; return false; }
        HOOK_ATTACH cmd{};
        cmd.pid = target_pid;
        cmd.target_addr = addr;
        return call(OP_CMD_HOOK_ATTACH, &cmd) == 0;
    }
    bool HookDetach() { return call(OP_CMD_HOOK_DETACH, nullptr) == 0; }
    bool HookSetRot(const HOOK_SET_ROT *config) {
        if (!config || config->stack_count > STACK_ROT_MAX) { errno = EINVAL; return false; }
        return call(OP_CMD_HOOK_SET_ROT, const_cast<HOOK_SET_ROT *>(config)) == 0;
    }
    bool HookGetStatus(HOOK_STATUS *status) {
        if (!status) { errno = EINVAL; return false; }
        return call(OP_CMD_HOOK_STATUS, status) == 0;
    }
    bool HookReadReg(uint32_t reg_idx, uint64_t *value, uint64_t *hit_count = nullptr) {
        if (!value) { errno = EINVAL; return false; }
        HOOK_READ_REG cmd{};
        cmd.reg_idx = reg_idx;
        if (call(OP_CMD_HOOK_READREG, &cmd) != 0)
            return false;
        *value = cmd.value;
        if (hit_count)
            *hit_count = cmd.hit_count;
        return true;
    }

    /* ---------------- PTE 便捷封装 ---------------- */
    bool PteAttach(uintptr_t addr) { return HookAttach(pid_, addr); }
    bool PteDetach() { return HookDetach(); }

    static uint32_t float_bits(float v) {
        uint32_t bits;
        memcpy(&bits, &v, sizeof(bits));
        return bits;
    }

    bool PteUpdateFloat3(float x, float y, float z) {
        HOOK_SET_ROT config{};
        config.stack_rot[0] = 3;
        config.stack_rot[1] = float_bits(x);
        config.stack_rot[2] = 1;
        config.stack_rot[3] = 4;
        config.stack_rot[4] = float_bits(y);
        config.stack_rot[5] = 1;
        config.stack_rot[6] = 5;
        config.stack_rot[7] = float_bits(z);
        config.stack_rot[8] = 1;
        config.stack_count = 9;
        config.rot[2] = 4;
        return call(OP_CMD_HOOK_SET_ROT, &config) == 0;
    }
    bool PteSetFloat3(uintptr_t addr, float x, float y, float z) {
        return PteAttach(addr) && PteUpdateFloat3(x, y, z);
    }
    bool PtePause() {
        HOOK_SET_ROT config{};
        config.rot[2] = 4;
        return call(OP_CMD_HOOK_SET_ROT, &config) == 0;
    }
    bool PteCapture(uintptr_t addr) {
        if (!PteAttach(addr)) return false;
        HOOK_SET_ROT config{};
        config.rot[2] = 2;
        return call(OP_CMD_HOOK_SET_ROT, &config) == 0;
    }
    bool PteKernelCopy(uintptr_t addr, uint32_t extra_offset, uint32_t skip_bytes) {
        if (!PteAttach(addr)) return false;
        HOOK_SET_ROT config{};
        config.rot[2] = 3;
        config.stack_rot[0] = extra_offset;
        config.stack_rot[1] = skip_bytes;
        config.stack_count = 2;
        return call(OP_CMD_HOOK_SET_ROT, &config) == 0;
    }

    /* ---------------- BRK ---------------- */
    bool AddBrk(BRK_BP_INFO *info) {
        if (!info || info->pid <= 0 || !info->addr ||
            (info->addr & 3) || info->len != 4 || info->type != BRK_BP_TYPE_X) {
            errno = EINVAL;
            return false;
        }
        return call(OP_CMD_BRK_ADD, info) == 0;
    }
    bool UpdateAndEnableBrk(BRK_BP_INFO *info) {
        if (!info) { errno = EINVAL; return false; }
        return call(OP_CMD_BRK_ENABLE, info) == 0;
    }
    bool DisableBrk(pid_t target_pid, uintptr_t addr) {
        BRK_BP_INFO info{};
        info.pid = target_pid; info.addr = addr;
        return call(OP_CMD_BRK_DISABLE, &info) == 0;
    }
    bool ClearBrk() { return call(OP_CMD_BRK_CLEAR, nullptr) == 0; }
    bool GetBrkHits(BRK_HIT_ARGS *args) {
        if (!args || !args->out_buf || args->out_len <= 0 ||
            args->out_len > BRK_MAX_HIT_RECORDS) { errno = EINVAL; return false; }
        return call(OP_CMD_BRK_GET_HITS, args) == 0;
    }
    bool UpdateBrkTracking(bool active, uintptr_t bp_addr,
                           float x, float y, float z) {
        TRACKING_DATA data{};
        data.is_active = active;
        data.bp_addr = bp_addr;
        data.x = x; data.y = y; data.z = z;
        return call(OP_CMD_BRK_UPDATE_TRACKING, &data) == 0;
    }

    /* ---------------- 触摸 ---------------- */
    bool TouchInit(touch_init *resolution) {
        if (!resolution) { errno = EINVAL; return false; }
        return call(OP_CMD_TOUCH_INIT, resolution) == 0;
    }
    bool TouchEvent(touch_command command, uint32_t x, uint32_t y, uint16_t slot_mask) {
        if ((command != TOUCH_DOWN && command != TOUCH_MOVE && command != TOUCH_UP) ||
            !slot_mask || (slot_mask & ~static_cast<uint16_t>(NB9_TOUCH_MASK))) {
            errno = EINVAL;
            return false;
        }
        touch_event event{};
        event.command   = command;
        event.x         = x;
        event.y         = y;
        event.slot_mask = slot_mask;
        return call(OP_CMD_TOUCH_EVENT, &event) == 0;
    }
    bool TouchDestroy() { return call(OP_CMD_TOUCH_DESTROY, nullptr) == 0; }
};


#endif /* RT_HOOK_KERNEL_H */
