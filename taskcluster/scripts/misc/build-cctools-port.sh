#!/bin/bash

set -x -e -v

# This script is for building cctools (Apple's binutils) for Linux using
# cctools-port (https://github.com/tpoechtrager/cctools-port).
WORKSPACE=$HOME/workspace

# Set some crosstools-port and libtapi directories
CROSSTOOLS_SOURCE_DIR=$MOZ_FETCHES_DIR/cctools-port
CROSSTOOLS_CCTOOLS_DIR=$CROSSTOOLS_SOURCE_DIR/cctools
CROSSTOOLS_BUILD_DIR=$WORKSPACE/cctools
LIBTAPI_SOURCE_DIR=$MOZ_FETCHES_DIR/apple-libtapi
LIBTAPI_BUILD_DIR=$WORKSPACE/libtapi-build
LIBDISPATCH_SOURCE_DIR=$MOZ_FETCHES_DIR/apple-libdispatch
LIBDISPATCH_BUILD_DIR=$WORKSPACE/libdispatch-build
LDID_SOURCE_DIR=$MOZ_FETCHES_DIR/ldid
CLANG_DIR=$MOZ_FETCHES_DIR/clang

# Create our directories
mkdir -p $CROSSTOOLS_BUILD_DIR $LIBTAPI_BUILD_DIR $LIBDISPATCH_BUILD_DIR

# Apply a minor libtapi tweak so cmake from outside of the srcdir works.
cd $LIBTAPI_SOURCE_DIR
patch -p1 < $GECKO_PATH/taskcluster/scripts/misc/libtapi.patch

# Build libtapi; the included build.sh is not sufficient for our purposes.
cd $LIBTAPI_BUILD_DIR
# Common setup for libtapi and cctools
export CC=$CLANG_DIR/bin/clang
export CXX=$CLANG_DIR/bin/clang++
# We also need this LD_LIBRARY_PATH at build time, since tapi builds bits of
# clang build tools, and then executes those tools.
export LD_LIBRARY_PATH=$CLANG_DIR/lib

# Value taken from build.sh
TAPI_VERSION=1600.0.11.8

cmake -G Ninja $LIBTAPI_SOURCE_DIR/src/llvm \
      -DLLVM_INCLUDE_TESTS=OFF \
      -DCMAKE_BUILD_TYPE=RELEASE \
      -DLLVM_ENABLE_PROJECTS="tapi;clang" \
      -DLLVM_ENABLE_ZSTD=OFF \
      -DCMAKE_INSTALL_PREFIX=$CROSSTOOLS_BUILD_DIR \
      -DCMAKE_SYSROOT=$MOZ_FETCHES_DIR/sysroot \
      -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld \
      -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld \
      -DLLVM_TARGETS_TO_BUILD="X86;ARM;AArch64" \
      -DTAPI_REPOSITORY_STRING=$TAPI_VERSION \
      -DTAPI_FULL_VERSION=$TAPI_VERSION

ninja clangBasic vt_gen -v
ninja libtapi install-libtapi install-tapi-headers -v

cd $LIBDISPATCH_SOURCE_DIR
patch -p1 < $GECKO_PATH/taskcluster/scripts/misc/libdispatch-unused-but-set-global.patch

cd $LIBDISPATCH_BUILD_DIR
cmake -G Ninja $LIBDISPATCH_SOURCE_DIR \
      -DCMAKE_BUILD_TYPE=RELEASE \
      -DCMAKE_INSTALL_PREFIX=$CROSSTOOLS_BUILD_DIR \
      -DCMAKE_SYSROOT=$MOZ_FETCHES_DIR/sysroot \
      -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld \
      -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld

ninja -v
ninja install -v

# Setup LDFLAGS late so run-at-build-time tools in the basic clang build don't
# pick up the possibly-incompatible libstdc++ from clang.
# Also set it up such that loading libtapi doesn't require a LD_LIBRARY_PATH.
# (this requires two dollars and extra backslashing because it's used verbatim
# via a Makefile)
export LDFLAGS="-fuse-ld=lld -lpthread -Wl,-rpath-link,$MOZ_FETCHES_DIR/sysroot/lib/x86_64-linux-gnu -Wl,-rpath-link,$MOZ_FETCHES_DIR/sysroot/usr/lib/x86_64-linux-gnu -Wl,-rpath,\\\$\$ORIGIN/../lib,-rpath,\\\$\$ORIGIN/../../clang/lib"

export CC="$CC --sysroot=$MOZ_FETCHES_DIR/sysroot"
export CXX="$CXX --sysroot=$MOZ_FETCHES_DIR/sysroot"

cd $CROSSTOOLS_SOURCE_DIR
patch -p1 < $GECKO_PATH/taskcluster/scripts/misc/cctools-llvm-c-headers.patch

# Keep Rust compiler_builtins atoms live during LTO dead-strip so cross-language
# LTO links (e.g. macOS shippable) don't drop symbols like __umodti3.
patch -p1 < $GECKO_PATH/taskcluster/scripts/misc/cctools-ld64-dead-strip-compiler-builtins.patch
# Fix out-of-bounds read when a string fills a string pool buffer to its end
patch -p1 < $GECKO_PATH/taskcluster/scripts/misc/cctools-ld64-fix-out-of-bounds-read.patch

# Configure crosstools-port
cd $CROSSTOOLS_CCTOOLS_DIR

# Force re-libtoolization to overwrite files with the new libtool bits.
perl -pi -e 's/(LIBTOOLIZE -c)/\1 -f/' autogen.sh
./autogen.sh
./configure \
    --prefix=$CROSSTOOLS_BUILD_DIR \
    --target=x86_64-apple-darwin \
    --with-llvm-config=$CLANG_DIR/bin/llvm-config \
    --enable-lto-support \
    --enable-tapi-support \
    --with-libtapi=$CROSSTOOLS_BUILD_DIR \
    --with-libdispatch=$CROSSTOOLS_BUILD_DIR \
    --with-libblocksruntime=$CROSSTOOLS_BUILD_DIR

# Build cctools
make -j `nproc --all` install

# Build ldid
cd $LDID_SOURCE_DIR
# The crypto library in the sysroot cannot be linked in a PIE executable so we use -no-pie
make -j `nproc --all` install INSTALLPREFIX=$CROSSTOOLS_BUILD_DIR LDFLAGS="-no-pie -Wl,-Bstatic -lcrypto -Wl,-Bdynamic -ldl -pthread"

strip $CROSSTOOLS_BUILD_DIR/bin/*
# various build scripts based on cmake want to find `lipo` without a prefix
cp $CROSSTOOLS_BUILD_DIR/bin/x86_64-apple-darwin-lipo $CROSSTOOLS_BUILD_DIR/bin/lipo

(cd $CROSSTOOLS_BUILD_DIR/bin/; for i in x86_64-apple-darwin-*; do
    ln $i aarch64${i#x86_64}
done)

# Put a tarball in the artifacts dir
mkdir -p $UPLOAD_DIR
tar caf $UPLOAD_DIR/cctools.tar.zst -C $CROSSTOOLS_BUILD_DIR/.. `basename $CROSSTOOLS_BUILD_DIR`
