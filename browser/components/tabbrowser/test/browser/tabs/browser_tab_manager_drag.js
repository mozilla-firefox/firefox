/**
 * Test reordering the tabs in the Tab Manager, moving the tab between the
 * Tab Manager and tab bar.
 */

"use strict";

const URL1 = "data:text/plain,tab1";
const URL2 = "data:text/plain,tab2";
const URL3 = "data:text/plain,tab3";
const URL4 = "data:text/plain,tab4";
const URL5 = "data:text/plain,tab5";

const TAB_GROUP_1 = "tab-group-1";
const TAB_GROUP_2 = "tab-group-2";

registerCleanupFunction(async () => {
  await SpecialPowers.clearUserPref("browser.tabs.splitview.hasUsed");
});

/**
 * @param {(string|number)[]} order
 * @param {(string|number)[]} expected
 * @param {string} message
 */
function assertOrder(order, expected, message) {
  is(
    JSON.stringify(order),
    JSON.stringify(expected),
    `The order of the tabs ${message}`
  );
}

/**
 * Returns a numeric ID that represents a tab. The ID is based on the URL, e.g.
 * a tab with the URL `data:text/plain,tab3` has an ID of `3`.
 *
 * @see URL1 through URL5
 * @param {MozTabbrowserTab} tab
 * @returns {string|number}
 *   Numeric tab ID, or `"unknown"` if the ID can't be determined.
 */
function getTabIdFromTab(tab) {
  const url = tab.linkedBrowser.currentURI.spec ?? "";
  const m = url.match(/^data:text\/plain,tab(\d)/);
  if (m) {
    return parseInt(m[1]);
  }
  return "unknown";
}

/**
 * Returns a string ID that represents a tab group.
 *
 * @param {MozTabbrowserTabGroup} tabGroup
 * @returns {string}
 */
function getTabGroupIdFromTabGroup(tabGroup) {
  return tabGroup.id;
}

/**
 * @see TabsList.sys.mjs#getTabFromRow
 * @param {XulToolbarItem} row
 * @returns {MozTabbrowserTab}
 */
function tabOf(row) {
  return row._tab;
}

/**
 * @see TabsList.sys.mjs#getTabGroupFromRow
 * @param {XulToolbarItem} row
 * @returns {MozTabbrowserTabGroup}
 */
function tabGroupOf(row) {
  return row._tabGroup;
}

/**
 * @param {XulToolbarItem} row
 * @returns {string|number|"unknown"}
 *   A unique identifier for a tab or tab group represented by `row`:
 *   - `string` for a tab group ID (see TAB_GROUP_*).
 *   - `number` for the URL text data ID of a tab (see URL*).
 *   - "unknown" if the row didn't have tab or tab group data for some reason.
 */
function getRowId(row) {
  if (row.getAttribute("row-variant") == "tab") {
    return getTabIdFromTab(tabOf(row));
  } else if (row.getAttribute("row-variant") == "tab-group") {
    return getTabGroupIdFromTabGroup(tabGroupOf(row));
  }
  return "unknown";
}

/**
 * Returns an ordered set of IDs describing the state of the tabs list menu.
 * This state should always match the real state of the tab strip.
 *
 * @param {Element} containerNode
 *   The `containerNode` configured for an instance of `TabsList`, i.e. the root
 *   node of the menu of tabs.
 * @returns {(string|number)[]}
 */
function getTabsListOrderedIds(containerNode) {
  return [...containerNode.querySelectorAll("toolbaritem")].map(row =>
    getRowId(row)
  );
}

/**
 * Returns an ordered set of IDs describing the real state of the tab strip as
 * reported by Tabbrowser. The `TabsList` state should always match this state.
 *
 * @param {Window} win
 * @returns {(string|number)[]}
 */
function getTabStripOrderedIds(win) {
  return win.gBrowser.tabContainer.ariaFocusableItems.map(tabStripItem => {
    if (Tabbrowser.isTab(tabStripItem)) {
      return getTabIdFromTab(tabStripItem);
    }
    if (Tabbrowser.isTabGroupLabel(tabStripItem)) {
      return tabStripItem.group.id;
    }
    return "unknown";
  });
}

/**
 * @param {function(Window):Promise<void>} func
 * @param {object} [options]
 * @param {function(Window):Promise<void>} [options.beforeOpen]
 *   Runs after the tabs are added and before the All Tabs panel opens. The
 *   panel builds its rows when it opens.
 */
async function testWithNewWindow(func, { beforeOpen } = {}) {
  const newWindow = await BrowserTestUtils.openNewBrowserWindow();

  await Promise.all([
    addTabTo(newWindow.gBrowser, URL1),
    addTabTo(newWindow.gBrowser, URL2),
    addTabTo(newWindow.gBrowser, URL3),
    addTabTo(newWindow.gBrowser, URL4),
    addTabTo(newWindow.gBrowser, URL5),
  ]);

  await beforeOpen?.(newWindow);

  newWindow.gTabsPanel.init();

  const button = newWindow.document.getElementById("alltabs-button");

  const allTabsView = newWindow.document.getElementById(
    "allTabsMenu-allTabsView"
  );
  const allTabsPopupShownPromise = BrowserTestUtils.waitForEvent(
    allTabsView,
    "ViewShown"
  );
  button.click();
  await allTabsPopupShownPromise;

  await func(newWindow);

  await BrowserTestUtils.closeWindow(newWindow);
}

