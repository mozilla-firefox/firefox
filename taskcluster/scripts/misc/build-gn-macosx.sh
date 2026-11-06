#!/bin/bash
set -e -v

# This script is for building GN for macOS.

WORKSPACE=$HOME/workspace

# The target triple selects the CPU architecture of the resulting binary
# (x86_64-apple-darwin for Intel, aarch64-apple-darwin for Apple Silicon).
TARGET="${1:-x86_64-apple-darwin}"

CROSS_SYSROOT=$MOZ_FETCHES_DIR/MacOSX13.3.sdk

# arm64 macOS only exists from 11.0; Intel goes back further.
case "$TARGET" in
  aarch64-apple-darwin) export MACOSX_DEPLOYMENT_TARGET=11.0 ;;
  x86_64-apple-darwin) export MACOSX_DEPLOYMENT_TARGET=10.12 ;;
  *)
    echo "Unsupported macOS target $TARGET"
    exit 1
esac

export CC=$MOZ_FETCHES_DIR/clang/bin/clang
export CXX=$MOZ_FETCHES_DIR/clang/bin/clang++
export AR=$MOZ_FETCHES_DIR/clang/bin/llvm-ar
export CFLAGS="-target $TARGET -isysroot ${CROSS_SYSROOT} -I${CROSS_SYSROOT}/usr/include -iframework ${CROSS_SYSROOT}/System/Library/Frameworks"
export CXXFLAGS="-stdlib=libc++ ${CFLAGS}"
export LDFLAGS="-fuse-ld=lld ${CXXFLAGS} -Wl,-syslibroot,${CROSS_SYSROOT} -Wl,-dead_strip"

# We patch tools/gn/bootstrap/bootstrap.py to detect this.
export MAC_CROSS=1

cd $GECKO_PATH

. taskcluster/scripts/misc/build-gn-common.sh
