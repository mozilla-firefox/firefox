#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Name: background-resource
# Owner: Perf Team
# Description: Runs a background CPU test on mobile
# Options: {"default": {"perfherder": true, "perfherder-metrics": [{ "name": "time", "unit": "s" }, { "name": "rss-memory", "unit": "s" }, { "name": "pss-memory", "unit": "s" }]}} #noqa

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
if [ "$MOZPERFTEST_SIMPLEPERF" = "1" ] && [ -z "$RESOURCE_PROFILING_WRAPPED" ]; then
    export RESOURCE_PROFILING_WRAPPED=1
    exec "$PYTHON_PATH_SHELL_SCRIPT" "$SCRIPT_DIR/profile_resource_usage.py" "$0"
fi
TEST_TIME=600
export RUN_BACKGROUND=True

bash $SCRIPT_DIR/collect-proc-info-cpu.sh $SCRIPT_DIR $TEST_TIME &
bash $SCRIPT_DIR/collect-proc-info-memory.sh $SCRIPT_DIR $TEST_TIME &
bash $SCRIPT_DIR/test-android-resource-usage.sh $TEST_TIME &
TEST_PID=$!
wait $TEST_PID
TEST_STATUS=$?
wait
pkill -f "sh test-background-resource-usage"
pkill -f "sh collect-proc-info-cpu"
pkill -f "sh collect-proc-info-memory"
rm -f $TESTING_DIR/test_start.signal
rm -f $TESTING_DIR/test_end.signal
pkill -f "sh main"

if [ $TEST_STATUS -ne 0 ]; then
    exit $TEST_STATUS
fi

python3 $SCRIPT_DIR/parse_resource_usage.py $TESTING_DIR $BROWSER_BINARY $RUN_BACKGROUND
