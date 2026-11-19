/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async () => {
  // turn off animations for this test
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.animation.enabled", false]],
  });
  await SidebarTestUtils.ensureLauncherVisible(window);
  Assert.equal(
    SidebarController.sidebarRevampVisibility,
    "hide-on-close",
    "revamp.visibility has the expected value"
  );
});

const initialTabDirection = Services.prefs.getBoolPref(VERTICAL_TABS_PREF)
  ? "vertical"
  : "horizontal";

add_task(async function test_extension_context_menu() {
  const win = await BrowserTestUtils.openNewBrowserWindow();
  await waitForBrowserWindowActive(win);
  await SidebarTestUtils.ensureLauncherVisible(win);
  const { document } = win;
  const sidebar = document.querySelector("sidebar-main");
  ok(BrowserTestUtils.isVisible(sidebar), "Sidebar is shown.");

  const manageStub = sinon.stub(sidebar, "manageExtension");
  const reportStub = sinon.stub(sidebar, "reportExtension");
  const removeStub = sinon.stub(sidebar, "removeExtension");

  const extension = ExtensionTestUtils.loadExtension({ ...extData });
  await extension.startup();
  // TODO: Once `sidebar.revamp` is either enabled by default, or removed
  // entirely, this test should run in the current window, and it should only
  // await one "sidebar" message. Bug 1896421
  await extension.awaitMessage("sidebar");
  await extension.awaitMessage("sidebar");
  is(sidebar.extensionButtons.length, 1, "Extension is shown in the sidebar.");

  const contextMenu = document.getElementById("sidebar-context-menu");
  is(contextMenu.state, "closed", "Checking if context menu is closed");

  await Promise.all([
    BrowserTestUtils.waitForEvent(sidebar, "sidebar-contextmenu-ready"),
    openAndWaitForContextMenu(contextMenu, sidebar.extensionButtons[0]),
  ]);
  // Check sidebar context menu buttons are hidden
  ok(
    document.getElementById("sidebar-context-menu-hide-sidebar").hidden,
    "Hide sidebar button is hidden"
  );
  ok(
    document.getElementById("sidebar-context-menu-enable-vertical-tabs").hidden,
    "Enable vertical tabs button is hidden"
  );
  ok(
    document.getElementById("sidebar-context-menu-customize-sidebar").hidden,
    "Customize sidebar button is hidden"
  );
  // There should be a separator in this menu
  Assert.ok(
    !contextMenu.querySelector("menuseparator").hidden,
    "menuseparator is visible"
  );
  contextMenu.hidePopup();

  await Promise.all([
    BrowserTestUtils.waitForEvent(sidebar, "sidebar-contextmenu-ready"),
    openAndWaitForContextMenu(contextMenu, sidebar.extensionButtons[0]),
  ]);
  // Click "Manage Extension"
  const manageExtensionButtonEl = document.getElementById(
    "sidebar-context-menu-manage-extension"
  );
  manageExtensionButtonEl.click();

  ok(manageStub.called, "Manage Extension called");
  contextMenu.hidePopup();

  await Promise.all([
    BrowserTestUtils.waitForEvent(sidebar, "sidebar-contextmenu-ready"),
    openAndWaitForContextMenu(contextMenu, sidebar.extensionButtons[0]),
  ]);
  // Click "Report Extension"
  const reportExtensionButtonEl = document.getElementById(
    "sidebar-context-menu-report-extension"
  );
  reportExtensionButtonEl.click();
  ok(reportStub.called, "Report Extension called");
  contextMenu.hidePopup();

  await Promise.all([
    BrowserTestUtils.waitForEvent(sidebar, "sidebar-contextmenu-ready"),
    openAndWaitForContextMenu(contextMenu, sidebar.extensionButtons[0]),
  ]);
  // Click "Remove Extension"
  document.getElementById("sidebar-context-menu-remove-extension").click();
  ok(removeStub.called, "Remove Extension called");
  contextMenu.hidePopup();

  info(
    "Verify report context menu disabled/enabled based on about:config pref"
  );
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.abuseReport.enabled", false]],
  });

  await Promise.all([
    BrowserTestUtils.waitForEvent(sidebar, "sidebar-contextmenu-ready"),
    openAndWaitForContextMenu(contextMenu, sidebar.extensionButtons[0]),
  ]);
  is(
    document.getElementById("sidebar-context-menu-report-extension").disabled,
    true,
    "Expect report item to be disabled"
  );
  contextMenu.hidePopup();
  await SpecialPowers.popPrefEnv();

  await Promise.all([
    BrowserTestUtils.waitForEvent(sidebar, "sidebar-contextmenu-ready"),
    openAndWaitForContextMenu(contextMenu, sidebar.extensionButtons[0]),
  ]);
  is(
    document.getElementById("sidebar-context-menu-report-extension").disabled,
    false,
    "Expect report item to be enabled"
  );
  contextMenu.hidePopup();

  info(
    "Verify remove context menu disabled/enabled based on addon uninstall permission"
  );
  const { EnterprisePolicyTesting } = ChromeUtils.importESModule(
    "resource://testing-common/EnterprisePolicyTesting.sys.mjs"
  );
  await EnterprisePolicyTesting.setupPolicyEngineWithJson({
    policies: {
      ExtensionSettings: {
        [extension.id]: { installation_mode: "force_installed" },
      },
    },
  });
  await Promise.all([
    BrowserTestUtils.waitForEvent(sidebar, "sidebar-contextmenu-ready"),
    openAndWaitForContextMenu(contextMenu, sidebar.extensionButtons[0]),
  ]);
  is(
    document.getElementById("sidebar-context-menu-remove-extension").disabled,
    true,
    "Expect remove item to be disabled"
  );
  await EnterprisePolicyTesting.setupPolicyEngineWithJson("");
  contextMenu.hidePopup();

  await Promise.all([
    BrowserTestUtils.waitForEvent(sidebar, "sidebar-contextmenu-ready"),
    openAndWaitForContextMenu(contextMenu, sidebar.extensionButtons[0]),
  ]);
  is(
    document.getElementById("sidebar-context-menu-remove-extension").disabled,
    false,
    "Expect remove item to be enabled"
  );
  contextMenu.hidePopup();

  await Promise.all([
    BrowserTestUtils.waitForEvent(sidebar, "sidebar-contextmenu-ready"),
    openAndWaitForContextMenu(contextMenu, sidebar.extensionButtons[0]),
  ]);
  // Click "Remove from Sidebar"
  document.getElementById("sidebar-context-menu-unpin-extension").click();
  await sidebar.updateComplete;
  is(
    sidebar.extensionButtons.length,
    0,
    "Extension is removed from the sidebar."
  );

  sinon.restore();
  await extension.unload();
  ok(
    BrowserTestUtils.isVisible(sidebar),
    "Unloading the extension does not cause the sidebar launcher to hide"
  );
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_sidebar_context_menu() {
  const { sidebarMain, sidebarContainer } = SidebarController;
  await SidebarTestUtils.ensureLauncherVisible(window);

  const contextMenu = document.getElementById("sidebar-context-menu");
  is(contextMenu.state, "closed", "Checking if context menu is closed");

  await Promise.all([
    BrowserTestUtils.waitForEvent(sidebarMain, "sidebar-contextmenu-ready"),
    openAndWaitForContextMenu(contextMenu, sidebarMain),
  ]);
  ok(
    document.getElementById("sidebar-context-menu-report-extension").hidden,
    "Report extension button is hidden"
  );
  ok(
    document.getElementById("sidebar-context-menu-remove-extension").hidden,
    "Remove extension tabs button is hidden"
  );
  ok(
    document.getElementById("sidebar-context-menu-manage-extension").hidden,
    "Manage extension button is hidden"
  );
  ok(
    document.getElementById("sidebar-context-menu-unpin-extension").hidden,
    "Remove extension from Sidebar button is hidden"
  );
  contextMenu.hidePopup();

  await Promise.all([
    BrowserTestUtils.waitForEvent(sidebarMain, "sidebar-contextmenu-ready"),
    openAndWaitForContextMenu(contextMenu, sidebarMain),
  ]);
  // Click customize sidebar
  const customizeSidebarMenuItem = document.getElementById(
    "sidebar-context-menu-customize-sidebar"
  );
  ok(
    BrowserTestUtils.isVisible(customizeSidebarMenuItem),
    "The customize menu item is visible"
  );
  const promiseSidebarFocused = BrowserTestUtils.waitForEvent(
    window,
    "SidebarFocused"
  );

  customizeSidebarMenuItem.click();
  // Wait for the sidebar panel to load before continuing
  await promiseSidebarFocused;

  is(
    SidebarController.currentID,
    "viewCustomizeSidebar",
    "Customize sidebar panel is open"
  );
  // Calling .click() directly doesn't dismiss the context menu popop, we need to do that.
  contextMenu.hidePopup();

  await Promise.all([
    BrowserTestUtils.waitForEvent(sidebarMain, "sidebar-contextmenu-ready"),
    openAndWaitForContextMenu(contextMenu, sidebarMain),
  ]);
  // Click hide sidebar
  const hideSidebarMenuItem = document.getElementById(
    "sidebar-context-menu-hide-sidebar"
  );
  hideSidebarMenuItem.click();
  await BrowserTestUtils.waitForMutationCondition(
    SidebarController.sidebarContainer,
    { attributes: true, attributeFilter: ["hidden"] },
    () => sidebarContainer.hidden
  );
  contextMenu.hidePopup();

  ok(sidebarContainer.hidden, "Sidebar is not visible");
  ok(!SidebarController.isOpen, "Sidebar panel is closed");

  await SidebarTestUtils.ensureLauncherVisible(window);
  await openAndWaitForContextMenu(contextMenu, sidebarMain);
  // Click turn on vertical tabs
  const enableVerticalTabsMenuItem = document.getElementById(
    "sidebar-context-menu-enable-vertical-tabs"
  );
  enableVerticalTabsMenuItem.click();
  await SidebarTestUtils.waitForTabstripOrientation(window, "vertical");
  contextMenu.hidePopup();
  ok(
    Services.prefs.getBoolPref(VERTICAL_TABS_PREF, false),
    "Vertical tabs disabled"
  );
  Services.prefs.clearUserPref(VERTICAL_TABS_PREF);
  await SidebarTestUtils.waitForTabstripOrientation(
    window,
    initialTabDirection
  );

  is(contextMenu.state, "closed", "Context menu closed for vertical tabs");
});

