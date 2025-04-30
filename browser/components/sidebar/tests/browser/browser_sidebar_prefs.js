/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_tools_prefs() {
  const win = await BrowserTestUtils.openNewBrowserWindow();
  const { document } = win;
  const sidebar = document.querySelector("sidebar-main");
  await sidebar.updateComplete;

  Services.prefs.setStringPref(
    "sidebar.main.tools",
    "aichat,syncedtabs,history,bookmarks"
  );

  // Open customize sidebar
  await toggleSidebarPanel(win, "viewCustomizeSidebar");

  // Set tools
  let customizeDocument = win.SidebarController.browser.contentDocument;
  let customizeComponent = customizeDocument.querySelector("sidebar-customize");
  let toolEntrypointsCount = sidebar.toolButtons.length;
  let checkedInputs = Array.from(customizeComponent.toolInputs).filter(
    input => input.checked
  );
  is(
    checkedInputs.length,
    toolEntrypointsCount,
    `${toolEntrypointsCount} inputs to toggle Firefox Tools are shown in the Customize Menu.`
  );
  let bookmarksInput = Array.from(customizeComponent.toolInputs).find(
    input => input.name === "viewBookmarksSidebar"
  );
  ok(
    bookmarksInput.checked,
    "The bookmarks input is checked initially as Bookmarks is a default tool."
  );
  for (const toolInput of customizeComponent.toolInputs) {
    let toolDisabledInitialState = !toolInput.checked;
    if (toolInput.name == "viewBookmarksSidebar") {
      continue;
    }
    toolInput.click();
    await BrowserTestUtils.waitForCondition(
      () => {
        let toggledTool = win.SidebarController.toolsAndExtensions.get(
          toolInput.name
        );
        return toggledTool.disabled === !toolDisabledInitialState;
      },
      `The entrypoint for ${toolInput.name} has been ${toolDisabledInitialState ? "enabled" : "disabled"} in the sidebar.`
    );
    toolEntrypointsCount = sidebar.toolButtons.length;
    checkedInputs = Array.from(customizeComponent.toolInputs).filter(
      input => input.checked
    );
    is(
      toolEntrypointsCount,
      checkedInputs.length,
      `The button for the ${toolInput.name} entrypoint has been ${
        toolDisabledInitialState ? "added" : "removed"
      }.`
    );
  }

  const updatedTools = Services.prefs.getStringPref("sidebar.main.tools");
  is(
    updatedTools,
    "bookmarks",
    "All tools have been removed from the launcher except bookmarks"
  );

  await BrowserTestUtils.closeWindow(win);

  //   Open a new window to check that it uses the pref
  const newWin = await BrowserTestUtils.openNewBrowserWindow();
  const newSidebar = newWin.document.querySelector("sidebar-main");
  ok(newSidebar, "New Window sidebar is shown.");
  await newSidebar.updateComplete;
  info("Waiting for customize button to be present");
  await BrowserTestUtils.waitForMutationCondition(
    newSidebar,
    { childList: true, subTree: true },
    () => !!newSidebar.customizeButton
  );
  ok(
    BrowserTestUtils.isVisible(newSidebar.customizeButton),
    "The sidebar-main component has fully rendered, and the customize button is present."
  );

  // TO DO: opening the customize category can be removed once bug 1898613 is resolved.
  // Open customize sidebar
  await toggleSidebarPanel(newWin, "viewCustomizeSidebar");

  let newCustomizeDocument = newWin.SidebarController.browser.contentDocument;
  let newCustomizeComponent =
    newCustomizeDocument.querySelector("sidebar-customize");
  let newToolEntrypointsCount = newSidebar.toolButtons.length;
  let newCheckedInputs = Array.from(newCustomizeComponent.toolInputs).filter(
    input => input.checked
  );
  is(
    newCheckedInputs.length,
    newToolEntrypointsCount,
    `${newToolEntrypointsCount} Firefox Tool button is shown in the sidebar matching ${newCheckedInputs.length} shown in the Customize Menu.`
  );
  is(
    newCheckedInputs.length,
    checkedInputs.length,
    "The number of tool inputs checked matches that of the other window's sidebar"
  );
  let newBookmarksInput = Array.from(newCustomizeComponent.toolInputs).find(
    input => input.name === "viewBookmarksSidebar"
  );
  is(
    newBookmarksInput.checked,
    bookmarksInput.checked,
    "The bookmarks input is checked, as suggested by the pref."
  );

  is(
    Services.prefs.getStringPref("sidebar.main.tools"),
    updatedTools,
    "Previously set tools should still be the same"
  );

  await BrowserTestUtils.closeWindow(newWin);
});

/**
 * Check that tools pref changes happen for existing windows
 */
add_task(async function test_tool_pref_change() {
  const sidebar = document.querySelector("sidebar-main");
  await sidebar.updateComplete;

  const origCount = sidebar.toolButtons.length;
  is(origCount, 1, "Expected number of initial tools");

  const origTools = Services.prefs.getStringPref("sidebar.main.tools");
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.main.tools", origTools.replace(/,?bookmarks/, "")]],
  });
  is(sidebar.toolButtons.length, origCount - 1, "Removed tool");

  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.main.tools", origTools]],
  });
  is(sidebar.toolButtons.length, origCount, "Restored tool");

  await SpecialPowers.pushPrefEnv({ clear: [["sidebar.main.tools"]] });
  is(sidebar.toolButtons.length, 0, "Cleared default tools");
});

