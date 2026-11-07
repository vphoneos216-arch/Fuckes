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
//   POST /api/login  key=<licence key>   -> sets the session cookie
//   POST /api/logout                      -> drops it
//
// Safety — TWO independent gates, both required
//   Gate 1, the link token: every request must carry ?t=<token>, generated
//     fresh at startup from the kernel CSPRNG (32 bytes). Possession of the
//     link is what gets you as far as the login screen.
//   Gate 2, the licence key: /api/state, /api/set and /snd/* additionally
//     require a session, and a session only exists after POST /api/login
//     presents the SAME licence key the module already verified with the
//     server. So reaching the port, or even stealing the link, is not
//     enough to drive the menu or read licence details.
//
//   * bound to 127.0.0.1 only, so nothing off the device can reach it
//   * both secrets are compared in constant time, and neither an empty
//     token nor an empty key can ever match
//   * login is rate limited, so the key cannot be brute forced locally
//   * the Host header must be loopback, which stops a DNS-rebinding page
//     from talking to this server from its own origin
//   * state-changing requests must be POST, and the session cookie is
//     SameSite=Strict + HttpOnly, so a page in the device browser cannot
//     drive the menu with an <img> tag and cannot read the cookie
//   * the control list is built from the menu's own variables, so the page
//     can never set something the menu does not already have, and every
//     value is range-clamped and rejected if it is not finite
//
// Threading
//   The server runs on its own thread and never writes a control directly.
//   It queues the change, and ix_remote_apply() — called once per frame from
//   the render thread — applies it. That keeps every write on the thread
//   that owns these variables.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/random.h>
#include <unistd.h>
#include <fcntl.h>

#include <atomic>
#include <cmath>
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

// NOTE: every POSIX socket call below is written as ::name. main.cpp has
// `using namespace std;` in scope, so an unqualified bind() resolves to
// std::bind and the build fails; the same trap applies to the others.

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

// ── session / login state ───────────────────────────────────
// A session is created only by POST /api/login presenting the licence key
// the module already verified against the server. The daemon hands us that
// key with ix_remote_set_key() every poll, so it tracks config.json.
struct Session { std::string id; time_t expires; };
inline std::vector<Session> g_sessions;
inline std::mutex           g_sessMtx;

inline std::string g_expectKey;          // verified licence key, or empty
inline std::mutex  g_keyMtx;

inline int    g_loginFails = 0;          // brute-force brake, under g_sessMtx
inline time_t g_lockUntil  = 0;

inline std::atomic<int> g_liveConns{0};  // connection cap, see serve()

static const int IX_SESSION_TTL   = 12 * 60 * 60;  // 12 h
static const int IX_MAX_SESSIONS  = 8;             // phone + a spare tab or two
static const int IX_MAX_FAILS     = 5;
static const int IX_LOCK_SECS     = 60;
static const int IX_MAX_CONNS     = 12;
static const size_t IX_MAX_REQ    = 8192;          // headers + body ceiling

