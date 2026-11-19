/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockRegistrar } = ChromeUtils.importESModule(
  "resource://testing-common/MockRegistrar.sys.mjs"
);
const { WindowLock } = ChromeUtils.importESModule(
  "moz-src:///browser/modules/WindowLock.sys.mjs"
);

const PREF = "browser.privatebrowsing.lockOnScreenLock.enabled";

function getLockDialog(win) {
  return win.document.getElementById("window-lock-dialog");
}

function isLocked(win) {
  return win.document.documentElement.hasAttribute("windowlocked");
}

function authErrorShown(win) {
  return !getLockDialog(win).querySelector(".window-lock-error").hidden;
}

function commandsDisabled(win) {
  return win.document
    .getElementById("cmd_newNavigatorTab")
    .hasAttribute("disabled");
}

// Outcomes of the mocked OS reauthentication prompt.
const ReauthResult = Object.freeze({
  SUCCESS: "success",
  CANCELED: "canceled",
  ERROR: "error",
});
let gReauthResult = ReauthResult.CANCELED;
let gOnReauth = null;

const mockReauthenticator = {
  QueryInterface: ChromeUtils.generateQI(["nsIOSReauthenticator"]),
  async asyncReauthenticateUser() {
    gOnReauth?.();
    if (gReauthResult == ReauthResult.ERROR) {
      throw new Components.Exception(
        "Simulated OS reauthentication error",
        Cr.NS_ERROR_FAILURE
      );
    }
    return [gReauthResult == ReauthResult.SUCCESS ? 1 : 0, 0];
  },
};

async function unlockWith(win, result) {
  gReauthResult = result;
  let reauthPromise = new Promise(resolve => (gOnReauth = resolve));
  getLockDialog(win).querySelector(".window-lock-button").click();
  await reauthPromise;
  gOnReauth = null;
  await TestUtils.waitForTick();
}

async function setLockPrivateWindows(enabled) {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, enabled]] });
}

add_setup(async function () {
  let cid = MockRegistrar.register(
    "@mozilla.org/security/osreauthenticator;1",
    mockReauthenticator
  );
  registerCleanupFunction(() => {
    MockRegistrar.unregister(cid);
  });
});

