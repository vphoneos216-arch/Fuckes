#include "AetherUI.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace AetherUI {
namespace {

constexpr float PW = 1000.f;  // panel logical width
constexpr float PH = 620.f;   // panel logical height
constexpr float SIDEBAR = 208.f;
constexpr float PI = 3.14159265358979f;
constexpr float TSZ = 1.92f;  // text scale: logical units -> pixels

State g;
Bindings gBind;

// ---------------------------------------------------------------- Projection
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

// Accent RGB per theme card. Declared ahead of P() because P() reads it.
// The original had a separate accent index that was written by the colour
// dots and never read by anything, so the dots are gone and the three
// theme cards carry the accent instead.
void AccentRGB(int idx, int* r, int* g2, int* b) {
  // 0 = SUNLIT, 1 = MIST, 2 = SAKURA
  static const int lite[3][3] = {{198, 134, 40}, {45, 122, 246}, {220, 92, 146}};
  static const int dark[3][3] = {{238, 198, 98}, {58, 224, 194}, {255, 148, 194}};
  const int i = (idx % 3 + 3) % 3;
  if (g.darkSkin) { *r = dark[i][0]; *g2 = dark[i][1]; *b = dark[i][2]; }
  else            { *r = lite[i][0]; *g2 = lite[i][1]; *b = lite[i][2]; }
}

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
  int ar = 0, ag2 = 0, ab = 0;
  AccentRGB(g.themeIdx, &ar, &ag2, &ab);
  p.accent = Col(ar, ag2, ab, 1.f);
  p.accentWash = Col(ar, ag2, ab, g.darkSkin ? 0.13f : 0.14f);
  return p;
}

// ---------------------------------------------------------------- Primitives
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

// Left/right edge of a rounded rect at height y
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

// ---------------------------------------------------------------- Text (projected per glyph)
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

// ---------------------------------------------------------------- Input
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

// ================================================================ Widgets
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

// ---------------------------------------------------------------- Bound rows
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

// ================================================================ Icons
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

// ================================================================ Sidebar
void DrawSidebar(ImDrawList* dl) {
  const Pal p = P();

  FillRR(dl, 20.f, 20.f, 64.f, 64.f, 12.f, Col(255, 255, 255, 0.55f));
  QuadGrad(dl, Pr(21.f, 21.f), Pr(63.f, 21.f), Pr(63.f, 63.f), Pr(21.f, 63.f),
           Col(255, 178, 152, 0.95f), Col(252, 150, 172, 0.95f), Col(122, 152, 236, 0.95f),
           Col(96, 136, 226, 0.95f));
  Txt(42.f, 34.f, 10.5f, Col(255, 255, 255, 0.95f), "T", 1);

  TxtB(74.f, 20.f, 15.f, p.text, "TESLA", 0, 0.4f);
  Txt(74.f, 48.f, 4.6f, p.textDim, "KERNEL EDITION", 0, 1.5f);

  Txt(22.f, 88.f, 4.8f, p.textFaint, "NAVIGATION", 0, 1.8f);

  const char* nav[PAGE_COUNT] = {"Visual", "Aim", "Exploit", "Settings", "Theme"};
  const char* sub[PAGE_COUNT] = {"ESP DRAWING", "SILENT / HW BT", "GHOST / RECOIL",
                                 "SAVE / EXIT", "SKIN / ACCENT"};
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
    Txt(46.f, y + 3.f, 7.2f, act ? p.text : p.textDim, nav[i], 0);
    Txt(46.f, y + 21.f, 4.3f, act ? p.accent : p.textFaint, sub[i], 0, 1.3f);
    if (Click(H(sub[i]), 14.f, y, 194.f, y + 38.f)) g.page = i;
  }

  // Footer status. The "3D rotate" drag pad that used to sit here is gone
  // along with the tilt it controlled.
  dl->AddCircleFilled(Pr(22.f, 548.f), 2.0f * Ds(22.f, 548.f), Col(58, 208, 128, 0.95f), 12);
  Txt(28.f, 542.f, 4.8f, Col(56, 190, 120, 0.95f), "MENU READY", 0, 1.2f);
  Txt(20.f, 560.f, 4.5f, p.textFaint, "SurfaceFlinger overlay", 0);
  Txt(20.f, 578.f, 4.2f, p.textFaint, "TESLA UI / 1.0", 0, 0.7f);

  dl->AddLine(Pr(SIDEBAR, 0.f), Pr(SIDEBAR, PH), p.hair, 1.f);
}

