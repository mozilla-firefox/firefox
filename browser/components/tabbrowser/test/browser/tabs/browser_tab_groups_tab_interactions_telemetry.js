/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const {
  openFirefoxViewTab,
  closeFirefoxViewTab,
  init: FirefoxViewTestUtilsInit,
} = ChromeUtils.importESModule(
  "resource://testing-common/FirefoxViewTestUtils.sys.mjs"
);
FirefoxViewTestUtilsInit(this);

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

const { UrlbarTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlbarTestUtils.sys.mjs"
);

let resetTelemetry = async () => {
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
};

let assertMetricEmpty = async metricName => {
  Assert.equal(
    Glean.tabgroup.tabInteractions[metricName].testGetValue(),
    null,
    `tab_interactions.${metricName} starts empty`
  );
};

let assertMetricFoundFor = async (metricName, count = 1) => {
  await TestUtils.waitForCondition(() => {
    return Glean.tabgroup.tabInteractions[metricName].testGetValue() == count;
  }, `Wait for tab_interactions.${metricName} to be recorded`);
  Assert.equal(
    Glean.tabgroup.tabInteractions[metricName].testGetValue(),
    count,
    `tab_interactions.${metricName} was recorded`
  );
};

let activateTabContextMenuItem = async (
  selectedTab,
  menuItemSelector,
  submenuItemSelector
) => {
  let submenuItem;
  let submenuItemHiddenPromise;

  const tabContextMenu = window.document.getElementById("tabContextMenu");
  Assert.equal(
    tabContextMenu.state,
    "closed",
    "context menu is initially closed"
  );
  const contextMenuShown = BrowserTestUtils.waitForEvent(
    tabContextMenu,
    "popupshown",
    false,
    ev => ev.target == tabContextMenu
  );
  EventUtils.synthesizeMouseAtCenter(
    selectedTab,
    { type: "contextmenu", button: 2 },
    window
  );
  await contextMenuShown;

  if (submenuItemSelector) {
    submenuItem = tabContextMenu.querySelector(submenuItemSelector);

    const submenuPopupPromise = BrowserTestUtils.waitForEvent(
      submenuItem.menupopup,
      "popupshown"
    );
    submenuItem.openMenu(true);
    await submenuPopupPromise;

    submenuItemHiddenPromise = BrowserTestUtils.waitForEvent(
      submenuItem.menupopup,
      "popuphidden"
    );
  }

  const contextMenuHidden = BrowserTestUtils.waitForEvent(
    tabContextMenu,
    "popuphidden",
    false,
    ev => ev.target == tabContextMenu
  );
  tabContextMenu.activateItem(tabContextMenu.querySelector(menuItemSelector));
  await contextMenuHidden;
  if (submenuItemSelector) {
    await submenuItemHiddenPromise;
  }

  Assert.equal(tabContextMenu.state, "closed", "context menu is closed");
};

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.groups.enabled", true]],
  });
  window.gTabsPanel.init();
  registerCleanupFunction(async () => {
    await SpecialPowers.popPrefEnv();
  });
});

add_task(async function test_tabInteractionsBasic() {
  let initialTab = window.gBrowser.tabs[0];
  await resetTelemetry();

  let groupedTab = await addTab();
  let group = window.gBrowser.addTabGroup([groupedTab]);
  group.collapsed = true;

  info(
    "Test that selecting a tab in a collapsed group records tab_interactions.activate_collapsed"
  );
  await assertMetricEmpty("activate_collapsed");
  let tabSelectEvent = BrowserTestUtils.waitForEvent(window, "TabSelect");
  gBrowser.selectedTab = groupedTab;
  await tabSelectEvent;
  await assertMetricFoundFor("activate_collapsed");

  info(
    "Test that selecting a tab in an expanded group records tab_interactions.activate_expanded"
  );
  tabSelectEvent = BrowserTestUtils.waitForEvent(window, "TabSelect");
  gBrowser.selectedTab = initialTab;
  await tabSelectEvent;
  group.collapsed = false;
  await assertMetricEmpty("activate_expanded");
  tabSelectEvent = BrowserTestUtils.waitForEvent(window, "TabSelect");
  gBrowser.selectedTab = groupedTab;
  await tabSelectEvent;
  await assertMetricFoundFor("activate_expanded");

  info(
    "Test that moving an existing tab into a tab group records tab_interactions.add"
  );
  let tab1 = await addTab();
  await assertMetricEmpty("add");
  window.gBrowser.moveTabToExistingGroup(tab1, group, {
    metricsContext: gBrowser.TabMetrics.userTriggeredContext(),
  });
  await assertMetricFoundFor("add");

  info(
    "Test that adding a new tab to a tab group records tab_interactions.new"
  );
  await assertMetricEmpty("new");
  await addTab(undefined, {
    tabGroup: group,
  });
  await assertMetricFoundFor("new");

  info("Test that moving a tab within a group calls tab_interactions.reorder");
  await assertMetricEmpty("reorder");
  window.gBrowser.moveTabTo(group.tabs[0], {
    tabIndex: 3,
    metricsContext: gBrowser.TabMetrics.userTriggeredContext(),
  });
  await assertMetricFoundFor("reorder");

  info(
    "Test that duplicating a tab within a group calls tab_interactions.duplicate"
  );
  await assertMetricEmpty("duplicate");
  let ssTabRestored = BrowserTestUtils.waitForEvent(window, "SSTabRestored");
  window.gBrowser.duplicateTab(group.tabs[0], true, { tabIndex: 2 });
  await ssTabRestored;
  await assertMetricFoundFor("duplicate");

  window.gBrowser.removeAllTabsBut(initialTab);
  await resetTelemetry();
});

