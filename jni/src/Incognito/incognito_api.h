#pragma once
/* ==========================================================================
 * Incognito KPM — userspace client
 * ==========================================================================
 * Driver for the Incognito KernelPatch module (name=Incognito,
 * version=1.1.0, author=HijadaKernel, built from
 * IncognitoKpm/src/incognito.c).
 *
 * Everything in this file was read out of the supplied Incognito.kpm
 * itself -- an AArch64 relocatable ELF with full DWARF debug info, so the
 * request structs below are the module's own layouts, and the command
 * numbers are its own enum. Nothing here is guessed:
 *
 *   struct layouts   readelf --debug-dump=info  (DW_TAG_structure_type,
 *                    members with DW_AT_data_member_location)
 *   command numbers  the module's anonymous enum CMD_PING..CMD_HWBP_UPDATE_EX
 *   call ABI         disassembly of before_prctl()
 *   ext magic        disassembly of read_hwbp_ext()
 *
 * ── Call ABI (from before_prctl) ─────────────────────────────────────────
 * The module hooks prctl. before_prctl checks, in this order:
 *
 *   (option >> 16) == 0x494e        "IN"  -- else it is not for this module
 *   arg2 == 0x9e37f1c5a3b7d24f      shared secret
 *   (option & 0xffff) - 1 <= 0xd    command must be 1..14, else -EINVAL
 *   arg1                            pointer to the request struct
 *
 * so every call is:
 *
 *   prctl(0x494e0000 | cmd, (unsigned long)&req, INC_PRCTL_MAGIC, 0, 0)
 *
 * The return value is the module's status: 0 on success, negative errno
 * otherwise (-EINVAL -22, -EFAULT -14 are the ones it uses most).
 *
 * ── Hardware breakpoints ─────────────────────────────────────────────────
 * The module registers real kernel perf hardware breakpoints
 * (register_user_hw_breakpoint; its init line prints reg_hwbp=, and
 * hwbp_slot holds a perf_event*). On a hit it records the GP register
 * state, and the override block can rewrite registers before the
 * instruction runs -- including the FP/SIMD registers, which is what
 * matters for a float argument in s0/s1/s2.
 *
 * Note the module's own init log says the hardware budget is
 * "fixed, system-wide budget shared by ALL processes", so a breakpoint can
 * fail to materialise simply because something else on the device holds
 * the registers. That is worth checking against its dmesg line before
 * concluding anything about a given address.
 *
 * Hits carry GP registers only (hwbp_hit_item embeds user_pt_regs_min,
 * which is x0..x30/sp/pc/pstate -- no vregs), so a hit tells you execution
 * reached the instruction and what the integer registers held; float
 * arguments have to come from the override side or from memory.
 * ========================================================================== */

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <sys/prctl.h>
#include <errno.h>

/* ── call constants (before_prctl) ─────────────────────────────────────── */
#define INC_PRCTL_MAGIC   0x9e37f1c5a3b7d24fULL
#define INC_OPT_TAG       0x494eu          /* "IN", checked as option>>16   */
#define INC_OPT(cmd)      ((int)((INC_OPT_TAG << 16) | ((cmd) & 0xffffu)))

/* ── the module's own command enum ─────────────────────────────────────── */
enum {
    INC_CMD_PING            = 1,
    INC_CMD_READ            = 2,
    INC_CMD_WRITE           = 3,
    INC_CMD_FIND_PID        = 4,
    INC_CMD_MODULE_BASE     = 5,
    INC_CMD_HWBP_ADD        = 6,
    INC_CMD_HWBP_UPDATE     = 7,
    INC_CMD_HWBP_DEL        = 8,
    INC_CMD_HWBP_CLEAR      = 9,
    INC_CMD_HWBP_FREE       = 10,
    INC_CMD_HWBP_GET_HITS   = 11,
    INC_CMD_DEBUG_VMA_DUMP  = 12,
    INC_CMD_HWBP_ADD_EX     = 13,
    INC_CMD_HWBP_UPDATE_EX  = 14,
};

