/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/*
 * Checks reporting of pages without ads is accurate.
 */

ChromeUtils.defineESModuleGetters(this, {
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
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
      ads: [],
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
        key: "shopping_tab_displayed",
        element: {
          selector: "#shopping",
          component: {
            type: "shopping_tab",
            countImpressions: true,
          },
        },
      },
    ],
  },
];

add_setup(async function () {
  SearchSERPTelemetry.overrideSearchTelemetryForTests(TEST_PROVIDER_INFO);
  await waitForIdle();

  let promise = waitForDomainToCategoriesUpdate();
  await insertRecordIntoCollectionAndSync();
  await promise;

  registerCleanupFunction(async () => {
    SearchSERPTelemetry.overrideSearchTelemetryForTests();
    resetTelemetry();
  });
});

add_task(
  async function test_load_serp_without_sponsored_links_and_categorize() {
    resetTelemetry();

    let url = getSERPUrl(
      "searchTelemetryDomainCategorizationReportingWithoutAds.html"
    );
    info("Load a SERP with organic and ad components that are non-sponsored.");
    let promise = waitForPageWithCategorizedDomains();
    let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
    await promise;

    info("Assert there is a non-sponsored component on the page.");
    assertSERPTelemetry([
      {
        impression: {
          shopping_tab_displayed: "true",
        },
        adImpressions: [
          {
            component: SearchSERPTelemetryUtils.COMPONENTS.SHOPPING_TAB,
            ads_loaded: "1",
            ads_visible: "1",
            ads_hidden: "0",
          },
        ],
      },
    ]);

    info("Click on the non-sponsored component.");
    await BrowserTestUtils.synthesizeMouseAtCenter(
      "#shopping",
      {},
      tab.linkedBrowser
    );

    await BrowserTestUtils.removeTab(tab);
    info("Assert no ads were visible or clicked on.");
    assertCategorizationValues([
      {
        organic_category: "3",
        organic_num_domains: "1",
        organic_num_inconclusive: "0",
        organic_num_unknown: "0",
        sponsored_category: "0",
        sponsored_num_domains: "0",
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
        num_ads_loaded: "0",
        num_ads_visible: "0",
      },
    ]);
  }
);
