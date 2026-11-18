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
# [UPDATED] confirmed via three real CI round-trips, not guessed:
#   1. LOCAL_MODULE := injector — the APK's unzipped lib/ listing showed it
#      never got packaged (AGP only picks up ndk-build output ending .so).
#   2. LOCAL_MODULE_FILENAME := libinjector.so — NDK rejected it: "should
#      not include file extensions" (CXX1429).
#   3. LOCAL_MODULE := libinjector.so directly (thinking only
#      LOCAL_MODULE_FILENAME was validated) — NDK rejected that too, same
#      CXX1429: it turns out BUILD_EXECUTABLE derives
#      LOCAL_MODULE_FILENAME from LOCAL_MODULE internally either way and
#      validates the derived value, so an executable's output name simply
#      cannot contain a dot through Android.mk at all.
# Given that hard constraint, this stays a plain extensionless executable
# here; getting it into the APK as a .so-named file is handled on the
# Gradle side instead (see app/build.gradle's copyInjectorAsSo task),
# which copies ndk-build's real output and renames the copy — Android.mk
# itself never needs to produce that name.
LOCAL_MODULE            := injector
LOCAL_SRC_FILES         := injector/injector.cpp
LOCAL_CPPFLAGS          := -std=c++17 -Wall
LOCAL_LDLIBS            := -llog -ldl
include $(BUILD_EXECUTABLE)

