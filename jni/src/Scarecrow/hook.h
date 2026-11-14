#pragma once
// v9.1 Brutal=head collider 0x6D8, ESP instant dead purge | v9: Hardware BT (H/B/Legit) + BrutalBT head-only + AimLock COLLIDER + FOV
// No heartbeat wipe, no mid-match ESP clear. offsets.h only.

#include "bool.h"
#include "offsets.h"
#include "ix_features.h"
#include "ix_rt_offsets.h"
#include "../ix_aob.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <map>
#include <cfloat>

extern int  selectedBoneIndex;
extern int  aimBotBoneIndex;
extern int  chestRate;
extern bool g_ShowFovOverlay;

std::atomic<bool> g_EspAlive{false};

struct Matrix { Vector4 Position; Quaternion Rotation; Vector4 Scale; };

static inline uintptr_t GetPlayerHeadTF    (uintptr_t p){ return Read<uintptr_t>(p+BoneOffsets::Head()); }
static inline uintptr_t GetPlayerChestTF   (uintptr_t p){ return Read<uintptr_t>(p+BoneOffsets::Chest()); }
static inline uintptr_t GetPlayerPeTF      (uintptr_t p){ return Read<uintptr_t>(p+BoneOffsets::Root()); }
static inline uintptr_t GetPlayerMainCamera(uintptr_t p){ return Read<uintptr_t>(p+PlayerOffsets::MainCamera()); }

static Vector3 CalcTransformPos(uintptr_t tf){
    Vector3 z = Vector3::Zero();
    auto obj = Read<uintptr_t>(tf+PlayerOffsets::Transform::Object()); if(!obj) return z;
    auto idx = Read<uintptr_t>(obj+PlayerOffsets::Transform::Index());
    auto mv  = Read<uintptr_t>(obj+PlayerOffsets::Transform::Matrix());
    auto ml  = Read<uintptr_t>(mv+PlayerOffsets::Transform::MatrixList());
    auto mi  = Read<uintptr_t>(mv+PlayerOffsets::Transform::MatrixIndices());
    auto res = Read<Vector3>(ml + idx*PlayerOffsets::Transform::Stride());
    int  ti  = Read<int>(mi + idx*4), n = 0;
    while(ti >= 0 && n < PlayerOffsets::Transform::MaxTries()){
        n++;
        auto m  = Read<Matrix>(ml + ti*PlayerOffsets::Transform::Stride());
        float rx=m.Rotation.X, ry=m.Rotation.Y, rz=m.Rotation.Z, rw=m.Rotation.W;
        float sx=res.X*m.Scale.X, sy=res.Y*m.Scale.Y, sz=res.Z*m.Scale.Z;
        res.X = m.Position.X+sx + (sx*(ry*ry*-2.f-rz*rz*2.f))
                                 + (sy*(rw*rz*-2.f-ry*rx*-2.f))
                                 + (sz*(rz*rx*2.f -rw*ry*-2.f));
        res.Y = m.Position.Y+sy + (sx*(rx*ry*2.f -rw*rz*-2.f))
                                 + (sy*(rz*rz*-2.f-rx*rx*2.f))
                                 + (sz*(rw*rx*-2.f-rz*ry*-2.f));
        res.Z = m.Position.Z+sz + (sx*(rw*ry*-2.f-rx*rz*-2.f))
                                 + (sy*(ry*rz*2.f -rw*rx*-2.f))
                                 + (sz*(rx*rx*-2.f-ry*ry*2.f));
        ti = Read<int>(mi + ti*4);
    }
    return (n < PlayerOffsets::Transform::MaxTries()) ? res : z;
}

static inline Vector3 NodePos(uintptr_t node){
    auto t = Read<uintptr_t>(node+PlayerOffsets::Transform::Object());
    return t ? CalcTransformPos(t) : Vector3::Zero();
}
static inline Vector3 BonePos(uintptr_t e, uintptr_t off){
    auto b = Read<uintptr_t>(e+off);
    return b ? NodePos(b) : Vector3::Zero();
}

static inline bool VecZero(const Vector3& v){
    return (v.X == 0.f && v.Y == 0.f && v.Z == 0.f);
}


static inline uint32_t ChestRateRand(){
    static uint32_t s = 0xDEADBEEFu;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}

/* HW BT / Brutal head world position.
 * Bullet was redirecting but missing head → aim point was wrong node.
 * Strategy:
 *   1) Head TF + Chest TF: if head is above chest, use head (skull + small lift)
 *   2) If head TF is spine-level (≈ chest Y), synthesize skull from chest + 0.38m
 *   3) Fallback 0x6D8 node / ColliderNode
 * Silent aim writes ONLY SilentAimDir — no AimCollider on Hardware BT.
 */
static inline Vector3 HwbtHeadWorld(uintptr_t e){
    if(!e) return Vector3::Zero();

    Vector3 head  = BonePos(e, BoneOffsets::Head());
    Vector3 chest = BonePos(e, BoneOffsets::Chest());

    if(!VecZero(head) && !VecZero(chest)){
        /* real head sits above chest */
        if(head.Y > chest.Y + 0.12f){
            head.Y += 0.05f;
            return head;
        }
        /* Head TF ≈ spine: build skull from chest */
        Vector3 skull = chest;
        skull.Y += 0.38f;
        return skull;
    }
    if(!VecZero(chest)){
        chest.Y += 0.38f;
        return chest;
    }
    if(!VecZero(head)){
        head.Y += 0.20f;
        return head;
    }

    uintptr_t n6 = Read<uintptr_t>(e + 0x6D8);
    if(n6){
        Vector3 p = NodePos(n6);
        if(!VecZero(p)){
            if(!VecZero(chest) && p.Y < chest.Y + 0.10f)
                p.Y = chest.Y + 0.38f;
            else
                p.Y += 0.06f;
            return p;
        }
    }

    uintptr_t col = Read<uintptr_t>(e + PlayerOffsets::ColliderNode());
    if(col){
        Vector3 p = NodePos(col);
        if(!VecZero(p)){
            p.Y += 0.20f;
            return p;
        }
    }
    return Vector3::Zero();
}

static inline Vector3 BrutalHeadPos(uintptr_t e){
    return HwbtHeadWorld(e);
}

/* Hardware BT only — bone pick by chestRate.
 * 1=Head, 2=Body, 3=Legit (70% body / 30% head).
 * Aim Silent / Brutal BT do NOT use this. */
static inline Vector3 SilentAimBone(uintptr_t e){
    switch(chestRate){
        case 2: { /* Body */
            Vector3 p = BonePos(e, BoneOffsets::Chest());
            if (VecZero(p)) p = BonePos(e, BoneOffsets::Hip());
            return p;
        }
        case 3: { /* Legit 70% body / 30% head */
            uint32_t r = ChestRateRand() % 100u;
            if (r < 70u) {
                Vector3 p = BonePos(e, BoneOffsets::Chest());
                if (VecZero(p)) p = BonePos(e, BoneOffsets::Hip());
                return p;
            }
            return HwbtHeadWorld(e);
        }
        case 1:
        default:
            return HwbtHeadWorld(e);
    }
}

static inline Vector3 AimbotBone(uintptr_t e){
    return BonePos(e, BoneOffsets::Head());
}

/* Body Kill — body aim point (chest, falling back to hip). Only used once
 * BodyKillThresholdReached() (below) says the enemy's real HP has dropped
 * to/below the slider value; before that, aim stays on the head. Never
 * writes HP — real in-game damage does the work. Independent of
 * chestRate/Hardware BT's old Body/Legit selector (removed from the menu —
 * this replaces it). */
static inline Vector3 BodyKillBone(uintptr_t e){
    if(!e) return Vector3::Zero();
    Vector3 p = BonePos(e, BoneOffsets::Chest());
    if(VecZero(p)) p = BonePos(e, BoneOffsets::Hip());
    return p;
}

static inline Quaternion QNlerp(Quaternion a, Quaternion b, float t){
    if(t < 0.f) t = 0.f;
    if(t > 1.f) t = 1.f;
    float dot = a.X*b.X + a.Y*b.Y + a.Z*b.Z + a.W*b.W;
    if(dot < 0.f){ b.X=-b.X; b.Y=-b.Y; b.Z=-b.Z; b.W=-b.W; }
    Quaternion r;
    r.X = a.X + (b.X - a.X) * t;
    r.Y = a.Y + (b.Y - a.Y) * t;
    r.Z = a.Z + (b.Z - a.Z) * t;
    r.W = a.W + (b.W - a.W) * t;
    float mag = sqrtf(r.X*r.X + r.Y*r.Y + r.Z*r.Z + r.W*r.W);
    if(mag > 1e-6f){ r.X/=mag; r.Y/=mag; r.Z/=mag; r.W/=mag; }
    return r;
}

static inline float AimHumanNoise(){
    static uint32_t s = 0xA5A5A5A5u;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return ((s & 0xFFFF) / 65535.f - 0.5f) * 0.015f;
}

static inline Vector3 W2S(D3DMatrix vm, Vector3 p){
    Vector3 r(-1,-1,-1);
    float v12 = p.X*vm._14 + p.Y*vm._24 + p.Z*vm._34 + vm._44;
    if(v12 < 0.001f) return r;
    float hx=(float)abs_ScreenX/2.f, hy=(float)abs_ScreenY/2.f;
    r.X = hx + hx*(p.X*vm._11 + p.Y*vm._21 + p.Z*vm._31 + vm._41)/v12;
    r.Y = hy - hy*(p.X*vm._12 + p.Y*vm._22 + p.Z*vm._32 + vm._42)/v12;
    r.Z = v12;
    return r;
}

static inline bool OnSc(const Vector3& v, float sw, float sh){
    return (v.Z>0.001f && v.X>1.f && v.Y>1.f && v.X<sw-1.f && v.Y<sh-1.f);
}

static inline bool EspOnScreen(const Vector3& h, const Vector3& f, float sw, float sh, bool wasVisibleLastFrame){
    if (h.Z <= 0.001f && f.Z <= 0.001f) return false;
    const float mEnter = 120.f;
    const float mHold  = wasVisibleLastFrame ? 160.f : mEnter;
    auto ok = [&](const Vector3& v, float m){
        return v.Z > 0.001f && v.X > -m && v.Y > -m && v.X < sw + m && v.Y < sh + m;
    };
    return ok(h, mHold) || ok(f, mHold);
}

static inline ImU32 HpColor(int h){
    if(h>=80) return IM_COL32(0,220,0,255);
    if(h>=50) return IM_COL32(255,220,0,255);
    if(h>=25) return IM_COL32(255,140,0,255);
    return IM_COL32(255,50,50,255);
}
static inline ImU32 DistColor(float d){
    if(d<50)  return IM_COL32(255,50,50,255);
    if(d<150) return IM_COL32(255,220,0,255);
    return IM_COL32(0,220,0,255);
}
static ImU32 RainbowColor(float off=0.f){
    static float hue=0.f;
    static auto  last=std::chrono::steady_clock::now();
    auto now=std::chrono::steady_clock::now();
    hue+=std::chrono::duration<float>(now-last).count()*0.10f;
    last=now; if(hue>1.f) hue-=1.f;
    return ImColor::HSV(fmodf(hue+off,1.f),1.f,1.f);
}

// ── FEATURE TOGGLES ─────────────────────────────────────────
bool  Enable       = false;
bool  showLine     = false;
bool  showDistance = false;
bool  showBox      = false;
bool  showName     = true;
bool  showHealth   = false;
bool  showCount    = false;
bool  AimLock      = false;
bool  AimLockNew   = false;
bool  Aimbot       = false;
bool  AimSilent    = false;
bool  HardwareBT   = false;  /* Hardware BT — Body / Legit modes */
bool  GhostHack     = false;
bool  NoRecoil      = false;
bool  NoReload      = false;
int   SilentAimPercentage = 100;  /* 100=head, 0=body — set from chestRate */
bool  g_ExploitFloatUI = false;
bool  g_ExploitFloatGhost = false;   /* floating exploit icons on screen */
bool  BrutalBT     = false;
bool  AimMagnet    = false;   /* pull enemy feet onto fire ray (XZ) while firing */
bool  DownPlayer   = false;   /* magnet vertical pull-down while firing */
bool  FastReload   = false;
bool  FlyHack      = false;
bool  UpEnemy      = false;   /* raise FOV target while firing */
bool  UnderGround  = false;
bool  SpinBody     = false;
bool  PullEnemy    = false;   /* lock FOV target to crosshair ray */
bool  ScaleDown    = false;   /* shrink FOV target visual root scale */
bool  Telekill     = false;   /* short-range snap local near target (guarded) */
bool  showAimLine  = false;
// Body Kill — head aim until target's real HP drops to/below BodyKillHP,
// then switches to body. Read-only threshold check; never writes HP.
bool  BodyKill     = false;
float BodyKillHP   = 100.f;  // 1–200; set from g_Cfg.bodyKillHP

