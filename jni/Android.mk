LOCAL_PATH := $(call my-dir)

# =====================================================================
#  PREBUILT STATIC LIBS — KPM drivers 8 & 9 + Paradise + sodium
#  Declared as PREBUILT_STATIC_LIBRARY (not LOCAL_LDLIBS) so the linker
#  places them AFTER the object files that reference them. Putting a .a
#  in LOCAL_LDLIBS puts it too early in the link line and produces
#  "undefined reference" errors.
# =====================================================================
include $(CLEAR_VARS)
LOCAL_MODULE    := sodium
LOCAL_SRC_FILES := include/external/libsodium-android-arm64-v8a/lib/libsodium.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include/external/libsodium-android-arm64-v8a/include
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := kma_kpm_driver
LOCAL_SRC_FILES := src/KMA_KPM/driver.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := ldg_kpm_driver
LOCAL_SRC_FILES := src/LDK_KPM/libLdgDriverStatic.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := paradise_driver
LOCAL_SRC_FILES := src/Paradise/libparadise_api.a
include $(PREBUILT_STATIC_LIBRARY)

# =====================================================================
#  Ed448 crypto — PRIVATE renamed OpenSSL 3.x (kdoss3_ prefixed symbols),
#  built only in CI (see .github/workflows/build.yml's build-ed448-lib
#  job). Only present there, never on a phone/AIDE build, so everything
#  about it is guarded by `wildcard`: a phone build just compiles without
#  Ed448 verification (Ed25519 still fully protects it) rather than
#  failing because the file isn't there.
# =====================================================================
KD_ED448_LIB := $(LOCAL_PATH)/include/external/openssl3-ed448/libcrypto_kdoss3.a
ifneq ($(wildcard $(KD_ED448_LIB)),)
include $(CLEAR_VARS)
LOCAL_MODULE    := openssl3_ed448
LOCAL_SRC_FILES := include/external/openssl3-ed448/libcrypto_kdoss3.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include/external/openssl3-ed448/include
include $(PREBUILT_STATIC_LIBRARY)
endif

# =====================================================================
#  ML-DSA-65 (post-quantum, FIPS 204) — liboqs, built only in CI (see
#  .github/workflows/build.yml's build-mldsa-android-lib job). liboqs'
#  symbols (OQS_*) don't collide with anything else in the project, so
#  unlike Ed448 no symbol-renaming is needed — just link it directly.
#  Same wildcard-guard pattern as Ed448: a phone/AIDE build without this
#  artifact just compiles without ML-DSA-65 (Ed25519+Ed448 still fully
#  protect it) instead of failing.
# =====================================================================
KD_MLDSA_LIB := $(LOCAL_PATH)/include/external/liboqs-android/lib/liboqs.a
ifneq ($(wildcard $(KD_MLDSA_LIB)),)
include $(CLEAR_VARS)
LOCAL_MODULE    := oqs_mldsa
LOCAL_SRC_FILES := include/external/liboqs-android/lib/liboqs.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include/external/liboqs-android/include
include $(PREBUILT_STATIC_LIBRARY)
endif

# =====================================================================
#  MAIN MODULE
# =====================================================================
include $(CLEAR_VARS)

OPENGL_DRAW = 1

LOCAL_MODULE := stranger.sh

# =====================================================================
#  BASE FLAGS
# =====================================================================
# NOTE: this NDK's clang does not recognise the name "c++20" (it only
# knows the draft spelling "c++2a"). Nothing in the compiled sources
# actually needs C++20 — the designated initialisers live in
# VulkanMain.cpp which is not in LOCAL_SRC_FILES, and the "requires"/
# "concept" hits are just English words inside ImGui comments.
# c++17 is the standard this project originally built with.
#
# The -std flag belongs in LOCAL_CPPFLAGS only. LOCAL_CFLAGS is also
# applied to C sources, where a C++ standard name is invalid.
LOCAL_CPPFLAGS := -std=c++17
LOCAL_CFLAGS   :=

LOCAL_CFLAGS   += -DVK_USE_PLATFORM_ANDROID_KHR
LOCAL_CPPFLAGS += -DVK_USE_PLATFORM_ANDROID_KHR

