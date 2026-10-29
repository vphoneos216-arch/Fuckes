#pragma once
/* ============================================================
   ix_rt_offsets.h — runtime offsets from server secret only
   Default = 0. Filled only after valid login decrypts secret.
   ============================================================ */
#include <cstdint>
#include <cstring>
#include <string>
#include <atomic>
#include <mutex>
#include <json.hpp>
#include "ix_features.h"

struct IxPlayerOff {
    uintptr_t IsDead = 0;
    uintptr_t AimCollider = 0;
    uintptr_t ColliderNode = 0;
    uintptr_t HealthLink1 = 0, HealthLink2 = 0, HealthLink3 = 0, Health = 0;
    uintptr_t NickName = 0, IsClientBot = 0;
    uintptr_t MainCamera = 0, FollowCamera = 0;
    uintptr_t Rotation = 0, IsFiringSilent = 0, IsFiring = 0;
    uintptr_t HitObjInfo = 0, AmmoBase = 0, SilentAimDir = 0;
    uintptr_t AvatarManager = 0;
    uintptr_t Status = 0, StatusSub = 0, StatusState = 0;
    uintptr_t AvatarSimple = 0, IsVisible = 0, UmaData = 0, UmaDataDead = 0;
    uintptr_t GameFacade = 0, FacadeGame = 0, FacadeBase = 0, BaseGame = 0;
    uintptr_t MatchRunning = 0, MatchLocal = 0, Dictionary = 0;
    uintptr_t DictValues = 0, EntityStride = 0;
    int       MaxEntities = 0;
    uintptr_t CameraStep1 = 0, CameraStep2 = 0, ViewMatrix = 0;
    uintptr_t GhostHack = 0, WeaponInstance = 0, WeaponRecoilValue = 0;
    uintptr_t InventoryManager = 0, InventoryActiveItem = 0;
    uintptr_t InventoryWeaponComp = 0, InventoryNoRecoil = 0;
    uintptr_t PlayerAttributes = 0, ShootNoReload = 0;
    /* Transform */
    uintptr_t TfObject = 0, TfIndex = 0, TfMatrix = 0;
    uintptr_t TfMatrixList = 0, TfMatrixIndices = 0, TfStride = 0;
    int       TfMaxTries = 0;
};

struct IxBoneOff {
    uintptr_t Head = 0, Hip = 0, Chest = 0, Root = 0;
    uintptr_t LeftShoulder = 0, RightShoulder = 0;
    uintptr_t LeftAnkle = 0, RightAnkle = 0;
    uintptr_t LeftToe = 0, RightToe = 0;
    uintptr_t RightForearm = 0, LeftForearm = 0;
    uintptr_t RightHand = 0, LeftHand = 0;
};

struct IxRtBundle {
    IxPlayerOff P{};
    IxBoneOff   B{};
    std::atomic<bool> ready{false};
};

inline IxRtBundle& ix_rt() {
    static IxRtBundle g;
    return g;
}

inline void ix_rt_clear() {
    auto& g = ix_rt();
    g.P = IxPlayerOff{};
    g.B = IxBoneOff{};
    g.ready.store(false, std::memory_order_release);
}

static inline uintptr_t ix_parse_off(const nlohmann::json& j, const char* key, uintptr_t def = 0) {
    if (!j.contains(key)) return def;
    const auto& v = j[key];
    if (v.is_number_unsigned() || v.is_number_integer())
        return (uintptr_t)v.get<uint64_t>();
    if (v.is_string()) {
        std::string s = v.get<std::string>();
        if (s.size() > 2 && s[0]=='0' && (s[1]=='x' || s[1]=='X'))
            return (uintptr_t)strtoull(s.c_str(), nullptr, 16);
        return (uintptr_t)strtoull(s.c_str(), nullptr, 0);
    }
    return def;
}

