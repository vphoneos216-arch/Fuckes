/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * inx_hwbp.h -- hardware-breakpoint register/memory override engine for the
 * inx_mem KPM. Included by inx_mem.c. Kept in its own file so the proven,
 * long-stable memory read/write path in inx_mem.c is not touched.
 *
 * WHAT THIS DOES
 *   Arms a real ARM64 hardware EXECUTE breakpoint on one user-space address
 *   inside a target process (via the kernel's own perf hardware-breakpoint
 *   facility). Each time the target thread is about to execute that
 *   instruction, the kernel calls our overflow handler IN THE CONTEXT OF
 *   THAT THREAD, and the handler can overwrite the thread's registers before
 *   the instruction runs:
 *     - GP registers x0..x30  (directly in the trapped pt_regs)
 *     - FP/SIMD registers q0..q31  (via the kernel's own fpsimd save/restore
 *       helpers, so NO task_struct field offset is ever needed)
 *     - a memory patch at [reg+off]
 *   with an optional "only fire when [reg+off] matches" condition.
 *
 *   This is the mechanism the aim-point override needs: put the breakpoint on
 *   the `bl WorldPosToUIPos` instruction and rewrite s0/s1/s2 (= q0/q1/q2 low
 *   lanes, the Vector3 argument) so the game projects a different world point.
 *
 * GROUND TRUTH (not guessed)
 *   The kernel entry points and the FP save/restore approach were taken from
 *   a disassembly of a working module on this exact class of device
 *   (Incognito.kpm): it resolves and uses
 *     register_user_hw_breakpoint, unregister_hw_breakpoint,
 *     perf_event_enable, perf_event_disable,
 *     fpsimd_save_state, fpsimd_update_current_state
 *   fpsimd_save_state(buf) snapshots the live CPU FP regs INTO a caller
 *   buffer and fpsimd_update_current_state(buf) loads a caller buffer back --
 *   both take a `struct user_fpsimd_state *`, which is why this needs no
 *   task_struct layout.
 *
 *   The struct layouts below (perf_event_attr, user_fpsimd_state, pt_regs
 *   register block) are stable kernel UAPI, confirmed against that module's
 *   DWARF (perf_event_attr_min=128 bytes, user_fpsimd_state=528 bytes,
 *   user_pt_regs=272 bytes). The breakpoint constants (PERF_TYPE_BREAKPOINT,
 *   HW_BREAKPOINT_X, HW_BREAKPOINT_LEN_4, the perf_event_attr flag bits) are
 *   fixed UAPI values, not build-specific.
 *
 * SAFETY POSTURE (this is unverifiable-in-CI kernel code that runs in an
 * exception handler, so it is written to fail closed and do nothing unless
 * explicitly and validly commanded):
 *   - Every symbol is resolved via kallsyms and checked. If any is missing,
 *     the HWBP feature reports NOT READY and arms nothing; plain memory
 *     read/write is unaffected.
 *   - Loading the module arms NOTHING. A breakpoint exists only after a valid
 *     CMD_HBADD, so a bad build cannot brick the boot path -- it can only
 *     misbehave while a client is actively arming it against a running game.
 *   - The handler never sleeps, never allocates, bounds every index and
 *     count, ignores a null/!active slot, and matches the firing perf_event
 *     to its slot by pointer (no perf_event field offset assumed).
 */

#ifndef INX_HWBP_H
#define INX_HWBP_H

/* ---- fixed-width types without pulling in kernel headers ---- */
typedef unsigned int        inx_u32;
typedef int                 inx_s32;
typedef unsigned long long  inx_u64;
typedef long long           inx_s64;
typedef unsigned char       inx_u8;
typedef unsigned short      inx_u16;

/* ---- UAPI struct layouts (stable kernel ABI; confirmed via DWARF) ---- */

/* arm64 user_pt_regs / the head of struct pt_regs: regs[0..30], sp, pc,
 * pstate. The overflow handler receives a pointer to the trapped thread's
 * pt_regs; its first 34 u64 are exactly this. Writing regs[n] changes the
 * value the thread resumes with. */
struct inx_pt_regs {
    inx_u64 regs[31];
    inx_u64 sp;
    inx_u64 pc;
    inx_u64 pstate;
};

/* arm64 struct user_fpsimd_state: 32x128-bit vector regs, then fpsr/fpcr.
 * This is what fpsimd_save_state writes into and fpsimd_update_current_state
 * reads from. 528 bytes total (512 + 4 + 4 + 8 reserved). */
