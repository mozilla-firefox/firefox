/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  WindowsRegistry: "resource://gre/modules/WindowsRegistry.sys.mjs",
  ctypes: "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs",
});

const PER_INSTALLATION_PREFS_PLATFORMS = ["win"];

// The file that stores Application Update configuration settings. The file is
// located in the update directory which makes it a common setting across all
// application profiles and allows the Background Update Agent to read it.
const FILE_UPDATE_CONFIG_JSON = "update-config.json";
const FILE_DEFAULT_LOCALE = "default.locale";
const PREF_APP_DISTRIBUTION = "distribution.id";
const PREF_APP_DISTRIBUTION_VERSION = "distribution.version";

export var UpdateUtils = {
  _locale: undefined,
  _configFilePath: undefined,

  /**
   * Read the update channel from defaults only.  We do this to ensure that
   * the channel is tightly coupled with the application and does not apply
   * to other instances of the application that may use the same profile.
   *
   * @param [optional] aIncludePartners
   *        Whether or not to include the partner bits. Default: true.
   */
  getUpdateChannel(aIncludePartners = true) {
    let defaults = Services.prefs.getDefaultBranch(null);
    let channel = defaults.getCharPref(
      "app.update.channel",
      AppConstants.MOZ_UPDATE_CHANNEL
    );

    if (aIncludePartners) {
      try {
        let partners = Services.prefs.getChildList("app.partner.").sort();
        if (partners.length) {
          channel += "-cck";
          partners.forEach(function (prefName) {
            channel += "-" + Services.prefs.getCharPref(prefName);
          });
        }
      } catch (e) {
        console.error(e);
      }
    }

    return channel;
  },

  get UpdateChannel() {
    return this.getUpdateChannel();
  },

  /**
   * Formats a URL by replacing %...% values with OS, build and locale specific
   * values.
   *
   * @param  url
   *         The URL to format.
   * @return The formatted URL.
   */
  async formatUpdateURL(url) {
    const locale = await this.getLocale();

    return url.replace(/%(\w+)%/g, (match, name) => {
      let replacement = match;
      switch (name) {
        case "PRODUCT":
          replacement = Services.appinfo.name;
          break;
        case "VERSION":
          replacement = Services.appinfo.version;
          break;
        case "BUILD_ID":
          replacement = Services.appinfo.appBuildID;
          break;
        case "BUILD_TARGET":
          replacement = Services.appinfo.OS + "_" + this.ABI;
          break;
        case "OS_VERSION":
          replacement = this.OSVersion;
          break;
        case "LOCALE":
          replacement = locale;
          break;
        case "CHANNEL":
          replacement = this.UpdateChannel;
          break;
        case "PLATFORM_VERSION":
          replacement = Services.appinfo.platformVersion;
          break;
        case "SYSTEM_CAPABILITIES":
          replacement = getSystemCapabilities();
          break;
        case "DISTRIBUTION":
          replacement = getDistributionPrefValue(PREF_APP_DISTRIBUTION);
          break;
        case "DISTRIBUTION_VERSION":
          replacement = getDistributionPrefValue(PREF_APP_DISTRIBUTION_VERSION);
          break;
      }
      return encodeURIComponent(replacement);
    });
  },

  /**
   * Gets the locale from the default.locale file for replacing %LOCALE% in the
   * update url. The default.locale file can be located in the application
   * directory or the GRE directory with preference given to it being located in
   * the application directory.
   */
  async getLocale() {
    if (this._locale !== undefined) {
      return this._locale;
    }

    for (let res of ["app", "gre"]) {
      const url = `resource://${res}/${FILE_DEFAULT_LOCALE}`;
      let data;
      try {
        data = await fetch(url);
      } catch (e) {
        continue;
      }
      const locale = await data.text();
      if (locale) {
        return (this._locale = locale.trim());
      }
    }
    console.error(
      FILE_DEFAULT_LOCALE,
      " file doesn't exist in either the application or GRE directories"
    );

    return (this._locale = null);
  },

  /* Get the path to the config file. */
  getConfigFilePath() {
    let path = PathUtils.join(
      Services.dirsvc.get("UpdRootD", Ci.nsIFile).path,
      FILE_UPDATE_CONFIG_JSON
    );
    return (this._configFilePath = path);
  },

  get configFilePath() {
    if (this._configFilePath !== undefined) {
      return this._configFilePath;
    }
    return this.getConfigFilePath();
  },

  /**
   * Determines whether or not the Application Update Service automatically
   * downloads and installs updates. This corresponds to whether or not the user
   * has selected "Automatically install updates" in about:preferences.
   *
   * On Windows, this setting is shared across all profiles for the installation
   * and is read asynchronously from the file. On other operating systems, this
   * setting is stored in a pref and is thus a per-profile setting.
   *
   * @return A Promise that resolves with a boolean.
   */
  async getAppUpdateAutoEnabled() {
    return this.readUpdateConfigSetting("app.update.auto");
  },

  /**
   * Toggles whether the Update Service automatically downloads and installs
   * updates. This effectively selects between the "Automatically install
   * updates" and "Check for updates but let you choose to install them" options
   * in about:preferences.
   *
   * On Windows, this setting is shared across all profiles for the installation
   * and is written asynchronously to the file. On other operating systems, this
   * setting is stored in a pref and is thus a per-profile setting.
   *
   * If this method is called when the setting is locked, the returned promise
   * will reject. The lock status can be determined with
   * UpdateUtils.appUpdateAutoSettingIsLocked()
   *
   * @param  enabled If set to true, automatic download and installation of
   *                 updates will be enabled. If set to false, this will be
   *                 disabled.
   * @return A Promise that, once the setting has been saved, resolves with the
   *         boolean value that was saved. If the setting could not be
   *         successfully saved, the Promise will reject.
   *         On Windows, where this setting is stored in a file, this Promise
   *         may reject with an I/O error.
   *         On other operating systems, this promise should not reject as
   *         this operation simply sets a pref.
   */
  async setAppUpdateAutoEnabled(enabledValue) {
    return this.writeUpdateConfigSetting("app.update.auto", !!enabledValue);
  },

  /**
   * This function should be used to determine if the automatic application
   * update setting is locked by an enterprise policy
   *
   * @return true if the automatic update setting is currently locked.
   *         Otherwise, false.
   */
  appUpdateAutoSettingIsLocked() {
    return this.appUpdateSettingIsLocked("app.update.auto");
  },

  /**
   * Indicates whether or not per-installation prefs are supported on this
   * platform.
   */
  PER_INSTALLATION_PREFS_SUPPORTED: PER_INSTALLATION_PREFS_PLATFORMS.includes(
    AppConstants.platform
  ),

  /**
   * Possible per-installation pref types.
   */
  PER_INSTALLATION_PREF_TYPE_BOOL: "boolean",
  PER_INSTALLATION_PREF_TYPE_ASCII_STRING: "ascii",
  PER_INSTALLATION_PREF_TYPE_INT: "integer",

  /**
   * We want the preference definitions to be part of UpdateUtils for a couple
   * of reasons. It's a clean way for consumers to look up things like observer
   * topic names. It also allows us to manipulate the supported prefs during
   * testing. However, we want to use values out of UpdateUtils (like pref
   * types) to construct this object. Therefore, this will initially be a
   * placeholder, which we will properly define after the UpdateUtils object
   * definition.
   */
  PER_INSTALLATION_PREFS: null,

  /**
   * This function initializes per-installation prefs. Note that it does not
   * need to be called manually; it is already called within the file.
   *
   * This function is called on startup, so it does not read or write to disk.
   */
  initPerInstallPrefs() {
    // If we don't have per-installation prefs, we store the update config in
    // preferences. In that case, the best way to notify observers of this
    // setting is just to propagate it from a pref observer. This ensures that
    // the expected observers still get notified, even if a user manually
    // changes the pref value.
    if (!UpdateUtils.PER_INSTALLATION_PREFS_SUPPORTED) {
      let initialConfig = {};
      for (const [prefName, pref] of Object.entries(
        UpdateUtils.PER_INSTALLATION_PREFS
      )) {
        const prefTypeFns = TYPE_SPECIFIC_PREF_FNS[pref.type];

        try {
          let initialValue = prefTypeFns.getProfilePref(prefName);
          initialConfig[prefName] = initialValue;
        } catch (e) {}

        Services.prefs.addObserver(prefName, async () => {
          let config = { ...gUpdateConfigCache };
          config[prefName] =
            await UpdateUtils.readUpdateConfigSetting(prefName);
          maybeUpdateConfigChanged(config);
        });
      }

      // On the first call to maybeUpdateConfigChanged, it has nothing to
      // compare its input to, so it just populates the cache and doesn't notify
      // any observers. This makes sense during normal usage, because the first
      // call will be on the first config file read, and we don't want to notify
      // observers of changes on the first read. But that means that when
      // propagating pref observers, we need to make one initial call to
      // simulate that initial read so that the cache will be populated when the
      // first pref observer fires.
      maybeUpdateConfigChanged(initialConfig);
    }
  },

  /**
   * Reads an installation-specific configuration setting from the update config
   * JSON file. This function is guaranteed not to throw. If there are problems
   * reading the file, the default value will be returned so that update can
   * proceed. This is particularly important since the configuration file is
   * writable by anyone and we don't want an unprivileged user to be able to
   * break update for other users.
   *
   * If relevant policies are active, this function will read the policy value
   * rather than the stored value.
   *
   * @param  prefName
   *           The preference to read. Must be a key of the
   *           PER_INSTALLATION_PREFS object.
   * @return A Promise that resolves with the pref's value.
   */
  readUpdateConfigSetting(prefName) {
    if (!(prefName in this.PER_INSTALLATION_PREFS)) {
      return Promise.reject(
        new Error(
          `UpdateUtils.readUpdateConfigSetting: Unknown per-installation ` +
            `pref '${prefName}'`
        )
      );
    }

    const pref = this.PER_INSTALLATION_PREFS[prefName];
    const prefTypeFns = TYPE_SPECIFIC_PREF_FNS[pref.type];

    if (Services.policies && "policyFn" in pref) {
      let policyValue = pref.policyFn();
      if (policyValue !== null) {
        return Promise.resolve(policyValue);
      }
    }

    if (!this.PER_INSTALLATION_PREFS_SUPPORTED) {
      // If we don't have per-installation prefs, we use regular preferences.
      let prefValue = prefTypeFns.getProfilePref(prefName, pref.defaultValue);
      return Promise.resolve(prefValue);
    }

    let readPromise = updateConfigIOPromise
      // All promises returned by (read|write)UpdateConfigSetting are part of a
      // single promise chain in order to serialize disk operations. But we
      // don't want the entire promise chain to reject when one operation fails.
      // So we are going to silently clear any rejections the promise chain
      // might contain.
      //
      // We will also pass an empty function for the first then() argument as
      // well, just to make sure we are starting fresh rather than potentially
      // propagating some stale value.
      .then(
        () => {},
        () => {}
      )
      .then(readUpdateConfig)
      .then(maybeUpdateConfigChanged)
      .then(config => {
        return readEffectiveValue(config, prefName);
      });
    updateConfigIOPromise = readPromise;
    return readPromise;
  },

  /**
   * Changes an installation-specific configuration setting by writing it to
   * the update config JSON file.
   *
   * If this method is called on a prefName that is locked, the returned promise
   * will reject. The lock status can be determined with
   * appUpdateSettingIsLocked().
   *
   * @param  prefName
   *           The preference to change. This must be a key of the
   *           PER_INSTALLATION_PREFS object.
   * @param  value
   *           The value to be written. Its type must match
   *           PER_INSTALLATION_PREFS[prefName].type
   * @param  options
   *           Optional. An object containing any of the following keys:
   *             setDefaultOnly
   *               If set to true, the default branch value will be set rather
   *               than user value. If a user value is set for this pref, this
   *               will have no effect on the pref's effective value.
   *               NOTE - The behavior of the default pref branch currently
   *                      differs depending on whether the current platform
   *                      supports per-installation prefs. If they are
   *                      supported, default branch values persist across
   *                      Firefox sessions. If they aren't supported, default
   *                      branch values reset when Firefox shuts down.
   * @return A Promise that, once the setting has been saved, resolves with the
   *         value that was saved.
   * @throws If there is an I/O error when attempting to write to the config
   *         file, the returned Promise will reject with a DOMException.
   */
  writeUpdateConfigSetting(prefName, value, options) {
    if (!(prefName in this.PER_INSTALLATION_PREFS)) {
      return Promise.reject(
        new Error(
          `UpdateUtils.writeUpdateConfigSetting: Unknown per-installation ` +
            `pref '${prefName}'`
        )
      );
    }

    if (this.appUpdateSettingIsLocked(prefName)) {
      return Promise.reject(
        new Error(
          `UpdateUtils.writeUpdateConfigSetting: Unable to change value of ` +
            `setting '${prefName}' because it is locked by policy`
        )
      );
    }

    if (!options) {
      options = {};
    }

    const pref = this.PER_INSTALLATION_PREFS[prefName];
    const prefTypeFns = TYPE_SPECIFIC_PREF_FNS[pref.type];

    if (!prefTypeFns.isValid(value)) {
      return Promise.reject(
        new Error(
          `UpdateUtils.writeUpdateConfigSetting: Attempted to change pref ` +
            `'${prefName} to invalid value: ${JSON.stringify(value)}`
        )
      );
    }

    if (!this.PER_INSTALLATION_PREFS_SUPPORTED) {
      // If we don't have per-installation prefs, we use regular preferences.
      if (options.setDefaultOnly) {
        prefTypeFns.setProfileDefaultPref(prefName, value);
      } else {
        prefTypeFns.setProfilePref(prefName, value);
      }
      // Rather than call maybeUpdateConfigChanged, a pref observer has
      // been connected to the relevant pref. This allows us to catch direct
      // changes to prefs (which Firefox shouldn't be doing, but the user
      // might do in about:config).
      return Promise.resolve(value);
    }

    let writePromise = updateConfigIOPromise
      // All promises returned by (read|write)UpdateConfigSetting are part of a
      // single promise chain in order to serialize disk operations. But we
      // don't want the entire promise chain to reject when one operation fails.
      // So we are going to silently clear any rejections the promise chain
      // might contain.
      //
      // We will also pass an empty function for the first then() argument as
      // well, just to make sure we are starting fresh rather than potentially
      // propagating some stale value.
      .then(
        () => {},
        () => {}
      )
      // We always re-read the update config before writing, rather than using a
      // cached version. Otherwise, two simultaneous instances may overwrite
      // each other's changes.
      .then(readUpdateConfig)
      .then(async config => {
        setConfigValue(config, prefName, value, {
          setDefaultOnly: !!options.setDefaultOnly,
        });

        try {
          await writeUpdateConfig(config);
          return config;
        } catch (e) {
          console.error(
            "UpdateUtils.writeUpdateConfigSetting: App update configuration " +
              "file write failed. Exception: ",
            e
          );
          // Re-throw the error so the caller knows that writing the value in
          // the app update config file failed.
          throw e;
        }
      })
      .then(maybeUpdateConfigChanged)
      .then(() => {
        // If this value wasn't written, a previous promise in the chain will
        // have thrown, so we can unconditionally return the expected written
        // value as the value that was written.
        return value;
      });
    updateConfigIOPromise = writePromise;
    return writePromise;
  },

  /**
   * Returns true if the specified pref is controlled by policy and thus should
   * not be changeable by the user.
   */
  appUpdateSettingIsLocked(prefName) {
    if (!(prefName in UpdateUtils.PER_INSTALLATION_PREFS)) {
      return Promise.reject(
        new Error(
          `UpdateUtils.appUpdateSettingIsLocked: Unknown per-installation pref '${prefName}'`
        )
      );
    }

    // If we don't have policy support, nothing can be locked.
    if (!Services.policies) {
      return false;
    }

    const pref = UpdateUtils.PER_INSTALLATION_PREFS[prefName];
    if (!pref.policyFn) {
      return false;
    }
    const policyValue = pref.policyFn();
    return policyValue !== null;
  },

  summarizeLatestUpdate(history) {
    let is_patch = null;
    let target_version = null;
    let previous_version = null;
    let total_retries = 0;
    if (history && history.length) {
      is_patch = history[0]?.isCompleteUpdate != "true";
      // In updates.xml, 'appVersion' is the version we are updating to.
      target_version = history[0]?.appVersion;
      previous_version = history[0]?.previousAppVersion;
      if (history[0]?.childNodes && history[0]?.childNodes.length) {
        total_retries = history[0].childNodes.reduce(
          (accum, item) =>
            item?.numTotalInstallAttempts
              ? accum + item.numTotalInstallAttempts
              : accum,
          0
        );
      }
    }
    return { is_patch, target_version, previous_version, total_retries };
  },
};

