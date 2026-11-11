/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Tests that clicking links on a SERP whose provider has no components doesn't
 * record SERP engagements, since there is no impression to associate them
 * with, while legacy telemetry is still recorded.
 */

"use strict";

const TEST_PROVIDER_INFO = [
  {
    telemetryId: "example",
    searchPageRegexp:
      /^https:\/\/example.org\/browser\/browser\/components\/search\/test\/browser\/telemetry\/searchTelemetryAd(?:_searchbox)?.html/,
    queryParamNames: ["s"],
    codeParamName: "abc",
    taggedCodes: ["ff"],
    followOnParamNames: ["a"],
    extraAdServersRegexps: [/^https:\/\/example\.com\/ad2?/],
  },
];

add_setup(async function () {
  SearchSERPTelemetry.overrideSearchTelemetryForTests(TEST_PROVIDER_INFO);
  await waitForIdle();
  let oldCanRecord = Services.telemetry.canRecordExtended;
  Services.telemetry.canRecordExtended = true;

  registerCleanupFunction(async () => {
    SearchSERPTelemetry.overrideSearchTelemetryForTests();
    Services.telemetry.canRecordExtended = oldCanRecord;
    resetTelemetry();
  });
});

add_task(async function test_click_ad() {
  resetTelemetry();

  let adsReported = TestUtils.topicObserved("reported-page-with-ads");
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    getSERPUrl("searchTelemetryAd.html")
  );
  await adsReported;

  let pageLoadPromise = BrowserTestUtils.waitForLocationChange(gBrowser);
  BrowserTestUtils.synthesizeMouseAtCenter("#ad1", {}, tab.linkedBrowser);
  await pageLoadPromise;

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": { "example:tagged:ff": 1 },
      "browser.search.withads.unknown": { "example:tagged": 1 },
      "browser.search.adclicks.unknown": { "example:tagged": 1 },
    }
  );

  Assert.equal(
    Glean.serp.impression.testGetValue(),
    null,
    "Should not have recorded an impression."
  );
  Assert.equal(
    Glean.serp.engagement.testGetValue(),
    null,
    "Should not have recorded an engagement."
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_click_non_ad() {
  resetTelemetry();

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    getSERPUrl("searchTelemetryAd_searchbox.html")
  );

  let browserLoadedPromise = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    true,
    "https://example.com/hello_world"
  );
  await BrowserTestUtils.synthesizeMouseAtCenter(
    "#non_ads_link",
    {},
    tab.linkedBrowser
  );
  await browserLoadedPromise;

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": { "example:tagged:ff": 1 },
    }
  );

  Assert.equal(
    Glean.serp.impression.testGetValue(),
    null,
    "Should not have recorded an impression."
  );
  Assert.equal(
    Glean.serp.engagement.testGetValue(),
    null,
    "Should not have recorded an engagement."
  );

  BrowserTestUtils.removeTab(tab);
});
