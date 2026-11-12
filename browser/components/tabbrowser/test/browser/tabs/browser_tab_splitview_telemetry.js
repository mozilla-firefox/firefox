/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

async function resetTelemetry() {
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
}

async function tabIsInSplitView(tab) {
  const panel = document.getElementById(tab.linkedPanel);
  info("Waiting in tabIsInSplitView");
  await BrowserTestUtils.waitForMutationCondition(
    panel,
    { attributes: true },
    () => panel.classList.contains("split-view-panel")
  );
}

function navigateTab(tab, url) {
  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, url);
  return BrowserTestUtils.browserLoaded(tab.linkedBrowser);
}

async function openSplitViewIconMenu() {
  const urlbarButton = document.getElementById("split-view-button");
  await BrowserTestUtils.waitForMutationCondition(
    urlbarButton,
    { attributes: true, attributeFilter: ["hidden"] },
    () => BrowserTestUtils.isVisible(urlbarButton)
  );
  const menu = document.getElementById("split-view-menu");
  const promiseMenuShown = BrowserTestUtils.waitForPopupEvent(menu, "shown");
  EventUtils.synthesizeMouseAtCenter(urlbarButton, {});
  await promiseMenuShown;
  return menu;
}

async function openSplitViewFooterMenu(panel) {
  const footerMenu = document.getElementById("split-view-menu");
  const promiseShown = BrowserTestUtils.waitForPopupEvent(footerMenu, "shown");
  const footer = panel.querySelector("address-footer");
  AccessibilityUtils.setEnv({ focusableRule: false });
  EventUtils.synthesizeMouseAtCenter(footer.menuButtonElement, {});
  AccessibilityUtils.resetEnv();
  await promiseShown;
  return footerMenu;
}

async function closeSplitViewMenu(menu) {
  const menuHidden = BrowserTestUtils.waitForPopupEvent(menu, "hidden");
  menu.hidePopup();
  await menuHidden;
}

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.splitView.enabled", true]],
  });
});

registerCleanupFunction(async () => {
  await resetTelemetry();
});

add_task(async function test_splitview_uri_count_telemetry() {
  await resetTelemetry();
  // Create 4 tabs with different URLs
  const tabs = await Promise.all(
    [
      "https://example.com/start1",
      "https://example.com/start2",
      "https://example.org/start3",
      "about:blank",
    ].map(async url => {
      const tab = BrowserTestUtils.addTab(gBrowser, url);
      if (url !== "about:blank") {
        await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
      }
      return tab;
    })
  );
  await BrowserTestUtils.switchTab(gBrowser, tabs[0]);
  await Services.fog.testFlushAllChildren();

  // Verify telemetry starts undefined
  Assert.equal(
    Glean.splitview.uriCount["1"].testGetValue(),
    undefined,
    "URI count for left side (label '1') starts undefined"
  );
  Assert.equal(
    Glean.splitview.uriCount["2"].testGetValue(),
    undefined,
    "URI count for right side (label '2') starts undefined"
  );

  // Create split view (tab 0 is left/"1", tab 1 is right/"2")
  const splitView1 = gBrowser.addTabSplitView([tabs[0], tabs[1]], {
    id: 11,
  });
  await tabIsInSplitView(tabs[0]);
  await tabIsInSplitView(tabs[1]);
  Assert.ok(splitView1.splitViewId, "Split view created successfully");
  Assert.equal(splitView1.tabs.length, 2, "Split view has 2 tabs");

  await Services.fog.testFlushAllChildren();
  Assert.equal(
    Glean.splitview.uriCount["1"].testGetValue(),
    1,
    "URI count for left side (label '1') should be 1 after initial splitview creation"
  );
  Assert.equal(
    Glean.splitview.uriCount["2"].testGetValue(),
    1,
    "URI count for right side (label '2') should be 1 after initial splitview creation"
  );

  // Navigate the first (left-side) tab
  await navigateTab(tabs[0], "https://example.org/page1");
  await Services.fog.testFlushAllChildren();
  Assert.equal(
    Glean.splitview.uriCount["1"].testGetValue(),
    2,
    "URI count for left side (label '1') should increment to 2"
  );
  Assert.equal(
    Glean.splitview.uriCount["2"].testGetValue(),
    1,
    "URI count for right side should still be 1"
  );

  // Navigate the right-side tab
  await navigateTab(tabs[1], "https://example.com/page2");
  await Services.fog.testFlushAllChildren();
  Assert.equal(
    Glean.splitview.uriCount["1"].testGetValue(),
    2,
    "URI count for left side (label '1') should still be 2"
  );
  Assert.equal(
    Glean.splitview.uriCount["2"].testGetValue(),
    2,
    "URI count for right side (label '2') should increment to 2"
  );

  // Create a split view from the other 2 tabs
  info("Create a splitview from tabs 3 and 4");
  await BrowserTestUtils.switchTab(gBrowser, tabs[3]);
  const splitView2 = gBrowser.addTabSplitView([tabs[2], tabs[3]], {
    id: 21,
  });
  await tabIsInSplitView(tabs[2]);
  await tabIsInSplitView(tabs[3]);
  await Services.fog.testFlushAllChildren();

  Assert.equal(
    Glean.splitview.uriCount["1"].testGetValue(),
    3,
    "URI count for left side (label '1') should now be 3 after 2nd splitview creation"
  );
  Assert.equal(
    Glean.splitview.uriCount["2"].testGetValue(),
    2,
    "URI count for right side (label '2') should remain at 2 as only about:blank was loaded"
  );

  // Navigate the right-side tab in 2nd splitview
  await navigateTab(tabs[3], "https://example.com/page2");
  await Services.fog.testFlushAllChildren();
  Assert.equal(
    Glean.splitview.uriCount["1"].testGetValue(),
    3,
    "URI count for left side (label '1') should still be 3"
  );
  Assert.equal(
    Glean.splitview.uriCount["2"].testGetValue(),
    3,
    "URI count for right side (label '2') should increment to 3"
  );

  // Cleanup
  splitView1.close();
  splitView2.close();
  BrowserTestUtils.removeTab(tabs[0]);
  BrowserTestUtils.removeTab(tabs[1]);
  BrowserTestUtils.removeTab(tabs[2]);
  BrowserTestUtils.removeTab(tabs[3]);
});

