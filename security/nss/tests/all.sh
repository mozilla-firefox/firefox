#!/bin/bash
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

########################################################################
#
# mozilla/security/nss/tests/all.sh
#
# Script to start selected available NSS QA suites on one machine
# this script is called or sourced by NSS QA which runs on all required
# platforms
#
# Needs to work on all Unix and Windows platforms
#
# Currently available NSS QA suites:
# ----------------------------------
#   cipher.sh    - tests NSS ciphers
#   libpkix.sh   - tests PKIX functionality
#   cert.sh      - exercises certutil and creates certs necessary for
#                  all other tests
#   dbtests.sh   - tests related to certificate databases
#   tools.sh     - tests the majority of the NSS tools
#   fips.sh      - tests basic functionallity of NSS in FIPS-compliant
#                - mode
#   sdr.sh       - tests NSS SDR
#   smime.sh     - S/MIME testing
#   ssl.sh       - tests SSL V2 SSL V3 and TLS
#   ocsp.sh      - OCSP testing
#   merge.sh     - tests merging old and new shareable databases
#   pkits.sh     - NIST/PKITS tests
#   chains.sh    - PKIX cert chains tests
#   dbupgrade.sh - upgrade databases to new shareable version (used
#                  only in upgrade test cycle)
#   memleak.sh   - memory leak testing (optional)
#   ssl_gtests.sh- Gtest based unit tests for ssl
#   gtests.sh    - Gtest based unit tests for everything else
#   policy.sh    - Crypto Policy tests
#   bogo.sh      - Bogo interop tests (needs go, git)
#                  https://boringssl.googlesource.com/boringssl/+/master/ssl/test/PORTING.md
#   tlsfuzzer.sh - tlsfuzzer interop tests (needs python3, git)
#                  https://github.com/tomato42/tlsfuzzer/
#   mpi.sh       - MPI unit tests (needs mpi_tests binary)
#   cipher-noaes.sh    - Cipher tests with hardware AES disabled
#   cipher-noavx.sh    - Cipher tests with AVX disabled
#   cipher-nopclmul.sh - Cipher tests with PCLMUL disabled
#   cipher-nosha.sh    - Cipher tests with hardware SHA disabled
#   cipher-nosse41.sh  - Cipher tests with SSE4.1 disabled
#   cipher-nossse3.sh  - Cipher tests with SSSE3/NEON disabled
#
# NSS testing is now devided to 4 cycles:
# ---------------------------------------
#   standard     - run test suites with defaults settings
#   pkix         - run test suites with PKIX enabled
#   upgradedb    - upgrade existing certificate databases to shareable
#                  format (creates them if doesn't exist yet) and run
#                  test suites with those databases. Requires to enable libdm.
#   sharedb      - run test suites with shareable database format
#                  enabled (databases are created directly to this
#                  format). This is the default and doesn't need to be run separately.
#   threadunsafe - run test suites with thread unsafe environment variable
#                  so simulate running NSS locking for PKCS #11 modules which
#                  are not thread safe.
#
# Mandatory environment variables (to be set before testing):
# -----------------------------------------------------------
#   HOST         - test machine host name
#   DOMSUF       - test machine domain name
#
# Optional environment variables to specify build to use:
# -------------------------------------------------------
#   BUILT_OPT    - use optimized/debug build
#   USE_64       - use 64bit/32bit build
#
# Optional environment variables to select which cycles/suites to test:
# ---------------------------------------------------------------------
#   NSS_CYCLES     - list of cycles to run (separated by space
#                    character)
#                  - by default all cycles are tested
#
#   NSS_TESTS      - list of all test suites to run (separated by space
#                    character, without trailing .sh)
#                  - this list can be reduced for individual test cycles
#   NSS_THREAD_TESTS - list of test suites run in the threadunsafe cycle
#
#   NSS_SSL_TESTS  - list of ssl tests to run (see ssl.sh)
#   NSS_SSL_RUN    - list of ssl sub-tests to run (see ssl.sh)
#
# Testing schema:
# ---------------
#                           all.sh                       ~  (main)
#                              |                               |
#          +------------+------------+-----------+---    ~  run_cycles
#          |            |            |           |             |
#      standard       pkix       upgradedb     sharedb   ~  run_cycle_*
#         ...           |           ...         ...            |
#                +------+------+------+----->            ~  run_tests
#                |      |      |      |                        |
#              cert   tools   fips   ssl   ...           ~  . *.sh
#
# Special strings:
# ----------------
#   FIXME ... known problems, search for this string
#   NOTE .... unexpected behavior
#
# NOTE:
# -----
#   Unlike the old QA this is based on files sourcing each other
#   This is done to save time, since a great portion of time is lost
#   in calling and sourcing the same things multiple times over the
#   network. Also, this way all scripts have all shell function
#   available and a completely common environment
#
########################################################################