# [DISABLED] Runtime anti-tamper (debugger/Frida/Xposed/emulator detection,
# ix_tamper_detected() inside ix_integrity_gate()) + the self-ptrace lock
# that feeds it. Both were causing the process to SIGKILL itself with zero
# output on real devices — first via a TracerPid race against its own
# locker (fixed), then via a second false-positive in this same detection
# layer (emulator/root-tool/Frida string match firing on a legitimate
# rooted device). Reliability > this extra layer: the actual security
# boundary — XChaCha20/XSalsa20 transport encryption + Ed25519+Ed448
# response signature verification — is completely unaffected by this flag.
# O-MVLL compiler obfuscation also stays fully active. Re-enable later by
# removing this define once the detectors are hardened against false
# positives on real rooted hardware (not just emulators/CI).
LOCAL_CFLAGS   += -DIX_ANTITAMPER_OFF
LOCAL_CPPFLAGS += -DIX_ANTITAMPER_OFF

ifeq ($(OPENGL_DRAW), 1)
    LOCAL_CFLAGS   += -DUSE_OPENGL
    LOCAL_CPPFLAGS += -DUSE_OPENGL
endif

# =====================================================================
#  PROTECTION FLAGS
# =====================================================================

LOCAL_CFLAGS   += -fvisibility=hidden
LOCAL_CPPFLAGS += -fvisibility=hidden
LOCAL_CFLAGS   += -fvisibility-inlines-hidden
LOCAL_CPPFLAGS += -fvisibility-inlines-hidden

LOCAL_CFLAGS   += -g0
LOCAL_CPPFLAGS += -g0

LOCAL_CFLAGS   += -O2
LOCAL_CPPFLAGS += -O2

LOCAL_CFLAGS   += -finline-functions
LOCAL_CPPFLAGS += -finline-functions

LOCAL_CFLAGS   += -funroll-loops
LOCAL_CPPFLAGS += -funroll-loops

LOCAL_CFLAGS   += -fstack-protector-strong
LOCAL_CPPFLAGS += -fstack-protector-strong

LOCAL_CFLAGS   += -fPIE
LOCAL_CPPFLAGS += -fPIE

LOCAL_CFLAGS   += -D_FORTIFY_SOURCE=2
LOCAL_CPPFLAGS += -D_FORTIFY_SOURCE=2

LOCAL_CFLAGS   += -ffunction-sections -fdata-sections
LOCAL_CPPFLAGS += -ffunction-sections -fdata-sections

# --- extra safe hardening ---
# -fomit-frame-pointer: drops the frame pointer -> harder stack-walking /
# function-boundary reconstruction in Ghidra. Safe with C++ exceptions
# (do NOT add -fno-unwind-tables, that would break try/catch).
LOCAL_CFLAGS   += -fomit-frame-pointer
LOCAL_CPPFLAGS += -fomit-frame-pointer

LOCAL_CPPFLAGS += -fexceptions
LOCAL_CPPFLAGS += -w
# Tolerate warning-group names this clang may not know, instead of
# erroring out on them.
LOCAL_CPPFLAGS += -Wno-unknown-warning-option
LOCAL_CPPFLAGS += -Wno-error=format-security
LOCAL_CPPFLAGS += -Wno-error=c++11-narrowing
LOCAL_CPPFLAGS += -Wno-error=deprecated-declarations
LOCAL_CPPFLAGS += -Wno-error=writable-strings
LOCAL_CPPFLAGS += -Wall

ifneq ($(wildcard $(KD_ED448_LIB)),)
LOCAL_CFLAGS   += -DKD_HAVE_ED448=1
LOCAL_CPPFLAGS += -DKD_HAVE_ED448=1
# The rename header must be force-included BEFORE any <openssl/...> header
# anywhere in the translation unit, so every EVP_* call gets rewritten to
# kdoss3_EVP_* and matches the renamed archive. -include does that for
# every source file, with zero per-file #include changes needed.
LOCAL_CPPFLAGS += -include $(LOCAL_PATH)/include/external/openssl3-ed448/kd_ossl_rename.h
endif

ifneq ($(wildcard $(KD_MLDSA_LIB)),)
LOCAL_CFLAGS   += -DKD_HAVE_MLDSA=1
LOCAL_CPPFLAGS += -DKD_HAVE_MLDSA=1
endif

