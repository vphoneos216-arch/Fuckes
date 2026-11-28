#pragma once
#include "release_log.hpp"
#include "imgui.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <android/log.h>

namespace cfg {
namespace esp {
    inline bool  line         = false;
    inline bool  box          = true;
    inline bool  info_bar     = true;
    inline bool  skeleton     = false;
    inline bool  health       = true;
    inline bool  distance     = true;
    inline bool  weapon       = true;
    inline bool  bot_tag      = true;
    inline bool  tracer       = false;
    inline bool  crosshair    = true;
    inline bool  fov_circle   = false;
    inline bool  name         = true;
    // Head Dot, ported from InvisibleX's hook.h (showHeadDot/showHeadDotRadius/
    // headDotColor) -- the one piece of hook.h's ESP visual set this project's
    // own (already-working) esp.hpp draw() didn't have yet.
    inline bool  head_dot        = false;
    inline float head_dot_radius = 4.0f;
    inline ImVec4 head_dot_col   = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);

    inline float max_range     = 250.f;
    inline float box_thickness = 2.0f;
    inline float fov_radius    = 250.f;

    // Distance/name/weapon text size, matching hook.h's own "textSize"
    // field (default 15) exactly -- drawn at a fixed pixel size via
    // ImGui::GetFont()->CalcTextSizeA/dl->AddText(nullptr,size,...),
    // not the current ImGui window font scale, same as hook.h.
    inline float text_size = 15.f;

    // Health bar -- matches hook.h's own fields (healthBarWidth/
    // healthBarRounding/showHealthColorGradient) exactly.
    inline float health_bar_width       = 5.f;
    inline float health_bar_rounding    = 1.5f;
    inline bool  health_color_gradient  = true;

    // White by default -- hook.h's box/line color (box_color_mode==0,
    // Fixed) is the user's own boxCustomColor, which is white in their
    // reference build; both the visible and non-visible fixed colors
    // are set to white here so box AND line (which reuses the box's
    // color) are white out of the box, matching that exactly.
    inline ImVec4 box_col      = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    inline ImVec4 box_visible  = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    inline ImVec4 accent       = ImVec4(0.0f, 0.90f, 1.0f, 1.0f);
    inline ImVec4 name_col     = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    inline ImVec4 distance_col = ImVec4(0.95f, 0.95f, 0.30f, 1.0f);
    inline ImVec4 weapon_col   = ImVec4(1.0f, 0.85f, 0.30f, 1.0f);
    inline ImVec4 bot_col      = ImVec4(1.0f, 0.55f, 0.10f, 1.0f);
    inline ImVec4 tracer_col   = ImVec4(0.0f, 0.90f, 1.0f, 0.55f);
    inline ImVec4 crosshair_col= ImVec4(1.0f, 1.0f, 1.0f, 0.9f);
    inline ImVec4 fov_col      = ImVec4(1.0f, 1.0f, 1.0f, 0.5f);

    // Color box mode, ported from InvisibleX's hook.h (BoxCol/LineCol):
    // 0 Fixed (box_col/box_visible above, unchanged default behaviour),
    // 1 HP gradient, 2 Distance gradient, 3 Rainbow (cycling hue).
    inline int box_color_mode = 0;
}

// ============================================
// Top-of-screen HUD pill (the "Dynamic Island" enemy/bot counter).
// counter_enabled toggles just the number readout; the pill itself (and
// its tap-to-open/close menu handle) stays visible either way, so there
// is always a way back into the menu. counter_style picks which shape
// renders it: 0 = Bar (the existing horizontal pill), 1 = Volume (a
// vertical rounded slider look) -- asked for at startup, right after
// driver selection, same as the driver prompt itself.
// ============================================
namespace hud {
    inline bool counter_enabled = true;
    inline int  counter_style   = 0; // 0 Bar, 1 Volume
}

// ============================================
// Account info (from the InvisibleX-style login) for the Info page.
// Populated once in main() right after g_auth.run() succeeds.
// ============================================
namespace account {
    inline std::string license_key  = "N/A";
    inline std::string seller_name  = "N/A";
    inline std::string expiry_date  = "N/A";
    inline std::string status       = "INACTIVE";
    inline int          days_remain = 0;
    inline long long    remain_secs = 0;
    inline int          devices_used = 0;
    inline int          devices_max  = 0;
}

namespace aim {
    inline bool  visible_enabled  = false;
    inline float visible_fov      = 200.f;
    inline float visible_max      = 250.f;
    inline bool  fov_circle       = false;
    inline float fov_thickness    = 1.5f;
    inline ImVec4 fov_col         = ImVec4(1.f, 1.f, 1.f, 0.6f);
    inline bool  wipe_aim_counter = false;
    inline float head_bias        = 0.0f;
    inline float horizontal_bias  = 0.0f;
    inline bool  scope_safeguard  = true;
    inline int   target_position  = 0;
    inline bool  target_knocked   = false;
}

