#pragma once
// ============================================================
//  ix_ff_patch.h — the game-file patches, moved into the binary
// ============================================================
// These are the two patches from ff_combo_patch.sh, reimplemented here
// because the flow now lives in the loader instead of a shell script.
//
// Every byte, offset, marker and size below was GENERATED from that
// script, not retyped: the tables are a direct dump of its sr_one
// search/replace pairs and its 17 poke calls, in file order.
//
//   IxHologramPatch()  pdcache assets/bin/Data hash swap   (Normal Mode,
//                      applied right after the mode is chosen)
//   IxAssetPatch()     assetindexer Drag-HS bytes          (Inject button)
//   IxRestoreStock()   put the original bytes back from .stockbak
//
// IxRestoreStock() is the "real value" step. Both patches back their file
// up to <file>.stockbak before the first write, so restoring is a copy
// back of the untouched original — the patched file stops existing on
// disk once the game has already read it.
//
// The header path inside the pdcache is never touched: the hologram
// patch only swaps two hash strings that it finds in place, exactly as
// the script did, and it refuses to write unless a pattern occurs
// exactly once.

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <atomic>

// The server-delivered rows. Empty unless the product sends patch data,
// in which case they take precedence over the fallback tables below.
#include "ix_rt_patches.h"

// ── flow state, shared by the loader, the ESP thread and the menu ─────
// Declared here because this header is included before both hook.h and
// the menu, so all three see one copy.
//
//  g_NormalMode     true when "Normal Mode" was chosen at startup: the
//                   hologram patch is applied and NO kernel driver is
//                   loaded, so g_driver stays null and Read<T>/Write<T>
//                   fall back to process_vm_*. The menu greys its ESP
//                   section out and says DRIVER REQUIRED in that state.
//  g_InjectApplied  the Inject button has written the asset bytes.
//  g_RestoreDone    the stock bytes have been put back; latched so lobby
//                   detection restores once per run, not every frame.

// ============================================================
//  No patch tables in the binary
// ============================================================
// The bytes used to sit here as arrays. They are gone.
//
// They now live in keydock_cheat_patches on the server and arrive inside
// the per-product secret, read by ix_rt_patches.h — the same treatment
// the offsets in ix_rt_offsets.h get, which is the point: a dump of this
// binary reveals no game-file hex, no offsets and no markers, because it
// carries none.
//
// There is deliberately NO fallback copy. A fallback is a second source
// of truth that silently takes over when the first one is misconfigured,
// and that is exactly how the previous version shipped bytes nobody had
// reviewed. With no rows from the server these functions refuse and say
// so, which is a problem you can see rather than one you cannot.
//
// Consequence worth knowing: the rows in keydock_cheat_patches are now
// REQUIRED. Without them Normal Mode's hologram patch and the Inject
// button both report that the server sent nothing.

// Longest row the server may send for a single write. Checked, not
// assumed — a row past this is refused rather than truncated.
#define IX_ROW_MAX 64

// ── where the files live ──────────────────────────────────────────────
// Both roots are tried, same as the script's *_CANDIDATES lists.
static std::atomic<bool> g_NormalMode{false};
static std::atomic<bool> g_InjectApplied{false};
static std::atomic<bool> g_RestoreDone{false};

static inline const char* IxPkgName() { return "com.dts.freefiremax"; }

static inline std::vector<std::string> IxPdcachePaths() {
    const std::string p = IxPkgName();
    return {
        "/data/user/0/" + p + "/files/split_asset_pack_install_time.apk.pdcache",
        "/data/data/"   + p + "/files/split_asset_pack_install_time.apk.pdcache",
    };
}

static inline std::vector<std::string> IxAssetPaths() {
    const std::string p = IxPkgName();
    const std::string n = "assetindexer.U0jVgfPVdG5YZ2HvIuO5tFkQcCM~3D";
    return {
        "/data/user/0/" + p + "/files/contentcache/Compulsory/android/gameassetbundles/avatar/" + n,
        "/data/data/"   + p + "/files/contentcache/Compulsory/android/gameassetbundles/avatar/" + n,
    };
}

// ── small file helpers ────────────────────────────────────────────────
static inline long IxFileSize(const std::string& f) {
    struct stat st{};
    if (stat(f.c_str(), &st) != 0) return -1;
    return (long)st.st_size;
}

