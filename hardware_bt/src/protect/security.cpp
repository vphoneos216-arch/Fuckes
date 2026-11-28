#include "security.hpp"
#include "game/Offsets.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <random>
#include <openssl/sha.h>

namespace sec {

    // ============================================================================
    // Global Security State
    // ============================================================================
    static std::atomic<bool> g_tampered{false};
    static std::atomic<bool> g_session_authenticated{false};
    static std::atomic<uint64_t> g_security_token{0x1337C0DE5A5A5A5AULL};
    static std::atomic<bool> g_watchdog_started{false};

    // Stored checksums for self-mapped executable memory
    struct MemorySegmentHash {
        uintptr_t start_addr;
        uintptr_t end_addr;
        uint32_t crc32;
    };
    static std::vector<MemorySegmentHash> g_golden_segments;
    static bool g_has_golden_segments = false;

    // ============================================================================
    // CRC32 Implementation (IEEE 802.3 standard)
    // ============================================================================
    uint32_t Security::CalculateCRC32(const void* data, size_t length) {
        if (!data || length == 0) return 0;

        static uint32_t table[256];
        static bool have_table = false;
        if (!have_table) {
            for (uint32_t i = 0; i < 256; i++) {
                uint32_t rem = i;
                for (int j = 0; j < 8; j++) {
                    if (rem & 1) rem = (rem >> 1) ^ 0xEDB88320u;
                    else rem >>= 1;
                }
                table[i] = rem;
            }
            have_table = true;
        }

        const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
        uint32_t crc = ~0u;
        for (size_t i = 0; i < length; i++) {
            crc = (crc >> 8) ^ table[(crc & 0xFF) ^ p[i]];
        }
        return ~crc;
    }

