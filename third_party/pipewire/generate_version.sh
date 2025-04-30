#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

set -Eeuo pipefail
version_string=$(awk -F"'" '/^[[:space:]]*version[[:space:]]*:[[:space:]]*/ {print $2; exit}' meson.build)
version_major=$(echo $version_string | cut -d. -f1)
version_minor=$(echo $version_string | cut -d. -f2)
version_micro=$(echo $version_string | cut -d. -f3)

input_file="src/pipewire/version.h.in"
result_file="src/pipewire/version.h"

sed -e "s/@PIPEWIRE_VERSION_MAJOR@/$version_major/g" \
    -e "s/@PIPEWIRE_VERSION_MINOR@/$version_minor/g" \
    -e "s/@PIPEWIRE_VERSION_MICRO@/$version_micro/g" \
    $input_file > $result_file
