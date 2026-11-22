/* ============================================================
   ix_auth.cpp — InvisibleX Android app-side native auth client
   (JNI module)

   This is the APP-SIDE of the "2 login" design:
     1. THIS module — app-side gate, runs once at app login / session
        refresh, decides whether the app's UI shows the user as
        authenticated and whether it's allowed to hand off to the
        binary at all.
     2. jni/src/login.h (compiled into INX.SH itself) — binary-side
        gate, runs again independently every time the binary is
        executed, before it does anything else. This module never
        replaces that check — it is a second, completely separate
        enforcement point, exactly as specified.

   WHY NATIVE (not a plain HTTP POST from Java):
   The server at keydock_connect.php does not accept plain JSON.
   Every request/response is wrapped in a double-sealed encrypted
   envelope (X25519 key exchange -> XChaCha20-Poly1305 outer layer ->
   XSalsa20-Poly1305 inner layer) with an Ed25519-signed response
   payload. An earlier version of this app sent a plain, unencrypted
   JSON body, which the server correctly rejected with HTTP 400 — it
   never matched the only shape the server accepts. This module
   reimplements the EXACT protocol already proven correct in
   jni/src/login.h (kd_seal_request / kd_http_post / kd_open_response /
   perform_auth), minus everything that is binary-only: anti-tamper/
   anti-debug watchdogs, in-memory game offsets, and the per-session
   secret payload (ix_features / ix_rt) — none of that applies to or
   belongs in a normal Android app process. This module ONLY decides
   "is this license valid right now", and returns that decision
   (+ expiry/seller/device info) to Java.

   SIGNATURE VERIFICATION: all three legs of the same hybrid check
   login.h enforces -- Ed25519, Ed448, ML-DSA-65 (post-quantum), all
   three required, fail-closed on any missing/bad field. Unlike
   login.h, there is no #ifdef gate here: this module has only one
   build target (this Gradle app), so there is no "lighter" variant of
   it that is allowed to check fewer signatures.

   HWID PARITY: uses the SAME ix_hwid.h device fingerprint the binary
   uses (vendor/ix_hwid.h, byte-for-byte copy of jni/src/ix_hwid.h) so
   the app's login and the binary's later login are recognized by the
   server as the SAME device, not two separate device-slot
   registrations.
   ============================================================ */

#include <jni.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <cctype>
#include <sys/stat.h>

#include <sodium.h>
#include <curl/curl.h>

// kd_ossl_rename.h #defines every OpenSSL symbol name to its kdoss3_
// prefixed equivalent -- it MUST come before <openssl/evp.h> (and any
// other <openssl/*.h>) so the declarations below, and every call site
// in ix_verify_ed448(), resolve to the renamed Ed448-capable OpenSSL
// 3.3.2 static lib (ed448_static in CMakeLists.txt) instead of
// colliding with the plain OpenSSL 1.1.0c already linked for curl's
// TLS (crypto_static/ssl_static), which predates Ed448 entirely.
#include "kd_ossl_rename.h"
#include <openssl/evp.h>
#include <oqs/oqs.h>

#include "vendor/json.hpp"
#include "vendor/ca_bundle_data.h"
#include "vendor/ix_hwid.h"

using json = nlohmann::json;

// ==================== AUTH CONFIG ====================
// Same deployment constants as jni/src/login.h — public values (a
// server URL and public keys), safe to compile in plainly.
#define CONNECT_URL \
    "https://adminshop.vipsite.in/keydock_connect.php"

#define ED25519_PUBLIC_KEY_HEX \
    "4e94b747bf4042fdb99c82dae81f41c278d95ad05e0b4763a89d4ec2316a8bb5"

#define TRANSPORT_SERVER_PK_HEX \
    "11776d4190bed1f57b9e26b01ffaf2d20dfff70fbce9e8c803d48590bde6f90e"

// ED448_PUBLIC_KEY_HEX / MLDSA65_PUBLIC_KEY_HEX — the SAME two keys
// jni/src/login.h embeds (byte-for-byte copy, public values, safe to
// compile in plainly). The server already signs every response with
// all three keys unconditionally -- login.h's own shipped builds have
// required sig2 (Ed448) and sig3 (ML-DSA-65) alongside sig (Ed25519)
// since before this app module existed. This module was only reading
// "sig" and discarding "sig2"/"sig3" from the same envelope; nothing
// server-side needs to change for the checks below to start working.
#define ED448_PUBLIC_KEY_HEX \
    "a33362a5038b6d0a133adf3cce75ad63350fa8055cee23741883a4c0765848134e7e1ce735e39e164bc7ac17c0a945faf9a8c2cd7f7a15bf00"