# =====================================================================
#  INCLUDES
# =====================================================================
# Ed448's new OpenSSL headers MUST be found before the old bundled
# openssl-android ones for the SAME <openssl/evp.h> name to resolve to
# the new (Ed448-capable) version, not the 2016 one — clang's -I search
# is first-match-wins, so this has to be the very first include path.
# Safe: login.h is the ONLY file in the whole project that includes
# <openssl/...> directly (grepped and confirmed) — curl's own openssl
# use is inside its prebuilt .a already, unaffected by header search
# order here.
ifneq ($(wildcard $(KD_ED448_LIB)),)
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/external/openssl3-ed448/include $(LOCAL_C_INCLUDES)
endif
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/src
LOCAL_C_INCLUDES += $(LOCAL_PATH)/src/UI
LOCAL_C_INCLUDES += $(LOCAL_PATH)/src/UI/fonts
LOCAL_C_INCLUDES += $(LOCAL_PATH)/src/UI/images
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/Android_vulkan
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/ImGui
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/ImGui/backends
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/external/curl-android-$(TARGET_ARCH_ABI)/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/external/openssl-android-$(TARGET_ARCH_ABI)/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/external/libsodium-android-arm64-v8a/include
# json.hpp resolves via $(LOCAL_PATH)/include (already added above)
# KPM 7: DitPro — no extra headers (uses proKpm already in wanbai.h)
# KPM 8: KMA KPM
LOCAL_C_INCLUDES += $(LOCAL_PATH)/src/KMA_KPM
# KPM 9: LDK KPM (绿豆糕)
LOCAL_C_INCLUDES += $(LOCAL_PATH)/src/LDK_KPM
# Paradise driver API
LOCAL_C_INCLUDES += $(LOCAL_PATH)/src/Paradise

# =====================================================================
#  SOURCE FILES
# =====================================================================
LOCAL_SRC_FILES := \
    src/main.cpp \
    src/wanbai_statics.cpp \
    src/UI/bar.cpp \
    src/Scarecrow/oxorany.cpp \
    src/ImGui/stb_image.cpp \
    src/Android_draw/draw.cpp \
    src/Android_touch/TouchHelperA.cpp \
    src/ImGui/imgui.cpp \
    src/ImGui/imgui_demo.cpp \
    src/ImGui/imgui_draw.cpp \
    src/ImGui/imgui_tables.cpp \
    src/ImGui/imgui_widgets.cpp \
    src/ImGui/backends/imgui_impl_android.cpp \

ifeq ($(OPENGL_DRAW), 1)
    LOCAL_SRC_FILES += src/ImGui/backends/imgui_impl_opengl3.cpp
else
    LOCAL_SRC_FILES += src/ImGui/backends/imgui_impl_vulkan.cpp
    LOCAL_SRC_FILES += src/Android_vulkan/vulkan_wrapper.cpp
    LOCAL_SRC_FILES += src/Android_vulkan/VulkanUtils.cpp
endif

# =====================================================================
#  STATIC LIBS — curl (HTTPS) + OpenSSL (AES/HMAC + TLS backend)
# =====================================================================
LOCAL_LDLIBS += $(LOCAL_PATH)/include/external/curl-android-$(TARGET_ARCH_ABI)/lib/libcurl.a
LOCAL_LDLIBS += $(LOCAL_PATH)/include/external/openssl-android-$(TARGET_ARCH_ABI)/lib/libssl.a
LOCAL_LDLIBS += $(LOCAL_PATH)/include/external/openssl-android-$(TARGET_ARCH_ABI)/lib/libcrypto.a

# KPM 8 (KMA) + KPM 9 (LDK) + Paradise + sodium (+ Ed448's private OpenSSL,
# only when build-ed448-lib produced it — see the wildcard guard above)
LOCAL_STATIC_LIBRARIES := kma_kpm_driver ldg_kpm_driver paradise_driver sodium
ifneq ($(wildcard $(KD_ED448_LIB)),)
LOCAL_STATIC_LIBRARIES += openssl3_ed448
endif
ifneq ($(wildcard $(KD_MLDSA_LIB)),)
LOCAL_STATIC_LIBRARIES += oqs_mldsa
endif

# =====================================================================
#  LINKER FLAGS
# =====================================================================
LOCAL_LDLIBS += -llog -landroid -lEGL -lGLESv3 -lz

