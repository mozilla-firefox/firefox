/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  AppConstants: "resource://gre/modules/AppConstants.sys.mjs",
  ShellService: "moz-src:///browser/components/shell/ShellService.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  TaskbarTabs: "moz-src:///browser/components/taskbartabs/TaskbarTabs.sys.mjs",
  TaskbarTabsUtils:
    "moz-src:///browser/components/taskbartabs/TaskbarTabsUtils.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetters(this, {
  WinTaskbar: ["@mozilla.org/windows-taskbar;1", Ci.nsIWinTaskbar],
});

const registry = createInMemoryRegistry();

const url1 = Services.io.newURI("https://example.com");
const userContextId1 = 0;
const taskbarTab1 = createTaskbarTab(registry, url1, userContextId1);
const id1 = taskbarTab1.id;

const url2 = Services.io.newURI("https://subdomain.example.com");
const userContextId2 = 1;
const taskbarTab2 = createTaskbarTab(registry, url2, userContextId2);
const id2 = taskbarTab2.id;

add_task(async function test_count_for_id() {
  const wm = new TaskbarTabsWindowManager();
  let testWindowCount = (aCount1, aCount2) => {
    is(
      wm.getCountForId(id1),
      aCount1,
      `${aCount1} Taskbar Tab window(s) should exist for id ${id1}`
    );
    is(
      wm.getCountForId(id2),
      aCount2,
      `${aCount2} Taskbar Tab window(s) should exist for id ${id2}`
    );
  };

  testWindowCount(0, 0);

  let windowPromise = BrowserTestUtils.waitForNewWindow();
  await wm.openWindow(taskbarTab1);
  let win1_to_eject = await windowPromise;

  testWindowCount(1, 0);

  windowPromise = BrowserTestUtils.waitForNewWindow();
  await wm.openWindow(taskbarTab1);
  let win2 = await windowPromise;

  testWindowCount(2, 0);

  windowPromise = BrowserTestUtils.waitForNewWindow();
  await wm.openWindow(taskbarTab2);
  let win3_to_eject = await windowPromise;

  testWindowCount(2, 1);

  let tab1_adopted = await BrowserTestUtils.addTab(window.gBrowser, url1.spec);
  windowPromise = BrowserTestUtils.waitForNewWindow();
  await wm.replaceTabWithWindow(taskbarTab1, tab1_adopted);
  let win4 = await windowPromise;

  testWindowCount(3, 1);

  let tabOpenPromise = BrowserTestUtils.waitForEvent(
    window.gBrowser.tabContainer,
    "TabOpen"
  );
  await wm.ejectWindow(win1_to_eject);
  let tab2 = (await tabOpenPromise).target;

  testWindowCount(2, 1);

  tabOpenPromise = BrowserTestUtils.waitForEvent(
    window.gBrowser.tabContainer,
    "TabOpen"
  );
  await wm.ejectWindow(win3_to_eject);
  let tab3 = (await tabOpenPromise).target;

  testWindowCount(2, 0);

  BrowserTestUtils.removeTab(tab2);
  BrowserTestUtils.removeTab(tab3);
  await Promise.all([
    BrowserTestUtils.closeWindow(win2),
    BrowserTestUtils.closeWindow(win4),
  ]);
});

