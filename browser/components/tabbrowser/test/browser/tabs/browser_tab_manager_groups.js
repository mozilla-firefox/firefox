/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TabStateFlusher } = ChromeUtils.importESModule(
  "resource:///modules/sessionstore/TabStateFlusher.sys.mjs"
);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.groups.enabled", true]],
  });
  forgetSavedTabGroups();
  window.gTabsPanel.init();
});

/**
 * One-liner to create a basic tab group
 *
 * @param {Object} [options] options for addTabGroup
 * @param {Window} [options.targetWin] window to create the group in
 * @returns {MozTabbrowserTabGroup}
 */
async function createTestGroup(options = {}) {
  let win = options.targetWin ? options.targetWin : window;
  let tab = await addTabTo(win.gBrowser, `data:text/plain,tab1`, {
    skipAnimation: true,
  });
  await TabStateFlusher.flush(tab.linkedBrowser);
  return win.gBrowser.addTabGroup([tab], options);
}

function forgetSavedTabGroups() {
  const tabGroups = SessionStore.getSavedTabGroups();
  tabGroups.forEach(tabGroup => SessionStore.forgetSavedTabGroup(tabGroup.id));
}

/**
 * @param {Window} win
 * @returns {Promise<PanelView>}
 */
async function openTabsMenu(win = window) {
  let viewShown = BrowserTestUtils.waitForEvent(
    win.document.getElementById("allTabsMenu-allTabsView"),
    "ViewShown"
  );
  win.document.getElementById("alltabs-button").click();
  return (await viewShown).target;
}

/**
 * @param {Window} win
 * @returns {Promise<void>}
 */
async function closeTabsMenu(win = window) {
  let panel = win.document
    .getElementById("allTabsMenu-allTabsView")
    .closest("panel");
  if (!panel) {
    return;
  }
  let hidden = BrowserTestUtils.waitForPopupEvent(panel, "hidden");
  panel.hidePopup();
  await hidden;
}

async function assertTabMenuContains(expectedLabels) {
  let allTabsMenu = await openTabsMenu();
  let tabButtons = allTabsMenu.querySelectorAll(
    "#allTabsMenu-allTabsView-tabs .all-tabs-button"
  );

  tabButtons.forEach((button, i) => {
    Assert.equal(
      button.label,
      expectedLabels[i],
      `Expected: ${expectedLabels[i]}`
    );
  });

  await closeTabsMenu();
}

/**
 * Tests that grouped tabs in alltabsmenu are prepended by
 * a group indicator
 */
add_task(async function test_allTabsView() {
  let tabs = [];
  for (let i = 1; i <= 5; i++) {
    tabs.push(
      await addTab(`data:text/plain,tab${i}`, {
        skipAnimation: true,
      })
    );
  }
  let testGroup = gBrowser.addTabGroup([tabs[0], tabs[1]], {
    label: "Test Group",
  });
  gBrowser.addTabGroup([tabs[2], tabs[3]]);

  await assertTabMenuContains([
    "New Tab",
    "data:text/plain,tab5",
    "Test Group",
    "data:text/plain,tab1",
    "data:text/plain,tab2",
    "Unnamed Group",
    "data:text/plain,tab3",
    "data:text/plain,tab4",
  ]);

  testGroup.collapsed = true;
  await assertTabMenuContains([
    "New Tab",
    "data:text/plain,tab5",
    "Test Group",
    // tab1 and tab2 rows should be hidden when Test Group is collapsed
    "Unnamed Group",
    "data:text/plain,tab3",
    "data:text/plain,tab4",
  ]);

  for (let tab of tabs) {
    BrowserTestUtils.removeTab(tab);
  }
});

/**
 * Tests that groups appear in the supplementary group menu
 * when they are saved (and closed,) or open in any window.
 * Clicking an open group in this menu focuses it,
 * and clicking on a saved group restores it.
 */
