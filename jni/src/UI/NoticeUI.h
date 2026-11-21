#pragma once
// =====================================================================
//  NoticeUI — the "致用户 / To the user" screen and the floating bottom
//  nav bar, rebuilt from the reference screenshot.
//
//  PROVENANCE: every position, size and colour in this file was measured
//  off the supplied screenshot, NOT taken from source — the uploaded
//  jni.zip contains a different UI entirely (a 1100x850 solid-white
//  tabbed window; none of the strings in this file appear anywhere in
//  it). So these are read-from-image values and are expected to need a
//  pass of 1-2px nudges once it is seen on a real device. They are not
//  guesses about someone else's code.
//
//  Layout is authored in a fixed design space (NUI_DW x NUI_DH) and
//  scaled uniformly to the real display, so it holds its proportions on
//  any screen instead of drifting with resolution.
// =====================================================================

#include "ImGui/imgui.h"
#include "UI/fonts/FaIcons.h"
#include <math.h>

// ── Design space: the screenshot's own pixel grid ────────────────────
#define NUI_DW 2400.0f
#define NUI_DH 1080.0f

// ── Palette, sampled from the screenshot ─────────────────────────────
#define NUI_CARD_FILL     IM_COL32(236, 232, 228, 150)  // frosted panel body
#define NUI_CARD_EDGE     IM_COL32(255, 255, 255,  70)  // 1px inner highlight
#define NUI_TITLE         IM_COL32( 43,  52,  64, 255)  // 致用户
#define NUI_SUBTITLE      IM_COL32( 91, 107, 130, 255)  // To the user
#define NUI_DIVIDER       IM_COL32(120, 120, 120,  70)
#define NUI_QUOTE         IM_COL32(125,  74,  70, 255)  // Rousseau quote
#define NUI_WARN_1        IM_COL32(176,  69,  60, 255)  // disclaimer, line 1
#define NUI_WARN_2        IM_COL32(176,  69,  60, 205)  // ... fading
#define NUI_WARN_3        IM_COL32(176,  69,  60, 150)
#define NUI_PILL_BG       IM_COL32(252, 250, 248, 235)
#define NUI_PILL_ICON     IM_COL32( 58,  58,  62, 255)
#define NUI_PILL_RED      IM_COL32(222,  62,  54, 255)
#define NUI_PILL_GREEN    IM_COL32( 60, 190,  90, 255)
#define NUI_NAV_BG        IM_COL32( 42,  42,  46, 205)
#define NUI_NAV_ACTIVE    IM_COL32( 56, 132, 246, 255)  // blue home circle
#define NUI_NAV_IDLE      IM_COL32(154, 163, 173, 255)
#define NUI_STATUS        IM_COL32(255, 255, 255, 235)
#define NUI_CHANNEL       IM_COL32(236, 200,  60, 255)
#define NUI_HANDLE        IM_COL32(255, 255, 255, 225)
#define NUI_QT_BG         IM_COL32( 28,  30,  36, 199)  // quick-toggle card
#define NUI_QT_EDGE       IM_COL32(255, 255, 255,  26)
#define NUI_QT_CAPTION    IM_COL32(255, 255, 255, 115)
#define NUI_QT_ON         IM_COL32(255, 255, 255, 255)
#define NUI_QT_OFF        IM_COL32(255, 255, 255, 140)
#define NUI_SW_ON         IM_COL32( 56, 132, 246, 255)
#define NUI_SW_OFF        IM_COL32(255, 255, 255,  46)
#define NUI_SW_KNOB       IM_COL32(255, 255, 255, 255)

struct NuiMetrics {
    float s;        // uniform design -> screen scale
    float ox, oy;   // top-left of the scaled design rect on screen
    float sw, sh;   // real screen size
};

// Maps a design-space point into screen space.
static inline ImVec2 NuiP(const NuiMetrics& m, float x, float y) {
    return ImVec2(m.ox + x * m.s, m.oy + y * m.s);
}
static inline float NuiS(const NuiMetrics& m, float v) { return v * m.s; }

static inline NuiMetrics NuiComputeMetrics(float scrW, float scrH) {
    NuiMetrics m{};
    m.sw = scrW; m.sh = scrH;
    const float fx = scrW / NUI_DW, fy = scrH / NUI_DH;
    m.s  = (fx < fy) ? fx : fy;                 // contain, never distort
    m.ox = (scrW - NUI_DW * m.s) * 0.5f;
    m.oy = (scrH - NUI_DH * m.s) * 0.5f;
    return m;
}

