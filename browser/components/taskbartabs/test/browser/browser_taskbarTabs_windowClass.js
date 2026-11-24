/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  sinon: "resource://testing-common/Sinon.sys.mjs",
  TaskbarTabs: "moz-src:///browser/components/taskbartabs/TaskbarTabs.sys.mjs",
  TaskbarTabsPin:
    "moz-src:///browser/components/taskbartabs/TaskbarTabsPin.sys.mjs",
  TaskbarTabsUtils:
    "moz-src:///browser/components/taskbartabs/TaskbarTabsUtils.sys.mjs",
});

const kShortcut = "org.example.webapp-test.desktop";
const kWindowClass = "org.example.webapp-test";
const kGnomeShellDelayPref = "browser.taskbarTabs.gnomeShellDelayMs";
const kOriginalAppInfo = Services.appinfo;

let gPinResult;
sinon.stub(TaskbarTabsPin, "pinTaskbarTab").callsFake(() => gPinResult.promise);
sinon.stub(TaskbarTabsPin, "unpinTaskbarTab");

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({ set: [[kGnomeShellDelayPref, 1]] });

  registerCleanupFunction(() => {
    Services.appinfo = kOriginalAppInfo;
    sinon.restore();
  });
});

function setIsGnomeShell(aIsGnomeShell) {
  Services.appinfo = Object.create(kOriginalAppInfo, {
    isGnomeDesktopEnvironment: { value: aIsGnomeShell },
  });
}

/**
 * Adds aTask once as if on GNOME Shell and once as if on another desktop.
 * Both go through the same steps; only GNOME Shell waits before applying the
 * window class, and the delay is 1 ms in these tests.
 *
 * @param {Function} aTask - The task to add.
 */
function addTaskOnEachDesktop(aTask) {
  for (let isGnomeShell of [true, false]) {
    add_task(async function () {
      info(`${aTask.name}, isGnomeShell=${isGnomeShell}`);
      setIsGnomeShell(isGnomeShell);
      await aTask();
    });
  }
}

async function moveNewTabIntoTaskbarTab() {
  let tab = BrowserTestUtils.addTab(gBrowser, "https://example.com/");
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  return TaskbarTabs.moveTabIntoTaskbarTab(tab);
}

function getWindowClass(aWindow) {
  return aWindow.document.documentElement.getAttribute("windowclass");
}

async function cleanUp(aTaskbarTab, aWindows) {
  for (let win of aWindows) {
    await BrowserTestUtils.closeWindow(win);
  }
  await TaskbarTabs.removeTaskbarTab(aTaskbarTab.id);
}

addTaskOnEachDesktop(async function test_windowClassDeferred() {
  gPinResult = Promise.withResolvers();

  let { window: win1, taskbarTab } = await moveNewTabIntoTaskbarTab();
  Assert.equal(
    getWindowClass(win1),
    null,
    "The first window starts with Firefox's window class"
  );

  let win2 = await TaskbarTabs.openWindow(taskbarTab);
  Assert.equal(
    getWindowClass(win2),
    null,
    "A window opened while pinning starts with Firefox's window class"
  );

  gPinResult.resolve({ shortcut: kShortcut, rejected: false });
  await TestUtils.waitForCondition(
    () =>
      getWindowClass(win1) === kWindowClass &&
      getWindowClass(win2) === kWindowClass,
    "Waiting for the windows to get the web app's window class"
  );

  let win3 = await TaskbarTabs.openWindow(taskbarTab);
  Assert.equal(
    getWindowClass(win3),
    kWindowClass,
    "A window opened afterwards starts with the web app's window class"
  );

  await cleanUp(taskbarTab, [win1, win2, win3]);
});

addTaskOnEachDesktop(async function test_windowClassDeferredWithoutTab() {
  gPinResult = Promise.withResolvers();

  let { taskbarTab } = await TaskbarTabs.findOrCreateTaskbarTab(
    Services.io.newURI("https://example.com/"),
    0
  );
  let win = await TaskbarTabs.openWindow(taskbarTab);
  Assert.equal(
    getWindowClass(win),
    null,
    "A window opened while pinning starts with Firefox's window class"
  );

  gPinResult.resolve({ shortcut: kShortcut, rejected: false });
  await TestUtils.waitForCondition(
    () => getWindowClass(win) === kWindowClass,
    "Waiting for the window to get the web app's window class"
  );

  await cleanUp(taskbarTab, [win]);
});

addTaskOnEachDesktop(
  async function test_windowClassWithWindowClosedWhilePinning() {
    gPinResult = Promise.withResolvers();

    let { window: win1, taskbarTab } = await moveNewTabIntoTaskbarTab();
    let win2 = await TaskbarTabs.openWindow(taskbarTab);
    await BrowserTestUtils.closeWindow(win1);

    gPinResult.resolve({ shortcut: kShortcut, rejected: false });
    await TestUtils.waitForCondition(
      () => getWindowClass(win2) === kWindowClass,
      "Waiting for the remaining window to get the web app's window class"
    );
    await BrowserTestUtils.closeWindow(win2);

    let win3 = await TaskbarTabs.openWindow(taskbarTab);
    Assert.equal(
      getWindowClass(win3),
      kWindowClass,
      "A window opened after closing the others starts with the web app's window class"
    );

    await cleanUp(taskbarTab, [win3]);
  }
);

