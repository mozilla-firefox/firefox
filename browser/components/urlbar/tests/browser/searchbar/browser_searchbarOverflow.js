/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if the results panel is disabled on the overflow panel
 * and if searching and search mode work as expected.
 */

let searchbar;
let engine1;
let engine2;

async function openOverflowPanel() {
  let overflowButton = document.getElementById("nav-bar-overflow-button");
  let menu = document.getElementById("widget-overflow");
  let shown = BrowserTestUtils.waitForEvent(menu, "popupshown");
  overflowButton.click();
  await shown;
}

/**
 * @returns {Promise}
 */
function promiseOverflowHidden() {
  let menu = document.getElementById("widget-overflow");
  return BrowserTestUtils.waitForEvent(menu, "popuphidden");
}

add_setup(async function () {
  await SearchTestUtils.updateRemoteSettingsConfig([
    { identifier: "engine1" },
    {
      identifier: "engine2",
      base: {
        urls: {
          search: {
            base: "https://www.example.com/engine2/search",
            searchTermParamName: "q",
          },
        },
      },
    },
  ]);
  engine1 = SearchService.getEngineById("engine1");
  engine2 = SearchService.getEngineById("engine2");
  searchbar = document.querySelector("#searchbar-new");
});

add_task(async function test_fixedOverflow() {
  Assert.ok(!searchbar.inOverflowPanel, "Not in overflow panel to start with");
  CustomizableUI.addWidgetToArea(
    "search-container",
    CustomizableUI.AREA_FIXED_OVERFLOW_PANEL
  );
  Assert.ok(searchbar.inOverflowPanel, "Was moved to overflow panel");
  await openOverflowPanel();

  for (let i = 0; i < 10; i++) {
    EventUtils.synthesizeKey("KEY_Tab");
    if (searchbar.focused) {
      break;
    }
  }
  Assert.ok(searchbar.focused, "Searchbar is focused eventually");
  EventUtils.synthesizeKey("KEY_Tab", { shiftKey: true });
  Assert.equal(
    document.activeElement,
    searchbar.querySelector(".searchmode-switcher"),
    "Previous focusable element is the searchmode switcher"
  );
  EventUtils.synthesizeKey("KEY_Tab");
  Assert.ok(searchbar.focused, "Searchbar is focused again");

  // Clear lastQueryContextPromise in case another test ran before.
  searchbar.lastQueryContextPromise = Promise.resolve();

  // The search term is a URL but it should still be searched.
  let searchTerm = "https://example.com/";
  EventUtils.sendString(searchTerm);

  let lastQueryContext = await searchbar.lastQueryContextPromise;
  Assert.equal(lastQueryContext, undefined, "", "Did not start a query");
  Assert.ok(!searchbar.view.isOpen, "Did not open the results panel");

  EventUtils.synthesizeKey("KEY_Enter");
  await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);

  let expectedUrl = engine1.getSubmission(searchTerm).uri.spec;
  Assert.equal(gBrowser.currentURI.spec, expectedUrl, "Search successful");
  Assert.equal(searchbar.value, searchTerm, "Search term was persisted");

  info("Try searching");
  let popup = await SearchbarTestUtils.openSearchModeSwitcher(window);
  Assert.ok(true, "Can open search mode switcher");
  expectedUrl = engine2.getSubmission(searchTerm).uri.spec;
  let browserLoaded = BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    expectedUrl
  );
  popup.querySelector('panel-item[data-engine-name="engine2"]').click();
  EventUtils.synthesizeKey("KEY_Enter");

  await browserLoaded;
  Assert.equal(gBrowser.currentURI.spec, expectedUrl, "Used correct engine");
  Assert.equal(searchbar.value, searchTerm, "Search term was persisted");
  await SearchbarTestUtils.assertSearchMode(window, {
    engineName: "engine2",
    entry: "searchbutton",
    source: 3,
  });
  Assert.ok(true, "Still in search mode");

  info("Try exiting search mode.");
  let closeButton = searchbar.querySelector(".searchmode-switcher-close");
  EventUtils.synthesizeMouseAtCenter(closeButton, {});
  await SearchbarTestUtils.assertSearchMode(window, null);
  Assert.ok(true, "Exited search mode");

  await SearchbarTestUtils.withContextMenu(window, () => {});
  Assert.equal(
    document.getElementById("widget-overflow").state,
    "open",
    "Opening the context menu doesn't close the overflow panel"
  );

  let hiddenPanelPromise = promiseOverflowHidden(window);
  EventUtils.synthesizeKey("KEY_Escape");
  await hiddenPanelPromise;
  Assert.ok(true, "Escape closes overflow panel");

  // Move back to the navbar.
  await gCUITestUtils.addSearchBar();
});

add_task(async function test_overflowing() {
  await SearchbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "kitten",
  });
  Assert.ok(searchbar.view.isOpen, "Results panel is open");

  // Create widgets that will cause overflow.
  let widgetIds = [];
  for (let i = 0; i < 10; i++) {
    let id = "test-widget-" + i;
    widgetIds.push(id);
    CustomizableUI.createWidget({
      id,
      type: "button",
      removable: true,
      label: "test " + i,
      tooltiptext: id,
      defaultArea: CustomizableUI.AREA_NAVBAR,
    });
  }
  for (let id of widgetIds) {
    CustomizableUI.getWidget(id).forWindow(window).node.style.minWidth =
      "200px";
    // Move to start of navbar to make sure the searchbar will overflow.
    CustomizableUI.moveWidgetWithinArea(id, 0);
  }

  // Wait for overflow handling
  await window.promiseDocumentFlushed(() => {});
  let navbar = document.getElementById(CustomizableUI.AREA_NAVBAR);
  await TestUtils.waitForCondition(
    () => !navbar.overflowable.isHandlingOverflow()
  );

  Assert.ok(searchbar.inOverflowPanel, "Was moved to overflow panel");
  Assert.ok(!searchbar.view.isOpen, "Results panel was closed");

  for (let id of widgetIds) {
    CustomizableUI.destroyWidget(id);
  }
  // Wait for overflow handling
  await window.promiseDocumentFlushed(() => {});
  await TestUtils.waitForCondition(
    () => !navbar.overflowable.isHandlingOverflow()
  );

  Assert.ok(!searchbar.inOverflowPanel, "Was moved back into navbar");
  await SearchbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "kitten",
  });
  Assert.ok(searchbar.view.isOpen, "Results panel is open");
  searchbar.handleRevert();
});

// Make sure tab navigation in the overflow panel didn't break tab
// navigation in the navbar. This happened in the past because the
// PanelView code set tabindex="-1" on the input element.
add_task(async function test_tabNavigation() {
  gURLBar.focus();
  // Move across site actions.
  for (let i = 0; i < 10; i++) {
    EventUtils.synthesizeKey("KEY_Tab");
    if (searchbar.focused) {
      break;
    }
  }
  Assert.ok(searchbar.focused, "Searchbar is focused eventually");
  EventUtils.synthesizeKey("KEY_Tab", { shiftKey: true });
  Assert.equal(
    document.activeElement,
    searchbar.querySelector(".searchmode-switcher"),
    "Previous focusable element is the searchmode switcher"
  );
  EventUtils.synthesizeKey("KEY_Tab");
  Assert.ok(searchbar.focused, "Searchbar is focused again");
});
