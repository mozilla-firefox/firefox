/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      [VERTICAL_TABS_PREF, true],
      ["sidebar.expandOnHover", true],
    ],
  });
});
registerCleanupFunction(async () => {
  await SpecialPowers.popPrefEnv();
  cleanUpExtraTabs();
});

async function mouseOverSidebarToExpand() {
  // Disable non-test mouse events
  window.windowUtils.disableNonTestMouseEvents(true);

  EventUtils.synthesizeMouse(SidebarController.sidebarContainer, 1, 80, {
    type: "mousemove",
  });

  await BrowserTestUtils.waitForMutationCondition(
    SidebarController.sidebarContainer,
    { attributes: true },
    async () => {
      await SidebarController.waitUntilStable();
      return (
        SidebarController.sidebarContainer.hasAttribute(
          "sidebar-launcher-expanded"
        ) &&
        SidebarController.sidebarMain.expanded &&
        SidebarController._state.launcherExpanded &&
        window.getComputedStyle(SidebarController.sidebarContainer).position ===
          "absolute"
      );
    },
    "The sidebar launcher is expanded"
  );

  info("The sidebar launcher is expanded on mouse over");

  window.windowUtils.disableNonTestMouseEvents(false);
}

async function mouseOutSidebarToCollapse() {
  // Disable non-test mouse events
  window.windowUtils.disableNonTestMouseEvents(true);

  EventUtils.synthesizeMouseAtCenter(SidebarController.contentArea, {
    type: "mousemove",
  });

  await BrowserTestUtils.waitForMutationCondition(
    SidebarController.sidebarContainer,
    { attributes: true },
    async () => {
      await SidebarController.waitUntilStable();
      return (
        !SidebarController.sidebarContainer.hasAttribute(
          "sidebar-launcher-expanded"
        ) &&
        !SidebarController.sidebarMain.expanded &&
        !SidebarController._state.launcherExpanded &&
        window.getComputedStyle(SidebarController.sidebarContainer).position ===
          "relative"
      );
    },
    "The sidebar launcher is collapsed"
  );

  info("The sidebar launcher is collapsed on mouse out");

  window.windowUtils.disableNonTestMouseEvents(false);
}

add_task(async function test_enable_expand_on_hover() {
  await SidebarController.show("viewCustomizeSidebar");
  let rootEl = document.documentElement;
  let browserEl = document.getElementById("browser");
  const panel =
    SidebarController.browser.contentDocument.querySelector(
      "sidebar-customize"
    );
  const sidebarBox = document.getElementById("sidebar-box");
  await BrowserTestUtils.waitForMutationCondition(
    browserEl,
    { childList: true, subtree: true },
    () =>
      BrowserTestUtils.isVisible(sidebarBox) &&
      panel.expandOnHoverInput?.shadowRoot.querySelector("input"),
    "Sidebar panel is visible and input is displayed"
  );

  info("Sidebar panel is visible and input is displayed");

  // Enable expand on hover
  panel.expandOnHoverInput.click();
  EventUtils.synthesizeMouseAtCenter(SidebarController.contentArea, {
    type: "mousemove",
  });
  await BrowserTestUtils.waitForMutationCondition(
    SidebarController.sidebarContainer,
    { attributes: true },
    () =>
      rootEl.hasAttribute("sidebar-expand-on-hover") &&
      !SidebarController.sidebarContainer.hasAttribute(
        "sidebar-launcher-expanded"
      ) &&
      !SidebarController._state.launcherExpanded &&
      SidebarController.sidebarRevampVisibility === "expand-on-hover" &&
      window.getComputedStyle(SidebarController.sidebarContainer).position ===
        "relative" &&
      panel.expandOnHoverInput.checked,
    "Expand on hover has been enabled"
  );

  info("Expand on hover has been enabled");

  ok(
    rootEl.hasAttribute("sidebar-expand-on-hover"),
    "#browser element has sidebar-expand-on-hover attribute"
  );

  await mouseOverSidebarToExpand();
  await mouseOutSidebarToCollapse();
  await SidebarController.waitUntilStable();

  panel.positionInput.click();
  await BrowserTestUtils.waitForMutationCondition(
    SidebarController.sidebarContainer,
    { attributes: true },
    () =>
      SidebarController.sidebarContainer.hasAttribute("sidebar-positionend"),
    "The sidebar is positioned on the right"
  );

  await mouseOverSidebarToExpand();
  await mouseOutSidebarToCollapse();

  // Move the sidebar back to the left
  panel.positionInput.click();
  await BrowserTestUtils.waitForMutationCondition(
    SidebarController.sidebarContainer,
    { attributes: true },
    () =>
      !SidebarController.sidebarContainer.hasAttribute("sidebar-positionend"),
    "The sidebar is positioned on the left"
  );

  await mouseOutSidebarToCollapse();
  panel.expandOnHoverInput.click();
  await BrowserTestUtils.waitForMutationCondition(
    SidebarController.sidebarContainer,
    { attributes: true },
    () =>
      !rootEl.hasAttribute("sidebar-expand-on-hover") &&
      SidebarController.sidebarRevampVisibility !== "expand-on-hover" &&
      window.getComputedStyle(SidebarController.sidebarContainer).position !==
        "relative" &&
      !panel.expandOnHoverInput.checked,
    "Expand on hover has been disabled"
  );
});

