"use strict";

// Bug 2049982: privacy.trackingprotection.urlclassifier.disable_for_channel_classifier
// disables the URLClassifier ETP engine when the ContentClassifier engine is
// active, and keeps URLClassifier as a fail-safe when ContentClassifier is not
// running.

const SKIP_PREF =
  "privacy.trackingprotection.urlclassifier.disable_for_channel_classifier";
const TRACKER_DOMAIN_FOR_URLCLASSIFIER = "https://tracking.example.com/";

add_setup(async function () {
  // Only install url-classifier's tables. The tasks drive the content
  // classifier's test_block engine themselves.
  await SpecialPowers.pushPrefEnv({ set: [[SKIP_PREF, false]] });
  await UrlClassifierTestUtils.addTestTrackers();
  registerCleanupFunction(async () => {
    await UrlClassifierTestUtils.cleanupTestTrackers();
  });
});

// Skip pref OFF -> URLClassifier blocks its tracker.
add_task(async function test_baseline_urlclassifier_blocks() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.trackingprotection.enabled", true],
      [SKIP_PREF, false],
    ],
  });

  let tab = await openTestTab();
  let browser = tab.linkedBrowser;

  await assertImageBlocked(
    browser,
    TRACKER_DOMAIN_FOR_URLCLASSIFIER,
    "With the skip pref off, URLClassifier should block tracking.example.com"
  );

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

// Skip pref ON + ContentClassifier active.
// ContentClassifier blocks its own tracker (example.org); URLClassifier is
// skipped, so its tracker (trackertest.org) loads.
add_task(async function test_skip_with_contentclassifier_active() {
  let listsLoaded = TestUtils.topicObserved(LISTS_LOADED_TOPIC);
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.trackingprotection.enabled", true],
      [SKIP_PREF, true],
      ["privacy.trackingprotection.content.testing", true],
      ["privacy.trackingprotection.content.protection.enabled", true],
      [
        "privacy.trackingprotection.content.protection.test_list_urls",
        BLOCK_LIST_URL,
      ],
      ["privacy.trackingprotection.content.protection.engines", "test_block"],
      ["privacy.trackingprotection.content.annotation.enabled", false],
      ["privacy.trackingprotection.content.annotation.test_list_urls", ""],
    ],
  });

  let tab = await openTestTab();
  let browser = tab.linkedBrowser;
  await listsLoaded;

  await assertImageBlocked(
    browser,
    TEST_BLOCKED_3RD_PARTY_DOMAIN,
    "ContentClassifier should still block example.org"
  );
  await assertImageLoaded(
    browser,
    TRACKER_DOMAIN_FOR_URLCLASSIFIER,
    "URLClassifier is skipped, so tracking.example.com should load"
  );

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

// Skip pref ON but ContentClassifier NOT active
// URLClassifier still blocks its tracker.
add_task(async function test_failsafe_without_contentclassifier() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.trackingprotection.enabled", true],
      [SKIP_PREF, true],
      ["privacy.trackingprotection.content.protection.enabled", false],
      ["privacy.trackingprotection.content.protection.engines", ""],
      ["privacy.trackingprotection.content.annotation.enabled", false],
      ["privacy.trackingprotection.content.annotation.engines", ""],
    ],
  });

  let tab = await openTestTab();
  let browser = tab.linkedBrowser;

  await assertImageBlocked(
    browser,
    TRACKER_DOMAIN_FOR_URLCLASSIFIER,
    "With ContentClassifier inactive, the URLClassifier fail-safe should block tracking.example.com"
  );

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});
