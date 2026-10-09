#!/bin/bash

set -ex

function usage {
  cat <<EOF

Usage: $(basename "$0") -h # Displays this usage/help text
Usage: $(basename "$0") -x # lists exit codes
Usage: $(basename "$0") [-p product]
           # Use mozilla-central builds to check HSTS & HPKP
           [--use-mozilla-central]
           # Use archive.m.o instead of the taskcluster index to get xpcshell
           [--use-ftp-builds]
           # Use git rather than hg. Using git does not currently support cloning (use
           # --skip-clone as well).
           [--use-git]
           # One (or more) of the following actions must be specified.
           --hsts | --hpkp | --remote-settings | --suffix-list | --mobile-experiments | --mobile-merino-manifest | --ct-logs
           -b branch
           # The name of top source directory to use for the repository clone.
           [-t topsrcdir]
           # Skips cloning of the repository.
           [--skip-clone]
           # Performs a dry run - no commits are created.
           [-n]
           # Skips pushing of the repository - create a commit but does not try
           # to push it.
           [--skip-push]

EOF
}

# Defaults
PRODUCT="firefox"
DRY_RUN=false
CLOSED_TREE=false
DONTBUILD=false
APPROVAL=false

DO_PRELOAD_PINSET=false
DO_HSTS=false
DO_HPKP=false
DO_REMOTE_SETTINGS=false
DO_SUFFIX_LIST=false
DO_MOBILE_EXPERIMENTS=false
DO_MOBILE_MERINO_MANIFEST=false
DO_CT_LOGS=false

CLONE_REPO=true
HGHOST="hg.mozilla.org"
STAGEHOST="archive.mozilla.org"

USE_MC=false
USE_TC=true
USE_GIT=false
SKIP_PUSH=false

# Parse our command-line options.
while [ $# -gt 0 ]; do
  case "$1" in
    -h) usage; exit 0 ;;
    -p) PRODUCT="$2"; shift ;;
    -b) BRANCH="$2"; shift ;;
    -n) DRY_RUN=true ;;
    -c) CLOSED_TREE=true ;;
    -d) DONTBUILD=true ;;
    -a) APPROVAL=true ;;
    --pinset) DO_PRELOAD_PINSET=true ;;
    --hsts) DO_HSTS=true ;;
    --hpkp) DO_HPKP=true ;;
    --remote-settings) DO_REMOTE_SETTINGS=true ;;
    --suffix-list) DO_SUFFIX_LIST=true ;;
    --mobile-experiments) DO_MOBILE_EXPERIMENTS=true ;;
    --mobile-merino-manifest) DO_MOBILE_MERINO_MANIFEST=true ;;
    --ct-logs) DO_CT_LOGS=true ;;
    --skip-clone) CLONE_REPO=false ;;
    --skip-push) SKIP_PUSH=true ;;
    -t) TOPSRCDIR="$2"; shift ;;
    --use-mozilla-central) USE_MC=true ;;
    --use-ftp-builds) USE_TC=false ;;
    --use-git) USE_GIT=true ;;
    -*) usage
      exit 11 ;;
    *)  break ;; # terminate while loop
  esac
  shift
done

# Must supply a code branch to work with.
if [ "${BRANCH}" == "" ]; then
  echo "Error: You must specify a branch with -b branchname." >&2
  usage
  exit 12
fi

# Must choose at least one update action.
if [ "$DO_HSTS" == "false" ] && [ "$DO_HPKP" == "false" ] && [ "$DO_REMOTE_SETTINGS" == "false" ] && [ "$DO_SUFFIX_LIST" == "false" ] && [ "$DO_MOBILE_EXPERIMENTS" == false ] && [ "$DO_MOBILE_MERINO_MANIFEST" == false ] && [ "$DO_CT_LOGS" == false ]
then
  echo "Error: you must specify at least one action from: --hsts, --hpkp, --remote-settings, --suffix-list, --mobile-experiments, --mobile-merino-manifest, or --ct-logs" >&2
  usage
  exit 13
fi

# per-product constants
case "${PRODUCT}" in
  thunderbird)
    COMMIT_AUTHOR="tbirdbld <tbirdbld@thunderbird.net>"
    ;;
  firefox)
    ;;
  *)
    echo "Error: Invalid product specified"
    usage
    exit 14
    ;;
esac

if [ "${TOPSRCDIR}" == "" ]; then
  TOPSRCDIR="$(basename "${BRANCH}")"
fi

case "${BRANCH}" in
  try)
    # don't clone try, that can only end in sadness
    HGREPO="https://${HGHOST}/mozilla-central"
    ;;
  mozilla-central|comm-central )
    HGREPO="https://${HGHOST}/${BRANCH}"
    ;;
  mozilla-*|comm-* )
    HGREPO="https://${HGHOST}/releases/${BRANCH}"
    ;;
  * )
    HGREPO="https://${HGHOST}/projects/${BRANCH}"
    ;;
