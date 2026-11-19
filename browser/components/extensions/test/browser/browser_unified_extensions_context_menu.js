/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

requestLongerTimeout(2);

const { EnterprisePolicyTesting } = ChromeUtils.importESModule(
  "resource://testing-common/EnterprisePolicyTesting.sys.mjs"
);
loadTestSubscript("head_unified_extensions.js");

// We expect this rejection when the abuse report dialog window is
// being forcefully closed as part of the related test task.
PromiseTestUtils.allowMatchingRejectionsGlobally(/report dialog closed/);

const promiseExtensionUninstalled = extensionId => {
  return new Promise(resolve => {
    let listener = {};
    listener.onUninstalled = addon => {
      if (addon.id == extensionId) {
        AddonManager.removeAddonListener(listener);
        resolve();
      }
    };
    AddonManager.addAddonListener(listener);
  });
};

function assertVisibleContextMenuItems(contextMenu, expected) {
  let visibleItems = contextMenu.querySelectorAll(
    ":is(menuitem, menuseparator):not([hidden])"
  );
  is(visibleItems.length, expected, `expected ${expected} visible menu items`);
}

function assertOrderOfWidgetsInPanel(extensions, win = window) {
  const widgetIds = CustomizableUI.getWidgetIdsInArea(
    CustomizableUI.AREA_ADDONS
  ).filter(
    widgetId => !!CustomizableUI.getWidget(widgetId).forWindow(win).node
  );
  const widgetIdsFromExtensions = extensions.map(ext =>
    AppUiTestInternals.getBrowserActionWidgetId(ext.id)
  );

  Assert.deepEqual(
    widgetIds,
    widgetIdsFromExtensions,
    "expected extensions to be ordered"
  );
}

async function moveWidgetUp(extension, win = window) {
  const contextMenu = await openUnifiedExtensionsContextMenu(extension.id, win);
  const hidden = BrowserTestUtils.waitForEvent(contextMenu, "popuphidden");
  contextMenu.activateItem(
    contextMenu.querySelector(".unified-extensions-context-menu-move-widget-up")
  );
  await hidden;
}

async function moveWidgetDown(extension, win = window) {
  const contextMenu = await openUnifiedExtensionsContextMenu(extension.id, win);
  const hidden = BrowserTestUtils.waitForEvent(contextMenu, "popuphidden");
  contextMenu.activateItem(
    contextMenu.querySelector(
      ".unified-extensions-context-menu-move-widget-down"
    )
  );
  await hidden;
}

async function pinToToolbar(extension, win = window) {
  const contextMenu = await openUnifiedExtensionsContextMenu(extension.id, win);
  const pinToToolbarItem = contextMenu.querySelector(
    ".unified-extensions-context-menu-pin-to-toolbar"
  );
  ok(pinToToolbarItem, "expected 'pin to toolbar' menu item");

  const hidden = BrowserTestUtils.waitForEvent(
    win.gUnifiedExtensions.panel,
    "popuphidden",
    true
  );
  contextMenu.activateItem(pinToToolbarItem);
  await hidden;
}

async function assertMoveContextMenuItems(
  ext,
  { expectMoveUpHidden, expectMoveDownHidden, expectOrder },
  win = window
) {
  const extName = WebExtensionPolicy.getByID(ext.id).name;
  info(`Assert Move context menu items visibility for ${extName}`);
  const contextMenu = await openUnifiedExtensionsContextMenu(ext.id, win);
  const moveUp = contextMenu.querySelector(
    ".unified-extensions-context-menu-move-widget-up"
  );
  const moveDown = contextMenu.querySelector(
    ".unified-extensions-context-menu-move-widget-down"
  );
  ok(moveUp, "expected 'move up' item in the context menu");
  ok(moveDown, "expected 'move down' item in the context menu");

  is(
    BrowserTestUtils.isHidden(moveUp),
    expectMoveUpHidden,
    `expected 'move up' item to be ${expectMoveUpHidden ? "hidden" : "visible"}`
  );
  is(
    BrowserTestUtils.isHidden(moveDown),
    expectMoveDownHidden,
    `expected 'move down' item to be ${
      expectMoveDownHidden ? "hidden" : "visible"
    }`
  );
  const expectedVisibleItems =
    5 + (+(expectMoveUpHidden ? 0 : 1) + (expectMoveDownHidden ? 0 : 1));
  assertVisibleContextMenuItems(contextMenu, expectedVisibleItems);
  if (expectOrder) {
    assertOrderOfWidgetsInPanel(expectOrder, win);
  }
  await closeChromeContextMenu(contextMenu.id, null, win);
}

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["extensions.abuseReport.enabled", true],
      [
        "extensions.abuseReport.amoFormURL",
        "https://example.org/%LOCALE%/firefox/feedback/addon/%addonID%/",
      ],
    ],
  });
});

