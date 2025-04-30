/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { TabStateFlusher } = ChromeUtils.importESModule(
  "resource:///modules/sessionstore/TabStateFlusher.sys.mjs"
);

const { UrlbarTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlbarTestUtils.sys.mjs"
);

let resetTelemetry = async () => {
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
};

/** @type {Window} */
let win;

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
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

  let group1 = win.gBrowser.addTabGroup([group1tab], {
    isUserTriggered: true,
    telemetryUserCreateSource: "test-source",
  });
  win.gBrowser.tabGroupMenu.close();

  await BrowserTestUtils.waitForCondition(() => {
    tabGroupCreateTelemetry = Glean.tabgroup.createGroup.testGetValue();
    return (
      tabGroupCreateTelemetry?.length == 1 &&
      Glean.tabgroup.tabCountInGroups.inside.testGetValue() !== null &&
      Glean.tabgroup.tabsPerActiveGroup.average.testGetValue() !== null
    );
  }, "Wait for createGroup and at least one metric from the tabCountInGroups and tabsPerActiveGroup to be set");

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
    isUserTriggered: true,
    telemetryUserCreateSource: "test-source",
  });
  win.gBrowser.tabGroupMenu.close();

  await BrowserTestUtils.waitForCondition(() => {
    return (
      Glean.tabgroup.tabCountInGroups.inside.testGetValue() !== null &&
      Glean.tabgroup.tabsPerActiveGroup.average.testGetValue() !== null
    );
  }, "Wait for at least one metric from the tabCountInGroups and tabsPerActiveGroup to be set after adding a new tab group");

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

  await BrowserTestUtils.waitForCondition(() => {
    return (
      Glean.tabgroup.tabCountInGroups.inside.testGetValue() !== null &&
      Glean.tabgroup.tabsPerActiveGroup.average.testGetValue() !== null
    );
  }, "Wait for at least one metric from the tabCountInGroups and tabsPerActiveGroup to be set after modifying a tab group");

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

  await BrowserTestUtils.waitForCondition(() => {
    return Glean.tabgroup.activeGroups.collapsed.testGetValue() !== null;
  }, "Wait for the activeGroups metric to be set after collapsing a tab group");

  Assert.equal(
    Glean.tabgroup.activeGroups.collapsed.testGetValue(),
    1,
    "activeGroups.collapsed has correct value after collapsing a tab group"
  );
  Assert.equal(
    Glean.tabgroup.activeGroups.expanded.testGetValue(),
    1,
    "activeGroups.collapsed has correct value after collapsing a tab group"
  );

  await resetTelemetry();

  await removeTabGroup(group1);
  await removeTabGroup(group2);
});

