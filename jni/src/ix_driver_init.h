#pragma once
// ix_driver_init.h — WebUI driver → wanbai mode
// DEFAULT is AUTO (-1). Empty config must NOT force KMA (8).
//
// Modes:
//   -1/0 AUTO
//   1 QX1  2 GT  3 rt-dev  4 rt-proc  5 QX2
//   6 TB-KPM  7 DitPro  8 KMA  9 LDK  10 Paradise  11 Bing  12 Dit-KPM/Dits-KO
//   13 RT Hook

#include <string>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <cctype>

// InxMods module id is "inx_mods" and its WebUI writes config.json to
// webroot/ — the old invisiblex_loader path never existed for this module,
// so the WebUI driver selection never reached the binary at all.
//
// Derived from INX_MODULE_DIR like every other path in the project. It used
// to be hardcoded, which meant a build with INX_MODULE_DIR pointed somewhere
// else still read and wrote the real one.
#ifndef INX_MODULE_DIR
#define INX_MODULE_DIR "/data/adb/modules/inx_mods"
#endif
#define IX_CONFIG_PATH INX_MODULE_DIR "/webroot/config.json"

static inline int ix_driver_number(const std::string& v) {
    if (v.empty()) return -1;  // AUTO — was wrongly 8
    if (v == "0" || v == "-1") return -1;
    if (v == "10") return 10;
    if (v == "11") return 11;
    if (v == "12") return 12;
    if (v == "13") return 13;
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
    if (s == "bing") return 11;
    if (s == "ditkpm" || s == "ditko" || s == "dits" || s == "ditskpm") return 12;
    if (s == "rthook") return 13;  // bare "rt" deliberately NOT aliased here -- too easy to confuse with rt-dev/rt-proc (modes 3/4)
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
            if (n >= 1 && n <= 13) return n;
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

// ── SCREENSHOT VISIBILITY — same config.json, key "show_in_capture" ──
// The overlay layer is created with eSkipScreenshot (0x40) unless this is
// on, which is why the menu never shows up in a screenshot or a recording.
// draw.cpp reads this when it creates the surface, so a change takes effect
// the next time the overlay starts.
static inline bool ix_get_show_in_capture() {
    if (const char* e = getenv("IX_SHOW_IN_CAPTURE"))
        if (*e) return (*e == '1' || *e == 't' || *e == 'T');

    const char* p = getenv("IX_CONFIG");
    std::ifstream f(p && *p ? p : IX_CONFIG_PATH);
    if (!f.good()) return false;

    std::string all((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());
    size_t k = all.find("\"show_in_capture\"");
    if (k == std::string::npos) return false;
    size_t c = all.find(':', k);
    if (c == std::string::npos) return false;
    // accept true/false and 1/0
    for (size_t i = c + 1; i < all.size(); ++i) {
        const char ch = all[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '"') continue;
        return (ch == 't' || ch == 'T' || ch == '1');
    }
    return false;
}

// Writes the key back, preserving the rest of the file. Used by the menu's
// "Show" switch; a missing key is appended before the closing brace.
static inline bool ix_set_show_in_capture(bool on) {
    const char* envp = getenv("IX_CONFIG");
    const std::string path = (envp && *envp) ? envp : IX_CONFIG_PATH;

    std::string all;
    {
        std::ifstream f(path);
        if (!f.good()) return false;
        all.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
    if (all.empty()) return false;

    const std::string val = on ? "true" : "false";
    size_t k = all.find("\"show_in_capture\"");
    if (k != std::string::npos) {
        size_t c = all.find(':', k);
        if (c == std::string::npos) return false;
        size_t b = c + 1;
        while (b < all.size() && (all[b] == ' ' || all[b] == '\t')) ++b;
        size_t e = b;
        while (e < all.size() && all[e] != ',' && all[e] != '}' && all[e] != '\n') ++e;
        all.replace(b, e - b, val);
    } else {
        size_t close = all.rfind('}');
        if (close == std::string::npos) return false;
        size_t prev = all.find_last_not_of(" \t\r\n", close - 1);
        if (prev == std::string::npos) return false;
        // Insert straight after the last value so the comma stays on its line.
        const bool needComma = (all[prev] != '{');
        all.insert(prev + 1, std::string(needComma ? "," : "") +
                             "\n    \"show_in_capture\": " + val);
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.good()) return false;
    out << all;
    return out.good();
}

// ── REMOTE CONTROL — same config.json, key "remote_control" ──
// On, the in-game menu is not drawn and the controls are driven from the
// page the binary serves on 127.0.0.1 instead. Read once at startup.
static inline bool ix_get_remote_control() {
    if (const char* e = getenv("IX_REMOTE_CONTROL"))
        if (*e) return (*e == '1' || *e == 't' || *e == 'T');

    const char* p = getenv("IX_CONFIG");
    std::ifstream f(p && *p ? p : IX_CONFIG_PATH);
    if (!f.good()) return false;

    std::string all((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());
    size_t k = all.find("\"remote_control\"");
    if (k == std::string::npos) return false;
    size_t c = all.find(':', k);
    if (c == std::string::npos) return false;
    for (size_t i = c + 1; i < all.size(); ++i) {
        const char ch = all[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '"') continue;
        return (ch == 't' || ch == 'T' || ch == '1');
    }
    return false;
}

// The page needs the token the binary generated, so it is written back for
// the module's WebUI to read and show as a link.
static inline bool ix_write_remote_url(const std::string& url) {
    const char* envp = getenv("IX_CONFIG");
    const std::string path = (envp && *envp) ? envp : IX_CONFIG_PATH;

    std::string all;
    {
        std::ifstream f(path);
        if (!f.good()) return false;
        all.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
    if (all.empty()) return false;

    const std::string val = "\"" + url + "\"";
    size_t k = all.find("\"remote_url\"");
    if (k != std::string::npos) {
        size_t c = all.find(':', k);
        if (c == std::string::npos) return false;
        size_t b = c + 1;
        while (b < all.size() && (all[b] == ' ' || all[b] == '\t')) ++b;
        size_t e = b;
        if (e < all.size() && all[e] == '"') { ++e; while (e < all.size() && all[e] != '"') ++e; ++e; }
        else while (e < all.size() && all[e] != ',' && all[e] != '}' && all[e] != '\n') ++e;
        all.replace(b, e - b, val);
    } else {
        size_t close = all.rfind('}');
        if (close == std::string::npos) return false;
        size_t prev = all.find_last_not_of(" \t\r\n", close - 1);
        if (prev == std::string::npos) return false;
        const bool needComma = (all[prev] != '{');
        all.insert(prev + 1, std::string(needComma ? "," : "") +
                             "\n    \"remote_url\": " + val);
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.good()) return false;
    out << all;
    return out.good();
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
        case 11: return "bing";
        case 12: return "ditkpm";
        default: return "auto";
    }
}
