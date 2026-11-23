#!/usr/bin/env bash

# This script cross-compiles the NSS library for Android.

set -euvx

if [[ "${#}" -ne 4 ]]
then
    echo "Usage:"
    echo "./build-nss-android.sh <ABSOLUTE_SRC_DIR> <DIST_DIR> <TOOLCHAIN> <ANDROID_NDK_API_VERSION>"
    echo "Requires ANDROID_NDK_ROOT and NDK_HOST_TAG in the environment (android_defaults.sh sets both)."
    exit 1
fi

NSS_SRC_DIR=${1}
DIST_DIR=${2}
TOOLCHAIN=${3}
ANDROID_NDK_API_VERSION=${4}

if [[ -d "${DIST_DIR}" ]]; then
  echo "${DIST_DIR} folder already exists. Skipping build."
  exit 0
fi

if [[ "${TOOLCHAIN}" == "x86_64-linux-android" ]]
then
  NSS_ARCH="x64"
elif [[ "${TOOLCHAIN}" == "aarch64-linux-android" ]]
then
  NSS_ARCH="arm64"
elif [[ "${TOOLCHAIN}" == "arm-linux-androideabi" ]]
then
  NSS_ARCH="arm"
else
  echo "Unknown toolchain"
  exit 1
fi

# build.sh builds NSPR as well, with the NDK's compilers for the target.
BUILD_DIR=$(mktemp -d)
rm -rf "${NSS_SRC_DIR}/nss/out"
"${NSS_SRC_DIR}"/nss/build.sh \
  -v \
  --opt \
  --static \
  --disable-tests \
  --disable-keylog \
  --python=python3 \
  --target-os=android \
  --android-ndk="${ANDROID_NDK_ROOT}" \
  --android-api="${ANDROID_NDK_API_VERSION}" \
  -t "${NSS_ARCH}" \
  --dist="${BUILD_DIR}" \
  -Ddisable_dbm=1 \
  -Ddisable_libpkix=1
NSS_DIST_OBJ_DIR="${BUILD_DIR}/Release"

mkdir -p "${DIST_DIR}/include/nss"
./copy-nss-libs.sh "${NSS_DIST_OBJ_DIR}/lib" "${DIST_DIR}/lib"

cp -p -L -R "${BUILD_DIR}/public/nss/"* "${DIST_DIR}/include/nss"
cp -p -L -R "${NSS_DIST_OBJ_DIR}/include/nspr/"* "${DIST_DIR}/include/nss"

NM="${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/${NDK_HOST_TAG}/bin/llvm-nm" \
  ./check-nss-libs.sh "${NSS_DIST_OBJ_DIR}/lib" "${DIST_DIR}/lib"
