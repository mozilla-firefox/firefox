#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
ACTIVITY="org.mozilla.fenix.HomeActivity"
TOOLBAR_RESOURCE_ID="ADDRESSBAR_URL_BOX"
TABS_TRAY_BUTTON_RESOURCE_ID="ADDRESSBAR_TABS_COUNTER"
LAUNCH_EXTRAS="--ez performancetest true"

XML_FILE=$TESTING_DIR/window_dump.xml
XMLSTARLET_CMD=${XMLSTARLET:-xmlstarlet}

# Emulates `xmlstarlet sel -t -v XPATH FILE` on hosts without xmlstarlet,
# such as the macOS workers running the Android emulator.
xmllint_sel() {
    xmllint --xpath "string($4)" "$5" 2>/dev/null
}

if ! command -v "$XMLSTARLET_CMD" > /dev/null && command -v xmllint > /dev/null; then
    XMLSTARLET_CMD=xmllint_sel
fi
TEST_TIME=$1

URL_MOZILLA="https://www.mozilla.org/"

if [[ $BROWSER_BINARY == *"chrome"* ]]; then
  ACTIVITY="com.google.android.apps.chrome.Main"
  TOOLBAR_RESOURCE_ID="$BROWSER_BINARY:id/search_box_text"
  TOOLBAR_FALLBACK_RESOURCE_ID="$BROWSER_BINARY:id/url_bar"
  TABS_TRAY_BUTTON_RESOURCE_ID="$BROWSER_BINARY:id/tab_switcher_button"
  LAUNCH_EXTRAS=""
fi

TAP_X=0
TAP_Y=0

fail() {
    echo "ERROR: $1"
    pkill -f collect-proc-info
    exit 1
}

calculate_tap_coords() {
    if [ -z "$1" ]; then
        fail "Could not find bounds for $2"
    fi
    x1=$(($(echo "$1" | awk -F'[][]' '{print $2}' | awk -F',' '{print $1}')))
    x2=$(($(echo "$1" | awk -F'[][]' '{print $4}' | awk -F',' '{print $1}')))
    sum_x=$(($x1+$x2))

    y1=$(($(echo "$1" | awk -F'[][]' '{print $2}' | awk -F',' '{print $2}')))
    y2=$(($(echo "$1" | awk -F'[][]' '{print $4}' | awk -F',' '{print $2}')))
    sum_y=$(($y1+$y2))

    TAP_X=$(($sum_x/2))
    TAP_Y=$(($sum_y/2))
}

tap_at_coords(){
    adb shell input tap $TAP_X $TAP_Y
}

adb shell pm clear $BROWSER_BINARY
adb shell am start $LAUNCH_EXTRAS -n "$BROWSER_BINARY/$ACTIVITY"
sleep 4

if [[ $BROWSER_BINARY == *"chrome"* ]]; then
    # navigate away from the first run prompt
    adb shell uiautomator dump
    adb pull /sdcard/window_dump.xml $XML_FILE
    sleep 1

    DISMISS_BOUNDS=$($XMLSTARLET_CMD sel -t -v 'string(//node[@resource-id = "'$BROWSER_BINARY':id/signin_fre_dismiss_button"]/@bounds)' "$XML_FILE")
    sleep 1

    if [ -n "$DISMISS_BOUNDS" ]; then
        calculate_tap_coords "$DISMISS_BOUNDS"
        tap_at_coords
    fi
    sleep 2

    # navigate away from privacy notice
    adb shell uiautomator dump
    adb pull /sdcard/window_dump.xml $XML_FILE
    sleep 1

    DISMISS_BOUNDS=$($XMLSTARLET_CMD sel -t -v 'string(//node[@resource-id = "'$BROWSER_BINARY':id/ack_button"]/@bounds)' "$XML_FILE")
    sleep 1

    if [ -n "$DISMISS_BOUNDS" ]; then
        calculate_tap_coords "$DISMISS_BOUNDS"
        tap_at_coords
    fi
    sleep 1

    # navigate away from notification permission prompt
    adb shell uiautomator dump
    adb pull /sdcard/window_dump.xml $XML_FILE
    sleep 1

    DISMISS_BOUNDS=$($XMLSTARLET_CMD sel -t -v 'string(//node[@resource-id = "'$BROWSER_BINARY':id/negative_button"]/@bounds)' "$XML_FILE")
    sleep 1

    if [ -n "$DISMISS_BOUNDS" ]; then
        calculate_tap_coords "$DISMISS_BOUNDS"
        tap_at_coords
    fi
    sleep 1
fi

adb shell uiautomator dump
adb pull /sdcard/window_dump.xml $XML_FILE
sleep 1

