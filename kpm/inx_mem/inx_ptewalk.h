/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INX_PTEWALK_H
#define INX_PTEWALK_H
/*
 * inx_ptewalk -- cross-process memory read/write by SOFTWARE PAGE-TABLE WALK.
 *
 * This is the read/write method used by the Incognito KPM, ported here as a
 * drop-in primary backend for inx_mem (access_process_vm stays as the
 * fallback, see inx_vm_copy in inx_mem.c). Instead of calling
 * access_process_vm() -- an easily-watched kernel entry point -- it walks the
 * target process's own page tables to turn a user virtual address into a
 * physical address, maps that physical address through the kernel linear map,
 * and copies with copy_{from,to}_kernel_nofault(). Because the data copy then
 * touches a KERNEL linear-map address (not a user address), PAN never trips
 * and no user-access path is used at all.
 *
 * WHY THIS IS SAFE WHERE A NAIVE PTE-WALK IS NOT (the concern in inx_mem.c's
 * header): it does NOT touch the maple-tree / VMA structure. It only reads
 * mm->pgd and then walks hardware page tables, whose format is fixed by the
 * ARMv8 architecture, not by kernel version. The one kernel-build-specific
 * value -- the byte offset of the `pgd` field inside mm_struct -- is NOT
 * guessed: it is brute-forced from a candidate table and then CONFIRMED by
 * cross-checking the first bytes it produces against access_process_vm()
 * before it is ever trusted (see inx_pte_calibrate). If confirmation fails on
 * a given kernel, this backend reports "not calibrated" and inx_vm_copy falls
 * back to access_process_vm. Nothing here is hardcoded per device:
 *   - VA_BITS        : read from TCR_EL1.T0SZ at init
 *   - PAGE_OFFSET    : derived from VA_BITS   ((~0UL) << (VA_BITS-1))
 *   - PHYS_OFFSET    : read from the kernel's memstart_addr symbol
 *   - mm->pgd offset : brute-forced + confirmed at first use
 *
 * All of the above (the TCR_EL1.T0SZ math, the PAGE_OFFSET formula, the 4 KB
 * descriptor layout, the memstart-based __va, the candidate offset list) were
 * recovered from the real Incognito.kpm binary's own code, not invented.
 */

/* Resolved-at-runtime kernel symbols -- none assumed present. */
typedef void *(*fn_get_task_mm_t)(struct task_struct *task);
typedef void  (*fn_mmput_t)(void *mm);
/* copy_{from,to}_kernel_nofault(void *dst, const void *src, size_t n):
 * return 0 on success, negative (-EFAULT) if the access would fault. Older
 * kernels export the same thing as probe_kernel_{read,write}. */
typedef long  (*fn_knofault_t)(void *dst, const void *src, unsigned long n);

static fn_get_task_mm_t p_get_task_mm = 0;
static fn_mmput_t       p_mmput = 0;
static fn_knofault_t    p_cfkn = 0;   /* copy_from_kernel_nofault */
static fn_knofault_t    p_ctkn = 0;   /* copy_to_kernel_nofault   */

/* Runtime-discovered VA layout (0 until inx_ptewalk_resolve succeeds). */
static unsigned long g_va_bits     = 0;
static unsigned long g_page_offset = 0;
static unsigned long g_phys_offset = 0;   /* value of memstart_addr */

static int  g_pte_ready = 0;   /* all symbols + VA layout resolved */

/* Calibration cache: the mm->pgd offset that was confirmed correct. */
static int           g_cal_locked  = 0;
static unsigned long g_cal_pgd_off = 0;

/* Return sentinel: tell inx_vm_copy "I did not handle this, use fallback".
 * Chosen negative and distinct from real errnos the caller forwards. */
#define INX_PTE_FALLBACK  (-7777)

/* Candidate byte offsets of the `pgd` field within mm_struct. Taken verbatim
 * from Incognito.kpm's g_pgd_off_candidates table -- these are the offsets it
 * actually ships for current arm64 Android kernels. Order preserved. */
static const unsigned short INX_PGD_OFF_CANDIDATES[] = {
    0x38, 0x40, 0x48, 0x50, 0x58, 0x28, 0x30, 0x60, 0x68, 0x70, 0x78, 0x80,
};
#define INX_PGD_OFF_N \
    ((int)(sizeof(INX_PGD_OFF_CANDIDATES) / sizeof(INX_PGD_OFF_CANDIDATES[0])))

/* ARMv8 4 KB translation constants. */
#define INX_PAGE_SHIFT   12
#define INX_PTRS_SHIFT    9                 /* 9 index bits per level        */
#define INX_PTE_IDX_MASK  0x1ffUL           /* (1<<9)-1                      */
#define INX_PA_MASK       0xfffffffff000UL  /* desc output-address field     */
#define INX_DESC_VALID    0x1UL             /* bit0: valid                   */
#define INX_DESC_TABLE    0x2UL             /* bit1: 1=table/page, 0=block   */