add_task(async function test_tabGroupTelemetrySaveGroup() {
  let tabGroupSaveTelemetry;

  await resetTelemetry();

  let group1tab = BrowserTestUtils.addTab(win.gBrowser, "https://example.com");
  await BrowserTestUtils.browserLoaded(group1tab.linkedBrowser);
  let group1 = win.gBrowser.addTabGroup([group1tab]);
  group1.saveAndClose();

  await BrowserTestUtils.waitForCondition(() => {
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

  await resetTelemetry();

  let group2tab = BrowserTestUtils.addTab(win.gBrowser, "https://example.com");
  await BrowserTestUtils.browserLoaded(group2tab.linkedBrowser);
  let group2 = gBrowser.addTabGroup([group2tab]);
  group2.saveAndClose({ isUserTriggered: true });

  await BrowserTestUtils.waitForCondition(() => {
    tabGroupSaveTelemetry = Glean.tabgroup.save.testGetValue();
    return tabGroupSaveTelemetry?.length == 1;
  }, "Wait for tabgroup.save event after tab group save with explicit user event");

  Assert.deepEqual(
    tabGroupSaveTelemetry[0].extra,
    {
      user_triggered: "true",
      id: group2.id,
    },
    "tabgroup.save event extra_keys has correct values after tab group save by explicit user event"
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

  await BrowserTestUtils.waitForCondition(() => {
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
    tabGroup.ownerGlobal
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
  group.ownerGlobal.SessionStore.addSavedTabGroup(group);
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
  return BrowserTestUtils.waitForCondition(() => {
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
  return BrowserTestUtils.waitForCondition(
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
  undoCloseTab(undefined, win.__SSi);
  await waitForReopenRecord();
  assertReopenEvent({
    id: groupId,
    source: "recent",
    layout: expectedLayout,
    type: "saved",
  });
  await addTab("about:blank"); // removed by undoCloseTab
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

  await win.gBrowser.removeTabGroup(win.gBrowser.getTabGroupById(groupId));
  await resetTelemetry();
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

  info("create 8 ungrouped tabs to test with");
  let moreTabs = Array.from({ length: 8 }).map(() =>
    BrowserTestUtils.addTab(win.gBrowser, "https://example.com")
  );

  info("select first ungrouped tab and multi-select three more tabs");
  win.gBrowser.selectedTab = moreTabs[0];
  moreTabs.slice(1, 4).forEach(tab => win.gBrowser.addToMultiSelectedTabs(tab));

  await BrowserTestUtils.waitForCondition(() => {
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

  await BrowserTestUtils.waitForCondition(() => {
    return Glean.tabgroup.addTab.testGetValue() !== null;
  }, "Wait for a Glean event to be recorded");

  let [addTabEvent] = Glean.tabgroup.addTab.testGetValue();
  Assert.deepEqual(
    addTabEvent.extra,
    {
      source: "tab_menu",
      tabs: "4",
      layout: "horizontal",
    },
    "should have recorded the correct event metadata"
  );

  for (let tab of moreTabs) {
    BrowserTestUtils.removeTab(tab);
  }
  await removeTabGroup(group);

  await resetTelemetry();
});

add_task(async function test_tabInteractions() {
  let assertMetricEmpty = async metricName => {
    Assert.equal(
      Glean.tabgroup.tabInteractions[metricName].testGetValue(),
      null,
      `tab_interactions.${metricName} starts empty`
    );
  };

  let assertOneMetricFoundFor = async metricName => {
    await BrowserTestUtils.waitForCondition(() => {
      return Glean.tabgroup.tabInteractions[metricName].testGetValue() !== null;
    }, `Wait for tab_interactions.${metricName} to be recorded`);
    Assert.equal(
      Glean.tabgroup.tabInteractions[metricName].testGetValue(),
      1,
      `tab_interactions.${metricName} was recorded`
    );
  };

  let initialTab = win.gBrowser.tabs[0];

  await resetTelemetry();
  let group = await makeTabGroup();

  info(
    "Test that selecting a tab in a group records tab_interactions.activate"
  );
  await assertMetricEmpty("activate");
  const tabSelectEvent = BrowserTestUtils.waitForEvent(win, "TabSelect");
  win.gBrowser.selectTabAtIndex(1);
  await tabSelectEvent;
  await assertOneMetricFoundFor("activate");

  info(
    "Test that moving an existing tab into a tab group records tab_interactions.add"
  );
  let tab1 = BrowserTestUtils.addTab(win.gBrowser, "https://example.com");
  await assertMetricEmpty("add");
  win.gBrowser.moveTabToGroup(tab1, group, { isUserTriggered: true });
  await assertOneMetricFoundFor("add");

  info(
    "Test that adding a new tab to a tab group records tab_interactions.new"
  );
  await assertMetricEmpty("new");
  BrowserTestUtils.addTab(win.gBrowser, "https://example.com", {
    tabGroup: group,
  });
  await assertOneMetricFoundFor("new");

  info("Test that moving a tab within a group calls tab_interactions.reorder");
  await assertMetricEmpty("reorder");
  win.gBrowser.moveTabTo(group.tabs[0], { tabIndex: 3, isUserTriggered: true });
  await assertOneMetricFoundFor("reorder");

  info(
    "Test that duplicating a tab within a group calls tab_interactions.duplicate"
  );
  await assertMetricEmpty("duplicate");
  win.gBrowser.duplicateTab(group.tabs[0], true, { index: 2 });
  await assertOneMetricFoundFor("duplicate");

  info(
    "Test that closing a tab using the tab's close button calls tab_interactions.close_tabstrip"
  );
  await assertMetricEmpty("close_tabstrip");
  group.tabs.at(-1).querySelector(".tab-close-button").click();
  await assertOneMetricFoundFor("close_tabstrip");

  info(
    "Test that closing a tab from the tab overflow menu calls tab_interactions.close_tabmenu"
  );
  await openTabsMenu();
  await assertMetricEmpty("close_tabmenu");
  win.document
    .querySelector(".all-tabs-item.grouped .all-tabs-close-button")
    .click();
  await assertOneMetricFoundFor("close_tabmenu");
  await closeTabsMenu();

  info(
    "Test that moving a tab out of a tab group calls tab_interactions.remove_same_window"
  );
  await assertMetricEmpty("remove_same_window");
  win.gBrowser.moveTabTo(group.tabs[0], { tabIndex: 0, isUserTriggered: true });
  await assertOneMetricFoundFor("remove_same_window");

  info(
    "Test that moving a tab out of a tab group and into a different (existing) window calls tab_interactions.remove_other_window"
  );
  await assertMetricEmpty("remove_other_window");
  let tab2 = BrowserTestUtils.addTab(win.gBrowser, "https://example.com");
  win.gBrowser.moveTabToGroup(tab2, group, { isUserTriggered: true });
  let newWin = await BrowserTestUtils.openNewBrowserWindow();
  newWin.gBrowser.adoptTab(tab2);
  await assertOneMetricFoundFor("remove_other_window");
  await BrowserTestUtils.closeWindow(newWin);

  info(
    "Test that moving a tab out of a tab group and into a different (new) window calls tab_interactions.remove_new_window"
  );
  await assertMetricEmpty("remove_new_window");
  let newWindowPromise = BrowserTestUtils.waitForNewWindow();
  await EventUtils.synthesizePlainDragAndDrop({
    srcElement: group.tabs[0],
    srcWindow: win,
    destElement: null,
    // don't move horizontally because that could cause a tab move
    // animation, and there's code to prevent a tab detaching if
    // the dragged tab is released while the animation is running.
    stepX: 0,
    stepY: 100,
  });
  newWin = await newWindowPromise;
  await assertOneMetricFoundFor("remove_new_window");
  await BrowserTestUtils.closeWindow(newWin);

  win.gBrowser.removeAllTabsBut(initialTab);
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

  await resetTelemetry();
});
