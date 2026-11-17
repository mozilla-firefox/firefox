/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  IndexedDB: "resource://gre/modules/IndexedDB.sys.mjs",
  ProfilesDatastoreService:
    "moz-src:///toolkit/profile/ProfilesDatastoreService.sys.mjs",
  ASRouterPreferences:
    "resource:///modules/asrouter/ASRouterPreferences.sys.mjs",
});

export class ASRouterStorage {
  // Tracks in-flight IDB write promises so the shutdown blocker can await them.
  #pendingWrites = new Set();

  /**
   * @param storeNames Array of strings used to create all the required stores
   */
  constructor({ storeNames, telemetry }) {
    if (!storeNames) {
      throw new Error("storeNames required");
    }

    this.dbName = "ActivityStream";
    this.dbVersion = 3;
    this.storeNames = storeNames;
    this.telemetry = telemetry;
  }

  get pendingWriteCount() {
    return this.#pendingWrites.size;
  }

  get db() {
    if (!this._db) {
      this._db = this.createOrOpenDb().catch(e => {
        this._db = null;
        throw e;
      });
    }
    return this._db;
  }

  _trackedSet(storeName, key, value) {
    const p = this._set(storeName, key, value).finally(() =>
      this.#pendingWrites.delete(p)
    );
    p.catch(() => {});
    this.#pendingWrites.add(p);
    return p;
  }

