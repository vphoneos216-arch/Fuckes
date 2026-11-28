// ============================================
// menu.cpp - KernelSRC UI Unified Implementation
// Full Features from client.sh + KernelSRC UI
// ============================================

#define IMGUI_DEFINE_MATH_OPERATORS
#include "menu.hpp"
#include "bar.hpp"
#include "whitecloud_widgets.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "Android_draw/draw.h"
#include "protect/oxorany.hpp"
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <string>
#include <vector>
#include <inttypes.h>
#include <thread>
#include <cstring>
#include <atomic>
#include <android/log.h>
#include "png/image.hpp"
#include "png/images/logo1.hpp"

#include "driver.h"
#include "other/addr.hpp"
#include "func/esp.hpp"
#include "func/silent.hpp"
#include "func/hwbp_aim.hpp"
#include "game/data.hpp"
#include "cfg.hpp"

#define MLOG(...) ((void)0)

// External functions from data.cpp
extern bool IsDataHackAllowed();
extern int GetControlState();
extern const char* GetControlMessage();
extern const char* GetUpdateURL();
extern const char* GetRedirectURL();
extern void ClearRedirectURL();

enum {
    CS_NONE          = 0,
    CS_LIVE          = 1,
    CS_CRASH         = 2,
    CS_FUCK          = 3,
    CS_FUCKSYSTEM    = 4,
    CS_MAINTENANCE   = 5,
    CS_ALERT         = 6,
    CS_REDIRECT      = 7,
    CS_UPDATE        = 8,
    CS_EXIT          = 9
};

namespace ui::menu {

    static float  content_w = 0.f;

    static GLuint g_LogoTex = 0;
    static bool   g_LogoLoaded = false;
    static ImVec2 g_LogoPos(50.f, 200.f);
    static bool   g_LogoPosInit = false;

    // KernelSRC tab state
    static int   Tab = 0;
    static float setting_tab = 0.0f;
    static bool  menu_window_open = true;

    // Special state dialog renderers
    static void render_all_okey_content(float a) {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        ImDrawList* dl = w->DrawList;
        ImVec2 p = w->DC.CursorPos;
        float ww = content_w > 0 ? content_w : ImGui::GetContentRegionAvail().x;
        float ch = ImGui::GetContentRegionAvail().y;

        float centerX = p.x + ww * 0.5f;
        float centerY = p.y + ch * 0.5f;

        float boxW = ww - 60.f;
        float boxH = 200.f;
        ImVec2 tl(centerX - boxW * 0.5f, centerY - boxH * 0.5f);
        ImVec2 br(centerX + boxW * 0.5f, centerY + boxH * 0.5f);

        for (int i = 0; i < 10; ++i) {
            float off = 10.0f * i;
            float al = 50.0f / (i + 1);
            dl->AddRectFilled(
                ImVec2(tl.x - off, tl.y - off),
                ImVec2(br.x + off, br.y + off),
                IM_COL32(0, 195, 255, (int)al), 14.f);
        }

        dl->AddRectFilled(tl, br, IM_COL32(10, 16, 24, 245), 14.f);
        dl->AddRect(tl, br, IM_COL32(0, 195, 255, 255), 14.f, 0, 3.f);

        ImFont* bold = ImGui::GetIO().Fonts->Fonts[0];
        const char* title = "ALL OKEY!";
        float fontSize = 52.0f;
        ImVec2 tsz = ImGui::CalcTextSize(title);

        dl->AddText(bold, fontSize,
            ImVec2(centerX - (tsz.x * fontSize / ImGui::GetFontSize()) * 0.5f,
                   centerY - fontSize * 0.5f),
            IM_COL32(0, 195, 255, 255), title);
    }

    static void render_expired_content(float a) {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        ImDrawList* dl = w->DrawList;
        ImVec2 p = w->DC.CursorPos;
        float ww = content_w > 0 ? content_w : ImGui::GetContentRegionAvail().x;
        float ch = ImGui::GetContentRegionAvail().y;

        float centerX = p.x + ww * 0.5f;
        float centerY = p.y + ch * 0.5f;

        float boxW = ww - 60.f;
        float boxH = 320.f;
        ImVec2 tl(centerX - boxW * 0.5f, centerY - boxH * 0.5f);
        ImVec2 br(centerX + boxW * 0.5f, centerY + boxH * 0.5f);

        for (int i = 0; i < 12; ++i) {
            float off = 10.0f * i;
            float al = 50.0f / (i + 1);
            dl->AddRectFilled(
                ImVec2(tl.x - off, tl.y - off),
                ImVec2(br.x + off, br.y + off),
                IM_COL32(255, 0, 0, (int)al), 14.f);
        }

        dl->AddRectFilled(tl, br, IM_COL32(8, 8, 12, 245), 14.f);
        dl->AddRect(tl, br, IM_COL32(255, 30, 30, 255), 14.f, 0, 3.5f);

        ImFont* bold = ImGui::GetIO().Fonts->Fonts[0];

        const char* icon = "!";
        float iconSz = 60.0f;
        ImVec2 isz = ImGui::CalcTextSize(icon);
        dl->AddText(bold, iconSz,
            ImVec2(centerX - (isz.x * iconSz / ImGui::GetFontSize()) * 0.5f, tl.y + 25.f),
            IM_COL32(255, 60, 60, 255), icon);

        const char* title = "HACK IS EXPIRED";
        float titleSz = 42.0f;
        ImVec2 tsz = ImGui::CalcTextSize(title);
        dl->AddText(bold, titleSz,
            ImVec2(centerX - (tsz.x * titleSz / ImGui::GetFontSize()) * 0.5f, tl.y + 100.f),
            IM_COL32(255, 40, 40, 255), title);

        const char* sub = "Please Update New Version!";
        float subSz = 26.0f;
        ImVec2 ssz = ImGui::CalcTextSize(sub);
        dl->AddText(bold, subSz,
            ImVec2(centerX - (ssz.x * subSz / ImGui::GetFontSize()) * 0.5f, tl.y + 165.f),
            IM_COL32(255, 200, 200, 255), sub);

        float btnW = 200.f, btnH = 55.f;
        ImVec2 btnTl(centerX - btnW * 0.5f, br.y - btnH - 25.f);
        ImVec2 btnBr(centerX + btnW * 0.5f, br.y - 25.f);

        ImVec2 mouse = ImGui::GetIO().MousePos;
        bool btnHover = mouse.x >= btnTl.x && mouse.x <= btnBr.x &&
                        mouse.y >= btnTl.y && mouse.y <= btnBr.y;

        ImU32 btnBg = btnHover ? IM_COL32(255, 50, 50, 255) : IM_COL32(180, 20, 20, 255);
        dl->AddRectFilled(btnTl, btnBr, btnBg, 10.f);
        dl->AddRect(btnTl, btnBr, IM_COL32(255, 100, 100, 255), 10.f, 0, 2.5f);

        const char* btnTxt = "EXIT";
        float btnFontSz = 30.0f;
        ImVec2 btsz = ImGui::CalcTextSize(btnTxt);
        dl->AddText(bold, btnFontSz,
            ImVec2(btnTl.x + (btnW - btsz.x * btnFontSz / ImGui::GetFontSize()) * 0.5f,
                   btnTl.y + (btnH - btsz.y * btnFontSz / ImGui::GetFontSize()) * 0.5f),
            IM_COL32(255, 255, 255, 255), btnTxt);

        if (btnHover && ImGui::IsMouseClicked(0)) {
            exit(0);
        }
    }