add_task(async function test_tabInteractionsClose() {
  let initialTab = window.gBrowser.tabs[0];
  await resetTelemetry();
  FirefoxViewTestUtilsInit(this, window);

  let tabs = await Promise.all(Array.from({ length: 5 }, () => addTab()));
  let group = window.gBrowser.addTabGroup(tabs);

  info(
    "Test that closing a tab using the tab's close button calls tab_interactions.close_tabstrip"
  );
  await assertMetricEmpty("close_tabstrip");
  group.tabs.at(-1).querySelector(".tab-close-button").click();
  await assertMetricFoundFor("close_tabstrip");

  info(
    "Test that closing a tab via the tab context menu calls tab_interactions.close_tabmenu"
  );
  await activateTabContextMenuItem(group.tabs[0], "#context_closeTab");
  await assertMetricFoundFor("close_tabmenu", 1);

  info(
    "Test that closing a tab via the tab close keyboard shortcut calls tab_interactions.close_tab_other"
  );
  window.gBrowser.selectedTab = group.tabs.at(-1);
  await assertMetricEmpty("close_tab_other");
  EventUtils.synthesizeKey("w", { accelKey: true }, window);
  await assertMetricFoundFor("close_tab_other");

  info(
    "Test that closing a tab via top menu calls tab_interactions.close_tab_other"
  );
  window.document.getElementById("cmd_close").doCommand();
  await assertMetricFoundFor("close_tab_other", 2);

  info(
    "Test that closing a tab via firefox view calls tab_interactions.close_tab_other"
  );
  await openFirefoxViewTab(window).then(async viewTab => {
    const openTabsCard = viewTab.linkedBrowser.contentDocument
      .querySelector("named-deck > view-recentbrowsing view-opentabs")
      .shadowRoot.querySelector("view-opentabs-card");
    let tabElement;
    await TestUtils.waitForCondition(() => {
      tabElement = Array.from(openTabsCard.tabList.rowEls).find(
        t => t.__tabElement?.group
      );
      return !!tabElement;
    }, "Wait for grouped tab to appear in Firefox View open tabs");
    tabElement.shadowRoot.querySelector("moz-button.dismiss-button").click();
    await assertMetricFoundFor("close_tab_other", 3);
  });
  await closeFirefoxViewTab(window);

  window.gBrowser.removeAllTabsBut(initialTab);
  await resetTelemetry();
});

