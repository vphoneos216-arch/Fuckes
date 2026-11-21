#include "AetherUI.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace AetherUI {
namespace {

constexpr float PW = 1000.f;  // 面板逻辑宽
constexpr float PH = 620.f;   // 面板逻辑高
constexpr float SIDEBAR = 208.f;
constexpr float PI = 3.14159265358979f;
constexpr float TSZ = 1.92f;  // 字号系数：逻辑单位 -> 实际字号

State g;
Bindings gBind;

// ---------------------------------------------------------------- 投影
float gS = 1.f;
float gCx = 0.f, gCy = 0.f;
float gDist = 1500.f;
float R[3][3];
float gN[3];
ImVec2 gMouse(-1.f, -1.f);
bool gMouseOK = false;
float gAlpha = 1.f;

inline ImVec2 Pr(float x, float y, float z = 0.f) {
  const float px = x - PW * 0.5f, py = y - PH * 0.5f;
  const float x1 = R[0][0] * px + R[0][1] * py + R[0][2] * z;
  const float y1 = R[1][0] * px + R[1][1] * py + R[1][2] * z;
  const float z1 = R[2][0] * px + R[2][1] * py + R[2][2] * z;
  float dep = z1 + gDist;
  if (dep < 40.f) dep = 40.f;
  const float f = gDist / dep;
  return ImVec2(gCx + x1 * f * gS, gCy + y1 * f * gS);
}

inline float Ds(float x, float y) {
  const float px = x - PW * 0.5f, py = y - PH * 0.5f;
  const float z1 = R[2][0] * px + R[2][1] * py;
  float dep = z1 + gDist;
  if (dep < 40.f) dep = 40.f;
  return (gDist / dep) * gS;
}

ImDrawList* DL() { return ImGui::GetForegroundDrawList(); }
ImFont* Fnt() { return ImGui::GetFont(); }

ImU32 Col(int r, int gg, int b, float a = 1.f) {
  int aa = (int)(a * 255.f * gAlpha + 0.5f);
  if (aa < 0) aa = 0;
  if (aa > 255) aa = 255;
  return IM_COL32(r, gg, b, aa);
}

struct Pal {
  ImU32 text, textDim, textFaint, accent, accentWash, hair, card, cardEdge;
  ImU32 panel, panelEdge, track, knob, chipText;
};

Pal P() {
  Pal p{};
  if (!g.darkSkin) {
    p.text = Col(34, 40, 48, 0.96f);
    p.textDim = Col(104, 116, 130, 0.95f);
    p.textFaint = Col(140, 152, 166, 0.90f);
    p.accent = Col(45, 122, 246, 1.f);
    p.accentWash = Col(45, 122, 246, 0.14f);
    p.hair = Col(150, 168, 190, 0.40f);
    p.card = Col(255, 255, 255, 0.50f);
    p.cardEdge = Col(255, 255, 255, 0.80f);
    p.panel = Col(250, 252, 255, 0.66f);
    p.panelEdge = Col(255, 255, 255, 0.95f);
    p.track = Col(148, 164, 185, 0.32f);
    p.knob = Col(255, 255, 255, 1.f);
    p.chipText = Col(255, 255, 255, 1.f);
  } else {
    p.text = Col(230, 240, 248, 0.96f);
    p.textDim = Col(148, 164, 178, 0.92f);
    p.textFaint = Col(116, 134, 150, 0.85f);
    p.accent = Col(58, 224, 194, 1.f);
    p.accentWash = Col(58, 224, 194, 0.13f);
    p.hair = Col(110, 150, 160, 0.30f);
    p.card = Col(255, 255, 255, 0.055f);
    p.cardEdge = Col(140, 226, 214, 0.18f);
    p.panel = Col(15, 20, 26, 0.80f);
    p.panelEdge = Col(110, 226, 210, 0.40f);
    p.track = Col(255, 255, 255, 0.14f);
    p.knob = Col(238, 255, 251, 1.f);
    p.chipText = Col(10, 22, 24, 1.f);
  }
  return p;
}

// ---------------------------------------------------------------- 图元
void PathRR(ImDrawList* dl, float x0, float y0, float x1, float y1, float r, float z = 0.f) {
  const float maxR = (x1 - x0) * 0.5f < (y1 - y0) * 0.5f ? (x1 - x0) * 0.5f : (y1 - y0) * 0.5f;
  if (r > maxR) r = maxR;
  const int N = 6;
  const float cx[4] = {x0 + r, x1 - r, x1 - r, x0 + r};
  const float cy[4] = {y0 + r, y0 + r, y1 - r, y1 - r};
  const float a0[4] = {PI, PI * 1.5f, 0.f, PI * 0.5f};
  dl->PathClear();
  for (int c = 0; c < 4; ++c) {
    for (int i = 0; i <= N; ++i) {
      const float a = a0[c] + (PI * 0.5f) * (float)i / (float)N;
      dl->PathLineTo(Pr(cx[c] + cosf(a) * r, cy[c] + sinf(a) * r, z));
    }
  }
}

void FillRR(ImDrawList* dl, float x0, float y0, float x1, float y1, float r, ImU32 col,
            float z = 0.f) {
  PathRR(dl, x0, y0, x1, y1, r, z);
  dl->PathFillConvex(col);
}

void StrokeRR(ImDrawList* dl, float x0, float y0, float x1, float y1, float r, ImU32 col,
              float th = 1.f, float z = 0.f) {
  PathRR(dl, x0, y0, x1, y1, r, z);
  dl->PathStroke(col, ImDrawFlags_Closed, th);
}

void DropShadow(ImDrawList* dl, float x0, float y0, float x1, float y1, float r, float spread,
                float alpha) {
  for (int i = 3; i >= 0; --i) {
    const float k = (float)i;
    FillRR(dl, x0 - spread * k * 0.4f + k * 1.4f, y0 - spread * k * 0.3f + k * 3.6f,
           x1 + spread * k * 0.4f + k * 1.4f, y1 + spread * k * 0.4f + k * 3.6f, r + k * 2.5f,
           IM_COL32(38, 56, 84, (int)(alpha * 255.f / (1.f + k * 1.7f))), -8.f - k * 10.f);
  }
}

void QuadGrad(ImDrawList* dl, ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 ca, ImU32 cb,
              ImU32 cc, ImU32 cd) {
  dl->PrimReserve(6, 4);
  const ImDrawIdx i = (ImDrawIdx)dl->_VtxCurrentIdx;
  const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
  dl->PrimWriteVtx(a, uv, ca);
  dl->PrimWriteVtx(b, uv, cb);
  dl->PrimWriteVtx(c, uv, cc);
  dl->PrimWriteVtx(d, uv, cd);
  dl->PrimWriteIdx(i);
  dl->PrimWriteIdx((ImDrawIdx)(i + 1));
  dl->PrimWriteIdx((ImDrawIdx)(i + 2));
  dl->PrimWriteIdx(i);
  dl->PrimWriteIdx((ImDrawIdx)(i + 2));
  dl->PrimWriteIdx((ImDrawIdx)(i + 3));
}

// 圆角矩形某一行（y 处）的左右边界
inline void RowSpan(float y, float y0, float y1, float x0, float x1, float r, float* lx,
                    float* rx) {
  float inset = 0.f;
  if (y < y0 + r) {
    const float dy = (y0 + r) - y;
    const float k = r * r - dy * dy;
    inset = r - sqrtf(k > 0.f ? k : 0.f);
  } else if (y > y1 - r) {
    const float dy = y - (y1 - r);
    const float k = r * r - dy * dy;
    inset = r - sqrtf(k > 0.f ? k : 0.f);
  }
  *lx = x0 + inset;
  *rx = x1 - inset;
}

// ---------------------------------------------------------------- 文本（逐字投影，带透视伸缩）
float GlyphW(float sizePx, const char* g0, int n) {
  char buf[8];
  memcpy(buf, g0, n);
  buf[n] = 0;
  return Fnt()->CalcTextSizeA(sizePx, FLT_MAX, 0.f, buf).x;
}

void Txt(float x, float y, float size, ImU32 col, const char* s, int align = 0,
         float track = 0.f) {
  const float S = size * TSZ;
  if (S < 2.f) return;
  ImDrawList* dl = DL();

  float wu = 0.f;
  for (const char* q = s; *q;) {
    const char* nxt = q + 1;
    while ((*nxt & 0xC0) == 0x80) ++nxt;
    const int n = (int)(nxt - q);
    if (n <= 0 || n >= 8) break;
    wu += GlyphW(S, q, n) + track;
    q = nxt;
  }
  float pen = x;
  if (align == 1) pen = x - wu * 0.5f;
  else if (align == 2) pen = x - wu;

  for (const char* q = s; *q;) {
    const char* nxt = q + 1;
    while ((*nxt & 0xC0) == 0x80) ++nxt;
    const int n = (int)(nxt - q);
    if (n <= 0 || n >= 8) break;
    char buf[8];
    memcpy(buf, q, n);
    buf[n] = 0;
    const float ds = Ds(pen, y);
    const float px = S * ds;
    const ImVec2 p0 = Pr(pen, y);
    dl->AddText(Fnt(), px, ImVec2(p0.x, p0.y - px * 0.18f), col, buf);
    pen += GlyphW(S, q, n) + track;
    q = nxt;
  }
}

void TxtB(float x, float y, float size, ImU32 col, const char* s, int align = 0,
          float track = 0.f) {
  Txt(x - 0.16f, y, size, col, s, align, track);
  Txt(x + 0.16f, y, size, col, s, align, track);
}

float TxtWU(float size, const char* s) {
  const float S = size * TSZ;
  float wu = 0.f;
  for (const char* q = s; *q;) {
    const char* nxt = q + 1;
    while ((*nxt & 0xC0) == 0x80) ++nxt;
    const int n = (int)(nxt - q);
    if (n <= 0 || n >= 8) break;
    wu += GlyphW(S, q, n);
    q = nxt;
  }
  return wu;
}

void TxtWrap(float x, float y, float w, float size, ImU32 col, const char* s, float lineH = 0.f) {
  const float S = size * TSZ;
  if (S < 2.f) return;
  if (lineH <= 0.f) lineH = size * 1.8f;
  char line[512];
  int li = 0;
  float cx = 0.f, cy = 0.f;
  auto flush = [&]() {
    if (li <= 0) return;
    line[li] = 0;
    Txt(x, y + cy, size, col, line, 0);
    li = 0;
    cx = 0.f;
    cy += lineH;
  };
  for (const char* p = s; *p;) {
    const char* nxt = p + 1;
    while ((*nxt & 0xC0) == 0x80) ++nxt;
    const int n = (int)(nxt - p);
    if (n <= 0 || n >= 8) break;
    char ch[8];
    memcpy(ch, p, n);
    ch[n] = 0;
    if (*p == '\n') {
      flush();
      p = nxt;
      continue;
    }
    const float cw = GlyphW(S, p, n);
    if (cx + cw > w && li > 0) flush();
    if (li + n < 500) {
      memcpy(line + li, p, n);
      li += n;
    }
    cx += cw;
    p = nxt;
  }
  flush();
}

// ---------------------------------------------------------------- 交互
uint32_t gActive = 0;
uint32_t H(const char* s) {
  uint32_t h = 2166136261u;
  while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
  return h;
}

bool Hover(float x0, float y0, float x1, float y1) {
  if (!gMouseOK) return false;
  return gMouse.x >= x0 && gMouse.x <= x1 && gMouse.y >= y0 && gMouse.y <= y1;
}

bool Click(uint32_t id, float x0, float y0, float x1, float y1) {
  const bool hov = Hover(x0, y0, x1, y1);
  if (hov && ImGui::IsMouseClicked(0)) gActive = id;
  bool fired = false;
  if (ImGui::IsMouseReleased(0) && gActive == id) {
    fired = hov;
    gActive = 0;
  }
  return fired;
}

bool Held(uint32_t id, float x0, float y0, float x1, float y1) {
  if (Hover(x0, y0, x1, y1) && ImGui::IsMouseClicked(0)) gActive = id;
  return gActive == id && ImGui::IsMouseDown(0);
}

ImU32 AccentCol(int i) {
  if (!g.darkSkin) {
    const ImU32 c[3] = {IM_COL32(45, 122, 246, 255), IM_COL32(56, 186, 248, 255),
                        IM_COL32(32, 96, 226, 255)};
    return c[i % 3];
  }
  const ImU32 c[3] = {IM_COL32(58, 224, 194, 255), IM_COL32(120, 238, 255, 255),
                      IM_COL32(170, 255, 205, 255)};
  return c[i % 3];
}

// ================================================================ 控件
void Toggle(ImDrawList* dl, uint32_t id, float x, float y, bool* v, float sc = 1.f) {
  const Pal p = P();
  const float w = 34.f * sc, h = 18.f * sc;
  const bool hov = Hover(x, y, x + w, y + h);
  if (Click(id, x, y, x + w, y + h)) *v = !*v;
  FillRR(dl, x, y, x + w, y + h, h * 0.5f, *v ? p.accent : p.track);
  if (hov && !*v) StrokeRR(dl, x, y, x + w, y + h, h * 0.5f, p.accent, 1.f);
  const float kx = *v ? x + w - h * 0.5f : x + h * 0.5f;
  dl->AddCircleFilled(Pr(kx, y + h * 0.5f), 7.f * sc * Ds(x, y), p.knob, 24);
}

void Slider(ImDrawList* dl, uint32_t id, float x, float y, float w, float* v, float vmin,
            float vmax) {
  const float trackY = y + 6.f;
  const bool hov = Hover(x, y - 6.f, x + w, y + 18.f);
  if (Held(id, x, y - 6.f, x + w, y + 18.f)) {
    float t = (gMouse.x - x) / w;
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    *v = vmin + (vmax - vmin) * t;
  }
  const Pal p = P();
  float t = (*v - vmin) / (vmax - vmin);
  if (t < 0.f) t = 0.f;
  if (t > 1.f) t = 1.f;
  dl->AddLine(Pr(x, trackY), Pr(x + w, trackY), p.track, 2.4f * gS);
  dl->AddLine(Pr(x, trackY), Pr(x + w * t, trackY), p.accent, 2.4f * gS);
  const float kx = x + w * t;
  const float kr = (hov ? 5.4f : 4.6f) * Ds(x, y);
  dl->AddCircleFilled(Pr(kx, trackY), kr, p.knob, 20);
  dl->AddCircle(Pr(kx, trackY), kr, p.accent, 20, 1.3f);
}

void Segmented(ImDrawList* dl, uint32_t id, float x, float y, float w, float h, int* idx,
               const char** items, int n) {
  const Pal p = P();
  FillRR(dl, x, y, x + w, y + h, h * 0.5f,
         g.darkSkin ? Col(0, 0, 0, 0.28f) : Col(148, 164, 185, 0.20f));
  const float cw = w / (float)n;
  for (int i = 0; i < n; ++i) {
    const float bx = x + cw * (float)i;
    if (Click(id + (uint32_t)i, bx, y, bx + cw, y + h)) *idx = i;
    const bool sel = (*idx == i);
    if (sel) {
      FillRR(dl, bx + 2.f, y + 2.f, bx + cw - 2.f, y + h - 2.f, (h - 4.f) * 0.5f,
             g.darkSkin ? Col(58, 224, 194, 0.16f) : Col(255, 255, 255, 0.96f));
      if (g.darkSkin)
        StrokeRR(dl, bx + 2.f, y + 2.f, bx + cw - 2.f, y + h - 2.f, (h - 4.f) * 0.5f,
                 Col(58, 224, 194, 0.55f), 1.f);
    }
    Txt(bx + cw * 0.5f, y + h * 0.5f - 6.0f, 8.0f, sel ? p.text : p.textDim, items[i], 1);
  }
}

bool Button(ImDrawList* dl, uint32_t id, float x, float y, float w, float h, const char* label,
            bool primary = false) {
  const bool hov = Hover(x, y, x + w, y + h);
  const bool down = hov && ImGui::IsMouseDown(0) && gActive == id;
  const bool fired = Click(id, x, y, x + w, y + h);
  const Pal p = P();
  const ImU32 bg = primary ? p.accent
                           : (g.darkSkin ? Col(255, 255, 255, hov ? 0.12f : 0.07f)
                                         : Col(255, 255, 255, hov ? 0.92f : 0.62f));
  FillRR(dl, x, y, x + w, y + h, 8.f, down ? Col(30, 60, 110, 0.30f) : bg);
  if (!primary) StrokeRR(dl, x, y, x + w, y + h, 8.f, p.cardEdge, 1.f);
  Txt(x + w * 0.5f, y + h * 0.5f - 6.6f, 8.4f, primary ? p.chipText : p.text, label, 1);
  return fired;
}

void CheckRow(ImDrawList* dl, uint32_t id, float x, float y, float w, bool* v, const char* label) {
  if (Click(id, x, y - 3.f, x + w, y + 13.f)) *v = !*v;
  const Pal p = P();
  FillRR(dl, x, y, x + 11.f, y + 11.f, 3.2f, *v ? p.accent : Col(255, 255, 255, 0.30f));
  if (!*v) StrokeRR(dl, x, y, x + 11.f, y + 11.f, 3.2f, p.textFaint, 1.f);
  if (*v) {
    dl->AddLine(Pr(x + 2.6f, y + 5.8f), Pr(x + 4.6f, y + 8.2f), Col(255, 255, 255, 1.f),
                1.6f * gS);
    dl->AddLine(Pr(x + 4.6f, y + 8.2f), Pr(x + 8.6f, y + 3.0f), Col(255, 255, 255, 1.f),
                1.6f * gS);
  }
  Txt(x + 18.f, y - 2.f, 8.0f, *v ? p.text : p.textDim, label, 0);
}

// ---------------------------------------------------------------- 绑定行
// Draws a labelled toggle row only when the flag is actually bound.
// Returns the y advance so a column can pack only what exists.
float BindRow(ImDrawList* dl, const char* id, float x, float y, float w,
              bool* v, const char* label) {
  if (!v) return 0.f;
  CheckRow(dl, H(id), x, y, w, v, label);
  return 31.f;
}

float BindToggle(ImDrawList* dl, const char* id, float x, float y, float w,
                 bool* v, const char* label) {
  if (!v) return 0.f;
  const Pal p = P();
  Txt(x, y + 5.f, 5.4f, p.textDim, label, 0);
  Toggle(dl, H(id), x + w - 36.f, y, v);
  return 40.f;
}

float BindSliderI(ImDrawList* dl, const char* id, float x, float y, float w,
                  int* v, int lo, int hi, const char* label, const char* unit) {
  if (!v) return 0.f;
  const Pal p = P();
  Txt(x, y, 5.4f, p.textDim, label, 0);
  char b[24];
  snprintf(b, sizeof(b), "%d%s", *v, unit ? unit : "");
  Txt(x + w, y, 5.0f, p.text, b, 2);
  float f = (float)*v;
  Slider(dl, H(id), x, y + 14.f, w, &f, (float)lo, (float)hi);
  *v = (int)(f + 0.5f);
  return 40.f;
}

float BindSliderF(ImDrawList* dl, const char* id, float x, float y, float w,
                  float* v, float lo, float hi, const char* label,
                  const char* fmt) {
  if (!v) return 0.f;
  const Pal p = P();
  Txt(x, y, 5.4f, p.textDim, label, 0);
  char b[24];
  snprintf(b, sizeof(b), fmt ? fmt : "%.0f", *v);
  Txt(x + w, y, 5.0f, p.text, b, 2);
  Slider(dl, H(id), x, y + 14.f, w, v, lo, hi);
  return 40.f;
}

// ================================================================ 图标
void IconEye(ImDrawList* dl, float cx, float cy, ImU32 col, float s) {
  dl->AddEllipse(Pr(cx, cy), 7.4f * s * gS, 4.8f * s * gS, col, 0.f, 28, 1.3f);
  dl->AddCircleFilled(Pr(cx, cy), 1.9f * s * Ds(cx, cy), col, 16);
}

void IconGear(ImDrawList* dl, float cx, float cy, ImU32 col, float s) {
  dl->AddCircle(Pr(cx, cy), 3.8f * s * gS, col, 20, 1.3f);
  for (int i = 0; i < 8; ++i) {
    const float a = (float)i * PI * 0.25f;
    dl->AddLine(Pr(cx + cosf(a) * 5.0f * s, cy + sinf(a) * 5.0f * s),
                Pr(cx + cosf(a) * 6.9f * s, cy + sinf(a) * 6.9f * s), col, 1.4f * gS);
  }
}

void IconCross(ImDrawList* dl, float cx, float cy, ImU32 col, float s) {
  const float r = 7.0f * s;
  dl->AddCircle(Pr(cx, cy), r * Ds(cx, cy), col, 22, 1.3f);
  dl->AddLine(Pr(cx - r - 2.2f, cy), Pr(cx - r + 2.2f, cy), col, 1.3f);
  dl->AddLine(Pr(cx + r - 2.2f, cy), Pr(cx + r + 2.2f, cy), col, 1.3f);
  dl->AddLine(Pr(cx, cy - r - 2.2f), Pr(cx, cy - r + 2.2f), col, 1.3f);
  dl->AddLine(Pr(cx, cy + r - 2.2f), Pr(cx, cy + r + 2.2f), col, 1.3f);
  dl->AddCircleFilled(Pr(cx, cy), 1.5f * Ds(cx, cy), col, 8);
}

void IconBolt(ImDrawList* dl, float cx, float cy, ImU32 col, float s) {
  const float h = 8.0f * s, w = 4.6f * s;
  dl->AddTriangleFilled(Pr(cx + w * 0.2f, cy - h), Pr(cx - w, cy + 0.8f),
                        Pr(cx + w * 0.1f, cy + 0.8f), col);
  dl->AddTriangleFilled(Pr(cx - w * 0.1f, cy - 0.8f), Pr(cx + w, cy - 0.8f),
                        Pr(cx - w * 0.2f, cy + h), col);
}

void IconTheme(ImDrawList* dl, float cx, float cy, ImU32 col, float s) {
  dl->AddCircleFilled(Pr(cx - 3.0f * s, cy - 1.4f * s), 2.0f * s * Ds(cx, cy), col, 14);
  dl->AddCircle(Pr(cx + 2.2f * s, cy + 1.8f * s), 2.8f * s * gS, col, 18, 1.3f);
}

// ================================================================ 侧栏
void DrawSidebar(ImDrawList* dl) {
  const Pal p = P();

  FillRR(dl, 20.f, 20.f, 64.f, 64.f, 12.f, Col(255, 255, 255, 0.55f));
  QuadGrad(dl, Pr(21.f, 21.f), Pr(63.f, 21.f), Pr(63.f, 63.f), Pr(21.f, 63.f),
           Col(255, 178, 152, 0.95f), Col(252, 150, 172, 0.95f), Col(122, 152, 236, 0.95f),
           Col(96, 136, 226, 0.95f));
  Txt(42.f, 34.f, 10.5f, Col(255, 255, 255, 0.95f), "A", 1);

  TxtB(74.f, 20.f, 15.f, p.text, "AETHER", 0, 0.4f);
  Txt(74.f, 48.f, 4.6f, p.textDim, "LS KERNEL EDITION", 0, 1.5f);

  Txt(22.f, 88.f, 4.8f, p.textFaint, "NAVIGATION", 0, 1.8f);

  const char* cn[PAGE_COUNT] = {"视觉", "瞄准", "功能", "设置", "主题"};
  const char* en[PAGE_COUNT] = {"VISUAL", "AIM", "EXPLOIT", "SETTINGS", "THEME"};
  for (int i = 0; i < PAGE_COUNT; ++i) {
    const float y = 106.f + 42.f * (float)i;
    const bool act = (g.page == i);
    if (act) {
      FillRR(dl, 14.f, y, 194.f, y + 38.f, 9.f, Col(255, 255, 255, g.darkSkin ? 0.09f : 0.88f));
      FillRR(dl, 14.f, y + 8.f, 16.4f, y + 30.f, 1.2f, p.accent);
    }
    const ImU32 ic = act ? p.accent : p.textDim;
    if (i == PAGE_VISUAL) IconEye(dl, 31.f, y + 16.f, ic, 0.95f);
    else if (i == PAGE_AIM) IconCross(dl, 31.f, y + 16.f, ic, 0.95f);
    else if (i == PAGE_EXPLOIT) IconBolt(dl, 31.f, y + 16.f, ic, 0.95f);
    else if (i == PAGE_SETTINGS) IconGear(dl, 31.f, y + 16.f, ic, 0.95f);
    else IconTheme(dl, 31.f, y + 16.f, ic, 0.95f);
    Txt(46.f, y + 3.f, 7.2f, act ? p.text : p.textDim, cn[i], 0);
    Txt(46.f, y + 21.f, 4.3f, act ? p.accent : p.textFaint, en[i], 0, 1.3f);
    if (Click(H(en[i]), 14.f, y, 194.f, y + 38.f)) g.page = i;
  }

  // 3D 旋转手柄
  Txt(20.f, 442.f, 5.4f, p.textFaint, "3D 旋转", 0, 1.0f);
  const bool hov = Hover(14.f, 462.f, 194.f, 512.f);
  const bool held = Held(H("3d.handle"), 14.f, 462.f, 194.f, 512.f);
  StrokeRR(dl, 14.f, 462.f, 194.f, 512.f, 9.f,
           held ? p.accent : (hov ? Col(110, 165, 240, 0.60f) : p.hair), 1.f);
  if (held) FillRR(dl, 14.f, 462.f, 194.f, 512.f, 9.f, p.accentWash);
  const float ix = 30.f, iy = 480.f;
  dl->PathArcTo(Pr(ix, iy), 7.f * gS, 0.7f, 4.7f, 20);
  dl->PathStroke(p.accent, 0, 1.5f);
  dl->AddTriangleFilled(Pr(ix + 5.6f, iy - 5.6f), Pr(ix + 8.6f, iy - 1.0f), Pr(ix + 2.0f, iy - 0.6f),
                        p.accent);
  Txt(46.f, 470.f, 5.6f, p.textDim, "拖动旋转视角", 0);
  Txt(46.f, 486.f, 4.8f, p.textFaint, "松手回弹", 0, 0.5f);
  Txt(46.f, 498.f, 4.8f, p.textFaint, "双击复位", 0, 0.5f);

  // 底部状态
  dl->AddCircleFilled(Pr(22.f, 548.f), 2.0f * Ds(22.f, 548.f), Col(58, 208, 128, 0.95f), 12);
  Txt(28.f, 542.f, 4.8f, Col(56, 190, 120, 0.95f), "LS MENU READY", 0, 1.2f);
  Txt(20.f, 560.f, 4.5f, p.textFaint, "SurfaceFlinger overlay", 0);
  Txt(20.f, 574.f, 4.5f, p.textFaint, "音量 + 打开菜单", 0);
  Txt(20.f, 586.f, 4.5f, p.textFaint, "音量 - 隐藏菜单", 0);
  Txt(20.f, 600.f, 4.2f, p.textFaint, "AETHER UI / 1.3", 0, 0.7f);

  FillRR(dl, 178.f, 598.f, 194.f, 610.f, 3.f, Col(255, 255, 255, 0.28f));
  Txt(186.f, 599.f, 4.2f, p.textDim, "VK", 1);

  dl->AddLine(Pr(SIDEBAR, 0.f), Pr(SIDEBAR, PH), p.hair, 1.f);
}

// ================================================================ 卡片
void CardBg(ImDrawList* dl, float x0, float y0, float x1, float y1, float r = 12.f) {
  const Pal p = P();
  FillRR(dl, x0, y0, x1, y1, r, p.card);
  dl->AddLine(Pr(x0 + 14.f, y0 + 1.f), Pr(x1 - 14.f, y0 + 1.f),
              Col(255, 255, 255, g.darkSkin ? 0.14f : 0.60f), 1.2f);
  StrokeRR(dl, x0, y0, x1, y1, r, p.cardEdge, 1.f);
}

void CardHead(float x, float y, const char* cn, const char* en) {
  const Pal p = P();
  TxtB(x, y, 9.0f, p.text, cn, 0);
  Txt(x + TxtWU(9.0f, cn) + 10.f, y + 2.4f, 4.5f, p.textFaint, en, 0, 1.3f);
}

// ================================================================ 封面（逐格采样，贴合圆角）
// 用「天空 + 地平线 + 日光 + 光斑」的抽象构图代替插画，避免引入外部图片
void CoverArt(ImDrawList* dl, float x0, float y0, float x1, float y1, float r, int idx) {
  const int ROWS = 40, COLS = 24;
  ImU32 sky0, sky1, field0, field1, sun, lite;
  float sunX = 0.30f, sunY = 0.36f, sunR = 0.34f;
  if (idx == 0) {  // 晴绿
    sky0 = Col(255, 244, 206);
    sky1 = Col(196, 214, 160);
    field0 = Col(126, 156, 118);
    field1 = Col(74, 100, 82);
    sun = Col(255, 252, 226);
    lite = Col(255, 236, 176);
  } else if (idx == 1) {  // 薄雾
    sky0 = Col(238, 243, 250);
    sky1 = Col(176, 190, 206);
    field0 = Col(138, 150, 166);
    field1 = Col(96, 108, 124);
    sun = Col(255, 255, 255);
    lite = Col(226, 236, 246);
    sunX = 0.62f;
    sunR = 0.40f;
  } else {  // 樱白
    sky0 = Col(255, 230, 236);
    sky1 = Col(236, 178, 198);
    field0 = Col(196, 146, 172);
    field1 = Col(132, 96, 128);
    sun = Col(255, 246, 248);
    lite = Col(255, 214, 226);
    sunX = 0.40f;
    sunY = 0.32f;
  }
  auto mix = [](ImU32 a, ImU32 b, float t) {
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    const int ar = a & 0xFF, ag = (a >> 8) & 0xFF, ab = (a >> 16) & 0xFF;
    const int br = b & 0xFF, bg = (b >> 8) & 0xFF, bb = (b >> 16) & 0xFF;
    return IM_COL32((int)(ar + (br - ar) * t), (int)(ag + (bg - ag) * t),
                    (int)(ab + (bb - ab) * t), 255);
  };
  const float kHorizon = 0.62f;
  const ImU32 amask = ((ImU32)(255.f * gAlpha)) << 24;
  // 固定光斑位置（每个主题不同，制造"景深"
  const float bokeh[5][4] = {{0.20f, 0.16f, 0.055f, 0.55f},
                             {0.72f, 0.24f, 0.040f, 0.40f},
                             {0.86f, 0.52f, 0.070f, 0.30f},
                             {0.34f, 0.48f, 0.030f, 0.45f},
                             {0.58f, 0.10f, 0.028f, 0.35f}};

  for (int ry = 0; ry < ROWS; ++ry) {
    const float ya = y0 + (y1 - y0) * (float)ry / (float)ROWS;
    const float yb = y0 + (y1 - y0) * (float)(ry + 1) / (float)ROWS;
    float lx, rx;
    RowSpan((ya + yb) * 0.5f, y0, y1, x0, x1, r, &lx, &rx);
    for (int cxi = 0; cxi < COLS; ++cxi) {
      const float xa = lx + (rx - lx) * (float)cxi / (float)COLS;
      const float xb = lx + (rx - lx) * (float)(cxi + 1) / (float)COLS;
      auto sample = [&](float sx2, float sy2) -> ImU32 {
        const float u = (sx2 - x0) / (x1 - x0), v = (sy2 - y0) / (y1 - y0);
        // 天空 -> 地面，交界处柔和过渡
        const float tSk = v / kHorizon;
        ImU32 skyc = mix(sky0, sky1, tSk * tSk * 0.85f + u * 0.10f);
        ImU32 gnd = mix(field0, field1, (v - kHorizon) / (1.f - kHorizon));
        float bt = (v - kHorizon) / 0.10f + 0.5f;
        ImU32 c = mix(skyc, gnd, bt);
        // 日光
        const float dx = (u - sunX) * 1.0f, dy = (v - sunY) * 1.25f;
        const float dd = sqrtf(dx * dx + dy * dy);
        if (dd < sunR) {
          const float g = 1.f - dd / sunR;
          c = mix(c, sun, powf(g, 2.2f) * 0.85f);
        }
        // 光斑
        for (int b = 0; b < 5; ++b) {
          const float bd = sqrtf((u - bokeh[b][0]) * (u - bokeh[b][0]) * 2.0f +
                                 (v - bokeh[b][1]) * (v - bokeh[b][1]));
          if (bd < bokeh[b][2] * 2.6f) {
            const float g = 1.f - bd / (bokeh[b][2] * 2.6f);
            c = mix(c, lite, powf(g, 2.0f) * bokeh[b][3] * 0.55f);
          }
        }
        // 底部轻微压暗
        if (v > 0.82f) c = mix(c, IM_COL32(40, 48, 62, 255), (v - 0.82f) * 1.1f);
        return (c & 0x00FFFFFF) | amask;
      };
      const ImU32 c00 = sample(xa, ya), c10 = sample(xb, ya), c11 = sample(xb, yb),
                  c01 = sample(xa, yb);
      QuadGrad(dl, Pr(xa, ya), Pr(xb, ya), Pr(xb, yb), Pr(xa, yb), c00, c10, c11, c01);
    }
  }
  // 底部压暗，保证白色标题可读
  const int SN = 26;
  for (int i = 0; i < SN; ++i) {
    const float t = (float)i / (float)SN;
    const float ya = y1 - 62.f + 62.f * t;
    const float yb = y1 - 62.f + 62.f * (1.f / (float)SN) + 62.f * t;
    float lx, rx;
    RowSpan((ya + yb) * 0.5f, y0, y1, x0, x1, r, &lx, &rx);
    const int al = (int)(120.f * powf(t, 2.0f) * gAlpha);
    const ImU32 c = IM_COL32(10, 16, 26, al);
    QuadGrad(dl, Pr(lx, ya), Pr(rx, ya), Pr(rx, yb), Pr(lx, yb), c, c, c, c);
  }
}

// ================================================================ 页面：主题
const char* kThemeCn[3] = {"晴绿", "薄雾", "樱白"};
const char* kThemeEn[3] = {"SUNLIT", "MIST", "SAKURA"};

void PageTheme(ImDrawList* dl) {
  const Pal p = P();
  const float X = SIDEBAR + 24.f;

  TxtB(X, 36.f, 15.5f, p.text, "主题", 0);
  Txt(X + TxtWU(15.5f, "主题") + 8.f, 42.f, 4.6f, p.textFaint, "THEME", 0, 1.5f);
  Txt(X, 78.f, 5.4f, p.textDim, "为整个菜单选择一套新的气氛。", 0);

  const float cy0 = 100.f, ch = 116.f, cw = 232.f, gap = 13.f;
  for (int i = 0; i < 3; ++i) {
    const float cx0 = X + (cw + gap) * (float)i;
    const float cx1 = cx0 + cw, cy1 = cy0 + ch;
    const bool sel = (g.themeIdx == i);
    DropShadow(dl, cx0, cy0, cx1, cy1, 12.f, 3.5f, sel ? 0.18f : 0.10f);
    CoverArt(dl, cx0, cy0, cx1, cy1, 12.f, i);
    StrokeRR(dl, cx0, cy0, cx1, cy1, 12.f, sel ? p.accent : Col(255, 255, 255, 0.45f),
             sel ? 1.8f : 1.f);
    Txt(cx0 + 12.f, cy1 - 40.f, 8.2f, Col(255, 255, 255, 0.98f), kThemeCn[i], 0);
    Txt(cx0 + 12.f, cy1 - 22.f, 4.2f, Col(255, 255, 255, 0.78f), kThemeEn[i], 0, 1.3f);
    if (sel) {
      FillRR(dl, cx1 - 54.f, cy1 - 26.f, cx1 - 10.f, cy1 - 11.f, 6.f, Col(255, 255, 255, 0.94f));
      Txt(cx1 - 32.f, cy1 - 24.5f, 4.8f, Col(38, 58, 88, 0.95f), "使用中", 1);
    }
    if (Click(H(kThemeEn[i]), cx0, cy0, cx1, cy1)) g.themeIdx = i;
  }

  // 左卡：外观参数
  const float lx0 = X, ly0 = 232.f, lx1 = X + 350.f, ly1 = 552.f;
  CardBg(dl, lx0, ly0, lx1, ly1);
  CardHead(lx0 + 18.f, ly0 + 16.f, "外观参数", "APPEARANCE");

  Txt(lx0 + 18.f, ly0 + 48.f, 5.4f, p.textDim, "强调色", 0);
  for (int i = 0; i < 3; ++i) {
    const float dx = lx0 + 118.f + 24.f * (float)i;
    const bool sel = (g.accentIdx == i);
    dl->AddCircleFilled(Pr(dx, ly0 + 53.f), 7.0f * Ds(dx, ly0), AccentCol(i), 20);
    if (sel) dl->AddCircle(Pr(dx, ly0 + 53.f), 10.2f * Ds(dx, ly0), p.accent, 24, 1.2f);
    if (Click(H(i == 0 ? "ac0" : i == 1 ? "ac1" : "ac2"), dx - 10.f, ly0 + 43.f, dx + 10.f,
              ly0 + 63.f))
      g.accentIdx = i;
  }
  Txt(lx0 + 196.f, ly0 + 48.f, 5.4f, p.textDim, "下雨特效", 0);
  Toggle(dl, H("rain"), lx0 + 258.f, ly0 + 44.f, &g.rainFx);

  Txt(lx0 + 18.f, ly0 + 84.f, 5.4f, p.textDim, "当前背景", 0);
  Txt(lx0 + 332.f, ly0 + 84.f, 5.0f, p.accent, "壁纸", 2);
  Txt(lx0 + 18.f, ly0 + 100.f, 4.3f, p.textFaint, "背景如透镜显示，不做高斯模糊", 0);

  const char* names[3] = {"菜单大小", "字体大小", "菜单透明度"};
  float* vals[3] = {&g.menuScale, &g.fontSize, &g.menuAlpha};
  for (int i = 0; i < 3; ++i) {
    const float ry = ly0 + 146.f + 44.f * (float)i;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", (int)(*vals[i] * 100.f + 0.5f));
    Txt(lx0 + 18.f, ry - 18.f, 5.4f, p.textDim, names[i], 0);
    Txt(lx0 + 332.f, ry - 18.f, 5.0f, p.text, buf, 2);
    float tmp = (*vals[i] - 0.7f) / 0.6f;
    if (tmp < 0.f) tmp = 0.f;
    if (tmp > 1.f) tmp = 1.f;
    char sid[8] = {'s', (char)('0' + i), 0};
    Slider(dl, H(sid), lx0 + 18.f, ry, 314.f, &tmp, 0.f, 1.f);
    *vals[i] = 0.7f + tmp * 0.6f;
  }

  // 右卡：界面风格
  const float rx0 = lx1 + 12.f, rx1 = 978.f;
  CardBg(dl, rx0, ly0, rx1, ly1);
  CardHead(rx0 + 18.f, ly0 + 16.f, "界面风格", "SKIN / EFFECTS");

  const char* skins[2] = {"浅雾", "暗夜全息"};
  int skinIdx = g.darkSkin ? 1 : 0;
  Segmented(dl, H("skin"), rx0 + 18.f, ly0 + 40.f, rx1 - rx0 - 36.f, 24.f, &skinIdx, skins, 2);
  g.darkSkin = (skinIdx == 1);

  TxtWrap(rx0 + 18.f, ly0 + 76.f, rx1 - rx0 - 36.f, 4.7f, p.textDim,
          g.darkSkin ? "深色玻璃质感，青绿描边，随视角读取深色星空。"
                     : "浅色毛玻璃加薄雾，透镜更柔，更接近原生 AETHER。");

  Txt(rx0 + 18.f, ly0 + 118.f, 5.4f, p.text, "3D 立体", 0);
  Toggle(dl, H("use3d"), rx0 + 74.f, ly0 + 113.f, &g.use3D);
  TxtWrap(rx0 + 18.f, ly0 + 140.f, rx1 - rx0 - 36.f, 4.4f, p.textFaint,
          "陀螺透视 + 边缘高光。左侧「3D 旋转」区域拖动可改视角。");

  Txt(rx0 + 18.f, ly0 + 204.f, 5.4f, p.textDim, "菜单帧率", 0);
  char fb[16];
  snprintf(fb, sizeof(fb), "%d FPS", (int)g.fpsTarget);
  Txt(rx1 - 18.f, ly0 + 204.f, 5.0f, p.text, fb, 2);
  Slider(dl, H("fps"), rx0 + 18.f, ly0 + 218.f, rx1 - rx0 - 36.f, &g.fpsTarget, 60.f, 144.f);
  TxtWrap(rx0 + 18.f, ly0 + 242.f, rx1 - rx0 - 36.f, 4.2f, p.textFaint,
          "帧率越高越顺滑，但也越耗电。");

  Txt(X, 566.f, 4.6f, p.textFaint, "写入 /data/local/AETHER/config.conf", 0);
}

// ================================================================ 页面：视觉
void PageVisual(ImDrawList* dl) {
  const Pal p = P();
  const Bindings& b = gBind;
  const float X = SIDEBAR + 24.f;

  TxtB(X, 36.f, 15.5f, p.text, "视觉", 0);
  Txt(X + TxtWU(15.5f, "视觉") + 8.f, 42.f, 4.6f, p.textFaint, "VISUAL", 0, 1.5f);
  Txt(X, 78.f, 5.4f, p.textDim, "ESP 绘制 · 提示 · 字号", 0);

  const float y0 = 100.f, y1 = 520.f;
  const float wCol = 236.f, gap = 14.f;
  const float c1 = X, c2 = X + wCol + gap, c3 = X + (wCol + gap) * 2.f;

  // ---- 绘制元素 ----
  CardBg(dl, c1, y0, c1 + wCol, y1);
  CardHead(c1 + 18.f, y0 + 16.f, "绘制元素", "DRAW");
  {
    float y = y0 + 48.f;
    const float w = wCol - 36.f;
    y += BindRow(dl, "v.box",  c1 + 18.f, y, w, b.showBox,      "Box ESP");
    y += BindRow(dl, "v.skel", c1 + 18.f, y, w, b.showSkeleton, "Skeleton");
    y += BindRow(dl, "v.name", c1 + 18.f, y, w, b.showName,     "Name");
    y += BindRow(dl, "v.hp",   c1 + 18.f, y, w, b.showHealth,   "Health");
    y += BindRow(dl, "v.dist", c1 + 18.f, y, w, b.showDistance, "Distance");
    y += BindRow(dl, "v.line", c1 + 18.f, y, w, b.showLine,     "ESP Line");
  }

  // ---- 提示 ----
  CardBg(dl, c2, y0, c2 + wCol, y1);
  CardHead(c2 + 18.f, y0 + 16.f, "提示", "ALERTS");
  {
    float y = y0 + 48.f;
    const float w = wCol - 36.f;
    y += BindToggle(dl, "v.360",  c2 + 18.f, y, w, b.is3600,        "360 Alert");
    y += BindToggle(dl, "v.fovc", c2 + 18.f, y, w, b.showFovCircle, "FOV Circle");
    y += BindToggle(dl, "v.alin", c2 + 18.f, y, w, b.showAimLine,   "Aim Line");
    y += 8.f;
    y += BindSliderI(dl, "v.tsz", c2 + 18.f, y, w, b.textSize, 10, 50,
                     "Text Size", "");
  }

  // ---- 说明 ----
  CardBg(dl, c3, y0, c3 + wCol, y1);
  CardHead(c3 + 18.f, y0 + 16.f, "说明", "NOTES");
  TxtWrap(c3 + 18.f, y0 + 52.f, wCol - 36.f, 4.6f, p.textDim,
          "这些开关直接写入引擎自身的标志位，与旧菜单完全一致。\n\n"
          "未接线的开关不会显示，所以这里看到的每一项都是真的在起作用。");

  Txt(X, 566.f, 4.6f, p.textFaint, "绑定自引擎标志位 · 未绑定的控件不会绘制", 0);
}

// ================================================================ 页面：瞄准
void PageAim(ImDrawList* dl) {
  const Pal p = P();
  const Bindings& b = gBind;
  const float X = SIDEBAR + 24.f;

  TxtB(X, 36.f, 15.5f, p.text, "瞄准", 0);
  Txt(X + TxtWU(15.5f, "瞄准") + 8.f, 42.f, 4.6f, p.textFaint, "AIM", 0, 1.5f);
  Txt(X, 78.f, 5.4f, p.textDim, "Aim Silent · Hardware BT · Body Kill", 0);

  const float y0 = 100.f, y1 = 520.f;
  const float wCol = 236.f, gap = 14.f;
  const float c1 = X, c2 = X + wCol + gap, c3 = X + (wCol + gap) * 2.f;

  CardBg(dl, c1, y0, c1 + wCol, y1);
  CardHead(c1 + 18.f, y0 + 16.f, "主要", "PRIMARY");
  {
    float y = y0 + 48.f;
    const float w = wCol - 36.f;
    y += BindToggle(dl, "a.silent", c1 + 18.f, y, w, b.aimSilent,  "Aim Silent");
    y += BindToggle(dl, "a.hwbt",   c1 + 18.f, y, w, b.hardwareBT, "Hardware BT");
    y += BindToggle(dl, "a.bot",    c1 + 18.f, y, w, b.aimbot,     "Aimbot");
    y += 8.f;
    y += BindSliderF(dl, "a.fov", c1 + 18.f, y, w, b.aimFov, 10.f, 600.f,
                     "FOV Range", "%.0f");
  }

  CardBg(dl, c2, y0, c2 + wCol, y1);
  CardHead(c2 + 18.f, y0 + 16.f, "Body Kill", "BODY");
  {
    float y = y0 + 48.f;
    const float w = wCol - 36.f;
    y += BindToggle(dl, "a.bk",   c2 + 18.f, y, w, b.bodyKill,     "Body Kill");
    y += BindToggle(dl, "a.bbt",  c2 + 18.f, y, w, b.brutalBT,     "Brutal BT");
    y += BindToggle(dl, "a.drag", c2 + 18.f, y, w, b.dragHeadshot, "Drag Headshot");
    y += 8.f;
    y += BindSliderF(dl, "a.bkhp", c2 + 18.f, y, w, b.bodyKillHP, 1.f, 200.f,
                     "Body Kill HP", "%.0f");
    y += BindSliderI(dl, "a.chest", c2 + 18.f, y, w, b.chestRateSave, 0, 100,
                     "Chest Rate", "%");
  }

  CardBg(dl, c3, y0, c3 + wCol, y1);
  CardHead(c3 + 18.f, y0 + 16.f, "写入方式", "WRITE MODE");
  {
    float y = y0 + 48.f;
    const float w = wCol - 36.f;
    y += BindToggle(dl, "a.nw", c3 + 18.f, y, w, b.normalWrite, "Normal Write");
    TxtWrap(c3 + 18.f, y + 6.f, w, 4.6f, p.textDim,
            "关闭时：Aim Silent 与 Hardware BT 走 Paradise EXECUTE 断点，"
            "由断点改写游戏自己的那条 store 指令。\n\n"
            "打开时：两者先释放各自的断点，再直接写方向字段。\n\n"
            "这是手动选择的模式，不是失败回退 —— 不会自己切换。");
  }

  Txt(X, 566.f, 4.6f, p.textFaint, "绑定自引擎标志位 · 未绑定的控件不会绘制", 0);
}

// ================================================================ 页面：功能
void PageExploit(ImDrawList* dl) {
  const Pal p = P();
  const Bindings& b = gBind;
  const float X = SIDEBAR + 24.f;

  TxtB(X, 36.f, 15.5f, p.text, "功能", 0);
  Txt(X + TxtWU(15.5f, "功能") + 8.f, 42.f, 4.6f, p.textFaint, "EXPLOIT", 0, 1.5f);
  Txt(X, 78.f, 5.4f, p.textDim, "Ghost · No Recoil · No Reload", 0);

  const float y0 = 100.f, y1 = 520.f;
  const float wCol = 236.f, gap = 14.f;
  const float c1 = X, c2 = X + wCol + gap, c3 = X + (wCol + gap) * 2.f;

  CardBg(dl, c1, y0, c1 + wCol, y1);
  CardHead(c1 + 18.f, y0 + 16.f, "战斗", "COMBAT");
  {
    float y = y0 + 48.f;
    const float w = wCol - 36.f;
    y += BindToggle(dl, "x.ghost", c1 + 18.f, y, w, b.ghostHack, "Ghost Hack");
    y += BindToggle(dl, "x.recl",  c1 + 18.f, y, w, b.noRecoil,  "No Recoil");
    y += BindToggle(dl, "x.rld",   c1 + 18.f, y, w, b.noReload,  "No Reload");
  }

  CardBg(dl, c2, y0, c2 + wCol, y1);
  CardHead(c2 + 18.f, y0 + 16.f, "界面", "OVERLAY");
  {
    float y = y0 + 48.f;
    const float w = wCol - 36.f;
    y += BindToggle(dl, "x.float", c2 + 18.f, y, w, b.floatGhost, "Float: Ghost");
    y += BindToggle(dl, "x.mute",  c2 + 18.f, y, w, b.soundMuted, "Sound Muted");
  }

  CardBg(dl, c3, y0, c3 + wCol, y1);
  CardHead(c3 + 18.f, y0 + 16.f, "说明", "NOTES");
  TxtWrap(c3 + 18.f, y0 + 52.f, wCol - 36.f, 4.6f, p.textDim,
          "这一页的开关与旧菜单 Exploit 标签页是同一组标志位，"
          "没有新增任何功能，只是换了控件。");

  Txt(X, 566.f, 4.6f, p.textFaint, "绑定自引擎标志位 · 未绑定的控件不会绘制", 0);
}

// ================================================================ 页面：设置
void PageSettings(ImDrawList* dl) {
  const Pal p = P();
  const float X = SIDEBAR + 24.f;

  TxtB(X, 36.f, 15.5f, p.text, "设置", 0);
  Txt(X + TxtWU(15.5f, "设置") + 8.f, 42.f, 4.6f, p.textFaint, "SETTINGS", 0, 1.5f);
  Txt(X, 78.f, 5.4f, p.textDim, "保存配置、数据通道与关于信息。", 0);

  CardBg(dl, X, 100.f, 978.f, 330.f);
  CardHead(X + 18.f, 116.f, "操作", "ACTIONS");

  Button(dl, H("save"), X + 18.f, 144.f, 152.f, 30.f, "保存全部配置", true);
  Button(dl, H("load"), X + 182.f, 144.f, 152.f, 30.f, "读取配置");
  if (Button(dl, H("link"), X + 18.f, 184.f, 316.f, 30.f, "连接数据通道"))
    g.driverLinked = !g.driverLinked;

  const char* k[3] = {"驱动状态", "世界对象", "菜单帧率"};
  char v0[32];
  const bool linked = gBind.driverLinked ? *gBind.driverLinked : g.driverLinked;
  snprintf(v0, sizeof(v0), "%s", linked ? "已连接" : "待连接");
  char v1[16] = "0";
  char v2[16];
  snprintf(v2, sizeof(v2), "%d FPS", (int)g.fpsTarget);
  const char* vals[3] = {v0, v1, v2};
  for (int i = 0; i < 3; ++i) {
    const float ry = 234.f + 22.f * (float)i;
    Txt(X + 18.f, ry, 5.2f, p.textDim, k[i], 0);
    Txt(978.f - 18.f, ry, 5.2f,
        i == 0 ? (linked ? Col(46, 186, 118) : Col(210, 148, 58)) : p.text, vals[i], 2);
  }
  if (Button(dl, H("exit"), X + 18.f, 292.f, 316.f, 30.f, "退出程序")) g.requestExit = true;

  CardBg(dl, X, 342.f, 978.f, 520.f);
  CardHead(X + 18.f, 358.f, "关于", "ABOUT / NOTICE");
  Txt(X + 18.f, 382.f, 5.6f, p.text, "AETHER UI 2.2 · LS", 0);
  TxtWrap(X + 18.f, 404.f, 720.f, 5.0f, p.textDim,
          "渲染：OpenGL ES 3.0 / ImGui 1.90.1 / SurfaceFlinger 独立图层\n"
          "快捷键：音量+ 打开菜单 / 音量- 隐藏菜单 / 标题栏可拖动窗口\n"
          "外观、页签与动效统一在「主题」与「视觉」页调整；3D 旋转在左侧栏。\n"
          "菜单配置：/data/local/AETHER/config.conf");

  Txt(X, 566.f, 4.6f, p.textFaint, "本界面仅作 UI 复刻演示，不含任何游戏读写逻辑。", 0);
}

// ================================================================ 面板
void DrawPanel(ImDrawList* dl) {
  const Pal p = P();

  // 背后玻璃薄片（视差层）
  FillRR(dl, -18.f, 16.f, PW - 16.f, PH + 18.f, 24.f, Col(160, 196, 240, 0.13f), -30.f);
  StrokeRR(dl, -18.f, 16.f, PW - 16.f, PH + 18.f, 24.f, Col(255, 255, 255, 0.30f), 1.f, -30.f);

  DropShadow(dl, 0.f, 0.f, PW, PH, 29.f, 6.f, g.darkSkin ? 0.34f : 0.17f);

  FillRR(dl, 0.f, 0.f, PW, PH, 29.f, p.panel);
  // 顶部柔光（逐条带渐变）
  for (int i = 0; i < 10; ++i) {
    const float t = (float)i / 10.f;
    const ImU32 c = Col(255, 255, 255, (g.darkSkin ? 0.06f : 0.32f) * (1.f - t));
    const float ya = 1.f + 96.f * t, yb = 1.f + 96.f * (t + 0.1f);
    QuadGrad(dl, Pr(1.f, ya), Pr(PW - 1.f, ya), Pr(PW - 1.f, yb), Pr(1.f, yb), c, c, c, c);
  }
  StrokeRR(dl, 0.f, 0.f, PW, PH, 29.f, p.panelEdge, g.darkSkin ? 1.4f : 1.2f);
  StrokeRR(dl, 1.3f, 1.3f, PW - 1.3f, PH - 1.3f, 28.f, Col(255, 255, 255, 0.20f), 1.f);

  // 顶栏
  Txt(SIDEBAR + 24.f, 14.f, 4.4f, p.textFaint, "AETHER / LS KERNEL MENU", 0, 1.7f);
  char fps[40];
  snprintf(fps, sizeof(fps), "%d FPS / GPU OVERLAY", (int)ImGui::GetIO().Framerate);
  Txt(944.f, 14.f, 4.2f, p.textFaint, fps, 2, 0.9f);

  if (Click(H("close"), 950.f, 6.f, 974.f, 30.f)) g.open = false;
  const bool xh = Hover(950.f, 6.f, 974.f, 30.f);
  dl->AddLine(Pr(958.f, 13.f), Pr(966.f, 21.f), xh ? p.text : p.textDim, 1.3f);
  dl->AddLine(Pr(966.f, 13.f), Pr(958.f, 21.f), xh ? p.text : p.textDim, 1.3f);

  DrawSidebar(dl);

  switch (g.page) {
    case PAGE_VISUAL:  PageVisual(dl);  break;
    case PAGE_AIM:     PageAim(dl);     break;
    case PAGE_EXPLOIT: PageExploit(dl); break;
    case PAGE_THEME:   PageTheme(dl);   break;
    default:           PageSettings(dl); break;
  }
}

void DrawPill() {
  const Pal p = P();
  const float cx = gCx + (PW * 0.5f - 26.f) * gS, cy = gCy - (PH * 0.5f - 26.f) * gS;
  ImDrawList* dl = DL();
  dl->AddCircleFilled(ImVec2(cx, cy), 20.f * gS, p.panel, 28);
  dl->AddCircle(ImVec2(cx, cy), 20.f * gS, p.panelEdge, 28, 1.4f);
  dl->AddText(Fnt(), 17.f * gS, ImVec2(cx - 5.5f * gS, cy - 10.f * gS), p.text, "A");
}

void BuildR(float yaw, float pitch) {
  const float cy = cosf(yaw), sy = sinf(yaw);
  const float cp = cosf(pitch), sp = sinf(pitch);
  R[0][0] = cy;
  R[0][1] = 0.f;
  R[0][2] = sy;
  R[1][0] = sp * sy;
  R[1][1] = cp;
  R[1][2] = -sp * cy;
  R[2][0] = -cp * sy;
  R[2][1] = sp;
  R[2][2] = cp * cy;
  gN[0] = R[0][2];
  gN[1] = R[1][2];
  gN[2] = R[2][2];
}

bool Unproject(const ImVec2& s, float* ox, float* oy) {
  if (gS <= 0.f) return false;
  const float u = (s.x - gCx) / gS;
  const float v = (s.y - gCy) / gS;
  if (fabsf(gN[2]) < 1e-4f) return false;
  const float f = 1.f + (u * gN[0] + v * gN[1]) / (gDist * gN[2]);
  if (f < 0.08f) return false;
  const float x2 = u / f, y2 = v / f;
  const float z2 = gDist * (1.f - f) / f;
  const float px = R[0][0] * x2 + R[1][0] * y2 + R[2][0] * z2;
  const float py = R[0][1] * x2 + R[1][1] * y2 + R[2][1] * z2;
  *ox = px + PW * 0.5f;
  *oy = py + PH * 0.5f;
  return true;
}

float gOpenT = 1.f;
float gBaseYaw = -0.300f, gBasePitch = -0.050f;
bool gDragging = false;
float gDragX0 = 0.f, gDragY0 = 0.f, gYaw0 = 0.f, gPitch0 = 0.f;

}  // namespace

