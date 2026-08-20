/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

const { UrlbarTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlbarTestUtils.sys.mjs"
);

let resetTelemetry = async () => {
  // Ensure the tab movements deferred task has an idle moment to fire.
  await new Promise(resolve => ChromeUtils.idleDispatch(resolve));
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
};

/** @type {Window} */
let win;

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["test.wait300msAfterTabSwitch", true],
      ["browser.tabs.groups.enabled", true],
      ["browser.urlbar.scotchBonnet.enableOverride", true],
    ],
  });
  win = await BrowserTestUtils.openNewBrowserWindow();
  win.gTabsPanel.init();
  registerCleanupFunction(async () => {
    await BrowserTestUtils.closeWindow(win);
    await SpecialPowers.popPrefEnv();
  });
});

add_task(async function test_tabGroupTelemetry() {
  await resetTelemetry();

  let tabGroupCreateTelemetry;

  let group1tab = BrowserTestUtils.addTab(win.gBrowser, "https://example.com");
  await BrowserTestUtils.browserLoaded(group1tab.linkedBrowser);

  let tabGroupCreateByUser = BrowserTestUtils.waitForEvent(
    win.gBrowser.tabContainer,
    "TabGroupCreateByUser"
  );
  let group1 = win.gBrowser.addTabGroup([group1tab], {
    metricsContext: gBrowser.TabMetrics.userTriggeredContext("test-source"),
  });
  win.gBrowser.tabGroupMenu.close();
  await tabGroupCreateByUser;

  await TestUtils.waitForCondition(() => {
    tabGroupCreateTelemetry = Glean.tabgroup.createGroup.testGetValue();
    return tabGroupCreateTelemetry?.length == 1;
  }, "Wait for createGroup event to be recorded");

  Assert.deepEqual(
    tabGroupCreateTelemetry[0].extra,
    {
      id: group1.id,
      layout: "horizontal",
      source: "test-source",
      tabs: "1",
    },
    "tabGroupCreate event extra_keys has correct values after tab group create"
  );

  await TestUtils.waitForCondition(() => {
    return (
      Glean.tabgroup.tabCountInGroups.inside.testGetValue() == 1 &&
      Glean.tabgroup.tabCountInGroups.outside.testGetValue() == 2
    );
  }, "Wait for group tab counts to settle");

  Assert.equal(
    Glean.tabgroup.tabCountInGroups.inside.testGetValue(),
    1,
    "tabCountInGroups.inside has correct value"
  );
  Assert.equal(
    Glean.tabgroup.tabCountInGroups.outside.testGetValue(),
    2,
    "tabCountInGroups.outside has correct value"
  );

  await TestUtils.waitForCondition(() => {
    return (
      Glean.tabgroup.tabsPerActiveGroup.median.testGetValue() == 1 &&
      Glean.tabgroup.tabsPerActiveGroup.average.testGetValue() == 1 &&
      Glean.tabgroup.tabsPerActiveGroup.max.testGetValue() == 1 &&
      Glean.tabgroup.tabsPerActiveGroup.min.testGetValue() == 1
    );
  }, "Wait for active group tab statistics to settle");

  Assert.equal(
    Glean.tabgroup.tabsPerActiveGroup.median.testGetValue(),
    1,
    "tabsPerActiveGroup.median has correct value"
  );
  Assert.equal(
    Glean.tabgroup.tabsPerActiveGroup.average.testGetValue(),
    1,
    "tabsPerActiveGroup.average has correct value"
  );
  Assert.equal(
    Glean.tabgroup.tabsPerActiveGroup.max.testGetValue(),
    1,
    "tabsPerActiveGroup.max has correct value"
  );
  Assert.equal(
    Glean.tabgroup.tabsPerActiveGroup.min.testGetValue(),
    1,
    "tabsPerActiveGroup.min has correct value"
  );

  await resetTelemetry();

  let group2Tabs = [
    BrowserTestUtils.addTab(win.gBrowser, "https://example.com"),
    BrowserTestUtils.addTab(win.gBrowser, "https://example.com"),
    BrowserTestUtils.addTab(win.gBrowser, "https://example.com"),
  ];
  await Promise.all(
    group2Tabs.map(t => BrowserTestUtils.browserLoaded(t.linkedBrowser))
  );

  let group2 = win.gBrowser.addTabGroup(group2Tabs, {
    metricsContext: gBrowser.TabMetrics.userTriggeredContext("test-source"),
  });
  win.gBrowser.tabGroupMenu.close();

  await TestUtils.waitForCondition(() => {
    return (
      Glean.tabgroup.tabCountInGroups.inside.testGetValue() == 4 &&
      Glean.tabgroup.tabCountInGroups.outside.testGetValue() == 2
    );
  }, "Wait for group tab counts to settle");

  Assert.equal(
    Glean.tabgroup.tabCountInGroups.inside.testGetValue(),
    4,
    "tabCountInGroups.inside has correct value after adding a new tab group"
  );
  Assert.equal(
    Glean.tabgroup.tabCountInGroups.outside.testGetValue(),
    2,
    "tabCountInGroups.outside has correct value after adding a new tab group"
  );

  await TestUtils.waitForCondition(() => {
    return (
      Glean.tabgroup.tabsPerActiveGroup.median.testGetValue() == 2 &&
      Glean.tabgroup.tabsPerActiveGroup.average.testGetValue() == 2 &&
      Glean.tabgroup.tabsPerActiveGroup.max.testGetValue() == 3 &&
      Glean.tabgroup.tabsPerActiveGroup.min.testGetValue() == 1
    );
  }, "Wait for active group tab statistics to settle");

  Assert.equal(
    Glean.tabgroup.tabsPerActiveGroup.median.testGetValue(),
    2,
    "tabsPerActiveGroup.median has correct value after adding a new tab group"
  );
  Assert.equal(
    Glean.tabgroup.tabsPerActiveGroup.average.testGetValue(),
    2,
    "tabsPerActiveGroup.average has correct value after adding a new tab group"
  );
  Assert.equal(
    Glean.tabgroup.tabsPerActiveGroup.max.testGetValue(),
    3,
    "tabsPerActiveGroup.max has correct value after adding a new tab group"
  );
  Assert.equal(
    Glean.tabgroup.tabsPerActiveGroup.min.testGetValue(),
    1,
    "tabsPerActiveGroup.min has correct value after adding a new tab group"
  );

  await resetTelemetry();

  let newTabInGroup2 = BrowserTestUtils.addTab(
    win.gBrowser,
    "https://example.com"
  );
  await BrowserTestUtils.browserLoaded(newTabInGroup2.linkedBrowser);

  group2.addTabs([newTabInGroup2]);

  await TestUtils.waitForCondition(() => {
    return (
      Glean.tabgroup.tabCountInGroups.inside.testGetValue() == 5 &&
      Glean.tabgroup.tabCountInGroups.outside.testGetValue() == 2
    );
  }, "Wait for group tab counts to settle");

  Assert.equal(
    Glean.tabgroup.tabCountInGroups.inside.testGetValue(),
    5,
    "tabCountInGroups.inside has correct value after modifying a tab group"
  );
  Assert.equal(
    Glean.tabgroup.tabCountInGroups.outside.testGetValue(),
    2,
    "tabCountInGroups.outside has correct value after modifying a tab group"
  );

  await TestUtils.waitForCondition(() => {
    return (
      Glean.tabgroup.tabsPerActiveGroup.median.testGetValue() == 2 &&
      Glean.tabgroup.tabsPerActiveGroup.average.testGetValue() == 2 &&
      Glean.tabgroup.tabsPerActiveGroup.max.testGetValue() == 4 &&
      Glean.tabgroup.tabsPerActiveGroup.min.testGetValue() == 1
    );
  }, "Wait for active group tab statistics to settle");

  Assert.equal(
    Glean.tabgroup.tabsPerActiveGroup.median.testGetValue(),
    2,
    "tabsPerActiveGroup.median has correct value after modifying a tab group"
  );
  Assert.equal(
    Glean.tabgroup.tabsPerActiveGroup.average.testGetValue(),
    2,
    "tabsPerActiveGroup.average has correct value after modifying a tab group"
  );
  Assert.equal(
    Glean.tabgroup.tabsPerActiveGroup.max.testGetValue(),
    4,
    "tabsPerActiveGroup.max has correct value after modifying a tab group"
  );
  Assert.equal(
    Glean.tabgroup.tabsPerActiveGroup.min.testGetValue(),
    1,
    "tabsPerActiveGroup.min has correct value after modifying a tab group"
  );

  await resetTelemetry();

  group2.collapsed = true;

  await TestUtils.waitForCondition(() => {
    return (
      Glean.tabgroup.activeGroups.collapsed.testGetValue() == 1 &&
      Glean.tabgroup.activeGroups.expanded.testGetValue() == 1
    );
  }, "Wait for the activeGroups metrics to settle");

  Assert.equal(
    Glean.tabgroup.activeGroups.collapsed.testGetValue(),
    1,
    "activeGroups.collapsed has correct value after collapsing a tab group"
  );
  Assert.equal(
    Glean.tabgroup.activeGroups.expanded.testGetValue(),
    1,
    "activeGroups.expanded has correct value after collapsing a tab group"
  );

  await resetTelemetry();

  await removeTabGroup(group1);
  await removeTabGroup(group2);
});

