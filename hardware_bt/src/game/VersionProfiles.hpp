#pragma once
#include "Offsets.h"
#include <cstddef>
#include <cstring>

// Applied once on the main thread before any workers start.
namespace versions {
struct OffsetValues {
    std::uintptr_t GameFacade_TypeInfo;
    std::uintptr_t GameFacade;
    std::uintptr_t Staticfield;
    std::uintptr_t StaticGameFacade;
    std::uintptr_t BaseGame_HasInited;
    std::uintptr_t BaseGame_MatchEnd;
    std::uintptr_t MatchIsRunning;
    std::uintptr_t CurrentMatch;
    std::uintptr_t localPlayer;
    std::uintptr_t LocalPlayer;
    std::uintptr_t CurrentObserve;
    std::uintptr_t ObserverPlayer;
    std::uintptr_t Dictionary;
    std::uintptr_t DictionaryEntities;
    std::uintptr_t LDictionaryEntities;
    std::uintptr_t CameraBase;
    std::uintptr_t Camera;
    std::uintptr_t IntPtrCam;
    std::uintptr_t Matrix;
    std::uintptr_t FollowCamera;
    std::uintptr_t MainCameraTransform;
    std::uintptr_t Player_IsDead;
    std::uintptr_t Player_isdead;
    std::uintptr_t Player_Data;
    std::uintptr_t GetCurHP;
    std::uintptr_t GetCur;
    std::uintptr_t entryAddr;
    std::uintptr_t enemyList;
    std::uintptr_t HP;
    std::uintptr_t Health;
    std::uintptr_t IsClientBot;
    std::uintptr_t IsBot;
    std::uintptr_t NamePtr;
    std::uintptr_t Player_Name;
    std::uintptr_t AvatarManager;
    std::uintptr_t UmaAvatarSimple;
    std::uintptr_t Avatar;
    std::uintptr_t IsVisible;
    std::uintptr_t Avatar_IsVisible;
    std::uintptr_t UmaData;
    std::uintptr_t Avatar_Data;
    std::uintptr_t UmaData_IsTeam;
    std::uintptr_t Avatar_Data_IsTeam;
    std::uintptr_t AimRotation;
    std::uintptr_t Colider_Field;
    std::uintptr_t HedColider;
    std::uintptr_t aimInfo;
    std::uintptr_t startPos;
    std::uintptr_t raycast;
    std::uintptr_t isFiring;
    std::uintptr_t sAim1;
    std::uintptr_t sAim2;
    std::uintptr_t sAim3;
    std::uintptr_t sAim4;
    std::uintptr_t CallSetAimRotationCount;
    std::uintptr_t Head;
    std::uintptr_t Spine;
    std::uintptr_t Hip;
    std::uintptr_t Root;
    std::uintptr_t lHand;
    std::uintptr_t rHand;
    std::uintptr_t lElbow;
    std::uintptr_t rElbow;
    std::uintptr_t lShoulder;
    std::uintptr_t rShoulder;
    std::uintptr_t lAnkle;
    std::uintptr_t rAnkle;
    std::uintptr_t lToe;
    std::uintptr_t rToe;
    std::uintptr_t InventoryManager;
    std::uintptr_t Weapon;
    std::uintptr_t Player_ActiveUISightingWeapon;
    std::uintptr_t Player_WeaponOnHand;
    std::uintptr_t Item;
    std::uintptr_t WeaponComponent;
    std::uintptr_t ItemData;
    std::uintptr_t ItemData_WeaponId;
    std::uintptr_t NoRecoil;
    std::uintptr_t Weapon_Data;
    std::uintptr_t WeaponData_Type;
    std::uintptr_t WeaponData_AimType;
    std::uintptr_t WeaponType_Sniper;
    std::uintptr_t PhyXdata;
    std::uintptr_t maybeDead;
    std::uintptr_t isKnocked;
    std::uintptr_t GhostHack;
    std::uintptr_t PlayerAttributes;
    std::uintptr_t ShootNoReload;
    std::uintptr_t transformValue;
    std::uintptr_t transformObjValue;
    std::uintptr_t indexValue;
    std::uintptr_t matrixValue;
    std::uintptr_t matrixListValue;
    std::uintptr_t matrixIndicesValue;
    std::uintptr_t resultValue;
    std::uintptr_t tMatrixValue;
    std::uintptr_t Feet;
    std::uintptr_t CurrentGame;
    std::uintptr_t CurrentAimRotation;
};
struct Profile { const char* label; const char* version; const char* package; OffsetValues offsets; };

inline constexpr Profile MaxIndia {"MAX INDIA","2.133.1","com.dts.freefiremax",{
    0xAD68E18, // GameFacade_TypeInfo: current working MAX INDIA
    0xAD68E18, // GameFacade: current working MAX INDIA
    0xB8, // Staticfield: current working MAX INDIA
    0xB8, // StaticGameFacade: current working MAX INDIA
    0x80, // BaseGame_HasInited: current working MAX INDIA
    0x8C, // BaseGame_MatchEnd: current working MAX INDIA
    0xCC, // MatchIsRunning: current working MAX INDIA
    0x90, // CurrentMatch: current working MAX INDIA
    0xD8, // localPlayer: current working MAX INDIA
    0xD8, // LocalPlayer: current working MAX INDIA
    0x100, // CurrentObserve: current working MAX INDIA
    0x30, // ObserverPlayer: current working MAX INDIA
    0xC0, // Dictionary: current working MAX INDIA
    0xC0, // DictionaryEntities: current working MAX INDIA
    0xC8, // LDictionaryEntities: current working MAX INDIA
    0xD8, // CameraBase: current working MAX INDIA
    0x20, // Camera: current working MAX INDIA
    0x10, // IntPtrCam: current working MAX INDIA
    0x100, // Matrix: current working MAX INDIA
    0x698, // FollowCamera: current working MAX INDIA
    0x3F0, // MainCameraTransform: current working MAX INDIA
    0x7C, // Player_IsDead: current working MAX INDIA
    0x7C, // Player_isdead: current working MAX INDIA
    0x70, // Player_Data: current working MAX INDIA
    0x70, // GetCurHP: current working MAX INDIA
    0x10, // GetCur: current working MAX INDIA
    0x18, // entryAddr: current working MAX INDIA
    0x10, // enemyList: current working MAX INDIA
    0x20, // HP: current working MAX INDIA
    0x18, // Health: current working MAX INDIA
    0x4A8, // IsClientBot: current working MAX INDIA
    0x4A8, // IsBot: current working MAX INDIA
    0x498, // NamePtr: current working MAX INDIA
    0x498, // Player_Name: current working MAX INDIA
    0x778, // AvatarManager: current working MAX INDIA
    0x138, // UmaAvatarSimple: current working MAX INDIA
    0x138, // Avatar: current working MAX INDIA
    0x101, // IsVisible: current working MAX INDIA
    0x101, // Avatar_IsVisible: current working MAX INDIA
    0x28, // UmaData: current working MAX INDIA
    0x28, // Avatar_Data: current working MAX INDIA
    0x81, // UmaData_IsTeam: current working MAX INDIA
    0x81, // Avatar_Data_IsTeam: current working MAX INDIA
    0x61C, // AimRotation: current working MAX INDIA
    0x80, // Colider_Field: current working MAX INDIA
    0x740, // HedColider: current working MAX INDIA
    0xE58, // aimInfo: current working MAX INDIA
    0x4C, // startPos: current working MAX INDIA
    0x40, // raycast: current working MAX INDIA
    0x850, // isFiring: current working MAX INDIA
    0x850, // sAim1: current working MAX INDIA
    0xE58, // sAim2: current working MAX INDIA
    0x4C, // sAim3: current working MAX INDIA
    0x40, // sAim4: current working MAX INDIA
    0x79C, // CallSetAimRotationCount: current working MAX INDIA
    0x6A8, // Head: current working MAX INDIA
    0x6B8, // Spine: current working MAX INDIA
    0x6B0, // Hip: current working MAX INDIA
    0x6D0, // Root: current working MAX INDIA
    0x728, // lHand: current working MAX INDIA
    0x720, // rHand: current working MAX INDIA
    0x738, // lElbow: current working MAX INDIA
    0x730, // rElbow: current working MAX INDIA
    0x710, // lShoulder: current working MAX INDIA
    0x718, // rShoulder: current working MAX INDIA
    0x6E0, // lAnkle: current working MAX INDIA
    0x6E8, // rAnkle: current working MAX INDIA
    0x6F0, // lToe: current working MAX INDIA
    0x6F8, // rToe: current working MAX INDIA
    0x748, // InventoryManager: current working MAX INDIA
    0x608, // Weapon: current working MAX INDIA
    0x608, // Player_ActiveUISightingWeapon: current working MAX INDIA
    0x17E8, // Player_WeaponOnHand: current working MAX INDIA
    0xA0, // Item: current working MAX INDIA
    0x80, // WeaponComponent: current working MAX INDIA
    0x28, // ItemData: current working MAX INDIA
    0x40, // ItemData_WeaponId: current working MAX INDIA
    0x18, // NoRecoil: current working MAX INDIA
    0x98, // Weapon_Data: current working MAX INDIA
    0x64, // WeaponData_Type: current working MAX INDIA
    0xC8, // WeaponData_AimType: current working MAX INDIA
    0x1, // WeaponType_Sniper: current working MAX INDIA
    0x1D60, // PhyXdata: current working MAX INDIA
    0x20, // maybeDead: current working MAX INDIA
    0x10, // isKnocked: current working MAX INDIA
    0x814, // GhostHack: current working MAX INDIA
    0x770, // PlayerAttributes: current working MAX INDIA
    0x111, // ShootNoReload: current working MAX INDIA
    0x10, // transformValue: current working MAX INDIA
    0x10, // transformObjValue: current working MAX INDIA
    0x40, // indexValue: current working MAX INDIA
    0x38, // matrixValue: current working MAX INDIA
    0x18, // matrixListValue: current working MAX INDIA
    0x20, // matrixIndicesValue: current working MAX INDIA
    0x30, // resultValue: current working MAX INDIA
    0x30, // tMatrixValue: current working MAX INDIA
    0x6D0, // Feet: current working MAX INDIA
    0x0, // CurrentGame: current working MAX INDIA
    0x1AA4, // CurrentAimRotation: current working MAX INDIA
}};

inline constexpr Profile MaxGlobal {"MAX GLOBAL","2.132.1","com.dts.freefiremax",{
    0xAD6DC18, // GameFacade_TypeInfo: MAX-GLOBAL.h: GameFacade_c
    0xAD6DC18, // GameFacade: MAX-GLOBAL.h: GameFacade_c
    0xB8, // Staticfield: MAX-GLOBAL.h: StaticClass
    0xB8, // StaticGameFacade: MAX-GLOBAL.h: StaticClass
    0x80, // BaseGame_HasInited: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x8C, // BaseGame_MatchEnd: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0xCC, // MatchIsRunning: MAX-GLOBAL.h: MatchRun
    0x90, // CurrentMatch: MAX-GLOBAL.h: m_Match
    0xD8, // localPlayer: MAX-GLOBAL.h: localPlayer
    0xD8, // LocalPlayer: MAX-GLOBAL.h: localPlayer
    0x100, // CurrentObserve: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x30, // ObserverPlayer: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0xC0, // Dictionary: MAX-GLOBAL.h: dictionary
    0xC0, // DictionaryEntities: MAX-GLOBAL.h: dictionary
    0xC8, // LDictionaryEntities: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0xD8, // CameraBase: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x30, // Camera: MAX-GLOBAL.h: Camera
    0x10, // IntPtrCam: MAX-GLOBAL.h: intPtrCam
    0x100, // Matrix: MAX-GLOBAL.h: Matrix
    0x698, // FollowCamera: MAX-GLOBAL.h: FollowCamera
    0x3F0, // MainCameraTransform: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x7C, // Player_IsDead: MAX-GLOBAL.h: isDead
    0x7C, // Player_isdead: MAX-GLOBAL.h: isDead
    0x70, // Player_Data: MAX-GLOBAL.h: PlayerData
    0x70, // GetCurHP: MAX-GLOBAL.h: PlayerData
    0x10, // GetCur: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x18, // entryAddr: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x10, // enemyList: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x20, // HP: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x18, // Health: MAX-GLOBAL.h: Health
    0x4A8, // IsClientBot: MAX-GLOBAL.h: isClientBot
    0x4A8, // IsBot: MAX-GLOBAL.h: isClientBot
    0x4A0, // NamePtr: DUMP-VERIFIED member: MAX GLOBAL.cs:1260991 OriginalNickName
    0x4A0, // Player_Name: DUMP-VERIFIED member: MAX GLOBAL.cs:1260991 OriginalNickName
    0x778, // AvatarManager: MAX-GLOBAL.h: avatarManager
    0x138, // UmaAvatarSimple: MAX-GLOBAL.h: umaAvatarSimple
    0x138, // Avatar: MAX-GLOBAL.h: umaAvatarSimple
    0x101, // IsVisible: MAX-GLOBAL.h: isVisible
    0x101, // Avatar_IsVisible: MAX-GLOBAL.h: isVisible
    0x28, // UmaData: MAX-GLOBAL.h: umaData
    0x28, // Avatar_Data: MAX-GLOBAL.h: umaData
    0x81, // UmaData_IsTeam: MAX-GLOBAL.h: isTeam
    0x81, // Avatar_Data_IsTeam: MAX-GLOBAL.h: isTeam
    0x61C, // AimRotation: MAX-GLOBAL.h: AimRotation
    0x80, // Colider_Field: MAX-GLOBAL.h: ColliderBody
    0x740, // HedColider: MAX-GLOBAL.h: ColliderHead
    0xE58, // aimInfo: MAX-GLOBAL.h: AimInfo
    0x4C, // startPos: MAX-GLOBAL.h: StartPos
    0x40, // raycast: MAX-GLOBAL.h: RayDir
    0x850, // isFiring: MAX-GLOBAL.h: isFiring
    0x850, // sAim1: MAX-GLOBAL.h: isFiring
    0xE58, // sAim2: MAX-GLOBAL.h: AimInfo
    0x4C, // sAim3: MAX-GLOBAL.h: StartPos
    0x40, // sAim4: MAX-GLOBAL.h: RayDir
    0x79C, // CallSetAimRotationCount: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x6A8, // Head: MAX-GLOBAL.h: HeadBone
    0x6B8, // Spine: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x6B0, // Hip: MAX-GLOBAL.h: HipBone
    0x6D0, // Root: MAX-GLOBAL.h: RootBone
    0x728, // lHand: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x720, // rHand: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x738, // lElbow: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x730, // rElbow: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x710, // lShoulder: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x718, // rShoulder: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x6E0, // lAnkle: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x6E8, // rAnkle: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x6F0, // lToe: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x6F8, // rToe: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x748, // InventoryManager: MAX-GLOBAL.h: inventoryManager
    0x608, // Weapon: MAX-GLOBAL.h: Weapon
    0x608, // Player_ActiveUISightingWeapon: MAX-GLOBAL.h: Weapon
    0x1800, // Player_WeaponOnHand: DUMP-VERIFIED member: MAX GLOBAL.cs:1261896 FCKONEHNLHP weapon member
    0xA0, // Item: MAX-GLOBAL.h: itemOnHand
    0x80, // WeaponComponent: MAX-GLOBAL.h: WeaponAtri
    0x28, // ItemData: MAX-GLOBAL.h: itemData
    0x40, // ItemData_WeaponId: MAX-GLOBAL.h: shortId
    0x18, // NoRecoil: MAX-GLOBAL.h: NoRecoil
    0x98, // Weapon_Data: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x64, // WeaponData_Type: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0xC8, // WeaponData_AimType: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x1, // WeaponType_Sniper: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x1D60, // PhyXdata: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x20, // maybeDead: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x10, // isKnocked: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x814, // GhostHack: MAX-GLOBAL.h: GhostHack
    0x770, // PlayerAttributes: MAX-GLOBAL.h: Attributes
    0x111, // ShootNoReload: MAX-GLOBAL.h: NoReload
    0x10, // transformValue: MAX-GLOBAL.h: Transform
    0x10, // transformObjValue: MAX-GLOBAL.h: Transform2
    0x40, // indexValue: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x38, // matrixValue: MAX-GLOBAL.h: MatrixPtr
    0x18, // matrixListValue: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x20, // matrixIndicesValue: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x30, // resultValue: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x30, // tMatrixValue: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x6D0, // Feet: MAX-GLOBAL.h: RootBone
    0x0, // CurrentGame: MAX-GLOBAL.h: CurrentGame
    0x1AA4, // CurrentAimRotation: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
}};

inline constexpr Profile Global {"GLOBAL","1.132.1","com.dts.freefireth",{
    0xAC1E768, // GameFacade_TypeInfo: GLOBAL.h: Poiner::GameFacade
    0xAC1E768, // GameFacade: GLOBAL.h: Poiner::GameFacade
    0xB8, // Staticfield: GLOBAL.h: GameFacade::StaticField
    0xB8, // StaticGameFacade: GLOBAL.h: GameFacade::StaticField
    0x80, // BaseGame_HasInited: GLOBAL.h: BaseGame::m_HasInited
    0x8C, // BaseGame_MatchEnd: GLOBAL.h: BaseGame::m_IsMatchEnd
    0xCC, // MatchIsRunning: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x90, // CurrentMatch: GLOBAL.h: MatchGame::m_Match
    0xD8, // localPlayer: GLOBAL.h: EMKJHAJNPDH::PDBGEOANOEP
    0xD8, // LocalPlayer: GLOBAL.h: EMKJHAJNPDH::PDBGEOANOEP
    0x100, // CurrentObserve: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x30, // ObserverPlayer: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0xC0, // Dictionary: GLOBAL.h: MatchGame::m_ReplicationEntitis
    0xC0, // DictionaryEntities: GLOBAL.h: MatchGame::m_ReplicationEntitis
    0xC8, // LDictionaryEntities: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0xD8, // CameraBase: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x30, // Camera: GLOBAL.h: FollowCamera::FCKFGJMEECI
    0x10, // IntPtrCam: GLOBAL.h: Camera::IntPtr
    0x100, // Matrix: GLOBAL.h: IntPtrCam::VM
    0x690, // FollowCamera: GLOBAL.h: Player::FollowCamera
    0x3E8, // MainCameraTransform: GLOBAL.h: Player::MainCameraTF
    0x7C, // Player_IsDead: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x7C, // Player_isdead: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x70, // Player_Data: GLOBAL.h: ReplicationEntity::m_PRIDataPool
    0x70, // GetCurHP: GLOBAL.h: ReplicationEntity::m_PRIDataPool
    0x10, // GetCur: GLOBAL.h: replicationDataPool::m_Datas
    0x18, // entryAddr: GLOBAL.h: Dictionary::ArrPtr
    0x10, // enemyList: GLOBAL.h: Dictionary::Entry_Enemy
    0x20, // HP: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x18, // Health: GLOBAL.h: ReplicationData::Val
    0x4A0, // IsClientBot: GLOBAL.h: Player::IsClientBot
    0x4A0, // IsBot: GLOBAL.h: Player::IsClientBot
    0x498, // NamePtr: GLOBAL.h: Player::OriginalNickName
    0x498, // Player_Name: GLOBAL.h: Player::OriginalNickName
    0x770, // AvatarManager: GLOBAL.h: Player::AvatarManager
    0x138, // UmaAvatarSimple: GLOBAL.h: AvatarManager::IUmaAvatar
    0x138, // Avatar: GLOBAL.h: AvatarManager::IUmaAvatar
    0x101, // IsVisible: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x101, // Avatar_IsVisible: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x28, // UmaData: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x28, // Avatar_Data: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x81, // UmaData_IsTeam: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x81, // Avatar_Data_IsTeam: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x614, // AimRotation: GLOBAL.h: Player::m_AimRotation
    0x80, // Colider_Field: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x738, // HedColider: DUMP-VERIFIED member: GLOBAL.cs:718536 BANPEGJEAKE
    0xE50, // aimInfo: DUMP-VERIFIED member: GLOBAL.cs:718888 FDMIEDDNCEC
    0x4C, // startPos: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x40, // raycast: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x848, // isFiring: GLOBAL.h: Player::IsPrepareAttack
    0x848, // sAim1: GLOBAL.h: Player::IsPrepareAttack
    0xE50, // sAim2: DUMP-VERIFIED member: alias of aimInfo
    0x4C, // sAim3: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x40, // sAim4: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x810, // CallSetAimRotationCount: GLOBAL.h: Player::CallSetAimRotationCount
    0x6A0, // Head: GLOBAL.h: Player::HeadTF
    0x6B0, // Spine: GLOBAL.h: Bones::Chest
    0x6B0, // Hip: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x6D0, // Root: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x698, // lHand: GLOBAL.h: Bones::L_Hand
    0x6F8, // rHand: GLOBAL.h: Bones::R_Hand
    0x718, // lElbow: GLOBAL.h: Bones::L_Elbow
    0x730, // rElbow: GLOBAL.h: Bones::R_Elbow
    0x710, // lShoulder: GLOBAL.h: Bones::L_Shoulder
    0x708, // rShoulder: GLOBAL.h: Bones::R_Shoulder
    0x6F0, // lAnkle: GLOBAL.h: Bones::L_Ankle
    0x6E8, // rAnkle: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x6F0, // lToe: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x6F8, // rToe: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x740, // InventoryManager: GLOBAL.h: Player::InventoryManager
    0x600, // Weapon: GLOBAL.h: Player::ActiveUISightingWeapon
    0x600, // Player_ActiveUISightingWeapon: GLOBAL.h: Player::ActiveUISightingWeapon
    0x17E8, // Player_WeaponOnHand: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0xA0, // Item: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x80, // WeaponComponent: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x28, // ItemData: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x40, // ItemData_WeaponId: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x18, // NoRecoil: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x98, // Weapon_Data: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x64, // WeaponData_Type: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0xC8, // WeaponData_AimType: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x1, // WeaponType_Sniper: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x1D48, // PhyXdata: DUMP-VERIFIED member: GLOBAL.cs:719555 GBJCJJNPPEC
    0x20, // maybeDead: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x10, // isKnocked: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x814, // GhostHack: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x768, // PlayerAttributes: DUMP-VERIFIED member: GLOBAL.cs:718542 IODLOCEJIOK
    0x111, // ShootNoReload: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x10, // transformValue: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x10, // transformObjValue: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x40, // indexValue: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x38, // matrixValue: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x18, // matrixListValue: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x20, // matrixIndicesValue: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x30, // resultValue: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x30, // tMatrixValue: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x6E8, // Feet: GLOBAL.h: Player::FeetTF
    0x8, // CurrentGame: GLOBAL.h: GameFacade::CurrentMatchGame
    0x1A8C, // CurrentAimRotation: DUMP-VERIFIED member: GLOBAL.cs:719437 m_CurrentAimRotation
}};

inline constexpr Profile India {"INDIA","1.134.1","com.garena.game.ffi",{
    0xAC19D88, // GameFacade_TypeInfo: INDIA.h: GameFacade
    0xAC19D88, // GameFacade: INDIA.h: GameFacade
    0xB8, // Staticfield: INDIA.h: GameFacade_P2
    0xB8, // StaticGameFacade: INDIA.h: GameFacade_P2
    0x80, // BaseGame_HasInited: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x8C, // BaseGame_MatchEnd: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0xCC, // MatchIsRunning: INDIA.h: MatchIsRunning
    0x90, // CurrentMatch: INDIA.h: BaseGame_Match
    0xD8, // localPlayer: INDIA.h: Match_LocalPlayer
    0xD8, // LocalPlayer: INDIA.h: Match_LocalPlayer
    0x100, // CurrentObserve: INDIA.h: CurrentObserve
    0x30, // ObserverPlayer: INDIA.h: ObserverPlayer
    0xC0, // Dictionary: INDIA.h: Match_PlayerDict
    0xC0, // DictionaryEntities: INDIA.h: Match_PlayerDict
    0xC8, // LDictionaryEntities: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0xD8, // CameraBase: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x30, // Camera: INDIA.h: Camera_Follow
    0x10, // IntPtrCam: INDIA.h: Camera_IntPtr
    0x100, // Matrix: INDIA.h: Camera_Matrix
    0x690, // FollowCamera: INDIA.h: Player_Camera
    0x3E8, // MainCameraTransform: DUMP-VERIFIED member: INDIA.cs:654576 MainCameraTransform
    0x7C, // Player_IsDead: INDIA.h: Player_IsDead
    0x7C, // Player_isdead: INDIA.h: Player_IsDead
    0x70, // Player_Data: INDIA.h: Player_HP
    0x70, // GetCurHP: INDIA.h: Player_HP
    0x10, // GetCur: INDIA.h: HP_List
    0x18, // entryAddr: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x10, // enemyList: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x20, // HP: INDIA.h: HP_Entry
    0x18, // Health: INDIA.h: HP_Value
    0x4A0, // IsClientBot: DUMP-VERIFIED member: INDIA.cs:654599 IsClientBot
    0x4A0, // IsBot: DUMP-VERIFIED member: INDIA.cs:654599 IsClientBot
    0x498, // NamePtr: DUMP-VERIFIED member: INDIA.cs:654598 OriginalNickName
    0x498, // Player_Name: DUMP-VERIFIED member: INDIA.cs:654598 OriginalNickName
    0x770, // AvatarManager: INDIA.h: Player_Avatar
    0x138, // UmaAvatarSimple: INDIA.h: Avatar_Uma
    0x138, // Avatar: INDIA.h: Avatar_Uma
    0x101, // IsVisible: INDIA.h: Uma_Visible
    0x101, // Avatar_IsVisible: INDIA.h: Uma_Visible
    0x28, // UmaData: INDIA.h: Uma_Data
    0x28, // Avatar_Data: INDIA.h: Uma_Data
    0x81, // UmaData_IsTeam: INDIA.h: UmaData_Skip
    0x81, // Avatar_Data_IsTeam: INDIA.h: UmaData_Skip
    0x614, // AimRotation: INDIA.h: Player_Rotation
    0x80, // Colider_Field: INDIA.h: Player_ColWrite
    0x738, // HedColider: INDIA.h: Player_HedColider
    0xE50, // aimInfo: INDIA.h: Player_BulletInfo
    0x4C, // startPos: INDIA.h: Bullet_SpawnPos
    0x40, // raycast: INDIA.h: Bullet_DirWrite
    0x848, // isFiring: INDIA.h: Player_IsFiring
    0x848, // sAim1: INDIA.h: Player_IsFiring
    0xE50, // sAim2: INDIA.h: Player_BulletInfo
    0x4C, // sAim3: INDIA.h: Bullet_SpawnPos
    0x40, // sAim4: INDIA.h: Bullet_DirWrite
    0x810, // CallSetAimRotationCount: INDIA.h: CallSetAimRotationCount
    0x6A0, // Head: INDIA.h: Bones_Head
    0x6B0, // Spine: DUMP-VERIFIED member: INDIA.cs:654703 FFFPCADFFGA
    0x6A8, // Hip: INDIA.h: Bones_Hip
    0x6A8, // Root: INDIA.h: Bones_Root
    0x720, // lHand: INDIA.h: Bones_LeftHand
    0x718, // rHand: INDIA.h: Bones_RightHand
    0x728, // lElbow: INDIA.h: Bones_LeftForeArm
    0x720, // rElbow: INDIA.h: Bones_RightForeArm
    0x708, // lShoulder: INDIA.h: Bones_LeftShoulder
    0x710, // rShoulder: INDIA.h: Bones_RightShoulder
    0x6D8, // lAnkle: INDIA.h: Bones_LeftAnkle
    0x6E0, // rAnkle: INDIA.h: Bones_RightAnkle
    0x6D8, // lToe: INDIA.h: Bones_LeftToe
    0x6E0, // rToe: INDIA.h: Bones_RightToe
    0x740, // InventoryManager: INDIA.h: InventoryManager
    0x600, // Weapon: INDIA.h: WeaponInstance
    0x600, // Player_ActiveUISightingWeapon: INDIA.h: WeaponInstance
    0x17E8, // Player_WeaponOnHand: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0xA0, // Item: INDIA.h: Inventory_ActiveItem
    0x80, // WeaponComponent: INDIA.h: Inventory_WeaponComponent
    0x28, // ItemData: INDIA.h: Inventory_ItemData
    0x40, // ItemData_WeaponId: INDIA.h: Inventory_WeaponId
    0x18, // NoRecoil: INDIA.h: Inventory_NoRecoil
    0x88, // Weapon_Data: INDIA.h: WeaponData
    0x64, // WeaponData_Type: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0xC8, // WeaponData_AimType: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x1, // WeaponType_Sniper: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x1D48, // PhyXdata: DUMP-VERIFIED member: INDIA.cs:655739 GBJCJJNPPEC
    0x20, // maybeDead: INDIA.h: Status_StatusData
    0x10, // isKnocked: INDIA.h: Status_StateID
    0x814, // GhostHack: INDIA.h: GhostHack
    0x768, // PlayerAttributes: INDIA.h: PlayerAttributes
    0x111, // ShootNoReload: INDIA.h: ShootNoReload
    0x10, // transformValue: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x10, // transformObjValue: INDIA.h: Transform_TransformToObj
    0x40, // indexValue: INDIA.h: Transform_ObjToIndex
    0x38, // matrixValue: INDIA.h: Transform_ObjToMatrix
    0x18, // matrixListValue: INDIA.h: Transform_MatrixList
    0x20, // matrixIndicesValue: INDIA.h: Transform_MatrixIndices
    0x30, // resultValue: INDIA.h: Transform_MatrixSize
    0x30, // tMatrixValue: INDIA.h: Transform_MatrixSize
    0x6C8, // Feet: INDIA.h: Bones_Feet
    0x0, // CurrentGame: NOT VERIFIED: inherited MAX INDIA fallback (user authorized)
    0x1A8C, // CurrentAimRotation: DUMP-VERIFIED member: INDIA.cs:655621 m_CurrentAimRotation
}};

inline constexpr const Profile* choices[]={&MaxIndia,&MaxGlobal,&Global,&India};
inline const Profile* selected=&MaxIndia;
inline const Profile* Get(int choice){return choice>=1&&choice<=4?choices[choice-1]:nullptr;}
inline int ParseChoice(const char* text){
    if(!text)return 0;
    while(*text==' '||*text=='\t'||*text=='\r'||*text=='\n')++text;
    if(*text<'1'||*text>'4')return 0;
    int choice=*text++-'0';
    while(*text==' '||*text=='\t'||*text=='\r'||*text=='\n')++text;
    return *text?0:choice;
}
inline bool MatchesCommandLine(const char* command,std::size_t bytes){
    if(!command)return false;
    const auto length=std::strlen(selected->package);
    return bytes>length&&std::memcmp(command,selected->package,length)==0&&command[length]=='\0';
}
inline bool Apply(int choice){
    const auto* profile=Get(choice);
    if(!profile||Offsets::IsNullified())return false;
    const auto& values=profile->offsets;


    Offsets::GameFacade_TypeInfo=values.GameFacade_TypeInfo;
    Offsets::GameFacade=values.GameFacade;
    Offsets::Staticfield=values.Staticfield;
    Offsets::StaticGameFacade=values.StaticGameFacade;
    Offsets::BaseGame_HasInited=values.BaseGame_HasInited;
    Offsets::BaseGame_MatchEnd=values.BaseGame_MatchEnd;
    Offsets::MatchIsRunning=values.MatchIsRunning;
    Offsets::CurrentMatch=values.CurrentMatch;
    Offsets::localPlayer=values.localPlayer;
    Offsets::LocalPlayer=values.LocalPlayer;
    Offsets::CurrentObserve=values.CurrentObserve;
    Offsets::ObserverPlayer=values.ObserverPlayer;
    Offsets::Dictionary=values.Dictionary;
    Offsets::DictionaryEntities=values.DictionaryEntities;
    Offsets::LDictionaryEntities=values.LDictionaryEntities;
    Offsets::CameraBase=values.CameraBase;
    Offsets::Camera=values.Camera;
    Offsets::IntPtrCam=values.IntPtrCam;
    Offsets::Matrix=values.Matrix;
    Offsets::FollowCamera=values.FollowCamera;
    Offsets::MainCameraTransform=values.MainCameraTransform;
    Offsets::Player_IsDead=values.Player_IsDead;
    Offsets::Player_isdead=values.Player_isdead;
    Offsets::Player_Data=values.Player_Data;
    Offsets::GetCurHP=values.GetCurHP;
    Offsets::GetCur=values.GetCur;
    Offsets::entryAddr=values.entryAddr;
    Offsets::enemyList=values.enemyList;
    Offsets::HP=values.HP;
    Offsets::Health=values.Health;
    Offsets::IsClientBot=values.IsClientBot;
    Offsets::IsBot=values.IsBot;
    Offsets::NamePtr=values.NamePtr;
    Offsets::Player_Name=values.Player_Name;
    Offsets::AvatarManager=values.AvatarManager;
    Offsets::UmaAvatarSimple=values.UmaAvatarSimple;
    Offsets::Avatar=values.Avatar;
    Offsets::IsVisible=values.IsVisible;
    Offsets::Avatar_IsVisible=values.Avatar_IsVisible;
    Offsets::UmaData=values.UmaData;
    Offsets::Avatar_Data=values.Avatar_Data;
    Offsets::UmaData_IsTeam=values.UmaData_IsTeam;
    Offsets::Avatar_Data_IsTeam=values.Avatar_Data_IsTeam;
    Offsets::AimRotation=values.AimRotation;
    Offsets::Colider_Field=values.Colider_Field;
    Offsets::HedColider=values.HedColider;
    Offsets::aimInfo=values.aimInfo;
    Offsets::startPos=values.startPos;
    Offsets::raycast=values.raycast;
    Offsets::isFiring=values.isFiring;
    Offsets::sAim1=values.sAim1;
    Offsets::sAim2=values.sAim2;
    Offsets::sAim3=values.sAim3;
    Offsets::sAim4=values.sAim4;
    Offsets::CallSetAimRotationCount=values.CallSetAimRotationCount;
    Offsets::Head=values.Head;
    Offsets::Spine=values.Spine;
    Offsets::Hip=values.Hip;
    Offsets::Root=values.Root;
    Offsets::lHand=values.lHand;
    Offsets::rHand=values.rHand;
    Offsets::lElbow=values.lElbow;
    Offsets::rElbow=values.rElbow;
    Offsets::lShoulder=values.lShoulder;
    Offsets::rShoulder=values.rShoulder;
    Offsets::lAnkle=values.lAnkle;
    Offsets::rAnkle=values.rAnkle;
    Offsets::lToe=values.lToe;
    Offsets::rToe=values.rToe;
    Offsets::InventoryManager=values.InventoryManager;
    Offsets::Weapon=values.Weapon;
    Offsets::Player_ActiveUISightingWeapon=values.Player_ActiveUISightingWeapon;
    Offsets::Player_WeaponOnHand=values.Player_WeaponOnHand;
    Offsets::Item=values.Item;
    Offsets::WeaponComponent=values.WeaponComponent;
    Offsets::ItemData=values.ItemData;
    Offsets::ItemData_WeaponId=values.ItemData_WeaponId;
    Offsets::NoRecoil=values.NoRecoil;
    Offsets::Weapon_Data=values.Weapon_Data;
    Offsets::WeaponData_Type=values.WeaponData_Type;
    Offsets::WeaponData_AimType=values.WeaponData_AimType;
    Offsets::WeaponType_Sniper=static_cast<int>(values.WeaponType_Sniper);
    Offsets::PhyXdata=values.PhyXdata;
    Offsets::maybeDead=values.maybeDead;
    Offsets::isKnocked=values.isKnocked;
    Offsets::GhostHack=values.GhostHack;
    Offsets::PlayerAttributes=values.PlayerAttributes;
    Offsets::ShootNoReload=values.ShootNoReload;
    Offsets::transformValue=values.transformValue;
    Offsets::transformObjValue=values.transformObjValue;
    Offsets::indexValue=values.indexValue;
    Offsets::matrixValue=values.matrixValue;
    Offsets::matrixListValue=values.matrixListValue;
    Offsets::matrixIndicesValue=values.matrixIndicesValue;
    Offsets::resultValue=values.resultValue;
    Offsets::tMatrixValue=values.tMatrixValue;
    Offsets::Feet=values.Feet;
    Offsets::CurrentGame=values.CurrentGame;
    Offsets::CurrentAimRotation=values.CurrentAimRotation;
    bone::HedColider=values.HedColider;
    bone::Head=values.Head;
    bone::Spine=values.Spine;
    bone::Hip=values.Hip;
    bone::Root=values.Root;
    bone::lHand=values.lHand;
    bone::rHand=values.rHand;
    bone::lElbow=values.lElbow;
    bone::rElbow=values.rElbow;
    bone::lShoulder=values.lShoulder;
    bone::rShoulder=values.rShoulder;
    bone::lAnkle=values.lAnkle;
    bone::rAnkle=values.rAnkle;
    bone::lToe=values.lToe;
    bone::rToe=values.rToe;
    selected=profile;
    return true;
}
}
