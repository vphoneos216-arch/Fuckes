// hw_aim_max.h  -  HWBP register override silent aim for Free Fire Max
// ---------------------------------------------------------------------------
// ZERO game-data writes. We never touch aimingInfo / camera / bones in the game.
// Instead the paradise driver plants an exec breakpoint on the instruction
//     ldp s8, s9, [x8, #0x40]        (libil2cpp + 0x758C568)
// and, while the CPU is stopped there, rewrites register x8 to point at a
// private copy of aimingInfo whose direction vector we control. The game code
// then reads the shot direction from that copy and keeps running.
//
// Flag map (from driver disassembly, record offset -> flag byte/shift):
//     x0 -> b1 sh6 | x1 -> b2 sh0 | x2 -> b2 sh2 | x3 -> b2 sh4
//     x8 -> b3 sh6   <-- the only slot we ever override
// PC / LR / SP / x29 / pstate are never overridden (that panics the kernel).
#pragma once
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <cstdint>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <csignal>      // kill() in ValidatePid; the source drop relied on a
                        // transitive include for this and would not build alone
#include <sys/syscall.h>

namespace hwaim {

// ---------------- paradise driver ABI ----------------
enum : unsigned long {
    CMD_HWBP_SET   = 0xC1985728UL,
    CMD_HWBP_REM   = 0xC0045729UL,
    CMD_HWBP_CLR   = 0xC004572DUL,
    CMD_SET_REC    = 0xC360572CUL,
    CMD_GET_RECS   = 0xC018572BUL,
    CMD_FIND_PID   = 0xC104570BUL,
};

struct bp_entry { uint64_t addr; uint32_t rwx; uint32_t size; uint32_t mode; uint32_t pad; };
struct bp_cfg   { int32_t pid; int32_t count; uint8_t pad[16]; bp_entry entries[16]; };
struct bp_recs_req { uint32_t index, start, count, out_count; uint64_t buffer; };
struct bp_pid_req { int32_t pid; char name[260]; uint64_t base; uint32_t name_len, pad; uint64_t pad2; };

// record layout (0x350): +0x00..0x15 flags (2-bit slots), +0x20 PC key,
// +0x50 x0, +0x58 x1 ... +0x90 x8
static const uint32_t RVA_HOOK      = 0x758C568;   // ldp s8,s9,[x8,#0x40]
static const uint32_t RVA_DIR       = 0x40;        // dir vector inside aimingInfo
static const uint32_t RVA_ORIGIN    = 0x4C;        // muzzle origin inside aimingInfo
static const uint32_t FACADE_OFF    = 0x16095E18;  // static GameFacade slot (this build)
static const uint64_t FIRE_TIME_OFF = 0x1854;      // float LastFireBtnDownTime on localPlayer

static int   s_fd    = -1;
static int   s_pid   = 0;
static int   s_memfd = -1;
static uint64_t s_scratch = 0;
static uint64_t s_hook    = 0;
static uint64_t s_ai      = 0;
static bool  s_armed  = false;
static bool  s_failed = false;
static bool  s_ovr    = false;   // x8 override currently switched on?
static int   s_lastRet = 0, s_lastErr = 0;
static uint32_t s_lastOut = 0;

// ---------------- io helpers ----------------
inline int rdmem(uint64_t a, void *b, size_t n) {
    if (s_memfd < 0) return -1;
    return pread64(s_memfd, b, n, (off_t)a) == (ssize_t)n ? 0 : -1;
}
inline int wrmem(uint64_t a, const void *b, size_t n) {
    if (s_memfd < 0) return -1;
    return pwrite64(s_memfd, b, n, (off_t)a) == (ssize_t)n ? 0 : -1;
}
inline uint64_t rd64(uint64_t a) { uint64_t v = 0; rdmem(a, &v, 8); return v; }

inline bool pid_matches(int pid, const char *name) {
    char path[64], cl[256];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    memset(cl, 0, sizeof(cl));
    ssize_t n = read(fd, cl, sizeof(cl) - 1);
    close(fd);
    return n > 0 && strncmp(cl, name, strlen(name)) == 0;
}

// thread count of a pid: the real game process runs many threads, the stub with
// the same cmdline runs a handful. Picking the wrong one makes hwbp_set EINVAL.
inline int pid_threads(int pid) {
    char path[64], buf[512];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = 0;
    char *rp = strrchr(buf, ')');
    if (!rp) return 0;
    int field = 2;
    for (char *c = rp + 2; *c; c++) {
        if (*c == ' ') { field++; if (field == 20) return atoi(c + 1); }
    }
    return 0;
}

// The menu already resolved the live game pid for its own reads and wrote it to
// state.txt; reuse that so the breakpoint lands on the same process.
inline int state_pid() {
    FILE *f = fopen("/data/local/tmp/state.txt", "r");
    if (!f) return 0;
    char buf[512]; int pid = 0;
    while (fgets(buf, sizeof(buf), f)) {
        char *q = strstr(buf, "pid=");
        if (q) { pid = atoi(q + 4); break; }
    }
    fclose(f);
    if (pid > 0 && pid_matches(pid, "com.dts.freefiremax") && pid_threads(pid) > 8) return pid;
    return 0;
}
// Ask the driver itself which pid runs the target package (same path the
// standalone tools use). Falls back to /proc scans if the driver says no.
inline int driver_pid(const char *name) {
    if (s_fd < 0) return 0;
    bp_pid_req q; memset(&q, 0, sizeof(q));
    strncpy(q.name, name, sizeof(q.name) - 1);
    q.name_len = (uint32_t)strlen(name);
    if (ioctl(s_fd, CMD_FIND_PID, &q) != 0 || q.pid <= 0) return 0;
    return q.pid;
}
inline int find_pid(const char *name) {
    static int cached = 0;
    if (cached > 0 && pid_matches(cached, name) && pid_threads(cached) > 8) return cached;
    int best = 0, bestTh = -1;
    for (int pid = 1000; pid < 65000; pid++) {
        if (!pid_matches(pid, name)) continue;
        int th = pid_threads(pid);
        if (th > bestTh) { bestTh = th; best = pid; }
    }
    cached = best;
    return best;
}

inline uint64_t lib_base_from_maps(int pid, const char *soname) {
    char p[64]; snprintf(p, sizeof(p), "/proc/%d/maps", pid);
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    char line[512]; uint64_t best = 0;
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, soname)) continue;
        unsigned long long s = 0;
        if (sscanf(line, "%llx-", &s) == 1) { if (!best || s < best) best = s; }
    }
    fclose(f);
    return best;
}