add_task(async function test_tabGroupTelemetrySaveGroup() {
  let tabGroupSaveTelemetry;
  let tabGroupDeleteTelemetry;

  await resetTelemetry();

  let group1tab = BrowserTestUtils.addTab(win.gBrowser, "https://example.com");
  await BrowserTestUtils.browserLoaded(group1tab.linkedBrowser);
  let group1 = win.gBrowser.addTabGroup([group1tab]);
  group1.saveAndClose();

  await TestUtils.waitForCondition(() => {
    tabGroupSaveTelemetry = Glean.tabgroup.save.testGetValue();
    return tabGroupSaveTelemetry?.length == 1;
  }, "Wait for tabgroup.save event after tab group save");

  Assert.deepEqual(
    tabGroupSaveTelemetry[0].extra,
    {
      user_triggered: "false",
      id: group1.id,
    },
    "tabgroup.save event extra_keys has correct values after tab group save"
  );

  Assert.equal(
    Glean.tabgroup.delete.testGetValue(),
    null,
    "We don't record non user-triggered delete events"
  );

  await resetTelemetry();

  let group2tab = BrowserTestUtils.addTab(win.gBrowser, "https://example.com");
  await BrowserTestUtils.browserLoaded(group2tab.linkedBrowser);
  let group2 = gBrowser.addTabGroup([group2tab]);
  group2.saveAndClose(
    gBrowser.TabMetrics.userTriggeredContext(
      gBrowser.TabMetrics.METRIC_SOURCE.TAB_GROUP_MENU
    )
  );

  await TestUtils.waitForCondition(() => {
    tabGroupSaveTelemetry = Glean.tabgroup.save.testGetValue();
    tabGroupDeleteTelemetry = Glean.tabgroup.delete.testGetValue();
    return (
      tabGroupSaveTelemetry?.length == 1 && tabGroupDeleteTelemetry?.length == 1
    );
  }, "Wait for tabgroup.save and tabgroup.close event after tab group save");

  Assert.deepEqual(
    tabGroupSaveTelemetry[0].extra,
    {
      user_triggered: "true",
      id: group2.id,
    },
    "tabgroup.save event extra_keys has correct values after tab group save"
  );

  Assert.deepEqual(
    tabGroupDeleteTelemetry[0].extra,
    {
      id: group2.id,
      source: gBrowser.TabMetrics.METRIC_SOURCE.TAB_GROUP_MENU,
    },
    "tabgroup.delete event extra_keys has correct values after tab group delete"
  );
});

