#!/bin/bash

function usage {
    log "In the updates subdirectory of the directory this script is in,"
    log "there are a bunch of config files. You should call this script,"
    log "passing the names of one or more of those files as parameters"
    log "to this script."
    log ""
    log "This will validate that the update.xml files all exist for the"
    log "given config file, and that they report the correct file sizes"
    log "for the associated mar files, and that the associated mar files"
    log "are available on the update servers."
    log ""
    log "This script will spawn multiple curl processes to query the"
    log "snippets (update.xml file downloads) and the download urls in"
    log "parallel. The number of parallel curl processes can be managed"
    log "with the -p MAX_PROCS option."
    log ""
    log "Only the first three bytes of the mar files are downloaded"
    log "using curl -r 0-2 option to save time. GET requests are issued"
    log "rather than HEAD requests, since Akamai (one of our CDN"
    log "partners) caches GET and HEAD requests separately - therefore"
    log "they can be out-of-sync, and it is important that we validate"
    log "that the GET requests return the expected results."
    log ""
    log "Please note this script can run on linux and OS X. It has not"
    log "been tested on Windows, but may also work. It can be run"
    log "locally, and does not require access to the mozilla vpn or"
    log "any other special network, since the update servers are"
    log "available over the internet. However, it does require an"
    log "up-to-date checkout of the tools repository, as the updates/"
    log "subfolder changes over time, and reflects the currently"
    log "available updates. It makes no changes to the update servers"
    log "so there is no harm in running it. It simply generates a"
    log "report. However, please try to avoid hammering the update"
    log "servers aggressively, e.g. with thousands of parallel"
    log "processes. For example, feel free to run the examples below,"
    log "first making sure that your source code checkout is up-to-"
    log "date on your own machine, to get the latest configs in the"
    log "updates/ subdirectory."
    log ""
    log "Usage:"
    log "    $(basename "${0}") [-p MAX_PROCS] config1 [config2 config3 config4 ...]"
    log "    $(basename "${0}") -h"
    log ""
    log "Examples:"
    log "    1. $(basename "${0}") -p 128 mozBeta-thunderbird-linux.cfg mozBeta-thunderbird-linux64.cfg"
    log "    2. $(basename "${0}") mozBeta-thunderbird-linux64.cfg"
}

function log {
    echo "$(date):  ${1}"
}

# subprocesses don't log in real time, due to synchronisation
# issues which can cause log entries to overwrite each other.
# therefore this function outputs log entries written to
# temporary files on disk, and then deletes them.
function flush_logs {
    ls -1rt "${TMPDIR}" | grep '^log\.' | while read LOG
    do
        cat "${TMPDIR}/${LOG}"
        rm "${TMPDIR}/${LOG}"
    done
}

# this function takes an update.xml url as an argument
# and then logs a list of config files and their line
# numbers, that led to this update.xml url being tested
function show_cfg_file_entries {
    local update_xml_url="${1}"
    cat "${update_xml_urls}" | cut -f1 -d' ' | grep -Fn "${update_xml_url}" | sed 's/:.*//' | while read match_line_no
    do
        cfg_file="$(sed -n -e "${match_line_no}p" "${update_xml_urls}" | cut -f3 -d' ')"
        cfg_line_no="$(sed -n -e "${match_line_no}p" "${update_xml_urls}" | cut -f4 -d' ')"
        log "        ${cfg_file} line ${cfg_line_no}: $(sed -n -e "${cfg_line_no}p" "${cfg_file}")"
    done
}

