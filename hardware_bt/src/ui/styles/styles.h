#pragma once
#include "imgui.h"
#include "imgui_internal.h"
#include <string>
#include <unordered_map>
#include <cmath>
#include <algorithm>

inline float g_sw, g_sh;

namespace NeoUI {  // was `UI` -- collides with OpenSSL's `typedef struct ui_st UI;`
    inline ImVec4 bg           = ImVec4(9/255.f, 13/255.f, 19/255.f, 1.f);
    inline ImVec4 bg_two       = ImVec4(14/255.f, 20/255.f, 28/255.f, 1.f);
    inline ImVec4 sidebar      = ImVec4(8/255.f, 11/255.f, 16/255.f, 1.f);
    inline ImVec4 panel        = ImVec4(16/255.f, 23/255.f, 33/255.f, 0.90f);
    inline ImVec4 widget       = ImVec4(23/255.f, 33/255.f, 47/255.f, 0.92f);

    inline ImVec4 accent       = ImVec4(0/255.f, 195/255.f, 255/255.f, 1.f);
    inline ImVec4 accent_light = ImVec4(60/255.f, 215/255.f, 255/255.f, 1.f);
    inline ImVec4 accent_dark  = ImVec4(14/255.f, 85/255.f, 125/255.f, 1.f);

    inline ImVec4 text         = ImVec4(240/255.f, 244/255.f, 250/255.f, 1.f);
    inline ImVec4 text_light   = ImVec4(255/255.f, 255/255.f, 255/255.f, 1.f);
    inline ImVec4 text_dim     = ImVec4(120/255.f, 138/255.f, 160/255.f, 1.f);
    inline ImVec4 border       = ImVec4(0/255.f, 195/255.f, 255/255.f, 0.35f);
    inline ImVec4 border_light = ImVec4(60/255.f, 215/255.f, 255/255.f, 0.55f);
    inline ImVec4 border_dark  = ImVec4(5/255.f, 8/255.f, 12/255.f, 1.f);
    inline ImVec4 subtab_bg    = ImVec4(12/255.f, 17/255.f, 24/255.f, 1.f);

    inline ImVec4 ButtonColor   = ImVec4(12/255.f, 105/255.f, 150/255.f, 0.85f);
    inline ImVec4 ButtonHovered = ImVec4(0/255.f, 175/255.f, 235/255.f, 1.0f);
    inline ImVec4 ButtonActive  = ImVec4(8/255.f, 80/255.f, 120/255.f, 1.0f);

    inline bool g_dark_mode = true;

    inline void ButtonStyle() {
        ImGui::PushStyleColor(ImGuiCol_Button, ButtonColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ButtonHovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ButtonActive);
    }
    inline void PopButtonStyle() {
        ImGui::PopStyleColor(3);
    }

