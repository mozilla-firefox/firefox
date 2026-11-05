/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  FileTestUtils: "resource://testing-common/FileTestUtils.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  ShellService: "moz-src:///browser/components/shell/ShellService.sys.mjs",
  TaskbarTabsPin:
    "moz-src:///browser/components/taskbartabs/TaskbarTabsPin.sys.mjs",
  TaskbarTabsPinWin:
    "moz-src:///browser/components/taskbartabs/TaskbarTabsPinWin.sys.mjs",
  TaskbarTabsRegistry:
    "moz-src:///browser/components/taskbartabs/TaskbarTabsRegistry.sys.mjs",
  TaskbarTabsUtils:
    "moz-src:///browser/components/taskbartabs/TaskbarTabsUtils.sys.mjs",
  XPCOMUtils: "resource://gre/modules/XPCOMUtils.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetters(this, {
  imgTools: ["@mozilla.org/image/tools;1", Ci.imgITools],
});

// We want to mock the native XPCOM interfaces of the initialized
// `ShellService.shellService`, but those interfaces are frozen. Instead we
// proxy `ShellService.shellService` and mock it.
const kMockNativeShellService = {
  ...ShellService.shellService,
  createShortcut: sinon.stub().resolves("dummy_path"),
  deleteShortcut: sinon.stub().resolves("dummy_path"),
  pinShortcutToTaskbar: sinon.stub().resolves(),
  unpinShortcutFromTaskbar: sinon.stub(),
};

sinon.stub(ShellService, "shellService").value(kMockNativeShellService);
sinon.stub(ShellService, "writeShortcutIcon");

sinon.stub(TaskbarTabsPinWin, "_getLocalization").returns({
  formatValue(msg) {
    // Slash must also be sanitized, so it should appear as '_' in paths.
    return `[formatValue/${msg}]`;
  },
});

registerCleanupFunction(() => {
  sinon.restore();
});

const registry = createInMemoryRegistry();

// Favicons are written to the profile directory, ensure it exists.
do_get_profile();

async function attemptPin(aTaskbarTab, aDestFolder, aShortcutRelativePath) {
  Assert.equal(
    (
      await TaskbarTabsPin.pinTaskbarTab(
        aTaskbarTab,
        await TaskbarTabsUtils.getDefaultIcon()
      )
    ).shortcut,
    aShortcutRelativePath,
    "pinTaskbarTab returned expected relative path."
  );
  ok(
    ShellService.writeShortcutIcon.calledOnce,
    `Icon creation should have been called.`
  );
  ok(
    kMockNativeShellService.createShortcut.calledOnce,
    `Shortcut creation should have been called.`
  );
  Assert.equal(
    kMockNativeShellService.createShortcut.firstCall.args[6],
    aDestFolder,
    "The shortcut should go into the expected destination."
  );
  Assert.equal(
    kMockNativeShellService.createShortcut.firstCall.args[7],
    aShortcutRelativePath,
    "The shortcut path should match the expected result."
  );
  ok(
    kMockNativeShellService.pinShortcutToTaskbar.calledOnce,
    `Pin to taskbar should have been called.`
  );
  Assert.equal(
    kMockNativeShellService.pinShortcutToTaskbar.firstCall.args[1],
    kMockNativeShellService.createShortcut.firstCall.args[6],
    `The created and pinned shortcuts should be in the same folder.`
  );
  Assert.equal(
    kMockNativeShellService.pinShortcutToTaskbar.firstCall.args[2],
    kMockNativeShellService.createShortcut.firstCall.args[7],
    `The created and pinned shortcuts should be the same file.`
  );
}

function shellUnpinCalled(aShortcutRelativePath) {
  ok(
    kMockNativeShellService.deleteShortcut.calledOnce,
    `Unpin from taskbar should have been called.`
  );
  Assert.equal(
    kMockNativeShellService.deleteShortcut.firstCall.args[1],
    aShortcutRelativePath,
    "shortcutRelativePath was deleted."
  );
  ok(
    kMockNativeShellService.unpinShortcutFromTaskbar.calledOnce,
    `Unpin from taskbar should have been called.`
  );
  Assert.equal(
    kMockNativeShellService.unpinShortcutFromTaskbar.firstCall.args[1],
    aShortcutRelativePath,
    "shortcutRelativePath was deleted."
  );
}