add_task(async function test_tool_context_menu() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.page", true]],
  });
  const contextMenu = document.getElementById("sidebar-context-menu");
  const { sidebarMain } = SidebarController;
  const aichatTool = sidebarMain.shadowRoot.querySelector(
    'moz-button[view="viewGenaiChatSidebar"]'
  );

  await Promise.all([
    BrowserTestUtils.waitForEvent(sidebarMain, "sidebar-contextmenu-ready"),
    openAndWaitForContextMenu(contextMenu, aichatTool),
  ]);

  Assert.ok(
    document.getElementById("sidebar-context-menu-report-extension").hidden,
    "Report extension button is hidden"
  );
  Assert.ok(
    document.getElementById("sidebar-context-menu-remove-extension").hidden,
    "Remove extension tabs button is hidden"
  );
  Assert.ok(
    document.getElementById("sidebar-context-menu-manage-extension").hidden,
    "Manage extension button is hidden"
  );
  Assert.ok(
    document.getElementById("sidebar-context-menu-hide-sidebar").hidden,
    "Hide sidebar button is hidden"
  );
  Assert.ok(
    document.getElementById("sidebar-context-menu-enable-vertical-tabs").hidden,
    "Enable vertical tab button is hidden"
  );
  Assert.ok(
    document.getElementById("sidebar-context-menu-customize-sidebar").hidden,
    "Customize sidebar button is hidden"
  );
  Assert.ok(
    contextMenu.querySelector("menuseparator").hidden,
    "menuseparator is hidden"
  );

  // The submenu is populated asynchronously.
  await BrowserTestUtils.waitForMutationCondition(
    contextMenu,
    { childList: true, subtree: true, attributes: true },
    () => {
      const toolMenuItems = [
        ...contextMenu.querySelectorAll("[customized-tool='true']"),
      ];
      return (
        !!toolMenuItems.length && toolMenuItems.every(item => !item.hidden)
      );
    }
  );
  Assert.ok(true, "Tool menuitems are visible");

  contextMenu.hidePopup();

  await Promise.all([
    BrowserTestUtils.waitForEvent(sidebarMain, "sidebar-contextmenu-ready"),
    openAndWaitForContextMenu(contextMenu, sidebarMain),
  ]);

  // Check tool menuitems are hidden and sidebar-main context menus are visible
  Assert.ok(
    !document.getElementById("sidebar-context-menu-hide-sidebar").hidden,
    "Hide sidebar button is visible"
  );
  Assert.ok(
    !document.getElementById("sidebar-context-menu-enable-vertical-tabs")
      .hidden,
    "Remove extension tabs button is visible"
  );
  Assert.ok(
    !document.getElementById("sidebar-context-menu-customize-sidebar").hidden,
    "Enable vertical tab button is visible"
  );

  const toolMenuItems = [
    ...contextMenu.querySelectorAll("[customized-tool='true']"),
  ];
  Assert.ok(toolMenuItems[0].hidden, "One of tool menuitems is hidden");

  contextMenu.hidePopup();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_toggle_vertical_tabs_from_sidebar_button() {
  await SpecialPowers.pushPrefEnv({
    set: [[VERTICAL_TABS_PREF, false]],
  });
  await SidebarTestUtils.waitForTabstripOrientation(window, "horizontal");
  Assert.equal(
    Services.prefs.getStringPref(SIDEBAR_VISIBILITY_PREF),
    "hide-on-close",
    "Sanity check the visibilty pref when verticalTabs are disabled"
  );

  info("Enable vertical tabs from right clicking the sidebar-button");
  const toolbarContextMenu = document.getElementById("toolbar-context-menu");
  const toggleMenuItem = document.getElementById(
    "toolbar-context-toggle-vertical-tabs"
  );
  const customizeSidebarItem = document.getElementById(
    "toolbar-context-customize-sidebar"
  );
  const sidebarButton = document.getElementById("sidebar-button");
  await openAndWaitForContextMenu(toolbarContextMenu, sidebarButton);
  Assert.deepEqual(
    document.l10n.getAttributes(toggleMenuItem),
    { id: "toolbar-context-turn-on-vertical-tabs", args: null },
    "Context menu item indicates that it enables vertical tabs."
  );
  toggleMenuItem.click();

  await SidebarTestUtils.waitForTabstripOrientation(window, "vertical");
  ok(gBrowser.tabContainer.verticalMode, "Vertical tabs are enabled.");

  toolbarContextMenu.hidePopup();

  Assert.equal(
    Services.prefs.getStringPref(SIDEBAR_VISIBILITY_PREF),
    "always-show",
    "Sanity check the visibilty pref when verticalTabs are enabled"
  );

  // Open customize sidebar panel from context menu
  await openAndWaitForContextMenu(toolbarContextMenu, sidebarButton);

  customizeSidebarItem.click();
  ok(SidebarController.isOpen, "Sidebar is open");
  Assert.equal(
    SidebarController.currentID,
    "viewCustomizeSidebar",
    "Sidebar should have opened to the customize sidebar panel"
  );
  toolbarContextMenu.hidePopup();

  info("Disable vertical tabs from right clicking the sidebar-button");
  await openAndWaitForContextMenu(toolbarContextMenu, sidebarButton);

  Assert.deepEqual(
    document.l10n.getAttributes(toggleMenuItem),
    { id: "toolbar-context-turn-off-vertical-tabs", args: null },
    "Context menu item indicates that it disables vertical tabs."
  );
  toggleMenuItem.click();

  await SidebarTestUtils.waitForTabstripOrientation(window, "horizontal");
  ok(!gBrowser.tabContainer.verticalMode, "Vertical tabs are disabled.");
  Assert.equal(
    Services.prefs.getStringPref(SIDEBAR_VISIBILITY_PREF),
    "hide-on-close",
    "Sanity check the visibilty pref when verticalTabs are disabled"
  );
  toolbarContextMenu.hidePopup();

  SidebarController.hide();
  await SpecialPowers.popPrefEnv();
  await SidebarController.waitUntilStable();
});