add_task(async function test_context_menu() {
  const [extension] = createExtensions([{ name: "an extension" }]);
  await extension.startup();

  // Open the extension panel.
  await openExtensionsPanel();

  // Get the menu button of the extension and verify the mouseover/mouseout
  // behavior. We expect a help message (in the message deck) to be selected
  // (and therefore displayed) when the menu button is hovered/focused.
  const item = getUnifiedExtensionsItem(extension.id);
  ok(item, "expected an item for the extension");

  const messageDeck = item.querySelector(
    ".unified-extensions-item-message-deck"
  );
  ok(messageDeck, "expected message deck");
  is(
    messageDeck.selectedIndex,
    gUnifiedExtensions.MESSAGE_DECK_INDEX_DEFAULT,
    "expected selected message in the deck to be the default message"
  );

  const hoverMenuButtonMessage = item.querySelector(
    ".unified-extensions-item-message-hover-menu-button"
  );
  Assert.deepEqual(
    document.l10n.getAttributes(hoverMenuButtonMessage),
    { id: "unified-extensions-item-message-manage", args: null },
    "expected correct l10n attributes for the hover message"
  );

  const menuButton = item.querySelector(".unified-extensions-item-menu-button");
  ok(menuButton, "expected menu button");

  let hovered = BrowserTestUtils.waitForEvent(menuButton, "mouseover");
  EventUtils.synthesizeMouseAtCenter(menuButton, { type: "mouseover" });
  await hovered;
  is(
    messageDeck.selectedIndex,
    gUnifiedExtensions.MESSAGE_DECK_INDEX_MENU_HOVER,
    "expected selected message in the deck to be the message when hovering the menu button"
  );

  let notHovered = BrowserTestUtils.waitForEvent(menuButton, "mouseout");
  // Move mouse somewhere else...
  EventUtils.synthesizeMouseAtCenter(item, { type: "mouseover" });
  await notHovered;
  is(
    messageDeck.selectedIndex,
    gUnifiedExtensions.MESSAGE_DECK_INDEX_HOVER,
    "expected selected message in the deck to be the hover message"
  );

  // Open the context menu for the extension.
  const contextMenu = await openUnifiedExtensionsContextMenu(extension.id);
  const doc = contextMenu.ownerDocument;

  const manageButton = contextMenu.querySelector(
    ".unified-extensions-context-menu-manage-extension"
  );
  ok(manageButton, "expected manage button");
  is(manageButton.hidden, false, "expected manage button to be visible");
  is(manageButton.disabled, false, "expected manage button to be enabled");
  Assert.deepEqual(
    doc.l10n.getAttributes(manageButton),
    { id: "unified-extensions-context-menu-manage-extension", args: null },
    "expected correct l10n attributes for manage button"
  );

  const removeButton = contextMenu.querySelector(
    ".unified-extensions-context-menu-remove-extension"
  );
  ok(removeButton, "expected remove button");
  is(removeButton.hidden, false, "expected remove button to be visible");
  is(removeButton.disabled, false, "expected remove button to be enabled");
  Assert.deepEqual(
    doc.l10n.getAttributes(removeButton),
    { id: "unified-extensions-context-menu-remove-extension", args: null },
    "expected correct l10n attributes for remove button"
  );

  const reportButton = contextMenu.querySelector(
    ".unified-extensions-context-menu-report-extension"
  );
  ok(reportButton, "expected report button");
  is(reportButton.hidden, false, "expected report button to be visible");
  is(reportButton.disabled, false, "expected report button to be enabled");
  Assert.deepEqual(
    doc.l10n.getAttributes(reportButton),
    { id: "unified-extensions-context-menu-report-extension", args: null },
    "expected correct l10n attributes for report button"
  );

  await closeChromeContextMenu(contextMenu.id, null);
  await closeExtensionsPanel();

  await extension.unload();
});

