#pragma once
#include "release_log.hpp"

#include <cmath>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <cstdarg>
#include <cstring>
#include <unistd.h>
#include <android/log.h>

#include "driver.h"
#include "kernel.h"
#include "../other/memory.hpp"
#include "../other/vector3.h"
#include "../game/Offsets.h"
#include "../game/math.hpp"
#include "../game/data.hpp"
#include "../ui/cfg.hpp"
#include "../protect/security.hpp"
#include "imgui.h"

#define HWBP_LOG(...) TB_ANDROID_LOG(ANDROID_LOG_INFO, "NB9_HWBP", __VA_ARGS__)

namespace hwbp {

    static inline bool is_valid_ptr(uint64_t p) {
        return (p >= 0x10000 && p < 0x00007FFFFFFFFFFFULL);
    }

    // Complete Debug Telemetry & Event Logging
    struct DebugInfo {
        std::atomic<bool>     is_active{false};
        std::atomic<bool>     is_firing{false};
        std::atomic<bool>     is_scoping{false};
        std::atomic<bool>     bp_suspended{false};
        std::atomic<pid_t>    target_pid{0};
        std::atomic<uint64_t> lib_base{0};
        std::atomic<uint64_t> local_player{0};
        std::atomic<uint64_t> watch_addr{0};
        std::atomic<size_t>   attached_threads{0};

        std::atomic<uint64_t> total_traps{0};
        std::atomic<uint64_t> hits_applied{0};
        std::atomic<uint64_t> single_steps{0};

        std::atomic<uint64_t> last_pc{0};
        std::atomic<uint64_t> last_rel_pc{0};
        std::atomic<uint64_t> last_si_addr{0};
        std::atomic<int>      last_si_code{0};
        std::atomic<pid_t>    last_hit_tid{0};

        std::atomic<uint64_t> last_target_addr{0};
        std::atomic<float>    last_target_dist{0.f};
        std::atomic<float>    last_pitch{0.f};
        std::atomic<float>    last_yaw{0.f};

        std::mutex            log_mutex;
        std::vector<std::string> recent_logs;

        void log(const char* fmt, ...) {
#if TOUCHBASE_DIAGNOSTICS
            char buf[384];
            va_list args;
            va_start(args, fmt);
            vsnprintf(buf, sizeof(buf), fmt, args);
            va_end(args);

            HWBP_LOG("%s", buf);

            std::lock_guard<std::mutex> lk(log_mutex);
            if (recent_logs.size() >= 30) {
                recent_logs.erase(recent_logs.begin());
            }
            recent_logs.push_back(buf);
#endif
        }

        void clear_logs() {
            std::lock_guard<std::mutex> lk(log_mutex);
            recent_logs.clear();
        }
    };

    inline DebugInfo g_debug;

    // Target tracking for UI overlay
    struct Target {
        uint64_t addr{0};
        Vector3  targetScreen{};
        float    distance{0.f};
        float    fovDelta{0.f};
    };
    inline Target     g_target{ 0, {}, 0.f, 0.f };
    inline std::mutex g_target_mutex;

    // Running state
    inline std::atomic<bool> g_run{false};
    inline std::thread       g_worker_thread;

    // Scoping / ADS detection: checks ActiveUISightingWeapon (0x608) and FOV zoom
    static inline bool is_player_scoping(uint64_t lp) {
        if (!is_valid_ptr(lp)) return false;
        uint64_t sighting = rpm<uint64_t>(lp + Offsets::Player_ActiveUISightingWeapon);
        if (is_valid_ptr(sighting)) return true;

        if (data::viewMatrix.m22 > 0.001f) {
            float fovDeg = 2.0f * atanf(1.0f / data::viewMatrix.m22) * 57.2957795f;
            if (fovDeg > 5.0f && fovDeg < 54.0f) return true;
        }
        return false;
    }

    // Fast target selector using cached PlayerData
    static inline uint64_t select_target(Vector3& outTargetWorld, Vector3& outTargetScreen,
                                         float& outDist, float& outFov) {
        float fov  = cfg::aim::visible_fov;
        float maxd = cfg::aim::visible_max;
        float cx = g_sw * 0.5f;
        float cy = g_sh * 0.5f;
        float bestFov = fov;
        uint64_t bestAddr = 0;

        std::lock_guard<std::mutex> lk(data::players_mutex);
        for (const auto& p : data::players) {
            if (!p.addr || p.curHP <= 0) continue;
            if (!p.isVisible) continue;
            if (p.isKnocked && !cfg::aim::target_knocked) continue;
            if (p.distance > maxd) continue;

            Vector3 targetWorld = p.getTargetWorld(cfg::aim::target_position);
            if (targetWorld == Vector3::Zero()) continue;

            Vector3 targetScreen = WorldToScreenPoint(data::viewMatrix, targetWorld);
            if (targetScreen.x < 1 || targetScreen.y < 1) continue;

            float dx = targetScreen.x - cx;
            float dy = targetScreen.y - cy;
            float d  = sqrtf(dx * dx + dy * dy);
            if (d > fov) continue;

            if (d < bestFov) {
                bestFov = d;
                bestAddr = p.addr;
                outTargetScreen = targetScreen;
                outTargetWorld  = targetWorld;
                outDist = p.distance;
                outFov = d;
            }
        }
        return bestAddr;
    }

