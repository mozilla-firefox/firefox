/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { JSONFile } from "resource://gre/modules/JSONFile.sys.mjs";
import { Log } from "resource://gre/modules/Log.sys.mjs";

import { Async } from "resource://services-common/async.sys.mjs";
import { Observers } from "resource://services-common/observers.sys.mjs";

import {
  DEFAULT_DOWNLOAD_BATCH_SIZE,
  DEFAULT_GUID_FETCH_BATCH_SIZE,
  ENGINE_BATCH_INTERRUPTED,
  ENGINE_DOWNLOAD_FAIL,
  ENGINE_UPLOAD_FAIL,
  VERSION_OUT_OF_DATE,
  PREFS_BRANCH,
} from "resource://services-sync/constants.sys.mjs";

import {
  Collection,
  CryptoWrapper,
  WBORecord,
} from "resource://services-sync/record.sys.mjs";
import { Resource } from "resource://services-sync/resource.sys.mjs";
import {
  SerializableSet,
  Svc,
  Utils,
} from "resource://services-sync/util.sys.mjs";
import { SyncedRecordsTelemetry } from "resource://services-sync/telemetry.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
});

function ensureDirectory(path) {
  return IOUtils.makeDirectory(PathUtils.parent(path), {
    createAncestors: true,
  });
}

/**
 * Trackers are associated with a single engine and deal with
 * listening for changes to their particular data type.
 *
 * The base `Tracker` only supports listening for changes, and bumping the score
 * to indicate how urgently the engine wants to sync. It does not persist any
 * data. Engines that track changes directly in the storage layer (like
 * bookmarks, bridged engines, addresses, and credit cards) or only upload a
 * single record (tabs and preferences) should subclass `Tracker`.
 */
export function Tracker(name, engine) {
  if (!engine) {
    throw new Error("Tracker must be associated with an Engine instance.");
  }

  name = name || "Unnamed";
  this.name = name.toLowerCase();
  this.engine = engine;

  this._log = Log.repository.getLogger(`Sync.Engine.${name}.Tracker`);

  this._score = 0;

  this.asyncObserver = Async.asyncObserver(this, this._log);
}

Tracker.prototype = {
  // New-style trackers use change sources to filter out changes made by Sync in
  // observer notifications, so we don't want to let the engine ignore all
  // changes during a sync.
  get ignoreAll() {
    return false;
  },

  // Define an empty setter so that the engine doesn't throw a `TypeError`
  // setting a read-only property.
  set ignoreAll(value) {},

  /*
   * Score can be called as often as desired to decide which engines to sync
   *
   * Valid values for score:
   * -1: Do not sync unless the user specifically requests it (almost disabled)
   * 0: Nothing has changed
   * 100: Please sync me ASAP!
   *
   * Setting it to other values should (but doesn't currently) throw an exception
   */
  get score() {
    return this._score;
  },

  set score(value) {
    this._score = value;
    Observers.notify("weave:engine:score:updated", this.name);
  },

  // Should be called by service everytime a sync has been done for an engine
  resetScore() {
    this._score = 0;
  },

  // Unsupported, and throws a more descriptive error to ensure callers aren't
  // accidentally using persistence.
  async getChangedIDs() {
    throw new TypeError("This tracker doesn't store changed IDs");
  },

  // Also unsupported.
  async addChangedID() {
    throw new TypeError("Can't add changed ID to this tracker");
  },

  // Ditto.
  async removeChangedID() {
    throw new TypeError("Can't remove changed IDs from this tracker");
  },

  // This method is called at various times, so we override with a no-op
  // instead of throwing.
  clearChangedIDs() {},

  _now() {
    return Date.now() / 1000;
  },

  _isTracking: false,

  start() {
    if (!this.engineIsEnabled()) {
      return;
    }
    this._log.trace("start().");
    if (!this._isTracking) {
      this.onStart();
      this._isTracking = true;
    }
  },

  async stop() {
    this._log.trace("stop().");
    if (this._isTracking) {
      await this.asyncObserver.promiseObserversComplete();
      this.onStop();
      this._isTracking = false;
    }
  },

  // Override these in your subclasses.
  onStart() {},
  onStop() {},
  async observe() {},

  engineIsEnabled() {
    if (!this.engine) {
      // Can't tell -- we must be running in a test!
      return true;
    }
    return this.engine.enabled;
  },

  /**
   * Starts or stops listening for changes depending on the associated engine's
   * enabled state.
   *
   * @param {boolean} engineEnabled Whether the engine was enabled.
   */
  async onEngineEnabledChanged(engineEnabled) {
    if (engineEnabled == this._isTracking) {
      return;
    }

    if (engineEnabled) {
      this.start();
    } else {
      await this.stop();
      this.clearChangedIDs();
    }
  },

  async finalize() {
    await this.stop();
  },
};

/*
 * A tracker that persists a list of IDs for all changed items that need to be
 * synced. This is 🚨 _extremely deprecated_ 🚨 and only kept around for current
 * engines. ⚠️ Please **don't use it** for new engines! ⚠️
 *
 * Why is this kind of external change tracking deprecated? Because it causes
 * consistency issues due to missed notifications, interrupted syncs, and the
 * tracker's view of what changed diverging from the data store's.
 */
export function LegacyTracker(name, engine) {
  Tracker.call(this, name, engine);

  this._ignored = [];
  this.file = this.name;
  this._storage = new JSONFile({
    path: Utils.jsonFilePath("changes", this.file),
    dataPostProcessor: json => this._dataPostProcessor(json),
    beforeSave: () => this._beforeSave(),
  });
  this._ignoreAll = false;
}

LegacyTracker.prototype = {
  get ignoreAll() {
    return this._ignoreAll;
  },

  set ignoreAll(value) {
    this._ignoreAll = value;
  },

  // Default to an empty object if the file doesn't exist.
  _dataPostProcessor(json) {
    return (typeof json == "object" && json) || {};
  },

  // Ensure the Weave storage directory exists before writing the file.
  _beforeSave() {
    return ensureDirectory(this._storage.path);
  },

  async getChangedIDs() {
    await this._storage.load();
    return this._storage.data;
  },

  _saveChangedIDs() {
    this._storage.saveSoon();
  },

  // ignore/unignore specific IDs.  Useful for ignoring items that are
  // being processed, or that shouldn't be synced.
  // But note: not persisted to disk

  ignoreID(id) {
    this.unignoreID(id);
    this._ignored.push(id);
  },

  unignoreID(id) {
    let index = this._ignored.indexOf(id);
    if (index != -1) {
      this._ignored.splice(index, 1);
    }
  },

  async _saveChangedID(id, when) {
    this._log.trace(`Adding changed ID: ${id}, ${JSON.stringify(when)}`);
    const changedIDs = await this.getChangedIDs();
    changedIDs[id] = when;
    this._saveChangedIDs();
  },

  async addChangedID(id, when) {
    if (!id) {
      this._log.warn("Attempted to add undefined ID to tracker");
      return false;
    }

    if (this.ignoreAll || this._ignored.includes(id)) {
      return false;
    }

    // Default to the current time in seconds if no time is provided.
    if (when == null) {
      when = this._now();
    }

    const changedIDs = await this.getChangedIDs();
    // Add/update the entry if we have a newer time.
    if ((changedIDs[id] || -Infinity) < when) {
      await this._saveChangedID(id, when);
    }

    return true;
  },

  async removeChangedID(...ids) {
    if (!ids.length || this.ignoreAll) {
      return false;
    }
    for (let id of ids) {
      if (!id) {
        this._log.warn("Attempted to remove undefined ID from tracker");
        continue;
      }
      if (this._ignored.includes(id)) {
        this._log.debug(`Not removing ignored ID ${id} from tracker`);
        continue;
      }
      const changedIDs = await this.getChangedIDs();
      if (changedIDs[id] != null) {
        this._log.trace("Removing changed ID " + id);
        delete changedIDs[id];
      }
    }
    this._saveChangedIDs();
    return true;
  },

  clearChangedIDs() {
    this._log.trace("Clearing changed ID list");
    this._storage.data = {};
    this._saveChangedIDs();
  },

  async finalize() {
    // Persist all pending tracked changes to disk, and wait for the final write
    // to finish.
    await super.finalize();
    this._saveChangedIDs();
    await this._storage.finalize();
  },
};
Object.setPrototypeOf(LegacyTracker.prototype, Tracker.prototype);

