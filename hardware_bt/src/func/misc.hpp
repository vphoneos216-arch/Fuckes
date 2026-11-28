#pragma once
// Misc tab: No Reload (UI label "Brutal BT"), No Recoil, Ghost Hack --
// ported from the working InvisibleX hook.h (see jni/src/Scarecrow/
// hook.h, "Ghost + NoRecoil + NoReload -- exact port from working
// hook.h"), using the SAME offsets Hardware_bt's own Offsets.h already
// carries (verified against hook.h's literal numeric values, not
// guessed): GhostHack=0x814, PlayerAttributes=0x770, ShootNoReload=
// 0x111, InventoryManager=0x748, Item=0xA0 (hook.h's InventoryActiveItem
// chain link), WeaponComponent=0x80 (hook.h's InventoryWeaponComp chain
// link), NoRecoil=0x18 (hook.h's InventoryNoRecoil float).
//
// These three are continuous poll-and-write patches (same as hook.h's
// own IxUpdateGhost/IxUpdateNoRecoil/IxUpdateNoReload), not hardware-
// breakpoint redirects -- that "breakpoint only, no fallback" rule is
// specific to Bullet Track's hit redirection, not these simple memory
// patches, same as hook.h itself does not breakpoint them either.
#include "../other/memory.hpp"
#include "../game/Offsets.h"
#include "../game/data.hpp"
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

namespace misc {

struct Settings {
    bool no_reload  = false; // UI label: "Brutal BT"
    bool no_recoil  = false;
    bool ghost_hack = false;
    bool ghost_float = false; // show a floating on-screen Ghost Hack badge
};

struct Status {
    std::string text = "OFF";
};

class Controller {
    std::mutex mutex_;
    Settings settings_;
    Status status_;
    std::atomic<bool> running_{true};
    std::thread worker_;

    // NoReload needs to remember the ORIGINAL ShootNoReload byte so it
    // can be restored when the toggle goes off, same as hook.h's
    // IxUpdateNoReload (origSaved/origValue, reset whenever the
    // PlayerAttributes pointer itself changes -- e.g. a new match).
    uint64_t last_attrs_ = 0;
    bool     orig_saved_ = false;
    bool     orig_value_ = false;

    static bool valid(uint64_t p) { return p && p > 0x10000ULL; }

    void update_ghost(uint64_t local, bool on) {
        if (!valid(local)) return;
        // Continuous bool write, exactly hook.h's IxUpdateGhost.
        wpm<bool>(local + Offsets::GhostHack, on);
    }

    void update_no_recoil(uint64_t local, bool on) {
        if (!on || !valid(local)) return;
        // Exact chain from hook.h: invMgr -> activeItem -> wcomp -> float 0.
        uint64_t invMgr = rpm<uint64_t>(local + Offsets::InventoryManager);
        if (!valid(invMgr)) return;
        uint64_t activeItem = rpm<uint64_t>(invMgr + Offsets::Item);
        if (!valid(activeItem)) return;
        uint64_t wcomp = rpm<uint64_t>(activeItem + Offsets::WeaponComponent);
        if (!valid(wcomp)) return;
        wpm<float>(wcomp + Offsets::NoRecoil, 0.0f);
    }

    void update_no_reload(uint64_t local, bool on) {
        if (!valid(local)) return;
        uint64_t attrs = rpm<uint64_t>(local + Offsets::PlayerAttributes);
        if (!valid(attrs)) return;
        if (attrs != last_attrs_) { last_attrs_ = attrs; orig_saved_ = false; orig_value_ = false; }
        if (on) {
            if (!orig_saved_) { orig_value_ = rpm<bool>(attrs + Offsets::ShootNoReload); orig_saved_ = true; }
            wpm<bool>(attrs + Offsets::ShootNoReload, true);
        } else if (orig_saved_) {
            wpm<bool>(attrs + Offsets::ShootNoReload, orig_value_);
            orig_saved_ = false;
        }
    }

    void loop() {
        while (running_.load()) {
            const auto begin = std::chrono::steady_clock::now();
            Settings s; { std::lock_guard<std::mutex> lk(mutex_); s = settings_; }
            const uint64_t local = data::localPlayerAddr.load();
            const bool ready = data::inMatch.load() && valid(local);
            {
                std::lock_guard<std::mutex> lk(mutex_);
                status_.text = !ready ? "Waiting for match"
                             : (s.no_reload || s.no_recoil || s.ghost_hack) ? "Active" : "OFF";
            }
            if (ready) {
                update_ghost(local, s.ghost_hack);
                update_no_recoil(local, s.no_recoil);
                update_no_reload(local, s.no_reload);
            } else {
                // No local player this tick: NoReload's save/restore state
                // must not carry over into whatever match comes next.
                last_attrs_ = 0; orig_saved_ = false; orig_value_ = false;
            }
            std::this_thread::sleep_until(begin + std::chrono::milliseconds(50));
        }
    }

public:
    Controller() { worker_ = std::thread([this] { loop(); }); }
    ~Controller() { running_.store(false); if (worker_.joinable()) worker_.join(); }

    Settings settings() { std::lock_guard<std::mutex> lk(mutex_); return settings_; }
    Status   status()   { std::lock_guard<std::mutex> lk(mutex_); return status_; }
    void configure(Settings s) { std::lock_guard<std::mutex> lk(mutex_); settings_ = s; }
};

inline Controller& controller() { static Controller instance; return instance; }

} // namespace misc
