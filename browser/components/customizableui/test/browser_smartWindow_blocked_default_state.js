/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const SMART_WINDOW_WIDGETS = [
  "smartwindow-group-tabs-button",
  "ai-window-toggle",
];

add_task(async function test_default_state_while_blocked() {
  for (let id of SMART_WINDOW_WIDGETS) {
    ok(!document.getElementById(id), `${id} shouldn't be in the window`);
  }
  ok(CustomizableUI.inDefaultState, "Should be in default state");

  await startCustomizing();
  ok(
    document.getElementById("customization-reset-button").disabled,
    "Restore Defaults should be disabled"
  );
  await endCustomizing();
});

add_task(async function test_unblocking_restores_default_slots() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ai.control.smartWindow", "available"],
      ["browser.smartwindow.autoTabGrouping.enabled", true],
    ],
  });

  let defaults = CustomizableUI.getDefaultPlacementsForArea(
    CustomizableUI.AREA_TABSTRIP
  );
  for (let id of SMART_WINDOW_WIDGETS) {
    ok(document.getElementById(id), `${id} should be in the window`);
    Assert.deepEqual(
      CustomizableUI.getPlacementOfWidget(id),
      { area: CustomizableUI.AREA_TABSTRIP, position: defaults.indexOf(id) },
      `${id} should be in its default slot`
    );
  }
  ok(CustomizableUI.inDefaultState, "Should still be in default state");

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_reset_after_blocking_mid_session() {
  for (let id of SMART_WINDOW_WIDGETS) {
    ok(!document.getElementById(id), `${id} should be destroyed`);
  }
  await startCustomizing();
  await gCustomizeMode.reset();
  ok(CustomizableUI.inDefaultState, "Should be in default state after reset");
  ok(
    document.getElementById("customization-reset-button").disabled,
    "Restore Defaults should be disabled after reset"
  );
  await endCustomizing();
});
