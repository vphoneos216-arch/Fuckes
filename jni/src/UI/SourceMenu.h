// ============================================================
//  SourceMenu.h
//  Direct port of the source project's 雨落/菜单.cpp UI.
//  Layout, sizes, colours, rounding, easing and ImDrawList
//  calls are reproduced exactly; only the config bindings are
//  mapped onto InvisibleX's existing g_Cfg / hook globals.
//  Backend is NOT touched by this file.
// ============================================================
#pragma once

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_widgets.h"
#include "ix_features.h"
#include "ix_rt_offsets.h"
#ifndef IX_PREVIEW_NO_AUDIO
#include "ix_audio.h"
#endif
#include <ctime>
#include <cmath>
#include <cstdlib>
#include <string>

// ── RED PALETTE ─────────────────────────────────────────────
// Red accent on a cool near-black. The neutral surfaces carry a trace of
// blue so they sit under the artwork without looking muddy, and the reds
// are stepped (bright -> base -> deep) so the accent has depth instead of
// one flat tone.
//
// Function-like colour macros: alpha is supplied per call site.
// Colour tokens only — no geometry, size, spacing or layout value
// is affected by anything in this block.
#define COL_ACCENT(a)        IM_COL32(232,  38,  58, (a))   // #E8263A core red
#define COL_ACCENT_HOVER(a)  IM_COL32(255,  74,  92, (a))   // #FF4A5C red light
#define COL_ACCENT_DEEP(a)   IM_COL32(150,  18,  32, (a))   // #961220 red deep
#define COL_ACCENT_ALT(a)    IM_COL32(255, 110,  80, (a))   // #FF6E50 ember
#define COL_COOL(a)          IM_COL32(118, 196, 160, (a))   // #76C4A0 status
#define COL_PANEL(a)         IM_COL32( 11,  11,  14, (a))   // #0B0B0E panel
#define COL_CARD(a)          IM_COL32( 22,  22,  27, (a))   // #16161B card surface
#define COL_CARD_INNER(a)    IM_COL32( 15,  15,  19, (a))   // #0F0F13 inner card
#define COL_BTN_IDLE(a)      IM_COL32( 32,  32,  39, (a))   // #202027 inactive button
#define COL_TOGGLE_OFF(a)    IM_COL32( 38,  38,  46, (a))   // #26262E toggle off
#define COL_TXT_HI(a)        IM_COL32(245, 245, 248, (a))   // primary text
#define COL_TXT_MID(a)       IM_COL32(168, 168, 178, (a))   // secondary text
#define COL_TXT_LOW(a)       IM_COL32(112, 112, 122, (a))   // muted text
#define COL_SHADOW(a)        IM_COL32(  0,   0,   0, (a))   // soft shadow
#define COL_HAIRLINE(a)      IM_COL32( 52,  52,  62, (a))   // subtle border

// ── LAYOUT GRID ─────────────────────────────────────────────
// The panel is split into a content column and an art zone. Everything
// interactive lives inside the column; nothing is drawn over the artwork.
//
// The pages used to centre 780-wide cards in an 860-wide panel, so they
// covered 91% of it. The artwork was then almost entirely hidden behind
// them and only a sliver showed at the edge, which is why enabling the
// background made the menu look broken rather than finished.
//
// Values are in design units (SM_W x SM_H) and multiplied by `scale`.
#define LAY_COL_X     32.0f    // content column left edge
#define LAY_COL_W    516.0f    // content column width  (0.60 of SM_W)
#define LAY_BODY_Y   100.0f    // first row below the header
#define LAY_BODY_B   524.0f    // bottom of the content area
#define LAY_GAP       14.0f    // gap between cards
#define LAY_ROUND     12.0f    // card corner radius

// ── Soft shadow helper (purely visual, drawn behind surfaces) ──
static inline void SM_SoftShadow(ImDrawList* dl, ImVec2 a, ImVec2 b,
                                 float rnd, float alpha) {
    for (int i = 4; i >= 1; --i) {
        float o = (float)i * 3.0f;
        int   al = (int)((10.0f - i * 1.6f) * alpha);
        if (al <= 0) continue;
        dl->AddRectFilled(ImVec2(a.x - o, a.y - o), ImVec2(b.x + o, b.y + o),
                          COL_SHADOW(al), rnd + o);
    }
}

// ── Easing helpers — source 雨落/定义.cpp ────────────────────
static inline float SM_Lerp(float a, float b, float t) {
    t = t < 0.0f ? 0.0f : t;
    t = t > 1.0f ? 1.0f : t;
    return a + (b - a) * t;
}
static inline float EaseInOutQuad(float t) {
    return t < 0.5f ? 2.0f * t * t : 1.0f - powf(-2.0f * t + 2.0f, 2.0f) / 2.0f;
}
static inline float EaseOutBack(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f);
}


// ── INX mark ────────────────────────────────────────────────
// Drawn with the draw list rather than loaded from an image, so it stays
// crisp at any panel scale and costs nothing to ship.
//
// A pointy-top hexagon in the accent red over a dark fill, a soft outer
// glow built from concentric outlines, a tick on the top and bottom vertex,
// and the wordmark with the X in accent red.
static inline void SM_HexPoints(ImVec2* out, ImVec2 c, float r) {
    for (int i = 0; i < 6; ++i) {
        const float a = (3.14159265f / 180.0f) * (60.0f * (float)i - 90.0f);
        out[i] = ImVec2(c.x + r * cosf(a), c.y + r * sinf(a));
    }
}

static inline void SM_DrawInxMark(ImDrawList* dl, ImVec2 center, float size, float a01) {
    if (a01 <= 0.0f) return;
    const int   A    = (int)(255.0f * a01);
    const float r    = size * 0.5f * 0.850f;   // hex radius, leaves room for the glow
    const float th   = size * 0.019f;          // outline thickness
    ImVec2 pts[6];

    // Outer glow: concentric outlines, faintest first.
    for (int i = 4; i >= 1; --i) {
        SM_HexPoints(pts, center, r + (float)i * size * 0.016f);
        const int ga = (int)(14.0f * (float)(5 - i) * a01);
        if (ga <= 0) continue;
        dl->AddPolyline(pts, 6, COL_ACCENT(ga), ImDrawFlags_Closed, th * 0.7f);
    }

    // Body + accent border.
    SM_HexPoints(pts, center, r);
    dl->AddConvexPolyFilled(pts, 6, COL_CARD_INNER(A));
    dl->AddPolyline(pts, 6, COL_ACCENT(A), ImDrawFlags_Closed, th);

    // Ticks across the top and bottom vertices.
    const float tw = size * 0.082f;
    dl->AddLine(ImVec2(center.x - tw, center.y - r), ImVec2(center.x + tw, center.y - r),
                COL_ACCENT(A), th * 1.25f);
    dl->AddLine(ImVec2(center.x - tw, center.y + r), ImVec2(center.x + tw, center.y + r),
                COL_ACCENT(A), th * 1.25f);

    // Wordmark: "IN" white, "X" accent, measured as one unit so it is centred
    // whatever font is bound.
    const float   fs = size * 0.253f;
    ImFont* const f  = ImGui::GetFont();
    const float wIN  = f->CalcTextSizeA(fs, FLT_MAX, -1.0f, "IN").x;
    const float wX   = f->CalcTextSizeA(fs, FLT_MAX, -1.0f, "X").x;
    const float sx   = center.x - (wIN + wX) * 0.5f;
    const float ty   = center.y - fs * 0.5f;
    const float sh   = size * 0.013f;          // shadow offset

    dl->AddText(NULL, fs, ImVec2(sx + sh, ty + sh),       IM_COL32(0, 0, 0, (int)(140 * a01)), "IN");
    dl->AddText(NULL, fs, ImVec2(sx + wIN + sh, ty + sh), IM_COL32(0, 0, 0, (int)(140 * a01)), "X");
    dl->AddText(NULL, fs, ImVec2(sx, ty),                 COL_TXT_HI(A),  "IN");
    dl->AddText(NULL, fs, ImVec2(sx + wIN, ty),           COL_ACCENT(A),  "X");
}

