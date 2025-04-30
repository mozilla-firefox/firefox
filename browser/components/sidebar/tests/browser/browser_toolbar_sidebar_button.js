/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const CustomizableUIInternal = CustomizableUI.getTestOnlyInternalProp(
  "CustomizableUIInternal"
);
let gAreas = CustomizableUI.getTestOnlyInternalProp("gAreas");

const SIDEBAR_BUTTON_INTRODUCED_PREF =
  "browser.toolbarbuttons.introduced.sidebar-button";

add_setup(async () => {
  // Only vertical tabs mode has expanded state
  await SpecialPowers.pushPrefEnv({
    set: [
      [VERTICAL_TABS_PREF, true],
      [SIDEBAR_BUTTON_INTRODUCED_PREF, false],
    ],
  });
  await waitForTabstripOrientation("vertical");
  Assert.equal(
    Services.prefs.getStringPref(SIDEBAR_VISIBILITY_PREF),
    "always-show",
    "Sanity check the visibility pref when verticalTabs are enabled"
  );

  let navbarDefaults = gAreas.get("nav-bar").get("defaultPlacements");
  let hadSavedState = !!CustomizableUI.getTestOnlyInternalProp("gSavedState");
  if (!hadSavedState) {
    CustomizableUI.setTestOnlyInternalProp("gSavedState", {
      currentVersion: CustomizableUI.getTestOnlyInternalProp("kVersion"),
      placements: {
        "nav-bar": Array.from(navbarDefaults),
      },
    });
  }

  // Normally, if the `sidebar.revamp` pref is set at startup,
  // we'll have it in the default placements for the navbar.
  // Simulate this from the test:
  let backupDefaults = Array.from(navbarDefaults);
  registerCleanupFunction(() => {
    gAreas.get("nav-bar").set("defaultPlacements", backupDefaults);
    CustomizableUI.reset();
  });
  navbarDefaults.splice(
    navbarDefaults.indexOf("stop-reload-button") + 1,
    0,
    "sidebar-button"
  );
  ok(
    SidebarController.sidebarMain?.expanded,
    "With verticalTabs enabled, the launcher should be initially expanded"
  );
  ok(
    BrowserTestUtils.isVisible(SidebarController.sidebarMain),
    "Sidebar launcher is visible at setup"
  );
});

