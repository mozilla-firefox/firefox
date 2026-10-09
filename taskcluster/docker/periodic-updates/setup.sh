#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

set -ve

apt-get update -q
apt-get install -y --no-install-recommends \
    curl \
    jq \
    libasound2 \
    libgtk-3-0 \
    libx11-xcb1 \
    libxml2-utils \
    libxt6 \
    libxtst6 \
    shellcheck \
    unzip \
    bzip2 \
    wget

uv tool install MozPhab==2.19.0

# turn off update checks
cat >"$HOME"/.moz-phab-config<<EOF
[updater]
self_last_check = -1
self_auto_update = False
EOF

# moz-phab requires some hg config even though it's not used
cat >"$HOME"/.hgrc<<EOF
[ui]
username = hg user <user@example.com>
EOF

rm -rf /setup