inline bool ix_rt_apply_json(const nlohmann::json& root) {
    if (!root.contains("offsets") || !root["offsets"].is_object())
        return false;
    const auto& o = root["offsets"];
    auto& P = ix_rt().P;
    auto& B = ix_rt().B;

    nlohmann::json pj = o.contains("player") ? o["player"] : o;
    nlohmann::json bj = o.contains("bone")   ? o["bone"]   : o;

    P.IsDead          = ix_parse_off(pj, "IsDead");
    P.AimCollider     = ix_parse_off(pj, "AimCollider");
    P.ColliderNode    = ix_parse_off(pj, "ColliderNode");
    P.HealthLink1     = ix_parse_off(pj, "HealthLink1");
    P.HealthLink2     = ix_parse_off(pj, "HealthLink2");
    P.HealthLink3     = ix_parse_off(pj, "HealthLink3");
    P.Health          = ix_parse_off(pj, "Health");
    P.NickName        = ix_parse_off(pj, "NickName");
    P.IsClientBot     = ix_parse_off(pj, "IsClientBot");
    P.MainCamera      = ix_parse_off(pj, "MainCamera");
    P.FollowCamera    = ix_parse_off(pj, "FollowCamera");
    P.Rotation        = ix_parse_off(pj, "Rotation");
    P.IsFiringSilent  = ix_parse_off(pj, "IsFiringSilent");
    P.IsFiring        = ix_parse_off(pj, "IsFiring");
    P.HitObjInfo      = ix_parse_off(pj, "HitObjInfo");
    P.AmmoBase        = ix_parse_off(pj, "AmmoBase");
    P.SilentAimDir    = ix_parse_off(pj, "SilentAimDir");
    P.AvatarManager   = ix_parse_off(pj, "AvatarManager");
    P.Status          = ix_parse_off(pj, "Status");
    P.StatusSub       = ix_parse_off(pj, "StatusSub");
    P.StatusState     = ix_parse_off(pj, "StatusState");
    P.AvatarSimple    = ix_parse_off(pj, "AvatarSimple");
    P.IsVisible       = ix_parse_off(pj, "IsVisible");
    P.UmaData         = ix_parse_off(pj, "UmaData");
    P.UmaDataDead     = ix_parse_off(pj, "UmaDataDead");
    P.GameFacade      = ix_parse_off(pj, "GameFacade");
    P.FacadeGame      = ix_parse_off(pj, "FacadeGame");
    P.FacadeBase      = ix_parse_off(pj, "FacadeBase");
    P.BaseGame        = ix_parse_off(pj, "BaseGame");
    P.MatchRunning    = ix_parse_off(pj, "MatchRunning");
    P.MatchLocal      = ix_parse_off(pj, "MatchLocal");
    P.Dictionary      = ix_parse_off(pj, "Dictionary");
    P.DictValues      = ix_parse_off(pj, "DictValues");
    P.EntityStride    = ix_parse_off(pj, "EntityStride");
    P.MaxEntities     = (int)ix_parse_off(pj, "MaxEntities", 1000);
    P.CameraStep1     = ix_parse_off(pj, "CameraStep1");
    P.CameraStep2     = ix_parse_off(pj, "CameraStep2");
    P.ViewMatrix      = ix_parse_off(pj, "ViewMatrix");
    P.GhostHack       = ix_parse_off(pj, "GhostHack");
    P.WeaponInstance  = ix_parse_off(pj, "WeaponInstance");
    P.WeaponRecoilValue = ix_parse_off(pj, "WeaponRecoilValue");
    P.InventoryManager = ix_parse_off(pj, "InventoryManager");
    P.InventoryActiveItem = ix_parse_off(pj, "InventoryActiveItem");
    P.InventoryWeaponComp = ix_parse_off(pj, "InventoryWeaponComp");
    P.InventoryNoRecoil = ix_parse_off(pj, "InventoryNoRecoil");
    P.PlayerAttributes = ix_parse_off(pj, "PlayerAttributes");
    P.ShootNoReload   = ix_parse_off(pj, "ShootNoReload");
    P.TfObject        = ix_parse_off(pj, "TransformObject", ix_parse_off(pj, "TfObject", 0x10));
    P.TfIndex         = ix_parse_off(pj, "TransformIndex", ix_parse_off(pj, "TfIndex", 0x40));
    P.TfMatrix        = ix_parse_off(pj, "TransformMatrix", ix_parse_off(pj, "TfMatrix", 0x38));
    P.TfMatrixList    = ix_parse_off(pj, "TransformMatrixList", ix_parse_off(pj, "TfMatrixList", 0x18));
    P.TfMatrixIndices = ix_parse_off(pj, "TransformMatrixIndices", ix_parse_off(pj, "TfMatrixIndices", 0x20));
    P.TfStride        = ix_parse_off(pj, "TransformStride", ix_parse_off(pj, "TfStride", 0x30));
    P.TfMaxTries      = (int)ix_parse_off(pj, "TransformMaxTries", 50);

    B.Head          = ix_parse_off(bj, "Head");
    B.Hip           = ix_parse_off(bj, "Hip");
    B.Chest         = ix_parse_off(bj, "Chest");
    B.Root          = ix_parse_off(bj, "Root");
    B.LeftShoulder  = ix_parse_off(bj, "LeftShoulder");
    B.RightShoulder = ix_parse_off(bj, "RightShoulder");
    B.LeftAnkle     = ix_parse_off(bj, "LeftAnkle");
    B.RightAnkle    = ix_parse_off(bj, "RightAnkle");
    B.LeftToe       = ix_parse_off(bj, "LeftToe");
    B.RightToe      = ix_parse_off(bj, "RightToe");
    B.RightForearm  = ix_parse_off(bj, "RightForearm");
    B.LeftForearm   = ix_parse_off(bj, "LeftForearm");
    B.RightHand     = ix_parse_off(bj, "RightHand");
    B.LeftHand      = ix_parse_off(bj, "LeftHand");

    /* Minimum required for any ESP chain */
    bool ok = (P.GameFacade != 0 && B.Head != 0);
    ix_rt().ready.store(ok, std::memory_order_release);
    return ok;
}

inline bool ix_rt_ready() {
    return ix_rt().ready.load(std::memory_order_acquire) && ix_feat_ok();
}
