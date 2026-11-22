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

// Hologram: two search/replace pairs on the pdcache, generated from
// ff_combo_patch.sh's sr_one calls. Each pattern must occur exactly once.
struct IxSR { const unsigned char* find; const unsigned char* repl; int len; const char* tag; };
static const unsigned char kHoloFind0[48] = { 0x69, 0x6e, 0x2f, 0x44, 0x61, 0x74, 0x61, 0x2f, 0x63, 0x38, 0x61, 0x30, 0x61, 0x37, 0x65, 0x37, 0x64, 0x39, 0x30, 0x62, 0x39, 0x64, 0x63, 0x34, 0x61, 0x62, 0x65, 0x62, 0x35, 0x64, 0x62, 0x64, 0x63, 0x65, 0x30, 0x34, 0x34, 0x65, 0x38, 0x36, 0xd9, 0xb1, 0x4f, 0x0c, 0x27, 0xb2, 0x4f, 0x0c };
static const unsigned char kHoloRepl0[48] = { 0x69, 0x6e, 0x2f, 0x44, 0x61, 0x74, 0x61, 0x2f, 0x32, 0x31, 0x36, 0x61, 0x37, 0x31, 0x31, 0x33, 0x62, 0x37, 0x66, 0x31, 0x62, 0x63, 0x37, 0x34, 0x65, 0x62, 0x32, 0x32, 0x38, 0x39, 0x64, 0x34, 0x37, 0x66, 0x32, 0x61, 0x34, 0x30, 0x36, 0x64, 0xd9, 0xb1, 0x4f, 0x0c, 0x27, 0xb2, 0x4f, 0x0c };
static const unsigned char kHoloFind1[48] = { 0x69, 0x6e, 0x2f, 0x44, 0x61, 0x74, 0x61, 0x2f, 0x32, 0x31, 0x36, 0x61, 0x37, 0x31, 0x31, 0x33, 0x62, 0x37, 0x66, 0x31, 0x62, 0x63, 0x37, 0x34, 0x65, 0x62, 0x32, 0x32, 0x38, 0x39, 0x64, 0x34, 0x37, 0x66, 0x32, 0x61, 0x34, 0x30, 0x36, 0x64, 0xa5, 0xb5, 0xe1, 0x01, 0xf3, 0xb5, 0xe1, 0x01 };
static const unsigned char kHoloRepl1[48] = { 0x69, 0x6e, 0x2f, 0x44, 0x61, 0x74, 0x61, 0x2f, 0x63, 0x38, 0x61, 0x30, 0x61, 0x37, 0x65, 0x37, 0x64, 0x39, 0x30, 0x62, 0x39, 0x64, 0x63, 0x34, 0x61, 0x62, 0x65, 0x62, 0x35, 0x64, 0x62, 0x64, 0x63, 0x65, 0x30, 0x34, 0x34, 0x65, 0x38, 0x36, 0xa5, 0xb5, 0xe1, 0x01, 0xf3, 0xb5, 0xe1, 0x01 };
static const IxSR kHolo[2] = {
    { kHoloFind0, kHoloRepl0, (int)sizeof(kHoloFind0), "hash A->B" },
    { kHoloFind1, kHoloRepl1, (int)sizeof(kHoloFind1), "hash B->A" },
};

