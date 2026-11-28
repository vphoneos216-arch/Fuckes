#pragma once
// ─────────────────────────────────────────────────────────────────────
//  HWBP AIM  —  Aimbot (quaternion) and HWBPY (bullet track)
// ─────────────────────────────────────────────────────────────────────
//
// Ported from the "Hardware bt" source. Both features work the same way:
// plant an EXECUTE hardware breakpoint on a libil2cpp instruction, and
// when it hits, rewrite the SIMD (S/Q) registers the game is about to
// use. Nothing in game memory is ever written.
//
//   Aimbot  -> aim QUATERNION site. Rewrites S0-S3 (or S11/S10/S9/S8),
//              which actually turns the player's aim.
//   HWBPY   -> raycast DIRECTION site. Rewrites the direction registers
//              only, so the bullet tracks the target while the view
//              stays put. This is the bullet-track feature.
//
// Driver: Paradise only (backend 3 in the source's own numbering). The
// source's aim_hwbp_* wrappers collapse to four Paradise calls, which is
// all this file uses:
//
//   install -> hwbp_set(EXECUTE, len 4, ALL_THREADS)
//   poll    -> hwbp_get_records(index, 0, &record, 1, &count)
//   write   -> hwbp_set_record(index, 0, record)   (q regs masked WRITE)
//   remove  -> hwbp_remove()
//
// PROVENANCE OF EVERY CONSTANT  (nothing here is guessed)
//   * IsFiring, HitObjInfo, Head/Neck/Chest  -> the panel's own
//     server-delivered offsets, via PlayerOffsets/BoneOffsets.
//   * The four code RVAs, the S-register maps, the context-guard rules
//     and AimRotation (0x61C) -> the "Hardware bt" source supplied for
//     this work (src/func/breakpoint_rewrite.hpp, hwbp_aim_control.hpp,
//     src/game/Offsets.h). They are libil2cpp RVAs and therefore build
//     dependent; they are all gathered in IxHwbpCfg below so a new build
//     is one edit, not a hunt.

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>

#include "Paradise/paradise_api.h"

