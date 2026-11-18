LOCAL_PATH := $(call my-dir)
MAIN_LOCAL_PATH := $(call my-dir)

# [UPDATED] libcurl/libssl/libcrypto prebuilt static libs removed — the
# curl/ dir they pointed at isn't in this source tree, and the only call
# site that needed them (ItSRohitOp.h's Login()) was dead code (see the
# note in ItSRohitOp.h) and has been removed too, per explicit request to
# build this "without any login" rather than source the missing .a files.

include $(CLEAR_VARS)

LOCAL_MODULE           := main
#LOCAL_MODULE_FILENAME  := $(TARGET_ARCH_ABI)
LOCAL_CFLAGS           := -Wno-error=format-security -fvisibility=hidden -ffunction-sections -fdata-sections -w
LOCAL_CFLAGS           += -fno-rtti -fno-exceptions -fpermissive
LOCAL_CPPFLAGS         := -Wno-error=format-security -fvisibility=hidden -ffunction-sections -fdata-sections -w -Werror -s -std=c++17
LOCAL_CPPFLAGS         += -Wno-error=c++11-narrowing -fms-extensions -fno-rtti -fno-exceptions -fpermissive
LOCAL_LDFLAGS          += -Wl,--gc-sections,--strip-all, -llog
LOCAL_ARM_MODE         := arm
LOCAL_LDLIBS           := -llog -landroid -lEGL -lGLESv3 -lGLESv2 -lGLESv1_CM -lz

LOCAL_C_INCLUDES       := $(LOCAL_PATH)
# [UPDATED] curl/openssl include paths removed along with the prebuilt
# libs above — also NOTE the original line here was
# `LOCAL_C_INCLUDES := $(LOCAL_PATH)/curl/...` (:=, not +=), which silently
# THREW AWAY the `+= $(LOCAL_PATH)` line right before it — a real pre-
# existing bug, not something this change introduces. Using := once here
# at the top keeps that same reset-then-append structure correct.
LOCAL_C_INCLUDES       += $(LOCAL_PATH)/gsmodz
LOCAL_C_INCLUDES       += $(LOCAL_PATH)/zygisk
LOCAL_C_INCLUDES       += $(LOCAL_PATH)/others
# [UPDATED] was `$(LOCAL_PATH)/imagens` — that top-level dir doesn't exist;
# the real folder is `others/imagens` (confirmed on disk), another
# pre-existing path bug fixed alongside the unity/Unity one below.
LOCAL_C_INCLUDES       += $(LOCAL_PATH)/others/imagens
LOCAL_C_INCLUDES       += $(LOCAL_PATH)/others/unity
LOCAL_C_INCLUDES       += $(LOCAL_PATH)/others/update/xdl/include
LOCAL_C_INCLUDES       += $(LOCAL_PATH)/others/update
LOCAL_C_INCLUDES       += $(LOCAL_PATH)/others/update/xdl
LOCAL_C_INCLUDES       += $(LOCAL_PATH)/others/imgui
LOCAL_C_INCLUDES       += $(LOCAL_PATH)/others/imgui/fonts

FILE_LIST               += $(wildcard $(LOCAL_PATH)/others/imgui/*.c*)
FILE_LIST               += $(wildcard $(LOCAL_PATH)/others/update/*.c*)
FILE_LIST               += $(wildcard $(LOCAL_PATH)/others/update/xdl/*.c*)
FILE_LIST               += $(wildcard $(LOCAL_PATH)/zygisk/*.c*)
FILE_LIST              += $(wildcard $(LOCAL_PATH)/*.c*)
LOCAL_SRC_FILES        := $(FILE_LIST:$(LOCAL_PATH)/%=%)

LOCAL_CFLAGS += -DDEBUG
LOCAL_CPP_FEATURES     := exceptions

include $(BUILD_SHARED_LIBRARY)

# [NEW] root-run ptrace injector, built as its own executable module.
# Deliberately in its own jni/injector/ subdirectory and named explicitly
# (not globbed) so it never gets swept into the "main" module above, which
# wildcards $(LOCAL_PATH)/*.c* — a standalone executable with its own
# main() can't share a module with libmain.so.
include $(CLEAR_VARS)
# [UPDATED] confirmed via two real CI round-trips, not guessed:
#   1. With LOCAL_MODULE := injector, the APK's unzipped lib/ listing
#      showed the binary never got packaged at all — AGP's native-lib
#      merge only picks up ndk-build output files ending in .so.
#   2. Tried LOCAL_MODULE_FILENAME := libinjector.so to rename just the
#      installed file — NDK rejected that outright: "LOCAL_MODULE_FILENAME
#      should not include file extensions" (BUILD_EXECUTABLE explicitly
#      forbids it), confirmed from that build's own CXX1429 error.
# BUILD_EXECUTABLE's default output name is LOCAL_MODULE verbatim, with no
# extension manipulation of its own — so naming the module itself
# "libinjector.so" (the dot is just a character to it) gets exactly that
# filename with no LOCAL_MODULE_FILENAME override needed. It's still the
# same real ELF executable, run via `su -c`, not a shared library — this
# only affects what Gradle's packaging step sees.
LOCAL_MODULE            := libinjector.so
LOCAL_SRC_FILES         := injector/injector.cpp
LOCAL_CPPFLAGS          := -std=c++17 -Wall
LOCAL_LDLIBS            := -llog -ldl
include $(BUILD_EXECUTABLE)

