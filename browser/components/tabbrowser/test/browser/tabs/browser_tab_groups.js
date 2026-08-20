/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { ASRouterTriggerListeners } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouterTriggerListeners.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.groups.enabled", true]],
  });
});

function createManyTabs(number, win = window) {
  return Array.from({ length: number }, () => {
    return BrowserTestUtils.addTab(win.gBrowser, "about:blank", {
      skipAnimation: true,
    });
  });
}

add_task(async function test_tabGroupCreateAndAddTab() {
  let tab1 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let group = gBrowser.addTabGroup([tab1]);

  Assert.ok(group.id, "group has id");
  Assert.ok(group.tabs.includes(tab1), "tab1 is in group");

  let tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  group.addTabs([tab2]);

  Assert.equal(group.tabs.length, 2, "group has 2 tabs");
  Assert.ok(group.tabs.includes(tab2), "tab2 is in group");

  await removeTabGroup(group);
});

add_task(async function test_tabGroupCreateAndAddSplitView() {
  let tab1 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank");

  let splitview = gBrowser.addTabSplitView([tab1, tab2]);

  Assert.ok(splitview.splitViewId, "split view has id");
  Assert.ok(
    splitview.tabs.includes(tab1) && splitview.tabs.includes(tab2),
    "tab1 and tab2 are in split view"
  );

  let group = gBrowser.addTabGroup([splitview]);

  Assert.equal(group.tabs.length, 2, "group has 2 tabs");

  await removeTabGroup(group);
});

add_task(async function test_tabGroupCreateAndAddTabAtPosition() {
  let tabs = createManyTabs(10);
  let tabToGroup = tabs[5];
  let originalPos = tabToGroup._tPos;
  gBrowser.addTabGroup([tabs[5]], { insertBefore: tabs[5] });

  Assert.equal(tabToGroup._tPos, originalPos, "tab has not changed position");

  tabs.forEach(t => {
    BrowserTestUtils.removeTab(t);
  });
});

add_task(async function test_pinned() {
  let tab1 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  gBrowser.pinTab(tab1);
  gBrowser.pinTab(tab2);
  Assert.ok(tab1.pinned, "Tab1 is pinned");
  Assert.ok(tab2.pinned, "Tab2 is pinned");
  let group = gBrowser.addTabGroup([tab1, tab2]);
  Assert.equal(
    group.tabs.length,
    2,
    "addTabGroup creates a group when only supplied with pinned tabs"
  );
  Assert.ok(!tab1.pinned, "Tab1 is no longer pinned");
  Assert.ok(!tab2.pinned, "Tab2 is no longer pinned");
  group.ungroupTabs();
  gBrowser.pinTab(tab1);
  Assert.ok(tab1.pinned, "Tab1 is pinned again");

  group = gBrowser.addTabGroup([tab1, tab2]);
  Assert.ok(
    group,
    "addTabGroup should create a group when supplied with both pinned and non-pinned tabs"
  );

  Assert.equal(group.tabs.length, 2, "group contains both tabs");
  Assert.ok(!tab1.pinned, "tab1 is no longer pinned");

  BrowserTestUtils.removeTab(tab1);
  await removeTabGroup(group);
});

add_task(async function test_getTabGroups() {
  let tab1 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let group1 = gBrowser.addTabGroup([tab1]);
  Assert.equal(
    gBrowser.tabGroups.length,
    1,
    "there is one group in the tabstrip"
  );

  let tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let group2 = gBrowser.addTabGroup([tab2]);
  Assert.equal(
    gBrowser.tabGroups.length,
    2,
    "there are two groups in the tabstrip"
  );

  await removeTabGroup(group1);
  await removeTabGroup(group2);
  Assert.equal(
    gBrowser.tabGroups.length,
    0,
    "there are no groups in the tabstrip"
  );
});

/**
 * Tests that creating a group without specifying a color will select a
 * unique color.
 */
add_task(async function test_tabGroupUniqueColors() {
  let initialTab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });
  let initialGroup = gBrowser.addTabGroup([initialTab]);
  let existingGroups = [initialGroup];

  for (let i = 2; i <= 9; i++) {
    let newTab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
      skipAnimation: true,
    });
    let newGroup = gBrowser.addTabGroup([newTab]);
    Assert.ok(
      !existingGroups.find(grp => grp.color == newGroup.color),
      `Group ${i} has a distinct color`
    );
    existingGroups.push(newGroup);
  }

  for (let group of existingGroups) {
    await removeTabGroup(group);
  }
});

add_task(async function test_tabGroupCollapseAndExpand() {
  let tab1 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let group = gBrowser.addTabGroup([tab1]);

  Assert.ok(!group.collapsed, "group is expanded by default");

  group.querySelector(".tab-group-label").click();
  Assert.ok(group.collapsed, "group is collapsed on click");

  group.querySelector(".tab-group-label").click();
  Assert.ok(!group.collapsed, "collapsed group is expanded on click");

  group.collapsed = true;
  Assert.ok(group.collapsed, "group is collapsed via API");

  gBrowser.moveTabToExistingGroup(tab2, group);
  Assert.ok(
    group.collapsed,
    "group stays collapsed after moving inactive tab into group"
  );

  group.querySelector(".tab-group-overflow-count").click();
  Assert.ok(
    !group.collapsed,
    "group is expanded after clicking the overflow counter"
  );

  await removeTabGroup(group);
});

add_task(async function test_tabGroupCollapsedTabsNotVisible() {
  let tab = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let group = gBrowser.addTabGroup([tab]);

  Assert.ok(!group.collapsed, "group is expanded by default");

  Assert.ok(
    gBrowser.visibleTabs.includes(tab),
    "tab in expanded tab group is visible"
  );

  group.collapsed = true;
  Assert.ok(
    !gBrowser.visibleTabs.includes(tab),
    "tab in collapsed tab group is not visible"
  );

  // TODO gBrowser.removeTabs breaks if the tab is not in a visible state
  group.collapsed = false;
  await removeTabGroup(group);
});

/*
 * Tests that if a tab group is collapsed while the selected tab is in the group,
 * the selected tab will remain visible while the rest of the tabs hide.
 */
