#ifndef LOGIN_H
#define LOGIN_H

// ── WHICH PRODUCT THIS BINARY IS ────────────────────────────────────
// Must equal the Product Code set on this product in the admin panel
// (Products -> Code). The server refuses any key belonging to a
// different product, which is what keeps TESLA Internal and InvisibleX
// External separately sellable.
//
// Change it in one place here, or clear the Product Code in the panel
// to switch the gate off without rebuilding.
#ifndef IX_BUILD_PRODUCT_CODE
#define IX_BUILD_PRODUCT_CODE "TESLA"
#endif
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
         "nonce":"<64 hex>", "build":"<product code>" }

     "build" names the product this binary is, and is matched server-side
     against keydock_cheats.product_code so a key sold for one product
     cannot open another product's build. It rides inside the sealed
     body like every other field, so it is not visible on the wire. A
     server that does not know the field ignores it, and a build that
     does not send it is not gated at all.

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
#include "ix_rt_patches.h"  // game-file patch bytes, same secret as the offsets
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
#define CONNECT_URL      oxorany("https://adminshop.vipsite.in/keydock_connect.php")

#define ED25519_PUBLIC_KEY_HEX \
    oxorany("4e94b747bf4042fdb99c82dae81f41c278d95ad05e0b4763a89d4ec2316a8bb5")

// REPLACE with your deployment's X25519 transport public key (64 hex chars).
// Generate alongside the server config — see keydock_connect.php header.
#define TRANSPORT_SERVER_PK_HEX \
    oxorany("11776d4190bed1f57b9e26b01ffaf2d20dfff70fbce9e8c803d48590bde6f90e")

// ED448_PUBLIC_KEY_HEX — second, independent signature over the SAME
// payload as ED25519_PUBLIC_KEY_HEX (hybrid: both must verify). Raw
// 57-byte Ed448 public key, 114 hex chars. Generated + roundtrip-verified
// (CLI openssl genpkey/pkeyutl sign+verify) this session. Verification
// of this one only compiles in when KD_HAVE_ED448 is defined (CI builds
// only — see Android.mk / build-ed448-lib). On a phone/AIDE build this
// key is simply unused and Ed25519 alone remains the trust anchor.
#define ED448_PUBLIC_KEY_HEX \
    oxorany("a33362a5038b6d0a133adf3cce75ad63350fa8055cee23741883a4c0765848134e7e1ce735e39e164bc7ac17c0a945faf9a8c2cd7f7a15bf00")

