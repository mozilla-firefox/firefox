/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

requestLongerTimeout(2);

// Restoring default should reset density and automatic touch mode and show an
// "undo" option which undoes the restoring operation.
add_task(async function () {
  let stopReloadButtonId = "stop-reload-button";
  CustomizableUI.removeWidgetFromArea(stopReloadButtonId);
  await startCustomizing();
  ok(!CustomizableUI.inDefaultState, "Not in default state to begin with");
  is(
    CustomizableUI.getPlacementOfWidget(stopReloadButtonId),
    null,
    "Stop/reload button is in palette"
  );
  let undoResetButton = document.getElementById(
    "customization-undo-reset-button"
  );
  is(undoResetButton.hidden, true, "The undo button is hidden before reset");

  Services.prefs.setIntPref("browser.uidensity", gUIDensity.MODE_COMPACT);
  Services.prefs.setBoolPref("browser.touchmode.auto", true);

  await gCustomizeMode.reset();

  ok(CustomizableUI.inDefaultState, "In default state after reset");
  is(undoResetButton.hidden, false, "The undo button is visible after reset");
  ok(
    !Services.prefs.prefHasUserValue("browser.uidensity"),
    "Density has been restored to the default."
  );
  ok(
    !Services.prefs.prefHasUserValue("browser.touchmode.auto"),
    "Automatic touch mode has been restored to the default."
  );

  await gCustomizeMode.undoReset();

  is(
    Services.prefs.getIntPref("browser.uidensity"),
    gUIDensity.MODE_COMPACT,
    "Density has been reset to compact."
  );
  ok(
    Services.prefs.getBoolPref("browser.touchmode.auto"),
    "Automatic touch mode has been restored to on."
  );

  ok(!CustomizableUI.inDefaultState, "Not in default state after undo-reset");
  is(
    undoResetButton.hidden,
    true,
    "The undo button is hidden after clicking on the undo button"
  );
  is(
    CustomizableUI.getPlacementOfWidget(stopReloadButtonId),
    null,
    "Stop/reload button is in palette"
  );

  await gCustomizeMode.reset();
});

// Performing an action after a reset will hide the undo button.
add_task(async function action_after_reset_hides_undo() {
  let stopReloadButtonId = "stop-reload-button";
  CustomizableUI.removeWidgetFromArea(stopReloadButtonId);
  ok(!CustomizableUI.inDefaultState, "Not in default state to begin with");
  is(
    CustomizableUI.getPlacementOfWidget(stopReloadButtonId),
    null,
    "Stop/reload button is in palette"
  );
  let undoResetButton = document.getElementById(
    "customization-undo-reset-button"
  );
  is(undoResetButton.hidden, true, "The undo button is hidden before reset");

  await gCustomizeMode.reset();

  ok(CustomizableUI.inDefaultState, "In default state after reset");
  is(undoResetButton.hidden, false, "The undo button is visible after reset");

  CustomizableUI.addWidgetToArea(
    stopReloadButtonId,
    CustomizableUI.AREA_FIXED_OVERFLOW_PANEL
  );
  is(
    undoResetButton.hidden,
    true,
    "The undo button is hidden after another change"
  );
});

// "Restore defaults", exiting customize, and re-entering shouldn't show the Undo button
add_task(async function () {
  let undoResetButton = document.getElementById(
    "customization-undo-reset-button"
  );
  is(undoResetButton.hidden, true, "The undo button is hidden before a reset");
  ok(
    !CustomizableUI.inDefaultState,
    "The browser should not be in default state"
  );
  await gCustomizeMode.reset();

  is(undoResetButton.hidden, false, "The undo button is visible after a reset");
  await endCustomizing();
  await startCustomizing();
  is(
    undoResetButton.hidden,
    true,
    "The undo reset button should be hidden after entering customization mode"
  );
});

// Bug 971626 - Restore Defaults should collapse the Title Bar
add_task(async function () {
  {
    const supported = CustomTitlebar.systemSupported;
    is(typeof supported, "boolean");
    info("CustomTitlebar support: " + supported);
    if (!supported) {
      return;
    }
  }

  const kDefaultValue = Services.appinfo.drawInTitlebar;
  let restoreDefaultsButton = document.getElementById(
    "customization-reset-button"
  );
  let titlebarCheckbox = document.getElementById(
    "customization-titlebar-visibility-checkbox"
  );
  let undoResetButton = document.getElementById(
    "customization-undo-reset-button"
  );
  ok(
    CustomizableUI.inDefaultState,
    "Should be in default state at start of test"
  );
  ok(
    restoreDefaultsButton.disabled,
    "Restore defaults button should be disabled when in default state"
  );
  is(
    titlebarCheckbox.hasAttribute("checked"),
    !kDefaultValue,
    "Title bar checkbox should reflect pref value"
  );
  is(
    undoResetButton.hidden,
    true,
    "Undo reset button should be hidden at start of test"
  );

  let prefName = "browser.tabs.inTitlebar";
  Services.prefs.setIntPref(prefName, !kDefaultValue);
  ok(
    !restoreDefaultsButton.disabled,
    "Restore defaults button should be enabled when pref changed"
  );
  is(
    Services.appinfo.drawInTitlebar,
    !kDefaultValue,
    "Title bar checkbox should reflect changed pref value"
  );
  is(
    titlebarCheckbox.hasAttribute("checked"),
    kDefaultValue,
    "Title bar checkbox should reflect changed pref value"
  );
  ok(
    !CustomizableUI.inDefaultState,
    "With titlebar flipped, no longer default"
  );
  is(
    undoResetButton.hidden,
    true,
    "Undo reset button should be hidden after pref change"
  );

  await gCustomizeMode.reset();
  ok(
    restoreDefaultsButton.disabled,
    "Restore defaults button should be disabled after reset"
  );
  is(
    titlebarCheckbox.hasAttribute("checked"),
    !kDefaultValue,
    "Title bar checkbox should reflect default value after reset"
  );
  is(
    Services.prefs.getIntPref(prefName),
    2,
    "Reset should reset drawInTitlebar"
  );
  is(
    Services.appinfo.drawInTitlebar,
    kDefaultValue,
    "Default state should be restored"
  );
  ok(CustomizableUI.inDefaultState, "In default state after titlebar reset");
  is(
    undoResetButton.hidden,
    false,
    "Undo reset button should be visible after reset"
  );
  ok(
    !undoResetButton.disabled,
    "Undo reset button should be enabled after reset"
  );

  await gCustomizeMode.undoReset();
  ok(
    !restoreDefaultsButton.disabled,
    "Restore defaults button should be enabled after undo-reset"
  );
  is(
    titlebarCheckbox.hasAttribute("checked"),
    kDefaultValue,
    "Title bar checkbox should reflect undo-reset value"
  );
  ok(!CustomizableUI.inDefaultState, "No longer in default state after undo");
  is(
    Services.prefs.getIntPref(prefName),
    kDefaultValue ? 0 : 1,
    "Undo-reset goes back to previous pref value"
  );
  is(
    undoResetButton.hidden,
    true,
    "Undo reset button should be hidden after undo-reset clicked"
  );

  Services.prefs.clearUserPref(prefName);
  ok(CustomizableUI.inDefaultState, "In default state after pref cleared");
  is(
    undoResetButton.hidden,
    true,
    "Undo reset button should be hidden at end of test"
  );
});

add_task(async function asyncCleanup() {
  await gCustomizeMode.reset();
  await endCustomizing();
});
