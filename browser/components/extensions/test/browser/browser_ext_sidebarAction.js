"use strict";

requestLongerTimeout(2);

let extData = {
  manifest: {
    sidebar_action: {
      default_icon: {
        16: "icon.png",
        32: "icon@2x.png",
      },
      default_panel: "sidebar.html",
    },
  },
  useAddonManager: "temporary",

  files: {
    "sidebar.html": `
      <!DOCTYPE html>
      <html>
      <head><meta charset="utf-8"/>
      <script src="sidebar.js"></script>
      </head>
      <body>
      A Test Sidebar
      </body></html>
    `,

    "sidebar.js": function () {
      window.onload = () => {
        browser.test.sendMessage("sidebar");
      };
    },

    "icon.png": imageBuffer,
    "icon@2x.png": imageBuffer,
    "updated-icon.png": imageBuffer,
  },

  background: function () {
    browser.test.onMessage.addListener(async ({ msg, data }) => {
      if (msg === "set-panel") {
        await browser.sidebarAction.setPanel({ panel: null });
        browser.test.assertEq(
          await browser.sidebarAction.getPanel({}),
          browser.runtime.getURL("sidebar.html"),
          "Global panel can be reverted to the default."
        );
      } else if (msg === "isOpen") {
        let { arg = {}, result } = data;
        let isOpen = await browser.sidebarAction.isOpen(arg);
        browser.test.assertEq(result, isOpen, "expected value from isOpen");
      } else if (msg === "set-icon") {
        await browser.sidebarAction.setIcon({ path: data });
      }
      browser.test.sendMessage("done");
    });
  },
};

function getExtData(manifestUpdates = {}) {
  return {
    ...extData,
    manifest: {
      ...extData.manifest,
      ...manifestUpdates,
    },
  };
}

async function sendMessage(ext, msg, data = undefined) {
  ext.sendMessage({ msg, data });
  await ext.awaitMessage("done");
}

add_setup(() =>
  SpecialPowers.pushPrefEnv({
    set: [["layout.css.devPixelsPerPx", 1]],
  })
);
registerCleanupFunction(() => SpecialPowers.popPrefEnv());

add_task(async function sidebar_initial_install() {
  ok(
    document.getElementById("sidebar-box").hidden,
    "sidebar box is not visible"
  );
  let extData = getExtData({
    browser_specific_settings: { gecko: { id: "@sidebar" } },
    version: "1.0",
  });
  extData.useAddonManager = "permanent";

  let extension = ExtensionTestUtils.loadExtension(extData);
  await extension.startup();
  await extension.awaitMessage("sidebar");

  // Test sidebar is opened on install
  ok(!document.getElementById("sidebar-box").hidden, "sidebar box is visible");

  extData.manifest.version = "1.1";
  let updatedExt = ExtensionTestUtils.loadExtension(extData);
  await updatedExt.startup();
  await updatedExt.awaitMessage("sidebar");

  // Test sidebar is still opened after update.
  ok(!document.getElementById("sidebar-box").hidden, "sidebar still visible");
  await updatedExt.unload();

  await extension.unload();
  // Test that the sidebar was closed on unload.
  ok(
    document.getElementById("sidebar-box").hidden,
    "sidebar box is not visible"
  );
});

add_task(async function sidebar__install_closed() {
  let sidebarBox = document.getElementById("sidebar-box");
  ok(sidebarBox.hidden, "sidebar box is not visible");
  let tempExtData = getExtData();
  tempExtData.manifest.sidebar_action.open_at_install = false;
  let extension = ExtensionTestUtils.loadExtension(tempExtData);
  await extension.startup();

  if (Services.prefs.getBoolPref("sidebar.revamp", false)) {
    // The launcher toolbar will show automatically when an extension with sidebar
    // action is installed. Wait for that to avoid racing the `.show()` below.
    await BrowserTestUtils.waitForMutationCondition(
      SidebarController.sidebarMain,
      { attributes: true, attributeFilter: ["hidden"] },
      () => !SidebarController.sidebarMain.hidden,
      `Sidebar launcher element should be un-hidden`
    );
  }

  // Test sidebar panel is closed on install
  ok(sidebarBox.hidden, "sidebar box is hidden");

  await SidebarController.show(`${makeWidgetId(extension.id)}-sidebar-action`);
  ok(!sidebarBox.hidden, "Opened by the user.");

  SidebarController.hide({ dismissPanel: true });
  ok(sidebarBox.hidden, "Hidden by the user.");

  info("Reloading to verify the sidebar panel stays closed.");
  let addon = await AddonManager.getAddonByID(extension.id);
  await addon.reload();

  ok(sidebarBox.hidden, "Hidden after reload.");

  await extension.unload();
  // This is the default value
  tempExtData.manifest.sidebar_action.open_at_install = true;
});

