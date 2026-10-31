cmake_minimum_required(VERSION 3.22.1)
project(ixauth)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(EXT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external)

add_library(ixauth SHARED
    ix_auth.cpp
)

target_include_directories(ixauth PRIVATE
    ${EXT_DIR}/libsodium-android-arm64-v8a/include
    ${EXT_DIR}/curl-android-arm64-v8a/include
    ${EXT_DIR}/openssl-android-arm64-v8a/include
)

# Prebuilt static libs — the SAME ones already proven working for the
# INX.SH binary itself (see ../../../../jni/Android.mk). curl needs
# openssl for TLS; link order matters for static linking (consumer
# before dependency).
add_library(sodium_static STATIC IMPORTED)
set_target_properties(sodium_static PROPERTIES
    IMPORTED_LOCATION ${EXT_DIR}/libsodium-android-arm64-v8a/lib/libsodium.a)

add_library(curl_static STATIC IMPORTED)
set_target_properties(curl_static PROPERTIES
    IMPORTED_LOCATION ${EXT_DIR}/curl-android-arm64-v8a/lib/libcurl.a)

add_library(ssl_static STATIC IMPORTED)
set_target_properties(ssl_static PROPERTIES
    IMPORTED_LOCATION ${EXT_DIR}/openssl-android-arm64-v8a/lib/libssl.a)

add_library(crypto_static STATIC IMPORTED)
set_target_properties(crypto_static PROPERTIES
    IMPORTED_LOCATION ${EXT_DIR}/openssl-android-arm64-v8a/lib/libcrypto.a)

find_library(log-lib log)
find_library(z-lib z)

target_link_libraries(ixauth
    curl_static
    ssl_static
    crypto_static
    sodium_static
    ${log-lib}
    ${z-lib}
)

target_compile_options(ixauth PRIVATE
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    -O2
    -fstack-protector-strong
    -D_FORTIFY_SOURCE=2
    -ffunction-sections -fdata-sections
)

target_link_options(ixauth PRIVATE
    -Wl,--gc-sections
)
