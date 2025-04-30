/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ExtensionCommon } = ChromeUtils.importESModule(
  "resource://gre/modules/ExtensionCommon.sys.mjs"
);

add_setup(() =>
  SpecialPowers.pushPrefEnv({
    set: [["layout.css.devPixelsPerPx", 1]],
  })
);

const extData2 = { ...extData };

async function sendMessage(extension, msg, data) {
  extension.sendMessage({ msg, data });
  await extension.awaitMessage("done");
}

add_task(async function test_extension_sidebar_actions() {
  const sidebar = document.querySelector("sidebar-main");
  ok(sidebar, "Sidebar is shown.");

  const extension = ExtensionTestUtils.loadExtension({ ...extData });
  await extension.startup();
  await extension.awaitMessage("sidebar");
  is(sidebar.extensionButtons.length, 1, "Extension is shown in the sidebar.");

  // Default icon and title matches.
  let button = sidebar.extensionButtons[0];
  let iconUrl = `moz-extension://${extension.uuid}/icon.png`;
  is(button.iconSrc, iconUrl, "Extension has the correct icon.");
  is(button.title, "Default Title", "Extension has the correct title.");

  // Icon can be updated.
  await sendMessage(extension, "set-icon", "updated-icon.png");
  await sidebar.updateComplete;
  iconUrl = `moz-extension://${extension.uuid}/updated-icon.png`;
  is(button.iconSrc, iconUrl, "Extension has updated icon.");

  // Title can be updated.
  await sendMessage(extension, "set-title", "Updated Title");
  await sidebar.updateComplete;
  is(button.title, "Updated Title", "Extension has updated title.");

  sidebar.expanded = true;
  await sidebar.updateComplete;
  ok(button.hasVisibleLabel, "Title is visible when sidebar is expanded.");

  sidebar.expanded = false;
  await sidebar.updateComplete;
  ok(!button.hasVisibleLabel, "Title is hidden when sidebar is collapsed.");

  // Panel can be updated.
  await sendMessage(extension, "set-panel", "1.html");
  const panelUrl = `moz-extension://${extension.uuid}/1.html`;
  const browser = SidebarController.browser.contentDocument.getElementById(
    "webext-panels-browser"
  );
  await BrowserTestUtils.browserLoaded(browser, false, panelUrl);

  await extension.unload();
  await sidebar.updateComplete;
  is(
    sidebar.extensionButtons.length,
    0,
    "Extension is removed from the sidebar."
  );
});

add_task(async function test_open_new_window_after_install() {
  const extension = ExtensionTestUtils.loadExtension({ ...extData });
  await extension.startup();
  await extension.awaitMessage("sidebar");

  const sidebar = document.querySelector("sidebar-main");
  ok(sidebar, "Sidebar is shown.");

  is(
    sidebar.extensionButtons.length,
    1,
    "Extension is shown in new browser window."
  );

  await BrowserTestUtils.withNewTab("about:addons", async browser => {
    await BrowserTestUtils.synthesizeMouseAtCenter(
      "categories-box button[name=extension]",
      {},
      browser
    );
    const extensionToggle = await TestUtils.waitForCondition(
      () =>
        browser.contentDocument.querySelector(
          `addon-card[addon-id="${extension.id}"] moz-toggle`
        ),
      "Toggle button for extension is shown."
    );

    let promiseEvent = BrowserTestUtils.waitForEvent(
      window,
      "SidebarItemRemoved"
    );
    extensionToggle.click();
    await promiseEvent;
    await sidebar.updateComplete;
    is(sidebar.extensionButtons.length, 0, "The extension is disabled.");

    promiseEvent = BrowserTestUtils.waitForEvent(window, "SidebarItemAdded");
    extensionToggle.click();
    await promiseEvent;
    await sidebar.updateComplete;
    is(sidebar.extensionButtons.length, 1, "The extension is enabled.");
  });

  await extension.unload();
  await sidebar.updateComplete;
  is(
    sidebar.extensionButtons.length,
    0,
    "Extension is removed from the sidebar."
  );
});