// ── State — source 菜单.cpp ─────────────────────────────────
static int   现在页面     = 0;

#include "UI/NoticeUI.h"   // 致用户 card + floating nav bar (screenshot rebuild)
static bool  已初始化随机 = false;

enum 动画阶段 {
    动画_无,
    动画_关闭_缩小,
    动画_关闭_小球停留,
    动画_关闭_消失,
    动画_打开_小球出现,
    动画_打开_小球停留,
    动画_打开_放大
};
static 动画阶段 当前动画   = 动画_无;
static float    动画进度   = 0.0f;
static float    停留计时器 = 0.0f;
static bool     窗口显示状态 = false;
static ImVec2   窗口原始位置;
static ImVec2   窗口原始大小;

// ── Open/close animation state ────────────────────────────
// openAnim: 0.0 = closed, 1.0 = fully open
// Driven by deltaTime each frame for a smooth ease-in/out feel
static float    openAnim       = 0.0f;   // current interpolation value
static bool     prevVisible    = false;  // tracks transitions

// Exact source dimensions
const float SM_W        = 860.0f;
const float SM_H        = 560.0f;
const float SM_BallSize = 80.0f;

// Daily-quote list — source 欢迎文案列表
static const char* 欢迎文案列表[] = {
    "Aim true, stay unseen",
    "Precision over power",
    "Silence is the sharpest edge",
    "Every shot counts",
    "Patience wins the round",
    "Control the fight, not the clock",
    "Steady hands, clear mind",
    "The quiet ones land first",
    "Read the map, own the map",
    "Position beats reflex",
    "Calm under fire",
    "One breath, one shot",
    "Discipline is the real advantage",
    "Play the angles, not the odds",
    "Focus is a weapon",
    "Finish what you start"
};
static const int  文案总数         = sizeof(欢迎文案列表) / sizeof(欢迎文案列表[0]);
static const char* 当前固定欢迎语  = 欢迎文案列表[0];

// ── Public control API ──────────────────────────────────────
inline bool IsMenuWindowVisible() {
    return 窗口显示状态;
}
/* Instant open/close — no INX ball animation */
inline void ShowMenuWindow() {
    if (!窗口显示状态) {
        窗口显示状态 = true;
        ix_sound_welcome_once();   // once per process, on first open
        当前动画     = 动画_无;
        动画进度     = 1.0f;
        停留计时器   = 0.0f;
    }
}
inline void HideMenuWindow() {
    if (窗口显示状态) {
        窗口显示状态 = false;
        当前动画     = 动画_无;
        动画进度     = 0.0f;
        停留计时器   = 0.0f;
    }
}
static inline void 触发关闭动画(const ImVec2& currentPos) {
    (void)currentPos;
    窗口显示状态 = false;
    当前动画     = 动画_无;
    动画进度     = 0.0f;
    停留计时器   = 0.0f;
}

