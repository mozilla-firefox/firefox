#!/bin/bash

set -ex

mkdir -p /builds/worker/artifacts/
# for PGO logs
export UPLOAD_PATH=/builds/worker/artifacts/

mkdir -p /builds/worker/.local/state/snapcraft/

BRANCH=$1
ARCH=$2
DEBUG=${3:-0}

SOURCE_REPO=${SOURCE_REPO:-https://github.com/canonical/firefox-snap/}
SOURCE_BRANCH=${SOURCE_BRANCH:-${BRANCH}}

export LC_ALL=C.UTF-8
export LANG=C.UTF-8
export SNAP_ARCH=amd64
export SNAPCRAFT_BUILD_INFO=1

export PATH=$PATH:$HOME/.local/bin/
unset MOZ_AUTOMATION

MOZCONFIG=mozconfig.in

USE_SNAP_FROM_STORE_OR_MC=${USE_SNAP_FROM_STORE_OR_MC:-0}

TRY=0
if [ "${BRANCH}" = "try" ]; then
  if [ "${SOURCE_BRANCH}" = "try" ]; then
    # This would happen when e.g.: mach try fuzzy -q "'snap 'build 'try"
    # In this case we want to default to nightly from upstream, except if the
    # user passes a SOURCE_BRANCH, with e.g.:
    # SOURCE_REPO=... SOURCE_BRANCH=... mach try fuzzy -q "'snap 'build 'try"
    SOURCE_BRANCH=nightly
  fi
  TRY=1
fi

if [ "${USE_SNAP_FROM_STORE_OR_MC}" = "0" ]; then
  # ESR currently still has a hard dependency against zstandard==0.17.0 so
  # install this specific version here
  if [ "${BRANCH}" = "esr" ]; then
    sudo apt-get remove -y python3-zstandard && sudo apt-get install -y python3-pip && sudo pip3 install --no-input zstandard==0.17.0
    MOZCONFIG=mozconfig
  fi

  # Stable and beta runs out of file descriptors during link with gold
  ulimit -n 65536

  git clone --single-branch --depth 1 --branch "${SOURCE_BRANCH}" "${SOURCE_REPO}" firefox-snap/
  cd firefox-snap/

  if [ "${TRY}" = "1" ]; then
    # Symlink so that we can directly re-use Gecko mercurial checkout
    ln -s /builds/worker/checkouts/gecko gecko
  fi

  # Force an update to avoid the case of a stale docker image and repos updated
  # after
  sudo apt-get update

  # shellcheck disable=SC2046
  sudo apt-get install -y $(/usr/bin/python3 /builds/worker/parse.py snapcraft.yaml "${ARCH}")

  if [ -d "/builds/worker/patches/${BRANCH}/" ]; then
    for p in /builds/worker/patches/"${BRANCH}"/*.patch; do
      patch -p1 < "$p"
    done;
  fi

  if [ "${TRY}" = "1" ]; then
    # don't remove hg source, and don't force changeset so we get correct stamp
    # still force repo because the try clone is from mozilla-unified but the
    # generated link does not work
    sed -ri 's|rm -rf .hg||g' snapcraft.yaml
    # shellcheck disable=SC2016
    sed -ri 's|MOZ_SOURCE_REPO=\$\{REPO\}|MOZ_SOURCE_REPO=${GECKO_HEAD_REPOSITORY}|g' snapcraft.yaml
    # shellcheck disable=SC2016
    sed -ri 's|MOZ_SOURCE_CHANGESET=\$\{REVISION\}|MOZ_SOURCE_CHANGESET=${GECKO_HEAD_REV}|g' snapcraft.yaml
    # shellcheck disable=SC2016
    sed -ri 's|hg clone --stream \$REPO -u \$REVISION|cp -r \$SNAPCRAFT_PROJECT_DIR/gecko/. |g' snapcraft.yaml
  fi

  if [ "${DEBUG}" = "--debug" ]; then
    {
      echo "ac_add_options --enable-debug"
      echo "ac_add_options --disable-install-strip"
    } >> ${MOZCONFIG}
    echo "MOZ_DEBUG=1" >> ${MOZCONFIG}

    # No PGO on debug builds
    sed -ri 's/ac_add_options --enable-linker=gold//g' snapcraft.yaml
    sed -ri 's/ac_add_options --enable-lto=cross//g' snapcraft.yaml
    sed -ri 's/ac_add_options MOZ_PGO=1//g' snapcraft.yaml
  fi

  # Until launchpad is able to handle platforms definition, the snapcraft yaml
  # hides them and we want to unhide.
  sed -ri 's/^##CROSS-COMPILATION##//g' snapcraft.yaml

  MAX_MEMORY_GB=$(free -g | awk '/Mem:/ { print $2 - 1 }')

  # setting parallelism does not work with core24 ?
  #
  # SNAPCRAFT_BUILD_ENVIRONMENT_CPU=$(nproc) \
  # SNAPCRAFT_PARALLEL_BUILD_COUNT=$(nproc) \
  # CRAFT_PARALLEL_BUILD_COUNT=$(nproc) \

  # Get the value and overwrite the snap's content.
  MAX_CPUS=$(nproc)
  sed -ri "s|\\\$CRAFT_PARALLEL_BUILD_COUNT|${MAX_CPUS}|g" snapcraft.yaml
  grep "make -j" snapcraft.yaml

  SNAPCRAFT_BUILD_ENVIRONMENT_MEMORY="${MAX_MEMORY_GB}G" \
    snapcraft --destructive-mode --verbosity verbose --build-for "${ARCH}"
elif [ "${USE_SNAP_FROM_STORE_OR_MC}" = "store" ]; then
  mkdir from-snap-store && cd from-snap-store

  CHANNEL="${BRANCH}"
  if [ "${CHANNEL}" = "try" ] || [ "${CHANNEL}" = "nightly" ]; then
    CHANNEL=edge
  fi;

  snap download --channel="${CHANNEL}" firefox
  SNAP_DEBUG_NAME=$(find . -maxdepth 1 -type f -name "firefox*.snap" | sed -e 's/\.snap$/.debug/g')
  touch "${SNAP_DEBUG_NAME}"
else
  mkdir from-mc && cd from-mc

  # index.gecko.v2.mozilla-central.latest.firefox.amd64-esr-debug
  #  => https://firefox-ci-tc.services.mozilla.com/api/index/v1/task/gecko.v2.mozilla-central.latest.firefox.amd64-esr-debug/artifacts/public%2Fbuild%2Ffirefox.snap
  # index.gecko.v2.mozilla-central.revision.bf0897ec442e625c185407cc615a6adc0e40fa75.firefox.amd64-esr-debug
  #  => https://firefox-ci-tc.services.mozilla.com/api/index/v1/task/gecko.v2.mozilla-central.revision.bf0897ec442e625c185407cc615a6adc0e40fa75.firefox.amd64-esr-debug/artifacts/public%2Fbuild%2Ffirefox.snap
  # index.gecko.v2.mozilla-central.latest.firefox.amd64-nightly
  #  => https://firefox-ci-tc.services.mozilla.com/api/index/v1/task/gecko.v2.mozilla-central.latest.firefox.amd64-nightly/artifacts/public%2Fbuild%2Ffirefox.snap
  # index.gecko.v2.mozilla-central.revision.bf0897ec442e625c185407cc615a6adc0e40fa75.firefox.amd64-nightly
  #  => https://firefox-ci-tc.services.mozilla.com/api/index/v1/task/gecko.v2.mozilla-central.revision.bf0897ec442e625c185407cc615a6adc0e40fa75.firefox.amd64-nightly/artifacts/public%2Fbuild%2Ffirefox.snap

  # Remove "-" so we get e.g., esr128 from esr-128
  INDEX_NAME=${BRANCH//-/}
  if [ "${INDEX_NAME}" = "try" ]; then
    INDEX_NAME=nightly
  fi;

  if [ "${DEBUG}" = "--debug" ]; then
    INDEX_NAME="${INDEX_NAME}-debug"
  fi;

  TASKCLUSTER_API_ROOT="https://firefox-ci-tc.services.mozilla.com/api"

  URL_TASK="${TASKCLUSTER_API_ROOT}/index/v1/task/gecko.v2.mozilla-central.${USE_SNAP_FROM_STORE_OR_MC}.firefox.${ARCH}-${INDEX_NAME}"
  PKGS_TASK_ID=$(curl "${URL_TASK}" | jq -r '.taskId')

  if [ -z "${PKGS_TASK_ID}" ]; then
    echo "Failure to find matching taskId for ${USE_SNAP_FROM_STORE_OR_MC} + ${INDEX_NAME}"
    exit 1
  fi

  PKGS_URL="${TASKCLUSTER_API_ROOT}/queue/v1/task/${PKGS_TASK_ID}/artifacts"
  for pkg in $(curl "${PKGS_URL}" | jq -r '.artifacts | . [] | select(.name | contains("public/build/firefox_")) | .name');
  do
    url="${TASKCLUSTER_API_ROOT}/queue/v1/task/${PKGS_TASK_ID}/artifacts/${pkg}"
    target_name="$(basename "${pkg}")"
    echo "$url => $target_name"
    curl -SL "${url}" -o "${target_name}"
  done;
fi

cp ./*.snap ./*.debug /builds/worker/artifacts/

# Those are for fetches usage by the test task
cp ./*.snap /builds/worker/artifacts/firefox.snap
cp ./*.debug /builds/worker/artifacts/firefox.debug

# Those are for running snap-upstream-test
cd /builds/worker/checkouts/gecko/taskcluster/docker/snap-coreXX-build/snap-tests/ && zip -r9 /builds/worker/artifacts/snap-tests.zip ./*