add_task(
  async function test_context_menu_report_button_hidden_when_abuse_report_disabled() {
    await SpecialPowers.pushPrefEnv({
      set: [["extensions.abuseReport.enabled", false]],
    });

    const [extension] = createExtensions([{ name: "an extension" }]);
    await extension.startup();

    // Open the extension panel, then open the contextMenu for the extension.
    await openExtensionsPanel();
    const contextMenu = await openUnifiedExtensionsContextMenu(extension.id);

    const reportButton = contextMenu.querySelector(
      ".unified-extensions-context-menu-report-extension"
    );
    ok(reportButton, "expected report button");
    is(reportButton.hidden, true, "expected report button to be hidden");

    await closeChromeContextMenu(contextMenu.id, null);
    await closeExtensionsPanel();

    await extension.unload();

    await SpecialPowers.popPrefEnv();
  }
);

add_task(
  async function test_context_menu_remove_button_disabled_when_extension_cannot_be_uninstalled() {
    const [extension] = createExtensions([{ name: "an extension" }]);
    await extension.startup();

    await EnterprisePolicyTesting.setupPolicyEngineWithJson({
      policies: {
        ExtensionSettings: {
          [extension.id]: { installation_mode: "force_installed" },
        },
      },
    });

    // Open the extension panel, then open the context menu for the extension.
    await openExtensionsPanel();
    const contextMenu = await openUnifiedExtensionsContextMenu(extension.id);

    const removeButton = contextMenu.querySelector(
      ".unified-extensions-context-menu-remove-extension"
    );
    ok(removeButton, "expected remove button");
    is(removeButton.disabled, true, "expected remove button to be disabled");

    await closeChromeContextMenu(contextMenu.id, null);
    await closeExtensionsPanel();

    await extension.unload();
    await EnterprisePolicyTesting.setupPolicyEngineWithJson("");
  }
);

add_task(async function test_manage_extension() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:robots" },
    async () => {
      const [extension] = createExtensions([{ name: "an extension" }]);
      await extension.startup();

      // Open the extension panel, then open the context menu for the extension.
      await openExtensionsPanel();
      const contextMenu = await openUnifiedExtensionsContextMenu(extension.id);

      const manageButton = contextMenu.querySelector(
        ".unified-extensions-context-menu-manage-extension"
      );
      ok(manageButton, "expected manage button");

      // Click the "manage extension" context menu item, and wait until the menu is
      // closed and about:addons is open.
      const hidden = BrowserTestUtils.waitForEvent(contextMenu, "popuphidden");
      const aboutAddons = BrowserTestUtils.waitForNewTab(
        gBrowser,
        "about:addons",
        true
      );
      contextMenu.activateItem(manageButton);
      const [aboutAddonsTab] = await Promise.all([aboutAddons, hidden]);

      // Close the tab containing about:addons because we don't need it anymore.
      BrowserTestUtils.removeTab(aboutAddonsTab);

      await extension.unload();
    }
  );
});

add_task(async function test_report_extension() {
  function runReportTest(extension) {
    return BrowserTestUtils.withNewTab({ gBrowser }, async () => {
      // Open the extension panel, then open the context menu for the extension.
      await openExtensionsPanel();
      const contextMenu = await openUnifiedExtensionsContextMenu(extension.id);

      const reportButton = contextMenu.querySelector(
        ".unified-extensions-context-menu-report-extension"
      );
      ok(reportButton, "expected report button");

      // Click the "report extension" context menu item, and wait until the menu is
      // closed and about:addons is open with the "abuse report dialog".
      const hidden = BrowserTestUtils.waitForEvent(contextMenu, "popuphidden");

      const reportURL = Services.urlFormatter
        .formatURLPref("extensions.abuseReport.amoFormURL")
        .replace("%addonID%", extension.id);

      const promiseReportTab = BrowserTestUtils.waitForNewTab(
        gBrowser,
        reportURL,
        /* waitForLoad */ false,
        // Do not expect it to be the next tab opened
        /* waitForAnyTab */ true
      );
      contextMenu.activateItem(reportButton);
      const [reportTab] = await Promise.all([promiseReportTab, hidden]);
      // Remove the report tab and expect the selected tab
      // to become the about:addons tab.
      BrowserTestUtils.removeTab(reportTab);
      is(
        gBrowser.selectedBrowser.currentURI.spec,
        "about:blank",
        "Expect about:addons tab to have not been opened"
      );
    });
  }

  const [ext] = createExtensions([{ name: "an extension" }]);
  await ext.startup();
  await runReportTest(ext);
  await ext.unload();
});

