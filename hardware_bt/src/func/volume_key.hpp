#pragma once
// Physical Volume Up key -> menu open/close, used only when the HUD
// style is "Volume" (cfg::hud::counter_style==1). Bar style keeps the
// existing tap-on-the-pill toggle in bar.cpp; Volume style has no tap
// target at all -- this is the only way to open/close the menu in that
// mode, exactly as asked: "Volume select karunga to volume up button se
// menu khulega aur band hoga."
//
// Reuses the same /dev/input/event* enumeration pattern already used
// for touch injection (includes/draw/Android_touch/Touch.cpp), but
// looks for a device that reports EV_KEY/KEY_VOLUMEUP instead of a
// touchscreen's ABS axes. Opened read-only, non-exclusively (no
// EVIOCGRAB) -- this only observes the key, it never steals it from
// the system, so the phone's actual volume still changes normally.
#include "bar.hpp"
#include <atomic>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace volume_key {

inline std::atomic<bool> g_running{false};
inline std::vector<std::thread> g_readers;
inline std::vector<int> g_fds;

inline bool supports_volume_up(int fd) {
    unsigned long bits[(KEY_MAX / (8 * sizeof(unsigned long))) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) return false;
    return (bits[KEY_VOLUMEUP / (8 * sizeof(unsigned long))] >>
            (KEY_VOLUMEUP % (8 * sizeof(unsigned long)))) & 1UL;
}

inline void reader_thread(int fd) {
    auto last_toggle = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    while (g_running.load()) {
        pollfd pfd{fd, POLLIN, 0};
        int r = poll(&pfd, 1, 200);
        if (r <= 0) continue;
        input_event ev{};
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n != (ssize_t)sizeof(ev)) continue;
        // value==1 is key-down; value==2 is key-repeat (held), skipped so
        // holding Volume Up doesn't rapid-fire the toggle.
        if (ev.type == EV_KEY && ev.code == KEY_VOLUMEUP && ev.value == 1) {
            auto now = std::chrono::steady_clock::now();
            if (now - last_toggle > std::chrono::milliseconds(250)) {
                last_toggle = now;
                ui::bar::toggle();
            }
        }
    }
}

inline void start() {
    if (g_running.load()) return;
    g_running.store(true);
    const char* path = "/dev/input/";
    auto dir = opendir(path);
    if (!dir) { g_running.store(false); return; }
    std::vector<std::string> paths;
    dirent* ptr;
    while ((ptr = readdir(dir))) {
        if (std::strncmp(ptr->d_name, "event", 5) == 0) paths.push_back(std::string(path) + ptr->d_name);
    }
    closedir(dir);
    for (const auto& p : paths) {
        int fd = open(p.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        if (!supports_volume_up(fd)) { close(fd); continue; }
        g_fds.push_back(fd);
        g_readers.emplace_back(reader_thread, fd);
    }
}

inline void stop() {
    g_running.store(false);
    for (auto& t : g_readers) if (t.joinable()) t.join();
    g_readers.clear();
    for (int fd : g_fds) close(fd);
    g_fds.clear();
}

} // namespace volume_key
