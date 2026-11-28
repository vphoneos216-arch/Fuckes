#pragma once
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <ctime>
#include <chrono>
#include <atomic>
#include "imgui.h"
#include "imgui_internal.h"

namespace ui::wc {

struct WidgetState {
    int Tab = 0;
    float beg = 0.0f, end = 0.0f;
    float position = 0.0f;
    float anim = 0.0f, anim_current = 0.0f, anim_target = 0.0f;
    float anim_inc_speed = 0.0f, anim_dec_speed = 0.0f, anim_min = 0.0f, anim_expand_pct = 0.0f;
    bool anim_switch = false, show_content = false;
    ImVec2 p{0, 0};
    ImVec2 b{0, 0};
    ImColor color{200, 200, 200, 255};
};

inline std::map<std::string, WidgetState> wh;

inline float anim_speed = 1.5f;
inline float speed = 1.5f;
inline float island_size = 1.0f;
inline float rounding_val = 25.0f;

inline float R_col = 255.0f, G_col = 255.0f, B_col = 255.0f, A_col = 255.0f;
inline ImVec4* currentSelectedVec4 = nullptr;

inline void Lerp(float& a, const float& b, float t) {
    a += (b - a) * (t * speed);
}

inline void window_set() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Text] = ImColor(0, 0, 0, 255);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.86f, 0.86f, 0.86f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_PopupBg] = ImColor(255, 255, 255, 255);
    style.Colors[ImGuiCol_BorderShadow] = ImColor(255, 255, 255, 0);
    style.Colors[ImGuiCol_ScrollbarBg] = ImColor(255, 255, 255, 255);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImColor(211, 211, 211, 255);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImColor(211, 211, 211, 255);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImColor(211, 211, 211, 255);
    style.Colors[ImGuiCol_CheckMark] = ImColor(30, 144, 255, 255);
    style.Colors[ImGuiCol_SliderGrab] = ImColor(30, 144, 255, 100);
    style.Colors[ImGuiCol_SliderGrabActive] = ImColor(30, 144, 255, 100);
    style.Colors[ImGuiCol_Header] = ImColor(30, 144, 255, 100);

    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.WindowBorderSize = 3.0f;
    style.ChildRounding = 12.5f;
    style.ChildBorderSize = 1.0f;
    style.FrameRounding = 20.0f;
    style.FrameBorderSize = 2.0f;
    style.FramePadding = ImVec2(11, 11);
    style.ScrollbarRounding = 15.0f;
    style.ScrollbarSize = 25.0f;
    style.GrabMinSize = 30.0f;
    style.GrabRounding = 999.0f;
}

inline void zhu_set() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding_val);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.5f, 0.5f, 0.5f, 0.0f));
}

inline void title(const char* text, float size = 6.0f,
                  ImVec4 color = ImVec4(50.0f / 255.0f, 150.0f / 255.0f, 255.0f / 255.0f, 1.0f),
                  ImVec4 textcolor = ImVec4(50.0f / 255.0f, 150.0f / 255.0f, 255.0f / 255.0f, 1.0f)) {
    ImGui::TextColored(textcolor, "%s", text);
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    ImVec2 start = ImVec2(cursor_pos.x, cursor_pos.y + ImGui::GetFrameHeight() * 0.5f - 15.0f);
    ImVec2 end = ImVec2(start.x + ImGui::GetContentRegionAvail().x, start.y);
    ImGui::GetWindowDrawList()->AddLine(start, end, ImGui::GetColorU32(color), size);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
}

inline void Line(float longg = -1.0f, float size = 1.0f, ImVec4 color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f)) {
    if (longg <= 0.0f) longg = ImGui::GetContentRegionAvail().x;
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    ImVec2 end = ImVec2(cursor_pos.x + longg, cursor_pos.y);
    ImGui::GetWindowDrawList()->AddLine(cursor_pos, end, ImGui::GetColorU32(color), size);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f);
}

inline bool button(const std::string& name, ImVec2 size, float dingsize = 15.0f,
                   ImVec4 color = ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
                   ImVec4 color1 = ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
                   ImVec4 color2 = ImVec4(0.2f, 0.2f, 0.2f, 0.5f)) {
    ImGui::PushStyleColor(ImGuiCol_Button,        color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
    ImGui::PushStyleColor(ImGuiCol_Border,        color1);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  color2);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, dingsize);
    bool clicked = ImGui::Button(name.c_str(), size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return clicked;
}

inline bool Checkbox(const char* label, bool* value) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9999.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    bool ret = ImGui::Checkbox(label, value);

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    return ret;
}

