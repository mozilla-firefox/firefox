/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

import { StartupCache } from "resource://gre/modules/ExtensionParent.sys.mjs";
import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { ExtensionDNRLimits } from "./ExtensionDNRLimits.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  Extension: "resource://gre/modules/Extension.sys.mjs",
  ExtensionDNR: "resource://gre/modules/ExtensionDNR.sys.mjs",
  ExtensionDNRLimits: "resource://gre/modules/ExtensionDNRLimits.sys.mjs",
  Schemas: "resource://gre/modules/Schemas.sys.mjs",
  aomStartup: {
    service: "@mozilla.org/addons/addon-manager-startup;1",
    iid: Ci.amIAddonManagerStartup,
  },
});

const LAST_UPDATE_TAG_PREF_PREFIX = "extensions.dnr.lastStoreUpdateTag.";

const { DefaultMap, ExtensionError } = ExtensionUtils;

// DNR Rules store subdirectory/file names and file extensions.
//
// NOTE: each extension's stored rules are stored in a per-extension file
// and stored rules filename is derived from the extension uuid assigned
// at install time.
const RULES_STORE_DIRNAME = "extension-dnr";
const RULES_STORE_FILEEXT = ".json.lz4";
const RULES_CACHE_FILENAME = "extensions-dnr.sc.lz4";

const requireTestOnlyCallers = () => {
  if (!Services.env.exists("XPCSHELL_TEST_PROFILE_DIR")) {
    throw new Error("This should only be called from XPCShell tests");
  }
};

/**
 * Internal representation of the enabled static rulesets (used in StoreData
 * and Store methods type signatures).
 *
 * @typedef {object} EnabledStaticRuleset
 * @inner
 * @property {number} idx
 *           Represent the position of the static ruleset in the manifest
 *           `declarative_net_request.rule_resources` array.
 * @property {Array<Rule>} rules
 *           Represent the array of the DNR rules associated with the static
 *           ruleset.
 */

// Class defining the format of the data stored into the per-extension files
// managed by RulesetsStore.
//
// StoreData instances are saved in the profile extension-dir subdirectory as
// lz4-compressed JSON files, only the ruleset_id is stored on disk for the
// enabled static rulesets (while the actual rules would need to be loaded back
// from the related rules JSON files part of the extension assets).
class StoreData {
  // NOTE: Update schema version upgrade handling code in `StoreData.fromJSON`
  // along with bumps to the schema version here.
  //
  // Changelog:
  // - 1: Initial DNR store schema:
  //      Initial implementation officially release in Firefox 113.
  //      Support for disableStaticRuleIds added in Firefox 128 (Bug 1810762).
  static VERSION = 1;

  static getLastUpdateTagPref(extensionUUID) {
    return `${LAST_UPDATE_TAG_PREF_PREFIX}${extensionUUID}`;
  }

  static getLastUpdateTag(extensionUUID) {
    return Services.prefs.getCharPref(
      this.getLastUpdateTagPref(extensionUUID),
      null
    );
  }

  static storeLastUpdateTag(extensionUUID, lastUpdateTag) {
    Services.prefs.setCharPref(
      this.getLastUpdateTagPref(extensionUUID),
      lastUpdateTag
    );
  }

  static clearLastUpdateTagPref(extensionUUID) {
    Services.prefs.clearUserPref(this.getLastUpdateTagPref(extensionUUID));
  }

  static isStaleCacheEntry(extensionUUID, cacheStoreData) {
    return (
      // Drop the cache entry if the data stored doesn't match the current
      // StoreData schema version (this shouldn't happen unless the file
      // have been manually restored by the user from an older firefox version).
      cacheStoreData.schemaVersion !== this.VERSION ||
      // Drop the cache entry if the lastUpdateTag from the cached data entry
      // doesn't match the lastUpdateTag recorded in the prefs, the tag is applied
      // with a per-extension granularity to reduce the chances of cache misses
      // last update on the cached data for an unrelated extensions did not make it
      // to disk).
      cacheStoreData.lastUpdateTag != this.getLastUpdateTag(extensionUUID)
    );
  }

  #extUUID;
  #initialLastUdateTag;
  #temporarilyInstalled;

  /**
   * @param {Extension} extension
   *        The extension the StoreData is associated to.
   * @param {object} params
   * @param {string} [params.extVersion]
   *        extension version
   * @param {string} [params.lastUpdateTag]
   *        a tag associated to the data. It is only passed when we are loading the data
   *        from the StartupCache file, while a new tag uuid string will be generated
   *        for brand new data (and then new ones generated on each calls to the `updateRulesets`
   *        method).
   * @param {number} [params.schemaVersion=StoreData.VERSION]
   *        file schema version
   * @param {Map<string, EnabledStaticRuleset>} [params.staticRulesets=new Map()]
   *        map of the enabled static rulesets by ruleset_id, as resolved by
   *        `Store.prototype.#getManifestStaticRulesets`.
   *        NOTE: This map is converted in an array of the ruleset_id strings when the StoreData
   *        instance is being stored on disk (see `toJSON` method) and then converted back to a Map
   *        by `Store.prototype.#getManifestStaticRulesets` when the data is loaded back from disk.
   * @param {object} [params.disabledStaticRuleIds={}]
   *        map of the disabled static rule ids by ruleset_id. This map is updated by the extension
   *        calls to the updateStaticRules API method and persisted across browser session,
   *        and browser and extension updates. Disabled rule ids for a disabled ruleset are going
   *        to become effective when the disabled ruleset is enabled (e.g. through updateEnabledRulesets
   *        API calls or through manifest in extension updates).
   * @param {Array<Rule>} [params.dynamicRuleset=[]]
   *        array of dynamic rules stored by the extension.
   */
  constructor(
    extension,
    {
      extVersion,
      lastUpdateTag,
      dynamicRuleset,
      disabledStaticRuleIds,
      staticRulesets,
      schemaVersion,
    } = {}
  ) {
    if (!(extension instanceof lazy.Extension)) {
      throw new Error("Missing mandatory extension parameter");
    }
    this.schemaVersion = schemaVersion || StoreData.VERSION;
    this.extVersion = extVersion ?? extension.version;
    this.#extUUID = extension.uuid;
    // Used to skip storing the data in the startupCache or storing the lastUpdateTag in
    // the about:config prefs.
    this.#temporarilyInstalled = extension.temporarilyInstalled;
    // The lastUpdateTag gets set (and updated) by calls to updateRulesets.
    this.lastUpdateTag = undefined;
    this.#initialLastUdateTag = lastUpdateTag;

    this.#updateRulesets({
      staticRulesets: staticRulesets ?? new Map(),
      disabledStaticRuleIds: disabledStaticRuleIds ?? {},
      dynamicRuleset: dynamicRuleset ?? [],
      lastUpdateTag,
    });
  }

  isFromStartupCache() {
    return this.#initialLastUdateTag == this.lastUpdateTag;
  }

  isFromTemporarilyInstalled() {
    return this.#temporarilyInstalled;
  }

  get isEmpty() {
    return !this.staticRulesets.size && !this.dynamicRuleset.length;
  }

  /**
   * Updates the static and or dynamic rulesets stored for the related
   * extension.
   *
   * NOTE: This method also:
   * - regenerates the lastUpdateTag associated as an unique identifier
   *   of the revision for the stored data (used to detect stale startup
   *   cache data)
   * - stores the lastUpdateTag into an about:config pref associated to
   *   the extension uuid (also used as part of detecting stale startup
   *   cache data), unless the extension is installed temporarily.
   *
   * @param {object} params
   * @param {Map<string, EnabledStaticRuleset>} [params.staticRulesets]
   *        optional new updated Map of static rulesets
   *        (static rulesets are unchanged if not passed).
   * @param {object} [params.disabledStaticRuleIds]
   *        optional new updated Map of static rules ids disabled individually.
   * @param {Array<Rule>} [params.dynamicRuleset=[]]
   *        optional array of updated dynamic rules
   *        (dynamic rules are unchanged if not passed).
   */
  updateRulesets({
    staticRulesets,
    disabledStaticRuleIds,
    dynamicRuleset,
  } = {}) {
    let currentUpdateTag = this.lastUpdateTag;
    let lastUpdateTag = this.#updateRulesets({
      staticRulesets,
      disabledStaticRuleIds,
      dynamicRuleset,
    });

    // Tag each cache data entry with a value synchronously stored in an
    // about:config prefs, if on a browser restart the tag in the startupCache
    // data entry doesn't match the one in the about:config pref then the startup
    // cache entry is dropped as stale (assuming an issue prevented the updated
    // cache data to be written on disk, e.g. browser crash, failure on writing
    // on disk etc.), each entry is tagged separately to decrease the chances
    // of cache misses on unrelated cache data entries if only a few extension
    // got stale data in the startup cache file.
    if (
      !this.isFromTemporarilyInstalled() &&
      currentUpdateTag != lastUpdateTag
    ) {
      StoreData.storeLastUpdateTag(this.#extUUID, lastUpdateTag);
    }
  }

  #updateRulesets({
    staticRulesets = null,
    disabledStaticRuleIds = null,
    dynamicRuleset = null,
    lastUpdateTag = Services.uuid.generateUUID().toString(),
  } = {}) {
    if (staticRulesets) {
      this.staticRulesets = staticRulesets;
    }

    if (disabledStaticRuleIds) {
      this.disabledStaticRuleIds = disabledStaticRuleIds;
    }

    if (dynamicRuleset) {
      this.dynamicRuleset = dynamicRuleset;
    }

    if (staticRulesets || dynamicRuleset) {
      this.lastUpdateTag = lastUpdateTag;
    }

    return this.lastUpdateTag;
  }

  // This method is used to convert the data in the format stored on disk
  // as a JSON file.
  toJSON() {
    const data = {
      schemaVersion: this.schemaVersion,
      extVersion: this.extVersion,
      // Only store the array of the enabled ruleset_id in the set of data
      // persisted in a JSON form.
      staticRulesets: this.staticRulesets
        ? Array.from(this.staticRulesets.entries(), ([id, _ruleset]) => id)
        : undefined,
      disabledStaticRuleIds:
        this.disabledStaticRuleIds &&
        Object.keys(this.disabledStaticRuleIds).length
          ? this.disabledStaticRuleIds
          : undefined,
      dynamicRuleset: this.dynamicRuleset,
    };
    return data;
  }

  // This method is used to convert the data back to a StoreData class from
  // the format stored on disk as a JSON file.
  // NOTE: this method should be kept in sync with toJSON and make sure that
  // we do deserialize the same property we are serializing into the JSON file.
  static fromJSON(paramsFromJSON, extension) {
    // TODO: Add schema versions migrations here if necessary.
    // if (paramsFromJSON.version < StoreData.VERSION) {
    //   paramsFromJSON = this.upgradeStoreDataSchema(paramsFromJSON);
    // }

    let {
      schemaVersion,
      extVersion,
      staticRulesets,
      disabledStaticRuleIds,
      dynamicRuleset,
    } = paramsFromJSON;

    return new StoreData(extension, {
      schemaVersion,
      extVersion,
      staticRulesets,
      disabledStaticRuleIds,
      dynamicRuleset,
    });
  }
}

