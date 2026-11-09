/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { SidebarTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/SidebarTestUtils.sys.mjs"
);
SidebarTestUtils.init(this);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.revamp", true],
      ["sidebar.verticalTabs", true],
      ["ui.prefersReducedMotion", 0],
    ],
  });
  await SidebarTestUtils.waitForTabstripOrientation(window, "vertical");
});

/**
 * Opens a vertical tab and waits for its height animation to finish.
 *
 * @returns {Promise<MozTabbrowserTab>}
 */
async function addTab() {
  const tab = BrowserTestUtils.addTab(gBrowser, "about:blank");
  await waitForOpeningAnimation(tab);
  Assert.greater(
    getTransitions(tab).length,
    0,
    "A vertical tab has an opening height transition"
  );
  const opened = BrowserTestUtils.waitForEvent(tab, "TabAnimationEnd");
  await finishHeightAnimations(tab);
  await opened;
  return tab;
}

/**
 * Waits for a tab to enter its opening animation.
 *
 * @param {MozTabbrowserTab} tab
 * @returns {Promise<boolean>}
 */
function waitForOpeningAnimation(tab) {
  return BrowserTestUtils.waitForMutationCondition(
    tab,
    { attributes: true },
    () => tab.hasAttribute("fadein") && tab.hasAttribute("height-animating")
  );
}

/**
 * Finishes the element's animations and lets completion handlers run.
 *
 * @param {Element} element
 * @returns {Promise<void>}
 */
async function finishHeightAnimations(element) {
  for (const animation of element.getAnimations()) {
    animation.finish();
  }
  await TestUtils.waitForTick();
}

/**
 * Returns active transitions on a tab.
 *
 * @param {MozTabbrowserTab} tab
 * @param {string} transitionProperty
 * @returns {CSSTransition[]}
 */
function getTransitions(tab, transitionProperty = "grid-template-rows") {
  return tab
    .getAnimations()
    .filter(
      animation =>
        CSSTransition.isInstance(animation) &&
        animation.transitionProperty == transitionProperty
    );
}

/**
 * Temporarily switch to horizontal tabs and execute a task. It can be async.
 *
 * @param {() => Promise<void>} task
 * @returns {Promise<void>}
 */
async function withHorizontalTabstrip(task) {
  await SpecialPowers.pushPrefEnv({ set: [["sidebar.verticalTabs", false]] });
  try {
    await SidebarTestUtils.waitForTabstripOrientation(window, "horizontal");
    await task();
  } finally {
    await SpecialPowers.popPrefEnv();
    await SidebarTestUtils.waitForTabstripOrientation(window, "vertical");
  }
}

add_task(async function test_open_and_close() {
  const tab = await addTab();
  Assert.ok(
    gBrowser.tabContainer.openAnimationFinished(tab),
    "Vertical tab opening completes"
  );

  const height = tab.getBoundingClientRect().height;
  const closed = BrowserTestUtils.waitForEvent(tab, "TabAnimationEnd");
  gBrowser.removeTab(tab, { animate: true });
  const animations = tab.getAnimations();
  Assert.greater(
    getTransitions(tab).length,
    0,
    "Vertical tab closing is animated"
  );
  for (const animation of animations) {
    animation.pause();
    await animation.ready;
    animation.currentTime = animation.effect.getComputedTiming().duration / 2;
  }
  const intermediateHeight = tab.getBoundingClientRect().height;
  Assert.greater(
    intermediateHeight,
    0,
    "The tab remains rendered during closing"
  );
  Assert.less(
    intermediateHeight,
    height,
    "Closing reduces the occupied height"
  );
  await finishHeightAnimations(tab);
  await closed;
  Assert.ok(!tab.isConnected, "The tab is removed when its animation ends");
});

