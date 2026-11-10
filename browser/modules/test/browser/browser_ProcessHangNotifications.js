/* globals ProcessHangMonitor */

const { WebExtensionPolicy } = Cu.getGlobalForObject(Services);

function promiseNotificationShown(aWindow) {
  return new Promise(resolve => {
    let notificationBox = aWindow.gNotificationBox;
    notificationBox.stack.addEventListener(
      "AlertActive",
      function () {
        is(
          notificationBox.allNotifications.length,
          1,
          "Notification Displayed."
        );
        resolve(notificationBox);
      },
      { once: true }
    );
  });
}

function pushPrefs(...aPrefs) {
  return SpecialPowers.pushPrefEnv({ set: aPrefs });
}

function popPrefs() {
  return SpecialPowers.popPrefEnv();
}

const TEST_ACTION_UNKNOWN = 0;
const TEST_ACTION_CANCELLED = 1;
const TEST_ACTION_TERMSCRIPT = 2;
const TEST_ACTION_TERMGLOBAL = 3;
const SLOW_SCRIPT = 1;
const ADDON_HANG = 3;
const ADDON_ID = "fake-addon";

/**
 * A mock nsIHangReport that we can pass through nsIObserverService
 * to trigger notifications.
 *
 * @param hangType
 *        One of SLOW_SCRIPT, ADDON_HANG.
 * @param browser (optional)
 *        The <xul:browser> that this hang should be associated with.
 *        If not supplied, the hang will be associated with every browser,
 *        but the nsIHangReport.scriptBrowser attribute will return the
 *        currently selected browser in this window's gBrowser.
 */
let TestHangReport = function (
  hangType = SLOW_SCRIPT,
  browser = gBrowser.selectedBrowser
) {
  this.promise = new Promise(resolve => {
    this._resolver = resolve;
  });

  if (hangType == ADDON_HANG) {
    // Add-on hangs need an associated add-on ID for us to blame.
    this._addonId = ADDON_ID;
  }

  this._browser = browser;
};

TestHangReport.prototype = {
  get addonId() {
    return this._addonId;
  },

  QueryInterface: ChromeUtils.generateQI(["nsIHangReport"]),

  userCanceled() {
    this._resolver(TEST_ACTION_CANCELLED);
  },

  terminateScript() {
    this._resolver(TEST_ACTION_TERMSCRIPT);
  },

  isReportForBrowserOrChildren(aFrameLoader) {
    if (this._browser) {
      return this._browser.frameLoader === aFrameLoader;
    }

    return true;
  },

  get scriptBrowser() {
    return this._browser;
  },

  // Shut up warnings about this property missing:
  get scriptFileName() {
    return "chrome://browser/content/browser.js";
  },
};

add_setup(async function () {
  // Create a fake WebExtensionPolicy that we can use for
  // the add-on hang notification.
  const uuidGen = Services.uuid;
  const uuid = uuidGen.generateUUID().number.slice(1, -1);
  let policy = new WebExtensionPolicy({
    name: "Scapegoat",
    id: ADDON_ID,
    mozExtensionHostname: uuid,
    baseURL: "file:///",
    allowedOrigins: new MatchPatternSet([]),
    localizeCallback() {},
  });
  policy.active = true;

  registerCleanupFunction(() => {
    policy.active = false;
  });
});

/**
 * Test if hang reports receive a terminate script callback when the user selects
 * stop in response to a script hang.
 */
add_task(async function terminateScriptTest() {
  Services.fog.testResetFOG();
  let promise = promiseNotificationShown(window, "process-hang");
  let hangReport = new TestHangReport();
  Services.obs.notifyObservers(hangReport, "process-hang-report");
  let notification = await promise;

  let buttons =
    notification.currentNotification.buttonContainer.getElementsByTagName(
      "button"
    );
  is(buttons.length, 1, "Only one button displayed");

  // Click the "Stop" button, we should get a terminate script callback
  buttons[0].click();
  let action = await hangReport.promise;
  is(
    action,
    TEST_ACTION_TERMSCRIPT,
    "Clicking 'Stop' should have terminated the script."
  );
  const events = Glean.slowScriptWarning.shownContent.testGetValue();
  is(events.length, 1, "Recorded the correct number of events.");
  is(events[0].extra.end_reason, "user-aborted", "User aborted.");
  Assert.greater(Number(events[0].extra.uptime), 0, "Had some uptime.");
});

/**
 * Test if hang reports receive user canceled callbacks after a user selects wait
 * and the browser frees up from a script hang on its own.
 */