  flush() {
    return Promise.allSettled(this.#pendingWrites);
  }

  /**
   * Public method that binds the store required by the consumer and exposes
   * the private db getters and setters.
   *
   * @param storeName String name of desired store
   */
  getDbTable(storeName) {
    if (this.storeNames.includes(storeName)) {
      return {
        get: this._get.bind(this, storeName),
        getAll: this._getAll.bind(this, storeName),
        getAllKeys: this._getAllKeys.bind(this, storeName),
        set: (key, value) => {
          this._trackedSet(storeName, key, value);
        },
        // Like set, but returns a promise that rejects if the write fails.
        setWithResult: this._trackedSet.bind(this, storeName),
        getSharedMessageImpressions:
          this.getSharedMessageImpressions.bind(this),
        getSharedMessageBlocklist: this.getSharedMessageBlocklist.bind(this),
        setSharedMessageImpressions:
          this.setSharedMessageImpressions.bind(this),
        setSharedMessageBlocked: this.setSharedMessageBlocked.bind(this),
        resetSharedMessageStorage: this.resetSharedMessageStorage.bind(this),
      };
    }

    throw new Error(`Store name ${storeName} does not exist.`);
  }

  async _getStore(storeName, mode = "readonly") {
    return (await this.db).objectStore(storeName, mode);
  }

  _get(storeName, key) {
    return this._requestWrapper(async () =>
      (await this._getStore(storeName)).get(key)
    );
  }

  _getAll(storeName) {
    return this._requestWrapper(async () =>
      (await this._getStore(storeName)).getAll()
    );
  }

  _getAllKeys(storeName) {
    return this._requestWrapper(async () =>
      (await this._getStore(storeName)).getAllKeys()
    );
  }

  async _getTransaction(storeName, mode) {
    return (await this.db).transaction([storeName], mode);
  }

  _set(storeName, key, value) {
    return this._requestWrapper(async () => {
      const transaction = await this._getTransaction(storeName, "readwrite");
      // Wait for the commit too, so a write that is accepted but fails to
      // commit is reported as a failure.
      const [result] = await Promise.all([
        transaction.objectStore(storeName).put(value, key),
        transaction.promiseComplete(),
      ]);
      return result;
    });
  }

  _openDatabase() {
    return lazy.IndexedDB.open(this.dbName, this.dbVersion, db => {
      // If provided with array of objectStore names we need to create all the
      // individual stores.
      // createObjectStore is synchronous (returns IDBObjectStore, not
      // IDBRequest), so we must not wrap it with the async _requestWrapper:
      // its returned Promise would never be awaited inside this synchronous
      // callback, silently swallowing any error instead of letting it abort
      // the version-change transaction.
      this.storeNames.forEach(store => {
        if (!db.objectStoreNames.contains(store)) {
          db.createObjectStore(store);
        }
      });
    });
  }

  /**
   * Open a db (with this.dbName) if it exists. If it does not exist, create it.
   * If an error occurs, deleted the db and attempt to re-create it.
   *
   * @returns Promise that resolves with a db instance
   */
  async createOrOpenDb() {
    try {
      const db = await this._openDatabase();
      return this._registerLifecycleHandlers(db);
    } catch (e) {
      if (this.telemetry) {
        this.telemetry.handleUndesiredEvent({ event: "INDEXEDDB_OPEN_FAILED" });
      }
      try {
        await lazy.IndexedDB.deleteDatabase(this.dbName);
      } catch (deleteErr) {
        if (this.telemetry) {
          this.telemetry.handleUndesiredEvent({
            event: "INDEXEDDB_DELETE_FAILED",
          });
        }
      }
      const db = await this._openDatabase();
      return this._registerLifecycleHandlers(db);
    }
  }

  // Register event handlers on a newly opened database connection so that
  // external lifecycle events (version upgrades from other connections, or
  // the backend closing the connection due to storage pressure/corruption)
  // clear the cached _db promise and allow the next access to re-open.
  _registerLifecycleHandlers(db) {
    db.onversionchange = () => {
      db.close();
      this._db = null;
    };
    db.onclose = () => {
      this._db = null;
    };
    return db;
  }

  async _requestWrapper(request) {
    let result = null;
    try {
      result = await request();
    } catch (e) {
      if (this.telemetry) {
        this.telemetry.handleUndesiredEvent({ event: "TRANSACTION_FAILED" });
      }
      throw e;
    }

    return result;
  }

  /**
   * Gets all of the message impression data
   *
   * @returns {object|null} All multiprofile message impressions or null if error occurs
   */
  async getSharedMessageImpressions() {
    try {
      const conn = await lazy.ProfilesDatastoreService.getConnection();
      if (!conn) {
        return null;
      }
      const rows = await conn.executeCached(
        `SELECT messageId, json(impressions) AS impressions FROM MessagingSystemMessageImpressions;`
      );

      if (rows.length === 0) {
        return null;
      }

      const impressionsData = {};

      for (const row of rows) {
        const messageId = row.getResultByName("messageId");
        const impressions = JSON.parse(row.getResultByName("impressions"));

        impressionsData[messageId] = impressions;
      }

      return impressionsData;
    } catch (e) {
      lazy.ASRouterPreferences.console.error(
        `ASRouterStorage: Failed reading from MessagingSystemMessageImpressions`,
        e
      );
      if (this.telemetry) {
        this.telemetry.handleUndesiredEvent({
          event: "SHARED_DB_READ_FAILED",
        });
      }
      return null;
    }
  }

  /**
   * Gets the message blocklist
   *
   * @returns {Array|null} The message blocklist, or null if error occurred
   */
  async getSharedMessageBlocklist() {
    try {
      const conn = await lazy.ProfilesDatastoreService.getConnection();
      if (!conn) {
        return null;
      }
      const rows = await conn.executeCached(
        `SELECT messageId FROM MessagingSystemMessageBlocklist;`
      );

      return rows.map(row => row.getResultByName("messageId"));
    } catch (e) {
      lazy.ASRouterPreferences.console.error(
        `ASRouterStorage: Failed reading from MessagingSystemMessageBlocklist`,
        e
      );
      if (this.telemetry) {
        this.telemetry.handleUndesiredEvent({
          event: "SHARED_DB_READ_FAILED",
        });
      }
      return null;
    }
  }

  /**
   * Set the message impressions for a given message ID
   *
   * @param {string} messageId - The message ID to set the impressions for
   * @param {Array|null} impressions - The new value of "impressions" (an array of
   *  impression data or an emtpy array, or null to delete)
   * @returns {boolean} Success status
   */
  async setSharedMessageImpressions(messageId, impressions) {
    let success = true;
    try {
      const conn = await lazy.ProfilesDatastoreService.getConnection();
      if (!conn) {
        return false;
      }
      if (!messageId) {
        throw new Error(
          "Failed attempt to set shared message impressions with no message ID."
        );
      }

      // If impressions is falsy or empty, delete the row.
      if (!impressions?.length) {
        await conn.executeBeforeShutdown(
          "ASRouter: setSharedMessageImpressions",
          async () => {
            await conn.executeCached(
              `DELETE FROM MessagingSystemMessageImpressions WHERE messageId = :messageId;`,
              {
                messageId,
              }
            );
          }
        );
      } else {
        await conn.executeBeforeShutdown(
          "ASRouter: setSharedMessageImpressions",
          async () => {
            await conn.executeCached(
              `INSERT INTO MessagingSystemMessageImpressions (messageId, impressions) VALUES (
                :messageId,
                jsonb(:impressions)
              )
              ON CONFLICT (messageId) DO UPDATE SET impressions = excluded.impressions;`,
              {
                messageId,
                impressions: JSON.stringify(impressions),
              }
            );
          }
        );
      }

      lazy.ProfilesDatastoreService.notify();
    } catch (e) {
      lazy.ASRouterPreferences.console.error(
        `ASRouterStorage: Failed writing to MessagingSystemMessageImpressions`,
        e
      );
      if (this.telemetry) {
        this.telemetry.handleUndesiredEvent({
          event: "SHARED_DB_WRITE_FAILED",
        });
      }
      success = false;
    }

    return success;
  }

  /**
   * Adds a message ID to the blocklist and removes impressions
   * for that message ID from the impressions table when isBlocked is true
   * and deletes message ID from the blocklist when isBlocked is false
   *
   * @param {string} messageId - The message ID to set the blocked status for
   * @param {boolean} [isBlocked=true] - If the message should be blocked (true) or unblocked (false)
   * @returns {boolean} Success status
   */
  async setSharedMessageBlocked(messageId, isBlocked = true) {
    let success = true;
    if (isBlocked) {
      // Block the message, and clear impressions
      try {
        const conn = await lazy.ProfilesDatastoreService.getConnection();
        if (!conn) {
          return false;
        }
        await conn.executeTransaction(async () => {
          await conn.executeCached(
            `INSERT INTO MessagingSystemMessageBlocklist (messageId)
                VALUES (:messageId);`,
            {
              messageId,
            }
          );
          await conn.executeCached(
            `DELETE FROM MessagingSystemMessageImpressions
                WHERE messageId = :messageId;`,
            {
              messageId,
            }
          );
        });
      } catch (e) {
        lazy.ASRouterPreferences.console.error(
          `ASRouterStorage: Failed writing to MessagingSystemMessageBlocklist`,
          e
        );
        if (this.telemetry) {
          this.telemetry.handleUndesiredEvent({
            event: "SHARED_DB_WRITE_FAILED",
          });
        }
        success = false;
      }
    } else {
      // Unblock the message
      try {
        const conn = await lazy.ProfilesDatastoreService.getConnection();
        if (!conn) {
          return false;
        }
        await conn.executeBeforeShutdown(
          "ASRouter: setSharedMessageBlocked",
          async () => {
            await conn.executeCached(
              `DELETE FROM MessagingSystemMessageBlocklist WHERE messageId = :messageId;`,
              {
                messageId,
              }
            );
          }
        );
      } catch (e) {
        lazy.ASRouterPreferences.console.error(
          `ASRouterStorage: Failed writing to MessagingSystemMessageBlocklist`,
          e
        );
        if (this.telemetry) {
          this.telemetry.handleUndesiredEvent({
            event: "SHARED_DB_WRITE_FAILED",
          });
        }
        success = false;
      }
    }

    lazy.ProfilesDatastoreService.notify();
    return success;
  }

  async resetSharedMessageStorage() {
    let success = true;
    try {
      const conn = await lazy.ProfilesDatastoreService.getConnection();
      if (!conn) {
        return false;
      }
      await conn.executeBeforeShutdown(
        "ASRouter: resetSharedMessageBlocklist",
        async () => {
          await conn.executeCached(
            `DELETE FROM MessagingSystemMessageBlocklist;`
          );
          await conn.executeCached(
            `DELETE FROM MessagingSystemMessageImpressions;`
          );
        }
      );
    } catch (e) {
      lazy.ASRouterPreferences.console.error(
        `ASRouterStorage: Failed resetting MessagingSystemMessageBlocklist and MessagingSystemMessageImpressions`,
        e
      );
      if (this.telemetry) {
        this.telemetry.handleUndesiredEvent({
          event: "SHARED_DB_WRITE_FAILED",
        });
      }
      success = false;
    }

    lazy.ProfilesDatastoreService.notify();
    return success;
  }
}

export function getDefaultOptions(options) {
  return { collapsed: !!options.collapsed };
}
