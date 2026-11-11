/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/*
 * This test ensures we are correctly reporting categorized domains from a SERP.
 */

ChromeUtils.defineESModuleGetters(this, {
  CATEGORIZATION_SETTINGS:
    "moz-src:///browser/components/search/SERPCategorization.sys.mjs",
  SERPDomainToCategoriesMap:
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
    organicCodes: [],
    adServerAttributes: ["mozAttr"],
    nonAdsLinkRegexps: [],
    extraAdServersRegexps: [
      /^https:\/\/example\.com\/ad/,
      /^https:\/\/www\.test(1[3456789]|2[01234])\.com/,
    ],
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
    impressionAttributes: [
      {
        key: "is_shopping_page",
        url: {
          regexp: "&page=shop",
        },
      },
    ],
  },
];

const client = RemoteSettings(TELEMETRY_CATEGORIZATION_KEY);
const db = client.db;

let categorizationRecord;
let categorizationAttachment;

add_setup(async function () {
  SearchSERPTelemetry.overrideSearchTelemetryForTests(TEST_PROVIDER_INFO);
  await waitForIdle();

  // Enable local telemetry recording for the duration of the tests.
  let oldCanRecord = Services.telemetry.canRecordExtended;
  Services.telemetry.canRecordExtended = true;

  let { record, attachment } = await insertRecordIntoCollection();
  categorizationRecord = record;
  categorizationAttachment = attachment;

  let promise = waitForDomainToCategoriesUpdate();
  await syncCollection(record);
  await promise;

  registerCleanupFunction(async () => {
    SearchSERPTelemetry.overrideSearchTelemetryForTests();
    Services.telemetry.canRecordExtended = oldCanRecord;
    resetTelemetry();
    await db.clear();
  });
});

add_task(async function test_categorization_reporting() {
  resetTelemetry();

  let url = getSERPUrl("searchTelemetryDomainCategorizationReporting.html");
  info("Load a sample SERP with organic and sponsored results.");
  let promise = waitForPageWithCategorizedDomains();
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  await promise;

  await BrowserTestUtils.removeTab(tab);
  assertCategorizationValues([
    {
      organic_category: "3",
      organic_num_domains: "1",
      organic_num_inconclusive: "0",
      organic_num_unknown: "0",
      sponsored_category: "4",
      sponsored_num_domains: "2",
      sponsored_num_inconclusive: "0",
      sponsored_num_unknown: "0",
      mappings_version: "1",
      app_version: APP_MAJOR_VERSION,
      channel: CHANNEL,
      region: REGION,
      partner_code: "ff",
      provider: "example",
      tagged: "true",
      is_shopping_page: "false",
      num_ads_clicked: "0",
      num_ads_hidden: "0",
      num_ads_loaded: "2",
      num_ads_visible: "2",
    },
  ]);
});

add_task(async function test_no_reporting_if_download_failure() {
  resetTelemetry();

  let sandbox = sinon.createSandbox();
  sandbox
    .stub(RemoteSettings(TELEMETRY_CATEGORIZATION_KEY).attachments, "download")
    .throws(new Error("Simulated Download Error"));

  let observeDownloadError = TestUtils.consoleMessageObserved(msg => {
    return (
      typeof msg.wrappedJSObject.arguments?.[0] == "string" &&
      msg.wrappedJSObject.arguments[0].includes("Could not download file:")
    );
  });
  // Since the preference is already enabled, and the map is filled we trigger
  // the map to be updated via an RS sync. The download failure should cause the
  // map to remain empty.
  await syncCollection(categorizationRecord);
  await observeDownloadError;

  let url = getSERPUrl("searchTelemetryDomainCategorizationReporting.html");
  info("Load a sample SERP with organic results.");
  let promise = waitForPageWithCategorizedDomains();
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  await promise;

  await BrowserTestUtils.removeTab(tab);
  // We should not record telemetry if attachments weren't downloaded.
  assertCategorizationValues([]);

  await sandbox.restore();

  // The map is going to attempt to redo a download. There are other tests that
  // do it, so instead reset the map so later tests don't get interrupted by
  // a sync event caused by this test.
  await SERPDomainToCategoriesMap.uninit();
  await SERPDomainToCategoriesMap.init();
});