const PER_INSTALLATION_DEFAULTS_BRANCH = "__DEFAULTS__";

/**
 * Some prefs are specific to the installation, not the profile. They are
 * stored in JSON format in FILE_UPDATE_CONFIG_JSON.
 * Not all platforms currently support per-installation prefs, in which case
 * we fall back to using profile-specific prefs.
 *
 * Note: These prefs should always be accessed through UpdateUtils. Do NOT
 *       attempt to read or write their prefs directly.
 *
 * Keys in this object should be the name of the pref. The same name will be
 * used whether we are writing it to the per-installation or per-profile pref.
 * Values in this object should be objects with the following keys:
 *   type
 *     Must be one of the Update.PER_INSTALLATION_PREF_TYPE_* values, defined
 *     above.
 *   defaultValue
 *     The default value to use for this pref if no value is set. This must be
 *     of a type that is compatible with the type value specified.
 *   migrate
 *     Optional - defaults to false. A boolean indicating whether an existing
 *     value in the profile-specific prefs ought to be migrated to an
 *     installation specific pref. This is useful for prefs like
 *     app.update.auto that used to be profile-specific prefs.
 *     Note - Migration currently happens only on the creation of the JSON
 *            file. If we want to add more prefs that require migration, we
 *            will probably need to change this.
 *   observerTopic
 *     When a config value is changed, an observer will be fired, much like
 *     the existing preference observers. This specifies the topic of the
 *     observer that will be fired.
 *   policyFn
 *     Optional. If defined, should be a function that returns null or a value
 *     of the specified type of this pref. If null is returned, this has no
 *     effect. If another value is returned, it will be used rather than
 *     reading the pref. This function will only be called if
 *     Services.policies is defined. Asynchronous functions are not currently
 *     supported.
 */
