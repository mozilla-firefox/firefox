/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/components/urlbar/tests/browser/head-common.js",
  this
);

registerCleanupFunction(async () => {
  await UrlbarTestUtils.promisePopupClose(window);
});

async function searchWithTab(
  searchString,
  tab = null,
  engine = SearchService.defaultEngine,
  expectedPersistedSearchTerms = true
) {
  if (!tab) {
    tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  }

  let [expectedSearchUrl] = UrlbarUtils.getSearchQueryUrl(engine, searchString);
  let browserLoadedPromise = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    expectedSearchUrl
  );

  gURLBar.focus();
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    waitForFocus,
    value: searchString,
    fireInputEvent: true,
    selectionStart: 0,
    selectionEnd: searchString.length - 1,
  });
  EventUtils.synthesizeKey("KEY_Enter");
  await browserLoadedPromise;

  if (expectedPersistedSearchTerms) {
    info("Load a tab with search terms persisting in the urlbar.");
    UrlbarTestUtils.assertSearchStringIsInUrlbar(window, searchString);
  }

  return { tab, expectedSearchUrl };
}
