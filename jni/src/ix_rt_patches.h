#pragma once
/* ============================================================
   ix_rt_patches.h — game-file patch bytes from the server secret

   The same channel ix_rt_offsets.h uses, and nothing new on top of it:
   the bytes arrive inside the per-product secret that login.h already
   decrypts from the response's "enc" field, and are read out of that
   same parsed JSON object. There is no extra encryption layer and no
   extra response field — one less thing between a correct server and a
   working patch.

     secret JSON:
       { "features": {...},
         "offsets":  { "player": {...}, "bone": {...} },
         "patches":  [ { "slot":"hologram", "find":"<hex>",
                         "repl":"<hex>", "off":null, "size":null }, ... ] }

   Empty is a NORMAL state. A product with no patch rows simply has
   none, and ix_ff_patch.h then falls back to its compiled-in tables.
   That is why adding this cannot change a product that does not use it.
   ============================================================ */

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <utility>
#include <json.hpp>

struct IxSrvPatch {
    std::string slot;                   // "hologram", "asset", ...
    std::string target;                 // free-text hint, not a path
    std::vector<unsigned char> find;    // empty => write at off instead
    std::vector<unsigned char> repl;
    long long off  = -1;                // -1 => search for find
    long long size = -1;                // -1 => no size check
};

struct IxPatchBundle {
    std::vector<IxSrvPatch> rows;
    std::atomic<bool> ready{false};
    std::mutex mu;
};

inline IxPatchBundle& ix_patches() {
    static IxPatchBundle g;
    return g;
}

inline void ix_patch_clear() {
    auto& g = ix_patches();
    {
        std::lock_guard<std::mutex> lk(g.mu);
        g.rows.clear();
    }
    g.ready.store(false, std::memory_order_release);
}

/* Named ix_px_* rather than ix_hex* because main.cpp already defines an
   ix_hexval() for its crash handler and includes this header in the same
   translation unit. */
static inline int ix_px_hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Returns false on an odd length or any non-hex digit, so a malformed
   row is dropped whole rather than written half-decoded into a game
   file. */
static inline bool ix_px_hex2bin(const std::string& h,
                                 std::vector<unsigned char>& out) {
    out.clear();
    if (h.empty() || (h.size() % 2) != 0) return false;
    out.reserve(h.size() / 2);
    for (size_t i = 0; i + 1 < h.size(); i += 2) {
        const int hi = ix_px_hexval(h[i]);
        const int lo = ix_px_hexval(h[i + 1]);
        if (hi < 0 || lo < 0) { out.clear(); return false; }
        out.push_back((unsigned char)((hi << 4) | lo));
    }
    return true;
}

/* Reads root["patches"]. Never throws, never fatal: a missing or
   malformed array just leaves the table empty, and the caller treats
   empty as "use the compiled tables". */
inline void ix_patch_apply_json(const nlohmann::json& root) {
    ix_patch_clear();

    if (!root.contains("patches") || !root["patches"].is_array()) return;

    std::vector<IxSrvPatch> parsed;

    for (const auto& e : root["patches"]) {
        if (!e.is_object()) continue;

        IxSrvPatch p;
        p.slot = e.value("slot", std::string(""));
        if (p.slot.empty()) continue;
        p.target = e.value("target", std::string(""));

        const std::string fh = e.value("find", std::string(""));
        const std::string rh = e.value("repl", std::string(""));

        if (!ix_px_hex2bin(rh, p.repl)) continue;          // repl is required
        if (!fh.empty() && !ix_px_hex2bin(fh, p.find)) continue;

        if (e.contains("off") &&
            (e["off"].is_number_integer() || e["off"].is_number_unsigned())) {
            p.off = e["off"].get<long long>();
        }
        if (e.contains("size") &&
            (e["size"].is_number_integer() || e["size"].is_number_unsigned())) {
            p.size = e["size"].get<long long>();
        }

        /* The same two rules the server enforces, re-checked here rather
           than trusted: a row is either a find/replace pair of equal
           length, or an absolute-offset write. */
        if (!p.find.empty() && p.find.size() != p.repl.size()) continue;
        if (p.find.empty() && p.off < 0) continue;

        parsed.push_back(std::move(p));
    }

    auto& g = ix_patches();
    {
        std::lock_guard<std::mutex> lk(g.mu);
        g.rows = std::move(parsed);
    }
    g.ready.store(!ix_patches().rows.empty(), std::memory_order_release);
}

/* Copies out the rows for one slot, so file IO never runs while the
   table lock is held. */
inline std::vector<IxSrvPatch> ix_patch_rows(const char* slot) {
    std::vector<IxSrvPatch> out;
    if (!slot) return out;
    auto& g = ix_patches();
    std::lock_guard<std::mutex> lk(g.mu);
    for (const auto& r : g.rows) {
        if (r.slot == slot) out.push_back(r);
    }
    return out;
}

inline bool ix_patch_have(const char* slot) {
    return !ix_patch_rows(slot).empty();
}

inline size_t ix_patch_count() {
    auto& g = ix_patches();
    std::lock_guard<std::mutex> lk(g.mu);
    return g.rows.size();
}

/* First non-negative size declared for this slot, or -1 when the server
   sent none — in which case the caller keeps its own expected size. */
inline long long ix_patch_expect_size(const char* slot) {
    for (const auto& r : ix_patch_rows(slot)) {
        if (r.size >= 0) return r.size;
    }
    return -1;
}