// ============================================================
//  BODY KILL — threshold switch, NOT a forced-HP hack.
//  Health is only ever READ here, never written.
//  Real in-game headshots (Aim Silent phase) reduce enemy HP
//  naturally. Once actual HP drops to or below the slider value
//  (BodyKillHP, 1–200), every subsequent bullet redirects to
//  body. Before that point aim targets the head (like Aim Silent).
//  Defined here (right after BodyKill/BodyKillHP, before the FOV
//  loop + UpdateSilentAimFast) so both call sites can reach it
//  without a forward declaration.
// ============================================================
static inline bool BodyKillThresholdReached(uintptr_t e){
    if(!BodyKill || !e) return false;
    uintptr_t hpC = ReadChain(e,{
        PlayerOffsets::HealthLink1(),
        PlayerOffsets::HealthLink2(),
        PlayerOffsets::HealthLink3()
    });
    if(!hpC) return false;
    int curHP = Read<int>(hpC + PlayerOffsets::Health());
    int threshold = (int)BodyKillHP;
    if(threshold < 1)   threshold = 1;
    if(threshold > 200) threshold = 200;
    return curHP <= threshold;
}

float MagnetMaxDist    = 4.0f;
float UndergroundDepth = 1.5f;
float FlyHeight        = 1.2f;
float SpinSpeed        = 0.35f;
float PullEnemyDist    = 3.5f;  /* metres in front of camera */
float TelekillMaxDist  = 25.0f;

float AimSmooth    = 6.0f;

static Vector3   g_AB_smoothPos     = {0,0,0};
static bool      g_AB_smoothInit    = false;
static uintptr_t g_AB_lastTgtOwner  = 0;
static Vector3   g_SA_smoothPos     = {0,0,0};
static bool      g_SA_smoothInit    = false;

bool  showSkeleton = false;
float AimFov       = 150.f;

/* Called when session/offsets not ready — wipe toggles */
static inline void IxForceDisableCheats() {
    Enable = false;
    showLine = false; showDistance = false; showBox = false;
    showName = false; showHealth = false; showCount = false;
    AimLock = false; AimLockNew = false; Aimbot = false; AimSilent = false;
    HardwareBT = false;
    GhostHack = false; NoRecoil = false; NoReload = false;
    BrutalBT = false; AimMagnet = false;
    showSkeleton = false; showAimLine = false;
    g_ExploitFloatUI = false; g_ExploitFloatGhost = false;
    g_ShowFovOverlay = false;
    DownPlayer = false; FastReload = false; FlyHack = false;
    UpEnemy = false; UnderGround = false; SpinBody = false;
    PullEnemy = false; ScaleDown = false; Telekill = false;
    BodyKill = false;
}

int   textSize     = 15;
bool  is3600       = false;

int    boxStyle       = 0;
float  boxThickness   = 1.8f;
int    boxColorMode   = 0;
ImVec4 boxCustomColor = ImVec4(1.f,1.f,1.f,1.f);

float  lineThickness   = 1.5f;
int    lineColorMode   = 0;
ImVec4 lineCustomColor = ImVec4(1.f,1.f,1.f,1.f);

float  healthBarWidth          = 5.f;
float  healthBarRounding       = 1.5f;
bool   showHealthText          = true;
bool   showHealthColorGradient = true;

bool   showHeadDot       = false;
float  showHeadDotRadius = 4.f;
ImVec4 headDotColor      = ImVec4(1.f,0.f,0.f,1.f);

bool      g_HasAimTarget      = false;
uintptr_t g_CurrentBestTarget = 0;
int       currentTargetIndex  = -1;
char      PlayerName[64]      = {};

static const ImU32 TRI_SAFFRON = IM_COL32(255,153, 51,255);
static const ImU32 TRI_WHITE   = IM_COL32(255,255,255,255);
static const ImU32 TRI_GREEN   = IM_COL32( 19,136,  8,255);
static const ImU32 TRI_CHAKRA  = IM_COL32(  0,  0,128,255);

static inline ImU32 BoxCol(int h,float d,bool k){
    if(k) return IM_COL32(255,60,60,255);
    switch(boxColorMode){
        case 1:  return HpColor(h);
        case 2:  return DistColor(d);
        case 3:  return RainbowColor(0.f);
        default: return ImGui::ColorConvertFloat4ToU32(boxCustomColor);
    }
}
static inline ImU32 LineCol(int h,float d,bool k){
    if(k) return IM_COL32(255,100,0,255);
    switch(lineColorMode){
        case 1:  return HpColor(h);
        case 2:  return DistColor(d);
        case 3:  return RainbowColor(0.3f);
        default: return ImGui::ColorConvertFloat4ToU32(lineCustomColor);
    }
}

enum { BONE_HEAD=0, BONE_CHEST, BONE_HIP, BONE_LSH, BONE_RSH,
       BONE_LHAND, BONE_RHAND, BONE_LFOOT, BONE_RFOOT, BONE_COUNT };

struct EspEntry {
    uintptr_t id;
    Vector3   wHead;
    Vector3   wFeet;
    Vector3   wBones[BONE_COUNT];
    float     dist;
    int       hp;
    bool      knocked;
    bool      isBot;
    char      name[64];
    bool      skelValid;
};
static const int MAX_ESP = 64;

struct EspBuffer {
    EspEntry  entries[MAX_ESP];
    int       count;
    float     sw, sh;
    D3DMatrix vm;
    bool      vmValid;
    uintptr_t camIC;
    bool      hasAimLine;
    Vector3   aimWorld;
    bool      hasFov;
    float     fovRadius;
    bool      fovHasTarget;
};

// ── SEQLOCK PUBLISH ─────────────────────────────────────────
static EspBuffer              g_StageBuf;
static EspBuffer              g_PubBuf;
static std::atomic<uint32_t>  g_PubSeq{0};
static std::atomic<bool>      g_BufReady{false};
static std::atomic<uint64_t>  g_LastPublishMs{0};

static void PublishEspBuffer(const EspBuffer& src){
    uint32_t s = g_PubSeq.load(std::memory_order_relaxed);
    g_PubSeq.store(s + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);
    g_PubBuf = src;
    std::atomic_thread_fence(std::memory_order_release);
    g_PubSeq.store(s + 2, std::memory_order_release);
    g_BufReady.store(true, std::memory_order_release);
    g_LastPublishMs.store(
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count(),
        std::memory_order_release);
}

// ── THREAD FLAGS ────────────────────────────────────────────
static std::atomic<bool> espThreadRunning{false};
static std::atomic<bool> silentThreadRunning{false};
static std::atomic<bool> aimbotThreadRunning{false};
static std::atomic<bool> bodyKillThreadRunning{false};
static std::atomic<bool> aimlockNewThreadRunning{false};

static uintptr_t  g_SA_lp  = 0;
static Vector3    g_SA_tgt = {0,0,0};
static std::mutex g_SA_mtx;

static uintptr_t  g_AB_lp  = 0;
static Vector3    g_AB_tgt = {0,0,0};
static std::mutex g_AB_mtx;

static std::atomic<uintptr_t> g_EspLib{0};
static std::atomic<float>     g_EspSW{0}, g_EspSH{0};

// ── CAMERA CACHE ────────────────────────────────────────────
static D3DMatrix  g_LastGoodMX;
static uintptr_t  g_LastGoodIC = 0;
static bool       g_HasLastCam = false;
static D3DMatrix  g_PrevOwnMX  = {};
static bool       g_HasPrevOwn = false;
static int        g_OwnFrozen  = 0;
static uintptr_t  g_CamEnt     = 0;

static bool       g_LocalPlayerIsDead = false;
static uintptr_t  g_TrueLocalPlayer   = 0;
static int        g_LastMatchState    = -1;

// ── SESSION TRACKING ────────────────────────────────────────
static std::atomic<uint64_t>  g_SessionId{0};
static uintptr_t              g_SessionLib     = 0;
static uint64_t               g_SessionResetMs = 0;

// ── HEARTBEAT (auto-recovery) ───────────────────────────────
static std::atomic<uint64_t>  g_LastGoodScanMs{0};
static const uint64_t HEARTBEAT_TIMEOUT_MS = 8000ULL;

// ── TRACKER ─────────────────────────────────────────────────
struct TrackedEntry {
    EspEntry data;
    uint64_t lastSeenMs;
};
static std::map<uintptr_t, TrackedEntry> g_Tracked;
static const uint64_t ENTRY_TTL_MS = 350;

// ── VISIBLE STREAK ──────────────────────────────────────────
static std::map<uintptr_t,int> g_VisibleStreakLocal;

static int       g_ChainFailStreak = 0;
static const int CHAIN_FAIL_LIMIT  = 150;
static uint64_t  g_ChainFailStart  = 0;

