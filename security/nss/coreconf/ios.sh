#!/usr/bin/env bash
# This script sets up a cross-compile for iOS with Xcode's clang. It is sourced
# by build.sh, which then builds NSPR and NSS with the CC, CXX, AR, ...
# exported here.

ios_setup()
{
    if [ "$(uname -s)" != Darwin ]; then
        echo "Building for iOS needs macOS and Xcode." 1>&2
        exit 2
    fi

    local arch
    case "${target_arch:=arm64}" in
        arm64|aarch64) arch=arm64; nspr_target=aarch64-apple-darwin ;;
        x64) arch=x86_64; nspr_target=x86_64-apple-darwin ;;
        *)
            echo "Unsupported iOS target architecture: $target_arch" 1>&2
            exit 2
            ;;
    esac

    # There are no x86_64 iOS devices, only simulators.
    if [ -z "$ios_sdk" ]; then
        if [ "$arch" = x86_64 ]; then
            ios_sdk=iphonesimulator
        else
            ios_sdk=iphoneos
        fi
    fi
    local target version_flag
    case "$ios_sdk" in
        iphoneos)
            if [ "$arch" = x86_64 ]; then
                echo "There are no x64 iOS devices; use --ios-sdk=iphonesimulator." 1>&2
                exit 2
            fi
            target="$arch-apple-ios"
            version_flag=-mios-version-min
            ;;
        iphonesimulator)
            target="$arch-apple-ios-simulator"
            version_flag=-mios-simulator-version-min
            ;;
        *)
            echo "Unknown iOS SDK: $ios_sdk; use iphoneos or iphonesimulator." 1>&2
            exit 2
            ;;
    esac
    ios_version_min="${ios_version_min:-12.0}"

    local sdk_path
    if ! sdk_path=$(xcrun --sdk "$ios_sdk" --show-sdk-path); then
        echo "The $ios_sdk SDK was not found; is Xcode installed?" 1>&2
        exit 2
    fi
    local cflags="-target $target -arch $arch -isysroot $sdk_path $version_flag=$ios_version_min"
    export CC="$(xcrun --sdk "$ios_sdk" -f clang) $cflags"
    export CCC="$(xcrun --sdk "$ios_sdk" -f clang++) $cflags"
    export CXX="$CCC"
    export AR="$(xcrun --sdk "$ios_sdk" -f ar)"
    export RANLIB="$(xcrun --sdk "$ios_sdk" -f ranlib)"
    export STRIP="$(xcrun --sdk "$ios_sdk" -f strip)"

    # gyp's Xcode emulation builds iOS targets into a directory per SDK.
    out_dir_suffix="-$ios_sdk"
    # shlibsign has to run the libraries it signs, which it can't do here.
    gyp_params+=(-DOS=ios -Diphone_deployment_target="$ios_version_min" -Dsign_libs=0)
}

ios_setup
