/* ============================================================
   inx_login.cpp — InxMods module login binary.

   Usage:  inx_login_@api <KEY>

   Output (one JSON line to stdout):
     Success: {"status":"success","message":"...","key":"...",
               "seller":"...","validity":"...","days":N}
     Failure: {"status":"error","message":"..."}

   Exit code: 0 on success, 1 on failure.

   On success it also writes webroot/config.json (key/seller/
   validity/status) so the WebUI and the daemon read the same
   verified state. The JSON on stdout carries the same fields so the
   WebUI can show Key Info immediately, without relying on a re-read
   of config.json.

   Module dir is overridable at compile time:
     -DINX_MODULE_DIR="/data/adb/modules/inx_mods"
   ============================================================ */

#include "login.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
#include <json.hpp>

using json = nlohmann::json;

#ifndef INX_MODULE_DIR
#define INX_MODULE_DIR "/data/adb/modules/inx_mods"
#endif

static const char* WEB_CFG = INX_MODULE_DIR "/webroot/config.json";

// -------------------------------------------------------------
//  Single JSON line to stdout. nlohmann handles all escaping.
// -------------------------------------------------------------
static void print_error(const std::string& msg) {
    json out = { {"status", "error"}, {"message", msg} };
    printf("%s\n", out.dump().c_str());
}

// -------------------------------------------------------------
//  Read config.json -> update key/seller/validity/status -> write
//  Other fields (binary, duration, driver, ...) are preserved.
// -------------------------------------------------------------
static bool update_web_config(const std::string& key,
                               const std::string& seller,
                               const std::string& validity,
                               const std::string& status) {
    json cfg = json::object();
    {
        std::ifstream f(WEB_CFG);
        if (f.good()) {
            try {
                json tmp; f >> tmp;
                if (tmp.is_object()) cfg = tmp;
            } catch (...) {}
        }
    }

    cfg["key"]      = key;
    cfg["seller"]   = seller;
    cfg["validity"] = validity;
    cfg["status"]   = status;

    std::ofstream out(WEB_CFG, std::ios::trunc);
    if (!out.good()) return false;
    out << cfg.dump(4);
    out.close();
    chmod(WEB_CFG, 0644);
    return true;
}

// ===============================================================
int main(int argc, char* argv[]) {
    if (argc < 2 || strlen(argv[1]) == 0) {
        print_error("Missing license key argument.");
        return 1;
    }

    const std::string key = argv[1];

    Authentication auth;
    const bool ok = auth.run_silent(key);

    if (ok) {
        const std::string seller = auth.getSellerName();
        const std::string expiry = auth.getExpiryDate();
        const int         days   = auth.getDaysRemain();

        char msg[512];
        if (days > 0) {
            snprintf(msg, sizeof(msg),
                     "Welcome %s! Valid till %s (%d day%s remaining)",
                     seller.c_str(), expiry.c_str(),
                     days, days == 1 ? "" : "s");
        } else {
            snprintf(msg, sizeof(msg),
                     "Welcome %s! Valid till %s",
                     seller.c_str(), expiry.c_str());
        }

        if (!update_web_config(key, seller, expiry, "success")) {
            print_error("Login verified, but config.json could not be written.");
            return 1;
        }

        json out = {
            {"status",   "success"},
            {"message",  msg},
            {"key",      key},
            {"seller",   seller},
            {"validity", expiry},
            {"days",     days}
        };
        printf("%s\n", out.dump().c_str());
        return 0;
    }

    // Failure: mark status as error but keep the previously saved key
    // so the user can retry without retyping it.
    {
        json cfg = json::object();
        std::ifstream f(WEB_CFG);
        if (f.good()) {
            try { json tmp; f >> tmp; if (tmp.is_object()) cfg = tmp; } catch (...) {}
        }
        cfg["status"] = "error";
        std::ofstream out(WEB_CFG, std::ios::trunc);
        if (out.good()) { out << cfg.dump(4); chmod(WEB_CFG, 0644); }
    }

    print_error("Login failed. Please check your key and try again.");
    return 1;
}
