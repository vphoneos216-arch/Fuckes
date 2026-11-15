/* ============================================================
   kd_ctl.cpp — compiled control/daemon helper for the InvisibleX
   WebUI module. NOT a shell script on purpose: the login check,
   the start/stop logic, AND the per-app auto-launch watcher are all
   native, stripped, hybrid-signed-crypto-linked binaries — nothing
   here is human-readable or patchable the way a .sh control script
   would be.

   Talks to the exact same config.json optimum_bar already reads
   (jni/src/ix_driver_init.h):
     /data/adb/modules/invisiblex_loader/config/config.json
       { "driver": "qx1", "menu_open": "bar", "target_package": "com.x" }

   Subcommands (all print one line of JSON to stdout):
     kd_ctl login <KEY>          run_silent(key) — same hybrid-signed
                                  (Ed25519+Ed448+ML-DSA-65) server check
                                  optimum_bar itself uses; persists the key
                                  the same way optimum_bar would.
     kd_ctl status               { running, has_saved_key, driver,
                                    menu_open, target_package,
                                    watcher_running }
     kd_ctl start / stop          manual launch/kill of optimum_bar
     kd_ctl set-driver <name>     write config.json "driver"
     kd_ctl set-menu <vol|bar>    write config.json "menu_open"
     kd_ctl list-apps             installed third-party apps, as
                                   JSON [{"package":"..","name":".."}]
     kd_ctl set-target <pkg>      write config.json "target_package"
     kd_ctl watch                 start the background watcher: polls
                                   the foreground app every ~1.5s and
                                   starts/stops optimum_bar automatically
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
// [RENAME] Deployed/installed filename is "optimum_bar" (the compiled
// artifact is still produced as INX.SH by Android.mk/ndk-build — only
// the name it gets copied to inside the module's bin/ folder changed,
// at packaging time in .github/workflows/build.yml). This path must
// match exactly what service.sh chmods and what the module zip ships
// under bin/, or start/stop/status/watch all silently find nothing.
static std::string kd_binpath()        { return std::string(KD_MODULE_DIR) + "/bin/optimum_bar"; }
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

// nlohmann's value() throws json::type_error when the stored field is
// not the type of the default - so a config.json whose "validity" or
// "status" had been edited to a number or null (the file is 0644 and
// the WebUI writes it too) would take an UNCAUGHT exception here and
// abort the process. That is the same class of failure this change
// exists to remove, so every config string is read through this
// instead: wrong type or missing key both just give the fallback.
static std::string kd_cfg_str(const json& cfg, const char* key,
                              const char* fallback = "") {
    auto it = cfg.find(key);
    if (it != cfg.end() && it->is_string()) return it->get<std::string>();
    return std::string(fallback);
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

// [BUG FIX] A bare kill(p,0) check only proves SOME process holds that
// pid right now — it says nothing about WHICH process. Android recycles
// pids quickly (seconds, not minutes) under normal churn. If optimum_bar
// dies (crash, integrity-watchdog SIGKILL, driver failure) and the pid
// gets reassigned to an unrelated process before the next watcher poll
// (1.5s later — easily within range), kd_pid_alive(old_pid) keeps
// returning true forever: kd_inx_running() == true, so kd_watch_loop()
// never relaunches it even while the target app is still open. This is
// the same class of fix daemon_service.cpp's is_running() already has —
// kd_ctl's own pid checks never got it. Confirms identity by requiring
// the pid's /proc/<pid>/cmdline to actually contain the binary name.
static bool kd_pid_is_our_binary(pid_t p, const char* binName) {
    if (p <= 0) return false;
    std::ifstream cl("/proc/" + std::to_string((int)p) + "/cmdline", std::ios::binary);
    if (!cl.good()) return false;
    std::string cmd((std::istreambuf_iterator<char>(cl)), std::istreambuf_iterator<char>());
    return cmd.find(binName) != std::string::npos;
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
//  [CRASH FIX - exit 135 "Bus error", empty stdout]
//  "kd_ctl login" calls run_silent(), which starts the integrity
//  watchdog in login.h: a detached thread that re-reads this binary's
//  own file-backed .rodata every 1.5s (ix_integrity_gate() evaluates
//  oxorany(CONNECT_URL) on every tick). Returning from main() then let
//  the runtime tear the process down underneath that thread, and a tick
//  landing in the teardown window faulted on a page that was no longer
//  mapped - SIGBUS, reported by the shell as exit 135.
//
//  stdout here is a pipe (the WebUI reads it), so it is block-buffered:
//  the single JSON line kd_print() had already produced was still in
//  the buffer and died with the process. That is both halves of the
//  reported failure at once - "[exit 135] out: (empty stdout) | err:
//  Bus error" - and why the caller then said the output was not JSON.
//
//  Every exit now parks the watchdog, flushes the JSON line out for
//  real, and leaves via _exit(), which ends the process in the kernel
//  with no userspace teardown and so no window left to fault in.
//
//  [[noreturn]] so the compiler stops treating the call sites as
//  falling through to the code after them.
// -------------------------------------------------------------
[[noreturn]] static void kd_finish(int code) {
    kd_stop_integrity_watchdog();
    fflush(stdout);
    fflush(stderr);
    _exit(code);
}

// -------------------------------------------------------------
//  optimum_bar process control, shared by "start"/"stop" and the watcher.
// -------------------------------------------------------------
static bool kd_inx_running() {
    return kd_pid_is_our_binary(kd_read_pid(kd_pidfile()), "optimum_bar");
}

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
//  Watcher daemon: forks, detaches, polls, starts/stops optimum_bar.
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

// [BUG FIX] This had NO tolerance at all: one single poll (1.5s) where
// dumpsys's regex didn't match the target — a notification shade, a
// system dialog, a brief app-switcher frame, a permission prompt, even
// one dumpsys call that returned late or empty — made inTarget false
// for that tick and instantly stopped optimum_bar, even though the game
// was still open and the player never left it. That alone explains
// "sometimes just stops by itself" far better than a crash would: a
// crash is rare, but a one-tick focus flicker during normal play is
// not. MISS_TOLERANCE requires several consecutive misses (bounded,
// not unlimited) before treating the player as having actually left —
// same reasoning as daemon_service.cpp's LEAVE_MISS_POLLS, simplified
// (no per-OEM screen-recorder allowlist here, just a flat tolerance).
static constexpr int KD_MISS_TOLERANCE = 8; // 8 * 1.5s = 12s of continuous misses

static void kd_watch_loop() {
    bool launchedByWatcher = false;
    int  missCount = 0;
    for (;;) {
        json cfg = kd_load_config();
        const std::string target = kd_cfg_str(cfg, "target_package", "");
        if (!target.empty()) {
            const std::string fg = kd_foreground_package();
            const bool inTarget = (fg == target);
            const bool running = kd_inx_running();
            if (inTarget) {
                missCount = 0;
                if (!running) {
                    if (kd_inx_start()) launchedByWatcher = true;
                }
            } else if (running && launchedByWatcher) {
                if (++missCount >= KD_MISS_TOLERANCE) {
                    kd_inx_stop();
                    launchedByWatcher = false;
                    missCount = 0;
                }
            } else {
                missCount = 0;
            }
        } else {
            missCount = 0;
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
        if (argc < 3) { kd_print(false, R"(,"error":"missing key")"); kd_finish(1); }
        const std::string key = argv[2];
        Authentication auth;
        const bool ok = auth.run_silent(key);
        if (ok) {
            // [FIX - unescaped server strings could produce invalid JSON]
            // seller and expiry come from the server. They used to be
            // pasted straight into the JSON with %s, so a single quote
            // or backslash anywhere in them broke the line the caller
            // has to parse - and a long seller name hit the 512-byte
            // snprintf buffer and got truncated mid-string, breaking it
            // the same way. kd_json_escape() already existed in this
            // file for exactly this; building the line with std::string
            // also removes the fixed buffer, so nothing can truncate.
            const std::string extra =
                std::string(R"(,"seller":")") + kd_json_escape(auth.getSellerName()) +
                std::string(R"(","expiry":")") + kd_json_escape(auth.getExpiryDate()) +
                std::string(R"(","days_remain":)") + std::to_string(auth.getDaysRemain());
            kd_print(true, extra);
        } else {
            // [FIX - an expired key reported nothing about expiry]
            // run_silent() keeps the server's own reason for the refusal
            // ("License expired on ...", "License is not valid.", a
            // device-limit message, ...) in auth_message. This path threw
            // it away and emitted one fixed line, so an EXPIRED licence
            // looked exactly like a mistyped one and the status panel had
            // nothing to show. The real reason is reported now, escaped
            // and untruncated for the same reasons as above.
            std::string why = auth.getAuthMessage();
            if (why.empty()) why = oxorany("Login failed. Please try again.");
            kd_print(false, std::string(R"(,"error":")") + kd_json_escape(why) + "\"");
        }
        kd_finish(ok ? 0 : 1);
    }

    // ---------------------------------------------------------
    if (cmd == "status") {
        const bool running = kd_inx_running();
        const bool watcherRunning = kd_pid_alive(kd_read_pid(kd_watcher_pidfile()));
        const bool hasKey = !kd_load_saved_key().empty();
        json cfg = kd_load_config();
        // Built with std::string instead of a 512-byte snprintf: the
        // buffer could truncate a long target_package or seller name
        // mid-string and hand the caller a broken JSON line. Every
        // server- or argv-sourced string goes through kd_json_escape()
        // for the same reason - "driver" and "menu_open" come from
        // set-driver/set-menu arguments and were being pasted in raw.
        //
        // license_status / seller / validity / message are read straight
        // out of the config the login binary writes, so a status poll
        // can show WHY a key was refused and when it expired - an
        // expired licence used to leave the panel with nothing to say.
        // Named license_status, not status, so it cannot be mistaken for
        // the result of this command.
        const std::string extra =
            std::string(R"(,"running":)")        + (running ? "true" : "false") +
            std::string(R"(,"has_saved_key":)")  + (hasKey ? "true" : "false") +
            std::string(R"(,"driver":")")        + kd_json_escape(kd_cfg_str(cfg, "driver", "auto")) +
            std::string(R"(","menu_open":")")    + kd_json_escape(kd_cfg_str(cfg, "menu_open", "bar")) +
            std::string(R"(","target_package":")") + kd_json_escape(kd_cfg_str(cfg, "target_package", "")) +
            std::string(R"(","watcher_running":)") + (watcherRunning ? "true" : "false") +
            std::string(R"(,"license_status":")") + kd_json_escape(kd_cfg_str(cfg, "status", "")) +
            std::string(R"(","seller":")")       + kd_json_escape(kd_cfg_str(cfg, "seller", "")) +
            std::string(R"(","validity":")")     + kd_json_escape(kd_cfg_str(cfg, "validity", "")) +
            std::string(R"(","message":")")      + kd_json_escape(kd_cfg_str(cfg, "message", "")) +
            std::string(R"(")");
        kd_print(true, extra);
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