UpdateUtils.PER_INSTALLATION_PREFS = {
  "app.update.auto": {
    type: UpdateUtils.PER_INSTALLATION_PREF_TYPE_BOOL,
    defaultValue: true,
    migrate: true,
    observerTopic: "auto-update-config-change",
    policyFn: () => {
      if (!Services.policies.isAllowed("app-auto-updates-off")) {
        // We aren't allowed to turn off auto-update - it is forced on.
        return true;
      }
      if (!Services.policies.isAllowed("app-auto-updates-on")) {
        // We aren't allowed to turn on auto-update - it is forced off.
        return false;
      }
      return null;
    },
  },
  "app.update.background.enabled": {
    type: UpdateUtils.PER_INSTALLATION_PREF_TYPE_BOOL,
    defaultValue: true,
    observerTopic: "background-update-config-change",
    policyFn: () => {
      if (!Services.policies.isAllowed("app-background-update-off")) {
        // We aren't allowed to turn off background update - it is forced on.
        return true;
      }
      if (!Services.policies.isAllowed("app-background-update-on")) {
        // We aren't allowed to turn on background update - it is forced off.
        return false;
      }
      return null;
    },
  },
};

const TYPE_SPECIFIC_PREF_FNS = {
  [UpdateUtils.PER_INSTALLATION_PREF_TYPE_BOOL]: {
    getProfilePref: Services.prefs.getBoolPref,
    setProfilePref: Services.prefs.setBoolPref,
    setProfileDefaultPref: (pref, value) => {
      let defaults = Services.prefs.getDefaultBranch("");
      defaults.setBoolPref(pref, value);
    },
    isValid: value => typeof value == "boolean",
  },
  [UpdateUtils.PER_INSTALLATION_PREF_TYPE_ASCII_STRING]: {
    getProfilePref: Services.prefs.getCharPref,
    setProfilePref: Services.prefs.setCharPref,
    setProfileDefaultPref: (pref, value) => {
      let defaults = Services.prefs.getDefaultBranch("");
      defaults.setCharPref(pref, value);
    },
    isValid: value => typeof value == "string",
  },
  [UpdateUtils.PER_INSTALLATION_PREF_TYPE_INT]: {
    getProfilePref: Services.prefs.getIntPref,
    setProfilePref: Services.prefs.setIntPref,
    setProfileDefaultPref: (pref, value) => {
      let defaults = Services.prefs.getDefaultBranch("");
      defaults.setIntPref(pref, value);
    },
    isValid: value => Number.isInteger(value),
  },
};

