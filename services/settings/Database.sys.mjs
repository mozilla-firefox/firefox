/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  CommonUtils: "resource://services-common/utils.sys.mjs",
  IDBHelpers: "resource://services-settings/IDBHelpers.sys.mjs",
  ObjectUtils: "resource://gre/modules/ObjectUtils.sys.mjs",
  Utils: "resource://services-settings/Utils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", () => lazy.Utils.log);

const DELETE_CHUNK_SIZE = 50;

class EmptyDatabaseError extends Error {
  constructor(cid) {
    super(`"${cid}" has not been synced yet`);
    this.name = "EmptyDatabaseError";
  }
}

/**
 * Database is a tiny wrapper with the objective
 * of providing major kinto-offline-client collection API.
 * (with the objective of getting rid of kinto-offline-client)
 */
export class Database {
  static get EmptyDatabaseError() {
    return EmptyDatabaseError;
  }

  static destroy() {
    return destroyIDB();
  }

  constructor(identifier) {
    ensureShutdownBlocker();
    this.identifier = identifier;
  }

  async list(options = {}) {
    const { filters = {}, order = "" } = options;
    let results = [];
    let timestamp = null;
    try {
      await executeIDB(
        ["timestamps", "records"],
        (stores, rejectTransaction) => {
          const [storeTimestamps, storeRecords] = stores;
          // Since we cannot distinguish an empty collection from
          // a collection that has not been synced yet, we also
          // retrieve the timestamp for the collection.
          storeTimestamps.get(this.identifier).onsuccess = evt =>
            (timestamp = evt.target.result);

          // Fast-path the (very common) no-filters case
          if (lazy.ObjectUtils.isEmpty(filters)) {
            const range = IDBKeyRange.only(this.identifier);
            const request = storeRecords.index("cid").getAll(range);
            request.onsuccess = e => {
              results = e.target.result;
            };
            return;
          }
          // If we have filters, we need to iterate over the records
          // and apply the filters to each record using a cursor.
          const request = storeRecords
            .index("cid")
            .openCursor(IDBKeyRange.only(this.identifier));
          const objFilters = transformSubObjectFilters(filters);
          request.onsuccess = event => {
            try {
              const cursor = event.target.result;
              if (cursor) {
                const { value } = cursor;
                if (lazy.Utils.filterObject(objFilters, value)) {
                  results.push(value);
                }
                cursor.continue();
              }
            } catch (ex) {
              rejectTransaction(ex);
            }
          };
        },
        { mode: "readonly" }
      );
    } catch (e) {
      throw new lazy.IDBHelpers.IndexedDBError(e, "list()", this.identifier);
    }
    // Throw an error if consumer tries to list records of a collection
    // that has not been synced yet.
    if (results.length === 0 && !timestamp) {
      throw new EmptyDatabaseError(this.identifier);
    }
    // Remove IDB key field from results.
    for (const result of results) {
      delete result._cid;
    }
    return order ? lazy.Utils.sortObjects(order, results) : results;
  }

