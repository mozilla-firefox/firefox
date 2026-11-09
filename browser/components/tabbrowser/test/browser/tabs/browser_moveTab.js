/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Opens tabs A, B and C at the end of the tab strip.
 * Returns an object of MozTabbrowserTabs identified by
 * their name, plus helper functions to group tabs,
 * split tabs, assert the order and clean up the tabs.
 *
 * @returns {{tabs: object, addGroup: Function, addSplitView: Function, assertOrder: Function, cleanup: Function}}
 */
function setupTest() {
  let tabs = {};
  let tabNames = new Map();
  for (let name of ["A", "B", "C"]) {
    let tab = BrowserTestUtils.addTab(gBrowser, "about:blank");
    tabs[name] = tab;
    tabNames.set(tab, name);
  }

  /**
   * Asserts the order of test tabs A, B, and C.
   * Ignores unrelated tabs and counts hidden tabs.
   *
   * @param {Array<string>} expected
   *  An array of tab names representing the order we want.
   * @param {string} message
   *  The assertion message to display when we compare the actual
   *  and expected order.
   */
  let assertOrder = (expected, message) => {
    let tabsToTestOnly = gBrowser.tabs.filter(tab => tabNames.has(tab));
    let actual = tabsToTestOnly.map(tab => tabNames.get(tab));
    Assert.deepEqual(actual, expected, message);
  };

  let groups = [];

  /**
   * Groups the given tabs. The new group is added at the end of the tab strip.
   *
   * @param {Array<MozTabbrowserTab>} tabsToGroup
   *  The tabs we want to group.
   * @returns {MozTabbrowserTabGroup}
   *  The tab group
   */
  let addGroup = tabsToGroup => {
    let group = gBrowser.addTabGroup(tabsToGroup);
    groups.push(group);
    return group;
  };

  /**
   * Puts the given tabs in a splitview. The new splitview is added at the
   * end of the tab strip. It is removed when its tabs are closed.
   *
   * @param {Array<MozTabbrowserTab>} tabsToSplit
   *  The two tabs we want to show side by side.
   * @returns {MozTabSplitViewWrapper}
   *  The splitview
   */
  let addSplitView = tabsToSplit => gBrowser.addTabSplitView(tabsToSplit);

  let cleanup = async () => {
    for (let group of groups) {
      await removeTabGroup(group);
    }
    for (let tab of tabNames.keys()) {
      if (tab.isConnected) {
        BrowserTestUtils.removeTab(tab);
      }
    }
  };

  assertOrder(["A", "B", "C"], "Tabs start in order: A, B, C");
  return { tabs, addGroup, addSplitView, assertOrder, cleanup };
}

/**
 * Asserts that every pinned tab comes before every unpinned tab.
 */
function assertPinnedTabsFirst() {
  let pinnedTabCount = gBrowser.pinnedTabCount;
  Assert.ok(
    gBrowser.tabs.every((tab, i) => tab.pinned == i < pinnedTabCount),
    "All pinned tabs come before all unpinned tabs"
  );
}

add_task(async function test_move_before() {
  let { tabs, assertOrder, cleanup } = setupTest();
  let { A, B, C } = tabs;

  gBrowser.moveTabBefore(B, A);
  assertOrder(
    ["B", "A", "C"],
    "moveTabBefore moved B before A. New order: B, A, C"
  );

  gBrowser.moveTabsBefore([C, A], B);
  assertOrder(
    ["C", "A", "B"],
    "moveTabsBefore moved C and A before B. New order: C, A, B"
  );

  // Test non-adjacent tabs
  gBrowser.moveTabsBefore([C, B], A);
  assertOrder(
    ["C", "B", "A"],
    "moveTabsBefore moved non-adjacent C and B before A. New order: C, B, A"
  );

  await cleanup();
});

add_task(async function test_move_after() {
  let { tabs, assertOrder, cleanup } = setupTest();
  let { A, B, C } = tabs;

  gBrowser.moveTabAfter(A, B);
  assertOrder(
    ["B", "A", "C"],
    "moveTabAfter moved A after B. New order: B, A, C"
  );

  gBrowser.moveTabsAfter([B, A], C);
  assertOrder(
    ["C", "B", "A"],
    "moveTabsAfter moved B and A after C. New order: C, B, A"
  );

  // Test non-adjacent tabs
  gBrowser.moveTabsAfter([C, A], B);
  assertOrder(
    ["B", "C", "A"],
    "moveTabsAfter moved non-adjacent C and A after B. New order: B, C, A"
  );

  await cleanup();
});