/**
 * Used for serializing reads and writes of the app update json config file so
 * the writes don't happen out of order and the last write is the one that
 * the sets the value.
 */
var updateConfigIOPromise = Promise.resolve();

/**
 * Returns a pref name that we will use to keep track of if the passed pref has
 * been migrated already, so we don't end up migrating it twice.
 */
function getPrefMigratedPref(prefName) {
  return prefName + ".migrated";
}

/**
 * @return true if prefs need to be migrated from profile-specific prefs to
 *         installation-specific prefs.
 */
function updateConfigNeedsMigration() {
  for (const [prefName, pref] of Object.entries(
    UpdateUtils.PER_INSTALLATION_PREFS
  )) {
    if (pref.migrate) {
      let migratedPrefName = getPrefMigratedPref(prefName);
      let migrated = Services.prefs.getBoolPref(migratedPrefName, false);
      if (!migrated) {
        return true;
      }
    }
  }
  return false;
}

function setUpdateConfigMigrationDone() {
  for (const [prefName, pref] of Object.entries(
    UpdateUtils.PER_INSTALLATION_PREFS
  )) {
    if (pref.migrate) {
      let migratedPrefName = getPrefMigratedPref(prefName);
      Services.prefs.setBoolPref(migratedPrefName, true);
    }
  }
}