inline bool Toggle(const char* label, bool* value) {
    ImVec4 bgColor = *value ? ImVec4(30.0f / 255.0f, 144.0f / 255.0f, 255.0f / 255.0f, 1.0f)
                            : ImVec4(211.0f / 255.0f, 211.0f / 255.0f, 211.0f / 255.0f, 1.0f);
    ImVec4 checkMarkColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, bgColor);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, bgColor);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, bgColor);

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        ImGui::PopStyleColor(3);
        return false;
    }

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

    float height = ImGui::GetFrameHeight();
    const ImVec2 pos = window->DC.CursorPos;

    float width = height * 2.0f;
    float radius = height * 0.42f;

    const ImRect total_bb(pos, ImVec2(pos.x + width + (label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f), pos.y + label_size.y + style.FramePadding.y * 3.0f));

    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id)) {
        ImGui::PopStyleColor(3);
        return false;
    }

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);
    if (pressed) {
        *value = !*value;
        ImGui::MarkItemEdited(id);
    }

    float t = *value ? 1.0f : 0.0f;
    if (g.LastActiveId == id) {
        float t_anim = ImSaturate(g.LastActiveIdTimer / 0.16f);
        t = *value ? (t_anim) : (1.0f - t_anim);
    }

    ImU32 col_bg = ImGui::GetColorU32((held && hovered) ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);

    const ImRect frame_bb(pos, ImVec2(pos.x + width, pos.y + height));
    ImGui::RenderFrame(frame_bb.Min, frame_bb.Max, col_bg, true, height * 0.6f);
    ImGui::RenderNavHighlight(total_bb, id);

    ImVec2 label_pos = ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, frame_bb.Min.y + style.FramePadding.y);
    ImGui::RenderText(label_pos, label);

    window->DrawList->AddCircleFilled(ImVec2(pos.x + radius * 1.2f + t * (width - radius * 2.4f), pos.y + height * 0.5f), radius - 1.5f, ImGui::ColorConvertFloat4ToU32(checkMarkColor), 36);

    ImGui::PopStyleColor(3);
    return pressed;
}

inline bool SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = NULL, ImGuiSliderFlags flags = 0, float cursor_offset_x = 0) {
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.86f, 0.86f, 0.86f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.86f, 0.86f, 0.86f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.86f, 0.86f, 0.86f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.196f, 0.588f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.196f, 0.588f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImColor(0, 0, 0, 85).Value);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

    ImGui::Text("%s", label);
    ImGui::PopStyleColor(); // pop text

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPos().x + cursor_offset_x, ImGui::GetCursorPos().y - 13.0f));
    bool ret = ImGui::SliderFloat(label, v, v_min, v_max, format, flags);
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPos().x, ImGui::GetCursorPos().y - 13.0f));
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(2);
    return ret;
}

inline bool SliderFloat1(const char* label, float* v, float v_min, float v_max, const char* format = NULL, ImGuiSliderFlags flags = 0, float cursor_offset_x = 0) {
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.86f, 0.86f, 0.86f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.86f, 0.86f, 0.86f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.86f, 0.86f, 0.86f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.196f, 0.588f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.196f, 0.588f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImColor(0, 0, 0, 85).Value);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

    ImGui::Text("%s", label);
    ImGui::PopStyleColor();

    ImGui::PushItemWidth(380.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPos().x + cursor_offset_x, ImGui::GetCursorPos().y - 13.0f));
    bool ret = ImGui::SliderFloat(label, v, v_min, v_max, format, flags);
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPos().x, ImGui::GetCursorPos().y - 13.0f));
    ImGui::PopItemWidth();
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(2);
    return ret;
}

inline bool SliderFloat2(const char* label, float* v, float v_min, float v_max, const char* format = NULL, ImGuiSliderFlags flags = 0, float cursor_offset_x = 0) {
    return SliderFloat1(label, v, v_min, v_max, format, flags, cursor_offset_x);
}

inline bool RGBSliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = NULL, ImGuiSliderFlags flags = 0, float cursor_offset_x = 0) {
    return SliderFloat(label, v, v_min, v_max, format, flags, cursor_offset_x);
}

inline bool RGBSliderFloat1(const char* label, float* v, float v_min, float v_max, const char* format = NULL, ImGuiSliderFlags flags = 0, float cursor_offset_x = 0) {
    return SliderFloat1(label, v, v_min, v_max, format, flags, cursor_offset_x);
}