add_task(async function test_remove_extension() {
  const [extension] = createExtensions([{ name: "an extension" }]);
  await extension.startup();

  // Open the extension panel, then open the context menu for the extension.
  await openExtensionsPanel();
  const contextMenu = await openUnifiedExtensionsContextMenu(extension.id);

  const removeButton = contextMenu.querySelector(
    ".unified-extensions-context-menu-remove-extension"
  );
  ok(removeButton, "expected remove button");

  // Set up a mock prompt service that returns 0 to indicate that the user
  // pressed the OK button.
  const { prompt } = Services;
  const promptService = {
    QueryInterface: ChromeUtils.generateQI(["nsIPromptService"]),
    confirmEx() {
      return 0;
    },
  };
  Services.prompt = promptService;
  registerCleanupFunction(() => {
    Services.prompt = prompt;
  });

  // Click the "remove extension" context menu item, and wait until the menu is
  // closed and the extension is uninstalled.
  const uninstalled = promiseExtensionUninstalled(extension.id);
  const hidden = BrowserTestUtils.waitForEvent(contextMenu, "popuphidden");
  contextMenu.activateItem(removeButton);
  await Promise.all([uninstalled, hidden]);

  await extension.unload();
  // Restore prompt service.
  Services.prompt = prompt;
});

add_task(async function test_remove_extension_cancelled() {
  const [extension] = createExtensions([{ name: "an extension" }]);
  await extension.startup();

  // Open the extension panel, then open the context menu for the extension.
  await openExtensionsPanel();
  const contextMenu = await openUnifiedExtensionsContextMenu(extension.id);

  const removeButton = contextMenu.querySelector(
    ".unified-extensions-context-menu-remove-extension"
  );
  ok(removeButton, "expected remove button");

  // Set up a mock prompt service that returns 1 to indicate that the user
  // refused to uninstall the extension.
  const { prompt } = Services;
  const promptService = {
    QueryInterface: ChromeUtils.generateQI(["nsIPromptService"]),
    confirmEx() {
      return 1;
    },
  };
  Services.prompt = promptService;
  registerCleanupFunction(() => {
    Services.prompt = prompt;
  });

  // Click the "remove extension" context menu item, and wait until the menu is
  // closed.
  const hidden = BrowserTestUtils.waitForEvent(contextMenu, "popuphidden");
  contextMenu.activateItem(removeButton);
  await hidden;

  // Re-open the panel to make sure the extension is still there.
  await openExtensionsPanel();
  const item = getUnifiedExtensionsItem(extension.id);
  is(
    item.querySelector(".unified-extensions-item-name").textContent,
    "an extension",
    "expected extension to still be listed"
  );
  await closeExtensionsPanel();

  await extension.unload();
  // Restore prompt service.
  Services.prompt = prompt;
});

add_task(async function test_open_context_menu_on_click() {
  const [extension] = createExtensions([{ name: "an extension" }]);
  await extension.startup();

  // Open the extension panel.
  await openExtensionsPanel();

  const button = getUnifiedExtensionsItem(extension.id).querySelector(
    ".unified-extensions-item-menu-button"
  );
  ok(button, "expected menu button");

  const contextMenu = document.getElementById(
    "unified-extensions-context-menu"
  );
  ok(contextMenu, "expected menu");

  // Open the context menu with a "right-click".
  const shown = BrowserTestUtils.waitForEvent(contextMenu, "popupshown");
  EventUtils.synthesizeMouseAtCenter(button, { type: "contextmenu" });
  await shown;

  await closeChromeContextMenu(contextMenu.id, null);
  await closeExtensionsPanel();

  await extension.unload();
});