// Bug 1956252: an unpinned tab must not be moved in between pinned tabs.
// Bug 1955388: a pinned tab must not be moved in between unpinned tabs.
add_task(async function test_move_pinned_and_unpinned_tabs() {
  let { tabs, assertOrder, cleanup } = setupTest();
  let { A, B, C } = tabs;

  gBrowser.pinTab(A);
  gBrowser.pinTab(B);
  assertOrder(["A", "B", "C"], "A and B are pinned. Order: A*, B*, C");

  gBrowser.moveTabBefore(C, B);
  Assert.ok(!C.pinned, "C is still unpinned");
  Assert.equal(
    C.index,
    gBrowser.pinnedTabCount,
    "C was moved to be the first unpinned tab instead of in between A and B"
  );
  assertPinnedTabsFirst();
  assertOrder(["A", "B", "C"], "Order is still: A*, B*, C");

  gBrowser.moveTabAfter(A, C);
  Assert.ok(A.pinned, "A is still pinned");
  Assert.equal(
    A.index,
    gBrowser.pinnedTabCount - 1,
    "A was moved to be the last pinned tab instead of after C"
  );
  assertPinnedTabsFirst();
  assertOrder(["B", "A", "C"], "New order: B*, A*, C");

  await cleanup();
});

// Bug 1955388: a pinned tab must not be moved in between unpinned tabs when
// the target is a hidden tab.
add_task(async function test_move_pinned_tab_next_to_hidden_tab() {
  let { tabs, assertOrder, cleanup } = setupTest();
  let { A, B, C } = tabs;

  gBrowser.pinTab(A);
  gBrowser.pinTab(B);
  gBrowser.moveTabBefore(C, gBrowser.tabs[gBrowser.pinnedTabCount]);
  gBrowser.hideTab(C);
  Assert.ok(C.hidden, "C is hidden");
  Assert.equal(
    C.index,
    gBrowser.pinnedTabCount,
    "Hidden C is right after the pinned tabs"
  );

  gBrowser.moveTabBefore(A, C);
  Assert.ok(A.pinned, "A is still pinned");
  Assert.equal(gBrowser.pinnedTabCount, 2, "Pinned tab count is correct");
  assertPinnedTabsFirst();
  assertOrder(
    ["B", "A", "C"],
    "A was moved to be the last pinned tab. New order: B*, A*, _C_"
  );

  gBrowser.moveTabAfter(B, C);
  Assert.ok(B.pinned, "B is still pinned");
  Assert.equal(gBrowser.pinnedTabCount, 2, "Pinned tab count is correct");
  assertPinnedTabsFirst();
  assertOrder(
    ["A", "B", "C"],
    "B was moved to be the last pinned tab. New order: A*, B*, _C_"
  );

  await cleanup();
});

// Bug 1908439 and bug 1954346: a tab group, or its label standing in for it,
// can be moved next to a tab.
add_task(async function test_move_tab_group() {
  let { tabs, addGroup, assertOrder, cleanup } = setupTest();
  let { A, B, C } = tabs;

  let group = addGroup([B, C]);
  assertOrder(["A", "B", "C"], "B and C are grouped. Order: A, [B, C]");

  gBrowser.moveTabBefore(group.labelElement, A);
  Assert.equal(B.group, group, "B is still in the group");
  Assert.equal(C.group, group, "C is still in the group");
  Assert.ok(!A.group, "A was not added to the group");
  assertOrder(
    ["B", "C", "A"],
    "Group label moved the group before A. New order: [B, C], A"
  );

  gBrowser.moveTabAfter(group, A);
  Assert.ok(!A.group, "A was not added to the group");
  assertOrder(["A", "B", "C"], "Group moved after A. New order: A, [B, C]");

  await cleanup();
});

// Bug 1908439: moving a tab group next to a tab in another group moves it
// next to that other group instead of nesting it.
add_task(async function test_move_tab_group_next_to_grouped_tab() {
  let { tabs, addGroup, assertOrder, cleanup } = setupTest();
  let { A, B, C } = tabs;

  let groupA = addGroup([A]);
  let groupBC = addGroup([B, C]);
  assertOrder(["A", "B", "C"], "Groups are [A] and [B, C]. Order: [A], [B, C]");

  gBrowser.moveTabAfter(groupA.labelElement, C);
  Assert.equal(A.group, groupA, "A is still in its group");
  Assert.equal(
    groupA.parentElement,
    groupBC.parentElement,
    "Group [A] was not nested in group [B, C]"
  );
  assertOrder(
    ["B", "C", "A"],
    "Group [A] moved after group [B, C]. New order: [B, C], [A]"
  );

  gBrowser.moveTabBefore(groupA.labelElement, B);
  Assert.equal(A.group, groupA, "A is still in its group");
  Assert.equal(groupBC.tabs.length, 2, "Group [B, C] still has 2 tabs");
  assertOrder(
    ["A", "B", "C"],
    "Group [A] moved before group [B, C]. New order: [A], [B, C]"
  );

  await cleanup();
});

// Bug 1954346: moving a tab after an expanded group's label makes it the
// group's first tab. For a collapsed group, the tab is moved after the group.
add_task(async function test_move_tab_after_group_label() {
  let { tabs, addGroup, assertOrder, cleanup } = setupTest();
  let { A, B, C } = tabs;

  let group = addGroup([B, C]);
  group.collapsed = true;
  info("Grouped and collapsed [B, C]");

  gBrowser.moveTabAfter(A, group.labelElement);
  Assert.ok(!A.group, "A was not added to the collapsed group");
  assertOrder(["B", "C", "A"], "A moved after the collapsed group");

  group.collapsed = false;
  info("Uncollapsed [B, C]");

  gBrowser.moveTabAfter(A, group.labelElement);
  Assert.equal(A.group, group, "A was added to the expanded group");
  assertOrder(
    ["A", "B", "C"],
    "A moved to the start of the expanded group, just after the group label. New order: [A, B, C]"
  );

  await cleanup();
});