function forgetSavedTabGroups() {
  let tabGroups = SessionStore.getSavedTabGroups();
  tabGroups.forEach(tabGroup => SessionStore.forgetSavedTabGroup(tabGroup.id));
}

add_task(async function test_tabGroupTelemetry_savedGroupMetrics() {
  forgetSavedTabGroups();
  await resetTelemetry();

  let group1Tabs = Array.from({ length: 3 }).map(() =>
    BrowserTestUtils.addTab(win.gBrowser, "https://example.com")
  );
  await Promise.all(
    group1Tabs.map(tab => BrowserTestUtils.browserLoaded(tab.linkedBrowser))
  );

  let group2Tabs = Array.from({ length: 5 }).map(() =>
    BrowserTestUtils.addTab(win.gBrowser, "https://example.com")
  );
  await Promise.all(
    group2Tabs.map(tab => BrowserTestUtils.browserLoaded(tab.linkedBrowser))
  );

  let group1 = win.gBrowser.addTabGroup(group1Tabs);
  let group2 = win.gBrowser.addTabGroup(group2Tabs);
  await TabStateFlusher.flushWindow(win);
  await saveAndCloseGroup(group1);
  await saveAndCloseGroup(group2);

  await TestUtils.waitForCondition(() => {
    return [
      Glean.tabgroup.savedGroups,
      Glean.tabgroup.tabsPerSavedGroup.max,
      Glean.tabgroup.tabsPerSavedGroup.min,
      Glean.tabgroup.tabsPerSavedGroup.median,
      Glean.tabgroup.tabsPerSavedGroup.average,
    ].every(metric => metric.testGetValue());
  }, "Wait for saved tab group metrics to populate");

  Assert.equal(
    Glean.tabgroup.savedGroups.testGetValue(),
    2,
    "should count 2 saved tab groups"
  );
  Assert.equal(
    Glean.tabgroup.tabsPerSavedGroup.max.testGetValue(),
    5,
    "should count 5 tabs as minimum number of tabs"
  );
  Assert.equal(
    Glean.tabgroup.tabsPerSavedGroup.min.testGetValue(),
    3,
    "should count 3 tabs as minimum number of tabs"
  );
  Assert.equal(
    Glean.tabgroup.tabsPerSavedGroup.median.testGetValue(),
    4,
    "should count 4 tabs as median number of tabs"
  );
  Assert.equal(
    Glean.tabgroup.tabsPerSavedGroup.average.testGetValue(),
    4,
    "should count 4 tabs as average number of tabs"
  );

  forgetSavedTabGroups();
  await resetTelemetry();
});

