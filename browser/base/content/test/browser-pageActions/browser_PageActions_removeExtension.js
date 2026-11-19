"use strict";

// Initialization. Must run first.
add_setup(async function () {
  // The page action urlbar button, and therefore the panel, is only shown when
  // the current tab is actionable -- i.e., a normal web page.  about:blank is
  // not, so open a new tab first thing, and close it when this test is done.
  let tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    // eslint-disable-next-line sdl/no-insecure-url
    url: "http://example.com/",
  });

  // The prompt service is mocked later, so set it up to be restored.
  let { prompt } = Services;

  registerCleanupFunction(async () => {
    BrowserTestUtils.removeTab(tab);
    Services.prompt = prompt;
  });
});

add_task(async function contextMenu_removeExtension_panel() {
  // We use an extension that shows a page action so that we can test the
  // "remove extension" item in the context menu.
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      name: "Test contextMenu",
      page_action: { show_matches: ["<all_urls>"] },
    },

    useAddonManager: "temporary",
  });

  await extension.startup();

  let actionId = ExtensionCommon.makeWidgetId(extension.id);

  const url = "data:text/html,<h1>A Page</h1>";
  let win = await BrowserTestUtils.openNewBrowserWindow();
  await SimpleTest.promiseFocus(win);
  BrowserTestUtils.startLoadingURIString(win.gBrowser, url);
  await BrowserTestUtils.browserLoaded(win.gBrowser.selectedBrowser);

  info("Shrink the window if necessary, check the meatball menu is visible");
  let originalOuterWidth = win.outerWidth;
  await promiseStableResize(500, win);

  // The pageAction implementation enables the button at the next animation
  // frame, so before we look for the button we should wait one animation frame
  // as well.
  await promiseAnimationFrame(win);

  let meatballButton = win.document.getElementById("pageActionButton");
  Assert.ok(BrowserTestUtils.isVisible(meatballButton));

  // Open the panel.
  await promisePageActionPanelOpen(win);

  info("Open the context menu");
  let panelButton = win.BrowserPageActions.panelButtonNodeForActionID(actionId);
  let contextMenuPromise = promisePanelShown("pageActionContextMenu", win);
  EventUtils.synthesizeMouseAtCenter(
    panelButton,
    {
      type: "contextmenu",
      button: 2,
    },
    win
  );
  let contextMenu = await contextMenuPromise;

  let removeExtensionItem = getRemoveExtensionItem(win);
  Assert.ok(removeExtensionItem, "'Remove' item exists");
  Assert.ok(!removeExtensionItem.hidden, "'Remove' item is visible");
  Assert.ok(!removeExtensionItem.disabled, "'Remove' item is not disabled");

  // Click the "remove extension" item, a prompt should be displayed and then
  // the add-on should be uninstalled. We mock the prompt service to confirm
  // the removal of the add-on.
  contextMenuPromise = promisePanelHidden("pageActionContextMenu", win);
  let addonUninstalledPromise = promiseAddonUninstalled(extension.id);
  mockPromptService();
  contextMenu.activateItem(removeExtensionItem);
  await Promise.all([contextMenuPromise, addonUninstalledPromise]);

  // Done, clean up.
  await extension.unload();

  await promiseStableResize(originalOuterWidth, win);
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function contextMenu_removeExtension_urlbar() {
  // We use an extension that shows a page action so that we can test the
  // "remove extension" item in the context menu.
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      name: "Test contextMenu",
      page_action: { show_matches: ["<all_urls>"] },
    },

    useAddonManager: "temporary",
  });

  await extension.startup();
  // The pageAction implementation enables the button at the next animation
  // frame, so before we look for the button we should wait one animation frame
  // as well.
  await promiseAnimationFrame();

  let actionId = ExtensionCommon.makeWidgetId(extension.id);

  // Open the context menu on the action's urlbar button.
  let urlbarButton = BrowserPageActions.urlbarButtonNodeForActionID(actionId);
  let contextMenuPromise = promisePanelShown("pageActionContextMenu");
  EventUtils.synthesizeMouseAtCenter(urlbarButton, {
    type: "contextmenu",
    button: 2,
  });
  let contextMenu = await contextMenuPromise;

  let menuItems = collectContextMenuItems();
  Assert.equal(menuItems.length, 2, "Context menu has two children");
  let removeExtensionItem = getRemoveExtensionItem();
  Assert.ok(removeExtensionItem, "'Remove' item exists");
  Assert.ok(!removeExtensionItem.hidden, "'Remove' item is visible");
  Assert.ok(!removeExtensionItem.disabled, "'Remove' item is not disabled");
  let manageExtensionItem = getManageExtensionItem();
  Assert.ok(manageExtensionItem, "'Manage' item exists");
  Assert.ok(!manageExtensionItem.hidden, "'Manage' item is visible");
  Assert.ok(!manageExtensionItem.disabled, "'Manage' item is not disabled");

  // Click the "remove extension" item, a prompt should be displayed and then
  // the add-on should be uninstalled. We mock the prompt service to cancel the
  // removal of the add-on.
  contextMenuPromise = promisePanelHidden("pageActionContextMenu");
  let promptService = mockPromptService();
  let promptCancelledPromise = new Promise(resolve => {
    promptService.confirmEx = () => resolve();
  });
  contextMenu.activateItem(removeExtensionItem);
  await Promise.all([contextMenuPromise, promptCancelledPromise]);

  // Done, clean up.
  await extension.unload();

  // urlbar tests that run after this one can break if the mouse is left over
  // the area where the urlbar popup appears, which seems to happen due to the
  // above synthesized mouse events.  Move it over the urlbar.
  EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, { type: "mousemove" });
  gURLBar.focus();
});