add_task(async function test_user_context_id() {
  function checkUserContextId(win, taskbarTab) {
    is(
      win.gBrowser.selectedTab.userContextId,
      taskbarTab.userContextId,
      "Tab's userContextId should match that for the Taskbar Tab."
    );
  }

  const wm = new TaskbarTabsWindowManager();

  let testForTaskbarTab = async taskbarTab => {
    let windowPromise = BrowserTestUtils.waitForNewWindow();
    await wm.openWindow(taskbarTab);
    let win = await windowPromise;
    checkUserContextId(win, taskbarTab);

    const tabOpenPromise = BrowserTestUtils.waitForEvent(
      window.gBrowser.tabContainer,
      "TabOpen"
    );
    await wm.ejectWindow(win);
    let tab = (await tabOpenPromise).target;
    win = tab.documentGlobal;
    checkUserContextId(win, taskbarTab);

    windowPromise = BrowserTestUtils.waitForNewWindow();
    await wm.replaceTabWithWindow(taskbarTab, tab);
    win = await windowPromise;
    checkUserContextId(win, taskbarTab);

    await BrowserTestUtils.closeWindow(win);
  };

  await testForTaskbarTab(taskbarTab1);
  await testForTaskbarTab(taskbarTab2);
});

add_task(async function test_eject_window_selected_tab() {
  const wm = new TaskbarTabsWindowManager();

  let windowPromise = BrowserTestUtils.waitForNewWindow();
  await wm.openWindow(taskbarTab1);
  let win = await windowPromise;

  const tabOpenPromise = BrowserTestUtils.waitForEvent(
    window.gBrowser.tabContainer,
    "TabOpen"
  );
  await wm.ejectWindow(win);
  let tab = (await tabOpenPromise).target;

  is(
    tab,
    window.gBrowser.selectedTab,
    "The ejected Taskbar Tab should be the selected tab."
  );

  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_windows_for_id() {
  const wm = new TaskbarTabsWindowManager();

  Assert.deepEqual(
    wm.getWindowsForId(id1),
    [],
    "No window should be returned when none is open."
  );

  let win1 = await wm.openWindow(taskbarTab1);
  let win2 = await wm.openWindow(taskbarTab1);
  let win3 = await wm.openWindow(taskbarTab2);

  await SimpleTest.promiseFocus(win1);
  Assert.deepEqual(
    wm.getWindowsForId(id1),
    [win1, win2],
    "The windows of the Taskbar Tab should be ordered by most recent use."
  );
  Assert.deepEqual(
    wm.getWindowsForId(id2),
    [win3],
    "Only windows of the requested Taskbar Tab should be returned."
  );

  await BrowserTestUtils.closeWindow(win2);
  Assert.deepEqual(
    wm.getWindowsForId(id1),
    [win1],
    "A closed window should not be returned."
  );

  const tabOpenPromise = BrowserTestUtils.waitForEvent(
    window.gBrowser.tabContainer,
    "TabOpen"
  );
  const win1Closed = BrowserTestUtils.windowClosed(win1);
  await wm.ejectWindow(win1);
  let tab = (await tabOpenPromise).target;
  Assert.deepEqual(
    wm.getWindowsForId(id1),
    [],
    "An ejected window should not be returned."
  );

  await win1Closed;
  await BrowserTestUtils.removeTab(tab);
  await BrowserTestUtils.closeWindow(win3);
});

add_task(async function test_open_window_raises_existing_windows() {
  const wm = new TaskbarTabsWindowManager();

  let winMinimized = await wm.openWindow(taskbarTab1);
  let winRecent = await wm.openWindow(taskbarTab1);
  let winOther = await wm.openWindow(taskbarTab2);

  let sizeModeChange = BrowserTestUtils.waitForEvent(
    winMinimized,
    "sizemodechange"
  );
  winMinimized.minimize();
  await sizeModeChange;

  await SimpleTest.promiseFocus(winOther);
  await SimpleTest.promiseFocus(window);

  let winNew = await wm.openWindow(taskbarTab1);
  await TestUtils.waitForCondition(
    () => winMinimized.windowState != winMinimized.STATE_MINIMIZED,
    "Waiting for the minimized window to be restored."
  );

  is(
    Services.focus.activeWindow,
    winNew,
    "The new window should be the active window."
  );
  let ordered = BrowserWindowTracker.getOrderedWindows();
  Assert.deepEqual(
    ordered.slice(0, 3),
    [winNew, winRecent, winMinimized],
    "The existing windows should be raised in order, below the new window."
  );
  Assert.greater(
    ordered.indexOf(window),
    ordered.indexOf(winMinimized),
    "The windows of the Taskbar Tab should be above the browser window."
  );
  Assert.greater(
    ordered.indexOf(winOther),
    ordered.indexOf(winMinimized),
    "Windows of other Taskbar Tabs should not be raised."
  );

  await Promise.all([
    BrowserTestUtils.closeWindow(winMinimized),
    BrowserTestUtils.closeWindow(winRecent),
    BrowserTestUtils.closeWindow(winOther),
    BrowserTestUtils.closeWindow(winNew),
  ]);
}).skip(AppConstants.platform !== "win"); // Existing windows are only raised on Windows.

add_task(async function test_window_class() {
  const wm = new TaskbarTabsWindowManager();

  let winOpen = await wm.openWindow(taskbarTab1);
  is(
    TaskbarTabsUtils.getTaskbarTabIdFromWindow(winOpen),
    taskbarTab1.id,
    "The window's `taskbartab` attribute should match the Taskbar Tab ID when opened."
  );
  checkWindowAssociatedApp(taskbarTab1, winOpen);
  await BrowserTestUtils.closeWindow(winOpen);

  let tab1_adopted = await BrowserTestUtils.addTab(window.gBrowser, url1.spec);
  let winReplace = await wm.replaceTabWithWindow(taskbarTab1, tab1_adopted);
  is(
    TaskbarTabsUtils.getTaskbarTabIdFromWindow(winReplace),
    taskbarTab1.id,
    "The window's `taskbartab` attribute should match the Taskbar Tab ID when a tab was replaced with a Taskbar Tab window."
  );
  checkWindowAssociatedApp(taskbarTab1, winReplace);
  await BrowserTestUtils.closeWindow(winReplace);
});

/**
 * Asserts that aWindow has the expected AUMID (on Windows) or 'windowclass' (on
 * Linux) for aTaskbarTab.
 *
 * @param {TaskbarTab} aTaskbarTab - The expected taskbar tab for the window.
 * @param {DOMWindow} aWindow - The window itself.
 */
function checkWindowAssociatedApp(aTaskbarTab, aWindow) {
  if (AppConstants.platform === "win") {
    if (TaskbarTabsUtils.isMSIX()) {
      // The format of this doesn't seem to be documented anywhere; I got it
      // through a small custom utility using the undocumented IPinnedList3 API
      // (the one we use for pinning on Windows 10). It's possible that Windows
      // could change it, in which case the Taskbar Tab window wouldn't line up
      // with its taskbar entry.
      is(
        WinTaskbar.getGroupIdForWindow(aWindow),
        `${Services.sysinfo.getProperty("winPackageFamilyName")}!App:taskbartab-${aTaskbarTab.id}`,
        "The window AUMID should match the ID likely assigned by Windows."
      );
    } else {
      is(
        WinTaskbar.getGroupIdForWindow(aWindow),
        taskbarTab1.id,
        "The window AUMID should match the Taskbar Tab ID when opened."
      );
    }
  } else if (AppConstants.platform === "linux") {
    is(
      aWindow.document.documentElement.getAttribute("windowclass"),
      TaskbarTabsUtils._determineNewDesktopEntryName(aTaskbarTab.id),
      "The window class should match the current GLib app name."
    );
  }
}

add_task(async function testLinuxWindowClassRestores() {
  const wm = new TaskbarTabsWindowManager();

  let altURI = Services.io.newURI("https://example.org/"); // vs. example.com
  let taskbarTab = createTaskbarTab(registry, altURI, 0);

  async function captureWindowClass(aShortcutRelativePath) {
    registry.patchTaskbarTab(taskbarTab, {
      shortcutRelativePath: aShortcutRelativePath,
    });
    let win = await wm.openWindow(taskbarTab);
    let windowClass = win.document.documentElement.getAttribute("windowclass");
    await BrowserTestUtils.closeWindow(win);

    let tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      altURI.spec
    );
    win = await wm.replaceTabWithWindow(taskbarTab, tab);
    Assert.equal(
      win.document.documentElement.getAttribute("windowclass"),
      windowClass,
      "openWindow and replaceTabWithWindow returned the same result"
    );
    await BrowserTestUtils.closeWindow(win);

    return windowClass;
  }

  is(
    await captureWindowClass("some.other.thing.entirely.desktop"),
    "some.other.thing.entirely",
    "The value in shortcutRelativePath should be used with the extension removed."
  );
  is(
    await captureWindowClass("another.odd.thing.desktop"),
    "another.odd.thing",
    "Gracefully handles the relative path missing a .desktop extension."
  );
  is(
    await captureWindowClass("oops/directory/name/thing.goes.here.desktop"),
    "thing.goes.here",
    "If for some reason it's in a subdirectory, only the basename is used."
  );

  registry.removeTaskbarTab(taskbarTab);
}).skip(AppConstants.platform !== "linux"); // windowclass is only used on Linux

