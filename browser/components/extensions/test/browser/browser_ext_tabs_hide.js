"use strict";

ChromeUtils.defineESModuleGetters(this, {
  ExtensionControlledPopup:
    "resource:///modules/ExtensionControlledPopup.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  TabStateFlusher:
    "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs",
});

const triggeringPrincipal_base64 = E10SUtils.SERIALIZED_SYSTEMPRINCIPAL;

async function doorhangerTest(testFn, manifestProps = {}) {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabs", "tabHide"],
      icons: {
        48: "addon-icon.png",
      },
      ...manifestProps,
    },
    background() {
      browser.test.onMessage.addListener(async (msg, data) => {
        let tabs = await browser.tabs.query(data);
        await browser.tabs[msg](tabs.map(t => t.id));
        browser.test.sendMessage("done");
      });
    },
    useAddonManager: "temporary",
  });

  await extension.startup();

  // Open some tabs so we can hide them.
  let firstTab = gBrowser.selectedTab;
  let tabs = [
    await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "http://example.com/?one",
      true,
      true
    ),
    await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "http://example.com/?two",
      true,
      true
    ),
  ];
  gBrowser.selectedTab = firstTab;

  await testFn(extension);

  BrowserTestUtils.removeTab(tabs[0]);
  BrowserTestUtils.removeTab(tabs[1]);

  await extension.unload();
}

add_task(function test_doorhanger_keep() {
  return doorhangerTest(async function (extension) {
    is(gBrowser.visibleTabs.length, 3, "There are 3 visible tabs");

    // Hide the first tab, expect the doorhanger.
    let panel = ExtensionControlledPopup._getAndMaybeCreatePanel(document);
    let popupShown = promisePopupShown(panel);
    extension.sendMessage("hide", { url: "*://*/?one" });
    await extension.awaitMessage("done");
    await popupShown;

    is(gBrowser.visibleTabs.length, 2, "There are 2 visible tabs now");
    is(
      panel.anchorNode.closest("toolbarbutton").id,
      "unified-extensions-button",
      "The doorhanger is anchored to the extensions button"
    );

    let description = panel.querySelector(
      "#extension-tab-hide-notification-description"
    );
    is(
      description.textContent,
      "An extension,  Generated extension, is hiding some of your tabs. You can still access all of your tabs from .",
      "The extension name is in the description"
    );

    const popupnotification = document.getElementById(
      "extension-tab-hide-notification"
    );
    const learnMoreEl = popupnotification.querySelector(
      ".popup-notification-learnmore-link"
    );
    ok(
      learnMoreEl,
      "Expect the popupnotification learnmore link to be visible"
    );

    is(
      learnMoreEl.getAttribute("href"),
      Services.urlFormatter.formatURLPref("app.support.baseURL") +
        "extension-hiding-tabs",
      "learnmore link should have the expected url set"
    );

    // Click the Keep Tabs Hidden button.
    let popupHidden = promisePopupHidden(panel);
    popupnotification.button.click();
    await popupHidden;

    // Hide another tab and ensure the popup didn't open.
    extension.sendMessage("hide", { url: "*://*/?two" });
    await extension.awaitMessage("done");
    is(panel.state, "closed", "The popup is still closed");
    is(gBrowser.visibleTabs.length, 1, "There's one visible tab now");

    extension.sendMessage("show", {});
    await extension.awaitMessage("done");
  });
});