add_task(async function test_open_new_private_window_after_install() {
  const extension = ExtensionTestUtils.loadExtension({ ...extData });
  await extension.startup();
  await extension.awaitMessage("sidebar");

  const privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  const { document } = privateWin;
  const sidebar = document.querySelector("sidebar-main");
  ok(sidebar, "Sidebar is shown.");

  info("Waiting for extension buttons to be present");
  await BrowserTestUtils.waitForMutationCondition(
    sidebar,
    { childList: true, subTree: true },
    () => !!sidebar.extensionButtons
  );
  is(
    sidebar.extensionButtons.length,
    0,
    "Extension is hidden in private browser window."
  );

  await extension.unload();
  await BrowserTestUtils.closeWindow(privateWin);
});

add_task(async function test_customize_sidebar_extensions() {
  const sidebar = document.querySelector("sidebar-main");
  ok(sidebar, "Sidebar is shown.");
  await sidebar.updateComplete;

  const extension = ExtensionTestUtils.loadExtension({ ...extData });
  await extension.startup();
  await extension.awaitMessage("sidebar");
  is(sidebar.extensionButtons.length, 1, "Extension is shown in the sidebar.");

  await toggleSidebarPanel(window, "viewCustomizeSidebar");
  let customizeDocument = SidebarController.browser.contentDocument;
  const customizeComponent =
    customizeDocument.querySelector("sidebar-customize");
  let extensionEntrypointsCount = sidebar.extensionButtons.length;
  is(
    customizeComponent.extensionLinks.length,
    extensionEntrypointsCount,
    `${extensionEntrypointsCount} links to manage sidebar extensions are shown in the Customize Menu.`
  );

  // Default icon and title matches.
  const extensionLink = customizeComponent.extensionLinks[0];
  let iconUrl = `moz-extension://${extension.uuid}/icon.png`;
  let iconEl = extensionLink.closest(".extension-item").querySelector(".icon");
  is(iconEl.src, iconUrl, "Extension has the correct icon.");
  is(
    extensionLink.textContent.trim(),
    "Default Title",
    "Extension has the correct title."
  );

  // Test manage extension
  let browserLoaded = BrowserTestUtils.browserLoaded(
    window.gBrowser,
    false,
    "about:addons"
  );
  extensionLink.click();
  await browserLoaded;
  info("about:addons is the new opened tab");

  await extension.unload();
  await sidebar.updateComplete;
  is(
    sidebar.extensionButtons.length,
    0,
    "Extension is removed from the sidebar."
  );
});

add_task(async function test_extensions_keyboard_navigation() {
  const sidebar = document.querySelector("sidebar-main");

  const extension = ExtensionTestUtils.loadExtension({ ...extData });
  await extension.startup();
  await extension.awaitMessage("sidebar");
  is(sidebar.extensionButtons.length, 1, "Extension is shown in the sidebar.");
  const extension2 = ExtensionTestUtils.loadExtension({ ...extData2 });
  await extension2.startup();
  await extension2.awaitMessage("sidebar");
  is(
    sidebar.extensionButtons.length,
    2,
    "Two extensions are shown in the sidebar."
  );

  await toggleSidebarPanel(window, "viewCustomizeSidebar");
  let customizeDocument = SidebarController.browser.contentDocument;
  const customizeComponent =
    customizeDocument.querySelector("sidebar-customize");
  let extensionEntrypointsCount = sidebar.extensionButtons.length;
  is(
    customizeComponent.extensionLinks.length,
    extensionEntrypointsCount,
    `${extensionEntrypointsCount} links to manage sidebar extensions are shown in the Customize Menu.`
  );

  customizeComponent.extensionLinks[0].focus();
  ok(
    isActiveElement(customizeComponent.extensionLinks[0]),
    "First extension link is focused."
  );

  info("Press Arrow Down key.");
  EventUtils.synthesizeKey("KEY_ArrowDown", {});
  ok(
    isActiveElement(customizeComponent.extensionLinks[1]),
    "Second extension link is focused."
  );

  info("Press Arrow Up key.");
  EventUtils.synthesizeKey("KEY_ArrowUp", {});
  ok(
    isActiveElement(customizeComponent.extensionLinks[0]),
    "First extension link is focused."
  );

  info("Press Enter key.");
  let browserLocationChanged = BrowserTestUtils.waitForLocationChange(
    window.gBrowser,
    "about:addons"
  );
  EventUtils.synthesizeKey("KEY_Enter", {});
  await browserLocationChanged;
  info("about:addons is the new opened tab");

  await extension.unload();
  await extension2.unload();
  await sidebar.updateComplete;
  is(
    sidebar.extensionButtons.length,
    0,
    "Extensions are removed from the sidebar."
  );
});

