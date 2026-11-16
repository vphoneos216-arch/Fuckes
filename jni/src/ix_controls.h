#pragma once
// ── Shared control state ────────────────────────────────────
// The remote page used to be served from inside the overlay binary, which
// only exists while the game is open. Switching to a browser backgrounds the
// game, Android is free to kill it, and the server went with it — that is
// what "ERR_CONNECTION_REFUSED" was: a valid link with nothing listening.
//
// So the server now lives in the daemon, which runs all the time, and the
// two processes meet in this file instead:
//
//   daemon  : owns webroot/controls.json, writes it when the page changes
//             something
//   overlay : reads it every frame (only when the file's mtime moves) and
//             copies the values into the variables the features read
//
// That means the page answers whether or not the game is running, and a
// change made while the game is closed is picked up the moment it opens.

#include <fstream>
#include <string>
#include <cstdlib>
#include <sys/stat.h>
#include <json.hpp>

#ifndef INX_MODULE_DIR
#define INX_MODULE_DIR "/data/adb/modules/inx_mods"
#endif

// [NEW] "INX_STANDALONE_DIR" lets the exact same optimum_bar binary run
// with no module installed at all: the standalone CLI (standalone_cli.cpp)
// sets this env var to its own directory before exec'ing optimum_bar, and
// everything that used to be hardcoded to INX_MODULE_DIR reads from there
// instead for that one process. Unset (the normal module launch path from
// the daemon), nothing changes — same compiled-in path as before. Checked
// once and cached: an env var read every frame would be wasteful, and
// nothing legitimately changes it mid-process.
inline const std::string& ix_controls_base_dir() {
    static const std::string base = []() -> std::string {
        const char* v = getenv("INX_STANDALONE_DIR");
        return (v && v[0] != '\0') ? std::string(v) : std::string(INX_MODULE_DIR);
    }();
    return base;
}
inline std::string ix_controls_path() {
    return ix_controls_base_dir() + "/webroot/controls.json";
}

// Every control the menu has, and nothing else. Defaults match the binary's.
struct IxControls {
    // ESP
    bool esp_box = false, esp_skeleton = false, esp_name = false;
    bool esp_health = false, esp_distance = false, esp_line = false, esp_360 = false;
    int  esp_textsize = 22;
    // Aim
    bool  aim_silent = false, aim_hwbt = false, aim_fovcircle = false;
    int   aim_mode = 1;              // 1 head only, 2 body, 3 legit
    float aim_fov = 180.0f;
    // Exploit
    bool  exp_pull = false, exp_ghost = false, exp_norecoil = false;
    bool  exp_noreload = false, exp_floatpull = false, exp_floatghost = false;
    float exp_pullrange = 6.0f;
};

inline nlohmann::json ix_controls_to_json(const IxControls& c) {
    nlohmann::json j;
    j["esp_box"] = c.esp_box;             j["esp_skeleton"] = c.esp_skeleton;
    j["esp_name"] = c.esp_name;           j["esp_health"] = c.esp_health;
    j["esp_distance"] = c.esp_distance;   j["esp_line"] = c.esp_line;
    j["esp_360"] = c.esp_360;             j["esp_textsize"] = c.esp_textsize;
    j["aim_silent"] = c.aim_silent;       j["aim_hwbt"] = c.aim_hwbt;
    j["aim_fovcircle"] = c.aim_fovcircle; j["aim_mode"] = c.aim_mode;
    j["aim_fov"] = c.aim_fov;
    j["exp_pull"] = c.exp_pull;           j["exp_ghost"] = c.exp_ghost;
    j["exp_norecoil"] = c.exp_norecoil;   j["exp_noreload"] = c.exp_noreload;
    j["exp_floatpull"] = c.exp_floatpull; j["exp_floatghost"] = c.exp_floatghost;
    j["exp_pullrange"] = c.exp_pullrange;
    return j;
}

inline void ix_controls_from_json(const nlohmann::json& j, IxControls& c) {
    c.esp_box       = j.value("esp_box", c.esp_box);
    c.esp_skeleton  = j.value("esp_skeleton", c.esp_skeleton);
    c.esp_name      = j.value("esp_name", c.esp_name);
    c.esp_health    = j.value("esp_health", c.esp_health);
    c.esp_distance  = j.value("esp_distance", c.esp_distance);
    c.esp_line      = j.value("esp_line", c.esp_line);
    c.esp_360       = j.value("esp_360", c.esp_360);
    c.esp_textsize  = j.value("esp_textsize", c.esp_textsize);
    c.aim_silent    = j.value("aim_silent", c.aim_silent);
    c.aim_hwbt      = j.value("aim_hwbt", c.aim_hwbt);
    c.aim_fovcircle = j.value("aim_fovcircle", c.aim_fovcircle);
    c.aim_mode      = j.value("aim_mode", c.aim_mode);
    c.aim_fov       = j.value("aim_fov", c.aim_fov);
    c.exp_pull      = j.value("exp_pull", c.exp_pull);
    c.exp_ghost     = j.value("exp_ghost", c.exp_ghost);
    c.exp_norecoil  = j.value("exp_norecoil", c.exp_norecoil);
    c.exp_noreload  = j.value("exp_noreload", c.exp_noreload);
    c.exp_floatpull = j.value("exp_floatpull", c.exp_floatpull);
    c.exp_floatghost= j.value("exp_floatghost", c.exp_floatghost);
    c.exp_pullrange = j.value("exp_pullrange", c.exp_pullrange);
}

inline bool ix_controls_load(IxControls& c) {
    std::ifstream f(ix_controls_path());
    if (!f.good()) return false;
    try {
        nlohmann::json j; f >> j;
        if (!j.is_object()) return false;
        ix_controls_from_json(j, c);
        return true;
    } catch (...) { return false; }
}

// Written to a temp file and renamed, so a reader never sees half a file.
inline bool ix_controls_save(const IxControls& c) {
    const std::string path = ix_controls_path();
    const std::string tmp = path + ".tmp";
    {
        std::ofstream o(tmp, std::ios::trunc);
        if (!o.good()) return false;
        o << ix_controls_to_json(c).dump(4);
        if (!o.good()) return false;
    }
    chmod(tmp.c_str(), 0644);
    return rename(tmp.c_str(), path.c_str()) == 0;
}

// True when the file changed since the last call, so the overlay can skip the
// parse on the frames where nothing moved.
inline bool ix_controls_changed() {
    static time_t last = 0;
    static off_t  lastSize = 0;
    struct stat st;
    if (stat(ix_controls_path().c_str(), &st) != 0) return false;
    if (st.st_mtime == last && st.st_size == lastSize) return false;
    last = st.st_mtime;
    lastSize = st.st_size;
    return true;
}
