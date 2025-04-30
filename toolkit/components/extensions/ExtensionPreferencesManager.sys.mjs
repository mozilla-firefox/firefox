/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

/**
 * @file
 * This module is used for managing preferences from WebExtension APIs.
 * It takes care of the precedence chain and decides whether a preference
 * needs to be updated when a change is requested by an API.
 *
 * It deals with preferences via settings objects, which are objects with
 * the following properties:
 *
 * prefNames:   An array of strings, each of which is a preference on
 *              which the setting depends.
 * setCallback: A function that returns an object containing properties and
 *              values that correspond to the prefs to be set.
 */

export let ExtensionPreferencesManager;

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { Management } from "resource://gre/modules/Extension.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  ExtensionCommon: "resource://gre/modules/ExtensionCommon.sys.mjs",
  ExtensionSettingsStore:
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs",
  Preferences: "resource://gre/modules/Preferences.sys.mjs",
  defaultPreferences: () => new lazy.Preferences({ defaultBranch: true }),
});

import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";

const { ExtensionError } = ExtensionUtils;

/* eslint-disable mozilla/balanced-listeners */
Management.on("uninstall", async (type, { id }) => {
  // Ensure managed preferences are cleared if they were
  // not cleared at the module level.
  await Management.asyncLoadSettingsModules();
  return ExtensionPreferencesManager.removeAll(id);
});

Management.on("disable", async (type, id) => {
  await Management.asyncLoadSettingsModules();
  return ExtensionPreferencesManager.disableAll(id);
});

Management.on("enabling", async (type, id) => {
  await Management.asyncLoadSettingsModules();
  return ExtensionPreferencesManager.enableAll(id);
});

Management.on("change-permissions", (type, change) => {
  // Called for added or removed, but we only care about removed here.
  if (!change.removed) {
    return;
  }
  ExtensionPreferencesManager.removeSettingsForPermissions(
    change.extensionId,
    change.removed.permissions
  );
});

/* eslint-enable mozilla/balanced-listeners */

const STORE_TYPE = "prefs";

// Definitions of settings, each of which correspond to a different API.
let settingsMap = new Map();

/**
 * This function is passed into the ExtensionSettingsStore to determine the
 * initial value of the setting. It reads an array of preference names from
 * the this scope, which gets bound to a settings object.
 *
 * @returns {object}
 *          An object with one property per preference, which holds the current
 *          value of that preference.
 */
function initialValueCallback() {
  let initialValue = {};
  for (let pref of this.prefNames) {
    // If there is a prior user-set value, get it.
    if (lazy.Preferences.isSet(pref)) {
      initialValue[pref] = lazy.Preferences.get(pref);
    }
  }
  return initialValue;
}

/**
 * Updates the initialValue stored to exclude any values that match
 * default preference values.
 *
 * @param {object} initialValue Initial Value data from settings store.
 * @returns {object}
 *          The initialValue object after updating the values.
 */
function settingsUpdate(initialValue) {
  for (let pref of this.prefNames) {
    try {
      if (
        initialValue[pref] !== undefined &&
        initialValue[pref] === lazy.defaultPreferences.get(pref)
      ) {
        initialValue[pref] = undefined;
      }
    } catch (e) {
      // Exception thrown if a default value doesn't exist.  We
      // presume that this pref had a user-set value initially.
    }
  }
  return initialValue;
}

/**
 * Loops through a set of prefs, either setting or resetting them.
 *
 * @param {string} name
 *        The api name of the setting.
 * @param {object} setting
 *        An object that represents a setting, which will have a setCallback
 *        property. If a onPrefsChanged function is provided it will be called
 *        with item when the preferences change.
 * @param {object} item
 *        An object that represents an item handed back from the setting store
 *        from which the new pref values can be calculated.
 */
