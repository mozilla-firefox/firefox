#!/usr/bin/env bash

# Fails if an NSS static library that was built but not copied into the dist directory defines symbols that
# the copied libraries leave undefined.
# Usage: NM=<nm> ./check-nss-libs.sh <BUILD_LIB_DIR> <DIST_LIB_DIR>

set -euo pipefail

if [[ "${#}" -ne 2 ]]; then
  echo "Usage: ./check-nss-libs.sh <BUILD_LIB_DIR> <DIST_LIB_DIR>"
  exit 1
fi

BUILD_LIB_DIR=${1}
DIST_LIB_DIR=${2}
NM=${NM:-nm}
PROVIDED_ELSEWHERE=(libsqlite.a)

symbols() {
  "${NM}" -g "$@" | awk 'NF >= 2 { print $(NF-1), $NF }'
}

dist_symbols=$(symbols "${DIST_LIB_DIR}"/*.a)
if [[ -z "${dist_symbols}" ]]; then
  echo "error: ${NM} listed no symbols in ${DIST_LIB_DIR}"
  exit 1
fi

defined() { awk '$1 != "U" && $1 != "w" && $1 != "v" { print $2 }' | sort -u; }
undefined=$(awk '$1 == "U" { print $2 }' <<<"${dist_symbols}" | sort -u)
unresolved=$(comm -23 <(echo "${undefined}") <(defined <<<"${dist_symbols}"))

status=0
for lib in "${BUILD_LIB_DIR}"/*.a; do
  name=$(basename "${lib}")
  if [[ -e "${DIST_LIB_DIR}/${name}" ]] || [[ " ${PROVIDED_ELSEWHERE[*]} " == *" ${name} "* ]]; then
    continue
  fi
  provided=$(comm -12 <(symbols "${lib}" | defined) <(echo "${unresolved}") | head -3 | tr '\n' ' ')
  if [[ -n "${provided}" ]]; then
    echo "error: ${name} is built but not copied to ${DIST_LIB_DIR}; it defines ${provided}"
    status=1
  fi
done
exit "${status}"
