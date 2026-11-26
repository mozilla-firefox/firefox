/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { TabMetrics } = ChromeUtils.importESModule(
  "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs"
);

const { CustomizableUITestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/CustomizableUITestUtils.sys.mjs"
);

let resetTelemetry = async () => {
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
};

let getInteractionEvents = () => Glean.tab.interaction.testGetValue() ?? [];

let getLastInteractionEvent = () => {
  let events = getInteractionEvents();
  Assert.greater(
    events.length,
    0,
    "At least one tab.interaction event was recorded"
  );
  return events.at(-1);
};

/**
 * Asserts that a tab.interaction event has the expected extra keys.
 * Glean serializes boolean and quantity extras as strings.
 */
let assertInteractionEvent = (event, expected) => {
  Assert.equal(
    event.extra.action,
    expected.action,
    `action is "${expected.action}"`
  );
  Assert.equal(
    event.extra.source,
    expected.source,
    `source is "${expected.source}"`
  );
  Assert.equal(
    event.extra.layout,
    expected.layout ?? "horizontal",
    `layout is "${expected.layout ?? "horizontal"}"`
  );
  Assert.equal(
    event.extra.tab_count,
    String(expected.tab_count ?? 1),
    `tab_count is ${expected.tab_count ?? 1}`
  );
  Assert.equal(
    Glean.tab.actions[expected.action].testGetValue(),
    1,
    `tab.actions["${expected.action}"] is 1`
  );
  Assert.equal(
    Glean.tab.tabCount[expected.action].testGetValue(),
    expected.tab_count ?? 1,
    `tab.tabCount["${expected.action}"] is ${expected.tab_count ?? 1}`
  );
};

let activateTabContextMenuItem = async (tab, menuItemSelector) => {
  const tabContextMenu = document.getElementById("tabContextMenu");
  const contextMenuShown = BrowserTestUtils.waitForEvent(
    tabContextMenu,
    "popupshown",
    false,
    ev => ev.target == tabContextMenu
  );
  EventUtils.synthesizeMouseAtCenter(
    tab,
    { type: "contextmenu", button: 2 },
    window
  );
  await contextMenuShown;
  const contextMenuHidden = BrowserTestUtils.waitForEvent(
    tabContextMenu,
    "popuphidden",
    false,
    ev => ev.target == tabContextMenu
  );
  tabContextMenu.activateItem(tabContextMenu.querySelector(menuItemSelector));
  await contextMenuHidden;
};

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.tabs.groups.enabled", true],
      // Suppress the "close other tabs?" warning dialog so it doesn't block tests.
      ["browser.tabs.warnOnCloseOtherTabs", false],
    ],
  });
  registerCleanupFunction(() => SpecialPowers.popPrefEnv());
});

add_task(async function test_activate_tabstrip_click() {
  let initialTab = gBrowser.tabs[0];
  await resetTelemetry();

  let tab = await addTab("about:blank");
  gBrowser.selectedTab = initialTab;
  Assert.notEqual(
    gBrowser.selectedTab,
    tab,
    "Tab is not selected before click"
  );
  Assert.equal(getInteractionEvents().length, 0, "No events before click");

  // synthesizeMouseAtCenter dispatches events synchronously; the tab switch
  // and Glean recording both happen before it returns.
  EventUtils.synthesizeMouseAtCenter(tab, {});

  Assert.equal(gBrowser.selectedTab, tab, "Tab is now selected");
  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.ACTIVATE,
    source: TabMetrics.METRIC_SOURCE.TAB_STRIP,
    tab_count: 1,
  });

  gBrowser.removeTab(tab);
  await resetTelemetry();
});

add_task(async function test_activate_tabstrip_click_in_group() {
  let initialTab = gBrowser.tabs[0];
  await resetTelemetry();

  let tab = await addTab("about:blank");
  gBrowser.addTabGroup([tab]);
  gBrowser.selectedTab = initialTab;

  EventUtils.synthesizeMouseAtCenter(tab, {});

  Assert.equal(gBrowser.selectedTab, tab, "Grouped tab is now selected");
  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.ACTIVATE,
    source: TabMetrics.METRIC_SOURCE.TAB_STRIP,
    tab_count: 1,
  });

  gBrowser.removeAllTabsBut(initialTab);
  await resetTelemetry();
});

add_task(async function test_activate_keyboard_ctrl_pagedown() {
  let initialTab = gBrowser.tabs[0];
  await resetTelemetry();

  let tab = await addTab("about:blank");
  gBrowser.selectedTab = initialTab;
  Assert.equal(getInteractionEvents().length, 0, "No events before keypress");

  let tabSelectEvent = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "TabSelect"
  );
  EventUtils.synthesizeKey("KEY_PageDown", { ctrlKey: true }, window);
  await tabSelectEvent;

  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.ACTIVATE,
    source: TabMetrics.METRIC_SOURCE.KEYBOARD,
    tab_count: 1,
  });

  gBrowser.removeTab(tab);
  await resetTelemetry();
});