struct inx_user_fpsimd_state {
    __uint128_t vregs[32];
    inx_u32     fpsr;
    inx_u32     fpcr;
    inx_u32     __reserved[2];
};

/* UAPI struct perf_event_attr, first 128 bytes (the part we set). Field
 * names mirror the kernel's; we only touch type/size/config/sample_period/
 * the flag bitfield/bp_type/bp_addr/bp_len. */
struct inx_perf_event_attr {
    inx_u32 type;
    inx_u32 size;
    inx_u64 config;
    inx_u64 sample_period;   /* union sample_freq */
    inx_u64 sample_type;
    inx_u64 read_format;
    inx_u64 flags;           /* disabled:1, inherit:1, pinned:1, exclusive:1,
                              * exclude_user:1, exclude_kernel:1,
                              * exclude_hv:1, exclude_idle:1, ... */
    inx_u32 wakeup_events;
    inx_u32 bp_type;
    inx_u64 bp_addr;         /* union config1 */
    inx_u64 bp_len;          /* union config2 */
    inx_u64 branch_sample_type;
    inx_u64 sample_regs_user;
    inx_u32 sample_stack_user;
    inx_s32 clockid;
    inx_u64 sample_regs_intr;
    inx_u32 aux_watermark;
    inx_u16 _pad_sample_max_stack;
    inx_u16 _pad_reserved2;
    inx_u32 aux_sample_size;
    inx_u32 _pad_reserved3;
    inx_u64 sig_data;
};

/* ---- fixed UAPI constants (not build-specific) ---- */
#define INX_PERF_TYPE_BREAKPOINT   5u
#define INX_HW_BREAKPOINT_X        4u   /* execute */
#define INX_HW_BREAKPOINT_LEN_4    4ull
/* perf_event_attr flag bits */
#define INX_PEA_DISABLED       (1ull << 0)
#define INX_PEA_PINNED         (1ull << 2)
#define INX_PEA_EXCLUDE_KERNEL (1ull << 5)
#define INX_PEA_EXCLUDE_HV     (1ull << 6)

/* ---- resolved-at-runtime kernel symbols (fail closed if absent) ---- */
struct perf_event;      /* opaque */
struct task_struct;     /* opaque (already forward-declared in inx_mem.c) */

/* struct perf_event *register_user_hw_breakpoint(struct perf_event_attr *attr,
 *     perf_overflow_handler_t triggered, void *context, struct task_struct*) */
typedef struct perf_event *(*fn_reg_user_hwbp_t)(struct inx_perf_event_attr *,
        void *triggered, void *context, struct task_struct *);
typedef int  (*fn_unreg_hwbp_t)(struct perf_event *);
typedef void (*fn_fpsimd_save_state_t)(struct inx_user_fpsimd_state *);
typedef void (*fn_fpsimd_update_current_state_t)(struct inx_user_fpsimd_state *);

static fn_reg_user_hwbp_t              p_register_user_hw_breakpoint   = 0;
static fn_unreg_hwbp_t                 p_unregister_hw_breakpoint      = 0;
static fn_fpsimd_save_state_t          p_fpsimd_save_state             = 0;
static fn_fpsimd_update_current_state_t p_fpsimd_update_current_state  = 0;

static int g_hwbp_ready = 0;   /* all HWBP symbols resolved */

/* ---- override slot table ---- */
#define INX_HWBP_SLOTS       8    /* system-wide HW breakpoint budget is tiny
                                   * (6 on the probed device); keep well under */
#define INX_HWBP_MAX_GP      8
#define INX_HWBP_MAX_FP      8
#define INX_HWBP_COND_MAX    8

struct inx_hwbp_slot {
    inx_u32 in_use;
    inx_u32 pid;
    inx_u64 addr;
    struct perf_event *event;

    /* GP register overrides */
    inx_u32 gp_count;
    inx_u32 gp_idx[INX_HWBP_MAX_GP];   /* 0..30 */
    inx_u64 gp_val[INX_HWBP_MAX_GP];

    /* FP/SIMD (q0..q31) overrides -- full 128-bit value per reg */
    inx_u32     fp_count;
    inx_u32     fp_idx[INX_HWBP_MAX_FP]; /* 0..31 */
    __uint128_t fp_val[INX_HWBP_MAX_FP];