esac

BROWSER_ARCHIVE="target.tar.xz"
TESTS_ARCHIVE="target.common.tests.tar.zst"

UNPACK_CMD="tar xf"
COMMIT_AUTHOR='ffxbld <ffxbld@mozilla.com>'
WGET="wget -nv"
DIFF="$(command -v diff) -u"
JQ="$(command -v jq)"

if [ "${USE_GIT}" == "true" ]; then
  GIT="$(command -v git)"
else
  HG="$(command -v hg)"
fi

BASEDIR="${HOME}"
SCRIPTDIR="$(realpath "$(dirname "$0")")"
DATADIR="${BASEDIR}/data"

HSTS_PRELOAD_SCRIPT="${SCRIPTDIR}/getHSTSPreloadList.js"
HSTS_PRELOAD_ERRORS="nsSTSPreloadList.errors"
HSTS_PRELOAD_INC_OLD="${DATADIR}/nsSTSPreloadList.inc"
HSTS_PRELOAD_INC_NEW="${BASEDIR}/${PRODUCT}/nsSTSPreloadList.inc"
HSTS_UPDATED=false

HPKP_PRELOAD_SCRIPT="${SCRIPTDIR}/genHPKPStaticPins.js"
HPKP_PRELOAD_ERRORS="StaticHPKPins.errors"
HPKP_PRELOAD_JSON="${DATADIR}/PreloadedHPKPins.json"
HPKP_PRELOAD_INC="StaticHPKPins.h"
HPKP_PRELOAD_INPUT="${DATADIR}/${HPKP_PRELOAD_INC}"
HPKP_PRELOAD_OUTPUT="${DATADIR}/${HPKP_PRELOAD_INC}.out"
HPKP_UPDATED=false

REMOTE_SETTINGS_SERVER=''
REMOTE_SETTINGS_DIR="${TOPSRCDIR}/services/settings/dumps"
REMOTE_SETTINGS_UPDATED=false

PUBLIC_SUFFIX_URL="https://publicsuffix.org/list/public_suffix_list.dat"
PUBLIC_SUFFIX_LOCAL="public_suffix_list.dat"
HG_SUFFIX_LOCAL="effective_tld_names.dat"
HG_SUFFIX_PATH="/netwerk/dns/${HG_SUFFIX_LOCAL}"
PUBLIC_SUFFIX_END_MARKER="// ===END PRIVATE DOMAINS==="
SUFFIX_LIST_UPDATED=false

EXPERIMENTER_URL="https://experimenter.services.mozilla.com/api/v6/experiments-first-run/"
FENIX_INITIAL_EXPERIMENTS="mobile/android/fenix/app/src/main/res/raw/initial_experiments.json"
FOCUS_INITIAL_EXPERIMENTS="mobile/android/focus-android/app/src/main/res/raw/initial_experiments.json"
MOBILE_EXPERIMENTS_UPDATED=false

MOBILE_MERINO_MANIFEST_UPDATE_SCRIPT="${SCRIPTDIR}/update_mobile_merino_manifest.py"
MOBILE_MERINO_MANIFEST_UPDATED=false

CT_LOG_UPDATE_SCRIPT="${SCRIPTDIR}/getCTKnownLogs.py"

ARTIFACTS_DIR="${ARTIFACTS_DIR:-.}"
# Defaults
HSTS_DIFF_ARTIFACT="${ARTIFACTS_DIR}/${HSTS_DIFF_ARTIFACT:-"nsSTSPreloadList.diff"}"
HPKP_DIFF_ARTIFACT="${ARTIFACTS_DIR}/${HPKP_DIFF_ARTIFACT:-"StaticHPKPins.h.diff"}"
REMOTE_SETTINGS_DIFF_ARTIFACT="${ARTIFACTS_DIR}/${REMOTE_SETTINGS_DIFF_ARTIFACT:-"remote-settings.diff"}"
SUFFIX_LIST_DIFF_ARTIFACT="${ARTIFACTS_DIR}/${SUFFIX_LIST_DIFF_ARTIFACT:-"effective_tld_names.diff"}"
EXPERIMENTER_DIFF_ARTIFACT="${ARTIFACTS_DIR}/initial_experiments.diff"

# duplicate the functionality of taskcluster-lib-urls, but in bash..
queue_base="$TASKCLUSTER_ROOT_URL/api/queue/v1"
index_base="$TASKCLUSTER_ROOT_URL/api/index/v1"

function create_repo_diff() {
  if [ "${USE_GIT}" == "true" ]; then
    ${GIT} -C "${TOPSRCDIR}" diff -u "$1" > "$2"
  else
    ${HG} -R "${TOPSRCDIR}" diff "$1" > "$2"
  fi
}