/* read_hwbp_ext() requires this in hwbp_ext.ext_magic, and only looks for
 * an ext block at all for commands 13/14. 0x31434e49 == "INC1". */
#define INC_EXT_MAGIC     0x31434e49u

/* perf hardware-breakpoint types. EXECUTE is 4, which is both perf's
 * HW_BREAKPOINT_X and the value the other driver reported back for an
 * execute point on this device. */
#define INC_BP_READ       1
#define INC_BP_WRITE      2
#define INC_BP_RW         3
#define INC_BP_EXECUTE    4

/* FP register indices are plain vreg numbers: 0 => s0/d0/q0, 1 => s1, ...
 * A clear bit in fp_reg_is_f64_mask means the value is a 32-bit float in
 * the low half; a set bit means a 64-bit double. */
#define INC_MAX_GP_OVERRIDE 10
#define INC_MAX_FP_OVERRIDE 10

#pragma pack(push, 1)

struct inc_mem_patch {          /* hwbp_override.mem_patches[3] element    */
    int64_t  offset;            /* 32 bytes each (376..472 for 3)          */
    uint32_t len;
    uint32_t pad;
    uint8_t  bytes[16];
};

struct inc_hwbp_override {                  /* struct hwbp_override, 568 B */
    uint32_t pid;                           /* +0    */
    int32_t  type;                          /* +4    INC_BP_*              */
    uint64_t addr;                          /* +8    target instruction    */
    int32_t  len;                           /* +16   4 for one AArch64 insn*/
    uint32_t is_write_gp_regs;              /* +20   */
    int32_t  gp_reg_count;                  /* +24   */
    int32_t  gp_reg_indices[10];            /* +28   */
    uint64_t gp_reg_values[10];             /* +72   */
    uint32_t is_write_fp_regs;              /* +152  */
    int32_t  fp_reg_count;                  /* +156  */
    int32_t  fp_reg_indices[10];            /* +160  */
    uint64_t fp_reg_values[10];             /* +200  */
    uint32_t fp_reg_is_f64_mask;            /* +360  */
    uint32_t is_mem_patch;                  /* +364  */
    int32_t  mem_patch_base_reg;            /* +368  */
    int32_t  mem_patch_count;               /* +372  */
    struct inc_mem_patch mem_patches[3];    /* +376  */
    uint32_t is_conditional;                /* +472  */
    int32_t  cond_base_reg;                 /* +476  */
    int64_t  cond_offset;                   /* +480  */
    uint32_t cond_len;                      /* +488  */
    uint8_t  cond_expected[8];              /* +492  */
    uint32_t cond_is_reg_value;             /* +500  */
    uint32_t is_conditional2;               /* +504  */
    int32_t  cond2_base_reg;                /* +508  */
    int64_t  cond2_offset;                  /* +512  */
    uint32_t cond2_list_count;              /* +520  */
    uint32_t cond2_list_values[8];          /* +524  */
    uint32_t is_reg_copy;                   /* +556  */
    int32_t  reg_copy_src;                  /* +560  */
    int32_t  reg_copy_dst;                  /* +564  */
};

struct inc_hwbp_ext {                       /* struct hwbp_ext, 16 B       */
    uint32_t ext_magic;                     /* must be INC_EXT_MAGIC       */
    uint32_t is_persistent;
    uint32_t fp_reg_add_mask;
    uint32_t reserved;
};

/* read_hwbp_ext() copies the ext block from req + 0x238, and 0x238 == 568
 * == sizeof(hwbp_override), so commands 13/14 take the two structs back to
 * back as one request. */
struct inc_hwbp_add_ex {
    struct inc_hwbp_override ov;
    struct inc_hwbp_ext      ext;
};