// Scratch: the fake aimingInfo must live where the game will NEVER allocate.
// Priority: unused area of a thread stack (stacks grow down, so the pages just
// above the guard page stay untouched for shallow threads). Fallback: an anon
// rw mapping with several consecutive zero pages. A magic word lets us detect
// later collisions and back off instead of corrupting a live object.
inline uint64_t find_scratch(int pid) {
    char p[64]; snprintf(p, sizeof(p), "/proc/%d/maps", pid);
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    char line[512];
    static unsigned char probe[0x1000];
    uint64_t stacks[32]; int nst = 0, nc = 0;
    uint64_t cand[64];
    while (fgets(line, sizeof(line), f)) {
        unsigned long long s, e; char perm[8] = {0}, rest[400] = {0};
        int g = sscanf(line, "%llx-%llx %4s %*s %*s %*s %399[^\n]", &s, &e, perm, rest);
        if (strncmp(perm, "rw", 2) != 0) continue;
        if (strstr(rest, "thread stack")) {
            if (e - s >= 0x80000 && nst < 32) stacks[nst++] = s;
        }
        if (nst >= 4) break;
    }
    fclose(f);
    // 1) thread stacks: page just above the guard page, verify a few zero pages
    for (int i = 0; i < nst; i++) {
        for (int pg = 1; pg < 8; pg++) {
            uint64_t a = stacks[i] + (uint64_t)pg * 0x1000;
            bool ok = true;
            for (int k = 0; k < 3 && ok; k++) {
                memset(probe, 1, sizeof(probe));
                if (rdmem(a + (uint64_t)k * 0x1000, probe, sizeof(probe)) != 0) { ok = false; break; }
                for (size_t j = 0; j < sizeof(probe); j++) if (probe[j]) { ok = false; break; }
            }
            if (ok) return a + 0x100;
        }
    }
    // 2) fallback: 3 consecutive zero pages inside an anonymous rw mapping
    f = fopen(p, "r");
    if (!f) return 0;
    while (fgets(line, sizeof(line), f) && nc < 64) {
        unsigned long long s, e; char perm[8] = {0}, rest[400] = {0};
        int g = sscanf(line, "%llx-%llx %4s %*s %*s %*s %399[^\n]", &s, &e, perm, rest);
        if (strncmp(perm, "rw", 2) != 0) continue;
        if (strstr(rest, "dalvik") || strstr(rest, "scudo") || strstr(rest, "jit") || strstr(rest, "art")) continue;
        if (g >= 4 && strchr(rest, '/')) continue;
        if (e - s < 0x10000) continue;
        cand[nc++] = s;
    }
    fclose(f);
    for (int i = 0; i < nc; i++) {
        for (int pg = 8; pg < 48; pg++) {
            uint64_t a = cand[i] + (uint64_t)pg * 0x1000;
            bool ok = true;
            for (int k = 0; k < 3 && ok; k++) {
                memset(probe, 1, sizeof(probe));
                if (rdmem(a + (uint64_t)k * 0x1000, probe, sizeof(probe)) != 0) { ok = false; break; }
                for (size_t j = 0; j < sizeof(probe); j++) if (probe[j]) { ok = false; break; }
            }
            if (ok) return a + 0x100;
        }
    }
    return 0;
}

