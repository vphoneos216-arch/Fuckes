#include "main.h"
#include "login.h"
#include <string>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <linux/input.h>
#include <poll.h>
#include <sys/stat.h>
#include "Scarecrow/hook.h"
#include "Scarecrow/oxorany_include.h"
#include "Scarecrow/oxorany.h"
//#include "Scarecrow/oxorany.h"

using namespace std;
using namespace ImGui;

// ==================== GLOBALS ====================
static Authentication g_auth;
static ImVec4 uiGlowColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
static float uiGlowIntensity = 0.0f;
static float uiBgOpacity = 0.95f;
static bool g_MenuMinimized = true;
static bool g_GameIsRunning = false;

// ========== FLOATING SYSTEM STATE ==========
static bool g_FloatingSystemReady = false;
static bool g_FloatingSystemInit = false;

// ========== ORIENTATION VARIABLES ==========
static bool g_IsLandscape = false;
static bool g_Landscape2SecDone = false;
static std::chrono::steady_clock::time_point g_LandscapeDetectTime;
static bool g_LandscapeTimerActive = false;

static int currentTab = 0;
static auto lastVolumePressTime = std::chrono::steady_clock::now();

struct SwitchState {
    float knobPosition = 0.0f;
    std::chrono::steady_clock::time_point lastUpdate;
    bool isAnimating = false;
    bool targetState = false;
};
static std::unordered_map<ImGuiID, SwitchState> g_SwitchStates;

// ==================== CHECK SCREEN ORIENTATION ====================
bool IsScreenLandscape() {
    screen_config();
    return (displayInfo.width > displayInfo.height);
}

// ==================== VOLUME KEY ====================
void VolumeKeyThread() {
    struct pollfd fds[20]; int nfds = 0;
    for (int i = 0; i < 20; i++) {
        char p[32]; snprintf(p, sizeof(p), "/dev/input/event%d", i);
        int fd = open(p, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) { fds[nfds].fd = fd; fds[nfds].events = POLLIN; nfds++; }
    }
    while (main_thread_flag) {
        if (nfds == 0) { std::this_thread::sleep_for(std::chrono::seconds(1)); continue; }
        if (poll(fds, nfds, 200) > 0) {
            for (int i = 0; i < nfds; i++) {
                if (fds[i].revents & POLLIN) {
                    struct input_event ev;
                    while (read(fds[i].fd, &ev, sizeof(ev)) > 0) {
                        if (ev.type == EV_KEY && ev.code == 115 && ev.value == 1) {
                            if (!g_FloatingSystemReady || !g_Landscape2SecDone) continue;
                            
                            auto now = std::chrono::steady_clock::now();
                            auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastVolumePressTime).count();
                            if (dur > 400) g_MenuMinimized = !g_MenuMinimized;
                            lastVolumePressTime = now;
                        }
                    }
                }
            }
        }
    }
    for (int i = 0; i < nfds; i++) close(fds[i].fd);
}

// ==================== GLOW TEXT HELPER ====================
void DrawGlowText(ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text, float glowIntensity = 1.5f) {
    if (!dl || !text) return;
    // Glow layers
    for (int i = 3; i >= 1; i--) {
        float alpha = (glowIntensity * 0.25f) / (float)i;
        ImU32 glowCol = IM_COL32(
            (int)(uiGlowColor.x * 255 * 0.6f),
            (int)(uiGlowColor.y * 255 * 0.6f),
            (int)(uiGlowColor.z * 255 * 0.6f),
            (int)(alpha * 255)
        );
        dl->AddText(ImVec2(pos.x - i, pos.y - i), glowCol, text);
        dl->AddText(ImVec2(pos.x + i, pos.y - i), glowCol, text);
        dl->AddText(ImVec2(pos.x - i, pos.y + i), glowCol, text);
        dl->AddText(ImVec2(pos.x + i, pos.y + i), glowCol, text);
    }
    // Main text
    dl->AddText(pos, color, text);
}

void DrawGlowTextColored(ImDrawList* dl, ImVec2 pos, ImVec4 textColor, const char* text) {
    if (!dl || !text) return;
    for (int i = 3; i >= 1; i--) {
        float alpha = (0.4f) / (float)i;
        ImU32 glowCol = IM_COL32(
            (int)(uiGlowColor.x * 255),
            (int)(uiGlowColor.y * 255),
            (int)(uiGlowColor.z * 255),
            (int)(alpha * 255)
        );
        dl->AddText(ImVec2(pos.x - i, pos.y), glowCol, text);
        dl->AddText(ImVec2(pos.x + i, pos.y), glowCol, text);
        dl->AddText(ImVec2(pos.x, pos.y - i), glowCol, text);
        dl->AddText(ImVec2(pos.x, pos.y + i), glowCol, text);
    }
    dl->AddText(pos, ImGui::ColorConvertFloat4ToU32(textColor), text);
}