inline void PaletteButton(const std::string& names, ImVec4& colorRef, int TabIndex, float btn_w = 200, float btn_h = 75, int rect_pos = 0, float rect_rounding = 17) {
    ImVec2 min, max, textPos, textMin, textMax;

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0, 0, 0, 1));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0, 0, 0, 0));

    if (ImGui::Button(names.c_str(), ImVec2(btn_w, btn_h))) {
        currentSelectedVec4 = &colorRef;
        R_col = colorRef.x * 255.0f;
        G_col = colorRef.y * 255.0f;
        B_col = colorRef.z * 255.0f;
        A_col = colorRef.w * 255.0f;
    }

    ImGui::PopStyleColor(5);

    ImU32 swatchColor = ImGui::ColorConvertFloat4ToU32(colorRef);

    if (!rect_pos) {
        min = ImVec2(ImGui::GetItemRectMin().x + btn_w * 0.6f, ImGui::GetItemRectMin().y + 20);
        max = ImVec2(ImGui::GetItemRectMax().x - 20, ImGui::GetItemRectMax().y - 20);
        ImGui::GetWindowDrawList()->AddRectFilled(min, max, swatchColor, 12);
    } else {
        min = ImVec2(ImGui::GetItemRectMin().x + btn_w * 0.744f, ImGui::GetItemRectMin().y + 15);
        max = ImVec2(ImGui::GetItemRectMax().x - 15, ImGui::GetItemRectMax().y - 15);
        ImGui::GetWindowDrawList()->AddRectFilled(min, max, swatchColor, 12);
    }

    textMin = ImGui::GetItemRectMin();
    textMax = ImVec2(ImGui::GetItemRectMin().x + (ImGui::GetItemRectMax().x - ImGui::GetItemRectMin().x) / 2,
                     ImGui::GetItemRectMin().y + (ImGui::GetItemRectMax().y - ImGui::GetItemRectMin().y) / 2);
    auto textSize = ImGui::GetFont()->CalcTextSizeA(26.0f, FLT_MAX, -1, names.c_str(), NULL, NULL);
    textPos = ImVec2(textMax.x - (textMax.x - textMin.x) / 2 - (textSize.x / 2), textMax.y - (textSize.y / 2));

    if (!rect_pos) {
        ImGui::GetWindowDrawList()->AddText(NULL, 26.0f, textPos, ImColor(0, 0, 0), names.c_str());
    } else {
        textPos = ImVec2(textMax.x - (textMax.x - textMin.x) / 2 - (textSize.x / 2) - 8, textMax.y - (textSize.y / 2));
        ImGui::GetWindowDrawList()->AddText(NULL, 24.0f, textPos, ImColor(0, 0, 0), ("Edit: " + names).c_str());
    }
}

inline void SlidingSelector(const std::vector<std::string>& names, float& param_val, bool is_centered = false, float btn_w = 120, float btn_h = 70, float rect_rounding = 17) {
    if (names.empty()) return;
    ImVec2 btn_size = ImVec2(btn_w, btn_h);
    ImVec2 rect_size = ImVec2(btn_size.x * 0.97f, btn_size.y * 0.95f);
    ImVec2 diff_size = ImVec2(btn_size.x - rect_size.x, btn_size.y - rect_size.y);
    ImVec2 win_size(btn_size.x * names.size(), btn_size.y);
    ImVec2 min, max;
    WidgetState& name = wh[names[0]];

    int max_tab = static_cast<int>(names.size()) - 1;
    name.Tab = static_cast<int>(param_val);
    if (name.Tab < 0) name.Tab = 0;
    if (name.Tab > max_tab) name.Tab = max_tab;
    name.end = btn_size.x * name.Tab;

    if (is_centered) {
        ImGui::SetNextWindowPos(ImVec2(
            ImGui::GetWindowPos().x + (ImGui::GetWindowSize().x - win_size.x) * 0.5f,
            ImGui::GetWindowPos().y + ImGui::GetCursorPos().y
        ));
    }

    ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.86f, 0.86f, 0.86f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,       ImVec4(0.86f, 0.86f, 0.86f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.86f, 0.86f, 0.86f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.86f, 0.86f, 0.86f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.86f, 0.86f, 0.86f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, ImGui::GetStyle().ItemSpacing.y));

    std::string child_id = "##sel_" + names[0];
    ImGui::BeginChild(child_id.c_str(), win_size, true);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
    if (ImGui::Button(names[0].c_str(), ImVec2(btn_size.x, btn_size.y))) {
        name.Tab = 0;
        param_val = 0;
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    min = ImVec2(ImGui::GetItemRectMin().x + diff_size.x, ImGui::GetItemRectMin().y + diff_size.y);
    max = ImVec2(ImGui::GetItemRectMax().x - diff_size.x, ImGui::GetItemRectMax().y - diff_size.y);

    Lerp(name.beg, name.end, 0.06f);

    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(min.x + name.beg, min.y),
        ImVec2(max.x + name.beg, max.y),
        ImColor(255, 255, 255, 255),
        rect_rounding
    );

    for (size_t i = 0; i < names.size(); i++) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));

        if (i + 1 < names.size()) {
            if (ImGui::Button(names[i + 1].c_str(), ImVec2(btn_size.x, btn_size.y))) {
                name.Tab = static_cast<int>(i + 1);
                param_val = static_cast<float>(i + 1);
            }
            ImGui::SameLine();
        }
        ImGui::PopStyleColor();

        if (name.Tab == (int)i) {
            name.end = btn_size.x * i;
        }

        const std::string& bynh = names[i];
        ImVec2 center_pt(min.x, min.y);
        if (i > 0) {
            center_pt.x += btn_size.x * i;
        }
        float px = center_pt.x + (max.x - min.x) * 0.5f;
        float py = center_pt.y + (max.y - min.y) * 0.5f;
        auto textSize_bynh = ImGui::GetFont()->CalcTextSizeA(28.0f, FLT_MAX, -1, bynh.c_str(), NULL, NULL);
        ImVec2 textPos = ImVec2(px - (textSize_bynh.x * 0.5f), py - (textSize_bynh.y * 0.5f));

        ImGui::GetWindowDrawList()->AddText(NULL, 28.0f, textPos, ImColor(0, 0, 0, 255), bynh.c_str());
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(2);
}

} // namespace ui::wc
