/* ============================================================
   daemon_service.cpp — InxMods background daemon.

   Started at boot by service.sh (via nohup, detached).
   Stays running permanently; service.sh/pgrep restarts it on boot.

   Responsibilities
   ─────────────────
   1. Monitor foreground app every ~1.5 s.
   2. When target package (default: com.dts.freefiremax — Free Fire MAX) comes
      to the foreground:
        a. Load the driver that was selected in the WebUI
           (config.json → "driver" field, or a custom path).
        b. Launch the selected binary: optimum_bar or optimum_vol
           (config.json → "binary" field).
   3. When the target package leaves the foreground AND the daemon
      launched it this session, stop the binary.
   4. Write a short daemon log to
      /data/adb/modules/inx_mods/webroot/daemon.log
      (ring-buffer, ≤ 200 lines) so the WebUI "Daemon Logs" panel
      can show what happened.

   Config.json schema (InxMods):
   ─────────────────────────────
   {
     "binary":   "optimum_bar",   // or "optimum_vol"
     "duration": "1d",
     "key":      "<license key>",
     "seller":   "<seller name>",
     "status":   "success",
     "validity": "<expiry date>",
     // optional, written by WebUI driver section:
     "driver":   "<driver name or absolute path>",
     "target_package": "com.dts.freefiremax"
   }

   Driver loading:
   ─────────────────
   If "driver" is an absolute path (starts with '/'), run it directly.
   Otherwise look under /data/adb/modules/inx_mods/drivers/<driver>.
   A missing / empty driver field is fine — binary still launches; the
   driver loading step is simply skipped.

   Binary control:
   ─────────────────
   Binaries live at /data/adb/modules/inx_mods/bin/arm64-v8a/<name>.
   Launched with: chmod 777, chown 0:0, then execl detached child.
   PID written to /data/adb/modules/inx_mods/config/<name>.pid.

   Compile-time override:
     -DINX_MODULE_DIR="/data/adb/modules/inx_mods"
   ============================================================ */

#include <cstdio>
#include <cstdarg>
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
#include <time.h>
#include <errno.h>
#include <json.hpp>

using json = nlohmann::json;

#ifndef INX_MODULE_DIR
#define INX_MODULE_DIR "/data/adb/modules/inx_mods"
#endif

// ─── paths ───────────────────────────────────────────────────
static const char* MOD_DIR      = INX_MODULE_DIR;
static const char* WEB_CFG      = INX_MODULE_DIR "/webroot/config.json";
static const char* DAEMON_LOG   = INX_MODULE_DIR "/webroot/daemon.log";
static const char* CFG_DIR      = INX_MODULE_DIR "/config";
static const char* BIN_DIR      = INX_MODULE_DIR "/bin/arm64-v8a";
static const char* DRIVER_DIR   = INX_MODULE_DIR "/drivers";

// Default target package when config has none
static const char* DEFAULT_TARGET = "com.dts.freefiremax";

// ─── logging ─────────────────────────────────────────────────
#define LOG_MAX_LINES 200

static void daemon_log(const char* fmt, ...) {
    // Read existing lines
    std::vector<std::string> lines;
    {
        std::ifstream f(DAEMON_LOG);
        std::string l;
        while (std::getline(f, l)) lines.push_back(l);
    }

    // Format new line with timestamp
    char timebuf[32];
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm_info);

    char msgbuf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
    va_end(ap);

    char line[600];
    snprintf(line, sizeof(line), "[%s] %s", timebuf, msgbuf);
    lines.push_back(line);

    // Keep last LOG_MAX_LINES lines
    if ((int)lines.size() > LOG_MAX_LINES)
        lines.erase(lines.begin(), lines.begin() + (lines.size() - LOG_MAX_LINES));

    // Write back
    std::ofstream out(DAEMON_LOG, std::ios::trunc);
    if (out.good()) {
        for (const auto& l : lines) out << l << "\n";
        chmod(DAEMON_LOG, 0644);
    }
}

// ─── shell helper ────────────────────────────────────────────
static std::string run_shell(const std::string& cmd) {
    std::string out;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    pclose(p);
    return out;
}

// ─── config ──────────────────────────────────────────────────
static json load_config() {
    std::ifstream f(WEB_CFG);
    if (!f.good()) return json::object();
    try {
        json j; f >> j;
        if (j.is_object()) return j;
    } catch (...) {}
    return json::object();
}

