#ifndef OFFSET_H
#define OFFSET_H

#include <cstdint>
#include <atomic>

// =========================================
// DYNAMIC SECURE OFFSETS (ARM64 Bit)
// Supports Instant Nullification on Tamper
// =========================================

namespace Offsets {
    // ---- MainBase / GameFacade ----
    inline uintptr_t GameFacade_TypeInfo = 0xAD68E18;
    inline uintptr_t GameFacade          = 0xAD68E18;   // TypeInfo / BSS slot
    inline uintptr_t Staticfield         = 0xB8;
    inline uintptr_t StaticGameFacade    = 0xB8;
    inline uintptr_t BaseGame_HasInited  = 0x80;
    inline uintptr_t BaseGame_MatchEnd   = 0x8C;
    inline uintptr_t MatchIsRunning      = 0xCC;

    // ---- Match / Player container (MatchGame) ----
    inline uintptr_t CurrentMatch        = 0x90;
    inline uintptr_t localPlayer         = 0xD8;
    inline uintptr_t LocalPlayer         = 0xD8;
    inline uintptr_t CurrentObserve      = 0x100;
    inline uintptr_t ObserverPlayer      = 0x30;
    inline uintptr_t Dictionary          = 0xC0;
    inline uintptr_t DictionaryEntities  = 0xC0;
    inline uintptr_t LDictionaryEntities = 0xC8;

    // ---- Camera chain ----
    inline uintptr_t CameraBase          = 0xD8;
    inline uintptr_t Camera              = 0x20;
    inline uintptr_t IntPtrCam           = 0x10;
    inline uintptr_t Matrix              = 0x100;
    inline uintptr_t FollowCamera        = 0x698;
    inline uintptr_t MainCameraTransform = 0x3F0;

    // ---- Player flags & info ----
    inline uintptr_t Player_IsDead       = 0x7C;
    inline uintptr_t Player_isdead       = 0x7C;
    inline uintptr_t Player_Data         = 0x70;        // GetCurHP
    inline uintptr_t GetCurHP            = 0x70;
    inline uintptr_t GetCur              = 0x10;
    inline uintptr_t entryAddr           = 0x18;
    inline uintptr_t enemyList           = 0x10;
    inline uintptr_t HP                  = 0x20;
    inline uintptr_t Health              = 0x18;
    inline uintptr_t IsClientBot         = 0x4A8;
    inline uintptr_t IsBot               = 0x4A8;
    inline uintptr_t NamePtr             = 0x498;
    inline uintptr_t Player_Name         = 0x498;

    // ---- Avatar / team / visible ----
    inline uintptr_t AvatarManager       = 0x778;
    inline uintptr_t UmaAvatarSimple     = 0x138;
    inline uintptr_t Avatar              = 0x138;
    inline uintptr_t IsVisible           = 0x101;
    inline uintptr_t Avatar_IsVisible    = 0x101;
    inline uintptr_t UmaData             = 0x28;
    inline uintptr_t Avatar_Data         = 0x28;
    inline uintptr_t UmaData_IsTeam      = 0x81;
    inline uintptr_t Avatar_Data_IsTeam  = 0x81;

    // ---- Aim & SilentAim ----
    inline uintptr_t AimRotation         = 0x61C;
    inline uintptr_t Colider_Field       = 0x80;
    inline uintptr_t HedColider          = 0x740;
    inline uintptr_t aimInfo             = 0xE58;
    inline uintptr_t startPos            = 0x4C;
    inline uintptr_t raycast             = 0x40;
    inline uintptr_t isFiring            = 0x850;
    inline uintptr_t sAim1               = 0x850;       // isFiring
    inline uintptr_t sAim2               = 0xE58;       // aimInfo
    inline uintptr_t sAim3               = 0x4C;        // startPos
    inline uintptr_t sAim4               = 0x40;        // raycast
    inline uintptr_t CallSetAimRotationCount = 0x79C;

    // ---- Bones ----
    inline uintptr_t Head                = 0x6A8;
    inline uintptr_t Spine               = 0x6B8;
    inline uintptr_t Hip                 = 0x6B0;
    inline uintptr_t Root                = 0x6D0;
    inline uintptr_t lHand               = 0x728;
    inline uintptr_t rHand               = 0x720;
    inline uintptr_t lElbow              = 0x738;
    inline uintptr_t rElbow              = 0x730;
    inline uintptr_t lShoulder           = 0x710;
    inline uintptr_t rShoulder           = 0x718;
    inline uintptr_t lAnkle              = 0x6E0;
    inline uintptr_t rAnkle              = 0x6E8;
    inline uintptr_t lToe                = 0x6F0;
    inline uintptr_t rToe                = 0x6F8;

    // ---- Weapon ----
    inline uintptr_t InventoryManager    = 0x748;
    inline uintptr_t Weapon              = 0x608;
    inline uintptr_t Player_ActiveUISightingWeapon = 0x608;
    inline uintptr_t Player_WeaponOnHand = 0x17E8;
    inline uintptr_t Item                = 0xA0;
    inline uintptr_t WeaponComponent     = 0x80;
    inline uintptr_t ItemData            = 0x28;
    inline uintptr_t ItemData_WeaponId   = 0x40;
    inline uintptr_t NoRecoil            = 0x18;
    inline uintptr_t Weapon_Data         = 0x98;
    inline uintptr_t WeaponData_Type     = 0x64;
    inline uintptr_t WeaponData_AimType  = 0xC8;
    inline int       WeaponType_Sniper   = 1;

    // ---- Knock & Health ----
    inline uintptr_t PhyXdata            = 0x1D60;
    inline uintptr_t maybeDead           = 0x20;
    inline uintptr_t isKnocked           = 0x10;