class Queue {
  #tasks = [];
  #runningTask = null;
  #closed = false;

  get hasPendingTasks() {
    return !!this.#runningTask || !!this.#tasks.length;
  }

  get isClosed() {
    return this.#closed;
  }

  async close() {
    if (this.#closed) {
      const lastTask = this.#tasks[this.#tasks.length - 1];
      return lastTask?.deferred.promise;
    }
    const drainedQueuePromise = this.queueTask(() => {});
    this.#closed = true;
    return drainedQueuePromise;
  }

  queueTask(callback) {
    if (this.#closed) {
      throw new Error("Unexpected queueTask call on closed queue");
    }
    const deferred = Promise.withResolvers();
    this.#tasks.push({ callback, deferred });
    // Run the queued task right away if there isn't one already running.
    if (!this.#runningTask) {
      this.#runNextTask();
    }
    return deferred.promise;
  }

  async #runNextTask() {
    if (!this.#tasks.length) {
      this.#runningTask = null;
      return;
    }

    this.#runningTask = this.#tasks.shift();
    const { callback, deferred } = this.#runningTask;
    try {
      let result = callback();
      if (result instanceof Promise) {
        result = await result;
      }
      deferred.resolve(result);
    } catch (err) {
      deferred.reject(err);
    }

    this.#runNextTask();
  }
}

/**
 * Class managing the rulesets persisted across browser sessions.
 *
 * The data gets stored in two per-extension files:
 *
 * - `ProfD/extension-dnr/EXT_UUID.json.lz4` is a lz4-compressed JSON file that is expected to include
 *   the ruleset ids for the enabled static rulesets and the dynamic rules.
 *
 * All browser data stored is expected to be persisted across browser updates, but the enabled static ruleset
 * ids are expected to be reset and reinitialized from the extension manifest.json properties when the
 * add-on is being updated (either downgraded or upgraded).
 *
 * In case of unexpected data schema downgrades (which may be hit if the user explicit pass --allow-downgrade
 * while using an older browser version than the one used when the data has been stored), the entire stored
 * data is reset and re-initialized from scratch based on the manifest.json file.
 */
class RulesetsStore {
  constructor() {
    // Map<extensionUUID, StoreData>
    this._data = new Map();
    // Map<extensionUUID, Promise<StoreData>>
    this._dataPromises = new Map();
    // Map<extensionUUID, Promise<void>>
    this._savePromises = new Map();
    // Map<extensionUUID, Queue>
    this._dataUpdateQueues = new DefaultMap(() => new Queue());
    // Promise to await on to ensure the store parent directory exist
    // (the parent directory is shared by all extensions and so we only need one).
    this._ensureStoreDirectoryPromise = null;
    // Promise to await on to ensure (there is only one startupCache file for all
    // extensions and so we only need one):
    // - the cache file parent directory exist
    // - the cache file data has been loaded (if any was available and matching
    //   the last DNR data stored on disk)
    // - the cache file data has been saved.
    this._ensureCacheDirectoryPromise = null;
    this._ensureCacheLoaded = null;
    this._saveCacheTask = null;
    // Map of the raw data read from the startupCache.
    // Map<extensionUUID, Object>
    this._startupCacheData = new Map();
  }

  /**
   * Wait for the startup cache data to be stored on disk.
   *
   * NOTE: Only meant to be used in xpcshell tests.
   *
   * @returns {Promise<void>}
   */
  async waitSaveCacheDataForTesting() {
    requireTestOnlyCallers();
    if (this._saveCacheTask) {
      if (this._saveCacheTask.isRunning) {
        await this._saveCacheTask._runningPromise;
      }
      // #saveCacheDataNow() may schedule another save if anything has changed in between
      while (this._saveCacheTask.isArmed) {
        this._saveCacheTask.disarm();
        await this.#saveCacheDataNow();
      }
    }
  }

  /**
   * Remove store file for the given extension UUId from disk (used to remove all
   * data on addon uninstall).
   *
   * @param {string} extensionUUID
   * @returns {Promise<void>}
   */
  async clearOnUninstall(extensionUUID) {
    // TODO(Bug 1825510): call scheduleCacheDataSave to update the startup cache data
    // stored on disk, but skip it if it is late in the application shutdown.
    StoreData.clearLastUpdateTagPref(extensionUUID);
    const storeFile = this.#getStoreFilePath(extensionUUID);

    // TODO(Bug 1803363): consider collect telemetry on DNR store file removal errors.
    // TODO: consider catch and report unexpected errors
    await IOUtils.remove(storeFile, { ignoreAbsent: true });
  }

  /**
   * Load (or initialize) the store file data for the given extension and
   * return an Array of the dynamic rules.
   *
   * @param {Extension} extension
   *
   * @returns {Promise<Array<Rule>>}
   *          Resolve to a reference to the dynamic rules array.
   *          NOTE: the caller should never mutate the content of this array,
   *          updates to the dynamic rules should always go through
   *          the `updateDynamicRules` method.
   */
  async getDynamicRules(extension) {
    let data = await this.#getDataPromise(extension);
    return data.dynamicRuleset;
  }

  /**
   * Load (or initialize) the store file data for the given extension and
   * return a Map of the enabled static rulesets and their related rules.
   *
   * - if the extension manifest doesn't have any static rulesets declared in the
   *   manifest, returns null
   *
   * - if the extension version from the stored data doesn't match the current
   *   extension versions, the static rules are being reloaded from the manifest.
   *
   * @param {Extension} extension
   *
   * @returns {Promise<Map<ruleset_id, EnabledStaticRuleset>>}
   *          Resolves to a reference to the static rulesets map.
   *          NOTE: the caller should never mutate the content of this map,
   *          updates to the enabled static rulesets should always go through
   *          the `updateEnabledStaticRulesets` method.
   */
  async getEnabledStaticRulesets(extension) {
    let data = await this.#getDataPromise(extension);
    return data.staticRulesets;
  }