namespace settings {
    inline int fps_choice = 1;

    inline int get_target_fps() {
        switch (fps_choice) {
            case 0: return 60;
            case 1: return 90;
            case 2: return 120;
            default: return 0;
        }
    }
}

// ============================================
// ✅ Expiry State
// ============================================
namespace status {
    inline bool  is_expired   = false;
    inline bool  is_allowed   = false;
    inline float msg_timer    = 0.f;
    inline float fade_alpha   = 1.f;
}

// ============================================
// Config Save & Load (/data/local/tmp/neoFF.c)
// ============================================
inline const char* CONFIG_FILE_PATH = "/data/local/tmp/neoFF.c";

inline bool save_config(const char* path = CONFIG_FILE_PATH) {
    FILE* fp = fopen(path, "w");
    if (!fp) {
        TB_ANDROID_LOG(ANDROID_LOG_ERROR, "NEO_CFG", "Failed to open %s for writing", path);
        return false;
    }

    fprintf(fp, "# NEO FREE FIRE CONFIG\n");

    // ESP settings
    fprintf(fp, "[esp]\n");
    fprintf(fp, "esp_box=%d\n", cfg::esp::box ? 1 : 0);
    fprintf(fp, "esp_line=%d\n", cfg::esp::line ? 1 : 0);
    fprintf(fp, "esp_name=%d\n", cfg::esp::name ? 1 : 0);
    fprintf(fp, "esp_health=%d\n", cfg::esp::health ? 1 : 0);
    fprintf(fp, "esp_distance=%d\n", cfg::esp::distance ? 1 : 0);
    fprintf(fp, "esp_box_thickness=%.2f\n", cfg::esp::box_thickness);
    fprintf(fp, "esp_health_bar_width=%.2f\n", cfg::esp::health_bar_width);
    fprintf(fp, "esp_health_bar_rounding=%.2f\n", cfg::esp::health_bar_rounding);
    fprintf(fp, "esp_health_color_gradient=%d\n", cfg::esp::health_color_gradient ? 1 : 0);
    fprintf(fp, "esp_text_size=%.1f\n", cfg::esp::text_size);
    fprintf(fp, "esp_max_range=%.1f\n", cfg::esp::max_range);
    fprintf(fp, "esp_box_col=%.3f,%.3f,%.3f,%.3f\n", cfg::esp::box_col.x, cfg::esp::box_col.y, cfg::esp::box_col.z, cfg::esp::box_col.w);

    // Aim settings
    fprintf(fp, "\n[aim]\n");
    fprintf(fp, "aim_visible_enabled=%d\n", cfg::aim::visible_enabled ? 1 : 0);
    fprintf(fp, "aim_visible_fov=%.1f\n", cfg::aim::visible_fov);
    fprintf(fp, "aim_visible_max=%.1f\n", cfg::aim::visible_max);
    fprintf(fp, "aim_fov_circle=%d\n", cfg::aim::fov_circle ? 1 : 0);
    fprintf(fp, "aim_target_position=%d\n", cfg::aim::target_position);
    fprintf(fp, "aim_target_knocked=%d\n", cfg::aim::target_knocked ? 1 : 0);
    fprintf(fp, "aim_head_bias=%.2f\n", cfg::aim::head_bias);
    fprintf(fp, "aim_horizontal_bias=%.2f\n", cfg::aim::horizontal_bias);
    fprintf(fp, "aim_scope_safeguard=%d\n", cfg::aim::scope_safeguard ? 1 : 0);
    fprintf(fp, "aim_fov_col=%.3f,%.3f,%.3f,%.3f\n", cfg::aim::fov_col.x, cfg::aim::fov_col.y, cfg::aim::fov_col.z, cfg::aim::fov_col.w);

    // General Settings
    fprintf(fp, "\n[settings]\n");
    fprintf(fp, "settings_fps_choice=%d\n", cfg::settings::fps_choice);
    fprintf(fp, "hud_counter_enabled=%d\n", cfg::hud::counter_enabled ? 1 : 0);
    // counter_style is NOT persisted here -- it's asked fresh at startup,
    // right after driver selection (see main.cpp), same as the driver
    // choice itself; loading it back from a saved file would silently
    // overwrite whatever the user just picked this run.

    fclose(fp);
    TB_ANDROID_LOG(ANDROID_LOG_INFO, "NEO_CFG", "[+] Saved config to %s", path);
    return true;
}