namespace ixhw {

// ── build-dependent values, all from the supplied source ─────────────
namespace IxHwbpCfg {
    // Aim quaternion sites. Option A / Option B in the source's menu.
    static constexpr uint64_t RVA_AIM_QUAT_A = 0x7014464ULL; // arg in S0-S3
    static constexpr uint64_t RVA_AIM_QUAT_B = 0x7014510ULL; // store S11/S10/S9/S8
    // Raycast direction sites -> the bullet track.
    static constexpr uint64_t RVA_DIR_A      = 0x7C5405CULL; // dir S3/S4/S5
    static constexpr uint64_t RVA_DIR_B      = 0x7C56530ULL; // dir S11/S10/S9
    // Player + this = aim rotation quaternion, used only to pick the
    // hemisphere so the aim cannot flip 360 degrees.
    static constexpr uintptr_t AIM_ROTATION  = 0x61C;
    // *(Player + HitObjInfo) + this = ray origin.
    static constexpr uintptr_t AIM_ORIGIN    = 0x4C;
}

// ── which SIMD registers each site carries ───────────────────────────
// Verbatim from breakpoint_rewrite.hpp.
struct RewriteSpec { bool quaternion; std::array<unsigned,4> s; };
static inline RewriteSpec rewrite_spec(uint64_t rva) {
    switch (rva) {
        case IxHwbpCfg::RVA_DIR_A:      return {false, {3,4,5,0}};
        case IxHwbpCfg::RVA_DIR_B:      return {false, {11,10,9,0}};
        case IxHwbpCfg::RVA_AIM_QUAT_A: return {true,  {0,1,2,3}};
        case IxHwbpCfg::RVA_AIM_QUAT_B: return {true,  {11,10,9,8}};
        default:                        return {false, {0,0,0,0}};
    }
}

static inline bool ValidPtr(uint64_t p){ return p >= 0x10000 && p < 0x00007FFFFFFFFFFFULL; }
static inline bool Finite3(const Vector3& v){
    return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
}

// ── is this hit ours? ────────────────────────────────────────────────
// Verbatim rules from hwbp_aim_control.hpp::record_matches. Without
// these the breakpoint also fires for other players' rays and the
// rewrite lands on someone else's shot.
static inline bool RecordMatches(uint64_t rva, bool quat, uint64_t base, uint64_t pc,
                                 uint64_t local, uint64_t info, uint64_t adjusted,
                                 uint64_t x0, uint64_t x1, uint64_t x8) {
    if (pc != base + rva) return false;
    if (quat) {
        // quaternion path: x0 must be the local player
        if (x0 != 0 && local != 0 && x0 != local) return false;
        return true;
    }
    if (rva == IxHwbpCfg::RVA_DIR_A) {
        // at this site x0 is the raycast helper, not the player; only x1
        // identifies our AimInfo context
        return x1 == local + 0xE58 || x1 == local + 0xE60 ||
               (info && x1 == info) || (adjusted && x1 == adjusted);
    }
    // alternate output: x8 holds the AimInfo pointer
    return (info && x8 == info) || (adjusted && x8 == adjusted);
}

// ── target motion, for lead ──────────────────────────────────────────
// Verbatim shape from hwbp_aim_control.hpp::Motion.
class Motion {
    std::array<float,3> previous_{}, velocity_{};
    uint64_t sequence_ = 0;
    double   sampled_  = 0;
public:
    void reset(){ *this = Motion{}; }
    std::array<float,3> observe(std::array<float,3> p, uint64_t sequence, double sampled){
        if (sequence == sequence_) return velocity_;
        for (float v : p) if (!std::isfinite(v)) { reset(); return {}; }
        const double dt = sampled - sampled_;
        if (sequence_ && dt >= 0.005 && dt <= 0.12) {
            std::array<float,3> raw{}; float square = 0, dot = 0;
            for (unsigned i=0;i<3;++i){
                raw[i] = float((p[i]-previous_[i])/dt);
                square += raw[i]*raw[i];
                dot    += raw[i]*velocity_[i];
            }
            if (square > 1600) velocity_ = {};
            else {
                float alpha = float(1 - std::exp(-dt/(square < 0.0625 ? 0.01 : 0.02)));
                if (dot < 0) alpha = alpha > 0.9f ? alpha : 0.9f;
                for (unsigned i=0;i<3;++i) velocity_[i] += alpha*(raw[i]-velocity_[i]);
            }
        } else velocity_ = {};
        previous_ = p; sequence_ = sequence; sampled_ = sampled;
        return velocity_;
    }
};

// Verbatim from hwbp_aim_control.hpp::predicted.
static inline std::array<float,3> Predicted(std::array<float,3> world,
                                            std::array<float,3> velocity,
                                            double age, float lead_ms) {
    double l = double(lead_ms); if (l < 0) l = 0; if (l > 100) l = 100;
    double h = age + l/1000.0; if (h < 0) h = 0; if (h > 0.12) h = 0.12;
    const float horizon = float(h);
    float length = 0; std::array<float,3> shift{};
    for (unsigned i=0;i<3;++i){ shift[i] = velocity[i]*horizon; length += shift[i]*shift[i]; }
    const float scale = length > 1.0f ? 1.0f/std::sqrt(length) : 1.0f;
    for (unsigned i=0;i<3;++i) world[i] += shift[i]*scale;
    return world;
}

// ── settings, mirrored by the menu ───────────────────────────────────
struct Settings {
    bool  aimbot = false;      // the source's "x" (quaternion site)
    bool  hwbpy  = false;      // the source's "y" (direction site = bullet track)
    int   aimbot_option = 0;   // A / B
    int   hwbpy_option  = 0;   // A / B
    int   bone   = 0;          // 0 Head, 1 Neck, 2 Chest
    bool  fire_check = false;
    float lead_ms = 16.f;
};

struct Point { uint64_t rva = 0; bool quaternion = false; };

// Verbatim from hwbp_aim_control.hpp::plan.
static inline std::array<Point,2> Plan(const Settings& s, unsigned& count) {
    std::array<Point,2> pts{}; count = 0;
    if (s.aimbot) pts[count++] = { s.aimbot_option == 0 ? IxHwbpCfg::RVA_AIM_QUAT_A
                                                        : IxHwbpCfg::RVA_AIM_QUAT_B, true };
    if (s.hwbpy)  pts[count++] = { s.hwbpy_option  == 0 ? IxHwbpCfg::RVA_DIR_A
                                                        : IxHwbpCfg::RVA_DIR_B,      false };
    return pts;
}
static inline bool SamePlan(const Settings& a, const Settings& b) {
    return a.aimbot == b.aimbot && a.hwbpy == b.hwbpy &&
           (!a.aimbot || a.aimbot_option == b.aimbot_option) &&
           (!a.hwbpy  || a.hwbpy_option  == b.hwbpy_option);
}

// ── the four Paradise calls the source's wrappers collapse to ────────
static inline bool PdInstall(paradise_driver* pd, const uintptr_t* addrs, unsigned count){
    if (!pd || !addrs || !count || count > 2) return false;
    paradise_bp_point_config cfg[2]{};
    for (unsigned i=0;i<count;++i){
        if (!addrs[i] || (addrs[i] & 3)) return false;
        cfg[i].address = addrs[i];
        cfg[i].type    = PARADISE_BP_EXECUTE;
        cfg[i].length  = 4;
        cfg[i].scope   = PARADISE_BP_ALL_THREADS;
        cfg[i].reserved= 0;
    }
    return pd->hwbp_set(cfg, count);
}
static inline bool PdPoll(paradise_driver* pd, unsigned index,
                          paradise_bp_record& rec, bool& available, uint64_t& hits){
    rec = {}; available = false; hits = 0;
    if (!pd) return false;
    uint32_t count = 0;
    const bool ok = pd->hwbp_get_records(index, 0, &rec, 1, &count);
    available = (count == 1);
    hits = rec.hit_count;
    return ok;
}
// Clear every edit on a point without disturbing the breakpoint itself.
static inline bool PdClearEdits(paradise_driver* pd, unsigned index, paradise_bp_record rec){
    if (!pd) return false;
    for (int r = 0; r < PARADISE_REG_COUNT; ++r)
        PARADISE_BP_SET_MASK(&rec, r, PARADISE_BP_OP_READ);
    return pd->hwbp_set_record(index, 0, &rec);
}
// Write floats into the SIMD registers the site uses and mask them WRITE.
static inline bool PdWriteRegs(paradise_driver* pd, unsigned index, paradise_bp_record rec,
                               const std::array<unsigned,4>& regs,
                               const std::array<float,4>& values, unsigned n){
    if (!pd) return false;
    for (int r = 0; r < PARADISE_REG_COUNT; ++r)
        PARADISE_BP_SET_MASK(&rec, r, PARADISE_BP_OP_READ);
    for (unsigned i = 0; i < n; ++i){
        __uint128_t q = 0;
        std::memcpy(&q, &values[i], sizeof(float));
        std::memcpy(reinterpret_cast<char*>(&rec) + offsetof(paradise_bp_record, q0) + regs[i]*16,
                    &q, 16);
        PARADISE_BP_SET_MASK(&rec, PARADISE_REG_Q0 + regs[i], PARADISE_BP_OP_WRITE);
    }
    return pd->hwbp_set_record(index, 0, &rec);
}

} // namespace ixhw