// ================================================================ Cards
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

// ================================================================ Cover art
// Sampled cell by cell so it follows the rounded corners. An abstract
// sky / horizon / sun / bokeh composition rather than an imported image.
void CoverArt(ImDrawList* dl, float x0, float y0, float x1, float y1, float r, int idx) {
  const int ROWS = 40, COLS = 24;
  ImU32 sky0, sky1, field0, field1, sun, lite;
  float sunX = 0.30f, sunY = 0.36f, sunR = 0.34f;
  if (idx == 0) {  // sunlit
    sky0 = Col(255, 244, 206);
    sky1 = Col(196, 214, 160);
    field0 = Col(126, 156, 118);
    field1 = Col(74, 100, 82);
    sun = Col(255, 252, 226);
    lite = Col(255, 236, 176);
  } else if (idx == 1) {  // mist
    sky0 = Col(238, 243, 250);
    sky1 = Col(176, 190, 206);
    field0 = Col(138, 150, 166);
    field1 = Col(96, 108, 124);
    sun = Col(255, 255, 255);
    lite = Col(226, 236, 246);
    sunX = 0.62f;
    sunR = 0.40f;
  } else {  // sakura
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
  // Fixed bokeh positions, different per theme, to fake depth of field
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
        // sky -> ground, soft blend at the horizon
        const float tSk = v / kHorizon;
        ImU32 skyc = mix(sky0, sky1, tSk * tSk * 0.85f + u * 0.10f);
        ImU32 gnd = mix(field0, field1, (v - kHorizon) / (1.f - kHorizon));
        float bt = (v - kHorizon) / 0.10f + 0.5f;
        ImU32 c = mix(skyc, gnd, bt);
        // sun
        const float dx = (u - sunX) * 1.0f, dy = (v - sunY) * 1.25f;
        const float dd = sqrtf(dx * dx + dy * dy);
        if (dd < sunR) {
          const float g = 1.f - dd / sunR;
          c = mix(c, sun, powf(g, 2.2f) * 0.85f);
        }
        // bokeh
        for (int b = 0; b < 5; ++b) {
          const float bd = sqrtf((u - bokeh[b][0]) * (u - bokeh[b][0]) * 2.0f +
                                 (v - bokeh[b][1]) * (v - bokeh[b][1]));
          if (bd < bokeh[b][2] * 2.6f) {
            const float g = 1.f - bd / (bokeh[b][2] * 2.6f);
            c = mix(c, lite, powf(g, 2.0f) * bokeh[b][3] * 0.55f);
          }
        }
        // slight darkening toward the bottom
        if (v > 0.82f) c = mix(c, IM_COL32(40, 48, 62, 255), (v - 0.82f) * 1.1f);
        return (c & 0x00FFFFFF) | amask;
      };
      const ImU32 c00 = sample(xa, ya), c10 = sample(xb, ya), c11 = sample(xb, yb),
                  c01 = sample(xa, yb);
      QuadGrad(dl, Pr(xa, ya), Pr(xb, ya), Pr(xb, yb), Pr(xa, yb), c00, c10, c11, c01);
    }
  }
  // Darken the bottom so the white title stays readable
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

// ================================================================ Page: Theme
// Big name + the accent each card actually applies. Picking a card now
// changes the menu's accent colour everywhere (see AccentRGB above); in
// the original these cards only highlighted themselves.
const char* kThemeName[3] = {"Sunlit", "Mist", "Sakura"};
const char* kThemeTag[3] = {"AMBER", "BLUE", "PINK"};

