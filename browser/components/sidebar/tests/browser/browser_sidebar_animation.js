/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const ANIMATION_ENABLED_PREF = "sidebar.animation.enabled";
const EXPAND_ON_HOVER_DURATION_PREF =
  "sidebar.animation.expand-on-hover.duration-ms";
const EXPAND_ON_HOVER_DELAY_PREF =
  "sidebar.animation.expand-on-hover.delay-duration-ms";

/**
 * Press a keyboard shortcut. Unlike a click, a key press isn't lost to hit
 * testing while a transition is running.
 *
 * @param {string} id
 *   The id of the <key> element.
 * @param {ChromeWindow} [win]
 */
function pressShortcut(id, win = window) {
  const keyEl = win.document.getElementById(id);
  const modifiers = keyEl.getAttribute("modifiers").split(",");
  EventUtils.synthesizeKey(
    keyEl.getAttribute("key"),
    {
      accelKey: modifiers.includes("accel"),
      altKey: modifiers.includes("alt"),
      ctrlKey: modifiers.includes("control"),
      shiftKey: modifiers.includes("shift"),
    },
    win
  );
}

function toggleWithShortcut(win = window) {
  // Runs the same handler as clicking the sidebar button, which isn't
  // guaranteed to be present in the toolbar.
  pressShortcut("toggleSidebarKb", win);
}

/**
 * Read a motion token from the root element.
 *
 * @param {string} name
 * @returns {string}
 */
function getToken(name) {
  return getComputedStyle(document.documentElement)
    .getPropertyValue(name)
    .trim();
}

/**
 * Read a duration token, in milliseconds.
 *
 * @param {string} name
 * @returns {number}
 */
function getDurationToken(name) {
  return parseFloat(getToken(name));
}

/**
 * Run an action that starts a sidebar view transition and capture what it
 * animates. The animations are paused as soon as they exist so that assertions
 * have no timing dependency, then the transition is skipped.
 *
 * @param {Function} action
 * @param {ChromeWindow} [win]
 * @param {Function} [inspect]
 *   Called while the animations are paused, before the transition is skipped.
 * @returns {Promise<{types: string[], animations: Animation[], inspected: *}>}
 */
async function captureTransition(action, win = window, inspect = () => {}) {
  const { SidebarController: controller } = win;
  const result = action();
  await TestUtils.waitForCondition(
    () => controller._activeViewTransition,
    "Waiting for a view transition to start"
  );
  const transition = controller._activeViewTransition;
  await transition.ready;
  const animations = win.document
    .getAnimations()
    .filter(a => a.effect.pseudoElement?.startsWith("::view-transition"));
  animations.forEach(a => a.pause());
  const types = [...transition.types];
  const inspected = inspect();

  transition.skipTransition();
  await transition.finished;
  await result;
  await controller.waitUntilStable();
  ok(
    !controller._activeViewTransition,
    "The finished transition is no longer tracked"
  );
  return { types, animations, inspected };
}

function getGroupZIndex(name) {
  return getComputedStyle(
    document.documentElement,
    `::view-transition-group(${name})`
  ).zIndex;
}

/**
 * Assert that a sidebar group stacks beneath the content area, which matters
 * most when the sidebar element is only in the new state, as such groups would
 * otherwise come last and paint on top.
 *
 * @param {string} name
 * @param {string} description
 */
function assertBeneathContentArea(name, description) {
  is(
    getGroupZIndex(name),
    "auto",
    `${description}: ${name} isn't raised above the content area`
  );
  is(
    getGroupZIndex("tabbrowser-tabbox"),
    "1",
    `${description}: the content area is raised above ${name}`
  );
}

/**
 * Assert that a transition leaves the content area and panel live, rather than
 * swapping in their snapshots.
 *
 * @param {Animation[]} animations
 * @param {string} description
 */
function assertContentAreaNotCaptured(animations, description) {
  for (const name of ["tabbrowser-tabbox", "sidebar-box"]) {
    ok(
      !animations.some(a => a.effect.pseudoElement.endsWith(`(${name})`)),
      `${description}: ${name} isn't captured`
    );
  }
}

/**
 * Assert that every group animation in a transition uses a given timing.
 *
 * @param {Animation[]} animations
 * @param {number} duration
 * @param {string} easing
 * @param {string} description
 */
