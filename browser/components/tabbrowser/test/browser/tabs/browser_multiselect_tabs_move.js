add_task(async function testMoveStartEnabledClickedFromNonSelectedTab() {
  let tab = gBrowser.selectedTab;
  let tab2 = await addTab();
  let tab3 = await addTab();

  let tabs = [tab2, tab3];

  let menuItemMoveStartTab = document.getElementById("context_moveToStart");

  await triggerClickOn(tab, {});
  await triggerClickOn(tab2, { ctrlKey: true });

  ok(tab.multiselected, "Tab is multiselected");
  ok(tab2.multiselected, "Tab2 is multiselected");

  updateTabContextMenu(tab3);
  is(menuItemMoveStartTab.disabled, false, "Move Tab to Start is enabled");

  for (let tabToRemove of tabs) {
    BrowserTestUtils.removeTab(tabToRemove);
  }
});

add_task(async function testMoveStartDisabledFromFirstUnpinnedTab() {
  let tab = gBrowser.selectedTab;
  let tab2 = await addTab();

  let menuItemMoveStartTab = document.getElementById("context_moveToStart");

  gBrowser.pinTab(tab);

  updateTabContextMenu(tab2);
  is(menuItemMoveStartTab.disabled, true, "Move Tab to Start is disabled");

  BrowserTestUtils.removeTab(tab2);
  gBrowser.unpinTab(tab);
});

add_task(async function testMoveStartDisabledFromFirstPinnedTab() {
  let tab = gBrowser.selectedTab;
  let tab2 = await addTab();

  let menuItemMoveStartTab = document.getElementById("context_moveToStart");

  gBrowser.pinTab(tab);

  updateTabContextMenu(tab);
  is(menuItemMoveStartTab.disabled, true, "Move Tab to Start is disabled");

  BrowserTestUtils.removeTab(tab2);
  gBrowser.unpinTab(tab);
});

add_task(async function testMoveStartDisabledFromOnlyTab() {
  let tab = gBrowser.selectedTab;

  let menuItemMoveStartTab = document.getElementById("context_moveToStart");

  updateTabContextMenu(tab);
  is(menuItemMoveStartTab.disabled, true, "Move Tab to Start is disabled");
});

add_task(async function testMoveStartDisabledFromOnlyPinnedTab() {
  let tab = gBrowser.selectedTab;

  let menuItemMoveStartTab = document.getElementById("context_moveToStart");

  gBrowser.pinTab(tab);

  updateTabContextMenu(tab);
  is(menuItemMoveStartTab.disabled, true, "Move Tab to Start is disabled");

  gBrowser.unpinTab(tab);
});

add_task(async function testMoveStartEnabledFromLastPinnedTab() {
  let tab = gBrowser.selectedTab;
  let tab2 = await addTab();
  let tab3 = await addTab();

  let tabs = [tab2, tab3];

  let menuItemMoveStartTab = document.getElementById("context_moveToStart");

  gBrowser.pinTab(tab);
  gBrowser.pinTab(tab2);

  updateTabContextMenu(tab2);
  is(menuItemMoveStartTab.disabled, false, "Move Tab to Start is enabled");

  for (let tabToRemove of tabs) {
    BrowserTestUtils.removeTab(tabToRemove);
  }

  gBrowser.unpinTab(tab);
});

add_task(async function testMoveStartDisabledFromFirstVisibleTab() {
  let tab = gBrowser.selectedTab;
  let tab2 = await addTab();

  let menuItemMoveStartTab = document.getElementById("context_moveToStart");

  gBrowser.selectTabAtIndex(1);
  gBrowser.hideTab(tab);

  updateTabContextMenu(tab2);
  is(menuItemMoveStartTab.disabled, true, "Move Tab to Start is disabled");

  gBrowser.showTab(tab);
  BrowserTestUtils.removeTab(tab2);
});

add_task(async function testMoveEndEnabledClickedFromNonSelectedTab() {
  let tab = gBrowser.selectedTab;
  let tab2 = await addTab();
  let tab3 = await addTab();

  let tabs = [tab2, tab3];

  let menuItemMoveEndTab = document.getElementById("context_moveToEnd");

  await triggerClickOn(tab2, {});
  await triggerClickOn(tab3, { ctrlKey: true });

  ok(tab2.multiselected, "Tab2 is multiselected");
  ok(tab3.multiselected, "Tab3 is multiselected");

  updateTabContextMenu(tab);
  is(menuItemMoveEndTab.disabled, false, "Move Tab to End is enabled");

  for (let tabToRemove of tabs) {
    BrowserTestUtils.removeTab(tabToRemove);
  }
});

