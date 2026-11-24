#!/usr/bin/env bash

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/. */

# This must be run from the root webrender directory!
# Users may set the CARGOFLAGS environment variable to pass
# additional flags to cargo if desired.

set -o errexit
set -o nounset
set -o pipefail
set -o xtrace

CARGOFLAGS=${CARGOFLAGS:-""}  # default to empty if not set

python3 -m pip install -r $(dirname ${0})/requirements.txt

pushd wrench
# Test that all shaders compile successfully and pass tests.
python3 script/headless.py --precache test_init
python3 script/headless.py --precache --use-unoptimized-shaders test_init
python3 script/headless.py test_shaders

python3 script/headless.py reftest
python3 script/headless.py --compositor reftest reftests/compositor-layer
python3 script/headless.py rawtest
python3 script/headless.py test_invalidation
CXX=clang++ cargo run ${CARGOFLAGS} --release --features=software -- \
  --software --headless reftest
CXX=clang++ cargo run ${CARGOFLAGS} --release --features=software -- \
  --software --sw-compositor --headless reftest reftests/sw-compositor
popd