/**
 * @param {MozTabbrowserTabGroup} tabGroup
 * @returns {Promise<MozPanel>}
 *   Panel holding the tab group context menu for the requested tab group.
 */
async function openTabGroupContextMenu(tabGroup) {
  let tabgroupEditor =
    tabGroup.ownerDocument.getElementById("tab-group-editor");
  let tabgroupPanel = tabgroupEditor.panel;

  let panelShown = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "shown");
  EventUtils.synthesizeMouseAtCenter(
    tabGroup.querySelector(".tab-group-label"),
    { type: "contextmenu", button: 2 },
    tabGroup.documentGlobal
  );
  await panelShown;

  return tabgroupPanel;
}

add_task(async function test_tabGroupContextMenu_deleteTabGroup() {
  await resetTelemetry();

  let tab = BrowserTestUtils.addTab(win.gBrowser, "https://example.com");
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);

  let group = win.gBrowser.addTabGroup([tab]);
  // Close the automatically-opened "create tab group" menu.
  win.gBrowser.tabGroupMenu.close();
  let groupId = group.id;

  let menu = await openTabGroupContextMenu(group);
  let deleteTabGroupButton = menu.querySelector("#tabGroupEditor_deleteGroup");
  deleteTabGroupButton.click();

  await TestUtils.waitForCondition(
    () => !win.gBrowser.tabGroups.includes(group),
    "wait for group to be deleted"
  );

  let tabGroupDeleteEvents = Glean.tabgroup.delete.testGetValue();
  Assert.equal(
    tabGroupDeleteEvents.length,
    1,
    "should have recorded a tabgroup.delete event"
  );

  let [tabGroupDeleteEvent] = tabGroupDeleteEvents;
  Assert.deepEqual(
    tabGroupDeleteEvent.extra,
    {
      source: "tab_group",
      id: groupId,
    },
    "should have recorded the correct source and ID"
  );

  await resetTelemetry();
});

/**
 * @returns {Promise<PanelView>}
 */
async function openTabsMenu() {
  let viewShown = BrowserTestUtils.waitForEvent(
    win.document.getElementById("allTabsMenu-allTabsView"),
    "ViewShown"
  );
  win.document.getElementById("alltabs-button").click();
  return (await viewShown).target;
}

/**
 * @returns {Promise<void>}
 */