/**
 * Virtually drag and drop a `source` element onto the `target` element,
 * offset by `clientX`, `clientY` pixels from the top-left of the viewport.
 *
 * @param {Element} source
 * @param {Element} target
 * @param {number} clientX
 * @param {number} clientY
 * @param {Window} win
 */
async function drop(source, target, clientX, clientY, win) {
  EventUtils.synthesizeDrop(source, target, null, "move", win, win, {
    clientX,
    clientY,
  });
  await win.gTabsPanel.allTabsPanel.domRefreshComplete;
}

/**
 * Virtually drag and drop one tabs list row after another.
 *
 * @param {XulToolbarItem} rowToDrag
 * @param {XulToolbarItem} rowToDropAfter
 * @param {Window} win
 */
async function dropAfter(rowToDrag, rowToDropAfter, win) {
  const rect = rowToDropAfter.getBoundingClientRect();
  await drop(
    rowToDrag,
    rowToDropAfter,
    rect.left + 1,
    rect.top + 0.75 * rect.height,
    win
  );
}

/**
 * Virtually drag and drop one tabs list row before another.
 *
 * @param {XulToolbarItem} rowToDrag
 * @param {XulToolbarItem} rowToDropBefore
 * @param {Window} win
 */
async function dropBefore(rowToDrag, rowToDropBefore, win) {
  const rect = rowToDropBefore.getBoundingClientRect();
  await drop(
    rowToDrag,
    rowToDropBefore,
    rect.left + 1,
    rect.top + 0.25 * rect.height,
    win
  );
}

/**
 * Virtually drag and drop a tabs list row onto an element that is inside the
 * All Tabs panel but is not a row of the tabs list, for example the
 * "View All Tabs" button.
 *
 * @param {XulToolbarItem} rowToDrag
 * @param {Element} target
 * @param {Window} win
 */
async function dropOnPanelElement(rowToDrag, target, win) {
  const rect = target.getBoundingClientRect();
  await drop(
    rowToDrag,
    target,
    rect.left + 1,
    rect.top + 0.5 * rect.height,
    win
  );
}

/**
 * Virtually drag a tabs list row over an element of the All Tabs panel without
 * a drop. Call `cancelDrag` to end the drag.
 *
 * @param {XulToolbarItem} rowToDrag
 * @param {Element} target
 * @param {Window} win
 */
function dragOverPanelElement(rowToDrag, target, win) {
  const rect = target.getBoundingClientRect();
  EventUtils.startDragSession(win, "move");
  EventUtils.synthesizeDragOver(rowToDrag, target, null, "move", win, win, {
    clientX: rect.left + 1,
    clientY: rect.top + 0.5 * rect.height,
  });
}

/**
 * Ends the current drag session without a drop and without a `dragend`.
 *
 * @param {Window} win
 */
function cancelDrag(win) {
  EventUtils._getDOMWindowUtils(win).dragSession.endDragSession(false);
}

/**
 * @param {XulToolbarItem} row
 * @param {MozTabbrowserTabGroup} tabGroup
 */
function assertTabGroupLabel(row, tabGroup) {
  const rowId = getRowId(row);
  Assert.equal(tabGroupOf(row), tabGroup, `tab group ${rowId} label`);
}

/**
 * @param {XulToolbarItem} row
 */
function assertUngroupedTab(row) {
  const rowId = getRowId(row);
  Assert.ok(!tabOf(row).group, `tab ${rowId} is not in a tab group`);
}

/**
 * @param {XulToolbarItem} row
 * @param {MozTabbrowserTabGroup} tabGroup
 */
function assertGroupedTab(row, tabGroup) {
  const tabRowId = getRowId(row);
  const tabGroupId = getTabGroupIdFromTabGroup(tabGroup);
  Assert.equal(
    tabOf(row).group,
    tabGroup,
    `tab ${tabRowId} is in tab group ${tabGroupId}`
  );
}

add_task(async function test_reorder() {
  await testWithNewWindow(async function (newWindow) {
    Services.fog.testResetFOG();

    const tabsListNode = newWindow.document.getElementById(
      "allTabsMenu-allTabsView-tabs"
    );

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, 2, 3, 4, 5],
      "before reorder"
    );

    let rows;
    rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropBefore(rows[3], rows[1], newWindow);

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 3, 1, 2, 4, 5],
      "after moving up"
    );

    rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropAfter(rows[1], rows[4], newWindow);

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, 2, 4, 3, 5],
      "after moving down"
    );

    rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropBefore(rows[4], rows[3], newWindow);

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, 2, 3, 4, 5],
      "after moving up again"
    );

    Assert.equal(
      Glean.browserUiInteraction.allTabsPanelDragstartTabEventCount.testGetValue(),
      3
    );
  });
});

