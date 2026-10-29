/* ============================================================
   kd_ctl.cpp — compiled control/daemon helper for the InvisibleX
   WebUI module. NOT a shell script on purpose: the login check,
   the start/stop logic, AND the per-app auto-launch watcher are all
   native, stripped, hybrid-signed-crypto-linked binaries — nothing
   here is human-readable or patchable the way a .sh control script
   would be.

   Talks to the exact same config.json INX.SH already reads
   (jni/src/ix_driver_init.h):
     /data/adb/modules/invisiblex_loader/config/config.json
       { "driver": "qx1", "menu_open": "bar", "target_package": "com.x" }

   Subcommands (all print one line of JSON to stdout):
     kd_ctl login <KEY>          run_silent(key) — same hybrid-signed
                                  (Ed25519+Ed448+ML-DSA-65) server check
                                  INX.SH itself uses; persists the key
                                  the same way INX.SH would.
     kd_ctl status               { running, has_saved_key, driver,
                                    menu_open, target_package,
                                    watcher_running }
     kd_ctl start / stop          manual launch/kill of INX.SH
     kd_ctl set-driver <name>     write config.json "driver"
     kd_ctl set-menu <vol|bar>    write config.json "menu_open"
     kd_ctl list-apps             installed third-party apps, as
                                   JSON [{"package":"..","name":".."}]
     kd_ctl set-target <pkg>      write config.json "target_package"
     kd_ctl watch                 start the background watcher: polls
                                   the foreground app every ~1.5s and
                                   starts/stops INX.SH automatically
                                   when target_package opens/closes
     kd_ctl watch-stop             stop that watcher
   ============================================================ */

#include "login.h"
#include "ix_driver_init.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <regex>
#include <csignal>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <json.hpp>

using json = nlohmann::json;

#ifndef KD_MODULE_DIR
#define KD_MODULE_DIR "/data/adb/modules/invisiblex_loader"
#endif

static std::string kd_pidfile()        { return std::string(KD_MODULE_DIR) + "/config/inx.pid"; }
static std::string kd_watcher_pidfile(){ return std::string(KD_MODULE_DIR) + "/config/watcher.pid"; }
static std::string kd_binpath()        { return std::string(KD_MODULE_DIR) + "/bin/INX.SH"; }
static std::string kd_cfgpath()        { return std::string(KD_MODULE_DIR) + "/config/config.json"; }

static json kd_load_config() {
    std::ifstream f(kd_cfgpath());
    if (!f.good()) return json::object();
    try {
        json j; f >> j;
        if (j.is_object()) return j;
    } catch (...) {}
    return json::object();
}

static bool kd_write_config(const json& j) {
    std::ofstream out(kd_cfgpath(), std::ios::trunc);
    if (!out.good()) return false;
    out << j.dump(2);
    out.close();
    chmod(kd_cfgpath().c_str(), 0644);
    return true;
}

static pid_t kd_read_pid(const std::string& path) {
    std::ifstream f(path);
    if (!f.good()) return -1;
    pid_t p = -1;
    f >> p;
    return p;
}

static bool kd_pid_alive(pid_t p) {
    if (p <= 0) return false;
    return kill(p, 0) == 0;
}

static void kd_print(bool ok, const std::string& extra) {
    printf("{\"ok\":%s%s}\n", ok ? "true" : "false", extra.c_str());
}

static std::string kd_shell(const std::string& cmdline) {
    std::string out;
    FILE* p = popen(cmdline.c_str(), "r");
    if (!p) return out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    pclose(p);
    return out;
}

static std::string kd_json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if ((unsigned char)c < 0x20) continue;
        else out += c;
    }
    return out;
}

// -------------------------------------------------------------
//  INX.SH process control, shared by "start"/"stop" and the watcher.
// -------------------------------------------------------------
static bool kd_inx_running() { return kd_pid_alive(kd_read_pid(kd_pidfile())); }

static bool kd_inx_start() {
    if (kd_inx_running()) return true;
    if (kd_load_saved_key().empty()) return false;

    pid_t child = fork();
    if (child < 0) return false;
    if (child == 0) {
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); }
        execl(kd_binpath().c_str(), kd_binpath().c_str(), (char*)nullptr);
        _exit(127);
    }
    std::ofstream pf(kd_pidfile(), std::ios::trunc);
    if (pf.good()) { pf << child; pf.close(); chmod(kd_pidfile().c_str(), 0644); }
    return true;
}

static void kd_inx_stop() {
    pid_t p = kd_read_pid(kd_pidfile());
    if (kd_pid_alive(p)) {
        kill(p, SIGTERM);
        for (int i = 0; i < 20 && kd_pid_alive(p); ++i) usleep(100000);
        if (kd_pid_alive(p)) kill(p, SIGKILL);
    }
    unlink(kd_pidfile().c_str());
}