// Centred text helper working in design space.
static inline void NuiTextC(ImDrawList* dl, const NuiMetrics& m,
                            float cx, float y, float px, ImU32 col,
                            const char* txt) {
    const float fp = NuiS(m, px);
    const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fp, FLT_MAX, -1.f, txt);
    dl->AddText(NULL, fp, ImVec2(NuiP(m, cx, y).x - sz.x * 0.5f,
                                 NuiP(m, cx, y).y), col, txt);
}
static inline void NuiTextL(ImDrawList* dl, const NuiMetrics& m,
                            float x, float y, float px, ImU32 col,
                            const char* txt) {
    dl->AddText(NULL, NuiS(m, px), NuiP(m, x, y), col, txt);
}

// The frosted panel. ImGui has no gaussian blur, so the glass look is
// built the way the screenshot reads it: a soft dark halo, a light
// translucent body that lets the wallpaper through, and a 1px white
// inner edge catching the light at the top.
static inline void NuiFrostedPanel(ImDrawList* dl, ImVec2 a, ImVec2 b,
                                   float round) {
    for (int i = 10; i >= 1; --i) {            // halo
        const float e = (float)i * 1.6f;
        dl->AddRectFilled(ImVec2(a.x - e, a.y - e), ImVec2(b.x + e, b.y + e),
                          IM_COL32(0, 0, 0, 5), round + e);
    }
    dl->AddRectFilled(a, b, NUI_CARD_FILL, round);
    dl->AddRect(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1),
                NUI_CARD_EDGE, round, 0, 1.5f);
}

// ── The six nav entries, left to right as in the screenshot ──────────
struct NuiNavItem { const char* icon; const char* id; };
static const NuiNavItem kNuiNav[6] = {
    { ICON_FA_HOME,   "nui_home" },
    { ICON_FA_EYE,    "nui_esp"  },
    { ICON_FA_SHIELD, "nui_aim"  },
    { ICON_FA_GEM,    "nui_xpl"  },
    { ICON_FA_USER,   "nui_usr"  },
    { ICON_FA_COG,    "nui_cfg"  },
};

// Draws the floating nav bar and returns the page the user tapped, or
// -1 when nothing was tapped this frame.
static inline int NuiDrawNavBar(ImDrawList* dl, const NuiMetrics& m,
                                int page) {
    const float barW = 800.f, barH = 70.f;
    const float barX = (NUI_DW - barW) * 0.5f, barY = 948.f;
    const ImVec2 a = NuiP(m, barX, barY);
    const ImVec2 b = NuiP(m, barX + barW, barY + barH);
    const float r  = NuiS(m, barH * 0.5f);

    for (int i = 8; i >= 1; --i) {
        const float e = (float)i * 1.5f;
        dl->AddRectFilled(ImVec2(a.x - e, a.y - e), ImVec2(b.x + e, b.y + e),
                          IM_COL32(0, 0, 0, 6), r + e);
    }
    dl->AddRectFilled(a, b, NUI_NAV_BG, r);

    int clicked = -1;
    const float step = barW / 6.f;
    ImFont* iconFont = g_FontNavIcons ? g_FontNavIcons : ImGui::GetFont();
    const float iconPx = NuiS(m, 34.f);

    for (int i = 0; i < 6; ++i) {
        const float cx = barX + step * ((float)i + 0.5f);
        const float cy = barY + barH * 0.5f;
        const ImVec2 c = NuiP(m, cx, cy);

        if (i == page)
            dl->AddCircleFilled(c, NuiS(m, 28.f), NUI_NAV_ACTIVE, 48);

        const ImVec2 gs = iconFont->CalcTextSizeA(iconPx, FLT_MAX, -1.f,
                                                  kNuiNav[i].icon);
        dl->AddText(iconFont, iconPx,
                    ImVec2(c.x - gs.x * 0.5f, c.y - gs.y * 0.5f),
                    (i == page) ? IM_COL32(255, 255, 255, 255) : NUI_NAV_IDLE,
                    kNuiNav[i].icon);

        ImGui::SetCursorScreenPos(ImVec2(c.x - NuiS(m, 32.f),
                                         c.y - NuiS(m, 32.f)));
        if (ImGui::InvisibleButton(kNuiNav[i].id,
                                   ImVec2(NuiS(m, 64.f), NuiS(m, 64.f))))
            clicked = i;
    }
    return clicked;
}

