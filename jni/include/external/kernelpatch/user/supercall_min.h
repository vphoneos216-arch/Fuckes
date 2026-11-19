#pragma once
/*
 * supercall_min.h — the exact few constants and inline wrappers from
 * KernelPatch's own user/supercall.h + kernel/patch/include/uapi/scdefs.h
 * (pinned commit d4c88d6672687456b299e4b5153c24dc979d789b, SDK version
 * 0.13.9) that Option 16's client (KPM/kpm_driver.h) actually calls:
 * sc_hello, sc_ready, sc_kpm_load, sc_kpm_control.
 *
 * Why this exists instead of just #including the real supercall.h/
 * scdefs.h (which ARE vendored unmodified, right next to this file, for
 * provenance/diffing — see supercall.h and uapi/scdefs.h in this same
 * directory): those headers also define a full standalone type system
 * (ktypes.h: __s64, __kernel_fd_set, dev_t, loff_t, timer_t, ...) meant
 * for KernelPatch's own -nostdinc KERNEL build. Pulled into this
 * project's ordinary ndk-build translation unit, which already has the
 * NDK's real libc headers, those collide — confirmed from a real CI
 * failure (ten "typedef redefinition with different types" errors
 * against the NDK's own sys/types.h, posix_types.h, etc.), not a
 * guessed problem. Transcribing just the few pieces actually needed
 * avoids the collision entirely while keeping every value and every
 * function body byte-for-byte identical to the real vendored source
 * (diff this file's bodies against supercall.h's sc_hello/sc_ready/
 * sc_kpm_load/sc_kpm_control and ver_and_cmd to verify).
 */

#include <unistd.h>
#include <sys/syscall.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

/* kernel/patch/include/uapi/scdefs.h, the subset used below */
#define INX_KP_NR_supercall 45
#define INX_KP_SUPERCALL_HELLO 0x1000
#define INX_KP_SUPERCALL_HELLO_MAGIC 0x11581158
#define INX_KP_SUPERCALL_KPM_LOAD 0x1020
#define INX_KP_SUPERCALL_KPM_CONTROL 0x1022

/* top-level "version" file at the pinned commit: MAJOR=0 MINOR=13 PATCH=9 */
#define INX_KP_MAJOR 0
#define INX_KP_MINOR 13
#define INX_KP_PATCH 9

static inline long inx_kp_ver_and_cmd(const char *key, long cmd)
{
    (void)key;
    uint32_t version_code = (INX_KP_MAJOR << 16) + (INX_KP_MINOR << 8) + INX_KP_PATCH;
    return ((long)version_code << 32) | (0x1158 << 16) | (cmd & 0xFFFF);
}

static inline long sc_hello(const char *key)
{
    if (!key || !key[0]) return -EINVAL;
    return syscall(INX_KP_NR_supercall, key, inx_kp_ver_and_cmd(key, INX_KP_SUPERCALL_HELLO));
}

static inline bool sc_ready(const char *key)
{
    return sc_hello(key) == INX_KP_SUPERCALL_HELLO_MAGIC;
}

static inline long sc_kpm_load(const char *key, const char *path, const char *args, void *reserved)
{
    if (!key || !key[0]) return -EINVAL;
    if (!path || strlen(path) == 0) return -EINVAL;
    return syscall(INX_KP_NR_supercall, key, inx_kp_ver_and_cmd(key, INX_KP_SUPERCALL_KPM_LOAD),
                   path, args, reserved);
}

static inline long sc_kpm_control(const char *key, const char *name, const char *ctl_args,
                                   char *out_msg, long outlen)
{
    if (!key || !key[0]) return -EINVAL;
    if (!name || strlen(name) == 0) return -EINVAL;
    if (!ctl_args || strlen(ctl_args) == 0) return -EINVAL;
    return syscall(INX_KP_NR_supercall, key, inx_kp_ver_and_cmd(key, INX_KP_SUPERCALL_KPM_CONTROL),
                   name, ctl_args, out_msg, outlen);
}