add_task(async function test_activate_keyboard_index_ctrl_1() {
  let initialTab = gBrowser.tabs[0];
  await resetTelemetry();

  let tab = await addTab("about:blank");
  await BrowserTestUtils.switchTab(gBrowser, tab);
  Assert.equal(getInteractionEvents().length, 0, "No events before keypress");

  // On GTK (Linux/GNOME) the shortcut is Alt+1; on other platforms it is Ctrl/Cmd+1.
  let keyModifiers =
    AppConstants.MOZ_WIDGET_TOOLKIT == "gtk"
      ? { altKey: true }
      : { accelKey: true };

  let tabSelectEvent = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "TabSelect"
  );
  EventUtils.synthesizeKey("1", keyModifiers, window);
  await tabSelectEvent;

  Assert.equal(gBrowser.selectedTab, initialTab, "Switched to first tab");
  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.ACTIVATE,
    source: TabMetrics.METRIC_SOURCE.KEYBOARD,
    tab_count: 1,
  });

  gBrowser.removeTab(tab);
  await resetTelemetry();
});

add_task(async function test_activate_ctrl_tab_panel() {
  let initialTab = gBrowser.tabs[0];
  await resetTelemetry();

  await SpecialPowers.pushPrefEnv({
    set: [["browser.ctrlTab.sortByRecentlyUsed", true]],
  });

  // The ctrlTab panel only opens when there are >2 visible tabs.
  let tab1 = await addTab("about:blank");
  let tab2 = await addTab("about:blank");
  gBrowser.selectedTab = initialTab;

  let panelShown = BrowserTestUtils.waitForEvent(ctrlTab.panel, "popupshown");
  EventUtils.synthesizeKey("VK_TAB", { ctrlKey: true }, window);
  await panelShown;

  let panelHidden = BrowserTestUtils.waitForEvent(ctrlTab.panel, "popuphidden");
  EventUtils.synthesizeKey("VK_CONTROL", { type: "keyup" }, window);
  await panelHidden;

  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.ACTIVATE,
    source: TabMetrics.METRIC_SOURCE.CTRL_TAB,
    tab_count: 1,
  });

  await SpecialPowers.popPrefEnv();
  gBrowser.removeTab(tab1);
  gBrowser.removeTab(tab2);
  await resetTelemetry();
});

add_task(async function test_close_tabstrip_close_button() {
  await resetTelemetry();

  let tab = await addTab("about:blank");
  Assert.equal(getInteractionEvents().length, 0, "No events before close");

  let tabClosing = BrowserTestUtils.waitForTabClosing(tab);
  EventUtils.synthesizeMouseAtCenter(
    tab.querySelector(".tab-close-button"),
    {}
  );
  await tabClosing;

  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.CLOSE,
    source: TabMetrics.METRIC_SOURCE.TAB_STRIP,
    tab_count: 1,
  });

  await resetTelemetry();
});

add_task(async function test_close_tabstrip_close_button_in_group() {
  await resetTelemetry();

  let tab = await addTab("about:blank");
  gBrowser.addTabGroup([tab]);

  let tabClosing = BrowserTestUtils.waitForTabClosing(tab);
  EventUtils.synthesizeMouseAtCenter(
    tab.querySelector(".tab-close-button"),
    {}
  );
  await tabClosing;

  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.CLOSE,
    source: TabMetrics.METRIC_SOURCE.TAB_STRIP,
    tab_count: 1,
  });

  await resetTelemetry();
});

add_task(async function test_close_middle_click() {
  let initialTab = gBrowser.tabs[0];
  await resetTelemetry();

  let tab = await addTab("about:blank");
  gBrowser.selectedTab = initialTab;

  let tabClosing = BrowserTestUtils.waitForTabClosing(tab);
  EventUtils.synthesizeMouseAtCenter(tab, { button: 1 }, window);
  await tabClosing;

  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.CLOSE,
    source: TabMetrics.METRIC_SOURCE.MIDDLE_CLICK,
    tab_count: 1,
  });

  await resetTelemetry();
});

add_task(async function test_close_keyboard_ctrl_w() {
  await resetTelemetry();

  let tab = await addTab("about:blank");
  await BrowserTestUtils.switchTab(gBrowser, tab);

  let tabClosing = BrowserTestUtils.waitForTabClosing(tab);
  EventUtils.synthesizeKey("w", { accelKey: true }, window);
  await tabClosing;

  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.CLOSE,
    source: TabMetrics.METRIC_SOURCE.KEYBOARD,
    tab_count: 1,
  });

  await resetTelemetry();
});

add_task(async function test_close_context_menu() {
  await resetTelemetry();

  let tab = await addTab("about:blank");

  let tabClosing = BrowserTestUtils.waitForTabClosing(tab);
  await activateTabContextMenuItem(tab, "#context_closeTab");
  await tabClosing;

  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.CLOSE,
    source: TabMetrics.METRIC_SOURCE.TAB_MENU,
    tab_count: 1,
  });

  await resetTelemetry();
});

