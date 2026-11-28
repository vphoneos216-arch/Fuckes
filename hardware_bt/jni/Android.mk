# ============================================
# Android.mk - সম্পূর্ণ ফিক্সড
# ============================================

LOCAL_PATH := $(call my-dir)
TOUCHBASE_DIAGNOSTICS ?= 0

# ============================================
# ১. curl prebuilt static library
# ============================================
include $(CLEAR_VARS)
LOCAL_MODULE    := curl
LOCAL_SRC_FILES := ../includes/external/curl-android-$(TARGET_ARCH_ABI)/lib/libcurl.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/../includes/external/curl-android-$(TARGET_ARCH_ABI)/include
include $(PREBUILT_STATIC_LIBRARY)

# ============================================
# ২. ssl prebuilt static library
# ============================================
include $(CLEAR_VARS)
LOCAL_MODULE    := ssl
LOCAL_SRC_FILES := ../includes/external/openssl-android-$(TARGET_ARCH_ABI)/lib/libssl.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/../includes/external/openssl-android-$(TARGET_ARCH_ABI)/include
include $(PREBUILT_STATIC_LIBRARY)

# ============================================
# ৩. crypto prebuilt static library
# ============================================
include $(CLEAR_VARS)
LOCAL_MODULE    := crypto
LOCAL_SRC_FILES := ../includes/external/openssl-android-$(TARGET_ARCH_ABI)/lib/libcrypto.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/../includes/external/openssl-android-$(TARGET_ARCH_ABI)/include
include $(PREBUILT_STATIC_LIBRARY)

# ============================================
# ৪. Main executable (client.sh)
# ============================================
# Paradise public API (arm64-v8a supplied prebuilt)
include $(CLEAR_VARS)
LOCAL_MODULE    := sodium
LOCAL_SRC_FILES := ../includes/external/libsodium-android-arm64-v8a/lib/libsodium.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/../includes/external/libsodium-android-arm64-v8a/include
include $(PREBUILT_STATIC_LIBRARY)

# =====================================================================
#  Ed448 crypto -- PRIVATE renamed OpenSSL 3.x (kdoss3_ prefixed symbols),
#  built only in CI (see .github/workflows/hardware_bt.yml's
#  build-ed448-lib job). Only present there, never on a phone/AIDE build,
#  so everything about it is guarded by `wildcard`: a phone build just
#  compiles without Ed448 verification (Ed25519 still fully protects it)
#  rather than failing because the file isn't there. Ported verbatim
#  from the main InvisibleX build (jni/Android.mk / build.yml).
# =====================================================================
KD_ED448_LIB := $(LOCAL_PATH)/../includes/external/openssl3-ed448/libcrypto_kdoss3.a
ifneq ($(wildcard $(KD_ED448_LIB)),)
include $(CLEAR_VARS)
LOCAL_MODULE    := openssl3_ed448
LOCAL_SRC_FILES := ../includes/external/openssl3-ed448/libcrypto_kdoss3.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/../includes/external/openssl3-ed448/include
include $(PREBUILT_STATIC_LIBRARY)
endif

# =====================================================================
#  ML-DSA-65 (post-quantum, FIPS 204) -- liboqs, built only in CI (see
#  hardware_bt.yml's build-mldsa-android-lib job). Same wildcard-guard
#  pattern as Ed448.
# =====================================================================
KD_MLDSA_LIB := $(LOCAL_PATH)/../includes/external/liboqs-android/lib/liboqs.a
ifneq ($(wildcard $(KD_MLDSA_LIB)),)
include $(CLEAR_VARS)
LOCAL_MODULE    := oqs_mldsa
LOCAL_SRC_FILES := ../includes/external/liboqs-android/lib/liboqs.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/../includes/external/liboqs-android/include
include $(PREBUILT_STATIC_LIBRARY)
endif