add_task(async function test_tabGroupCollapseWhileSelected() {
  let tabInGroup = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let secondTabInGroup = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let group = gBrowser.addTabGroup([tabInGroup, secondTabInGroup]);

  gBrowser.selectedTab = tabInGroup;

  let collapseFinished = BrowserTestUtils.waitForEvent(
    window,
    "TabGroupCollapse"
  );
  group.collapsed = true;
  await collapseFinished;
  Assert.equal(
    gBrowser.selectedTab,
    tabInGroup,
    "Tab remains selected after group is collapsed"
  );
  Assert.ok(
    tabInGroup.visible,
    "Grouped tab remains visible after group is collapsed because it's selected"
  );

  Assert.ok(
    !secondTabInGroup.visible,
    "The other tab in the collapsed group is no longer visible"
  );

  Assert.equal(
    group.querySelector(".tab-group-overflow-count").textContent,
    "+1",
    "Collapsed group overflow indicator indicates 1 additional tab"
  );

  BrowserTestUtils.removeTab(secondTabInGroup);

  // TODO gBrowser.removeTabs breaks if the tab is not in a visible state
  group.collapsed = false;
  await removeTabGroup(group);
});

add_task(async function test_tabGroupPreventScrollOnUncollapse() {
  let win = await BrowserTestUtils.openNewBrowserWindow();

  // create a tab group that overflows the tabstrip
  await BrowserTestUtils.overflowTabs(null, win);
  let bigGroup = win.gBrowser.addTabGroup(win.gBrowser.tabs);

  // create some more tabs after the group
  createManyTabs(4, win);

  await TestUtils.waitForCondition(() => {
    return Array.from(win.gBrowser.tabs).every(tab => tab._fullyOpen);
  });

  info("selecting the last tab");
  let tabSelected = BrowserTestUtils.waitForEvent(win, "TabSelect");
  win.gBrowser.selectedTab = win.gBrowser.tabs[win.gBrowser.tabs.length - 1];
  await tabSelected;

  info("scrolling to beginning of tabstrip");
  let arrowScrollbox = win.gBrowser.tabContainer.arrowScrollbox;
  let scrolledToStart = TestUtils.waitForCondition(() => {
    return arrowScrollbox.hasAttribute("scrolledtostart");
  }, "Waiting for tabstrip to be scrolled to start");

  arrowScrollbox.ensureElementIsVisible(bigGroup.labelElement, true);
  await scrolledToStart;
  Assert.ok(!bigGroup.collapsed, "Group is expanded");
  Assert.ok(
    arrowScrollbox.hasAttribute("scrolledtostart"),
    "arrowscrollbox is scrolled to start"
  );

  info("Collapsing and expanding the big group");
  await TabGroupTestUtils.toggleCollapsed(bigGroup, true);
  Assert.ok(bigGroup.collapsed, "Group is collapsed");
  await TabGroupTestUtils.toggleCollapsed(bigGroup, false);
  Assert.ok(!bigGroup.collapsed, "Group is expanded");

  Assert.ok(
    arrowScrollbox.hasAttribute("scrolledtostart"),
    "arrowscrollbox is still scrolled to start"
  );

  await TabGroupTestUtils.removeTabGroup(bigGroup);
  BrowserTestUtils.closeWindow(win);
});

/**
 * Bug 1979067 - The collapsed tab group's overflow counter should be included in the bounds
 * calculations that determine whether the tabstrip scroll button is enabled.
 */
add_task(async function test_tabGroupOverflowCounterScrollable() {
  let win = await BrowserTestUtils.openNewBrowserWindow();
  await BrowserTestUtils.overflowTabs(null, win, {
    overflowAtStart: false,
    overflowTabFactor: 3,
  });
  let group = win.gBrowser.addTabGroup([
    win.gBrowser.tabs.at(-2),
    win.gBrowser.tabs.at(-1),
  ]);
  win.gBrowser.selectedTab = win.gBrowser.tabs.at(-2);
  // collapse and expand once to place grouped tab at end of tabstrip
  await TabGroupTestUtils.toggleCollapsed(group);
  await TabGroupTestUtils.toggleCollapsed(group);
  // collapsing again here causes the overflow counter to be placed off screen
  await TabGroupTestUtils.toggleCollapsed(group);
  Assert.ok(
    !win.gBrowser.tabContainer.arrowScrollbox.hasAttribute("scrolledtoend"),
    "Scrollbox correctly overflows at end"
  );
  await TabGroupTestUtils.removeTabGroup(group);
  BrowserTestUtils.closeWindow(win);
});

add_task(async function test_multiselectedTabsInTabGroupDeselectedOnCollapse() {
  let tabInGroup = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let secondTabInGroup = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let group = gBrowser.addTabGroup([tabInGroup, secondTabInGroup]);
  let tabOutsideOfGroup = BrowserTestUtils.addTab(gBrowser, "about:blank");

  gBrowser.selectedTab = tabOutsideOfGroup;

  gBrowser.addToMultiSelectedTabs(tabInGroup);
  gBrowser.addToMultiSelectedTabs(secondTabInGroup);
  Assert.deepEqual(
    gBrowser.selectedTabs,
    [tabInGroup, secondTabInGroup, tabOutsideOfGroup],
    "all tabs should be multiselected"
  );

  let collapseFinished = BrowserTestUtils.waitForEvent(
    window,
    "TabGroupCollapse"
  );
  group.collapsed = true;
  await collapseFinished;

  Assert.deepEqual(
    gBrowser.selectedTabs,
    [tabOutsideOfGroup],
    "tabs in the collapsed tab group should no longer be multiselected"
  );

  // TODO gBrowser.removeTabs breaks if the tab is not in a visible state
  // group.collapsed = false;
  await removeTabGroup(group);
  await BrowserTestUtils.removeTab(tabOutsideOfGroup);
});