// magic written next to the fake struct; if the game overwrites it, the page
// has been recycled and we must stop using it
static const uint64_t SCRATCH_MAGIC = 0x41494d5f4d415857ULL;   // "XWAM_MIA"
inline void mark_scratch() { if (s_scratch) wrmem(s_scratch, &SCRATCH_MAGIC, 8); }
inline bool scratch_ok() {
    if (!s_scratch) return false;
    uint64_t v = 0;
    if (rdmem(s_scratch, &v, 8) != 0) return false;
    return v == SCRATCH_MAGIC;
}

// The RVA we know is a *vaddr* and the loader maps libil2cpp's segments with a
// 0x4000 vaddr<->fileoff bias, so base+rva lands in non-executable memory.
// Find the hooked instruction itself inside the r-xp mappings instead:
//   ldp s8, s9, [x8, #0x40] ; ldr s10, [x8, #0x48]
inline int find_hook_sigs(int pid, uint64_t *out, int max) {
    static const unsigned char pat[8] = {0x08,0x25,0x48,0x2d,0x0a,0x49,0x40,0xbd};
    char mp[64]; snprintf(mp, sizeof(mp), "/proc/%d/maps", pid);
    FILE *f = fopen(mp, "r");
    if (!f) return 0;
    char line[512];
    uint64_t seg[16][2]; int ns = 0;
    while (fgets(line, sizeof(line), f) && ns < 16) {
        if (!strstr(line, "libil2cpp.so")) continue;
        unsigned long long s, e, off; char perm[8] = {0};
        if (sscanf(line, "%llx-%llx %7s %llx", &s, &e, perm, &off) != 4) continue;
        if (!strchr(perm, 'x')) continue;
        seg[ns][0] = s; seg[ns][1] = e; ns++;
    }
    fclose(f);
    char memp[64]; snprintf(memp, sizeof(memp), "/proc/%d/mem", pid);
    int fd = open(memp, O_RDONLY);
    if (fd < 0) return 0;
    static unsigned char buf[0x100000 + 16];
    int found = 0;
    for (int i = 0; i < ns && found < max; i++) {
        uint64_t sz = seg[i][1] - seg[i][0];
        for (uint64_t o = 0; o < sz && found < max; o += 0x100000) {
            size_t want = (size_t)((sz - o < 0x100000) ? (sz - o) : 0x100000);
            ssize_t r = pread64(fd, buf, want, (off_t)(seg[i][0] + o));
            if (r <= 0) break;
            if ((size_t)r < want) want = (size_t)r;
            for (size_t k = 0; k + 8 <= want && found < max; k++)
                if (memcmp(buf + k, pat, 8) == 0) out[found++] = seg[i][0] + o + k;
        }
    }
    close(fd);
    return found;
}

