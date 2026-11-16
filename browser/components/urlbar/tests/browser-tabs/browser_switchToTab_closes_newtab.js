/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This tests that switch to tab from a blank tab switches and then closes
 * the blank tab.
 */

"use strict";

add_task(async function test_switchToTab_closes() {
  registerCleanupFunction(PlacesUtils.history.clear);

  let testURL =
    "http://example.org/browser/browser/components/urlbar/tests/browser/dummy_page.html";

  // Open the base tab
  let baseTab = await BrowserTestUtils.openNewForegroundTab(gBrowser, testURL);

  if (baseTab.linkedBrowser.currentURI.spec == "about:blank") {
    return;
  }

  // Open a blank tab to start the test from.
  let testTab = await BrowserTestUtils.openNewForegroundTab(gBrowser);

  // Functions for TabClose and TabSelect
  let tabClosePromise = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "TabClose",
    false,
    event => {
      return event.originalTarget == testTab;
    }
  );
  let tabSelectPromise = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "TabSelect",
    false,
    event => {
      return event.originalTarget == baseTab;
    }
  );

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "dummy",
  });

  // The first result is the heuristic, the second will be the switch to tab.
  let element = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1);
  EventUtils.synthesizeMouseAtCenter(element, {}, window);

  await Promise.all([tabSelectPromise, tabClosePromise]);

  // Confirm that the selected tab is now the base tab
  Assert.equal(
    gBrowser.selectedTab,
    baseTab,
    "Should have switched to the correct tab"
  );

  gBrowser.removeTab(baseTab);
});
