#pragma once
// v9.1 Brutal=head collider 0x6D8, ESP instant dead purge | v9: Hardware BT (H/B/Legit) + BrutalBT head-only + AimLock COLLIDER + FOV
// No heartbeat wipe, no mid-match ESP clear. offsets.h only.

#include "bool.h"
#include "offsets.h"
#include "ix_features.h"
#include "ix_rt_offsets.h"
#include "../ix_aob.h"
#include "../ix_arm64_store.h"
#include <string>
#include <cstddef>
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
#include <cstdlib>

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
bool  DragHeadshot = false;  /* [NEW] own feature -- not AimLock/AimSilent/Aimbot. See UpdateDragHeadshotThread(). */
/* [NEW] Set by anything that wipes the Paradise breakpoint set behind the
 * aim threads' backs (the diagnostic probe in ix_pd_probe.h does exactly
 * that, since hwbp_set replaces the whole point set and hwbp_remove clears
 * every debug register). The aim threads cache which address they last
 * armed and only re-arm when it changes, so without this they would stay
 * silently disarmed until the target changed. */
std::atomic<bool> g_SilentRearmRequest{false};
float g_DragHSRadiusPct  = 12.f; /* [NEW] trigger radius, % of half-screen-height -- resolution-independent, same convention as AimFov being in degrees not pixels. User-tunable via its own slider. */
bool  g_ShowDragHSCircle = false; /* [NEW] mirrors DragHeadshot -- circle only drawn while the feature itself is on */
bool  g_DragHSTriggered  = false; /* [NEW] true the instant the redirect is actually armed this frame -- drives the circle's red/gray color, same convention as fovHasTarget */
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
static std::atomic<bool> dragHeadshotThreadRunning{false}; /* [NEW] */

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

        // [NEW] Drag Headshot must not be coupled to the FOV Circle
        // slider -- its own candidate-selection radius, used below to
        // widen the shared "is this enemy near the crosshair" check
        // only when DragHeadshot is actually on, so it still finds a
        // candidate even when the FOV Range slider is set smaller
        // than Drag Headshot's own radius.
        float dragHSRadiusPx = (g_DragHSRadiusPct/100.0f) * sh2;

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
                    // [NEW] widen to Drag Headshot's own radius when it's
                    // on and that radius is bigger than the FOV circle --
                    // see dragHSRadiusPx comment above.
                    float selectRadius = DragHeadshot ? fmaxf(fovRadius, dragHSRadiusPx) : fovRadius;
                    if(sd <= selectRadius){
                        // [RE-ADDED] AimLock back in this filter — without
                        // it, Collider Lock had no target-selection path
                        // of its own at all: g_CurrentBestTarget (which
                        // UpdateAimLockNewThread reads) only ever came
                        // from this bucket, so with AimSilent/BrutalBT/
                        // HardwareBT/BodyKill all off, bestE_SA was never
                        // set and the collider loop always saw "no target
                        // selected" no matter what its own checkbox/
                        // thread fixes did.
                        // [FIX] DragHeadshot added -- without it,
                        // g_CurrentBestTarget (which
                        // UpdateDragHeadshotThread reads) never got
                        // populated when Drag Headshot was the only
                        // feature on, so the function always saw "no
                        // target selected" and silently did nothing.
                        if((AimSilent || BrutalBT || HardwareBT || BodyKill || AimLock || DragHeadshot) && sd < bestSADist){
                            bestSADist = sd;
                            bestE_SA   = e;
                            bestSAPos  = bonePos;
                            bestIdx    = eidx;
                            hasSA      = true;
                        }
                        // [RE-ADDED] Aimbot back in this filter for the
                        // same reason — its own target bucket (bestE_AB /
                        // bestABPos, feeding g_AB_tgt/g_AB_lp below) never
                        // got populated for it otherwise.
                        if((AimMagnet || DownPlayer || Aimbot) && sd < bestABDist){
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
            if((AimSilent || BrutalBT || HardwareBT || BodyKill || DragHeadshot) && bestE_SA){
                    g_CurrentBestTarget = bestE_SA;
                }
                if((AimSilent || BrutalBT || HardwareBT || BodyKill || DragHeadshot) && bestE_SA)
                g_SA_tgt = bestSAPos;
            else
                g_SA_tgt = Vector3{0,0,0};
        }
        {
            std::lock_guard<std::mutex>lk(g_AB_mtx);
            /* The unprotected raw write that used to live here
             * (Write<uintptr_t>(bestE_AB + AimCollider(), HC), firing every
             * scan tick with no HWBP/RT-Hook/Paradise protection at all)
             * stays removed — that was the actual unsafe path, not
             * UpdateAimbotFast() itself, which only ever did hook/HWBP-
             * based rotation injection (see its own comment above). What's
             * re-added here is just feeding that already-safe thread its
             * target again: g_AB_tgt/g_AB_lp have exactly one reader in
             * the whole file (UpdateAimbotFast(), a few hundred lines
             * below), so this can't affect AimMagnet/DownPlayer — neither
             * of them ever reads these two variables.
             */
            if(Aimbot && hasAB){
                g_AB_tgt = bestABPos;
                g_AB_lp  = lp;
            } else {
                g_AB_tgt = {0,0,0};
                g_AB_lp  = 0;
            }
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

    // [NEW — humanized collider-lock engagement] A fixed, zero-delay,
    // always-100%-of-the-time lock is the most visible statistical
    // tell server-side replay/behavior analysis looks for — not what
    // process issued the HWBP ioctl (that's invisible to the server
    // either way; the gameplay effect is identical regardless of
    // external vs internal injection, which is why that path was
    // dropped instead of chased further). This changes WHEN the lock
    // engages, never WHAT it writes — the override value is always the
    // real head-collider pointer already read from the game itself,
    // nothing invented here. Three independent, purely timing/
    // probability-based gates:
    //   - a short randomized reaction delay before a newly acquired
    //     target gets locked at all (a real player doesn't snap onto
    //     a target the instant it's acquired)
    //   - a per-target chance the lock skips engaging entirely
    //   - a streak cooldown after several engagements close together,
    //     so a long play session doesn't show a perfectly uniform
    //     100% engagement rate either
    static uint32_t colliderHumanRng = 0xA17B92C3u;
    inline uint32_t ColliderHumanRand() {
        colliderHumanRng ^= colliderHumanRng << 13;
        colliderHumanRng ^= colliderHumanRng >> 17;
        colliderHumanRng ^= colliderHumanRng << 5;
        return colliderHumanRng;
    }

    struct ColliderHumanizer {
        uintptr_t pendingEnemy = 0;
        std::chrono::steady_clock::time_point pendingSince{};
        bool rolledThisTarget = false;
        bool engageThisTarget = false;

        int streakCount = 0;
        std::chrono::steady_clock::time_point streakWindowStart{};
        std::chrono::steady_clock::time_point cooldownUntil{};

        // Tunables — plain timing/probability constants on our own
        // side, not derived from or dependent on anything game-
        // specific, so there is nothing here that can be "wrong"
        // about the game itself; adjust freely if a different feel
        // is wanted.
        static constexpr int kReactionDelayMinMs = 90;
        static constexpr int kReactionDelayMaxMs = 230;
        static constexpr int kEngageChancePercent = 85; // skip ~1 in ~7 targets entirely
        static constexpr int kStreakWindowMs = 9000;
        static constexpr int kStreakLimit = 5;
        static constexpr int kCooldownMinMs = 1200;
        static constexpr int kCooldownMaxMs = 2600;

        void RegisterEngagement(std::chrono::steady_clock::time_point now) {
            auto sinceWindow = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - streakWindowStart).count();
            if (streakCount == 0 || sinceWindow > kStreakWindowMs) {
                streakWindowStart = now;
                streakCount = 0;
            }
            streakCount++;
            if (streakCount >= kStreakLimit) {
                uint32_t cd = (uint32_t)kCooldownMinMs +
                    (ColliderHumanRand() % (uint32_t)(kCooldownMaxMs - kCooldownMinMs + 1));
                cooldownUntil = now + std::chrono::milliseconds(cd);
                streakCount = 0;
            }
        }

        // Returns true if the collider lock should actually engage
        // `enemy` right now. False means: treat this exactly like "no
        // valid target" for this poll — don't arm, don't update, don't
        // write anything.
        bool ShouldEngage(uintptr_t enemy) {
            auto now = std::chrono::steady_clock::now();
            if (now < cooldownUntil) return false;

            if (enemy != pendingEnemy) {
                pendingEnemy = enemy;
                pendingSince = now;
                rolledThisTarget = false;
                engageThisTarget = false;
            }

            if (!rolledThisTarget) {
                uint32_t delayMs = (uint32_t)kReactionDelayMinMs +
                    (ColliderHumanRand() % (uint32_t)(kReactionDelayMaxMs - kReactionDelayMinMs + 1));
                auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - pendingSince).count();
                if ((uint32_t)elapsedMs < delayMs) return false; // still "reacting"

                rolledThisTarget = true;
                engageThisTarget = (ColliderHumanRand() % 100u) < (uint32_t)kEngageChancePercent;
                if (engageThisTarget) RegisterEngagement(now);
            }

            return engageThisTarget;
        }
    };

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

    // ── RT Hook PTE tier — SilentAimDir / AimCollider ──────────
    // Same category of fix as the Aimbot PTE tier above: instead of a
    // HWBP (a hardware watchpoint on the DATA address — which requires
    // the CPU's debug-exception state to be armed, a condition an
    // anti-cheat can check for independent of which driver/brand set
    // it, which is why RT Hook HWBP, Paradise HWBP and TwT HWBP all
    // ban identically — the common factor across all three is "HWBP is
    // active", not which process set it), inline-hook the native
    // FUNCTION that performs the write and patch its register argument
    // before it lands. The game's own instruction does the store; no
    // debug register is ever touched.
    //
    // STATUS — infrastructure only, intentionally not wired live yet.
    // I don't have a dump/disassembly of the functions that write
    // SilentAimDir or AimCollider, so I'm not guessing an AOB pattern
    // for them the way Transform::set_rotation_Injected's pattern was
    // known for Rotation — a wrong pattern here either matches nothing
    // (safe no-op) or matches the wrong bytes and corrupts an unrelated
    // call, which is worse than the current ban. Both Resolve*Setter()
    // below return 0 until a real pattern is supplied, so every call
    // site below falls straight through to the existing, already-
    // working HWBP tier — zero behavior change from this commit.
    //
    // Exact recipe to get the missing input (IDA/Ghidra on the game's
    // libil2cpp.so, or a captured memory dump):
    //   1. Take the byte offset PlayerOffsets::SilentAimDir() (and
    //      separately whatever the AimCollider field's offset is) —
    //      the static struct offset, not a runtime address.
    //   2. xref every function containing a STR/STUR to [x?, #offset]
    //      matching that offset within the AimingInfo/PlayerInfo
    //      struct layout.
    //   3. If exactly ONE function writes it — same shape as the
    //      Transform::set_rotation_Injected stub used for Rotation
    //      above — send its prologue bytes (first ~16-32 bytes, plus
    //      which registers hold the incoming value, i.e. its calling
    //      convention) and I'll wire the matching Resolve*Setter() with
    //      a real AOB pattern and the correct register mapping in
    //      Rth*() below, exactly like RthSetRotation.
    //   4. If MULTIPLE functions write it with no single funnel point,
    //      a function-level inline hook can't cover every write site —
    //      say so explicitly rather than picking one to guess with;
    //      the fallback then is to hook the READ site that turns the
    //      field into the actual fired-shot/hitbox event instead (more
    //      likely to be a single function even when the write isn't).
    static uintptr_t ResolveSilentDirSetter() {
        // AOB pattern unknown — see recipe above. Returns 0 until a
        // real pattern is supplied; never guess a byte pattern here.
        return 0;
    }
    static uintptr_t ResolveColliderSetter() {
        // AOB pattern unknown — see recipe above. Returns 0 until a
        // real pattern is supplied; never guess a byte pattern here.
        return 0;
    }
    // Register layout for RthSetSilentDir/RthSetCollider (which GP/fp
    // register index the setter's incoming value argument(s) live in)
    // is ALSO unknown until step 3 above is done — it depends entirely
    // on that function's actual calling convention, which can't be
    // assumed from Rotation's (a different function, possibly a
    // different arg-passing shape). Deliberately not stubbing out a
    // "best guess" register-override body here: an invented register
    // index is exactly the class of mistake ground_truth forbids, and
    // unlike a 0-returning Resolve*(), a wrong register index inside a
    // live HookSetRot call could silently corrupt an unrelated call on
    // the same thread. Once a real setter address AND its calling
    // convention are confirmed, add RthSetSilentDir(RtHookDriver*, const
    // Vector3&)/RthSetCollider(RtHookDriver*, uintptr_t) here, built the
    // same way as RthSetRotation, and wire them into the `if(rth)`
    // branches in UpdateSilentAimFast()/UpdateAimLockNewThread() ahead
    // of the existing HWBP tier.

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

    // Arm HWBP on SilentAimDir (Vector3 — X,Y,Z) to override the bullet
    // direction the game writes with our computed aim vector. Same
    // AAPCS-style convention as HwbpArmRotation above (x0=X x1=Y x2=Z),
    // just one fewer register since Vector3 has no W component.
    static bool HwbpArmSilentDir(RtHookDriver* rth, uintptr_t dir_addr,
                                  const Vector3& dir) {
        HW_BP_INFO info{};
        info.pid  = g_driver ? g_driver->GetPid() : 0;
        info.addr = dir_addr;
        info.type = HW_BP_TYPE_W;
        info.len  = 4;   // 4-byte granule (Vector3 X — first field), same as rotation

        info.is_write_gp_regs = true;
        info.gp_reg_count     = 3;
        info.gp_reg_indices[0] = 0;  info.gp_reg_values[0] = RtHookDriver::float_bits(dir.X);
        info.gp_reg_indices[1] = 1;  info.gp_reg_values[1] = RtHookDriver::float_bits(dir.Y);
        info.gp_reg_indices[2] = 2;  info.gp_reg_values[2] = RtHookDriver::float_bits(dir.Z);

        return rth->UpdateAndEnableHwBp(&info);
    }
    // ── Paradise HWBP helpers ──────────────────────────────────────────
    // Two real bugs fixed here, both confirmed directly from
    // jni/src/Paradise/paradise_api.h's own documentation, not guessed:
    //
    // BUG 1 — missing register-override mask. paradise_bp_record has an
    // 18-byte `mask` field; the header's own PARADISE_BP_SET_MASK macro
    // comment says it is a "2-bit register-operation mask" and
    // hwbp_set_record's comment says it writes back "a record AND its
    // 2-bit mask". Every Pd*Arm* helper below used to build a
    // `paradise_bp_record rec{}` and set rec.x0/x1/x2/x3 directly WITHOUT
    // ever calling PARADISE_BP_SET_MASK — so every register's mask byte
    // stayed 0 (PARADISE_BP_OP_NONE), meaning the driver was told "do not
    // override this register" for every single register whose value we
    // were setting. The breakpoint armed fine; the override simply never
    // applied. This alone is enough to make Aim Silent (and Rotation,
    // and Collider Lock) look like they silently do nothing.
    //
    // BUG 2 — shared point-table stomping. hwbp_set(points, count) is a
    // REPLACE of the driver's whole point table (confirmed by its own
    // comment: "设置最多16个...断点/观察点" — set [the] breakpoints, not
    // "add one"), and hwbp_remove() "清理所有 CPU 调试寄存器" — clears
    // ALL CPU debug registers for the whole process, not just the
    // caller's own point. Rotation, SilentDir and Collider Lock each
    // independently called `pd->hwbp_set(&pt, 1)` with their OWN lone
    // point at index 0, and PdHwbpDisarm called `pd->hwbp_remove()`
    // unconditionally — so whichever of those three features last
    // (re)armed wiped out the other two's breakpoints process-wide, from
    // a separate thread, with no coordination at all. Three features
    // sharing one hardware resource with no registry is exactly "koi
    // function not working jab doosra use karte ho".
    //
    // FIX, v2 — [CHANGED] v1 padded the array passed to hwbp_set() with
    // PARADISE_HWBP_EMPTY placeholder entries for inactive slots so each
    // feature's point_index would stay fixed regardless of what else was
    // active. That assumed the driver accepts/ignores EMPTY entries
    // mixed into an otherwise-real point array — nothing in
    // paradise_api.h actually documents that, it was an unverified guess,
    // and after it shipped BOTH Aim Silent and Hardware BT stopped
    // working (where Hardware BT's old plain-write path used to work
    // fine before this registry existed) — consistent with hwbp_set()
    // now failing outright, or silently misbehaving, on a table full of
    // zeroed/EMPTY entries it was never meant to receive.
    //
    // v2 never sends a placeholder entry: only the currently-ACTIVE
    // slots are compacted into the array passed to hwbp_set(), count =
    // however many are active, exactly matching the one thing the header
    // actually documents ("set up to 16 points"). Because compacting
    // means a slot's position in that array can shift when some OTHER
    // slot joins or leaves, g_position[] tracks, per slot, where it
    // currently lives — recomputed on every CommitLocked() call — and
    // callers must use ParadisePoints::GetPosition(slot) for
    // hwbp_set_record, never the raw Slot enum value.
    //
    // FIX, v3 — [CHANGED] the registry now holds ONLY the two features
    // that are actually supposed to run on a Paradise hardware
    // breakpoint: Aim Silent and Hardware BT. Rotation (aimbot),
    // Collider Lock and Drag Headshot have been taken off the Paradise
    // HWBP path entirely — Rotation and Collider Lock now use the plain
    // direct write they already had as their non-Paradise path (and
    // which is how they worked before any HWBP code existed), and Drag
    // Headshot's UI/feature is gone. With exactly two possible
    // occupants, there are at most two points in the table, both owned
    // here, and nothing else in the process can shift or wipe them.
    // FIX, v5 — [CHANGED BACK] ONE point per feature, at the Vector3's
    // base address.
    //
    // v4 gave each feature three 4-byte points, one per component, to
    // cover the case where the compiler emitted three separate component
    // stores. That was unsound. The three windows are adjacent, so a
    // WIDE store — `str q0, [x8]` covers 16 bytes — intersects ALL THREE
    // of them, and one instruction execution then matches three debug
    // registers at once. Nothing in paradise_api.h says how the driver
    // handles that: one combined event, three events, three separate
    // override injections. Worse, this code then read each point's
    // records and pushed an override through all three independently, so
    // three hwbp_set_record calls were templating the same store. That is
    // consistent with exactly what came back from the device — the
    // breakpoint fires and the override lands, but the direction that
    // reaches the game is erratic frame to frame.
    //
    // One point at the base address is the shape that is actually
    // reasoned about end to end. It traps a wide store (any overlapping
    // byte triggers a watchpoint), and ONE captured record is enough to
    // steer all three components, because the override is derived from
    // the instruction's own effective address and transfer size — not
    // from which watched byte happened to trip. That path is verified on
    // the host for str q0 / stp d0,d1 / stp s0,s1 / str s0 / str w9 /
    // stur: a single 16-byte-store record fills X, Y and Z from one
    // register and leaves the 4th lane alone.
    //
    // FIX, v6 — TWO non-overlapping points per feature, from the device's
    // own evidence. The v5 diagnostic came back as:
    //
    //   pc=0x6f38cd8594 insn=0x2d08310d FP rt=13,rt2=12 xfer=4B rn=8 imm=64
    //   pc=0x6f38cda530 insn=0x2d08290b FP rt=11,rt2=10 xfer=4B rn=8 imm=64
    //   pc=0x6f380ffd60 insn=0x2d080121 FP rt=1,rt2=0   xfer=4B rn=9 imm=64
    //
    // which decode as `stp s13, s12, [x8, #64]`, `stp s11, s10, [x8, #64]`
    // and `stp s1, s0, [x9, #64]`. So this build writes the field as a
    // PAIR of 32-bit FP registers: Rt lands on base+0 (X) and Rt2 on
    // base+4 (Y). Three different call sites do it, two inlined copies in
    // one function plus one elsewhere, each with its own register
    // allocation — which is precisely why the source register had to be
    // decoded per PC instead of assumed.
    //
    // The important part: that STP spans bytes 0..7 only. NOTHING in the
    // capture writes bytes 8..11. Z is stored by a separate instruction
    // that a point on bytes 0-3 never traps, so Z was never overridden —
    // the game kept its own Z while X and Y were forced elsewhere, and a
    // direction vector built from two hijacked components and one
    // original one points off to the side. That is the reported "bullet
    // right side ja raha hai, hit nahi kar raha".
    //
    // So: one point at base+0 (traps the X/Y pair store, already proven
    // to work by the capture above) and a second at base+8 (traps
    // whatever stores Z). These two watch byte ranges [0,4) and [8,12) —
    // they share no byte with each other, and the STP's [0,8) footprint
    // cannot reach the second point. That is the difference from the
    // broken v4: there, three adjacent windows all sat inside one wide
    // store's footprint, so a single instruction tripped all three and
    // three hwbp_set_record calls fought over templating it. Here each
    // point is tripped by a different instruction, so there is no shared
    // store to race over.
    //
    // The lane mapping needs no change for this and is unchanged: it
    // derives which component each source register carries from the
    // instruction's own effective address and transfer size, not from
    // which point caught the record, so a record captured by either
    // point is mapped correctly on its own terms.
    //
    // (The capture also showed one stray record — pc=0x6f38cd429c,
    // `stp q0, q0, [x20, #-64]`, a register-spill shape at a negative
    // offset. Nothing is done about it on purpose: the lane check
    // compares the instruction's computed effective address against the
    // live base, so a record that does not actually land on this
    // Vector3 gets no override and is skipped.)
    namespace ParadisePoints {
        enum Slot {
            SLOT_SILENT_XY = 0, SLOT_SILENT_Z = 1,
            SLOT_BT_XY     = 2, SLOT_BT_Z     = 3,
            SLOT_COUNT     = 4
        };
        static std::mutex                g_mtx;
        static paradise_bp_point_config  g_points[SLOT_COUNT];
        static bool                      g_active[SLOT_COUNT]   = { false, false, false, false };
        static int                       g_position[SLOT_COUNT] = { -1, -1, -1, -1 };

        // Rebuilds g_position[] from g_active[] (stable order: lowest
        // Slot id first) and submits ONLY the active points, compacted,
        // count = number active (count is never 0 here — the only path
        // that reaches 0 is Disarm() on the last remaining slot, handled
        // as its own documented case below via hwbp_remove(), since
        // passing an empty/null array to hwbp_set() isn't anything
        // paradise_api.h actually documents and isn't worth guessing at).
        static bool CommitLocked(paradise_driver* pd) {
            paradise_bp_point_config snapshot[SLOT_COUNT];
            int n = 0;
            for(int i = 0; i < SLOT_COUNT; i++) {
                if(g_active[i]) {
                    snapshot[n] = g_points[i];
                    g_position[i] = n;
                    n++;
                } else {
                    g_position[i] = -1;
                }
            }
            if(n == 0) {
                // Nobody active: this is exactly what hwbp_remove() is
                // documented for ("删除当前硬件断点/观察点并清理所有 CPU
                // 调试寄存器" — remove current breakpoints and clean up
                // all CPU debug registers). Safe here specifically
                // because nothing is active, so there is nothing else to
                // wipe out.
                return pd->hwbp_remove();
            }
            return pd->hwbp_set(snapshot, n);
        }

        // Claims/updates `slot`'s point config. Re-submits the compacted
        // table (CommitLocked) whenever this slot's address/type/length
        // changed, it was previously inactive, or the active SET itself
        // just changed as a side effect of another slot's own Arm/Disarm
        // call racing in on another thread (g_position[slot] == -1 while
        // g_active[slot] is about to become true is exactly that case) —
        // a value-only update (same address, new direction/rotation, no
        // membership change anywhere) skips straight to the caller's
        // hwbp_set_record and never touches hwbp_set at all.
        static bool Arm(paradise_driver* pd, int slot, uintptr_t addr,
                         uint32_t type, uint32_t len) {
            std::lock_guard<std::mutex> lk(g_mtx);
            bool changed = !g_active[slot] ||
                           g_points[slot].address != (uint64_t)addr ||
                           g_points[slot].type    != type ||
                           g_points[slot].length  != len;
            paradise_bp_point_config pt{};
            pt.address = (uint64_t)addr;
            pt.type    = type;
            pt.length  = len;
            pt.scope   = PARADISE_HWBP_ALL_THREADS;
            g_points[slot] = pt;
            g_active[slot] = true;
            if(!changed) return true; // table unchanged — caller just updates its record
            return CommitLocked(pd);
        }

        static void Disarm(paradise_driver* pd, int slot) {
            std::lock_guard<std::mutex> lk(g_mtx);
            if(!g_active[slot]) return;
            g_active[slot] = false;
            CommitLocked(pd);
        }

        // Current array position for `slot`'s point in the LAST
        // hwbp_set() call, or -1 if it isn't active. This is what the
        // caller must pass to hwbp_set_record/hwbp_get_records — never
        // the raw Slot enum value, since compaction can move it.
        static int GetPosition(int slot) {
            std::lock_guard<std::mutex> lk(g_mtx);
            return g_position[slot];
        }

    } // namespace ParadisePoints

    // ── Vector3 write-override engine ──────────────────────────────────
    //
    // BUG 3 — the override targeted the wrong registers. The old helpers
    // put the three floats in rec.x0/x1/x2. x0-x2 is the AAPCS slot for
    // an integer argument; a Vector3 is a homogeneous floating-point
    // aggregate, so even as an argument it travels in s0/s1/s2 — the low
    // 32 bits of q0/q1/q2. ix_pd_probe.h has this straight off the
    // device: "Vector3 argument (AAPCS HFA: s0=X s1=Y s2=Z)". And for a
    // STORE none of that applies anyway: the source register is whatever
    // the compiler allocated at that instruction. Writing floats into
    // x0/x1/x2 therefore did two wrong things at once — it failed to
    // change the stored value, and it clobbered three live
    // general-purpose registers (likely pointers) on every hit.
    //
    // BUG 4 — the record index was hardcoded to 0. hwbp_get_records is
    // documented as "分段读取指定点位按触发 PC 聚合的现场记录" — records
    // for a point are aggregated BY TRIGGER PC, so record_index selects
    // WHICH instruction's capture you are overriding. Writing record 0
    // unconditionally means that if more than one instruction writes the
    // field, at most one of them is ever overridden, and before the
    // point has been hit at all there is no record 0 to write to.
    //
    // So the override is now derived instead of assumed: read the point's
    // records, decode the instruction at each record's PC, work out the
    // effective address it stores to and which register bytes land on
    // which Vector3 component, then patch exactly those bytes — starting
    // from the register values the record itself captured, so nothing
    // else in the register is disturbed — and write that record back at
    // its own index. An instruction the decoder does not recognise gets
    // NO override at all, because overriding a register a store does not
    // read is how you corrupt the game instead of steering it.

    // paradise_bp_record declares x0..x29 and q0..q31 as separate
    // consecutive members of one type each. These static_asserts pin
    // that layout down so indexing across them is safe; if the vendor
    // ever reorders the struct this stops compiling instead of silently
    // reading the wrong register.
    static_assert(offsetof(paradise_bp_record, x1) - offsetof(paradise_bp_record, x0) == sizeof(uint64_t),
                  "paradise_bp_record x-registers are not contiguous");
    static_assert(offsetof(paradise_bp_record, x29) - offsetof(paradise_bp_record, x0) == 29 * sizeof(uint64_t),
                  "paradise_bp_record x-register span is not 30 consecutive slots");
    static_assert(offsetof(paradise_bp_record, q1) - offsetof(paradise_bp_record, q0) == sizeof(__uint128_t),
                  "paradise_bp_record q-registers are not contiguous");
    static_assert(offsetof(paradise_bp_record, q31) - offsetof(paradise_bp_record, q0) == 31 * sizeof(__uint128_t),
                  "paradise_bp_record q-register span is not 32 consecutive slots");

    static inline uint64_t* RecGpSlot(paradise_bp_record* r, unsigned n) {
        if(n <= 29) return reinterpret_cast<uint64_t*>(&r->x0) + n;
        if(n == 30) return &r->lr;   // x30
        if(n == 31) return &r->sp;   // SP in the base-register position
        return nullptr;
    }
    static inline __uint128_t* RecFpSlot(paradise_bp_record* r, unsigned n) {
        if(n <= 31) return reinterpret_cast<__uint128_t*>(&r->q0) + n;
        return nullptr;
    }

    // Instruction word at a PC, cached: the code at a given PC does not
    // change, so this is read once per distinct trigger PC rather than
    // every frame.
    static uint32_t InsnAt(uintptr_t pc) {
        struct Entry { uintptr_t pc; uint32_t w; };
        static std::mutex  s_mtx;
        static Entry       s_cache[16] = {};
        static int         s_next = 0;
        {
            std::lock_guard<std::mutex> lk(s_mtx);
            for(const auto& e : s_cache)
                if(e.pc == pc) return e.w;
        }
        uint32_t w = Read<uint32_t>(pc);
        if(w){
            std::lock_guard<std::mutex> lk(s_mtx);
            s_cache[s_next % 16] = Entry{ pc, w };
            s_next++;
        }
        return w;
    }

    // Logs an instruction the decoder could not handle, once per PC, so
    // an unsupported store form is identifiable instead of silent.
    static void LogUndecoded(uintptr_t pc, uint32_t w) {
        static std::mutex   s_mtx;
        static uintptr_t    s_seen[8] = {};
        static int          s_n = 0;
        std::lock_guard<std::mutex> lk(s_mtx);
        for(const auto& p : s_seen) if(p == pc) return;
        s_seen[s_n % 8] = pc; s_n++;
        printf("[INX] hwbp: store at pc=%p not decoded (insn=0x%08x) — no register "
               "override applied for it. Disassemble that word to identify the form.\n",
               (void*)pc, w);
        fflush(stdout);
    }

    // Reports the decoded shape of a store, once per PC. xfer=16 or a
    // pair means the whole Vector3 moves in one instruction and one
    // point on the base address covers everything; xfer=4 with no pair
    // means component-wise stores, and the PCs logged here will differ
    // for X, Y and Z.
    static void LogStoreShape(uintptr_t pc, uint32_t w, const IxArm64StoreInfo& si) {
        static std::mutex   s_mtx;
        static uintptr_t    s_seen[8] = {};
        static int          s_n = 0;
        std::lock_guard<std::mutex> lk(s_mtx);
        for(const auto& p : s_seen) if(p == pc) return;
        s_seen[s_n % 8] = pc; s_n++;
        char rt2buf[24] = "";
        if(si.is_pair) snprintf(rt2buf, sizeof(rt2buf), ",rt2=%u", si.rt2);
        printf("[INX] hwbp: dir store pc=%p insn=0x%08x regfile=%s rt=%u%s "
               "xfer=%uB rn=%u imm=%lld form=%d\n",
               (void*)pc, w,
               si.regfile == IX_REGFILE_FP ? "FP(q)" : "GP(x)",
               si.rt, rt2buf,
               si.xfer_bytes, si.rn, (long long)si.imm, (int)si.addr_form);
        fflush(stdout);
    }

    // Builds the override for ONE captured record and writes it back.
    // `base` is the Vector3's address, `dir` the value we want stored.
    // Returns true if a register override was applied for this record.
    static bool ApplyDirOverrideToRecord(paradise_driver* pd, int point_pos,
                                         uint32_t rec_index,
                                         paradise_bp_record rec,
                                         uintptr_t base, const Vector3& dir) {
        const uint32_t w = InsnAt((uintptr_t)rec.pc);
        if(!w) return false;

        const IxArm64StoreInfo si = ix_arm64_decode_store(w);
        if(!si.decoded){
            LogUndecoded((uintptr_t)rec.pc, w);
            return false;
        }

        // One line per distinct PC, the first time that instruction is
        // actually seen writing the field. This is the measurement that
        // settles whether the Vector3 goes out as ONE wide store or as
        // separate narrow component stores — the single open question
        // left in this path, and the thing that decides whether a point
        // on the base address alone is sufficient. Read it off the
        // device rather than reasoning about what the compiler probably
        // did.
        LogStoreShape((uintptr_t)rec.pc, w, si);

        // Effective address of the store, from the registers this very
        // hit captured.
        const uint64_t* rn_p = RecGpSlot(&rec, si.rn);
        if(!rn_p) return false;
        uint64_t ea = *rn_p;
        if(si.addr_form == IX_ADDR_REG){
            const uint64_t* rm_p = RecGpSlot(&rec, si.rm);
            if(!rm_p) return false;
            ea += ix_arm64_extend_reg(*rm_p, si.extend_option, si.extend_shift);
        } else if(si.addr_form == IX_ADDR_IMM){
            ea = (uint64_t)((int64_t)ea + si.imm);
        }
        // IX_ADDR_POST stores to the un-updated base: ea stays as-is.

        const float comp[3] = { dir.X, dir.Y, dir.Z };
        bool touched = false;

        // Walk each source register this instruction stores from, and
        // patch any 4-byte lane of it that lands on one of the three
        // Vector3 components. Start from the captured register value so
        // bytes we do not own keep whatever the game had in them.
        const int nregs = si.is_pair ? 2 : 1;
        for(int k = 0; k < nregs; k++){
            const unsigned regno   = (k == 0) ? si.rt : si.rt2;
            const uint64_t reg_ea  = ea + (uint64_t)k * si.xfer_bytes;

            uint8_t  buf[16] = {};
            const unsigned nbytes = si.xfer_bytes > 16 ? 16 : si.xfer_bytes;

            uint64_t*     gp = nullptr;
            __uint128_t*  fp = nullptr;
            if(si.regfile == IX_REGFILE_GP){
                gp = RecGpSlot(&rec, regno);
                if(!gp) continue;
                __builtin_memcpy(buf, gp, nbytes > 8 ? 8 : nbytes);
            } else {
                fp = RecFpSlot(&rec, regno);
                if(!fp) continue;
                __builtin_memcpy(buf, fp, nbytes);
            }

            bool reg_touched = false;
            for(int c = 0; c < 3; c++){
                const uint64_t caddr = (uint64_t)base + (uint64_t)c * 4u;
                if(caddr < reg_ea) continue;
                const uint64_t off = caddr - reg_ea;
                if(off + 4u > nbytes) continue;      // lane not inside this register
                uint32_t bits;
                __builtin_memcpy(&bits, &comp[c], 4);
                __builtin_memcpy(buf + off, &bits, 4);
                reg_touched = true;
            }
            if(!reg_touched) continue;

            if(si.regfile == IX_REGFILE_GP){
                __builtin_memcpy(gp, buf, nbytes > 8 ? 8 : nbytes);
                PARADISE_BP_SET_MASK(&rec, PARADISE_REG_X0 + (int)regno, PARADISE_BP_OP_WRITE);
            } else {
                __builtin_memcpy(fp, buf, nbytes);
                PARADISE_BP_SET_MASK(&rec, PARADISE_REG_Q0 + (int)regno, PARADISE_BP_OP_WRITE);
            }
            touched = true;
        }

        if(!touched) return false;
        return pd->hwbp_set_record((uint32_t)point_pos, rec_index, &rec);
    }

    // Reads every record captured for one point and pushes the override
    // into each. Returns true if at least one record was overridden.
    static bool PushDirOverride(paradise_driver* pd, int slot,
                                 uintptr_t base, const Vector3& dir) {
        const int pos = ParadisePoints::GetPosition(slot);
        if(pos < 0) return false;    // raced with a Disarm — skip this frame

        paradise_bp_record recs[8]{};
        uint32_t count = 0;
        if(!pd->hwbp_get_records((uint32_t)pos, 0, recs,
                                 (uint32_t)(sizeof(recs)/sizeof(recs[0])), &count))
            return false;
        if(count == 0) return false; // armed, not hit yet — nothing to override

        if(count > sizeof(recs)/sizeof(recs[0]))
            count = (uint32_t)(sizeof(recs)/sizeof(recs[0]));

        bool any = false;
        for(uint32_t i = 0; i < count; i++)
            if(ApplyDirOverrideToRecord(pd, pos, i, recs[i], base, dir))
                any = true;
        return any;
    }

    // Arms a feature's two points — base+0 for the X/Y pair store and
    // base+8 for the Z store — and pushes the current direction into
    // whatever each has captured so far. `slot_xy` is the feature's
    // first slot; its Z slot follows immediately.
    //
    // Returns false only if BOTH arms failed. A true return does NOT
    // mean an override is live yet: a freshly armed point holds no
    // record until the game next writes through it.
    static bool PdHwbpArmDir(paradise_driver* pd, int slot_xy,
                              uintptr_t base, const Vector3& dir) {
        const int slot_z = slot_xy + 1;

        // Non-overlapping by construction: [base+0, base+4) and
        // [base+8, base+12). See the ParadisePoints comment for why the
        // gap at [base+4, base+8) is correct and not a hole — the X/Y
        // pair store spans 0..7, so it already trips the base point.
        const bool xy = ParadisePoints::Arm(pd, slot_xy, base,
                                             PARADISE_HWBP_WRITE, 4);
        const bool z  = ParadisePoints::Arm(pd, slot_z,  base + 8u,
                                             PARADISE_HWBP_WRITE, 4);
        if(!xy && !z) return false;

        if(xy) PushDirOverride(pd, slot_xy, base, dir);
        if(z)  PushDirOverride(pd, slot_z,  base, dir);
        return true;
    }

    static void PdHwbpDisarmDir(paradise_driver* pd, int slot_xy) {
        if(!pd) return;
        ParadisePoints::Disarm(pd, slot_xy);
        ParadisePoints::Disarm(pd, slot_xy + 1);
    }

    // ── TwT HWBP helpers ───────────────────────────────────────────────
    // Same convention as RT Hook/Paradise above: a write breakpoint on
    // the pointer field, with the hit-time register override replacing
    // x0 (the value the game itself is writing) with head_collider_val.
    // TwT's bp_inst() hands back an opaque handle (0 on failure) that
    // bp_modify()/bp_uninst() consume for update/disarm — tracked in a
    // static alongside the existing s_lastHwbpAddr/s_lastEnemy/
    // s_lastCollider trio the collider-lock loop already keeps.
    static bool TwtHwbpArmCollider(TwtDriver* twt, pid_t target_pid,
                                    uintptr_t collider_addr,
                                    uintptr_t head_collider_val,
                                    uint64_t& out_handle) {
        twt_bp_user_pt_regs regs{};
        regs.regs[0] = (uint64_t)head_collider_val;
        uint64_t handle = twt->bp_inst(target_pid, (uint64_t)collider_addr,
                                        TWT_HW_BREAKPOINT_LEN_8, TWT_HW_BREAKPOINT_W,
                                        TWT_REG_MODIFY_X(0), 0, &regs, 0);
        if(!handle) return false;
        out_handle = handle;
        return true;
    }

    static bool TwtHwbpUpdateCollider(TwtDriver* twt, uint64_t handle,
                                       uintptr_t head_collider_val) {
        if(!handle) return false;
        twt_bp_user_pt_regs regs{};
        regs.regs[0] = (uint64_t)head_collider_val;
        return twt->bp_modify(handle, TWT_REG_MODIFY_X(0), 0, &regs);
    }

    // Arm/update TwT HWBP on SilentAimDir (Vector3) — same AAPCS-style
    // x0-x2 GP-register override convention as the RT Hook/Paradise
    // SilentDir helpers above, one register fewer than rotation (no W).
    static bool TwtHwbpArmSilentDir(TwtDriver* twt, pid_t target_pid,
                                     uintptr_t dir_addr, const Vector3& dir,
                                     uint64_t& out_handle) {
        twt_bp_user_pt_regs regs{};
        regs.regs[0] = (uint64_t)RtHookDriver::float_bits(dir.X);
        regs.regs[1] = (uint64_t)RtHookDriver::float_bits(dir.Y);
        regs.regs[2] = (uint64_t)RtHookDriver::float_bits(dir.Z);
        uint64_t handle = twt->bp_inst(target_pid, (uint64_t)dir_addr,
                                        TWT_HW_BREAKPOINT_LEN_4, TWT_HW_BREAKPOINT_W,
                                        TWT_REG_MODIFY_X(0) | TWT_REG_MODIFY_X(1) | TWT_REG_MODIFY_X(2),
                                        0, &regs, 0);
        if(!handle) return false;
        out_handle = handle;
        return true;
    }

    static bool TwtHwbpUpdateSilentDir(TwtDriver* twt, uint64_t handle,
                                        const Vector3& dir) {
        if(!handle) return false;
        twt_bp_user_pt_regs regs{};
        regs.regs[0] = (uint64_t)RtHookDriver::float_bits(dir.X);
        regs.regs[1] = (uint64_t)RtHookDriver::float_bits(dir.Y);
        regs.regs[2] = (uint64_t)RtHookDriver::float_bits(dir.Z);
        return twt->bp_modify(handle, TWT_REG_MODIFY_X(0) | TWT_REG_MODIFY_X(1) | TWT_REG_MODIFY_X(2),
                               0, &regs);
    }

    static void TwtHwbpDisarm(TwtDriver* twt, uint64_t& handle) {
        if(twt && handle) twt->bp_uninst(handle);
        handle = 0;
    }

    // ── inx_mem KPM HWBP tier (Option 16) ───────────────────────────────
    // Same write-watchpoint + GP-register-override convention as the
    // Paradise/RT-Hook/TwT tiers above, now actually wired end to end:
    // kernel module (kpm/inx_mem/inx_hwbp.h) -> KpmDriver::hwbp_arm_write
    // (jni/src/KPM/kpm_driver.h) -> here. 4-byte granule on the first
    // float (x0=X), matching the RT Hook/TwT tiers' convention rather
    // than Paradise's wider 12-byte region -- arm64 hardware watchpoints
    // are only defined for power-of-two lengths up to 8 bytes, so a
    // single 12-byte watchpoint was never a real hardware breakpoint to
    // begin with; this tier arms three separate 4-byte watchpoints
    // (one per component) exactly as a real debug register layout
    // requires, tracked as three slots.
    static bool KpmHwbpArmSilentDir(KpmDriver* kpm, uintptr_t dir_addr,
                                     const Vector3& dir, int out_slots[3]) {
        if(!kpm || !kpm->is_ready()) return false;

        const uint32_t idx[1] = { 0 }; // x0 — the value about to be stored
        const float    comps[3] = { dir.X, dir.Y, dir.Z };

        for(int c = 0; c < 3; c++) {
            uint64_t val = (uint64_t)RtHookDriver::float_bits(comps[c]);
            if(!kpm->hwbp_arm_write(dir_addr + (uintptr_t)(c * 4), 4,
                                     idx, &val, 1, out_slots[c])) {
                // roll back whatever we already armed this call — never
                // leave a partial set of watchpoints active
                for(int k = 0; k < c; k++) {
                    if(out_slots[k] >= 0) kpm->hwbp_del(out_slots[k]);
                    out_slots[k] = -1;
                }
                out_slots[c] = -1;
                return false;
            }
        }
        return true;
    }

    // Updates the register override on three ALREADY-armed slots (from a
    // prior KpmHwbpArmSilentDir call) with a new direction — no re-add, no
    // new kernel slot consumed. This is the per-frame hot path; the kernel
    // only has an 8-slot HWBP budget total (inx_hwbp.h's INX_HWBP_SLOTS),
    // so calling hwbp_add again every frame at ~90Hz would exhaust it in
    // under a tenth of a second instead of reusing the 3 slots already
    // held, exactly like Paradise's hwbp_set_record updates an
    // already-armed point instead of re-registering it.
    static bool KpmHwbpUpdateSilentDir(KpmDriver* kpm, const int slots[3],
                                        const Vector3& dir) {
        if(!kpm) return false;
        const uint32_t idx[1] = { 0 };
        const float    comps[3] = { dir.X, dir.Y, dir.Z };
        bool ok = true;
        for(int c = 0; c < 3; c++) {
            if(slots[c] < 0) { ok = false; continue; }
            uint64_t val = (uint64_t)RtHookDriver::float_bits(comps[c]);
            if(!kpm->hwbp_set_gp(slots[c], idx, &val, 1)) ok = false;
        }
        return ok;
    }

    static void KpmHwbpDisarm(KpmDriver* kpm, int slots[3]) {
        if(!kpm) return;
        for(int i = 0; i < 3; i++) {
            if(slots[i] >= 0) kpm->hwbp_del(slots[i]);
            slots[i] = -1;
        }
    }

} // namespace AimHookState