/**
 * The Store serves as the interface between Sync and stored data.
 *
 * The name "store" is slightly a misnomer because it doesn't actually "store"
 * anything. Instead, it serves as a gateway to something that actually does
 * the "storing."
 *
 * The store is responsible for record management inside an engine. It tells
 * Sync what items are available for Sync, converts items to and from Sync's
 * record format, and applies records from Sync into changes on the underlying
 * store.
 *
 * Store implementations require a number of functions to be implemented. These
 * are all documented below.
 *
 * For stores that deal with many records or which have expensive store access
 * routines, it is highly recommended to implement a custom applyIncomingBatch
 * and/or applyIncoming function on top of the basic APIs.
 */

export function Store(name, engine) {
  if (!engine) {
    throw new Error("Store must be associated with an Engine instance.");
  }

  name = name || "Unnamed";
  this.name = name.toLowerCase();
  this.engine = engine;

  this._log = Log.repository.getLogger(`Sync.Engine.${name}.Store`);

  ChromeUtils.defineLazyGetter(this, "_timer", function () {
    return Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  });
}

Store.prototype = {
  /**
   * Apply multiple incoming records against the store.
   *
   * This is called with a set of incoming records to process. The function
   * should look at each record, reconcile with the current local state, and
   * make the local changes required to bring its state in alignment with the
   * record.
   *
   * The default implementation simply iterates over all records and calls
   * applyIncoming(). Store implementations may overwrite this function
   * if desired.
   *
   * @param  records Array of records to apply
   * @param  a SyncedRecordsTelemetry obj that will keep track of failed reasons
   * @return Array of record IDs which did not apply cleanly
   */
  async applyIncomingBatch(records, countTelemetry) {
    let failed = [];

    await Async.yieldingForEach(records, async record => {
      try {
        await this.applyIncoming(record);
      } catch (ex) {
        if (ex.code == SyncEngine.prototype.eEngineAbortApplyIncoming) {
          // This kind of exception should have a 'cause' attribute, which is an
          // originating exception.
          // ex.cause will carry its stack with it when rethrown.
          throw ex.cause;
        }
        if (Async.isShutdownException(ex)) {
          throw ex;
        }
        this._log.warn("Failed to apply incoming record " + record.id, ex);
        failed.push(record.id);
        countTelemetry.addIncomingFailedReason(ex.message);
      }
    });

    return failed;
  },

  /**
   * Apply a single record against the store.
   *
   * This takes a single record and makes the local changes required so the
   * local state matches what's in the record.
   *
   * The default implementation calls one of remove(), create(), or update()
   * depending on the state obtained from the store itself. Store
   * implementations may overwrite this function if desired.
   *
   * @param record
   *        Record to apply
   */
  async applyIncoming(record) {
    if (record.deleted) {
      await this.remove(record);
    } else if (!(await this.itemExists(record.id))) {
      await this.create(record);
    } else {
      await this.update(record);
    }
  },

  // override these in derived objects

  /**
   * Create an item in the store from a record.
   *
   * This is called by the default implementation of applyIncoming(). If using
   * applyIncomingBatch(), this won't be called unless your store calls it.
   *
   * @param record
   *        The store record to create an item from
   */
  async create() {
    throw new Error("override create in a subclass");
  },

  /**
   * Remove an item in the store from a record.
   *
   * This is called by the default implementation of applyIncoming(). If using
   * applyIncomingBatch(), this won't be called unless your store calls it.
   *
   * @param record
   *        The store record to delete an item from
   */
  async remove() {
    throw new Error("override remove in a subclass");
  },

  /**
   * Update an item from a record.
   *
   * This is called by the default implementation of applyIncoming(). If using
   * applyIncomingBatch(), this won't be called unless your store calls it.
   *
   * @param record
   *        The record to use to update an item from
   */
  async update() {
    throw new Error("override update in a subclass");
  },

  /**
   * Determine whether a record with the specified ID exists.
   *
   * Takes a string record ID and returns a booleans saying whether the record
   * exists.
   *
   * @param  id
   *         string record ID
   * @return boolean indicating whether record exists locally
   */
  async itemExists() {
    throw new Error("override itemExists in a subclass");
  },

  /**
   * Create a record from the specified ID.
   *
   * If the ID is known, the record should be populated with metadata from
   * the store. If the ID is not known, the record should be created with the
   * delete field set to true.
   *
   * @param  id
   *         string record ID
   * @param  collection
   *         Collection to add record to. This is typically passed into the
   *         constructor for the newly-created record.
   * @return record type for this engine
   */
  async createRecord() {
    throw new Error("override createRecord in a subclass");
  },

  /**
   * Change the ID of a record.
   *
   * @param  oldID
   *         string old/current record ID
   * @param  newID
   *         string new record ID
   */
  async changeItemID() {
    throw new Error("override changeItemID in a subclass");
  },

  /**
   * Obtain the set of all known record IDs.
   *
   * @return Object with ID strings as keys and values of true. The values
   *         are ignored.
   */
  async getAllIDs() {
    throw new Error("override getAllIDs in a subclass");
  },

  /**
   * Wipe all data in the store.
   *
   * This function is called during remote wipes or when replacing local data
   * with remote data.
   *
   * This function should delete all local data that the store is managing. It
   * can be thought of as clearing out all state and restoring the "new
   * browser" state.
   */
  async wipe() {
    throw new Error("override wipe in a subclass");
  },
};

export function EngineManager(service) {
  this.service = service;

  this._engines = {};

  this._altEngineInfo = {};

  // This will be populated by Service on startup.
  this._declined = new Set();
  this._log = Log.repository.getLogger("Sync.EngineManager");
  this._log.manageLevelFromPref("services.sync.log.logger.service.engines");
  // define the default level for all engine logs here (although each engine
  // allows its level to be controlled via a specific, non-default pref)
  Log.repository
    .getLogger(`Sync.Engine`)
    .manageLevelFromPref("services.sync.log.logger.engine");
}