// ============================================================
//  RenderSingleMenuWindow — exact source structure
// ============================================================
inline void RenderSingleMenuWindow() {
    if (!已初始化随机) {
        srand((unsigned int)time(NULL));
        当前固定欢迎语 = 欢迎文案列表[rand() % 文案总数];
        已初始化随机   = true;
    }

    ImGuiIO& io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    if (deltaTime <= 0.0f || deltaTime > 0.1f) deltaTime = 0.016f; // clamp

    /* No open/close animation: the panel is simply there or not there.
       openAnim jumps straight to its target, which makes animScale 1.0 and
       animAlpha 1.0 below, so the panel is drawn at full size and full
       opacity on the very first frame after the toggle. */
    openAnim    = 窗口显示状态 ? 1.0f : 0.0f;
    prevVisible = 窗口显示状态;
    (void)deltaTime;

    当前动画 = 动画_无;
    动画进度 = 1.0f;

    if (openAnim < 0.01f) return; // nothing to draw yet

    // ── REAL VISIBLE SCREEN ──────────────────────────────────
    // main.cpp creates the overlay surface as a SQUARE of the long
    // edge (native_window_screen_y == abs_ScreenX), so io.DisplaySize
    // is the square surface, NOT the visible screen. Centering on it
    // pushes the panel below the display. Use live display metrics.
    float scrW = (float)displayInfo.width;
    float scrH = (float)displayInfo.height;
    if (scrW < 1.0f || scrH < 1.0f) {            // fallback before first screen_config()
        scrW = (float)abs_ScreenX;
        scrH = (float)abs_ScreenY;
    }
    if (scrW < 1.0f || scrH < 1.0f) {            // last-resort fallback
        scrW = io.DisplaySize.x;
        scrH = io.DisplaySize.y;
    }

    // ── NEW SHELL ────────────────────────────────────────────
    // The 致用户 card, counter pill, push button, status block and the
    // floating nav bar, rebuilt from the reference screenshot. It owns
    // the nav; the old text tab bar below still works and stays in sync
    // through 现在页面, so nothing that already worked was removed.
    {
        int nuiPage = 现在页面;
        if (nuiPage < 0) nuiPage = 0;
        if (nuiPage > 5) nuiPage = 5;
        // Counts are shown as the screenshot shows them. They are not
        // wired to a live source yet: this build has no confirmed
        // player/bot count global outside Backup/, and inventing one
        // would put a made-up number on screen.
        const int nuiPlayers = 0, nuiBots = 0;
        if (NuiRenderShell(scrW, scrH, nuiPage, nuiPlayers, nuiBots,
                           "Checking for updates...",
                           "秋山公益追锁  @xsIboynb")) {
            // push button tapped — no action bound yet
        }
        // Page 5 (gear) has no body of its own; it shares Config.
        现在页面 = (nuiPage > 4) ? 4 : nuiPage;
    }

    // ── UNIFORM FIT ──────────────────────────────────────────
    // Scale the whole panel down uniformly if the display is smaller
    // than the design size, keeping 3% safe-area margin on each side.
    // Proportions are untouched: every element already derives from
    // `scale = windowSize.x / SM_W` further below.
    float fitW = (scrW * 0.94f) / SM_W;
    float fitH = (scrH * 0.94f) / SM_H;
    float fit  = (fitW < fitH) ? fitW : fitH;
    if (fit > 1.0f) fit = 1.0f;

    const float PW   = SM_W * fit;               // effective panel width
    const float PH   = SM_H * fit;               // effective panel height
    const float PB   = SM_BallSize * fit;        // effective ball size
    const float PR   = 26.0f * fit;              // effective corner radius

    /* Open animation: scale up from 88% + fade in */
    float easedAnim   = EaseOutBack(openAnim < 1.0f ? openAnim : 1.0f);
    // clamp EaseOutBack overshoot so size never exceeds actual PW/PH
    if (easedAnim > 1.0f) easedAnim = 1.0f;
    float animScale   = 0.88f + 0.12f * easedAnim;   // 88% → 100%
    float animAlpha   = openAnim;                      // 0 → 1 (linear fade)

    float aPW   = PW   * animScale;
    float aPH   = PH   * animScale;

    ImVec2 screenCenter = ImVec2(scrW * 0.5f, scrH * 0.5f);
    ImVec2 windowPos    = ImVec2(screenCenter.x - aPW * 0.5f, screenCenter.y - aPH * 0.5f);
    ImVec2 windowSize   = ImVec2(aPW, aPH);
    float  alpha        = animAlpha;
    float  rounding     = PR * animScale;
    float  contentAlpha = animAlpha;
    窗口原始位置 = windowPos;
    窗口原始大小 = windowSize;
    (void)PB;

    // ── SAFE-AREA CLAMP ──────────────────────────────────────
    // Guarantees the panel never leaves the visible display on any
    // resolution / aspect ratio / orientation.
    if (windowPos.x < 0.0f) windowPos.x = 0.0f;
    if (windowPos.y < 0.0f) windowPos.y = 0.0f;
    if (windowPos.x + windowSize.x > scrW) windowPos.x = scrW - windowSize.x;
    if (windowPos.y + windowSize.y > scrH) windowPos.y = scrH - windowSize.y;
    if (windowPos.x < 0.0f) windowPos.x = 0.0f;
    if (windowPos.y < 0.0f) windowPos.y = 0.0f;

    ImGui::SetNextWindowPos(windowPos);
    ImGui::SetNextWindowSize(windowSize);
    ImGui::SetNextWindowBgAlpha(0.12f * animAlpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

    bool open = true;
    if (ImGui::Begin("InvisibleX", &open,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p     = ImGui::GetWindowPos();
        ImVec2 p_end = ImVec2(p.x + windowSize.x, p.y + windowSize.y);

        /* PANEL only — ball / INX animation permanently removed */
        if (contentAlpha > 0.01f) {
            float scale    = windowSize.x / SM_W;
            float uiAlpha  = contentAlpha * alpha;

            // Glass panel — soft shadow, fill, subtle hairline border
            SM_SoftShadow(dl, p, p_end, rounding, uiAlpha);
            dl->AddRectFilled(p, p_end, COL_PANEL((int)(242 * uiAlpha)), rounding);

            // ── Background artwork ───────────────────────────────
            // UI/images/Background.h was embedded and loaded into g_BgTex
            // but never drawn anywhere, which is why the artwork never
            // appeared. It is framed deliberately: the character's face
            // sits at about (0.44, 0.30) of the source, and the UV window
            // below places it in the open right-hand side of the panel,
            // clear of the content column. The window is derived from the
            // panel's own aspect ratio, so it holds on any screen.
            if (g_BgTex) {
                const float FACE_U = 0.44f, FACE_V = 0.30f;  // face in source UV
                const float TARGET_X = 0.72f, TARGET_Y = 0.34f;  // where it lands
                // Show enough of the composition that the hair and blossoms
                // read too — a narrow span zoomed into the cheek and looked
                // like a crop accident rather than a backdrop.
                const float U_SPAN = 0.58f;

                const float srcW_px = U_SPAN * 1920.0f;
                const float srcH_px = srcW_px * (windowSize.y / windowSize.x);
                float v_span = srcH_px / 1080.0f;
                if (v_span > 1.0f) v_span = 1.0f;

                float u0 = FACE_U - TARGET_X * U_SPAN;
                float v0 = FACE_V - TARGET_Y * v_span;
                if (u0 < 0.0f) u0 = 0.0f;
                if (u0 > 1.0f - U_SPAN) u0 = 1.0f - U_SPAN;
                if (v0 < 0.0f) v0 = 0.0f;
                if (v0 > 1.0f - v_span) v0 = 1.0f - v_span;

                dl->AddImageRounded(g_BgTex, p, p_end,
                                    ImVec2(u0, v0), ImVec2(u0 + U_SPAN, v0 + v_span),
                                    IM_COL32(255, 255, 255, (int)(255 * uiAlpha)), rounding);

                // Scrim: nearly opaque over the content column, clearing to
                // almost nothing over the artwork, so text stays readable
                // without hiding the picture.
                // Two flat veils instead of a gradient. A stepped gradient
                // banded visibly across the panel, and a wide one hazed the
                // artwork out; flat fills cannot band at all.
                //
                //  1. a light veil over everything, so the art reads as a
                //     backdrop rather than competing with the controls
                //  2. a near-solid surface under the content column, so every
                //     card and label sits on an even background
                dl->AddRectFilled(p, p_end, COL_PANEL((int)(96 * uiAlpha)), rounding);

                const float colEdge = p.x + (LAY_COL_X + LAY_COL_W + 16.0f) * scale;
                dl->AddRectFilled(p, ImVec2(colEdge, p_end.y),
                                  COL_PANEL((int)(232 * uiAlpha)), rounding,
                                  ImDrawFlags_RoundCornersLeft);

                // A soft edge where the column meets the art, so the join does
                // not read as a hard line.
                dl->AddRectFilledMultiColor(
                    ImVec2(colEdge, p.y), ImVec2(colEdge + 56.0f * scale, p_end.y),
                    COL_PANEL((int)(232 * uiAlpha)), COL_PANEL(0),
                    COL_PANEL(0), COL_PANEL((int)(232 * uiAlpha)));

                // Keep the header and footer legible over the art.
                const int vt = (int)(92 * uiAlpha), vb = (int)(112 * uiAlpha);
                dl->AddRectFilledMultiColor(p, ImVec2(p_end.x, p.y + windowSize.y * 0.22f),
                                            COL_SHADOW(vt), COL_SHADOW(vt),
                                            COL_SHADOW(0), COL_SHADOW(0));
                dl->AddRectFilledMultiColor(ImVec2(p.x, p_end.y - windowSize.y * 0.22f), p_end,
                                            COL_SHADOW(0), COL_SHADOW(0),
                                            COL_SHADOW(vb), COL_SHADOW(vb));
            }

            // ── Drifting sakura ──────────────────────────────────
            // main.cpp already carries a particle system; like the artwork
            // it was written but never called from the menu. Clipped to the
            // panel so nothing drifts outside it.
            dl->PushClipRect(p, p_end, true);
            更新粒子系统(p, windowSize, true);
            绘制粒子系统(true);
            dl->PopClipRect();

            dl->AddRect(p, p_end, COL_HAIRLINE((int)(200 * uiAlpha)), rounding, 0, 1.2f);

            ImVec2 barMin = ImVec2(p.x, p.y);
            ImVec2 barMax = ImVec2(p.x + windowSize.x, p.y + 72 * scale);
            dl->AddRectFilled(barMin, barMax, IM_COL32(0, 0, 0, 0), rounding);

            // A hairline under the header with a short accent run at the left,
            // so the chrome reads as a separate band from the content.
            {
                const float hy = p.y + 72 * scale;
                dl->AddLine(ImVec2(p.x + 32 * scale, hy), ImVec2(p_end.x - 32 * scale, hy),
                            COL_HAIRLINE((int)(130 * uiAlpha)), 1.0f);
                dl->AddLine(ImVec2(p.x + 32 * scale, hy), ImVec2(p.x + 96 * scale, hy),
                            COL_ACCENT((int)(220 * uiAlpha)), 2.0f);
            }

            // Header title — "Invisible" white + "X" red.
            // 44px collided with the tab bar and hid the "X" behind the first
            // pill, so the title is smaller and the tabs are placed after its
            // measured width rather than at a fixed 250.
            float  titleSize  = 26.0f * scale;
            ImVec2 textPos    = ImVec2(p.x + 40 * scale, p.y + 14 * scale);
            const char* titlePart1 = "Invisible";
            const char* titlePart2 = "X";
            ImFont* titleFont = ImGui::GetFont();
            float tw_p1 = titleFont->CalcTextSizeA(titleSize, FLT_MAX, -1.f, titlePart1).x;
            // Shadow pass (single draw, full string)
            dl->AddText(NULL, titleSize, ImVec2(textPos.x + 2*scale, textPos.y + 2*scale),
                        IM_COL32(0,0,0,(int)(60*uiAlpha)), "InvisibleX");
            // "Invisible" — white
            dl->AddText(NULL, titleSize, textPos,
                        COL_TXT_HI((int)(255 * uiAlpha)), titlePart1);
            // "X" — accent red
            dl->AddText(NULL, titleSize, ImVec2(textPos.x + tw_p1, textPos.y),
                        COL_ACCENT((int)(255 * uiAlpha)), titlePart2);

            // ── TAB BAR — exact source geometry ──────────────
            if (scale > 0.5f) {
                const char* btnList[] = { "Home", "ESP", "Aim", "Exploit", "Config" };
                int 按钮总数 = 5;
                float btnCenterY = p.y + 36 * scale;
                float btnH = 34 * scale;
                const float btnFontSize = 20.0f * scale;
                const float btnPadX     = 14.0f * scale;
                const float btnGap      =  8.0f * scale;

                // Start clear of the title, and give every pill the width its
                // own label needs — a fixed 62 clipped "Exploit" to "Exploi".
                ImFont* const btnFont = ImGui::GetFont();
                float titleEndX = p.x + 40 * scale
                                + btnFont->CalcTextSizeA(titleSize, FLT_MAX, -1.f, "InvisibleX").x;
                float btnStartX = titleEndX + 28 * scale;

                float btnWidths[5];
                for (int i = 0; i < 按钮总数; i++)
                    btnWidths[i] = btnFont->CalcTextSizeA(btnFontSize, FLT_MAX, -1.f, btnList[i]).x
                                 + btnPadX * 2.0f;

                // Keep the row clear of the close + [NEW] exit buttons
                // on narrow panels (two circles now, not one).
                float rowW = 0.0f;
                for (int i = 0; i < 按钮总数; i++) rowW += btnWidths[i] + (i ? btnGap : 0.0f);
                const float rowLimit = p.x + windowSize.x - 110 * scale;
                if (btnStartX + rowW > rowLimit) btnStartX = rowLimit - rowW;

                float btnX = btnStartX;
                for (int i = 0; i < 按钮总数; i++) {
                    const float btnW = btnWidths[i];
                    ImVec2 btnMin = ImVec2(btnX, btnCenterY - btnH / 2);
                    ImVec2 btnMax = ImVec2(btnMin.x + btnW, btnMin.y + btnH);
                    btnX += btnW + btnGap;
                    ImU32 btnBg  = (i == 现在页面) ? COL_ACCENT((int)(255 * uiAlpha))
                                                   : COL_BTN_IDLE((int)(235 * uiAlpha));
                    ImU32 txtCol = (i == 现在页面) ? IM_COL32(255, 255, 255, (int)(255 * uiAlpha))
                                                   : COL_TXT_MID((int)(255 * uiAlpha));
                    if (i == 现在页面) {
                        // Glow under the active pill, then a red -> ember
                        // gradient across it. A flat fill read as a plain
                        // coloured box next to the artwork.
                        for (int gi = 3; gi >= 1; --gi) {
                            const float o  = (float)gi * 2.0f * scale;
                            const int   ga = (int)(15.0f * (float)(4 - gi) * uiAlpha);
                            if (ga <= 0) continue;
                            dl->AddRectFilled(ImVec2(btnMin.x - o, btnMin.y - o),
                                              ImVec2(btnMax.x + o, btnMax.y + o),
                                              COL_ACCENT(ga), btnH * 0.5f + o);
                        }
                        const float cr = btnH * 0.5f;
                        dl->AddRectFilled(btnMin, ImVec2(btnMin.x + cr * 2.0f, btnMax.y),
                                          COL_ACCENT((int)(255 * uiAlpha)), cr);
                        dl->AddRectFilled(ImVec2(btnMax.x - cr * 2.0f, btnMin.y), btnMax,
                                          COL_ACCENT_ALT((int)(255 * uiAlpha)), cr);
                        dl->AddRectFilledMultiColor(
                            ImVec2(btnMin.x + cr, btnMin.y), ImVec2(btnMax.x - cr, btnMax.y),
                            COL_ACCENT((int)(255 * uiAlpha)), COL_ACCENT_ALT((int)(255 * uiAlpha)),
                            COL_ACCENT_ALT((int)(255 * uiAlpha)), COL_ACCENT((int)(255 * uiAlpha)));
                    } else {
                        dl->AddRectFilled(btnMin, btnMax, btnBg, btnH * 0.5f);
                        dl->AddRect(btnMin, btnMax, COL_HAIRLINE((int)(160 * uiAlpha)),
                                    btnH * 0.5f, 0, 1.0f);
                    }
                    const float tw = btnFont->CalcTextSizeA(btnFontSize, FLT_MAX, -1.f, btnList[i]).x;
                    dl->AddText(NULL, btnFontSize,
                                ImVec2(btnMin.x + (btnW - tw) * 0.5f, btnCenterY - btnFontSize * 0.5f),
                                txtCol, btnList[i]);
                    ImGui::SetCursorScreenPos(btnMin);
                    if (ImGui::InvisibleButton(btnList[i], ImVec2(btnW, btnH))) 现在页面 = i;
                }

                // Close button — only minimizes/hides the overlay panel,
                // does not stop the process (触发关闭动画 is a visual
                // close animation, nothing more).
                ImVec2 closeCenter = ImVec2(p.x + windowSize.x - 38 * scale, btnCenterY);
                dl->AddCircleFilled(closeCenter, 15 * scale, COL_BTN_IDLE((int)(235 * uiAlpha)), 18);
                dl->AddText(NULL, 28.0f * scale, ImVec2(closeCenter.x - 7 * scale, closeCenter.y - 14 * scale),
                            COL_ACCENT((int)(255 * uiAlpha)), "x");
                ImGui::SetCursorScreenPos(ImVec2(closeCenter.x - 19 * scale, closeCenter.y - 19 * scale));
                if (ImGui::InvisibleButton("##close", ImVec2(38 * scale, 38 * scale))) {
                    触发关闭动画(windowPos);
                }

                // [NEW — "exit button cheat ke side mein laga do"] The
                // REAL program-exit button (stops the whole process),
                // moved here next to the minimize "x" so it's reachable
                // from every page, not only buried on the Config/
                // Settings page. Drawn as a small power-style circle
                // just to the left of the minimize button.
                ImVec2 exitCenter = ImVec2(closeCenter.x - 46 * scale, btnCenterY);
                dl->AddCircleFilled(exitCenter, 15 * scale, COL_BTN_IDLE((int)(235 * uiAlpha)), 18);
                dl->AddText(NULL, 18.0f * scale, ImVec2(exitCenter.x - 13 * scale, exitCenter.y - 9 * scale),
                            COL_ACCENT((int)(255 * uiAlpha)), "EX");
                ImGui::SetCursorScreenPos(ImVec2(exitCenter.x - 19 * scale, exitCenter.y - 19 * scale));
                if (ImGui::InvisibleButton("##hard_exit", ImVec2(38 * scale, 38 * scale))) {
                    UpdateAndSaveSettings();
                    main_thread_flag = false;
                    shutdown();
                    Touch_Close();
                    _exit(0);
                }
            }

            if (scale > 0.6f) {
                // ════════ PAGE 0 — HOME ════════
                if (现在页面 == 0) {
                    float 左W = windowSize.x * 0.4f;
                    float 间隔 = 18 * scale;
                    float Lx1 = p.x + 38 * scale, Ly1 = p.y + 80 * scale;
                    float Lx2 = p.x + 38 * scale + 左W - 56 * scale, Ly2 = p.y + windowSize.y - 38 * scale;
                    dl->AddRectFilled(ImVec2(Lx1, Ly1), ImVec2(Lx2, Ly2), COL_CARD((int)(235 * uiAlpha)), 22 * scale);
                    float Lnx1 = Lx1 + 22 * scale, Lny1 = Ly1 + 22 * scale;
                    float Lnx2 = Lx2 - 22 * scale, Lny2 = Ly2 - 22 * scale;
                    dl->AddRectFilled(ImVec2(Lnx1, Lny1), ImVec2(Lnx2, Lny2), COL_CARD_INNER((int)(240 * uiAlpha)), 18 * scale);

                    ImVec2 cardCenter = ImVec2((Lnx1 + Lnx2) / 2, (Lny1 + Lny2) / 2 - 60 * scale);
                    ImVec2 avatarSize(158 * scale, 158 * scale);
                    static float avatarHoverScale = 1.0f;
                    float targetAvatarScale = 1.0f;
                    ImGui::SetCursorScreenPos(ImVec2(cardCenter.x - avatarSize.x / 2, cardCenter.y - avatarSize.y / 2));
                    ImGui::InvisibleButton("##avatar", avatarSize);
                    bool isAvatarHovered = ImGui::IsItemHovered();
                    bool isAvatarClicked = ImGui::IsItemClicked();
                    if (isAvatarClicked) { 触发关闭动画(windowPos); targetAvatarScale = 0.9f; }
                    else                 { targetAvatarScale = 1.0f; }
                    avatarHoverScale = SM_Lerp(avatarHoverScale, targetAvatarScale, 0.2f);

                    ImVec2 avatarP0 = ImGui::GetItemRectMin();
                    ImVec2 avatarP1 = ImGui::GetItemRectMax();
                    ImVec2 avatarCenter = ImVec2((avatarP0.x + avatarP1.x) * 0.5f, (avatarP0.y + avatarP1.y) * 0.5f);
                    ImVec2 scaledHalfSize = ImVec2(avatarSize.x * 0.5f * avatarHoverScale, avatarSize.y * 0.5f * avatarHoverScale);
                    ImVec2 scaledP0 = ImVec2(avatarCenter.x - scaledHalfSize.x, avatarCenter.y - scaledHalfSize.y);
                    ImVec2 scaledP1 = ImVec2(avatarCenter.x + scaledHalfSize.x, avatarCenter.y + scaledHalfSize.y);
                    // The INX mark, drawn as vectors. The old build shipped a
                    // 3.7 MB PNG for this and fell back to a plain "IX" when the
                    // texture was missing; the mark now needs neither.
                    ImU32 avatarBgColor = isAvatarHovered ? COL_BTN_IDLE((int)(255 * uiAlpha))
                                                          : COL_CARD((int)(255 * uiAlpha));
                    float avatarRounding = 25.0f * scale;
                    dl->AddRectFilled(scaledP0, scaledP1, avatarBgColor, avatarRounding);
                    SM_DrawInxMark(dl, avatarCenter,
                                   (scaledP1.x - scaledP0.x), uiAlpha);

                    {   // Centred under the mark, inside the card — it used to
                        // be offset by a fixed 70 and sat on the card's edge.
                        const float wSize = 24.0f * scale;
                        const float wTw = ImGui::GetFont()->CalcTextSizeA(wSize, FLT_MAX, -1.f,
                                                                         "Welcome back").x;
                        dl->AddText(NULL, wSize,
                                    ImVec2(cardCenter.x - wTw * 0.5f, cardCenter.y + 108 * scale),
                                    COL_TXT_HI((int)(255 * uiAlpha)), "Welcome back");
                    }

                    // Right column
                    float Rx1 = Lx2 + 间隔;
                    float Rx2 = p.x + (LAY_COL_X + LAY_COL_W) * scale;  /* column edge, not panel edge */
                    float CenterX = (Rx1 + Rx2) * 0.5f;
                    float Ry1 = p.y + 80 * scale, Ry2 = p.y + 220 * scale;
                    dl->AddRectFilled(ImVec2(Rx1, Ry1), ImVec2(Rx2, Ry2), COL_CARD((int)(235 * uiAlpha)), 22 * scale);

                    std::time_t t = std::time(nullptr);
                    std::tm tmv = *std::localtime(&t);
                    char hhmm[16], day[64];
                    strftime(hhmm, sizeof(hhmm), "%H:%M", &tmv);
                    strftime(day,  sizeof(day),  "%d %b %Y  %A", &tmv);
                    ImFont* font = ImGui::GetFont();
                    float tw_time_label = font->CalcTextSizeA(22.0f * scale, FLT_MAX, -1.f, "TIME").x;
                    float tw_time_value = font->CalcTextSizeA(62.0f * scale, FLT_MAX, -1.f, hhmm).x;
                    float tw_date       = font->CalcTextSizeA(18.0f * scale, FLT_MAX, -1.f, day).x;
                    dl->AddText(NULL, 22.0f * scale, ImVec2(CenterX - tw_time_label * 0.5f, p.y + 95 * scale),
                                COL_ACCENT((int)(255 * uiAlpha)), "TIME");
                    dl->AddText(NULL, 62.0f * scale, ImVec2(CenterX - tw_time_value * 0.5f - 12 * scale, p.y + 120 * scale),
                                COL_TXT_HI((int)(255 * uiAlpha)), hhmm);
                    dl->AddText(NULL, 18.0f * scale, ImVec2(CenterX - tw_date * 0.5f + 12 * scale, p.y + 175 * scale),
                                COL_TXT_MID((int)(220 * uiAlpha)), day);

                    float Ry3 = p.y + 236 * scale, Ry4 = p.y + 330 * scale;
                    dl->AddRectFilled(ImVec2(Rx1, Ry3), ImVec2(Rx2, Ry4), COL_CARD((int)(235 * uiAlpha)), 22 * scale);
                    float tw_h = font->CalcTextSizeA(24.0f * scale, FLT_MAX, -1.f, "Daily").x;
                    // Fit the quote to the card. At a fixed 22 it ran past the
                    // card edge and over the artwork once the cards narrowed.
                    float quoteSize = 22.0f * scale;
                    const float quoteMaxW = (Rx2 - Rx1) - 28.0f * scale;
                    float tw_s = font->CalcTextSizeA(quoteSize, FLT_MAX, -1.f, 当前固定欢迎语).x;
                    if (tw_s > quoteMaxW && tw_s > 0.0f) {
                        quoteSize *= quoteMaxW / tw_s;
                        if (quoteSize < 11.0f * scale) quoteSize = 11.0f * scale;
                        tw_s = font->CalcTextSizeA(quoteSize, FLT_MAX, -1.f, 当前固定欢迎语).x;
                    }
                    dl->AddText(NULL, 24.0f * scale, ImVec2(CenterX - tw_h * 0.5f, Ry3 + 25 * scale),
                                COL_ACCENT((int)(255 * uiAlpha)), "Daily");
                    dl->AddText(NULL, quoteSize, ImVec2(CenterX - tw_s * 0.5f, Ry3 + 60 * scale),
                                COL_TXT_HI((int)(255 * uiAlpha)), 当前固定欢迎语);

                    float Ry5 = p.y + 346 * scale, Ry6 = p.y + windowSize.y - 38 * scale;
                    dl->AddRectFilled(ImVec2(Rx1, Ry5), ImVec2(Rx2, Ry6), COL_CARD((int)(235 * uiAlpha)), 22 * scale);
                    char statusBuf[96];
                    snprintf(statusBuf, sizeof(statusBuf), "%s   FPS %.0f",
                             g_GameIsRunning ? "Game running" : "Waiting for game", g_FPS);
                    float tw_g1 = font->CalcTextSizeA(32.0f * scale, FLT_MAX, -1.f, "Status").x;
                    float tw_g2 = font->CalcTextSizeA(20.0f * scale, FLT_MAX, -1.f, statusBuf).x;
                    dl->AddText(NULL, 32.0f * scale, ImVec2(CenterX - tw_g1 * 0.5f, Ry5 + 20 * scale),
                                COL_ACCENT((int)(255 * uiAlpha)), "Status");
                    dl->AddText(NULL, 20.0f * scale, ImVec2(CenterX - tw_g2 * 0.5f, Ry5 + 65 * scale),
                                COL_TXT_MID((int)(220 * uiAlpha)), statusBuf);
                }
                // ════════ PAGE 1 — ESP (source 绘制 layout) ════════
                else if (现在页面 == 1) {
                    if (!ix_feat_esp() || !ix_rt_ready()) {
                        float cy = p.y + windowSize.y * 0.42f;
                        const char* msg = "ESP locked";
                        float tw = ImGui::GetFont()->CalcTextSizeA(24.f * scale, FLT_MAX, -1.f, msg).x;
                        dl->AddText(NULL, 24.f * scale, ImVec2(p.x+(windowSize.x-tw)*0.5f, cy),
                                    COL_TXT_MID((int)(220*uiAlpha)), msg);
                    } else {
                    // Content column only — the artwork keeps the right side.
                    const float colX  = p.x + LAY_COL_X * scale;
                    const float colW  = LAY_COL_W * scale;
                    const float gap   = LAY_GAP * scale;
                    const float round = LAY_ROUND * scale;
                    const float bodyY = p.y + LAY_BODY_Y * scale;
                    const float bodyB = p.y + LAY_BODY_B * scale;
                    const ImU32 cardBg = COL_CARD((int)(210 * uiAlpha));

                    // Seven switches in two columns, then the Text Size card.
                    // Row height is solved from the body box so the page fills
                    // it exactly instead of leaving a gap at the bottom.
                    const float sliderH = 86 * scale;
                    const float rowH    = (bodyB - bodyY - 4 * gap - sliderH) / 4.0f;
                    const float halfW   = (colW - gap) * 0.5f;

                    struct EspRow { const char* label; bool* value; };
                    EspRow espRows[7] = {
                        { "Box",       &g_Cfg.showBox      },
                        { "Skeleton",  &g_Cfg.showSkeleton },
                        { "Name",      &g_Cfg.showName     },
                        { "Health",    &g_Cfg.showHealth   },
                        { "Distance",  &g_Cfg.showDistance },
                        { "ESP Line",  &g_Cfg.showLine     },
                        { "360 Alert", &g_Cfg.is3600       }
                    };

                    for (int i = 0; i < 7; ++i) {
                        const bool  wide = (i == 6);           // last one spans the column
                        const float cw   = wide ? colW : halfW;
                        const float cx   = colX + (wide ? 0.0f : (i % 2) * (halfW + gap));
                        const float cy   = bodyY + (float)(wide ? 3 : i / 2) * (rowH + gap);

                        dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + cw, cy + rowH), cardBg, round);
                        dl->AddRect(ImVec2(cx, cy), ImVec2(cx + cw, cy + rowH),
                                    COL_HAIRLINE((int)(150 * uiAlpha)), round, 0, 1.0f);

                        // The switch draws its own label, so it is placed where
                        // the label should start and sized by Checkbox1 itself.
                        ImGui::SetCursorScreenPos(ImVec2(cx + 16 * scale,
                                                         cy + (rowH - 36 * scale) * 0.5f));
                        ImGui::Checkbox1(espRows[i].label, espRows[i].value);
                    }

                    // Text Size
                    const float sy = bodyY + 4 * (rowH + gap);
                    dl->AddRectFilled(ImVec2(colX, sy), ImVec2(colX + colW, sy + sliderH), cardBg, round);
                    dl->AddRect(ImVec2(colX, sy), ImVec2(colX + colW, sy + sliderH),
                                COL_HAIRLINE((int)(150 * uiAlpha)), round, 0, 1.0f);
                    dl->AddText(NULL, 22.0f * scale, ImVec2(colX + 16 * scale, sy + 14 * scale),
                                COL_TXT_HI((int)(255 * uiAlpha)), "Text Size");

                    ImGui::SetCursorScreenPos(ImVec2(colX + 16 * scale, sy + 48 * scale));
                    ImGui::SetNextItemWidth(colW - 32 * scale);
                    float ts = (float)textSize;
                    if (ImGui::SliderFloat("##TextSize", &ts, 10.0f, 50.0f, "%.0f")) textSize = (int)ts;

                    // Sync
                    showLine     = g_Cfg.showLine;
                    showBox      = g_Cfg.showBox;
                    showSkeleton = g_Cfg.showSkeleton;
                    showName     = g_Cfg.showName;
                    showHealth   = g_Cfg.showHealth;
                    showDistance = g_Cfg.showDistance;
                    is3600       = g_Cfg.is3600;
                    Enable = (showLine || showBox || showSkeleton || showName ||
                              showHealth || showDistance || showCount);
                }
                    } /* feat gate esp */
                // ════════ PAGE 2 — AIM (Hardware BT) ════════
                else if (现在页面 == 2) {
                    if (!ix_feat_aim() || !ix_rt_ready()) {
                        float cy = p.y + windowSize.y * 0.42f;
                        const char* msg = "Aim locked";
                        float tw = ImGui::GetFont()->CalcTextSizeA(24.f * scale, FLT_MAX, -1.f, msg).x;
                        dl->AddText(NULL, 24.f * scale, ImVec2(p.x+(windowSize.x-tw)*0.5f, cy),
                                    COL_TXT_MID((int)(220*uiAlpha)), msg);
                    } else {
                    float contentY = p.y + 95 * scale;
                    // Card geometry moved into the content column so the
                    // artwork on the right stays visible. Controls inside are
                    // unchanged; only the box they sit in is narrower.
                    float centerX  = p.x + (LAY_COL_X + LAY_COL_W * 0.5f) * scale;
                    float cardW    = LAY_COL_W * scale;
                    float cardRound = 22 * scale;
                    ImU32 cardBg = COL_CARD((int)(235 * uiAlpha));
                    /* Taller than the other pages: this one carries the Show
                       switch and its two lines of state underneath. */

                    ImVec2 c1Min = ImVec2(centerX - cardW * 0.5f, contentY);
                    // Card shrunk 500 -> 400 after removing the Drag
                    // Headshot checkbox + its radius slider (~100*scale of
                    // content). Still fits Aim Silent, Hardware BT, Show
                    // FOV Circle, the FOV Range slider and the Head/Body/
                    // Legit mode selector block below it.
                    ImVec2 c1Max = ImVec2(centerX + cardW * 0.5f, contentY + 400 * scale);
                    dl->AddRectFilled(c1Min, c1Max, cardBg, cardRound);

                    float startX = c1Min.x + 25 * scale;
                    float startY = c1Min.y + 20 * scale;
                    float row    = 48 * scale;
                    float col    = 236 * scale;  /* fits the content column */

                    // Row 0 — Silent Aim
                    ImGui::SetCursorScreenPos(ImVec2(startX, startY));
                    ImGui::Checkbox1("Aim Silent", &AimSilent);
                    g_Cfg.aimSilent = AimSilent;
                    // [REMOVED] Drag Headshot checkbox + its own radius
                    // slider below — feature dropped from the menu per
                    // explicit request. UpdateDragHeadshotThread() and
                    // g_Cfg.dragHeadshot/dragHeadshotRadius are left
                    // untouched in hook.h/ix_cfg.h (same unreachable-
                    // but-not-deleted pattern already used for Body
                    // Kill/Aimbot below), just force-cleared every frame
                    // a few lines down so stale config.json can't bring
                    // it back silently.

                    // Row 1 — Hardware BT | Show FOV Circle
                    // [SIMPLIFIED] Aim section now only carries Aim
                    // Silent, Hardware BT and FOV Circle — Drag
                    // Headshot, Body Kill and Aimbot checkboxes removed
                    // from this page entirely. Their code (BodyKillBone(),
                    // UpdateAimbotFast(), UpdateDragHeadshotThread(),
                    // g_Cfg.bodyKill/Aimbot/dragHeadshot) is left
                    // untouched in hook.h/main.cpp, just unreachable
                    // from any menu now — same approach already used
                    // for the old Collider Lock globals.
                    ImGui::SetCursorScreenPos(ImVec2(startX, startY + row));
                    ImGui::Checkbox1("Hardware BT", &g_Cfg.hardwareBT);
                    ImGui::SetCursorScreenPos(ImVec2(startX + col, startY + row));
                    ImGui::Checkbox1("Show FOV Circle", &g_Cfg.showFovCircle);

                    // Label drawn here rather than by the slider: ImGui puts an
                    // inline label to the RIGHT of the widget, which pushed
                    // "FOV Range" outside the card and over the artwork.
                    float fovLabelY = startY + row * 2.6f;
                    dl->AddText(NULL, 20.0f * scale,
                                ImVec2(startX, fovLabelY),
                                COL_TXT_HI((int)(255 * uiAlpha)), "FOV Range");
                    ImGui::SetCursorScreenPos(ImVec2(startX, fovLabelY + 30 * scale));
                    ImGui::SetNextItemWidth(cardW - 50 * scale);
                    ImGui::SliderFloat("##FOVRange", &g_Cfg.aimFovUI, 1.0f, 360.0f, "%.0f");

                    // [REMOVED] Drag Headshot trigger-radius slider that
                    // used to sit here — modeY now follows directly after
                    // the FOV Range slider instead of after it.

                    // -- [RE-ADDED, NOW TOGGLE BOXES] Hardware BT mode --
                    // Head / Body / Legit. Was a slider (chestRateSlider);
                    // tumne bola toggle-box style chahiye, same pattern as
                    // the dead Scarecrow/SourceMenu.h code already had for
                    // this exact picker. Neither box selected = Head.
                    // Legit's actual 70/30 split is still implemented in
                    // UpdateHardwareBTFast() in hook.h — this is only the
                    // picker for which of the three it runs.
                    float modeY = fovLabelY + 30 * scale + 56 * scale;
                    dl->AddText(NULL, 20.0f * scale,
                                ImVec2(startX, modeY),
                                COL_TXT_HI((int)(255 * uiAlpha)), "Hardware BT Mode");
                    {
                        if (g_Cfg.chestRateSave < 1 || g_Cfg.chestRateSave > 3)
                            g_Cfg.chestRateSave = 1;

                        const char* btModes[] = { "Body", "Legit" };
                        const int   btVals[]  = { 2, 3 };
                        float modeBtnY   = modeY + 30 * scale;
                        float modeW      = (cardW - 50 * scale - 20 * scale) * 0.5f;
                        float modeH      = 40 * scale;
                        float modeGap    = 20 * scale;
                        for (int mi = 0; mi < 2; mi++) {
                            int modeVal   = btVals[mi];
                            bool selected = (g_Cfg.chestRateSave == modeVal);
                            ImVec2 bMin = ImVec2(startX + mi * (modeW + modeGap), modeBtnY);
                            ImVec2 bMax = ImVec2(bMin.x + modeW, bMin.y + modeH);
                            ImU32 bg = selected ? COL_ACCENT((int)(255 * uiAlpha))
                                                : COL_BTN_IDLE((int)(235 * uiAlpha));
                            ImU32 tc = selected ? IM_COL32(255, 255, 255, (int)(255 * uiAlpha))
                                                : COL_TXT_MID((int)(255 * uiAlpha));
                            dl->AddRectFilled(bMin, bMax, bg, 12 * scale);
                            ImFont* f = ImGui::GetFont();
                            float tsz = 20.0f * scale;
                            float tw2 = f->CalcTextSizeA(tsz, FLT_MAX, -1.f, btModes[mi]).x;
                            dl->AddText(NULL, tsz,
                                        ImVec2(bMin.x + (modeW - tw2) * 0.5f,
                                               bMin.y + (modeH - tsz) * 0.5f),
                                        tc, btModes[mi]);
                            ImGui::SetCursorScreenPos(bMin);
                            char btnId[32];
                            snprintf(btnId, sizeof(btnId), "##btmode%d", mi);
                            if (ImGui::InvisibleButton(btnId, ImVec2(modeW, modeH))) {
                                if (selected) g_Cfg.chestRateSave = 1; // toggle off -> headshot
                                else          g_Cfg.chestRateSave = modeVal;
                            }
                        }
                        if (!g_Cfg.hardwareBT) g_Cfg.chestRateSave = 1;
                        chestRate = g_Cfg.chestRateSave; // UpdateHardwareBTFast() reads this global directly
                        const char* hint = "Head only";
                        if (g_Cfg.chestRateSave == 2) hint = "Body 100%";
                        else if (g_Cfg.chestRateSave == 3) hint = "Legit 70% body / 30% head";
                        dl->AddText(NULL, 16.0f * scale,
                                    ImVec2(startX, modeBtnY + modeH + 10 * scale),
                                    COL_TXT_MID((int)(200 * uiAlpha)), hint);
                    }

                    // Sync → hook globals
                    g_ShowFovOverlay  = g_Cfg.showFovCircle;
                    AimFov            = g_Cfg.aimFovUI;
                    g_Cfg.aimSilent   = AimSilent;    // Aim Silent sync — unchanged, untouched by this fix
                    // [REMOVED] Drag Headshot — checkbox/slider gone from
                    // the menu above; force-cleared every frame (same
                    // pattern as AimMagnet/g_ExploitFloatUI below) so a
                    // stale config.json can't silently re-enable it.
                    // AimLock/AimLockNew intentionally left alone here
                    // (always whatever they were last, effectively
                    // unused now that nothing sets them from the menu).
                    DragHeadshot      = false;
                    g_Cfg.dragHeadshot = false;
                    // [RE-ADDED] Aimbot — now synced from its own
                    // checkbox above instead of being forced off.
                    Aimbot            = g_Cfg.Aimbot;
                    // [REMOVED] Pull / Float-Pull — both force-cleared every
                    // frame so stale config.json (exp_floatpull) can't bring
                    // either back silently now their checkboxes are gone.
                    AimMagnet         = false;
                    g_ExploitFloatUI  = false;
                    BrutalBT          = g_Cfg.brutalBT;
                    aimBotBoneIndex   = 0;
                    showAimLine       = g_Cfg.showAimLine;
                    chestRate         = g_Cfg.chestRateSave;
                    if (chestRate < 1) chestRate = 1;
                    if (chestRate > 3) chestRate = 3;
                    // [RE-ADDED] Hardware BT — real checkbox is Row 1 above
                    // now; just mirror it into the global the hook thread
                    // reads, same pattern as every other feature here.
                    HardwareBT        = g_Cfg.hardwareBT;
                    BodyKill          = g_Cfg.bodyKill;
                    BodyKillHP        = g_Cfg.bodyKillHP;
                    /* threads are idempotent (guarded by running flags).
                     * Body Kill has no thread of its own — its threshold
                     * check runs inline inside StartSilentAimThread's loop. */
                    StartSilentAimThread();
                    // [REMOVED] StartDragHeadshotThread() call — Drag
                    // Headshot has no checkbox left to enable it from, and
                    // DragHeadshot is force-cleared above, so the thread
                    // (if started elsewhere/left over) idles permanently
                    // rather than being started fresh here. Aimbot's start
                    // kept, unaffected by this.
                    StartAimbotThread();
                    StartHardwareBTThread();
                }

                    } /* feat gate aim */
                // ════════ PAGE 3 — EXPLOIT (full section like Aim) ════════
                else if (现在页面 == 3) {
                    if (!ix_feat_exploit() || !ix_rt_ready()) {
                        float cy = p.y + windowSize.y * 0.42f;
                        const char* msg = "Exploit locked";
                        float tw = ImGui::GetFont()->CalcTextSizeA(24.f * scale, FLT_MAX, -1.f, msg).x;
                        dl->AddText(NULL, 24.f * scale, ImVec2(p.x+(windowSize.x-tw)*0.5f, cy),
                                    COL_TXT_MID((int)(220*uiAlpha)), msg);
                    } else {
                    float contentY = p.y + 95 * scale;
                    // Card geometry moved into the content column so the
                    // artwork on the right stays visible. Controls inside are
                    // unchanged; only the box they sit in is narrower.
                    float centerX  = p.x + (LAY_COL_X + LAY_COL_W * 0.5f) * scale;
                    float cardW    = LAY_COL_W * scale;
                    float cardRound = 22 * scale;
                    ImU32 cardBg = COL_CARD((int)(235 * uiAlpha));

                    /* taller card so rows never overlap */
                    ImVec2 c1Min = ImVec2(centerX - cardW * 0.5f, contentY);
                    ImVec2 c1Max = ImVec2(centerX + cardW * 0.5f, contentY + 360 * scale);
                    SM_SoftShadow(dl, c1Min, c1Max, cardRound, uiAlpha);
                    dl->AddRectFilled(c1Min, c1Max, cardBg, cardRound);

                    dl->AddText(NULL, 26.0f * scale,
                                ImVec2(c1Min.x + 25 * scale, c1Min.y + 16 * scale),
                                COL_ACCENT((int)(255 * uiAlpha)), "Exploit");

                    float startX = c1Min.x + 28 * scale;
                    float startY = c1Min.y + 58 * scale;
                    float row    = 52 * scale;   /* spacing between rows */
                    float col    = 236 * scale;  /* fits the content column */

                    /* Row 0 */
                    // [REMOVED] "Enemies pull" checkbox — Pull/AimMagnet feature removed.
                    // AimMagnet force-cleared below so stale config.json can't bring it back.
                    ImGui::SetCursorScreenPos(ImVec2(startX + col, startY));
                    ImGui::Checkbox1("Ghost Hack", &GhostHack);

                    /* Row 1 */
                    ImGui::SetCursorScreenPos(ImVec2(startX, startY + row));
                    ImGui::Checkbox1("No Recoil", &NoRecoil);
                    ImGui::SetCursorScreenPos(ImVec2(startX + col, startY + row));
                    ImGui::Checkbox1("No Reload", &NoReload);

                    /* Row 2 — floating toggles */
                    // [REMOVED] "Float: pull" checkbox — Float-Pull feature removed.
                    // g_ExploitFloatUI force-cleared below for the same reason.
                    ImGui::SetCursorScreenPos(ImVec2(startX + col, startY + row * 2));
                    ImGui::Checkbox1("Float: Ghost", &g_ExploitFloatGhost);
                    ImGui::Checkbox1("Normal Write (no breakpoint)", &g_DirNormalWrite);

                    // [REMOVED] "Pull Range" slider — no longer needed, Pull removed.
                }
                    } /* feat gate exploit */
                else if (现在页面 == 4) {
                    float contentY  = p.y + 95 * scale;
                    // Card geometry moved into the content column so the
                    // artwork on the right stays visible. Controls inside are
                    // unchanged; only the box they sit in is narrower.
                    float centerX   = p.x + (LAY_COL_X + LAY_COL_W * 0.5f) * scale;
                    float cardW     = LAY_COL_W * scale;
                    float cardRound = 22 * scale;
                    ImU32 cardBg    = COL_CARD((int)(235 * uiAlpha));

                    // ── INFO CARD ──────────────────────────────────
                    float cardH1 = 250 * scale;
                    ImVec2 c1Min = ImVec2(centerX - cardW * 0.5f, contentY);
                    ImVec2 c1Max = ImVec2(centerX + cardW * 0.5f, contentY + cardH1);
                    SM_SoftShadow(dl, c1Min, c1Max, cardRound, uiAlpha);
                    dl->AddRectFilled(c1Min, c1Max, cardBg, cardRound);

                    dl->AddText(NULL, 28.0f * scale,
                                ImVec2(c1Min.x + 25 * scale, c1Min.y + 18 * scale),
                                COL_TXT_HI((int)(255 * uiAlpha)), "Info");

                    _SrvData sd;
                    { std::lock_guard<std::mutex> lk(g_SrvMtx); sd = g_Srv; }

                    float infoX  = c1Min.x + 25 * scale;
                    float infoY  = c1Min.y + 62 * scale;
                    float infoRow = 42 * scale;
                    float valX   = c1Max.x - 25 * scale;

                    auto InfoRow = [&](int idx, const char* label, const char* value, ImU32 valCol) {
                        float y = infoY + infoRow * (float)idx;
                        dl->AddText(NULL, 22.0f * scale, ImVec2(infoX, y),
                                    COL_TXT_MID((int)(255 * uiAlpha)), label);
                        ImFont* f = ImGui::GetFont();
                        float vw = f->CalcTextSizeA(22.0f * scale, FLT_MAX, -1.f, value).x;
                        dl->AddText(NULL, 22.0f * scale, ImVec2(valX - vw, y), valCol, value);
                    };

                    char buf[96];
                    InfoRow(0, "Seller", sd.seller.c_str(), COL_ACCENT((int)(255 * uiAlpha)));

                    snprintf(buf, sizeof(buf), "%d Days", sd.days_remain);
                    InfoRow(1, "Remaining", buf, COL_ACCENT((int)(255 * uiAlpha)));

                    snprintf(buf, sizeof(buf), "%d Days", sd.days_total);
                    InfoRow(2, "Total Days", buf, COL_TXT_HI((int)(255 * uiAlpha)));

                    InfoRow(3, "Expiry", sd.expiry.c_str(), COL_TXT_HI((int)(255 * uiAlpha)));

                    // ── SETTINGS CARD (Mute + Exit) ──
                    // [REMOVED] "Inject Collider Stub"/"Re-Inject Collider
                    // Stub" button and "Internal Collider Lock" checkbox —
                    // the whole ptrace-injected collider_stub.so + shared-
                    // memory IPC mechanism (collider_ipc.h, collider_stub.cpp,
                    // ix_retry_collider_injection()) is being retired from
                    // the menu; its SilentAimDir equivalent (Aim Silent) now
                    // has its own Paradise-then-KPM HWBP tiers instead of a
                    // separate injected stub for the collider field. Card
                    // shrunk back down 290 -> 190 (Volume Off + Exit only,
                    // same height as before the stub UI was added).
                    float cardH2      = 190 * scale;
                    float cardSpacing = 20 * scale;
                    ImVec2 c2Min = ImVec2(centerX - cardW * 0.5f, contentY + cardH1 + cardSpacing);
                    ImVec2 c2Max = ImVec2(centerX + cardW * 0.5f, contentY + cardH1 + cardSpacing + cardH2);
                    SM_SoftShadow(dl, c2Min, c2Max, cardRound, uiAlpha);
                    dl->AddRectFilled(c2Min, c2Max, cardBg, cardRound);

                    dl->AddText(NULL, 28.0f * scale,
                                ImVec2(c2Min.x + 25 * scale, c2Min.y + 16 * scale),
                                COL_TXT_HI((int)(255 * uiAlpha)), "Settings");

                    // Volume Off — mutes the menu's own click/toggle sounds
                    // (ix_audio.h). Checkbox is the source of truth (same
                    // pattern as Body Kill etc. above); applied every frame
                    // so it takes effect immediately either way.
                    ImGui::SetCursorScreenPos(ImVec2(c2Min.x + 25 * scale, c2Min.y + 58 * scale));
                    ImGui::Checkbox1("Volume Off", &g_Cfg.soundMuted);
                    ix_sound_set_muted(g_Cfg.soundMuted);

                    float btnW = 380 * scale;
                    float btnH = 45 * scale;

                    ImVec2 btnPos = ImVec2(centerX - btnW * 0.5f, c2Min.y + 112 * scale);
                    ImGui::SetCursorScreenPos(btnPos);
                    if (ImGui::Button("Exit", ImVec2(btnW, btnH))) {
                        UpdateAndSaveSettings();
                        main_thread_flag = false;
                        shutdown();
                        Touch_Close();
                        _exit(0);
                    }
                }
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}
