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

/* int access_process_vm(struct task_struct *tsk, unsigned long addr,
 *                        void *buf, int len, unsigned int gup_flags);
 * Long-stable exported mm/process_vm_access.c (or mm/memory.c, version-
 * dependent file but stable symbol name/signature) helper -- this is the
 * real kernel path used by /proc/<pid>/mem, ptrace, etc. Does its own
 * page-fault/COW handling correctly regardless of internal VMA structure. */
typedef int (*fn_access_process_vm_t)(struct task_struct *tsk, unsigned long addr,
                                       void *buf, int len, unsigned int gup_flags);
static fn_access_process_vm_t fn_access_process_vm = 0;

/* FOLL_WRITE / FOLL_FORCE -- real values from include/linux/mm_types.h
 * (confirmed against actual upstream kernel source, not guessed):
 *   FOLL_WRITE = 0x01   "check pte is writable"
 *   FOLL_FORCE = 0x10   "get_user_pages read/write w/o permission"
 * FOLL_FORCE is NOT optional here: every real in-tree caller of
 * access_process_vm() against a task that is not "current" (ptrace's
 * PEEKTEXT/POKETEXT handlers, one per architecture under arch dot dot
 * dot ptrace.c) passes it for BOTH reads and writes, not just writes --
 * without it, GUP enforces the normal page-permission rules as if the
 * CURRENT process were the one touching the page, which is not what a
 * cross-process reader wants and would make most reads fail as often
 * as writes. */
#define INX_FOLL_WRITE 0x01
#define INX_FOLL_FORCE 0x10

static int g_ready = 0;       /* all required symbols resolved */
static int g_has_put = 0;     /* put_task_struct specifically resolved (see above) */

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

    ret = fn_access_process_vm(task, addr, kbuf, len,
                                INX_FOLL_FORCE | (do_write ? INX_FOLL_WRITE : 0));

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

KPM_NAME("sysutil");
KPM_VERSION("1.0.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("@PFSR999");
KPM_DESCRIPTION("InvisibleX standalone memory read/write KPM");

static long inx_mem_init(const char *args, const char *event, void *__user reserved)
{
    (void)args;
    (void)event;
    (void)reserved;

    fn_find_get_task_by_vpid = (fn_find_get_task_by_vpid_t)kallsyms_lookup_name("find_get_task_by_vpid");
    fn_access_process_vm = (fn_access_process_vm_t)kallsyms_lookup_name("access_process_vm");
    fn_put_task_struct = (fn_put_task_struct_t)kallsyms_lookup_name("put_task_struct");

    g_has_put = fn_put_task_struct != 0;

    /* Fail CLOSED, not degraded: leaking a task_struct reference on every
     * read/write call (if we proceeded without put_task_struct) would pin
     * exited processes' task_structs forever -- a real, cumulative kernel
     * memory leak and eventual instability under the repeated-call load
     * this is actually used under (aim loops poll many times a second).
     * That is strictly worse than simply not working. */
    g_ready = fn_find_get_task_by_vpid && fn_access_process_vm && g_has_put;

    pr_info("sysutil: find_get_task_by_vpid=%px access_process_vm=%px put_task_struct=%px ready=%d\n",
            (void *)fn_find_get_task_by_vpid, (void *)fn_access_process_vm,
            (void *)fn_put_task_struct, g_ready);

    if (!g_ready) {
        pr_info("sysutil: NOT READY on this kernel build -- one or more required "
                "symbols were not found via kallsyms. memory read/write via this "
                "KPM is disabled; the client should fall back to its other "
                "drivers. Check 'dmesg | grep inx-mem' for which symbol(s) are "
                "0x0 above and report that line.\n");
    }

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
    pr_info("sysutil: exit\n");
    return 0;
}

KPM_INIT(inx_mem_init);
KPM_CTL0(inx_mem_control0);
KPM_CTL1(inx_mem_control1);
KPM_EXIT(inx_mem_exit);
