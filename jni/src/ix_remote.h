#pragma once
// ── Remote control ──────────────────────────────────────────
// A small HTTP server inside the binary. When the module's "Remote Control"
// switch is on, the in-game menu is not drawn and every control it has is
// driven from a page served on 127.0.0.1 instead.
//
// What it exposes, and nothing else:
//   GET /            the page (ix_remote_page.h)
//   GET /api/state   every control with its current value, plus licence info
//   GET /api/set     id=<control>&v=<value>
//   GET /snd/<n>.wav the same three clips the menu plays
//
// Safety
//   * bound to 127.0.0.1 only, so nothing off the device can reach it
//   * every request must carry a token generated fresh at startup; without
//     it the server answers 403, so another app on the phone cannot drive
//     the menu just by knowing the port
//   * the control list is built from the menu's own variables, so the page
//     can never set something the menu does not already have
//
// Threading
//   The server runs on its own thread and never writes a control directly.
//   It queues the change, and ix_remote_apply() — called once per frame from
//   the render thread — applies it. That keeps every write on the thread
//   that owns these variables.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "remote/ix_remote_page.h"
#include "UI/sounds/ix_sound_data.h"

#define IX_REMOTE_PORT 8787

// ── control table ───────────────────────────────────────────
// Filled by ix_remote_register() in main.cpp from the menu's own variables.
enum IxCtlType { IXC_BOOL, IXC_INT, IXC_FLOAT, IXC_ENUM };

struct IxCtl {
    const char* id;
    const char* page;
    const char* label;
    const char* note;      // optional second line, may be null
    IxCtlType   type;
    void*       ptr;
    float       lo, hi, step;
    const char* unit;      // optional suffix, may be null
};

struct IxInfoRow { std::string label, value; };

namespace ix_remote {

inline std::vector<IxCtl>      g_ctls;
inline std::vector<IxInfoRow>  g_info;
inline std::mutex              g_infoMtx;

// Pending writes from the server thread, applied on the render thread.
struct Pending { int index; float value; };
inline std::vector<Pending> g_pending;
inline std::mutex           g_pendMtx;

inline std::atomic<bool> g_running{false};
inline std::atomic<bool> g_enabled{false};
inline std::string       g_token;
inline int               g_listenFd = -1;

inline std::string make_token() {
    static const char* hex = "0123456789abcdef";
    unsigned seed = (unsigned)time(nullptr) ^ (unsigned)getpid();
    std::string t;
    t.reserve(24);
    for (int i = 0; i < 24; ++i) {
        seed = seed * 1103515245u + 12345u;
        t += hex[(seed >> 16) & 0xF];
    }
    return t;
}

inline std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
                else o += c;
        }
    }
    return o;
}

inline float read_ctl(const IxCtl& c) {
    switch (c.type) {
        case IXC_BOOL:  return *(bool*)c.ptr ? 1.0f : 0.0f;
        case IXC_INT:
        case IXC_ENUM:  return (float)*(int*)c.ptr;
        default:        return *(float*)c.ptr;
    }
}

inline std::string state_json() {
    std::string j = "{\"controls\":[";
    for (size_t i = 0; i < g_ctls.size(); ++i) {
        const IxCtl& c = g_ctls[i];
        if (i) j += ',';
        char buf[512];
        const char* tname = (c.type == IXC_BOOL)  ? "bool"
                          : (c.type == IXC_ENUM)  ? "enum"
                          : (c.type == IXC_INT)   ? "int" : "float";
        snprintf(buf, sizeof(buf),
                 "{\"id\":\"%s\",\"page\":\"%s\",\"label\":\"%s\",\"type\":\"%s\",",
                 c.id, c.page, c.label, tname);
        j += buf;
        if (c.type == IXC_BOOL) {
            j += std::string("\"value\":") + (read_ctl(c) > 0.5f ? "true" : "false");
        } else {
            snprintf(buf, sizeof(buf), "\"value\":%.3f,\"min\":%.3f,\"max\":%.3f,\"step\":%.3f",
                     read_ctl(c), c.lo, c.hi, c.step);
            j += buf;
        }
        if (c.unit) { j += ",\"unit\":\""; j += c.unit; j += "\""; }
        if (c.note) { j += ",\"note\":\""; j += json_escape(c.note); j += "\""; }
        if (c.type == IXC_ENUM) {
            // The only enum is Hardware BT Mode, whose values the menu fixes
            // at 1 head only / 2 body / 3 legit.
            j += ",\"options\":[{\"label\":\"Head only\",\"value\":1},"
                 "{\"label\":\"Body\",\"value\":2},{\"label\":\"Legit\",\"value\":3}]";
        }
        j += '}';
    }
    j += "],\"info\":[";
    {
        std::lock_guard<std::mutex> lk(g_infoMtx);
        for (size_t i = 0; i < g_info.size(); ++i) {
            if (i) j += ',';
            j += "{\"label\":\"" + json_escape(g_info[i].label) +
                 "\",\"value\":\"" + json_escape(g_info[i].value) + "\"}";
        }
    }
    j += "]}";
    return j;
}

