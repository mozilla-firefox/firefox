/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var { Preferences } = ChromeUtils.importESModule(
  "resource://gre/modules/Preferences.sys.mjs"
);
var { TelemetryReportingPolicy } = ChromeUtils.importESModule(
  "resource://gre/modules/TelemetryReportingPolicy.sys.mjs"
);

const PREF_BRANCH = "datareporting.policy.";
const PREF_FIRST_RUN = "toolkit.telemetry.reportingpolicy.firstRun";
const PREF_BYPASS_NOTIFICATION =
  PREF_BRANCH + "dataSubmissionPolicyBypassNotification";
const PREF_CURRENT_POLICY_VERSION = PREF_BRANCH + "currentPolicyVersion";
const PREF_ACCEPTED_POLICY_VERSION =
  PREF_BRANCH + "dataSubmissionPolicyAcceptedVersion";
const PREF_ACCEPTED_POLICY_DATE =
  PREF_BRANCH + "dataSubmissionPolicyNotifiedTime";
const PREF_TOS_ROLLOUT_POPULATION =
  "browser.preonboarding.onTrainRolloutPopulation";
const PREF_TOS_ROLLOUT_ENROLLED =
  "browser.preonboarding.enrolledInOnTrainRollout";

const PREF_TELEMETRY_LOG_LEVEL = "toolkit.telemetry.log.level";

const TEST_POLICY_VERSION = 37;

function fakeShowPolicyTimeout(set, clear) {
  let reportingPolicy = ChromeUtils.importESModule(
    "resource://gre/modules/TelemetryReportingPolicy.sys.mjs"
  ).Policy;
  reportingPolicy.setShowInfobarTimeout = set;
  reportingPolicy.clearShowInfobarTimeout = clear;
}

async function sendSessionRestoredNotification() {
  let reportingPolicy = ChromeUtils.importESModule(
    "resource://gre/modules/TelemetryReportingPolicy.sys.mjs"
  ).Policy;

  await reportingPolicy.fakeSessionRestoreNotification();
}

/**
 * Wait for a tick.
 */
function promiseNextTick() {
  return new Promise(resolve => executeSoon(resolve));
}

/**
 * Wait for a notification to be shown in a notification box.
 * @param {Object} aNotificationBox The notification box.
 * @return {Promise} Resolved when the notification is displayed.
 */
function promiseWaitForAlertActive(aNotificationBox) {
  let deferred = Promise.withResolvers();
  aNotificationBox.stack.addEventListener(
    "AlertActive",
    function () {
      deferred.resolve();
    },
    { once: true }
  );
  return deferred.promise;
}

/**
 * Wait for a notification to be closed.
 * @param {Object} aNotification The notification.
 * @return {Promise} Resolved when the notification is closed.
 */
function promiseWaitForNotificationClose(aNotification) {
  let deferred = Promise.withResolvers();
  waitForNotificationClose(aNotification, deferred.resolve);
  return deferred.promise;
}

async function triggerInfoBar(expectedTimeoutMs) {
  let showInfobarCallback = null;
  let timeoutMs = null;
  fakeShowPolicyTimeout(
    (callback, timeout) => {
      showInfobarCallback = callback;
      timeoutMs = timeout;
    },
    () => {}
  );
  await sendSessionRestoredNotification();
  Assert.ok(!!showInfobarCallback, "Must have a timer callback.");
  if (expectedTimeoutMs !== undefined) {
    Assert.equal(timeoutMs, expectedTimeoutMs, "Timeout should match");
  }
  showInfobarCallback();
}

var checkInfobarButton = async function (aNotification) {
  // Check that the button on the data choices infobar does the right thing.
  let buttons = aNotification.buttonContainer.getElementsByTagName("button");
  Assert.equal(
    buttons.length,
    1,
    "There is 1 button in the data reporting notification."
  );
  let button = buttons[0];

  let openPrefsPromise = BrowserTestUtils.waitForLocationChange(
    gBrowser,
    "about:preferences#privacy"
  );

  // Click on the button.
  button.click();

  // Wait for the preferences panel to open.
  await openPrefsPromise;
};

add_setup(async function () {
  const isFirstRun = Preferences.get(PREF_FIRST_RUN, true);
  const bypassNotification = Preferences.get(PREF_BYPASS_NOTIFICATION, true);
  const currentPolicyVersion = Preferences.get(PREF_CURRENT_POLICY_VERSION, 1);

  // Register a cleanup function to reset our preferences.
  registerCleanupFunction(() => {
    Preferences.set(PREF_FIRST_RUN, isFirstRun);
    Preferences.set(PREF_BYPASS_NOTIFICATION, bypassNotification);
    Preferences.set(PREF_CURRENT_POLICY_VERSION, currentPolicyVersion);
    Preferences.reset(PREF_TELEMETRY_LOG_LEVEL);
    Preferences.reset(PREF_TOS_ROLLOUT_ENROLLED);
    Preferences.reset(PREF_TOS_ROLLOUT_POPULATION);

    return closeAllNotifications();
  });

  // Don't skip the infobar visualisation.
  Preferences.set(PREF_BYPASS_NOTIFICATION, false);
  // Set the current policy version.
  Preferences.set(PREF_CURRENT_POLICY_VERSION, TEST_POLICY_VERSION);
  // Ensure this isn't the first run, because then we open the first run page.
  Preferences.set(PREF_FIRST_RUN, false);
  TelemetryReportingPolicy.testUpdateFirstRun();
  // Do not trigger ToS modal for Linux, Mac, and MSIX builds
  Preferences.set(PREF_TOS_ROLLOUT_ENROLLED, false);
  Preferences.set(PREF_TOS_ROLLOUT_POPULATION, 0);
});

