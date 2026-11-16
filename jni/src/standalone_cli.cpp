/* ============================================================
   standalone_cli.cpp — InvisibleX standalone launcher.

   Runs the exact same optimum_bar binary and the exact same login
   security stack (login.h) as the KernelSU/Magisk/APatch module does,
   with NO module installed at all. Everything it needs sits next to
   its own executable:

       inx_cli                         <- this binary
       api/arm64-v8a/inx_login_@api    <- same login binary the module ships
       bin/arm64-v8a/optimum_bar       <- same overlay binary the module ships
       drivers/paradise_old/*.ko.sh    <- same driver scripts the module ships
       drivers/paradise_new/*.ko.sh

   Nothing here re-implements or shortcuts authentication:
     - Step 1 shells out to the real inx_login_@api binary for the
       verify-and-show-result step, same server round trip, same
       login.h (sealed transport, FNV-checked endpoint, redundant
       tri-state auth flags, ptrace/Frida checks, integrity watchdog).
     - Step 4 then hands the same key to optimum_bar's own argv[1],
       which performs its OWN independent run_silent() call -- this is
       the existing "dual login" contract main.cpp already documents
       (daemon_service.cpp launches it the same way), not something
       invented for this binary.
   Driver loading (chmod/chown/fork+exec a .ko.sh) is the same
   sequence daemon_service.cpp's load_driver() uses, reproduced here
   rather than shared through a header because the two files have no
   other dependency on each other and copying ~30 lines is lower risk
   than introducing one.

   Usage:  inx_cli
   (interactive: key -> driver -> bar/vol -> runs optimum_bar in the
   foreground, inheriting this terminal)
   ============================================================ */

#include "login.h"   // kd_exe_dir() -- resolves paths relative to THIS binary
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <json.hpp>

using json = nlohmann::json;

static std::string g_base;   // this binary's own directory

static std::string login_bin()  { return g_base + "/api/arm64-v8a/inx_login_@api"; }
static std::string overlay_bin(){ return g_base + "/bin/arm64-v8a/optimum_bar"; }
static std::string driver_dir() { return g_base + "/drivers"; }

static void banner() {
    std::cout <<
        "============================================\n"
        "   InvisibleX  -  Standalone Launcher\n"
        "   (no module required)\n"
        "============================================\n\n";
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// -------------------------------------------------------------
//  Step 1 — license key, verified by the REAL inx_login_@api
//  binary (same process, same login.h stack the module uses).
//  Returns the verified key on success, empty string on failure.
// -------------------------------------------------------------
static std::string step_login() {
    const std::string bin = login_bin();
    if (access(bin.c_str(), F_OK) != 0) {
        std::cerr << "[ERROR] Login binary not found next to this program:\n  "
                   << bin << "\n"
                   << "Make sure inx_cli, api/, bin/ and drivers/ were all\n"
                   << "extracted together into the same folder.\n";
        return "";
    }
    chmod(bin.c_str(), 0755);

    for (;;) {
        std::cout << "Step 1 - License Key\nEnter your key: ";
        std::string key;
        if (!std::getline(std::cin, key)) return "";
        key = trim(key);
        if (key.empty()) {
            std::cout << "[ERROR] Key cannot be empty.\n\n";
            continue;
        }

        std::cout << "Verifying with server...\n";

        // Run the real login binary and capture its one JSON line, same
        // contract inxlaunch.sh already relies on (status/message/...).
        const std::string cmd = "\"" + bin + "\" \"" + key + "\" 2>/dev/null";
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) {
            std::cerr << "[ERROR] Could not run login binary: "
                       << strerror(errno) << "\n";
            return "";
        }
        std::string out;
        char buf[512];
        while (fgets(buf, sizeof(buf), p)) out += buf;
        const int rc = pclose(p);

        json result;
        bool parsed = false;
        try { result = json::parse(out); parsed = true; } catch (...) {}

        if (rc == 0 && parsed && result.value("status", "") == "success") {
            std::cout << "\n== LOGIN SUCCESS ==\n"
                       << result.value("message", "Key verified.") << "\n\n";
            return key;
        }

        std::string why = parsed ? result.value("message", "") : "";
        if (why.empty()) why = "Login failed. Check your key and internet connection.";
        std::cout << "\n== LOGIN FAILED ==\nReason: " << why << "\n\n";

        std::cout << "Try again? [y/N]: ";
        std::string again;
        std::getline(std::cin, again);
        if (again.empty() || (again[0] != 'y' && again[0] != 'Y')) return "";
    }
}