RUN_FIPS=""

########################################################################
# Output formatting - use color when stdout is a terminal
# Note: this check must happen before run_cycles is piped through tee,
# since the pipe makes -t 1 return false in the subshell.
########################################################################
if [ -z "${NSS_TEST_COLOR+set}" ]; then
    if [ -t 1 ]; then
        NSS_TEST_COLOR=1
    else
        NSS_TEST_COLOR=0
    fi
fi
export NSS_TEST_COLOR

if [ "$NSS_TEST_COLOR" = "1" ]; then
    COLOR_RED='\033[0;31m'
    COLOR_GREEN='\033[0;32m'
    COLOR_YELLOW='\033[0;33m'
    COLOR_BOLD='\033[1m'
    COLOR_RESET='\033[0m'
else
    COLOR_RED=''
    COLOR_GREEN=''
    COLOR_YELLOW=''
    COLOR_BOLD=''
    COLOR_RESET=''
fi
export COLOR_RED COLOR_GREEN COLOR_YELLOW COLOR_BOLD COLOR_RESET

############################## run_tests ###############################
# run test suites defined in TESTS variable, skip scripts defined in
# TESTS_SKIP variable
########################################################################
run_tests()
{
    echo ""
    printf "${COLOR_BOLD}Running test cycle: ${TEST_MODE}${COLOR_RESET}\n"

    # Count total runnable tests for progress indicator.
    local total=0
    local current=0
    for TEST in ${TESTS}; do
        echo " ${TESTS_SKIP} ${TESTS_SKIP_MISSING} " | grep " ${TEST} " > /dev/null
        if [ $? -ne 0 ]; then
            total=$((total + 1))
        fi
    done

    for TEST in ${TESTS}
    do
        SCRIPTNAME=${TEST}.sh

        # NOTE: the spaces are important. If you don't include
        # the spaces, then turning off ssl_gtests will also turn off ssl
        # tests.

        # Check for dependency-missing skip.
        echo " ${TESTS_SKIP_MISSING} " | grep " ${TEST} " > /dev/null
        if [ $? -eq 0 ]; then
            # Find the reason from the dep check output.
            printf "  ${COLOR_YELLOW}SKIP${COLOR_RESET} ${TEST} (missing dependencies)\n"
            continue
        fi

        # Check for cycle skip.
        echo " ${TESTS_SKIP} " | grep " ${TEST} " > /dev/null
        if [ $? -eq 0 ]; then
            printf "  ${COLOR_YELLOW}SKIP${COLOR_RESET} ${TEST} (not run in ${TEST_MODE} cycle)\n"
            continue
        fi

        current=$((current + 1))

        # Snapshot the results file line count before the test runs.
        local fail_count_before=0
        if [ -f "${RESULTS}" ]; then
            fail_count_before=$(grep -c '>Failed<\|>Failed Core<' "${RESULTS}" 2>/dev/null)
            fail_count_before=${fail_count_before:-0}
        fi

        # Buffer test output to a temp file.
        local test_output="${HOSTDIR}/${TEST_MODE}.${TEST}.output"

        printf "[%d/%d] Running ${COLOR_BOLD}${TEST}${COLOR_RESET}..." "$current" "$total"

        local start_secs=$SECONDS
        (cd ${QADIR}/${TEST}; . ./${SCRIPTNAME} 2>&1) > "${test_output}" 2>&1
        local elapsed=$(( SECONDS - start_secs ))

        # Format elapsed time.
        local time_str
        if [ $elapsed -ge 60 ]; then
            time_str="$((elapsed / 60))m$((elapsed % 60))s"
        else
            time_str="${elapsed}s"
        fi

        # Check for new failures by comparing results file.
        local fail_count_after=0
        if [ -f "${RESULTS}" ]; then
            fail_count_after=$(grep -c '>Failed<\|>Failed Core<' "${RESULTS}" 2>/dev/null)
            fail_count_after=${fail_count_after:-0}
        fi

        local new_failures=$((fail_count_after - fail_count_before))

        # Always append full test output to logfile for archival,
        # regardless of pass/fail.
        cat "${test_output}" >> "${LOGFILE}" 2>/dev/null

        if [ $new_failures -gt 0 ]; then
            printf " ${COLOR_RED}FAILED${COLOR_RESET} (${new_failures} failures, ${time_str})\n"
            # Dump the full output to stderr so the user sees it
            # without it being captured again by the tee to LOGFILE.
            echo "--- output of ${TEST} (${TEST_MODE}) ---" >&2
            cat "${test_output}" >&2
            echo "--- end of ${TEST} (${TEST_MODE}) ---" >&2
            echo "${TEST_MODE}/${TEST}" >> "${FAILED_TESTS_FILE}"
        else
            printf " ${COLOR_GREEN}PASSED${COLOR_RESET} (${time_str})\n"
        fi

        rm -f "${test_output}"
    done
}

