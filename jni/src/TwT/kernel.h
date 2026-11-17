#ifndef TWT_KERNEL_H
#define TWT_KERNEL_H

// TwT/kernel.h — TwT driver backend (Option 14)
//
// Source: user-provided kernel.h ("TwT_driver" ioctl ABI — GET_PID,
// MODULE_BASE/MODULE_BSS, READ_MEM/READ_MEM_V2, WRITE_MEM, TOUCH_*,
// GYRO_*, TLS/PACGA helpers, and the BP_* hardware-breakpoint family
// with full x0-x30/SP/PC/PSTATE/vreg register-override on hit),
// integrated unchanged except for the same one rename already applied
// to the RT Hook driver right above this in wanbai.h: the original file
// defines `class c_driver`, which collides with wanbai.h's own, unrelated
// `class c_driver` (the project's main driver dispatcher this file plugs
// into as one more backend). Renamed here to TwtDriver — every ioctl
// command number, struct layout, and ABI static_assert is untouched.
//
// One other change, not a rename: the constructor's optional gyro/touch
// "initialize_input_devices" path (scanf prompts picking a gyro/touch
// mode at construction time) is skipped by always constructing with
// TwtDriver(false) at the call site in wanbai.h. That prompt sequence
// is this driver's own interactive CLI, same category as the file's own
// stdout chatter ("驱动对接成功" etc.) — appropriate for a standalone
// tool driving its own terminal, not for a backend object constructed
// inside this project's own driver-selection flow, which already has
// its own WebUI-driven menu/config and must not block on scanf. The
// core connect step (MY_CALL / getFd(fd) fallback) still runs either way.

#include <cstdint>
#include <dirent.h>
#include <random>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <signal.h>
#include <initializer_list>
#include <utility>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/types.h>
#include <termios.h>
#include <time.h>
#include <pthread.h>
#include <thread>
#include <vector>
#include <unordered_map>

#include <sys/syscall.h>

#define TWT_MY_CALL(magic1, magic2, cmd, arg) ({    \
    long _ret;                                             \
    register long _x0 __asm__("x0") = (long)(magic1);      \
    register long _x1 __asm__("x1") = (long)(magic2);      \
    register long _x2 __asm__("x2") = (long)(cmd);         \
    register long _x3 __asm__("x3") = (long)(arg);         \
    register long _nr __asm__("x8") = __NR_reboot;         \
    __asm__ __volatile__(                                  \
        "svc #0"                                           \
        : "=r"(_x0)                                        \
        : "r"(_x0), "r"(_x1), "r"(_x2), "r"(_x3), "r"(_nr)\
        : "memory", "cc"                                   \
    );                                                     \
    _ret = _x0;                                            \
    _ret;                                                  \
})

#define TWT_MARK 'T'
#define TWT_GET_PID         _IOW(TWT_MARK, 0, _twt_request)
#define TWT_MODULE_BASE     _IOW(TWT_MARK, 1, _twt_request)
#define TWT_MODULE_BSS      _IOW(TWT_MARK, 3, _twt_request)
#define TWT_READ_MEM        _IOW(TWT_MARK, 4, _twt_request)
#define TWT_READ_MEM_V2     _IOW(TWT_MARK, 11, _twt_request)
#define TWT_WRITE_MEM       _IOW(TWT_MARK, 5, _twt_request)
#define TWT_TOUCH_INIT      _IOW(TWT_MARK, 6, twt_touch_event_base)
#define TWT_TOUCH_DOWN      _IOW(TWT_MARK, 7, twt_touch_event_base)
#define TWT_TOUCH_UP        _IOW(TWT_MARK, 8, twt_touch_event_base)
#define TWT_GYRO_INIT       _IOW(TWT_MARK, 9, int)
#define TWT_GYRO_CONFIG     _IOWR(TWT_MARK, 10, twt_gyro_config)
#define TWT_GET_THREAD_TLS  _IOWR(TWT_MARK, 12, twt_tls_request)
#define TWT_EXEC_PACGA      _IOWR(TWT_MARK, 13, twt_pacga_request)

#define TWT_FILE_HIDE_DIRECTORY_MAX 256
#define TWT_FILE_HIDE_KEYWORD_MAX   128
#define TWT_FILE_HIDE_PATH_MAX      TWT_FILE_HIDE_DIRECTORY_MAX