add_task(async function test_pin_tab_while_opening() {
  // Open one tab, and wait for its animations to complete before pinning it.
  const pinnedTab = await addTab();
  gBrowser.pinTab(pinnedTab);

  // Open another tab, this time pinning it during in-flight animation.
  const newTab = BrowserTestUtils.addTab(gBrowser, "about:blank");
  const animationEnd = BrowserTestUtils.waitForEvent(newTab, "TabAnimationEnd");
  Assert.ok(
    !gBrowser.tabContainer.openAnimationFinished(newTab),
    "The new tab is still opening before pinning"
  );
  gBrowser.pinTab(newTab);
  await animationEnd;

  Assert.ok(
    gBrowser.tabContainer.openAnimationFinished(newTab),
    "Pinning does not leave stale animations"
  );
  Assert.ok(
    !newTab.hasAttribute("height-animating"),
    "Pinning does not leave height animation state behind"
  );
  Assert.equal(
    getTransitions(newTab).length,
    0,
    "No height transition remains on the pinned tab"
  );
  Assert.greater(
    newTab.getBoundingClientRect().height,
    0,
    "The pinned tab is not collapsed"
  );
  Assert.equal(
    newTab.getBoundingClientRect().height,
    pinnedTab.getBoundingClientRect().height,
    "Both tabs reach the same height"
  );

  BrowserTestUtils.removeTab(newTab);
  BrowserTestUtils.removeTab(pinnedTab);
});

add_task(async function test_density_changes_do_not_animate() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:blank" },
    async browser => {
      const tab = gBrowser.getTabForBrowser(browser);
      await BrowserTestUtils.allTabOpenAnimationsFinished(window);
      Assert.ok(!gReduceMotion, "Motion is enabled when changing density");

      gUIDensity.update(gUIDensity.MODE_NORMAL);
      let previousHeight = tab.getBoundingClientRect().height;
      for (const density of ["MODE_COMPACT", "MODE_TOUCH", "MODE_NORMAL"]) {
        gUIDensity.update(gUIDensity[density]);
        const height = tab.getBoundingClientRect().height;
        const heightTransitions = getTransitions(tab);
        Assert.equal(
          heightTransitions.length,
          0,
          `Changing density to ${density} does not start a height transition`
        );
        Assert.notEqual(
          height,
          previousHeight,
          `Changing density to ${density} immediately changes the tab height`
        );
        previousHeight = height;
      }
    }
  );
  gUIDensity.update();
});

