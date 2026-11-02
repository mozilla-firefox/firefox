/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Load anti-tracking test utilities
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/toolkit/components/antitracking/test/browser/head.js",
  this
);

const BLOCKED_STATE = "Blocked 🛑";
const NOT_BLOCKED_STATE = "Not Blocked";

/**
 * Creates and loads a tracker iframe in the current page.
 * Similar to the approach used in browser_siteSpecificWorkAroundsComplex.js
 */
async function loadTracker(
  browser,
  trackerUrl = "https://tracking.example.org"
) {
  const blockedPromise = waitForContentBlockingEvent(window).then(
    () => "blocked"
  );

  const loadPromise = SpecialPowers.spawn(
    browser,
    [trackerUrl],
    async function (url) {
      // Create a unique iframe ID to avoid conflicts
      const iframe = content.document.createElement("iframe");

      const iframeLoadPromise = ContentTaskUtils.waitForEvent(
        iframe,
        "load"
      ).then(() => "loaded");

      iframe.src = url;
      content.document.body.appendChild(iframe);
      return iframeLoadPromise;
    }
  );

  return Promise.race([loadPromise, blockedPromise]);
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.contentblocking.category", "strict"],
      ["privacy.trackingprotection.enabled", true],
      ["privacy.trackingprotection.allow_list.baseline.enabled", false],
      ["privacy.trackingprotection.allow_list.convenience.enabled", false],
      [
        "urlclassifier.trackingTable.testEntries",
        "tracking.example.org,social-tracking.example.org",
      ],
      [
        "urlclassifier.trackingAnnotationTable.testEntries",
        "tracking.example.org,social-tracking.example.org",
      ],
    ],
  });
  await UrlClassifierTestUtils.addTestTrackers();
  await UrlClassifierTestUtils.addTestHostsFromPrefs();
  registerCleanupFunction(async () => {
    await UrlClassifierTestUtils.cleanupTestTrackers();
  });
});

async function cleanup(toolbox) {
  await closeToolboxAndTab(toolbox);
}

/**
 * Test that a single blocked request is shown in the panel
 */
add_task(async function test_single_blocked_request_shown() {
  const tab = await addTab("https://example.com/");
  const toolbox = await openToolboxForTab(tab, "antitracking");
  const panel = toolbox.getCurrentPanel();
  const panelWindow = panel.panelWin;

  const webcompatDebugger = panelWindow.AntiTracking.debugger;

  const result = await loadTracker(tab.linkedBrowser);
  is(result, "blocked", "Tracker should be blocked");

  const hasTrackers = !!Object.keys(webcompatDebugger.allTrackers).length;

  ok(hasTrackers, "There should be at least one tracker in the debugger");

  const trackerRows = panelWindow.document.querySelectorAll(
    "#tracker-table tbody tr"
  );
  Assert.equal(
    trackerRows.length,
    1,
    "There should be one tracker shown in the panel"
  );

  // Check that the blocked tracker is displayed
  const blockedColumn = trackerRows[0].querySelector("td:nth-child(2)");
  is(
    blockedColumn.textContent,
    BLOCKED_STATE,
    "The tracker should be marked as blocked"
  );

  // Check hostname is displayed
  const hostnameColumn = trackerRows[0].querySelector("td:nth-child(3)");
  ok(
    hostnameColumn.textContent.includes("tracking.example.org"),
    "Tracker hostname should be displayed"
  );

  await cleanup(toolbox);
});

/**
 * Test that when no requests are blocked, nothing is shown in the panel
 */
add_task(async function test_no_blocked_requests_shown() {
  const tab = await addTab("https://example.com/");
  const toolbox = await openToolboxForTab(tab, "antitracking");
  const panel = toolbox.getCurrentPanel();
  const panelWindow = panel.panelWin;

  const table = panelWindow.document.getElementById("tracker-table");
  const trackerRows = table.querySelectorAll("tbody tr");

  is(
    trackerRows.length,
    0,
    "No trackers should be shown when none are blocked"
  );

  // Should show the "no content" message
  const noContentMessage = panelWindow.document.querySelector(
    ".no-content-message"
  );
  ok(
    noContentMessage &&
      noContentMessage.textContent.includes("No blocked resources"),
    "Should show no blocked resources message"
  );

  await cleanup(toolbox);
});

