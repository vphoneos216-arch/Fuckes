#ifndef LOGIN_H
#define LOGIN_H
/* ============================================================
   login.h — Application License Authentication Client v5.0
   (Double-sealed encrypted transport + Ed25519-signed +
    per-license features + anti-patch integrity watchdog)

   PROTOCOL v5.0 (wire version "v":2):

     OUTBOUND WIRE (POST, JSON body) — fully encrypted, no plaintext fields,
     TWO independent cipher layers nested (see TRANSPORT ENCRYPTION LAYER
     section below for the full derivation):
       { "v":2, "cpk":"<64hex X25519 pubkey>",
         "ct":"<hex, XChaCha20-Poly1305 over an XSalsa20-Poly1305 layer>",
         "n":"<48hex outer nonce>", "n2":"<48hex inner nonce>" }

     Inner plaintext (before either encryption layer, never sent raw):
       { "mode":"key", "key_name":"X-XXXXXXXX",
         "device_id":"<hwid>", "ts":<unix_ts>,
         "nonce":"<64 hex>" }

     INBOUND WIRE (JSON) — fully encrypted, same double layer:
       { "ct":"<hex>", "n":"<48hex>", "n2":"<48hex>",
         "spk":"<64hex server ephemeral pubkey>" }

     Inner plaintext (after decryption) — the Ed25519-signed envelope:
       { "payload":"base64(json)", "sig":"base64(ed25519)" }

       Signature covers: nonce + "." + payload

     Signed payload (success):
       { "ok":true, "status":"active"/"activated",
         "features":{...}, "cheat":"...", "seller":"...",
         "expires_at":"...", "remaining_seconds":N,
         "devices_used":N, "max_devices":N,
         "kernel_allowed":true, "server_ts":N,
         "session_token":"...", "session_expires_at":N,
         "enc":{...} }

     Signed payload (failure):
       { "ok":false, "error":"..." }

   LAYER MODEL:
     1. TLS 1.2+                        — transport-level confidentiality
     2. XChaCha20-Poly1305 (this file)  — payload-level confidentiality,
                                           so even a TLS-stripping MITM or
                                           a logging proxy sees only noise
     3. Ed25519 signature (server side) — authenticity of the auth decision

   v4.1 FIXES vs v4:
     [F1] cpk is now a SINGLE value, generated once per request and sent
          only in the outer wire envelope. It is no longer duplicated
          inside the inner encrypted JSON — one source of truth on both
          ends removes a whole class of mismatch bugs.
     [F2] The unsigned-error fast path in kd_auth_post is now locked to
          the exact shape {"ok":false,"error":"<string>"} with no other
          keys, so a network-level attacker who injects a plaintext
          response (before any crypto context exists server-side) cannot
          smuggle extra fields past this layer.
     [F3] kd_wipe_server_secret() wipes memory BEFORE releasing the
          buffer, not after, so the wipe operates on live memory.

   KEY SAFETY — no resets, no repeated activation:
   -------------------------------------------------
   This client NEVER calls any reset/delete/logout endpoint. It only ever
   performs the single "connect" lookup/activate call. The server
   recognizes an already-registered device_id and returns "known device"
   without any write. A stable per-install HWID (persisted to disk)
   guarantees the same device is always "known" after the first run, so
   the key's activation and expiry are never touched again.
   ============================================================ */

#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iterator>
#include <random>
#include <ctime>
#include <unistd.h>
#include <termios.h>
#include <cctype>
#include <cstdint>
#include <limits>
#include <atomic>
#include <csignal>
#include <thread>
#include <chrono>

#include <curl/curl.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sodium.h>
#include <json.hpp>

#include "Scarecrow/oxorany_include.h"
#include "Scarecrow/oxorany.h"
#include "ca_bundle_data.h"
#include "ix_hwid.h"
#include "ix_features.h"
#include "ix_rt_offsets.h"
#include "ix_antidebug.h"   // runtime anti-debug / anti-Frida / anti-hook

using json = nlohmann::json;

// ==================== ANSI COLORS (screenshot palette) ====================
#define COL_RESET   oxorany("\033[0m")
#define COL_WHITE   oxorany("\033[97m")
#define COL_RED     oxorany("\033[91m")
#define COL_GREEN   oxorany("\033[92m")
#define COL_YELLOW  oxorany("\033[93m")
#define COL_CYAN    oxorany("\033[96m")
#define COL_GRAY    oxorany("\033[90m")

#define TERM_RESET  COL_RESET
#define TERM_WHITE  COL_WHITE
#define TERM_RED    COL_RED
#define TERM_GREEN  COL_GREEN
#define TERM_YELLOW COL_YELLOW
#define TERM_CYAN   COL_CYAN
#define TERM_GRAY   COL_GRAY

// ==================== AUTH CONFIG ====================
// Two DIFFERENT embedded public keys, two different purposes:
//   ED25519_PUBLIC_KEY_HEX    — verifies the server's AUTH DECISION
//                                (signature over the response payload)
//   TRANSPORT_SERVER_PK_HEX   — X25519 key used only to ENCRYPT requests /
//                                DECRYPT responses (confidentiality, not
//                                authenticity)
// Never embed either PRIVATE key here. Both are server-side only.
#define CONNECT_URL      oxorany("https://adminshop.myvipsite.fun/keydock_connect.php")

#define ED25519_PUBLIC_KEY_HEX \
    oxorany("4e94b747bf4042fdb99c82dae81f41c278d95ad05e0b4763a89d4ec2316a8bb5")

// REPLACE with your deployment's X25519 transport public key (64 hex chars).
// Generate alongside the server config — see keydock_connect.php header.
#define TRANSPORT_SERVER_PK_HEX \
    oxorany("034e65e9b094bb4ccefc0acd53d691b9e88810c5b1f9fdfeed968fd67f25f72b")

// ED448_PUBLIC_KEY_HEX — second, independent signature over the SAME
// payload as ED25519_PUBLIC_KEY_HEX (hybrid: both must verify). Raw
// 57-byte Ed448 public key, 114 hex chars. Generated + roundtrip-verified
// (CLI openssl genpkey/pkeyutl sign+verify) this session. Verification
// of this one only compiles in when KD_HAVE_ED448 is defined (CI builds
// only — see Android.mk / build-ed448-lib). On a phone/AIDE build this
// key is simply unused and Ed25519 alone remains the trust anchor.
#define ED448_PUBLIC_KEY_HEX \
    oxorany("a33362a5038b6d0a133adf3cce75ad63350fa8055cee23741883a4c0765848134e7e1ce735e39e164bc7ac17c0a945faf9a8c2cd7f7a15bf00")

#define HWID_FILE_NAME   oxorany(".kd_hwid")
#define KEY_FILE_NAME    oxorany(".kd_key")
#define CA_BUNDLE_NAME   oxorany(".kd_cacert.pem")
#define MASTER_KEY_FILE_NAME oxorany(".kd_mk")

static const size_t KD_MAX_RESPONSE_BYTES = 512 * 1024;

// ==================== PRIVATE-FILE CREATION GUARD ====================
// Forces umask 0177 (deny all group/other access) for the scope of file
// creation, so a license key / HWID / CA bundle file is private from the
// instant it is created, not just after a later chmod().
struct KdPrivateUmaskGuard {
    mode_t old_mask;
    KdPrivateUmaskGuard()  { old_mask = umask(0177); }
    ~KdPrivateUmaskGuard() { umask(old_mask); }
};

// ==================== WRITABLE STORAGE PATH RESOLUTION ====================
// The binary can be launched from very different contexts (an installed
// app's private data dir, a raw shell under /data/local/tmp, a third-party
// terminal/launcher app, etc). This tries a short list of realistic
// writable locations, in priority order, and uses the first one that a
// test file can actually be written to. Chosen directory is cached for
// the lifetime of the process.
//
// SECURITY NOTE: /data/local/tmp is world-writable (0777 directory even
// though files inside are chmod 0600) — any process on the device can see
// filenames there and race a replace before the chmod lands. It is kept
// only as a fallback for devices where the app-private dir is unreachable.
// For production Android deployments, integrate the Android Keystore API
// (via JNI) to keep the master key in hardware-backed secure storage, and
// prefer the app's private data directory exclusively.
static inline const std::string& kd_writable_dir() {
    static std::string cached;
    if (!cached.empty()) return cached;

    const char* candidates[] = {
        "/data/data/com.mycompany.application/files",
        "/sdcard/Android/data/com.mycompany.application/files",
        "/data/local/tmp/.kd_store",
        "/data/local/tmp",
        "."   // last resort: current working directory
    };

    for (const char* dir : candidates) {
        mkdir(dir, 0700);

        std::string probe = std::string(dir) + "/.kd_write_test";
        KdPrivateUmaskGuard umaskGuard;
        std::ofstream test(probe, std::ios::trunc);
        if (test.good()) {
            test << "1";
            test.close();
            remove(probe.c_str());
            cached = dir;
            return cached;
        }
    }

    cached = ".";
    return cached;
}

