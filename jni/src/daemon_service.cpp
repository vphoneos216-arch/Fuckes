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
   Launched with: chmod 0755, chown 0:0, then execl detached child.
   0755 and not 777: anything this daemon executes, it executes as root.
   A world-writable file that root then runs can be rewritten by any
   process that can reach it (a driver usually sits somewhere like
   /data/local/tmp), which hands that process root on the next launch.
   Root-owned 0755 keeps it executable while only root can change it.
   PID written to /data/adb/modules/inx_mods/config/<name>.pid.

   Compile-time override:
     -DINX_MODULE_DIR="/data/adb/modules/inx_mods"
   ============================================================ */

#include <cctype>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include <regex>
#include <csignal>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <time.h>
#include <errno.h>
#include <cstring>
#include <json.hpp>
#include "ix_controls.h"
#include "ix_driver_init.h"   // ix_write_remote_url
#include "ix_remote.h"

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

// The launched binary's stdout/stderr go here (overwritten each start), so a
// failure reason is visible instead of vanishing into /dev/null.
static const char* CHEAT_LOG = INX_MODULE_DIR "/webroot/cheat.log";

// After a launch, wait this long before trying again. Every launch does a
// full server login, so a binary that keeps exiting must not be relaunched
// every poll cycle.
static const int RESTART_BACKOFF_SECS = 60;

// How many times "Don't stop" reloads the driver and retries before it
// gives up and lets the normal backoff take over.
static const int MAX_DRIVER_RETRIES = 3;

// Consecutive polls the target must be missing from the foreground before the
// game counts as closed. Focus flickers for a poll during dialogs and system
// windows; without this the menu was killed in the middle of a match.
static const int LEAVE_MISS_POLLS = 4;

// One tick. Each tick only scans /proc in process, which costs no fork and a
// couple of milliseconds, so the game can be caught the moment it starts.
static const useconds_t TICK_USEC = 1000 * 1000;

// dumpsys is the expensive call (system_server builds the whole dump), so it
// runs once every this many ticks, and only while the game is alive. It is
// needed solely to take the menu down when the player leaves the game.
static const unsigned FOREGROUND_EVERY_TICKS = 3;

// Heartbeat is written at most this often, to avoid writing to flash every
// second for a whole day. The watchdog's stale window is far longer.
static const unsigned HEARTBEAT_EVERY_TICKS = 5;

// Heartbeat + state for the WebUI. The page reads this file with fetch(), so
// the status card needs no shell call: a shell call on a timer was flooding
// the WebUI bridge and is why the page froze after pressing Install.
static const char* STATUS_JSON = INX_MODULE_DIR "/webroot/status.json";

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
// Re-parses config.json only when it has been written since the last read.
// At a one-second tick, parsing it every time is pure waste.
static json load_config_cached() {
    static json   cached = json::object();
    static time_t cachedMtime = 0;
    static bool   loaded = false;

    struct stat st;
    if (stat(WEB_CFG, &st) != 0) return cached;
    if (loaded && st.st_mtime == cachedMtime) return cached;

    std::ifstream f(WEB_CFG);
    if (f.good()) {
        try {
            json j; f >> j;
            if (j.is_object()) cached = j;
        } catch (...) {}
    }
    cachedMtime = st.st_mtime;
    loaded = true;
    return cached;
}

// ─── foreground app detection ────────────────────────────────
// Pulls "<pkg>" out of the first line containing `key`, where the line has
// the form "... <pkg>/<activity> ...". grep -m1 means only one line ever
// reaches us, so no multi-megabyte dumpsys dump is read or regex-scanned.
static std::string pkg_from_dumpsys(const std::string& dumpsys_cmd, const char* key) {
    const std::string line = run_shell(dumpsys_cmd + " 2>/dev/null | grep -m1 '" + key + "'");
    const size_t k = line.find(key);
    if (k == std::string::npos) return "";
    const size_t slash = line.find('/', k);
    if (slash == std::string::npos) return "";
    size_t start = slash;
    while (start > 0) {
        const unsigned char c = (unsigned char)line[start - 1];
        if (!(isalnum(c) || c == '_' || c == '.')) break;
        --start;
    }
    return line.substr(start, slash - start);
}