// ==================== DRAW GLOW (Rectangle) ====================
void DrawGlow(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImVec4 c, float r, float i, int l = 2) {
    if (!dl || i <= 0.05f) return;
    for (int j = 1; j <= l; ++j) {
        float a = (i * 0.3f) / (float)j;
        dl->AddRect(mn, mx, ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, a)), r + (float)j, 0, 1.0f + (float)j);
    }
}

// FIXED: Panel with glow text title
void BeginModernPanel(const char* id, const char* title, ImVec2 sz) {
    ImGui::BeginChild(id, sz, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize);
    ImVec2 mn = ImGui::GetWindowPos(), mx = ImVec2(mn.x + ImGui::GetWindowWidth(), mn.y + ImGui::GetWindowHeight());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (dl) {
        // Darker background with subtle gradient
        dl->AddRectFilled(mn, mx, IM_COL32(12, 12, 15, 245), 10.0f);
        // Top subtle highlight
        dl->AddRectFilled(mn, ImVec2(mx.x, mn.y + 2), IM_COL32(255, 255, 255, 8), 10.0f, ImDrawFlags_RoundCornersTop);
        // Glow border
        DrawGlow(dl, mn, mx, uiGlowColor, 10.0f, uiGlowIntensity * 0.15f, 3);
        // Title line - thoda niche
        dl->AddLine(ImVec2(mn.x + 10, mn.y + 52), ImVec2(mx.x - 10, mn.y + 52), IM_COL32(35, 35, 40, 255), 1.5f);
    }
    ImGui::SetCursorPos(ImVec2(15, 18));
    // FIXED: Glow title text
    if (dl) {
        dl->AddText(ImVec2(mn.x + 15, mn.y + 16), ImGui::ColorConvertFloat4ToU32(uiGlowColor), title);
    }
    ImGui::SetCursorPos(ImVec2(15, 68));
    ImGui::BeginGroup();
}
void EndModernPanel() { ImGui::EndGroup(); ImGui::EndChild(); }

// FIXED: Toggle with glow label
/*bool ModernToggle(const char* label, bool* v) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (!w || w->SkipItems) return false;
    const ImGuiID id = w->GetID(label);
    const ImVec2 ls = ImGui::CalcTextSize(label, NULL, true);
    float h = ImGui::GetFrameHeight() * 0.85f, wd = h * 1.8f;
    const ImVec2 pos = w->DC.CursorPos;
    ImRect bb(pos, ImVec2(pos.x + ImGui::GetWindowWidth() - 30.0f, pos.y + h));
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id)) return false;
    
    auto& st = g_SwitchStates[id];
    auto now = std::chrono::steady_clock::now();
    if (st.lastUpdate.time_since_epoch().count() == 0) { 
        st.knobPosition = *v ? 1.0f : 0.0f; st.targetState = *v; st.lastUpdate = now; 
    }
    
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) { *v = !(*v); st.targetState = *v; st.isAnimating = true; st.lastUpdate = now; }
    
    if (st.isAnimating) {
        auto el = std::chrono::duration_cast<std::chrono::milliseconds>(now - st.lastUpdate).count();
        float step = el / 150.0f;
        if (step >= 1.0f) { st.knobPosition = st.targetState ? 1.0f : 0.0f; st.isAnimating = false; }
        else { 
            float tgt = st.targetState ? 1.0f : 0.0f; 
            st.knobPosition += (tgt - st.knobPosition) * step;
            if (st.knobPosition > 1.0f) st.knobPosition = 1.0f; 
            if (st.knobPosition < 0.0f) st.knobPosition = 0.0f; 
        }
    }
    
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (dl) {
        float sx = bb.Max.x - wd - 10.0f;
        ImVec2 mn(sx, pos.y), mx(sx + wd, pos.y + h);
        
        // FIXED: Darker track for better visibility
        ImU32 bgc = IM_COL32(40, 40, 46, 255);
        if (st.knobPosition > 0.5f) {
            float t = (st.knobPosition - 0.5f) * 2.0f;
            bgc = ImGui::ColorConvertFloat4ToU32(ImVec4(
                uiGlowColor.x * t + 0.2f * (1.0f - t), 
                uiGlowColor.y * t + 0.2f * (1.0f - t), 
                uiGlowColor.z * t + 0.25f * (1.0f - t), 
                1.0f));
        }
        float r = h * 0.5f;
        // Track shadow
        dl->AddRectFilled(ImVec2(mn.x, mn.y + 1), ImVec2(mx.x, mx.y + 1), IM_COL32(0, 0, 0, 60), r);
        dl->AddRectFilled(mn, mx, bgc, r);
        
        // Knob with glow
        float ks = h * 0.75f, kp = (h - ks) * 0.5f;
        float kx = mn.x + kp + (mx.x - mn.x - ks - kp * 2) * st.knobPosition;
        ImVec2 knobCenter(kx + ks * 0.5f, mn.y + h * 0.5f);
        
        // Knob glow
        if (st.knobPosition > 0.1f) {
            dl->AddCircleFilled(knobCenter, ks * 0.7f, ImGui::ColorConvertFloat4ToU32(ImVec4(uiGlowColor.x, uiGlowColor.y, uiGlowColor.z, 0.3f * st.knobPosition)));
        }
        dl->AddCircleFilled(knobCenter, ks * 0.5f, IM_COL32(255, 255, 255, 255));
    }
    
    // FIXED: Glow text label
    ImVec2 textPos(pos.x, pos.y + (h - ls.y) * 0.5f);
    if (dl) {
        dl->AddText(textPos, IM_COL32(220, 220, 225, 255), label);
    }
    
    return pressed;
}*/



