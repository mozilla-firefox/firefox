/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Name of the RemoteSettings collection containing exceptions.
const COLLECTION_NAME = "url-classifier-exceptions";

// RemoteSettings collection db.
let db;

/**
 * Load a tracker in third-party context.
 *
 * @param {string} topLevelURL - The URL of the top level page to load.
 * @param {boolean} usePrivateWindow - Whether to use a private window.
 * @returns {Promise} A promise that resolves when the tracker is loaded or
 * blocked. The promise resolves to "loaded" if the tracker is loaded, or
 * "blocked" if it is blocked.
 */
async function loadTracker({ topLevelURL, usePrivateWindow }) {
  let win = window;

  if (usePrivateWindow) {
    win = await BrowserTestUtils.openNewBrowserWindow({ private: true });
  }

  let tab = win.gBrowser.addTab(topLevelURL, {
    triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
  });
  win.gBrowser.selectedTab = tab;
  let browser = tab.linkedBrowser;

  await BrowserTestUtils.browserLoaded(browser, false, topLevelURL);

  info("Create an iframe loading tracking.example.org.");

  let blockedPromise = waitForContentBlockingEvent(win).then(() => "blocked");

  let loadPromise = SpecialPowers.spawn(
    browser,
    ["https://tracking.example.org"],
    async function (url) {
      let iframe = content.document.createElement("iframe");
      let loadPromise = ContentTaskUtils.waitForEvent(iframe, "load").then(
        () => "loaded"
      );
      iframe.src = url;
      content.document.body.appendChild(iframe);
      return loadPromise;
    }
  );

  let result = await Promise.race([loadPromise, blockedPromise]);

  // Clean up.
  if (usePrivateWindow) {
    await BrowserTestUtils.closeWindow(win);
  } else {
    await BrowserTestUtils.removeTab(tab);
  }

  return result == "loaded";
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      // The url-classifier-exceptions allow list is url-classifier only, so
      // keep it as the channel classifier where the content classifier is
      // the default engine.
      ["privacy.trackingprotection.content.mirror.mode", 0],
      [
        "privacy.trackingprotection.urlclassifier.disable_for_channel_classifier",
        false,
      ],
      // Add test tracker.
      ["urlclassifier.trackingTable.testEntries", "tracking.example.org"],
      [
        "urlclassifier.trackingAnnotationTable.testEntries",
        "tracking.example.org",
      ],
      // Clear predefined exceptions.
      ["urlclassifier.trackingAnnotationSkipURLs", ""],
      ["urlclassifier.trackingSkipURLs", ""],
      // Make sure we're in "standard" mode.
      ["browser.contentblocking.category", "standard"],
      // Enable both allow-list categories.
      ["privacy.trackingprotection.allow_list.baseline.enabled", true],
      ["privacy.trackingprotection.allow_list.convenience.enabled", true],
    ],
  });

  // Start with an empty RS collection.
  info(`Initializing RemoteSettings collection "${COLLECTION_NAME}".`);
  db = RemoteSettings(COLLECTION_NAME).db;
  await db.importChanges({}, Date.now(), [], { clear: true });
});

// Tests that exception list entries can be scoped to only private browsing.
add_task(async function test_private_browsing_exception() {
  info("Load tracker in normal browsing.");
  let success = await loadTracker({
    topLevelURL: "https://example.com/",
    usePrivateWindow: false,
  });
  is(
    success,
    true,
    "Tracker access should be allowed in normal browsing, because TP is disabled."
  );

  info("Load tracker in private browsing.");
  success = await loadTracker({
    topLevelURL: "https://example.com/",
    usePrivateWindow: true,
  });
  is(
    success,
    false,
    "Tracker access should be blocked in private browsing, because TP is enabled and there is no exception set."
  );

  info("Set exception for private browsing.");
  await setExceptions(
    [
      {
        category: "baseline",
        urlPattern: "*://tracking.example.org/*",
        topLevelUrlPattern: "*://example.com/*",
        classifierFeatures: ["tracking-protection"],
        isPrivateBrowsingOnly: true,
        filterContentBlockingCategories: ["standard"],
      },
    ],
    db,
    COLLECTION_NAME
  );

  info("Load tracker in normal browsing.");
  success = await loadTracker({
    topLevelURL: "https://example.com/",
    usePrivateWindow: false,
  });
  is(
    success,
    true,
    "Tracker access should be allowed in normal browsing, because TP is disabled."
  );

  info("Load tracker in private browsing.");
  success = await loadTracker({
    topLevelURL: "https://example.com/",
    usePrivateWindow: true,
  });
  is(
    success,
    true,
    "Tracker access should be allowed in private browsing even though TP is enabled, because it's allow-listed."
  );

  info("Switch to ETP 'strict' mode.");
  await SpecialPowers.pushPrefEnv({
    set: [["browser.contentblocking.category", "strict"]],
  });

  info("Load tracker in normal browsing.");
  success = await loadTracker({
    topLevelURL: "https://example.com/",
    usePrivateWindow: false,
  });
  is(
    success,
    false,
    "Tracker access should be blocked in normal browsing in ETP 'strict' mode."
  );

  info("Load tracker in private browsing.");
  success = await loadTracker({
    topLevelURL: "https://example.com/",
    usePrivateWindow: true,
  });
  is(
    success,
    false,
    "Tracker access should be blocked in private browsing in ETP 'strict' mode."
  );

  info("Switch back to ETP 'standard' mode.");
  await SpecialPowers.popPrefEnv();

  info("Test with different top level site.");
  success = await loadTracker({
    topLevelURL: "https://example.org/",
    usePrivateWindow: false,
  });
  is(
    success,
    true,
    "Tracker access should be allowed in normal browsing, because TP is disabled."
  );

  info("Load tracker in private browsing.");
  success = await loadTracker({
    topLevelURL: "https://example.org/",
    usePrivateWindow: true,
  });
  is(
    success,
    true,
    "Tracker access should be blocked in private browsing, because TP is enabled and there is no exception set for top the top level URL https://example.org."
  );

  info("Update exception, removing the additional filtering criteria.");
  await setExceptions(
    [
      {
        category: "baseline",
        urlPattern: "*://tracking.example.org/*",
        classifierFeatures: ["tracking-protection"],
        filterContentBlockingCategories: ["standard", "strict"],
      },
    ],
    db,
    COLLECTION_NAME
  );

  info("Load tracker in private browsing.");
  success = await loadTracker({
    topLevelURL: "https://example.org/",
    usePrivateWindow: true,
  });
  is(
    success,
    true,
    "Tracker access should be allowed in private browsing, because TP is enabled and there is an exception set."
  );

  info("Switch to ETP 'strict' mode.");
  await SpecialPowers.pushPrefEnv({
    set: [["browser.contentblocking.category", "strict"]],
  });

  info("Load tracker in normal browsing.");
  success = await loadTracker({
    topLevelURL: "https://example.com/",
    usePrivateWindow: false,
  });
  is(
    success,
    true,
    "Tracker access should be allowed in normal browsing in ETP 'strict' mode, because there is an exception set."
  );

  info("Cleanup");
  await SpecialPowers.popPrefEnv();
  await setExceptions([], db, COLLECTION_NAME);
});