add_task(async function sidebar_two_sidebar_addons() {
  let extension2 = ExtensionTestUtils.loadExtension(getExtData());
  await extension2.startup();
  // Test sidebar is opened on install
  await extension2.awaitMessage("sidebar");
  ok(!document.getElementById("sidebar-box").hidden, "sidebar box is visible");

  // Test second sidebar install opens new sidebar
  let extension3 = ExtensionTestUtils.loadExtension(getExtData());
  await extension3.startup();
  // Test sidebar is opened on install
  await extension3.awaitMessage("sidebar");
  ok(!document.getElementById("sidebar-box").hidden, "sidebar box is visible");
  await extension3.unload();

  // We just close the sidebar on uninstall of the current sidebar.
  ok(
    document.getElementById("sidebar-box").hidden,
    "sidebar box is not visible"
  );

  await extension2.unload();
});

add_task(async function sidebar_empty_panel() {
  let extension = ExtensionTestUtils.loadExtension(getExtData());
  await extension.startup();
  // Test sidebar is opened on install
  await extension.awaitMessage("sidebar");
  ok(
    !document.getElementById("sidebar-box").hidden,
    "sidebar box is visible in first window"
  );
  await sendMessage(extension, "set-panel");
  await extension.unload();
});

add_task(async function sidebar_isOpen() {
  info("Load extension1");
  let extension1 = ExtensionTestUtils.loadExtension(getExtData());
  await extension1.startup();

  info("Test extension1's sidebar is opened on install");
  await extension1.awaitMessage("sidebar");
  await sendMessage(extension1, "isOpen", { result: true });
  let sidebar1ID = SidebarController.currentID;

  info("Load extension2");
  let extension2 = ExtensionTestUtils.loadExtension(getExtData());
  await extension2.startup();

  info("Test extension2's sidebar is opened on install");
  await extension2.awaitMessage("sidebar");
  await sendMessage(extension1, "isOpen", { result: false });
  await sendMessage(extension2, "isOpen", { result: true });

  info("Switch back to extension1's sidebar");
  await SidebarController.show(sidebar1ID);
  await extension1.awaitMessage("sidebar");
  await sendMessage(extension1, "isOpen", { result: true });
  await sendMessage(extension2, "isOpen", { result: false });

  info("Test passing a windowId parameter");
  let windowId = window.docShell.outerWindowID;
  let WINDOW_ID_CURRENT = -2;
  await sendMessage(extension1, "isOpen", { arg: { windowId }, result: true });
  await sendMessage(extension2, "isOpen", { arg: { windowId }, result: false });
  await sendMessage(extension1, "isOpen", {
    arg: { windowId: WINDOW_ID_CURRENT },
    result: true,
  });
  await sendMessage(extension2, "isOpen", {
    arg: { windowId: WINDOW_ID_CURRENT },
    result: false,
  });

  info("Open a new window");
  open("", "", "noopener");
  let newWin = Services.wm.getMostRecentWindow("navigator:browser");

  info("The new window has no sidebar");
  await sendMessage(extension1, "isOpen", { result: false });
  await sendMessage(extension2, "isOpen", { result: false });

  info("But the original window still does");
  await sendMessage(extension1, "isOpen", { arg: { windowId }, result: true });
  await sendMessage(extension2, "isOpen", { arg: { windowId }, result: false });

  info("Close the new window");
  newWin.close();

  info("Close the sidebar in the original window");
  SidebarController.hide({ dismissPanel: true });
  await sendMessage(extension1, "isOpen", { result: false });
  await sendMessage(extension2, "isOpen", { result: false });

  await extension1.unload();
  await extension2.unload();
});

