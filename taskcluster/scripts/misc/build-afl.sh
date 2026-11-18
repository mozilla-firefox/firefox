#!/bin/sh

set -e -x

artifact="$(basename "$TOOLCHAIN_ARTIFACT")"
dir="${artifact%.tar.*}"
scripts="$(realpath "${0%/*}")"

# these lists are copied from AFLplusplus/GNUmakefile
PROGS="afl-fuzz afl-showmap afl-tmin afl-gotcpu afl-analyze afl-cmin"
SH_PROGS="afl-plot afl-cmin.awk afl-cmin.bash afl-cmin.py afl-whatsup afl-addseeds afl-system-config afl-persistent-config"

cd "$MOZ_FETCHES_DIR/AFLplusplus"
patch -p1 -i "$scripts/afl-nyx.patch"

make -f GNUmakefile $PROGS \
    CC="$MOZ_FETCHES_DIR/clang/bin/clang" \
    CFLAGS="--sysroot $MOZ_FETCHES_DIR/sysroot" \
    CODE_COVERAGE=1 \
    NO_PYTHON=1 \
    PREFIX=/
mkdir -p "$dir/bin"
install -m 755 $PROGS $SH_PROGS "$dir/bin"
ln -sf afl-cmin "$dir/bin/afl-merge"
install -m 644 ./utils/dynamic_covfilter/make_symbol_list.py "$dir/bin/"

make -f GNUmakefile.llvm install \
    CODE_COVERAGE=1 \
    CPPFLAGS="--sysroot $MOZ_FETCHES_DIR/sysroot" \
    DESTDIR="$dir" \
    LLVM_CONFIG="$MOZ_FETCHES_DIR/clang/bin/llvm-config" \
    PREFIX=/
rm -rf "$dir/share"

tar caf "$artifact" "$dir"

mkdir -p "$UPLOAD_DIR"
mv "$artifact" "$UPLOAD_DIR"