// ── randomness ──────────────────────────────────────────────
// The old token was an LCG seeded with time(nullptr) ^ getpid(). Both halves
// are readable by any app on the device (the pid straight out of /proc, the
// start time to within a second or two), and the LCG is fully determined by
// that seed - so the "secret" token was computable by anything running
// locally, which is precisely the threat it was there to stop. Everything
// secret now comes from the kernel CSPRNG instead.
inline bool rand_bytes(unsigned char* out, size_t n) {
    size_t got = 0;
    while (got < n) {                      // getrandom(2): API 28+, our floor
        const ssize_t r = ::getrandom(out + got, n - got, 0);
        if (r <= 0) break;
        got += (size_t)r;
    }
    if (got == n) return true;

    const int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    got = 0;
    while (got < n) {
        const ssize_t r = ::read(fd, out + got, n - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    ::close(fd);
    return got == n;
}

inline std::string hex_of(const unsigned char* p, size_t n) {
    static const char* hex = "0123456789abcdef";
    std::string o;
    o.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) { o += hex[p[i] >> 4]; o += hex[p[i] & 0xF]; }
    return o;
}

// Returns "" if the kernel would not give us randomness. Every caller treats
// that as fatal rather than falling back to something guessable.
inline std::string make_secret(size_t bytes = 32) {
    unsigned char b[64];
    if (bytes > sizeof(b)) bytes = sizeof(b);
    if (!rand_bytes(b, bytes)) return std::string();
    const std::string out = hex_of(b, bytes);
    memset(b, 0, sizeof(b));
    return out;
}

inline std::string make_token() { return make_secret(32); }

// Length is not secret; the contents are. An empty string never matches, so
// a server that failed to generate a token cannot be satisfied by simply
// omitting the parameter.
inline bool ct_eq(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty() || a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i)
        diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
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
    // Built by appending, never through a fixed-size snprintf: a 512-byte
    // buffer silently truncates mid-string and what reaches the page is
    // then not JSON at all. Every string is escaped for the same reason -
    // one quote anywhere would end the document early.
    std::string j = "{\"controls\":[";
    for (size_t i = 0; i < g_ctls.size(); ++i) {
        const IxCtl& c = g_ctls[i];
        if (i) j += ',';
        char buf[256];
        const char* tname = (c.type == IXC_BOOL)  ? "bool"
                          : (c.type == IXC_ENUM)  ? "enum"
                          : (c.type == IXC_INT)   ? "int" : "float";
        j += "{\"id\":\"";    j += json_escape(c.id ? c.id : "");
        j += "\",\"page\":\"";  j += json_escape(c.page ? c.page : "");
        j += "\",\"label\":\""; j += json_escape(c.label ? c.label : "");
        j += "\",\"type\":\"";  j += tname;
        j += "\",";
        if (c.type == IXC_BOOL) {
            j += std::string("\"value\":") + (read_ctl(c) > 0.5f ? "true" : "false");
        } else {
            snprintf(buf, sizeof(buf), "\"value\":%.3f,\"min\":%.3f,\"max\":%.3f,\"step\":%.3f",
                     read_ctl(c), c.lo, c.hi, c.step);
            j += buf;
        }
        if (c.unit) { j += ",\"unit\":\""; j += json_escape(c.unit); j += "\""; }
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

// ── sessions ────────────────────────────────────────────────
inline void prune_sessions_locked(time_t now) {
    for (size_t i = 0; i < g_sessions.size(); ) {
        if (g_sessions[i].expires <= now) g_sessions.erase(g_sessions.begin() + i);
        else ++i;
    }
}

inline bool session_valid(const std::string& sid) {
    if (sid.empty()) return false;
    const time_t now = time(nullptr);
    std::lock_guard<std::mutex> lk(g_sessMtx);
    prune_sessions_locked(now);
    for (const Session& s : g_sessions)
        if (ct_eq(s.id, sid)) return true;
    return false;
}

// "" on failure. Callers distinguish lock-out from a wrong key via the
// locked flag, so the page can say which it was without leaking anything
// about the key itself.
inline std::string session_create(const std::string& key, bool& lockedOut) {
    lockedOut = false;
    const time_t now = time(nullptr);

    std::string expect;
    { std::lock_guard<std::mutex> lk(g_keyMtx); expect = g_expectKey; }

    std::lock_guard<std::mutex> lk(g_sessMtx);
    if (now < g_lockUntil) { lockedOut = true; return std::string(); }

    // No verified key on file means nobody is logged into the module yet,
    // so there is nothing to log in against - fail closed.
    if (expect.empty() || !ct_eq(key, expect)) {
        if (++g_loginFails >= IX_MAX_FAILS) {
            g_loginFails = 0;
            g_lockUntil  = now + IX_LOCK_SECS;
        }
        return std::string();
    }

    g_loginFails = 0;
    const std::string sid = make_secret(32);
    if (sid.empty()) return std::string();      // no randomness, no session

    prune_sessions_locked(now);
    if (g_sessions.size() >= (size_t)IX_MAX_SESSIONS)
        g_sessions.erase(g_sessions.begin());   // oldest out
    g_sessions.push_back({sid, now + IX_SESSION_TTL});
    return sid;
}

inline void session_drop(const std::string& sid) {
    if (sid.empty()) return;
    std::lock_guard<std::mutex> lk(g_sessMtx);
    for (size_t i = 0; i < g_sessions.size(); ++i) {
        if (ct_eq(g_sessions[i].id, sid)) { g_sessions.erase(g_sessions.begin() + i); return; }
    }
}

// ── request parsing ─────────────────────────────────────────
// Case-insensitive single-header lookup over the raw request text.
inline std::string header_value(const std::string& req, const char* name) {
    const std::string lower = [&]{
        std::string l = req;
        for (char& c : l) c = (char)tolower((unsigned char)c);
        return l;
    }();
    std::string want = std::string("\r\n") + name + ":";
    for (char& c : want) c = (char)tolower((unsigned char)c);

    size_t p = lower.find(want);
    if (p == std::string::npos) return "";
    p += want.size();
    const size_t e = req.find("\r\n", p);
    std::string v = req.substr(p, e == std::string::npos ? std::string::npos : e - p);
    size_t b = v.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    size_t f = v.find_last_not_of(" \t");
    return v.substr(b, f - b + 1);
}

// Pulls one cookie out of the Cookie header.
inline std::string cookie_value(const std::string& req, const char* name) {
    const std::string ck = header_value(req, "cookie");
    if (ck.empty()) return "";
    const std::string key = std::string(name) + "=";
    size_t p = 0;
    while ((p = ck.find(key, p)) != std::string::npos) {
        const bool atStart = (p == 0);
        const bool afterSep = !atStart && (ck[p - 1] == ' ' || ck[p - 1] == ';');
        if (atStart || afterSep) {
            const size_t v = p + key.size();
            const size_t e = ck.find(';', v);
            return ck.substr(v, e == std::string::npos ? std::string::npos : e - v);
        }
        p += key.size();
    }
    return "";
}

// A page served from an attacker's domain that resolves to 127.0.0.1 would
// otherwise be same-origin with this server as far as the browser cares
// (DNS rebinding). Requiring a loopback Host closes that off.
inline bool host_is_loopback(const std::string& req) {
    const std::string h = header_value(req, "host");
    if (h.empty()) return false;
    char want1[64], want2[64];
    snprintf(want1, sizeof(want1), "127.0.0.1:%d", IX_REMOTE_PORT);
    snprintf(want2, sizeof(want2), "localhost:%d", IX_REMOTE_PORT);
    return h == want1 || h == want2;
}

inline std::string form_field(const std::string& body, const char* key) {
    std::string k = std::string(key) + "=";
    size_t p = 0;
    bool first = true;
    while (p <= body.size()) {
        size_t at = body.find(k, p);
        if (at == std::string::npos) return "";
        if (at == 0 || body[at - 1] == '&') { first = false; p = at + k.size(); break; }
        p = at + 1;
    }
    if (first) return "";
    const size_t e = body.find('&', p);
    std::string raw = body.substr(p, e == std::string::npos ? std::string::npos : e - p);
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
        const ssize_t w = ::send(fd, p, n, MSG_NOSIGNAL);
        if (w <= 0) return;
        p += w; n -= (size_t)w;
    }
}

// extraHdr, when given, must already end in CRLF (used for Set-Cookie).
//
// The three added headers are not decoration: nosniff stops a browser
// treating a JSON body as script, the CSP keeps the page from loading or
// contacting anything off this server even if markup were ever injected
// into it, and no-referrer stops the ?t=<token> in the URL being handed
// to any other site the page might ever link to.
inline void respond(int fd, const char* status, const char* ctype,
                    const char* body, size_t len,
                    const char* extraHdr = nullptr) {
    char head[640];
    const int hn = snprintf(head, sizeof(head),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Referrer-Policy: no-referrer\r\n"
        "Content-Security-Policy: default-src 'none'; img-src 'self' data:; "
        "media-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; "
        "connect-src 'self'; form-action 'none'; base-uri 'none'\r\n"
        "Connection: close\r\n"
        "%s\r\n",
        status, ctype, len, extraHdr ? extraHdr : "");
    if (hn > 0) send_all(fd, head, (size_t)hn);
    if (len) send_all(fd, body, len);
}

inline void respond_json(int fd, const char* status, const std::string& j,
                         const char* extraHdr = nullptr) {
    respond(fd, status, "application/json", j.data(), j.size(), extraHdr);
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

// Reads the whole request: headers, then exactly Content-Length bytes of
// body, both under IX_MAX_REQ. The old single recv() of 2048 could return a
// partial request on any split packet, which with a POST body means losing
// the key the user just typed.
inline bool read_request(int fd, std::string& req, std::string& body) {
    char buf[2048];
    req.clear(); body.clear();

    size_t hdrEnd = std::string::npos;
    while (req.size() < IX_MAX_REQ) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.append(buf, (size_t)n);
        hdrEnd = req.find("\r\n\r\n");
        if (hdrEnd != std::string::npos) break;
    }
    if (hdrEnd == std::string::npos) return false;

    const size_t bodyAt = hdrEnd + 4;
    body = req.substr(bodyAt);
    req.resize(bodyAt);

    size_t want = 0;
    const std::string cl = header_value(req, "content-length");
    if (!cl.empty()) {
        const long v = strtol(cl.c_str(), nullptr, 10);
        if (v > 0) want = (size_t)v;
        if (want > IX_MAX_REQ) return false;
    }
    while (body.size() < want) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        body.append(buf, (size_t)n);
        if (body.size() > IX_MAX_REQ) return false;
    }
    if (body.size() > want) body.resize(want);
    return true;
}