inline std::string query_param(const std::string& path, const char* key) {
    const size_t q = path.find('?');
    if (q == std::string::npos) return "";
    std::string k = std::string(key) + "=";
    size_t p = path.find(k, q);
    while (p != std::string::npos) {
        const char prev = path[p - 1];
        if (prev == '?' || prev == '&') break;
        p = path.find(k, p + 1);
    }
    if (p == std::string::npos) return "";
    p += k.size();
    size_t e = path.find('&', p);
    std::string raw = path.substr(p, e == std::string::npos ? std::string::npos : e - p);
    // percent-decode
    std::string out;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '%' && i + 2 < raw.size()) {
            out += (char)strtol(raw.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else if (raw[i] == '+') out += ' ';
        else out += raw[i];
    }
    return out;
}

inline void send_all(int fd, const char* p, size_t n) {
    while (n) {
        const ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
        if (w <= 0) return;
        p += w; n -= (size_t)w;
    }
}

inline void respond(int fd, const char* status, const char* ctype,
                    const char* body, size_t len) {
    char head[256];
    const int hn = snprintf(head, sizeof(head),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n", status, ctype, len);
    send_all(fd, head, (size_t)hn);
    if (len) send_all(fd, body, len);
}

// 44-byte RIFF header in front of the embedded PCM, so a browser can play it.
inline std::string wav_of(const int16_t* pcm, int32_t frames) {
    const int32_t dataBytes = frames * 2;
    const int32_t riff = 36 + dataBytes;
    const int32_t rate = IX_SND_RATE;
    const int16_t ch = IX_SND_CHANNELS, bits = 16;
    const int32_t byteRate = rate * ch * bits / 8;
    const int16_t blockAlign = (int16_t)(ch * bits / 8);
    const int32_t fmtLen = 16; const int16_t fmtTag = 1;

    std::string h;
    auto put32 = [&](int32_t v){ h.append((const char*)&v, 4); };
    auto put16 = [&](int16_t v){ h.append((const char*)&v, 2); };
    h += "RIFF"; put32(riff); h += "WAVE";
    h += "fmt "; put32(fmtLen); put16(fmtTag); put16((int16_t)ch);
    put32(rate); put32(byteRate); put16(blockAlign); put16(bits);
    h += "data"; put32(dataBytes);
    h.append((const char*)pcm, (size_t)dataBytes);
    return h;
}

inline void handle(int fd) {
    char req[2048];
    const ssize_t n = recv(fd, req, sizeof(req) - 1, 0);
    if (n <= 0) { close(fd); return; }
    req[n] = '\0';

    // request line: METHOD PATH HTTP/1.1
    const char* sp1 = strchr(req, ' ');
    if (!sp1) { close(fd); return; }
    const char* sp2 = strchr(sp1 + 1, ' ');
    if (!sp2) { close(fd); return; }
    const std::string path(sp1 + 1, sp2);

    if (query_param(path, "t") != g_token) {
        static const char* msg = "forbidden";
        respond(fd, "403 Forbidden", "text/plain", msg, strlen(msg));
        close(fd);
        return;
    }

    const std::string bare = path.substr(0, path.find('?'));

    if (bare == "/" || bare == "/index.html") {
        respond(fd, "200 OK", "text/html; charset=utf-8",
                IX_REMOTE_PAGE, strlen(IX_REMOTE_PAGE));
    } else if (bare == "/api/state") {
        const std::string j = state_json();
        respond(fd, "200 OK", "application/json", j.data(), j.size());
    } else if (bare == "/api/set") {
        const std::string id = query_param(path, "id");
        const std::string v  = query_param(path, "v");
        int found = -1;
        for (size_t i = 0; i < g_ctls.size(); ++i)
            if (id == g_ctls[i].id) { found = (int)i; break; }
        if (found < 0 || v.empty()) {
            static const char* bad = "{\"ok\":false}";
            respond(fd, "404 Not Found", "application/json", bad, strlen(bad));
        } else {
            {
                std::lock_guard<std::mutex> lk(g_pendMtx);
                g_pending.push_back({found, (float)atof(v.c_str())});
            }
            static const char* ok = "{\"ok\":true}";
            respond(fd, "200 OK", "application/json", ok, strlen(ok));
        }
    } else if (bare.rfind("/snd/", 0) == 0) {
        const int16_t* pcm = nullptr; int32_t len = 0;
        if (bare == "/snd/on.wav")           { pcm = ix_snd_on;      len = ix_snd_on_len; }
        else if (bare == "/snd/off.wav")     { pcm = ix_snd_off;     len = ix_snd_off_len; }
        else if (bare == "/snd/welcome.wav") { pcm = ix_snd_welcome; len = ix_snd_welcome_len; }
        if (pcm) {
            const std::string w = wav_of(pcm, len);
            respond(fd, "200 OK", "audio/wav", w.data(), w.size());
        } else {
            respond(fd, "404 Not Found", "text/plain", "", 0);
        }
    } else {
        respond(fd, "404 Not Found", "text/plain", "", 0);
    }
    close(fd);
}

inline void serve() {
    while (g_running.load(std::memory_order_acquire)) {
        sockaddr_in cli{};
        socklen_t cl = sizeof(cli);
        const int fd = accept(g_listenFd, (sockaddr*)&cli, &cl);
        if (fd < 0) {
            if (!g_running.load(std::memory_order_acquire)) break;
            continue;
        }
        timeval tv{}; tv.tv_sec = 3;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        handle(fd);
    }
}

} // namespace ix_remote

