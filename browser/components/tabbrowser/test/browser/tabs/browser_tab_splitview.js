/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.verticalTabs", true]],
  });
});

registerCleanupFunction(async function () {
  Services.prefs.clearUserPref("sidebar.revamp");
  Services.prefs.clearUserPref(
    "browser.toolbarbuttons.introduced.sidebar-button"
  );
  Services.prefs.clearUserPref("browser.tabs.splitview.hasUsed");
});

const urlbarButton = document.getElementById("split-view-button");

async function addTabAndLoadBrowser() {
  const tab = BrowserTestUtils.addTab(gBrowser, "https://example.com");
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  return tab;
}

async function checkSelectedSplitViewColumn(expected, message) {
  const root = document.documentElement;
  await BrowserTestUtils.waitForMutationCondition(
    root,
    { attributes: true, attributeFilter: ["splitview-selected-column"] },
    () => root.getAttribute("splitview-selected-column") === expected
  );
  Assert.equal(
    root.getAttribute("splitview-selected-column"),
    expected,
    message
  );
}

async function checkSplitViewPanelVisible(tab, isVisible) {
  const panel = document.getElementById(tab.linkedPanel);
  info("wait for split-view-panel-active to change visibility");
  await BrowserTestUtils.waitForMutationCondition(
    panel,
    { attributes: true },
    () => panel.classList.contains("split-view-panel-active") == isVisible
  );
}

add_task(async function test_splitViewCreateAndAddTabs() {
  let tab1 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let tab3 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let tab4 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  const tabpanels = document.getElementById("tabbrowser-tabpanels");
  // Add tabs to split view
  let splitview = gBrowser.addTabSplitView([tab1, tab2]);
  let splitview2 = gBrowser.addTabSplitView([tab3, tab4]);
  let tabbrowserTabs = document.getElementById("tabbrowser-tabs");
  await BrowserTestUtils.waitForMutationCondition(
    tabbrowserTabs,
    { childList: true },
    () => tabbrowserTabs.querySelectorAll("tab-split-view-wrapper").length === 2
  );

  Assert.ok(splitview.splitViewId, "Split view has id");
  Assert.equal(splitview.tabs.length, 2, "Split view has 2 tabs");
  Assert.ok(splitview.tabs.includes(tab1), "tab1 is in split view wrapper");
  Assert.ok(splitview.tabs.includes(tab2), "tab2 is in split view wrapper");

  await BrowserTestUtils.waitForMutationCondition(
    splitview2,
    { attributeFilter: ["splitViewId"] },
    () => splitview2.hasAttribute("splitViewId")
  );
  Assert.notEqual(
    splitview.splitViewId,
    splitview2.splitViewId,
    "Split view has different id than split view 2"
  );
  Assert.equal(splitview2.tabs.length, 2, "Split view 2 has 2 tabs");
  Assert.ok(splitview2.tabs.includes(tab3), "tab3 is in split view wrapper");
  Assert.ok(splitview2.tabs.includes(tab4), "tab4 is in split view wrapper");

  Assert.ok(
    !splitview.hasAttribute("hasactivetab"),
    "The split view wrapper has the expected attribute when it does not contain the selected tab"
  );

  // Verify ARIA labels for split view tabs
  const splitViewLeft = gBrowser.tabLocalization.formatValueSync(
    "tabbrowser-tab-label-tab-split-view-left",
    { label: "" }
  );
  const splitViewRight = gBrowser.tabLocalization.formatValueSync(
    "tabbrowser-tab-label-tab-split-view-right",
    { label: "" }
  );
  Assert.ok(
    tab1.getAttribute("aria-label").includes(splitViewLeft),
    "Left tab has the correct ARIA label."
  );
  Assert.ok(
    tab2.getAttribute("aria-label").includes(splitViewRight),
    "Right tab has the correct ARIA label."
  );

  gBrowser.selectTabAtIndex(tab1._tPos);
  await BrowserTestUtils.waitForMutationCondition(
    splitview,
    { attributes: true, attributeFilter: ["hasactivetab"] },
    () => splitview.hasAttribute("hasactivetab")
  );
  Assert.ok(
    splitview.hasAttribute("hasactivetab"),
    "The split view wrapper has the expected attribute when it contains the selected tab"
  );

  splitview.unsplitTabs();
  Assert.strictEqual(
    document.querySelectorAll("tab-split-view-wrapper").length,
    2,
    "Tabs have been unsplit from split view"
  );
  Assert.ok(
    !tab1.hasAttribute("aria-label"),
    "ARIA label was removed from the left tab."
  );
  Assert.ok(
    !tab2.hasAttribute("aria-label"),
    "ARIA label was removed from the right tab."
  );

  let tab3Panel = tab3.linkedBrowser.closest(".browserSidebarContainer");
  let tab4Panel = tab4.linkedBrowser.closest(".browserSidebarContainer");

  Assert.ok(
    !tab3Panel.classList.contains("split-view-panel-active") &&
      !tab4Panel.classList.contains("split-view-panel-active"),
    "Split view active classes have been removed from the tab panels"
  );

  await BrowserTestUtils.waitForMutationCondition(
    tabpanels,
    { attributes: true },
    () => !tabpanels.hasAttribute("splitview")
  );
  Assert.ok(
    !tabpanels.hasAttribute("splitview"),
    "Tab panel does not have blue outline"
  );

  let splitViewCreated = BrowserTestUtils.waitForEvent(
    tabbrowserTabs,
    "SplitViewCreated"
  );
  // Add tabs back to split view
  splitview = gBrowser.addTabSplitView([tab1, tab2]);
  await splitViewCreated;
  await BrowserTestUtils.waitForMutationCondition(
    tabbrowserTabs,
    { childList: true },
    () => tabbrowserTabs.querySelectorAll("tab-split-view-wrapper").length === 2
  );
  is(
    tabbrowserTabs.querySelectorAll("tab-split-view-wrapper").length,
    2,
    "Two splitviews have been created"
  );

  gBrowser.removeTab(tab1);
  await BrowserTestUtils.waitForMutationCondition(
    splitview,
    { childList: true },
    () => !splitview.children.length
  );
  ok(!tab2.splitview, "Tab2 is no longer in a splitview");
  is(
    tabbrowserTabs.querySelectorAll("tab-split-view-wrapper").length,
    1,
    "Only one splitview remains after closing tab in first splitview"
  );
  splitview2.close();
  BrowserTestUtils.removeTab(tab2);
});