add_task(async function test_pin_location() {
  const parsedURI = Services.io.newURI("https://www.example.com");
  const taskbarTab = createTaskbarTab(registry, parsedURI, 0);
  sinon.resetHistory();

  await attemptPin(
    taskbarTab,
    "Programs",
    `[formatValue_taskbar-tab-shortcut-folder]\\${taskbarTab.name}.lnk`
  );

  registry.removeTaskbarTab(taskbarTab.id);
});

add_task(async function test_pin_location_dos_name() {
  const parsedURI = Services.io.newURI("https://aux.test");
  const invalidTaskbarTab = createTaskbarTab(registry, parsedURI, 0);
  sinon.resetHistory();

  await attemptPin(
    invalidTaskbarTab,
    "Programs",
    // 'Untitled' is the default selected by the MIME code, since
    // AUX is a reserved name on Windows.
    "[formatValue_taskbar-tab-shortcut-folder]\\Untitled.lnk"
  );

  registry.removeTaskbarTab(invalidTaskbarTab.id);
});

add_task(async function test_pin_location_bad_characters() {
  const parsedURI = Services.io.newURI("https://another.test");
  const invalidTaskbarTab = createTaskbarTab(registry, parsedURI, 0, {
    manifest: {
      name: "** :\t\r\n \\\\ >> Not a valid. filename??! << // |||: **.",
    },
  });
  sinon.resetHistory();

  await attemptPin(
    invalidTaskbarTab,
    "Programs",
    "[formatValue_taskbar-tab-shortcut-folder]\\__ ____ __ __ Not a valid. filename__! __ __ ____ __..lnk"
  );

  registry.removeTaskbarTab(invalidTaskbarTab.id);
});

add_task(async function test_pin_location_lnk_extension() {
  const parsedURI = Services.io.newURI("https://another.test");
  const invalidTaskbarTab = createTaskbarTab(registry, parsedURI, 0, {
    manifest: {
      name: "coolstartup.lnk",
    },
  });
  sinon.resetHistory();

  await attemptPin(
    invalidTaskbarTab,
    "Programs",
    "[formatValue_taskbar-tab-shortcut-folder]\\coolstartup.lnk.lnk"
  );

  registry.removeTaskbarTab(invalidTaskbarTab.id);
});

add_task(async function test_unpin() {
  const exampleUrl = Services.io.newURI("https://example.com");
  const tt = createTaskbarTab(registry, exampleUrl, 0);
  registry.patchTaskbarTab(tt, {
    shortcutRelativePath: "somewhere else\\shortcut name.lnk",
  });

  sinon.resetHistory();
  await TaskbarTabsPin.unpinTaskbarTab(tt);

  shellUnpinCalled("somewhere else\\shortcut name.lnk");
});

add_task(async function test_pin_rejected() {
  const parsedURI = Services.io.newURI("https://www.example.com");
  const taskbarTab = createTaskbarTab(registry, parsedURI, 0);
  const { pinShortcutToTaskbar } = kMockNativeShellService;

  pinShortcutToTaskbar.resolves(Ci.nsIWindowsShellService.PINNED);
  let result = await TaskbarTabsPin.pinTaskbarTab(
    taskbarTab,
    await TaskbarTabsUtils.getDefaultIcon()
  );
  Assert.equal(result.rejected, false, "An accepted pin is not rejected");

  pinShortcutToTaskbar.resolves(Ci.nsIWindowsShellService.REJECTED);
  result = await TaskbarTabsPin.pinTaskbarTab(
    taskbarTab,
    await TaskbarTabsUtils.getDefaultIcon()
  );
  Assert.equal(result.rejected, true, "A declined pin is reported");
  Assert.ok(result.shortcut, "The shortcut path is still returned");

  pinShortcutToTaskbar.resolves();
  registry.removeTaskbarTab(taskbarTab.id);
});

add_task(async function test_pin_error_returns_shortcut() {
  const parsedURI = Services.io.newURI("https://www.example.com");
  const taskbarTab = createTaskbarTab(registry, parsedURI, 0);
  const { pinShortcutToTaskbar } = kMockNativeShellService;
  sinon.resetHistory();

  pinShortcutToTaskbar.rejects(new Error("Pin failed"));
  let result = await TaskbarTabsPin.pinTaskbarTab(
    taskbarTab,
    await TaskbarTabsUtils.getDefaultIcon()
  );

  Assert.strictEqual(
    result.shortcut,
    kMockNativeShellService.createShortcut.firstCall.args[7],
    "The created shortcut is returned so that it can be deleted later"
  );
  Assert.strictEqual(result.rejected, false, "A failed pin is not a rejection");

  pinShortcutToTaskbar.resolves();
  registry.removeTaskbarTab(taskbarTab.id);
});