// -------------------------------------------------------------
//  Step 2 — driver selection. Names/aliases match ix_driver_init.h
//  exactly, so whatever is picked here is guaranteed understood by
//  optimum_bar when passed through IX_DRIVER_NAME.
// -------------------------------------------------------------
struct DriverOpt { const char* num; const char* name; const char* label; };
static const DriverOpt kDrivers[] = {
    {"1",  "auto",     "Auto (let the binary choose)"},
    {"2",  "qx1",      "QX1"},
    {"3",  "gt",       "GT"},
    {"4",  "rt-dev",   "RT (dev)"},
    {"5",  "rt-proc",  "RT (proc)"},
    {"6",  "qx2",      "QX2"},
    {"7",  "tb-kpm",   "TB KPM"},
    {"8",  "ditpro",   "DitPro KPM"},
    {"9",  "kma",      "KMA KPM"},
    {"10", "ldk",      "LDK KPM"},
    {"11", "paradise", "Paradise (recommended)"},
    {"12", "bing",     "Bing"},
    {"13", "ditkpm",   "Dit KPM"},
    {"14", "rthook",   "RT Hook"},
};

// Same kernel-version bracket check service.sh's "load on boot" path
// uses for paradise (5.10-6.18), applied here to pick old/new + the
// matching .ko.sh instead of just logging which bracket it's in.
static std::string kernel_release() {
    std::string out;
    FILE* p = popen("uname -r 2>/dev/null", "r");
    if (p) {
        char buf[256];
        if (fgets(buf, sizeof(buf), p)) out = trim(buf);
        pclose(p);
    }
    return out;
}

// Runs the chosen .ko.sh exactly the way daemon_service.cpp's
// load_driver() does: root-owned 0755 (never leave a just-downloaded
// driver script world-writable right before running it as root),
// detached fork+exec, half a second for it to initialise.
static void load_paradise_driver() {
    const std::string rel = kernel_release();
    if (rel.empty()) {
        std::cout << "[WARN] Could not read kernel version (uname -r) -- "
                     "skipping automatic Paradise driver load.\n"
                     "       Load it yourself if needed, then continue.\n";
        return;
    }
    // "6.1" -> major=6, minor=1 (same parse service.sh does with cut/tr)
    int major = 0, minor = 0;
    sscanf(rel.c_str(), "%d.%d", &major, &minor);
    const int kv = major * 1000 + minor;

    // Same bracket as service.sh: 5.10-6.18 for paradise_new's actual
    // shipped files; paradise_old only goes up to 6.6.
    std::string sub, file;
    if (kv >= 5010 && kv <= 6006)      { sub = "paradise_old"; }
    else if (kv >= 5010 && kv <= 6018) { sub = "paradise_new"; }
    else {
        std::cout << "[WARN] Kernel " << rel << " is outside the Paradise "
                     "driver's supported range (5.10-6.18) -- skipping.\n";
        return;
    }
    char verbuf[32];
    snprintf(verbuf, sizeof(verbuf), "%d.%d", major, minor);
    const std::string path = driver_dir() + "/" + sub + "/" + verbuf + ".ko.sh";

    if (access(path.c_str(), F_OK) != 0) {
        std::cout << "[WARN] No matching driver script for kernel " << rel
                   << " at:\n  " << path << "\n"
                   << "       Continuing without loading a driver.\n";
        return;
    }

    chmod(path.c_str(), 0755);
    chown(path.c_str(), 0, 0);
    std::cout << "Loading driver: " << path << "\n";

    pid_t child = fork();
    if (child < 0) {
        std::cout << "[WARN] fork() failed for driver load: "
                   << strerror(errno) << "\n";
        return;
    }
    if (child == 0) {
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); }
        execl("/system/bin/sh", "sh", path.c_str(), (char*)nullptr);
        execl(path.c_str(), path.c_str(), (char*)nullptr);
        _exit(127);
    }
    usleep(500 * 1000);
}