add_task(async function test_close_multi_tab_count() {
  await resetTelemetry();

  let tab1 = await addTab("about:blank");
  let tab2 = await addTab("about:blank");
  let tab3 = await addTab("about:blank");

  // Switch to one of the tabs so it is the selectedTab; otherwise
  // selectedTabs would also include the background initialTab.
  gBrowser.selectedTab = tab1;
  gBrowser.addToMultiSelectedTabs(tab1);
  gBrowser.addToMultiSelectedTabs(tab2);
  gBrowser.addToMultiSelectedTabs(tab3);
  Assert.equal(gBrowser.multiSelectedTabsCount, 3, "3 tabs multi-selected");

  // Clicking the close button on one multi-selected tab closes all of them.
  // Wait for full DOM removal (not just TabClose) so the next test sees clean state.
  let closePromises = [tab1, tab2, tab3].map(t =>
    TestUtils.waitForCondition(() => !t.isConnected, "Tab removed")
  );
  EventUtils.synthesizeMouseAtCenter(
    tab1.querySelector(".tab-close-button"),
    {}
  );
  await Promise.all(closePromises);

  let events = getInteractionEvents();
  Assert.equal(events.length, 1, "One event for all closed tabs");
  assertInteractionEvent(events[0], {
    action: TabMetrics.METRIC_ACTION.CLOSE,
    source: TabMetrics.METRIC_SOURCE.TAB_STRIP,
    tab_count: 3,
  });

  await resetTelemetry();
});

add_task(async function test_move_keyboard_ctrl_shift_pagedown() {
  await resetTelemetry();

  let tab1 = await addTab("about:blank");
  let tab2 = await addTab("about:blank");
  gBrowser.selectedTab = tab1;
  let startPos = tab1.index;
  Assert.equal(getInteractionEvents().length, 0, "No events before keypress");

  let tabMovePromise = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "TabMove"
  );
  EventUtils.synthesizeKey(
    "KEY_PageDown",
    { ctrlKey: true, shiftKey: true },
    window
  );
  await tabMovePromise;

  Assert.greater(tab1.index, startPos, "Tab moved forward");
  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.MOVE,
    source: TabMetrics.METRIC_SOURCE.KEYBOARD,
    tab_count: 1,
  });

  gBrowser.removeTab(tab1);
  gBrowser.removeTab(tab2);
  await resetTelemetry();
});

add_task(async function test_move_context_menu_to_start() {
  await resetTelemetry();

  // Add a spacer tab so the target tab actually has somewhere to move.
  let spacer = await addTab("about:blank");
  let tab = await addTab("about:blank");
  Assert.greater(tab.index, 0, "Tab starts after index 0");

  // context_moveToStart is inside the moveTabOptions submenu; navigating
  // nested submenus in headless tests is not reliable, so we call the same
  // API that the menu item's command handler invokes.
  let tabMovePromise = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "TabMove"
  );
  gBrowser.moveTabsToStart(tab, {
    metricsContext: gBrowser.TabMetrics.userTriggeredContext(
      gBrowser.TabMetrics.METRIC_SOURCE.TAB_MENU
    ),
  });
  await tabMovePromise;

  Assert.equal(tab.index, 0, "Tab moved to start");
  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.MOVE,
    source: TabMetrics.METRIC_SOURCE.TAB_MENU,
    tab_count: 1,
  });

  gBrowser.removeTab(spacer);
  gBrowser.removeTab(tab);
  await resetTelemetry();
});

add_task(async function test_move_multi_tab_count() {
  await resetTelemetry();

  // initialTab at 0 (not multiselected), spacer at 1, then 3 tabs to move.
  let spacer = await addTab("about:blank");
  let tab1 = await addTab("about:blank");
  let tab2 = await addTab("about:blank");
  let tab3 = await addTab("about:blank");

  gBrowser.selectedTab = tab1;
  gBrowser.addToMultiSelectedTabs(tab1);
  gBrowser.addToMultiSelectedTabs(tab2);
  gBrowser.addToMultiSelectedTabs(tab3);
  Assert.equal(gBrowser.multiSelectedTabsCount, 3, "3 tabs multi-selected");

  let tabMoveEvents = [tab1, tab2, tab3].map(tab =>
    BrowserTestUtils.waitForEvent(tab, "TabMove")
  );

  // context_moveToStart is inside the moveTabOptions submenu; navigating
  // nested submenus in headless tests is not reliable, so we call the same
  // API that the menu item's command handler invokes.
  gBrowser.moveTabsToStart(tab1, {
    metricsContext: gBrowser.TabMetrics.userTriggeredContext(
      gBrowser.TabMetrics.METRIC_SOURCE.TAB_MENU
    ),
  });
  await Promise.all(tabMoveEvents);

  let events = getInteractionEvents();
  Assert.equal(events.length, 1, "One move event for all tabs");
  assertInteractionEvent(events[0], {
    action: TabMetrics.METRIC_ACTION.MOVE,
    source: TabMetrics.METRIC_SOURCE.TAB_MENU,
    tab_count: 3,
  });

  gBrowser.removeTab(spacer);
  gBrowser.removeTab(tab1);
  gBrowser.removeTab(tab2);
  gBrowser.removeTab(tab3);
});

// sourceForEvent must not throw on null/undefined — previously crashed with
// TypeError when advanceSelectedTab was called without an event argument.
add_task(function test_source_for_event_null_no_crash() {
  Assert.equal(
    TabMetrics.sourceForEvent(null),
    TabMetrics.METRIC_SOURCE.UNKNOWN,
    "sourceForEvent(null) returns UNKNOWN without crashing"
  );
  Assert.equal(
    TabMetrics.sourceForEvent(undefined),
    TabMetrics.METRIC_SOURCE.UNKNOWN,
    "sourceForEvent(undefined) returns UNKNOWN without crashing"
  );
  // Sanity-check the non-null path still works.
  Assert.equal(
    TabMetrics.sourceForEvent(new KeyboardEvent("keydown")),
    TabMetrics.METRIC_SOURCE.KEYBOARD,
    "sourceForEvent with a KeyboardEvent returns KEYBOARD"
  );
});