# this function takes a mar url as an argument and then
# logs information about which update.xml urls referenced
# this mar url, and which config files referenced those
# mar urls - so you have a full understanding of why this
# mar url was ever tested
function show_update_xml_entries {
    local mar_url="${1}"
    grep -Frl "${mar_url}" "${TMPDIR}" | grep '/update_xml_to_mar\.' | while read update_xml_to_mar
    do
        mar_size="$(cat "${update_xml_to_mar}" | cut -f2 -d' ')"
        update_xml_url="$(cat "${update_xml_to_mar}" | cut -f3 -d' ')"
        patch_type="$(cat "${update_xml_to_mar}" | cut -f4 -d' ')"
        update_xml_actual_url="$(cat "${update_xml_to_mar}" | cut -f5 -d' ')"
        log "        ${update_xml_url}"
        [ -n "${update_xml_actual_url}" ] && log "            which redirected to: ${update_xml_actual_url}"
        log "            This contained an entry for:"
        log "                patch type: ${patch_type}"
        log "                mar size: ${mar_size}"
        log "                mar url: ${mar_url}"
        log "            The update.xml url above was retrieved because of the following cfg file entries:"
        show_cfg_file_entries "${update_xml_url}" | sed 's/        /                /'
    done
}

echo -n "$(date):  Command called:"
for ((INDEX=0; INDEX<=$#; INDEX+=1))
do
    echo -n " '${!INDEX}'"
done
echo ''
log "From directory: '$(pwd)'"
log ''
log "Parsing arguments..."

# Max procs lowered in bug 894368 to try to avoid spurious failures
MAX_PROCS=48
BAD_ARG=0
BAD_FILE=0
while getopts p:h OPT
do
    case "${OPT}" in
        p) MAX_PROCS="${OPTARG}";;
        h) usage
           exit;;
        *) BAD_ARG=1;;
    esac
done
shift "$((OPTIND - 1))"

# invalid option specified
[ "${BAD_ARG}" == 1 ] && exit 66