struct inc_user_pt_regs {                   /* user_pt_regs_min, 272 B     */
    uint64_t regs[31];                      /* x0..x30                     */
    uint64_t sp;                            /* +248 */
    uint64_t pc;                            /* +256 */
    uint64_t pstate;                        /* +264 */
};

struct inc_hwbp_hit_item {                  /* hwbp_hit_item, 296 B        */
    uint32_t task_id;
    uint32_t pad;
    uint64_t hit_addr;
    uint64_t hit_time;
    struct inc_user_pt_regs regs_info;      /* +24 */
};

struct inc_req_rw {                         /* req_rw, 32 B                */
    uint32_t pid;
    uint32_t pad;
    uint64_t addr;
    uint64_t buffer;                        /* user VA in THIS process     */
    uint64_t size;
};

struct inc_req_find_pid {                   /* req_find_pid, 256 B         */
    char name[256];
};

struct inc_req_module_base {                /* req_module_base, 272 B      */
    uint32_t pid;
    uint32_t pad;
    char     name[256];
    uint64_t out_base;                      /* +264, written by the module */
};

struct inc_req_hwbp_disable {               /* req_hwbp_disable, 16 B      */
    uint32_t pid;
    uint32_t pad;
    uint64_t addr;
};

struct inc_req_hwbp_hits {                  /* req_hwbp_hits, 32 B         */
    uint32_t pid;
    uint32_t pad;
    uint64_t addr;
    uint64_t out_buf;                       /* user VA for hit items       */
    int32_t  out_len;                       /* capacity (see get_hits)     */
    int32_t  real_count;                    /* +28, written by the module  */
};

#pragma pack(pop)

/* If any of these fire, the struct above stopped matching the module and
 * every call would be feeding it garbage -- better to fail the build. */
static_assert(sizeof(struct inc_hwbp_override)   == 568, "hwbp_override size");
static_assert(offsetof(struct inc_hwbp_override, is_write_fp_regs) == 152, "fp flag off");
static_assert(offsetof(struct inc_hwbp_override, fp_reg_indices)   == 160, "fp idx off");
static_assert(offsetof(struct inc_hwbp_override, fp_reg_values)    == 200, "fp val off");
static_assert(offsetof(struct inc_hwbp_override, fp_reg_is_f64_mask) == 360, "f64 mask off");
static_assert(offsetof(struct inc_hwbp_override, mem_patches)      == 376, "mem patches off");
static_assert(offsetof(struct inc_hwbp_override, is_reg_copy)      == 556, "reg copy off");
static_assert(sizeof(struct inc_hwbp_ext)        == 16,  "hwbp_ext size");
static_assert(offsetof(struct inc_hwbp_add_ex, ext) == 568, "ext must sit at +0x238");
static_assert(sizeof(struct inc_user_pt_regs)    == 272, "user_pt_regs_min size");
static_assert(sizeof(struct inc_hwbp_hit_item)   == 296, "hwbp_hit_item size");
static_assert(offsetof(struct inc_hwbp_hit_item, regs_info) == 24, "regs_info off");
static_assert(sizeof(struct inc_req_rw)          == 32,  "req_rw size");
static_assert(sizeof(struct inc_req_module_base) == 272, "req_module_base size");
static_assert(offsetof(struct inc_req_module_base, out_base) == 264, "out_base off");
static_assert(sizeof(struct inc_req_hwbp_hits)   == 32,  "req_hwbp_hits size");

/* ── the client ───────────────────────────────────────────────────────── */
class IncognitoDriver {
public:
    explicit IncognitoDriver(pid_t target = 0) : pid_(target) {}

    void set_pid(pid_t p) { pid_ = p; }
    pid_t get_pid() const { return pid_; }

    /* Returns 0 when the module answers, so this doubles as "is the KPM
     * loaded at all". Anything else means it is not there (the unhooked
     * prctl just returns -EINVAL for an unknown option). */
    long ping() { int z = 0; return call(INC_CMD_PING, &z); }