// TabsList._selectTab used METRIC_SOURCE.TAB_OVERFLOW (undefined); the correct
// key is TAB_OVERFLOW_MENU.  Verify activating a tab with that source records
// "tab_overflow" rather than "unknown".
// The overflow menu only appears when the tab strip overflows, which is not
// reliably triggerable in headless tests, so we exercise the code path directly.
add_task(async function test_activate_tab_overflow_menu_source() {
  let initialTab = gBrowser.tabs[0];
  await resetTelemetry();

  let tab = await addTab("about:blank");
  gBrowser.selectedTab = initialTab;

  gBrowser.setSelectedTab(
    tab,
    gBrowser.TabMetrics.userTriggeredContext(
      gBrowser.TabMetrics.METRIC_SOURCE.TAB_OVERFLOW_MENU
    )
  );

  Assert.equal(gBrowser.selectedTab, tab, "Tab is now selected");
  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.ACTIVATE,
    source: TabMetrics.METRIC_SOURCE.TAB_OVERFLOW_MENU,
  });

  gBrowser.removeTab(tab);
  await resetTelemetry();
});

// Closing a multi-selection that contains a complete tab group previously fired
// two CLOSE events (one from the outer removeTabs and one from the nested
// removeTabs inside removeTabGroup).  After the fix only one event fires.
add_task(async function test_close_group_in_multiselect_no_double_count() {
  await resetTelemetry();

  let tab1 = await addTab("about:blank");
  let tab2 = await addTab("about:blank");
  gBrowser.addTabGroup([tab1, tab2]);

  gBrowser.selectedTab = tab1;
  gBrowser.addToMultiSelectedTabs(tab1);
  gBrowser.addToMultiSelectedTabs(tab2);
  Assert.equal(gBrowser.multiSelectedTabsCount, 2, "2 tabs multi-selected");

  let closePromises = [tab1, tab2].map(t =>
    TestUtils.waitForCondition(() => !t.isConnected, "Tab removed")
  );
  await activateTabContextMenuItem(tab1, "#context_closeTab");
  await Promise.all(closePromises);
  await Services.fog.testFlushAllChildren();

  let events = getInteractionEvents();
  Assert.equal(
    events.length,
    1,
    "Exactly one CLOSE event (no double-count for group tabs)"
  );
  assertInteractionEvent(events[0], {
    action: TabMetrics.METRIC_ACTION.CLOSE,
    source: TabMetrics.METRIC_SOURCE.TAB_MENU,
    tab_count: 2,
  });

  await resetTelemetry();
});

// When the selected tab (lastToClose) has its beforeunload cancelled, the
// tab_count in the CLOSE metric was previously off-by-one (too high).
//
// Note: removeMultiSelectedTabs → removeTabs uses a synchronous event-loop
// spin (spinEventLoopUntilOrQuit) while waiting for browser.permitUnload().
// PromptTestUtils.handleNextPrompt is async and its Promise chain cannot
// advance during that spin, so we instead use a synchronous observer that
// clicks "Stay on Page" directly inside the common-dialog-loaded callback.
add_task(async function test_close_lastToClose_cancel_decrements_tab_count() {
  await SpecialPowers.pushPrefEnv({
    set: [["dom.require_user_interaction_for_beforeunload", false]],
  });
  await resetTelemetry();

  // tabA = selected tab (becomes lastToClose); has a beforeunload handler
  // that will be cancelled.  tabB = sibling that closes without a prompt.
  let tabA = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    // Inline the handler so browser.hasBeforeUnload is set on page load.
    "data:text/html,<script>window.onbeforeunload=()=>'';</script>"
  );
  let tabB = await addTab("about:blank");

  gBrowser.addToMultiSelectedTabs(tabA);
  gBrowser.addToMultiSelectedTabs(tabB);
  Assert.equal(gBrowser.multiSelectedTabsCount, 2, "2 tabs multi-selected");

  // Click "Stay on Page" (button1) synchronously inside the observer so the
  // dialog is dismissed while browser.permitUnload() holds the spin lock.
  // PromptTestUtils.handleNextPrompt is async and can't advance during the spin.
  let dialogFired = false;
  let dialogObserver = {
    observe(subject) {
      dialogFired = true;
      Services.obs.removeObserver(dialogObserver, "common-dialog-loaded");
      subject.Dialog.ui.button1.click();
    },
  };
  Services.obs.addObserver(dialogObserver, "common-dialog-loaded");

  // registerCleanupFunction ensures tabA is closed and the observer removed
  // even if an assertion below fails.
  registerCleanupFunction(() => {
    if (!dialogFired) {
      Services.obs.removeObserver(dialogObserver, "common-dialog-loaded");
    }
    if (!tabA.closing && tabA.isConnected) {
      // skipPermitUnload avoids re-triggering the beforeunload dialog.
      BrowserTestUtils.removeTab(tabA, { skipPermitUnload: true });
    }
  });

  await activateTabContextMenuItem(tabA, "#context_closeTab");
  // Wait for tabB to be fully removed from the DOM (not just for TabClose to
  // fire) so that no closing animation is still running when the next test
  // reads tab positions.
  await TestUtils.waitForCondition(
    () => !tabB.isConnected,
    "tabB fully removed from DOM"
  );

  Assert.ok(dialogFired, "The beforeunload dialog appeared and was dismissed");
  Assert.ok(!tabA.closing, "tabA is still open after beforeunload cancel");

  // Only tabB closed — tab_count must be 1, not 2.
  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.CLOSE,
    source: TabMetrics.METRIC_SOURCE.TAB_MENU,
    tab_count: 1,
  });

  await BrowserTestUtils.removeTab(tabA, { skipPermitUnload: true });
  await SpecialPowers.popPrefEnv();
  await resetTelemetry();
});