async function closeTabsMenu() {
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

/**
 * Returns a new basic, unnamed tab group that is fully loaded in the browser
 * and in session state.
 *
 * @returns {Promise<MozTabbrowserTabGroup>}
 */
async function makeTabGroup(name = "") {
  let tab = BrowserTestUtils.addTab(win.gBrowser, "https://example.com");
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  await TabStateFlusher.flush(tab.linkedBrowser);

  let group = win.gBrowser.addTabGroup([tab], { label: name });
  // Close the automatically-opened "create tab group" menu.
  win.gBrowser.tabGroupMenu.close();
  return group;
}

/**
 * Returns a basic tab group from makeTabGroup and saves it.
 *
 * @returns {string} the ID of the saved group
 */
async function saveAndCloseGroup(group) {
  let closedObjectsChanged = TestUtils.topicObserved(
    "sessionstore-closed-objects-changed"
  );
  group.documentGlobal.SessionStore.addSavedTabGroup(group);
  await removeTabGroup(group);
  await closedObjectsChanged;
  return group.id;
}

add_task(async function test_tabOverflowContextMenu_deleteOpenTabGroup() {
  await resetTelemetry();

  info("set up an open tab group to be deleted");
  let openGroup = await makeTabGroup();
  let openGroupId = openGroup.id;

  info("delete the open tab group");
  let allTabsMenu = await openTabsMenu();
  let tabGroupButton = allTabsMenu.querySelector(
    `#allTabsMenu-groupsView [data-tab-group-id="${openGroupId}"]`
  );

  let menu = await getContextMenu(
    tabGroupButton,
    "open-tab-group-context-menu"
  );

  menu.querySelector("#open-tab-group-context-menu_delete").click();
  await closeContextMenu(menu);
  await closeTabsMenu();

  await TestUtils.waitForCondition(
    () => !win.gBrowser.tabGroups.includes(openGroup),
    "wait for group to be deleted"
  );

  let tabGroupDeleteEvents = Glean.tabgroup.delete.testGetValue();
  Assert.equal(
    tabGroupDeleteEvents.length,
    1,
    "should have recorded one tabgroup.delete event"
  );

  let [openTabGroupDeleteEvent] = tabGroupDeleteEvents;

  Assert.deepEqual(
    openTabGroupDeleteEvent.extra,
    {
      source: "tab_overflow",
      id: openGroupId,
    },
    "should have recorded the correct source and ID for the open tab group"
  );

  await resetTelemetry();
});

async function waitForReopenRecord() {
  return TestUtils.waitForCondition(() => {
    let tabGroupReopenTelemetry = Glean.tabgroup.reopen.testGetValue();
    return tabGroupReopenTelemetry?.length > 0;
  }, "Waiting for reopen telemetry to populate");
}
function assertReopenEvent({ id, source, layout, type }) {
  let tabGroupReopenEvents = Glean.tabgroup.reopen.testGetValue();
  Assert.equal(
    tabGroupReopenEvents.length,
    1,
    "should have recorded one tabgroup.reopen event"
  );

  let [reopenEvent] = tabGroupReopenEvents;

  Assert.deepEqual(
    reopenEvent.extra,
    {
      id,
      source,
      layout,
      type,
    },
    "should have recorded correct id, source, and layout for reopen event"
  );
}

async function waitForNoActiveGroups() {
  return TestUtils.waitForCondition(
    () => !win.gBrowser.getAllTabGroups().length,
    "waiting for an empty group list"
  );
}

async function doReopenTests(useVerticalTabs) {
  await waitForNoActiveGroups();
  Assert.ok(!win.gBrowser.getAllTabGroups().length, "there are no tab groups");
  Assert.ok(!win.SessionStore.savedGroups.length, "no saved groups");
  let expectedLayout = useVerticalTabs ? "vertical" : "horizontal";
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.revamp", true],
      ["sidebar.verticalTabs", useVerticalTabs],
    ],
  });
  let group = await makeTabGroup("reopen-test");
  let groupId = await saveAndCloseGroup(group);

  info("Restoring from overflow menu");
  await waitForNoActiveGroups();
  let menu = await openTabsMenu();
  let groupItems = menu.querySelectorAll(
    "#allTabsMenu-groupsView .all-tabs-group-action-button"
  );
  Assert.equal(groupItems.length, 1, "1 group in menu");
  let groupButton = groupItems[0];
  Assert.equal(
    groupButton.getAttribute("data-tab-group-id"),
    groupId,
    "Correct group appears in menu"
  );
  groupButton.click();
  await waitForReopenRecord();
  assertReopenEvent({
    id: groupId,
    source: "tab_overflow",
    layout: expectedLayout,
    type: "saved",
  });
  await resetTelemetry();
  await saveAndCloseGroup(win.gBrowser.getTabGroupById(groupId));

  info("restoring saved group via undoClosetab");
  await waitForNoActiveGroups();
  SessionWindowUI.undoCloseTab(win, undefined, win.__SSi);
  await waitForReopenRecord();
  assertReopenEvent({
    id: groupId,
    source: "recent",
    layout: expectedLayout,
    type: "saved",
  });
  await addTabTo(win.gBrowser, "about:blank"); // removed by undoCloseTab
  await saveAndCloseGroup(win.gBrowser.getTabGroupById(groupId));
  await resetTelemetry();

  info("restoring saved group from URLbar suggestion");
  await waitForNoActiveGroups();
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: win,
    waitForFocus,
    value: "reopen-test",
    fireInputEvent: true,
    reopenOnBlur: true,
  });
  let reopenGroupButton = win.gURLBar.panel.querySelector(
    `[data-action^="tabgroup"]`
  );
  Assert.ok(!!reopenGroupButton, "Reopen group action is present in results");
  let closedObjectsChanged = TestUtils.topicObserved(
    "sessionstore-closed-objects-changed"
  );
  await UrlbarTestUtils.promisePopupClose(win, () => {
    EventUtils.synthesizeKey("KEY_Tab", {}, win);
    EventUtils.synthesizeKey("KEY_Enter", {}, win);
  });
  await closedObjectsChanged;
  await waitForReopenRecord();
  assertReopenEvent({
    id: groupId,
    source: "suggest",
    layout: expectedLayout,
    type: "saved",
  });
  await resetTelemetry();

  await TabGroupTestUtils.removeTabGroup(win.gBrowser.getTabGroupById(groupId));
  SessionStore.undoCloseTabGroup(win, groupId, win);
  await waitForReopenRecord();
  assertReopenEvent({
    id: groupId,
    source: "recent",
    layout: expectedLayout,
    type: "deleted",
  });
  await resetTelemetry();
  await TabGroupTestUtils.removeTabGroup(win.gBrowser.getTabGroupById(groupId));
  await SpecialPowers.popPrefEnv();
}

add_task(async function test_reopenSavedGroupTelemetry() {
  info("Perform reopen tests in horizontal tabs mode");
  await doReopenTests(false);
  info("Perform reopen tests in vertical tabs mode");
  await doReopenTests(true);
});

