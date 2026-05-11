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

const BOOKMARK_DIALOG_URL =
  "chrome://browser/content/places/bookmarkProperties.xhtml";

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
        lastUsed: 1655391592,
        client: 1,
        fxaDeviceId: "1",
        availableCommands: {
          "https://identity.mozilla.com/cmd/close-uri/v1": "encryption_is_cool",
        },
        secondaryL10nArgs: '{"deviceName": "My Desktop"}',
      },
    ],
  },
];

let sandbox;

Services.fog.testResetFOG();

add_setup(async () => {
  sandbox = sinon.createSandbox();
  sandbox.stub(lazy.SyncedTabsErrorHandler, "getErrorType").returns(null);
  sandbox.stub(lazy.TabsSetupFlowManager, "uiStateIndex").value(4);
  sandbox.stub(lazy.SyncedTabs, "getTabClients").resolves(tabClients);
  sandbox
    .stub(lazy.SyncedTabs, "createRecentTabsList")
    .resolves(tabClients.flatMap(client => client.tabs));
  await SidebarTestUtils.waitForInitialized(window);
});

registerCleanupFunction(async () => {
  sandbox.restore();
  await PlacesUtils.bookmarks.eraseEverything();
  SidebarTestUtils.closePanel(window);
});

/**
 * Show the Synced Tabs panel in the revamped sidebar and return the component,
 * its content window, and the populated tab row elements.
 *
 * @returns {Promise<{component: Element, contentWindow: Window, rows: Element[]}>}
 */
async function showSyncedTabsSidebar() {
  if (SidebarController.currentID !== "viewTabsSidebar") {
    await SidebarController.show("viewTabsSidebar");
  }
  const { contentDocument, contentWindow } = SidebarController.browser;
  const component = contentDocument.querySelector("sidebar-syncedtabs");

  info("Waiting for the cards list to be populated");
  await BrowserTestUtils.waitForMutationCondition(
    component,
    { childList: true, subtree: true },
    () => component.cards.length
  );
  const tabList = component.cards[0].querySelector("sidebar-tab-list");
  info("Waiting for the tabs list to be populated");
  await BrowserTestUtils.waitForMutationCondition(
    tabList.shadowRoot,
    { childList: true, subtree: true },
    () => tabList.rowEls?.length
  );

  await SimpleTest.promiseFocus();
  return { component, contentWindow, rows: tabList.rowEls };
}

/**
 * Assert that the given `browser.ui.interaction.sidebar_synced_tabs` labeled
 * counter has the expected value.
 *
 * @param {string} label
 *   One of the labels declared in metrics.yaml.
 * @param {number} value
 *   The expected counter value.
 */
function assertLabeledCounterValue(label, value) {
  Assert.equal(
    Glean.browserUiInteraction.sidebarSyncedTabs[label].testGetValue(),
    value,
    `sidebar_synced_tabs["${label}"] should be ${value}`
  );
}

add_task(async function test_open_in_new_window() {
  const { rows } = await showSyncedTabsSidebar();

  const promiseWin = BrowserTestUtils.waitForNewWindow();
  await activateContextMenuItem(
    rows[0].mainEl,
    "sidebar-synced-tabs-context-open-in-window"
  );
  assertLabeledCounterValue("open_in_new_window", 1);
  await BrowserTestUtils.closeWindow(await promiseWin);
});

add_task(async function test_open_in_private_window() {
  const { rows } = await showSyncedTabsSidebar();

  const promiseWin = BrowserTestUtils.waitForNewWindow();
  await activateContextMenuItem(
    rows[0].mainEl,
    "sidebar-synced-tabs-context-open-in-private-window"
  );
  assertLabeledCounterValue("open_in_private_window", 1);
  await BrowserTestUtils.closeWindow(await promiseWin);
});

add_task(async function test_close_tab_on_connected_device() {
  const { rows } = await showSyncedTabsSidebar();
  await activateContextMenuItem(
    rows[0].mainEl,
    "sidebar-context-menu-close-remote-tab"
  );
  assertLabeledCounterValue("close_tab_on_connected_device", 1);
});

add_task(async function test_bookmark_tab_cancelled() {
  const { sidebarSyncedTabs } = Glean.browserUiInteraction;
  const { rows } = await showSyncedTabsSidebar();

  const promiseCancel = BrowserTestUtils.promiseAlertDialogOpen(
    "cancel",
    BOOKMARK_DIALOG_URL,
    { isSubDialog: true }
  );
  await activateContextMenuItem(
    rows[0].mainEl,
    "sidebar-synced-tabs-context-bookmark-tab"
  );
  await promiseCancel;
  await TestUtils.waitForCondition(
    () => sidebarSyncedTabs.bookmark_tab_cancelled.testGetValue() === 1,
    "bookmark_tab_cancelled recorded"
  );
  await SimpleTest.promiseFocus();
});

add_task(async function test_bookmark_tab_confirmed() {
  const { sidebarSyncedTabs } = Glean.browserUiInteraction;
  const { rows } = await showSyncedTabsSidebar();

  const promiseConfirm = BrowserTestUtils.promiseAlertDialogOpen(
    null,
    BOOKMARK_DIALOG_URL,
    {
      isSubDialog: true,
      callback: async win => {
        await win.document.mozSubdialogReady;
        EventUtils.synthesizeKey("VK_RETURN", {}, win);
      },
    }
  );
  await activateContextMenuItem(
    rows[0].mainEl,
    "sidebar-synced-tabs-context-bookmark-tab"
  );
  await promiseConfirm;
  await TestUtils.waitForCondition(
    () => sidebarSyncedTabs.bookmark_tab_confirmed.testGetValue() === 1,
    "bookmark_tab_confirmed recorded"
  );
  await SimpleTest.promiseFocus();
});

add_task(async function test_copy_link() {
  const { rows } = await showSyncedTabsSidebar();

  await activateContextMenuItem(
    rows[0].mainEl,
    "sidebar-synced-tabs-context-copy-link"
  );
  assertLabeledCounterValue("copy_link", 1);
});

add_task(async function test_search() {
  const { component, contentWindow } = await showSyncedTabsSidebar();

  EventUtils.synthesizeMouseAtCenter(
    component.searchTextbox,
    {},
    contentWindow
  );
  EventUtils.sendString("example", contentWindow);
  await TestUtils.waitForCondition(
    () =>
      Glean.browserUiInteraction.sidebarSyncedTabs.search.testGetValue() >= 1,
    "Search counter was incremented"
  );
});