/**
 * Deletes the migrated data.
 */
function onMigrationSuccessful() {
  for (const [prefName, pref] of Object.entries(
    UpdateUtils.PER_INSTALLATION_PREFS
  )) {
    if (pref.migrate) {
      Services.prefs.clearUserPref(prefName);
    }
  }
}

function makeMigrationUpdateConfig() {
  let config = makeDefaultUpdateConfig();

  for (const [prefName, pref] of Object.entries(
    UpdateUtils.PER_INSTALLATION_PREFS
  )) {
    if (!pref.migrate) {
      continue;
    }
    let migratedPrefName = getPrefMigratedPref(prefName);
    let alreadyMigrated = Services.prefs.getBoolPref(migratedPrefName, false);
    if (alreadyMigrated) {
      continue;
    }

    const prefTypeFns = TYPE_SPECIFIC_PREF_FNS[pref.type];

    let prefHasValue = true;
    let prefValue;
    try {
      // Without a second argument, this will throw if the pref has no user
      // value or default value.
      prefValue = prefTypeFns.getProfilePref(prefName);
    } catch (e) {
      prefHasValue = false;
    }
    if (prefHasValue) {
      setConfigValue(config, prefName, prefValue);
    }
  }

  return config;
}

function makeDefaultUpdateConfig() {
  let config = {};

  for (const [prefName, pref] of Object.entries(
    UpdateUtils.PER_INSTALLATION_PREFS
  )) {
    setConfigValue(config, prefName, pref.defaultValue, {
      setDefaultOnly: true,
    });
  }

  return config;
}