add_task(async function test_groupHasActiveTab() {
  let [tab1, tab2, tab3] = createManyTabs(3);
  let group1 = gBrowser.addTabGroup([tab1]);
  let group2 = gBrowser.addTabGroup([tab2]);

  async function activeTabTest(tabsMode) {
    async function addTabToGroup(group, tab) {
      if (tab.group == group) {
        return;
      }
      let tabGrouped = BrowserTestUtils.waitForEvent(
        group,
        "TabGrouped",
        false,
        event => event.detail == tab
      );
      group.addTabs([tab]);
      await tabGrouped;
    }

    info(`hasactivetab test for ${tabsMode} tabs mode`);
    info("tab3 is ungrouped and active");
    gBrowser.selectedTab = tab3;
    Assert.ok(!group1.hasActiveTab, "group1 hasactivetab=false");
    Assert.ok(!group2.hasActiveTab, "group2 hasactivetab=false");
    info("tab1 is in group1 and active");
    gBrowser.selectedTab = tab1;
    Assert.ok(group1.hasActiveTab, "group1 hasactivetab=true");
    Assert.ok(!group2.hasActiveTab, "group2 hasactivetab=false");
    info("tab2 is in group2 and active");
    gBrowser.selectedTab = tab2;
    Assert.ok(!group1.hasActiveTab, "group1 hasactivetab=false");
    Assert.ok(group2.hasActiveTab, "group2 hasactivetab=true");
    info("tab3 is still ungrouped and active");
    gBrowser.selectedTab = tab3;
    Assert.ok(!group1.hasActiveTab, "group1 hasactivetab=false");
    Assert.ok(!group2.hasActiveTab, "group2 hasactivetab=false");
    info("tab3 enters group1 as the active tab");
    await addTabToGroup(group1, tab3);
    Assert.ok(group1.hasActiveTab, "group1 hasactivetab=true");
    Assert.ok(!group2.hasActiveTab, "group2 hasactivetab=false");
    info("tab3 enters group2 as the active tab");
    await addTabToGroup(group2, tab3);
    Assert.ok(!group1.hasActiveTab, "group1 hasactivetab=false");
    Assert.ok(group2.hasActiveTab, "group2 hasactivetab=true");
    info("tab3 becomes ungrouped again as the active tab");
    let tab3Moved = BrowserTestUtils.waitForEvent(tab3, "TabMove");
    let tab3Ungrouped = BrowserTestUtils.waitForEvent(
      group2,
      "TabUngrouped",
      false,
      event => event.detail == tab3
    );
    gBrowser.moveTabToEnd(tab3);
    await Promise.all([tab3Moved, tab3Ungrouped]);
    Assert.ok(!group1.hasActiveTab, "group1 hasactivetab=false");
    Assert.ok(!group2.hasActiveTab, "group2 hasactivetab=false");
  }

  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.revamp", true],
      ["sidebar.verticalTabs", true],
    ],
  });
  await activeTabTest("vertical");
  await SpecialPowers.popPrefEnv();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.revamp", true],
      ["sidebar.verticalTabs", false],
    ],
  });
  await activeTabTest("horizontal");
  await SpecialPowers.popPrefEnv();

  await removeTabGroup(group1);
  await removeTabGroup(group2);
  BrowserTestUtils.removeTab(tab3);
});

add_task(async function test_closingLastTabBeforeCollapsedTabGroup() {
  // If there is one standalone tab that's active and there is a collapsed
  // tab group, and the user closes the standalone tab, the first tab of
  // the collapsed tab group should become the active tab

  let fgWindow = await BrowserTestUtils.openNewBrowserWindow();

  Assert.equal(fgWindow.gBrowser.tabs.length, 1, "only one tab exists");
  let [standaloneTab] = fgWindow.gBrowser.tabs;

  let groupedTab1 = BrowserTestUtils.addTab(fgWindow.gBrowser, "about:blank", {
    skipAnimation: true,
  });
  let groupedTab2 = BrowserTestUtils.addTab(fgWindow.gBrowser, "about:blank", {
    skipAnimation: true,
  });
  let group = fgWindow.gBrowser.addTabGroup([groupedTab1, groupedTab2]);
  group.collapsed = true;

  fgWindow.gBrowser.selectedTab = standaloneTab;

  let waitForClose = BrowserTestUtils.waitForTabClosing(standaloneTab);
  BrowserTestUtils.removeTab(standaloneTab);
  await waitForClose;

  Assert.equal(
    fgWindow.gBrowser.selectedTab,
    groupedTab1,
    "first tab in the group should be the active tab"
  );

  await BrowserTestUtils.closeWindow(fgWindow);
});

add_task(async function test_closingLastTabAfterCollapsedTabGroup() {
  // If there is a collapsed tab group followed by a single standalone tab,
  // and the user closes the standalone tab, the last tab of the collapsed
  // tab group should become the active tab (also expanding the tab group
  // in the process)
  let fgWindow = await BrowserTestUtils.openNewBrowserWindow();

  Assert.equal(fgWindow.gBrowser.tabs.length, 1, "only one tab exists");
  let [standaloneTab] = fgWindow.gBrowser.tabs;

  let groupedTab1 = BrowserTestUtils.addTab(fgWindow.gBrowser, "about:blank", {
    skipAnimation: true,
  });
  let groupedTab2 = BrowserTestUtils.addTab(fgWindow.gBrowser, "about:blank", {
    skipAnimation: true,
  });
  let group = fgWindow.gBrowser.addTabGroup([groupedTab1, groupedTab2], {
    insertBefore: standaloneTab,
  });
  group.collapsed = true;

  fgWindow.gBrowser.selectedTab = standaloneTab;

  let waitForClose = BrowserTestUtils.waitForTabClosing(standaloneTab);
  BrowserTestUtils.removeTab(standaloneTab);
  await waitForClose;

  Assert.equal(
    fgWindow.gBrowser.selectedTab,
    groupedTab2,
    "last tab in the group should be the active tab"
  );

  await BrowserTestUtils.closeWindow(fgWindow);
});