function clearAcceptedPolicy() {
  // Reset the accepted policy.
  Preferences.reset(PREF_ACCEPTED_POLICY_VERSION);
  Preferences.reset(PREF_ACCEPTED_POLICY_DATE);
}

function assertCoherentInitialState() {
  // Make sure that we have a coherent initial state.
  Assert.equal(
    Preferences.get(PREF_ACCEPTED_POLICY_VERSION, 0),
    0,
    "No version should be set on init."
  );
  Assert.equal(
    Preferences.get(PREF_ACCEPTED_POLICY_DATE, 0),
    0,
    "No date should be set on init."
  );
  Assert.ok(
    !TelemetryReportingPolicy.testIsUserNotified(),
    "User not notified about datareporting policy."
  );
}

add_task(async function test_single_window() {
  TelemetryReportingPolicy.reset();
  clearAcceptedPolicy();

  // Close all the notifications, then try to trigger the data choices infobar.
  await closeAllNotifications();

  assertCoherentInitialState();

  let alertShownPromise = promiseWaitForAlertActive(gNotificationBox);
  Assert.ok(
    !TelemetryReportingPolicy.canUpload(),
    "User should not be allowed to upload."
  );

  // Wait for the infobar to be displayed.
  await triggerInfoBar(10 * 1000);

  await alertShownPromise;
  await promiseNextTick();

  Assert.equal(
    gNotificationBox.allNotifications.length,
    1,
    "Notification Displayed."
  );
  Assert.ok(
    TelemetryReportingPolicy.canUpload(),
    "User should be allowed to upload now."
  );

  await promiseNextTick();
  let promiseClosed = promiseWaitForNotificationClose(
    gNotificationBox.currentNotification
  );
  await checkInfobarButton(gNotificationBox.currentNotification);
  await promiseClosed;

  Assert.equal(
    gNotificationBox.allNotifications.length,
    0,
    "No notifications remain."
  );

  // Check that we are still clear to upload and that the policy data is saved.
  Assert.ok(TelemetryReportingPolicy.canUpload());
  Assert.equal(
    TelemetryReportingPolicy.testIsUserNotified(),
    true,
    "User notified about datareporting policy."
  );
  Assert.equal(
    Preferences.get(PREF_ACCEPTED_POLICY_VERSION, 0),
    TEST_POLICY_VERSION,
    "Version pref set."
  );
  Assert.greater(
    parseInt(Preferences.get(PREF_ACCEPTED_POLICY_DATE, null), 10),
    -1,
    "Date pref set."
  );
});

/* See bug 1571932
add_task(async function test_multiple_windows() {
  clearAcceptedPolicy();

  // Close all the notifications, then try to trigger the data choices infobar.
  await closeAllNotifications();

  // Ensure we see the notification on all windows and that action on one window
  // results in dismiss on every window.
  let otherWindow = await BrowserTestUtils.openNewBrowserWindow();

  Assert.ok(
    otherWindow.gNotificationBox,
    "2nd window has a global notification box."
  );

  assertCoherentInitialState();

  let showAlertPromises = [
    promiseWaitForAlertActive(gNotificationBox),
    promiseWaitForAlertActive(otherWindow.gNotificationBox),
  ];

  Assert.ok(
    !TelemetryReportingPolicy.canUpload(),
    "User should not be allowed to upload."
  );

  // Wait for the infobars.
  await triggerInfoBar(10 * 1000);
  await Promise.all(showAlertPromises);

  // Both notification were displayed. Close one and check that both gets closed.
  let closeAlertPromises = [
    promiseWaitForNotificationClose(gNotificationBox.currentNotification),
    promiseWaitForNotificationClose(
      otherWindow.gNotificationBox.currentNotification
    ),
  ];
  gNotificationBox.currentNotification.close();
  await Promise.all(closeAlertPromises);

  // Close the second window we opened.
  await BrowserTestUtils.closeWindow(otherWindow);

  // Check that we are clear to upload and that the policy data us saved.
  Assert.ok(
    TelemetryReportingPolicy.canUpload(),
    "User should be allowed to upload now."
  );
  Assert.equal(
    TelemetryReportingPolicy.testIsUserNotified(),
    true,
    "User notified about datareporting policy."
  );
  Assert.equal(
    Preferences.get(PREF_ACCEPTED_POLICY_VERSION, 0),
    TEST_POLICY_VERSION,
    "Version pref set."
  );
  Assert.greater(
    parseInt(Preferences.get(PREF_ACCEPTED_POLICY_DATE, null), 10),
    -1,
    "Date pref set."
  );
});*/