function assertGroupTiming(animations, duration, easing, description) {
  const groups = animations.filter(a =>
    a.effect.pseudoElement.startsWith("::view-transition-group")
  );
  Assert.greater(groups.length, 0, `${description}: groups are animating`);
  for (const animation of groups) {
    const { pseudoElement } = animation.effect;
    is(
      animation.effect.getComputedTiming().duration,
      duration,
      `${description}: ${pseudoElement} has the expected duration`
    );
    is(
      animation.effect.getKeyframes()[0].easing,
      easing,
      `${description}: ${pseudoElement} has the expected easing`
    );
  }
}

/**
 * Put the launcher back in its default visible state without animating, so that
 * each task starts from the same place and toggling is reliably a hide.
 */
async function ensureLauncherVisibleWithoutAnimating() {
  await SpecialPowers.pushPrefEnv({
    set: [[ANIMATION_ENABLED_PREF, false]],
  });
  await SidebarTestUtils.ensureLauncherVisible(window);
  await SpecialPowers.popPrefEnv();
}

add_setup(async () => {
  await SidebarTestUtils.waitForInitialized(window);
  // Windows CI machines have prefers-reduced-motion set, which suppresses the
  // animations under test. Opt back in rather than depend on the host setting.
  gReduceMotionOverride = false;
  await ensureLauncherVisibleWithoutAnimating();
});

registerCleanupFunction(() => {
  gReduceMotionOverride = undefined;
});

add_task(async function test_toolbar_button_toggle_uses_launcher_tokens() {
  const emphasized = getToken("--sidebar-motion-easing");

  const hide = await captureTransition(
    () => toggleWithShortcut(),
    window,
    () => assertBeneathContentArea("sidebar-launcher", "Launcher hide")
  );
  ok(
    SidebarController.sidebarContainer.hidden,
    "Launcher was hidden by the toolbar button"
  );
  ok(hide.types.includes("launcher-exit"), "Hiding is a launcher exit");
  ok(hide.types.includes("launcher-hide"), "Hiding is a launcher hide");
  const covered = hide.animations.find(
    a => a.effect.pseudoElement == "::view-transition-old(sidebar-launcher)"
  );
  is(
    covered?.animationName,
    "sidebar-panel-covered",
    "The hidden launcher is removed before the transition ends"
  );
  is(
    covered?.effect.getComputedTiming().fill,
    "both",
    "The hidden launcher doesn't reappear as the transition ends"
  );
  assertGroupTiming(
    hide.animations,
    getDurationToken("--sidebar-motion-duration-exit"),
    emphasized,
    "Launcher exit"
  );

  const show = await captureTransition(
    () => toggleWithShortcut(),
    window,
    () => assertBeneathContentArea("sidebar-launcher", "Launcher show")
  );
  ok(
    !SidebarController.sidebarContainer.hidden,
    "Launcher was shown by the toolbar button"
  );
  ok(show.types.includes("launcher-enter"), "Showing is a launcher enter");
  ok(show.types.includes("launcher-show"), "Showing is a launcher show");
  assertGroupTiming(
    show.animations,
    getDurationToken("--sidebar-motion-duration-enter"),
    emphasized,
    "Launcher enter"
  );
});

add_task(async function test_expand_and_collapse_show_only_new_launcher() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [VERTICAL_TABS_PREF, true],
      [SIDEBAR_VISIBILITY_PREF, "always-show"],
    ],
  });
  await SidebarTestUtils.waitForTabstripOrientation(window, "vertical");
  SidebarController._state.launcherExpanded = true;
  await SidebarController.waitUntilStable();

  const snapshotStyle = which =>
    getComputedStyle(
      document.documentElement,
      `::view-transition-${which}(sidebar-launcher)`
    );
  const inspect = () => ({
    oldDisplay: snapshotStyle("old").display,
    newLeft: snapshotStyle("new").left,
    zIndex: getGroupZIndex("sidebar-launcher"),
  });
  const assertOnlyNewLauncher = ({ inspected }, description) => {
    is(
      inspected.oldDisplay,
      "none",
      `${description}: the old launcher isn't shown`
    );
    is(
      inspected.newLeft,
      "0px",
      `${description}: the new launcher is pinned to the window edge`
    );
    is(
      inspected.zIndex,
      "auto",
      `${description}: the launcher is beneath the content area`
    );
  };

  const collapse = await captureTransition(
    () => toggleWithShortcut(),
    window,
    inspect
  );
  ok(!SidebarController._state.launcherExpanded, "The launcher collapsed");
  ok(collapse.types.includes("collapse"), "Collapsing is a collapse");
  assertOnlyNewLauncher(collapse, "Collapse");

  const expand = await captureTransition(
    () => toggleWithShortcut(),
    window,
    inspect
  );
  ok(SidebarController._state.launcherExpanded, "The launcher expanded");
  ok(expand.types.includes("expand"), "Expanding is an expand");
  assertOnlyNewLauncher(expand, "Expand");

  await SpecialPowers.popPrefEnv();
  await ensureLauncherVisibleWithoutAnimating();
});