/**
 * Sets the specified value in the config object.
 *
 * @param  config
 *           The config object for which to set the value
 * @param  prefName
 *           The name of the preference to set.
 * @param  prefValue
 *           The value to set the preference to.
 * @param  options
 *           Optional. An object containing any of the following keys:
 *             setDefaultOnly
 *               If set to true, the default value will be set rather than
 *               user value. If a user value is set for this pref, this will
 *               have no effect on the pref's effective value.
 */
function setConfigValue(config, prefName, prefValue, options) {
  if (!options) {
    options = {};
  }

  if (options.setDefaultOnly) {
    if (!(PER_INSTALLATION_DEFAULTS_BRANCH in config)) {
      config[PER_INSTALLATION_DEFAULTS_BRANCH] = {};
    }
    config[PER_INSTALLATION_DEFAULTS_BRANCH][prefName] = prefValue;
  } else if (prefValue != readDefaultValue(config, prefName)) {
    config[prefName] = prefValue;
  } else {
    delete config[prefName];
  }
}

/**
 * Reads the specified pref out of the given configuration object.
 * If a user value of the pref is set, that will be returned. If only a default
 * branch value is set, that will be returned. Otherwise, the default value from
 * PER_INSTALLATION_PREFS will be returned.
 *
 * Values will be validated before being returned. Invalid values are ignored.
 *
 * @param  config
 *           The configuration object to read.
 * @param  prefName
 *           The name of the preference to read.
 * @return The value of the preference.
 */
function readEffectiveValue(config, prefName) {
  if (!(prefName in UpdateUtils.PER_INSTALLATION_PREFS)) {
    throw new Error(
      `readEffectiveValue: Unknown per-installation pref '${prefName}'`
    );
  }
  const pref = UpdateUtils.PER_INSTALLATION_PREFS[prefName];
  const prefTypeFns = TYPE_SPECIFIC_PREF_FNS[pref.type];

  if (prefName in config) {
    if (prefTypeFns.isValid(config[prefName])) {
      return config[prefName];
    }
    console.error(
      `readEffectiveValue: Got invalid value for update config's` +
        ` '${prefName}' value: "${config[prefName]}"`
    );
  }
  return readDefaultValue(config, prefName);
}

/**
 * Reads the default branch pref out of the given configuration object. If one
 * is not set, the default value from PER_INSTALLATION_PREFS will be returned.
 *
 * Values will be validated before being returned. Invalid values are ignored.
 *
 * @param  config
 *           The configuration object to read.
 * @param  prefName
 *           The name of the preference to read.
 * @return The value of the preference.
 */
function readDefaultValue(config, prefName) {
  if (!(prefName in UpdateUtils.PER_INSTALLATION_PREFS)) {
    throw new Error(
      `readDefaultValue: Unknown per-installation pref '${prefName}'`
    );
  }
  const pref = UpdateUtils.PER_INSTALLATION_PREFS[prefName];
  const prefTypeFns = TYPE_SPECIFIC_PREF_FNS[pref.type];

  if (PER_INSTALLATION_DEFAULTS_BRANCH in config) {
    let defaults = config[PER_INSTALLATION_DEFAULTS_BRANCH];
    if (prefName in defaults) {
      if (prefTypeFns.isValid(defaults[prefName])) {
        return defaults[prefName];
      }
      console.error(
        `readEffectiveValue: Got invalid default value for update` +
          ` config's '${prefName}' value: "${defaults[prefName]}"`
      );
    }
  }
  return pref.defaultValue;
}