add_task(async function test_splitview_start_event_menu_open() {
  await resetTelemetry();

  const tab1 = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  const tab2 = BrowserTestUtils.addTab(gBrowser, "https://example.org");
  await Promise.all([
    BrowserTestUtils.browserLoaded(tab1.linkedBrowser),
    BrowserTestUtils.browserLoaded(tab2.linkedBrowser),
  ]);
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  const events = Glean.splitview.start.testGetValue();
  Assert.equal(events, undefined, "No start events recorded initially");

  // Multiselect tab1 and tab2
  gBrowser.addToMultiSelectedTabs(tab1);
  gBrowser.addToMultiSelectedTabs(tab2);
  Assert.ok(tab1.multiselected, "tab1 is multiselected");
  Assert.ok(tab2.multiselected, "tab2 is multiselected");

  // Open context menu and click "Open in Split View"
  const menu = await openTabContextMenu(tab2);
  const moveToSplitViewItem = document.getElementById(
    "context_moveTabToSplitView"
  );
  await BrowserTestUtils.waitForMutationCondition(
    moveToSplitViewItem,
    { attributes: true },
    () => !moveToSplitViewItem.hidden && !moveToSplitViewItem.disabled
  );
  moveToSplitViewItem.click();
  await closeTabContextMenu(menu);

  await tabIsInSplitView(tab1);
  await tabIsInSplitView(tab2);

  const splitView = tab1.splitview;
  Assert.ok(splitView, "Split view was created");

  await Services.fog.testFlushAllChildren();

  const recordedEvents = Glean.splitview.start.testGetValue();
  Assert.equal(recordedEvents.length, 1, "One start event recorded");
  Assert.equal(
    recordedEvents[0].extra.trigger,
    "menu_open",
    "Trigger is menu_open"
  );
  Assert.equal(
    recordedEvents[0].extra.tab_layout,
    "horizontal",
    "Tab layout is horizontal"
  );
  Assert.equal(
    recordedEvents[0].extra.tabgroup,
    "none",
    "Tabgroup is none (no groups)"
  );

  // Clean up without recording telemetry
  if (splitView) {
    splitView.unsplitTabs();
  }
  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
});

