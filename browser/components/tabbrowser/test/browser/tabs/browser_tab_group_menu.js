/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ASRouterTriggerListeners } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouterTriggerListeners.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

const SAVE_HINT_SHOWN_COUNT_PREF = "browser.tabs.groups.saveHintShownCount";
const ALTERNATE_MENU_PREF = "browser.tabs.groups.alternateMenu";
const SAVE_HINT_MESSAGE_ID = "confirmation-hint-tab-group-saved";

add_setup(() => {
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(SAVE_HINT_SHOWN_COUNT_PREF);
  });
});

function readClipboardText() {
  let xferable = Cc["@mozilla.org/widget/transferable;1"].createInstance(
    Ci.nsITransferable
  );
  xferable.init(null);
  xferable.addDataFlavor("text/plain");
  Services.clipboard.getData(xferable, Ci.nsIClipboard.kGlobalClipboard);
  let data = {};
  xferable.getTransferData("text/plain", data);
  return data.value?.QueryInterface(Ci.nsISupportsString)?.data ?? "";
}

async function createTabGroupAndOpenEditPanel(tabs = [], label = "") {
  let tabgroupEditor = document.getElementById("tab-group-editor");
  let tabgroupPanel = tabgroupEditor.panel;
  if (!tabs.length) {
    let tab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
      animate: false,
    });
    tabs = [tab];
  }
  let group = gBrowser.addTabGroup(tabs, { color: "cyan", label });

  let panelShown = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "shown");
  EventUtils.synthesizeMouseAtCenter(
    group.querySelector(".tab-group-label"),
    { type: "contextmenu", button: 2 },
    window
  );
  return new Promise(resolve => {
    panelShown.then(() => {
      resolve({ tabgroupEditor, group });
    });
  });
}

/**
 * Tests behavior of the group management panel.
 */