add_task(async function test_open_context_menu_with_keyboard() {
  const [extension] = createExtensions([{ name: "an extension" }]);
  await extension.startup();

  // Open the extension panel.
  await openExtensionsPanel();

  const button = getUnifiedExtensionsItem(extension.id).querySelector(
    ".unified-extensions-item-menu-button"
  );
  ok(button, "expected menu button");
  // Make this button focusable because those (toolbar) buttons are only made
  // focusable when a user is navigating with the keyboard, which isn't exactly
  // what we are doing in this test.
  button.setAttribute("tabindex", "-1");

  const contextMenu = document.getElementById(
    "unified-extensions-context-menu"
  );
  ok(contextMenu, "expected menu");

  // Open the context menu by focusing the button and pressing the SPACE key.
  let shown = BrowserTestUtils.waitForEvent(contextMenu, "popupshown");
  button.focus();
  is(button, document.activeElement, "expected button to be focused");
  EventUtils.synthesizeKey(" ", {});
  await shown;

  await closeChromeContextMenu(contextMenu.id, null);

  if (AppConstants.platform != "macosx") {
    // Open the context menu by focusing the button and pressing the ENTER key.
    // TODO(emilio): Maybe we should harmonize this behavior across platforms,
    // we're inconsistent right now.
    shown = BrowserTestUtils.waitForEvent(contextMenu, "popupshown");
    button.focus();
    is(button, document.activeElement, "expected button to be focused");
    EventUtils.synthesizeKey("KEY_Enter", {});
    await shown;
    await closeChromeContextMenu(contextMenu.id, null);
  }

  await closeExtensionsPanel();

  await extension.unload();
});

add_task(async function test_context_menu_without_browserActionFor_global() {
  const { ExtensionParent } = ChromeUtils.importESModule(
    "resource://gre/modules/ExtensionParent.sys.mjs"
  );
  const { browserActionFor } = ExtensionParent.apiManager.global;
  const cleanup = () => {
    ExtensionParent.apiManager.global.browserActionFor = browserActionFor;
  };
  registerCleanupFunction(cleanup);
  // This is needed to simulate the case where the browserAction API hasn't
  // been loaded yet (since it is lazy-loaded). That could happen when only
  // extensions without browser actions are installed. In which case, the
  // `global.browserActionFor()` function would not be defined yet.
  delete ExtensionParent.apiManager.global.browserActionFor;

  const [extension] = createExtensions([{ name: "an extension" }]);
  await extension.startup();

  // Open the extension panel and then the context menu for the extension that
  // has been loaded above. We expect the context menu to be displayed and no
  // error caused by the lack of `global.browserActionFor()`.
  await openExtensionsPanel();
  // This promise rejects with an error if the implementation does not handle
  // the case where `global.browserActionFor()` is undefined.
  const contextMenu = await openUnifiedExtensionsContextMenu(extension.id);
  assertVisibleContextMenuItems(contextMenu, 3);

  await closeChromeContextMenu(contextMenu.id, null);
  await closeExtensionsPanel();

  await extension.unload();

  cleanup();
});

add_task(async function test_page_action_context_menu() {
  const extWithMenuPageAction = ExtensionTestUtils.loadExtension({
    manifest: {
      page_action: {},
      permissions: ["contextMenus"],
    },
    background() {
      browser.contextMenus.create(
        {
          id: "some-menu-id",
          title: "Click me!",
          contexts: ["all"],
        },
        () => browser.test.sendMessage("menu-created")
      );
    },
    useAddonManager: "temporary",
  });
  const extWithoutMenu1 = ExtensionTestUtils.loadExtension({
    manifest: {
      name: "extension without any menu",
    },
    useAddonManager: "temporary",
  });

  const extensions = [extWithMenuPageAction, extWithoutMenu1];

  await Promise.all(extensions.map(extension => extension.startup()));

  await extWithMenuPageAction.awaitMessage("menu-created");

  await openExtensionsPanel();

  info("extension with page action and a menu");
  // This extension declares a page action so its menu shouldn't be added to
  // the unified extensions context menu.
  let contextMenu = await openUnifiedExtensionsContextMenu(
    extWithMenuPageAction.id
  );
  assertVisibleContextMenuItems(contextMenu, 3);
  await closeChromeContextMenu(contextMenu.id, null);

  info("extension with no browser action and no menu");
  // There is no context menu created by this extension, so there should only
  // be 3 menu items corresponding to the default manage/remove/report items.
  contextMenu = await openUnifiedExtensionsContextMenu(extWithoutMenu1.id);
  assertVisibleContextMenuItems(contextMenu, 3);
  await closeChromeContextMenu(contextMenu.id, null);

  await closeExtensionsPanel();

  await Promise.all(extensions.map(extension => extension.unload()));
});

add_task(async function test_pin_to_toolbar() {
  const [extension] = createExtensions([
    { name: "an extension", browser_action: {} },
  ]);
  await extension.startup();

  // Open the extension panel, then open the context menu for the extension and
  // pin the extension to the toolbar.
  await openExtensionsPanel();
  await pinToToolbar(extension);

  // Undo the 'pin to toolbar' action.
  await CustomizableUI.reset();
  await extension.unload();
});

