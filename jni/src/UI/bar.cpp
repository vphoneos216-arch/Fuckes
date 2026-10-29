#include "bar.hpp"
#include "imgui.h"
#include "imgui_internal.h"

namespace ui {
namespace bar {

void render() {
    ImGuiIO& io = ImGui::GetIO();
    static float lt = 0.f;
    float ct = (float)ImGui::GetTime();

    if (g_sw < 10.f || g_sh < 10.f) {
        g_sw = io.DisplaySize.x;
        g_sh = io.DisplaySize.y;
    }
    if (g_sw < 10.f || g_sh < 10.f) return;
    if (g_game_alpha < 0.001f) return;

    // Larger OG white bar bottom-center — easy to tap
    float sc = g_sw / 1920.f;
    if (sc < 0.4f) sc = 0.4f;
    if (sc > 1.5f) sc = 1.5f;

    float bw = 110.f * sc;   /* was 50 — wider */
    float bh = 14.f;         /* was 7  — taller */
    ImVec2 bar_min((g_sw - bw) * 0.5f, g_sh - bh - 12.f);
    ImVec2 bar_max((g_sw + bw) * 0.5f, g_sh - 12.f);

    /* Big invisible hit zone around the bar */
    ImVec2 click_size(bw + 120.f * sc, bh + 100.f * sc);
    ImVec2 click_pos(bar_min.x - 60.f * sc, bar_min.y - 50.f * sc);

    ImGui::SetNextWindowPos(click_pos);
    ImGui::SetNextWindowSize(click_size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##bar_handle", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);

    if (ImGui::InvisibleButton("##b_btn", click_size)) {
        if (ct - lt >= 0.18f) {
            g_open   = !g_open;
            g_closed = !g_open;
            lt = ct;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    int a = (int)(220.f * g_game_alpha);
    if (a > 0) {
        dl->AddRectFilled(bar_min, bar_max, IM_COL32(255, 255, 255, a), 14.f);
        dl->AddRect(bar_min, bar_max, IM_COL32(255, 255, 255, a / 2), 14.f, 0, 2.0f);
        /* soft glow under bar for visibility */
        dl->AddRectFilled(
            ImVec2(bar_min.x - 4.f, bar_min.y - 3.f),
            ImVec2(bar_max.x + 4.f, bar_max.y + 3.f),
            IM_COL32(255, 255, 255, a / 5), 16.f);
    }
    g_alpha = g_open ? 1.0f : 0.0f;
}

} // namespace bar
} // namespace ui
