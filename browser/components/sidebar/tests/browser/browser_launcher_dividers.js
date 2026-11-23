/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TRANSPARENT = "rgba(0, 0, 0, 0)";

/**
 * The dividers are drawn with a background-image, so a hidden divider is a
 * splitter which is still laid out and can still be dragged, but doesn't
 * paint anything.
 *
 * @param {Element} splitter
 * @param {boolean} shouldBeVisible
 * @param {string} message
 */
function assertDivider(splitter, shouldBeVisible, message) {
  Assert.ok(
    BrowserTestUtils.isVisible(splitter),
    `${message} (splitter laid out)`
  );
  const { backgroundImage } = window.getComputedStyle(splitter);
  if (shouldBeVisible) {
    Assert.notStrictEqual(backgroundImage, "none", message);
  } else {
    Assert.strictEqual(backgroundImage, "none", message);
  }
}

function getToolsSplitter() {
  return SidebarController.sidebarMain.shadowRoot.getElementById(
    "sidebar-tools-and-extensions-splitter"
  );
}

async function setLauncherExpanded(expanded) {
  SidebarController._state.launcherExpanded = expanded;
  await SidebarController.sidebarMain.updateComplete;
  await waitForRepaint();
  Assert.equal(
    gBrowser.tabContainer.hasAttribute("expanded"),
    expanded,
    `The tab strip is ${expanded ? "expanded" : "collapsed"}`
  );
}

/**
 * @param {Element} splitter
 * @param {boolean} hovered
 */
async function setSplitterHovered(splitter, hovered) {
  if (hovered) {
    EventUtils.synthesizeMouseAtCenter(splitter, { type: "mousemove" }, window);
  } else {
    // The top left corner of the window is never the splitter, which sits
    // below the pinned tabs.
    EventUtils.synthesizeMouse(
      document.documentElement,
      0,
      0,
      { type: "mousemove" },
      window
    );
  }
  await TestUtils.waitForCondition(
    () => splitter.matches(":hover") === hovered,
    `The splitter is ${hovered ? "hovered" : "no longer hovered"}`
  );
  await waitForRepaint();
}

async function overflowTabs() {
  const sidebarMain = SidebarController.sidebarMain;
  info("Open tabs until the tab strip overflows");
  while (!gBrowser.tabContainer.hasAttribute("overflow")) {
    await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:blank");
  }
  await BrowserTestUtils.waitForMutationCondition(
    sidebarMain,
    { attributes: true, attributeFilter: ["tabs-overflow"] },
    () => sidebarMain.hasAttribute("tabs-overflow")
  );
  await waitForRepaint();
}

async function stopOverflowingTabs() {
  const sidebarMain = SidebarController.sidebarMain;
  info("Close tabs until the tab strip no longer overflows");
  cleanUpExtraTabs();
  await BrowserTestUtils.waitForMutationCondition(
    sidebarMain,
    { attributes: true, attributeFilter: ["tabs-overflow"] },
    () => !sidebarMain.hasAttribute("tabs-overflow")
  );
  await waitForRepaint();
}

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      [VERTICAL_TABS_PREF, true],
      [SIDEBAR_VISIBILITY_PREF, "always-show"],
      ["sidebar.animation.enabled", false],
    ],
  });
  await SidebarTestUtils.waitForTabstripOrientation(window, "vertical");

  // The pinned tabs divider is only rendered when there are pinned tabs.
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:blank");
  gBrowser.pinTab(gBrowser.selectedTab);
  await SidebarController.waitUntilStable();
});