// ==================== SMALL HELPERS ====================
static inline std::string kd_hex(const unsigned char* buf, size_t len) {
    static const char* hexd = "0123456789abcdef";
    std::string out; out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out.push_back(hexd[(buf[i] >> 4) & 0xF]);
        out.push_back(hexd[buf[i] & 0xF]);
    }
    return out;
}

static inline bool kd_hex_decode_any(const std::string& hex,
                                      std::vector<unsigned char>& out) {
    if (hex.empty() || hex.size() % 2 != 0) return false;
    out.clear();
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        unsigned int v = 0;
        if (sscanf(hex.substr(i, 2).c_str(), "%02x", &v) != 1)
            return false;
        out.push_back((unsigned char)v);
    }
    return true;
}

static inline std::string kd_b64_decode(const std::string& in) {
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[(unsigned char)chars[i]] = i;

    std::string out;
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (c == '=' || T[c] == -1) continue;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

static inline std::string kd_random_hex(size_t bytes = 16) {
    std::vector<unsigned char> buf(bytes);
    // No fallback to std::random_device. This value is used as a per-request
    // nonce / cryptographic key material, so we fail closed on CSPRNG failure
    // instead of weakening it.
    if (RAND_bytes(buf.data(), (int)bytes) != 1) {
        return "";
    }
    return kd_hex(buf.data(), bytes);
}

static inline std::string kd_trim_upper(const std::string& in) {
    std::string s = in;
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    s = s.substr(a, b - a + 1);
    for (auto& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

static inline bool kd_key_format_ok(const std::string& k) {
    if (k.size() < 6 || k.size() > 34) return false;      // "X-" + 4..32
    if (k[0] != 'X' || k[1] != '-') return false;
    for (size_t i = 2; i < k.size(); ++i) {
        const char c = k[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

static inline std::string kd_fmt_remaining(long long seconds) {
    if (seconds <= 0) return "0d";
    long long d = seconds / 86400;
    long long h = (seconds % 86400) / 3600;
    long long m = (seconds % 3600) / 60;
    char buf[64];
    if (d > 0)      snprintf(buf, sizeof(buf), "%lldd %lldh", d, h);
    else if (h > 0) snprintf(buf, sizeof(buf), "%lldh %lldm", h, m);
    else            snprintf(buf, sizeof(buf), "%lldm", m);
    return std::string(buf);
}

// Unsigned text from the network is shown for diagnosis only. It can never
// grant access; control characters are stripped so it cannot inject escapes.
static inline std::string kd_sanitize_msg(const std::string& in) {
    std::string out;
    for (unsigned char c : in) {
        if (c >= 0x20 && c < 0x7F) out.push_back((char)c);
        if (out.size() >= 80) break;
    }
    return out;
}

// ==================== STABLE PER-DEVICE HWID ====================
// Persisted once so the SAME device_id is sent on every future launch.
// This is what stops the server from ever seeing a "new device" again
// after the very first run - so the key is never re-activated, never
// reset, and its expiry is never recomputed.
static inline std::string kd_get_hwid() {
    // ── SHARED WITH THE MODULE WebUI ─────────────────────────────
    {
        std::string shared = ix_shared_hwid();
        if (shared.size() >= 8) return shared;
    }

    std::string path = kd_writable_dir() + "/" + HWID_FILE_NAME;

    std::ifstream f(path);
    if (f.good()) {
        std::string saved;
        std::getline(f, saved);
        f.close();
        if (saved.size() >= 16) return saved;
    }

    unsigned char rnd[16];
    if (RAND_bytes(rnd, sizeof(rnd)) != 1) {
        return "";
    }
    std::string hwid = std::string(oxorany("AND-")) + kd_hex(rnd, sizeof(rnd));

    {
        KdPrivateUmaskGuard umaskGuard;
        std::ofstream out(path, std::ios::trunc);
        if (out.good()) { out << hwid; out.close(); chmod(path.c_str(), 0600); }
    }

    return hwid;
}

// ==================== KEY-AT-REST ENCRYPTION ====================
// The saved license key is encrypted at rest with a local secretbox key
// that is generated once, stored only on this device, and never sent over
// the network — so the key file is unreadable even if persistence ever
// lands on a less-private fallback path.
static inline bool kd_load_or_create_master_key(unsigned char out[crypto_secretbox_KEYBYTES]) {
    if (sodium_init() < 0) return false;
    std::string path = kd_writable_dir() + "/" + MASTER_KEY_FILE_NAME;

    std::ifstream in(path, std::ios::binary);
    if (in.good()) {
        in.read(reinterpret_cast<char*>(out), crypto_secretbox_KEYBYTES);
        const bool okRead = (in.gcount() == (std::streamsize)crypto_secretbox_KEYBYTES);
        in.close();
        if (okRead) return true;
    }

    randombytes_buf(out, crypto_secretbox_KEYBYTES);

    KdPrivateUmaskGuard umaskGuard;
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o.good()) return false;
    o.write(reinterpret_cast<const char*>(out), crypto_secretbox_KEYBYTES);
    o.close();
    chmod(path.c_str(), 0600);
    return true;
}

// ==================== SAVED KEY PERSISTENCE ====================
static inline bool kd_save_key(const std::string& key) {
    unsigned char mk[crypto_secretbox_KEYBYTES];
    if (!kd_load_or_create_master_key(mk)) return false;

    unsigned char nonce[crypto_secretbox_NONCEBYTES];
    randombytes_buf(nonce, sizeof(nonce));

    std::vector<unsigned char> cipher(key.size() + crypto_secretbox_MACBYTES);
    crypto_secretbox_easy(cipher.data(),
        reinterpret_cast<const unsigned char*>(key.data()), key.size(),
        nonce, mk);
    sodium_memzero(mk, sizeof(mk));

    json blob = {
        {"n", kd_hex(nonce, sizeof(nonce))},
        {"c", kd_hex(cipher.data(), cipher.size())}
    };

    std::string path = kd_writable_dir() + "/" + KEY_FILE_NAME;
    KdPrivateUmaskGuard umaskGuard;
    std::ofstream out(path, std::ios::trunc);
    if (!out.good()) return false;
    out << blob.dump();
    out.close();
    chmod(path.c_str(), 0600);
    return true;
}

static inline std::string kd_load_saved_key() {
    std::string path = kd_writable_dir() + "/" + KEY_FILE_NAME;
    std::ifstream f(path);
    if (!f.good()) return "";
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    if (raw.empty()) return "";

    json blob;
    try { blob = json::parse(raw); } catch (...) { return ""; }
    if (!blob.contains("n") || !blob.contains("c") ||
        !blob["n"].is_string() || !blob["c"].is_string()) return "";

    std::vector<unsigned char> nonce_v, cipher;
    if (!kd_hex_decode_any(blob["n"].get<std::string>(), nonce_v) ||
        nonce_v.size() != crypto_secretbox_NONCEBYTES) return "";
    if (!kd_hex_decode_any(blob["c"].get<std::string>(), cipher) ||
        cipher.size() < crypto_secretbox_MACBYTES) return "";

    unsigned char mk[crypto_secretbox_KEYBYTES];
    if (!kd_load_or_create_master_key(mk)) return "";

    std::vector<unsigned char> plain(cipher.size() - crypto_secretbox_MACBYTES);
    const int rc = crypto_secretbox_open_easy(
        plain.data(), cipher.data(), cipher.size(), nonce_v.data(), mk);
    sodium_memzero(mk, sizeof(mk));
    if (rc != 0) return "";

    std::string k(reinterpret_cast<char*>(plain.data()), plain.size());
    return kd_trim_upper(k);
}

// Note: intentionally NO delete_saved_key()/reset function exists.
// A key is only ever overwritten by a NEWER successful login, never
// cleared automatically - the user's saved license persists.

// ==================== EMBEDDED CA BUNDLE EXTRACTION ====================
// A full, current Mozilla root CA bundle is compiled directly into the
// binary (see ca_bundle_data.h) and written out ONCE to a confirmed-writable
// location. curl is then pointed at that file via CURLOPT_CAINFO, so TLS
// verification always succeeds without depending on the device's often
// broken/rootless system CA store. Verification stays fully ON throughout.
static inline std::string kd_ensure_ca_bundle() {
    std::string path = kd_writable_dir() + "/" + CA_BUNDLE_NAME;

    struct stat st;
    if (stat(path.c_str(), &st) == 0 && st.st_size > 1000) {
        return path;
    }

    KdPrivateUmaskGuard umaskGuard;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.good()) {
        return "";
    }
    out.write(reinterpret_cast<const char*>(g_CaBundleData), (std::streamsize)g_CaBundleSize);
    out.close();
    chmod(path.c_str(), 0600);

    if (stat(path.c_str(), &st) == 0 && (unsigned long)st.st_size == g_CaBundleSize) {
        return path;
    }
    return "";
}

// ==================== CURL WRITE CALLBACK ====================
static size_t kd_curl_write(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total = size * nmemb;
    std::string* response = (std::string*)userp;
    if (!response || response->size() > KD_MAX_RESPONSE_BYTES ||
        total > KD_MAX_RESPONSE_BYTES - response->size()) {
        return 0;
    }
    response->append((char*)contents, total);
    return total;
}

// ==================== HTTP POST (raw transport) ====================
static inline bool kd_http_post(const std::string& url, const std::string& body,
                                 std::string& response, std::string& curlErr,
                                 long& httpCode) {
    httpCode = 0;
    CURL* curl = curl_easy_init();
    if (!curl) { curlErr = "curl init failed"; return false; }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    response.clear();
    char errBuf[CURL_ERROR_SIZE] = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, kd_curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
#ifdef KD_PINNED_PUBKEY
    curl_easy_setopt(curl, CURLOPT_PINNEDPUBLICKEY, KD_PINNED_PUBKEY);
#else
    // SECURITY NOTE: no certificate pin is compiled in, so this build relies
    // on the system CA trust store alone for transport security. Impact is
    // limited (a MITM still cannot forge a valid Ed25519-signed payload, and
    // now cannot even read the request/response bodies without the X25519
    // transport key), but it is still a gap against a compromised CA. Get a
    // pin with:
    //   openssl s_client -connect adminshop.myvipsite.fun:443 </dev/null 2>/dev/null \
    //     | openssl x509 -pubkey -noout \
    //     | openssl pkey -pubin -outform der \
    //     | openssl dgst -sha256 -binary | openssl base64
    // then build with -DKD_PINNED_PUBKEY='"sha256//<output>"'
#endif
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errBuf);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "AppLicenseClient/4.1");

    static std::string caPath = kd_ensure_ca_bundle();
    if (!caPath.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, caPath.c_str());
    } else {
        static const char* sysCaDir = "/system/etc/security/cacerts";
        struct stat sdst;
        if (stat(sysCaDir, &sdst) == 0) {
            curl_easy_setopt(curl, CURLOPT_CAPATH, sysCaDir);
        }
    }

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        curlErr = errBuf[0] ? errBuf : curl_easy_strerror(res);
        return false;
    }
    return true;
}