  /**
   * Returns the number of static rules still available to the given extension.
   *
   * @param {Extension} extension
   *
   * @returns {Promise<number>}
   *          Resolves to the number of static rules available.
   */
  async getAvailableStaticRuleCount(extension) {
    const { GUARANTEED_MINIMUM_STATIC_RULES } = lazy.ExtensionDNRLimits;

    const existingRulesetIds = this.#getExistingStaticRulesetIds(extension);
    if (!existingRulesetIds.length) {
      return GUARANTEED_MINIMUM_STATIC_RULES;
    }

    const enabledRulesets = await this.getEnabledStaticRulesets(extension);
    const enabledRulesCount = Array.from(enabledRulesets.values()).reduce(
      (acc, ruleset) => acc + ruleset.rules.length,
      0
    );

    return GUARANTEED_MINIMUM_STATIC_RULES - enabledRulesCount;
  }

  /**
   * Returns the static rule ids disabled individually for the given extension
   * and static ruleset id.
   *
   * @param {Extension} extension
   * @param {string} rulesetId
   *
   * @returns {Promise<Array<number>>}
   *          Resolves to the array of rule ids disabled.
   */
  async getDisabledRuleIds(extension, rulesetId) {
    const existingRulesetIds = this.#getExistingStaticRulesetIds(extension);
    if (!existingRulesetIds.includes(rulesetId)) {
      throw new ExtensionError(`Invalid ruleset id: "${rulesetId}"`);
    }

    let data = await this.#getDataPromise(extension);
    return data.disabledStaticRuleIds[rulesetId] ?? [];
  }

  /**
   * Initialize the DNR store for the given extension, it does also queue the task to make
   * sure that extension DNR API calls triggered while the initialization may still be
   * in progress will be executed sequentially.
   *
   * @param {Extension}     extension
   *
   * @returns {Promise<void>} A promise resolved when the async initialization has been
   *                          completed.
   */
  async initExtension(extension) {
    const ensureExtensionRunning = () => {
      if (extension.hasShutdown) {
        throw new Error(
          `DNR store initialization abort, extension is already shutting down: ${extension.id}`
        );
      }
    };

    // Make sure we wait for pending save promise to have been
    // completed and old data unloaded (this may be hit if an
    // extension updates or reloads while there are still
    // rules updates being processed and then stored on disk).
    ensureExtensionRunning();
    if (this._savePromises.has(extension.uuid)) {
      Cu.reportError(
        `Unexpected pending save task while reading DNR data after an install/update of extension "${extension.id}"`
      );
      // await pending saving data to be saved and unloaded.
      await this.#unloadData(extension.uuid);
      // Make sure the extension is still running after awaiting on
      // unloadData to be completed.
      ensureExtensionRunning();
    }

    return this._dataUpdateQueues.get(extension.uuid).queueTask(() => {
      return this.#initExtension(extension);
    });
  }