add_task(async function test_move_to_tab_bar() {
  await testWithNewWindow(async function (newWindow) {
    Services.fog.testResetFOG();

    const tabsListNode = newWindow.document.getElementById(
      "allTabsMenu-allTabsView-tabs"
    );

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, 2, 3, 4, 5],
      "before reorder"
    );

    let rows;
    rows = tabsListNode.querySelectorAll("toolbaritem");
    EventUtils.synthesizeDrop(
      rows[3],
      tabOf(rows[1]),
      null,
      "move",
      newWindow,
      newWindow,
      { clientX: 0, clientY: 0 }
    );

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 3, 1, 2, 4, 5],
      "after moving up with tab bar"
    );

    rows = tabsListNode.querySelectorAll("toolbaritem");
    EventUtils.synthesizeDrop(
      rows[1],
      tabOf(rows[4]),
      null,
      "move",
      newWindow,
      newWindow,
      { clientX: 0, clientY: 0 }
    );
    await newWindow.gTabsPanel.allTabsPanel.domRefreshComplete;

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, 2, 3, 4, 5],
      "after moving down with tab bar"
    );

    Assert.equal(
      Glean.browserUiInteraction.allTabsPanelDragstartTabEventCount.testGetValue(),
      2
    );
  });
});

add_task(async function test_move_to_different_tab_bar() {
  const newWindow2 = await BrowserTestUtils.openNewBrowserWindow();

  await testWithNewWindow(async function (newWindow) {
    Services.fog.testResetFOG();

    const tabsListNode = newWindow.document.getElementById(
      "allTabsMenu-allTabsView-tabs"
    );

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, 2, 3, 4, 5],
      "before reorder in newWindow"
    );
    assertOrder(
      getTabStripOrderedIds(newWindow2),
      ["unknown"],
      "before reorder in newWindow2"
    );

    let rows;
    rows = tabsListNode.querySelectorAll("toolbaritem");
    EventUtils.synthesizeDrop(
      rows[3],
      newWindow2.gBrowser.tabs[0],
      null,
      "move",
      newWindow,
      newWindow2,
      { clientX: 0, clientY: 0 }
    );
    await newWindow.gTabsPanel.allTabsPanel.domRefreshComplete;

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, 2, 4, 5],
      "after moving to other window in newWindow"
    );

    assertOrder(
      getTabStripOrderedIds(newWindow2),
      [3, "unknown"],
      "after moving to other window in newWindow2"
    );

    Assert.equal(
      Glean.browserUiInteraction.allTabsPanelDragstartTabEventCount.testGetValue(),
      1
    );
  });

  await BrowserTestUtils.closeWindow(newWindow2);
});