// Ctrl+Tab with exactly 2 visible tabs and sortByRecentlyUsed=true bypasses
// the panel and calls setSelectedTab directly via ctrlTab.onKeyDown; previously
// it used the bare selectedTab setter and missed telemetry.
add_task(async function test_activate_ctrl_tab_two_tabs() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ctrlTab.sortByRecentlyUsed", true]],
  });
  let initialTab = gBrowser.tabs[0];
  await resetTelemetry();

  // Exactly 2 visible tabs: the initial tab and one new one.
  let tab = await addTab("about:blank");
  gBrowser.selectedTab = initialTab;
  Assert.equal(getInteractionEvents().length, 0, "No events before Ctrl+Tab");

  let tabSelectEvent = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "TabSelect"
  );
  EventUtils.synthesizeKey("VK_TAB", { ctrlKey: true }, window);
  await tabSelectEvent;

  Assert.equal(gBrowser.selectedTab, tab, "Switched to other tab");
  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.ACTIVATE,
    source: TabMetrics.METRIC_SOURCE.CTRL_TAB,
  });

  gBrowser.removeTab(tab);
  await SpecialPowers.popPrefEnv();
  await resetTelemetry();
});

// The tabs.mjs on_keydown handler (Ctrl+Shift+Arrow when the tab strip has focus)
// passes a KEYBOARD metricsContext to moveTabForward/Backward/ToStart/ToEnd.
add_task(async function test_move_keyboard_tabstrip_arrow_moves() {
  await resetTelemetry();

  let tab1 = await addTab("about:blank");
  let tab2 = await addTab("about:blank");
  gBrowser.selectedTab = tab1;
  Assert.less(tab1.index, tab2.index, "tab1 starts before tab2");

  let tabMovePromise = BrowserTestUtils.waitForEvent(tab1, "TabMove");
  Services.focus.setFocus(tab1, Services.focus.FLAG_BYKEY);
  EventUtils.synthesizeKey("KEY_ArrowRight", {
    accelKey: true,
    shiftKey: true,
  });
  await tabMovePromise;

  Assert.greater(tab1.index, tab2.index, "tab1 moved past tab2");
  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.MOVE,
    source: TabMetrics.METRIC_SOURCE.KEYBOARD,
  });

  gBrowser.removeTab(tab1);
  gBrowser.removeTab(tab2);
  await resetTelemetry();
});

// pinMultiSelectedTabs should fire one PIN event with the total tab count,
// not one event per tab.
add_task(async function test_pin_multi_tab_count() {
  await resetTelemetry();

  let tab1 = await addTab("about:blank");
  let tab2 = await addTab("about:blank");
  let tab3 = await addTab("about:blank");

  gBrowser.selectedTab = tab1;
  gBrowser.addToMultiSelectedTabs(tab1);
  gBrowser.addToMultiSelectedTabs(tab2);
  gBrowser.addToMultiSelectedTabs(tab3);
  Assert.equal(gBrowser.multiSelectedTabsCount, 3, "3 tabs multi-selected");

  await activateTabContextMenuItem(tab1, "#context_pinSelectedTabs");

  let events = getInteractionEvents();
  Assert.equal(events.length, 1, "One PIN event for all tabs, not one per tab");
  assertInteractionEvent(events[0], {
    action: TabMetrics.METRIC_ACTION.PIN,
    source: TabMetrics.METRIC_SOURCE.TAB_MENU,
    tab_count: 3,
  });

  gBrowser.removeTab(tab1);
  gBrowser.removeTab(tab2);
  gBrowser.removeTab(tab3);
  await resetTelemetry();
});

// unpinMultiSelectedTabs should fire one UNPIN event with the total tab count.
add_task(async function test_unpin_multi_tab_count() {
  let tab1 = await addTab("about:blank");
  let tab2 = await addTab("about:blank");
  // Pin without telemetry (UNKNOWN_CONTEXT → isUserTriggered=false → no event).
  gBrowser.pinTab(tab1);
  gBrowser.pinTab(tab2);
  await resetTelemetry();

  gBrowser.selectedTab = tab1;
  gBrowser.addToMultiSelectedTabs(tab1);
  gBrowser.addToMultiSelectedTabs(tab2);
  Assert.equal(gBrowser.multiSelectedTabsCount, 2, "2 tabs multi-selected");

  await activateTabContextMenuItem(tab1, "#context_unpinSelectedTabs");

  let events = getInteractionEvents();
  Assert.equal(
    events.length,
    1,
    "One UNPIN event for all tabs, not one per tab"
  );
  assertInteractionEvent(events[0], {
    action: TabMetrics.METRIC_ACTION.UNPIN,
    source: TabMetrics.METRIC_SOURCE.TAB_MENU,
    tab_count: 2,
  });

  gBrowser.removeTab(tab1);
  gBrowser.removeTab(tab2);
  await resetTelemetry();
});

