#pragma once
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "../game/data.hpp"
#include "../ui/cfg.hpp"
#include "../ui/styles/styles.h"
#include <cstdio>
#include <mutex>
#include <chrono>
#include <cmath>

namespace func::esp {

    // ── Color-box functions, ported verbatim from InvisibleX's hook.h
    // (HpColor / DistColor / RainbowColor / the BoxCol dispatcher). Pure
    // color math only — no offsets, no memory reads — so nothing here
    // touches the scanning/W2S backend this project already has working.
    inline ImU32 ix_hp_color(int h) {
        if (h >= 80) return IM_COL32(0, 220, 0, 255);
        if (h >= 50) return IM_COL32(255, 220, 0, 255);
        if (h >= 25) return IM_COL32(255, 140, 0, 255);
        return IM_COL32(255, 50, 50, 255);
    }
    inline ImU32 ix_dist_color(float d) {
        if (d < 50)  return IM_COL32(255, 50, 50, 255);
        if (d < 150) return IM_COL32(255, 220, 0, 255);
        return IM_COL32(0, 220, 0, 255);
    }
    inline ImU32 ix_rainbow_color(float off = 0.f) {
        static float hue = 0.f;
        static auto  last = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        hue += std::chrono::duration<float>(now - last).count() * 0.10f;
        last = now; if (hue > 1.f) hue -= 1.f;
        return ImColor::HSV(fmodf(hue + off, 1.f), 1.f, 1.f);
    }
    // knocked always wins (red), same as InvisibleX's BoxCol; otherwise the
    // existing fixed (visible/hidden) color stays mode 0's behaviour so
    // nothing changes on screen until the user picks a different mode.
    inline ImU32 ix_box_color(bool knocked, int curHP, int maxHP, float dist,
                              ImU32 fixedCol) {
        if (knocked) return IM_COL32(255, 60, 60, 255);
        switch (cfg::esp::box_color_mode) {
            case 1:  return ix_hp_color(maxHP > 0 ? (curHP * 100) / maxHP : curHP);
            case 2:  return ix_dist_color(dist);
            case 3:  return ix_rainbow_color(0.f);
            default: return fixedCol;
        }
    }