EngineManager.prototype = {
  get(name) {
    // Return an array of engines if we have an array of names
    if (Array.isArray(name)) {
      let engines = [];
      name.forEach(function (name) {
        let engine = this.get(name);
        if (engine) {
          engines.push(engine);
        }
      }, this);
      return engines;
    }

    return this._engines[name]; // Silently returns undefined for unknown names.
  },

  getAll() {
    let engines = [];
    for (let [, engine] of Object.entries(this._engines)) {
      engines.push(engine);
    }
    return engines;
  },

  /**
   * If a user has changed a pref that controls which variant of a sync engine
   * for a given collection we use, unregister the old engine and register the
   * new one.
   *
   * This is called by EngineSynchronizer before every sync.
   */
  async switchAlternatives() {
    for (let [name, info] of Object.entries(this._altEngineInfo)) {
      let prefValue = info.prefValue;
      if (prefValue === info.lastValue) {
        this._log.trace(
          `No change for engine ${name} (${info.pref} is still ${prefValue})`
        );
        continue;
      }
      // Unregister the old engine, register the new one.
      this._log.info(
        `Switching ${name} engine ("${info.pref}" went from ${info.lastValue} => ${prefValue})`
      );
      try {
        await this._removeAndFinalize(name);
      } catch (e) {
        this._log.warn(`Failed to remove previous ${name} engine...`, e);
      }
      let engineType = prefValue ? info.whenTrue : info.whenFalse;
      try {
        // If register throws, we'll try again next sync, but until then there
        // won't be an engine registered for this collection.
        await this.register(engineType);
        info.lastValue = prefValue;
        // Note: engineType.name is using Function.prototype.name.
        this._log.info(`Switched the ${name} engine to use ${engineType.name}`);
      } catch (e) {
        this._log.warn(
          `Switching the ${name} engine to use ${engineType.name} failed (couldn't register)`,
          e
        );
      }
    }
  },

  async registerAlternatives(name, pref, whenTrue, whenFalse) {
    let info = { name, pref, whenTrue, whenFalse };

    XPCOMUtils.defineLazyPreferenceGetter(info, "prefValue", pref, false);

    let chosen = info.prefValue ? info.whenTrue : info.whenFalse;
    info.lastValue = info.prefValue;
    this._altEngineInfo[name] = info;

    await this.register(chosen);
  },

  /**
   * N.B., does not pay attention to the declined list.
   */
  getEnabled() {
    return this.getAll()
      .filter(engine => engine.enabled)
      .sort((a, b) => a.syncPriority - b.syncPriority);
  },

  get enabledEngineNames() {
    return this.getEnabled().map(e => e.name);
  },

  persistDeclined() {
    Svc.PrefBranch.setStringPref(
      "declinedEngines",
      [...this._declined].join(",")
    );
  },

  /**
   * Returns an array.
   */
  getDeclined() {
    return [...this._declined];
  },

  setDeclined(engines) {
    this._declined = new Set(engines);
    this.persistDeclined();
  },

  isDeclined(engineName) {
    return this._declined.has(engineName);
  },

  /**
   * Accepts a Set or an array.
   */
  decline(engines) {
    for (let e of engines) {
      this._declined.add(e);
    }
    this.persistDeclined();
  },

  undecline(engines) {
    for (let e of engines) {
      this._declined.delete(e);
    }
    this.persistDeclined();
  },

  /**
   * Register an Engine to the service. Alternatively, give an array of engine
   * objects to register.
   *
   * @param engineObject
   *        Engine object used to get an instance of the engine
   * @return The engine object if anything failed
   */
  async register(engineObject) {
    if (Array.isArray(engineObject)) {
      for (const e of engineObject) {
        await this.register(e);
      }
      return;
    }

    try {
      let engine = new engineObject(this.service);
      let name = engine.name;
      if (name in this._engines) {
        this._log.error("Engine '" + name + "' is already registered!");
      } else {
        if (engine.initialize) {
          await engine.initialize();
        }
        this._engines[name] = engine;
      }
    } catch (ex) {
      let name = engineObject || "";
      name = name.prototype || "";
      name = name.name || "";

      this._log.error(`Could not initialize engine ${name}`, ex);
    }
  },

  async unregister(val) {
    let name = val;
    if (val instanceof SyncEngine) {
      name = val.name;
    }
    await this._removeAndFinalize(name);
    delete this._altEngineInfo[name];
  },

  // Common code for disabling an engine by name, that doesn't complain if the
  // engine doesn't exist. Doesn't touch the engine's alternative info (if any
  // exists).
  async _removeAndFinalize(name) {
    if (name in this._engines) {
      let engine = this._engines[name];
      delete this._engines[name];
      await engine.finalize();
    }
  },

  async clear() {
    for (let name in this._engines) {
      let engine = this._engines[name];
      delete this._engines[name];
      await engine.finalize();
    }
    this._altEngineInfo = {};
  },
};

export function SyncEngine(name, service) {
  if (!service) {
    throw new Error("SyncEngine must be associated with a Service instance.");
  }

  this.Name = name || "Unnamed";
  this.name = name.toLowerCase();
  this.service = service;

  this._notify = Utils.notify("weave:engine:");
  this._log = Log.repository.getLogger("Sync.Engine." + this.Name);
  this._log.manageLevelFromPref(`services.sync.log.logger.engine.${this.name}`);

  this._modified = this.emptyChangeset();
  this._tracker; // initialize tracker to load previously changed IDs
  this._log.debug("Engine constructed");

  this._toFetchStorage = new JSONFile({
    path: Utils.jsonFilePath("toFetch", this.name),
    dataPostProcessor: json => this._metadataPostProcessor(json),
    beforeSave: () => this._beforeSaveMetadata(),
  });

  this._previousFailedInStorage = new JSONFile({
    path: Utils.jsonFilePath("failed", this.name),
    dataPostProcessor: json => this._metadataPostProcessor(json),
    beforeSave: () => this._beforeSaveMetadata(),
  });

  this._previousFailedOutStorage = new JSONFile({
    path: Utils.jsonFilePath("failedOut", this.name),
    dataPostProcessor: json => this._metadataPostProcessor(json),
    beforeSave: () => this._beforeSaveMetadata(),
  });

  XPCOMUtils.defineLazyPreferenceGetter(
    this,
    "_enabled",
    `services.sync.engine.${this.prefName}`,
    false
  );
  XPCOMUtils.defineLazyPreferenceGetter(
    this,
    "_syncID",
    `services.sync.${this.name}.syncID`,
    ""
  );
  XPCOMUtils.defineLazyPreferenceGetter(
    this,
    "_lastSync",
    `services.sync.${this.name}.lastSync`,
    "0",
    null,
    v => parseFloat(v)
  );
  // Async initializations can be made in the initialize() method.

  this.asyncObserver = Async.asyncObserver(this, this._log);
}

// Enumeration to define approaches to handling bad records.
// Attached to the constructor to allow use as a kind of static enumeration.
SyncEngine.kRecoveryStrategy = {
  ignore: "ignore",
  retry: "retry",
  error: "error",
};

