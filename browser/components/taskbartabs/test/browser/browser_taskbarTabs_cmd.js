/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  sinon: "resource://testing-common/Sinon.sys.mjs",
  Subprocess: "resource://gre/modules/Subprocess.sys.mjs",
  TaskbarTabs: "moz-src:///browser/components/taskbartabs/TaskbarTabs.sys.mjs",
  TaskbarTabsPin:
    "moz-src:///browser/components/taskbartabs/TaskbarTabsPin.sys.mjs",
  TaskbarTabsUtils:
    "moz-src:///browser/components/taskbartabs/TaskbarTabsUtils.sys.mjs",
});

sinon.stub(TaskbarTabsPin, "pinTaskbarTab").resolves({});

registerCleanupFunction(async () => {
  sinon.restore();
  await TaskbarTabs.resetForTests();
});

let taskbarTab1;

add_setup(async () => {
  const url1 = Services.io.newURI("https://example.com");
  const userContextId1 = 0;
  taskbarTab1 = await createTaskbarTab(TaskbarTabs, url1, userContextId1);

  // Reset memory of pinning being called.
  sinon.resetHistory();
});

// Headless instances don't hand the command line to the running instance.
add_task(
  { skip_if: () => Services.env.exists("MOZ_HEADLESS") },
  async function test_commandline_handling() {
    let exe = Services.dirsvc.get("XREExeF", Ci.nsIFile).path;
    let profile = Services.dirsvc.get("ProfD", Ci.nsIFile).path;

    // Test existing Taskbar Tab opens when launched via commandline.

    let args = [
      "-taskbar-tab",
      taskbarTab1.id,
      "-new-window",
      taskbarTab1.startUrl,
      "-profile",
      profile,
      "-container",
      taskbarTab1.userContextId,
    ];

    let winPromise = BrowserTestUtils.waitForNewWindow();
    await Subprocess.call({
      command: exe,
      arguments: args,
    });
    let winExisting = await winPromise;

    ok(
      TaskbarTabsUtils.isTaskbarTabWindow(winExisting),
      "The window opened via command line should be a Taskbar Tabs window."
    );
    is(
      TaskbarTabsUtils.getTaskbarTabIdFromWindow(winExisting),
      taskbarTab1.id,
      "The existing Taskbar Tab ID should be used."
    );
    ok(
      !TaskbarTabsPin.pinTaskbarTab.called,
      "Pinning should not be attempted when launching an existing Taskbar Tab."
    );

    // Test non-existant Taskbar Tab is reconstructed when launched via commandline.

    const startUrl = "https://www.subdomain.example.com";
    const userContextId = 1;
    const id = Services.uuid.generateUUID().toString().slice(1, -1);

    args = [
      "-taskbar-tab",
      id,
      "-new-window",
      startUrl,
      "-profile",
      profile,
      "-container",
      userContextId,
    ];

    winPromise = BrowserTestUtils.waitForNewWindow();
    await Subprocess.call({
      command: exe,
      arguments: args,
    });
    let winReconstruct = await winPromise;

    ok(
      TaskbarTabsUtils.isTaskbarTabWindow(winReconstruct),
      "The window opened via command line should be a Taskbar Tabs window."
    );

    let windowId = TaskbarTabsUtils.getTaskbarTabIdFromWindow(winReconstruct);
    isnot(windowId, taskbarTab1.id, "A new Taskbar Tab ID should be used.");

    let registered = await TaskbarTabs.getTaskbarTab(windowId);
    ok(registered, "A new Taskbar Tab should have been registered.");

    ok(
      TaskbarTabsPin.pinTaskbarTab.called,
      "Pinning should be attempted when reconstructing a Taskbar Tab."
    );

    is(
      registered.startUrl,
      "https://www.subdomain.example.com",
      "The start URL should be correct."
    );
    Assert.deepEqual(
      registered.scopes,
      [{ hostname: "www.subdomain.example.com" }],
      "The scope should match the URL."
    );
    is(registered.userContextId, 1, "The container ID should be correct.");

    await Promise.all([
      BrowserTestUtils.closeWindow(winExisting),
      BrowserTestUtils.closeWindow(winReconstruct),
    ]);
  }
);