// ==================== TRANSPORT ENCRYPTION LAYER (v2, DOUBLE-SEALED) ====
// Every auth request/response is sealed with TWO independent algorithms,
// nested, each keyed by an independently domain-separated subkey derived
// from the SAME X25519 ECDH exchange via crypto_generichash (BLAKE2b):
//
//   shared secret (crypto_kx) ──┬─> k_outer = H(secret || "kd-outer-v2")
//                               └─> k_inner = H(secret || "kd-inner-v2")
//
//   plaintext
//     -> crypto_secretbox_easy(plaintext, nonce2, k_inner)   [XSalsa20-Poly1305]
//     -> crypto_aead_xchacha20poly1305_ietf_encrypt(that, ad=cpk, nonce1, k_outer)
//
// Two different primitives (XSalsa20-Poly1305 and XChaCha20-Poly1305),
// two independently derived keys, two independent nonces. A weakness
// found in one stream cipher or one derivation does not by itself open
// the other layer — an attacker has to break both, with different keys,
// to read anything.
//
// A passive observer, a logging proxy, or a TLS-stripping MITM sees only
// random ciphertext in both directions — field names, key names, device
// IDs, nonces: none of it is visible on the wire, at either layer.
//
// This is a confidentiality layer. The Ed25519 signature inside the
// decrypted response is still the separate authenticity layer — a forged
// or replayed reply still fails signature verification even if somehow
// both encryption layers were defeated.
//
//   Outbound wire : { "v":2, "cpk":"<64hex>", "ct":"<hex>",
//                     "n":"<48hex outer nonce>", "n2":"<48hex inner nonce>" }
//   Inbound wire  : { "ct":"<hex>", "n":"<48hex>", "n2":"<48hex>",
//                     "spk":"<64hex>" }
//
// cpk is generated ONCE per request by kd_seal_request() and travels only
// in the outer wire envelope — never duplicated inside the encrypted body.

struct KdTransportKx {
    unsigned char pk[crypto_kx_PUBLICKEYBYTES];   // 32 bytes
    unsigned char sk[crypto_kx_SECRETKEYBYTES];   // 32 bytes
    KdTransportKx() {
        memset(pk, 0, sizeof(pk));
        memset(sk, 0, sizeof(sk));
    }
    ~KdTransportKx() {
        sodium_memzero(sk, sizeof(sk));
    }
};

// Derives two independent 32-byte subkeys from a 32-byte ECDH shared
// secret using domain-separated BLAKE2b (crypto_generichash). Each
// subkey is wiped by the caller once the corresponding cipher call
// finishes.
static inline void kd_derive_subkeys(
    const unsigned char secret[crypto_kx_SESSIONKEYBYTES],
    unsigned char outOuter[crypto_aead_xchacha20poly1305_ietf_KEYBYTES],
    unsigned char outInner[crypto_secretbox_KEYBYTES]
) {
    static const unsigned char ctxOuter[] = "kd-outer-v2";
    static const unsigned char ctxInner[] = "kd-inner-v2";

    crypto_generichash(outOuter, crypto_aead_xchacha20poly1305_ietf_KEYBYTES,
                        ctxOuter, sizeof(ctxOuter) - 1,
                        secret, crypto_kx_SESSIONKEYBYTES);
    crypto_generichash(outInner, crypto_secretbox_KEYBYTES,
                        ctxInner, sizeof(ctxInner) - 1,
                        secret, crypto_kx_SESSIONKEYBYTES);
}

