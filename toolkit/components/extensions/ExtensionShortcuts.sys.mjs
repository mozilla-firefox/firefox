/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { ExtensionCommon } from "resource://gre/modules/ExtensionCommon.sys.mjs";

import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  ExtensionParent: "resource://gre/modules/ExtensionParent.sys.mjs",
  ExtensionSettingsStore:
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  ShortcutUtils: "resource://gre/modules/ShortcutUtils.sys.mjs",
  global: () => lazy.ExtensionParent.apiManager.global,
});

const { ExtensionError, DefaultMap } = ExtensionUtils;
const { makeWidgetId } = ExtensionCommon;

const EXECUTE_SIDEBAR_ACTION = "_execute_sidebar_action";

function normalizeShortcut(shortcut) {
  return shortcut ? shortcut.replace(/\s+/g, "") : "";
}

export class ExtensionShortcutKeyMap extends DefaultMap {
  async buildForAddonIds(addonIds) {
    this.clear();
    for (const addonId of addonIds) {
      const policy = WebExtensionPolicy.getByID(addonId);
      if (policy?.extension?.shortcuts) {
        const { shortcuts } = policy.extension;
        for (const command of await shortcuts.allCommands()) {
          this.recordShortcut(command.shortcut, policy.name, command.name);
        }
      }
    }
  }

  recordShortcut(shortcutString, addonName, commandName) {
    if (!shortcutString) {
      return;
    }

    const valueSet = this.get(shortcutString);
    valueSet.add({ addonName, commandName });
  }

  removeShortcut(shortcutString, addonName, commandName) {
    if (!this.has(shortcutString)) {
      return;
    }

    const valueSet = this.get(shortcutString);
    for (const entry of valueSet.values()) {
      if (entry.addonName === addonName && entry.commandName === commandName) {
        valueSet.delete(entry);
      }
    }
    if (valueSet.size === 0) {
      this.delete(shortcutString);
    }
  }

  getFirstAddonName(shortcutString) {
    if (this.has(shortcutString)) {
      return this.get(shortcutString).values().next().value.addonName;
    }
    return null;
  }

  has(shortcutString) {
    const platformShortcut = this.getPlatformShortcutString(shortcutString);
    return super.has(platformShortcut) && super.get(platformShortcut).size > 0;
  }

  // Class internals.

  constructor() {
    super(() => new Set());

    // Overridden in some unit test to make it easier to cover some
    // platform specific behaviors (in particular the platform specific.
    // normalization of the shortcuts using the Ctrl modifier on macOS).
    this._os = lazy.ExtensionParent.PlatformInfo.os;
  }

  getPlatformShortcutString(shortcutString) {
    if (this._os == "mac") {
      // when running on macos, make sure to also track in the shortcutKeyMap
      // (which is used to check for duplicated shortcuts) a shortcut string
      // that replace the `Ctrl` modifiers with the `Command` modified:
      // they are going to be the same accel in the key element generated,
      // by tracking both of them shortcut string value would confuse the about:addons "Manager Shortcuts"
      // view and make it unable to correctly catch conflicts on mac
      // (See bug 1565854).
      shortcutString = shortcutString
        .split("+")
        .map(p => (p === "Ctrl" ? "Command" : p))
        .join("+");
    }

    return shortcutString;
  }

  get(shortcutString) {
    const platformShortcut = this.getPlatformShortcutString(shortcutString);
    return super.get(platformShortcut);
  }

  add(shortcutString, addonCommandValue) {
    const setValue = this.get(shortcutString);
    setValue.add(addonCommandValue);
  }

  delete(shortcutString) {
    const platformShortcut = this.getPlatformShortcutString(shortcutString);
    return super.delete(platformShortcut);
  }
}

