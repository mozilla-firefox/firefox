/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const MEDIA_PAGE =
  "https://example.com/browser/dom/media/mediacontrol/tests/browser/file_non_autoplay.html";

add_setup(async function () {
  registerMockReauthenticator();
  await SpecialPowers.pushPrefEnv({
    set: [[WINDOW_LOCK_FEATURE_GATE_PREF, true]],
  });
});

add_task(async function test_lock_private_windows() {
  Services.fog.testResetFOG();
  await setLockPrivateWindows(true);
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  await BrowserTestUtils.openNewForegroundTab(
    privateWin.gBrowser,
    "data:text/html,<title>SecretTitle</title>"
  );
  await BrowserTestUtils.waitForMutationCondition(
    privateWin.document.querySelector("title"),
    { childList: true, characterData: true, subtree: true },
    () => privateWin.document.title.includes("SecretTitle")
  );
  ok(true, "The content title is in the window title");

  Services.obs.notifyObservers(null, "screen_lock_notification");

  ok(WindowLock.locked, "Locked");
  ok(isWindowLocked(privateWin), "The private window is locked");
  ok(getLockDialog(privateWin).open, "The lock screen is shown");
  ok(!isWindowLocked(window), "The non-private window is not locked");
  ok(!getLockDialog(window), "No lock screen in the non-private window");
  ok(!commandsDisabled(window), "Commands are enabled in the other window");
  ok(
    !privateWin.document.title.includes("SecretTitle"),
    "The content title is hidden from the window title"
  );
  ok(commandsDisabled(privateWin), "Commands are disabled");

  EventUtils.synthesizeKey("KEY_Escape", {}, privateWin);
  await TestUtils.waitForTick();
  ok(getLockDialog(privateWin).open, "Escape does not dismiss the lock screen");

  let newPrivateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  ok(
    isWindowLocked(newPrivateWin),
    "A private window opened while locked is locked"
  );

  await unlockWith(privateWin, ReauthResult.CANCELED);
  ok(WindowLock.locked, "Canceling authentication keeps windows locked");
  ok(getLockDialog(privateWin).open, "The lock screen is still shown");

  await unlockWith(privateWin, ReauthResult.SUCCESS);
  ok(!WindowLock.locked, "Windows are unlocked");
  Assert.deepEqual(
    Glean.windowLock.locked.testGetValue().map(event => event.extra),
    [{ trigger: "screen_lock" }],
    "The lock is recorded"
  );
  Assert.deepEqual(
    Glean.windowLock.authenticate.testGetValue().map(event => event.extra),
    [{ result: "failed" }, { result: "success" }],
    "Authentication attempts are recorded"
  );
  ok(
    privateWin.document.title.includes("SecretTitle"),
    "The content title is restored in the window title"
  );
  for (let win of [privateWin, newPrivateWin]) {
    ok(!isWindowLocked(win), "The private window is unlocked");
    ok(!getLockDialog(win).open, "The lock screen is hidden");
    ok(!commandsDisabled(win), "Commands are enabled again");
  }

  await BrowserTestUtils.closeWindow(newPrivateWin);
  await BrowserTestUtils.closeWindow(privateWin);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_auth_error() {
  Services.fog.testResetFOG();
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
  Assert.deepEqual(
    Glean.windowLock.authenticate.testGetValue().map(event => event.extra),
    [
      { result: "failed" },
      { result: "failed" },
      { result: "failed" },
      { result: "success" },
    ],
    "Authentication attempts are recorded"
  );

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
    isWindowLocked(privateWin),
    "The private window is locked when the screensaver starts"
  );
  await unlockWith(privateWin, ReauthResult.SUCCESS);
  ok(!isWindowLocked(privateWin), "The private window is unlocked");
  await BrowserTestUtils.closeWindow(privateWin);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_lock_screen_titlebar() {
  Services.fog.testResetFOG();
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
  Assert.deepEqual(
    Glean.windowLock.authenticate.testGetValue().map(event => event.extra),
    [{ result: "window_closed" }],
    "Closing a locked window is recorded"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_popups_hidden_on_lock_screen() {
  await setLockPrivateWindows(true);
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  let { document } = privateWin;
  Services.obs.notifyObservers(null, "screen_lock_notification");

  let isHidden = element => {
    let style = privateWin.getComputedStyle(element);
    return style.visibility == "hidden" && style.pointerEvents == "none";
  };

  // Native menus can't be hidden or receive synthesized keys, so use a panel.
  let popup = document.createXULElement("panel");
  document.getElementById("mainPopupSet").append(popup);
  let shown = BrowserTestUtils.waitForPopupEvent(popup, "shown");
  popup.openPopup(document.getElementById("nav-bar"));
  await shown;
  ok(isHidden(popup), "Popups are hidden while locked");
  EventUtils.synthesizeKey("KEY_Escape", {}, privateWin);
  await TestUtils.waitForTick();
  is(popup.state, "open", "Popups don't handle keys while locked");

  let popover = document.createElement("div");
  popover.popover = "manual";
  document.getElementById("navigator-toolbox").append(popover);
  popover.showPopover();
  ok(isHidden(popover), "Popovers are hidden while locked");

  await unlockWith(privateWin, ReauthResult.SUCCESS);
  for (let element of [popup, popover]) {
    let style = privateWin.getComputedStyle(element);
    isnot(style.visibility, "hidden", "Popups are visible after unlocking");
    isnot(
      style.pointerEvents,
      "none",
      "Popups are interactive after unlocking"
    );
  }

  let hidden = BrowserTestUtils.waitForPopupEvent(popup, "hidden");
  EventUtils.synthesizeKey("KEY_Escape", {}, privateWin);
  await hidden;
  ok(true, "Popups handle keys again after unlocking");
  popup.remove();
  popover.remove();

  await BrowserTestUtils.closeWindow(privateWin);
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
  ok(isWindowLocked(privateWin), "The window stays locked");

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

add_task(async function test_close_locked_window_skips_page_prompts() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [WINDOW_LOCK_PREF, true],
      ["dom.require_user_interaction_for_beforeunload", false],
    ],
  });
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  let browser = privateWin.gBrowser.selectedBrowser;
  BrowserTestUtils.startLoadingURIString(
    browser,
    "data:text/html,<script>onbeforeunload = e => e.preventDefault()</script>"
  );
  await BrowserTestUtils.browserLoaded(browser);
  Services.obs.notifyObservers(null, "screen_lock_notification");
  ok(isWindowLocked(privateWin), "The private window is locked");

  let promptShown = false;
  let onPrompt = dialogWin => {
    promptShown = true;
    dialogWin.document.querySelector("dialog").getButton("accept").click();
  };
  Services.obs.addObserver(onPrompt, "common-dialog-loaded");
  let closed = BrowserTestUtils.domWindowClosed(privateWin);
  privateWin.document.getElementById("cmd_closeWindow").doCommand();
  await closed;
  Services.obs.removeObserver(onPrompt, "common-dialog-loaded");
  ok(!promptShown, "The window closes without a beforeunload prompt");
  await TestUtils.waitForCondition(
    () => !WindowLock.locked,
    "Closing the last private window ends the lock"
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_lock_suspends_pages() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [WINDOW_LOCK_PREF, true],
      ["media.mediacontrol.testingevents.enabled", true],
    ],
  });
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  let tab = await BrowserTestUtils.openNewForegroundTab(
    privateWin.gBrowser,
    MEDIA_PAGE
  );
  let controller = tab.linkedBrowser.browsingContext.mediaController;
  await SpecialPowers.spawn(tab.linkedBrowser, [], () =>
    content.document.getElementById("video").play()
  );
  await TestUtils.waitForCondition(
    () => controller.isPlaying,
    "The media is playing"
  );

  Services.obs.notifyObservers(null, "screen_lock_notification");

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    let video = content.document.getElementById("video");
    if (!video.paused) {
      await ContentTaskUtils.waitForEvent(video, "pause");
    }
  });
  ok(true, "Locking pauses the media");
  await TestUtils.waitForCondition(
    () => !controller.isActive,
    "Locking removes the media from the OS media controls"
  );
  await TestUtils.waitForCondition(
    () =>
      SpecialPowers.spawn(tab.linkedBrowser, [], () =>
        content.windowUtils.isInModalState()
      ),
    "Locking puts the page into modal state"
  );
  ok(controller.isMuted, "Locking mutes the tab");
  ok(!tab.hasAttribute("muted"), "The tab isn't marked as muted by the user");

  await unlockWith(privateWin, ReauthResult.SUCCESS);
  await TestUtils.waitForCondition(
    async () =>
      !(await SpecialPowers.spawn(tab.linkedBrowser, [], () =>
        content.windowUtils.isInModalState()
      )),
    "Unlocking makes the page leave modal state"
  );
  ok(!controller.isMuted, "Unlocking unmutes the tab");
  ok(
    await SpecialPowers.spawn(
      tab.linkedBrowser,
      [],
      () => content.document.getElementById("video").paused
    ),
    "Unlocking doesn't resume the media"
  );

  await BrowserTestUtils.closeWindow(privateWin);
  await SpecialPowers.popPrefEnv();
});