  async importChanges(metadata, timestamp, records = [], options = {}) {
    const { clear = false } = options;
    const _cid = this.identifier;
    try {
      await executeIDB(
        ["collections", "timestamps", "records"],
        (stores, rejectTransaction) => {
          const [storeMetadata, storeTimestamps, storeRecords] = stores;

          if (clear) {
            // Our index is over the _cid and id fields. We want to remove
            // all of the items in the collection for which the object was
            // created, ie with _cid == this.identifier.
            // We would like to just tell IndexedDB:
            // store.index(IDBKeyRange.only(this.identifier)).delete();
            // to delete all records matching the first part of the 2-part key.
            // Unfortunately such an API does not exist.
            // While we could iterate over the index with a cursor, we'd do
            // a roundtrip to PBackground for each item. Once you have 1000
            // items, the result is very slow because of all the overhead of
            // jumping between threads and serializing/deserializing.
            // So instead, we tell the store to delete everything between
            // "our" _cid identifier, and what would be the next identifier
            // (via lexicographical sorting). Unfortunately there does not
            // seem to be a way to specify bounds for all items that share
            // the same first part of the key using just that first part, hence
            // the use of the hypothetical [] for the second part of the end of
            // the bounds.
            storeRecords.delete(
              IDBKeyRange.bound([_cid], [_cid, []], false, true)
            );
          }

          // Store or erase metadata.
          if (metadata === null) {
            storeMetadata.delete(_cid);
          } else if (metadata) {
            storeMetadata.put({ cid: _cid, metadata });
          }
          // Store or erase timestamp.
          if (timestamp === null) {
            storeTimestamps.delete(_cid);
          } else if (timestamp) {
            storeTimestamps.put({ cid: _cid, value: timestamp });
          }

          if (!records.length) {
            return;
          }

          // Separate tombstones from creations/updates.
          const toDelete = records.filter(r => r.deleted);
          const toInsert = records.filter(r => !r.deleted);
          lazy.console.debug(
            `${_cid} ${toDelete.length} to delete, ${toInsert.length} to insert`
          );
          // Delete local records for each tombstone.
          lazy.IDBHelpers.bulkOperationHelper(
            storeRecords,
            {
              reject: rejectTransaction,
              completion() {
                // Overwrite all other data.
                lazy.IDBHelpers.bulkOperationHelper(
                  storeRecords,
                  {
                    reject: rejectTransaction,
                  },
                  "put",
                  toInsert.map(item => ({ ...item, _cid }))
                );
              },
            },
            "delete",
            toDelete.map(item => [_cid, item.id])
          );
        },
        { desc: "importChanges() in " + _cid }
      );
    } catch (e) {
      throw new lazy.IDBHelpers.IndexedDBError(e, "importChanges()", _cid);
    }
  }

  async getLastModified() {
    let entry = null;
    try {
      await executeIDB(
        "timestamps",
        store => {
          store.get(this.identifier).onsuccess = e => (entry = e.target.result);
        },
        { mode: "readonly" }
      );
    } catch (e) {
      throw new lazy.IDBHelpers.IndexedDBError(
        e,
        "getLastModified()",
        this.identifier
      );
    }
    if (!entry) {
      return null;
    }
    // Some distributions where released with a modified dump that did not
    // contain timestamps for last_modified. Work around this here, and return
    // the timestamp as zero, so that the entries should get updated.
    if (isNaN(entry.value)) {
      lazy.console.warn(`Local timestamp is NaN for ${this.identifier}`);
      return 0;
    }
    return entry.value;
  }

  async getMetadata() {
    let entry = null;
    try {
      await executeIDB(
        "collections",
        store => {
          store.get(this.identifier).onsuccess = e => (entry = e.target.result);
        },
        { mode: "readonly" }
      );
    } catch (e) {
      throw new lazy.IDBHelpers.IndexedDBError(
        e,
        "getMetadata()",
        this.identifier
      );
    }
    return entry ? entry.metadata : null;
  }

  async getAttachment(attachmentId) {
    let entry = null;
    try {
      await executeIDB(
        "attachments",
        store => {
          store.get([this.identifier, attachmentId]).onsuccess = e => {
            entry = e.target.result;
          };
        },
        { mode: "readonly" }
      );
    } catch (e) {
      throw new lazy.IDBHelpers.IndexedDBError(
        e,
        "getAttachment()",
        this.identifier
      );
    }
    return entry ? entry.attachment : null;
  }

  async saveAttachment(attachmentId, attachment) {
    return await this.saveAttachments([[attachmentId, attachment]]);
  }

  async saveAttachments(idsAndBlobs) {
    try {
      await executeIDB(
        "attachments",
        store => {
          for (const [attachmentId, attachment] of idsAndBlobs) {
            if (attachment) {
              store.put({ cid: this.identifier, attachmentId, attachment });
            } else {
              store.delete([this.identifier, attachmentId]);
            }
          }
        },
        {
          desc:
            "saveAttachments(<" +
            idsAndBlobs.length +
            " items>) in " +
            this.identifier,
        }
      );
    } catch (e) {
      throw new lazy.IDBHelpers.IndexedDBError(
        e,
        "saveAttachments()",
        this.identifier
      );
    }
  }