add_task(async function test_drag_and_drop_to_bookmark_toolbar() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.toolbars.bookmarks.visibility", "always"]],
  });

  await PlacesUtils.bookmarks.eraseEverything();
  registerCleanupFunction(async () => {
    await PlacesUtils.bookmarks.eraseEverything();
  });

  await testWithNewWindow(async function (newWindow) {
    is(
      await PlacesUtils.bookmarks.fetch({
        parentGuid: PlacesUtils.bookmarks.toolbarGuid,
        index: 0,
      }),
      null,
      "The bookmark toolbar shouldn't have any item"
    );

    const bookmarkToolbar =
      newWindow.document.getElementById("PlacesToolbarItems");

    // Wait if the bookmark toolbar initialization hasn't finished.
    await BrowserTestUtils.waitForMutationCondition(
      bookmarkToolbar,
      { attributes: true, childNodes: true, attributeFilter: ["collapsed"] },
      () => !bookmarkToolbar.collapsed && !bookmarkToolbar.childNodes.length
    );

    newWindow.gBrowser.removeTab(newWindow.gBrowser.selectedTab);

    const tabsListNode = newWindow.document.getElementById(
      "allTabsMenu-allTabsView-tabs"
    );

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      [1, 2, 3, 4, 5],
      "0-th tab is closed"
    );

    is(getTabIdFromTab(newWindow.gBrowser.selectedTab), 1, "1th tab is active");

    const { PlacesTestUtils } = ChromeUtils.importESModule(
      "resource://testing-common/PlacesTestUtils.sys.mjs"
    );

    const bookmarkPromise = PlacesTestUtils.waitForNotification(
      "bookmark-added",
      events => events.some(e => e.url == URL5)
    );

    // Drag and drop the 5st tab to the bookmark toolbar, while the active tab
    // is the 1th tab.
    const rows = tabsListNode.querySelectorAll("toolbaritem");
    EventUtils.synthesizeDrop(
      rows[4],
      bookmarkToolbar,
      null,
      "move",
      newWindow,
      newWindow,
      { clientX: 0, clientY: 0 }
    );

    await bookmarkPromise;

    is(
      (
        await PlacesUtils.bookmarks.fetch({
          parentGuid: PlacesUtils.bookmarks.toolbarGuid,
          index: 0,
        })
      ).url.href,
      URL5,
      "5th tab should be bookmarked"
    );

    is(
      await PlacesUtils.bookmarks.fetch({
        parentGuid: PlacesUtils.bookmarks.toolbarGuid,
        index: 1,
      }),
      null,
      "No other tabs should be bookmarked"
    );
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_drag_and_drop_tab_groups() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.groups.enabled", true]],
  });

  await testWithNewWindow(async function (newWindow) {
    const tabsListNode = newWindow.gTabsPanel.allTabsPanel.containerNode;

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, 2, 3, 4, 5],
      "before creating any tab groups"
    );

    const tab2 = newWindow.gBrowser.tabs.at(2);
    const tab4 = newWindow.gBrowser.tabs.at(4);

    const tabGroup1 = newWindow.gBrowser.addTabGroup([tab2], {
      id: TAB_GROUP_1,
      insertBefore: tab2,
    });

    const tabGroup2 = newWindow.gBrowser.addTabGroup([tab4], {
      id: TAB_GROUP_2,
      insertBefore: tab4,
    });

    await newWindow.gTabsPanel.allTabsPanel.domRefreshComplete;

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, TAB_GROUP_1, 2, 3, TAB_GROUP_2, 4, 5],
      "after creating the initial tab groups"
    );

    info("drag a whole tab group to the start of the tab list");
    let rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropBefore(
      rows[2], // TAB_GROUP_1 tab group label
      rows[0], // initial "unknown" tab from the test window
      newWindow
    );
    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      [TAB_GROUP_1, 2, "unknown", 1, 3, TAB_GROUP_2, 4, 5],
      "after dragging tab group 1 to the start of the tab menu"
    );

    info("drag a whole tab group to the end of the tab list");
    rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropAfter(
      rows[5], // TAB_GROUP_2 tab group label
      rows[7], // tab 5
      newWindow
    );
    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      [TAB_GROUP_1, 2, "unknown", 1, 3, 5, TAB_GROUP_2, 4],
      "after dragging tab group 2 to the end of the tab menu"
    );

    info(
      "drag a tab into the beginning of a tab group: drop after group label"
    );
    rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropAfter(
      rows[4], // tab 3
      rows[0], // TAB_GROUP_1 tab group label
      newWindow
    );
    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      [TAB_GROUP_1, 3, 2, "unknown", 1, 5, TAB_GROUP_2, 4],
      "after dragging tab 3 to the start of tab group 1"
    );
    rows = tabsListNode.querySelectorAll("toolbaritem");
    assertGroupedTab(rows[1], tabGroup1);

    info(
      "drag a tab into the beginning of a tab group: drop before first grouped tab"
    );
    rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropBefore(
      rows[5], // tab 5
      rows[1], // tab 3
      newWindow
    );
    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      [TAB_GROUP_1, 5, 3, 2, "unknown", 1, TAB_GROUP_2, 4],
      "after dragging tab 5 to the start of tab group 1"
    );
    rows = tabsListNode.querySelectorAll("toolbaritem");
    assertGroupedTab(rows[1], tabGroup1);

    info("try to drag a whole tab group above a tab in another group");
    rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropBefore(
      rows[6], // TAB_GROUP_2 tab group label
      rows[2], // tab 3
      newWindow
    );
    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      [TAB_GROUP_2, 4, TAB_GROUP_1, 5, 3, 2, "unknown", 1],
      "after dragging tab group 2 above a tab inside of tab group 1, tab group 2 should be before tab group 1"
    );

    info("try to drag a whole tab group below a tab in another group");
    rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropAfter(
      rows[0], // TAB_GROUP_2 tab group label
      rows[3], // tab 5
      newWindow
    );
    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      [TAB_GROUP_1, 5, 3, 2, TAB_GROUP_2, 4, "unknown", 1],
      "after dragging tab group 2 below a tab inside of tab group 1, tab group 2 should be after tab group 1"
    );

    info("drag a grouped tab next to an ungrouped tab");
    rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropAfter(
      rows[1], // tab 5
      rows[7], // tab 1
      newWindow
    );
    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      [TAB_GROUP_1, 3, 2, TAB_GROUP_2, 4, "unknown", 1, 5],
      "after dragging tab 5 below ungrouped tab 1, tab 5 should be the last tab"
    );
    rows = tabsListNode.querySelectorAll("toolbaritem");
    assertUngroupedTab(rows[7]);

    info("validate tab group membership for all menu items");
    rows = tabsListNode.querySelectorAll("toolbaritem");
    assertTabGroupLabel(rows[0], tabGroup1);
    assertGroupedTab(rows[1], tabGroup1);
    assertGroupedTab(rows[2], tabGroup1);
    assertTabGroupLabel(rows[3], tabGroup2);
    assertGroupedTab(rows[4], tabGroup2);
    assertUngroupedTab(rows[5]);
    assertUngroupedTab(rows[6]);
    assertUngroupedTab(rows[7]);
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_drag_and_drop_split_view() {
  await testWithNewWindow(async function (newWindow) {
    const tabsListNode = newWindow.gTabsPanel.allTabsPanel.containerNode;

    const tab3 = newWindow.gBrowser.tabs.at(3);
    const tab4 = newWindow.gBrowser.tabs.at(4);
    const splitview = newWindow.gBrowser.addTabSplitView([tab3, tab4], {
      insertBefore: tab3,
    });
    await newWindow.gTabsPanel.allTabsPanel.domRefreshComplete;

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, 2, 3, 4, 5],
      "after creating split view, order should be unchanged"
    );

    info("drag split view before tab 2");
    let rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropBefore(rows[3], rows[2], newWindow);

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, 3, 4, 2, 5],
      "after dragging split view, both tabs should appear together in the new position"
    );
    splitview.unsplitTabs();
  });
});

