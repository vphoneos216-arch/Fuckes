#define IMGUI_DEFINE_MATH_OPERATORS
#include "widgets.hpp"
#include "Android_draw/draw.h"
#include <functional>
#include <string>
#include <vector>

namespace ui {

namespace widgets {
    using namespace style;

    // ============================================
    // Separator
    // ============================================
    void separator(float a) {
        if (a < 0.01f) return;

        ImGuiWindow* w = ImGui::GetCurrentWindow();
        ImVec2 pos = w->DC.CursorPos;
        float ww = content_w > 0 ? content_w : ImGui::GetContentRegionAvail().x;

        ImDrawList* dl = w->DrawList;
        float y = pos.y + 4.f * S;

        dl->AddRectFilledMultiColor(
            ImVec2(pos.x, y), ImVec2(pos.x + ww * 0.4f, y + 1),
            col(UI::border, a * 0.6f), col(UI::border, 0.f),
            col(UI::border, 0.f), col(UI::border, a * 0.6f)
        );
        dl->AddRectFilledMultiColor(
            ImVec2(pos.x + ww * 0.6f, y), ImVec2(pos.x + ww, y + 1),
            col(UI::border, 0.f), col(UI::border, a * 0.6f),
            col(UI::border, a * 0.6f), col(UI::border, 0.f)
        );

        ImGui::Dummy(ImVec2(0, 8.f * S));
    }

    // ============================================
    // Button
    // ============================================
    bool button(const char* name, float a, std::function<void()> callback) {
        if (a < 0.01f) return false;

        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems) return false;

        std::string ids = std::string("btn_") + name;
        ImGuiID id = w->GetID(ids.c_str());

        float ww = content_w > 0 ? content_w : ImGui::GetContentRegionAvail().x;
        float h = 30.f * S;

        ImVec2 pos = w->DC.CursorPos;
        ImRect r(pos, ImVec2(pos.x + ww, pos.y + h));

        ImGui::ItemSize(r);
        if (!ImGui::ItemAdd(r, id)) return false;

        bool hov = ImGui::IsMouseHoveringRect(r.Min, r.Max) && !popup();
        bool press = hov && ImGui::IsMouseClicked(0);

        if (press) {
            if (callback) callback();
        }

        ImDrawList* dl = w->DrawList;
        ImVec4 bg = hov ? UI::widget : UI::panel;
        dl->AddRectFilled(r.Min, r.Max, col(bg, a));
        dl->AddRect(r.Min, r.Max, col(UI::border, a));

