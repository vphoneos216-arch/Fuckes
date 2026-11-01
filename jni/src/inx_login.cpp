/* ============================================================
   inx_login.cpp — InxMods module login binary.

   Usage:  inx_login_@api <KEY>

   Output (one JSON line to stdout):
     Success: {"status":"success","message":"...","key":"...",
               "seller":"...","validity":"...","days":N,
               "saved":true|false,"save_error":"..."}
     Failure: {"status":"error","message":"..."}

   The key is verified server-side first. If that passes, the binary
   tries to write webroot/config.json. The write result is reported in
   "saved" / "save_error" instead of being ignored, so the WebUI can
   save the same verified data itself when the binary cannot write.

   Exit code: 0 on success (even when saved=false, the key is verified),
   1 on verification failure.

   Module dir is overridable at compile time:
     -DINX_MODULE_DIR="/data/adb/modules/inx_mods"
   ============================================================ */

#include "login.h"
#include <cerrno>
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
//  Read config.json -> update key/seller/validity/status -> write.
//  Other fields (binary, duration, driver, ...) are preserved.
//  Returns "" on success, otherwise the real reason (errno text).
// -------------------------------------------------------------
static std::string update_web_config(const std::string& key,
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

    errno = 0;
    std::ofstream out(WEB_CFG, std::ios::trunc);
    if (!out.good()) {
        return std::string("open failed: ") + strerror(errno) + " (" + WEB_CFG + ")";
    }
    out << cfg.dump(4);
    out.close();
    if (out.fail()) {
        return std::string("write failed: ") + strerror(errno) + " (" + WEB_CFG + ")";
    }
    chmod(WEB_CFG, 0644);
    return "";
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

        const std::string save_error = update_web_config(key, seller, expiry, "success");

        json out = {
            {"status",     "success"},
            {"message",    msg},
            {"key",        key},
            {"seller",     seller},
            {"validity",   expiry},
            {"days",       days},
            {"saved",      save_error.empty()},
            {"save_error", save_error}
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
