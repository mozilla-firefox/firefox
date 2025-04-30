#!/bin/bash -vex

set -x -e

echo "running as" $(id)

set -v

cd $GECKO_PATH

export PATH=$PATH:$MOZ_FETCHES_DIR/node/bin

./mach configure --disable-compile-environment
./mach npm ci --prefix tools/lint/stylelint/stylelint-plugin-mozilla

# We have tools/lint/stylelint/stylelint-plugin-mozilla/{node_modules,...} and want
# stylelint-plugin-mozilla/{node_modules}.
mkdir -p /builds/worker/artifacts
cd tools/lint/stylelint/
tar caf /builds/worker/artifacts/stylelint-plugin-mozilla.tar.zst stylelint-plugin-mozilla
