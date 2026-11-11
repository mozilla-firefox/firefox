/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Check SPA page loads on a single provider using multiple tabs.
 */

"use strict";

// Allow more time for Mac machines so they don't time out in verify mode.
if (AppConstants.platform == "macosx") {
  requestLongerTimeout(3);
}

add_setup(async function () {
  await initSinglePageAppTest();

  registerCleanupFunction(async () => {
    SearchSERPTelemetry.overrideSearchTelemetryForTests();
    resetTelemetry();
  });
});

// Deliberately has actions happening after one another to assert that the
// different events are recorded properly.

// One issue that can occur is if two SERPs have the same search term, if we
// try finding the item for the URL, it might match the wrong item.
// e.g. two tabs search for foobar
// one tab then searches for barfoo
// the other tab that had foobar does another search, but instead of referring
// back to foobar, it looks at barfoo and messes with its state.

// We use switch tabs to avoid `getBoundsWithoutFlushing` not returning the
// latest visual info, which affects ad visibility counts.
add_task(async function test_load_serps_and_click_related_searches() {
  resetTelemetry();

  let tab1 = await SinglePageAppUtils.createTabAndLoadURL();
  let tab2 = await SinglePageAppUtils.createTabAndLoadURL();
  let tab3 = await SinglePageAppUtils.createTabAndLoadURL();

  await BrowserTestUtils.switchTab(gBrowser, tab1);
  await SinglePageAppUtils.visitRelatedSearchWithoutAds(tab1);

  await BrowserTestUtils.switchTab(gBrowser, tab2);
  await SinglePageAppUtils.visitRelatedSearch(tab2);

  await BrowserTestUtils.switchTab(gBrowser, tab3);
  await SinglePageAppUtils.visitRelatedSearchWithoutAds(tab3);

  await BrowserTestUtils.switchTab(gBrowser, tab1);
  await SinglePageAppUtils.visitRelatedSearch(tab1);

  await BrowserTestUtils.switchTab(gBrowser, tab2);
  await SinglePageAppUtils.visitRelatedSearchWithoutAds(tab2);

  await BrowserTestUtils.switchTab(gBrowser, tab3);
  await SinglePageAppUtils.visitRelatedSearchWithoutAds(tab3);

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": {
        "example1:tagged:ff": 9,
      },
      "browser.search.withads.unknown": {
        "example1:tagged": 5,
      },
    }
  );

  assertSERPTelemetry([
    {
      // Tab 1 - Visit a SERP, clicks a related search without ads.
      impression: {
        provider: "example1",
      },
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
        },
      ],
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
    {
      // Tab 2 - Visits a SERP, clicks a related SERP with ads.
      impression: {
        provider: "example1",
      },
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
        },
      ],
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
    {
      // Tab 3 - Visit a SERP, clicks a related SERP without ads.
      impression: {
        provider: "example1",
      },
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
        },
      ],
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
    {
      // Tab 1 - Visits a related SERP without ads, clicks on a related SERP
      // with ads.
      impression: {
        provider: "example1",
      },
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
        },
      ],
      adImpressions: [],
    },
    {
      // Tab 2 - Visits a related search with ads, clicks a related SERP
      // without ads.
      impression: {
        provider: "example1",
      },
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
        },
      ],
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
    {
      // Tab 3 - Visit a SERP without ads, clicks a related SERP without ads.
      impression: {
        provider: "example1",
      },
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
        },
      ],
      adImpressions: [],
    },
    {
      // Tab 1 - Visit a related SERP with ads.
      impression: {
        provider: "example1",
      },
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
    {
      // Tab 2 - Visit a related SERP without ads.
      impression: {
        provider: "example1",
      },
      adImpressions: [],
    },
    {
      // Tab 3 - Visit a related SERP without ads.
      impression: {
        provider: "example1",
      },
      adImpressions: [],
    },
  ]);

  await BrowserTestUtils.removeTab(tab1);
  await BrowserTestUtils.removeTab(tab2);
  await BrowserTestUtils.removeTab(tab3);
});