add_task(async function testShortcuts() {
  // This test covers the access key expected to be set on the switcher panel
  // element for the extension, which is not part of the new sidebar design.
  if (Services.prefs.getBoolPref("sidebar.revamp", false)) {
    info("skipping test because sidebar.revamp is set");
    return;
  }

  function verifyShortcut(id, commandKey, win = window) {
    const doc = win.document;
    // We're just testing the command key since the modifiers have different
    // icons on different platforms.
    let menuitem = doc.getElementById(
      `sidebarswitcher_menu_${makeWidgetId(id)}-sidebar-action`
    );
    ok(menuitem, `Expect a menuitem for ${id}`);
    ok(menuitem.hasAttribute("key"), "The menu item has a key specified");
    let key = doc.getElementById(menuitem.getAttribute("key"));
    ok(key, "The key attribute finds the related key element");
    ok(
      menuitem.getAttribute("acceltext").endsWith(commandKey),
      "The shortcut has the right key"
    );
  }

  async function toggleSwitcherPanel(win = window) {
    // Open and close the switcher panel to trigger shortcut content rendering.
    let switcherPanelShown = promisePopupShown(
      win.SidebarController._switcherPanel
    );
    win.SidebarController.showSwitcherPanel();
    await switcherPanelShown;
    let switcherPanelHidden = promisePopupHidden(
      win.SidebarController._switcherPanel
    );
    win.SidebarController.hideSwitcherPanel();
    await switcherPanelHidden;
  }

  let extension1 = ExtensionTestUtils.loadExtension(
    getExtData({
      commands: {
        _execute_sidebar_action: {
          suggested_key: {
            default: "Ctrl+Shift+I",
          },
        },
      },
    })
  );
  let extension2 = ExtensionTestUtils.loadExtension(
    getExtData({
      commands: {
        _execute_sidebar_action: {
          suggested_key: {
            default: "Ctrl+Shift+E",
          },
        },
      },
    })
  );

  await extension1.startup();
  await extension1.awaitMessage("sidebar");
  await extension2.startup();
  await extension2.awaitMessage("sidebar");

  info("Open a second window");
  const win = await BrowserTestUtils.openNewBrowserWindow();
  info("Wait for extension2 sidebar to be open in the new window");
  await extension2.awaitMessage("sidebar");

  info("Toggle switcher panel");
  await toggleSwitcherPanel();
  await toggleSwitcherPanel(win);

  // Test that the key is set for the extension after the shortcuts are rendered.
  verifyShortcut(extension1.id, "I");
  verifyShortcut(extension1.id, "I", win);

  // Once the switcher panel has been opened new shortcuts should be added
  // automatically.
  verifyShortcut(extension2.id, "E");
  verifyShortcut(extension2.id, "E", win);

  // Regression test (see Bug 1881820).
  info(
    "Reload the addon and verify the sidebar shortcut still works as expected"
  );
  const addon = await AddonManager.getAddonByID(extension1.id);
  await addon.reload();

  const keysetId1 = `#ext-keyset-id-${makeWidgetId(extension1.id)}`;
  Assert.equal(
    window.document.querySelectorAll(keysetId1).length,
    1,
    "Expect no keyset leaked in the 1st window after addon reload"
  );
  Assert.equal(
    win.document.querySelectorAll(keysetId1).length,
    1,
    "Expect no keyset leaked in the 2nd window after addon reload"
  );

  await extension1.unload();
  await extension2.unload();

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function sidebar_action_icon_update() {
  const isNewSidebar = Services.prefs.getBoolPref("sidebar.revamp", false);
  const sidebar = isNewSidebar && document.querySelector("sidebar-main");

  info("Load extension");
  const extension = ExtensionTestUtils.loadExtension(getExtData());
  await extension.startup();

  info("Test extension's sidebar is opened on install");
  await extension.awaitMessage("sidebar");
  await sendMessage(extension, "isOpen", { result: true });
  const sidebarID = SidebarController.currentID;

  let iconUrl = `moz-extension://${extension.uuid}/icon.png`;

  if (isNewSidebar) {
    ok(sidebar, "sidebar is shown");
    const { iconSrc } = sidebar.extensionButtons[0];
    is(iconSrc, iconUrl, "Extension has the correct icon.");
  } else {
    const item = SidebarController._switcherPanel.querySelector(
      ".webextension-menuitem"
    );
    is(
      item.style.getPropertyValue("--webextension-menuitem-image"),
      `url("${iconUrl}")`,
      "Extension has the correct icon."
    );
  }
  const menuItem = document.getElementById(`menubar_menu_${sidebarID}`);
  is(
    menuItem.style.getPropertyValue("--webextension-menuitem-image"),
    `url("${iconUrl}")`,
    "View>Sidebars menu item has the correct icon."
  );

  SidebarController.hide({ dismissPanel: true });
  await sendMessage(extension, "isOpen", { result: false });

  await sendMessage(extension, "set-icon", "1.png");
  await SidebarController.show(sidebarID);
  await extension.awaitMessage("sidebar");
  await sendMessage(extension, "isOpen", { result: true });
  iconUrl = `moz-extension://${extension.uuid}/1.png`;

  const updatedMenuItem = document.getElementById(`menubar_menu_${sidebarID}`);
  is(
    updatedMenuItem.style.getPropertyValue("--webextension-menuitem-image"),
    `url("${iconUrl}")`,
    "View>Sidebars menu item has the updated icon."
  );

  if (isNewSidebar) {
    const { iconSrc } = sidebar.extensionButtons[0];
    is(iconSrc, iconUrl, "Extension has updated icon.");
  } else {
    const item = SidebarController._switcherPanel.querySelector(
      ".webextension-menuitem"
    );
    is(
      item.style.getPropertyValue("--webextension-menuitem-image"),
      `url("${iconUrl}")`,
      "Extension has updated icon."
    );
  }

  await extension.unload();
});

add_task(async function sidebar_action_hidpi_icon() {
  await SpecialPowers.pushPrefEnv({
    set: [["layout.css.devPixelsPerPx", 2]],
  });

  const isNewSidebar = Services.prefs.getBoolPref("sidebar.revamp", false);

  info("Load extension");
  const extension = ExtensionTestUtils.loadExtension(getExtData());
  await extension.startup();

  info("Test extension's sidebar is opened on install");
  await extension.awaitMessage("sidebar");
  await sendMessage(extension, "isOpen", { result: true });
  const sidebarID = SidebarController.currentID;

  let icon2xUrl = `moz-extension://${extension.uuid}/icon@2x.png`;

  if (isNewSidebar) {
    const sidebar = document.querySelector("sidebar-main");
    const { iconSrc } = sidebar.extensionButtons[0];
    is(
      iconSrc,
      icon2xUrl,
      "Extension has the correct icon for HiDPI displays."
    );
  } else {
    const item = SidebarController._switcherPanel.querySelector(
      ".webextension-menuitem"
    );
    is(
      item.style.getPropertyValue("--webextension-menuitem-image"),
      `url("${icon2xUrl}")`,
      "Extension has the correct icon for HiDPI displays."
    );
  }

  const menuItem = document.getElementById(`menubar_menu_${sidebarID}`);
  is(
    menuItem.style.getPropertyValue("--webextension-menuitem-image"),
    `url("${icon2xUrl}")`,
    "View>Sidebars menu item has the correct icon for HiDPI displays."
  );

  await extension.unload();
  await SpecialPowers.popPrefEnv();
});

add_task(async function sidebar_switcher_label_bug1905771_regression_test() {
  // This test covers a regression specific to the current sidebar design and
  // doesn't need to be covered with the new sidebar design.
  if (Services.prefs.getBoolPref("sidebar.revamp", false)) {
    info("skipping test because sidebar.revamp is set");
    return;
  }

  let extData = getExtData({
    name: "Test Extension",
    browser_specific_settings: { gecko: { id: "@sidebar" } },
    version: "1.0",
    sidebar_action: {
      default_icon: {
        16: "icon.png",
        32: "icon@2x.png",
      },
      default_panel: "sidebar.html",
      open_at_install: true,
    },
  });
  extData.useAddonManager = "permanent";

  await window.SidebarController.promiseInitialized;
  let extension = ExtensionTestUtils.loadExtension(extData);
  await extension.startup();
  await extension.awaitMessage("sidebar");

  // Recreate Bug 1905771 conditions:
  // - sidebar extension installed
  // - sidebar extension page selected
  // - open a new tab
  //
  // Under these conditions SidebarController._setExtensionAttributes used to trigger the bug
  // by setting a label DOM attribute on the SidebarController._switcherTarget element if the
  // extension sidebar was the one selected for the SidebarController instance.

  const extSidebarActionID = `${makeWidgetId(extension.id)}-sidebar-action`;
  is(
    window.SidebarController.currentID,
    extSidebarActionID,
    "Expect extension sidebar ID to be set as currently selected"
  );

  await BrowserTestUtils.withNewTab("about:blank", async () => {
    is(
      window.SidebarController._switcherTarget.querySelector(
        "label#sidebar-title"
      ).value,
      extData.manifest.name,
      "sidebar switcher target element textContent expected to be the extension name"
    );
    await window.SidebarController.show("viewBookmarksSidebar");
    is(
      window.SidebarController._switcherTarget.querySelector(
        "label#sidebar-title"
      ).value,
      "Bookmarks",
      "sidebar switcher target element textContent expected to be set to Bookmarks"
    );
    // NOTE: assertion to prevent Bug 1905771 from regressing.
    is(
      window.SidebarController._switcherTarget.getAttribute("label"),
      null,
      "sidebar switcher target element should not have a label attribute set"
    );
  });

  // NOTE: explicitly close the sidebar to prevent perma-failure when running in
  // test-verify mode (due to the sidebar not being inizially closed as the first
  // test ask in this file expects).
  await window.SidebarController.hide({ dismissPanel: true });
  await extension.unload();
});