// When a multiselect includes a whole tab group plus standalone tabs and all
// close successfully, one CLOSE event should fire with the total tab count.
add_task(async function test_close_mixed_group_and_standalone_single_event() {
  let initialTab = gBrowser.tabs[0];
  await resetTelemetry();

  let groupTab1 = await addTab("about:blank");
  let groupTab2 = await addTab("about:blank");
  let standaloneTab = await addTab("about:blank");
  gBrowser.addTabGroup([groupTab1, groupTab2]);

  gBrowser.selectedTab = groupTab1;
  gBrowser.addToMultiSelectedTabs(groupTab1);
  gBrowser.addToMultiSelectedTabs(groupTab2);
  gBrowser.addToMultiSelectedTabs(standaloneTab);
  Assert.equal(gBrowser.multiSelectedTabsCount, 3, "3 tabs multi-selected");

  let closePromises = [groupTab1, groupTab2, standaloneTab].map(t =>
    TestUtils.waitForCondition(() => !t.isConnected, "Tab removed")
  );
  await activateTabContextMenuItem(groupTab1, "#context_closeTab");
  await Promise.all(closePromises);
  await Services.fog.testFlushAllChildren();

  let events = getInteractionEvents();
  Assert.equal(events.length, 1, "One CLOSE event for the whole multiselect");
  assertInteractionEvent(events[0], {
    action: TabMetrics.METRIC_ACTION.CLOSE,
    source: TabMetrics.METRIC_SOURCE.TAB_MENU,
    tab_count: 3,
  });

  gBrowser.selectedTab = initialTab;
  await resetTelemetry();
});

// When a whole tab group's beforeunload handler cancels the close during a
// multiselect, only the tabs that actually closed should be counted.
// removeTabs now waits for all removeTabGroup promises in the same spin so it
// can inspect tab.closing on each group's snapshot after the spin.
//
// Note: uses a synchronous observer to cancel the dialog because removeTabs
// holds a spinEventLoopUntilOrQuit spin that prevents async promise chains
// from advancing until the spin exits.
add_task(async function test_close_group_cancel_decrements_tab_count() {
  await SpecialPowers.pushPrefEnv({
    set: [["dom.require_user_interaction_for_beforeunload", false]],
  });
  await resetTelemetry();

  // groupTab has a beforeunload that cancels; standaloneTab does not.
  let groupTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "data:text/html,<script>window.onbeforeunload=()=>'';</script>"
  );
  let standaloneTab = await addTab("about:blank");
  gBrowser.addTabGroup([groupTab]);

  gBrowser.selectedTab = groupTab;
  gBrowser.addToMultiSelectedTabs(groupTab);
  gBrowser.addToMultiSelectedTabs(standaloneTab);
  Assert.equal(gBrowser.multiSelectedTabsCount, 2, "2 tabs multi-selected");

  let dialogFired = false;
  let dialogObserver = {
    observe(subject) {
      dialogFired = true;
      Services.obs.removeObserver(dialogObserver, "common-dialog-loaded");
      subject.Dialog.ui.button1.click(); // "Stay on Page"
    },
  };
  Services.obs.addObserver(dialogObserver, "common-dialog-loaded");

  registerCleanupFunction(() => {
    if (!dialogFired) {
      Services.obs.removeObserver(dialogObserver, "common-dialog-loaded");
    }
    if (!groupTab.closing && groupTab.isConnected) {
      BrowserTestUtils.removeTab(groupTab, { skipPermitUnload: true });
    }
  });

  await activateTabContextMenuItem(groupTab, "#context_closeTab");

  // standaloneTab closes normally; wait for its DOM removal.
  await TestUtils.waitForCondition(
    () => !standaloneTab.isConnected,
    "standaloneTab fully removed from DOM"
  );
  await Services.fog.testFlushAllChildren();

  Assert.ok(dialogFired, "The beforeunload dialog appeared and was dismissed");
  Assert.ok(
    !groupTab.closing,
    "groupTab is still open after beforeunload cancel"
  );

  // Only standaloneTab closed — tab_count must be 1, not 2.
  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.CLOSE,
    source: TabMetrics.METRIC_SOURCE.TAB_MENU,
    tab_count: 1,
  });

  await BrowserTestUtils.removeTab(groupTab, { skipPermitUnload: true });
  await SpecialPowers.popPrefEnv();
  await resetTelemetry();
});