  /**
   * Update the dynamic rules, queue changes to prevent races between calls
   * that may be triggered while an update is still in process.
   *
   * @param {Extension}     extension
   * @param {object}        params
   * @param {Array<number>} [params.removeRuleIds=[]]
   * @param {Array<Rule>} [params.addRules=[]]
   *
   * @returns {Promise<void>} A promise resolved when the dynamic rules async update has
   *                          been completed.
   */
  async updateDynamicRules(extension, { removeRuleIds, addRules }) {
    return this._dataUpdateQueues.get(extension.uuid).queueTask(() => {
      return this.#updateDynamicRules(extension, {
        removeRuleIds,
        addRules,
      });
    });
  }

  /**
   * Update the static rules ids disabled individually on a given static ruleset id,
   * queue changes to prevent races between calls that may be triggered while an
   * update is still in process.
   *
   * @param {Extension}     extension
   * @param {object}        params
   * @param {string}        [params.rulesetId]
   * @param {Array<number>} [params.disableRuleIds]
   * @param {Array<number>} [params.enableRuleIds]
   *
   * @returns {Promise<void>} A promise resolved when the disabled rules async update has
   *                          been completed.
   */
  async updateStaticRules(
    extension,
    { rulesetId, disableRuleIds, enableRuleIds }
  ) {
    return this._dataUpdateQueues.get(extension.uuid).queueTask(() => {
      return this.#updateStaticRules(extension, {
        rulesetId,
        disableRuleIds,
        enableRuleIds,
      });
    });
  }

  /**
   * Update the enabled rulesets, queue changes to prevent races between calls
   * that may be triggered while an update is still in process.
   *
   * @param {Extension}     extension
   * @param {object}        params
   * @param {Array<string>} [params.disableRulesetIds=[]]
   * @param {Array<string>} [params.enableRulesetIds=[]]
   *
   * @returns {Promise<void>} A promise resolved when the enabled static rulesets async
   *                          update has been completed.
   */
  async updateEnabledStaticRulesets(
    extension,
    { disableRulesetIds, enableRulesetIds }
  ) {
    return this._dataUpdateQueues.get(extension.uuid).queueTask(() => {
      return this.#updateEnabledStaticRulesets(extension, {
        disableRulesetIds,
        enableRulesetIds,
      });
    });
  }

  /**
   * Update DNR RulesetManager rules to match the current DNR rules enabled in the DNRStore.
   *
   * @param {Extension} extension
   * @param {object}    [params]
   * @param {boolean}   [params.updateStaticRulesets=true]
   * @param {boolean}   [params.updateDynamicRuleset=true]
   */
  updateRulesetManager(
    extension,
    { updateStaticRulesets = true, updateDynamicRuleset = true } = {}
  ) {
    if (!updateStaticRulesets && !updateDynamicRuleset) {
      return;
    }

    if (
      !this._dataPromises.has(extension.uuid) ||
      !this._data.has(extension.uuid)
    ) {
      throw new Error(
        `Unexpected call to updateRulesetManager before DNR store was fully initialized for extension "${extension.id}"`
      );
    }
    const data = this._data.get(extension.uuid);
    const ruleManager = lazy.ExtensionDNR.getRuleManager(extension);

    if (updateStaticRulesets) {
      let staticRulesetsMap = data.staticRulesets;
      // Convert into array and ensure order match the order of the rulesets in
      // the extension manifest.
      const enabledStaticRules = [];
      // Order the static rulesets by index of rule_resources in manifest.json.
      const orderedRulesets = Array.from(staticRulesetsMap.entries()).sort(
        ([_idA, rsA], [_idB, rsB]) => rsA.idx - rsB.idx
      );
      for (const [rulesetId, ruleset] of orderedRulesets) {
        enabledStaticRules.push({
          id: rulesetId,
          rules: ruleset.rules,
          disabledRuleIds: data.disabledStaticRuleIds[rulesetId]
            ? new Set(data.disabledStaticRuleIds[rulesetId])
            : null,
        });
      }
      ruleManager.setEnabledStaticRulesets(enabledStaticRules);
    }

    if (updateDynamicRuleset) {
      ruleManager.setDynamicRules(data.dynamicRuleset);
    }
  }

  /**
   * Return the store file path for the given the extension's uuid and the cache
   * file with startupCache data for all the extensions.
   *
   * @param {string} extensionUUID
   * @returns {{ storeFile: string | void, cacheFile: string}}
   *          An object including the full paths to both the per-extension store file
   *          for the given extension UUID and the full path to the single startupCache
   *          file (which would include the cached data for all the extensions).
   */
  getFilePaths(extensionUUID) {
    return {
      storeFile: this.#getStoreFilePath(extensionUUID),
      cacheFile: this.#getCacheFilePath(),
    };
  }

  /**
   * Save the data for the given extension on disk.
   *
   * @param {Extension} extension
   */
  async save(extension) {
    const { uuid, id } = extension;
    let savePromise = this._savePromises.get(uuid);

    if (!savePromise) {
      savePromise = this.#saveNow(uuid, id);
      this._savePromises.set(uuid, savePromise);
      IOUtils.profileBeforeChange.addBlocker(
        `Flush WebExtension DNR RulesetsStore: ${id}`,
        savePromise
      );
    }

    return savePromise;
  }

  /**
   * Register an onClose shutdown handler to cleanup the data from memory when
   * the extension is shutting down.
   *
   * @param {Extension} extension
   * @returns {void}
   */
  unloadOnShutdown(extension) {
    if (extension.hasShutdown) {
      throw new Error(
        `DNR store registering an extension shutdown handler too late, the extension is already shutting down: ${extension.id}`
      );
    }

    const extensionUUID = extension.uuid;
    extension.callOnClose({
      close: async () => this.#unloadData(extensionUUID),
    });
  }

  /**
   * Return a branch new StoreData instance given an extension.
   *
   * @param {Extension} extension
   * @returns {StoreData}
   */
  #getDefaults(extension) {
    return new StoreData(extension, { extVersion: extension.version });
  }

  /**
   * Return the cache file path.
   *
   * @returns {string}
   *          The absolute path to the startupCache file.
   */
  #getCacheFilePath() {
    // When the application version changes, this file is removed by
    // RemoveComponentRegistries in nsAppRunner.cpp.
    return PathUtils.join(
      Services.dirsvc.get("ProfLD", Ci.nsIFile).path,
      "startupCache",
      RULES_CACHE_FILENAME
    );
  }

  /**
   * Return the path to the store file given the extension's uuid.
   *
   * @param {string} extensionUUID
   * @returns {string} Full path to the store file for the extension.
   */
  #getStoreFilePath(extensionUUID) {
    return PathUtils.join(
      Services.dirsvc.get("ProfD", Ci.nsIFile).path,
      RULES_STORE_DIRNAME,
      `${extensionUUID}${RULES_STORE_FILEEXT}`
    );
  }

  #ensureCacheDirectory() {
    if (this._ensureCacheDirectoryPromise === null) {
      const file = this.#getCacheFilePath();
      this._ensureCacheDirectoryPromise = IOUtils.makeDirectory(
        PathUtils.parent(file),
        {
          ignoreExisting: true,
          createAncestors: true,
        }
      );
    }
    return this._ensureCacheDirectoryPromise;
  }

  #ensureStoreDirectory(extensionUUID) {
    // Currently all extensions share the same directory, so we can re-use this promise across all
    // `#ensureStoreDirectory` calls.
    if (this._ensureStoreDirectoryPromise === null) {
      const file = this.#getStoreFilePath(extensionUUID);
      this._ensureStoreDirectoryPromise = IOUtils.makeDirectory(
        PathUtils.parent(file),
        {
          ignoreExisting: true,
          createAncestors: true,
        }
      );
    }
    return this._ensureStoreDirectoryPromise;
  }

  #getDataPromise(extension) {
    let dataPromise = this._dataPromises.get(extension.uuid);
    if (!dataPromise) {
      if (extension.hasShutdown) {
        throw new Error(
          `DNR store data loading aborted, the extension is already shutting down: ${extension.id}`
        );
      }

      // Note: when dataPromise resolves, this._data and this._dataPromises are
      // set. Keep this logic in sync with the end of #initExtension().

      this.unloadOnShutdown(extension);
      dataPromise = this.#readData(extension);
      this._dataPromises.set(extension.uuid, dataPromise);
    }
    return dataPromise;
  }

  /**
   * Reads the store file for the given extensions and all rules
   * for the enabled static ruleset ids listed in the store file.
   *
   * @typedef {string} ruleset_id
   *
   * @param {Extension} extension
   * @param {object} [options]
   * @param {Array<string>} [options.enabledRulesetIds]
   *        An optional array of enabled ruleset ids to be loaded
   *        (used to load a specific group of static rulesets,
   *        either when the list of static rules needs to be recreated based
   *        on the enabled rulesets, or when the extension is
   *        changing the enabled rulesets using the `updateEnabledRulesets`
   *        API method).
   * @param {boolean} [options.isUpdateEnabledRulesets]
   *        Whether this is a call by updateEnabledRulesets. When true,
   *        `enabledRulesetIds` contains the IDs of disabled rulesets that
   *        should be enabled. Already-enabled rulesets are not included in
   *        `enabledRulesetIds`.
   * @param {import("ExtensionDNR.sys.mjs").RuleQuotaCounter} [options.ruleQuotaCounter]
   *        The counter of already-enabled rules that are not part of
   *        `enabledRulesetIds`. Set when `isUpdateEnabledRulesets` is true.
   *        This method may mutate its internal counters.
   * @returns {Promise<Map<ruleset_id, EnabledStaticRuleset>>}
   *          map of the enabled static rulesets by ruleset_id.
   */
  async #getManifestStaticRulesets(
    extension,
    {
      enabledRulesetIds = null,
      isUpdateEnabledRulesets = false,
      ruleQuotaCounter,
    } = {}
  ) {
    // Map<ruleset_id, EnabledStaticRuleset>}
    const rulesets = new Map();

    const ruleResources =
      extension.manifest.declarative_net_request?.rule_resources;
    if (!Array.isArray(ruleResources)) {
      return rulesets;
    }

    if (!isUpdateEnabledRulesets) {
      ruleQuotaCounter = new lazy.ExtensionDNR.RuleQuotaCounter(
        "GUARANTEED_MINIMUM_STATIC_RULES"
      );
    }

    const {
      MAX_NUMBER_OF_ENABLED_STATIC_RULESETS,
      // Warnings on MAX_NUMBER_OF_STATIC_RULESETS are already
      // reported (see ExtensionDNR.validateManifestEntry, called
      // from the DNR API onManifestEntry callback).
    } = lazy.ExtensionDNRLimits;

    for (let [idx, { id, enabled, path }] of ruleResources.entries()) {
      // If passed enabledRulesetIds is used to determine if the enabled
      // rules in the manifest should be overridden from the list of
      // enabled static rulesets stored on disk.
      if (Array.isArray(enabledRulesetIds)) {
        enabled = enabledRulesetIds.includes(id);
      }

      // Duplicated ruleset ids are validated as part of the JSONSchema validation,
      // here we log a warning to signal that we are ignoring it if when the validation
      // error isn't strict (e.g. for non temporarily installed, which shouldn't normally
      // hit in the long run because we can also validate it before signing the extension).
      if (rulesets.has(id)) {
        Cu.reportError(
          `Disabled static ruleset with duplicated ruleset_id "${id}"`
        );
        continue;
      }

      if (enabled && rulesets.size >= MAX_NUMBER_OF_ENABLED_STATIC_RULESETS) {
        // This is technically reported from the manifest validation, as a warning
        // on extension installed non temporarily, and so checked and logged here
        // in case we are hitting it while loading the enabled rulesets.
        Cu.reportError(
          `Ignoring enabled static ruleset exceeding the MAX_NUMBER_OF_ENABLED_STATIC_RULESETS limit (${MAX_NUMBER_OF_ENABLED_STATIC_RULESETS}): ruleset_id "${id}" (extension: "${extension.id}")`
        );
        continue;
      }

      const readJSONStartTime = Cu.now();
      const rawRules =
        enabled &&
        (await fetch(path)
          .then(res => res.json())
          .catch(err => {
            Cu.reportError(err);
            enabled = false;
            extension.packagingError(
              `Reading declarative_net_request static rules file ${path}: ${err.message}`
            );
          }));
      ChromeUtils.addProfilerMarker(
        "ExtensionDNRStore",
        { startTime: readJSONStartTime },
        `StaticRulesetsReadJSON, addonId: ${extension.id}`
      );

      // Skip rulesets that are not enabled or can't be enabled (e.g. if we got error on loading or
      // parsing the rules JSON file).
      if (!enabled) {
        continue;
      }

      if (!Array.isArray(rawRules)) {
        extension.packagingError(
          `Reading declarative_net_request static rules file ${path}: rules file must contain an Array of rules`
        );
        continue;
      }

      // TODO(Bug 1803369): consider to only report the errors and warnings about invalid static rules for
      // temporarily installed extensions (chrome only shows them for unpacked extensions).
      const logRuleValidationError = err => extension.packagingWarning(err);

      const validatedRules = this.#getValidatedRules(extension, id, rawRules, {
        logRuleValidationError,
      });

      // NOTE: this is currently only accounting for valid rules because
      // only the valid rules will be actually be loaded. Reconsider if
      // we should instead also account for the rules that have been
      // ignored as invalid.
      try {
        ruleQuotaCounter.tryAddRules(id, validatedRules);
      } catch (e) {
        // If this is an API call (updateEnabledRulesets), just propagate the
        // error. Otherwise we are intializing the extension and should just
        // ignore the ruleset while reporting the error.
        if (isUpdateEnabledRulesets) {
          throw e;
        }
        // TODO(Bug 1803363): consider collect telemetry.
        Cu.reportError(
          `Ignoring static ruleset "${id}" in extension "${extension.id}" because: ${e.message}`
        );
        continue;
      }

      rulesets.set(id, { idx, rules: validatedRules });
    }

    return rulesets;
  }

  /**
   * Returns an array of validated and normalized Rule instances given an array
   * of raw rules data (e.g. in form of plain objects read from the static rules
   * JSON files or the dynamicRuleset property from the extension DNR store data).
   *
   * @typedef {import("ExtensionDNR.sys.mjs").Rule} Rule
   *
   * @param   {Extension}     extension
   * @param   {string}        rulesetId
   * @param   {Array<object>} rawRules
   * @param   {object}        options
   * @param   {Function}      [options.logRuleValidationError]
   *                          an optional callback to call for logging the
   *                          validation errors, defaults to use Cu.reportError
   *                          (but getManifestStaticRulesets overrides it to use
   *                          extensions.packagingWarning instead).
   *
   * @returns {Array<Rule>}
   */
  #getValidatedRules(
    extension,
    rulesetId,
    rawRules,
    { logRuleValidationError = err => Cu.reportError(err) } = {}
  ) {
    const startTime = Cu.now();
    const validatedRulesTimerId =
      Glean.extensionsApisDnr.validateRulesTime.start();
    try {
      const ruleValidator = new lazy.ExtensionDNR.RuleValidator([]);
      // Normalize rules read from JSON.
      const validationContext = {
        url: extension.baseURI.spec,
        principal: extension.principal,
        logError: logRuleValidationError,
        preprocessors: {},
        manifestVersion: extension.manifestVersion,
        ignoreUnrecognizedProperties: true,
      };

      // TODO(Bug 1803369): consider to also include the rule id if one was available.
      const getInvalidRuleMessage = (ruleIndex, msg) =>
        `Invalid rule at index ${ruleIndex} from ruleset "${rulesetId}", ${msg}`;

      for (const [rawIndex, rawRule] of rawRules.entries()) {
        try {
          const normalizedRule = lazy.Schemas.normalize(
            rawRule,
            "declarativeNetRequest.Rule",
            validationContext
          );
          if (normalizedRule.value) {
            ruleValidator.addRules([normalizedRule.value]);
          } else {
            logRuleValidationError(
              getInvalidRuleMessage(
                rawIndex,
                normalizedRule.error ?? "Unexpected undefined rule"
              )
            );
          }
        } catch (err) {
          Cu.reportError(err);
          logRuleValidationError(
            getInvalidRuleMessage(rawIndex, "An unexpected error occurred")
          );
        }
      }

      // TODO(Bug 1803369): consider including an index in the invalid rules warnings.
      if (ruleValidator.getFailures().length) {
        logRuleValidationError(
          `Invalid rules found in ruleset "${rulesetId}": ${ruleValidator
            .getFailures()
            .map(f => f.message)
            .join(", ")}`
        );
      }

      return ruleValidator.getValidatedRules();
    } finally {
      ChromeUtils.addProfilerMarker(
        "ExtensionDNRStore",
        { startTime },
        `#getValidatedRules, addonId: ${extension.id}`
      );
      Glean.extensionsApisDnr.validateRulesTime.stopAndAccumulate(
        validatedRulesTimerId
      );
    }
  }

  #getExistingStaticRulesetIds(extension) {
    const ruleResources =
      extension.manifest.declarative_net_request?.rule_resources;
    if (!Array.isArray(ruleResources)) {
      return [];
    }

    return ruleResources.map(rs => rs.id);
  }

  #hasInstallOrUpdateStartupReason(extension) {
    switch (extension.startupReason) {
      case "ADDON_INSTALL":
      case "ADDON_UPGRADE":
      case "ADDON_DOWNGRADE":
        return true;
    }

    return false;
  }

  /**
   * Load and add the DNR stored rules to the RuleManager instance for the given
   * extension.
   *
   * @param {Extension} extension
   * @returns {Promise<void>}
   */
  async #initExtension(extension) {
    // - on new installs the stored rules should be recreated from scratch
    //   (and any stale previously stored data to be ignored)
    // - on upgrades/downgrades:
    //   - the dynamic rules are expected to be preserved
    //   - the static rules are expected to be refreshed from the new
    //     manifest data (also the enabled rulesets are expected to be
    //     reset to the state described in the manifest)
    //
    // TODO(Bug 1803369): consider also setting to true if the extension is installed temporarily.
    if (this.#hasInstallOrUpdateStartupReason(extension)) {
      // Reset the stored static rules on addon updates.
      await StartupCache.delete(extension, ["dnr", "hasEnabledStaticRules"]);
    }

    const hasEnabledStaticRules = await StartupCache.get(
      extension,
      ["dnr", "hasEnabledStaticRules"],
      async () => {
        const staticRulesets = await this.getEnabledStaticRulesets(extension);

        // Note: if the outcome changes, call #setStartupFlag to update this!
        return staticRulesets.size;
      }
    );
    const hasDynamicRules = await StartupCache.get(
      extension,
      ["dnr", "hasDynamicRules"],
      async () => {
        const dynamicRuleset = await this.getDynamicRules(extension);

        // Note: if the outcome changes, call #setStartupFlag to update this!
        return dynamicRuleset.length;
      }
    );

    if (hasEnabledStaticRules || hasDynamicRules) {
      const data = await this.#getDataPromise(extension);
      if (!data.isFromStartupCache() && !data.isFromTemporarilyInstalled()) {
        this.scheduleCacheDataSave();
      }
      if (extension.hasShutdown) {
        return;
      }
      this.updateRulesetManager(extension, {
        updateStaticRulesets: hasEnabledStaticRules,
        updateDynamicRuleset: hasDynamicRules,
      });
    } else if (
      !extension.hasShutdown &&
      !this._dataPromises.has(extension.uuid)
    ) {
      // #getDataPromise() initializes _dataPromises and _data (via #readData).
      // This may be called when the StartupCache is not populated, but if they
      // were, then these methods are not called. All other logic expects these
      // to be initialized when #initExtension() returns, see e.g. bug 1921353.
      let storeData = this.#getDefaults(extension);
      this._data.set(extension.uuid, storeData);
      this._dataPromises.set(extension.uuid, Promise.resolve(storeData));
      this.unloadOnShutdown(extension);
    }
  }

  /**
   * Update the flags that record the (non-)existence of static/dynamic rules.
   * These flags are used by #initExtension.
   * "StartupCache" here refers to the general StartupCache, NOT the one from
   * #getCacheFilePath().
   */
  #setStartupFlag(extension, name, value) {
    // The StartupCache.set method is async, but we do not wait because in
    // practice the "async" part of it completes very quickly because the
    // underlying StartupCache data has already been read when an extension is
    // starting.
    // And any writes is scheduled with an AsyncShutdown blocker, which ensures
    // that the writes complete before the browser shuts down.
    StartupCache.general.set(
      [extension.id, extension.version, "dnr", name],
      value
    );
  }

  #promiseStartupCacheLoaded() {
    if (!this._ensureCacheLoaded) {
      if (this._data.size) {
        return Promise.reject(
          new Error(
            "Unexpected non-empty DNRStore data. DNR startupCache data load aborted."
          )
        );
      }

      const startTime = Cu.now();
      const timerId = Glean.extensionsApisDnr.startupCacheReadTime.start();
      this._ensureCacheLoaded = (async () => {
        const cacheFilePath = this.#getCacheFilePath();
        const { buffer, byteLength } = await IOUtils.read(cacheFilePath);
        Glean.extensionsApisDnr.startupCacheReadSize.accumulate(byteLength);
        const decodedData = lazy.aomStartup.decodeBlob(buffer);
        const emptyOrCorruptedCache = !(decodedData?.cacheData instanceof Map);
        if (emptyOrCorruptedCache) {
          Cu.reportError(
            `Unexpected corrupted DNRStore startupCache data. DNR startupCache data load dropped.`
          );
          // Remove the cache file right away on corrupted (unexpected empty)
          // or obsolete cache content.
          await IOUtils.remove(cacheFilePath, { ignoreAbsent: true });
          return;
        }
        if (this._data.size) {
          Cu.reportError(
            `Unexpected non-empty DNRStore data. DNR startupCache data load dropped.`
          );
          return;
        }
        for (const [
          extUUID,
          cacheStoreData,
        ] of decodedData.cacheData.entries()) {
          if (StoreData.isStaleCacheEntry(extUUID, cacheStoreData)) {
            StoreData.clearLastUpdateTagPref(extUUID);
            continue;
          }
          // TODO(Bug 1825510): schedule a task long enough after startup to detect and
          // remove unused entries in the _startupCacheData Map sooner.
          this._startupCacheData.set(extUUID, {
            extUUID: extUUID,
            ...cacheStoreData,
          });
        }
      })()
        .catch(err => {
          // TODO: collect telemetry on unexpected cache load failures.
          if (!DOMException.isInstance(err) || err.name !== "NotFoundError") {
            Cu.reportError(err);
          }
        })
        .finally(() => {
          ChromeUtils.addProfilerMarker(
            "ExtensionDNRStore",
            { startTime },
            "_ensureCacheLoaded"
          );
          Glean.extensionsApisDnr.startupCacheReadTime.stopAndAccumulate(
            timerId
          );
        });
    }

    return this._ensureCacheLoaded;
  }

  /**
   * Read the stored data for the given extension, either from:
   * - store file (if available and not detected as a data schema downgrade)
   * - manifest file and packaged ruleset JSON files (if there was no valid stored data found)
   *
   * This private method is only called from #getDataPromise, which caches the return value
   * in memory.
   *
   * @param {Extension} extension
   *
   * @returns {Promise<StoreData>}
   */
  #readData(extension) {
    // This just forwards to the actual implementation.
    return this._readData(extension);
  }
  async _readData(extension) {
    const startTime = Cu.now();
    try {
      let result;
      // Try to load data from the startupCache.
      if (extension.startupReason === "APP_STARTUP") {
        result = await this.#readStoreDataFromStartupCache(extension);
      }
      // Fallback to load the data stored in the json file.
      result ??= await this.#readStoreData(extension);

      // Reset the stored data if a data schema version downgrade has been
      // detected (this should only be hit on downgrades if the user have
      // also explicitly passed --allow-downgrade CLI option).
      if (result && result.schemaVersion > StoreData.VERSION) {
        Cu.reportError(
          `Unsupport DNR store schema version downgrade: resetting stored data for ${extension.id}`
        );
        result = null;
      }

      // If the number of disabled rules exceeds the limit when loaded from the store
      // (e.g. if the limit has been customized through prefs, and so not expected to
      // be a common case), then we drop the entire list of disabled rules.
      if (result?.disabledStaticRuleIds) {
        for (const [rulesetId, disabledRuleIds] of Object.entries(
          result.disabledStaticRuleIds
        )) {
          if (
            Array.isArray(disabledRuleIds) &&
            disabledRuleIds.length <=
              ExtensionDNRLimits.MAX_NUMBER_OF_DISABLED_STATIC_RULES
          ) {
            continue;
          }

          Cu.reportError(
            `Discard "${extension.id}" static ruleset "${rulesetId}" disabled rules` +
              ` for exceeding the MAX_NUMBER_OF_DISABLED_STATIC_RULES (${ExtensionDNRLimits.MAX_NUMBER_OF_DISABLED_STATIC_RULES})`
          );
          result.disabledStaticRuleIds[rulesetId] = [];
        }
      }

      // Use defaults and extension manifest if no data stored was found
      // (or it got reset due to an unsupported profile downgrade being detected).
      if (!result) {
        // We don't have any data stored, load the static rules from the manifest.
        result = this.#getDefaults(extension);
        // Initialize the staticRules data from the manifest.
        result.updateRulesets({
          staticRulesets: await this.#getManifestStaticRulesets(extension),
        });
      }

      // The extension has already shutting down and we may already got past
      // the unloadData cleanup (given that there is still a promise in
      // the _dataPromises Map).
      if (extension.hasShutdown && !this._dataPromises.has(extension.uuid)) {
        throw new Error(
          `DNR store data loading aborted, the extension is already shutting down: ${extension.id}`
        );
      }

      this._data.set(extension.uuid, result);

      return result;
    } finally {
      ChromeUtils.addProfilerMarker(
        "ExtensionDNRStore",
        { startTime },
        `readData, addonId: ${extension.id}`
      );
    }
  }

  // Convert extension entries in the startCache map back to StoreData instances
  // (because the StoreData instances get converted into plain objects when
  // serialized into the startupCache structured clone blobs).
  async #readStoreDataFromStartupCache(extension) {
    await this.#promiseStartupCacheLoaded();

    if (!this._startupCacheData.has(extension.uuid)) {
      Glean.extensionsApisDnr.startupCacheEntries.miss.add(1);
      return;
    }

    const extCacheData = this._startupCacheData.get(extension.uuid);
    this._startupCacheData.delete(extension.uuid);

    if (extCacheData.extVersion != extension.version) {
      StoreData.clearLastUpdateTagPref(extension.uuid);
      Glean.extensionsApisDnr.startupCacheEntries.miss.add(1);
      return;
    }

    Glean.extensionsApisDnr.startupCacheEntries.hit.add(1);
    for (const ruleset of extCacheData.staticRulesets.values()) {
      ruleset.rules = ruleset.rules.map(rule =>
        lazy.ExtensionDNR.RuleValidator.deserializeRule(rule)
      );
    }
    extCacheData.dynamicRuleset = extCacheData.dynamicRuleset.map(rule =>
      lazy.ExtensionDNR.RuleValidator.deserializeRule(rule)
    );
    return new StoreData(extension, extCacheData);
  }

  /**
   * Reads the store file for the given extensions and all rules
   * for the enabled static ruleset ids listed in the store file.
   *
   * @param {Extension} extension
   *
   * @returns {Promise<StoreData|null>}
   */
  async #readStoreData(extension) {
    // TODO(Bug 1803363): record into Glean telemetry DNR RulesetsStore store load time.
    let file = this.#getStoreFilePath(extension.uuid);
    let data;
    let isCorrupted = false;
    let storeFileFound = false;
    try {
      data = await IOUtils.readJSON(file, { decompress: true });
      storeFileFound = true;
    } catch (e) {
      if (!(DOMException.isInstance(e) && e.name === "NotFoundError")) {
        Cu.reportError(e);
        isCorrupted = true;
        storeFileFound = true;
      }
      // TODO(Bug 1803363) record store read errors in telemetry scalar.
    }

    // Reset data read from disk if its type isn't the expected one.
    isCorrupted ||=
      !data ||
      !Array.isArray(data.staticRulesets) ||
      // DNR data stored in 109 would not have any dynamicRuleset
      // property and so don't consider the data corrupted if
      // there isn't any dynamicRuleset property at all.
      ("dynamicRuleset" in data && !Array.isArray(data.dynamicRuleset));

    if (isCorrupted && storeFileFound) {
      // Wipe the corrupted data and backup the corrupted file.
      data = null;
      try {
        let uniquePath = await IOUtils.createUniqueFile(
          PathUtils.parent(file),
          PathUtils.filename(file) + ".corrupt",
          0o600
        );
        Cu.reportError(
          `Detected corrupted DNR store data for ${extension.id}, renaming store data file to ${uniquePath}`
        );
        await IOUtils.move(file, uniquePath);
      } catch (err) {
        Cu.reportError(err);
      }
    }

    if (!data) {
      return null;
    }

    const resetStaticRulesets =
      // Reset the static rulesets on install or updating the extension.
      //
      // NOTE: this method is called only once and its return value cached in
      // memory for the entire lifetime of the extension and so we don't need
      // to store any flag to avoid resetting the static rulesets more than
      // once for the same Extension instance.
      this.#hasInstallOrUpdateStartupReason(extension) ||
      // Ignore the stored enabled ruleset ids if the current extension version
      // mismatches the version the store data was generated from.
      data.extVersion !== extension.version;

    if (resetStaticRulesets) {
      data.staticRulesets = undefined;
      data.disabledStaticRuleIds = {};
      data.extVersion = extension.version;
    }

    // If the data is being loaded for a new addon install, make sure to clear
    // any potential stale dynamic rules stored on disk.
    //
    // NOTE: this is expected to only be hit if there was a failure to cleanup
    // state data upon uninstall (e.g. in case the machine shutdowns or
    // Firefox crashes before we got to update the data stored on disk).
    if (extension.startupReason === "ADDON_INSTALL") {
      data.dynamicRuleset = [];
    }

    // In the JSON stored data we only store the enabled rulestore_id and
    // the actual rules have to be loaded.
    data.staticRulesets = await this.#getManifestStaticRulesets(
      extension,
      // Only load the rules from rulesets that are enabled in the stored DNR data,
      // if the array (eventually empty) of the enabled static rules isn't in the
      // stored data, then load all the ones enabled in the manifest.
      { enabledRulesetIds: data.staticRulesets }
    );

    if (data.dynamicRuleset?.length) {
      // Make sure all dynamic rules loaded from disk as validated and normalized
      // (in case they may have been tempered, but also for when we are loading
      // data stored by a different Firefox version from the one that stored the
      // data on disk, e.g. in case validation or normalization logic may have been
      // different in the two Firefox version).
      const validatedDynamicRules = this.#getValidatedRules(
        extension,
        "_dynamic" /* rulesetId */,
        data.dynamicRuleset
      );

      let ruleQuotaCounter = new lazy.ExtensionDNR.RuleQuotaCounter(
        "MAX_NUMBER_OF_DYNAMIC_RULES"
      );
      try {
        ruleQuotaCounter.tryAddRules("_dynamic", validatedDynamicRules);
        data.dynamicRuleset = validatedDynamicRules;
      } catch (e) {
        // This should not happen in practice, because updateDynamicRules
        // rejects quota errors. If we get here, the data on disk may have been
        // tampered with, or the limit was lowered in a browser update.
        Cu.reportError(
          `Ignoring dynamic ruleset in extension "${extension.id}" because: ${e.message}`
        );
        data.dynamicRuleset = [];
      }
    }
    // We use StoreData.fromJSON here to prevent properties that are not expected to
    // be stored in the JSON file from overriding other StoreData constructor properties
    // that are not included in the JSON data returned by StoreData toJSON.
    return StoreData.fromJSON(data, extension);
  }

  async scheduleCacheDataSave() {
    this.#ensureCacheDirectory();
    if (!this._saveCacheTask) {
      this._saveCacheTask = new lazy.DeferredTask(
        () => this.#saveCacheDataNow(),
        5000
      );
      IOUtils.profileBeforeChange.addBlocker(
        "Flush WebExtensions DNR RulesetsStore startupCache",
        async () => {
          await this._saveCacheTask.finalize();
          this._saveCacheTask = null;
        }
      );
    }

    return this._saveCacheTask.arm();
  }

  getStartupCacheData() {
    const filteredData = new Map();
    const seenLastUpdateTags = new Set();
    for (const [extUUID, dataEntry] of this._data) {
      // Only store in the startup cache extensions that are permanently
      // installed (the temporarilyInstalled extension are removed
      // automatically either on shutdown or startup, and so the data
      // stored and then loaded back from the startup cache file
      // would never be used).
      if (dataEntry.isFromTemporarilyInstalled()) {
        continue;
      }
      filteredData.set(extUUID, dataEntry);
      seenLastUpdateTags.add(dataEntry.lastUpdateTag);
    }
    return {
      seenLastUpdateTags,
      filteredData,
    };
  }

  detectStartupCacheDataChanged(seenLastUpdateTags) {
    // Detect if there are changes to the stored data applied while we
    // have been writing the cache data on disk, and reschedule a new
    // cache data save if that is the case.
    // TODO(Bug 1825510): detect also obsoleted entries to make sure
    // they are removed from the startup cache data stored on disk
    // sooner.
    for (const dataEntry of this._data.values()) {
      if (dataEntry.isFromTemporarilyInstalled()) {
        continue;
      }
      if (!seenLastUpdateTags.has(dataEntry.lastUpdateTag)) {
        return true;
      }
    }
    return false;
  }

  async #saveCacheDataNow() {
    const startTime = Cu.now();
    const timerId = Glean.extensionsApisDnr.startupCacheWriteTime.start();
    try {
      const cacheFilePath = this.#getCacheFilePath();
      const { filteredData, seenLastUpdateTags } = this.getStartupCacheData();
      const data = new Uint8Array(
        lazy.aomStartup.encodeBlob({
          cacheData: filteredData,
        })
      );
      await this._ensureCacheDirectoryPromise;
      await IOUtils.write(cacheFilePath, data, {
        tmpPath: `${cacheFilePath}.tmp`,
      });
      Glean.extensionsApisDnr.startupCacheWriteSize.accumulate(data.byteLength);

      if (this.detectStartupCacheDataChanged(seenLastUpdateTags)) {
        this.scheduleCacheDataSave();
      }
    } finally {
      ChromeUtils.addProfilerMarker(
        "ExtensionDNRStore",
        { startTime },
        "#saveCacheDataNow"
      );
      Glean.extensionsApisDnr.startupCacheWriteTime.stopAndAccumulate(timerId);
    }
  }

  /**
   * Save the data for the given extension on disk.
   *
   * @param {string} extensionUUID
   * @param {string} extensionId
   * @returns {Promise<void>}
   */
  async #saveNow(extensionUUID, extensionId) {
    const startTime = Cu.now();
    try {
      if (
        !this._dataPromises.has(extensionUUID) ||
        !this._data.has(extensionUUID)
      ) {
        throw new Error(
          `Unexpected uninitialized DNR store on saving data for extension uuid "${extensionUUID}"`
        );
      }
      const storeFile = this.#getStoreFilePath(extensionUUID);
      const data = this._data.get(extensionUUID);
      await this.#ensureStoreDirectory(extensionUUID);
      await IOUtils.writeJSON(storeFile, data, {
        tmpPath: `${storeFile}.tmp`,
        compress: true,
      });

      this.scheduleCacheDataSave();

      // TODO(Bug 1803363): report jsonData lengths into a telemetry scalar.
      // TODO(Bug 1803363): report jsonData time to write into a telemetry scalar.
    } catch (err) {
      Cu.reportError(err);
      throw err;
    } finally {
      this._savePromises.delete(extensionUUID);
      ChromeUtils.addProfilerMarker(
        "ExtensionDNRStore",
        { startTime },
        `#saveNow, addonId: ${extensionId}`
      );
    }
  }

  /**
   * Unload data for the given extension UUID from memory (e.g. when the extension is disabled or uninstalled),
   * waits for a pending save promise to be settled if any.
   *
   * NOTE: this method clear the data cached in memory and close the update queue
   * and so it should only be called from the extension shutdown handler and
   * by the initExtension method before pushing into the update queue for the
   * for the extension the initExtension task.
   *
   * @param {string} extensionUUID
   * @returns {Promise<void>}
   */
  async #unloadData(extensionUUID) {
    // Wait for the update tasks to have been executed, then
    // wait for the data to have been saved and finally unload
    // the data cached in memory.
    const dataUpdateQueue = this._dataUpdateQueues.has(extensionUUID)
      ? this._dataUpdateQueues.get(extensionUUID)
      : undefined;

    if (dataUpdateQueue) {
      try {
        await dataUpdateQueue.close();
      } catch (err) {
        // Unexpected error on closing the update queue.
        Cu.reportError(err);
      }
      this._dataUpdateQueues.delete(extensionUUID);
    }

    const savePromise = this._savePromises.get(extensionUUID);
    if (savePromise) {
      await savePromise;
      this._savePromises.delete(extensionUUID);
    }

    this._dataPromises.delete(extensionUUID);
    this._data.delete(extensionUUID);
  }

  /**
   * Internal implementation for updating the dynamic ruleset and enforcing
   * dynamic rules count limits.
   *
   * Callers ensure that there is never a concurrent call of #updateDynamicRules
   * for a given extension, so we can safely modify ruleManager.dynamicRules
   * from inside this method, even asynchronously.
   *
   * @param {Extension}     extension
   * @param {object}        params
   * @param {Array<number>} [params.removeRuleIds=[]]
   * @param {Array<Rule>}   [params.addRules=[]]
   */
  async #updateDynamicRules(extension, { removeRuleIds, addRules }) {
    const ruleManager = lazy.ExtensionDNR.getRuleManager(extension);
    const ruleValidator = new lazy.ExtensionDNR.RuleValidator(
      ruleManager.getDynamicRules()
    );
    if (removeRuleIds) {
      ruleValidator.removeRuleIds(removeRuleIds);
    }
    if (addRules) {
      ruleValidator.addRules(addRules);
    }
    let failures = ruleValidator.getFailures();
    if (failures.length) {
      throw new ExtensionError(failures[0].message);
    }

    const validatedRules = ruleValidator.getValidatedRules();
    let ruleQuotaCounter = new lazy.ExtensionDNR.RuleQuotaCounter(
      "MAX_NUMBER_OF_DYNAMIC_RULES"
    );
    ruleQuotaCounter.tryAddRules("_dynamic", validatedRules);

    this._data.get(extension.uuid).updateRulesets({
      dynamicRuleset: validatedRules,
    });
    this.#setStartupFlag(extension, "hasDynamicRules", validatedRules.length);
    await this.save(extension);
    // updateRulesetManager calls ruleManager.setDynamicRules using the
    // validated rules assigned above to this._data.
    this.updateRulesetManager(extension, {
      updateDynamicRuleset: true,
      updateStaticRulesets: false,
    });
  }

  async #updateStaticRules(
    extension,
    { rulesetId, disableRuleIds, enableRuleIds }
  ) {
    const existingRulesetIds = this.#getExistingStaticRulesetIds(extension);
    if (!existingRulesetIds.includes(rulesetId)) {
      throw new ExtensionError(`Invalid ruleset id: "${rulesetId}"`);
    }

    const data = this._data.get(extension.uuid);
    const disabledRuleIdsSet = new Set(data.disabledStaticRuleIds[rulesetId]);
    const enableSet = new Set(enableRuleIds);
    const disableSet = new Set(disableRuleIds);

    let changed = false;
    for (const ruleId of disableSet) {
      // Skip rule ids that are disabled and enabled in the same call.
      if (enableSet.delete(ruleId)) {
        continue;
      }
      if (!disabledRuleIdsSet.has(ruleId)) {
        changed = true;
      }
      disabledRuleIdsSet.add(ruleId);
    }
    for (const ruleId of enableSet) {
      if (disabledRuleIdsSet.delete(ruleId)) {
        changed = true;
      }
    }

    if (!changed) {
      return;
    }

    if (
      disabledRuleIdsSet.size >
      ExtensionDNRLimits.MAX_NUMBER_OF_DISABLED_STATIC_RULES
    ) {
      throw new ExtensionError(
        `Number of individually disabled static rules exceeds MAX_NUMBER_OF_DISABLED_STATIC_RULES limit`
      );
    }

    // Chrome doesn't seem to validate if the rule id actually exists in the ruleset,
    // and so set the resulting updated array of disabled rule ids right away.
    //
    // For more details, see the "Invalid rules" and "Error handling in updateStaticRules"
    // section of https://github.com/w3c/webextensions/issues/162#issuecomment-2101003746
    data.disabledStaticRuleIds[rulesetId] = Array.from(disabledRuleIdsSet);

    await this.save(extension);

    // If the ruleset isn't currently enabled, after saving the updated
    // disabledRuleIdsSet we are done.
    if (!data.staticRulesets.has(rulesetId)) {
      return;
    }
    //
    // updateRulesetManager calls ruleManager.setStaticRules to
    // update the list of disabled ruleIds.
    this.updateRulesetManager(extension, {
      updateDynamicRuleset: false,
      updateStaticRulesets: true,
    });
  }

  /**
   * Internal implementation for updating the enabled rulesets and enforcing
   * static rulesets and rules count limits.
   *
   * @param {Extension}     extension
   * @param {object}        params
   * @param {Array<string>} [params.disableRulesetIds=[]]
   * @param {Array<string>} [params.enableRulesetIds=[]]
   */
  async #updateEnabledStaticRulesets(
    extension,
    { disableRulesetIds, enableRulesetIds }
  ) {
    const existingIds = new Set(this.#getExistingStaticRulesetIds(extension));
    if (!existingIds.size) {
      return;
    }

    const enabledRulesets = await this.getEnabledStaticRulesets(extension);
    const updatedEnabledRulesets = new Map();
    let disableIds = new Set(disableRulesetIds);
    let enableIds = new Set(enableRulesetIds);

    // valiate the ruleset ids for existence (which will also reject calls
    // including the reserved _session and _dynamic, because static rulesets
    // id are validated as part of the manifest validation and they are not
    // allowed to start with '_').
    const errorOnInvalidRulesetIds = rsIdSet => {
      for (const rsId of rsIdSet) {
        if (!existingIds.has(rsId)) {
          throw new ExtensionError(`Invalid ruleset id: "${rsId}"`);
        }
      }
    };
    errorOnInvalidRulesetIds(disableIds);
    errorOnInvalidRulesetIds(enableIds);

    // Copy into the updatedEnabledRulesets Map any ruleset that is not
    // requested to be disabled or is enabled back in the same request.
    for (const [rulesetId, ruleset] of enabledRulesets) {
      if (!disableIds.has(rulesetId) || enableIds.has(rulesetId)) {
        updatedEnabledRulesets.set(rulesetId, ruleset);
        enableIds.delete(rulesetId);
      }
    }

    const { MAX_NUMBER_OF_ENABLED_STATIC_RULESETS } = lazy.ExtensionDNRLimits;

    const maxNewRulesetsCount =
      MAX_NUMBER_OF_ENABLED_STATIC_RULESETS - updatedEnabledRulesets.size;

    if (enableIds.size > maxNewRulesetsCount) {
      // Log an error for the developer.
      throw new ExtensionError(
        `updatedEnabledRulesets request is exceeding MAX_NUMBER_OF_ENABLED_STATIC_RULESETS`
      );
    }

    // At this point, every item in |updatedEnabledRulesets| is an enabled
    // ruleset with already-valid rules. In order to not exceed the rule quota
    // when previously-disabled rulesets are enabled, we need to count what we
    // already have.
    let ruleQuotaCounter = new lazy.ExtensionDNR.RuleQuotaCounter(
      "GUARANTEED_MINIMUM_STATIC_RULES"
    );
    for (let [rulesetId, ruleset] of updatedEnabledRulesets) {
      ruleQuotaCounter.tryAddRules(rulesetId, ruleset.rules);
    }

    const newRulesets = await this.#getManifestStaticRulesets(extension, {
      enabledRulesetIds: Array.from(enableIds),
      ruleQuotaCounter,
      isUpdateEnabledRulesets: true,
    });

    for (const [rulesetId, ruleset] of newRulesets.entries()) {
      updatedEnabledRulesets.set(rulesetId, ruleset);
    }

    this._data.get(extension.uuid).updateRulesets({
      staticRulesets: updatedEnabledRulesets,
    });
    this.#setStartupFlag(
      extension,
      "hasEnabledStaticRules",
      updatedEnabledRulesets.size
    );
    await this.save(extension);
    this.updateRulesetManager(extension, {
      updateDynamicRuleset: false,
      updateStaticRulesets: true,
    });
  }
}