    // ---- Misc ----
    inline uintptr_t GhostHack           = 0x814;
    inline uintptr_t PlayerAttributes    = 0x770;
    inline uintptr_t ShootNoReload       = 0x111;

    // ---- Transform ----
    inline uintptr_t transformValue      = 0x10;
    inline uintptr_t transformObjValue   = 0x10;
    inline uintptr_t indexValue          = 0x40;
    inline uintptr_t matrixValue         = 0x38;
    inline uintptr_t matrixListValue     = 0x18;
    inline uintptr_t matrixIndicesValue  = 0x20;
    inline uintptr_t resultValue         = 0x30;
    inline uintptr_t tMatrixValue        = 0x30;

    // Selected-version fields; MAX INDIA defaults preserve existing behavior.
    inline uintptr_t Feet = Root;
    inline uintptr_t CurrentGame = 0x0;
    inline uintptr_t CurrentAimRotation = 0x1AA4;

    inline std::atomic<bool> g_nullified{false};

    // ========================================================================
    // Offset Nullification (Wipes all offsets to 0 when tamper is detected)
    // ========================================================================
    inline void Nullify() {
        g_nullified.store(true, std::memory_order_relaxed);

        Feet = 0; CurrentGame = 0; CurrentAimRotation = 0;
        GameFacade_TypeInfo = 0;
        GameFacade          = 0;
        Staticfield         = 0;
        StaticGameFacade    = 0;
        BaseGame_HasInited  = 0;
        BaseGame_MatchEnd   = 0;
        MatchIsRunning      = 0;

        CurrentMatch        = 0;
        localPlayer         = 0;
        LocalPlayer         = 0;
        CurrentObserve      = 0;
        ObserverPlayer      = 0;
        Dictionary          = 0;
        DictionaryEntities  = 0;
        LDictionaryEntities = 0;

        CameraBase          = 0;
        Camera              = 0;
        IntPtrCam           = 0;
        Matrix              = 0;
        FollowCamera        = 0;
        MainCameraTransform = 0;

        Player_IsDead       = 0;
        Player_isdead       = 0;
        Player_Data         = 0;
        GetCurHP            = 0;
        GetCur              = 0;
        entryAddr           = 0;
        enemyList           = 0;
        HP                  = 0;
        Health              = 0;
        IsClientBot         = 0;
        IsBot               = 0;
        NamePtr             = 0;
        Player_Name         = 0;

        AvatarManager       = 0;
        UmaAvatarSimple     = 0;
        Avatar              = 0;
        IsVisible           = 0;
        Avatar_IsVisible    = 0;
        UmaData             = 0;
        Avatar_Data         = 0;
        UmaData_IsTeam      = 0;
        Avatar_Data_IsTeam  = 0;

        AimRotation         = 0;
        Colider_Field       = 0;
        HedColider          = 0;
        aimInfo             = 0;
        startPos            = 0;
        raycast             = 0;
        isFiring            = 0;
        sAim1               = 0;
        sAim2               = 0;
        sAim3               = 0;
        sAim4               = 0;
        CallSetAimRotationCount = 0;

        Head                = 0;
        Spine               = 0;
        Hip                 = 0;
        Root                = 0;
        lHand               = 0;
        rHand               = 0;
        lElbow              = 0;
        rElbow              = 0;
        lShoulder           = 0;
        rShoulder           = 0;
        lAnkle              = 0;
        rAnkle              = 0;
        lToe                = 0;
        rToe                = 0;

        InventoryManager    = 0;
        Weapon              = 0;
        Player_ActiveUISightingWeapon = 0;
        Player_WeaponOnHand = 0;
        Item                = 0;
        WeaponComponent     = 0;
        ItemData            = 0;
        ItemData_WeaponId   = 0;
        NoRecoil            = 0;
        Weapon_Data         = 0;
        WeaponData_Type     = 0;
        WeaponData_AimType  = 0;

        PhyXdata            = 0;
        maybeDead           = 0;
        isKnocked           = 0;

        GhostHack           = 0;
        PlayerAttributes    = 0;
        ShootNoReload       = 0;

        transformValue      = 0;
        transformObjValue   = 0;
        indexValue          = 0;
        matrixValue         = 0;
        matrixListValue     = 0;
        matrixIndicesValue  = 0;
        resultValue         = 0;
        tMatrixValue        = 0;
    }

    inline bool IsNullified() {
        return g_nullified.load(std::memory_order_relaxed);
    }
}

namespace bone {
    inline uintptr_t HedColider          = 0x740;
    inline uintptr_t Head                = 0x6A8;
    inline uintptr_t Spine               = 0x6B8;
    inline uintptr_t Hip                 = 0x6B0;
    inline uintptr_t Root                = 0x6D0;
    inline uintptr_t lHand               = 0x728;
    inline uintptr_t rHand               = 0x720;
    inline uintptr_t lElbow              = 0x738;
    inline uintptr_t rElbow              = 0x730;
    inline uintptr_t lShoulder           = 0x710;
    inline uintptr_t rShoulder           = 0x718;
    inline uintptr_t lAnkle              = 0x6E0;
    inline uintptr_t rAnkle              = 0x6E8;
    inline uintptr_t lToe                = 0x6F0;
    inline uintptr_t rToe                = 0x6F8;

    inline void Nullify() {
        HedColider = 0;
        Head = 0;
        Spine = 0;
        Hip = 0;
        Root = 0;
        lHand = 0;
        rHand = 0;
        lElbow = 0;
        rElbow = 0;
        lShoulder = 0;
        rShoulder = 0;
        lAnkle = 0;
        rAnkle = 0;
        lToe = 0;
        rToe = 0;
    }
}

namespace offset {
    using namespace Offsets;
}

#endif // OFFSET_H
