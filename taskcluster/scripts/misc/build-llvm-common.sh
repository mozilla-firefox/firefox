#!/bin/sh

set -e -x

artifact=$(basename $TOOLCHAIN_ARTIFACT)
dir=${artifact%.tar.*}
what=$1
shift
install=$1
shift
target=$1
shift

clang=$MOZ_FETCHES_DIR/clang/bin/clang

case "$target" in
aarch64-apple-darwin)
  arch=arm64
  compiler_wrapper() {
    echo exec \$MOZ_FETCHES_DIR/clang/bin/$1 -mcpu=apple-m1 \"\$@\" > $1
    chmod +x $1
  }
  compiler_wrapper clang
  compiler_wrapper clang++
  clang=$PWD/clang
  ;;
x86_64-apple-darwin)
  arch=x86_64
  ;;
esac

case "$target" in
*-apple-darwin)
  MACOS_TARGET=$target
  . $GECKO_PATH/taskcluster/scripts/misc/macos-setup.sh
  EXTRA_CMAKE_FLAGS="
    $EXTRA_CMAKE_FLAGS
    -DCMAKE_LINKER=$MACOS_LD
    -DCMAKE_SYSTEM_NAME=Darwin
    -DCMAKE_SYSTEM_VERSION=$MACOSX_DEPLOYMENT_TARGET
    -DCMAKE_OSX_SYSROOT=$MACOS_SDK
    -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld
    -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld
    -DLLVM_DEFAULT_TARGET_TRIPLE=$target
  "
  if [ "$what" = "compiler-rt" ]; then
    EXTRA_CMAKE_FLAGS="
      $EXTRA_CMAKE_FLAGS
      -DCMAKE_LIPO=$MACOS_LIPO
      -DDARWIN_osx_ARCHS=$arch
      -DDARWIN_osx_SYSROOT=$MACOS_SDK
      -DDARWIN_macosx_OVERRIDE_SDK_VERSION=11.0
      -DDARWIN_osx_BUILTIN_ARCHS=$arch
    "
  fi
  # compiler-rt build script expects to find `codesign` in $PATH.
  # Give it a fake one.
  echo "#!/bin/sh" > codesign
  chmod +x codesign
  # cmake makes decisions based on the output of the mac-only sw_vers, which is
  # obviously missing when cross-compiling, so create a fake one. The exact
  # version doesn't really matter: as of writing, cmake checks at most for 10.5.
  echo "#!/bin/sh" > sw_vers
  echo echo 10.15 >> sw_vers
  chmod +x sw_vers
  PATH="$PATH:$PWD"
  ;;
*-linux-android)
  case "$target" in
  armv7-linux-android)
    arch=arm
    ;;
  *-linux-android)
    arch=${target%-linux-android}
    ;;
  esac
  # Keep this in sync with min_android_version in android-ndk.configure.
  api_level=26
  target=$target$api_level
  # These flags are only necessary to pass the cmake tests. They don't end up
  # actually using libgcc, so use an empty library instead of trying to find
  # where it is in the NDK.
  if [ "$what" = "compiler-rt" ]; then
    exe_linker_flags="--rtlib=libgcc -L$PWD"
    touch libgcc.a
  fi
  EXTRA_CMAKE_FLAGS="
    $EXTRA_CMAKE_FLAGS
    -DCMAKE_SYSROOT=$MOZ_FETCHES_DIR/android-ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot
    -DCMAKE_LINKER=$MOZ_FETCHES_DIR/clang/bin/ld.lld
    -DCMAKE_EXE_LINKER_FLAGS='-fuse-ld=lld $exe_linker_flags'
    -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld
    -DANDROID=1
    -DANDROID_NATIVE_API_LEVEL=$api_level
    -DSANITIZER_ALLOW_CXXABI=OFF
    -DLLVM_DEFAULT_TARGET_TRIPLE=$arch-unknown-linux-android
  "
  ;;
*-unknown-linux-gnu)
  if [ -d "$MOZ_FETCHES_DIR/sysroot" ]; then
    sysroot=$MOZ_FETCHES_DIR/sysroot
  else
    sysroot=$MOZ_FETCHES_DIR/sysroot-${target%-unknown-linux-gnu}-linux-gnu
  fi
  if [ "${target%-unknown-linux-gnu}" = i686 ]; then
    EXTRA_CMAKE_FLAGS="
      $EXTRA_CMAKE_FLAGS
      -DLLVM_TABLEGEN=$MOZ_FETCHES_DIR/clang/bin/llvm-tblgen
    "
  fi
  EXTRA_CMAKE_FLAGS="
    $EXTRA_CMAKE_FLAGS
    -DCMAKE_SYSROOT=$sysroot
    -DCMAKE_LINKER=$MOZ_FETCHES_DIR/clang/bin/ld.lld
    -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld
    -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld
    -DLLVM_ENABLE_TERMINFO=OFF
  "
  ;;