SyncEngine.prototype = {
  _recordObj: CryptoWrapper,
  // _storeObj, and _trackerObj should to be overridden in subclasses
  _storeObj: Store,
  _trackerObj: Tracker,
  version: 1,

  // Local 'constant'.
  // Signal to the engine that processing further records is pointless.
  eEngineAbortApplyIncoming: "error.engine.abort.applyincoming",

  // Should we keep syncing if we find a record that cannot be uploaded (ever)?
  // If this is false, we'll throw, otherwise, we'll ignore the record and
  // continue. This currently can only happen due to the record being larger
  // than the record upload limit.
  allowSkippedRecord: true,

  // Which sortindex to use when retrieving records for this engine.
  _defaultSort: undefined,

  _hasSyncedThisSession: false,

  _metadataPostProcessor(json) {
    if (Array.isArray(json)) {
      // Pre-`JSONFile` storage stored an array, but `JSONFile` defaults to
      // an object, so we wrap the array for consistency.
      json = { ids: json };
    }
    if (!json.ids) {
      json.ids = [];
    }
    // The set serializes the same way as an array, but offers more efficient
    // methods of manipulation.
    json.ids = new SerializableSet(json.ids);
    return json;
  },

  async _beforeSaveMetadata() {
    await ensureDirectory(this._toFetchStorage.path);
    await ensureDirectory(this._previousFailedInStorage.path);
    await ensureDirectory(this._previousFailedOutStorage.path);
  },

  // A relative priority to use when computing an order
  // for engines to be synced. Higher-priority engines
  // (lower numbers) are synced first.
  // It is recommended that a unique value be used for each engine,
  // in order to guarantee a stable sequence.
  syncPriority: 0,

  // How many records to pull in a single sync. This is primarily to avoid very
  // long first syncs against profiles with many history records.
  downloadLimit: null,

  // How many records to pull at one time when specifying IDs. This is to avoid
  // URI length limitations.
  guidFetchBatchSize: DEFAULT_GUID_FETCH_BATCH_SIZE,

  downloadBatchSize: DEFAULT_DOWNLOAD_BATCH_SIZE,

  async initialize() {
    await this._toFetchStorage.load();
    await this._previousFailedInStorage.load();
    await this._previousFailedOutStorage.load();
    Services.prefs.addObserver(
      `${PREFS_BRANCH}engine.${this.prefName}`,
      this.asyncObserver,
      true
    );
    this._log.debug("SyncEngine initialized", this.name);
  },

  get prefName() {
    return this.name;
  },

  get enabled() {
    return this._enabled;
  },

  set enabled(val) {
    if (!!val != this._enabled) {
      Svc.PrefBranch.setBoolPref("engine." + this.prefName, !!val);
    }
  },

  get score() {
    return this._tracker.score;
  },

  get _store() {
    let store = new this._storeObj(this.Name, this);
    this.__defineGetter__("_store", () => store);
    return store;
  },

  get _tracker() {
    let tracker = new this._trackerObj(this.Name, this);
    this.__defineGetter__("_tracker", () => tracker);
    return tracker;
  },

  get storageURL() {
    return this.service.storageURL;
  },

  get engineURL() {
    return this.storageURL + this.name;
  },

  get cryptoKeysURL() {
    return this.storageURL + "crypto/keys";
  },

  get metaURL() {
    return this.storageURL + "meta/global";
  },

  startTracking() {
    this._tracker.start();
  },

  // Returns a promise
  stopTracking() {
    return this._tracker.stop();
  },

  // Listens for engine enabled state changes, and updates the tracker's state.
  // This is an async observer because the tracker waits on all its async
  // observers to finish when it's stopped.
  async observe(subject, topic, data) {
    if (
      topic == "nsPref:changed" &&
      data == `services.sync.engine.${this.prefName}`
    ) {
      await this._tracker.onEngineEnabledChanged(this._enabled);
    }
  },

  async sync() {
    if (!this.enabled) {
      return false;
    }

    if (!this._sync) {
      throw new Error("engine does not implement _sync method");
    }

    return this._notify("sync", this.name, this._sync)();
  },

  // Override this method to return a new changeset type.
  emptyChangeset() {
    return new Changeset();
  },

  /**
   * Returns the local sync ID for this engine, or `""` if the engine hasn't
   * synced for the first time. This is exposed for tests.
   *
   * @return the current sync ID.
   */
  async getSyncID() {
    return this._syncID;
  },

  /**
   * Ensures that the local sync ID for the engine matches the sync ID for the
   * collection on the server. A mismatch indicates that another client wiped
   * the collection; we're syncing after a node reassignment, and another
   * client synced before us; or the store was replaced since the last sync.
   * In case of a mismatch, we need to reset all local Sync state and start
   * over as a first sync.
   *
   * In most cases, this method should return the new sync ID as-is. However, an
   * engine may ignore the given ID and assign a different one, if it determines
   * that the sync ID on the server is out of date. The bookmarks engine uses
   * this to wipe the server and other clients on the first sync after the user
   * restores from a backup.
   *
   * @param  newSyncID
   *         The new sync ID for the collection from `meta/global`.
   * @return The assigned sync ID. If this doesn't match `newSyncID`, we'll
   *         replace the sync ID in `meta/global` with the assigned ID.
   */
  async ensureCurrentSyncID(newSyncID) {
    let existingSyncID = this._syncID;
    if (existingSyncID == newSyncID) {
      return existingSyncID;
    }
    this._log.debug(
      `Engine syncIDs differ (old="${existingSyncID}", new="${newSyncID}") - resetting the engine`
    );
    await this.resetClient();
    Svc.PrefBranch.setStringPref(this.name + ".syncID", newSyncID);
    Svc.PrefBranch.setStringPref(this.name + ".lastSync", "0");
    return newSyncID;
  },

  /**
   * Resets the local sync ID for the engine, wipes the server, and resets all
   * local Sync state to start over as a first sync.
   *
   * @return the new sync ID.
   */
  async resetSyncID() {
    let newSyncID = await this.resetLocalSyncID();
    await this.wipeServer();
    return newSyncID;
  },

  /**
   * Resets the local sync ID for the engine, signaling that we're starting over
   * as a first sync.
   *
   * @return the new sync ID.
   */
  async resetLocalSyncID() {
    return this.ensureCurrentSyncID(Utils.makeGUID());
  },

  /**
   * Allows overriding scheduler logic -- added to help reduce kinto server
   * getting hammered because our scheduler never got tuned for it.
   *
   * Note: Overriding engines must take resyncs into account -- score will not
   * be cleared.
   */
  shouldSkipSync() {
    return false;
  },

  /*
   * lastSync is a timestamp in server time.
   */
  async getLastSync() {
    return this._lastSync;
  },
  async setLastSync(lastSync) {
    // Store the value as a string to keep floating point precision
    Svc.PrefBranch.setStringPref(this.name + ".lastSync", lastSync.toString());
  },
  async resetLastSync() {
    this._log.debug("Resetting " + this.name + " last sync time");
    await this.setLastSync(0);
  },

  get hasSyncedThisSession() {
    return this._hasSyncedThisSession;
  },

  set hasSyncedThisSession(hasSynced) {
    this._hasSyncedThisSession = hasSynced;
  },

  get toFetch() {
    this._toFetchStorage.ensureDataReady();
    return this._toFetchStorage.data.ids;
  },

  set toFetch(ids) {
    if (ids.constructor.name != "SerializableSet") {
      throw new Error(
        "Bug: Attempted to set toFetch to something that isn't a SerializableSet"
      );
    }
    this._toFetchStorage.data = { ids };
    this._toFetchStorage.saveSoon();
  },

  get previousFailedIn() {
    this._previousFailedInStorage.ensureDataReady();
    return this._previousFailedInStorage.data.ids;
  },

  set previousFailedIn(ids) {
    if (ids.constructor.name != "SerializableSet") {
      throw new Error(
        "Bug: Attempted to set previousFailedIn to something that isn't a SerializableSet"
      );
    }
    this._previousFailedInStorage.data = { ids };
    this._previousFailedInStorage.saveSoon();
  },

  get previousFailedOut() {
    this._previousFailedOutStorage.ensureDataReady();
    return this._previousFailedOutStorage.data.ids;
  },

  set previousFailedOut(ids) {
    if (ids.constructor.name != "SerializableSet") {
      throw new Error(
        "Bug: Attempted to set previousFailedOut to something that isn't a SerializableSet"
      );
    }
    this._previousFailedOutStorage.data = { ids };
    this._previousFailedOutStorage.saveSoon();
  },

  /*
   * Returns a changeset for this sync. Engine implementations can override this
   * method to bypass the tracker for certain or all changed items.
   */
  async getChangedIDs() {
    return this._tracker.getChangedIDs();
  },

  // Create a new record using the store and add in metadata.
  async _createRecord(id) {
    let record = await this._store.createRecord(id, this.name);
    record.id = id;
    record.collection = this.name;
    return record;
  },

  // Creates a tombstone Sync record with additional metadata.
  _createTombstone(id) {
    let tombstone = new this._recordObj(this.name, id);
    tombstone.id = id;
    tombstone.collection = this.name;
    tombstone.deleted = true;
    return tombstone;
  },

  /**
   * Returns this engine's `{version, syncID}` from `meta/syncids`, if it
   * matches this account. Per-device clients keep the generations there that
   * they can't publish in meta/global.
   */
  async _getSyncIDsEngineData() {
    let record = new WBORecord("meta", "syncids");
    await record.fetch(
      this.service.resource(this.service.storageURL + "meta/syncids")
    );
    let { response } = record;
    // Don't reset on errors: we'd discard a generation we couldn't read.
    if (!response.success && response.status != 404) {
      throw response;
    }
    let { version, globalSyncID, engines } =
      (response.success && record.payload) || {};
    let data = engines?.[this.name];
    if (version == 1 && globalSyncID == this.service.syncID && data?.syncID) {
      return { version: data.version, syncID: data.syncID };
    }
    return null;
  },

  // Any setup that needs to happen at the beginning of each sync.
  async _syncStartup() {
    // Determine if we need to wipe on outdated versions
    let metaGlobal = await this.service.recordManager.get(this.metaURL);
    let engines = metaGlobal.payload.engines || {};
    let engineData = engines[this.name] || {};
    // Adopting a per-device client's generation avoids resetting it. A
    // meta/global we just created has nothing to adopt.
    if (!engines[this.name] && !metaGlobal.isNew) {
      let syncIDsData = await this._getSyncIDsEngineData();
      if (syncIDsData) {
        this._log.debug("Adopting the sync ID from meta/syncids", syncIDsData);
        engineData = syncIDsData;
        engines[this.name] = engineData;
        metaGlobal.payload.engines = engines;
        metaGlobal.changed = true;
      }
    }

    // Assume missing versions are 0 and wipe the server
    if ((engineData.version || 0) < this.version) {
      this._log.debug("Old engine data: " + [engineData.version, this.version]);

      // Clear the server and reupload everything on bad version or missing
      // meta. Note that we don't regenerate per-collection keys here.
      let newSyncID = await this.resetSyncID();

      // Set the newer version and newly generated syncID
      engineData.version = this.version;
      engineData.syncID = newSyncID;

      // Put the new data back into meta/global and mark for upload
      engines[this.name] = engineData;
      metaGlobal.payload.engines = engines;
      metaGlobal.changed = true;
    } else if (engineData.version > this.version) {
      // Don't sync this engine if the server has newer data

      let error = new Error("New data: " + [engineData.version, this.version]);
      error.failureCode = VERSION_OUT_OF_DATE;
      throw error;
    } else {
      // Changes to syncID mean we'll need to upload everything
      let assignedSyncID = await this.ensureCurrentSyncID(engineData.syncID);
      if (assignedSyncID != engineData.syncID) {
        engineData.syncID = assignedSyncID;
        metaGlobal.changed = true;
      }
    }

    // Save objects that need to be uploaded in this._modified. As we
    // successfully upload objects we remove them from this._modified. If an
    // error occurs or any objects fail to upload, they will remain in
    // this._modified. At the end of a sync, or after an error, we add all
    // objects remaining in this._modified to the tracker.
    let initialChanges = await this.pullChanges();
    this._modified.replace(initialChanges);
    // Clear the tracker now. If the sync fails we'll add the ones we failed
    // to upload back.
    this._tracker.clearChangedIDs();
    this._tracker.resetScore();

    // Keep track of what to delete at the end of sync
    this._delete = {};
  },

  async pullChanges() {
    let lastSync = await this.getLastSync();
    if (lastSync) {
      return this.pullNewChanges();
    }
    this._log.debug("First sync, uploading all items");
    return this.pullAllChanges();
  },

  /**
   * A tiny abstraction to make it easier to test incoming record
   * application.
   */
  itemSource() {
    return new Collection(this.engineURL, this._recordObj, this.service);
  },

  /**
   * Download and apply remote records changed since the last sync. This
   * happens in three stages.
   *
   * In the first stage, we fetch full records for all changed items, newest
   * first, up to the download limit. The limit lets us make progress for large
   * collections, where the sync is likely to be interrupted before we
   * can fetch everything.
   *
   * In the second stage, we fetch the IDs of any remaining records changed
   * since the last sync, add them to our backlog, and fast-forward our last
   * sync time.
   *
   * In the third stage, we fetch and apply records for all backlogged IDs,
   * as well as any records that failed to apply during the last sync. We
   * request records for the IDs in chunks, to avoid exceeding URL length
   * limits, then remove successfully applied records from the backlog, and
   * record IDs of any records that failed to apply to retry on the next sync.
   */
  async _processIncoming() {
    this._log.trace("Downloading & applying server changes");

    let newitems = this.itemSource();
    let lastSync = await this.getLastSync();

    newitems.newer = lastSync;
    newitems.full = true;

    let downloadLimit = Infinity;
    if (this.downloadLimit) {
      // Fetch new records up to the download limit. Currently, only the history
      // engine sets a limit, since the history collection has the highest volume
      // of changed records between syncs. The other engines fetch all records
      // changed since the last sync.
      if (this._defaultSort) {
        // A download limit with a sort order doesn't make sense: we won't know
        // which records to backfill.
        throw new Error("Can't specify download limit with default sort order");
      }
      newitems.sort = "newest";
      downloadLimit = newitems.limit = this.downloadLimit;
    } else if (this._defaultSort) {
      // The bookmarks engine fetches records by sort index; other engines leave
      // the order unspecified. We can remove `_defaultSort` entirely after bug
      // 1305563: the sort index won't matter because we'll buffer all bookmarks
      // before applying.
      newitems.sort = this._defaultSort;
    }

    // applied    => number of items that should be applied.
    // failed     => number of items that failed in this sync.
    // newFailed  => number of items that failed for the first time in this sync.
    // reconciled => number of items that were reconciled.
    // failedReasons => {name, count} of reasons a record failed
    let countTelemetry = new SyncedRecordsTelemetry();
    let count = countTelemetry.incomingCounts;
    let recordsToApply = [];
    let failedInCurrentSync = new SerializableSet();

    let oldestModified = this.lastModified;
    let downloadedIDs = new Set();

    // Stage 1: Fetch new records from the server, up to the download limit.
    if (this.lastModified == null || this.lastModified > lastSync) {
      let { response, records } = await newitems.getBatched(
        this.downloadBatchSize
      );
      if (!response.success) {
        response.failureCode = ENGINE_DOWNLOAD_FAIL;
        throw response;
      }

      await Async.yieldingForEach(records, async record => {
        downloadedIDs.add(record.id);

        if (record.modified < oldestModified) {
          oldestModified = record.modified;
        }

        let { shouldApply, error } = await this._maybeReconcile(record);
        if (error) {
          failedInCurrentSync.add(record.id);
          count.failed++;
          countTelemetry.addIncomingFailedReason(error.message);
          return;
        }
        if (!shouldApply) {
          count.reconciled++;
          return;
        }
        recordsToApply.push(record);
      });

      let failedToApply = await this._applyRecords(
        recordsToApply,
        countTelemetry
      );
      Utils.setAddAll(failedInCurrentSync, failedToApply);

      // `applied` is a bit of a misnomer: it counts records that *should* be
      // applied, so it also includes records that we tried to apply and failed.
      // `recordsToApply.length - failedToApply.length` is the number of records
      // that we *successfully* applied.
      count.failed += failedToApply.length;
      count.applied += recordsToApply.length;
    }

    // Stage 2: If we reached our download limit, we might still have records
    // on the server that changed since the last sync. Fetch the IDs for the
    // remaining records, and add them to the backlog. Note that this stage
    // only runs for engines that set a download limit.
    if (downloadedIDs.size == downloadLimit) {
      let guidColl = this.itemSource();

      guidColl.newer = lastSync;
      guidColl.older = oldestModified;
      guidColl.sort = "oldest";

      let guids = await guidColl.get();
      if (!guids.success) {
        throw guids;
      }

      // Filtering out already downloaded IDs here isn't necessary. We only do
      // that in case the Sync server doesn't support `older` (bug 1316110).
      let remainingIDs = guids.obj.filter(id => !downloadedIDs.has(id));
      if (remainingIDs.length) {
        this.toFetch = Utils.setAddAll(this.toFetch, remainingIDs);
      }
    }

    // Fast-foward the lastSync timestamp since we have backlogged the
    // remaining items.
    if (lastSync < this.lastModified) {
      lastSync = this.lastModified;
      await this.setLastSync(lastSync);
    }

    // Stage 3: Backfill records from the backlog, and those that failed to
    // decrypt or apply during the last sync. We only backfill up to the
    // download limit, to prevent a large backlog for one engine from blocking
    // the others. We'll keep processing the backlog on subsequent engine syncs.
    let failedInPreviousSync = this.previousFailedIn;
    let idsToBackfill = Array.from(
      Utils.setAddAll(
        Utils.subsetOfSize(this.toFetch, downloadLimit),
        failedInPreviousSync
      )
    );

    // Note that we intentionally overwrite the previously failed list here.
    // Records that fail to decrypt or apply in two consecutive syncs are likely
    // corrupt; we remove them from the list because retrying and failing on
    // every subsequent sync just adds noise.
    this.previousFailedIn = failedInCurrentSync;

    let backfilledItems = this.itemSource();

    backfilledItems.sort = "newest";
    backfilledItems.full = true;

    // `getBatched` includes the list of IDs as a query parameter, so we need to fetch
    // records in chunks to avoid exceeding URI length limits.
    if (this.guidFetchBatchSize) {
      for (let ids of lazy.PlacesUtils.chunkArray(
        idsToBackfill,
        this.guidFetchBatchSize
      )) {
        backfilledItems.ids = ids;

        let { response, records } = await backfilledItems.getBatched(
          this.downloadBatchSize
        );
        if (!response.success) {
          response.failureCode = ENGINE_DOWNLOAD_FAIL;
          throw response;
        }

        let backfilledRecordsToApply = [];
        let failedInBackfill = [];

        await Async.yieldingForEach(records, async record => {
          let { shouldApply, error } = await this._maybeReconcile(record);
          if (error) {
            failedInBackfill.push(record.id);
            count.failed++;
            countTelemetry.addIncomingFailedReason(error.message);
            return;
          }
          if (!shouldApply) {
            count.reconciled++;
            return;
          }
          backfilledRecordsToApply.push(record);
        });

        let failedToApply = await this._applyRecords(
          backfilledRecordsToApply,
          countTelemetry
        );
        failedInBackfill.push(...failedToApply);

        count.failed += failedToApply.length;
        count.applied += backfilledRecordsToApply.length;

        this.toFetch = Utils.setDeleteAll(this.toFetch, ids);
        this.previousFailedIn = Utils.setAddAll(
          this.previousFailedIn,
          failedInBackfill
        );

        if (lastSync < this.lastModified) {
          lastSync = this.lastModified;
          await this.setLastSync(lastSync);
        }
      }
    }

    count.newFailed = 0;
    for (let item of this.previousFailedIn) {
      // Anything that failed in the current sync that also failed in
      // the previous sync means there is likely something wrong with
      // the record, we remove it from trying again to prevent
      // infinitely syncing corrupted records
      if (failedInPreviousSync.has(item)) {
        this.previousFailedIn.delete(item);
      } else {
        // otherwise it's a new failed and we count it as so
        ++count.newFailed;
      }
    }

    count.succeeded = Math.max(0, count.applied - count.failed);
    this._log.info(
      [
        "Records:",
        count.applied,
        "applied,",
        count.succeeded,
        "successfully,",
        count.failed,
        "failed to apply,",
        count.newFailed,
        "newly failed to apply,",
        count.reconciled,
        "reconciled.",
      ].join(" ")
    );
    Observers.notify("weave:engine:sync:applied", count, this.name);
  },

  async _maybeReconcile(item) {
    let key = this.service.collectionKeys.keyForCollection(this.name);

    // Grab a later last modified if possible
    if (this.lastModified == null || item.modified > this.lastModified) {
      this.lastModified = item.modified;
    }

    try {
      try {
        await item.decrypt(key);
      } catch (ex) {
        if (!Utils.isHMACMismatch(ex)) {
          throw ex;
        }
        let strategy = await this.handleHMACMismatch(item, true);
        if (strategy == SyncEngine.kRecoveryStrategy.retry) {
          // You only get one retry.
          try {
            // Try decrypting again, typically because we've got new keys.
            this._log.info("Trying decrypt again...");
            key = this.service.collectionKeys.keyForCollection(this.name);
            await item.decrypt(key);
            strategy = null;
          } catch (ex) {
            if (!Utils.isHMACMismatch(ex)) {
              throw ex;
            }
            strategy = await this.handleHMACMismatch(item, false);
          }
        }

        switch (strategy) {
          case null:
            // Retry succeeded! No further handling.
            break;
          case SyncEngine.kRecoveryStrategy.retry:
            this._log.debug("Ignoring second retry suggestion.");
          // Fall through to error case.
          case SyncEngine.kRecoveryStrategy.error:
            this._log.warn("Error decrypting record", ex);
            return { shouldApply: false, error: ex };
          case SyncEngine.kRecoveryStrategy.ignore:
            this._log.debug(
              "Ignoring record " + item.id + " with bad HMAC: already handled."
            );
            return { shouldApply: false, error: null };
        }
      }
    } catch (ex) {
      if (Async.isShutdownException(ex)) {
        throw ex;
      }
      this._log.warn("Error decrypting record", ex);
      return { shouldApply: false, error: ex };
    }

    if (this._shouldDeleteRemotely(item)) {
      this._log.trace("Deleting item from server without applying", item);
      await this._deleteId(item.id);
      return { shouldApply: false, error: null };
    }

    let shouldApply;
    try {
      shouldApply = await this._reconcile(item);
    } catch (ex) {
      if (ex.code == SyncEngine.prototype.eEngineAbortApplyIncoming) {
        this._log.warn("Reconciliation failed: aborting incoming processing.");
        throw ex.cause;
      } else if (!Async.isShutdownException(ex)) {
        this._log.warn("Failed to reconcile incoming record " + item.id, ex);
        return { shouldApply: false, error: ex };
      } else {
        throw ex;
      }
    }

    if (!shouldApply) {
      this._log.trace("Skipping reconciled incoming item " + item.id);
    }

    return { shouldApply, error: null };
  },

  async _applyRecords(records, countTelemetry) {
    this._tracker.ignoreAll = true;
    try {
      let failedIDs = await this._store.applyIncomingBatch(
        records,
        countTelemetry
      );
      return failedIDs;
    } catch (ex) {
      // Catch any error that escapes from applyIncomingBatch. At present
      // those will all be abort events.
      this._log.warn("Got exception, aborting processIncoming", ex);
      throw ex;
    } finally {
      this._tracker.ignoreAll = false;
    }
  },

  // Indicates whether an incoming item should be deleted from the server at
  // the end of the sync. Engines can override this method to clean up records
  // that shouldn't be on the server.
  _shouldDeleteRemotely() {
    return false;
  },

  /**
   * Find a GUID of an item that is a duplicate of the incoming item but happens
   * to have a different GUID
   *
   * @return GUID of the similar item; falsy otherwise
   */
  async _findDupe() {
    // By default, assume there's no dupe items for the engine
  },

  /**
   * Called before a remote record is discarded due to failed reconciliation.
   * Used by bookmark sync to merge folder child orders.
   */
  beforeRecordDiscard() {},

  // Called when the server has a record marked as deleted, but locally we've
  // changed it more recently than the deletion. If we return false, the
  // record will be deleted locally. If we return true, we'll reupload the
  // record to the server -- any extra work that's needed as part of this
  // process should be done at this point (such as mark the record's parent
  // for reuploading in the case of bookmarks).
  async _shouldReviveRemotelyDeletedRecord() {
    return true;
  },

  async _deleteId(id) {
    await this._tracker.removeChangedID(id);
    this._noteDeletedId(id);
  },

  // Marks an ID for deletion at the end of the sync.
  _noteDeletedId(id) {
    if (this._delete.ids == null) {
      this._delete.ids = [id];
    } else {
      this._delete.ids.push(id);
    }
  },

  async _switchItemToDupe(localDupeGUID, incomingItem) {
    // The local, duplicate ID is always deleted on the server.
    await this._deleteId(localDupeGUID);

    // We unconditionally change the item's ID in case the engine knows of
    // an item but doesn't expose it through itemExists. If the API
    // contract were stronger, this could be changed.
    this._log.debug(
      "Switching local ID to incoming: " +
        localDupeGUID +
        " -> " +
        incomingItem.id
    );
    return this._store.changeItemID(localDupeGUID, incomingItem.id);
  },

  /**
   * Reconcile incoming record with local state.
   *
   * This function essentially determines whether to apply an incoming record.
   *
   * @param  item
   *         Record from server to be tested for application.
   * @return boolean
   *         Truthy if incoming record should be applied. False if not.
   */
  async _reconcile(item) {
    if (this._log.traceEnabled) {
      this._log.trace("Incoming: " + item);
    }

    // We start reconciling by collecting a bunch of state. We do this here
    // because some state may change during the course of this function and we
    // need to operate on the original values.
    let existsLocally = await this._store.itemExists(item.id);
    let locallyModified = this._modified.has(item.id);

    // TODO Handle clock drift better. Tracked in bug 721181.
    let remoteAge = Resource.serverTime - item.modified;
    let localAge = locallyModified
      ? Date.now() / 1000 - this._modified.getModifiedTimestamp(item.id)
      : null;
    let remoteIsNewer = remoteAge < localAge;

    this._log.trace(
      "Reconciling " +
        item.id +
        ". exists=" +
        existsLocally +
        "; modified=" +
        locallyModified +
        "; local age=" +
        localAge +
        "; incoming age=" +
        remoteAge
    );

    // We handle deletions first so subsequent logic doesn't have to check
    // deleted flags.
    if (item.deleted) {
      // If the item doesn't exist locally, there is nothing for us to do. We
      // can't check for duplicates because the incoming record has no data
      // which can be used for duplicate detection.
      if (!existsLocally) {
        this._log.trace(
          "Ignoring incoming item because it was deleted and " +
            "the item does not exist locally."
        );
        return false;
      }

      // We decide whether to process the deletion by comparing the record
      // ages. If the item is not modified locally, the remote side wins and
      // the deletion is processed. If it is modified locally, we take the
      // newer record.
      if (!locallyModified) {
        this._log.trace(
          "Applying incoming delete because the local item " +
            "exists and isn't modified."
        );
        return true;
      }
      this._log.trace("Incoming record is deleted but we had local changes.");

      if (remoteIsNewer) {
        this._log.trace("Remote record is newer -- deleting local record.");
        return true;
      }
      // If the local record is newer, we defer to individual engines for
      // how to handle this. By default, we revive the record.
      let willRevive = await this._shouldReviveRemotelyDeletedRecord(item);
      this._log.trace("Local record is newer -- reviving? " + willRevive);

      return !willRevive;
    }

    // At this point the incoming record is not for a deletion and must have
    // data. If the incoming record does not exist locally, we check for a local
    // duplicate existing under a different ID. The default implementation of
    // _findDupe() is empty, so engines have to opt in to this functionality.
    //
    // If we find a duplicate, we change the local ID to the incoming ID and we
    // refresh the metadata collected above. See bug 710448 for the history
    // of this logic.
    if (!existsLocally) {
      let localDupeGUID = await this._findDupe(item);
      if (localDupeGUID) {
        this._log.trace(
          "Local item " +
            localDupeGUID +
            " is a duplicate for " +
            "incoming item " +
            item.id
        );

        // The current API contract does not mandate that the ID returned by
        // _findDupe() actually exists. Therefore, we have to perform this
        // check.
        existsLocally = await this._store.itemExists(localDupeGUID);

        // If the local item was modified, we carry its metadata forward so
        // appropriate reconciling can be performed.
        if (this._modified.has(localDupeGUID)) {
          locallyModified = true;
          localAge =
            this._tracker._now() -
            this._modified.getModifiedTimestamp(localDupeGUID);
          remoteIsNewer = remoteAge < localAge;

          this._modified.changeID(localDupeGUID, item.id);
        } else {
          locallyModified = false;
          localAge = null;
        }

        // Tell the engine to do whatever it needs to switch the items.
        await this._switchItemToDupe(localDupeGUID, item);

        this._log.debug(
          "Local item after duplication: age=" +
            localAge +
            "; modified=" +
            locallyModified +
            "; exists=" +
            existsLocally
        );
      } else {
        this._log.trace("No duplicate found for incoming item: " + item.id);
      }
    }

    // At this point we've performed duplicate detection. But, nothing here
    // should depend on duplicate detection as the above should have updated
    // state seamlessly.

    if (!existsLocally) {
      // If the item doesn't exist locally and we have no local modifications
      // to the item (implying that it was not deleted), always apply the remote
      // item.
      if (!locallyModified) {
        this._log.trace(
          "Applying incoming because local item does not exist " +
            "and was not deleted."
        );
        return true;
      }

      // If the item was modified locally but isn't present, it must have
      // been deleted. If the incoming record is younger, we restore from
      // that record.
      if (remoteIsNewer) {
        this._log.trace(
          "Applying incoming because local item was deleted " +
            "before the incoming item was changed."
        );
        this._modified.delete(item.id);
        return true;
      }

      this._log.trace(
        "Ignoring incoming item because the local item's " +
          "deletion is newer."
      );
      return false;
    }

    // If the remote and local records are the same, there is nothing to be
    // done, so we don't do anything. In the ideal world, this logic wouldn't
    // be here and the engine would take a record and apply it. The reason we
    // want to defer this logic is because it would avoid a redundant and
    // possibly expensive dip into the storage layer to query item state.
    // This should get addressed in the async rewrite, so we ignore it for now.
    let localRecord = await this._createRecord(item.id);
    let recordsEqual = Utils.deepEquals(item.cleartext, localRecord.cleartext);

    // If the records are the same, we don't need to do anything. This does
    // potentially throw away a local modification time. But, if the records
    // are the same, does it matter?
    if (recordsEqual) {
      this._log.trace(
        "Ignoring incoming item because the local item is identical."
      );

      this._modified.delete(item.id);
      return false;
    }

    // At this point the records are different.

    // If we have no local modifications, always take the server record.
    if (!locallyModified) {
      this._log.trace("Applying incoming record because no local conflicts.");
      return true;
    }

    // At this point, records are different and the local record is modified.
    // We resolve conflicts by record age, where the newest one wins. This does
    // result in data loss and should be handled by giving the engine an
    // opportunity to merge the records. Bug 720592 tracks this feature.
    this._log.warn(
      "DATA LOSS: Both local and remote changes to record: " + item.id
    );
    if (!remoteIsNewer) {
      this.beforeRecordDiscard(localRecord, item, remoteIsNewer);
    }
    return remoteIsNewer;
  },

  // Upload outgoing records.
  async _uploadOutgoing() {
    this._log.trace("Uploading local changes to server.");

    // collection we'll upload
    let up = new Collection(this.engineURL, null, this.service);
    let modifiedIDs = new Set(this._modified.ids());
    let countTelemetry = new SyncedRecordsTelemetry();
    let counts = countTelemetry.outgoingCounts;
    this._log.info(`Uploading ${modifiedIDs.size} outgoing records`);
    if (modifiedIDs.size) {
      counts.sent = modifiedIDs.size;

      let failed = [];
      let successful = [];
      let lastSync = await this.getLastSync();
      let handleResponse = async (postQueue, resp, batchOngoing) => {
        // Note: We don't want to update this.lastSync, or this._modified until
        // the batch is complete, however we want to remember success/failure
        // indicators for when that happens.
        if (!resp.success) {
          this._log.debug(`Uploading records failed: ${resp.status}`);
          resp.failureCode =
            resp.status == 412 ? ENGINE_BATCH_INTERRUPTED : ENGINE_UPLOAD_FAIL;
          throw resp;
        }

        // Update server timestamp from the upload.
        failed = failed.concat(Object.keys(resp.obj.failed));
        successful = successful.concat(resp.obj.success);

        if (batchOngoing) {
          // Nothing to do yet
          return;
        }

        if (failed.length && this._log.debugEnabled) {
          this._log.debug(
            "Records that will be uploaded again because " +
              "the server couldn't store them: " +
              failed.join(", ")
          );
        }

        counts.failed += failed.length;
        Object.values(failed).forEach(message => {
          countTelemetry.addOutgoingFailedReason(message);
        });

        for (const id of failed) {
          // Retry once.
          if (this.previousFailedOut.has(id)) {
            this._modified.delete(id);
            this.previousFailedOut.delete(id);
          } else {
            this.previousFailedOut.add(id);
          }
        }

        for (let id of successful) {
          this._modified.delete(id);
          this.previousFailedOut.delete(id);
        }

        await this._onRecordsWritten(
          successful,
          failed,
          postQueue.lastModified
        );

        // Advance lastSync since we've finished the batch.
        if (postQueue.lastModified > lastSync) {
          lastSync = postQueue.lastModified;
          await this.setLastSync(lastSync);
        }

        // clear for next batch
        failed.length = 0;
        successful.length = 0;
      };

      let postQueue = up.newPostQueue(this._log, lastSync, handleResponse);

      for (let id of modifiedIDs) {
        let out;
        let ok = false;
        try {
          out = await this._createRecord(id);
          if (this._log.traceEnabled) {
            this._log.trace("Outgoing: " + out);
          }
          await out.encrypt(
            this.service.collectionKeys.keyForCollection(this.name)
          );
          ok = true;
        } catch (ex) {
          this._log.warn("Error creating record", ex);
          ++counts.failed;
          countTelemetry.addOutgoingFailedReason(ex.message);
          if (Async.isShutdownException(ex) || !this.allowSkippedRecord) {
            if (!this.allowSkippedRecord) {
              // Don't bother for shutdown errors
              Observers.notify("weave:engine:sync:uploaded", counts, this.name);
            }
            throw ex;
          }
        }
        if (ok) {
          let { enqueued, error } = await postQueue.enqueue(out);
          if (!enqueued) {
            ++counts.failed;
            countTelemetry.addOutgoingFailedReason(error.message);
            if (!this.allowSkippedRecord) {
              Observers.notify("weave:engine:sync:uploaded", counts, this.name);
              this._log.warn(
                `Failed to enqueue record "${id}" (aborting)`,
                error
              );
              throw error;
            }
            this._modified.delete(id);
            this._log.warn(
              `Failed to enqueue record "${id}" (skipping)`,
              error
            );
          }
        }
        await Async.promiseYield();
      }
      await postQueue.flush(true);
    }

    if (counts.sent || counts.failed) {
      Observers.notify("weave:engine:sync:uploaded", counts, this.name);
    }
  },

  async _onRecordsWritten() {
    // Implement this method to take specific actions against successfully
    // uploaded records and failed records.
  },

  // Any cleanup necessary.
  // Save the current snapshot so as to calculate changes at next sync
  async _syncFinish() {
    this._log.trace("Finishing up sync");

    let doDelete = async (key, val) => {
      let coll = new Collection(this.engineURL, this._recordObj, this.service);
      coll[key] = val;
      await coll.delete();
    };

    for (let [key, val] of Object.entries(this._delete)) {
      // Remove the key for future uses
      delete this._delete[key];

      this._log.trace("doing post-sync deletions", { key, val });
      // Send a simple delete for the property
      if (key != "ids" || val.length <= 100) {
        await doDelete(key, val);
      } else {
        // For many ids, split into chunks of at most 100
        while (val.length) {
          await doDelete(key, val.slice(0, 100));
          val = val.slice(100);
        }
      }
    }
    this.hasSyncedThisSession = true;
    await this._tracker.asyncObserver.promiseObserversComplete();
  },

  async _syncCleanup() {
    try {
      // Mark failed WBOs as changed again so they are reuploaded next time.
      await this.trackRemainingChanges();
    } finally {
      this._modified.clear();
    }
  },

  async _sync() {
    try {
      Async.checkAppReady();
      await this._syncStartup();
      Async.checkAppReady();
      Observers.notify("weave:engine:sync:status", "process-incoming");
      await this._processIncoming();
      Async.checkAppReady();
      Observers.notify("weave:engine:sync:status", "upload-outgoing");
      try {
        await this._uploadOutgoing();
        Async.checkAppReady();
        await this._syncFinish();
      } catch (ex) {
        if (!ex.status || ex.status != 412) {
          throw ex;
        }
        // a 412 posting just means another client raced - but we don't want
        // to treat that as a sync error - the next sync is almost certain
        // to work.
        this._log.warn("412 error during sync - will retry.");
      }
    } finally {
      await this._syncCleanup();
    }
  },

  async canDecrypt() {
    // Report failure even if there's nothing to decrypt
    let canDecrypt = false;

    // Fetch the most recently uploaded record and try to decrypt it
    let test = new Collection(this.engineURL, this._recordObj, this.service);
    test.limit = 1;
    test.sort = "newest";
    test.full = true;

    let key = this.service.collectionKeys.keyForCollection(this.name);

    // Any failure fetching/decrypting will just result in false
    try {
      this._log.trace("Trying to decrypt a record from the server..");
      let json = (await test.get()).obj[0];
      let record = new this._recordObj();
      record.deserialize(json);
      await record.decrypt(key);
      canDecrypt = true;
    } catch (ex) {
      if (Async.isShutdownException(ex)) {
        throw ex;
      }
      this._log.debug("Failed test decrypt", ex);
    }

    return canDecrypt;
  },

  /**
   * Deletes the collection for this engine on the server, and removes all local
   * Sync metadata for this engine. This does *not* remove any existing data on
   * other clients. This is called when we reset the sync ID.
   */
  async wipeServer() {
    await this._deleteServerCollection();
    await this._resetClient();
  },

  /**
   * Deletes the collection for this engine on the server, without removing
   * any local Sync metadata or user data. Deleting the collection will not
   * remove any user data on other clients, but will force other clients to
   * start over as a first sync.
   */
  async _deleteServerCollection() {
    let response = await this.service.resource(this.engineURL).delete();
    if (response.status != 200 && response.status != 404) {
      throw response;
    }
  },

  async removeClientData() {
    // Implement this method in engines that store client specific data
    // on the server.
  },

  /*
   * Decide on (and partially effect) an error-handling strategy.
   *
   * Asks the Service to respond to an HMAC error, which might result in keys
   * being downloaded. That call returns true if an action which might allow a
   * retry to occur.
   *
   * If `mayRetry` is truthy, and the Service suggests a retry,
   * handleHMACMismatch returns kRecoveryStrategy.retry. Otherwise, it returns
   * kRecoveryStrategy.error.
   *
   * Subclasses of SyncEngine can override this method to allow for different
   * behavior -- e.g., to delete and ignore erroneous entries.
   *
   * All return values will be part of the kRecoveryStrategy enumeration.
   */
  async handleHMACMismatch(item, mayRetry) {
    // By default we either try again, or bail out noisily.
    return (await this.service.handleHMACEvent()) && mayRetry
      ? SyncEngine.kRecoveryStrategy.retry
      : SyncEngine.kRecoveryStrategy.error;
  },

  /**
   * Returns a changeset containing all items in the store. The default
   * implementation returns a changeset with timestamps from long ago, to
   * ensure we always use the remote version if one exists.
   *
   * This function is only called for the first sync. Subsequent syncs call
   * `pullNewChanges`.
   *
   * @return A `Changeset` object.
   */
  async pullAllChanges() {
    let changes = {};
    let ids = await this._store.getAllIDs();
    for (let id in ids) {
      changes[id] = 0;
    }
    return changes;
  },

  /**
   * Returns a changeset containing entries for all currently tracked items.
   * The default implementation returns a changeset with timestamps indicating
   * when the item was added to the tracker.
   *
   * @return A `Changeset` object.
   */
  async pullNewChanges() {
    await this._tracker.asyncObserver.promiseObserversComplete();
    return this.getChangedIDs();
  },

  /**
   * Adds all remaining changeset entries back to the tracker, typically for
   * items that failed to upload. This method is called at the end of each sync.
   *
   */
  async trackRemainingChanges() {
    for (let [id, change] of this._modified.entries()) {
      await this._tracker.addChangedID(id, change);
    }
  },

  /**
   * Removes all local Sync metadata for this engine, but keeps all existing
   * local user data.
   */
  async resetClient() {
    return this._notify("reset-client", this.name, this._resetClient)();
  },

  async _resetClient() {
    await this.resetLastSync();
    this.hasSyncedThisSession = false;
    this.previousFailedIn = new SerializableSet();
    this.previousFailedOut = new SerializableSet();
    this.toFetch = new SerializableSet();
  },

  /**
   * Removes all local Sync metadata and user data for this engine.
   */
  async wipeClient() {
    return this._notify("wipe-client", this.name, this._wipeClient)();
  },

  async _wipeClient() {
    await this.resetClient();
    this._log.debug("Deleting all local data");
    this._tracker.ignoreAll = true;
    await this._store.wipe();
    this._tracker.ignoreAll = false;
    this._tracker.clearChangedIDs();
  },

  /**
   * If one exists, initialize and return a validator for this engine (which
   * must have a `validate(engine)` method that returns a promise to an object
   * with a getSummary method). Otherwise return null.
   */
  getValidator() {
    return null;
  },

  async finalize() {
    Services.prefs.removeObserver(
      `${PREFS_BRANCH}engine.${this.prefName}`,
      this.asyncObserver
    );
    await this.asyncObserver.promiseObserversComplete();
    await this._tracker.finalize();
    await this._toFetchStorage.finalize();
    await this._previousFailedInStorage.finalize();
    await this._previousFailedOutStorage.finalize();
  },

  // Returns a new watchdog. Exposed for tests.
  _newWatchdog() {
    return Async.watchdog();
  },
};

