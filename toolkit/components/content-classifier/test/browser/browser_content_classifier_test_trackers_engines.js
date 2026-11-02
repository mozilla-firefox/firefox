"use strict";

// privacy.trackingprotection.content.testing.use_test_trackers_engines makes
// the service use test_block and test_annotate wherever the engines prefs list
// the trackers engine, so blocking and annotation lists can differ.

const USE_TEST_ENGINES_PREF =
  "privacy.trackingprotection.content.testing.use_test_trackers_engines";
const BLOCKED_TRACKER = "https://example.org/image.jpg";
const ANNOTATED_TRACKER = "https://example.com/image.jpg";

let service;

function probeRequest(url) {
  return {
    url,
    sourceUrl: TEST_DOMAIN,
    topWindowUrl: TEST_DOMAIN,
    requestType: "image",
    privateBrowsing: false,
    forceThirdPartyToTop: false,
    isNonRecommendedAddon: false,
  };
}

async function matchedFeatures(probe, url) {
  let report = await probe.call(service, probeRequest(url));
  return Array.from(report.results)
    .filter(r => r.matched && !r.exception)
    .map(r => r.featureName);
}

async function engineNames(probe) {
  let report = await probe.call(service, probeRequest(BLOCKED_TRACKER));
  return Array.from(report.results).map(r => r.featureName);
}

add_task(async function test_trackers_replaced_by_test_engines() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.contentblocking.category", "custom"],
      ["privacy.trackingprotection.allow_list.hasMigratedCategoryPrefs", true],
      ["privacy.trackingprotection.content.mirror.mode", 1],
      ["privacy.trackingprotection.content.testing", true],
      ["privacy.trackingprotection.enabled", true],
      ["privacy.trackingprotection.annotate_channels", true],
      [
        "privacy.trackingprotection.content.protection.test_list_urls",
        "data:text/plain," + encodeURIComponent("||example.org^\n"),
      ],
      [
        "privacy.trackingprotection.content.annotation.test_list_urls",
        "data:text/plain," +
          encodeURIComponent("||example.org^\n||example.com^\n"),
      ],
      [USE_TEST_ENGINES_PREF, true],
    ],
  });
  service = Cc["@mozilla.org/content-classifier-service;1"].getService(
    Ci.nsIContentClassifierService
  );

  await TestUtils.waitForCondition(
    async () =>
      (await matchedFeatures(service.probeBlocking, BLOCKED_TRACKER)).includes(
        "test_block"
      ) &&
      (
        await matchedFeatures(service.probeAnnotate, ANNOTATED_TRACKER)
      ).includes("test_annotate"),
    "Waiting for the test engines to be built"
  );

  ok(
    !(await engineNames(service.probeBlocking)).includes("trackers"),
    "The trackers engine is not used for blocking"
  );
  ok(
    !(await engineNames(service.probeAnnotate)).includes("trackers"),
    "The trackers engine is not used for annotation"
  );
  ok(
    !(await matchedFeatures(service.probeBlocking, ANNOTATED_TRACKER)).includes(
      "test_block"
    ),
    "A host only on the annotation list is not blocked"
  );

  let tab = await openTestTab();
  let bc = tab.linkedBrowser.browsingContext;
  ok(
    bc.scopedPrefs.getBoolPrefScoped(
      Ci.nsIScopedPrefs.PRIVACY_TRACKINGPROTECTION_CONTENT_ENABLED,
      bc
    ),
    "The trackers scoped pref defaults to test_block being active"
  );
  BrowserTestUtils.removeTab(tab);

  await SpecialPowers.pushPrefEnv({
    set: [["privacy.trackingprotection.enabled", false]],
  });
  await TestUtils.waitForCondition(
    async () =>
      !(await engineNames(service.probeBlocking)).includes("test_block"),
    "test_block follows the trackers engine out of the blocking engines"
  );
  ok(
    (await matchedFeatures(service.probeAnnotate, ANNOTATED_TRACKER)).includes(
      "test_annotate"
    ),
    "test_annotate is still used for annotation"
  );
  await SpecialPowers.popPrefEnv();

  await SpecialPowers.pushPrefEnv({ set: [[USE_TEST_ENGINES_PREF, false]] });
  await TestUtils.waitForCondition(
    async () =>
      !(await engineNames(service.probeBlocking)).includes("test_block") &&
      !(await engineNames(service.probeAnnotate)).includes("test_annotate"),
    "The test engines are dropped once the pref is cleared"
  );
  ok(true, "The test engines are no longer used");
});