add_task(async function test_tabGroupsView() {
  forgetSavedTabGroups();
  let allTabsMenu = await openTabsMenu();
  Assert.equal(
    allTabsMenu.querySelectorAll("#allTabsMenu-groupsView toolbaritem").length,
    0,
    "tab groups section is empty initially"
  );
  await closeTabsMenu();

  const savedGroupId = "test-saved-group";
  let group1 = await createTestGroup({
    id: savedGroupId,
    label: "Test Saved Group",
  });
  let group2 = await createTestGroup({
    label: "Test Open Group",
  });

  allTabsMenu = await openTabsMenu();
  Assert.equal(
    allTabsMenu.querySelectorAll("#allTabsMenu-groupsView toolbaritem").length,
    2,
    "tab groups section should list groups from the current window"
  );
  await closeTabsMenu();

  let newWindow = await BrowserTestUtils.openNewBrowserWindow();
  newWindow.gTabsPanel.init();

  allTabsMenu = await openTabsMenu(newWindow);
  Assert.equal(
    allTabsMenu.querySelectorAll("#allTabsMenu-groupsView toolbaritem").length,
    2,
    "should list tab groups from any window"
  );
  Assert.equal(
    allTabsMenu.querySelectorAll(
      "#allTabsMenu-groupsView .all-tabs-group-action-button.tab-group-icon"
    ).length,
    2,
    "groups from any window should be shown as open"
  );
  await closeTabsMenu(newWindow);

  group1.save();
  await removeTabGroup(group1);
  Assert.ok(!gBrowser.getTabGroupById(savedGroupId), "Group 1 removed");

  allTabsMenu = await openTabsMenu(newWindow);
  Assert.equal(
    allTabsMenu.querySelectorAll("#allTabsMenu-groupsView toolbaritem").length,
    2,
    "Saved groups should be shown in groups list"
  );
  let savedGroupButton = allTabsMenu.querySelector(
    "#allTabsMenu-groupsView .all-tabs-group-action-button.all-tabs-group-saved-group"
  );
  Assert.equal(
    savedGroupButton.label,
    "Test Saved Group",
    "Saved group appears as saved"
  );

  // Clicking on an open group should select that group in the origin window
  let openGroupButton = allTabsMenu.querySelector(
    "#allTabsMenu-groupsView .all-tabs-button:not(.all-tabs-group-saved-group)"
  );
  openGroupButton.click();
  Assert.equal(
    gBrowser.selectedTab.group.id,
    group2.id,
    "Tab in group 2 is selected"
  );

  await BrowserTestUtils.closeWindow(newWindow, { animate: false });

  // Clicking on a saved group should restore the group to the current window
  allTabsMenu = await openTabsMenu();
  savedGroupButton = allTabsMenu.querySelector(
    "#allTabsMenu-groupsView .all-tabs-button.all-tabs-group-saved-group"
  );
  savedGroupButton.click();
  group1 = gBrowser.getTabGroupById(savedGroupId);
  Assert.ok(group1, "Group 1 has been restored");

  await removeTabGroup(group1);
  await removeTabGroup(group2);
  forgetSavedTabGroups();
});

/**
 * Tests that the groups section initially shows at most 5 groups, or 4
 * plus a "show more" button.
 */
add_task(async function test_groupsViewShowMore() {
  const groupId = "test-group";
  let groups = [];
  for (let i = 1; i <= 5; i++) {
    let group = await createTestGroup({
      id: groupId + i,
      label: "Test Group " + i,
    });
    groups.push(group);
  }

  let allTabsMenu = await openTabsMenu();
  await BrowserTestUtils.waitForCondition(
    () =>
      allTabsMenu.querySelectorAll(
        "#allTabsMenu-groupsView .all-tabs-group-item"
      ).length === 5,
    "5 groups should be shown in groups list"
  );
  Assert.ok(
    !allTabsMenu.querySelector("#allTabsMenu-groupsViewShowMore"),
    "Show more button should not be shown"
  );
  await closeTabsMenu();

  groups.push(
    await createTestGroup({
      id: groupId + 6,
      label: "Test Group " + 6,
    })
  );
  groups[0].select();
  ok(groups[0].tabs[0].selected, "the first group's tab is selected");
  allTabsMenu = await openTabsMenu();
  Assert.equal(
    allTabsMenu.querySelectorAll("#allTabsMenu-groupsView .all-tabs-group-item")
      .length,
    4,
    "4 groups should be shown in groups list"
  );
  let showMore = allTabsMenu.querySelector("#allTabsMenu-groupsViewShowMore");
  Assert.ok(showMore, "Show more button should be shown");

  let subView = document.getElementById("allTabsMenu-groupsSubView");
  let subViewShown = BrowserTestUtils.waitForEvent(subView, "ViewShown");
  showMore.click();
  await subViewShown;
  let subViewItems = subView.querySelectorAll(".all-tabs-group-item");

  Assert.equal(
    subViewItems.length,
    6,
    "6 groups should be shown in groups sub view"
  );
  Assert.ok(
    !subView.querySelector("#allTabsMenu-groupsViewShowMore"),
    "Show more button should not be shown in sub view"
  );
  await EventUtils.synthesizeMouseAtCenter(subViewItems[1], {}, window);
  ok(
    groups.at(-1).tabs[0].selected,
    "last created group's tab is selected after clicking second item in groups sub view"
  );

  while (groups.length) {
    await removeTabGroup(groups.pop());
  }
});