// MLDSA65_PUBLIC_KEY_HEX — THIRD, independent signature over the SAME
// payload as ED25519_PUBLIC_KEY_HEX / ED448_PUBLIC_KEY_HEX (hybrid: all
// three must verify). ML-DSA-65 (FIPS 204, NIST-standardized post-quantum
// signature, formerly CRYSTALS-Dilithium level 3) public key is 1952
// bytes raw -> 3904 hex chars. Only compiles in when KD_HAVE_MLDSA is
// defined (CI builds only — see Android.mk / build-mldsa-android-lib).
// Real deployment public key (keypair generated via tools/kd_mldsa_tool,
// self-test sign+verify roundtrip confirmed before this was embedded).
// Corresponding private key lives on the server, outside public_html —
// same convention as the Ed448 key.
#define MLDSA65_PUBLIC_KEY_HEX \
    oxorany("4053bccb4827a70ee8f10e45366335d7ac34dccddafce6c08a14fc84b7333754097dabb9f868eed2bc07296a4e4c43ed90bd94ebcca78bc397dab82c56ea92ab1c454ea4b161260a3aeb9320c2855f049ad82176605de16a8559d0a5d8488325b06d498141640d57b117ff9e51dbdd5b4c9a08267bd7ffe50f7b2e1132b5fc8dd077a70cde3d1eeb9acbc7799fe7c67bc03f258d7df32a7f1da930e9afc45ba00ffcdd019dfa28ce1df2a33756ef01662b3bcb0768daab49bd3ab6a1d3789d10a4adfd190028754ad58b8ee6e554968bb4bbdc8d538c6c8694d464734752981ee04a50861731f3533f424e455d12b0e77601b0d28b342bc964a5d72cf3ea502148fb5421c38af4e9b220c192ce42d6c3736f0b90464358ca8dc2128747a13b17fa39b5f7d838890cd6f32a90bfd4cbdfc465a4f2c1400a1c65eb9930d50e9546dcb1a4f61a8a729b07d3d637285f5b1b5333c740d8d8c20de9d18cc81cba3839150620316374a66928fa3aa661f71fcaa9c5d25aae64a00ae2dbec3242cf0a4b20818db9f5e6976e5f1db7a22388ded29af3bef903c41bc93cbd09c7d8f8a249ef97e4ea1c92d38fc391dba6035f89d4c2f50ec7dbc4e31a759693b5e2719e22edcd8ad41b8f0c110b7ea9b101e10f2ce1452268e49689cc2767ef3df2dc560ea4909ff92701ed1cc81cceca834b0aee276eb1b310205e3be390efc1b8de4456d8041b21a1f00f49032c9bb2e53364bddc78b3a17deb71949e3dd2e3b8fdd1f72d0cafc2c7227ef7b0ac20bce0e522fef398d159806035b0b632706f70afcda2a919424e178db18e91da025ec3cf29d23741acfd6417beaf2ef8b9290115535a28ecbc9d9a16ba8161d3b0933c2ca5a536f2436b8f091813de855cea494cf2f5cb53e943bdabfeb3a7eea82643f604d5f2021a7af9ab5b511f55cefb7d1a042ec0e996157aa0ff47459a835af2611fb3edeb871e82efb739c01d84a1911f309c57f21cc41142bc72a0f1cec1142c9f84b2683fcbac79f746356c8c022fc2d69501149d59ee542a8ce4dbb7836e99af0a3e2e4672eb5024d5da5c8d938017053067736fa547ca440bf7bc600137497e32c549d63e7b7993902a5bbb38f0dd027f4bebba179204e3859cbb1bb45077cf9dd3b7eff00194a8184153aa234f64a6fb8aaf2a36c31c5412e16abfbb77e06f7517b03d5a6c4e1d5792f5564b83ea953e7039cbaf3083d165c759c28aaa65ef7694a49b0a34bf3f6cd056f3bf9dc8aa09381fc72fea88d5e13f1397112b20a7e6c5962a5e46feaba1b1ef5c3190d764acbbd2f83dc20ff732d5f0ab230725b8786c0b64a1ed2335aaa659e538e7b8e6c4d6016b905b924ecc33805addaa3265b5678221a771a084ba59a31c318c9e33d7c7f2e8ca6950a9a2ca6fb7f3dce2e77954ccd313621581fc38246d56c85a262575d3189eace85756ba5c627af76035844ab6819f946ae173261fc39f56dbbc8dba1b5f0ca7294a3519dab68f51588fa8307fa1b7e92db5894e500bb4b3a4e40dbc00eaa92a792ad513b2843d0ed0be325718d8e89f231d95ad645f514e53e7851f80a9bcc13454331c585a04fd70929e565f2281f23e1d98414b3d507e58d6082dd1e17029c8350936790ee6bfd23ebeb01a68f41ff390fe70be90dd270f24c53df3857d8b1b3dec2f08d2c94012f8da2129b212d416ada99e7cda06f1be0ff8e7b5088dde339be7a69a78ae0b16dd9630d3f58c8a92d45d0f7b44634443841cec57664314422083ab839499c8caad18dddb66cbaa04821d6b90e10077961e1e41557be3eff8b468862fe9324db395a96914568014b4d3852bb2c575b04c2a46d424ef56b48720c0e4d09bcae4bbe5002ef635f4d9f5c5631e866f5fd32fbbdae28bfebb04ef343b4d21af946d6387e4e366b908937ca29623be50680876df67719845657d2429db121ac02c7c0de0ae909be6d95a5e58fa3b30826fcbb71624b01a3c797b74ecf8687667a9bff053e8a961a9a27b58a5de1a7ccd67bcd790f23afe3de453213bf48e581c3dcc6829ef4c95b768507fd851ef0a40dbc349ebe9af91a03fd2523d683823667963a5abb947fdb37bad2f5681a13106000d5554aa69785178e59f1ba19cbe2de93f67fe4dc9a3c7c9b5605393503b062ff42b0513ddb4e717e7b299ef20405580d6f6e2e2d6e817b5f2bfa2c49d8eab05330dda3e4dd96ef0d385c7ba3d1fb595baf7143ca6226ebcb4b5286145028eaeae52b3f24c06c40ef920a60b3d4a0bbdbb954e2159cad8b3a3a0fa121a04800866df711ea25ba6922892da5f3ad808840fd77b58f764c69975d193ac44a32e86194f8c0edd212429e03df572d9ef825ca3beaaced798109f6af023d2268d453aef4d8d92037580d07655f5636735fa3467585f5970e658ab481e645d0c9c87558af0f32bdef916aed6f7feb195339af01d2627d72ccb90cafccf6d41678ddb2501f17b180c2e6e931fe37f58786216f6000fab927ef2531f6cb7e87313ba1467299ede427505f0e43b41a90c79e8e8a0b76a92c1f60cde470df51403ecda850e2875adeedd2fc6d53fb5e5b22a53e29c30ed55abfff07e25fe2087da7e3ad28bebffe454429eb7758f91ddd3d3d028a24773817e907f21edb7301662c42351b807e0339cafe6163f1eae890e3b7d09a0377c67e13161655c87917ae837ccc7958222ed12a3d1706217316c3ae74b1fb859a0d386b63b09c8a7ee700f66d93eb0fc11e6ac")

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
// Resolves the ABSOLUTE directory the running binary itself lives in
// (via /proc/self/exe), so a fallback never depends on the shell's
// current working directory at launch time. Without this, a bare "."
// last-resort would save the key under a different real path every
// time the binary is launched from a different folder (e.g. once from
// "~" and once from "/data/local/tmp"), making a saved key silently
// "disappear" even though kd_save_key() succeeded each time - the key
// was always being written, just to a different file each run.
// [FIX — root cause of the collider-stub "/.collider_stub_tmp.so"
// write failing with errno=30/EROFS] On at least one real device,
// readlink("/proc/self/exe") itself fails with ENOENT even though
// /proc/self/maps opens fine for the very same process — so this
// used to silently return "" here, and every caller then built a
// path like "" + "/.collider_stub_tmp.so" = "/.collider_stub_tmp.so",
// i.e. the filesystem ROOT, which is mounted read-only on Android
// (hence EROFS). Every other caller of this (crash.log, run.log,
// the drivers/ lookup) had the exact same silent failure mode, just
// never hit a path where it was reported back loudly. Falls back to
// parsing /proc/self/maps for this binary's own load-segment path
// when the symlink can't be resolved directly.
static inline std::string kd_exe_dir() {
    char buf[1024];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        std::string exe(buf);
        if (exe.size() < 10 || exe.substr(exe.size() - 10) != " (deleted)") {
            size_t slash = exe.find_last_of('/');
            if (slash != std::string::npos) return exe.substr(0, slash);
        }
    }

    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) return "";
    char line[1024];
    std::string found;
    while (fgets(line, sizeof(line), maps)) {
        char* slashPos = strchr(line, '/');
        if (!slashPos) continue;
        char* nl = strchr(slashPos, '\n');
        if (nl) *nl = '\0';
        if (strstr(slashPos, "(deleted)")) continue;
        found = slashPos;
        break; // first mapped absolute path is this executable's own load segment
    }
    fclose(maps);
    if (found.empty()) return "";
    size_t slash = found.find_last_of('/');
    if (slash == std::string::npos) return "";
    return found.substr(0, slash);
}

static inline std::vector<std::string>& kd_candidate_cache() {
    static std::vector<std::string> v;
    return v;
}

// [NEW] The full candidate list, exposed so the LOAD path can search
// every directory instead of only the one this process happened to pick.
// That is what actually broke key reuse: two binaries, two different
// choices from this list, neither able to see the other's file.
static inline std::vector<std::string> kd_candidate_dirs();

