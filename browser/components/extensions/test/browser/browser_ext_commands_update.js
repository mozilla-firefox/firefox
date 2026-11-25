"use strict";

ChromeUtils.defineESModuleGetters(this, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  ExtensionSettingsStore:
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs",
});

function enableAddon(addon) {
  return new Promise(resolve => {
    AddonManager.addAddonListener({
      onEnabled(enabledAddon) {
        if (enabledAddon.id == addon.id) {
          resolve();
          AddonManager.removeAddonListener(this);
        }
      },
    });
    addon.enable();
  });
}

function disableAddon(addon) {
  return new Promise(resolve => {
    AddonManager.addAddonListener({
      onDisabled(disabledAddon) {
        if (disabledAddon.id == addon.id) {
          resolve();
          AddonManager.removeAddonListener(this);
        }
      },
    });
    addon.disable();
  });
}

add_task(async function test_update_defined_command() {
  let extension;
  let updatedExtension;
  let needsCleanup = true;
  const cleanup = async () => {
    if (!needsCleanup) {
      return;
    }
    needsCleanup = false;
    const extensionId = extension.id;
    await extension?.unload();
    // updatedExtension might not have started up if we didn't make it that far.
    await updatedExtension?.unload();

    // Check that ESS is cleaned up on uninstall.
    if (extensionId) {
      let storedCommands = ExtensionSettingsStore.getAllForExtension(
        extensionId,
        "commands"
      );
      is(storedCommands.length, 0, "There are no stored commands after unload");
    }
  };

  registerCleanupFunction(cleanup);

  extension = ExtensionTestUtils.loadExtension({
    useAddonManager: "permanent",
    manifest: {
      version: "1.0",
      browser_specific_settings: { gecko: { id: "commands@mochi.test" } },
      commands: {
        foo: {
          suggested_key: {
            default: "Ctrl+Shift+I",
          },
          description: "The foo command",
        },
      },
    },
    background() {
      browser.test.onMessage.addListener(async (msg, data) => {
        if (msg == "update") {
          await browser.commands.update(data);
          return browser.test.sendMessage("updateDone");
        } else if (msg == "reset") {
          await browser.commands.reset(data);
          return browser.test.sendMessage("resetDone");
        } else if (msg != "run") {
          return;
        }
        // Test initial manifest command.
        let commands = await browser.commands.getAll();
        browser.test.assertEq(1, commands.length, "There is 1 command");
        let command = commands[0];
        browser.test.assertEq("foo", command.name, "The name is right");
        browser.test.assertEq(
          "The foo command",
          command.description,
          "The description is right"
        );
        browser.test.assertEq(
          "Ctrl+Shift+I",
          command.shortcut,
          "The shortcut is right"
        );

        // Update the shortcut.
        await browser.commands.update({
          name: "foo",
          shortcut: "Ctrl+Shift+L",
        });

        // Test the updated shortcut.
        commands = await browser.commands.getAll();
        browser.test.assertEq(1, commands.length, "There is still 1 command");
        command = commands[0];
        browser.test.assertEq("foo", command.name, "The name is unchanged");
        browser.test.assertEq(
          "The foo command",
          command.description,
          "The description is unchanged"
        );
        browser.test.assertEq(
          "Ctrl+Shift+L",
          command.shortcut,
          "The shortcut is updated"
        );

        // Update the description.
        await browser.commands.update({
          name: "foo",
          description: "The only command",
        });

        // Test the updated shortcut.
        commands = await browser.commands.getAll();
        browser.test.assertEq(1, commands.length, "There is still 1 command");
        command = commands[0];
        browser.test.assertEq("foo", command.name, "The name is unchanged");
        browser.test.assertEq(
          "The only command",
          command.description,
          "The description is updated"
        );
        browser.test.assertEq(
          "Ctrl+Shift+L",
          command.shortcut,
          "The shortcut is unchanged"
        );

        // Clear the shortcut.
        await browser.commands.update({
          name: "foo",
          shortcut: "",
        });
        commands = await browser.commands.getAll();
        browser.test.assertEq(1, commands.length, "There is still 1 command");
        command = commands[0];
        browser.test.assertEq("foo", command.name, "The name is unchanged");
        browser.test.assertEq(
          "The only command",
          command.description,
          "The description is unchanged"
        );
        browser.test.assertEq("", command.shortcut, "The shortcut is empty");

        // Update the description and shortcut.
        await browser.commands.update({
          name: "foo",
          description: "The new command",
          shortcut: "   Alt+  Shift +9",
        });

        // Test the updated shortcut.
        commands = await browser.commands.getAll();
        browser.test.assertEq(1, commands.length, "There is still 1 command");
        command = commands[0];
        browser.test.assertEq("foo", command.name, "The name is unchanged");
        browser.test.assertEq(
          "The new command",
          command.description,
          "The description is updated"
        );
        browser.test.assertEq(
          "Alt+Shift+9",
          command.shortcut,
          "The shortcut is updated"
        );

        // Test a bad shortcut update.
        browser.test.assertThrows(
          () =>
            browser.commands.update({ name: "foo", shortcut: "Ctl+Shift+L" }),
          /Type error for parameter detail .+ primary modifier and a key/,
          "It rejects for a bad shortcut"
        );

        // Try to update a command that doesn't exist.
        await browser.test.assertRejects(
          browser.commands.update({ name: "bar", shortcut: "Ctrl+Shift+L" }),
          'Unknown command "bar"',
          "It rejects for an unknown command"
        );

        browser.test.notifyPass("commands");
      });
      browser.test.sendMessage("ready");
    },
  });

  await extension.startup();

  function extensionKeyset(extensionId) {
    return document.getElementById(
      makeWidgetId(`ext-keyset-id-${extensionId}`)
    );
  }

  function checkKey(extensionId, shortcutKey, modifiers) {
    let keyset = extensionKeyset(extensionId);
    is(keyset.children.length, 1, "There is 1 key in the keyset");
    let key = keyset.children[0];
    is(key.getAttribute("key"), shortcutKey, "The key is correct");
    is(key.getAttribute("modifiers"), modifiers, "The modifiers are correct");
  }

  function checkNumericKey(extensionId, key, modifiers) {
    let keyset = extensionKeyset(extensionId);
    is(
      keyset.children.length,
      2,
      "There are 2 keys in the keyset now, 1 of which contains a keycode."
    );
    let numpadKey = keyset.children[0];
    is(
      numpadKey.getAttribute("keycode"),
      `VK_NUMPAD${key}`,
      "The numpad keycode is correct."
    );
    is(
      numpadKey.getAttribute("modifiers"),
      modifiers,
      "The modifiers are correct"
    );

    let originalNumericKey = keyset.children[1];
    is(
      originalNumericKey.getAttribute("keycode"),
      `VK_${key}`,
      "The original key is correct."
    );
    is(
      originalNumericKey.getAttribute("modifiers"),
      modifiers,
      "The modifiers are correct"
    );
  }

  // Check that the <key> is set for the original shortcut.
  checkKey(extension.id, "I", "accel,shift");

  await extension.awaitMessage("ready");
  extension.sendMessage("run");
  await extension.awaitFinish("commands");

  // Check that the <keycode> has been updated.
  checkNumericKey(extension.id, "9", "alt,shift");

  // Check that the updated command is stored in ExtensionSettingsStore.
  let storedCommands = ExtensionSettingsStore.getAllForExtension(
    extension.id,
    "commands"
  );
  is(storedCommands.length, 1, "There is only one stored command");
  let command = ExtensionSettingsStore.getSetting(
    "commands",
    "foo",
    extension.id
  ).value;
  is(command.description, "The new command", "The description is stored");
  is(command.shortcut, "Alt+Shift+9", "The shortcut is stored");

  // Check that the key is updated immediately.
  extension.sendMessage("update", { name: "foo", shortcut: "Ctrl+Shift+M" });
  await extension.awaitMessage("updateDone");
  checkKey(extension.id, "M", "accel,shift");

  // Ensure all successive updates are stored.
  // Force the command to only have a description saved.
  await ExtensionSettingsStore.addSetting(extension.id, "commands", "foo", {
    description: "description only",
  });
  // This command now only has a description set in storage, also update the shortcut.
  extension.sendMessage("update", { name: "foo", shortcut: "Alt+Shift+9" });
  await extension.awaitMessage("updateDone");
  let storedCommand = await ExtensionSettingsStore.getSetting(
    "commands",
    "foo",
    extension.id
  );
  is(
    storedCommand.value.shortcut,
    "Alt+Shift+9",
    "The shortcut is saved correctly"
  );
  is(
    storedCommand.value.description,
    "description only",
    "The description is saved correctly"
  );

  // Calling browser.commands.reset("foo") should reset to manifest version.
  extension.sendMessage("reset", "foo");
  await extension.awaitMessage("resetDone");

  checkKey(extension.id, "I", "accel,shift");

  // Check that enable/disable removes the keyset and reloads the saved command.
  let addon = await AddonManager.getAddonByID(extension.id);
  await disableAddon(addon);
  let keyset = extensionKeyset(extension.id);
  is(keyset, null, "The extension keyset is removed when disabled");
  // Add some commands to storage, only "foo" should get loaded.
  await ExtensionSettingsStore.addSetting(extension.id, "commands", "foo", {
    shortcut: "Alt+Shift+9",
  });
  await ExtensionSettingsStore.addSetting(extension.id, "commands", "unknown", {
    shortcut: "Ctrl+Shift+P",
  });
  storedCommands = ExtensionSettingsStore.getAllForExtension(
    extension.id,
    "commands"
  );
  is(storedCommands.length, 2, "There are now 2 commands stored");
  await enableAddon(addon);
  // Wait for the keyset to appear (it's async on enable).
  await TestUtils.waitForCondition(() => extensionKeyset(extension.id));
  // The keyset is back with the value from ExtensionSettingsStore.
  checkNumericKey(extension.id, "9", "alt,shift");

  // Check that an update to a shortcut in the manifest is mapped correctly.
  updatedExtension = ExtensionTestUtils.loadExtension({
    useAddonManager: "permanent",
    manifest: {
      version: "1.0",
      browser_specific_settings: { gecko: { id: "commands@mochi.test" } },
      commands: {
        foo: {
          suggested_key: {
            default: "Ctrl+Shift+L",
          },
          description: "The foo command",
        },
      },
    },
  });
  await updatedExtension.startup();

  await TestUtils.waitForCondition(() => extensionKeyset(extension.id));
  // Shortcut is unchanged since it was previously updated.
  checkNumericKey(extension.id, "9", "alt,shift");

  await cleanup();
});