    bool available() { return ping() == 0; }

    bool read(uintptr_t addr, void* out, size_t len) {
        if (!addr || !out || !len) return false;
        struct inc_req_rw r{};
        r.pid    = (uint32_t)pid_;
        r.addr   = (uint64_t)addr;
        r.buffer = (uint64_t)(uintptr_t)out;
        r.size   = (uint64_t)len;
        return call(INC_CMD_READ, &r) == 0;
    }

    bool write(uintptr_t addr, const void* in, size_t len) {
        if (!addr || !in || !len) return false;
        struct inc_req_rw r{};
        r.pid    = (uint32_t)pid_;
        r.addr   = (uint64_t)addr;
        r.buffer = (uint64_t)(uintptr_t)in;
        r.size   = (uint64_t)len;
        return call(INC_CMD_WRITE, &r) == 0;
    }

    pid_t find_pid(const char* name) {
        struct inc_req_find_pid r{};
        snprintf(r.name, sizeof(r.name), "%s", name ? name : "");
        long rc = call(INC_CMD_FIND_PID, &r);
        return rc > 0 ? (pid_t)rc : (pid_t)-1;
    }

    uintptr_t module_base(const char* name) {
        struct inc_req_module_base r{};
        r.pid = (uint32_t)pid_;
        snprintf(r.name, sizeof(r.name), "%s", name ? name : "");
        if (call(INC_CMD_MODULE_BASE, &r) != 0) return 0;
        return (uintptr_t)r.out_base;
    }

    /* Plain execute breakpoint, no register override. */
    bool bp_add_exec(uintptr_t addr, bool persistent = true) {
        struct inc_hwbp_add_ex r{};
        r.ov.pid  = (uint32_t)pid_;
        r.ov.type = INC_BP_EXECUTE;
        r.ov.addr = (uint64_t)addr;
        r.ov.len  = 4;
        r.ext.ext_magic     = INC_EXT_MAGIC;
        r.ext.is_persistent = persistent ? 1u : 0u;
        return call(INC_CMD_HWBP_ADD_EX, &r) == 0;
    }

    /* Execute breakpoint that rewrites up to 3 single-precision FP
     * registers before the instruction runs. idx/val are vreg numbers and
     * float values: {0,1,2} with a Vector3 is the AAPCS layout for a
     * float-triplet argument (s0=X, s1=Y, s2=Z).
     *
     * add_mask follows hwbp_ext.fp_reg_add_mask: a set bit means the value
     * is added to whatever the register already holds instead of replacing
     * it. Left 0 here unless asked for, so values replace. */
    bool bp_add_exec_fp(uintptr_t addr, const int* idx, const float* val,
                        int count, bool persistent = true,
                        uint32_t add_mask = 0) {
        if (count <= 0 || count > INC_MAX_FP_OVERRIDE || !idx || !val) return false;
        struct inc_hwbp_add_ex r{};
        r.ov.pid  = (uint32_t)pid_;
        r.ov.type = INC_BP_EXECUTE;
        r.ov.addr = (uint64_t)addr;
        r.ov.len  = 4;
        r.ov.is_write_fp_regs = 1;
        r.ov.fp_reg_count     = count;
        for (int i = 0; i < count; ++i) {
            uint32_t bits = 0;
            memcpy(&bits, &val[i], sizeof(bits));
            r.ov.fp_reg_indices[i] = idx[i];
            r.ov.fp_reg_values[i]  = (uint64_t)bits;   /* f32 in the low half */
        }
        r.ov.fp_reg_is_f64_mask = 0;                   /* all single precision */
        r.ext.ext_magic         = INC_EXT_MAGIC;
        r.ext.is_persistent     = persistent ? 1u : 0u;
        r.ext.fp_reg_add_mask   = add_mask;
        return call(INC_CMD_HWBP_ADD_EX, &r) == 0;
    }