# fetch_file DEST URL [REQUIRED_MARKER]
# Returns non-zero on failure, for callers that can carry on without the file.
function fetch_file {
  local dest=$1 url=$2 marker=$3
  rm -f "${dest}"
  echo "INFO: ${WGET} -O ${dest} ${url}"
  if ! ${WGET} -O "${dest}" "${url}"; then
    echo "ERROR: download of ${url} failed" >&2
    return 80
  fi
  if [ ! -s "${dest}" ]; then
    echo "ERROR: ${url} returned an empty file" >&2
    return 81
  fi
  if [ -n "${marker}" ] && ! grep -Fq -- "${marker}" "${dest}"; then
    echo "ERROR: ${dest} from ${url} is missing '${marker}', truncated?" >&2
    return 82
  fi
}

# download_file DEST URL [REQUIRED_MARKER]
# Must exit rather than return: callers run in `if` conditions, where `set -e` is disabled.
function download_file {
  fetch_file "$@" || exit $?
}

# download_json DEST URL
function download_json {
  download_file "$1" "$2"
  if ! ${JQ} -e . "$1" > /dev/null; then
    echo "ERROR: $2 did not return valid JSON" >&2
    exit 83
  fi
}

# Cleanup common artifacts.
function preflight_cleanup {
  cd "${BASEDIR}"
  rm -rf "${PRODUCT}" tests "${BROWSER_ARCHIVE}" "${TESTS_ARCHIVE}"
}

function download_shared_artifacts_from_ftp {
  cd "${BASEDIR}"

  # Download everything we need to run js with xpcshell
  echo "INFO: Downloading all the necessary pieces from ${STAGEHOST}..."
  ARTIFACT_DIR="nightly/latest-${BRANCH}"
  if [ "${USE_MC}" == "true" ]; then
    ARTIFACT_DIR="nightly/latest-mozilla-central"
  fi

  BROWSER_ARCHIVE_URL="https://${STAGEHOST}/pub/mozilla.org/${PRODUCT}/${ARTIFACT_DIR}/${BROWSER_ARCHIVE}"
  TESTS_ARCHIVE_URL="https://${STAGEHOST}/pub/mozilla.org/${PRODUCT}/${ARTIFACT_DIR}/${TESTS_ARCHIVE}"

  download_file "${BROWSER_ARCHIVE}" "${BROWSER_ARCHIVE_URL}"
  download_file "${TESTS_ARCHIVE}" "${TESTS_ARCHIVE_URL}"
}

function download_shared_artifacts_from_tc {
  cd "${BASEDIR}"
  TASKID_FILE="taskId.json"

  # Download everything we need to run js with xpcshell
  echo "INFO: Downloading all the necessary pieces from the taskcluster index..."
  TASKID_URL="$index_base/task/gecko.v2.${BRANCH}.shippable.latest.${PRODUCT}.linux64-opt"
  if [ "${USE_MC}" == "true" ]; then
    TASKID_URL="$index_base/task/gecko.v2.mozilla-central.shippable.latest.${PRODUCT}.linux64-opt"
  fi
  download_json "${TASKID_FILE}" "${TASKID_URL}"
  INDEX_TASK_ID="$($JQ -r '.taskId' ${TASKID_FILE})"
  if [ -z "${INDEX_TASK_ID}" ] || [ "${INDEX_TASK_ID}" == "null" ]; then
    echo "Failed to look up taskId at ${TASKID_URL}"
    exit 22
  else
    echo "INFO: Got taskId of $INDEX_TASK_ID"
  fi

  TASKSTATUS_FILE="taskstatus.json"
  STATUS_URL="$queue_base/task/${INDEX_TASK_ID}/status"
  download_json "${TASKSTATUS_FILE}" "${STATUS_URL}"
  LAST_RUN_INDEX=$(($(jq '.status.runs | length' ${TASKSTATUS_FILE}) - 1))
  echo "INFO: Examining run number ${LAST_RUN_INDEX}"

  BROWSER_ARCHIVE_URL="$queue_base/task/${INDEX_TASK_ID}/runs/${LAST_RUN_INDEX}/artifacts/public/build/${BROWSER_ARCHIVE}"
  download_file "${BROWSER_ARCHIVE}" "${BROWSER_ARCHIVE_URL}"

  TESTS_ARCHIVE_URL="$queue_base/task/${INDEX_TASK_ID}/runs/${LAST_RUN_INDEX}/artifacts/public/build/${TESTS_ARCHIVE}"
  download_file "${TESTS_ARCHIVE}" "${TESTS_ARCHIVE_URL}"
}