LOCAL_LDFLAGS += -s
LOCAL_LDFLAGS += -Wl,--gc-sections
LOCAL_LDFLAGS += -pie
LOCAL_LDFLAGS += -Wl,-z,relro,-z,now
# Hide static libs' (sodium/curl/openssl/KPM) symbols from the dynsym.
LOCAL_LDFLAGS += -Wl,--exclude-libs,ALL

include $(BUILD_EXECUTABLE)

# =====================================================================
#  kd_ctl — compiled control/daemon helper for the WebUI module.
#  Shares login.h's full hybrid-signed (Ed25519+Ed448+ML-DSA-65) auth
#  path with stranger.sh (same server check, same key-at-rest file),
#  but none of the ImGui/Vulkan/KPM-driver code — it only logs in,
#  writes config.json, and starts/stops the main binary. Deliberately
#  a SEPARATE native executable, not a shell script: per the project's
#  own security bar, anything that decides whether the main binary is
#  allowed to run must be compiled and stripped, not readable text.
# =====================================================================
include $(CLEAR_VARS)

LOCAL_MODULE := kd_ctl

LOCAL_CFLAGS   := -std=c17   -O2 -fvisibility=hidden -ffunction-sections -fdata-sections
LOCAL_CPPFLAGS := -std=c++17 -O2 -fvisibility=hidden -ffunction-sections -fdata-sections -fno-rtti -fexceptions

ifneq ($(wildcard $(KD_ED448_LIB)),)
LOCAL_CFLAGS   += -DKD_HAVE_ED448=1
LOCAL_CPPFLAGS += -DKD_HAVE_ED448=1
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/external/openssl3-ed448/include $(LOCAL_C_INCLUDES)
# Same force-include as the main module (see its own comment above) —
# without this, login.h's EVP_* calls resolve to the plain (1.1.0c,
# Ed448-incapable) bundled libcrypto.a symbol names instead of the
# kdoss3_-renamed ones, and the linker can't find them.
LOCAL_CPPFLAGS += -include $(LOCAL_PATH)/include/external/openssl3-ed448/kd_ossl_rename.h
endif
ifneq ($(wildcard $(KD_MLDSA_LIB)),)
LOCAL_CFLAGS   += -DKD_HAVE_MLDSA=1
LOCAL_CPPFLAGS += -DKD_HAVE_MLDSA=1
endif
LOCAL_CFLAGS   += -DIX_ANTITAMPER_OFF
LOCAL_CPPFLAGS += -DIX_ANTITAMPER_OFF

LOCAL_C_INCLUDES += $(LOCAL_PATH)/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/src
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/external/curl-android-$(TARGET_ARCH_ABI)/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/external/openssl-android-$(TARGET_ARCH_ABI)/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/external/libsodium-android-arm64-v8a/include
ifneq ($(wildcard $(KD_MLDSA_LIB)),)
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/external/liboqs-android/include
endif

LOCAL_SRC_FILES := \
    src/kd_ctl.cpp \
    src/Scarecrow/oxorany.cpp

LOCAL_LDLIBS += $(LOCAL_PATH)/include/external/curl-android-$(TARGET_ARCH_ABI)/lib/libcurl.a
LOCAL_LDLIBS += $(LOCAL_PATH)/include/external/openssl-android-$(TARGET_ARCH_ABI)/lib/libssl.a
LOCAL_LDLIBS += $(LOCAL_PATH)/include/external/openssl-android-$(TARGET_ARCH_ABI)/lib/libcrypto.a

LOCAL_STATIC_LIBRARIES := sodium
ifneq ($(wildcard $(KD_ED448_LIB)),)
LOCAL_STATIC_LIBRARIES += openssl3_ed448
endif
ifneq ($(wildcard $(KD_MLDSA_LIB)),)
LOCAL_STATIC_LIBRARIES += oqs_mldsa
endif

LOCAL_LDLIBS += -llog -lz

LOCAL_LDFLAGS += -s
LOCAL_LDFLAGS += -Wl,--gc-sections
LOCAL_LDFLAGS += -pie
LOCAL_LDFLAGS += -Wl,-z,relro,-z,now
LOCAL_LDFLAGS += -Wl,--exclude-libs,ALL

include $(BUILD_EXECUTABLE)
