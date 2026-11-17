#pragma once
// =====================================================================
// [NEW] Second, separate menu window — red/black themed, "Jini" UI.
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
//     foreign widget library in, this recreates the asked-for red/black
//     look with this project's own existing ImGui widgets, including
//     the project's own custom switch control (ImGui::Checkbox1, the
//     same one SourceMenu.h already uses everywhere) so the look and
//     feel actually matches the rest of the app.
//
// Per the request: the EXISTING menu (SourceMenu.h / RenderMenu() in
// main.cpp) is completely untouched — not one line in it changed for
// this. This is a second, independent, scrollable window, red/black
// themed, covering the full working feature set (ESP, Aim, Exploit) —
// every checkbox here is bound to the SAME g_Cfg / global variables
// SourceMenu.h's own checkboxes already use, so there is exactly one
// source of truth for each feature's state; toggling it here or on the
// original menu has the identical, immediate, real effect either way.
// Nothing here is a new feature or a new mechanism — it is a second,
// themed surface onto features that already exist and already work.
// =====================================================================

static void RenderNewThemeUI(){
    if(!g_FloatingSystemReady || !g_Landscape2SecDone) return;
    if(!ix_rt_ready()) return; // same auth gate RenderMenu() already enforces

    ImGuiIO& io = ImGui::GetIO();
    float scrW = io.DisplaySize.x, scrH = io.DisplaySize.y;
    if(scrW <= 0.f || scrH <= 0.f) return;

    // Taller scrollable panel, top-left, well clear of the main
    // InvisibleX panel — draggable via its own (visible, red/black)
    // title bar rather than reimplementing custom drag handling here.
    ImVec2 panelSize(300.0f, 460.0f);
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
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,     kBlackSoft);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,   kRed);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);

    if(ImGui::Begin("Jini — Aim / ESP / Exploit / Info", nullptr, ImGuiWindowFlags_NoCollapse)){

        // ── Headline control: combined Collider Aimbot ─────────────
        // Binds both g_Cfg.aimCollider and g_Cfg.Aimbot (and their live
        // globals) together as one switch, through the exact same sync
        // path the individual checkboxes below already use — flipping
        // either this or the two separate switches below updates the
        // same state, so they always agree with each other.
        {
            static bool colliderAimbot = false;
            colliderAimbot = g_Cfg.aimCollider && g_Cfg.Aimbot;

            ImGui::TextColored(kRedBright, "Collider Aimbot (combined)");
            ImGui::Separator();
            if(ImGui::Checkbox1("Enable (HWBP)", &colliderAimbot)){
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
        }

        ImGui::Dummy(ImVec2(0, 10));

        // ── Aim ──────────────────────────────────────────────────
        ImGui::TextColored(kRedBright, "Aim");
        ImGui::Separator();
        if(ImGui::Checkbox1("Aim Silent", &AimSilent))   g_Cfg.aimSilent = AimSilent;
        ImGui::Checkbox1("Collider Lock", &g_Cfg.aimCollider);
        ImGui::Checkbox1("Body Kill",     &g_Cfg.bodyKill);
        ImGui::Checkbox1("Aimbot",        &g_Cfg.Aimbot);
        ImGui::Checkbox1("Show FOV Circle", &g_Cfg.showFovCircle);
        ImGui::Checkbox1("Hardware BT",   &g_Cfg.hardwareBT);

        ImGui::Dummy(ImVec2(0, 10));

        // ── ESP ──────────────────────────────────────────────────
        ImGui::TextColored(kRedBright, "ESP");
        ImGui::Separator();
        ImGui::Checkbox1("Box",       &g_Cfg.showBox);
        ImGui::Checkbox1("Skeleton",  &g_Cfg.showSkeleton);
        ImGui::Checkbox1("Name",      &g_Cfg.showName);
        ImGui::Checkbox1("Health",    &g_Cfg.showHealth);
        ImGui::Checkbox1("Distance",  &g_Cfg.showDistance);
        ImGui::Checkbox1("ESP Line",  &g_Cfg.showLine);
        ImGui::Checkbox1("360 Alert", &g_Cfg.is3600);

        ImGui::Dummy(ImVec2(0, 10));

        // ── Exploit ──────────────────────────────────────────────
        ImGui::TextColored(kRedBright, "Exploit");
        ImGui::Separator();
        ImGui::Checkbox1("Ghost Hack", &GhostHack);
        ImGui::Checkbox1("No Recoil",  &NoRecoil);
        ImGui::Checkbox1("No Reload",  &NoReload);
        ImGui::Checkbox1("Float: Ghost", &g_ExploitFloatGhost);

        ImGui::Dummy(ImVec2(0, 10));

        // ── Info ─────────────────────────────────────────────────
        ImGui::TextColored(kRedDim, "Info");
        ImGui::Separator();
        ImGui::Checkbox1("Volume Off", &g_Cfg.soundMuted);
        ix_sound_set_muted(g_Cfg.soundMuted);
        ImGui::Checkbox1("Internal Collider Lock", &g_ColliderInternalEnabled);

        // ── Sync g_Cfg → live legacy globals, every frame ───────────
        // SourceMenu.h's own Aim/ESP pages only perform this exact sync
        // while that specific page is the one being rendered — if the
        // user only ever opens this window and never the old tabs, a
        // g_Cfg write alone would sit there with no effect, since the
        // actual feature logic (hook.h, the draw loop) reads these
        // plain globals, not g_Cfg directly. Mirrored here, every
        // frame this window renders, so every toggle above is real
        // regardless of which menu the user is using.
        AimLock      = g_Cfg.aimCollider;
        AimLockNew   = g_Cfg.aimCollider;
        Aimbot       = g_Cfg.Aimbot;
        BodyKill     = g_Cfg.bodyKill;
        HardwareBT   = g_Cfg.hardwareBT;
        g_ShowFovOverlay = g_Cfg.showFovCircle;

        showBox      = g_Cfg.showBox;
        showSkeleton = g_Cfg.showSkeleton;
        showName     = g_Cfg.showName;
        showHealth   = g_Cfg.showHealth;
        showDistance = g_Cfg.showDistance;
        showLine     = g_Cfg.showLine;
        is3600       = g_Cfg.is3600;
        Enable = (showLine || showBox || showSkeleton || showName ||
                  showHealth || showDistance || showCount);
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(11);
}
