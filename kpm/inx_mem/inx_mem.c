/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * inx_mem -- standalone KernelPatch KPM: cross-process memory read/write.
 *
 * SCOPE (deliberate, see repo notes): this KPM does ONLY memory read/write
 * for a pid the caller already resolved. PID lookup and module-base lookup
 * stay in userspace (/proc/<pid>/maps) because this kernel's VMA structure
 * (maple-tree, kernel 6.6+) is not something we can safely hand-walk without
 * real device kernel headers, which we don't have.
 *
 * Every kernel symbol used here is resolved at runtime via
 * kallsyms_lookup_name() and explicitly checked before use. Nothing is
 * assumed present -- if a symbol this module needs isn't found on the
 * running kernel, this module reports itself NOT READY instead of silently
 * doing something unsafe.
 */

#include <compiler.h>
#include <kpmodule.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <common.h>
#include <kputils.h>
#include <ksyms.h>
#include <linux/kallsyms.h>
#include <linux/errno.h>

/* Only ever used as an opaque pointer below -- never dereferenced, so no
 * field layout of task_struct is assumed anywhere in this file. That is
 * also exactly why this stays a forward declaration instead of
 * #include <linux/sched.h>: pulling in the full definition would invite
 * someone later to access a field directly "for convenience" without
 * realizing every field offset here is kernel-build-specific. */
struct task_struct;

/* ---- resolved-at-runtime kernel symbols, never assumed present ---- */

/* struct task_struct *find_get_task_by_vpid(pid_t nr);
 * Real, long-stable kernel/pid.c function: does rcu_read_lock + pid lookup
 * + a REAL get_task_struct() all inside actual compiled kernel code, so we
 * never touch task_struct's internal layout ourselves. */
typedef struct task_struct *(*fn_find_get_task_by_vpid_t)(int nr);
static fn_find_get_task_by_vpid_t fn_find_get_task_by_vpid = 0;

/* void put_task_struct(struct task_struct *t);
 * NOT guaranteed to exist as a standalone symbol -- in many kernel builds
 * this is a header-only static inline (refcount_dec_and_test on a field we
 * don't know the offset of), which means it may be fully inlined away with
 * no kallsyms entry at all. We only use it if kallsyms actually finds it;
 * if it doesn't, this module stays NOT READY rather than leak references
 * or guess the field offset. */
typedef void (*fn_put_task_struct_t)(struct task_struct *t);
static fn_put_task_struct_t fn_put_task_struct = 0;

/* NOTE: access_process_vm has been removed entirely. Cross-process memory
 * read/write is now done ONLY by the page-table-walk backend in
 * inx_ptewalk.h (Incognito's method) -- no user-access entry point is called
 * at all. See inx_vm_copy below. */

static int g_ready = 0;       /* all required symbols resolved */
static int g_has_put = 0;     /* put_task_struct specifically resolved (see above) */

/* Hardware-breakpoint register/memory override engine. Included AFTER the
 * fn_find_get_task_by_vpid / fn_put_task_struct declarations above, which it
 * reuses. Self-contained otherwise; see inx_hwbp.h for the full design and
 * safety notes. Loading the module arms nothing -- a breakpoint exists only
 * after a valid "hbadd" command. */
#include "inx_hwbp.h"

/* Page-table-walk read/write backend (Incognito's method). Included AFTER
 * fn_access_process_vm and the INX_FOLL_* defines above, which its
 * calibration cross-check reuses. Loading it resolves nothing by itself;
 * inx_ptewalk_resolve() in init does that, and it stays fully off unless
 * every symbol it needs is present. */
#include "inx_ptewalk.h"

/* In-kernel pid-by-name + module-base lookup (Incognito's two functional
 * extras). Included after the task-lookup symbols it reuses. Pure VFS, no
 * struct offsets. See inx_proc.h. */
#include "inx_proc.h"

/* ---- bounded, gated memory copy ---- */

/* Hard bound -- never trust caller-supplied length blindly. Kept small
 * (not KB-scale) on purpose: this and its hex-encoded output buffer both
 * live on the KERNEL STACK in control0 below, and kernel thread stacks are
 * commonly only 16KB total on arm64 -- a 4KB buffer plus its 8KB+1 hex
 * encoding would have been a real stack-overflow risk. 512 bytes is far
 * more than any single game-memory read/write in this project needs
 * (floats, vectors, pointers are all a few bytes to a few dozen). */