add_task(async function test_expand_on_hover_duration_default() {
  is(
    Services.prefs
      .getDefaultBranch("")
      .getIntPref(EXPAND_ON_HOVER_DURATION_PREF),
    getDurationToken("--sidebar-motion-duration-enter"),
    "Expand on hover defaults to the same duration as expanding by click"
  );
});

add_task(async function test_panel_transitions_use_panel_tokens() {
  const emphasized = getToken("--sidebar-motion-easing");

  const open = await captureTransition(
    () => pressShortcut("key_gotoHistory"),
    window,
    () => assertBeneathContentArea("sidebar-box", "Panel open")
  );
  ok(SidebarController.isOpen, "The panel was opened");
  ok(open.types.includes("panel-open"), "Opening is a panel open");
  assertGroupTiming(
    open.animations,
    getDurationToken("--sidebar-panel-motion-duration-enter"),
    emphasized,
    "Panel open"
  );

  const switched = await captureTransition(() =>
    pressShortcut("viewBookmarksSidebarKb")
  );
  is(
    SidebarController.currentID,
    "viewBookmarksSidebar",
    "Switched to another panel"
  );
  ok(switched.types.includes("panel-switch"), "Switching is a panel switch");
  const contentFade = getDurationToken(
    "--sidebar-panel-content-motion-duration-fade"
  );
  const standard = getToken("--sidebar-panel-content-motion-easing-fade");
  assertGroupTiming(switched.animations, contentFade, standard, "Panel switch");
  const fades = switched.animations.filter(a =>
    /^::view-transition-(old|new)\(sidebar-box\)$/.test(a.effect.pseudoElement)
  );
  Assert.greater(fades.length, 0, "The panel content cross-fades");
  for (const animation of fades) {
    is(
      animation.effect.getComputedTiming().duration,
      contentFade,
      `${animation.effect.pseudoElement} fades for the content fade duration`
    );
  }

  const close = await captureTransition(() =>
    pressShortcut("viewBookmarksSidebarKb")
  );
  ok(!SidebarController.isOpen, "The panel was closed");
  ok(close.types.includes("panel-close"), "Closing is a panel close");
  assertGroupTiming(
    close.animations,
    getDurationToken("--sidebar-panel-motion-duration-exit"),
    emphasized,
    "Panel close"
  );

  await ensureLauncherVisibleWithoutAnimating();
});

add_task(async function test_panel_transitions_show_only_new_launcher() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [VERTICAL_TABS_PREF, true],
      [SIDEBAR_VISIBILITY_PREF, "always-show"],
    ],
  });
  await SidebarTestUtils.waitForTabstripOrientation(window, "vertical");
  SidebarController._state.launcherExpanded = true;
  await SidebarController.waitUntilStable();

  const inspect = () =>
    getComputedStyle(
      document.documentElement,
      "::view-transition-old(sidebar-launcher)"
    ).display;
  for (const [description, key] of [
    ["Panel open", "key_gotoHistory"],
    ["Panel switch", "viewBookmarksSidebarKb"],
    ["Panel close", "viewBookmarksSidebarKb"],
  ]) {
    const { inspected } = await captureTransition(
      () => pressShortcut(key),
      window,
      inspect
    );
    is(
      inspected,
      "none",
      `${description}: the old launcher isn't shown over the new one`
    );
  }
  ok(!SidebarController.isOpen, "The panel was closed");

  await SpecialPowers.popPrefEnv();
  await ensureLauncherVisibleWithoutAnimating();
});

