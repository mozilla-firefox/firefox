/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);

const BOOKMARK_DIALOG_URL =
  "chrome://browser/content/places/bookmarkProperties.xhtml";
const CLEAR_DATA_FOR_SITE_URL =
  "chrome://browser/content/places/clearDataForSite.xhtml";
const SANITIZE_DIALOG_URL = "chrome://browser/content/sanitize_v2.xhtml";
const SORT_OPTION_PREF = "sidebar.history.sortOption";

Services.fog.testResetFOG();

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.userContext.enabled", true]],
  });
  await SidebarTestUtils.waitForInitialized(window);
});

registerCleanupFunction(async () => {
  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();
  Services.prefs.clearUserPref(SORT_OPTION_PREF);
  SidebarTestUtils.closePanel(window);
});

/**
 * Assert that the given `browser.ui.interaction.sidebar_history` labeled
 * counter has the expected value.
 *
 * @param {string} label
 *   One of the labels declared in metrics.yaml.
 * @param {number} value
 *   The expected counter value.
 */
function assertLabeledCounterValue(label, value) {
  Assert.equal(
    Glean.browserUiInteraction.sidebarHistory[label].testGetValue(),
    value,
    `sidebar_history["${label}"] should be ${value}`
  );
}

/**
 * Populate history and show the History sidebar, returning the component,
 * its content window, and the first populated row.
 *
 * @returns {Promise<{component: Element, contentWindow: Window, rows: Element[]}>}
 */
async function setupHistorySidebar() {
  await populateHistory();
  const { component, contentWindow } = await showHistorySidebar();
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => !!component.lists.length
  );
  const list = component.lists[0];
  await BrowserTestUtils.waitForMutationCondition(
    list.shadowRoot,
    { subtree: true, childList: true },
    () => !!list.rowEls.length
  );
  await SimpleTest.promiseFocus();
  return { component, contentWindow, rows: list.rowEls };
}

/**
 * Open the History panel's options menu (sort / clear), activate a menu
 * item, and resolve once the menu has closed.
 *
 * @param {Element} component
 *   The `<sidebar-history>` element.
 * @param {Window} contentWindow
 *   The sidebar's content window, used to dispatch the click event.
 * @param {string} menuItemId
 *   The id of the menuitem to activate.
 */
async function activateOptionsMenuItem(component, contentWindow, menuItemId) {
  const menu = document.getElementById("sidebar-history-menu");
  const promiseShown = BrowserTestUtils.waitForPopupEvent(menu, "shown");
  EventUtils.synthesizeMouseAtCenter(component.menuButton, {}, contentWindow);
  await promiseShown;
  const promiseHidden = BrowserTestUtils.waitForPopupEvent(menu, "hidden");
  menu.activateItem(document.getElementById(menuItemId));
  await promiseHidden;
}

add_task(async function test_open_in_new_tab() {
  const { rows } = await setupHistorySidebar();

  const promiseNewTab = BrowserTestUtils.waitForNewTab(gBrowser);
  await activateContextMenuItem(
    rows[0].mainEl,
    "sidebar-history-context-open-in-tab"
  );
  assertLabeledCounterValue("open_in_new_tab", 1);
  BrowserTestUtils.removeTab(await promiseNewTab);
});

add_task(async function test_open_in_new_container_tab() {
  const { rows } = await setupHistorySidebar();
  const containerMenu = document.getElementById(
    "sidebar-history-context-menu-container-tab"
  );
  const containerPopup = document.getElementById(
    "sidebar-history-context-menu-container-popup"
  );

  const promiseNewTab = BrowserTestUtils.waitForNewTab(gBrowser);
  await activateContextMenuItem(rows[0].mainEl, null, async contextMenu => {
    const subpopupShown = BrowserTestUtils.waitForPopupEvent(
      containerPopup,
      "shown"
    );
    containerMenu.openMenu(true);
    await subpopupShown;
    contextMenu.activateItem(containerPopup.firstElementChild);
  });
  assertLabeledCounterValue("open_in_new_container_tab", 1);
  BrowserTestUtils.removeTab(await promiseNewTab);
});

add_task(async function test_open_in_new_window() {
  const { rows } = await setupHistorySidebar();

  const promiseWin = BrowserTestUtils.waitForNewWindow();
  await activateContextMenuItem(
    rows[0].mainEl,
    "sidebar-history-context-open-in-window"
  );
  assertLabeledCounterValue("open_in_new_window", 1);
  await BrowserTestUtils.closeWindow(await promiseWin);
});

add_task(async function test_open_in_private_window() {
  const { rows } = await setupHistorySidebar();

  const promiseWin = BrowserTestUtils.waitForNewWindow();
  await activateContextMenuItem(
    rows[0].mainEl,
    "sidebar-history-context-open-in-private-window"
  );
  assertLabeledCounterValue("open_in_private_window", 1);
  await BrowserTestUtils.closeWindow(await promiseWin);
});

add_task(async function test_delete_from_history() {
  const { rows } = await setupHistorySidebar();

  const promiseRemoved = PlacesTestUtils.waitForNotification("page-removed");
  await activateContextMenuItem(
    rows[0].mainEl,
    "sidebar-history-context-delete-page"
  );
  assertLabeledCounterValue("delete_from_history", 1);
  await promiseRemoved;
});

add_task(async function test_clear_all_website_data_cancelled() {
  const { sidebarHistory } = Glean.browserUiInteraction;
  const { rows } = await setupHistorySidebar();

  const promiseCancel = BrowserTestUtils.promiseAlertDialogOpen(
    "cancel",
    CLEAR_DATA_FOR_SITE_URL,
    { isSubDialog: true }
  );
  await activateContextMenuItem(
    rows[0].mainEl,
    "sidebar-history-context-forget-site"
  );
  await promiseCancel;
  await TestUtils.waitForCondition(
    () => sidebarHistory.clear_all_website_data_cancelled.testGetValue() === 1,
    "clear_all_website_data_cancelled recorded"
  );
  await SimpleTest.promiseFocus();
});