add_task(async function test_contextmenu_command_closes_panel() {
  const extension = ExtensionTestUtils.loadExtension({
    manifest: {
      name: "an extension",
      browser_action: {},
      permissions: ["contextMenus"],
    },
    background() {
      browser.contextMenus.create(
        {
          id: "some-menu-id",
          title: "Click me!",
          contexts: ["all"],
        },
        () => browser.test.sendMessage("menu-created")
      );
    },
    useAddonManager: "temporary",
  });
  await extension.startup();
  await extension.awaitMessage("menu-created");

  await openExtensionsPanel();
  const contextMenu = await openUnifiedExtensionsContextMenu(extension.id);

  const firstMenuItem = contextMenu.querySelector("menuitem");
  is(
    firstMenuItem?.getAttribute("label"),
    "Click me!",
    "expected custom menu item as first child"
  );

  const hidden = BrowserTestUtils.waitForEvent(
    gUnifiedExtensions.panel,
    "popuphidden",
    true
  );
  contextMenu.activateItem(firstMenuItem);
  await hidden;

  await extension.unload();
});

add_task(async function test_contextmenu_reorder_extensions() {
  const [ext1, ext2, ext3] = createExtensions([
    { name: "ext1", browser_action: {} },
    { name: "ext2", browser_action: {} },
    { name: "ext3", browser_action: {} },
  ]);
  // Start the test extensions in sequence to reduce chance of
  // intermittent failures when asserting the order of the
  // entries in the panel in the rest of this test task.
  await ext1.startup();
  await ext2.startup();
  await ext3.startup();

  await openExtensionsPanel();

  // First extension in the list should only have "Move Down".
  await assertMoveContextMenuItems(ext1, {
    expectMoveUpHidden: true,
    expectMoveDownHidden: false,
  });

  // Second extension in the list should have "Move Up" and "Move Down".
  await assertMoveContextMenuItems(ext2, {
    expectMoveUpHidden: false,
    expectMoveDownHidden: false,
  });

  // Third extension in the list should only have "Move Up".
  await assertMoveContextMenuItems(ext3, {
    expectMoveUpHidden: false,
    expectMoveDownHidden: true,
    expectOrder: [ext1, ext2, ext3],
  });

  // Let's move some extensions now. We'll start by moving ext1 down until it
  // is positioned at the end of the list.
  info("Move down ext1 action to the bottom of the list");
  await moveWidgetDown(ext1);
  assertOrderOfWidgetsInPanel([ext2, ext1, ext3]);
  await moveWidgetDown(ext1);

  // Verify that the extension 1 has the right context menu items now that it
  // is located at the end of the list.
  await assertMoveContextMenuItems(ext1, {
    expectMoveUpHidden: false,
    expectMoveDownHidden: true,
    expectOrder: [ext2, ext3, ext1],
  });

  info("Move up ext1 action to the top of the list");
  await moveWidgetUp(ext1);
  assertOrderOfWidgetsInPanel([ext2, ext1, ext3]);

  await moveWidgetUp(ext1);
  assertOrderOfWidgetsInPanel([ext1, ext2, ext3]);

  // Move the last extension up.
  info("Move up ext3 action");
  await moveWidgetUp(ext3);
  assertOrderOfWidgetsInPanel([ext1, ext3, ext2]);

  // Move the last extension up (again).
  info("Move up ext2 action to the top of the list");
  await moveWidgetUp(ext2);
  assertOrderOfWidgetsInPanel([ext1, ext2, ext3]);

  // Move the second extension up.
  await moveWidgetUp(ext2);
  assertOrderOfWidgetsInPanel([ext2, ext1, ext3]);

  // Pin an extension to the toolbar, which should remove it from the panel.
  info("Pin ext1 action to the toolbar");
  await pinToToolbar(ext1);
  await openExtensionsPanel();
  assertOrderOfWidgetsInPanel([ext2, ext3]);
  await closeExtensionsPanel();

  await Promise.all([ext1.unload(), ext2.unload(), ext3.unload()]);
  await CustomizableUI.reset();
});

add_task(async function test_contextmenu_only_one_widget() {
  const [extension] = createExtensions([{ name: "ext1", browser_action: {} }]);
  await extension.startup();

  await openExtensionsPanel();
  await assertMoveContextMenuItems(extension, {
    expectMoveUpHidden: true,
    expectMoveDownHidden: true,
  });
  await closeExtensionsPanel();

  await extension.unload();
  await CustomizableUI.reset();
});