void PageTheme(ImDrawList* dl) {
  const Pal p = P();
  const float X = SIDEBAR + 24.f;

  TxtB(X, 36.f, 15.5f, p.text, "Theme", 0);
  Txt(X + TxtWU(15.5f, "Theme") + 10.f, 42.f, 4.6f, p.textFaint, "LOOK", 0, 1.5f);
  Txt(X, 78.f, 5.4f, p.textDim, "Skin, accent and panel size.", 0);

  const float cy0 = 100.f, ch = 116.f, cw = 232.f, gap = 13.f;
  for (int i = 0; i < 3; ++i) {
    const float cx0 = X + (cw + gap) * (float)i;
    const float cx1 = cx0 + cw, cy1 = cy0 + ch;
    const bool sel = (g.themeIdx == i);
    DropShadow(dl, cx0, cy0, cx1, cy1, 12.f, 3.5f, sel ? 0.18f : 0.10f);
    CoverArt(dl, cx0, cy0, cx1, cy1, 12.f, i);
    StrokeRR(dl, cx0, cy0, cx1, cy1, 12.f, sel ? p.accent : Col(255, 255, 255, 0.45f),
             sel ? 1.8f : 1.f);
    Txt(cx0 + 12.f, cy1 - 40.f, 8.2f, Col(255, 255, 255, 0.98f), kThemeName[i], 0);
    Txt(cx0 + 12.f, cy1 - 22.f, 4.2f, Col(255, 255, 255, 0.78f), kThemeTag[i], 0, 1.3f);
    if (sel) {
      FillRR(dl, cx1 - 62.f, cy1 - 26.f, cx1 - 10.f, cy1 - 11.f, 6.f, Col(255, 255, 255, 0.94f));
      Txt(cx1 - 36.f, cy1 - 24.5f, 4.8f, Col(38, 58, 88, 0.95f), "IN USE", 1);
    }
    if (Click(H(kThemeTag[i]), cx0, cy0, cx1, cy1)) g.themeIdx = i;
  }

  // Only the two controls that actually do something are left. The
  // original's rain toggle, font-size slider, menu-alpha slider, frame
  // rate slider and accent dots were all written and never read by
  // anything, so they were drawn controls that changed nothing.
  const float ly0 = 232.f, ly1 = 420.f;
  CardBg(dl, X, ly0, 978.f, ly1);
  CardHead(X + 18.f, ly0 + 16.f, "Appearance", "SKIN / SIZE");

  const char* skins[2] = {"Light Glass", "Dark Glass"};
  int skinIdx = g.darkSkin ? 1 : 0;
  Segmented(dl, H("skin"), X + 18.f, ly0 + 46.f, 320.f, 26.f, &skinIdx, skins, 2);
  g.darkSkin = (skinIdx == 1);

  Txt(X + 18.f, ly0 + 92.f, 5.4f, p.textDim, "Menu Size", 0);
  char mb[16];
  snprintf(mb, sizeof(mb), "%d%%", (int)(g.menuScale * 100.f + 0.5f));
  Txt(X + 338.f, ly0 + 92.f, 5.0f, p.text, mb, 2);
  float ms = (g.menuScale - 0.7f) / 0.6f;
  if (ms < 0.f) ms = 0.f;
  if (ms > 1.f) ms = 1.f;
  Slider(dl, H("msize"), X + 18.f, ly0 + 108.f, 320.f, &ms, 0.f, 1.f);
  g.menuScale = 0.7f + ms * 0.6f;

  TxtWrap(X + 380.f, ly0 + 46.f, 560.f, 4.7f, p.textDim,
          "The panel is drawn front-on. The tilted perspective view and the "
          "drag pad that span it were removed.");
}

