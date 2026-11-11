/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);

const ENABLED_PREF = "browser.widgets.generated.enabled";

function getToolButton(win = window) {
  return [...win.document.querySelector("sidebar-main").toolButtons].find(
    button => button.getAttribute("view") == WIDGETS_VIEW
  );
}

add_task(async function test_tool_follows_pref() {
  SidebarTestUtils.restoreStateAtCleanup(window);
  await SidebarTestUtils.ensureLauncherVisible(window);
  await TestUtils.waitForCondition(() => getToolButton(), "Tool shown");
  let menuItem = document.getElementById("menu_widgetsSidebar");
  ok(!menuItem.hidden, "Menu item shown");

  await SpecialPowers.pushPrefEnv({
    set: [[ENABLED_PREF, false]],
  });
  await TestUtils.waitForCondition(() => !getToolButton(), "Tool hidden");
  ok(menuItem.hidden, "Menu item hidden");

  await SpecialPowers.popPrefEnv();
  await TestUtils.waitForCondition(() => getToolButton(), "Tool shown again");
  ok(!menuItem.hidden, "Menu item shown again");
});

add_task(async function test_nimbus_enrolment_toggles_tool() {
  await SidebarTestUtils.ensureLauncherVisible(window);

  await SpecialPowers.pushPrefEnv({
    clear: [[ENABLED_PREF]],
  });
  await TestUtils.waitForCondition(
    () => !getToolButton(),
    "Tool hidden before enrolling"
  );

  let cleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "generatedWidgets",
    value: { enabled: true },
  });
  ok(Services.prefs.getBoolPref(ENABLED_PREF), "Nimbus enables the tool");
  ok(
    Services.prefs.prefHasUserValue(ENABLED_PREF),
    "Nimbus sets the user pref"
  );
  await TestUtils.waitForCondition(
    () => getToolButton(),
    "Tool shown after enrolling"
  );
  await cleanup();
  ok(
    !Services.prefs.prefHasUserValue(ENABLED_PREF),
    "Nimbus clears the user pref"
  );
  ok(!Services.prefs.getBoolPref(ENABLED_PREF), "Tool pref restored to false");
  await TestUtils.waitForCondition(
    () => !getToolButton(),
    "Tool hidden after unenrolling"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_close_button_closes_panel() {
  const panel = await openWidgetsSidebar();
  ok(
    BrowserTestUtils.isHidden(document.getElementById("sidebar-panel-header")),
    "Header hidden"
  );

  await panel.updateComplete;
  panel.closeButton.click();
  await TestUtils.waitForCondition(
    () => document.getElementById("sidebar-box").hidden,
    "Panel closed"
  );
});
