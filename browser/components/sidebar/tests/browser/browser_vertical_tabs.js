/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TelemetryTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryTestUtils.sys.mjs"
);
const { SessionStoreTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/SessionStoreTestUtils.sys.mjs"
);
const { NonPrivateTabs } = ChromeUtils.importESModule(
  "resource:///modules/OpenTabs.sys.mjs"
);

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.animation.enabled", false],
      [VERTICAL_TABS_PREF, false],
    ],
  });
  Services.telemetry.clearScalars();
  SessionStoreTestUtils.init(this, window);
});
registerCleanupFunction(async () => {
  await SpecialPowers.popPrefEnv();
  cleanUpExtraTabs();
  NonPrivateTabs.stop();
});

function getTelemetryScalars(names) {
  return TestUtils.waitForCondition(
    () => {
      const scalars = TelemetryTestUtils.getProcessScalars("parent");
      return names.every(name => Object.hasOwn(scalars, name)) && scalars;
    },
    `Scalars are present in Telemetry data: ${names.join(", ")}`
  );
}

function checkTelemetryScalar(name, value) {
  return TestUtils.waitForCondition(() => {
    const scalars = TelemetryTestUtils.getProcessScalars("parent");
    return scalars[name] == value;
  }, `Scalar ${name} has value: ${value}`);
}

function getExpectedElements(win, tabstripOrientation = "horizontal") {
  const sizeMode = win.document.documentElement.getAttribute("sizemode");
  let selectors;

  // NOTE: CustomTitlebar behaviour isn't under test here. We just want to assert on
  // the right stuff being visible whatever the case for the given window.

  if (tabstripOrientation == "horizontal") {
    selectors = ["#TabsToolbar"];

    if (win.CustomTitlebar.enabled) {
      selectors.push("#TabsToolbar .titlebar-buttonbox-container");
      if (sizeMode == "normal") {
        selectors.push("#TabsToolbar .titlebar-spacer");
      }
    }
    return selectors;
  }

  selectors = ["#vertical-tabs"];
  if (win.CustomTitlebar.enabled) {
    selectors.push("#nav-bar .titlebar-buttonbox-container");
  }
  return selectors;
}