add_task(async function test_toolbar_sidebar_button() {
  ok(
    document.getElementById("sidebar-button"),
    "Sidebar button is showing in the toolbar initially."
  );
  let gFuturePlacements =
    CustomizableUI.getTestOnlyInternalProp("gFuturePlacements");
  is(
    gFuturePlacements.size,
    0,
    "All future placements should be dealt with by now."
  );

  // Check it's actually present if we open a new window.
  const win = await BrowserTestUtils.openNewBrowserWindow();
  const { document: newDoc } = win;
  ok(
    newDoc.getElementById("sidebar-button"),
    "Should have button in new window"
  );
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_expanded_state_for_always_show() {
  await waitForTabstripOrientation("vertical");
  const { sidebarMain, toolbarButton } = SidebarController;

  const checkExpandedState = async (
    expanded,
    component = sidebarMain,
    button = toolbarButton
  ) => {
    info(
      `Waiting for component to become ${expanded ? "expanded" : "collapsed"}`
    );
    await BrowserTestUtils.waitForMutationCondition(
      component,
      { attributes: true, attributeFilter: ["expanded"] },
      () => Boolean(component.expanded) == expanded
    );
    ok(true, expanded ? "Sidebar is expanded." : "Sidebar is collapsed.");
    info(
      `Waiting for button to become ${expanded ? "highlighted" : "not highlighted"}`
    );
    await BrowserTestUtils.waitForMutationCondition(
      button,
      { attributes: true, attributeFilter: ["checked", "expanded"] },
      () =>
        Boolean(button.checked) == expanded &&
        button.hasAttribute("expanded") == expanded
    );
    ok(
      true,
      expanded
        ? "Toolbar button is highlighted and expanded attribute is present.."
        : "Toolbar button is not highlighted and expanded attribute is absent."
    );
    Assert.deepEqual(
      document.l10n.getAttributes(button),
      {
        id: expanded
          ? "sidebar-widget-collapse-sidebar2"
          : "sidebar-widget-expand-sidebar2",
        args:
          AppConstants.platform === "macosx"
            ? { shortcut: "⌃Z" }
            : { shortcut: "Alt+Ctrl+Z" },
      },
      "Toolbar button has the correct tooltip."
    );
  };

  await checkExpandedState(true);

  info("Toggle expanded state via toolbar button.");
  EventUtils.synthesizeMouseAtCenter(toolbarButton, {}, window);
  await SidebarController.waitUntilStable();
  await checkExpandedState(false);

  info("Don't collapse the sidebar by loading a tool.");
  await SidebarController.initializeUIState({
    launcherExpanded: true,
    command: "",
  });
  info("Waiting to re-initialize UI state to make the launcher expanded");
  await BrowserTestUtils.waitForMutationCondition(
    sidebarMain,
    { attributes: true, attributeFilter: ["expanded"] },
    () => Boolean(sidebarMain.expanded)
  );

  const toolButton = sidebarMain.toolButtons[0];
  EventUtils.synthesizeMouseAtCenter(toolButton, {}, window);
  await checkExpandedState(true);
  SidebarController.hide();

  info("Load and unload a tool with the sidebar collapsed to begin with.");
  await SidebarController.initializeUIState({
    launcherExpanded: false,
    command: "",
  });
  info("Waiting to re-initialize UI state to make the launcher collapsed");
  await BrowserTestUtils.waitForMutationCondition(
    sidebarMain,
    { attributes: true, attributeFilter: ["expanded"] },
    () => !sidebarMain.expanded
  );

  EventUtils.synthesizeMouseAtCenter(toolButton, {}, window);
  await checkExpandedState(false);
  SidebarController.hide();

  await SidebarController.initializeUIState({
    launcherExpanded: true,
    command: "",
  });
  info("Waiting to re-initialize UI state to make the launcher expanded");
  await BrowserTestUtils.waitForMutationCondition(
    sidebarMain,
    { attributes: true, attributeFilter: ["expanded"] },
    () => Boolean(sidebarMain.expanded)
  );

  info("Check expanded state on a new window.");
  const newWin = await BrowserTestUtils.openNewBrowserWindow();
  await waitForTabstripOrientation("vertical", newWin);
  await checkExpandedState(
    true,
    newWin.SidebarController.sidebarMain,
    newWin.SidebarController.toolbarButton
  );

  await BrowserTestUtils.closeWindow(newWin);
});

add_task(async function test_states_for_hide_sidebar() {
  // With horizontal tabs and visibility set to "hide-sidebar", check launcher is initially visible
  await SpecialPowers.pushPrefEnv({
    set: [[VERTICAL_TABS_PREF, false]],
  });
  await waitForTabstripOrientation("horizontal");

  const { sidebarContainer, sidebarMain, toolbarButton } = SidebarController;

  Assert.equal(
    Services.prefs.getStringPref(SIDEBAR_VISIBILITY_PREF),
    "hide-sidebar",
    "Sanity check the visibility pref when verticalTabs are disabled"
  );
  // The sidebar launcher should be initially visible when visibility is "hide-sidebar"
  Assert.ok(
    !SidebarController.sidebarContainer.hidden,
    "The launcher is initially visible"
  );
  Assert.ok(
    SidebarController.toolbarButton.checked,
    "The toolbar button is initially checked"
  );

  const checkStates = async (
    { hidden },
    container = sidebarContainer,
    component = sidebarMain,
    button = toolbarButton
  ) => {
    info(`Waiting for container to become ${hidden ? "hidden" : "not hidden"}`);
    await BrowserTestUtils.waitForMutationCondition(
      container,
      { attributes: true, attributeFilter: ["hidden"] },
      () => container.hidden == hidden
    );
    ok(
      true,
      hidden ? "Sidebar container is hidden." : "Sidebar container is shown."
    );
    info("Waiting for component to be not expanded");
    await BrowserTestUtils.waitForMutationCondition(
      component,
      { attributes: true, attributeFilter: ["expanded"] },
      () => !component.expanded
    );
    ok(true, "Sidebar should not be expanded");
    info(
      `Waiting for button to be ${hidden ? "not highlighted" : "highlighted"}`
    );
    await BrowserTestUtils.waitForMutationCondition(
      button,
      { attributes: true, attributeFilter: ["checked", "expanded"] },
      () => button.checked == !hidden && !button.hasAttribute("expanded")
    );
    ok(
      true,
      "Toolbar button checked state is correct and expanded attribute is absent."
    );
    Assert.deepEqual(
      document.l10n.getAttributes(button),
      {
        id: hidden
          ? "sidebar-widget-show-sidebar2"
          : "sidebar-widget-hide-sidebar2",
        args:
          AppConstants.platform === "macosx"
            ? { shortcut: "⌃Z" }
            : { shortcut: "Alt+Ctrl+Z" },
      },
      "Toolbar button has the correct tooltip."
    );
  };

  info("Check the launcher is initially visible");
  await checkStates({ hidden: false });

  info("Hide sidebar using the toolbar button.");
  await SimpleTest.promiseFocus(window);
  EventUtils.synthesizeMouseAtCenter(toolbarButton, {}, window);
  await checkStates({ hidden: true });
  Assert.ok(
    !toolbarButton.checked,
    "The toolbar button becomes unchecked when clicking it hides the launcher"
  );

  info("Check states on a new window.");
  const newWin = await BrowserTestUtils.openNewBrowserWindow();
  await waitForTabstripOrientation("horizontal", newWin);
  await checkStates(
    { hidden: true },
    newWin.SidebarController.sidebarContainer,
    newWin.SidebarController.sidebarMain,
    newWin.SidebarController.toolbarButton
  );
  Assert.ok(
    !newWin.SidebarController.toolbarButton.checked,
    "The toolbar button in the new window is unchecked when the launcher is hidden"
  );

  await BrowserTestUtils.closeWindow(newWin);
  await SpecialPowers.popPrefEnv();
  await waitForTabstripOrientation("vertical");
});

add_task(async function test_states_for_hide_sidebar_vertical() {
  info(
    `starting test with pref values: verticalTabs: ${Services.prefs.getBoolPref(VERTICAL_TABS_PREF)},
    visibility: ${Services.prefs.getStringPref(SIDEBAR_VISIBILITY_PREF)}`
  );
  await waitForTabstripOrientation("vertical", window);
  await SpecialPowers.pushPrefEnv({
    set: [[SIDEBAR_VISIBILITY_PREF, "hide-sidebar"]],
  });
  await window.SidebarController.sidebarMain.updateComplete;
  ok(
    window.SidebarController.sidebarContainer.hidden,
    "Sidebar is hidden when visibility is set to hide-sidebar"
  );

  info("Initial state ok, opening a new browser window");
  const win = await BrowserTestUtils.openNewBrowserWindow();
  await waitForTabstripOrientation("vertical", win);
  const { SidebarController } = win;
  const { sidebarContainer, sidebarMain, toolbarButton } = SidebarController;

  const checkStates = async (
    { hidden, expanded },
    container = sidebarContainer,
    component = sidebarMain,
    button = toolbarButton
  ) => {
    info(`Waiting for container to become ${hidden ? "hidden" : "not hidden"}`);
    await BrowserTestUtils.waitForMutationCondition(
      container,
      { attributes: true, attributeFilter: ["hidden"] },
      () => container.hidden == hidden
    );
    ok(
      true,
      hidden ? "Sidebar container is hidden." : "Sidebar container is shown."
    );
    info(
      `Waiting for component to be ${expanded ? "expanded" : "not expanded"}`
    );
    await BrowserTestUtils.waitForMutationCondition(
      component,
      { attributes: true, attributeFilter: ["expanded"] },
      () => Boolean(component.expanded) == expanded
    );
    ok(true, expanded ? "Sidebar is expanded." : "Sidebar is collapsed.");
    info(
      `Waiting for button to be ${hidden ? "not highlighted" : "highlighted"}`
    );
    await BrowserTestUtils.waitForMutationCondition(
      button,
      { attributes: true, attributeFilter: ["checked", "expanded"] },
      () =>
        button.checked == !hidden && button.hasAttribute("expanded") == expanded
    );
    ok(
      true,
      `Toolbar button checked state is correct and expanded attribute is ${expanded ? "present" : "absent"}.`
    );
    Assert.deepEqual(
      document.l10n.getAttributes(button),
      {
        id: hidden
          ? "sidebar-widget-show-sidebar2"
          : "sidebar-widget-hide-sidebar2",
        args:
          AppConstants.platform === "macosx"
            ? { shortcut: "⌃Z" }
            : { shortcut: "Alt+Ctrl+Z" },
      },
      "Toolbar button has the correct tooltip."
    );
  };

  // Check initial sidebar state - it should be hidden
  info("Check default hidden state in the new window.");
  await checkStates({ hidden: true, expanded: false });
  info("Show expanded sidebar using the toolbar button.");
  EventUtils.synthesizeMouseAtCenter(toolbarButton, {}, win);
  await checkStates({ hidden: false, expanded: true });
  await SidebarController.waitUntilStable();

  info("Don't collapse the sidebar by loading a tool.");
  const toolButton = sidebarMain.toolButtons[0];
  EventUtils.synthesizeMouseAtCenter(toolButton, {}, win);

  await checkStates({ hidden: false, expanded: true });
  ok(SidebarController.isOpen, "Panel is open.");

  info("Close a panel using the toolbar button.");
  EventUtils.synthesizeMouseAtCenter(toolbarButton, {}, win);

  await checkStates({ hidden: true, expanded: false });
  ok(!SidebarController.isOpen, "Panel is closed.");

  EventUtils.synthesizeMouseAtCenter(toolbarButton, {}, win);
  await checkStates({ hidden: false, expanded: true });

  info("Check states on a new window.");
  const newWin = await BrowserTestUtils.openNewBrowserWindow();
  await checkStates(
    { hidden: false, expanded: true },
    newWin.SidebarController.sidebarContainer,
    newWin.SidebarController.sidebarMain,
    newWin.SidebarController.toolbarButton
  );

  await BrowserTestUtils.closeWindow(win);
  await BrowserTestUtils.closeWindow(newWin);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_sidebar_button_runtime_pref_enabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.revamp", false]],
  });
  let button = document.getElementById("sidebar-button");
  Assert.ok(
    BrowserTestUtils.isVisible(button),
    "The sidebar button is visible"
  );

  CustomizableUI.removeWidgetFromArea("sidebar-button");
  Assert.ok(
    BrowserTestUtils.isHidden(button),
    "The sidebar button is not visible after being removed"
  );

  // rever the pref change, this should cause the button to be placed in the nav-bar
  await SpecialPowers.popPrefEnv();
  button = document.getElementById("sidebar-button");
  Assert.ok(
    BrowserTestUtils.isVisible(button),
    "The sidebar button is visible again when the pref is flipped"
  );

  let widgetPlacement = CustomizableUI.getPlacementOfWidget("sidebar-button");
  Assert.equal(
    widgetPlacement.area,
    CustomizableUI.AREA_NAVBAR,
    "The sidebar button is in the nav-bar"
  );

  Assert.ok(button.checked, "Sidebar button should be checked when showing.");
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.revamp", false]],
  });
  Assert.ok(
    !button.checked,
    "Sidebar button should not be checked when old sidebar is not showing."
  );
  await SpecialPowers.popPrefEnv();
  await SidebarController.waitUntilStable();
});

