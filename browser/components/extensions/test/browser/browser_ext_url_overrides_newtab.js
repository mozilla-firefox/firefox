"use strict";

requestLongerTimeout(4);

ChromeUtils.defineESModuleGetters(this, {
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  ExtensionControlledPopup:
    "resource:///modules/ExtensionControlledPopup.sys.mjs",
  ExtensionSettingsStore:
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs",
});

function getNotificationSetting(extensionId) {
  return ExtensionSettingsStore.getSetting("newTabNotification", extensionId);
}

function getNewTabDoorhanger() {
  ExtensionControlledPopup._getAndMaybeCreatePanel(document);
  return document.getElementById("extension-new-tab-notification");
}

function clickKeepChanges(notification) {
  notification.button.click();
}

function clickManage(notification) {
  notification.secondaryButton.click();
}

async function promiseNewTab(expectUrl = AboutNewTab.newTabURL, win = window) {
  let eventName = "browser-open-newtab-start";
  let newTabStartPromise = new Promise(resolve => {
    async function observer(subject) {
      Services.obs.removeObserver(observer, eventName);
      resolve(subject.wrappedJSObject);
    }
    Services.obs.addObserver(observer, eventName);
  });

  let newtabShown = TestUtils.waitForCondition(
    () => win.gBrowser.currentURI.spec == expectUrl,
    `Should open correct new tab url ${expectUrl}.`
  );

  win.BrowserCommands.openTab();
  const newTabCreatedPromise = newTabStartPromise;
  const browser = await newTabCreatedPromise;
  await newtabShown;
  const tab = win.gBrowser.selectedTab;

  // Wait for the New Tab page to be done loading. While it is still loading the
  // tab is not "empty" as far as switchToTabHavingURI is concerned, so clicking
  // the doorhanger's Manage button would open about:preferences in a new tab
  // instead of replacing the New Tab page in this one.
  await BrowserTestUtils.waitForMutationCondition(
    tab,
    { attributes: true, attributeFilter: ["busy"] },
    () => !tab.hasAttribute("busy"),
    { msg: `Should finish loading ${expectUrl}.` }
  );
  // The wait above can resolve from the mutation callback, which runs while
  // tabbrowser is still handling that busy removal and still needs the tab.
  // Yield first, so callers can remove the tab without tripping over it.
  await TestUtils.waitForTick();

  Assert.deepEqual(
    browser,
    tab.linkedBrowser,
    "browser-open-newtab-start notified with the created browser"
  );
  return tab;
}

function waitForAddonDisabled(addon) {
  return new Promise(resolve => {
    let listener = {
      onDisabled(disabledAddon) {
        if (disabledAddon.id == addon.id) {
          resolve();
          AddonManager.removeAddonListener(listener);
        }
      },
    };
    AddonManager.addAddonListener(listener);
  });
}

function waitForAddonEnabled(addon) {
  return new Promise(resolve => {
    let listener = {
      onEnabled(enabledAddon) {
        if (enabledAddon.id == addon.id) {
          AddonManager.removeAddonListener(listener);
          resolve();
        }
      },
    };
    AddonManager.addAddonListener(listener);
  });
}

// Default test extension data for newtab.
const extensionData = {
  manifest: {
    browser_specific_settings: {
      gecko: {
        id: "newtaburl@mochi.test",
      },
    },
    chrome_url_overrides: {
      newtab: "newtab.html",
    },
  },
  files: {
    "newtab.html": "<h1>New tab!</h1>",
  },
  useAddonManager: "temporary",
};

add_task(async function test_new_tab_opens() {
  let panel = getNewTabDoorhanger().closest("panel");
  let extension = ExtensionTestUtils.loadExtension(extensionData);

  await extension.startup();
  let extensionNewTabUrl = `moz-extension://${extension.uuid}/newtab.html`;

  // Simulate opening the newtab open as a user would.
  let popupShown = promisePopupShown(panel);
  let tab = await promiseNewTab(extensionNewTabUrl);
  await popupShown;

  // This will show a confirmation doorhanger, make sure we don't leave it open.
  let popupHidden = promisePopupHidden(panel);
  panel.hidePopup();
  await popupHidden;

  BrowserTestUtils.removeTab(tab);
  await extension.unload();
});