/**
 * Reads the update config and, if necessary, performs migration of un-migrated
 * values. We don't want to completely give up on update if this file is
 * unavailable, so default values will be returned on failure rather than
 * throwing an error.
 *
 * @return An Update Config object.
 */
async function readUpdateConfig() {
  try {
    let config = await IOUtils.readJSON(UpdateUtils.getConfigFilePath());

    // We only migrate once. If we read something, the migration has already
    // happened so we should make sure it doesn't happen again.
    setUpdateConfigMigrationDone();

    return config;
  } catch (e) {
    if (DOMException.isInstance(e) && e.name == "NotFoundError") {
      if (updateConfigNeedsMigration()) {
        const migrationConfig = makeMigrationUpdateConfig();
        setUpdateConfigMigrationDone();
        try {
          await writeUpdateConfig(migrationConfig);
          onMigrationSuccessful();
          return migrationConfig;
        } catch (e2) {
          console.error("readUpdateConfig: Migration failed: ", e2);
        }
      }
    } else {
      // We only migrate once. If we got an error other than the file not
      // existing, the migration has already happened so we should make sure
      // it doesn't happen again.
      setUpdateConfigMigrationDone();

      console.error(
        "readUpdateConfig: Unable to read app update configuration file. " +
          "Exception: ",
        e
      );
    }
    return makeDefaultUpdateConfig();
  }
}

/**
 * Writes the given configuration to the disk.
 *
 * @param  config
 *           The configuration object to write.
 * @return The configuration object written.
 * @throws A DOMException will be thrown on I/O error.
 */
async function writeUpdateConfig(config) {
  let path = UpdateUtils.getConfigFilePath();
  await IOUtils.writeJSON(path, config, { tmpPath: `${path}.tmp` });
  return config;
}

var gUpdateConfigCache;
/**
 * Notifies observers if any update config prefs have changed.
 *
 * @param  config
 *           The most up-to-date config object.
 * @return The same config object that was passed in.
 */
function maybeUpdateConfigChanged(config) {
  if (!gUpdateConfigCache) {
    // We don't want to generate a change notification for every pref on the
    // first read of the session.
    gUpdateConfigCache = config;
    return config;
  }

  for (const [prefName, pref] of Object.entries(
    UpdateUtils.PER_INSTALLATION_PREFS
  )) {
    let newPrefValue = readEffectiveValue(config, prefName);
    let oldPrefValue = readEffectiveValue(gUpdateConfigCache, prefName);
    if (newPrefValue != oldPrefValue) {
      Services.obs.notifyObservers(
        null,
        pref.observerTopic,
        newPrefValue.toString()
      );
    }
  }

  gUpdateConfigCache = config;
  return config;
}

/**
 * Note that this function sets up observers only, it does not do any I/O.
 */
UpdateUtils.initPerInstallPrefs();

/* Get the distribution pref values, from defaults only */
function getDistributionPrefValue(aPrefName) {
  let value = Services.prefs
    .getDefaultBranch(null)
    .getCharPref(aPrefName, "default");
  if (!value) {
    value = "default";
  }
  return value;
}

function getSystemCapabilities() {
  return "ISET:" + lazy.gInstructionSet + ",MEM:" + getMemoryMB();
}

/**
 * Gets the RAM size in megabytes. This will round the value because sysinfo
 * doesn't always provide RAM in multiples of 1024.
 */
function getMemoryMB() {
  let memoryMB = "unknown";
  try {
    memoryMB = Services.sysinfo.getProperty("memsize");
    if (memoryMB) {
      memoryMB = Math.round(memoryMB / 1024 / 1024);
    }
  } catch (e) {
    console.error("Error getting system info memsize property. Exception: ", e);
  }
  return memoryMB;
}

/**
 * Gets the supported CPU instruction set.
 */
ChromeUtils.defineLazyGetter(lazy, "gInstructionSet", function aus_gIS() {
  const CPU_EXTENSIONS = [
    "hasSSE4_2",
    "hasSSE4_1",
    "hasSSE4A",
    "hasSSSE3",
    "hasSSE3",
    "hasSSE2",
    "hasSSE",
    "hasMMX",
    "hasNEON",
    "hasARMv7",
    "hasARMv6",
  ];
  for (let ext of CPU_EXTENSIONS) {
    if (Services.sysinfo.getProperty(ext)) {
      return ext.substring(3);
    }
  }

  return "unknown";
});

