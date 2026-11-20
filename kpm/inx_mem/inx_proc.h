/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INX_PROC_H
#define INX_PROC_H
/*
 * inx_proc -- in-kernel pid-by-name and module-base lookup, the two
 * functional extras the Incognito KPM has over a plain RW module.
 *
 * Both are done purely through stable VFS APIs resolved at runtime
 * (filp_open / iterate_dir / kernel_read / filp_close / get_cmdline), so NO
 * build-specific struct offset is ever touched or guessed:
 *
 *   pid-by-name : iterate /proc with a dir_context whose actor collects the
 *                 numeric entries, then read each pid's cmdline via
 *                 get_cmdline() and substring-match the wanted name. This is
 *                 Incognito's own method (its proc_dir_filldir actor returns
 *                 _Bool and its dir_context is {actor; pos;} -- matched here).
 *
 *   module-base : Incognito walks the target's VMAs with find_vma() and reads
 *                 vm_start / vm_file, which needs build-specific
 *                 vm_area_struct field offsets. Those are NOT invented here;
 *                 instead the same answer is read straight out of
 *                 /proc/<pid>/maps as text (filp_open + kernel_read), which
 *                 needs no offsets at all and gives the identical base.
 *
 * Nothing here is wired on by itself. inx_proc_resolve() in init resolves the
 * symbols; the two helpers report failure cleanly if anything is missing.
 */

/* ---- resolved-at-runtime VFS symbols ---- */
typedef void *(*fn_filp_open_t)(const char *name, int flags, unsigned short mode);
typedef int   (*fn_filp_close_t)(void *filp, void *id);
typedef long  (*fn_kernel_read_t)(void *file, void *buf, unsigned long count, long long *pos);
typedef int   (*fn_iterate_dir_t)(void *file, void *ctx);
typedef int   (*fn_get_cmdline_t)(struct task_struct *task, char *buf, int buflen);

static fn_filp_open_t   p_filp_open   = 0;
static fn_filp_close_t  p_filp_close  = 0;
static fn_kernel_read_t p_kernel_read = 0;
static fn_iterate_dir_t p_iterate_dir = 0;
static fn_get_cmdline_t p_get_cmdline = 0;

static int g_proc_ready = 0;

/* O_* flags we need (arch-stable on arm64 Linux). */
#define INX_O_RDONLY     0x00000
#define INX_O_DIRECTORY  0x04000

/* filldir_t on 6.x returns _Bool (true = keep going). dir_context is just
 * { actor; pos; } -- Incognito's dir_context_min, matched exactly so
 * iterate_dir drives our actor with the right ABI. The actor's 6th arg is
 * d_type; we ignore it. */
typedef int /*_Bool*/ (*inx_filldir_t)(void *ctx, const char *name, int namelen,
                                       long long offset, unsigned long long ino,
                                       unsigned int d_type);
struct inx_dir_context {
    inx_filldir_t actor;     /* offset 0  */
    long long     pos;       /* offset 8  */
};

/* Container the actor recovers from its ctx pointer (ctx is &c.dc, and dc is
 * the first member, so &dc == &container). */
#define INX_MAX_PIDS 64
struct inx_pid_collector {
    struct inx_dir_context dc;   /* MUST be first */
    int pids[INX_MAX_PIDS];
    int count;
};

static int inx_all_digits(const char *s, int len)
{
    int i;
    if (len <= 0)
        return 0;
    for (i = 0; i < len; i++)
        if (s[i] < '0' || s[i] > '9')
            return 0;
    return 1;
}

static long inx_atol_n(const char *s, int len)
{
    long v = 0;
    int i;
    for (i = 0; i < len; i++)
        v = v * 10 + (s[i] - '0');
    return v;
}

/* actor: record every numeric (pid) directory entry. */
static int inx_pid_fill(void *ctx, const char *name, int namelen,
                        long long offset, unsigned long long ino,
                        unsigned int d_type)
{
    struct inx_pid_collector *c = (struct inx_pid_collector *)ctx;
    (void)offset; (void)ino; (void)d_type;
    if (c->count < INX_MAX_PIDS && inx_all_digits(name, namelen))
        c->pids[c->count++] = (int)inx_atol_n(name, namelen);
    return 1; /* true: keep enumerating */
}

/* tiny substring match */
static int inx_contains(const char *hay, int haylen, const char *needle)
{
    int nl = 0, i, j;
    while (needle[nl])
        nl++;
    if (nl == 0)
        return 1;
    for (i = 0; i + nl <= haylen; i++) {
        for (j = 0; j < nl; j++)
            if (hay[i + j] != needle[j])
                break;
        if (j == nl)
            return 1;
    }
    return 0;
}

/* Return the first pid whose cmdline contains `name`, or -1. Uses the task
 * lookup symbols that inx_mem.c already resolved (this header is included
 * after them). */