static std::string step_driver() {
    std::cout << "Step 2 - Driver\n";
    for (const auto& d : kDrivers)
        std::cout << "  " << d.num << ") " << d.label << "\n";
    std::cout << "\nSelect [1-" << (sizeof(kDrivers) / sizeof(kDrivers[0]))
               << ", default=1]: ";
    std::string sel;
    std::getline(std::cin, sel);
    sel = trim(sel);

    std::string chosen = "auto";
    for (const auto& d : kDrivers) {
        if (sel == d.num) { chosen = d.name; break; }
    }
    std::cout << "-> Driver selected: " << chosen << "\n\n";

    if (chosen == "paradise") load_paradise_driver();
    return chosen;
}

// -------------------------------------------------------------
//  Step 3 — bar vs. volume menu.
// -------------------------------------------------------------
static std::string step_menu_mode() {
    std::cout << "Step 3 - Menu\n  1) Bar (on-screen tap bar)\n"
                 "  2) Volume (volume-key toggle)\n"
               << "\nSelect [1-2, default=1]: ";
    std::string sel;
    std::getline(std::cin, sel);
    sel = trim(sel);
    const std::string mode = (sel == "2") ? "vol" : "bar";
    std::cout << "-> Menu mode: " << mode << "\n\n";
    return mode;
}

// -------------------------------------------------------------
//  Step 4 — run optimum_bar in the foreground with the verified key
//  as argv[1] (its own run_silent() re-verifies independently, same
//  dual-login contract the daemon already uses) and the chosen
//  driver/menu mode as env vars ix_driver_init.h already reads.
// -------------------------------------------------------------
static void step_run(const std::string& key, const std::string& driver,
                      const std::string& menuMode) {
    const std::string bin = overlay_bin();
    if (access(bin.c_str(), F_OK) != 0) {
        std::cerr << "[ERROR] Overlay binary not found next to this program:\n  "
                   << bin << "\n";
        return;
    }
    chmod(bin.c_str(), 0755);
    if (getuid() == 0) chown(bin.c_str(), 0, 0);

    setenv("IX_DRIVER_NAME", driver.c_str(), 1);
    setenv("IX_MENU_OPEN", menuMode.c_str(), 1);

    std::cout << "Step 4 - Starting\n"
                 "Driver: " << driver << "   Menu: " << menuMode << "\n"
                 "Launching optimum_bar...\n\n";

    execl(bin.c_str(), bin.c_str(), key.c_str(), (char*)nullptr);
    // execl only returns on failure.
    std::cerr << "[ERROR] exec failed: " << strerror(errno) << "\n";
}

int main() {
    banner();

    g_base = kd_exe_dir();
    if (g_base.empty()) {
        std::cerr << "[ERROR] Could not resolve this program's own directory "
                     "(/proc/self/exe unreadable).\n";
        return 1;
    }

    if (getuid() != 0) {
        std::cout << "[WARN] Not running as root (su). Driver loading and\n"
                     "       launching optimum_bar as root will likely fail.\n"
                     "       Run this through 'su -c' or an su shell.\n\n";
    }

    const std::string key = step_login();
    if (key.empty()) {
        std::cerr << "[ERROR] Login did not succeed. Exiting.\n";
        return 1;
    }

    const std::string driver = step_driver();
    const std::string menuMode = step_menu_mode();

    step_run(key, driver, menuMode);
    return 1; // only reached if exec failed
}