static inline uint64_t NowMs(){
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static int ReadLocalPlayerHP(uintptr_t lp){
    auto hpC = ReadChain(lp,{PlayerOffsets::HealthLink1(),PlayerOffsets::HealthLink2(),PlayerOffsets::HealthLink3()});
    if(!hpC) return 0;
    return Read<int>(hpC+PlayerOffsets::Health());
}

// ============================================================
//  NAME READER — NO FALLBACK
//  Real name only. Empty string if cannot be read.
// ============================================================
static bool DecodeUnityString(uintptr_t np, char* out, int outMax){
    out[0] = 0;
    if(np < 0x10000ULL || np > 0x7FFFFFFFFFFFULL) return false;

    // 1) Project helper (reads engine string)
    char tmp[64] = {};
    getUTF8(tmp, np);
    tmp[63] = 0;
    if(tmp[0]){
        int n = 0;
        for(int i=0; tmp[i] && n < outMax-1; i++){
            unsigned char c = (unsigned char)tmp[i];
            if(c >= 32 && c != 0x7F) out[n++] = (char)c;
        }
        out[n] = 0;
        if(out[0]) return true;
    }

    // 2) IL2CPP System.String: length @+0x10, UTF-16 chars @+0x14
    int len = Read<int>(np + 0x10);
    if(len > 0 && len < 48){
        int n = 0;
        for(int i=0; i<len && n<outMax-1; i++){
            uint16_t c = Read<uint16_t>(np + 0x14 + (uintptr_t)i * 2);
            if(c == 0) break;
            if(c < 32 || c == 0x7F) continue;
            out[n++] = (c < 128) ? (char)c : '?';
        }
        out[n] = 0;
        if(out[0]) return true;
    }

    // 3) Some builds: length @+0x8 (packed object header differences)
    len = Read<int>(np + 0x8);
    if(len > 0 && len < 48){
        int n = 0;
        for(int i=0; i<len && n<outMax-1; i++){
            uint16_t c = Read<uint16_t>(np + 0xC + (uintptr_t)i * 2);
            if(c == 0) break;
            if(c < 32 || c == 0x7F) continue;
            out[n++] = (c < 128) ? (char)c : '?';
        }
        out[n] = 0;
        if(out[0]) return true;
    }

    // 4) Direct ASCII buffer
    {
        int n = 0; bool ok = true;
        for(; n < outMax-1; n++){
            char c = Read<char>(np + (uintptr_t)n);
            if(c == 0) break;
            if((unsigned char)c < 32 || c == 0x7F){ ok = false; break; }
            out[n] = c;
        }
        if(ok && n > 0){ out[n] = 0; return true; }
        out[0] = 0;
    }
    return false;
}

static void ReadEnemyName(uintptr_t e, char* out, bool& isBot){
    out[0] = 0;
    isBot  = false;
    if(!e) return;

    // Bot flag: try offsets.h + dump-confirmed 0x4A8
    int botFlag = Read<int>(e + PlayerOffsets::IsClientBot());
    if(botFlag != 1){
        int b2 = Read<int>(e + 0x4A8);
        if(b2 == 1) botFlag = 1;
    }
    // bool-sized bot field
    if(botFlag != 1){
        if(Read<uint8_t>(e + PlayerOffsets::IsClientBot()) == 1) botFlag = 1;
        else if(Read<uint8_t>(e + 0x4A8) == 1) botFlag = 1;
    }
    isBot = (botFlag == 1);

    // Name pointer candidates: offsets.h NickName + dump OriginalNickName 0x4A0
    static const uintptr_t kNameOffs[] = {
        PlayerOffsets::NickName(),
        (uintptr_t)0x4A0,
        (uintptr_t)0x448,
        (uintptr_t)0x4A8 - 0x8,
    };
    for(uintptr_t off : kNameOffs){
        uintptr_t np = Read<uintptr_t>(e + off);
        if(DecodeUnityString(np, out, 64)) break;
    }

    // Bot with no readable name → show BOT (only for bots)
    if(!out[0] && isBot){
        out[0]='B'; out[1]='O'; out[2]='T'; out[3]=0;
    }
}

static void ClearAimTargets(){
    {std::lock_guard<std::mutex>lk(g_SA_mtx); g_SA_tgt={0,0,0};}
    {std::lock_guard<std::mutex>lk(g_AB_mtx); g_AB_tgt={0,0,0}; g_AB_lp=0;}
}


// ============================================================
//  AIM MAGNET — logic from all-fix MagnetLogic::ProcessMagnet
//  Mapped to THIS project's offsets only:
//    feet node  = BoneOffsets::Root() via GetPlayerPeTF
//    transform  = PlayerOffsets::Transform::Object() / Matrix
//    fire gate  = PlayerOffsets::IsFiring (no foreign 0x344)
//  Unity internal write slots 0x90 / 0xA0 are transform-data
//  layout (same as all-fix), not game entity dump offsets.
// ============================================================
namespace MagnetLogic {
    /* data.cpp path:
         feetTF + TransformToObj(0x10) → trVal
         trVal  + ObjToMatrix(0x38)    → transformPtr
         write transformPtr + 0x90
       (extra Object hop was wrong — pull never landed) */
    static std::map<uintptr_t, Vector3> BasePosMap;
    static std::map<uintptr_t, Vector3> ColliderBasePosMap;   // real hitbox original pos, for restore
    static std::mutex magnetMutex;
    static uintptr_t stablePullEntity = 0;
    static Vector3 smoothedCamPos{}, smoothedFireDir{}, smoothWritePos{};
    static float smoothedProjLen = 0.f;
    static bool pullSmoothInited = false;

    static inline uintptr_t ResolveWriteTransform(uintptr_t feetTF) {
        if (!feetTF) return 0;
        uintptr_t trVal = Read<uintptr_t>(feetTF + PlayerOffsets::Transform::Object());
        if (!trVal) return 0;
        /* data.cpp: ObjToMatrix directly on trVal — NOT trVal→Object→Matrix */
        return Read<uintptr_t>(trVal + PlayerOffsets::Transform::Matrix());
    }

    /* FAKE-DAMAGE FIX: the write above only moves the cosmetic render
     * Transform (feetTF/root). The game's actual hit-detection runs against
     * the enemy's real physics collider (PlayerOffsets::ColliderNode()),
     * which this never touched — so the enemy visually got pulled onto the
     * crosshair but shots registered against the old, un-pulled hitbox,
     * producing damage numbers that don't actually land (fake damage).
     * Move the real collider node's transform to the SAME pulled position
     * so visual pull and hit-validation stay in sync. */
    static inline void WriteColliderPos(uintptr_t enemy, const Vector3& pos) {
        uintptr_t colNode = Read<uintptr_t>(enemy + PlayerOffsets::ColliderNode());
        if (!colNode) return;
        uintptr_t transformPtr = ResolveWriteTransform(colNode);
        if (transformPtr)
            Write<Vector3>(transformPtr + 0x90, pos);
    }

    inline void RestoreAllPositions() {
        if (BasePosMap.empty()) return;
        for (auto const& kv : BasePosMap) {
            uintptr_t enemy = kv.first;
            const Vector3& originalPos = kv.second;
            uintptr_t feetTF = GetPlayerPeTF(enemy);
            uintptr_t transformPtr = ResolveWriteTransform(feetTF);
            if (transformPtr)
                Write<Vector3>(transformPtr + 0x90, originalPos);

            // Restore the real collider too, if we moved it
            auto colIt = ColliderBasePosMap.find(enemy);
            if (colIt != ColliderBasePosMap.end()) {
                WriteColliderPos(enemy, colIt->second);
            }
        }
        ColliderBasePosMap.clear();
        BasePosMap.clear();
        pullSmoothInited = false;
        stablePullEntity = 0;
    }

    inline void ProcessMagnet(bool magnetEnabled, uintptr_t target,
                              const D3DMatrix& viewMatrix, Vector3 camPos,
                              bool isFiring, uintptr_t localPlayer) {
        std::lock_guard<std::mutex> lock(magnetMutex);

        if (!magnetEnabled || !localPlayer || !target || !isFiring) {
            RestoreAllPositions();
            return;
        }

        uintptr_t feetTF = GetPlayerPeTF(target);
        if (!feetTF) { RestoreAllPositions(); return; }

        Vector3 currentRootPos = NodePos(feetTF);
        if (VecZero(currentRootPos)) return;

        if (BasePosMap.find(target) == BasePosMap.end())
            BasePosMap[target] = currentRootPos;
        Vector3 originalPos = BasePosMap[target];

        // Snapshot the real collider's original position once, so it can
        // be restored exactly when the pull ends / target changes.
        if (ColliderBasePosMap.find(target) == ColliderBasePosMap.end()) {
            uintptr_t colNode = Read<uintptr_t>(target + PlayerOffsets::ColliderNode());
            if (colNode) {
                Vector3 colPos = NodePos(colNode);
                if (!VecZero(colPos))
                    ColliderBasePosMap[target] = colPos;
            }
        }

        Vector3 rawForward = { viewMatrix._13, viewMatrix._23, viewMatrix._33 };
        float flen = sqrtf(rawForward.X*rawForward.X + rawForward.Y*rawForward.Y + rawForward.Z*rawForward.Z);
        if (flen < 0.001f) return;
        rawForward.X /= flen; rawForward.Y /= flen; rawForward.Z /= flen;

        const float CamSmoothAlpha = 0.20f;
        const float DirSmoothAlpha = 0.15f;
        const float ProjSmoothAlpha = 0.20f;
        const float WriteSmoothAlpha = 0.35f;

        if (!pullSmoothInited || stablePullEntity != target) {
            smoothedCamPos = camPos;
            smoothedFireDir = rawForward;
            Vector3 toE = { currentRootPos.X - camPos.X, currentRootPos.Y - camPos.Y, currentRootPos.Z - camPos.Z };
            float rp = toE.X*rawForward.X + toE.Y*rawForward.Y + toE.Z*rawForward.Z;
            smoothedProjLen = rp < 1.5f ? 1.5f : rp;
            smoothWritePos = currentRootPos;
            stablePullEntity = target;
            pullSmoothInited = true;
        } else {
            smoothedCamPos.X += (camPos.X - smoothedCamPos.X) * CamSmoothAlpha;
            smoothedCamPos.Y += (camPos.Y - smoothedCamPos.Y) * CamSmoothAlpha;
            smoothedCamPos.Z += (camPos.Z - smoothedCamPos.Z) * CamSmoothAlpha;
            Vector3 blended = {
                smoothedFireDir.X + (rawForward.X - smoothedFireDir.X) * DirSmoothAlpha,
                smoothedFireDir.Y + (rawForward.Y - smoothedFireDir.Y) * DirSmoothAlpha,
                smoothedFireDir.Z + (rawForward.Z - smoothedFireDir.Z) * DirSmoothAlpha
            };
            float blen = sqrtf(blended.X*blended.X + blended.Y*blended.Y + blended.Z*blended.Z);
            if (blen > 0.001f) {
                smoothedFireDir.X = blended.X/blen;
                smoothedFireDir.Y = blended.Y/blen;
                smoothedFireDir.Z = blended.Z/blen;
            } else smoothedFireDir = rawForward;
            Vector3 toEnemy = {
                currentRootPos.X - smoothedCamPos.X,
                currentRootPos.Y - smoothedCamPos.Y,
                currentRootPos.Z - smoothedCamPos.Z
            };
            float rawProj = toEnemy.X*rawForward.X + toEnemy.Y*rawForward.Y + toEnemy.Z*rawForward.Z;
            if (rawProj < 1.5f) rawProj = 1.5f;
            smoothedProjLen += (rawProj - smoothedProjLen) * ProjSmoothAlpha;
        }

        Vector3 targetPos = {
            smoothedCamPos.X + smoothedFireDir.X * smoothedProjLen,
            smoothedCamPos.Y + smoothedFireDir.Y * smoothedProjLen,
            smoothedCamPos.Z + smoothedFireDir.Z * smoothedProjLen
        };
        targetPos.Y += 0.13f;

        float maxDrag = MagnetMaxDist;
        if (maxDrag < 0.5f) maxDrag = 0.5f;
        if (maxDrag > 8.0f) maxDrag = 8.0f;
        float deltaX = targetPos.X - originalPos.X;
        float deltaZ = targetPos.Z - originalPos.Z;
        if (fabsf(deltaX) > maxDrag) targetPos.X = originalPos.X + (deltaX > 0 ? maxDrag : -maxDrag);
        if (fabsf(deltaZ) > maxDrag) targetPos.Z = originalPos.Z + (deltaZ > 0 ? maxDrag : -maxDrag);

        const float maxUp = 0.44f;
        if (targetPos.Y < originalPos.Y) targetPos.Y = originalPos.Y;
        else if (targetPos.Y - originalPos.Y > maxUp) targetPos.Y = originalPos.Y + maxUp;

        smoothWritePos.X += (targetPos.X - smoothWritePos.X) * WriteSmoothAlpha;
        smoothWritePos.Y += (targetPos.Y - smoothWritePos.Y) * WriteSmoothAlpha;
        smoothWritePos.Z += (targetPos.Z - smoothWritePos.Z) * WriteSmoothAlpha;

        uintptr_t transformPtr = ResolveWriteTransform(feetTF);
        if (transformPtr)
            Write<Vector3>(transformPtr + 0x90, smoothWritePos);

        // Move the REAL collider to the same pulled position every frame —
        // this is the fake-damage fix: without this, the hitbox stays put
        // while only the visual mesh moves, so shots at the pulled model
        // don't register against the real collider.
        WriteColliderPos(target, smoothWritePos);
    }
} // namespace MagnetLogic

// ============================================================
//  EXPLOIT HELPERS — same transform resolve as Aim Magnet
//  Entity offsets: only BoneOffsets / PlayerOffsets from THIS project.
// ============================================================








/* ===== Ghost + NoRecoil + NoReload — exact port from working hook.h ===== */
static inline void IxUpdateGhost(uintptr_t lp){
    if(!lp) return;
    /* Offsets::GhostHack = 0x814 continuous bool write */
    Write<bool>(lp + PlayerOffsets::GhostHack(), GhostHack);
}

static inline void IxUpdateNoRecoil(uintptr_t lp){
    /* exact: invMgr → activeItem → wcomp → float 0 */
    if(!NoRecoil || !lp) return;
    uintptr_t invMgr = Read<uintptr_t>(lp + PlayerOffsets::InventoryManager());
    if(!invMgr) return;
    uintptr_t activeItem = Read<uintptr_t>(invMgr + PlayerOffsets::InventoryActiveItem());
    if(!activeItem) return;
    uintptr_t wcomp = Read<uintptr_t>(activeItem + PlayerOffsets::InventoryWeaponComp());
    if(!wcomp) return;
    Write<float>(wcomp + PlayerOffsets::InventoryNoRecoil(), 0.0f);
}

static inline void IxUpdateNoReload(uintptr_t lp){
    if(!lp) return;
    uintptr_t attrs = Read<uintptr_t>(lp + PlayerOffsets::PlayerAttributes());
    if(!attrs) return;
    static uintptr_t lastAttrs = 0;
    static bool origSaved = false;
    static bool origValue = false;
    if(attrs != lastAttrs){ lastAttrs = attrs; origSaved = false; origValue = false; }
    if(NoReload){
        if(!origSaved){ origValue = Read<bool>(attrs + PlayerOffsets::ShootNoReload()); origSaved = true; }
        Write<bool>(attrs + PlayerOffsets::ShootNoReload(), true);
    } else if(origSaved){
        Write<bool>(attrs + PlayerOffsets::ShootNoReload(), origValue);
        origSaved = false;
    }
}



static inline void IxSyncSilentPercentFromChestRate(){
    /* Hardware BT only */
    if (chestRate == 2) SilentAimPercentage = 0;
    else if (chestRate == 3) SilentAimPercentage = 30; /* 30% head in legit */
    else SilentAimPercentage = 100;
}

namespace ExploitX {
    static std::atomic<bool> thrFly{false}, thrUnder{false}, thrSpin{false};
    static std::atomic<uintptr_t> g_Local{0};
    static std::atomic<uintptr_t> g_BestEnemy{0};
    static std::mutex pullMu;
    static std::map<uintptr_t, Vector3> pullBase;
    static std::map<uintptr_t, Vector3> scaleBase; // unused pos; scale uses quat/scale slot

    static inline uintptr_t WriteTF(uintptr_t entity) {
        uintptr_t feet = GetPlayerPeTF(entity);
        return feet ? MagnetLogic::ResolveWriteTransform(feet) : 0;
    }
    static inline bool ReadPos(uintptr_t entity, Vector3& out) {
        uintptr_t tf = WriteTF(entity);
        if (!tf) return false;
        out = Read<Vector3>(tf + 0x90);
        return !(out.X == 0.f && out.Y == 0.f && out.Z == 0.f);
    }
    static inline void WritePos(uintptr_t entity, const Vector3& p) {
        uintptr_t tf = WriteTF(entity);
        if (tf) Write<Vector3>(tf + 0x90, p);
    }

    /* Fast reload — PlayerAttributes path from current dump family (not all-fix 0x708/0xD9) */
    static inline void TickFastReload(uintptr_t lp) {
        if (!FastReload || !lp) return;
        /* Player + attributes object; bool NoReload field */
        uintptr_t attr = Read<uintptr_t>(lp + 0x770);
        if (!attr || attr < 0x10000ULL) return;
        Write<uint8_t>(attr + 0x111, 1);
    }

    static void FlyThread() {
        bool active = false; float lockedY = 0.f;
        while (thrFly.load()) {
            uintptr_t lp = g_Local.load();
            if (!FlyHack || !lp) { active = false; std::this_thread::sleep_for(std::chrono::milliseconds(40)); continue; }
            Vector3 pos;
            if (!ReadPos(lp, pos)) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); continue; }
            if (!active) { lockedY = pos.Y + FlyHeight; active = true; }
            pos.Y = lockedY;
            for (int i = 0; i < 6; i++) WritePos(lp, pos);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
    static void UnderThread() {
        bool active = false; float baseY = 0.f;
        while (thrUnder.load()) {
            uintptr_t lp = g_Local.load();
            if (!UnderGround || !lp) { active = false; std::this_thread::sleep_for(std::chrono::milliseconds(40)); continue; }
            Vector3 pos;
            if (!ReadPos(lp, pos)) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); continue; }
            if (!active) { baseY = pos.Y; active = true; }
            float d = UndergroundDepth; if (d < 0.2f) d = 0.2f; if (d > 5.f) d = 5.f;
            pos.Y = baseY - d;
            WritePos(lp, pos);
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    }
    static void SpinThread() {
        float angle = 0.f;
        while (thrSpin.load()) {
            uintptr_t lp = g_Local.load();
            if (!SpinBody || !lp) { std::this_thread::sleep_for(std::chrono::milliseconds(40)); continue; }
            uintptr_t tf = WriteTF(lp);
            if (!tf) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); continue; }
            float sp = SpinSpeed; if (sp < 0.05f) sp = 0.05f; if (sp > 2.f) sp = 2.f;
            angle += sp;
            if (angle > 6.2831853f) angle -= 6.2831853f;
            /* yaw quaternion around Y */
            float half = angle * 0.5f;
            Quaternion q;
            q.X = 0.f; q.Y = sinf(half); q.Z = 0.f; q.W = cosf(half);
            Write<Quaternion>(tf + 0xA0, q);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    static inline void EnsureThreads() {
        static std::once_flag once;
        std::call_once(once, [](){
            thrFly = true; thrUnder = true; thrSpin = true;
            std::thread(FlyThread).detach();
            std::thread(UnderThread).detach();
            std::thread(SpinThread).detach();
        });
    }

    static inline void TickUpEnemy(uintptr_t lp, uintptr_t enemy, bool firing) {
        if (!UpEnemy || !lp || !enemy || !firing) return;
        Vector3 pos;
        if (!ReadPos(enemy, pos)) return;
        pos.Y += 0.35f;
        WritePos(enemy, pos);
    }

    static inline void TickPullEnemy(uintptr_t lp, uintptr_t enemy, const D3DMatrix& vm, bool firing) {
        std::lock_guard<std::mutex> lk(pullMu);
        if (!PullEnemy || !lp || !enemy || !firing) {
            /* restore */
            for (auto& kv : pullBase) WritePos(kv.first, kv.second);
            pullBase.clear();
            return;
        }
        Vector3 cam = BonePos(lp, BoneOffsets::Head());
        if (VecZero(cam)) cam = BonePos(lp, BoneOffsets::Chest());
        if (VecZero(cam)) return;
        Vector3 fwd = {vm._13, vm._23, vm._33};
        float fl = sqrtf(fwd.X*fwd.X+fwd.Y*fwd.Y+fwd.Z*fwd.Z);
        if (fl < 0.001f) return;
        fwd.X/=fl; fwd.Y/=fl; fwd.Z/=fl;
        Vector3 pos;
        if (!ReadPos(enemy, pos)) return;
        if (pullBase.find(enemy) == pullBase.end()) pullBase[enemy] = pos;
        float dist = PullEnemyDist;
        if (dist < 1.f) dist = 1.f; if (dist > 15.f) dist = 15.f;
        Vector3 np = { cam.X + fwd.X*dist, cam.Y + fwd.Y*dist, cam.Z + fwd.Z*dist };
        /* keep a bit of original height blend */
        np.Y = pullBase[enemy].Y * 0.3f + np.Y * 0.7f;
        WritePos(enemy, np);
    }

    static inline void TickScaleDown(uintptr_t enemy) {
        if (!enemy) return;
        uintptr_t tf = WriteTF(enemy);
        if (!tf) return;
        if (ScaleDown) {
            /* scale vector often near 0xB0 on same transform block — write small scale */
            Write<Vector3>(tf + 0xB0, Vector3{0.15f, 0.15f, 0.15f});
        }
    }

    static inline void TickTelekill(uintptr_t lp, uintptr_t enemy, bool firing) {
        if (!Telekill || !lp || !enemy || !firing) return;
        Vector3 epos, lpos;
        if (!ReadPos(enemy, epos) || !ReadPos(lp, lpos)) return;
        float dx = epos.X-lpos.X, dy = epos.Y-lpos.Y, dz = epos.Z-lpos.Z;
        float d = sqrtf(dx*dx+dy*dy+dz*dz);
        if (d > TelekillMaxDist || d < 0.5f) return;
        /* place local slightly in front of enemy feet */
        Vector3 np = epos;
        np.Y += 0.1f;
        np.X -= dx / d * 1.2f;
        np.Z -= dz / d * 1.2f;
        WritePos(lp, np);
    }

    static inline void OnEspTick(uintptr_t lp, uintptr_t bestEnemy, const D3DMatrix& mx, bool firing) {
        EnsureThreads();
        g_Local.store(lp);
        g_BestEnemy.store(bestEnemy);
        TickFastReload(lp);
        TickUpEnemy(lp, bestEnemy, firing);
        TickPullEnemy(lp, bestEnemy, mx, firing);
        if (ScaleDown && bestEnemy) TickScaleDown(bestEnemy);
        TickTelekill(lp, bestEnemy, firing);
    }

    static inline void Reset() {
        std::lock_guard<std::mutex> lk(pullMu);
        for (auto& kv : pullBase) WritePos(kv.first, kv.second);
        pullBase.clear();
    }
} // namespace ExploitX