add_task(async function updateSidebarCommand() {
  // This test doesn't work with the new sidebar because it relies on the
  // switcher, which is no longer a thing in the new sidebar. See the
  // `test_extension_sidebar_shortcuts` task, which covers shortcuts for the
  // new sidebar separately.
  if (Services.prefs.getBoolPref("sidebar.revamp", false)) {
    info("skipping test because sidebar.revamp is set");
    return;
  }

  let extension = ExtensionTestUtils.loadExtension({
    useAddonManager: "temporary",
    manifest: {
      commands: {
        _execute_sidebar_action: {
          suggested_key: {
            default: "Ctrl+Shift+E",
          },
        },
      },
      sidebar_action: {
        default_panel: "sidebar.html",
      },
    },
    background() {
      browser.test.onMessage.addListener(async (msg, data) => {
        if (msg == "updateShortcut") {
          await browser.commands.update(data);
          return browser.test.sendMessage("done");
        }
        throw new Error("Unknown message");
      });
    },
    files: {
      "sidebar.html": `
        <!DOCTYPE html>
        <html>
        <head><meta charset="utf-8"/>
        <script src="sidebar.js"></script>
        </head>
        <body>
        A Test Sidebar
        </body></html>
      `,

      "sidebar.js": function () {
        window.onload = () => {
          browser.test.sendMessage("sidebar");
        };
      },
    },
  });
  await extension.startup();
  await extension.awaitMessage("sidebar");

  // Show and hide the switcher panel to generate the initial shortcuts.
  let switcherShown = promisePopupShown(SidebarController._switcherPanel);
  SidebarController.showSwitcherPanel();
  await switcherShown;
  let switcherHidden = promisePopupHidden(SidebarController._switcherPanel);
  SidebarController.hideSwitcherPanel();
  await switcherHidden;

  let menuitemId = `sidebarswitcher_menu_${makeWidgetId(
    extension.id
  )}-sidebar-action`;
  let menuitem = document.getElementById(menuitemId);
  let acceltext = menuitem.getAttribute("acceltext");
  ok(acceltext.endsWith("E"), "The menuitem has the accel text set");

  extension.sendMessage("updateShortcut", {
    name: "_execute_sidebar_action",
    shortcut: "Ctrl+Shift+M",
  });
  await extension.awaitMessage("done");

  acceltext = menuitem.getAttribute("acceltext");
  ok(acceltext.endsWith("M"), "The menuitem accel text has been updated");

  await extension.unload();
});

