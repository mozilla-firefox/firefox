#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

set -vex

. ./updatebot-version.sh # Get UPDATEBOT_REVISION

export DEBIAN_FRONTEND=noninteractive

# Update apt-get lists
apt-get update -y

# Install dependencies
apt-get install -y --no-install-recommends \
    ca-certificates \
    cloudsql-proxy \
    curl \
    ed \
    golang-go \
    gcc \
    libc6-dev \
    meson \
    php-cli \
    php-curl \
    python3-minimal \
    python3-wheel \
    python3-pip \
    python3-venv \
    python3-requests \
    python3-requests-unixsocket \
    python3-setuptools \
    openssh-client \
    rsync \
    unzip \
    wget

mkdir -p /builds/worker/.mozbuild
chown -R worker:worker /builds/worker/
export GOPATH=/builds/worker/go

# Install specific version of Arcanist
pushd /builds/worker/.mozbuild/
git clone https://github.com/phacility/arcanist.git
cd arcanist
git checkout e50d1bc4eabac9c37e3220e9f3fb8e37ae20b957
# Show the response when Phabricator (or something in front of it) answers a
# Conduit call with something other than JSON.
git apply /setup/arcanist_log_invalid_json.patch
# Send Conduit requests uncompressed: Phabricator's PHP parses a gzipped form
# body before decompressing it, and large ones fail on max_input_vars.
git apply /setup/arcanist_no_gzip.patch
ln -s /builds/worker/.mozbuild/arcanist/bin/arc /usr/local/bin/arc
popd

. install-node-for-pdfjs.sh

# Install the Claude Code CLI, used for AI-assisted tasks (e.g. attempting a
# vendor when `./mach vendor` fails). The launcher and its platform binary are
# pinned by the committed claude-code/package-lock.json and integrity-verified
# by `npm ci`, which aborts the build on any mismatch; the auto-updater is
# disabled via DISABLE_AUTOUPDATER (see Dockerfile). So the code we execute is
# reproducible. Requires ANTHROPIC_API_KEY at runtime.
mkdir -p /builds/worker/.mozbuild/claude-code
cp claude-code/package.json claude-code/package-lock.json /builds/worker/.mozbuild/claude-code/
pushd /builds/worker/.mozbuild/claude-code
npm ci
ln -s /builds/worker/.mozbuild/claude-code/node_modules/.bin/claude /usr/local/bin/claude
popd
claude --version  # verify

# pdf.js setup
# We want to aviod downloading a ton of packages all the time, so
# we will preload the pdf.js repo (and packages) in the Docker image
# and only update it at runtime. This means that the `./mach vendor`
# behavior for pdf.js will also be kind of custom
npm install -g gulp-cli
cd /builds/worker/
git clone https://github.com/mozilla/pdf.js.git
cd /builds/worker/pdf.js
npm ci --legacy-peer-deps

# seed a v8 repository because it's large, and doing an update will
# be much faster than a new clone each time.
cd /builds/worker/
git clone https://github.com/v8/v8.git

# Check out source code
cd /builds/worker/
git clone https://github.com/mozilla-services/updatebot.git
cd updatebot
git checkout "$UPDATEBOT_REVISION"

# Set up dependencies
cd /builds/worker/
chown -R worker:worker .
chown -R worker:worker .*

python3 -m pip install --break-system-packages -U pip==26.2.2
python3 -m pip install --break-system-packages poetry==2.1.1
python3 -m pip install --break-system-packages --only-binary :all: --require-hashes -r /setup/evolve-requirements.txt

rm -rf /setup
