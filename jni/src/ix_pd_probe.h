#pragma once
/* ==========================================================================
 * Paradise execute-breakpoint PROBE — native aim-assist target point
 * ==========================================================================
 * This file is a measurement tool, not a feature. It is wired to nothing,
 * changes no game state unless the override file below is present, and is
 * off entirely unless a trigger file exists on the device.
 *
 * ── What it targets ──────────────────────────────────────────────────────
 * dump.cs:  class ELCADGFIEDD  ->  method CAMCIEBBIOG(BEADLMGGGGL, bool,
 *           ref float, ref float)  returning Nullable<Vector2>
 *           RVA 0x7589260, file Offset 0x7585260
 *
 * ── Why this address ─────────────────────────────────────────────────────
 * 32 KB of the REAL libil2cpp.so (187877616 bytes, pulled off the device
 * at that file offset) was disassembled. Inside the function body:
 *
 *   +0x100  bl  WrappersManagerImpl.IsPatched(int)      <- hotfix gate
 *   +0x114  bl  WrappersManagerImpl.GetPatch(int)
 *   +0x154  bl  ILFixDynamicMethodWrapper.__Gen_Wrap_15411(...)
 *   +0x2a8  bl  GameFacade.CurrentMatch()
 *   +0x2dc  bl  JMAGGLCNGIG.HFMDJGBOJKF(BEADLMGGGGL, bool)   -> Player
 *   +0x330  bl  Player.IsLocalPlayer()
 *   +0x340  bl  Player.get_HipsBoneTransform()
 *   +0x34c  bl  Transform.get_position()               -> s0,s1,s2
 *   +0x364  mov v8.16b,  v0.16b     \
 *   +0x368  mov v10.16b, v1.16b      >  hips world position parked
 *   +0x370  mov v9.16b,  v2.16b     /
 *   +0x3c8..+0x3ec  fsub/fmul/fsqrt  (distance, stored via x21, then
 *                                     compared against [x19,#0x28])
 *   +0x420  bl  Player.GetAimRotation()
 *   +0x480  bl  Quaternion.op_Multiply(Quaternion, Vector3)
 *   +0x498  bl  Player.get_MyFollowCamera()
 *   +0x530  bl  IPBGFJNGLOH.FFLLOGJDGOJ(Vector3, Vector3, uint, ref ...)
 *   +0x590  bl  HitDetectColliderHelper.GetOwner()
 *   +0x64c  bl  Player.get_PlayerID()
 *   +0x6d4  bl  UICamera.get_mainCamera()
 *   +0x6f8  mov v0.16b,  v8.16b     \
 *   +0x6fc  mov v1.16b, v10.16b      >  parked hips pos restored as the
 *   +0x700  mov v2.16b,  v9.16b     /   Vector3 argument (AAPCS HFA:
 *   +0x704  mov x0, x20                 s0=X s1=Y s2=Z)
 *   +0x708  mov x1, x21
 *   +0x710  bl  MathUnity.WorldPosToUIPos(Vector3, Camera, Camera)
 *
 * So the game's own absorb/aim target point is the HIPS bone, and +0x710
 * is the one instruction where that world position is turned into a screen
 * point. s0/s1/s2 at that instruction are hips X/Y/Z. That matches the
 * reported behaviour (crosshair sits on the chest/body, not the head).
 *
 * ── What is NOT yet known (this probe answers it; nothing is assumed) ────
 *   Q1. Does this body actually run on the live build, or does the
 *       IsPatched gate at +0x100 route everything to the hotfix wrapper?
 *   Q2. Does a Paradise EXECUTE breakpoint hit at +0x710 at all?
 *   Q3. Does a Paradise record register override actually take effect?
 *       paradise_api.h says hwbp_set_record() writes "a record and its
 *       2-bit register operation mask", and PARADISE_BP_SET_MASK() selects
 *       per-register NONE/READ/WRITE. The project's existing Paradise
 *       helpers (PdHwbpArmSilentDir / PdHwbpArmCollider) fill rec.x0/x1/x2
 *       but never set that mask -- so whether those overrides ever applied
 *       is itself unverified. This probe tests the mask path directly.
 *   Q4. Is this screen point driving the aim, or only the HUD marker?
 *       The override test below shifts Y by a large, obvious amount: if
 *       the aim moves, it drives aim; if only a marker moves, it is HUD.
 *
 * ── How to turn it on (no menu changes, no feature coupling) ─────────────
 *   adb/termux:  touch /sdcard/Download/ix_probe.on
 *   log output:         /sdcard/Download/pd_probe.txt
 *
 *   Override test (only after the log shows hits):
 *                touch /sdcard/Download/ix_probe_override.on
 *   Default override is Y + 2.0 world units -- deliberately far above the
 *   body so the effect is unmistakable. Put a different float in that file
 *   to change it (e.g. `echo 0.35 > /sdcard/Download/ix_probe_override.on`).
 *
 * Address resolution is self-verifying: it reads the first 16 bytes at each
 * candidate address and requires them to equal the real prologue bytes that
 * came off the device. If nothing matches, it logs every candidate it tried
 * and arms nothing. No address is ever assumed.
 * ========================================================================== */