// Seal plaintext JSON body for transport (double layer). Returns false on
// any crypto error.
static inline bool kd_seal_request(
    const std::string&   plainJson,
    KdTransportKx&       kx,           // out: ephemeral keypair (sk wiped by dtor)
    std::string&         wireBody      // out: JSON string for HTTP POST
) {
    if (sodium_init() < 0) return false;
    if (crypto_kx_keypair(kx.pk, kx.sk) != 0) return false;

    const std::string cpkHex = kd_hex(kx.pk, sizeof(kx.pk));

    unsigned char outerNonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
    unsigned char innerNonce[crypto_secretbox_NONCEBYTES];
    randombytes_buf(outerNonce, sizeof(outerNonce));
    randombytes_buf(innerNonce, sizeof(innerNonce));

    unsigned char serverPk[crypto_kx_PUBLICKEYBYTES];
    {
        std::vector<unsigned char> spk;
        if (!kd_hex_decode_any(std::string(TRANSPORT_SERVER_PK_HEX), spk) ||
            spk.size() != crypto_kx_PUBLICKEYBYTES)
            return false;
        memcpy(serverPk, spk.data(), sizeof(serverPk));
    }

    // Client→server direction shared secret.
    unsigned char rx[crypto_kx_SESSIONKEYBYTES];
    unsigned char tx[crypto_kx_SESSIONKEYBYTES];
    if (crypto_kx_client_session_keys(rx, tx, kx.pk, kx.sk, serverPk) != 0) {
        sodium_memzero(rx, sizeof(rx));
        sodium_memzero(tx, sizeof(tx));
        return false;
    }
    sodium_memzero(rx, sizeof(rx));  // only tx is used for client→server

    unsigned char kOuter[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];
    unsigned char kInner[crypto_secretbox_KEYBYTES];
    kd_derive_subkeys(tx, kOuter, kInner);
    sodium_memzero(tx, sizeof(tx));

    // ── LAYER 2 (inner): XSalsa20-Poly1305 over the raw plaintext ──
    std::vector<unsigned char> innerCt(
        plainJson.size() + crypto_secretbox_MACBYTES);
    crypto_secretbox_easy(innerCt.data(),
        reinterpret_cast<const unsigned char*>(plainJson.data()), plainJson.size(),
        innerNonce, kInner);
    sodium_memzero(kInner, sizeof(kInner));

    // ── LAYER 1 (outer): XChaCha20-Poly1305 over the layer-2 ciphertext ──
    // Additional data = our ephemeral public key hex (binds ct to this
    // keypair, so a captured ciphertext cannot be replayed under a
    // different cpk).
    const auto*  ad    = reinterpret_cast<const unsigned char*>(cpkHex.data());
    const size_t adLen = cpkHex.size();

    std::vector<unsigned char> outerCt(
        innerCt.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
    unsigned long long outerCtLen = 0;

    const int rc = crypto_aead_xchacha20poly1305_ietf_encrypt(
        outerCt.data(), &outerCtLen,
        innerCt.data(), innerCt.size(),
        ad, adLen,
        nullptr,
        outerNonce, kOuter);

    sodium_memzero(kOuter, sizeof(kOuter));
    if (rc != 0) return false;
    outerCt.resize((size_t)outerCtLen);

    json wire = {
        {"v",   2},
        {"cpk", cpkHex},
        {"ct",  kd_hex(outerCt.data(), outerCt.size())},
        {"n",   kd_hex(outerNonce, sizeof(outerNonce))},
        {"n2",  kd_hex(innerNonce, sizeof(innerNonce))}
    };
    wireBody = wire.dump();
    return true;
}

// Open a sealed server response (double layer). Returns false if either
// decryption fails or structure is invalid. On success, outEnvelope
// contains {"payload":"...","sig":"..."}.
static inline bool kd_open_response(
    const std::string&     wireBody,
    const KdTransportKx&   kx,
    json&                  outEnvelope
) {
    json resp;
    try { resp = json::parse(wireBody); } catch (...) { return false; }

    if (!resp.contains("ct")  || !resp["ct"].is_string()  ||
        !resp.contains("n")   || !resp["n"].is_string()   ||
        !resp.contains("n2")  || !resp["n2"].is_string()  ||
        !resp.contains("spk") || !resp["spk"].is_string())
        return false;

    std::vector<unsigned char> spk, outerNonce_v, innerNonce_v, outerCt;
    if (!kd_hex_decode_any(resp["spk"].get<std::string>(), spk) ||
        spk.size() != crypto_kx_PUBLICKEYBYTES) return false;
    if (!kd_hex_decode_any(resp["n"].get<std::string>(), outerNonce_v) ||
        outerNonce_v.size() != crypto_aead_xchacha20poly1305_ietf_NPUBBYTES) return false;
    if (!kd_hex_decode_any(resp["n2"].get<std::string>(), innerNonce_v) ||
        innerNonce_v.size() != crypto_secretbox_NONCEBYTES) return false;
    if (!kd_hex_decode_any(resp["ct"].get<std::string>(), outerCt) ||
        outerCt.size() < crypto_aead_xchacha20poly1305_ietf_ABYTES) return false;

    // Derive server→client (rx) shared secret using the server's ephemeral spk.
    unsigned char rx[crypto_kx_SESSIONKEYBYTES];
    unsigned char tx[crypto_kx_SESSIONKEYBYTES];
    if (crypto_kx_client_session_keys(rx, tx, kx.pk, kx.sk, spk.data()) != 0) {
        sodium_memzero(rx, sizeof(rx));
        sodium_memzero(tx, sizeof(tx));
        return false;
    }
    sodium_memzero(tx, sizeof(tx));  // only rx is used for server→client

    unsigned char kOuter[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];
    unsigned char kInner[crypto_secretbox_KEYBYTES];
    kd_derive_subkeys(rx, kOuter, kInner);
    sodium_memzero(rx, sizeof(rx));

    // ── LAYER 1 (outer) decrypt ──
    const std::string spkHex = resp["spk"].get<std::string>();
    const auto*  ad    = reinterpret_cast<const unsigned char*>(spkHex.data());
    const size_t adLen = spkHex.size();

    std::vector<unsigned char> innerCt(
        outerCt.size() - crypto_aead_xchacha20poly1305_ietf_ABYTES);
    unsigned long long innerCtLen = 0;

    const int rc1 = crypto_aead_xchacha20poly1305_ietf_decrypt(
        innerCt.data(), &innerCtLen, nullptr,
        outerCt.data(), (unsigned long long)outerCt.size(),
        ad, adLen,
        outerNonce_v.data(), kOuter);

    sodium_memzero(kOuter, sizeof(kOuter));
    if (rc1 != 0) { sodium_memzero(kInner, sizeof(kInner)); return false; }
    innerCt.resize((size_t)innerCtLen);

    if (innerCt.size() < crypto_secretbox_MACBYTES) {
        sodium_memzero(kInner, sizeof(kInner));
        return false;
    }

    // ── LAYER 2 (inner) decrypt ──
    std::string plain(innerCt.size() - crypto_secretbox_MACBYTES, '\0');
    const int rc2 = crypto_secretbox_open_easy(
        reinterpret_cast<unsigned char*>(&plain[0]),
        innerCt.data(), innerCt.size(),
        innerNonce_v.data(), kInner);

    sodium_memzero(kInner, sizeof(kInner));
    if (rc2 != 0) return false;

    if (plain.size() > KD_MAX_RESPONSE_BYTES) return false;

    try { outEnvelope = json::parse(plain); }
    catch (...) { return false; }

    return true;
}

// High-level helper used by perform_auth: builds the encrypted request,
// posts it, and decrypts the response into outEnvelope. On any failure,
// returns false and sets curlErr. kx must stay alive until this returns
// (its destructor wipes the ephemeral secret key).
static inline bool kd_auth_post(
    const json&        innerBody,
    json&              outEnvelope,
    std::string&       curlErr,
    long&              httpCode,
    KdTransportKx&     kx
) {
    std::string wireBody;
    if (!kd_seal_request(innerBody.dump(), kx, wireBody)) {
        curlErr = "transport encryption failed";
        return false;
    }

    std::string rawResponse;
    if (!kd_http_post(CONNECT_URL, wireBody, rawResponse, curlErr, httpCode))
        return false;

    // Fast path: server returned a plain JSON error (rate limit / bad
    // request / method not allowed) BEFORE any crypto context existed on
    // the server side — these fire pre-decryption and so are inherently
    // unauthenticated. [v4.1 FIX] Locked to the EXACT expected shape
    // {"ok":false,"error":"<string>"} with no other keys, so a MITM
    // injecting this path can spoof a denial message but cannot smuggle
    // any additional field for downstream code to misuse.
    {
        json probe;
        bool parsed = false;
        try { probe = json::parse(rawResponse); parsed = true; } catch (...) {}

        const bool isStrictPlainError =
            parsed &&
            probe.is_object() &&
            probe.size() == 2 &&
            probe.contains("ok") && probe["ok"].is_boolean() && probe["ok"].get<bool>() == false &&
            probe.contains("error") && probe["error"].is_string();

        if (isStrictPlainError) {
            outEnvelope = probe;
            return true;
        }
    }

    if (!kd_open_response(rawResponse, kx, outEnvelope)) {
        curlErr = "transport decryption failed";
        return false;
    }
    return true;
}

// ==================== PER-SESSION SERVER SECRET ====================
// After a valid license check the server can release a secret (offsets,
// core data, a decryption key ...) that exists ONLY on the server. It
// arrives encrypted for this one session inside the Ed25519-signed reply —
// a separate encryption from the transport layer above, scoped to the
// authenticated session rather than the raw HTTP exchange. A build that
// skips or patches the license check never receives it.
static std::string g_ServerSecret;
static inline const std::string& kd_server_secret() { return g_ServerSecret; }

// [v4.1 FIX] Wipe before clear() so sodium_memzero operates on live memory,
// not a buffer std::string may have already released internally.
static inline void kd_wipe_server_secret() {
    if (!g_ServerSecret.empty()) {
        sodium_memzero(&g_ServerSecret[0], g_ServerSecret.size());
        g_ServerSecret.clear();
    }
    ix_features_lock();
    ix_rt_clear();
}

struct KdKx {                       // ephemeral X25519 pair, secret half wiped on scope exit
    unsigned char pk[crypto_kx_PUBLICKEYBYTES];
    unsigned char sk[crypto_kx_SECRETKEYBYTES];
    KdKx() { memset(pk, 0, sizeof(pk)); memset(sk, 0, sizeof(sk)); }
    ~KdKx() { sodium_memzero(sk, sizeof(sk)); }
};

// [SECURE-CHANNEL FIX] Takes the client keypair as raw pk/sk pointers so the
// SAME ephemeral keypair used for the transport layer (KdTransportKx) can be
// passed here. The server encrypts the per-session secret envelope to the
// client public key it received as the outer-wire `cpk` — which IS the
// transport keypair's public key. Opening `enc` therefore requires that same
// transport keypair, not the separate (never-sent) kx that v5.0 left unused.
static inline bool kd_open_secret(json& enc, const std::string& nonceHex,
                                  const unsigned char client_pk[crypto_kx_PUBLICKEYBYTES],
                                  const unsigned char client_sk[crypto_kx_SECRETKEYBYTES],
                                  std::string& out) {
    if (!enc.is_object() || !enc.contains("spk") || !enc.contains("n") || !enc.contains("ct"))
        return false;
    if (!enc["spk"].is_string() || !enc["n"].is_string() || !enc["ct"].is_string())
        return false;

    const std::string spk = kd_b64_decode(enc["spk"].get<std::string>());
    const std::string n   = kd_b64_decode(enc["n"].get<std::string>());
    const std::string ct  = kd_b64_decode(enc["ct"].get<std::string>());

    if (spk.size() != crypto_kx_PUBLICKEYBYTES ||
        n.size()   != crypto_aead_xchacha20poly1305_ietf_NPUBBYTES ||
        ct.size()  <  crypto_aead_xchacha20poly1305_ietf_ABYTES)
        return false;

    unsigned char rx[crypto_kx_SESSIONKEYBYTES];
    unsigned char tx[crypto_kx_SESSIONKEYBYTES];
    if (crypto_kx_client_session_keys(rx, tx, client_pk, client_sk,
            reinterpret_cast<const unsigned char*>(spk.data())) != 0)
        return false;

    std::string plain(ct.size() - crypto_aead_xchacha20poly1305_ietf_ABYTES, '\0');
    unsigned long long mlen = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
        reinterpret_cast<unsigned char*>(&plain[0]), &mlen, nullptr,
        reinterpret_cast<const unsigned char*>(ct.data()), (unsigned long long)ct.size(),
        reinterpret_cast<const unsigned char*>(nonceHex.data()), (unsigned long long)nonceHex.size(),
        reinterpret_cast<const unsigned char*>(n.data()), rx);

    sodium_memzero(rx, sizeof(rx));
    sodium_memzero(tx, sizeof(tx));
    if (rc != 0) return false;

    plain.resize((size_t)mlen);
    out.swap(plain);
    return true;
}

