/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that the unified search button gets an "add engine" badge when the
 * current page offers an opensearch engine, and that the address bar's button
 * doesn't carry one while the search bar is in the toolbar.
 */

// This page defines multiple opensearch engines.
const ENGINE_TEST_URL =
  "http://mochi.test:8888/browser/browser/components/search/test/browser/opensearch.html";

add_task(async function test_badge() {
  let switcherButton = document.querySelector(
    "#searchbar-new .searchmode-switcher"
  );
  let urlbarSwitcherButton = document.querySelector(
    "#urlbar .searchmode-switcher"
  );

  let offeringTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    ENGINE_TEST_URL
  );
  await TestUtils.waitForCondition(
    () => switcherButton.hasAttribute("addengines"),
    "Badge should appear when the page offers engines."
  );
  Assert.ok(
    !urlbarSwitcherButton.hasAttribute("addengines"),
    "The addressbar's button should not be badged while it only appears on focus."
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
  Assert.ok(
    switcherButton.hasAttribute("addengines"),
    "Badge should reappear when switching back to the offering page."
  );

  BrowserTestUtils.removeTab(blankTab);
  BrowserTestUtils.removeTab(offeringTab);
});
