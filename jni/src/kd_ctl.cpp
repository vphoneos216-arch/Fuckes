/* ============================================================
   kd_ctl.cpp — compiled control/daemon helper for the InvisibleX
   WebUI module. NOT a shell script on purpose: the login check and
   the start/stop logic are both native, stripped, hybrid-signed-
   crypto-linked binaries, same hardening level as stranger.sh
   itself — nothing here is human-readable or patchable the way a
   .sh control script would be.

   Talks to the exact same config.json the main binary already
   reads (jni/src/ix_driver_init.h):
     /data/adb/modules/invisiblex_loader/config/config.json
       { "driver": "qx1", "menu_open": "bar" }

   Subcommands (all print one line of JSON to stdout):
     kd_ctl login <KEY>        run_silent(key) against the SAME
                                hybrid-signed (Ed25519+Ed448+ML-DSA-65)
                                server check stranger.sh itself uses,
                                and on success persists the key the
                                same way stranger.sh would (encrypted
                                at rest, same file, same master key) —
                                so the next "start" finds a saved key
                                and never hits the interactive prompt.
     kd_ctl status             { running, has_saved_key }
     kd_ctl start               launch stranger.sh in the background
     kd_ctl stop                kill it
     kd_ctl set-driver <name>   write config.json "driver"
     kd_ctl set-menu <vol|bar>  write config.json "menu_open"
   ============================================================ */

#include "login.h"
#include "ix_driver_init.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
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

static std::string kd_pidfile()  { return std::string(KD_MODULE_DIR) + "/config/stranger.pid"; }
static std::string kd_binpath()  { return std::string(KD_MODULE_DIR) + "/bin/stranger.sh"; }
static std::string kd_cfgpath()  { return std::string(KD_MODULE_DIR) + "/config/config.json"; }

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

static pid_t kd_read_pid() {
    std::ifstream f(kd_pidfile());
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

int main(int argc, char* argv[]) {
    if (argc < 2) {
        kd_print(false, R"(,"error":"usage: kd_ctl <login|status|start|stop|set-driver|set-menu> [arg]")");
        return 1;
    }
    const std::string cmd = argv[1];

    // ---------------------------------------------------------
    if (cmd == "login") {
        if (argc < 3) { kd_print(false, R"(,"error":"missing key"")"); return 1; }
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
        pid_t p = kd_read_pid();
        const bool running = kd_pid_alive(p);
        const bool hasKey = !kd_load_saved_key().empty();
        json cfg = kd_load_config();
        char buf[256];
        snprintf(buf, sizeof(buf),
            R"(,"running":%s,"has_saved_key":%s,"driver":"%s","menu_open":"%s")",
            running ? "true" : "false",
            hasKey ? "true" : "false",
            cfg.value("driver", "auto").c_str(),
            cfg.value("menu_open", "bar").c_str());
        kd_print(true, buf);
        return 0;
    }

    // ---------------------------------------------------------
    if (cmd == "start") {
        pid_t existing = kd_read_pid();
        if (kd_pid_alive(existing)) { kd_print(true, R"(,"already_running":true)"); return 0; }

        if (kd_load_saved_key().empty()) {
            kd_print(false, R"(,"error":"no_saved_key")");
            return 1;
        }

        pid_t child = fork();
        if (child < 0) { kd_print(false, R"(,"error":"fork_failed")"); return 1; }
        if (child == 0) {
            // Detach fully: new session, stdio to /dev/null, so the
            // WebUI/daemon process exiting never takes stranger.sh
            // down with it.
            setsid();
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); }
            execl(kd_binpath().c_str(), kd_binpath().c_str(), (char*)nullptr);
            _exit(127);
        }
        std::ofstream pf(kd_pidfile(), std::ios::trunc);
        if (pf.good()) { pf << child; pf.close(); chmod(kd_pidfile().c_str(), 0644); }
        kd_print(true, "");
        return 0;
    }

    // ---------------------------------------------------------
    if (cmd == "stop") {
        pid_t p = kd_read_pid();
        if (kd_pid_alive(p)) {
            kill(p, SIGTERM);
            for (int i = 0; i < 20 && kd_pid_alive(p); ++i) usleep(100000);
            if (kd_pid_alive(p)) kill(p, SIGKILL);
        }
        unlink(kd_pidfile().c_str());
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

    kd_print(false, R"(,"error":"unknown command")");
    return 1;
}
