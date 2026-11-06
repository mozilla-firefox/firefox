#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# Create an NSS certificate database for ssl_gtest.
# Usage: ssl_gtest_db.sh [db_dir [certutil [noise_file]]]
#   db_dir     - directory for the NSS cert DB (default: ./ssl_gtest_certdb)
#   certutil   - path to certutil binary (default: found in PATH)
#   noise_file - entropy file for key generation (default: auto-generated)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

PROFILEDIR="${1:-./ssl_gtest_certdb}"
CERTUTIL="${2:-}"
R_NOISE_FILE="${3:-}"

html_msg() { :; }

. "${SCRIPT_DIR}/../common/certsetup.sh"

# Use certutil from PATH if not provided
if [ -z "$CERTUTIL" ]; then
    CERTUTIL="$(command -v certutil 2>/dev/null || true)"
fi
if [ -z "$CERTUTIL" ]; then
    echo "certutil not found; pass it as \$2 or ensure it is in PATH" >&2
    exit 1
fi
BINDIR="$(dirname "$CERTUTIL")"

# Auto-generate a noise file if not provided
if [ -z "$R_NOISE_FILE" ]; then
    _noise_tmp="$(mktemp)"
    trap 'rm -f "$_noise_tmp"' EXIT
    dd if=/dev/urandom of="$_noise_tmp" bs=2048 count=1 2>/dev/null
    R_NOISE_FILE="$_noise_tmp"
fi

mkdir -p "$PROFILEDIR"
PROFILEDIR="$(cd "$PROFILEDIR" && pwd)"
"$CERTUTIL" -N -d "$PROFILEDIR" --empty-password

cd "$PROFILEDIR"

counter=0
make_cert client rsa sign
make_cert rsa rsa sign kex
make_cert rsa2048 rsa2048 sign kex
make_cert rsa8192 rsa8192 sign kex
make_cert rsa_sign rsa sign
make_cert rsa_pss rsapss sign
make_cert rsa_pss384 rsapss384 sign
make_cert rsa_pss512 rsapss512 sign
make_cert rsa_pss_noparam rsapss_noparam sign
make_cert rsa_decrypt rsa kex
make_cert ecdsa256 p256 sign
make_cert ecdsa384 p384 sign
make_cert ecdsa521 p521 sign
make_cert mldsa44 mldsa44 sign
make_cert mldsa65 mldsa65 sign
make_cert mldsa87 mldsa87 sign
make_cert ecdh_ecdsa p256 kex
make_cert rsa_ca rsa_ca ca
make_cert rsa_chain rsa_chain sign
make_cert rsa_pss_ca rsapss_ca ca
make_cert rsa_pss_chain rsapss_chain sign
make_cert rsa_ca_rsa_pss_chain rsa_ca_rsapss_chain sign
make_cert mldsa44_ca mldsa44_ca ca
make_cert mldsa44_chain mldsa44_chain sign
make_cert mldsa65_ca mldsa65_ca ca
make_cert mldsa65_chain mldsa65_chain sign
make_cert mldsa87_ca mldsa87_ca ca
make_cert mldsa87_chain mldsa87_chain sign
make_cert ecdh_rsa ecdh_rsa kex
if [ -z "${NSS_DISABLE_DSA}" ]; then
    make_cert dsa dsa sign
fi
make_cert delegator_ecdsa256 delegator_p256 sign
make_cert delegator_rsae2048 delegator_rsae2048 sign
make_cert delegator_rsa_pss2048 delegator_rsa_pss2048 sign
make_cert delegator_mldsa44 delegator_mldsa44 sign
make_cert delegator_mldsa65 delegator_mldsa65 sign
make_cert delegator_mldsa87 delegator_mldsa87 sign
make_cert unknown_ext unknown_ext sign
make_cert unknown_critical_ext unknown_critical_ext sign
