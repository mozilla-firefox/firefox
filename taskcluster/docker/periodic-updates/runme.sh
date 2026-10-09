#!/bin/bash

set -xe

# Things to be set by task definition.
# --pinset --hsts --hpkp
# -b branch
# --use-mozilla-central
# Artifact directory
# Artifact names.


test "${BRANCH}"

PARAMS=""

if [ -n "${USE_MOZILLA_CENTRAL}" ]
then
  PARAMS="${PARAMS} --use-mozilla-central"
fi

# TODO change these, so that they're run if the artifact location is specified?
if [ -n "${DO_HSTS}" ]
then
  PARAMS="${PARAMS} --hsts"
fi

if [ -n "${DO_HPKP}" ]
then
  PARAMS="${PARAMS} --hpkp"
fi

if [ -n "${DO_REMOTE_SETTINGS}" ]
then
  PARAMS="${PARAMS} --remote-settings"
fi

if [ -n "${DO_SUFFIX_LIST}" ]
then
  PARAMS="${PARAMS} --suffix-list"
fi

if [ -n "${DO_MOBILE_EXPERIMENTS}" ]
then
  PARAMS="${PARAMS} --mobile-experiments"
fi

if [ -n "${DO_MOBILE_MERINO_MANIFEST}" ]
then
  PARAMS="${PARAMS} --mobile-merino-manifest"
fi

if [ -n "${DO_CT_LOGS}" ]
then
  PARAMS="${PARAMS} --ct-logs"
fi

if [ -n "${DONTBUILD}" ]
then
  PARAMS="${PARAMS} -d"
fi

if [ "${BRANCH}" = try ]; then
  PARAMS="${PARAMS} --skip-push"
fi

export ARTIFACTS_DIR="/home/worker/artifacts"
mkdir -p "$ARTIFACTS_DIR"

# duplicate the functionality of taskcluster-lib-urls, but in bash..
queue_base="$TASKCLUSTER_ROOT_URL/api/queue/v1"

# Get Arcanist API token

if [ -n "${TASK_ID}" ]
then
  curl --location --retry 10 --retry-delay 10 -o /home/worker/task.json "$queue_base/task/$TASK_ID"
  ARC_SECRET=$(jq -r '.scopes[] | select(contains ("arc-phabricator-token"))' /home/worker/task.json | awk -F: '{print $3}')
fi
if [ -n "${ARC_SECRET}" ] && getent hosts taskcluster
then
  set +x # Don't echo these
  secrets_url="${TASKCLUSTER_PROXY_URL}/api/secrets/v1/secret/${ARC_SECRET}"
  SECRET=$(curl "${secrets_url}")
  TOKEN=$(echo "${SECRET}" | jq -r '.secret.token')
elif [ -n "${ARC_TOKEN}" ] # Allow for local testing.
then
  TOKEN="${ARC_TOKEN}"
fi

if [ -n "${TOKEN}" ]
then
  cat >"${HOME}/.arcrc" <<END
{
  "hosts": {
    "https://phabricator.services.mozilla.com/api/": {
      "token": "${TOKEN}"
    }
  }
}
END
  set -x
  chmod 600 "${HOME}/.arcrc"
fi

export HGPLAIN=1

# shellcheck disable=SC2086
/home/worker/scripts/periodic_file_updates.sh -b "${BRANCH}" -a ${PARAMS} -t "${GECKO_PATH}"