add_task(async function test_splitview_start_event_menu_add() {
  await resetTelemetry();

  const tab1 = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  await BrowserTestUtils.browserLoaded(tab1.linkedBrowser);
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  // Open context menu with only tab1 selected and click "Add to Split View"
  const menu = await openTabContextMenu(tab1);
  const moveToSplitViewItem = document.getElementById(
    "context_moveTabToSplitView"
  );
  await BrowserTestUtils.waitForMutationCondition(
    moveToSplitViewItem,
    { attributes: true },
    () => !moveToSplitViewItem.hidden && !moveToSplitViewItem.disabled
  );
  moveToSplitViewItem.click();
  await closeTabContextMenu(menu);

  // This should create a split view with tab1 and a new "about:opentabs" tab
  await TestUtils.waitForCondition(
    () => tab1.splitview,
    "Waiting for split view to be created"
  );

  const splitView = tab1.splitview;
  Assert.ok(splitView, "Split view was created");
  Assert.equal(splitView.tabs.length, 2, "Split view has 2 tabs");

  const newTab = splitView.tabs.find(t => t !== tab1);
  await tabIsInSplitView(tab1);
  await tabIsInSplitView(newTab);

  await Services.fog.testFlushAllChildren();

  const recordedEvents = Glean.splitview.start.testGetValue();
  Assert.equal(recordedEvents.length, 1, "One start event recorded");
  Assert.equal(
    recordedEvents[0].extra.trigger,
    "menu_add",
    "Trigger is menu_add"
  );
  Assert.equal(recordedEvents[0].extra.tabgroup, "none", "Tabgroup is none");

  // Clean up without recording telemetry
  if (splitView) {
    splitView.unsplitTabs();
  }
  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(newTab);
});

add_task(async function test_splitview_start_event_tabgroup_main() {
  await resetTelemetry();

  const tab1 = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  const tab2 = BrowserTestUtils.addTab(gBrowser, "https://example.org");
  await Promise.all([
    BrowserTestUtils.browserLoaded(tab1.linkedBrowser),
    BrowserTestUtils.browserLoaded(tab2.linkedBrowser),
  ]);

  const group = gBrowser.addTabGroup([tab1]);
  Assert.ok(tab1.group, "Tab1 is in a group");
  Assert.ok(!tab2.group, "Tab2 is not in a group");

  await BrowserTestUtils.switchTab(gBrowser, tab1);

  // Multiselect tab1 and tab2
  gBrowser.addToMultiSelectedTabs(tab1);
  gBrowser.addToMultiSelectedTabs(tab2);

  // Open context menu and click "Open in Split View"
  const menu = await openTabContextMenu(tab2);
  const moveToSplitViewItem = document.getElementById(
    "context_moveTabToSplitView"
  );
  await BrowserTestUtils.waitForMutationCondition(
    moveToSplitViewItem,
    { attributes: true },
    () => !moveToSplitViewItem.hidden && !moveToSplitViewItem.disabled
  );
  moveToSplitViewItem.click();
  await closeTabContextMenu(menu);

  await tabIsInSplitView(tab1);
  await tabIsInSplitView(tab2);

  const splitView = tab1.splitview;
  Assert.ok(splitView, "Split view was created");

  await Services.fog.testFlushAllChildren();

  const recordedEvents = Glean.splitview.start.testGetValue();
  Assert.equal(recordedEvents.length, 1, "One start event recorded");
  Assert.equal(
    recordedEvents[0].extra.tabgroup,
    "main",
    "Tabgroup is main (primary tab in group)"
  );

  // Clean up without recording telemetry
  if (splitView) {
    splitView.unsplitTabs();
  }
  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
  if (group) {
    group.remove();
  }
});

add_task(async function test_splitview_start_event_tabgroup_other() {
  await resetTelemetry();

  const tab1 = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  const tab2 = BrowserTestUtils.addTab(gBrowser, "https://example.org");
  await Promise.all([
    BrowserTestUtils.browserLoaded(tab1.linkedBrowser),
    BrowserTestUtils.browserLoaded(tab2.linkedBrowser),
  ]);

  const group = gBrowser.addTabGroup([tab2]);
  Assert.ok(!tab1.group, "Tab1 is not in a group");
  Assert.ok(tab2.group, "Tab2 is in a group");

  await BrowserTestUtils.switchTab(gBrowser, tab1);

  // Multiselect tab1 and tab2
  gBrowser.addToMultiSelectedTabs(tab1);
  gBrowser.addToMultiSelectedTabs(tab2);

  // Open context menu and click "Open in Split View"
  const menu = await openTabContextMenu(tab2);
  const moveToSplitViewItem = document.getElementById(
    "context_moveTabToSplitView"
  );
  await BrowserTestUtils.waitForMutationCondition(
    moveToSplitViewItem,
    { attributes: true },
    () => !moveToSplitViewItem.hidden && !moveToSplitViewItem.disabled
  );
  moveToSplitViewItem.click();
  await closeTabContextMenu(menu);

  await tabIsInSplitView(tab1);
  await tabIsInSplitView(tab2);

  const splitView = tab1.splitview;
  Assert.ok(splitView, "Split view was created");

  await Services.fog.testFlushAllChildren();

  const recordedEvents = Glean.splitview.start.testGetValue();
  Assert.equal(recordedEvents.length, 1, "One start event recorded");
  Assert.equal(
    recordedEvents[0].extra.tabgroup,
    "other",
    "Tabgroup is other (secondary tab in group)"
  );

  // Clean up without recording telemetry
  if (splitView) {
    splitView.unsplitTabs();
  }
  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
  if (group) {
    group.remove();
  }
});