add_task(async function test_tabGroupCreatePanel() {
  const triggerHandler = sinon.stub();
  const tabGroupCreatedTrigger =
    ASRouterTriggerListeners.get("tabGroupCreated");
  tabGroupCreatedTrigger.uninit();
  tabGroupCreatedTrigger.init(triggerHandler);

  let tabgroupEditor = document.getElementById("tab-group-editor");
  let tabgroupPanel = tabgroupEditor.panel;
  let swatches = tabgroupPanel.querySelector(".tab-group-editor-swatches");
  let nameField = tabgroupPanel.querySelector("#tab-group-name");
  let tab = BrowserTestUtils.addTab(gBrowser, "about:blank");

  let openCreatePanel = async () => {
    let panelShown = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "shown");
    let group = gBrowser.addTabGroup([tab], {
      color: "cyan",
      label: "Food",
      metricsContext: gBrowser.TabMetrics.userTriggeredContext(),
    });
    await panelShown;
    return group;
  };

  let group = await openCreatePanel();
  Assert.equal(tabgroupPanel.state, "open", "Create panel is visible");
  Assert.ok(tabgroupEditor.createMode, "Group editor is in create mode");
  // Edit panel should be populated with correct group details
  Assert.equal(
    document.activeElement,
    nameField,
    "Create panel's input is focused initially"
  );
  Assert.equal(
    nameField.value,
    group.label,
    "Create panel's input populated with correct name"
  );
  Assert.equal(
    swatches.value,
    group.color,
    "Create panel's colorpicker has correct color pre-selected"
  );

  info("New group should be removed after hitting Cancel");
  let panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  let cancelButton = tabgroupPanel.querySelector(
    "#tab-group-editor-button-cancel"
  );
  if (AppConstants.platform == "macosx") {
    cancelButton.click();
  } else {
    cancelButton.focus();
    EventUtils.synthesizeKey("VK_RETURN");
  }
  await panelHidden;
  Assert.ok(!tab.group, "Tab is ungrouped after hitting Cancel");

  info("New group should be removed after hitting Esc");
  group = await openCreatePanel();
  panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  EventUtils.synthesizeKey("KEY_Escape");
  await panelHidden;
  Assert.ok(!tab.group, "Tab is ungrouped after hitting Esc");

  info("New group should remain when dismissing panel");
  group = await openCreatePanel();
  panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  tabgroupPanel.hidePopup();
  await panelHidden;
  Assert.equal(tabgroupPanel.state, "closed", "Tabgroup edit panel is closed");
  Assert.equal(group.label, "Food");
  Assert.equal(group.color, "cyan");
  group.ungroupTabs();

  info("Panel inputs should work correctly");
  group = await openCreatePanel();
  nameField.focus();
  nameField.value = "";
  EventUtils.sendString("Shopping");
  Assert.equal(
    group.label,
    "Shopping",
    "Group label changed when input value changed"
  );
  // moz-visual-picker-item re-dispatches click on its host, which has no
  // accessible of its own.
  AccessibilityUtils.setEnv({ mustHaveAccessibleRule: false });
  tabgroupPanel.querySelector("#tab-group-editor-swatch-red").click();
  AccessibilityUtils.resetEnv();
  Assert.equal(
    group.color,
    "red",
    "Group color changed to red after clicking red swatch"
  );
  Assert.equal(
    swatches.value,
    "red",
    "Red swatch radio selected after clicking red swatch"
  );
  Assert.ok(
    tabgroupPanel.querySelector("#tab-group-editor-swatch-red").checked,
    "Red swatch item is checked after clicking red swatch"
  );

  info(
    "Panel should be dismissed after clicking Create and new group should remain"
  );
  panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  let done = BrowserTestUtils.waitForEvent(
    tabgroupEditor,
    "TabGroupCreateDone"
  );
  tabgroupPanel.querySelector("#tab-group-editor-button-create").click();
  await panelHidden;
  await done;
  Assert.ok(triggerHandler.called, "Called after tab group created");
  Assert.equal(tabgroupPanel.state, "closed", "Tabgroup edit panel is closed");
  Assert.equal(group.label, "Shopping");
  Assert.equal(group.color, "red");

  let rightClickGroupLabel = async () => {
    info("right-clicking on the group label should reopen panel in edit mode");
    let panelShown = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "shown");
    EventUtils.synthesizeMouseAtCenter(
      group.querySelector(".tab-group-label"),
      { type: "contextmenu", button: 2 },
      window
    );
    await panelShown;
    Assert.equal(tabgroupPanel.state, "open", "Tabgroup edit panel is open");
    Assert.ok(!tabgroupEditor.createMode, "Group editor is not in create mode");
  };

  info("Panel should be dismissed after hitting Enter and group should remain");
  await rightClickGroupLabel();
  panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  EventUtils.synthesizeKey("VK_RETURN");
  await panelHidden;
  Assert.equal(tabgroupPanel.state, "closed", "Tabgroup edit panel is closed");
  Assert.equal(group.label, "Shopping");
  Assert.equal(group.color, "red");

  await rightClickGroupLabel();
  info("Esc key should should close the edit panel");
  panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  EventUtils.synthesizeKey("KEY_Escape");
  await panelHidden;
  Assert.equal(tabgroupPanel.state, "closed", "Tabgroup edit panel is closed");
  Assert.equal(group.label, "Shopping");
  Assert.equal(group.color, "red");

  await rightClickGroupLabel();
  info("Removing group via delete button");
  panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  let removePromise = BrowserTestUtils.waitForEvent(group, "TabGroupRemoved");
  let deleteButton = tabgroupPanel.querySelector("#tabGroupEditor_deleteGroup");
  if (AppConstants.platform == "macosx") {
    deleteButton.click();
  } else {
    deleteButton.focus();
    EventUtils.synthesizeKey("VK_RETURN");
  }
  await Promise.all([panelHidden, removePromise]);
  tabGroupCreatedTrigger.uninit();
});

add_task(async function test_tabGroupPanelAddTab() {
  let { tabgroupEditor, group } = await createTabGroupAndOpenEditPanel(
    [],
    "test_tabGroupPanelAddTab"
  );
  let tabgroupPanel = tabgroupEditor.panel;

  let addNewTabButton = tabgroupPanel.querySelector(
    "#tabGroupEditor_addNewTabInGroup"
  );

  Assert.equal(group.tabs.length, 1, "Group has 1 tab");
  let panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  addNewTabButton.click();
  await panelHidden;
  Assert.strictEqual(tabgroupPanel.state, "closed", "Group editor is closed");
  Assert.equal(group.tabs.length, 2, "Group has 2 tabs");

  await removeTabGroup(group);
});

