#pragma once

#include <math.h>
#include <stdint.h>

class Vvector3 {
public:
float X;
float Y;
float Z;
Vvector3() : X(0), Y(0), Z(0) {}
Vvector3(float x1, float y1, float z1) : X(x1), Y(y1), Z(z1) {}
Vvector3(const Vvector3 &v);
~Vvector3();
};

Vvector3::Vvector3(const Vvector3 &v) : X(v.X), Y(v.Y), Z(v.Z) {}
Vvector3::~Vvector3() {}

#define Class_Camera__get_main (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Camera"), OBFUSCATE("get_main"))
void* get_main() {
    return reinterpret_cast<void* (__fastcall*)()>(Class_Camera__get_main)();
}

#define Class_Input__get_touchCount (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Input"), OBFUSCATE("get_touchCount"))

#define Class_Input__GetTouch (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Input"), OBFUSCATE("GetTouch"), 1)

#define Class_Input__get_mousePosition (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Input"), OBFUSCATE("get_mousePosition"))

#define Class_Screen__get_width (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Screen"), OBFUSCATE("get_width"))
int get_width() {
    return reinterpret_cast<int(__fastcall*)()>(Class_Screen__get_width)();
}

#define Class_Screen__get_height (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Screen"), OBFUSCATE("get_height"))
int get_height() {
    return reinterpret_cast<int(__fastcall*)()>(Class_Screen__get_height)();
}

#define Class_Screen__get_density (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Screen"), OBFUSCATE("get_dpi"))

#define Camera_get_fieldOfView (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Camera"), OBFUSCATE("get_fieldOfView"))
float get_fieldOfView() {
    return reinterpret_cast<float(__fastcall*)(void*)>(Camera_get_fieldOfView)(get_main());
}

#define Camera_set_fieldOfView (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Camera"), OBFUSCATE("set_fieldOfView"), 1)
void* set_fieldOfView(float value) {
    return reinterpret_cast<void* (__fastcall*)(void*, float)>(Camera_set_fieldOfView)(get_main(), value);
}

#define ForWard (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Transform"), OBFUSCATE("get_forward"), 0)
static Vector3 GetForward(void* player) {
    Vector3(*_GetForward)(void* players) = (Vector3(*)(void*))(ForWard);
    return _GetForward(player);
}

#define Class_Transform__GetPosition Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Transform"), OBFUSCATE("get_position_Injected"), 1)
static Vector3 Transform_GetPosition(void* player) {
    Vector3 out = Vector3::zero();
    void (*_Transform_GetPosition)(void* transform, Vector3 * out) = (void (*)(void*, Vector3*))(Class_Transform__GetPosition);
    _Transform_GetPosition(player, &out);
    return out;
}

#define Class_Transform__SetPosition Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Transform"), OBFUSCATE("set_position_Injected"), 1)
static void Transform_INTERNAL_SetPosition(void* player, Vvector3 inn) {
    void (*Transform_INTERNAL_SetPosition)(void* transform, Vvector3 in) = (void (*)(void*, Vvector3))(Class_Transform__SetPosition);
    Transform_INTERNAL_SetPosition(player, inn);
}

#define Class_Transform__Position Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Transform"), OBFUSCATE("get_position"), 0)
Vector3 get_position(void* player) {
    Vector3(*_get_position)(void* players) = (Vector3(*)(void*))(Class_Transform__Position);
    return _get_position(player);
}

#define Class_Transform__Rotation Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Transform"), OBFUSCATE("get_rotation"), 0)
static Quaternion GetRotation(void* player) {
Quaternion (*_GetRotation)(void* players) = (Quaternion(*)(void *))(Class_Transform__Rotation);
return _GetRotation(player);
}

#define Class_Camera__WorldToScreenPoint (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Camera"), OBFUSCATE("WorldToScreenPoint"), 1)
static Vector3 WorldToScreenPoint(void* WorldCam, Vector3 WorldPos) {
    Vector3(*_WorldToScreenScene)(void* Camera, Vector3 position) = (Vector3(*)(void*, Vector3)) (Class_Camera__WorldToScreenPoint);
    return _WorldToScreenScene(WorldCam, WorldPos);
}

#define ListPlayer (uintptr_t) Il2CppGetFieldOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW.GamePlay"), OBFUSCATE("Match"), OBFUSCATE("m_Players"))

#define EnemyUpdate (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW.GamePlay"), OBFUSCATE("Player"), OBFUSCATE("LateUpdate"), 0)

