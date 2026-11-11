/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/*
 * This test ensures we are correctly submitting the custom ping for SERP
 * categorization. (Please see the search component's Marionette tests for
 * a test of the ping's submission upon startup.)
 */

// Can fail in TV mode.
requestLongerTimeout(2);

ChromeUtils.defineESModuleGetters(this, {
  CATEGORIZATION_SETTINGS:
    "moz-src:///browser/components/search/SERPCategorization.sys.mjs",
  DomainToCategoriesStore:
    "moz-src:///browser/components/search/SERPCategorization.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  SearchSERPTelemetry:
    "moz-src:///browser/components/search/SearchSERPTelemetry.sys.mjs",
  SERPCategorizationRecorder:
    "moz-src:///browser/components/search/SERPCategorization.sys.mjs",
  TELEMETRY_CATEGORIZATION_KEY:
    "moz-src:///browser/components/search/SERPCategorization.sys.mjs",
});

const TEST_PROVIDER_INFO = [
  {
    telemetryId: "example",
    searchPageRegexp:
      /^https:\/\/example.org\/browser\/browser\/components\/search\/test\/browser\/telemetry\/searchTelemetry/,
    queryParamNames: ["s"],
    codeParamName: "abc",
    taggedCodes: ["ff"],
    adServerAttributes: ["mozAttr"],
    nonAdsLinkRegexps: [/^https:\/\/example.com/],
    extraAdServersRegexps: [/^https:\/\/example\.com\/ad/],
    // The search telemetry entry responsible for targeting the specific results.
    domainExtraction: {
      ads: [
        {
          selectors: "[data-ad-domain]",
          method: "data-attribute",
          options: {
            dataAttributeKey: "adDomain",
          },
        },
        {
          selectors: ".ad",
          method: "href",
          options: {
            queryParamKey: "ad_domain",
          },
        },
      ],
      nonAds: [
        {
          selectors: "#results .organic a",
          method: "href",
        },
      ],
    },
    components: [
      {
        type: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
        default: true,
      },
    ],
  },
];

const client = RemoteSettings(TELEMETRY_CATEGORIZATION_KEY);
const db = client.db;

add_setup(async function () {
  SearchSERPTelemetry.overrideSearchTelemetryForTests(TEST_PROVIDER_INFO);
  await waitForIdle();
  // Enable local telemetry recording for the duration of the tests.
  let oldCanRecord = Services.telemetry.canRecordExtended;
  Services.telemetry.canRecordExtended = true;
  await db.clear();

  let promise = waitForDomainToCategoriesUpdate();
  await insertRecordIntoCollectionAndSync();
  await SpecialPowers.pushPrefEnv({
    set: [["telemetry.fog.test.activity_limit", 120]],
  });
  await promise;

  registerCleanupFunction(async () => {
    await SpecialPowers.popPrefEnv();
    SearchSERPTelemetry.overrideSearchTelemetryForTests();
    Services.telemetry.canRecordExtended = oldCanRecord;
    resetTelemetry();
  });
});

add_task(async function test_threshold_reached() {
  resetTelemetry();

  let oldThreshold = CATEGORIZATION_SETTINGS.PING_SUBMISSION_THRESHOLD;
  // For testing, it's fine to categorize fewer SERPs before sending the ping.
  CATEGORIZATION_SETTINGS.PING_SUBMISSION_THRESHOLD = 2;
  SERPCategorizationRecorder.uninit();
  SERPCategorizationRecorder.init();

  Assert.equal(
    null,
    Glean.serp.categorization.testGetValue(),
    "Should not have recorded any metrics yet."
  );

  let submitted = false;
  GleanPings.serpCategorization.testBeforeNextSubmit(reason => {
    submitted = true;
    Assert.equal(
      "threshold_reached",
      reason,
      "Ping submission reason should be 'threshold_reached'."
    );
  });

  // Categorize first SERP, which results in one organic and one sponsored
  // reporting.
  let url = getSERPUrl("searchTelemetryDomainCategorizationReporting.html");
  info("Load a sample SERP with organic and sponsored results.");
  let promise = waitForPageWithCategorizedDomains();
  let tab1 = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  await promise;

  Assert.equal(
    false,
    submitted,
    "Ping should not be submitted before threshold is reached."
  );

  // Categorize second SERP, which results in one organic and one sponsored
  // reporting.
  url = getSERPUrl("searchTelemetryDomainExtraction.html");
  info("Load a sample SERP with organic and sponsored results.");
  promise = waitForPageWithCategorizedDomains();
  let tab2 = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  await promise;

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);

  Assert.equal(
    true,
    submitted,
    "Ping should be submitted once threshold is reached."
  );

  CATEGORIZATION_SETTINGS.PING_SUBMISSION_THRESHOLD = oldThreshold;
});