// First candidate that exists and is at least min_sz bytes.
static inline std::string IxPickExisting(const std::vector<std::string>& c, long min_sz) {
    for (const auto& f : c) {
        const long sz = IxFileSize(f);
        if (sz >= min_sz) return f;
    }
    return std::string();
}

static inline bool IxCopyFile(const std::string& from, const std::string& to) {
    const int in = open(from.c_str(), O_RDONLY);
    if (in < 0) return false;
    const int out = open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (out < 0) { close(in); return false; }
    char buf[65536];
    bool ok = true;
    for (;;) {
        const ssize_t r = read(in, buf, sizeof(buf));
        if (r == 0) break;
        if (r < 0) { ok = false; break; }
        ssize_t done = 0;
        while (done < r) {
            const ssize_t w = write(out, buf + done, (size_t)(r - done));
            if (w <= 0) { ok = false; break; }
            done += w;
        }
        if (!ok) break;
    }
    close(in);
    close(out);
    return ok;
}

// Taken once, before the first write to that file, and never overwritten
// afterwards — so it always holds the untouched original even if a patch
// runs twice.
static inline bool IxBackupOnce(const std::string& f) {
    const std::string b = f + ".stockbak";
    if (IxFileSize(b) > 0) return true;
    return IxCopyFile(f, b);
}

static inline bool IxReadAll(const std::string& f, std::vector<unsigned char>& out) {
    const long sz = IxFileSize(f);
    if (sz <= 0 || sz > (64L * 1024 * 1024)) return false;
    const int fd = open(f.c_str(), O_RDONLY);
    if (fd < 0) return false;
    out.resize((size_t)sz);
    long done = 0;
    while (done < sz) {
        const ssize_t r = read(fd, out.data() + done, (size_t)(sz - done));
        if (r <= 0) { close(fd); return false; }
        done += r;
    }
    close(fd);
    return true;
}

static inline bool IxPwrite(const std::string& f, long off,
                            const unsigned char* b, int n) {
    const int fd = open(f.c_str(), O_WRONLY);
    if (fd < 0) return false;
    const ssize_t w = pwrite(fd, b, (size_t)n, (off_t)off);
    close(fd);
    return w == (ssize_t)n;
}

// Counts occurrences and reports the first offset. The script required
// exactly one hit before writing; so does this.
static inline int IxCountPattern(const std::vector<unsigned char>& hay,
                                 const unsigned char* needle, int n,
                                 long* first_off) {
    if (n <= 0 || hay.size() < (size_t)n) return 0;
    int hits = 0;
    const size_t last = hay.size() - (size_t)n;
    for (size_t i = 0; i <= last; ++i) {
        if (hay[i] == needle[0] && memcmp(hay.data() + i, needle, (size_t)n) == 0) {
            if (hits == 0 && first_off) *first_off = (long)i;
            if (++hits > 1) return hits;   // one extra hit is enough to refuse
        }
    }
    return hits;
}

// ── HOLOGRAM ──────────────────────────────────────────────────────────
// Returns true when both swaps are in place (already-applied counts as
// in place, exactly like the script's "already applied" branch).
// quiet = print nothing at all; the caller logs the one-line result.
static inline bool IxHologramPatch(bool quiet, std::string* detail) {
    std::vector<IxSrvPatch> srv = ix_patch_rows("hologram");
    if (srv.empty()) {
        if (detail) *detail = "no hologram bytes from server";
        return false;
    }

    const std::string f = IxPickExisting(IxPdcachePaths(), 32);
    if (f.empty()) { if (detail) *detail = "pdcache not found"; return false; }

    std::vector<unsigned char> buf;
    if (!IxReadAll(f, buf)) { if (detail) *detail = "pdcache unreadable"; return false; }

    // A hologram row is a find/replace swap of equal length. An
    // offset-only row has nothing to search for and is not usable here.
    struct Pair { const unsigned char* find; const unsigned char* repl; int len; std::string tag; };
    std::vector<Pair> pairs;
    for (size_t i = 0; i < srv.size(); ++i) {
        const IxSrvPatch& r = srv[i];
        if (r.find.empty() || r.find.size() != r.repl.size()) continue;
        if (r.repl.size() > IX_ROW_MAX) {
            if (detail) *detail = "row " + std::to_string(i) + ": too large";
            return false;
        }
        pairs.push_back({ r.find.data(), r.repl.data(), (int)r.repl.size(),
                          "row " + std::to_string(i) });
    }
    if (pairs.empty()) {
        if (detail) *detail = "server sent no usable hologram pair";
        return false;
    }

    if (!IxBackupOnce(f)) { if (detail) *detail = "backup failed"; return false; }

    for (const Pair& p : pairs) {
        long off = -1;
        const int hits = IxCountPattern(buf, p.find, p.len, &off);
        if (hits == 0) {
            // Already swapped? Then the replacement is what is in there.
            long roff = -1;
            if (IxCountPattern(buf, p.repl, p.len, &roff) > 0) {
                if (!quiet) printf("[INX] holo %s already applied\n", p.tag.c_str());
                continue;
            }
            if (detail) *detail = p.tag + ": pattern not found";
            return false;
        }
        if (hits != 1) {
            if (detail) *detail = p.tag + ": pattern not unique";
            return false;
        }
        if (!IxPwrite(f, off, p.repl, p.len)) {
            if (detail) *detail = p.tag + ": write failed";
            return false;
        }
        // Keep the in-memory copy in step so the next pair searches the
        // same bytes the file now holds.
        memcpy(buf.data() + off, p.repl, (size_t)p.len);
        if (!quiet) printf("[INX] holo %s ok @ %ld\n", p.tag.c_str(), off);
    }
    if (detail) *detail = "ok";
    return true;
}