include $(CLEAR_VARS)
LOCAL_MODULE := paradise_api
ifeq ($(TOUCHBASE_DIAGNOSTICS),1)
LOCAL_SRC_FILES := ../includes/external/paradise/libparadise_api.a
else
LOCAL_SRC_FILES := ../includes/external/paradise/libparadise_api.release.a
endif
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/../includes/external/paradise
include $(PREBUILT_STATIC_LIBRARY)

# =====================================================================
#  main.cpp -- its OWN static-library module, separate from client.sh's
#  other sources. This is the ONLY file in the project that #includes
#  Login.h (confirmed: grep for "Login.h"/"login.h" across src/includes
#  finds no other hit), and Login.h is the only file that #includes
#  <openssl/...> directly. When Ed448 is present, that translation unit
#  needs the NEW OpenSSL 3.x headers found FIRST in the -I search (so
#  <openssl/evp.h> resolves to the Ed448-capable version) -- but every
#  OTHER file in this project (security.cpp in particular, which calls
#  the OLD bundled OpenSSL 1.1.0c's low-level SHA256_CTX/SHA256_Init
#  API directly) must keep seeing the OLD headers first, or they fail
#  to compile against a newer OpenSSL that doesn't expose the same
#  low-level API the same way. Splitting main.cpp into its own module
#  lets it have a different LOCAL_C_INCLUDES order than everything
#  else, instead of a module-wide override breaking those other files.
# =====================================================================
include $(CLEAR_VARS)
LOCAL_MODULE := main_obj

LOCAL_SRC_FILES := ../src/main.cpp

ifneq ($(wildcard $(KD_ED448_LIB)),)
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../includes/external/openssl3-ed448/include
endif

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/../includes \
    $(LOCAL_PATH)/../includes/external/libsodium-android-arm64-v8a/include \
    $(LOCAL_PATH)/../includes/fonts \
    $(LOCAL_PATH)/../includes/internal \
    $(LOCAL_PATH)/../includes/internal/ImGui \
    $(LOCAL_PATH)/../includes/internal/ImGui/backends \
    $(LOCAL_PATH)/../includes/draw/ImGui \
    $(LOCAL_PATH)/../includes/draw/ImGui/backends \
    $(LOCAL_PATH)/../includes/external/openssl-android-$(TARGET_ARCH_ABI)/include \
    $(LOCAL_PATH)/../src \
    $(LOCAL_PATH)/../src/protect \
    $(LOCAL_PATH)/../src/ui \
    $(LOCAL_PATH)/../src/game

ifneq ($(wildcard $(KD_MLDSA_LIB)),)
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../includes/external/liboqs-android/include
endif

LOCAL_CPPFLAGS := \
    -std=c++17 \
    -DTOUCHBASE_DIAGNOSTICS=$(TOUCHBASE_DIAGNOSTICS) \
    -fno-rtti \
    -fexceptions \
    -fvisibility=hidden \
    -fvisibility-inlines-hidden \
    -Oz \
    -ffunction-sections \
    -fdata-sections \
    -fomit-frame-pointer \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -fno-asynchronous-unwind-tables \
    -fno-unwind-tables \
    -g0 \
    -Wno-error=format-security \
    -fno-color-diagnostics \
    -fmerge-all-constants \
    -fno-ident

ifneq ($(wildcard $(KD_ED448_LIB)),)
LOCAL_CPPFLAGS += -DKD_HAVE_ED448=1
# Force-included BEFORE any <openssl/...> header in THIS translation
# unit (main.cpp / Login.h), so every EVP_* call gets rewritten to
# kdoss3_EVP_* and matches the renamed archive. Scoped to main_obj only
# -- NOT applied to client.sh's other sources (see the block comment
# above this module).
LOCAL_CPPFLAGS += -include $(LOCAL_PATH)/../includes/external/openssl3-ed448/kd_ossl_rename.h
endif

