/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests retained results when switching tabs.
// When a tab has a pending search, switching to it should show the search
// results again.

async function checkPanelStatePersists(win, isOpen) {
  // Check for popup events, we should not see any of them because the urlbar
  // popup state should not change. This also ensures we don't cause flickering
  // open/close actions.
  function handler(event) {
    Assert.ok(false, `Received unexpected event ${event.type}`);
  }
  win.gURLBar.addEventListener("popupshowing", handler);
  win.gURLBar.addEventListener("popuphiding", handler);
  // Because the panel opening may not be immediate, we must wait a bit.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 300));
  win.gURLBar.removeEventListener("popupshowing", handler);
  win.gURLBar.removeEventListener("popuphiding", handler);
  Assert.equal(
    isOpen,
    win.gURLBar.view.isOpen,
    `check urlbar remains ${isOpen ? "open" : "closed"}`
  );
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.autoFill", true],
      ["browser.urlbar.scotchBonnet.enableOverride", false],
    ],
  });
  UrlbarTestUtils.registerZeroPrefixProvider();
  // Add some history for the empty panel and autofill.
  await PlacesTestUtils.addVisits([
    {
      uri: "https://example.com/",
      transition: PlacesUtils.history.TRANSITIONS.TYPED,
    },
    {
      uri: "https://example.com/foo/",
      transition: PlacesUtils.history.TRANSITIONS.TYPED,
    },
  ]);
  registerCleanupFunction(async function () {
    await PlacesUtils.history.clear();
    UrlbarUtils._backspaceBlocks.clear();
  });
});