// addTabGroup with multiple tabs should record exactly one MOVE event with the
// correct tab_count, not one event per tab.
add_task(async function test_move_create_group_records_once() {
  await resetTelemetry();

  let tab1 = await addTab("about:blank");
  let tab2 = await addTab("about:blank");
  let tab3 = await addTab("about:blank");

  let tabgroupEditor = document.getElementById("tab-group-editor");
  let panelShown = BrowserTestUtils.waitForPopupEvent(
    tabgroupEditor.panel,
    "shown"
  );

  let tabMoveEvents = [tab1, tab2, tab3].map(tab =>
    BrowserTestUtils.waitForEvent(tab, "TabMove")
  );

  gBrowser.addTabGroup([tab1, tab2, tab3], {
    metricsContext: gBrowser.TabMetrics.userTriggeredContext(
      gBrowser.TabMetrics.METRIC_SOURCE.TAB_MENU
    ),
  });

  await Promise.all(tabMoveEvents);

  // addTabGroup fires TabGroupCreateByUser which opens the editor panel;
  // close it so it doesn't interfere with subsequent tests.
  await panelShown;
  let panelHidden = BrowserTestUtils.waitForPopupEvent(
    tabgroupEditor.panel,
    "hidden"
  );
  tabgroupEditor.panel.hidePopup();
  await panelHidden;
  let events = getInteractionEvents();
  Assert.equal(
    events.length,
    1,
    "One MOVE event for all tabs, not one per tab"
  );
  assertInteractionEvent(events[0], {
    action: TabMetrics.METRIC_ACTION.MOVE,
    source: TabMetrics.METRIC_SOURCE.TAB_MENU,
    tab_count: 3,
  });

  gBrowser.removeAllTabsBut(gBrowser.tabs[0]);
  await resetTelemetry();
});

// Adding multiple tabs to an existing group via addTabs should record exactly
// one MOVE event, not one per tab.
add_task(async function test_move_into_existing_group_records_once() {
  await resetTelemetry();

  let anchorTab = await addTab("about:blank");
  let tab1 = await addTab("about:blank");
  let tab2 = await addTab("about:blank");
  // Create group with anchorTab only, no metricsContext so no event fires.
  let group = gBrowser.addTabGroup([anchorTab]);

  let tabMoveEvents = [tab1, tab2].map(tab =>
    BrowserTestUtils.waitForEvent(tab, "TabMove")
  );

  group.addTabs(
    [tab1, tab2],
    gBrowser.TabMetrics.userTriggeredContext(
      gBrowser.TabMetrics.METRIC_SOURCE.DRAG_AND_DROP
    )
  );

  await Promise.all(tabMoveEvents);
  let events = getInteractionEvents();
  Assert.equal(
    events.length,
    1,
    "One MOVE event for both added tabs, not one per tab"
  );
  assertInteractionEvent(events[0], {
    action: TabMetrics.METRIC_ACTION.MOVE,
    source: TabMetrics.METRIC_SOURCE.DRAG_AND_DROP,
    tab_count: 2,
  });

  gBrowser.removeAllTabsBut(gBrowser.tabs[0]);
  await resetTelemetry();
});

// replaceGroupWithWindow should record DETACH with the correct tab count.
add_task(async function test_detach_group_to_new_window() {
  await resetTelemetry();

  let tab1 = await addTab("about:blank");
  let tab2 = await addTab("about:blank");
  let group = gBrowser.addTabGroup([tab1, tab2]);
  Assert.equal(group.tabs.length, 2, "Group has 2 tabs");

  let tabgroupEditor = document.getElementById("tab-group-editor");
  let panelShown = BrowserTestUtils.waitForPopupEvent(
    tabgroupEditor.panel,
    "shown"
  );
  EventUtils.synthesizeMouseAtCenter(
    group.querySelector(".tab-group-label"),
    { type: "contextmenu", button: 2 },
    window
  );
  await panelShown;

  let newWindowPromise = BrowserTestUtils.waitForNewWindow();
  EventUtils.synthesizeMouseAtCenter(
    document.getElementById("tabGroupEditor_moveGroupToNewWindow"),
    {}
  );
  let newWin = await newWindowPromise;

  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.DETACH,
    source: TabMetrics.METRIC_SOURCE.TAB_GROUP_MENU,
    tab_count: 2,
  });

  await BrowserTestUtils.closeWindow(newWin);
  await resetTelemetry();
});

let openTabsMenu = async () => {
  let viewShown = BrowserTestUtils.waitForEvent(
    document.getElementById("allTabsMenu-allTabsView"),
    "ViewShown"
  );
  document.getElementById("alltabs-button").click();
  return (await viewShown).target;
};

let closeTabsMenu = async () => {
  let panel = document
    .getElementById("allTabsMenu-allTabsView")
    .closest("panel");
  if (!panel) {
    return;
  }
  let hidden = BrowserTestUtils.waitForPopupEvent(panel, "hidden");
  panel.hidePopup();
  await hidden;
};

let openAppMenuTabGroupsList = async () => {
  await new CustomizableUITestUtils(window).openMainMenu();
  let subView = PanelMultiView.getViewNode(
    document,
    "appMenu-tabGroupsListView"
  );
  let viewShown = BrowserTestUtils.waitForEvent(subView, "ViewShown");
  PanelMultiView.getViewNode(document, "appMenu-tab-groups-button").click();
  await viewShown;
  await subView.querySelector("tab-groups-list").updateComplete;
  return subView;
};