add_task(async function test_splitview_start_event_tabgroup_both_same() {
  await resetTelemetry();

  const tab1 = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  const tab2 = BrowserTestUtils.addTab(gBrowser, "https://example.org");
  await Promise.all([
    BrowserTestUtils.browserLoaded(tab1.linkedBrowser),
    BrowserTestUtils.browserLoaded(tab2.linkedBrowser),
  ]);

  const group = gBrowser.addTabGroup([tab1, tab2]);
  Assert.ok(tab1.group, "Tab1 is in a group");
  Assert.ok(tab2.group, "Tab2 is in a group");
  Assert.equal(tab1.group, tab2.group, "Both tabs are in the same group");

  await BrowserTestUtils.switchTab(gBrowser, tab1);

  // Multiselect tab1 and tab2
  gBrowser.addToMultiSelectedTabs(tab1);
  gBrowser.addToMultiSelectedTabs(tab2);

  // Open context menu and click "Open in Split View"
  const menu = await openTabContextMenu(tab2);
  const moveToSplitViewItem = document.getElementById(
    "context_moveTabToSplitView"
  );
  await BrowserTestUtils.waitForMutationCondition(
    moveToSplitViewItem,
    { attributes: true },
    () => !moveToSplitViewItem.hidden && !moveToSplitViewItem.disabled
  );
  moveToSplitViewItem.click();
  await closeTabContextMenu(menu);

  await tabIsInSplitView(tab1);
  await tabIsInSplitView(tab2);

  const splitView = tab1.splitview;
  Assert.ok(splitView, "Split view was created");

  await Services.fog.testFlushAllChildren();

  const recordedEvents = Glean.splitview.start.testGetValue();
  Assert.equal(recordedEvents.length, 1, "One start event recorded");
  Assert.equal(
    recordedEvents[0].extra.tabgroup,
    "both_same",
    "Tabgroup is both_same (both tabs in same group)"
  );

  // Clean up without recording telemetry
  if (splitView) {
    splitView.unsplitTabs();
  }
  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
  if (group) {
    group.remove();
  }
});

add_task(async function test_splitview_start_event_tabgroup_both_different() {
  await resetTelemetry();

  const tab1 = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  const tab2 = BrowserTestUtils.addTab(gBrowser, "https://example.org");
  await Promise.all([
    BrowserTestUtils.browserLoaded(tab1.linkedBrowser),
    BrowserTestUtils.browserLoaded(tab2.linkedBrowser),
  ]);

  const group1 = gBrowser.addTabGroup([tab1]);
  const group2 = gBrowser.addTabGroup([tab2]);
  Assert.ok(tab1.group, "Tab1 is in a group");
  Assert.ok(tab2.group, "Tab2 is in a group");
  Assert.notEqual(tab1.group, tab2.group, "Tabs are in different groups");

  await BrowserTestUtils.switchTab(gBrowser, tab1);

  // Multiselect tab1 and tab2
  gBrowser.addToMultiSelectedTabs(tab1);
  gBrowser.addToMultiSelectedTabs(tab2);

  // Open context menu and click "Open in Split View"
  const menu = await openTabContextMenu(tab2);
  const moveToSplitViewItem = document.getElementById(
    "context_moveTabToSplitView"
  );
  await BrowserTestUtils.waitForMutationCondition(
    moveToSplitViewItem,
    { attributes: true },
    () => !moveToSplitViewItem.hidden && !moveToSplitViewItem.disabled
  );
  moveToSplitViewItem.click();
  await closeTabContextMenu(menu);

  await tabIsInSplitView(tab1);
  await tabIsInSplitView(tab2);

  const splitView = tab1.splitview;
  Assert.ok(splitView, "Split view was created");

  await Services.fog.testFlushAllChildren();

  const recordedEvents = Glean.splitview.start.testGetValue();
  Assert.equal(recordedEvents.length, 1, "One start event recorded");
  Assert.equal(
    recordedEvents[0].extra.tabgroup,
    "both_different",
    "Tabgroup is both_different (tabs in different groups)"
  );

  // Clean up without recording telemetry
  if (splitView) {
    splitView.unsplitTabs();
  }
  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
  if (group1) {
    group1.remove();
  }
  if (group2) {
    group2.remove();
  }
});