inline bool fail(const char *why) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    static long last = -1;
    if (ts.tv_sec != last) { last = ts.tv_sec; fprintf(stderr, "[HW] arm fail: %s\n", why); }
    return false;
}

inline void reset() {
    s_armed = false; s_ovr = false; s_scratch = 0; s_hook = 0; s_ai = 0;
}

// ---- live probe: arm extra capture-only breakpoints to find the real
// bullet-direction read. Each probe has its own bp index and record.
static const uint32_t PROBE_VA[4] = { 0x758C568, 0x758CA1C, 0x7C54594, 0x7C56530 };
static const char    *PROBE_NM[4] = { "dir_reader", "muzzle", "aimw_1", "aimw_2" };
static uint64_t s_probe_addr[4] = {0,0,0,0};
static uint64_t s_probe_hits[4] = {0,0,0,0};

// vaddr -> runtime address, using the 0x4000 vaddr/fileoff bias of the exec segment
inline uint64_t resolve_va(int pid, uint32_t va) {
    uint64_t fo = (uint64_t)va - 0x4000ULL;
    char p[64]; snprintf(p, sizeof(p), "/proc/%d/maps", pid);
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    char line[512]; uint64_t a = 0;
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, "libil2cpp.so")) continue;
        unsigned long long s, e, off;
        if (sscanf(line, "%llx-%llx %*s %llx", &s, &e, &off) != 3) continue;
        if (fo >= off && fo < off + (e - s)) { a = s + (fo - off); break; }
    }
    fclose(f);
    return a;
}

// All three copies of the dir-reader pattern are armed together and all of them
// see the same private struct while the trigger is down, so whichever one the
// shot code uses reads our direction.
static const uint32_t RVA_EXTRA_SITES[3] = { 0x42EF9E4, 0x4309970, 0x4309AC0 };

static uint64_t s_hooks[8] = {0,0,0,0,0,0,0,0};
static int      s_nhooks = 0;

// How many direction-reader sites to arm. Only the first signature match is the
// proven bullet-direction builder; the extra guessed sites can send shots wild
// because x8 there points at a different object.
#ifndef HW_MAX_SITES
#define HW_MAX_SITES 3
#endif
static const int MAX_SITES = HW_MAX_SITES;

inline bool ArmAll(int pid) {
    s_nhooks = find_hook_sigs(pid, s_hooks, MAX_SITES);
    if (s_nhooks <= 0) return false;
    {   // append the extra dir readers found with the data watchpoint
        uint64_t base = 0;
        char mp[64]; snprintf(mp, sizeof(mp), "/proc/%d/maps", pid);
        FILE *mf = fopen(mp, "r");
        if (mf) {
            char ln[512];
            while (fgets(ln, sizeof(ln), mf)) {
                uint64_t s = 0, e = 0; char perms[8] = {0}, pth[256] = {0};
                if (sscanf(ln, "%llx-%llx %7s %*s %*s %*s %255s", (unsigned long long *)&s,
                           (unsigned long long *)&e, perms, pth) < 3) continue;
                if (strstr(pth, "libil2cpp.so") && perms[2] == 'x') { base = s; break; }
            }
            fclose(mf);
        }
        if (base && MAX_SITES > 3) {
            for (int i = 0; i < 3 && s_nhooks < 8; i++) {
                uint64_t sa = base + RVA_EXTRA_SITES[i];
                bool dup = false;
                for (int j = 0; j < s_nhooks; j++) if (s_hooks[j] == sa) dup = true;
                if (!dup) s_hooks[s_nhooks++] = sa;
            }
        }
    }
    bp_cfg c; memset(&c, 0, sizeof(c));
    c.pid = pid;
    for (int i = 0; i < s_nhooks; i++) {
        c.entries[i].addr = s_hooks[i];
        c.entries[i].rwx = 4; c.entries[i].size = 4; c.entries[i].mode = 0;
    }
    // some driver builds only accept a few entries -> shrink until it takes
    for (int n = s_nhooks; n >= 1; n--) {
        c.count = n;
        errno = 0;
        int r = ioctl(s_fd, CMD_HWBP_SET, &c);
        if (r >= 0) {
            s_nhooks = n;
            s_hook = s_hooks[0];
            fprintf(stderr, "[HW] bp_set ok count=%d errno=%d\n", n, errno);
            return true;
        }
        fprintf(stderr, "[HW] bp_set fail count=%d errno=%d pid=%d a0=%llx a1=%llx fd=%d\n", n, errno, pid, (unsigned long long)s_hooks[0], (unsigned long long)s_hooks[1], s_fd);
    }
    return false;
}
inline bool SetOverride(bool on);   // fwd
inline uint32_t Hits();             // fwd
inline void Mirror();               // fwd