add_task(async function test_tabUngroup() {
  let extraTab1 = BrowserTestUtils.addTab(gBrowser, "about:blank");

  let groupedTab1 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let groupedTab2 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let group = gBrowser.addTabGroup([groupedTab1, groupedTab2]);

  let extraTab2 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let group2 = gBrowser.addTabGroup([extraTab2]);

  Assert.equal(
    groupedTab1._tPos,
    2,
    "grouped tab 1 starts in correct position"
  );
  Assert.equal(
    groupedTab2._tPos,
    3,
    "grouped tab 2 starts in correct position"
  );
  Assert.equal(groupedTab1.group, group, "tab 1 belongs to group");
  Assert.equal(groupedTab2.group, group, "tab 2 belongs to group");

  info("Calling ungroupTabs and waiting for TabGroupRemoved event.");
  let removePromise = BrowserTestUtils.waitForEvent(group, "TabGroupRemoved");
  group.ungroupTabs();
  await removePromise;

  Assert.equal(
    groupedTab1._tPos,
    2,
    "tab 1 is in the same position as before ungroup"
  );
  Assert.equal(
    groupedTab2._tPos,
    3,
    "tab 2 is in the same position as before ungroup"
  );
  Assert.equal(groupedTab1.group, null, "tab 1 no longer belongs to group");
  Assert.equal(groupedTab2.group, null, "tab 2 no longer belongs to group");
  Assert.equal(
    groupedTab1.nextElementSibling,
    groupedTab2,
    "tab 1 moved before tab 2"
  );
  Assert.equal(
    groupedTab2.nextElementSibling,
    group2,
    "tab 2 moved before the next group"
  );

  BrowserTestUtils.removeTab(groupedTab1);
  BrowserTestUtils.removeTab(groupedTab2);
  BrowserTestUtils.removeTab(extraTab1);
  BrowserTestUtils.removeTab(extraTab2);
});

add_task(async function test_tabGroupRemove() {
  let groupedTab = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let group = gBrowser.addTabGroup([groupedTab]);

  await removeTabGroup(group);

  Assert.equal(groupedTab.parentElement, null, "grouped tab is unloaded");
  Assert.equal(group.parentElement, null, "group is unloaded");
});

add_task(async function test_tabGroupDeletesWhenLastTabClosed() {
  let tab = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let group = gBrowser.addTabGroup([tab]);

  let removePromise = BrowserTestUtils.waitForEvent(group, "TabGroupRemoved");
  gBrowser.removeTab(tab);
  await removePromise;

  Assert.equal(group.parent, null, "group is removed from tabbrowser");
});

add_task(async function test_tabGroupMoveToNewWindow() {
  let tabUri = "https://example.com/tab-group-test";
  let groupedTab = await addTab(tabUri);
  let group = gBrowser.addTabGroup([groupedTab], {
    color: "blue",
    label: "test",
  });

  info("Calling adoptTabGroup and waiting for TabGroupRemoved event.");
  let removePromise = BrowserTestUtils.waitForEvent(group, "TabGroupRemoved");
  let fgWindow = await BrowserTestUtils.openNewBrowserWindow();
  let tabGroupCreate = BrowserTestUtils.waitForEvent(
    fgWindow,
    "TabGroupCreate"
  );
  fgWindow.gBrowser.adoptTabGroup(group, { elementIndex: 0 });
  let [, tabGroupCreateEvent] = await Promise.all([
    removePromise,
    tabGroupCreate,
  ]);
  Assert.ok(
    tabGroupCreateEvent.detail.isAdoptingGroup,
    "TabGroupCreate event should report that this tab group was creating by adoption"
  );

  Assert.equal(
    gBrowser.tabGroups.length,
    0,
    "Tab group no longer exists in original window"
  );
  Assert.equal(
    fgWindow.gBrowser.tabGroups.length,
    1,
    "A tab group exists in the new window"
  );

  let newGroup = fgWindow.gBrowser.tabGroups[0];

  Assert.equal(
    newGroup.color,
    "blue",
    "New group has same color as original group"
  );
  Assert.equal(
    newGroup.label,
    "test",
    "New group has same label as original group"
  );
  Assert.equal(
    newGroup.tabs.length,
    1,
    "New group has same number of tabs as original group"
  );
  Assert.equal(
    newGroup.tabs[0].linkedBrowser.currentURI.spec,
    tabUri,
    "New tab has same URI as old tab"
  );

  await removeTabGroup(newGroup);
  await BrowserTestUtils.closeWindow(fgWindow);
});

add_task(async function test_TabGroupEvents() {
  const triggerHandler = sinon.stub();
  const tabGroupCollapsedTrigger =
    ASRouterTriggerListeners.get("tabGroupCollapsed");
  tabGroupCollapsedTrigger.uninit();
  tabGroupCollapsedTrigger.init(triggerHandler);

  let tab1 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let group;

  let createdGroupId = null;
  let tabGroupCreated = BrowserTestUtils.waitForEvent(
    window,
    "TabGroupCreate"
  ).then(event => {
    Assert.ok(
      !event.detail.isAdoptingGroup,
      "a tab group being created from scratch should not be treated like it was adopted from another window"
    );
    createdGroupId = event.target.id;
  });
  group = gBrowser.addTabGroup([tab1]);
  await tabGroupCreated;
  Assert.equal(
    createdGroupId,
    group.id,
    "TabGroupCreate fired with correct group as target"
  );

  let groupedGroupId = null;
  let groupedTab = null;
  let tabGrouped = BrowserTestUtils.waitForEvent(group, "TabGrouped").then(
    event => {
      groupedGroupId = event.target.id;
      groupedTab = event.detail;
    }
  );
  group.addTabs([tab2]);
  await tabGrouped;
  Assert.equal(groupedGroupId, group.id, "TabGrouped fired with correct group");
  Assert.equal(groupedTab, tab2, "TabGrouped fired with correct tab");

  let groupCollapsed = BrowserTestUtils.waitForEvent(group, "TabGroupCollapse");
  group.collapsed = true;
  await groupCollapsed;
  Assert.ok(triggerHandler.calledOnce, "Called once after tab group collapsed");

  let groupExpanded = BrowserTestUtils.waitForEvent(group, "TabGroupExpand");
  group.collapsed = false;
  await groupExpanded;

  let ungroupedGroupId = null;
  let ungroupedTab = null;
  let tabUngrouped = BrowserTestUtils.waitForEvent(group, "TabUngrouped").then(
    event => {
      ungroupedGroupId = event.target.id;
      ungroupedTab = event.detail;
    }
  );
  gBrowser.moveTabToStart(tab2);
  await tabUngrouped;
  Assert.equal(
    ungroupedGroupId,
    group.id,
    "TabUngrouped fired with correct group"
  );
  Assert.equal(ungroupedTab, tab2, "TabUngrouped fired with correct tab");

  let tabGroupRemoved = BrowserTestUtils.waitForEvent(group, "TabGroupRemoved");
  await removeTabGroup(group);
  await tabGroupRemoved;

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
  tabGroupCollapsedTrigger.uninit();
});

