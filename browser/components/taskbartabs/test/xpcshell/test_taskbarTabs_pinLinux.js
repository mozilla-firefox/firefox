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

XPCOMUtils.defineLazyServiceGetters(this, {
  imgTools: ["@mozilla.org/image/tools;1", Ci.imgITools],
});

const gRegistry = createInMemoryRegistry();

sinon.stub(ShellService, "writeShortcutIcon").resolves();
sinon.stub(ShellService, "createLinuxDesktopEntry").resolves();
sinon.stub(ShellService, "deleteLinuxDesktopEntry").resolves();
sinon.stub(ShellService, "shellService").value({
  getGlibPrgname() {
    return "glib.mock.prgname";
  },
});

registerCleanupFunction(() => {
  sinon.restore();
});

// Favicons are written to the profile directory, ensure it exists.
do_get_profile();

function checkCreateLinuxDesktopEntryCall(aTaskbarTab) {
  Assert.equal(
    ShellService.createLinuxDesktopEntry.callCount,
    1,
    "createLinuxDesktopEntry was called once."
  );
  Assert.equal(
    ShellService.createLinuxDesktopEntry.firstCall.args[0],
    "glib.mock.prgname.webapp-" + aTaskbarTab.id,
    "Correct application ID was specified."
  );
  Assert.equal(
    ShellService.createLinuxDesktopEntry.firstCall.args[1],
    aTaskbarTab.name,
    "Correct name was specified."
  );
  Assert.ok(
    ShellService.createLinuxDesktopEntry.firstCall.args[2].includes(
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
    "glib.mock.prgname.webapp-" + taskbarTab.id + ".desktop",
    "Expected shortcut path is returned"
  );

  checkCreateLinuxDesktopEntryCall(taskbarTab);
  gRegistry.removeTaskbarTab(taskbarTab.id);
});

// This is more of a Windows problem, so make sure it doesn't affect Linux.
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
    "glib.mock.prgname.webapp-" + invalidTaskbarTab.id + ".desktop",
    "Expected shortcut path is returned"
  );

  checkCreateLinuxDesktopEntryCall(invalidTaskbarTab);
  gRegistry.removeTaskbarTab(invalidTaskbarTab.id);
});

add_task(async function test_unpin() {
  const parsedURI = Services.io.newURI("https://example.com");
  const tt = createTaskbarTab(gRegistry, parsedURI, 0);
  gRegistry.patchTaskbarTab(tt, {
    shortcutRelativePath: "this.is.an.app.id.desktop",
  });

  sinon.resetHistory();
  await TaskbarTabsPin.unpinTaskbarTab(tt);

  Assert.equal(
    ShellService.deleteLinuxDesktopEntry.callCount,
    1,
    "deleteLinuxDesktopEntry was called once"
  );
  Assert.deepEqual(
    ShellService.deleteLinuxDesktopEntry.firstCall.args,
    ["this.is.an.app.id"],
    "deleteLinuxDesktopEntry was called with the value in shortcutRelativePath without the extension"
  );
});