function setPrefs(name, setting, item) {
  let prefs = item.initialValue || setting.setCallback(item.value);
  let changed = false;
  for (let pref of setting.prefNames) {
    if (prefs[pref] === undefined) {
      if (lazy.Preferences.isSet(pref)) {
        changed = true;
        lazy.Preferences.reset(pref);
      }
    } else if (lazy.Preferences.get(pref) != prefs[pref]) {
      lazy.Preferences.set(pref, prefs[pref]);
      changed = true;
    }
  }
  if (changed && typeof setting.onPrefsChanged == "function") {
    setting.onPrefsChanged(item);
  }
  Management.emit(`extension-setting-changed:${name}`);
}

/**
 * Commits a change to a setting and conditionally sets preferences.
 *
 * If the change to the setting causes a different extension to gain
 * control of the pref (or removes all extensions with control over the pref)
 * then the prefs should be updated, otherwise they should not be.
 * In addition, if the current value of any of the prefs does not
 * match what we expect the value to be (which could be the result of a
 * user manually changing the pref value), then we do not change any
 * of the prefs.
 *
 * @param {string} id
 *        The id of the extension for which a setting is being modified.  Also
 *        see selectSetting.
 * @param {string} name
 *        The name of the setting being processed.
 * @param {string} action
 *        The action that is being performed. Will be one of disable, enable
 *        or removeSetting.

 * @returns {Promise}
 *          Resolves to true if preferences were set as a result and to false
 *          if preferences were not set.
*/
async function processSetting(id, name, action) {
  await lazy.ExtensionSettingsStore.initialize();
  let expectedItem = lazy.ExtensionSettingsStore.getSetting(STORE_TYPE, name);
  let item = lazy.ExtensionSettingsStore[action](id, STORE_TYPE, name);
  if (item) {
    let setting = settingsMap.get(name);
    let expectedPrefs =
      expectedItem.initialValue || setting.setCallback(expectedItem.value);
    if (
      Object.keys(expectedPrefs).some(
        pref =>
          expectedPrefs[pref] &&
          lazy.Preferences.get(pref) != expectedPrefs[pref]
      )
    ) {
      return false;
    }
    setPrefs(name, setting, item);
    return true;
  }
  return false;
}

