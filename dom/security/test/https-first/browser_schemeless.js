/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// We explicitly need HTTP URLs in this test
/* eslint-disable sdl/no-insecure-url */

ChromeUtils.defineLazyGetter(this, "UrlbarTestUtils", () => {
  const { UrlbarTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/UrlbarTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

XPCOMUtils.defineLazyServiceGetter(
  this,
  "clipboardHelper",
  "@mozilla.org/widget/clipboardhelper;1",
  Ci.nsIClipboardHelper
);

/** Type aInput into the address bar and press enter */
async function runMainTest(aInput, aDesc, aExpectedScheme) {
  await BrowserTestUtils.withNewTab("about:blank", async function (browser) {
    const loaded = BrowserTestUtils.browserLoaded(browser, false, null, true);
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: aInput,
    });
    EventUtils.synthesizeKey("KEY_Enter");
    await loaded;

    is(browser.currentURI.scheme, aExpectedScheme, "Main test: " + aDesc);
  });
}

/**
 * Type aInput into the address bar and press ctrl+enter,
 * resulting in the input being canonized first.
 * This should not change schemeless HTTPS behaviour.
 */
async function runCanonizedTest(aInput, aDesc, aExpectedScheme) {
  await BrowserTestUtils.withNewTab("about:blank", async function (browser) {
    const loaded = BrowserTestUtils.browserLoaded(browser, false, null, true);
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: aInput,
    });
    EventUtils.synthesizeKey("KEY_Enter", { ctrlKey: true });
    await loaded;

    is(browser.currentURI.scheme, aExpectedScheme, "Canonized test: " + aDesc);
  });
}

/**
 * Type aInput into the address bar and press alt+enter,
 * resulting in the input being loaded in a new tab.
 * This should not change schemeless HTTPS behaviour.
 */
async function runNewTabTest(aInput, aDesc, aExpectedScheme) {
  await BrowserTestUtils.withNewTab(
    "about:about", // For alt+enter to do anything, we need to be on a page other than about:blank.
    async function () {
      const newTabPromise = BrowserTestUtils.waitForNewTab(
        gBrowser,
        null,
        true
      );
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: aInput,
      });
      EventUtils.synthesizeKey("KEY_Enter", { altKey: true });
      const newTab = await newTabPromise;

      is(
        newTab.linkedBrowser.currentURI.scheme,
        aExpectedScheme,
        "New tab test: " + aDesc
      );

      BrowserTestUtils.removeTab(newTab);
    }
  );
}

/**
 * Type aInput into the address bar and press shift+enter,
 * resulting in the input being loaded in a new window.
 * This should not change schemeless HTTPS behaviour.
 */
async function runNewWindowTest(aInput, aDesc, aExpectedScheme) {
  await BrowserTestUtils.withNewTab("about:about", async function () {
    const newWindowPromise = BrowserTestUtils.waitForNewWindow({
      waitForAnyURLLoaded: true,
    });
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: aInput,
    });
    EventUtils.synthesizeKey("KEY_Enter", { shiftKey: true });
    const newWindow = await newWindowPromise;

    is(
      newWindow.gBrowser.selectedBrowser.currentURI.scheme,
      aExpectedScheme,
      "New window test: " + aDesc
    );

    await BrowserTestUtils.closeWindow(newWindow);
  });
}

/**
 * Instead of typing aInput into the address bar, copy it
 * to the clipboard and use the "Paste and Go" menu entry.
 * This should not change schemeless HTTPS behaviour.
 */
async function runPasteAndGoTest(aInput, aDesc, aExpectedScheme) {
  await BrowserTestUtils.withNewTab("about:blank", async function (browser) {
    gURLBar.focus();
    await SimpleTest.promiseClipboardChange(aInput, () => {
      clipboardHelper.copyString(aInput);
    });

    const loaded = BrowserTestUtils.browserLoaded(browser, false, null, true);
    const cxmenu = window.EditContextMenu.popup;
    const cxmenuPromise = BrowserTestUtils.waitForEvent(cxmenu, "popupshown");
    EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {
      type: "contextmenu",
      button: 2,
    });
    await cxmenuPromise;
    const menuitem = cxmenu.querySelector('[anonid="paste-and-go"]');
    menuitem.closest("menupopup").activateItem(menuitem);
    await loaded;

    is(
      browser.currentURI.scheme,
      aExpectedScheme,
      "Paste and go test: " + aDesc
    );
  });
}

async function runTest(aInput, aDesc, aExpectedScheme) {
  await runMainTest(aInput, aDesc, aExpectedScheme);
  await runCanonizedTest(aInput, aDesc, aExpectedScheme);
  await runNewTabTest(aInput, aDesc, aExpectedScheme);
  await runNewWindowTest(aInput, aDesc, aExpectedScheme);
  await runPasteAndGoTest(aInput, aDesc, aExpectedScheme);
}

add_task(async function () {
  requestLongerTimeout(10);
  // Typed visits to https://example.com, from earlier tests or an earlier run
  // of this one, would make the URL bar autofill "example.com" with https.
  await PlacesUtils.history.clear();
  registerCleanupFunction(() => PlacesUtils.history.clear());
  Services.fog.testResetFOG();

  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.security.https_first", false],
      ["dom.security.https_first_schemeless", false],
    ],
  });

  await runTest(
    "http://example.com",
    "Should not upgrade upgradeable website with explicit scheme",
    "http"
  );

  await runTest(
    "example.com",
    "Should not upgrade upgradeable website without explicit scheme",
    "http"
  );

  await SpecialPowers.pushPrefEnv({
    set: [["dom.security.https_first_schemeless", true]],
  });

  await runTest(
    "http://example.com",
    "Should not upgrade upgradeable website with explicit scheme",
    "http"
  );

  for (const key of [
    "upgraded",
    "upgradedSchemeless",
    "downgraded",
    "downgradedSchemeless",
    "downgradedOnTimer",
    "downgradedOnTimerSchemeless",
    "downgradeTime",
    "downgradeTimeSchemeless",
  ]) {
    is(
      Glean.httpsfirst[key].testGetValue(),
      null,
      `No telemetry should have been recorded yet for ${key}`
    );
  }

  await runTest(
    "example.com",
    "Should upgrade upgradeable website without explicit scheme",
    "https"
  );

  info("Checking expected telemetry");
  is(Glean.httpsfirst.upgraded.testGetValue(), null);
  is(Glean.httpsfirst.upgradedSchemeless.testGetValue(), 5);
  is(Glean.httpsfirst.downgraded.testGetValue(), null);
  is(Glean.httpsfirst.downgradedSchemeless.testGetValue(), null);
  is(Glean.httpsfirst.downgradedOnTimer.testGetValue(), null);
  is(Glean.httpsfirst.downgradedOnTimerSchemeless.testGetValue(), null);
  is(Glean.httpsfirst.downgradeTime.testGetValue(), null);
  is(Glean.httpsfirst.downgradeTimeSchemeless.testGetValue(), null);
});