/**
 * An instance of this class is assigned to the shortcuts property of each
 * active webextension that has commands defined.
 *
 * It manages loading any updated shortcuts along with the ones defined in
 * the manifest and registering them to a browser window. It also provides
 * the list, update and reset APIs for the browser.commands interface and
 * the about:addons manage shortcuts page.
 */
export class ExtensionShortcuts {
  static async removeCommandsFromStorage(extensionId) {
    // Cleanup the updated commands. In some cases the extension is installed
    // and uninstalled so quickly that `this.commands` hasn't loaded yet. To
    // handle that we need to make sure ExtensionSettingsStore is initialized
    // before we clean it up.
    await lazy.ExtensionSettingsStore.initialize();
    lazy.ExtensionSettingsStore.getAllForExtension(
      extensionId,
      "commands"
    ).forEach(key => {
      lazy.ExtensionSettingsStore.removeSetting(extensionId, "commands", key);
    });
  }

  constructor({ extension, onCommand, onShortcutChanged }) {
    this.keysetsMap = new WeakMap();
    this.windowOpenListener = null;
    this.extension = extension;
    this.onCommand = onCommand;
    this.onShortcutChanged = onShortcutChanged;
    this.id = makeWidgetId(extension.id);
  }

  async allCommands() {
    let commands = await this.commands;
    return Array.from(commands, ([name, command]) => {
      return {
        name,
        description: command.description,
        shortcut: command.shortcut,
      };
    });
  }

  async updateCommand({ name, description, shortcut }) {
    let { extension } = this;
    let commands = await this.commands;
    let command = commands.get(name);

    if (!command) {
      throw new ExtensionError(`Unknown command "${name}"`);
    }

    // Only store the updates so manifest changes can take precedence
    // later.
    let previousUpdates = await lazy.ExtensionSettingsStore.getSetting(
      "commands",
      name,
      extension.id
    );
    let commandUpdates = (previousUpdates && previousUpdates.value) || {};

    if (description && description != command.description) {
      commandUpdates.description = description;
      command.description = description;
    }

    let oldShortcut = command.shortcut;

    if (shortcut != null && shortcut != command.shortcut) {
      shortcut = normalizeShortcut(shortcut);
      commandUpdates.shortcut = shortcut;
      command.shortcut = shortcut;
    }

    await lazy.ExtensionSettingsStore.addSetting(
      extension.id,
      "commands",
      name,
      commandUpdates
    );

    this.registerKeys(commands);

    if (command.shortcut !== oldShortcut) {
      this.onShortcutChanged({
        name,
        newShortcut: command.shortcut,
        oldShortcut,
      });
    }
  }

  async resetCommand(name) {
    let { extension, manifestCommands } = this;
    let commands = await this.commands;
    let command = commands.get(name);

    if (!command) {
      throw new ExtensionError(`Unknown command "${name}"`);
    }

    let storedCommand = lazy.ExtensionSettingsStore.getSetting(
      "commands",
      name,
      extension.id
    );

    if (storedCommand && storedCommand.value) {
      commands.set(name, { ...manifestCommands.get(name) });

      lazy.ExtensionSettingsStore.removeSetting(extension.id, "commands", name);
      if (
        name === "_execute_action" &&
        extension.manifestVersion > 2 &&
        lazy.ExtensionSettingsStore.hasSetting(
          extension.id,
          "commands",
          "_execute_browser_action"
        )
      ) {
        lazy.ExtensionSettingsStore.removeSetting(
          extension.id,
          "commands",
          "_execute_browser_action"
        );
      }

      this.registerKeys(commands);
    }
  }

  async openShortcutSettings() {
    let window = lazy.global.windowTracker.topWindow;
    if (!window) {
      throw new ExtensionError("No browser window available");
    }

    let { extension } = this;
    const viewId = `addons://shortcuts/${encodeURIComponent(extension.id)}`;
    await window.BrowserAddonUI.openAddonsMgr(viewId);
  }

