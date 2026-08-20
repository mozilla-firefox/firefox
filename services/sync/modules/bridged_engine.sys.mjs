/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This file has all the machinery for hooking up bridged engines implemented
 * in Rust. It's the JavaScript side of the Golden Gate bridge that connects
 * Desktop Sync to a Rust sync engine, via the UniFFI-generated bridged engine
 * object (a thin wrapper over the Rust `SyncEngine` trait).
 *
 * Creating a bridged engine only takes a few lines of code, since most of the
 * hard work is done on the Rust side. On the JS side, you'll need to subclass
 * `BridgedEngine` (instead of `SyncEngine`), supply a bridged engine object
 * for your subclass to wrap, and optionally implement and override the tracker.
 */

import { SyncEngine, Tracker } from "resource://services-sync/engines.sys.mjs";
import { RawCryptoWrapper } from "resource://services-sync/record.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  Log: "resource://gre/modules/Log.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
});

/**
 * A stub store that converts between raw decrypted incoming records and
 * envelopes. Since the interface we need is so minimal, this class doesn't
 * inherit from the base `Store` implementation...it would take more code to
 * override all those behaviors!
 *
 * This class isn't meant to be subclassed, because bridged engines shouldn't
 * override their store classes in `_storeObj`.
 */
class BridgedStore {
  constructor(name, engine) {
    if (!engine) {
      throw new Error("Store must be associated with an Engine instance.");
    }
    this.engine = engine;
    this._log = engine.log;
    this._batchChunkSize = 500;
  }

  async applyIncomingBatch(records) {
    for (let chunk of lazy.PlacesUtils.chunkArray(
      records,
      this._batchChunkSize
    )) {
      let incomingEnvelopesAsJSON = chunk.map(record =>
        JSON.stringify(record.toIncomingBso())
      );
      this._log.trace("incoming envelopes", incomingEnvelopesAsJSON);
      await this.engine._bridge.storeIncoming(incomingEnvelopesAsJSON);
    }
    // Array of failed records.
    return [];
  }

  async wipe() {
    await this.engine._bridge.wipe();
  }
}

/**
 * A wrapper class to convert between BSOs on the JS side, and envelopes on the
 * Rust side. This class intentionally subclasses `RawCryptoWrapper`, because we
 * don't want the stringification and parsing machinery in `CryptoWrapper`.
 *
 * This class isn't meant to be subclassed, because bridged engines shouldn't
 * override their record classes in `_recordObj`.
 */
class BridgedRecord extends RawCryptoWrapper {
  /**
   * Creates an outgoing record from a BSO returned by a bridged engine.
   *
   * @param  {string} collection The collection name.
   * @param  {object} bso   The outgoing bso (ie, a sync15::bso::OutgoingBso) returned from
   *                        the bridged engine's `apply`.
   * @return {BridgedRecord}     A Sync record ready to encrypt and upload.
   */
  static fromOutgoingBso(collection, bso) {
    // The BSO has already been JSON serialized coming out of Rust, so the
    // envelope has been flattened.
    if (typeof bso.id != "string") {
      throw new TypeError("Outgoing BSO missing ID");
    }
    if (typeof bso.payload != "string") {
      throw new TypeError("Outgoing BSO missing payload");
    }
    let record = new BridgedRecord(collection, bso.id);
    record.cleartext = bso.payload;
    return record;
  }

  transformBeforeEncrypt(cleartext) {
    if (typeof cleartext != "string") {
      throw new TypeError("Outgoing bridged engine records must be strings");
    }
    return cleartext;
  }

  transformAfterDecrypt(cleartext) {
    if (typeof cleartext != "string") {
      throw new TypeError("Incoming bridged engine records must be strings");
    }
    return cleartext;
  }

  /**
   * Converts this incoming record into an envelope to pass to a bridged engine.
   * This object must be kept in sync with `sync15::IncomingBso`.
   *
   * @return {object} The incoming envelope, to pass to
   *                  the bridged engine's `storeIncoming`.
   */
  toIncomingBso() {
    return {
      id: this.data.id,
      modified: this.data.modified,
      payload: this.cleartext,
    };
  }
}

/**
 * A base class used to plug a Rust engine into Sync, and have it work like any
 * other engine. The constructor takes a bridge as its first argument, which is
 * a "bridged sync engine", as defined by UniFFI in the application-services
 * crate.
 *
 * This class inherits from `SyncEngine`, which has a lot of machinery that we
 * don't need, but that's fairly easy to override. It would be harder to
 * reimplement the machinery that we _do_ need here. However, because of that,
 * this class has lots of methods that do nothing, or return empty data. The
 * docs above each method explain what it's overriding, and why.
 *
 * This class is designed to be subclassed, but the only part that your engine
 * may want to override is `_trackerObj`. Even then, using the default (no-op)
 * tracker is fine, because the shape of the `Tracker` interface may not make
 * sense for all engines.
 */
export function BridgedEngine(name, service) {
  SyncEngine.call(this, name, service);
  // The store is lazily created, which means we'd miss a number of logs if the store
  // managed the log like happens for other engines. But we still keep Store in the name for consistency
  // (but really we should kill all these sub-logs - no-one cares about ".Store" etc!)
  this.log = lazy.Log.repository.getLogger(`Sync.Engine.${name}.Store`);
}