static std::string foreground_package() {
    std::string pkg = pkg_from_dumpsys("dumpsys window", "mCurrentFocus=");
    if (pkg.empty()) pkg = pkg_from_dumpsys("dumpsys activity top", "ACTIVITY ");
    return pkg;
}

// ─── target-process check, in process ────────────────────────
// Scans /proc directly instead of shelling out to pgrep. popen() forks a
// shell for every call, and at a one-second tick that is 86,400 shells a day
// for nothing. Reading a few hundred small cmdline files costs a couple of
// milliseconds and no process at all, so the game can be spotted the moment
// it starts without costing battery while it is not running.
//
// An Android app's cmdline is exactly its package name, sometimes with a
// ":suffix" for an extra process, so argv[0] is compared on that basis.
static pid_t find_target_pid(const std::string& pkg) {
    if (pkg.empty()) return -1;

    DIR* d = opendir("/proc");
    if (!d) return -1;

    pid_t found = -1;
    char path[320];
    char buf[256];

    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;

        snprintf(path, sizeof(path), "/proc/%s/cmdline", e->d_name);
        const int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        const ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) continue;
        buf[n] = '\0';

        // argv[0] is the first NUL-terminated string in the buffer.
        if (pkg.compare(0, std::string::npos, buf, strnlen(buf, (size_t)n)) == 0 ||
            (strncmp(buf, pkg.c_str(), pkg.size()) == 0 && buf[pkg.size()] == ':')) {
            found = (pid_t)atoi(e->d_name);
            break;
        }
    }
    closedir(d);
    return found;
}

// ─── status file for the WebUI ───────────────────────────────
// Written to a temp file and renamed, so the page never reads half a file.
static void write_status(const json& st) {
    const std::string tmp = std::string(STATUS_JSON) + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f.good()) return;
        f << st.dump(2, ' ', false, json::error_handler_t::replace);
        if (!f.good()) return;
    }
    chmod(tmp.c_str(), 0644);
    rename(tmp.c_str(), STATUS_JSON);
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