    static void render_crash_content(float a, const char* msg) {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        ImDrawList* dl = w->DrawList;
        ImVec2 p = w->DC.CursorPos;
        float ww = content_w > 0 ? content_w : ImGui::GetContentRegionAvail().x;
        float ch = ImGui::GetContentRegionAvail().y;

        float centerX = p.x + ww * 0.5f;
        float centerY = p.y + ch * 0.5f;

        float boxW = ww - 60.f;
        float boxH = 280.f;
        ImVec2 tl(centerX - boxW * 0.5f, centerY - boxH * 0.5f);
        ImVec2 br(centerX + boxW * 0.5f, centerY + boxH * 0.5f);

        for (int i = 0; i < 12; ++i) {
            float off = 10.0f * i;
            float al = 50.0f / (i + 1);
            dl->AddRectFilled(
                ImVec2(tl.x - off, tl.y - off),
                ImVec2(br.x + off, br.y + off),
                IM_COL32(255, 0, 0, (int)al), 14.f);
        }

        dl->AddRectFilled(tl, br, IM_COL32(8, 8, 12, 245), 14.f);
        dl->AddRect(tl, br, IM_COL32(255, 30, 30, 255), 14.f, 0, 3.5f);

        ImFont* bold = ImGui::GetIO().Fonts->Fonts[0];
        const char* icon = "!";
        float iconSz = 60.0f;
        ImVec2 isz = ImGui::CalcTextSize(icon);
        dl->AddText(bold, iconSz,
            ImVec2(centerX - (isz.x * iconSz / ImGui::GetFontSize()) * 0.5f, tl.y + 25.f),
            IM_COL32(255, 60, 60, 255), icon);

        const char* title = "Something Wrong";
        float titleSz = 42.0f;
        ImVec2 tsz = ImGui::CalcTextSize(title);
        dl->AddText(bold, titleSz,
            ImVec2(centerX - (tsz.x * titleSz / ImGui::GetFontSize()) * 0.5f, tl.y + 100.f),
            IM_COL32(255, 40, 40, 255), title);

        const char* sub = (msg && strlen(msg) > 0) ? msg : "Closing Menu...";
        float subSz = 22.0f;
        ImVec2 ssz = ImGui::CalcTextSize(sub);
        dl->AddText(bold, subSz,
            ImVec2(centerX - (ssz.x * subSz / ImGui::GetFontSize()) * 0.5f, tl.y + 165.f),
            IM_COL32(255, 200, 200, 255), sub);
    }

    static void render_update_content(float a, const char* updateUrl) {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        ImDrawList* dl = w->DrawList;
        ImVec2 p = w->DC.CursorPos;
        float ww = content_w > 0 ? content_w : ImGui::GetContentRegionAvail().x;
        float ch = ImGui::GetContentRegionAvail().y;

        float centerX = p.x + ww * 0.5f;
        float centerY = p.y + ch * 0.5f;

        float boxW = ww - 60.f;
        float boxH = 340.f;
        ImVec2 tl(centerX - boxW * 0.5f, centerY - boxH * 0.5f);
        ImVec2 br(centerX + boxW * 0.5f, centerY + boxH * 0.5f);

        for (int i = 0; i < 12; ++i) {
            float off = 10.0f * i;
            float al = 50.0f / (i + 1);
            dl->AddRectFilled(
                ImVec2(tl.x - off, tl.y - off),
                ImVec2(br.x + off, br.y + off),
                IM_COL32(255, 165, 0, (int)al), 14.f);
        }

        dl->AddRectFilled(tl, br, IM_COL32(12, 10, 8, 245), 14.f);
        dl->AddRect(tl, br, IM_COL32(255, 165, 0, 255), 14.f, 0, 3.5f);

        ImFont* bold = ImGui::GetIO().Fonts->Fonts[0];
        const char* icon = "!";
        float iconSz = 60.0f;
        ImVec2 isz = ImGui::CalcTextSize(icon);
        dl->AddText(bold, iconSz,
            ImVec2(centerX - (isz.x * iconSz / ImGui::GetFontSize()) * 0.5f, tl.y + 25.f),
            IM_COL32(255, 200, 60, 255), icon);

        const char* title = "New Update Available!";
        float titleSz = 38.0f;
        ImVec2 tsz = ImGui::CalcTextSize(title);
        dl->AddText(bold, titleSz,
            ImVec2(centerX - (tsz.x * titleSz / ImGui::GetFontSize()) * 0.5f, tl.y + 100.f),
            IM_COL32(255, 200, 60, 255), title);

        const char* sub = "Please update to continue";
        float subSz = 24.0f;
        ImVec2 ssz = ImGui::CalcTextSize(sub);
        dl->AddText(bold, subSz,
            ImVec2(centerX - (ssz.x * subSz / ImGui::GetFontSize()) * 0.5f, tl.y + 165.f),
            IM_COL32(255, 230, 200, 255), sub);

        float btnW = 220.f, btnH = 55.f;
        ImVec2 btnTl(centerX - btnW * 0.5f, br.y - btnH - 25.f);
        ImVec2 btnBr(centerX + btnW * 0.5f, br.y - 25.f);

        ImVec2 mouse = ImGui::GetIO().MousePos;
        bool btnHover = mouse.x >= btnTl.x && mouse.x <= btnBr.x &&
                        mouse.y >= btnTl.y && mouse.y <= btnBr.y;

        ImU32 btnBg = btnHover ? IM_COL32(255, 180, 40, 255) : IM_COL32(200, 130, 20, 255);
        dl->AddRectFilled(btnTl, btnBr, btnBg, 10.f);
        dl->AddRect(btnTl, btnBr, IM_COL32(255, 200, 100, 255), 10.f, 0, 2.5f);

        const char* btnTxt = "UPDATE NOW";
        float btnFontSz = 26.0f;
        ImVec2 btsz = ImGui::CalcTextSize(btnTxt);
        dl->AddText(bold, btnFontSz,
            ImVec2(btnTl.x + (btnW - btsz.x * btnFontSz / ImGui::GetFontSize()) * 0.5f,
                   btnTl.y + (btnH - btsz.y * btnFontSz / ImGui::GetFontSize()) * 0.5f),
            IM_COL32(255, 255, 255, 255), btnTxt);

        if (btnHover && ImGui::IsMouseClicked(0)) {
            if (updateUrl && strlen(updateUrl) > 0) {
                std::string cmd = "am start -a android.intent.action.VIEW -d '"
                                  + std::string(updateUrl) + "' >/dev/null 2>&1";
                system(cmd.c_str());
            }
            exit(0);
        }
    }