BridgedEngine.prototype = {
  /**
   * The Rust implemented bridge. Must be set by the engine which subclasses us.
   */
  _bridge: null,
  /**
   * The tracker class for this engine. Subclasses may want to override this
   * with their own tracker, though using the default `Tracker` is fine.
   */
  _trackerObj: Tracker,

  /** Returns the record class for all bridged engines. */
  get _recordObj() {
    return BridgedRecord;
  },

  set _recordObj(obj) {
    throw new TypeError("Don't override the record class for bridged engines");
  },

  /** Returns the store class for all bridged engines. */
  get _storeObj() {
    return BridgedStore;
  },

  set _storeObj(obj) {
    throw new TypeError("Don't override the store class for bridged engines");
  },

  /** Returns the storage version for this engine. */
  get version() {
    return this._bridge.storageVersion;
  },

  // Legacy engines allow sync to proceed if some records are too large to
  // upload (eg, a payload that's bigger than the server's published limits).
  // If this returns true, we will just skip the record without even attempting
  // to upload. If this is false, we'll abort the entire batch.
  // If the engine allows this, it will need to detect this scenario by noticing
  // the ID is not in the 'success' records reported to `setUploaded`.
  // (Note that this is not to be confused with the fact server's can currently
  // reject records as part of a POST - but we hope to remove this ability from
  // the server API. Note also that this is not bullet-proof - if the count of
  // records is high, it's possible that we will have committed a previous
  // batch before we hit the relevant limits, so things might have been written.
  // We hope to fix this by ensuring batch limits are such that this is
  // impossible)
  get allowSkippedRecord() {
    return this._bridge.allowSkippedRecord;
  },

  /**
   * Returns the sync ID for this engine. This is exposed for tests, but
   * Sync code always calls `resetSyncID()` and `ensureCurrentSyncID()`,
   * not this.
   *
   * @returns {string?} The sync ID, or `null` if one isn't set.
   */
  async getSyncID() {
    // Note that all methods on an XPCOM class instance are automatically bound,
    // so we don't need to write `this._bridge.getSyncId.bind(this._bridge)`.
    let syncID = await this._bridge.getSyncId();
    return syncID;
  },

  async resetSyncID() {
    await this._deleteServerCollection();
    let newSyncID = await this.resetLocalSyncID();
    return newSyncID;
  },

  async resetLocalSyncID() {
    let newSyncID = await this._bridge.resetSyncId();
    return newSyncID;
  },

  async ensureCurrentSyncID(newSyncID) {
    let assignedSyncID = await this._bridge.ensureCurrentSyncId(newSyncID);
    return assignedSyncID;
  },

  async getLastSync() {
    // The bridge defines lastSync as integer ms, but sync itself wants to work
    // in a float seconds with 2 decimal places.
    let lastSyncMS = await this._bridge.lastSync();
    return Math.round(lastSyncMS / 10) / 100;
  },

  // Engines own their own last-sync timestamp, Only use-case to set it is a reset.
  async resetLastSync() {
    this._log.debug(`Resetting ${this.name} last sync time`);
    await this._bridge.resetLastSync();
  },

  /**
   * Returns the initial changeset for the sync. Bridged engines handle
   * reconciliation internally, so we don't know what changed until after we've
   * stored and applied all incoming records. So we return an empty changeset
   * here, and replace it with the real one in `_processIncoming`.
   */
  async pullChanges() {
    return {};
  },

  async trackRemainingChanges() {
    await this._bridge.syncFinished();
  },

  /**
   * Marks a record for a hard-`DELETE` at the end of the sync. The base method
   * also removes it from the tracker, but we don't use the tracker for that,
   * so we override the method to just mark.
   */
  _deleteId(id) {
    this._noteDeletedId(id);
  },

  /**
   * Always stage incoming records, bypassing the base engine's reconciliation
   * machinery.
   */
  async _reconcile() {
    return true;
  },

  async _syncStartup() {
    await super._syncStartup();
    await this._bridge.syncStarted();
  },

  async _processIncoming(newitems) {
    await super._processIncoming(newitems);

    // Pass the collection's server last-modified time (which the base engine
    // has just stored as our last sync time) so the engine can reconcile
    // against the real timestamp. `lastModified` is in seconds; the bridge
    // works in integer milliseconds.
    let serverModifiedMillis = Math.round((this.lastModified ?? 0) * 1000);
    let outgoingBsosAsJSON = await this._bridge.apply(serverModifiedMillis);
    let changeset = {};
    for (let bsoAsJSON of outgoingBsosAsJSON) {
      this._log.trace("outgoing bso", bsoAsJSON);
      let record = BridgedRecord.fromOutgoingBso(
        this.name,
        JSON.parse(bsoAsJSON)
      );
      changeset[record.id] = {
        synced: false,
        record,
      };
    }
    this._modified.replace(changeset);
  },

  /**
   * Notify the bridged engine that we've successfully uploaded a batch, so
   * that it can update its local state. For example, if the engine uses a
   * mirror and a temp table for outgoing records, it can write the uploaded
   * records from the outgoing table back to the mirror.
   */
  async _onRecordsWritten(succeeded, failed, serverModifiedTime) {
    // JS uses seconds but Rust uses milliseconds so we'll need to convert
    let serverModifiedMS = Math.round(serverModifiedTime * 1000);
    await this._bridge.setUploaded(Math.floor(serverModifiedMS), succeeded);
  },

  async _createTombstone() {
    throw new Error("Bridged engines don't support weak uploads");
  },

  async _createRecord(id) {
    let change = this._modified.changes[id];
    if (!change) {
      throw new TypeError("Can't create record for unchanged item");
    }
    return change.record;
  },

  async _resetClient() {
    await super._resetClient();
    await this._bridge.reset();
  },
};
Object.setPrototypeOf(BridgedEngine.prototype, SyncEngine.prototype);
