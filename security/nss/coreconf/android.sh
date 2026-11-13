#!/usr/bin/env bash
# This script sets up a cross-compile for Android with the clang toolchain in
# an Android NDK (r19 or later). It is sourced by build.sh, which then builds
# NSPR and NSS with the CC, CXX, AR, ... exported here.

android_setup()
{
    android_ndk="${android_ndk:-${ANDROID_NDK_ROOT:-${ANDROID_NDK_HOME:-$ANDROID_NDK}}}"
    if [ -z "$android_ndk" ]; then
        echo "Building for Android needs an NDK: pass --android-ndk=<dir>" \
             "or set ANDROID_NDK_ROOT." 1>&2
        exit 2
    fi

    local host_tag
    case "$(uname -s)" in
        Linux) host_tag=linux-x86_64 ;;
        # The macOS NDK is universal, but still lives under this name.
        Darwin) host_tag=darwin-x86_64 ;;
        *)
            echo "Building for Android is only supported on Linux and macOS." 1>&2
            exit 2
            ;;
    esac
    local bin="$android_ndk/toolchains/llvm/prebuilt/$host_tag/bin"
    if [ ! -d "$bin" ]; then
        echo "$android_ndk has no clang toolchain at $bin;" \
             "NDK r19 or later is needed." 1>&2
        exit 2
    fi

    # Default to the oldest API level that the NDK supports.
    if [ -z "$android_api" ]; then
        android_api=$(sed -n 's/.*"min": *\([0-9]*\).*/\1/p' \
                          "$android_ndk/meta/platforms.json" 2>/dev/null)
        if [ -z "$android_api" ]; then
            echo "Unable to determine the minimum API level of $android_ndk;" \
                 "pass --android-api=<level>." 1>&2
            exit 2
        fi
    fi

    # The NDK names each clang wrapper for its target and API level, e.g.
    # aarch64-linux-android23-clang. 32-bit ARM is the exception: the wrapper
    # is armv7a-linux-androideabi, but NSPR knows the target as
    # arm-linux-androideabi.
    local clang_target
    case "${target_arch:=arm64}" in
        arm) clang_target=armv7a-linux-androideabi; nspr_target=arm-linux-androideabi ;;
        arm64|aarch64) clang_target=aarch64-linux-android ;;
        x64) clang_target=x86_64-linux-android ;;
        ia32) clang_target=i686-linux-android ;;
        *)
            echo "Unsupported Android target architecture: $target_arch" 1>&2
            exit 2
            ;;
    esac
    nspr_target="${nspr_target:-$clang_target}"

    export CC="$bin/$clang_target$android_api-clang"
    export CCC="$bin/$clang_target$android_api-clang++"
    export CXX="$CCC"
    if [ ! -x "$CC" ]; then
        echo "$CC not found; is API level $android_api supported by this NDK?" 1>&2
        exit 2
    fi
    export AR="$bin/llvm-ar"
    export RANLIB="$bin/llvm-ranlib"
    export STRIP="$bin/llvm-strip"
    # NM and READELF are left alone: gyp runs "nm -gD -f p", which llvm-nm
    # rejects, to list the symbols of shared libraries.

    # gyp otherwise picks its generator flavor from the host, which on macOS
    # would link the way macOS does.
    gyp_format=ninja-android
    # shlibsign has to run the libraries it signs, which it can't do here.
    gyp_params+=(-DOS=android -Dsign_libs=0)
}

android_setup
