/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head.js */

// The `input` option of the test functions below is the string searched for in
// the session the sap is asserted on. Tests that need a specific kind of result,
// for example a Suggest result, pass a string matching it.

async function doSapTest(testFn, { private: isPrivate } = {}) {
  if (isPrivate) {
    await doTest(async function () {
      const win = await BrowserTestUtils.openNewBrowserWindow({
        private: true,
      });
      try {
        win.gURLBar.controller.session.reset();
        await testFn(win);
      } finally {
        await BrowserTestUtils.closeWindow(win);
      }
    });
  } else {
    await doTest(async function () {
      await testFn(window);
    });
  }
}

async function doUrlbarNewTabTest({
  trigger,
  assert,
  private: isPrivate,
  input = "x",
}) {
  await doSapTest(
    async function (win) {
      await openPopup(input, UrlbarTestUtils, win);
      await trigger(win);
      await assert();
    },
    { private: isPrivate }
  );
}

async function doUrlbarTest({
  trigger,
  assert,
  private: isPrivate,
  input = "x",
}) {
  await doSapTest(
    async function (win) {
      await openPopup(input, UrlbarTestUtils, win);
      await doEnter({}, win);
      await openPopup("y", UrlbarTestUtils, win);
      await trigger(win);
      await assert();
    },
    { private: isPrivate }
  );
}

async function doSearchbarTest({
  trigger,
  assert,
  private: isPrivate,
  input = "x",
}) {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.search.widget.new", true]],
  });

  await doSapTest(
    async function (win) {
      let cuiTestUtils = new CustomizableUITestUtils(win);
      await cuiTestUtils.addSearchBar();

      try {
        await openPopup(input, SearchbarTestUtils, win);
        await doEnter({}, win);
        await openPopup("y", SearchbarTestUtils, win);
        await trigger(win);
        await assert();
      } finally {
        await cuiTestUtils.removeSearchBar();
      }
    },
    { private: isPrivate }
  );

  await SpecialPowers.popPrefEnv();
}

async function doHandoffTest({
  trigger,
  assert,
  private: isPrivate,
  input = "x",
}) {
  // This SAP is the handoff search bar, which the newtab <moz-urlbar>
  // supersedes when its feature gate is on.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.newtab.featureGate", false]],
  });

  await doSapTest(
    async function (win) {
      const browser = win.gBrowser.selectedBrowser;
      const handoffPage = isPrivate ? "about:privatebrowsing" : "about:newtab";

      BrowserTestUtils.startLoadingURIString(browser, handoffPage);
      await BrowserTestUtils.browserLoaded(browser, false, handoffPage);
      // Ensure the urlbar is not focused so setHiddenFocus() calls focus() and
      // fires a focus event we can wait on.
      browser.focus();
      const onFocus = BrowserTestUtils.waitForEvent(
        win.gURLBar.inputField,
        "focus"
      );
      await SpecialPowers.spawn(browser, [], async function () {
        await ContentTaskUtils.waitForCondition(() =>
          content.document.querySelector("content-search-handoff-ui")
        );
        let handoffUI = content.document.querySelector(
          "content-search-handoff-ui"
        );
        await handoffUI.updateComplete;
        handoffUI.shadowRoot.querySelector(".fake-editable").click();
      });
      await onFocus;
      EventUtils.sendString(input, win);
      await UrlbarTestUtils.promiseSearchComplete(win);
      await trigger(win);
      await assert();
    },
    { private: isPrivate }
  );

  await SpecialPowers.popPrefEnv();
}

async function doUrlbarAddonpageTest({
  trigger,
  assert,
  private: isPrivate,
  input = "x",
}) {
  const extensionData = {
    files: {
      "page.html": "<!DOCTYPE html>hello",
    },
    ...(isPrivate ? { incognitoOverride: "spanning" } : {}),
  };
  const extension = ExtensionTestUtils.loadExtension(extensionData);
  await extension.startup();
  const extensionURL = `moz-extension://${extension.uuid}/page.html`;

  await doSapTest(
    async function (win) {
      const browser = win.gBrowser.selectedBrowser;
      const onLoad = BrowserTestUtils.browserLoaded(browser);
      BrowserTestUtils.startLoadingURIString(browser, extensionURL);
      await onLoad;
      await openPopup(input, UrlbarTestUtils, win);
      await trigger(win);
      await assert();
    },
    { private: isPrivate }
  );

  await extension.unload();
}
