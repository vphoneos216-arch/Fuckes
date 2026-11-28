#define IMGUI_DEFINE_MATH_OPERATORS
#include "bar.hpp"
#include "whitecloud_widgets.hpp"
#include "cfg.hpp"
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

    static void render_bar_style(int play_count, int bot_count) {
        std::string play = std::to_string(play_count);
        std::string bot  = std::to_string(bot_count);

        float pxx = g_sw * 0.5f;
        float island_sz = wc::island_size;

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

        // Text Display inside Island -- the number readout is gated by
        // counter_enabled, but the pill and its close label are not, so
        // there's always a way back into the menu.
        if (cfg::hud::counter_enabled && player_pos_wc.beg > 10.0f) {
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
            std::string bynh = "InvisibleX";
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

    // Volume style -- a vertical rounded slider (like a phone's volume
    // popup) instead of the horizontal Bar pill: narrow and tall when
    // closed, stacking the enemy/bot counts top-to-bottom instead of
    // side-by-side; widens and shows "InvisibleX" when open. Same tap-
    // to-toggle behaviour and the same counter_enabled gating as Bar.
    static void render_volume_style(int play_count, int bot_count) {
        std::string play = std::to_string(play_count);
        std::string bot  = std::to_string(bot_count);

        float pxx = g_sw * 0.5f;
        float island_sz = wc::island_size;

        wc::WidgetState& open_w  = wc::wh["volume_width"];
        wc::WidgetState& open_h  = wc::wh["volume_height"];

        if (g_open) {
            open_w.end = 150.0f;
            open_h.end = 40.0f;
        } else {
            open_w.end = 0.0f;
            open_h.end = 0.0f;
        }
        wc::Lerp(open_w.beg, open_w.end, 0.125f);
        wc::Lerp(open_h.beg, open_h.end, 0.125f);

        float base_w = 70.0f * island_sz;
        float base_h = 150.0f * island_sz;
        float w = base_w + open_w.beg * island_sz;
        float h = base_h + open_h.beg * island_sz;
        float top_y = 35.0f * island_sz;

        ImVec2 tl(pxx - w * 0.5f, top_y);
        ImVec2 br(pxx + w * 0.5f, top_y + h);
        float rounding = w * 0.5f;

        ImDrawList* fg = ImGui::GetForegroundDrawList();
        fg->AddRectFilled(tl, br, ImColor(225, 225, 225, 255), rounding, 0);
        fg->AddRect(tl, br, ImColor(255, 255, 255, 255), rounding, 0, 3.3f);

        if (cfg::hud::counter_enabled && !g_open) {
            float font_sz = 32.0f * island_sz;
            ImVec2 ts_play = ImGui::GetFont()->CalcTextSizeA(font_sz, FLT_MAX, -1, play.c_str(), NULL, NULL);
            fg->AddText(NULL, font_sz,
                ImVec2(pxx - ts_play.x * 0.5f, tl.y + h * 0.28f - ts_play.y * 0.5f),
                ImColor(255, 0, 0, 255), play.c_str());

            ImVec2 ts_bot = ImGui::GetFont()->CalcTextSizeA(font_sz, FLT_MAX, -1, bot.c_str(), NULL, NULL);
            fg->AddText(NULL, font_sz,
                ImVec2(pxx - ts_bot.x * 0.5f, tl.y + h * 0.72f - ts_bot.y * 0.5f),
                ImColor(0, 255, 0, 240), bot.c_str());
        } else if (g_open) {
            std::string bynh = "InvisibleX";
            float font_sz = 22.0f * island_sz;
            ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(font_sz, FLT_MAX, -1, bynh.c_str(), NULL, NULL);
            fg->AddText(NULL, font_sz,
                ImVec2(pxx - ts.x * 0.5f, tl.y + h * 0.5f - ts.y * 0.5f),
                ImColor(30, 144, 255), bynh.c_str());
        }

        ImVec2 childWindowSize(w, h);
        ImGui::SetNextWindowPos(tl);
        ImGui::SetNextWindowSize(childWindowSize);

        ImVec4 transparentColor = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, transparentColor);
        ImGui::PushStyleColor(ImGuiCol_Border, transparentColor);
        ImGui::PushStyleColor(ImGuiCol_BorderShadow, transparentColor);
        ImGui::PushStyleColor(ImGuiCol_Button, transparentColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, transparentColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, transparentColor);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 100.0f);

        ImGui::Begin("VolumeClickEvent", NULL,
                     ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);

        if (ImGui::Button("##volume_click_btn", childWindowSize)) {
            toggle();
        }

        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(6);
    }

    void render() {
        if (g_game_alpha < 0.001f) return;

        int play_count = 0;
        int bot_count = 0;
        {
            std::lock_guard<std::mutex> lk(data::players_mutex);
            for (const auto& p : data::players) {
                if (p.isBot) bot_count++;
                else play_count++;
            }
        }

        if (cfg::hud::counter_style == 1) render_volume_style(play_count, bot_count);
        else render_bar_style(play_count, bot_count);
    }
}