ExtensionPreferencesManager = {
  /**
   * Adds a setting to the settingsMap. This is how an API tells the
   * preferences manager what its setting object is. The preferences
   * manager needs to know this when settings need to be removed
   * automatically.
   *
   * @param {string} name The unique id of the setting.
   * @param {object} setting
   *        A setting object that should have properties for
   *        prefNames, getCallback and setCallback.
   */
  addSetting(name, setting) {
    settingsMap.set(name, setting);
  },

  /**
   * Gets the default value for a preference.
   *
   * @param {string} prefName The name of the preference.
   *
   * @returns {string|number|boolean} The default value of the preference.
   */
  getDefaultValue(prefName) {
    return lazy.defaultPreferences.get(prefName);
  },

  /**
   * Returns a map of prefName to setting Name for use in about:config, about:preferences or
   * other areas of Firefox that need to know whether a specific pref is controlled by an
   * extension.
   *
   * Given a prefName, you can get the settingName.  Call EPM.getSetting(settingName) to
   * get the details of the setting, including which id if any is in control of the
   * setting.
   *
   * @returns {Promise}
   *          Resolves to a Map of prefName->settingName
   */
  async getManagedPrefDetails() {
    await Management.asyncLoadSettingsModules();
    let prefs = new Map();
    settingsMap.forEach((setting, name) => {
      for (let prefName of setting.prefNames) {
        prefs.set(prefName, name);
      }
    });
    return prefs;
  },

  /**
   * Indicates that an extension would like to change the value of a previously
   * defined setting.
   *
   * @param {string} id
   *        The id of the extension for which a setting is being set.
   * @param {string} name
   *        The unique id of the setting.
   * @param {any} value
   *        The value to be stored in the settings store for this
   *        group of preferences.
   *
   * @returns {Promise}
   *          Resolves to true if the preferences were changed and to false if
   *          the preferences were not changed.
   */
  async setSetting(id, name, value) {
    let setting = settingsMap.get(name);
    await lazy.ExtensionSettingsStore.initialize();
    let item = await lazy.ExtensionSettingsStore.addSetting(
      id,
      STORE_TYPE,
      name,
      value,
      initialValueCallback.bind(setting),
      name,
      settingsUpdate.bind(setting)
    );
    if (item) {
      setPrefs(name, setting, item);
      return true;
    }
    return false;
  },

  /**
   * Indicates that this extension wants to temporarily cede control over the
   * given setting.
   *
   * @param {string} id
   *        The id of the extension for which a preference setting is being disabled.
   * @param {string} name
   *        The unique id of the setting.
   *
   * @returns {Promise}
   *          Resolves to true if the preferences were changed and to false if
   *          the preferences were not changed.
   */
  disableSetting(id, name) {
    return processSetting(id, name, "disable");
  },

  /**
   * Enable a setting that has been disabled.
   *
   * @param {string} id
   *        The id of the extension for which a setting is being enabled.
   * @param {string} name
   *        The unique id of the setting.
   *
   * @returns {Promise}
   *          Resolves to true if the preferences were changed and to false if
   *          the preferences were not changed.
   */
  enableSetting(id, name) {
    return processSetting(id, name, "enable");
  },

  /**
   * Specifically select an extension, the user, or the precedence order that will
   * be in control of this setting.
   *
   * @param {string | null} id
   *        The id of the extension for which a setting is being selected, or
   *        ExtensionSettingStore.SETTING_USER_SET (null).
   * @param {string} name
   *        The unique id of the setting.
   *
   * @returns {Promise}
   *          Resolves to true if the preferences were changed and to false if
   *          the preferences were not changed.
   */
  selectSetting(id, name) {
    return processSetting(id, name, "select");
  },

  /**
   * Indicates that this extension no longer wants to set the given setting.
   *
   * @param {string} id
   *        The id of the extension for which a preference setting is being removed.
   * @param {string} name
   *        The unique id of the setting.
   *
   * @returns {Promise}
   *          Resolves to true if the preferences were changed and to false if
   *          the preferences were not changed.
   */
  removeSetting(id, name) {
    return processSetting(id, name, "removeSetting");
  },

  /**
   * Disables all previously set settings for an extension. This can be called when
   * an extension is being disabled, for example.
   *
   * @param {string} id
   *        The id of the extension for which all settings are being unset.
   */
  async disableAll(id) {
    await lazy.ExtensionSettingsStore.initialize();
    let settings = lazy.ExtensionSettingsStore.getAllForExtension(
      id,
      STORE_TYPE
    );
    let disablePromises = [];
    for (let name of settings) {
      disablePromises.push(this.disableSetting(id, name));
    }
    await Promise.all(disablePromises);
  },

  /**
   * Enables all disabled settings for an extension. This can be called when
   * an extension has finished updating or is being re-enabled, for example.
   *
   * @param {string} id
   *        The id of the extension for which all settings are being enabled.
   */
  async enableAll(id) {
    await lazy.ExtensionSettingsStore.initialize();
    let settings = lazy.ExtensionSettingsStore.getAllForExtension(
      id,
      STORE_TYPE
    );
    let enablePromises = [];
    for (let name of settings) {
      enablePromises.push(this.enableSetting(id, name));
    }
    await Promise.all(enablePromises);
  },

  /**
   * Removes all previously set settings for an extension. This can be called when
   * an extension is being uninstalled, for example.
   *
   * @param {string} id
   *        The id of the extension for which all settings are being unset.
   */
  async removeAll(id) {
    await lazy.ExtensionSettingsStore.initialize();
    let settings = lazy.ExtensionSettingsStore.getAllForExtension(
      id,
      STORE_TYPE
    );
    let removePromises = [];
    for (let name of settings) {
      removePromises.push(this.removeSetting(id, name));
    }
    await Promise.all(removePromises);
  },

  /**
   * Removes a set of settings that are available under certain addon permissions.
   *
   * @param {string} id
   *        The extension id.
   * @param {Array<string>} permissions
   *        The permission name from the extension manifest.
   * @returns {Promise}
   *        A promise that resolves when all related settings are removed.
   */
  async removeSettingsForPermissions(id, permissions) {
    if (!permissions || !permissions.length) {
      return;
    }
    await Management.asyncLoadSettingsModules();
    let removePromises = [];
    settingsMap.forEach((setting, name) => {
      if (permissions.includes(setting.permission)) {
        removePromises.push(this.removeSetting(id, name));
      }
    });
    return Promise.all(removePromises);
  },

  /**
   * Return the currently active value for a setting.
   *
   * @param {string} name
   *        The unique id of the setting.
   *
   * @returns {Promise<object>} The current setting object.
   */
  async getSetting(name) {
    await lazy.ExtensionSettingsStore.initialize();
    return lazy.ExtensionSettingsStore.getSetting(STORE_TYPE, name);
  },

  /**
   * Return the levelOfControl for a setting / extension combo.
   * This queries the levelOfControl from the ExtensionSettingsStore and also
   * takes into account whether any of the setting's preferences are locked.
   *
   * @param {string} id
   *        The id of the extension for which levelOfControl is being requested.
   * @param {string} name
   *        The unique id of the setting.
   * @param {string} storeType
   *        The name of the store in ExtensionSettingsStore.
   *        Defaults to STORE_TYPE.
   *
   * @returns {Promise}
   *          Resolves to the level of control of the extension over the setting.
   */
  async getLevelOfControl(id, name, storeType = STORE_TYPE) {
    // This could be called for a setting that isn't defined to the PreferencesManager,
    // in which case we simply defer to the SettingsStore.
    if (storeType === STORE_TYPE) {
      let setting = settingsMap.get(name);
      if (!setting) {
        return "not_controllable";
      }
      for (let prefName of setting.prefNames) {
        if (lazy.Preferences.locked(prefName)) {
          return "not_controllable";
        }
      }
    }
    await lazy.ExtensionSettingsStore.initialize();
    return lazy.ExtensionSettingsStore.getLevelOfControl(id, storeType, name);
  },

  /**
   * Returns an API object with get/set/clear used for a setting.
   *
   * @param {string|object} extensionId or params object
   * @param {string} name
   *          The unique id of the setting.
   * @param {Function} callback
   *          The function that retreives the current setting from prefs.
   * @param {string} storeType
   *          The name of the store in ExtensionSettingsStore.
   *          Defaults to STORE_TYPE.
   * @param {boolean} readOnly
   * @param {Function} validate
   *          Utility function for any specific validation, such as checking
   *          for supported platform.  Function should throw an error if necessary.
   *
   * @returns {object} API object with get/set/clear methods
   */
  getSettingsAPI(
    extensionId,
    name,
    callback,
    storeType,
    readOnly = false,
    validate
  ) {
    if (arguments.length > 1) {
      Services.console.logStringMessage(
        `ExtensionPreferencesManager.getSettingsAPI for ${name} should be updated to use a single paramater object.`
      );
    }
    return ExtensionPreferencesManager._getInternalSettingsAPI(
      arguments.length === 1
        ? extensionId
        : {
            extensionId,
            name,
            callback,
            storeType,
            readOnly,
            validate,
          }
    ).api;
  },

  /**
   * getPrimedSettingsListener returns a function used to create
   * a primed event listener.
   *
   * If a module overrides onChange then it must provide it's own
   * persistent listener logic.  See homepage_override in browserSettings
   * for an example.
   *
   * addSetting must be called prior to priming listeners.
   *
   * @param {object} config see getSettingsAPI
   *        {Extension} extension, passed through to validate and used for extensionId
   *        {string} name
   *          The unique id of the settings api in the module, e.g. "settings"
   * @returns {object} prime listener object
   */
  getPrimedSettingsListener(config) {
    let { name, extension } = config;
    if (!name || !extension) {
      throw new Error(
        `name and extension are required for getPrimedSettingListener`
      );
    }
    if (!settingsMap.get(name)) {
      throw new Error(
        `addSetting must be called prior to getPrimedSettingListener`
      );
    }
    return ExtensionPreferencesManager._getInternalSettingsAPI({
      name,
      extension,
    }).registerEvent;
  },

  /**
   * Returns an object with a public API containing get/set/clear used for a setting,
   * and a registerEvent function used for registering the event listener.
   *
   * @param {object} params The params object contains the following:
   *        {BaseContext} context
   *        {Extension} extension, optional, passed through to validate and used for extensionId
   *        {string} extensionId, optional to support old API
   *        {string} module
   *          The name of the api module, e.g. "proxy"
   *        {string} name
   *          The unique id of the settings api in the module, e.g. "settings"
   *          "name" should match the name given in the addSetting call.
   *        {Function} callback
   *          The function that retreives the current setting from prefs.
   *        {string} storeType
   *          The name of the store in ExtensionSettingsStore.
   *          Defaults to STORE_TYPE.
   *        {boolean} readOnly
   *        {Function} validate
   *          Utility function for any specific validation, such as checking
   *          for supported platform.  Function should throw an error if necessary.
   *
   * @returns {object} internal API object with
   *          {object} api
   *            the public api available to extensions
   *          {Function} registerEvent
   *            the registration function used for priming events
   */
  _getInternalSettingsAPI(params) {
    let {
      extensionId,
      context,
      extension,
      module,
      name,
      callback,
      storeType,
      readOnly = false,
      onChange,
      validate,
    } = params;
    if (context) {
      extension = context.extension;
    }
    if (!extensionId && extension) {
      extensionId = extension.id;
    }

    const checkScope = details => {
      let { scope } = details;
      if (scope && scope !== "regular") {
        throw new ExtensionError(
          `Firefox does not support the ${scope} settings scope.`
        );
      }
    };

    // Check the setting for anything we may need.
    let setting = settingsMap.get(name);
    readOnly = readOnly || !!setting?.readOnly;
    validate = validate || setting?.validate || (() => {});
    let getValue = callback || setting?.getCallback;
    if (!getValue || typeof getValue !== "function") {
      throw new Error(`Invalid get callback for setting ${name} in ${module}`);
    }

    let settingsAPI = {
      async get(details) {
        validate(extension);
        let levelOfControl = details.incognito
          ? "not_controllable"
          : await ExtensionPreferencesManager.getLevelOfControl(
              extensionId,
              name,
              storeType
            );
        levelOfControl =
          readOnly && levelOfControl === "controllable_by_this_extension"
            ? "not_controllable"
            : levelOfControl;
        return {
          levelOfControl,
          value: await getValue(),
        };
      },
      set(details) {
        validate(extension);
        checkScope(details);
        if (!readOnly) {
          return ExtensionPreferencesManager.setSetting(
            extensionId,
            name,
            details.value
          );
        }
        return false;
      },
      clear(details) {
        validate(extension);
        checkScope(details);
        if (!readOnly) {
          return ExtensionPreferencesManager.removeSetting(extensionId, name);
        }
        return false;
      },
      onChange,
    };
    let registerEvent = fire => {
      let listener = async () => {
        fire.async(await settingsAPI.get({}));
      };
      Management.on(`extension-setting-changed:${name}`, listener);
      return {
        unregister: () => {
          Management.off(`extension-setting-changed:${name}`, listener);
        },
        convert(_fire) {
          fire = _fire;
        },
      };
    };

    // Any caller using the old call signature will not have passed
    // context to us.  This should only be experimental addons in the
    // wild.
    if (onChange === undefined && context) {
      // Some settings that are read-only may not have called addSetting, in
      // which case we have no way to listen on the pref changes.
      if (setting) {
        settingsAPI.onChange = new lazy.ExtensionCommon.EventManager({
          context,
          module,
          event: name,
          name: `${name}.onChange`,
          register: fire => {
            return registerEvent(fire).unregister;
          },
        }).api();
      } else {
        Services.console.logStringMessage(
          `ExtensionPreferencesManager API ${name} created but addSetting was not called.`
        );
      }
    }
    return { api: settingsAPI, registerEvent };
  },
};