add_task(async function test_quick_activity_to_inactivity_alternation() {
  resetTelemetry();

  Assert.equal(
    null,
    Glean.serp.categorization.testGetValue(),
    "Should not have recorded any metrics yet."
  );

  let submitted = false;
  GleanPings.serpCategorization.testBeforeNextSubmit(() => {
    submitted = true;
  });

  let url = getSERPUrl("searchTelemetryDomainCategorizationReporting.html");
  info("Load a sample SERP with organic and sponsored results.");
  let promise = waitForPageWithCategorizedDomains();
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  await promise;

  BrowserTestUtils.removeTab(tab);

  let activityDetectedPromise = TestUtils.topicObserved(
    "user-interaction-active"
  );
  // Simulate ~2.5 seconds of activity.
  for (let i = 0; i < 25; i++) {
    EventUtils.synthesizeKey("KEY_Enter");
    await sleep(100);
  }
  await activityDetectedPromise;

  let inactivityDetectedPromise = TestUtils.topicObserved(
    "user-interaction-inactive"
  );
  await inactivityDetectedPromise;

  Assert.equal(
    false,
    submitted,
    "Ping should not be submitted after a quick alternation from activity to inactivity."
  );
});

add_task(async function test_submit_after_activity_then_inactivity() {
  resetTelemetry();

  let oldActivityLimit = Services.prefs.getIntPref(
    "telemetry.fog.test.activity_limit"
  );
  Services.prefs.setIntPref("telemetry.fog.test.activity_limit", 2);

  Assert.equal(
    null,
    Glean.serp.categorization.testGetValue(),
    "Should not have recorded any metrics yet."
  );

  let url = getSERPUrl("searchTelemetryDomainCategorizationReporting.html");
  info("Load a sample SERP with organic and sponsored results.");
  let promise = waitForPageWithCategorizedDomains();
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  await promise;

  let submitted = false;
  GleanPings.serpCategorization.testBeforeNextSubmit(reason => {
    submitted = true;
    Assert.equal(
      "inactivity",
      reason,
      "Ping submission reason should be 'inactivity'."
    );
  });

  BrowserTestUtils.removeTab(tab);

  let activityDetectedPromise = TestUtils.topicObserved(
    "user-interaction-active"
  );
  // Simulate ~2.5 seconds of activity.
  for (let i = 0; i < 25; i++) {
    EventUtils.synthesizeKey("KEY_Enter");
    await sleep(100);
  }
  await activityDetectedPromise;

  let inactivityDetectedPromise = TestUtils.topicObserved(
    "user-interaction-inactive"
  );
  await inactivityDetectedPromise;

  Assert.equal(
    true,
    submitted,
    "Ping should be submitted after 2+ seconds of activity, followed by inactivity."
  );

  Services.prefs.setIntPref(
    "telemetry.fog.test.activity_limit",
    oldActivityLimit
  );
});

add_task(async function test_no_observers_added_if_pref_is_off() {
  resetTelemetry();

  let prefOnActiveObserverCount = Array.from(
    Services.obs.enumerateObservers("user-interaction-active")
  ).length;
  let prefOnInactiveObserverCount = Array.from(
    Services.obs.enumerateObservers("user-interaction-inactive")
  ).length;

  await SpecialPowers.pushPrefEnv({
    set: [["browser.search.serpEventTelemetryCategorization.enabled", false]],
  });
  await waitForDomainToCategoriesUninit();

  let prefOffActiveObserverCount = Array.from(
    Services.obs.enumerateObservers("user-interaction-active")
  ).length;
  let prefOffInactiveObserverCount = Array.from(
    Services.obs.enumerateObservers("user-interaction-inactive")
  ).length;

  Assert.equal(
    prefOnActiveObserverCount - prefOffActiveObserverCount,
    1,
    "There should be one fewer active observer when the pref is off."
  );
  Assert.equal(
    prefOnInactiveObserverCount - prefOffInactiveObserverCount,
    1,
    "There should be one fewer inactive observer when the pref is off."
  );

  await SpecialPowers.popPrefEnv();
  await waitForDomainToCategoriesInit();
});

add_task(async function test_count_incremented_if_store_is_not_created() {
  resetTelemetry();

  // Clear the existing domain-to-categories map.
  await SERPDomainToCategoriesMap.uninit({ deleteMap: true });

  let sandbox = sinon.createSandbox();
  sandbox
    .stub(DomainToCategoriesStore.prototype, "insertFileContents")
    .throws(new Error());
  // Initializing should fail and cause the component to un-initialize.
  let promise = waitForDomainToCategoriesUninit();
  await SERPDomainToCategoriesMap.init();
  await promise;
  info("Store for the domain-to-categories map not created successfully.");

  let url = getSERPUrl("searchTelemetryDomainCategorizationReporting.html");
  info("Load a SERP with organic and sponsored results.");
  promise = waitForPageWithCategorizedDomains();
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  await promise;

  Assert.equal(
    Glean.serp.categorizationNoMapFound.testGetValue(),
    1,
    "Counter should be incremented when there is an issue creating the store for the domain-to-categories map."
  );

  sandbox.restore();
  await SERPDomainToCategoriesMap.init();
  await BrowserTestUtils.removeTab(tab);
});
