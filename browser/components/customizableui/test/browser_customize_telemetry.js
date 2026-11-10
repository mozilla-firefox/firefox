/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests the Glean telemetry recorded for Customize Toolbar mode actions.
 */

/**
 * Returns a Promise that resolves once customize mode has been successfully
 * entered. Best to set this up before doing something that initiates the
 * transition.
 *
 * @returns {Promise<void>}
 */
async function waitForCustomizationReady() {
  await BrowserTestUtils.waitForEvent(gNavToolbox, "customizationready");
  if (document.hasPendingL10nMutations) {
    await BrowserTestUtils.waitForEvent(document, "L10nMutationsFinished");
  }
}

/**
 * Tests that the opened event was recorded exactly once, and that the
 * extra_keys on the event match expectations.
 *
 * @param {string} entryPoint
 *   The expected entry_point extra_keys value.
 * @param {string} tabOrientation
 *   The expected tab_orientation extra_keys value.
 */
function assertOpenedEvent(entryPoint, tabOrientation) {
  let events = Glean.customize.opened.testGetValue();
  Assert.equal(events?.length, 1, "Recorded exactly one customize.opened");
  Assert.deepEqual(
    events[0].extra,
    { entry_point: entryPoint, tab_orientation: tabOrientation },
    "customize.opened has the expected extras"
  );
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.verticalTabs", false]],
  });
  registerCleanupFunction(async () => {
    await endCustomizing();
    await resetCustomization();
    // And clear any prefs that were set as a side-effect of manipulating the
    // sidebar.
    Services.prefs.clearUserPref(
      "browser.toolbarbuttons.introduced.sidebar-button"
    );
    Services.prefs.clearUserPref("sidebar.newTool.migration.passwords");
  });
});

add_task(async function test_opened_from_toolbar_context_menu() {
  Services.fog.testResetFOG();

  let contextMenu = document.getElementById("toolbar-context-menu");
  let shown = popupShown(contextMenu);
  EventUtils.synthesizeMouseAtCenter(
    document.getElementById("stop-reload-button"),
    { type: "contextmenu", button: 2 }
  );
  await shown;

  let ready = waitForCustomizationReady();
  contextMenu.activateItem(
    document.getElementById("toolbar-context-customize")
  );
  await ready;

  assertOpenedEvent("toolbar_context_menu", "horizontal");
  await endCustomizing();
});

add_task(async function test_opened_from_app_menu() {
  Services.fog.testResetFOG();

  await gCUITestUtils.openMainMenu();
  let moreToolsView = PanelMultiView.getViewNode(document, "appmenu-moreTools");
  let viewShown = BrowserTestUtils.waitForEvent(moreToolsView, "ViewShown");
  document.getElementById("appMenu-more-button2").click();
  await viewShown;

  let ready = waitForCustomizationReady();
  document.getElementById("appmenu-moreTools-button").click();
  await ready;

  assertOpenedEvent("app_menu", "horizontal");
  await endCustomizing();
});

add_task(async function test_opened_from_other_command_entry_points() {
  for (let [id, entryPoint] of [
    ["overflowMenu-customize-button", "overflow_menu"],
    ["menu_customizeToolbars", "menu_bar"],
  ]) {
    Services.fog.testResetFOG();
    let ready = waitForCustomizationReady();
    document.getElementById(id).doCommand();
    await ready;
    assertOpenedEvent(entryPoint, "horizontal");
    await endCustomizing();
  }
});

add_task(async function test_opened_vertical_tabs() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.revamp", true],
      ["sidebar.verticalTabs", true],
    ],
  });
  Services.fog.testResetFOG();

  let ready = waitForCustomizationReady();
  document.getElementById("menu_customizeToolbars").doCommand();
  await ready;
  assertOpenedEvent("menu_bar", "vertical");

  await endCustomizing();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_other_event_without_entry_point() {
  Services.fog.testResetFOG();
  await startCustomizing();
  assertOpenedEvent("other", "horizontal");
  await endCustomizing();
});

add_task(async function test_restore_defaults_and_undo() {
  CustomizableUI.removeWidgetFromArea("stop-reload-button");
  await startCustomizing();
  Services.fog.testResetFOG();

  let resetButton = document.getElementById("customization-reset-button");
  ok(!resetButton.disabled, "Reset button is enabled");
  resetButton.doCommand();
  await TestUtils.waitForCondition(() => !gCustomizeMode.resetting);
  ok(CustomizableUI.inDefaultState, "In default state after reset");

  Assert.equal(
    Glean.customize.restoreDefaults.testGetValue()?.length,
    1,
    "Recorded customize.restore_defaults"
  );
  Assert.equal(
    Glean.customize.undoRestore.testGetValue(),
    null,
    "No customize.undo_restore yet"
  );

  let undoButton = document.getElementById("customization-undo-reset-button");
  ok(!undoButton.hidden, "Undo button is visible");
  undoButton.doCommand();
  await TestUtils.waitForCondition(() => !gCustomizeMode.resetting);
  ok(!CustomizableUI.inDefaultState, "Not in default state after undo");

  Assert.equal(
    Glean.customize.undoRestore.testGetValue()?.length,
    1,
    "Recorded customize.undo_restore"
  );
  Assert.equal(
    Glean.customize.restoreDefaults.testGetValue()?.length,
    1,
    "Still only one customize.restore_defaults"
  );

  await endCustomizing();
  await resetCustomization();
});