bool ModernToggle(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

    float height = ImGui::GetFrameHeight() * 0.8f;
    float width = height * 2.0f;
    const ImVec2 pos = window->DC.CursorPos;

    ImRect total_bb(pos, ImVec2(pos.x + ImGui::GetWindowWidth() - 30.0f, pos.y + height + style.ItemInnerSpacing.y));
    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);
    if (pressed) {
        *v = !(*v);
        ImGui::MarkItemEdited(id);
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // Align toggle to right
    float toggle_x = total_bb.Max.x - width - 10.0f;
    ImVec2 p_min = ImVec2(toggle_x, pos.y + (height * 0.1f));
    ImVec2 p_max = ImVec2(toggle_x + width, pos.y + height - (height * 0.1f));

    // Colors
    ImU32 bg_col = *v ? ImGui::ColorConvertFloat4ToU32(uiGlowColor) : IM_COL32(40, 40, 45, 255);
    
    // Draw Background
    draw_list->AddRectFilled(p_min, p_max, bg_col, height * 0.5f);
    if (*v) DrawGlow(draw_list, p_min, p_max, uiGlowColor, height * 0.5f, uiGlowIntensity, 3);

    // Draw Knob
    float radius = height * 0.45f;
    float knob_pos_x = *v ? (p_max.x - radius - 2.0f) : (p_min.x + radius + 2.0f);
    ImVec2 center = ImVec2(knob_pos_x, p_min.y + height * 0.4f);
    draw_list->AddCircleFilled(center, radius, IM_COL32(255, 255, 255, 255));

    // Label
    ImGui::RenderText(ImVec2(pos.x, pos.y + (height - label_size.y) * 0.5f), label);

    return pressed;
}

bool ModernSlider(const char* label, float* v, float vmin, float vmax, const char* fmt = "%.1f") {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    
    // Label with glow
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImVec2 labelSize = ImGui::CalcTextSize(label);
    if (dl) {
        dl->AddText(cursorPos, IM_COL32(200, 200, 210, 255), label);
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + labelSize.y + 5);
    
    // FIXED: Value ABOVE slider - center se thoda RIGHT
    char buf[64];
    snprintf(buf, sizeof(buf), fmt, *v);
    ImVec2 valueSize = ImGui::CalcTextSize(buf);
    float sliderWidth = ImGui::GetWindowWidth() - 40;
    
    // Calculate where slider will be
    ImVec2 sliderWillStart = ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + valueSize.y + 4);
    ImVec2 sliderWillEnd = ImVec2(sliderWillStart.x + sliderWidth, sliderWillStart.y + 20);
    
    float centerX = sliderWillStart.x + (sliderWillEnd.x - sliderWillStart.x) * 0.5f - valueSize.x * 0.5f;
    float rightOffset = (sliderWillEnd.x - sliderWillStart.x) * 0.18f; // 18% right
    float valueX = centerX + rightOffset;
    float valueY = ImGui::GetCursorScreenPos().y;
    
    if (dl) {
        dl->AddText(ImVec2(valueX, valueY), IM_COL32(255, 255, 255, 255), buf);
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + valueSize.y);
    
    // Slider track
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.08f, 0.10f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(uiGlowColor.x * 0.2f, uiGlowColor.y * 0.2f, uiGlowColor.z * 0.2f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(uiGlowColor.x * 0.3f, uiGlowColor.y * 0.3f, uiGlowColor.z * 0.3f, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, uiGlowColor);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(uiGlowColor.x + 0.2f, uiGlowColor.y + 0.2f, uiGlowColor.z + 0.2f, 1.0f));
    ImGui::SetNextItemWidth(sliderWidth);
    
    char sliderId[128];
    snprintf(sliderId, sizeof(sliderId), "##%s", label);
    bool ch = ImGui::SliderFloat(sliderId, v, vmin, vmax, "");
    
    ImGui::PopStyleColor(5);
    ImGui::Dummy(ImVec2(0, 10));
    return ch;
}