/**
 * Tests the context menu behaviors for saved groups
 */
add_task(async function test_tabGroupsViewContextMenu_savedGroups() {
  let savedGroupId = "test-saved-group";
  let group1 = await createTestGroup({
    id: savedGroupId,
    label: "Test Saved Group",
  });
  group1.save();
  await removeTabGroup(group1);
  Assert.ok(!gBrowser.getTabGroupById(savedGroupId), "Group 1 removed");

  let newWindow = await BrowserTestUtils.openNewBrowserWindow();
  newWindow.gTabsPanel.init();
  let allTabsMenu = await openTabsMenu(newWindow);
  let savedGroupButton = allTabsMenu.querySelector(
    `#allTabsMenu-groupsView .all-tabs-group-saved-group[data-tab-group-id="${savedGroupId}"]`
  );
  Assert.equal(
    savedGroupButton.label,
    "Test Saved Group",
    "Saved group appears as saved"
  );

  info("open saved group in current window");

  let menu = await getContextMenu(
    savedGroupButton,
    "saved-tab-group-context-menu"
  );
  let waitForGroup = BrowserTestUtils.waitForEvent(
    newWindow,
    "SSWindowStateReady"
  );
  menu.querySelector("#saved-tab-group-context-menu_openInThisWindow").click();
  menu.hidePopup();
  await waitForGroup;
  await BrowserTestUtils.waitForCondition(
    () =>
      !allTabsMenu.querySelector(
        `#allTabsMenu-groupsView .all-tabs-group-saved-group[data-tab-group-id="${savedGroupId}"]`
      ),
    "Saved group item has been removed from the menu"
  );
  await closeTabsMenu(newWindow);

  group1 = gBrowser.getTabGroupById(savedGroupId);
  Assert.equal(group1.name, "Test Saved Group", "Saved group was reopened");

  // re-save group
  group1.save();
  await removeTabGroup(group1);
  Assert.ok(!gBrowser.getTabGroupById(savedGroupId), "Group 1 removed");

  info("delete saved group");

  allTabsMenu = await openTabsMenu(newWindow);
  savedGroupButton = allTabsMenu.querySelector(
    `#allTabsMenu-groupsView [data-tab-group-id="${savedGroupId}"]`
  );
  Assert.ok(savedGroupButton, "saved group should be in the TOM");

  menu = await getContextMenu(savedGroupButton, "saved-tab-group-context-menu");
  menu.querySelector("#saved-tab-group-context-menu_delete").click();
  menu.hidePopup();

  await BrowserTestUtils.waitForCondition(
    () =>
      !allTabsMenu.querySelector(
        `#allTabsMenu-groupsView [data-tab-group-id="${savedGroupId}"]`
      ),
    "Saved group item has been removed"
  );
  await closeTabsMenu(newWindow);

  await BrowserTestUtils.closeWindow(newWindow, { animate: false });
  forgetSavedTabGroups();
});

/**
 * Tests behavior of the context menu for open groups
 */
