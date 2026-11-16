/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests browser.js's lastWindowClose messaging trigger:
// - it only fires when closing the last browser window, not any window close
// - it cancels the close attempt (WindowIsClosing returns false) while a
//   message is pending, and re-requests the close (window.close()) once the
//   message resolves
// - a second close attempt while the first message is still pending doesn't
//   dispatch the trigger again
//
// These go through BrowserCommands.tryToCloseWindow() rather than calling
// WindowIsClosing() directly as tryToCloseWindow is the one that actually calls
// window.close() when WindowIsClosing() says it's OK to. Calling
// WindowIsClosing() directly doesn't generate an actual close.
//
// This also stands in for closing via the window's close (X) button, since
// that runs through the same WindowIsClosing() call. There's no way to
// simulate an actual OS-level close-button click from a test anyway.

const { ASRouter } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouter.sys.mjs"
);
const { TaskbarTabsUtils } = ChromeUtils.importESModule(
  "moz-src:///browser/components/taskbartabs/TaskbarTabsUtils.sys.mjs"
);
ChromeUtils.defineESModuleGetters(this, {
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

add_setup(async function () {
  await ASRouter.waitForInitialized;
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.tabs.warnOnClose", false],
      ["browser.warnOnQuitShortcut", false],
      // The tests below simulate "win is the last window" by stubbing
      // browserWindows() (see stubAsLastWindow), which only fakes that for
      // our own check in WindowIsClosing. BrowserGlue's own
      // browser-lastwindow-close-requested handler uses
      // BrowserWindowTracker.orderedWindows instead, which still sees the
      // real test window and would compute its own (real, un-mocked) tab
      // count across every actually-open window. Disable its dialog
      // entirely so that doesn't interfere with these tests.
      ["browser.warnOnQuit", false],
    ],
  });
});

/**
 * Makes `win` look like the only open browser window to WindowIsClosing's
 * own isLastWindow check, without actually closing the real test window
 * (which the harness needs to keep running).
 */
function stubAsLastWindow(sandbox, win) {
  sandbox.stub(win, "browserWindows").returns([win][Symbol.iterator]());
}

/**
 * WindowIsClosing skips dispatching the trigger entirely unless
 * ASRouter.hasMessageForTrigger("lastWindowClose") says a message could
 * possibly show, which isn't true in this test environment. Tests below
 * that stub sendTriggerMessage to exercise dispatch need this too.
 */
function stubHasMessageForTrigger(sandbox) {
  sandbox.stub(ASRouter, "hasMessageForTrigger").returns(true);
}

add_task(async function test_not_last_window_does_not_dispatch() {
  // Deliberately does NOT stub browserWindows(): the real test window is
  // still open, so win here is not the last window.
  let sandbox = sinon.createSandbox();
  let win = await BrowserTestUtils.openNewBrowserWindow();
  let sendTriggerStub = sandbox.stub(ASRouter, "sendTriggerMessage");

  let closedPromise = BrowserTestUtils.windowClosed(win);
  win.BrowserCommands.tryToCloseWindow();
  await closedPromise;

  Assert.ok(
    sendTriggerStub.notCalled,
    "lastWindowClose trigger is not dispatched when another window remains open"
  );

  sandbox.restore();
});

add_task(async function test_popup_windows_are_included() {
  let sandbox = sinon.createSandbox();
  let win = await BrowserTestUtils.openNewBrowserWindow();
  stubAsLastWindow(sandbox, win);
  stubHasMessageForTrigger(sandbox);
  sandbox.stub(win.toolbar, "visible").get(() => false);
  let sendTriggerStub = sandbox
    .stub(ASRouter, "sendTriggerMessage")
    .resolves({ message: { id: "test" }, closedPromise: Promise.resolve() });

  let closedPromise = BrowserTestUtils.windowClosed(win);
  win.BrowserCommands.tryToCloseWindow();

  Assert.ok(
    sendTriggerStub.calledOnce,
    "lastWindowClose trigger is dispatched for a popup window"
  );

  await closedPromise;
  sandbox.restore();
});

add_task(async function test_web_app_windows_are_excluded() {
  let sandbox = sinon.createSandbox();
  let win = await BrowserTestUtils.openNewBrowserWindow();
  stubAsLastWindow(sandbox, win);
  stubHasMessageForTrigger(sandbox);
  sandbox.stub(TaskbarTabsUtils, "isTaskbarTabWindow").returns(true);
  let sendTriggerStub = sandbox.stub(ASRouter, "sendTriggerMessage");

  let closedPromise = BrowserTestUtils.windowClosed(win);
  win.BrowserCommands.tryToCloseWindow();
  await closedPromise;

  Assert.ok(
    sendTriggerStub.notCalled,
    "lastWindowClose trigger is not dispatched for a web app (Taskbar Tabs) window"
  );

  sandbox.restore();
});