/**
 * Test that unblocking a tracker works correctly
 */
add_task(async function test_unblock_single_request() {
  const tab = await addTab("https://example.com/");
  const toolbox = await openToolboxForTab(tab, "antitracking");
  const panel = toolbox.getCurrentPanel();
  const panelWindow = panel.panelWin;
  const webcompatDebugger = panelWindow.AntiTracking.debugger;

  let result = await loadTracker(tab.linkedBrowser);
  is(result, "blocked", "Tracker should be blocked initially");

  const trackerRows = panelWindow.document.querySelectorAll(
    "#tracker-table tbody tr"
  );
  Assert.equal(
    trackerRows.length,
    1,
    "There should be one tracker shown in the panel"
  );

  const firstRow = trackerRows[0];

  const blockedColumn = firstRow.querySelector("td:nth-child(2)");
  is(
    blockedColumn.textContent,
    BLOCKED_STATE,
    "Tracker should initially be blocked"
  );

  const unblockButton = firstRow.querySelector("td:last-child button");
  is(unblockButton.textContent, "Unblock", "Button should say 'Unblock'");

  const browserLoadedPromise = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser
  );

  unblockButton.click();
  await browserLoadedPromise;

  const updatedTrackerRows = panelWindow.document.querySelectorAll(
    "#tracker-table tbody tr"
  );
  const updatedFirstRow = updatedTrackerRows[0];

  const updatedBlockedColumn = updatedFirstRow.querySelector("td:nth-child(2)");
  is(
    updatedBlockedColumn.textContent,
    NOT_BLOCKED_STATE,
    "Tracker should now be unblocked in UI"
  );

  const updatedButton = updatedFirstRow.querySelector("td:last-child button");
  is(updatedButton.textContent, "Block", "Button should now say 'Block'");

  ok(
    webcompatDebugger.unblockedChannels.has("tracking.example.org"),
    "Tracker should now be unblocked in debugger state"
  );

  await reloadSelectedTab();

  // Test that the tracker would now load
  result = await loadTracker(tab.linkedBrowser);
  is(result, "loaded", "Tracker should load successfully after unblocking");

  await cleanup(toolbox);
});

/**
 * Test that clicking the "block" button for a request blocks it
 */
add_task(async function test_block_unblocked_channel() {
  const tab = await addTab("https://example.com/");
  const toolbox = await openToolboxForTab(tab, "antitracking");
  const panel = toolbox.getCurrentPanel();
  const panelWindow = panel.panelWin;
  const webcompatDebugger = panelWindow.AntiTracking.debugger;

  // Set up the debugger state to simulate an unblocked tracker
  webcompatDebugger.unblockedChannels = new Set(["tracking.example.org"]);
  webcompatDebugger.allTrackers = {
    "tracking.example.org": "Tracking Protection",
  };

  webcompatDebugger.populateTrackerTable();

  const firstRow = panelWindow.document.querySelectorAll(
    "#tracker-table tbody tr"
  )[0];

  const blockButton = firstRow.querySelector("td:last-child button");
  ok(blockButton, "Block button should be present");
  is(blockButton.textContent, "Block", "Button should say 'Block'");

  const browserLoadedPromise = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser
  );
  blockButton.click();
  await browserLoadedPromise;

  const updatedTrackerRows = panelWindow.document.querySelectorAll(
    "#tracker-table tbody tr"
  );
  const updatedFirstRow = updatedTrackerRows[0];

  const updatedBlockedColumn = updatedFirstRow.querySelector("td:nth-child(2)");
  is(
    updatedBlockedColumn.textContent,
    BLOCKED_STATE,
    "Tracker should now be blocked in UI"
  );

  const updatedButton = updatedFirstRow.querySelector("td:last-child button");
  is(updatedButton.textContent, "Unblock", "Button should now say 'Unblock'");

  ok(
    !webcompatDebugger.unblockedChannels.has("tracking.example.org"),
    "Tracker should now be blocked (removed from unblockedChannels)"
  );

  const result = await loadTracker(tab.linkedBrowser);
  is(
    result,
    "blocked",
    "Tracker should be blocked after clicking block button"
  );

  await cleanup(toolbox);
});

/**
 * Test when there are multiple trackers and one is unblocked, the others are still blocked
 */
