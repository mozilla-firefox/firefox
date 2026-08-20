/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const BASE_URL = "https://example.com/";

// Use a different origin so HTTP doesn't upgrade to HTTPS.
// eslint-disable-next-line sdl/no-insecure-url
const BASE_URL_HTTP = "http://mochi.test:8888/";
const HIDDEN_URI = "about:about";
const FILE_URI = "file:///";
let MOZ_EXTENSION_URI; // set under 'add_setup'

ChromeUtils.defineESModuleGetters(this, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  FileTestUtils: "resource://testing-common/FileTestUtils.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  TaskbarTabs: "resource:///modules/taskbartabs/TaskbarTabs.sys.mjs",
  TaskbarTabsPin: "resource:///modules/taskbartabs/TaskbarTabsPin.sys.mjs",
  TaskbarTabsPageAction:
    "resource:///modules/taskbartabs/TaskbarTabsPageAction.sys.mjs",
  TaskbarTabsUtils: "resource:///modules/taskbartabs/TaskbarTabsUtils.sys.mjs",
});

sinon.stub(TaskbarTabsPin, "pinTaskbarTab");
sinon.stub(TaskbarTabsPin, "unpinTaskbarTab");

const kFakeAddonId = "taskbartabs-test-addon@tests.mozilla.org";
const kFakeAddonName = "browser_taskbarTabs_addons.js test addon";
let gExtension;

add_setup(async () => {
  gExtension = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_specific_settings: { gecko: { id: kFakeAddonId } },
      name: kFakeAddonName,
      version: "1.2.3",
    },
    files: {
      "example.html": "<!doctype html>addon example page",
      // This is used in test_extension_name_is_used.
      "with-manifest.html": `<!doctype html><link href='data:application/json,{"name": "override!"}' rel=manifest>`,
    },
  });

  await gExtension.startup();
  MOZ_EXTENSION_URI = `moz-extension://${gExtension.uuid}/example.html`;
});

registerCleanupFunction(async () => {
  sinon.restore();
  await gExtension.unload();
  await TaskbarTabs.resetForTests();
});

async function browserPageAction(win) {
  let pageAction = win.document.getElementById("taskbar-tabs-button");
  ok(pageAction, "Taskbar tab page action button should exist.");

  is(
    pageAction.hidden,
    false,
    "Taskbar tab page action button should not be hidden."
  );
  is(
    pageAction.getAttribute("data-l10n-id"),
    "taskbar-tab-urlbar-button-open",
    "Taskbar tab page action should have the 'add' tooltip."
  );

  let newWinPromise = BrowserTestUtils.waitForNewWindow();

  const clickEvent = new PointerEvent("click", {
    view: win,
  });
  pageAction.dispatchEvent(clickEvent);

  let taskbarTabWindow = await newWinPromise;

  ok(
    taskbarTabWindow.document.documentElement.hasAttribute("taskbartab"),
    "The window HTML should have a taskbartab attribute."
  );

  return taskbarTabWindow;
}

async function taskbarTabsPageAction(win, destWin) {
  let pageAction = win.document.getElementById("taskbar-tabs-button");
  ok(pageAction, "Taskbar tab page action button should exist.");

  is(
    pageAction.hidden,
    false,
    "Taskbar tab page action button should not be hidden."
  );
  is(
    pageAction.getAttribute("data-l10n-id"),
    "taskbar-tab-urlbar-button-close",
    "Taskbar tab page action should have the 'remove' tooltip."
  );

  let closeWinPromise = BrowserTestUtils.windowClosed(win);

  const tabOpenPromise = BrowserTestUtils.waitForEvent(
    destWin.gBrowser.tabContainer,
    "TabOpen"
  );

  const clickEvent = new PointerEvent("click", {
    view: win,
  });
  pageAction.dispatchEvent(clickEvent);

  await closeWinPromise;
  let tab = (await tabOpenPromise).target;

  is(
    tab.documentGlobal,
    destWin,
    "Shoud've reverted back to secondWin, as it is most recently focused"
  );

  ok(
    !tab.ownerDocument.documentElement.hasAttribute("taskbartab"),
    "The window HTML should not have a taskbartab attribute."
  );
  return tab;
}