static int inx_find_pid_by_name(const char *name)
{
    struct inx_pid_collector c;
    void *dirf;
    char cmd[256];
    int i, found = -1;

    if (!g_proc_ready || !fn_find_get_task_by_vpid || !fn_put_task_struct ||
        !p_get_cmdline)
        return -1;

    c.dc.actor = inx_pid_fill;
    c.dc.pos   = 0;
    c.count    = 0;

    dirf = p_filp_open("/proc", INX_O_RDONLY | INX_O_DIRECTORY, 0);
    /* filp_open returns an ERR_PTR on failure: anything in the top guard page
     * region (>= -4095) is an error, not a real pointer. */
    if (!dirf || (unsigned long)dirf >= (unsigned long)(-4095))
        return -1;

    p_iterate_dir(dirf, &c.dc);
    p_filp_close(dirf, 0);

    for (i = 0; i < c.count && found < 0; i++) {
        struct task_struct *task = fn_find_get_task_by_vpid(c.pids[i]);
        int n;
        if (!task)
            continue;
        n = p_get_cmdline(task, cmd, (int)sizeof(cmd) - 1);
        fn_put_task_struct(task);
        if (n <= 0)
            continue;
        if (n > (int)sizeof(cmd) - 1)
            n = (int)sizeof(cmd) - 1;
        /* get_cmdline packs args separated by NULs; match within the whole
         * buffer, treating NULs as ordinary bytes (the name usually sits in
         * argv[0]). */
        if (inx_contains(cmd, n, name))
            found = c.pids[i];
    }
    return found;
}

/* Read /proc/<pid>/maps and return the lowest start address of the first
 * mapping whose path contains `lib`. 0 on failure. No struct offsets: the
 * base comes straight from the maps text, exactly what userspace would read,
 * just done in-kernel. */
static unsigned long inx_module_base(int pid, const char *lib)
{
    char path[64];
    char buf[1024];
    char line[512];
    int linelen = 0;
    void *f;
    long long pos = 0;
    long n;
    unsigned long base = 0;

    if (!g_proc_ready || !p_kernel_read)
        return 0;

    /* build "/proc/<pid>/maps" without snprintf dependencies */
    {
        char num[16];
        int ni = 0, k = 0, t = 0, v = pid;
        const char *a = "/proc/";
        const char *b = "/maps";
        while (a[k]) path[k] = a[k], k++;
        if (v == 0) num[ni++] = '0';
        else { char tmp[16]; while (v > 0 && t < 15) { tmp[t++] = '0' + (v % 10); v /= 10; }
               while (t > 0) num[ni++] = tmp[--t]; }
        { int j; for (j = 0; j < ni; j++) path[k++] = num[j]; }
        { int j; for (j = 0; b[j]; j++) path[k++] = b[j]; }
        path[k] = '\0';
    }

    f = p_filp_open(path, INX_O_RDONLY, 0);
    if (!f || (unsigned long)f >= (unsigned long)(-4095))
        return 0;

    /* Read in chunks, reassemble lines, scan each for `lib`. */
    while ((n = p_kernel_read(f, buf, sizeof(buf), &pos)) > 0) {
        long i;
        for (i = 0; i < n; i++) {
            char ch = buf[i];
            if (ch != '\n' && linelen < (int)sizeof(line) - 1) {
                line[linelen++] = ch;
                continue;
            }
            /* end of a line (or overflow flush) */
            line[linelen] = '\0';
            if (linelen > 0 && inx_contains(line, linelen, lib)) {
                /* parse leading hex up to '-' */
                unsigned long v = 0;
                int j = 0;
                while (line[j] && line[j] != '-') {
                    char d = line[j];
                    int hv;
                    if (d >= '0' && d <= '9') hv = d - '0';
                    else if (d >= 'a' && d <= 'f') hv = d - 'a' + 10;
                    else if (d >= 'A' && d <= 'F') hv = d - 'A' + 10;
                    else { v = 0; break; }
                    v = (v << 4) | (unsigned long)hv;
                    j++;
                }
                if (v) { base = v; break; }
            }
            linelen = 0;
            if (ch != '\n') { /* overflowed line: drop until next newline */
                while (i < n && buf[i] != '\n') i++;
            }
        }
        if (base)
            break;
    }

    p_filp_close(f, 0);
    return base;
}

/* Resolve the VFS symbols. Call once from init; sets g_proc_ready. */
static void inx_proc_resolve(void)
{
    p_filp_open   = (fn_filp_open_t)kallsyms_lookup_name("filp_open");
    p_filp_close  = (fn_filp_close_t)kallsyms_lookup_name("filp_close");
    p_kernel_read = (fn_kernel_read_t)kallsyms_lookup_name("kernel_read");
    p_iterate_dir = (fn_iterate_dir_t)kallsyms_lookup_name("iterate_dir");
    p_get_cmdline = (fn_get_cmdline_t)kallsyms_lookup_name("get_cmdline");

    g_proc_ready = p_filp_open && p_filp_close && p_kernel_read &&
                   p_iterate_dir && p_get_cmdline;

    pr_info("hypervisor: proc filp_open=%px iterate_dir=%px kernel_read=%px "
            "get_cmdline=%px ready=%d\n",
            (void *)p_filp_open, (void *)p_iterate_dir, (void *)p_kernel_read,
            (void *)p_get_cmdline, g_proc_ready);
}

#endif /* INX_PROC_H */
