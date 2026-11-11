/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Check SPA in-content interactions (e.g. search box, clicking autosuggest) and
 * ensures we're correctly unloading / adding listeners to elements, and
 * registering the right engagements for search submission events that could
 * change the location of the page.
 */

"use strict";

add_setup(async function () {
  await initSinglePageAppTest();

  registerCleanupFunction(async () => {
    SearchSERPTelemetry.overrideSearchTelemetryForTests();
    resetTelemetry();
  });
});

add_task(async function test_content_process_type_search_click_suggestion() {
  resetTelemetry();

  let tab = await SinglePageAppUtils.createTabAndLoadURL();
  await SinglePageAppUtils.clickSearchboxAndType(tab);
  await SinglePageAppUtils.clickSuggestion(tab);

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": {
        "example1:tagged:ff": 2,
      },
      "browser.search.withads.unknown": {
        "example1:tagged": 2,
      },
    }
  );

  assertSERPTelemetry([
    {
      impression: {
        provider: "example1",
      },
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.INCONTENT_SEARCHBOX,
        },
        {
          action: SearchSERPTelemetryUtils.ACTIONS.SUBMITTED,
          target: SearchSERPTelemetryUtils.COMPONENTS.INCONTENT_SEARCHBOX,
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
      impression: {
        provider: "example1",
        source: "follow_on_from_refine_on_incontent_search",
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
  await BrowserTestUtils.removeTab(tab);
});

add_task(
  async function test_content_process_type_search_click_related_search() {
    resetTelemetry();

    let tab = await SinglePageAppUtils.createTabAndLoadURL();
    await SinglePageAppUtils.clickSearchboxAndType(tab);
    await SinglePageAppUtils.visitRelatedSearch(tab);

    await assertSearchSourcesTelemetry(
      {},
      {
        "browser.search.content.unknown": {
          "example1:tagged:ff": 2,
        },
        "browser.search.withads.unknown": {
          "example1:tagged": 2,
        },
      }
    );

    assertSERPTelemetry([
      {
        impression: {
          provider: "example1",
        },
        engagements: [
          {
            action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
            target: SearchSERPTelemetryUtils.COMPONENTS.INCONTENT_SEARCHBOX,
          },
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
    await BrowserTestUtils.removeTab(tab);
  }
);

add_task(async function test_content_process_engagement() {
  resetTelemetry();

  let tab = await SinglePageAppUtils.createTabAndLoadURL();
  await SinglePageAppUtils.clickSearchbox(tab);

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": {
        "example1:tagged:ff": 1,
      },
      "browser.search.withads.unknown": {
        "example1:tagged": 1,
      },
    }
  );

  assertSERPTelemetry([
    {
      impression: {
        provider: "example1",
      },
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.INCONTENT_SEARCHBOX,
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

  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_content_process_engagement_that_changes_page() {
  resetTelemetry();

  let tab = await SinglePageAppUtils.createTabAndLoadURL();
  await SinglePageAppUtils.clickSuggestion(tab);

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": {
        "example1:tagged:ff": 2,
      },
      "browser.search.withads.unknown": {
        "example1:tagged": 2,
      },
    }
  );

  assertSERPTelemetry([
    {
      impression: {
        provider: "example1",
      },
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.SUBMITTED,
          target: SearchSERPTelemetryUtils.COMPONENTS.INCONTENT_SEARCHBOX,
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
      impression: {
        provider: "example1",
        source: "follow_on_from_refine_on_incontent_search",
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

  await BrowserTestUtils.removeTab(tab);
});

// This is to ensure if the user switches to another search page, we unload
// the listeners, add them back in, and then accurately register the correct
// number of engagements. The engagement target should also be accurate.
add_task(
  async function test_in_page_reload_and_content_process_engagement_that_changes_page() {
    resetTelemetry();

    let tab = await SinglePageAppUtils.createTabAndLoadURL();
    await SinglePageAppUtils.visitRelatedSearch(tab);
    await SinglePageAppUtils.clickSuggestion(tab);

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
        impression: {
          provider: "example1",
        },
        engagements: [
          {
            action: SearchSERPTelemetryUtils.ACTIONS.SUBMITTED,
            target: SearchSERPTelemetryUtils.COMPONENTS.INCONTENT_SEARCHBOX,
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
        impression: {
          provider: "example1",
          source: "follow_on_from_refine_on_incontent_search",
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

    await BrowserTestUtils.removeTab(tab);
  }
);

// Clicking on another SERP tab and selecting the searchbox shouldn't cause a
// new engagement.
add_task(async function test_unload_listeners_single_tab() {
  resetTelemetry();

  let tab = await SinglePageAppUtils.createTabAndLoadURL();
  await SinglePageAppUtils.clickImagesTab(tab);
  await SinglePageAppUtils.clickSearchbox(tab);
  await SinglePageAppUtils.clickSuggestionOnImagesTab(tab);

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": {
        "example1:tagged:ff": 1,
      },
      "browser.search.withads.unknown": {
        "example1:tagged": 1,
      },
    }
  );

  assertSERPTelemetry([
    {
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
  ]);

  await BrowserTestUtils.removeTab(tab);
});

// Make sure unloading listeners is specific to the tab.
add_task(async function test_unload_listeners_multi_tab() {
  resetTelemetry();

  let tab1 = await SinglePageAppUtils.createTabAndLoadURL();
  let tab2 = await SinglePageAppUtils.createTabAndLoadURL();

  // Listener should no longer be applicable on tab2 because we're switching
  // to tab2.
  await SinglePageAppUtils.clickImagesTab(tab2);
  await SinglePageAppUtils.clickSearchbox(tab2);
  await SinglePageAppUtils.clickSuggestionOnImagesTab(tab2);

  // Click a searchbox on tab1 to verify the listener is still working.
  await SinglePageAppUtils.clickSearchbox(tab1);

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": {
        "example1:tagged:ff": 2,
      },
      "browser.search.withads.unknown": {
        "example1:tagged": 2,
      },
    }
  );

  assertSERPTelemetry([
    {
      impression: {
        provider: "example1",
      },
      engagements: [
        {
          action: SearchSERPTelemetryUtils.ACTIONS.CLICKED,
          target: SearchSERPTelemetryUtils.COMPONENTS.INCONTENT_SEARCHBOX,
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
  ]);

  await BrowserTestUtils.removeTab(tab1);
  await BrowserTestUtils.removeTab(tab2);
});
