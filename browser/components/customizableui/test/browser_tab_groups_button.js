/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  TabGroupTestUtils: "resource://testing-common/TabGroupTestUtils.sys.mjs",
  TabGroupsToolbarButton:
    "moz-src:///browser/components/tabbrowser/TabGroupsToolbarButton.sys.mjs",
});

const TAB_GROUPS_BUTTON_ID = "tab-groups-button";
const TAB_GROUPS_BUTTON_INTRODUCED =
  "browser.toolbarbuttons.introduced.tab-groups-button";
const REMOVED_HINT_SHOWN = "browser.tabs.groups.removedHintShown";

add_setup(async () => {
  CustomizableUI.reset();
  // Keep the removed hint out of the tasks that don't test it.
  await SpecialPowers.pushPrefEnv({
    set: [[REMOVED_HINT_SHOWN, true]],
  });
});

registerCleanupFunction(() => {
  CustomizableUI.removeListener(TabGroupsToolbarButton);
  CustomizableUI.reset();
});

add_task(async function test_is_removable() {
  Assert.ok(
    CustomizableUI.isWidgetRemovable(TAB_GROUPS_BUTTON_ID),
    "The tab groups button should be removable"
  );
});

add_task(async function test_can_be_added_to_area() {
  CustomizableUI.addWidgetToArea(
    TAB_GROUPS_BUTTON_ID,
    CustomizableUI.AREA_FIXED_OVERFLOW_PANEL
  );
  Assert.equal(
    CustomizableUI.getPlacementOfWidget(TAB_GROUPS_BUTTON_ID).area,
    CustomizableUI.AREA_FIXED_OVERFLOW_PANEL,
    "Tab groups button should be in the overflow area"
  );
  CustomizableUI.reset();
});

add_task(async function test_panel_contents() {
  CustomizableUI.addWidgetToArea(
    TAB_GROUPS_BUTTON_ID,
    CustomizableUI.AREA_FIXED_OVERFLOW_PANEL
  );
  await waitForOverflowButtonShown();
  await document.getElementById("nav-bar").overflowable.show();

  const button = document.getElementById(TAB_GROUPS_BUTTON_ID);
  Assert.ok(button, "Tab group button appears in Panel Menu");
  button.click();

  let view = document.getElementById("toolbar-tabGroupsListView");
  await BrowserTestUtils.waitForEvent(view, "ViewShown");

  let header = view.querySelector(".panel-header");
  Assert.ok(header, "toolbar menu has a header");
  Assert.equal(
    header.querySelector("span").textContent,
    "Tab groups",
    "toolbar menu should be titled 'Tab groups'"
  );
  Assert.ok(
    view.querySelector("tab-groups-list"),
    "toolbar menu has a tab groups list component"
  );

  let viewPanel = view.closest("panel");
  let panelHidden = BrowserTestUtils.waitForPopupEvent(viewPanel, "hidden");
  viewPanel.hidePopup();
  await panelHidden;

  if (isOverflowOpen()) {
    await hideOverflow();
  }
});

function assertButtonAfterAlltabs(message) {
  let alltabs = CustomizableUI.getPlacementOfWidget("alltabs-button");
  let placement = CustomizableUI.getPlacementOfWidget(TAB_GROUPS_BUTTON_ID);
  Assert.equal(placement?.area, alltabs.area, `${message}: same area`);
  Assert.equal(
    placement?.position,
    alltabs.position + 1,
    `${message}: right after List All Tabs`
  );
}

add_task(async function test_auto_add_places_button_once() {
  CustomizableUI.reset();
  await SpecialPowers.pushPrefEnv({
    set: [[TAB_GROUPS_BUTTON_INTRODUCED, false]],
  });

  TabGroupsToolbarButton.maybeAddTabGroupsButton();
  assertButtonAfterAlltabs("Auto-add places the button");
  Assert.ok(
    Services.prefs.getBoolPref(TAB_GROUPS_BUTTON_INTRODUCED),
    "Auto-add sets the introduced pref"
  );

  CustomizableUI.removeWidgetFromArea(TAB_GROUPS_BUTTON_ID);
  TabGroupsToolbarButton.maybeAddTabGroupsButton();
  Assert.ok(
    !CustomizableUI.getPlacementOfWidget(TAB_GROUPS_BUTTON_ID),
    "Auto-add doesn't add the button a second time"
  );

  await SpecialPowers.popPrefEnv();
  CustomizableUI.reset();
});