add_task(async function test_toggle_vertical_tabs() {
  await SidebarTestUtils.waitForTabstripOrientation(window, "horizontal");

  const sidebar = document.querySelector("sidebar-main");
  ok(sidebar, "Sidebar is shown.");

  let tabStrip = document.getElementById("tabbrowser-tabs");
  let defaultTabstripParent = document.getElementById(
    "TabsToolbar-customization-target"
  );
  let verticalTabs = document.querySelector("#vertical-tabs");
  info(
    `toolbars collapsed: \n${Array.from(
      document.querySelectorAll("#navigator-toolbox > toolbar")
    )
      .map(t => `  ${t.id}: ${t.collapsed}`)
      .join("\n")}`
  );
  info(`sizemode: ${document.documentElement.getAttribute("sizemode")}`);
  info(
    `customtitlebar: ${document.documentElement.getAttribute("customtitlebar")}`
  );

  const expectedElementsWhenHorizontal = getExpectedElements(
    window,
    "horizontal"
  );
  const expectedElementsWhenVertical = getExpectedElements(window, "vertical");

  for (let selector of expectedElementsWhenHorizontal) {
    let elem = document.querySelector(selector);
    ok(
      elem && BrowserTestUtils.isVisible(elem),
      `${selector} exists and is visible`
    );
  }
  for (let selector of expectedElementsWhenVertical) {
    let elem = document.querySelector(selector);
    ok(
      elem && BrowserTestUtils.isHidden(elem),
      `${selector} exists and is hidden`
    );
  }

  is(
    tabStrip.parentNode,
    defaultTabstripParent,
    "Tabstrip is in default horizontal position"
  );
  is(
    tabStrip.nextElementSibling.id,
    "new-tab-button",
    "Tabstrip is before the new tab button"
  );

  // flip the pref to move the tabstrip into the sidebar
  await SpecialPowers.pushPrefEnv({ set: [[VERTICAL_TABS_PREF, true]] });
  await SidebarTestUtils.waitForTabstripOrientation(window, "vertical");

  for (let selector of expectedElementsWhenVertical) {
    let elem = document.querySelector(selector);
    ok(
      elem && BrowserTestUtils.isVisible(elem),
      `${selector} exists and is visible: ${!!elem}, ${
        elem && BrowserTestUtils.isVisible(elem)
      }`
    );
  }
  for (let selector of expectedElementsWhenHorizontal) {
    let elem = document.querySelector(selector);
    ok(
      elem && BrowserTestUtils.isHidden(elem),
      `${selector} exists and is hidden: ${!!elem}, ${
        elem && BrowserTestUtils.isHidden(elem)
      }`
    );
  }

  is(
    tabStrip.parentNode,
    verticalTabs,
    "Tabstrip is slotted into the sidebar vertical tabs container"
  );
  is(gBrowser.tabs.length, 1, "Tabstrip now has one tab");

  // make sure the tab context menu still works
  const contextMenu = document.getElementById("tabContextMenu");
  gBrowser.selectedTab.focus();

  info("Open a new tab using the context menu.");
  const promiseTabOpen = BrowserTestUtils.waitForEvent(
    window.gBrowser.tabContainer,
    "TabOpen"
  );
  let hidden = BrowserTestUtils.waitForEvent(contextMenu, "popuphidden");
  await openAndWaitForContextMenu(
    contextMenu,
    gBrowser.selectedTab,
    async () => {
      info("Tab context menu opened");
      let newTabOption = document.getElementById("context_openANewTab");
      if (!newTabOption) {
        info("New tab context menu option not found");
      }
      newTabOption?.click();
    }
  );

  await promiseTabOpen;
  info("New tab opened");
  contextMenu.hidePopup();
  await hidden;

  let keyedScalars = TelemetryTestUtils.getProcessScalars("parent", true);
  TelemetryTestUtils.assertKeyedScalar(
    keyedScalars,
    "browser.ui.interaction.tabs_context_entrypoint",
    "vertical-tabs-container",
    1
  );

  let scalars = await getTelemetryScalars([
    "browser.engagement.vertical_tab_open_event_count",
  ]);
  TelemetryTestUtils.assertScalar(
    scalars,
    "browser.engagement.vertical_tab_open_event_count",
    1
  );
  await checkTelemetryScalar(
    "browser.engagement.max_concurrent_vertical_tab_count",
    2
  );

  info("Pin a tab using the context menu.");
  await SidebarController.waitUntilStable();
  const promiseTabPinned = BrowserTestUtils.waitForEvent(
    window,
    "TabPinned",
    true
  );
  let hidden2 = BrowserTestUtils.waitForEvent(contextMenu, "popuphidden");
  const tabToPin = gBrowser.selectedTab;
  await openAndWaitForContextMenu(contextMenu, tabToPin, async () => {
    info("Tab context menu opened");
    let pinTabOption = document.getElementById("context_pinTab");
    if (!pinTabOption) {
      info("Pin tab context menu option not found");
    }
    pinTabOption?.click();
  });

  await promiseTabPinned;
  // Wait for the specific pinned tab to appear in the container
  let pinnedTabsContainer = document.getElementById("pinned-tabs-container");
  await BrowserTestUtils.waitForMutationCondition(
    pinnedTabsContainer,
    { childList: true },
    () => pinnedTabsContainer.contains(tabToPin)
  );
  info("Tab pinned via the context menu");
  contextMenu.hidePopup();
  await hidden2;

  scalars = await getTelemetryScalars([
    "browser.engagement.max_concurrent_vertical_tab_pinned_count",
    "browser.engagement.vertical_tab_pinned_event_count",
  ]);
  TelemetryTestUtils.assertScalar(
    scalars,
    "browser.engagement.max_concurrent_vertical_tab_pinned_count",
    1
  );
  TelemetryTestUtils.assertScalar(
    scalars,
    "browser.engagement.vertical_tab_pinned_event_count",
    1
  );

  is(gBrowser.tabs.length, 2, "Tabstrip now has two tabs");

  let lastUnpinnedTab = gBrowser.visibleTabs.filter(t => !t.pinned).at(-1);
  let tabRect = lastUnpinnedTab.getBoundingClientRect();
  let containerRect = gBrowser.tabContainer.getBoundingClientRect();

  Assert.greater(
    containerRect.bottom - tabRect.bottom,
    450,
    "Container should extend far beyond the last tab."
  );

  // We are testing click events on the tabstrip itself, rather than on any
  // specific button. Disable a11y checks for this portion.
  AccessibilityUtils.setEnv({
    mustHaveAccessibleRule: false,
  });

  // Synthesize a double click 200px below the last tab
  let target = document.getElementById("tabbrowser-arrowscrollbox");
  let dblClickPromise = BrowserTestUtils.waitForEvent(target, "dblclick");
  EventUtils.synthesizeMouseAtPoint(
    containerRect.left + containerRect.width / 2,
    tabRect.bottom + 200,
    { clickCount: 1 }
  );
  EventUtils.synthesizeMouseAtPoint(
    containerRect.left + containerRect.width / 2,
    tabRect.bottom + 200,
    { clickCount: 2 }
  );
  await dblClickPromise;

  is(gBrowser.tabs.length, 3, "Tabstrip now has three tabs");

  tabRect = gBrowser.selectedTab.getBoundingClientRect();

  // Middle click should also open a new tab.
  EventUtils.synthesizeMouseAtPoint(
    containerRect.left + containerRect.width / 2,
    tabRect.bottom + 200,
    { button: 1 }
  );

  is(gBrowser.tabs.length, 4, "Tabstrip now has four tabs");

  AccessibilityUtils.resetEnv();

  const toolbarContextMenu = document.getElementById("toolbar-context-menu");
  EventUtils.synthesizeMouseAtPoint(
    containerRect.left + containerRect.width / 2,
    tabRect.bottom + 200,
    {
      type: "contextmenu",
      button: 2,
    }
  );

  await openAndWaitForContextMenu(
    toolbarContextMenu,
    gBrowser.selectedTab,
    () => {
      ok(
        document.getElementById("toolbar-context-customize").hidden,
        "Customize menu item should be hidden"
      );
      ok(
        !document.getElementById("toggle_PersonalToolbar"),
        "Bookmarks menu item should not be present"
      );
      ok(
        !document.getElementById("toolbar-context-reloadSelectedTab").hidden,
        "Reload tab item should be visible"
      );
      ok(
        !document.getElementById("toolbar-context-undoCloseTab").hidden,
        "Undo close tab item should be visible"
      );
    }
  );
  toolbarContextMenu.hidePopup();

  await openAndWaitForContextMenu(
    toolbarContextMenu,
    document.getElementById("sidebar-button"),
    () => {
      ok(
        !document.getElementById("toolbar-context-customize-sidebar").hidden,
        "Customize sidebar should be visible when sidebar-button is right clicked"
      );
    }
  );
  toolbarContextMenu.hidePopup();

  await openAndWaitForContextMenu(
    toolbarContextMenu,
    document.getElementById("tabbrowser-tabs"),
    () => {
      ok(
        !document.getElementById("toolbar-context-customize-sidebar").hidden,
        "Customize sidebar should be visible when the tab-strip is right clicked"
      );
      ok(
        document.getElementById("sidebarRevampSeparator").hidden,
        "If vertical tabs are enabled we should hide sidebar revamp separator"
      );
    }
  );
  toolbarContextMenu.hidePopup();

  await openAndWaitForContextMenu(
    toolbarContextMenu,
    document.querySelector("toolbarspring"),
    () => {
      ok(
        document.getElementById("toolbar-context-customize-sidebar").hidden,
        "Customize sidebar should be hidden when the toolbar is right clicked"
      );
    }
  );
  toolbarContextMenu.hidePopup();

  let newTabButton = document.getElementById("tabs-newtab-button");
  info("Open a new tab using the new tab button.");
  EventUtils.synthesizeMouseAtCenter(newTabButton, {});
  is(gBrowser.tabs.length, 5, "Tabstrip now has six tabs");

  // Middle click on new tab button should also open a new tab.
  info("Open a new tab middle clicking the new tab button.");
  // Make sure there is something in the clipboard that can be opened.
  SpecialPowers.clipboardCopyString("about:blank");
  EventUtils.synthesizeMouseAtCenter(newTabButton, { button: 1 });
  is(gBrowser.tabs.length, 6, "Tabstrip now has five tabs");

  keyedScalars = TelemetryTestUtils.getProcessScalars("parent", true);
  TelemetryTestUtils.assertKeyedScalar(
    keyedScalars,
    "browser.ui.interaction.vertical_tabs_container",
    "tabs-newtab-button",
    1
  );

  await checkTelemetryScalar(
    "browser.engagement.max_concurrent_vertical_tab_count",
    6
  );

  // flip the pref to move the tabstrip horizontally
  await SpecialPowers.pushPrefEnv({ set: [[VERTICAL_TABS_PREF, false]] });
  await SidebarTestUtils.waitForTabstripOrientation(window, "horizontal");

  ok(
    !BrowserTestUtils.isVisible(verticalTabs),
    "Vertical tabs slot is not visible"
  );
  is(
    tabStrip.parentNode,
    defaultTabstripParent,
    "Tabstrip is in default horizontal position"
  );
  is(
    tabStrip.nextElementSibling.id,
    "new-tab-button",
    "Tabstrip is before the new tab button"
  );

  scalars = await getTelemetryScalars([
    "browser.engagement.max_concurrent_tab_count",
    "browser.engagement.max_concurrent_tab_pinned_count",
  ]);
  TelemetryTestUtils.assertScalar(
    scalars,
    "browser.engagement.max_concurrent_tab_count",
    6
  );
  TelemetryTestUtils.assertScalar(
    scalars,
    "browser.engagement.max_concurrent_tab_pinned_count",
    1
  );
});

