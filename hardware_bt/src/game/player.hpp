#pragma once
#include "../other/vector3.h"
#include <cstdint>
#include <chrono>

// hook.h's own bone enum, ported verbatim (same order/names), so the
// skeleton draw code can be an exact port of hook.h's Ln(a,b) chain.
enum { BONE_HEAD = 0, BONE_CHEST, BONE_HIP, BONE_LSH, BONE_RSH,
       BONE_LHAND, BONE_RHAND, BONE_LFOOT, BONE_RFOOT, BONE_COUNT };

// ESP PlayerData — screen coords + gameplay metadata.
struct PlayerData {
    uint64_t addr;
    Vector3  Head;        // topo do box (screen)
    Vector3  Toe;         // base do box (screen)
    Vector3  headWorld;   // world pos of head (bone::Head)
    Vector3  spineWorld;  // world pos of spine (bone::Spine)
    Vector3  neckWorld;   // world pos of neck (interpolated)
    bool     isKnocked;
    bool     isVisible; // Existing avatar flag; not line of sight.
    bool     isBot;
    float    distance;
    int      curHP;
    int      maxHP;
    char     name[36];    // player display name (UTF-8, truncated)
    int      weaponType;  // -1 unknown, 0..N weapon-type enum

    // Full skeleton for hook.h's 9-bone chain, screen-space (already
    // World-to-Screen'd, same as Head/Toe above). skelOk[i] is false
    // when that bone's own transform/position read failed this frame
    // (best-effort, non-rejecting -- a missing limb just isn't drawn,
    // same as hook.h's own ok[b] check).
    Vector3  skelBones[BONE_COUNT];
    bool     skelOk[BONE_COUNT];

    PlayerData()
        : addr(0), Head(Vector3::Zero()), Toe(Vector3::Zero()),
          headWorld(Vector3::Zero()), spineWorld(Vector3::Zero()), neckWorld(Vector3::Zero()),
          isKnocked(false), isVisible(false), isBot(false), distance(0.f), curHP(0), maxHP(200), weaponType(-1)
    {
        name[0] = 0;
        for (int i = 0; i < BONE_COUNT; i++) { skelBones[i] = Vector3::Zero(); skelOk[i] = false; }
    }

    Vector3 getTargetWorld(int targetPos) const {
        switch (targetPos) {
            case 1:  return neckWorld;
            case 2:  return spineWorld;
            default: return headWorld;
        }
    }
};