/**
 * Assert which content corner type, if any, a transition has, and that the
 * content area's corner animates to match.
 *
 * @param {{types: string[], animations: Animation[]}} transition
 * @param {"content-corner-round"|"content-corner-square"|null} expected
 * @param {string} description
 */
function assertContentCorner({ types, animations }, expected, description) {
  for (const type of ["content-corner-round", "content-corner-square"]) {
    is(
      types.includes(type),
      type == expected,
      `${description}: ${type} is ${type == expected ? "" : "not "}a type`
    );
  }
  const corner = animations.find(
    a =>
      a.effect.pseudoElement ==
      "::view-transition-image-pair(tabbrowser-tabbox)"
  );
  is(
    corner?.animationName,
    expected?.replace("content-corner-", "sidebar-content-corner-start-"),
    `${description}: the content area's corner animates as expected`
  );
}

add_task(async function test_content_corner_transitions() {
  const hide = await captureTransition(() => toggleWithShortcut());
  ok(SidebarController.sidebarContainer.hidden, "The launcher was hidden");
  assertContentCorner(hide, "content-corner-square", "Launcher hide");

  const show = await captureTransition(() => toggleWithShortcut());
  ok(!SidebarController.sidebarContainer.hidden, "The launcher was shown");
  assertContentCorner(show, "content-corner-round", "Launcher show");

  const open = await captureTransition(() => pressShortcut("key_gotoHistory"));
  ok(SidebarController.isOpen, "The panel was opened");
  assertContentCorner(open, null, "Panel open next to the launcher");
  await captureTransition(() => pressShortcut("key_gotoHistory"));
  ok(!SidebarController.isOpen, "The panel was closed");

  const originalTab = gBrowser.selectedTab;
  const tabs = await Promise.all(
    [1, 2].map(i =>
      BrowserTestUtils.openNewForegroundTab(gBrowser, `data:text/plain,${i}`)
    )
  );
  gBrowser.addTabSplitView(tabs);
  await BrowserTestUtils.switchTab(gBrowser, tabs[0]);
  ok(
    gBrowser.selectedBrowser.closest(".split-view-panel"),
    "The selected tab is in a split view"
  );

  const splitHide = await captureTransition(() => toggleWithShortcut());
  ok(SidebarController.sidebarContainer.hidden, "The launcher was hidden");
  assertContentCorner(splitHide, null, "Launcher hide in a split view");

  const splitShow = await captureTransition(() => toggleWithShortcut());
  ok(!SidebarController.sidebarContainer.hidden, "The launcher was shown");
  assertContentCorner(splitShow, null, "Launcher show in a split view");

  await BrowserTestUtils.switchTab(gBrowser, originalTab);
  tabs.forEach(tab => BrowserTestUtils.removeTab(tab));
  Services.prefs.clearUserPref("browser.tabs.splitview.hasUsed");
  await ensureLauncherVisibleWithoutAnimating();
});

add_task(async function test_retoggle_skips_ongoing_transition() {
  const spy = sinon.spy(document, "startViewTransition");
  toggleWithShortcut();
  toggleWithShortcut();
  await TestUtils.waitForCondition(
    () => spy.callCount == 2,
    "Waiting for the second transition to start"
  );
  spy.restore();
  const [first, second] = spy.returnValues;
  is(
    SidebarController._activeViewTransition,
    second,
    "The latest transition is tracked"
  );

  await Assert.rejects(
    first.ready,
    e => e.name == "AbortError",
    "The interrupted transition was skipped"
  );
  await first.finished;

  second.skipTransition();
  await second.finished;
  await SidebarController.waitUntilStable();
  ok(
    !SidebarController._activeViewTransition,
    "No transition is tracked once both have finished"
  );

  await ensureLauncherVisibleWithoutAnimating();
});