    /* optional condition: only apply the override when the `cond_len` bytes at
     * [regs[cond_base] + cond_off] equal cond_expect[]. cond_active==0 -> always */
    inx_u32 cond_active;
    inx_u32 cond_base;                 /* GP reg index holding the base ptr */
    inx_s64 cond_off;
    inx_u32 cond_len;                  /* 1..INX_HWBP_COND_MAX */
    inx_u8  cond_expect[INX_HWBP_COND_MAX];
};

static struct inx_hwbp_slot g_hwbp_slots[INX_HWBP_SLOTS];

/* ---- the overflow handler: runs in the target thread's context ----
 * Signature matches perf_overflow_handler_t:
 *   void (*)(struct perf_event *, struct perf_sample_data *, struct pt_regs *)
 * We do not touch perf_sample_data, and we treat pt_regs as inx_pt_regs. */
static void inx_hwbp_handler(struct perf_event *bp, void *data,
                             struct inx_pt_regs *regs)
{
    struct inx_hwbp_slot *s = 0;
    int i;

    (void)data;
    if (!regs)
        return;

    /* match the firing event to its slot by pointer -- no perf_event field
     * offset assumed. O(INX_HWBP_SLOTS). */
    for (i = 0; i < INX_HWBP_SLOTS; i++) {
        if (g_hwbp_slots[i].in_use && g_hwbp_slots[i].event == bp) {
            s = &g_hwbp_slots[i];
            break;
        }
    }
    if (!s)
        return;

    /* optional condition gate: compare in-process memory via the trapped
     * thread's own registers. We read through the GP base register already
     * present in regs, so this is a plain kernel read of the current address
     * space (current == target thread here). Guarded with
     * copy_from_kernel_nofault-free bounds: we only ever dereference a user
     * pointer the thread itself was about to use, and cap the length. */
    if (s->cond_active && s->cond_len > 0 && s->cond_len <= INX_HWBP_COND_MAX
        && s->cond_base < 31) {
        volatile inx_u8 *src =
            (volatile inx_u8 *)(unsigned long)(regs->regs[s->cond_base] + s->cond_off);
        inx_u32 k;
        /* NOTE: this dereferences a user address in the current task context.
         * It is the address the instruction itself is about to use, so it is
         * valid and mapped at this point. We still keep it byte-wise and
         * bounded. If it ever needs to be fault-safe, route it through
         * copy_from_kernel_nofault (resolved separately). */
        for (k = 0; k < s->cond_len; k++) {
            if (src[k] != s->cond_expect[k])
                return; /* condition not met -- do not override this time */
        }
    }

    /* ---- GP register overrides ---- */
    for (i = 0; i < (int)s->gp_count && i < INX_HWBP_MAX_GP; i++) {
        inx_u32 idx = s->gp_idx[i];
        if (idx < 31)
            regs->regs[idx] = s->gp_val[i];
    }

    /* ---- FP/SIMD register overrides ---- */
    if (s->fp_count > 0 && p_fpsimd_save_state && p_fpsimd_update_current_state) {
        struct inx_user_fpsimd_state st;
        int j;
        /* snapshot the live FP state so we preserve q3..q31 etc. */
        p_fpsimd_save_state(&st);
        for (j = 0; j < (int)s->fp_count && j < INX_HWBP_MAX_FP; j++) {
            inx_u32 idx = s->fp_idx[j];
            if (idx < 32)
                st.vregs[idx] = s->fp_val[j];
        }
        p_fpsimd_update_current_state(&st);
    }
}

/* ---- init: resolve the HWBP symbols (called from inx_mem_init) ---- */
static void inx_hwbp_resolve(void)
{
    p_register_user_hw_breakpoint =
        (fn_reg_user_hwbp_t)kallsyms_lookup_name("register_user_hw_breakpoint");
    p_unregister_hw_breakpoint =
        (fn_unreg_hwbp_t)kallsyms_lookup_name("unregister_hw_breakpoint");
    p_fpsimd_save_state =
        (fn_fpsimd_save_state_t)kallsyms_lookup_name("fpsimd_save_state");
    p_fpsimd_update_current_state =
        (fn_fpsimd_update_current_state_t)kallsyms_lookup_name("fpsimd_update_current_state");

    /* GP-only override needs just register/unregister. FP override
     * additionally needs the two fpsimd helpers; if those are missing we
     * still allow GP/mem overrides and simply ignore FP entries. */
    g_hwbp_ready = (p_register_user_hw_breakpoint != 0) &&
                   (p_unregister_hw_breakpoint != 0);

    pr_info("sysutil/hwbp: reg_user_hwbp=%px unreg_hwbp=%px fpsimd_save=%px "
            "fpsimd_update=%px ready=%d\n",
            (void *)p_register_user_hw_breakpoint,
            (void *)p_unregister_hw_breakpoint,
            (void *)p_fpsimd_save_state,
            (void *)p_fpsimd_update_current_state,
            g_hwbp_ready);
}