add_task(async function test_new_tab_ignore_settings() {
  await ExtensionSettingsStore.initialize();
  let notification = getNewTabDoorhanger();
  let panel = notification.closest("panel");
  let extensionId = "newtabignore@mochi.test";
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_specific_settings: { gecko: { id: extensionId } },
      browser_action: {
        default_popup: "ignore.html",
        default_area: "navbar",
      },
      chrome_url_overrides: { newtab: "ignore.html" },
    },
    files: { "ignore.html": '<h1 id="extension-new-tab">New Tab!</h1>' },
    useAddonManager: "temporary",
  });

  Assert.notEqual(
    panel.getAttribute("panelopen"),
    "true",
    "The notification panel is initially closed"
  );

  await extension.startup();

  // Simulate opening the New Tab as a user would.
  let popupShown = promisePopupShown(panel);
  let tab = await promiseNewTab();
  await popupShown;

  // Ensure the doorhanger is shown and the setting isn't set yet.
  is(
    panel.getAttribute("panelopen"),
    "true",
    "The notification panel is open after opening New Tab"
  );
  is(gURLBar.focused, false, "The URL bar is not focused with a doorhanger");
  is(
    getNotificationSetting(extensionId),
    null,
    "The New Tab notification is not set for this extension"
  );
  is(
    panel.anchorNode.closest("toolbarbutton").id,
    "newtabignore_mochi_test-BAP",
    "The doorhanger is anchored to the browser action"
  );

  // Manually close the panel, as if the user ignored it.
  let popupHidden = promisePopupHidden(panel);
  panel.hidePopup();
  await popupHidden;

  // Ensure panel is closed and the setting still isn't set.
  Assert.notEqual(
    panel.getAttribute("panelopen"),
    "true",
    "The notification panel is closed"
  );
  is(
    getNotificationSetting(extensionId),
    null,
    "The New Tab notification is not set after ignoring the doorhanger"
  );

  // Close the first tab and open another new tab.
  BrowserTestUtils.removeTab(tab);
  tab = await promiseNewTab();

  // Verify the doorhanger is not shown a second time.
  Assert.notEqual(
    panel.getAttribute("panelopen"),
    "true",
    "The notification panel doesn't open after ignoring the doorhanger"
  );
  is(gURLBar.focused, true, "The URL bar is focused with no doorhanger");

  BrowserTestUtils.removeTab(tab);
  await extension.unload();
});