add_task(function test_doorhanger_alltabs_button_in_menubar() {
  if (Services.appinfo.nativeMenubar) {
    info("skipping test because the menubar is not customizable");
    return;
  }

  return doorhangerTest(async function (extension) {
    is(gBrowser.visibleTabs.length, 3, "There are 3 visible tabs");

    CustomizableUI.addWidgetToArea(
      "alltabs-button",
      CustomizableUI.AREA_MENUBAR
    );

    is(
      CustomizableUI.getPlacementOfWidget("alltabs-button").area,
      CustomizableUI.AREA_MENUBAR,
      "alltabs-button is in the menubar"
    );

    // Hide the first tab, expect the doorhanger.
    let panel = ExtensionControlledPopup._getAndMaybeCreatePanel(document);
    let popupShown = promisePopupShown(panel);
    extension.sendMessage("hide", { url: "*://*/?one" });
    await extension.awaitMessage("done");
    await popupShown;

    is(
      CustomizableUI.getPlacementOfWidget("alltabs-button").area,
      CustomizableUI.AREA_TABSTRIP,
      "alltabs-button has been moved back to the tabstrip"
    );

    is(gBrowser.visibleTabs.length, 2, "There are 2 visible tabs now");
  });
});

add_task(function test_doorhanger_alltabs_button_in_personaltoolbar() {
  return doorhangerTest(async function (extension) {
    is(gBrowser.visibleTabs.length, 3, "There are 3 visible tabs");

    CustomizableUI.addWidgetToArea(
      "alltabs-button",
      CustomizableUI.AREA_BOOKMARKS
    );

    is(
      CustomizableUI.getPlacementOfWidget("alltabs-button").area,
      CustomizableUI.AREA_BOOKMARKS,
      "alltabs-button is in the bookmarks bar"
    );

    // Hide the first tab, expect the doorhanger.
    let panel = ExtensionControlledPopup._getAndMaybeCreatePanel(document);
    let popupShown = promisePopupShown(panel);
    extension.sendMessage("hide", { url: "*://*/?one" });
    await extension.awaitMessage("done");
    await popupShown;

    is(
      CustomizableUI.getPlacementOfWidget("alltabs-button").area,
      CustomizableUI.AREA_TABSTRIP,
      "alltabs-button has been moved back to the tabstrip"
    );

    is(gBrowser.visibleTabs.length, 2, "There are 2 visible tabs now");
  });
});

add_task(function test_doorhanger_alltabs_button_in_overflow_menu() {
  return doorhangerTest(async function (extension) {
    is(gBrowser.visibleTabs.length, 3, "There are 3 visible tabs");

    CustomizableUI.addWidgetToArea(
      "alltabs-button",
      CustomizableUI.AREA_FIXED_OVERFLOW_PANEL
    );

    is(
      CustomizableUI.getPlacementOfWidget("alltabs-button").area,
      CustomizableUI.AREA_FIXED_OVERFLOW_PANEL,
      "alltabs-button is in the overflow menu"
    );

    // Hide the first tab, expect the doorhanger.
    let panel = ExtensionControlledPopup._getAndMaybeCreatePanel(document);
    let popupShown = promisePopupShown(panel);
    extension.sendMessage("hide", { url: "*://*/?one" });
    await extension.awaitMessage("done");
    await popupShown;

    is(
      CustomizableUI.getPlacementOfWidget("alltabs-button").area,
      CustomizableUI.AREA_TABSTRIP,
      "alltabs-button has been moved back to the tabstrip"
    );

    is(gBrowser.visibleTabs.length, 2, "There are 2 visible tabs now");
  });
});

add_task(function test_doorhanger_alltabs_button_removed() {
  return doorhangerTest(async function (extension) {
    is(gBrowser.visibleTabs.length, 3, "There are 3 visible tabs");

    CustomizableUI.removeWidgetFromArea("alltabs-button");

    is(
      CustomizableUI.getPlacementOfWidget("alltabs-button"),
      null,
      "alltabs-button is removed"
    );

    // Hide the first tab, expect the doorhanger.
    let panel = ExtensionControlledPopup._getAndMaybeCreatePanel(document);
    let popupShown = promisePopupShown(panel);
    extension.sendMessage("hide", { url: "*://*/?one" });
    await extension.awaitMessage("done");
    await popupShown;

    is(
      CustomizableUI.getPlacementOfWidget("alltabs-button").area,
      CustomizableUI.AREA_TABSTRIP,
      "alltabs-button has been moved back to the tabstrip"
    );

    is(gBrowser.visibleTabs.length, 2, "There are 2 visible tabs now");
  });
});