add_task(async function test_taskbar_tabs_page_action() {
  // Open first Taskbar Tab via page action.
  await BrowserTestUtils.openNewForegroundTab({
    gBrowser: window.gBrowser,
    url: BASE_URL,
  });
  let taskbarTabsWindow1 = await browserPageAction(window);

  is(
    BrowserWindowTracker.getAllVisibleTabs().length,
    2,
    "The number of existing tabs should be two."
  );

  ok(
    TaskbarTabsPin.pinTaskbarTab.called,
    `Pin to taskbar should have been called.`
  );

  // Open second Taskbar Tab via page action.
  sinon.resetHistory();
  await BrowserTestUtils.openNewForegroundTab({
    gBrowser: window.gBrowser,
    url: BASE_URL,
  });
  let taskbarTabsWindow2 = await browserPageAction(window);

  is(
    BrowserWindowTracker.getAllVisibleTabs().length,
    3,
    "The number of existing tabs should be three."
  );

  ok(
    !TaskbarTabsPin.pinTaskbarTab.called,
    `Pin to taskbar should not have been called.`
  );

  // Close first window via page action.
  sinon.resetHistory();
  let tab1 = await taskbarTabsPageAction(taskbarTabsWindow1, window);
  ok(
    !TaskbarTabsPin.unpinTaskbarTab.called,
    `Unpin from taskbar should not have been called.`
  );

  // Close second window via page action.
  sinon.resetHistory();
  let tab2 = await taskbarTabsPageAction(taskbarTabsWindow2, window);
  ok(
    TaskbarTabsPin.unpinTaskbarTab.called,
    `Unpin from taskbar should have been called.`
  );

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
});

add_task(async function testRightClick() {
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser: window.gBrowser,
    url: BASE_URL,
  });
  // Don't synthesize a event with e.g. TestUtils, since testing whether it
  // does nothing means there aren't really any great synchronization options.
  // TaskbarTabsPageAction.handleEvent is async but handling the event drops the
  // promise, so we don't know when it's finished. Also, there are barely any
  // observable results: the whole point is that it does nothing.
  await TaskbarTabsPageAction.handleEvent({
    type: "click",
    button: 2,
    target: document.getElementById("taskbar-tabs-button"),
  });

  const uri = Services.io.newURI(BASE_URL);
  const taskbarTab = await createTaskbarTab(TaskbarTabs, uri, 0);
  is(
    await TaskbarTabs.getCountForId(taskbarTab.id),
    0,
    "right-click did nothing"
  );

  BrowserTestUtils.removeTab(tab);
  await TaskbarTabs.removeTaskbarTab(taskbarTab.id);
});

// Browser tests start with a window1. Convert a tab in this window into a web
// app. Then open another window, window2. Click "revert" on the web app, the
// tab should be back to window1.
add_task(async function revertToOriginal() {
  await BrowserTestUtils.openNewForegroundTab({
    gBrowser: window.gBrowser,
    url: BASE_URL,
  });

  let taskbarTabWindow = await browserPageAction(window);
  is(
    window.gBrowser.openTabs.length,
    1,
    "The original window should have one tab"
  );
  let secondWindow = await BrowserTestUtils.openNewBrowserWindow();

  // Revert back to original window
  let tab = await taskbarTabsPageAction(taskbarTabWindow, window);

  BrowserTestUtils.removeTab(tab);
  await BrowserTestUtils.closeWindow(secondWindow);
});