// ---------------- arm ----------------
// libBase: libil2cpp base (0 = resolve from /proc/<pid>/maps)
inline bool Arm(int pid, uint64_t libBase = 0, uint64_t lpIn = 0, uint64_t aiIn = 0) {
    if (s_armed) return true;
    if (s_failed) return false;

    // FF runs several processes; only the one with libil2cpp mapped accepts our
    // breakpoints. A stale sibling pid makes hwbp_set fail with EINVAL.
    if (pid <= 0 || !lib_base_from_maps(pid, "libil2cpp.so")) {
        int c[3];
        c[0] = driver_pid("com.dts.freefiremax");
        c[1] = state_pid();
        c[2] = find_pid("com.dts.freefiremax");
        pid = 0;
        for (int i = 0; i < 3; i++)
            if (c[i] > 0 && lib_base_from_maps(c[i], "libil2cpp.so")) { pid = c[i]; break; }
        if (pid <= 0)
            for (int i = 0; i < 3; i++) if (c[i] > 0) { pid = c[i]; break; }
        fprintf(stderr, "[HW] pid pick: drv=%d state=%d scan=%d -> %d\n", c[0], c[1], c[2], pid);
    }
    if (pid <= 0) return false;                 // game not running yet -> retry later
    s_pid = pid;

    if (s_fd < 0) {
        int out = -1;
        syscall(142, 0x7a3b9c4dUL, 0x2f8e1d6bUL, (long)&out, (long)&out);
        s_fd = out;
        if (s_fd < 0) { s_failed = true; return fail("driver handshake"); }
    }
    if (s_memfd < 0) {
        char mp[64]; snprintf(mp, sizeof(mp), "/proc/%d/mem", pid);
        s_memfd = open(mp, O_RDWR);
        if (s_memfd < 0) { s_failed = true; return fail("memfd open"); }
    }
    if (!libBase) libBase = lib_base_from_maps(pid, "libil2cpp.so");
    if (!libBase) return fail("no libil2cpp base");                     // game not up yet, retry later

    // aimingInfo: prefer the caller's already-validated pointers, else walk the chain
    uint64_t lp = lpIn, ai = aiIn;
    if (!lp || !ai) {
        uint64_t facade = rd64(libBase + FACADE_OFF);
        if (!facade) return fail("facade NULL");
        uint64_t p2 = rd64(facade + 0xB8);   if (!p2) return fail("p2 NULL");
        uint64_t bg = rd64(p2);              if (!bg) return fail("baseGame NULL");
        uint64_t match = rd64(bg + 0x90);    if (!match) return fail("match NULL");
        lp = rd64(match + 0xD8);             if (!lp) return fail("localPlayer NULL");
        ai = rd64(lp + 0xE58);               if (!ai) return fail("aimingInfo NULL");
    }
    if (!ai) return fail("aimingInfo NULL");

    uint64_t scratch = find_scratch(pid);
    if (!scratch) { s_failed = true; return fail("no scratch"); }

    // private copy of the real aimingInfo (so origin/pitch/etc. stay correct)
    unsigned char fake[0x100];
    memset(fake, 0, sizeof(fake));
    if (rdmem(ai, fake, sizeof(fake)) != 0) return fail("read ai");
    float *d = (float *)(fake + RVA_DIR);
    float m = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    if (m < 0.5f) return fail("dir degenerate");                    // not in a match yet
    if (wrmem(scratch + 8, fake, sizeof(fake)) != 0) { s_failed = true; return fail("write scratch"); }
    s_scratch = scratch;
    mark_scratch();

    // arm exec breakpoints: entry 0 = our override target, 1..4 = probes
    int z = 0;
    ioctl(s_fd, CMD_HWBP_REM, &z);
    ioctl(s_fd, CMD_HWBP_CLR, &z);
    s_scratch = scratch; s_ai = ai;
    if (!ArmAll(pid)) { s_failed = false; return fail("hwbp_set"); }   // retry: pid may still be settling

    // The hooked instruction runs constantly (not only while firing), so the
    // override stays OFF and is switched on only for the brief fire window.
    if (!SetOverride(false)) fprintf(stderr, "[HW] set_record(off) failed errno=%d\\n", errno);

    s_armed = true;
    fprintf(stderr, "[HW] armed pid=%d sites=%d hook=%llx scratch=%llx ai=%llx\n",
            pid, s_nhooks, (unsigned long long)s_hook, (unsigned long long)scratch, (unsigned long long)ai);
    return true;
}