  /**
   * Delete attachments in small transactions, so that a shutdown only has to
   * wait for the current one. Rejects once shutdown has started.
   *
   * @param {Array<string>} attachmentIds
   */
  async deleteAttachments(attachmentIds) {
    try {
      for (let i = 0; i < attachmentIds.length; i += DELETE_CHUNK_SIZE) {
        if (Services.startup.shuttingDown) {
          throw new lazy.IDBHelpers.ShutdownError(
            "The application is shutting down",
            "deleteAttachments()"
          );
        }
        const chunk = attachmentIds.slice(i, i + DELETE_CHUNK_SIZE);
        await executeIDB(
          "attachments",
          store => {
            for (const attachmentId of chunk) {
              store.delete([this.identifier, attachmentId]);
            }
          },
          {
            desc: `deleteAttachments(<${chunk.length} items>) in ${this.identifier}`,
          }
        );
      }
    } catch (e) {
      throw new lazy.IDBHelpers.IndexedDBError(
        e,
        "deleteAttachments()",
        this.identifier
      );
    }
  }

  async hasAttachments() {
    let count = 0;
    try {
      const range = IDBKeyRange.bound(
        [this.identifier],
        [this.identifier, []],
        false,
        true
      );
      await executeIDB(
        "attachments",
        store => {
          store.count(range).onsuccess = e => {
            count = e.target.result;
          };
        },
        { mode: "readonly" }
      );
    } catch (e) {
      throw new lazy.IDBHelpers.IndexedDBError(
        e,
        "hasAttachments()",
        this.identifier
      );
    }
    return count > 0;
  }

  /**
   * Delete all attachments which don't match any record.
   *
   * Attachments are linked to records, except when a fixed `attachmentId` is used.
   * A record can be updated or deleted, potentially by deleting a record and restoring an updated version
   * of the record with the same ID. Potentially leaving orphaned attachments in the database.
   * Since we run the pruning logic after syncing, any attachment without a
   * matching record can be discarded as they will be unreachable forever.
   *
   * @param {Array<string>} excludeIds List of attachments IDs to exclude from pruning.
   */
  async pruneAttachments(excludeIds) {
    const _cid = this.identifier;
    let deletedCount = 0;
    try {
      await executeIDB(
        ["attachments", "records"],
        async (stores, rejectTransaction) => {
          const [attachmentsStore, recordsStore] = stores;

          // List all stored attachments.
          // All keys ≥ [_cid, ..] && < [_cid, []]. See comment in `importChanges()`
          const rangeAllKeys = IDBKeyRange.bound(
            [_cid],
            [_cid, []],
            false,
            true
          );
          const allAttachments = await new Promise((resolve, reject) => {
            const request = attachmentsStore.getAll(rangeAllKeys);
            request.onsuccess = e => resolve(e.target.result);
            request.onerror = e => reject(e);
          });
          if (!allAttachments.length) {
            lazy.console.debug(
              `${this.identifier} No attachments in IDB cache. Nothing to do.`
            );
            return;
          }

          // List all stored records.
          const allRecords = await new Promise((resolve, reject) => {
            const rangeAllIndexed = IDBKeyRange.only(_cid);
            const request = recordsStore.index("cid").getAll(rangeAllIndexed);
            request.onsuccess = e => resolve(e.target.result);
            request.onerror = e => reject(e);
          });

          // Compare known records IDs to those stored along the attachments.
          const currentRecordsIDs = new Set(allRecords.map(r => r.id));
          const attachmentsToDelete = allAttachments.reduce((acc, entry) => {
            // Skip excluded attachments.
            if (excludeIds.includes(entry.attachmentId)) {
              return acc;
            }
            // Delete attachment if associated record does not exist.
            if (!currentRecordsIDs.has(entry.attachment.record.id)) {
              acc.push([_cid, entry.attachmentId]);
            }
            return acc;
          }, []);

          // Perform a bulk delete of all obsolete attachments.
          lazy.console.debug(
            `${this.identifier} Bulk delete ${attachmentsToDelete.length} obsolete attachments`
          );
          lazy.IDBHelpers.bulkOperationHelper(
            attachmentsStore,
            {
              reject: rejectTransaction,
            },
            "delete",
            attachmentsToDelete
          );
          deletedCount = attachmentsToDelete.length;
        },
        { desc: "pruneAttachments() in " + this.identifier }
      );
    } catch (e) {
      throw new lazy.IDBHelpers.IndexedDBError(
        e,
        "pruneAttachments()",
        this.identifier
      );
    }
    return deletedCount;
  }