    static void render_maintenance_content(float a, const char* msg) {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        ImDrawList* dl = w->DrawList;
        ImVec2 p = w->DC.CursorPos;
        float ww = content_w > 0 ? content_w : ImGui::GetContentRegionAvail().x;
        float ch = ImGui::GetContentRegionAvail().y;

        float centerX = p.x + ww * 0.5f;
        float centerY = p.y + ch * 0.5f;

        float boxW = ww - 60.f;
        float boxH = 300.f;
        ImVec2 tl(centerX - boxW * 0.5f, centerY - boxH * 0.5f);
        ImVec2 br(centerX + boxW * 0.5f, centerY + boxH * 0.5f);

        for (int i = 0; i < 12; ++i) {
            float off = 10.0f * i;
            float al = 50.0f / (i + 1);
            dl->AddRectFilled(
                ImVec2(tl.x - off, tl.y - off),
                ImVec2(br.x + off, br.y + off),
                IM_COL32(255, 0, 0, (int)al), 14.f);
        }

        dl->AddRectFilled(tl, br, IM_COL32(8, 8, 12, 245), 14.f);
        dl->AddRect(tl, br, IM_COL32(255, 30, 30, 255), 14.f, 0, 3.5f);

        ImFont* bold = ImGui::GetIO().Fonts->Fonts[0];
        const char* icon = "!";
        float iconSz = 60.0f;
        ImVec2 isz = ImGui::CalcTextSize(icon);
        dl->AddText(bold, iconSz,
            ImVec2(centerX - (isz.x * iconSz / ImGui::GetFontSize()) * 0.5f, tl.y + 25.f),
            IM_COL32(255, 60, 60, 255), icon);

        const char* title = "MAINTENANCE MODE";
        float titleSz = 38.0f;
        ImVec2 tsz = ImGui::CalcTextSize(title);
        dl->AddText(bold, titleSz,
            ImVec2(centerX - (tsz.x * titleSz / ImGui::GetFontSize()) * 0.5f, tl.y + 110.f),
            IM_COL32(255, 40, 40, 255), title);

        const char* sub = (msg && strlen(msg) > 0) ? msg : "Please try again later";
        float subSz = 22.0f;
        ImVec2 ssz = ImGui::CalcTextSize(sub);
        dl->AddText(bold, subSz,
            ImVec2(centerX - (ssz.x * subSz / ImGui::GetFontSize()) * 0.5f, tl.y + 175.f),
            IM_COL32(255, 200, 200, 255), sub);

        float btnW = 200.f, btnH = 55.f;
        ImVec2 btnTl(centerX - btnW * 0.5f, br.y - btnH - 25.f);
        ImVec2 btnBr(centerX + btnW * 0.5f, br.y - 25.f);

        ImVec2 mouse = ImGui::GetIO().MousePos;
        bool btnHover = mouse.x >= btnTl.x && mouse.x <= btnBr.x &&
                        mouse.y >= btnTl.y && mouse.y <= btnBr.y;

        ImU32 btnBg = btnHover ? IM_COL32(255, 50, 50, 255) : IM_COL32(180, 20, 20, 255);
        dl->AddRectFilled(btnTl, btnBr, btnBg, 10.f);
        dl->AddRect(btnTl, btnBr, IM_COL32(255, 100, 100, 255), 10.f, 0, 2.5f);

        const char* btnTxt = "EXIT";
        float btnFontSz = 30.0f;
        ImVec2 btsz = ImGui::CalcTextSize(btnTxt);
        dl->AddText(bold, btnFontSz,
            ImVec2(btnTl.x + (btnW - btsz.x * btnFontSz / ImGui::GetFontSize()) * 0.5f,
                   btnTl.y + (btnH - btsz.y * btnFontSz / ImGui::GetFontSize()) * 0.5f),
            IM_COL32(255, 255, 255, 255), btnTxt);

        if (btnHover && ImGui::IsMouseClicked(0)) {
            exit(0);
        }
    }

    static void render_alert_content(float a, const char* msg) {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        ImDrawList* dl = w->DrawList;
        ImVec2 p = w->DC.CursorPos;
        float ww = content_w > 0 ? content_w : ImGui::GetContentRegionAvail().x;
        float ch = ImGui::GetContentRegionAvail().y;

        float centerX = p.x + ww * 0.5f;
        float centerY = p.y + ch * 0.5f;

        float boxW = ww - 60.f;
        float boxH = 280.f;
        ImVec2 tl(centerX - boxW * 0.5f, centerY - boxH * 0.5f);
        ImVec2 br(centerX + boxW * 0.5f, centerY + boxH * 0.5f);

        for (int i = 0; i < 10; ++i) {
            float off = 10.0f * i;
            float al = 40.0f / (i + 1);
            dl->AddRectFilled(
                ImVec2(tl.x - off, tl.y - off),
                ImVec2(br.x + off, br.y + off),
                IM_COL32(255, 200, 0, (int)al), 14.f);
        }

        dl->AddRectFilled(tl, br, IM_COL32(10, 10, 8, 245), 14.f);
        dl->AddRect(tl, br, IM_COL32(255, 200, 0, 255), 14.f, 0, 3.5f);

        ImFont* bold = ImGui::GetIO().Fonts->Fonts[0];
        const char* icon = "!";
        float iconSz = 60.0f;
        ImVec2 isz = ImGui::CalcTextSize(icon);
        dl->AddText(bold, iconSz,
            ImVec2(centerX - (isz.x * iconSz / ImGui::GetFontSize()) * 0.5f, tl.y + 25.f),
            IM_COL32(255, 220, 60, 255), icon);

        const char* title = "ALERT";
        float titleSz = 44.0f;
        ImVec2 tsz = ImGui::CalcTextSize(title);
        dl->AddText(bold, titleSz,
            ImVec2(centerX - (tsz.x * titleSz / ImGui::GetFontSize()) * 0.5f, tl.y + 110.f),
            IM_COL32(255, 220, 60, 255), title);

        const char* sub = (msg && strlen(msg) > 0) ? msg : "Notification from server";
        float subSz = 22.0f;
        ImVec2 ssz = ImGui::CalcTextSize(sub);
        dl->AddText(bold, subSz,
            ImVec2(centerX - (ssz.x * subSz / ImGui::GetFontSize()) * 0.5f, tl.y + 175.f),
            IM_COL32(255, 255, 200, 255), sub);
    }