  loadCommands() {
    let { extension } = this;

    // Map[{String} commandName -> {Object} commandProperties]
    this.manifestCommands = this.loadCommandsFromManifest(extension.manifest);

    this.commands = (async () => {
      // Deep copy the manifest commands to commands so we can keep the original
      // manifest commands and update commands as needed.
      let commands = new Map();
      this.manifestCommands.forEach((command, name) => {
        commands.set(name, { ...command });
      });

      // Update the manifest commands with the persisted updates from
      // browser.commands.update().
      let savedCommands = await this.loadCommandsFromStorage(extension.id);
      savedCommands.forEach((update, name) => {
        let command = commands.get(name);
        if (
          name === "_execute_browser_action" &&
          extension.manifestVersion > 2
        ) {
          // Ignore the old _execute_browser_action if there is data stored for
          // the new _execute_action command. Otherwise use the stored data for
          // `_execute_action` (since we renamed `_execute_browser_action` to
          // `_execute_action` in MV3).
          command = savedCommands.has("_execute_action")
            ? null
            : commands.get("_execute_action");
        }

        if (command) {
          // We will only update commands, not add them.
          Object.assign(command, update);
        }
      });

      return commands;
    })();
  }

  registerKeys(commands) {
    for (let window of lazy.global.windowTracker.browserWindows()) {
      this.registerKeysToDocument(window, commands);
    }
  }

  /**
   * Registers the commands to all open windows and to any which
   * are later created.
   */
  async register() {
    let commands = await this.commands;
    this.registerKeys(commands);

    this.windowOpenListener = window => {
      if (!this.keysetsMap.has(window)) {
        this.registerKeysToDocument(window, commands);
      }
    };

    lazy.global.windowTracker.addOpenListener(this.windowOpenListener);
  }

  /**
   * Unregisters the commands from all open windows and stops commands
   * from being registered to windows which are later created.
   */
  unregister() {
    for (let window of lazy.global.windowTracker.browserWindows()) {
      if (this.keysetsMap.has(window)) {
        this.keysetsMap.get(window).remove();
      }
    }

    lazy.global.windowTracker.removeOpenListener(this.windowOpenListener);
  }

  /**
   * Creates a Map from commands for each command in the manifest.commands object.
   *
   * @param {object} manifest The manifest JSON object.
   * @returns {Map<string, object>}
   */
  loadCommandsFromManifest(manifest) {
    let commands = new Map();
    // For Windows, chrome.runtime expects 'win' while chrome.commands
    // expects 'windows'.  We can special case this for now.
    let { PlatformInfo } = lazy.ExtensionParent;
    let os = PlatformInfo.os == "win" ? "windows" : PlatformInfo.os;
    for (let [name, command] of Object.entries(manifest.commands)) {
      let suggested_key = command.suggested_key || {};
      let shortcut = normalizeShortcut(
        suggested_key[os] || suggested_key.default
      );
      commands.set(name, {
        description: command.description,
        shortcut,
      });
    }
    return commands;
  }

  async loadCommandsFromStorage(extensionId) {
    await lazy.ExtensionSettingsStore.initialize();
    let names = lazy.ExtensionSettingsStore.getAllForExtension(
      extensionId,
      "commands"
    );
    return names.reduce((map, name) => {
      let command = lazy.ExtensionSettingsStore.getSetting(
        "commands",
        name,
        extensionId
      ).value;
      return map.set(name, command);
    }, new Map());
  }

