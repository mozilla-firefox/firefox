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
        title: "example.com",
        url: "https://example.com/",
        icon: "https://example.com/assets/images/favicon.png",
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
        title: "Examples for people, not profits - Mozilla",
        url: "https://example.org/",
        icon: "https://example.org/media/img/favicons/mozilla/favicon.d25d81d39065.ico",
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
        title: "The Example",
        url: "https://example.net/",
        icon: "page-icon:https://example.net/",
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
        title: "The Other Example",
        url: "https://example.com/thetimes/",
        icon: "page-icon:https://example.com/",
        lastUsed: 1655727485, // Mon Jun 20 2022 12:18:05 GMT+0000
        client: 2,
        fxaDeviceId: "2",
        availableCommands: {},
        secondaryL10nArgs: '{"deviceName": "My iphone"}',
      },
    ],
  },
];

function verifyContexMenuItemsByL10nIds(menu, expectedItems, message) {
  // verify context menu items by comparing l10n-ids and placement of separators
  const actualItems = Array.from(
    menu.querySelectorAll(":scope > *:not([hidden]")
  ).map(item =>
    item.localName == "menuseparator" ? "---" : item.dataset.l10nId
  );

  Assert.deepEqual(actualItems, expectedItems, message);
}

async function waitForSyncedTabListInCard(
  component,
  cardIndex = 0,
  minRows = 1
) {
  info("Waiting for the cards list to be populated");
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => {
      return component.cards.length > cardIndex;
    }
  );
  const card = component.cards[cardIndex];
  const tabList = card.querySelector("sidebar-tab-list");
  info("Waiting for the tabs list to be populated");
  await BrowserTestUtils.waitForMutationCondition(
    tabList.shadowRoot,
    { childList: true, subtree: true },
    () => {
      info(`Got rowEls: ${tabList.rowEls?.length}`);
      return tabList.rowEls?.length >= minRows;
    }
  );
  return tabList.rowEls;
}