/**
 * Check that keyboard shortcut toggles sidebar
 */
add_task(async function test_keyboard_shortcut() {
  // When the button was removed, "hide-sidebar" was set automatically. Revert for this test.
  // Expanded is the default when "hide-sidebar" is set - revert to collapsed.
  await SpecialPowers.pushPrefEnv({
    set: [[SIDEBAR_VISIBILITY_PREF, "always-show"]],
  });
  await SidebarController.initializeUIState({ launcherExpanded: false });

  const sidebar = document.querySelector("sidebar-main");
  const key = document.getElementById("toggleSidebarKb");

  Assert.equal(
    Services.prefs.getStringPref(SIDEBAR_VISIBILITY_PREF),
    "always-show",
    "Got expected visibility value"
  );
  Assert.ok(!sidebar.expanded, "Sidebar initially not expanded");

  key.doCommand();

  Assert.ok(sidebar.expanded, "Sidebar expanded with keyboard");

  key.doCommand();

  Assert.ok(!sidebar.expanded, "Closed sidebar with keyboard");
  const events = Glean.sidebar.keyboardShortcut.testGetValue();
  Assert.equal(events.length, 2, "Got 2 keyboard events");
  Assert.equal(
    events[0].extra.panel,
    "",
    "No sidebar panels opened when expanding via keyboard shortcut"
  );
  Assert.equal(
    events[0].extra.opened,
    "true",
    "Glean event recorded that sidebar was expanded/shown with keyboard shortcut"
  );
  Assert.equal(
    events[1].extra.opened,
    "false",
    "Glean event recorded that sidebar was collapsed/hidden with keyboard shortcut"
  );
});