void UpdateSilentAimFast(){
    /* Aim Silent / Body Kill — redirects the actual bullet via SilentAimDir.
     * Body Kill drives this thread too, but only switches to body once the
     * enemy's real HP (read-only, never written) drops to/below the slider
     * value — see BodyKillThresholdReached() below. Before that point the
     * bullet lands on the head, same as plain Aim Silent.
     * Do not touch for Hardware BT (separate thread/logic).
     *
     * [CHANGED] Driver-selected dispatch, NOT a runtime fallback chain:
     *   - Paradise connected (g_driver->GetParadise() non-null)  -> ALWAYS
     *     uses the Paradise HWBP tier. Full stop.
     *   - Paradise not connected AND KPM (Option 16) connected   -> ALWAYS
     *     uses the inx_mem KPM HWBP tier. Full stop.
     *   - Neither connected                                      -> this
     *     frame is skipped (no write at all).
     * There is no "try Paradise, if it fails this frame fall back to KPM"
     * logic, and no retry/streak/cross-switch logic of any kind — which
     * tier runs is decided purely by which driver is selected for this
     * run (c_driver::use_paradise / use_kpm from config.json's "driver"
     * key), never by a per-frame success/failure count. If the selected
     * driver's arm call itself fails on a given frame, that frame is
     * skipped (SilentAimDir left exactly as the game wrote it) — it does
     * NOT switch to the other tier. RT Hook tier, TwT tier and the plain
     * direct-write fallback are all still removed (same reasoning as
     * before: a direct write to SilentAimDir is visible as an
     * out-of-band write instead of the game's own write being overridden
     * in place, and is intentionally not restored). See
     * AimHookState::PdHwbpArmDir/PdHwbpDisarmDir and
     * AimHookState::KpmHwbpArmSilentDir/KpmHwbpDisarm above. */
    static uintptr_t s_lastDirAddr    = 0;
    static bool      s_lastViaKpm     = false;
    static int        s_kpmSlots[3]   = { -1, -1, -1 };

    auto disarm_silent = [&](){
        if(!s_lastDirAddr) return;
        if(s_lastViaKpm){
            KpmDriver* kpm = g_driver ? g_driver->GetKpm() : nullptr;
            AimHookState::KpmHwbpDisarm(kpm, s_kpmSlots);
        } else {
            paradise_driver* pd = g_driver ? g_driver->GetParadise() : nullptr;
            AimHookState::PdHwbpDisarmDir(pd, AimHookState::ParadisePoints::SLOT_SILENT_XY);
        }
        s_lastDirAddr = 0;
        s_lastViaKpm  = false;
    };

    while(silentThreadRunning){
        /* [NEW] Something wiped the Paradise point set from outside (see
         * g_SilentRearmRequest). Forget the cached address so the arm below
         * actually re-runs instead of assuming it is still installed. */
        if(g_SilentRearmRequest.exchange(false))
            s_lastDirAddr = 0;

        if(!ix_feat_aim() || !ix_rt_ready() || !(AimSilent || BodyKill)){
            disarm_silent();
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

                uintptr_t dirAddr = aimingInfo + PlayerOffsets::SilentAimDir();
                paradise_driver* pd  = g_driver ? g_driver->GetParadise() : nullptr;
                KpmDriver*       kpm = g_driver ? g_driver->GetKpm()      : nullptr;

                // ── Driver-selected, not failure-driven: whichever of
                // Paradise/KPM is actually the connected driver this run
                // is the ONLY one ever tried, every frame. No runtime
                // fallback between them. If that one driver's arm call
                // fails on a given frame, the frame is skipped — it does
                // NOT reach for the other tier. "viaKpm" only flips if
                // which driver is connected changes (e.g. hot-swapped
                // between runs); it is never toggled by an arm failure. ──
                bool viaKpm = (!pd && kpm && kpm->is_ready());
                bool need_rearm = (s_lastDirAddr != dirAddr) || (s_lastViaKpm != viaKpm);

                if(need_rearm && s_lastDirAddr)
                    disarm_silent();

                if(pd){
                    // Arm (idempotent once the address is unchanged) and
                    // push the override into every record captured so
                    // far. Both happen every frame: the arm call is a
                    // no-op after the first, and the override push is
                    // what tracks the moving aim direction.
                    if(AimHookState::PdHwbpArmDir(
                           pd, AimHookState::ParadisePoints::SLOT_SILENT_XY, dirAddr, dir)){
                        s_lastDirAddr = dirAddr;
                        s_lastViaKpm  = false;
                    } else {
                        // [FIX] Arm() may already have claimed a slot
                        // before a later one failed. Zeroing
                        // s_lastDirAddr alone would leave those points
                        // armed while disarm_silent() early-returns on
                        // !s_lastDirAddr — a watchpoint stuck on a stale
                        // address after the feature is switched off.
                        // Release the whole group explicitly.
                        AimHookState::PdHwbpDisarmDir(pd, AimHookState::ParadisePoints::SLOT_SILENT_XY);
                        s_lastDirAddr = 0; // arm failed this frame — skip, no fallback write
                    }
                } else if(kpm){
                    // Only (re-)allocate the 3 kernel slots when the
                    // address/tier actually changed this frame (need_rearm
                    // already disarmed any stale slots above); otherwise
                    // just push the new direction into the slots we
                    // already hold — see KpmHwbpUpdateSilentDir's comment
                    // for why re-adding every frame would be wrong here.
                    bool justArmed = (s_lastDirAddr != dirAddr) || !s_lastViaKpm;
                    bool armed = justArmed
                        ? AimHookState::KpmHwbpArmSilentDir(kpm, dirAddr, dir, s_kpmSlots)
                        : AimHookState::KpmHwbpUpdateSilentDir(kpm, s_kpmSlots, dir);
                    if(armed){
                        s_lastDirAddr = dirAddr;
                        s_lastViaKpm  = true;
                    } else {
                        AimHookState::KpmHwbpDisarm(kpm, s_kpmSlots);
                        s_lastDirAddr = 0; // arm/update failed this frame — skip, no fallback write
                    }
                } else {
                    s_lastDirAddr = 0; // neither driver available this frame
                }
            }
        }
        std::this_thread::yield();
    }
}

