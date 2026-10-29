APP_ABI := arm64-v8a
APP_PLATFORM := android-28
APP_STL := c++_static
APP_OPTIM := release

# --- O-MVLL (OLLVM) injection, CI-only ---------------------------------
# On your phone build, OMVLL_PLUGIN is undefined -> nothing changes, the
# binary compiles exactly as before. In the GitHub Actions cloud build we
# pass  OMVLL_PLUGIN=/path/to/omvll-ndk.so  on the ndk-build command line,
# which turns on compiler-level obfuscation (control-flow flattening,
# opaque constants, anti-hooking) via the Clang pass plugin. Which functions
# get obfuscated is decided by omvll_config.py in the repo root.
#
# NOTE: only APP_CFLAGS, not APP_CPPFLAGS — in ndk-build, APP_CFLAGS is
# applied to BOTH C and C++ sources already (APP_CPPFLAGS is C++-only, on
# top of it). Setting it on both doubled the flag on every .cpp compile.
ifdef OMVLL_PLUGIN
APP_CFLAGS += -fpass-plugin=$(OMVLL_PLUGIN)
endif
