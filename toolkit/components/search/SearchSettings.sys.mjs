/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AppProvidedConfigEngine:
    "moz-src:///toolkit/components/search/ConfigSearchEngine.sys.mjs",
  ConfigSearchEngine:
    "moz-src:///toolkit/components/search/ConfigSearchEngine.sys.mjs",
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  ObjectUtils: "resource://gre/modules/ObjectUtils.sys.mjs",
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  logConsole: () =>
    console.createInstance({
      logModule: "SearchSettings",
      maxLogLevel: lazy.SearchUtils.loggingEnabled ? "Debug" : "Warn",
    }),
});

/**
 * @import {SearchEngine} from "./SearchEngine.sys.mjs"
 */

const SETTINGS_FILENAME = "search.json.mozlz4";

/**
 * A map of engine ids to their previous names. These are required for
 * ensuring that user's settings are correctly migrated for users upgrading
 * from a settings file prior to settings version 7 (Firefox 108).
 *
 * @type {Map<string, string>}
 */
const ENGINE_ID_TO_OLD_NAME_MAP = new Map([
  ["wikipedia-hy", "Wikipedia (hy)"],
  ["wikipedia-kn", "Wikipedia (kn)"],
  ["wikipedia-lv", "Vikipēdija"],
  ["wikipedia-NO", "Wikipedia (no)"],
  ["wikipedia-el", "Wikipedia (el)"],
  ["wikipedia-lt", "Wikipedia (lt)"],
  ["wikipedia-my", "Wikipedia (my)"],
  ["wikipedia-pa", "Wikipedia (pa)"],
  ["wikipedia-pt", "Wikipedia (pt)"],
  ["wikipedia-si", "Wikipedia (si)"],
  ["wikipedia-tr", "Wikipedia (tr)"],
]);

/**
 * This class manages the saves search settings.
 *
 * Global settings can be saved and obtained from this class via the
 * `*Attribute` methods.
 */
export class SearchSettings {
  constructor(searchService) {
    this.#searchService = searchService;

    // Once the search service has initialized, schedule a write to ensure
    // that any settings that may have changed or need updating are handled.
    searchService.promiseInitialized.then(() => {
      this._delayedWrite();
    });
  }

  QueryInterface = ChromeUtils.generateQI([Ci.nsIObserver]);

  // Delay for batching invalidation of the JSON settings (ms)
  static SETTINGS_INVALIDATION_DELAY = 1000;