add_task(async function testTaskbarTabCount() {
  const count = () => TaskbarTabs.getCountForId(taskbarTab1.id);

  is(await count(), 0, "window count starts at 0");

  const window1 = await TaskbarTabs.openWindow(taskbarTab1);
  is(await count(), 1, "window count increases on first open");

  const window2 = await TaskbarTabs.openWindow(taskbarTab1);
  is(await count(), 2, "window count increases on second open");

  await BrowserTestUtils.closeWindow(window1);
  is(await count(), 1, "window count decreases on first close");

  const addedTab = BrowserTestUtils.addTab(gBrowser, url1.spec);
  const window3 = await TaskbarTabs.replaceTabWithWindow(taskbarTab1, addedTab);
  is(await count(), 2, "window count increases on replace");

  await TaskbarTabs.ejectWindow(window2);
  await BrowserTestUtils.windowClosed(window2);
  is(await count(), 1, "window count decreases on ejection");

  await BrowserTestUtils.closeWindow(window3);
  is(await count(), 0, "window count decreases on final close");

  const ejected = gBrowser.tabs[gBrowser.tabs.length - 1];
  const promise = BrowserTestUtils.waitForTabClosing(ejected);
  gBrowser.removeTab(ejected);
  await promise;

  TaskbarTabs.removeTaskbarTab(taskbarTab1.id);
});

