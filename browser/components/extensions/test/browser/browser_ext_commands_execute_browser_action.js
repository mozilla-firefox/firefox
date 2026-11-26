"use strict";

ChromeUtils.defineESModuleGetters(this, {
  ExtensionSettingsStore:
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs",
});

async function testExecuteBrowserActionWithOptions(options = {}) {
  // Make sure the mouse isn't hovering over the browserAction widget.
  EventUtils.synthesizeMouseAtCenter(gURLBar, { type: "mouseover" }, window);

  let extensionOptions = {};

  let browser_action =
    options.manifest_version > 2 ? "action" : "browser_action";
  let browser_action_key = options.manifest_version > 2 ? "a" : "j";

  // We accept any command in the manifest, so here we add commands for
  // both V2 and V3, but only the command that matches the manifest version
  // should ever work.
  extensionOptions.manifest = {
    manifest_version: options.manifest_version || 2,
    commands: {
      _execute_browser_action: {
        suggested_key: {
          default: "Alt+Shift+J",
        },
      },
      _execute_action: {
        suggested_key: {
          default: "Alt+Shift+A",
        },
      },
    },
    [browser_action]: {
      browser_style: true,
    },
  };

  if (options.withPopup) {
    extensionOptions.manifest[browser_action].default_popup = "popup.html";

    extensionOptions.files = {
      "popup.html": `
        <!DOCTYPE html>
        <html>
          <head>
            <meta charset="utf-8">
            <script src="popup.js"></script>
          </head>
        </html>
      `,

      "popup.js": function () {
        browser.runtime.sendMessage("from-browser-action-popup");
      },
    };
  }

  extensionOptions.background = () => {
    let manifest = browser.runtime.getManifest();
    let { manifest_version } = manifest;
    const action = manifest_version < 3 ? "browserAction" : "action";

    browser.test.onMessage.addListener((message, options) => {
      browser.commands.onCommand.addListener(commandName => {
        if (
          ["_execute_browser_action", "_execute_action"].includes(commandName)
        ) {
          browser.test.assertEq(
            commandName,
            options.expectedCommand,
            `The onCommand listener fired for ${commandName}.`
          );
          browser[action].openPopup();
        }
      });

      if (!options.expectedCommand) {
        browser[action].onClicked.addListener(() => {
          if (options.withPopup) {
            browser.test.fail(
              "The onClick listener should never fire if the browserAction has a popup."
            );
            browser.test.notifyFail("execute-browser-action-on-clicked-fired");
          } else {
            browser.test.notifyPass("execute-browser-action-on-clicked-fired");
          }
        });
      }

      browser.runtime.onMessage.addListener(msg => {
        if (msg == "from-browser-action-popup") {
          browser.test.notifyPass("execute-browser-action-popup-opened");
        }
      });

      browser.test.sendMessage("send-keys");
    });
  };

  let extension = ExtensionTestUtils.loadExtension(extensionOptions);

  extension.onMessage("send-keys", () => {
    EventUtils.synthesizeKey(browser_action_key, {
      altKey: true,
      shiftKey: true,
    });
  });

  await extension.startup();

  await SimpleTest.promiseFocus(window);

  if (options.inArea) {
    let widget = getBrowserActionWidget(extension);
    CustomizableUI.addWidgetToArea(widget.id, options.inArea);
  }

  // The popup script can run before the panel is shown.
  let panelShown = options.withPopup && awaitExtensionPanel(extension);

  extension.sendMessage("options", options);

  if (options.withPopup) {
    await extension.awaitFinish("execute-browser-action-popup-opened");
    await panelShown;
    await closeBrowserAction(extension);
  } else {
    await extension.awaitFinish("execute-browser-action-on-clicked-fired");
  }
  await extension.unload();
}

add_task(async function test_execute_browser_action_with_popup() {
  await testExecuteBrowserActionWithOptions({
    withPopup: true,
  });
});