function unpack_artifacts {
  cd "${BASEDIR}"
  if [ ! -f "${BROWSER_ARCHIVE}" ]; then
    echo "Downloaded file '${BROWSER_ARCHIVE}' not found in directory '$(pwd)'." >&2
    exit 31
  fi
  if [ ! -f "${TESTS_ARCHIVE}" ]; then
    echo "Downloaded file '${TESTS_ARCHIVE}' not found in directory '$(pwd)'." >&2
    exit 32
  fi
  # Unpack the browser and move xpcshell in place for updating the preload list.
  echo "INFO: Unpacking resources..."
  ${UNPACK_CMD} "${BROWSER_ARCHIVE}"
  mkdir -p tests
  cd tests
  ${UNPACK_CMD} "../${TESTS_ARCHIVE}"
  cd "${BASEDIR}"
  cp tests/bin/xpcshell "${PRODUCT}"
}

# Downloads the current in-tree HSTS (HTTP Strict Transport Security) files.
# Runs a simple xpcshell script to generate up-to-date HSTS information.
# Compares the new HSTS output with the old to determine whether we need to update.
function compare_hsts_files {
  cd "${BASEDIR}"

  HSTS_PRELOAD_INC_HG="${HGREPO}/raw-file/default/security/manager/ssl/$(basename "${HSTS_PRELOAD_INC_OLD}")"

  echo "INFO: Downloading existing include file..."
  rm -rf "${HSTS_PRELOAD_ERRORS}"
  download_file "${HSTS_PRELOAD_INC_OLD}" "${HSTS_PRELOAD_INC_HG}"

  # Run the script to get an updated preload list.
  echo "INFO: Generating new HSTS preload list..."
  cd "${BASEDIR}/${PRODUCT}"
  if ! LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:. ./xpcshell "${HSTS_PRELOAD_SCRIPT}" "${HSTS_PRELOAD_INC_OLD}"; then
    echo "HSTS preload list generation failed" >&2
    exit 43
  fi

  # The created files should be non-empty.
  echo "INFO: Checking whether new HSTS preload list is valid..."
  if [ ! -s "${HSTS_PRELOAD_INC_NEW}" ]; then
    echo "New HSTS preload list ${HSTS_PRELOAD_INC_NEW} is empty. That's less good." >&2
    exit 42
  fi
  cd "${BASEDIR}"

  # Check for differences
  echo "INFO: diffing old/new HSTS preload lists into ${HSTS_DIFF_ARTIFACT}"
  ${DIFF} "${HSTS_PRELOAD_INC_OLD}" "${HSTS_PRELOAD_INC_NEW}" | tee "${HSTS_DIFF_ARTIFACT}"
  if [ -s "${HSTS_DIFF_ARTIFACT}" ]
  then
    return 0
  fi
  return 1
}

# Downloads the current in-tree HPKP (HTTP public key pinning) files.
# Runs a simple xpcshell script to generate up-to-date HPKP information.
# Compares the new HPKP output with the old to determine whether we need to update.
function compare_hpkp_files {
  cd "${BASEDIR}"
  HPKP_PRELOAD_JSON_HG="${HGREPO}/raw-file/default/security/manager/tools/$(basename "${HPKP_PRELOAD_JSON}")"

  HPKP_PRELOAD_OUTPUT_HG="${HGREPO}/raw-file/default/security/manager/ssl/${HPKP_PRELOAD_INC}"

  rm -f "${HPKP_PRELOAD_OUTPUT}"
  download_file "${HPKP_PRELOAD_INPUT}" "${HPKP_PRELOAD_OUTPUT_HG}" kPreloadPKPinsExpirationTime
  download_file "${HPKP_PRELOAD_JSON}" "${HPKP_PRELOAD_JSON_HG}" '"entries"'

  # Run the script to get an updated preload list.
  echo "INFO: Generating new HPKP preload list..."
  cd "${BASEDIR}/${PRODUCT}"
  if ! LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:. ./xpcshell "${HPKP_PRELOAD_SCRIPT}" "${HPKP_PRELOAD_JSON}" "${HPKP_PRELOAD_OUTPUT}" > "${HPKP_PRELOAD_ERRORS}"; then
    echo "HPKP preload list generation failed" >&2
    exit 54
  fi

  # The created files should be non-empty.
  echo "INFO: Checking whether new HPKP preload list is valid..."

  if [ ! -s "${HPKP_PRELOAD_OUTPUT}" ]; then
    echo "${HPKP_PRELOAD_OUTPUT} is empty. That's less good." >&2
    exit 52
  fi
  if ! grep kPreloadPKPinsExpirationTime "${HPKP_PRELOAD_OUTPUT}"; then
    echo "${HPKP_PRELOAD_OUTPUT} is missing an expiration time. Truncated?" >&2
    exit 53
  fi
  cd "${BASEDIR}"

  echo "INFO: diffing old/new HPKP preload lists..."
  ${DIFF} "${HPKP_PRELOAD_INPUT}" "${HPKP_PRELOAD_OUTPUT}" | tee "${HPKP_DIFF_ARTIFACT}"
  if [ -s "${HPKP_DIFF_ARTIFACT}" ]
  then
    return 0
  fi
  return 1
}