  async clear() {
    try {
      await this.importChanges(null, null, [], { clear: true });
    } catch (e) {
      throw new lazy.IDBHelpers.IndexedDBError(e, "clear()", this.identifier);
    }
  }

  /*
   * Methods used by unit tests.
   */

  async create(record) {
    if (!("id" in record)) {
      record = { ...record, id: lazy.CommonUtils.generateUUID() };
    }
    try {
      await executeIDB(
        "records",
        store => {
          store.add({ ...record, _cid: this.identifier });
        },
        { desc: "create() in " + this.identifier }
      );
    } catch (e) {
      throw new lazy.IDBHelpers.IndexedDBError(e, "create()", this.identifier);
    }
    return record;
  }

  async update(record) {
    try {
      await executeIDB(
        "records",
        store => {
          store.put({ ...record, _cid: this.identifier });
        },
        { desc: "update() in " + this.identifier }
      );
    } catch (e) {
      throw new lazy.IDBHelpers.IndexedDBError(e, "update()", this.identifier);
    }
  }

  async delete(recordId) {
    try {
      await executeIDB(
        "records",
        store => {
          store.delete([this.identifier, recordId]); // [_cid, id]
        },
        { desc: "delete() in " + this.identifier }
      );
    } catch (e) {
      throw new lazy.IDBHelpers.IndexedDBError(e, "delete()", this.identifier);
    }
  }
}

let gDB = null;
let gDBPromise = null;

/**
 * This function attempts to ensure `gDB` points to a valid database value.
 * If gDB is already a database, it will do no-op (but this may take a
 * microtask or two).
 * If opening the database fails, it will throw an IndexedDBError.
 */
async function openIDB() {
  // We can be called multiple times in a race; always ensure that when
  // we complete, `gDB` is no longer null, but avoid doing the actual
  // IndexedDB work more than once.
  if (!gDBPromise) {
    // Open and initialize/upgrade if needed.
    gDBPromise = lazy.IDBHelpers.openIDB();
  }
  let db = await gDBPromise;
  if (!gDB) {
    gDB = db;
  }
}

const gPendingReadOnlyTransactions = new Set();
const gPendingWriteOperations = new Set();
/**
 * Helper to wrap some IDBObjectStore operations into a promise.
 *
 * @param {IDBDatabase} db
 * @param {string | string[]} storeNames - either a string or an array of strings.
 * @param {function} callback
 * @param {object} options
 * @param {string} options.mode
 * @param {string} options.desc   for shutdown tracking.
 */
async function executeIDB(storeNames, callback, options = {}) {
  if (!gDB) {
    // Check if we're shutting down. Services.startup.shuttingDown will
    // be true sooner, but is never true in xpcshell tests, so we check
    // both that and a bool we set ourselves when `profile-before-change`
    // starts.
    if (gShutdownStarted || Services.startup.shuttingDown) {
      throw new lazy.IDBHelpers.ShutdownError(
        "The application is shutting down",
        "execute()"
      );
    }
    await openIDB();
  } else {
    // Even if we have a db, wait a tick to avoid making IndexedDB sad.
    // We should be able to remove this once bug 1626935 is fixed.
    await Promise.resolve();
  }

  // Check for shutdown again as we've await'd something...
  if (!gDB && (gShutdownStarted || Services.startup.shuttingDown)) {
    throw new lazy.IDBHelpers.ShutdownError(
      "The application is shutting down",
      "execute()"
    );
  }

  // Start the actual transaction:
  const { mode = "readwrite", desc = "" } = options;
  let { promise, transaction } = lazy.IDBHelpers.executeIDB(
    gDB,
    storeNames,
    mode,
    callback,
    desc
  );

  // We track all readonly transactions and abort them at shutdown.
  // We track all readwrite ones and await their completion at shutdown
  // (to avoid dataloss when writes fail).
  // We use a `.finally()` clause for this; it'll run the function irrespective
  // of whether the promise resolves or rejects, and the promise it returns
  // will resolve/reject with the same value.
  let finishedFn;
  if (mode == "readonly") {
    gPendingReadOnlyTransactions.add(transaction);
    finishedFn = () => gPendingReadOnlyTransactions.delete(transaction);
  } else {
    let obj = { promise, desc };
    gPendingWriteOperations.add(obj);
    finishedFn = () => gPendingWriteOperations.delete(obj);
  }
  return promise.finally(finishedFn);
}

