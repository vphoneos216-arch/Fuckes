#pragma once
// ============================================================
//  ix_aob.h — AOB (array-of-bytes) search and patch.
//
//  Why this exists: the offsets in ix_rt_offsets.h are read through
//  pointer chains, and the module-relative ones (GameFacade) break on
//  every game update. A byte signature survives updates far better,
//  because the code it matches usually does not move or change even
//  when the layout around it does.
//
//  What it does NOT do: it cannot find a field offset. "Health is at
//  +0x18" exists nowhere in memory as a byte sequence, so no scan can
//  locate it. Signatures find CODE; field offsets still come from the
//  server JSON, which is the right tool for them.
//
//  Patterns come from the same server JSON as the offsets, under a
//  "patches" object, so a new signature is a database edit rather than
//  a rebuild.
//
//  Every patch records the bytes it overwrote, so turning a toggle off
//  puts the original code back. Without that a switch only works once.
// ============================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <fstream>
#include <sstream>
#include <functional>

namespace ix_aob {

// ── a parsed signature: bytes plus a per-byte "must match" mask ──
struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<bool>    care;     // false where the token was ?? / ?
    bool valid() const { return !bytes.empty() && bytes.size() == care.size(); }
    size_t size() const { return bytes.size(); }
};

// "48 8B ?? ?? 90" or "488B????90" -> Pattern. Returns an invalid
// Pattern on anything it cannot read, so callers can report instead of
// scanning for nonsense.
inline Pattern parse_pattern(const std::string& text) {
    Pattern p;
    size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',') { ++i; continue; }

        if (c == '?') {
            // accept both "?" and "??" for one wildcard byte
            p.bytes.push_back(0);
            p.care.push_back(false);
            i += (i + 1 < text.size() && text[i + 1] == '?') ? 2 : 1;
            continue;
        }

        auto hexval = [](char h) -> int {
            if (h >= '0' && h <= '9') return h - '0';
            if (h >= 'a' && h <= 'f') return h - 'a' + 10;
            if (h >= 'A' && h <= 'F') return h - 'A' + 10;
            return -1;
        };
        const int hi = hexval(c);
        if (hi < 0) { p.bytes.clear(); p.care.clear(); return p; }   // junk
        if (i + 1 >= text.size()) { p.bytes.clear(); p.care.clear(); return p; }
        const int lo = hexval(text[i + 1]);
        if (lo < 0) { p.bytes.clear(); p.care.clear(); return p; }

        p.bytes.push_back((uint8_t)((hi << 4) | lo));
        p.care.push_back(true);
        i += 2;
    }
    return p;
}

// "1F 20 03 D5" -> raw bytes. No wildcards allowed in a replacement.
inline bool parse_bytes(const std::string& text, std::vector<uint8_t>& out) {
    const Pattern p = parse_pattern(text);
    if (!p.valid()) return false;
    for (size_t i = 0; i < p.care.size(); ++i)
        if (!p.care[i]) return false;          // "??" makes no sense here
    out = p.bytes;
    return true;
}

// ── where a library is mapped, from /proc/<pid>/maps ────────────
// Only executable, readable ranges are considered: a signature is code,
// and scanning the data mappings wastes time and can hit guard pages.
struct Range { uintptr_t start = 0, end = 0; };

inline std::vector<Range> module_ranges(int pid, const std::string& lib) {
    std::vector<Range> out;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    std::ifstream f(path);
    if (!f.good()) return out;

    std::string line;
    while (std::getline(f, line)) {
        if (line.find(lib) == std::string::npos) continue;
        // "7f8a2c0000-7f8a2d0000 r-xp ..."
        const size_t dash = line.find('-');
        const size_t sp   = line.find(' ');
        if (dash == std::string::npos || sp == std::string::npos || dash > sp) continue;
        const std::string perms = line.substr(sp + 1, 4);
        if (perms.size() < 3 || perms[0] != 'r' || perms[2] != 'x') continue;

        Range r;
        r.start = (uintptr_t)strtoull(line.substr(0, dash).c_str(), nullptr, 16);
        r.end   = (uintptr_t)strtoull(line.substr(dash + 1, sp - dash - 1).c_str(), nullptr, 16);
        if (r.end > r.start) out.push_back(r);
    }
    return out;
}

// ── matching ────────────────────────────────────────────────────
inline bool match_at(const uint8_t* hay, size_t hayLen, size_t pos, const Pattern& pat) {
    if (pos + pat.size() > hayLen) return false;
    for (size_t i = 0; i < pat.size(); ++i)
        if (pat.care[i] && hay[pos + i] != pat.bytes[i]) return false;
    return true;
}

