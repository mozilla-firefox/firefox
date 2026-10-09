/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

async function checkOpensOnFocus(win = window) {
  // The view should not open when the input is focused programmatically.
  win.gURLBar.blur();
  win.gURLBar.focus();
  Assert.ok(!win.gURLBar.view.isOpen, "check urlbar panel is not open");
  win.gURLBar.blur();

  // Check the keyboard shortcut.
  await UrlbarTestUtils.promisePopupOpen(win, () => {
    win.document.getElementById("Browser:OpenLocation").doCommand();
  });
  await UrlbarTestUtils.promisePopupClose(win, () => {
    win.gURLBar.blur();
  });

  // Focus with the mouse.
  await UrlbarTestUtils.promisePopupOpen(win, () => {
    EventUtils.synthesizeMouseAtCenter(win.gURLBar.inputField, {}, win);
  });
  await UrlbarTestUtils.promisePopupClose(win, () => {
    win.gURLBar.blur();
  });
}

add_setup(async function () {
  // Tests get no default Top Sites, and the empty panel only opens when it has
  // results to show.
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://example.com/",
      ],
    ],
  });

  // Add some history for the empty panel.
  await PlacesTestUtils.addVisits([
    {
      uri: "http://mochi.test:8888/",
      transition: PlacesUtils.history.TRANSITIONS.TYPED,
    },
  ]);
  registerCleanupFunction(() => PlacesUtils.history.clear());
});

add_task(async function test() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:blank" },
    async () => {
      await checkOpensOnFocus();
    }
  );
});

add_task(async function newtabAndHome() {
  for (let url of ["about:newtab", "about:home"]) {
    // withNewTab randomly hangs on these pages when waitForLoad = true (the
    // default), so pass false.
    await BrowserTestUtils.withNewTab(
      { gBrowser, url, waitForLoad: false },
      async browser => {
        // We don't wait for load, but we must ensure to be on the expected url.
        await TestUtils.waitForCondition(
          () => gBrowser.currentURI.spec == url,
          "Ensure we're on the expected page"
        );
        await checkOpensOnFocus();
        await BrowserTestUtils.withNewTab(
          { gBrowser, url: "http://example.com/" },
          async otherBrowser => {
            await checkOpensOnFocus();
            // Switch back to about:newtab/home.
            await BrowserTestUtils.switchTab(
              gBrowser,
              gBrowser.getTabForBrowser(browser)
            );
            await checkOpensOnFocus();
            // Switch back to example.com.
            await BrowserTestUtils.switchTab(
              gBrowser,
              gBrowser.getTabForBrowser(otherBrowser)
            );
            await checkOpensOnFocus();
          }
        );
        // After example.com closes, about:newtab/home is selected again.
        await checkOpensOnFocus();
        // Load example.com in the same tab.
        BrowserTestUtils.startLoadingURIString(
          gBrowser.selectedBrowser,
          "http://example.com/"
        );
        await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
        await checkOpensOnFocus();
      }
    );
  }
});
