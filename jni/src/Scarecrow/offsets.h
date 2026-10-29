#pragma once
/* ============================================================
   offsets.h — SERVER-DRIVEN runtime offsets
   Values are 0 until valid login applies secret JSON.
   API keeps PlayerOffsets::Name style as inline getters.
   ============================================================ */
#include <cstdint>
#include "ix_rt_offsets.h"

namespace PlayerOffsets {

inline uintptr_t IsDead()          { return ix_rt().P.IsDead; }
inline uintptr_t AimCollider()     { return ix_rt().P.AimCollider; }
inline uintptr_t ColliderNode()    { return ix_rt().P.ColliderNode; }
inline uintptr_t HealthLink1()     { return ix_rt().P.HealthLink1; }
inline uintptr_t HealthLink2()     { return ix_rt().P.HealthLink2; }
inline uintptr_t HealthLink3()     { return ix_rt().P.HealthLink3; }
inline uintptr_t Health()          { return ix_rt().P.Health; }
inline uintptr_t NickName()        { return ix_rt().P.NickName; }
inline uintptr_t IsClientBot()     { return ix_rt().P.IsClientBot; }
inline uintptr_t MainCamera()      { return ix_rt().P.MainCamera; }
inline uintptr_t FollowCamera()    { return ix_rt().P.FollowCamera; }
inline uintptr_t Rotation()        { return ix_rt().P.Rotation; }
inline uintptr_t IsFiringSilent()  { return ix_rt().P.IsFiringSilent; }
inline uintptr_t IsFiring()        { return ix_rt().P.IsFiring; }
inline uintptr_t HitObjInfo()      { return ix_rt().P.HitObjInfo; }
inline uintptr_t AmmoBase()        { return ix_rt().P.AmmoBase; }
inline uintptr_t SilentAimDir()    { return ix_rt().P.SilentAimDir; }
inline uintptr_t AvatarManager()   { return ix_rt().P.AvatarManager; }
inline uintptr_t Status()          { return ix_rt().P.Status; }
inline uintptr_t StatusSub()       { return ix_rt().P.StatusSub; }
inline uintptr_t StatusState()     { return ix_rt().P.StatusState; }
inline uintptr_t AvatarSimple()    { return ix_rt().P.AvatarSimple; }
inline uintptr_t IsVisible()       { return ix_rt().P.IsVisible; }
inline uintptr_t UmaData()         { return ix_rt().P.UmaData; }
inline uintptr_t UmaDataDead()     { return ix_rt().P.UmaDataDead; }
inline uintptr_t GameFacade()      { return ix_rt().P.GameFacade; }
inline uintptr_t FacadeGame()      { return ix_rt().P.FacadeGame; }
inline uintptr_t FacadeBase()      { return ix_rt().P.FacadeBase; }
inline uintptr_t BaseGame()        { return ix_rt().P.BaseGame; }
inline uintptr_t MatchRunning()    { return ix_rt().P.MatchRunning; }
inline uintptr_t MatchLocal()      { return ix_rt().P.MatchLocal; }
inline uintptr_t Dictionary()      { return ix_rt().P.Dictionary; }
inline uintptr_t DictValues()      { return ix_rt().P.DictValues; }
inline uintptr_t EntityStride()    { return ix_rt().P.EntityStride; }
inline int       MaxEntities()     { return ix_rt().P.MaxEntities > 0 ? ix_rt().P.MaxEntities : 0; }
inline uintptr_t CameraStep1()     { return ix_rt().P.CameraStep1; }
inline uintptr_t CameraStep2()     { return ix_rt().P.CameraStep2; }
inline uintptr_t ViewMatrix()      { return ix_rt().P.ViewMatrix; }
inline uintptr_t GhostHack()       { return ix_rt().P.GhostHack; }
inline uintptr_t WeaponInstance()  { return ix_rt().P.WeaponInstance; }
inline uintptr_t WeaponRecoilValue(){ return ix_rt().P.WeaponRecoilValue; }
inline uintptr_t InventoryManager(){ return ix_rt().P.InventoryManager; }
inline uintptr_t InventoryActiveItem(){ return ix_rt().P.InventoryActiveItem; }
inline uintptr_t InventoryWeaponComp(){ return ix_rt().P.InventoryWeaponComp; }
inline uintptr_t InventoryNoRecoil(){ return ix_rt().P.InventoryNoRecoil; }
inline uintptr_t PlayerAttributes(){ return ix_rt().P.PlayerAttributes; }
inline uintptr_t ShootNoReload()   { return ix_rt().P.ShootNoReload; }

namespace Transform {
    inline uintptr_t Object()        { return ix_rt().P.TfObject; }
    inline uintptr_t Index()         { return ix_rt().P.TfIndex; }
    inline uintptr_t Matrix()        { return ix_rt().P.TfMatrix; }
    inline uintptr_t MatrixList()    { return ix_rt().P.TfMatrixList; }
    inline uintptr_t MatrixIndices() { return ix_rt().P.TfMatrixIndices; }
    inline uintptr_t Stride()        { return ix_rt().P.TfStride; }
    inline int       MaxTries()      { return ix_rt().P.TfMaxTries > 0 ? ix_rt().P.TfMaxTries : 0; }
}

} // namespace PlayerOffsets

namespace BoneOffsets {
inline uintptr_t Head()          { return ix_rt().B.Head; }
inline uintptr_t Hip()           { return ix_rt().B.Hip; }
inline uintptr_t Chest()         { return ix_rt().B.Chest; }
inline uintptr_t Root()          { return ix_rt().B.Root; }
inline uintptr_t LeftShoulder()  { return ix_rt().B.LeftShoulder; }
inline uintptr_t RightShoulder() { return ix_rt().B.RightShoulder; }
inline uintptr_t LeftAnkle()     { return ix_rt().B.LeftAnkle; }
inline uintptr_t RightAnkle()    { return ix_rt().B.RightAnkle; }
inline uintptr_t LeftToe()       { return ix_rt().B.LeftToe; }
inline uintptr_t RightToe()      { return ix_rt().B.RightToe; }
inline uintptr_t RightForearm()  { return ix_rt().B.RightForearm; }
inline uintptr_t LeftForearm()   { return ix_rt().B.LeftForearm; }
inline uintptr_t RightHand()     { return ix_rt().B.RightHand; }
inline uintptr_t LeftHand()      { return ix_rt().B.LeftHand; }
} // namespace BoneOffsets