// If the origin window of a web app is closed, then clicking revert should put
// the tab into the most recently used window.
add_task(async function revertToMostRecent() {
  let firstWin = await BrowserTestUtils.openNewBrowserWindow();

  await BrowserTestUtils.openNewForegroundTab({
    gBrowser: firstWin.gBrowser,
    url: BASE_URL,
  });
  let taskbarTabWindow = await browserPageAction(firstWin);

  let [secondWin, thirdWin] = await Promise.all([
    BrowserTestUtils.openNewBrowserWindow(),
    BrowserTestUtils.openNewBrowserWindow(),
  ]);

  await BrowserTestUtils.closeWindow(firstWin);
  await SimpleTest.promiseFocus(secondWin);

  // Revert back to regular window
  await taskbarTabsPageAction(taskbarTabWindow, secondWin);

  await Promise.all([
    BrowserTestUtils.closeWindow(secondWin),
    BrowserTestUtils.closeWindow(thirdWin),
  ]);
});

add_task(async function testVariousVisibilityChanges() {
  const argsList = [
    [BASE_URL, HIDDEN_URI, true, false],
    [BASE_URL_HTTP, HIDDEN_URI, true, false],
    [BASE_URL, BASE_URL_HTTP, true, true],
    [HIDDEN_URI, BASE_URL, false, true],
    [HIDDEN_URI, BASE_URL_HTTP, false, true],
    [FILE_URI, BASE_URL, false, true],
    [BASE_URL, FILE_URI, true, false],
    [MOZ_EXTENSION_URI, BASE_URL, true, true],
    [HIDDEN_URI, MOZ_EXTENSION_URI, false, true],
  ];

  for (const args of argsList) {
    await testVisibilityChange(...args);
  }
});

async function testVisibilityChange(aFrom, aTo, aFirstVisible, aSecondVisible) {
  const element = document.getElementById("taskbar-tabs-button");
  let locationChange;

  locationChange = BrowserTestUtils.waitForLocationChange(gBrowser, aFrom);
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    aFrom,
    false
  );

  await locationChange;
  is(
    element.hidden,
    !aFirstVisible,
    `Page action is ${aFirstVisible ? "not " : ""}hidden on ${getURIScheme(aFrom)} new tab`
  );

  locationChange = BrowserTestUtils.waitForLocationChange(gBrowser, aTo);
  BrowserTestUtils.startLoadingURIString(gBrowser, aTo);

  await locationChange;
  is(
    element.hidden,
    !aSecondVisible,
    `Page action is ${aSecondVisible ? "not " : ""}hidden on ${getURIScheme(aTo)} reused tab`
  );

  BrowserTestUtils.removeTab(tab);
}

add_task(async function testIframeNavigationIsIgnored() {
  // Navigation within an iframe issues events very similar to top-level navigation.
  // We only want top-level, so test that nothing happens.
  const element = document.getElementById("taskbar-tabs-button");

  await BrowserTestUtils.withNewTab("data:text/plain,", async browser => {
    ok(element.hidden, "Page action is hidden on about: scheme");

    await SpecialPowers.spawn(browser, [], async () => {
      content.document.body.innerHTML =
        "<iframe id='iframe' src='https://example.com'></iframe><p>taskbartabs!</p>";
      await new Promise(resolve => {
        content.document
          .getElementById("iframe")
          .addEventListener("load", _e => resolve(), { once: true });
      });
    });

    ok(element.hidden, "Page action is still hidden despite iframe change");
  });
});

function getURIScheme(uri) {
  return Services.io.newURI(uri).scheme;
}

add_task(async function testPrefIsMonitored() {
  const element = document.getElementById("taskbar-tabs-button");

  await BrowserTestUtils.withNewTab(BASE_URL, async () => {
    ok(!element.hidden, "Page action starts as visible");
    await testVisibilityChange(BASE_URL, HIDDEN_URI, true, false);

    await SpecialPowers.pushPrefEnv({
      set: [["browser.taskbarTabs.enabled", false]],
    });
    ok(element.hidden, "Page action becomes invisible");
    await testVisibilityChange(BASE_URL, HIDDEN_URI, false, false);

    await SpecialPowers.popPrefEnv();
    ok(!element.hidden, "Page action becomes visible again");
    await testVisibilityChange(BASE_URL, HIDDEN_URI, true, false);
  });
});

