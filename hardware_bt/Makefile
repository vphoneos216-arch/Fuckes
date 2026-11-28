CXX ?= clang++
CXXFLAGS = -std=c++17 -DTOUCHBASE_DIAGNOSTICS=0 -fno-rtti -fexceptions -fvisibility=hidden -fvisibility-inlines-hidden -O2 -ffunction-sections -fdata-sections -fomit-frame-pointer -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fno-asynchronous-unwind-tables -fno-unwind-tables -g0 -Wno-error=format-security -Wno-nontrivial-memcall -Wno-unused-result

INCLUDES = -Iincludes \
           -Iincludes/fonts \
           -Iincludes/font \
           -Iincludes/internal \
           -Iincludes/internal/ImGui \
           -Iincludes/internal/ImGui/backends \
           -Iincludes/draw/ImGui \
           -Iincludes/draw/ImGui/backends \
           -Iincludes/external/openssl-android-arm64-v8a/include \
           -Iincludes/external/curl-android-arm64-v8a/include \
           -Iincludes/external/paradise \
           -Iincludes/external/libsodium-android-arm64-v8a/include \
           -Isrc \
           -Isrc/protect \
           -Isrc/ui \
           -Isrc/game

STATIC_LIBS = includes/external/paradise/libparadise_api.release.a \
              includes/external/libsodium-android-arm64-v8a/lib/libsodium.a \
              includes/external/curl-android-arm64-v8a/lib/libcurl.a \
              includes/external/openssl-android-arm64-v8a/lib/libssl.a \
              includes/external/openssl-android-arm64-v8a/lib/libcrypto.a \
              /data/user/0/com.agy/files/usr/aarch64-linux-android/lib/libc++_static.a \
              /data/user/0/com.agy/files/usr/aarch64-linux-android/lib/libc++abi.a \
              /data/user/0/com.agy/files/usr/aarch64-linux-android/lib/libunwind.a

LDFLAGS = -nostdlib++ -pie -Wl,--gc-sections -Wl,--strip-all -Wl,--build-id=none -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -L/data/user/0/com.agy/files/usr/opt/ndk-multilib/aarch64-linux-android/lib -L/system/lib64
LIBS = -llog -landroid -lEGL -lGLESv3 -lz -ldl -lm -lc

SRCS = src/main.cpp \
       src/other/vendor_release_log.cpp \
       src/game/data.cpp \
       src/protect/oxorany.cpp \
       src/protect/security.cpp \
       src/ui/menu.cpp \
       src/ui/bar.cpp \
       src/ui/widgets/widgets.cpp \
       src/ui/png/image.cpp \
       includes/draw/Android_draw/draw.cpp \
       includes/draw/Android_touch/Touch.cpp \
       includes/draw/ImGui/imgui.cpp \
       includes/draw/ImGui/imgui_draw.cpp \
       includes/draw/ImGui/imgui_tables.cpp \
       includes/draw/ImGui/imgui_widgets.cpp \
       includes/draw/ImGui/backends/imgui_impl_android.cpp \
       includes/draw/ImGui/backends/imgui_impl_opengl3.cpp

OBJS = $(patsubst %.cpp, build/%.o, $(notdir $(SRCS)))

TARGET_DIR = libs/arm64-v8a
TARGET = $(TARGET_DIR)/client.sh

all: directories $(TARGET) client.sh

directories:
	@mkdir -p build $(TARGET_DIR)

build/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

build/%.o: src/other/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

build/%.o: src/game/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

build/%.o: src/protect/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

build/%.o: src/ui/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

build/%.o: src/ui/widgets/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

build/%.o: src/ui/png/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

build/%.o: includes/draw/Android_draw/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

build/%.o: includes/draw/Android_touch/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

build/%.o: includes/draw/ImGui/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

build/%.o: includes/draw/ImGui/backends/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) $(STATIC_LIBS) $(LDFLAGS) $(LIBS) -o $@
	@chmod +x $@

client.sh: $(TARGET)
	@cp $(TARGET) client.sh
	@chmod +x client.sh

clean:
	rm -rf build $(TARGET_DIR) client.sh

.PHONY: all directories clean
