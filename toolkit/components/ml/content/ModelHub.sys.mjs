/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

/**
 * @import { ProgressAndStatusCallbackParams } from "./Utils.sys.mjs"
 * @import { ModelFileHeaders } from "../ml.d.ts"
 */

/**
 * A key used for the IndexedDB stores.
 *
 * @typedef {?(any[] | IDBKeyRange)} StoreKey
 */

/**
 * @typedef {{
 *   taskName: string,
 *   model: string,
 *   revision: string,
 *   file: string,
 * }} TaskRecord
 */

/**
 * @typedef {{
 *   model: string,
 *   revision: string,
 *   file: string,
 *   engineIds?: Array<string>,
 * }} EngineRecord
 */

/**
 * @typedef {{
 *   model: string,
 *   revision: string,
 *   file: string,
 *   headers: ModelFileHeaders,
 * }} HeadersRecord
 */

/**
 * @typedef {{
 *   engineId: string,
 *   taskName: string,
 *   model: string,
 *   revision: string,
 *   file: string,
 *   featureId: string,
 *   sessionId: string,
 *   modelHubRootUrl?: string,
 *   modelHubUrlTemplate?: string,
 *   progressCallback?: ?function(ProgressAndStatusCallbackParams):void,
 *   telemetryData?: object,
 *   abortSignal?: ?AbortSignal,
 * }} GetModelDataAsFileConfig
 */

/**
 * @typedef {{
 *   promise: Promise<[string, ModelFileHeaders]>,
 *   progressCallbacks: Map<object, function(ProgressAndStatusCallbackParams):void>,
 *   controller: AbortController,
 *   waiters: Set<object>,
 *   downloadStarted: boolean,
 *   downloadStartedAt: number,
 *   duration: number,
 *   lastProgress: ?ProgressAndStatusCallbackParams,
 * }} InFlightDownload
 */

const lazy = XPCOMUtils.declareLazy({
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  ObjectUtils: "resource://gre/modules/ObjectUtils.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  Progress: "chrome://global/content/ml/Utils.sys.mjs",
  MLUtils: "chrome://global/content/ml/Utils.sys.mjs",
  OPFS: "chrome://global/content/ml/OPFS.sys.mjs",
  URLChecker: "chrome://global/content/ml/Utils.sys.mjs",
  RejectionType: "chrome://global/content/ml/Utils.sys.mjs",
  createFileUrl: "chrome://global/content/ml/Utils.sys.mjs",
  DEFAULT_ENGINE_ID: "chrome://global/content/ml/EngineProcess.sys.mjs",
  FILE_REGEX: "chrome://global/content/ml/EngineProcess.sys.mjs",
  console: () =>
    console.createInstance({
      maxLogLevelPref: "browser.ml.logLevel",
      logModule: "GeckoMLModelHub",
    }),
  DEFAULT_MAX_CACHE_SIZE: {
    pref: "browser.ml.modelCacheMaxSize",
    default: 4,
  },
  DEFAULT_URL_TEMPLATE: {
    pref: "browser.ml.modelHubUrlTemplate",
    default: "{model}/{revision}",
  },
  DEFAULT_ROOT_URL: {
    pref: "browser.ml.modelHubRootUrl",
    default: "https://model-hub.mozilla.org/",
  },
});

const ALLOWED_HEADERS_KEYS = [
  "Content-Type",
  "ETag",
  "status",
  "fileSize", // the size in bytes we store
  "Content-Length", // the size we download (can be different when gzipped)
  "lastUpdated",
  "lastUsed",
];

const MOZILLA_HUB_HOSTNAME = "model-hub.mozilla.org";
const HF_HUB_HOSTNAME = "huggingface.co";
const MOCHITESTS_HOSTNAME = "mochitests";
const DEFAULT_CONTENT_TYPE = "application/octet-stream";
const DEFAULT_DELETE_TIMEOUT_MS = 5000;
const LOCAL_CHROME_PREFIX = "chrome://";

// Default indexedDB revision.
const DEFAULT_MODEL_REVISION = 9;

/**
 * One-shot engineId renames applied during the 6 -> 7 schema migration. Keys
 * are the obsolete engineIds, values are the replacements. Used to preserve
 * already-downloaded model files when a feature's engineId is renamed.
 *
 * @type {Record<string, string>}
 */
const RENAMED_ENGINE_IDS = {
  // Bug 1967279: Link Preview engineId rename.
  wllamapreview: "link-preview",
};

/**
 * One-shot taskName renames applied during the 7 -> 8 schema migration. The
 * taskName is part of the tasks store key, so without this rewrite a renamed
 * task would miss the cache and re-download its model files.
 *
 * @type {Record<string, string>}
 */
const RENAMED_TASK_NAMES = {
  "wllama-text-generation": "llama-text-generation",
};

const DEFAULT_DB_NAME = "modelFiles";

const DEFAULT_OWNER_ROOT = "modelOwners";

const ONE_GIB = 1024 * 1024 * 1024;
const NO_ETAG = "NO_ETAG";

/**
 * Custom error when a fetch is attempted on a forbidden URL
 */
class ForbiddenURLError extends Error {
  /**
   * @param {string} url
   * @param {string} rejectionType
   */
  constructor(url, rejectionType) {
    super(
      `Forbidden URL: ${url} (${rejectionType}). Set MOZ_ALLOW_EXTERNAL_ML_HUB=1 to allow external URLs.`
    );
    this.name = "ForbiddenURLError";
    this.url = url;
  }
}

/**
 * Class representing a model owner.
 *
 * A model owner can be a user or an organization
 */
class ModelOwner {
  /**
   * @param {object} config
   * @param {string} config.hostname
   * @param {string} config.owner
   */
  constructor({ hostname, owner }) {
    this.hostname = hostname;
    this.owner = owner;
  }

  /**
   * @param {string} model - The fully qualified model name (hub/owner/name)
   * @returns {ModelOwner}
   */
  static fromModel(model) {
    const hostname = model.split("/")[0];
    const owner = model.split("/")[1];
    return new ModelOwner({ hostname, owner });
  }

  /**
   * Gets the icon OPFS path
   *
   * @returns {string}
   */
  #getIconFilePath() {
    return `${DEFAULT_OWNER_ROOT}/${this.hostname}/${this.owner}/icon`;
  }

  /**
   * Removes any cache associated with this owner
   */
  async pruneCache() {
    const filePath = this.#getIconFilePath();
    try {
      const fileHandle = await lazy.OPFS.getFileHandle(filePath);

      if (fileHandle) {
        await lazy.OPFS.remove(filePath);
      }
    } catch (e) {
      // we can ignore this error, as the deleteIconFile may be called multiple times on the same file.
    }
  }

  /**
   * Returns the owner's icon
   *
   * @returns {Promise<string|null>}
   */
  async getIcon() {
    // If it's not the known HF hub root, we don't know how to fetch an icon
    if (
      ![MOCHITESTS_HOSTNAME, HF_HUB_HOSTNAME, MOZILLA_HUB_HOSTNAME].includes(
        this.hostname
      )
    ) {
      lazy.console.debug(
        "We don't know how to get icons from that hub",
        this.hostname
      );
      return null;
    }

    // Switch from Mozilla to Hugging Face if needed
    if (this.hostname === MOZILLA_HUB_HOSTNAME) {
      this.hostname = "huggingface.co";
    }

    const hubRootUrl = `https://${this.hostname}/`;

    const possibleUrls =
      this.hostname === MOCHITESTS_HOSTNAME
        ? ["chrome://global/content/ml/mozilla-logo.webp"]
        : [
            // Attempt to fetch (org first, then user, then default)
            `${hubRootUrl}api/organizations/${this.owner}/avatar?redirect=true`,
            `${hubRootUrl}api/users/${this.owner}/avatar?redirect=true`,
            "chrome://global/content/ml/mozilla-logo.webp",
          ];

    for (const url of possibleUrls) {
      try {
        const fileObject = await lazy.OPFS.download({
          source: url,
          savePath: this.#getIconFilePath(),
          deletePreviousVersions: false,
          useCache: true,
          ignoreCachingErrors: true,
        });

        return URL.createObjectURL(fileObject);
      } catch (error) {
        console.error(error);
      }
    }

    throw new Error("Could not fetch the icon from the provided urls");
  }
}

/**
 * Class for managing a cache stored in IndexedDB.
 */
class IndexedDBCache {
  /**
   * Reference to the IndexedDB database.
   *
   * @type {IDBDatabase|null}
   */
  db = null;

  /**
   * Reference to the IndexedDB principal.
   *
   * @type {nsIPrincipal}
   */
  #principal;

  /**
   * Version of the database.
   *
   * @type {number}
   */
  dbVersion;

  /**
   * Name of the database used by IndexedDB.
   *
   * @type {string}
   */
  dbName;

  /**
   * Name of the object store for storing files. (Retained for deletion in previous versions.)
   * Files are expected to be unique for a given tuple of file name, model, file, revision
   *
   * @type {string}
   */
  fileStoreName;

  /**
   * Name of the object store for storing headers.
   * Headers are expected to be unique for a given triplet model, file, revision
   *
   * @type {string}
   */
  headersStoreName;

  /**
   * Name of the object store for storing task names.
   * Tasks are expected to be unique for a given tuple of task name, model, file, revision
   *
   * @type {string}
   */
  taskStoreName;

  /**
   * Name of the object store for storing (model, file, revision, engineIds)
   *
   * @type {string}
   */
  enginesStoreName;

