/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests retained results.
// When there is a pending search (user typed a search string and blurred
// without picking a result), on focus we should the search results again.

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

async function checkOpensOnFocus(win, state) {
  Assert.ok(!win.gURLBar.view.isOpen, "check urlbar panel is not open");
  win.gURLBar.blur();

  info("Check the keyboard shortcut.");
  await UrlbarTestUtils.promisePopupOpen(win, () => {
    win.document.getElementById("Browser:OpenLocation").doCommand();
  });

  await UrlbarTestUtils.promiseSearchComplete(win);
  Assert.equal(state.selectionStart, win.gURLBar.selectionStart);
  Assert.equal(state.selectionEnd, win.gURLBar.selectionEnd);

  await UrlbarTestUtils.promisePopupClose(win, () => {
    win.gURLBar.blur();
  });
  info("Focus with the mouse.");
  await UrlbarTestUtils.promisePopupOpen(win, () => {
    EventUtils.synthesizeMouseAtCenter(win.gURLBar.inputField, {}, win);
  });

  await UrlbarTestUtils.promiseSearchComplete(win);
  Assert.equal(state.selectionStart, win.gURLBar.selectionStart);
  Assert.equal(state.selectionEnd, win.gURLBar.selectionEnd);
}

async function checkDoesNotOpenOnFocus(win) {
  Assert.ok(!win.gURLBar.view.isOpen, "check urlbar panel is not open");
  win.gURLBar.blur();

  info("Check the keyboard shortcut.");
  let promiseState = checkPanelStatePersists(win, false);
  win.document.getElementById("Browser:OpenLocation").doCommand();
  await promiseState;
  win.gURLBar.blur();
  info("Focus with the mouse.");
  promiseState = checkPanelStatePersists(win, false);
  EventUtils.synthesizeMouseAtCenter(win.gURLBar.inputField, {}, win);
  await promiseState;
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.autoFill", true],
      ["browser.urlbar.scotchBonnet.enableOverride", false],
      // Tests get no default Top Sites, and the view is opened on an empty
      // string here, which would otherwise leave it closed with no results.
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://example.com/",
      ],
    ],
  });
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
  });
});

async function test_window(win) {
  for (let url of ["about:newtab", "about:home", "https://example.com/"]) {
    // withNewTab may hang on preloaded pages, thus instead of waiting for load
    // we just wait for the expected currentURI value.
    await BrowserTestUtils.withNewTab(
      { gBrowser: win.gBrowser, url, waitForLoad: false },
      async () => {
        await TestUtils.waitForCondition(
          () => win.gBrowser.currentURI.spec == url,
          "Ensure we're on the expected page"
        );

        // In one case use a value that triggers autofill.
        let autofill = url == "https://example.com/";
        await UrlbarTestUtils.promiseAutocompleteResultPopup({
          window: win,
          value: autofill ? "ex" : "foo",
          fireInputEvent: true,
        });
        let { value, selectionStart, selectionEnd } = win.gURLBar;
        if (!autofill) {
          selectionStart = 0;
        }
        info("expected " + value + " " + selectionStart + " " + selectionEnd);
        await UrlbarTestUtils.promisePopupClose(win, () => {
          win.gURLBar.blur();
        });

        info("The panel should open when there's a search string");
        await checkOpensOnFocus(win, { value, selectionStart, selectionEnd });
        await UrlbarTestUtils.promisePopupClose(win, () => {
          win.gURLBar.blur();
        });
      }
    );
  }
}