add_task(async function test_moveTabGroup() {
  let tab1 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let group = gBrowser.addTabGroup([tab1, tab2]);

  let tabMoveEvents = Promise.all([
    BrowserTestUtils.waitForEvent(tab1, "TabMove"),
    BrowserTestUtils.waitForEvent(tab2, "TabMove"),
  ]);
  info("moving tab group and awaiting TabMove events");
  gBrowser.moveTabToStart(group);
  await tabMoveEvents;

  await removeTabGroup(group);
});

add_task(async function test_moveTabBetweenGroups() {
  let tab1 = await addTab("about:blank");
  let tab2 = await addTab("about:blank");

  let tab1Added = BrowserTestUtils.waitForEvent(
    window,
    "TabGrouped",
    false,
    event => event.detail === tab1
  );
  let tab2Added = BrowserTestUtils.waitForEvent(
    window,
    "TabGrouped",
    false,
    event => event.detail === tab2
  );
  let group1 = gBrowser.addTabGroup([tab1]);
  let group2 = gBrowser.addTabGroup([tab2]);
  await Promise.allSettled([tab1Added, tab2Added]);

  let ungroupedGroupId = null;
  let ungroupedTab = null;
  let tabUngrouped = BrowserTestUtils.waitForEvent(window, "TabUngrouped").then(
    event => {
      ungroupedGroupId = event.target.id;
      ungroupedTab = event.detail;
    }
  );
  let groupedGroupId = null;
  let groupedTab = null;
  let tabGrouped = BrowserTestUtils.waitForEvent(window, "TabGrouped").then(
    event => {
      groupedGroupId = event.target.id;
      groupedTab = event.detail;
      Assert.ok(ungroupedGroupId, "TabUngrouped fires before TabGrouped");
    }
  );

  group2.addTabs([tab1]);
  await Promise.allSettled([tabUngrouped, tabGrouped]);
  Assert.equal(ungroupedGroupId, group1.id, "TabUngrouped fired with group1");
  Assert.equal(ungroupedTab, tab1, "TabUngrouped fired with tab1");
  Assert.equal(groupedGroupId, group2.id, "TabGrouped fired with group2");
  Assert.equal(groupedTab, tab1, "TabUngrouped fired with tab1");

  Assert.ok(
    !group1.parent,
    "group1 has been removed after losing its last tab"
  );
  Assert.equal(group2.tabs.length, 2, "group2 has 2 tabs");

  await removeTabGroup(group2);
});

add_task(async function test_moveTabToStartOrEnd() {
  let tab1 = gBrowser.selectedTab;
  let tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  gBrowser.addTabGroup([tab1, tab2]);
  Assert.equal(tab1._tPos, 0, "tab 1 starts at tab index 0");
  Assert.equal(tab2._tPos, 1, "tab 2 starts at tab index 1");
  Assert.equal(
    tab1.elementIndex,
    1,
    "tab 1 starts at element index 1, after the group label"
  );
  Assert.equal(
    tab2.elementIndex,
    2,
    "tab 2 starts at element index 2, after tab 1"
  );

  gBrowser.moveTabToStart(tab1);
  Assert.ok(
    !tab1.group,
    "first tab is not grouped anymore after moving to start"
  );
  Assert.ok(
    tab2.group,
    "last tab is still grouped after moving first tab to start"
  );
  Assert.equal(
    tab1._tPos,
    0,
    "tab 1 remains at tab index 0 after moving to start"
  );
  Assert.equal(
    tab2._tPos,
    1,
    "tab 2 remains at tab index 1 after tab 1 moved to start"
  );
  Assert.equal(
    tab1.elementIndex,
    0,
    "tab 1 moved to element index 0, before the group label"
  );
  Assert.equal(
    tab2.elementIndex,
    2,
    "tab 2 remains at element index 2, after the group label"
  );

  gBrowser.moveTabToEnd(tab2);
  Assert.ok(!tab2.group, "last tab is not grouped anymore after moving to end");
  Assert.equal(
    tab1._tPos,
    0,
    "tab 1 remains at tab index 0 after tab 2 moved to end"
  );
  Assert.equal(
    tab2._tPos,
    1,
    "tab 2 remains at tab index 1 after moving to end"
  );
  Assert.equal(tab1.elementIndex, 0, "tab 1 remains at element index 0");
  Assert.equal(
    tab2.elementIndex,
    1,
    "tab 2 moved at element index 1 since the group label is gone"
  );

  BrowserTestUtils.removeTab(tab2);
});

add_task(async function test_tabGroupSelect() {
  let tab1 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let tab3 = BrowserTestUtils.addTab(gBrowser, "about:blank");

  let tab1Added = BrowserTestUtils.waitForEvent(
    window,
    "TabGrouped",
    false,
    ev => ev.detail == tab1
  );
  let tab2Added = BrowserTestUtils.waitForEvent(
    window,
    "TabGrouped",
    false,
    ev => ev.detail == tab2
  );

  let group = gBrowser.addTabGroup([tab1, tab2]);
  await Promise.allSettled([tab1Added, tab2Added]);
  gBrowser.selectTabAtIndex(tab3._tPos);
  Assert.ok(tab3.selected, "Tab 3 is selected");
  group.select();
  Assert.ok(group.tabs[0].selected, "First tab is selected");
  gBrowser.selectTabAtIndex(group.tabs[1]._tPos);
  Assert.ok(group.tabs[1].selected, "Second tab is selected");
  group.select();
  Assert.ok(group.tabs[1].selected, "Second tab is still selected");
  group.collapsed = true;
  Assert.ok(group.collapsed, "Group is collapsed");
  Assert.ok(group.tabs[1].selected, "Second tab is still selected");
  gBrowser.selectTabAtIndex(tab3._tPos);
  Assert.ok(tab3.selected, "Tab 3 is selected");
  group.select();
  Assert.ok(group.tabs[0].selected, "First tab in group is selected");

  await removeTabGroup(group);
  BrowserTestUtils.removeTab(tab3);
});

