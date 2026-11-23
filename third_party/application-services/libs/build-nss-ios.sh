#!/usr/bin/env bash

# This script cross-compiles the NSS library for iOS.

set -euvx

if [[ "${#}" -ne 4 ]]
then
    echo "Usage:"
    echo "./build-nss-ios.sh <ABSOLUTE_SRC_DIR> <DIST_DIR> <ARCH> <IOS_MIN_SDK_VERSION>"
    exit 1
fi

NSS_SRC_DIR=${1}
DIST_DIR=${2}
ARCH=${3}
IOS_MIN_SDK_VERSION=${4}

if [[ -d "${DIST_DIR}" ]]; then
  echo "${DIST_DIR} folder already exists. Skipping build."
  exit 0
fi

if [[ "${ARCH}" == "x86_64" ]]; then
  NSS_ARCH="x64"
  IOS_SDK="iphonesimulator"
elif [[ "${ARCH}" == "arm64" ]]; then
  NSS_ARCH="arm64"
  IOS_SDK="iphoneos"
elif [[ "${ARCH}" == "arm64-sim" ]]; then
  # We need to build nss for M1 sims ahead of time
  NSS_ARCH="arm64"
  IOS_SDK="iphonesimulator"
  # ARCH is used further down the line, but arm64-sim doesn't exist so we swap it back
  # to the original
  ARCH="arm64"
else
  echo "Unsupported architecture"
  exit 1
fi

# build.sh builds NSPR as well, with Xcode's compilers for the target.
BUILD_DIR=$(mktemp -d)
rm -rf "${NSS_SRC_DIR}/nss/out"
"${NSS_SRC_DIR}"/nss/build.sh \
  -v \
  --opt \
  --static \
  --disable-tests \
  --disable-keylog \
  --python=python3 \
  --target-os=ios \
  --ios-sdk="${IOS_SDK}" \
  --ios-version-min="${IOS_MIN_SDK_VERSION}" \
  -t "${NSS_ARCH}" \
  --dist="${BUILD_DIR}" \
  -Ddisable_dbm=1 \
  -Ddisable_libpkix=1
NSS_DIST_OBJ_DIR="${BUILD_DIR}/Release"

mkdir -p "${DIST_DIR}/include/nss"
./copy-nss-libs.sh "${NSS_DIST_OBJ_DIR}/lib" "${DIST_DIR}/lib"

cp -p -L -R "${BUILD_DIR}/public/nss/"* "${DIST_DIR}/include/nss"
cp -p -L -R "${NSS_DIST_OBJ_DIR}/include/nspr/"* "${DIST_DIR}/include/nss"

./check-nss-libs.sh "${NSS_DIST_OBJ_DIR}/lib" "${DIST_DIR}/lib"
