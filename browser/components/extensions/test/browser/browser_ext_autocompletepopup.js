"use strict";

add_task(async function testAutocompletePopup() {
  let extension = ExtensionTestUtils.loadExtension({
    useAddonManager: "temporary",
    manifest: {
      browser_action: {
        default_popup: "page.html",
        browser_style: false,
      },
      page_action: {
        default_popup: "page.html",
        browser_style: false,
      },
      options_ui: {
        page: "page.html",
      },
    },
    background: async function () {
      let [tab] = await browser.tabs.query({
        active: true,
        currentWindow: true,
      });
      await browser.pageAction.show(tab.id);
      browser.test.sendMessage("ready");
    },
    files: {
      "page.html": `<!DOCTYPE html>
        <html>
          <head><meta charset="utf-8"></head>
          <body>
          <div>
          <input placeholder="Test input" id="test-input" list="test-list" />
          <datalist id="test-list">
            <option value="aa">
            <option value="ab">
            <option value="ae">
            <option value="af">
            <option value="ak">
            <option value="am">
            <option value="an">
            <option value="ar">
          </datalist>
          </div>
          <script src="page.js"></script>
          </body>
        </html>`,
      "page.js": () => browser.test.sendMessage("page_loaded"),
    },
  });

  async function testDatalist(browser, doc) {
    // A click only opens the datalist popup of an input that is already
    // focused, and focusing the browser of a just-opened popup panel reaches
    // the extension process asynchronously. Without waiting for that, both
    // clicks below can be dispatched before the input is focusable, neither
    // opens the popup, and the test times out.
    await SimpleTest.promiseFocus(browser);

    let autocompletePopup = doc.getElementById("PopupAutoComplete");
    let opened = promisePopupShown(autocompletePopup);
    info("click in test-input now");
    // two clicks to open
    await BrowserTestUtils.synthesizeMouseAtCenter("#test-input", {}, browser);
    await BrowserTestUtils.synthesizeMouseAtCenter("#test-input", {}, browser);
    info("wait for opened event");
    await opened;
    // third to close
    let closed = promisePopupHidden(autocompletePopup);
    info("click in test-input now");
    await BrowserTestUtils.synthesizeMouseAtCenter("#test-input", {}, browser);
    info("wait for closed event");
    await closed;
    // If this didn't work, we hang. Other tests deal with testing the actual functionality of datalist.
    ok(true, "datalist popup has been shown");
  }
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "http://example.com/"
  );
  await extension.startup();
  await extension.awaitMessage("ready");

  info("Testing autocomplete in pageAction popup panel");
  clickPageAction(extension);
  // intentional misspell so eslint is ok with browser in background script.
  let bowser = await awaitExtensionPanel(extension);
  ok(!!bowser, "panel opened with browser");
  await extension.awaitMessage("page_loaded");
  await testDatalist(bowser, document);
  await closePageAction(extension);
  await new Promise(resolve => setTimeout(resolve, 0));

  info("Testing autocomplete in browserAction popup panel");
  clickBrowserAction(extension);
  bowser = await awaitExtensionPanel(extension);
  ok(!!bowser, "panel opened with browser");
  await extension.awaitMessage("page_loaded");
  await testDatalist(bowser, document);
  await closeBrowserAction(extension);
  await new Promise(resolve => setTimeout(resolve, 0));

  info("Testing autocomplete in embedded options page in about:addons");
  // Open new window for about:addons to be loaded in, as opening about:addons
  // may consume an existing tab and we don't want that.
  let browserWindow = await BrowserTestUtils.openNewBrowserWindow();
  // Opens about:addons and returns its content window:
  let aboutaddonsWin = await browserWindow.BrowserAddonUI.openAddonsMgr(
    `addons://detail/${encodeURIComponent(extension.id)}/preferences`
  );
  is(
    browserWindow.gBrowser.selectedTab.linkedBrowser.currentURI.spec,
    "about:addons",
    "about:addons opened in the browser window"
  );
  await extension.awaitMessage("page_loaded");
  let optionsBrowser = aboutaddonsWin.document.getElementById(
    "addon-inline-options"
  );
  is(
    optionsBrowser.currentURI.spec,
    `moz-extension://${extension.uuid}/page.html`,
    "options_ui document from extension is loaded in about:addons"
  );
  // The detail view focuses its "more options" button in an animation frame
  // after rendering. Wait for it, or it can take the focus away from the
  // options browser after testDatalist focused it, and the clicks don't open
  // the popup.
  let moreOptionsButton = aboutaddonsWin.document.querySelector(
    "addon-card .more-options-button"
  );
  if (aboutaddonsWin.document.activeElement != moreOptionsButton) {
    await BrowserTestUtils.waitForEvent(moreOptionsButton, "focus");
  }
  await testDatalist(optionsBrowser, browserWindow.document);

  await BrowserTestUtils.closeWindow(browserWindow);

  await extension.unload();
  BrowserTestUtils.removeTab(tab);
});