// Opening new tabs from links around/within tab groups
// ---

const PAGE_WITH_LINK_URI =
  "https://example.com/browser/browser/components/tabbrowser/test/browser/tabs/file_new_tab_page.html";
const LINK_ID_SELECTOR = "#link_to_example_com";

/**
 * @returns {Promise<MozTabbrowserTab>}
 */
async function getNewTabFromLink() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.tabs.opentabfor.middleclick", true],
      ["browser.tabs.insertRelatedAfterCurrent", true],
    ],
  });

  let newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser, null, true);
  await BrowserTestUtils.synthesizeMouseAtCenter(
    LINK_ID_SELECTOR,
    { button: 1 },
    gBrowser.selectedBrowser
  );
  let newTab = await newTabPromise;

  return newTab;
}

/**
 * Tests that for a tab inside of a tab group, opening a link on the
 * page in a new tab will open the new tab inside the tab group
 */
add_task(async function test_openLinkInNewTabInsideGroup() {
  let tab1 = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });
  let tabWithLink = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    PAGE_WITH_LINK_URI
  );
  let tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });
  let group = gBrowser.addTabGroup([tab1, tabWithLink, tab2]);

  const newTab = await getNewTabFromLink();
  Assert.equal(
    newTab.group,
    group,
    "new tab should be in the same tab group as the opening page"
  );

  await removeTabGroup(group);
});

/**
 * Tests that for the last tab inside of a tab group, opening a link on the
 * page in a new tab will open the new tab inside the tab group
 */
add_task(async function test_openLinkInNewTabAtEndOfGroup() {
  let [tab1, tab2] = createManyTabs(2);
  let tabWithLink = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    PAGE_WITH_LINK_URI
  );
  let group = gBrowser.addTabGroup([tab1, tab2, tabWithLink]);

  const newTab = await getNewTabFromLink();
  Assert.equal(
    newTab.group,
    group,
    "new tab should be in the same tab group as the opening page"
  );

  await removeTabGroup(group);
});

/**
 * Tests that for a standalone tab to the left of a tab group, opening a link
 * on the page in a new tab will NOT open the new tab inside the tab group
 */
add_task(async function test_openLinkInNewTabBeforeGroup() {
  let tabWithLink = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    PAGE_WITH_LINK_URI
  );
  let [tab1, tab2] = createManyTabs(2);
  let group = gBrowser.addTabGroup([tab1, tab2]);
  gBrowser.selectedTab = tabWithLink;

  const newTab = await getNewTabFromLink();
  Assert.ok(
    !newTab.group,
    "new tab should not be in a group because the opening tab was not in a group"
  );

  await removeTabGroup(group);
  BrowserTestUtils.removeTab(tabWithLink);
  BrowserTestUtils.removeTab(newTab);
});

/*
 * Tests that gBrowser.tabs does not contain tab groups after tabs have been
 * moved between tab groups. Resolves bug1920731.
 */
add_task(async function test_tabsContainNoTabGroups() {
  let tab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });

  let group1 = gBrowser.addTabGroup([tab]);
  gBrowser.addTabGroup([tab]);

  Assert.equal(
    gBrowser.tabs.length,
    2,
    "tab strip contains default tab and our tab"
  );
  gBrowser.tabs.forEach((t, idx) => {
    Assert.equal(
      t.constructor.name,
      "MozTabbrowserTab",
      `gBrowser.tabs[${idx}] is of type MozTabbrowserTab`
    );
  });

  BrowserTestUtils.removeTab(tab);
  await removeTabGroup(group1);
});

add_task(async function test_saveAndCloseGroupViaMiddleClick() {
  let tab = await addTab("about:mozilla");
  let group = gBrowser.addTabGroup([tab]);
  await TabStateFlusher.flush(tab.linkedBrowser);

  Assert.ok(gBrowser.getTabGroupById(group.id), "Group exists in browser");

  let events = [
    BrowserTestUtils.waitForEvent(group, "TabGroupSaved"),
    BrowserTestUtils.waitForEvent(group, "TabGroupRemoved"),
  ];
  triggerMiddleClickOn(group.labelElement);
  await Promise.all(events);

  Assert.ok(
    !gBrowser.getTabGroupById(group.id),
    "Group was removed from browser"
  );
  Assert.ok(SessionStore.getSavedTabGroup(group.id), "Group is in savedGroups");

  SessionStore.forgetSavedTabGroup(group.id);
  // Move the mouse off the tab strip to prevent conflicts in subsequent tests.
  EventUtils.synthesizeMouseAtCenter(document.documentElement, {
    type: "mouseover",
  });
});

add_task(async function test_pinningInteractionsWithTabGroups() {
  const tabs = createManyTabs(3);
  let group = gBrowser.addTabGroup(tabs, { insertBefore: gBrowser.tabs[0] });
  const workingTab = tabs[1];

  Assert.equal(workingTab.group, group, "tab is in group");
  gBrowser.pinTab(workingTab);
  Assert.ok(!workingTab.group, "pinned tab is no longer in the tab group");
  Assert.notEqual(
    group.previousElementSibling,
    workingTab,
    "pinned tab should not be before the tab group"
  );

  gBrowser.unpinTab(workingTab);
  Assert.ok(!workingTab.group, "unpinned tab is still not in the tab group");
  Assert.equal(
    group.previousElementSibling,
    workingTab,
    "unpinned tab is still before before the tab group"
  );

  const moreTabs = createManyTabs(5);
  moreTabs.forEach(tab => gBrowser.pinTab(tab));
  Assert.ok(
    !moreTabs.some(tab => !!tab.group),
    "none of the new pinned tabs are in the tab group"
  );

  const firstPinnedTabToUnpin = gBrowser.tabs[0];
  const tabContainer = document.getElementById("tabbrowser-arrowscrollbox");
  gBrowser.unpinTab(firstPinnedTabToUnpin);
  Assert.ok(
    !firstPinnedTabToUnpin.group,
    "unpinned tab is not in the tab group"
  );
  Assert.equal(
    tabContainer.firstChild,
    firstPinnedTabToUnpin,
    "unpinned tab is the first tab after all of the pinned tabs"
  );

  moreTabs.concat(tabs).forEach(tab => BrowserTestUtils.removeTab(tab));
  await removeTabGroup(group);
});