add_task(async function test_split_view_panels() {
  const tab1 = await addTabAndLoadBrowser();
  const tab2 = await addTabAndLoadBrowser();
  const originalTab = gBrowser.selectedTab;
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  info("Activate split view.");
  const splitView = gBrowser.addTabSplitView([tab1, tab2]);
  for (const tab of splitView.tabs) {
    await checkSplitViewPanelVisible(tab, true);
  }
  await BrowserTestUtils.waitForMutationCondition(
    urlbarButton,
    { attributes: true, attributeFilter: ["hidden"] },
    () => BrowserTestUtils.isVisible(urlbarButton)
  );

  info("Open split view menu.");
  const menu = document.getElementById("split-view-menu");
  const promiseMenuShown = BrowserTestUtils.waitForPopupEvent(menu, "shown");
  EventUtils.synthesizeMouseAtCenter(urlbarButton, {});
  await promiseMenuShown;
  menu.hidePopup();

  info("Select tabs using tab panels.");
  await SimpleTest.promiseFocus(tab1.linkedBrowser);
  let panel = document.getElementById(tab1.linkedPanel);
  Assert.ok(
    panel.classList.contains("deck-selected"),
    "First panel is selected."
  );
  await BrowserTestUtils.waitForMutationCondition(
    urlbarButton,
    { attributes: true, attributeFilter: ["data-active-index"] },
    () => urlbarButton.dataset.activeIndex == "0"
  );

  await SimpleTest.promiseFocus(tab2.linkedBrowser);
  panel = document.getElementById(tab2.linkedPanel);
  Assert.ok(
    panel.classList.contains("deck-selected"),
    "Second panel is selected."
  );
  await BrowserTestUtils.waitForMutationCondition(
    urlbarButton,
    { attributes: true },
    () => urlbarButton.dataset.activeIndex == "1"
  );

  info("Switch to a non-split view tab.");
  await BrowserTestUtils.switchTab(gBrowser, originalTab);
  for (const tab of splitView.tabs) {
    await checkSplitViewPanelVisible(tab, false);
  }

  info("Switch back to a split view tab.");
  await BrowserTestUtils.switchTab(gBrowser, tab1);
  for (const tab of splitView.tabs) {
    await checkSplitViewPanelVisible(tab, true);
  }

  info("Remove the split view, keeping tabs intact.");
  splitView.unsplitTabs();
  await checkSplitViewPanelVisible(tab1, false, true);
  await checkSplitViewPanelVisible(tab2, false, true);
  await BrowserTestUtils.waitForMutationCondition(
    urlbarButton,
    { attributes: true },
    () => BrowserTestUtils.isHidden(urlbarButton)
  );

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
});

