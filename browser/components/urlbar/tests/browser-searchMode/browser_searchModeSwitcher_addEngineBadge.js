/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test the address bar's "add engine" badge.
 */

ChromeUtils.defineESModuleGetters(this, {
  OpenSearchManager:
    "moz-src:///browser/components/search/OpenSearchManager.sys.mjs",
  SearchUIUtils: "moz-src:///browser/components/search/SearchUIUtils.sys.mjs",
});

// This page defines multiple opensearch engines.
const ENGINE_TEST_URL =
  "http://mochi.test:8888/browser/browser/components/search/test/browser/opensearch.html";

const BADGE_SHOWN_COUNT_PREF = "browser.urlbar.addEnginesBadgeShownCount";

function clearBadgeShownCount() {
  Cc["@mozilla.org/content-pref/service;1"]
    .getService(Ci.nsIContentPrefService2)
    .removeByName(BADGE_SHOWN_COUNT_PREF, null);
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.unifiedSearchButton.always", true]],
  });
  clearBadgeShownCount();
  registerCleanupFunction(clearBadgeShownCount);
});

add_task(async function test_badge_in_the_address_bar() {
  let switcherButton = document.querySelector("#urlbar .searchmode-switcher");

  let offeringTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    ENGINE_TEST_URL
  );
  await TestUtils.waitForCondition(
    () => switcherButton.hasAttribute("addengines"),
    "The addressbar's button is badged when its button is always shown."
  );

  let blankTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );
  Assert.ok(
    !switcherButton.hasAttribute("addengines"),
    "Badge should be hidden on a page that offers no engines."
  );

  await BrowserTestUtils.switchTab(gBrowser, offeringTab);
  await TestUtils.waitForCondition(
    () => switcherButton.hasAttribute("addengines"),
    "Badge should reappear when switching back to the offering page."
  );

  BrowserTestUtils.removeTab(blankTab);
  BrowserTestUtils.removeTab(offeringTab);
  clearBadgeShownCount();
});

add_task(async function test_badge_suppressed_when_installing() {
  // Fail the install, as when an engine with the same name already exists.
  let stub = sinon.stub(SearchUIUtils, "addOpenSearchEngine").resolves(false);

  let switcherButton = document.querySelector("#urlbar .searchmode-switcher");

  let offeringTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    ENGINE_TEST_URL
  );
  await TestUtils.waitForCondition(
    () => switcherButton.hasAttribute("addengines"),
    "The addressbar's button is badged."
  );

  let popup = await UrlbarTestUtils.openSearchModeSwitcher(window);
  let item = popup.querySelector("panel-item[data-engine-name=engine1]");
  EventUtils.synthesizeMouseAtCenter(item, {});
  await TestUtils.waitForCondition(
    () => stub.calledOnce,
    "The engine install was attempted."
  );
  Assert.ok(
    !switcherButton.hasAttribute("addengines"),
    "Badge is hidden once the user picks an engine to install."
  );

  await BrowserTestUtils.reloadTab(offeringTab);
  await TestUtils.waitForCondition(
    () =>
      OpenSearchManager.getInstallableEngines(offeringTab.linkedBrowser)
        ?.length,
    "The page offers engines again after reloading."
  );
  await TestUtils.waitForTick();
  Assert.ok(
    !switcherButton.hasAttribute("addengines"),
    "Badge is not shown again for the page after reloading."
  );

  BrowserTestUtils.removeTab(offeringTab);
  stub.restore();
  clearBadgeShownCount();
});