function is_valid_xml {
  xmlfile=$1
  XMLLINT=$(command -v xmllint 2>/dev/null | head -n1)

  if [ ! -x "${XMLLINT}" ]; then
    echo "ERROR: xmllint not found in PATH"
    exit 60
  fi
  ${XMLLINT} --nonet --noout "${xmlfile}"
}

# Downloads the public suffix list
function compare_suffix_lists {
  HG_SUFFIX_URL="${HGREPO}/raw-file/default/${HG_SUFFIX_PATH}"
  cd "${BASEDIR}"

  download_file "${PUBLIC_SUFFIX_LOCAL}" "${PUBLIC_SUFFIX_URL}" "${PUBLIC_SUFFIX_END_MARKER}"
  download_file "${HG_SUFFIX_LOCAL}" "${HG_SUFFIX_URL}" "${PUBLIC_SUFFIX_END_MARKER}"

  echo "INFO: diffing in-tree suffix list against the suffix list from publicsuffix.org"
  ${DIFF} ${HG_SUFFIX_LOCAL} ${PUBLIC_SUFFIX_LOCAL} | tee "${SUFFIX_LIST_DIFF_ARTIFACT}"
  if [ -s "${SUFFIX_LIST_DIFF_ARTIFACT}" ]
  then
    return 0
  fi
  return 1
}

function compare_remote_settings_files {
  # cd "${TOPSRCDIR}"

  REMOTE_SETTINGS_SERVER="https://firefox.settings.services.mozilla.com/v1"

  # 1. List remote settings collections from server.
  echo "INFO: fetch remote settings list from server"
  changes_lines="$(
    ${WGET} -O- \
      "${REMOTE_SETTINGS_SERVER}/buckets/monitor/collections/changes/changeset?_expected=0" |
        ${JQ} -r '.changes[] | .bucket+"/"+.collection+"/"+(.last_modified|tostring)'
  )"
  line_count="$(printf '%s\n' "${changes_lines}" | wc -l | xargs)"
  if [ "${line_count}" -le 1 ]; then
    echo "ERROR: no changes pulled from server" >&2
    exit 15
  fi

  # 2. For each entry ${bucket, collection, last_modified}
  while IFS="/" read -r bucket collection last_modified; do

    # 3. Check to see if the collection exists in the dump directory of the repository,
    #    if it does not then we aren't keeping the dump, and so we skip it.
    local_dump_file="${REMOTE_SETTINGS_DIR}/${bucket}/${collection}.json"
    if [ ! -r "${local_dump_file}" ]; then
      continue
    fi

    # 4. Download server version into REMOTE_SETTINGS_DIR folder
    remote_records_url="$REMOTE_SETTINGS_SERVER/buckets/${bucket}/collections/${collection}/changeset?_expected=${last_modified}"
    local_location_output="$REMOTE_SETTINGS_DIR/${bucket}/${collection}.json"
    download_json "${BASEDIR}/changeset.json" "${remote_records_url}"

    # We sort both the keys and the records in search-config-v2 to make it
    # easier to read and to experiment with making changes via the dump file.
    if [ "${collection}" = "search-config-v2" ]; then
      ${JQ} --sort-keys '{"data": .changes | sort_by(.recordType, .identifier), "timestamp": .timestamp}' < "${BASEDIR}/changeset.json" > "${local_location_output}"
    else
      ${JQ} '{"data": .changes, "timestamp": .timestamp}' < "${BASEDIR}/changeset.json" > "${local_location_output}"
    fi

    # 5. Download attachments if needed.
    if [ "${bucket}" = "blocklists" ] && [ "${collection}" = "addons-bloomfilters" ]; then
      # Find the attachment with the most recent generation_time, like _updateMLBF in Blocklist.sys.mjs.
      # The server should return one "bloomfilter-base" record, but in case it returns multiple,
      # return the most recent one. The server may send multiple entries if we ever decide to use
      # the "filter_expression" feature of Remote Settings to send different records to specific
      # channels. In that case this code should be updated to recognize the filter expression,
      # but until we do, simply select the most recent record - can't go wrong with that.
      # Note that "attachment_type" and "generation_time" are specific to addons-bloomfilters.
      update_remote_settings_attachment "${bucket}" "${collection}" addons-mlbf.bin \
        'map(select(.attachment_type == "bloomfilter-base")) | sort_by(.generation_time) | last'
      update_remote_settings_attachment "${bucket}" "${collection}" softblocks-addons-mlbf.bin \
        'map(select(.attachment_type == "softblocks-bloomfilter-base")) | sort_by(.generation_time) | last'
    fi
    # TODO: Bug 1873448. This cannot handle new/removed files currently, due to the
    # build system making it difficult.
    if [ "${bucket}" = "main" ] && [ "${collection}" = "search-config-icons" ]; then
      ${JQ} -r '.data[] | .id' < "${local_location_output}" |\
      while read -r id; do
        # We do not want quotes around ${id}
        # shellcheck disable=SC2086
        update_remote_settings_attachment "${bucket}" "${collection}" ${id} ".[] | select(.id == \"${id}\")"
      done || exit $?
    fi
    # NOTE: The downloaded data is not validated. xpcshell should be used for that.
    
    # bug 1959683: remote settings update can add untracked search-config-icons
    # It is not safe to take these (see https://bugzilla.mozilla.org/show_bug.cgi?id=1873448)
    # If they are around as untracked files when `arc diff` runs, that command will fail.
    # (We explicitly don't want to use `arc diff --allow-untracked` to avoid accidentally
    # missing files from other updates - we'd rather the job fail.)
    if [ "${USE_GIT}" == "true" ]; then
      ${GIT} -C "${TOPSRCDIR}" clean -f -d services/settings/dumps/main/search-config-icons
    else
      ${HG} --cwd "${TOPSRCDIR}" purge services/settings/dumps/main/search-config-icons
    fi
  done <<< "${changes_lines}"

  echo "INFO: diffing old/new remote settings dumps..."
  create_repo_diff "${REMOTE_SETTINGS_DIR}" "${REMOTE_SETTINGS_DIFF_ARTIFACT}"

  # cd "${BASEDIR}"
  if [ -s "${REMOTE_SETTINGS_DIFF_ARTIFACT}" ]
  then
    return 0
  fi
  return 1
}

