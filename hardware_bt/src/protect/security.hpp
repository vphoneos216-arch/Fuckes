#ifndef SECURITY_HPP
#define SECURITY_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <array>
#include <atomic>
#include <vector>

namespace sec {

    // ============================================================================
    // Compile-Time String Encryption (Constexpr XOR)
    // Ensures sensitive strings never appear in plaintext in .rodata
    // ============================================================================
    template <size_t N, uint8_t K>
    class ObfuscatedString {
    private:
        std::array<uint8_t, N> data;
    public:
        constexpr ObfuscatedString(const char(&str)[N]) : data{} {
            for (size_t i = 0; i < N; ++i) {
                data[i] = static_cast<uint8_t>(str[i] ^ (K + (i * 7)));
            }
        }

        std::string decrypt() const {
            std::string res;
            res.resize(N - 1);
            for (size_t i = 0; i < N - 1; ++i) {
                res[i] = static_cast<char>(data[i] ^ (K + (i * 7)));
            }
            return res;
        }

        const char* decrypt_to(char* buf, size_t buf_len) const {
            size_t copy_len = (N - 1 < buf_len - 1) ? N - 1 : buf_len - 1;
            for (size_t i = 0; i < copy_len; ++i) {
                buf[i] = static_cast<char>(data[i] ^ (K + (i * 7)));
            }
            buf[copy_len] = '\0';
            return buf;
        }
    };

    #define SEC_KEY(offset) static_cast<uint8_t>(((__TIME__[7] - '0') * 13 + (__TIME__[6] - '0') * 7 + __LINE__ + (offset)) & 0xFF)
    #define SEC_STR(str) ([]() { \
        constexpr sec::ObfuscatedString<sizeof(str), SEC_KEY(3)> enc(str); \
        return enc.decrypt(); \
    })()

    // ============================================================================
    // Security Engine API
    // ============================================================================
    class Security {
    public:
        // Lifecycle
        static void Init();
        static void StartWatchdog();

        // Integrity Checksums
        static uint32_t CalculateCRC32(const void* data, size_t length);
        static std::string CalculateSHA256(const void* data, size_t length);

        // Core Checks
        static bool VerifyCodeIntegrity();
        static bool VerifyFunctionPrologues();
        static bool CheckDebugger();
        static bool CheckInjectedModules();
        static bool CheckNetworkProxies();

        // Distributed Check Interface (used in render loop, worker threads)
        static bool DistributedCheck(uint32_t source_id);

        // Session Binding (tied to genuine login response)
        static void AuthenticateSession(const std::string& server_token);
        static bool IsSessionAuthenticated();

        // Tamper Response: Wipes offsets immediately, crashes after 60s
        static void TriggerTamperAction();
        static bool IsTampered();

        // Rolling token update
        static uint64_t GetSecurityToken();
    };

} // namespace sec

#endif // SECURITY_HPP