add_task(async function test_splitview_end_event_menu_separate() {
  await resetTelemetry();

  const tab1 = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  const tab2 = BrowserTestUtils.addTab(gBrowser, "https://example.org");
  await Promise.all([
    BrowserTestUtils.browserLoaded(tab1.linkedBrowser),
    BrowserTestUtils.browserLoaded(tab2.linkedBrowser),
  ]);
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  // Multiselect and create split view via menu
  gBrowser.addToMultiSelectedTabs(tab1);
  gBrowser.addToMultiSelectedTabs(tab2);
  const createMenu = await openTabContextMenu(tab2);
  const moveToSplitViewItem = document.getElementById(
    "context_moveTabToSplitView"
  );
  await BrowserTestUtils.waitForMutationCondition(
    moveToSplitViewItem,
    { attributes: true },
    () => !moveToSplitViewItem.hidden && !moveToSplitViewItem.disabled
  );
  moveToSplitViewItem.click();
  await closeTabContextMenu(createMenu);

  await tabIsInSplitView(tab1);
  await tabIsInSplitView(tab2);

  await resetTelemetry();

  const events = Glean.splitview.end.testGetValue();
  Assert.equal(events, undefined, "No end events recorded initially");

  // Open context menu on split view tab and click "Separate Split View"
  const menu = await openTabContextMenu(tab1);
  const separateItem = document.getElementById("context_separateSplitView");
  await BrowserTestUtils.waitForMutationCondition(
    separateItem,
    { attributes: true },
    () => !separateItem.hidden && !separateItem.disabled
  );
  separateItem.click();
  await closeTabContextMenu(menu);

  await Services.fog.testFlushAllChildren();

  const recordedEvents = Glean.splitview.end.testGetValue();
  Assert.equal(recordedEvents.length, 1, "One end event recorded");
  Assert.equal(
    recordedEvents[0].extra.trigger,
    "menu_separate",
    "Trigger is menu_separate"
  );
  Assert.equal(
    recordedEvents[0].extra.tab_layout,
    "horizontal",
    "Tab layout is horizontal"
  );

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
});

add_task(async function test_splitview_end_event_icon_separate() {
  await resetTelemetry();

  const tab1 = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  const tab2 = BrowserTestUtils.addTab(gBrowser, "https://example.org");
  await Promise.all([
    BrowserTestUtils.browserLoaded(tab1.linkedBrowser),
    BrowserTestUtils.browserLoaded(tab2.linkedBrowser),
  ]);
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  // Multiselect and create split view via menu
  gBrowser.addToMultiSelectedTabs(tab1);
  gBrowser.addToMultiSelectedTabs(tab2);
  const createMenu = await openTabContextMenu(tab2);
  const moveToSplitViewItem = document.getElementById(
    "context_moveTabToSplitView"
  );
  await BrowserTestUtils.waitForMutationCondition(
    moveToSplitViewItem,
    { attributes: true },
    () => !moveToSplitViewItem.hidden && !moveToSplitViewItem.disabled
  );
  moveToSplitViewItem.click();
  await closeTabContextMenu(createMenu);

  await tabIsInSplitView(tab1);
  await tabIsInSplitView(tab2);

  await resetTelemetry();

  // Open URLbar icon menu and click "Separate Tabs"
  const menu = await openSplitViewIconMenu();
  const separateItem = menu.querySelector(
    'menuitem[command="splitViewCmd_separateTabs"]'
  );

  // Activate the menu item
  menu.activateItem(separateItem);

  // Wait for split view to be removed
  await TestUtils.waitForCondition(
    () => !tab1.splitview && !tab2.splitview,
    "Waiting for split view to be removed"
  );

  await Services.fog.testFlushAllChildren();

  const recordedEvents = Glean.splitview.end.testGetValue();
  Assert.equal(recordedEvents.length, 1, "One end event recorded");
  Assert.equal(
    recordedEvents[0].extra.trigger,
    "icon_separate",
    "Trigger is icon_separate"
  );

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
});

