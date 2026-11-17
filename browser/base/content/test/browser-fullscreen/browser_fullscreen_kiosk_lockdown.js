/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// A kiosk window offers no way to bring the chrome back, so from lockdown
// level 1 it collapses the nav toolbox whatever browser.fullscreen.autohide
// says. These tests run with that pref off, which is its default on macOS.

const ORIGINAL_BROWSER_HANDLER = Object.getOwnPropertyDescriptor(
  window,
  "BrowserHandler"
);

// gBrowserInit.kioskLockdown reads the command line handler, and a test window
// is never started with --kiosk.
function setKiosk(isKiosk) {
  if (isKiosk) {
    Object.defineProperty(window, "BrowserHandler", {
      configurable: true,
      value: { kiosk: true },
    });
  } else {
    Object.defineProperty(window, "BrowserHandler", ORIGINAL_BROWSER_HANDLER);
  }
}

async function enterFullscreen() {
  // Collapsing bails out while a chrome input has focus, which the urlbar may
  // still have from earlier in the test run.
  gBrowser.selectedBrowser.focus();
  let onFullscreen = BrowserTestUtils.waitForEvent(window, "fullscreen");
  document.getElementById("View:FullScreen").doCommand();
  await onFullscreen;
  await SimpleTest.promiseFocus(window);
  // FullScreen.toggle() decides synchronously on the same event, but it is not
  // guaranteed to run before the listener above.
  await TestUtils.waitForTick();
}

// Kiosk mode has to be off before calling this: BrowserCommands.fullScreen()
// keeps a kiosk window in fullscreen, so the command would never fire the
// "fullscreen" event we wait for.
async function exitFullscreen() {
  if (!window.fullScreen) {
    return;
  }
  let onFullscreen = BrowserTestUtils.waitForEvent(window, "fullscreen");
  document.getElementById("View:FullScreen").doCommand();
  await onFullscreen;
}

add_setup(async function () {
  // The value kiosk mode has to override, and already the default on macOS.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.fullscreen.autohide", false]],
  });
});

registerCleanupFunction(async () => {
  setKiosk(false);
  await exitFullscreen();
});

// Both locked down levels hide the toolbars; they differ only in what the
// widget does with the OS chrome, which is not observable from here.
add_task(async function test_locked_down_kiosk_collapses_toolbox() {
  for (let level of [1, 2]) {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.kiosk.lockdown", level]],
    });
    setKiosk(true);
    Assert.equal(
      gBrowserInit.kioskLockdown,
      level,
      `Window reports lockdown level ${level}`
    );

    let onToolboxHidden = TestUtils.topicObserved(
      "fullscreen-nav-toolbox",
      (subject, data) => data == "hidden"
    );
    await enterFullscreen();
    await onToolboxHidden;
    ok(
      FullScreen.navToolboxHidden,
      `Nav toolbox is collapsed at lockdown level ${level} even though ` +
        `browser.fullscreen.autohide is off`
    );

    setKiosk(false);
    await exitFullscreen();
    await SpecialPowers.popPrefEnv();
  }
});

// Level 0 turns the lockdown off and leaves the pref in charge.
add_task(async function test_lockdown_level_zero_respects_the_pref() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.kiosk.lockdown", 0]],
  });
  setKiosk(true);

  await enterFullscreen();
  ok(
    !FullScreen.navToolboxHidden,
    "Nav toolbox stays visible at lockdown level 0 when autohide is off"
  );

  setKiosk(false);
  await exitFullscreen();
  await SpecialPowers.popPrefEnv();
});

// The pref defaults to the full lockdown, so a window that is not in kiosk mode
// has to honor browser.fullscreen.autohide.
add_task(async function test_regular_window_is_unaffected() {
  Assert.equal(
    gBrowserInit.kioskLockdown,
    0,
    "A window that is not in kiosk mode reports no lockdown"
  );

  await enterFullscreen();
  ok(
    !FullScreen.navToolboxHidden,
    "Nav toolbox stays visible outside kiosk mode when autohide is off"
  );

  await exitFullscreen();
});
