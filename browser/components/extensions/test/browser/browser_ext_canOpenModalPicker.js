"use strict";

// This file tests the state of the browsingContext.canOpenModalPicker flag,
// which reflects whether the document within is allowed to open a file picker.

add_task(async function test_canOpenModalPicker_in_tab() {
  let extension = ExtensionTestUtils.loadExtension({
    files: {
      "tab.html": "Nothing special here",
    },
  });
  await extension.startup();
  const extensionTabUrl = `moz-extension://${extension.uuid}/tab.html`;

  let firstTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    extensionTabUrl
  );
  is(
    firstTab.linkedBrowser.browsingContext.canOpenModalPicker,
    true,
    "Focused extension tab can open modal picker"
  );
  await BrowserTestUtils.withNewTab(extensionTabUrl, browser => {
    is(
      browser.browsingContext.canOpenModalPicker,
      true,
      "Newly focused extension tab can open modal picker"
    );
    is(
      firstTab.linkedBrowser.browsingContext.canOpenModalPicker,
      false,
      "Background tab cannot open modal picker"
    );
  });
  is(
    firstTab.linkedBrowser.browsingContext.canOpenModalPicker,
    true,
    "Re-focused extension tab can open modal picker"
  );
  BrowserTestUtils.removeTab(firstTab);
  await extension.unload();
});

// This test verifies that canOpenModalPicker is set for extension sidebar
// documents iff the document is focused (regression test for bug 1949092).
// It also verifies that canOpenModalPicker of regular tab browsers are focused
// iff the tab browser is focused (unit test for bug 1837963).
add_task(async function test_canOpenModalPicker_in_sidebar() {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      sidebar_action: {
        default_panel: "sidebar.html",
      },
    },
    useAddonManager: "temporary",
    files: {
      "sidebar.html": `
        <!DOCTYPE html>
        <meta charset="utf-8">
        <body>
        Click in this extension document to focus it and start the test.
        <script src="sidebar.js"></script>
      `,
      "sidebar.js": () => {
        document.body.onclick = () => {
          browser.test.sendMessage("clicked_sidebar_ext_doc");
        };
        window.onload = () => {
          browser.test.sendMessage("sidebar_ready");
        };
      },
    },
  });

  await extension.startup();
  await extension.awaitMessage("sidebar_ready");

  // <browser> with webext-panels.xhtml contains <browser> with moz-extension:.
  let sidebarBrowser = SidebarController.browser;
  is(
    sidebarBrowser.currentURI.spec,
    "chrome://browser/content/webext-panels.xhtml",
    "Sidebar wrapper loaded in sidebar browser"
  );
  let extSidebarBrowser = sidebarBrowser.contentDocument.getElementById(
    "webext-panels-browser"
  );
  is(
    extSidebarBrowser.currentURI.spec,
    `moz-extension://${extension.uuid}/sidebar.html`,
    "Sidebar document from extension is loaded in sidebar wrapper"
  );

  // Wait for layout to be stable before triggering click.
  await sidebarBrowser.contentWindow.promiseDocumentFlushed(() => {});

  info("Moving focus to sidebar content");
  await SimpleTest.promiseFocus(extSidebarBrowser);
  await BrowserTestUtils.synthesizeMouseAtCenter("body", {}, extSidebarBrowser);
  await extension.awaitMessage("clicked_sidebar_ext_doc");

  is(
    extSidebarBrowser.browsingContext.canOpenModalPicker,
    true,
    "Focused sidebar can open modal picker"
  );

  info("Moving focus off sidebar, by opening and focusing a new tab");
  await BrowserTestUtils.withNewTab("about:blank", async browser => {
    is(
      browser.browsingContext.canOpenModalPicker,
      true,
      "Focused tab can open modal picker"
    );
    is(
      extSidebarBrowser.browsingContext.canOpenModalPicker,
      false,
      "Sidebar is no longer focused and cannot open modal picker"
    );
  });

  await extension.unload();
});