// Top-centre counter pill: live player / bot counts.
static inline void NuiDrawCounterPill(ImDrawList* dl, const NuiMetrics& m,
                                      int players, int bots) {
    const float w = 290.f, h = 62.f;
    const float x = (NUI_DW - w) * 0.5f, y = 58.f;
    const ImVec2 a = NuiP(m, x, y), b = NuiP(m, x + w, y + h);
    dl->AddRectFilled(a, b, NUI_PILL_BG, NuiS(m, h * 0.5f));

    char buf[32];
    ImFont* f = g_FontNavIcons ? g_FontNavIcons : ImGui::GetFont();
    const float ip = NuiS(m, 26.f), tp = NuiS(m, 30.f);

    dl->AddText(f, ip, NuiP(m, x + 34.f, y + 18.f), NUI_PILL_ICON, ICON_FA_USER);
    snprintf(buf, sizeof(buf), "%d", players);
    dl->AddText(NULL, tp, NuiP(m, x + 74.f, y + 15.f), NUI_PILL_RED, buf);

    dl->AddCircleFilled(NuiP(m, x + w * 0.5f, y + h * 0.5f), NuiS(m, 4.f),
                        IM_COL32(150, 150, 150, 200), 12);

    dl->AddText(f, ip, NuiP(m, x + 168.f, y + 18.f), NUI_PILL_GREEN, ICON_FA_ROBOT);
    snprintf(buf, sizeof(buf), "%d", bots);
    dl->AddText(NULL, tp, NuiP(m, x + 212.f, y + 15.f), NUI_PILL_GREEN, buf);
}

// Top-right telegram button. Returns true when tapped.
static inline bool NuiDrawPushButton(ImDrawList* dl, const NuiMetrics& m) {
    const float sz = 66.f, x = NUI_DW - 150.f, y = 18.f;
    const ImVec2 a = NuiP(m, x, y), b = NuiP(m, x + sz, y + sz);
    dl->AddRectFilled(a, b, IM_COL32(90, 90, 96, 190), NuiS(m, 12.f));

    ImFont* f = g_FontNavIcons ? g_FontNavIcons : ImGui::GetFont();
    const float ip = NuiS(m, 32.f);
    const ImVec2 gs = f->CalcTextSizeA(ip, FLT_MAX, -1.f, ICON_FA_PAPER_PLANE);
    dl->AddText(f, ip,
                ImVec2((a.x + b.x) * 0.5f - gs.x * 0.5f,
                       (a.y + b.y) * 0.5f - gs.y * 0.5f),
                IM_COL32(255, 255, 255, 240), ICON_FA_PAPER_PLANE);

    NuiTextC(dl, m, x + sz * 0.5f, y + sz + 6.f, 22.f,
             IM_COL32(255, 255, 255, 230), "Enable Push Notifi");

    ImGui::SetCursorScreenPos(a);
    return ImGui::InvisibleButton("nui_push",
                                  ImVec2(NuiS(m, sz), NuiS(m, sz)));
}

// Bottom-left status block.
static inline void NuiDrawStatus(ImDrawList* dl, const NuiMetrics& m,
                                 const char* status, const char* channel) {
    dl->AddLine(NuiP(m, 148.f, 912.f), NuiP(m, NUI_DW - 145.f, 912.f),
                IM_COL32(255, 255, 255, 60), NuiS(m, 2.f));
    NuiTextL(dl, m, 148.f, 930.f, 30.f, NUI_STATUS,  status);
    NuiTextL(dl, m,  75.f, 994.f, 26.f, NUI_CHANNEL, channel);
}

// Android gesture handle.
static inline void NuiDrawHandle(ImDrawList* dl, const NuiMetrics& m) {
    dl->AddRectFilled(NuiP(m, 900.f, 1046.f), NuiP(m, 1500.f, 1055.f),
                      NUI_HANDLE, NuiS(m, 4.f));
}

// The "致用户" card itself — page 0's content.
static inline void NuiDrawNoticeCard(ImDrawList* dl, const NuiMetrics& m) {
    const float x0 = 675.f, x1 = 1715.f, y0 = 150.f, y1 = 710.f;
    const float cx = (x0 + x1) * 0.5f;
    NuiFrostedPanel(dl, NuiP(m, x0, y0), NuiP(m, x1, y1), NuiS(m, 28.f));

    NuiTextC(dl, m, cx, 222.f, 40.f, NUI_TITLE,    "致 用 户");
    NuiTextC(dl, m, cx, 274.f, 30.f, NUI_SUBTITLE, "To the user");

    dl->AddLine(NuiP(m, x0 + 70.f, 310.f), NuiP(m, x1 - 70.f, 310.f),
                NUI_DIVIDER, NuiS(m, 1.5f));

    NuiTextC(dl, m, cx, 372.f, 27.f, NUI_QUOTE, "人之所以走入迷途，");
    NuiTextC(dl, m, cx, 412.f, 27.f, NUI_QUOTE, "并不是由于他的无知，");
    NuiTextC(dl, m, cx, 452.f, 27.f, NUI_QUOTE, "而是由于他自以为知。");
    NuiTextC(dl, m, cx, 508.f, 25.f, NUI_QUOTE, "—— 卢梭《爱弥儿》");

    dl->AddLine(NuiP(m, x0 + 70.f, 548.f), NuiP(m, x1 - 70.f, 548.f),
                NUI_DIVIDER, NuiS(m, 1.5f));

    NuiTextC(dl, m, cx, 600.f, 23.f, NUI_WARN_1,
             "本产品为个人技术探索，无任何盈利目的");
    NuiTextC(dl, m, cx, 638.f, 23.f, NUI_WARN_2,
             "切勿用于任何违反游戏规则的行为");
    NuiTextC(dl, m, cx, 676.f, 23.f, NUI_WARN_3,
             "技术无罪，使用者的选择决定一切");
}

