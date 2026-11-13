#!/usr/bin/env bash

# This script cross-compiles the NSS library for Android.

set -euvx

if [[ "${#}" -ne 5 ]]
then
    echo "Usage:"
    echo "./build-nss-android.sh <ABSOLUTE_SRC_DIR> <DIST_DIR> <TOOLCHAIN_PATH> <TOOLCHAIN> <ANDROID_NDK_API_VERSION>"
    exit 1
fi

NSS_SRC_DIR=${1}
DIST_DIR=${2}
TOOLCHAIN_PATH=${3}
TOOLCHAIN=${4}
ANDROID_NDK_API_VERSION=${5}

if [[ -d "${DIST_DIR}" ]]; then
  echo "${DIST_DIR} folder already exists. Skipping build."
  exit 0
fi

if [[ "${TOOLCHAIN}" == "x86_64-linux-android" ]]
then
  GYP_ARCH="x64"
  NSPR_64="--enable-64bit"
elif [[ "${TOOLCHAIN}" == "aarch64-linux-android" ]]
then
  GYP_ARCH="arm64"
  NSPR_64="--enable-64bit"
elif [[ "${TOOLCHAIN}" == "arm-linux-androideabi" ]]
then
  GYP_ARCH="arm"
else
  echo "Unknown toolchain"
  exit 1
fi
NSPR_64="${NSPR_64:-""}"

# shellcheck source=libs/build-android-common.sh
source ./build-android-common.sh

# Build NSPR
NSPR_BUILD_DIR=$(mktemp -d)
pushd "${NSPR_BUILD_DIR}"
"${NSS_SRC_DIR}"/nspr/configure \
  "${NSPR_64}" \
  --target="${TOOLCHAIN}" \
   --disable-debug \
   --enable-optimize
make
popd

# Build NSS
BUILD_DIR=$(mktemp -d)

BUILD_DIR=$(mktemp -d)
rm -rf "${NSS_SRC_DIR}/nss/out"
gyp -f ninja-android "${NSS_SRC_DIR}/nss/nss.gyp" \
  --depth "${NSS_SRC_DIR}/nss/" \
  --generator-output=. \
  -DOS=android \
  -Dforce_integrated_as=1 \
  -Dnspr_lib_dir="${NSPR_BUILD_DIR}/dist/lib" \
  -Dnspr_include_dir="${NSPR_BUILD_DIR}/dist/include/nspr" \
  -Dnss_dist_dir="${BUILD_DIR}" \
  -Dnss_dist_obj_dir="${BUILD_DIR}" \
  -Dhost_arch="${GYP_ARCH}" \
  -Dtarget_arch="${GYP_ARCH}" \
  -Dstatic_libs=1 \
  -Ddisable_dbm=1 \
  -Dsign_libs=0 \
  -Denable_sslkeylogfile=0 \
  -Ddisable_tests=1 \
  -Ddisable_libpkix=1 \
  -Dpython=python3

GENERATED_DIR="${NSS_SRC_DIR}/nss/out/Release"
ninja -C "${GENERATED_DIR}"

mkdir -p "${DIST_DIR}/include/nss"
mkdir -p "${DIST_DIR}/lib"
cp -p -L "${BUILD_DIR}/lib/libcertdb.a" "${DIST_DIR}/lib"
cp -p -L "${BUILD_DIR}/lib/libcerthi.a" "${DIST_DIR}/lib"
cp -p -L "${BUILD_DIR}/lib/libcryptohi.a" "${DIST_DIR}/lib"
cp -p -L "${BUILD_DIR}/lib/libfreebl_static.a" "${DIST_DIR}/lib"
cp -p -L "${BUILD_DIR}/lib/libgcm.a" "${DIST_DIR}/lib"
cp -p -L "${BUILD_DIR}/lib/libmozpkix.a" "${DIST_DIR}/lib"
cp -p -L "${BUILD_DIR}/lib/libnss_static.a" "${DIST_DIR}/lib"
cp -p -L "${BUILD_DIR}/lib/libnssb.a" "${DIST_DIR}/lib"
cp -p -L "${BUILD_DIR}/lib/libnssdev.a" "${DIST_DIR}/lib"
cp -p -L "${BUILD_DIR}/lib/libnsspki.a" "${DIST_DIR}/lib"
cp -p -L "${BUILD_DIR}/lib/libnssutil.a" "${DIST_DIR}/lib"
cp -p -L "${BUILD_DIR}/lib/libpk11wrap_static.a" "${DIST_DIR}/lib"
cp -p -L "${BUILD_DIR}/lib/libpkcs12.a" "${DIST_DIR}/lib"
cp -p -L "${BUILD_DIR}/lib/libpkcs7.a" "${DIST_DIR}/lib"
cp -p -L "${BUILD_DIR}/lib/libsmime.a" "${DIST_DIR}/lib"
cp -p -L "${BUILD_DIR}/lib/libsoftokn_static.a" "${DIST_DIR}/lib"
cp -p -L "${BUILD_DIR}/lib/libssl.a" "${DIST_DIR}/lib"
# HW specific.
if [[ "${TOOLCHAIN}" == "x86_64-linux-android" ]]; then
  cp -p -L "${BUILD_DIR}/lib/libghash-aes-x86_c_lib.a" "${DIST_DIR}/lib"
  cp -p -L "${BUILD_DIR}/lib/libintel-gcm-wrap_c_lib.a" "${DIST_DIR}/lib"
  cp -p -L "${BUILD_DIR}/lib/libhw-acc-crypto-avx.a" "${DIST_DIR}/lib"
  cp -p -L "${BUILD_DIR}/lib/libhw-acc-crypto-avx2.a" "${DIST_DIR}/lib"
  cp -p -L "${BUILD_DIR}/lib/libsha-x86_c_lib.a" "${DIST_DIR}/lib"
fi
if [[ "${TOOLCHAIN}" == "aarch64-linux-android" ]] || [[ "${TOOLCHAIN}" == "arm-linux-androideabi" ]]; then
  cp -p -L "${BUILD_DIR}/lib/libarmv8_c_lib.a" "${DIST_DIR}/lib"
fi
if [[ "${TOOLCHAIN}" == "aarch64-linux-android" ]]; then
  cp -p -L "${BUILD_DIR}/lib/libghash-aes-aarch64_c_lib.a" "${DIST_DIR}/lib"
fi
if [[ "${TOOLCHAIN}" == "arm-linux-androideabi" ]]; then
  cp -p -L "${BUILD_DIR}/lib/libghash-aes-arm32-neon_c_lib.a" "${DIST_DIR}/lib"
fi
cp -p -L "${NSPR_BUILD_DIR}/dist/lib/libplc4.a" "${DIST_DIR}/lib"
cp -p -L "${NSPR_BUILD_DIR}/dist/lib/libplds4.a" "${DIST_DIR}/lib"
cp -p -L "${NSPR_BUILD_DIR}/dist/lib/libnspr4.a" "${DIST_DIR}/lib"

cp -p -L -R "${BUILD_DIR}/public/nss/"* "${DIST_DIR}/include/nss"
cp -p -L -R "${NSPR_BUILD_DIR}/dist/include/nspr/"* "${DIST_DIR}/include/nss"
