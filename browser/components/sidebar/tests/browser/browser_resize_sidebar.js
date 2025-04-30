/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SIDEBAR_VISIBILITY_PREF, "always-show"],
      [POSITION_SETTING_PREF, true],
      [VERTICAL_TABS_PREF, true],
    ],
  });
  await SidebarController.initializeUIState({
    launcherExpanded: false,
    launcherVisible: true,
  });
  await waitForTabstripOrientation("vertical");
});

registerCleanupFunction(async () => {
  await SpecialPowers.popPrefEnv();
});

async function dragLauncher(deltaX, shouldExpand) {
  AccessibilityUtils.setEnv({ mustHaveAccessibleRule: false });

  // Let the launcher splitter stabilize before attempting a drag-and-drop.
  await waitForRepaint();

  info(`Drag the launcher by ${deltaX} px.`);
  const { sidebarMain, _launcherSplitter: splitter } = SidebarController;
  EventUtils.synthesizeMouseAtCenter(splitter, { type: "mousedown" });
  await mouseMoveInChunks(splitter, deltaX, 10);
  EventUtils.synthesizeMouse(splitter, 0, 0, { type: "mouseup" });

  info(`The sidebar should be ${shouldExpand ? "expanded" : "collapsed"}.`);
  await BrowserTestUtils.waitForMutationCondition(
    sidebarMain,
    { attributeFilter: ["expanded"] },
    () => sidebarMain.hasAttribute("expanded") == shouldExpand
  );

  AccessibilityUtils.resetEnv();
}

async function mouseMoveInChunks(el, deltaX, numberOfChunks) {
  let chunkIndex = 0;
  const chunkSize = deltaX / numberOfChunks;
  const finished = Promise.withResolvers();

  function synthesizeMouseMove() {
    // mousemove by a single chunk. Queue up the next chunk if necessary.
    EventUtils.synthesizeMouse(el, chunkSize, 0, { type: "mousemove" });
    if (++chunkIndex === numberOfChunks) {
      finished.resolve();
    } else {
      requestAnimationFrame(synthesizeMouseMove);
    }
  }

  await waitForRepaint();
  requestAnimationFrame(synthesizeMouseMove);
  await finished.promise;
}

function getLauncherWidth({ SidebarController } = window) {
  return SidebarController.sidebarContainer.style.width;
}

add_task(async function test_drag_expand_and_collapse() {
  await dragLauncher(200, true);
  ok(getLauncherWidth(), "Launcher width set.");

  await dragLauncher(-200, false);
  ok(!getLauncherWidth(), "Launcher width unset.");
});

add_task(async function test_drag_show_and_hide() {
  await SpecialPowers.pushPrefEnv({
    set: [[SIDEBAR_VISIBILITY_PREF, "hide-sidebar"]],
  });
  await SidebarController.initializeUIState({
    launcherExpanded: true,
    launcherVisible: true,
  });

  await dragLauncher(-200, false);
  ok(SidebarController.sidebarContainer.hidden, "Sidebar is hidden.");

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_custom_width_persists() {
  await SidebarController.initializeUIState({
    launcherExpanded: false,
    launcherVisible: true,
  });
  await dragLauncher(200, true);
  const customWidth = getLauncherWidth();

  info("Collapse the sidebar using toolbar button.");
  EventUtils.synthesizeMouseAtCenter(SidebarController.toolbarButton, {});
  await SidebarController.sidebarMain.updateComplete;

  info("Expand the sidebar using toolbar button.");
  EventUtils.synthesizeMouseAtCenter(SidebarController.toolbarButton, {});
  await SidebarController.sidebarMain.updateComplete;
  Assert.equal(
    customWidth,
    getLauncherWidth(),
    "Sidebar expands to the previously stored custom width."
  );

  info("Open a new window.");
  const win = await BrowserTestUtils.openNewBrowserWindow();
  await win.SidebarController.promiseInitialized;
  Assert.equal(
    customWidth,
    getLauncherWidth(win),
    "Sidebar expands to the custom width set from the original window."
  );
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_drag_show_and_hide_for_horizontal_tabs() {
  await SidebarController.initializeUIState({
    launcherExpanded: false,
    launcherVisible: true,
  });

  await dragLauncher(-200, false);
  ok(!SidebarController.sidebarContainer.hidden, "Sidebar is not hidden.");
  ok(!SidebarController.sidebarContainer.expanded, "Sidebar is not expanded.");
});

add_task(async function test_resize_after_toggling_revamp() {
  await SidebarController.initializeUIState({
    launcherExpanded: true,
  });

  info("Disable and then re-enable sidebar and vertical tabs.");
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.revamp", false],
      [VERTICAL_TABS_PREF, false],
    ],
  });
  await waitForTabstripOrientation("horizontal");
  await SpecialPowers.popPrefEnv();
  await waitForTabstripOrientation("vertical");

  info("Resize the vertical tab strip.");
  const originalWidth = getLauncherWidth();
  await dragLauncher(200, true);
  const newWidth = getLauncherWidth();
  Assert.greater(
    parseInt(newWidth),
    parseInt(originalWidth),
    "Vertical tab strip was resized."
  );
});
