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

include $(CLEAR_VARS)
LOCAL_MODULE := paradise_api
ifeq ($(TOUCHBASE_DIAGNOSTICS),1)
LOCAL_SRC_FILES := ../includes/external/paradise/libparadise_api.a
else
LOCAL_SRC_FILES := ../includes/external/paradise/libparadise_api.release.a
endif
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/../includes/external/paradise
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := client.sh

LOCAL_SRC_FILES := \
    ../src/main.cpp \
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

# Static libs (curl, ssl, crypto)
LOCAL_STATIC_LIBRARIES := paradise_api curl ssl crypto sodium

include $(BUILD_EXECUTABLE)