// ── SESSION RESET ───────────────────────────────────────────
static void ResetSessionState(){
    g_HasLastCam  = false;
    g_LastGoodIC  = 0;
    g_LastGoodMX  = {};
    g_PrevOwnMX   = {};
    g_HasPrevOwn  = false;
    g_OwnFrozen   = 0;
    g_CamEnt      = 0;
    g_LocalPlayerIsDead = false;
    g_TrueLocalPlayer   = 0;
    g_LastMatchState    = -1;
    g_Tracked.clear();
    g_VisibleStreakLocal.clear();
    g_ChainFailStreak = 0;
    g_ChainFailStart  = 0;
    g_SA_smoothInit = false;
    g_AB_smoothInit = false;
    g_AB_lastTgtOwner = 0;
    ClearAimTargets();
    MagnetLogic::RestoreAllPositions();
    ExploitX::Reset();
    g_SessionResetMs  = NowMs();
    g_LastGoodScanMs.store(g_SessionResetMs, std::memory_order_release);
    g_SessionId.fetch_add(1, std::memory_order_release);
}

// ============================================================
//  ESP THREAD
// ============================================================
void EspThreadFunc(){
    // Init heartbeat — prevents instant trigger on first loop
    g_LastGoodScanMs.store(NowMs(), std::memory_order_release);

    while(espThreadRunning){
        /* HARD: no server offsets/features → no ESP work at all */
        if (!ix_rt_ready() || !ix_feat_esp()) {
            IxForceDisableCheats();
            g_EspAlive.store(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        uintptr_t lib = g_EspLib.load();
        float sw = g_EspSW.load(), sh = g_EspSH.load();

        // ── LIB CHANGE DETECTION ────────────────────────────
        if(lib != 0 && lib != g_SessionLib){
            g_SessionLib = lib;
            ResetSessionState();
        }


        // Warmup after reset
        if(g_SessionResetMs != 0 && (NowMs() - g_SessionResetMs) < 200ULL){
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            continue;
        }

        if(!lib || sw < 10.f || sh < 10.f){
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            continue;
        }

        float sw2 = sw * 0.5f;
        float sh2 = sh * 0.5f;

        EspBuffer& buf = g_StageBuf;
        buf.count=0; buf.sw=sw; buf.sh=sh;
        buf.hasAimLine=false;

        float fovRadius = (AimFov/180.0f)*(sh/2.0f);
        buf.hasFov       = g_ShowFovOverlay && fovRadius>0.f;
        buf.fovRadius    = fovRadius;
        buf.fovHasTarget = false;

        // ── POINTER CHAIN (more retries) ────────────────────
        uintptr_t GF=0,P2=0,BG=0,MM=0,lp=0,dict=0,elist=0;
        int matchState=0;
        for(int retry=0; retry<6; retry++){
            GF = Read<uintptr_t>(lib + PlayerOffsets::GameFacade()); if(!GF) { std::this_thread::sleep_for(std::chrono::microseconds(200)); continue; }
            P2 = Read<uintptr_t>(GF + PlayerOffsets::FacadeGame());  if(!P2) { std::this_thread::sleep_for(std::chrono::microseconds(200)); continue; }
            BG = Read<uintptr_t>(P2 + PlayerOffsets::FacadeBase());  if(!BG) { std::this_thread::sleep_for(std::chrono::microseconds(200)); continue; }
            MM = Read<uintptr_t>(BG + PlayerOffsets::BaseGame());    if(!MM) { std::this_thread::sleep_for(std::chrono::microseconds(200)); continue; }
            matchState = Read<int>(MM + PlayerOffsets::MatchRunning());
            lp   = Read<uintptr_t>(MM + PlayerOffsets::MatchLocal());
            dict = Read<uintptr_t>(BG + PlayerOffsets::Dictionary()); if(!dict) { std::this_thread::sleep_for(std::chrono::microseconds(200)); continue; }
            elist= Read<uintptr_t>(dict + PlayerOffsets::DictValues()); if(!elist) { std::this_thread::sleep_for(std::chrono::microseconds(200)); continue; }
            break;
        }

        // ── TRANSIENT FAILURE HANDLING ──────────────────────
        if(!GF || !MM || !elist){
            if(g_ChainFailStart == 0) g_ChainFailStart = NowMs();
            g_ChainFailStreak++;

            if(g_ChainFailStreak < CHAIN_FAIL_LIMIT){
                // FOV/ESP blink fix: do NOT republish. Last good frame
                // (entities + FOV circle state) stays on screen.
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
                continue;
            }

            // Sustained failure only — still avoid forced empty FOV frame.
            // Tracker TTL will drain entries naturally; heartbeat recovers.
            g_EspAlive.store(false);
            ResetSessionState();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        // Chain read succeeded → heartbeat update
        g_LastGoodScanMs.store(NowMs(), std::memory_order_release);

        if(g_ChainFailStart != 0){
            uint64_t outage = NowMs() - g_ChainFailStart;
            if(outage > 60ULL){
                uint64_t nowMs = NowMs();
                for(auto& kv : g_Tracked) kv.second.lastSeenMs = nowMs;
            }
            g_ChainFailStart = 0;
        }
        g_ChainFailStreak = 0;
        g_EspAlive.store(true);

        // ── MATCH TRANSITION → FULL RESET ───────────────────
        if(g_LastMatchState != -1 && g_LastMatchState != matchState){
            int prev = g_LastMatchState;
            g_LastMatchState = matchState;
            // Leaving a live match OR entering a new one → wipe session
            if(prev == 1 || matchState == 1){
                ResetSessionState();
                g_LastMatchState = matchState;
                g_LastGoodScanMs.store(NowMs(), std::memory_order_release);
                if(matchState == 1){
                    // re-resolve chain next loop
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
            }
        } else {
            g_LastMatchState = matchState;
        }

        if(matchState != 1){
            ClearAimTargets();
            // still publish empty-ish FOV so UI stays alive
        }
        if(g_TrueLocalPlayer == 0 && matchState == 1 && lp != 0) g_TrueLocalPlayer = lp;

        uintptr_t originalLP = lp;
        {std::lock_guard<std::mutex>lk(g_SA_mtx); g_SA_lp = originalLP;}
        IxSyncSilentPercentFromChestRate();

        int localHP = originalLP ? ReadLocalPlayerHP(originalLP) : 0;
        g_LocalPlayerIsDead = (!originalLP) || (localHP <= 0);

        // ── CAMERA ──────────────────────────────────────────
        D3DMatrix mx;
        bool camOK = false;

        if(originalLP){
            auto FC = Read<uintptr_t>(originalLP+PlayerOffsets::FollowCamera());
            if(FC){
                auto CA = Read<uintptr_t>(FC+PlayerOffsets::CameraStep1());
                if(CA){
                    auto IC = Read<uintptr_t>(CA+PlayerOffsets::CameraStep2());
                    if(IC){
                        D3DMatrix ownMX = Read<D3DMatrix>(IC+PlayerOffsets::ViewMatrix());
                        if(ownMX._44 != 0.0f && std::isfinite(ownMX._44)){
                            bool accept = true;
                            if(g_LocalPlayerIsDead){
                                if(g_HasPrevOwn &&
                                   ownMX._41 == g_PrevOwnMX._41 &&
                                   ownMX._42 == g_PrevOwnMX._42 &&
                                   ownMX._43 == g_PrevOwnMX._43){
                                    if(++g_OwnFrozen >= 6) accept = false;
                                } else {
                                    g_OwnFrozen = 0;
                                }
                            } else {
                                g_OwnFrozen = 0;
                            }
                            g_PrevOwnMX  = ownMX;
                            g_HasPrevOwn = true;

                            if(accept){
                                mx           = ownMX;
                                camOK        = true;
                                g_LastGoodMX = ownMX;
                                g_LastGoodIC = IC;
                                g_HasLastCam = true;
                            }
                        }
                    }
                }
            }
        }
        if(!g_LocalPlayerIsDead) g_OwnFrozen = 0;

        auto TryEntityCam = [&](uintptr_t ent)->bool{
            if(!ent || ent == originalLP) return false;
            auto sFC = Read<uintptr_t>(ent + PlayerOffsets::FollowCamera());
            if(!sFC) return false;
            auto sCA = Read<uintptr_t>(sFC + PlayerOffsets::CameraStep1());
            if(!sCA) return false;
            auto sIC = Read<uintptr_t>(sCA + PlayerOffsets::CameraStep2());
            if(!sIC) return false;
            auto sMx = Read<D3DMatrix>(sIC + PlayerOffsets::ViewMatrix());
            if(sMx._44 == 0.0f || !std::isfinite(sMx._44)) return false;
            mx = sMx; lp = ent; camOK = true;
            g_LastGoodMX = sMx; g_LastGoodIC = sIC; g_HasLastCam = true;
            return true;
        };

        if(!camOK && elist){
            if(g_CamEnt && TryEntityCam(g_CamEnt)){
                // sticky
            } else {
                g_CamEnt = 0;
                for(int ci=0; ci<32 && !camOK; ci++){
                    uintptr_t ent = Read<uintptr_t>(elist + ci * PlayerOffsets::EntityStride());
                    if(TryEntityCam(ent)){ g_CamEnt = ent; break; }
                }
            }
        }
        if(!g_LocalPlayerIsDead) g_CamEnt = 0;

        if(!camOK && g_HasLastCam){
            mx    = g_LastGoodMX;
            camOK = true;
        }

        if(!camOK){
            if(g_HasLastCam){
                mx = g_LastGoodMX;
                camOK = true;
            } else {
                ClearAimTargets();
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
                continue;
            }
        }

        Vector3 myPos = {0,0,0};
        if(originalLP){
            myPos = NodePos(GetPlayerHeadTF(originalLP));
            if(VecZero(myPos)) myPos = NodePos(GetPlayerChestTF(originalLP));
        }

        uintptr_t bestE_SA  = 0;
        uintptr_t bestE_AB  = 0;
        Vector3   bestSAPos = {0,0,0};
        Vector3   bestABPos = {0,0,0};
        float     bestSADist= FLT_MAX;
        float     bestABDist= FLT_MAX;
        bool      hasSA     = false;
        bool      hasAB     = false;
        int       bestIdx   = -1;
        int       eidx      = 0;

        for(int i=0; i<1000 && buf.count<MAX_ESP; i++){
            uintptr_t e = Read<uintptr_t>(elist + i*8);
            if(!e || e==originalLP) { eidx++; continue; }
            if(g_TrueLocalPlayer && e==g_TrueLocalPlayer) { eidx++; continue; }

            auto AM = Read<uintptr_t>(e+PlayerOffsets::AvatarManager());  if(!AM){ eidx++; continue; }
            auto UA = Read<uintptr_t>(AM+PlayerOffsets::AvatarSimple());  if(!UA){ eidx++; continue; }
            auto UD = Read<uintptr_t>(UA+PlayerOffsets::UmaData());       if(!UD){ eidx++; continue; }

            if(Read<bool>(UD+PlayerOffsets::UmaDataDead())){ g_Tracked.erase(e); eidx++; continue; }
            if(Read<bool>(e+PlayerOffsets::IsDead())){ g_Tracked.erase(e); eidx++; continue; }

            if(!g_LocalPlayerIsDead){
                if(!Read<bool>(UA+PlayerOffsets::IsVisible())){ eidx++; continue; }
            }

            bool knocked = false;
            auto KP = Read<uintptr_t>(e+PlayerOffsets::Status());
            if(KP){
                auto md = Read<uintptr_t>(KP+PlayerOffsets::StatusSub());
                if(md && Read<int>(md+PlayerOffsets::StatusState())==8) knocked = true;
            }
            if(knocked){ g_Tracked.erase(e); eidx++; continue; }

            auto hpC = ReadChain(e,{PlayerOffsets::HealthLink1(),PlayerOffsets::HealthLink2(),PlayerOffsets::HealthLink3()});
            int rawHP = 0;
            if(hpC) rawHP = Read<int>(hpC+PlayerOffsets::Health());
            else if(g_LocalPlayerIsDead) rawHP = 1;
            else { eidx++; continue; }

            if(rawHP <= 0){ g_Tracked.erase(e); eidx++; continue; }
            if(rawHP < 0) rawHP = 0;

            Vector3 EH = NodePos(GetPlayerHeadTF(e));
            Vector3 EF = NodePos(GetPlayerPeTF(e));
            if(VecZero(EH) || VecZero(EF)){ eidx++; continue; }

            float dist = 0.f;
            if(!VecZero(myPos)){
                float dx=myPos.X-EH.X, dy=myPos.Y-EH.Y, dz=myPos.Z-EH.Z;
                dist = sqrtf(dx*dx + dy*dy + dz*dz);
            }

            int   hp  = ImClamp(rawHP, 0, 200);
            char ename[64]={}; bool isBot=false;
            ReadEnemyName(e, ename, isBot);

            EspEntry& en = buf.entries[buf.count];
            en.id=e;
            en.wHead=EH; en.wFeet=EF;
            en.dist=dist; en.hp=hp;
            en.knocked=knocked; en.isBot=isBot;
            strncpy(en.name, ename, 63); en.name[63]=0;

            if(showSkeleton){
                auto Wb=[&](uintptr_t o)->Vector3{ return BonePos(e,o); };
                en.wBones[BONE_HEAD ]=Wb(BoneOffsets::Head());
                en.wBones[BONE_CHEST]=Wb(BoneOffsets::Chest());
                en.wBones[BONE_HIP  ]=Wb(BoneOffsets::Hip());
                en.wBones[BONE_LSH  ]=Wb(BoneOffsets::LeftShoulder());
                en.wBones[BONE_RSH  ]=Wb(BoneOffsets::RightShoulder());
                en.wBones[BONE_LHAND]=Wb(BoneOffsets::LeftHand());
                en.wBones[BONE_RHAND]=Wb(BoneOffsets::RightHand());
                en.wBones[BONE_LFOOT]=Wb(BoneOffsets::LeftAnkle());
                en.wBones[BONE_RFOOT]=Wb(BoneOffsets::RightAnkle());
                en.skelValid=true;
            } else {
                for(int b=0;b<BONE_COUNT;b++) en.wBones[b]=Vector3::Zero();
                en.skelValid=false;
            }

            if(fovRadius > 0.f && !knocked){
                Vector3 bonePos;
                /* Body Kill threshold logic:
                 *   - BodyKill ON  + threshold NOT yet reached  → aim at HEAD (like Aim Silent)
                 *   - BodyKill ON  + threshold reached          → aim at BODY (chest/hip)
                 *   - BodyKill OFF                              → normal branch
                 * BodyKillThresholdReached() only reads HP — never writes. */
                if(BodyKill && BodyKillThresholdReached(e))
                    bonePos = BodyKillBone(e);                /* threshold reached → body */
                else if(BodyKill || BrutalBT || AimSilent)
                    bonePos = HwbtHeadWorld(e);               /* below threshold or Aim Silent/Brutal → HEAD */
                else if(HardwareBT)
                    bonePos = SilentAimBone(e);               /* Hardware BT (Head only now) */
                else
                    bonePos = HwbtHeadWorld(e);

                if(!VecZero(bonePos)){
                    auto sc = W2S(mx, bonePos);
                    float dx = sc.X-sw2, dy = sc.Y-sh2;
                    float sd = sqrtf(dx*dx+dy*dy);
                    if(sd <= fovRadius){
                        if((AimSilent || BrutalBT || HardwareBT || BodyKill) && sd < bestSADist){
                            bestSADist = sd;
                            bestE_SA   = e;
                            bestSAPos  = bonePos;
                            bestIdx    = eidx;
                            hasSA      = true;
                        }
                        // [REMOVED] AimLock / Aimbot from this filter — both features removed.
                        if((AimMagnet || DownPlayer) && sd < bestABDist){
                            bestABDist = sd;
                            bestE_AB   = e;
                            bestABPos  = bonePos;
                            if(!hasSA) bestIdx = eidx;
                            hasAB = true;
                        }
                    }
                }
            }

            buf.count++;
            eidx++;
        }

        bool hasAny          = hasSA || hasAB;
        uintptr_t lockTarget = bestE_SA ? bestE_SA : bestE_AB;
        buf.fovHasTarget     = hasAny;
        g_CurrentBestTarget  = lockTarget;
        g_HasAimTarget       = hasAny;
        currentTargetIndex   = bestIdx;

        {
            std::lock_guard<std::mutex>lk(g_SA_mtx);
            if((AimSilent || BrutalBT || HardwareBT || BodyKill) && bestE_SA){
                    g_CurrentBestTarget = bestE_SA;
                }
                if((AimSilent || BrutalBT || HardwareBT || BodyKill) && bestE_SA)
                g_SA_tgt = bestSAPos;
            else
                g_SA_tgt = Vector3{0,0,0};
        }
        {
            std::lock_guard<std::mutex>lk(g_AB_mtx);
            /* [REMOVED] Aim Lock / Aimbot — this was the LIVE raw-write path
             * (Write<uintptr_t>(bestE_AB + AimCollider(), HC)) that actually
             * fired every scan tick regardless of the HWBP/RT-Hook/Paradise
             * protection added to UpdateAimLockNewThread/UpdateAimbotFast —
             * those threads' protected paths were never the real trigger.
             * AimLock and Aimbot are force-cleared to false everywhere else
             * (main.cpp, SourceMenu.h) so this branch is dead, but the write
             * itself is removed here too for defense-in-depth. */
            g_AB_tgt = {0,0,0};
            g_AB_lp  = 0;
        }

        /* ── AIM MAGNET + DOWNPLAYER + EXPLOITS ── */
        {
            uintptr_t magTgt = bestE_SA ? bestE_SA : (bestE_AB ? bestE_AB : 0);
            if (!magTgt) magTgt = lockTarget;
            bool firing = false;
            if (originalLP){
                /* IsFiring is bool @ 0x850 — read byte, not int-only */
                uint8_t fb = Read<uint8_t>(originalLP + PlayerOffsets::IsFiring());
                int     fi = Read<int>(originalLP + PlayerOffsets::IsFiring());
                firing = (fb != 0) || (fi > 0);
            }
            Vector3 camPos = Vector3::Zero();
            if (originalLP) {
                /* data.cpp uses MainCamera world pos for magnet fire ray */
                uintptr_t camN = GetPlayerMainCamera(originalLP);
                if (camN) camPos = NodePos(camN);
                if (VecZero(camPos))
                    camPos = BonePos(originalLP, BoneOffsets::Head());
                if (VecZero(camPos))
                    camPos = BonePos(originalLP, BoneOffsets::Chest());
            }
            if ((AimMagnet || DownPlayer) && magTgt && originalLP && !VecZero(camPos) && firing) {
                MagnetLogic::ProcessMagnet(AimMagnet, magTgt, mx, camPos, firing, originalLP);
                if (DownPlayer) {
                    uintptr_t tf = MagnetLogic::ResolveWriteTransform(GetPlayerPeTF(magTgt));
                    if (tf) {
                        Vector3 tp = Read<Vector3>(tf + 0x90);
                        Vector3 fireDir = {mx._13, mx._23, mx._33};
                        float fl = sqrtf(fireDir.X*fireDir.X+fireDir.Y*fireDir.Y+fireDir.Z*fireDir.Z);
                        if (fl > 0.001f) {
                            fireDir.X/=fl; fireDir.Y/=fl; fireDir.Z/=fl;
                            Vector3 toE = {tp.X-camPos.X, tp.Y-camPos.Y, tp.Z-camPos.Z};
                            float proj = toE.X*fireDir.X + toE.Y*fireDir.Y + toE.Z*fireDir.Z;
                            if (proj < 1.f) proj = 1.f;
                            float idealY = camPos.Y + fireDir.Y * proj;
                            float vShift = idealY - tp.Y;
                            if (vShift > 0.f) vShift = 0.f;
                            if (vShift < -2.9f) vShift = -2.9f;
                            tp.Y += vShift;
                            Write<Vector3>(tf + 0x90, tp);
                        }
                    }
                }
            } else {
                MagnetLogic::ProcessMagnet(false, 0, mx, Vector3::Zero(), false, 0);
            }
            if (originalLP)
                IxUpdateGhost(originalLP);
                IxUpdateNoRecoil(originalLP);
                IxUpdateNoReload(originalLP);
                ExploitX::OnEspTick(originalLP, magTgt, mx, firing);
        }

        uint64_t nowMs = NowMs();
        for(int i=0; i<buf.count; i++){
            TrackedEntry& t = g_Tracked[buf.entries[i].id];
            t.data       = buf.entries[i];
            t.lastSeenMs = nowMs;
        }
        for(auto it = g_Tracked.begin(); it != g_Tracked.end(); ){
            if(nowMs - it->second.lastSeenMs > ENTRY_TTL_MS) it = g_Tracked.erase(it);
            else ++it;
        }

        EspBuffer out;
        out.count        = 0;
        out.sw           = sw;
        out.sh           = sh;
        out.vm           = mx;
        out.vmValid      = true;
        out.camIC        = g_LastGoodIC;
        out.hasAimLine   = buf.hasAimLine;
        out.aimWorld     = buf.aimWorld;
        out.hasFov       = buf.hasFov;
        out.fovRadius    = buf.fovRadius;
        out.fovHasTarget = buf.fovHasTarget;
        for(auto const& kv : g_Tracked){
            if(out.count >= MAX_ESP) break;
            out.entries[out.count++] = kv.second.data;
        }

        PublishEspBuffer(out);
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}

// ============================================================
//  SILENT AIM THREAD  (also serves BrutalBT)
// ============================================================
void UpdateSilentAimFast(){
    /* Aim Silent / Body Kill — redirects the actual bullet via SilentAimDir.
     * Body Kill drives this thread too, but only switches to body once the
     * enemy's real HP (read-only, never written) drops to/below the slider
     * value — see BodyKillThresholdReached() below. Before that point the
     * bullet lands on the head, same as plain Aim Silent.
     * Do not touch for Hardware BT (separate thread/logic). */
    while(silentThreadRunning){
        if(!ix_feat_aim() || !ix_rt_ready() || !(AimSilent || BodyKill)){
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        uintptr_t lp = 0;
        {
            std::lock_guard<std::mutex> lk(g_SA_mtx);
            lp = g_SA_lp;
        }
        uintptr_t currentTarget = g_CurrentBestTarget;
        if(!lp || !currentTarget){
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        uintptr_t aimingInfo = Read<uintptr_t>(lp + PlayerOffsets::HitObjInfo());
        if(!aimingInfo){
            std::this_thread::yield();
            continue;
        }

        /* Body Kill threshold: only redirect to body AFTER enemy HP drops to/below slider.
         * Before threshold → aim at HEAD (same as Aim Silent).
         * BodyKillThresholdReached() only reads HP, never writes. */
        bool bodyPhase = BodyKill && BodyKillThresholdReached(currentTarget);
        uintptr_t targetNode = bodyPhase
            ? Read<uintptr_t>(currentTarget + BoneOffsets::Chest())
            : Read<uintptr_t>(currentTarget + BoneOffsets::Head());
        if(bodyPhase && !targetNode)
            targetNode = Read<uintptr_t>(currentTarget + BoneOffsets::Hip());   // chest missing → hip fallback

        if(targetNode){
            Vector3 targetPos = NodePos(targetNode);
            if(!(targetPos.X == 0.f && targetPos.Y == 0.f && targetPos.Z == 0.f)){
                if(!bodyPhase) targetPos.Y += 0.04f;   // small skull lift — head shots only (pre-threshold)
                Vector3 startPos = Read<Vector3>(aimingInfo + PlayerOffsets::AmmoBase());
                Vector3 dir;
                dir.X = targetPos.X - startPos.X;
                dir.Y = targetPos.Y - startPos.Y;
                dir.Z = targetPos.Z - startPos.Z;
                Write<Vector3>(aimingInfo + PlayerOffsets::SilentAimDir(), dir);
            }
        }
        std::this_thread::yield();
    }
}

/* GetRotationToTheLocation — provided by bool.h */

// ============================================================
//  Hook-based aim injection state
//  RT Hook mode (driver==13): kernel PTE hook intercepts the
//  game's own rotation-setter; zero external writes visible.
//  Fallback (all other drivers): HWBP write-intercept on the
//  Rotation field + timed memory patch cloaked by write-only-
//  on-change and randomised intervals.
// ============================================================
namespace AimHookState {
    static std::atomic<bool>       ab_hook_armed{false};
    static std::atomic<uintptr_t>  ab_hook_addr{0};
    static std::atomic<bool>       al_hwbp_armed{false};
    static std::atomic<uintptr_t>  al_hwbp_addr{0};

    // XOR-shift per-feature RNGs — seeded with distinct constants
    static uint32_t abRng = 0xC1D2E3F4u;
    static uint32_t alRng = 0xF1E2D3C4u;

    inline int abJitter() {
        abRng ^= abRng << 13; abRng ^= abRng >> 17; abRng ^= abRng << 5;
        return 8 + (int)(abRng % 9u);   // 8–16 ms
    }
    inline int alJitter() {
        alRng ^= alRng << 13; alRng ^= alRng >> 17; alRng ^= alRng << 5;
        return 3 + (int)(alRng % 5u);   // 3–7 ms
    }

    // ── RT Hook helpers ────────────────────────────────────────
    // Push a Quaternion into the hooked rotation slot via kernel PTE.
    // Packs X/Y/Z/W as the four fp-reg doubles the hook monitors
    // (register layout: d8=X, d9=Y, d10=Z, d11=W per game ABI).
    static bool RthSetRotation(RtHookDriver* rth, const Quaternion& q) {
        HOOK_SET_ROT cfg{};
        // rot[2]=1 means "replace fp-reg values on next hit"
        cfg.rot[2] = 1;
        // We pack the four floats as raw uint32 bit patterns into
        // stack_rot[0..7]: pairs of (reg_index, bit_pattern)
        // Registers 32..35 are d8..d11 in the kernel driver's numbering.
        cfg.stack_rot[0] = 32u;  cfg.stack_rot[1] = RtHookDriver::float_bits(q.X);
        cfg.stack_rot[2] = 33u;  cfg.stack_rot[3] = RtHookDriver::float_bits(q.Y);
        cfg.stack_rot[4] = 34u;  cfg.stack_rot[5] = RtHookDriver::float_bits(q.Z);
        cfg.stack_rot[6] = 35u;  cfg.stack_rot[7] = RtHookDriver::float_bits(q.W);
        cfg.stack_count  = 8;
        return rth->HookSetRot(&cfg);
    }

    // Attach the PTE hook to a rotation-setter inside il2cpp.
    // We hook the Transform::set_rotation_Injected stub which is
    // called by the engine every frame to apply ViewRotation.
    // Address is resolved once via AOB scan and cached.
    static uintptr_t ResolveRotationSetter() {
        static std::atomic<uintptr_t> cached{0};
        uintptr_t v = cached.load(std::memory_order_relaxed);
        if (v) return v;
        // Pattern: Transform::set_rotation_Injected prologue (arm64)
        // STP x29,x30,[sp,#-0x10]!  MOV x29,sp  LDR x0,[x0,#offset]
        // This is stable across BGMI/PUBGM versions; the offset byte varies
        // but the surrounding opcodes do not.
        static const char kPat[] =
            "FD 7B BF A9 FD 03 00 91 ?? ?? ?? ?? ?? ?? ?? ?? "
            "?? ?? ?? ?? ?? ?? ?? ?? 08 00 40 F9";
        std::string why;
        uintptr_t addr = ix_aob::scan(g_driver->GetPid(),
                                      targetaLibName, ix_aob::parse_pattern(kPat),
                                      0, &why);
        if (addr) cached.store(addr, std::memory_order_relaxed);
        return addr;
    }

    // ── HWBP helpers (fallback path) ───────────────────────────
    // Set a hardware watchpoint (write type, 8 bytes) on `rot_addr`
    // and configure the kernel to override the written value with our
    // quaternion by patching the GP registers at the breakpoint hit.
    // We use AArch64 GP regs x0–x3 which hold the four float args in
    // the AAPCS calling convention for set_rotation_Injected.
    static bool HwbpArmRotation(RtHookDriver* rth, uintptr_t rot_addr,
                                 const Quaternion& q) {
        HW_BP_INFO info{};
        info.pid  = g_driver ? g_driver->GetPid() : 0;
        info.addr = rot_addr;
        info.type = HW_BP_TYPE_W;
        info.len  = 4;   // 4-byte granule (Quaternion X — first field)

        // Patch the four float args: x0=X x1=Y x2=Z x3=W
        info.is_write_gp_regs = true;
        info.gp_reg_count     = 4;
        info.gp_reg_indices[0] = 0;  info.gp_reg_values[0] = RtHookDriver::float_bits(q.X);
        info.gp_reg_indices[1] = 1;  info.gp_reg_values[1] = RtHookDriver::float_bits(q.Y);
        info.gp_reg_indices[2] = 2;  info.gp_reg_values[2] = RtHookDriver::float_bits(q.Z);
        info.gp_reg_indices[3] = 3;  info.gp_reg_values[3] = RtHookDriver::float_bits(q.W);

        return rth->UpdateAndEnableHwBp(&info);
    }

    // Arm HWBP on AimCollider field (write type) to override the value
    // the game writes with our head-collider pointer.
    static bool HwbpArmCollider(RtHookDriver* rth, uintptr_t collider_addr,
                                 uintptr_t head_collider_val) {
        HW_BP_INFO info{};
        info.pid  = g_driver ? g_driver->GetPid() : 0;
        info.addr = collider_addr;
        info.type = HW_BP_TYPE_W;
        info.len  = 8;   // pointer size

        // Replace x0 (the value being written) with head collider
        info.is_write_gp_regs = true;
        info.gp_reg_count     = 1;
        info.gp_reg_indices[0] = 0;
        info.gp_reg_values[0]  = (uint64_t)head_collider_val;

        return rth->UpdateAndEnableHwBp(&info);
    }
    // ── Paradise HWBP helpers ──────────────────────────────────────────
    // Arms a Paradise hardware write-breakpoint on a rotation field.
    // paradise_bp_record.x0–x3 carry the new float values as GP register
    // overrides; the driver overwrites the game's store in-kernel.
    static bool PdHwbpArmRotation(paradise_driver* pd, uintptr_t rot_addr,
                                   const Quaternion& q) {
        paradise_bp_point_config pt{};
        pt.address  = (uint64_t)rot_addr;
        pt.type     = PARADISE_HWBP_WRITE;
        pt.length   = 16;  // Quaternion = 4 floats = 16 bytes
        pt.scope    = PARADISE_HWBP_ALL_THREADS;
        pt.reserved = 0;
        if(!pd->hwbp_set(&pt, 1)) return false;

        // Build an override record: pack x, y, z, w into GP regs x0–x3
        // (ARM64 ABI: float args arrive in x0–x3 as bit-cast uint32).
        paradise_bp_record rec{};
        uint32_t bx, by, bz, bw;
        __builtin_memcpy(&bx, &q.X, 4);
        __builtin_memcpy(&by, &q.Y, 4);
        __builtin_memcpy(&bz, &q.Z, 4);
        __builtin_memcpy(&bw, &q.W, 4);
        rec.x0 = (uint64_t)bx;
        rec.x1 = (uint64_t)by;
        rec.x2 = (uint64_t)bz;
        rec.x3 = (uint64_t)bw;
        pd->hwbp_set_record(0, 0, &rec);
        return true;
    }

    static bool PdHwbpArmCollider(paradise_driver* pd, uintptr_t collider_addr,
                                   uintptr_t head_collider_val) {
        paradise_bp_point_config pt{};
        pt.address  = (uint64_t)collider_addr;
        pt.type     = PARADISE_HWBP_WRITE;
        pt.length   = 8;   // pointer = 8 bytes
        pt.scope    = PARADISE_HWBP_ALL_THREADS;
        pt.reserved = 0;
        if(!pd->hwbp_set(&pt, 1)) return false;

        // Override x0 (the pointer the game is writing) with our head-collider
        paradise_bp_record rec{};
        rec.x0 = (uint64_t)head_collider_val;
        pd->hwbp_set_record(0, 0, &rec);
        return true;
    }

    static void PdHwbpDisarm(paradise_driver* pd) {
        if(pd) pd->hwbp_remove();
    }

} // namespace AimHookState

void UpdateAimbotFast(){
    /* Aimbot — hook-based rotation injection.
     *
     * RT Hook driver (mode 13):
     *   Attaches a kernel PTE hook to Transform::set_rotation_Injected
     *   inside libil2cpp.so.  On each game-frame call the kernel patches
     *   the fp-register arguments before they land — the game itself
     *   "wrote" our rotation, so AC memory scanners see nothing external.
     *
     * All other drivers (HWBP path):
     *   Places a hardware write-breakpoint on the player's Rotation field.
     *   The kernel intercepts the game's own write and overrides the GP
     *   register value mid-instruction — same effect, no userspace write.
     *   Falls back to write-on-change + jitter if HWBP is unavailable.
     *
     * Fire-gate, human-speed Nlerp (t 0.08–0.18), and dot-product
     * similarity skip are kept on all paths. */

    // ── one-time hook setup per feature activation ─────────────
    bool hook_init_done = false;

    while(aimbotThreadRunning){
        if(!ix_feat_aim() || !ix_rt_ready() || !Aimbot){
            // Disarm hooks when feature is off
            if(AimHookState::ab_hook_armed.exchange(false)){
                RtHookDriver* rth = g_driver ? g_driver->GetRtHook() : nullptr;
                if(rth) rth->HookDetach();
                else if(g_driver){
                    uintptr_t addr = AimHookState::ab_hook_addr.load();
                    if(addr) g_driver->GetRtHook(); // best-effort disable
                }
                AimHookState::ab_hook_addr.store(0);
            }
            hook_init_done = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        uintptr_t lp = 0;
        Vector3   tgt = {0,0,0};
        {
            std::lock_guard<std::mutex> lk(g_AB_mtx);
            lp  = g_AB_lp;
            tgt = g_AB_tgt;
        }
        if(!lp || VecZero(tgt)){
            std::this_thread::sleep_for(std::chrono::milliseconds(AimHookState::abJitter()));
            continue;
        }

        // Fire-gate: only act while player is shooting
        uint8_t fb = Read<uint8_t>(lp + PlayerOffsets::IsFiring());
        int     fi = Read<int>(lp + PlayerOffsets::IsFiring());
        if(fb == 0 && fi <= 0){
            std::this_thread::sleep_for(std::chrono::milliseconds(AimHookState::abJitter()));
            continue;
        }

        // Camera / eye position for LookRotation
        Vector3 liveCam = Vector3::Zero();
        uintptr_t camNode = GetPlayerMainCamera(lp);
        if(camNode) liveCam = NodePos(camNode);
        if(VecZero(liveCam))
            liveCam = BonePos(lp, BoneOffsets::Head());
        if(VecZero(liveCam)){
            std::this_thread::sleep_for(std::chrono::milliseconds(AimHookState::abJitter()));
            continue;
        }

        Vector3 aimPos = tgt;
        aimPos.Y += 0.05f;

        Quaternion targetRot  = GetRotationToTheLocation(aimPos, 0.0f, liveCam);
        Quaternion currentRot = Read<Quaternion>(lp + PlayerOffsets::Rotation());

        // Human-like smoothing speed 0.08–0.18
        float t = 0.10f;
        if(AimSmooth > 1.f){
            t = 1.f / AimSmooth;
            if(t > 0.18f) t = 0.18f;
            if(t < 0.08f) t = 0.08f;
        }
        Quaternion nextRot = QNlerp(currentRot, targetRot, t);

        // Skip if already close enough (< ~0.26°)
        float dot = currentRot.X*nextRot.X + currentRot.Y*nextRot.Y
                  + currentRot.Z*nextRot.Z + currentRot.W*nextRot.W;
        if(dot < 0.f) dot = -dot;
        if(dot >= 0.9999f){
            std::this_thread::sleep_for(std::chrono::milliseconds(AimHookState::abJitter()));
            continue;
        }

        RtHookDriver* rth = g_driver ? g_driver->GetRtHook() : nullptr;

        if(rth){
            // ── RT Hook path: kernel PTE hook on rotation setter ──
            if(!hook_init_done){
                uintptr_t setter = AimHookState::ResolveRotationSetter();
                if(setter && rth->HookAttach(g_driver->GetPid(), setter)){
                    AimHookState::ab_hook_addr.store(setter);
                    AimHookState::ab_hook_armed.store(true);
                    hook_init_done = true;
                }
            }
            if(AimHookState::ab_hook_armed.load()){
                // Inject via kernel — game's own call path, AC-invisible
                AimHookState::RthSetRotation(rth, nextRot);
            } else {
                // Hook attach failed — fallback write this frame
                Write<Quaternion>(lp + PlayerOffsets::Rotation(), nextRot);
            }
        } else {
            uintptr_t rotAddr = lp + PlayerOffsets::Rotation();
            paradise_driver* pd = g_driver ? g_driver->GetParadise() : nullptr;

            if(pd){
                // ── Paradise HWBP tier: kernel intercepts game's own write ──
                bool need_rearm = (!hook_init_done ||
                                   AimHookState::ab_hook_addr.load() != rotAddr);
                if(need_rearm){
                    // Disarm previous point before re-arming (target may have changed)
                    if(AimHookState::ab_hook_armed.load())
                        AimHookState::PdHwbpDisarm(pd);
                    if(AimHookState::PdHwbpArmRotation(pd, rotAddr, nextRot)){
                        AimHookState::ab_hook_addr.store(rotAddr);
                        AimHookState::ab_hook_armed.store(true);
                        hook_init_done = true;
                    } else {
                        // Paradise arm failed — plain write this frame
                        Write<Quaternion>(lp + PlayerOffsets::Rotation(), nextRot);
                    }
                } else {
                    // Breakpoint already armed — update override values in-place
                    AimHookState::PdHwbpArmRotation(pd, rotAddr, nextRot);
                }
            } else {
                // ── Fallback: write-on-change + jitter (all other drivers) ──
                if(!hook_init_done || AimHookState::ab_hook_addr.load() != rotAddr){
                    AimHookState::ab_hook_addr.store(rotAddr);
                    hook_init_done = true;
                }
                Write<Quaternion>(lp + PlayerOffsets::Rotation(), nextRot);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(AimHookState::abJitter()));
    }
}

// ============================================================
//  AIM LOCK NEW — hook + HWBP collider intercept
//  RT Hook: HWBP write-intercept on AimCollider; game's own
//  write gets its value replaced in-kernel — no external write.
//  Fallback: write-on-change + jitter (previous approach).
// ============================================================
void UpdateAimLockNewThread(){
    /* AimLock — kernel hardware-breakpoint intercept on AimCollider.
     *
     * When RT Hook driver is active:
     *   Places a HW_BP_TYPE_W breakpoint on the enemy's AimCollider
     *   field.  Every time the game writes that field the kernel fires,
     *   intercepts the store, and overrides the value with our head-
     *   collider pointer — the game "wrote" it itself, externally
     *   invisible to AC memory monitors.
     *
     * All other drivers:
     *   Write-on-change + jitter (previous approach, kept as fallback). */

    static uintptr_t s_lastCollider  = 0;
    static uintptr_t s_lastEnemy     = 0;
    static uintptr_t s_lastHwbpAddr  = 0;

    // Helper lambda to disarm both RT Hook and Paradise HWBPs
    auto disarm_all = [&](){
        RtHookDriver*   rth = g_driver ? g_driver->GetRtHook()   : nullptr;
        paradise_driver* pd = g_driver ? g_driver->GetParadise() : nullptr;
        if(rth && s_lastHwbpAddr)
            rth->DisableHwBp(g_driver->GetPid(), s_lastHwbpAddr);
        if(pd && s_lastHwbpAddr)
            AimHookState::PdHwbpDisarm(pd);
        s_lastHwbpAddr = 0;
        s_lastCollider = 0;
        s_lastEnemy    = 0;
    };

    while(aimlockNewThreadRunning){
        if(!ix_feat_aim() || !ix_rt_ready() || !AimLock){
            disarm_all();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        uintptr_t enemy = g_CurrentBestTarget;
        if(!enemy){
            disarm_all();
            std::this_thread::sleep_for(std::chrono::milliseconds(AimHookState::alJitter()));
            continue;
        }

        uintptr_t HedColider = Read<uintptr_t>(enemy + PlayerOffsets::ColliderNode());
        if(!HedColider){
            std::this_thread::sleep_for(std::chrono::milliseconds(AimHookState::alJitter()));
            continue;
        }

        RtHookDriver* rth = g_driver ? g_driver->GetRtHook() : nullptr;

        if(rth){
            // ── HWBP path: intercept AimCollider write in-kernel ──
            uintptr_t colliderField = enemy + PlayerOffsets::AimCollider();

            bool need_rearm = (enemy     != s_lastEnemy)    ||
                              (HedColider != s_lastCollider)  ||
                              (s_lastHwbpAddr == 0);

            if(need_rearm){
                // Disarm old breakpoint first
                if(s_lastHwbpAddr)
                    rth->DisableHwBp(g_driver->GetPid(), s_lastHwbpAddr);

                // Arm new breakpoint: intercept write to AimCollider,
                // override written value with head-collider pointer
                if(AimHookState::HwbpArmCollider(rth, colliderField, HedColider)){
                    s_lastHwbpAddr  = colliderField;
                    s_lastEnemy     = enemy;
                    s_lastCollider  = HedColider;
                } else {
                    // HWBP arm failed — fall back to direct write
                    Write<uintptr_t>(colliderField, HedColider);
                    s_lastHwbpAddr = 0;
                }
            }
            // HWBP is armed — no active write needed; kernel handles it
        } else {
            uintptr_t colliderField = enemy + PlayerOffsets::AimCollider();
            paradise_driver* pd = g_driver ? g_driver->GetParadise() : nullptr;

            if(pd){
                // ── Paradise HWBP tier: kernel intercepts AimCollider write ──
                bool need_rearm = (enemy     != s_lastEnemy)   ||
                                  (HedColider != s_lastCollider)||
                                  (s_lastHwbpAddr == 0);
                if(need_rearm){
                    if(s_lastHwbpAddr)
                        AimHookState::PdHwbpDisarm(pd);
                    if(AimHookState::PdHwbpArmCollider(pd, colliderField, HedColider)){
                        s_lastHwbpAddr = colliderField;
                        s_lastEnemy    = enemy;
                        s_lastCollider = HedColider;
                    } else {
                        // Paradise arm failed — direct write this frame
                        Write<uintptr_t>(colliderField, HedColider);
                        s_lastHwbpAddr = 0;
                    }
                } else {
                    // Update override value for new target pointer
                    if(HedColider != s_lastCollider){
                        AimHookState::PdHwbpArmCollider(pd, colliderField, HedColider);
                        s_lastCollider = HedColider;
                    }
                }
            } else {
                // ── Fallback: write-on-change + jitter (all other drivers) ──
                if(HedColider != s_lastCollider){
                    Write<uintptr_t>(colliderField, HedColider);
                    s_lastCollider = HedColider;
                    s_lastEnemy    = enemy;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(AimHookState::alJitter()));
    }
}


// ============================================================
//  HARDWARE BT THREAD (Body / Legit) — separate from Aim Silent
// ============================================================
static bool hwbtThreadRunning = false;

void UpdateHardwareBTFast(){
    while(hwbtThreadRunning){
        if(!ix_feat_aim() || !ix_rt_ready() || !HardwareBT){
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        /* Body/Legit only — if Head mode, still allow head via SilentAimBone */
        uintptr_t lp = 0;
        {
            std::lock_guard<std::mutex> lk(g_SA_mtx);
            lp = g_SA_lp;
        }
        uintptr_t currentTarget = g_CurrentBestTarget;
        if(!lp || !currentTarget){
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        uintptr_t aimingInfo = Read<uintptr_t>(lp + PlayerOffsets::HitObjInfo());
        if(!aimingInfo){
            std::this_thread::yield();
            continue;
        }
        Vector3 targetPos = SilentAimBone(currentTarget);
        if(!(targetPos.X == 0.f && targetPos.Y == 0.f && targetPos.Z == 0.f)){
            if (chestRate == 1)      targetPos.Y += 0.04f;
            else if (chestRate == 2) targetPos.Y -= 0.08f;
            Vector3 startPos = Read<Vector3>(aimingInfo + PlayerOffsets::AmmoBase());
            Vector3 dir;
            dir.X = targetPos.X - startPos.X;
            dir.Y = targetPos.Y - startPos.Y;
            dir.Z = targetPos.Z - startPos.Z;
            Write<Vector3>(aimingInfo + PlayerOffsets::SilentAimDir(), dir);
        }
        std::this_thread::yield();
    }
}

void StartHardwareBTThread(){
    if(!hwbtThreadRunning){
        hwbtThreadRunning = true;
        std::thread(UpdateHardwareBTFast).detach();
    }
}
void StopHardwareBTThread(){ hwbtThreadRunning = false; }

void StartEspThread(){
    if(!espThreadRunning){
        espThreadRunning = true;
        std::thread(EspThreadFunc).detach();
    }
}
void StopEspThread(){ espThreadRunning = false; }

void StartSilentAimThread(){
    if(!silentThreadRunning){
        silentThreadRunning = true;
        std::thread(UpdateSilentAimFast).detach();
    }
}
void StopSilentAimThread(){ silentThreadRunning = false; }

void StartAimbotThread(){
    if(!aimbotThreadRunning){
        aimbotThreadRunning = true;
        std::thread(UpdateAimbotFast).detach();
    }
}
void StopAimbotThread(){ aimbotThreadRunning = false; }

void StartAimLockNewThread(){
    if(!aimlockNewThreadRunning){
        aimlockNewThreadRunning = true;
        std::thread(UpdateAimLockNewThread).detach();
    }
}
void StopAimLockNewThread(){ aimlockNewThreadRunning = false; }

void DrawESP(float,float,uintptr_t){}
void AimbotMenu(float,float,uintptr_t){}

// ============================================================
//  FOV CIRCLE
// ============================================================
static void DrawFOVCircle(ImDrawList* dl,float sw,float sh,float radius,bool hasTarget){
    if(radius<2.f) return;
    ImU32 col = hasTarget ? IM_COL32(255,50,50,255) : IM_COL32(200,200,200,180);
    dl->AddCircle({sw/2.f, sh/2.f}, radius, col, 64, 2.f);
}

// ============================================================
//  DRAW ALL
// ============================================================

static inline void DrawExploitFloatUI(){
    /* Large movable real-style toggles — drag the TOP BAR to move */
    const float panelW = 280.f;
    const float dragH  = 28.f;
    const float btnH   = 78.f;
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize;

    auto drawOne = [&](const char* winId, const char* label, bool* value,
                       bool show, float defX, float defY,
                       ImVec4 onCol){
        if(!show) return;
        ImGui::SetNextWindowPos(ImVec2(defX, defY), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.94f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 8.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.f);
        if(ImGui::Begin(winId, nullptr, flags)){
            /* === DRAG BAR (hold & move) === */
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##drag", ImVec2(panelW, dragH));
            bool dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
            if(dragging){
                ImVec2 d = ImGui::GetIO().MouseDelta;
                ImVec2 wp = ImGui::GetWindowPos();
                ImGui::SetWindowPos(ImVec2(wp.x + d.x, wp.y + d.y));
            }
            ImVec2 p1 = ImVec2(p0.x + panelW, p0.y + dragH);
            dl->AddRectFilled(p0, p1, IM_COL32(40, 40, 48, 220), 10.f);
            /* grip dots */
            for(int i = 0; i < 3; i++){
                float cx = p0.x + panelW * 0.5f - 12.f + i * 12.f;
                float cy = p0.y + dragH * 0.5f;
                dl->AddCircleFilled(ImVec2(cx, cy), 3.2f, IM_COL32(180, 180, 190, 255));
            }
            const char* hint = "drag";
            ImVec2 hs = ImGui::CalcTextSize(hint);
            dl->AddText(ImVec2(p0.x + 8.f, p0.y + (dragH - hs.y) * 0.5f),
                        IM_COL32(140, 140, 150, 255), hint);

            ImGui::Dummy(ImVec2(0, 4.f));

            /* === TOGGLE === */
            bool on = *value;
            ImVec4 col = on ? onCol : ImVec4(0.22f, 0.22f, 0.26f, 1.f);
            ImGui::PushStyleColor(ImGuiCol_Button, col);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4(col.x + 0.08f, col.y + 0.05f, col.z + 0.05f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                ImVec4(col.x + 0.12f, col.y + 0.07f, col.z + 0.07f, 1.f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.f);
            char btnLab[64];
            snprintf(btnLab, sizeof(btnLab), "%s  [%s]", label, on ? "ON" : "OFF");
            if(ImGui::Button(btnLab, ImVec2(panelW, btnH)))
                *value = !*value;
            ImGui::PopStyleVar(1);
            ImGui::PopStyleColor(3);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    };

    if (!ix_feat_exploit() || !ix_rt_ready()) return;
    drawOne("##ix_float_magnet", "pull enemy", &AimMagnet,
            g_ExploitFloatUI, 24.f, 120.f,
            ImVec4(0.92f, 0.12f, 0.18f, 1.f));
    drawOne("##ix_float_ghost", "Ghost Mod", &GhostHack,
            g_ExploitFloatGhost, 24.f, 250.f,
            ImVec4(0.12f, 0.72f, 0.95f, 1.f));
}

void DrawAll(float sw, float sh, uintptr_t lib){
    if (!ix_rt_ready() || !ix_feat_ok()) {
        IxForceDisableCheats();
        return;
    }
    /* Always feed lib/size to ESP/aim threads when runtime ready */
    if (lib) {
        g_EspLib.store(lib);
        g_EspSW.store(sw);
        g_EspSH.store(sh);
    }
    if (ix_feat_exploit()) DrawExploitFloatUI();
    if (!ix_feat_esp()) return;
    /* Master Enable follows any ESP toggle (menu writes g_Cfg → show*) */
    Enable = (showLine || showBox || showSkeleton || showName ||
              showHealth || showDistance || showCount);
    if(!Enable) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if(!dl) return;

    {
        uint64_t lastPub = g_LastPublishMs.load(std::memory_order_acquire);
        if(lastPub != 0 && NowMs() - lastPub > 5000ULL) return;
    }

    if(!g_BufReady.load(std::memory_order_acquire)) return;

    static EspBuffer s_LocalBuf;
    static bool      s_HasLocal     = false;
    static uint64_t  s_LastSession  = (uint64_t)-1;
    static std::map<uintptr_t,int> s_Vis;

    uint64_t curSession = g_SessionId.load(std::memory_order_acquire);
    if(curSession != s_LastSession){
        s_LastSession = curSession;
        s_HasLocal    = false;
        s_Vis.clear();
        s_LocalBuf    = EspBuffer{};
    }

    for(int attempt = 0; attempt < 4; attempt++){
        uint32_t s1 = g_PubSeq.load(std::memory_order_acquire);
        if(s1 & 1u) continue;
        EspBuffer tmp = g_PubBuf;
        std::atomic_thread_fence(std::memory_order_acquire);
        uint32_t s2 = g_PubSeq.load(std::memory_order_acquire);
        if(s1 == s2){
            if(tmp.count > MAX_ESP) tmp.count = MAX_ESP;
            if(tmp.count < 0)       tmp.count = 0;
            s_LocalBuf = tmp;
            s_HasLocal = true;
            break;
        }
    }
    if(!s_HasLocal) return;

    const EspBuffer& buf = s_LocalBuf;
    if(sw < 10.f || sh < 10.f) return;

    if(buf.hasFov) DrawFOVCircle(dl, sw, sh, buf.fovRadius, buf.fovHasTarget);

    if(s_Vis.size() > (size_t)(MAX_ESP * 4)){
        std::map<uintptr_t,int> pruned;
        for(int pi=0; pi<buf.count; pi++){
            auto it = s_Vis.find(buf.entries[pi].id);
            if(it != s_Vis.end()) pruned[it->first] = it->second;
        }
        s_Vis = std::move(pruned);
    }

    D3DMatrix vm   = buf.vm;
    bool      vmOK = buf.vmValid;

    if(buf.camIC > 0x10000ULL && buf.camIC < 0x7FFFFFFFFFFFULL){
        D3DMatrix liveMX = Read<D3DMatrix>(buf.camIC + PlayerOffsets::ViewMatrix());
        bool finite = std::isfinite(liveMX._44) && std::isfinite(liveMX._41) &&
                      std::isfinite(liveMX._42) && std::isfinite(liveMX._43) &&
                      std::isfinite(liveMX._11) && std::isfinite(liveMX._22);
        if(liveMX._44 != 0.0f && finite &&
           fabsf(liveMX._44) < 1e6f && fabsf(liveMX._41) < 1e8f){
            vm   = liveMX;
            vmOK = true;
        }
    }
    if(!vmOK) return;

    for(int i = 0; i < buf.count; i++){
        const EspEntry& e = buf.entries[i];
        float fs = (float)textSize;

        Vector3 sH = W2S(vm, e.wHead);
        Vector3 sF = W2S(vm, e.wFeet);

        bool wasVis = (s_Vis.count(e.id) && s_Vis[e.id] > 0);
        if(!EspOnScreen(sH, sF, sw, sh, wasVis)){ s_Vis[e.id] = 0; continue; }
        s_Vis[e.id] = 1;

        if(sH.Z <= 0.001f || sF.Z <= 0.001f){ s_Vis[e.id] = 0; continue; }

        float rawH = sF.Y - sH.Y;
        if(rawH < 2.f) continue;

        float bh = rawH * 1.12f;
        if(bh < 8.f || bh > sh * 1.2f) continue;
        float bw = bh * 0.45f;
        if(bw < 4.f) bw = 4.f;
        float bx = sH.X - bw * 0.5f;
        float by = sH.Y - bh * 0.08f;
        if(by < -bh) continue;
        if(bx + bw < -20.f || bx > sw + 20.f) continue;
        float hx = sH.X, hy = sH.Y;

        if(showSkeleton && e.skelValid){
            ImVec2 p[BONE_COUNT];
            bool   ok[BONE_COUNT];
            for(int b=0;b<BONE_COUNT;b++){
                Vector3 s = W2S(vm, e.wBones[b]);
                ok[b] = (s.Z > 0.001f) && !VecZero(e.wBones[b]);
                p[b]  = ImVec2(s.X, s.Y);
            }
            constexpr float tk = 2.4f;
            const ImU32 SK = IM_COL32(0,255,80,255);
            auto Ln=[&](int a,int b){
                if(ok[a] && ok[b]) dl->AddLine(p[a], p[b], SK, tk);
            };
            Ln(BONE_HEAD, BONE_CHEST);
            Ln(BONE_CHEST,BONE_LSH);
            Ln(BONE_CHEST,BONE_RSH);
            Ln(BONE_LSH,  BONE_LHAND);
            Ln(BONE_RSH,  BONE_RHAND);
            Ln(BONE_CHEST,BONE_HIP);
            Ln(BONE_HIP,  BONE_LFOOT);
            Ln(BONE_HIP,  BONE_RFOOT);
            if(ok[BONE_CHEST])
                dl->AddCircleFilled(p[BONE_CHEST], 2.4f, SK, 10);
        }

        if(showBox){
            ImU32 bc = BoxCol(e.hp, e.dist, e.knocked);
            {
                ImU32 wc = IM_COL32(255,255,255,255);
                dl->AddRect({bx,by}, {bx+bw,by+bh}, wc, 0, 0, boxThickness);
            }
            if(false && boxStyle==1){
                float cl = bw * 0.25f;
                dl->AddLine({bx,   by   },{bx+cl,   by   },bc,boxThickness);
                dl->AddLine({bx,   by   },{bx,      by+cl},bc,boxThickness);
                dl->AddLine({bx+bw,by   },{bx+bw-cl,by   },bc,boxThickness);
                dl->AddLine({bx+bw,by   },{bx+bw,   by+cl},bc,boxThickness);
                dl->AddLine({bx,   by+bh},{bx+cl,   by+bh},bc,boxThickness);
                dl->AddLine({bx,   by+bh},{bx,      by+bh-cl},bc,boxThickness);
                dl->AddLine({bx+bw,by+bh},{bx+bw-cl,by+bh},bc,boxThickness);
                dl->AddLine({bx+bw,by+bh},{bx+bw,   by+bh-cl},bc,boxThickness);
            } else {
                dl->AddRect({bx,by}, {bx+bw,by+bh}, bc, 0, 0, boxThickness);
            }
        }

        if(showHealth){
            int displayHP = e.knocked ? 0 : e.hp;
            float pct   = ImClamp(displayHP/200.f, 0.f, 1.f);
            float barX  = bx - healthBarWidth - 3.f;
            float barY  = by;
            float barH  = bh;
            float fillH = barH * pct;
            float fillY = barY + (barH - fillH);
            dl->AddRectFilled({barX,barY}, {barX+healthBarWidth,barY+barH},
                              IM_COL32(0,0,0,180), healthBarRounding);
            ImU32 fc = showHealthColorGradient ? HpColor(displayHP) : IM_COL32(0,200,0,255);
            if(e.knocked) fc = IM_COL32(255,50,50,255);
            dl->AddRectFilled({barX,fillY}, {barX+healthBarWidth,barY+barH}, fc, healthBarRounding);
            if(showHealthText){
                char hpTxt[8];
                snprintf(hpTxt, sizeof(hpTxt), "%d", displayHP);
                auto ts = ImGui::GetFont()->CalcTextSizeA(11.f, FLT_MAX, 0.f, hpTxt);
                float tx = barX - ts.x - 3.f;
                float ty = fillY + (barH*pct - ts.y) * 0.5f;
                if(ty < barY) ty = barY;
                dl->AddText(nullptr, 11.f, {tx+1.f,ty+1.f}, IM_COL32(0,0,0,200), hpTxt);
                dl->AddText(nullptr, 11.f, {tx,ty},           IM_COL32(255,255,255,255), hpTxt);
            }
        }

        if(showLine){
            float ox = sw/2.f, oy = 5.f;
            dl->AddLine({ox,oy}, {hx,hy}, IM_COL32(255,255,255,255), lineThickness);
        }

        if(showHeadDot)
            dl->AddCircleFilled({hx,hy}, showHeadDotRadius,
                                ImGui::ColorConvertFloat4ToU32(headDotColor));

        float labelY = by - 2.f;

        if(showDistance){
            char dt[32];
            snprintf(dt, sizeof(dt), "%.0fm", e.dist);
            auto ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.f, dt);
            labelY -= (ts.y + 2.f);
            float tx = bx + bw/2.f - ts.x/2.f;
            dl->AddText(nullptr, fs, {tx+1.f,labelY+1.f}, IM_COL32(0,0,0,200), dt);
            dl->AddText(nullptr, fs, {tx,labelY},          IM_COL32(220,220,220,255), dt);
        }

        // ── NAME — real names only, no fallback ─────────────
        if(showName && e.name[0]){
            auto ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.f, e.name);
            labelY -= (ts.y + 2.f);
            float tx = bx + bw/2.f - ts.x/2.f;

            // Bots: orange. Real players: white.
            ImU32 nc = e.isBot
                ? IM_COL32(255,165,  0,255)   // bot — orange
                : IM_COL32(255,255,255,255);  // real player — white

            // Black outline
            for(float ox=-1.f; ox<=1.f; ox+=1.f)
                for(float oy=-1.f; oy<=1.f; oy+=1.f){
                    if(ox==0.f && oy==0.f) continue;
                    dl->AddText(nullptr, fs, {tx+ox,labelY+oy}, IM_COL32(0,0,0,220), e.name);
                }
            dl->AddText(nullptr, fs, {tx,labelY}, nc, e.name);
        }
    }
}