    // Fixed-pixel-size text, same as hook.h's own ImGui::GetFont()->
    // CalcTextSizeA(size,...)/dl->AddText(nullptr,size,...) pattern --
    // size is independent of the menu's current window font scale, so
    // HP/distance/name/weapon text renders at the exact size hook.h
    // uses instead of whatever size the menu's ImGui font happens to
    // be drawing at when the overlay is rendered.
    inline ImVec2 calc_text_size_fixed(float size, const char* txt) {
        return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.f, txt);
    }
    inline void draw_shadowed_text(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* txt, float size = 0.f) {
        if (size <= 0.f) {
            dl->AddText({pos.x + 1.f, pos.y + 1.f}, IM_COL32(0, 0, 0, 200), txt);
            dl->AddText(pos, col, txt);
        } else {
            dl->AddText(nullptr, size, {pos.x + 1.f, pos.y + 1.f}, IM_COL32(0, 0, 0, 200), txt);
            dl->AddText(nullptr, size, pos, col, txt);
        }
    }

    inline void draw_pill(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* txt, float pad = 4.f) {
        ImVec2 ts = calc_text_size_fixed(cfg::esp::text_size, txt);
        ImVec2 tl(pos.x - pad, pos.y - pad * 0.5f);
        ImVec2 br(pos.x + ts.x + pad, pos.y + ts.y + pad * 0.5f);
        dl->AddRectFilled(tl, br, IM_COL32(10, 10, 15, 210), 4.f);
        dl->AddRect(tl, br, col, 4.f, 0, 1.2f);
        dl->AddText(nullptr, cfg::esp::text_size, pos, IM_COL32_WHITE, txt);
    }

    inline void draw() {
        if (!cfg::esp::box && !cfg::esp::name && !cfg::esp::health && !cfg::esp::distance && !cfg::esp::line) return;

        std::vector<PlayerData> snap;
        {
            std::lock_guard<std::mutex> lk(data::players_mutex);
            snap = data::players;
        }
        if (snap.empty()) return;

        ImDrawList* dl = ImGui::GetForegroundDrawList();

        ImU32 col_box_default = ImGui::ColorConvertFloat4ToU32(cfg::esp::box_col);
        ImU32 col_box_visible = ImGui::ColorConvertFloat4ToU32(cfg::esp::box_visible);
        ImU32 col_name        = ImGui::ColorConvertFloat4ToU32(cfg::esp::name_col);
        ImU32 col_dist        = ImGui::ColorConvertFloat4ToU32(cfg::esp::distance_col);
        ImU32 col_weapon      = ImGui::ColorConvertFloat4ToU32(cfg::esp::weapon_col);
        ImU32 col_bot         = ImGui::ColorConvertFloat4ToU32(cfg::esp::bot_col);
        ImU32 col_tracer      = ImGui::ColorConvertFloat4ToU32(cfg::esp::tracer_col);

        ImVec2 screen(g_sw, g_sh);
        ImVec2 center(screen.x * 0.5f, screen.y * 0.5f);

        for (const auto& p : snap) {
            if (p.Head.x < 0 || p.Toe.x < 0) continue;
            if (p.curHP <= 0) continue;

            // hook.h's exact box math -- rawH is literally feet.y - head.y
            // in screen space, bh/bw/bx/by are derived from it with no
            // distance-based height GUESS of any kind. This replaces the
            // old est_h/body_h<20 fallback formulas, which is what was
            // making far-away boxes come out the wrong size: those
            // formulas overrode the real (naturally shrinking-with-
            // distance) screen height with an invented one whenever it
            // read as "too small", instead of just letting perspective do
            // its job the way hook.h does. Degenerate cases are skipped
            // outright here too (rawH too small, box off-screen), same as
            // hook.h -- never patched over with a guessed size.
            float rawH = p.Toe.y - p.Head.y;
            if (rawH < 2.f) continue;
            float bh = rawH * 1.12f;
            if (bh < 8.f || bh > g_sh * 1.2f) continue;
            float bw = bh * 0.45f;
            if (bw < 4.f) bw = 4.f;
            float bx = p.Head.x - bw * 0.5f;
            float by = p.Head.y - bh * 0.08f;
            if (by < -bh) continue;
            if (bx + bw < -20.f || bx > g_sw + 20.f) continue;

            ImVec2 tl(bx, by);
            ImVec2 br(bx + bw, by + bh);
            float cx = p.Head.x; // hx in hook.h -- the line/tracer origin point

            const ImU32 col_box = ix_box_color(
                p.isKnocked, p.curHP, p.maxHP, p.distance,
                p.isVisible ? col_box_visible : col_box_default);

            if (cfg::esp::box) {
                // hook.h's exact box style: a plain white outline rect first,
                // then the color-coded rect on top -- both square (0
                // rounding, 0 flags), same as InvisibleX's ESP. Replaces the
                // corner-bracket/rounded-fill styles this project had before,
                // which hook.h doesn't have.
                dl->AddRect(tl, br, IM_COL32(255, 255, 255, 255), 0, 0, cfg::esp::box_thickness);
                dl->AddRect(tl, br, col_box, 0, 0, cfg::esp::box_thickness);
            }

            if (cfg::esp::skeleton) {
                // hook.h's exact skeleton: Head-Chest-L/RShoulder-L/RHand,
                // Chest-Hip-L/RFoot, plus a chest dot. Same color/thickness
                // as hook.h (fixed green, 2.4f), same ok[a]&&ok[b] guard per
                // segment -- a bone this frame's read didn't produce just
                // isn't drawn, the rest of the chain still is.
                const ImU32 SK = IM_COL32(0, 255, 80, 255);
                constexpr float tk = 2.4f;
                auto Pt = [&](int b) { return ImVec2(p.skelBones[b].x, p.skelBones[b].y); };
                auto Ln = [&](int a, int b) {
                    if (p.skelOk[a] && p.skelOk[b]) dl->AddLine(Pt(a), Pt(b), SK, tk);
                };
                Ln(BONE_HEAD,  BONE_CHEST);
                Ln(BONE_CHEST, BONE_LSH);
                Ln(BONE_CHEST, BONE_RSH);
                Ln(BONE_LSH,   BONE_LHAND);
                Ln(BONE_RSH,   BONE_RHAND);
                Ln(BONE_CHEST, BONE_HIP);
                Ln(BONE_HIP,   BONE_LFOOT);
                Ln(BONE_HIP,   BONE_RFOOT);
                if (p.skelOk[BONE_CHEST]) dl->AddCircleFilled(Pt(BONE_CHEST), 2.4f, SK, 10);
            }

            if (cfg::esp::head_dot) {
                dl->AddCircleFilled(ImVec2(p.Head.x, p.Head.y), cfg::esp::head_dot_radius,
                                    ImGui::ColorConvertFloat4ToU32(cfg::esp::head_dot_col));
            }

            if (cfg::esp::health) {
                // hook.h's exact health-bar formula: displayHP is 0 when
                // knocked, pct is displayHP/200 (fixed scale, not current/
                // max), fill is HpColor(displayHP) in gradient mode or solid
                // green otherwise, knocked always forces red.
                int displayHP = p.isKnocked ? 0 : p.curHP;
                float pct = displayHP / 200.f;
                if (pct < 0.f) pct = 0.f;
                if (pct > 1.f) pct = 1.f;
                float bar_x_tl = tl.x - cfg::esp::health_bar_width - 3.f;
                float bar_x_br = tl.x - 3.f;
                float fill_h = (br.y - tl.y) * pct;
                float fill_y = br.y - fill_h;
                dl->AddRectFilled({bar_x_tl, tl.y}, {bar_x_br, br.y},
                                  IM_COL32(0, 0, 0, 180), cfg::esp::health_bar_rounding);
                ImU32 fc = cfg::esp::health_color_gradient ? ix_hp_color(displayHP) : IM_COL32(0, 200, 0, 255);
                if (p.isKnocked) fc = IM_COL32(255, 50, 50, 255);
                dl->AddRectFilled({bar_x_tl, fill_y}, {bar_x_br, br.y}, fc, cfg::esp::health_bar_rounding);

                char hpbuf[16];
                snprintf(hpbuf, sizeof(hpbuf), "%d", displayHP);
                // hook.h draws its HP number at a fixed 11.f pixel size,
                // not the UI's text_size -- ported exactly, same constant.
                ImVec2 ts = calc_text_size_fixed(11.f, hpbuf);
                draw_shadowed_text(dl, {bar_x_tl - ts.x - 3.f, tl.y - ts.y * 0.15f}, IM_COL32_WHITE, hpbuf, 11.f);
            }

            float top_stack_y = tl.y - 6.f;
            if (cfg::esp::name && p.name[0]) {
                ImVec2 ts = calc_text_size_fixed(cfg::esp::text_size, p.name);
                float nx = (tl.x + br.x - ts.x) * 0.5f;
                float ny = top_stack_y - ts.y;
                draw_pill(dl, {nx, ny}, col_box, p.name);
                top_stack_y = ny - 3.f;
            }
            if (p.isBot && cfg::esp::bot_tag) {
                const char* tag = "BOT";
                ImVec2 ts = calc_text_size_fixed(cfg::esp::text_size, tag);
                float bx = (tl.x + br.x - ts.x) * 0.5f;
                float by = top_stack_y - ts.y;
                draw_pill(dl, {bx, by}, col_bot, tag, 3.f);
                top_stack_y = by - 3.f;
            }

            float bottom_stack_y = br.y + 4.f;
            if (cfg::esp::distance) {
                char buf[24];
                snprintf(buf, sizeof(buf), "%.0f m", p.distance);
                ImVec2 ts = calc_text_size_fixed(cfg::esp::text_size, buf);
                draw_shadowed_text(dl, {(tl.x + br.x - ts.x) * 0.5f, bottom_stack_y}, col_dist, buf, cfg::esp::text_size);
                bottom_stack_y += ts.y + 2.f;
            }
            if (cfg::esp::weapon && p.weaponType >= 0) {
                static const char* kNames[] = {
                    "AR","SNIPER","SMG","SHOTGUN","PISTOL","MELEE","GRENADE","LMG","RIFLE","MARKSMAN","SPECIAL"
                };
                const char* wn = (p.weaponType < (int)(sizeof(kNames)/sizeof(*kNames)))
                                 ? kNames[p.weaponType] : "WPN";
                ImVec2 ts = calc_text_size_fixed(cfg::esp::text_size, wn);
                draw_shadowed_text(dl, {(tl.x + br.x - ts.x) * 0.5f, bottom_stack_y}, col_weapon, wn, cfg::esp::text_size);
            }

            if (cfg::esp::line) {
                dl->AddLine(ImVec2(screen.x * 0.5f, 0.f), ImVec2(cx, tl.y), col_box, 1.5f);
            }

            if (cfg::esp::tracer) {
                dl->AddLine(center, {cx, br.y}, col_tracer, 1.6f);
            }
        }
    }
}
