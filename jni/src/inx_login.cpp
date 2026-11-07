/* ============================================================
   inx_login.cpp — InxMods module login binary.

   Usage:  inx_login_@api <KEY>

   Output (one JSON line to stdout):
     Success: {"status":"success","message":"...","key":"...",
               "seller":"...","validity":"...","days":N,
               "saved":true|false,"save_error":"..."}
     Failure: {"status":"error","message":"<the server's own reason,
               e.g. License expired on ...>"}

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
#include <unistd.h>
#include <json.hpp>

using json = nlohmann::json;

// Server-provided strings can contain bytes that are not valid UTF-8.
// Plain dump() then throws type_error and aborts the process (exit 134)
// right after a successful verification. The replace handler swaps bad
// bytes for U+FFFD instead of crashing.
static std::string dump_safe(const json& j, int indent = -1) {
    return j.dump(indent, ' ', false, json::error_handler_t::replace);
}

#ifndef INX_MODULE_DIR
#define INX_MODULE_DIR "/data/adb/modules/inx_mods"
#endif

static const char* WEB_CFG = INX_MODULE_DIR "/webroot/config.json";

// -------------------------------------------------------------
//  Single JSON line to stdout. nlohmann handles all escaping.
// -------------------------------------------------------------
static void print_error(const std::string& msg) {
    json out = { {"status", "error"}, {"message", msg} };
    printf("%s\n", dump_safe(out).c_str());
}

// -------------------------------------------------------------
//  [CRASH FIX - exit 135 "Bus error", empty stdout]
//  Returning from main() used to leave the process to tear itself down
//  while the detached integrity watchdog (login.h) was still ticking.
//  That watchdog reads the binary's own file-backed .rodata on every
//  tick, so a tick landing inside the teardown window faults on a page
//  that is no longer valid - SIGBUS, which the shell reports as exit
//  135. stdout on a pipe is block-buffered, so the JSON line this
//  binary had already printf()'d was still in the buffer and went down
//  with the process, which is why the caller saw a crash AND no output
//  at all ("Login output not JSON ... out: (empty stdout)").
//
//  Every exit now goes through here: park the watchdog, flush the JSON
//  line out for real, then _exit(), which ends the process at the
//  kernel level with no userspace teardown and therefore no window for
//  any thread to fault in. The JSON contract is unchanged.
// -------------------------------------------------------------
//  [[noreturn]] matters: without it the compiler assumes finish() can
//  come back, so the argument guard below would be treated as falling
//  through into argv[1] on a missing-key call. It never does at runtime
//  (_exit is noreturn), but saying so keeps the control flow honest and
//  lets the compiler drop the dead paths.
[[noreturn]] static void finish(int code) {
    kd_stop_integrity_watchdog();
    fflush(stdout);
    fflush(stderr);
    _exit(code);
}

// -------------------------------------------------------------
//  Read config.json -> update key/seller/validity/status -> write.
//  Other fields (binary, duration, driver, ...) are preserved.
//  Returns "" on success, otherwise the real reason (errno text).
// -------------------------------------------------------------
static std::string update_web_config(const std::string& key,
                                     const std::string& seller,
                                     const std::string& validity,
                                     const std::string& status,
                                     const std::string& message) {
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
    // Written on every outcome so a refusal reason from an earlier
    // attempt (e.g. "License expired on ...") can never linger next to
    // a key that has since been accepted.
    cfg["message"]  = message;

    errno = 0;
    std::ofstream out(WEB_CFG, std::ios::trunc);
    if (!out.good()) {
        return std::string("open failed: ") + strerror(errno) + " (" + WEB_CFG + ")";
    }
    out << dump_safe(cfg, 4);
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
        finish(1);
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

        const std::string save_error = update_web_config(key, seller, expiry, "success", msg);

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
        printf("%s\n", dump_safe(out).c_str());
        finish(0);
    }

    // Failure: mark status as error but keep the previously saved key
    // so the user can retry without retyping it.
    //
    // [FIX - an expired key reported nothing about expiry] The server
    // already says exactly why it refused ("License expired on ...",
    // "License is not valid.", a device-limit message, ...), and
    // run_silent() keeps that text in auth_message. This path used to
    // throw it away and print one fixed line - "Please check your key
    // and try again" - so an EXPIRED licence was indistinguishable from
    // a typo, and the status panel had nothing to show either. The real
    // reason is now what gets printed AND what gets written to
    // config.json, so both the login screen and a later status read
    // show why the key was refused. No new source of truth: it is the
    // server's own signed message, unchanged.
    std::string why = auth.getAuthMessage();
    if (why.empty())
        why = "Login failed. Please check your key and try again.";

    {
        json cfg = json::object();
        std::ifstream f(WEB_CFG);
        if (f.good()) {
            try { json tmp; f >> tmp; if (tmp.is_object()) cfg = tmp; } catch (...) {}
        }
        cfg["status"]  = "error";
        cfg["message"] = why;
        // "key"/"seller"/"validity" are deliberately left as they were:
        // the previous expiry stays on screen next to the reason, and
        // the saved key survives so the user can retry without retyping.
        std::ofstream out(WEB_CFG, std::ios::trunc);
        if (out.good()) { out << dump_safe(cfg, 4); chmod(WEB_CFG, 0644); }
    }

    print_error(why);
    finish(1);
}