/**
 * Verify that switching to a non-split-view tab preserves the split-view panel
 * attributes needed to avoid browser content reflow when switching back.
 *
 * Specifically:
 * - split-view-panel class and column attribute are kept on the panels so they
 *   re-enter the flex layout at the correct width on reactivation.
 * - The splitview attribute stays on tabpanels so CSS rules scoped to it remain
 *   stable and do not affect the non-split-view tab's content area.
 * - The splitter is hidden but stays in position.
 * - After full dissolution, all attributes are properly cleaned up.
 */
add_task(async function test_suspend_preserves_split_view_attributes() {
  const tab1 = await addTabAndLoadBrowser();
  const tab2 = await addTabAndLoadBrowser();
  const originalTab = gBrowser.selectedTab;
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  const splitView = gBrowser.addTabSplitView([tab1, tab2]);
  const tabpanels = document.getElementById("tabbrowser-tabpanels");
  for (const tab of splitView.tabs) {
    await checkSplitViewPanelVisible(tab, true);
  }

  const panel1 = document.getElementById(tab1.linkedPanel);
  const panel2 = document.getElementById(tab2.linkedPanel);
  const splitter = tabpanels.splitViewSplitter;

  info("Switch to a non-split-view tab.");
  await BrowserTestUtils.switchTab(gBrowser, originalTab);
  for (const tab of splitView.tabs) {
    await checkSplitViewPanelVisible(tab, false);
  }

  Assert.ok(
    panel1.classList.contains("split-view-panel"),
    "panel1 retains split-view-panel class while a non-split-view tab is selected"
  );
  Assert.ok(
    panel2.classList.contains("split-view-panel"),
    "panel2 retains split-view-panel class while a non-split-view tab is selected"
  );
  Assert.ok(
    panel1.hasAttribute("column"),
    "panel1 retains column attribute while a non-split-view tab is selected"
  );
  Assert.ok(
    panel2.hasAttribute("column"),
    "panel2 retains column attribute while a non-split-view tab is selected"
  );
  Assert.ok(
    tabpanels.hasAttribute("splitview"),
    "tabpanels retains splitview attribute while a non-split-view tab is selected"
  );
  Assert.ok(
    splitter.hidden,
    "Splitter is hidden when a non-split-view tab is selected"
  );

  info("Switch back to a split-view tab.");
  await BrowserTestUtils.switchTab(gBrowser, tab1);
  for (const tab of splitView.tabs) {
    await checkSplitViewPanelVisible(tab, true);
  }

  Assert.ok(
    panel1.classList.contains("split-view-panel-active"),
    "panel1 has split-view-panel-active after switching back to the split-view tab"
  );
  Assert.ok(
    panel2.classList.contains("split-view-panel-active"),
    "panel2 has split-view-panel-active after switching back to the split-view tab"
  );
  Assert.ok(
    !splitter.hidden,
    "Splitter is visible after switching back to the split-view tab"
  );

  info("Dissolve split view and verify full attribute cleanup.");
  splitView.unsplitTabs();
  await BrowserTestUtils.waitForMutationCondition(
    tabpanels,
    { attributes: true },
    () => !tabpanels.hasAttribute("splitview")
  );
  Assert.ok(
    !tabpanels.hasAttribute("splitview"),
    "tabpanels splitview attribute is removed after split view dissolution"
  );
  Assert.ok(
    !panel1.classList.contains("split-view-panel"),
    "split-view-panel class is removed from panel1 after dissolution"
  );
  Assert.ok(
    !panel2.classList.contains("split-view-panel"),
    "split-view-panel class is removed from panel2 after dissolution"
  );
  Assert.ok(
    !panel1.hasAttribute("column"),
    "column attribute is removed from panel1 after dissolution"
  );
  Assert.ok(
    !panel2.hasAttribute("column"),
    "column attribute is removed from panel2 after dissolution"
  );

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
});

/**
 * The sidebar splitters and the split view splitter need to know which split
 * view column is selected, but they live outside of the selected panel's
 * subtree. That state is mirrored onto the root element as
 * [splitview-selected-column] rather than matched with :root:has(), which is
 * far too expensive to invalidate in the main window (bug 2054065).
 */