inline bool load_config(const char* path = CONFIG_FILE_PATH) {
    FILE* fp = fopen(path, "r");
    if (!fp) {
        TB_ANDROID_LOG(ANDROID_LOG_WARN, "NEO_CFG", "No config file at %s", path);
        return false;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '[' || *p == '\r' || *p == '\n' || *p == '\0') continue;

        char key[64] = {0};
        char val[128] = {0};
        if (sscanf(p, "%63[^=]=%127[^\r\n]", key, val) == 2) {
            int len = strlen(val);
            while (len > 0 && (val[len - 1] == ' ' || val[len - 1] == '\r' || val[len - 1] == '\n')) {
                val[--len] = '\0';
            }

            // ESP
            if (strcmp(key, "esp_box") == 0 || strcmp(key, "box") == 0) cfg::esp::box = (atoi(val) != 0);
            else if (strcmp(key, "esp_line") == 0 || strcmp(key, "line") == 0) cfg::esp::line = (atoi(val) != 0);
            else if (strcmp(key, "esp_name") == 0 || strcmp(key, "name") == 0) cfg::esp::name = (atoi(val) != 0);
            else if (strcmp(key, "esp_health") == 0 || strcmp(key, "health") == 0) cfg::esp::health = (atoi(val) != 0);
            else if (strcmp(key, "esp_distance") == 0 || strcmp(key, "distance") == 0) cfg::esp::distance = (atoi(val) != 0);
            else if (strcmp(key, "esp_box_thickness") == 0 || strcmp(key, "box_thickness") == 0) cfg::esp::box_thickness = atof(val);
            else if (strcmp(key, "esp_health_bar_width") == 0) cfg::esp::health_bar_width = atof(val);
            else if (strcmp(key, "esp_health_bar_rounding") == 0) cfg::esp::health_bar_rounding = atof(val);
            else if (strcmp(key, "esp_health_color_gradient") == 0) cfg::esp::health_color_gradient = (atoi(val) != 0);
            else if (strcmp(key, "esp_text_size") == 0) cfg::esp::text_size = atof(val);
            else if (strcmp(key, "esp_max_range") == 0 || strcmp(key, "max_range") == 0) cfg::esp::max_range = atof(val);
            else if (strcmp(key, "esp_box_col") == 0 || strcmp(key, "box_col") == 0) {
                float r, g, b, a;
                if (sscanf(val, "%f,%f,%f,%f", &r, &g, &b, &a) == 4) {
                    cfg::esp::box_col = ImVec4(r, g, b, a);
                }
            }

            // Aim
            else if (strcmp(key, "aim_visible_enabled") == 0 || strcmp(key, "visible_enabled") == 0) cfg::aim::visible_enabled = (atoi(val) != 0);
            else if (strcmp(key, "aim_visible_fov") == 0 || strcmp(key, "visible_fov") == 0) cfg::aim::visible_fov = atof(val);
            else if (strcmp(key, "aim_visible_max") == 0 || strcmp(key, "visible_max") == 0) cfg::aim::visible_max = atof(val);
            else if (strcmp(key, "aim_fov_circle") == 0 || strcmp(key, "fov_circle") == 0) cfg::aim::fov_circle = (atoi(val) != 0);
            else if (strcmp(key, "aim_target_position") == 0 || strcmp(key, "target_position") == 0) cfg::aim::target_position = atoi(val);
            else if (strcmp(key, "aim_target_knocked") == 0 || strcmp(key, "target_knocked") == 0) cfg::aim::target_knocked = (atoi(val) != 0);
            else if (strcmp(key, "aim_head_bias") == 0 || strcmp(key, "head_bias") == 0) cfg::aim::head_bias = atof(val);
            else if (strcmp(key, "aim_horizontal_bias") == 0 || strcmp(key, "horizontal_bias") == 0) cfg::aim::horizontal_bias = atof(val);
            else if (strcmp(key, "aim_scope_safeguard") == 0 || strcmp(key, "scope_safeguard") == 0) cfg::aim::scope_safeguard = (atoi(val) != 0);
            else if (strcmp(key, "aim_fov_col") == 0) {
                float r, g, b, a;
                if (sscanf(val, "%f,%f,%f,%f", &r, &g, &b, &a) == 4) {
                    cfg::aim::fov_col = ImVec4(r, g, b, a);
                }
            }

            // Settings
            else if (strcmp(key, "settings_fps_choice") == 0 || strcmp(key, "fps_choice") == 0) cfg::settings::fps_choice = atoi(val);
            else if (strcmp(key, "hud_counter_enabled") == 0) cfg::hud::counter_enabled = (atoi(val) != 0);
        }
    }

    fclose(fp);
    TB_ANDROID_LOG(ANDROID_LOG_INFO, "NEO_CFG", "[+] Loaded config from %s", path);
    return true;
}

}