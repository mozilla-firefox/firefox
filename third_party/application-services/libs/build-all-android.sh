#!/usr/bin/env bash

set -euvx

# Our short-names for the architectures.
TARGET_ARCHS=("x86_64" "arm64" "arm")
# The directories required for the Android-Gradle plugin and APK
# layout, like `jniLibs/x86_64` or `lib/x86_64` respectively.
TARGET_ARCHS_DISTS=("x86_64" "arm64-v8a" "armeabi-v7a")
# The corresponding Rust target names.
TARGET_ARCHS_TOOLCHAINS=("x86_64-linux-android" "aarch64-linux-android" "arm-linux-androideabi")

# End of configuration.

if [[ "${#}" -ne 1 ]]
then
    echo "Usage:"
    echo "./build-all-android.sh <NSS_SRC_PATH>"
    exit 1
fi

# shellcheck disable=SC1091
source "android_defaults.sh"
NSS_SRC_PATH=${1}

echo "# Building NSS"
for i in "${!TARGET_ARCHS[@]}"; do
  DIST=${TARGET_ARCHS_DISTS[${i}]}
  DIST_DIR=$(abspath "android/${DIST}/nss")
  if [[ -d "${DIST_DIR}" ]]; then
    echo "${DIST_DIR} already exists. Skipping building nss."
  else
    ./build-nss-android.sh "${NSS_SRC_PATH}" "${DIST_DIR}" "${TARGET_ARCHS_TOOLCHAINS[${i}]}" "${ANDROID_NDK_API_VERSION}" || exit 1
  fi
done