########################## run_cycle_standard ##########################
# run test suites with sql database
########################################################################
run_cycle_standard()
{
    TEST_MODE=STANDARD

    TESTS="${ALL_TESTS}"
    TESTS_SKIP="libpkix pkits"

    NSS_DEFAULT_DB_TYPE=${NSS_DEFAULT_DB_TYPE:-"sql"}
    export NSS_DEFAULT_DB_TYPE

    run_tests
}

############################ run_cycle_pkix ############################
# run test suites with PKIX enabled
########################################################################
run_cycle_pkix()
{
    TEST_MODE=PKIX

    TABLE_ARGS="bgcolor=cyan"
    html_head "Testing with PKIX"
    html "</TABLE><BR>"

    HOSTDIR="${HOSTDIR}/pkix"
    mkdir -p "${HOSTDIR}"
    init_directories

    TESTS="${ALL_TESTS}"
    TESTS_SKIP="cipher cipher-noaes cipher-noavx cipher-nopclmul cipher-nosha cipher-nosse41 cipher-nossse3 dbtests sdr smime merge multinit"

    export -n NSS_SSL_RUN

    # use the default format. (unset for the shell, export -n for binaries)
    export -n NSS_DEFAULT_DB_TYPE
    unset NSS_DEFAULT_DB_TYPE

    run_tests
}

######################### run_cycle_upgrade_db #########################
# upgrades certificate database to shareable format and run test suites
# with those databases
########################################################################
run_cycle_upgrade_db()
{
    TEST_MODE=UPGRADE_DB

    TABLE_ARGS="bgcolor=pink"
    html_head "Testing with upgraded library"
    html "</TABLE><BR>"

    OLDHOSTDIR="${HOSTDIR}"
    HOSTDIR="${HOSTDIR}/upgradedb"
    mkdir -p "${HOSTDIR}"
    init_directories

    if [ -r "${OLDHOSTDIR}/cert.log" ]; then
        DIRS="alicedir bobdir CA cert_extensions client clientCA dave eccurves eve ext_client ext_server $RUN_FIPS SDR server serverCA stapling tools/copydir cert.log cert.done tests.*"
        for i in $DIRS
        do
            cp -r ${OLDHOSTDIR}/${i} ${HOSTDIR} #2> /dev/null
        done
    fi

    # upgrade certs dbs to shared db
    TESTS="dbupgrade"
    TESTS_SKIP=

    run_tests

    NSS_DEFAULT_DB_TYPE="sql"
    export NSS_DEFAULT_DB_TYPE

    # run the subset of tests with the upgraded database
    TESTS="${ALL_TESTS}"
    TESTS_SKIP="cipher cipher-noaes cipher-noavx cipher-nopclmul cipher-nosha cipher-nosse41 cipher-nossse3 libpkix cert dbtests sdr ocsp pkits chains"

    run_tests
}