// -------------------------------------------------------------
//  Installed-apps listing (third-party only — the realistic
//  "which game/app" picker list). Package names always come from
//  `pm list packages -3` (always present on any Android). A
//  human-readable label is best-effort via aapt/aapt2 if either
//  binary happens to exist on this device; otherwise the package
//  name is shown as its own name rather than leaving it blank.
// -------------------------------------------------------------
static std::string kd_find_aapt() {
    for (const char* p : {"/system/bin/aapt2", "/system/bin/aapt",
                           "/system/xbin/aapt2", "/system/xbin/aapt"}) {
        if (access(p, X_OK) == 0) return p;
    }
    return "";
}

static std::string kd_app_label(const std::string& aapt, const std::string& pkg) {
    if (aapt.empty()) return "";
    std::string apkPath = kd_shell("pm path " + pkg + " 2>/dev/null | head -n1");
    const size_t pos = apkPath.find("package:");
    if (pos == std::string::npos) return "";
    apkPath = apkPath.substr(pos + 8);
    while (!apkPath.empty() && (apkPath.back() == '\n' || apkPath.back() == '\r')) apkPath.pop_back();
    if (apkPath.empty()) return "";

    const std::string dumpCmd = aapt.find("aapt2") != std::string::npos
        ? (aapt + " dump badging \"" + apkPath + "\" 2>/dev/null")
        : (aapt + " dump badging \"" + apkPath + "\" 2>/dev/null");
    const std::string dump = kd_shell(dumpCmd);

    std::smatch m;
    static const std::regex re("application-label:'([^']*)'");
    if (std::regex_search(dump, m, re) && m.size() > 1) return m[1].str();
    return "";
}

static void kd_list_apps() {
    const std::string raw = kd_shell("pm list packages -3 2>/dev/null");
    const std::string aapt = kd_find_aapt();

    std::vector<std::pair<std::string,std::string>> apps;
    std::istringstream iss(raw);
    std::string line;
    while (std::getline(iss, line)) {
        const size_t pos = line.find("package:");
        if (pos == std::string::npos) continue;
        std::string pkg = line.substr(pos + 8);
        while (!pkg.empty() && (pkg.back() == '\n' || pkg.back() == '\r' || pkg.back() == ' ')) pkg.pop_back();
        if (pkg.empty()) continue;
        std::string label = kd_app_label(aapt, pkg);
        apps.emplace_back(pkg, label.empty() ? pkg : label);
    }

    std::string body;
    for (size_t i = 0; i < apps.size(); ++i) {
        if (i) body += ",";
        body += "{\"package\":\"" + kd_json_escape(apps[i].first) +
                "\",\"name\":\"" + kd_json_escape(apps[i].second) + "\"}";
    }
    printf("{\"ok\":true,\"apps\":[%s]}\n", body.c_str());
}

// -------------------------------------------------------------
//  Foreground-app detection for the watcher. Tries two dumpsys
//  shapes (different Android versions phrase this differently);
//  cheap enough to call every ~1.5s without any noticeable load.
// -------------------------------------------------------------
static std::string kd_foreground_package() {
    static const std::regex re1("mCurrentFocus=.*?\\s([A-Za-z0-9_.]+)/");
    static const std::regex re2("mResumedActivity:.*?\\s([A-Za-z0-9_.]+)/");
    static const std::regex re3("mFocusedApp=.*?\\s([A-Za-z0-9_.]+)/");

    std::smatch m;
    std::string out = kd_shell("dumpsys window windows 2>/dev/null");
    if (std::regex_search(out, m, re1) && m.size() > 1) return m[1].str();
    if (std::regex_search(out, m, re3) && m.size() > 1) return m[1].str();

    out = kd_shell("dumpsys activity activities 2>/dev/null");
    if (std::regex_search(out, m, re2) && m.size() > 1) return m[1].str();

    return "";
}

// -------------------------------------------------------------
//  Watcher daemon: forks, detaches, polls, starts/stops INX.SH.
//  Single-instance via its own pidfile.
// -------------------------------------------------------------
static void kd_watch_stop_impl() {
    pid_t p = kd_read_pid(kd_watcher_pidfile());
    if (kd_pid_alive(p)) {
        kill(p, SIGTERM);
        for (int i = 0; i < 20 && kd_pid_alive(p); ++i) usleep(100000);
        if (kd_pid_alive(p)) kill(p, SIGKILL);
    }
    unlink(kd_watcher_pidfile().c_str());
}

static void kd_watch_loop() {
    bool launchedByWatcher = false;
    for (;;) {
        json cfg = kd_load_config();
        const std::string target = cfg.value("target_package", "");
        if (!target.empty()) {
            const std::string fg = kd_foreground_package();
            const bool inTarget = (fg == target);
            const bool running = kd_inx_running();
            if (inTarget && !running) {
                if (kd_inx_start()) launchedByWatcher = true;
            } else if (!inTarget && running && launchedByWatcher) {
                kd_inx_stop();
                launchedByWatcher = false;
            }
        }
        usleep(1500 * 1000); // 1.5s — cheap dumpsys call, negligible battery/heat impact
    }
}