struct twt_file_hide_config
{
    char directory[TWT_FILE_HIDE_DIRECTORY_MAX];
    char keyword[TWT_FILE_HIDE_KEYWORD_MAX];
    __u32 enabled;
};

struct twt_file_hide_status
{
    char directory[TWT_FILE_HIDE_DIRECTORY_MAX];
    char keyword[TWT_FILE_HIDE_KEYWORD_MAX];
    __u32 enabled;
    __u32 hook_active;
};

static_assert(sizeof(twt_file_hide_config) == 388,
              "unexpected file-hide config ioctl ABI size");
static_assert(sizeof(twt_file_hide_status) == 392,
              "unexpected file-hide status ioctl ABI size");

using twt_file_hide_request = twt_file_hide_config;

#define TWT_FILE_HIDE_INIT   _IO(TWT_MARK, 34)
#define TWT_FILE_HIDE_SET    _IOW(TWT_MARK, 31, struct twt_file_hide_config)
#define TWT_FILE_HIDE_CLEAR  _IO(TWT_MARK, 32)
#define TWT_FILE_HIDE_STATUS _IOR(TWT_MARK, 33, struct twt_file_hide_status)

#define TWT_BP_INIT_CMD        _IO(TWT_MARK, 19)
#define TWT_BP_CHECK_INITED    _IO(TWT_MARK, 30)
#define TWT_BP_GET_NUM_BRPS    _IO(TWT_MARK, 20)
#define TWT_BP_GET_NUM_WRPS    _IO(TWT_MARK, 21)
#define TWT_BP_INST            _IOWR(TWT_MARK, 22, char *)
#define TWT_BP_UNINST          _IOW(TWT_MARK, 23, char *)
#define TWT_BP_SUSPEND         _IOW(TWT_MARK, 24, char *)
#define TWT_BP_RESUME          _IOW(TWT_MARK, 25, char *)
#define TWT_BP_GET_HIT_COUNT   _IOWR(TWT_MARK, 26, char *)
#define TWT_BP_MODIFY          _IOW(TWT_MARK, 27, char *)
#define TWT_BP_GET_HIT_ITEMS   _IOWR(TWT_MARK, 28, char *)

#define TWT_HW_BREAKPOINT_LEN_1 1
#define TWT_HW_BREAKPOINT_LEN_2 2
#define TWT_HW_BREAKPOINT_LEN_3 3
#define TWT_HW_BREAKPOINT_LEN_4 4
#define TWT_HW_BREAKPOINT_LEN_5 5
#define TWT_HW_BREAKPOINT_LEN_6 6
#define TWT_HW_BREAKPOINT_LEN_7 7
#define TWT_HW_BREAKPOINT_LEN_8 8

#define TWT_HW_BREAKPOINT_EMPTY   0
#define TWT_HW_BREAKPOINT_R       1
#define TWT_HW_BREAKPOINT_W       2
#define TWT_HW_BREAKPOINT_RW      (TWT_HW_BREAKPOINT_R | TWT_HW_BREAKPOINT_W)
#define TWT_HW_BREAKPOINT_X       4

#define TWT_REG_MODIFY_X(N)     (1ULL << (N))
#define TWT_REG_MODIFY_SP     (1ULL << 31)
#define TWT_REG_MODIFY_PC     (1ULL << 32)
#define TWT_REG_MODIFY_PSTATE (1ULL << 33)

#define TWT_BP_FLAG_RECORD    0x1

#pragma pack(1)
struct twt_bp_user_pt_regs
{
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
    uint64_t orig_x0;
    uint64_t syscallno;
    __uint128_t vregs[32];
};

struct twt_bp_hit_item
{
    uint64_t task_id;
    uint64_t hit_addr;
    uint64_t hit_time;
    struct twt_bp_user_pt_regs regs_info;
};

struct twt_bp_get_hit_count_arg
{
    uint64_t handle;
    uint64_t hit_total_count;
    uint64_t hit_item_arr_count;
};

struct twt_bp_get_hit_items_args
{
    uint64_t handle;
    uint64_t user_buffer_ptr;
    uint64_t max_bytes;
    uint64_t items_copied;
};