/* Windows only getter that returns the processor architecture. */
ChromeUtils.defineLazyGetter(lazy, "gWinCPUArch", function aus_gWinCPUArch() {
  // Get processor architecture
  let arch = "unknown";

  const WORD = lazy.ctypes.uint16_t;
  const DWORD = lazy.ctypes.uint32_t;

  // This structure is described at:
  // http://msdn.microsoft.com/en-us/library/ms724958%28v=vs.85%29.aspx
  const SYSTEM_INFO = new lazy.ctypes.StructType("SYSTEM_INFO", [
    { wProcessorArchitecture: WORD },
    { wReserved: WORD },
    { dwPageSize: DWORD },
    { lpMinimumApplicationAddress: lazy.ctypes.voidptr_t },
    { lpMaximumApplicationAddress: lazy.ctypes.voidptr_t },
    { dwActiveProcessorMask: DWORD.ptr },
    { dwNumberOfProcessors: DWORD },
    { dwProcessorType: DWORD },
    { dwAllocationGranularity: DWORD },
    { wProcessorLevel: WORD },
    { wProcessorRevision: WORD },
  ]);

  let kernel32 = false;
  try {
    kernel32 = lazy.ctypes.open("Kernel32");
  } catch (e) {
    console.error("Unable to open kernel32! Exception: ", e);
  }

  if (kernel32) {
    try {
      let GetNativeSystemInfo = kernel32.declare(
        "GetNativeSystemInfo",
        lazy.ctypes.winapi_abi,
        lazy.ctypes.void_t,
        SYSTEM_INFO.ptr
      );
      let winSystemInfo = SYSTEM_INFO();
      // Default to unknown
      winSystemInfo.wProcessorArchitecture = 0xffff;

      GetNativeSystemInfo(winSystemInfo.address());
      switch (winSystemInfo.wProcessorArchitecture) {
        case 12:
          arch = "aarch64";
          break;
        case 9:
          arch = "x64";
          break;
        case 6:
          arch = "IA64";
          break;
        case 0:
          arch = "x86";
          break;
      }
    } catch (e) {
      console.error("Error getting processor architecture. Exception: ", e);
    } finally {
      kernel32.close();
    }
  }

  return arch;
});

ChromeUtils.defineLazyGetter(UpdateUtils, "ABI", function () {
  let abi = null;
  try {
    abi = Services.appinfo.XPCOMABI;
  } catch (e) {
    console.error("XPCOM ABI unknown");
  }

  if (AppConstants.platform == "win") {
    // Windows build should report the CPU architecture that it's running on.
    abi += "-" + lazy.gWinCPUArch;
  }

  if (AppConstants.ASAN) {
    // Allow ASan builds to receive their own updates
    abi += "-asan";
  }

  return abi;
});

ChromeUtils.defineLazyGetter(UpdateUtils, "OSVersion", function () {
  let osVersion;
  try {
    osVersion =
      Services.sysinfo.getProperty("name") +
      " " +
      Services.sysinfo.getProperty("version");
  } catch (e) {
    console.error("OS Version unknown.");
  }

  if (osVersion) {
    if (AppConstants.platform == "win") {
      const CURRENT_VERSION_PATH =
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

      // 'CurrentBuild' is a REG_SZ for some reason, so coerce it to a number
      // first.
      let build = Number(
        lazy.WindowsRegistry.readRegKey(
          Ci.nsIWindowsRegKey.ROOT_KEY_LOCAL_MACHINE,
          CURRENT_VERSION_PATH,
          "CurrentBuild",
          Ci.nsIWindowsRegKey.WOW64_64
        )
      );
      if (Number.isInteger(build)) {
        osVersion += `.0.0.${build}`;
      } else {
        osVersion += ".unknown";
      }

      // The value of UBR is already a REG_DWORD, so don't coerce it.
      let ubr = lazy.WindowsRegistry.readRegKey(
        Ci.nsIWindowsRegKey.ROOT_KEY_LOCAL_MACHINE,
        CURRENT_VERSION_PATH,
        "UBR",
        Ci.nsIWindowsRegKey.WOW64_64
      );
      if (Number.isInteger(ubr)) {
        osVersion += `.${ubr}`;
      } else {
        osVersion += ".unknown";
      }

      // Add processor architecture
      osVersion += " (" + lazy.gWinCPUArch + ")";
    }

    try {
      osVersion +=
        " (" + Services.sysinfo.getProperty("secondaryLibrary") + ")";
    } catch (e) {
      // Not all platforms have a secondary widget library, so an error is nothing to worry about.
    }
    osVersion = encodeURIComponent(osVersion);
  }
  return osVersion;
});