// Scans one buffer. Returns the index of the first hit, or npos.
inline size_t find_in(const uint8_t* hay, size_t hayLen, const Pattern& pat) {
    if (!pat.valid() || hayLen < pat.size()) return std::string::npos;
    const size_t last = hayLen - pat.size();
    for (size_t i = 0; i <= last; ++i)
        if (match_at(hay, hayLen, i, pat)) return i;
    return std::string::npos;
}

// ── a patch the menu can turn on and off ────────────────────────
struct Patch {
    std::string lib;        // e.g. "libil2cpp.so"
    std::string pattern;    // signature text
    int         offset = 0; // bytes from the match where the write lands
    std::string replace;    // bytes to write

    // filled in once applied, so it can be undone
    uintptr_t            at = 0;
    std::vector<uint8_t> original;
    bool                 applied = false;
};

// ── scanning and patching against live memory ───────────────────
// Reads and writes go through callbacks so this header does not depend on
// the driver, and so the logic can be exercised off-device.
using ReadFn  = std::function<bool(uintptr_t addr, void* buf, size_t len)>;
using WriteFn = std::function<bool(uintptr_t addr, const void* buf, size_t len)>;

// Walks the library's executable ranges in overlapping chunks. The overlap
// is pattern-1 bytes so a signature straddling a chunk boundary is still
// found; without it a hit could be missed depending on where it lands.
inline uintptr_t scan(int pid, const std::string& lib, const Pattern& pat,
                      const ReadFn& rd, std::string* why = nullptr) {
    if (!pat.valid()) { if (why) *why = "bad pattern"; return 0; }

    const std::vector<Range> ranges = module_ranges(pid, lib);
    if (ranges.empty()) { if (why) *why = "library not mapped: " + lib; return 0; }

    const size_t CHUNK = 64 * 1024;
    std::vector<uint8_t> buf(CHUNK);

    for (const Range& r : ranges) {
        for (uintptr_t a = r.start; a < r.end; ) {
            size_t want = (size_t)(r.end - a);
            if (want > CHUNK) want = CHUNK;
            if (want < pat.size()) break;

            if (rd(a, buf.data(), want)) {
                const size_t hit = find_in(buf.data(), want, pat);
                if (hit != std::string::npos) return a + (uintptr_t)hit;
            }
            // step back so a match across the seam is not lost
            a += want - (pat.size() - 1);
        }
    }
    if (why) *why = "pattern not found in " + lib;
    return 0;
}

// Finds the signature, saves the bytes it is about to overwrite, then
// writes. The saved bytes are what makes revert() possible, so a toggle
// works in both directions.
inline bool apply(Patch& p, int pid, const ReadFn& rd, const WriteFn& wr,
                  std::string* why = nullptr) {
    if (p.applied) return true;

    std::vector<uint8_t> newBytes;
    if (!parse_bytes(p.replace, newBytes) || newBytes.empty()) {
        if (why) *why = "bad replacement bytes";
        return false;
    }

    const Pattern pat = parse_pattern(p.pattern);
    const uintptr_t found = scan(pid, p.lib, pat, rd, why);
    if (!found) return false;

    const uintptr_t target = found + (uintptr_t)p.offset;

    std::vector<uint8_t> orig(newBytes.size());
    if (!rd(target, orig.data(), orig.size())) {
        if (why) *why = "could not read the original bytes";
        return false;
    }
    if (!wr(target, newBytes.data(), newBytes.size())) {
        if (why) *why = "write failed";
        return false;
    }

    p.at       = target;
    p.original = orig;
    p.applied  = true;
    return true;
}

inline bool revert(Patch& p, const WriteFn& wr, std::string* why = nullptr) {
    if (!p.applied) return true;
    if (p.original.empty() || p.at == 0) {
        if (why) *why = "nothing saved to restore";
        return false;
    }
    if (!wr(p.at, p.original.data(), p.original.size())) {
        if (why) *why = "restore write failed";
        return false;
    }
    p.applied = false;
    p.at      = 0;
    p.original.clear();
    return true;
}

// ── the patch table, filled from the server JSON ────────────────
inline std::mutex& table_mutex() { static std::mutex m; return m; }
inline std::map<std::string, Patch>& table() { static std::map<std::string, Patch> t; return t; }

} // namespace ix_aob