add_task(async function test_reduced_motion() {
  let tab = BrowserTestUtils.addTab(gBrowser, "about:blank");
  await waitForOpeningAnimation(tab);
  for (const animation of tab.getAnimations()) {
    animation.pause();
    await animation.ready;
  }
  const opened = BrowserTestUtils.waitForEvent(tab, "TabAnimationEnd");
  await SpecialPowers.pushPrefEnv({ set: [["ui.prefersReducedMotion", 1]] });
  await opened;
  Assert.ok(gReduceMotion, "Reduced motion is enabled when opening completes");
  Assert.ok(
    !tab.hasAttribute("height-animating"),
    "Enabling reduced motion finishes the opening animation"
  );
  Assert.equal(tab.getAnimations().length, 0, "No opening animation remains");
  BrowserTestUtils.removeTab(tab, { animate: true });
  Assert.ok(!tab.isConnected, "Reduced motion closes tabs immediately");

  tab = BrowserTestUtils.addTab(gBrowser, "about:blank");
  Assert.ok(
    !tab.hasAttribute("height-animating"),
    "Reduced motion skips the opening animation"
  );
  Assert.equal(
    tab.getAnimations().length,
    0,
    "The tab opens without animation"
  );
  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_close_while_opening() {
  const tab = BrowserTestUtils.addTab(gBrowser, "about:blank");
  await waitForOpeningAnimation(tab);
  Assert.greater(
    getTransitions(tab).length,
    0,
    "Closing interrupts a height transition"
  );
  for (const animation of tab.getAnimations()) {
    animation.pause();
    await animation.ready;
    animation.currentTime = animation.effect.getComputedTiming().duration / 2;
  }
  const height = tab.getBoundingClientRect().height;
  const closed = BrowserTestUtils.waitForEvent(tab, "TabAnimationEnd");
  gBrowser.removeTab(tab, { animate: true });
  Assert.equal(
    tab.getBoundingClientRect().height,
    height,
    "Closing continues from the current opening height"
  );
  await finishHeightAnimations(tab);
  await closed;
  Assert.ok(
    !tab.isConnected,
    "The interrupted opening does not prevent removal"
  );
  Assert.ok(
    !tab.hasAttribute("height-animating"),
    "Closing clears transient state"
  );
  Assert.ok(
    gBrowser.tabContainer.openAnimationFinished(tab),
    "The closed tab is no longer counted as opening"
  );
});

add_task(async function test_switch_from_vertical_to_horizontal() {
  // Open a tab in vertical mode, then switch to horizontal while tab is opening.
  const tab = BrowserTestUtils.addTab(gBrowser, "about:blank");
  await waitForOpeningAnimation(tab);
  let animation = getTransitions(tab)[0];
  Assert.ok(animation, "Opening starts a height animation");
  animation.pause();
  await animation.ready;
  animation.currentTime = animation.effect.getComputedTiming().duration / 2;

  const opened = BrowserTestUtils.waitForEvent(tab, "TabAnimationEnd");
  await withHorizontalTabstrip(() => opened);
  Assert.ok(
    !tab.hasAttribute("height-animating"),
    "Changing to horizontal tabs clears the opening animation state"
  );
  Assert.ok(
    gBrowser.tabContainer.openAnimationFinished(tab),
    "The tab is no longer counted as opening"
  );

  // Close a tab in vertical mode, then switch to horizontal while tab is closing.
  const closed = BrowserTestUtils.waitForEvent(tab, "TabAnimationEnd");
  gBrowser.removeTab(tab, { animate: true });
  animation = getTransitions(tab)[0];
  Assert.ok(animation, "Closing starts a height animation");
  animation.pause();
  await animation.ready;
  animation.currentTime = animation.effect.getComputedTiming().duration / 2;

  await withHorizontalTabstrip(() => closed);
  Assert.ok(
    !tab.hasAttribute("height-animating"),
    "Changing to horizontal tabs clears the closing animation state"
  );
  Assert.ok(!tab.isConnected, "The tab is removed after its animation ends");
});

add_task(async function test_switch_from_horizontal_to_vertical() {
  // Open a tab in horizontal mode, then switch to vertical while tab is opening.
  let tab;
  let opened;
  await withHorizontalTabstrip(async () => {
    tab = BrowserTestUtils.addTab(gBrowser, "about:blank");
    await BrowserTestUtils.waitForMutationCondition(
      tab,
      { attributes: true },
      () => tab.hasAttribute("fadein")
    );
    const animation = getTransitions(tab, "max-width")[0];
    Assert.ok(animation, "Horizontal opening starts a width transition");
    animation.pause();
    await animation.ready;
    animation.currentTime = animation.effect.getComputedTiming().duration / 2;
    opened = BrowserTestUtils.waitForEvent(tab, "TabAnimationEnd");
  });
  await opened;
  Assert.equal(
    gBrowser.tabContainer.tabAnimationsInProgress,
    0,
    "Switching to vertical tabs completes horizontal opening"
  );
  Assert.ok(
    !tab.hasAttribute("height-animating"),
    "No height animation state remains after opening"
  );

  // Close a tab in horizontal mode, then switch to vertical while tab is closing.
  let closed;
  await withHorizontalTabstrip(async () => {
    closed = BrowserTestUtils.waitForEvent(tab, "TabAnimationEnd");
    gBrowser.removeTab(tab, { animate: true });
    const animation = getTransitions(tab, "max-width")[0];
    Assert.ok(animation, "Horizontal closing starts a width transition");
    animation.pause();
    await animation.ready;
    animation.currentTime = animation.effect.getComputedTiming().duration / 2;
  });
  await closed;
  Assert.equal(
    gBrowser.tabContainer.tabAnimationsInProgress,
    0,
    "The closed tab is no longer counted as removing"
  );
});

add_task(async function test_expanded_height_fits_tab_content() {
  const tab = await addTab();
  const firstTab = gBrowser.visibleTabs.find(t => !t.pinned);
  for (const fontSize of ["", "24px"]) {
    document.documentElement.style.fontSize = fontSize;
    for (const density of ["MODE_COMPACT", "MODE_NORMAL", "MODE_TOUCH"]) {
      gUIDensity.update(gUIDensity[density]);
      for (const t of [firstTab, tab]) {
        const height = t.getBoundingClientRect().height;
        t.style.display = "flex";
        const naturalHeight = t.getBoundingClientRect().height;
        t.style.display = "";
        Assert.equal(
          height,
          naturalHeight,
          `display doesn't clip the tab at ${density}, font size ${fontSize || "default"}`
        );
      }
    }
  }
  document.documentElement.style.fontSize = "";
  gUIDensity.update();
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_split_view_tabs_skip_opening_animations() {
  // TODO: (Bug 2086091) Replace with animation tests when split view animations are supported.
  const tabs = [
    BrowserTestUtils.addTab(gBrowser, "about:blank"),
    BrowserTestUtils.addTab(gBrowser, "about:blank"),
  ];
  gBrowser.addTabSplitView(tabs, { insertBefore: tabs[0] });

  for (const tab of tabs) {
    Assert.greater(
      tab.getBoundingClientRect().height,
      0,
      "A split view tab occupies its height immediately"
    );
    Assert.equal(
      getTransitions(tab).length,
      0,
      "The split view tab has no height transition"
    );
  }

  for (const tab of tabs) {
    await BrowserTestUtils.waitForMutationCondition(
      tab,
      { attributes: true },
      () => tab.hasAttribute("fadein")
    );
    Assert.equal(
      getTransitions(tab).length,
      0,
      "The split view tab opens without a height transition"
    );
  }

  for (const tab of tabs) {
    BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function test_split_view_tabs_skip_closing_animations() {
  // TODO: (Bug 2086091) Replace with animation tests when split view animations are supported.
  const firstTab = await addTab();
  const secondTab = await addTab();
  gBrowser.addTabSplitView([firstTab, secondTab], {
    insertBefore: firstTab,
  });

  gBrowser.removeTab(firstTab, { animate: true });
  Assert.ok(!firstTab.isConnected, "A split view tab closes immediately");
  const splitView = secondTab.splitview;
  if (splitView) {
    await BrowserTestUtils.waitForMutationCondition(
      splitView,
      { childList: true, subtree: true },
      () => !secondTab.splitview
    );
  }
  Assert.ok(secondTab.isConnected, "The remaining tab stays open");
  Assert.equal(
    getTransitions(secondTab).length,
    0,
    "The remaining tab does not animate when it is unsplit"
  );

  BrowserTestUtils.removeTab(secondTab);

  // Ensure that closing a split view immediately removes its child tabs,
  // regardless of which one was selected (if any) before closing.
  for (const selectTabIndex of [-1, 0, 1]) {
    const tabsToClose = [await addTab(), await addTab()];
    const splitViewToClose = gBrowser.addTabSplitView(tabsToClose, {
      insertBefore: tabsToClose[0],
    });
    if (selectTabIndex > -1) {
      await BrowserTestUtils.switchTab(gBrowser, tabsToClose[selectTabIndex]);
    }
    splitViewToClose.close();
    Assert.ok(
      tabsToClose.every(tab => !tab.isConnected),
      "Closing a split view wrapper removes its tabs immediately"
    );
  }
});