add_task(async function test_new_tab_keep_settings() {
  await ExtensionSettingsStore.initialize();
  let notification = getNewTabDoorhanger();
  let panel = notification.closest("panel");
  let extensionId = "newtabkeep@mochi.test";
  let manifest = {
    version: "1.0",
    name: "New Tab Add-on",
    browser_specific_settings: { gecko: { id: extensionId } },
    chrome_url_overrides: { newtab: "newtab.html" },
  };
  let extension = ExtensionTestUtils.loadExtension({
    ...extensionData,
    manifest,
    useAddonManager: "permanent",
  });

  Assert.notEqual(
    panel.getAttribute("panelopen"),
    "true",
    "The notification panel is initially closed"
  );

  await extension.startup();
  let extensionNewTabUrl = `moz-extension://${extension.uuid}/newtab.html`;

  // Simulate opening the New Tab as a user would.
  let popupShown = promisePopupShown(panel);
  let tab = await promiseNewTab(extensionNewTabUrl);
  await popupShown;

  // Ensure the panel is open and the setting isn't saved yet.
  is(
    panel.getAttribute("panelopen"),
    "true",
    "The notification panel is open after opening New Tab"
  );
  is(
    getNotificationSetting(extensionId),
    null,
    "The New Tab notification is not set for this extension"
  );
  is(
    panel.anchorNode.closest("toolbarbutton").id,
    "unified-extensions-button",
    "The doorhanger is anchored to the extensions button"
  );
  is(
    panel.querySelector("#extension-new-tab-notification-description")
      .textContent,
    "An extension,  New Tab Add-on, changed the page you see when you open a new tab.",
    "The description includes the add-on name"
  );

  const learnMoreEl = panel.querySelector(
    "#extension-new-tab-notification .popup-notification-learnmore-link"
  );
  ok(
    BrowserTestUtils.isVisible(learnMoreEl),
    "Expect the popupnotification learnmore link to be visible"
  );

  is(
    learnMoreEl.getAttribute("href"),
    Services.urlFormatter.formatURLPref("app.support.baseURL") +
      "extension-home",
    "learnmore link should have the expected url set"
  );

  // Click the Keep Changes button.
  let confirmationSaved = TestUtils.waitForCondition(() => {
    return ExtensionSettingsStore.getSetting(
      "newTabNotification",
      extensionId,
      extensionId
    ).value;
  });
  clickKeepChanges(notification);
  await confirmationSaved;

  // Ensure panel is closed and setting is updated.
  Assert.notEqual(
    panel.getAttribute("panelopen"),
    "true",
    "The notification panel is closed after click"
  );
  is(
    getNotificationSetting(extensionId).value,
    true,
    "The New Tab notification is set after keeping the changes"
  );

  // Close the first tab and open another new tab.
  BrowserTestUtils.removeTab(tab);
  tab = await promiseNewTab(extensionNewTabUrl);

  // Verify the doorhanger is not shown a second time.
  Assert.notEqual(
    panel.getAttribute("panelopen"),
    "true",
    "The notification panel is not opened after keeping the changes"
  );

  BrowserTestUtils.removeTab(tab);

  let upgradedExtension = ExtensionTestUtils.loadExtension({
    ...extensionData,
    manifest: Object.assign({}, manifest, { version: "2.0" }),
    useAddonManager: "permanent",
  });

  await upgradedExtension.startup();
  extensionNewTabUrl = `moz-extension://${upgradedExtension.uuid}/newtab.html`;

  tab = await promiseNewTab(extensionNewTabUrl);

  // Ensure panel is closed and setting is still set.
  Assert.notEqual(
    panel.getAttribute("panelopen"),
    "true",
    "The notification panel is closed after click"
  );
  is(
    getNotificationSetting(extensionId).value,
    true,
    "The New Tab notification is set after keeping the changes"
  );

  BrowserTestUtils.removeTab(tab);
  await upgradedExtension.unload();
  await extension.unload();

  let confirmation = ExtensionSettingsStore.getSetting(
    "newTabNotification",
    extensionId,
    extensionId
  );
  is(confirmation, null, "The confirmation has been cleaned up");
});