// ================================================================ Page: Visual
void PageVisual(ImDrawList* dl) {
  const Pal p = P();
  const Bindings& b = gBind;
  const float X = SIDEBAR + 24.f;

  TxtB(X, 36.f, 15.5f, p.text, "Visual", 0);
  Txt(X + TxtWU(15.5f, "Visual") + 10.f, 42.f, 4.6f, p.textFaint, "ESP", 0, 1.5f);
  Txt(X, 78.f, 5.4f, p.textDim, "Box, skeleton, name, health, distance.", 0);

  const float y0 = 100.f, y1 = 520.f;
  const float wCol = 360.f, gap = 26.f;
  const float c1 = X, c2 = X + wCol + gap;

  CardBg(dl, c1, y0, c1 + wCol, y1);
  CardHead(c1 + 18.f, y0 + 16.f, "Draw", "ELEMENTS");
  {
    float y = y0 + 54.f;
    const float w = wCol - 36.f;
    y += BindRow(dl, "v.box",  c1 + 18.f, y, w, b.showBox,      "Box ESP");
    y += BindRow(dl, "v.skel", c1 + 18.f, y, w, b.showSkeleton, "Skeleton");
    y += BindRow(dl, "v.name", c1 + 18.f, y, w, b.showName,     "Name");
    y += BindRow(dl, "v.hp",   c1 + 18.f, y, w, b.showHealth,   "Health");
    y += BindRow(dl, "v.dist", c1 + 18.f, y, w, b.showDistance, "Distance");
    y += BindRow(dl, "v.line", c1 + 18.f, y, w, b.showLine,     "ESP Line");
  }

  CardBg(dl, c2, y0, c2 + wCol, y1);
  CardHead(c2 + 18.f, y0 + 16.f, "Overlay", "HUD");
  {
    float y = y0 + 54.f;
    const float w = wCol - 36.f;
    y += BindToggle(dl, "v.360",  c2 + 18.f, y, w, b.is3600,        "360 Alert");
    y += BindToggle(dl, "v.fovc", c2 + 18.f, y, w, b.showFovCircle, "FOV Circle");
    y += BindToggle(dl, "v.alin", c2 + 18.f, y, w, b.showAimLine,   "Aim Line");
    y += 10.f;
    y += BindSliderI(dl, "v.tsz", c2 + 18.f, y, w, b.textSize, 10, 50, "Text Size", "");
  }

  Txt(X, 566.f, 4.6f, p.textFaint,
      "Bound to the engine's own flags. An unbound control is not drawn.", 0);
}

// ================================================================ Page: Aim
// Aim Silent and Hardware BT only, as asked. Aimbot, FOV range, Body Kill,
// Brutal BT, Drag Headshot, Body Kill HP and Chest Rate are no longer drawn
// here. Their flags are still bound (AetherUI.h), so they keep whatever
// value the saved config gives them -- they are off this page, not reset.
void PageAim(ImDrawList* dl) {
  const Pal p = P();
  const Bindings& b = gBind;
  const float X = SIDEBAR + 24.f;

  TxtB(X, 36.f, 15.5f, p.text, "Aim", 0);
  Txt(X + TxtWU(15.5f, "Aim") + 10.f, 42.f, 4.6f, p.textFaint, "SILENT / HW BT", 0, 1.5f);
  Txt(X, 78.f, 5.4f, p.textDim, "Paradise EXECUTE breakpoint path.", 0);

  const float y0 = 100.f, y1 = 248.f;
  const float wCol = 360.f;

  CardBg(dl, X, y0, X + wCol, y1);
  CardHead(X + 18.f, y0 + 16.f, "Primary", "AIM");
  {
    float y = y0 + 58.f;
    const float w = wCol - 36.f;
    y += BindToggle(dl, "a.silent", X + 18.f, y, w, b.aimSilent,  "Aim Silent");
    y += BindToggle(dl, "a.hwbt",   X + 18.f, y, w, b.hardwareBT, "Hardware BT");
  }

  Txt(X, 566.f, 4.6f, p.textFaint,
      "Bound to the engine's own flags. An unbound control is not drawn.", 0);
}