/**
 * A changeset is created for each sync in `Engine::get{Changed, All}IDs`,
 * and stores opaque change data for tracked IDs. The default implementation
 * only records timestamps, though engines can extend this to store additional
 * data for each entry.
 */
export class Changeset {
  // Creates an empty changeset.
  constructor() {
    this.changes = {};
  }

  // Returns the last modified time, in seconds, for an entry in the changeset.
  // `id` is guaranteed to be in the set.
  getModifiedTimestamp(id) {
    return this.changes[id];
  }

  // Adds a change for a tracked ID to the changeset.
  set(id, change) {
    this.changes[id] = change;
  }

  // Adds multiple entries to the changeset, preserving existing entries.
  insert(changes) {
    Object.assign(this.changes, changes);
  }

  // Overwrites the existing set of tracked changes with new entries.
  replace(changes) {
    this.changes = changes;
  }

  // Indicates whether an entry is in the changeset.
  has(id) {
    return id in this.changes;
  }

  // Deletes an entry from the changeset. Used to clean up entries for
  // reconciled and successfully uploaded records.
  delete(id) {
    delete this.changes[id];
  }

  // Changes the ID of an entry in the changeset. Used when reconciling
  // duplicates that have local changes.
  changeID(oldID, newID) {
    this.changes[newID] = this.changes[oldID];
    delete this.changes[oldID];
  }

  // Returns an array of all tracked IDs in this changeset.
  ids() {
    return Object.keys(this.changes);
  }

  // Returns an array of `[id, change]` tuples. Used to repopulate the tracker
  // with entries for failed uploads at the end of a sync.
  entries() {
    return Object.entries(this.changes);
  }

  // Returns the number of entries in this changeset.
  count() {
    return this.ids().length;
  }

  // Clears the changeset.
  clear() {
    this.changes = {};
  }
}