add_task(
  async function test_contextmenu_reorder_extensions_with_private_window() {
    // We want a panel in private mode that looks like this one (ext2 is not
    // allowed in PB mode):
    //
    // - ext1
    // - ext3
    //
    // But if we ask CUI to list the widgets in the panel, it would list:
    //
    // - ext1
    // - ext2
    // - ext3
    //
    const ext1 = ExtensionTestUtils.loadExtension({
      manifest: {
        name: "ext1",
        browser_specific_settings: { gecko: { id: "ext1@reorder-private" } },
        browser_action: {},
      },
      useAddonManager: "temporary",
      incognitoOverride: "spanning",
    });
    await ext1.startup();

    const ext2 = ExtensionTestUtils.loadExtension({
      manifest: {
        name: "ext2",
        browser_specific_settings: { gecko: { id: "ext2@reorder-private" } },
        browser_action: {},
      },
      useAddonManager: "temporary",
      incognitoOverride: "not_allowed",
    });
    await ext2.startup();

    const ext3 = ExtensionTestUtils.loadExtension({
      manifest: {
        name: "ext3",
        browser_specific_settings: { gecko: { id: "ext3@reorder-private" } },
        browser_action: {},
      },
      useAddonManager: "temporary",
      incognitoOverride: "spanning",
    });
    await ext3.startup();

    // Make sure all extension widgets are in the correct order.
    assertOrderOfWidgetsInPanel([ext1, ext2, ext3]);

    const privateWin = await BrowserTestUtils.openNewBrowserWindow({
      private: true,
    });

    await openExtensionsPanel(privateWin);

    // First extension in the list should only have "Move Down".
    await assertMoveContextMenuItems(
      ext1,
      {
        expectMoveUpHidden: true,
        expectMoveDownHidden: false,
        expectOrder: [ext1, ext3],
      },
      privateWin
    );

    // Second extension in the list (which is ext3) should only have "Move Up".
    await assertMoveContextMenuItems(
      ext3,
      {
        expectMoveUpHidden: false,
        expectMoveDownHidden: true,
        expectOrder: [ext1, ext3],
      },
      privateWin
    );

    // In private mode, we should only have two CUI widget nodes in the panel.
    assertOrderOfWidgetsInPanel([ext1, ext3], privateWin);

    info("Move ext1 down");
    await moveWidgetDown(ext1, privateWin);
    // The new order in a regular window should be:
    assertOrderOfWidgetsInPanel([ext2, ext3, ext1]);
    // ... while the order in the private window should be:
    assertOrderOfWidgetsInPanel([ext3, ext1], privateWin);

    // Verify that the extension 1 has the right context menu items now that it
    // is located at the end of the list in PB mode.
    await assertMoveContextMenuItems(
      ext1,
      {
        expectMoveUpHidden: false,
        expectMoveDownHidden: true,
        expectOrder: [ext3, ext1],
      },
      privateWin
    );

    // Verify that the extension 3 has the right context menu items now that it
    // is located at the top of the list in PB mode.
    await assertMoveContextMenuItems(
      ext3,
      {
        expectMoveUpHidden: true,
        expectMoveDownHidden: false,
        expectOrder: [ext3, ext1],
      },
      privateWin
    );

    info("Move ext3 extension down");
    await moveWidgetDown(ext3, privateWin);
    // The new order in a regular window should be:
    assertOrderOfWidgetsInPanel([ext2, ext1, ext3]);
    // ... while the order in the private window should be:
    assertOrderOfWidgetsInPanel([ext1, ext3], privateWin);

    // Pin an extension to the toolbar, which should remove it from the panel.
    info("Pin ext1 to the toolbar");
    await pinToToolbar(ext1, privateWin);
    await openExtensionsPanel(privateWin);

    // The new order in a regular window should be:
    assertOrderOfWidgetsInPanel([ext2, ext3]);
    await assertMoveContextMenuItems(
      ext3,
      {
        expectMoveUpHidden: true,
        expectMoveDownHidden: true,
        // ... while the order in the private window should be:
        expectOrder: [ext3],
      },
      privateWin
    );

    await closeExtensionsPanel(privateWin);

    await Promise.all([ext1.unload(), ext2.unload(), ext3.unload()]);
    await CustomizableUI.reset();

    await BrowserTestUtils.closeWindow(privateWin);
  }
);
