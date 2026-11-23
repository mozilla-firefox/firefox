#!/usr/bin/env bash

# Copies the static libraries to link NSS with from a `build.sh --static` dist: those
# that its pkg-config files name, and mozpkix, which no .pc names but rc_crypto
# links for verifying content signatures. Also copies the pkg-config files, from
# which nss_build_common reads the same list.
# Usage: ./copy-nss-libs.sh <BUILD_LIB_DIR> <DIST_LIB_DIR>

set -euo pipefail

if [[ "${#}" -ne 2 ]]; then
  echo "Usage: ./copy-nss-libs.sh <BUILD_LIB_DIR> <DIST_LIB_DIR>"
  exit 1
fi

BUILD_LIB_DIR=${1}
DIST_LIB_DIR=${2}
# libsqlite3-sys supplies SQLite.
PROVIDED_ELSEWHERE=(sqlite)

# The -l names in the Libs: line of a .pc.
pc_libs() {
  sed -n 's/^Libs:[[:space:]]*//p' "${1}" | tr ' ' '\n' | sed -n 's/^-l//p'
}

# Checked here because a failure inside the $(...) below would not stop the script.
for pc in nss-static.pc nspr.pc; do
  if [[ ! -f "${BUILD_LIB_DIR}/pkgconfig/${pc}" ]]; then
    echo "error: no ${BUILD_LIB_DIR}/pkgconfig/${pc}; is this NSS 3.129 or later, built with build.sh --static?"
    exit 1
  fi
done

libs=$(
  pc_libs "${BUILD_LIB_DIR}/pkgconfig/nss-static.pc"
  pc_libs "${BUILD_LIB_DIR}/pkgconfig/nspr.pc"
  echo mozpkix
)

mkdir -p "${DIST_LIB_DIR}/pkgconfig"
cp -p -L "${BUILD_LIB_DIR}/pkgconfig/nss-static.pc" "${BUILD_LIB_DIR}/pkgconfig/nspr.pc" "${DIST_LIB_DIR}/pkgconfig"
for lib in ${libs}; do
  if [[ " ${PROVIDED_ELSEWHERE[*]} " != *" ${lib} "* ]]; then
    cp -p -L "${BUILD_LIB_DIR}/lib${lib}.a" "${DIST_LIB_DIR}"
  fi
done