const doorHangerDisable = (
  manifestProps = {},
  expectedAnchorID = "unified-extensions-button"
) => {
  return doorhangerTest(async function (extension) {
    is(gBrowser.visibleTabs.length, 3, "There are 3 visible tabs");

    // Hide the first tab, expect the doorhanger.
    let panel = ExtensionControlledPopup._getAndMaybeCreatePanel(document);
    let popupShown = promisePopupShown(panel);
    extension.sendMessage("hide", { url: "*://*/?one" });
    await extension.awaitMessage("done");
    await popupShown;

    is(gBrowser.visibleTabs.length, 2, "There are 2 visible tabs now");
    is(
      panel.anchorNode.closest("toolbarbutton").id,
      expectedAnchorID,
      "The doorhanger is anchored to the right element"
    );

    // verify the contents of the description.
    let popupnotification = document.getElementById(
      "extension-tab-hide-notification"
    );
    let description = popupnotification.querySelector(
      "#extension-tab-hide-notification-description"
    );
    let addon = await AddonManager.getAddonByID(extension.id);
    ok(
      description.textContent.includes(addon.name),
      "The extension name is in the description"
    );
    let images = Array.from(description.querySelectorAll("image"));
    is(images.length, 2, "There are two images");
    ok(
      images.some(img => img.src.includes("addon-icon.png")),
      "There's an icon for the extension"
    );
    ok(
      images.some(img =>
        getComputedStyle(img).backgroundImage.includes(
          gBrowser.tabContainer.verticalMode ? "/tabs.svg" : "/arrow-down.svg"
        )
      ),
      "There's an icon for the all tabs menu"
    );

    // Click the Disable Extension button.
    let popupHidden = promisePopupHidden(panel);
    popupnotification.secondaryButton.click();
    await popupHidden;
    await new Promise(executeSoon);

    is(gBrowser.visibleTabs.length, 3, "There are 3 visible tabs again");
    is(addon.userDisabled, true, "The extension is now disabled");
  }, manifestProps);
};

add_task(async function test_doorhanger_disable() {
  await doorHangerDisable();
});

add_task(async function test_no_doorhanger_when_locked_by_policy() {
  const { EnterprisePolicyTesting } = ChromeUtils.importESModule(
    "resource://testing-common/EnterprisePolicyTesting.sys.mjs"
  );
  const ID = "tabhide-locked@mochi.test";

  await doorhangerTest(
    async function (extension) {
      let panel = ExtensionControlledPopup._getAndMaybeCreatePanel(document);
      is(gBrowser.visibleTabs.length, 3, "There are 3 visible tabs");

      await EnterprisePolicyTesting.setupPolicyEngineWithJson({
        policies: { Extensions: { Locked: [ID] } },
      });

      extension.sendMessage("hide", { url: "*://*/?one" });
      await extension.awaitMessage("done");
      is(gBrowser.visibleTabs.length, 2, "There are 2 visible tabs now");
      is(panel.state, "closed", "No doorhanger while the add-on is locked");

      // Dropping the policy lets the doorhanger appear again.
      await EnterprisePolicyTesting.setupPolicyEngineWithJson("");

      let popupShown = promisePopupShown(panel);
      extension.sendMessage("hide", { url: "*://*/?two" });
      await extension.awaitMessage("done");
      await popupShown;
      is(gBrowser.visibleTabs.length, 1, "There is 1 visible tab now");

      let popupHidden = promisePopupHidden(panel);
      panel.hidePopup();
      await popupHidden;
    },
    { browser_specific_settings: { gecko: { id: ID } } }
  );
});

