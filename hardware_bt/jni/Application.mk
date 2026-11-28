# ============================================
# Application.mk - সম্পূর্ণ ফিক্সড
# ============================================

APP_ABI := arm64-v8a
APP_PLATFORM := android-21
APP_STL := c++_static

APP_CPPFLAGS := \
    -std=c++17 \
    -fno-rtti \
    -fexceptions \
    -fvisibility=hidden \
    -fvisibility-inlines-hidden

APP_LDFLAGS := \
    -Wl,--gc-sections \
    -Wl,--strip-all \
    -Wl,--build-id=none \
    -Wl,--hash-style=gnu \
    -Wl,--no-undefined \
    -pie \
    -Wl,-z,relro \
    -Wl,-z,now \
    -Wl,-z,noexecstack