add_task(async function test_enabling_vertical_tabs_enables_sidebar_revamp() {
  await SpecialPowers.pushPrefEnv({ set: [["sidebar.revamp", false]] });
  await SidebarTestUtils.waitForTabstripOrientation(window, "horizontal");
  ok(
    !Services.prefs.getBoolPref("sidebar.revamp", false),
    "sidebar.revamp pref is false initially."
  );
  ok(
    !Services.prefs.getBoolPref(VERTICAL_TABS_PREF, false),
    "sidebar.verticalTabs pref is false initially."
  );

  await SpecialPowers.pushPrefEnv({ set: [[VERTICAL_TABS_PREF, true]] });
  await SidebarTestUtils.waitForTabstripOrientation(window, "vertical");
  ok(
    Services.prefs.getBoolPref(VERTICAL_TABS_PREF, false),
    "sidebar.verticalTabs pref is enabled after we've enabled it."
  );
  ok(
    Services.prefs.getBoolPref("sidebar.revamp", false),
    "sidebar.revamp pref is also enabled after we've enabled vertical tabs."
  );
});

add_task(async function test_vertical_tabs_overflow() {
  await SidebarTestUtils.waitForTabstripOrientation(window, "vertical");
  let tabsContainer = document.getElementById("tabbrowser-tabs");
  info("Open new tabs until they overflow");
  let index = gBrowser.tabs.length;
  while (!tabsContainer.hasAttribute("overflow")) {
    await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      `data:text/html,<title>${index + 1}</title>`
    );
    index++;
  }
  await SidebarController.waitUntilStable();

  info("Open a new tab using the new tab button.");
  const newTabButton = document.getElementById("vertical-tabs-newtab-button");
  ok(
    BrowserTestUtils.isVisible(newTabButton),
    "New tab button is visible while tabs are overflowing."
  );
  EventUtils.synthesizeMouseAtCenter(newTabButton, {});

  is(gBrowser.tabs.length, index + 1, `Tabstrip now has ${index + 1} tabs.`);
  const keyedScalars = TelemetryTestUtils.getProcessScalars("parent", true);
  TelemetryTestUtils.assertKeyedScalar(
    keyedScalars,
    "browser.ui.interaction.vertical_tabs_container",
    "vertical-tabs-newtab-button",
    1
  );

  cleanUpExtraTabs();
});