// Bug 1954346: moving a tab group next to a pinned tab moves it to be the
// first unpinned element instead.
add_task(async function test_move_tab_group_next_to_pinned_tab() {
  let { tabs, addGroup, assertOrder, cleanup } = setupTest();
  let { A, B, C } = tabs;

  gBrowser.pinTab(A);
  let group = addGroup([B, C]);
  info("Pinned A and grouped [B, C]");

  gBrowser.moveTabBefore(group.labelElement, A);
  Assert.ok(A.pinned, "A is still pinned");
  Assert.ok(!B.pinned && !C.pinned, "Grouped tabs were not pinned");
  Assert.equal(
    B.index,
    gBrowser.pinnedTabCount,
    "The group was moved to be the first unpinned element"
  );
  assertPinnedTabsFirst();
  assertOrder(["A", "B", "C"], "Order: A*, [B, C]");

  await cleanup();
});

// Bug 2009564: a splitview and tab can be moved next to each other
// without changing the contents of the splitview.
// The splitview only changes when we pull one of its tabs out of the view.
// Note: whether callers should be able to separate splitviews this way
// could change with Bug 2024173.
add_task(async function test_move_splitview_and_tab() {
  let { tabs, addSplitView, assertOrder, cleanup } = setupTest();
  let { A, B, C } = tabs;

  let splitview = addSplitView([B, C]);

  gBrowser.moveTabBefore(splitview, A);
  Assert.equal(B.splitview, splitview, "B is still in the splitview");
  Assert.equal(C.splitview, splitview, "C is still in the splitview");
  assertOrder(["B", "C", "A"], "Splitview moved before A: (B, C), A");

  gBrowser.moveTabAfter(splitview, A);
  Assert.ok(!A.splitview, "A was not added to the splitview");
  Assert.equal(splitview.tabs.length, 2, "splitview still has 2 tabs");
  assertOrder(["A", "B", "C"], "The splitview moved after A: A, (B, C)");

  // Now pull a splitview tab out
  let splitViewRemoved = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "SplitViewRemoved"
  );
  gBrowser.moveTabBefore(B, A);
  await splitViewRemoved;
  info("Removed splitview by pulling tab B out");

  Assert.ok(!B.splitview, "B is no longer in a splitview");
  Assert.ok(!C.splitview, "C is no longer in a splitview");
  Assert.ok(!splitview.isConnected, "The splitview was removed");
  assertOrder(
    ["B", "A", "C"],
    "B moved before A and C stayed in place: B, A, C"
  );

  await cleanup();
});

add_task(async function test_move_splitview_after_group_label() {
  let { tabs, addGroup, addSplitView, assertOrder, cleanup } = setupTest();
  let { A, B, C } = tabs;

  let group = addGroup([A]);
  let splitview = addSplitView([B, C]);
  gBrowser.moveTabBefore(splitview, group);
  assertOrder(["B", "C", "A"], "Order: (B, C), [A]");

  group.collapsed = true;
  info("Grouped and collapsed [A]");
  gBrowser.moveTabAfter(splitview, group.labelElement);
  Assert.ok(!splitview.group, "Splitview was not added to the collapsed group");
  Assert.equal(B.splitview, splitview, "B is still in the splitview");
  Assert.equal(C.splitview, splitview, "C is still in the splitview");
  assertOrder(
    ["A", "B", "C"],
    "Splitview moved after the collapsed group: [A], (B, C)"
  );

  group.collapsed = false;
  info("Uncollapsed [A]");
  gBrowser.moveTabAfter(splitview, group.labelElement);
  Assert.equal(splitview.group, group, "Splitview was added to the group");
  Assert.equal(B.splitview, splitview, "B is still in the splitview");
  Assert.equal(C.splitview, splitview, "C is still in the splitview");
  assertOrder(
    ["B", "C", "A"],
    "Splitview moved to the start of the group, just after the group label. New order: [(B, C), A]"
  );

  await cleanup();
});

add_task(async function test_move_splitview_next_to_pinned_tab() {
  let { tabs, addSplitView, assertOrder, cleanup } = setupTest();
  let { A, B, C } = tabs;

  gBrowser.pinTab(A);
  let splitview = addSplitView([B, C]);
  info("Pinned A, split B and C");

  gBrowser.moveTabBefore(splitview, A);
  Assert.ok(!B.pinned && !C.pinned, "Splitview tabs were not pinned");
  Assert.equal(
    B.index,
    gBrowser.pinnedTabCount,
    "Splitview was moved to be the first unpinned element"
  );
  Assert.equal(B.splitview, splitview, "B is still in the splitview");
  Assert.equal(C.splitview, splitview, "C is still in the splitview");
  assertPinnedTabsFirst();
  assertOrder(["A", "B", "C"], "Order: A*, [B, C]");

  await cleanup();
});