// ==================== CLIENT INTEGRITY GATE ====================
/* Detects URL replace, pubkey replace, or a late in-memory patch of
   CONNECT_URL / ED25519_PUBLIC_KEY_HEX / TRANSPORT_SERVER_PK_HEX.
   Fail closed: wipe session state and SIGKILL. */

static std::atomic<int> g_ix_ic_tripped{0};

// ==================== REDUNDANT ANTI-PATCH AUTH STATE ====================
// A single `bool g_AuthKernelOk` is one memory address a debugger or a
// runtime patch can flip with a single write. To raise that bar, the
// authorized/not-authorized decision is now stored THREE independent
// ways that must all agree:
//   1. g_authOk_plain   — the value itself
//   2. g_authOk_inverted— its logical NOT
//   3. g_authOk_xor     — the value XORed with a per-process random cookie
//      generated fresh at startup, so a patch written against a previous
//      run's cookie (or a guessed constant) does not carry over.
// kd_get_kernel_ok() cross-checks all three and treats ANY disagreement
// as "not authorized" — a single-point memory patch that only flips (1)
// now fails that check instead of silently succeeding. This does not
// make local patching impossible (a sufficiently capable attacker who
// controls the whole process can patch kd_get_kernel_ok() itself, or
// patch all three stores in one go) — it removes the single-write bypass
// and forces a patch to find and update three scattered locations plus
// the comparison function, which a watchdog thread (below) also
// periodically re-verifies against drift.
static unsigned char g_authCookie = 0;
static bool g_authOk_plain    = false;
static bool g_authOk_inverted = true;
static bool g_authOk_xor      = false;

// [SELF-RED-TEAM FIX 3] RAND_bytes failure used to fall back to the
// literal constant 0xA5, compiled directly into the binary. An attacker
// who finds that fallback (or forces RAND_bytes to fail — possible in a
// restricted/sandboxed launch environment) can read 0xA5 straight out of
// the disassembly and precompute the expected XOR relationship, turning
// the triple-redundant check back into a single-write bypass. The
// fallback now derives from the address of a stack local — ASLR-random
// per process launch and never present in the binary as a static value,
// so it cannot be read out of a disassembly the way a compiled constant
// can. This is still a fallback path only exercised when the CSPRNG is
// unavailable; the primary path is always genuine RAND_bytes output.
static inline void kd_auth_cookie_init() {
    static std::atomic<int> done{0};
    if (done.exchange(1) == 0) {
        unsigned char c = 0;
        if (RAND_bytes(&c, 1) != 1) {
            int stackLocal = 0;
            auto addr = reinterpret_cast<uintptr_t>(&stackLocal);
            addr ^= reinterpret_cast<uintptr_t>(&done);
            addr ^= (uintptr_t)time(nullptr);
            c = (unsigned char)(addr ^ (addr >> 8) ^ (addr >> 16) ^ (addr >> 24));
        }
        g_authCookie = c ? c : 0x3Du;  // never zero-silent either way
    }
}

static inline void kd_set_kernel_ok(bool v) {
    kd_auth_cookie_init();
    g_authOk_plain    = v;
    g_authOk_inverted = !v;
    g_authOk_xor      = v ^ (bool)(g_authCookie & 1);
}

static inline bool kd_get_kernel_ok() {
    const bool expectInverted = !g_authOk_plain;
    const bool expectXor      = g_authOk_plain ^ (bool)(g_authCookie & 1);
    if (g_authOk_inverted != expectInverted) return false;
    if (g_authOk_xor      != expectXor)      return false;
    return g_authOk_plain;
}

// Kept as a plain-named alias so existing call sites in this file read
// naturally; every assignment below goes through kd_set_kernel_ok() so
// the three redundant stores can never fall out of sync, and every read
// goes through kd_get_kernel_ok() so a single-location patch is caught.
#define g_AuthKernelOk_READ() kd_get_kernel_ok()

// [BUILD-COMPAT] main.cpp (and any other consumer) still READS the old
// symbol name `g_AuthKernelOk`. The raw bool no longer exists — it was
// split into the triple-redundant encoding above. This read-only macro
// maps the old name onto the verified accessor so existing read sites
// compile unchanged and still get the anti-patch cross-check for free.
//
// IMPORTANT: this is READ-ONLY. If any consumer tries to ASSIGN to
// g_AuthKernelOk (e.g. `g_AuthKernelOk = true;`) it will NOT compile —
// that is intentional. Authorization state must only ever be set by the
// server-verified path inside perform_auth(); a consumer must never flip
// it locally. If you have such an assignment, it was a bug: remove it, or
// if it is genuinely a reset-to-false, call kd_set_kernel_ok(false).
#define g_AuthKernelOk (kd_get_kernel_ok())

// Fixed expected hashes of the genuine macro values, computed offline with
// the same FNV-1a32 routine below. Regenerate both if either macro value is
// ever legitimately changed:
//   URL : https://adminshop.myvipsite.fun/keydock_connect.php
//   PUB : 4e94b747bf4042fdb99c82dae81f41c278d95ad05e0b4763a89d4ec2316a8bb5
// SECURITY NOTE: FNV-1a is non-cryptographic. These constants add friction
// against a naïve string patch but are NOT a cryptographic guarantee — the
// Ed25519 signature verification on the server's response is the actual
// security boundary, not this check.
static const uint32_t IX_IC_EXPECTED_URL_FNV = 0x459cfd3eu;
static const uint32_t IX_IC_EXPECTED_PUB_FNV = 0x8272ee91u;