  /**
   * Registers the commands to a document.
   *
   * @param {ChromeWindow} window The XUL window to insert the Keyset.
   * @param {Map} commands The commands to be set.
   */
  registerKeysToDocument(window, commands) {
    if (
      !this.extension.privateBrowsingAllowed &&
      lazy.PrivateBrowsingUtils.isWindowPrivate(window)
    ) {
      return;
    }

    let doc = window.document;
    let keyset = doc.createXULElement("keyset");
    keyset.id = `ext-keyset-id-${this.id}`;
    if (this.keysetsMap.has(window)) {
      this.keysetsMap.get(window).remove();
    }
    let sidebarKey;
    for (let [name, command] of commands) {
      if (command.shortcut) {
        let parts = command.shortcut.split("+");

        // The key is always the last element.
        let key = parts.pop();

        if (/^[0-9]$/.test(key)) {
          let shortcutWithNumpad = command.shortcut.replace(
            /[0-9]$/,
            "Numpad$&"
          );
          let numpadKeyElement = this.buildKey(doc, name, shortcutWithNumpad);
          keyset.appendChild(numpadKeyElement);
        }

        let keyElement = this.buildKey(doc, name, command.shortcut);
        keyset.appendChild(keyElement);
        if (name == EXECUTE_SIDEBAR_ACTION) {
          sidebarKey = keyElement;
        }
      }
    }
    doc.documentElement.appendChild(keyset);
    if (sidebarKey) {
      window.SidebarController.updateShortcut({ keyId: sidebarKey.id });
    }
    this.keysetsMap.set(window, keyset);
  }

  /**
   * Builds a XUL Key element and attaches an onCommand listener which
   * emits a command event with the provided name when fired.
   *
   * @param {Document} doc The XUL document.
   * @param {string} name The name of the command.
   * @param {string} shortcut The shortcut provided in the manifest.
   * @see https://developer.mozilla.org/en-US/docs/Mozilla/Tech/XUL/key
   *
   * @returns {Element} The newly created Key element.
   */
  buildKey(doc, name, shortcut) {
    let keyElement = this.buildKeyFromShortcut(doc, name, shortcut);

    /* eslint-disable mozilla/balanced-listeners */
    // We remove all references to the key elements when the extension is shutdown,
    // therefore the listeners for these elements will be garbage collected.
    keyElement.addEventListener("command", event => {
      let action;
      let _execute_action =
        this.extension.manifestVersion < 3
          ? "_execute_browser_action"
          : "_execute_action";

      let actionFor = {
        [_execute_action]: lazy.global.browserActionFor,
        _execute_page_action: lazy.global.pageActionFor,
        _execute_sidebar_action: lazy.global.sidebarActionFor,
      }[name];

      if (actionFor) {
        action = actionFor(this.extension);
        let win = event.target.ownerGlobal;
        action.triggerAction(win);
      } else {
        this.onCommand(name);
      }
    });
    /* eslint-enable mozilla/balanced-listeners */

    return keyElement;
  }

  /**
   * Builds a XUL Key element from the provided shortcut.
   *
   * @param {Document} doc The XUL document.
   * @param {string} name The name of the shortcut.
   * @param {string} shortcut The shortcut provided in the manifest.
   *
   * @see https://developer.mozilla.org/en-US/docs/Mozilla/Tech/XUL/key
   * @returns {Element} The newly created Key element.
   */
  buildKeyFromShortcut(doc, name, shortcut) {
    let keyElement = doc.createXULElement("key");

    let parts = shortcut.split("+");

    // The key is always the last element.
    let chromeKey = parts.pop();

    // The modifiers are the remaining elements.
    keyElement.setAttribute(
      "modifiers",
      lazy.ShortcutUtils.getModifiersAttribute(parts)
    );

    // A keyElement with key "NumpadX" is created above and isn't from the
    // manifest. The id will be set on the keyElement with key "X" only.
    if (name == EXECUTE_SIDEBAR_ACTION && !chromeKey.startsWith("Numpad")) {
      let id = `ext-key-id-${this.id}-sidebar-action`;
      keyElement.setAttribute("id", id);
    }

    let [attribute, value] = lazy.ShortcutUtils.getKeyAttribute(chromeKey);
    keyElement.setAttribute(attribute, value);
    if (attribute == "keycode") {
      keyElement.setAttribute("event", "keydown");
    }

    return keyElement;
  }
}
