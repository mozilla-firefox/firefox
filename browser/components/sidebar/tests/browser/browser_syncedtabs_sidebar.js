/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  SyncedTabs: "resource://services-sync/SyncedTabs.sys.mjs",
  SyncedTabsErrorHandler:
    "resource:///modules/firefox-view-synced-tabs-error-handler.sys.mjs",
  TabsSetupFlowManager:
    "resource:///modules/firefox-view-tabs-setup-manager.sys.mjs",
});

const tabClients = [
  {
    id: 1,
    type: "client",
    name: "My desktop",
    clientType: "desktop",
    lastModified: 1655730486760,
    tabs: [
      {
        device: "My desktop",
        deviceType: "desktop",
        type: "tab",
        title: "Sandboxes - Sinon.JS",
        url: "https://sinonjs.org/releases/latest/sandbox/",
        icon: "https://sinonjs.org/assets/images/favicon.png",
        lastUsed: 1655391592, // Thu Jun 16 2022 14:59:52 GMT+0000
        client: 1,
        fxaDeviceId: "1",
        availableCommands: {
          "https://identity.mozilla.com/cmd/close-uri/v1": "encryption_is_cool",
        },
        secondaryL10nArgs: '{"deviceName": "My Desktop"}',
      },
      {
        device: "My desktop",
        deviceType: "desktop",
        type: "tab",
        title: "Internet for people, not profits - Mozilla",
        url: "https://www.mozilla.org/",
        icon: "https://www.mozilla.org/media/img/favicons/mozilla/favicon.d25d81d39065.ico",
        lastUsed: 1655730486, // Mon Jun 20 2022 13:08:06 GMT+0000
        client: 1,
        fxaDeviceId: "1",
        availableCommands: {
          "https://identity.mozilla.com/cmd/close-uri/v1": "encryption_is_cool",
        },
        secondaryL10nArgs: '{"deviceName": "My Desktop"}',
      },
    ],
  },
  {
    id: 2,
    type: "client",
    name: "My iphone",
    clientType: "phone",
    lastModified: 1655727832930,
    tabs: [
      {
        device: "My iphone",
        deviceType: "mobile",
        type: "tab",
        title: "The Guardian",
        url: "https://www.theguardian.com/",
        icon: "page-icon:https://www.theguardian.com/",
        lastUsed: 1655291890, // Wed Jun 15 2022 11:18:10 GMT+0000
        client: 2,
        fxaDeviceId: "2",
        availableCommands: {},
        secondaryL10nArgs: '{"deviceName": "My iphone"}',
      },
      {
        device: "My iphone",
        deviceType: "mobile",
        type: "tab",
        title: "The Times",
        url: "https://www.thetimes.co.uk/",
        icon: "page-icon:https://www.thetimes.co.uk/",
        lastUsed: 1655727485, // Mon Jun 20 2022 12:18:05 GMT+0000
        client: 2,
        fxaDeviceId: "2",
        availableCommands: {},
        secondaryL10nArgs: '{"deviceName": "My iphone"}',
      },
    ],
  },
];

