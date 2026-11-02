/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TRACKING_PAGE =
  // eslint-disable-next-line sdl/no-insecure-url
  "http://example.com/browser/browser/base/content/test/browser-protectionsUI/trackingPage.html";

const ST_PROTECTION_PREF = "privacy.trackingprotection.socialtracking.enabled";
const ST_BLOCK_COOKIES_PREF = "privacy.socialtracking.block_cookies.enabled";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      [ST_BLOCK_COOKIES_PREF, true],
      [
        "urlclassifier.features.socialtracking.blacklistHosts",
        "social-tracking.example.org",
      ],
      [
        "urlclassifier.features.socialtracking.annotate.blacklistHosts",
        "social-tracking.example.org",
      ],
      // Whitelist trackertest.org loaded by default in trackingPage.html
      ["urlclassifier.trackingSkipURLs", "*://trackertest.org/*"],
      ["urlclassifier.trackingAnnotationSkipURLs", "*://trackertest.org/*"],
      ["privacy.trackingprotection.enabled", false],
      ["privacy.trackingprotection.annotate_channels", true],
    ],
  });
  await UrlClassifierTestUtils.addTestHostsFromPrefs();
  registerCleanupFunction(async () => {
    await UrlClassifierTestUtils.cleanupTestTrackers();
  });
});

async function testIdentityState(hasException) {
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
  let categoryItem = document.getElementById(
    "protections-popup-category-socialblock"
  );

  ok(
    categoryItem.classList.contains("notFound"),
    "socialtrackings are not detected"
  );

  ok(
    BrowserTestUtils.isVisible(gProtectionsHandler.iconBox),
    "icon box is visible regardless the exception"
  );
  await closeProtectionsPanel();

  await SpecialPowers.spawn(tab.linkedBrowser, [], function () {
    content.postMessage("socialtracking", "*");
  });
  await openProtectionsPanel();

  await TestUtils.waitForCondition(() => {
    return !categoryItem.classList.contains("notFound");
  });

  ok(
    gProtectionsHandler._protectionsPopup.hasAttribute("detected"),
    "trackers are detected"
  );
  ok(
    !categoryItem.classList.contains("notFound"),
    "social trackers are detected"
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
  await closeProtectionsPanel();

  if (hasException) {
    let loaded = BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false,
      TRACKING_PAGE
    );
    gProtectionsHandler.enableForCurrentPage();
    await loaded;
  }

  BrowserTestUtils.removeTab(tab);
}

async function testSubview(hasException) {
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
    content.postMessage("socialtracking", "*");
  });
  await promise;

  await openProtectionsPanel();

  let categoryItem = document.getElementById(
    "protections-popup-category-socialblock"
  );

  // Explicitly waiting for the category item becoming visible.
  await TestUtils.waitForCondition(() => {
    return BrowserTestUtils.isVisible(categoryItem);
  });

  ok(BrowserTestUtils.isVisible(categoryItem), "STP category item is visible");
  ok(
    categoryItem.classList.contains("blocked"),
    "STP category item is blocked"
  );

  /* eslint-disable mozilla/no-arbitrary-setTimeout */
  // We have to wait until the ContentBlockingLog gets updated in the content.
  // Unfortunately, we need to use the setTimeout here since we don't have an
  // easy to know whether the log is updated in the content. This should be
  // removed after the log been removed in the content (Bug 1599046).
  await new Promise(resolve => {
    setTimeout(resolve, 500);
  });
  /* eslint-enable mozilla/no-arbitrary-setTimeout */

  let subview = document.getElementById("protections-popup-socialblockView");
  let viewShown = BrowserTestUtils.waitForEvent(subview, "ViewShown");
  categoryItem.click();
  await viewShown;

  let trackersViewShimHint = document.getElementById(
    "protections-popup-socialblockView-shim-allow-hint"
  );
  ok(trackersViewShimHint.hidden, "Shim hint is hidden");

  let listItems = subview.querySelectorAll(".protections-popup-list-item");
  is(listItems.length, 1, "We have 1 item in the list");
  let listItem = listItems[0];
  ok(BrowserTestUtils.isVisible(listItem), "List item is visible");
  is(
    listItem.querySelector("label").value,
    "https://social-tracking.example.org",
    "Has the correct host"
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

  BrowserTestUtils.removeTab(tab);
}