// ─── foreground app detection ────────────────────────────────
static std::string foreground_package() {
    static const std::regex re1("mCurrentFocus=.*?\\s([A-Za-z0-9_.]+)/");
    static const std::regex re2("mResumedActivity:.*?\\s([A-Za-z0-9_.]+)/");
    static const std::regex re3("mFocusedApp=.*?\\s([A-Za-z0-9_.]+)/");

    std::smatch m;
    std::string out = run_shell("dumpsys window windows 2>/dev/null");
    if (std::regex_search(out, m, re1) && m.size() > 1) return m[1].str();
    if (std::regex_search(out, m, re3) && m.size() > 1) return m[1].str();

    out = run_shell("dumpsys activity activities 2>/dev/null");
    if (std::regex_search(out, m, re2) && m.size() > 1) return m[1].str();

    return "";
}

// ─── PID helpers ─────────────────────────────────────────────
static std::string pid_file_path(const std::string& bin_name) {
    return std::string(CFG_DIR) + "/" + bin_name + ".pid";
}

static pid_t read_pid(const std::string& path) {
    std::ifstream f(path);
    if (!f.good()) return -1;
    pid_t p = -1;
    f >> p;
    return p;
}

static bool pid_alive(pid_t p) {
    if (p <= 0) return false;
    return kill(p, 0) == 0;
}

static bool is_running(const std::string& bin_name) {
    return pid_alive(read_pid(pid_file_path(bin_name)));
}

// ─── driver loading ──────────────────────────────────────────
static void load_driver(const std::string& driver_field) {
    if (driver_field.empty()) return;

    std::string driver_path;
    if (driver_field[0] == '/') {
        // Absolute path provided directly
        driver_path = driver_field;
    } else {
        // Named driver under module drivers/ folder
        driver_path = std::string(DRIVER_DIR) + "/" + driver_field;
    }

    if (access(driver_path.c_str(), F_OK) != 0) {
        daemon_log("Driver not found: %s", driver_path.c_str());
        return;
    }

    // Ensure executable
    chmod(driver_path.c_str(), 0777);
    chown(driver_path.c_str(), 0, 0);

    daemon_log("Loading driver: %s", driver_path.c_str());

    // Fork + exec driver script/binary detached
    pid_t child = fork();
    if (child < 0) {
        daemon_log("Failed to fork for driver: %s", strerror(errno));
        return;
    }
    if (child == 0) {
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); }
        execl("/system/bin/sh", "sh", driver_path.c_str(), (char*)nullptr);
        // If driver is an ELF binary, try direct exec
        execl(driver_path.c_str(), driver_path.c_str(), (char*)nullptr);
        _exit(127);
    }
    // Give driver a moment to initialize before launching main binary
    usleep(500 * 1000); // 0.5s
}

// ─── binary start / stop ─────────────────────────────────────
// NOTE (fixed bug): optimum_bar/optimum_vol run their OWN independent
// login (Authentication::run_silent(argv[1])) — same dual-login
// contract main.cpp documents. They were being launched here with NO
// argv[1], so main.cpp fell back to kd_load_saved_key(), which resolves
// a writable dir from a candidate list that includes $HOME/.kd_store.
// inx_login_@api (launched from the WebUI via a DIFFERENT root shell
// context than this boot daemon) can end up with a different $HOME,
// so the two binaries silently disagree on where the saved key lives —
// optimum_bar/optimum_vol then fails its own login and prints
// "Auth/runtime gate failed" with no indication why. Fix: always pass
// the already-verified key from config.json's "key" field (written by
// inx_login_@api on success) straight through as argv[1], so the
// binary verifies the exact same key with zero dependency on shared
// filesystem state.
static bool start_binary(const std::string& bin_name, const std::string& key) {
    if (is_running(bin_name)) return true;

    std::string bin_path = std::string(BIN_DIR) + "/" + bin_name;
    if (access(bin_path.c_str(), F_OK) != 0) {
        daemon_log("Binary not found: %s", bin_path.c_str());
        return false;
    }

    chmod(bin_path.c_str(), 0777);
    chown(bin_path.c_str(), 0, 0);

    pid_t child = fork();
    if (child < 0) {
        daemon_log("Fork failed for binary %s: %s", bin_name.c_str(), strerror(errno));
        return false;
    }
    if (child == 0) {
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); }
        if (!key.empty()) {
            execl(bin_path.c_str(), bin_path.c_str(), key.c_str(), (char*)nullptr);
        } else {
            execl(bin_path.c_str(), bin_path.c_str(), (char*)nullptr);
        }
        _exit(127);
    }

    // Write PID
    std::string pf = pid_file_path(bin_name);
    std::ofstream pfout(pf, std::ios::trunc);
    if (pfout.good()) {
        pfout << child;
        pfout.close();
        chmod(pf.c_str(), 0644);
    }

    daemon_log("Started %s (pid %d)", bin_name.c_str(), (int)child);
    return true;
}

