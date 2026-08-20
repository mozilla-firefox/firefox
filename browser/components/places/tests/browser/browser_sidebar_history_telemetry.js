/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { TelemetryTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryTestUtils.sys.mjs"
);
const firstNodeIndex = 0;

// The prompt returns 1 for cancelled and 0 for accepted.
let gResponse = 1;
(function replacePromptService() {
  let originalPromptService = Services.prompt;
  Services.prompt = {
    QueryInterface: ChromeUtils.generateQI([Ci.nsIPromptService]),
    confirmEx: () => gResponse,
  };
  registerCleanupFunction(() => {
    Services.prompt = originalPromptService;
  });
})();

add_setup(async function () {
  // This test is specific to the legacy history sidebar. For equivalent telemetry for
  // the new sidebar and history panel implementation, see bug 2002108.
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.revamp", false]],
  });
  await PlacesUtils.history.clear();

  // Visited pages listed by descending visit date.
  let pages = [
    "https://sidebar.mozilla.org/a",
    "https://sidebar.mozilla.org/b",
    "https://sidebar.mozilla.org/c",
    "https://www.mozilla.org/d",
  ];

  // Add some visited page.
  let time = Date.now();
  let places = [];
  for (let i = 0; i < pages.length; i++) {
    places.push({
      uri: Services.io.newURI(pages[i]),
      visitDate: (time - i) * 1000,
      transition: PlacesUtils.history.TRANSITION_TYPED,
    });
  }
  await PlacesTestUtils.addVisits(places);

  registerCleanupFunction(async () => {
    await PlacesUtils.history.clear();
  });
});

add_task(async function test_click_multiple_history_entries() {
  await withSidebarTree("history", async tree => {
    tree.ownerDocument.getElementById("byday").doCommand();

    // Select the first link and click on it.
    tree.selectNode(tree.view.nodeForTreeIndex(firstNodeIndex));
    is(
      tree.selectedNode.title,
      "Today",
      "The Today history sidebar button is selected"
    );

    // if a user tries to open all history items and they cancel opening
    // those items in new tabs (due to max tab limit warning),
    // no telemetry should be recorded
    gResponse = 1;
    await SpecialPowers.pushPrefEnv({
      set: [["browser.tabs.maxOpenBeforeWarn", 4]],
    });

    // open multiple history items with a single click
    await synthesizeClickOnSelectedTreeCell(tree, { button: 1 });

    TelemetryTestUtils.assertScalarUnset(
      TelemetryTestUtils.getProcessScalars("parent", true, true),
      "sidebar.link"
    );

    // if they proceed with opening history multiple history items despite the warning,
    // telemetry should be recorded
    gResponse = 0;
    await synthesizeClickOnSelectedTreeCell(tree, { button: 1 });

    TelemetryTestUtils.assertKeyedScalar(
      TelemetryTestUtils.getProcessScalars("parent", true, true),
      "sidebar.link",
      "history",
      4
    );

    let parentNode = tree.selectedNode;
    if (!parentNode.containerOpen) {
      // Only need to open/expand container node on first run
      await synthesizeClickOnSelectedTreeCell(tree);
    }
    if (parentNode.title == "Today" && parentNode.hasChildren) {
      info(`Selecting node with title ${parentNode?.getChild(0)?.title}`);
      tree.selectNode(parentNode.getChild(0));
    }

    let popupShown = BrowserTestUtils.waitForPopupEvent(
      tree.ownerDocument.getElementById("placesContext"),
      "shown"
    );
    synthesizeClickOnSelectedTreeCell(tree, { type: "contextmenu" });
    await popupShown;

    TelemetryTestUtils.assertScalarUnset(
      TelemetryTestUtils.getProcessScalars("parent", true, true),
      "sidebar.link"
    );

    let openNewTabOption = tree.ownerDocument.getElementById(
      "placesContext_open:newtab"
    );
    await BrowserTestUtils.activateMenuItem(openNewTabOption);

    TelemetryTestUtils.assertKeyedScalar(
      TelemetryTestUtils.getProcessScalars("parent", true, true),
      "sidebar.link",
      "history",
      1
    );

    if (parentNode.title == "Today" && parentNode.hasChildren) {
      tree.selectNode(parentNode.getChild(0));
    }

    let newWinOpened = BrowserTestUtils.waitForNewWindow();

    popupShown = BrowserTestUtils.waitForPopupEvent(
      tree.ownerDocument.getElementById("placesContext"),
      "shown"
    );
    synthesizeClickOnSelectedTreeCell(tree, { type: "contextmenu" });
    await popupShown;

    TelemetryTestUtils.assertScalarUnset(
      TelemetryTestUtils.getProcessScalars("parent", true, true),
      "sidebar.link"
    );

    let openNewWindowOption = tree.ownerDocument.getElementById(
      "placesContext_open:newwindow"
    );
    await BrowserTestUtils.activateMenuItem(openNewWindowOption);

    let newWin = await newWinOpened;

    TelemetryTestUtils.assertKeyedScalar(
      TelemetryTestUtils.getProcessScalars("parent", true, true),
      "sidebar.link",
      "history",
      1
    );

    await BrowserTestUtils.closeWindow(newWin);

    if (parentNode.title == "Today" && parentNode.hasChildren) {
      tree.selectNode(parentNode.getChild(0));
    }

    let newPrivateWinOpened = BrowserTestUtils.waitForNewWindow();

    popupShown = BrowserTestUtils.waitForPopupEvent(
      tree.ownerDocument.getElementById("placesContext"),
      "shown"
    );
    synthesizeClickOnSelectedTreeCell(tree, { type: "contextmenu" });
    await popupShown;

    TelemetryTestUtils.assertScalarUnset(
      TelemetryTestUtils.getProcessScalars("parent", true, true),
      "sidebar.link"
    );

    let openNewPrivateWindowOption = tree.ownerDocument.getElementById(
      "placesContext_open:newprivatewindow"
    );
    await BrowserTestUtils.activateMenuItem(openNewPrivateWindowOption);

    let newPrivateWin = await newPrivateWinOpened;

    TelemetryTestUtils.assertKeyedScalar(
      TelemetryTestUtils.getProcessScalars("parent", true, true),
      "sidebar.link",
      "history",
      1
    );

    await BrowserTestUtils.closeWindow(newPrivateWin);
  });

  await SpecialPowers.popPrefEnv();

  while (gBrowser.tabs.length > 1) {
    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  }
});

