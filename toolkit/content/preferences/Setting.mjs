/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  AsyncSetting,
  AsyncSettingHandler,
} from "chrome://global/content/preferences/AsyncSetting.mjs";
import { Preferences } from "chrome://global/content/preferences/Preferences.mjs";

/**
 * @import { type Preference } from "chrome://global/content/preferences/Preference.mjs"
 * @import { SettingControlConfig } from "chrome://browser/content/preferences/widgets/setting-control.mjs"
 * @import { ExtensionSettingsStore } from "resource://gre/modules/ExtensionSettingsStore.sys.mjs"
 * @import { AddonManager } from "resource://gre/modules/AddonManager.sys.mjs"
 * @import { Management } from "resource://gre/modules/Extension.sys.mjs"
 */

/**
 * A map of Setting instances (values) along with their IDs
 * (keys) so that the dependencies of a setting can
 * be easily looked up by just their ID.
 *
 * @typedef {Record<string, Setting>} SettingDeps
 */

/**
 * @typedef {string | boolean | number | nsIFile | null} SettingValue
 */

/**
 * @callback SettingVisibleCallback
 * @param {SettingDeps} deps
 * @param {Setting} setting
 * @returns {any} If truthy shows the setting in the UI, or hides it if not
 */

/**
 * Gets the value of a {@link Setting}.
 *
 * @callback SettingGetCallback
 * @param {any} val - The value that was retrieved from the preferences backend
 * @param {SettingDeps} deps
 * @param {Setting} setting
 * @returns {any} - The value to set onto the setting
 */

/**
 * Sets the value of a {@link Setting}. If the SettingConfig has {@link SettingConfig.pref} set
 * use this callback to transform the control's value to the literal pref value, this
 * would likely be the inverse of {@link SettingConfig.get}. If {@link SettingConfig.pref} is not set,
 * this should store the value manually in a fashion that can be retrieved in {@link SettingConfig.get}.
 *
 * @callback SettingSetCallback
 * @param {SettingValue} val - The value/pressed/checked from the input (control) associated with the setting
 * @param {SettingDeps} deps
 * @param {Setting} setting
 * @returns {SettingValue}
 */

/**
 * @callback SettingOnUserChangeCallback
 * @param {SettingValue} val - The value/pressed/checked from the input of the control associated with the setting
 * @param {SettingDeps} deps
 * @param {Setting} setting
 * @returns {void}
 */

/**
 * @callback SettingDisabledCallback
 * @param {SettingDeps} deps
 * @param {Setting} setting
 * @returns {any}
 */

/**
 * @callback SettingGetControlConfigCallback
 * @param {SettingControlConfig} config
 * @param {SettingDeps} deps
 * @param {Setting} setting
 * @returns {SettingControlConfig}
 */

/**
 * @callback SettingTeardownCallback
 * @returns {void}
 */

/**
 * @callback SettingEmitChange
 */

/**
 * @callback SettingSetupCallback
 * @param {SettingEmitChange} emitChange Notify listeners of a change to this setting.
 * @param {SettingDeps} deps
 * @param {Setting} setting
 * @returns {SettingTeardownCallback | void}
 */

/**
 * @callback SettingOnUserClickCallback
 * @param {MouseEvent} event
 * @param {SettingDeps} deps
 * @param {Setting} setting
 * @returns {void}
 */

/**
 * @callback SettingOnMessageBarDismissCallback
 * @param {CustomEvent} event - The dismiss event
 * @param {SettingDeps} deps
 * @param {Setting} setting
 * @returns {void}
 */

/**
 * @callback SettingOnUserReorderCallback
 * @param {CustomEvent} event - The reorder event with detail containing draggedElement, targetElement, position, draggedIndex, targetIndex
 * @param {SettingDeps} deps
 * @param {Setting} setting
 * @returns {void}
 */

/**
 * @typedef {object} SettingControllingExtensionInfo
 * @property {string} storeId The ExtensionSettingsStore id that controls this setting.
 * @property {string} l10nId A fluent id to show in a controlled by extension message.
 * @property {string} [name] The controlling extension's name.
 * @property {string} [id] The controlling extension's id.
 * @property {string} [supportPage] A support page to show in the message.
 * @property {boolean} [allowControl] If the control should be enabled while controlled.
 * @property {boolean} [mayDisable] If the controlling extension may be disabled.
 */

