/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TRACKING_PAGE =
  // eslint-disable-next-line sdl/no-insecure-url
  "http://example.org/browser/browser/base/content/test/browser-protectionsUI/trackingPage.html";
const FP_PROTECTION_PREF = "privacy.trackingprotection.fingerprinting.enabled";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "urlclassifier.features.fingerprinting.blacklistHosts",
        "fingerprinting.example.com",
      ],
      [
        "urlclassifier.features.fingerprinting.annotate.blacklistHosts",
        "fingerprinting.example.com",
      ],
      ["privacy.trackingprotection.enabled", false],
      ["privacy.trackingprotection.annotate_channels", false],
      ["privacy.trackingprotection.cryptomining.enabled", false],
      ["urlclassifier.features.cryptomining.annotate.blacklistHosts", ""],
      ["urlclassifier.features.cryptomining.annotate.blacklistTables", ""],
    ],
  });
  await UrlClassifierTestUtils.addTestHostsFromPrefs();
  registerCleanupFunction(async () => {
    await UrlClassifierTestUtils.cleanupTestTrackers();
    Services.fog.testResetFOG();
  });
});

async function testIdentityState(hasException) {
  Services.fog.testResetFOG();
  let promise = BrowserTestUtils.openNewForegroundTab({
    url: TRACKING_PAGE,
    gBrowser,
  });
  let [tab] = await Promise.all([promise, waitForContentBlockingEvent()]);

  if (hasException) {
    let loaded = BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false,
      TRACKING_PAGE
    );
    gProtectionsHandler.disableForCurrentPage();
    await loaded;
  }

  await openProtectionsPanel();

  ok(
    !gProtectionsHandler._protectionsPopup.hasAttribute("detected"),
    "fingerprinters are not detected"
  );
  ok(
    !BrowserTestUtils.isHidden(gProtectionsHandler.iconBox),
    "icon box is visible regardless the exception"
  );

  promise = waitForContentBlockingEvent();

  await SpecialPowers.spawn(tab.linkedBrowser, [], function () {
    content.postMessage("fingerprinting", "*");
  });

  await promise;

  ok(
    gProtectionsHandler._protectionsPopup.hasAttribute("detected"),
    "trackers are detected"
  );
  ok(
    BrowserTestUtils.isVisible(gProtectionsHandler.iconBox),
    "icon box is visible"
  );
  is(
    gProtectionsHandler.iconBox.hasAttribute("hasException"),
    hasException,
    "Shows an exception when appropriate"
  );

  if (hasException) {
    let loaded = BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false,
      TRACKING_PAGE
    );
    gProtectionsHandler.enableForCurrentPage();
    await loaded;
  }

  let loads = hasException ? 3 : 1;
  testTelemetry(loads, 1, hasException);

  BrowserTestUtils.removeTab(tab);
}

async function testCategoryItem() {
  Services.prefs.setBoolPref(FP_PROTECTION_PREF, false);
  let promise = BrowserTestUtils.openNewForegroundTab({
    url: TRACKING_PAGE,
    gBrowser,
  });
  let [tab] = await Promise.all([promise, waitForContentBlockingEvent()]);

  await openProtectionsPanel();
  let categoryItem = document.getElementById(
    "protections-popup-category-fingerprinters"
  );

  ok(
    !categoryItem.classList.contains("blocked"),
    "Category not marked as blocked"
  );
  ok(
    categoryItem.classList.contains("notFound"),
    "Category marked as not found"
  );
  Services.prefs.setBoolPref(FP_PROTECTION_PREF, true);
  ok(categoryItem.classList.contains("blocked"), "Category marked as blocked");
  ok(
    categoryItem.classList.contains("notFound"),
    "Category marked as not found"
  );
  Services.prefs.setBoolPref(FP_PROTECTION_PREF, false);
  ok(
    !categoryItem.classList.contains("blocked"),
    "Category not marked as blocked"
  );
  ok(
    categoryItem.classList.contains("notFound"),
    "Category marked as not found"
  );
  await closeProtectionsPanel();

  promise = waitForContentBlockingEvent();

  await SpecialPowers.spawn(tab.linkedBrowser, [], function () {
    content.postMessage("fingerprinting", "*");
  });

  await promise;

  await openProtectionsPanel();
  ok(
    !categoryItem.classList.contains("blocked"),
    "Category not marked as blocked"
  );
  ok(
    !categoryItem.classList.contains("notFound"),
    "Category not marked as not found"
  );
  Services.prefs.setBoolPref(FP_PROTECTION_PREF, true);
  ok(categoryItem.classList.contains("blocked"), "Category marked as blocked");
  ok(
    !categoryItem.classList.contains("notFound"),
    "Category not marked as not found"
  );
  Services.prefs.setBoolPref(FP_PROTECTION_PREF, false);
  ok(
    !categoryItem.classList.contains("blocked"),
    "Category not marked as blocked"
  );
  ok(
    !categoryItem.classList.contains("notFound"),
    "Category not marked as not found"
  );
  await closeProtectionsPanel();

  BrowserTestUtils.removeTab(tab);
}