  /**
   * Name and KeyPath for indices to be created on object stores.
   */
  #indices = {
    modelRevisionIndex: {
      name: "modelRevisionIndex",
      keyPath: ["model", "revision"],
    },
    modelRevisionFileIndex: {
      name: "modelRevisionFileIndex",
      keyPath: ["model", "revision", "file"],
    },
    taskModelRevisionIndex: {
      name: "taskModelRevisionIndex",
      keyPath: ["taskName", "model", "revision"],
    },
  };
  /**
   * Maximum size of the cache in GiB. Defaults to "browser.ml.modelCacheMaxSize".
   *
   * @type {number}
   */
  #maxSize = lazy.DEFAULT_MAX_CACHE_SIZE;

  /**
   * The dbName only changes in tests, but doing this as a Map keyed on the dbName
   * makes it correct for a configurable dbName in cases that ever changes.
   *
   * @type {Map<string, Promise<void>>}
   */
  static #pendingRepairs = new Map();

  /**
   * Private constructor to prevent direct instantiation.
   * Use IndexedDBCache.init to create an instance.
   *
   * @param {object} config
   * @param {nsIPrincipal} config.principal - The principal to use for the database.
   * @param {string} [config.dbName] - The name of the database file.
   * @param {number} [config.version] - The version number of the database.
   * @param {number} [config.maxSize] Maximum size of the cache in GiB. Defaults to "browser.ml.modelCacheMaxSize".
   */
  constructor({
    principal,
    dbName = DEFAULT_DB_NAME,
    version = DEFAULT_MODEL_REVISION,
    maxSize = lazy.DEFAULT_MAX_CACHE_SIZE,
  }) {
    this.dbName = dbName;
    this.dbVersion = version;
    this.fileStoreName = "files";
    this.headersStoreName = "headers";
    this.taskStoreName = "tasks";
    this.enginesStoreName = "engines";
    this.#maxSize = maxSize;
    this.#principal = principal;
  }

  /**
   * Delete a database and wait for it to close.
   *
   * @param {nsIPrincipal} principal
   * @param {string} [dbName]
   * @param {number} [timeoutMs]
   * @returns {Promise<{status: string, error?: DOMException|null}>}
   */
  static async deleteDatabaseAndWait(
    principal,
    dbName = DEFAULT_DB_NAME,
    timeoutMs = DEFAULT_DELETE_TIMEOUT_MS
  ) {
    try {
      await Promise.all([
        lazy.OPFS.remove(dbName, { recursive: true }),
        lazy.OPFS.remove(DEFAULT_OWNER_ROOT, { recursive: true }),
      ]);
    } catch (e) {
      // can be empty
    }

    return new Promise((resolve, reject) => {
      if (!indexedDB) {
        throw new Error("Expected the IDBFactory to be available.");
      }
      const request = indexedDB.deleteForPrincipal(principal, dbName);

      const timer = lazy.setTimeout(() => {
        reject(new Error("Request timed out (possibly blocked forever)"));
      }, timeoutMs);

      request.onsuccess = () => {
        lazy.clearTimeout(timer);
        resolve({ status: "success" });
      };

      request.onerror = () => {
        lazy.clearTimeout(timer);
        lazy.console.warn("Request error:", request.error);
        resolve({ status: "error", error: request.error });
      };

      request.onblocked = () => {
        lazy.console.warn(
          "Request blocked — waiting for other connections to close"
        );
        // Let it continue to wait
      };
    });
  }

  /**
   * Get the principal used by the the cache for storage access.
   *
   * @returns {nsIPrincipal} The principal to use for database operations.
   */
  getPrincipal() {
    return this.#principal;
  }

  /**
   * Static method to create and initialize an instance of IndexedDBCache.
   *
   * @param {object} config
   * @param {string} [config.dbName]
   * @param {number} [config.version]
   * @param {number} [config.maxSize] Maximum size of the cache in bytes. Defaults to "browser.ml.modelCacheMaxSize".
   * @param {boolean} [config.reset=false] - Whether to reset the database.
   * @returns {Promise<IndexedDBCache>} An initialized instance of IndexedDBCache.
   */
  static async init({
    dbName = DEFAULT_DB_NAME,
    version = DEFAULT_MODEL_REVISION,
    maxSize = lazy.DEFAULT_MAX_CACHE_SIZE,
    reset = false,
  } = {}) {
    const principal = Services.scriptSecurityManager.getSystemPrincipal();

    if (reset) {
      await IndexedDBCache.deleteDatabaseAndWait(principal, dbName);
    }

    const cacheInstance = new IndexedDBCache({
      dbName,
      version,
      maxSize,
      principal,
    });
    cacheInstance.db = await cacheInstance.#openDB();

    return cacheInstance;
  }

  /**
   * Called to close the DB connection and dispose the instance.
   */
  async dispose() {
    if (this.db) {
      this.db.close();
      this.db = null;
    }
  }

  /**
   * Perform schema migrations between IndexedDB versions.
   *
   * @param {IDBDatabase} db
   * @param {number} oldVersion
   * @param {IDBTransaction} transaction
   * @returns {{destructive: boolean, requiresRepair: boolean}} `destructive`
   *   is true when the migration dropped object stores, in which case the
   *   on-disk OPFS cache must also be cleared by the caller to stay
   *   consistent.
   */
  #migrateStore(db, oldVersion, transaction) {
    const newVersion = db.version;
    lazy.console.debug(`Migrating from version ${oldVersion} to ${newVersion}`);
    try {
      // Schemas from version 5 onwards are migrated in place with the
      // cumulative steps below. Version 5 -> 6 only added optional header
      // fields; no rewrite needed.
      if (oldVersion >= 5 && oldVersion < newVersion) {
        // Version 6 -> 7 renames engineIds in place to preserve OPFS files for
        // users who already downloaded models under a since-renamed engineId.
        if (
          oldVersion < 7 &&
          db.objectStoreNames.contains(this.enginesStoreName)
        ) {
          const store = transaction.objectStore(this.enginesStoreName);
          const cursorRequest = store.openCursor();
          cursorRequest.onsuccess = () => {
            /** @type {IDBCursorWithValue} */
            const cursor = cursorRequest.result;
            if (!cursor) {
              return;
            }
            /** @type {EngineRecord} */
            const row = cursor.value;
            if (Array.isArray(row.engineIds)) {
              let changed = false;
              const renamed = row.engineIds.map(id => {
                if (RENAMED_ENGINE_IDS[id]) {
                  changed = true;
                  return RENAMED_ENGINE_IDS[id];
                }
                return id;
              });
              if (changed) {
                row.engineIds = Array.from(new Set(renamed));
                cursor.update(row);
              }
            }
            cursor.continue();
          };
        }

        // Version 7 -> 8 rewrites renamed taskNames. The taskName is part of
        // the tasks store key, so the row is re-inserted under its new key.
        if (
          oldVersion < 8 &&
          db.objectStoreNames.contains(this.taskStoreName)
        ) {
          const store = transaction.objectStore(this.taskStoreName);
          const cursorRequest = store.openCursor();
          cursorRequest.onsuccess = () => {
            /** @type {IDBCursorWithValue} */
            const cursor = cursorRequest.result;
            if (!cursor) {
              return;
            }
            /** @type {TaskRecord} */
            const row = cursor.value;
            if (RENAMED_TASK_NAMES[row.taskName]) {
              store.put({ ...row, taskName: RENAMED_TASK_NAMES[row.taskName] });
              cursor.delete();
            }
            cursor.continue();
          };
        }

        // Version 8 -> 9 does not rewrite anything in place. Instead it
        // flags that #repairStaleEntries should run once OPFS is reachable,
        // dropping headers/tasks rows and OPFS files left stale by bugs in
        // earlier versions.
        return { destructive: false, requiresRepair: oldVersion < 9 };
      }

      if (oldVersion < newVersion) {
        for (const name of [
          this.fileStoreName,
          this.headersStoreName,
          this.taskStoreName,
          this.enginesStoreName,
        ]) {
          if (db.objectStoreNames.contains(name)) {
            db.deleteObjectStore(name);
          }
        }
        return { destructive: true, requiresRepair: false };
      }
      return { destructive: false, requiresRepair: false };
    } finally {
      lazy.console.debug("Migration done");
    }
  }

  /**
   * @param {object} config
   * @param {IDBObjectStore} config.store
   * @param {string} config.name
   * @param {string | Array<string>} config.keyPath
   */
  #createOrMigrateIndices({ store, name, keyPath }) {
    if (store.indexNames.contains(name)) {
      if (!lazy.ObjectUtils.deepEqual(store.index(name).keyPath, keyPath)) {
        lazy.console.debug(
          `Deleting and recreating index ${name} on store ${store.name}`
        );
        store.deleteIndex(name);
        store.createIndex(name, keyPath);
      }
    } else {
      // index does not exist, so create
      lazy.console.debug(`Creating index ${name} on store ${store.name}`);
      store.createIndex(name, keyPath);
    }
  }

  /**
   * Opens or creates the IndexedDB database.
   *
   * @returns {Promise<IDBDatabase>}
   */
  async #openDB() {
    let wasUpgraded = false;
    let migrationResult = { destructive: false, requiresRepair: false };

    /** @type {Promise<IDBDatabase>} */
    const opened = new Promise((resolve, reject) => {
      if (!indexedDB) {
        throw new Error("Expected the IDBFactory to be available.");
      }

      const request = indexedDB.openForPrincipal(
        this.#principal,
        this.dbName,
        this.dbVersion
      );

      request.onerror = () => reject(request.error);

      request.onupgradeneeded = event => {
        /** @type {IDBDatabase} */
        const db = request.result;
        const transaction = /** @type {IDBTransaction} */ (request.transaction);

        try {
          migrationResult = this.#migrateStore(
            db,
            /** @type {IDBVersionChangeEvent} */ (event).oldVersion,
            transaction
          );

          if (!db.objectStoreNames.contains(this.headersStoreName)) {
            db.createObjectStore(this.headersStoreName, {
              keyPath: ["model", "revision", "file"],
            });
          }
          if (!db.objectStoreNames.contains(this.taskStoreName)) {
            db.createObjectStore(this.taskStoreName, {
              keyPath: ["taskName", "model", "revision", "file"],
            });
          }
          if (!db.objectStoreNames.contains(this.enginesStoreName)) {
            db.createObjectStore(this.enginesStoreName, {
              keyPath: ["model", "revision", "file"],
            });
          }

          const headerStore = transaction.objectStore(this.headersStoreName);
          this.#createOrMigrateIndices({
            store: headerStore,
            name: this.#indices.modelRevisionIndex.name,
            keyPath: this.#indices.modelRevisionIndex.keyPath,
          });

          const enginesStore = transaction.objectStore(this.enginesStoreName);
          this.#createOrMigrateIndices({
            store: enginesStore,
            name: this.#indices.modelRevisionIndex.name,
            keyPath: this.#indices.modelRevisionIndex.keyPath,
          });

          const taskStore = transaction.objectStore(this.taskStoreName);
          for (const { name, keyPath } of Object.values(this.#indices)) {
            this.#createOrMigrateIndices({ store: taskStore, name, keyPath });
          }

          wasUpgraded = true;
        } catch (error) {
          console.error("Migration failed:", error);
          reject(error);
        }
      };

      request.onsuccess = () => {
        /** @type {IDBDatabase} */
        const db = request.result;
        db.onversionchange = () => {
          lazy.console.debug(
            "The version of this database is changing. Closing."
          );
          db.close();
          if (this.db == db) {
            this.db = null;
          }
        };

        db.onclose = () => {
          lazy.console.debug("Database is closed");
          if (this.db == db) {
            this.db = null;
          }
        };

        resolve(db); // Immediately resolve after DB is ready
      };
    });

    const db = await opened;
    if (wasUpgraded && migrationResult.destructive) {
      lazy.console.debug("Clearing OPFS cache (destructive migration)");
      await lazy.OPFS.remove(this.dbName, {
        recursive: true,
        ignoreErrors: true,
      });
    }
    if (wasUpgraded && migrationResult.requiresRepair) {
      this.db = db;
      const repairPromise = this.#repairStaleEntries();
      IndexedDBCache.#pendingRepairs.set(this.dbName, repairPromise);
      try {
        await repairPromise;
      } finally {
        if (IndexedDBCache.#pendingRepairs.get(this.dbName) === repairPromise) {
          IndexedDBCache.#pendingRepairs.delete(this.dbName);
        }
      }
    } else {
      await IndexedDBCache.#pendingRepairs.get(this.dbName);
    }
    return db;
  }

  /**
   * The database 8 -> 9 migration fixes correctness issues for file handling. This
   * repair step fixes stale file entries.
   *
   * @returns {Promise<void>}
   */
  async #repairStaleEntries() {
    lazy.console.debug("Repairing stale cache entries after upgrade");

    const headers = /** @type {Array<HeadersRecord>} */ (
      await this.#getData({ storeName: this.headersStoreName })
    );

    const validPaths = new Set();
    for (const { model, revision, file } of headers) {
      const path = this.generateFilePathInOPFS({ model, revision, file });
      let hasRealFile = false;
      try {
        const fileHandle = await lazy.OPFS.getFileHandle(path);
        hasRealFile = (await fileHandle.getFile()).size > 0;
      } catch (e) {
        hasRealFile = false;
      }

      if (hasRealFile) {
        validPaths.add(path);
      } else {
        await this.#deleteData(this.headersStoreName, [model, revision, file]);
        await this.#deleteData(this.enginesStoreName, [model, revision, file]);
      }
    }

    const tasks = /** @type {Array<TaskRecord>} */ (
      await this.#getData({ storeName: this.taskStoreName })
    );
    for (const { taskName, model, revision, file } of tasks) {
      const path = this.generateFilePathInOPFS({ model, revision, file });
      if (!validPaths.has(path)) {
        await this.#deleteData(this.taskStoreName, [
          taskName,
          model,
          revision,
          file,
        ]);
      }
    }

    await this.#pruneOrphanedFiles(this.dbName, validPaths);
  }

  /**
   * Recursively clean up orphaned files. Part of the 8 -> 9 database migration.
   *
   * @param {string} dirPath
   * @param {Set<string>} validPaths
   * @returns {Promise<boolean>} True when `dirPath` itself ends up empty.
   */
  async #pruneOrphanedFiles(dirPath, validPaths) {
    let dirHandle;
    try {
      dirHandle = await lazy.OPFS.getDirectoryHandle(dirPath);
    } catch (e) {
      return true;
    }

    const entries = [];
    for await (const entry of dirHandle.entries()) {
      entries.push(entry);
    }

    let isEmpty = true;
    for (const [name, handle] of entries) {
      const entryPath = `${dirPath}/${name}`;
      if (handle.kind === "directory") {
        const childIsEmpty = await this.#pruneOrphanedFiles(
          entryPath,
          validPaths
        );
        if (childIsEmpty) {
          await dirHandle.removeEntry(name, { recursive: true });
        } else {
          isEmpty = false;
        }
      } else if (!validPaths.has(entryPath)) {
        await dirHandle.removeEntry(name);
      } else {
        isEmpty = false;
      }
    }

    return isEmpty;
  }

  /**
   * The open database connection. Throws rather than returning null so callers
   * can use it directly.
   *
   * @returns {IDBDatabase}
   */
  #requireDB() {
    if (!this.db) {
      throw new Error("The model cache database connection is not open.");
    }
    return this.db;
  }

  /**
   * Check whether the database connection is currently open.
   *
   * @returns {boolean} `true` if a database connection is currently open.
   */
  isOpen() {
    return this.db != null;
  }

  /**
   * Generic method to get the data from a specified object store.
   *
   * @param {object} config
   * @param {string} config.storeName
   * @param {StoreKey} [config.key]
   * @param {?string} [config.indexName]
   * @returns {Promise<Array<any>>}
   */
  async #getData({ storeName, key, indexName }) {
    return new Promise((resolve, reject) => {
      const transaction = this.#requireDB().transaction(
        [storeName],
        "readonly"
      );
      const store = transaction.objectStore(storeName);
      const request = (indexName ? store.index(indexName) : store).getAll(
        key ?? undefined
      );
      request.onerror = () => reject(request.error);
      request.onsuccess = () => resolve(request.result);
    });
  }

  /**
   * Generic method to get the unique keys from a specified object store.
   *
   * @param {object} config
   * @param {string} config.storeName
   * @param {StoreKey} [config.key]
   * @param {?string} [config.indexName]
   * @param {?function(IDBCursor):boolean} [config.filterFn] - A function to execute for each key found.
   *  It should return a truthy value to keep the key, and a falsy value otherwise.
   * @param {IDBCursorDirection} config.direction
   *
   * @returns {Promise<Array<{primaryKey: any, key: any}>>}
   */
  async #getKeys({ storeName, key, indexName, filterFn, direction }) {
    /** @type {Array<{primaryKey: any, key: any}>} */
    const keys = [];
    return new Promise((resolve, reject) => {
      const transaction = this.#requireDB().transaction(
        [storeName],
        "readonly"
      );
      const store = transaction.objectStore(storeName);
      const request = (
        indexName ? store.index(indexName) : store
      ).openKeyCursor(key ?? undefined, direction);
      request.onerror = () => reject(request.error);
      request.onsuccess = () => {
        const cursor = request.result;

        if (cursor) {
          if (!filterFn || filterFn(cursor)) {
            keys.push({ primaryKey: cursor.primaryKey, key: cursor.key });
          }

          cursor.continue();
        } else {
          resolve(keys);
        }
      };
    });
  }

  /**
   * Generic method to check if data exists from a specified object store.
   *
   * @param {object} config
   * @param {string} config.storeName
   * @param {StoreKey} config.key
   * @param {?string} [config.indexName]
   * @returns {Promise<boolean>} A promise that resolves with `true` if the key exists, otherwise `false`.
   */
  async #hasData({ storeName, key, indexName }) {
    return new Promise((resolve, reject) => {
      const transaction = this.#requireDB().transaction(
        [storeName],
        "readonly"
      );
      const store = transaction.objectStore(storeName);
      const request = (indexName ? store.index(indexName) : store).getKey(key);
      request.onerror = () => reject(request.error);
      request.onsuccess = () => resolve(request.result !== undefined);
    });
  }

  /**
   * Used in tests
   *
   * @param {string} storeName
   * @param {StoreKey} key
   */
  async _testGetData(storeName, key) {
    return (await this.#getData({ storeName, key }))[0];
  }

  /**
   * Generic method to update data in a specified object store.
   *
   * @param {string} storeName - The name of the object store.
   * @param {object} data - The data to store.
   * @returns {Promise<void>}
   */
  async #updateData(storeName, data) {
    return new Promise((resolve, reject) => {
      const transaction = this.#requireDB().transaction(
        [storeName],
        "readwrite"
      );
      const store = transaction.objectStore(storeName);
      store.put(data);
      transaction.oncomplete = () => resolve();
      transaction.onabort = () =>
        reject(
          transaction.error ??
            new DOMException("The transaction was aborted.", "AbortError")
        );
    });
  }

  /**
   * Deletes a specific cache entry.
   *
   * @param {string} storeName
   * @param {StoreKey} key
   * @returns {Promise<void>}
   */
  async #deleteData(storeName, key) {
    return new Promise((resolve, reject) => {
      const transaction = this.#requireDB().transaction(
        [storeName],
        "readwrite"
      );
      const store = transaction.objectStore(storeName);
      const request = store.delete(key);
      request.onerror = () => reject(request.error);
      request.onsuccess = () => resolve();
    });
  }

  /**
   * Generates an IndexedDB query to retrieve entries from the appropriate index.
   *
   * @param {object} config - Configuration object.
   * @param {?string} [config.taskName] - The name of the inference task. Retrieves all tasks if null.
   * @param {?string} [config.model] - The model name (organization/name). Retrieves all models if null.
   * @param {?string} [config.revision] - The model revision. Retrieves all revisions if null.
   *
   * @returns {{key: StoreKey, indexName: ?string}} The query key
   *   and the name of the index to run it against.
   */
  #getFileQuery({ taskName, model, revision }) {
    // See https://developer.mozilla.org/en-US/docs/Web/API/IDBKeyRange for explanation on the query.

    // Case 1: Query to retrieve all entries matching taskName, model, and revision
    if (taskName && model && revision) {
      return {
        key: [taskName, model, revision],
        indexName: this.#indices.taskModelRevisionIndex.name,
      };
    }
    // Case 2: Query to retrieve all entries with taskName
    if (taskName && !model && !revision) {
      return {
        key: IDBKeyRange.bound([taskName], [taskName, []]),
        indexName: this.#indices.taskModelRevisionIndex.name,
      };
    }
    // Case 3: Query to retrieve all entries matching model and revision
    if (!taskName && model && revision) {
      return {
        key: [model, revision],
        indexName: this.#indices.modelRevisionIndex.name,
      };
    }
    // Case 4: Query to retrieve all entries
    if (!taskName && !model && !revision) {
      return { key: null, indexName: null };
    }
    // Case 5: Query to retrieve all entries matching taskName and model
    if (taskName && model && !revision) {
      return {
        key: IDBKeyRange.bound([taskName, model], [taskName, model, []]),
        indexName: this.#indices.taskModelRevisionIndex.name,
      };
    }
    throw new Error("Invalid query configuration.");
  }

  /**
   * Checks if a specified model file exists in storage.
   *
   * @param {object} config
   * @param {string} config.model - The model name (organization/name)
   * @param {string} config.revision - The model revision.
   * @param {string} config.file - The file name.
   * @returns {Promise<boolean>} A promise that resolves with `true` if the key exists, otherwise `false`.
   */
  async fileExists({ model, revision, file }) {
    // First, check if the file is in the headers store
    const fileMetadataExists = await this.#hasData({
      storeName: this.headersStoreName,
      key: [model, revision, file],
    });

    if (!fileMetadataExists) {
      return false;
    }

    // Now check if we have the file in OPFS
    const localFilePath = this.generateFilePathInOPFS({
      model,
      revision,
      file,
    });

    lazy.console.debug(
      "ModelHub: Checking if file exists in OPFS: " + localFilePath
    );

    try {
      const fileHandle = await lazy.OPFS.getFileHandle(localFilePath);

      if (!fileHandle) {
        // The file is not in OPFS and is in IndexDB...
        // TODO: we should clean up
        lazy.console.debug(
          "ModelHub: The file is not in OPFS and is in IndexDB..."
        );
        return false;
      }
    } catch (e) {
      return false;
    }
    return true;
  }

  /**
   * Generate the path where a model file will be stored in Origin Private FileSystem (OPFS).
   *
   * @param {object} config
   * @param {string} config.model - The model name (organization/name)
   * @param {string} config.revision - The model revision.
   * @param {string} config.file - The file name.
   * @returns {string} The generated file path.
   */
  generateFilePathInOPFS({ model, revision, file }) {
    return `${this.dbName}/${model}/${revision}/${file}`;
  }

  /**
   * Retrieves the headers for a specific cache entry.
   *
   * @param {object} config
   * @param {string} config.model - The model name (organization/name)
   * @param {string} config.revision - The model revision.
   * @param {string} config.file - The file name.
   * @returns {Promise<ModelFileHeaders|undefined>} The headers, or undefined if not found.
   */
  async getHeaders({ model, revision, file }) {
    return (
      await this.#getData({
        storeName: this.headersStoreName,
        key: [model, revision, file],
      })
    )[0]?.headers;
  }

  /**
   * Sets the headers for a specific cache entry.
   *
   * @param {object} config
   * @param {string} config.model - The model name (organization/name)
   * @param {string} config.revision - The model revision.
   * @param {string} config.file - The file name.
   * @param {ModelFileHeaders} config.headers - The headers to set.
   * @returns {Promise<void>} A promise that resolves when the headers are set.
   */
  async setHeaders({ model, revision, file, headers }) {
    return await this.#updateData(this.headersStoreName, {
      model,
      revision,
      file,
      headers,
    });
  }

  /**
   * Retrieves the file for a specific cache entry.
   *
   * @param {object} config
   * @param {string} config.model - The model name (organization/name)
   * @param {string} config.revision - The model revision.
   * @param {string} config.file - The file name.
   * @param {string} [config.engineId] - The engine Id. Defaults to "default-engine"
   * @returns {Promise<[Blob, ModelFileHeaders]|null>} The file Blob and its headers or null if not found.
   */
  async getFile({ engineId = lazy.DEFAULT_ENGINE_ID, model, revision, file }) {
    const headers = await this.getHeaders({ model, revision, file });

    if (headers) {
      await this.#updateEngines({
        engineId,
        model,
        revision,
        file,
      });

      const fileData = await (
        await lazy.OPFS.getFileHandle(
          this.generateFilePathInOPFS({ model, revision, file })
        )
      ).getFile();

      // mark the last used header value
      headers.lastUsed = Date.now();
      await this.setHeaders({ model, revision, file, headers });

      return [fileData, headers];
    }
    return null; // Return null if no file is found
  }

  /**
   * @param {object} config
   * @param {string} config.engineId
   * @param {string} config.model
   * @param {string} config.revision
   * @param {string} config.file
   * @returns {Promise<void>}
   */
  #updateEngines({ engineId, model, revision, file }) {
    return new Promise((resolve, reject) => {
      const transaction = this.#requireDB().transaction(
        [this.enginesStoreName],
        "readwrite"
      );
      const store = transaction.objectStore(this.enginesStoreName);
      const getRequest = store.get([model, revision, file]);
      transaction.oncomplete = () => resolve();
      transaction.onabort = () =>
        reject(
          transaction.error ??
            new DOMException("The transaction was aborted.", "AbortError")
        );
      getRequest.onsuccess = () => {
        const stored = /** @type {EngineRecord} */ (getRequest.result);
        const engineIds = stored?.engineIds ? [...stored.engineIds] : [];
        if (!engineIds.includes(engineId)) {
          engineIds.push(engineId);
        }
        store.put({ engineIds, model, revision, file });
      };
    });
  }

  /**
   * Adds or updates task entry.
   *
   * @param {object} config
   * @param {string} config.taskName - name of the inference task.
   * @param {string} config.model - The model name (organization/name).
   * @param {string} config.revision - The model version.
   * @param {string} config.file - The file name.
   * @param {string} [config.engineId] - ID of the engine
   * @returns {Promise<void>}
   */
  async updateTask({
    engineId = lazy.DEFAULT_ENGINE_ID,
    taskName,
    model,
    revision,
    file,
  }) {
    await this.#updateEngines({
      engineId,
      model,
      revision,
      file,
    });

    await this.#updateData(this.taskStoreName, {
      taskName,
      model,
      revision,
      file,
    });
  }

  /**
   * Estimate the disk size in bytes used by the system origin.
   *
   * @returns {Promise<number>} The estimated size.
   */
  async #estimateUsage() {
    const { promise, resolve, reject } =
      /** @type {PromiseWithResolvers<number>} */ (Promise.withResolvers());
    try {
      Services.qms.getUsageForPrincipal(
        Services.scriptSecurityManager.getSystemPrincipal(),
        request => {
          if (request.resultCode == Cr.NS_OK) {
            const result = /** @type {nsIQuotaOriginUsageResult} */ (
              request.result
            );
            resolve(result.usage);
          } else {
            reject(new Error(String(request.resultCode)));
          }
        }
      );
    } catch (error) {
      reject(new Error(`An unexpected error occurred: ${error.message}`));
    }

    return promise;
  }

  /**
   * Adds or updates a cache entry.
   *
   * @param {object} config
   * @param {string} config.taskName - name of the inference task.
   * @param {string} config.model - The model name (organization/name).
   * @param {string} config.revision - The model version.
   * @param {string} config.file - The file name.
   * @param {Blob | string} config.data - The content or path to the data to cache.
   * @param {string} [config.engineId] - ID of the engine.
   * @param {ModelFileHeaders} [config.headers] - The headers for the file.
   * @returns {Promise<number>} Resolves with the timestamp of the update.
   */
  async put({
    engineId = lazy.DEFAULT_ENGINE_ID,
    taskName,
    model,
    revision,
    file,
    data,
    headers,
  }) {
    const updatePromises = [];
    const fileSize = headers?.fileSize ?? /** @type {Blob} */ (data).size;
    const cacheKey = this.generateFilePathInOPFS({ model, revision, file });
    const totalSize = await this.#estimateUsage();

    if (totalSize + fileSize > this.#maxSize * ONE_GIB) {
      throw new Error(`Exceeding cache size limit of ${this.#maxSize}GiB"`);
    }

    // Store the file data if a blob is passed.
    if (Blob.isInstance(data) && !File.isInstance(data)) {
      updatePromises.push(
        data.stream().pipeTo(
          await lazy.OPFS.getFileHandle(cacheKey, {
            create: true,
          }).then(handle =>
            handle.createWritable({
              keepExistingData: false,
            })
          )
        )
      );
    }

    // Store task metadata
    updatePromises.push(
      this.updateTask({
        engineId,
        taskName,
        model,
        revision,
        file,
      })
    );

    const currentTimeSinceEpoch = Date.now();

    // Update headers store - whith defaults for ETag and Content-Type
    const updatedHeaders = headers ?? {};
    updatedHeaders["Content-Type"] =
      updatedHeaders["Content-Type"] ?? DEFAULT_CONTENT_TYPE;
    updatedHeaders.fileSize = fileSize;
    updatedHeaders.ETag = updatedHeaders.ETag ?? NO_ETAG;
    updatedHeaders.lastUpdated = currentTimeSinceEpoch;
    updatedHeaders.lastUsed = currentTimeSinceEpoch;

    // filter out any keys that are not allowed
    const storedHeaders = Object.fromEntries(
      Object.entries(updatedHeaders).filter(([key]) =>
        ALLOWED_HEADERS_KEYS.includes(key)
      )
    );

    lazy.console.debug(`Storing ${cacheKey} with headers:`, storedHeaders);

    // Update headers
    updatePromises.push(
      this.#updateData(this.headersStoreName, {
        model,
        revision,
        file,
        headers: storedHeaders,
      })
    );

    await Promise.all(updatePromises);
    return currentTimeSinceEpoch;
  }

  /**
   * Deletes files associated with a specific engine ID.
   * If the engine ID is the only one associated with a file, the file is deleted.
   * Otherwise, the engine ID is removed from the file's engine list.
   *
   * @async
   *
   * @param {object} config
   * @param {string} config.engineId - The ID of the engine whose files are to be deleted.
   * @param {string} [config.deletedBy] - The feature who deleted the model
   * @returns {Promise<void>} A promise that resolves once the deletion process is complete.
   */
  async deleteFilesByEngine({ engineId, deletedBy = "other" }) {
    const items = /** @type {Array<EngineRecord>} */ (
      await this.#getData({ storeName: this.enginesStoreName })
    );

    /** @type {Map<string, Set<string>>} */
    const filesToDelete = new Map();

    for (const item of items) {
      if (!item.engineIds?.includes(engineId)) {
        continue;
      }

      // Always remove the engine from the association list.
      const newEngineIds = item.engineIds.filter(id => id !== engineId);

      // If no engines remain, queue physical deletion.
      if (newEngineIds.length === 0) {
        const key = JSON.stringify([item.model, item.revision]);
        const set = filesToDelete.get(key) ?? new Set();
        set.add(item.file);
        filesToDelete.set(key, set);
      } else {
        // Update the item with the new engine ids
        await this.#updateData(this.enginesStoreName, {
          engineIds: newEngineIds,
          model: item.model,
          revision: item.revision,
          file: item.file,
        });
      }
    }

    // Delete queued files grouped by (model, revision)
    for (const [key, fileSet] of filesToDelete) {
      const [model, revision] = JSON.parse(key);

      await this.deleteModels({
        model,
        revision,
        filterFn: record => fileSet.has(record.file),
      });

      Glean.firefoxAiRuntime.modelDeletion.record({
        modelId: model,
        modelRevision: revision,
        deletedBy,
      });
    }
  }

  /**
   * Deletes a file and its associated data across various storage locations.
   *
   * @async
   * @param {object} file - The file object containing model, revision, and file details.
   * @param {string} file.model - The model associated with the file.
   * @param {string} file.revision - The revision of the file.
   * @param {string} file.file - The filename or unique identifier for the file.
   * @returns {Promise<void>} A promise that resolves once the file and associated data are deleted.
   */
  async #deleteFile({ model, revision, file }) {
    const owner = ModelOwner.fromModel(model);

    await Promise.all([
      // For now we delete the icon file any time a file from a model is removed.
      owner.pruneCache(),
      this.#deleteData(this.headersStoreName, [model, revision, file]),
      this.#deleteData(this.enginesStoreName, [model, revision, file]),
      lazy.OPFS.remove(this.generateFilePathInOPFS({ model, revision, file }), {
        ignoreErrors: true,
      }),
    ]);
  }

  /**
   * Deletes all data related to the specifed models.
   *
   * @param {object} config
   *
   * @param {?string} [config.model] - The model name (organization/name) to delete.
   * @param {?string} [config.revision] - The model version to delete.
   *  If both model and revision are null, delete models of any name and version.
   *
   * @param {?string} [config.taskName] - name of the inference task to delete.
   *                                    If null, delete specified models for all tasks.
   *
   * @param {?function(TaskRecord):boolean} [config.filterFn] - A function to execute for each model file candidate for deletion.
   * It should return a truthy value to delete the model file, and a falsy value otherwise.
   *
   * @param {?string} [config.deletedBy] - The feature who deleted the model
   *
   * @throws {Error} If a revision is defined, the model must also be defined.
   *                 If the model is not defined, the revision should also not be defined.
   *                 Otherwise, an error will be thrown.

   * @returns {Promise<void>}
   */
  async deleteModels({ taskName, model, revision, filterFn, deletedBy }) {
    Glean.firefoxAiRuntime.modelDeletion.record({
      modelId: model ?? undefined,
      modelRevision: revision ?? undefined,
      deletedBy: deletedBy ?? undefined,
    });

    const tasks = /** @type {Array<TaskRecord>} */ (
      await this.#getData({
        storeName: this.taskStoreName,
        ...this.#getFileQuery({ taskName, model, revision }),
      })
    );

    if (!tasks.length) {
      lazy.console.debug("No models to delete found in task store", {
        taskName,
        model,
        revision,
      });
    }

    let deletePromises = [];
    const filesToMaybeDelete = new Set();
    for (const task of tasks) {
      if (
        filterFn &&
        !filterFn({
          taskName: task.taskName,
          model: task.model,
          revision: task.revision,
          file: task.file,
        })
      ) {
        continue;
      }
      filesToMaybeDelete.add(
        JSON.stringify([task.model, task.revision, task.file])
      );

      deletePromises.push(
        this.#deleteData(this.taskStoreName, [
          task.taskName,
          task.model,
          task.revision,
          task.file,
        ])
      );
    }
    await Promise.all(deletePromises);

    deletePromises = [];

    const remainingFileKeys = await this.#getKeys({
      storeName: this.taskStoreName,
      indexName: this.#indices.modelRevisionFileIndex.name,
      direction: "nextunique", // One row per model/revision, not per task.
    });

    const remainingFiles = new Set();

    for (const { key } of remainingFileKeys) {
      remainingFiles.add(JSON.stringify(key));
    }

    const filesToDelete = filesToMaybeDelete.difference(remainingFiles);

    const deletedPaths = [];

    for (const key of filesToDelete) {
      const [modelValue, revisionValue, fileValue] = JSON.parse(key);
      deletedPaths.push(
        this.generateFilePathInOPFS({
          model: modelValue,
          revision: revisionValue,
          file: fileValue,
        })
      );
      deletePromises.push(
        this.#deleteFile({
          model: modelValue,
          revision: revisionValue,
          file: fileValue,
        })
      );
    }
    await Promise.all(deletePromises);

    // Pruned after every file is gone rather than inside #deleteFile, so that
    // files sharing a directory do not race each other to remove it.
    for (const path of deletedPaths) {
      await lazy.OPFS.pruneEmptyDirectories(path, this.dbName);
    }

    if (deletePromises.length) {
      lazy.console.debug(
        `Deleted model ${model} (${deletePromises.length} files.)`
      );
    }
  }

  /**
   * Lists all files for a given model and revision stored in the cache,
   * and aggregates metadata from the file headers.
   *
   * When a `taskName` is provided, the method retrieves all model/revision
   * pairs associated with that task; otherwise, it uses the provided `model`
   * and `revision`. It then queries the store to retrieve file information (path
   * and headers) and aggregates metadata (totalSize, lastUsed, updateDate, engineIds)
   * across all files.
   *
   * @param {object} config - The configuration for querying the files.
   * @param {?string} [config.model] - The model name (in "organization/name" format).
   * @param {?string} [config.revision] - The model version.
   * @param {?string} [config.taskName] - The name of the inference task.
   * @returns {Promise<{
   *   files: Array<{ path: string, headers: ModelFileHeaders, engineIds: Array<string> }>,
   *   metadata: { totalSize: number, lastUsed: number, updateDate: number, engineIds: Array<string> }
   * }>} An object containing:
   *   - files: an array of file records with their path, headers, and engine IDs.
   *   - metadata: aggregated metadata computed from all the files.
   */
  async listFiles({ taskName, model, revision }) {
    // When not providing taskName, both model and revision must be defined.
    if (!taskName && (!model || !revision)) {
      throw new Error("Both model and revision must be defined");
    }

    // Determine which model/revision pairs we want files for.
    let modelRevisions =
      /** @type {Array<{model: string, revision: string}>} */ ([
        { model, revision },
      ]);
    if (taskName) {
      // Get all model/revision pairs associated with this task.
      const keysData = await this.#getKeys({
        storeName: this.taskStoreName,
        direction: "nextunique", // One row per model/revision, not per task.
        ...this.#getFileQuery({ taskName, model, revision }),
      });
      modelRevisions = keysData.map(({ key }) => ({
        model: key[1],
        revision: key[2],
      }));
    }

    // For each model/revision, query for headers data.
    const fileDataPromises = modelRevisions.map(async task => {
      const records = /** @type {Array<HeadersRecord>} */ (
        await this.#getData({
          storeName: this.headersStoreName,
          indexName: this.#indices.modelRevisionIndex.name,
          key: [task.model, task.revision],
        })
      );
      return records.map(record => ({ record, task }));
    });
    const fileData = (await Promise.all(fileDataPromises)).flat();

    // Initialize aggregated metadata.
    let totalFileSize = 0;
    let aggregatedLastUsed = 0;
    let aggregatedUpdateDate = 0;
    const aggregatedEngineIds = new Set();

    // Process each file entry.
    const files = [];
    for (const { record, task } of fileData) {
      const { file: path, headers } = record;
      const stored = /** @type {Array<EngineRecord>} */ (
        await this.#getData({
          storeName: this.enginesStoreName,
          key: [task.model, task.revision, path],
        })
      );

      const engineIds = stored.length ? (stored[0].engineIds ?? []) : [];
      for (const engineId of engineIds) {
        aggregatedEngineIds.add(engineId);
      }

      // Aggregate metadata.
      totalFileSize += headers.fileSize ?? 0;
      aggregatedLastUsed = Math.max(aggregatedLastUsed, headers.lastUsed ?? 0);
      aggregatedUpdateDate = Math.max(
        aggregatedUpdateDate,
        headers.lastUpdated ?? 0
      );
      files.push({ path, headers, engineIds });
    }

    return {
      files,
      metadata: {
        totalSize: totalFileSize,
        lastUsed: aggregatedLastUsed,
        updateDate: aggregatedUpdateDate,
        engineIds: Array.from(aggregatedEngineIds),
      },
    };
  }

  /**
   * Lists all models stored in the cache.
   *
   * @returns {Promise<Array<{name: string, revision: string, tasks: Array<string>}>>}
   *          An array of model identifiers.
   */
  async listModels() {
    const modelRevisions = await this.#getKeys({
      storeName: this.taskStoreName,
      indexName: this.#indices.modelRevisionIndex.name,
      // Keep every task's row, not just one per model/revision.
      direction: "next",
    });

    /** @type {Map<string, {name: string, revision: string, tasks: Array<string>}>} */
    const models = new Map();
    // The primary key is [taskName, model, revision, file], so the same model
    // appears once per task and file.
    for (const { primaryKey } of modelRevisions) {
      const [taskName, name, revision] = primaryKey;
      const id = JSON.stringify([name, revision]);

      let model = models.get(id);
      if (!model) {
        model = { name, revision, tasks: [] };
        models.set(id, model);
      }
      if (!model.tasks.includes(taskName)) {
        model.tasks.push(taskName);
      }
    }

    return Array.from(models.values());
  }
}