add_task(async function waitForScriptTest() {
  Services.fog.testResetFOG();
  let hangReport = new TestHangReport();
  let promise = promiseNotificationShown(window, "process-hang");
  Services.obs.notifyObservers(hangReport, "process-hang-report");
  let notification = await promise;

  let buttons =
    notification.currentNotification.buttonContainer.getElementsByTagName(
      "button"
    );
  is(buttons.length, 1, "Only one button displayed");

  await pushPrefs(["browser.hangNotification.waitPeriod", 1000]);

  let ignoringReport = true;

  hangReport.promise.then(action => {
    if (ignoringReport) {
      ok(
        false,
        "Hang report was somehow dealt with when it " +
          "should have been ignored."
      );
    } else {
      is(
        action,
        TEST_ACTION_CANCELLED,
        "Hang report should have been cancelled."
      );
      const events = Glean.slowScriptWarning.shownContent.testGetValue();
      is(events.length, 1, "Recorded the correct number of events.");
      is(events[0].extra.end_reason, "cleared", "Correct end reason.");
      is(events[0].extra.wait_count, "1", "Waited once.");
    }
  });

  // Click the "Close" button this time, we shouldn't get a callback at all.
  notification.currentNotification.closeButton.click();

  // send another hang pulse, we should not get a notification here
  Services.obs.notifyObservers(hangReport, "process-hang-report");
  is(
    notification.currentNotification,
    null,
    "no notification should be visible"
  );

  // Make sure that any queued Promises have run to give our report-ignoring
  // then() a chance to fire.
  await Promise.resolve();

  ignoringReport = false;
  Services.obs.notifyObservers(hangReport, "clear-hang-report");

  await popPrefs();
});

/**
 * Test if hang reports receive user canceled callbacks after the content
 * process stops sending hang notifications.
 */
add_task(async function hangGoesAwayTest() {
  Services.fog.testResetFOG();
  await pushPrefs(["browser.hangNotification.expiration", 1000]);

  let hangReport = new TestHangReport();
  let promise = promiseNotificationShown(window, "process-hang");
  Services.obs.notifyObservers(hangReport, "process-hang-report");
  await promise;

  Services.obs.notifyObservers(hangReport, "clear-hang-report");
  let action = await hangReport.promise;
  is(action, TEST_ACTION_CANCELLED, "Hang report should have been cancelled.");

  const events = Glean.slowScriptWarning.shownContent.testGetValue();
  is(events.length, 1, "Recorded the correct number of events.");
  is(events[0].extra.end_reason, "cleared", "Correct end reason.");
  is(events[0].extra.wait_count, "0", "Didn't wait.");

  await popPrefs();
});

/**
 * Tests that if we're shutting down, any pre-existing hang reports will
 * be terminated appropriately.
 */
add_task(async function terminateAtShutdown() {
  Services.fog.testResetFOG();
  let pausedHang = new TestHangReport(SLOW_SCRIPT);
  Services.obs.notifyObservers(pausedHang, "process-hang-report");
  ProcessHangMonitor.waitLonger(window);
  ok(
    ProcessHangMonitor.findPausedReport(gBrowser.selectedBrowser),
    "There should be a paused report for the selected browser."
  );

  let scriptHang = new TestHangReport(SLOW_SCRIPT);
  let addonHang = new TestHangReport(ADDON_HANG);

  [scriptHang, addonHang].forEach(hangReport => {
    Services.obs.notifyObservers(hangReport, "process-hang-report");
  });

  // Simulate the browser being told to shutdown. This should cause
  // hangs to terminate scripts.
  ProcessHangMonitor.onQuitApplicationGranted();

  // In case this test happens to throw before it can finish, make
  // sure to reset the shutting-down state.
  registerCleanupFunction(() => {
    ProcessHangMonitor._shuttingDown = false;
  });

  let pausedAction = await pausedHang.promise;
  let scriptAction = await scriptHang.promise;
  let addonAction = await addonHang.promise;

  is(
    pausedAction,
    TEST_ACTION_TERMSCRIPT,
    "On shutdown, should have terminated script for paused script hang."
  );
  is(
    scriptAction,
    TEST_ACTION_TERMSCRIPT,
    "On shutdown, should have terminated script for script hang."
  );
  is(
    addonAction,
    TEST_ACTION_TERMSCRIPT,
    "On shutdown, should have terminated script for add-on hang."
  );

  let events = Glean.slowScriptWarning.shownContent.testGetValue();
  Services.fog.testResetFOG(); // Clear already-checked events.
  is(events.length, 3, "Recorded one event per hang.");
  Assert.deepEqual(
    events.map(e => e.extra.end_reason),
    [
      "quit-application-granted",
      "quit-application-granted",
      "quit-application-granted",
    ],
    "Hangs all ended due to shutdown."
  );
  is(
    events.filter(e => e.extra.uri_type == "extension").length,
    1,
    "Had one extension hang."
  );
  is(
    events.filter(e => e.extra.wait_count == "1").length,
    1,
    "Had one paused hang."
  );

  // ProcessHangMonitor should now be in the "shutting down" state,
  // meaning that any further hangs should be handled immediately
  // without user interaction.
  let scriptHang2 = new TestHangReport(SLOW_SCRIPT);
  let addonHang2 = new TestHangReport(ADDON_HANG);

  [scriptHang2, addonHang2].forEach(hangReport => {
    Services.obs.notifyObservers(hangReport, "process-hang-report");
  });

  let scriptAction2 = await scriptHang.promise;
  let addonAction2 = await addonHang.promise;

  is(
    scriptAction2,
    TEST_ACTION_TERMSCRIPT,
    "On shutdown, should have terminated script for script hang."
  );
  is(
    addonAction2,
    TEST_ACTION_TERMSCRIPT,
    "On shutdown, should have terminated script for add-on hang."
  );

  events = Glean.slowScriptWarning.shownContent.testGetValue();
  is(events.length, 2, "Two more events.");
  Assert.deepEqual(
    events.map(e => e.extra.end_reason),
    ["shutdown-in-progress", "shutdown-in-progress"],
    "Hangs ended due to in-progress shutdown."
  );
  is(
    events.filter(e => e.extra.uri_type == "extension").length,
    1,
    "One more extension hang."
  );

  ProcessHangMonitor._shuttingDown = false;
});

