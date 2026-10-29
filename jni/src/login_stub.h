#pragma once
/**
 * login_stub.h — TEST-ONLY replacement for login.h
 *
 * WHY THIS EXISTS
 * login.h needs libcurl + OpenSSL, and this project only ships those
 * prebuilt for arm64-v8a. Until the armeabi-v7a versions are in place the
 * 32-bit build cannot link, which blocks testing whether the cheat itself
 * works on 32-bit at all.
 *
 * This file provides the same tiny API that main.cpp uses, with no network
 * and no crypto, so you can build and confirm the ESP/aim side works.
 *
 * IT DOES NO LICENCE CHECK. Use it to test, then switch back to login.h
 * once the v7a libs are in.
 *
 * USE:
 *   In main.cpp replace
 *       #include "login.h"
 *   with
 *       #include "login_stub.h"
 */

#include <string>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>

// main.cpp gates the kernel path on this. Normal mode does not use a
// driver, but the variable still has to exist and be true or main() exits.
static bool g_AuthKernelOk = true;

class Authentication {
public:
    bool run() {
        printf("\n");
        printf("========================================\n");
        printf("  InvisibleX  —  32-bit TEST BUILD\n");
        printf("  NO LICENCE CHECK IN THIS BINARY\n");
        printf("========================================\n");
        printf("  HWID   : %s\n", hwid().c_str());
        printf("  Mode   : normal (process_vm_readv)\n");
        printf("  Auth   : stubbed out\n");
        printf("========================================\n\n");
        fflush(stdout);
        g_AuthKernelOk = true;
        return true;
    }

    std::string getLicenseKey()  const { return "TEST-BUILD"; }
    std::string getSellerName()  const { return "local";      }
    std::string getExpiryDate()  const { return "-";          }
    int         getDaysRemain()  const { return 0;            }
    int         getTotalDays()   const { return 0;            }
    int         getDevicesUsed() const { return 1;            }
    int         getMaxDevices()  const { return 1;            }

private:
    /** Same shape as the real HWID so logs stay comparable. */
    static std::string hwid() {
        static std::string cached;
        if (!cached.empty()) return cached;

        const char* path = "/data/local/tmp/.inx_test_hwid";
        {
            std::ifstream f(path);
            if (f.good()) {
                std::getline(f, cached);
                if (cached.size() >= 8) return cached;
            }
        }

        std::string raw = prop("ro.serialno") + "|" + prop("ro.product.brand")
                        + "|" + prop("ro.product.model");
        unsigned long long h = 1469598103934665603ULL;
        for (unsigned char c : raw) { h ^= c; h *= 1099511628211ULL; }

        char buf[32];
        snprintf(buf, sizeof(buf), "TEST-%016llX", h);
        cached = buf;

        std::ofstream o(path, std::ios::trunc);
        if (o.good()) { o << cached; o.close(); ::chmod(path, 0600); }
        return cached;
    }

    static std::string prop(const char* key) {
        std::string cmd = std::string("getprop ") + key;
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) return "";
        char b[128] = {0};
        if (!fgets(b, sizeof(b), p)) { pclose(p); return ""; }
        pclose(p);
        std::string s(b);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        return s;
    }
};
