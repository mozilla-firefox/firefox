/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests retained results.
// When there is a pending search (user typed a search string and blurred
// without picking a result), on focus we should the search results again.

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
  registerCleanupFunction(PlacesUtils.history.clear);
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
  await test_window(window);
});

add_task(async function test_privateWindow() {
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  await test_window(privateWin);
  await BrowserTestUtils.closeWindow(privateWin);
});

add_task(async function test_pageproxystate_valid() {
  info("Focusing on valid pageproxystate should not reopen the view.");
  await BrowserTestUtils.withNewTab("about:blank", async browser => {
    info("Search for a full url and confirm it with Enter");
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "about:robots",
      fireInputEvent: true,
    });
    let loadedPromise = BrowserTestUtils.browserLoaded(browser);
    EventUtils.synthesizeKey("KEY_Enter");
    await loadedPromise;

    Assert.ok(!gURLBar.focused, "The urlbar should not be focused");
    info("Focus the urlbar");
    document.getElementById("Browser:OpenLocation").doCommand();
  });
});

add_task(async function test_allowAutofill() {
  info("Check we respect allowAutofill from the last search");
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    await UrlbarTestUtils.promisePopupOpen(window, async () => {
      await UrlbarTestUtils.selectAndPaste("e", window);
    });
    Assert.equal(gURLBar.value, "e", "Should not autofill");
    let context = await gURLBar.lastQueryContextPromise;
    Assert.equal(context.allowAutofill, false, "Check initial allowAutofill");
    await UrlbarTestUtils.promisePopupClose(window);

    await UrlbarTestUtils.promisePopupOpen(window, async () => {
      document.getElementById("Browser:OpenLocation").doCommand();
    });
    await UrlbarTestUtils.promiseSearchComplete(window);
    Assert.equal(gURLBar.value, "e", "Should not autofill");
    context = await gURLBar.lastQueryContextPromise;
    Assert.equal(context.allowAutofill, false, "Check reopened allowAutofill");
    await UrlbarTestUtils.promisePopupClose(window);
  });
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

  // Wait until the bar collapses.
  // This will test if row caching based on the urlbar width works
  // even when the width changes with the popover (bug 2037933).
  await TestUtils.waitForCondition(
    () => !win.gURLBar.matches(":popover-open"),
    "Wait for the popover to close"
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

  info(
    "Result rows should not be reused when the urlbar container width " +
      "changed between close and refocus."
  );
  await UrlbarTestUtils.promiseSearchComplete(win);
  initialRows = Array.from(rowsContainer.children);
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

  newRows = Array.from(rowsContainer.children);
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
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    info("autofill in tab2, switch to tab1, then back to tab2 with the mouse");
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "e",
      fireInputEvent: true,
    });
    Assert.equal(gURLBar.value, "example.com/", "Should have autofilled");

    // Check single click.
    let input = gURLBar.inputField;
    EventUtils.synthesizeMouse(input, 30, 10, {});
    // Wait a bit, in case of a mistake this would run a query, otherwise not.
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    await new Promise(resolve => setTimeout(resolve, 300));
    Assert.less(gURLBar.selectionStart, gURLBar.value.length);
    Assert.equal(gURLBar.selectionStart, gURLBar.selectionEnd);

    // Check double click.
    EventUtils.synthesizeMouse(input, 30, 10, { clickCount: 2 });
    // Wait a bit, in case of a mistake this would run a query, otherwise not.
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    await new Promise(resolve => setTimeout(resolve, 300));
    Assert.less(gURLBar.selectionStart, gURLBar.value.length);
    Assert.greater(gURLBar.selectionEnd, gURLBar.selectionStart);

    await UrlbarTestUtils.promisePopupClose(window);
  });
});