add_task(async function test_tabGroupPanelUngroupTabs() {
  let { tabgroupEditor, group } = await createTabGroupAndOpenEditPanel(
    [],
    "test_tabGroupPanelAddTab"
  );
  let tabgroupPanel = tabgroupEditor.panel;
  let tab = group.tabs[0];
  let ungroupTabsButton = tabgroupPanel.querySelector(
    "#tabGroupEditor_ungroupTabs"
  );

  Assert.equal(tab.group.id, group.id, "Tab is in group");
  let panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  ungroupTabsButton.click();
  await panelHidden;
  Assert.ok(!tab.group, "Tab is no longer grouped");

  BrowserTestUtils.removeTab(tab);
});

/**
 * Tests that the "move group to new window" correctly moves a group
 * to a new window, preserving tab selection and order.
 */
add_task(async function test_moveGroupToNewWindow() {
  let tabs = [
    BrowserTestUtils.addTab(gBrowser, "about:cache", {
      skipAnimation: true,
    }),
    BrowserTestUtils.addTab(gBrowser, "about:robots", {
      skipAnimation: true,
    }),
    BrowserTestUtils.addTab(gBrowser, "about:mozilla", {
      skipAnimation: true,
    }),
  ];
  gBrowser.selectedTab = tabs[1];
  let assertTabsInCorrectOrder = tabsToCheck => {
    Assert.equal(
      tabsToCheck[0].linkedBrowser.currentURI.spec,
      "about:cache",
      "about:cache is first"
    );
    Assert.equal(
      tabsToCheck[1].linkedBrowser.currentURI.spec,
      "about:robots",
      "about:robots is second"
    );
    Assert.equal(
      tabsToCheck[2].linkedBrowser.currentURI.spec,
      "about:mozilla",
      "about:mozilla is third"
    );
  };
  let { group } = await createTabGroupAndOpenEditPanel(
    tabs,
    "test_moveGroupToNewWindow"
  );

  let newWindowOpened = BrowserTestUtils.waitForNewWindow();
  document.getElementById("tabGroupEditor_moveGroupToNewWindow").click();
  let newWin = await newWindowOpened;
  Assert.notEqual(newWin, window, "Group is moved to new window");

  let movedTabs = newWin.gBrowser.tabs;
  let movedGroup = movedTabs[0].group;
  Assert.equal(movedGroup.id, group.id, "Tab is in original group");

  Assert.equal(
    newWin.gBrowser.selectedTab,
    newWin.gBrowser.tabs[1],
    "Second tab remains selected"
  );
  assertTabsInCorrectOrder(newWin.gBrowser.tabs);
  let tabgroupEditor = newWin.document.getElementById("tab-group-editor");
  let panelOpen = BrowserTestUtils.waitForPopupEvent(
    tabgroupEditor.panel,
    "shown"
  );
  tabgroupEditor.openEditModal(movedGroup);
  await panelOpen;

  let moveGroupButton = newWin.document.getElementById(
    "tabGroupEditor_moveGroupToNewWindow"
  );
  Assert.ok(
    moveGroupButton.disabled,
    "Button is disabled when group is only thing in window"
  );

  let panelHidden = BrowserTestUtils.waitForPopupEvent(
    tabgroupEditor.panel,
    "hidden"
  );
  tabgroupEditor.panel.hidePopup();
  await panelHidden;

  BrowserTestUtils.addTab(newWin.gBrowser, "about:blank", {
    skipAnimation: true,
  });
  panelOpen = BrowserTestUtils.waitForPopupEvent(tabgroupEditor.panel, "shown");
  tabgroupEditor.openEditModal(movedGroup);
  await panelOpen;
  Assert.ok(
    !moveGroupButton.disabled,
    "Button is enabled again when additional tab present"
  );
  await removeTabGroup(movedGroup);
  await BrowserTestUtils.closeWindow(newWin, { animate: false });
});

/**
 * The "save & close" button in the tabgroup menu should be disabled if the
 * group is not saveable.
 */
add_task(async function test_saveDisabledForUnimportantGroup() {
  let { tabgroupEditor, group } = await createTabGroupAndOpenEditPanel(
    [],
    "test_saveDisabledForUnimportantGroups"
  );
  let saveAndCloseGroupButton = tabgroupEditor.panel.querySelector(
    "#tabGroupEditor_saveAndCloseGroup"
  );
  Assert.ok(
    saveAndCloseGroupButton.disabled,
    "Save button is disabled for newtab-only group"
  );
  let panelHidden = BrowserTestUtils.waitForPopupEvent(
    tabgroupEditor.panel,
    "hidden"
  );
  tabgroupEditor.panel.hidePopup();
  await panelHidden;
  await removeTabGroup(group);
});

