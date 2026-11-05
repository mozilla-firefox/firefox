/*
 This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  Sqlite: "resource://gre/modules/Sqlite.sys.mjs",
});

const DEFAULT_MAX_DB_SIZE_BYTES = 75 * 1024 * 1024;

/**
 * Base class for SQLite-backed store modules. Handles connection lifecycle,
 * schema initialization, migrations, corruption recovery, and database pruning.
 *
 * Subclasses must override the configuration getters to provide store-specific
 * values (database filename, schema version, SQL entity statements, etc.).
 *
 * @example
 * class MyStore extends SQLiteStoreBase {
 *   get logPrefix() { return "MyStore"; }
 *   get logLevelPref() { return "browser.mystore.loglevel"; }
 *   get shutdownBlockerName() { return "MyStore: Shutdown"; }
 *   get CURRENT_SCHEMA_VERSION() { return 1; }
 *   get databaseFileName() { return "my-store.sqlite"; }
 *   get prefBranch() { return "browser.mystore"; }
 *   get createEntityStatements() { return [TABLE_SQL, INDEX_SQL]; }
 *   get migrations() { return []; }
 * }
 */
export class SQLiteStoreBase {
  #asyncShutdownBlocker;
  #conn;
  #promiseConn;
  #log;

  constructor() {
    this.#asyncShutdownBlocker = async () => {
      await this.#closeConnection();
    };
  }

  // -- Configuration getters (subclass MUST override) --

  get logPrefix() {
    throw new Error("Subclass must implement logPrefix");
  }

  get logLevelPref() {
    throw new Error("Subclass must implement logLevelPref");
  }

  get shutdownBlockerName() {
    throw new Error("Subclass must implement shutdownBlockerName");
  }

  get CURRENT_SCHEMA_VERSION() {
    throw new Error("Subclass must implement CURRENT_SCHEMA_VERSION");
  }

  get databaseFileName() {
    throw new Error("Subclass must implement databaseFileName");
  }

  get prefBranch() {
    throw new Error("Subclass must implement prefBranch");
  }

  // eslint-disable-next-line jsdoc/require-returns-check
  /**
   * @returns {Array<string>} SQL statements to execute when creating a fresh database.
   */
  get createEntityStatements() {
    throw new Error("Subclass must implement createEntityStatements");
  }

  // eslint-disable-next-line jsdoc/require-returns-check
  /**
   * @returns {Array<Function>} Migration functions, each accepting (conn, version).
   */
  get migrations() {
    throw new Error("Subclass must implement migrations");
  }

  // -- Pruning hooks (subclass implements to enable pruneDatabase) --

  /**
   * Returns the oldest records eligible for pruning.
   *
   * @param {number} _batchSize - How many records to retrieve
   * @returns {Array<{id: string}>} Records with at least an `id` property
   */
  async findOldestPrunableRecords(_batchSize) {
    throw new Error("Subclass must implement findOldestPrunableRecords");
  }

  /**
   * Deletes a single record by its id during pruning.
   *
   * @param {string} _id - The record id to delete
   */
  async deletePrunableRecord(_id) {
    throw new Error("Subclass must implement deletePrunableRecord");
  }

  // -- Public getters --

  /**
   * Gets prefixed console log instance
   *
   * @returns {ConsoleInstance}
   */
  get log() {
    if (!this.#log) {
      this.#log = console.createInstance({
        logModule: this.logPrefix,
        maxLogLevelPref: this.logLevelPref,
      });
    }
    return this.#log;
  }

  /**
   * Returns the SQLite connection object
   *
   * @returns {OpenedConnection}
   */
  get connection() {
    return this.#conn;
  }

  /**
   * File path to the database file for the store
   *
   * @returns {string}
   */
  get databaseFilePath() {
    return PathUtils.join(PathUtils.profileDir, this.databaseFileName);
  }

  /**
   * The max database size in bytes
   *
   * @returns {number}
   */
  get maxDatabaseSizeBytes() {
    return DEFAULT_MAX_DB_SIZE_BYTES;
  }

  /**
   * Profiler marker category used for connection markers. Subclasses may
   * override to group markers differently in the profiler.
   *
   * @returns {string}
   */
  get profilerMarkerCategory() {
    return "SmartWindow";
  }

  // -- Public methods --

  /**
   * Makes sure the database is ready to interact with.
   *
   * If a connection is already pending the connection promise is returned.
   *
   * If the #removeDatabaseOnStartup flag has been set the database will be
   * removed first before re-creating the schemas and returning the connection
   * promise.
   *
   * @returns {Promise<OpenedConnection>}
   */
  async ensureDatabase() {
    if (this.#promiseConn) {
      return this.#promiseConn;
    }

    let deferred = Promise.withResolvers();
    this.#promiseConn = deferred.promise;
    if (this.#removeDatabaseOnStartup) {
      this.log.debug("Removing database on startup");
      try {
        await this.#removeDatabaseFiles();
      } catch (e) {
        deferred.reject(new Error("Could not remove the database files"));
        return deferred.promise;
      }
    }

    try {
      await this.#openConnection();
    } catch (e) {
      if (
        e.result == Cr.NS_ERROR_FILE_CORRUPTED ||
        e.errors?.some(error => error.result == Ci.mozIStorageError.NOTADB)
      ) {
        this.log.warn("Invalid database detected, removing it.", e);
        await this.#removeDatabaseFiles();
      }
    }

    if (!this.#conn) {
      try {
        await this.#openConnection();
      } catch (e) {
        this.log.error(
          "Could not open the database connection.",
          e.message,
          e.stack
        );
        deferred.reject(new Error("Could not open the database connection"));
        return deferred.promise;
      }
    }

    try {
      await this.#initializeSchema();
    } catch (e) {
      this.log.warn(
        "Failed to initialize the database schema, recreating the database.",
        e
      );
      // If the schema cannot be initialized try to create a new database file.
      await this.#removeDatabaseFiles();
    }

    deferred.resolve(this.#conn);
    return this.#promiseConn;
  }

  /**
   * The current version of the db schema
   *
   * @returns {number}
   */
  async getDatabaseSchemaVersion() {
    if (!this.#conn) {
      await this.ensureDatabase();
    }

    return this.#conn.getSchemaVersion();
  }

  /**
   * Update the current version of the db schema
   *
   * @param {number} version
   */
  async setSchemaVersion(version) {
    await this.#conn.setSchemaVersion(version);
  }

  /**
   * Retrieve the current size of the database in bytes
   *
   * @returns {number}
   */
  async getDatabaseSize() {
    await this.ensureDatabase().catch(e => {
      this.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    const stats = await IOUtils.stat(this.databaseFilePath);
    return stats.size;
  }

  /**
   * Deletes all the database files
   */
  async destroyDatabase() {
    await this.#removeDatabaseFiles();
    this.#promiseConn = null;
  }

  /**
   * Applies any migration functions needed for the specified schema version
   *
   * @param {number} version
   */
  async applyMigrations(version) {
    for (const migration of this.migrations) {
      if (typeof migration !== "function") {
        continue;
      }

      await migration(this.#conn, version);
    }
  }

  /**
   * Reads the actual bytes in use by the database, as opposed to the physical
   * size on disk of the SQLite file.
   *
   * @returns {number} Current bytes in use by the db
   */
  async getDbBytesInUse() {
    const rows = await this.#conn.execute(
      "SELECT sum(pgsize) AS size FROM dbstat WHERE aggregate = TRUE"
    );

    return rows[0].getResultByName("size") ?? 0;
  }

  /**
   * Prunes the database by deleting oldest records until the size in use drops
   * below the target threshold.
   *
   * Subclasses must implement `findOldestPrunableRecords` and
   * `deletePrunableRecord` for this to work.
   *
   * @param {number} [reduceByPercentage=0.05] - Percentage to reduce db size by
   * @param {number} [maxDbSizeBytes=this.maxDatabaseSizeBytes] - Db max size
   */
  async pruneDatabase(
    reduceByPercentage = 0.05,
    maxDbSizeBytes = this.maxDatabaseSizeBytes
  ) {
    await this.ensureDatabase().catch(e => {
      this.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    const dbExists = await IOUtils.exists(this.databaseFilePath);
    if (!dbExists) {
      return;
    }

    const DELETE_BATCH_SIZE = 50;

    let dbSize = await this.getDbBytesInUse();
    if (dbSize < maxDbSizeBytes) {
      return;
    }

    const targetDbSize = Math.max(0, dbSize * (1 - reduceByPercentage));

    const MAX_ITERATIONS = 100;
    // how many "no size change" batches we tolerate
    const MAX_STAGNANT = 5;
    let iterations = 0;
    let stagnantIterations = 0;

    while (
      dbSize > targetDbSize &&
      iterations < MAX_ITERATIONS &&
      stagnantIterations < MAX_STAGNANT
    ) {
      iterations++;

      const oldestRecords =
        await this.findOldestPrunableRecords(DELETE_BATCH_SIZE);

      if (!oldestRecords.length) {
        break;
      }

      for (const record of oldestRecords) {
        await this.deletePrunableRecord(record.id);
      }

      const newDbSize = await this.getDbBytesInUse();
      if (newDbSize >= dbSize) {
        stagnantIterations++;
      } else {
        stagnantIterations = 0;
      }

      dbSize = newDbSize;
    }

    // Actually reclaim disk space.
    await this.#conn.execute("PRAGMA incremental_vacuum;");
  }

  // -- Private methods --

  /**
   * Opens connection to SQLite file
   *
   * @private
   */
  async #openConnection() {
    this.log.debug("Opening new connection");

    let markerData = null;
    if (Services.profiler.IsActive()) {
      let sizeLabel = "new";
      try {
        const stat = await IOUtils.stat(this.databaseFilePath);
        sizeLabel = `${(stat.size / 1048576).toFixed(1)} MiB`;
      } catch (_e) {}
      markerData = { startTime: ChromeUtils.now(), sizeLabel };
    }

    try {
      const confConfig = { path: this.databaseFilePath };
      this.#conn = await lazy.Sqlite.openConnection(confConfig);
    } catch (e) {
      this.log.error("openConnection() could not open db:", e.message, e.stack);
      throw e;
    }

    lazy.Sqlite.shutdown.addBlocker(
      this.shutdownBlockerName,
      this.#asyncShutdownBlocker
    );

    try {
      // TODO - Bug 2048333 Migrate this to default value 32k
      await this.#conn.execute("PRAGMA page_size = 4096;");
      // Setup WAL journaling, as it is generally faster.
      await this.#conn.execute("PRAGMA journal_mode = WAL;");
      await this.#conn.execute("PRAGMA wal_autocheckpoint = 16;");

      // Store VACUUM information to be used by the VacuumManager.
      await this.#conn.execute("PRAGMA auto_vacuum = INCREMENTAL;");
      await this.#conn.execute("PRAGMA foreign_keys = ON;");
    } catch (e) {
      this.log.warn("Configuring SQLite PRAGMA settings: ", e.message);
    }

    if (markerData) {
      ChromeUtils.addProfilerMarker(
        this.profilerMarkerCategory,
        { startTime: markerData.startTime },
        `${this.logPrefix}:open_db(${markerData.sizeLabel})`
      );
    }
  }

  /**
   * Closes connection to SQLite file
   *
   * @private
   */
  async #closeConnection() {
    if (!this.#conn) {
      return;
    }

    this.log.debug("Closing connection");
    lazy.Sqlite.shutdown.removeBlocker(this.#asyncShutdownBlocker);
    try {
      await this.#conn.close();
    } catch (e) {
      this.log.warn(`Error closing connection: ${e.message}`);
    }
    this.#conn = null;
  }

  /**
   * Initializes the database schema with all the necessary database
   * entities, tables, indexes, etc. Sets the schema version, and
   * applies the migrations that are necessary.
   *
   * @private
   */
  async #initializeSchema() {
    const version = await this.getDatabaseSchemaVersion();

    if (version == this.CURRENT_SCHEMA_VERSION) {
      return;
    }

    if (version > this.CURRENT_SCHEMA_VERSION) {
      await this.setSchemaVersion(this.CURRENT_SCHEMA_VERSION);
      return;
    }

    // Must migrate the schema.
    await this.#conn.executeTransaction(async () => {
      if (version == 0) {
        // This is a newly created database, just create the entities.
        await this.#createDatabaseEntities();
        await this.#conn.setSchemaVersion(this.CURRENT_SCHEMA_VERSION);

        // eslint-disable-next-line no-useless-return
        return;
      }

      await this.applyMigrations(version);
      await this.setSchemaVersion(this.CURRENT_SCHEMA_VERSION);
    });
  }

  /**
   * Executes all the SQL statements to add tables/indexes/etc to the db.
   *
   * @private
   */
  async #createDatabaseEntities() {
    for (const sql of this.createEntityStatements) {
      await this.#conn.execute(sql);
    }
  }

  /**
   * Closes connection and removes all database files
   *
   * @private
   */
  async #removeDatabaseFiles() {
    this.log.debug("Removing database files");
    await this.#closeConnection();
    try {
      for (let file of [
        this.databaseFilePath,
        PathUtils.join(PathUtils.profileDir, this.databaseFileName + "-wal"),
        PathUtils.join(PathUtils.profileDir, this.databaseFileName + "-shm"),
      ]) {
        this.log.debug(`Removing ${file}`);
        await IOUtils.remove(file, {
          retryReadonly: true,
          recursive: true,
          ignoreAbsent: true,
        });
      }
      this.#removeDatabaseOnStartup = false;
    } catch (e) {
      this.log.warn("Failed to remove database files", e);
      // Try to clear on next startup.
      this.#removeDatabaseOnStartup = true;
      // Re-throw the exception for the caller.
      throw e;
    }
  }

  /**
   * Gets current removeDatabaseOnStartup preference value
   *
   * @private
   */
  get #removeDatabaseOnStartup() {
    return Services.prefs.getBoolPref(
      `${this.prefBranch}.removeDatabaseOnStartup`,
      false
    );
  }

  /**
   * Sets current removeDatabaseOnStartup preference value
   *
   * @param {boolean} value
   *
   * @private
   */
  set #removeDatabaseOnStartup(value) {
    this.log.debug(`Setting removeDatabaseOnStartup to ${value}`);
    Services.prefs.setBoolPref(
      `${this.prefBranch}.removeDatabaseOnStartup`,
      value
    );
  }
}