add_task(async function test_multiple_trackers_one_unblocked() {
  const tab = await addTab("https://example.com/");
  const toolbox = await openToolboxForTab(tab, "antitracking");
  const panel = toolbox.getCurrentPanel();
  const panelWindow = panel.panelWin;

  // Load multiple trackers
  let result = await loadTracker(
    tab.linkedBrowser,
    "https://tracking.example.org"
  );
  is(result, "blocked", "First tracker should be blocked");

  result = await loadTracker(
    tab.linkedBrowser,
    "https://social-tracking.example.org"
  );
  is(result, "blocked", "Second tracker should also be blocked");

  const trackerRows = panelWindow.document.querySelectorAll(
    "#tracker-table tbody tr"
  );
  Assert.equal(
    trackerRows.length,
    2,
    "Two trackers should be shown in the panel"
  );

  const firstRow = trackerRows[0];
  const unblockButton = firstRow.querySelector("td:last-child button");

  const browserLoadedPromise = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser
  );
  unblockButton.click();

  await browserLoadedPromise;

  const updatedTrackerRows = panelWindow.document.querySelectorAll(
    "#tracker-table tbody tr"
  );
  const updatedFirstRow = updatedTrackerRows[0];

  const updatedBlockedColumn = updatedFirstRow.querySelector("td:nth-child(2)");
  is(
    updatedBlockedColumn.textContent,
    NOT_BLOCKED_STATE,
    "First tracker should now be unblocked in UI"
  );

  result = await loadTracker(tab.linkedBrowser, "https://tracking.example.org");
  is(result, "loaded", "First tracker should be loaded");

  await cleanup(toolbox);
});

/**
 * Test the "unblock selected" button functionality with multiple selected trackers
 */
add_task(async function test_unblock_selected_button() {
  const tab = await addTab("https://example.com/");
  const toolbox = await openToolboxForTab(tab, "antitracking");
  const panel = toolbox.getCurrentPanel();
  const panelWindow = panel.panelWin;
  const webcompatDebugger = panelWindow.AntiTracking.debugger;

  // Load two trackers
  let result = await loadTracker(
    tab.linkedBrowser,
    "https://tracking.example.org"
  );
  is(result, "blocked", "First tracker should be blocked");

  result = await loadTracker(
    tab.linkedBrowser,
    "https://social-tracking.example.org"
  );
  is(result, "blocked", "Second tracker should also be blocked");

  const rowCheckboxes = panelWindow.document.querySelectorAll(
    "#tracker-table tbody .row-checkbox"
  );
  is(rowCheckboxes.length, 2, "Two row checkboxes should be present");

  rowCheckboxes[0].click();
  rowCheckboxes[1].click();

  const unblockSelectedButton =
    panelWindow.document.getElementById("unblock-selected");
  ok(unblockSelectedButton, "Unblock selected button should be present");

  const browserLoadedPromise = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser
  );

  unblockSelectedButton.click();

  await browserLoadedPromise;

  // Verify both trackers are now unblocked in the debugger state
  ok(
    webcompatDebugger.unblockedChannels.has("tracking.example.org"),
    "First tracker should be unblocked in debugger state"
  );
  ok(
    webcompatDebugger.unblockedChannels.has("social-tracking.example.org"),
    "Second tracker should be unblocked in debugger state"
  );

  // Verify UI shows both trackers as unblocked
  const updatedTrackerRows = panelWindow.document.querySelectorAll(
    "#tracker-table tbody tr"
  );
  // Verify buttons changed to "Block"
  const firstRowButton = updatedTrackerRows[0].querySelector(
    "td:last-child button"
  );
  const secondRowButton = updatedTrackerRows[1].querySelector(
    "td:last-child button"
  );

  is(
    firstRowButton.textContent,
    "Block",
    "First tracker button should say 'Block'"
  );
  is(
    secondRowButton.textContent,
    "Block",
    "Second tracker button should say 'Block'"
  );

  // Test that both trackers now load successfully
  await reloadSelectedTab();

  result = await loadTracker(tab.linkedBrowser, "https://tracking.example.org");
  is(result, "loaded", "First tracker should load after bulk unblock");

  result = await loadTracker(
    tab.linkedBrowser,
    "https://social-tracking.example.org"
  );
  is(result, "loaded", "Second tracker should load after bulk unblock");

  await cleanup(toolbox);
});