add_task(async function test_tabInteractionsCloseViaAnotherTabContext() {
  let initialTab = window.gBrowser.tabs[0];
  await resetTelemetry();

  window.gBrowser.addTabGroup([await addTab()]);

  await assertMetricEmpty("close_tabmenu");

  info(
    "Test that closing a tab via the tab context menu 'close other tabs' command calls tab_interactions.close_tabmenu"
  );
  await activateTabContextMenuItem(
    initialTab,
    "#context_closeOtherTabs",
    "#context_closeTabOptions"
  );
  await assertMetricFoundFor("close_tabmenu");

  info(
    "Test that closing a tab via the tab context menu 'close tabs to left' command calls tab_interactions.close_tabmenu"
  );
  window.gBrowser.addTabGroup([await addTab()]);
  window.gBrowser.moveTabToEnd(initialTab);
  await activateTabContextMenuItem(
    initialTab,
    "#context_closeTabsToTheStart",
    "#context_closeTabOptions"
  );
  await assertMetricFoundFor("close_tabmenu", 2);

  info(
    "Test that closing a tab via the tab context menu 'close tabs to right' command calls tab_interactions.close_tabmenu"
  );
  window.gBrowser.addTabGroup([await addTab()]);
  await activateTabContextMenuItem(
    initialTab,
    "#context_closeTabsToTheEnd",
    "#context_closeTabOptions"
  );
  await assertMetricFoundFor("close_tabmenu", 3);

  info(
    "Test that closing a tab via the tab context menu 'close duplicate tabs' command calls tab_interactions.close_tabmenu"
  );
  let duplicateTabs = await Promise.all([addTab(), addTab()]);
  window.gBrowser.addTabGroup([duplicateTabs[1]]);

  await activateTabContextMenuItem(
    duplicateTabs[0],
    "#context_closeDuplicateTabs",
    "#context_closeTabOptions"
  );
  await assertMetricFoundFor("close_tabmenu", 4);

  // Dismiss the confirmation panel that appears after closing duplicate tabs.
  EventUtils.synthesizeMouseAtCenter(document.documentElement, {});
  await TestUtils.waitForCondition(
    () => !window.ConfirmationHint._panel.getAttribute("animating"),
    "Ensure the confirmation panel is closed"
  );

  window.gBrowser.removeAllTabsBut(initialTab);
  await resetTelemetry();
});

add_task(async function test_tabInteractionsCloseTabOverflowMenu() {
  let initialTab = window.gBrowser.tabs[0];
  await resetTelemetry();

  window.gBrowser.addTabGroup([await addTab()]);

  info(
    "Test that closing a tab from the tab overflow menu calls tab_interactions.close_tabmenu"
  );
  let viewShown = BrowserTestUtils.waitForEvent(
    window.document.getElementById("allTabsMenu-allTabsView"),
    "ViewShown"
  );
  window.document.getElementById("alltabs-button").click();
  await viewShown;

  await assertMetricEmpty("close_tabmenu");
  window.document
    .getElementById("allTabsMenu-allTabsView-tabs")
    .querySelector(":scope .all-tabs-item.grouped .all-tabs-close-button")
    .click();
  await assertMetricFoundFor("close_tabmenu");

  let panel = window.document
    .getElementById("allTabsMenu-allTabsView")
    .closest("panel");
  if (!panel) {
    return;
  }
  let hidden = BrowserTestUtils.waitForPopupEvent(panel, "hidden");
  panel.hidePopup();
  await hidden;

  window.gBrowser.removeAllTabsBut(initialTab);
  await resetTelemetry();
});

add_task(async function test_tabInteractionsRemoveFromGroup() {
  let initialTab = window.gBrowser.tabs[0];
  await resetTelemetry();

  let tabs = await Promise.all(Array.from({ length: 3 }, () => addTab()));
  let group = window.gBrowser.addTabGroup(tabs);

  info(
    "Test that moving a tab out of a tab group calls tab_interactions.remove_same_window"
  );
  await assertMetricEmpty("remove_same_window");
  window.gBrowser.moveTabTo(group.tabs[0], {
    tabIndex: 0,
    metricsContext: gBrowser.TabMetrics.userTriggeredContext(),
  });
  await assertMetricFoundFor("remove_same_window");

  info(
    "Test that moving a tab out of a tab group and into a different (existing) window calls tab_interactions.remove_other_window"
  );
  await assertMetricEmpty("remove_other_window");
  let newWin = await BrowserTestUtils.openNewBrowserWindow();
  newWin.gBrowser.adoptTab(group.tabs[0]);
  await assertMetricFoundFor("remove_other_window");
  await BrowserTestUtils.closeWindow(newWin);

  info(
    "Test that moving a tab out of a tab group and into a different (new) window calls tab_interactions.remove_new_window"
  );
  await assertMetricEmpty("remove_new_window");
  let newWindowPromise = BrowserTestUtils.waitForNewWindow();
  await EventUtils.synthesizePlainDragAndDrop({
    srcElement: group.tabs[0],
    srcWindow: window,
    destElement: null,
    // don't move horizontally because that could cause a tab move
    // animation, and there's code to prevent a tab detaching if
    // the dragged tab is released while the animation is running.
    stepX: 0,
    stepY: 100,
  });
  newWin = await newWindowPromise;
  await assertMetricFoundFor("remove_new_window");
  await BrowserTestUtils.closeWindow(newWin);

  window.gBrowser.removeAllTabsBut(initialTab);
  await resetTelemetry();
});
