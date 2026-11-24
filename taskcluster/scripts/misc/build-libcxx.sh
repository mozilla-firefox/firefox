#!/bin/sh

set -e -x

artifact=$(basename $TOOLCHAIN_ARTIFACT)
dir=${artifact%.tar.*}
target=${dir#libcxx-}

# The static libraries are only for the few programs linked with
# -static-libstdc++. A plain -lc++ picks libc++.so, and linking against it is
# what catches libc++ symbols that Gecko code ends up referencing as hidden.
EXTRA_CMAKE_FLAGS="
  $EXTRA_CMAKE_FLAGS
  -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=ON
  -DLLVM_DEFAULT_TARGET_TRIPLE=$target
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  -DCMAKE_C_COMPILER_WORKS=1
  -DCMAKE_CXX_COMPILER_WORKS=1
  -DLLVM_ENABLE_RUNTIMES='libcxx;libcxxabi'
  -DLIBCXX_ENABLE_SHARED=ON
  -DLIBCXX_ENABLE_STATIC=ON
  -DLIBCXX_ENABLE_ABI_LINKER_SCRIPT=ON
  -DLIBCXX_STATICALLY_LINK_ABI_IN_STATIC_LIBRARY=ON
  -DLIBCXX_INCLUDE_BENCHMARKS=OFF
  -DLIBCXX_INCLUDE_TESTS=OFF
  -DLIBCXXABI_ENABLE_SHARED=ON
  -DLIBCXXABI_ENABLE_STATIC=ON
  -DLIBCXXABI_INCLUDE_TESTS=OFF
  -DLIBCXXABI_USE_LLVM_UNWINDER=OFF
"

export EXTRA_CMAKE_FLAGS

$(dirname $0)/build-llvm-common.sh runtimes install $target "$@"