add_task(async function test_cancels_dispatches_and_recloses() {
  let sandbox = sinon.createSandbox();
  let win = await BrowserTestUtils.openNewBrowserWindow();
  stubAsLastWindow(sandbox, win);
  stubHasMessageForTrigger(sandbox);

  let resolveMessage;
  let closedPromise = new Promise(resolve => {
    resolveMessage = resolve;
  });
  let sendTriggerStub = sandbox
    .stub(ASRouter, "sendTriggerMessage")
    .callsFake(({ id }) => {
      Assert.equal(id, "lastWindowClose", "Dispatched the right trigger");
      return Promise.resolve({ message: { id: "test" }, closedPromise });
    });

  win.BrowserCommands.tryToCloseWindow();
  Assert.ok(!win.closed, "Close is cancelled while the message is pending");
  Assert.equal(
    sendTriggerStub.callCount,
    1,
    "Trigger dispatched exactly once for the initial attempt"
  );

  // Resolving the message should make WindowIsClosing's .finally() call
  // window.close() on the same window, this time actually closing it (since
  // window.skipNextCanClose was already set true on the first attempt).
  let windowClosedPromise = BrowserTestUtils.windowClosed(win);
  resolveMessage();
  await windowClosedPromise;
  Assert.equal(
    sendTriggerStub.callCount,
    1,
    "The re-requested close did not re-dispatch the trigger"
  );

  sandbox.restore();
});

add_task(async function test_second_close_attempt_does_not_redispatch() {
  Services.fog.testResetFOG();
  let sandbox = sinon.createSandbox();
  let win = await BrowserTestUtils.openNewBrowserWindow();
  stubAsLastWindow(sandbox, win);
  stubHasMessageForTrigger(sandbox);

  // Never resolves as this test addresses what happens while a message is stuck
  // pending.
  let sendTriggerStub = sandbox
    .stub(ASRouter, "sendTriggerMessage")
    .callsFake(({ id }) => {
      Assert.equal(id, "lastWindowClose", "Dispatched the right trigger");
      return Promise.resolve({
        message: { id: "test" },
        closedPromise: new Promise(() => {}),
      });
    });

  win.BrowserCommands.tryToCloseWindow();
  Assert.ok(
    !win.closed,
    "First close attempt is cancelled while the message is pending"
  );
  Assert.equal(sendTriggerStub.callCount, 1, "Trigger dispatched once");
  Assert.equal(
    Glean.messagingSystem.lastWindowCloseTriggerBypassed.testGetValue(),
    null,
    "Bypass metric not recorded yet"
  );

  // A second attempt while the guard is still set skips the trigger-dispatch
  // block entirely, so WindowIsClosing() returns true and
  // tryToCloseWindow's own window.close() call actually closes the window
  // this time, rather than trapping the user indefinitely behind an
  // unresolved message.
  let closedPromise = BrowserTestUtils.windowClosed(win);
  win.BrowserCommands.tryToCloseWindow();
  await closedPromise;

  Assert.equal(
    sendTriggerStub.callCount,
    1,
    "The second attempt did not dispatch the trigger again"
  );
  Assert.equal(
    Glean.messagingSystem.lastWindowCloseTriggerBypassed.testGetValue(),
    1,
    "Bypass metric recorded exactly once"
  );

  sandbox.restore();
});

add_task(async function test_skips_trigger_when_tabs_warning_would_show() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.warnOnClose", true]],
  });
  let sandbox = sinon.createSandbox();
  let win = await BrowserTestUtils.openNewBrowserWindow();
  stubAsLastWindow(sandbox, win);

  // Skip closeWindow()'s own platform-dependent tab-close warning so this test
  // only exercises thre shouldWarnForTabs gate below.
  sandbox.stub(win, "closeWindow").returns(true);
  let sendTriggerStub = sandbox.stub(ASRouter, "sendTriggerMessage");
  await BrowserTestUtils.openNewForegroundTab(win.gBrowser, "about:blank");

  let closedPromise = BrowserTestUtils.windowClosed(win);
  win.BrowserCommands.tryToCloseWindow();
  await closedPromise;

  Assert.ok(
    sendTriggerStub.notCalled,
    "lastWindowClose trigger is not dispatched when the tabs-closing warning applies"
  );

  sandbox.restore();
  await SpecialPowers.popPrefEnv();
});

add_task(
  async function test_pinned_tabs_excluded_from_last_window_nonmac_check() {
    // This exercises the non-macOS branch of shouldWarnForTabs's own
    // last-window formula, which relies on the real platform. Nothing to
    // test on macOS, where that branch can't be true.
    if (AppConstants.platform == "macosx") {
      todo(false, "last-window-nonmac branch does not apply on macOS");
      return;
    }

    await SpecialPowers.pushPrefEnv({
      set: [["browser.tabs.warnOnClose", true]],
    });
    let sandbox = sinon.createSandbox();
    let win = await BrowserTestUtils.openNewBrowserWindow();
    stubAsLastWindow(sandbox, win);
    stubHasMessageForTrigger(sandbox);

    // 1 pinned and 1 regular tab would make openTabs.length 2 (and would read
    // as "warn" under the non-last-window formula), but the pagecount
    // BrowserGlue's own last-window dialog actually uses (visibleTabs -
    // pinnedTabCount) is only 1, so no warning applies here.
    win.gBrowser.pinTab(win.gBrowser.selectedTab);
    await BrowserTestUtils.openNewForegroundTab(win.gBrowser, "about:blank");

    let sendTriggerStub = sandbox
      .stub(ASRouter, "sendTriggerMessage")
      .resolves({ message: { id: "test" }, closedPromise: Promise.resolve() });

    let closedPromise = BrowserTestUtils.windowClosed(win);
    win.BrowserCommands.tryToCloseWindow();
    Assert.ok(
      !win.closed,
      "Close is cancelled while the message is dispatched"
    );
    Assert.ok(
      sendTriggerStub.calledOnce,
      "lastWindowClose trigger is still dispatched: the pinned tab doesn't count toward the last-window check"
    );

    // The message resolves immediately, so the reissued close should follow
    // shortly on its own.
    await closedPromise;

    sandbox.restore();
    await SpecialPowers.popPrefEnv();
  }
);