#define MainCam (uintptr_t) Il2CppGetFieldOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW.GamePlay"), OBFUSCATE("Player"), OBFUSCATE("MainCameraTransform"))
static Vector3 CameraMain(void* player) {
    return get_position(*(void**)((uint64_t)player + MainCam));
}

#define Match (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW"), OBFUSCATE("GameFacade"), OBFUSCATE("CurrentMatch"), 0)
static void* Curent_Match() {
    void* (*_Curent_Match) (void* nuls) = (void* (*)(void*))(Match);
    return _Curent_Match(NULL);
}

#define Local (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW"), OBFUSCATE("UIHudDetectorController"), OBFUSCATE("GetLocalPlayer"), 0)
static void* GetLocalPlayer(void* Game) {
    void* (*_GetLocalPlayer)(void* match) = (void* (*)(void*))(Local);
    return _GetLocalPlayer(Game);
}

#define Visible (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW.GamePlay"), OBFUSCATE("Player"), OBFUSCATE("IsVisible"), 0)
static bool get_isVisible(void* player) {
    bool (*_get_isVisible)(void* players) = (bool (*)(void*))(Visible);
    return _get_isVisible(player);
}

#define Team (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW.GamePlay"), OBFUSCATE("Player"), OBFUSCATE("IsLocalTeammate"), 1)
static bool get_isLocalTeam(void* player) {
    using fnGetIsLocalTeam = bool(*)(void*, bool);
    auto _get_isLocalTeam = reinterpret_cast<fnGetIsLocalTeam>(Team);
    return _get_isLocalTeam(player, false);
}

#define Die (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW.GamePlay"), OBFUSCATE("Player"), OBFUSCATE("get_IsDieing"), 0)
static bool get_IsDieing(void* player) {
    bool (*_get_die)(void* players) = (bool (*)(void*))(Die);
    return _get_die(player);
}

#define CurHP (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW.GamePlay"), OBFUSCATE("Player"), OBFUSCATE("get_CurHP"), 0)
static int GetHp(void* player) {
    int (*_GetHp)(void* players) = (int(*)(void*))(CurHP);
    return _GetHp(player);
}

#define MaxHP (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW.GamePlay"), OBFUSCATE("Player"), OBFUSCATE("get_MaxHP"), 0)
static int get_MaxHP(void* enemy) {
    int (*_get_MaxHP)(void* player) = (int(*)(void*))(MaxHP);
    return _get_MaxHP(enemy);
}

#define Name (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW.GamePlay"), OBFUSCATE("Player"), OBFUSCATE("get_NickName"), 0)
static monoString* get_NickName(void* player) {
    monoString* (*_get_NickName)(void* players) = (monoString * (*)(void*))(Name);
    return _get_NickName(player);
}

#define Aim (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW.GamePlay"), OBFUSCATE("Player"), OBFUSCATE("SetAimRotation"), 2) //0
static void set_aim(void* player, Quaternion look) {
    using fnSetAim = void(*)(void*, Quaternion, bool);
    auto _set_aim = reinterpret_cast<fnSetAim>(Aim);

    _set_aim(player, look, false);
}

#define Scope (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW.GamePlay"), OBFUSCATE("Player"), OBFUSCATE("get_IsSighting"),0 )
static bool get_IsSighting(void* player) {
    bool (*_get_IsSighting)(void* players) = (bool (*)(void*))(Scope);
    return _get_IsSighting(player);
}

#define Fire (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW.GamePlay"), OBFUSCATE("Player"), OBFUSCATE("IsFiring"), 0)
static bool get_IsFiring(void* player) {
    bool (*_get_IsFiring)(void* players) = (bool (*)(void*))(Fire);
    return _get_IsFiring(player);
}

#define Head (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW.GamePlay"), OBFUSCATE("Player"), OBFUSCATE("GetHeadTF"), 0)
static void* GetHeadPositions(void* player) {
    void* (*_GetHeadPositions)(void* players) = (void* (*)(void*))(Head);
    return _GetHeadPositions(player);
}

#define CharGet (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("mscorlib.dll"), OBFUSCATE("System"), OBFUSCATE("String"), OBFUSCATE("get_Chars"), 1) //0
char get_Chars(monoString* str, int index) {
    char (*_get_Chars)(monoString * str, int index) = (char (*)(monoString*, int))(CharGet);
    return _get_Chars(str, index);
}