add_task(async function contextMenu_removeExtension_disabled_in_urlbar() {
  // We use an extension that shows a page action so that we can test the
  // "remove extension" item in the context menu.
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      name: "Test contextMenu",
      page_action: { show_matches: ["<all_urls>"] },
    },

    useAddonManager: "temporary",
  });

  await extension.startup();
  // The pageAction implementation enables the button at the next animation
  // frame, so before we look for the button we should wait one animation frame
  // as well.
  await promiseAnimationFrame();
  // Add a policy to prevent the add-on from being uninstalled.
  await EnterprisePolicyTesting.setupPolicyEngineWithJson({
    policies: {
      ExtensionSettings: {
        [extension.id]: { installation_mode: "force_installed" },
      },
    },
  });

  let actionId = ExtensionCommon.makeWidgetId(extension.id);

  // Open the context menu on the action's urlbar button.
  let urlbarButton = BrowserPageActions.urlbarButtonNodeForActionID(actionId);
  let contextMenuPromise = promisePanelShown("pageActionContextMenu");
  EventUtils.synthesizeMouseAtCenter(urlbarButton, {
    type: "contextmenu",
    button: 2,
  });
  let contextMenu = await contextMenuPromise;

  let menuItems = collectContextMenuItems();
  Assert.equal(menuItems.length, 2, "Context menu has two children");
  let removeExtensionItem = getRemoveExtensionItem();
  Assert.ok(removeExtensionItem, "'Remove' item exists");
  Assert.ok(!removeExtensionItem.hidden, "'Remove' item is visible");
  Assert.ok(removeExtensionItem.disabled, "'Remove' item is disabled");
  let manageExtensionItem = getManageExtensionItem();
  Assert.ok(manageExtensionItem, "'Manage' item exists");
  Assert.ok(!manageExtensionItem.hidden, "'Manage' item is visible");
  Assert.ok(!manageExtensionItem.disabled, "'Manage' item is not disabled");

  // Hide the context menu.
  contextMenuPromise = promisePanelHidden("pageActionContextMenu");
  contextMenu.hidePopup();
  await contextMenuPromise;

  // Done, clean up.
  await extension.unload();
  await EnterprisePolicyTesting.setupPolicyEngineWithJson("");

  // urlbar tests that run after this one can break if the mouse is left over
  // the area where the urlbar popup appears, which seems to happen due to the
  // above synthesized mouse events.  Move it over the urlbar.
  EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, { type: "mousemove" });
  gURLBar.focus();
});

