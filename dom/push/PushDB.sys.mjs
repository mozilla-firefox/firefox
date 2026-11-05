/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { IndexedDBHelper } from "moz-src:///dom/base/IndexedDBHelper.sys.mjs";

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    maxLogLevelPref: "dom.push.loglevel",
    logModule: "PushDB",
  });
});

export function PushDB(dbName, dbVersion, dbStoreName, keyPath, model) {
  lazy.console.debug("PushDB()");
  this._dbStoreName = dbStoreName;
  this._keyPath = keyPath;
  this._model = model;

  // set the indexeddb database
  this.initDBHelper(dbName, dbVersion, [dbStoreName]);
}

PushDB.prototype = {
  __proto__: IndexedDBHelper.prototype,

  toPushRecord(record) {
    if (!record) {
      return null;
    }
    return new this._model(record);
  },

  isValidRecord(record) {
    return (
      record &&
      typeof record.scope == "string" &&
      typeof record.originAttributes == "string" &&
      record.quota >= 0 &&
      typeof record[this._keyPath] == "string"
    );
  },

  upgradeSchema(aTransaction, aDb, aOldVersion) {
    if (aOldVersion <= 3) {
      // XXXnsm We haven't shipped Push during this upgrade, so I'm just going to throw old
      // registrations away without even informing the app.
      if (aDb.objectStoreNames.contains(this._dbStoreName)) {
        aDb.deleteObjectStore(this._dbStoreName);
      }

      let objectStore = aDb.createObjectStore(this._dbStoreName, {
        keyPath: this._keyPath,
      });

      // index to fetch records based on endpoints. used by unregister
      objectStore.createIndex("pushEndpoint", "pushEndpoint", { unique: true });

      // index to fetch records by identifiers.
      // In the current security model, the originAttributes distinguish between
      // different 'apps' on the same origin. Since ServiceWorkers are
      // same-origin to the scope they are registered for, the attributes and
      // scope are enough to reconstruct a valid principal.
      objectStore.createIndex("identifiers", ["scope", "originAttributes"], {
        unique: true,
      });
      objectStore.createIndex("originAttributes", "originAttributes", {
        unique: false,
      });
    }

    if (aOldVersion < 4) {
      let objectStore = aTransaction.objectStore(this._dbStoreName);

      // index to fetch active and expired registrations.
      objectStore.createIndex("quota", "quota", { unique: false });
    }
  },

  /**
   * @param aRecord
   *        The record to be added.
   */

  put(aRecord) {
    lazy.console.debug("put()", aRecord);
    if (!this.isValidRecord(aRecord)) {
      return Promise.reject(
        new TypeError(
          "Scope, originAttributes, and quota are required! " +
            JSON.stringify(aRecord)
        )
      );
    }

    return new Promise((resolve, reject) =>
      this.newTxn(
        "readwrite",
        this._dbStoreName,
        (aTxn, aStore) => {
          aTxn.result = undefined;

          aStore.put(aRecord).onsuccess = aEvent => {
            lazy.console.debug(
              "put: Request successful. Updated record",
              aEvent.target.result
            );
            aTxn.result = this.toPushRecord(aRecord);
          };
        },
        resolve,
        reject
      )
    );
  },

  /**
   * @param aKeyID
   *        The ID of record to be deleted.
   */
  delete(aKeyID) {
    lazy.console.debug("delete()");

    return new Promise((resolve, reject) =>
      this.newTxn(
        "readwrite",
        this._dbStoreName,
        (aTxn, aStore) => {
          lazy.console.debug("delete: Removing record", aKeyID);
          aStore.get(aKeyID).onsuccess = event => {
            aTxn.result = this.toPushRecord(event.target.result);
            aStore.delete(aKeyID);
          };
        },
        resolve,
        reject
      )
    );
  },

  // testFn(record) is called with a database record and should return true if
  // that record should be deleted.
  clearIf(testFn) {
    lazy.console.debug("clearIf()");
    return new Promise((resolve, reject) =>
      this.newTxn(
        "readwrite",
        this._dbStoreName,
        (aTxn, aStore) => {
          aTxn.result = undefined;

          aStore.openCursor().onsuccess = event => {
            let cursor = event.target.result;
            if (cursor) {
              let record = this.toPushRecord(cursor.value);
              if (testFn(record)) {
                let deleteRequest = cursor.delete();
                deleteRequest.onerror = e => {
                  lazy.console.error(
                    "clearIf: Error removing record",
                    record.keyID,
                    e
                  );
                };
              }
              cursor.continue();
            }
          };
        },
        resolve,
        reject
      )
    );
  },

  getByPushEndpoint(aPushEndpoint) {
    lazy.console.debug("getByPushEndpoint()");

    return new Promise((resolve, reject) =>
      this.newTxn(
        "readonly",
        this._dbStoreName,
        (aTxn, aStore) => {
          aTxn.result = undefined;

          let index = aStore.index("pushEndpoint");
          index.get(aPushEndpoint).onsuccess = aEvent => {
            let record = this.toPushRecord(aEvent.target.result);
            lazy.console.debug("getByPushEndpoint: Got record", record);
            aTxn.result = record;
          };
        },
        resolve,
        reject
      )
    );
  },

  getByKeyID(aKeyID) {
    lazy.console.debug("getByKeyID()");

    return new Promise((resolve, reject) =>
      this.newTxn(
        "readonly",
        this._dbStoreName,
        (aTxn, aStore) => {
          aTxn.result = undefined;

          aStore.get(aKeyID).onsuccess = aEvent => {
            let record = this.toPushRecord(aEvent.target.result);
            lazy.console.debug("getByKeyID: Got record", record);
            aTxn.result = record;
          };
        },
        resolve,
        reject
      )
    );
  },

  /**
   * Iterates over all records associated with an origin.
   *
   * @param {string} origin The origin, matched as a prefix against the scope.
   * @param {string} originAttributes Additional origin attributes. Requires
   *  an exact match.
   * @param {Function} callback A function with the signature `(record,
   *  cursor)`, called for each record. `record` is the registration, and
   *  `cursor` is an `IDBCursor`.
   * @returns {Promise} Resolves once all records have been processed.
   */
  async forEachOrigin(origin, originAttributes, callback) {
    lazy.console.debug("forEachOrigin()");
    let error;
    await new Promise((resolve, reject) =>
      this.newTxn(
        "readwrite",
        this._dbStoreName,
        (aTxn, aStore) => {
          aTxn.result = undefined;

          let index = aStore.index("identifiers");
          let range = IDBKeyRange.bound(
            [origin, originAttributes],
            [origin + "\x7f", originAttributes]
          );
          index.openCursor(range).onsuccess = event => {
            let cursor = event.target.result;
            if (!cursor) {
              return;
            }
            try {
              callback(this.toPushRecord(cursor.value), cursor);
            } catch (e) {
              // If the callback throws, still continue with other records.
              lazy.console.error(`forEachOrigin() callback: ${e}`);
              if (!error) {
                error = e;
              }
            }
            cursor.continue();
          };
        },
        resolve,
        reject
      )
    );
    if (error) {
      throw error;
    }
  },

  // Perform a unique match against { scope, originAttributes }
  getByIdentifiers(aPageRecord) {
    lazy.console.debug("getByIdentifiers()", aPageRecord);
    if (!aPageRecord.scope || aPageRecord.originAttributes == undefined) {
      lazy.console.error(
        "getByIdentifiers: Scope and originAttributes are required",
        aPageRecord
      );
      return Promise.reject(new TypeError("Invalid page record"));
    }

    return new Promise((resolve, reject) =>
      this.newTxn(
        "readonly",
        this._dbStoreName,
        (aTxn, aStore) => {
          aTxn.result = undefined;

          let index = aStore.index("identifiers");
          let request = index.get(
            IDBKeyRange.only([aPageRecord.scope, aPageRecord.originAttributes])
          );
          request.onsuccess = aEvent => {
            aTxn.result = this.toPushRecord(aEvent.target.result);
          };
        },
        resolve,
        reject
      )
    );
  },

  _getAllByKey(aKeyName, aKeyValue) {
    return new Promise((resolve, reject) =>
      this.newTxn(
        "readonly",
        this._dbStoreName,
        (aTxn, aStore) => {
          aTxn.result = undefined;

          let index = aStore.index(aKeyName);
          // It seems ok to use getAll here, since unlike contacts or other
          // high storage APIs, we don't expect more than a handful of
          // registrations per domain, and usually only one.
          let getAllReq = index.getAll(aKeyValue);
          getAllReq.onsuccess = aEvent => {
            aTxn.result = aEvent.target.result.map(record =>
              this.toPushRecord(record)
            );
          };
        },
        resolve,
        reject
      )
    );
  },

  // aOriginAttributes must be a string!
  getAllByOriginAttributes(aOriginAttributes) {
    if (typeof aOriginAttributes !== "string") {
      return Promise.reject("Expected string!");
    }
    return this._getAllByKey("originAttributes", aOriginAttributes);
  },

  getAllKeyIDs() {
    lazy.console.debug("getAllKeyIDs()");

    return new Promise((resolve, reject) =>
      this.newTxn(
        "readonly",
        this._dbStoreName,
        (aTxn, aStore) => {
          aTxn.result = undefined;
          aStore.getAll().onsuccess = event => {
            aTxn.result = event.target.result.map(record =>
              this.toPushRecord(record)
            );
          };
        },
        resolve,
        reject
      )
    );
  },

  _getAllByPushQuota(range) {
    lazy.console.debug("getAllByPushQuota()");

    return new Promise((resolve, reject) =>
      this.newTxn(
        "readonly",
        this._dbStoreName,
        (aTxn, aStore) => {
          aTxn.result = [];

          let index = aStore.index("quota");
          index.openCursor(range).onsuccess = event => {
            let cursor = event.target.result;
            if (cursor) {
              aTxn.result.push(this.toPushRecord(cursor.value));
              cursor.continue();
            }
          };
        },
        resolve,
        reject
      )
    );
  },

  getAllUnexpired() {
    lazy.console.debug("getAllUnexpired()");
    return this._getAllByPushQuota(IDBKeyRange.lowerBound(1));
  },

  getAllExpired() {
    lazy.console.debug("getAllExpired()");
    return this._getAllByPushQuota(IDBKeyRange.only(0));
  },

  /**
   * Updates an existing push registration.
   *
   * @param {string} aKeyID The registration ID.
   * @param {Function} aUpdateFunc A function that receives the existing
   *  registration record as its argument, and returns a new record.
   * @returns {Promise} A promise resolved with either the updated record.
   *  Rejects if the record does not exist, or the function returns an invalid
   *  record.
   */
  update(aKeyID, aUpdateFunc) {
    return new Promise((resolve, reject) =>
      this.newTxn(
        "readwrite",
        this._dbStoreName,
        (aTxn, aStore) => {
          aStore.get(aKeyID).onsuccess = aEvent => {
            aTxn.result = undefined;

            let record = aEvent.target.result;
            if (!record) {
              throw new Error("Record " + aKeyID + " does not exist");
            }
            let newRecord = aUpdateFunc(this.toPushRecord(record));
            if (!this.isValidRecord(newRecord)) {
              lazy.console.error(
                "update: Ignoring invalid update",
                aKeyID,
                newRecord
              );
              throw new Error("Invalid update for record " + aKeyID);
            }
            function putRecord() {
              let req = aStore.put(newRecord);
              req.onsuccess = () => {
                lazy.console.debug(
                  "update: Update successful",
                  aKeyID,
                  newRecord
                );
                aTxn.result = newRecord;
              };
            }
            if (aKeyID === newRecord.keyID) {
              putRecord();
            } else {
              // If we changed the primary key, delete the old record to avoid
              // unique constraint errors.
              aStore.delete(aKeyID).onsuccess = putRecord;
            }
          };
        },
        resolve,
        reject
      )
    );
  },

  drop() {
    lazy.console.debug("drop()");

    return new Promise((resolve, reject) =>
      this.newTxn(
        "readwrite",
        this._dbStoreName,
        function txnCb(aTxn, aStore) {
          aStore.clear();
        },
        resolve,
        reject
      )
    );
  },
};
