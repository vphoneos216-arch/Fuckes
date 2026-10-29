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

# =====================================================================
#  INCLUDES
# =====================================================================
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

# KPM 8 (KMA) + KPM 9 (LDK) + Paradise + sodium
LOCAL_STATIC_LIBRARIES := kma_kpm_driver ldg_kpm_driver paradise_driver sodium

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