add_task(async function test_execute_browser_action_without_popup() {
  await testExecuteBrowserActionWithOptions();
});

add_task(async function test_execute_browser_action_command() {
  await testExecuteBrowserActionWithOptions({
    withPopup: true,
    expectedCommand: "_execute_browser_action",
  });
});

add_task(async function test_execute_action_with_popup() {
  await testExecuteBrowserActionWithOptions({
    withPopup: true,
    manifest_version: 3,
  });
});

add_task(async function test_execute_action_without_popup() {
  await testExecuteBrowserActionWithOptions({
    manifest_version: 3,
  });
});

add_task(async function test_execute_action_command() {
  await testExecuteBrowserActionWithOptions({
    withPopup: true,
    expectedCommand: "_execute_action",
  });
});

add_task(
  async function test_execute_browser_action_in_hamburger_menu_with_popup() {
    await testExecuteBrowserActionWithOptions({
      withPopup: true,
      inArea: getCustomizableUIPanelID(),
    });
  }
);

add_task(
  async function test_execute_browser_action_in_hamburger_menu_without_popup() {
    await testExecuteBrowserActionWithOptions({
      inArea: getCustomizableUIPanelID(),
    });
  }
);

add_task(async function test_fallback_to_execute_browser_action_in_mv3() {
  // Make sure the mouse isn't hovering over the browserAction widget.
  EventUtils.synthesizeMouseAtCenter(gURLBar, { type: "mouseover" }, window);

  const EXTENSION_ID = "@test-action";
  const extMV2 = ExtensionTestUtils.loadExtension({
    manifest: {
      manifest_version: 2,
      browser_action: {},
      browser_specific_settings: { gecko: { id: EXTENSION_ID } },
      commands: {
        _execute_browser_action: {
          suggested_key: {
            default: "Alt+1",
          },
        },
      },
    },
    async background() {
      await browser.commands.update({
        name: "_execute_browser_action",
        shortcut: "Alt+Shift+2",
      });
      browser.test.sendMessage("command-update");
    },
    useAddonManager: "temporary",
  });
  await extMV2.startup();
  await extMV2.awaitMessage("command-update");

  let storedCommands = ExtensionSettingsStore.getAllForExtension(
    EXTENSION_ID,
    "commands"
  );
  Assert.deepEqual(
    storedCommands,
    ["_execute_browser_action"],
    "expected a stored command"
  );

  const extDataMV3 = {
    manifest: {
      manifest_version: 3,
      action: {},
      browser_specific_settings: { gecko: { id: EXTENSION_ID } },
      commands: {
        _execute_action: {
          suggested_key: {
            default: "Alt+3",
          },
        },
      },
    },
    background() {
      browser.action.onClicked.addListener(() => {
        browser.test.notifyPass("execute-action-on-clicked-fired");
      });

      browser.test.onMessage.addListener(async (msg, data) => {
        switch (msg) {
          case "verify": {
            const commands = await browser.commands.getAll();
            browser.test.assertDeepEq(
              data,
              commands,
              "expected correct commands"
            );
            browser.test.sendMessage(`${msg}-done`);
            break;
          }

          case "update":
            await browser.commands.update(data);
            browser.test.sendMessage(`${msg}-done`);
            break;

          case "reset":
            await browser.commands.reset("_execute_action");
            browser.test.sendMessage(`${msg}-done`);
            break;

          default:
            browser.test.fail(`unexpected message: ${msg}`);
        }
      });

      browser.test.sendMessage("ready");
    },
    useAddonManager: "temporary",
  };
  const extMV3 = ExtensionTestUtils.loadExtension(extDataMV3);
  await extMV3.startup();
  await extMV3.awaitMessage("ready");

  // We should have the shortcut value from the previous extension.
  extMV3.sendMessage("verify", [
    {
      name: "_execute_action",
      description: null,
      shortcut: "Alt+Shift+2",
    },
  ]);
  await extMV3.awaitMessage("verify-done");

  // Execute the shortcut from the MV2 extension.
  await SimpleTest.promiseFocus(window);
  EventUtils.synthesizeKey("2", {
    altKey: true,
    shiftKey: true,
  });
  await extMV3.awaitFinish("execute-action-on-clicked-fired");

  // Update the shortcut.
  extMV3.sendMessage("update", {
    name: "_execute_action",
    shortcut: "Alt+Shift+4",
  });
  await extMV3.awaitMessage("update-done");

  extMV3.sendMessage("verify", [
    {
      name: "_execute_action",
      description: null,
      shortcut: "Alt+Shift+4",
    },
  ]);
  await extMV3.awaitMessage("verify-done");

  // At this point, we should have the old and new commands in storage.
  storedCommands = ExtensionSettingsStore.getAllForExtension(
    EXTENSION_ID,
    "commands"
  );
  Assert.deepEqual(
    storedCommands,
    ["_execute_browser_action", "_execute_action"],
    "expected two stored commands"
  );

  // Disarm any pending writes before we modify the JSONFile directly.
  await ExtensionSettingsStore._reloadFile(
    true // saveChanges
  );

  let jsonFileName = "extension-settings.json";
  let storePath = PathUtils.join(PathUtils.profileDir, jsonFileName);

  let settingsStoreData = await IOUtils.readJSON(storePath);
  Assert.deepEqual(
    Array.from(Object.keys(settingsStoreData.commands)),
    ["_execute_browser_action", "_execute_action"],
    "expected command hortcuts data to be found in extension-settings.json"
  );

  // Reverse the order of _execute_action and _execute_browser_action stored
  // in the settings store.
  settingsStoreData.commands = {
    _execute_action: settingsStoreData.commands._execute_action,
    _execute_browser_action: settingsStoreData.commands._execute_browser_action,
  };

  Assert.deepEqual(
    Array.from(Object.keys(settingsStoreData.commands)),
    ["_execute_action", "_execute_browser_action"],
    "expected command shortcuts order to be reversed in extension-settings.json data"
  );

  // Write the extension-settings.json data and reload it.
  await IOUtils.writeJSON(storePath, settingsStoreData);
  await ExtensionSettingsStore._reloadFile(
    false // saveChanges
  );

  // Restart the extension to verify that the loaded command is the right one.
  const updatedExtMV3 = ExtensionTestUtils.loadExtension(extDataMV3);
  await updatedExtMV3.startup();
  await updatedExtMV3.awaitMessage("ready");

  // We should *still* have two stored commands.
  storedCommands = ExtensionSettingsStore.getAllForExtension(
    EXTENSION_ID,
    "commands"
  );
  Assert.deepEqual(
    storedCommands,
    ["_execute_action", "_execute_browser_action"],
    "expected two stored commands"
  );

  updatedExtMV3.sendMessage("verify", [
    {
      name: "_execute_action",
      description: null,
      shortcut: "Alt+Shift+4",
    },
  ]);
  await updatedExtMV3.awaitMessage("verify-done");

  updatedExtMV3.sendMessage("reset");
  await updatedExtMV3.awaitMessage("reset-done");

  // Resetting the shortcut should take the default value from the latest
  // extension version.
  updatedExtMV3.sendMessage("verify", [
    {
      name: "_execute_action",
      description: null,
      shortcut: "Alt+3",
    },
  ]);
  await updatedExtMV3.awaitMessage("verify-done");

  // At this point, we should no longer have any stored commands, since we are
  // using the default.
  storedCommands = ExtensionSettingsStore.getAllForExtension(
    EXTENSION_ID,
    "commands"
  );
  Assert.deepEqual(storedCommands, [], "expected no stored command");

  await extMV2.unload();
  await extMV3.unload();
  await updatedExtMV3.unload();
});