add_task(async function test_tabContextMenu_addTabsToGroup() {
  await resetTelemetry();

  // `tabgroup.add_tab` is disabled by default and enabled by server knobs,
  // so this test needs to enable it manually in order to test it.
  Services.fog.applyServerKnobsConfig(
    JSON.stringify({
      metrics_enabled: {
        "tabgroup.add_tab": true,
      },
    })
  );

  info("set up a tab group to test with");
  let group = await makeTabGroup();
  let groupId = group.id;

  info("create and save a group");
  let savedGroup = await makeTabGroup();
  let savedGroupId = savedGroup.id;
  await TabGroupTestUtils.saveAndCloseTabGroup(savedGroup);

  info("create 8 ungrouped tabs to test with");
  let moreTabs = Array.from({ length: 8 }).map(() =>
    BrowserTestUtils.addTab(win.gBrowser, "https://example.com")
  );
  await TabGroupTestUtils.ensureTabsLoaded(moreTabs);

  info("select first ungrouped tab and multi-select three more tabs");
  win.gBrowser.selectedTab = moreTabs[0];
  moreTabs.slice(1, 4).forEach(tab => win.gBrowser.addToMultiSelectedTabs(tab));

  await TestUtils.waitForCondition(() => {
    return win.gBrowser.multiSelectedTabsCount == 4;
  }, "Wait for Tabbrowser to update the multiselected tab state");

  let menu = await getContextMenu(win.gBrowser.selectedTab, "tabContextMenu");
  let moveTabToGroupItem = win.document.getElementById(
    "context_moveTabToGroup"
  );
  let tabGroupButton = moveTabToGroupItem.querySelector(
    `[tab-group-id="${groupId}"]`
  );
  tabGroupButton.click();
  await closeContextMenu(menu);

  await TestUtils.waitForCondition(() => {
    return Glean.tabgroup.addTab.testGetValue()?.length === 1;
  }, "Wait for a Glean event to be recorded");

  info("Collapse the tab group and add another tab to it");
  await TabGroupTestUtils.toggleCollapsed(group, true);

  menu = await getContextMenu(win.gBrowser.tabs.at(-1), "tabContextMenu");
  moveTabToGroupItem = win.document.getElementById("context_moveTabToGroup");
  tabGroupButton = moveTabToGroupItem.querySelector(
    `[tab-group-id="${groupId}"]`
  );
  Assert.ok(
    tabGroupButton,
    "the open tab group should be available in 'move tab to group'"
  );
  tabGroupButton.click();
  await closeContextMenu(menu);

  await TestUtils.waitForCondition(() => {
    return Glean.tabgroup.addTab.testGetValue()?.length === 2;
  }, "Wait for a Glean event to be recorded");

  info("Add a tab to the saved tab group");
  menu = await getContextMenu(win.gBrowser.tabs.at(-1), "tabContextMenu");
  moveTabToGroupItem = win.document.getElementById(
    "context_moveTabToSavedGroup"
  );
  tabGroupButton = moveTabToGroupItem.querySelector(
    `[tab-group-id="${savedGroupId}"]`
  );
  Assert.ok(
    tabGroupButton,
    "the saved tab group should be available in 'move tab to saved group'"
  );
  tabGroupButton.click();
  await closeContextMenu(menu);

  await TestUtils.waitForCondition(() => {
    return Glean.tabgroup.addTab.testGetValue()?.length === 3;
  }, "Wait for a Glean event to be recorded");

  let [addTabEventExpanded, addTabEventCollapsed, addTabEventSaved] =
    Glean.tabgroup.addTab.testGetValue();
  Assert.deepEqual(
    addTabEventExpanded.extra,
    {
      source: "tab_menu",
      tabs: "4",
      layout: "horizontal",
      group_type: "expanded",
    },
    "should have recorded the correct event metadata"
  );
  Assert.deepEqual(
    addTabEventCollapsed.extra,
    {
      source: "tab_menu",
      tabs: "1",
      layout: "horizontal",
      group_type: "collapsed",
    },
    "should have recorded the correct event metadata"
  );
  Assert.deepEqual(
    addTabEventSaved.extra,
    {
      source: "tab_menu",
      tabs: "1",
      layout: "horizontal",
      group_type: "saved",
    },
    "should have recorded the correct event metadata"
  );

  for (let tab of moreTabs) {
    BrowserTestUtils.removeTab(tab);
  }
  await removeTabGroup(group);

  await resetTelemetry();
});