#include <atomic>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <ctime>
#include <string>
#include <utility>
#include <vector>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

/* ── Ground truth taken from the device's own libil2cpp.so ─────────────── */
#define IX_PROBE_FN_RVA       0x7589260ull   /* dump.cs RVA              */
#define IX_PROBE_FN_FILEOFF   0x7585260ull   /* dump.cs file Offset      */
#define IX_PROBE_W2S_DELTA    0x710ull       /* bl WorldPosToUIPos       */
#define IX_PROBE_SO_SIZE      187877616ull   /* ls -l on the device      */

/* First 16 bytes of the function, exactly as dd read them off the device:
 *   ff 83 06 d1   sub  sp, sp, #0x1a0
 *   ef 3b 11 6d   stp  d15, d14, [sp, #0x110]
 *   ed 33 12 6d   stp  d13, d12, [sp, #0x120]
 *   eb 2b 13 6d   stp  d11, d10, [sp, #0x130]                           */
static const uint8_t IX_PROBE_PROLOGUE[16] = {
    0xff, 0x83, 0x06, 0xd1,  0xef, 0x3b, 0x11, 0x6d,
    0xed, 0x33, 0x12, 0x6d,  0xeb, 0x2b, 0x13, 0x6d
};

/* The process may run as root inside its own mount namespace, where
 * /sdcard is not mounted at all. So every path below is tried, both for
 * the trigger files and for the log. /data/local/tmp is the one that is
 * always present for a root process; the /sdcard spellings are there so
 * the log also lands somewhere a file manager can reach when it can. */
static const char* IX_PROBE_DIRS[] = {
    "/data/local/tmp",
    "/sdcard/Download",
    "/storage/emulated/0/Download",
    "/data/media/0/Download",
};
static const size_t IX_PROBE_DIR_COUNT =
    sizeof(IX_PROBE_DIRS) / sizeof(IX_PROBE_DIRS[0]);

static std::atomic<bool> g_PdProbeRunning{false};

static bool ix_probe_file_exists(const char* p) {
    struct stat st{};
    return stat(p, &st) == 0;
}

/* Returns the first directory that holds the given trigger file, or null. */
static const char* ix_probe_find_trigger(const char* name) {
    static char path[256];
    for (size_t i = 0; i < IX_PROBE_DIR_COUNT; ++i) {
        snprintf(path, sizeof(path), "%s/%s", IX_PROBE_DIRS[i], name);
        if (ix_probe_file_exists(path)) return IX_PROBE_DIRS[i];
    }
    return nullptr;
}

/* ── log: writes to every directory that accepts it ───────────────────── */
static void ix_probe_log(const char* fmt, ...) {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);

    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    char path[256];
    for (size_t i = 0; i < IX_PROBE_DIR_COUNT; ++i) {
        snprintf(path, sizeof(path), "%s/pd_probe.txt", IX_PROBE_DIRS[i]);
        FILE* f = fopen(path, "a");
        if (!f) continue;
        fprintf(f, "[%6ld.%03ld] %s\n",
                (long)ts.tv_sec, (long)(ts.tv_nsec / 1000000), msg);
        fclose(f);
    }
}

/* Reads the override delta out of whichever directory holds the trigger
 * file; falls back to 2.0 when the file is empty or unparsable. Never
 * silently uses a number the file did not contain. */