add_task(async function test_drag_cannot_drop_between_splitview_tabs() {
  await testWithNewWindow(async function (newWindow) {
    const tabsListNode = newWindow.gTabsPanel.allTabsPanel.containerNode;

    const tab3 = newWindow.gBrowser.tabs.at(3);
    const tab4 = newWindow.gBrowser.tabs.at(4);
    const splitview = newWindow.gBrowser.addTabSplitView([tab3, tab4], {
      insertBefore: tab3,
    });
    await newWindow.gTabsPanel.allTabsPanel.domRefreshComplete;

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, 2, 3, 4, 5],
      "verify the starting order"
    );

    info("drag tab5 to bottom half of tab3 (first splitview tab)");
    let rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropAfter(
      rows[5], // tab5
      rows[3], // tab3
      newWindow
    );

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, 2, 5, 3, 4],
      "dragging onto the bottom half of the first splitview tab should place the drop before both splitview tabs"
    );

    info("drag tab5 to bottom half of tab4 (second splitview tab)");
    rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropAfter(
      rows[3], // tab5
      rows[5], // tab4
      newWindow
    );

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, 2, 3, 4, 5],
      "dragging onto the bottom half of the second splitview tab should place the drop after both splitview tabs"
    );

    info("drag tab5 to top half of tab3 (first splitview tab)");
    rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropBefore(
      rows[5], // tab5
      rows[3], // tab3
      newWindow
    );

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, 2, 5, 3, 4],
      "dragging onto the top half of the first splitview tab should place the drop before both splitview tabs"
    );

    info("drag tab2 to top half of tab4 (second splitview tab)");
    rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropBefore(
      rows[2], // tab2
      rows[5], // tab4
      newWindow
    );

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, 5, 3, 4, 2],
      "dragging onto the top half of the second splitview tab should place the drop after both splitview tabs"
    );
    splitview.unsplitTabs();
  });
});

add_task(async function test_move_split_view_from_tab_list_to_tab_bar() {
  await testWithNewWindow(async function (newWindow) {
    const tabsListNode = newWindow.gTabsPanel.allTabsPanel.containerNode;

    const tab1 = newWindow.gBrowser.tabs.at(1);
    const tab2 = newWindow.gBrowser.tabs.at(2);
    const splitview = newWindow.gBrowser.addTabSplitView([tab1, tab2], {
      insertBefore: tab1,
    });
    await newWindow.gTabsPanel.allTabsPanel.domRefreshComplete;

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, 2, 3, 4, 5],
      "after creating split view, order should be unchanged"
    );

    info("drag split view from all tabs list to before tab5 in the tab strip");
    const rows = tabsListNode.querySelectorAll("toolbaritem");
    const tab5 = newWindow.gBrowser.tabs.at(5);
    const tab5Rect = tab5.getBoundingClientRect();
    EventUtils.synthesizeDrop(
      rows[1],
      tab5,
      null,
      "move",
      newWindow,
      newWindow,
      {
        clientX: tab5Rect.left + 1,
        clientY: tab5Rect.top + tab5Rect.height * 0.25,
      }
    );
    await newWindow.gTabsPanel.allTabsPanel.domRefreshComplete;

    assertOrder(
      getTabStripOrderedIds(newWindow),
      ["unknown", 3, 4, 1, 2, 5],
      "split view should be dropped before tab5, not before tab4"
    );

    splitview.unsplitTabs();
  });
});

/**
 * A tab dropped on an element below the tabs list moves to the end of the
 * list.
 */
add_task(async function test_drop_on_view_all_tabs() {
  await testWithNewWindow(async function (newWindow) {
    const tabsListNode = newWindow.gTabsPanel.allTabsPanel.containerNode;
    const viewAllTabs = newWindow.document.getElementById(
      "allTabsMenu-viewAllTabs"
    );

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, 2, 3, 4, 5],
      "before the drop"
    );

    const rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropOnPanelElement(rows[1], viewAllTabs, newWindow);

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 2, 3, 4, 5, 1],
      "after dropping tab 1 on the View All Tabs button"
    );
    assertOrder(
      getTabStripOrderedIds(newWindow),
      ["unknown", 2, 3, 4, 5, 1],
      "the tab strip matches the tabs list"
    );
  });
});

/**
 * A tab dropped on the separator between the tabs list and the
 * "View All Tabs" button moves to the end of the list.
 */
add_task(async function test_drop_on_footer_separator() {
  await testWithNewWindow(async function (newWindow) {
    const tabsListNode = newWindow.gTabsPanel.allTabsPanel.containerNode;
    const separator = newWindow.document.getElementById(
      "allTabsMenu-viewAllTabs"
    ).previousElementSibling;
    Assert.equal(
      separator.localName,
      "toolbarseparator",
      "the element before the View All Tabs button is a separator"
    );

    const rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropOnPanelElement(rows[1], separator, newWindow);

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 2, 3, 4, 5, 1],
      "after dropping tab 1 on the footer separator"
    );
    assertOrder(
      getTabStripOrderedIds(newWindow),
      ["unknown", 2, 3, 4, 5, 1],
      "the tab strip matches the tabs list"
    );
  });
});