add_task(async function test_extension_sidebar_shortcuts() {
  // TODO: Bug 1905766 - we can remove the push/pop pref and the need for a new
  // window in this test once the new sidebar is enabled by default.
  await SpecialPowers.pushPrefEnv({ set: [["sidebar.revamp", true]] });

  const extension = ExtensionTestUtils.loadExtension({
    useAddonManager: "temporary",
    manifest: {
      commands: {
        _execute_sidebar_action: {
          suggested_key: {
            default: "F1",
          },
        },
      },
      sidebar_action: {
        default_panel: "sidebar.html",
      },
    },
    background() {
      browser.test.onMessage.addListener(async (msg, data) => {
        if (msg == "updateShortcut") {
          await browser.commands.update(data);
          return browser.test.sendMessage("done");
        }
        throw new Error("Unknown message");
      });
    },
    files: {
      "sidebar.html": `
        <!DOCTYPE html>
        <html>
        <head><meta charset="utf-8"/>
        <script src="sidebar.js"></script>
        </head>
        <body>A Test Sidebar</body>
        </html>
      `,

      "sidebar.js": function () {
        window.onload = () => {
          browser.test.sendMessage("sidebar");
        };
      },
    },
  });
  await extension.startup();
  // The sidebar panel is shown in the initial window.
  await extension.awaitMessage("sidebar");

  const win = await BrowserTestUtils.openNewBrowserWindow();
  // The sidebar panel is shown in the new window.
  await extension.awaitMessage("sidebar");
  const sidebar = win.document.querySelector("sidebar-main");
  ok(sidebar, "sidebar is shown");
  ok(win.SidebarController.isOpen, "sidebar panel is open by default");

  // The sidebar panel is open by default, so let's just close it.
  EventUtils.synthesizeKey("KEY_F1", {}, win);
  await win.SidebarController.waitUntilStable();
  ok(!win.SidebarController.isOpen, "sidebar is closed");

  // Update shortcut.
  extension.sendMessage("updateShortcut", {
    name: "_execute_sidebar_action",
    shortcut: "F2",
  });
  await extension.awaitMessage("done");

  // Re-open the sidebar panel with the new shortcut.
  EventUtils.synthesizeKey("KEY_F2", {}, win);
  await win.SidebarController.waitUntilStable();
  // The sidebar panel is shown again.
  await extension.awaitMessage("sidebar");
  ok(win.SidebarController.isOpen, "sidebar is open");

  await extension.unload();
  await BrowserTestUtils.closeWindow(win);

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_extended_function_keys() {
  const extension = ExtensionTestUtils.loadExtension({
    useAddonManager: "permanent",
    manifest: {
      version: "1.0",
      browser_specific_settings: { gecko: { id: "commands@mochi.test" } },
      commands: {
        foo: {
          suggested_key: {
            default: "Alt+Shift+F12",
          },
          description: "The foo command",
        },
      },
    },
    background() {
      browser.test.onMessage.addListener(async (msg, data) => {
        if (msg == "update") {
          await browser.commands.update(data);
          return browser.test.sendMessage("updateDone");
        }
      });
      browser.commands.onCommand.addListener(name =>
        browser.test.sendMessage("oncommand", name)
      );
      browser.test.sendMessage("bgpage:ready");
    },
  });

  await extension.startup();
  await extension.awaitMessage("bgpage:ready");

  // Sanity check.
  info("Verify command listener called on original manifest-assigned shortcut");
  EventUtils.synthesizeKey("VK_F12", { altKey: true, shiftKey: true });
  is(
    await extension.awaitMessage("oncommand"),
    "foo",
    "Expect onCommand listener call for command foo on Alt+Shift+F12"
  );

  info("Update foo command shortcut to be set to Alt+Shift+F19");
  extension.sendMessage("update", { name: "foo", shortcut: "Alt+Shift+F19" });
  await extension.awaitMessage("updateDone");

  info("Verify command listener called on extension-updated shortcut");
  EventUtils.synthesizeKey("VK_F19", { altKey: true, shiftKey: true });
  is(
    await extension.awaitMessage("oncommand"),
    "foo",
    "Expect onCommand listener call for command foo on Alt+Shift+F19"
  );

  await extension.unload();
});