/* ===== Drag Headshot — own feature, independent of Collider Lock / =====
 * ===== Aim Silent / Aimbot. Requested explicitly: must not reuse  =====
 * ===== any of those three functions or their gating.              =====
 *
 * What it does: while the player is actively dragging their own view
 * (detected below — NOT a fixed timer, NOT "player is firing", purely
 * "is the view rotation actually changing right now") and a valid
 * target is selected, it silently redirects the shot toward the
 * target's NECK bone (BoneOffsets::Neck(), server-driven offset --
 * requested by name, separate from Head/Chest/Hip already used by
 * every other feature). The moment the drag stops (rotation holds
 * steady again) this disarms immediately -- it never holds a lock on
 * an idle crosshair, so it cannot behave like Aimbot/Collider Lock.
 *
 * Reuses the same low-level HWBP-arm / plain-write helpers as the
 * other SilentAimDir-based features (AimHookState::*SilentDir) --
 * those are generic driver plumbing, not the Aim Silent *feature* --
 * but keeps its own armed-breakpoint state (s_lastDirAddr/s_lastTwtHandle
 * below are local statics of THIS function) so it never fights over
 * the same hardware breakpoint slot as UpdateSilentAimFast() if both
 * somehow ran at once. */
void UpdateDragHeadshotThread(){
    static uintptr_t s_lastDirAddr   = 0;
    static uint64_t  s_lastTwtHandle = 0;

    auto disarm_dh = [&](){
        RtHookDriver*    rth = g_driver ? g_driver->GetRtHook()   : nullptr;
        paradise_driver*  pd = g_driver ? g_driver->GetParadise() : nullptr;
        TwtDriver*       twt = g_driver ? g_driver->GetTwT()      : nullptr;
        if(rth && s_lastDirAddr)
            rth->DisableHwBp(g_driver->GetPid(), s_lastDirAddr);
        // [CHANGED] no Paradise branch — Drag Headshot never takes a
        // Paradise point anymore (slot removed from the registry).
        (void)pd;
        if(twt && s_lastTwtHandle)
            AimHookState::TwtHwbpDisarm(twt, s_lastTwtHandle);
        s_lastDirAddr   = 0;
        s_lastTwtHandle = 0;
        g_DragHSTriggered = false;
    };

    while(dragHeadshotThreadRunning){
        g_ShowDragHSCircle = DragHeadshot; // circle visible whenever the feature itself is on

        if(!ix_feat_aim() || !ix_rt_ready() || !DragHeadshot){
            disarm_dh();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        uintptr_t lp = 0;
        {
            std::lock_guard<std::mutex> lk(g_SA_mtx);
            lp = g_SA_lp;
        }
        uintptr_t currentTarget = g_CurrentBestTarget;
        if(!lp || !currentTarget){
            disarm_dh();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        uintptr_t neckNode = Read<uintptr_t>(currentTarget + BoneOffsets::Neck());
        if(!neckNode){
            // Neck offset not resolved (server JSON missing "Neck", or
            // zero on this build) -- fail closed, do NOT fall back to
            // Head/Chest: this feature was asked for specifically as
            // its own neck-targeting behavior, not "whatever bone is
            // available".
            disarm_dh();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        Vector3 neckWorld = NodePos(neckNode);
        if(neckWorld.X == 0.f && neckWorld.Y == 0.f && neckWorld.Z == 0.f){
            disarm_dh();
            std::this_thread::yield();
            continue;
        }

        // ── drag detection: is the player's OWN crosshair (screen
        // center) currently close to this on-screen neck point? ──
        // Replaces the earlier rotation-delta approach (unreliable in
        // testing) with a direct screen-space proximity check, same
        // projection math ESP already uses (W2S) but a fresh,
        // independent trigger -- not reusing Collider/Silent/Aimbot's
        // own target-distance logic, which is computed separately
        // inside their own loops above.
        D3DMatrix vm; bool vmOK = false;
        if(g_LastGoodIC > 0x10000ULL && g_LastGoodIC < 0x7FFFFFFFFFFFULL){
            D3DMatrix liveMX = Read<D3DMatrix>(g_LastGoodIC + PlayerOffsets::ViewMatrix());
            bool finite = std::isfinite(liveMX._44) && std::isfinite(liveMX._41) &&
                          std::isfinite(liveMX._42) && std::isfinite(liveMX._43) &&
                          std::isfinite(liveMX._11) && std::isfinite(liveMX._22);
            if(liveMX._44 != 0.0f && finite &&
               fabsf(liveMX._44) < 1e6f && fabsf(liveMX._41) < 1e8f){
                vm = liveMX; vmOK = true;
            }
        }
        if(!vmOK && g_HasLastCam){ vm = g_LastGoodMX; vmOK = true; }
        if(!vmOK){
            disarm_dh();
            std::this_thread::yield();
            continue;
        }

        Vector3 screenPt = W2S(vm, neckWorld);
        float sw = (float)abs_ScreenX, sh = (float)abs_ScreenY;
        if(!OnSc(screenPt, sw, sh)){
            disarm_dh();
            std::this_thread::yield();
            continue;
        }

        float dx = screenPt.X - sw/2.f;
        float dy = screenPt.Y - sh/2.f;
        float screenDist = sqrtf(dx*dx + dy*dy);
        float radiusPx = (g_DragHSRadiusPct/100.f) * (sh/2.f);

        if(screenDist > radiusPx){
            // Neck is on screen but not close enough to the crosshair
            // yet -- this is the "haven't dragged there yet" case.
            disarm_dh();
            std::this_thread::yield();
            continue;
        }

        g_DragHSTriggered = true;

        uintptr_t aimingInfo = Read<uintptr_t>(lp + PlayerOffsets::HitObjInfo());
        if(!aimingInfo){
            std::this_thread::yield();
            continue;
        }

        Vector3 targetPos = neckWorld;
        if(targetPos.X == 0.f && targetPos.Y == 0.f && targetPos.Z == 0.f){
            std::this_thread::yield();
            continue;
        }

        Vector3 startPos = Read<Vector3>(aimingInfo + PlayerOffsets::AmmoBase());
        Vector3 dir;
        dir.X = targetPos.X - startPos.X;
        dir.Y = targetPos.Y - startPos.Y;
        dir.Z = targetPos.Z - startPos.Z;

        uintptr_t dirAddr = aimingInfo + PlayerOffsets::SilentAimDir();
        RtHookDriver*    rth = g_driver ? g_driver->GetRtHook()   : nullptr;
        paradise_driver*  pd = g_driver ? g_driver->GetParadise() : nullptr;
        TwtDriver*       twt = g_driver ? g_driver->GetTwT()      : nullptr;

        if(rth){
            if(AimHookState::HwbpArmSilentDir(rth, dirAddr, dir)){
                s_lastDirAddr = dirAddr;
            } else {
                Write<Vector3>(dirAddr, dir);
            }
        } else if(pd){
            // [CHANGED] Drag Headshot takes NO Paradise hardware
            // breakpoint. Paradise's debug registers belong to Aim
            // Silent and Hardware BT only; this thread doesn't start
            // anymore anyway (see UI/SourceMenu.h removal notes), and if
            // it ever did it writes directly instead of competing for a
            // point-table entry.
            Write<Vector3>(dirAddr, dir);
            s_lastDirAddr = 0;
        } else if(twt){
            bool need_rearm = (s_lastDirAddr != dirAddr) || (s_lastTwtHandle == 0);
            if(need_rearm){
                if(s_lastTwtHandle)
                    AimHookState::TwtHwbpDisarm(twt, s_lastTwtHandle);
                uint64_t handle = 0;
                if(AimHookState::TwtHwbpArmSilentDir(twt, g_driver->GetPid(), dirAddr, dir, handle)){
                    s_lastTwtHandle = handle;
                    s_lastDirAddr   = dirAddr;
                } else {
                    Write<Vector3>(dirAddr, dir);
                    s_lastTwtHandle = 0;
                }
            } else if(!AimHookState::TwtHwbpUpdateSilentDir(twt, s_lastTwtHandle, dir)) {
                Write<Vector3>(dirAddr, dir);
            }
        } else {
            Write<Vector3>(dirAddr, dir);
        }

        std::this_thread::yield();
    }
}

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
            // [CHANGED] Rotation no longer takes a Paradise hardware
            // breakpoint. The Paradise debug registers are reserved
            // entirely for Aim Silent and Hardware BT now, so this path
            // is the plain write-on-change + jitter it always was on
            // every non-RT-Hook driver — no HWBP arm, nothing that can
            // disturb the two slots the registry owns.
            uintptr_t rotAddr = lp + PlayerOffsets::Rotation();
            if(!hook_init_done || AimHookState::ab_hook_addr.load() != rotAddr){
                AimHookState::ab_hook_addr.store(rotAddr);
                hook_init_done = true;
            }
            Write<Quaternion>(lp + PlayerOffsets::Rotation(), nextRot);
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
    static uint64_t  s_lastTwtHandle = 0; // [NEW] TwT driver breakpoint handle (Option 14)
    static AimHookState::ColliderHumanizer s_humanizer;

    // Helper lambda to disarm RT Hook, Paradise and TwT HWBPs
    auto disarm_all = [&](){
        RtHookDriver*   rth = g_driver ? g_driver->GetRtHook()   : nullptr;
        paradise_driver* pd = g_driver ? g_driver->GetParadise() : nullptr;
        TwtDriver*      twt = g_driver ? g_driver->GetTwT()      : nullptr;
        if(rth && s_lastHwbpAddr)
            rth->DisableHwBp(g_driver->GetPid(), s_lastHwbpAddr);
        // [CHANGED] Collider Lock holds no Paradise point anymore, so
        // there is nothing of ours in Paradise's table to disarm here.
        // The injected stub (if one were ever alive) still owns its own
        // arm/disarm, so it still gets told to stop.
        if(pd && s_lastHwbpAddr){
            if(g_ColliderIpc && g_ColliderIpc->stubAlive.load(std::memory_order_acquire)){
                g_ColliderIpc->active = 0;
                g_ColliderIpc->generation.fetch_add(1, std::memory_order_release);
            }
        }
        if(twt && s_lastTwtHandle)
            AimHookState::TwtHwbpDisarm(twt, s_lastTwtHandle);
        s_lastHwbpAddr  = 0;
        s_lastCollider  = 0;
        s_lastEnemy     = 0;
        s_lastTwtHandle = 0;
    };

    // [NEW — "collider not working" diagnostics] Edge-triggered prints
    // (only when the state actually changes, never spamming every
    // poll) so the next test shows exactly which stage is failing
    // instead of guessing again: no target selected at all vs a
    // target but no collider field vs the humanizer gate holding vs
    // an actual HWBP-arm failure.
    static bool s_dbgNoFeatureLogged  = false;
    static bool s_dbgNoTargetLogged   = false;
    static bool s_dbgNoColliderLogged = false;
    static bool s_dbgGateBlocked      = false;

    while(aimlockNewThreadRunning){
        if(!ix_feat_aim() || !ix_rt_ready() || !AimLock){
            if(AimLock && !s_dbgNoFeatureLogged){
                printf("[INX] collider-lock: checkbox is on but blocked — ix_feat_aim()=%d ix_rt_ready()=%d\n",
                       (int)ix_feat_aim(), (int)ix_rt_ready());
                fflush(stdout);
                s_dbgNoFeatureLogged = true;
            }
            disarm_all();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        s_dbgNoFeatureLogged = false;

        uintptr_t enemy = g_CurrentBestTarget;
        if(!enemy){
            if(!s_dbgNoTargetLogged){
                printf("[INX] collider-lock: on, but no target selected (g_CurrentBestTarget == 0)\n");
                fflush(stdout);
                s_dbgNoTargetLogged = true;
            }
            disarm_all();
            std::this_thread::sleep_for(std::chrono::milliseconds(AimHookState::alJitter()));
            continue;
        }
        s_dbgNoTargetLogged = false;

        uintptr_t HedColider = Read<uintptr_t>(enemy + PlayerOffsets::ColliderNode());
        if(!HedColider){
            if(!s_dbgNoColliderLogged){
                printf("[INX] collider-lock: target acquired (0x%llx) but ColliderNode read is 0\n",
                       (unsigned long long)enemy);
                fflush(stdout);
                s_dbgNoColliderLogged = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(AimHookState::alJitter()));
            continue;
        }
        s_dbgNoColliderLogged = false;

        // [NEW — humanized engagement gate] Decides WHEN to engage this
        // target (reaction delay / per-target skip chance / streak
        // cooldown) — never touches HedColider itself, which stays the
        // real value just read from the game above. While this says
        // "not yet" / "skip", treat it exactly like no valid target:
        // disarm whatever was armed and wait for the next poll.
        if(!s_humanizer.ShouldEngage(enemy)){
            if(!s_dbgGateBlocked){
                printf("[INX] collider-lock: target+collider valid, humanizer gate holding "
                       "(reaction delay / skip / cooldown)\n");
                fflush(stdout);
                s_dbgGateBlocked = true;
            }
            disarm_all();
            std::this_thread::sleep_for(std::chrono::milliseconds(AimHookState::alJitter()));
            continue;
        }
        if(s_dbgGateBlocked){
            printf("[INX] collider-lock: humanizer gate passed, engaging target 0x%llx\n",
                   (unsigned long long)enemy);
            fflush(stdout);
            s_dbgGateBlocked = false;
        }

        RtHookDriver* rth = g_driver ? g_driver->GetRtHook() : nullptr;

        // [NEW] Which driver tier the collider lock is actually running
        // on — printed once (edge-triggered), not every poll, since
        // this only changes if the driver connection itself changes.
        {
            static int s_dbgLastTier = -1; // 0=rth 1=pd 2=twt 3=fallback
            int tier = rth ? 0 : ((g_driver && g_driver->GetParadise()) ? 1 :
                       ((g_driver && g_driver->GetTwT()) ? 2 : 3));
            if(tier != s_dbgLastTier){
                static const char* kTierNames[] = {
                    "RT Hook HWBP", "Paradise HWBP", "TwT HWBP", "fallback write-on-change"
                };
                printf("[INX] collider-lock: driver tier = %s\n", kTierNames[tier]);
                fflush(stdout);
                s_dbgLastTier = tier;
            }
        }

        // ── RT Hook PTE tier (preferred — no debug registers touched) ──
        // Same gating as UpdateSilentAimFast()'s PTE tier above: only
        // engages once ResolveColliderSetter() returns a real address
        // (AOB pattern + register convention unknown — see the
        // AimHookState comment near ResolveSilentDirSetter() for the
        // exact recipe). Until then this is a no-op and every path
        // below runs exactly as before this commit.
        uintptr_t colliderSetter = rth ? AimHookState::ResolveColliderSetter() : 0;
        if(rth && colliderSetter){
            // Placeholder — not reachable yet (Resolve* returns 0).
            // Wire AimHookState::RthSetCollider(rth, HedColider) here
            // once that helper exists (see comment above).
        } else if(rth){
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
                    printf("[INX] collider-lock: RT Hook HwbpArmCollider() failed — falling back to direct write\n");
                    fflush(stdout);
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
                // ── Paradise tier ──
                // [CHANGED] Collider Lock takes NO Paradise hardware
                // breakpoint anymore. Paradise's debug registers are
                // reserved for Aim Silent and Hardware BT; this feature
                // used to arm its own point in the same shared table,
                // which is exactly what kept knocking those two out. It
                // now uses the direct write it already fell back to.
                //
                // The injected collider_stub.so path is untouched: if a
                // stub is ever alive it owns its own hwbp calls from
                // inside the game's PID, which never touches this
                // process's Paradise point table.
                bool useStub = g_ColliderInternalEnabled && g_ColliderIpc &&
                               g_ColliderIpc->stubAlive.load(std::memory_order_acquire);

                bool need_rearm = (enemy     != s_lastEnemy)   ||
                                  (HedColider != s_lastCollider)||
                                  (s_lastHwbpAddr == 0);
                if(need_rearm){
                    if(useStub){
                        g_ColliderIpc->colliderField   = (uint64_t)colliderField;
                        g_ColliderIpc->headColliderVal = (uint64_t)HedColider;
                        g_ColliderIpc->active          = 1;
                        g_ColliderIpc->generation.fetch_add(1, std::memory_order_release);
                        s_lastHwbpAddr = colliderField;
                        s_lastEnemy    = enemy;
                        s_lastCollider = HedColider;
                    } else {
                        Write<uintptr_t>(colliderField, HedColider);
                        s_lastHwbpAddr = 0;
                        s_lastEnemy    = enemy;
                        s_lastCollider = HedColider;
                    }
                } else {
                    // Target pointer changed — push the new value
                    if(HedColider != s_lastCollider){
                        if(useStub){
                            g_ColliderIpc->colliderField   = (uint64_t)colliderField;
                            g_ColliderIpc->headColliderVal = (uint64_t)HedColider;
                            g_ColliderIpc->active          = 1;
                            g_ColliderIpc->generation.fetch_add(1, std::memory_order_release);
                        } else {
                            Write<uintptr_t>(colliderField, HedColider);
                        }
                        s_lastCollider = HedColider;
                    }
                }
            } else if(TwtDriver* twt = (g_driver ? g_driver->GetTwT() : nullptr)) {
                // ── TwT HWBP tier: kernel intercepts AimCollider write ──
                // Same override convention as RT Hook/Paradise above
                // (write breakpoint on the pointer field, hit-time x0
                // override replaced with the head-collider pointer) —
                // see AimHookState::TwtHwbpArmCollider/TwtHwbpDisarm.
                bool need_rearm = (enemy     != s_lastEnemy)    ||
                                  (HedColider != s_lastCollider)  ||
                                  (s_lastTwtHandle == 0);

                if(need_rearm){
                    if(s_lastTwtHandle)
                        AimHookState::TwtHwbpDisarm(twt, s_lastTwtHandle);

                    uint64_t handle = 0;
                    if(AimHookState::TwtHwbpArmCollider(twt, g_driver->GetPid(),
                                                         colliderField, HedColider, handle)){
                        s_lastTwtHandle = handle;
                        s_lastEnemy     = enemy;
                        s_lastCollider  = HedColider;
                    } else {
                        printf("[INX] collider-lock: TwT TwtHwbpArmCollider() failed — falling back to direct write\n");
                        fflush(stdout);
                        Write<uintptr_t>(colliderField, HedColider);
                        s_lastTwtHandle = 0;
                    }
                } else if(HedColider != s_lastCollider) {
                    if(!AimHookState::TwtHwbpUpdateCollider(twt, s_lastTwtHandle, HedColider))
                        Write<uintptr_t>(colliderField, HedColider); // handle went stale — direct write this frame
                    s_lastCollider = HedColider;
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
    /* [CHANGED] Was a plain Write<Vector3> directly into SilentAimDir —
     * a visible, out-of-band memory write every frame, exactly the kind
     * of thing the Aim Silent tier was explicitly built to avoid. Now
     * uses the same driver-selected HWBP dispatch as UpdateSilentAimFast:
     * Paradise connected -> always Paradise HWBP (its OWN slot,
     * the SLOT_BT_* pair, never SLOT_SILENT_* — see the registry
     * comment above PdHwbpArmDir); Paradise
     * not connected + KPM connected -> always KPM HWBP tier. No runtime
     * fallback between them, and no plain-write fallback below either —
     * if the selected driver's arm fails this frame, the frame is
     * skipped, full stop. */
    static uintptr_t s_lastDirAddr = 0;
    static bool      s_lastViaKpm  = false;
    static int       s_kpmSlots[3] = { -1, -1, -1 };

    auto disarm_bt = [&](){
        if(!s_lastDirAddr) return;
        if(s_lastViaKpm){
            KpmDriver* kpm = g_driver ? g_driver->GetKpm() : nullptr;
            AimHookState::KpmHwbpDisarm(kpm, s_kpmSlots);
        } else {
            paradise_driver* pd = g_driver ? g_driver->GetParadise() : nullptr;
            AimHookState::PdHwbpDisarmDir(pd, AimHookState::ParadisePoints::SLOT_BT_XY);
        }
        s_lastDirAddr = 0;
        s_lastViaKpm  = false;
    };

    while(hwbtThreadRunning){
        if(!ix_feat_aim() || !ix_rt_ready() || !HardwareBT){
            disarm_bt();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        /* [FIX] Aim Silent and Hardware BT write the SAME field
         * (aimingInfo + SilentAimDir()). Both arming a hardware
         * watchpoint on one address means two debug registers on the
         * same bytes, both firing, each overriding x0-x2 with its own
         * direction — the last one to fire wins and the result is
         * neither feature behaving predictably. ARM64 only has a handful
         * of watchpoint registers, so it also burns one for nothing.
         * Aim Silent owns that field when it is on; Hardware BT releases
         * its own point and stands down until Aim Silent is off. This is
         * arbitration over one shared address, not a driver fallback —
         * the selected driver never changes. */
        if(AimSilent || BodyKill){
            disarm_bt();
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
            if (chestRate == 1) {
                targetPos.Y += 0.04f;
            } else if (chestRate == 2) {
                targetPos.Y -= 0.08f;
            } else if (chestRate == 3) {
                // [RE-ADDED] Legit Mode. This case fell through to no offset
                // at all before — not a 70/30 split, nothing, which is why
                // it never looked like anything was happening. A human
                // player hits head most of the time and body the rest, not
                // a fixed ratio repeated every shot on the same spot, so the
                // split is decided ONCE per target lock and held — re-rolling
                // it every tick would read as the aim point visibly
                // twitching between head and chest, which is the opposite of
                // "legit". Same offsets as the Head/Body modes above, so
                // switching between modes mid-game doesn't change behaviour,
                // only how it is chosen.
                static uintptr_t legitLastTarget = 0;
                static bool      legitBodyThisTarget = false;
                if (currentTarget != legitLastTarget) {
                    legitLastTarget = currentTarget;
                    legitBodyThisTarget = (rand() % 100) < 30; // 30% body, 70% head
                }
                if (legitBodyThisTarget) targetPos.Y -= 0.08f;
                else                     targetPos.Y += 0.04f;
            }
            Vector3 startPos = Read<Vector3>(aimingInfo + PlayerOffsets::AmmoBase());
            Vector3 dir;
            dir.X = targetPos.X - startPos.X;
            dir.Y = targetPos.Y - startPos.Y;
            dir.Z = targetPos.Z - startPos.Z;

            uintptr_t dirAddr = aimingInfo + PlayerOffsets::SilentAimDir();
            paradise_driver* pd  = g_driver ? g_driver->GetParadise() : nullptr;
            KpmDriver*       kpm = g_driver ? g_driver->GetKpm()      : nullptr;

            bool viaKpm = (!pd && kpm && kpm->is_ready());
            bool need_rearm = (s_lastDirAddr != dirAddr) || (s_lastViaKpm != viaKpm);
            if(need_rearm && s_lastDirAddr)
                disarm_bt();

            if(pd){
                if(AimHookState::PdHwbpArmDir(
                       pd, AimHookState::ParadisePoints::SLOT_BT_XY, dirAddr, dir)){
                    s_lastDirAddr = dirAddr;
                    s_lastViaKpm  = false;
                } else {
                    // [FIX] Same stale-armed-group release as in
                    // UpdateSilentAimFast — see the comment there.
                    AimHookState::PdHwbpDisarmDir(pd, AimHookState::ParadisePoints::SLOT_BT_XY);
                    s_lastDirAddr = 0; // arm failed this frame — skip, no fallback write
                }
            } else if(kpm){
                bool justArmed = (s_lastDirAddr != dirAddr) || !s_lastViaKpm;
                bool armed = justArmed
                    ? AimHookState::KpmHwbpArmSilentDir(kpm, dirAddr, dir, s_kpmSlots)
                    : AimHookState::KpmHwbpUpdateSilentDir(kpm, s_kpmSlots, dir);
                if(armed){
                    s_lastDirAddr = dirAddr;
                    s_lastViaKpm  = true;
                } else {
                    AimHookState::KpmHwbpDisarm(kpm, s_kpmSlots);
                    s_lastDirAddr = 0; // arm/update failed this frame — skip, no fallback write
                }
            } else {
                s_lastDirAddr = 0; // neither driver available this frame
            }
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

void StartDragHeadshotThread(){ /* [NEW] */
    if(!dragHeadshotThreadRunning){
        dragHeadshotThreadRunning = true;
        std::thread(UpdateDragHeadshotThread).detach();
    }
}
void StopDragHeadshotThread(){ dragHeadshotThreadRunning = false; }

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
    // [REMOVED] floating "pull enemy" button — Pull/AimMagnet feature removed.
    // AimMagnet and g_ExploitFloatUI are force-cleared everywhere they're
    // set (main.cpp, SourceMenu.h) so this overlay would never show anyway.
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
    // [NEW] Drag Headshot's own trigger-radius circle — same draw
    // helper (generic "draw a circle" primitive, not feature logic),
    // own independent radius/color state (g_DragHSRadiusPct/
    // g_DragHSTriggered, set by UpdateDragHeadshotThread()).
    if(g_ShowDragHSCircle)
        DrawFOVCircle(dl, sw, sh, (g_DragHSRadiusPct/100.f) * (sh/2.f), g_DragHSTriggered);

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