/**
 * A tab dropped on an element above the tabs list moves to the start of the
 * list.
 */
add_task(async function test_drop_above_the_tabs_list() {
  await testWithNewWindow(async function (newWindow) {
    const tabsListNode = newWindow.gTabsPanel.allTabsPanel.containerNode;
    const searchTabs = newWindow.document.getElementById(
      "allTabsMenu-searchTabs"
    );

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, 2, 3, 4, 5],
      "before the drop"
    );

    const rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropOnPanelElement(rows[2], searchTabs, newWindow);

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      [2, "unknown", 1, 3, 4, 5],
      "after dropping tab 2 on the Search All Tabs button"
    );
    assertOrder(
      getTabStripOrderedIds(newWindow),
      [2, "unknown", 1, 3, 4, 5],
      "the tab strip matches the tabs list"
    );
  });
});

/**
 * A hidden tab that plays audio puts the hidden tabs button and a nested list
 * of hidden tabs at the top of the tabs list. A tab dropped on a row of that
 * nested list moves to the start of the visible tabs.
 */
add_task(async function test_drop_on_hidden_audio_tab() {
  const beforeOpen = async newWindow => {
    const tab5 = newWindow.gBrowser.tabs.at(5);
    const tabHidden = BrowserTestUtils.waitForEvent(tab5, "TabHide");
    newWindow.gBrowser.hideTab(tab5);
    await tabHidden;
    tab5.setAttribute("soundplaying", "");
  };

  const runTest = async newWindow => {
    const tabsListNode = newWindow.gTabsPanel.allTabsPanel.containerNode;
    const hiddenAudioTabs = newWindow.document.getElementById(
      "allTabsMenu-allTabsView-hiddenAudio-tabs"
    );
    Assert.ok(
      BrowserTestUtils.isVisible(hiddenAudioTabs),
      "the list of hidden tabs playing audio is visible"
    );

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      [5, "unknown", 1, 2, 3, 4],
      "before the drop"
    );

    const rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropOnPanelElement(rows[4], rows[0], newWindow);

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      [5, 3, "unknown", 1, 2, 4],
      "after dropping tab 3 on the hidden tab playing audio"
    );
    assertOrder(
      getTabStripOrderedIds(newWindow),
      [3, "unknown", 1, 2, 4],
      "the tab strip matches the tabs list"
    );
  };

  await testWithNewWindow(runTest, { beforeOpen });
});

/**
 * The tabs list scrolls separately from the rest of the panel. A tab dropped
 * above or below a scrolled list moves next to the first or the last row that
 * is in view, which is where the drop indicator shows it.
 */
add_task(async function test_drop_with_scrolled_list() {
  const beforeOpen = newWindow => {
    for (let i = 0; i < 40; i++) {
      BrowserTestUtils.addTab(newWindow.gBrowser, "about:blank", {
        skipAnimation: true,
      });
    }
  };

  const runTest = async newWindow => {
    const { gBrowser } = newWindow;
    const tabsListNode = newWindow.gTabsPanel.allTabsPanel.containerNode;
    const searchTabs = newWindow.document.getElementById(
      "allTabsMenu-searchTabs"
    );
    const viewAllTabs = newWindow.document.getElementById(
      "allTabsMenu-viewAllTabs"
    );

    const scrollToMiddle = () => {
      tabsListNode.scrollTop =
        (tabsListNode.scrollHeight - tabsListNode.clientHeight) / 2;
    };
    const getVisibleRows = () => {
      const listRect = tabsListNode.getBoundingClientRect();
      return [...tabsListNode.querySelectorAll("toolbaritem")].filter(row => {
        const rect = row.getBoundingClientRect();
        return rect.bottom > listRect.top && rect.top <= listRect.bottom - 1;
      });
    };

    scrollToMiddle();
    let visibleRows = getVisibleRows();
    const rows = [...tabsListNode.querySelectorAll("toolbaritem")];
    Assert.ok(
      !visibleRows.includes(rows[0]) && !visibleRows.includes(rows.at(-1)),
      "the first and the last rows are scrolled out of view"
    );

    let rowToDrag = visibleRows[Math.floor(visibleRows.length / 2)];
    let draggedTab = tabOf(rowToDrag);
    const firstVisibleTab = tabOf(visibleRows[0]);
    await dropOnPanelElement(rowToDrag, searchTabs, newWindow);
    Assert.equal(
      gBrowser.tabs.indexOf(draggedTab) + 1,
      gBrowser.tabs.indexOf(firstVisibleTab),
      "a tab dropped above the list moves before the first visible row"
    );

    scrollToMiddle();
    visibleRows = getVisibleRows();
    rowToDrag = visibleRows[Math.floor(visibleRows.length / 2)];
    draggedTab = tabOf(rowToDrag);
    const lastVisibleTab = tabOf(visibleRows.at(-1));
    await dropOnPanelElement(rowToDrag, viewAllTabs, newWindow);
    Assert.equal(
      gBrowser.tabs.indexOf(draggedTab) - 1,
      gBrowser.tabs.indexOf(lastVisibleTab),
      "a tab dropped below the list moves after the last visible row"
    );
  };

  await testWithNewWindow(runTest, { beforeOpen });
});

