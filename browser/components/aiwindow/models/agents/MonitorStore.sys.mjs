/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  MAX_HISTORY_ENTRIES,
  MONITOR_ERROR_CODES,
  MONITOR_EXPIRY_REASONS,
  trimAndFilterWatchUrls,
} from "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  IndexedDB: "resource://gre/modules/IndexedDB.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", () =>
  console.createInstance({
    logModule: "MonitorStore",
    maxLogLevelPref: "browser.smartwindow.monitorStore.logLevel",
  })
);

const CURRENT_SCHEMA_VERSION = 1;
const DB_NAME = "monitor-store";
const MONITOR_STORE_NAME = "monitors";
const CREATED_AT_INDEX = "createdAt";
const PREF_BRANCH = "browser.smartwindow.monitorStore";
const HISTORY_STATUSES = new Set(["error", "running", "success"]);
const HISTORY_ERROR_CODES = new Set(Object.values(MONITOR_ERROR_CODES));
const EXPIRY_REASONS = new Set(Object.values(MONITOR_EXPIRY_REASONS));

function invalidField(field) {
  return new Error(`Monitor ${field} is invalid.`);
}

function stringField(value, field, allowEmpty = false) {
  if (
    typeof value !== "string" ||
    (!allowEmpty && !value.trim()) ||
    value !== value.trim()
  ) {
    throw invalidField(field);
  }
  return value;
}

function timestampField(value, field, nullable = false) {
  if (nullable && value == null) {
    return null;
  }
  if (typeof value !== "string") {
    throw invalidField(field);
  }
  const date = new Date(value);
  if (Number.isNaN(date.getTime()) || date.toISOString() !== value) {
    throw invalidField(field);
  }
  return value;
}

function runCountField(value, recoverInvalid = false) {
  if (Number.isInteger(value) && value >= 0) {
    return value;
  }
  if (!recoverInvalid) {
    throw invalidField("run count");
  }
  lazy.log.warn("Discarding invalid stored monitor run count.");
  return 0;
}

function scheduleRecord(schedule) {
  if (!schedule || typeof schedule !== "object" || Array.isArray(schedule)) {
    throw invalidField("schedule");
  }

  switch (schedule.type) {
    case "interval":
      if (!Number.isFinite(schedule.hours) || schedule.hours <= 0) {
        throw invalidField("schedule");
      }
      return { type: schedule.type, hours: schedule.hours };
    case "daily":
      if (
        !Number.isInteger(schedule.hour) ||
        schedule.hour < 0 ||
        schedule.hour > 23 ||
        !Number.isInteger(schedule.minute) ||
        schedule.minute < 0 ||
        schedule.minute > 59
      ) {
        throw invalidField("schedule");
      }
      return {
        type: schedule.type,
        hour: schedule.hour,
        minute: schedule.minute,
      };
    case "weekly":
      if (
        !Number.isInteger(schedule.weekday) ||
        schedule.weekday < 0 ||
        schedule.weekday > 6 ||
        !Number.isInteger(schedule.hour) ||
        schedule.hour < 0 ||
        schedule.hour > 23 ||
        !Number.isInteger(schedule.minute) ||
        schedule.minute < 0 ||
        schedule.minute > 59
      ) {
        throw invalidField("schedule");
      }
      return {
        type: schedule.type,
        weekday: schedule.weekday,
        hour: schedule.hour,
        minute: schedule.minute,
      };
    default:
      throw invalidField("schedule");
  }
}

function watchUrlRecords(watchUrls) {
  const normalizedUrls = trimAndFilterWatchUrls(watchUrls);
  if (!normalizedUrls.length) {
    throw invalidField("watch URLs");
  }
  return normalizedUrls;
}

function initialSnapshotRecord(snapshot, recoverInvalid = false) {
  if (snapshot == null) {
    return null;
  }
  try {
    if (
      typeof snapshot !== "object" ||
      Array.isArray(snapshot) ||
      typeof snapshot.pageContent !== "string"
    ) {
      throw invalidField("initial snapshot");
    }
    return {
      capturedAt: timestampField(
        snapshot.capturedAt,
        "initial snapshot timestamp"
      ),
      pageContent: snapshot.pageContent,
    };
  } catch (error) {
    if (!recoverInvalid) {
      throw error;
    }
    lazy.log.warn(`Discarding invalid stored initial snapshot: ${error}`);
    return null;
  }
}

