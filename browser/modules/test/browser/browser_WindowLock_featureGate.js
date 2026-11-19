/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(function () {
  registerMockReauthenticator();
});

add_task(async function test_no_lock_when_feature_disabled() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [WINDOW_LOCK_FEATURE_GATE_PREF, false],
      [WINDOW_LOCK_PREF, true],
    ],
  });
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  ok(
    document.getElementById("key_lockWindows").hasAttribute("disabled"),
    "The shortcut is disabled"
  );

  Services.obs.notifyObservers(null, "screen_lock_notification");
  WindowLock.lockFromShortcut();
  ok(!WindowLock.locked, "Nothing gets locked when the feature is disabled");
  ok(
    !isWindowLocked(window) && !isWindowLocked(privateWin),
    "No window is locked"
  );

  await BrowserTestUtils.closeWindow(privateWin);
  await SpecialPowers.popPrefEnv();
});
