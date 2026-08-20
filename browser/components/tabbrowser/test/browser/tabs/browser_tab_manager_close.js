/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

const URL1 = "data:text/plain,tab1";
const URL2 = "data:text/plain,tab2";
const URL3 = "data:text/plain,tab3";
const URL4 = "data:text/plain,tab4";
const URL5 = "data:text/plain,tab5";

/**
 * @see TabsList.sys.mjs#getTabFromRow
 * @param {XulToolbarItem} row
 * @returns {MozTabbrowserTab}
 */
function tabOf(row) {
  return row._tab;
}

/**
 * Tests that middle-clicking on a tab in the Tab Manager will close it.
 */
add_task(async function test_tab_manager_close_middle_click() {
  let win = await BrowserTestUtils.openNewBrowserWindow();
  win.gTabsPanel.init();
  await addTabTo(win.gBrowser, URL1);
  await addTabTo(win.gBrowser, URL2);
  await addTabTo(win.gBrowser, URL3);
  await addTabTo(win.gBrowser, URL4);
  await addTabTo(win.gBrowser, URL5);

  let button = win.document.getElementById("alltabs-button");
  let allTabsView = win.document.getElementById("allTabsMenu-allTabsView");
  let allTabsPopupShownPromise = BrowserTestUtils.waitForEvent(
    allTabsView,
    "ViewShown"
  );
  button.click();
  await allTabsPopupShownPromise;

  let list = win.document.getElementById("allTabsMenu-allTabsView-tabs");
  while (win.gBrowser.tabs.length > 1) {
    let row = [...list.querySelectorAll(".all-tabs-item")].at(-1);
    let tabClosing = BrowserTestUtils.waitForTabClosing(tabOf(row));
    EventUtils.synthesizeMouseAtCenter(row, { button: 1 }, win);
    await tabClosing;
    Assert.ok(true, "Closed a tab with middle-click.");
  }
  await BrowserTestUtils.closeWindow(win);
});

/**
 * Tests that clicking the close button next to a tab manager item
 * will close it.
 */
add_task(async function test_tab_manager_close_button() {
  let win = await BrowserTestUtils.openNewBrowserWindow();
  win.gTabsPanel.init();
  let pinnedTab = await addTabTo(win.gBrowser, URL1);
  win.gBrowser.pinTab(pinnedTab);
  await addTabTo(win.gBrowser, URL2);
  await addTabTo(win.gBrowser, URL3);
  await addTabTo(win.gBrowser, URL4);
  await addTabTo(win.gBrowser, URL5);

  let button = win.document.getElementById("alltabs-button");
  let allTabsView = win.document.getElementById("allTabsMenu-allTabsView");
  let allTabsPopupShownPromise = BrowserTestUtils.waitForEvent(
    allTabsView,
    "ViewShown"
  );
  button.click();
  await allTabsPopupShownPromise;

  let list = win.document.getElementById("allTabsMenu-allTabsView-tabs");

  let [pinnedTabRow] = list.querySelectorAll(".all-tabs-item");
  Assert.ok(tabOf(pinnedTabRow).pinned, "first tab row is for the pinned tab");
  Assert.ok(
    !pinnedTabRow.querySelector(".all-tabs-close-button"),
    "row for pinned tab doesn't have a close button"
  );

  // Disable the tab closing animation so tabs are removed immediately. This simplifies the test.
  win.gReduceMotionOverride = true;
  while (win.gBrowser.tabs.length > 1) {
    let row = [...list.querySelectorAll(".all-tabs-item")].at(-1);
    let tab = tabOf(row);
    Assert.ok(!tab.pinned, "Tab for last row is not pinned");
    let tabClosing = BrowserTestUtils.waitForTabClosing(tab);
    let closeButton = row.querySelector(".all-tabs-close-button");
    Assert.ok(closeButton, "row for last tab has a close button");
    EventUtils.synthesizeMouseAtCenter(closeButton, { button: 1 }, win);
    await tabClosing;
    Assert.ok(true, "Closed a tab with the close button.");
  }
  await BrowserTestUtils.closeWindow(win);
});

/**
 * Tests that middle-clicking on a tab group in the Tab Manager will
 * save+close it.
 */
add_task(
  async function test_tab_manager_save_and_close_tab_group_middle_click() {
    let win = await BrowserTestUtils.openNewBrowserWindow();
    win.gTabsPanel.init();

    let tabGroup = win.gBrowser.addTabGroup([
      await addTabTo(win.gBrowser, URL1),
      await addTabTo(win.gBrowser, URL2),
    ]);
    let tabGroupId = tabGroup.id;
    // Tab data must be fully loaded in order for a group to be considered
    // worthy of saving.
    await Promise.allSettled(
      tabGroup.tabs.map(tab => TabStateFlusher.flush(tab.linkedBrowser))
    );

    let button = win.document.getElementById("alltabs-button");
    let allTabsView = win.document.getElementById("allTabsMenu-allTabsView");
    let allTabsPopupShownPromise = BrowserTestUtils.waitForEvent(
      allTabsView,
      "ViewShown"
    );
    button.click();
    await allTabsPopupShownPromise;

    let list = win.document.getElementById("allTabsMenu-allTabsView-tabs");
    let tabGroupRow = Array.from(list.children).find(
      row => row.getAttribute("tab-group-id") == tabGroupId
    );
    Assert.ok(tabGroupRow, "tab group appears in the list all tabs menu");

    let saveAndClose = Promise.all([
      BrowserTestUtils.waitForEvent(tabGroup, "TabGroupSaved"),
      BrowserTestUtils.waitForEvent(tabGroup, "TabGroupRemoved"),
    ]);
    EventUtils.synthesizeMouseAtCenter(tabGroupRow, { button: 1 }, win);
    await saveAndClose;

    Assert.ok(
      !win.gBrowser.getTabGroupById(tabGroupId),
      "tab group should no longer be in the window"
    );
    Assert.ok(
      win.SessionStore.getSavedTabGroup(tabGroupId),
      "tab group should be saved in session state"
    );

    await BrowserTestUtils.closeWindow(win);
    await TabGroupTestUtils.forgetSavedTabGroups();
  }
);