add_task(async function test_pinFirstGroupedTab() {
  let tabs = [gBrowser.selectedTab, ...createManyTabs(1)];
  let group = gBrowser.addTabGroup(tabs);

  gBrowser.pinTab(tabs[0]);
  Assert.ok(tabs[0].pinned, "pinned first tab of group");
  Assert.ok(!tabs[0].group, "pinning first tab ungroups it");
  gBrowser.unpinTab(tabs[0]);

  await removeTabGroup(group);
});

add_task(async function test_adoptTab() {
  let newWin = await BrowserTestUtils.openNewBrowserWindow();
  let tabs = createManyTabs(3, newWin);
  let group = newWin.gBrowser.addTabGroup(tabs);

  let otherWinTab = BrowserTestUtils.addTab(gBrowser, "about:robots", {
    skipAnimation: true,
  });
  let adoptedTab = newWin.gBrowser.adoptTab(otherWinTab, { tabIndex: 1 });

  Assert.equal(adoptedTab._tPos, 1, "tab adopted into expected position");
  Assert.equal(adoptedTab.group, group, "tab adopted into tab group");

  await removeTabGroup(group);
  await BrowserTestUtils.closeWindow(newWin, { animate: false });
});

add_task(async function test_insertAfterCurrent() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.insertAfterCurrent", true]],
  });

  let group = gBrowser.addTabGroup([gBrowser.selectedTab]);
  let extraTab = BrowserTestUtils.addTab(gBrowser, "about:robots", {
    skipAnimation: true,
  });
  let newTabPromise = BrowserTestUtils.waitForEvent(window, "TabOpen");
  BrowserCommands.openTab();
  let { target: newTab } = await newTabPromise;

  Assert.equal(newTab.group, group, "new tab added to current group");
  Assert.equal(
    group.tabs.indexOf(newTab),
    1,
    "new tab added as second tab to group"
  );

  group.ungroupTabs();
  BrowserTestUtils.removeTab(newTab);
  BrowserTestUtils.removeTab(extraTab);

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_bug1936015() {
  // Checks that groups are properly deleted if the group was created before
  // the user switches between vertical and horizontal tabs.
  const tabs = createManyTabs(3);
  let group = gBrowser.addTabGroup(tabs);

  SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.revamp", true],
      ["sidebar.verticalTabs", true],
    ],
  });

  await removeTabGroup(group);

  Assert.ok(!group.parentNode, "Group has been fully removed from DOM");

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_bug1957723_addTabsByIndex() {
  let initialTab = gBrowser.tabs[0];
  let triggeringPrincipal = Services.scriptSecurityManager.getSystemPrincipal();

  const tabs = createManyTabs(5);
  const tabGroup = gBrowser.addTabGroup([tabs[1], tabs[2], tabs[3]], {
    insertBefore: tabs[1],
  });

  let tab1 = gBrowser.addTab("https://example.com", {
    tabIndex: 2,
    triggeringPrincipal,
  });
  Assert.equal(
    tab1._tPos,
    2,
    "Tab added at starting index of tab group is in correct position"
  );
  Assert.equal(
    tab1.group,
    null,
    "Tab added at starting index of tab group is not in group"
  );
  gBrowser.removeTab(tab1);

  let tab2 = gBrowser.addTab("https://example.com", {
    tabIndex: 4,
    triggeringPrincipal,
  });
  Assert.equal(
    tab2._tPos,
    4,
    "Tab added by index just before end of tab group is in correct position"
  );
  Assert.equal(
    tab2.group.id,
    tabGroup.id,
    "Tab added by index just before end of tab group is in group"
  );
  gBrowser.removeTab(tab2);

  let tab3 = gBrowser.addTab("https://example.com", {
    tabIndex: 5,
    triggeringPrincipal,
  });
  Assert.equal(
    tab3._tPos,
    5,
    "Tab added at index just after end of tab group is in correct position"
  );
  Assert.equal(
    tab3.group,
    null,
    "Tab added at index just after end of tab group is not in group"
  );
  gBrowser.removeTab(tab3);

  // animate: false ensures tabs are synchronously removed from the DOM before the next test runs:
  gBrowser.removeAllTabsBut(initialTab, { animate: false });
});

add_task(async function test_bug1959438_duplicateTabJustBeforeGroup() {
  let initialTab = gBrowser.tabs[0];
  let triggeringPrincipal = Services.scriptSecurityManager.getSystemPrincipal();
  const tabs = createManyTabs(3);

  gBrowser.addTabGroup(tabs);

  Assert.equal(gBrowser.tabs.length, 4, "Tab strip starts with four tabs");

  gBrowser.selectTabAtIndex(0);

  // Simulate an addTab call similar to what would be called when a tab is
  // duplicated. This produces a situation where addTab has no index, but knows
  // it needs to create one next to the currently selected tab, and guesses for
  // itself.
  // If this happens next to a tab group, the resulting element index will
  // point to the tab group label.
  gBrowser.addTab("https://example.com", {
    tabIndex: undefined,
    relatedToCurrent: true,
    ownerTab: gBrowser.selectedTab,
    triggeringPrincipal,
  });

  // This will fail if the tab ends up merged with the tab label.
  Assert.equal(gBrowser.tabs.length, 5, "A new tab was added to the tab strip");

  // animate: false ensures tabs are synchronously removed from the DOM before the next test runs:
  gBrowser.removeAllTabsBut(initialTab, { animate: false });
});