    template<typename F>
    static void render_special_modal(F&& func, const char* param) {
        ImVec2 wsz(620, 360);
        ImGui::SetNextWindowSize(wsz, ImGuiCond_Always);
        ImGui::SetNextWindowPos({(g_sw - wsz.x) * 0.5f, (g_sh - wsz.y) * 0.5f}, ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, {0, 0, 0, 0});
        ImGui::PushStyleColor(ImGuiCol_Border, {0, 0, 0, 0});
        ImGui::Begin("##special_modal", nullptr,
                     ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoMove);
        content_w = wsz.x;
        func(1.0f, param);
        ImGui::End();
        ImGui::PopStyleColor(2);
    }

    static void render_floating_logo() {
        if (bar::g_open) return;

        if (!g_LogoPosInit) {
            g_LogoPosInit = true;
            g_LogoPos = ImVec2(50.f, 200.f);
        }

        float radius = 32.f;
        ImVec2 center = {g_LogoPos.x + radius, g_LogoPos.y + radius};

        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* dl = ImGui::GetForegroundDrawList();

        static bool  dragging = false;
        static ImVec2 drag_offset(0, 0);

        ImVec2 mouse = io.MousePos;
        float dist = std::hypot(mouse.x - center.x, mouse.y - center.y);
        bool hovered = dist <= radius;

        if (hovered && io.MouseClicked[0]) {
            dragging = true;
            drag_offset = {mouse.x - g_LogoPos.x, mouse.y - g_LogoPos.y};
        }

        if (dragging) {
            if (io.MouseDown[0]) {
                g_LogoPos.x = mouse.x - drag_offset.x;
                g_LogoPos.y = mouse.y - drag_offset.y;
                g_LogoPos.x = ImClamp(g_LogoPos.x, 0.f, g_sw - radius * 2.f);
                g_LogoPos.y = ImClamp(g_LogoPos.y, 0.f, g_sh - radius * 2.f);
                center = {g_LogoPos.x + radius, g_LogoPos.y + radius};
            } else {
                dragging = false;
                if (dist <= radius) {
                    bar::toggle();
                }
            }
        }

        dl->AddCircleFilled(center, radius + 3.f, IM_COL32(0, 195, 255, 60), 48);
        dl->AddCircleFilled(center, radius, IM_COL32(14, 20, 30, 220), 48);
        dl->AddCircle(center, radius, IM_COL32(0, 195, 255, 220), 48, 2.5f);

        if (g_LogoLoaded && g_LogoTex != 0) {
            float img_r = radius * 0.75f;
            dl->AddImageRounded((ImTextureID)(intptr_t)g_LogoTex,
                {center.x - img_r, center.y - img_r},
                {center.x + img_r, center.y + img_r},
                {0, 0}, {1, 1}, IM_COL32(255, 255, 255, 255), img_r);
        }
    }

    // ============================================
    // Tab Contents (client.sh features + KernelSRC UI)
    // ============================================

