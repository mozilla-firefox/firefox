#!/bin/bash -vex

set -x -e

echo "running as" $(id)

: WORKSPACE ${WORKSPACE:=/builds/worker/workspace}

set -v

# Resolved before the `pushd` below leaves the source directory. The
# inventories are collected here by `mach android gradle-dependencies`, one per
# enumeration pass.
DEPENDENCY_INVENTORIES="$PWD/gradle/dependency-inventories"
VERIFY_DEPENDENCIES="$PWD/taskcluster/scripts/misc/android-gradle-dependencies/verify_dependencies.py"

# Package everything up.
pushd $WORKSPACE
mkdir -p /builds/worker/artifacts android-gradle-dependencies

# NEXUS_WORK is exported by `before.sh`.
cp -R ${NEXUS_WORK}/storage/mozilla android-gradle-dependencies
cp -R ${NEXUS_WORK}/storage/central android-gradle-dependencies
cp -R ${NEXUS_WORK}/storage/google android-gradle-dependencies
cp -R ${NEXUS_WORK}/storage/gradle-plugins android-gradle-dependencies

# The Gradle wrapper will have downloaded and verified the hash of exactly one
# Gradle distribution.  It will be located in $GRADLE_USER_HOME, like
# ~/.gradle/wrapper/dists/gradle-8.5-bin/$PROJECT_HASH/gradle-8.5.  We
# want to remove the version from the internal directory for use via tooltool in
# a mozconfig.
cp -a ${GRADLE_USER_HOME}/wrapper/dists/gradle-*-*/*/gradle-*/ android-gradle-dependencies/gradle-dist

# Uploaded before the check below, so that a failure ships the evidence for it.
cp -R "$DEPENDENCY_INVENTORIES" /builds/worker/artifacts/
mkdir -p /builds/worker/artifacts/nexus-logs
cp -R ${NEXUS_WORK}/logs/* /builds/worker/artifacts/nexus-logs/ || true
ls -l /builds/worker/artifacts/nexus-logs/

# Catch an incomplete artifact here rather than downstream, where it surfaces
# much later as a confusing resolution failure. Checked before the packaging
# below so that a failure doesn't pay for compressing the artifact first.
#
# GeckoView is built by this task and published to a local Maven repository, so
# the passes rooted at the standalone Gradle builds resolve it from there. It is
# not a cached dependency and is deliberately not packaged.
#
# The Nimbus Gradle plugin resolves nimbus-fml from ivy repositories, which
# Nexus doesn't proxy; it is packaged from the Gradle cache below instead.
python3 "$VERIFY_DEPENDENCIES" --inventories "$DEPENDENCY_INVENTORIES" \
    --unproxied org/mozilla/geckoview \
    --unproxied org/mozilla/appservices/tools/nimbus-fml \
    android-gradle-dependencies/mozilla \
    android-gradle-dependencies/central \
    android-gradle-dependencies/google \
    android-gradle-dependencies/gradle-plugins

# Laid out like archive.mozilla.org, which NIMBUS_FML_REPOSITORY expects.
if [ -z "$MOZ_APPSERVICES_IN_TREE" ]; then
    for zip in ${GRADLE_USER_HOME}/caches/modules-2/files-2.1/org.mozilla.appservices.tools/nimbus-fml/*/*/*.zip; do
        version=$(basename "$(dirname "$(dirname "$zip")")")
        mkdir -p "android-gradle-dependencies/nimbus-fml/$version"
        cp "$zip" "android-gradle-dependencies/nimbus-fml/$version/nimbus-fml.zip"
    done
fi

tar cavf /builds/worker/artifacts/android-gradle-dependencies.tar.zst android-gradle-dependencies

popd