add_task(async function test_auto_add_falls_back_to_navbar() {
  let assertButtonBeforeExtensions = message => {
    let navbar = CustomizableUI.getWidgetIdsInArea(CustomizableUI.AREA_NAVBAR);
    Assert.equal(
      navbar[navbar.indexOf("unified-extensions-button") - 1],
      TAB_GROUPS_BUTTON_ID,
      message
    );
  };

  CustomizableUI.reset();
  await SpecialPowers.pushPrefEnv({
    set: [[TAB_GROUPS_BUTTON_INTRODUCED, false]],
  });
  CustomizableUI.removeWidgetFromArea("alltabs-button");
  TabGroupsToolbarButton.maybeAddTabGroupsButton();
  assertButtonBeforeExtensions(
    "Without List All Tabs, the button goes before the extensions button"
  );
  await SpecialPowers.popPrefEnv();

  CustomizableUI.reset();
  await SpecialPowers.pushPrefEnv({
    set: [[TAB_GROUPS_BUTTON_INTRODUCED, false]],
  });
  CustomizableUI.addWidgetToArea(
    "alltabs-button",
    CustomizableUI.AREA_FIXED_OVERFLOW_PANEL
  );
  TabGroupsToolbarButton.maybeAddTabGroupsButton();
  assertButtonBeforeExtensions(
    "With List All Tabs in the overflow menu, the button goes before the extensions button"
  );

  await SpecialPowers.popPrefEnv();
  CustomizableUI.reset();
});

add_task(async function test_auto_add_respects_user_placement() {
  CustomizableUI.reset();
  await SpecialPowers.pushPrefEnv({
    set: [[TAB_GROUPS_BUTTON_INTRODUCED, false]],
  });
  CustomizableUI.addWidgetToArea(
    TAB_GROUPS_BUTTON_ID,
    CustomizableUI.AREA_FIXED_OVERFLOW_PANEL
  );

  TabGroupsToolbarButton.maybeAddTabGroupsButton();
  Assert.equal(
    CustomizableUI.getPlacementOfWidget(TAB_GROUPS_BUTTON_ID)?.area,
    CustomizableUI.AREA_FIXED_OVERFLOW_PANEL,
    "Auto-add doesn't move a button the user already placed"
  );
  Assert.ok(
    Services.prefs.getBoolPref(TAB_GROUPS_BUTTON_INTRODUCED),
    "Finding the button already placed sets the introduced pref"
  );

  await SpecialPowers.popPrefEnv();
  CustomizableUI.reset();
  await SpecialPowers.pushPrefEnv({
    set: [[TAB_GROUPS_BUTTON_INTRODUCED, false]],
  });

  TabGroupsToolbarButton.init();
  CustomizableUI.addWidgetToArea(
    TAB_GROUPS_BUTTON_ID,
    CustomizableUI.AREA_NAVBAR
  );
  CustomizableUI.removeWidgetFromArea(TAB_GROUPS_BUTTON_ID);
  Assert.ok(
    Services.prefs.getBoolPref(TAB_GROUPS_BUTTON_INTRODUCED),
    "Removing the button sets the introduced pref"
  );

  TabGroupsToolbarButton.maybeAddTabGroupsButton();
  Assert.ok(
    !CustomizableUI.getPlacementOfWidget(TAB_GROUPS_BUTTON_ID),
    "Auto-add doesn't add back a button the user removed"
  );

  CustomizableUI.removeListener(TabGroupsToolbarButton);
  await SpecialPowers.popPrefEnv();
  CustomizableUI.reset();
});

async function openCreateTabGroupPanel() {
  let tab = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let panel = document.getElementById("tab-group-editor").panel;
  let panelShown = BrowserTestUtils.waitForPopupEvent(panel, "shown");
  let group = gBrowser.addTabGroup([tab], {
    metricsContext: gBrowser.TabMetrics.userTriggeredContext(),
  });
  await panelShown;
  return { tab, group, panel };
}

add_task(async function test_auto_add_on_tab_group_create() {
  CustomizableUI.reset();
  await SpecialPowers.pushPrefEnv({
    set: [[TAB_GROUPS_BUTTON_INTRODUCED, false]],
  });

  let cancelled = await openCreateTabGroupPanel();
  let panelHidden = BrowserTestUtils.waitForPopupEvent(
    cancelled.panel,
    "hidden"
  );
  EventUtils.synthesizeMouseAtCenter(
    cancelled.panel.querySelector("#tab-group-editor-button-cancel"),
    {}
  );
  await panelHidden;
  Assert.ok(
    !CustomizableUI.getPlacementOfWidget(TAB_GROUPS_BUTTON_ID),
    "Cancelling tab group creation doesn't add the button"
  );
  Assert.ok(
    !Services.prefs.getBoolPref(TAB_GROUPS_BUTTON_INTRODUCED),
    "Cancelling tab group creation doesn't set the introduced pref"
  );
  BrowserTestUtils.removeTab(cancelled.tab);

  let created = await openCreateTabGroupPanel();
  let done = BrowserTestUtils.waitForEvent(
    document.getElementById("tab-group-editor"),
    "TabGroupCreateDone"
  );
  EventUtils.synthesizeMouseAtCenter(
    created.panel.querySelector("#tab-group-editor-button-create"),
    {}
  );
  await done;
  assertButtonAfterAlltabs("Creating a tab group adds the button");

  await TabGroupTestUtils.removeTabGroup(created.group);
  await SpecialPowers.popPrefEnv();
  CustomizableUI.reset();
});