/**
 * The source of the ad click should match the correct tab.
 */
add_task(async function test_different_sources_click_ad() {
  resetTelemetry();

  let tab1 = await SinglePageAppUtils.createTabAndLoadURL();
  let tab2 = await SinglePageAppUtils.createTabAndLoadURL();

  await SinglePageAppUtils.visitRelatedSearch(tab2);
  await SinglePageAppUtils.goBack(tab2);
  await SinglePageAppUtils.clickAd(tab2);

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": {
        "example1:tagged:ff": 3,
      },
      "browser.search.withads.unknown": {
        "example1:tagged": 3,
      },
      "browser.search.content.tabhistory": {
        "example1:tagged:ff": 1,
      },
      "browser.search.withads.tabhistory": {
        "example1:tagged": 1,
      },
      "browser.search.adclicks.tabhistory": {
        "example1:tagged": 1,
      },
    }
  );

  assertSERPTelemetry([
    {
      // Tab 1 - Visit a SERP.
      impression: {
        provider: "example1",
      },
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
    {
      // Tab 2 - Visit a SERP, click a related SERP with ads.
      impression: {
        provider: "example1",
      },
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
        },
      ],
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
    {
      // Tab 2 - Visit a SERP, click back button.
      impression: {
        provider: "example1",
      },
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
      abandonment: {
        reason: SearchSERPTelemetryUtils.ABANDONMENTS.NAVIGATION,
      },
    },
    {
      // Tab 2 - Visit a SERP, click ad button.
      impression: {
        provider: "example1",
        source: "tabhistory",
      },
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
        },
      ],
    },
  ]);

  await BrowserTestUtils.removeTab(tab1);
  await BrowserTestUtils.removeTab(tab2);
});

add_task(async function test_different_sources_click_redirect_ad_in_new_tab() {
  resetTelemetry();

  let tab1 = await SinglePageAppUtils.createTabAndLoadURL();
  let tab2 = await SinglePageAppUtils.createTabAndLoadURL();

  await SinglePageAppUtils.visitRelatedSearch(tab2);
  await SinglePageAppUtils.goBack(tab2);
  let tab3 = await SinglePageAppUtils.clickRedirectAdInNewTab(tab2);

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": {
        "example1:tagged:ff": 3,
      },
      "browser.search.withads.unknown": {
        "example1:tagged": 3,
      },
      "browser.search.content.tabhistory": {
        "example1:tagged:ff": 1,
      },
      "browser.search.withads.tabhistory": {
        "example1:tagged": 1,
      },
      "browser.search.adclicks.tabhistory": {
        "example1:tagged": 1,
      },
    }
  );

  assertSERPTelemetry([
    {
      // Tab 1 - Visit a SERP.
      impression: {
        provider: "example1",
      },
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
    {
      // Tab 2 - Visit a SERP, click a related SERP with ads.
      impression: {
        provider: "example1",
      },
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
        },
      ],
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
    {
      // Tab 2 - Visit a SERP, click back button.
      impression: {
        provider: "example1",
      },
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
      abandonment: {
        reason: SearchSERPTelemetryUtils.ABANDONMENTS.NAVIGATION,
      },
    },
    {
      // Tab 2 - Visit a SERP, click ad button.
      impression: {
        provider: "example1",
        source: "tabhistory",
      },
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
        },
      ],
    },
  ]);

  await BrowserTestUtils.removeTab(tab1);
  await BrowserTestUtils.removeTab(tab2);
  await BrowserTestUtils.removeTab(tab3);
});

