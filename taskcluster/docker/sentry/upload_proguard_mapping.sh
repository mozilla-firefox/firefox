#!/bin/bash

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

set -o nounset
set -o pipefail

MAPPING_FILE="$1"
UUID_FILE="$2"

run() {
    local proguard_uuid
    proguard_uuid=$(cat "$UUID_FILE") || return 1
    local secret
    secret=$(curl --silent --fail "http://taskcluster/secrets/v1/secret/$SENTRY_SECRET") || return 1
    local sentry_auth_token sentry_org sentry_project sentry_url
    sentry_auth_token=$(echo "$secret" | jq -r ".secret.sentryToken")
    sentry_org=$(echo "$secret" | jq -r ".secret.sentryOrg")
    sentry_project=$(echo "$secret" | jq -r ".secret.sentryProject")
    sentry_url=$(echo "$secret" | jq -r ".secret.sentryUrl")

    SENTRY_AUTH_TOKEN="$sentry_auth_token" sentry-cli \
        --url "$sentry_url" \
        --org "$sentry_org" \
        dif upload \
        --type proguard \
        --uuid "$proguard_uuid" \
        --project "$sentry_project" \
        "$MAPPING_FILE" || return 1
}

with_backoff() {
    local failures=0
    while ! "$@"; do
        failures=$(( failures + 1 ))
        if (( failures >= 5 )); then
            echo "[with_backoff] Unable to succeed after 5 tries, failing the job."
            return 1
        else
            local seconds=$(( 2 ** (failures - 1) ))
            echo "[with_backoff] Retrying in $seconds second(s)"
            sleep "$seconds"
        fi
    done
}

with_backoff run