add_task(async function test_auto_add_on_startup_with_saved_groups() {
  CustomizableUI.reset();
  await SpecialPowers.pushPrefEnv({
    set: [[TAB_GROUPS_BUTTON_INTRODUCED, false]],
  });
  TabGroupTestUtils.forgetSavedTabGroups();

  TabGroupsToolbarButton.init();
  Assert.ok(
    !CustomizableUI.getPlacementOfWidget(TAB_GROUPS_BUTTON_ID),
    "Starting up without saved tab groups doesn't add the button"
  );

  let tab = BrowserTestUtils.addTab(gBrowser, "https://example.com/");
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  let group = gBrowser.addTabGroup([tab]);
  await TabGroupTestUtils.saveAndCloseTabGroup(group);
  Assert.ok(
    !CustomizableUI.getPlacementOfWidget(TAB_GROUPS_BUTTON_ID),
    "Creating a group without the create panel doesn't add the button"
  );

  TabGroupsToolbarButton.init();
  Assert.ok(
    CustomizableUI.getPlacementOfWidget(TAB_GROUPS_BUTTON_ID),
    "Starting up with saved tab groups adds the button"
  );

  TabGroupTestUtils.forgetSavedTabGroups();
  Services.prefs.clearUserPref("browser.tabs.groups.saveHintShownCount");
  CustomizableUI.removeListener(TabGroupsToolbarButton);
  await SpecialPowers.popPrefEnv();
  CustomizableUI.reset();
});

async function setupRemovedHintTest() {
  CustomizableUI.reset();
  await SpecialPowers.pushPrefEnv({
    set: [
      [TAB_GROUPS_BUTTON_INTRODUCED, true],
      [REMOVED_HINT_SHOWN, false],
    ],
  });
  TabGroupsToolbarButton.init();
  CustomizableUI.addWidgetToArea(
    TAB_GROUPS_BUTTON_ID,
    CustomizableUI.AREA_NAVBAR
  );
  return sinon.stub(window.ConfirmationHint, "show");
}

async function cleanupRemovedHintTest(showStub) {
  showStub.restore();
  CustomizableUI.removeListener(TabGroupsToolbarButton);
  await SpecialPowers.popPrefEnv();
  CustomizableUI.reset();
}

add_task(async function test_removed_hint_shown_once() {
  let showStub = await setupRemovedHintTest();

  CustomizableUI.removeWidgetFromArea(TAB_GROUPS_BUTTON_ID);
  await TestUtils.waitForTick();
  Assert.ok(showStub.calledOnce, "Removing the button shows the hint");
  let [anchor, messageId] = showStub.firstCall?.args ?? [];
  Assert.equal(
    anchor.id,
    "PanelUI-menu-button",
    "The hint points at the application menu button"
  );
  Assert.equal(
    messageId,
    "confirmation-hint-tab-group-button-removed",
    "The hint uses the removed button message"
  );
  Assert.ok(
    Services.prefs.getBoolPref(REMOVED_HINT_SHOWN),
    "Showing the hint sets the shown pref"
  );

  CustomizableUI.addWidgetToArea(
    TAB_GROUPS_BUTTON_ID,
    CustomizableUI.AREA_NAVBAR
  );
  CustomizableUI.removeWidgetFromArea(TAB_GROUPS_BUTTON_ID);
  await TestUtils.waitForTick();
  Assert.ok(showStub.calledOnce, "Removing the button again shows no hint");

  await cleanupRemovedHintTest(showStub);
});

add_task(async function test_removed_hint_not_shown_on_move() {
  let showStub = await setupRemovedHintTest();

  CustomizableUI.addWidgetToArea(
    TAB_GROUPS_BUTTON_ID,
    CustomizableUI.AREA_FIXED_OVERFLOW_PANEL
  );
  await TestUtils.waitForTick();
  Assert.ok(showStub.notCalled, "Moving the button shows no hint");
  Assert.ok(
    !Services.prefs.getBoolPref(REMOVED_HINT_SHOWN),
    "Moving the button doesn't set the shown pref"
  );

  await cleanupRemovedHintTest(showStub);
});