/**
 * @typedef {object} SettingConfig
 * @property {string} id - The ID for the Setting, this should match the layout id
 * @property {string} [pref] - A {@link Services.prefs} id that will be used as the backend if it is provided
 * @property {string[]} [deps] - An array of setting IDs that this setting depends on, when these settings change this setting will emit a change event to update the UI
 * @property {Pick<SettingControllingExtensionInfo, "storeId" | "l10nId" | "allowControl" | "supportPage">} [controllingExtensionInfo] Data related to the setting being controlled by an extension.
 * @property {SettingVisibleCallback} [visible] - Function to determine if a setting is visible in the UI
 * @property {SettingGetCallback} [get] - Function to get the value of the setting. Optional if {@link SettingConfig#pref} is set.
 * @property {SettingSetCallback} [set] - See {@link SettingSetCallback}
 * @property {SettingGetControlConfigCallback} [getControlConfig] -  Function that allows the setting to modify its layout, this is intended to be used to provide the options, {@link SettingConfig#l10nId} or {@link SettingConfig#l10nArgs} data if necessary, but technically it can change anything (that doesn't mean it will have any effect though).
 * @property {SettingOnUserChangeCallback} [onUserChange] - A function that will be called when the setting
 *    has been modified by the user, it is passed the value/pressed/checked from its input. NOTE: This should be used for
 *    additional work that needs to happen, such as recording telemetry.
 *    If you want to set the value of the Setting then use the {@link SettingConfig.set} function.
 * @property {SettingSetupCallback} [setup] -  A function to be called to register listeners for
 *    the setting. It should return a {@link SettingTeardownCallback} function to
 *    remove the listeners if necessary. This should emit change events when the setting has changed to
 *    ensure the UI stays in sync if possible.
 * @property {SettingDisabledCallback} [disabled] - A function to determine if a setting should be disabled
 * @property {SettingOnUserClickCallback} [onUserClick] - A function that will be called when a setting has been
 *    clicked, the element name must be included in the CLICK_HANDLERS array
 *    in {@link file://./../../browser/components/preferences/widgets/setting-group/setting-group.mjs}. This should be
 *    used for controls that aren't regular form controls but instead perform an action when clicked, like a button or link.
 * @property {SettingOnMessageBarDismissCallback} [onMessageBarDismiss] - A function that will be called when a message bar has been
 *    dismissed. This should be used for moz-message-bar to override the default behavior.
 * @property {SettingOnUserReorderCallback} [onUserReorder] - A function that will be called when items in a
 *    reorderable list have been reordered. This should be used to update the underlying data when the user
 *    reorders items, such as updating preference values.
 */

const { EventEmitter } = ChromeUtils.importESModule(
  "resource://gre/modules/EventEmitter.sys.mjs"
);

/** @type {{ ExtensionSettingsStore: typeof ExtensionSettingsStore, AddonManager: typeof AddonManager, Management: typeof Management }} */
// @ts-expect-error bug 1996860
const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ExtensionSettingsStore:
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs",
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  Management: "resource://gre/modules/Extension.sys.mjs",
});

class PreferenceNotAddedError extends Error {
  /**
   * @param {string} settingId
   * @param {string} prefId
   */
  constructor(settingId, prefId) {
    super(
      `Setting "${settingId}" was unable to find Preference "${prefId}". Did you register it with Preferences.add/addAll?`
    );
    this.name = "PreferenceNotAddedError";
    this.settingId = settingId;
    this.prefId = prefId;
  }
}

export class Setting extends EventEmitter {
  /**
   * @type {Preference}
   */
  _pref;

  /**
   * Keeps a cache of each dep's Setting so that
   * it can be easily looked up by its ID.
   *
   * @type {SettingDeps}
   */
  _deps;

  /**
   * @type {SettingConfig | AsyncSettingHandler}
   */
  config;

  get pref() {
    return this._pref;
  }

  set pref(newPref) {
    if (this._pref) {
      this._pref.off("change", this.onChange);
    }

    this._pref = newPref;

    if (this._pref) {
      this._pref.on("change", this.onChange);
    }
  }

  /**
   * @param {SettingConfig['id']} id
   * @param {SettingConfig | typeof AsyncSetting} config
   * @throws {Error} Will throw an error (PreferenceNotAddedError) if
   *    config.pref was not registered
   */
  constructor(id, config) {
    super();

    /** @type {SettingConfig | AsyncSettingHandler} */
    let configObj;

    if (Object.getPrototypeOf(config) == AsyncSetting) {
      configObj = new AsyncSettingHandler(
        id,
        /** @type {typeof AsyncSetting} */ (config)
      );
    } else {
      configObj = config;
    }

    this.id = id;
    this.config = configObj;
    this.pref = configObj.pref && Preferences.get(configObj.pref);
    if (configObj.pref && !this.pref) {
      throw new PreferenceNotAddedError(id, configObj.pref);
    }
    this._emitting = false;

    /** @type {SettingControllingExtensionInfo} */
    this.controllingExtensionInfo = {
      ...this.config.controllingExtensionInfo,
    };
    if (this.config.controllingExtensionInfo?.storeId) {
      this._checkForControllingExtension();
      this.watchExtensionPrefChange();
    }
    if (typeof this.config.setup === "function") {
      this._teardown = this.config.setup(this.onChange, this.deps, this);
    }
  }

  onChange = () => {
    if (this._emitting) {
      return;
    }
    this._emitting = true;
    this.emit("change");
    this._emitting = false;
  };