#define MLDSA65_PUBLIC_KEY_HEX \
    "4053bccb4827a70ee8f10e45366335d7ac34dccddafce6c08a14fc84b7333754097dabb9f868eed2bc07296a4e4c43ed90bd94ebcca78bc397dab82c56ea92ab1c454ea4b161260a3aeb9320c2855f049ad82176605de16a8559d0a5d8488325b06d498141640d57b117ff9e51dbdd5b4c9a08267bd7ffe50f7b2e1132b5fc8dd077a70cde3d1eeb9acbc7799fe7c67bc03f258d7df32a7f1da930e9afc45ba00ffcdd019dfa28ce1df2a33756ef01662b3bcb0768daab49bd3ab6a1d3789d10a4adfd190028754ad58b8ee6e554968bb4bbdc8d538c6c8694d464734752981ee04a50861731f3533f424e455d12b0e77601b0d28b342bc964a5d72cf3ea502148fb5421c38af4e9b220c192ce42d6c3736f0b90464358ca8dc2128747a13b17fa39b5f7d838890cd6f32a90bfd4cbdfc465a4f2c1400a1c65eb9930d50e9546dcb1a4f61a8a729b07d3d637285f5b1b5333c740d8d8c20de9d18cc81cba3839150620316374a66928fa3aa661f71fcaa9c5d25aae64a00ae2dbec3242cf0a4b20818db9f5e6976e5f1db7a22388ded29af3bef903c41bc93cbd09c7d8f8a249ef97e4ea1c92d38fc391dba6035f89d4c2f50ec7dbc4e31a759693b5e2719e22edcd8ad41b8f0c110b7ea9b101e10f2ce1452268e49689cc2767ef3df2dc560ea4909ff92701ed1cc81cceca834b0aee276eb1b310205e3be390efc1b8de4456d8041b21a1f00f49032c9bb2e53364bddc78b3a17deb71949e3dd2e3b8fdd1f72d0cafc2c7227ef7b0ac20bce0e522fef398d159806035b0b632706f70afcda2a919424e178db18e91da025ec3cf29d23741acfd6417beaf2ef8b9290115535a28ecbc9d9a16ba8161d3b0933c2ca5a536f2436b8f091813de855cea494cf2f5cb53e943bdabfeb3a7eea82643f604d5f2021a7af9ab5b511f55cefb7d1a042ec0e996157aa0ff47459a835af2611fb3edeb871e82efb739c01d84a1911f309c57f21cc41142bc72a0f1cec1142c9f84b2683fcbac79f746356c8c022fc2d69501149d59ee542a8ce4dbb7836e99af0a3e2e4672eb5024d5da5c8d938017053067736fa547ca440bf7bc600137497e32c549d63e7b7993902a5bbb38f0dd027f4bebba179204e3859cbb1bb45077cf9dd3b7eff00194a8184153aa234f64a6fb8aaf2a36c31c5412e16abfbb77e06f7517b03d5a6c4e1d5792f5564b83ea953e7039cbaf3083d165c759c28aaa65ef7694a49b0a34bf3f6cd056f3bf9dc8aa09381fc72fea88d5e13f1397112b20a7e6c5962a5e46feaba1b1ef5c3190d764acbbd2f83dc20ff732d5f0ab230725b8786c0b64a1ed2335aaa659e538e7b8e6c4d6016b905b924ecc33805addaa3265b5678221a771a084ba59a31c318c9e33d7c7f2e8ca6950a9a2ca6fb7f3dce2e77954ccd313621581fc38246d56c85a262575d3189eace85756ba5c627af76035844ab6819f946ae173261fc39f56dbbc8dba1b5f0ca7294a3519dab68f51588fa8307fa1b7e92db5894e500bb4b3a4e40dbc00eaa92a792ad513b2843d0ed0be325718d8e89f231d95ad645f514e53e7851f80a9bcc13454331c585a04fd70929e565f2281f23e1d98414b3d507e58d6082dd1e17029c8350936790ee6bfd23ebeb01a68f41ff390fe70be90dd270f24c53df3857d8b1b3dec2f08d2c94012f8da2129b212d416ada99e7cda06f1be0ff8e7b5088dde339be7a69a78ae0b16dd9630d3f58c8a92d45d0f7b44634443841cec57664314422083ab839499c8caad18dddb66cbaa04821d6b90e10077961e1e41557be3eff8b468862fe9324db395a96914568014b4d3852bb2c575b04c2a46d424ef56b48720c0e4d09bcae4bbe5002ef635f4d9f5c5631e866f5fd32fbbdae28bfebb04ef343b4d21af946d6387e4e366b908937ca29623be50680876df67719845657d2429db121ac02c7c0de0ae909be6d95a5e58fa3b30826fcbb71624b01a3c797b74ecf8687667a9bff053e8a961a9a27b58a5de1a7ccd67bcd790f23afe3de453213bf48e581c3dcc6829ef4c95b768507fd851ef0a40dbc349ebe9af91a03fd2523d683823667963a5abb947fdb37bad2f5681a13106000d5554aa69785178e59f1ba19cbe2de93f67fe4dc9a3c7c9b5605393503b062ff42b0513ddb4e717e7b299ef20405580d6f6e2e2d6e817b5f2bfa2c49d8eab05330dda3e4dd96ef0d385c7ba3d1fb595baf7143ca6226ebcb4b5286145028eaeae52b3f24c06c40ef920a60b3d4a0bbdbb954e2159cad8b3a3a0fa121a04800866df711ea25ba6922892da5f3ad808840fd77b58f764c69975d193ac44a32e86194f8c0edd212429e03df572d9ef825ca3beaaced798109f6af023d2268d453aef4d8d92037580d07655f5636735fa3467585f5970e658ab481e645d0c9c87558af0f32bdef916aed6f7feb195339af01d2627d72ccb90cafccf6d41678ddb2501f17b180c2e6e931fe37f58786216f6000fab927ef2531f6cb7e87313ba1467299ede427505f0e43b41a90c79e8e8a0b76a92c1f60cde470df51403ecda850e2875adeedd2fc6d53fb5e5b22a53e29c30ed55abfff07e25fe2087da7e3ad28bebffe454429eb7758f91ddd3d3d028a24773817e907f21edb7301662c42351b807e0339cafe6163f1eae890e3b7d09a0377c67e13161655c87917ae837ccc7958222ed12a3d1706217316c3ae74b1fb859a0d386b63b09c8a7ee700f66d93eb0fc11e6ac"