async function testCategoryItem(blockLoads) {
  if (blockLoads) {
    Services.prefs.setBoolPref(ST_PROTECTION_PREF, true);
  }

  Services.prefs.setBoolPref(ST_BLOCK_COOKIES_PREF, false);

  let promise = BrowserTestUtils.openNewForegroundTab({
    url: TRACKING_PAGE,
    gBrowser,
  });
  let [tab] = await Promise.all([promise, waitForContentBlockingEvent()]);

  await openProtectionsPanel();

  let categoryItem = document.getElementById(
    "protections-popup-category-socialblock"
  );

  let noTrackersDetectedDesc = document.getElementById(
    "protections-popup-no-trackers-found-description"
  );

  ok(categoryItem.hasAttribute("uidisabled"), "Category should be uidisabled");

  ok(
    !categoryItem.classList.contains("blocked"),
    "Category not marked as blocked"
  );
  ok(!BrowserTestUtils.isVisible(categoryItem), "Item should be hidden");
  ok(
    !gProtectionsHandler._protectionsPopup.hasAttribute("detected"),
    "trackers are not detected"
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], function () {
    content.postMessage("socialtracking", "*");
  });

  ok(
    !categoryItem.classList.contains("blocked"),
    "Category not marked as blocked"
  );
  ok(!BrowserTestUtils.isVisible(categoryItem), "Item should be hidden");
  ok(
    !gProtectionsHandler._protectionsPopup.hasAttribute("detected"),
    "trackers are not detected"
  );
  ok(
    BrowserTestUtils.isVisible(noTrackersDetectedDesc),
    "No Trackers detected should be shown"
  );

  BrowserTestUtils.removeTab(tab);

  Services.prefs.setBoolPref(ST_BLOCK_COOKIES_PREF, true);

  promise = BrowserTestUtils.openNewForegroundTab({
    url: TRACKING_PAGE,
    gBrowser,
  });
  [tab] = await Promise.all([promise, waitForContentBlockingEvent()]);

  await openProtectionsPanel();

  ok(!categoryItem.hasAttribute("uidisabled"), "Item shouldn't be uidisabled");

  ok(categoryItem.classList.contains("blocked"), "Category marked as blocked");
  ok(
    categoryItem.classList.contains("notFound"),
    "Category marked as not found"
  );
  // At this point we should still be showing "No Trackers Detected"
  ok(!BrowserTestUtils.isVisible(categoryItem), "Item should not be visible");
  ok(
    BrowserTestUtils.isVisible(noTrackersDetectedDesc),
    "No Trackers detected should be shown"
  );
  ok(
    !gProtectionsHandler._protectionsPopup.hasAttribute("detected"),
    "trackers are not detected"
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], function () {
    content.postMessage("socialtracking", "*");
  });

  await TestUtils.waitForCondition(() => {
    return !categoryItem.classList.contains("notFound");
  });

  ok(categoryItem.classList.contains("blocked"), "Category marked as blocked");
  ok(
    !categoryItem.classList.contains("notFound"),
    "Category not marked as not found"
  );
  ok(BrowserTestUtils.isVisible(categoryItem), "Item should be visible");
  ok(
    !BrowserTestUtils.isVisible(noTrackersDetectedDesc),
    "No Trackers detected should be hidden"
  );
  ok(
    gProtectionsHandler._protectionsPopup.hasAttribute("detected"),
    "trackers are not detected"
  );

  BrowserTestUtils.removeTab(tab);

  Services.prefs.clearUserPref(ST_PROTECTION_PREF);
}

add_task(async function testIdentityUI() {
  requestLongerTimeout(2);

  await testIdentityState(false);
  await testIdentityState(true);

  await testSubview(false);
  await testSubview(true);

  await testCategoryItem(false);
  await testCategoryItem(true);
});