// Asset (Drag-HS): absolute offset + bytes, generated from the poke calls.
struct IxPoke { long off; const unsigned char* bytes; int len; };
static const unsigned char kAsset00[1] = { 0x4e };
static const unsigned char kAsset01[2] = { 0x63, 0x6b };
static const unsigned char kAsset02[29] = { 0xe7, 0x74, 0xbd, 0xbe, 0x16, 0xdc, 0x98, 0xbd, 0xbb, 0x82, 0x97, 0xb4, 0x00, 0x00, 0x00, 0x00, 0xbf, 0xb2, 0x2f, 0x3f, 0x43, 0x32, 0x73, 0x36, 0x66, 0x03, 0x7b, 0x35, 0x01 };
static const unsigned char kAsset03[1] = { 0xc0 };
static const unsigned char kAsset04[1] = { 0xc0 };
static const unsigned char kAsset05[3] = { 0x01, 0x00, 0xc0 };
static const unsigned char kAsset06[1] = { 0x48 };
static const unsigned char kAsset07[9] = { 0x61, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const unsigned char kAsset08[4] = { 0xb2, 0xf7, 0x1f, 0xa4 };
static const unsigned char kAsset09[29] = { 0xe7, 0x74, 0xbd, 0xbe, 0x16, 0xdc, 0x98, 0xbd, 0xbb, 0x82, 0x97, 0xb4, 0x00, 0x00, 0x00, 0x00, 0xbf, 0xb2, 0x2f, 0x3f, 0x43, 0x32, 0x73, 0x36, 0x66, 0x03, 0x7b, 0x35, 0x01 };
static const unsigned char kAsset10[1] = { 0xc0 };
static const unsigned char kAsset11[1] = { 0x01 };
static const unsigned char kAsset12[1] = { 0xc0 };
static const unsigned char kAsset13[3] = { 0x01, 0x00, 0xc0 };
static const unsigned char kAsset14[1] = { 0x48 };
static const unsigned char kAsset15[9] = { 0x61, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const unsigned char kAsset16[4] = { 0xb2, 0xf7, 0x1f, 0xa4 };
static const IxPoke kAssetPokes[17] = {
    { 28517L, kAsset00, (int)sizeof(kAsset00) },
    { 28519L, kAsset01, (int)sizeof(kAsset01) },
    { 29004L, kAsset02, (int)sizeof(kAsset02) },
    { 29034L, kAsset03, (int)sizeof(kAsset03) },
    { 29038L, kAsset04, (int)sizeof(kAsset04) },
    { 29040L, kAsset05, (int)sizeof(kAsset05) },
    { 29053L, kAsset06, (int)sizeof(kAsset06) },
    { 29055L, kAsset07, (int)sizeof(kAsset07) },
    { 29068L, kAsset08, (int)sizeof(kAsset08) },
    { 42432L, kAsset09, (int)sizeof(kAsset09) },
    { 42462L, kAsset10, (int)sizeof(kAsset10) },
    { 42464L, kAsset11, (int)sizeof(kAsset11) },
    { 42466L, kAsset12, (int)sizeof(kAsset12) },
    { 42468L, kAsset13, (int)sizeof(kAsset13) },
    { 42481L, kAsset14, (int)sizeof(kAsset14) },
    { 42483L, kAsset15, (int)sizeof(kAsset15) },
    { 42496L, kAsset16, (int)sizeof(kAsset16) },
};

static const long  kAssetMarkerOff  = 28517L;   // 0x48 stock -> 0x4e patched
static const long  kAssetExpectSize = 46640L;

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
    const std::string f = IxPickExisting(IxPdcachePaths(), 32);
    if (f.empty()) { if (detail) *detail = "pdcache not found"; return false; }

    std::vector<unsigned char> buf;
    if (!IxReadAll(f, buf)) { if (detail) *detail = "pdcache unreadable"; return false; }
    if (!IxBackupOnce(f))   { if (detail) *detail = "backup failed";     return false; }

    for (int i = 0; i < (int)(sizeof(kHolo) / sizeof(kHolo[0])); ++i) {
        const IxSR& sr = kHolo[i];
        long off = -1;
        const int hits = IxCountPattern(buf, sr.find, sr.len, &off);
        if (hits == 0) {
            // Already swapped? Then the replacement is what is in there.
            long roff = -1;
            if (IxCountPattern(buf, sr.repl, sr.len, &roff) > 0) {
                if (!quiet) printf("[INX] holo %s already applied\n", sr.tag);
                continue;
            }
            if (detail) *detail = std::string(sr.tag) + ": pattern not found";
            return false;
        }
        if (hits != 1) {
            if (detail) *detail = std::string(sr.tag) + ": pattern not unique";
            return false;
        }
        if (!IxPwrite(f, off, sr.repl, sr.len)) {
            if (detail) *detail = std::string(sr.tag) + ": write failed";
            return false;
        }
        // Keep the in-memory copy in step so the second pair searches the
        // same bytes the file now holds.
        memcpy(buf.data() + off, sr.repl, (size_t)sr.len);
        if (!quiet) printf("[INX] holo %s ok @ %ld\n", sr.tag, off);
    }
    if (detail) *detail = "ok";
    return true;
}

// ── ASSET (Drag-HS) ───────────────────────────────────────────────────
static inline bool IxAssetPatch(std::string* detail) {
    const std::string f = IxPickExisting(IxAssetPaths(), kAssetExpectSize);
    if (f.empty()) { if (detail) *detail = "assetindexer not ready"; return false; }

    const long sz = IxFileSize(f);
    if (sz != kAssetExpectSize) {
        if (detail) *detail = "unexpected size";
        return false;
    }
    if (!IxBackupOnce(f)) { if (detail) *detail = "backup failed"; return false; }

    // Same marker gate the script used: 0x4e means already done, 0x48 is
    // the stock byte, anything else means this is not the file expected.
    unsigned char marker = 0;
    {
        const int fd = open(f.c_str(), O_RDONLY);
        if (fd < 0) { if (detail) *detail = "open failed"; return false; }
        const ssize_t r = pread(fd, &marker, 1, (off_t)kAssetMarkerOff);
        close(fd);
        if (r != 1) { if (detail) *detail = "marker unreadable"; return false; }
    }
    if (marker == 0x4e) { if (detail) *detail = "already patched"; return true; }
    if (marker != 0x48) { if (detail) *detail = "marker mismatch";  return false; }

    for (int i = 0; i < (int)(sizeof(kAssetPokes) / sizeof(kAssetPokes[0])); ++i) {
        const IxPoke& p = kAssetPokes[i];
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
    const std::string f = IxPickExisting(IxAssetPaths(), kAssetExpectSize);
    if (f.empty()) return false;
    unsigned char marker = 0;
    const int fd = open(f.c_str(), O_RDONLY);
    if (fd < 0) return false;
    const ssize_t r = pread(fd, &marker, 1, (off_t)kAssetMarkerOff);
    close(fd);
    return r == 1 && marker == 0x4e;      // 0x48 stock, 0x4e patched
}

static inline bool IxHologramIsPatched() {
    const std::string f = IxPickExisting(IxPdcachePaths(), 32);
    if (f.empty()) return false;
    std::vector<unsigned char> buf;
    if (!IxReadAll(f, buf)) return false;
    // Patched means the first pair's REPLACEMENT is the one in the file.
    long off = -1;
    return IxCountPattern(buf, kHolo[0].repl, kHolo[0].len, &off) > 0;
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
