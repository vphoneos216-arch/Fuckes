#define IMGUI_DEFINE_MATH_OPERATORS
#include "bar.hpp"
#include "whitecloud_widgets.hpp"
#include "game/data.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <cmath>
#include <string>

extern float g_sw, g_sh;

namespace ui::bar {
    static float prog = 1.0f;

    void set_game_alpha(float a) { g_game_alpha = a; }
    float game_alpha() { return g_game_alpha; }

    void set_open(bool open) {
        g_open = open;
        g_alpha = open ? 1.0f : 0.0f;
    }

    void toggle() { set_open(!g_open); }

    void render() {
        if (g_game_alpha < 0.001f) return;

        float pxx = g_sw * 0.5f;
        float island_sz = wc::island_size;

        int play_count = 0;
        int bot_count = 0;
        {
            std::lock_guard<std::mutex> lk(data::players_mutex);
            for (const auto& p : data::players) {
                if (p.isBot) bot_count++;
                else play_count++;
            }
        }

        std::string play = std::to_string(play_count);
        std::string bot  = std::to_string(bot_count);

        wc::WidgetState& logo          = wc::wh["logo"];
        wc::WidgetState& player_pos_wc = wc::wh["player_count_pos"];
        wc::WidgetState& island_width  = wc::wh["island_width"];
        wc::WidgetState& island_height = wc::wh["island_height"];

        float island_height_f = 75.0f * island_sz;

        if (g_open) {
            logo.end = 50.0f;
            player_pos_wc.end = 0.0f;
            wc::rounding_val = 25.0f;
        } else {
            logo.end = 0.0f;
            player_pos_wc.end = 45.0f;
            wc::rounding_val = 50.0f;
        }

        wc::Lerp(logo.beg, logo.end, 0.6f);
        wc::Lerp(player_pos_wc.beg, player_pos_wc.end, 0.1f);
        wc::Lerp(island_width.beg, island_width.end, 0.125f);
        wc::Lerp(island_height.beg, island_height.end, 0.125f);

        float island_width_val = island_width.beg;

        ImDrawList* fg = ImGui::GetForegroundDrawList();

        // White Pill Background
        fg->AddRectFilled(
            {pxx - 95.0f * island_sz - island_width_val * island_sz, 35.0f * island_sz},
            {pxx + 95.0f * island_sz + island_width_val * island_sz, 35.0f * island_sz + island_height_f},
            ImColor(225, 225, 225, 255),
            200.0f, 0
        );

        // White Pill Border
        fg->AddRect(
            {pxx - 95.0f * island_sz - island_width_val * island_sz, 35.0f * island_sz},
            {pxx + 95.0f * island_sz + island_width_val * island_sz, 35.0f * island_sz + island_height_f},
            ImColor(255, 255, 255, 255),
            200.0f, 0, 3.3f
        );

        // Text Display inside Island
        if (player_pos_wc.beg > 10.0f) {
            float font_sz = 50.0f * island_sz;
            ImVec2 textSize_play = ImGui::GetFont()->CalcTextSizeA(font_sz, FLT_MAX, -1, play.c_str(), NULL, NULL);
            fg->AddText(NULL, font_sz,
                ImVec2(pxx - (textSize_play.x * 0.5f) - (player_pos_wc.beg) * island_sz,
                       (74.0f) * island_sz - (textSize_play.y * 0.5f)),
                ImColor(255, 0, 0, 255), play.c_str());

            ImVec2 textSize_bot = ImGui::GetFont()->CalcTextSizeA(font_sz, FLT_MAX, -1, bot.c_str(), NULL, NULL);
            fg->AddText(NULL, font_sz,
                ImVec2(pxx - (textSize_bot.x * 0.5f) + (player_pos_wc.beg) * island_sz,
                       (74.0f) * island_sz - (textSize_bot.y * 0.5f)),
                ImColor(0, 255, 0, 240), bot.c_str());
        } else if (logo.beg > 5.0f) {
            std::string bynh = "Click to Close";
            float font_sz = (logo.beg) * island_sz;
            ImVec2 textSize_bynh = ImGui::GetFont()->CalcTextSizeA(font_sz, FLT_MAX, -1, bynh.c_str(), NULL, NULL);
            fg->AddText(NULL, font_sz,
                ImVec2(pxx - (textSize_bynh.x * 0.5f),
                       (73.5f) * island_sz - (textSize_bynh.y * 0.5f)),
                ImColor(30, 144, 255), bynh.c_str());
        }

        // Exact Click Touch Region Window - sized precisely to the pill to avoid overlapping menu
        float pill_total_w = (95.0f + island_width_val) * island_sz * 2.0f;
        float pill_total_h = island_height_f;
        ImVec2 childWindowSize = ImVec2(pill_total_w, pill_total_h);
        ImVec2 childWindowPos  = ImVec2(pxx - (pill_total_w * 0.5f), 35.0f * island_sz);
        ImGui::SetNextWindowPos(childWindowPos);
        ImGui::SetNextWindowSize(childWindowSize);

        ImVec4 transparentColor = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, transparentColor);
        ImGui::PushStyleColor(ImGuiCol_Border, transparentColor);
        ImGui::PushStyleColor(ImGuiCol_BorderShadow, transparentColor);
        ImGui::PushStyleColor(ImGuiCol_Button, transparentColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, transparentColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, transparentColor);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 100.0f);

        ImGui::Begin("IslandClickEvent", NULL,
                     ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);

        if (ImGui::Button("##island_click_btn", childWindowSize)) {
            toggle();
        }

        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(6);
    }
}