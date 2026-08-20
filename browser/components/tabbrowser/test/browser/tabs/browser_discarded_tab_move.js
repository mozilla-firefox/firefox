/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

/**
 * Tests that the "discarded" attribute is properly preserved when tabs
 * are moved between windows via swapBrowsersAndCloseOther and adoptTab.
 * This is a regression test for Bug 1984922.
 */

add_task(async function test_discarded_tab_swapBrowsersAndCloseOther() {
  info("Testing discarded attribute preservation in swapBrowsersAndCloseOther");

  // Create a tab and discard it
  let discardedTab = BrowserTestUtils.addTab(gBrowser, "https://example.com");

  // Make sure the tab is loaded first
  await BrowserTestUtils.browserLoaded(discardedTab.linkedBrowser);

  // Discard the tab to set the "discarded" attribute
  await gBrowser.prepareDiscardBrowser(discardedTab);
  gBrowser.discardBrowser(discardedTab, true);

  ok(
    discardedTab.hasAttribute("discarded"),
    "Tab should have discarded attribute"
  );

  // Create a new tab to swap into
  let newTab = BrowserTestUtils.addTab(gBrowser, "about:blank");

  // Wait for TabAttrModified event to ensure the attribute is transferred
  let attrChangePromise = BrowserTestUtils.waitForEvent(
    newTab,
    "TabAttrModified",
    false,
    event => {
      return event.detail.changed.includes("discarded");
    }
  );

  // Swap the browsers
  let swapSuccess = gBrowser.swapBrowsersAndCloseOther(newTab, discardedTab);
  ok(swapSuccess, "swapBrowsersAndCloseOther should succeed");

  // Wait for the attribute change
  await attrChangePromise;

  // Verify the discarded attribute was transferred
  ok(
    newTab.hasAttribute("discarded"),
    "New tab should have the discarded attribute after swap"
  );

  // Clean up
  BrowserTestUtils.removeTab(newTab);
});

add_task(async function test_discarded_tab_adoptTab() {
  info("Testing discarded attribute preservation in adoptTab");

  // Open a new window
  let newWin = await BrowserTestUtils.openNewBrowserWindow();

  // Create and discard a tab in the original window
  let discardedTab = BrowserTestUtils.addTab(
    gBrowser,
    "https://example.com/test"
  );

  // Make sure the tab is loaded first
  await BrowserTestUtils.browserLoaded(discardedTab.linkedBrowser);

  // Discard the tab
  await gBrowser.prepareDiscardBrowser(discardedTab);
  gBrowser.discardBrowser(discardedTab, true);

  ok(
    discardedTab.hasAttribute("discarded"),
    "Tab should have discarded attribute"
  );

  // Adopt the tab into the new window
  let adoptedTab = newWin.gBrowser.adoptTab(discardedTab);

  ok(adoptedTab, "adoptTab should succeed");
  ok(
    adoptedTab.hasAttribute("discarded"),
    "Adopted tab should preserve the discarded attribute"
  );

  // Verify the original tab is gone (should be closed by adoptTab)
  ok(
    discardedTab.closing || !discardedTab.parentNode,
    "Original tab should be closed or removed"
  );

  // Clean up
  await BrowserTestUtils.closeWindow(newWin);
});

add_task(async function test_discarded_with_other_attributes() {
  info("Testing discarded attribute with other attributes during swap");

  // Create a tab and set multiple attributes
  let multiAttrTab = BrowserTestUtils.addTab(gBrowser, "https://example.com");

  // Load the tab first
  await BrowserTestUtils.browserLoaded(multiAttrTab.linkedBrowser);

  await gBrowser.prepareDiscardBrowser(multiAttrTab);
  gBrowser.discardBrowser(multiAttrTab, true); // This sets discarded

  // Set another attribute (but do it after discarding the tab, otherwise it
  // will get removed when the tab is discarded)
  multiAttrTab.setAttribute("soundplaying", "true");
  ok(multiAttrTab.hasAttribute("discarded"), "Tab should be discarded");

  // Create target tab for swap
  let targetTab = BrowserTestUtils.addTab(gBrowser, "about:blank");

  // Perform the swap
  let swapSuccess = gBrowser.swapBrowsersAndCloseOther(targetTab, multiAttrTab);
  ok(swapSuccess, "Swap should succeed");

  // Verify both attributes were transferred
  ok(
    targetTab.hasAttribute("discarded"),
    "Target tab should have discarded attribute"
  );
  ok(
    targetTab.hasAttribute("soundplaying"),
    "Target tab should have soundplaying attribute"
  );

  // Clean up
  BrowserTestUtils.removeTab(targetTab);
});
