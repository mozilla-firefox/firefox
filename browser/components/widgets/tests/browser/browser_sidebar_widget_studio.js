/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

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
    set: [["browser.widgets.generated.enabled", false]],
  });
  await TestUtils.waitForCondition(() => !getToolButton(), "Tool hidden");
  ok(menuItem.hidden, "Menu item hidden");

  await SpecialPowers.popPrefEnv();
  await TestUtils.waitForCondition(() => getToolButton(), "Tool shown again");
  ok(!menuItem.hidden, "Menu item shown again");
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