add_task(async function test_normalWindow() {
  let win = await BrowserTestUtils.openNewBrowserWindow();
  await test_window(win);
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_privateWindow() {
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  await test_window(privateWin);
  await BrowserTestUtils.closeWindow(privateWin);
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

add_task(async function test_pageproxystate_valid() {
  info("Focusing on valid pageproxystate should not reopen the view.");
  let win = await BrowserTestUtils.openNewBrowserWindow();

  info("Search for a full url and confirm it with Enter");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: win,
    value: "about:robots",
    fireInputEvent: true,
  });
  let loadedPromise = BrowserTestUtils.browserLoaded(
    win.gBrowser.selectedBrowser
  );
  EventUtils.synthesizeKey("KEY_Enter", {}, win);
  await loadedPromise;

  Assert.ok(!win.gURLBar.focused, "The urlbar should not be focused");
  info("Focus the urlbar");
  win.document.getElementById("Browser:OpenLocation").doCommand();

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_allowAutofill() {
  info("Check we respect allowAutofill from the last search");
  let win = await BrowserTestUtils.openNewBrowserWindow();

  await UrlbarTestUtils.promisePopupOpen(win, async () => {
    await UrlbarTestUtils.selectAndPaste("e", win);
  });
  Assert.equal(win.gURLBar.value, "e", "Should not autofill");
  let context = await win.gURLBar.lastQueryContextPromise;
  Assert.equal(context.allowAutofill, false, "Check initial allowAutofill");
  await UrlbarTestUtils.promisePopupClose(win);

  await UrlbarTestUtils.promisePopupOpen(win, async () => {
    win.document.getElementById("Browser:OpenLocation").doCommand();
  });
  await UrlbarTestUtils.promiseSearchComplete(win);
  Assert.equal(win.gURLBar.value, "e", "Should not autofill");
  context = await win.gURLBar.lastQueryContextPromise;
  Assert.equal(context.allowAutofill, false, "Check reopened allowAutofill");

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_rowReuse() {
  info("Result rows from the last query should be reused after refocus.");
  let win = await BrowserTestUtils.openNewBrowserWindow();

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: win,
    value: "ex",
    fireInputEvent: true,
  });

  let rowsContainer = win.gURLBar.view.panel.querySelector(
    ".urlbarView-results"
  );
  let initialRows = Array.from(rowsContainer.children);
  Assert.greater(initialRows.length, 0, "Initial query produced rows");

  await UrlbarTestUtils.promisePopupClose(win, () => {
    win.gURLBar.blur();
  });

  // Wait until breakout-extend stops.
  // This will test if row caching based on the urlbar width works
  // even when the width changes due to breakout-extend (bug 2037933).
  await TestUtils.waitForCondition(
    () => !win.gURLBar.hasAttribute("breakout-extend"),
    "Wait for breakout-extend to finish"
  );

  // Don't use promiseAutocompleteResultPopup.
  // Check the rows before the new query completes.
  EventUtils.synthesizeMouseAtCenter(win.gURLBar.inputField, {}, win);

  let newRows = Array.from(rowsContainer.children);
  Assert.equal(
    newRows.length,
    initialRows.length,
    "Same number of rows after refocus"
  );
  for (let i = 0; i < initialRows.length; i++) {
    Assert.equal(newRows[i], initialRows[i], `Row ${i} was reused`);
  }

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_rowReuseChangedWidth() {
  info(
    "Result rows should not be reused when the urlbar container width " +
      "changed between close and refocus."
  );
  let win = await BrowserTestUtils.openNewBrowserWindow();

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: win,
    value: "ex",
    fireInputEvent: true,
  });

  let rowsContainer = win.gURLBar.view.panel.querySelector(
    ".urlbarView-results"
  );
  let initialRows = Array.from(rowsContainer.children);
  Assert.greater(initialRows.length, 0, "Initial query produced rows");

  await UrlbarTestUtils.promisePopupClose(win, () => {
    win.gURLBar.blur();
  });

  // Resize the window to change the urlbar's width. This invalidates
  // the rows and they should be rebuilt from the cached query context.
  let urlbarContainer = win.gURLBar.parentElement;
  let originalContainerWidth = urlbarContainer.getBoundingClientRect().width;
  win.resizeTo(win.innerWidth + 100, win.outerHeight);
  await TestUtils.waitForCondition(
    () =>
      urlbarContainer.getBoundingClientRect().width != originalContainerWidth,
    "Wait for the urlbar container to resize"
  );

  // Don't use promiseAutocompleteResultPopup.
  // Check the rows before the new query completes.
  EventUtils.synthesizeMouseAtCenter(win.gURLBar.inputField, {}, win);

  let newRows = Array.from(rowsContainer.children);
  Assert.equal(
    newRows.length,
    initialRows.length,
    "Same number of rows after refocus"
  );
  for (let i = 0; i < initialRows.length; i++) {
    Assert.notEqual(
      newRows[i],
      initialRows[i],
      `Row ${i} was rebuilt from cached query context (not reused)`
    );
  }

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_clicks_after_autofill() {
  info(
    "Check that clickin on an autofilled input field doesn't requery, causing loss of the caret position"
  );
  let win = await BrowserTestUtils.openNewBrowserWindow();
  info("autofill in tab2, switch to tab1, then back to tab2 with the mouse");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: win,
    value: "e",
    fireInputEvent: true,
  });
  Assert.equal(win.gURLBar.value, "example.com/", "Should have autofilled");

  // Check single click.
  let input = win.gURLBar.inputField;
  EventUtils.synthesizeMouse(input, 30, 10, {}, win);
  // Wait a bit, in case of a mistake this would run a query, otherwise not.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 300));
  Assert.less(win.gURLBar.selectionStart, win.gURLBar.value.length);
  Assert.equal(win.gURLBar.selectionStart, win.gURLBar.selectionEnd);

  // Check double click.
  EventUtils.synthesizeMouse(input, 30, 10, { clickCount: 2 }, win);
  // Wait a bit, in case of a mistake this would run a query, otherwise not.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 300));
  Assert.less(win.gURLBar.selectionStart, win.gURLBar.value.length);
  Assert.greater(win.gURLBar.selectionEnd, win.gURLBar.selectionStart);

  await BrowserTestUtils.closeWindow(win);
});