/**
 * Check that the new sidebar is hidden/shown automatically (without a browser restart)
 * when flipping the sidebar.revamp pref
 */
add_task(async function test_flip_revamp_pref() {
  const win = await BrowserTestUtils.openNewBrowserWindow();
  await waitForTabstripOrientation("horizontal", win);
  const { sidebarMain, sidebarContainer } = win.SidebarController;

  let verticalTabs = win.document.querySelector("#vertical-tabs");
  ok(
    !BrowserTestUtils.isVisible(verticalTabs),
    "Vertical tabs slot is not visible initially"
  );
  // Open history sidebar
  await toggleSidebarPanel(win, "viewHistorySidebar");

  await SpecialPowers.pushPrefEnv({ set: [[VERTICAL_TABS_PREF, true]] });
  await waitForTabstripOrientation("vertical", win);
  ok(BrowserTestUtils.isVisible(verticalTabs), "Vertical tabs slot is visible");
  ok(
    BrowserTestUtils.isVisible(sidebarMain),
    "Revamped sidebar main is shown initially."
  );
  ok(
    BrowserTestUtils.isVisible(sidebarContainer),
    "Revamped sidebar container is shown initially."
  );
  Assert.equal(
    Services.prefs.getStringPref(SIDEBAR_VISIBILITY_PREF),
    "always-show",
    "Sanity check the visibilty pref when verticalTabs are enabled"
  );

  await SpecialPowers.pushPrefEnv({ set: [["sidebar.revamp", false]] });
  await waitForTabstripOrientation("horizontal", win);

  info("Waiting for sidebar container to be visible");
  await BrowserTestUtils.waitForMutationCondition(
    sidebarContainer,
    { subTree: true, attributes: true, attributeFilter: ["hidden"] },
    () =>
      sidebarContainer.hidden &&
      !BrowserTestUtils.isVisible(verticalTabs) &&
      win.document.getElementById("sidebar-header")
  );
  const sidebarHeader = win.document.getElementById("sidebar-header");
  ok(sidebarHeader, "Sidebar header is shown");
  info("Waiting for sidebar header to be visible");
  await BrowserTestUtils.waitForMutationCondition(
    sidebarHeader,
    { attributes: true, attributeFilter: ["hidden"] },
    () => !sidebarHeader.hidden
  );
  ok(true, "The new sidebar is hidden and the old sidebar is shown.");

  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.revamp", true]],
  });
  await sidebarMain.updateComplete;
  info("Waiting for sidebar container to be visible");
  await BrowserTestUtils.waitForMutationCondition(
    sidebarContainer,
    { attributes: true, attributeFilter: ["hidden"] },
    () => !sidebarContainer.hidden
  );
  info("Waiting for sidebar header to be hidden");
  await BrowserTestUtils.waitForMutationCondition(
    sidebarHeader,
    { attributes: true, attributeFilter: ["hidden"] },
    () => sidebarHeader.hidden
  );
  ok(true, "The old sidebar is hidden and the new sidebar is shown.");

  await BrowserTestUtils.closeWindow(win);
});

/**
 * Check that panels can stay open when flipping sidebar.revamp
 */
add_task(async function test_flip_revamp_pref_with_panel() {
  await toggleSidebarPanel(window, "viewGenaiChatSidebar");
  ok(SidebarController.isOpen, "panel open with revamp");

  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.revamp", false]],
  });

  ok(SidebarController.isOpen, "panel still open after old");

  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.revamp", true]],
  });

  ok(SidebarController.isOpen, "panel still open after new");

  await SidebarController.hide();
});

add_task(async function test_opening_panel_flips_has_used_pref() {
  Services.prefs.clearUserPref("sidebar.old-sidebar.has-used");
  Services.prefs.clearUserPref("sidebar.new-sidebar.has-used");

  info("Open a panel from the legacy sidebar.");
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.revamp", false]],
  });
  await SidebarController.show("viewHistorySidebar");
  ok(
    Services.prefs.getBoolPref("sidebar.old-sidebar.has-used"),
    "has-used pref enabled for legacy sidebar."
  );
  SidebarController.hide();
  await SpecialPowers.popPrefEnv();

  info("Open a panel from the revamped sidebar.");
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.revamp", true]],
  });
  await SidebarController.show("viewHistorySidebar");
  ok(
    Services.prefs.getBoolPref("sidebar.new-sidebar.has-used"),
    "has-used pref enabled for revamped sidebar."
  );
  SidebarController.hide();
  await SpecialPowers.popPrefEnv();

  info("Sanity check: Both prefs should stay enabled.");
  ok(
    Services.prefs.getBoolPref("sidebar.old-sidebar.has-used"),
    "has-used pref enabled for legacy sidebar."
  );
  ok(
    Services.prefs.getBoolPref("sidebar.new-sidebar.has-used"),
    "has-used pref enabled for revamped sidebar."
  );
});