// [UPDATED] COW.GamePlay.PhysicsUtil.SingleLineCheck no longer exists in the
// current build (confirmed absent from the current il2cpp dump — the whole
// PhysicsUtil class is gone, not just this method; not guessing a same-named
// replacement). Re-implemented on UnityEngine.Physics.Raycast(origin,
// direction, out RaycastHit, maxDistance, layerMask, QueryTriggerInteraction)
// instead, confirmed present in the current dump (UnityEngine.PhysicsModule.dll,
// namespace UnityEngine, class Physics). This is a generic engine API, not a
// game-specific wrapper, so the exact trigger-collider/occlusion behavior
// PhysicsUtil.SingleLineCheck used to have is NOT guaranteed identical — only
// the boolean "did anything block the line" result is used by IsVisible()
// below, which is what the old call site actually consumed.
//
// QueryTriggerInteraction.Ignore (1) is used below as a standard choice for a
// visibility/occlusion line check (ignore trigger volumes, same as most
// games' wallhack-check convention) — this is an engine-level default I'm
// picking, not something recovered from the removed function; flag it if
// visibility ends up wrong after testing in-game and I'll switch to
// Collide (2) or UseGlobal (0).
#define RaycastWithHit (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Physics"), OBFUSCATE("Raycast"), 6)

// RaycastHit layout confirmed from the current il2cpp dump:
//   0x00 Vector3 point      (12 bytes)
//   0x0C Vector3 normal     (12 bytes)
//   0x18 uint    faceID     (4 bytes)
//   0x1C float   distance   (4 bytes)
//   0x20 Vector2 uv         (8 bytes)
//   0x28 int     colliderID (4 bytes, internal — resolved via get_collider(),
//                            not a raw pointer; not needed here)
// Buffer sized generously past the confirmed 0x2C end to absorb any struct
// padding/alignment the compiler adds — never read from it, only need a
// valid out-param destination for the ABI.
struct RaycastHitBuf { uint8_t _raw[64]; };

static bool Physics_Raycast(Vector3 camLocation, Vector3 headLocation, unsigned int LayerID, void* /*collider — unused by any caller, kept for ABI compat*/) {
    Vector3 delta = Vector3(headLocation.x - camLocation.x,
                             headLocation.y - camLocation.y,
                             headLocation.z - camLocation.z);
    float maxDistance = sqrtf(delta.x*delta.x + delta.y*delta.y + delta.z*delta.z);
    if (maxDistance <= 0.0001f) return false; // degenerate — nothing to check
    Vector3 direction(delta.x / maxDistance, delta.y / maxDistance, delta.z / maxDistance);

    RaycastHitBuf hitBuf{};
    int queryTriggerInteraction = 1; // QueryTriggerInteraction.Ignore — see note above

    bool (*_Raycast)(Vector3 origin, Vector3 direction, RaycastHitBuf* hitInfo,
                      float maxDistance, unsigned int layerMask, int queryTriggerInteraction) =
        (bool (*)(Vector3, Vector3, RaycastHitBuf*, float, unsigned int, int))(RaycastWithHit);

    return _Raycast(camLocation, direction, &hitBuf, maxDistance, LayerID, queryTriggerInteraction);
}

#define HeadColider (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW.GamePlay"), OBFUSCATE("Player"), OBFUSCATE("get_HeadCollider"), 0)
static void* Player_GetHeadCollider(void* player) {
    void* (*_Player_GetHeadCollider)(void* players) = (void* (*)(void*))(HeadColider);
    return _Player_GetHeadCollider(player);
}

#define Hip (uintptr_t) Il2CppGetMethodOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW.GamePlay"), OBFUSCATE("Player"), OBFUSCATE("GetHipTF"), 0)
static void* GetHipPositions(void* player) {
void* (*_GetHipPositions)(void* players) = (void*(*)(void*))(Hip);
return _GetHipPositions(player);
}

#define Class_Compent__Transform Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Component"), OBFUSCATE("get_transform"), 0)
static void *Component_GetTransform(void *player) {
void *(*_Component_GetTransform)(void *component) = (void *(*)(void *))(Class_Compent__Transform);
return _Component_GetTransform(player);
}


static void *Camera_main() {
void *(*_Camera_main)(void *nuls) = (void *(*)(void *))(Class_Camera__get_main);
return _Camera_main(nullptr);
}

Vector3 getPosition(void *transform) {
return get_position(Component_GetTransform(transform));
}
static Vector3 GetHeadPosition(void* player) {
return get_position(GetHeadPositions(player));
}
static Vector3 GetHipPosition(void* player) {
return get_position(GetHipPositions(player));
}

float get_density() {
return reinterpret_cast<float(__fastcall *)()>(Class_Screen__get_density)();
}