function expiryRecord(expiry, recoverInvalid = false) {
  if (expiry == null) {
    return null;
  }
  try {
    if (
      typeof expiry !== "object" ||
      Array.isArray(expiry) ||
      !EXPIRY_REASONS.has(expiry.reason)
    ) {
      throw invalidField("expiry");
    }
    return {
      expiredAt: timestampField(expiry.expiredAt, "expiry timestamp"),
      reason: expiry.reason,
    };
  } catch (error) {
    if (!recoverInvalid) {
      throw error;
    }
    lazy.log.warn(`Discarding invalid stored expiry: ${error}`);
    return null;
  }
}

function historyRecord(entry) {
  if (!entry || typeof entry !== "object" || Array.isArray(entry)) {
    throw invalidField("history entry");
  }
  if (!HISTORY_STATUSES.has(entry.status)) {
    throw invalidField("history status");
  }
  if (typeof entry.resultExplanation !== "string") {
    throw invalidField("history result explanation");
  }
  if (typeof entry.conditionMet !== "boolean") {
    throw invalidField("history condition result");
  }

  const record = {
    id: stringField(entry.id, "history ID"),
    checkedAt: timestampField(entry.checkedAt, "history timestamp"),
    status: entry.status,
    resultExplanation: entry.resultExplanation,
    conditionMet: entry.conditionMet,
  };
  if (entry.errorCode != null) {
    if (!HISTORY_ERROR_CODES.has(entry.errorCode)) {
      throw invalidField("history error code");
    }
    record.errorCode = entry.errorCode;
  }
  return record;
}

function sanitizeHistoryRecords(history, recoverInvalid) {
  if (!Array.isArray(history)) {
    if (recoverInvalid) {
      lazy.log.warn("Discarding invalid stored monitor history.");
      return [];
    }
    throw invalidField("history");
  }

  const records = [];
  for (const entry of history) {
    try {
      records.push(historyRecord(entry));
    } catch (error) {
      if (!recoverInvalid) {
        throw error;
      }
      lazy.log.warn(`Discarding invalid stored history entry: ${error}`);
    }
  }
  if (records.length > MAX_HISTORY_ENTRIES) {
    if (!recoverInvalid) {
      throw invalidField("history");
    }
    return records.slice(-MAX_HISTORY_ENTRIES);
  }
  return records;
}

function sanitizeMonitorRecord(monitor, recoverInvalid = false) {
  if (!monitor || typeof monitor !== "object" || Array.isArray(monitor)) {
    throw invalidField("record");
  }
  const title = monitor.title ?? "";
  if (typeof monitor.enabled !== "boolean") {
    throw invalidField("enabled state");
  }

  return {
    id: stringField(monitor.id, "ID"),
    title: stringField(title, "title", true),
    monitorPrompt: stringField(monitor.monitorPrompt, "prompt"),
    watchUrls: watchUrlRecords(monitor.watchUrls),
    schedule: scheduleRecord(monitor.schedule),
    enabled: monitor.enabled,
    // Monitors stored before the counter existed start from their visible
    // history length.
    runCount: runCountField(
      monitor.runCount ??
        (Array.isArray(monitor.history) ? monitor.history.length : 0),
      recoverInvalid
    ),
    createdAt: timestampField(monitor.createdAt, "creation timestamp"),
    updatedAt: timestampField(monitor.updatedAt, "update timestamp"),
    lastRunTime: timestampField(monitor.lastRunTime, "last run timestamp"),
    nextRunTime: timestampField(
      monitor.nextRunTime,
      "next run timestamp",
      true
    ),
    // records stored before auto-expiry existed count from their creation
    activeSince: timestampField(
      monitor.activeSince ?? monitor.createdAt,
      "active since timestamp"
    ),
    lastMatchAt: timestampField(
      monitor.lastMatchAt ?? null,
      "last match timestamp",
      true
    ),
    expiry: expiryRecord(monitor.expiry ?? null, recoverInvalid),
    history: sanitizeHistoryRecords(monitor.history ?? [], recoverInvalid),
    initialSnapshot: initialSnapshotRecord(
      monitor.initialSnapshot ?? null,
      recoverInvalid
    ),
  };
}