  get #settingsFilePath() {
    return PathUtils.join(PathUtils.profileDir, SETTINGS_FILENAME);
  }

  /**
   * A reference to the pending DeferredTask, if there is one.
   */
  _batchTask = null;

  /**
   * A reference to the search service so that we can save the engines list.
   */
  #searchService = null;

  /**
   * The user's settings file read from disk so we can persist metadata for
   * engines that are default or hidden, the user's locale and region, hashes
   * for the loadPath, and hashes for default and private default engines.
   * This is the JSON we read from disk and save to disk when there's an update
   * to the settings.
   *
   * Structure of settings:
   *
   * ```
   * Object {
   *   version: <number>,
   *   engines: [...],
   *   metaData: {...},
   * }
   * ```
   *
   * Settings metaData is the active metadata for setting and getting attributes.
   * When a new metadata attribute is set, we save it to #settings.metaData and
   * write #settings to disk.
   *
   * #settings.metaData attributes:
   *
   * @property {string} current
   *   The current user-set default engine. The associated hash is called
   *   'hash'.
   * @property {string} private
   *   The current user-set private engine. The associated hash is called
   *   'privateHash'.
   *   The current and prviate objects have associated hash fields to validate
   *   the value is set by the application.
   * @property {string} appDefaultEngine
   *   The identifier of the current application default engine.
   * @property {string} channel
   *   Configuration is restricted to the specified channel. ESR is an example
   *   of a channel.
   * @property {string} distroID
   *   Specifies which distribution the default engine is included in.
   * @property {string} experiment
   *   Specifies if the application is running on an experiment.
   * @property {string} locale
   *   The current locale.
   * @property {string} region
   *   The current region.
   * @property {boolean} useSavedOrder
   *   True if the user's order information stored in settings is used.
   */
  #settings = null;

  /**
   * #cachedSettings is updated when we read the settings from disk and when
   * we write settings to disk. #cachedSettings is compared with #settings
   * before we do a write to disk. If there's no change to the settings
   * attributes, then we don't write the settings to disk.
   *
   * This is a deep copy of #settings.
   *
   * @type {object}
   */
  #cachedSettings = {};

  addObservers() {
    Services.obs.addObserver(this, lazy.SearchUtils.TOPIC_ENGINE_MODIFIED);
    Services.obs.addObserver(this, lazy.SearchUtils.TOPIC_SEARCH_SERVICE);
  }

  /**
   * Cleans up, removing observers.
   */
  removeObservers() {
    Services.obs.removeObserver(this, lazy.SearchUtils.TOPIC_ENGINE_MODIFIED);
    Services.obs.removeObserver(this, lazy.SearchUtils.TOPIC_SEARCH_SERVICE);
  }

  /**
   * Whether the last `get` reset the settings because they were corrupt.
   */
  lastGetCorrupt = false;

  /**
   * Reads the settings file.
   *
   * @param {string} origin
   *   If this parameter is "test", then the settings will not be written. As
   *   some tests manipulate the settings directly, we allow turning off writing to
   *   avoid writing stale settings data.
   * @returns {Promise<object>}
   *   Returns the settings file data.
   */
  async get(origin = "") {
    this.lastGetCorrupt = false;

    let json;
    await this._ensurePendingWritesCompleted(origin);
    try {
      json = await IOUtils.readJSON(this.#settingsFilePath, {
        decompress: true,
      });
      if (!json.engines || !json.engines.length) {
        throw new Error("no engine in the file");
      }
    } catch (ex) {
      if (DOMException.isInstance(ex) && ex.name === "NotFoundError") {
        lazy.logConsole.debug("get: No settings file exists, new profile?", ex);
        return this.#resetSettings(false);
      }
      lazy.logConsole.error("get: Settings file empty or corrupt.", ex);
      return this.#resetSettings(true);
    }

    this.#settings = json;
    this.#cachedSettings = structuredClone(json);

    if (!this.#settings.metaData) {
      this.#settings.metaData = {};
    }

    try {
      await this.#migrateSettings();
    } catch (ex) {
      lazy.logConsole.error("get: Migration failed.", ex);
      return this.#resetSettings(true);
    }

    return structuredClone(json);
  }

  /**
   * Resets the search settings without writing to disk yet.
   *
   * If the reset is due to a corrupt settings file, the corrupt file is
   * backed up, the lastSettingsCorruptTime pref is set to the current time,
   * and this.lastGetCorrupt is set to true.
   *
   * @param {boolean} corrupt
   *   Whether the reset is carried out because the settings are corrupt.
   * @returns {Promise<object>}
   *   New empty search settings.
   */
  async #resetSettings(corrupt) {
    this.#settings = { metaData: {} };
    this.#cachedSettings = {};

    if (corrupt) {
      this.lastGetCorrupt = true;
      Services.prefs.setIntPref(
        "browser.search.lastSettingsCorruptTime",
        Date.now() / 1000
      );
      try {
        await IOUtils.move(
          this.#settingsFilePath,
          this.#settingsFilePath + ".bak"
        );
      } catch (ex) {
        lazy.logConsole.warn(
          "#resetSettings: Unable to create backup of corrupt settings file.",
          ex
        );
      }
    }

    return structuredClone(this.#settings);
  }

  /**
   * Test-only function to reset the settings.
   */
  _testResetSettings() {
    this.#resetSettings(false);
  }

  /**
   * Queues writing the settings until after SETTINGS_INVALIDATION_DELAY. If there
   * is a currently queued task then it will be restarted.
   */
  _delayedWrite() {
    if (this._batchTask) {
      this._batchTask.disarm();
    } else {
      let task = async () => {
        if (
          !this.#searchService.isInitialized ||
          this.#searchService._reloadingEngines
        ) {
          // Re-arm the task as we don't want to save potentially incomplete
          // information during the middle of (re-)initializing.
          this._batchTask.arm();
          return;
        }
        lazy.logConsole.debug("batchTask: Invalidating engine settings");
        await this._write();
      };
      this._batchTask = new lazy.DeferredTask(
        task,
        SearchSettings.SETTINGS_INVALIDATION_DELAY
      );
    }
    this._batchTask.arm();
  }

  /**
   * Ensures any pending writes of the settings are completed.
   *
   * @param {string} origin
   *   If this parameter is "test", then the settings will not be written. As
   *   some tests manipulate the settings directly, we allow turning off writing to
   *   avoid writing stale settings data.
   */
  async _ensurePendingWritesCompleted(origin = "") {
    // Before we read the settings file, first make sure all pending tasks are clear.
    if (!this._batchTask) {
      return;
    }
    lazy.logConsole.debug("finalizing batch task");
    let task = this._batchTask;
    this._batchTask = null;
    // Tests manipulate the settings directly, so let's not double-write with
    // stale settings data here.
    if (origin == "test") {
      task.disarm();
    } else {
      await task.finalize();
    }
  }

  /**
   * Writes the settings to disk (no delay).
   */
  async _write() {
    if (this._batchTask) {
      this._batchTask.disarm();
    }

    let settings = {};

    // Allows us to force a settings refresh should the settings format change.
    settings.version = lazy.SearchUtils.SETTINGS_VERSION;
    settings.engines = [...this.#searchService._engines.values()].map(engine =>
      JSON.parse(JSON.stringify(engine))
    );
    settings.metaData = this.#settings.metaData;

    // Persist metadata for config engines even if they aren't currently
    // active, this means if they become active again their settings
    // will be restored. This can happen if a user switches between regions.
    if (this.#settings?.engines) {
      for (let engine of this.#settings.engines) {
        if (
          engine._isConfigEngine &&
          // If a config engine is user-installed and not included, it was
          // explicitly removed by the user and we should not persist its metadata.
          !engine._metaData["user-installed"]
        ) {
          // If the saved settings is already included in the settings, don't re-add it.
          // TODO (Bug 2046142): Refactor settings updates for overridden engines.
          // Checking the engine name as a fallback alongside the canonical ID is a temporary
          // workaround to handle third-party overrides. We should remove this name check to
          // rely strictly on IDs and prevent potential duplicates, which may require changing
          // how we handle search engines with duplicate names first.
          let included = settings.engines.some(
            e => e.id == engine.id || e._name == engine._name
          );
          if (!included) {
            settings.engines.push(engine);
          }
        }
      }
    }

    // Update the local copy.
    this.#settings = settings;

    try {
      if (!settings.engines.length) {
        throw new Error("cannot write without any engine.");
      }

      if (this.isCurrentAndCachedSettingsEqual()) {
        lazy.logConsole.debug(
          "_write: Settings unchanged. Did not write to disk."
        );
        Services.obs.notifyObservers(
          null,
          lazy.SearchUtils.TOPIC_SEARCH_SERVICE,
          "write-prevented-when-settings-unchanged"
        );
        Services.obs.notifyObservers(
          null,
          lazy.SearchUtils.TOPIC_SEARCH_SERVICE,
          "write-settings-to-disk-complete"
        );

        return;
      }

      // At this point, the settings and cached settings are different. We
      // write settings to disk and update #cachedSettings.
      this.#cachedSettings = structuredClone(this.#settings);

      lazy.logConsole.debug("_write: Writing to settings file.");
      await IOUtils.writeJSON(this.#settingsFilePath, settings, {
        compress: true,
        tmpPath: this.#settingsFilePath + ".tmp",
      });
      lazy.logConsole.debug("_write: settings file written to disk.");
      Services.obs.notifyObservers(
        null,
        lazy.SearchUtils.TOPIC_SEARCH_SERVICE,
        "write-settings-to-disk-complete"
      );
    } catch (ex) {
      lazy.logConsole.error("_write: Could not write to settings file:", ex);
    }
  }

  /**
   * Sets an attribute without verification.
   *
   * @param {string} name
   *   The name of the attribute to set.
   * @param {*} val
   *   The value to set.
   */
  setMetaDataAttribute(name, val) {
    this.#settings.metaData[name] = val;
    this._delayedWrite();
  }

  /**
   * Sets a verified attribute. This will save an additional hash
   * value, that can be verified when reading back.
   *
   * @param {string} name
   *   The name of the attribute to set.
   * @param {*} val
   *   The value to set.
   */
  setVerifiedMetaDataAttribute(name, val) {
    this.#settings.metaData[name] = val;
    this.#settings.metaData[this.getHashName(name)] =
      lazy.SearchUtils.getVerificationHash(val);
    this._delayedWrite();
  }

  /**
   * Gets an attribute without verification.
   *
   * @param {string} name
   *   The name of the attribute to get.
   * @returns {*}
   *   The value of the attribute, or undefined if not known.
   */
  getMetaDataAttribute(name) {
    return this.#settings.metaData[name] ?? undefined;
  }

  /**
   * Gets a copy of the settings metadata.
   *
   * @returns {*}
   *   A copy of the settings metadata object.
   */
  getSettingsMetaData() {
    return { ...this.#settings.metaData };
  }

  /**
   * Gets a verified attribute.
   *
   * @param {string} name
   *   The name of the attribute to get.
   * @param {boolean} isConfigEngine
   *   Whether the engine associated with the attribute is a config engine.
   * @returns {*}
   *   The value of the attribute.
   *   We return undefined if the value of the attribute is not known or does
   *   not match the verification hash.
   */
  getVerifiedMetaDataAttribute(name, isConfigEngine) {
    let attribute = this.getMetaDataAttribute(name);

    // If the selected engine is a config engine, we can relax the
    // verification hash check to reduce the annoyance for users who
    // backup/sync their profile in custom ways.
    if (isConfigEngine) {
      return attribute;
    }

    if (
      attribute &&
      this.getMetaDataAttribute(this.getHashName(name)) !=
        lazy.SearchUtils.getVerificationHash(attribute)
    ) {
      lazy.logConsole.warn(
        "getVerifiedMetaDataAttribute, invalid hash for",
        name
      );
      return undefined;
    }
    return attribute;
  }

  /**
   * Sets an attribute in #settings.engines._metaData
   *
   * @param {string} engineName
   *   The name of the engine.
   * @param {string} property
   *   The name of the attribute to set.
   * @param {*} value
   *   The value to set.
   */
  setEngineMetaDataAttribute(engineName, property, value) {
    let engines = [...this.#searchService._engines.values()];
    let engine = engines.find(e => e._name == engineName);
    if (engine) {
      engine._metaData[property] = value;
      this._delayedWrite();
    }
  }

  /**
   * Gets an attribute from #settings.engines._metaData
   *
   * @param {string} engineName
   *   The name of the engine.
   * @param {string} property
   *   The name of the attribute to get.
   * @returns {*}
   *   The value of the attribute, or undefined if not known.
   */
  getEngineMetaDataAttribute(engineName, property) {
    let engine = this.#settings.engines.find(e => e._name == engineName);
    return engine._metaData[property] ?? undefined;
  }

  /**
   * Returns the name for the hash for a particular attribute. This is
   * necessary because the default engine ID property is named `current`
   * with its hash as `hash`. All other hashes are in the `<name>Hash` format.
   *
   * @param {string} name
   *   The name of the attribute to get the hash name for.
   * @returns {string}
   *   The hash name to use.
   */
  getHashName(name) {
    // The "current" check remains here because we need to retrieve the
    // "current" hash name for the migration of engine ids. After the migration,
    // the "current" property is no longer used because we now store
    // "defaultEngineId" instead.
    if (name == "current") {
      return "hash";
    }
    return name + "Hash";
  }

  /**
   * Handles shutdown; writing the settings if necessary.
   *
   * @param {object} state
   *   The shutdownState object that is used to help analyzing the shutdown
   *   state in case of a crash or shutdown timeout.
   */
  async shutdown(state) {
    if (!this._batchTask) {
      return;
    }
    state.step = "Finalizing batched task";
    try {
      await this._batchTask.finalize();
      state.step = "Batched task finalized";
    } catch (ex) {
      state.step = "Batched task failed to finalize";

      state.latestError.message = "" + ex;
      if (ex && typeof ex == "object") {
        state.latestError.stack = ex.stack || undefined;
      }
    }
  }

  // nsIObserver
  observe(subject, topic, verb) {
    switch (topic) {
      case lazy.SearchUtils.TOPIC_ENGINE_MODIFIED:
        switch (verb) {
          case lazy.SearchUtils.MODIFIED_TYPE.ADDED:
          case lazy.SearchUtils.MODIFIED_TYPE.CHANGED:
          case lazy.SearchUtils.MODIFIED_TYPE.REMOVED:
            this._delayedWrite();
            break;
          case lazy.SearchUtils.MODIFIED_TYPE.ICON_CHANGED:
            // Config Search Engines have their icons stored in Remote
            // Settings, so we don't need to update the saved settings.
            if (!(subject.wrappedJSObject instanceof lazy.ConfigSearchEngine)) {
              this._delayedWrite();
            }
            break;
        }
        break;
      case lazy.SearchUtils.TOPIC_SEARCH_SERVICE:
        switch (verb) {
          case "engines-reloaded":
            this._delayedWrite();
            break;
        }
        break;
    }
  }

  /**
   * Compares the #settings and #cachedSettings objects.
   *
   * @returns {boolean}
   *   True if the objects have the same property and values.
   */
  isCurrentAndCachedSettingsEqual() {
    return lazy.ObjectUtils.deepEqual(this.#settings, this.#cachedSettings);
  }

  /**
   * This function writes to settings versions 6 and below. It does two
   * updates:
   *   1) Store engine ids.
   *   2) Store "defaultEngineId" and "privateDefaultEngineId" to replace
   *      "current" and "private" because we are no longer referencing the
   *      "current" and "private" attributes with engine names as their values.
   *
   * @param {object} clonedSettings
   *   The SearchService holds a deep copy of the settings file object. This
   *   clonedSettings is passed in as an argument from SearchService.
   */
  migrateEngineIds(clonedSettings) {
    if (clonedSettings.version <= 6) {
      lazy.logConsole.debug("migrateEngineIds: start");

      for (let engineSettings of clonedSettings.engines) {
        let engine = this.#getEngineByName(engineSettings._name);

        if (engine) {
          // Store the engine id
          engineSettings.id = engine.id;
        }
      }

      let currentDefaultEngine = this.#getEngineByName(
        clonedSettings.metaData.current
      );
      let privateDefaultEngine = this.#getEngineByName(
        clonedSettings.metaData.private
      );

      // As per SearchService._getEngineDefault, we relax the verification hash
      // check for application provided engines to reduce the annoyance for
      // users who backup/sync their profile in custom ways.
      if (
        currentDefaultEngine &&
        (currentDefaultEngine instanceof lazy.AppProvidedConfigEngine ||
          lazy.SearchUtils.getVerificationHash(
            clonedSettings.metaData.current
          ) == clonedSettings.metaData[this.getHashName("current")])
      ) {
        // Store the defaultEngineId
        this.setVerifiedMetaDataAttribute(
          "defaultEngineId",
          currentDefaultEngine.id
        );
      } else {
        this.setVerifiedMetaDataAttribute("defaultEngineId", "");
      }

      if (
        privateDefaultEngine &&
        (privateDefaultEngine instanceof lazy.AppProvidedConfigEngine ||
          lazy.SearchUtils.getVerificationHash(
            clonedSettings.metaData.private
          ) == clonedSettings.metaData[this.getHashName("private")])
      ) {
        // Store the privateDefaultEngineId
        this.setVerifiedMetaDataAttribute(
          "privateDefaultEngineId",
          privateDefaultEngine.id
        );
      } else {
        this.setVerifiedMetaDataAttribute("privateDefaultEngineId", "");
      }

      lazy.logConsole.debug("migrateEngineIds: done");
    }
  }

  /**
   * Finds the settings for the engine, based on the version of the settings
   * passed in. Older versions of settings used the engine name as the key,
   * whereas newer versions now use the engine id.
   *
   * @param {object} settings
   *   The saved settings object.
   * @param {string} engineId
   *   The id of the engine.
   * @param {string} engineName
   *   The name of the engine.
   * @returns {object|undefined}
   *   The engine settings if found, undefined otherwise.
   */
  static findSettingsForEngine(settings, engineId, engineName) {
    if (settings.version <= 6) {
      let engineSettings = settings.engines?.find(e => e._name == engineName);
      if (!engineSettings) {
        // If we can't find the engine settings with the current name,
        // see if there was an older name.
        let oldEngineName = ENGINE_ID_TO_OLD_NAME_MAP.get(engineId);
        if (oldEngineName) {
          engineSettings = settings.engines?.find(
            e => e._name == oldEngineName
          );
        }
      }
      return engineSettings;
    }
    return settings.engines?.find(e => e.id == engineId);
  }

  /**
   * Returns the engine associated with the name without SearchService
   * initialization checks.
   *
   * @param {string} engineName
   *   The name of the engine.
   * @returns {?SearchEngine}
   *   The associated engine if found, null otherwise.
   */
  #getEngineByName(engineName) {
    for (let engine of this.#searchService._engines.values()) {
      if (engine.name == engineName) {
        return engine;
      }
    }

    return null;
  }

  /**
   * Migrates older settings to the latest version.
   * Does not migrate the engine IDs yet because that happens
   * after the ApplicationProvidedEngines have been loaded.
   */
  async #migrateSettings() {
    this.#migrateTo6();
    this.#migrateTo8();
    this.#migrateTo9();
    this.#migrateTo10();
    this.#migrateTo11();
    await this.#migrateTo12();
    this.#migrateTo13();
    this.#migrateTo14();
  }

  #migrateTo6() {
    // Versions of gecko older than 82 stored the order flag as a preference.
    // See bug 1642995.
    if (
      this.#settings.version < 6 ||
      !("useSavedOrder" in this.#settings.metaData)
    ) {
      const prefName = "browser.search.useDBForOrder";
      let useSavedOrder = Services.prefs.getBoolPref(prefName, false);

      this.setMetaDataAttribute("useSavedOrder", useSavedOrder);

      // Clear the old pref so it isn't lying around.
      Services.prefs.clearUserPref(prefName);
    }
  }

  #migrateTo8() {
    // The load path is changed to better differentiate policy/user/
    // add-on engines for telemetry. See bug 1801813.
    if (this.#settings.version < 8 && Array.isArray(this.#settings.engines)) {
      for (let engine of this.#settings.engines) {
        if (!engine._loadPath) {
          continue;
        }
        if (engine._loadPath.includes("set-via-policy")) {
          engine._loadPath = "[policy]";
        } else if (engine._loadPath.includes("set-via-user")) {
          engine._loadPath = "[user]";
        } else if (
          engine._loadPath.startsWith("[other]addEngineWithDetails:")
        ) {
          engine._loadPath = engine._loadPath.replace(
            "[other]addEngineWithDetails:",
            "[addon]"
          );
        }
      }
    }
  }

  #migrateTo9() {
    // The hiddenOneOffs pref is moved to the search settings.
    // See bug 1643887.
    if (this.#settings.version < 9 && this.#settings.engines) {
      const hiddenOneOffsPrefs = Services.prefs.getStringPref(
        "browser.search.hiddenOneOffs",
        ""
      );
      for (const engine of this.#settings.engines) {
        engine._metaData.hideOneOffButton = hiddenOneOffsPrefs.includes(
          engine._name
        );
      }
      Services.prefs.clearUserPref("browser.search.hiddenOneOffs");
    }
  }

  #migrateTo10() {
    // The format of the IDs of app provided engines is changed.
    // See bug 1870687.
    if (
      this.#settings.version > 6 &&
      this.#settings.version < 10 &&
      this.#settings.engines
    ) {
      let changedEngines = new Map();
      for (let engine of this.#settings.engines) {
        if (engine._isAppProvided && engine.id) {
          let oldId = engine.id;
          engine.id = engine.id
            .replace("@search.mozilla.orgdefault", "")
            .replace("@search.mozilla.org", "-");
          changedEngines.set(oldId, engine.id);
        }
      }

      const PROPERTIES_CONTAINING_IDS = [
        "privateDefaultEngineId",
        "appDefaultEngineId",
        "defaultEngineId",
      ];

      for (let prop of PROPERTIES_CONTAINING_IDS) {
        if (changedEngines.has(this.#settings.metaData[prop])) {
          this.#settings.metaData[prop] = changedEngines.get(
            this.#settings.metaData[prop]
          );
        }
      }
    }
  }

  #migrateTo11() {
    // The keys of _iconMapObj are changed from width and height to width only.
    // See bug 1655066.
    if (this.#settings.version < 11 && this.#settings.engines) {
      for (let engine of this.#settings.engines) {
        if (!engine._iconMapObj) {
          continue;
        }
        let oldIconMap = engine._iconMapObj;
        engine._iconMapObj = {};

        for (let [sizeStr, icon] of Object.entries(oldIconMap)) {
          let sizeObj;
          try {
            sizeObj = JSON.parse(sizeStr);
          } catch {}
          if (
            typeof sizeObj === "object" &&
            "width" in sizeObj &&
            parseInt(sizeObj.width) > 0 &&
            sizeObj.width == sizeObj.height
          ) {
            engine._iconMapObj[sizeObj.width] = icon;
          } else if (typeof sizeObj === "number") {
            // This happens if the user copies a version 11+ search config to
            // an old install, which gets updated eventually; see bug 1940533.
            engine._iconMapObj[sizeObj] = icon;
          }
        }
      }
    }
  }

  async #migrateTo12() {
    // _iconURL is removed and its icon is stored in _iconMapObj instead.
    // See bug 1655076.
    if (this.#settings.version < 12 && this.#settings.engines) {
      for (let engine of this.#settings.engines) {
        if (engine._iconURL) {
          let iconURL = engine._iconURL;
          delete engine._iconURL;

          let uri = lazy.SearchUtils.makeURI(iconURL);
          if (!uri) {
            continue;
          }

          // The URL should be either a data or moz-extension URL so this should
          // always succeed and be fast. We skip other schemes just to be sure.
          // If we fail to fetch or decode the icon, we assume it's 16x16.
          switch (uri.scheme) {
            case "moz-extension":
              try {
                await lazy.AddonManager.readyPromise;
              } catch (e) {
                if (e == "shutting down") {
                  throw new Error("Addon manager shutting down");
                } else {
                  throw new Error("Addon manager failed");
                }
              }
              break;
            case "data":
              break;
            default:
              continue;
          }

          let byteArray, contentType;
          try {
            [byteArray, contentType] = await lazy.SearchUtils.fetchIcon(uri);
          } catch {
            lazy.logConsole.warn(
              `_iconURL migration: failed to load icon of search engine ${engine._name}.`
            );
            engine._iconMapObj ||= {};
            engine._iconMapObj[16] = iconURL;
            continue;
          }

          // MAX_ICON_SIZE is not enforced in some cases. In those cases, we
          // rescale the icon to 32x32.
          if (byteArray.length > lazy.SearchUtils.MAX_ICON_SIZE) {
            try {
              [byteArray, contentType] = lazy.SearchUtils.rescaleIcon(
                byteArray,
                contentType
              );
              let url =
                "data:" + contentType + ";base64," + byteArray.toBase64();

              engine._iconMapObj ||= {};
              engine._iconMapObj[32] = url;
            } catch {
              lazy.logConsole.warn(
                `_iconURL migration: failed to resize icon of search engine ${engine._name}.`
              );
            }
            continue;
          }

          let size = lazy.SearchUtils.decodeSize(byteArray, contentType, 16);
          engine._iconMapObj ||= {};
          engine._iconMapObj[size] = iconURL;
        }
      }
    }
  }

  #migrateTo13() {
    // App provided engines are renamed to config engines, see bug 1973315.
    // At the same time, we also rename _isBuiltin _isConfigEngine.
    // This originally happed in bug 1631898, but instead of adding a migration,
    // the initial implementation simply checked both values.
    if (this.#settings.version < 13 && this.#settings.engines) {
      for (let engine of this.#settings.engines) {
        if (engine._isAppProvided) {
          delete engine._isAppProvided;
          engine._isConfigEngine = true;
        } else if (engine._isBuiltin) {
          delete engine._isBuiltin;
          engine._isConfigEngine = true;
        }
      }
    }
  }

  #migrateTo14() {
    // Bug 2046554 - Force the remove search engine enterprise policy to be
    // re-applied, to ensure previously hidden engines are fully disabled. This
    // is in case a user re-enabled them in version 152 when they were visible
    // for a while.
    //
    // Note: This is here rather than in ProfileDataUpgrader.sys.mjs, as it
    // means we can uplift without having to worry about 153 and 154 having
    // different UI migration versions.
    Services.prefs.clearUserPref(
      "browser.policies.runOncePerModification.removeSearchEngines"
    );
  }
}