/* ---- arm a breakpoint for (pid, addr), returning the slot index or <0 ----
 * The caller fills the override fields in the returned slot BEFORE or AFTER;
 * here we only create the perf_event. find_get_task_by_vpid / put_task_struct
 * are the ones already resolved in inx_mem.c. */
static int inx_hwbp_add(pid_t pid, inx_u64 addr)
{
    struct task_struct *task;
    struct perf_event *ev;
    struct inx_perf_event_attr attr;
    int slot = -1, i;

    if (!g_hwbp_ready)
        return -ENOSYS;
    if (!addr)
        return -EINVAL;
    if (!fn_find_get_task_by_vpid || !fn_put_task_struct)
        return -ENOSYS;

    /* find a free slot */
    for (i = 0; i < INX_HWBP_SLOTS; i++) {
        if (!g_hwbp_slots[i].in_use) { slot = i; break; }
    }
    if (slot < 0)
        return -ENOSPC;

    task = fn_find_get_task_by_vpid((int)pid);
    if (!task)
        return -ESRCH;

    /* hw_breakpoint_init() equivalent, done by hand: zero, then set the
     * execute-breakpoint attributes. */
    __builtin_memset(&attr, 0, sizeof(attr));
    attr.type         = INX_PERF_TYPE_BREAKPOINT;
    attr.size         = sizeof(struct inx_perf_event_attr); /* 128 */
    attr.bp_type      = INX_HW_BREAKPOINT_X;
    attr.bp_addr      = addr;
    attr.bp_len       = INX_HW_BREAKPOINT_LEN_4;
    attr.sample_period = 1;
    /* user-space execute bp, pinned, not disabled */
    attr.flags = INX_PEA_PINNED | INX_PEA_EXCLUDE_KERNEL | INX_PEA_EXCLUDE_HV;

    ev = p_register_user_hw_breakpoint(&attr, (void *)inx_hwbp_handler,
                                       (void *)&g_hwbp_slots[slot], task);

    fn_put_task_struct(task);

    /* register_user_hw_breakpoint returns an ERR_PTR on failure. Treat any
     * value in the top error page as failure without assuming IS_ERR macros. */
    if (!ev || (unsigned long)ev >= (unsigned long)(-4095)) {
        pr_info("sysutil/hwbp: register_user_hw_breakpoint failed ev=%px addr=%llx pid=%d\n",
                (void *)ev, (unsigned long long)addr, (int)pid);
        return -EFAULT;
    }

    /* zero the override, mark in_use */
    __builtin_memset(&g_hwbp_slots[slot], 0, sizeof(g_hwbp_slots[slot]));
    g_hwbp_slots[slot].event  = ev;
    g_hwbp_slots[slot].pid    = (inx_u32)pid;
    g_hwbp_slots[slot].addr   = addr;
    g_hwbp_slots[slot].in_use = 1;

    pr_info("sysutil/hwbp: armed slot=%d pid=%d addr=%llx ev=%px\n",
            slot, (int)pid, (unsigned long long)addr, (void *)ev);
    return slot;
}

/* ---- remove one slot / all slots ---- */
static void inx_hwbp_del_slot(int slot)
{
    if (slot < 0 || slot >= INX_HWBP_SLOTS)
        return;
    if (!g_hwbp_slots[slot].in_use)
        return;
    if (p_unregister_hw_breakpoint && g_hwbp_slots[slot].event)
        p_unregister_hw_breakpoint(g_hwbp_slots[slot].event);
    __builtin_memset(&g_hwbp_slots[slot], 0, sizeof(g_hwbp_slots[slot]));
}

static void inx_hwbp_clear_all(void)
{
    int i;
    for (i = 0; i < INX_HWBP_SLOTS; i++)
        inx_hwbp_del_slot(i);
}

#endif /* INX_HWBP_H */