struct twt_bp_inst_args
{
    int32_t pid;
    uint64_t addr;
    uint32_t bp_len;
    uint32_t bp_type;
    uint64_t reg_modify_mask;
    uint64_t fp_reg_modify_mask;
    uint64_t regs_to_set_ptr;
    uint32_t flags;
};

struct twt_bp_modify_args
{
    uint64_t handle;
    uint64_t reg_modify_mask;
    uint64_t fp_reg_modify_mask;
    uint64_t regs_to_set_ptr;
};
#pragma pack()

struct twt_tls_request
{
    int32_t pid;
    int32_t tid;
    uint64_t tls_value;
    int32_t result;
    uint32_t reserved;
};

struct twt_pacga_request
{
    int32_t pid;
    uint32_t reserved0;
    uint64_t value;
    uint64_t modifier;
    uint64_t result_pac;
    int32_t ok;
    uint32_t reserved1;
};

static_assert(sizeof(twt_tls_request) == 24, "unexpected TLS ioctl ABI size");
static_assert(sizeof(twt_pacga_request) == 40, "unexpected PACGA ioctl ABI size");

struct twt_bp_reg_batch
{
    twt_bp_user_pt_regs regs = {};
    uint64_t reg_mask = 0;
    uint64_t fp_mask = 0;

    twt_bp_reg_batch &x(int idx, uint64_t val)
    {
        if (idx >= 0 && idx <= 30) {
            regs.regs[idx] = val;
            reg_mask |= 1ULL << idx;
        }
        return *this;
    }

    twt_bp_reg_batch &sp(uint64_t val)
    {
        regs.sp = val;
        reg_mask |= TWT_REG_MODIFY_SP;
        return *this;
    }

    twt_bp_reg_batch &pc(uint64_t val)
    {
        regs.pc = val;
        reg_mask |= TWT_REG_MODIFY_PC;
        return *this;
    }

    twt_bp_reg_batch &pstate(uint64_t val)
    {
        regs.pstate = val;
        reg_mask |= TWT_REG_MODIFY_PSTATE;
        return *this;
    }

    twt_bp_reg_batch &vf(int idx, float val)
    {
        if (idx >= 0 && idx <= 31) {
            __uint128_t v = 0;
            memcpy(&v, &val, sizeof(float));
            regs.vregs[idx] = v;
            fp_mask |= 1ULL << idx;
        }
        return *this;
    }

    twt_bp_reg_batch &vd(int idx, double val)
    {
        if (idx >= 0 && idx <= 31) {
            __uint128_t v = 0;
            memcpy(&v, &val, sizeof(double));
            regs.vregs[idx] = v;
            fp_mask |= 1ULL << idx;
        }
        return *this;
    }

    twt_bp_reg_batch &reset()
    {
        memset(&regs, 0, sizeof(regs));
        reg_mask = 0;
        fp_mask = 0;
        return *this;
    }

    bool empty() const { return reg_mask == 0 && fp_mask == 0; }
};

class TwtDriver
{
private:
    int fd = -1;
    pid_t pid = -1;

    typedef struct _twt_request
    {
        pid_t pid;
        uintptr_t addr;
        void *buffer;
        size_t size;
    } twt_request, *Ptwt_request;

    typedef struct _twt_touch_event_base
    {
        int slot;
        int x;
        int y;
    } twt_touch_event_base, *Ptwt_touch_event_base;

    typedef struct twt_gyro_config {
        uint32_t enable;
        uint32_t x;
        uint32_t y;
    } twt_gyro_config, *Ptwt_gyro_config;