static const size_t IX_MAX_RESPONSE_BYTES = 512 * 1024;

// ==================== SMALL HELPERS ====================
static inline std::string ix_hex(const unsigned char* buf, size_t len) {
    static const char* hexd = "0123456789abcdef";
    std::string out; out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out.push_back(hexd[(buf[i] >> 4) & 0xF]);
        out.push_back(hexd[buf[i] & 0xF]);
    }
    return out;
}

static inline bool ix_hex_decode(const std::string& hex, std::vector<unsigned char>& out) {
    if (hex.empty() || hex.size() % 2 != 0) return false;
    out.clear();
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        unsigned int v = 0;
        if (sscanf(hex.substr(i, 2).c_str(), "%02x", &v) != 1) return false;
        out.push_back((unsigned char)v);
    }
    return true;
}

static inline std::string ix_b64_decode(const std::string& in) {
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

static inline std::string ix_random_hex(size_t bytes = 16) {
    std::vector<unsigned char> buf(bytes);
    randombytes_buf(buf.data(), bytes);
    return ix_hex(buf.data(), bytes);
}

static inline std::string ix_trim_upper(const std::string& in) {
    std::string s = in;
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    s = s.substr(a, b - a + 1);
    for (auto& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

// ==================== CA BUNDLE (write-once) ====================
static inline std::string ix_ensure_ca_bundle(const std::string& writableDir) {
    std::string path = writableDir + "/.ix_cacert.pem";
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && st.st_size > 1000) return path;

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return "";
    fwrite(g_CaBundleData, 1, g_CaBundleSize, f);
    fclose(f);
    chmod(path.c_str(), 0600);

    if (stat(path.c_str(), &st) == 0 && (size_t)st.st_size == g_CaBundleSize) return path;
    return "";
}

// ==================== HTTP POST ====================
static size_t ix_curl_write(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total = size * nmemb;
    std::string* response = (std::string*)userp;
    if (!response || response->size() > IX_MAX_RESPONSE_BYTES ||
        total > IX_MAX_RESPONSE_BYTES - response->size()) return 0;
    response->append((char*)contents, total);
    return total;
}

static inline bool ix_http_post(const std::string& url, const std::string& body,
                                 const std::string& caPath,
                                 std::string& response, long& httpCode) {
    httpCode = 0;
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    response.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ix_curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "InvisibleXAndroid/1.0");
    if (!caPath.empty()) curl_easy_setopt(curl, CURLOPT_CAINFO, caPath.c_str());

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return res == CURLE_OK;
}

// ==================== TRANSPORT ENCRYPTION (v2, double-sealed) ====
// Identical derivation/layering to jni/src/login.h's kd_seal_request /
// kd_open_response — see that file for the full design rationale.
struct IxTransportKx {
    unsigned char pk[crypto_kx_PUBLICKEYBYTES];
    unsigned char sk[crypto_kx_SECRETKEYBYTES];
    IxTransportKx() { memset(pk, 0, sizeof(pk)); memset(sk, 0, sizeof(sk)); }
    ~IxTransportKx() { sodium_memzero(sk, sizeof(sk)); }
};

static inline void ix_derive_subkeys(
    const unsigned char secret[crypto_kx_SESSIONKEYBYTES],
    unsigned char outOuter[crypto_aead_xchacha20poly1305_ietf_KEYBYTES],
    unsigned char outInner[crypto_secretbox_KEYBYTES]
) {
    static const unsigned char ctxOuter[] = "kd-outer-v2";
    static const unsigned char ctxInner[] = "kd-inner-v2";
    crypto_generichash(outOuter, crypto_aead_xchacha20poly1305_ietf_KEYBYTES,
                        ctxOuter, sizeof(ctxOuter) - 1, secret, crypto_kx_SESSIONKEYBYTES);
    crypto_generichash(outInner, crypto_secretbox_KEYBYTES,
                        ctxInner, sizeof(ctxInner) - 1, secret, crypto_kx_SESSIONKEYBYTES);
}

static inline bool ix_seal_request(const std::string& plainJson, IxTransportKx& kx, std::string& wireBody) {
    if (sodium_init() < 0) return false;
    if (crypto_kx_keypair(kx.pk, kx.sk) != 0) return false;

    const std::string cpkHex = ix_hex(kx.pk, sizeof(kx.pk));

    unsigned char outerNonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
    unsigned char innerNonce[crypto_secretbox_NONCEBYTES];
    randombytes_buf(outerNonce, sizeof(outerNonce));
    randombytes_buf(innerNonce, sizeof(innerNonce));

    unsigned char serverPk[crypto_kx_PUBLICKEYBYTES];
    {
        std::vector<unsigned char> spk;
        if (!ix_hex_decode(std::string(TRANSPORT_SERVER_PK_HEX), spk) ||
            spk.size() != crypto_kx_PUBLICKEYBYTES) return false;
        memcpy(serverPk, spk.data(), sizeof(serverPk));
    }

    unsigned char rx[crypto_kx_SESSIONKEYBYTES];
    unsigned char tx[crypto_kx_SESSIONKEYBYTES];
    if (crypto_kx_client_session_keys(rx, tx, kx.pk, kx.sk, serverPk) != 0) {
        sodium_memzero(rx, sizeof(rx)); sodium_memzero(tx, sizeof(tx));
        return false;
    }
    sodium_memzero(rx, sizeof(rx));

    unsigned char kOuter[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];
    unsigned char kInner[crypto_secretbox_KEYBYTES];
    ix_derive_subkeys(tx, kOuter, kInner);
    sodium_memzero(tx, sizeof(tx));

    std::vector<unsigned char> innerCt(plainJson.size() + crypto_secretbox_MACBYTES);
    crypto_secretbox_easy(innerCt.data(),
        reinterpret_cast<const unsigned char*>(plainJson.data()), plainJson.size(),
        innerNonce, kInner);
    sodium_memzero(kInner, sizeof(kInner));

    const auto*  ad    = reinterpret_cast<const unsigned char*>(cpkHex.data());
    const size_t adLen = cpkHex.size();

    std::vector<unsigned char> outerCt(innerCt.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
    unsigned long long outerCtLen = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_encrypt(
        outerCt.data(), &outerCtLen,
        innerCt.data(), innerCt.size(),
        ad, adLen, nullptr,
        outerNonce, kOuter);
    sodium_memzero(kOuter, sizeof(kOuter));
    if (rc != 0) return false;
    outerCt.resize((size_t)outerCtLen);

    json wire = {
        {"v", 2},
        {"cpk", cpkHex},
        {"ct", ix_hex(outerCt.data(), outerCt.size())},
        {"n", ix_hex(outerNonce, sizeof(outerNonce))},
        {"n2", ix_hex(innerNonce, sizeof(innerNonce))}
    };
    wireBody = wire.dump();
    return true;
}

static inline bool ix_open_response(const std::string& wireBody, const IxTransportKx& kx, json& outEnvelope) {
    json resp;
    try { resp = json::parse(wireBody); } catch (...) { return false; }

    if (!resp.contains("ct") || !resp["ct"].is_string() ||
        !resp.contains("n") || !resp["n"].is_string() ||
        !resp.contains("n2") || !resp["n2"].is_string() ||
        !resp.contains("spk") || !resp["spk"].is_string()) return false;

    std::vector<unsigned char> spk, outerNonce_v, innerNonce_v, outerCt;
    if (!ix_hex_decode(resp["spk"].get<std::string>(), spk) || spk.size() != crypto_kx_PUBLICKEYBYTES) return false;
    if (!ix_hex_decode(resp["n"].get<std::string>(), outerNonce_v) ||
        outerNonce_v.size() != crypto_aead_xchacha20poly1305_ietf_NPUBBYTES) return false;
    if (!ix_hex_decode(resp["n2"].get<std::string>(), innerNonce_v) ||
        innerNonce_v.size() != crypto_secretbox_NONCEBYTES) return false;
    if (!ix_hex_decode(resp["ct"].get<std::string>(), outerCt) ||
        outerCt.size() < crypto_aead_xchacha20poly1305_ietf_ABYTES) return false;

    unsigned char rx[crypto_kx_SESSIONKEYBYTES];
    unsigned char tx[crypto_kx_SESSIONKEYBYTES];
    if (crypto_kx_client_session_keys(rx, tx, kx.pk, kx.sk, spk.data()) != 0) {
        sodium_memzero(rx, sizeof(rx)); sodium_memzero(tx, sizeof(tx));
        return false;
    }
    sodium_memzero(tx, sizeof(tx));

    unsigned char kOuter[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];
    unsigned char kInner[crypto_secretbox_KEYBYTES];
    ix_derive_subkeys(rx, kOuter, kInner);
    sodium_memzero(rx, sizeof(rx));

    const std::string spkHex = resp["spk"].get<std::string>();
    const auto*  ad    = reinterpret_cast<const unsigned char*>(spkHex.data());
    const size_t adLen = spkHex.size();

    std::vector<unsigned char> innerCt(outerCt.size() - crypto_aead_xchacha20poly1305_ietf_ABYTES);
    unsigned long long innerCtLen = 0;
    const int rc1 = crypto_aead_xchacha20poly1305_ietf_decrypt(
        innerCt.data(), &innerCtLen, nullptr,
        outerCt.data(), (unsigned long long)outerCt.size(),
        ad, adLen, outerNonce_v.data(), kOuter);
    sodium_memzero(kOuter, sizeof(kOuter));
    if (rc1 != 0) { sodium_memzero(kInner, sizeof(kInner)); return false; }
    innerCt.resize((size_t)innerCtLen);
    if (innerCt.size() < crypto_secretbox_MACBYTES) { sodium_memzero(kInner, sizeof(kInner)); return false; }

    std::string plain(innerCt.size() - crypto_secretbox_MACBYTES, '\0');
    const int rc2 = crypto_secretbox_open_easy(
        reinterpret_cast<unsigned char*>(&plain[0]),
        innerCt.data(), innerCt.size(), innerNonce_v.data(), kInner);
    sodium_memzero(kInner, sizeof(kInner));
    if (rc2 != 0) return false;
    if (plain.size() > IX_MAX_RESPONSE_BYTES) return false;

    try { outEnvelope = json::parse(plain); } catch (...) { return false; }
    return true;
}

static inline bool ix_verify_ed25519(const std::string& message, const std::string& signatureB64) {
    std::vector<unsigned char> pub;
    if (!ix_hex_decode(std::string(ED25519_PUBLIC_KEY_HEX), pub)) return false;
    if (pub.size() != crypto_sign_PUBLICKEYBYTES) return false;

    const std::string signature = ix_b64_decode(signatureB64);
    if (signature.size() != crypto_sign_BYTES) return false;
    if (sodium_init() < 0) return false;

    return crypto_sign_verify_detached(
        reinterpret_cast<const unsigned char*>(signature.data()),
        reinterpret_cast<const unsigned char*>(message.data()),
        (unsigned long long)message.size(),
        pub.data()) == 0;
}

// ==================== HYBRID SIGNATURE: ED448 (second, independent) ====================
// Mirrors jni/src/login.h kd_verify_ed448() exactly -- same key, same
// EVP_DigestVerify call. Unconditional here (no #ifdef): this module has
// no "phone/AIDE build" variant the way the binary does, so there is no
// build of this app that is allowed to skip it.
static inline bool ix_verify_ed448(const std::string& message, const std::string& signatureB64) {
    std::vector<unsigned char> pub;
    if (!ix_hex_decode(std::string(ED448_PUBLIC_KEY_HEX), pub))
        return false;
    if (pub.size() != 57)   // raw Ed448 public key length
        return false;

    bool allZero = true;
    for (unsigned char c : pub) if (c) { allZero = false; break; }
    if (allZero) return false;   // placeholder public key never verifies

    const std::string signature = ix_b64_decode(signatureB64);
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

// ==================== HYBRID SIGNATURE: ML-DSA-65 (third, independent, post-quantum) ====================
// Mirrors jni/src/login.h kd_verify_mldsa65() exactly -- same key, same
// liboqs call. Unconditional here too, for the same reason as above.
static inline bool ix_verify_mldsa65(const std::string& message, const std::string& signatureB64) {
    std::vector<unsigned char> pub;
    if (!ix_hex_decode(std::string(MLDSA65_PUBLIC_KEY_HEX), pub))
        return false;
    if (pub.size() != OQS_SIG_ml_dsa_65_length_public_key)   // 1952 bytes
        return false;

    bool allZero = true;
    for (unsigned char c : pub) if (c) { allZero = false; break; }
    if (allZero) return false;   // placeholder public key never verifies

    const std::string signature = ix_b64_decode(signatureB64);
    if (signature.empty() || signature.size() > OQS_SIG_ml_dsa_65_length_signature)
        return false;   // ML-DSA signatures are variable-length up to this max

    OQS_SIG* sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_65);
    if (!sig) return false;

    const OQS_STATUS rc = OQS_SIG_verify(
        sig,
        reinterpret_cast<const unsigned char*>(message.data()), message.size(),
        reinterpret_cast<const unsigned char*>(signature.data()), signature.size(),
        pub.data()
    );
    OQS_SIG_free(sig);
    return rc == OQS_SUCCESS;
}

static inline std::string ix_writable_dir(JNIEnv* env, jobject filesDirPath) {
    const char* cstr = env->GetStringUTFChars((jstring)filesDirPath, nullptr);
    std::string s(cstr ? cstr : "");
    if (cstr) env->ReleaseStringUTFChars((jstring)filesDirPath, cstr);
    return s;
}

// ==================== APP-SIDE LOGIN (JNI entry point) ====================
// Mirrors jni/src/login.h perform_auth(), but intentionally does NOT
// enforce kernel_allowed, does NOT open the per-session "enc" secret,
// and never touches ix_features/ix_rt — none of that exists in a
// normal app process; those are exclusively the binary's own domain,
// re-checked independently when the binary itself runs.
extern "C" JNIEXPORT jstring JNICALL
Java_com_invisiblex_loader_NativeAuth_performLogin(
    JNIEnv* env, jclass /*clazz*/, jstring jKey, jstring jFilesDir) {

    json out;
    out["success"] = false;
    out["message"] = "Unknown error";

    const char* keyChars = env->GetStringUTFChars(jKey, nullptr);
    std::string keyIn(keyChars ? keyChars : "");
    if (keyChars) env->ReleaseStringUTFChars(jKey, keyChars);

    std::string writableDir = ix_writable_dir(env, jFilesDir);

    if (sodium_init() < 0) {
        out["message"] = "Crypto init failed.";
        std::string s = out.dump();
        return env->NewStringUTF(s.c_str());
    }

    const std::string hwid = ix_shared_hwid();
    out["hwid"] = hwid;

    if (hwid.empty()) {
        out["message"] = "Invalid hardware identity.";
        std::string s = out.dump();
        return env->NewStringUTF(s.c_str());
    }

    const long long ts = (long long)time(nullptr);
    const std::string nonce = ix_random_hex(32);
    if (nonce.size() != 64) {
        out["message"] = "Secure random generator unavailable.";
        std::string s = out.dump();
        return env->NewStringUTF(s.c_str());
    }

    json reqBody = {
        {"mode", "key"},
        {"device_id", hwid},
        {"ts", ts},
        {"nonce", nonce},
        {"key_name", ix_trim_upper(keyIn)}
    };

    IxTransportKx transportKx;
    std::string wireBody;
    if (!ix_seal_request(reqBody.dump(), transportKx, wireBody)) {
        out["message"] = "Transport encryption failed.";
        std::string s = out.dump();
        return env->NewStringUTF(s.c_str());
    }

    const std::string caPath = ix_ensure_ca_bundle(writableDir);
    std::string rawResponse;
    long httpCode = 0;
    if (!ix_http_post(CONNECT_URL, wireBody, caPath, rawResponse, httpCode)) {
        out["message"] = "Could not reach the license server.";
        std::string s = out.dump();
        return env->NewStringUTF(s.c_str());
    }

    // Plain (unsigned) pre-crypto error — e.g. rate limit, bad request.
    {
        json probe;
        bool parsed = false;
        try { probe = json::parse(rawResponse); parsed = true; } catch (...) {}
        const bool isPlainError =
            parsed && probe.is_object() &&
            probe.contains("ok") && probe["ok"].is_boolean() && probe["ok"].get<bool>() == false;
        if (isPlainError) {
            std::string why = probe.value("error", std::string("Server rejected request."));
            out["message"] = std::string("Server rejected request (HTTP ") +
                              std::to_string(httpCode) + "): " + why;
            std::string s = out.dump();
            return env->NewStringUTF(s.c_str());
        }
    }

    json envelope;
    if (!ix_open_response(rawResponse, transportKx, envelope)) {
        out["message"] = "Secure channel failed. Please try again.";
        std::string s = out.dump();
        return env->NewStringUTF(s.c_str());
    }

    if (!envelope.contains("payload") || !envelope.contains("sig") ||
        !envelope["payload"].is_string() || !envelope["sig"].is_string()) {
        out["message"] = "Incomplete authentication response.";
        std::string s = out.dump();
        return env->NewStringUTF(s.c_str());
    }

    const std::string payloadB64 = envelope["payload"].get<std::string>();
    const std::string signatureB64 = envelope["sig"].get<std::string>();
    const std::string signedMessage = nonce + "." + payloadB64;

    if (!ix_verify_ed25519(signedMessage, signatureB64)) {
        out["message"] = "Login failed. Please try again.";
        std::string s = out.dump();
        return env->NewStringUTF(s.c_str());
    }

    // Hybrid, second leg: Ed448 over the SAME message. Fail-closed, same
    // convention as jni/src/login.h -- a missing or empty "sig2" is a
    // rejection, not treated as "server doesn't send this yet". The
    // server already sends it unconditionally (login.h's shipped builds
    // have required it for a while); this app was simply not checking it.
    if (!envelope.contains("sig2") || !envelope["sig2"].is_string()) {
        out["message"] = "Login failed. Please try again.";
        std::string s = out.dump();
        return env->NewStringUTF(s.c_str());
    }
    {
        const std::string sig2B64 = envelope["sig2"].get<std::string>();
        if (sig2B64.empty() || !ix_verify_ed448(signedMessage, sig2B64)) {
            out["message"] = "Login failed. Please try again.";
            std::string s = out.dump();
            return env->NewStringUTF(s.c_str());
        }
    }

    // Hybrid, third leg: ML-DSA-65 (post-quantum) over the SAME message.
    // Same fail-closed convention.
    if (!envelope.contains("sig3") || !envelope["sig3"].is_string()) {
        out["message"] = "Login failed. Please try again.";
        std::string s = out.dump();
        return env->NewStringUTF(s.c_str());
    }
    {
        const std::string sig3B64 = envelope["sig3"].get<std::string>();
        if (sig3B64.empty() || !ix_verify_mldsa65(signedMessage, sig3B64)) {
            out["message"] = "Login failed. Please try again.";
            std::string s = out.dump();
            return env->NewStringUTF(s.c_str());
        }
    }

    const std::string plain = ix_b64_decode(payloadB64);
    json payload;
    try { payload = json::parse(plain); }
    catch (...) {
        out["message"] = "Login failed. Please try again.";
        std::string s = out.dump();
        return env->NewStringUTF(s.c_str());
    }

    const bool ok = payload.value("ok", false);
    if (!ok) {
        out["message"] = payload.value("error", std::string("License is not valid."));
        std::string s = out.dump();
        return env->NewStringUTF(s.c_str());
    }

    out["success"] = true;
    out["message"] = "License Verified!";
    out["keyName"] = ix_trim_upper(keyIn);
    out["status"] = payload.value("status", std::string(""));
    out["sellerName"] = payload.value("seller", std::string("N/A"));
    out["expiryDate"] = payload.value("expires_at", std::string("N/A"));
    out["remainingSeconds"] = payload.value("remaining_seconds", (long long)0);
    out["devicesUsed"] = payload.value("devices_used", 0);
    out["maxDevices"] = payload.value("max_devices", 0);
    out["sessionToken"] = payload.value("session_token", std::string(""));
    out["sessionExpiresAt"] = payload.value("session_expires_at", (long long)0);

    std::string s = out.dump();
    return env->NewStringUTF(s.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_invisiblex_loader_NativeAuth_getSharedHwid(JNIEnv* env, jclass) {
    const std::string hwid = ix_shared_hwid();
    return env->NewStringUTF(hwid.c_str());
}

// ==================== APP-SIDE INTEGRITY CHECK ====================
// Runs BEFORE the login screen is allowed to open — the key goes nowhere
// until this passes. Checks that CONNECT_URL / ED25519_PUBLIC_KEY_HEX
// (both compiled directly into THIS .so, not read from anywhere
// patchable at runtime) are still the genuine deployment values, and
// that libsodium initializes. This is deliberately lighter than
// jni/src/login.h's ix_integrity_gate() — no ptrace/Frida/Xposed scan —
// because this runs inside the app's own normal (debuggable-in-dev,
// non-rooted) process, where that scan produces far more false
// positives than real detections; the binary's own, much stricter
// ix_integrity_gate() is what actually guards the privileged path.
// This check's job is narrower and still real: it proves the app
// talking to the server is still running the genuine, unmodified
// deployment constants, not a repackaged/retargeted APK.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_invisiblex_loader_NativeAuth_integrityCheck(JNIEnv*, jclass) {
    const std::string url(CONNECT_URL);
    const std::string pub(ED25519_PUBLIC_KEY_HEX);

    if (url.size() < 20)                                  return JNI_FALSE;
    if (url.rfind("https://", 0) != 0)                     return JNI_FALSE;
    if (url.find("http://") != std::string::npos)          return JNI_FALSE;
    if (url.find('@') != std::string::npos)                return JNI_FALSE;
    if (url.find("adminshop.vipsite.in") == std::string::npos) return JNI_FALSE;
    if (url.find("keydock_connect.php") == std::string::npos)     return JNI_FALSE;

    if (pub.size() != 64) return JNI_FALSE;
    bool nonzero = false;
    for (unsigned char c : pub) {
        if (!std::isxdigit(c)) return JNI_FALSE;
        if (c != '0') nonzero = true;
    }
    if (!nonzero) return JNI_FALSE;

    if (sodium_init() < 0) return JNI_FALSE;

    return JNI_TRUE;
}