let store = new RulesetsStore();

export const ExtensionDNRStore = {
  SCHEMA_VERSION: StoreData.VERSION,
  async clearOnUninstall(extensionUUID) {
    return store.clearOnUninstall(extensionUUID);
  },
  async initExtension(extension) {
    await store.initExtension(extension);
  },
  async updateDynamicRules(extension, updateRuleOptions) {
    await store.updateDynamicRules(extension, updateRuleOptions);
  },
  async updateEnabledStaticRulesets(extension, updateRulesetOptions) {
    await store.updateEnabledStaticRulesets(extension, updateRulesetOptions);
  },
  async updateStaticRules(extension, updateStaticRulesOptions) {
    await store.updateStaticRules(extension, updateStaticRulesOptions);
  },
  getDisabledRuleIds(extension, rulesetId) {
    return store.getDisabledRuleIds(extension, rulesetId);
  },
  // Test-only helpers
  _getLastUpdateTag(extensionUUID) {
    requireTestOnlyCallers();
    return StoreData.getLastUpdateTag(extensionUUID);
  },
  _getStoreForTesting() {
    requireTestOnlyCallers();
    return store;
  },
  _getStoreDataClassForTesting() {
    requireTestOnlyCallers();
    return StoreData;
  },
  _recreateStoreForTesting() {
    requireTestOnlyCallers();
    store = new RulesetsStore();
    return store;
  },
  _storeLastUpdateTag(extensionUUID, lastUpdateTag) {
    requireTestOnlyCallers();
    return StoreData.storeLastUpdateTag(extensionUUID, lastUpdateTag);
  },
};