add_task(async function test_groupInteractions() {
  await resetTelemetry();
  let group = await makeTabGroup();
  const groupId = group.id;

  info("test that collapsing and expanding the group gets counted");
  Assert.ok(!group.collapsed, "new tab group should start expanded");
  const tabGroupCollapseEvent = BrowserTestUtils.waitForEvent(
    win,
    "TabGroupCollapse"
  );
  group.labelElement.click();
  await tabGroupCollapseEvent;
  Assert.equal(
    Glean.tabgroup.groupInteractions.collapse.testGetValue(),
    1,
    "tab group collapse should have been recorded"
  );
  const tabGroupExpandEvent = BrowserTestUtils.waitForEvent(
    win,
    "TabGroupExpand"
  );
  group.labelElement.click();
  await tabGroupExpandEvent;
  Assert.equal(
    Glean.tabgroup.groupInteractions.expand.testGetValue(),
    1,
    "tab group expand should have been recorded"
  );

  info("opening and closing tab group context menu");
  let tabGroupContextMenu = await openTabGroupContextMenu(group);
  await closeContextMenu(tabGroupContextMenu);

  Assert.equal(
    Glean.tabgroup.groupInteractions.rename.testGetValue(),
    null,
    "tab group rename count should not have changed because the name did not change"
  );

  info("opening tab group context menu and inputting tab group name change");
  tabGroupContextMenu = await openTabGroupContextMenu(group);
  let tabGroupNameInput = win.document.getElementById("tab-group-name");
  let inputEvent = BrowserTestUtils.waitForEvent(tabGroupNameInput, "input");
  tabGroupNameInput.value = "test group name";
  tabGroupNameInput.dispatchEvent(
    new InputEvent("input", { data: "test group name" })
  );
  await inputEvent;
  await closeContextMenu(tabGroupContextMenu);

  await TestUtils.waitForCondition(
    () => Glean.tabgroup.groupInteractions.rename.testGetValue() != null,
    "waiting for `rename` metric to be set"
  );
  Assert.equal(
    Glean.tabgroup.groupInteractions.rename.testGetValue(),
    1,
    "tab group rename count should have increased because the name changed"
  );

  Assert.equal(
    Glean.tabgroup.groupInteractions.change_color.testGetValue(),
    null,
    "tab group change_color count should start unset"
  );
  info("opening tab group context menu and changing tab group color");
  tabGroupContextMenu = await openTabGroupContextMenu(group);
  win.document.getElementById("tab-group-editor-swatch-green").click();
  await closeContextMenu(tabGroupContextMenu);
  Assert.equal(
    Glean.tabgroup.groupInteractions.change_color.testGetValue(),
    1,
    "tab group change_color count should have increased because the color changed"
  );

  Assert.equal(
    Glean.tabgroup.groupInteractions.delete.testGetValue(),
    null,
    "tab group delete count should start unset"
  );
  info("opening tab group context menu and deleting it");
  tabGroupContextMenu = await openTabGroupContextMenu(group);
  let tabGroupRemoved = BrowserTestUtils.waitForEvent(group, "TabGroupRemoved");
  win.document.getElementById("tabGroupEditor_deleteGroup").click();
  await tabGroupRemoved;
  Assert.equal(
    Glean.tabgroup.groupInteractions.delete.testGetValue(),
    1,
    "tab group delete count should have increased because a tab group was deleted"
  );

  Assert.equal(
    Glean.tabgroup.groupInteractions.open_recent.testGetValue(),
    null,
    "tab group open_recent count should start unset"
  );
  info(
    "undoing last closed tab group, which should restore the just-deleted tab group"
  );
  SessionStore.undoCloseTabGroup(win, groupId, win);
  group = win.gBrowser.getTabGroupById(groupId);
  Assert.ok(group, "group should have been restored");
  Assert.equal(
    Glean.tabgroup.groupInteractions.open_recent.testGetValue(),
    1,
    "tab group open_recent count should have increased because a recently closed tab group was reopened"
  );

  Assert.equal(
    Glean.tabgroup.groupInteractions.save.testGetValue(),
    null,
    "tab group save count should start unset"
  );
  info("opening tab group context menu and saving it");
  tabGroupContextMenu = await openTabGroupContextMenu(group);
  win.document.getElementById("tabGroupEditor_saveAndCloseGroup").click();
  await TestUtils.waitForCondition(
    () => Glean.tabgroup.groupInteractions.save.testGetValue() != null,
    "waiting for `save` metric to be set"
  );
  Assert.equal(
    Glean.tabgroup.groupInteractions.save.testGetValue(),
    1,
    "tab group save count should have increased because a tab group was saved"
  );

  info("reopen saved tab group virtually from tab overflow menu");

  group = SessionStore.openSavedTabGroup(groupId, win, {
    source: "tab_overflow",
  });
  let groupTabs = group.tabs;
  Assert.equal(
    Glean.tabgroup.groupInteractions.open_tabmenu.testGetValue(),
    1,
    "`open_tabmenu` metric should increment"
  );

  Assert.equal(
    Glean.tabgroup.groupInteractions.ungroup.testGetValue(),
    null,
    "tab group ungroup count should start unset"
  );
  info("opening tab group context menu and ungrouping it");

  tabGroupContextMenu = await openTabGroupContextMenu(group);

  tabGroupRemoved = BrowserTestUtils.waitForEvent(group, "TabGroupRemoved");
  win.document.getElementById("tabGroupEditor_ungroupTabs").click();
  await tabGroupRemoved;
  Assert.equal(
    Glean.tabgroup.groupInteractions.ungroup.testGetValue(),
    1,
    "tab group ungroup count should have increased because a tab group was ungrouped"
  );
  const ungroupEvents = Glean.tabgroup.ungroup.testGetValue();
  Assert.ok(ungroupEvents, "an `ungroup` event should have been recorded");
  Assert.deepEqual(
    ungroupEvents[0].extra,
    { source: "tab_group" },
    "ungroup event should have come from the tab group context menu"
  );

  for (const tab of groupTabs) {
    BrowserTestUtils.removeTab(tab);
  }

  await resetTelemetry();
});