# calculate toolbar coordinates
TOOLBAR_BOUNDS=$($XMLSTARLET_CMD sel -t -v '//node[@resource-id = "'$TOOLBAR_RESOURCE_ID'"]/@bounds' $XML_FILE)
# Chrome may start on a non-NTP homepage (e.g. carrier builds), which only has the omnibox
if [ -z "$TOOLBAR_BOUNDS" ] && [ -n "$TOOLBAR_FALLBACK_RESOURCE_ID" ]; then
    TOOLBAR_RESOURCE_ID=$TOOLBAR_FALLBACK_RESOURCE_ID
    TOOLBAR_BOUNDS=$($XMLSTARLET_CMD sel -t -v '//node[@resource-id = "'$TOOLBAR_RESOURCE_ID'"]/@bounds' $XML_FILE)
fi
sleep 1

calculate_tap_coords "$TOOLBAR_BOUNDS" "$TOOLBAR_RESOURCE_ID"
TOOLBAR_X_COORDINATE=$TAP_X
TOOLBAR_Y_COORDINATE=$TAP_Y

# calculate tabs tray coordinates
TABS_TRAY_BUTTON_BOUNDS=$($XMLSTARLET_CMD sel -t -v '//node[@resource-id = "'$TABS_TRAY_BUTTON_RESOURCE_ID'"]/@bounds' $XML_FILE)
sleep 1

calculate_tap_coords "$TABS_TRAY_BUTTON_BOUNDS" "$TABS_TRAY_BUTTON_RESOURCE_ID"
TABS_TRAY_BUTTON_X_COORDINATE=$TAP_X
TABS_TRAY_BUTTON_Y_COORDINATE=$TAP_Y

adb shell input tap $TABS_TRAY_BUTTON_X_COORDINATE $TABS_TRAY_BUTTON_Y_COORDINATE
sleep 2

adb shell uiautomator dump
adb pull /sdcard/window_dump.xml $XML_FILE

# calculate new tab button coordinates
if [[ $BROWSER_BINARY == *"chrome"* ]]; then
    ADD_TAB_BUTTON_BOUNDS=$($XMLSTARLET_CMD sel -t -v '//node[@resource-id="new_tab_view_button" or @resource-id="'$BROWSER_BINARY':id/toolbar_action_button"]/@bounds' $XML_FILE)
else
    ADD_TAB_BUTTON_BOUNDS=$($XMLSTARLET_CMD sel -t -v '//node[@content-desc="Add tab"]/@bounds' $XML_FILE)
fi
sleep 1

calculate_tap_coords "$ADD_TAB_BUTTON_BOUNDS" "add tab button"
ADD_TAB_BUTTON_X_COORDINATE=$TAP_X
ADD_TAB_BUTTON_Y_COORDINATE=$TAP_Y

rm $XML_FILE

# go back to main page to start testing
adb shell input keyevent KEYCODE_BACK
sleep 1

function tapToFocusToolbar() {
    adb shell input tap $TOOLBAR_X_COORDINATE $TOOLBAR_Y_COORDINATE
    sleep 4
}

function inputTextToToolbar() {
    local text=$1
    while [ -n "$text" ]; do
        adb shell input text "${text:0:4}"
        text=${text:4}
        sleep 0.5
    done
    sleep 2
}

function tapEnterAndWait5s() {
    adb shell input keyevent 66
    sleep 5
}

function tapEnterAndWait10s() {
    adb shell input keyevent 66
    sleep 10
}

function performScrollDown() {
    adb shell input swipe 500 500 500 300
    adb shell input swipe 500 500 500 300
    adb shell input swipe 500 500 500 300
    sleep 2
}

function performScrollUp() {
    adb shell input swipe 500 300 500 500
    adb shell input swipe 500 300 500 500
    adb shell input swipe 500 300 500 500
    sleep 2
}

function tapToOpenTabsTray() {
    adb shell input tap $TABS_TRAY_BUTTON_X_COORDINATE $TABS_TRAY_BUTTON_Y_COORDINATE
    sleep 2
}

function tapToAddTab() {
    adb shell input tap $ADD_TAB_BUTTON_X_COORDINATE $ADD_TAB_BUTTON_Y_COORDINATE
    sleep 3
}

function addTab() {
    tapToOpenTabsTray
    tapToAddTab
}

function surfingSingleSite() {
    tapToFocusToolbar
    inputTextToToolbar $1
    tapEnterAndWait10s
    performScrollDown
    performScrollUp
}

function appToBackground() {
    adb shell input keyevent KEYCODE_HOME
    sleep 2
}

surfingSingleSite $URL_MOZILLA

if [ "$RUN_BACKGROUND" = True ]; then
    appToBackground
fi

# at this point our system is ready, the buttons' coordinates are generated
# test starts after this line
touch $TESTING_DIR/test_start.signal
sleep $(($TEST_TIME+10)) # wait 10 mins in the background
touch $TESTING_DIR/test_end.signal
adb shell am force-stop $BROWSER_BINARY