function wrapRequest(request) {
  return new Promise((resolve, reject) => {
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}

function isNewerSchemaError(error) {
  return error?.name === "VersionError";
}

/**
 * Persists monitor definitions and run history.
 */
class MonitorStoreImpl {
  #asyncShutdownBlocker;
  #db = null;
  #pendingWrites = new Set();
  #promiseDb = null;
  #promiseWrite = Promise.resolve();
  #shutdownClient;
  #shutdownBlockerAdded = false;
  #shuttingDown = false;

  constructor(shutdownClient = lazy.AsyncShutdown.profileBeforeChange) {
    this.#shutdownClient = shutdownClient;
    this.#asyncShutdownBlocker = async () => {
      this.#shuttingDown = true;
      try {
        await this.#promiseWrite;
        this.#closeDatabase();
      } finally {
        this.#shutdownBlockerAdded = false;
      }
    };
  }

  async listMonitors() {
    this.#prepareForOperation();
    const db = await this.#ensureDatabase();
    const transaction = db.transaction(MONITOR_STORE_NAME, "readonly");
    const transactionComplete = transaction.promiseComplete();
    let records;
    try {
      records = await transaction.objectStore(MONITOR_STORE_NAME).getAll();
      await transactionComplete;
    } catch (error) {
      transactionComplete.catch(() => {});
      throw error;
    }

    const monitors = [];
    for (const record of records) {
      try {
        monitors.push(sanitizeMonitorRecord(record, true));
      } catch (error) {
        lazy.log.warn(`Skipping invalid stored monitor: ${error}`);
      }
    }
    return monitors.sort(
      (first, second) =>
        Date.parse(first.createdAt) - Date.parse(second.createdAt) ||
        first.id.localeCompare(second.id)
    );
  }

  async saveMonitor(monitor) {
    const record = sanitizeMonitorRecord(monitor);
    return this.#queueWrite("saveMonitor", () =>
      this.#withWriteStore(store => store.put(record))
    );
  }

  async saveMonitors(monitors) {
    if (!Array.isArray(monitors)) {
      throw invalidField("collection");
    }
    const records = monitors.map(sanitizeMonitorRecord);
    return this.#queueWrite("saveMonitors", () =>
      this.#withWriteStore(async store => {
        await store.clear();
        for (const record of records) {
          await store.put(record);
        }
      })
    );
  }

  async deleteMonitor(id) {
    const monitorId = stringField(id, "ID");
    return this.#queueWrite("deleteMonitor", () =>
      this.#withWriteStore(store => store.delete(monitorId))
    );
  }

  async destroyDatabase() {
    return this.#queueWrite("destroyDatabase", async () => {
      await this.#closeDatabaseConnection();
      await this.#deleteDatabase();
      this.#promiseDb = null;
    });
  }

  async close() {
    await this.#closeDatabaseConnection();
    if (!this.#shuttingDown) {
      this.#removeShutdownBlocker();
    }
  }

  async #closeDatabaseConnection() {
    if (!this.#db && this.#promiseDb) {
      await this.#promiseDb.catch(() => {});
    }

    this.#closeDatabase();
    this.#promiseDb = null;
  }

  async #withWriteStore(callback) {
    const db = await this.#ensureDatabase();
    const transaction = db.transaction(MONITOR_STORE_NAME, "readwrite");
    const transactionComplete = transaction.promiseComplete();
    try {
      await callback(transaction.objectStore(MONITOR_STORE_NAME));
      await transactionComplete;
    } catch (error) {
      transactionComplete.catch(() => {});
      try {
        transaction.abort();
      } catch (_) {}
      throw error;
    }
  }

  #queueWrite(operation, task) {
    this.#prepareForOperation();
    const pendingWrite = {
      operation,
      queuedAt: Date.now(),
      startedAt: null,
    };
    this.#pendingWrites.add(pendingWrite);
    const runTask = () => {
      pendingWrite.startedAt = Date.now();
      return task();
    };
    const promise = this.#promiseWrite.then(runTask, runTask);
    this.#promiseWrite = promise
      .finally(() => {
        this.#pendingWrites.delete(pendingWrite);
      })
      .catch(() => {});
    return promise;
  }

  #prepareForOperation() {
    if (this.#shuttingDown) {
      throw new Error("Monitor store is shutting down.");
    }
    this.#addShutdownBlocker();
  }

  async #openDatabase() {
    const database = await lazy.IndexedDB.open(
      DB_NAME,
      CURRENT_SCHEMA_VERSION,
      (db, event) => {
        if (!db.objectStoreNames.contains(MONITOR_STORE_NAME)) {
          const store = db.createObjectStore(MONITOR_STORE_NAME, {
            keyPath: "id",
          });
          store.createIndex(CREATED_AT_INDEX, "createdAt");
          return;
        }

        const store = event.target.transaction.objectStore(MONITOR_STORE_NAME);
        if (!store.indexNames.contains(CREATED_AT_INDEX)) {
          store.createIndex(CREATED_AT_INDEX, "createdAt");
        }
      }
    );

    if (this.#shuttingDown && !this.#pendingWrites.size) {
      try {
        database.close();
      } catch (error) {
        lazy.log.warn(`Error closing database: ${error.message}`);
      }
      throw new Error("Monitor store is shutting down.");
    }

    this.#db = database;
    this.#db.onversionchange = () => {
      this.#closeDatabase();
      this.#promiseDb = null;
    };
    this.#db.onclose = () => {
      this.#db = null;
      this.#promiseDb = null;
    };
    return this.#db;
  }

  async #ensureDatabase() {
    if (this.#promiseDb) {
      return this.#promiseDb;
    }

    this.#promiseDb = (async () => {
      if (this.#removeDatabaseOnStartup) {
        this.#closeDatabase();
        await this.#deleteDatabase();
      }

      try {
        return await this.#openDatabase();
      } catch (error) {
        if (isNewerSchemaError(error)) {
          throw new Error(
            `Monitor store schema version is newer than supported version ${CURRENT_SCHEMA_VERSION}.`
          );
        }
        lazy.log.warn(`Error opening database: ${error}`);
        throw error;
      }
    })().catch(error => {
      this.#closeDatabase();
      this.#promiseDb = null;
      throw error;
    });

    return this.#promiseDb;
  }

  async #deleteDatabase() {
    try {
      await wrapRequest(lazy.IndexedDB.deleteDatabase(DB_NAME));
      this.#removeDatabaseOnStartup = false;
    } catch (error) {
      this.#removeDatabaseOnStartup = true;
      throw error;
    }
  }

  #closeDatabase() {
    if (!this.#db) {
      return;
    }

    const db = this.#db;
    db.onversionchange = null;
    db.onclose = null;
    try {
      db.close();
    } catch (error) {
      lazy.log.warn(`Error closing database: ${error.message}`);
    }
    this.#db = null;
  }

  #addShutdownBlocker() {
    if (this.#shutdownBlockerAdded) {
      return;
    }

    this.#shutdownClient.addBlocker(
      "MonitorStore: Shutdown",
      this.#asyncShutdownBlocker,
      {
        fetchState: () => ({
          databaseOpen: !!this.#db,
          shuttingDown: this.#shuttingDown,
          pendingWrites: Array.from(this.#pendingWrites, pendingWrite => ({
            operation: pendingWrite.operation,
            state: pendingWrite.startedAt === null ? "queued" : "active",
            pendingForMs: Date.now() - pendingWrite.queuedAt,
          })),
        }),
      }
    );
    this.#shutdownBlockerAdded = true;
  }

  #removeShutdownBlocker() {
    if (!this.#shutdownBlockerAdded) {
      return;
    }

    this.#shutdownClient.removeBlocker(this.#asyncShutdownBlocker);
    this.#shutdownBlockerAdded = false;
  }

  get #removeDatabaseOnStartup() {
    return Services.prefs.getBoolPref(
      `${PREF_BRANCH}.removeDatabaseOnStartup`,
      false
    );
  }

  set #removeDatabaseOnStartup(value) {
    Services.prefs.setBoolPref(`${PREF_BRANCH}.removeDatabaseOnStartup`, value);
  }

  get databaseName() {
    return DB_NAME;
  }

  get databaseVersion() {
    return CURRENT_SCHEMA_VERSION;
  }

  get objectStoreName() {
    return MONITOR_STORE_NAME;
  }
}

const MonitorStore = new MonitorStoreImpl();
export { MonitorStore, MonitorStoreImpl };