add_task(async function test_new_tab_restore_settings() {
  await ExtensionSettingsStore.initialize();
  let notification = getNewTabDoorhanger();
  let panel = notification.closest("panel");
  let extensionId = "newtabrestore@mochi.test";
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_specific_settings: { gecko: { id: extensionId } },
      chrome_url_overrides: { newtab: "restore.html" },
    },
    files: { "restore.html": '<h1 id="extension-new-tab">New Tab!</h1>' },
    useAddonManager: "temporary",
  });

  Assert.notEqual(
    panel.getAttribute("panelopen"),
    "true",
    "The notification panel is initially closed"
  );
  is(
    getNotificationSetting(extensionId),
    null,
    "The New Tab notification is not initially set for this extension"
  );

  await extension.startup();

  // Simulate opening the newtab open as a user would.
  let popupShown = promisePopupShown(panel);
  let tab = await promiseNewTab();
  await popupShown;

  // Verify that the panel is open and add-on is enabled.
  let addon = await AddonManager.getAddonByID(extensionId);
  is(addon.userDisabled, false, "The add-on is enabled at first");
  is(
    panel.getAttribute("panelopen"),
    "true",
    "The notification panel is open after opening New Tab"
  );
  is(
    getNotificationSetting(extensionId),
    null,
    "The New Tab notification is not set for this extension"
  );

  // Click the Manage button.
  let preferencesShown = TestUtils.waitForCondition(
    () => gBrowser.currentURI.spec == "about:preferences#home",
    "Should open about:preferences."
  );

  let popupHidden = promisePopupHidden(panel);
  clickManage(notification);
  await popupHidden;
  await preferencesShown;

  is(
    gBrowser.selectedTab,
    tab,
    "about:preferences replaced the New Tab page in the same tab"
  );

  // Ensure panel is closed, settings haven't changed and add-on is disabled.
  Assert.notEqual(
    panel.getAttribute("panelopen"),
    "true",
    "The notification panel is closed after click"
  );

  is(
    getNotificationSetting(extensionId),
    null,
    "The New Tab notification is not set after clicking manage"
  );

  // Reopen a browser tab and verify that there's no doorhanger.
  BrowserTestUtils.removeTab(tab);
  tab = await promiseNewTab();

  Assert.notEqual(
    panel.getAttribute("panelopen"),
    "true",
    "The notification panel is not opened after keeping the changes"
  );

  BrowserTestUtils.removeTab(tab);
  await extension.unload();
});