static float ix_probe_override_delta() {
    char path[256];
    for (size_t i = 0; i < IX_PROBE_DIR_COUNT; ++i) {
        snprintf(path, sizeof(path), "%s/ix_probe_override.on", IX_PROBE_DIRS[i]);
        FILE* f = fopen(path, "r");
        if (!f) continue;
        float v = 0.f;
        int got = fscanf(f, "%f", &v);
        fclose(f);
        if (got == 1 && std::isfinite(v)) return v;
        return 2.0f;
    }
    return 2.0f;
}

/* ── raw read that works with or without the kernel driver ────────────── */
static bool ix_probe_read(uintptr_t addr, void* out, size_t len) {
    if (!addr || !out || !len) return false;
    if (g_driver != nullptr) {
        if (g_driver->read(addr, out, len)) return true;
        /* fall through to process_vm_readv if the driver read failed */
    }
    struct iovec local{}, remote{};
    local.iov_base  = out;
    local.iov_len   = len;
    remote.iov_base = reinterpret_cast<void*>(addr);
    remote.iov_len  = len;
    ssize_t n = syscall(SYS_process_vm_readv, game_pid, &local, 1, &remote, 1, 0);
    return n == (ssize_t)len;
}

/* ── candidate collection from /proc/<pid>/maps ───────────────────────── */
struct IxProbeMapping {
    uintptr_t start;
    uintptr_t end;
    uintptr_t fileoff;
    char      perms[8];
};

static void ix_probe_collect_mappings(std::vector<IxProbeMapping>& out) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", game_pid);
    FILE* m = fopen(path, "r");
    if (!m) { ix_probe_log("maps: cannot open %s", path); return; }
    char line[1024];
    while (fgets(line, sizeof(line), m)) {
        if (!strstr(line, "libil2cpp.so")) continue;
        IxProbeMapping mp{};
        unsigned long s = 0, e = 0, off = 0;
        char perms[8] = {0};
        if (sscanf(line, "%lx-%lx %7s %lx", &s, &e, perms, &off) != 4) continue;
        mp.start   = (uintptr_t)s;
        mp.end     = (uintptr_t)e;
        mp.fileoff = (uintptr_t)off;
        snprintf(mp.perms, sizeof(mp.perms), "%s", perms);
        out.push_back(mp);
    }
    fclose(m);
}

/* Resolves the function's runtime address and PROVES it by comparing the
 * first 16 bytes against the real prologue. Returns 0 if nothing matched. */