// ── ASSET (Drag-HS) ───────────────────────────────────────────────────
static inline bool IxAssetPatch(std::string* detail) {
    std::vector<IxSrvPatch> srv = ix_patch_rows("asset");
    if (srv.empty()) {
        if (detail) *detail = "no asset bytes from server";
        return false;
    }

    // Expected size is whatever the server declared, if it declared one.
    const long long declared = ix_patch_expect_size("asset");
    const long expectSize = (declared >= 0) ? (long)declared : 0;

    const std::string f = IxPickExisting(IxAssetPaths(), expectSize > 0 ? expectSize : 1);
    if (f.empty()) { if (detail) *detail = "assetindexer not ready"; return false; }

    const long sz = IxFileSize(f);
    if (expectSize > 0 && sz != expectSize) {
        if (detail) *detail = "unexpected size";
        return false;
    }

    struct Poke { long off; const unsigned char* bytes; int len;
                  const unsigned char* stock; int stockLen; };
    std::vector<Poke> pokes;
    for (const IxSrvPatch& r : srv) {
        if (r.off < 0 || r.repl.empty()) continue;      // needs a target offset
        pokes.push_back({ (long)r.off, r.repl.data(), (int)r.repl.size(),
                          r.find.empty() ? nullptr : r.find.data(),
                          (int)r.find.size() });
    }
    if (pokes.empty()) {
        if (detail) *detail = "server sent no usable asset poke";
        return false;
    }

    // Validate every row BEFORE touching the file, so an out-of-range row
    // reports why it was refused instead of failing later in a read and
    // pointing at the wrong thing.
    for (const Poke& p : pokes) {
        if (p.len <= 0 || p.len > IX_ROW_MAX) {
            if (detail) *detail = "poke row too large";
            return false;
        }
        if (p.off < 0 || p.off + p.len > sz) {
            if (detail) *detail = "poke outside the file";
            return false;
        }
    }

    // A row that declares its stock bytes in `find` is the gate: if the
    // replacement is already there the work is done, and if neither the
    // stock nor the replacement is there this is not the file expected.
    // That is the same test the compiled-in 0x48/0x4e marker used to do,
    // except the two values now come from the server with everything else.
    bool gated = false;
    for (const Poke& p : pokes) {
        if (!p.stock || p.stockLen != p.len) continue;
        std::vector<unsigned char> cur((size_t)p.len, 0);
        const int fd = open(f.c_str(), O_RDONLY);
        if (fd < 0) { if (detail) *detail = "open failed"; return false; }
        const ssize_t got = pread(fd, cur.data(), (size_t)p.len, (off_t)p.off);
        close(fd);
        if (got != (ssize_t)p.len) { if (detail) *detail = "marker unreadable"; return false; }

        if (memcmp(cur.data(), p.bytes, (size_t)p.len) == 0) {
            if (detail) *detail = "already patched";
            return true;
        }
        if (memcmp(cur.data(), p.stock, (size_t)p.len) != 0) {
            if (detail) *detail = "stock bytes differ";
            return false;
        }
        gated = true;
        break;
    }

    // No row declared its stock bytes: fall back to asking whether the
    // first row's replacement is already in place, which still stops a
    // second run from rewriting what is already there.
    if (!gated) {
        const Poke& p0 = pokes[0];
        std::vector<unsigned char> cur((size_t)p0.len, 0);
        const int fd = open(f.c_str(), O_RDONLY);
        if (fd < 0) { if (detail) *detail = "open failed"; return false; }
        const ssize_t got = pread(fd, cur.data(), (size_t)p0.len, (off_t)p0.off);
        close(fd);
        if (got != (ssize_t)p0.len) { if (detail) *detail = "marker unreadable"; return false; }
        if (memcmp(cur.data(), p0.bytes, (size_t)p0.len) == 0) {
            if (detail) *detail = "already patched";
            return true;
        }
    }

    if (!IxBackupOnce(f)) { if (detail) *detail = "backup failed"; return false; }

    for (const Poke& p : pokes) {
        if (!IxPwrite(f, p.off, p.bytes, p.len)) {
            if (detail) *detail = "write failed";
            return false;
        }
    }
    if (detail) *detail = "ok";
    return true;
}