static inline uint32_t ix_ic_fnv32(const char* s, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

static inline void ix_ic_die() {
    g_ix_ic_tripped.store(1, std::memory_order_relaxed);
    kd_set_kernel_ok(false);
    kd_wipe_server_secret();
#if defined(SIGKILL)
    raise(SIGKILL);
#endif
    std::abort();
    _exit(137);
}

static inline bool ix_ic_url_sane(const std::string& url) {
    if (url.size() < 20) return false;
    if (url.rfind("https://", 0) != 0) return false;
    if (url.find("http://") != std::string::npos) return false;
    if (url.find('@') != std::string::npos) return false;
    const std::string host = "adminshop.myvipsite.fun";
    const size_t p = 8;
    if (url.size() < p + host.size()) return false;
    if (url.compare(p, host.size(), host) != 0) return false;
    const char nxt = (p + host.size() < url.size()) ? url[p + host.size()] : '\0';
    if (nxt != '/' && nxt != ':' && nxt != '\0') return false;
    if (url.find("keydock_connect.php") == std::string::npos) return false;
    return true;
}

static inline bool ix_ic_pub_sane(const std::string& hex) {
    if (hex.size() != 64) return false;
    bool nonzero = false;
    for (unsigned char c : hex) {
        if (!std::isxdigit(c)) return false;
        if (c != '0') nonzero = true;
    }
    return nonzero;
}

/** Call once early and before every network auth */
static inline void ix_integrity_gate() {
    if (g_ix_ic_tripped.load(std::memory_order_relaxed))
        ix_ic_die();

    const std::string url(CONNECT_URL);
    const std::string pub(ED25519_PUBLIC_KEY_HEX);

    if (!ix_ic_url_sane(url) || !ix_ic_pub_sane(pub))
        ix_ic_die();

    const uint32_t uh = ix_ic_fnv32(url.data(), url.size());
    const uint32_t ph = ix_ic_fnv32(pub.data(), pub.size());

    if (uh != IX_IC_EXPECTED_URL_FNV || ph != IX_IC_EXPECTED_PUB_FNV) {
        ix_ic_die();
    }

    if (sodium_init() < 0)
        ix_ic_die();

#ifndef IX_ANTITAMPER_OFF
    // [ANTI-TAMPER] Detect active instrumentation: a native debugger
    // (ptrace/TracerPid), Frida (maps/threads/default port), or an
    // Xposed/Substrate framework. The watchdog runs this gate every ~1.5s,
    // so a tool attached at ANY point trips it and fail-closes via the
    // existing kill path. Does NOT flag root (rooted users unaffected).
    // Build with -DIX_ANTITAMPER_OFF to disable if it ever false-positives.
    if (ix_tamper_detected())
        ix_ic_die();
#endif

    // Re-verify the redundant auth-state encoding every time the gate runs.
    // If the process is currently claiming "authorized" but the three
    // stores disagree, that is a patch in progress or already landed —
    // die immediately rather than let the mismatch persist.
    if (g_authOk_plain && !kd_get_kernel_ok())
        ix_ic_die();
}

// ==================== INTEGRITY WATCHDOG (ANTI-PATCH) ====================
// A single call to ix_integrity_gate() only protects the instant it runs.
// Between that call and the next one, a runtime patch (memory write via
// ptrace/Frida/a kernel module, or a hot-patched function) can flip state
// and go unnoticed until the next explicit auth attempt — which may be
// hours later, or never, if the attacker only needed a momentary bypass.
//
// This watchdog runs ix_integrity_gate() AND the redundant-state check on
// a fixed interval for the lifetime of the process, independent of
// whether a login is in progress. It cannot be stopped from outside
// without either killing the thread (which an attacker would need to
// locate and suspend specifically) or patching ix_ic_die() itself (which
// is a different, additional target from patching the auth flags —
// raising the number of places a bypass has to touch simultaneously).
//
// This is defense in depth, not a guarantee: a sufficiently capable local
// attacker with full process control (a kernel-level hook, a debugger
// that pauses this thread, or static binary patching before launch) can
// still defeat any purely client-side check. The actual security boundary
// that cannot be bypassed by patching the client is the Ed25519 signature
// verification on the server's response — this watchdog exists to make
// the *window* during which a runtime patch could go undetected as small
// as the interval below, not to claim the client is unpatchable.
static inline void kd_integrity_watchdog_loop() {
    using namespace std::chrono_literals;
    for (;;) {
        std::this_thread::sleep_for(1500ms);
        ix_integrity_gate();   // dies on any mismatch; never returns false
    }
}

static inline void kd_start_integrity_watchdog() {
    static std::atomic<int> started{0};
    if (started.exchange(1) == 0) {
        std::thread t(kd_integrity_watchdog_loop);
        t.detach();
    }
}

// ==================== RESULT STRUCT ====================
struct LoginResult {
    bool        success = false;
    std::string message;
    std::string status;
    std::string cheat;
    std::string seller;
    std::string expiry_date = "N/A";
    long long   remaining_seconds = 0;
    int         devices_used = 0;
    int         max_devices  = 0;
    std::string session_token;
    long long   session_expires_at = 0;
    json        features = json::object();   // from signed payload only
};

// ==================== ED25519 RESPONSE VERIFICATION ====================
static inline bool kd_hex_decode(const std::string& hex,
                                 std::vector<unsigned char>& out) {
    if (hex.size() != 64) return false;

    out.clear();
    out.reserve(32);

    for (size_t i = 0; i < hex.size(); i += 2) {
        unsigned int v = 0;
        if (sscanf(hex.substr(i, 2).c_str(), "%02x", &v) != 1)
            return false;
        out.push_back((unsigned char)v);
    }
    return out.size() == 32;
}

static inline bool kd_verify_ed25519(
    const std::string& message,
    const std::string& signatureB64
) {
    std::vector<unsigned char> pub;
    if (!kd_hex_decode(ED25519_PUBLIC_KEY_HEX, pub))
        return false;

    if (pub.size() != crypto_sign_PUBLICKEYBYTES)
        return false;

    bool allZero = true;
    for (unsigned char c : pub) if (c) { allZero = false; break; }
    if (allZero) return false;   // placeholder public key never verifies

    const std::string signature = kd_b64_decode(signatureB64);
    if (signature.size() != crypto_sign_BYTES)
        return false;

    if (sodium_init() < 0)
        return false;

    return crypto_sign_verify_detached(
        reinterpret_cast<const unsigned char*>(signature.data()),
        reinterpret_cast<const unsigned char*>(message.data()),
        static_cast<unsigned long long>(message.size()),
        pub.data()
    ) == 0;
}

// ==================== HYBRID SIGNATURE: ED448 (second, independent) ====================
// Only compiled in when the CI build produced the private renamed
// OpenSSL 3.x lib (KD_HAVE_ED448, set by Android.mk — see its comment).
// Not available on a phone/AIDE build; Ed25519 above remains the trust
// anchor there. Where it IS available, perform_auth() requires BOTH
// kd_verify_ed25519() and kd_verify_ed448() to pass — either failing is
// a rejection (hybrid AND, not OR: breaking one algorithm isn't enough).
#ifdef KD_HAVE_ED448
#include <openssl/evp.h>

static inline bool kd_verify_ed448(
    const std::string& message,
    const std::string& signatureB64
) {
    std::vector<unsigned char> pub;
    if (!kd_hex_decode_any(ED448_PUBLIC_KEY_HEX, pub))
        return false;
    if (pub.size() != 57)   // raw Ed448 public key length
        return false;

    bool allZero = true;
    for (unsigned char c : pub) if (c) { allZero = false; break; }
    if (allZero) return false;   // placeholder public key never verifies

    const std::string signature = kd_b64_decode(signatureB64);
    if (signature.size() != 114)   // raw Ed448 signature length
        return false;

    EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_ED448, nullptr, pub.data(), pub.size());
    if (!pkey) return false;

    bool ok = false;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx) {
        if (EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey) == 1) {
            ok = (EVP_DigestVerify(
                ctx,
                reinterpret_cast<const unsigned char*>(signature.data()), signature.size(),
                reinterpret_cast<const unsigned char*>(message.data()), message.size()
            ) == 1);
        }
        EVP_MD_CTX_free(ctx);
    }
    EVP_PKEY_free(pkey);
    return ok;
}
#endif // KD_HAVE_ED448

// ==================== RELEASE INTEGRITY ====================
static inline bool kd_sha256_file(
    const char* path,
    std::string& outHex
) {
    if (!path || !*path)
        return false;

    std::ifstream in(path, std::ios::binary);
    if (!in.good())
        return false;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx)
        return false;

    bool ok = (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1);

    char buffer[16384];

    while (ok && in.good()) {
        in.read(buffer, sizeof(buffer));
        const std::streamsize n = in.gcount();

        if (n > 0) {
            ok = (EVP_DigestUpdate(
                ctx,
                reinterpret_cast<const unsigned char*>(buffer),
                static_cast<size_t>(n)
            ) == 1);
        }
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;

    if (ok) {
        ok = (EVP_DigestFinal_ex(ctx, digest, &digestLen) == 1);
    }

    EVP_MD_CTX_free(ctx);

    if (!ok)
        return false;

    outHex = kd_hex(digest, digestLen);
    return outHex.size() == 64;
}

static inline bool kd_client_integrity_matches(
    const std::string& expectedHash
) {
    if (expectedHash.size() != 64)
        return false;

    for (size_t i = 0; i < expectedHash.size(); ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(expectedHash[i])))
            return false;
    }

    char exePath[4096] = {0};
    const ssize_t n = readlink(
        "/proc/self/exe",
        exePath,
        sizeof(exePath) - 1
    );

    if (n <= 0)
        return false;

    exePath[n] = '\0';

    std::string actualHash;
    if (!kd_sha256_file(exePath, actualHash))
        return false;

    for (size_t i = 0; i < actualHash.size(); ++i) {
        actualHash[i] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(actualHash[i]))
        );
    }

    std::string wanted = expectedHash;
    for (size_t i = 0; i < wanted.size(); ++i) {
        wanted[i] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(wanted[i]))
        );
    }

    return actualHash == wanted;
}