add_task(async function test_splitview_end_event_footer_separate() {
  await resetTelemetry();

  const tab1 = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  const tab2 = BrowserTestUtils.addTab(gBrowser, "https://example.org");
  await Promise.all([
    BrowserTestUtils.browserLoaded(tab1.linkedBrowser),
    BrowserTestUtils.browserLoaded(tab2.linkedBrowser),
  ]);
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  // Multiselect and create split view via menu
  gBrowser.addToMultiSelectedTabs(tab1);
  gBrowser.addToMultiSelectedTabs(tab2);
  const createMenu = await openTabContextMenu(tab2);
  const moveToSplitViewItem = document.getElementById(
    "context_moveTabToSplitView"
  );
  await BrowserTestUtils.waitForMutationCondition(
    moveToSplitViewItem,
    { attributes: true },
    () => !moveToSplitViewItem.hidden && !moveToSplitViewItem.disabled
  );
  moveToSplitViewItem.click();
  await closeTabContextMenu(createMenu);

  await tabIsInSplitView(tab1);
  await tabIsInSplitView(tab2);

  await resetTelemetry();

  const panel2 = document.getElementById(tab2.linkedPanel);
  const menu = await openSplitViewFooterMenu(panel2);
  const separateItem = menu.querySelector(
    'menuitem[command="splitViewCmd_separateTabs"]'
  );
  menu.activateItem(separateItem);

  // Wait for split view to be removed
  await TestUtils.waitForCondition(
    () => !tab1.splitview && !tab2.splitview,
    "Waiting for split view to be removed"
  );

  await Services.fog.testFlushAllChildren();

  const recordedEvents = Glean.splitview.end.testGetValue();
  Assert.equal(recordedEvents.length, 1, "One end event recorded");
  Assert.equal(
    recordedEvents[0].extra.trigger,
    "footer_separate",
    "Trigger is footer_separate"
  );

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
});

add_task(async function test_splitview_end_event_icon_close() {
  await resetTelemetry();

  const tab1 = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  const tab2 = BrowserTestUtils.addTab(gBrowser, "https://example.org");
  await Promise.all([
    BrowserTestUtils.browserLoaded(tab1.linkedBrowser),
    BrowserTestUtils.browserLoaded(tab2.linkedBrowser),
  ]);
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  // Multiselect and create split view via menu
  gBrowser.addToMultiSelectedTabs(tab1);
  gBrowser.addToMultiSelectedTabs(tab2);
  const createMenu = await openTabContextMenu(tab2);
  const moveToSplitViewItem = document.getElementById(
    "context_moveTabToSplitView"
  );
  await BrowserTestUtils.waitForMutationCondition(
    moveToSplitViewItem,
    { attributes: true },
    () => !moveToSplitViewItem.hidden && !moveToSplitViewItem.disabled
  );
  moveToSplitViewItem.click();
  await closeTabContextMenu(createMenu);

  await tabIsInSplitView(tab1);
  await tabIsInSplitView(tab2);

  await resetTelemetry();

  // Open URLbar icon menu and click "Close both tabs"
  const menu = await openSplitViewIconMenu();
  const closeItem = menu.querySelector(
    'menuitem[command="splitViewCmd_closeTabs"]'
  );

  // Wait for tabs to be removed
  const tabRemovedPromise = Promise.all([
    BrowserTestUtils.waitForEvent(tab1, "TabClose"),
    BrowserTestUtils.waitForEvent(tab2, "TabClose"),
  ]);

  menu.activateItem(closeItem);
  await tabRemovedPromise;

  await Services.fog.testFlushAllChildren();

  const recordedEvents = Glean.splitview.end.testGetValue();
  Assert.equal(recordedEvents.length, 1, "One end event recorded");
  Assert.equal(
    recordedEvents[0].extra.trigger,
    "icon_close",
    "Trigger is icon_close"
  );
});