add_task(async function test_expand_on_hover_duration_pref() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [VERTICAL_TABS_PREF, true],
      [SIDEBAR_VISIBILITY_PREF, "expand-on-hover"],
      [EXPAND_ON_HOVER_DURATION_PREF, 1234],
      [EXPAND_ON_HOVER_DELAY_PREF, 0],
    ],
  });
  // Expand on hover leaves state behind in the window it was enabled in, so
  // keep it out of the main window.
  const win = await BrowserTestUtils.openNewBrowserWindow();
  try {
    await SidebarTestUtils.waitForInitialized(win);
    win.gReduceMotionOverride = false;
    const { SidebarController: controller } = win;
    await TestUtils.waitForCondition(
      () =>
        win.document.documentElement.hasAttribute("sidebar-expand-on-hover"),
      "Waiting for expand on hover to be enabled"
    );
    await controller.waitUntilStable();
    const emphasized = getToken("--sidebar-motion-easing");

    win.windowUtils.disableNonTestMouseEvents(true);
    const expand = await captureTransition(
      () =>
        EventUtils.synthesizeMouse(
          controller.sidebarContainer,
          1,
          150,
          { type: "mousemove" },
          win
        ),
      win
    );
    ok(controller._state.launcherExpanded, "The launcher expanded");
    ok(
      expand.types.includes("launcher-enter"),
      "Expanding is a launcher enter"
    );
    assertGroupTiming(expand.animations, 1234, emphasized, "Hover expand");
    assertContentAreaNotCaptured(expand.animations, "Hover expand");

    await SpecialPowers.pushPrefEnv({
      set: [[EXPAND_ON_HOVER_DURATION_PREF, 4321]],
    });
    const collapse = await captureTransition(
      () =>
        EventUtils.synthesizeMouseAtCenter(
          controller.contentArea,
          { type: "mousemove" },
          win
        ),
      win
    );
    ok(!controller._state.launcherExpanded, "The launcher collapsed");
    ok(
      collapse.types.includes("launcher-exit"),
      "Collapsing is a launcher exit"
    );
    assertGroupTiming(
      collapse.animations,
      4321,
      emphasized,
      "Hover collapse after the pref changed"
    );
    assertContentAreaNotCaptured(collapse.animations, "Hover collapse");
    await SpecialPowers.popPrefEnv();
  } finally {
    win.windowUtils.disableNonTestMouseEvents(false);
    await BrowserTestUtils.closeWindow(win);
    // Resetting the prefs also updates the main window's sidebar. Don't animate
    // that, or the pending transition's update lands in the next test.
    gReduceMotionOverride = true;
    // Leaving vertical tabs saves an expand-on-hover choice and restores it the
    // next time they're enabled, which would leak into later tests.
    Services.prefs.setStringPref(SIDEBAR_VISIBILITY_PREF, "always-show");
    await SpecialPowers.popPrefEnv();
    gReduceMotionOverride = false;
  }
});

add_task(async function test_no_transition_when_pref_disabled() {
  const spy = sinon.spy(document, "startViewTransition");
  await SpecialPowers.pushPrefEnv({
    set: [[ANIMATION_ENABLED_PREF, false]],
  });

  const wasHidden = SidebarController.sidebarContainer.hidden;
  toggleWithShortcut();
  await SidebarController.waitUntilStable();
  await waitForElementHidden(SidebarController.sidebarContainer, !wasHidden);

  ok(!spy.called, "No transition runs when the animation pref is disabled");

  await SpecialPowers.popPrefEnv();
  spy.restore();
  await ensureLauncherVisibleWithoutAnimating();
});

add_task(async function test_no_transition_when_reduce_motion() {
  const spy = sinon.spy(document, "startViewTransition");
  gReduceMotionOverride = true;

  const wasHidden = SidebarController.sidebarContainer.hidden;
  toggleWithShortcut();
  await SidebarController.waitUntilStable();
  await waitForElementHidden(SidebarController.sidebarContainer, !wasHidden);

  ok(!spy.called, "No transition runs when motion is reduced");

  gReduceMotionOverride = false;
  spy.restore();
  await ensureLauncherVisibleWithoutAnimating();
});

add_task(async function test_position_change_animates() {
  const root = document.documentElement;
  ok(
    !root.hasAttribute("sidebar-positionend"),
    "The sidebar starts on the start side"
  );

  await captureTransition(() => SidebarController.reversePosition());
  ok(root.hasAttribute("sidebar-positionend"), "Moving the sidebar animates");

  Services.prefs.clearUserPref(SidebarController.POSITION_START_PREF);
  await SidebarController.waitUntilStable();
});