inline bool Armed() { return s_armed; }
inline bool IsOverride() { return s_ovr; }
inline int GamePidCached() { return s_pid; }
inline int ArmedPid() { return s_armed ? s_pid : 0; }
inline uint64_t HookAddr() { return s_hook; }
inline int LastRet() { return s_lastRet; }
inline int LastErr() { return s_lastErr; }
inline uint32_t LastOut() { return s_lastOut; }

// switch the x8 override on/off (capture-only record while off)
inline bool SetOverride(bool on) {
    if (s_fd < 0 || !s_hook || s_nhooks <= 0) return false;
    static unsigned char tmpl[0x360];
    bool allOk = true;
    // Descending order: bp 0 is written LAST. This driver keeps one record slot
    // per call, and site 0 is the one that runs on every tap and burst.
    for (int i = s_nhooks - 1; i >= 0; i--) {
        memset(tmpl, 0, sizeof(tmpl));
        unsigned char *rec = tmpl + 0x10;          // [0]=bp_idx, [4]=rec_idx, then 0x350
        tmpl[0] = (unsigned char)i;                // bp index
        tmpl[4] = 0;                               // record index
        if (on) rec[3] = 0x80;                     // b3 sh6 -> x8 = override
        *(uint64_t *)(rec + 0x20) = s_hooks[i];    // PC key, must match hit PC
        *(uint64_t *)(rec + 0x90) = s_scratch + 8; // x8 -> private copy
        if (ioctl(s_fd, CMD_SET_REC, tmpl) < 0) allOk = false;
    }
    if (allOk) s_ovr = on;
    return allOk;
}

// called every loop: override only while the fire window is open
inline void SyncOverride(bool firing) {
    if (!s_armed) return;
    static uint64_t lastHits = 0, lastHitMove = 0;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

    // The game only refreshes the fire timestamp on the press edge, so a plain
    // time window closes in the middle of a full-auto spray. Instead keep the
    // override alive while the hooked dir reader is still executing: its hit
    // counter only moves while the weapon is actually cycling.
    // FIX (ESP freeze): Hits() sabse bhaari record-dump ioctl hai. Pehle ye
    // har aim-loop iteration me chalta tha -> driver lock + CPU dono busy ->
    // ESP render thread atak jata tha. Ab 50 ms cache.
    static uint64_t s_lastHS = 0;
    static uint32_t s_hCache = 0;
    if (s_lastHS == 0 || (now - s_lastHS) >= 120) {
        s_lastHS = now;
        s_hCache = Hits();
    }
    uint32_t h = s_hCache;
    if (h != lastHits) { lastHits = h; lastHitMove = now; }

    bool active = firing || (lastHitMove && (now - lastHitMove) < 400);
    if (!active) { if (s_ovr) SetOverride(false); return; }
    if (!scratch_ok()) { SetOverride(false); return; }   // page recycled -> back off
    if (!s_ovr) SetOverride(true);
}

