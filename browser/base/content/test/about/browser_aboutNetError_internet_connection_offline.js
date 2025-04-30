/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

async function checkErrorForInvalidUriLoad(l10nId) {
  let browser;
  let pageLoaded;
  await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    () => {
      gBrowser.selectedTab = BrowserTestUtils.addTab(
        gBrowser,
        "https://does-not-exist.test"
      );
      browser = gBrowser.selectedBrowser;
      pageLoaded = BrowserTestUtils.waitForErrorPage(browser);
    },
    false
  );
  await pageLoaded;

  await SpecialPowers.spawn(browser, [l10nId], expectedl10nId => {
    const doc = content.document;
    ok(
      doc.documentURI.startsWith("about:neterror"),
      "Should be showing error page"
    );
    const titleEl = doc.querySelector(".title-text");
    const actualDataL10nID = titleEl.getAttribute("data-l10n-id");
    is(actualDataL10nID, expectedl10nId, "Correct error page title is set");
  });
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
}

registerCleanupFunction(function () {
  Services.io.offline = false;
  Services.io.setConnectivityForTesting(true);
});

add_task(async function test_offline_mode() {
  Services.io.offline = true;
  await checkErrorForInvalidUriLoad("netOffline-title");
});

add_task(async function test_internet_connection_offline() {
  Services.io.offline = false;
  Services.io.setConnectivityForTesting(false);
  await checkErrorForInvalidUriLoad("internet-connection-offline-title");
});