// ── STATE CHECKS ──────────────────────────────────────────────────────
// "Is it patched right now?" answered from the bytes on disk, not from a
// flag this process is keeping, so it is still right after a crash, a
// force-stop, or a run that never pressed Exit.
static inline bool IxAssetIsPatched() {
    std::vector<IxSrvPatch> srv = ix_patch_rows("asset");
    if (srv.empty()) return false;

    const long long declared = ix_patch_expect_size("asset");
    const std::string f = IxPickExisting(IxAssetPaths(), declared >= 0 ? (long)declared : 1);
    if (f.empty()) return false;

    for (const IxSrvPatch& r : srv) {
        if (r.off < 0 || r.repl.empty()) continue;
        std::vector<unsigned char> cur(r.repl.size(), 0);
        const int fd = open(f.c_str(), O_RDONLY);
        if (fd < 0) return false;
        const ssize_t got = pread(fd, cur.data(), cur.size(), (off_t)r.off);
        close(fd);
        return got == (ssize_t)cur.size() &&
               memcmp(cur.data(), r.repl.data(), cur.size()) == 0;
    }
    return false;
}

static inline bool IxHologramIsPatched() {
    std::vector<IxSrvPatch> srv = ix_patch_rows("hologram");
    if (srv.empty()) return false;

    const std::string f = IxPickExisting(IxPdcachePaths(), 32);
    if (f.empty()) return false;
    std::vector<unsigned char> buf;
    if (!IxReadAll(f, buf)) return false;

    // Patched means the first usable pair's REPLACEMENT is in the file.
    for (const IxSrvPatch& r : srv) {
        if (r.find.empty() || r.repl.empty()) continue;
        long off = -1;
        return IxCountPattern(buf, r.repl.data(), (int)r.repl.size(), &off) > 0;
    }
    return false;
}

// ── RESTORE ───────────────────────────────────────────────────────────
// Each file on its own, because the two patches are now unpatched at
// different points: Kernel Mode takes both back out, Normal Mode only
// the asset (its hologram patch is meant to stay), and Exit takes out
// whatever is still applied.
static inline int IxRestoreFrom(const std::vector<std::string>& cands,
                                std::string* detail) {
    int restored = 0;
    for (const auto& f : cands) {
        const std::string b = f + ".stockbak";
        if (IxFileSize(b) > 0 && IxFileSize(f) > 0) {
            if (IxCopyFile(b, f)) ++restored;
        }
    }
    if (detail) *detail = restored ? "restored" : "nothing to restore";
    return restored;
}

static inline int IxRestoreAsset(std::string* detail) {
    return IxRestoreFrom(IxAssetPaths(), detail);
}

static inline int IxRestoreHologram(std::string* detail) {
    return IxRestoreFrom(IxPdcachePaths(), detail);
}

// Both, for the Exit button.
static inline int IxRestoreStock(std::string* detail) {
    std::vector<std::string> all = IxPdcachePaths();
    for (const auto& a : IxAssetPaths()) all.push_back(a);
    return IxRestoreFrom(all, detail);
}