let closeAppMenu = async () => {
  if (PanelUI.panel.state == "closed") {
    return;
  }
  await new CustomizableUITestUtils(window).hideMainMenu();
};

// replaceGroupWithWindow should record DETACH with the correct source when
// triggered from the "list all tabs" overflow menu's context menu.
add_task(async function test_detach_group_to_new_window_tab_overflow() {
  await resetTelemetry();

  let tab1 = await addTab("about:blank");
  let tab2 = await addTab("about:blank");
  let group = gBrowser.addTabGroup([tab1, tab2]);
  gBrowser.tabGroupMenu.close();

  let allTabsMenu = await openTabsMenu();
  let tabGroupButton = allTabsMenu.querySelector(
    `#allTabsMenu-groupsView [data-tab-group-id="${group.id}"]`
  );
  let menu = await getContextMenu(
    tabGroupButton,
    "open-tab-group-context-menu"
  );

  let newWindowPromise = BrowserTestUtils.waitForNewWindow();
  menu.querySelector("#open-tab-group-context-menu_moveToNewWindow").click();
  await closeContextMenu(menu);
  await closeTabsMenu();
  let newWin = await newWindowPromise;

  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.DETACH,
    source: TabMetrics.METRIC_SOURCE.TAB_OVERFLOW_MENU,
    tab_count: 2,
  });

  await BrowserTestUtils.closeWindow(newWin);
  await resetTelemetry();
});

// replaceGroupWithWindow should record DETACH with the correct source when
// triggered from the app menu tab groups list's context menu.
add_task(async function test_detach_group_to_new_window_app_menu() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.groups.alternateMenu", true]],
  });
  await resetTelemetry();

  let tab1 = await addTab("about:blank");
  let tab2 = await addTab("about:blank");
  let group = gBrowser.addTabGroup([tab1, tab2]);
  gBrowser.tabGroupMenu.close();

  let subView = await openAppMenuTabGroupsList();
  let tabGroupButton = subView.querySelector(
    `[data-tab-group-id="${group.id}"]`
  );
  let menu = await getContextMenu(
    tabGroupButton,
    "open-tab-group-context-menu"
  );

  let newWindowPromise = BrowserTestUtils.waitForNewWindow();
  menu.querySelector("#open-tab-group-context-menu_moveToNewWindow").click();
  await closeContextMenu(menu);
  let newWin = await newWindowPromise;

  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.DETACH,
    source: TabMetrics.METRIC_SOURCE.APP_MENU_GROUP_LIST,
    tab_count: 2,
  });

  await closeAppMenu();
  await BrowserTestUtils.closeWindow(newWin);
  await resetTelemetry();
  await SpecialPowers.popPrefEnv();
});

let openToolbarTabGroupsList = async () => {
  let view = PanelMultiView.getViewNode(document, "toolbar-tabGroupsListView");
  let viewShown = BrowserTestUtils.waitForEvent(view, "ViewShown");
  document.getElementById("tab-groups-button").click();
  await viewShown;
  await view.querySelector("tab-groups-list").updateComplete;
  return view;
};

let closeToolbarTabGroupsList = async () => {
  let panel = PanelMultiView.getViewNode(
    document,
    "toolbar-tabGroupsListView"
  ).closest("panel");
  if (!panel || panel.state == "closed") {
    return;
  }
  let hidden = BrowserTestUtils.waitForPopupEvent(panel, "hidden");
  panel.hidePopup();
  await hidden;
};

// replaceGroupWithWindow should record DETACH with the correct source when
// triggered from the toolbar button tab groups list's context menu.
add_task(async function test_detach_group_to_new_window_toolbar() {
  // The toolbar button is only registered when its pref is set at startup,
  // so register it the same way CustomizableWidgets does.
  CustomizableUI.createWidget({
    id: "tab-groups-button",
    type: "view",
    viewId: "toolbar-tabGroupsListView",
    label: "Tab groups",
  });
  CustomizableUI.addWidgetToArea(
    "tab-groups-button",
    CustomizableUI.AREA_NAVBAR
  );
  await resetTelemetry();

  let tab1 = await addTab("about:blank");
  let tab2 = await addTab("about:blank");
  let group = gBrowser.addTabGroup([tab1, tab2]);
  gBrowser.tabGroupMenu.close();

  let view = await openToolbarTabGroupsList();
  let tabGroupButton = view.querySelector(`[data-tab-group-id="${group.id}"]`);
  let menu = await getContextMenu(
    tabGroupButton,
    "open-tab-group-context-menu"
  );

  let newWindowPromise = BrowserTestUtils.waitForNewWindow();
  menu.querySelector("#open-tab-group-context-menu_moveToNewWindow").click();
  await closeContextMenu(menu);
  let newWin = await newWindowPromise;

  assertInteractionEvent(getLastInteractionEvent(), {
    action: TabMetrics.METRIC_ACTION.DETACH,
    source: TabMetrics.METRIC_SOURCE.TOOLBAR_GROUP_LIST,
    tab_count: 2,
  });

  await closeToolbarTabGroupsList();
  await BrowserTestUtils.closeWindow(newWin);
  CustomizableUI.destroyWidget("tab-groups-button");
  CustomizableUI.reset();
  await resetTelemetry();
});
