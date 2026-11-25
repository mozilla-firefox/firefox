/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that when opening a private browsing window and typing in it before
 * about:privatebrowsing loads, we don't clear the URL bar.
 */
add_task(async function () {
  let urlbarTestValue = "Mary had a little lamb";
  let win = OpenBrowserWindow({ private: true });
  registerCleanupFunction(() => BrowserTestUtils.closeWindow(win));
  await BrowserTestUtils.waitForEvent(win, "load");
  let promise = new Promise(resolve => {
    let wpl = {
      onLocationChange(aWebProgress, aRequest, aLocation) {
        if (aLocation && aLocation.spec == "about:privatebrowsing") {
          win.gBrowser.removeProgressListener(wpl);
          resolve();
        }
      },
    };
    win.gBrowser.addProgressListener(wpl);
  });
  Assert.notEqual(
    win.gBrowser.selectedBrowser.currentURI.spec,
    "about:privatebrowsing",
    "Check privatebrowsing page has not been loaded yet"
  );
  info("Search in urlbar");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: win,
    value: urlbarTestValue,
    fireInputEvent: true,
  });
  info("waiting for about:privatebrowsing load");
  await promise;

  let urlbar = win.gURLBar;
  is(
    urlbar.value,
    urlbarTestValue,
    "URL bar value should be the same once about:privatebrowsing has loaded"
  );
  is(
    win.gBrowser.selectedBrowser.userTypedValue,
    urlbarTestValue,
    "User typed value should be the same once about:privatebrowsing has loaded"
  );
});