add_task(async function contextMenu_removeExtension_disabled_in_panel() {
  // We use an extension that shows a page action so that we can test the
  // "remove extension" item in the context menu.
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      name: "Test contextMenu",
      page_action: { show_matches: ["<all_urls>"] },
    },

    useAddonManager: "temporary",
  });

  await extension.startup();
  // Add a policy to prevent the add-on from being uninstalled.
  await EnterprisePolicyTesting.setupPolicyEngineWithJson({
    policies: {
      ExtensionSettings: {
        [extension.id]: { installation_mode: "force_installed" },
      },
    },
  });

  let actionId = ExtensionCommon.makeWidgetId(extension.id);

  const url = "data:text/html,<h1>A Page</h1>";
  let win = await BrowserTestUtils.openNewBrowserWindow();
  await SimpleTest.promiseFocus(win);
  BrowserTestUtils.startLoadingURIString(win.gBrowser, url);
  await BrowserTestUtils.browserLoaded(win.gBrowser.selectedBrowser);

  info("Shrink the window if necessary, check the meatball menu is visible");
  let originalOuterWidth = win.outerWidth;
  await promiseStableResize(500, win);

  // The pageAction implementation enables the button at the next animation
  // frame, so before we look for the button we should wait one animation frame
  // as well.
  await promiseAnimationFrame(win);

  let meatballButton = win.document.getElementById("pageActionButton");
  Assert.ok(BrowserTestUtils.isVisible(meatballButton));

  // Open the panel.
  await promisePageActionPanelOpen(win);

  info("Open the context menu");
  let panelButton = win.BrowserPageActions.panelButtonNodeForActionID(actionId);
  let contextMenuPromise = promisePanelShown("pageActionContextMenu", win);
  EventUtils.synthesizeMouseAtCenter(
    panelButton,
    {
      type: "contextmenu",
      button: 2,
    },
    win
  );
  let contextMenu = await contextMenuPromise;

  let removeExtensionItem = getRemoveExtensionItem(win);
  Assert.ok(removeExtensionItem, "'Remove' item exists");
  Assert.ok(!removeExtensionItem.hidden, "'Remove' item is visible");
  Assert.ok(removeExtensionItem.disabled, "'Remove' item is disabled");

  // Hide the context menu.
  contextMenuPromise = promisePanelHidden("pageActionContextMenu", win);
  contextMenu.hidePopup();
  await contextMenuPromise;

  // Done, clean up.
  await extension.unload();
  await EnterprisePolicyTesting.setupPolicyEngineWithJson("");

  await promiseStableResize(originalOuterWidth, win);
  await BrowserTestUtils.closeWindow(win);
});

function promiseAddonUninstalled(addonId) {
  return new Promise(resolve => {
    let listener = {};
    listener.onUninstalled = addon => {
      if (addon.id == addonId) {
        AddonManager.removeAddonListener(listener);
        resolve();
      }
    };
    AddonManager.addAddonListener(listener);
  });
}

function mockPromptService() {
  let promptService = {
    // The prompt returns 1 for cancelled and 0 for accepted.
    _response: 0,
    QueryInterface: ChromeUtils.generateQI(["nsIPromptService"]),
    confirmEx: () => promptService._response,
  };

  Services.prompt = promptService;

  return promptService;
}

function getRemoveExtensionItem(win = window) {
  return win.document.querySelector(
    "#pageActionContextMenu > menuitem[label='Remove Extension']"
  );
}

function getManageExtensionItem(win = window) {
  return win.document.querySelector(
    "#pageActionContextMenu > menuitem[label='Manage Extension…']"
  );
}

function collectContextMenuItems(win = window) {
  let contextMenu = win.document.getElementById("pageActionContextMenu");
  return Array.prototype.filter.call(contextMenu.children, node => {
    return win.getComputedStyle(node).visibility == "visible";
  });
}