ifneq ($(wildcard $(KD_MLDSA_LIB)),)
LOCAL_CPPFLAGS += -DKD_HAVE_MLDSA=1
endif

include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := client.sh

LOCAL_SRC_FILES := \
    ../src/other/vendor_release_log.cpp \
    ../src/game/data.cpp \
    ../src/protect/oxorany.cpp \
    ../src/protect/security.cpp \
    ../src/ui/menu.cpp \
    ../src/ui/bar.cpp \
    ../src/ui/widgets/widgets.cpp \
    ../src/ui/png/image.cpp \
    ../includes/draw/Android_draw/draw.cpp \
    ../includes/draw/Android_touch/Touch.cpp \
    ../includes/draw/ImGui/imgui.cpp \
    ../includes/draw/ImGui/imgui_draw.cpp \
    ../includes/draw/ImGui/imgui_tables.cpp \
    ../includes/draw/ImGui/imgui_widgets.cpp \
    ../includes/draw/ImGui/backends/imgui_impl_android.cpp \
    ../includes/draw/ImGui/backends/imgui_impl_opengl3.cpp

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../includes \
    $(LOCAL_PATH)/../includes/external/libsodium-android-arm64-v8a/include \
    $(LOCAL_PATH)/../includes/fonts \
    $(LOCAL_PATH)/../includes/internal \
    $(LOCAL_PATH)/../includes/internal/ImGui \
    $(LOCAL_PATH)/../includes/internal/ImGui/backends \
    $(LOCAL_PATH)/../includes/draw/ImGui \
    $(LOCAL_PATH)/../includes/draw/ImGui/backends \
    $(LOCAL_PATH)/../includes/external/openssl-android-$(TARGET_ARCH_ABI)/include \
    $(LOCAL_PATH)/../src \
    $(LOCAL_PATH)/../src/protect \
    $(LOCAL_PATH)/../src/ui \
    $(LOCAL_PATH)/../src/game

ifneq ($(wildcard $(KD_MLDSA_LIB)),)
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../includes/external/liboqs-android/include
endif

LOCAL_CPPFLAGS := \
    -std=c++17 \
    -DTOUCHBASE_DIAGNOSTICS=$(TOUCHBASE_DIAGNOSTICS) \
    -fno-rtti \
    -fexceptions \
    -fvisibility=hidden \
    -fvisibility-inlines-hidden \
    -Oz \
    -ffunction-sections \
    -fdata-sections \
    -fomit-frame-pointer \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -fno-asynchronous-unwind-tables \
    -fno-unwind-tables \
    -g0 \
    -Wno-error=format-security \
    -fno-color-diagnostics \
    -fmerge-all-constants \
    -fno-ident

ifneq ($(wildcard $(KD_MLDSA_LIB)),)
LOCAL_CPPFLAGS += -DKD_HAVE_MLDSA=1
endif

LOCAL_LDFLAGS := \
    -Wl,--gc-sections \
    -Wl,--strip-all \
    -Wl,--exclude-libs,ALL \
    -Wl,--no-export-dynamic \
    -Wl,--build-id=none \
    -Wl,--no-undefined \
    -pie \
    -Wl,-z,relro \
    -Wl,-z,now \
    -Wl,-z,noexecstack

# System libs
LOCAL_LDLIBS := \
    -llog \
    -landroid \
    -lEGL \
    -lGLESv3 \
    -lz

# Static libs (curl, ssl, crypto) + main_obj (main.cpp, built above with
# its own, isolated include order)
LOCAL_STATIC_LIBRARIES := main_obj paradise_api curl ssl crypto sodium

ifneq ($(wildcard $(KD_ED448_LIB)),)
LOCAL_STATIC_LIBRARIES += openssl3_ed448
endif
ifneq ($(wildcard $(KD_MLDSA_LIB)),)
LOCAL_STATIC_LIBRARIES += oqs_mldsa
endif

include $(BUILD_EXECUTABLE)