add_task(async function test_new_tab_restore_settings_multiple() {
  await ExtensionSettingsStore.initialize();
  let notification = getNewTabDoorhanger();
  let panel = notification.closest("panel");
  let extensionOneId = "newtabrestoreone@mochi.test";
  let extensionOne = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_specific_settings: { gecko: { id: extensionOneId } },
      chrome_url_overrides: { newtab: "restore-one.html" },
    },
    files: {
      "restore-one.html": `
        <h1 id="extension-new-tab">New Tab!</h1>
      `,
    },
    useAddonManager: "temporary",
  });
  let extensionTwoId = "newtabrestoretwo@mochi.test";
  let extensionTwo = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_specific_settings: { gecko: { id: extensionTwoId } },
      chrome_url_overrides: { newtab: "restore-two.html" },
    },
    files: { "restore-two.html": '<h1 id="extension-new-tab">New Tab!</h1>' },
    useAddonManager: "temporary",
  });

  Assert.notEqual(
    panel.getAttribute("panelopen"),
    "true",
    "The notification panel is initially closed"
  );
  is(
    getNotificationSetting(extensionOneId),
    null,
    "The New Tab notification is not initially set for this extension"
  );
  is(
    getNotificationSetting(extensionTwoId),
    null,
    "The New Tab notification is not initially set for this extension"
  );

  await extensionOne.startup();
  await extensionTwo.startup();

  // Simulate opening the newtab open as a user would.
  let popupShown = promisePopupShown(panel);
  let tab1 = await promiseNewTab();
  await popupShown;

  // Verify that the panel is open and add-on is enabled.
  let addonTwo = await AddonManager.getAddonByID(extensionTwoId);
  is(addonTwo.userDisabled, false, "The add-on is enabled at first");
  is(
    panel.getAttribute("panelopen"),
    "true",
    "The notification panel is open after opening New Tab"
  );
  is(
    getNotificationSetting(extensionTwoId),
    null,
    "The New Tab notification is not set for this extension"
  );

  // Click the Manage button.
  let popupHidden = promisePopupHidden(panel);
  let preferencesShown = TestUtils.waitForCondition(
    () => gBrowser.currentURI.spec == "about:preferences#home",
    "Should open about:preferences."
  );
  clickManage(notification);
  await popupHidden;
  await preferencesShown;

  is(
    gBrowser.selectedTab,
    tab1,
    "about:preferences replaced the New Tab page in the same tab"
  );

  // Disable the second addon then refresh the new tab expect to see a new addon dropdown.
  let addonDisabled = waitForAddonDisabled(addonTwo);
  addonTwo.disable();
  await addonDisabled;

  // Ensure the panel opens again for the next add-on.
  popupShown = promisePopupShown(panel);
  let newtabShown = TestUtils.waitForCondition(
    () => gBrowser.currentURI.spec == AboutNewTab.newTabURL,
    "Should open correct new tab url."
  );
  let tab2 = await promiseNewTab();
  await newtabShown;
  await popupShown;

  is(
    getNotificationSetting(extensionTwoId),
    null,
    "The New Tab notification is not set after restoring the settings"
  );
  let addonOne = await AddonManager.getAddonByID(extensionOneId);
  is(
    addonOne.userDisabled,
    false,
    "The extension is enabled before making a choice"
  );
  is(
    getNotificationSetting(extensionOneId),
    null,
    "The New Tab notification is not set before making a choice"
  );
  is(
    panel.getAttribute("panelopen"),
    "true",
    "The notification panel is open after opening New Tab"
  );
  is(
    gBrowser.currentURI.spec,
    AboutNewTab.newTabURL,
    "The user is now on the next extension's New Tab page"
  );

  preferencesShown = TestUtils.waitForCondition(
    () => gBrowser.currentURI.spec == "about:preferences#home",
    "Should open about:preferences."
  );
  popupHidden = promisePopupHidden(panel);
  clickManage(notification);
  await popupHidden;
  await preferencesShown;

  is(
    gBrowser.selectedTab,
    tab1,
    "Switched back to the tab already showing about:preferences"
  );

  // remove the extra new tab.
  BrowserTestUtils.removeTab(tab2);

  addonDisabled = waitForAddonDisabled(addonOne);
  addonOne.disable();
  await addonDisabled;
  tab2 = await promiseNewTab();

  Assert.notEqual(
    panel.getAttribute("panelopen"),
    "true",
    "The notification panel is closed after restoring the second time"
  );
  is(
    getNotificationSetting(extensionOneId),
    null,
    "The New Tab notification is not set after restoring the settings"
  );
  is(
    gBrowser.currentURI.spec,
    "about:newtab",
    "The user is now on the original New Tab URL since all extensions are disabled"
  );

  // Reopen a browser tab and verify that there's no doorhanger.
  BrowserTestUtils.removeTab(tab2);
  tab2 = await promiseNewTab();

  Assert.notEqual(
    panel.getAttribute("panelopen"),
    "true",
    "The notification panel is not opened after keeping the changes"
  );

  // FIXME: We need to enable the add-on so it gets cleared from the
  // ExtensionSettingsStore for now. See bug 1408226.
  let addonsEnabled = Promise.all([
    waitForAddonEnabled(addonOne),
    waitForAddonEnabled(addonTwo),
  ]);
  await addonOne.enable();
  await addonTwo.enable();
  await addonsEnabled;
  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);

  await extensionOne.unload();
  await extensionTwo.unload();
});

/**
 * Ensure we don't show the extension URL in the URL bar temporarily in new tabs
 * while we're switching remoteness (when the URL we're loading and the
 * default content principal are different).
 */
add_task(async function dontTemporarilyShowAboutExtensionPath() {
  await ExtensionSettingsStore.initialize();
  let extension = ExtensionTestUtils.loadExtension(extensionData);

  await extension.startup();
  let extensionNewTabUrl = `moz-extension://${extension.uuid}/newtab.html`;

  let wpl = {
    onLocationChange() {
      is(gURLBar.value, "", "URL bar value should stay empty.");
    },
  };
  gBrowser.addProgressListener(wpl);

  let tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    url: extensionNewTabUrl,
  });

  gBrowser.removeProgressListener(wpl);
  is(gURLBar.value, "", "URL bar value should be empty.");
  await SpecialPowers.spawn(tab.linkedBrowser, [], function () {
    is(
      content.document.body.textContent,
      "New tab!",
      "New tab page is loaded."
    );
  });

  BrowserTestUtils.removeTab(tab);
  await extension.unload();
});