########################## run_cycle_shared_db #########################
# run test suites with certificate databases set to shareable format
########################################################################
run_cycle_shared_db()
{
    TEST_MODE=SHARED_DB

    TABLE_ARGS="bgcolor=yellow"
    html_head "Testing with shared library"
    html "</TABLE><BR>"

    HOSTDIR="${HOSTDIR}/sharedb"
    mkdir -p "${HOSTDIR}"
    init_directories

    NSS_DEFAULT_DB_TYPE="sql"
    export NSS_DEFAULT_DB_TYPE

    # run the tests for native sharedb support
    TESTS="${ALL_TESTS}"
    TESTS_SKIP="dbupgrade"

    export -n NSS_SSL_TESTS
    export -n NSS_SSL_RUN

    run_tests
}

########################## run_thread_unsafe #########################
# run test suites with an non-thread safe softoken
# This simulates loading a non-threadsafe PKCS #11 module and makes
# Sure we don't have any deadlocks in our locking code
########################################################################
run_cycle_thread_unsafe()
{
    TEST_MODE=THREAD_UNSAFE

    TABLE_ARGS="bgcolor=lightgray"
    html_head "Testing with non-threadsafe softoken"
    html "</TABLE><BR>"

    HOSTDIR="${HOSTDIR}/threadunsafe"
    mkdir -p "${HOSTDIR}"
    init_directories

    NSS_FORCE_TOKEN_LOCK=1
    export NSS_FORCE_TOKEN_LOCK

    # run the tests for appropriate for thread unsafe
    # basically it's the ssl tests right now. 
    TESTS="${THREAD_TESTS}"
    TESTS_SKIP="dbupgrade"

    export -n NSS_SSL_TESTS
    export -n NSS_SSL_RUN

    run_tests
}

############################# run_cycles ###############################
# run test cycles defined in CYCLES variable
########################################################################
run_cycles()
{
    for CYCLE in ${CYCLES}
    do
        case "${CYCLE}" in
        "standard")
            run_cycle_standard
            ;;
        "pkix")
            if [ -z "$NSS_DISABLE_LIBPKIX" ]; then
                run_cycle_pkix
            fi
            ;;
        "upgradedb")
            run_cycle_upgrade_db
            ;;
        "sharedb")
            run_cycle_shared_db
            ;;
        "threadunsafe")
            run_cycle_thread_unsafe
            ;;
        esac
        . ${ENV_BACKUP}
    done
}

############################## main code ###############################

SCRIPTNAME=all.sh
CLEANUP="${SCRIPTNAME}"
cd `dirname $0`

# all.sh should be the first one to try to source the init
if [ -z "${INIT_SOURCED}" -o "${INIT_SOURCED}" != "TRUE" ]; then
    cd common
    . ./init.sh
fi

# Track failed tests across all cycles for the final summary.
# Written to a file because run_cycles may execute in a pipe subshell.
FAILED_TESTS_FILE="${HOSTDIR}/failed_tests.lst"
: > "${FAILED_TESTS_FILE}"
export FAILED_TESTS_FILE

########################################################################
# Dependency checks for optional tests - warn but don't fail
########################################################################
check_dep()
{
    local test_name="$1"
    shift
    local missing=""
    for cmd in "$@"; do
        if ! command -v "$cmd" > /dev/null 2>&1; then
            missing="$missing $cmd"
        fi
    done
    if [ -n "$missing" ]; then
        printf "${COLOR_YELLOW}WARNING:${COLOR_RESET} '${test_name}' tests will be skipped - missing:${missing}\n"
        TESTS_SKIP_MISSING="${TESTS_SKIP_MISSING} ${test_name}"
    fi
}