// ================================================================ Page: Exploit
void PageExploit(ImDrawList* dl) {
  const Pal p = P();
  const Bindings& b = gBind;
  const float X = SIDEBAR + 24.f;

  TxtB(X, 36.f, 15.5f, p.text, "Exploit", 0);
  Txt(X + TxtWU(15.5f, "Exploit") + 10.f, 42.f, 4.6f, p.textFaint, "COMBAT", 0, 1.5f);
  Txt(X, 78.f, 5.4f, p.textDim, "Ghost, recoil, reload and write mode.", 0);

  const float y0 = 100.f, y1 = 520.f;
  const float wCol = 236.f, gap = 14.f;
  const float c1 = X, c2 = X + wCol + gap, c3 = X + (wCol + gap) * 2.f;

  CardBg(dl, c1, y0, c1 + wCol, y1);
  CardHead(c1 + 18.f, y0 + 16.f, "Combat", "FIGHT");
  {
    float y = y0 + 54.f;
    const float w = wCol - 36.f;
    y += BindToggle(dl, "x.ghost", c1 + 18.f, y, w, b.ghostHack, "Ghost Hack");
    y += BindToggle(dl, "x.recl",  c1 + 18.f, y, w, b.noRecoil,  "No Recoil");
    y += BindToggle(dl, "x.rld",   c1 + 18.f, y, w, b.noReload,  "No Reload");
  }

  CardBg(dl, c2, y0, c2 + wCol, y1);
  CardHead(c2 + 18.f, y0 + 16.f, "Overlay", "FLOAT");
  {
    float y = y0 + 54.f;
    const float w = wCol - 36.f;
    y += BindToggle(dl, "x.float", c2 + 18.f, y, w, b.floatGhost, "Float: Ghost");
    y += BindToggle(dl, "x.mute",  c2 + 18.f, y, w, b.soundMuted, "Sound Muted");
  }

  // Moved here from the Aim page, which is where it was asked for.
  CardBg(dl, c3, y0, c3 + wCol, y1);
  CardHead(c3 + 18.f, y0 + 16.f, "Write Mode", "SILENT / BT");
  {
    float y = y0 + 54.f;
    const float w = wCol - 36.f;
    y += BindToggle(dl, "x.nw", c3 + 18.f, y, w, b.normalWrite, "Normal Write");
    TxtWrap(c3 + 18.f, y + 8.f, w, 4.6f, p.textDim,
            "Off: Aim Silent and Hardware BT go through the Paradise EXECUTE "
            "breakpoint, which rewrites the game's own store instruction.\n\n"
            "On: both release their breakpoints first, then write the direction "
            "field directly.\n\n"
            "This is a mode you pick, not a fallback. It never switches itself.");
  }

  Txt(X, 566.f, 4.6f, p.textFaint,
      "Bound to the engine's own flags. An unbound control is not drawn.", 0);
}

// ================================================================ Page: Settings
// Trimmed to what is real. Gone: "load config" and "connect data channel"
// (neither was wired to anything), the driver-status row (no driver flag is
// bound), the world-object row (it printed a hardcoded 0) and the about text
// that described a config file this build never writes.
void PageSettings(ImDrawList* dl) {
  const Pal p = P();
  const float X = SIDEBAR + 24.f;

  TxtB(X, 36.f, 15.5f, p.text, "Settings", 0);
  Txt(X + TxtWU(15.5f, "Settings") + 10.f, 42.f, 4.6f, p.textFaint, "SAVE / EXIT", 0, 1.5f);
  Txt(X, 78.f, 5.4f, p.textDim, "Write the current settings, or close the menu.", 0);

  CardBg(dl, X, 100.f, 978.f, 268.f);
  CardHead(X + 18.f, 116.f, "Actions", "SAVE / EXIT");

  if (Button(dl, H("save"), X + 18.f, 150.f, 200.f, 32.f, "Save Settings", true))
    g.requestSave = true;
  if (Button(dl, H("exitbtn"), X + 230.f, 150.f, 200.f, 32.f, "Exit"))
    g.requestExit = true;

  TxtWrap(X + 18.f, 198.f, 700.f, 4.7f, p.textDim,
          "Save writes the same settings file the old menu wrote. Exit saves "
          "first, then stops the overlay and the process.");

  CardBg(dl, X, 280.f, 978.f, 400.f);
  CardHead(X + 18.f, 296.f, "About", "BUILD");
  Txt(X + 18.f, 326.f, 5.6f, p.text, "TESLA UI 1.0", 0);
  Txt(X + 18.f, 350.f, 4.8f, p.textDim, "ImGui overlay on its own SurfaceFlinger layer", 0);

  char fb[32];
  snprintf(fb, sizeof(fb), "%d FPS", (int)ImGui::GetIO().Framerate);
  Txt(978.f - 18.f, 326.f, 5.2f, p.text, fb, 2);
}