/**
 * When the tabs list is scrolled, the first and the last row can be out of
 * view. For a drag above or below the list, the drop indicator stays at the
 * edge of the visible list.
 */
add_task(async function test_drop_indicator_with_scrolled_list() {
  const beforeOpen = newWindow => {
    for (let i = 0; i < 40; i++) {
      BrowserTestUtils.addTab(newWindow.gBrowser, "about:blank", {
        skipAnimation: true,
      });
    }
  };

  const runTest = async newWindow => {
    const allTabsPanel = newWindow.gTabsPanel.allTabsPanel;
    const tabsListNode = allTabsPanel.containerNode;
    const searchTabs = newWindow.document.getElementById(
      "allTabsMenu-searchTabs"
    );
    const viewAllTabs = newWindow.document.getElementById(
      "allTabsMenu-viewAllTabs"
    );

    tabsListNode.scrollTop =
      (tabsListNode.scrollHeight - tabsListNode.clientHeight) / 2;
    const listRect = tabsListNode.getBoundingClientRect();
    const rows = [...tabsListNode.querySelectorAll("toolbaritem")];
    Assert.less(
      rows[0].getBoundingClientRect().bottom,
      listRect.top,
      "the first row is scrolled out of view above the list"
    );
    Assert.greater(
      rows.at(-1).getBoundingClientRect().top,
      listRect.bottom,
      "the last row is scrolled out of view below the list"
    );
    const visibleRow = rows.find(
      row => row.getBoundingClientRect().top >= listRect.top
    );

    const assertIndicatorAt = (edgeY, message) => {
      Assert.ok(!allTabsPanel.dropIndicator.collapsed, "the indicator shows");
      const rect = allTabsPanel.dropIndicator.getBoundingClientRect();
      Assert.lessOrEqual(
        Math.abs(rect.top + rect.height / 2 - edgeY),
        rect.height,
        message
      );
    };

    dragOverPanelElement(visibleRow, searchTabs, newWindow);
    assertIndicatorAt(
      listRect.top,
      "the indicator is at the top edge of the list"
    );
    cancelDrag(newWindow);

    dragOverPanelElement(visibleRow, viewAllTabs, newWindow);
    assertIndicatorAt(
      listRect.bottom,
      "the indicator is at the bottom edge of the list"
    );
    cancelDrag(newWindow);
  };

  await testWithNewWindow(runTest, { beforeOpen });
});

/**
 * A tab dropped below the tabs list does not join an expanded group at the end
 * of the list.
 */
add_task(async function test_drop_below_trailing_tab_group() {
  const beforeOpen = newWindow => {
    const tab5 = newWindow.gBrowser.tabs.at(5);
    newWindow.gBrowser.addTabGroup([tab5], {
      id: TAB_GROUP_1,
      insertBefore: tab5,
    });
  };

  const runTest = async newWindow => {
    const tabsListNode = newWindow.gTabsPanel.allTabsPanel.containerNode;
    const viewAllTabs = newWindow.document.getElementById(
      "allTabsMenu-viewAllTabs"
    );

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 1, 2, 3, 4, TAB_GROUP_1, 5],
      "before the drop"
    );

    const rows = tabsListNode.querySelectorAll("toolbaritem");
    const tab1 = tabOf(rows[1]);
    await dropOnPanelElement(rows[1], viewAllTabs, newWindow);

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 2, 3, 4, TAB_GROUP_1, 5, 1],
      "after dropping tab 1 on the View All Tabs button"
    );
    Assert.ok(!tab1.group, "tab 1 is not in the tab group");
    assertOrder(
      getTabStripOrderedIds(newWindow),
      ["unknown", 2, 3, 4, TAB_GROUP_1, 5, 1],
      "the tab strip matches the tabs list"
    );
  };

  await testWithNewWindow(runTest, { beforeOpen });
});

/**
 * A tab dropped above a scrolled tabs list does not join an expanded group
 * whose first tab is the first visible row.
 */