// A stale pid file can point at a PID the kernel has since handed to an
// unrelated process. Android recycles PIDs quickly, so a bare kill(p,0)
// check reports "running" and the binary is never launched again. The
// process must also still be executing our binary (cmdline match).
static bool is_running(const std::string& bin_name) {
    const pid_t p = read_pid(pid_file_path(bin_name));
    if (!pid_alive(p)) return false;

    std::ifstream cl("/proc/" + std::to_string((int)p) + "/cmdline", std::ios::binary);
    std::string cmd((std::istreambuf_iterator<char>(cl)), std::istreambuf_iterator<char>());
    return cmd.find(bin_name) != std::string::npos;
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

    // Executable, root-owned, and NOT world-writable: this file is about to
    // be run as root, and a driver path is user-supplied (typically under
    // /data/local/tmp, which every process on the device can reach). At 0777
    // any app could overwrite it between loads and get its own code run as
    // root on the next game open.
    chmod(driver_path.c_str(), 0755);
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

// Did the binary quit because of the driver? wanbai.h prints exactly these
// when no driver is chosen or the chosen one will not initialise, so the
// daemon can tell a driver problem from any other exit.
static bool cheat_log_blames_driver() {
    std::ifstream f(CHEAT_LOG);
    if (!f.good()) return false;
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return all.find("No driver selected")        != std::string::npos
        || all.find("failed to initialise")      != std::string::npos
        || all.find("Please insert or load driver") != std::string::npos;
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

    // Root-owned 0755, same reasoning as load_driver() above.
    chmod(bin_path.c_str(), 0755);
    chown(bin_path.c_str(), 0, 0);

    pid_t child = fork();
    if (child < 0) {
        daemon_log("Fork failed for binary %s: %s", bin_name.c_str(), strerror(errno));
        return false;
    }
    if (child == 0) {
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) dup2(devnull, 0);
        int logfd = open(CHEAT_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (logfd >= 0) {
            dup2(logfd, 1);
            dup2(logfd, 2);
            close(logfd);
        } else if (devnull >= 0) {
            dup2(devnull, 1);
            dup2(devnull, 2);
        }
        if (devnull >= 0) close(devnull);
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

// ─── watchdog supervision ────────────────────────────────────
// The watchdog restarts this daemon; this restarts the watchdog. Either one
// surviving brings the other back, so a single kill cannot leave the module
// dead until the next reboot.
//
// The pattern is bracketed: the helper shell that runs pgrep has the pattern
// in its own command line, and "watchdog[.]sh" does not match the literal
// text "watchdog[.]sh", so the check cannot see itself as a hit.
static const char* WATCHDOG_SH = INX_MODULE_DIR "/watchdog.sh";

static void ensure_watchdog_running() {
    if (!run_shell("pgrep -f 'watchdog[.]sh' 2>/dev/null").empty()) return;

    if (access(WATCHDOG_SH, F_OK) != 0) {
        daemon_log("watchdog.sh missing: %s", WATCHDOG_SH);
        return;
    }
    chmod(WATCHDOG_SH, 0755);
    chown(WATCHDOG_SH, 0, 0);
    run_shell(std::string("setsid /system/bin/sh '") + WATCHDOG_SH + "' >/dev/null 2>&1 &");
    daemon_log("watchdog was not running, started it");
}

// ── Remote control server ───────────────────────────────────
// Hosted here rather than in the overlay binary: the overlay only lives
// while the game is open, so switching to a browser could take it — and the
// server — away with it. The daemon is kept alive by the watchdog, so the
// page answers whether the game is running or not.
//
// The daemon owns the values. The overlay picks them up from controls.json
// on its next frame, which is also how a change made with the game closed
// takes effect as soon as it opens.
static IxControls g_ctlState;

static void remote_register_controls() {
    ix_remote_add("esp_box",       "ESP", "Box",       IXC_BOOL, &g_ctlState.esp_box);
    ix_remote_add("esp_skeleton",  "ESP", "Skeleton",  IXC_BOOL, &g_ctlState.esp_skeleton);
    ix_remote_add("esp_name",      "ESP", "Name",      IXC_BOOL, &g_ctlState.esp_name);
    ix_remote_add("esp_health",    "ESP", "Health",    IXC_BOOL, &g_ctlState.esp_health);
    ix_remote_add("esp_distance",  "ESP", "Distance",  IXC_BOOL, &g_ctlState.esp_distance);
    ix_remote_add("esp_line",      "ESP", "ESP Line",  IXC_BOOL, &g_ctlState.esp_line);
    ix_remote_add("esp_360",       "ESP", "360 Alert", IXC_BOOL, &g_ctlState.esp_360);
    ix_remote_add("esp_textsize",  "ESP", "Text Size", IXC_INT,  &g_ctlState.esp_textsize,
                  10.f, 50.f, 1.f);

    ix_remote_add("aim_silent",    "Aim", "Aim Silent",      IXC_BOOL, &g_ctlState.aim_silent,
                  0, 1, 1, nullptr, "Head only");
    ix_remote_add("aim_hwbt",      "Aim", "Hardware BT",     IXC_BOOL, &g_ctlState.aim_hwbt);
    ix_remote_add("aim_fovcircle", "Aim", "Show FOV Circle", IXC_BOOL, &g_ctlState.aim_fovcircle);
    ix_remote_add("aim_mode",      "Aim", "Hardware BT Mode", IXC_ENUM, &g_ctlState.aim_mode,
                  1.f, 3.f, 1.f);
    ix_remote_add("aim_fov",       "Aim", "FOV Range",       IXC_FLOAT, &g_ctlState.aim_fov,
                  1.f, 360.f, 1.f);

    ix_remote_add("exp_pull",       "Exploit", "Enemies pull", IXC_BOOL,  &g_ctlState.exp_pull);
    ix_remote_add("exp_ghost",      "Exploit", "Ghost Hack",   IXC_BOOL,  &g_ctlState.exp_ghost);
    ix_remote_add("exp_norecoil",   "Exploit", "No Recoil",    IXC_BOOL,  &g_ctlState.exp_norecoil);
    ix_remote_add("exp_noreload",   "Exploit", "No Reload",    IXC_BOOL,  &g_ctlState.exp_noreload);
    ix_remote_add("exp_floatpull",  "Exploit", "Float: pull",  IXC_BOOL,  &g_ctlState.exp_floatpull);
    ix_remote_add("exp_floatghost", "Exploit", "Float: Ghost", IXC_BOOL,  &g_ctlState.exp_floatghost);
    ix_remote_add("exp_pullrange",  "Exploit", "Pull Range",   IXC_FLOAT, &g_ctlState.exp_pullrange,
                  1.f, 8.f, 0.1f, " m");
}

// Licence details for the page's Info tab, from the config the login binary
// writes — the same values the module's WebUI shows.
static void ix_remote_publish_info_from(const json& cfg) {
    static std::string lastKey;
    const std::string key = cfg.value("key", "");
    const std::string seller = cfg.value("seller", "");
    const std::string validity = cfg.value("validity", "");
    const std::string sig = key + "|" + seller + "|" + validity;
    if (sig == lastKey) return;            // only rebuild when it changes
    lastKey = sig;

    std::vector<IxInfoRow> rows;
    rows.push_back({"Seller",   seller.empty()   ? "-" : seller});
    rows.push_back({"Expiry",   validity.empty() ? "-" : validity});
    rows.push_back({"Status",   cfg.value("status", "") == "success" ? "Active" : "Not logged in"});
    rows.push_back({"Driver",   cfg.value("driver", "").empty() ? "none" : cfg.value("driver", "")});
    ix_remote_set_info(rows);
}

// ─── zombie reaping ──────────────────────────────────────────
static void reap_children(int /*sig*/) {
    while (waitpid(-1, nullptr, WNOHANG) > 0) {}
}

// ─── daemon pidfile ──────────────────────────────────────────
static const char* DAEMON_PIDFILE = INX_MODULE_DIR "/config/daemon_service.pid";
static const char* DAEMON_LOCK    = INX_MODULE_DIR "/config/daemon_service.lock";

// ===============================================================
int main(int /*argc*/, char* /*argv*/[]) {
    // Detach from terminal
    setsid();

    // Reap zombie children so we don't accumulate them
    signal(SIGCHLD, reap_children);

    // Ignore SIGHUP
    signal(SIGHUP, SIG_IGN);

    // Single instance. A second copy (watchdog restarting a daemon it could
    // not see, or a duplicate boot start) would run its own poll loop and fight
    // over the binary, which is what made the phone hot and the menu flicker.
    // The lock is held by this process only: O_CLOEXEC keeps it from leaking
    // into the binary it launches, so the lock frees when the daemon dies.
    mkdir(CFG_DIR, 0755);
    const int lockFd = open(DAEMON_LOCK, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (lockFd < 0 || flock(lockFd, LOCK_EX | LOCK_NB) != 0) {
        daemon_log("daemon_service_@api already running — this copy exits");
        return 0;
    }

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

    // Remote control is started from inside the poll loop, not here: the
    // daemon comes up at boot, long before anyone flips the switch in the
    // module, and reading the setting once at startup meant turning it on did
    // nothing until the next reboot.
    bool remoteStarted = false;

    bool launchedByDaemon = false;
    std::string lastBinaryName; // tracks which binary we launched
    time_t lastStartAttempt = 0;
    time_t launchedAt       = 0;
    bool   wasInTarget      = false;
    bool   inGame           = false;
    int    missCount        = 0;
    unsigned pollTick       = 0;
    int      driverRetries  = 0;
    // Set when the player leaves a running game; cleared when the game is
    // back on top, so the binary is not relaunched into the background.
    bool     awaitingForeground = false;

    for (;;) {
        json cfg = load_config_cached();

        // Determine target package (default: com.dts.freefiremax)
        std::string target = cfg.value("target_package", DEFAULT_TARGET);
        if (target.empty()) target = DEFAULT_TARGET;

        // One binary only. Bar vs Volume is config "menu_open", which the
        // binary reads itself.
        const std::string bin_name = "optimum_bar";

        std::string driver = cfg.value("driver", "");

        // Verified license key — written by inx_login_@api on success, and
        // re-verified by the binary itself against the server. Both logins
        // stay in place; this only decides whether launching is worth trying.
        std::string saved_key   = cfg.value("key", "");
        std::string login_state = cfg.value("status", "");

        // Remote control: the whole point is to drive the menu from a browser,
        // which means leaving the game. Stopping the binary on foreground loss
        // would kill the server the browser is talking to — that is exactly
        // why the page came back "connection refused". With remote on, the
        // binary is only stopped when the game process itself is gone.
        const bool remoteOn = cfg.value("remote_control", false);

        // "Don't stop": if the binary quits because the driver is not there,
        // run the driver and try again at once instead of waiting out the
        // backoff.
        const bool driverKeepAlive = cfg.value("driver_keep_alive", false);
        const bool  loggedIn    = (login_state == "success") && !saved_key.empty();

        // The cheap part, every tick: is the game running at all.
        const pid_t targetPid   = find_target_pid(target);
        const bool  targetAlive = (targetPid > 0);
        const bool  justAppeared = targetAlive && !wasInTarget;
        wasInTarget = targetAlive;

        const bool running = is_running(bin_name);

        // The binary exited by itself (driver missing, auth gate failed,
        // crash): log it once, then hold off before trying again.
        if (launchedByDaemon && !lastBinaryName.empty() && !is_running(lastBinaryName)) {
            daemon_log("%s exited on its own — reason in webroot/cheat.log", lastBinaryName.c_str());
            unlink(pid_file_path(lastBinaryName).c_str());

            // "Don't stop": the binary ran without its driver. Load the driver
            // and go straight round again rather than sitting out the backoff.
            // Bounded, so a driver that can never load cannot spin forever.
            bool retryNow = false;
            if (driverKeepAlive && cheat_log_blames_driver()) {
                if (driverRetries < MAX_DRIVER_RETRIES) {
                    ++driverRetries;
                    if (!driver.empty() && driver[0] == '/') {
                        daemon_log("Binary had no driver — loading %s and retrying (%d/%d)",
                                   driver.c_str(), driverRetries, MAX_DRIVER_RETRIES);
                        load_driver(driver);
                    } else {
                        daemon_log("Binary had no driver — retrying (%d/%d)",
                                   driverRetries, MAX_DRIVER_RETRIES);
                    }
                    retryNow = true;
                } else {
                    daemon_log("Driver still not ready after %d tries — backing off",
                               MAX_DRIVER_RETRIES);
                }
            }

            if (retryNow)                              lastStartAttempt = 0;
            else if (time(nullptr) - launchedAt < 30)  lastStartAttempt = time(nullptr);

            launchedByDaemon = false;
            lastBinaryName.clear();
        }

        // A fresh game launch always gets an attempt, whatever happened last
        // time, so a past failure can never block the menu on the next open.
        if (justAppeared) {
            lastStartAttempt   = 0;
            driverRetries      = 0;   // a new game session starts fresh
            awaitingForeground = false;
            missCount        = 0;
            inGame           = true;   // the game it just started is on top
            daemon_log("Game process appeared (pid %d) — starting binary now", (int)targetPid);
        }

        // The expensive part, only while the game is alive and only every few
        // ticks. It exists to notice the player leaving, nothing else.
        if (targetAlive && !justAppeared && (pollTick % FOREGROUND_EVERY_TICKS == 0)) {
            const bool fgIsTarget = (foreground_package() == target);
            if (fgIsTarget) {
                missCount = 0;
                inGame    = true;
                if (awaitingForeground) {
                    // Coming back to the game is a fresh start, not a retry
                    // after a failure, so it should not sit out the backoff
                    // that throttles a binary which keeps dying.
                    awaitingForeground = false;
                    lastStartAttempt   = 0;
                }
            } else if (++missCount >= LEAVE_MISS_POLLS) {
                inGame = false;
            }
        }
        if (!targetAlive) {
            inGame             = false;
            missCount          = 0;
            awaitingForeground = false;   // the game is gone; next open is fresh
        }

        const time_t now = time(nullptr);
        const bool backoffOver = (now - lastStartAttempt) >= RESTART_BACKOFF_SECS;

        // Launch as soon as the game process exists. The binary needs a few
        // seconds for its own login and driver init, and the game needs far
        // longer to load, so by the time the player can see anything the menu
        // is already up. Waiting for the foreground check first is what made
        // the menu show up late.
        // Launching is gated on the game's PROCESS existing, so the menu is
        // ready before the player can see anything. Stopping is gated on the
        // game being in the FOREGROUND. While the game is alive but in the
        // background both were true at once, so the binary was started and
        // stopped once a second. After a "player left" stop the game has to
        // come back to the foreground before it is launched again.
        if (targetAlive && !running && loggedIn && backoffOver && !awaitingForeground) {
            lastStartAttempt = now;

            // Absolute path -> an external driver executable, run here.
            // A name -> the binary picks that backend itself from config.json.
            if (!driver.empty() && driver[0] == '/') {
                load_driver(driver);
            } else if (!driver.empty()) {
                daemon_log("Driver '%s' selected — handled inside binary", driver.c_str());
            }

            if (start_binary(bin_name, saved_key)) {
                launchedByDaemon = true;
                lastBinaryName   = bin_name;
                launchedAt       = time(nullptr);
            }
        } else if (targetAlive && !running && !loggedIn) {
            static std::string lastWarnedTarget;
            if (lastWarnedTarget != target) {
                daemon_log("Game is open but no verified key — log in from the WebUI first");
                lastWarnedTarget = target;
            }
        } else if (launchedByDaemon && !lastBinaryName.empty() &&
                   (!targetAlive || (!inGame && !remoteOn))) {
            // The game closed, or the player left it for long enough — but
            // with remote control on, leaving the game is expected, so only a
            // closed game stops the binary.
            if (is_running(lastBinaryName)) {
                daemon_log(targetAlive ? "Player left the game — stopping binary"
                                       : "Game closed — stopping binary");
                stop_binary(lastBinaryName);
                // Only a closed game clears the backoff. Leaving the game is
                // not a failure, and zeroing it here is what let the next
                // tick relaunch immediately.
                if (!targetAlive) lastStartAttempt = 0;
                else              awaitingForeground = true;
                launchedByDaemon = false;
                lastBinaryName.clear();
            }
        }

        // Start the server the first time the module asks for it, whenever
        // that happens — no reboot needed.
        if (!remoteStarted && cfg.value("remote_control", false)) {
            ix_controls_load(g_ctlState);          // pick up where it left off
            remote_register_controls();
            if (ix_remote_start()) {
                remoteStarted = true;
                char url[160];
                snprintf(url, sizeof(url), "http://127.0.0.1:%d/?t=%s",
                         IX_REMOTE_PORT, ix_remote_token().c_str());
                ix_write_remote_url(url);
                ix_controls_save(g_ctlState);      // make sure the file exists
                daemon_log("Remote control listening on 127.0.0.1:%d", IX_REMOTE_PORT);
            } else {
                static time_t lastMoan = 0;
                if (time(nullptr) - lastMoan > 30) {   // do not spam the log
                    lastMoan = time(nullptr);
                    daemon_log("Remote control could not listen on port %d (%s)",
                               IX_REMOTE_PORT, strerror(errno));
                }
            }
        }

        // Anything the page changed: apply it to our copy and write the file
        // the overlay reads. Saving only when something actually moved keeps
        // this off the flash on an idle phone.
        if (remoteStarted) {
            const IxControls before = g_ctlState;
            ix_remote_apply();
            if (memcmp(&before, &g_ctlState, sizeof(IxControls)) != 0) {
                ix_controls_save(g_ctlState);
                daemon_log("Remote changed a setting");
            }
            ix_remote_publish_info_from(cfg);
        }

        ++pollTick;

        // Keep the watchdog alive, roughly every 15 s.
        if (pollTick % 15 == 0) ensure_watchdog_running();

        // Heartbeat for the WebUI and for the watchdog. "updated" is what
        // proves this loop is still turning: a daemon wedged on a hung
        // dumpsys keeps its process alive but stops updating this, and the
        // watchdog restarts it on that basis.
        if (pollTick % HEARTBEAT_EVERY_TICKS == 0) {
            json st;
            st["updated"]        = (long long)time(nullptr);
            st["pid"]            = (int)getpid();
            st["target"]         = target;
            st["target_alive"]   = targetAlive;
            st["in_game"]        = inGame;
            st["logged_in"]      = loggedIn;
            st["binary"]         = bin_name;
            st["binary_running"] = running;
            st["driver"]         = driver;
            write_status(st);
        }

        usleep(TICK_USEC);
    }

    return 0; // unreachable
}