log "Checking one or more config files have been specified..."
if [ $# -lt 1 ]
then
    usage
    log "ERROR: You must specify one or more config files"
    exit 64
fi

log "Checking whether MAX_PROCS is a number..."
if ! let x=MAX_PROCS 2>/dev/null
then
    usage
    log "ERROR: MAX_PROCS must be a number (-p option); you specified '${MAX_PROCS}' - this is not a number."
    exit 65
fi

if ! cd "$(dirname "${0}")" 2>/dev/null
then
    log "ERROR: Cannot cd into '$(dirname "${0}")' from '$(pwd)'"
    exit 68
fi

log "Checking specified config files (and downloading them if necessary):"
log ''
configs=()
for file in "${@}"
do
    if [[ ${file} == http* ]]
    then
        log "  Downloading config file '${file}'"
        cfg=$(mktemp)
        curl -fL --retry 5 --compressed "${file}" > "$cfg"
        if [ "$?" != 0 ]; then
            log "Error downloading config file '${file}'"
            BAD_FILE=1
        else
            log "  * '${file}' ok, downloaded to '${cfg}'"
            configs+=($cfg)
        fi
    elif [ -f "${file}" ]
    then
        log "  * '${file}' ok"
        configs+=(${file})
    else
        log "  * '${file}' missing"
        BAD_FILE=1
    fi
done
log ''

# invalid config specified
if [ "${BAD_FILE}" == 1 ]
then
    log "TEST-UNEXPECTED-ERROR: Unable to download config file(s) or config files are missing from repo - see above"
    exit 67
fi

log "All checks completed successfully."
log ''
log "Starting stopwatch..."
log ''
log "Please be aware output will now be buffered up, and only displayed after completion."
log "Therefore do not be alarmed if you see no output for several minutes."
log "See https://bugzilla.mozilla.org/show_bug.cgi?id=863602#c5 for details".
log ''

START_TIME="$(date +%s)"

# Create a temporary directory for all temp files, that can easily be
# deleted afterwards. See https://bugzilla.mozilla.org/show_bug.cgi?id=863602
# to understand why we write everything in distinct temporary files rather
# than writing to standard error/standard out or files shared across
# processes.
# Need to unset TMPDIR first since it affects mktemp behaviour on next line
unset TMPDIR
export TMPDIR="$(mktemp -d -t final_verification.XXXXXXXXXX)"

# this temporary file will list all update urls that need to be checked, in this format:
# <update url> <comma separated list of patch types> <cfg file that requests it> <line number of config file>
# e.g.
# https://aus4.mozilla.org/update/3/Firefox/18.0/20130104154748/Linux_x86_64-gcc3/zh-TW/releasetest/default/default/default/update.xml?force=1 complete moz20-firefox-linux64-major.cfg 3
# https://aus4.mozilla.org/update/3/Firefox/18.0/20130104154748/Linux_x86_64-gcc3/zu/releasetest/default/default/default/update.xml?force=1 complete moz20-firefox-linux64.cfg 7
# https://aus4.mozilla.org/update/3/Firefox/19.0/20130215130331/Linux_x86_64-gcc3/ach/releasetest/default/default/default/update.xml?force=1 complete,partial moz20-firefox-linux64-major.cfg 11
# https://aus4.mozilla.org/update/3/Firefox/19.0/20130215130331/Linux_x86_64-gcc3/af/releasetest/default/default/default/update.xml?force=1 complete,partial moz20-firefox-linux64.cfg 17
update_xml_urls="$(mktemp -t update_xml_urls.XXXXXXXXXX)"

####################################################################################
# And now a summary of all temp files that will get generated during this process...
#
# 1) mktemp -t failure.XXXXXXXXXX
#
# Each failure will generate a one line temp file, which is a space separated
# output of the error code, and the instance data for the failure.
# e.g.
#
# PATCH_TYPE_MISSING https://aus4.mozilla.org/update/3/Firefox/4.0b12/20110222205441/Linux_x86-gcc3/dummy-locale/releasetest/update.xml?force=1 complete https://aus4.mozilla.org/update/3/Firefox/4.0b12/20110222205441/Linux_x86-gcc3/dummy-locale/releasetest/default/default/default/update.xml?force=1
#
# 2) mktemp -t update_xml_to_mar.XXXXXXXXXX
#
# For each mar url referenced in an update.xml file, a temp file will be created to store the
# association between update.xml url and mar url. This is later used (e.g. in function
# show_update_xml_entries) to trace back the update.xml url(s) that led to a mar url being
# tested. It is also used to keep a full list of mar urls to test.
# e.g.
#
# <mar url> <mar size> <update.xml url> <patch type> <update.xml redirection url, if HTTP 302 returned>
#
# 3) mktemp -t log.XXXXXXXXXX
#
# For each log message logged by a subprocesses, we will create a temp log file with the
# contents of the log message, since we cannot safely output the log message from the subprocess
# and guarantee that it will be correctly output. By buffering log output in individual log files
# we guarantee that log messages will not interfere with each other. We then flush them when all
# forked subprocesses have completed.
#
# 4) mktemp -t mar_headers.XXXXXXXXXX
#
# We keep a copy of the mar url http headers retrieved in one file per mar url.
#
# 5) mktemp -t update.xml.headers.XXXXXXXXXX
#
# We keep a copy of the update.xml http headers retrieved in one file per update.xml url.
#
# 6) mktemp -t update.xml.XXXXXXXXXX
#
# We keep a copy of each update.xml file retrieved in individual files.
####################################################################################


# generate full list of update.xml urls, followed by patch types,
# as defined in the specified config files - and write into "${update_xml_urls}" file
aus_server="https://aus5.mozilla.org"
for cfg_file in "${configs[@]}"
do
    line_no=0
    sed -e 's/localtest/cdntest/' "${cfg_file}" | while read config_line
    do
        let line_no++
        # to avoid contamination between iterations, reset variables
        # each loop in case they are not declared
        # aus_server is not "cleared" each iteration - to be consistent with previous behaviour of old
        # final-verification.sh script - might be worth reviewing if we really want this behaviour
        release="" product="" platform="" build_id="" locales="" channel="" from="" patch_types="complete"
        eval "${config_line}"
        for locale in ${locales}
        do
            echo "${aus_server}/update/3/$product/$release/$build_id/$platform/$locale/$channel/default/default/default/update.xml?force=1" "${patch_types// /,}" "${cfg_file}" "${line_no}"
        done
    done
done > "${update_xml_urls}"

# download update.xml files and grab the mar urls from downloaded file for each patch type required
cat "${update_xml_urls}" | cut -f1-2 -d' ' | sort -u | xargs -n2 "-P${MAX_PROCS}" ./get-update-xml.sh
if [ "$?" != 0 ]; then
    flush_logs
    log "TEST-UNEXPECTED-ERROR: Error generating update requests"
    exit 70
fi

flush_logs

# download http header for each mar url
find "${TMPDIR}" -name 'update_xml_to_mar.*' -type f | xargs cat | cut -f1-2 -d' ' | sort -u | xargs -n2 "-P${MAX_PROCS}" ./test-mar-url.sh
if [ "$?" != 0 ]; then
    flush_logs
    log "TEST-UNEXPECTED-ERROR: Error HEADing mar urls"
    exit 71
fi

flush_logs

log ''
log 'Stopping stopwatch...'
STOP_TIME="$(date +%s)"

number_of_failures="$(find "${TMPDIR}" -name 'failure.*' -type f | wc -l | sed 's/ //g')"
number_of_update_xml_urls="$(cat "${update_xml_urls}" | cut -f1 -d' ' | sort -u | wc -l | sed 's/ //g')"
number_of_mar_urls="$(find "${TMPDIR}" -name "update_xml_to_mar.*" | xargs cat | cut -f1 -d' ' | sort -u | wc -l | sed 's/ //g')"

if [ "${number_of_failures}" -eq 0 ]
then
    log
    log "All tests passed successfully."
    log
    exit_code=0
else
    log ''
    log '===================================='
    [ "${number_of_failures}" -gt 1 ] && log "${number_of_failures} FAILURES" || log '1 FAILURE'
    failure=0
    ls -1tr "${TMPDIR}" | grep '^failure\.' | while read failure_file
    do
        while read failure_code entry1 entry2 entry3 entry4 entry5 entry6 entry7
        do
            log '===================================='
            log ''
            case "${failure_code}" in

                UPDATE_XML_UNAVAILABLE)
                    update_xml_url="${entry1}"
                    update_xml="${entry2}"
                    update_xml_headers="${entry3}"
                    update_xml_debug="${entry4}"
                    update_xml_curl_exit_code="${entry5}"
                    log "TEST-UNEXPECTED-FAIL $((++failure)): Update xml file not available"
                    log ""
                    log "    Download url: ${update_xml_url}"
                    log "    Curl returned exit code: ${update_xml_curl_exit_code}"
                    log ""
                    log "    The HTTP headers were:"
                    sed -e "s/$(printf '\r')//" -e "s/^/$(date):          /" -e '$a\' "${update_xml_headers}"
                    log ""
                    log "    The full curl debug output was:"
                    sed -e "s/$(printf '\r')//" -e "s/^/$(date):          /" -e '$a\' "${update_xml_debug}"
                    log ""
                    log "    The returned update.xml file was:"
                    sed -e "s/$(printf '\r')//" -e "s/^/$(date):          /" -e '$a\' "${update_xml}"
                    log ""
                    log "    This url was tested because of the following cfg file entries:"
                    show_cfg_file_entries "${update_xml_url}"
                    log ""

                    ;;

                UPDATE_XML_REDIRECT_FAILED)
                    update_xml_url="${entry1}"
                    update_xml_actual_url="${entry2}"
                    update_xml="${entry3}"
                    update_xml_headers="${entry4}"
                    update_xml_debug="${entry5}"
                    update_xml_curl_exit_code="${entry6}"
                    log "TEST-UNEXPECTED-FAIL $((++failure)): Update xml file not available at *redirected* location"
                    log ""
                    log "    Download url: ${update_xml_url}"
                    log "    Redirected to: ${update_xml_actual_url}"
                    log "    It could not be downloaded from this url."
                    log "    Curl returned exit code: ${update_xml_curl_exit_code}"
                    log ""
                    log "    The HTTP headers were:"
                    sed -e "s/$(printf '\r')//" -e "s/^/$(date):          /" -e '$a\' "${update_xml_headers}"
                    log ""
                    log "    The full curl debug output was:"
                    sed -e "s/$(printf '\r')//" -e "s/^/$(date):          /" -e '$a\' "${update_xml_debug}"
                    log ""
                    log "    The returned update.xml file was:"
                    sed -e "s/$(printf '\r')//" -e "s/^/$(date):          /" -e '$a\' "${update_xml}"
                    log ""
                    log "    This url was tested because of the following cfg file entries:"
                    show_cfg_file_entries "${update_xml_url}"
                    log ""
                    ;;

                PATCH_TYPE_MISSING)
                    update_xml_url="${entry1}"
                    patch_type="${entry2}"
                    update_xml="${entry3}"
                    update_xml_headers="${entry4}"
                    update_xml_debug="${entry5}"
                    update_xml_actual_url="${entry6}"
                    log "TEST-UNEXPECTED-FAIL $((++failure)): Patch type '${patch_type}' not present in the downloaded update.xml file."
                    log ""
                    log "    Update xml file downloaded from: ${update_xml_url}"
                    [ -n "${update_xml_actual_url}" ] && log "    This redirected to the download url: ${update_xml_actual_url}"
                    log "    Curl returned exit code: 0 (success)"
                    log ""
                    log "    The HTTP headers were:"
                    sed -e "s/$(printf '\r')//" -e "s/^/$(date):          /" -e '$a\' "${update_xml_headers}"
                    log ""
                    log "    The full curl debug output was:"
                    sed -e "s/$(printf '\r')//" -e "s/^/$(date):          /" -e '$a\' "${update_xml_debug}"
                    log ""
                    log "    The returned update.xml file was:"
                    sed -e "s/$(printf '\r')//" -e "s/^/$(date):          /" -e '$a\' "${update_xml}"
                    log ""
                    log "    This url and patch type combination was tested due to the following cfg file entries:"
                    show_cfg_file_entries "${update_xml_url}"
                    log ""
                    ;;

                NO_MAR_FILE)
                    mar_url="${entry1}"
                    mar_headers_file="${entry2}"
                    mar_headers_debug_file="${entry3}"
                    mar_file_curl_exit_code="${entry4}"
                    mar_actual_url="${entry5}"
                    log "TEST-UNEXPECTED-FAIL $((++failure)): Could not retrieve mar file"
                    log ""
                    log "    Mar file url: ${mar_url}"
                    [ -n "${mar_actual_url}" ] && log "    This redirected to: ${mar_actual_url}"
                    log "    The mar file could not be downloaded from this location."
                    log "    Curl returned exit code: ${mar_file_curl_exit_code}"
                    log ""
                    log "    The HTTP headers were:"
                    sed -e "s/$(printf '\r')//" -e "s/^/$(date):          /" -e '$a\' "${mar_headers_file}"
                    log ""
                    log "    The full curl debug output was:"
                    sed -e "s/$(printf '\r')//" -e "s/^/$(date):          /" -e '$a\' "${mar_headers_debug_file}"
                    log ""
                    log "    The mar download was tested because it was referenced in the following update xml file(s):"
                    show_update_xml_entries "${mar_url}"
                    log ""
                    ;;

                MAR_FILE_WRONG_SIZE)
                    mar_url="${entry1}"
                    mar_required_size="${entry2}"
                    mar_actual_size="${entry3}"
                    mar_headers_file="${entry4}"
                    mar_headers_debug_file="${entry5}"
                    mar_file_curl_exit_code="${entry6}"
                    mar_actual_url="${entry7}"
                    log "TEST-UNEXPECTED-FAIL $((++failure)): Mar file is wrong size"
                    log ""
                    log "    Mar file url: ${mar_url}"
                    [ -n "${mar_actual_url}" ] && log "    This redirected to: ${mar_actual_url}"
                    log "    The http header of the mar file url says that the mar file is ${mar_actual_size} bytes."
                    log "    One or more of the following update.xml file(s) says that the file should be ${mar_required_size} bytes."
                    log ""
                    log "    These are the update xml file(s) that referenced this mar:"
                    show_update_xml_entries "${mar_url}"
                    log ""
                    log "    Curl returned exit code: ${mar_file_curl_exit_code}"
                    log ""
                    log "    The HTTP headers were:"
                    sed -e "s/$(printf '\r')//" -e "s/^/$(date):          /" -e '$a\' "${mar_headers_file}"
                    log ""
                    log "    The full curl debug output was:"
                    sed -e "s/$(printf '\r')//" -e "s/^/$(date):          /" -e '$a\' "${mar_headers_debug_file}"
                    log ""
                    ;;

                BAD_HTTP_RESPONSE_CODE_FOR_MAR)
                    mar_url="${entry1}"
                    mar_headers_file="${entry2}"
                    mar_headers_debug_file="${entry3}"
                    mar_file_curl_exit_code="${entry4}"
                    mar_actual_url="${entry5}"
                    http_response_code="$(sed -e "s/$(printf '\r')//" -n -e '/^HTTP\//p' "${mar_headers_file}" | tail -1)"
                    log "TEST-UNEXPECTED-FAIL $((++failure)): '${http_response_code}' for mar file"
                    log ""
                    log "    Mar file url: ${mar_url}"
                    [ -n "${mar_actual_url}" ] && log "    This redirected to: ${mar_actual_url}"
                    log ""
                    log "    These are the update xml file(s) that referenced this mar:"
                    show_update_xml_entries "${mar_url}"
                    log ""
                    log "    Curl returned exit code: ${mar_file_curl_exit_code}"
                    log ""
                    log "    The HTTP headers were:"
                    sed -e "s/$(printf '\r')//" -e "s/^/$(date):          /" -e '$a\' "${mar_headers_file}"
                    log ""
                    log "    The full curl debug output was:"
                    sed -e "s/$(printf '\r')//" -e "s/^/$(date):          /" -e '$a\' "${mar_headers_debug_file}"
                    log ""
                    ;;

                *)
                    log "TEST-UNEXPECTED-FAIL: Unknown failure code - '${failure_code}'"
                    log "ERROR: This is a serious bug in this script."
                    log "ERROR: Only known failure codes are: UPDATE_XML_UNAVAILABLE, UPDATE_XML_REDIRECT_FAILED, PATCH_TYPE_MISSING, NO_MAR_FILE, MAR_FILE_WRONG_SIZE, BAD_HTTP_RESPONSE_CODE_FOR_MAR"
                    log ""
                    log "FAILURE $((++failure)): Data from failure is: ${entry1} ${entry2} ${entry3} ${entry4} ${entry5} ${entry6}"
                    log ""
                    ;;

            esac
        done < "${TMPDIR}/${failure_file}"
    done
    exit_code=1
fi


log ''
log '===================================='
log 'KEY STATS'
log '===================================='
log ''
log "Config files scanned:                       ${#@}"
log "Update xml files downloaded and parsed:     ${number_of_update_xml_urls}"
log "Unique mar urls found:                      ${number_of_mar_urls}"
log "Failures:                                   ${number_of_failures}"
log "Parallel processes used (maximum limit):    ${MAX_PROCS}"
log "Execution time:                             $((STOP_TIME-START_TIME)) seconds"
log ''

rm -rf "${TMPDIR}"
exit ${exit_code}
