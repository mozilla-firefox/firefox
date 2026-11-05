#!/bin/bash -vex

set -x -e

echo "running as" $(id)

set -v

cd $GECKO_PATH

. taskcluster/scripts/misc/android-gradle-dependencies/before.sh

export MOZCONFIG=mobile/android/config/mozconfigs/android-arm-gradle-dependencies/nightly
./mach build

./mach android gradle-dependencies

if [ -z "$MOZ_APPSERVICES_IN_TREE" ]; then
    ./mach gradle assembleNimbusTools
fi

. taskcluster/scripts/misc/android-gradle-dependencies/after.sh