add_task(async function test_url_selected_after_newtab_navigates() {
  await ExtensionSettingsStore.initialize();
  const url = "https://example.com/";
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      chrome_url_overrides: { newtab: "newtab.html" },
    },
    files: {
      "newtab.html": `<!DOCTYPE html><script src="newtab.js"></script>`,
      "newtab.js"() {
        browser.tabs.getCurrent().then(tab => {
          browser.tabs.update(tab.id, {
            url: "https://example.com/",
            loadReplace: true,
          });
        });
      },
    },
    useAddonManager: "temporary",
  });
  await extension.startup();

  let panel = getNewTabDoorhanger().closest("panel");
  let popupShown = promisePopupShown(panel);
  let tab = await promiseNewTab(url);
  await popupShown;
  let popupHidden = promisePopupHidden(panel);
  panel.hidePopup();
  await popupHidden;
  BrowserTestUtils.removeTab(tab);

  function checkAllSelected(msg) {
    ok(gURLBar.focused, `The URL bar is focused ${msg}`);
    Assert.notEqual(gURLBar.value, "", `The URL bar has a value ${msg}`);
    is(gURLBar.selectionStart, 0, `The selection starts at 0 ${msg}`);
    is(
      gURLBar.selectionEnd,
      gURLBar.value.length,
      `The selection reaches the end ${msg}`
    );
  }

  tab = await promiseNewTab(url);
  checkAllSelected("after the new tab navigates");

  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    content.history.pushState({}, "", "/pushed");
  });
  await TestUtils.waitForCondition(() => gURLBar.value.endsWith("/pushed"));
  checkAllSelected("after pushState");

  const redirectUrl = "https://example.org/";
  let loaded = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    redirectUrl
  );
  await SpecialPowers.spawn(tab.linkedBrowser, [redirectUrl], target => {
    content.location.replace(target);
  });
  await loaded;
  checkAllSelected("after location.replace");

  EventUtils.sendString("apples");
  is(gURLBar.value, "apples", "Typing replaces the URL");

  gURLBar.view.close();
  gURLBar.handleRevert();
  BrowserTestUtils.removeTab(tab);

  tab = await promiseNewTab(url);
  gURLBar.setSelectionRange(1, 1);
  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    content.history.pushState({}, "", "/moved-caret");
  });
  await TestUtils.waitForCondition(() =>
    gURLBar.value.endsWith("/moved-caret")
  );
  is(gURLBar.selectionStart, 1, "A moved caret isn't turned into a selection");
  is(gURLBar.selectionEnd, 1, "A moved caret stays collapsed");

  BrowserTestUtils.removeTab(tab);
  await extension.unload();
});