add_task(async function test_extension_sidebar_hidpi_icon() {
  await SpecialPowers.pushPrefEnv({
    set: [["layout.css.devPixelsPerPx", 2]],
  });

  const sidebar = document.querySelector("sidebar-main");
  ok(sidebar, "Sidebar is shown.");

  const extension = ExtensionTestUtils.loadExtension({ ...extData });
  await extension.startup();
  await extension.awaitMessage("sidebar");

  const { iconSrc } = sidebar.extensionButtons[0];
  is(
    iconSrc,
    `moz-extension://${extension.uuid}/icon@2x.png`,
    "Extension has the correct icon for HiDPI displays."
  );

  await SpecialPowers.popPrefEnv();
  await extension.unload();
});

add_task(async function test_extension_panel_load() {
  const launcher = document.querySelector("sidebar-main");
  const extension = ExtensionTestUtils.loadExtension({ ...extData });
  let textContent;

  async function waitForPanelReady() {
    return Promise.all([
      extension.awaitMessage("sidebar"),
      BrowserTestUtils.waitForEvent(window, "SidebarFocused"),
    ]);
  }

  async function getSidebarPanelContents(win = window) {
    let sidebarBrowser = win.SidebarController.browser;
    is(
      sidebarBrowser.currentURI.spec,
      "chrome://browser/content/webext-panels.xhtml",
      "Sidebar wrapper loaded in sidebar browser"
    );
    let extSidebarBrowser = sidebarBrowser.contentDocument.getElementById(
      "webext-panels-browser"
    );
    ok(extSidebarBrowser, "got extSidebarBrowser");

    const contentResult = await SpecialPowers.spawn(
      extSidebarBrowser,
      [],
      async () => {
        const doc = content.document;
        if (doc.readyState != "complete") {
          await new Promise(resolve =>
            doc.addEventListener("DOMContentLoaded", resolve, { once: true })
          );
        }
        return doc.documentElement.textContent.trim();
      }
    );
    return contentResult;
  }

  const extensionPanelInitialReady = waitForPanelReady();
  await extension.startup();
  const commandID = `${ExtensionCommon.makeWidgetId(extension.id)}-sidebar-action`;

  await extensionPanelInitialReady;
  is(launcher.extensionButtons.length, 1, "Extension is shown in the sidebar.");

  ok(SidebarController.isOpen, "The sidebar panel opened on install");
  is(
    SidebarController.currentID,
    commandID,
    "The currentID is the extension's sidebarAction"
  );

  textContent = await getSidebarPanelContents();
  is(
    textContent,
    "A Test Sidebar",
    "Extension's sidebarAction loaded its document into the sidebar panel"
  );

  // check we can toggle it closed and open again
  let panelHiddenPromise = BrowserTestUtils.waitForMutationCondition(
    SidebarController._box,
    { attributes: true, attributeFilter: ["hidden"] },
    () =>
      SidebarController._box.hidden &&
      SidebarController.browser.getAttribute("src") == "about:blank"
  );
  document.getElementById("sidebar-button").click();
  await panelHiddenPromise;

  const extensionPanelReopenReady = waitForPanelReady();
  document.getElementById("sidebar-button").click();

  info("Waiting for the panel to be shown & focused");
  await extensionPanelReopenReady;

  textContent = await getSidebarPanelContents();
  is(
    textContent,
    "A Test Sidebar",
    "Extension's sidebarAction re-loaded its document into the sidebar panel"
  );

  SidebarController.hide();
  await extension.unload();
  await launcher.updateComplete;
});