add_task(async function test_splitview_end_event_footer_close() {
  await resetTelemetry();

  const tab1 = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  const tab2 = BrowserTestUtils.addTab(gBrowser, "https://example.org");
  await Promise.all([
    BrowserTestUtils.browserLoaded(tab1.linkedBrowser),
    BrowserTestUtils.browserLoaded(tab2.linkedBrowser),
  ]);
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  // Multiselect and create split view via menu
  gBrowser.addToMultiSelectedTabs(tab1);
  gBrowser.addToMultiSelectedTabs(tab2);
  const createMenu = await openTabContextMenu(tab2);
  const moveToSplitViewItem = document.getElementById(
    "context_moveTabToSplitView"
  );
  await BrowserTestUtils.waitForMutationCondition(
    moveToSplitViewItem,
    { attributes: true },
    () => !moveToSplitViewItem.hidden && !moveToSplitViewItem.disabled
  );
  moveToSplitViewItem.click();
  await closeTabContextMenu(createMenu);

  await tabIsInSplitView(tab1);
  await tabIsInSplitView(tab2);

  await resetTelemetry();

  // Wait for tabs to be removed
  const tabRemovedPromise = Promise.all([
    BrowserTestUtils.waitForEvent(tab1, "TabClose"),
    BrowserTestUtils.waitForEvent(tab2, "TabClose"),
  ]);

  const panel2 = document.getElementById(tab2.linkedPanel);
  const menu = await openSplitViewFooterMenu(panel2);
  const closeItem = menu.querySelector(
    'menuitem[command="splitViewCmd_closeTabs"]'
  );
  menu.activateItem(closeItem);
  await tabRemovedPromise;

  await Services.fog.testFlushAllChildren();

  const recordedEvents = Glean.splitview.end.testGetValue();
  Assert.equal(recordedEvents.length, 1, "One end event recorded");
  Assert.equal(
    recordedEvents[0].extra.trigger,
    "footer_close",
    "Trigger is footer_close"
  );
});

add_task(async function test_splitview_end_event_tab_close() {
  await resetTelemetry();

  const tab1 = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  const tab2 = BrowserTestUtils.addTab(gBrowser, "https://example.org");
  await Promise.all([
    BrowserTestUtils.browserLoaded(tab1.linkedBrowser),
    BrowserTestUtils.browserLoaded(tab2.linkedBrowser),
  ]);
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  gBrowser.addTabSplitView([tab1, tab2], {
    trigger: "menu_open",
  });
  await tabIsInSplitView(tab1);
  await tabIsInSplitView(tab2);

  // Close one of the tabs, which should automatically unsplit
  BrowserTestUtils.removeTab(tab1);

  await Services.fog.testFlushAllChildren();

  const recordedEvents = Glean.splitview.end.testGetValue();
  Assert.equal(recordedEvents.length, 1, "One end event recorded");
  Assert.equal(
    recordedEvents[0].extra.trigger,
    "tab_close",
    "Trigger is tab_close"
  );

  BrowserTestUtils.removeTab(tab2);
});

add_task(async function test_splitview_reverse_event_icon() {
  await resetTelemetry();

  const tab1 = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  const tab2 = BrowserTestUtils.addTab(gBrowser, "https://example.org");
  await Promise.all([
    BrowserTestUtils.browserLoaded(tab1.linkedBrowser),
    BrowserTestUtils.browserLoaded(tab2.linkedBrowser),
  ]);
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  // Multiselect and create split view via menu
  gBrowser.addToMultiSelectedTabs(tab1);
  gBrowser.addToMultiSelectedTabs(tab2);
  const createMenu = await openTabContextMenu(tab2);
  const moveToSplitViewItem = document.getElementById(
    "context_moveTabToSplitView"
  );
  await BrowserTestUtils.waitForMutationCondition(
    moveToSplitViewItem,
    { attributes: true },
    () => !moveToSplitViewItem.hidden && !moveToSplitViewItem.disabled
  );
  moveToSplitViewItem.click();
  await closeTabContextMenu(createMenu);

  await tabIsInSplitView(tab1);
  await tabIsInSplitView(tab2);

  const splitView = tab1.splitview;

  await resetTelemetry();

  // Open URLbar icon menu and click "Reverse Tabs"
  const menu = await openSplitViewIconMenu();
  const reverseItem = menu.querySelector(
    'menuitem[command="splitViewCmd_reverseTabs"]'
  );

  // Store original order to verify reversal
  const originalFirstTab = splitView.tabs[0];

  menu.activateItem(reverseItem);

  // Wait for tabs to be reversed
  await TestUtils.waitForCondition(
    () => splitView.tabs[0] !== originalFirstTab,
    "Waiting for tabs to be reversed"
  );

  await Services.fog.testFlushAllChildren();

  const recordedEvents = Glean.splitview.reverse.testGetValue();
  Assert.equal(recordedEvents.length, 1, "One reverse event recorded");
  Assert.equal(recordedEvents[0].extra.trigger, "icon", "Trigger is icon");

  // Clean up without recording telemetry
  if (splitView) {
    splitView.unsplitTabs();
  }
  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
});

