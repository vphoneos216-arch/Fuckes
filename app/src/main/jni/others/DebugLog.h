#pragma once
// [NEW] Persistent file-based debug log, requested so issues can be
// diagnosed from a log FILE on-device (no PC/adb needed) instead of only
// logcat. Writes to both logcat (adb users) and a plain text file on
// external storage (readable with any file manager on the phone itself).
//
// Path: /sdcard/gsmodz_debug.log — chosen because it's a fixed, predictable
// location any file manager can open without needing the target app's
// private data dir (which varies and usually isn't world-readable). Needs
// WRITE_EXTERNAL_STORAGE (added to AndroidManifest.xml) on Android versions
// that still enforce it; on newer Android this path may be restricted by
// scoped storage depending on the target app's own permissions — if the
// file never appears, that's the likely reason, not a crash in this code.
//
// Every call flushes immediately so a crash right after a log line still
// leaves that line on disk — critical for exactly the "it crashed, why"
// case this was asked for.

#include <android/log.h>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <pthread.h>

#ifndef DBG_LOG_TAG
#define DBG_LOG_TAG "GSModz"
#endif

#define DBG_LOG_PATH "/sdcard/gsmodz_debug.log"

namespace DebugLog {
    inline pthread_mutex_t& Mutex() {
        static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
        return m;
    }

    inline void Write(const char* level, const char* fmt, va_list args) {
        // Format the message once, reuse for both logcat and file.
        char msg[1024];
        va_list argsCopy;
        va_copy(argsCopy, args);
        vsnprintf(msg, sizeof(msg), fmt, argsCopy);
        va_end(argsCopy);

        __android_log_print(ANDROID_LOG_DEBUG, DBG_LOG_TAG, "%s", msg);

        pthread_mutex_lock(&Mutex());
        FILE* f = fopen(DBG_LOG_PATH, "a");
        if (f) {
            time_t now = time(nullptr);
            char tbuf[32];
            struct tm tmNow{};
            localtime_r(&now, &tmNow);
            strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmNow);
            fprintf(f, "[%s] [%s] %s\n", tbuf, level, msg);
            fclose(f);
        }
        // fopen failing (e.g. storage permission denied on newer Android) is
        // silently swallowed on purpose here — we already wrote to logcat
        // above, and DBG_LOG must never itself crash the hooked process.
        pthread_mutex_unlock(&Mutex());
    }

    inline void Info(const char* fmt, ...) {
        va_list args; va_start(args, fmt);
        Write("INFO", fmt, args);
        va_end(args);
    }
    inline void Error(const char* fmt, ...) {
        va_list args; va_start(args, fmt);
        Write("ERROR", fmt, args);
        va_end(args);
    }
}

#define DBG_INFO(...)  DebugLog::Info(__VA_ARGS__)
#define DBG_ERROR(...) DebugLog::Error(__VA_ARGS__)