TESTS_SKIP_MISSING=""
if [ ! -x "${BINDIR}/mpi_tests" ]; then
    printf "${COLOR_YELLOW}WARNING:${COLOR_RESET} 'mpi' tests will be skipped - missing: ${BINDIR}/mpi_tests\n"
    TESTS_SKIP_MISSING="${TESTS_SKIP_MISSING} mpi"
fi

cycles="standard"
CYCLES=${NSS_CYCLES:-$cycles}

printf "${COLOR_BOLD}Test cycles:${COLOR_RESET} ${CYCLES}\n"
if [ "$CYCLES" = "standard" ]; then
    echo "  Additional cycles available: pkix, threadunsafe, upgradedb, sharedb"
    echo "  Enable with: NSS_CYCLES=\"standard pkix threadunsafe\""
fi

NO_INIT_SUPPORT=`certutil --build-flags |grep -cw NSS_NO_INIT_SUPPORT`
IS_FIPS_DISABLED=`certutil --build-flags |grep -cw NSS_FIPS_DISABLED`
if [ $NO_INIT_SUPPORT -eq 0 ] && [ $IS_FIPS_DISABLED -eq 0 ]; then
    RUN_FIPS="fips"
fi

tests="cipher lowhash libpkix cert dbtests tools $RUN_FIPS sdr smime ssl ocsp merge pkits ec gtests ssl_gtests policy mpi cipher-noaes cipher-noavx cipher-nopclmul cipher-nosha cipher-nosse41 cipher-nossse3"
thread_tests="ssl ssl_gtests"
# Don't run chains tests when we have a gyp build.
if [ "$OBJDIR" != "Debug" -a "$OBJDIR" != "Release" ]; then
  tests="$tests chains"
fi
TESTS=${NSS_TESTS:-$tests}

ALL_TESTS=${TESTS}
default_thread=""
for i in ${ALL_TESTS}
do
    for j in ${thread_tests}
    do
        if [ $i = $j ]; then 
            default_thread="$default_thread $i"
        fi
    done
done
THREAD_TESTS=${NSS_THREAD_TESTS-$default_thread}

nss_ssl_tests="crl iopr policy normal_normal"
if [ $NO_INIT_SUPPORT -eq 0 ]; then
    nss_ssl_tests="$nss_ssl_tests fips_normal normal_fips"
fi
NSS_SSL_TESTS="${NSS_SSL_TESTS:-$nss_ssl_tests}"

nss_ssl_run="cov auth signed_cert_timestamps scheme"
NSS_SSL_RUN="${NSS_SSL_RUN:-$nss_ssl_run}"

printf "${COLOR_BOLD}SSL test modes:${COLOR_RESET} ${NSS_SSL_TESTS}\n"
echo "  Override with: NSS_SSL_TESTS=\"crl iopr policy normal_normal fips_normal normal_fips\""
printf "${COLOR_BOLD}SSL sub-tests:${COLOR_RESET}  ${NSS_SSL_RUN}\n"
echo "  Override with: NSS_SSL_RUN=\"cov auth stapling signed_cert_timestamps stress scheme\""

# NOTE:
# Lists of enabled tests and other settings are stored to ${ENV_BACKUP}
# file and are are restored after every test cycle.

ENV_BACKUP=${HOSTDIR}/env.sh
env_backup > ${ENV_BACKUP}

# Print hardware support if we built it.
if [ -f ${BINDIR}/hw-support ]; then
    ${BINDIR}/hw-support
fi

if [ "${O_CRON}" = "ON" ]; then
    run_cycles >> ${LOGFILE}
else
    run_cycles | tee -a ${LOGFILE}
fi

SCRIPTNAME=all.sh

. ${QADIR}/common/cleanup.sh