static uintptr_t ix_probe_resolve_fn() {
    std::vector<IxProbeMapping> maps;
    ix_probe_collect_mappings(maps);
    if (maps.empty()) { ix_probe_log("maps: no libil2cpp.so mappings found"); return 0; }

    ix_probe_log("maps: %zu libil2cpp.so mappings", maps.size());
    for (const auto& mp : maps)
        ix_probe_log("  %016lx-%016lx %s fileoff=%#lx size=%#lx",
                     (unsigned long)mp.start, (unsigned long)mp.end,
                     mp.perms, (unsigned long)mp.fileoff,
                     (unsigned long)(mp.end - mp.start));

    /* Build the candidate list.
     *
     * IMPORTANT, learned from the first probe run on this device: the same
     * file bytes appear TWICE in this process. libil2cpp.so is also mapped
     * as one big read-only full-file mapping (fileoff=0, ~0xb32d000 long),
     * and the prologue matches there too -- but that page never executes,
     * so an execute breakpoint placed on it can never fire. The first run
     * matched exactly that mapping and logged zero records for 15 minutes.
     *
     * So only an address inside an EXECUTABLE mapping is accepted. A match
     * in a non-executable mapping is still logged, as information, and then
     * rejected.
     *
     * Two address conventions are possible depending on segment layout, so
     * both are tried per mapping and the prologue bytes decide -- nothing is
     * assumed. (The first run established that on this build the file-offset
     * convention is the correct one, but the check is kept either way.) */
    struct IxProbeCand {
        uintptr_t   addr;
        const char* how;
        bool        exec;
    };
    std::vector<IxProbeCand> cands;
    for (const auto& mp : maps) {
        const bool isExec = strchr(mp.perms, 'x') != nullptr;
        const uintptr_t loadBase = mp.start - mp.fileoff;

        /* The function's file offset lies inside this mapping's file range */
        if (IX_PROBE_FN_FILEOFF >= mp.fileoff) {
            uintptr_t a = mp.start + (IX_PROBE_FN_FILEOFF - mp.fileoff);
            if (a + sizeof(IX_PROBE_PROLOGUE) <= mp.end)
                cands.push_back({ a, "segment+fileoffset-delta", isExec });
        }
        if (IX_PROBE_FN_RVA >= mp.fileoff) {
            uintptr_t a = mp.start + (IX_PROBE_FN_RVA - mp.fileoff);
            if (a + sizeof(IX_PROBE_PROLOGUE) <= mp.end)
                cands.push_back({ a, "segment+RVA-delta", isExec });
        }
        cands.push_back({ loadBase + IX_PROBE_FN_FILEOFF, "loadbase+fileoffset", isExec });
        cands.push_back({ loadBase + IX_PROBE_FN_RVA,     "loadbase+RVA",        isExec });
    }

    /* Executable candidates first, so a non-exec match can never win. */
    uintptr_t nonExecMatch = 0;
    for (int pass = 0; pass < 2; ++pass) {
        const bool wantExec = (pass == 0);
        for (const auto& c : cands) {
            if (c.exec != wantExec) continue;
            uint8_t buf[16] = {0};
            if (!ix_probe_read(c.addr, buf, sizeof(buf))) {
                if (c.exec)
                    ix_probe_log("candidate %016lx (%s, executable): read failed",
                                 (unsigned long)c.addr, c.how);
                continue;
            }
            bool match = memcmp(buf, IX_PROBE_PROLOGUE, sizeof(buf)) == 0;
            /* Executable candidates are few, so log their bytes either way --
             * a near-miss there would mean the code was patched in place. */
            if (c.exec || match)
                ix_probe_log("candidate %016lx (%s, %s): %02x %02x %02x %02x "
                             "%02x %02x %02x %02x %02x %02x %02x %02x "
                             "%02x %02x %02x %02x  -> %s",
                             (unsigned long)c.addr, c.how,
                             c.exec ? "executable" : "NOT executable",
                             buf[0], buf[1], buf[2],  buf[3],  buf[4],  buf[5],
                             buf[6], buf[7], buf[8],  buf[9],  buf[10], buf[11],
                             buf[12], buf[13], buf[14], buf[15],
                             match ? "MATCH" : "no");
            if (!match) continue;
            if (c.exec) return c.addr;
            if (!nonExecMatch) nonExecMatch = c.addr;
        }
    }

    if (nonExecMatch) {
        ix_probe_log("resolve: the prologue was found at %016lx but only in a "
                     "non-executable mapping (the read-only full-file mapping). "
                     "Code never runs there, so arming a breakpoint on it would "
                     "never fire -- arming nothing. No executable mapping covered "
                     "file offset %#llx this time.",
                     (unsigned long)nonExecMatch,
                     (unsigned long long)IX_PROBE_FN_FILEOFF);
    } else {
        ix_probe_log("resolve: no candidate matched the known prologue -- arming "
                     "nothing. Most likely the game build changed since the dump "
                     "was taken.");
    }
    return 0;
}

