/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const SECURE_PAGE = "https://example.com/";
const GOOD_PAGE = "http://example.com/";
const BAD_CERT = "http://expired.example.com/";
const UNKNOWN_ISSUER = "http://self-signed.example.com/";

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["test.wait300msAfterTabSwitch", true]],
  });
});

add_task(async function () {
  info("Check that the error pages shows up");

  await testPageWithURI(
    GOOD_PAGE,
    "Should not show error page on upgradeable website.",
    false
  );
  await testPageWithURI(
    BAD_CERT,
    "Should show error page on bad-certificate error.",
    true
  );
  await testPageWithURI(
    UNKNOWN_ISSUER,
    "Should show error page on unkown-issuer error.",
    true
  );
});

add_task(async function () {
  info("Check that the go-back button returns to previous page");

  // Test with and without being in an iFrame
  for (let useFrame of [false, true]) {
    let tab = await openErrorPage(BAD_CERT, useFrame);
    let browser = tab.linkedBrowser;

    is(
      browser.webNavigation.canGoBack,
      false,
      "!webNavigation.canGoBack should be false."
    );
    is(
      browser.webNavigation.canGoForward,
      false,
      "webNavigation.canGoForward should be false."
    );

    // Populate the shistory entries manually, since it happens asynchronously
    // and the following tests will be too soon otherwise.
    await TabStateFlusher.flush(browser);
    let { entries } = JSON.parse(SessionStore.getTabState(tab));
    is(entries.length, 1, "There should be 1 shistory entry.");

    let bc = browser.browsingContext;
    if (useFrame) {
      bc = bc.children[0];
    }

    if (useFrame) {
      await SpecialPowers.spawn(bc, [], async function () {
        let returnButton = content.document.getElementById("goBack");
        is(
          returnButton,
          null,
          "Return-button should not be present in iFrame."
        );
      });
    } else {
      let locationChangePromise = BrowserTestUtils.waitForLocationChange(
        gBrowser,
        "about:home"
      );
      await SpecialPowers.spawn(bc, [], async function () {
        let returnButton = content.document.getElementById("goBack");
        is(
          returnButton.getAttribute("autofocus"),
          "true",
          "Return-button should have focus."
        );
        returnButton.click();
      });

      await locationChangePromise;

      is(browser.webNavigation.canGoBack, true, "webNavigation.canGoBack");
      is(
        browser.webNavigation.canGoForward,
        false,
        "!webNavigation.canGoForward"
      );
      is(gBrowser.currentURI.spec, "about:home", "Went back");
    }

    BrowserTestUtils.removeTab(gBrowser.selectedTab);
  }
});

add_task(async function () {
  info("Check that the go-back button returns to about:home");

  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, SECURE_PAGE);
  let browser = gBrowser.selectedBrowser;

  let errorPageLoaded = BrowserTestUtils.waitForErrorPage(browser);
  BrowserTestUtils.startLoadingURIString(browser, BAD_CERT);
  await errorPageLoaded;

  is(
    browser.webNavigation.canGoBack,
    true,
    "webNavigation.canGoBack should be true before navigation."
  );
  is(
    browser.webNavigation.canGoForward,
    false,
    "webNavigation.canGoForward should be false before navigation."
  );

  // Populate the shistory entries manually, since it happens asynchronously
  // and the following tests will be too soon otherwise.
  await TabStateFlusher.flush(browser);
  let { entries } = JSON.parse(SessionStore.getTabState(tab));
  is(entries.length, 2, "There should be 1 shistory entries.");

  let pageShownPromise = BrowserTestUtils.waitForContentEvent(
    browser,
    "pageshow",
    true
  );

  // Click on "go back" Button
  await SpecialPowers.spawn(browser, [], async function () {
    let returnButton = content.document.getElementById("goBack");
    returnButton.click();
  });
  await pageShownPromise;

  is(
    browser.webNavigation.canGoBack,
    false,
    "webNavigation.canGoBack should be false after navigation."
  );
  is(
    browser.webNavigation.canGoForward,
    true,
    "webNavigation.canGoForward should be true after navigation."
  );
  is(
    gBrowser.currentURI.spec,
    SECURE_PAGE,
    "Should go back to previous page after button click."
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// Utils

async function testPageWithURI(uri, message, expect) {
  // Open new Tab with URI
  let tab;
  if (expect) {
    tab = await openErrorPage(uri, false);
  } else {
    tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, uri, true);
  }

  // Check if HTTPS-Only Error-Page loaded instead
  let browser = tab.linkedBrowser;
  await SpecialPowers.spawn(
    browser,
    [message, expect],
    function (message, expect) {
      const doc = content.document;
      let result = doc.documentURI.startsWith("about:httpsonlyerror");
      is(result, expect, message);
    }
  );

  // Close tab again
  BrowserTestUtils.removeTab(tab);
}
