#!/usr/bin/env bash

# shellcheck disable=SC2148
# Ensure the build toolchains are set up correctly for android builds.
#
# This is intended for use in CI, so it verifies only the minimum that is needed
# to build in CI. For local development use `verify-android-environment.sh`.
#
# This file should be used via `./libs/verify-android-ci-environment.sh`.

set -e

RUST_TARGETS=("aarch64-linux-android" "armv7-linux-androideabi" "x86_64-linux-android")

if [[ ! -f "$(pwd)/libs/build-all.sh" ]]; then
  echo "ERROR: verify-android-ci-environment.sh should be run from the root directory of the repo"
  exit 1
fi

"$(pwd)/libs/verify-common.sh"

# If you add a dependency below, mention it in building.md in the Android section!

if [[ -z "${ANDROID_HOME}" ]]; then
  echo "Could not find Android SDK:"
  echo 'Please install the Android SDK and then set ANDROID_HOME.'
  exit 1
fi

rustup target add "${RUST_TARGETS[@]}"

# Determine the Java command to use to start the JVM.
# Same implementation as gradlew
if [[ -n "$JAVA_HOME" ]] ; then
    if [[ -x "$JAVA_HOME/jre/sh/java" ]] ; then
        # IBM's JDK on AIX uses strange locations for the executables
        JAVACMD="$JAVA_HOME/jre/sh/java"
    else
        JAVACMD="$JAVA_HOME/bin/java"
    fi
    if [[ ! -x "$JAVACMD" ]] ; then
        die "ERROR: JAVA_HOME is set to an invalid directory: $JAVA_HOME

Please set the JAVA_HOME variable in your environment to match the
location of your Java installation."
    fi
else
    JAVACMD="java"
    command -v $JAVACMD >/dev/null 2>&1 || die "ERROR: JAVA_HOME is not set and no 'java' command could be found in your PATH.

Please set the JAVA_HOME variable in your environment to match the
location of your Java installation."
fi

JAVA_VERSION=$("$JAVACMD" -version 2>&1 | grep -i version | cut -d'"' -f2 | cut -d'.' -f1-2)
if [[ "${JAVA_VERSION}" != "25.0" ]]; then
  echo "Incompatible java version: ${JAVA_VERSION}. JDK 25 must be installed."
  echo "Try switching versions and re-running. Using sdkman: sdk install java 25.0.5.0+8-tem || sdk use 25.0.5.0+8-tem"
  exit 1
fi

# NDK ez-install
if [[ ! -d "$ANDROID_HOME/cmdline-tools" ]]; then
  echo "Android SDK is missing command line tools. Install them via Android Studio -> SDK Manager -> SDK Tools."
  exit 1
fi
#"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" "ndk;$(./gradlew -q printNdkVersion | tail -1)"

# CI just downloads these libs anyway.
if [[ -z "${CI}" ]]; then
  if [[ ! -d "${PWD}/libs/android/arm64-v8a/nss" ]]; then
    pushd libs || exit 1
    ./build-all.sh android
    popd || exit 1
  fi
fi