add_task(async function test_doorhanger_disable_with_browser_action() {
  await doorHangerDisable(
    {
      browser_action: { default_area: "menupanel" },
    }
    // We expect to anchor the popup to the extensions button (the default)
    // because the extension is placed in the extensions panel.
  );
});

add_task(
  async function test_doorhanger_disable_with_browser_action_in_navbar() {
    const id = "@some-ext-id";

    await doorHangerDisable(
      {
        browser_specific_settings: { gecko: { id } },
        browser_action: { default_area: "navbar" },
      },
      // We expect to anchor the popup to the widget because it is placed in the
      // nav-bar.
      "_some-ext-id-BAP"
    );
  }
);

add_task(async function test_tabs_showhide() {
  async function background() {
    browser.test.onMessage.addListener(async msg => {
      switch (msg) {
        case "hideall": {
          let tabs = await browser.tabs.query({ hidden: false });
          browser.test.assertEq(tabs.length, 5, "got 5 tabs");
          let ids = tabs.map(tab => tab.id);
          browser.test.log(`working with ids ${JSON.stringify(ids)}`);

          let hidden = await browser.tabs.hide(ids);
          browser.test.assertEq(hidden.length, 3, "hid 3 tabs");
          tabs = await browser.tabs.query({ hidden: true });
          ids = tabs.map(tab => tab.id);
          browser.test.assertEq(
            JSON.stringify(hidden.sort()),
            JSON.stringify(ids.sort()),
            "hidden tabIds match"
          );

          browser.test.sendMessage("hidden", { hidden });
          break;
        }
        case "showall": {
          let tabs = await browser.tabs.query({ hidden: true });
          for (let tab of tabs) {
            browser.test.assertTrue(tab.hidden, "tab is hidden");
          }
          let ids = tabs.map(tab => tab.id);
          browser.tabs.show(ids);
          browser.test.sendMessage("shown");
          break;
        }
      }
    });
  }

  let extdata = {
    manifest: { permissions: ["tabs", "tabHide"] },
    background,
    useAddonManager: "temporary", // So the doorhanger can find the addon.
  };
  let extension = ExtensionTestUtils.loadExtension(extdata);
  await extension.startup();

  let sessData = {
    windows: [
      {
        tabs: [
          { entries: [{ url: "about:blank", triggeringPrincipal_base64 }] },
          {
            entries: [
              { url: "https://example.com/", triggeringPrincipal_base64 },
            ],
          },
          {
            entries: [
              { url: "https://mochi.test:8888/", triggeringPrincipal_base64 },
            ],
          },
        ],
      },
      {
        tabs: [
          { entries: [{ url: "about:blank", triggeringPrincipal_base64 }] },
          {
            entries: [
              { url: "http://test1.example.com/", triggeringPrincipal_base64 },
            ],
          },
        ],
      },
    ],
  };

  info("Set up a test session with 2 windows and 5 tabs.");
  let oldState = SessionStore.getBrowserState();
  let restored = TestUtils.topicObserved("sessionstore-browser-state-restored");
  SessionStore.setBrowserState(JSON.stringify(sessData));
  await restored;

  info(
    "Attempt to hide all the tabs, however the active tab in each window cannot be hidden, so the result will be 3 hidden tabs."
  );
  extension.sendMessage("hideall");
  await extension.awaitMessage("hidden");

  info(
    "We have 2 windows in this session.  Otherwin is the non-current window. " +
      "In each window, the first tab will be the selected tab and should not be hidden. " +
      "The rest of the tabs should be hidden at this point.  Hidden status was already" +
      "validated inside the extension, this double checks from chrome code."
  );
  let otherwin;
  for (let win of BrowserWindowIterator()) {
    if (win != window) {
      otherwin = win;
    }
    let tabs = Array.from(win.gBrowser.tabs);
    ok(!tabs[0].hidden, "first tab not hidden");
    for (let i = 1; i < tabs.length; i++) {
      ok(tabs[i].hidden, "tab hidden value is correct");
      let id = SessionStore.getCustomTabValue(tabs[i], "hiddenBy");
      is(id, extension.id, "tab hiddenBy value is correct");
      await TabStateFlusher.flush(tabs[i].linkedBrowser);
    }

    let allTabsButton = win.document.getElementById("alltabs-button");
    isnot(
      getComputedStyle(allTabsButton).display,
      "none",
      "The all tabs button is visible"
    );
  }

  info(
    "Close the other window then restore it to test that the tabs are restored with proper hidden state, and the correct extension id."
  );
  await BrowserTestUtils.closeWindow(otherwin);

  otherwin = SessionStore.undoCloseWindow(0);
  await BrowserTestUtils.waitForEvent(otherwin, "load");
  let tabs = Array.from(otherwin.gBrowser.tabs);
  ok(!tabs[0].hidden, "first tab not hidden");
  for (let i = 1; i < tabs.length; i++) {
    ok(tabs[i].hidden, "tab hidden value is correct");
    let id = SessionStore.getCustomTabValue(tabs[i], "hiddenBy");
    is(id, extension.id, "tab hiddenBy value is correct");
  }

  info("Showall will unhide any remaining hidden tabs.");
  extension.sendMessage("showall");
  await extension.awaitMessage("shown");

  info("Check from chrome code that all tabs are visible again.");
  for (let win of BrowserWindowIterator()) {
    let tabs = Array.from(win.gBrowser.tabs);
    for (let i = 0; i < tabs.length; i++) {
      ok(!tabs[i].hidden, "tab hidden value is correct");
    }
  }

  info("Close second window.");
  await BrowserTestUtils.closeWindow(otherwin);

  await extension.unload();

  info("Restore pre-test state.");
  restored = TestUtils.topicObserved("sessionstore-browser-state-restored");
  SessionStore.setBrowserState(oldState);
  await restored;
});