add_task(async function test_groupInteractions_copyAllLinks() {
  await resetTelemetry();

  let tab = BrowserTestUtils.addTab(win.gBrowser, "https://example.com");
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  let group = win.gBrowser.addTabGroup([tab]);
  win.gBrowser.tabGroupMenu.close();

  Assert.equal(
    Glean.tabgroup.groupInteractions.copy_all_links.testGetValue(),
    null,
    "tab group copy_all_links count should start unset"
  );

  let tabGroupContextMenu = await openTabGroupContextMenu(group);
  let panelHidden = BrowserTestUtils.waitForPopupEvent(
    tabGroupContextMenu,
    "hidden"
  );
  win.document.getElementById("tabGroupEditor_copyAllLinks").click();
  await panelHidden;

  Assert.equal(
    Glean.tabgroup.groupInteractions.copy_all_links.testGetValue(),
    1,
    "tab group copy_all_links count should have increased"
  );

  await removeTabGroup(group);

  await resetTelemetry();
});

add_task(async function test_cancelTabGroupCreation_ungroupTabsEvent() {
  await resetTelemetry();

  let tab = BrowserTestUtils.addTab(win.gBrowser, "https://example.com");
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);

  let tabGroupCreateByUser = BrowserTestUtils.waitForEvent(
    win.gBrowser.tabContainer,
    "TabGroupCreateByUser"
  );
  let tabGroupContextOpen = BrowserTestUtils.waitForPopupEvent(
    win.gBrowser.tabGroupMenu,
    "shown"
  );
  win.gBrowser.addTabGroup([tab], {
    metricsContext: win.gBrowser.TabMetrics.userTriggeredContext("test-source"),
  });
  await Promise.all([tabGroupCreateByUser, tabGroupContextOpen]);

  Assert.ok(
    win.gBrowser.tabGroupMenu.createMode,
    "tab group context menu should be in create mode"
  );

  info("hit Escape key in order to cancel/undo tab group creation");
  let tabGroupContextClosed = BrowserTestUtils.waitForPopupEvent(
    win.gBrowser.tabGroupMenu,
    "hidden"
  );
  EventUtils.synthesizeKey("KEY_Escape", {}, win);
  await tabGroupContextClosed;

  await TestUtils.waitForCondition(() => {
    return Glean.tabgroup.ungroup.testGetValue() != null;
  }, "wait until an ungroup event is recorded");

  let [ungroupEvent] = Glean.tabgroup.ungroup.testGetValue();
  Assert.deepEqual(
    ungroupEvent.extra,
    { source: "cancel_create" },
    "ungroup event should be from canceling group creation"
  );
  Assert.equal(
    Glean.tabgroup.groupInteractions.ungroup.testGetValue(),
    null,
    "tab group interactions ungroup count should not be increased when canceling tab group creation"
  );

  await BrowserTestUtils.removeTab(tab);

  await resetTelemetry();
});

/* Test that no "add tab to group" events are fired when moving tabs
 * but the tab does not enter or leave a group */
add_task(async function test_noGroupEventWhenNotMovingToGroup() {
  await resetTelemetry();

  // `tabgroup.add_tab` is disabled by default and enabled by server knobs,
  // so this test needs to enable it manually in order to test it.
  Services.fog.applyServerKnobsConfig(
    JSON.stringify({
      metrics_enabled: {
        "tabgroup.add_tab": true,
      },
    })
  );

  let tab = await addTabTo(win.gBrowser, "about:blank");
  Assert.equal(
    Glean.tabgroup.addTab.testGetValue(),
    null,
    "Sanity check: no add tab to group events recorded"
  );

  win.gBrowser.moveTabTo(tab, {
    tabIndex: 0,
    metricsContext: win.gBrowser.TabMetrics.userTriggeredContext(),
  });

  /* eslint-disable mozilla/no-arbitrary-setTimeout */
  await new Promise(r => setTimeout(r, 300));

  Assert.equal(
    Glean.tabgroup.addTab.testGetValue(),
    null,
    "No add tab to group event recorded when moving tabs not within a group"
  );

  BrowserTestUtils.removeTab(tab);

  await resetTelemetry();
});