add_task(async function test_saveAndCloseGroup() {
  // Suppress the confirmation hint so it doesn't bleed into the next test.
  await SpecialPowers.pushPrefEnv({ set: [[SAVE_HINT_SHOWN_COUNT_PREF, 3]] });
  const triggerHandler = sinon.stub();
  const tabGroupSavedTrigger = ASRouterTriggerListeners.get("tabGroupSaved");
  tabGroupSavedTrigger.uninit();
  tabGroupSavedTrigger.init(triggerHandler);

  let tab = await addTab("about:mozilla");
  let { tabgroupEditor, group } = await createTabGroupAndOpenEditPanel(
    [tab],
    "test_saveAndCloseGroup"
  );
  let tabgroupPanel = tabgroupEditor.panel;
  await TabStateFlusher.flush(tab.linkedBrowser);
  let saveAndCloseGroupButton = tabgroupPanel.querySelector(
    "#tabGroupEditor_saveAndCloseGroup"
  );

  Assert.ok(gBrowser.getTabGroupById(group.id), "Group exists in browser");

  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();

  let events = [
    BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden"),
    BrowserTestUtils.waitForEvent(group, "TabGroupSaved"),
    BrowserTestUtils.waitForEvent(group, "TabGroupRemoved"),
  ];
  saveAndCloseGroupButton.click();
  await Promise.all(events);

  Assert.ok(triggerHandler.calledOnce, "Called once after tab group saved");
  Assert.ok(
    !gBrowser.getTabGroupById(group.id),
    "Group was removed from browser"
  );
  Assert.ok(SessionStore.getSavedTabGroup(group.id), "Group is in savedGroups");

  let tabGroupSaveTelemetry = Glean.tabgroup.save.testGetValue();
  Assert.equal(tabGroupSaveTelemetry.length, 1, "Should be one recorded save");
  Assert.deepEqual(
    tabGroupSaveTelemetry[0].extra,
    {
      user_triggered: "true",
      id: group.id,
    },
    "tabgroup.save event extra_keys has correct values after tab group save"
  );

  let tabGroupDeleteTelemetry = Glean.tabgroup.delete.testGetValue();
  Assert.equal(
    tabGroupDeleteTelemetry.length,
    1,
    "Should be one recorded delete"
  );
  Assert.deepEqual(
    tabGroupDeleteTelemetry[0].extra,
    {
      id: group.id,
      source: gBrowser.TabMetrics.METRIC_SOURCE.TAB_GROUP_MENU,
    },
    "tabgroup.delete event extra_keys has correct values after tab group delete"
  );

  SessionStore.forgetSavedTabGroup(group.id);

  BrowserTestUtils.removeTab(tab);
  tabGroupSavedTrigger.uninit();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that saving and closing a group via the tab group menu's
 * "Save and close group" button shows the "Tab group saved" confirmation
 * hint, anchored to the List All Tabs button when the alternate menu is
 * disabled.
 */
add_task(async function test_saveAndCloseGroup_showsConfirmationHint() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SAVE_HINT_SHOWN_COUNT_PREF, 0],
      [ALTERNATE_MENU_PREF, false],
    ],
  });
  let showSpy = sinon.spy(window.ConfirmationHint, "show");
  let panel = window.ConfirmationHint._panel;

  let tab = await addTab("about:mozilla");
  let { tabgroupEditor, group } = await createTabGroupAndOpenEditPanel(
    [tab],
    "test_saveAndCloseGroup_showsConfirmationHint"
  );
  let tabgroupPanel = tabgroupEditor.panel;
  await TabStateFlusher.flush(tab.linkedBrowser);
  let saveAndCloseGroupButton = tabgroupPanel.querySelector(
    "#tabGroupEditor_saveAndCloseGroup"
  );

  let removed = BrowserTestUtils.waitForEvent(group, "TabGroupRemoved");
  let shown = BrowserTestUtils.waitForPopupEvent(panel, "shown");
  saveAndCloseGroupButton.click();
  await removed;
  await shown;
  Assert.ok(
    BrowserTestUtils.isVisible(panel),
    "Confirmation hint panel is visible"
  );

  Assert.ok(
    showSpy.calledOnceWith(
      sinon.match.same(document.getElementById("alltabs-button")),
      SAVE_HINT_MESSAGE_ID
    ),
    "Confirmation hint was shown once, anchored to the List All Tabs button"
  );
  Assert.equal(
    Services.prefs.getIntPref(SAVE_HINT_SHOWN_COUNT_PREF),
    1,
    "Shown-count pref was incremented"
  );

  let hidden = BrowserTestUtils.waitForPopupEvent(panel, "hidden");
  panel.hidePopup();
  await hidden;
  SessionStore.forgetSavedTabGroup(group.id);
  showSpy.restore();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that saving and closing a group still shows the confirmation hint
 * when the tab being closed is the currently selected tab, which forces a
 * tab switch as part of the close.
 */
