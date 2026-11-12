/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Checks that CustomizableUI reports the expected collapsed toolbar IDs.
 *
 * Note: on macOS, expectations for CustomizableUI.AREA_MENUBAR are
 * automatically skipped since that area isn't available on that platform.
 *
 * @param {string[]} The IDs of the expected collapsed toolbars.
 */
function assertCollapsedToolbarIds(expected) {
  if (AppConstants.platform == "macosx") {
    let menubarIndex = expected.indexOf(CustomizableUI.AREA_MENUBAR);
    if (menubarIndex != -1) {
      expected.splice(menubarIndex, 1);
    }
  }

  let collapsedIds = CustomizableUI.getCollapsedToolbarIds(window);
  Assert.equal(collapsedIds.size, expected.length);
  for (let expectedId of expected) {
    Assert.ok(
      collapsedIds.has(expectedId),
      `${expectedId} should be collapsed`
    );
  }
}

registerCleanupFunction(async () => {
  await CustomizableUI.reset();
});

/**
 * Tests that CustomizableUI.getCollapsedToolbarIds will return the IDs of
 * toolbars that are collapsed, or menubars that are autohidden.
 */
add_task(async function test_toolbar_collapsed_states() {
  // By default, we expect the menubar and the bookmarks toolbar to be
  // collapsed.
  assertCollapsedToolbarIds([
    CustomizableUI.AREA_BOOKMARKS,
    CustomizableUI.AREA_MENUBAR,
  ]);

  let bookmarksToolbar = document.getElementById(CustomizableUI.AREA_BOOKMARKS);
  // Make sure we're configured to show the bookmarks toolbar on about:newtab.
  setToolbarVisibility(bookmarksToolbar, "newtab");

  // Without a preloaded about:newtab browser, the tab is still on about:blank
  // when it gets selected, and the toolbar only shows once it navigates.
  let toolbarShown = BrowserTestUtils.waitForEvent(
    bookmarksToolbar,
    "toolbarvisibilitychange"
  );
  let newTab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "about:newtab",
    waitForLoad: false,
  });
  await toolbarShown;
  // Now that we've opened about:newtab, the bookmarks toolbar should now
  // be visible.
  assertCollapsedToolbarIds([CustomizableUI.AREA_MENUBAR]);
  await BrowserTestUtils.removeTab(newTab);

  // And with about:newtab closed again, the bookmarks toolbar should be
  // reported as collapsed.
  assertCollapsedToolbarIds([
    CustomizableUI.AREA_BOOKMARKS,
    CustomizableUI.AREA_MENUBAR,
  ]);

  // Make sure we're configured to show the bookmarks toolbar on about:newtab.
  setToolbarVisibility(bookmarksToolbar, "always");
  assertCollapsedToolbarIds([CustomizableUI.AREA_MENUBAR]);

  setToolbarVisibility(bookmarksToolbar, "never");
  assertCollapsedToolbarIds([
    CustomizableUI.AREA_BOOKMARKS,
    CustomizableUI.AREA_MENUBAR,
  ]);

  if (AppConstants.platform != "macosx") {
    // We'll still consider the menubar collapsed by default, even if it's being temporarily
    // shown via the alt key.
    let menubarActive = BrowserTestUtils.waitForEvent(
      window,
      "DOMMenuBarActive"
    );
    EventUtils.synthesizeKey("VK_ALT", {});
    await menubarActive;
    assertCollapsedToolbarIds([
      CustomizableUI.AREA_BOOKMARKS,
      CustomizableUI.AREA_MENUBAR,
    ]);
    let menubarInactive = BrowserTestUtils.waitForEvent(
      window,
      "DOMMenuBarInactive"
    );
    EventUtils.synthesizeKey("VK_ESCAPE", {});
    await menubarInactive;
    assertCollapsedToolbarIds([
      CustomizableUI.AREA_BOOKMARKS,
      CustomizableUI.AREA_MENUBAR,
    ]);

    let menubar = document.getElementById(CustomizableUI.AREA_MENUBAR);
    setToolbarVisibility(menubar, true);
    assertCollapsedToolbarIds([CustomizableUI.AREA_BOOKMARKS]);
    setToolbarVisibility(menubar, false);
    assertCollapsedToolbarIds([
      CustomizableUI.AREA_BOOKMARKS,
      CustomizableUI.AREA_MENUBAR,
    ]);
  }
});