    __attribute__((visibility("hidden"))) int getFd(const char *str)
    {
        DIR *dir;
        struct dirent *entry;
        char path[PATH_MAX], link[PATH_MAX];
        int found_fd = -1;

        dir = opendir("/proc/self/fd");
        if (!dir)
        {
            return -1;
        }

        while ((entry = readdir(dir)) != NULL)
        {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            {
                continue;
            }

            snprintf(path, sizeof(path), "/proc/self/fd/%s", entry->d_name);

            ssize_t len = readlink(path, link, sizeof(link) - 1);
            if (len == -1)
            {
                continue;
            }
            link[len] = '\0';

            if (strstr(link, str) != NULL && strstr(link, "anon_inode:") != NULL)
            {
                found_fd = atoi(entry->d_name);
                break;
            }
        }
        closedir(dir);

        return found_fd;
    }

public:
    // NOTE: default changed to false at this file's call site (wanbai.h
    // always constructs TwtDriver(false)) — see file header above for why.
    explicit TwtDriver(bool initialize_input_devices = false)
    {
        fd = -1;
        TWT_MY_CALL(0x114514, 0x1919810, 0x2778, &fd);
        if (fd < 0)
            fd = getFd("TwT_driver");
        if (fd < 0)
        {
            printf("[TwT] driver attach failed\n");
        } else {
            printf("[TwT] driver attach ok\n");
        }

        if (!initialize_input_devices)
            return;

        int choice = -1;

        printf("\n===== Gyro init =====\n");
        printf("0: tracepoint  1: uprobe  2: skip\nChoice: ");
        scanf("%d", &choice);
        if (choice >= 0 && choice <= 1) {
            if (gyro_init(choice))
                printf("[TwT] gyro init ok (mode %d)\n", choice);
            else
                printf("[TwT] gyro init failed\n");
        }

        printf("\n===== Touch init =====\n");
        printf("0: mode0  1: mode1  2: skip\nChoice: ");
        scanf("%d", &choice);
        if (choice >= 0 && choice <= 1) {
            if (touch_init(choice))
                printf("[TwT] touch init ok (mode %d)\n", choice);
            else
                printf("[TwT] touch init failed\n");
        }
    }

    ~TwtDriver()
    {
        if (fd >= 0)
        {
            close(fd);
        }
    }

    bool is_ready() const
    {
        return fd >= 0;
    }

    bool initialize(pid_t target_pid)
    {
        if (target_pid <= 0)
            return false;
        this->pid = target_pid;
        return true;
    }

    bool read(uintptr_t addr, void *buffer, size_t size)
    {
        twt_request req = {};
        if (fd < 0 || pid <= 0)
            return false;

        addr &= 0xFFFFFFFFFFFF;
        req.pid = pid;
        req.addr = addr;
        req.buffer = buffer;
        req.size = size;

        if (ioctl(fd, TWT_READ_MEM, &req) != 0)
        {
            return false;
        }
        return true;
    }

    template <typename T>
    T read(uintptr_t addr)
    {
        T res{};
        if (read(addr, &res, sizeof(T)))
            return res;
        return {};
    }

    bool read_v2(uintptr_t addr, void *buffer, size_t size)
    {
        twt_request req = {};
        if (fd < 0 || pid <= 0)
            return false;

        addr &= 0xFFFFFFFFFFFF;
        req.pid = pid;
        req.addr = addr;
        req.buffer = buffer;
        req.size = size;

        if (ioctl(fd, TWT_READ_MEM_V2, &req) != 0)
        {
            return false;
        }
        return true;
    }

    bool write(uintptr_t addr, void *buffer, size_t size)
    {
        twt_request req = {};
        if (fd < 0 || pid <= 0)
            return false;

        addr &= 0xFFFFFFFFFFFF;
        req.pid = this->pid;
        req.addr = addr;
        req.buffer = buffer;
        req.size = size;

        if (ioctl(fd, TWT_WRITE_MEM, &req) != 0)
        {
            return false;
        }
        return true;
    }

    template <typename T>
    bool write(uintptr_t addr, T value)
    {
        return write(addr, &value, sizeof(T));
    }

    pid_t get_name_pid(const char *name)
    {
        twt_request req = {};
        char buf[0x100];
        if (fd < 0 || !name)
            return -1;

        snprintf(buf, sizeof(buf), "%s", name);
        req.pid = 0;
        req.buffer = buf;

        if(ioctl(fd, TWT_GET_PID, &req) != 0)
        {
            return -1;
        }
        return req.pid;
    }

    uintptr_t get_module_base(const char *name)
    {
        twt_request req = {};
        char buf[0x100];
        if (fd < 0 || pid <= 0 || !name)
            return 0;

        snprintf(buf, sizeof(buf), "%s", name);
        req.pid = this->pid;
        req.addr = 0;
        req.buffer = buf;

        if (ioctl(fd, TWT_MODULE_BASE, &req) != 0)
        {
            return 0;
        }
        return req.addr;
    }

