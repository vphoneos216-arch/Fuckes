#pragma once
#include <cstdio>

// Release is the default. Re-enable diagnostics explicitly with
// -DTOUCHBASE_DIAGNOSTICS=1 when investigating a device issue.
#ifndef TOUCHBASE_DIAGNOSTICS
#define TOUCHBASE_DIAGNOSTICS 0
#endif
#if TOUCHBASE_DIAGNOSTICS
#define TB_DEBUG_PRINTF(...) std::printf(__VA_ARGS__)
#define TB_DEBUG_FLUSH() std::fflush(stdout)
#define TB_ANDROID_LOG(...) __android_log_print(__VA_ARGS__)
#else
#define TB_DEBUG_PRINTF(...) ((void)0)
#define TB_DEBUG_FLUSH() ((void)0)
#define TB_ANDROID_LOG(...) ((void)0)
#endif