# Helper for compare_remote_settings_files to download attachments from remote settings.
# The format and location is documented at:
# https://firefox-source-docs.mozilla.org/services/settings/index.html#services-packaging-attachments
function update_remote_settings_attachment() {
  local bucket=$1
  local collection=$2
  local attachment_id=$3
  # $4 is a jq filter on the arrays that should return one record with the attachment
  local jq_attachment_selector=".data | map(select(.attachment)) | $4"

  # These paths match _readAttachmentDump in services/settings/Attachments.sys.mjs.
  local path_to_attachment="${bucket}/${collection}/${attachment_id}"
  local path_to_meta="${bucket}/${collection}/${attachment_id}.meta.json"
  local meta_file="${REMOTE_SETTINGS_DIR}/${path_to_meta}"

  # Those files should have been created by compare_remote_settings_files before the function call.
  local source_collection_location="${REMOTE_SETTINGS_DIR}/${bucket}/${collection}.json"

  # Exact the metadata for this attachment from the already downloaded collection,
  # and compare with our current metadata to see if the attachment has changed or not.
  # Uses cmp for fast compare (rather than repository tools).
  if ${JQ} -cj "${jq_attachment_selector}" < "${source_collection_location}" | cmp --silent - "${meta_file}"; then
    # Metadata not changed, don't bother downloading the attachments themselves.
    return
  fi
  # Metadata changed. Download attachments.
  local new_meta_file="${BASEDIR}/attachment.meta.json.tmp"
  local new_attachment_file="${BASEDIR}/attachment.tmp"
  ${JQ} -cj <"${source_collection_location}" "${jq_attachment_selector}" > "${new_meta_file}"

  echo "INFO: Downloading updated remote settings dump: ${bucket}/${collection}/${attachment_id}"

  if [ -z "${ATTACHMENT_BASE_URL}" ] ; then
    download_json "${BASEDIR}/server-info.json" "${REMOTE_SETTINGS_SERVER}"
    ATTACHMENT_BASE_URL=$(${JQ} -r .capabilities.attachments.base_url < "${BASEDIR}/server-info.json")
  fi
  attachment_path_from_meta=$(${JQ} -r < "${new_meta_file}" .attachment.location)
  if ! fetch_file "${new_attachment_file}" "${ATTACHMENT_BASE_URL}${attachment_path_from_meta}"; then
    echo "WARNING: skipping ${path_to_attachment}, the in-tree copy (if any) is unchanged" >&2
    rm -f "${new_meta_file}" "${new_attachment_file}"
    return 0
  fi
  mv "${new_attachment_file}" "${REMOTE_SETTINGS_DIR}/${path_to_attachment}"
  mv "${new_meta_file}" "${meta_file}"
}