    uintptr_t get_module_bss(const char *name)
    {
        twt_request req = {};
        char buf[0x100];
        if (fd < 0 || pid <= 0 || !name)
            return 0;

        snprintf(buf, sizeof(buf), "%s", name);
        req.pid = this->pid;
        req.addr = 0;
        req.buffer = buf;

        if (ioctl(fd, TWT_MODULE_BSS, &req) != 0)
        {
            return 0;
        }
        return req.addr;
    }

    bool touch_init(int touch_mode)
    {
        twt_touch_event_base teb = {};
        if (fd < 0)
            return false;
        if (touch_mode > 1 || touch_mode < 0)
            return false;
        teb.slot = touch_mode;
        if (ioctl(fd, TWT_TOUCH_INIT, &teb) != 0)
        {
            if (errno == EALREADY)
                return true;
            return false;
        }
        return true;
    }

    bool touch_down(int slot, int x, int y)
    {
        twt_touch_event_base teb = {};
        if (fd < 0 || slot < 0)
            return false;

        teb.slot = slot;
        teb.x = x;
        teb.y = y;

        return ioctl(fd, TWT_TOUCH_DOWN, &teb) == 0;
    }

    bool touch_up(int slot)
    {
        twt_touch_event_base teb = {};
        if (fd < 0 || slot < 0)
            return false;

        teb.slot = slot;
        teb.x = 0;
        teb.y = 0;

        return ioctl(fd, TWT_TOUCH_UP, &teb) == 0;
    }

    bool gyro_init(int method)
    {
        int current_method = method;
        if (fd < 0)
            return false;
        if (method < 0 || method > 1)
            return false;
        if (ioctl(fd, TWT_GYRO_INIT, &current_method) != 0)
        {
            if (errno == EALREADY)
                return true;
            return false;
        }
        return true;
    }

    bool gyro_modify(float x, float y)
    {
        twt_gyro_config config = {};
        if (fd < 0)
            return false;

        config.enable = 1;
        memcpy(&config.x, &x, sizeof(uint32_t));
        memcpy(&config.y, &y, sizeof(uint32_t));

        return ioctl(fd, TWT_GYRO_CONFIG, &config) == 0;
    }

    bool gyro_disable()
    {
        twt_gyro_config config = {};
        if (fd < 0)
            return false;

        config.enable = 0;
        config.x = 0;
        config.y = 0;

        return ioctl(fd, TWT_GYRO_CONFIG, &config) == 0;
    }

    bool file_hide_init()
    {
        if (fd < 0) { errno = EBADF; return false; }
        return ioctl(fd, TWT_FILE_HIDE_INIT, 0) == 0;
    }

    bool file_hide_set(const char *directory, const char *keyword, bool enabled = true)
    {
        twt_file_hide_config config = {};
        if (fd < 0 || !directory || !keyword) { errno = EINVAL; return false; }

        const size_t directory_len = strnlen(directory, sizeof(config.directory));
        const size_t keyword_len = strnlen(keyword, sizeof(config.keyword));
        if (directory_len == 0 || keyword_len == 0) { errno = EINVAL; return false; }
        if (directory_len >= sizeof(config.directory) ||
            keyword_len >= sizeof(config.keyword)) { errno = ENAMETOOLONG; return false; }

        memcpy(config.directory, directory, directory_len);
        memcpy(config.keyword, keyword, keyword_len);
        config.enabled = enabled ? 1U : 0U;
        return ioctl(fd, TWT_FILE_HIDE_SET, &config) == 0;
    }

    bool file_hide_clear()
    {
        if (fd < 0) { errno = EBADF; return false; }
        return ioctl(fd, TWT_FILE_HIDE_CLEAR, 0) == 0;
    }

    bool get_thread_tls(pid_t target_pid, pid_t target_tid, uint64_t *tls_value)
    {
        twt_tls_request req = {};
        if (fd < 0 || target_pid <= 0 || !tls_value) { errno = EINVAL; return false; }

        req.pid = target_pid;
        req.tid = target_tid;
        if (ioctl(fd, TWT_GET_THREAD_TLS, &req) != 0)
            return false;
        if (req.result != 0) { errno = -req.result; return false; }

        *tls_value = req.tls_value;
        return true;
    }

