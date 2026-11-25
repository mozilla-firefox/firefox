/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Main tests for SearchSERPTelemetry - general engine visiting and link clicking.
 */

"use strict";

// Note: example.org is used for the SERP page, and example.com is used to serve
// the ads. This is done to simulate different domains like the real servers.
const TEST_PROVIDER_INFO = [
  {
    telemetryId: "example",
    searchPageRegexp:
      /^https:\/\/example.org\/browser\/browser\/components\/search\/test\/browser\/telemetry\/searchTelemetry(?:Ad)?.html/,
    queryParamNames: ["s"],
    codeParamName: "abc",
    taggedCodes: ["ff"],
    followOnParamNames: ["a"],
    extraAdServersRegexps: [/^https:\/\/example\.com\/ad2?/],
    components: [
      {
        type: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
        default: true,
      },
    ],
  },
];

function getSERPFollowOnUrl(page) {
  return page + "?s=test&abc=ff&a=foo";
}

add_setup(async function () {
  SearchSERPTelemetry.overrideSearchTelemetryForTests(TEST_PROVIDER_INFO);
  await waitForIdle();
  // Enable local telemetry recording for the duration of the tests.
  let oldCanRecord = Services.telemetry.canRecordExtended;
  Services.telemetry.canRecordExtended = true;

  registerCleanupFunction(async () => {
    SearchSERPTelemetry.overrideSearchTelemetryForTests();
    Services.telemetry.canRecordExtended = oldCanRecord;
    resetTelemetry();
  });
});

// The impression these tasks expect is the one recorded when the tab closes
// before the child actor has scanned the page. The scan runs on a timer after
// DOMContentLoaded, which a slow run can fire before the tab closes, so delay
// it past the end of the task.
async function withPageScanPrevented(task) {
  let loadTimeout = Services.ppmm.sharedData.get(
    SEARCH_TELEMETRY_SHARED.LOAD_TIMEOUT
  );
  Services.ppmm.sharedData.set(
    SEARCH_TELEMETRY_SHARED.LOAD_TIMEOUT,
    60 * 60 * 1000
  );
  Services.ppmm.sharedData.flush();
  try {
    await task();
  } finally {
    Services.ppmm.sharedData.set(
      SEARCH_TELEMETRY_SHARED.LOAD_TIMEOUT,
      loadTimeout
    );
    Services.ppmm.sharedData.flush();
  }
}

add_task(async function test_simple_search_page_visit() {
  resetTelemetry();

  await withPageScanPrevented(() =>
    BrowserTestUtils.withNewTab(
      {
        gBrowser,
        url: getSERPUrl("searchTelemetry.html"),
      },
      async () => {
        await assertSearchSourcesTelemetry(
          {},
          {
            "browser.search.content.unknown": { "example:tagged:ff": 1 },
          }
        );
      }
    )
  );

  assertSERPTelemetry([
    {
      impression: {
        has_ai_summary: "unknown",
        shopping_tab_displayed: "unknown",
        prescan: "not_run",
        scan: "not_run",
      },
      abandonment: {
        reason: SearchSERPTelemetryUtils.ABANDONMENTS.TAB_CLOSE,
      },
    },
  ]);
});

add_task(async function test_follow_on_visit() {
  await withPageScanPrevented(() =>
    BrowserTestUtils.withNewTab(
      {
        gBrowser,
        url: getSERPFollowOnUrl(getPageUrl()),
      },
      async () => {
        await assertSearchSourcesTelemetry(
          {},
          {
            "browser.search.content.unknown": {
              "example:tagged:ff": 1,
              "example:tagged-follow-on:ff": 1,
            },
          }
        );
      }
    )
  );
  assertSERPTelemetry([
    {
      impression: {
        has_ai_summary: "unknown",
        shopping_tab_displayed: "unknown",
        prescan: "not_run",
        scan: "not_run",
      },
      abandonment: {
        reason: SearchSERPTelemetryUtils.ABANDONMENTS.TAB_CLOSE,
      },
    },
    {
      impression: {
        has_ai_summary: "unknown",
        shopping_tab_displayed: "unknown",
        prescan: "not_run",
        scan: "not_run",
      },
      abandonment: {
        reason: SearchSERPTelemetryUtils.ABANDONMENTS.TAB_CLOSE,
      },
    },
  ]);
});

add_task(async function test_track_ad() {
  resetTelemetry();

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    getSERPUrl("searchTelemetryAd.html")
  );
  await waitForPageWithAdImpressions();

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": { "example:tagged:ff": 1 },
      "browser.search.withads.unknown": { "example:tagged": 1 },
    }
  );

  assertSERPTelemetry([
    {
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

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_track_ad_organic() {
  resetTelemetry();

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    getSERPUrl("searchTelemetryAd.html", true)
  );
  await waitForPageWithAdImpressions();

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": { "example:organic:none": 1 },
      "browser.search.withads.unknown": { "example:organic": 1 },
    }
  );

  assertSERPTelemetry([
    {
      impression: {
        tagged: "false",
        partner_code: "",
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

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_track_ad_new_window() {
  resetTelemetry();

  let win = await BrowserTestUtils.openNewBrowserWindow();

  let url = getSERPUrl("searchTelemetryAd.html");
  BrowserTestUtils.startLoadingURIString(win.gBrowser.selectedBrowser, url);
  await BrowserTestUtils.browserLoaded(
    win.gBrowser.selectedBrowser,
    false,
    url
  );
  await waitForPageWithAdImpressions();

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": { "example:tagged:ff": 1 },
      "browser.search.withads.unknown": { "example:tagged": 1 },
    }
  );

  assertSERPTelemetry([
    {
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

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_track_ad_pages_without_ads() {
  // Note: the above tests have already checked a page with no ad-urls.
  resetTelemetry();

  let tabs = [];

  tabs.push(
    await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      getSERPUrl("searchTelemetry.html")
    )
  );
  await waitForPageWithAdImpressions();

  tabs.push(
    await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      getSERPUrl("searchTelemetryAd.html")
    )
  );
  await waitForPageWithAdImpressions();

  await assertSearchSourcesTelemetry(
    {},
    {
      "browser.search.content.unknown": { "example:tagged:ff": 2 },
      "browser.search.withads.unknown": { "example:tagged": 1 },
    }
  );

  assertSERPTelemetry([
    {},
    {
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
    BrowserTestUtils.removeTab(tab);
  }
});