add_task(async function test_lock_private_windows() {
  await setLockPrivateWindows(true);
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });

  Services.obs.notifyObservers(null, "screen_lock_notification");

  ok(WindowLock.locked, "Locked");
  ok(isLocked(privateWin), "The private window is locked");
  ok(getLockDialog(privateWin).open, "The lock screen is shown");
  ok(!isLocked(window), "The non-private window is not locked");
  ok(!getLockDialog(window), "No lock screen in the non-private window");
  ok(!commandsDisabled(window), "Commands are enabled in the other window");
  ok(commandsDisabled(privateWin), "Commands are disabled");

  EventUtils.synthesizeKey("KEY_Escape", {}, privateWin);
  await TestUtils.waitForTick();
  ok(getLockDialog(privateWin).open, "Escape does not dismiss the lock screen");

  let newPrivateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  ok(isLocked(newPrivateWin), "A private window opened while locked is locked");

  await unlockWith(privateWin, ReauthResult.CANCELED);
  ok(WindowLock.locked, "Canceling authentication keeps windows locked");
  ok(getLockDialog(privateWin).open, "The lock screen is still shown");

  await unlockWith(privateWin, ReauthResult.SUCCESS);
  ok(!WindowLock.locked, "Windows are unlocked");
  for (let win of [privateWin, newPrivateWin]) {
    ok(!isLocked(win), "The private window is unlocked");
    ok(!getLockDialog(win).open, "The lock screen is hidden");
    ok(!commandsDisabled(win), "Commands are enabled again");
  }

  await BrowserTestUtils.closeWindow(newPrivateWin);
  await BrowserTestUtils.closeWindow(privateWin);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_auth_error() {
  await setLockPrivateWindows(true);
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  Services.obs.notifyObservers(null, "screen_lock_notification");
  ok(!authErrorShown(privateWin), "No error is shown initially");

  await unlockWith(privateWin, ReauthResult.ERROR);
  ok(WindowLock.locked, "An authentication error keeps windows locked");
  ok(authErrorShown(privateWin), "The error is shown on the lock screen");

  let newPrivateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  ok(authErrorShown(newPrivateWin), "The error is shown in new windows");

  await unlockWith(privateWin, ReauthResult.CANCELED);
  ok(WindowLock.locked, "Canceling authentication keeps windows locked");
  for (let win of [privateWin, newPrivateWin]) {
    ok(!authErrorShown(win), "The error is hidden after another attempt");
  }

  // Show the error again to check that a successful attempt still unlocks
  // while an error is shown.
  await unlockWith(privateWin, ReauthResult.ERROR);
  await unlockWith(privateWin, ReauthResult.SUCCESS);
  ok(!WindowLock.locked, "Windows are unlocked");

  await BrowserTestUtils.closeWindow(newPrivateWin);
  await BrowserTestUtils.closeWindow(privateWin);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_no_private_lock_without_private_windows() {
  await setLockPrivateWindows(true);
  Services.obs.notifyObservers(null, "sleep_notification");
  Services.obs.notifyObservers(null, "wake_notification");
  ok(!WindowLock.locked, "Nothing gets locked without private windows");
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_lock_on_screensaver_start() {
  await setLockPrivateWindows(true);
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  Services.obs.notifyObservers(null, "screensaver_start_notification");
  ok(
    isLocked(privateWin),
    "The private window is locked when the screensaver starts"
  );
  await unlockWith(privateWin, ReauthResult.SUCCESS);
  ok(!isLocked(privateWin), "The private window is unlocked");
  await BrowserTestUtils.closeWindow(privateWin);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_lock_screen_titlebar() {
  await setLockPrivateWindows(true);
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  Services.obs.notifyObservers(null, "screen_lock_notification");

  let titlebar = getLockDialog(privateWin).querySelector(
    ".window-lock-titlebar"
  );
  ok(titlebar, "The lock screen has a titlebar");
  let buttonbox = titlebar.querySelector(".titlebar-buttonbox-container");
  ok(buttonbox, "The lock screen has caption buttons");
  is(
    buttonbox.checkVisibility(),
    AppConstants.platform == "win",
    "Caption buttons are only drawn by the window on Windows"
  );
  for (let id of [
    "cmd_minimizeWindow",
    "cmd_maximizeWindow",
    "cmd_restoreWindow",
    "cmd_closeWindow",
  ]) {
    ok(
      !privateWin.document.getElementById(id).hasAttribute("disabled"),
      `${id} stays enabled`
    );
  }
  ok(commandsDisabled(privateWin), "Other commands are disabled");

  let closed = BrowserTestUtils.domWindowClosed(privateWin);
  titlebar.querySelector(".titlebar-close").doCommand();
  await closed;
  ok(privateWin.closed, "The close button closes the locked window");
  await TestUtils.waitForCondition(
    () => !WindowLock.locked,
    "Closing the last private window ends the lock"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_close_locked_window_with_warning() {
  await setLockPrivateWindows(true);
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  Services.obs.notifyObservers(null, "screen_lock_notification");
  let closeCommand = privateWin.document.getElementById("cmd_closeWindow");

  // Warn like downloads do when closing the last private window.
  let onExiting = subject => {
    subject.QueryInterface(Ci.nsISupportsPRBool).data =
      !Services.prompt.confirm(privateWin, "title", "message");
  };
  Services.obs.addObserver(onExiting, "last-pb-context-exiting");

  let prompt = BrowserTestUtils.promiseAlertDialogOpen(null, undefined, {
    callback: async dialogWin => {
      ok(privateWin.gDialogBox.isOpen, "The warning is shown");
      ok(getLockDialog(privateWin).open, "The lock screen stays open below");
      dialogWin.document.querySelector("dialog").getButton("cancel").click();
    },
  });
  closeCommand.doCommand();
  await prompt;
  ok(!privateWin.closed, "Canceling the warning keeps the window open");
  ok(isLocked(privateWin), "The window stays locked");

  prompt = BrowserTestUtils.promiseAlertDialogOpen("accept");
  let closed = BrowserTestUtils.domWindowClosed(privateWin);
  closeCommand.doCommand();
  await prompt;
  await closed;
  Services.obs.removeObserver(onExiting, "last-pb-context-exiting");
  ok(privateWin.closed, "Accepting the warning closes the window");
  await TestUtils.waitForCondition(
    () => !WindowLock.locked,
    "Closing the last private window ends the lock"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_no_lock_when_disabled() {
  await setLockPrivateWindows(false);
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });

  Services.obs.notifyObservers(null, "sleep_notification");
  Services.obs.notifyObservers(null, "wake_notification");
  ok(!WindowLock.locked, "Nothing gets locked when disabled");
  ok(!isLocked(window) && !isLocked(privateWin), "No window is locked");

  await BrowserTestUtils.closeWindow(privateWin);
  await SpecialPowers.popPrefEnv();
});