async function testSubview(hasException) {
  Services.fog.testResetFOG();
  let promise = BrowserTestUtils.openNewForegroundTab({
    url: TRACKING_PAGE,
    gBrowser,
  });
  let [tab] = await Promise.all([promise, waitForContentBlockingEvent()]);

  if (hasException) {
    let loaded = BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false,
      TRACKING_PAGE
    );
    gProtectionsHandler.disableForCurrentPage();
    await loaded;
  }

  promise = waitForContentBlockingEvent();
  await SpecialPowers.spawn(tab.linkedBrowser, [], function () {
    content.postMessage("fingerprinting", "*");
  });
  await promise;

  await openProtectionsPanel();

  let categoryItem = document.getElementById(
    "protections-popup-category-fingerprinters"
  );

  // Explicitly waiting for the category item becoming visible.
  await TestUtils.waitForCondition(() => {
    return BrowserTestUtils.isVisible(categoryItem);
  });

  ok(BrowserTestUtils.isVisible(categoryItem), "TP category item is visible");

  /* eslint-disable mozilla/no-arbitrary-setTimeout */
  // We have to wait until the ContentBlockingLog gets updated in the content.
  // Unfortunately, we need to use the setTimeout here since we don't have an
  // easy to know whether the log is updated in the content. This should be
  // removed after the log been removed in the content (Bug 1599046).
  await new Promise(resolve => {
    setTimeout(resolve, 500);
  });
  /* eslint-enable mozilla/no-arbitrary-setTimeout */

  let subview = document.getElementById("protections-popup-fingerprintersView");
  let viewShown = BrowserTestUtils.waitForEvent(subview, "ViewShown");
  categoryItem.click();
  await viewShown;

  let trackersViewShimHint = document.getElementById(
    "protections-popup-fingerprintersView-shim-allow-hint"
  );
  ok(trackersViewShimHint.hidden, "Shim hint is hidden");

  let listItems = subview.querySelectorAll(".protections-popup-list-item");
  is(listItems.length, 1, "We have 1 item in the list");
  let listItem = listItems[0];
  ok(BrowserTestUtils.isVisible(listItem), "List item is visible");
  is(
    listItem.querySelector("label").value,
    "https://fingerprinting.example.com",
    "Has the correct host"
  );
  is(
    listItem.classList.contains("allowed"),
    hasException,
    "Indicates the fingerprinter was blocked or allowed"
  );

  let mainView = document.getElementById("protections-popup-mainView");
  viewShown = BrowserTestUtils.waitForEvent(mainView, "ViewShown");
  let backButton = subview.querySelector(".subviewbutton-back");
  backButton.click();
  await viewShown;

  ok(true, "Main view was shown");

  if (hasException) {
    let loaded = BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false,
      TRACKING_PAGE
    );
    gProtectionsHandler.enableForCurrentPage();
    await loaded;
  }

  let loads = hasException ? 3 : 1;
  testTelemetry(loads, 1, hasException);

  BrowserTestUtils.removeTab(tab);
}

// testGetValue() returns null rather than 0 for a label that never recorded, so
// coalesce to keep a miscount reported as a number rather than a TypeError.
function assertLabelCount(label, expectedCount, message) {
  let count =
    Glean.contentblocking.fingerprintersBlockedCount[label].testGetValue();
  Assert.equal(count ?? 0, expectedCount, message);
}

function testTelemetry(pagesVisited, pagesWithBlockableContent, hasException) {
  assertLabelCount(
    "pageLoad",
    pagesVisited,
    "The correct number of page loads have been recorded"
  );
  assertLabelCount(
    hasException ? "allowed" : "blocked",
    pagesWithBlockableContent,
    "The correct number of fingerprinters have been recorded as blocked or allowed."
  );
}

add_task(async function test() {
  Services.prefs.setBoolPref(FP_PROTECTION_PREF, true);

  await testIdentityState(false);
  await testIdentityState(true);

  await testSubview(false);
  await testSubview(true);

  await testCategoryItem();

  Services.prefs.clearUserPref(FP_PROTECTION_PREF);
});
