/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Check SPA page loads on two different providers that are both SPAs. A sanity
 * check to ensure we're categorizing them separately. They differ by having
 * different top level domains (.com vs .org).
 */

"use strict";

add_setup(async function () {
  await initSinglePageAppTest();

  registerCleanupFunction(async () => {
    SearchSERPTelemetry.overrideSearchTelemetryForTests();
    resetTelemetry();
  });
});

add_task(async function test_load_serps_and_click_organic() {
  resetTelemetry();

  let tabs = await SinglePageAppUtils.createTabsWithDifferentProviders();

  for (let tab of tabs) {
    await SinglePageAppUtils.clickOrganic(tab);
  }

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": {
        "example1:tagged:ff": 1,
        "example2:tagged:ff": 1,
      },
      "browser.search.withads.unknown": {
        "example1:tagged": 1,
        "example2:tagged": 1,
      },
    }
  );

  assertSERPTelemetry([
    {
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
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
        },
      ],
    },
    {
      impression: {
        provider: "example2",
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
          target: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
        },
      ],
    },
  ]);

  for (let tab of tabs) {
    await BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function test_load_serps_and_click_ads() {
  resetTelemetry();

  let tabs = await SinglePageAppUtils.createTabsWithDifferentProviders();

  for (let tab of tabs) {
    await BrowserTestUtils.switchTab(gBrowser, tab);
    await SinglePageAppUtils.clickAd(tab);
  }

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": {
        "example1:tagged:ff": 1,
        "example2:tagged:ff": 1,
      },
      "browser.search.withads.unknown": {
        "example1:tagged": 1,
        "example2:tagged": 1,
      },
      "browser.search.adclicks.unknown": {
        "example1:tagged": 1,
        "example2:tagged": 1,
      },
    }
  );

  assertSERPTelemetry([
    {
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
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
        },
      ],
    },
    {
      impression: {
        provider: "example2",
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

  for (let tab of tabs) {
    await BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function test_load_serps_and_click_related() {
  resetTelemetry();

  let tabs = await SinglePageAppUtils.createTabsWithDifferentProviders();

  for (let tab of tabs) {
    await BrowserTestUtils.switchTab(gBrowser, tab);
    await SinglePageAppUtils.visitRelatedSearch(tab);
  }

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": {
        "example1:tagged:ff": 2,
        "example2:tagged:ff": 2,
      },
      "browser.search.withads.unknown": {
        "example1:tagged": 2,
        "example2:tagged": 2,
      },
    }
  );

  assertSERPTelemetry([
    {
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
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
        },
      ],
    },
    {
      impression: {
        provider: "example2",
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
          target: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
        },
      ],
    },
    {
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
      impression: {
        provider: "example2",
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

  for (let tab of tabs) {
    await BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function test_load_pages_tabhistory() {
  resetTelemetry();

  let tabs = await SinglePageAppUtils.createTabsWithDifferentProviders();

  for (let tab of tabs) {
    await BrowserTestUtils.switchTab(gBrowser, tab);
    await SinglePageAppUtils.visitRelatedSearch(tab);
    await SinglePageAppUtils.goBack(tab);
    await SinglePageAppUtils.goForward(tab);
  }

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": {
        "example1:tagged:ff": 2,
        "example2:tagged:ff": 2,
      },
      "browser.search.withads.unknown": {
        "example1:tagged": 2,
        "example2:tagged": 2,
      },
      "browser.search.content.tabhistory": {
        "example1:tagged:ff": 2,
        "example2:tagged:ff": 2,
      },
      "browser.search.withads.tabhistory": {
        "example1:tagged": 2,
        "example2:tagged": 2,
      },
    }
  );

  assertSERPTelemetry([
    {
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
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
        },
      ],
    },
    {
      // This is second because it was the second tab created.
      impression: {
        provider: "example2",
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
          target: SearchSERPTelemetryUtils.COMPONENTS.NON_ADS_LINK,
        },
      ],
    },
    {
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
      abandonment: {
        reason: SearchSERPTelemetryUtils.ABANDONMENTS.NAVIGATION,
      },
    },
    {
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
    },
    {
      impression: {
        provider: "example2",
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
      impression: {
        provider: "example2",
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
      abandonment: {
        reason: SearchSERPTelemetryUtils.ABANDONMENTS.NAVIGATION,
      },
    },
    {
      impression: {
        provider: "example2",
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
    },
  ]);

  for (let tab of tabs) {
    await BrowserTestUtils.removeTab(tab);
  }
});