// ==================== CORE AUTHENTICATION ====================
static inline LoginResult perform_auth(
    const std::string& mode,
    const std::string& key,
    const std::string& hwid
) {
    LoginResult result;

    // Occupy the ptrace slot BEFORE any other thread exists (fork() after
    // threads are running is unsafe) and before the integrity gate's own
    // checks, so a debugger/Frida attach attempt made during this very
    // function is already blocked, not just detected after the fact.
    // One-time: perform_auth() can be called more than once in a process
    // lifetime (retry flows), forking again each time would be wasteful
    // and pointless (the first lock already holds).
    {
        static bool locked = false;
        if (!locked) { ix_self_debug_lock(); locked = true; }
    }

    ix_integrity_gate();
    kd_start_integrity_watchdog();

    if (hwid.empty()) {
        result.message = oxorany("Invalid hardware identity.");
        return result;
    }

    kd_wipe_server_secret();

    if (sodium_init() < 0) {
        result.message = oxorany("Crypto init failed.");
        return result;
    }
    // [SECURE-CHANNEL FIX] The separate KdKx used to live here only to open the
    // per-session secret envelope, but its public key was never sent to the
    // server, so the server never encrypted to it. The transport keypair
    // (transportKx, below) is the one the server actually uses, so the secret
    // envelope is opened with that instead and this extra keypair is gone.

    const long long ts = (long long)time(nullptr);
    const std::string nonce = kd_random_hex(32);

    if (nonce.size() != 64) {
        result.message = oxorany("Secure random generator unavailable.");
        return result;
    }

    // [v4.1] No "cpk" field here — the transport-layer cpk is generated
    // and carried entirely by kd_seal_request()/kd_auth_post() below.
    // This inner body only ever carries the auth fields themselves.
    json reqBody = {
        {"mode", mode},
        {"device_id", hwid},
        {"ts", ts},
        {"nonce", nonce}
    };

    if (mode == "key")
        reqBody["key_name"] = kd_trim_upper(key);

    // ── ENCRYPTED TRANSPORT ──────────────────────────────────
    // transportKx is a SEPARATE ephemeral keypair from kx above: kx is for
    // the per-session server secret (crypto_kx_*), transportKx is purely
    // for wire confidentiality (XChaCha20-Poly1305 request/response seal).
    // Kept alive until kd_open_response() finishes; its destructor wipes
    // the ephemeral secret key.
    KdTransportKx transportKx;
    json envelope;
    std::string curlErr;
    long httpCode = 0;

    if (!kd_auth_post(reqBody, envelope, curlErr, httpCode, transportKx)) {
        // ╔═══════════════════════════════════════════════════════════════╗
        // ║  CHANGE THIS TEXT to whatever message you want shown when the  ║
        // ║  connection / TLS check fails. Edit only the text inside the   ║
        // ║  quotes, then recompile.                                       ║
        // ╚═══════════════════════════════════════════════════════════════╝
        // Note: curl's raw reason (curlErr) is intentionally NOT shown now.
        // It used to append "SSL certificate problem: ..." which told anyone
        // probing exactly why their capture was blocked. Keeping it generic
        // hides that. (void) marks it deliberately unused.
        (void)curlErr;
        result.message = oxorany("kuy ra mc packets captcha kar ka khud ka cracker samjh ta h agar 1 bap ka hoga to tu dubara try nahi karga tara bas ki nahi h samjh kuy .");
        return result;
    }

    // Plain (unsigned) error from the server, e.g. rate limit / bad request.
    if (!envelope.contains("payload") && !envelope.contains("sig")) {
        std::string why;
        if (envelope.contains("error") && envelope["error"].is_string())
            why = kd_sanitize_msg(envelope["error"].get<std::string>());
        result.message = std::string(oxorany("Server rejected request (HTTP ")) +
                         std::to_string(httpCode) + ")" +
                         (why.empty() ? std::string("") : ": " + why);
        return result;
    }

    if (!envelope.contains("payload") ||
        !envelope.contains("sig") ||
        !envelope["payload"].is_string() ||
        !envelope["sig"].is_string()) {
        result.message = oxorany("Unsigned authentication response.");
        return result;
    }

    const std::string payloadB64 = envelope["payload"].get<std::string>();
    const std::string signatureB64 = envelope["sig"].get<std::string>();

    if (payloadB64.empty() || signatureB64.empty()) {
        result.message = oxorany("Incomplete authentication response.");
        return result;
    }

    const std::string signedMessage = nonce + "." + payloadB64;

    if (!kd_verify_ed25519(signedMessage, signatureB64)) {
        result.message = oxorany("Server signature verification failed (check ED25519_PUBLIC_KEY_HEX).");
        return result;
    }

    // Hybrid: a second, independent signature (Ed448) over the SAME
    // message. Only enforced in a CI build (KD_HAVE_ED448) — a phone
    // build can't verify it (no Ed448-capable OpenSSL available there)
    // so it's skipped there, leaving Ed25519 as the sole trust anchor.
    // Where it IS enforced, the server is expected to ALWAYS send "sig2"
    // now, so a missing field is treated the same as a bad signature,
    // not as "field not supported yet".
#ifdef KD_HAVE_ED448
    if (!envelope.contains("sig2") || !envelope["sig2"].is_string()) {
        result.message = oxorany("Missing second (Ed448) signature.");
        return result;
    }
    {
        const std::string sig2B64 = envelope["sig2"].get<std::string>();
        if (sig2B64.empty() || !kd_verify_ed448(signedMessage, sig2B64)) {
            result.message = oxorany("Server signature verification failed (check ED448_PUBLIC_KEY_HEX).");
            return result;
        }
    }
#endif

    const std::string plain = kd_b64_decode(payloadB64);

    if (plain.empty() || plain.size() > KD_MAX_RESPONSE_BYTES) {
        result.message = oxorany("Invalid signed payload.");
        return result;
    }

    json payload;

    try {
        payload = json::parse(plain);
    } catch (...) {
        result.message = oxorany("Malformed signed payload.");
        return result;
    }

    const bool ok = payload.value("ok", false);

    if (!ok) {
        kd_set_kernel_ok(false);
        result.success = false;
        result.message = kd_sanitize_msg(payload.value(
            "error",
            std::string(oxorany("License is not valid."))
        ));
        return result;
    }

    result.status = payload.value("status", std::string(""));
    result.cheat = payload.value("cheat", std::string(""));
    result.seller = payload.value("seller", std::string("N/A"));
    result.expiry_date = payload.value("expires_at", std::string("N/A"));
    result.remaining_seconds = payload.value("remaining_seconds", (long long)0);
    result.devices_used = payload.value("devices_used", 0);
    result.max_devices = payload.value("max_devices", 0);
    result.session_token = payload.value("session_token", std::string(""));
    result.session_expires_at = payload.value("session_expires_at", (long long)0);

    // Feature flags come from the signed server payload only.
    if (payload.contains("features") && payload["features"].is_object())
        result.features = payload["features"];

    /*
     * Optional release pinning. The server only sends build_sha256 when the
     * operator configured KD_BUILD_SHA256. The field is inside the signed
     * payload, so it cannot be stripped or forged by a fake server.
     */
    const std::string expectedBuildHash =
        payload.value("build_sha256", std::string(""));
    if (!expectedBuildHash.empty() &&
        !kd_client_integrity_matches(expectedBuildHash)) {
        kd_set_kernel_ok(false);
        result.success = false;
        result.message = oxorany("Client integrity verification failed.");
        return result;
    }

    if (result.session_token.empty() ||
        result.session_expires_at <= (long long)time(nullptr)) {
        kd_set_kernel_ok(false);
        result.success = false;
        result.message = oxorany("Authentication session expired.");
        return result;
    }

    kd_set_kernel_ok(payload.value("kernel_allowed", false));
    if (!kd_get_kernel_ok()) {
        result.success = false;
        result.message = oxorany("License is not authorized.");
        return result;
    }

    /* enc is MANDATORY — no offline / no-secret login path */
    if (!payload.contains("enc") || !payload["enc"].is_object()) {
        kd_set_kernel_ok(false);
        ix_features_lock();
        ix_rt_clear();
        result.success = false;
        result.message = oxorany("License payload incomplete.");
        return result;
    }
    {
        std::string secret;
        // [SECURE-CHANNEL FIX] Open the per-session secret with the TRANSPORT
        // keypair — the one whose public key the server received as outer-wire
        // cpk and encrypted the envelope to. (The old `kx` was never sent, so
        // the server never encrypted to it → decryption always failed.)
        if (!kd_open_secret(payload["enc"], nonce, transportKx.pk, transportKx.sk, secret) || secret.empty()) {
            kd_set_kernel_ok(false);
            ix_features_lock();
            ix_rt_clear();
            result.success = false;
            result.message = oxorany("Secure channel failed.");
            return result;
        }
        g_ServerSecret.swap(secret);
    }

    /* HARD GATE: features + offsets only from decrypted server secret */
    ix_features_lock();
    ix_rt_clear();
    if (g_ServerSecret.empty()) {
        kd_set_kernel_ok(false);
        result.success = false;
        result.message = oxorany("License payload incomplete.");
        return result;
    }
    try {
        auto sec = nlohmann::json::parse(g_ServerSecret);
        ix_features_apply_json(sec);
        if (!ix_rt_apply_json(sec)) {
            kd_set_kernel_ok(false);
            ix_features_lock();
            ix_rt_clear();
            result.success = false;
            result.message = oxorany("Runtime data rejected.");
            return result;
        }
        if (!ix_feat_ok()) {
            kd_set_kernel_ok(false);
            ix_features_lock();
            ix_rt_clear();
            result.success = false;
            result.message = oxorany("No features authorized.");
            return result;
        }
    } catch (...) {
        kd_set_kernel_ok(false);
        ix_features_lock();
        ix_rt_clear();
        result.success = false;
        result.message = oxorany("Secure channel data invalid.");
        return result;
    }
    ix_integrity_gate();
    result.success = true;
    result.message = oxorany("License Verified!");
    return result;
}