    bool exec_pacga(pid_t target_pid, uint64_t value, uint64_t modifier, uint64_t *result_pac)
    {
        twt_pacga_request req = {};
        if (fd < 0 || target_pid <= 0 || !result_pac) { errno = EINVAL; return false; }

        req.pid = target_pid;
        req.value = value;
        req.modifier = modifier;
        if (ioctl(fd, TWT_EXEC_PACGA, &req) != 0)
            return false;
        if (!req.ok) { errno = EIO; return false; }

        *result_pac = req.result_pac;
        return true;
    }

    bool bp_check_inited()
    {
        if (fd < 0) return false;
        return ioctl(fd, TWT_BP_CHECK_INITED, 0) == 0;
    }

    bool bp_init_driver()
    {
        if (fd < 0) return false;
        return ioctl(fd, TWT_BP_INIT_CMD, 0) == 0;
    }

    int bp_get_num_brps()
    {
        if (fd < 0) return -1;
        return ioctl(fd, TWT_BP_GET_NUM_BRPS, 0);
    }

    int bp_get_num_wrps()
    {
        if (fd < 0) return -1;
        return ioctl(fd, TWT_BP_GET_NUM_WRPS, 0);
    }

    // Returns an opaque handle (0 on failure) — the ioctl writes the real
    // handle back into the same field it took addr in, same in/out
    // convention bp_uninst/bp_modify/bp_suspend/bp_resume below expect.
    uint64_t bp_inst(pid_t target_pid, uint64_t addr, uint32_t bp_len,
                     uint32_t bp_type, uint64_t reg_modify_mask = 0,
                     uint64_t fp_reg_modify_mask = 0,
                     struct twt_bp_user_pt_regs *regs_to_set = nullptr,
                     uint32_t flags = 0)
    {
        struct twt_bp_inst_args args = {};
        if (fd < 0)
            return 0;

        args.pid = target_pid;
        args.addr = addr;
        args.bp_len = bp_len;
        args.bp_type = bp_type;
        args.reg_modify_mask = reg_modify_mask;
        args.fp_reg_modify_mask = fp_reg_modify_mask;
        args.regs_to_set_ptr = (uint64_t)regs_to_set;
        args.flags = flags;

        if (ioctl(fd, TWT_BP_INST, &args) != 0)
            return 0;

        return args.addr;
    }

    uint64_t bp_inst(pid_t target_pid, uint64_t addr, uint32_t bp_len,
                     uint32_t bp_type, twt_bp_reg_batch &batch, uint32_t flags = 0)
    {
        return bp_inst(target_pid, addr, bp_len, bp_type,
                       batch.reg_mask, batch.fp_mask,
                       batch.empty() ? nullptr : &batch.regs, flags);
    }

    bool bp_uninst(uint64_t handle)
    {
        if (fd < 0 || !handle)
            return false;
        return ioctl(fd, TWT_BP_UNINST, &handle) == 0;
    }

    bool bp_suspend(uint64_t handle)
    {
        if (fd < 0 || !handle)
            return false;
        return ioctl(fd, TWT_BP_SUSPEND, &handle) == 0;
    }

    bool bp_resume(uint64_t handle)
    {
        if (fd < 0 || !handle)
            return false;
        return ioctl(fd, TWT_BP_RESUME, &handle) == 0;
    }

    bool bp_modify(uint64_t handle, uint64_t reg_modify_mask,
                   uint64_t fp_reg_modify_mask,
                   struct twt_bp_user_pt_regs *regs_to_set)
    {
        struct twt_bp_modify_args args = {};
        if (fd < 0 || !handle)
            return false;

        args.handle = handle;
        args.reg_modify_mask = reg_modify_mask;
        args.fp_reg_modify_mask = fp_reg_modify_mask;
        args.regs_to_set_ptr = (uint64_t)regs_to_set;

        return ioctl(fd, TWT_BP_MODIFY, &args) == 0;
    }

    bool bp_set_reg(uint64_t handle, int reg_idx, uint64_t value)
    {
        if (reg_idx < 0 || reg_idx > 30)
            return false;
        struct twt_bp_user_pt_regs regs = {};
        regs.regs[reg_idx] = value;
        return bp_modify(handle, 1ULL << reg_idx, 0, &regs);
    }
};

#endif // TWT_KERNEL_H