add_task(async function test_tabGroupsViewContextMenu_openGroups() {
  let groupId = "test-group";

  let otherWindow = await BrowserTestUtils.openNewBrowserWindow();
  let initialTab = otherWindow.gBrowser.tabs[0];
  await createTestGroup({
    id: groupId,
    label: "Test Group",
    targetWin: otherWindow,
  });
  // remove the initial tab to test context menu disabling
  BrowserTestUtils.removeTab(initialTab, { animate: false });
  otherWindow.gTabsPanel.init();
  let allTabsMenu = await openTabsMenu();

  info("move group from another window to this window");
  let group1MenuItem = allTabsMenu.querySelector(
    `#allTabsMenu-groupsView [data-tab-group-id="${groupId}"]`
  );
  let menu = await getContextMenu(
    group1MenuItem,
    "open-tab-group-context-menu"
  );
  let waitForGroup = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "TabGroupCreate"
  );
  Assert.ok(
    menu.querySelector("#open-tab-group-context-menu_moveToNewWindow").disabled,
    "'Move to New Window' is disabled"
  );
  let menuHidden = BrowserTestUtils.waitForPopupEvent(menu, "hidden");
  menu.hidePopup();
  await menuHidden;
  await closeTabsMenu();
  await addTabTo(otherWindow.gBrowser);
  allTabsMenu = await openTabsMenu();
  group1MenuItem = allTabsMenu.querySelector(
    `#allTabsMenu-groupsView [data-tab-group-id="${groupId}"]`
  );
  info("opening context menu");
  menu = await getContextMenu(group1MenuItem, "open-tab-group-context-menu");
  Assert.ok(
    !menu.querySelector("#open-tab-group-context-menu_moveToNewWindow")
      .disabled,
    "'Move to New Window' is enabled"
  );
  menu.querySelector("#open-tab-group-context-menu_moveToThisWindow").click();
  await waitForGroup;
  await closeTabsMenu();

  Assert.equal(
    otherWindow.gBrowser.tabGroups.length,
    0,
    "tab group should have moved from the second window"
  );
  Assert.equal(
    gBrowser.tabGroups.length,
    1,
    "tab group should have moved to the starting window"
  );
  Assert.equal(
    gBrowser.selectedTab,
    gBrowser.tabGroups[0].tabs[0],
    "tab group's first tab should be selected"
  );

  Assert.equal(
    gBrowser.tabGroups[0].id,
    groupId,
    "tab group in window should be the one that was moved"
  );
  allTabsMenu = await openTabsMenu();
  Assert.ok(
    allTabsMenu.querySelector(
      `#allTabsMenu-groupsView [data-tab-group-id="${groupId}"]`
    ),
    "Group item is still in the menu"
  );
  await closeTabsMenu();

  info("move group to a new window");

  allTabsMenu = await openTabsMenu(otherWindow);
  group1MenuItem = allTabsMenu.querySelector(
    `#allTabsMenu-groupsView [data-tab-group-id="${groupId}"]`
  );
  menu = await getContextMenu(group1MenuItem, "open-tab-group-context-menu");
  waitForGroup = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "TabGroupRemoved"
  );
  let newWindow;
  let waitForWindow = BrowserTestUtils.waitForNewWindow().then(
    receivedWindow => {
      newWindow = receivedWindow;
    }
  );
  menu.querySelector("#open-tab-group-context-menu_moveToNewWindow").click();
  menuHidden = BrowserTestUtils.waitForPopupEvent(menu, "hidden");
  menu.hidePopup();
  await Promise.allSettled([menuHidden, waitForGroup, waitForWindow]);

  await closeTabsMenu(otherWindow);
  Assert.equal(
    otherWindow.gBrowser.tabGroups.length,
    0,
    "tab group should have moved out of the second window to a new window"
  );

  await BrowserTestUtils.closeWindow(otherWindow, { animate: false });

  info("delete group");

  await addTabTo(newWindow.gBrowser);

  allTabsMenu = await openTabsMenu(window);
  group1MenuItem = allTabsMenu.querySelector(
    `#allTabsMenu-groupsView [data-tab-group-id="${groupId}"]`
  );
  menu = await getContextMenu(group1MenuItem, "open-tab-group-context-menu");

  info("ensure there's at least one tab group");
  info(gBrowser.getAllTabGroups().length);
  await TestUtils.waitForCondition(
    () => gBrowser.getAllTabGroups().length,
    "there's at least one tab group"
  );
  menu.querySelector("#open-tab-group-context-menu_delete").click();
  menu.hidePopup();
  info("waiting for delete");
  await TestUtils.waitForCondition(
    () => !gBrowser.getAllTabGroups().length,
    "wait for tab group to be deleted"
  );
  info("waiting for menu sync");
  await BrowserTestUtils.waitForCondition(
    () =>
      !allTabsMenu.querySelector(
        `#allTabsMenu-groupsView [data-tab-group-id="${groupId}"]`
      ),
    "Group item has been removed from the menu"
  );
  await closeTabsMenu(window);
  Assert.equal(gBrowser.getAllTabGroups().length, 0, "Group was deleted");
  await BrowserTestUtils.closeWindow(newWindow);
  forgetSavedTabGroups();
});

/**
 * Tests that groups opened in non-private windows do not appear in menus in
 * private windows, and vice-versa.
 */
