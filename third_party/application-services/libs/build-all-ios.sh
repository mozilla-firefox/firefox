#!/usr/bin/env bash

set -euvx

# This probably needs to be updated, but also includes the toolchain?
IOS_MIN_SDK_VERSION="11.0"
# Our short-names for the architectures.
TARGET_ARCHS=("x86_64" "arm64" "arm64-sim")

if [[ "${#}" -ne 1 ]]
then
    echo "Usage:"
    echo "./build-all-ios.sh <NSS_SRC_PATH>"
    exit 1
fi

NSS_SRC_PATH=${1}

function universal_lib() {
  DIR_NAME="${1}"
  LIB_NAME="${2}"
  shift; shift
  UNIVERSAL_DIR="ios/universal/${DIR_NAME}"
  LIB_PATH="${UNIVERSAL_DIR}/lib/${LIB_NAME}"
  if [[ ! -e "${LIB_PATH}" ]]; then
    mkdir -p "${UNIVERSAL_DIR}/lib"
    CMD="lipo"
    for ARCH in "${@}"; do
      if [[ "${ARCH}" != "arm64-sim" ]]; then
        CMD="${CMD} -arch ${ARCH} ios/${ARCH}/${DIR_NAME}/lib/${LIB_NAME}"
      fi
    done
    CMD="${CMD} -output ${LIB_PATH} -create"
    ${CMD}
  fi
}

echo "# Building NSS"
for i in "${!TARGET_ARCHS[@]}"; do
  ARCH=${TARGET_ARCHS[${i}]}
  DIST_DIR=$(abspath "ios/${ARCH}/nss")
  if [[ -d "${DIST_DIR}" ]]; then
    echo "${DIST_DIR} already exists. Skipping building nss."
  else
    ./build-nss-ios.sh "${NSS_SRC_PATH}" "${DIST_DIR}" "${ARCH}" "${IOS_MIN_SDK_VERSION}" || exit 1
  fi
done
# Every library that copy-nss-libs.sh copied for a device or Intel simulator
# build, made universal from the architectures that built it.
for LIB_PATH in ios/x86_64/nss/lib/*.a ios/arm64/nss/lib/*.a; do
  LIB_NAME=$(basename "${LIB_PATH}")
  LIB_ARCHS=()
  for ARCH in x86_64 arm64; do
    if [[ -e "ios/${ARCH}/nss/lib/${LIB_NAME}" ]]; then
      LIB_ARCHS+=("${ARCH}")
    fi
  done
  universal_lib "nss" "${LIB_NAME}" "${LIB_ARCHS[@]}"
done

HEADER_DIST_DIR="ios/universal/nss/include/nss"
if [[ ! -e "${HEADER_DIST_DIR}" ]]; then
  mkdir -p ${HEADER_DIST_DIR}
  # Choice of arm64 is arbitrary, it shouldn't matter.
  HEADER_SRC_DIR=$(abspath "ios/arm64/nss/include/nss")
  cp -L "${HEADER_SRC_DIR}"/*.h "${HEADER_DIST_DIR}"
fi

echo "# Ensure Glean checkout"
git submodule update --init
