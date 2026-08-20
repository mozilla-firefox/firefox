/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { TelemetryTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryTestUtils.sys.mjs"
);
let bookmarks;
let folder;

add_setup(async function () {
  folder = await PlacesUtils.bookmarks.insert({
    title: "Sidebar Test Folder",
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
  });
  bookmarks = await PlacesUtils.bookmarks.insertTree({
    guid: folder.guid,
    children: [
      {
        title: "Mozilla",
        url: "https://www.mozilla.org/",
      },
      {
        title: "Example",
        url: "https://sidebar.mozilla.org/",
      },
    ],
  });

  registerCleanupFunction(async () => {
    await PlacesUtils.bookmarks.eraseEverything();
  });
});

add_task(async function test_open_multiple_bookmarks() {
  await withSidebarTree("bookmarks", async tree => {
    tree.selectItems([folder.guid]);

    is(
      tree.selectedNode.title,
      "Sidebar Test Folder",
      "The sidebar test bookmarks folder is selected"
    );

    // open all bookmarks in this folder (which is two)
    await synthesizeClickOnSelectedTreeCell(tree, { button: 1 });

    // expand the "Other bookmarks" folder
    await synthesizeClickOnSelectedTreeCell(tree, { button: 0 });
    tree.selectItems([bookmarks[0].guid]);

    is(tree.selectedNode.title, "Mozilla", "The first bookmark is selected");

    await synthesizeClickOnSelectedTreeCell(tree, { button: 0 });

    TelemetryTestUtils.assertKeyedScalar(
      TelemetryTestUtils.getProcessScalars("parent", true),
      "sidebar.link",
      "bookmarks",
      3
    );

    let newWinOpened = BrowserTestUtils.waitForNewWindow();
    // open a bookmark in new window via context menu
    let popupShown = BrowserTestUtils.waitForPopupEvent(
      tree.ownerDocument.getElementById("placesContext"),
      "shown"
    );
    synthesizeClickOnSelectedTreeCell(tree, { type: "contextmenu" });
    await popupShown;

    let openNewWindowOption = tree.ownerDocument.getElementById(
      "placesContext_open:newwindow"
    );
    await BrowserTestUtils.activateMenuItem(openNewWindowOption);

    let newWin = await newWinOpened;

    // total bookmarks opened
    TelemetryTestUtils.assertKeyedScalar(
      TelemetryTestUtils.getProcessScalars("parent", true, true),
      "sidebar.link",
      "bookmarks",
      4
    );

    Services.telemetry.clearScalars();
    BrowserTestUtils.closeWindow(newWin);

    while (gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(gBrowser.selectedTab);
    }
  });
});

add_task(async function test_bookmarks_search() {
  let cumulativeSearchesHistogram = TelemetryTestUtils.getAndClearHistogram(
    "PLACES_BOOKMARKS_SEARCHBAR_CUMULATIVE_SEARCHES"
  );
  cumulativeSearchesHistogram.clear();

  await withSidebarTree("bookmarks", async tree => {
    // Search the tree.
    let searchBox = tree.ownerDocument.getElementById("search-box");
    await setSearch(searchBox, "example");

    searchBox.clear();
    info("Search was reset");

    await setSearch(searchBox, "mozilla");
    info("Second search was performed");

    // Select the first link and click on it.
    tree.selectNode(tree.view.nodeForTreeIndex(0));

    await synthesizeClickOnSelectedTreeCell(tree, { button: 0 });
    info("First link was selected and then clicked on");

    TelemetryTestUtils.assertKeyedScalar(
      TelemetryTestUtils.getProcessScalars("parent", true, true),
      "sidebar.link",
      "bookmarks",
      1
    );
  });

  TelemetryTestUtils.assertHistogram(cumulativeSearchesHistogram, 2, 1);
  info("Cumulative search probe is recorded");

  cumulativeSearchesHistogram.clear();
  Services.telemetry.clearScalars();

  while (gBrowser.tabs.length > 1) {
    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  }
});