*-pc-windows-msvc)
  EXTRA_CMAKE_FLAGS="
    $EXTRA_CMAKE_FLAGS
    -DCMAKE_TOOLCHAIN_FILE=$MOZ_FETCHES_DIR/llvm-project/llvm/cmake/platforms/WinMsvc.cmake
    -DLLVM_NATIVE_TOOLCHAIN=$MOZ_FETCHES_DIR/clang
    -DHOST_ARCH=${target%-pc-windows-msvc}
    -DLLVM_DISABLE_ASSEMBLY_FILES=ON
  "
  # LLVM 15+ uses different input variables.
  if grep -q LLVM_WINSYSROOT $MOZ_FETCHES_DIR/llvm-project/llvm/cmake/platforms/WinMsvc.cmake; then
    EXTRA_CMAKE_FLAGS="
      $EXTRA_CMAKE_FLAGS
      -DLLVM_WINSYSROOT=$MOZ_FETCHES_DIR/vs
    "
  else
    # WinMsvc.cmake before LLVM 15 doesn't support spaces in WINDSK_BASE.
    ln -s "windows kits/10" $MOZ_FETCHES_DIR/vs/sdk
    EXTRA_CMAKE_FLAGS="
      $EXTRA_CMAKE_FLAGS
      -DMSVC_BASE=$MOZ_FETCHES_DIR/vs/vc/tools/msvc/14.39.33519
      -DWINSDK_BASE=$MOZ_FETCHES_DIR/vs/sdk
      -DWINSDK_VER=10.0.22621.0
    "
  fi
  ;;
*)
  echo $target is not supported yet
  exit 1
  ;;
esac

case "$target" in
*-pc-windows-msvc)
  ;;
*)
  EXTRA_CMAKE_FLAGS="
    $EXTRA_CMAKE_FLAGS
    -DCMAKE_C_COMPILER=$clang
    -DCMAKE_CXX_COMPILER=$clang++
    -DCMAKE_AR=$MOZ_FETCHES_DIR/clang/bin/llvm-ar
    -DCMAKE_RANLIB=$MOZ_FETCHES_DIR/clang/bin/llvm-ranlib
  "
  ;;
esac

mkdir build
cd build

for patchfile in "$@"; do
  case $patchfile in
  *.json)
      jq -r '.patches[]' $GECKO_PATH/$patchfile | while read p; do
        patch -d $MOZ_FETCHES_DIR/llvm-project -p1 < $GECKO_PATH/$(dirname $patchfile)/$p
      done
      ;;
  *)
      patch -d $MOZ_FETCHES_DIR/llvm-project -p1 < $GECKO_PATH/$patchfile
      ;;
  esac
done

eval cmake \
  $MOZ_FETCHES_DIR/llvm-project/$what \
  -GNinja \
  -DCMAKE_C_COMPILER_TARGET=$target \
  -DCMAKE_CXX_COMPILER_TARGET=$target \
  -DCMAKE_ASM_COMPILER_TARGET=$target \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=${PWD}/${dir} \
  -DLLVM_ENABLE_ASSERTIONS=OFF \
  -DLLVM_ENABLE_ZSTD=OFF \
  -DLLVM_CONFIG_PATH=$MOZ_FETCHES_DIR/clang/bin/llvm-config \
  $EXTRA_CMAKE_FLAGS

ninja -v $install

if [ "$what" = "compiler-rt" ]; then
  # ninja install doesn't copy the PDBs, if any
  case "$target" in
  aarch64-pc-windows-msvc)
      # No pdb generated in that platform/arch configuration since
      # https://github.com/llvm/llvm-project/commit/655933070219f2b6f3a457c7e5af7edd4b5291b4
      if echo "$@" | grep -q -v 'clang-\(1[0-9]\|20\)'
      then
        test -z "$(find -name "*.pdb")"
      else
        cp lib/windows/*pdb $dir/lib/windows/
      fi
    ;;
  *-pc-windows-msvc)
    cp lib/windows/*pdb $dir/lib/windows/
    ;;
  esac
fi

tar caf "$artifact" "$dir"

mkdir -p "$UPLOAD_DIR"
mv "$artifact" "$UPLOAD_DIR"
