# Sourced by toolchain scripts targeting macOS. Set MACOS_TARGET (an *-apple-darwin triple; defaults to
# the host on Darwin) first. Sets MACOS_SDK (also exported as SDKROOT), MACOSX_DEPLOYMENT_TARGET, MACOS_CFLAGS,
# MACOS_RUSTFLAGS, the MACOS_CC/CXX/AR/RANLIB/LD/LIPO tool paths, and puts the fetched clang first on PATH.
# MACOS_EXTRA_CFLAGS is the target-independent part of MACOS_CFLAGS, for cmake builds that already select
# the target and SDK through cmake variables.

if [ -z "$MACOS_TARGET" ]; then
    case "$(uname -s)-$(uname -m)" in
        Darwin-arm64) MACOS_TARGET=aarch64-apple-darwin ;;
        Darwin-x86_64) MACOS_TARGET=x86_64-apple-darwin ;;
    esac
fi

case "$MACOS_TARGET" in
    aarch64-apple-darwin) export MACOSX_DEPLOYMENT_TARGET=11.0 ;;
    x86_64-apple-darwin) export MACOSX_DEPLOYMENT_TARGET=10.15 ;;
    *)
        echo "macos-setup.sh: unsupported target '$MACOS_TARGET'" >&2
        exit 1
        ;;
esac

MACOS_SDK=$(echo "$MOZ_FETCHES_DIR"/MacOSX*.sdk)
if [ ! -d "$MACOS_SDK" ]; then
    echo "macos-setup.sh: expected exactly one MacOSX*.sdk in $MOZ_FETCHES_DIR, found: $MACOS_SDK" >&2
    exit 1
fi
export SDKROOT=$MACOS_SDK

export PATH="$MOZ_FETCHES_DIR/clang/bin:$PATH"
MACOS_CC=$MOZ_FETCHES_DIR/clang/bin/clang
MACOS_CXX=$MOZ_FETCHES_DIR/clang/bin/clang++
MACOS_AR=$MOZ_FETCHES_DIR/clang/bin/llvm-ar
MACOS_RANLIB=$MOZ_FETCHES_DIR/clang/bin/llvm-ranlib
MACOS_LD=$MOZ_FETCHES_DIR/clang/bin/ld64.lld
MACOS_LIPO=$MOZ_FETCHES_DIR/clang/bin/llvm-lipo

MACOS_EXTRA_CFLAGS="-fuse-ld=lld -Qunused-arguments"
MACOS_CFLAGS="--target=$MACOS_TARGET -isysroot $MACOS_SDK -mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET $MACOS_EXTRA_CFLAGS"
MACOS_RUSTFLAGS="-C linker=$MACOS_CXX -C link-arg=-fuse-ld=lld -C link-arg=--target=$MACOS_TARGET"