/**
 * Test that if there happens to be no open browser windows, that any
 * hang reports that exist or appear while in this state will be handled
 * automatically.
 */
add_task(async function terminateNoWindows() {
  Services.fog.testResetFOG();
  let testWin = await BrowserTestUtils.openNewBrowserWindow();

  let pausedHang = new TestHangReport(
    SLOW_SCRIPT,
    testWin.gBrowser.selectedBrowser
  );
  Services.obs.notifyObservers(pausedHang, "process-hang-report");
  ProcessHangMonitor.waitLonger(testWin);
  ok(
    ProcessHangMonitor.findPausedReport(testWin.gBrowser.selectedBrowser),
    "There should be a paused report for the selected browser."
  );

  let scriptHang = new TestHangReport(SLOW_SCRIPT);
  let addonHang = new TestHangReport(ADDON_HANG);

  [scriptHang, addonHang].forEach(hangReport => {
    Services.obs.notifyObservers(hangReport, "process-hang-report");
  });

  // Quick and dirty hack to trick the window mediator into thinking there
  // are no browser windows without actually closing all browser windows.
  document.documentElement.setAttribute(
    "windowtype",
    "navigator:browsertestdummy"
  );

  // In case this test happens to throw before it can finish, make
  // sure to reset this.
  registerCleanupFunction(() => {
    document.documentElement.setAttribute("windowtype", "navigator:browser");
  });

  await BrowserTestUtils.closeWindow(testWin);

  let pausedAction = await pausedHang.promise;
  let scriptAction = await scriptHang.promise;
  let addonAction = await addonHang.promise;

  is(
    pausedAction,
    TEST_ACTION_TERMSCRIPT,
    "With no open windows, should have terminated script for paused script hang."
  );
  is(
    scriptAction,
    TEST_ACTION_TERMSCRIPT,
    "With no open windows, should have terminated script for script hang."
  );
  is(
    addonAction,
    TEST_ACTION_TERMSCRIPT,
    "With no open windows, should have terminated script for add-on hang."
  );

  let events = Glean.slowScriptWarning.shownContent.testGetValue();
  Services.fog.testResetFOG(); // Clear already-checked events.
  is(events.length, 3, "Recorded one event per hang.");
  is(
    events.filter(e => e.extra.end_reason == "no-windows-left").length,
    2,
    "Had two hangs ended because there were no windows left."
  );
  is(
    events.filter(e => e.extra.end_reason == "window-closed").length,
    1,
    "Had one hang ended because the window was closed."
  );

  // ProcessHangMonitor should notice we're in the "no windows" state,
  // so any further hangs should be handled immediately without user
  // interaction.
  let scriptHang2 = new TestHangReport(SLOW_SCRIPT);
  let addonHang2 = new TestHangReport(ADDON_HANG);

  [scriptHang2, addonHang2].forEach(hangReport => {
    Services.obs.notifyObservers(hangReport, "process-hang-report");
  });

  let scriptAction2 = await scriptHang.promise;
  let addonAction2 = await addonHang.promise;

  is(
    scriptAction2,
    TEST_ACTION_TERMSCRIPT,
    "With no open windows, should have terminated script for script hang."
  );
  is(
    addonAction2,
    TEST_ACTION_TERMSCRIPT,
    "With no open windows, should have terminated script for add-on hang."
  );

  events = Glean.slowScriptWarning.shownContent.testGetValue();
  is(events.length, 2, "Two more events.");
  Assert.deepEqual(
    events.map(e => e.extra.end_reason),
    ["no-windows-left", "no-windows-left"],
    "Hangs ended due to no windows left."
  );
  is(
    events.filter(e => e.extra.uri_type == "extension").length,
    1,
    "One more extension hang."
  );

  document.documentElement.setAttribute("windowtype", "navigator:browser");
});