#define INX_MAX_COPY 512

static long inx_vm_copy(pid_t pid, unsigned long addr, void *kbuf, int len, int do_write)
{
    struct task_struct *task;
    int ret;

    if (!g_ready)
        return -ENOSYS; /* fail closed: required kernel symbols not resolved on this build */
    if (len <= 0 || len > INX_MAX_COPY)
        return -EINVAL;
    if (!addr)
        return -EINVAL;

    task = fn_find_get_task_by_vpid((int)pid);
    if (!task)
        return -ESRCH; /* no such pid, or it exited between lookup and here */

    /* Only path: Incognito-style page-table walk (no access_process_vm, no
     * user-access, PAN-safe). It self-calibrates the mm->pgd offset on first
     * use (structural + stability checks, see inx_pte_calibrate) and locks
     * it. If the backend is not ready, cannot calibrate on this kernel, or
     * the target has no mm, it returns INX_PTE_FALLBACK -- which is now a
     * hard failure since there is no other backend. */
    ret = inx_ptewalk_rw(task, addr, kbuf, len, do_write);
    if (ret == INX_PTE_FALLBACK)
        ret = -ENOSYS;

    /* Always release what find_get_task_by_vpid acquired. If put_task_struct
     * wasn't resolvable we would never have reached here -- g_ready requires
     * g_has_put, see inx_mem_init -- so this call is always valid here. */
    fn_put_task_struct(task);

    return ret;
}

/* ---- tiny hex helpers (control0's wire format is plain hex ASCII) ---- */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex_decode(const char *hex, unsigned char *out, int out_cap)
{
    int n = 0;
    while (hex[0] && hex[1]) {
        int hi = hexval(hex[0]);
        int lo = hexval(hex[1]);
        if (hi < 0 || lo < 0) return -1;
        if (n >= out_cap) return -1;
        out[n++] = (unsigned char)((hi << 4) | lo);
        hex += 2;
    }
    return n;
}

static void hex_encode(const unsigned char *in, int len, char *out /* needs len*2+1 */)
{
    static const char hx[] = "0123456789abcdef";
    int i;
    for (i = 0; i < len; i++) {
        out[i * 2] = hx[(in[i] >> 4) & 0xF];
        out[i * 2 + 1] = hx[in[i] & 0xF];
    }
    out[len * 2] = '\0';
}

/* ---- KPM entry points ---- */

