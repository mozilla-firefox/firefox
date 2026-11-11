/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/*
 * This test verifies that, when information as to whether a client is enrolled
 * in a specific experiment has been requested via Nimbus config, we correctly
 * plug that info into the custom SERP categorization ping.
 */

ChromeUtils.defineESModuleGetters(this, {
  CATEGORIZATION_SETTINGS:
    "moz-src:///browser/components/search/SERPCategorization.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusTestUtils: "resource://testing-common/NimbusTestUtils.sys.mjs",
  SearchSERPTelemetry:
    "moz-src:///browser/components/search/SearchSERPTelemetry.sys.mjs",
  SERPCategorizationRecorder:
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
    nonAdsLinkRegexps: [],
    extraAdServersRegexps: [/^https:\/\/example\.com\/ad/],
    // The search telemetry entry responsible for targeting the specific results.
    domainExtraction: {
      ads: [
        {
          selectors: "[data-ad-domain]",
          method: "dataAttribute",
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

add_setup(async function () {
  let oldThreshold = CATEGORIZATION_SETTINGS.PING_SUBMISSION_THRESHOLD;
  CATEGORIZATION_SETTINGS.PING_SUBMISSION_THRESHOLD = 1;

  SearchSERPTelemetry.overrideSearchTelemetryForTests(TEST_PROVIDER_INFO);
  await waitForIdle();

  let promise = waitForDomainToCategoriesUpdate();
  await insertRecordIntoCollectionAndSync();
  await SpecialPowers.pushPrefEnv({
    set: [["browser.search.serpMetricsRecordedCounter", 0]],
  });
  await promise;

  registerCleanupFunction(async () => {
    await SpecialPowers.popPrefEnv();
    CATEGORIZATION_SETTINGS.PING_SUBMISSION_THRESHOLD = oldThreshold;
    SearchSERPTelemetry.overrideSearchTelemetryForTests();
    resetTelemetry();
  });
});

add_task(async function test_no_experiment_enrollments() {
  let url = getSERPUrl("searchTelemetryDomainCategorizationReporting.html");
  info("Load a SERP with organic and sponsored results.");
  let promise = waitForPageWithCategorizedDomains();
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  await promise;

  await GleanPings.serpCategorization.testSubmission(
    () =>
      Assert.equal(
        Glean.serp.experimentInfo.testGetValue(),
        null,
        "No experiment info should be recorded when the client isn't enrolled in any experiments."
      ),
    () => BrowserTestUtils.removeTab(tab)
  );
});

add_task(async function test_1_non_search_experiment() {
  resetTelemetry();
  await ExperimentAPI.ready();

  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig(
    {
      featureId: "no-feature-firefox-desktop",
    },
    {
      slug: "non-search-experiment",
    }
  );

  let url = getSERPUrl("searchTelemetryDomainCategorizationReporting.html");
  info("Load a SERP with organic and sponsored results.");
  let promise = waitForPageWithCategorizedDomains();
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  await promise;

  await GleanPings.serpCategorization.testSubmission(
    () =>
      Assert.equal(
        Glean.serp.experimentInfo.testGetValue(),
        null,
        "No experiment info should be recorded when the client isn't enrolled in any search experiments."
      ),
    () => BrowserTestUtils.removeTab(tab)
  );

  await doExperimentCleanup();
});

add_task(async function test_1_search_experiment_no_targetExperiment() {
  resetTelemetry();
  await ExperimentAPI.ready();

  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig(
    {
      featureId: "search",
      value: {
        notTargetExperiment: "dummy-search-experiment1",
      },
    },
    {
      slug: "dummy-search-experiment1",
    }
  );

  let url = getSERPUrl("searchTelemetryDomainCategorizationReporting.html");
  info("Load a SERP with organic and sponsored results.");
  let promise = waitForPageWithCategorizedDomains();
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  await promise;

  await GleanPings.serpCategorization.testSubmission(
    () =>
      Assert.equal(
        Glean.serp.experimentInfo.testGetValue(),
        null,
        "No experiment info should be recorded when the client is enrolled in a search experiment without a targetExperiment."
      ),
    () => BrowserTestUtils.removeTab(tab)
  );

  await doExperimentCleanup();
});

add_task(async function test_1_search_experiment_with_targetExperiment() {
  resetTelemetry();
  await ExperimentAPI.ready();

  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig(
    {
      featureId: "search",
      value: {
        targetExperiment: "dummy-search-experiment1",
      },
    },
    {
      slug: "dummy-search-experiment1",
    }
  );

  let url = getSERPUrl("searchTelemetryDomainCategorizationReporting.html");
  info("Load a SERP with organic and sponsored results.");
  let promise = waitForPageWithCategorizedDomains();
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  await promise;

  await GleanPings.serpCategorization.testSubmission(
    () => {
      let actualExperimentInfo = Glean.serp.experimentInfo.testGetValue();
      Assert.deepEqual(
        actualExperimentInfo,
        {
          slug: "dummy-search-experiment1",
          branch: "control",
        },
        "Experiment info should be correct when the client is enrolled in a search experiment with a targetExperiment."
      );
    },
    () => BrowserTestUtils.removeTab(tab)
  );

  await doExperimentCleanup();
});

// You can also add a test similar to test_1_search_experiment_with_targetExperiment that checks a rollout with targetExperiment works.
add_task(async function test_1_search_rollout_with_targetExperiment() {
  resetTelemetry();
  await ExperimentAPI.ready();

  let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig(
    {
      featureId: "search",
      value: {
        targetExperiment: "dummy-search-experiment1",
      },
    },
    {
      slug: "dummy-search-experiment1",
    },
    { isRollout: true }
  );

  let url = getSERPUrl("searchTelemetryDomainCategorizationReporting.html");
  info("Load a SERP with organic and sponsored results.");
  let promise = waitForPageWithCategorizedDomains();
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  await promise;

  await GleanPings.serpCategorization.testSubmission(
    () => {
      let actualExperimentInfo = Glean.serp.experimentInfo.testGetValue();
      Assert.deepEqual(
        actualExperimentInfo,
        {
          slug: "dummy-search-experiment1",
          branch: "control",
        },
        "Experiment info should be correct when the client is enrolled in a search rollout with a targetExperiment."
      );
    },
    () => BrowserTestUtils.removeTab(tab)
  );

  await doExperimentCleanup();
});

add_task(
  async function test_only_submit_experiment_info_when_there_are_categorization_events() {
    resetTelemetry();
    await ExperimentAPI.ready();

    let doExperimentCleanup1 = await NimbusTestUtils.enrollWithFeatureConfig(
      {
        featureId: "no-feature-firefox-desktop",
      },
      {
        slug: "non-search-experiment",
      }
    );

    let doExperimentCleanup2 = await NimbusTestUtils.enrollWithFeatureConfig(
      {
        featureId: "search",
        value: {
          targetExperiment: "dummy-search-experiment1",
        },
      },
      {
        slug: "dummy-search-experiment1",
      }
    );

    let url = getSERPUrl("searchTelemetryDomainCategorizationReporting.html");
    info(
      "Load a SERP with organic and sponsored results and intentionally skip the SERP categorization process."
    );
    let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);

    let submitted = false;
    GleanPings.serpCategorization.testBeforeNextSubmit(() => {
      submitted = true;
    });

    Assert.equal(
      Glean.serp.categorization.testGetValue(),
      null,
      "Should not have categorization events."
    );
    Assert.equal(
      Glean.serp.experimentInfo.testGetValue(),
      null,
      "Should not record experiment info when there are no categorization events."
    );

    // At this "accidental" early ping submission, we haven't triggered any
    // categorization events to be recorded yet.
    SERPCategorizationRecorder.submitPing("startup");
    Assert.equal(
      submitted,
      false,
      "Categorization ping should not have been submitted."
    );

    await BrowserTestUtils.removeTab(tab);

    await doExperimentCleanup1();
    await doExperimentCleanup2();
  }
);

add_task(
  async function test_no_inactivity_ping_when_no_categorizations_present() {
    resetTelemetry();

    let oldActivityLimit = Services.prefs.getIntPref(
      "telemetry.fog.test.activity_limit"
    );
    Services.prefs.setIntPref("telemetry.fog.test.activity_limit", 2);

    await ExperimentAPI.ready();

    let doExperimentCleanup1 = await NimbusTestUtils.enrollWithFeatureConfig(
      {
        featureId: "no-feature-firefox-desktop",
      },
      {
        slug: "non-search-experiment",
      }
    );

    let doExperimentCleanup2 = await NimbusTestUtils.enrollWithFeatureConfig(
      {
        featureId: "search",
        value: {
          targetExperiment: "dummy-search-experiment1",
        },
      },
      {
        slug: "dummy-search-experiment1",
      }
    );

    Assert.equal(
      Glean.serp.categorization.testGetValue(),
      null,
      "Should not have recorded any metrics yet."
    );

    let url = getSERPUrl("searchTelemetryDomainCategorizationReporting.html");
    info(
      "Load a SERP with organic and sponsored results and intentionally skip the SERP categorization process."
    );
    let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);

    let submitted = false;
    GleanPings.serpCategorization.testBeforeNextSubmit(() => {
      submitted = true;
    });

    Assert.equal(
      Glean.serp.categorization.testGetValue(),
      null,
      "Should not have categorization events."
    );
    Assert.equal(
      Glean.serp.experimentInfo.testGetValue(),
      null,
      "Should not record experiment info when there are no categorization events."
    );

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

    // At this "accidental" early ping submission, we haven't triggered any
    // categorization events to be recorded yet.
    SERPCategorizationRecorder.submitPing("inactivity");

    Assert.equal(
      submitted,
      false,
      "An inactivity ping should not be submitted when there is no SERP categorization."
    );

    await BrowserTestUtils.removeTab(tab);

    await doExperimentCleanup1();
    await doExperimentCleanup2();

    Services.prefs.setIntPref(
      "telemetry.fog.test.activity_limit",
      oldActivityLimit
    );
  }
);