add_task(async function test_moveTabIntoTaskbarTabCreation() {
  const uri = Services.io.newURI(BASE_URL);
  Assert.equal(
    await TaskbarTabs.findTaskbarTab(uri, 0),
    null,
    "example.com does not already have a taskbar tab"
  );

  await BrowserTestUtils.withNewTab("https://example.com/", async browser => {
    const tab = window.gBrowser.getTabForBrowser(browser);
    const move = await TaskbarTabs.moveTabIntoTaskbarTab(tab);

    const found = TaskbarTabsUtils.getTaskbarTabIdFromWindow(move.window);
    is(found, move.taskbarTab.id, "Returned Taskbar Tab matches window");
    await BrowserTestUtils.closeWindow(move.window);
    await TaskbarTabs.removeTaskbarTab(found);
  });
});

add_task(async function test_moveTabIntoTaskbarTabReuse() {
  // Ensure example.com has a Taskbar Tab.
  const uri = Services.io.newURI(BASE_URL);
  const tt = await createTaskbarTab(TaskbarTabs, uri, 0);

  await BrowserTestUtils.withNewTab("https://example.com/", async browser => {
    const tab = window.gBrowser.getTabForBrowser(browser);
    const move = await TaskbarTabs.moveTabIntoTaskbarTab(tab);

    const found = TaskbarTabsUtils.getTaskbarTabIdFromWindow(move.window);
    is(found, move.taskbarTab.id, "Returned Taskbar Tab matches window");
    is(tt.id, move.taskbarTab.id, "Returned Taskbar Tab existed before");
    await BrowserTestUtils.closeWindow(move.window);
    await TaskbarTabs.removeTaskbarTab(tt.id);
  });
});

add_task(async function test_page_action_uses_manifest() {
  const pageAction = window.document.getElementById("taskbar-tabs-button");
  const url = "https://example.com/";
  await BrowserTestUtils.withNewTab(url, async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      content.document.body.innerHTML =
        '<link rel="manifest" href="/manifest.webapp">';
    });

    const newWinPromise = BrowserTestUtils.waitForNewWindow();
    pageAction.dispatchEvent(new PointerEvent("click"));
    const win = await newWinPromise;

    const uri = Services.io.newURI(url);
    const tt = await TaskbarTabs.findTaskbarTab(uri, 0);
    is(
      await TaskbarTabsUtils.getTaskbarTabIdFromWindow(win),
      tt.id,
      "Page action created a Taskbar Tab"
    );
    is(tt.name, "Mochitest", "Manifest name was used");

    await BrowserTestUtils.closeWindow(win);
    await TaskbarTabs.removeTaskbarTab(tt.id);
  });
});

add_task(async function test_extension_name_is_used() {
  async function checkExtensionURIWithManifest({ withManifest, expectedName }) {
    let uri = Services.io.newURI(MOZ_EXTENSION_URI);
    uri = uri.resolve(withManifest ? "/with-manifest.html" : "/example.html");
    uri = Services.io.newURI(uri);

    let result = await TaskbarTabs.findOrCreateTaskbarTab(uri, 0, {
      ...(withManifest ? { manifest: { name: "override!" } } : {}),
    });
    is(
      result.taskbarTab.name,
      expectedName,
      "findOrCreateTaskbarTab uses expected name"
    );
    ok(result.created, "A new Taskbar Tab was created.");
    await TaskbarTabs.removeTaskbarTab(result.taskbarTab.id);

    await BrowserTestUtils.withNewTab(uri.spec, async browser => {
      const tab = window.gBrowser.getTabForBrowser(browser);
      const move = await TaskbarTabs.moveTabIntoTaskbarTab(tab);

      is(
        move.taskbarTab.name,
        expectedName,
        "moveTabIntoTaskbarTab uses expected name"
      );
      ok(move.created, "A new Taskbar Tab was created.");

      await BrowserTestUtils.closeWindow(move.window);
      await TaskbarTabs.removeTaskbarTab(move.taskbarTab.id);
    });
  }

  await checkExtensionURIWithManifest({
    withManifest: false,
    expectedName: kFakeAddonName,
  });

  await checkExtensionURIWithManifest({
    withManifest: true,
    expectedName: "override!",
  });
});