add_task(
  async function test_saveAndCloseSelectedTabGroup_showsConfirmationHint() {
    await SpecialPowers.pushPrefEnv({ set: [[SAVE_HINT_SHOWN_COUNT_PREF, 0]] });
    let panel = window.ConfirmationHint._panel;

    let tab = await addTab("about:mozilla");
    let group = gBrowser.addTabGroup([tab], {
      color: "cyan",
      label: "test_saveAndCloseSelectedTabGroup_showsConfirmationHint",
    });
    await TabStateFlusher.flush(tab.linkedBrowser);
    await BrowserTestUtils.switchTab(gBrowser, tab);

    let shown = BrowserTestUtils.waitForPopupEvent(panel, "shown");
    await TabGroupTestUtils.saveAndCloseTabGroup(group);
    await shown;
    Assert.ok(
      BrowserTestUtils.isVisible(panel),
      "Confirmation hint panel is visible"
    );

    let hidden = BrowserTestUtils.waitForPopupEvent(panel, "hidden");
    panel.hidePopup();
    await hidden;
    SessionStore.forgetSavedTabGroup(group.id);
    await SpecialPowers.popPrefEnv();
  }
);

/**
 * Tests that middle-clicking a tab group's label saves and closes the group
 * and shows the "Tab group saved" confirmation hint, same as using the
 * "Save and close group" button.
 */