add_task(async function test_vertical_tabs_expanded() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.revamp", true],
      [VERTICAL_TABS_PREF, true],
    ],
  });
  await SidebarTestUtils.waitForTabstripOrientation(window, "vertical");

  info("Disable revamped sidebar.");
  Services.prefs.setBoolPref("sidebar.revamp", false);
  await SidebarTestUtils.waitForTabstripOrientation(window, "horizontal");
  ok(
    BrowserTestUtils.isHidden(document.getElementById("sidebar-container")),
    "Sidebar launcher is hidden."
  );

  info("Enable vertical tabs.");
  Services.prefs.setBoolPref(VERTICAL_TABS_PREF, true);
  await SidebarTestUtils.waitForTabstripOrientation(window, "vertical");
  ok(
    BrowserTestUtils.isVisible(document.getElementById("sidebar-container")),
    "Sidebar launcher is shown."
  );
  // We expect the launcher to be expanded by default when enabling vertical tabs
  const expandedStateValues = [
    SidebarController.getUIState().launcherExpanded,
    SidebarController.sidebarMain.expanded,
    gBrowser.tabContainer.hasAttribute("expanded"),
  ];
  for (const val of expandedStateValues) {
    is(val, true, "Launcher is expanded.");
  }

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_vertical_tabs_min_width() {
  await SpecialPowers.pushPrefEnv({
    set: [[VERTICAL_TABS_PREF, true]],
  });
  await SidebarTestUtils.waitForTabstripOrientation(window, "vertical");

  ok(
    BrowserTestUtils.isVisible(SidebarController.sidebarMain),
    "Sidebar launcher is shown."
  );

  // We expect the launcher to be expanded by default when enabling vertical tabs
  const expandedStateValues = [
    SidebarController.getUIState().launcherExpanded,
    SidebarController.sidebarMain.expanded,
    gBrowser.tabContainer.hasAttribute("expanded"),
  ];
  for (const val of expandedStateValues) {
    is(val, true, "Launcher is expanded.");
  }

  is(
    window.getComputedStyle(gBrowser.visibleTabs[0]).minWidth,
    "auto",
    "Tab min-width is set to 'auto' when vertical tabs are enabled."
  );

  info("Collapse sidebar and tabs");
  await SidebarController.updateUIState({ launcherExpanded: false });

  const collapsedStateValues = [
    SidebarController.getUIState().launcherExpanded,
    SidebarController.sidebarMain.expanded,
    gBrowser.tabContainer.hasAttribute("expanded"),
  ];
  for (const val of collapsedStateValues) {
    is(val, false, "Launcher is collapsed.");
  }

  let tabs = [
    gBrowser.selectedTab,
    BrowserTestUtils.addTab(gBrowser, "about:blank"),
  ];
  gBrowser.pinTab(tabs[1]);
  let pinnedTabsContainer = document.querySelector("#pinned-tabs-container");
  ok(
    BrowserTestUtils.isVisible(pinnedTabsContainer),
    "Vertical pinned tabs container is visible"
  );
  is(
    pinnedTabsContainer.children.length,
    2,
    "Two tabs are pinned in vertical pinned tabs container"
  );
  is(
    pinnedTabsContainer.getBoundingClientRect().width,
    gBrowser.tabContainer.getBoundingClientRect().width,
    "Vertical pinned tabs container should be the same width as the tab strip"
  );

  is(
    Math.round(tabs[0].getBoundingClientRect().width),
    Math.round(tabs[1].getBoundingClientRect().width),
    "Vertical pinned tabs should be the same width as the unpinned tabs"
  );
  gBrowser.unpinTab(tabs[0]);
  gBrowser.unpinTab(tabs[1]);

  // Switch to horizontal tabs
  await SpecialPowers.pushPrefEnv({
    set: [[VERTICAL_TABS_PREF, false]],
  });
  await SidebarTestUtils.waitForTabstripOrientation(window, "horizontal");

  let tabbrowserTabs = document.getElementById("tabbrowser-tabs");
  let tabStyles = window.getComputedStyle(tabbrowserTabs);
  let tabMinWidthVal = tabStyles.getPropertyValue("--tab-min-width-pref");

  await BrowserTestUtils.allTabOpenAnimationsFinished(window);
  is(
    window.getComputedStyle(gBrowser.visibleTabs[0]).minWidth,
    tabMinWidthVal,
    "Tab min-width is set based on the browser.tabs.tabMinWidth pref in horizontal tabs mode."
  );

  // clean up extra tabs
  cleanUpExtraTabs();
  await SpecialPowers.popPrefEnv();
});