Bindings& GetBindings() { return gBind; }

State& GetState() { return g; }

void Draw(float screenW, float screenH, float uiScale) {
  (void)uiScale;
  ImDrawList* dl = DL();
  const ImGuiIO& io = ImGui::GetIO();

  const float fitW = screenW * 0.94f / PW;
  const float fitH = screenH * 0.74f / PH;
  float base = fitW < fitH ? fitW : fitH;
  base *= 0.94f + 0.06f * g.menuScale;
  base *= (0.88f + 0.12f * gOpenT);
  gS = base;
  gCx = screenW * 0.5f;
  gCy = screenH * 0.47f;

  if (gOpenT < 1.f) gOpenT += io.DeltaTime * 2.0f;
  if (gOpenT > 1.f) gOpenT = 1.f;
  gAlpha = gOpenT < 1.f ? (0.25f + 0.75f * gOpenT) : 1.f;

  if (!g.use3D) {
    g.yaw += (0.f - g.yaw) * 0.15f;
    g.pitch += (0.f - g.pitch) * 0.15f;
  } else {
    g.yaw += (gBaseYaw - g.yaw) * 0.12f;
    g.pitch += (gBasePitch - g.pitch) * 0.12f;
  }
  BuildR(g.yaw, g.pitch);

  gMouseOK = Unproject(io.MousePos, &gMouse.x, &gMouse.y);

  if (!g.open) {
    gMouseOK = false;
    DrawPill();
    const float cx = gCx + (PW * 0.5f - 26.f) * gS, cy = gCy - (PH * 0.5f - 26.f) * gS;
    if (io.MouseClicked[0] && fabsf(io.MousePos.x - cx) < 22.f * gS &&
        fabsf(io.MousePos.y - cy) < 22.f * gS) {
      g.open = true;
      gOpenT = 0.f;
    }
    return;
  }

  // 3D 旋转手柄
  if (g.use3D && gMouseOK && ImGui::IsMouseClicked(0) && gMouse.x >= 14.f && gMouse.x <= 194.f &&
      gMouse.y >= 458.f && gMouse.y <= 528.f) {
    gDragging = true;
    gDragX0 = io.MousePos.x;
    gDragY0 = io.MousePos.y;
    gYaw0 = g.yaw;
    gPitch0 = g.pitch;
  }
  // 双击手柄复位
  if (gMouseOK && ImGui::IsMouseDoubleClicked(0) && gMouse.x >= 14.f && gMouse.x <= 194.f &&
      gMouse.y >= 462.f && gMouse.y <= 512.f) {
    gYaw0 = g.yaw = gBaseYaw;
    gPitch0 = g.pitch = gBasePitch;
    gDragging = false;
  }
  if (gDragging) {
    if (!ImGui::IsMouseDown(0)) {
      gDragging = false;
    } else {
      g.yaw = gYaw0 + (io.MousePos.x - gDragX0) * 0.0024f;
      g.pitch = gPitch0 + (io.MousePos.y - gDragY0) * 0.0017f;
      if (g.yaw < -0.44f) g.yaw = -0.44f;
      if (g.yaw > 0.44f) g.yaw = 0.44f;
      if (g.pitch < -0.22f) g.pitch = -0.22f;
      if (g.pitch > 0.22f) g.pitch = 0.22f;
    }
  }

  DrawPanel(dl);
}

bool PointBlocksTouch(float x, float y) {
  if (!g.open) return false;
  float ox = 0.f, oy = 0.f;
  if (!Unproject(ImVec2(x, y), &ox, &oy)) return false;
  return ox >= -6.f && ox <= PW + 6.f && oy >= -6.f && oy <= PH + 6.f;
}

}  // namespace AetherUI