  /**
   * A map of each dep and it's associated {@link Setting} instance.
   *
   * @type {SettingDeps}
   */
  get deps() {
    if (this._deps) {
      return this._deps;
    }
    /**
     * @type {SettingDeps}
     */
    const deps = {};

    if (this.config.deps) {
      for (let id of this.config.deps) {
        const setting = Preferences.getSetting(id);
        if (setting) {
          deps[id] = setting;
        }
      }
    }
    this._deps = deps;

    for (const setting of Object.values(this._deps)) {
      setting.on("change", this.onChange);
    }

    return this._deps;
  }

  /**
   * @type {SettingValue}
   */
  get value() {
    let prefVal = this.pref?.value;
    if (this.config.get) {
      return this.config.get(prefVal, this.deps, this);
    }
    return prefVal;
  }

  /**
   * @param {SettingValue} val
   */
  set value(val) {
    let newVal = this.config.set ? this.config.set(val, this.deps, this) : val;
    if (this.pref && !(newVal instanceof Object && "then" in newVal)) {
      this.pref.value = newVal;
    }
  }

  /**
   * @type {boolean}
   */
  get locked() {
    return this.pref?.locked ?? false;
  }

  get visible() {
    return this.config.visible ? this.config.visible(this.deps, this) : true;
  }

  get disabled() {
    return this.config.disabled ? this.config.disabled(this.deps, this) : false;
  }

  /**
   * @param {SettingControlConfig} config
   * @returns {SettingControlConfig}
   */
  getControlConfig(config) {
    if (this.config.getControlConfig) {
      return this.config.getControlConfig(config, this.deps, this);
    }
    return config;
  }

  /**
   * @param {MouseEvent} event
   */
  userClick(event) {
    if (this.config.onUserClick) {
      this.config.onUserClick(event, this.deps, this);
    }
  }

  /**
   * @param {CustomEvent} event
   */
  messageBarDismiss(event) {
    if (this.config.onMessageBarDismiss) {
      this.config.onMessageBarDismiss(event, this.deps, this);
    }
  }

  /**
   * @param {CustomEvent} event
   */
  userReorder(event) {
    if (this.config.onUserReorder) {
      this.config.onUserReorder(event, this.deps, this);
    }
  }

  /**
   * @param {string} val
   */
  userChange(val) {
    this.value = val;
    if (this.config.onUserChange) {
      this.config.onUserChange(val, this.deps, this);
    }
  }

  async disableControllingExtension() {
    if (
      this.controllingExtensionInfo.name &&
      this.controllingExtensionInfo.id
    ) {
      await lazy.ExtensionSettingsStore.initialize();
      let { id } = lazy.ExtensionSettingsStore.getSetting(
        "prefs",
        this.controllingExtensionInfo.storeId
      );
      if (id) {
        let addon = await lazy.AddonManager.getAddonByID(id);
        await addon.disable();
      }
    }
  }

  _checkForControllingExtension = async () => {
    // Make sure all settings API modules are loaded
    // and the extension controlling settings metadata
    // loaded from the ExtensionSettingsStore backend.
    await lazy.Management.asyncLoadSettingsModules();
    await lazy.ExtensionSettingsStore.initialize();
    // Retrieve the extension controlled settings info
    // for the given setting storeId.
    let info = lazy.ExtensionSettingsStore.getSetting(
      "prefs",
      this.config.controllingExtensionInfo?.storeId
    );
    if (info && info.id) {
      let addon = await lazy.AddonManager.getAddonByID(info.id);
      if (addon) {
        this.controllingExtensionInfo.name = addon.name;
        this.controllingExtensionInfo.id = info.id;
        this.controllingExtensionInfo.mayDisable = !!(
          addon.permissions & lazy.AddonManager.PERM_CAN_DISABLE
        );
        this.emit("change");
        return;
      }
    }
    this._clearControllingExtensionInfo();
  };

  _clearControllingExtensionInfo() {
    delete this.controllingExtensionInfo.id;
    delete this.controllingExtensionInfo.name;
    delete this.controllingExtensionInfo.mayDisable;
    delete this.controllingExtensionInfo.supportPage;
    // Request an update to the setting control so the UI is in the correct state
    this.onChange();
  }

  watchExtensionPrefChange() {
    lazy.Management.on(
      `extension-setting-changed:${this.config.controllingExtensionInfo?.storeId}`,
      this._checkForControllingExtension
    );
  }

  destroy() {
    if (typeof this._teardown === "function") {
      this._teardown();
      this._teardown = null;
    }

    if (this.pref) {
      this.pref.off("change", this.onChange);
    }

    if (this.config.controllingExtensionInfo?.storeId) {
      lazy.Management.off(
        `extension-setting-changed:${this.config.controllingExtensionInfo?.storeId}`,
        this._checkForControllingExtension
      );
    }
  }
}