add_task(async function test_expand_on_hover_pinned_tabs() {
  await SidebarController.toggleExpandOnHover(true);
  await SidebarController.waitUntilStable();

  let newTabButton = document.getElementById("tabs-newtab-button");
  info("Open 2 new tabs using the new tab button.");
  newTabButton.click();
  newTabButton.click();
  is(gBrowser.tabs.length, 3, "Tabstrip now has three tabs");
  gBrowser.selectedTab.toggleMuteAudio();
  gBrowser.pinTab(gBrowser.selectedTab);
  let unpinnedTabs = gBrowser.visibleTabs.filter(tab => !tab.pinned);
  gBrowser.pinTab(unpinnedTabs[0]);
  let pinnedTabs = gBrowser.visibleTabs.filter(tab => tab.pinned);
  let verticalPinnedTabsContainer = document.getElementById(
    "vertical-pinned-tabs-container"
  );
  let verticalTabsComputedStyle = window.getComputedStyle(
    verticalPinnedTabsContainer
  );
  let inlineMuteButton =
    gBrowser.selectedTab.querySelector(".tab-audio-button");
  let muteButtonComputedStyle = window.getComputedStyle(inlineMuteButton);
  let pinnedTabComputedStyle = window.getComputedStyle(pinnedTabs[0]);
  await mouseOverSidebarToExpand();
  await SidebarController.waitUntilStable();
  await BrowserTestUtils.waitForMutationCondition(
    SidebarController.sidebarContainer,
    { attributes: true },
    () =>
      SidebarController._state.launcherExpanded &&
      SidebarController.sidebarMain.hasAttribute("expanded"),
    "The launcher is expanded"
  );
  is(
    Math.round(parseInt(verticalTabsComputedStyle.width)),
    Math.round(parseInt(pinnedTabComputedStyle.width)),
    "The pinned tabs are full width when expanded"
  );

  is(
    muteButtonComputedStyle.display,
    "none",
    "The expanded pinned tab is not showing the inline audio button."
  );

  await mouseOutSidebarToCollapse();
  await SidebarController.toggleExpandOnHover(false);
  await SidebarController.waitUntilStable();
});

add_task(async function test_expand_on_hover_context_menu() {
  await SidebarController.toggleExpandOnHover(true);
  await SidebarController.waitUntilStable();
  await mouseOverSidebarToExpand();
  await SidebarController.waitUntilStable();
  await BrowserTestUtils.waitForMutationCondition(
    SidebarController.sidebarContainer,
    { attributes: true },
    () => SidebarController._state.launcherExpanded,
    "The launcher is expanded"
  );

  const toolbarContextMenu = document.getElementById("toolbar-context-menu");
  await openAndWaitForContextMenu(
    toolbarContextMenu,
    SidebarController.sidebarMain,
    () => {
      ok(
        !document.getElementById("toolbar-context-customize-sidebar").hidden,
        "The sidebar context menu is loaded"
      );
      ok(
        SidebarController._state.launcherExpanded,
        "The sidebar launcher is still expanded with the context menu open"
      );
    }
  );
  toolbarContextMenu.hidePopup();
  await mouseOutSidebarToCollapse();
  await SidebarController.toggleExpandOnHover(false);
  await SidebarController.waitUntilStable();
});
