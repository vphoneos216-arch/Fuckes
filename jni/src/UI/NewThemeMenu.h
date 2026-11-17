#pragma once
// =====================================================================
// [NEW] Second, separate menu window — red/black themed.
//
// Context for why this file exists and what it deliberately does NOT
// do: the user uploaded a zip containing another project's UI code
// ("jni/UI/UI.h" + a Chinese-named menu header). Read in full before
// writing a line of this file:
//   - "UI.h" in that zip draws through Unreal Engine's UCanvas /
//     FLinearColor / UFont / K2_DrawLine reflection API. Its own
//     Android.mk compiles 三水_*.cpp (ShadowTrackerExtra / CoreUObject
//     / Engine SDK offsets) and GlossHook/shadowhook — that whole
//     stack is a PUBG MOBILE (Unreal Engine) cheat base. Free Fire
//     MAX does not run on Unreal Engine and exposes none of those
//     types or that SDK, so UI.h cannot be compiled into this project
//     at all — not a style choice, a different engine entirely.
//   - The OTHER menu file in that zip ("菜单.h") IS ImGui-based, same
//     framework this project already uses, and so is the one thing
//     from that upload that's actually portable in principle — but it
//     calls into its own custom widget header ("imgui/鑫傲控件.h")
//     whose full implementation isn't guaranteed compatible with this
//     project's ImGui build either, so rather than guess at wiring a
//     foreign widget library into an already-working renderer, this
//     recreates the asked-for red/black look directly with this
//     project's own existing ImGui widgets.
//
// Per the request: the EXISTING menu (SourceMenu.h / RenderMenu() in
// main.cpp) is completely untouched — not one line in it changed for
// this. This is a second, independent window, red/black themed, with
// exactly one working control: a combined "Collider Aimbot" toggle
// that turns on the already-HWBP-based collider lock (hook.h's
// UpdateAimLockNewThread) and the already-hook/HWBP-based Aimbot
// (UpdateAimbotFast) together, through the exact same live sync path
// already fixed for both of them (g_Cfg.aimCollider / g_Cfg.Aimbot →
// AimLock / AimLockNew / Aimbot), so flipping this checkbox has the
// same immediate real effect as the checkboxes already on the Aim
// page — nothing new being invented here, just a second place to
// reach the same two already-working features.
// =====================================================================

static void RenderNewThemeUI(){
    if(!g_FloatingSystemReady || !g_Landscape2SecDone) return;
    if(!ix_rt_ready()) return; // same auth gate RenderMenu() already enforces

    ImGuiIO& io = ImGui::GetIO();
    float scrW = io.DisplaySize.x, scrH = io.DisplaySize.y;
    if(scrW <= 0.f || scrH <= 0.f) return;

    // Small fixed panel, top-left, well clear of the main InvisibleX
    // panel — draggable via its own (visible, red/black) title bar
    // rather than reimplementing custom drag handling here.
    ImVec2 panelSize(260.0f, 92.0f);
    ImGui::SetNextWindowPos(ImVec2(scrW * 0.03f, scrH * 0.06f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(panelSize, ImGuiCond_FirstUseEver);

    // Red/black palette, as asked. Pushed/popped around this window
    // only — cannot bleed into the existing menu's own styling.
    const ImVec4 kBlack      (0.05f, 0.00f, 0.00f, 0.95f);
    const ImVec4 kBlackSoft  (0.10f, 0.02f, 0.02f, 0.92f);
    const ImVec4 kRed        (0.80f, 0.08f, 0.08f, 1.00f);
    const ImVec4 kRedBright  (0.95f, 0.15f, 0.15f, 1.00f);
    const ImVec4 kRedDim     (0.45f, 0.05f, 0.05f, 1.00f);
    const ImVec4 kText       (0.95f, 0.90f, 0.90f, 1.00f);

    ImGui::PushStyleColor(ImGuiCol_WindowBg,        kBlack);
    ImGui::PushStyleColor(ImGuiCol_TitleBg,         kBlackSoft);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,   kRedDim);
    ImGui::PushStyleColor(ImGuiCol_Border,          kRed);
    ImGui::PushStyleColor(ImGuiCol_Text,            kText);
    ImGui::PushStyleColor(ImGuiCol_CheckMark,       kRedBright);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,         kBlackSoft);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,  kRedDim);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,   kRed);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);

    if(ImGui::Begin("Aim / ESP / Exploit / Info", nullptr, ImGuiWindowFlags_NoCollapse)){
        // Only one working control here, exactly as asked. Setting
        // both config bools together is the "Collider" + "Aimbot"
        // combination the user asked for as a single toggle; synced
        // to the live globals immediately below so it takes effect
        // this frame, the same as the Aim page's own checkboxes.
        static bool colliderAimbot = false;
        colliderAimbot = g_Cfg.aimCollider && g_Cfg.Aimbot; // reflect external state too

        ImGui::TextColored(kRedBright, "Collider Aimbot");
        ImGui::Separator();
        if(ImGui::Checkbox("Enable (HWBP)", &colliderAimbot)){
            g_Cfg.aimCollider = colliderAimbot;
            g_Cfg.Aimbot      = colliderAimbot;
            AimLock           = colliderAimbot;
            AimLockNew        = colliderAimbot;
            Aimbot            = colliderAimbot;
            if(colliderAimbot){
                StartAimLockNewThread();
                StartAimbotThread();
            }
        }
        ImGui::TextColored(kRedDim, "Aim / ESP / Exploit / Info");
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(9);
}