add_task(async function test_middleClickGroupLabel_showsConfirmationHint() {
  await SpecialPowers.pushPrefEnv({ set: [[SAVE_HINT_SHOWN_COUNT_PREF, 0]] });
  let showSpy = sinon.spy(window.ConfirmationHint, "show");
  let panel = window.ConfirmationHint._panel;

  let tab = await addTab("about:mozilla");
  let group = gBrowser.addTabGroup([tab], {
    color: "cyan",
    label: "test_middleClickGroupLabel_showsConfirmationHint",
  });
  await TabStateFlusher.flush(tab.linkedBrowser);

  let removed = BrowserTestUtils.waitForEvent(group, "TabGroupRemoved");
  let shown = BrowserTestUtils.waitForPopupEvent(panel, "shown");
  EventUtils.synthesizeMouseAtCenter(
    group.querySelector(".tab-group-label"),
    { button: 1 },
    window
  );
  await removed;
  await shown;
  Assert.ok(
    BrowserTestUtils.isVisible(panel),
    "Confirmation hint panel is visible"
  );

  Assert.ok(
    showSpy.calledOnceWith(sinon.match.any, SAVE_HINT_MESSAGE_ID),
    "Confirmation hint was shown once after middle-clicking the group label"
  );

  let hidden = BrowserTestUtils.waitForPopupEvent(panel, "hidden");
  panel.hidePopup();
  await hidden;
  SessionStore.forgetSavedTabGroup(group.id);
  showSpy.restore();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that an implicit save (e.g. via `save()` alone, as happens when a
 * window with tab groups is closed) does not show the confirmation hint,
 * since that isn't an explicit user action to save and close a group.
 */
add_task(async function test_implicitSave_doesNotShowConfirmationHint() {
  await SpecialPowers.pushPrefEnv({ set: [[SAVE_HINT_SHOWN_COUNT_PREF, 0]] });
  let showSpy = sinon.spy(window.ConfirmationHint, "show");

  let tab = await addTab("about:mozilla");
  let group = gBrowser.addTabGroup([tab], {
    color: "cyan",
    label: "test_implicitSave_doesNotShowConfirmationHint",
  });
  await TabStateFlusher.flush(tab.linkedBrowser);
  let saved = BrowserTestUtils.waitForEvent(group, "TabGroupSaved");
  group.save();
  await saved;

  Assert.ok(
    showSpy.notCalled,
    "Confirmation hint was not shown for an implicit save"
  );

  SessionStore.forgetSavedTabGroup(group.id);
  await TabGroupTestUtils.removeTabGroup(group);
  showSpy.restore();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that the confirmation hint only appears the first
 * MAX_SAVE_HINT_SHOWN_COUNT (3) times a group is saved and closed.
 */
add_task(async function test_saveAndCloseGroup_confirmationHintCappedAtThree() {
  await SpecialPowers.pushPrefEnv({ set: [[SAVE_HINT_SHOWN_COUNT_PREF, 0]] });
  let showSpy = sinon.spy(window.ConfirmationHint, "show");
  let panel = window.ConfirmationHint._panel;

  for (let i = 1; i <= 4; i++) {
    let tab = await addTab("about:mozilla");
    let group = gBrowser.addTabGroup([tab], {
      color: "cyan",
      label: `test_saveAndCloseGroup_confirmationHintCappedAtThree_${i}`,
    });
    await TabGroupTestUtils.saveAndCloseTabGroup(group);
    SessionStore.forgetSavedTabGroup(group.id);
    // Showing the hint again while it is still opening leaves a stray
    // auto-hide timer that can close a later test's hint.
    if (i <= 3) {
      await TestUtils.waitForCondition(
        () => panel.state == "open",
        "Confirmation hint is open"
      );
    }
  }

  Assert.equal(
    showSpy.callCount,
    3,
    "Confirmation hint was only shown the first 3 times"
  );
  Assert.equal(
    Services.prefs.getIntPref(SAVE_HINT_SHOWN_COUNT_PREF),
    3,
    "Shown-count pref stops incrementing once it reaches the cap"
  );

  // Close the last confirmation hint so it doesn't bleed into the next test
  if (panel.state != "closed") {
    let hidden = BrowserTestUtils.waitForPopupEvent(panel, "hidden");
    panel.hidePopup();
    await hidden;
  }

  showSpy.restore();
  await SpecialPowers.popPrefEnv();
});

/**
 * Saves and closes a freshly created group, and asserts that the
 * confirmation hint is shown, visible, and anchored to `expectedAnchorId`.
 *
 * @param {string} expectedAnchorId
 */
async function assertSaveAndCloseHintAnchor(expectedAnchorId) {
  let showSpy = sinon.spy(window.ConfirmationHint, "show");
  let panel = window.ConfirmationHint._panel;

  let tab = await addTab("about:mozilla");
  let group = gBrowser.addTabGroup([tab], {
    color: "cyan",
    label: `assertSaveAndCloseHintAnchor_${expectedAnchorId}`,
  });
  let shown = BrowserTestUtils.waitForPopupEvent(panel, "shown");
  await TabGroupTestUtils.saveAndCloseTabGroup(group);
  await shown;
  Assert.ok(
    BrowserTestUtils.isVisible(panel),
    "Confirmation hint panel is visible"
  );
  Assert.ok(
    showSpy.calledOnceWith(
      sinon.match.same(document.getElementById(expectedAnchorId)),
      SAVE_HINT_MESSAGE_ID
    ),
    `Confirmation hint anchors to #${expectedAnchorId}`
  );

  let hidden = BrowserTestUtils.waitForPopupEvent(panel, "hidden");
  panel.hidePopup();
  await hidden;
  SessionStore.forgetSavedTabGroup(group.id);
  showSpy.restore();
}

/**
 * Tests that the confirmation hint falls back to the application menu
 * button when the alternate menu is enabled but its Tab Groups button
 * is unused.
 */
add_task(
  async function test_saveAndCloseGroup_confirmationHintFallsBackWhenTabGroupsButtonUnused() {
    await SpecialPowers.pushPrefEnv({
      set: [
        [SAVE_HINT_SHOWN_COUNT_PREF, 0],
        [ALTERNATE_MENU_PREF, true],
      ],
    });
    CustomizableUI.createWidget({
      id: "tab-groups-button",
      type: "view",
      viewId: "toolbar-tabGroupsListView",
      l10nId: "toolbar-button-tab-groups",
    });

    await assertSaveAndCloseHintAnchor("PanelUI-menu-button");

    CustomizableUI.destroyWidget("tab-groups-button");
    await SpecialPowers.popPrefEnv();
  }
);

/**
 * Tests that the confirmation hint anchors to the Tab Groups button when
 * the alternate menu is enabled and the button is on a toolbar.
 */
add_task(
  async function test_saveAndCloseGroup_confirmationHintAnchorsToTabGroupsButton() {
    await SpecialPowers.pushPrefEnv({
      set: [
        [SAVE_HINT_SHOWN_COUNT_PREF, 0],
        [ALTERNATE_MENU_PREF, true],
      ],
    });
    CustomizableUI.createWidget({
      id: "tab-groups-button",
      type: "view",
      viewId: "toolbar-tabGroupsListView",
      l10nId: "toolbar-button-tab-groups",
    });
    CustomizableUI.addWidgetToArea(
      "tab-groups-button",
      CustomizableUI.AREA_NAVBAR
    );

    await assertSaveAndCloseHintAnchor("tab-groups-button");

    CustomizableUI.removeWidgetFromArea("tab-groups-button");
    CustomizableUI.destroyWidget("tab-groups-button");
    await SpecialPowers.popPrefEnv();
  }
);

/**
 * Tests that "Copy all links in group" copies the URLs of all tabs in the
 * group to the clipboard.
 */
add_task(async function test_copyAllLinksInGroup() {
  let tab1 = await addTab("https://example.com/1");
  let tab2 = await addTab("https://example.com/2");
  let { tabgroupEditor, group } = await createTabGroupAndOpenEditPanel(
    [tab1, tab2],
    "test_copyAllLinksInGroup"
  );
  let tabgroupPanel = tabgroupEditor.panel;

  let copyAllLinksButton = tabgroupPanel.querySelector(
    "#tabGroupEditor_copyAllLinks"
  );
  Assert.ok(copyAllLinksButton, "Copy all links button exists");

  let panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  copyAllLinksButton.click();
  await panelHidden;

  let clipboardText = readClipboardText();

  Assert.ok(
    clipboardText.includes("https://example.com/1"),
    "Clipboard contains first tab URL"
  );
  Assert.ok(
    clipboardText.includes("https://example.com/2"),
    "Clipboard contains second tab URL"
  );

  Services.clipboard.emptyClipboard(Ci.nsIClipboard.kGlobalClipboard);
  await removeTabGroup(group);
});

/**
 * Tests that "Copy all links in group" works with a single tab.
 */
add_task(async function test_copyAllLinksInGroup_singleTab() {
  let tab = await addTab("https://example.com/single");
  let { tabgroupEditor, group } = await createTabGroupAndOpenEditPanel(
    [tab],
    "test_copyAllLinksInGroup_singleTab"
  );
  let tabgroupPanel = tabgroupEditor.panel;

  let copyAllLinksButton = tabgroupPanel.querySelector(
    "#tabGroupEditor_copyAllLinks"
  );
  Assert.ok(!copyAllLinksButton.disabled, "Copy link button is enabled");

  let panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  copyAllLinksButton.click();
  await panelHidden;

  let clipboardText = readClipboardText();

  Assert.ok(
    clipboardText.includes("https://example.com/single"),
    "Clipboard contains the tab URL"
  );

  Services.clipboard.emptyClipboard(Ci.nsIClipboard.kGlobalClipboard);
  await removeTabGroup(group);
});

/**
 * Tests that the swatches are positioned before the name field.
 */
add_task(async function test_swatchLayout() {
  let { tabgroupEditor, group } = await createTabGroupAndOpenEditPanel(
    [],
    "test_swatchLayout"
  );
  let tabgroupPanel = tabgroupEditor.panel;
  let swatches = tabgroupPanel.querySelector(".tab-group-editor-swatches");
  let nameContainer = tabgroupPanel.querySelector(".tab-group-editor-name");
  let propertiesActions = tabgroupPanel.querySelector(
    "#tab-group-properties-and-actions"
  );

  Assert.equal(
    swatches.nextElementSibling,
    nameContainer,
    "Swatches are directly above the name field"
  );
  Assert.notEqual(
    swatches.parentElement,
    propertiesActions,
    "Swatches are not in the properties-and-actions container"
  );

  let panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  tabgroupPanel.hidePopup();
  await panelHidden;
  await removeTabGroup(group);
});
