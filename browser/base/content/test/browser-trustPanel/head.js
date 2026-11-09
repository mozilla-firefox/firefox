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

async function loadBadCertPage(url) {
  const loaded = BrowserTestUtils.waitForErrorPage(gBrowser.selectedBrowser);
  const loadFlagsSkipCache =
    Ci.nsIWebNavigation.LOAD_FLAGS_BYPASS_PROXY |
    Ci.nsIWebNavigation.LOAD_FLAGS_BYPASS_CACHE;
  BrowserTestUtils.startLoadingURIString(
    gBrowser.selectedBrowser,
    url,
    loadFlagsSkipCache
  );
  await loaded;
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async () => {
    const netErrorCard =
      content.document.querySelector("net-error-card").wrappedJSObject;
    await netErrorCard.getUpdateComplete();
    EventUtils.synthesizeMouseAtCenter(
      netErrorCard.advancedButton,
      {},
      content
    );
    await ContentTaskUtils.waitForCondition(() => {
      return (
        netErrorCard.exceptionButton && !netErrorCard.exceptionButton.disabled
      );
    }, "Waiting for exception button");
    netErrorCard.exceptionButton.scrollIntoView(true);
    EventUtils.synthesizeMouseAtCenter(
      netErrorCard.exceptionButton,
      {},
      content
    );
  });
  await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
}