add_task(async function test_selected_split_view_column_on_root() {
  const tab1 = await addTabAndLoadBrowser();
  const tab2 = await addTabAndLoadBrowser();
  const originalTab = gBrowser.selectedTab;
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  await checkSelectedSplitViewColumn(
    null,
    "No selected column before a split view is created"
  );

  const splitView = gBrowser.addTabSplitView([tab1, tab2]);
  for (const tab of splitView.tabs) {
    await checkSplitViewPanelVisible(tab, true);
  }

  await SimpleTest.promiseFocus(tab1.linkedBrowser);
  await checkSelectedSplitViewColumn(
    "0",
    "Selecting the first panel reports column 0"
  );

  await SimpleTest.promiseFocus(tab2.linkedBrowser);
  await checkSelectedSplitViewColumn(
    "1",
    "Selecting the second panel reports column 1"
  );

  info("Reverse the tabs in the split view.");
  splitView.reverseTabs();
  await checkSelectedSplitViewColumn(
    "0",
    "Reversing the tabs moves the selected tab to column 0"
  );

  info("Switch to a non-split-view tab.");
  await BrowserTestUtils.switchTab(gBrowser, originalTab);
  await checkSelectedSplitViewColumn(
    null,
    "No selected column while a non-split-view tab is selected"
  );

  info("Switch back to a split-view tab.");
  await BrowserTestUtils.switchTab(gBrowser, tab2);
  await checkSelectedSplitViewColumn(
    "0",
    "Selected column is restored when switching back to the split view"
  );

  info("Dissolve the split view.");
  splitView.unsplitTabs();
  await checkSelectedSplitViewColumn(
    null,
    "No selected column after the split view is dissolved"
  );

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
});

add_task(async function test_splitview_replaceTab_activates_panels() {
  const tab1 = await addTabAndLoadBrowser();
  const tab2 = await addTabAndLoadBrowser();
  await BrowserTestUtils.switchTab(gBrowser, tab1);

  const splitView = gBrowser.addTabSplitView([tab1, tab2]);
  for (const tab of splitView.tabs) {
    await checkSplitViewPanelVisible(tab, true);
  }

  const tab3 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  splitView.replaceTab(tab1, tab3);

  await checkSplitViewPanelVisible(tab2, true);
  await checkSplitViewPanelVisible(tab3, true);

  splitView.close();
  BrowserTestUtils.removeTab(tab1);
});

add_task(async function test_split_view_preserves_multiple_pairings() {
  info("Create four tabs for two split view pairings.");
  const tab1 = await addTabAndLoadBrowser();
  const tab2 = await addTabAndLoadBrowser();
  const tab3 = await addTabAndLoadBrowser();
  const tab4 = await addTabAndLoadBrowser();

  info("Create two split views (Tab 1 + Tab 2) & (Tab 3 + Tab 4).");
  const splitView1 = gBrowser.addTabSplitView([tab1, tab2]);
  const splitView2 = gBrowser.addTabSplitView([tab3, tab4]);

  info("Switch to Tab 1 to activate the first split view.");
  await BrowserTestUtils.switchTab(gBrowser, tab1);
  await checkSplitViewPanelVisible(tab1, true);
  await checkSplitViewPanelVisible(tab2, true);
  await checkSplitViewPanelVisible(tab3, false);
  await checkSplitViewPanelVisible(tab4, false);

  info("Switch to Tab 3 to activate the second split view.");
  await BrowserTestUtils.switchTab(gBrowser, tab3);
  await checkSplitViewPanelVisible(tab1, false);
  await checkSplitViewPanelVisible(tab2, false);
  await checkSplitViewPanelVisible(tab3, true);
  await checkSplitViewPanelVisible(tab4, true);

  info("Switch back to the first split view.");
  await BrowserTestUtils.switchTab(gBrowser, tab1);
  await checkSplitViewPanelVisible(tab1, true);
  await checkSplitViewPanelVisible(tab2, true);
  await checkSplitViewPanelVisible(tab3, false);
  await checkSplitViewPanelVisible(tab4, false);

  splitView1.close();
  splitView2.close();
});