add_task(async function test_tabs() {
  const sandbox = sinon.createSandbox();
  sandbox.stub(lazy.SyncedTabsErrorHandler, "getErrorType").returns(null);
  sandbox.stub(lazy.TabsSetupFlowManager, "uiStateIndex").value(4);
  sandbox.stub(lazy.SyncedTabs, "getTabClients").resolves(tabClients);
  sandbox
    .stub(lazy.SyncedTabs, "createRecentTabsList")
    .resolves(tabClients.flatMap(client => client.tabs));

  await SidebarController.show("viewTabsSidebar");
  const { contentDocument } = SidebarController.browser;
  const component = contentDocument.querySelector("sidebar-syncedtabs");
  Assert.ok(component, "Synced tabs panel is shown.");
  const contextMenu = SidebarController.currentContextMenu;

  for (const [i, client] of tabClients.entries()) {
    const card = component.cards[i];
    Assert.equal(card.heading, client.name, "Device name is correct.");
    const rows = await TestUtils.waitForCondition(() => {
      const { rowEls } = card.querySelector("sidebar-tab-list");
      return rowEls.length === client.tabs.length && rowEls;
    }, "Device has the correct number of tabs.");
    for (const [j, row] of rows.entries()) {
      const tabData = client.tabs[j];
      Assert.equal(row.title, tabData.title, `Tab ${j + 1} has correct title.`);
      Assert.equal(row.url, tabData.url, `Tab ${j + 1} has correct URL.`);

      // We need to wait for the document to flush to ensure it's completely opened
      let content = SidebarController.browser.contentWindow;
      await content.promiseDocumentFlushed(() => {});
      await EventUtils.synthesizeMouseAtCenter(
        row.mainEl,
        { type: "mouseover" },
        content
      );

      // We set the second client to not have CloseTab as an available command
      // to ensure we properly test that path
      if (client.id === 2) {
        Assert.ok(
          !row.secondaryButtonEl,
          `Dismiss button should NOT appear for tab ${
            j + 1
          } on the client that does not have available commands.`
        );
      } else {
        // We need to use renderRoot since Lit components querySelector
        // won't return the right things
        await BrowserTestUtils.waitForMutationCondition(
          row.shadowRoot,
          { childList: true },
          () => row.secondaryButtonEl,
          `Dismiss button should appear for tab ${j + 1}`
        );
        // Check the presence of the dismiss button
        const dismissButton = row.secondaryButtonEl;
        Assert.ok(dismissButton, `Dismiss button is present on tab ${j + 1}.`);
        // Simulate clicking the dismiss button
        EventUtils.synthesizeMouseAtCenter(dismissButton, {}, content);

        await BrowserTestUtils.waitForMutationCondition(
          row.secondaryButtonEl,
          { attributes: true },
          () => {
            const undoButton = row.secondaryButtonEl;
            return (
              undoButton.classList.contains("undo-button") &&
              undoButton.style.display !== "none"
            );
          },
          `Undo button is shown after dismissing tab ${j + 1}.`
        );

        // Simulate clicking the undo button
        const undoButton = row.secondaryButtonEl;
        EventUtils.synthesizeMouseAtCenter(
          row.mainEl,
          { type: "mouseover" },
          content
        );
        EventUtils.synthesizeMouseAtCenter(undoButton, {}, content);
        await BrowserTestUtils.waitForMutationCondition(
          row.secondaryButtonEl,
          { attributes: true },
          () => {
            return (
              row.secondaryButtonEl.classList.contains("dismiss-button") &&
              !row.secondaryButtonEl.classList.contains("undo-button")
            );
          },
          `Dismiss button is restored after undoing tab ${j + 1}.`
        );
      }
    }
  }

  info("Copy the first link.");
  const tabList = component.cards[0].querySelector("sidebar-tab-list");
  const menuItem = document.getElementById(
    "sidebar-synced-tabs-context-copy-link"
  );
  await openAndWaitForContextMenu(contextMenu, tabList.rowEls[0].mainEl, () =>
    contextMenu.activateItem(menuItem)
  );
  await TestUtils.waitForCondition(() => {
    const copiedUrl = SpecialPowers.getClipboardData("text/plain");
    return copiedUrl == tabClients[0].tabs[0].url;
  }, "The copied URL is correct.");

  SidebarController.hide();
  sandbox.restore();
});

add_task(async function test_syncedtabs_searchbox_focus() {
  await SidebarController.show("viewTabsSidebar");
  const { contentDocument } = SidebarController.browser;
  const component = contentDocument.querySelector("sidebar-syncedtabs");
  const { searchTextbox } = component;

  ok(component.shadowRoot.activeElement, "check activeElement is present");
  Assert.equal(
    component.shadowRoot.activeElement,
    searchTextbox,
    "Check search box is focused"
  );
  SidebarController.hide();
});

