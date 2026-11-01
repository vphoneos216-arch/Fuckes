/* ============================================================
   inx_login.cpp — InxMods module login binary.

   Usage:  inx_login_@api <KEY>

   Output (one JSON line to stdout):
     Success: {"status":"success","message":"Welcome <seller>! Valid till <expiry>"}
     Failure: {"status":"error","message":"Login failed. Please try again."}

   On success:
     - Saves key via kd_save_key() (same slot INX.SH / optimum_vol use)
     - Writes /data/adb/modules/inx_mods/webroot/config.json updating:
         "key", "seller", "validity", "status"

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
//  JSON output — matches what script.js login handler expects
// -------------------------------------------------------------
static void print_result(bool ok, const std::string& msg) {
    printf("{\"status\":\"%s\",\"message\":\"%s\"}\n",
           ok ? "success" : "error",
           msg.c_str());
}

// -------------------------------------------------------------
//  Escape a string for embedding inside a JSON string value
// -------------------------------------------------------------
static std::string json_esc(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if      (c == '"')  { out += "\\\""; }
        else if (c == '\\') { out += "\\\\"; }
        else if (c == '\n') { out += "\\n";  }
        else if (c == '\r') { out += "\\r";  }
        else if ((unsigned char)c < 0x20) continue;
        else out += c;
    }
    return out;
}

// -------------------------------------------------------------
//  Read config.json → update key/seller/validity/status → write
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

    // Preserve existing fields (binary, duration) — only overwrite the above
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
        print_result(false, "Missing license key argument.");
        return 1;
    }

    const std::string key = argv[1];

    Authentication auth;
    const bool ok = auth.run_silent(key);

    if (ok) {
        const std::string seller  = auth.getSellerName();
        const std::string expiry  = auth.getExpiryDate();
        const int         days    = auth.getDaysRemain();

        // Compose friendly message
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

        // Persist: key file + webroot config.json
        update_web_config(key, seller, expiry, "success");

        print_result(true, json_esc(msg));
        return 0;
    } else {
        // On failure clear status in config (don't clear the key field —
        // user may retry; we leave the previously working key untouched)
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

        print_result(false, "Login failed. Please check your key and try again.");
        return 1;
    }
}
