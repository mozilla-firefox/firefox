/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// This file tests the interaction between calls to TaskbarTabs.sys.mjs and the
// underlying pinTaskbarTab call. If your test is testing pinTaskbarTab itself,
// please use test_taskbarTabs_pin[Platform].js instead.

ChromeUtils.defineESModuleGetters(this, {
  sinon: "resource://testing-common/Sinon.sys.mjs",
  TaskbarTabs: "moz-src:///browser/components/taskbartabs/TaskbarTabs.sys.mjs",
  TaskbarTabsPin:
    "moz-src:///browser/components/taskbartabs/TaskbarTabsPin.sys.mjs",
});

const BASE_URL = "https://example.com/";

const gPinStub = sinon.stub(TaskbarTabsPin, "pinTaskbarTab").callsFake(() => ({
  shortcut: "shortcut relative path",
  rejected: false,
}));
const gUnpinStub = sinon.stub(TaskbarTabsPin, "unpinTaskbarTab");

add_task(async function test_moveTabIntoTaskbarTabParentWindow() {
  await BrowserTestUtils.withNewTab("https://example.com/", async browser => {
    const tab = window.gBrowser.getTabForBrowser(browser);
    gPinStub.resetHistory();

    const move = await TaskbarTabs.moveTabIntoTaskbarTab(tab);
    is(gPinStub.callCount, 1, "Exactly one pin was attempted");
    is(
      gPinStub.firstCall.args[2]?.window,
      move.window,
      "The newly-created window was passed as part of the pinning"
    );

    await BrowserTestUtils.closeWindow(move.window);
    await TaskbarTabs.removeTaskbarTab(move.taskbarTab.id);
  });
});

add_task(async function test_findOrCreateTaskbarTabParentWindow() {
  gPinStub.resetHistory();

  let uri = Services.io.newURI(BASE_URL);
  let result = await TaskbarTabs.findOrCreateTaskbarTab(uri, 0);
  is(gPinStub.callCount, 1, "Exactly one pin was attempted");
  Assert.strictEqual(
    gPinStub.firstCall.args[2]?.window,
    null,
    "No window was passed to pinTaskbarTab"
  );
  await TaskbarTabs.removeTaskbarTab(result.taskbarTab.id);

  uri = Services.io.newURI(BASE_URL);
  const fakeWindow = {};
  result = await TaskbarTabs.findOrCreateTaskbarTab(uri, 0, {
    window: fakeWindow,
  });
  is(gPinStub.callCount, 2, "Exactly one pin was attempted");
  is(
    gPinStub.secondCall.args[2]?.window,
    fakeWindow,
    "Provided window was passed to pinTaskbarTab"
  );
  await TaskbarTabs.removeTaskbarTab(result.taskbarTab.id);
});

add_task(async function test_findOrCreateTaskbarTabEnsurePin() {
  gPinStub.resetHistory();

  let uri = Services.io.newURI(BASE_URL);
  let result1 = await TaskbarTabs.findOrCreateTaskbarTab(uri, 0);
  is(gPinStub.callCount, 1, "Exactly one pin was attempted");

  let result2 = await TaskbarTabs.findOrCreateTaskbarTab(uri, 0, {
    ensurePinned: true,
  });
  is(gPinStub.callCount, 2, "A second pin was attempted");

  Assert.equal(
    result1.taskbarTab.id,
    result2.taskbarTab.id,
    "The same taskbar tab was removed each time"
  );
  Assert.ok(result1.created, "The taskbar tab was created the first time");
  Assert.equal(
    result2.created,
    false,
    "The taskbar tab was reused the second time"
  );

  await TaskbarTabs.removeTaskbarTab(result1.taskbarTab.id);
});

add_task(async function test_shortcutRelativePathIsUpdated() {
  let sandbox = sinon.createSandbox();
  let patchStub = sandbox.stub(
    TaskbarTabsRegistry.prototype,
    "patchTaskbarTab"
  );

  let uri = Services.io.newURI(BASE_URL);
  let { taskbarTab } = await TaskbarTabs.findOrCreateTaskbarTab(uri, 0);
  Assert.equal(patchStub.callCount, 1, "Exactly one pin was attempted");
  Assert.equal(
    patchStub.firstCall.args[0]?.id,
    taskbarTab.id,
    "The correct taskbar tab was patched"
  );
  Assert.deepEqual(
    patchStub.firstCall.args[1],
    { shortcutRelativePath: "shortcut relative path" },
    "The return value from pinTaskbarTab was used as the shortcutRelativePath"
  );
  await TaskbarTabs.removeTaskbarTab(taskbarTab.id);

  await BrowserTestUtils.withNewTab(BASE_URL, async browser => {
    const tab = window.gBrowser.getTabForBrowser(browser);
    patchStub.resetHistory();

    const move = await TaskbarTabs.moveTabIntoTaskbarTab(tab);
    Assert.equal(patchStub.callCount, 1, "Exactly one pin was attempted");
    Assert.equal(
      patchStub.firstCall.args[0]?.id,
      move.taskbarTab.id,
      "The correct taskbar tab was patched"
    );
    Assert.deepEqual(
      patchStub.firstCall.args[1],
      { shortcutRelativePath: "shortcut relative path" },
      "The return value from pinTaskbarTab was used as the shortcutRelativePath"
    );
    await BrowserTestUtils.closeWindow(move.window);
    await TaskbarTabs.removeTaskbarTab(move.taskbarTab.id);
  });

  sandbox.restore();
});

add_task(async function test_findOrCreateTaskbarTabPinResult() {
  let uri = Services.io.newURI(BASE_URL);
  let result = await TaskbarTabs.findOrCreateTaskbarTab(uri, 0);
  Assert.deepEqual(
    await result.pin,
    { shortcut: "shortcut relative path", rejected: false },
    "The pin outcome is returned for an accepted pin"
  );

  let existing = await TaskbarTabs.findOrCreateTaskbarTab(uri, 0);
  Assert.strictEqual(
    existing.pin,
    null,
    "No pin is attempted for an existing Taskbar Tab without ensurePinned"
  );
  await TaskbarTabs.removeTaskbarTab(result.taskbarTab.id);

  gPinStub.callsFake(() => ({
    shortcut: "shortcut relative path",
    rejected: true,
  }));
  result = await TaskbarTabs.findOrCreateTaskbarTab(uri, 0);
  Assert.strictEqual(
    (await result.pin).rejected,
    true,
    "A declined pin is reported as rejected"
  );
  Assert.ok(result.created, "The Taskbar Tab is still created");
  await TaskbarTabs.removeTaskbarTab(result.taskbarTab.id);

  gPinStub.callsFake(() => ({
    shortcut: "shortcut relative path",
    rejected: false,
  }));
});