add_task(async function testWindowIconSet() {
  const wm = new TaskbarTabsWindowManager();

  let mockWindowsUIUtils = {
    QueryInterface: ChromeUtils.generateQI(["nsIWindowsUIUtils"]),
    setWindowIcon: sinon.spy(),
  };
  wm.testOnlyMockUIUtils(mockWindowsUIUtils);

  async function check(win, cause) {
    Assert.equal(
      mockWindowsUIUtils.setWindowIcon.callCount,
      1,
      `${cause} set the window icon once`
    );
    Assert.equal(
      mockWindowsUIUtils.setWindowIcon.firstCall.args[0],
      win,
      `${cause} passed the correct window`
    );
    Assert.equal(
      mockWindowsUIUtils.setWindowIcon.firstCall.args[1],
      img,
      `${cause} passed the correct large icon`
    );
    Assert.equal(
      mockWindowsUIUtils.setWindowIcon.firstCall.args[2],
      img,
      `${cause} passed the correct small icon`
    );

    // `sinon.spy` will hold a reference to the window by virtue of holding its
    // arguments, causing tests to fail from a "leaked" window. Release it before
    // closing the window.
    sinon.resetHistory();
    await BrowserTestUtils.closeWindow(win);
  }

  let img = await TaskbarTabsUtils._imageFromLocalURI(
    Services.io.newURI(
      "chrome://mochitests/content/browser/browser/components/taskbartabs/test/browser/favicon-normal16.png"
    )
  );

  let win = await wm.openWindow(taskbarTab1, img);
  await check(win, "openWindow (explicit)");

  let tab = BrowserTestUtils.addTab(window.gBrowser, url1.spec);
  win = await wm.replaceTabWithWindow(taskbarTab1, tab, img);
  await check(win, "replaceTabWithWindow (explicit)");

  wm.testOnlyMockUIUtils(null);
}).skip(AppConstants.platform !== "win"); // The window icon is only set on Windows.