    /* Same payload, but updates a point that already exists. Use this to
     * change the values every frame without tearing the breakpoint down. */
    bool bp_update_exec_fp(uintptr_t addr, const int* idx, const float* val,
                           int count, uint32_t add_mask = 0) {
        if (count <= 0 || count > INC_MAX_FP_OVERRIDE || !idx || !val) return false;
        struct inc_hwbp_add_ex r{};
        r.ov.pid  = (uint32_t)pid_;
        r.ov.type = INC_BP_EXECUTE;
        r.ov.addr = (uint64_t)addr;
        r.ov.len  = 4;
        r.ov.is_write_fp_regs = 1;
        r.ov.fp_reg_count     = count;
        for (int i = 0; i < count; ++i) {
            uint32_t bits = 0;
            memcpy(&bits, &val[i], sizeof(bits));
            r.ov.fp_reg_indices[i] = idx[i];
            r.ov.fp_reg_values[i]  = (uint64_t)bits;
        }
        r.ov.fp_reg_is_f64_mask = 0;
        r.ext.ext_magic         = INC_EXT_MAGIC;
        r.ext.is_persistent     = 1;
        r.ext.fp_reg_add_mask   = add_mask;
        return call(INC_CMD_HWBP_UPDATE_EX, &r) == 0;
    }

    bool bp_del(uintptr_t addr) {
        struct inc_req_hwbp_disable r{};
        r.pid  = (uint32_t)pid_;
        r.addr = (uint64_t)addr;
        return call(INC_CMD_HWBP_DEL, &r) == 0;
    }

    bool bp_clear() { int z = 0; return call(INC_CMD_HWBP_CLEAR, &z) == 0; }
    bool bp_free()  { int z = 0; return call(INC_CMD_HWBP_FREE,  &z) == 0; }

    /* Reads hit records for one breakpoint.
     *
     * req_hwbp_hits has out_len as a plain int32 capacity and real_count as
     * the module's answer, but which unit out_len is in is not something the
     * struct says. So this asks with the item COUNT first, which is the
     * conservative reading: if the module wants bytes instead, it sees a
     * tiny buffer and writes nothing rather than overrunning anything.
     * real_count still comes back either way, so when it reports hits but
     * nothing was written, the call is repeated with the byte size -- by
     * then the unit is known, so that second call is safe too.
     *
     * out_count gets however many items were actually filled in. */
    bool bp_get_hits(uintptr_t addr, struct inc_hwbp_hit_item* items,
                     int capacity_items, int* out_count, int* out_real = nullptr) {
        if (!items || capacity_items <= 0) return false;
        if (out_count) *out_count = 0;

        auto attempt = [&](int out_len) -> int {
            memset(items, 0, (size_t)capacity_items * sizeof(items[0]));
            struct inc_req_hwbp_hits r{};
            r.pid     = (uint32_t)pid_;
            r.addr    = (uint64_t)addr;
            r.out_buf = (uint64_t)(uintptr_t)items;
            r.out_len = out_len;
            if (call(INC_CMD_HWBP_GET_HITS, &r) != 0) return -1;
            if (out_real) *out_real = r.real_count;
            int filled = 0;
            for (int i = 0; i < capacity_items; ++i)
                if (items[i].hit_addr || items[i].regs_info.pc) ++filled;
            return filled;
        };

        int filled = attempt(capacity_items);
        if (filled < 0) return false;
        if (filled == 0 && out_real && *out_real > 0)
            filled = attempt(capacity_items * (int)sizeof(items[0]));
        if (filled < 0) return false;
        if (out_count) *out_count = filled;
        return true;
    }

private:
    long call(int cmd, void* req) {
        return (long)prctl(INC_OPT(cmd),
                           (unsigned long)(uintptr_t)req,
                           (unsigned long)INC_PRCTL_MAGIC,
                           0UL, 0UL);
    }

    pid_t pid_ = 0;
};