/* __va for the linear map, exactly as Incognito computes it:
 *   kernel_va = (phys - memstart_addr) | PAGE_OFFSET            */
static inline unsigned long inx_pa_to_va(unsigned long pa)
{
    return (pa - g_phys_offset) | g_page_offset;
}

/* Walk `table_va` (a level-0 pgd kernel VA) for user VA `va`, writing the
 * resolved physical address to *phys_out. Reads every descriptor with the
 * nofault primitive, so a wrong table pointer fails cleanly instead of
 * oopsing. Returns 0 on success, negative on any fault/invalid entry. */
static int inx_pte_walk(unsigned long table_va, unsigned long va,
                        unsigned long *phys_out)
{
    int nlevels = (int)((g_va_bits - INX_PAGE_SHIFT + (INX_PTRS_SHIFT - 1))
                        / INX_PTRS_SHIFT);               /* DIV_ROUND_UP */
    int shift   = INX_PAGE_SHIFT + INX_PTRS_SHIFT * (nlevels - 1);
    int level;

    if (nlevels < 2 || nlevels > 4)
        return -EINVAL;

    for (level = 0; level < nlevels; level++) {
        unsigned long index = (va >> shift) & INX_PTE_IDX_MASK;
        unsigned long entry_va = table_va + index * 8;
        unsigned long desc = 0;

        if (p_cfkn(&desc, (void *)entry_va, sizeof(desc)) != 0)
            return -EFAULT;
        if (!(desc & INX_DESC_VALID))
            return -EFAULT;

        /* A block (bit1==0) at an upper level, or any entry at the last
         * level, is the leaf that holds the output address. */
        if (!(desc & INX_DESC_TABLE) || level == nlevels - 1) {
            unsigned long off_mask = (1UL << shift) - 1UL;   /* in-page/block offset */
            unsigned long pa = (desc & INX_PA_MASK & ~off_mask) | (va & off_mask);
            *phys_out = pa;
            return 0;
        }

        /* Table descriptor: descend. Next table is at a PHYSICAL address;
         * bring it into the linear map so the next level can be read. */
        table_va = inx_pa_to_va(desc & INX_PA_MASK);
        shift -= INX_PTRS_SHIFT;
    }
    return -EFAULT;
}

/* Read mm->pgd (a kernel VA) at a given candidate offset. Returns 0 and the
 * pointer value on success. */
static int inx_read_pgd(void *mm, unsigned long pgd_off, unsigned long *pgd_va)
{
    unsigned long v = 0;
    if (p_cfkn(&v, (void *)((char *)mm + pgd_off), sizeof(v)) != 0)
        return -EFAULT;
    /* mm->pgd is a kernel linear-map pointer: the top VA_BITS-1 bit region
     * must be set. Cheap reject of a garbage offset before we trust it. */
    if ((v & g_page_offset) != g_page_offset)
        return -EFAULT;
    *pgd_va = v;
    return 0;
}

/* Try each candidate pgd offset against a known-mapped probe address, and
 * CONFIRM the winner by comparing the bytes it yields against what
 * access_process_vm() reads for the same address. Only a confirmed offset is
 * locked in. fn_access_process_vm is the one declared in inx_mem.c (this
 * header is included after it). If it is unavailable, fall back to a
 * structural accept (valid walk + sane physical address). */
static int inx_pte_calibrate(struct task_struct *task, void *mm,
                             unsigned long probe_va)
{
    int i;
    unsigned char ref[8];
    int have_ref = 0;
    int reflen = 8;

    if (fn_access_process_vm) {
        int n = fn_access_process_vm(task, probe_va, ref, reflen, INX_FOLL_FORCE);
        if (n == reflen)
            have_ref = 1;
    }

    for (i = 0; i < INX_PGD_OFF_N; i++) {
        unsigned long off = INX_PGD_OFF_CANDIDATES[i];
        unsigned long pgd_va = 0, phys = 0;
        unsigned char got[8];

        if (inx_read_pgd(mm, off, &pgd_va) != 0)
            continue;
        if (inx_pte_walk(pgd_va, probe_va, &phys) != 0)
            continue;

        /* Low 12 bits of phys must equal low 12 bits of the VA. Always true
         * for a correct walk; a sanity tripwire against a coincidental hit. */
        if ((phys & 0xfffUL) != (probe_va & 0xfffUL))
            continue;

        if (have_ref) {
            /* Strong confirmation: read the same bytes through the linear
             * map and require them to match access_process_vm's view. */
            if (p_cfkn(got, (void *)inx_pa_to_va(phys), reflen) != 0)
                continue;
            if (memcmp(got, ref, reflen) != 0)
                continue;
        } else {
            /* No cross-check available: require the physical address to sit
             * at or above memstart (inside RAM), a weak but non-zero guard. */
            if (phys < g_phys_offset)
                continue;
        }

        g_cal_pgd_off = off;
        g_cal_locked  = 1;
        return 0;
    }
    return -ENOENT;
}