add_task(async function test_update_query_params_after_search() {
  resetTelemetry();

  let tab1 = await SinglePageAppUtils.createTabAndLoadURL();
  info("Visit a related search so that the URL has an extra query parameter.");
  await SinglePageAppUtils.visitRelatedSearch(tab1);

  let tab2 = await SinglePageAppUtils.createTabAndLoadURL();

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": {
        "example1:tagged:ff": 3,
      },
      "browser.search.withads.unknown": {
        "example1:tagged": 3,
      },
    }
  );

  assertSERPTelemetry([
    {
      // Tab 1 - Visit a SERP, click on a related SERP.
      impression: {
        provider: "example1",
      },
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
        },
      ],
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
    {
      // Tab 1 - Visit a related SERP.
      impression: {
        provider: "example1",
      },
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
    {
      // Tab 2 - Visit a SERP, click on an ad.
      impression: {
        provider: "example1",
      },
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
  ]);

  await BrowserTestUtils.removeTab(tab1);
  await BrowserTestUtils.removeTab(tab2);
});

add_task(async function test_update_query_params() {
  resetTelemetry();

  // Deliberately use a different search term for the first example, because
  // if both tabs have the same search term and a link is clicked that opens a
  // new window, we currently can't recover the exact browser.
  let tab1 = await SinglePageAppUtils.createTabAndSearch("foo bar");
  info("Visit a related search so that the URL has an extra query parameter.");
  await SinglePageAppUtils.visitRelatedSearch(tab1);

  let tab2 = await SinglePageAppUtils.createTabAndLoadURL();
  let newWindow = await SinglePageAppUtils.clickRedirectAdInNewWindow(tab2);

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": {
        "example1:tagged:ff": 3,
      },
      "browser.search.withads.unknown": {
        "example1:tagged": 3,
      },
      "browser.search.adclicks.unknown": { "example1:tagged": 1 },
    }
  );

  assertSERPTelemetry([
    {
      // Tab 1 - Visit a SERP, clicked a related SERP.
      impression: {
        provider: "example1",
      },
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
        },
      ],
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
    {
      // Tab 1 - Visit a related SERP.
      impression: {
        provider: "example1",
      },
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
    {
      // Tab 2 - Visit a SERP, click ad opening in a new window.
      impression: {
        provider: "example1",
      },
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
        },
      ],
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
  ]);

  await BrowserTestUtils.closeWindow(newWindow);
  await BrowserTestUtils.removeTab(tab1);
  await BrowserTestUtils.removeTab(tab2);
});

add_task(async function test_update_query_params_multiple_related() {
  resetTelemetry();

  let tab1 = await SinglePageAppUtils.createTabAndSearch("foo bar");
  info("Visit a related search so that the URL has an extra query parameter.");
  await SinglePageAppUtils.visitRelatedSearch(tab1);

  let tab2 = await SinglePageAppUtils.createTabAndLoadURL();
  info("Visit a related search so that the URL has an extra query parameter.");
  await SinglePageAppUtils.visitRelatedSearch(tab2);

  let newWindow = await SinglePageAppUtils.clickRedirectAdInNewWindow(tab2);

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": {
        "example1:tagged:ff": 4,
      },
      "browser.search.withads.unknown": {
        "example1:tagged": 4,
      },
      "browser.search.adclicks.unknown": { "example1:tagged": 1 },
    }
  );

  assertSERPTelemetry([
    {
      // Tab 1 - Visit a SERP, clicked a related SERP.
      impression: {
        provider: "example1",
      },
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
        },
      ],
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
    {
      // Tab 1 - Visit a related SERP.
      impression: {
        provider: "example1",
      },
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
    {
      // Tab 2 - Visit a SERP, clicked a related SERP.
      impression: {
        provider: "example1",
      },
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
        },
      ],
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
    {
      // Tab 2 - Visit a related SERP. Click on ad that opens in a new window.
      impression: {
        provider: "example1",
      },
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
        },
      ],
      adImpressions: [
        {
          component: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
          ads_loaded: "2",
          ads_visible: "2",
          ads_hidden: "0",
        },
      ],
    },
  ]);

  await BrowserTestUtils.closeWindow(newWindow);
  await BrowserTestUtils.removeTab(tab1);
  await BrowserTestUtils.removeTab(tab2);
});