//exporting for testing purposes only
export const TestIndexedDBCache = IndexedDBCache;

export class ModelHub {
  /**
   * Tracks whether the last download of a session was successful.
   *
   * @type {Map<string, boolean>}
   */
  #lastDownloadOk = new Map();

  /**
   * Tracks the start time (ms) of each model download session, used to report
   * the total download time for the whole model on completion.
   *
   * @type {Map<string, number>}
   */
  #downloadStartTime = new Map();

  /**
   * Protect against simultaneous downloads.
   *
   * @type {Map<string, InFlightDownload>}
   */
  #inFlightDownloads = new Map();

  /**
   * Tracks pending cleanups for file downloads by URL.
   *
   * @type {Map<string, Promise<void>>}
   */
  #pendingCleanups = new Map();

  /**
   * Create an instance of ModelHub.
   *
   * @param {object} [config]
   * @param {string} [config.rootUrl] - Root URL used to download models.
   * @param {string} [config.urlTemplate] - The template to retrieve the full URL using a model name and revision.
   * @param {?Array<{filter: 'ALLOW'|'DENY', urlPrefix: string}>} [config.allowDenyList] - Array of URL patterns with filters.
   * @param {boolean} [config.reset=false] - Whether to reset the database.
   */
  constructor({
    rootUrl = lazy.DEFAULT_ROOT_URL,
    urlTemplate = lazy.DEFAULT_URL_TEMPLATE,
    allowDenyList = null,
    reset = false,
  } = {}) {
    this.rootUrl = rootUrl;
    /** @type {IndexedDBCache|null} */
    this.cache = null;

    // Ensures the URL template is well-formed and does not contain any invalid characters.
    const pattern = /^(?:\{\w+\}|\w+)(?:\/(?:\{\w+\}|\w+))*$/;
    //               ^                                         $   Start and end of string
    //                (?:\{\w+\}|\w+)                            Match a {placeholder} or alphanumeric characters
    //                                 (?:\/(?:\$\{\w+\}|\w+))*    Zero or more groups of a forward slash followed by a ${placeholder} or alphanumeric characters
    if (!pattern.test(urlTemplate)) {
      throw new Error(`Invalid URL template: ${urlTemplate}`);
    }
    this.urlTemplate = urlTemplate;
    if (Services.env.exists("MOZ_ALLOW_EXTERNAL_ML_HUB")) {
      this.allowDenyList = null;
    } else {
      this.allowDenyList = new lazy.URLChecker(allowDenyList ?? undefined);
    }
    this.reset = reset;
  }

