/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  MockRegistrar: "resource://testing-common/MockRegistrar.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  ShellService: "moz-src:///browser/components/shell/ShellService.sys.mjs",
  TaskbarTabsPin:
    "moz-src:///browser/components/taskbartabs/TaskbarTabsPin.sys.mjs",
  TaskbarTabsRegistry:
    "moz-src:///browser/components/taskbartabs/TaskbarTabsRegistry.sys.mjs",
  XPCOMUtils: "resource://gre/modules/XPCOMUtils.sys.mjs",
});

const gRegistry = createInMemoryRegistry();

sinon.stub(ShellService, "writeShortcutIcon").resolves();
sinon.stub(ShellService, "shellService").value({
  ...ShellService.shellService,
  // None of these should be called.
  createShortcut: sinon.stub().throws(),
  deleteShortcut: sinon.stub().throws(),
  pinShortcutToTaskbar: sinon.stub().throws(),
  unpinShortcutFromTaskbar: sinon.stub().throws(),
});
sinon.stub(ShellService, "requestCreateAndPinSecondaryTile").resolves();
sinon.stub(ShellService, "requestDeleteSecondaryTile").resolves();

registerCleanupFunction(() => {
  sinon.restore();
});

// Favicons are written to the profile directory, ensure it exists.
do_get_profile();

function checkCreateSecondaryTileCall(aTaskbarTab) {
  Assert.equal(
    ShellService.requestCreateAndPinSecondaryTile.callCount,
    1,
    "requestCreateAndPinSecondaryTile was called once."
  );
  Assert.equal(
    ShellService.requestCreateAndPinSecondaryTile.firstCall.args[0],
    "taskbartab-" + aTaskbarTab.id,
    "Correct secondary tile ID was specified."
  );
  Assert.equal(
    ShellService.requestCreateAndPinSecondaryTile.firstCall.args[1],
    aTaskbarTab.name,
    "Correct name was specified."
  );
  Assert.ok(
    ShellService.requestCreateAndPinSecondaryTile.firstCall.args[2].endsWith(
      // This is Windows-only, so it'll be an ICO.
      aTaskbarTab.id + ".ico"
    ),
    "Reasonable icon path was specified."
  );
  Assert.ok(
    ShellService.requestCreateAndPinSecondaryTile.firstCall.args[3].includes(
      "-taskbar-tab"
    ),
    "Reasonable arguments were specified."
  );
}

add_task(async function test_pinCreatesDesktopEntry() {
  const parsedURI = Services.io.newURI("https://www.example.com");
  const taskbarTab = createTaskbarTab(gRegistry, parsedURI, 0);
  sinon.resetHistory();

  Assert.equal(
    (await TaskbarTabsPin.pinTaskbarTab(taskbarTab)).shortcut,
    "taskbartab-" + taskbarTab.id,
    "Correct relative path is saved to the taskbar tab"
  );

  checkCreateSecondaryTileCall(taskbarTab);
  gRegistry.removeTaskbarTab(taskbarTab.id);
});

// We should be able to put whatever we want into the name on MSIX systems.
add_task(async function test_pinUnusualName() {
  const parsedURI = Services.io.newURI("https://another.test");
  const invalidTaskbarTab = createTaskbarTab(gRegistry, parsedURI, 0, {
    manifest: {
      name: "** :\t\r\n \\\\ >> Not a valid. filename??! << // |||: **.",
    },
  });
  sinon.resetHistory();

  Assert.equal(
    (await TaskbarTabsPin.pinTaskbarTab(invalidTaskbarTab)).shortcut,
    "taskbartab-" + invalidTaskbarTab.id,
    "Correct relative path is saved to the taskbar tab"
  );

  checkCreateSecondaryTileCall(invalidTaskbarTab);
  gRegistry.removeTaskbarTab(invalidTaskbarTab.id);
});

add_task(async function test_unpin() {
  const parsedURI = Services.io.newURI("https://example.com");
  const tt = createTaskbarTab(gRegistry, parsedURI, 0);
  gRegistry.patchTaskbarTab(tt, {
    shortcutRelativePath: "321cba-batrabksat",
  });

  sinon.resetHistory();
  await TaskbarTabsPin.unpinTaskbarTab(tt);

  Assert.equal(
    ShellService.requestDeleteSecondaryTile.callCount,
    1,
    "requestDeleteSecondaryTile was called once"
  );
  Assert.deepEqual(
    ShellService.requestDeleteSecondaryTile.firstCall.args,
    ["321cba-batrabksat"],
    "requestDeleteSecondaryTile was called with the value in shortcutRelativePath"
  );
});