static bool kd_watch_start() {
    pid_t existing = kd_read_pid(kd_watcher_pidfile());
    if (kd_pid_alive(existing)) return true;

    pid_t child = fork();
    if (child < 0) return false;
    if (child == 0) {
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); }
        kd_watch_loop();
        _exit(0);
    }
    std::ofstream pf(kd_watcher_pidfile(), std::ios::trunc);
    if (pf.good()) { pf << child; pf.close(); chmod(kd_watcher_pidfile().c_str(), 0644); }
    return true;
}

// ===============================================================
int main(int argc, char* argv[]) {
    if (argc < 2) {
        kd_print(false, R"(,"error":"usage: kd_ctl <login|status|start|stop|set-driver|set-menu|list-apps|set-target|watch|watch-stop> [arg]")");
        return 1;
    }
    const std::string cmd = argv[1];

    // ---------------------------------------------------------
    if (cmd == "login") {
        if (argc < 3) { kd_print(false, R"(,"error":"missing key")"); return 1; }
        const std::string key = argv[2];
        Authentication auth;
        const bool ok = auth.run_silent(key);
        if (ok) {
            char buf[512];
            snprintf(buf, sizeof(buf),
                R"(,"seller":"%s","expiry":"%s","days_remain":%d)",
                auth.getSellerName().c_str(), auth.getExpiryDate().c_str(), auth.getDaysRemain());
            kd_print(true, buf);
        } else {
            const std::string errMsg = oxorany("Login failed. Please try again.");
            char buf[512];
            snprintf(buf, sizeof(buf), R"(,"error":"%s")", errMsg.c_str());
            kd_print(false, buf);
        }
        return ok ? 0 : 1;
    }

    // ---------------------------------------------------------
    if (cmd == "status") {
        const bool running = kd_inx_running();
        const bool watcherRunning = kd_pid_alive(kd_read_pid(kd_watcher_pidfile()));
        const bool hasKey = !kd_load_saved_key().empty();
        json cfg = kd_load_config();
        char buf[512];
        snprintf(buf, sizeof(buf),
            R"(,"running":%s,"has_saved_key":%s,"driver":"%s","menu_open":"%s","target_package":"%s","watcher_running":%s)",
            running ? "true" : "false",
            hasKey ? "true" : "false",
            cfg.value("driver", "auto").c_str(),
            cfg.value("menu_open", "bar").c_str(),
            kd_json_escape(cfg.value("target_package", "")).c_str(),
            watcherRunning ? "true" : "false");
        kd_print(true, buf);
        return 0;
    }

    // ---------------------------------------------------------
    if (cmd == "start") {
        if (kd_inx_running()) { kd_print(true, R"(,"already_running":true)"); return 0; }
        if (kd_load_saved_key().empty()) { kd_print(false, R"(,"error":"no_saved_key")"); return 1; }
        const bool ok = kd_inx_start();
        kd_print(ok, ok ? "" : R"(,"error":"fork_failed")");
        return ok ? 0 : 1;
    }

    // ---------------------------------------------------------
    if (cmd == "stop") {
        kd_inx_stop();
        kd_print(true, "");
        return 0;
    }

    // ---------------------------------------------------------
    if (cmd == "set-driver") {
        if (argc < 3) { kd_print(false, R"(,"error":"missing driver name")"); return 1; }
        json cfg = kd_load_config();
        cfg["driver"] = std::string(argv[2]);
        const bool ok = kd_write_config(cfg);
        kd_print(ok, "");
        return ok ? 0 : 1;
    }

    // ---------------------------------------------------------
    if (cmd == "set-menu") {
        if (argc < 3) { kd_print(false, R"(,"error":"missing mode")"); return 1; }
        std::string mode = argv[2];
        if (mode != "vol" && mode != "bar") { kd_print(false, R"(,"error":"mode must be vol or bar")"); return 1; }
        json cfg = kd_load_config();
        cfg["menu_open"] = mode;
        const bool ok = kd_write_config(cfg);
        kd_print(ok, "");
        return ok ? 0 : 1;
    }

    // ---------------------------------------------------------
    if (cmd == "list-apps") {
        kd_list_apps();
        return 0;
    }

    // ---------------------------------------------------------
    if (cmd == "set-target") {
        if (argc < 3) { kd_print(false, R"(,"error":"missing package")"); return 1; }
        json cfg = kd_load_config();
        cfg["target_package"] = std::string(argv[2]);
        const bool ok = kd_write_config(cfg);
        kd_print(ok, "");
        return ok ? 0 : 1;
    }

    // ---------------------------------------------------------
    if (cmd == "watch") {
        const bool ok = kd_watch_start();
        kd_print(ok, ok ? "" : R"(,"error":"fork_failed")");
        return ok ? 0 : 1;
    }

    // ---------------------------------------------------------
    if (cmd == "watch-stop") {
        kd_watch_stop_impl();
        kd_print(true, "");
        return 0;
    }

    kd_print(false, R"(,"error":"unknown command")");
    return 1;
}