  /**
   * @param {string} url
   */
  allowedURL(url) {
    if (this.allowDenyList === null) {
      return { allowed: true, rejectionType: lazy.RejectionType.NONE };
    }
    return this.allowDenyList.allowedURL(url);
  }

  /**
   * @returns {Promise<IndexedDBCache>}
   */
  async #initCache() {
    if (this.cache && this.cache.isOpen()) {
      return this.cache;
    }
    this.cache = await IndexedDBCache.init({ reset: this.reset });
    return this.cache;
  }

  /**
   * @param {string} url
   * @param {RequestInit} [options]
   * @returns {Promise<Response>}
   */
  async #fetch(url, options) {
    const result = this.allowedURL(url);
    if (result && !result.allowed) {
      throw new ForbiddenURLError(url, result.rejectionType);
    }

    return lazy.MLUtils.fetchUrl(url, options);
  }

  /**
   * Completely purge the IndexedDB-backed cache.
   *
   * This deletes the database using default parameters and waits until all
   * open connections are closed and the deletion completes.
   *
   * All OPFS files are also deleted.
   *
   * @returns {Promise<void>}
   * @throws {Error} If the database deletion fails or does not complete.
   */
  async purgeDatabase() {
    const cache = await this.#initCache();
    await IndexedDBCache.deleteDatabaseAndWait(
      cache.getPrincipal(),
      cache.dbName
    );
  }

  /**
   * This method takes a model URL and parses it to extract the
   * model name, optional model version, and file path.
   *
   * The expected URL format are :
   *
   * `/organization/model/revision/filePath`
   * `https://hub/organization/model/revision/filePath`
   *
   * @param {string} url - The full URL to the model, including protocol and domain - or the relative path.
   * @param {object} [options]
   * @param {string} [options.rootUrl]
   * @param {string} [options.urlTemplate]
   * @returns {object} An object containing the parsed components of the URL. The
   *                   object has properties `model`, `modelWithHostname` and `file`,
   *                   and optionally `revision` if the URL includes a version.
   * @throws {Error} Throws an error if the URL does not start with `this.rootUrl` or
   *                 if the URL format does not match the expected structure.
   *
   * @example
   * // For a URL
   * parseModelUrl("https://example.com/org1/model1/v1/file/path");
   * // returns { model: "org1/model1", modelWithHostname: "example.com/org1/model1", revision: "v1", file: "file/path" }
   *
   * @example
   * // For a relative URL
   * parseModelUrl("/org1/model1/revision/file/path");
   * // returns { model: "org1/model1", modelWithHostname: "example.com/org1/model1", revision: "v1", file: "file/path" }
   */
  parseUrl(url, options = {}) {
    let parts;
    const rootUrl = options.rootUrl || this.rootUrl;
    const urlTemplate =
      options.urlTemplate || this.urlTemplate || lazy.DEFAULT_URL_TEMPLATE;
    let hostname;

    // Check if the URL is relative or absolute
    if (url.startsWith("/")) {
      // relative URL
      parts = url.slice(1); // Remove leading slash
      hostname = new URL(rootUrl).hostname;
    } else {
      // absolute URL
      if (!url.startsWith(rootUrl)) {
        throw new Error(`Invalid domain for model URL: ${url}`);
      }
      const urlObject = new URL(url);
      hostname = urlObject.hostname;
      const rootUrlObject = new URL(rootUrl);

      // Remove the root URL's pathname from the full URL's pathname
      const relativePath = urlObject.pathname.substring(
        rootUrlObject.pathname.length
      );
      parts = relativePath.slice(1); // Remove leading slash
    }

    // Match the parts with the template
    const templateRegex = urlTemplate
      .replace("{model}", "(?<model>[^/]+/[^/]+)")
      .replace("{revision}", "(?<revision>[^/]+)");

    // Create a regex to match the structure
    const regex = new RegExp(`^${templateRegex}/(?<file>.+)$`);
    const match = parts.match(regex);

    if (!match || !match.groups) {
      throw new Error(`Invalid model URL format: ${url}`);
    }

    // Extract the matched parts
    const { model, revision, file } = match.groups;
    if (!model) {
      lazy.console.error(match);
      throw new Error("No model found from url: " + url);
    }
    if (!revision) {
      lazy.console.error(match);
      throw new Error("No revision found from url: " + url);
    }
    if (!file) {
      lazy.console.error(match);
      throw new Error("No file found from url: " + url);
    }
    if (!file || !file.length) {
      throw new Error(`Invalid model URL: ${url}`);
    }

    const modelWithHostname = `${hostname}/${model}`;
    return {
      model,
      revision,
      file,
      modelWithHostname,
    };
  }

  /**
   * Creates the file URL from the organization, model, and version.
   *
   * @param {object} config - The configuration object to be updated.
   * @param {string} config.model - model name
   * @param {string} config.revision - model revision
   * @param {string} config.file - filename
   * @param {string} [config.modelHubRootUrl] - root url of the model hub
   * @param {string} [config.modelHubUrlTemplate] - url template of the model hub
   * @returns {string} The full URL
   */
  #fileUrl({ model, revision, file, modelHubRootUrl, modelHubUrlTemplate }) {
    const rootUrl = modelHubRootUrl || this.rootUrl;
    return lazy.createFileUrl({
      model,
      revision,
      file,
      rootUrl,
      urlTemplate: modelHubUrlTemplate || this.urlTemplate,
      addDownloadParams: !rootUrl.startsWith(LOCAL_CHROME_PREFIX),
    });
  }

  /**
   * Checks the model and revision inputs.
   *
   * @param { string } model
   * @param { string } revision
   * @param { string } file
   * @returns { ?Error } The error instance(can be null)
   */
  static checkInput(model, revision, file) {
    // Matches a string with the format 'organization/model' or just 'model' where:
    // - 'organization' consists only of letters, digits, and hyphens, cannot start or end with a hyphen,
    //   and cannot contain consecutive hyphens.
    // - 'model' can contain letters, digits, hyphens, underscores, or periods.
    //
    // Pattern breakdown:
    //   ^                                     Start of string
    //    (?:                                  non-capturing group to make 'organization/' optional
    //      (?!-)                              Negative lookahead for 'organization' not starting with hyphen
    //           (?!.*--)                      Negative lookahead for 'organization' not containing consecutive hyphens
    //                   [A-Za-z0-9-]+         'organization' part: Alphanumeric characters or hyphens
    //                              (?<!-)     Negative lookbehind for 'organization' not ending with a hyphen
    //                                    \/   Literal '/' character separating 'organization' and 'model'
    //    )?                                   make 'organization/' group optional
    //                                      [A-Za-z0-9-_.]+    'model' part: Alphanumeric characters, hyphens, underscores, or periods
    //                                                    $    End of string
    const modelRegex =
      /^(?:(?!-)(?!.*--)[A-Za-z0-9-]+(?<!-)\/)?[A-Za-z0-9-_.]+$/;

    // Matches strings consisting of alphanumeric characters, hyphens, or periods.
    //
    //                    ^               $   Start and end of string
    //                     [A-Za-z0-9-.]+     Alphanum characters, hyphens, or periods, one or more times
    const versionRegex = /^[A-Za-z0-9-.]+$/;

    if (typeof model !== "string" || !modelRegex.test(model)) {
      return new Error("Invalid model name.");
    }

    if (
      !versionRegex.test(revision) ||
      revision.includes(" ") ||
      /[\^$]/.test(revision)
    ) {
      return new Error("Invalid version identifier.");
    }

    if (!lazy.FILE_REGEX.test(file)) {
      return new Error(`Invalid file name ${file}`);
    }

    return null;
  }

  /**
   * Deletes all model files for the specified task and model, except for the specified revision.
   *
   * @param {object} config - Configuration object.
   * @param {string} config.taskName - The name of the inference task.
   * @param {string} config.modelWithHostname - The model name (hostname/organization/name).
   * @param {string} config.targetRevision - The revision to keep.
   *
   * @returns {Promise<void>}
   */
  async deleteNonMatchingModelRevisions({
    taskName,
    modelWithHostname,
    targetRevision,
  }) {
    // Ensure all required parameters are provided
    if (!taskName || !modelWithHostname || !targetRevision) {
      throw new Error(
        "taskName, modelWithHostname, and targetRevision are required."
      );
    }

    const cache = await this.#initCache();

    // Delete models with revisions that do not match the targetRevision
    return cache.deleteModels({
      taskName,
      model: modelWithHostname,
      filterFn: record => record.revision !== targetRevision,
    });
  }

  /**
   * Returns the ETag value given an URL
   *
   * @param {string} url
   * @param {number} [timeout] in ms. Default is 1000
   * @returns {Promise<?string>} ETag (can be null)
   */
  async getETag(url, timeout = 1000) {
    const controller = new AbortController();
    const id = lazy.setTimeout(() => controller.abort(), timeout);

    try {
      const headResponse = await this.#fetch(url, {
        method: "HEAD",
        signal: controller.signal,
      });
      const currentEtag = headResponse.headers.get("ETag");
      return currentEtag;
    } catch (error) {
      if (error instanceof ForbiddenURLError) {
        throw error;
      }
      lazy.console.warn("An error occurred when calling HEAD:", error);
      return null;
    } finally {
      lazy.clearTimeout(id);
    }
  }

  /**
   * Given an organization, model, and version, fetch a model file in the hub as an blob.
   *
   * @param {object} config
   * @param {string} config.engineId - The model engine id
   * @param {string} config.taskName - name of the inference task.
   * @param {string} config.model - The model name (organization/name).
   * @param {string} config.revision - The model revision.
   * @param {string} config.file - The file name.
   * @param {string} config.modelHubRootUrl - root url of the model hub
   * @param {string} config.modelHubUrlTemplate - url template of the model hub
   * @param {?function(ProgressAndStatusCallbackParams):void} [config.progressCallback] A function to call to indicate progress status.
   * @param {?AbortSignal} [config.abortSignal] - AbortSignal to cancel the download.
   * @param {string} [config.featureId] - The feature ID requesting the model
   * @param {string} [config.sessionId] - The session ID for tracking this download
   * @returns {Promise<[Blob, ModelFileHeaders]>} The file content
   */
  async getModelFileAsBlob({
    engineId,
    taskName,
    model,
    revision,
    file,
    modelHubRootUrl,
    modelHubUrlTemplate,
    progressCallback,
    abortSignal,
    featureId,
    sessionId,
  }) {
    const [filePath, headers] = await this.getModelDataAsFile({
      engineId,
      taskName,
      model,
      revision,
      file,
      modelHubRootUrl,
      modelHubUrlTemplate,
      progressCallback,
      abortSignal,
      featureId: featureId || engineId,
      sessionId: sessionId || `${engineId}-${Date.now()}`,
    });

    const fileObject = await (
      await lazy.OPFS.getFileHandle(filePath)
    ).getFile();

    // A file is a blob, so we can return it directly.
    return [fileObject, headers];
  }

  /**
   * Given an organization, model, and version, fetch a model file in the hub as an ArrayBuffer
   * while supporting status callback.
   *
   * @param {object} config
   * @param {string} config.engineId - The model engine id
   * @param {string} config.taskName - name of the inference task.
   * @param {string} config.model - The model name (organization/name).
   * @param {string} config.revision - The model revision.
   * @param {string} config.file - The file name.
   * @param {string} config.modelHubRootUrl - root url of the model hub
   * @param {string} config.modelHubUrlTemplate - url template of the model hub
   * @param {?function(ProgressAndStatusCallbackParams):void} [config.progressCallback] A function to call to indicate progress status.
   * @param {?AbortSignal} [config.abortSignal] - AbortSignal to cancel the download.
   * @returns {Promise<[ArrayBuffer, ModelFileHeaders]>} The file content
   */
  async getModelFileAsArrayBuffer({
    engineId,
    taskName,
    model,
    revision,
    file,
    modelHubRootUrl,
    modelHubUrlTemplate,
    progressCallback,
    abortSignal,
  }) {
    let [blob, headers] = await this.getModelFileAsBlob({
      engineId,
      taskName,
      model,
      revision,
      file,
      modelHubRootUrl,
      modelHubUrlTemplate,
      progressCallback,
      abortSignal,
    });

    return [await blob.arrayBuffer(), headers];
  }

  /**
   * @param {Response} response
   * @returns {ModelFileHeaders}
   */
  extractHeaders(response) {
    return {
      // We don't store the boundary or the charset, just the content type,
      // so we drop what's after the semicolon.
      "Content-Type": (
        response.headers.get("Content-Type") || DEFAULT_CONTENT_TYPE
      )
        .split(";")[0]
        .trim(),
      "Content-Length": response.headers.get("Content-Length"),
      ETag: response.headers.get("ETag"),
    };
  }

  /**
   * Notify that a model download is complete.
   *
   * @param {object} config
   * @param {string} config.engineId - The engine id.
   * @param {string} config.model - The model name (organization/name).
   * @param {string} config.revision - The model revision.
   * @param {string} config.featureId - The engine id.
   * @param {string} config.sessionId - Shared across the same model download session.
   * @returns {Promise<void>}
   */
  async notifyModelDownloadComplete({
    engineId,
    model,
    revision,
    featureId,
    sessionId,
  }) {
    // Allows multiple calls to notifyModelDownloadComplete to work as expected
    // Also, we don't want to signal model download end if there was no start
    if (!this.#lastDownloadOk.has(sessionId)) {
      return;
    }
    const isSuccess = this.#lastDownloadOk.get(sessionId);
    const step = isSuccess ? "end_download_success" : "end_download_failed";
    this.#lastDownloadOk.delete(sessionId);
    const startTime = this.#downloadStartTime.get(sessionId);
    this.#downloadStartTime.delete(sessionId);
    const duration =
      startTime === undefined ? 0 : Math.floor(ChromeUtils.now() - startTime);
    Glean.firefoxAiRuntime.modelDownload.record({
      modelDownloadId: sessionId,
      featureId,
      engineId,
      modelId: model,
      step,
      duration,
      modelRevision: revision,
      error: isSuccess
        ? ""
        : "Unable to retrieve all files needed for the model to work",
    });
  }

  /**
   * Given an organization, model, and version, fetch a model file in the hub
   * while supporting status callback.
   *
   * @param {GetModelDataAsFileConfig} config
   * @returns {Promise<[string, ModelFileHeaders]>} The local path to the file content and headers.
   */
  async getModelDataAsFile(config) {
    const checkError = ModelHub.checkInput(
      config.model,
      config.revision,
      config.file
    );
    if (checkError) {
      throw checkError;
    }

    if (config.abortSignal?.aborted) {
      throw (
        config.abortSignal.reason ??
        new DOMException("The operation was aborted.", "AbortError")
      );
    }

    const key = this.#fileUrl({
      model: config.model,
      revision: config.revision,
      file: config.file,
      modelHubRootUrl: config.modelHubRootUrl,
      modelHubUrlTemplate: config.modelHubUrlTemplate,
    });

    const pendingCleanup = this.#pendingCleanups.get(key);
    if (pendingCleanup) {
      await this.#raceAbort(pendingCleanup, config.abortSignal);
    }

    let inFlight = this.#inFlightDownloads.get(key);
    if (!inFlight) {
      // This code is a bit complex to satisfy TypeScript since the new InFlightDownload
      // object must be captured by itself for the progressCallback.

      /** @type {InFlightDownload} */
      const capturedInFlight = {
        promise:
          /** @type {Promise<[string, ModelFileHeaders]>} */
          (/** @type {unknown} */ (null)),
        progressCallbacks: new Map(),
        controller: new AbortController(),
        waiters: new Set(),
        downloadStarted: false,
        downloadStartedAt: 0,
        duration: 0,
        lastProgress: null,
      };

      capturedInFlight.promise = this.#fetchModelDataAsFile(
        {
          ...config,
          abortSignal: capturedInFlight.controller.signal,
          progressCallback: rawProgress => {
            capturedInFlight.lastProgress = rawProgress;
            for (const callback of capturedInFlight.progressCallbacks.values()) {
              try {
                callback(rawProgress);
              } catch (error) {
                lazy.console.error("Progress callback threw:", error);
              }
            }
          },
        },
        capturedInFlight
      ).finally(() => {
        // leave() may have already replaced this entry. Don't delete that one.
        if (this.#inFlightDownloads.get(key) === capturedInFlight) {
          this.#inFlightDownloads.delete(key);
        }
      });

      inFlight = capturedInFlight;
      this.#inFlightDownloads.set(key, inFlight);
    }

    return this.#joinInFlightDownload(key, inFlight, config);
  }

  /**
   * Wait for a promise, but reject immediately with AbortError if the given
   * signal fires first, without waiting for the promise itself to settle.
   *
   * @param {Promise<void>} promise
   * @param {?AbortSignal} [abortSignal]
   * @returns {Promise<void>}
   */
  #raceAbort(promise, abortSignal) {
    if (!abortSignal) {
      return promise;
    }
    if (abortSignal.aborted) {
      return Promise.reject(
        abortSignal.reason ??
          new DOMException("The operation was aborted.", "AbortError")
      );
    }
    return new Promise((resolve, reject) => {
      const onAbort = () => {
        reject(
          abortSignal.reason ??
            new DOMException("The operation was aborted.", "AbortError")
        );
      };
      abortSignal.addEventListener("abort", onAbort, { once: true });
      promise.then(
        value => {
          abortSignal.removeEventListener("abort", onAbort);
          resolve(value);
        },
        error => {
          abortSignal.removeEventListener("abort", onAbort);
          reject(error);
        }
      );
    });
  }

  /**
   * Catch a caller up on progress it missed by joining after the shared
   * fetch already started. Catches exceptions so a broken callback can't
   * block delivery to the other callers.
   *
   * @param {InFlightDownload} inFlight
   * @param {function(ProgressAndStatusCallbackParams):void} progressCallback
   * @param {?AbortSignal} [abortSignal]
   */
  #replayProgressForJoiner(inFlight, progressCallback, abortSignal) {
    const raw = inFlight.lastProgress;
    if (!raw) {
      return;
    }
    try {
      progressCallback(
        raw.cloneWith({
          statusText: lazy.Progress.ProgressStatusText.INITIATE,
          progress: null,
          totalLoaded: null,
          currentLoaded: null,
          total: null,
        })
      );
    } catch (error) {
      lazy.console.error("Progress callback threw:", error);
    }
    if (
      abortSignal?.aborted ||
      raw.statusText === lazy.Progress.ProgressStatusText.INITIATE
    ) {
      return;
    }
    try {
      progressCallback(
        raw.cloneWith({
          statusText: lazy.Progress.ProgressStatusText.SIZE_ESTIMATE,
          currentLoaded: raw.totalLoaded,
        })
      );
    } catch (error) {
      lazy.console.error("Progress callback threw:", error);
    }
  }

  /**
   * @param {object} config
   * @param {string} config.sessionId
   * @param {string} config.featureId
   * @param {string} config.engineId
   * @param {string} config.model
   * @param {string} config.revision
   * @param {object} [config.telemetryData]
   * @param {boolean} config.success
   * @param {Error} [config.error]
   * @param {number} config.duration
   * @param {number} config.startedAt - When the shared download actually began.
   */
  #recordDownloadAttempt({
    sessionId,
    featureId,
    engineId,
    model,
    revision,
    telemetryData = {},
    success,
    error,
    duration,
    startedAt,
  }) {
    if (!this.#lastDownloadOk.has(sessionId)) {
      this.#downloadStartTime.set(sessionId, startedAt);
      Glean.firefoxAiRuntime.modelDownload.record({
        modelDownloadId: sessionId,
        featureId,
        engineId,
        modelId: model,
        step: "start_download",
        duration: 0,
        modelRevision: revision,
        error: "",
        ...telemetryData,
      });
    }
    this.#lastDownloadOk.set(sessionId, success);

    Glean.firefoxAiRuntime.modelDownload.record({
      modelDownloadId: sessionId,
      featureId,
      engineId,
      modelId: model,
      step: "start_file_download",
      duration: 0,
      modelRevision: revision,
      error: "",
      ...telemetryData,
    });

    Glean.firefoxAiRuntime.modelDownload.record({
      modelDownloadId: sessionId,
      featureId,
      engineId,
      modelId: model,
      step: success ? "end_file_download_success" : "end_file_download_failed",
      duration,
      modelRevision: revision,
      error: success ? "" : ((error?.cause ?? error)?.constructor?.name ?? ""),
      ...telemetryData,
    });
  }

  /**
   * @param {string} key
   * @param {InFlightDownload} inFlight
   */
  #registerPendingCleanup(key, inFlight) {
    const cleanup = inFlight.promise.then(
      () => {},
      error => {
        if (error?.name === "AbortError") {
          return;
        }
        throw error;
      }
    );
    this.#pendingCleanups.set(key, cleanup);
    cleanup
      .catch(() => {})
      .finally(() => {
        if (this.#pendingCleanups.get(key) === cleanup) {
          this.#pendingCleanups.delete(key);
        }
      });
  }

  /**
   * Join a download already in flight, tracking this caller's progress
   * callback and abortSignal independently of every other caller sharing it.
   *
   * @param {string} key
   * @param {InFlightDownload} inFlight
   * @param {GetModelDataAsFileConfig} config
   * @returns {Promise<[string, ModelFileHeaders]>}
   */
  #joinInFlightDownload(
    key,
    inFlight,
    {
      progressCallback,
      abortSignal,
      engineId,
      taskName,
      model,
      revision,
      file,
      featureId,
      sessionId,
      telemetryData,
    }
  ) {
    const waiter = {};
    inFlight.waiters.add(waiter);
    if (progressCallback) {
      inFlight.progressCallbacks.set(waiter, progressCallback);
      this.#replayProgressForJoiner(inFlight, progressCallback, abortSignal);
    }

    const recordEngineReference = async () => {
      const hostname = new URL(key).hostname;
      const cache = await this.#initCache();
      await cache.updateTask({
        engineId,
        taskName,
        model: `${hostname}/${model}`,
        revision,
        file,
      });
    };

    /**
     * @param {boolean} success
     * @param {Error} [error]
     */
    const recordTelemetry = (success, error) => {
      if (!inFlight.downloadStarted) {
        return;
      }
      this.#recordDownloadAttempt({
        sessionId,
        featureId,
        engineId,
        model,
        revision,
        telemetryData,
        success,
        error,
        duration: inFlight.duration,
        startedAt: inFlight.downloadStartedAt,
      });
    };

    /**
     * Returns whether another waiter is still relying on the shared fetch.
     */
    const leave = () => {
      inFlight.waiters.delete(waiter);
      if (progressCallback) {
        inFlight.progressCallbacks.delete(waiter);
      }
      if (inFlight.waiters.size) {
        return true;
      }
      if (this.#inFlightDownloads.get(key) === inFlight) {
        this.#inFlightDownloads.delete(key);
      }
      this.#registerPendingCleanup(key, inFlight);
      inFlight.controller.abort();
      return false;
    };

    if (!abortSignal) {
      return inFlight.promise
        .then(
          async value => {
            await recordEngineReference();
            recordTelemetry(true);
            return value;
          },
          error => {
            recordTelemetry(false, error);
            throw error;
          }
        )
        .finally(leave);
    }

    return new Promise((resolve, reject) => {
      let settled = false;

      const onAbort = () => {
        if (leave()) {
          settled = true;
          const abortError =
            abortSignal.reason ??
            new DOMException("The operation was aborted.", "AbortError");
          recordTelemetry(false, abortError);
          reject(abortError);
        }
      };

      if (abortSignal.aborted) {
        // An already-fired 'abort' event has nothing left to listen for.
        onAbort();
      } else {
        abortSignal.addEventListener("abort", onAbort, { once: true });
      }

      inFlight.promise.then(
        value => {
          if (settled) {
            return;
          }
          abortSignal.removeEventListener("abort", onAbort);
          leave();
          recordEngineReference().then(() => {
            recordTelemetry(true);
            resolve(value);
          }, reject);
        },
        error => {
          if (settled) {
            return;
          }
          abortSignal.removeEventListener("abort", onAbort);
          leave();
          recordTelemetry(false, error);
          reject(error);
        }
      );
    });
  }

  /**
   * @param {GetModelDataAsFileConfig} config
   * @param {InFlightDownload} inFlight
   * @returns {Promise<[string, ModelFileHeaders]>} The local path to the file content and headers.
   */
  async #fetchModelDataAsFile(
    {
      engineId,
      taskName,
      model,
      revision,
      file,
      modelHubRootUrl,
      modelHubUrlTemplate,
      progressCallback,
      abortSignal,
    },
    inFlight
  ) {
    // Make sure inputs are clean. We don't sanitize them but throw an exception
    let checkError = ModelHub.checkInput(model, revision, file);
    if (checkError) {
      throw checkError;
    }
    const url = this.#fileUrl({
      model,
      revision,
      file,
      modelHubRootUrl,
      modelHubUrlTemplate,
    });
    lazy.console.debug(`Getting model file from ${url}`);

    const cache = await this.#initCache();

    // we store the hostname alongside the model so we can distinguished per hub
    const hostname = new URL(url).hostname;
    const modelWithHostname = `${hostname}/${model}`;

    let useCached;
    const chromeFile = url.startsWith("chrome://");
    const fileAllowed = this.allowedURL(url);
    /** @type {ModelFileHeaders|undefined} */
    let cachedHeaders;

    // If the revision is `main` we want to check the ETag in the hub
    if (revision === "main" && !chromeFile) {
      // this can be null if no ETag was found or there were a network error
      const hubETag = await this.getETag(url);

      // Storage ETag lookup
      cachedHeaders = await cache.getHeaders({
        model: modelWithHostname,
        revision,
        file,
      });
      const cachedEtag = cachedHeaders ? cachedHeaders.ETag : null;

      // If we have something in store, and the hub ETag is null or it matches the cached ETag, return the cached response
      useCached =
        cachedEtag !== null && (hubETag === null || cachedEtag === hubETag);
    } else {
      // If we are dealing with a pinned revision, we ignore the ETag, to spare HEAD hits on every call
      useCached = await cache.fileExists({
        model: modelWithHostname,
        revision,
        file,
      });
    }

    const progressInfo = {
      progress: null,
      totalLoaded: null,
      currentLoaded: null,
      total: null,
    };

    const statusInfo = {
      metadata: { model, revision, file, url },
      ok: true,
      id: url,
    };

    const localFilePath = cache.generateFilePathInOPFS({
      model: modelWithHostname,
      revision,
      file,
    });

    if (useCached) {
      if (!fileAllowed.allowed) {
        await cache.deleteModels({
          model: modelWithHostname,
          revision,
          deletedBy: "denylist",
        });
        throw new ForbiddenURLError(url, fileAllowed.rejectionType);
      }
      lazy.console.debug(`Cache Hit for ${url}`);
      progressCallback?.(
        new lazy.Progress.ProgressAndStatusCallbackParams({
          ...statusInfo,
          ...progressInfo,
          type: lazy.Progress.ProgressType.LOAD_FROM_CACHE,
          statusText: lazy.Progress.ProgressStatusText.INITIATE,
        })
      );

      if (!cachedHeaders) {
        cachedHeaders = await cache.getHeaders({
          model: modelWithHostname,
          revision,
          file,
        });
      }
      if (!cachedHeaders) {
        throw new Error(`Missing cached headers for the model file: ${url}`);
      }

      // Ensure that we indicate that the taskName is stored
      await cache.updateTask({
        engineId,
        taskName,
        model: modelWithHostname,
        revision,
        file,
      });

      progressCallback?.(
        new lazy.Progress.ProgressAndStatusCallbackParams({
          ...statusInfo,
          ...progressInfo,
          type: lazy.Progress.ProgressType.LOAD_FROM_CACHE,
          statusText: lazy.Progress.ProgressStatusText.DONE,
        })
      );

      cachedHeaders.lastUsed = Date.now();
      await cache.setHeaders({
        model: modelWithHostname,
        revision,
        file,
        headers: cachedHeaders,
      });

      return [localFilePath, cachedHeaders];
    }

    progressCallback?.(
      new lazy.Progress.ProgressAndStatusCallbackParams({
        ...statusInfo,
        ...progressInfo,
        type: lazy.Progress.ProgressType.DOWNLOAD,
        statusText: lazy.Progress.ProgressStatusText.INITIATE,
      })
    );

    inFlight.downloadStarted = true;
    const start = ChromeUtils.now();
    inFlight.downloadStartedAt = start;

    lazy.console.debug(`Fetching ${url}`);
    let caughtError;
    try {
      let isFirstCall = true;
      const response = await this.#fetch(url, { signal: abortSignal });
      const fileObject = await lazy.OPFS.download({
        savePath: localFilePath,
        deletePreviousVersions: false,
        useCache: false,
        source: response,
        abortSignal: abortSignal ?? undefined,
        progressCallback: progressData => {
          progressCallback?.(
            new lazy.Progress.ProgressAndStatusCallbackParams({
              ...progressInfo,
              ...progressData,
              statusText: isFirstCall
                ? lazy.Progress.ProgressStatusText.SIZE_ESTIMATE
                : lazy.Progress.ProgressStatusText.IN_PROGRESS,
              type: lazy.Progress.ProgressType.DOWNLOAD,
              ...statusInfo,
            })
          );
          isFirstCall = false;
        },
      });

      inFlight.duration = Math.floor(ChromeUtils.now() - start);

      const headers = this.extractHeaders(response);
      headers.fileSize = fileObject.size;

      await cache.put({
        engineId,
        taskName,
        model: modelWithHostname,
        revision,
        file,
        data: localFilePath,
        headers,
      });

      progressCallback?.(
        new lazy.Progress.ProgressAndStatusCallbackParams({
          ...statusInfo,
          ...progressInfo,
          type: lazy.Progress.ProgressType.DOWNLOAD,
          statusText: lazy.Progress.ProgressStatusText.DONE,
        })
      );

      return [localFilePath, headers];
    } catch (error) {
      caughtError = error;
      inFlight.duration = Math.floor(ChromeUtils.now() - start);
      lazy.console.error(`Failed to fetch ${url}:`, error);
    }

    // Indicate there is an error
    progressCallback?.(
      new lazy.Progress.ProgressAndStatusCallbackParams({
        ...statusInfo,
        ...progressInfo,
        type: lazy.Progress.ProgressType.DOWNLOAD,
        statusText: lazy.Progress.ProgressStatusText.DONE,
        ok: false,
      })
    );

    let enrichedErrMessage = `Failed to fetch the model file: ${url}. Reason: ${caughtError.message} ${caughtError.stack}`;

    if (DOMException.isInstance(caughtError)) {
      // DOMException's constructor takes no options dictionary, so the cause
      // cannot be carried over here.
      throw new DOMException(enrichedErrMessage, caughtError.name);
    }

    throw new Error(enrichedErrMessage, { cause: caughtError });
  }

  /**
   * Lists all models stored in the hub.
   *
   * @returns {Promise<Array<{name: string, revision: string}>>}
   */
  async listModels() {
    const cache = await this.#initCache();
    return cache.listModels();
  }

  /**
   * Lists all files for a given model and revision stored in the cache.
   *
   * @param {object} config
   * @param {?string} [config.model] - The model name (hostname/organization/name).
   * @param {?string} [config.revision] - The model version.
   * @param {?string} [config.taskName] - name of the inference task.
   * @returns {ReturnType<IndexedDBCache["listFiles"]>} The file records and their aggregated metadata.
   */
  async listFiles({ taskName, model, revision }) {
    const cache = await this.#initCache();
    return cache.listFiles({ taskName, model, revision });
  }

  /**
   * Deletes all data related to the specifed models.
   *
   * @param {object} config
   *
   * @param {?string} [config.model] - The model name (hostname/organization/name) to delete.
   * @param {?string} [config.revision] - The model version to delete.
   *  If both model and revision are null, delete models of any name and version.
   *
   * @param {?string} [config.taskName] - name of the inference task to delete.
   *                                    If null, delete specified models for all tasks.
   *
   * @param {?function(TaskRecord):boolean} [config.filterFn] - A function to execute for each model file candidate for deletion.
   * @param {?string} [config.deletedBy] - The feature who deleted the model
   * It should return a truthy value to delete the model file, and a falsy value otherwise.
   *
   * @throws {Error} If a revision is defined, the model must also be defined.
   *                 If the model is not defined, the revision should also not be defined.
   *                 Otherwise, an error will be thrown.

   * @returns {Promise<void>}
   */
  async deleteModels({
    taskName,
    model,
    revision,
    filterFn,
    deletedBy = "other",
  }) {
    const cache = await this.#initCache();
    return cache.deleteModels({
      taskName,
      model,
      revision,
      filterFn,
      deletedBy,
    });
  }

  /**
   * Deletes files associated with a specific engine ID in the cache.
   *
   * @param {object} config
   *
   * @param {string} config.engineId - The ID of the engine whose files are to be deleted.
   * @param {string} [config.deletedBy] - The feature who deleted the model
   *
   * @returns {Promise<void>} A promise that resolves once the deletion process is complete.
   */
  async deleteFilesByEngine({ engineId, deletedBy = "other" }) {
    const cache = await this.#initCache();
    return cache.deleteFilesByEngine({ engineId, deletedBy });
  }

  /**
   * Returns the owner icon from a model
   *
   * @param {string} model -- Fully qualified model name
   * @returns {Promise<string|null>}
   */
  async getOwnerIcon(model) {
    await this.#initCache();
    const owner = ModelOwner.fromModel(model);
    return owner.getIcon();
  }

  /**
   * Check if a model is available by attempting to verify a config file exists.
   * This does a lightweight HEAD request to check model availability without downloading.
   *
   * @param {string} model - The model name (organization/name)
   * @param {string} revision - The model revision
   * @param {object} options - Additional options
   * @param {string} [options.file="config.json"] - The file to check for availability
   * @param {string} [options.modelHubRootUrl] - Root URL of the model hub
   * @param {string} [options.modelHubUrlTemplate] - URL template of the model hub
   * @returns {Promise<boolean>} True if the model appears to be available
   */
  async isModelAvailable(
    model,
    revision,
    { file = "config.json", modelHubRootUrl, modelHubUrlTemplate } = {}
  ) {
    const url = this.#validatedFileUrl(model, revision, {
      file,
      modelHubRootUrl,
      modelHubUrlTemplate,
    });
    if (!url) {
      return false;
    }

    const cached = await this.#isCachedAndAllowed(model, revision, file, url);
    if (cached !== null) {
      return cached;
    }

    const response = await this.#fetch(url, { method: "HEAD" });
    return response.ok;
  }

  /**
   * Check if a model is already downloaded to the local cache. Unlike
   * isModelAvailable, this only looks at the local cache: it does no network
   * request at all, so it only reports true for a model that needs no
   * download.
   *
   * @param {string} model - The model name (organization/name)
   * @param {string} revision - The model revision
   * @param {object} options - Additional options
   * @param {string} [options.file="config.json"] - The file to check
   * @param {string} [options.modelHubRootUrl] - Root URL of the model hub
   * @param {string} [options.modelHubUrlTemplate] - URL template of the model hub
   * @returns {Promise<boolean>} True if the model is already installed
   */
  async isModelInstalled(
    model,
    revision,
    { file = "config.json", modelHubRootUrl, modelHubUrlTemplate } = {}
  ) {
    const url = this.#validatedFileUrl(model, revision, {
      file,
      modelHubRootUrl,
      modelHubUrlTemplate,
    });
    if (!url) {
      return false;
    }

    return (
      (await this.#isCachedAndAllowed(model, revision, file, url)) === true
    );
  }

  /**
   * Shared by isModelAvailable/isModelInstalled: validates the inputs and
   * builds the hub URL of a model file, returning null if the inputs are
   * invalid.
   *
   * @param {string} model
   * @param {string} revision
   * @param {object} options
   * @param {string} options.file
   * @param {string} [options.modelHubRootUrl]
   * @param {string} [options.modelHubUrlTemplate]
   * @returns {?string}
   */
  #validatedFileUrl(
    model,
    revision,
    { file, modelHubRootUrl, modelHubUrlTemplate }
  ) {
    const checkError = ModelHub.checkInput(model, revision, file);
    if (checkError) {
      lazy.console.error(
        `ModelHub: Invalid input for ${model}@${revision}: ${checkError.message}`
      );
      return null;
    }

    return this.#fileUrl({
      model,
      revision,
      file,
      modelHubRootUrl: modelHubRootUrl || this.rootUrl,
      modelHubUrlTemplate: modelHubUrlTemplate || this.urlTemplate,
    });
  }

  /**
   * Shared by isModelAvailable/isModelInstalled. Returns null if the file
   * isn't cached at all (caller decides what to do next, e.g. a network
   * check). Returns true/false if it is cached: true if its hub is still
   * allowed, false (after purging the cached model) if it has since been
   * denylisted.
   *
   * @param {string} model
   * @param {string} revision
   * @param {string} file
   * @param {string} url
   * @returns {Promise<?boolean>}
   */
  async #isCachedAndAllowed(model, revision, file, url) {
    const cache = await this.#initCache();
    const hostname = new URL(url).hostname;
    const modelWithHostname = `${hostname}/${model}`;
    const isCached = await cache.fileExists({
      model: modelWithHostname,
      revision,
      file,
    });
    if (!isCached) {
      return null;
    }
    const fileAllowed = this.allowedURL(url);
    if (!fileAllowed.allowed) {
      await cache.deleteModels({
        model: modelWithHostname,
        revision,
        deletedBy: "denylist",
      });
      return false;
    }
    return true;
  }
}