add_task(async function test_bug1969925_adoptLastTabGroupFromWindow() {
  // Assert that TabGrouped and TabUngrouped events fire correctly, even when
  // adopting a tab group that is the only thing in the window
  let newWin = await BrowserTestUtils.openNewBrowserWindow();
  let initialTab = newWin.gBrowser.selectedTab;
  let groupBeforeAdopt = newWin.gBrowser.addTabGroup([initialTab]);

  let tabUngroupedPromise = BrowserTestUtils.waitForEvent(
    newWin,
    "TabUngrouped"
  );
  let tabGroupedPromise = BrowserTestUtils.waitForEvent(window, "TabGrouped");
  let tabGroupCreate = BrowserTestUtils.waitForEvent(window, "TabGroupCreate");

  gBrowser.adoptTabGroup(groupBeforeAdopt);

  let [, tabUngroupedEvent, tabGroupedEvent] = await Promise.all([
    tabGroupCreate,
    tabUngroupedPromise,
    tabGroupedPromise,
  ]);

  let groupAfterAdopt = gBrowser.tabGroups[0];

  Assert.equal(
    tabUngroupedEvent.target,
    groupBeforeAdopt,
    "TabUngrouped event fired from original group"
  );
  Assert.equal(
    tabGroupedEvent.target,
    groupAfterAdopt,
    "TabGrouped event fired from adopted group"
  );

  await removeTabGroup(groupAfterAdopt);
});

/*
 * Tests that if a new tab is opened via right click from the active tab in a collapsed group,
 * the group auto-uncollapses so you can see the tab that was just opened.
 */
add_task(async function test_bug1997096_autoUncollapseOnRightClick() {
  let groupedTab = await addTabTo(gBrowser);
  let group = gBrowser.addTabGroup([groupedTab]);
  gBrowser.selectedTab = groupedTab;
  group.collapsed = true;

  let newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser, null, true);

  let contextMenu = document.getElementById("contentAreaContextMenu");
  let contextMenuShown = BrowserTestUtils.waitForPopupEvent(
    contextMenu,
    "shown"
  );
  await BrowserTestUtils.synthesizeMouseAtCenter(
    "a",
    { type: "contextmenu", button: 2 },
    groupedTab.linkedBrowser
  );
  await contextMenuShown;

  await BrowserTestUtils.activateMenuItem(
    document.getElementById("context-openlinkintab")
  );

  let newTab = await newTabPromise;

  Assert.ok(newTab, "New tab opened via right click");
  Assert.ok(
    !group.collapsed,
    "Group is automatically uncollapsed when opening tab via right click"
  );

  BrowserTestUtils.removeTab(newTab);
  BrowserTestUtils.removeTab(groupedTab);
});

add_task(async function test_bug2007061_hideSplitViewWhenDraggingGroup() {
  let tab1 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let splitview = gBrowser.addTabSplitView([tab1, tab2]);
  let group = gBrowser.addTabGroup([splitview]);

  gBrowser.selectedTab = tab1;
  Assert.ok(tab1.selected, "tab1 in split view is selected");

  Assert.ok(splitview.hasActiveTab, "split view has active tab attribute");

  group.collapsed = true;
  Assert.ok(group.collapsed, "group is collapsed");

  group.isBeingDragged = true;
  Assert.ok(
    group.hasAttribute("movingtabgroup"),
    "group has movingtabgroup attribute when being dragged"
  );

  Assert.ok(
    BrowserTestUtils.isHidden(splitview),
    "split view wrapper is hidden while dragging collapsed group"
  );

  group.isBeingDragged = false;

  await removeTabGroup(group);
});

add_task(
  async function test_bug2014632_splitViewInCollapsedGroupVisibleDuringDrag() {
    let group1Tab = BrowserTestUtils.addTab(gBrowser, "about:blank");
    let group1 = gBrowser.addTabGroup([group1Tab], {
      label: "A very long group name for testing",
    });
    group1.collapsed = true;

    let standaloneTab1 = BrowserTestUtils.addTab(gBrowser, "about:blank");

    let tab1 = BrowserTestUtils.addTab(gBrowser, "about:blank");
    let tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank");
    let splitview = gBrowser.addTabSplitView([tab1, tab2]);
    let group2 = gBrowser.addTabGroup([splitview]);
    group2.collapsed = true;

    let standaloneTab2 = BrowserTestUtils.addTab(gBrowser, "about:blank");

    gBrowser.selectedTab = tab1;
    Assert.ok(tab1.selected, "tab1 in split view is selected");
    Assert.ok(splitview.hasActiveTab, "split view has active tab attribute");
    Assert.ok(group2.collapsed, "group2 is collapsed");

    Assert.ok(
      splitview.visible,
      "split view should be visible when in collapsed group with active tab"
    );

    let dragAndDropElements = gBrowser.tabContainer.dragAndDropElements;
    Assert.ok(
      dragAndDropElements.includes(splitview),
      "split view should be included in dragAndDropElements"
    );

    let splitviewIndex = dragAndDropElements.indexOf(splitview);
    Assert.greater(
      splitviewIndex,
      0,
      "split view should have a valid index in dragAndDropElements"
    );

    group1.isBeingDragged = true;
    Assert.ok(
      splitview.visible,
      "split view should remain visible while dragging another group"
    );

    Assert.ok(
      gBrowser.tabContainer.dragAndDropElements.includes(splitview),
      "split view should remain in dragAndDropElements while dragging another group"
    );
    group1.isBeingDragged = false;

    await removeTabGroup(group1);
    await removeTabGroup(group2);
    BrowserTestUtils.removeTab(standaloneTab1);
    BrowserTestUtils.removeTab(standaloneTab2);
  }
);

/**
 * bug 2017651 - ensure tab-group elements can be garbage collected after removal.
 */
add_task(async function test_tabGroupGarbageCollection() {
  let tabGroup = document.createXULElement("tab-group");
  let weakRef = Cu.getWeakReference(tabGroup);

  document.body.appendChild(tabGroup);
  tabGroup.remove();
  tabGroup = null;

  // Perform multiple GC's to avoid intermittent delayed collection.
  await new Promise(resolve => SpecialPowers.exactGC(resolve));
  await new Promise(resolve => SpecialPowers.exactGC(resolve));
  await new Promise(resolve => SpecialPowers.exactGC(resolve));

  Assert.ok(
    !weakRef.get(),
    "tab-group element should be garbage collected after removal from DOM"
  );
});
