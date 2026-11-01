#pragma once
// ix_driver_init.h — WebUI driver → wanbai mode
// DEFAULT is AUTO (-1). Empty config must NOT force KMA (8).
//
// Modes:
//   -1/0 AUTO
//   1 QX1  2 GT  3 rt-dev  4 rt-proc  5 QX2
//   6 TB-KPM  7 DitPro  8 KMA  9 LDK  10 Paradise

#include <string>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <cctype>

// InxMods module id is "inx_mods" and its WebUI writes config.json to
// webroot/ — the old invisiblex_loader path never existed for this module,
// so the WebUI driver selection never reached the binary at all.
#define IX_CONFIG_PATH "/data/adb/modules/inx_mods/webroot/config.json"

static inline int ix_driver_number(const std::string& v) {
    if (v.empty()) return -1;  // AUTO — was wrongly 8
    if (v == "0" || v == "-1") return -1;
    if (v == "10") return 10;
    if (v.size() == 1 && v[0] >= '1' && v[0] <= '9') return v[0] - '0';

    std::string s;
    for (char c : v)
        if (c != '-' && c != '_' && c != ' ')
            s += (char)tolower((unsigned char)c);

    if (s == "auto" || s == "detect" || s == "automatic") return -1;
    if (s == "qx1") return 1;
    if (s == "gt") return 2;
    if (s == "rtdev") return 3;
    if (s == "rtproc") return 4;
    if (s == "qx2" || s == "qx") return 5;
    if (s == "tbkpm" || s == "tb") return 6;
    if (s == "ditprokpm" || s == "ditpro") return 7;
    if (s == "kmakpm" || s == "kma") return 8;
    if (s == "ldkkpm" || s == "ldk") return 9;
    if (s == "paradise" || s == "para") return 10;
    return -1;  // unknown → AUTO (was 8)
}

static inline std::string ix_read_driver_raw() {
    if (const char* e = getenv("IX_DRIVER_NAME"))
        if (*e) return std::string(e);

    const char* p = getenv("IX_CONFIG");
    std::ifstream f(p && *p ? p : IX_CONFIG_PATH);
    if (!f.good()) return "";

    std::string all((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());
    size_t k = all.find("\"driver\"");
    if (k == std::string::npos) return "";
    size_t c = all.find(':', k);
    if (c == std::string::npos) return "";
    size_t q1 = all.find('"', c);
    if (q1 == std::string::npos) return "";
    size_t q2 = all.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return all.substr(q1 + 1, q2 - q1 - 1);
}

static inline int ix_get_driver_choice() {
    // Env force
    if (const char* e = getenv("IX_DRIVER_MODE")) {
        if (*e) {
            int n = atoi(e);
            if (n == 0) return -1;
            if (n >= 1 && n <= 10) return n;
            if (n == -1) return -1;
        }
    }
    return ix_driver_number(ix_read_driver_raw());
}

// ── MENU OPEN MODE — same config.json, key "menu_open" ──────────
// Mirrors ix_get_driver_choice()'s lookup precedence: env var first
// (IX_MENU_OPEN), then the WebUI module's config.json, defaulting to
// "bar" (tap-to-open on-screen bar) when neither is set.
static inline std::string ix_get_menu_open_mode() {
    if (const char* e = getenv("IX_MENU_OPEN")) {
        std::string v(e);
        if (v == "vol" || v == "bar") return v;
    }

    const char* p = getenv("IX_CONFIG");
    std::ifstream f(p && *p ? p : IX_CONFIG_PATH);
    if (!f.good()) return "bar";

    std::string all((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());
    size_t k = all.find("\"menu_open\"");
    if (k == std::string::npos) return "bar";
    size_t c = all.find(':', k);
    if (c == std::string::npos) return "bar";
    size_t q1 = all.find('"', c);
    if (q1 == std::string::npos) return "bar";
    size_t q2 = all.find('"', q1 + 1);
    if (q2 == std::string::npos) return "bar";
    std::string v = all.substr(q1 + 1, q2 - q1 - 1);
    return (v == "vol" || v == "bar") ? v : "bar";
}

static inline std::string ix_get_selected_driver() {
    int n = ix_get_driver_choice();
    switch (n) {
        case -1: case 0: return "auto";
        case 1: return "qx1";
        case 2: return "gt";
        case 3: return "rt-dev";
        case 4: return "rt-proc";
        case 5: return "qx2";
        case 6: return "tb-kpm";
        case 7: return "ditpro";
        case 8: return "kma";
        case 9: return "ldk";
        case 10: return "paradise";
        default: return "auto";
    }
}
