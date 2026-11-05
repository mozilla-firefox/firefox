/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * A worker dedicated to Remote Settings.
 */

// These files are imported into the worker scope, and are not shared singletons
// with the main thread.
/* eslint-disable mozilla/reject-import-system-module-from-non-system */
import { CanonicalJSON } from "resource://gre/modules/CanonicalJSON.sys.mjs";
import { IDBHelpers } from "resource://services-settings/IDBHelpers.sys.mjs";
import { SharedUtils } from "resource://services-settings/SharedUtils.sys.mjs";
import { jsesc } from "resource://gre/modules/third_party/jsesc/jsesc.mjs";
/* eslint-enable mozilla/reject-import-system-module-from-non-system */

const IDB_RECORDS_STORE = "records";
const IDB_TIMESTAMPS_STORE = "timestamps";

let gShutdown = false;

const Agent = {
  /**
   * Return the canonical JSON serialization of the specified records.
   * It has to match what is done on the server (See Kinto/kinto-signer).
   *
   * @param {Array<object>} records
   * @param {string} timestamp
   * @returns {string}
   */
  async canonicalStringify(records, timestamp) {
    // Sort list by record id.
    let allRecords = records.sort((a, b) => {
      if (a.id < b.id) {
        return -1;
      }
      return a.id > b.id ? 1 : 0;
    });
    // All existing records are replaced by the version from the server
    // and deleted records are removed.
    for (let i = 0; i < allRecords.length /* no increment! */; ) {
      const rec = allRecords[i];
      const next = allRecords[i + 1];
      if ((next && rec.id === next.id) || rec.deleted) {
        allRecords.splice(i, 1); // remove local record
      } else {
        i++;
      }
    }
    const toSerialize = {
      last_modified: "" + timestamp,
      data: allRecords,
    };
    return CanonicalJSON.stringify(toSerialize, jsesc);
  },

  /**
   * If present, import the JSON file into the Remote Settings IndexedDB
   * for the specified bucket and collection.
   * (eg. blocklists/certificates, main/onboarding)
   *
   * @param {string} bucket
   * @param {string} collection
   * @returns {int} Number of records loaded from dump or -1 if no dump found.
   */
  async importJSONDump(bucket, collection) {
    const { data: records, timestamp } = await SharedUtils.loadJSONDump(
      bucket,
      collection
    );
    if (records === null) {
      // Return -1 if file is missing.
      return -1;
    }
    if (gShutdown) {
      throw new Error("Can't import when we've started shutting down.");
    }
    await importDumpIDB(bucket, collection, records, timestamp);
    return records.length;
  },

  /**
   * Check that the specified file matches the expected size and SHA-256 hash.
   *
   * @param {string} fileUrl file URL to read from
   * @param {number} size expected file size
   * @param {string} size expected file SHA-256 as hex string
   * @returns {boolean}
   */
  async checkFileHash(fileUrl, size, hash) {
    let resp;
    try {
      resp = await fetch(fileUrl);
    } catch (e) {
      // File does not exist.
      return false;
    }
    const buffer = await resp.arrayBuffer();
    return SharedUtils.checkContentHash(buffer, size, hash);
  },

  async prepareShutdown() {
    gShutdown = true;
    // Ensure we can iterate and abort (which may delete items) by cloning
    // the list.
    let transactions = Array.from(gPendingTransactions);
    for (let transaction of transactions) {
      try {
        transaction.abort();
      } catch (ex) {
        // We can hit this case if the transaction has finished but
        // we haven't heard about it yet.
      }
    }
  },

  _test_only_import(bucket, collection, records, timestamp) {
    return importDumpIDB(bucket, collection, records, timestamp);
  },
};

/**
 * Wrap worker invocations in order to return the `callbackId` along
 * the result. This will allow to transform the worker invocations
 * into promises in `RemoteSettingsWorker.sys.mjs`.
 */
self.onmessage = event => {
  const { callbackId, method, args = [] } = event.data;
  Agent[method](...args)
    .then(result => {
      self.postMessage({ callbackId, result });
    })
    .catch(error => {
      console.log(`RemoteSettingsWorker error: ${error}`);
      self.postMessage({ callbackId, error: "" + error });
    });
};

let gPendingTransactions = new Set();

/**
 * Import the records into the Remote Settings Chrome IndexedDB.
 *
 * Note: This duplicates some logics from `kinto-offline-client.sys.mjs`.
 *
 * @param {string} bucket
 * @param {string} collection
 * @param {Array<object>} records
 * @param {number} timestamp
 */
async function importDumpIDB(bucket, collection, records, timestamp) {
  // Open the DB. It will exist since if we are running this, it means
  // we already tried to read the timestamp in `remote-settings.sys.mjs`
  const db = await IDBHelpers.openIDB(false /* do not allow upgrades */);

  // try...finally to ensure we always close the db.
  try {
    if (gShutdown) {
      throw new Error("Can't import when we've started shutting down.");
    }

    // Each entry of the dump will be stored in the records store.
    // They are indexed by `_cid`.
    const cid = bucket + "/" + collection;
    // We can just modify the items in-place, as we got them from SharedUtils.loadJSONDump().
    records.forEach(item => {
      item._cid = cid;
    });
    // Store the collection timestamp.
    let { transaction, promise } = IDBHelpers.executeIDB(
      db,
      [IDB_RECORDS_STORE, IDB_TIMESTAMPS_STORE],
      "readwrite",
      ([recordsStore, timestampStore], rejectTransaction) => {
        // Wipe before loading
        recordsStore.delete(IDBKeyRange.bound([cid], [cid, []], false, true));
        IDBHelpers.bulkOperationHelper(
          recordsStore,
          {
            reject: rejectTransaction,
            completion() {
              timestampStore.put({ cid, value: timestamp });
            },
          },
          "put",
          records
        );
      }
    );
    gPendingTransactions.add(transaction);
    promise = promise.finally(() => gPendingTransactions.delete(transaction));
    await promise;
  } finally {
    // Close now that we're done.
    db.close();
  }
}