add_task(async function test_no_reporting_if_no_records() {
  resetTelemetry();

  let observeNoRecords = TestUtils.consoleMessageObserved(msg => {
    return (
      typeof msg.wrappedJSObject.arguments?.[0] == "string" &&
      msg.wrappedJSObject.arguments[0].includes(
        "No records found for domain-to-categories map."
      )
    );
  });
  await syncCollection();
  await observeNoRecords;

  let url = getSERPUrl("searchTelemetryDomainCategorizationReporting.html");
  info("Load a sample SERP with organic results.");
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);

  info("Wait roughly the amount of time a categorization event should occur.");
  let waitPromise = sleep(1000);
  let categorizationPromise = waitForPageWithCategorizedDomains();
  let result = await Promise.race([
    waitPromise.then(() => false),
    categorizationPromise.then(() => true),
  ]);
  Assert.equal(
    result,
    false,
    "Received a categorization event before the timeout."
  );
  await BrowserTestUtils.removeTab(tab);

  // We should not record telemetry if there are no records matching the region.
  assertCategorizationValues([]);
});

// Per a request from Data Science, we need to limit the number of domains
// categorized to 10 non ad domains and 10 ad domains.
add_task(async function test_reporting_limited_to_10_domains_of_each_kind() {
  resetTelemetry();

  await insertRecordIntoCollectionAndSync();

  let url = getSERPUrl(
    "searchTelemetryDomainCategorizationCapProcessedDomains.html"
  );
  info(
    "Load a sample SERP with more than 10 organic results and more than 10 sponsored results."
  );
  let domainsCategorizedPromise = waitForPageWithCategorizedDomains();
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  await domainsCategorizedPromise;

  await BrowserTestUtils.removeTab(tab);

  assertCategorizationValues([
    {
      organic_category: "0",
      organic_num_domains:
        CATEGORIZATION_SETTINGS.MAX_DOMAINS_TO_CATEGORIZE.toString(),
      organic_num_inconclusive: "0",
      organic_num_unknown: "10",
      sponsored_category: "2",
      sponsored_num_domains:
        CATEGORIZATION_SETTINGS.MAX_DOMAINS_TO_CATEGORIZE.toString(),
      sponsored_num_inconclusive: "0",
      sponsored_num_unknown: "8",
      mappings_version: "1",
      app_version: APP_MAJOR_VERSION,
      channel: CHANNEL,
      region: REGION,
      partner_code: "ff",
      provider: "example",
      tagged: "true",
      is_shopping_page: "false",
      num_ads_clicked: "0",
      num_ads_hidden: "0",
      num_ads_loaded: "12",
      num_ads_visible: "12",
    },
  ]);
});

add_task(async function test_categorization_reporting_for_shopping_page() {
  resetTelemetry();

  let url = getSERPUrl("searchTelemetryDomainCategorizationReporting.html");
  let shoppingUrl = new URL(url);
  shoppingUrl.searchParams.set("page", "shop");
  shoppingUrl = shoppingUrl.toString();
  info("Load a sample shopping page SERP with organic and sponsored results.");
  let promise = waitForPageWithCategorizedDomains();
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, shoppingUrl);
  await promise;

  await BrowserTestUtils.removeTab(tab);
  assertCategorizationValues([
    {
      organic_category: "3",
      organic_num_domains: "1",
      organic_num_inconclusive: "0",
      organic_num_unknown: "0",
      sponsored_category: "4",
      sponsored_num_domains: "2",
      sponsored_num_inconclusive: "0",
      sponsored_num_unknown: "0",
      mappings_version: "1",
      app_version: APP_MAJOR_VERSION,
      channel: CHANNEL,
      region: REGION,
      partner_code: "ff",
      provider: "example",
      tagged: "true",
      is_shopping_page: "true",
      num_ads_clicked: "0",
      num_ads_hidden: "0",
      num_ads_loaded: "2",
      num_ads_visible: "2",
    },
  ]);
});