/* ── the probe thread ─────────────────────────────────────────────────── */
static void ix_pd_probe_thread() {
    ix_probe_log("==== probe start (pid=%d) ====", (int)game_pid);

    uintptr_t fn = 0;
    int waitLoops = 0;
    while (g_PdProbeRunning) {
        if (game_pid > 0) {
            fn = ix_probe_resolve_fn();
            if (fn) break;
        } else if (++waitLoops % 10 == 0) {
            ix_probe_log("waiting for the game process (game_pid=%d)", (int)game_pid);
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    if (!fn || !g_PdProbeRunning) { ix_probe_log("==== probe stop (no address) ===="); return; }

    const uintptr_t w2s = fn + IX_PROBE_W2S_DELTA;
    ix_probe_log("fn verified at %016lx, breakpoint target (bl WorldPosToUIPos) = %016lx",
                 (unsigned long)fn, (unsigned long)w2s);

    paradise_driver* pd = g_driver ? g_driver->GetParadise() : nullptr;
    if (!pd) {
        ix_probe_log("Paradise driver not available (GetParadise()==null) -- cannot arm. "
                     "The probe needs the Paradise driver specifically.");
        ix_probe_log("==== probe stop ====");
        return;
    }

    /* ── where does execution actually go? ───────────────────────────
     * What the last run established, on the device:
     *   num_brps=6, num_wrps=4   the CPU does expose hardware breakpoint
     *                            registers, so there is no shortage
     *   point_count=1            the driver really registered the point
     *   point[0] addr=...970 type=4 len=4 scope=2
     *                            registered at the verified address, as
     *                            EXECUTE, 4 bytes, all threads
     *   hwbp_get_records -> true, count 0 for 20 s
     *   ptebp_set -> false       that backend is simply not available on
     *                            this driver build, both plain and with
     *                            the coord mask
     *
     * So the hardware breakpoint is installed correctly, on executable
     * memory, with registers to spare -- and never fires. That points at
     * the instruction never being reached, not at the mechanism.
     *
     * Inside this function, +0x100 calls WrappersManagerImpl.IsPatched()
     * and +0x154 calls ILFixDynamicMethodWrapper.__Gen_Wrap_15411(). If
     * the method is hot-patched, execution leaves through the wrapper and
     * never reaches +0x710 at all.
     *
     * With 6 BRPs available, several points can be armed at once, so this
     * run watches the whole route instead of one instruction. Whichever
     * points report hits say exactly where execution goes -- or, if even
     * the entry never hits, that this method is not called at all in the
     * situation being tested. */

    struct IxProbePoint { uint64_t delta; const char* name; };
    static const IxProbePoint kPoints[] = {
        { 0x000, "entry"                          },
        { 0x100, "bl IsPatched"                   },
        { 0x154, "bl hotfix wrapper Gen_Wrap"     },
        { 0x2dc, "bl get player HFMDJGBOJKF"      },
        { 0x710, "bl WorldPosToUIPos (aim point)" },
    };
    const uint32_t kPointCount = (uint32_t)(sizeof(kPoints) / sizeof(kPoints[0]));

    pd->hwbp_remove();
    pd->ptebp_remove();
    pd->hwbp_clear_records(-1);

    paradise_bp_point_config pts[8]{};
    for (uint32_t i = 0; i < kPointCount; ++i) {
        pts[i].address  = (uint64_t)(fn + kPoints[i].delta);
        pts[i].type     = PARADISE_HWBP_EXECUTE;
        pts[i].length   = 4;
        pts[i].scope    = PARADISE_HWBP_ALL_THREADS;
        pts[i].reserved = 0;
    }

    bool armed = pd->hwbp_set(pts, kPointCount);
    ix_probe_log("hwbp_set(%u EXECUTE points) -> %s", kPointCount,
                 armed ? "true" : "false");
    for (uint32_t i = 0; i < kPointCount; ++i)
        ix_probe_log("  point[%u] %016lx  +%#05llx  %s",
                     i, (unsigned long)(fn + kPoints[i].delta),
                     (unsigned long long)kPoints[i].delta, kPoints[i].name);
    if (!armed) {
        ix_probe_log("==== probe stop (arm failed) ====");
        return;
    }

    paradise_bp_config cfg{};
    if (pd->hwbp_get_config(&cfg)) {
        ix_probe_log("config: tgid=%d point_count=%u num_brps=%llu num_wrps=%llu",
                     (int)cfg.tgid, cfg.point_count,
                     (unsigned long long)cfg.num_brps,
                     (unsigned long long)cfg.num_wrps);
        for (uint32_t i = 0; i < cfg.point_count && i < 8; ++i)
            ix_probe_log("  registered[%u] addr=%016llx type=%u len=%u scope=%u",
                         i, (unsigned long long)cfg.points[i].address,
                         cfg.points[i].type, cfg.points[i].length,
                         cfg.points[i].scope);
        if (cfg.point_count < kPointCount)
            ix_probe_log("NOTE: only %u of %u points registered -- the rest did "
                         "not fit, so their silence means nothing",
                         cfg.point_count, kPointCount);
    } else {
        ix_probe_log("hwbp_get_config -> false");
    }

    /* ── poll every point, report the first hit on each ──────────────── */
    bool     reported[8] = {false};
    uint64_t lastHits[8] = {0};
    bool     anyHit      = false;
    bool     overrideDone = false;
    int      quietTicks  = 0;

    while (g_PdProbeRunning) {
        for (uint32_t i = 0; i < kPointCount; ++i) {
            paradise_bp_record recs[2]{};
            uint32_t count = 0;
            if (!pd->hwbp_get_records(i, 0, recs, 2, &count)) continue;
            if (count == 0) continue;

            const paradise_bp_record& r = recs[0];
            if (r.hit_count == lastHits[i] && reported[i]) continue;
            lastHits[i] = r.hit_count;
            anyHit = true;

            uint32_t bx = (uint32_t)(uint64_t)r.q0;
            uint32_t by = (uint32_t)(uint64_t)r.q1;
            uint32_t bz = (uint32_t)(uint64_t)r.q2;
            float x, y, z;
            memcpy(&x, &bx, 4); memcpy(&y, &by, 4); memcpy(&z, &bz, 4);

            ix_probe_log("HIT point[%u] %s  pc=%016lx hits=%llu  "
                         "q0q1q2=(%.3f, %.3f, %.3f)  x0=%016lx x1=%016lx x19=%016lx",
                         i, kPoints[i].name, (unsigned long)r.pc,
                         (unsigned long long)r.hit_count, x, y, z,
                         (unsigned long)r.x0, (unsigned long)r.x1,
                         (unsigned long)r.x19);
            reported[i] = true;

            /* Override test, only on the aim-point instruction and only
             * once the trigger file is there. Shifts Y by a large amount
             * so the effect is unmistakable, and touches nothing else in
             * the captured record. */
            if (kPoints[i].delta == 0x710 && !overrideDone
                && ix_probe_find_trigger("ix_probe_override.on")) {
                const float delta = ix_probe_override_delta();
                paradise_bp_record w = r;
                float oldY = y;
                float newY = oldY + delta;
                uint32_t nb = 0;
                memcpy(&nb, &newY, 4);
                w.q1 = (w.q1 & ~(__uint128_t)0xffffffffull) | (__uint128_t)nb;
                PARADISE_BP_SET_MASK(&w, PARADISE_REG_Q1, PARADISE_BP_OP_WRITE);
                bool wok = pd->hwbp_set_record(i, 0, &w);
                ix_probe_log("OVERRIDE: Y %.3f -> %.3f (delta %.3f), mask=WRITE on "
                             "Q1, hwbp_set_record -> %s",
                             oldY, newY, delta, wok ? "true" : "false");
                ix_probe_log("OVERRIDE: watch the game. Aim/absorb point moves = "
                             "this instruction drives the aim. Only a HUD marker "
                             "moves = HUD only. Nothing moves = the mask-write "
                             "path does not apply, which would also explain the "
                             "existing Paradise SilentAimDir helpers never "
                             "setting that mask.");
                overrideDone = true;
            }
        }

        if (!anyHit && ++quietTicks % 80 == 0)
            ix_probe_log("no point has hit yet (%ds). If even `entry` stays "
                         "silent, this method is not being called in whatever "
                         "is on screen right now -- it needs a live match with "
                         "an enemy actually being aimed at.",
                         quietTicks / 4);

        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    pd->hwbp_remove();
    ix_probe_log("==== probe stop ====");
}

/* Starts the probe only when the trigger file exists. Checked once at
 * startup and then every 5 s, so the file can be created while the game
 * is already running. */
static void ix_pd_probe_watch_thread() {
    bool announced = false;
    for (;;) {
        const char* dir = ix_probe_find_trigger("ix_probe.on");
        bool want = dir != nullptr;
        bool have = g_PdProbeRunning.load();
        if (want && !announced) {
            /* First thing written, so an empty log still proves the build
             * actually contains the probe and found the trigger file. */
            ix_probe_log("==== trigger found in %s, starting probe ====", dir);
            announced = true;
        }
        if (want && !have) {
            g_PdProbeRunning = true;
            std::thread(ix_pd_probe_thread).detach();
        } else if (!want && have) {
            g_PdProbeRunning = false;
            announced = false;
        }
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

static void StartPdProbeWatch() {
    static std::atomic<bool> started{false};
    if (started.exchange(true)) return;
    std::thread(ix_pd_probe_watch_thread).detach();
}