// Test our shutdown handling.  Currently this means any hidden tabs will be
// shown when a tabHide extension is shutdown.  We additionally test the
// tabs.onUpdated listener gets called with hidden state changes.
add_task(async function test_tabs_shutdown() {
  let tabs = [
    await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "http://example.com/",
      true,
      true
    ),
    await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "http://mochi.test:8888/",
      true,
      true
    ),
  ];

  async function background() {
    let tabs = await browser.tabs.query({ url: "http://example.com/" });
    let testTab = tabs[0];

    browser.tabs.onUpdated.addListener((tabId, changeInfo, tab) => {
      if ("hidden" in changeInfo) {
        browser.test.assertEq(tabId, testTab.id, "correct tab was hidden");
        browser.test.assertTrue(changeInfo.hidden, "tab is hidden");
        browser.test.assertEq(tab.url, testTab.url, "tab has correct URL");
        browser.test.sendMessage("changeInfo");
      }
    });

    let hidden = await browser.tabs.hide(testTab.id);
    browser.test.assertEq(hidden[0], testTab.id, "tab was hidden");
    tabs = await browser.tabs.query({ hidden: true });
    browser.test.assertEq(tabs[0].id, testTab.id, "tab was hidden");
    browser.test.sendMessage("ready");
  }

  let extdata = {
    manifest: { permissions: ["tabs", "tabHide"] },
    useAddonManager: "temporary", // For testing onShutdown.
    background,
  };
  let extension = ExtensionTestUtils.loadExtension(extdata);
  await extension.startup();

  info("test onUpdated");
  await Promise.all([
    extension.awaitMessage("ready"),
    extension.awaitMessage("changeInfo"),
  ]);
  Assert.ok(tabs[0].hidden, "Tab is hidden by extension");

  await extension.unload();

  Assert.ok(!tabs[0].hidden, "Tab is not hidden after unloading extension");
  BrowserTestUtils.removeTab(tabs[0]);
  BrowserTestUtils.removeTab(tabs[1]);
});