add_task(async function test_splitview_reverse_event_footer() {
  await resetTelemetry();

  const tab1 = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  const tab2 = BrowserTestUtils.addTab(gBrowser, "https://example.org");
  await Promise.all([
    BrowserTestUtils.browserLoaded(tab1.linkedBrowser),
    BrowserTestUtils.browserLoaded(tab2.linkedBrowser),
  ]);
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  // Multiselect and create split view via menu
  gBrowser.addToMultiSelectedTabs(tab1);
  gBrowser.addToMultiSelectedTabs(tab2);
  const createMenu = await openTabContextMenu(tab2);
  const moveToSplitViewItem = document.getElementById(
    "context_moveTabToSplitView"
  );
  await BrowserTestUtils.waitForMutationCondition(
    moveToSplitViewItem,
    { attributes: true },
    () => !moveToSplitViewItem.hidden && !moveToSplitViewItem.disabled
  );
  moveToSplitViewItem.click();
  await closeTabContextMenu(createMenu);

  await tabIsInSplitView(tab1);
  await tabIsInSplitView(tab2);

  const splitView = tab1.splitview;

  await resetTelemetry();

  // Store original order to verify reversal
  const originalFirstTab = splitView.tabs[0];

  const panel2 = document.getElementById(tab2.linkedPanel);
  const menu = await openSplitViewFooterMenu(panel2);
  const reverseItem = menu.querySelector(
    'menuitem[command="splitViewCmd_reverseTabs"]'
  );
  menu.activateItem(reverseItem);

  // Wait for tabs to be reversed
  await TestUtils.waitForCondition(
    () => splitView.tabs[0] !== originalFirstTab,
    "Waiting for tabs to be reversed"
  );

  await Services.fog.testFlushAllChildren();

  const recordedEvents = Glean.splitview.reverse.testGetValue();
  Assert.equal(recordedEvents.length, 1, "One reverse event recorded");
  Assert.equal(recordedEvents[0].extra.trigger, "footer", "Trigger is footer");

  // Clean up without recording telemetry
  if (splitView) {
    splitView.unsplitTabs();
  }
  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
});

add_task(async function test_splitview_resize_event() {
  await resetTelemetry();

  const tab1 = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  const tab2 = BrowserTestUtils.addTab(gBrowser, "https://example.org");
  await Promise.all([
    BrowserTestUtils.browserLoaded(tab1.linkedBrowser),
    BrowserTestUtils.browserLoaded(tab2.linkedBrowser),
  ]);
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  const splitView = gBrowser.addTabSplitView([tab1, tab2], {
    trigger: "menu_open",
  });
  await tabIsInSplitView(tab1);
  await tabIsInSplitView(tab2);

  // Wait for splitter to be visible
  const { tabpanels } = gBrowser;
  await BrowserTestUtils.waitForMutationCondition(
    tabpanels,
    { childList: true },
    () => tabpanels.querySelector(".split-view-splitter")
  );
  await BrowserTestUtils.waitForMutationCondition(
    tabpanels.splitViewSplitter,
    { attributes: true },
    () => BrowserTestUtils.isVisible(tabpanels.splitViewSplitter)
  );

  // Simulate dragging the splitter
  const splitter = tabpanels.splitViewSplitter;
  const leftPanel = document.getElementById(tab1.linkedPanel);

  AccessibilityUtils.setEnv({ mustHaveAccessibleRule: false });
  EventUtils.synthesizeMouseAtCenter(splitter, { type: "mousedown" });
  EventUtils.synthesizeMouse(splitter, -100, 0, { type: "mousemove" });
  EventUtils.synthesizeMouse(splitter, 0, 0, { type: "mouseup" });
  AccessibilityUtils.resetEnv();

  // Wait for panel width to be updated
  await TestUtils.waitForCondition(
    () => leftPanel.hasAttribute("width"),
    "Left panel should have width attribute after resize"
  );

  // Telemetry is recorded inside a promiseDocumentFlushed().then() callback;
  // flush here to ensure that callback has completed before checking.
  await window.promiseDocumentFlushed(() => {});

  await Services.fog.testFlushAllChildren();

  const recordedEvents = Glean.splitview.resize.testGetValue();
  Assert.ok(recordedEvents, "Resize events were recorded");
  Assert.equal(recordedEvents.length, 1, "One resize event recorded");
  Assert.ok(
    recordedEvents[0].extra.width > 0 && recordedEvents[0].extra.width < 100,
    `Width percentage is between 0 and 100: ${recordedEvents[0].extra.width}`
  );

  // Clean up without recording telemetry
  if (splitView) {
    splitView.unsplitTabs();
  }
  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
});
