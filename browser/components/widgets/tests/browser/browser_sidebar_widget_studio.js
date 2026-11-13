/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);

const ENABLED_PREF = "browser.widgets.generated.enabled";

add_setup(mockAuthentication);

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
  let panel = await openWidgetsSidebar();
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

add_task(async function test_composer_needs_authentication() {
  setSignedIn(false);
  let panel = await openWidgetsSidebar();
  await panel.updateComplete;
  ok(!panel.composer, "Composer hidden");

  setSignedIn(true);
  await panel.updateComplete;
  ok(panel.composer, "Composer shown");

  setSignedIn(false);
  await panel.updateComplete;
  ok(!panel.composer, "Composer hidden again");

  panel.composing = false;
  setSignedIn(true);
  await panel.updateComplete;
  ok(panel.composeButton, "Compose button shown");

  setSignedIn(false);
  await panel.updateComplete;
  ok(!panel.composeButton, "Compose button hidden");
  SidebarController.hide();
});

add_task(async function test_panel_composer() {
  setSignedIn(true);
  let panel = await openWidgetsSidebar();
  await panel.updateComplete;
  let { composer } = panel;
  ok(composer, "Composer shown");
  await TestUtils.waitForCondition(
    () => composer.shadowRoot.activeElement?.localName == "textarea",
    "Composer focused"
  );

  EventUtils.synthesizeKey("KEY_Escape", {}, panel.ownerGlobal);
  await panel.updateComplete;
  ok(!panel.composer, "Escape hid composer");
  await TestUtils.waitForCondition(
    () => panel.shadowRoot.activeElement == panel.composeButton,
    "Compose button focused"
  );

  panel.closeButton.focus();
  panel.handleSidebarFocusedEvent();
  is(
    panel.shadowRoot.activeElement,
    panel.composeButton,
    "Sidebar focus moved to compose button"
  );

  panel.composeButton.click();
  await panel.updateComplete;
  ({ composer } = panel);
  ok(composer, "Compose button showed composer");
  await TestUtils.waitForCondition(
    () => composer.shadowRoot.activeElement?.localName == "textarea",
    "Composer focused again"
  );

  EventUtils.sendString("weather", panel.ownerGlobal);
  EventUtils.synthesizeKey("KEY_Enter", {}, panel.ownerGlobal);
  await panel.updateComplete;
  ok(!panel.composer, "Submit hid composer");
  await TestUtils.waitForCondition(
    () => panel.shadowRoot.activeElement == panel.composeButton,
    "Compose button focused after submit"
  );
  ok(SidebarController.isOpen, "Panel open");
  SidebarController.hide();
});