add_task(async function test_overriding_newtab_incognito_not_allowed() {
  let panel = getNewTabDoorhanger().closest("panel");

  let extension = ExtensionTestUtils.loadExtension({
    ...extensionData,
    useAddonManager: "permanent",
  });

  await extension.startup();
  let extensionNewTabUrl = `moz-extension://${extension.uuid}/newtab.html`;

  let popupShown = promisePopupShown(panel);
  let tab = await promiseNewTab(extensionNewTabUrl);
  await popupShown;

  // This will show a confirmation doorhanger, make sure we don't leave it open.
  let popupHidden = promisePopupHidden(panel);
  panel.hidePopup();
  await popupHidden;

  BrowserTestUtils.removeTab(tab);

  // Verify a private window does not open the extension page.  We would
  // get an extra notification that we don't listen for if it gets loaded.
  let windowOpenedPromise = BrowserTestUtils.waitForNewWindow();
  let win = OpenBrowserWindow({ private: true });
  await windowOpenedPromise;

  await promiseNewTab("about:privatebrowsing", win);

  is(win.gURLBar.value, "", "newtab not used in private window");

  // Verify setting the pref directly doesn't bypass permissions.
  let origUrl = AboutNewTab.newTabURL;
  AboutNewTab.newTabURL = extensionNewTabUrl;
  await promiseNewTab("about:privatebrowsing", win);

  is(win.gURLBar.value, "", "directly set newtab not used in private window");

  AboutNewTab.newTabURL = origUrl;

  await extension.unload();
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_overriding_newtab_incognito_spanning() {
  let extension = ExtensionTestUtils.loadExtension({
    ...extensionData,
    useAddonManager: "permanent",
    incognitoOverride: "spanning",
  });

  await extension.startup();
  let extensionNewTabUrl = `moz-extension://${extension.uuid}/newtab.html`;

  let windowOpenedPromise = BrowserTestUtils.waitForNewWindow();
  let win = OpenBrowserWindow({ private: true });
  await windowOpenedPromise;
  let panel = ExtensionControlledPopup._getAndMaybeCreatePanel(win.document);
  let popupShown = promisePopupShown(panel);
  await promiseNewTab(extensionNewTabUrl, win);
  await popupShown;

  // This will show a confirmation doorhanger, make sure we don't leave it open.
  let popupHidden = promisePopupHidden(panel);
  panel.hidePopup();
  await popupHidden;

  await extension.unload();
  await BrowserTestUtils.closeWindow(win);
});

// Test that prefs set by the newtab override code are
// properly unset when all newtab extensions are gone.
add_task(async function testNewTabPrefsReset() {
  function isUndefinedPref(pref) {
    try {
      Services.prefs.getBoolPref(pref);
      return false;
    } catch (e) {
      return true;
    }
  }

  ok(
    isUndefinedPref("browser.newtab.extensionControlled"),
    "extensionControlled pref is not set"
  );
  ok(
    isUndefinedPref("browser.newtab.privateAllowed"),
    "privateAllowed pref is not set"
  );
});

// This test ensures that an extension provided newtab
// can be opened by another extension (e.g. tab manager)
// regardless of whether the newtab url is made available
// in web_accessible_resources.
add_task(async function test_newtab_from_extension() {
  let panel = getNewTabDoorhanger().closest("panel");

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_specific_settings: {
        gecko: {
          id: "newtaburl@mochi.test",
        },
      },
      chrome_url_overrides: {
        newtab: "newtab.html",
      },
    },
    files: {
      "newtab.html": `<h1>New tab!</h1><script src="newtab.js"></script>`,
      "newtab.js": () => {
        browser.test.sendMessage("newtab-loaded");
      },
    },
    useAddonManager: "temporary",
  });

  await extension.startup();
  let extensionNewTabUrl = `moz-extension://${extension.uuid}/newtab.html`;

  let popupShown = promisePopupShown(panel);
  let tab = await promiseNewTab(extensionNewTabUrl);
  await extension.awaitMessage("newtab-loaded");
  await popupShown;

  // This will show a confirmation doorhanger, make sure we don't leave it open.
  let popupHidden = promisePopupHidden(panel);
  panel.hidePopup();
  await popupHidden;

  BrowserTestUtils.removeTab(tab);

  // extension to open the newtab
  let opener = ExtensionTestUtils.loadExtension({
    async background() {
      let newtab = await browser.tabs.create({});
      browser.test.assertTrue(
        newtab.id !== browser.tabs.TAB_ID_NONE,
        "New tab was created."
      );
      browser.test.sendMessage("complete");
    },
  });

  function listener(msg) {
    Assert.ok(!/may not load or link to moz-extension/.test(msg.message));
  }
  Services.console.registerListener(listener);
  registerCleanupFunction(() => {
    Services.console.unregisterListener(listener);
  });

  await opener.startup();
  await opener.awaitMessage("complete");
  await extension.awaitMessage("newtab-loaded");
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  await opener.unload();
  await extension.unload();
});