function compare_mobile_experiments() {
  download_json experiments.json "${EXPERIMENTER_URL}"
  download_json fenix-experiments-old.json "${HGREPO}/raw-file/default/${FENIX_INITIAL_EXPERIMENTS}"
  download_json focus-experiments-old.json "${HGREPO}/raw-file/default/${FOCUS_INITIAL_EXPERIMENTS}"

  # shellcheck disable=SC2016
  ${JQ} --arg APP_NAME fenix '{"data":map(select(.appName == $APP_NAME))}' < experiments.json > fenix-experiments-new.json
  # shellcheck disable=SC2016
  ${JQ} --arg APP_NAME focus_android '{"data":map(select(.appName == $APP_NAME))}' < experiments.json > focus-experiments-new.json

  ( ${DIFF} fenix-experiments-old.json fenix-experiments-new.json; ${DIFF} focus-experiments-old.json focus-experiments-new.json ) > "${EXPERIMENTER_DIFF_ARTIFACT}"
  if [ -s "${EXPERIMENTER_DIFF_ARTIFACT}" ]; then
    return 0
  else
    # no change
    return 1
  fi
}

function update_mobile_merino_manifest() {
  echo "INFO: Updating mobile merino manifest..."
  "${TOPSRCDIR}"/mach python "${MOBILE_MERINO_MANIFEST_UPDATE_SCRIPT}"
  status=$?

  if [ $status -gt 1 ]; then
    exit 70
  fi
  return $status
}

function update_ct_logs() {
  echo "INFO: Updating CT logs..."
  "${TOPSRCDIR}"/mach python "${CT_LOG_UPDATE_SCRIPT}"
}

# Clones an hg repo
function clone_repo {
  cd "${BASEDIR}"
  if [ ! -d "${TOPSRCDIR}" ]; then
    ${HG} robustcheckout --sharebase /tmp/hg-store -b default "${HGREPO}" "${TOPSRCDIR}"
  fi

  ${HG} -R "${TOPSRCDIR}" pull
  ${HG} -R "${TOPSRCDIR}" update -C default
}

# Copies new HSTS files in place, and commits them.
function stage_hsts_files {
  cd "${BASEDIR}"
  cp -f "${HSTS_PRELOAD_INC_NEW}" "${TOPSRCDIR}/security/manager/ssl/"
}

function stage_hpkp_files {
  cd "${BASEDIR}"
  cp -f "${HPKP_PRELOAD_OUTPUT}" "${TOPSRCDIR}/security/manager/ssl/${HPKP_PRELOAD_INC}"
}

function stage_tld_suffix_files {
  cd "${BASEDIR}"
  cp -a "${PUBLIC_SUFFIX_LOCAL}" "${TOPSRCDIR}/${HG_SUFFIX_PATH}"
}

function stage_mobile_experiments_files {
  cd "${BASEDIR}"

  cp fenix-experiments-new.json "${TOPSRCDIR}/${FENIX_INITIAL_EXPERIMENTS}"
  cp focus-experiments-new.json "${TOPSRCDIR}/${FOCUS_INITIAL_EXPERIMENTS}"
}

# Push all pending commits to Phabricator
function push_repo {
  if [ "${SKIP_PUSH}" == "true" ]; then
    echo "Skipping push due to --skip-push"
    return 0
  fi

  cd "${TOPSRCDIR}"
  if [ ! -r "${HOME}/.arcrc" ]
  then
    return 1
  fi
  if ! ARC=$(command -v arc) && ! ARC=$(command -v arcanist)
  then
    return 1
  fi
  if [ -z "${REVIEWERS}" ]
  then
    return 1
  fi
  # Clean up older review requests of the same type as this run.
  # Pinning updates (HSTS/HPKP) and periodic updates are separate tasks and
  # must not abandon each other's patches.
  # Turn  Needs Review D624: No bug, Automated HSTS ...
  # into D624
  ALL_DIFFS=$($ARC list | grep "Needs Review" | grep -E "${BRANCH} repo-update" || true)
  if [ "${DO_HSTS}" == "true" ] || [ "${DO_HPKP}" == "true" ]; then
    OLDER_DIFFS=$(echo "${ALL_DIFFS}" | grep -E "HSTS|HPKP" || true)
  else
    OLDER_DIFFS=$(echo "${ALL_DIFFS}" | grep -vE "HSTS|HPKP" || true)
  fi
  for diff in $(echo "${OLDER_DIFFS}" | awk 'match($0, /D[0-9]+[^: ]/) { print substr($0, RSTART, RLENGTH)  }')
  do
    echo "Removing old request $diff"
    # There is no 'arc abandon', see bug 1452082
    echo '{"transactions": [{"type":"abandon", "value": true}], "objectIdentifier": "'"${diff}"'"}' | $ARC call-conduit -- differential.revision.edit
  done

  # bug 1959683: using /dev/null as stdin causes arcanist to fail quickly
  # instead of hang if user input is requested.
  $ARC diff --verbatim --reviewers "${REVIEWERS}" < /dev/null
}



# Main

preflight_cleanup

mkdir -p "${DATADIR}"

# Clone the repository here as some sections will use it for source data, and
# we'll need it later anyway.
if [ "${CLONE_REPO}" == "true" ]
then
  clone_repo
fi