inline void handle(int fd) {
    std::string req, body;
    if (!read_request(fd, req, body)) { ::close(fd); return; }

    // request line: METHOD PATH HTTP/1.1
    const size_t sp1 = req.find(' ');
    if (sp1 == std::string::npos) { ::close(fd); return; }
    const size_t sp2 = req.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) { ::close(fd); return; }
    const std::string method = req.substr(0, sp1);
    const std::string path   = req.substr(sp1 + 1, sp2 - sp1 - 1);

    static const char* kDeny = "{\"ok\":false}";

    // DNS-rebinding guard, before anything else looks at the request.
    if (!host_is_loopback(req)) {
        respond_json(fd, "403 Forbidden", kDeny);
        ::close(fd);
        return;
    }

    // ── Gate 1: the link token ──────────────────────────────
    if (!ct_eq(query_param(path, "t"), g_token)) {
        respond_json(fd, "403 Forbidden", kDeny);
        ::close(fd);
        return;
    }

    const std::string bare = path.substr(0, path.find('?'));
    const std::string sid  = cookie_value(req, "ix_sess");
    const bool        authed = session_valid(sid);

    // ── login / logout ──────────────────────────────────────
    if (bare == "/api/login") {
        if (method != "POST") {
            respond_json(fd, "405 Method Not Allowed", kDeny);
            ::close(fd); return;
        }
        const std::string key = form_field(body, "key");
        bool locked = false;
        const std::string newSid = session_create(key, locked);
        if (newSid.empty()) {
            // Deliberately says nothing about the key itself beyond
            // right/wrong, and the same for a locked-out attempt.
            const std::string j = locked
                ? std::string("{\"ok\":false,\"locked\":true}")
                : std::string("{\"ok\":false}");
            respond_json(fd, locked ? "429 Too Many Requests" : "401 Unauthorized", j);
            ::close(fd); return;
        }
        // HttpOnly: script cannot read it, so an injected script cannot
        // exfiltrate the session. SameSite=Strict: another site cannot make
        // the browser send it. Max-Age matches the server-side expiry.
        char ck[256];
        snprintf(ck, sizeof(ck),
                 "Set-Cookie: ix_sess=%s; Path=/; Max-Age=%d; HttpOnly; SameSite=Strict\r\n",
                 newSid.c_str(), IX_SESSION_TTL);
        respond_json(fd, "200 OK", "{\"ok\":true}", ck);
        ::close(fd); return;
    }

    if (bare == "/api/logout") {
        if (method != "POST") {
            respond_json(fd, "405 Method Not Allowed", kDeny);
            ::close(fd); return;
        }
        session_drop(sid);
        respond_json(fd, "200 OK", "{\"ok\":true}",
                     "Set-Cookie: ix_sess=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict\r\n");
        ::close(fd); return;
    }

    // The page itself is public behind the token - it IS the login screen.
    // It only ever renders controls after /api/state succeeds.
    if (bare == "/" || bare == "/index.html") {
        if (method != "GET") {
            respond_json(fd, "405 Method Not Allowed", kDeny);
            ::close(fd); return;
        }
        respond(fd, "200 OK", "text/html; charset=utf-8",
                IX_REMOTE_PAGE, strlen(IX_REMOTE_PAGE));
        ::close(fd); return;
    }

    // Tells the page whether to show the login screen or the controls,
    // without handing out anything either way.
    if (bare == "/api/session") {
        bool haveKey;
        { std::lock_guard<std::mutex> lk(g_keyMtx); haveKey = !g_expectKey.empty(); }
        std::string j = std::string("{\"ok\":true,\"authed\":") + (authed ? "true" : "false") +
                        ",\"module_logged_in\":" + (haveKey ? "true" : "false") + "}";
        respond_json(fd, "200 OK", j);
        ::close(fd); return;
    }

    // ── Gate 2: everything below needs the licence-key session ──
    if (!authed) {
        respond_json(fd, "401 Unauthorized", "{\"ok\":false,\"login\":true}");
        ::close(fd); return;
    }

    if (bare == "/api/state") {
        if (method != "GET") {
            respond_json(fd, "405 Method Not Allowed", kDeny);
            ::close(fd); return;
        }
        respond_json(fd, "200 OK", state_json());
    } else if (bare == "/api/set") {
        // POST only. As a GET this was a CSRF primitive: any page in the
        // device's browser could flip a control with an <img src=...> if it
        // ever learned the token. A cross-site POST cannot carry the
        // SameSite=Strict cookie, so it fails Gate 2 instead.
        if (method != "POST") {
            respond_json(fd, "405 Method Not Allowed", kDeny);
            ::close(fd); return;
        }
        const std::string id = form_field(body, "id");
        const std::string v  = form_field(body, "v");
        int found = -1;
        for (size_t i = 0; i < g_ctls.size(); ++i)
            if (id == g_ctls[i].id) { found = (int)i; break; }

        // strtof + isfinite, not atof. "nan"/"inf" parse happily, and a NaN
        // passes BOTH clamp comparisons in ix_remote_apply() (every compare
        // against NaN is false), so it used to land straight in a live
        // control - a NaN FOV or pull range corrupts the maths downstream,
        // and (int)(NaN + 0.5f) is undefined outright.
        char*  endp  = nullptr;
        const float parsed = v.empty() ? 0.0f : strtof(v.c_str(), &endp);
        const bool  okNum  = !v.empty() && endp && *endp == '\0' && std::isfinite(parsed);

        if (found < 0 || !okNum) {
            respond_json(fd, "400 Bad Request", kDeny);
        } else {
            {
                std::lock_guard<std::mutex> lk(g_pendMtx);
                g_pending.push_back({found, parsed});
            }
            respond_json(fd, "200 OK", "{\"ok\":true}");
        }
    } else if (bare.rfind("/snd/", 0) == 0) {
        if (method != "GET") {
            respond_json(fd, "405 Method Not Allowed", kDeny);
            ::close(fd); return;
        }
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
    ::close(fd);
}