// ================================================================ Panel
void DrawPanel(ImDrawList* dl) {
  const Pal p = P();

  // Glass sheet behind the panel
  FillRR(dl, -18.f, 16.f, PW - 16.f, PH + 18.f, 24.f, Col(160, 196, 240, 0.13f), -30.f);
  StrokeRR(dl, -18.f, 16.f, PW - 16.f, PH + 18.f, 24.f, Col(255, 255, 255, 0.30f), 1.f, -30.f);

  DropShadow(dl, 0.f, 0.f, PW, PH, 29.f, 6.f, g.darkSkin ? 0.34f : 0.17f);

  FillRR(dl, 0.f, 0.f, PW, PH, 29.f, p.panel);
  // Top sheen, banded gradient
  for (int i = 0; i < 10; ++i) {
    const float t = (float)i / 10.f;
    const ImU32 c = Col(255, 255, 255, (g.darkSkin ? 0.06f : 0.32f) * (1.f - t));
    const float ya = 1.f + 96.f * t, yb = 1.f + 96.f * (t + 0.1f);
    QuadGrad(dl, Pr(1.f, ya), Pr(PW - 1.f, ya), Pr(PW - 1.f, yb), Pr(1.f, yb), c, c, c, c);
  }
  StrokeRR(dl, 0.f, 0.f, PW, PH, 29.f, p.panelEdge, g.darkSkin ? 1.4f : 1.2f);
  StrokeRR(dl, 1.3f, 1.3f, PW - 1.3f, PH - 1.3f, 28.f, Col(255, 255, 255, 0.20f), 1.f);

  // Top bar
  Txt(SIDEBAR + 24.f, 14.f, 4.4f, p.textFaint, "TESLA / KERNEL MENU", 0, 1.7f);
  char fps[40];
  snprintf(fps, sizeof(fps), "%d FPS / GPU OVERLAY", (int)ImGui::GetIO().Framerate);
  Txt(858.f, 14.f, 4.2f, p.textFaint, fps, 2, 0.9f);

  // [NEW] Hard exit, reachable from every page instead of only the
  // settings page. The menu cannot kill the process itself -- saving,
  // stopping the threads and closing touch all live in the engine -- so
  // it only raises the request and the owner runs that path.
  {
    const float ex0 = 866.f, ey0 = 7.f, ex1 = 940.f, ey1 = 29.f;
    const bool eh = Hover(ex0, ey0, ex1, ey1);
    FillRR(dl, ex0, ey0, ex1, ey1, 7.f,
           eh ? Col(228, 76, 76, 0.94f) : Col(228, 76, 76, 0.60f));
    Txt((ex0 + ex1) * 0.5f, 13.6f, 5.6f, Col(255, 255, 255, 0.97f), "EXIT", 1, 0.8f);
    if (Click(H("hardexit"), ex0, ey0, ex1, ey1)) g.requestExit = true;
  }

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
  dl->AddText(Fnt(), 17.f * gS, ImVec2(cx - 5.0f * gS, cy - 10.f * gS), p.text, "T");
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
// [CHANGED] Was -0.300 / -0.050, i.e. the panel sat rotated ~17 degrees in
// yaw with a slight downward pitch. On a phone that just looks crooked, so
// the panel is now drawn front-on: with both angles zero BuildR() produces
// the identity matrix and Pr() reduces to a straight 2D map. The drag
// handle that let you spin it is gone with it.
float gYaw = 0.f, gPitch = 0.f;

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

  BuildR(gYaw, gPitch);

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

  DrawPanel(dl);
}

bool PointBlocksTouch(float x, float y) {
  if (!g.open) return false;
  float ox = 0.f, oy = 0.f;
  if (!Unproject(ImVec2(x, y), &ox, &oy)) return false;
  return ox >= -6.f && ox <= PW + 6.f && oy >= -6.f && oy <= PH + 6.f;
}

}  // namespace AetherUI
