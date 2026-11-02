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
#include <fcntl.h>
#include <sys/file.h>
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

// The launched binary's stdout/stderr go here (overwritten each start), so a
// failure reason is visible instead of vanishing into /dev/null.
static const char* CHEAT_LOG = INX_MODULE_DIR "/webroot/cheat.log";

// After a launch, wait this long before trying again. Every launch does a
// full server login, so a binary that keeps exiting must not be relaunched
// every poll cycle.
static const int RESTART_BACKOFF_SECS = 60;

// Consecutive polls the target must be missing from the foreground before the
// game counts as closed. Focus flickers for a poll during dialogs and system
// windows; without this the menu was killed in the middle of a match.
static const int LEAVE_MISS_POLLS = 4;

// Poll interval while the game process exists. 1.5 s with heavy dumpsys calls
// kept the CPU busy all the time.
static const useconds_t POLL_BUSY_USEC = 2000 * 1000;

// Poll interval while the game is not even running. Checking whether its
// process exists is a cheap pgrep, so over a 24 h day almost no dumpsys call
// is made at all — that is where the heat and battery drain came from.
static const useconds_t POLL_IDLE_USEC = 5000 * 1000;

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

// ─── cheap target-process check ──────────────────────────────
// Whether the game is running at all. A pgrep over /proc costs almost
// nothing next to dumpsys, which has to build its whole dump inside
// system_server. The expensive foreground check is skipped entirely unless
// this says the game is alive.
//
// The package is wrapped as com.dts[.]freefiremax so the helper shell, whose
// own command line contains the pattern, is never matched as a hit.
static std::string bracket_first_dot(const std::string& pkg) {
    const size_t dot = pkg.find('.');
    if (dot == std::string::npos) return pkg;
    return pkg.substr(0, dot) + "[.]" + pkg.substr(dot + 1);
}

static bool target_process_exists(const std::string& pkg) {
    if (pkg.empty()) return false;
    const std::string cmd = "pgrep -f '" + bracket_first_dot(pkg) + "' 2>/dev/null";
    return !run_shell(cmd).empty();
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

    bool launchedByDaemon = false;
    std::string lastBinaryName; // tracks which binary we launched
    time_t lastStartAttempt = 0;
    time_t launchedAt       = 0;
    bool   wasInTarget      = false;
    bool   inGame           = false;
    int    missCount        = 0;
    unsigned pollTick       = 0;

    for (;;) {
        json cfg = load_config();

        // Determine target package (default: com.dts.freefiremax)
        std::string target = cfg.value("target_package", DEFAULT_TARGET);
        if (target.empty()) target = DEFAULT_TARGET;

        // Determine which binary to run
        // One binary only. Bar vs Volume is config "menu_open", which the
        // binary reads itself.
        const std::string bin_name = "optimum_bar";

        // Driver selection
        std::string driver = cfg.value("driver", "");

        // Verified license key — written by inx_login_@api on success.
        // Passed straight through to the binary's own argv[1] so it
        // re-verifies the EXACT key already accepted, with no dependency
        // on the two binaries agreeing on a shared on-disk key location.
        std::string saved_key   = cfg.value("key", "");
        std::string login_state = cfg.value("status", "");
        const bool  loggedIn    = (login_state == "success") && !saved_key.empty();

        // The game not being alive at all is the common case over a day, and
        // answering that costs one pgrep. dumpsys is only asked once the game
        // actually exists.
        const bool targetAlive = target_process_exists(target);

        // Debounced game state: the game is "closed" only after
        // LEAVE_MISS_POLLS polls in a row without it in the foreground.
        if (targetAlive && foreground_package() == target) {
            missCount = 0;
            inGame    = true;
        } else if (++missCount >= LEAVE_MISS_POLLS) {
            inGame = false;
        }
        const bool inTarget = inGame;
        const bool running  = is_running(bin_name);

        // Binary that exited on its own (driver missing, auth gate failed,
        // crash): log it once, then wait RESTART_BACKOFF_SECS before retrying.
        if (launchedByDaemon && !lastBinaryName.empty() && !is_running(lastBinaryName)) {
            daemon_log("%s exited on its own — reason in webroot/cheat.log", lastBinaryName.c_str());
            unlink(pid_file_path(lastBinaryName).c_str());
            // Quick exit = a failure, so back off. A long run that ended is
            // a normal stop and must relaunch on the next game open.
            if (time(nullptr) - launchedAt < 30) lastStartAttempt = time(nullptr);
            launchedByDaemon = false;
            lastBinaryName.clear();
        }

        // A new game session (target just came to the foreground) always gets
        // a fresh launch attempt. Backoff only throttles retries inside one
        // session, so a failed launch can't block the menu on the next open.
        if (inTarget && !wasInTarget) {
            lastStartAttempt = 0;
            daemon_log("Target %s came to foreground", target.c_str());
        }
        wasInTarget = inTarget;

        const time_t now = time(nullptr);
        const bool backoffOver = (now - lastStartAttempt) >= RESTART_BACKOFF_SECS;

        if (inTarget && !running && loggedIn && backoffOver) {
            lastStartAttempt = now;
            daemon_log("Target %s in foreground — starting binary", target.c_str());

            // Driver handling:
            //  - Absolute path  -> external driver executable, run here.
            //  - Name (rtdev, qx1, paradise, ditpro, ...) -> the binary
            //    itself selects that in-process backend from config.json
            //    "driver", so nothing is executed here.
            if (!driver.empty() && driver[0] == '/') {
                load_driver(driver);
            } else if (!driver.empty()) {
                daemon_log("Driver '%s' selected — handled inside binary", driver.c_str());
            }

            // Launch binary (key forwarded as argv[1] — see start_binary() note)
            if (start_binary(bin_name, saved_key)) {
                launchedByDaemon = true;
                lastBinaryName   = bin_name;
                launchedAt       = time(nullptr);
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
                lastStartAttempt = 0;
                launchedByDaemon = false;
                lastBinaryName.clear();
            }
        }

        // Check the watchdog roughly every 15 s rather than every poll, so the
        // supervision costs one cheap pgrep instead of a constant one.
        if (++pollTick % 8 == 0) ensure_watchdog_running();

        // Heartbeat for the WebUI and for the watchdog. "updated" is what
        // proves this loop is still turning: a daemon wedged on a hung
        // dumpsys keeps its process alive but stops updating this, and the
        // watchdog restarts it on that basis.
        {
            json st;
            st["updated"]       = (long long)time(nullptr);
            st["pid"]           = (int)getpid();
            st["target"]        = target;
            st["target_alive"]  = targetAlive;
            st["in_game"]       = inTarget;
            st["logged_in"]     = loggedIn;
            st["binary"]        = bin_name;
            st["binary_running"]= running;
            st["driver"]        = driver;
            write_status(st);
        }

        usleep(targetAlive ? POLL_BUSY_USEC : POLL_IDLE_USEC);
    }

    return 0; // unreachable
}