add_task(async function test_drop_above_leading_tab_group() {
  let tabGroup;
  const beforeOpen = newWindow => {
    const { gBrowser } = newWindow;
    for (let i = 0; i < 40; i++) {
      BrowserTestUtils.addTab(gBrowser, "about:blank", {
        skipAnimation: true,
      });
    }
    const groupTabs = gBrowser.tabs.slice(20, 22);
    tabGroup = gBrowser.addTabGroup(groupTabs, {
      id: TAB_GROUP_1,
      insertBefore: groupTabs[0],
    });
  };

  const runTest = async newWindow => {
    const { gBrowser } = newWindow;
    const tabsListNode = newWindow.gTabsPanel.allTabsPanel.containerNode;
    const searchTabs = newWindow.document.getElementById(
      "allTabsMenu-searchTabs"
    );

    const rows = [...tabsListNode.querySelectorAll("toolbaritem")];
    const labelRow = rows.find(row => tabGroupOf(row) == tabGroup);
    const firstGroupTab = tabGroup.tabs[0];
    tabsListNode.scrollTop +=
      labelRow.getBoundingClientRect().bottom -
      tabsListNode.getBoundingClientRect().top +
      1;

    const listRect = tabsListNode.getBoundingClientRect();
    const visibleRows = rows.filter(row => {
      const rect = row.getBoundingClientRect();
      return rect.bottom > listRect.top && rect.top <= listRect.bottom - 1;
    });
    Assert.equal(
      tabOf(visibleRows[0]),
      firstGroupTab,
      "the first visible row is the first tab of the tab group"
    );

    const rowToDrag = visibleRows[Math.floor(visibleRows.length / 2)];
    const draggedTab = tabOf(rowToDrag);
    Assert.ok(!draggedTab.group, "the dragged tab is not in the tab group");
    await dropOnPanelElement(rowToDrag, searchTabs, newWindow);

    Assert.ok(!draggedTab.group, "the dragged tab does not join the tab group");
    Assert.equal(
      gBrowser.tabs.indexOf(draggedTab) + 1,
      gBrowser.tabs.indexOf(firstGroupTab),
      "the dragged tab moves before the tab group"
    );
  };

  await testWithNewWindow(runTest, { beforeOpen });
});

/**
 * A hidden tab that does not play audio puts the hidden tabs button at the end
 * of the tabs list. A tab dropped on that button does not join an expanded
 * group at the end of the list.
 */
add_task(
  async function test_drop_on_hidden_tabs_button_with_trailing_tab_group() {
    const beforeOpen = async newWindow => {
      const { gBrowser } = newWindow;
      const tab4 = gBrowser.tabs.at(4);
      const tabHidden = BrowserTestUtils.waitForEvent(tab4, "TabHide");
      gBrowser.hideTab(tab4);
      await tabHidden;
      const tab5 = gBrowser.tabs.at(5);
      gBrowser.addTabGroup([tab5], {
        id: TAB_GROUP_1,
        insertBefore: tab5,
      });
    };

    const runTest = async newWindow => {
      const tabsListNode = newWindow.gTabsPanel.allTabsPanel.containerNode;
      const hiddenTabsButton = newWindow.document.getElementById(
        "allTabsMenu-hiddenTabsButton"
      );
      Assert.equal(
        hiddenTabsButton.parentNode,
        tabsListNode,
        "the hidden tabs button is in the tabs list"
      );
      Assert.ok(
        BrowserTestUtils.isVisible(hiddenTabsButton),
        "the hidden tabs button is visible"
      );

      assertOrder(
        getTabsListOrderedIds(tabsListNode),
        ["unknown", 1, 2, 3, TAB_GROUP_1, 5],
        "before the drop"
      );

      const rows = tabsListNode.querySelectorAll("toolbaritem");
      const tab1 = tabOf(rows[1]);
      await dropOnPanelElement(rows[1], hiddenTabsButton, newWindow);

      assertOrder(
        getTabsListOrderedIds(tabsListNode),
        ["unknown", 2, 3, TAB_GROUP_1, 5, 1],
        "after dropping tab 1 on the hidden tabs button"
      );
      Assert.ok(!tab1.group, "tab 1 is not in the tab group");
    };

    await testWithNewWindow(runTest, { beforeOpen });
  }
);

/**
 * A row that moves to the end of the tabs list stays above the hidden tabs
 * button. A tab dropped on the button then moves to the end of the list.
 */
add_task(async function test_hidden_tabs_button_stays_after_moved_rows() {
  const beforeOpen = async newWindow => {
    const tab4 = newWindow.gBrowser.tabs.at(4);
    const tabHidden = BrowserTestUtils.waitForEvent(tab4, "TabHide");
    newWindow.gBrowser.hideTab(tab4);
    await tabHidden;
  };

  const runTest = async newWindow => {
    const tabsListNode = newWindow.gTabsPanel.allTabsPanel.containerNode;
    const hiddenTabsButton = newWindow.document.getElementById(
      "allTabsMenu-hiddenTabsButton"
    );
    const viewAllTabs = newWindow.document.getElementById(
      "allTabsMenu-viewAllTabs"
    );

    let rows = tabsListNode.querySelectorAll("toolbaritem");
    await dropOnPanelElement(rows[1], viewAllTabs, newWindow);

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 2, 3, 5, 1],
      "after dropping tab 1 on the View All Tabs button"
    );
    rows = tabsListNode.querySelectorAll("toolbaritem");
    const children = [...tabsListNode.children];
    Assert.greater(
      children.indexOf(hiddenTabsButton),
      children.indexOf(rows[rows.length - 1]),
      "the hidden tabs button is after the last row"
    );

    // A drop on the hidden tabs button after the move resolves to the last
    // row, so the tab moves to the end of the list.
    await dropOnPanelElement(rows[1], hiddenTabsButton, newWindow);

    assertOrder(
      getTabsListOrderedIds(tabsListNode),
      ["unknown", 3, 5, 1, 2],
      "after dropping tab 2 on the hidden tabs button"
    );
  };

  await testWithNewWindow(runTest, { beforeOpen });
});
