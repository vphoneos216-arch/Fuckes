#pragma once
/* ============================================================
   ix_features.h — post-login feature gate
   Login OK  → server features unlock ESP / Aim / Exploit
   Login fail → all features locked (no menu toggles, no float)
   ============================================================ */
#include <atomic>
#include <cstring>
#include <string>
#include <mutex>
#include <json.hpp>

struct IxFeatureGate {
    std::atomic<bool> unlocked{false};  /* session authorized */
    std::atomic<bool> esp{false};
    std::atomic<bool> aim{false};
    std::atomic<bool> exploit{false};
};

inline IxFeatureGate& ix_feat() {
    static IxFeatureGate g;
    return g;
}

inline void ix_features_lock() {
    auto& f = ix_feat();
    f.unlocked.store(false, std::memory_order_release);
    f.esp.store(false, std::memory_order_release);
    f.aim.store(false, std::memory_order_release);
    f.exploit.store(false, std::memory_order_release);
}

inline void ix_features_unlock_all() {
    auto& f = ix_feat();
    f.unlocked.store(true, std::memory_order_release);
    f.esp.store(true, std::memory_order_release);
    f.aim.store(true, std::memory_order_release);
    f.exploit.store(true, std::memory_order_release);
}

/* Parse features object from signed payload or decrypted secret JSON */
inline void ix_features_apply_json(const nlohmann::json& j) {
    bool e = false, a = false, x = false;
    if (j.contains("features") && j["features"].is_object()) {
        const auto& ft = j["features"];
        e = ft.value("esp", false) || ft.value("ESP", false);
        a = ft.value("aim", false) || ft.value("AIM", false);
        x = ft.value("exploit", false) || ft.value("EXPLOIT", false);
        /* also accept nested toggles */
        if (ft.contains("esp") && ft["esp"].is_number()) e = ft["esp"].get<int>() != 0;
        if (ft.contains("aim") && ft["aim"].is_number()) a = ft["aim"].get<int>() != 0;
        if (ft.contains("exploit") && ft["exploit"].is_number()) x = ft["exploit"].get<int>() != 0;
    } else {
        /* No features block → remain locked until secret supplies features */
        e = a = x = false;
    }
    auto& f = ix_feat();
    f.unlocked.store(true, std::memory_order_release);
    f.esp.store(e, std::memory_order_release);
    f.aim.store(a, std::memory_order_release);
    f.exploit.store(x, std::memory_order_release);
}

/* Prefer encrypted secret body when it is JSON with features */
inline void ix_features_apply_secret(const std::string& secret) {
    if (secret.empty()) return;
    try {
        auto j = nlohmann::json::parse(secret);
        if (j.contains("features"))
            ix_features_apply_json(j);
    } catch (...) {
        /* secret may be opaque binary — ignore */
    }
}

inline bool ix_feat_ok()       { return ix_feat().unlocked.load(std::memory_order_acquire); }
inline bool ix_feat_esp()      { return ix_feat_ok() && ix_feat().esp.load(std::memory_order_acquire); }
inline bool ix_feat_aim()      { return ix_feat_ok() && ix_feat().aim.load(std::memory_order_acquire); }
inline bool ix_feat_exploit()  { return ix_feat_ok() && ix_feat().exploit.load(std::memory_order_acquire); }
