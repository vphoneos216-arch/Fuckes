#include "ui/bar.hpp"
// ============================================
// main.cpp - Full Code (Player Counter + Control System)
// ============================================
#include "Login.h"
#include "driver.h"
#include "Android_draw/draw.h"
#include "ui/styles/styles.h"
#include "ui/menu.hpp"
#include "other/memory.hpp"
#include "game/data.hpp"
#include "func/esp.hpp"
#include "func/silent.hpp"
#include "func/volume_key.hpp"
#include "ui/cfg.hpp"
#include "protect/security.hpp"
#include <cstdio>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <unistd.h>
#include <android/log.h>
#include <sys/stat.h>
#include <cstring>
#include <imgui.h>

#define LOG(...) ((void)0)

static Authentication g_auth;

// ============================================
// Enemy Counter Draw
// ============================================
static void DrawEnemyCounter() {
    // Replaced by KernelSRC Dynamic Island pill in bar::render()
}


// ============================================
// Launch target
// ============================================
static bool select_version() {
    std::puts("\nSelect game version:");
    for(int i=1;i<=4;++i){
        const auto* profile=versions::Get(i);
        std::printf("  %d) %-11s - %s  [%s]\n",i,profile->label,profile->version,profile->package);
    }
    char line[64];
    while(true){
        std::printf("Version [1-4]: ");std::fflush(stdout);
        if(!std::fgets(line,sizeof(line),stdin)){std::puts("\nNo version selected; exiting.");return false;}
        if(!std::strchr(line,'\n')&&!std::feof(stdin)){
            int c;while((c=std::getchar())!='\n'&&c!=EOF){}
            std::puts("Enter one number from 1 to 4.");continue;
        }
        int choice=versions::ParseChoice(line);
        if(!choice){std::puts("Enter one number from 1 to 4.");continue;}
        if(!versions::Apply(choice)){std::puts("Cannot apply version offsets.");return false;}
        std::printf("Selected %s - %s\n",versions::selected->label,versions::selected->version);
        if(TOUCHBASE_DIAGNOSTICS && choice!=1)std::puts("Missing fields reuse MAX INDIA values marked NOT VERIFIED in VersionProfiles.hpp.");
        return true;
    }
}

// Asked right after driver selection, same CLI prompt style as
// select_version()/driver_selection::prompt(): which shape the top-of-
// screen HUD pill uses -- Bar (the existing horizontal pill) or Volume
// (a vertical rounded slider look). Stored in cfg::hud::counter_style
// for bar::render() to branch on; deliberately not persisted to the
// saved config file (see cfg.hpp), so this is asked fresh every launch.
static void select_counter_style() {
    std::puts("\nHUD style:");
    std::puts("  1) Bar    (horizontal pill, top-center)");
    std::puts("  2) Volume (vertical slider, top-center)");
    char line[64];
    while (true) {
        std::printf("Style [1-2]: "); std::fflush(stdout);
        if (!std::fgets(line, sizeof(line), stdin)) { cfg::hud::counter_style = 0; return; }
        if (!std::strchr(line, '\n') && !std::feof(stdin)) {
            int c; while ((c = std::getchar()) != '\n' && c != EOF) {}
            std::puts("Enter 1 or 2.");
            continue;
        }
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '1') { cfg::hud::counter_style = 0; return; }
        if (*p == '2') { cfg::hud::counter_style = 1; return; }
        std::puts("Enter 1 or 2.");
    }
}

static bool launch_target() {
    // Package is selected from the compiled profile table, never shell input.
    const std::string command=std::string("monkey -p ")+versions::selected->package+
        " -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1";
    if(std::system(command.c_str())!=0){
        std::fprintf(stderr,"Unable to launch %s. Check that the selected version is installed.\n",versions::selected->package);
        return false;
    }
    return true;
}