add_task(async function test_search_and_filter() {
  let cumulativeSearchesHistogram = Services.telemetry.getHistogramById(
    "PLACES_SEARCHBAR_CUMULATIVE_SEARCHES"
  );
  cumulativeSearchesHistogram.clear();
  let cumulativeFilterCountHistogram = Services.telemetry.getHistogramById(
    "PLACES_SEARCHBAR_CUMULATIVE_FILTER_COUNT"
  );
  cumulativeFilterCountHistogram.clear();

  await withSidebarTree("history", async tree => {
    // Apply a search filter.
    tree.ownerDocument.getElementById("bylastvisited").doCommand();
    info("Search filter was changed to bylastvisited");

    // Search the tree.
    let searchBox = tree.ownerDocument.getElementById("search-box");
    await setSearch(searchBox, "sidebar.mozilla");
    info("Tree was searched with sting sidebar.mozilla");

    await setSearch(searchBox, "");
    info("Search was reset");

    // Perform a second search.
    await setSearch(searchBox, "sidebar.mozilla");
    info("Second search was performed");

    // Select the first link and click on it.
    tree.selectNode(tree.view.nodeForTreeIndex(firstNodeIndex));
    await synthesizeClickOnSelectedTreeCell(tree, { button: 1 });
    info("First link was selected and then clicked on");

    TelemetryTestUtils.assertKeyedScalar(
      TelemetryTestUtils.getProcessScalars("parent", true, true),
      "sidebar.link",
      "history",
      1
    );
  });

  TelemetryTestUtils.assertHistogram(cumulativeSearchesHistogram, 2, 1);
  info("Cumulative search telemetry looks right");

  TelemetryTestUtils.assertHistogram(cumulativeFilterCountHistogram, 1, 1);
  info("Cumulative search filter telemetry looks right");

  cumulativeSearchesHistogram.clear();
  cumulativeFilterCountHistogram.clear();

  await withSidebarTree("history", async tree => {
    // Apply a search filter.
    tree.ownerDocument.getElementById("byday").doCommand();
    info("First search filter applied");

    // Apply another search filter.
    tree.ownerDocument.getElementById("bylastvisited").doCommand();
    info("Second search filter applied");

    // Apply a search filter.
    tree.ownerDocument.getElementById("byday").doCommand();
    info("Third search filter applied");

    // Search the tree.
    let searchBox = tree.ownerDocument.getElementById("search-box");
    await setSearch(searchBox, "sidebar.mozilla");

    // Select the first link and click on it.
    tree.selectNode(tree.view.nodeForTreeIndex(firstNodeIndex));
    await synthesizeClickOnSelectedTreeCell(tree, { button: 1 });

    TelemetryTestUtils.assertKeyedScalar(
      TelemetryTestUtils.getProcessScalars("parent", true, true),
      "sidebar.link",
      "history",
      1
    );
  });

  TelemetryTestUtils.assertHistogram(cumulativeSearchesHistogram, 1, 1);
  TelemetryTestUtils.assertHistogram(cumulativeFilterCountHistogram, 3, 1);

  cumulativeSearchesHistogram.clear();
  cumulativeFilterCountHistogram.clear();
  while (gBrowser.tabs.length > 1) {
    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  }
});
