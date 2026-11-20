/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * inx_libc -- self-contained mem* for the KPM.
 *
 * WHY THIS FILE EXISTS: the KernelPatch KPM loader resolves an undefined
 * symbol only through its own export table (symbol_lookup_name), and the
 * kallsyms fallback is disabled. It exports the kfunc trampolines
 * (kf_memcpy, kf_memset, ...) but NOT the bare names memcpy/memset/memmove/
 * memcmp. The compiler, however, SYNTHESIZES bare `memcpy`/`memset` calls on
 * its own for aggregate (struct/array) copies and zero-fills -- those calls
 * bypass the SDK's inline string.h wrappers and reference the bare symbol.
 * gcc did exactly this (an undefined `memcpy`), which left the whole module
 * unresolvable and it failed to load with -1.
 *
 * Defining the bare mem* here, as ordinary external symbols in their own
 * translation unit (this file does NOT include the SDK string.h, so there is
 * no clash with its static-inline versions), makes every compiler-synthesized
 * mem* call resolve inside the module itself -- no dependency on what a given
 * KParch build happens to export. Simple, correct, O(n) implementations;
 * speed is irrelevant for the few small copies/zeros a KPM does.
 *
 * These are only ever reached via compiler-generated calls; nothing here
 * calls them by name.
 */

typedef unsigned long inx_size_t;

void *memcpy(void *dest, const void *src, inx_size_t n)
{
    volatile unsigned char *d = (volatile unsigned char *)dest;
    const volatile unsigned char *s = (const volatile unsigned char *)src;
    inx_size_t i;
    for (i = 0; i < n; i++)
        d[i] = s[i];
    return dest;
}

void *memmove(void *dest, const void *src, inx_size_t n)
{
    volatile unsigned char *d = (volatile unsigned char *)dest;
    const volatile unsigned char *s = (const volatile unsigned char *)src;
    inx_size_t i;
    if (d == s || n == 0)
        return dest;
    if (d < s) {
        for (i = 0; i < n; i++)
            d[i] = s[i];
    } else {
        for (i = n; i > 0; i--)
            d[i - 1] = s[i - 1];
    }
    return dest;
}

void *memset(void *dest, int c, inx_size_t n)
{
    volatile unsigned char *d = (volatile unsigned char *)dest;
    inx_size_t i;
    for (i = 0; i < n; i++)
        d[i] = (unsigned char)c;
    return dest;
}

int memcmp(const void *a, const void *b, inx_size_t n)
{
    const volatile unsigned char *x = (const volatile unsigned char *)a;
    const volatile unsigned char *y = (const volatile unsigned char *)b;
    inx_size_t i;
    for (i = 0; i < n; i++) {
        if (x[i] != y[i])
            return (int)x[i] - (int)y[i];
    }
    return 0;
}