// keep the private copy a faithful mirror of the real struct, so every other
// field the game reads through x8 stays valid (only +0x40 is ours).
// 0x800 is mirrored because the game touches fields well past 0x400 and a short
// copy makes it read outside our block -> instant crash.
inline void Mirror() {
    if (!s_armed || !s_ai || !s_scratch) return;
    static unsigned char blk[0x800];
    if (rdmem(s_ai, blk, sizeof(blk)) != 0) return;
    wrmem(s_scratch + 8, blk, sizeof(blk));   // keep magic at +0
    mark_scratch();
}

// The player object (and its aimingInfo) is rebuilt on respawn / new match.
// Feeding the game an old mirror is the main crash source, so follow the live
// pointer and re-mirror as soon as it moves.
// if the game restarted, drop the old breakpoint state and allow a fresh arm
inline bool ValidatePid() {
    if (!s_armed) return false;
    if (kill(s_pid, 0) != 0) { reset(); s_failed = false; s_fd = -1; s_memfd = -1; return false; }
    return true;
}

inline void SetPointers(uint64_t lp, uint64_t ai) {
    if (!s_armed || ai == 0) return;
    if (ai != s_ai) {
        if (s_ovr) SetOverride(false);
        s_ai = ai;
        Mirror();
    }
}
// (bp_recs_req declared above)
// hit counter of record 0 -> proves the game actually executed the hooked insn
// The driver installs the watchpoints on the threads that exist at ioctl time.
// FF keeps creating worker threads, so after a while the firing thread no longer
// carries our breakpoint and hits stop (that is exactly when shots go wild).
// Re-install the same set without touching the mirror or our scratch block.
inline bool Rearm() {
    if (!s_armed || s_fd < 0) return false;
    SyncOverride(false);
    int z = 0;
    ioctl(s_fd, CMD_HWBP_REM, &z);
    ioctl(s_fd, CMD_HWBP_CLR, &z);
    if (!ArmAll(s_pid)) return false;
    // records are gone with the breakpoints: put them back (bp0 last)
    SetOverride(true);
    SyncOverride(false);
    return true;
}
inline uint32_t Hits() {
    if (!s_armed || s_fd < 0) return 0;
    static unsigned char recs[0x350];
    memset(recs, 0, sizeof(recs));
    bp_recs_req q; memset(&q, 0, sizeof(q));
    q.count = 1; q.buffer = (uint64_t)(uintptr_t)recs;
    errno = 0;
    int r = ioctl(s_fd, CMD_GET_RECS, &q);
    s_lastRet = r; s_lastErr = errno; s_lastOut = q.out_count;
    if (r != 0) return 0;
    return *(uint32_t *)(recs + 0x18);
}

// push a shot direction into the private copy (12 bytes, never the game's data)
inline void PushDir(float x, float y, float z) {
    if (!s_armed) return;
    float v[3] = { x, y, z };
    wrmem(s_scratch + 8 + RVA_DIR, v, sizeof(v));
}

// direction from muzzle origin (real, copied once) to a world point, normalised
inline void AimAt(float tx, float ty, float tz) {
    if (!s_armed) return;
    float org[3] = {0, 0, 0};
    rdmem(s_scratch + 8 + RVA_ORIGIN, org, sizeof(org));
    float d[3] = { tx - org[0], ty - org[1], tz - org[2] };
    float m = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    if (m < 0.001f) return;
    PushDir(d[0] / m, d[1] / m, d[2] / m);
}

inline bool Firing(uint64_t lp) {
    if (!lp) return false;
    float a = 0, b = 0;
    if (rdmem(lp + FIRE_TIME_OFF, &a, 4) != 0) return false;
    static float s_last = -1.f;
    static uint64_t s_t = 0;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (a != s_last) { s_last = a; s_t = now; return true; }
    return now - s_t < 320;
}

} // namespace hwaim
