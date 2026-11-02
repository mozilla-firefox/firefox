/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// The revealed nav toolbox hides again when the pointer leaves the window
// without crossing the content area, e.g. onto a monitor above it.

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.fullscreen.autohide", true]],
  });

  // Collapsing bails out while a chrome input has focus.
  gBrowser.selectedBrowser.focus();

  let onToolboxHidden = TestUtils.topicObserved(
    "fullscreen-nav-toolbox",
    (subject, data) => data == "hidden"
  );
  let onFullscreen = BrowserTestUtils.waitForEvent(window, "fullscreen");
  document.getElementById("View:FullScreen").doCommand();
  await onFullscreen;
  await SimpleTest.promiseFocus(window);
  await onToolboxHidden;

  registerCleanupFunction(async () => {
    if (window.fullScreen) {
      let onExit = BrowserTestUtils.waitForEvent(window, "fullscreen");
      document.getElementById("View:FullScreen").doCommand();
      await onExit;
    }
  });
});

add_task(async function test_leave_window_from_toolbox() {
  ok(FullScreen.navToolboxHidden, "Nav toolbox starts collapsed");

  FullScreen.showNavToolbox();
  ok(!FullScreen.navToolboxHidden, "Nav toolbox is revealed");

  EventUtils.synthesizeMouse(gNavToolbox, 10, 5, { type: "mousemove" });
  EventUtils.synthesizeMouse(gNavToolbox, 20, 5, { type: "mousemove" });
  ok(
    !FullScreen.navToolboxHidden,
    "Moving the pointer within the toolbox keeps it revealed"
  );

  let onToolboxHidden = TestUtils.topicObserved(
    "fullscreen-nav-toolbox",
    (subject, data) => data == "hidden"
  );
  // "mousecancel" is the top-level window exit; "mouseout" would be a child
  // widget exit, which is treated as a move within the window.
  EventUtils.synthesizeMouse(gNavToolbox, 20, -5, { type: "mousecancel" });
  await onToolboxHidden;
  ok(
    FullScreen.navToolboxHidden,
    "Leaving the window from the toolbox collapses it"
  );
});

add_task(
  { skip_if: () => !CustomizableUI.verticalTabsEnabled },
  async function test_leave_window_from_launcher() {
    ok(FullScreen.navToolboxHidden, "Nav toolbox starts collapsed");
    await SidebarController.promiseInitialized;
    const { sidebarMain } = SidebarController;
    const y = Math.round(window.innerHeight / 2);
    const moveToEdge = () =>
      EventUtils.synthesizeMouse(document.documentElement, 0, y, {
        type: "mousemove",
      });
    const waitForToolbox = state =>
      TestUtils.topicObserved(
        "fullscreen-nav-toolbox",
        (subject, data) => data == state
      );

    let onToolboxShown = waitForToolbox("shown");
    moveToEdge();
    await onToolboxShown;
    await SidebarController.waitUntilStable();
    ok(BrowserTestUtils.isVisible(sidebarMain), "Launcher is revealed");

    EventUtils.synthesizeMouseAtCenter(sidebarMain, { type: "mousemove" });
    await new Promise(resolve => requestAnimationFrame(resolve));
    ok(
      !FullScreen.navToolboxHidden,
      "Nav toolbox stays shown over the launcher"
    );

    let onToolboxHidden = waitForToolbox("hidden");
    EventUtils.synthesizeMouse(document.documentElement, -5, y, {
      type: "mousecancel",
    });
    await onToolboxHidden;
    ok(
      FullScreen.navToolboxHidden,
      "Leaving the window past the launcher collapses the chrome"
    );

    onToolboxShown = waitForToolbox("shown");
    moveToEdge();
    await onToolboxShown;
    ok(
      !FullScreen.navToolboxHidden,
      "Returning to the launcher's edge reveals the chrome again"
    );
  }
);