/**
 * Test that if a script hang occurs in one browser window, and that
 * browser window goes away, that we clear the hang. For plug-in hangs,
 * we do the conservative thing and terminate any plug-in hangs when a
 * window closes, even though we don't exactly know which window it
 * belongs to.
 */
add_task(async function terminateClosedWindow() {
  Services.fog.testResetFOG();
  let testWin = await BrowserTestUtils.openNewBrowserWindow();
  let testBrowser = testWin.gBrowser.selectedBrowser;

  let pausedHang = new TestHangReport(SLOW_SCRIPT, testBrowser);
  Services.obs.notifyObservers(pausedHang, "process-hang-report");
  ProcessHangMonitor.waitLonger(testWin);
  ok(
    ProcessHangMonitor.findPausedReport(testWin.gBrowser.selectedBrowser),
    "There should be a paused report for the selected browser."
  );

  let scriptHang = new TestHangReport(SLOW_SCRIPT, testBrowser);
  let addonHang = new TestHangReport(ADDON_HANG, testBrowser);

  [scriptHang, addonHang].forEach(hangReport => {
    Services.obs.notifyObservers(hangReport, "process-hang-report");
  });

  await BrowserTestUtils.closeWindow(testWin);

  let pausedAction = await pausedHang.promise;
  let scriptAction = await scriptHang.promise;
  let addonAction = await addonHang.promise;

  is(
    pausedAction,
    TEST_ACTION_TERMSCRIPT,
    "When closing window, should have terminated script for a paused script hang."
  );
  is(
    scriptAction,
    TEST_ACTION_TERMSCRIPT,
    "When closing window, should have terminated script for script hang."
  );
  is(
    addonAction,
    TEST_ACTION_TERMSCRIPT,
    "When closing window, should have terminated script for add-on hang."
  );

  const events = Glean.slowScriptWarning.shownContent.testGetValue();
  is(events.length, 3, "Recorded one event per hang.");
  Assert.deepEqual(
    events.map(e => e.extra.end_reason),
    ["window-closed", "window-closed", "window-closed"],
    "Hangs ended due to windows closed."
  );
  is(
    events.filter(e => e.extra.uri_type == "extension").length,
    1,
    "Had one extension hang."
  );
  is(
    events.filter(e => e.extra.wait_count == "1").length,
    1,
    "Had one paused hang."
  );
});

/**
 * Test that permitUnload (used for closing or discarding tabs) does not
 * try to talk to the hung child
 */
add_task(async function permitUnload() {
  Services.fog.testResetFOG();
  let testWin = await BrowserTestUtils.openNewBrowserWindow();
  let testTab = testWin.gBrowser.selectedTab;

  // Ensure we don't close the window:
  BrowserTestUtils.addTab(testWin.gBrowser, "about:blank");

  // Set up the test tab and another tab so we can check what happens when
  // they are closed:
  let otherTab = BrowserTestUtils.addTab(testWin.gBrowser, "about:blank");
  let permitUnloadCount = 0;
  for (let tab of [testTab, otherTab]) {
    let browser = tab.linkedBrowser;
    // Fake before unload state:
    Object.defineProperty(browser, "hasBeforeUnload", { value: true });
    // Increment permitUnloadCount if we ask for unload permission:
    browser.asyncPermitUnload = () => {
      permitUnloadCount++;
      return Promise.resolve({ permitUnload: true });
    };
  }

  // Set up a hang for the selected tab:
  let testBrowser = testTab.linkedBrowser;
  let pausedHang = new TestHangReport(SLOW_SCRIPT, testBrowser);
  Services.obs.notifyObservers(pausedHang, "process-hang-report");
  ProcessHangMonitor.waitLonger(testWin);
  ok(
    ProcessHangMonitor.findPausedReport(testWin.gBrowser.selectedBrowser),
    "There should be a paused report for the browser we're about to remove."
  );

  BrowserTestUtils.removeTab(otherTab);
  BrowserTestUtils.removeTab(testWin.gBrowser.getTabForBrowser(testBrowser));
  is(
    permitUnloadCount,
    1,
    "Should have called asyncPermitUnload once (not for the hung tab)."
  );

  await BrowserTestUtils.closeWindow(testWin);

  const events = Glean.slowScriptWarning.shownContent.testGetValue();
  is(events.length, 1, "Recorded one event for the hang.");
  is(
    events[0].extra.end_reason,
    "window-closed",
    "The hang was dismissed on window close."
  );
});