    void Info_1() {
        wc::title("Account Info");

        char line1[96];
        snprintf(line1, sizeof(line1), "License Key : %s", cfg::account::license_key.c_str());
        ImGui::TextColored(ImColor(255,255,255,255), "%s", line1);

        char line2[160];
        snprintf(line2, sizeof(line2), "Seller      : %s", cfg::account::seller_name.c_str());
        ImGui::TextColored(ImColor(0,191,255,255), "%s", line2);

        char line3[96];
        snprintf(line3, sizeof(line3), "Expiry Date : %s", cfg::account::expiry_date.c_str());
        ImGui::TextColored(ImColor(255,220,0,255), "%s", line3);

        ImGui::TextColored(ImColor(200,200,200,255), "Status      : %s", cfg::account::status.c_str());
        ImGui::TextColored(ImColor(200,200,200,255), "Days Left   : %d", cfg::account::days_remain);
        ImGui::TextColored(ImColor(200,200,200,255), "Devices     : %d / %d",
                            cfg::account::devices_used, cfg::account::devices_max);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImColor(100,100,110,255), "LIB: 0x%lx  |  PID: %d", (unsigned long)proc::lib, proc::pid);
        ImGui::Text("FPS: %.1f  |  Frame Time: %.3fms", ImGui::GetIO().Framerate, ImGui::GetIO().DeltaTime * 1000.0f);
    }

    void Home_1() {
        ImVec4 oldBorderColor = ImGui::GetStyle().Colors[ImGuiCol_Border];
        ImGui::GetStyle().Colors[ImGuiCol_Border] = ImVec4(0, 0, 0, 1);

        ImGui::BeginChild("Home Info", ImVec2(ImGui::GetContentRegionAvail().x, 160), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::Text("            Disclaimer & Notice\n"
                    "  1. For educational and research use only\n"
                    "  2. Do not use for illegal purposes\n"
                    "  3. Developer takes no responsibility\n"
                    "  4. Have fun!");
        ImGui::EndChild();

        int play_count = 0, bot_count = 0;
        {
            std::lock_guard<std::mutex> lk(data::players_mutex);
            for (const auto& p : data::players) {
                if (p.isBot) bot_count++;
                else play_count++;
            }
        }

        ImGui::TextColored(ImColor(0, 191, 255, 255), "NEO KERNEL  -  Version: 2x2  (By @cache_file)");
        ImGui::TextColored(ImColor(100, 100, 110, 255), "LIB: 0x%lx  |  PID: %d", (unsigned long)proc::lib, proc::pid);

        ImGui::TextColored(ImColor(255, 60, 60, 255), "Enemies: %d", play_count);
        ImGui::SameLine(+180);
        ImGui::TextColored(ImColor(0, 220, 0, 255), "Bots: %d", bot_count);

        ImGui::GetStyle().Colors[ImGuiCol_Border] = oldBorderColor;

        ImGui::Text("FPS: %.1f  |  Frame Time: %.3fms", ImGui::GetIO().Framerate, ImGui::GetIO().DeltaTime * 1000.0f);
        ImGui::Text("Resolution: %.0fx%.0f", g_sw, g_sh);
        ImGui::TextColored(ImColor(0, 220, 0, 255), "Driver: Hardware Root BP  <Driver OK>");
    }

    void Home_2() {
        wc::title("Record Section");

        static float record_duration = 30.0f;
        static std::atomic<bool> isRecording(false);
        static float lp_beg = 0.0f, lp_end = 0.0f;
        static bool isTimerRunning = false;
        static int timerStartTime = 0;

        lp_beg += (lp_end - lp_beg) * (0.2f * wc::anim_speed);

        wc::SliderFloat("Custom Record Duration (s)", &record_duration, 1, 120, "%.0f");
        ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing();

        wc::Line(400);
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() - lp_beg, ImGui::GetCursorPosY()));
        if (wc::button("Start Recording", ImVec2(400, 60))) {
            lp_end = 430;
            isTimerRunning = true;
            timerStartTime = static_cast<int>(ImGui::GetTime());
            isRecording.store(true);
            std::thread([](int dur) {
                std::string cmd = "screenrecord --time-limit " + std::to_string(dur) + " /sdcard/Record.mp4 &";
                system(cmd.c_str());
            }, (int)record_duration).detach();
        }

        if (isTimerRunning && (ImGui::GetTime() - timerStartTime >= record_duration)) {
            lp_end = 0;
            isTimerRunning = false;
            system("pkill -l SIGINT screenrecord");
        }

        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 430 - lp_beg, ImGui::GetCursorPosY() - 72.5f));
        if (wc::button("Stop Recording", ImVec2(400, 60))) {
            lp_end = 0;
            system("pkill -l SIGINT screenrecord");
            isRecording.store(false);
            isTimerRunning = false;
        }
        wc::Line(400);

        if (wc::button("Unlimited Duration", ImVec2(400, 60))) {
            record_duration = 9999;
        }
        wc::Line(400);
        if (wc::button("Screenshot", ImVec2(400, 60))) {
            system("screencap -p /sdcard/Screenshot.png");
        }
        wc::Line(400);
    }

    void Render_1() {
        wc::title("ESP Display Elements");
        wc::Checkbox("Esp Box", &cfg::esp::box);
        ImGui::SameLine();
        wc::Checkbox("Esp Line", &cfg::esp::line);
        ImGui::SameLine();
        wc::Checkbox("Esp Name", &cfg::esp::name);

        wc::Checkbox("Health Bar", &cfg::esp::health);
        ImGui::SameLine();
        wc::Checkbox("Distance", &cfg::esp::distance);
        ImGui::SameLine();
        wc::Checkbox("Skeleton", &cfg::esp::skeleton);

        wc::Checkbox("Player Info", &cfg::esp::info_bar);
        ImGui::SameLine();
        wc::Checkbox("Weapon In Hand", &cfg::esp::weapon);
        ImGui::SameLine();
        wc::Checkbox("Bot Tag", &cfg::esp::bot_tag);

        wc::Checkbox("Crosshair", &cfg::esp::crosshair);
        ImGui::SameLine();
        wc::Checkbox("FOV Circle", &cfg::aim::fov_circle);
    }

    void Render_2() {
        wc::title("Box Type & Appearance");
        std::vector<std::string> box_styles = {"Corner Box", "Full Box"};
        static float box_type_f = (float)cfg::esp::box_type;
        if (box_type_f > 1.0f) box_type_f = 0.0f;
        wc::SlidingSelector(box_styles, box_type_f, false, 180.0f, 48.0f);
        cfg::esp::box_type = (int)box_type_f;

        // Color box -- ported from InvisibleX's hook.h (BoxCol modes).
        // Mode 0 (Fixed) is the default and keeps the exact same colors
        // this project already used (box_col / box_visible below).
        ImGui::Spacing();
        ImGui::Text("Box Color Mode:");
        std::vector<std::string> color_modes = {"Fixed", "HP", "Distance", "Rainbow"};
        static float color_mode_f = (float)cfg::esp::box_color_mode;
        wc::SlidingSelector(color_modes, color_mode_f, false, 100.0f, 42.0f);
        cfg::esp::box_color_mode = (int)color_mode_f;

        if (cfg::esp::box_color_mode == 0) {
            ImGui::ColorEdit4("Box Color (hidden)", (float*)&cfg::esp::box_col,
                              ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit4("Box Color (visible)", (float*)&cfg::esp::box_visible,
                              ImGuiColorEditFlags_NoInputs);
        }
    }

    void Render_4() {
        wc::title("Visual Parameters");
        wc::SliderFloat("Box Thickness", &cfg::esp::box_thickness, 0.5f, 4.0f, "%.1f px");
        wc::SliderFloat("ESP Max Range", &cfg::esp::max_range, 50.0f, 500.0f, "%.0f m");
    }

    void Aimbot_1() {
        wc::title("HWBP Silent Aim Engine");
        auto& engine = hwbp_aim::controller();
        auto settings = engine.settings();
        const auto status = engine.status();
        bool changed = false;

        changed |= wc::Checkbox("Aimbot", &settings.x);
        ImGui::SameLine();
        std::vector<std::string> x_opts = {"Opt A", "Opt B"};
        float x_opt_f = (float)settings.x_option;
        wc::SlidingSelector(x_opts, x_opt_f, false, 90.0f, 38.0f);
        if ((int)x_opt_f != settings.x_option) {
            settings.x_option = (int)x_opt_f;
            changed = true;
        }
        ImGui::TextDisabled("  Status Aimbot: %s", status.x.c_str());

        changed |= wc::Checkbox("HWBP Y", &settings.y);
        ImGui::SameLine();
        std::vector<std::string> y_opts = {"Opt A", "Opt B"};
        float y_opt_f = (float)settings.y_option;
        wc::SlidingSelector(y_opts, y_opt_f, false, 90.0f, 38.0f);
        if ((int)y_opt_f != settings.y_option) {
            settings.y_option = (int)y_opt_f;
            changed = true;
        }
        ImGui::TextDisabled("  Status Y: %s", status.y.c_str());

        changed |= wc::Checkbox("Fire Check", &settings.fire_check);
        ImGui::SameLine();
        changed |= wc::Checkbox("Draw FOV Circle", &settings.draw_fov);

        if (changed) {
            engine.configure(settings);
        }
    }

    void Aimbot_3() {
        wc::title("Target Selection & Diagnostics");
        auto& engine = hwbp_aim::controller();
        auto settings = engine.settings();
        const auto status = engine.status();
        bool changed = false;

        ImGui::Text("Target Bone:");
        std::vector<std::string> bones = {"Head", "Neck", "Chest"};
        float bone_f = (float)settings.bone;
        wc::SlidingSelector(bones, bone_f, false, 110.0f, 42.0f);
        if ((int)bone_f != settings.bone) {
            settings.bone = (int)bone_f;
            cfg::aim::target_position = (int)bone_f;
            changed = true;
        }

        ImGui::Spacing();
        wc::Checkbox("Ignore Knocked", &cfg::aim::target_knocked);
        ImGui::SameLine();
        wc::Checkbox("Scope Safeguard", &cfg::aim::scope_safeguard);

        ImGui::Spacing();
        ImGui::TextDisabled("Engine Status: %s", status.text.c_str());
        ImGui::Text(driver && !driver->breakpoint_has_total_hits()
                    ? "Records: X=%llu | Y=%llu"
                    : "Hits: X=%llu | Y=%llu",
                    (unsigned long long)status.x_hits, (unsigned long long)status.y_hits);

        static std::string saved_report;
        if (wc::button("Save Report", ImVec2(150, 40))) {
            saved_report = engine.save();
        }
        if (!saved_report.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImColor(0, 180, 0, 255), "%s", saved_report.c_str());
        }

        if (changed) {
            engine.configure(settings);
        }
    }

    void Aimbot_2() {
        wc::title("HWBP Parameters");
        auto& engine = hwbp_aim::controller();
        auto settings = engine.settings();
        bool changed = false;

        changed |= wc::SliderFloat("FOV Radius (px)", &settings.fov, 30.0f, 500.0f, "%.0f px");
        changed |= wc::SliderFloat("Target Max Range", &settings.range, 50.0f, 500.0f, "%.0f m");
        changed |= wc::SliderFloat("Tracking Lead (ms)", &settings.lead_ms, 0.0f, 100.0f, "%.0f ms");

        if (changed) {
            engine.configure(settings);
        }

    }

    void Items_1() {
        wc::title("Items Section");
        static bool dummy_med = false, dummy_arm = false, dummy_wep = false;
        wc::Checkbox("Medical ESP", &dummy_med);
        ImGui::SameLine();
        wc::Checkbox("Armor & Helmet", &dummy_arm);
        ImGui::SameLine();
        wc::Checkbox("Weapon ESP", &dummy_wep);

        static bool dummy_crate = false, dummy_drop = false, dummy_veh = false;
        wc::Checkbox("Death Crate", &dummy_crate);
        ImGui::SameLine();
        wc::Checkbox("Airdrop ESP", &dummy_drop);
        ImGui::SameLine();
        wc::Checkbox("Vehicle ESP", &dummy_veh);
    }

    void Items_2() {
        wc::title("Item Filter");
        std::vector<std::string> item_level = {"All", "Level 1", "Level 2", "Level 3"};
        static float item_level_f = 0.0f;
        wc::SlidingSelector(item_level, item_level_f, false, 120.0f, 45.0f);
    }

    void settings_wc_1() {
        wc::title("Thermal Pacing (FPS Limit)");
        std::vector<std::string> fps_choices = {"60 FPS", "90 FPS", "120 FPS", "Max"};
        float fps_f = (float)cfg::settings::fps_choice;
        wc::SlidingSelector(fps_choices, fps_f, false, 140.0f, 45.0f);
        cfg::settings::fps_choice = (int)fps_f;

        static float s_cfg_status_timer = 0.f;
        static std::string s_cfg_status_msg = "";
        if (s_cfg_status_timer > 0.0f) {
            s_cfg_status_timer -= ImGui::GetIO().DeltaTime;
            ImGui::TextColored(ImColor(0, 200, 0, 255), "%s", s_cfg_status_msg.c_str());
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20);
        if (wc::button("Save Config", ImVec2(ImGui::GetContentRegionAvail().x, 65), 15.0f, ImVec4(0, 0, 0, 0), ImVec4(0.25f, 0.25f, 0.25f, 1.0f))) {
            if (cfg::save_config()) {
                s_cfg_status_msg = "[+] Config saved to /data/local/tmp/neoFF.c";
                s_cfg_status_timer = 3.5f;
            } else {
                s_cfg_status_msg = "[-] Failed to save config!";
                s_cfg_status_timer = 3.5f;
            }
        }
        ImGui::Spacing();
        if (wc::button("Load Config", ImVec2(ImGui::GetContentRegionAvail().x, 65), 15.0f, ImVec4(0, 0, 0, 0), ImVec4(0.25f, 0.25f, 0.25f, 1.0f))) {
            if (cfg::load_config()) {
                s_cfg_status_msg = "[+] Config loaded from /data/local/tmp/neoFF.c";
                s_cfg_status_timer = 3.5f;
            } else {
                s_cfg_status_msg = "[-] Failed to load config!";
                s_cfg_status_timer = 3.5f;
            }
        }
        ImGui::Spacing();
        if (wc::button("Exit App", ImVec2(ImGui::GetContentRegionAvail().x, 65), 15.0f, ImVec4(0.2f, 0.0f, 0.0f, 0.1f), ImVec4(0.8f, 0.2f, 0.2f, 1.0f))) {
            exit(0);
        }
    }

    void settings_wc_2() {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5);
        ImGui::Spacing();
        ImGui::BeginChild("Palette Child", ImVec2(0, 160), true);
        {
            wc::PaletteButton("Box Color", cfg::esp::box_col, 1);
            ImGui::SameLine(+265);
            wc::PaletteButton("Visible Box", cfg::esp::box_visible, 2);
            ImGui::SameLine(+520);
            wc::PaletteButton("FOV Circle", cfg::aim::fov_col, 3);
            ImGui::EndChild();
        }

        if (wc::currentSelectedVec4) {
            ImGui::TextColored(ImColor(0, 0, 0), "RGB Channel Intensity");
            wc::RGBSliderFloat1("Red Intensity", &wc::R_col, 0.0f, 255.0f, "%.0f");
            wc::RGBSliderFloat("Green Intensity", &wc::G_col, 0.0f, 255.0f, "%.0f");
            wc::RGBSliderFloat("Blue Intensity", &wc::B_col, 0.0f, 255.0f, "%.0f");
            wc::SliderFloat("Alpha", &wc::A_col, 0.0f, 255.0f, "%.0f");

            wc::currentSelectedVec4->x = wc::R_col / 255.0f;
            wc::currentSelectedVec4->y = wc::G_col / 255.0f;
            wc::currentSelectedVec4->z = wc::B_col / 255.0f;
            wc::currentSelectedVec4->w = wc::A_col / 255.0f;
        }
    }

    void settings_wc_3() {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5);
        wc::SliderFloat("Global Animation Speed", &wc::anim_speed, 0.1f, 3.0f, "%.1f");
        wc::SliderFloat("Dynamic Island Scale", &wc::island_size, 0.5f, 2.0f, "%.2f");
    }

    void settings_wc_4() {
        wc::title("Device & Kernel Info");
        ImGui::Text("Screen Resolution: %.0fx%.0f", g_sw, g_sh);
        ImGui::Text("Renderer: OpenGL ES 3.0 / EGL");
        ImGui::Text("Driver Type: Root BP Controller");
    }

    void settings_wc_5() {
        wc::title("KernelSRC Status");
        ImGui::TextColored(ImColor(0, 191, 255, 255), "Hardware BT + KernelSRC UI Unified Client");
        ImGui::Text("Version: 2.0 Hybrid");
        ImGui::Text("Status: All systems operational");
    }

    void settings_wcPage() {
        std::vector<std::string> settings_pages = {"General Settings", "Color Settings", "Animation Settings", "Misc Settings", "Update Panel"};
        wc::SlidingSelector(settings_pages, setting_tab, false, 180.0f, 50.0f);

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
        ImGui::BeginChild("Settings SubContent", ImVec2(0, 0), false);
        switch ((int)setting_tab) {
            case 0: settings_wc_1(); break;
            case 1: settings_wc_2(); break;
            case 2: settings_wc_3(); break;
            case 3: settings_wc_4(); break;
            case 4: settings_wc_5(); break;
        }
        ImGui::EndChild();
    }

    // ============================================
    // KernelSRC Rendering (Stable Tab Switching)
    // ============================================

    void Rendering(bool* flag) {
        wc::window_set();

        float window_x = ImMin(1850.0f, g_sw > 100.0f ? (g_sw - 40.0f) : 1850.0f);
        float window_y = ImMin(700.0f,  g_sh > 100.0f ? (g_sh - 40.0f) : 700.0f);

        wc::WidgetState& win_x = wc::wh["window_x"];
        wc::WidgetState& win_y = wc::wh["window_y"];
        wc::WidgetState& pos_x = wc::wh["pos_x"];
        wc::WidgetState& pos_y = wc::wh["pos_y"];

        if (bar::g_open) {
            win_x.end = window_x;
            win_y.end = window_y;
            pos_x.end = (g_sw * 0.5f) - (window_x * 0.5f);
            pos_y.end = (g_sh * 0.5f) - (window_y * 0.5f);
            menu_window_open = true;
        } else {
            win_x.end = 80.0f;
            win_y.end = 80.0f;
            pos_x.end = (g_sw * 0.5f) - 40.0f;
            pos_y.end = 33.0f;
            menu_window_open = false;
        }

        win_x.beg += (win_x.end - win_x.beg) * (0.15f * wc::anim_speed);
        win_y.beg += (win_y.end - win_y.beg) * (0.15f * wc::anim_speed);
        pos_x.beg += (pos_x.end - pos_x.beg) * (0.15f * wc::anim_speed);
        pos_y.beg += (pos_y.end - pos_y.beg) * (0.15f * wc::anim_speed);

        if (!bar::g_open && win_x.beg < 120.0f) {
            return;
        }

        ImGui::SetNextWindowPos(ImVec2(pos_x.beg, pos_y.beg), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(window_x, window_y), ImGuiCond_Always);

        wc::zhu_set();
        ImGuiWindowFlags win_flags = ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoDecoration |
                                     ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoScrollWithMouse;

        ImGui::Begin("Menu", &menu_window_open, win_flags);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        float aevrage_now = window_x / 6.302521f;

        if (menu_window_open && bar::g_open) {
            ImGui::SetCursorPos(ImVec2(15, 15));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 255, 255, 255));
            ImGui::BeginChild("Left Title", ImVec2(aevrage_now, window_y - 30.0f), true);

            if (g_LogoLoaded && g_LogoTex != 0) {
                float img_r = 45.0f;
                ImVec2 center = {ImGui::GetWindowPos().x + aevrage_now * 0.5f, ImGui::GetWindowPos().y + 65.0f};
                ImGui::GetWindowDrawList()->AddImageRounded(
                    (ImTextureID)(intptr_t)g_LogoTex,
                    {center.x - img_r, center.y - img_r},
                    {center.x + img_r, center.y + img_r},
                    {0, 0}, {1, 1}, IM_COL32(255, 255, 255, 255), img_r
                );
                ImGui::SetCursorPosY(115.0f);
            } else {
                ImGui::SetCursorPosY(20.0f);
                ImGui::TextColored(ImColor(30, 144, 255, 255), "   NEO KERNEL");
                ImGui::SetCursorPosY(60.0f);
            }

            ImGui::Separator();

            if (wc::button("Home", ImVec2(ImGui::GetContentRegionAvail().x, 81))) Tab = 0;
            ImGui::Separator();

            if (wc::button("Visuals", ImVec2(ImGui::GetContentRegionAvail().x, 81))) Tab = 1;
            ImGui::Separator();

            if (wc::button("Aimbot", ImVec2(ImGui::GetContentRegionAvail().x, 81))) Tab = 2;
            ImGui::Separator();

            if (wc::button("Items", ImVec2(ImGui::GetContentRegionAvail().x, 81))) Tab = 3;
            ImGui::Separator();

            if (wc::button("Settings", ImVec2(ImGui::GetContentRegionAvail().x, 81))) Tab = 4;
            ImGui::Separator();

            if (wc::button("Info", ImVec2(ImGui::GetContentRegionAvail().x, 81))) Tab = 5;

            ImGui::EndChild();
            ImGui::PopStyleColor(); // child bg

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

            switch (Tab) {
                case 0: { // Home
                    ImGui::SetCursorPos(ImVec2(aevrage_now + 30, 15));
                    ImGui::BeginChild("Home Row 1 Col 1", ImVec2(aevrage_now * 3, window_y - 30.0f), true);
                    Home_1();
                    ImGui::EndChild();

                    ImGui::SetCursorPos(ImVec2(aevrage_now * 4 + 45, 15));
                    ImGui::BeginChild("Home Row 1 Col 2", ImVec2(aevrage_now * 2, window_y - 30.0f), true);
                    Home_2();
                    ImGui::EndChild();
                    break;
                }
                case 1: { // Visuals
                    ImGui::SetCursorPos(ImVec2(aevrage_now + 30, 15));
                    ImGui::BeginChild("Visuals Row 1 Col 1", ImVec2(aevrage_now * 3, window_y - 30.0f), true);
                    Render_1();
                    ImGui::EndChild();

                    ImGui::SetCursorPos(ImVec2(aevrage_now * 4 + 45, 15));
                    ImGui::BeginChild("Visuals Row 1 Col 2", ImVec2(aevrage_now * 2, 260), true);
                    Render_2();
                    ImGui::EndChild();

                    ImGui::SetCursorPos(ImVec2(aevrage_now * 4 + 45, 290));
                    ImGui::BeginChild("Visuals Row 2 Col 2", ImVec2(aevrage_now * 2, window_y - 305.0f), true);
                    Render_4();
                    ImGui::EndChild();
                    break;
                }
                case 2: { // Aimbot
                    ImGui::SetCursorPos(ImVec2(aevrage_now + 30, 15));
                    ImGui::BeginChild("Aimbot Row 1 Col 1", ImVec2(aevrage_now * 3, 310.0f), true);
                    Aimbot_1();
                    ImGui::EndChild();

                    ImGui::SetCursorPos(ImVec2(aevrage_now * 4 + 45, 15));
                    ImGui::BeginChild("Aimbot Row 1 Col 2", ImVec2(aevrage_now * 2, window_y - 30.0f), true);
                    Aimbot_2();
                    ImGui::EndChild();

                    ImGui::SetCursorPos(ImVec2(aevrage_now + 30, 340));
                    ImGui::BeginChild("Aimbot Row 2 Col 1", ImVec2(aevrage_now * 3, window_y - 355.0f), true);
                    Aimbot_3();
                    ImGui::EndChild();
                    break;
                }
                case 3: { // Items
                    ImGui::SetCursorPos(ImVec2(aevrage_now + 30, 15));
                    ImGui::BeginChild("Items Row 4 Col 1", ImVec2(aevrage_now * 3, window_y - 30.0f), true);
                    Items_1();
                    ImGui::EndChild();

                    ImGui::SetCursorPos(ImVec2(aevrage_now * 4 + 45, 15));
                    ImGui::BeginChild("Items Row 4 Col 2", ImVec2(aevrage_now * 2, window_y - 30.0f), true);
                    Items_2();
                    ImGui::EndChild();
                    break;
                }
                case 4: { // Settings
                    ImGui::SetCursorPos(ImVec2(aevrage_now + 30, 15));
                    ImGui::BeginChild("Settings Child", ImVec2(aevrage_now * 5 + 15, window_y - 30.0f), true);
                    settings_wcPage();
                    ImGui::EndChild();
                    break;
                }
                case 5: { // Info
                    ImGui::SetCursorPos(ImVec2(aevrage_now + 30, 15));
                    ImGui::BeginChild("Info Child", ImVec2(aevrage_now * 5 + 15, window_y - 30.0f), true);
                    Info_1();
                    ImGui::EndChild();
                    break;
                }
            }

            ImGui::PopStyleColor(); // child bg
        }
        ImGui::End();
    }

    // ============================================
    // Main UI Render Entry Point
    // ============================================

    void render() {
        const auto hwSettings = hwbp_aim::controller().settings();
        if (hwSettings.draw_fov && (hwSettings.x || hwSettings.y)) {
            ImGui::GetBackgroundDrawList()->AddCircle(
                ImVec2(g_sw * 0.5f, g_sh * 0.5f), hwSettings.fov,
                IM_COL32(0, 195, 255, 180), 96, 1.5f);
        }

        static bool s_checked = false;
        if (!s_checked) {
            s_checked = true;
            bool allowed = IsDataHackAllowed();
            cfg::status::is_allowed = allowed;
            cfg::status::is_expired = !allowed;
            cfg::status::msg_timer = 0.f;
        }

        static bool s_cfg_loaded = false;
        if (!s_cfg_loaded) {
            s_cfg_loaded = true;
            cfg::load_config();
        }

        int controlState = GetControlState();
        const char* ctrlMsg = GetControlMessage();

        const char* redirectUrl = GetRedirectURL();
        if (redirectUrl && strlen(redirectUrl) > 0) {
            static bool redirectDone = false;
            if (!redirectDone) {
                redirectDone = true;
                std::string cmd = "am start -a android.intent.action.VIEW -d '"
                                  + std::string(redirectUrl) + "' >/dev/null 2>&1";
                system(cmd.c_str());
            }
        }

        static float crashTimer = 0.f;
        if (controlState == CS_CRASH) {
            crashTimer += ImGui::GetIO().DeltaTime;
            if (crashTimer > 2.0f) {
                exit(0);
            }
        } else {
            crashTimer = 0.f;
        }

        if (controlState == CS_EXIT) {
            exit(0);
        }

        bool normalMode = (controlState == CS_LIVE ||
                           controlState == CS_NONE ||
                           controlState == CS_REDIRECT);
        if (!cfg::status::is_expired && normalMode) {
            cfg::status::msg_timer += ImGui::GetIO().DeltaTime;
        }

        if (!cfg::status::is_expired && normalMode) {
            func::esp::draw();
            silent::draw_overlay();
        }

        // Render Dynamic Island Pill (top of screen)
        bar::render();

        if (!g_LogoLoaded) {
            int w, h;
            if (ui::image::LoadTextureFromMemory(logo1_data.data(), logo1_data.size(), &g_LogoTex, &w, &h)) {
                g_LogoLoaded = true;
            }
        }

        render_floating_logo();

        // Check for special startup/server status screens
        if (controlState == CS_CRASH) {
            render_special_modal([](float a, const char* m){ render_crash_content(a, m); }, ctrlMsg);
            return;
        } else if (controlState == CS_UPDATE) {
            render_special_modal([](float a, const char* u){ render_update_content(a, u); }, GetUpdateURL());
            return;
        } else if (controlState == CS_MAINTENANCE) {
            render_special_modal([](float a, const char* m){ render_maintenance_content(a, m); }, ctrlMsg);
            return;
        } else if (controlState == CS_ALERT) {
            render_special_modal([](float a, const char* m){ render_alert_content(a, m); }, ctrlMsg);
            return;
        } else if (cfg::status::is_expired) {
            render_special_modal([](float a, const char*){ render_expired_content(a); }, nullptr);
            return;
        } else if (cfg::status::msg_timer < 3.0f) {
            render_special_modal([](float a, const char*){ render_all_okey_content(a); }, nullptr);
            return;
        }

        // Render KernelSRC main menu
        Rendering(nullptr);
    }
}