// Register one control. Called from main.cpp, where the variables live.
inline void ix_remote_add(const char* id, const char* page, const char* label,
                          IxCtlType type, void* ptr,
                          float lo = 0, float hi = 1, float step = 1,
                          const char* unit = nullptr, const char* note = nullptr) {
    ix_remote::g_ctls.push_back({id, page, label, note, type, ptr, lo, hi, step, unit});
}

inline void ix_remote_set_info(const std::vector<IxInfoRow>& rows) {
    std::lock_guard<std::mutex> lk(ix_remote::g_infoMtx);
    ix_remote::g_info = rows;
}

inline bool ix_remote_enabled() {
    return ix_remote::g_enabled.load(std::memory_order_acquire);
}

inline const std::string& ix_remote_token() { return ix_remote::g_token; }

// Applies whatever the page changed. Called once per frame on the render
// thread, so the control variables are only ever written from one thread.
inline void ix_remote_apply() {
    using namespace ix_remote;
    std::vector<Pending> todo;
    {
        std::lock_guard<std::mutex> lk(g_pendMtx);
        if (g_pending.empty()) return;
        todo.swap(g_pending);
    }
    for (const Pending& p : todo) {
        if (p.index < 0 || (size_t)p.index >= g_ctls.size()) continue;
        const IxCtl& c = g_ctls[(size_t)p.index];
        float v = p.value;
        if (c.type != IXC_BOOL) {           // clamp to the menu's own range
            if (v < c.lo) v = c.lo;
            if (v > c.hi) v = c.hi;
        }
        switch (c.type) {
            case IXC_BOOL:  *(bool*)c.ptr  = (v > 0.5f); break;
            case IXC_INT:
            case IXC_ENUM:  *(int*)c.ptr   = (int)(v + 0.5f); break;
            default:        *(float*)c.ptr = v; break;
        }
    }
}

// Starts the server if the module asked for it. Safe to call repeatedly.
inline bool ix_remote_start() {
    using namespace ix_remote;
    if (g_running.load(std::memory_order_acquire)) return true;

    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(IX_REMOTE_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // 127.0.0.1 only
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0) { close(fd); return false; }
    if (listen(fd, 8) != 0) { close(fd); return false; }

    g_listenFd = fd;
    g_token = make_token();
    g_running.store(true, std::memory_order_release);
    g_enabled.store(true, std::memory_order_release);
    std::thread(serve).detach();
    return true;
}