// FIXED: Combo with glow label
bool ModernThemeCombo(const char* label, int* v, const char* items) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    if (dl) {
        dl->AddText(cursorPos, IM_COL32(220, 220, 230, 255), label);
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::CalcTextSize(label).y + 3);
    
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(uiGlowColor.x * 0.12f, uiGlowColor.y * 0.12f, uiGlowColor.z * 0.12f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(uiGlowColor.x * 0.2f, uiGlowColor.y * 0.2f, uiGlowColor.z * 0.2f, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(uiGlowColor.x * 0.25f, uiGlowColor.y * 0.25f, uiGlowColor.z * 0.25f, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(uiGlowColor.x * 0.25f, uiGlowColor.y * 0.25f, uiGlowColor.z * 0.25f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(uiGlowColor.x * 0.4f, uiGlowColor.y * 0.4f, uiGlowColor.z * 0.4f, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(uiGlowColor.x * 0.5f, uiGlowColor.y * 0.5f, uiGlowColor.z * 0.5f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(uiGlowColor.x * 0.3f, uiGlowColor.y * 0.3f, uiGlowColor.z * 0.3f, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(uiGlowColor.x * 0.5f, uiGlowColor.y * 0.5f, uiGlowColor.z * 0.5f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(uiGlowColor.x * 0.6f, uiGlowColor.y * 0.6f, uiGlowColor.z * 0.6f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(uiGlowColor.x * 0.4f, uiGlowColor.y * 0.4f, uiGlowColor.z * 0.4f, 0.5f));
    
    ImGui::PushItemWidth(ImGui::GetWindowWidth() - 45);
    
    char comboId[128];
    snprintf(comboId, sizeof(comboId), "##%s", label);
    bool ch = ImGui::Combo(comboId, v, items);
    
    ImGui::PopItemWidth(); 
    ImGui::PopStyleColor(10);
    ImGui::Dummy(ImVec2(0, 5));
    return ch;
}

// ==================== RENDER MENU ====================
void RenderMenu() {
    if (!g_FloatingSystemReady || !g_Landscape2SecDone) return;

    if (g_MenuMinimized) {
        float sw = ImGui::GetIO().DisplaySize.x, sh = ImGui::GetIO().DisplaySize.y;
        ImGui::SetNextWindowPos(ImVec2(sw / 2 - 130, sh - 50));
        ImGui::SetNextWindowSize(ImVec2(260, 50));
        ImGui::Begin("##Swipe", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetWindowPos();
        dl->AddRectFilled(ImVec2(p.x + 40, p.y + 20), ImVec2(p.x + 220, p.y + 26), IM_COL32(255, 255, 255, 140), 6.0f);
        DrawGlow(dl, ImVec2(p.x + 40, p.y + 20), ImVec2(p.x + 220, p.y + 26), ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 6.0f, 0.4f, 2);
        static ImVec2 ds; static bool dd = false;
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) { ds = ImGui::GetMousePos(); dd = true; }
        if (dd && ImGui::IsMouseDragging(0)) { if (ds.y - ImGui::GetMousePos().y > 50) { g_MenuMinimized = false; dd = false; } }
        if (ImGui::IsMouseReleased(0)) dd = false;
        ImGui::End();
        return;
    }

    if (!g_MenuMinimized && ImGui::IsMouseDoubleClicked(0) && !ImGui::GetIO().WantCaptureMouse) { g_MenuMinimized = true; }

    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 14.0f; st.ChildRounding = 10.0f; st.FrameRounding = 8.0f; st.PopupRounding = 10.0f;
    st.ItemSpacing = ImVec2(14, 14);
    st.Colors[ImGuiCol_WindowBg] = ImVec4(0.04f, 0.04f, 0.05f, uiBgOpacity);
    st.Colors[ImGuiCol_Border] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    st.Colors[ImGuiCol_ChildBg] = ImVec4(0.06f, 0.06f, 0.07f, 0.95f);

    ImGui::SetNextWindowSize(ImVec2(1100, 850), ImGuiCond_Once);
    ImGui::SetNextWindowSizeConstraints(ImVec2(1100, 850), ImVec2(1100, 850));
    ImGui::Begin(oxorany(" STRANGER "), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);

    ImVec2 p_min = ImGui::GetWindowPos();
    ImVec2 p_max = ImVec2(p_min.x + ImGui::GetWindowWidth(), p_min.y + ImGui::GetWindowHeight());
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // FIXED: Better header with glow
    draw_list->AddRectFilled(p_min, ImVec2(p_max.x, p_min.y + 60), IM_COL32(18, 18, 22, 255), 14.0f, ImDrawFlags_RoundCornersTop);
    // Header bottom accent line
    draw_list->AddLine(ImVec2(p_min.x + 20, p_min.y + 60), ImVec2(p_max.x - 20, p_min.y + 60), ImGui::ColorConvertFloat4ToU32(uiGlowColor), 2.0f);
    
    // FIXED: Glowing title
    ImGui::SetCursorPos(ImVec2(30, 18));
    const char* titleText = oxorany("STRANGER CHEATS");
    ImVec2 titleSize = ImGui::CalcTextSize(titleText);
    if (draw_list) {
        DrawGlowTextColored(draw_list, ImVec2(p_min.x + (p_max.x - p_min.x - titleSize.x) * 0.5f, p_min.y + 17), uiGlowColor, titleText);
    }

    ImGui::SetCursorPosY(65);

    // Left Sidebar
    ImGui::BeginChild("##Sidebar", ImVec2(240, 0), false, ImGuiWindowFlags_NoScrollbar);
    draw_list->AddRectFilled(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + 240, p_max.y), IM_COL32(14, 14, 17, 255), 14.0f, ImDrawFlags_RoundCornersBottomLeft);
    
    // Theme color divider line
    ImVec2 sidebar_pos = ImGui::GetWindowPos();
    

    ImGui::Dummy(ImVec2(0, 35));

    const char* tabs[] = { "AIMBOT", "VISUALS", "ESP CONFIG", "SETTINGS" };
    
    for (int i = 0; i < 4; i++) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 14));
        
        bool is_active = (currentTab == i);
        
        if (is_active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(uiGlowColor.x, uiGlowColor.y, uiGlowColor.z, 0.2f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(uiGlowColor.x, uiGlowColor.y, uiGlowColor.z, 0.3f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(uiGlowColor.x, uiGlowColor.y, uiGlowColor.z, 0.4f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.06f, 0.08f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(uiGlowColor.x * 0.3f, uiGlowColor.y * 0.3f, uiGlowColor.z * 0.3f, 0.2f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(uiGlowColor.x * 0.4f, uiGlowColor.y * 0.4f, uiGlowColor.z * 0.4f, 0.3f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.80f, 1.0f));
        }

        ImGui::SetCursorPosX(20);
        
        // FIXED: Glowing tab text when active
        ImVec2 btnPos = ImGui::GetCursorScreenPos();
        if (ImGui::Button(tabs[i], ImVec2(200, 55))) {
            currentTab = i;
        }
        
        if (is_active) {
            ImVec2 btn_min = ImGui::GetItemRectMin();
            ImVec2 btn_max = ImGui::GetItemRectMax();
            // Active indicator bar
            draw_list->AddRectFilled(
                ImVec2(btn_min.x - 1, btn_min.y + 1),
                ImVec2(btn_min.x + 6, btn_max.y - 1), 
                ImGui::ColorConvertFloat4ToU32(uiGlowColor), 3.0f);
        }
        
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
        ImGui::Dummy(ImVec2(0, 10));
    }
    
    ImGui::Dummy(ImVec2(0, 100));
    
    ImVec2 sidebar_size = ImGui::GetWindowSize();
    ImGui::SetCursorPos(ImVec2(20, sidebar_size.y - 55));
    if (draw_list) {
        DrawGlowText(draw_list, ImGui::GetCursorScreenPos(), IM_COL32(100, 100, 110, 255), oxorany("STRANGER"), 1.0f);
    }
    
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(20, 0));
    ImGui::SameLine();
    
    // Content Area
    ImGui::BeginChild("##Content", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::Dummy(ImVec2(0, 25));

    // Tab 0: AIM HACKS
    if (currentTab == 0) {
        ImGui::Columns(2, "AC", false);
        
        // LEFT COLUMN
        BeginModernPanel("##AM", oxorany("AIMBOT"), ImVec2(ImGui::GetColumnWidth() - 50, 440));     
        ModernToggle(oxorany("Aimbot Active"), &Aimbot); 
        ImGui::Dummy(ImVec2(0, 15));
        ModernToggle(oxorany("Aim Collider "), &AimLock); 
        ImGui::Dummy(ImVec2(0, 15));
        ModernToggle(oxorany("Show Aim Line"), &showAimLine); 
        ImGui::Dummy(ImVec2(0, 15));
        ModernToggle(oxorany("Ignore Knocked"), &Ignorknock);
        EndModernPanel();
        
        ImGui::NextColumn();
        
        // RIGHT COLUMN - FIXED: Height 500 se 300
        BeginModernPanel("##AP", oxorany("SILENT AIM"), ImVec2(ImGui::GetColumnWidth() - 50, 300));
        ModernToggle(oxorany("Silent Aim"), &AimSilent);
        ImGui::Dummy(ImVec2(0, 20));
        ModernSlider(oxorany("Aim Fov"), &AimFov, 0.0f, 360.0f, "%.1f");
        EndModernPanel();
        
        // FIXED: Targeting panel RIGHT column mein Silent Aim ke niche + thoda upar
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 10); // Thoda upar
        
        BeginModernPanel("##AT", oxorany("TARGETING"), ImVec2(ImGui::GetColumnWidth() - 50, 200));
        ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.7f, 1.0f), oxorany("AIM TARGET"));
        ImGui::PushItemWidth(ImGui::GetWindowWidth() - 55);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(uiGlowColor.x * 0.12f, uiGlowColor.y * 0.12f, uiGlowColor.z * 0.12f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(uiGlowColor.x * 0.2f, uiGlowColor.y * 0.2f, uiGlowColor.z * 0.2f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(uiGlowColor.x * 0.25f, uiGlowColor.y * 0.25f, uiGlowColor.z * 0.25f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(uiGlowColor.x * 0.25f, uiGlowColor.y * 0.25f, uiGlowColor.z * 0.25f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(uiGlowColor.x * 0.4f, uiGlowColor.y * 0.4f, uiGlowColor.z * 0.4f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(uiGlowColor.x * 0.3f, uiGlowColor.y * 0.3f, uiGlowColor.z * 0.3f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(uiGlowColor.x * 0.5f, uiGlowColor.y * 0.5f, uiGlowColor.z * 0.5f, 0.8f));
        
        ImGui::Combo("##BS", &selectedBoneIndex, oxorany("HEAD\0BODY\0"));
        
        ImGui::PopStyleColor(7); 
        ImGui::PopItemWidth();
        EndModernPanel();
        
        ImGui::Columns(1);
    }
    
    // Tab 1: VISUALS
    else if (currentTab == 1) {
        ImGui::Columns(2, "EC", false);
        
        BeginModernPanel("##EM", oxorany("ESP FUNCTIONS"), ImVec2(ImGui::GetColumnWidth() - 50, 700));
        ModernToggle(oxorany("Show Lines"), &showLine); 
        ImGui::Dummy(ImVec2(0, 15));
        ModernToggle(oxorany("Show Boxes"), &showBox); 
        ImGui::Dummy(ImVec2(0, 15));
        ModernToggle(oxorany("Show Health"), &showHealth); 
        ImGui::Dummy(ImVec2(0, 15));
        ModernToggle(oxorany("Show Distance"), &showDistance); 
        ImGui::Dummy(ImVec2(0, 15));       
        ModernToggle(oxorany("Show 360 Alert"), &is3600); 
        ImGui::Dummy(ImVec2(0, 15));
        ModernToggle(oxorany("Show Name"), &showName); 
        ImGui::Dummy(ImVec2(0, 15));
        ModernToggle(oxorany("Show Skeleton"), &showSkeleton); 
        ImGui::Dummy(ImVec2(0, 15));
        ModernToggle(oxorany("Show Player Count"), &showCount);
        EndModernPanel();
        
        ImGui::NextColumn();
        
        BeginModernPanel("##EE", oxorany("EXTRA ESP"), ImVec2(ImGui::GetColumnWidth() - 50, 700));
       // ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.7f, 1.0f), "Text Size");
        float ts = (float)textSize;
        if (ModernSlider(oxorany("Text Size"), &ts, 10.0f, 50.0f, "%.0f")) textSize = (int)ts;
        ImGui::Dummy(ImVec2(0, 30));
        ModernToggle(oxorany("Show Head Dot"), &showHeadDot);
        ImGui::Dummy(ImVec2(0, 20));
        ModernSlider(oxorany("Head Dot Radius"), &showHeadDotRadius, 2.0f, 12.0f, "%.1f");
        ImGui::Dummy(ImVec2(0, 30));
        ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.7f, 1.0f), oxorany("Head Dot Color"));
        ImGui::ColorEdit4("##HDC", (float*)&headDotColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        EndModernPanel();
        
        ImGui::Columns(1);
    }
    
    // Tab 2: ESP CONFIG
    else if (currentTab == 2) {
        ImGui::Columns(2, "EC2", false);
        
        BeginModernPanel("##BC", oxorany("ESP BOX STYLE"), ImVec2(ImGui::GetColumnWidth() - 50, 720));
        ModernThemeCombo("Style", &boxStyle, oxorany("Normal\0Filled\0Outline\0Gradient\0Corner\0Glow\0Dashed\0Double\0Rounded\03D\0"));
        ModernThemeCombo(oxorany("Color Mode"), &boxColorMode, oxorany("Static\0Health-Based\0Distance-Based\0Rainbow\0Team-Based\0"));
        ModernSlider(oxorany("Box Thickness"), &boxThickness, 1.0f, 5.0f, "%.1f");  
        ModernSlider(oxorany("Box Alpha"), &boxFillAlpha, 0.0f, 1.0f, "%.2f");
        ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.7f, 1.0f), oxorany("Box Color"));
        ImGui::ColorEdit4("##BCA", (float*)&boxCustomColor, ImGuiColorEditFlags_NoInputs);
       // ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.7f, 1.0f), "Glow Color");
      //  ImGui::ColorEdit4("##GCA", (float*)&boxGlowColor, ImGuiColorEditFlags_NoInputs);
        EndModernPanel();
        
        ImGui::NextColumn();
        
        BeginModernPanel("##LHC", oxorany("ESP LINE & HEALTH STYLE"), ImVec2(ImGui::GetColumnWidth() - 50, 720));
        ModernThemeCombo("Line Style", &lineStyle, oxorany("Normal\0Dotted\0Dashed\0Thick\0Gradient\0Glowing\0Arrow\0Double\0Neon\0Laser\0"));
        ModernSlider(oxorany("Line Thickness"), &lineThickness, 1.0f, 8.0f, "%.1f");
        ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.7f, 1.0f), "Line Color");
        ImGui::ColorEdit4("##LCA", (float*)&lineCustomColor, ImGuiColorEditFlags_NoInputs);
        ImGui::Dummy(ImVec2(0, 30));
        ModernThemeCombo(oxorany("Health Style"), &healthBarStyle, oxorany("Normal\0Gradient\0Smooth\0Thin\0Thick\0Outline\0Rounded\0Rainbow\0Animated\0Digital\0"));
        ModernSlider(oxorany("Health Width"), &healthBarWidth, 3.0f, 15.0f, "%.1f");
        ModernToggle(oxorany("Animated Health"), &animateHealthBar);
        EndModernPanel();
        
        ImGui::Columns(1);
    }
    
    // Tab 3: SETTINGS
    else if (currentTab == 3) {
        ImGui::Columns(2, "IC", false);
        
        BeginModernPanel("##UI", oxorany("UI SETTINGS"), ImVec2(ImGui::GetColumnWidth() - 50, 400));
        ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.7f, 1.0f), "Menu Color");
        ImGui::Dummy(ImVec2(0, 8));
        
        ImGui::ColorEdit4("##GT", (float*)&uiGlowColor, 
            ImGuiColorEditFlags_AlphaBar | 
            ImGuiColorEditFlags_NoLabel |
            ImGuiColorEditFlags_NoInputs |
            ImGuiColorEditFlags_DisplayRGB |
            ImGuiColorEditFlags_PickerHueBar);
        
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(uiGlowColor.x * 0.3f, uiGlowColor.y * 0.3f, uiGlowColor.z * 0.3f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(uiGlowColor.x * 0.5f, uiGlowColor.y * 0.5f, uiGlowColor.z * 0.5f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        if (ImGui::Button("Reset", ImVec2(80, 35))) {
            uiGlowColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
        }
        ImGui::PopStyleColor(3);
        
      /*  ImGui::Dummy(ImVec2(0, 30));
        ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.7f, 1.0f), "Bone Preview");
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(uiGlowColor.x * 0.12f, uiGlowColor.y * 0.12f, uiGlowColor.z * 0.12f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(uiGlowColor.x * 0.2f, uiGlowColor.y * 0.2f, uiGlowColor.z * 0.2f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(uiGlowColor.x * 0.25f, uiGlowColor.y * 0.25f, uiGlowColor.z * 0.25f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(uiGlowColor.x * 0.25f, uiGlowColor.y * 0.25f, uiGlowColor.z * 0.25f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(uiGlowColor.x * 0.4f, uiGlowColor.y * 0.4f, uiGlowColor.z * 0.4f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(uiGlowColor.x * 0.3f, uiGlowColor.y * 0.3f, uiGlowColor.z * 0.3f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(uiGlowColor.x * 0.5f, uiGlowColor.y * 0.5f, uiGlowColor.z * 0.5f, 0.8f));
        
        ImGui::Combo("##BSP", &selectedBoneIndex, 
            "HEAD\0BODY\0");*/
        
        ImGui::PopStyleColor(7);
        ImGui::Dummy(ImVec2(0, 25));
        ModernSlider("Menu border", &uiGlowIntensity, 0.0f, 5.0f, "%.1f");
        
        EndModernPanel();
        
        ImGui::NextColumn();
        
        BeginModernPanel("##SP", "INFORMATION", ImVec2(ImGui::GetColumnWidth() - 50, 450));
       // ImGui::TextColored(uiGlowColor, "License Information");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 20));
        
        ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.7f, 1.0f), "Status: "); ImGui::SameLine();
        std::string status = g_auth.getStatus();
        if (status == "ACTIVE") {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "%s", status.c_str());
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", status.c_str());
        }
        
        ImGui::Dummy(ImVec2(0, 15));
        ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.7f, 1.0f), "License Key: "); ImGui::SameLine();
        std::string maskedKey = g_auth.getLicenseKey();
        if (maskedKey.length() > 12) {
            maskedKey = maskedKey.substr(0, 6) + "..." + maskedKey.substr(maskedKey.length() - 6);
        }
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1), "%s", maskedKey.c_str());
        

        
     /*   ImGui::Dummy(ImVec2(0, 15));
        ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.7f, 1.0f), "Device ID: "); ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1), "%s", g_auth.getHWID().c_str());*/
        
        std::string msg = g_auth.getAuthMessage();
        if (!msg.empty() && msg != "Login successful") {
            ImGui::Dummy(ImVec2(0, 15));
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Message: %s", msg.c_str());
        }
        
        ImGui::Dummy(ImVec2(0, 80));
        
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(uiGlowColor.x * 0.2f, 0.03f, 0.03f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(uiGlowColor.x * 0.4f, 0.05f, 0.05f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(uiGlowColor.x * 0.6f, 0.08f, 0.08f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        
        if (ImGui::Button("EXIT", ImVec2(ImGui::GetWindowWidth() - 60, 65))) {
            main_thread_flag = false; 
            shutdown(); 
            Touch_Close(); 
            _exit(0);
        }
        
        ImGui::PopStyleColor(4);
        EndModernPanel();
        
        ImGui::Columns(1);
    }

    ImGui::EndChild();
    ImGui::End();
}

// ==================== MAIN ====================
int main(int argc, char *argv[]) {
    Authentication auth;
    if (!auth.run()) return 1;
    g_auth = auth;
    
    printf("\n[GO OPEN FREE FIRE MAX]\n");
    
    screen_config();
    
    bool landscapeMsgShown = false;
    bool portraitMsgShown = false;
    
    while (main_thread_flag) {
        screen_config();
        
        if (IsScreenLandscape()) {
            if (!landscapeMsgShown) {
                
                landscapeMsgShown = true;
                portraitMsgShown = false;
            }
            
            auto landscapeStartTime = std::chrono::steady_clock::now();
            bool twoSecDone = false;
            
            while (!twoSecDone && main_thread_flag) {
                usleep(100000);
                
                screen_config();
                if (!IsScreenLandscape()) {
                    
                    landscapeMsgShown = false;
                    break;
                }
                
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - landscapeStartTime).count();
                
                if (elapsed >= 2000) {
                    twoSecDone = true;
                    
                }
            }
            
            if (twoSecDone) {
                break;
            }
        } else {
            if (!portraitMsgShown) {
                printf("[GO OPEN FREE FIRE MAX]");
                portraitMsgShown = true;
                landscapeMsgShown = false;
            }
            usleep(500000);
        }
    }
    
    if (!main_thread_flag) return 0;
    
    
    
    std::thread(VolumeKeyThread).detach();
    std::thread(SilentAimThread).detach();

    game_pid = getProcessID(packageName);
    if (game_pid <= 0) {
        
        g_GameIsRunning = false;
    } else {
        lib_base = get_module_base(targetaLibName, PERM_EXEC);
        lib_base2 = get_module_base(targetaLibName2, PERM_EXEC);
        g_GameIsRunning = true;
        
    }
    
    screen_config();
    ::abs_ScreenX = (displayInfo.height > displayInfo.width ? displayInfo.height : displayInfo.width);
    ::abs_ScreenY = (displayInfo.height < displayInfo.width ? displayInfo.height : displayInfo.width);
    ::native_window_screen_x = (displayInfo.height > displayInfo.width ? displayInfo.height : displayInfo.width);
    ::native_window_screen_y = (displayInfo.height > displayInfo.width ? displayInfo.height : displayInfo.width);
    
    if (!initGUI_draw(native_window_screen_x, native_window_screen_y, true)) {
        
        return -1;
    }
    
    Touch_Init(displayInfo.width, displayInfo.height, displayInfo.orientation, true);
    
    g_FloatingSystemInit = true;
    g_FloatingSystemReady = true;
    g_Landscape2SecDone = true;
    g_IsLandscape = true;
    
    

    // Reset flags for main loop
    portraitMsgShown = false;
    bool landscapeResumeMsgShown = false;
    bool landscapePauseMsgShown = false;
    bool gameFoundMsgShown = false;
    
    while (main_thread_flag) {
        drawBegin();
        {
            ImGui::SetNextWindowPos(ImVec2(-100, -100));
            ImGui::SetNextWindowSize(ImVec2(10, 10));
            ImGui::Begin("##AntiCrash", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
            ImGui::End();
            
            bool currentlyLandscape = IsScreenLandscape();
            
            if (!currentlyLandscape) {
                if (g_FloatingSystemReady) {
                    if (!landscapePauseMsgShown) {
                        
                        landscapePauseMsgShown = true;
                        landscapeResumeMsgShown = false;
                    }
                    g_FloatingSystemReady = false;
                    g_Landscape2SecDone = false;
                    g_IsLandscape = false;
                    g_MenuMinimized = true;
                }
            } else {
                if (!g_FloatingSystemReady) {
                    if (!g_LandscapeTimerActive) {
                        g_LandscapeDetectTime = std::chrono::steady_clock::now();
                        g_LandscapeTimerActive = true;
                        g_IsLandscape = true;
                        
                    }
                    
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_LandscapeDetectTime).count();
                    
                    if (elapsed >= 2000) {
                        g_FloatingSystemReady = true;
                        g_Landscape2SecDone = true;
                        g_LandscapeTimerActive = false;
                        g_MenuMinimized = true;
                        landscapePauseMsgShown = false;
                        landscapeResumeMsgShown = true;
                        
                    }
                }
            }
            
            if (!g_GameIsRunning) {
                game_pid = getProcessID(packageName);
                if (game_pid > 0) {
                    lib_base = get_module_base(targetaLibName, PERM_EXEC);
                    lib_base2 = get_module_base(targetaLibName2, PERM_EXEC);
                    g_GameIsRunning = true;
                    
                }
            }
            
            if (g_FloatingSystemReady) {
                RenderMenu();
                
                if (g_GameIsRunning && lib_base != 0) {
                    DrawESP(abs_ScreenX, abs_ScreenY, lib_base);
                    AimbotMenu(abs_ScreenX, abs_ScreenY, lib_base);
                }
            }
        }
        drawEnd();
        usleep(16000);
    }

    shutdown();
    Touch_Close();
    return 0;
}