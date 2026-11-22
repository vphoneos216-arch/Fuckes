#pragma once

#include "ImGui/imgui.h"

// TESLA menu — glass panel drawn with plain ImDrawList calls, no FBO and
// no custom shaders. Rendered flat (front-on): the original source tilted
// the whole panel in perspective, which read as crooked on a phone, so the
// view angles are pinned at zero and the projection collapses to identity.
//
// The namespace is still AetherUI: that is only the C++ name the engine
// links against. Everything the user sees says TESLA.
namespace AetherUI {

enum Page : int {
  PAGE_VISUAL = 0,   // ESP
  PAGE_AIM = 1,      // Aim Silent / Hardware BT
  PAGE_EXPLOIT = 2,  // Ghost / NoRecoil / NoReload / Normal Write
  PAGE_SETTINGS = 3,
  PAGE_THEME = 4,
  PAGE_COUNT = 5
};

// Every control on the feature pages renders through one of these
// pointers. AetherUI.cpp deliberately knows nothing about the engine's
// globals -- g_Cfg is file-static in main.cpp and could not be reached
// from here anyway. The owner fills in what it has; a null pointer
// means that row is simply not drawn, so no control is ever shown that
// is not actually wired to something.
struct Bindings {
  // ESP
  bool* showBox = nullptr;
  bool* showSkeleton = nullptr;
  bool* showName = nullptr;
  bool* showHealth = nullptr;
  bool* showDistance = nullptr;
  bool* showLine = nullptr;
  bool* showAimLine = nullptr;
  bool* is3600 = nullptr;
  bool* showFovCircle = nullptr;
  int*  textSize = nullptr;
  // ESP colours. The mode ints are the engine's boxColorMode /
  // lineColorMode (0 Static, 1 Health, 2 Distance, 3 Rainbow); the
  // colour pointers are &ImVec4.x, i.e. four consecutive floats, which
  // is what the engine already stores in boxCustomColor/lineCustomColor.
  int*   boxColorMode = nullptr;
  float* boxColor = nullptr;
  int*   lineColorMode = nullptr;
  float* lineColor = nullptr;

  // Aim — Aim Silent and Hardware BT only, by request. The remaining
  // aim flags stay bound here so they can be put back on the page in
  // one line, but nothing draws them right now.
  bool*  aimSilent = nullptr;
  bool*  hardwareBT = nullptr;
  bool*  bodyKill = nullptr;
  bool*  brutalBT = nullptr;
  bool*  dragHeadshot = nullptr;
  bool*  aimbot = nullptr;
  float* aimFov = nullptr;
  float* bodyKillHP = nullptr;
  int*   chestRateSave = nullptr;

  // Exploit
  // "Aim Point" is the engine's DragHeadshot: it arms only once the
  // target's neck comes inside its radius of the crosshair, which is
  // why the caption under it says it turns itself on when you drag.
  bool* aimPoint = nullptr;

  bool* ghostHack = nullptr;
  bool* noRecoil = nullptr;
  bool* noReload = nullptr;
  bool* floatGhost = nullptr;
  bool* normalWrite = nullptr;
  bool* soundMuted = nullptr;

  // License info for the Settings > Info card. Read-only: the owner
  // copies g_Srv (main.cpp, file-static behind g_SrvMtx) into buffers
  // of its own each frame and points these at them.
  const char* infoKey = nullptr;
  const char* infoSeller = nullptr;
  const char* infoExpiry = nullptr;
  const int*  daysRemain = nullptr;
  const int*  daysTotal = nullptr;
  const int*  devUsed = nullptr;
  const int*  devMax = nullptr;
  // Seconds left on the key, recomputed every frame by the owner so the
  // Info card's countdown actually ticks. -1 means unknown.
  const long long* remainSecs = nullptr;

  // Normal Mode: no kernel driver was loaded, so the ESP section is
  // shown greyed with DRIVER REQUIRED and the Inject button appears.
  const bool* normalMode = nullptr;
  const bool* injectApplied = nullptr;
};

Bindings& GetBindings();

struct State {
  int page = PAGE_VISUAL;
  bool darkSkin = false;  // false = light glass, true = dark glass
  // 1 = Mist, whose accent is exactly the blue/teal the menu shipped
  // with, so the default look is unchanged and the other two cards are
  // the actual alternatives.
  int themeIdx = 1;

  float menuScale = 1.00f;

  // Set by the menu, cleared by the owner once it has acted on them.
  // The menu itself cannot save or kill the process -- both of those
  // live in the engine, so it only asks.
  bool requestExit = false;
  bool requestSave = false;
  bool requestInject = false;

  bool open = true;
};

State& GetState();

// Called every frame; screenW/H are full-screen pixels.
void Draw(float screenW, float screenH, float uiScale);

// Touch hit test: does this point land on the panel (or the collapsed pill)?
bool PointBlocksTouch(float x, float y);

}  // namespace AetherUI