static inline const std::string& kd_writable_dir() {
    static std::string cached;
    if (!cached.empty()) return cached;

    // [FIX] These two candidates were still the scaffold placeholder
    // package name "com.mycompany.application" instead of this app's
    // real package "com.invisiblex.loader" — neither path has ever
    // existed on a real device, so BOTH silently failed on every single
    // run and this always fell through to a lower-priority candidate
    // (typically /data/local/tmp/.kd_store). That's not where the saved
    // key actually "disappeared" from, but it IS a real, stable,
    // SELinux/app-lifecycle-friendly location (cleared only on uninstall,
    // never by a cache cleaner/task killer the way /data/local/tmp can
    // be on some ROMs) that was simply never being used because of the
    // wrong name. Fixed to the real package so this is actually tried
    // first, as originally intended.
    std::vector<std::string> candidateList;

    // [FIX — "automatically key save working nahi hai"] Root cause: the
    // binaries did not agree on where the store is.
    //
    //   inx_login_@api  is built WITHOUT IX_INTERACTIVE_CLI, so it took
    //                   the first app-data candidate further down and
    //                   saved the key there.
    //   inx_standalone  is built WITH it, so the block right below put
    //                   its own exe directory first and it looked for
    //                   the key there.
    //
    // One binary saved, the other looked somewhere else and found
    // nothing, so kd_load_saved_key() returned empty and the key had to
    // be typed again on every run. The save itself was never broken.
    //
    // This candidate goes FIRST for every build so they all land on the
    // same file: the module directory when the module is installed --
    // which is where the login binary already lives -- otherwise a fixed
    // path that does not move when the binary does. The write probe
    // below still applies, so a read-only module mount just falls
    // through to the next candidate.
    if (access("/data/adb/modules/inx_mods", F_OK) == 0)
        candidateList.push_back("/data/adb/modules/inx_mods/.kd_store");
    candidateList.push_back("/data/local/tmp/.kd_store");

#ifdef IX_INTERACTIVE_CLI
    // [FIX — standalone key not saving] The module build's candidate
    // order (below) tries com.invisiblex.loader's app-data folders
    // FIRST because that package is genuinely installed there in the
    // normal module+app setup. The standalone binary has no app
    // installed at all — that path doesn't exist, but as root this
    // binary CAN still mkdir+write it (root can create directories
    // almost anywhere), so the save looked like it worked. What broke
    // it: a data folder for a package that was never actually
    // installed has no real owner in Android's eyes, and nothing
    // guarantees it survives — a reboot, a cache clean, or the system
    // reclaiming orphaned /data/data entries can and does wipe it,
    // which is exactly "the key doesn't save" (it saves, then
    // vanishes before the next run). For this build only, the
    // binary's own directory goes FIRST instead of last: it is the
    // one location guaranteed to exist (the binary just ran from
    // there) and guaranteed stable (nothing cleans up next to a file
    // someone deliberately placed and is still running).
    std::string exeDirFirst = kd_exe_dir();
    if (!exeDirFirst.empty()) candidateList.push_back(exeDirFirst);
#endif

    // [FIX] These two candidates were still the scaffold placeholder
    // package name "com.mycompany.application" instead of this app's
    // real package "com.invisiblex.loader" — neither path has ever
    // existed on a real device, so BOTH silently failed on every single
    // run and this always fell through to a lower-priority candidate
    // (typically /data/local/tmp/.kd_store). That's not where the saved
    // key actually "disappeared" from, but it IS a real, stable,
    // SELinux/app-lifecycle-friendly location (cleared only on uninstall,
    // never by a cache cleaner/task killer the way /data/local/tmp can
    // be on some ROMs) that was simply never being used because of the
    // wrong name. Fixed to the real package so this is actually tried
    // first, as originally intended.
    candidateList.push_back("/data/data/com.invisiblex.loader/files");
    candidateList.push_back("/sdcard/Android/data/com.invisiblex.loader/files");

    // $HOME is set and stable under Termux (and most non-rooted test
    // environments) regardless of which directory the binary is
    // launched from - tried before /data/local/tmp (which is often
    // not writable/chmod-able without root) and before falling back
    // to the binary's own directory.
    const char* home = getenv("HOME");
    if (home && home[0] != '\0') {
        candidateList.push_back(std::string(home) + "/.kd_store");
    }

    candidateList.push_back("/data/local/tmp/.kd_store");
    candidateList.push_back("/data/local/tmp");

    // Last resort (module build only — the standalone build already put
    // this first, above): the binary's OWN directory (absolute, stable
    // across launches) instead of a bare "." (relative to launch-time
    // cwd, which is NOT stable across launches).
    std::string exeDir = kd_exe_dir();
    if (!exeDir.empty()) candidateList.push_back(exeDir);
    candidateList.push_back(".");

    // Remembered whole, so kd_candidate_dirs() can hand the same list to
    // the load path without duplicating any of the logic above.
    kd_candidate_cache() = candidateList;

    for (const std::string& dir : candidateList) {
        mkdir(dir.c_str(), 0700);

        std::string probe = dir + "/.kd_write_test";
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

static inline std::vector<std::string> kd_candidate_dirs() {
    (void)kd_writable_dir();              // fills the cache on first call
    std::vector<std::string> out = kd_candidate_cache();
    const std::string& chosen = kd_writable_dir();
    bool have = false;
    for (const auto& d : out) if (d == chosen) { have = true; break; }
    if (!have) out.push_back(chosen);
    return out;
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
// A file this process is willing to trust with key material: a real file
// (not a symlink or a fifo swapped in underneath us), owned by us or by
// root, and readable by nobody else. A key file that fails this was either
// planted by another process or left world-readable, and is treated as if
// it were not there -- which costs one re-login and never loads an
// attacker-controlled blob.
static inline bool kd_file_is_private(const std::string& path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0)        return false;
    if (!S_ISREG(st.st_mode))                 return false;
    if (st.st_uid != geteuid() && st.st_uid != 0) return false;
    if (st.st_mode & (S_IRWXG | S_IRWXO))     return false;
    return true;
}

// ==================== KEY-AT-REST: SECOND FACTOR ====================
// The stored master key file alone used to BE the at-rest key, and it sits
// in the same directory, with the same 0600, as the encrypted key it
// protects -- so whoever could read one could read the other, and the
// encryption bought nothing against the very threat it was written for (a
// fallback path like /data/local/tmp, which login.h already documents as
// world-traversable). Copying both files to another device worked too.
//
// The real at-rest key is now BLAKE2b(key = file key, msg = ctx || hwid):
// the file is one factor, this device's identity the other. The file alone
// is useless, and the pair carried to another device decrypts nothing.
// Same device, same files -> same key, so nothing changes for the user.
static inline void kd_mix_master_key(unsigned char io[crypto_secretbox_KEYBYTES]) {
    const std::string hwid = kd_get_hwid();
    if (hwid.size() < 8) return;   // no second factor available: file key alone
    static const char ctx[] = "kd-keyatrest-v2";
    std::string msg(ctx, sizeof(ctx) - 1);
    msg += hwid;
    unsigned char mixed[crypto_secretbox_KEYBYTES];
    crypto_generichash(mixed, sizeof(mixed),
                       reinterpret_cast<const unsigned char*>(msg.data()), msg.size(),
                       io, crypto_secretbox_KEYBYTES);
    memcpy(io, mixed, sizeof(mixed));
    sodium_memzero(mixed, sizeof(mixed));
}

// Reads the master key out of one specific directory. No creation, no
// overwrite -- used when hunting for a key another binary saved.
static inline bool kd_read_master_key_in(const std::string& dir,
                                         unsigned char out[crypto_secretbox_KEYBYTES]) {
    const std::string path = dir + "/" + MASTER_KEY_FILE_NAME;
    // Deliberately NOT gated on kd_file_is_private() here. That check
    // exists to refuse a planted file, but on the load path a refusal
    // used to mean "generate a new master key and overwrite this one",
    // which destroys every key already saved under it. Reading our own
    // 0600 file in our own store is the lesser risk by a wide margin.
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) return false;
    in.read(reinterpret_cast<char*>(out), crypto_secretbox_KEYBYTES);
    const bool ok = (in.gcount() == (std::streamsize)crypto_secretbox_KEYBYTES);
    in.close();
    return ok;
}

static inline bool kd_load_or_create_master_key(unsigned char out[crypto_secretbox_KEYBYTES]) {
    if (sodium_init() < 0) return false;
    std::string path = kd_writable_dir() + "/" + MASTER_KEY_FILE_NAME;

    if (kd_read_master_key_in(kd_writable_dir(), out)) return true;

    // [FIX] This is the step that made the symptom permanent. The old
    // code fell through to here whenever the existing file failed the
    // private-file check or came up short, generated a FRESH random key
    // and wrote it over the top -- so every key saved under the previous
    // master key became undecryptable, silently, with no way back. A
    // file that exists is now left exactly where it is.
    {
        struct stat st{};
        if (lstat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
            st.st_size == (off_t)crypto_secretbox_KEYBYTES) {
            return false;   // present but unreadable right now -- do not clobber
        }
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
    kd_mix_master_key(mk);          // file key + this device, never the file alone

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

// Tries to open one blob with one master key, mixed and unmixed.
// Returns the key, or "" when neither works.
// ====================================================================
//  SIMPLE KEY STORE  —  /data/local/tmp/.inx_key
// ====================================================================
// The encrypted store below works, but it only works when three separate
// things line up: the same directory in every binary, a readable master
// key file, and the same HWID. Any one of those going sideways loses the
// saved login, and it has.
//
// This is the plain version, and it is tried FIRST: one fixed path, no
// device binding, no master key, nothing to get out of step. One valid
// login writes the key here; the next run reads it back and tries to log
// in with it. If the server says no, the key is simply asked for again.
//
// The file is 0600 and written by whoever is running the loader (root in
// practice). It holds the licence key as text -- that is the trade for
// having it actually survive, and it is the behaviour asked for.
static inline const char* kd_simple_key_path() {
    return "/data/local/tmp/.inx_key";
}

static inline bool kd_simple_save_key(const std::string& key) {
    if (key.empty()) return false;
    const char* path = kd_simple_key_path();
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    const ssize_t w = write(fd, key.data(), key.size());
    close(fd);
    ::chmod(path, 0600);
    return w == (ssize_t)key.size();
}

static inline std::string kd_simple_load_key() {
    const int fd = open(kd_simple_key_path(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return "";
    char buf[256];
    const ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return "";
    buf[n] = '\0';
    return kd_trim_upper(std::string(buf));
}

static inline std::string kd_try_open_blob(const std::string& raw,
                                           const unsigned char mkFile[crypto_secretbox_KEYBYTES],
                                           bool* wasLegacy) {
    if (wasLegacy) *wasLegacy = false;
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
    memcpy(mk, mkFile, crypto_secretbox_KEYBYTES);
    kd_mix_master_key(mk);

    std::vector<unsigned char> plain(cipher.size() - crypto_secretbox_MACBYTES);
    int rc = crypto_secretbox_open_easy(
        plain.data(), cipher.data(), cipher.size(), nonce_v.data(), mk);
    sodium_memzero(mk, sizeof(mk));

    // Blob written before the device factor existed, or written on a run
    // whose HWID came out differently: open it with the file key alone.
    if (rc != 0) {
        rc = crypto_secretbox_open_easy(
            plain.data(), cipher.data(), cipher.size(), nonce_v.data(), mkFile);
        if (rc == 0 && wasLegacy) *wasLegacy = true;
    }
    if (rc != 0) return "";

    std::string k(reinterpret_cast<char*>(plain.data()), plain.size());
    sodium_memzero(plain.data(), plain.size());
    return kd_trim_upper(k);
}

static inline std::string kd_load_saved_key() {
    if (sodium_init() < 0) return "";

    // [FIX — "save key still not working"] This used to look in exactly
    // one directory: whichever kd_writable_dir() picked for THIS process.
    // inx_login_@api and the loader are built with different flags, so
    // they pick differently, and the key one of them saved was invisible
    // to the other. Every candidate is searched now, with that directory's
    // own master key, so wherever the key was written it is found.
    const std::vector<std::string> dirs = kd_candidate_dirs();
    const std::string primary = kd_writable_dir();

    for (const std::string& dir : dirs) {
        const std::string path = dir + "/" + KEY_FILE_NAME;

        std::ifstream f(path);
        if (!f.good()) continue;
        std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();
        if (raw.empty()) continue;

        unsigned char mkFile[crypto_secretbox_KEYBYTES];
        if (!kd_read_master_key_in(dir, mkFile)) {
            // No master key beside the blob: the pair has to live
            // together, so try the primary store's key before moving on.
            if (dir == primary || !kd_read_master_key_in(primary, mkFile)) continue;
        }

        bool wasLegacy = false;
        const std::string out = kd_try_open_blob(raw, mkFile, &wasLegacy);
        sodium_memzero(mkFile, sizeof(mkFile));
        if (out.empty()) continue;

        // Converge: re-save into the primary store so the next run finds
        // it on the first try, and so a legacy blob gets bound to this
        // device. An upgrade must never cost the user their saved login.
        if (wasLegacy || dir != primary) kd_save_key(out);
        return out;
    }
    return "";
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
//
// [MITM FIX — a planted CA file was accepted as the whole trust store]
// This used to return the on-disk path the moment a file existed there and
// was larger than 1000 bytes:
//
//     if (stat(path, &st) == 0 && st.st_size > 1000) return path;
//
// Nothing checked WHAT was in it, who owned it, or that it had anything to
// do with the bundle compiled into this binary. The embedded bundle is
// 224606 bytes, so "larger than 1000" accepted, for example, a 2 KB PEM
// holding one attacker-generated root — and that file is then handed to
// curl as CURLOPT_CAINFO, i.e. as the COMPLETE list of authorities this
// binary trusts. Plant it once (the fallback store can be a world-
// traversable directory like /data/local/tmp/.kd_store), point the domain
// at a relay — by DNS, a hosts entry, or a proxy — present a cert signed
// by that planted root, and TLS verification passes. The relay then sits
// in the middle of every login.
//
// The request and response bodies are independently sealed to the server's
// X25519 transport key, so even a successful relay reads ciphertext rather
// than licence data, and the certificate pin added in Android.mk now
// refuses the handshake outright. But the foothold itself should never
// have existed, and it is the one link in that chain that does not depend
// on a build flag being set.
//
// So the file is now only used when it IS the embedded bundle: private
// (regular file, owned by us or root, nothing for group or other), the
// exact expected size, and byte-identical by BLAKE2b hash. Anything else
// — wrong hash, wrong size, loose permissions, someone else's file, a
// symlink — is overwritten from the copy inside the binary rather than
// trusted. The rewrite goes to a temp file and is renamed into place, so
// there is no window in which curl could be pointed at a half-written
// bundle.
static inline bool kd_ca_bundle_matches_embedded(const std::string& path) {
    if (!kd_file_is_private(path)) return false;

    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    if ((unsigned long)st.st_size != g_CaBundleSize) return false;

    std::ifstream in(path, std::ios::binary);
    if (!in.good()) return false;
    std::string onDisk((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    in.close();
    if (onDisk.size() != (size_t)g_CaBundleSize) return false;

    unsigned char hDisk[crypto_generichash_BYTES];
    unsigned char hWant[crypto_generichash_BYTES];
    crypto_generichash(hDisk, sizeof(hDisk),
                       reinterpret_cast<const unsigned char*>(onDisk.data()),
                       onDisk.size(), nullptr, 0);
    crypto_generichash(hWant, sizeof(hWant),
                       g_CaBundleData, (size_t)g_CaBundleSize, nullptr, 0);
    return sodium_memcmp(hDisk, hWant, sizeof(hDisk)) == 0;
}

static inline std::string kd_ensure_ca_bundle() {
    if (sodium_init() < 0) return "";

    const std::string path = kd_writable_dir() + "/" + CA_BUNDLE_NAME;
    if (kd_ca_bundle_matches_embedded(path)) return path;

    // Not ours, or not intact. Replace it from the embedded copy.
    const std::string tmp = path + ".tmp";
    {
        KdPrivateUmaskGuard umaskGuard;
        ::unlink(tmp.c_str());                  // never write through a symlink
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.good()) return "";
        out.write(reinterpret_cast<const char*>(g_CaBundleData),
                  (std::streamsize)g_CaBundleSize);
        out.close();
        if (out.fail()) { ::unlink(tmp.c_str()); return ""; }
        chmod(tmp.c_str(), 0600);
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) { ::unlink(tmp.c_str()); return ""; }

    // Re-read and re-verify what actually landed, rather than assuming the
    // write did what it was told.
    if (kd_ca_bundle_matches_embedded(path)) return path;
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
    // [FIX] Bumped from 20s/10s — the engine's login call happens right as
    // the game is launching and the driver is attaching, competing hard for
    // CPU and radio with everything else starting up, and a slow mobile
    // connection could trip the old, tighter timeout well before the
    // request had any real chance to finish. Combined with the one retry
    // in perform_auth() above, this meaningfully cuts false "connection
    // failed" results without materially changing how long a genuinely
    // dead connection takes to give up.
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
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
    //   openssl s_client -connect adminshop.vipsite.in:443 </dev/null 2>/dev/null \
    //     | openssl x509 -pubkey -noout \
    //     | openssl pkey -pubin -outform der \
    //     | openssl dgst -sha256 -binary | openssl base64
    // then build with -DKD_PINNED_PUBKEY='"sha256//<output>"'
#endif
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);

    // [MITM FIX — libcurl honours proxy environment variables by default]
    // With no explicit proxy set, libcurl reads http_proxy / https_proxy /
    // ALL_PROXY out of the environment and routes the request through
    // whatever they name. This binary is launched from a shell (the WebUI
    // execs it, and it can be run by hand), so anyone who can set an
    // environment variable on that launch can put a relay in the middle of
    // every login WITHOUT touching DNS, a hosts file, or routing at all —
    // and it is far less visible than a DNS change. Setting CURLOPT_PROXY
    // to the empty string disables proxying outright, environment
    // variables included; CURLOPT_NOPROXY on "*" is belt and braces for
    // any code path that might still consult one.
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");

    // One more lever an attacker gets for free otherwise: --resolve-style
    // pre-seeded DNS entries and unix-socket redirection both come from
    // options we never set, so leave them alone, but make sure nothing
    // re-points the host behind our back by refusing anything but a fresh
    // resolve of the compiled-in host.
    curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 0L);

    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errBuf);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "AppLicenseClient/4.1");

    // Re-verified on every request, not cached across the process: a
    // verified-once path could be swapped underneath us between logins,
    // which is exactly the window a planted bundle needs. The check is a
    // stat plus one BLAKE2b over 224 KB, which is nothing next to a TLS
    // round trip.
    const std::string caPath = kd_ensure_ca_bundle();
    if (!caPath.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, caPath.c_str());
    } else {
#ifdef KD_PINNED_PUBKEY
        // The embedded bundle could not be written or verified. The device
        // store is the only remaining anchor, and on a rooted phone its
        // contents are attacker-writable — but the certificate pin above
        // is checked IN ADDITION to chain verification, so a cert from any
        // CA in there still has to carry the server's exact public key.
        // Tolerable here, and it keeps a device with awkward storage able
        // to log in at all.
        static const char* sysCaDir = "/system/etc/security/cacerts";
        struct stat sdst;
        if (stat(sysCaDir, &sdst) == 0) {
            curl_easy_setopt(curl, CURLOPT_CAPATH, sysCaDir);
        }
#else
        // No pin compiled in, so CA trust is the ONLY thing standing
        // between this login and a relay. Falling back to a store the
        // device owner (or anything with root) can add a certificate to
        // would hand that away, so fail the request instead. A login that
        // does not happen is recoverable; a login that silently went
        // through an attacker's relay is not.
        curlErr = "no trusted certificate store";
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return false;
#endif
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
    json&                  outEnvelope,
    std::string*           outSpkHex = nullptr   // the transport key this reply used
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
    if (outSpkHex) *outSpkHex = spkHex;
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
    KdTransportKx&     kx,
    std::string*       outSpkHex = nullptr
) {
    if (outSpkHex) outSpkHex->clear();
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

    if (!kd_open_response(rawResponse, kx, outEnvelope, outSpkHex)) {
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
//   URL : https://adminshop.vipsite.in/keydock_connect.php
//   PUB : 4e94b747bf4042fdb99c82dae81f41c278d95ad05e0b4763a89d4ec2316a8bb5
// (PUB/the Ed25519 key is unchanged -- same backend, only the domain moved
// from adminshop.myvipsite.fun to adminshop.vipsite.in -- so only the URL
// hash below was regenerated.)
// SECURITY NOTE: FNV-1a is non-cryptographic. These constants add friction
// against a naïve string patch but are NOT a cryptographic guarantee — the
// Ed25519 signature verification on the server's response is the actual
// security boundary, not this check.
static const uint32_t IX_IC_EXPECTED_URL_FNV = 0xe0979d80u;
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
    const std::string host = "adminshop.vipsite.in";
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
//
// [CRASH FIX - SIGBUS "Bus error", exit 135, empty stdout]
// The watchdog thread used to be detached and endless, with no way to
// stop it. In a SHORT-LIVED process (inx_login_@api, which authenticates
// and exits straight away) that is a teardown race, not a theoretical
// one: ix_integrity_gate() evaluates oxorany(CONNECT_URL) and
// oxorany(ED25519_PUBLIC_KEY_HEX) on every tick, and those read the
// binary's own FILE-BACKED .rodata/.text pages. Once main() returns, the
// runtime starts tearing the process down and those mappings stop being
// valid; a watchdog tick landing in that window touches a dead
// file-backed page, which on bionic is SIGBUS (signal 7 -> exit 135),
// not SIGSEGV. stdout on a pipe is block-buffered, so the JSON line
// printf() had already produced was still sitting in the buffer and died
// with the process - hence "(empty stdout)" next to exit 135.
//
// Two changes close it:
//   1. The loop is now stoppable. It sleeps in 50ms slices and checks a
//      stop flag, so kd_stop_integrity_watchdog() parks it in ~50ms
//      instead of up to 1.5s, and publishes "I am outside the gate" via
//      g_ix_wd_live so the caller can wait for that fact.
//   2. Callers stop it, flush stdout, and leave via _exit() (see
//      inx_login.cpp). _exit() ends every thread at the kernel level with
//      no userspace teardown at all, so there is no window left to race.
//
// This costs no protection. The gate still runs before and during every
// auth, and it still runs on the interval for the whole time the process
// has work to protect - the watchdog is only parked once authentication
// is finished and the process is about to leave. A long-lived process
// (optimum_bar) keeps it running exactly as before.
static std::atomic<int> g_ix_wd_stop{0};
static std::atomic<int> g_ix_wd_live{0};

static inline void kd_integrity_watchdog_loop() {
    using namespace std::chrono_literals;
    g_ix_wd_live.store(1, std::memory_order_release);
    for (;;) {
        // 1500ms total, in 50ms slices, so a stop request is honoured
        // promptly instead of after a full interval.
        for (int i = 0; i < 30; ++i) {
            if (g_ix_wd_stop.load(std::memory_order_acquire)) {
                g_ix_wd_live.store(0, std::memory_order_release);
                return;
            }
            std::this_thread::sleep_for(50ms);
        }
        if (g_ix_wd_stop.load(std::memory_order_acquire)) {
            g_ix_wd_live.store(0, std::memory_order_release);
            return;
        }
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

// Park the watchdog outside the gate before the process tears down.
// Bounded (~400ms) so a wedged thread can never hang the exit path; the
// _exit() that follows in the caller is what makes it unconditional.
static inline void kd_stop_integrity_watchdog() {
    using namespace std::chrono_literals;
    g_ix_wd_stop.store(1, std::memory_order_release);
    for (int i = 0; i < 80 && g_ix_wd_live.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(5ms);
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

// ==================== HYBRID SIGNATURE: ML-DSA-65 (third, independent, post-quantum) ====================
// Only compiled in when the CI build produced a cross-compiled liboqs
// static lib (KD_HAVE_MLDSA, set by Android.mk — see its comment). Not
// available on a phone/AIDE build; Ed25519 (+Ed448 where present) remain
// the trust anchor there. Where it IS available, perform_auth() requires
// kd_verify_ed25519() AND kd_verify_ed448() (if compiled in) AND
// kd_verify_mldsa65() to ALL pass — hybrid AND across three completely
// independent algorithm families (classical EC, classical EC on a
// different curve, and a lattice-based post-quantum scheme). An attacker
// would need to break all three simultaneously; breaking any one alone
// is not enough.
#ifdef KD_HAVE_MLDSA
#include <oqs/oqs.h>

static inline bool kd_verify_mldsa65(
    const std::string& message,
    const std::string& signatureB64
) {
    std::vector<unsigned char> pub;
    if (!kd_hex_decode_any(MLDSA65_PUBLIC_KEY_HEX, pub))
        return false;
    if (pub.size() != OQS_SIG_ml_dsa_65_length_public_key)   // 1952 bytes
        return false;

    bool allZero = true;
    for (unsigned char c : pub) if (c) { allZero = false; break; }
    if (allZero) return false;   // placeholder public key never verifies

    const std::string signature = kd_b64_decode(signatureB64);
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
#endif // KD_HAVE_MLDSA

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
// [TEMP DIAGNOSTIC] Only fires when the KD_AUTH_DEBUG env var is set to
// "1" — never in normal use, and never changes result.message (the
// generic client-facing text is untouched). Prints to stderr only.
static inline void kd_auth_dbg(const char* where) {
    static int on = -1;
    if (on == -1) {
        const char* v = getenv("KD_AUTH_DEBUG");
        on = (v && v[0] == '1') ? 1 : 0;
    }
    if (on) fprintf(stderr, "[KD_AUTH_DEBUG] fail at: %s\n", where);
}

static inline LoginResult perform_auth(
    const std::string& mode,
    const std::string& key,
    const std::string& hwid
) {
    LoginResult result;

    // [DISABLED, see -DIX_ANTITAMPER_OFF in Android.mk] The self-ptrace
    // lock caused the process to kill itself on real devices (TracerPid
    // self-flag race) even after that race was fixed, and it only exists
    // to feed the now-disabled ix_tamper_detected() path anyway — with
    // that detection off, occupying the ptrace slot has no consumer and
    // is pure risk for zero benefit. Left here, commented, for when the
    // detection layer is hardened and re-enabled:
    //
    // {
    //     static bool locked = false;
    //     if (!locked) { ix_self_debug_lock(); locked = true; }
    // }

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
    std::string nonce = kd_random_hex(32);   // not const: the retry below takes a fresh one

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

    // Which product this binary is. Server-side this is matched against
    // keydock_cheats.product_code; a key from another product is refused
    // with "This key is for a different product." Deliberately not added
    // to the bind>=2 echo check below: that check exists to bind a reply
    // to this request, and requiring a server to echo a field it may not
    // know would break against an older panel.
    reqBody["build"] = std::string(oxorany(IX_BUILD_PRODUCT_CODE));

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

    // [FIX — "fake packet capture" false positive] kd_auth_post() returning
    // false means ANY of: DNS/connect failure, a TLS handshake hiccup, a
    // slow mobile connection tripping the timeout, or a genuinely malicious
    // MITM/capture attempt — kd_http_post() cannot tell those apart, they
    // all just come back as "curl failed". The old message here accused
    // the user of running a packet-capture/cracking tool on EVERY one of
    // those cases, including a plain dropped packet on a weak signal —
    // which is exactly the "fake packet capture detection" the user hit
    // intermittently. Two changes:
    //   1. One short, silent retry with a fresh transport keypair on the
    //      FIRST failure only — absorbs a single transient drop (DNS
    //      hiccup, momentary signal loss, cold TLS session) without ever
    //      touching result.message, so a real capture/MITM attempt (which
    //      fails the SAME way on retry, since it can't forge the Ed25519
    //      signature or the transport keys) still ends up denied exactly
    //      as before — this only removes false positives on a device with
    //      a flaky connection, not real detection.
    //   2. The message itself no longer claims a capture was detected at
    //      all — connection failure is not evidence of one — so a genuine
    //      network problem now reads as a network problem.
    std::string wireSpkHex;
    bool posted = kd_auth_post(reqBody, envelope, curlErr, httpCode, transportKx, &wireSpkHex);
    if (!posted) {
        // Two things were wrong with this retry.
        //
        // 1. It used a SEPARATE keypair (retryKx). kd_seal_request() fills
        //    the keypair it is handed, so a successful retry sealed the reply
        //    to retryKx while everything downstream -- above all
        //    kd_open_secret() -- still used the stale transportKx. The
        //    per-session secret could then never open, and the login died
        //    with "Secure channel failed" even though the server had just
        //    approved it. Reusing transportKx keeps one source of truth: it
        //    is refilled in place, so the keypair that sealed the reply is
        //    the keypair that opens it.
        //
        // 2. It reused the nonce. The nonce is the response's anti-replay
        //    binding, so a server that rejects a repeated nonce -- which is
        //    exactly the protection a licence server should have -- would
        //    see the retry as a replay and deny a login that only stumbled
        //    on a flaky connection. A fresh nonce per attempt lets the
        //    server enforce single-use nonces without punishing bad signal.
        const std::string nonceRetry = kd_random_hex(32);
        if (nonceRetry.size() == 64) {
            nonce = nonceRetry;
            reqBody["nonce"] = nonce;
        }
        posted = kd_auth_post(reqBody, envelope, curlErr, httpCode, transportKx, &wireSpkHex);
    }
    if (!posted) {
        // Note: curl's raw reason (curlErr) is intentionally NOT shown.
        // It used to append "SSL certificate problem: ..." which told
        // anyone probing exactly why their request was rejected. (void)
        // marks it deliberately unused.
        (void)curlErr;
        result.message = oxorany("Connection failed — check your internet connection and try again.");
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
        kd_auth_dbg("missing payload/sig fields in envelope");
        result.message = oxorany("Login failed. Please try again.");
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
        kd_auth_dbg("ed25519 verify failed");
        result.message = oxorany("Login failed. Please try again.");
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
        kd_auth_dbg("sig2 (ed448) field missing");
        result.message = oxorany("Login failed. Please try again.");
        return result;
    }
    {
        const std::string sig2B64 = envelope["sig2"].get<std::string>();
        if (sig2B64.empty() || !kd_verify_ed448(signedMessage, sig2B64)) {
            kd_auth_dbg("ed448 verify failed");
            result.message = oxorany("Login failed. Please try again.");
            return result;
        }
    }
#endif

    // Hybrid, third leg: ML-DSA-65 (post-quantum, FIPS 204) over the SAME
    // message. Only enforced in a CI build (KD_HAVE_MLDSA). Same
    // fail-closed convention as sig2 above — a missing/empty/bad "sig3"
    // is rejected outright, never silently skipped.
#ifdef KD_HAVE_MLDSA
    if (!envelope.contains("sig3") || !envelope["sig3"].is_string()) {
        kd_auth_dbg("sig3 (mldsa65) field missing");
        result.message = oxorany("Login failed. Please try again.");
        return result;
    }
    {
        const std::string sig3B64 = envelope["sig3"].get<std::string>();
        if (sig3B64.empty() || !kd_verify_mldsa65(signedMessage, sig3B64)) {
            kd_auth_dbg("mldsa65 verify failed");
            result.message = oxorany("Login failed. Please try again.");
            return result;
        }
    }
#endif

    const std::string plain = kd_b64_decode(payloadB64);

    if (plain.empty() || plain.size() > KD_MAX_RESPONSE_BYTES) {
        kd_auth_dbg("decoded payload empty or too large");
        result.message = oxorany("Login failed. Please try again.");
        return result;
    }

    json payload;

    try {
        payload = json::parse(plain);
    } catch (...) {
        kd_auth_dbg("payload json parse failed");
        result.message = oxorany("Login failed. Please try again.");
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

    // ══════════ RESPONSE BINDING (server opt-in, signed) ══════════
    // What was already proven before this point: the server wrote this
    // payload (Ed25519 + Ed448 + ML-DSA-65, all three enforced in this
    // build) and wrote it for THIS request (the nonce is inside the signed
    // message). What was NOT proven: that the payload belongs to this
    // device, this licence key, and this transport key -- so a payload
    // legitimately signed for one context could in principle be presented
    // in another by whoever sits between the two.
    //
    // The server closes that by echoing, INSIDE the signed payload, the
    // values it received, and switching the check on with "bind": 2. The
    // marker lives inside the signature, so it cannot be stripped or
    // forged -- removing it breaks the signature. Until the server sends
    // it, nothing here changes: an old server keeps working unmodified.
    const long long bindVer = payload.value("bind", (long long)0);
    if (bindVer >= 2) {
        const std::string cpkHex = kd_hex(transportKx.pk, sizeof(transportKx.pk));
        auto mismatched = [&](const char* k, const std::string& mine) -> bool {
            if (!payload.contains(k) || !payload[k].is_string()) return true;  // bind on -> required
            return payload[k].get<std::string>() != mine;
        };
        const bool bad =
            mismatched("nonce",     nonce)      ||   // this request
            mismatched("cpk",       cpkHex)     ||   // this client keypair
            mismatched("tspk",      wireSpkHex) ||   // the key that sealed this reply
            mismatched("device_id", hwid)       ||   // this device
            (mode == "key" && mismatched("key_name", kd_trim_upper(key)));
        if (bad) {
            kd_auth_dbg("response binding mismatch");
            kd_set_kernel_ok(false);
            ix_features_lock();
            ix_rt_clear();
            result.success = false;
            result.message = oxorany("Response does not match this request.");
            return result;
        }
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
        // Patch bytes ride the same decrypted secret as the offsets.
        // Non-fatal by design: a product that sends none is normal, and
        // ix_ff_patch.h then uses its compiled-in tables.
        ix_patch_apply_json(sec);
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

// Checked once per process (env vars don't change mid-run) rather than on
// every hasFeature() call. Explicit opt-in only — unset by default, so
// every normally-distributed binary behaves exactly as before.
static inline bool ix_login_skip_active() {
    static const bool v = []() {
        const char* e = getenv("IX_SKIP_LOGIN");
        return e && *e && strcmp(e, "0") != 0;
    }();
    return v;
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
    // [NEW] Seconds, not days, so the on-screen countdown ticks on the
    // same number the server actually sent instead of a rounded day.
    long long getRemainSeconds() const { return remain_seconds; }
    int getTotalDays()   const { return (int)(remain_seconds / 86400); }
    int getDevicesUsed() const { return devices_used; }
    int getMaxDevices()  const { return devices_max; }
    bool isValid()        const { return authenticated; }
    const std::string& getServerSecret() const { return g_ServerSecret; }
    std::string getSessionToken() const {
        return authenticated ? session_token : "";
    }
    // Feature query from the signed server payload — never a client-side
    // flag, EXCEPT for the one explicit, opt-in escape hatch below: when
    // IX_SKIP_LOGIN is set in the environment (never by default — the
    // person running the binary has to set it themselves, every time),
    // every feature reports unlocked instead of checking the (empty,
    // since no server round-trip happened) signed payload. This exists
    // only for the project owner's own local testing when asked to
    // temporarily remove the login step; it is not a bypass of anyone
    // else's protection, and it is not silently always-on.
    bool hasFeature(const std::string& name) const {
        if (authenticated && ix_login_skip_active()) return true;
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
        hwid = kd_get_hwid();

        if (ix_login_skip_active()) {
            // Explicit opt-in only (IX_SKIP_LOGIN env var) -- see
            // ix_login_skip_active()'s comment. No network round-trip,
            // no key prompt; hasFeature() above also checks this same
            // flag so the rest of the binary actually runs instead of
            // sitting authenticated-but-feature-locked.
            printf("\033[2J\033[H");
            printf("%s[INX] IX_SKIP_LOGIN set -- login step skipped for this run.%s\n\n",
                   COL_YELLOW, COL_RESET);
            fflush(stdout);
            kd_set_kernel_ok(true);
            authenticated  = true;
            license_key    = "IX_SKIP_LOGIN";
            status         = "ACTIVE (login skipped)";
            seller_name    = "N/A";
            expiry_date    = "N/A";
            remain_seconds = 0;
            showActive();
            return true;
        }

        curl_global_init(CURL_GLOBAL_DEFAULT);
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

        // 1) Saved key from a previous successful login.
        // The plain store first -- one fixed path, nothing to get out of
        // step -- then the encrypted one for anything saved before this.
        std::string saved = kd_simple_load_key();
        if (saved.empty()) saved = kd_load_saved_key();
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
                kd_simple_save_key(key);   // the one that has to survive
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

        std::string key = kd_trim_upper(keyNameIn);
        if (key.empty()) {
            key = kd_simple_load_key();
            if (key.empty()) key = kd_load_saved_key();
        }
        LoginResult res;
        if (key.empty()) res.message = oxorany("No license key.");
        else             res = perform_login(key, hwid);

        curl_global_cleanup();

        if (res.success && kd_get_kernel_ok()) {
            applyResult(key, res);
            // Saved whichever way the key arrived: a key that came from
            // the store is worth re-writing, since this is the run that
            // just proved it still works.
            kd_simple_save_key(key);
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