static void stop_binary(const std::string& bin_name) {
    pid_t p = read_pid(pid_file_path(bin_name));
    if (pid_alive(p)) {
        kill(p, SIGTERM);
        for (int i = 0; i < 20 && pid_alive(p); ++i) usleep(100000);
        if (pid_alive(p)) kill(p, SIGKILL);
        daemon_log("Stopped %s (pid %d)", bin_name.c_str(), (int)p);
    }
    unlink(pid_file_path(bin_name).c_str());
}

// ─── zombie reaping ──────────────────────────────────────────
static void reap_children(int /*sig*/) {
    while (waitpid(-1, nullptr, WNOHANG) > 0) {}
}

// ─── daemon pidfile ──────────────────────────────────────────
static const char* DAEMON_PIDFILE = INX_MODULE_DIR "/config/daemon_service.pid";

// ===============================================================
int main(int /*argc*/, char* /*argv*/[]) {
    // Detach from terminal
    setsid();

    // Reap zombie children so we don't accumulate them
    signal(SIGCHLD, reap_children);

    // Ignore SIGHUP
    signal(SIGHUP, SIG_IGN);

    // Write our own PID
    {
        // Ensure config dir exists
        mkdir(CFG_DIR, 0755);
        std::ofstream pf(DAEMON_PIDFILE, std::ios::trunc);
        if (pf.good()) {
            pf << getpid();
            pf.close();
            chmod(DAEMON_PIDFILE, 0644);
        }
    }

    daemon_log("daemon_service_@api started (pid %d)", (int)getpid());

    bool launchedByDaemon = false;
    std::string lastBinaryName; // tracks which binary we launched

    for (;;) {
        json cfg = load_config();

        // Determine target package (default: com.dts.freefiremax)
        std::string target = cfg.value("target_package", DEFAULT_TARGET);
        if (target.empty()) target = DEFAULT_TARGET;

        // Determine which binary to run
        std::string bin_name = cfg.value("binary", "optimum_bar");
        if (bin_name != "optimum_bar" && bin_name != "optimum_vol")
            bin_name = "optimum_bar"; // safe default

        // Driver selection
        std::string driver = cfg.value("driver", "");

        // Verified license key — written by inx_login_@api on success.
        // Passed straight through to the binary's own argv[1] so it
        // re-verifies the EXACT key already accepted, with no dependency
        // on the two binaries agreeing on a shared on-disk key location.
        std::string saved_key   = cfg.value("key", "");
        std::string login_state = cfg.value("status", "");
        const bool  loggedIn    = (login_state == "success") && !saved_key.empty();

        const std::string fg = foreground_package();
        const bool inTarget  = (fg == target);
        const bool running   = is_running(bin_name);

        if (inTarget && !running && loggedIn) {
            daemon_log("Target %s in foreground — starting binary", target.c_str());

            // Load driver first (if configured)
            if (!driver.empty()) {
                load_driver(driver);
            }

            // Launch binary (key forwarded as argv[1] — see start_binary() note)
            if (start_binary(bin_name, saved_key)) {
                launchedByDaemon = true;
                lastBinaryName   = bin_name;
            }
        } else if (inTarget && !running && !loggedIn) {
            // Target is open but no verified key yet — don't spawn a
            // doomed process every poll cycle. Log once per foreground
            // session so the WebUI "Daemon Logs" panel explains why
            // nothing launched.
            static std::string lastWarnedTarget;
            if (lastWarnedTarget != target) {
                daemon_log("Target %s in foreground but not logged in — login first via WebUI", target.c_str());
                lastWarnedTarget = target;
            }
        } else if (!inTarget && launchedByDaemon && !lastBinaryName.empty()) {
            if (is_running(lastBinaryName)) {
                daemon_log("Target %s left foreground — stopping binary", target.c_str());
                stop_binary(lastBinaryName);
                launchedByDaemon = false;
                lastBinaryName.clear();
            }
        }

        usleep(1500 * 1000); // 1.5s poll interval
    }

    return 0; // unreachable
}