    // Hardware Breakpoint worker loop using Desktop/kernel.h NB9 Driver
    static void hwbp_worker_loop() {
        g_debug.log("[NB9_HWBP] Kernel Hardware Breakpoint engine started");

        uint64_t activeWatchAddr = 0;
        pid_t activePid = 0;
        bool bpArmed = false;
        bool bpSuspendedForScope = false;

        auto clear_kernel_bp = [&](pid_t pid, uintptr_t addr) {
            if (driver) {
                if (pid > 0 && addr > 0) {
                    driver->DisableHwBp(pid, addr);
                }
                driver->UpdateTrackingData(false, 0, 0, 0, 0);
                driver->ClearHwBp();
            }
            bpArmed = false;
            bpSuspendedForScope = false;
            g_debug.is_active.store(false);
            g_debug.bp_suspended.store(false);
            g_debug.attached_threads.store(0);
            g_debug.log("[NB9_HWBP] Kernel HWBP disarmed");
        };

        while (g_run.load(std::memory_order_relaxed)) {
            // Prerequisites check
            if (!cfg::aim::visible_enabled || proc::pid <= 0 || proc::lib == 0) {
                if (bpArmed) {
                    clear_kernel_bp(activePid, activeWatchAddr);
                    activeWatchAddr = 0;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }

            pid_t gamePid = proc::pid;
            uint64_t libBase = proc::lib; // libil2cpp.so base
            uint64_t localPlayer = data::localPlayerAddr.load();

            // Use the selected version's aim-rotation field.
            uint64_t desiredWatchAddr = localPlayer ? (localPlayer + Offsets::AimRotation) : 0;

            g_debug.target_pid.store(gamePid);
            g_debug.lib_base.store(libBase);
            g_debug.local_player.store(localPlayer);
            g_debug.watch_addr.store(desiredWatchAddr);

            if (!localPlayer || desiredWatchAddr == 0 || !driver) {
                if (bpArmed) {
                    clear_kernel_bp(activePid, activeWatchAddr);
                    activeWatchAddr = 0;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            // If target PID or watch address changed, arm kernel hardware breakpoint
            if (gamePid != activePid || desiredWatchAddr != activeWatchAddr || !bpArmed) {
                if (bpArmed) {
                    clear_kernel_bp(activePid, activeWatchAddr);
                }

                activePid = gamePid;
                activeWatchAddr = desiredWatchAddr;

                g_debug.log("[NB9_HWBP] Arming Kernel HWBP: PID=%d, libil2cpp=0x%lx, localPlayer=0x%lx, watchAddr=0x%lx (selected aim field)",
                            activePid, (unsigned long)libBase, (unsigned long)localPlayer, (unsigned long)activeWatchAddr);

                nb9::HW_BP_INFO bp{};
                bp.pid = activePid;
                bp.addr = activeWatchAddr;
                bp.type = HW_BP_TYPE_W; // Hardware watchpoint on write (or HW_BP_TYPE_RW)
                bp.len = 4;

                // HWBP watchpoint on write (do not clobber FP registers on initial arm)
                bp.is_write_fp_regs = false;
                bp.fp_reg_count = 0;

                bool okAdd = driver->AddHwBp(&bp);
                bool okEn  = driver->UpdateAndEnableHwBp(&bp);
                bpArmed = (okAdd || okEn);

                g_debug.is_active.store(bpArmed);
                g_debug.attached_threads.store(bpArmed ? 1 : 0);
                g_debug.log("[NB9_HWBP] Kernel HWBP Arm result: Add=%s, Enable=%s",
                            okAdd ? "OK" : "ERR", okEn ? "OK" : "ERR");
            }

            static uint32_t s_aim_check_ticker = 0;
            if ((++s_aim_check_ticker & 0xFF) == 0) {
                if (!sec::Security::DistributedCheck(4) || sec::Security::IsTampered()) {
                    Offsets::Nullify();
                }
            }

            // 0. Detect Scoping / ADS state & apply crash safeguard
            bool isScoping = is_player_scoping(localPlayer);
            g_debug.is_scoping.store(isScoping);

            if (cfg::aim::scope_safeguard) {
                if (isScoping) {
                    if (bpArmed && !bpSuspendedForScope) {
                        driver->DisableHwBp(activePid, activeWatchAddr);
                        bpSuspendedForScope = true;
                        g_debug.bp_suspended.store(true);
                        g_debug.log("[NB9_HWBP] ADS Scope detected -> HWBP safely suspended (Crash Safeguard active)");
                    }
                } else {
                    if (bpArmed && bpSuspendedForScope) {
                        nb9::HW_BP_INFO bp_re{};
                        bp_re.pid = activePid;
                        bp_re.addr = activeWatchAddr;
                        bp_re.type = HW_BP_TYPE_W;
                        bp_re.len = 4;
                        bp_re.is_write_fp_regs = false;
                        bp_re.fp_reg_count = 0;
                        driver->UpdateAndEnableHwBp(&bp_re);
                        bpSuspendedForScope = false;
                        g_debug.bp_suspended.store(false);
                        g_debug.log("[NB9_HWBP] ADS Scope released -> HWBP resumed");
                    }
                }
            } else {
                if (bpSuspendedForScope) {
                    bpSuspendedForScope = false;
                    g_debug.bp_suspended.store(false);
                }
            }

            // Select best enemy target within FOV
            Vector3 targetWorld = Vector3::Zero(), targetScreen = Vector3::Zero();
            float dist = 0.f, fov = 0.f;
            uint64_t enemy = select_target(targetWorld, targetScreen, dist, fov);

            // Read firing condition from localPlayer + Offsets::isFiring (0x850)
            int fireVal = rpm<int>(localPlayer + Offsets::isFiring);
            uint8_t fireByte = rpm<uint8_t>(localPlayer + Offsets::isFiring);
            bool isFiring = (fireVal > 0) || (fireByte > 0);
            g_debug.is_firing.store(isFiring);

            if (enemy && !(targetWorld == Vector3::Zero())) {
                std::lock_guard<std::mutex> lk(g_target_mutex);
                g_target = { enemy, targetScreen, dist, fov };
            } else {
                std::lock_guard<std::mutex> lk(g_target_mutex);
                g_target = { 0, {}, 0.f, 0.f };
            }

            // Only activate hardware breakpoint register overwrite when actively firing
            if (enemy && !(targetWorld == Vector3::Zero()) && isFiring) {

                // 1. Get accurate local player position (Head / Eye origin)
                uint64_t localHeadTF = GetPlayerHeadTF(localPlayer);
                Vector3 localPos = is_valid_ptr(localHeadTF) ? GetNodePosition(localHeadTF) : Vector3::Zero();
                if (localPos == Vector3::Zero()) {
                    uint64_t localPeTF = GetPlayerPeTF(localPlayer);
                    if (is_valid_ptr(localPeTF)) localPos = GetNodePosition(localPeTF);
                }

                // 2. Resolve true Camera Origin (eliminates third-person shoulder parallax error)
                // In Free Fire, third-person camera is placed behind the player and to the right shoulder (+0.45m).
                // Aiming from the head causes the camera crosshair to land slightly to the right of the target.
                // Using the actual camera origin solves parallax completely.
                Vector3 camPos = GetCameraPositionFromMatrix(data::viewMatrix);
                if (camPos == Vector3::Zero() || (localPos != Vector3::Zero() && calculate_distance(camPos, localPos) > 15.0f)) {
                    // Try MainCameraTransform (0x3F0)
                    uint64_t camTF = rpm<uint64_t>(localPlayer + Offsets::MainCameraTransform);
                    if (is_valid_ptr(camTF)) {
                        Vector3 cPos = GetPosition(camTF);
                        if (cPos != Vector3::Zero() && (localPos == Vector3::Zero() || calculate_distance(cPos, localPos) < 15.0f)) {
                            camPos = cPos;
                        }
                    }
                }
                if (camPos == Vector3::Zero() || (localPos != Vector3::Zero() && calculate_distance(camPos, localPos) > 15.0f)) {
                    // Try FollowCamera CachedCameraTransform (0x698 -> 0x40)
                    uint64_t followCam = rpm<uint64_t>(localPlayer + Offsets::FollowCamera);
                    if (is_valid_ptr(followCam)) {
                        uint64_t cachedTF = rpm<uint64_t>(followCam + 0x40);
                        if (is_valid_ptr(cachedTF)) {
                            Vector3 cPos = GetPosition(cachedTF);
                            if (cPos != Vector3::Zero() && (localPos == Vector3::Zero() || calculate_distance(cPos, localPos) < 15.0f)) {
                                camPos = cPos;
                            }
                        }
                    }
                }

                // 3. Aim Origin: prefer true camera position for perfect crosshair alignment
                Vector3 fromPos = (camPos != Vector3::Zero()) ? camPos : localPos;
                if (fromPos == Vector3::Zero()) {
                    uint64_t hitObjAddress = rpm<uint64_t>(localPlayer + Offsets::aimInfo);
                    Vector3 ammoBase = (hitObjAddress != 0) ? rpm<Vector3>(hitObjAddress + Offsets::startPos) : Vector3::Zero();
                    fromPos = (ammoBase != Vector3::Zero()) ? ammoBase : targetWorld;
                } else if (camPos == Vector3::Zero() && localPos != Vector3::Zero()) {
                    // Fallback: If camera position is unavailable, compensate for right shoulder offset from localPos
                    uint64_t followCam = rpm<uint64_t>(localPlayer + Offsets::FollowCamera);
                    float shoulderOffset = 0.45f;
                    if (is_valid_ptr(followCam)) {
                        float rOff = rpm<float>(followCam + 0x78);
                        if (rOff > 0.05f && rOff < 1.5f) shoulderOffset = rOff;
                    }
                    Vector3 initialDir = (targetWorld - localPos).normalized();
                    Vector3 rightVec = Vector3::Cross(Vector3::Up(), initialDir).normalized();
                    fromPos = localPos + (rightVec * shoulderOffset);
                }

                // 4. Target position with optional head (vertical) and horizontal bias
                Vector3 targetPos = targetWorld;
                if (cfg::aim::head_bias != 0.0f) {
                    targetPos.y += cfg::aim::head_bias;
                }
                if (cfg::aim::horizontal_bias != 0.0f) {
                    Vector3 aimDir = (targetPos - fromPos).normalized();
                    Vector3 rightVec = Vector3::Cross(Vector3::Up(), aimDir).normalized();
                    targetPos = targetPos + (rightVec * cfg::aim::horizontal_bias);
                }

                // 5. 3D Delta vector in Unity coordinates (X=Right, Y=Up, Z=Forward)
                Vector3 dir = {
                    targetPos.x - fromPos.x,
                    targetPos.y - fromPos.y,
                    targetPos.z - fromPos.z
                };

                // 6. Unity Euler angles (degrees)
                float groundDist = sqrtf(dir.x * dir.x + dir.z * dir.z);
                float newYaw   = atan2f(dir.x, dir.z) * 57.2957795f;        // [-180, 180]
                float newPitch = -atan2f(dir.y, groundDist) * 57.2957795f; // [-89, 89]

                // 7. Exact Unity LookRotation Quaternion (16 bytes: x, y, z, w)
                Quaternion targetQuat = Quaternion::LookRotation(dir);

                // 8. Update kernel driver tracking coordinates (World 3D target coordinates)
                driver->UpdateTrackingData(true, activeWatchAddr,
                                           targetPos.x, targetPos.y, targetPos.z);

                // 9. Pass accurate Euler angles to PTE Float3 hook interface
                driver->PteUpdateFloat3(newPitch, newYaw, 0.0f);

                // 10. Update memory aim rotation at localPlayer + 0x61C
                Quaternion curQ = driver->read<Quaternion>(activeWatchAddr);
                float qLenSq = curQ.x * curQ.x + curQ.y * curQ.y + curQ.z * curQ.z + curQ.w * curQ.w;
                bool isQuat = (fabsf(qLenSq - 1.0f) < 0.25f) || (qLenSq == 0.0f);

                if (isQuat) {
                    driver->write<Quaternion>(activeWatchAddr, targetQuat);
                } else {
                    Vector3 rotEuler = { newPitch, newYaw, 0.0f };
                    driver->write<Vector3>(activeWatchAddr, rotEuler);
                }

                // Also update m_CurrentAimRotation at localPlayer + 0x1AA4 if valid
                if(Offsets::CurrentAimRotation)driver->write<Quaternion>(localPlayer + Offsets::CurrentAimRotation, targetQuat);

                // 11. Update HWBP with target quaternion registers (hip-fire only; ADS scope uses direct memory/PTE aim)
                if (bpArmed && !bpSuspendedForScope) {
                    nb9::HW_BP_INFO bp_up{};
                    bp_up.pid = activePid;
                    bp_up.addr = activeWatchAddr;
                    bp_up.type = HW_BP_TYPE_W;
                    bp_up.len = 4;
                    bp_up.is_write_fp_regs = true;
                    bp_up.fp_reg_count = 4;
                    bp_up.fp_reg_indices[0] = 0; // S0
                    bp_up.fp_reg_indices[1] = 1; // S1
                    bp_up.fp_reg_indices[2] = 2; // S2
                    bp_up.fp_reg_indices[3] = 3; // S3

                    uint32_t bx, by, bz, bw;
                    memcpy(&bx, &targetQuat.x, 4);
                    memcpy(&by, &targetQuat.y, 4);
                    memcpy(&bz, &targetQuat.z, 4);
                    memcpy(&bw, &targetQuat.w, 4);
                    bp_up.fp_reg_values[0][0] = bx;
                    bp_up.fp_reg_values[1][0] = by;
                    bp_up.fp_reg_values[2][0] = bz;
                    bp_up.fp_reg_values[3][0] = bw;
                    driver->UpdateAndEnableHwBp(&bp_up);
                }

                // 12. Query kernel HWBP hit statistics
                nb9::HWBP_HIT_ITEM items[4];
                nb9::HWBP_HIT_ARGS hitArgs{};
                hitArgs.pid = activePid;
                hitArgs.addr = activeWatchAddr;
                hitArgs.out_buf = items;
                hitArgs.out_len = 4;
                hitArgs.real_count = 0;

                if (driver->GetHwBpHits(&hitArgs) && hitArgs.real_count > 0) {
                    for (int i = 0; i < hitArgs.real_count; i++) {
                        g_debug.total_traps.fetch_add(1, std::memory_order_relaxed);
                        g_debug.hits_applied.fetch_add(1, std::memory_order_relaxed);
                        g_debug.last_pc.store(items[i].regs_info.pc);
                        g_debug.last_hit_tid.store(items[i].task_id);
                        uint64_t rpc = (items[i].regs_info.pc >= libBase) ? (items[i].regs_info.pc - libBase) : 0;
                        g_debug.last_rel_pc.store(rpc);

                        g_debug.log("[NB9_HWBP] Kernel Hit! tid=%d pc=0x%lx (+0x%lx) Pitch=%.2f Yaw=%.2f",
                                    items[i].task_id, (unsigned long)items[i].regs_info.pc, (unsigned long)rpc,
                                    newPitch, newYaw);
                    }
                }

                g_debug.last_target_addr.store(enemy);
                g_debug.last_target_dist.store(dist);
                g_debug.last_pitch.store(newPitch);
                g_debug.last_yaw.store(newYaw);

                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } else {
                // No target in FOV or not firing - idle tracking
                driver->UpdateTrackingData(false, activeWatchAddr, 0, 0, 0);
                if (bpArmed && !bpSuspendedForScope) {
                    nb9::HW_BP_INFO bp_idle{};
                    bp_idle.pid = activePid;
                    bp_idle.addr = activeWatchAddr;
                    bp_idle.type = HW_BP_TYPE_W;
                    bp_idle.len = 4;
                    bp_idle.is_write_fp_regs = false;
                    driver->UpdateAndEnableHwBp(&bp_idle);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        // Cleanup upon thread stop
        clear_kernel_bp(activePid, activeWatchAddr);
        g_debug.log("[NB9_HWBP] Worker thread stopped");
    }

    inline void thread_start() {
        if (g_run.exchange(true)) return;
        g_worker_thread = std::thread(hwbp_worker_loop);
    }

    inline void thread_stop() {
        if (!g_run.exchange(false)) return;
        if (g_worker_thread.joinable()) g_worker_thread.join();
    }

    inline void draw_overlay() {
        if (!cfg::aim::visible_enabled) return;

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 center(g_sw * 0.5f, g_sh * 0.5f);

        if (cfg::aim::fov_circle) {
            ImU32 c = ImGui::ColorConvertFloat4ToU32(cfg::aim::fov_col);
            dl->AddCircle(center, cfg::aim::visible_fov, c, 64, 1.5f);
        }

        Target t;
        {
            std::lock_guard<std::mutex> lk(g_target_mutex);
            t = g_target;
        }
        if (t.addr && t.targetScreen.x > 1) {
            ImVec2 pos(t.targetScreen.x, t.targetScreen.y);
            dl->AddCircle(pos, 12.f, IM_COL32(255, 60, 60, 220), 24, 2.f);
            dl->AddCircleFilled(pos, 3.f, IM_COL32(255, 255, 255, 255));
        }
    }
}

// Namespace alias for backward compatibility
namespace silent = hwbp;