/* Primary backend. Returns bytes transferred (==len) on success, a negative
 * errno on a hard error, or INX_PTE_FALLBACK to ask the caller to use
 * access_process_vm instead (not ready / could not calibrate). */
static long inx_ptewalk_rw(struct task_struct *task, unsigned long addr,
                           void *buf, int len, int do_write)
{
    void *mm;
    unsigned long pgd_va = 0;
    unsigned long done = 0;
    long rc;

    if (!g_pte_ready)
        return INX_PTE_FALLBACK;
    if (len <= 0)
        return -EINVAL;

    mm = p_get_task_mm(task);
    if (!mm)
        return INX_PTE_FALLBACK;   /* kernel thread / no mm -- let fallback try */

    if (!g_cal_locked)
        inx_pte_calibrate(task, mm, addr);
    if (!g_cal_locked) {
        p_mmput(mm);
        return INX_PTE_FALLBACK;
    }

    if (inx_read_pgd(mm, g_cal_pgd_off, &pgd_va) != 0) {
        p_mmput(mm);
        return INX_PTE_FALLBACK;
    }

    /* Copy page-chunk by page-chunk: one VA->phys walk per page crossed. */
    rc = 0;
    while (done < (unsigned long)len) {
        unsigned long va = addr + done;
        unsigned long phys = 0;
        unsigned long page_left = 0x1000UL - (va & 0xfffUL);
        unsigned long chunk = (unsigned long)len - done;
        if (chunk > page_left)
            chunk = page_left;

        if (inx_pte_walk(pgd_va, va, &phys) != 0) {
            rc = done ? (long)done : -EFAULT;
            goto out;
        }

        if (do_write) {
            if (p_ctkn((void *)inx_pa_to_va(phys),
                       (char *)buf + done, chunk) != 0) {
                rc = done ? (long)done : -EFAULT;
                goto out;
            }
        } else {
            if (p_cfkn((char *)buf + done,
                       (void *)inx_pa_to_va(phys), chunk) != 0) {
                rc = done ? (long)done : -EFAULT;
                goto out;
            }
        }
        done += chunk;
    }
    rc = (long)done;

out:
    p_mmput(mm);
    return rc;
}

/* Resolve symbols + VA layout. Call once from module init. Sets g_pte_ready.
 * Never aborts the module -- if anything is missing, the pte-walk backend
 * just stays off and inx_vm_copy uses access_process_vm. */
static void inx_ptewalk_resolve(void)
{
    unsigned long tcr = 0, t0sz = 0;
    unsigned long *p_memstart;

    p_get_task_mm = (fn_get_task_mm_t)kallsyms_lookup_name("get_task_mm");
    p_mmput       = (fn_mmput_t)kallsyms_lookup_name("mmput");

    /* new name first, then the pre-5.8 name */
    p_cfkn = (fn_knofault_t)kallsyms_lookup_name("copy_from_kernel_nofault");
    if (!p_cfkn)
        p_cfkn = (fn_knofault_t)kallsyms_lookup_name("probe_kernel_read");
    p_ctkn = (fn_knofault_t)kallsyms_lookup_name("copy_to_kernel_nofault");
    if (!p_ctkn)
        p_ctkn = (fn_knofault_t)kallsyms_lookup_name("probe_kernel_write");

    p_memstart = (unsigned long *)kallsyms_lookup_name("memstart_addr");
    if (p_memstart)
        g_phys_offset = *p_memstart;

    /* VA_BITS = 64 - TCR_EL1.T0SZ (bits [21:16]); PAGE_OFFSET follows. */
    __asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
    t0sz = (tcr >> 16) & 0x3f;
    if (t0sz >= 16 && t0sz <= 28) {
        g_va_bits     = 64 - t0sz;
        g_page_offset = (~0UL) << (g_va_bits - 1);
    }

    g_pte_ready = p_get_task_mm && p_mmput && p_cfkn && p_ctkn &&
                  p_memstart && g_va_bits != 0;

    pr_info("sysutil: ptewalk get_task_mm=%px mmput=%px cfkn=%px ctkn=%px "
            "memstart=%lx va_bits=%lu page_off=%lx ready=%d\n",
            (void *)p_get_task_mm, (void *)p_mmput, (void *)p_cfkn,
            (void *)p_ctkn, g_phys_offset, g_va_bits, g_page_offset,
            g_pte_ready);
}

#endif /* INX_PTEWALK_H */