static void delete_client_sh() {
    // Disabled self-deletion so the binary persists in /data/local/tmp/
    /*
    std::this_thread::sleep_for(std::chrono::seconds(3));
    std::string targetPath = SEC_STR("/data/local/tmp/client.sh");
    struct stat st;
    if (stat(targetPath.c_str(), &st) == 0) {
        remove(targetPath.c_str());
    }
    system(SEC_STR("sleep 2; rm -f /data/local/tmp/client.sh >/dev/null 2>&1 &").c_str());
    */
}

// ============================================
// Main
// ============================================
int main() {
    // 1. Initialize Security Subsystem (anti-trace, anti-dump, golden hash, watchdog)
    sec::Security::Init();

    // 2. Authenticate session
    if (!g_auth.run()) {
        return 0;
    }

    // Account info for the menu's Info page -- read once, right after
    // the server round-trip in g_auth.run() succeeds.
    cfg::account::license_key  = g_auth.getLicenseKey();
    cfg::account::seller_name  = g_auth.getSellerName();
    cfg::account::expiry_date  = g_auth.getExpiryDate();
    cfg::account::status       = g_auth.getStatus();
    cfg::account::days_remain  = g_auth.getDaysRemain();
    cfg::account::remain_secs  = g_auth.getRemainSeconds();
    cfg::account::devices_used = g_auth.getDevicesUsed();
    cfg::account::devices_max  = g_auth.getMaxDevices();

    // Bind this project's own security subsystem to the real login that
    // just happened. sec::Security::IsSessionAuthenticated() was always
    // false before this -- nothing ever called AuthenticateSession() for
    // the new Authentication class -- so every run fell into the branch
    // below, nullified Offsets::, and printed "Cannot apply version
    // offsets." (and, downstream, broke HWBPY, which reads those same
    // offsets). getSessionToken() returns the server-issued token only
    // when g_auth.run() actually authenticated, so this call is exactly
    // as strict as the check it feeds.
    sec::Security::AuthenticateSession(g_auth.getSessionToken());

    if (!sec::Security::IsSessionAuthenticated()) {
        sec::Security::TriggerTamperAction();
        Offsets::Nullify();
    }

    if(!select_version())return 1;

    static c_driver drv;
    driver = &drv;

    select_counter_style();
    // Volume style has no on-screen tap target for the menu at all --
    // the physical Volume Up key is the only open/close control in that
    // mode (see func/volume_key.hpp). Bar style keeps the existing
    // tap-the-pill toggle in bar.cpp, so this listener is never started
    // for it.
    if (cfg::hud::counter_style == 1) volume_key::start();

    screen_config();
    int max_size = (displayInfo.height > displayInfo.width
                    ? displayInfo.height : displayInfo.width);
    int min_size = (displayInfo.height < displayInfo.width
                    ? displayInfo.height : displayInfo.width);

    g_sw = static_cast<float>(max_size);
    g_sh = static_cast<float>(min_size);
    native_window_screen_x = max_size;
    native_window_screen_y = min_size;

    if (!initGUI_draw(native_window_screen_x, native_window_screen_y, true)) {
        return -1;
    }

    if(!launch_target())return 1;
    touch::init(max_size, min_size, (uint8_t)displayInfo.orientation);
    std::thread(delete_client_sh).detach();

    game::init();
    data::thread_start();
    silent::thread_start();

    // 🟢 Load saved configuration from /data/local/tmp/neoFF.c
    cfg::load_config();

    static uint32_t s_frame_ticker = 0;

    while (true) {
        if ((++s_frame_ticker % 300) == 0) {
            sec::Security::DistributedCheck(2);
        }

        auto frame_start = std::chrono::steady_clock::now();
        drawBegin();
        bool run = game::valid();
        ui::menu::render();

        // Enemy counter
        DrawEnemyCounter();

        drawEnd();
        int target_fps = cfg::settings::get_target_fps();
        if (target_fps > 0) {
            int64_t target_us = 1000000 / target_fps;
            auto frame_end = std::chrono::steady_clock::now();
            int64_t elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                frame_end - frame_start).count();
            if (elapsed_us < target_us) {
                usleep(static_cast<useconds_t>(target_us - elapsed_us));
            }
        } else {
            usleep(1000);
        }
    }
    return 0;
}