add_task(async function test_clear_all_website_data_confirmed() {
  const { sidebarHistory } = Glean.browserUiInteraction;
  const { rows } = await setupHistorySidebar();

  const dialogClosed = BrowserTestUtils.promiseAlertDialogOpen(
    "accept",
    CLEAR_DATA_FOR_SITE_URL,
    { isSubDialog: true }
  );
  await activateContextMenuItem(
    rows[0].mainEl,
    "sidebar-history-context-forget-site"
  );
  await dialogClosed;
  await TestUtils.waitForCondition(
    () => sidebarHistory.clear_all_website_data_confirmed.testGetValue() === 1,
    "clear_all_website_data_confirmed recorded"
  );
  await SimpleTest.promiseFocus();
});

add_task(async function test_bookmark_tab_cancelled() {
  const { sidebarHistory } = Glean.browserUiInteraction;
  const { rows } = await setupHistorySidebar();

  const promiseCancel = BrowserTestUtils.promiseAlertDialogOpen(
    "cancel",
    BOOKMARK_DIALOG_URL,
    { isSubDialog: true }
  );
  await activateContextMenuItem(
    rows[0].mainEl,
    "sidebar-history-context-bookmark-page"
  );
  await promiseCancel;
  await TestUtils.waitForCondition(
    () => sidebarHistory.bookmark_tab_cancelled.testGetValue() === 1,
    "bookmark_tab_cancelled recorded"
  );
  await SimpleTest.promiseFocus();
});

add_task(async function test_bookmark_tab_confirmed() {
  const { sidebarHistory } = Glean.browserUiInteraction;
  const { rows } = await setupHistorySidebar();

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
    "sidebar-history-context-bookmark-page"
  );
  await promiseConfirm;
  await TestUtils.waitForCondition(
    () => sidebarHistory.bookmark_tab_confirmed.testGetValue() === 1,
    "bookmark_tab_confirmed recorded"
  );
  await SimpleTest.promiseFocus();
});

add_task(async function test_copy_link() {
  const { rows } = await setupHistorySidebar();

  await activateContextMenuItem(
    rows[0].mainEl,
    "sidebar-history-context-copy-link"
  );
  assertLabeledCounterValue("copy_link", 1);
});

add_task(async function test_sort_history_event() {
  Services.prefs.clearUserPref(SORT_OPTION_PREF);
  const { component, contentWindow } = await setupHistorySidebar();

  const sortEvents = Glean.browserUiInteraction.sidebarSortHistory;
  await activateOptionsMenuItem(
    component,
    contentWindow,
    "sidebar-history-sort-by-site"
  );
  Assert.equal(
    sortEvents.testGetValue()?.[0].extra.sort_type,
    "site",
    "Sort by site recorded."
  );

  await activateOptionsMenuItem(
    component,
    contentWindow,
    "sidebar-history-sort-by-date"
  );
  Assert.equal(
    sortEvents.testGetValue()?.[1].extra.sort_type,
    "date",
    "Sort by date recorded."
  );
});

add_task(async function test_clear_history_cancelled() {
  const { sidebarHistory } = Glean.browserUiInteraction;
  const { component, contentWindow } = await setupHistorySidebar();

  const promiseCancel = BrowserTestUtils.promiseAlertDialogOpen(
    null,
    SANITIZE_DIALOG_URL,
    {
      isSubDialog: true,
      callback: async ({ document }) => {
        await document.mozSubdialogReady;
        document.querySelector("dialog").getButton("cancel").click();
      },
    }
  );
  await activateOptionsMenuItem(
    component,
    contentWindow,
    "sidebar-history-clear"
  );
  await promiseCancel;
  await TestUtils.waitForCondition(
    () => sidebarHistory.clear_history_cancelled.testGetValue() === 1,
    "clear_history_cancelled recorded"
  );
  await SimpleTest.promiseFocus();
});

add_task(async function test_clear_history_confirmed() {
  const { sidebarHistory } = Glean.browserUiInteraction;
  const { component, contentWindow } = await setupHistorySidebar();

  // Stub sanitizer functionality.
  const sandbox = sinon.createSandbox();
  sandbox.stub(Sanitizer, "sanitize").resolves();
  registerCleanupFunction(() => {
    sandbox.restore();
  });

  const promiseAccept = BrowserTestUtils.promiseAlertDialogOpen(
    null,
    SANITIZE_DIALOG_URL,
    {
      isSubDialog: true,
      callback: async ({ document }) => {
        await document.mozSubdialogReady;
        document.querySelector("dialog").getButton("accept").click();
      },
    }
  );
  await activateOptionsMenuItem(
    component,
    contentWindow,
    "sidebar-history-clear"
  );
  await promiseAccept;
  await TestUtils.waitForCondition(
    () => sidebarHistory.clear_history_confirmed.testGetValue() === 1,
    "clear_history_confirmed recorded"
  );
  await SimpleTest.promiseFocus();
});

add_task(async function test_search() {
  const { component, contentWindow } = await setupHistorySidebar();

  EventUtils.synthesizeMouseAtCenter(
    component.searchTextbox,
    {},
    contentWindow
  );
  EventUtils.sendString("example", contentWindow);
  await TestUtils.waitForCondition(
    () => Glean.browserUiInteraction.sidebarHistory.search.testGetValue() >= 1,
    "Search counter was incremented"
  );
});