add_task(
  async function test_launcher_collapsed_entering_horiz_tabs_with_hide_sidebar() {
    const { sidebarMain } = SidebarController;
    await SpecialPowers.pushPrefEnv({ set: [[VERTICAL_TABS_PREF, true]] });
    await SidebarTestUtils.waitForTabstripOrientation(window, "vertical");
    ok(
      BrowserTestUtils.isVisible(sidebarMain),
      "Revamped sidebar main is shown initially."
    );
    ok(
      sidebarMain.expanded,
      "Launcher is expanded with vertical tabs and always-show"
    );

    await SpecialPowers.pushPrefEnv({
      set: [["sidebar.visibility", "hide-sidebar"]],
    });
    await sidebarMain.updateComplete;
    ok(
      BrowserTestUtils.isHidden(sidebarMain),
      "Revamped sidebar main hidden when we switch to hide-sidebar."
    );

    // toggle the launcher back open.
    document.getElementById("sidebar-button").doCommand();
    await sidebarMain.updateComplete;
    ok(
      BrowserTestUtils.isVisible(sidebarMain),
      "Revamped sidebar main visible again."
    );
    ok(
      sidebarMain.expanded,
      "Launcher is still expanded as vertical tabs are still enabled"
    );

    // switch back to horizontal tabs and confirm the launcher get un-expanded
    await SpecialPowers.pushPrefEnv({ set: [[VERTICAL_TABS_PREF, false]] });
    await SidebarTestUtils.waitForTabstripOrientation(window, "horizontal");

    ok(
      BrowserTestUtils.isVisible(sidebarMain),
      "Revamped sidebar main is still visible when we switch to horizontal tabs."
    );
    ok(
      !sidebarMain.expanded,
      "Launcher is collapsed when we switch to horizontal tabs with hide-sidebar"
    );

    await SpecialPowers.popPrefEnv();
    await SpecialPowers.popPrefEnv();
    await SpecialPowers.popPrefEnv();
  }
);