// This test verifies that canOpenModalPicker is set for options_ui pages in
// about:addons iff the document is focused (regression test for bug 1949092).
add_task(async function test_canOpenModalPicker_in_options_ui() {
  // Open new window for about:addons to be loaded in.
  let browserWindow = await BrowserTestUtils.openNewBrowserWindow();

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      options_ui: {
        page: "options.html",
      },
    },
    useAddonManager: "temporary",
    background() {
      browser.test.log("Opening about:addons");
      browser.runtime.openOptionsPage();
    },
    files: {
      "options.html": `
        <!DOCTYPE html>
        <meta charset="utf-8">
        <body>
        Click in this extension document to focus it and start the test.
        <script src="options.js"></script>
      `,
      "options.js": () => {
        document.body.onclick = () => {
          browser.test.sendMessage("clicked_options_ui_doc");
        };
        window.onload = () => {
          browser.test.sendMessage("options_ready");
        };
      },
    },
  });

  await extension.startup();
  await extension.awaitMessage("options_ready");

  let aboutaddonsBrowser = browserWindow.gBrowser.selectedBrowser;
  is(
    aboutaddonsBrowser.currentURI.spec,
    "about:addons",
    "openOptionsPage() opened about:addons tab"
  );

  // AddonCard calls optionsButton.focus() from a rAF callback.
  // Wait for that to have happened, so that it cannot steal focus from the
  // options browser later in this test.
  let moreOptionsButton = aboutaddonsBrowser.contentDocument.querySelector(
    "addon-card .more-options-button"
  );
  if (aboutaddonsBrowser.contentDocument.activeElement != moreOptionsButton) {
    await BrowserTestUtils.waitForEvent(moreOptionsButton, "focus");
  }

  let optionsBrowser = aboutaddonsBrowser.contentDocument.getElementById(
    "addon-inline-options"
  );
  is(
    optionsBrowser.currentURI.spec,
    `moz-extension://${extension.uuid}/options.html`,
    "options_ui document from extension is loaded in about:addons"
  );

  is(
    optionsBrowser.browsingContext.canOpenModalPicker,
    false,
    "Unfocused options_ui page cannot open modal picker"
  );

  await SimpleTest.promiseFocus(optionsBrowser);
  await BrowserTestUtils.synthesizeMouseAtCenter("body", {}, optionsBrowser);
  await extension.awaitMessage("clicked_options_ui_doc");

  is(
    optionsBrowser.browsingContext.canOpenModalPicker,
    true,
    "Focused options_ui page can open modal picker"
  );

  await BrowserTestUtils.closeWindow(browserWindow);

  await extension.unload();
});

async function testPickerInExtensionBackgroundPage({
  expectedCanOpenWithoutClick,
  expectedCanOpenAfterClick,
}) {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_action: {},
    },
    background() {
      browser.browserAction.onClicked.addListener(() => {
        browser.test.sendMessage("clicked_browserAction");
      });
    },
  });
  await extension.startup();
  let bgBrowsingContext;
  for (let view of WebExtensionPolicy.getByID(extension.id).extension.views) {
    if (view.viewType === "background") {
      bgBrowsingContext = view.browsingContext;
    }
  }
  is(
    bgBrowsingContext.canOpenModalPicker,
    expectedCanOpenWithoutClick,
    "Extension background script has expected canOpenModalPicker before click"
  );
  await clickBrowserAction(extension);
  await extension.awaitMessage("clicked_browserAction");
  is(
    bgBrowsingContext.canOpenModalPicker,
    expectedCanOpenAfterClick,
    `Extension background script has expected canOpenModalPicker after click`
  );
  await extension.unload();
}

add_task(async function test_canOpenModalPicker_in_background_pref_default() {
  is(
    Services.prefs.getBoolPref(
      "browser.disable_pickers_in_hidden_extension_pages"
    ),
    AppConstants.NIGHTLY_BUILD,
    "Testing default of browser.disable_pickers_in_hidden_extension_pages pref"
  );
});

add_task(async function test_canOpenModalPicker_in_background_pref_true() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.disable_pickers_in_hidden_extension_pages", true]],
  });
  await testPickerInExtensionBackgroundPage({
    expectedCanOpenWithoutClick: false,
    expectedCanOpenAfterClick: false,
  });
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_canOpenModalPicker_in_background_pref_false() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.disable_pickers_in_hidden_extension_pages", false]],
  });
  await testPickerInExtensionBackgroundPage({
    expectedCanOpenWithoutClick: true,
    expectedCanOpenAfterClick: true,
  });
  await SpecialPowers.popPrefEnv();
});
