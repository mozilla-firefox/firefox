/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Name of the RemoteSettings collection containing exceptions.
const COLLECTION_NAME = "url-classifier-exceptions";

/**
 * Load a tracker in third-party context.
 *
 * @param {string} trackerUrl - The URL of the tracker to load in an iframe.
 * @returns {Promise} A promise that resolves when the tracker is loaded or
 * blocked. The promise resolves to "loaded" if the tracker is loaded, or
 * "blocked" if it is blocked.
 */
async function loadTracker({ trackerUrl }) {
  let win = window;
  const topLevelURL = "https://example.com/";
  let tab = win.gBrowser.addTab(topLevelURL, {
    triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
  });
  win.gBrowser.selectedTab = tab;
  let browser = tab.linkedBrowser;

  await BrowserTestUtils.browserLoaded(browser, false, topLevelURL);

  info(`Create an iframe loading ${trackerUrl}`);

  let blockedPromise = waitForContentBlockingEvent(win).then(() => "blocked");

  let loadPromise = SpecialPowers.spawn(
    browser,
    [trackerUrl],
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
  await BrowserTestUtils.removeTab(tab);

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
      [
        "urlclassifier.trackingTable.testEntries",
        "tracking.example.org,email-tracking.example.org",
      ],
      // Clear predefined exceptions.
      ["urlclassifier.trackingAnnotationSkipURLs", ""],
      ["urlclassifier.trackingSkipURLs", ""],
      ["browser.contentblocking.category", "strict"],
    ],
  });

  // Start with an empty RS collection.
  info(`Initializing RemoteSettings collection "${COLLECTION_NAME}".`);
  const db = RemoteSettings(COLLECTION_NAME).db;
  await db.importChanges({}, Date.now(), [], { clear: true });
  await setExceptions(
    [
      {
        urlPattern: "*://email-tracking.example.org/*",
        classifierFeatures: ["tracking-protection"],
        category: "baseline",
      },
      {
        urlPattern: "*://tracking.example.org/*",
        classifierFeatures: ["tracking-protection"],
        category: "convenience",
      },
    ],
    db,
    COLLECTION_NAME
  );

  registerCleanupFunction(async () => {
    info("Cleanup RemoteSettings collection and preferences");
    await setExceptions([], db, COLLECTION_NAME);
    await SpecialPowers.flushPrefEnv();
  });
});

/**
 * Set exceptions via RemoteSettings.
 *
 * @param {boolean} baseline - If true, set baseline allow list entries.
 * @param {boolean} convenience - If true, set convenience allow list entries.
 * @param {string} category - The content blocking category to set.
 */
async function setAllowListPrefs(baseline, convenience, category) {
  info("Set allow list baseline and convenience prefs");
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.trackingprotection.allow_list.baseline.enabled", baseline],
      [
        "privacy.trackingprotection.allow_list.convenience.enabled",
        convenience,
      ],
      ["browser.contentblocking.category", category],
    ],
  });
  is(
    Services.prefs.getBoolPref(
      "privacy.trackingprotection.allow_list.baseline.enabled"
    ),
    baseline,
    "Baseline allow list preference should be set correctly."
  );
  is(
    Services.prefs.getBoolPref(
      "privacy.trackingprotection.allow_list.convenience.enabled"
    ),
    convenience,
    "Convenience allow list preference should be set correctly."
  );
  is(
    Services.prefs.getCharPref("browser.contentblocking.category"),
    category,
    "Content blocking category preference should be set correctly."
  );
}

add_task(async function test_allow_list_both_enabled() {
  info("Set baseline to true and convenience to true.");
  await setAllowListPrefs(true, true, "standard");

  let success = await loadTracker({
    trackerUrl: "https://email-tracking.example.org/",
  });
  is(
    success,
    true,
    "Tracker access should be allowed because baseline allow list is enabled."
  );

  success = await loadTracker({
    trackerUrl: "https://tracking.example.org/",
  });
  is(
    success,
    true,
    "Tracker access should be allowed because convenience allow list is enabled."
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_allow_list_baseline_only() {
  info("Set baseline to true and convenience to false.");
  await setAllowListPrefs(true, false, "strict");

  let success = await loadTracker({
    trackerUrl: "https://email-tracking.example.org/",
  });
  is(
    success,
    true,
    "Tracker access should be allowed because baseline allow list is enabled."
  );

  success = await loadTracker({
    trackerUrl: "https://tracking.example.org/",
  });
  is(
    success,
    false,
    "Tracker access should be blocked because convenience allow list is disabled."
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_allow_list_none_enabled() {
  info("Set baseline to false and convenience to false.");
  await setAllowListPrefs(false, false, "custom");

  let success = await loadTracker({
    trackerUrl: "https://email-tracking.example.org/",
  });
  is(
    success,
    false,
    "Tracker access should be blocked because baseline allow lists is disabled."
  );

  success = await loadTracker({
    trackerUrl: "https://tracking.example.org/",
  });
  is(
    success,
    false,
    "Tracker access should be blocked because convenience allow list is disabled."
  );
  await SpecialPowers.popPrefEnv();
});