    // ============================================================================
    // SHA-256 Implementation (Using statically linked libcrypto)
    // ============================================================================
    std::string Security::CalculateSHA256(const void* data, size_t length) {
        if (!data || length == 0) return "";

        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        SHA256_Update(&sha256, data, length);
        SHA256_Final(hash, &sha256);

        char hex[SHA256_DIGEST_LENGTH * 2 + 1];
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            snprintf(hex + (i * 2), 3, "%02x", hash[i]);
        }
        hex[SHA256_DIGEST_LENGTH * 2] = '\0';
        return std::string(hex);
    }

    // ============================================================================
    // Self-Memory (.text Section) Integrity Verification
    // Scans ONLY 'r-xp' executable code segments (never writable data)
    // ============================================================================
    bool Security::VerifyCodeIntegrity() {
        std::string maps_path = SEC_STR("/proc/self/maps");
        FILE* fp = fopen(maps_path.c_str(), "r");
        if (!fp) return true; // fail-safe if access restricted

        char line[512];
        std::vector<MemorySegmentHash> current_segments;

        while (fgets(line, sizeof(line), fp)) {
            // ONLY check READ-EXECUTE (.text) code segment of client.sh! NEVER r-w-p (writable data)
            if (strstr(line, " r-xp ") && strstr(line, "client.sh")) {
                uintptr_t start = 0, end = 0;
                if (sscanf(line, "%lx-%lx", &start, &end) == 2 && end > start) {
                    size_t len = end - start;
                    if (len > 0 && len < 0x2000000) { // Limit to reasonable size (32MB)
                        uint32_t crc = CalculateCRC32(reinterpret_cast<const void*>(start), len);
                        current_segments.push_back({start, end, crc});
                    }
                }
            }
        }
        fclose(fp);

        if (current_segments.empty()) {
            return true; // Not yet mapped or file was renamed
        }

        if (!g_has_golden_segments) {
            g_golden_segments = current_segments;
            g_has_golden_segments = true;
            return true;
        }

        if (g_golden_segments.size() != current_segments.size()) {
            return false;
        }

        for (size_t i = 0; i < g_golden_segments.size(); i++) {
            if (g_golden_segments[i].start_addr != current_segments[i].start_addr ||
                g_golden_segments[i].end_addr   != current_segments[i].end_addr ||
                g_golden_segments[i].crc32      != current_segments[i].crc32) {
                return false; // Code was modified in memory!
            }
        }

        return true;
    }

    // ============================================================================
    // Function Prologue Inspection
    // Detects common ARM64 patch sequences like 'mov w0, #1; ret', 'nop', Frida hooks
    // ============================================================================
    bool Security::VerifyFunctionPrologues() {
        const void* funcs[] = {
            reinterpret_cast<const void*>(&Security::VerifyCodeIntegrity),
            reinterpret_cast<const void*>(&Security::CheckDebugger),
            reinterpret_cast<const void*>(&Security::TriggerTamperAction)
        };

        for (const void* func : funcs) {
            if (!func) continue;

            const uint32_t* insn = reinterpret_cast<const uint32_t*>(func);

            // ARM64 Patch signatures:
            // 0x52800020 = mov w0, #1
            // 0x52800000 = mov w0, #0
            // 0xD65F03C0 = ret
            // 0xD503201F = nop
            // 0x58000050 = ldr x16, #8 (Frida trampoline)
            // 0xD61F0200 = br x16 (Frida trampoline)

            if (insn[0] == 0x52800020u && insn[1] == 0xD65F03C0u) {
                return false; // Patched with 'mov w0, #1; ret'
            }
            if (insn[0] == 0x52800000u && insn[1] == 0xD65F03C0u) {
                return false; // Patched with 'mov w0, #0; ret'
            }
            if (insn[0] == 0xD503201Fu && insn[1] == 0xD503201Fu && insn[2] == 0xD503201Fu) {
                return false; // NOP sled detected
            }
            if (insn[0] == 0x58000050u && insn[1] == 0xD61F0200u) {
                return false; // Frida inline hook trampoline detected
            }
        }

        return true;
    }

    // ============================================================================
    // Anti-Debugging Defense
    // Inspects TracerPid and verifies if tracer is an actual debugger process
    // ============================================================================
    bool Security::CheckDebugger() {
        std::string status_path = SEC_STR("/proc/self/status");
        FILE* fp = fopen(status_path.c_str(), "r");
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                if (strncmp(line, "TracerPid:", 10) == 0) {
                    int tracer_pid = atoi(line + 10);
                    fclose(fp);
                    if (tracer_pid > 0) {
                        // Check if the tracer is an actual debugger tool
                        char cmd_path[64];
                        snprintf(cmd_path, sizeof(cmd_path), "/proc/%d/cmdline", tracer_pid);
                        FILE* cfp = fopen(cmd_path, "r");
                        if (cfp) {
                            char cmd[256] = {0};
                            if (fgets(cmd, sizeof(cmd), cfp)) {
                                fclose(cfp);
                                if (strcasestr(cmd, "gdb") || strcasestr(cmd, "lldb") ||
                                    strcasestr(cmd, "frida") || strcasestr(cmd, "strace") ||
                                    strcasestr(cmd, "ida") || strcasestr(cmd, "radare")) {
                                    return true;
                                }
                            } else {
                                fclose(cfp);
                            }
                        }
                    }
                    return false;
                }
            }
            fclose(fp);
        }
        return false;
    }

    // ============================================================================
    // Scan for Injected Malicious Modules (Frida, GDB, LLDB)
    // Does NOT block legitimate root frameworks (Magisk, LSPosed)
    // ============================================================================
    bool Security::CheckInjectedModules() {
        std::string maps_path = SEC_STR("/proc/self/maps");
        FILE* fp = fopen(maps_path.c_str(), "r");
        if (!fp) return false;

        char line[512];
        const char* blacklisted[] = {
            "frida-agent",
            "frida-gadget",
            "libfrida",
            "gdbserver",
            "lldb-server"
        };

        while (fgets(line, sizeof(line), fp)) {
            for (const char* bad : blacklisted) {
                if (strcasestr(line, bad) != nullptr) {
                    fclose(fp);
                    return true; // Malicious module injected!
                }
            }
        }
        fclose(fp);
        return false;
    }

    // ============================================================================
    // Scan for Explicit Proxy Environment Variables
    // ============================================================================
    bool Security::CheckNetworkProxies() {
        const char* proxy_vars[] = {
            "http_proxy", "https_proxy", "ALL_PROXY", "HTTP_PROXY", "HTTPS_PROXY"
        };
        for (const char* v : proxy_vars) {
            const char* val = getenv(v);
            if (val && strlen(val) > 0) return true;
        }
        return false;
    }

    // ============================================================================
    // Tamper Response: Instantly Wipes Offsets to 0, Delayed Crash After 60 Seconds
    // ============================================================================
    void Security::TriggerTamperAction() {
        bool expected = false;
        if (!g_tampered.compare_exchange_strong(expected, true)) {
            return; // Already triggered
        }

        // 1. INSTANTLY nullify all game offsets
        Offsets::Nullify();

        // 2. Corrupt rolling security token
        g_security_token.store(0xDEADDEADDEADDEADULL);

        // 3. Launch stealth delayed crash thread (1 minute delay)
        std::thread([]() {
            // Sleep for 60 seconds
            std::this_thread::sleep_for(std::chrono::seconds(60));

            // Force intentional silent crash / segmentation fault
            volatile int* crash_ptr = reinterpret_cast<volatile int*>(0x0);
            *crash_ptr = 0xDEADBEEF;
            abort();
        }).detach();
    }

    bool Security::IsTampered() {
        return g_tampered.load(std::memory_order_relaxed);
    }

    uint64_t Security::GetSecurityToken() {
        return g_security_token.load(std::memory_order_relaxed);
    }

    // ============================================================================
    // Distributed Check Interface (called across render loop and worker threads)
    // ============================================================================
    bool Security::DistributedCheck(uint32_t source_id) {
        if (g_tampered.load(std::memory_order_relaxed)) {
            Offsets::Nullify();
            return false;
        }

        // Fast checks on worker thread
        if (CheckDebugger() || CheckInjectedModules() || CheckNetworkProxies()) {
            TriggerTamperAction();
            return false;
        }

        if (!VerifyFunctionPrologues()) {
            TriggerTamperAction();
            return false;
        }

        // Update rolling token
        uint64_t curr = g_security_token.load(std::memory_order_relaxed);
        curr = (curr * 6364136223846793005ULL + source_id + 1ULL) ^ 0x5A5A5A5AA5A5A5A5ULL;
        g_security_token.store(curr, std::memory_order_relaxed);

        return true;
    }

    // ============================================================================
    // Session Authentication Binding
    // ============================================================================
    void Security::AuthenticateSession(const std::string& server_token) {
        if (!server_token.empty()) {
            g_session_authenticated.store(true);
            uint32_t token_crc = CalculateCRC32(server_token.data(), server_token.size());
            g_security_token.fetch_xor(static_cast<uint64_t>(token_crc) << 32);
        }
    }

    bool Security::IsSessionAuthenticated() {
        return g_session_authenticated.load(std::memory_order_relaxed);
    }

    // ============================================================================
    // Asynchronous Background Watchdog Thread
    // ============================================================================
    void Security::StartWatchdog() {
        bool expected = false;
        if (!g_watchdog_started.compare_exchange_strong(expected, true)) {
            return;
        }

        std::thread([]() {
            std::mt19937 rng(static_cast<uint32_t>(time(nullptr)));
            std::uniform_int_distribution<int> dist(4, 8);

            // Give initial grace period for app initialization
            std::this_thread::sleep_for(std::chrono::seconds(3));

            while (!g_tampered.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::seconds(dist(rng)));

                // 1. Verify code integrity
                if (!VerifyCodeIntegrity()) {
                    TriggerTamperAction();
                    break;
                }

                // 2. Verify function prologues
                if (!VerifyFunctionPrologues()) {
                    TriggerTamperAction();
                    break;
                }

                // 3. Check debugger
                if (CheckDebugger()) {
                    TriggerTamperAction();
                    break;
                }

                // 4. Check injected modules
                if (CheckInjectedModules()) {
                    TriggerTamperAction();
                    break;
                }

                // 5. Check network proxies
                if (CheckNetworkProxies()) {
                    TriggerTamperAction();
                    break;
                }
            }
        }).detach();
    }

    // ============================================================================
    // Initialization
    // ============================================================================
    void Security::Init() {
        // 1. Set process non-dumpable to prevent /proc/self/mem reads
        prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);

        // 2. Establish initial golden memory checksums of .text
        VerifyCodeIntegrity();

        // 3. Launch watchdog thread for continuous background monitoring
        StartWatchdog();
    }

} // namespace sec