add_task(async function test_moving_tabs() {
  let [tab1, tab2, tab3, tab4] = await Promise.all(
    Array.from({ length: 4 }).map((_, index) =>
      addTab(`data:text/plain,tab${index + 1}`)
    )
  );
  let startingTab = gBrowser.tabs[0];

  let splitview = gBrowser.addTabSplitView([tab1, tab2], {
    insertBefore: tab1,
  });
  Assert.equal(splitview.tabs.length, 2, "Split view has 2 tabs");

  Assert.deepEqual(
    gBrowser.tabs,
    [startingTab, tab1, tab2, tab3, tab4],
    "Confirm starting order of tabs"
  );

  gBrowser.moveTabTo(startingTab, { tabIndex: 2 });

  Assert.deepEqual(
    gBrowser.tabs,
    [tab1, tab2, startingTab, tab3, tab4],
    "Starting tab is moved after splitview tabs"
  );
  ok(
    tab1.splitview && tab2.splitview && !startingTab.splitview,
    "Tab 1 and tab 2 are still in a splitview and starting tab isn't"
  );

  gBrowser.moveTabTo(tab1, { tabIndex: 3 });
  Assert.deepEqual(
    gBrowser.tabs,
    [startingTab, tab3, tab4, tab1, tab2],
    "Moving a splitview tab forwards moves both tabs in the splitview"
  );
  ok(
    tab1.splitview && tab2.splitview,
    "Tab 1 and tab 2 are still in a splitview"
  );

  gBrowser.moveTabTo(tab1, { tabIndex: 1 });
  Assert.deepEqual(
    gBrowser.tabs,
    [startingTab, tab1, tab2, tab3, tab4],
    "Moving a splitview tab backwards moves both tabs in the splitview"
  );
  ok(
    tab1.splitview && tab2.splitview,
    "Tab 1 and tab 2 are still in a splitview"
  );

  gBrowser.selectedTab = startingTab;
  gBrowser.moveTabForward();
  Assert.deepEqual(
    gBrowser.tabs,
    [tab1, tab2, startingTab, tab3, tab4],
    "Selected tab moves forward but after splitview tabs"
  );
  ok(
    tab1.splitview && tab2.splitview && !startingTab.splitview,
    "Tab 1 and tab 2 are still in a splitview and starting tab isn't"
  );

  gBrowser.moveTabBackward();
  Assert.deepEqual(
    gBrowser.tabs,
    [startingTab, tab1, tab2, tab3, tab4],
    "Selected tab moves backward but before splitview tabs"
  );
  ok(
    tab1.splitview && tab2.splitview && !startingTab.splitview,
    "Tab 1 and tab 2 are still in a splitview and starting tab isn't"
  );

  gBrowser.selectedTab = tab1;
  gBrowser.moveTabForward();

  Assert.deepEqual(
    gBrowser.tabs,
    [startingTab, tab3, tab1, tab2, tab4],
    "Selected tab in a splitview moves both splitview tabs forward"
  );
  ok(
    tab1.splitview && tab2.splitview,
    "Tab 1 and tab 2 are still in a splitview"
  );

  gBrowser.selectedTab = tab2;
  gBrowser.moveTabBackward();
  Assert.deepEqual(
    gBrowser.tabs,
    [startingTab, tab1, tab2, tab3, tab4],
    "Selected tab in a splitview moves both splitview tabs backward"
  );
  ok(
    tab1.splitview && tab2.splitview,
    "Tab 1 and tab 2 are still in a splitview"
  );

  // Create a tabgroup with tabs and a splitview
  let group = gBrowser.addTabGroup([tab1, tab2, tab3], { insertBefore: tab3 });
  Assert.deepEqual(
    gBrowser.tabs,
    [startingTab, tab1, tab2, tab3, tab4],
    "Selected tab in a splitview moves both splitview tabs backward"
  );

  // Add another splitview to the existing group, so two splitviews are in the group
  let splitview2 = gBrowser.addTabSplitView([tab3, tab4], {
    insertBefore: tab3,
  });
  ok(
    tab3.splitview && tab4.splitview && splitview2.group,
    "Tab 3 and tab 4 are in a splitview and in a tab group"
  );

  gBrowser.selectedTab = tab2;
  gBrowser.moveTabForward();

  Assert.deepEqual(
    gBrowser.tabs,
    [startingTab, tab3, tab4, tab1, tab2],
    "First splitview tabs are moved after the second splitview but within the group"
  );

  Assert.equal(
    splitview.group,
    splitview2.group,
    "Both splitviews are still in the same tab group"
  );

  gBrowser.selectedTab = tab4;
  gBrowser.moveTabBackward();

  // The order doesn't change, the splitview tabs just moved outside of the group.
  Assert.deepEqual(
    gBrowser.tabs,
    [startingTab, tab3, tab4, tab1, tab2],
    "Splitview tabs are moved together and in the correct order"
  );
  ok(
    tab3.splitview && tab4.splitview && !splitview2.group,
    "Tab 3 and tab 4 are still in a splitview but no longer in a tab group"
  );

  group.addTabs([startingTab]);
  Assert.deepEqual(
    gBrowser.tabs,
    [tab3, tab4, tab1, tab2, startingTab],
    "StartingTab is moved to the end of the tabstrip"
  );
  ok(
    !startingTab.splitview && startingTab.group,
    "Starting tab was added to the group but not the splitview"
  );
  gBrowser.moveTabsBefore([startingTab], tab1);
  Assert.deepEqual(
    gBrowser.tabs,
    [tab3, tab4, startingTab, tab1, tab2],
    "StartingTab is moved in front of tab1"
  );
  ok(
    !startingTab.splitview && startingTab.group,
    "Starting tab is still in the group but not added to the splitview"
  );

  for (let tab of [tab1, tab2, tab3, tab4]) {
    BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function test_move_group_with_splitview_to_new_window() {
  info("Create tabs for split view");
  const tab1 = await addTabAndLoadBrowser();
  const tab2 = await addTabAndLoadBrowser();
  const tab3 = await addTabAndLoadBrowser();

  info("Create a split view with tab1 and tab2");
  const splitView = gBrowser.addTabSplitView([tab1, tab2]);
  Assert.ok(splitView, "Split view was created");

  info("Create a tab group containing the split view and tab3");
  const group = gBrowser.addTabGroup([splitView, tab3]);
  Assert.ok(group, "Tab group was created");
  Assert.equal(group.tabs.length, 3, "Group has 3 tabs");

  info("Move the group to a new window");
  const promiseNewWindow = BrowserTestUtils.waitForNewWindow();
  gBrowser.replaceGroupWithWindow(group);
  const newWindow = await promiseNewWindow;

  info("Verify the new window contains the expected tabs");
  Assert.ok(newWindow, "New window was created");
  Assert.equal(
    newWindow.gBrowser.tabs.length,
    3,
    "New window has 3 tabs from the group"
  );

  info("Verify the split view was preserved in the new window");
  const movedTabs = newWindow.gBrowser.tabs;
  Assert.equal(movedTabs.length, 3, "Three tabs were moved");
  Assert.ok(
    movedTabs[0].splitview && movedTabs[1].splitview,
    "First two tabs are still in a split view"
  );
  Assert.equal(
    movedTabs[0].splitview,
    movedTabs[1].splitview,
    "Tabs are in the same split view"
  );

  await BrowserTestUtils.closeWindow(newWindow);
});

add_task(async function test_createGroupFromPinnedTabWithSplitView() {
  info(
    "Test creating a tab group from a pinned tab when a split view follows it"
  );

  let pinnedTab = BrowserTestUtils.addTab(gBrowser, "about:blank");
  gBrowser.pinTab(pinnedTab);

  let tab1 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank");

  let splitview = gBrowser.addTabSplitView([tab1, tab2], {
    insertBefore: tab1,
  });

  let tabScrollBoxChildren = Array.from(
    document.getElementById("tabbrowser-arrowscrollbox").children
  );
  let splitViewPosition = tabScrollBoxChildren.indexOf(splitview);

  Assert.ok(
    tab1.splitview && tab2.splitview,
    "Tab 1 and tab 2 are in a split view"
  );
  Assert.equal(pinnedTab._tPos, 0, "Pinned tab is at position 0");
  Assert.less(
    pinnedTab._tPos,
    splitViewPosition,
    "Pinned tab is before split view"
  );

  info("Create a tab group from the pinned tab using TabContextMenu");
  TabContextMenu.contextTab = pinnedTab;
  TabContextMenu.contextTabs = [pinnedTab];
  TabContextMenu.moveTabsToNewGroup();

  let group = pinnedTab.group;
  Assert.ok(group, "Pinned tab is now in a group");

  let groupPosition = tabScrollBoxChildren.indexOf(group);

  Assert.less(
    groupPosition,
    splitViewPosition,
    "Tab group is before split view"
  );

  Assert.equal(splitview.tabs.length, 2, "Split view still only has 2 tabs");

  await removeTabGroup(group);
  splitview.close();
  BrowserTestUtils.removeTab(pinnedTab);
});

add_task(async function test_move_splitview_to_end_and_start() {
  info("Create tabs");
  const startingTab = gBrowser.tabs[0];
  const tab1 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  const tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  const tab3 = BrowserTestUtils.addTab(gBrowser, "about:blank");
  const tab4 = BrowserTestUtils.addTab(gBrowser, "about:blank");

  let tabContainer = document.getElementById("tabbrowser-tabs");
  let splitViewCreated = BrowserTestUtils.waitForEvent(
    tabContainer,
    "SplitViewCreated"
  );
  const splitView = gBrowser.addTabSplitView([tab2, tab3], {
    insertBefore: tab2,
  });
  await splitViewCreated;

  Assert.deepEqual(
    gBrowser.tabs,
    [startingTab, tab1, tab2, tab3, tab4],
    "Confirm starting order of tabs"
  );

  info("Test moving splitview to end");
  gBrowser.moveTabToEnd(tab2);

  Assert.deepEqual(
    gBrowser.tabs,
    [startingTab, tab1, tab4, tab2, tab3],
    "Tabs are in expected order after moving splitview to end"
  );
  Assert.ok(
    tab2.splitview === splitView && tab3.splitview === splitView,
    "Tabs are still in the same splitview"
  );

  gBrowser.moveTabToStart(tab3);

  Assert.deepEqual(
    gBrowser.tabs,
    [tab2, tab3, startingTab, tab1, tab4],
    "Tabs are in expected order after moving splitview to start"
  );
  Assert.ok(
    tab2.splitview === splitView && tab3.splitview === splitView,
    "Tabs are still in the same splitview"
  );

  info("Add splitview to a tab group");
  let groupCreated = BrowserTestUtils.waitForEvent(window, "TabGroupCreate");
  const group = gBrowser.addTabGroup([splitView]);
  await groupCreated;
  Assert.strictEqual(tab2.group, group, "tab2 is in group");
  Assert.strictEqual(tab3.group, group, "tab3 is in group");

  info("Test moving splitview in group to end");
  gBrowser.moveTabToEnd(tab2);

  Assert.ok(!tab2.group, "tab2 is no longer in a group after moveTabToEnd");
  Assert.ok(!tab3.group, "tab3 is no longer in a group after moveTabToEnd");
  Assert.ok(
    tab2._tPos > startingTab._tPos && tab3._tPos > startingTab._tPos,
    "Splitview tabs are after startingTab"
  );
  Assert.ok(
    tab2._tPos > tab1._tPos && tab3._tPos > tab1._tPos,
    "Splitview tabs are after tab1"
  );
  Assert.ok(
    tab2._tPos > tab4._tPos && tab3._tPos > tab4._tPos,
    "Splitview tabs are after tab4"
  );
  Assert.ok(
    tab2.splitview === splitView && tab3.splitview === splitView,
    "Tabs are still in the same splitview after being ungrouped"
  );

  info("Clean up tabs");
  for (let tab of [tab1, tab2, tab3, tab4]) {
    BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function test_width_preserved_between_splitviews() {
  info("Create four tabs for two split view pairings.");
  const tab1 = await addTabAndLoadBrowser();
  const tab2 = await addTabAndLoadBrowser();
  const tab3 = await addTabAndLoadBrowser();
  const tab4 = await addTabAndLoadBrowser();

  info("Create two split views [1, 2] and [3, 4].");
  const splitView1 = gBrowser.addTabSplitView([tab1, tab2]);
  const splitView2 = gBrowser.addTabSplitView([tab3, tab4]);

  info("Switch to tab 1 to activate the first split view.");
  await BrowserTestUtils.switchTab(gBrowser, tab1);
  await checkSplitViewPanelVisible(tab1, true);

  const panel1 = document.getElementById(tab1.linkedPanel);
  const originalWidth = panel1.getBoundingClientRect().width;

  info("Switch to tab 3 to activate the second split view.");
  await BrowserTestUtils.switchTab(gBrowser, tab3);
  await checkSplitViewPanelVisible(tab1, false);
  await checkSplitViewPanelVisible(tab3, true);

  info("Switch back to tab 1.");
  await BrowserTestUtils.switchTab(gBrowser, tab1);
  await checkSplitViewPanelVisible(tab1, true);

  Assert.equal(
    panel1.getBoundingClientRect().width,
    originalWidth,
    "Panel 1 width is unchanged after switching between split views."
  );

  splitView1.close();
  splitView2.close();
});

/**
 * Tests that while both split view tabs are not active, showing and hiding
 * one of the tabs updates the entire split view - its visibility and whether
 * it can be dragged.
 */
add_task(async function test_show_hide_split_view_tab_affects_whole_view() {
  const tab1 = await addTabAndLoadBrowser();
  const tab2 = await addTabAndLoadBrowser();
  const splitView = gBrowser.addTabSplitView([tab1, tab2]);

  // Select a non split view tab.
  gBrowser.selectedTab = gBrowser.tabs[0];
  Assert.ok(
    !splitView.tabs.includes(gBrowser.selectedTab),
    "Selected tab is outside the split view"
  );
  Assert.ok(
    splitView.visible,
    "All split view tabs are considered visible before hiding a tab"
  );
  Assert.ok(
    BrowserTestUtils.isVisible(splitView),
    "Split view container is visible on the tab strip"
  );
  Assert.ok(
    gBrowser.tabContainer.dragAndDropElements.includes(splitView),
    "Visible split view is included in dragAndDropElements"
  );

  info("Hiding one tab hides the whole view");
  gBrowser.hideTab(tab1);

  Assert.ok(tab1.hidden, "Explicitly hidden tab is hidden");
  Assert.ok(tab2.hidden, "Sibling tab is hidden too");
  Assert.ok(
    !splitView.visible,
    "Not all split view tabs are considered visible after hiding a tab"
  );
  await BrowserTestUtils.waitForMutationCondition(
    splitView,
    { attributes: true, attributeFilter: ["hidden"] },
    () => splitView.hasAttribute("hidden")
  );
  Assert.ok(
    BrowserTestUtils.isHidden(splitView),
    "Split view container is visually hidden on the tab strip"
  );
  Assert.ok(
    !gBrowser.tabContainer.dragAndDropElements.includes(splitView),
    "Hidden split view is excluded from dragAndDropElements"
  );

  info("Showing one tab shows the whole view");
  gBrowser.showTab(tab1);

  Assert.ok(!tab1.hidden, "Shown tab is visible");
  Assert.ok(!tab2.hidden, "Sibling tab is shown too");
  Assert.ok(
    splitView.visible,
    "All split view tabs are considered visible again after showing a tab"
  );
  await BrowserTestUtils.waitForMutationCondition(
    splitView,
    { attributes: true, attributeFilter: ["hidden"] },
    () => !splitView.hasAttribute("hidden")
  );
  Assert.ok(
    BrowserTestUtils.isVisible(splitView),
    "Split view container is visible on the tab strip again"
  );
  Assert.ok(
    gBrowser.tabContainer.dragAndDropElements.includes(splitView),
    "Restored split view is included in dragAndDropElements again"
  );

  splitView.close();
});

/**
 * Tests that while one split view tab is active, hiding its sibling
 * keeps the active tab selected (a selected tab can't be hidden).
 * The split view container should still be hidden despite the active tab
 * so that a lone tab is never shown on the tab strip.
 */
add_task(async function test_hidden_split_view_sibling_keeps_active_tab() {
  const tab1 = await addTabAndLoadBrowser();
  const tab2 = await addTabAndLoadBrowser();
  const splitView = gBrowser.addTabSplitView([tab1, tab2]);

  gBrowser.selectedTab = tab1;
  Assert.ok(splitView.hasActiveTab, "Split view holds the active tab");

  info("Hide the non-selected sibling of the active split view");
  gBrowser.hideTab(tab2);

  Assert.ok(tab2.hidden, "Sibling tab is hidden");
  Assert.ok(!tab1.hidden, "Active tab is not hidden");
  Assert.ok(tab1.selected, "Active tab is still selected");
  Assert.ok(
    !splitView.visible,
    "Not all split view tabs are considered visible after hiding the sibling tab"
  );
  await BrowserTestUtils.waitForMutationCondition(
    splitView,
    { attributes: true, attributeFilter: ["hidden"] },
    () => splitView.hasAttribute("hidden")
  );
  Assert.ok(
    BrowserTestUtils.isHidden(splitView),
    "Split view container is visually hidden on the tab strip"
  );

  gBrowser.showTab(tab2);
  splitView.close();
});