add_task(async function testMoveEndDisabledFromLastPinnedTab() {
  let tab = gBrowser.selectedTab;
  let tab2 = await addTab();
  let tab3 = await addTab();

  let tabs = [tab2, tab3];

  let menuItemMoveEndTab = document.getElementById("context_moveToEnd");

  gBrowser.pinTab(tab);
  gBrowser.pinTab(tab2);

  updateTabContextMenu(tab2);
  is(menuItemMoveEndTab.disabled, true, "Move Tab to End is disabled");

  for (let tabToRemove of tabs) {
    BrowserTestUtils.removeTab(tabToRemove);
  }
});

add_task(async function testMoveEndDisabledFromLastVisibleTab() {
  let tab = gBrowser.selectedTab;
  let tab2 = await addTab();

  let menuItemMoveEndTab = document.getElementById("context_moveToEnd");

  gBrowser.hideTab(tab2);

  updateTabContextMenu(tab);
  is(menuItemMoveEndTab.disabled, true, "Move Tab to End is disabled");

  gBrowser.showTab(tab);
  BrowserTestUtils.removeTab(tab2);
});

add_task(async function testMoveEndDisabledFromOnlyTab() {
  let tab = gBrowser.selectedTab;

  let menuItemMoveEndTab = document.getElementById("context_moveToEnd");

  updateTabContextMenu(tab);
  is(menuItemMoveEndTab.disabled, true, "Move Tab to End is disabled");
});

add_task(async function testMoveEndDisabledFromOnlyPinnedTab() {
  let tab = gBrowser.selectedTab;

  let menuItemMoveEndTab = document.getElementById("context_moveToEnd");

  gBrowser.pinTab(tab);

  updateTabContextMenu(tab);
  is(menuItemMoveEndTab.disabled, true, "Move Tab to End is disabled");

  gBrowser.unpinTab(tab);
});

add_task(async function testMoveStartEnabledWithCollapsedGroupAtStart() {
  let tab = gBrowser.selectedTab;

  let menuItemMoveStartTab = document.getElementById("context_moveToStart");

  let tab2 = await addTab();
  let group = gBrowser.addTabGroup([tab2]);
  group.collapsed = true;

  gBrowser.moveTabToEnd(tab);
  is(group.tabs[0].index, 0, "Collapsed group is first element on strip");
  is(tab.index, 1, "Selected tab is second element on strip");

  updateTabContextMenu(tab);
  is(menuItemMoveStartTab.disabled, false, "Move Tab to Start is enabled");

  BrowserTestUtils.removeTab(tab2);
});

add_task(async function testMoveStartEnabledWithCollapsedGroupAtEnd() {
  let tab = gBrowser.selectedTab;

  let menuItemMoveEndTab = document.getElementById("context_moveToEnd");

  let tab2 = await addTab();
  let group = gBrowser.addTabGroup([tab2]);
  group.collapsed = true;

  is(tab.index, 0, "Selected tab is first element on strip");
  is(group.tabs[0].index, 1, "Collapsed group is second element on strip");

  updateTabContextMenu(tab);
  is(menuItemMoveEndTab.disabled, false, "Move Tab to End is enabled");

  BrowserTestUtils.removeTab(tab2);
});

add_task(async function testMoveTabOptionsDisabledFromOnlyTab() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.mini-window.enabled", false]],
  });
  let tab = gBrowser.selectedTab;
  let menuItemMoveTabOptions = document.getElementById(
    "context_moveTabOptions"
  );
  updateTabContextMenu(tab);
  is(
    menuItemMoveTabOptions.disabled,
    true,
    "Move Tab Options Submenu is disabled"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function testMoveTabOptionsEnabledFromOnlyTabWithMiniWindow() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.mini-window.enabled", true]],
  });
  let tab = gBrowser.selectedTab;
  updateTabContextMenu(tab);
  is(
    document.getElementById("context_moveTabOptions").disabled,
    false,
    "Move Tab Options Submenu is enabled"
  );
  let miniWindowItem = document.getElementById("context_openTabInMiniWindow");
  ok(!miniWindowItem.hidden, "Move to Mini Window is shown");
  ok(!miniWindowItem.disabled, "Move to Mini Window is enabled");
  for (let id of [
    "context_moveToStart",
    "context_moveToEnd",
    "context_openTabInWindow",
  ]) {
    ok(document.getElementById(id).disabled, `${id} is disabled`);
  }
  await SpecialPowers.popPrefEnv();
});