registerCleanupFunction(async () => {
  for (const tab of [...gBrowser.tabs]) {
    if (tab.pinned) {
      gBrowser.unpinTab(tab);
    }
  }
  cleanUpExtraTabs();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_dividers_hidden_while_expanded() {
  const pinnedTabsSplitter = SidebarController._pinnedTabsSplitter;

  await setLauncherExpanded(false);
  assertDivider(
    pinnedTabsSplitter,
    true,
    "The pinned tabs divider is drawn while the launcher is collapsed"
  );

  await setLauncherExpanded(true);
  assertDivider(
    pinnedTabsSplitter,
    false,
    "The pinned tabs divider is hidden while the launcher is expanded"
  );
  Assert.strictEqual(
    window.getComputedStyle(pinnedTabsSplitter).borderTopWidth,
    "0px",
    "No line is drawn in place of the hidden pinned tabs divider"
  );

  const toolsSplitter = getToolsSplitter();
  Assert.ok(
    toolsSplitter,
    "The tools and extensions splitter is rendered while the launcher is expanded"
  );
  assertDivider(
    toolsSplitter,
    false,
    "The tools and extensions divider is hidden while the launcher is expanded"
  );
});

add_task(async function test_hidden_divider_is_still_a_drag_target() {
  const pinnedTabsSplitter = SidebarController._pinnedTabsSplitter;

  await setLauncherExpanded(true);
  await setSplitterHovered(pinnedTabsSplitter, false);
  Assert.strictEqual(
    window.getComputedStyle(pinnedTabsSplitter).backgroundColor,
    TRANSPARENT,
    "The hidden pinned tabs divider is unhighlighted while the pointer is away"
  );

  await setSplitterHovered(pinnedTabsSplitter, true);
  const { backgroundColor, backgroundImage } =
    window.getComputedStyle(pinnedTabsSplitter);
  Assert.notStrictEqual(
    backgroundColor,
    TRANSPARENT,
    "Hovering the hidden pinned tabs divider highlights it for dragging"
  );
  Assert.strictEqual(
    backgroundImage,
    "none",
    "Hovering does not redraw the hidden pinned tabs divider"
  );

  await setSplitterHovered(pinnedTabsSplitter, false);
});

add_task(async function test_tools_divider_follows_tabs_overflow() {
  const sidebarMain = SidebarController.sidebarMain;
  const toolsSplitter = getToolsSplitter();

  await setLauncherExpanded(false);
  await setSplitterHovered(toolsSplitter, false);
  Assert.ok(
    !sidebarMain.hasAttribute("tabs-overflow"),
    "The launcher has no tabs-overflow attribute before the tabs overflow"
  );
  assertDivider(
    toolsSplitter,
    false,
    "The tools divider is hidden while the tabs fit"
  );

  await overflowTabs();
  assertDivider(
    toolsSplitter,
    true,
    "The tools divider is drawn while the tabs overflow"
  );

  await setSplitterHovered(toolsSplitter, true);
  Assert.strictEqual(
    window.getComputedStyle(toolsSplitter).backgroundImage,
    "none",
    "Hovering the tools divider shows the highlight instead of the line"
  );
  await setSplitterHovered(toolsSplitter, false);

  await stopOverflowingTabs();
  assertDivider(
    toolsSplitter,
    false,
    "The tools divider is hidden again once the tabs fit"
  );
});

add_task(async function test_tools_divider_full_width_while_expanded() {
  await SpecialPowers.pushPrefEnv({ set: [["browser.nova.enabled", true]] });
  const toolsSplitter = getToolsSplitter();

  await setLauncherExpanded(true);
  await setSplitterHovered(toolsSplitter, false);
  assertDivider(
    toolsSplitter,
    false,
    "The expanded tools divider is hidden while the tabs fit"
  );

  await overflowTabs();
  assertDivider(
    toolsSplitter,
    true,
    "The expanded tools divider is drawn while the tabs overflow"
  );
  Assert.equal(
    window.getComputedStyle(toolsSplitter).backgroundSize,
    "2px 2px, 2px 2px, calc(100% - 2px) 2px",
    "The expanded tools divider is a 2px line spanning the splitter between its caps"
  );

  await setSplitterHovered(toolsSplitter, true);
  const hoveredStyle = window.getComputedStyle(toolsSplitter);
  Assert.strictEqual(
    hoveredStyle.backgroundImage,
    "none",
    "Hovering the expanded tools divider replaces the line with the highlight"
  );
  Assert.notStrictEqual(
    hoveredStyle.backgroundColor,
    TRANSPARENT,
    "Hovering the expanded tools divider highlights it"
  );
  Assert.equal(
    toolsSplitter.getBoundingClientRect().height,
    4,
    "The hovered tools divider highlight is 4px tall"
  );
  await setSplitterHovered(toolsSplitter, false);

  await stopOverflowingTabs();
  await setLauncherExpanded(false);
  await SpecialPowers.popPrefEnv();
});