add_task(async function test_tabs() {
  const sandbox = sinon.createSandbox();
  sandbox.stub(lazy.SyncedTabsErrorHandler, "getErrorType").returns(null);
  sandbox.stub(lazy.TabsSetupFlowManager, "uiStateIndex").value(4);
  sandbox.stub(lazy.SyncedTabs, "getTabClients").resolves(tabClients);
  sandbox
    .stub(lazy.SyncedTabs, "createRecentTabsList")
    .resolves(tabClients.flatMap(client => client.tabs));

  await SidebarController.show("viewTabsSidebar");
  const { contentDocument, contentWindow } = SidebarController.browser;
  const content = SidebarController.browser.contentWindow;
  const contextMenu = SidebarController.currentContextMenu;
  const component = contentDocument.querySelector("sidebar-syncedtabs");
  Assert.ok(component, "Synced tabs panel is shown.");

  // wait for the synced tabs cards to get fully populated
  await waitForSyncedTabListInCard(component);

  for (const [i, client] of tabClients.entries()) {
    const card = component.cards[i];
    Assert.equal(card.heading, client.name, "Device name is correct.");

    const rows = card.querySelector("sidebar-tab-list").rowEls;
    Assert.equal(
      rows.length,
      client.tabs.length,
      "Device has the correct number of tabs."
    );

    for (const [j, row] of rows.entries()) {
      const tabData = client.tabs[j];
      Assert.equal(row.title, tabData.title, `Tab ${j + 1} has correct title.`);
      Assert.equal(row.url, tabData.url, `Tab ${j + 1} has correct URL.`);

      // We need to wait for the document to flush to ensure it's completely opened
      await content.promiseDocumentFlushed(() => {});
      EventUtils.synthesizeMouseAtCenter(
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

  info("Use keyboard shortcuts to navigate downwards.");
  const firstCardHeader = component.cards[0].summaryEl;
  const firstCardRows = component.lists[0].rowEls;
  const secondCardHeader = component.cards[1].summaryEl;
  const secondCardRows = component.lists[1].rowEls;

  firstCardHeader.focus();
  await focusWithKeyboard(firstCardRows[0], "KEY_ArrowDown", contentWindow);
  await focusWithKeyboard(firstCardRows[1], "KEY_ArrowDown", contentWindow);
  await focusWithKeyboard(secondCardHeader, "KEY_ArrowDown", contentWindow);
  await focusWithKeyboard(secondCardRows[0], "KEY_ArrowDown", contentWindow);
  await focusWithKeyboard(secondCardRows[1], "KEY_ArrowDown", contentWindow);

  info("Use keyboard shortcuts to navigate upwards.");
  await focusWithKeyboard(secondCardRows[0], "KEY_ArrowUp", contentWindow);
  await focusWithKeyboard(secondCardHeader, "KEY_ArrowUp", contentWindow);
  await focusWithKeyboard(firstCardRows[1], "KEY_ArrowUp", contentWindow);
  await focusWithKeyboard(firstCardRows[0], "KEY_ArrowUp", contentWindow);
  await focusWithKeyboard(firstCardHeader, "KEY_ArrowUp", contentWindow);

  SidebarController.hide({ dismissPanel: true });
  sandbox.restore();
});

add_task(async function test_syncedtabs_searchbox_focus_and_context_menu() {
  await SidebarController.show("viewTabsSidebar");
  const { contentDocument, contentWindow } = SidebarController.browser;
  const component = contentDocument.querySelector("sidebar-syncedtabs");
  const { searchTextbox } = component;

  ok(component.shadowRoot.activeElement, "check activeElement is present");
  Assert.equal(
    component.shadowRoot.activeElement,
    searchTextbox,
    "Check search box is focused"
  );

  const promisePopupShown = BrowserTestUtils.waitForEvent(
    contentWindow,
    "popupshown"
  );
  EventUtils.synthesizeMouseAtCenter(
    searchTextbox,
    { type: "contextmenu", button: 2 },
    contentWindow
  );
  const { target: menu } = await promisePopupShown;
  Assert.equal(
    menu.id,
    "textbox-contextmenu",
    "The correct context menu is shown."
  );
  menu.hidePopup();

  SidebarController.hide({ dismissPanel: true });
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

  SidebarController.hide({ dismissPanel: true });
  sandbox.restore();
});

add_task(async function test_device_header_context_menu() {
  const sandbox = sinon.createSandbox();
  sandbox.stub(lazy.SyncedTabsErrorHandler, "getErrorType").returns(null);
  sandbox.stub(lazy.TabsSetupFlowManager, "uiStateIndex").value(4);
  sandbox.stub(lazy.SyncedTabs, "getTabClients").resolves(tabClients);
  sandbox
    .stub(lazy.SyncedTabs, "createRecentTabsList")
    .resolves(tabClients.flatMap(client => client.tabs));

  await SidebarController.show("viewTabsSidebar");
  const contextMenu = SidebarController.currentContextMenu;

  const { contentDocument } = SidebarController.browser;
  const component = contentDocument.querySelector("sidebar-syncedtabs");
  Assert.ok(
    BrowserTestUtils.isVisible(component),
    "Synced tabs panel is shown."
  );

  const rows = await waitForSyncedTabListInCard(component);

  info("Right-click a tab row: tab items visible, device items hidden.");
  await openAndWaitForContextMenu(contextMenu, rows[0], () => {
    verifyContexMenuItemsByL10nIds(
      contextMenu,
      [
        "sidebar-context-menu-open-in-tab",
        "sidebar-context-menu-open-in-container-tab",
        "sidebar-context-menu-open-in-window",
        "sidebar-context-menu-open-in-private-window",
        "---",
        "sidebar-context-menu-close-remote-tab",
        "---",
        "sidebar-context-menu-bookmark-tab",
        "sidebar-context-menu-copy-link",
      ],
      "Got expected context menu items for a text row"
    );
  });
  contextMenu.hidePopup();

  info("Right-click a device header: device items visible, tab items hidden.");
  const deviceHeader = component.cards[0].summaryEl;
  await openAndWaitForContextMenu(contextMenu, deviceHeader, () => {
    verifyContexMenuItemsByL10nIds(
      contextMenu,
      [
        "sidebar-context-menu-open-in-tab",
        "sidebar-context-menu-open-in-container-tab",
        "---",
        "synced-tabs-context-open-all-in-tabs",
        "synced-tabs-context-connect-another-device",
        "synced-tabs-context-manage-this-device",
      ],
      "Got expected context menu items for a device summary element"
    );
  });
  contextMenu.hidePopup();

  info("Open All in Tabs from device header context menu.");
  const openAllItem = contextMenu.querySelector(
    "#sidebar-synced-tabs-context-open-all-in-tabs"
  );
  let expectedUrls = tabClients[0].tabs.map(t => t.url);
  let tabOpenPromises = expectedUrls.map(url =>
    BrowserTestUtils.waitForNewTab(gBrowser, url, false, true)
  );
  info(`Opening tabs with expected urls: ${expectedUrls.join("\n")}`);
  await openAndWaitForContextMenu(contextMenu, deviceHeader, () =>
    contextMenu.activateItem(openAllItem)
  );
  info("Waiting for all the tabs to open");
  let openedTabs = await Promise.all(tabOpenPromises);
  info("tabOpenPromises resolved");
  let actualUrls = openedTabs.map(tab => tab.linkedBrowser.currentURI.spec);
  Assert.deepEqual(
    actualUrls,
    expectedUrls,
    "Tabs were opened with the correct URLs"
  );

  for (let tab of openedTabs) {
    BrowserTestUtils.removeTab(tab);
  }
  SidebarController.hide({ dismissPanel: true });
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
  Assert.ok(
    BrowserTestUtils.isVisible(component),
    "Synced tabs panel is shown."
  );

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

  SidebarController.hide({ dismissPanel: true });
  cleanUpExtraTabs();
  sandbox.restore();
});

add_task(async function test_tabs_click_auxclick() {
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
  Assert.ok(
    BrowserTestUtils.isVisible(component),
    "Synced tabs panel is shown."
  );

  const client = tabClients[0];
  const rows = await waitForSyncedTabListInCard(component);
  Assert.equal(
    rows.length,
    client.tabs.length,
    "Device has the correct number of tabs."
  );

  const row = rows[1];
  const content = SidebarController.browser.contentWindow;
  await content.promiseDocumentFlushed(() => {});

  {
    const selectedTabAtStart = gBrowser.selectedTab;
    const tabsLengthAtStart = gBrowser.tabs.length;
    const browser = gBrowser.selectedBrowser;
    const loaded = BrowserTestUtils.browserLoaded(browser, false, row.url);

    // See the comment in test_history_hover_buttons in
    // browser_history_sidebar.js
    AccessibilityUtils.setEnv({ focusableRule: false });
    EventUtils.synthesizeMouseAtCenter(
      row.mainEl,
      {
        button: 0,
      },
      content
    );
    AccessibilityUtils.resetEnv();
    await loaded;

    is(
      gBrowser.selectedTab,
      selectedTabAtStart,
      "The link is loaded in the current tab"
    );
    is(
      gBrowser.tabs.length,
      tabsLengthAtStart,
      "No new tab is opened on left-click"
    );
  }

  {
    const tabPromise = BrowserTestUtils.waitForNewTab(
      gBrowser,
      row.url,
      false,
      true
    );

    AccessibilityUtils.setEnv({ focusableRule: false });
    EventUtils.synthesizeMouseAtCenter(
      row.mainEl,
      {
        button: 1,
        shiftKey: false,
      },
      content
    );
    AccessibilityUtils.resetEnv();

    const tab = await tabPromise;

    is(gBrowser.selectedTab, tab, "The opened tab should be selected");

    BrowserTestUtils.removeTab(tab);
  }

  {
    const selectedTabAtStart = gBrowser.selectedTab;
    const tabPromise = BrowserTestUtils.waitForNewTab(
      gBrowser,
      row.url,
      false,
      true
    );

    AccessibilityUtils.setEnv({ focusableRule: false });
    EventUtils.synthesizeMouseAtCenter(
      row.mainEl,
      {
        button: 1,
        shiftKey: true,
      },
      content
    );
    AccessibilityUtils.resetEnv();

    const tab = await tabPromise;

    is(
      gBrowser.selectedTab,
      selectedTabAtStart,
      "The opened tab should not be selected"
    );

    Assert.notEqual(
      gBrowser.selectedTab,
      tab,
      "The opened tab should not be selected"
    );

    BrowserTestUtils.removeTab(tab);
  }

  SidebarController.hide({ dismissPanel: true });
  sandbox.restore();
});

add_task(async function test_open_in_new_tab_context_menu() {
  // Test synced tabs context menu functionality for "Open in new Tab"
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

  // Verify "Open in new Tab" context menu item
  info("Verify 'Open in new Tab' context menu item.");
  const [, secondTab] = await waitForSyncedTabListInCard(component, 0, 2);

  // Wait for new tab to open when activating menu item
  // Use the URL from the second tab in our test data
  const promiseTabOpened = BrowserTestUtils.waitForNewTab(
    gBrowser,
    "https://example.org/",
    true
  );

  await openAndWaitForContextMenu(contextMenu, secondTab.mainEl, () => {
    const openInNewTabMenuItem = contextMenu.querySelector(
      "#sidebar-synced-tabs-context-open-in-tab"
    );
    Assert.ok(openInNewTabMenuItem, "'Open in new Tab' menu item is present.");
    Assert.ok(
      !openInNewTabMenuItem.disabled,
      "'Open in new Tab' menu item is enabled."
    );

    // click the menu item to test functionality
    contextMenu.activateItem(openInNewTabMenuItem);
  });

  // Wait for the new tab to be created
  const newTab = await promiseTabOpened;

  // Verify the newly opened tab has the correct URL
  is(
    newTab.linkedBrowser.currentURI.spec,
    "https://example.org/",
    "The opened tab should have the correct URL"
  );

  // Clean up: close the newly opened tab
  BrowserTestUtils.removeTab(newTab);

  SidebarController.hide();
  sandbox.restore();
});

add_task(async function test_open_in_new_container_tab_context_menu() {
  // Test synced tabs context menu functionality for "Open in new container Tab"
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

  // Verify "Open in new container Tab" context menu item
  info("Verify 'Open in new container Tab' context menu item.");
  const [, secondTab] = await waitForSyncedTabListInCard(component, 0, 2);

  // Wait for new tab to open when activating menu item
  const promiseTabOpened = BrowserTestUtils.waitForNewTab(
    gBrowser,
    "https://example.org/",
    true
  );

  await openAndWaitForContextMenu(contextMenu, secondTab.mainEl, () => {
    const containerTabMenuItem = contextMenu.querySelector(
      "#sidebar-synced-tabs-context-menu-container-tab"
    );
    Assert.ok(
      containerTabMenuItem,
      "'Open in new container Tab' menu item is present."
    );
    Assert.ok(
      !containerTabMenuItem.disabled,
      "'Open in new container Tab' menu item is enabled."
    );

    // Open the container submenu
    info("Open container submenu");
    const menuPopup = containerTabMenuItem.menupopup;
    const menuPopupPromise = BrowserTestUtils.waitForEvent(
      menuPopup,
      "popupshown"
    );
    containerTabMenuItem.openMenu(true);
    menuPopupPromise.then(() => {
      info("Container submenu opened, inspecting available containers");

      Assert.ok(menuPopup, "Container submenu is present");

      Assert.greater(
        menuPopup.childNodes.length,
        0,
        "Should have one or more container options"
      );
      // Check the first container is usable
      const firstContainer = menuPopup.childNodes[0];
      ok(firstContainer, "First container option should exist");
      ok(!firstContainer.disabled, "First container should be enabled");

      // Click the first container option to open the tab
      contextMenu.activateItem(firstContainer);
    });
  });

  // Wait for the new tab to be created
  const newTab = await promiseTabOpened;

  // Verify the newly opened tab has the correct URL
  is(
    newTab.linkedBrowser.currentURI.spec,
    "https://example.org/",
    "The opened container tab should have the correct URL"
  );

  // Verify if it is a container tab
  Assert.greater(
    newTab.userContextId,
    0,
    "Container tab should have a userContextId greater than 0"
  );

  // Clean up - close the newly opened tab
  BrowserTestUtils.removeTab(newTab);

  SidebarController.hide();
  sandbox.restore();
});