// ── Quick toggles ────────────────────────────────────────────────────
// A pill switch, drawn rather than themed, because ImGui::Checkbox
// cannot be made to look like this. The whole row is one card under the
// notice card, so the home page is useful and not just a disclaimer.
struct NuiToggle { const char* label; bool* value; };

static inline void NuiSwitch(ImDrawList* dl, const NuiMetrics& m,
                             float cx, float y, bool on, const char* id,
                             bool* value) {
    const float w = 64.f, h = 32.f;
    const float sx = cx - w * 0.5f;
    const ImVec2 a = NuiP(m, sx, y), b = NuiP(m, sx + w, y + h);
    dl->AddRectFilled(a, b, on ? NUI_SW_ON : NUI_SW_OFF, NuiS(m, h * 0.5f));

    const float kx = on ? (sx + w - h * 0.5f) : (sx + h * 0.5f);
    dl->AddCircleFilled(NuiP(m, kx, y + h * 0.5f), NuiS(m, h * 0.5f - 4.f),
                        NUI_SW_KNOB, 32);

    ImGui::SetCursorScreenPos(a);
    if (ImGui::InvisibleButton(id, ImVec2(NuiS(m, w), NuiS(m, h))) && value)
        *value = !*value;
}

static inline void NuiDrawQuickToggles(ImDrawList* dl, const NuiMetrics& m,
                                       NuiToggle* items, int count) {
    if (!items || count <= 0) return;
    const float x0 = 675.f, w = 1040.f, y0 = 732.f, h = 150.f;
    NuiFrostedPanel(dl, NuiP(m, x0, y0), NuiP(m, x0 + w, y0 + h), NuiS(m, 24.f));
    dl->AddRectFilled(NuiP(m, x0, y0), NuiP(m, x0 + w, y0 + h),
                      NUI_QT_BG, NuiS(m, 24.f));
    dl->AddRect(NuiP(m, x0 + 1, y0 + 1), NuiP(m, x0 + w - 1, y0 + h - 1),
                NUI_QT_EDGE, NuiS(m, 24.f), 0, 1.5f);

    NuiTextL(dl, m, x0 + 30.f, y0 + 22.f, 20.f, NUI_QT_CAPTION, "QUICK TOGGLES");

    const float colW = w / (float)count;
    for (int i = 0; i < count; ++i) {
        const float cx = x0 + colW * ((float)i + 0.5f);
        const bool on = items[i].value && *items[i].value;
        NuiTextC(dl, m, cx, y0 + 72.f, 22.f,
                 on ? NUI_QT_ON : NUI_QT_OFF, items[i].label);
        char id[48];
        snprintf(id, sizeof(id), "##nui_sw_%d", i);
        NuiSwitch(dl, m, cx, y0 + 110.f, on, id, items[i].value);
    }
}

// ── Entry point ──────────────────────────────────────────────────────
// Draws the whole shell. `page` is the caller's current tab index and is
// updated in place when a nav icon is tapped. Returns true if the push
// button was tapped this frame.
static inline bool NuiRenderShell(float scrW, float scrH, int& page,
                                  int players, int bots,
                                  const char* status, const char* channel,
                                  NuiToggle* quick = nullptr,
                                  int quickCount = 0) {
    if (scrW < 1.f || scrH < 1.f) return false;
    const NuiMetrics m = NuiComputeMetrics(scrW, scrH);
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    NuiDrawCounterPill(dl, m, players, bots);
    const bool push = NuiDrawPushButton(dl, m);
    if (page == 0) {
        NuiDrawNoticeCard(dl, m);
        NuiDrawQuickToggles(dl, m, quick, quickCount);
    }
    NuiDrawStatus(dl, m, status, channel);

    const int hit = NuiDrawNavBar(dl, m, page);
    if (hit >= 0) page = hit;

    NuiDrawHandle(dl, m);
    return push;
}

#undef NUI_DW
#undef NUI_DH
