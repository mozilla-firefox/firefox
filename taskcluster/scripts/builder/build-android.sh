#!/bin/sh

set -e
set -x

mozconfig=$(mktemp)
cat > $mozconfig <<EOF
# the corresponding geckoview's mozconfig, to pick up its config options
. $MOZCONFIG
# no-compile because we don't need to build native code here
. $GECKO_PATH/build/mozconfig.no-compile

# Disable Keyfile Loading (and checks)
# This overrides the settings in the common android mozconfig
ac_add_options --without-mozilla-api-keyfile
ac_add_options --without-google-safebrowsing-api-keyfile

ac_add_options --disable-nodejs
unset NODEJS

export GRADLE_MAVEN_REPOSITORIES="file://$MOZ_FETCHES_DIR/geckoview","file://$MOZ_FETCHES_DIR/android-gradle-dependencies/mozilla","file://$MOZ_FETCHES_DIR/android-gradle-dependencies/google","file://$MOZ_FETCHES_DIR/android-gradle-dependencies/central","file://$MOZ_FETCHES_DIR/android-gradle-dependencies/gradle-plugins"
EOF
export MOZCONFIG=$mozconfig
GRADLE=$MOZ_FETCHES_DIR/android-gradle-dependencies/gradle-dist/bin/gradle

export MOZ_OBJDIR=/builds/worker/workspace/obj-build

./mach configure

# Partial build ensures that `moz.build` generated code is in place.
./mach build pre-export export -v

eval $PRE_GRADLEW

eval $GET_SECRETS

if [ -n "$MOZ_GRADLE_USER_HOME_SEED" ]; then
    $GECKO_PATH/mach python $GECKO_PATH/mobile/android/gradle.py
fi

mkdir -p "$BUILD_METRICS_DIR"
$GRADLE $GRADLE_FLAGS listRepositories $GRADLEW_ARGS -PbuildMetricsOutputDir="$BUILD_METRICS_DIR"

eval $POST_GRADLEW