add_task(async function test_taskbarTab_persistence() {
  const wm = new TaskbarTabsWindowManager();

  // 1. Open first window
  info("Opening first taskbar tab window");
  let win1 = await wm.openWindow(taskbarTab1);

  // Verify ID is set
  is(
    win1.document.documentElement.id,
    "taskbartab-" + taskbarTab1.id,
    "Window ID should be set to taskbar tab ID 1"
  );

  // 2. Move first window
  let originalX1 = win1.screenX;
  let originalY1 = win1.screenY;

  let newX1 = originalX1 + 50;
  let newY1 = originalY1 + 50;

  info(
    `Moving window 1 from ${originalX1},${originalY1} to ${newX1}, ${newY1}`
  );
  win1.moveTo(newX1, newY1);

  await TestUtils.waitForCondition(
    () => win1.screenX == newX1 && win1.screenY == newY1,
    `Waiting for window 1 to move to ${newX1}, ${newY1}`
  );

  // Verify it moved
  is(win1.screenX, newX1, "Window 1 moved to new X");
  is(win1.screenY, newY1, "Window 1 moved to new Y");

  // 3. Close first window
  info("Closing first window");
  await BrowserTestUtils.closeWindow(win1);
  info("First window closed");

  // 4. Open second window with DIFFERENT ID
  info("Opening second taskbar tab window");
  let win2 = await wm.openWindow(taskbarTab2);

  // Verify ID is set
  is(
    win2.document.documentElement.id,
    "taskbartab-" + taskbarTab2.id,
    "Window ID should be set to taskbar tab ID 2"
  );

  // 5. Move second window to a DIFFERENT position
  let originalX2 = win2.screenX;
  let originalY2 = win2.screenY;

  let newX2 = originalX1 + 100;
  let newY2 = originalY1 + 100;

  info(
    `Moving window 2 from ${originalX2},${originalY2} to ${newX2}, ${newY2}`
  );
  win2.moveTo(newX2, newY2);

  // Wait for move to settle and persist
  await TestUtils.waitForCondition(
    () => win2.screenX == newX2 && win2.screenY == newY2,
    `Waiting for window 2 to move to ${newX2}, ${newY2}`
  );

  // Verify it moved
  is(win2.screenX, newX2, "Window 2 moved to new X");
  is(win2.screenY, newY2, "Window 2 moved to new Y");

  // 6. Open first window AGAIN (while second is still open)
  info("Opening first taskbar tab window AGAIN");
  win1 = await wm.openWindow(taskbarTab1);

  // 7. Verify position of first window matches its SAVED position (newX1, newY1)
  info(`Restored window 1 position: ${win1.screenX}, ${win1.screenY}`);

  is(
    win1.screenX,
    newX1,
    "Window 1 screenX should be restored to its own saved position"
  );
  is(
    win1.screenY,
    newY1,
    "Window 1 screenY should be restored to its own saved position"
  );

  isnot(win1.screenX, newX2, "Window 1 should NOT have Window 2's X position");
  isnot(win1.screenY, newY2, "Window 1 should NOT have Window 2's Y position");

  await Promise.all([
    BrowserTestUtils.closeWindow(win1),
    BrowserTestUtils.closeWindow(win2),
  ]);
}).skip(AppConstants.platform === "linux"); // We can't control the window position on Linux.