add_task(async function test_tabSwitch() {
  info("Check that switching tabs reopens the view.");
  let win = await BrowserTestUtils.openNewBrowserWindow();
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: win,
    value: "ex",
    fireInputEvent: true,
  });
  let { value, selectionStart, selectionEnd } = win.gURLBar;
  Assert.equal(value, "example.com/", "Check autofill value");
  Assert.ok(
    selectionStart > 0 && selectionEnd > selectionStart,
    "Check autofill selection"
  );

  Assert.ok(win.gURLBar.focused, "The urlbar should be focused");
  let tab1 = win.gBrowser.selectedTab;

  async function check_autofill() {
    // The urlbar code waits for both TabSelect and the focus change, thus
    // we can't just wait for search completion here, we have to poll for a
    // value.
    await TestUtils.waitForCondition(
      () => win.gURLBar.value == "example.com/",
      "wait for autofill value"
    );
    // Ensure stable results.
    await UrlbarTestUtils.promiseSearchComplete(win);
    Assert.equal(selectionStart, win.gURLBar.selectionStart);
    Assert.equal(selectionEnd, win.gURLBar.selectionEnd);
  }

  info("Open a new tab with the same search");
  let tab2 = await BrowserTestUtils.openNewForegroundTab(win.gBrowser);
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: win,
    value: "ex",
    fireInputEvent: true,
  });

  info("Switch across tabs");
  for (let tab of win.gBrowser.tabs) {
    await UrlbarTestUtils.promisePopupOpen(win, async () => {
      await BrowserTestUtils.switchTab(win.gBrowser, tab);
    });
    await check_autofill();
  }

  info("Close tab and check the view is open.");
  await UrlbarTestUtils.promisePopupOpen(win, () => {
    BrowserTestUtils.removeTab(tab2);
  });
  await check_autofill();

  info("Open a new tab with a different search");
  tab2 = await BrowserTestUtils.openNewForegroundTab(win.gBrowser);
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: win,
    value: "xam",
    fireInputEvent: true,
  });

  info("Switch to the first tab and check the panel remains open");
  let promiseState = checkPanelStatePersists(win, true);
  await BrowserTestUtils.switchTab(win.gBrowser, tab1);
  await promiseState;
  await UrlbarTestUtils.promiseSearchComplete(win);
  await check_autofill();

  info("Switch to the second tab and check the panel remains open");
  promiseState = checkPanelStatePersists(win, true);
  await BrowserTestUtils.switchTab(win.gBrowser, tab2);
  await promiseState;
  await UrlbarTestUtils.promiseSearchComplete(win);
  Assert.equal(win.gURLBar.value, "xam", "check value");
  Assert.equal(win.gURLBar.selectionStart, 3);
  Assert.equal(win.gURLBar.selectionEnd, 3);

  info("autofill in tab2, switch to tab1, then back to tab2 with the mouse");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: win,
    value: "e",
    fireInputEvent: true,
  });
  // Adjust selection start, we are using a different search string.
  await BrowserTestUtils.switchTab(win.gBrowser, tab1);
  await UrlbarTestUtils.promiseSearchComplete(win);
  await check_autofill();
  tab2.click();
  selectionStart = 1;
  await check_autofill();

  info("Check we don't rerun a search if the shortcut is used on an open view");
  EventUtils.synthesizeKey("KEY_Backspace", {}, win);
  await UrlbarTestUtils.promiseSearchComplete(win);
  Assert.ok(win.gURLBar.view.isOpen, "The view should be open");
  Assert.equal(win.gURLBar.value, "e", "The value should be the typed one");
  win.document.getElementById("Browser:OpenLocation").doCommand();
  // A search should not run here, so there's nothing to wait for.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 300));
  Assert.ok(win.gURLBar.view.isOpen, "The view should be open");
  Assert.equal(win.gURLBar.value, "e", "The value should not change");

  info(
    "Tab switch from an empty search tab with unfocused urlbar to a tab with a search string and a focused urlbar"
  );
  win.gURLBar.value = "";
  win.gURLBar.blur();
  await UrlbarTestUtils.promisePopupOpen(win, async () => {
    await BrowserTestUtils.switchTab(win.gBrowser, tab1);
  });

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_tabSwitch_pageproxystate() {
  info("Switching tabs on valid pageproxystate doesn't reopen.");

  info("Adding some visits for the empty panel");
  await PlacesTestUtils.addVisits([
    "https://example.com/",
    "https://example.org/",
  ]);
  registerCleanupFunction(PlacesUtils.history.clear);

  let win = await BrowserTestUtils.openNewBrowserWindow();
  BrowserTestUtils.startLoadingURIString(
    win.gBrowser.selectedBrowser,
    "about:robots"
  );
  let tab1 = win.gBrowser.selectedTab;

  info("Open a new tab and the empty search");
  let tab2 = await BrowserTestUtils.openNewForegroundTab(win.gBrowser);
  await UrlbarTestUtils.promisePopupOpen(win, async () => {
    win.gURLBar.focus();
    // On Linux and Mac down moves caret to the end of the text unless it's
    // there already.
    win.gURLBar.selectionStart = win.gURLBar.selectionEnd =
      win.gURLBar.value.length;
    EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);
  });
  await UrlbarTestUtils.promiseSearchComplete(win);
  let result = await UrlbarTestUtils.getDetailsOfResultAt(win, 0);
  Assert.notEqual(result.url, "about:robots");

  info("Switch to the first tab and start searching with DOWN");
  await UrlbarTestUtils.promisePopupClose(win, async () => {
    await BrowserTestUtils.switchTab(win.gBrowser, tab1);
  });
  await checkPanelStatePersists(win, false);
  await UrlbarTestUtils.promisePopupOpen(win, async () => {
    win.gURLBar.focus();
    // On Linux and Mac down moves caret to the end of the text unless it's
    // there already.
    win.gURLBar.selectionStart = win.gURLBar.selectionEnd =
      win.gURLBar.value.length;
    EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);
  });
  await UrlbarTestUtils.promiseSearchComplete(win);

  info("Switcihng to the second tab should not reopen the search");
  await UrlbarTestUtils.promisePopupClose(win, async () => {
    await BrowserTestUtils.switchTab(win.gBrowser, tab2);
  });
  await checkPanelStatePersists(win, false);

  info("Switching to the first tab should not reopen the search");
  let promiseState = await checkPanelStatePersists(win, false);
  await BrowserTestUtils.switchTab(win.gBrowser, tab1);
  await promiseState;
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_tabSwitch_emptySearch() {
  info("Switching between empty-search tabs should not reopen the view.");
  let win = await BrowserTestUtils.openNewBrowserWindow();

  info("Open the empty search");
  let tab1 = win.gBrowser.selectedTab;
  await UrlbarTestUtils.promisePopupOpen(win, async () => {
    win.gURLBar.focus();
    // On Linux and Mac down moves caret to the end of the text unless it's
    // there already.
    win.gURLBar.selectionStart = win.gURLBar.selectionEnd =
      win.gURLBar.value.length;
    EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);
  });
  await UrlbarTestUtils.promiseSearchComplete(win);

  info("Open a new tab and the empty search");
  let tab2 = await BrowserTestUtils.openNewForegroundTab(win.gBrowser);
  await UrlbarTestUtils.promisePopupOpen(win, async () => {
    win.gURLBar.focus();
    // On Linux and Mac down moves caret to the end of the text unless it's
    // there already.
    win.gURLBar.selectionStart = win.gURLBar.selectionEnd =
      win.gURLBar.value.length;
    EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);
  });
  await UrlbarTestUtils.promiseSearchComplete(win);

  info("Switching to the first tab should not reopen the view");
  await UrlbarTestUtils.promisePopupClose(win, async () => {
    await BrowserTestUtils.switchTab(win.gBrowser, tab1);
  });
  await checkPanelStatePersists(win, false);

  info("Switching to the second tab should not reopen the view");
  let promiseState = await checkPanelStatePersists(win, false);
  await BrowserTestUtils.switchTab(win.gBrowser, tab2);
  await promiseState;

  await BrowserTestUtils.closeWindow(win);
});