async function destroyIDB() {
  if (gDB) {
    if (gShutdownStarted || Services.startup.shuttingDown) {
      throw new lazy.IDBHelpers.ShutdownError(
        "The application is shutting down",
        "destroyIDB()"
      );
    }

    // This will return immediately; the actual close will happen once
    // there are no more running transactions.
    gDB.close();
    const allTransactions = new Set([
      ...gPendingWriteOperations,
      ...gPendingReadOnlyTransactions,
    ]);
    for (let transaction of Array.from(allTransactions)) {
      try {
        transaction.abort();
      } catch (ex) {
        // Ignore errors to abort transactions, we'll destroy everything.
      }
    }
  }
  gDB = null;
  gDBPromise = null;
  return lazy.IDBHelpers.destroyIDB();
}

function makeNestedObjectFromArr(arr, val, nestedFiltersObj) {
  const last = arr.length - 1;
  return arr.reduce((acc, cv, i) => {
    if (i === last) {
      return (acc[cv] = val);
    } else if (Object.prototype.hasOwnProperty.call(acc, cv)) {
      return acc[cv];
    }
    return (acc[cv] = {});
  }, nestedFiltersObj);
}

function transformSubObjectFilters(filtersObj) {
  const transformedFilters = {};
  for (const [key, val] of Object.entries(filtersObj)) {
    const keysArr = key.split(".");
    makeNestedObjectFromArr(keysArr, val, transformedFilters);
  }
  return transformedFilters;
}

// We need to expose this wrapper function so we can test
// shutdown handling.
Database._executeIDB = executeIDB;

let gShutdownStarted = false;
// Test-only helper to be able to test shutdown multiple times:
Database._cancelShutdown = () => {
  gShutdownStarted = false;
};

let gShutdownBlocker = false;
Database._shutdownHandler = () => {
  gShutdownStarted = true;
  // Duplicate the list (to avoid it being modified) and then
  // abort all read-only transactions.
  for (let transaction of Array.from(gPendingReadOnlyTransactions)) {
    try {
      transaction.abort();
    } catch (ex) {
      // Ensure we don't throw/break, because either way we're in shutdown.

      // In particular, `transaction.abort` can throw if the transaction
      // is complete, ie if we manage to get called in between the
      // transaction completing, and our completion handler being called
      // to remove the item from the set. We don't care about that.
      if (ex.result != Cr.NS_ERROR_DOM_INDEXEDDB_NOT_ALLOWED_ERR) {
        // Report any other errors:
        console.error(ex);
      }
    }
  }
  if (gDB) {
    // This will return immediately; the actual close will happen once
    // there are no more running transactions.
    gDB.close();
    gDB = null;
  }
  gDBPromise = null;
  return Promise.allSettled(
    Array.from(gPendingWriteOperations).map(op => op.promise)
  );
};

function ensureShutdownBlocker() {
  if (gShutdownBlocker) {
    return;
  }
  gShutdownBlocker = true;
  lazy.AsyncShutdown.profileBeforeChange.addBlocker(
    "RemoteSettingsClient - finish IDB access.",
    Database._shutdownHandler,
    {
      fetchState() {
        return Array.from(gPendingWriteOperations).map(op => op.desc);
      },
    }
  );
}