// [SMOOTHNESS/STABILITY] The loop used to call handle() inline, so exactly
// one request was in flight at a time and each socket carried a 3s receive
// timeout. A browser opens several connections per page and fires state
// polls alongside them, so the page felt laggy under normal use - and one
// connection that opened and then said nothing stalled EVERY other request
// behind it for the full 3 seconds. Each connection now gets its own
// short-lived thread, capped at IX_MAX_CONNS so a flood cannot spawn
// threads without bound; past the cap the newest connection is dropped
// immediately rather than queueing behind the others.
inline void serve() {
    while (g_running.load(std::memory_order_acquire)) {
        sockaddr_in cli{};
        socklen_t cl = sizeof(cli);
        const int fd = ::accept(g_listenFd, (sockaddr*)&cli, &cl);
        if (fd < 0) {
            if (!g_running.load(std::memory_order_acquire)) break;
            continue;
        }
        if (!g_running.load(std::memory_order_acquire)) { ::close(fd); break; }

        // Loopback only, but belt and braces: anything that is not
        // 127.0.0.1 has no business here.
        if (cli.sin_addr.s_addr != ::htonl(INADDR_LOOPBACK)) { ::close(fd); continue; }

        timeval tv{}; tv.tv_sec = 3;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (g_liveConns.load(std::memory_order_acquire) >= IX_MAX_CONNS) {
            ::close(fd);
            continue;
        }
        g_liveConns.fetch_add(1, std::memory_order_acq_rel);
        std::thread([fd]{
            handle(fd);                      // closes fd on every path
            g_liveConns.fetch_sub(1, std::memory_order_acq_rel);
        }).detach();
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

// The licence key the module already verified with the server. The daemon
// calls this every poll from config.json's "key", so logging out of the
// module (which clears/invalidates it) also stops anyone logging in here.
// Passing "" means "nobody is logged into the module", and that fails every
// remote login closed.
inline void ix_remote_set_key(const std::string& key) {
    using namespace ix_remote;
    std::lock_guard<std::mutex> lk(g_keyMtx);
    if (g_expectKey == key) return;
    g_expectKey = key;
    // The key changed (new licence, or the module logged out), so every
    // session minted against the old one has to go.
    std::lock_guard<std::mutex> sk(g_sessMtx);
    g_sessions.clear();
    g_loginFails = 0;
    g_lockUntil  = 0;
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

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = ::htons(IX_REMOTE_PORT);
    addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);   // 127.0.0.1 only
    if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0) { ::close(fd); return false; }
    if (::listen(fd, 8) != 0) { ::close(fd); return false; }

    // No token, no server. ct_eq() refuses an empty secret, so starting
    // without one would answer 403 to everything anyway - but failing here
    // makes that explicit instead of looking like a broken page, and means
    // there is never a listener up with no usable gate.
    const std::string tok = make_token();
    if (tok.empty()) { ::close(fd); return false; }

    g_listenFd = fd;
    g_token    = tok;
    g_running.store(true, std::memory_order_release);
    g_enabled.store(true, std::memory_order_release);
    std::thread(serve).detach();
    return true;
}

// Turning "Remote Control" off in the module used to leave this server
// listening for the life of the daemon, still answering to the same token.
// Now it actually stops: the listener is closed (which wakes the accept
// loop), every session is dropped, and the token is cleared so even a
// saved link is dead. Starting again mints a brand-new token and link.
inline void ix_remote_stop() {
    using namespace ix_remote;
    // exchange() both reads and clears, so a second call is a no-op.
    if (!g_running.exchange(false)) { g_enabled.store(false, std::memory_order_release); return; }
    g_enabled.store(false, std::memory_order_release);

    if (g_listenFd >= 0) {
        ::shutdown(g_listenFd, SHUT_RDWR);
        ::close(g_listenFd);
        g_listenFd = -1;
    }
    {
        std::lock_guard<std::mutex> lk(g_sessMtx);
        g_sessions.clear();
        g_loginFails = 0;
        g_lockUntil  = 0;
    }
    g_token.clear();
}