add_task(async function test_close_remote_tab_context_menu() {
  const sandbox = sinon.createSandbox();
  sandbox.stub(lazy.SyncedTabsErrorHandler, "getErrorType").returns(null);
  sandbox.stub(lazy.TabsSetupFlowManager, "uiStateIndex").value(4);
  sandbox.stub(lazy.SyncedTabs, "getTabClients").resolves(tabClients);
  sandbox
    .stub(lazy.SyncedTabs, "createRecentTabsList")
    .resolves(tabClients.flatMap(client => client.tabs));

  await SidebarController.show("viewTabsSidebar");
  const { contentDocument } = SidebarController.browser;
  const component = contentDocument.querySelector("sidebar-syncedtabs");
  Assert.ok(component, "Synced tabs panel is shown.");
  const contextMenu = SidebarController.currentContextMenu;

  // Verify that the context menu is available
  info("Check if the context menu is present in the DOM.");
  Assert.ok(contextMenu, "Context menu is present.");

  // Verify "Close Remote Tab" context menu item
  info("Verify 'Close Remote Tab' context menu item.");
  const rows = await TestUtils.waitForCondition(() => {
    const { rowEls } = component.cards[0].querySelector("sidebar-tab-list");
    return rowEls.length && rowEls;
  }, "Device has the correct number of tabs.");
  await openAndWaitForContextMenu(contextMenu, rows[0], () => {
    const closeTabMenuItem = contextMenu.querySelector(
      "#sidebar-context-menu-close-remote-tab"
    );
    Assert.ok(closeTabMenuItem, "'Close Remote Tab' menu item is present.");
    Assert.ok(
      !closeTabMenuItem.disabled,
      "'Close Remote Tab' menu item is enabled."
    );
  });
  contextMenu.hidePopup();

  SidebarController.hide();
  sandbox.restore();
});

add_task(async function test_connect_additional_devices() {
  const sandbox = sinon.createSandbox();
  sandbox.stub(lazy.SyncedTabsErrorHandler, "getErrorType").returns(null);
  sandbox.stub(lazy.TabsSetupFlowManager, "uiStateIndex").value(2);
  sandbox.stub(lazy.SyncedTabs, "getTabClients").resolves([
    {
      id: 1,
      name: "This Device",
      isCurrentDevice: true,
      type: "desktop",
      tabs: [],
    },
  ]);

  await SidebarController.show("viewTabsSidebar");
  const { contentDocument } = SidebarController.browser;
  const component = contentDocument.querySelector("sidebar-syncedtabs");
  Assert.ok(component, "Synced tabs panel is shown.");
  let emptyState = component.shadowRoot.querySelector("fxview-empty-state");
  ok(
    emptyState.getAttribute("headerlabel").includes("syncedtabs-adddevice"),
    "Add device message is shown"
  );

  const mockConnectAdditionDevicesPath = "https://example.com/";
  let expectedUrl =
    "https://support.mozilla.org/kb/how-do-i-set-sync-my-computer#w_connect-additional-devices-to-sync";
  let connectAdditionalDevicesLink = emptyState?.shadowRoot.querySelector("a");
  connectAdditionalDevicesLink.scrollIntoView();
  await TestUtils.waitForCondition(
    () => BrowserTestUtils.isVisible(connectAdditionalDevicesLink),
    "Support url is visible"
  );
  is(
    connectAdditionalDevicesLink.href,
    expectedUrl,
    "Support link href is correct"
  );
  connectAdditionalDevicesLink.href = mockConnectAdditionDevicesPath;
  info("Mock click on support link");
  let tabPromise = BrowserTestUtils.waitForLocationChange(
    gBrowser,
    mockConnectAdditionDevicesPath
  );
  connectAdditionalDevicesLink.click();
  await tabPromise;
  is(
    gBrowser.currentURI.spec,
    mockConnectAdditionDevicesPath,
    "Navigated to mock support link"
  );

  SidebarController.hide();

  cleanUpExtraTabs();

  sandbox.restore();
});
