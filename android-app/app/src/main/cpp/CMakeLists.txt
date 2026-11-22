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
    # Ed448 package's OWN OpenSSL 3.3.2 headers come BEFORE the plain
    # openssl-android-arm64-v8a ones below, so <openssl/evp.h> in
    # ix_auth.cpp resolves here (1.1.0c below predates EVP_PKEY_ED448
    # entirely -- see the CMakeLists comment on ed448_static).
    ${EXT_DIR}/openssl3-ed448-android-arm64-v8a
    ${EXT_DIR}/openssl3-ed448-android-arm64-v8a/include
    ${EXT_DIR}/openssl-android-arm64-v8a/include
    ${EXT_DIR}/liboqs-android-arm64-v8a/include
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

# Ed448 crypto — a SEPARATE OpenSSL 3.3.2 build (the 1.1.0c above
# predates Ed448 support entirely) with every one of its global symbols
# renamed to a kdoss3_ prefix, so it can be linked alongside the plain
# OpenSSL 1.1.0c above without both libraries colliding on the same
# symbol names (EVP_PKEY_new, EVP_DigestVerify, etc. exist in both).
# kd_ossl_rename.h (included in ix_auth.cpp before any <openssl/*.h>)
# #defines every plain name to its kdoss3_ equivalent, so the compiler
# emits calls under the renamed symbol and the linker resolves them
# here, not in crypto_static above. Same lib jni/Android.mk's
# openssl3_ed448 module uses (see build-ed448-lib in build.yml) --
# built fresh here because that job's artifact doesn't cross workflow
# boundaries.
add_library(ed448_static STATIC IMPORTED)
set_target_properties(ed448_static PROPERTIES
    IMPORTED_LOCATION ${EXT_DIR}/openssl3-ed448-android-arm64-v8a/libcrypto_kdoss3.a)

# liboqs (ML-DSA-65, post-quantum) — built for android-arm64 by the CI
# workflow below (same cmake/NDK-toolchain recipe .github/workflows/
# build.yml already uses for the kernel module's own build-mldsa-
# android-lib job), vendored here fresh on every run rather than
# committed, since it's a few-minute build and keeps this directory
# free of a large prebuilt binary.
add_library(oqs_static STATIC IMPORTED)
set_target_properties(oqs_static PROPERTIES
    IMPORTED_LOCATION ${EXT_DIR}/liboqs-android-arm64-v8a/lib/liboqs.a)

find_library(log-lib log)
find_library(z-lib z)

target_link_libraries(ixauth
    curl_static
    ssl_static
    crypto_static
    ed448_static
    sodium_static
    oqs_static
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