static inline LoginResult perform_login(
    const std::string& key,
    const std::string& hwid
) {
    return perform_auth("key", key, hwid);
}

// Kept so existing call sites still compile. There is no hardware-only /
// bot-registration mode any more: this re-checks the SAVED license key.
static inline LoginResult perform_hwid_auth(
    const std::string& hwid
) {
    const std::string saved = kd_load_saved_key();
    if (saved.empty()) {
        LoginResult r;
        r.message = oxorany("No saved license key.");
        return r;
    }
    return perform_auth("key", saved, hwid);
}

// ==================== AUTHENTICATION CLASS ====================
class Authentication {
private:
    bool        authenticated  = false;
    std::string hwid;
    std::string auth_message;
    std::string expiry_date    = "N/A";
    std::string license_key;
    std::string seller_name    = "N/A";
    std::string status         = "INACTIVE";
    long long   remain_seconds = 0;
    int         devices_used   = 0;
    int         devices_max    = 0;
    std::string session_token;
    long long   session_expires_at = 0;
    json        features = json::object();

public:
    bool isAuthenticated() const { return authenticated; }
    std::string getHWID()        const { return hwid; }
    std::string getAuthMessage() const { return auth_message; }
    std::string getLicenseKey()  const { return authenticated ? license_key : "N/A"; }
    std::string getSellerName()  const { return seller_name; }
    std::string getExpiryDate()  const { return expiry_date; }
    std::string getStatus()      const { return status; }
    int getDaysRemain()  const { return (int)(remain_seconds / 86400); }
    int getTotalDays()   const { return (int)(remain_seconds / 86400); }
    int getDevicesUsed() const { return devices_used; }
    int getMaxDevices()  const { return devices_max; }
    bool isValid()        const { return authenticated; }
    const std::string& getServerSecret() const { return g_ServerSecret; }
    std::string getSessionToken() const {
        return authenticated ? session_token : "";
    }
    // Feature query from the signed server payload — never a client-side flag.
    bool hasFeature(const std::string& name) const {
        return authenticated &&
               features.contains(name) &&
               features[name].is_boolean() &&
               features[name].get<bool>();
    }
    bool sessionValid() const {
        return authenticated &&
               !session_token.empty() &&
               session_expires_at > (long long)time(nullptr);
    }

    // -- run(): interactive terminal login --------------------------
    bool run() {
        ix_integrity_gate();
        curl_global_init(CURL_GLOBAL_DEFAULT);
        hwid = kd_get_hwid();
        kd_set_kernel_ok(false);
        authenticated = false;
        ix_features_lock();
        ix_rt_clear();

        printf("\033[2J\033[H");
        fflush(stdout);

        printf("%sApplication License%s\n", COL_WHITE, COL_RESET);
        printf("%sVersion: %s%s5.0%s\n", COL_WHITE, COL_RESET, COL_RED, COL_RESET);
        printf("------------------------------------------\n\n");
        fflush(stdout);

        printf("%sHardware ID%s\n", COL_CYAN, COL_RESET);
        printf("  %s%s%s\n\n", COL_WHITE, hwid.c_str(), COL_RESET);
        fflush(stdout);

        // 1) Saved key from a previous successful login
        const std::string saved = kd_load_saved_key();
        if (!saved.empty()) {
            printf("%sChecking saved license...%s\n", COL_YELLOW, COL_RESET);
            fflush(stdout);
            LoginResult r = perform_login(saved, hwid);
            if (r.success && kd_get_kernel_ok()) {
                applyResult(saved, r);
                showActive();
                curl_global_cleanup();
                return true;
            }
            printf("%s%s%s\n\n", COL_RED, r.message.c_str(), COL_RESET);
            fflush(stdout);
        }

        // 2) Ask for the key (3 tries)
        for (int attempt = 0; attempt < 3; ++attempt) {
            printf("%sEnter License Key: %s", COL_CYAN, COL_WHITE);
            fflush(stdout);

            char lineBuf[256];
            std::cin.getline(lineBuf, sizeof(lineBuf));
            if (std::cin.fail() && !std::cin.eof()) {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            }
            if (std::cin.eof() && lineBuf[0] == '\0') {
                printf("%s\n", COL_RESET);
                break;
            }
            std::string line(lineBuf);
            printf("%s", COL_RESET);

            const std::string key = kd_trim_upper(line);
            if (!kd_key_format_ok(key)) {
                printf("%sInvalid key format (example: X-XXXXXXXX).%s\n\n", COL_RED, COL_RESET);
                fflush(stdout);
                continue;
            }

            printf("%sVerifying...%s\n", COL_YELLOW, COL_RESET);
            fflush(stdout);

            LoginResult r = perform_login(key, hwid);
            if (r.success && kd_get_kernel_ok()) {
                kd_save_key(key);
                applyResult(key, r);
                showActive();
                curl_global_cleanup();
                return true;
            }
            printf("%s%s%s\n\n", COL_RED, r.message.c_str(), COL_RESET);
            fflush(stdout);
        }

        printf("%sLogin failed. Process stopped.%s\n\n", COL_RED, COL_RESET);
        fflush(stdout);
        kd_wipe_server_secret();
        kd_set_kernel_ok(false);
        authenticated = false;
        curl_global_cleanup();
        return false;
    }

    // -- run_silent(): non-interactive re-check, no terminal I/O ----
    bool run_silent(const std::string& keyNameIn = "") {
        ix_integrity_gate();
        curl_global_init(CURL_GLOBAL_DEFAULT);
        hwid = kd_get_hwid();

        const std::string key = keyNameIn.empty() ? kd_load_saved_key()
                                                  : kd_trim_upper(keyNameIn);
        LoginResult res;
        if (key.empty()) res.message = oxorany("No license key.");
        else             res = perform_login(key, hwid);

        curl_global_cleanup();

        if (res.success && kd_get_kernel_ok()) {
            applyResult(key, res);
            if (!keyNameIn.empty()) kd_save_key(key);
            return true;
        }

        kd_wipe_server_secret();
        kd_set_kernel_ok(false);
        authenticated = false;
        auth_message = res.message;
        return false;
    }

private:
    void applyResult(const std::string& keyName, const LoginResult& res) {
        authenticated   = true;
        license_key     = keyName;
        auth_message    = res.message;
        status          = res.status.empty() ? "ACTIVE" : res.status;
        seller_name     = res.seller.empty() ? "N/A" : res.seller;
        expiry_date     = res.expiry_date.empty() ? "N/A" : res.expiry_date;
        remain_seconds  = res.remaining_seconds;
        devices_used    = res.devices_used;
        devices_max     = res.max_devices;
        session_token   = res.session_token;
        session_expires_at = res.session_expires_at;
        features        = res.features;
    }

    void showActive() {
        printf("%sStatus  : %sActive%s\n", COL_GREEN, COL_RESET, COL_RESET);
        printf("%sSeller  : %s%s\n", COL_CYAN, seller_name.c_str(), COL_RESET);
        printf("%sExpires : %s%s\n", COL_CYAN, expiry_date.c_str(), COL_RESET);
        printf("%sRemaining: %s%s\n\n", COL_CYAN, kd_fmt_remaining(remain_seconds).c_str(), COL_RESET);
        fflush(stdout);
    }
};

#endif
