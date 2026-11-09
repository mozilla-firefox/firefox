/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Verifies that the trust panel/toolbar's blocked-tracker count and the
 * <privacy-metrics-card> shown on about:protections agree right after
 * visiting a single tracking page.
 * (The former counts trackers on a single page, the latter over all pages
 * in the past seven days — so visiting a single page should cause both to
 * increase by the same number.)
 *
 * This lives in its own file, with no other tests, on purpose:
 * TrackingDBService.sumAllEvents() flushes every live window's pending
 * content-blocking events to the database, so that we can assume that
 * subsequent events are caused by the trackers triggered by the test.
 * However, if a tab closed by an earlier test still is mid-teardown,
 * it could still throw new events after the sumAllEvents call,
 * which would then be counted by the <privacy-metrics-card>.
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  BrowserTestUtils: "resource://testing-common/BrowserTestUtils.sys.mjs",
  UrlClassifierTestUtils:
    "resource://testing-common/UrlClassifierTestUtils.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  this,
  "TrackingDBService",
  "@mozilla.org/tracking-db-service;1",
  Ci.nsITrackingDBService
);

const TRACKING_PAGE =
  // eslint-disable-next-line sdl/no-insecure-url
  "http://tracking.example.org/browser/browser/base/content/test/browser-protectionsUI/trackingPage.html";

function trustIconContainer() {
  return document.getElementById("trust-icon-container");
}

async function waitForTrustIconClass(className, message) {
  await TestUtils.waitForCondition(
    () => trustIconContainer()?.classList.contains(className),
    message,
    100,
    100
  );
}

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.trustPanel.featureGate", true],
      [
        "urlclassifier.features.cryptomining.blacklistHosts",
        "cryptomining.example.com",
      ],
      [
        "urlclassifier.features.cryptomining.annotate.blacklistHosts",
        "cryptomining.example.com",
      ],
      // trackingPage.html contains a static <iframe src="http://trackertest.org/">.
      // Exclude it from being counted as a tracker so it doesn't affect the
      // counts this test compares below.
      ["urlclassifier.trackingSkipURLs", "*://trackertest.org/*"],
      ["urlclassifier.trackingAnnotationSkipURLs", "*://trackertest.org/*"],
    ],
  });
  await UrlClassifierTestUtils.addTestHostsFromPrefs();
  registerCleanupFunction(async () => {
    await UrlClassifierTestUtils.cleanupTestTrackers();
  });
});

add_task(
  async function test_toolbar_count_matches_privacy_metrics_card_on_first_visit() {
    const baseline = await TrackingDBService.sumAllEvents();

    await SpecialPowers.pushPrefEnv({
      set: [["browser.contentblocking.report.privacy_metrics.enabled", true]],
    });

    const tab = await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      opening: TRACKING_PAGE,
      waitForLoad: true,
    });

    try {
      await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
        // See trackingAPI.js - this postMessage causes it to inject an iframe with
        // one of the blocked tracking hosts:
        content.postMessage("cryptomining", "*");
      });

      await waitForTrustIconClass(
        "has-blocked-trackers",
        "Waiting for has-blocked-trackers after a cryptominer is blocked"
      );

      const toolbarCount = Number(
        document.getElementById("trust-icon-tracker-count-shortform")
          .textContent
      );
      Assert.greater(
        toolbarCount,
        0,
        "Toolbar count is positive after a tracker is blocked"
      );

      const protectionsTab = await BrowserTestUtils.openNewForegroundTab({
        gBrowser,
        url: "about:protections",
      });
      try {
        const cardTotal = await SpecialPowers.spawn(
          protectionsTab.linkedBrowser,
          [],
          async () => {
            const card = content.document.querySelector("privacy-metrics-card");
            await ContentTaskUtils.waitForCondition(
              () => card.hasAttribute("total"),
              "Waiting for the privacy metrics card to load its stats"
            );
            return Number(card.getAttribute("total"));
          }
        );

        Assert.equal(
          cardTotal - baseline,
          toolbarCount,
          "about:protections' privacy metrics card total increased by " +
            "exactly the toolbar's tracker count after the first visit to " +
            "a tracking page"
        );
      } finally {
        await BrowserTestUtils.removeTab(protectionsTab);
      }
    } finally {
      await BrowserTestUtils.removeTab(tab);
    }
  }
);