addTaskOnEachDesktop(
  async function test_windowClassKeptIfRemovedWhilePinning() {
    gPinResult = Promise.withResolvers();

    let { taskbarTab: removedTaskbarTab } =
      await TaskbarTabs.findOrCreateTaskbarTab(
        Services.io.newURI("https://example.com/"),
        0
      );
    let removedWin = await TaskbarTabs.openWindow(removedTaskbarTab);

    // Its window class is applied after the removed web app's would be, so once
    // its window has the web app's window class, the removed web app's windows
    // would have it too.
    let { taskbarTab: keptTaskbarTab } =
      await TaskbarTabs.findOrCreateTaskbarTab(
        Services.io.newURI("https://example.org/"),
        0
      );
    let keptWin = await TaskbarTabs.openWindow(keptTaskbarTab);

    await TaskbarTabs.removeTaskbarTab(removedTaskbarTab.id);
    gPinResult.resolve({ shortcut: kShortcut, rejected: false });
    await TestUtils.waitForCondition(
      () => getWindowClass(keptWin) === kWindowClass,
      "Waiting for the remaining web app's window to get its window class"
    );
    Assert.equal(
      getWindowClass(removedWin),
      null,
      "The removed web app's window keeps Firefox's window class"
    );

    await BrowserTestUtils.closeWindow(removedWin);
    await cleanUp(keptTaskbarTab, [keptWin]);
  }
);

addTaskOnEachDesktop(async function test_windowClassKeptIfPinFails() {
  gPinResult = Promise.withResolvers();

  let { window: win1, taskbarTab } = await moveNewTabIntoTaskbarTab();
  gPinResult.resolve({ shortcut: null, rejected: false });
  await gPinResult.promise;
  await TestUtils.waitForTick();

  let win2 = await TaskbarTabs.openWindow(taskbarTab);
  Assert.equal(
    getWindowClass(win1),
    null,
    "The first window keeps Firefox's window class"
  );
  Assert.equal(
    getWindowClass(win2),
    null,
    "A window opened afterwards also uses Firefox's window class"
  );

  await cleanUp(taskbarTab, [win1, win2]);
});

addTaskOnEachDesktop(
  async function test_windowClassAppliedToAllWindowsWhenRepinned() {
    gPinResult = Promise.withResolvers();

    let { window: win1, taskbarTab } = await moveNewTabIntoTaskbarTab();
    gPinResult.resolve({ shortcut: null, rejected: false });

    let { window: win2, taskbarTab: taskbarTab2 } =
      await moveNewTabIntoTaskbarTab();
    Assert.equal(
      taskbarTab2.id,
      taskbarTab.id,
      "The second tab was moved into the same web app"
    );
    let win3 = await TaskbarTabs.openWindow(taskbarTab);
    let windows = [win1, win2, win3];
    Assert.ok(
      windows.every(win => getWindowClass(win) === null),
      "All the windows use Firefox's window class without a desktop entry"
    );

    gPinResult = Promise.withResolvers();
    let { pin } = await TaskbarTabs.findOrCreateTaskbarTab(
      Services.io.newURI("https://example.com/"),
      0,
      { ensurePinned: true }
    );
    gPinResult.resolve({ shortcut: kShortcut, rejected: false });
    await pin;
    await TestUtils.waitForCondition(
      () => windows.every(win => getWindowClass(win) === kWindowClass),
      "Waiting for all the windows to get the web app's window class"
    );

    await cleanUp(taskbarTab, windows);
  }
);

add_task(async function test_windowClassWaitsOnGnome() {
  setIsGnomeShell(true);
  await SpecialPowers.pushPrefEnv({ set: [[kGnomeShellDelayPref, 1000000]] });
  gPinResult = Promise.withResolvers();

  let { window: win, taskbarTab } = await moveNewTabIntoTaskbarTab();
  gPinResult.resolve({ shortcut: kShortcut, rejected: false });
  await gPinResult.promise;
  await TestUtils.waitForTick();
  Assert.equal(
    getWindowClass(win),
    null,
    "On GNOME Shell, the window keeps Firefox's window class until the delay passes"
  );

  // Removing the web app makes the pending timer do nothing.
  await cleanUp(taskbarTab, [win]);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_windowClassDoesNotWaitOutsideGnome() {
  setIsGnomeShell(false);
  await SpecialPowers.pushPrefEnv({ set: [[kGnomeShellDelayPref, 1000000]] });
  gPinResult = Promise.withResolvers();

  let { window: win, taskbarTab } = await moveNewTabIntoTaskbarTab();
  Assert.equal(
    getWindowClass(win),
    null,
    "The first window starts with Firefox's window class"
  );

  gPinResult.resolve({ shortcut: kShortcut, rejected: false });
  await TestUtils.waitForCondition(
    () => getWindowClass(win) === kWindowClass,
    "Outside GNOME Shell, the window gets the web app's window class without waiting"
  );

  await cleanUp(taskbarTab, [win]);
  await SpecialPowers.popPrefEnv();
});