/**
 * Check that a prompt overlapping with a locked window doesn't re-enable
 * commands that were disabled beforehand, such as Clear Recent History in
 * Private Windows.
 */
add_task(async function test_prompt_while_locked_keeps_disabled_commands() {
  await setLockPrivateWindows(true);
  await SpecialPowers.pushPrefEnv({
    set: [["browser.sessionstore.max_tabs_undo", 0]],
  });
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  for (let i = 0; i < 2; i++) {
    await BrowserTestUtils.openNewForegroundTab(
      privateWin.gBrowser,
      "https://example.com/"
    );
  }
  let isDisabled = id =>
    privateWin.document.getElementById(id).hasAttribute("disabled");
  ok(isDisabled("Tools:Sanitize"), "Clearing history is disabled");

  let dialogPromise = BrowserTestUtils.promiseAlertDialogOpen();
  let promptReturnedPromise = new Promise(resolve => {
    privateWin.setTimeout(() => {
      privateWin.gBrowser.removeAllTabsBut(privateWin.gBrowser.selectedTab);
      resolve();
    }, 0);
  });
  let dialogWin = await dialogPromise;

  Services.obs.notifyObservers(null, "screen_lock_notification");
  ok(WindowLock.locked, "Locked");
  await unlockWith(privateWin, ReauthResult.SUCCESS);
  ok(!WindowLock.locked, "Unlocked");
  ok(
    isDisabled("cmd_newNavigatorTab"),
    "Commands stay disabled while the prompt is open"
  );

  dialogWin.document.querySelector("dialog").cancelDialog();
  await promptReturnedPromise;
  is(privateWin.gBrowser.tabs.length, 3, "Canceling the prompt keeps the tabs");
  ok(
    !isDisabled("cmd_newNavigatorTab"),
    "Commands are enabled once the prompt is closed"
  );
  ok(isDisabled("Tools:Sanitize"), "Clearing history stays disabled");

  await BrowserTestUtils.closeWindow(privateWin);
  await SpecialPowers.popPrefEnv();
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
  ok(
    !isWindowLocked(window) && !isWindowLocked(privateWin),
    "No window is locked"
  );

  await BrowserTestUtils.closeWindow(privateWin);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_lock_with_shortcut() {
  Services.fog.testResetFOG();
  await setLockPrivateWindows(true);
  let normalKey = document.getElementById("key_lockWindows");
  ok(
    normalKey.hasAttribute("disabled"),
    "The shortcut is disabled without private windows"
  );

  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  let key = privateWin.document.getElementById("key_lockWindows");
  ok(!key.hasAttribute("disabled"), "The shortcut is enabled");
  ok(
    !normalKey.hasAttribute("disabled"),
    "The shortcut is enabled in the non-private window"
  );

  let minimized = BrowserTestUtils.waitForEvent(privateWin, "sizemodechange");
  EventUtils.synthesizeKey("L", { accelKey: true, shiftKey: true }, privateWin);

  ok(WindowLock.locked, "Locked");
  ok(isWindowLocked(privateWin), "The private window is locked");
  ok(!isWindowLocked(window), "The non-private window is not locked");
  await minimized;
  is(
    privateWin.windowState,
    privateWin.STATE_MINIMIZED,
    "The private window is minimized"
  );
  isnot(
    window.windowState,
    window.STATE_MINIMIZED,
    "The non-private window is not minimized"
  );
  let restored = BrowserTestUtils.waitForEvent(privateWin, "sizemodechange");
  privateWin.restore();
  await restored;
  Assert.deepEqual(
    Glean.windowLock.locked.testGetValue().map(event => event.extra),
    [{ trigger: "shortcut" }],
    "The lock is recorded"
  );

  await unlockWith(privateWin, ReauthResult.SUCCESS);
  ok(!WindowLock.locked, "Windows are unlocked");

  await setLockPrivateWindows(false);
  ok(key.hasAttribute("disabled"), "The shortcut is disabled");
  EventUtils.synthesizeKey("L", { accelKey: true, shiftKey: true }, privateWin);
  ok(!WindowLock.locked, "The disabled shortcut doesn't lock");
  await SpecialPowers.popPrefEnv();

  await BrowserTestUtils.closeWindow(privateWin);
  await BrowserTestUtils.waitForMutationCondition(
    normalKey,
    { attributeFilter: ["disabled"] },
    () => normalKey.hasAttribute("disabled")
  );
  ok(true, "The shortcut is disabled once the last private window is closed");
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_shortcut_in_about_keyboard() {
  let hasShortcut = () =>
    BrowserTestUtils.withNewTab("about:keyboard", tab =>
      SpecialPowers.spawn(tab, [], async () => {
        await ContentTaskUtils.waitForMutationCondition(
          content.document,
          { childList: true, subtree: true },
          () => content.document.querySelector(".key")
        );
        return !!content.document.querySelector(
          '.key[data-id="key_lockWindows"]'
        );
      })
    );

  await setLockPrivateWindows(true);
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  ok(await hasShortcut(), "The shortcut is listed in about:keyboard");
  await BrowserTestUtils.closeWindow(privateWin);
  await SpecialPowers.popPrefEnv();

  await setLockPrivateWindows(false);
  ok(!(await hasShortcut()), "The disabled shortcut isn't listed");
  await SpecialPowers.popPrefEnv();
});