    inline void ApplyGothTheme() {
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding    = 12.f;
        s.FrameRounding     = 5.f;
        s.PopupRounding     = 6.f;
        s.ScrollbarRounding = 6.f;
        s.GrabRounding      = 4.f;
        s.TabRounding       = 6.f;

        s.Colors[ImGuiCol_Text]                  = text;
        s.Colors[ImGuiCol_TextDisabled]          = text_dim;
        s.Colors[ImGuiCol_WindowBg]              = ImVec4(0.035f, 0.045f, 0.065f, 0.98f);
        s.Colors[ImGuiCol_ChildBg]               = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        s.Colors[ImGuiCol_PopupBg]               = ImVec4(0.05f, 0.07f, 0.10f, 0.98f);
        s.Colors[ImGuiCol_Border]                = border;
        s.Colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        s.Colors[ImGuiCol_FrameBg]               = ImVec4(0.06f, 0.09f, 0.14f, 0.90f);
        s.Colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.08f, 0.18f, 0.28f, 0.85f);
        s.Colors[ImGuiCol_FrameBgActive]         = ImVec4(0.10f, 0.28f, 0.42f, 0.95f);
        s.Colors[ImGuiCol_TitleBg]               = ImVec4(0.035f, 0.045f, 0.065f, 1.00f);
        s.Colors[ImGuiCol_TitleBgActive]         = ImVec4(0.05f, 0.08f, 0.12f, 1.00f);
        s.Colors[ImGuiCol_CheckMark]             = ImVec4(0.00f, 0.85f, 1.00f, 1.00f);
        s.Colors[ImGuiCol_SliderGrab]            = ImVec4(0.00f, 0.75f, 0.98f, 0.90f);
        s.Colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.25f, 0.88f, 1.00f, 1.00f);
        s.Colors[ImGuiCol_Button]                = ButtonColor;
        s.Colors[ImGuiCol_ButtonHovered]         = ButtonHovered;
        s.Colors[ImGuiCol_ButtonActive]          = ButtonActive;
        s.Colors[ImGuiCol_Header]                = ImVec4(0.06f, 0.18f, 0.28f, 0.80f);
        s.Colors[ImGuiCol_HeaderHovered]         = ImVec4(0.08f, 0.30f, 0.46f, 0.90f);
        s.Colors[ImGuiCol_HeaderActive]          = ImVec4(0.10f, 0.42f, 0.62f, 1.00f);
        s.Colors[ImGuiCol_Separator]             = ImVec4(0.08f, 0.32f, 0.48f, 0.40f);
        s.Colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.12f, 0.50f, 0.72f, 0.70f);
        s.Colors[ImGuiCol_SeparatorActive]       = ImVec4(0.15f, 0.75f, 1.00f, 1.00f);
        s.Colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.02f, 0.03f, 0.05f, 0.60f);
        s.Colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.06f, 0.25f, 0.38f, 0.80f);
        s.Colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.09f, 0.38f, 0.55f, 0.90f);
        s.Colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.12f, 0.55f, 0.80f, 1.00f);
    }
}

namespace style {
    inline std::unordered_map<std::string, float> anims;
    inline std::unordered_map<std::string, ImVec4> anim_colors;
    inline float content_w = 0.f;
    inline float content_alpha = 1.f;
    inline bool popup_open = false;
    inline std::string active_popup = "";
    inline constexpr float S = 2.5f;

    static float dt = 0.016f;
    static bool clicked = false;

    static inline float lrp(float a, float b, float t) { return a + (b - a) * t; }
    static inline ImVec4 lrp_col(const ImVec4& a, const ImVec4& b, float t) {
        return ImVec4(lrp(a.x, b.x, t), lrp(a.y, b.y, t), lrp(a.z, b.z, t), lrp(a.w, b.w, t));
    }

    inline void tick() {
        clicked = false;
        static float lt = 0.f;
        float ct = ImGui::GetTime();
        if (lt > 0.f) { dt = ct - lt; dt = ImClamp(dt, 0.001f, 0.1f); }
        lt = ct;
    }

    inline float anim(const std::string& id, float tgt, float spd = 12.f) {
        auto it = anims.find(id);
        if (it == anims.end()) { anims[id] = tgt; return tgt; }
        float t = ImClamp(spd * dt, 0.f, 1.f);
        it->second = lrp(it->second, tgt, t);
        if (fabsf(it->second - tgt) < 0.001f) it->second = tgt;
        return it->second;
    }

    inline ImVec4 anim_col(const std::string& id, const ImVec4& tgt, float spd = 12.f) {
        auto it = anim_colors.find(id);
        if (it == anim_colors.end()) { anim_colors[id] = tgt; return tgt; }
        float t = ImClamp(spd * dt, 0.f, 1.f);
        it->second = lrp_col(it->second, tgt, t);
        return it->second;
    }

    inline ImU32 col(const ImVec4& c, float a = 1.f) {
        return IM_COL32((int)(c.x * 255), (int)(c.y * 255), (int)(c.z * 255), (int)(c.w * 255 * a * content_alpha));
    }

    inline bool popup() { return popup_open || !active_popup.empty(); }

    inline void close_popup() { active_popup = ""; popup_open = false; }

    inline void popups() {
        if (active_popup.empty()) return;
        float pa = anim(active_popup + "_pa", 1.f, 18.f);
        if (pa < 0.01f) { active_popup = ""; return; }
        if (pa > 0.15f && ImGui::IsMouseClicked(0) && !clicked) { active_popup = ""; popup_open = false; }
    }
}