add_task(async function test_tabGroupsIsolatedByPrivateness() {
  forgetSavedTabGroups();
  const groupsViewId = "#allTabsMenu-groupsView toolbaritem";

  info(
    "Test that non-private groups don't appear in menus from private windows"
  );

  let privateWindow = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  privateWindow.gTabsPanel.init();

  let allTabsMenuPrivate = await openTabsMenu(privateWindow);
  Assert.equal(
    allTabsMenuPrivate.querySelectorAll(groupsViewId).length,
    0,
    "Tab groups section is initially empty in private window"
  );
  await closeTabsMenu(privateWindow);

  let nonPrivateGroup = await createTestGroup({
    label: "non-private-group",
  });
  allTabsMenuPrivate = await openTabsMenu(privateWindow);
  Assert.equal(
    allTabsMenuPrivate.querySelectorAll(groupsViewId).length,
    0,
    "Tab groups section is still empty even when tab group open in non-private window"
  );
  await closeTabsMenu(privateWindow);
  await removeTabGroup(nonPrivateGroup);

  info("Test that private windows don't appear in non-private menus");

  let allTabsMenuNonPrivate = await openTabsMenu();
  Assert.equal(
    allTabsMenuNonPrivate.querySelectorAll(groupsViewId).length,
    0,
    "Tab groups section is initially empty in non-private window"
  );
  await closeTabsMenu();

  let privateGroup = await createTestGroup({
    label: "private-group",
    targetWin: privateWindow,
  });
  allTabsMenuNonPrivate = await openTabsMenu();
  Assert.equal(
    allTabsMenuNonPrivate.querySelectorAll(groupsViewId).length,
    0,
    "Tab groups section is still empty even when tab group open in private window"
  );
  await closeTabsMenu();
  await removeTabGroup(privateGroup);

  await BrowserTestUtils.closeWindow(privateWindow, { animate: false });
  forgetSavedTabGroups();
});

/**
 * Tests that saved groups never appear in menus from private windows.
 */
add_task(async function test_tabGroupsIsolatedByPrivateness() {
  forgetSavedTabGroups();
  const groupsViewId = "#allTabsMenu-groupsView toolbaritem";

  info("Test that saved groups do not appear in menus from private windows");

  let privateWindow = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  privateWindow.gTabsPanel.init();

  let allTabsMenuPrivate = await openTabsMenu(privateWindow);
  Assert.equal(
    allTabsMenuPrivate.querySelectorAll(groupsViewId).length,
    0,
    "Tab groups section is initially empty in private window"
  );
  await closeTabsMenu(privateWindow);

  let savedGroup = await createTestGroup({
    label: "saved-group",
  });
  savedGroup.save();
  await removeTabGroup(savedGroup);

  allTabsMenuPrivate = await openTabsMenu(privateWindow);
  Assert.equal(
    allTabsMenuPrivate.querySelectorAll(groupsViewId).length,
    0,
    "Tab groups section is still empty even when saved tab group exists"
  );
  await closeTabsMenu(privateWindow);

  await BrowserTestUtils.closeWindow(privateWindow, { animate: false });
  forgetSavedTabGroups();
});

/**
 * Tests that creating a new tab group using the tab context menu in the
 * all tabs list will correctly update the all tabs list when it is next opened.
 */
add_task(async function test_createGroupFromTabContextMenu() {
  let tabs = [];
  for (let i = 1; i <= 2; i++) {
    tabs.push(
      await addTab(`data:text/plain,tab${i}`, {
        skipAnimation: true,
      })
    );
  }
  await assertTabMenuContains([
    "New Tab",
    "data:text/plain,tab1",
    "data:text/plain,tab2",
  ]);
  let allTabsMenu = await openTabsMenu();
  let tabsButtons = Array.from(
    allTabsMenu.querySelectorAll(
      "#allTabsMenu-allTabsView-tabs .all-tabs-button"
    )
  );
  let tab1Button = tabsButtons.find(
    button => button.label == "data:text/plain,tab1"
  );
  Assert.ok(tab1Button, "tab1 is present in the all tabs menu");
  info("open tab1's tab context menu and create a new tab group from it");
  let tabContextMenu = await getContextMenu(tab1Button, "tabContextMenu");
  let createNewGroupButton = document.getElementById(
    "context_moveTabToNewGroup"
  );
  let tabGroupCreatePromise = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "TabGroupCreate"
  );
  createNewGroupButton.click();
  await tabGroupCreatePromise;
  await closeContextMenu(tabContextMenu);

  await assertTabMenuContains([
    "New Tab",
    "Unnamed Group",
    "data:text/plain,tab1",
    "data:text/plain,tab2",
  ]);

  for (let tab of tabs) {
    BrowserTestUtils.removeTab(tab);
  }
  forgetSavedTabGroups();
});