KPM_NAME("hypervisor");
KPM_VERSION("1.0.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("jeanna");
KPM_DESCRIPTION("KVM based");

static long inx_mem_init(const char *args, const char *event, void *__user reserved)
{
    (void)args;
    (void)event;
    (void)reserved;

    fn_find_get_task_by_vpid = (fn_find_get_task_by_vpid_t)kallsyms_lookup_name("find_get_task_by_vpid");
    fn_put_task_struct = (fn_put_task_struct_t)kallsyms_lookup_name("put_task_struct");

    g_has_put = fn_put_task_struct != 0;

    /* Resolve the page-table-walk backend (sets g_pte_ready). Done before
     * the readiness check below so the module can be READY on a kernel that
     * has the pte-walk symbols even if access_process_vm is absent. */
    inx_ptewalk_resolve();

    /* Fail CLOSED, not degraded: leaking a task_struct reference on every
     * read/write call (if we proceeded without put_task_struct) would pin
     * exited processes' task_structs forever -- a real, cumulative kernel
     * memory leak and eventual instability under the repeated-call load
     * this is actually used under (aim loops poll many times a second).
     * That is strictly worse than simply not working.
     *
     * task lookup + release plus the pte-walk backend are all required now
     * that access_process_vm is gone. */
    g_ready = fn_find_get_task_by_vpid && g_has_put && g_pte_ready;

    pr_info("hypervisor: find_get_task_by_vpid=%px put_task_struct=%px "
            "pte_ready=%d ready=%d\n",
            (void *)fn_find_get_task_by_vpid,
            (void *)fn_put_task_struct, g_pte_ready, g_ready);

    if (!g_ready) {
        pr_info("hypervisor: NOT READY on this kernel build -- one or more required "
                "symbols were not found via kallsyms. memory read/write via this "
                "KPM is disabled; the client should fall back to its other "
                "drivers. Check 'dmesg | grep inx-mem' for which symbol(s) are "
                "0x0 above and report that line.\n");
    }

    /* Resolve the HWBP-override symbols separately. This is independent of
     * the mem r/w readiness above: if these are missing, mem r/w still works
     * and only the "hb*" commands report NOT READY. Arms nothing here. */
    inx_hwbp_resolve();

    /* Resolve the pid-by-name / module-base VFS symbols. Independent too:
     * if missing, only the "pidof"/"modbase" commands report failure. */
    inx_proc_resolve();

    return 0;
}

/*
 * control0 wire protocol (plain ASCII, parsed from userspace's
 * sc_kpm_control ctl_args):
 *
 *   "ping"
 *     -> out_msg = "pong"
 *
 *   "status"
 *     -> out_msg = "ready" or "not_ready"
 *
 *   "r <pid_dec> <addr_hex> <len_dec>"
 *     -> out_msg = hex-encoded bytes read (empty on failure), return value
 *        is the access_process_vm result (negative errno or byte count)
 *
 *   "w <pid_dec> <addr_hex> <hexbytes>"
 *     -> out_msg untouched, return value is the access_process_vm result
 */
static long inx_mem_control0(const char *args, char *__user out_msg, int outlen)
{
    char local_out[INX_MAX_COPY * 2 + 1];
    unsigned char buf[INX_MAX_COPY];

    if (!args)
        return -EINVAL;

    if (!strncmp(args, "ping", 4)) {
        compat_copy_to_user(out_msg, "pong", 5);
        return 0;
    }

    if (!strncmp(args, "status", 6)) {
        const char *s = g_ready ? "ready" : "not_ready";
        compat_copy_to_user(out_msg, s, strlen(s) + 1);
        return 0;
    }

    if (args[0] == 'r' && args[1] == ' ') {
        long pid_l = 0, len_l = 0;
        unsigned long addr = 0;
        const char *p = args + 2;

        /* pid (decimal) */
        while (*p >= '0' && *p <= '9') { pid_l = pid_l * 10 + (*p - '0'); p++; }
        while (*p == ' ') p++;
        /* addr (hex, optional 0x prefix) */
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
        while (hexval(*p) >= 0) { addr = (addr << 4) | (unsigned long)hexval(*p); p++; }
        while (*p == ' ') p++;
        /* len (decimal) */
        while (*p >= '0' && *p <= '9') { len_l = len_l * 10 + (*p - '0'); p++; }

        if (len_l <= 0 || len_l > INX_MAX_COPY)
            return -EINVAL;

        long ret = inx_vm_copy((pid_t)pid_l, addr, buf, (int)len_l, 0);
        if (ret > 0) {
            hex_encode(buf, (int)ret, local_out);
            compat_copy_to_user(out_msg, local_out, (int)(ret * 2 + 1) < outlen ? (int)(ret * 2 + 1) : outlen);
        }
        return ret;
    }

    if (args[0] == 'w' && args[1] == ' ') {
        long pid_l = 0;
        unsigned long addr = 0;
        const char *p = args + 2;

        while (*p >= '0' && *p <= '9') { pid_l = pid_l * 10 + (*p - '0'); p++; }
        while (*p == ' ') p++;
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
        while (hexval(*p) >= 0) { addr = (addr << 4) | (unsigned long)hexval(*p); p++; }
        while (*p == ' ') p++;

        int n = hex_decode(p, buf, INX_MAX_COPY);
        if (n <= 0)
            return -EINVAL;

        return inx_vm_copy((pid_t)pid_l, addr, buf, n, 1);
    }

    /* ---- in-kernel pid-by-name / module-base (Incognito's extras) ---- */

    /* "pidof <name>" -> out_msg = decimal pid string; return value is the
     * pid, or -ESRCH if no process cmdline contains <name>. */
    if (!strncmp(args, "pidof ", 6)) {
        const char *name = args + 6;
        int pid = inx_find_pid_by_name(name);
        char num[16]; int ni = 0, v;
        if (pid < 0)
            return -ESRCH;
        v = pid;
        if (v == 0) { num[ni++] = '0'; }
        else { char tmp[16]; int t = 0; while (v > 0 && t < 15) { tmp[t++] = '0' + (v % 10); v /= 10; }
               while (t > 0) num[ni++] = tmp[--t]; }
        num[ni] = '\0';
        compat_copy_to_user(out_msg, num, ni + 1);
        return pid;
    }

    /* "modbase <pid_dec> <lib>" -> out_msg = lowercase hex base (no 0x);
     * return 0 on success, -ENOENT if no mapping path contains <lib>. */
    if (!strncmp(args, "modbase ", 8)) {
        long pid_l = 0;
        const char *p = args + 8;
        unsigned long base;
        char hx[20]; int hi = 0, started = 0, shift;

        while (*p >= '0' && *p <= '9') { pid_l = pid_l * 10 + (*p - '0'); p++; }
        while (*p == ' ') p++;
        if (!*p)
            return -EINVAL; /* no library name */

        base = inx_module_base((int)pid_l, p);
        if (!base)
            return -ENOENT;

        for (shift = 60; shift >= 0; shift -= 4) {
            int nyb = (int)((base >> shift) & 0xf);
            if (nyb || started || shift == 0) {
                hx[hi++] = nyb < 10 ? ('0' + nyb) : ('a' + nyb - 10);
                started = 1;
            }
        }
        hx[hi] = '\0';
        compat_copy_to_user(out_msg, hx, hi + 1);
        return 0;
    }

    /* ---- hardware-breakpoint override commands ---- */

    if (!strncmp(args, "hbstatus", 8)) {
        const char *s = g_hwbp_ready ? "ready" : "not_ready";
        compat_copy_to_user(out_msg, s, strlen(s) + 1);
        return 0;
    }

    /* "hbadd <pid_dec> <addr_hex>" -> out_msg = slot index (decimal string),
     * return value is the slot index or a negative errno. */
    if (!strncmp(args, "hbadd ", 6)) {
        long pid_l = 0;
        unsigned long addr = 0;
        const char *p = args + 6;
        int slot;
        char num[16];
        int ni = 0, v;

        while (*p >= '0' && *p <= '9') { pid_l = pid_l * 10 + (*p - '0'); p++; }
        while (*p == ' ') p++;
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
        while (hexval(*p) >= 0) { addr = (addr << 4) | (unsigned long)hexval(*p); p++; }

        slot = inx_hwbp_add((pid_t)pid_l, (inx_u64)addr);
        if (slot < 0)
            return slot;

        /* encode slot index as a short decimal string */
        v = slot;
        if (v == 0) { num[ni++] = '0'; }
        else { char tmp[16]; int t = 0; while (v > 0 && t < 15) { tmp[t++] = '0' + (v % 10); v /= 10; }
               while (t > 0) num[ni++] = tmp[--t]; }
        num[ni] = '\0';
        compat_copy_to_user(out_msg, num, ni + 1);
        return slot;
    }

    /* "hbset <slot_dec> <tokens...>" rewrites the override of an existing slot
     * WITHOUT re-registering the perf_event -- this is the per-frame hot path.
     * Tokens (space separated):
     *   g<idx>=<hex>   GP reg override, idx 0..30, value up to 16 hex digits
     *   f<idx>=<hex>   FP reg override, idx 0..31, value up to 32 hex digits
     *                  (for s<idx> float, low 32 bits = the float's bit pattern)
     *   c<base>:<off_hex>:<expect_hex>  condition: only apply when the
     *                  expect bytes match [regs[base]+off]
     *   (no c token clears any previous condition)
     */
    if (!strncmp(args, "hbset ", 6)) {
        long slot_l = 0;
        const char *p = args + 6;
        struct inx_hwbp_slot *s;
        inx_u32 gp_n = 0, fp_n = 0;
        inx_u32 gp_i[INX_HWBP_MAX_GP]; inx_u64 gp_v[INX_HWBP_MAX_GP];
        inx_u32 fp_i[INX_HWBP_MAX_FP]; __uint128_t fp_v[INX_HWBP_MAX_FP];
        inx_u32 cond_on = 0, cond_base = 0, cond_len = 0;
        inx_s64 cond_off = 0;
        inx_u8  cond_exp[INX_HWBP_COND_MAX];

        while (*p >= '0' && *p <= '9') { slot_l = slot_l * 10 + (*p - '0'); p++; }
        if (slot_l < 0 || slot_l >= INX_HWBP_SLOTS)
            return -EINVAL;
        s = &g_hwbp_slots[slot_l];
        if (!s->in_use)
            return -ENOENT;

        while (*p) {
            while (*p == ' ') p++;
            if (!*p) break;

            if ((*p == 'g' || *p == 'f')) {
                int is_fp = (*p == 'f');
                unsigned idx = 0;
                p++;
                while (*p >= '0' && *p <= '9') { idx = idx * 10 + (*p - '0'); p++; }
                if (*p != '=') return -EINVAL;
                p++;
                if (!is_fp) {
                    inx_u64 val = 0;
                    while (hexval(*p) >= 0) { val = (val << 4) | (inx_u64)hexval(*p); p++; }
                    if (idx < 31 && gp_n < INX_HWBP_MAX_GP) {
                        gp_i[gp_n] = idx; gp_v[gp_n] = val; gp_n++;
                    }
                } else {
                    __uint128_t val = 0;
                    while (hexval(*p) >= 0) { val = (val << 4) | (__uint128_t)hexval(*p); p++; }
                    if (idx < 32 && fp_n < INX_HWBP_MAX_FP) {
                        fp_i[fp_n] = idx; fp_v[fp_n] = val; fp_n++;
                    }
                }
            } else if (*p == 'c') {
                unsigned base = 0; inx_s64 off = 0; int neg = 0; int bn = 0;
                p++;
                while (*p >= '0' && *p <= '9') { base = base * 10 + (*p - '0'); p++; }
                if (*p != ':') return -EINVAL;
                p++;
                if (*p == '-') { neg = 1; p++; }
                while (hexval(*p) >= 0) { off = (off << 4) | hexval(*p); p++; }
                if (neg) off = -off;
                if (*p != ':') return -EINVAL;
                p++;
                /* expect bytes, hex pairs */
                while (hexval(p[0]) >= 0 && hexval(p[1]) >= 0 && bn < INX_HWBP_COND_MAX) {
                    cond_exp[bn++] = (inx_u8)((hexval(p[0]) << 4) | hexval(p[1]));
                    p += 2;
                }
                cond_on = 1; cond_base = base; cond_off = off; cond_len = (inx_u32)bn;
            } else {
                /* unknown token -- skip to next space to stay robust */
                while (*p && *p != ' ') p++;
            }
        }

        /* publish into the slot so the handler sees a consistent view:
         * drop counts to 0 first, write arrays, then set counts last. */
        s->gp_count = 0; s->fp_count = 0; s->cond_active = 0;
        __asm__ __volatile__("" ::: "memory");
        {
            inx_u32 k;
            for (k = 0; k < gp_n; k++) { s->gp_idx[k] = gp_i[k]; s->gp_val[k] = gp_v[k]; }
            for (k = 0; k < fp_n; k++) { s->fp_idx[k] = fp_i[k]; s->fp_val[k] = fp_v[k]; }
            if (cond_on) {
                s->cond_base = cond_base; s->cond_off = cond_off;
                s->cond_len = cond_len;
                for (k = 0; k < cond_len; k++) s->cond_expect[k] = cond_exp[k];
            }
        }
        __asm__ __volatile__("" ::: "memory");
        s->gp_count = gp_n; s->fp_count = fp_n; s->cond_active = cond_on;
        return 0;
    }

    if (!strncmp(args, "hbdel ", 6)) {
        long slot_l = 0;
        const char *p = args + 6;
        while (*p >= '0' && *p <= '9') { slot_l = slot_l * 10 + (*p - '0'); p++; }
        if (slot_l < 0 || slot_l >= INX_HWBP_SLOTS)
            return -EINVAL;
        inx_hwbp_del_slot((int)slot_l);
        return 0;
    }

    if (!strncmp(args, "hbclr", 5)) {
        inx_hwbp_clear_all();
        return 0;
    }

    return -EINVAL;
}

static long inx_mem_control1(void *a1, void *a2, void *a3)
{
    (void)a1; (void)a2; (void)a3;
    return -ENOSYS; /* unused -- all control goes through control0's text protocol */
}

static long inx_mem_exit(void *__user reserved)
{
    (void)reserved;
    pr_info("hypervisor: exit\n");
    return 0;
}

KPM_INIT(inx_mem_init);
KPM_CTL0(inx_mem_control0);
KPM_CTL1(inx_mem_control1);
KPM_EXIT(inx_mem_exit);
