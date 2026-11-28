# ============================================
# Application.mk - সম্পূর্ণ ফিক্সড
# ============================================

APP_ABI := arm64-v8a
# Raised from android-21 to android-28: liboqs (ML-DSA-65) and the
# Ed448 OpenSSL 3.x build both need libc symbols that only exist from
# API 28 onward (getentropy, aligned_alloc, stdin/stderr as real
# symbols rather than macros) -- confirmed by this exact link failure
# at android-21 ("undefined symbol: getentropy/aligned_alloc/stdin/
# stderr", all referenced from liboqs.a / libcrypto_kdoss3.a). The
# main InvisibleX build, which links the same two libraries, already
# targets android-28 for the same reason -- this matches it rather
# than introducing a new minimum. Real consequence: this binary now
# requires Android 9 (API 28)+ instead of Android 5 (API 21)+.
APP_PLATFORM := android-28
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