if [ "${DO_HSTS}" == "true" ] || [ "${DO_HPKP}" == "true" ] || [ "${DO_PRELOAD_PINSET}" == "true" ]
then
  if [ "${USE_TC}" == "true" ]; then
    download_shared_artifacts_from_tc
  else
    download_shared_artifacts_from_ftp
  fi
  unpack_artifacts
fi

if [ "${DO_HSTS}" == "true" ]; then
  if compare_hsts_files
  then
    HSTS_UPDATED=true
  fi
fi
if [ "${DO_HPKP}" == "true" ]; then
  if compare_hpkp_files
  then
    HPKP_UPDATED=true
  fi
fi
if [ "${DO_REMOTE_SETTINGS}" == "true" ]; then
  if compare_remote_settings_files
  then
    REMOTE_SETTINGS_UPDATED=true
  fi
fi
if [ "${DO_SUFFIX_LIST}" == "true" ]; then
  if compare_suffix_lists
  then
    SUFFIX_LIST_UPDATED=true
  fi
fi
if [ "${DO_MOBILE_EXPERIMENTS}" == "true" ]; then
  if compare_mobile_experiments
  then
    MOBILE_EXPERIMENTS_UPDATED=true
  fi
fi
if [ "${DO_MOBILE_MERINO_MANIFEST}" == "true" ]; then
  if update_mobile_merino_manifest
  then
    MOBILE_MERINO_MANIFEST_UPDATED=true
  fi
fi
if [ "${DO_CT_LOGS}" == "true" ]; then
  update_ct_logs
fi


if [ "${HSTS_UPDATED}" == "false" ] && [ "${HPKP_UPDATED}" == "false" ] && [ "${REMOTE_SETTINGS_UPDATED}" == "false" ] && [ "${SUFFIX_LIST_UPDATED}" == "false" ] && [ "${MOBILE_EXPERIMENTS_UPDATED}" == "false" ] && [ "${MOBILE_MERINO_MANIFEST_UPDATED}" == "false" ] && [ "${DO_CT_LOGS}" == "false" ]; then
  echo "INFO: no updates required. Exiting."
  exit 0
else
  if [ "${DRY_RUN}" == "true" ]; then
    echo "INFO: Updates are available, not updating hg in dry-run mode."
    exit 2
  fi
fi

COMMIT_MESSAGE="No Bug, ${BRANCH} repo-update"
if [ "${HSTS_UPDATED}" == "true" ]
then
  stage_hsts_files
  COMMIT_MESSAGE="${COMMIT_MESSAGE} HSTS"
fi

if [ "${HPKP_UPDATED}" == "true" ]
then
  stage_hpkp_files
  COMMIT_MESSAGE="${COMMIT_MESSAGE} HPKP"
fi

if [ "${REMOTE_SETTINGS_UPDATED}" == "true" ]
then
  COMMIT_MESSAGE="${COMMIT_MESSAGE} remote-settings"
fi

if [ "${SUFFIX_LIST_UPDATED}" == "true" ]
then
  stage_tld_suffix_files
  COMMIT_MESSAGE="${COMMIT_MESSAGE} tld-suffixes"
fi

if [ "${MOBILE_EXPERIMENTS_UPDATED}" == "true" ]
then
  stage_mobile_experiments_files
  COMMIT_MESSAGE="${COMMIT_MESSAGE} mobile-experiments"
fi

if [ "${MOBILE_MERINO_MANIFEST_UPDATED}" == "true" ]
then
  # Merino manifest file is already updated in-place in the tree, 
  # so there's no need to stage them.
  COMMIT_MESSAGE="${COMMIT_MESSAGE} mobile-merino-manifest"
fi

if [ "${DO_CT_LOGS}" == "true" ]
then
  # CT log files are already updated in-place in the tree, so
  # there's no need to stage them.
  COMMIT_MESSAGE="${COMMIT_MESSAGE} ct-logs"
fi

if [ ${DONTBUILD} == true ]; then
  COMMIT_MESSAGE="${COMMIT_MESSAGE} - (DONTBUILD)"
fi
if [ ${CLOSED_TREE} == true ]; then
  COMMIT_MESSAGE="${COMMIT_MESSAGE} - CLOSED TREE"
fi
if [ ${APPROVAL} == true ]; then
  COMMIT_MESSAGE="${COMMIT_MESSAGE} - a=repo-update"
fi

if [ "${USE_GIT}" == "true" ]; then
  if ${GIT} -C "${TOPSRCDIR}" commit -a --author "${COMMIT_AUTHOR}" -m "${COMMIT_MESSAGE}"
  then
    push_repo
  fi
else
  if ${HG} -R "${TOPSRCDIR}" commit -u "${COMMIT_AUTHOR}" -m "${COMMIT_MESSAGE}"
  then
    push_repo
  fi
fi

echo "All done"