        ImU32 btnText = UI::g_dark_mode ? IM_COL32(240, 244, 250, 255) : IM_COL32(30, 35, 45, 255);
        ImGui::PushFont(fontMedium);
        ImVec2 ts = ImGui::CalcTextSize(name);
        dl->AddText(ImVec2(r.GetCenter().x - ts.x * 0.5f, r.GetCenter().y - ts.y * 0.5f),
                    btnText, name);
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, h + 4.f * S));

        return press;
    }

    // ============================================
    // ✅ Toggle — শুধু Switch-এ Tap করলে Toggle হবে
    // ============================================
    bool toggle(const char* label, bool* v, float a) {
        if (a < 0.01f || !v) return false;

        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems) return false;

        std::string ids = std::string("tog_") + label;
        ImGuiID id = w->GetID(ids.c_str());

        float ww = content_w > 0 ? content_w : ImGui::GetContentRegionAvail().x;
        float h  = 28.f * S;

        ImVec2 pos = w->DC.CursorPos;
        ImRect r(pos, ImVec2(pos.x + ww, pos.y + h));

        ImGui::ItemSize(r);
        if (!ImGui::ItemAdd(r, id)) return false;

        // ✅ Switch geometry আগে calculate (hit-test এর জন্য)
        float sw = 42.f * S;                                          // switch width
        float sh = 20.f * S;                                          // switch height
        float sx = pos.x + ww - sw - 8.f * S;                         // switch x
        float sy = pos.y + (h - sh) * 0.5f;                           // switch y

        // ✅ শুধু switch rectangle-এ hover/click check
        ImVec2 mouse = ImGui::GetIO().MousePos;
        bool hov_switch = !popup() &&
                          mouse.x >= sx && mouse.x <= sx + sw &&
                          mouse.y >= sy && mouse.y <= sy + sh;

        bool changed = false;
        // ✅ শুধু switch এ click করলেই toggle (label এ না)
        if (hov_switch && ImGui::IsMouseClicked(0)) {
            *v = !(*v);
            changed = true;
        }

        ImDrawList* dl = w->DrawList;

        float t = anim(ids + "_anim", *v ? 1.f : 0.f, 15.f);

        ImGui::PushFont(fontMedium);

        // Label color
        ImU32 label_col;
        if (UI::g_dark_mode) {
            if (*v) {
                label_col = IM_COL32(255, 255, 255, 255);
            } else {
                label_col = hov_switch ? IM_COL32(220, 230, 245, 255)
                                       : IM_COL32(130, 142, 160, 255);
            }
        } else {
            if (*v) {
                label_col = hov_switch ? IM_COL32(0, 165, 225, 255)
                                       : IM_COL32(0, 135, 195, 255);
            } else {
                label_col = hov_switch ? IM_COL32(30, 35, 45, 255)
                                       : IM_COL32(80, 85, 95, 255);
            }
        }

        dl->AddText(ImVec2(pos.x + 8.f * S, pos.y + (h - fontMedium->FontSize) * 0.5f),
                    label_col, label);
        ImGui::PopFont();

        // Track colors
        ImU32 off_track = UI::g_dark_mode ? IM_COL32(28, 38, 52, 255) : IM_COL32(185, 195, 205, 255);
        ImU32 on_track  = IM_COL32(0, 195, 255, 255);
        ImU32 track_col = ImLerp(off_track, on_track, t);

        ImU32 off_border = UI::g_dark_mode ? IM_COL32(0, 160, 220, 80) : IM_COL32(150, 165, 180, 200);
        ImU32 on_border  = IM_COL32(60, 220, 255, 255);
        ImU32 border_col = ImLerp(off_border, on_border, t);

        // Glow (on হলে)
        if (t > 0.05f) {
            int glow_alpha = (int)(120 * t);
            for (int i = 1; i <= 4; i++) {
                dl->AddRectFilled(
                    ImVec2(sx - i * 2.0f, sy - i * 2.0f),
                    ImVec2(sx + sw + i * 2.0f, sy + sh + i * 2.0f),
                    IM_COL32(0, 195, 255, glow_alpha / i),
                    (sh * 0.5f) + i * 2.0f);
            }
        }

        // Track
        dl->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + sw, sy + sh), track_col, sh * 0.5f);
        dl->AddRect(ImVec2(sx, sy), ImVec2(sx + sw, sy + sh), border_col, sh * 0.5f, 0, 1.5f);

        // Knob
        float kr = (sh - 4.f * S) * 0.5f;
        float kx = sx + 2.f * S + kr + (sw - sh) * t;
        float ky = sy + sh * 0.5f;

        dl->AddCircleFilled(ImVec2(kx + 1.f, ky + 1.f), kr, IM_COL32(0, 0, 0, 90));
        dl->AddCircleFilled(ImVec2(kx, ky), kr, IM_COL32(255, 255, 255, 255));

        ImGui::Dummy(ImVec2(0, 2.f * S));
        return changed;
    }

    // ============================================
    // Card
    // ============================================
    static std::vector<ImVec2> s_card_stack;

    void begin_card(const char* title, float a) {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        ImVec2 pos = w->DC.CursorPos;
        s_card_stack.push_back(pos);

        float ww = content_w > 0 ? content_w : ImGui::GetContentRegionAvail().x;
        float h  = 24.f * S;

        ImDrawList* dl = w->DrawList;

        dl->AddRectFilled(pos, ImVec2(pos.x + 3.f * S, pos.y + h), col(UI::accent, a), 2.f);

        ImU32 titleCol = UI::g_dark_mode ? IM_COL32(240, 244, 250, 255) : IM_COL32(30, 35, 45, 255);

        ImGui::PushFont(fontMedium);
        dl->AddText(ImVec2(pos.x + 10.f * S, pos.y + (h - fontMedium->FontSize) * 0.5f),
                    titleCol, title);
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, h + 4.f * S));
    }

    void end_card(float a) {
        if (s_card_stack.empty()) return;
        ImVec2 start = s_card_stack.back();
        s_card_stack.pop_back();

        ImGuiWindow* w = ImGui::GetCurrentWindow();
        ImDrawList* dl = w->DrawList;
        float ww = content_w > 0 ? content_w : ImGui::GetContentRegionAvail().x;
        ImVec2 end_pos = ImVec2(start.x + ww, w->DC.CursorPos.y + 4.f * S);

        dl->AddRect(start, end_pos, col(UI::border, 0.45f * a), 8.f);

        ImGui::Dummy(ImVec2(0, 8.f * S));
    }

    // ============================================
    // Slider
    // ============================================
    void slider_styled(const char* label, float* val, float min, float max, float a, const char* fmt) {
        if (a < 0.01f || !val) return;

        ImGuiWindow* w = ImGui::GetCurrentWindow();
        ImDrawList* dl = w->DrawList;
        ImVec2 pos = w->DC.CursorPos;
        float ww = content_w > 0 ? content_w : ImGui::GetContentRegionAvail().x;

        char val_buf[32];
        snprintf(val_buf, sizeof(val_buf), fmt, *val);

        ImU32 labelCol = UI::g_dark_mode ? IM_COL32(185, 195, 210, 255) : IM_COL32(70, 75, 85, 255);
        ImU32 valueCol = UI::g_dark_mode ? IM_COL32(0, 215, 255, 255) : IM_COL32(0, 135, 195, 255);

        ImGui::PushFont(fontMedium);
        dl->AddText(ImVec2(pos.x + 8.f * S, pos.y), labelCol, label);

        ImVec2 ts = ImGui::CalcTextSize(val_buf);
        float bw = ts.x + 10.f * S;
        float bh = ts.y + 4.f * S;
        ImVec2 bmin(pos.x + ww - bw - 8.f * S, pos.y);
        ImVec2 bmax(bmin.x + bw, bmin.y + bh);

        ImU32 badgeBg = UI::g_dark_mode ? IM_COL32(12, 34, 50, (int)(210 * a))
                                        : IM_COL32(220, 238, 248, (int)(220 * a));
        dl->AddRectFilled(bmin, bmax, badgeBg, 4.f);
        dl->AddRect(bmin, bmax, col(UI::accent, 0.4f * a), 4.f);
        dl->AddText(ImVec2(bmin.x + 5.f * S, bmin.y + 2.f * S), valueCol, val_buf);
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, bh + 2.f * S));

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f * S);
        ImGui::PushItemWidth(ww - 16.f * S);
        ImGui::SliderFloat((std::string("##sld_") + label).c_str(), val, min, max, "");
        ImGui::PopItemWidth();
        ImGui::Dummy(ImVec2(0, 4.f * S));
    }
}
}