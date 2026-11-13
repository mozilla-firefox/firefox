/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { TelemetryTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryTestUtils.sys.mjs"
);

const SCALAR_NAME = "browser.ui.customized_widgets";
let initialCounts;
const bookmarksInfo = [
  {
    title: "firefox",
    url: "https://example.com",
  },
  {
    title: "rules",
    url: "https://example.com/2",
  },
  {
    title: "yo",
    url: "https://example.com/2",
  },
];

// Setup.
add_task(async function test_bookmarks_toolbar_telemetry() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.toolbars.bookmarks.visibility", "newtab"]],
  });

  // This is added during startup
  await TestUtils.waitForCondition(
    () =>
      keyedScalarExists(
        "browser.ui.toolbar_widgets",
        "bookmarks-bar_pinned_newtab",
        true
      ),
    `Waiting for "bookmarks-bar_pinned_newtab" to appear in Telemetry snapshot`
  );
  ok(true, `"bookmarks-bar_pinned_newtab"=true found in Telemetry`);

  // Keyed scalars aren't cleared between tests, so count from the values
  // already recorded, e.g. by a previous run of this test.
  initialCounts =
    Services.telemetry.getSnapshotForKeyedScalars("main", false).parent[
      SCALAR_NAME
    ] ?? {};

  await changeToolbarVisibilityViaContextMenu("never");
  await assertUIChange(
    "bookmarks-bar_move_newtab_never_toolbar-context-menu",
    1
  );

  await changeToolbarVisibilityViaContextMenu("newtab");
  await assertUIChange(
    "bookmarks-bar_move_never_newtab_toolbar-context-menu",
    1
  );

  await changeToolbarVisibilityViaContextMenu("always");
  await assertUIChange(
    "bookmarks-bar_move_newtab_always_toolbar-context-menu",
    1
  );

  // Extra windows are opened to make sure telemetry numbers aren't
  // double counted since there will be multiple instances of the
  // bookmarks toolbar.
  let extraWindows = [];
  extraWindows.push(await BrowserTestUtils.openNewBrowserWindow());

  Services.telemetry.getSnapshotForScalars("main", true);
  let bookmarks = await PlacesUtils.bookmarks.insertTree({
    guid: PlacesUtils.bookmarks.toolbarGuid,
    children: bookmarksInfo,
  });
  registerCleanupFunction(() => PlacesUtils.bookmarks.remove(bookmarks));
  TelemetryTestUtils.assertScalar(
    TelemetryTestUtils.getProcessScalars("parent"),
    "browser.engagement.bookmarks_toolbar_bookmark_added",
    3,
    "Bookmarks added value should be 3"
  );

  let newtab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  registerCleanupFunction(() => BrowserTestUtils.removeTab(newtab));
  let bookmarkToolbarButton = document.querySelector(
    "#PlacesToolbarItems > toolbarbutton"
  );
  bookmarkToolbarButton.click();
  TelemetryTestUtils.assertScalar(
    TelemetryTestUtils.getProcessScalars("parent"),
    "browser.engagement.bookmarks_toolbar_bookmark_opened",
    1,
    "Bookmarks opened value should be 1"
  );

  extraWindows.push(await BrowserTestUtils.openNewBrowserWindow());
  extraWindows.push(await BrowserTestUtils.openNewBrowserWindow());

  // Simulate dragging a bookmark within the toolbar to ensure
  // that the bookmarks_toolbar_bookmark_added probe doesn't increment
  let srcElement = document.querySelector("#PlacesToolbar .bookmark-item");
  let destElement = document.querySelector("#PlacesToolbar");
  await EventUtils.synthesizePlainDragAndDrop({
    srcElement,
    destElement,
    srcX: 5,
    srcY: 5,
  });

  TelemetryTestUtils.assertScalar(
    TelemetryTestUtils.getProcessScalars("parent"),
    "browser.engagement.bookmarks_toolbar_bookmark_added",
    3,
    "Bookmarks added value should still be 3"
  );

  for (let win of extraWindows) {
    await BrowserTestUtils.closeWindow(win);
  }

  // Popping the pref env doesn't update the toolbar state that
  // BrowserUsageTelemetry remembers, so restore it through the UI.
  await changeToolbarVisibilityViaContextMenu("newtab");
});

async function changeToolbarVisibilityViaContextMenu(nextState) {
  let contextMenu = document.querySelector("#toolbar-context-menu");
  let popupShown = BrowserTestUtils.waitForEvent(contextMenu, "popupshown");
  let menuButton = document.getElementById("PanelUI-menu-button");
  EventUtils.synthesizeMouseAtCenter(
    menuButton,
    { type: "contextmenu" },
    window
  );
  await popupShown;
  let bookmarksToolbarMenu = document.querySelector("#toggle_PersonalToolbar");
  let subMenu = bookmarksToolbarMenu.querySelector("menupopup");
  popupShown = BrowserTestUtils.waitForEvent(subMenu, "popupshown");
  bookmarksToolbarMenu.openMenu(true);
  await popupShown;
  let menuItem = document.querySelector(
    `menuitem[data-visibility-enum="${nextState}"]`
  );
  subMenu.activateItem(menuItem);
  contextMenu.hidePopup();
}

async function assertUIChange(key, value) {
  value += initialCounts[key] ?? 0;
  await TestUtils.waitForCondition(
    () => keyedScalarExists(SCALAR_NAME, key, value),
    `Waiting for ${key} to appear in Telemetry snapshot`
  );
  ok(true, `${key}=${value} found in Telemetry`);
}

function keyedScalarExists(scalar, key, value) {
  let snapshot = Services.telemetry.getSnapshotForKeyedScalars(
    "main",
    false
  ).parent;
  if (!snapshot.hasOwnProperty(scalar)) {
    return false;
  }
  if (!snapshot[scalar].hasOwnProperty(key)) {
    info(`Looking for ${key} in ${JSON.stringify(snapshot[scalar])}`);
    return false;
  }
  return snapshot[scalar][key] == value;
}
