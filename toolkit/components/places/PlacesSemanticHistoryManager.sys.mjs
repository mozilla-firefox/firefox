/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * PlacesSemanticHistoryManager manages the semantic.sqlite database and provides helper
 * methods for initializing, querying, and updating semantic data.
 *
 * This module handles embeddings-based semantic search capabilities using the
 * Places database and an ML engine for vector operations.
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  PlacesSemanticHistoryDatabase:
    "resource://gre/modules/PlacesSemanticHistoryDatabase.sys.mjs",
  EmbeddingsGenerator: "chrome://global/content/ml/EmbeddingsGenerator.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", function () {
  return lazy.PlacesUtils.getLogger({ prefix: "PlacesSemanticHistoryManager" });
});

// Time between deferred task executions.
const DEFERRED_TASK_INTERVAL_MS = 1000;
// Maximum time to wait for an idle before the task is executed anyway.
const DEFERRED_TASK_MAX_IDLE_WAIT_MS = 2 * 60000;
// Number of entries to update at once.
const DEFAULT_CHUNK_SIZE = 50;
const ONE_MiB = 1024 * 1024;

export class PlacesSemanticHistoryManager {
  #conn;
  #engine = undefined;
  #embeddingSize;
  #rowLimit;
  #samplingAttrib;
  #changeThresholdCount;
  #distanceThreshold;
  #finalized = false;
  #updateTask = null;
  #prevPagesRankChangedCount = 0;
  #pageRankCountThreshold = 2;
  #pendingUpdates = true;
  testFlag = false;
  #updateTaskLatency = [];
  embedder;
  qualifiedForSemanticSearch = false;

  /**
   * Constructor for PlacesSemanticHistoryManager.
   *
   * @param {Object} options - Configuration options.
   * @param {number} [options.embeddingSize=384] - Size of embeddings used for vector operations.
   * @param {number} [options.rowLimit=600] - Maximum number of rows to process from the database.
   * @param {string} [options.samplingAttrib="frecency"] - Attribute used for sampling rows.
   * @param {number} [options.changeThresholdCount=3] - Threshold of changed rows to trigger updates.
   * @param {number} [options.distanceThreshold=0.75] - Cosine distance threshold to determine similarity.
   * @param {boolean} [options.testFlag=false] - Flag for test behavior.
   */
  constructor({
    embeddingSize = 384,
    rowLimit = 600,
    samplingAttrib = "frecency",
    changeThresholdCount = 3,
    distanceThreshold = 0.75,
    testFlag = false,
  } = {}) {
    this.QueryInterface = ChromeUtils.generateQI([
      "nsIObserver",
      "nsISupportsWeakReference",
    ]);

    // Do not initialize during shutdown.
    if (
      Services.startup.isInOrBeyondShutdownPhase(
        Ci.nsIAppStartup.SHUTDOWN_PHASE_APPSHUTDOWNCONFIRMED
      )
    ) {
      this.#finalized = true;
      return;
    }
    this.embedder = new lazy.EmbeddingsGenerator(embeddingSize);
    this.semanticDB = new lazy.PlacesSemanticHistoryDatabase(
      embeddingSize,
      PathUtils.join(PathUtils.profileDir, "places_semantic.sqlite")
    );
    this.qualifiedForSemanticSearch =
      this.embedder.isEnoughPhysicalMemoryAvailable() &&
      this.embedder.isEnoughCpuCoresAvailable();

    if (this.qualifiedForSemanticSearch) {
      this.#createOrUpdateTask();
    }

    lazy.AsyncShutdown.appShutdownConfirmed.addBlocker(
      "SemanticManager: shutdown",
      () => this.#finalize()
    );

    // Add the observer for pages-rank-changed and history-cleared topics
    this.handlePlacesEvents = this.handlePlacesEvents.bind(this);
    lazy.PlacesUtils.observers.addListener(
      ["pages-rank-changed", "history-cleared"],
      this.handlePlacesEvents
    );

    this.#rowLimit = rowLimit;
    this.#embeddingSize = embeddingSize;
    this.#samplingAttrib = samplingAttrib;
    this.#changeThresholdCount = changeThresholdCount;
    this.#distanceThreshold = distanceThreshold;
    this.testFlag = testFlag;
    this.#updateTaskLatency = [];
    lazy.logger.info("Inside PlaceSemanticManager constructor");
    if (this.qualifiedForSemanticSearch) {
      this.onPagesRankChanged();
    }
  }

  /**
   * Connects to the semantic.sqlite database and attaches the Places DB.
   *
   * @returns {Promise<object>}
   *   A promise resolving to the database connection.
   */
  async getConnection() {
    if (!this.#conn) {
      this.#conn = await this.semanticDB.getConnection();
      await this.semanticDB.initVectorDatabase();
    }
    return this.#conn;
  }

  /**
   * Observes changes to the "pages-rank-changed" and
   * "history-cleared" topics.
   *
   * @param {object} subject - The subject of the observation.
   * @param {string} topic - The observed topic.
   */
  observe(subject, topic) {
    if (topic === "pages-rank-changed") {
      lazy.logger.info("Observed pages-rank-changed topic.");
      this.onPagesRankChanged();
    }
    if (topic === "history-cleared") {
      lazy.logger.info("Observed history-cleared topic.");
      this.onPagesRankChanged();
    }
  }

  handlePlacesEvents(events) {
    for (const { type } of events) {
      switch (type) {
        case "pages-rank-changed":
          this.onPagesRankChanged();
          break;
        case "history-cleared":
          this.onPagesRankChanged();
          break;
      }
    }
  }

  /**
   * Handles updates triggered by database changes or rank changes.
   *
   * This is invoked whenever the `"pages-rank-changed"` or
   * `"history-cleared"` event is observed.
   * It re-arms the DeferredTask for updates if not finalized.
   *
   * @private
   * @returns Promise<void>
   */
  async onPagesRankChanged() {
    if (!this.#updateTask.isFinalized) {
      lazy.logger.trace("Arm update task");
      this.#updateTask.arm();
    }
  }

  // getter for testing purposes
  getUpdateTaskLatency() {
    return this.#updateTaskLatency;
  }

  /**
   * Creates or updates the DeferredTask for managing updates to the semantic DB.
   */
  #createOrUpdateTask() {
    if (this.#finalized) {
      lazy.logger.trace(`Not resurrecting #updateTask because finalized`);
      return;
    }
    if (this.#updateTask) {
      this.#updateTask.disarm();
      this.#updateTask.finalize().catch(console.error);
    }

    this.#updateTask = new lazy.DeferredTask(
      async () => {
        //capture updateTask startTime
        const updateStartTime = Cu.now();

        try {
          lazy.logger.info("Running vector DB update task...");
          if (!this.#conn) {
            try {
              this.#conn = await this.getConnection();
            } catch (ex) {
              lazy.logger.error("Unable to initialize database");
              console.error(ex.message);
              throw new Error("Unable to initialize database", { cause: ex });
            }
          }
          let pagesRankChangedCount =
            PlacesObservers.counts.get("pages-rank-changed") +
            PlacesObservers.counts.get("history-cleared");
          if (
            pagesRankChangedCount - this.#prevPagesRankChangedCount >=
              this.#pageRankCountThreshold ||
            this.#pendingUpdates ||
            this.testFlag
          ) {
            this.#prevPagesRankChangedCount = pagesRankChangedCount;
            const startTime = Cu.now();
            lazy.logger.info(
              `Changes exceed threshold (${this.#changeThresholdCount}). Scheduling update task.`
            );
            let addedRows = await this.findAdds();
            let deletedRows = await this.findDeletes();

            let totalAdds = addedRows.length;
            let totalDeletes = deletedRows.length;

            lazy.logger.info(
              `Total rows to add: ${totalAdds}, delete: ${totalDeletes}`
            );

            if (totalAdds > 0) {
              this.#pendingUpdates = true;
              const chunk = addedRows.slice(0, DEFAULT_CHUNK_SIZE);
              await this.updateVectorDB(chunk, []);
              ChromeUtils.addProfilerMarker(
                "updateVectorDB",
                startTime,
                "Details about updateVectorDB event"
              );
              this.#updateTask.arm();
            }
            if (totalDeletes > 0) {
              this.#pendingUpdates = true;
              const chunk = deletedRows.slice(0, DEFAULT_CHUNK_SIZE);
              await this.updateVectorDB([], chunk);
              ChromeUtils.addProfilerMarker(
                "updateVectorDB",
                startTime,
                "Details about updateVectorDB event"
              );
              this.#updateTask.arm();
            }
            if (totalAdds + totalDeletes == 0) {
              this.#pendingUpdates = false;
              Services.obs.notifyObservers(
                null,
                "places-semantichistorymanager-update-complete"
              );
            }
            if (this.testFlag) {
              this.#updateTask.arm();
            }
            lazy.logger.info("Vector DB update task completed.");
          } else {
            lazy.logger.info("No significant changes detected.");
          }
        } catch (error) {
          lazy.logger.error("Error executing vector DB update task:", error);
        } finally {
          const updateEndTime = Cu.now();
          const updateTaskTime = updateEndTime - updateStartTime;
          this.#updateTaskLatency.push(updateTaskTime);

          lazy.logger.info(
            `DeferredTask update completed in ${updateTaskTime} ms.`
          );
        }
      },
      DEFERRED_TASK_INTERVAL_MS,
      DEFERRED_TASK_MAX_IDLE_WAIT_MS
    );
    this.#updateTask.arm();
    lazy.logger.info("Update task armed.");
  }

  /**
   * Finalizes the PlacesSemanticHistoryManager by cleaning up resources.
   *
   * This ensures any tasks are finalized and the manager is properly
   * cleaned up during shutdown.
   *
   * @private
   * @returns {void}
   */
  #finalize() {
    lazy.logger.trace("Finalizing SemanticManager");
    // We don't mind about tasks completiion, since we can execute them in the
    // next session.
    this.#updateTask.disarm();
    this.#updateTask.finalize().catch(console.error);
    this.#finalized = true;
  }

  async findAdds() {
    // find any adds after successful checkForChanges
    const addedRows = await this.#conn.execute(
      `
      SELECT top_places.url_hash, title, COALESCE(description, '') AS description
      FROM (
        SELECT url_hash, title, description
        FROM places.moz_places
        WHERE title NOTNULL
          AND length(title) > 2
          AND last_visit_date NOTNULL
        ORDER BY :samplingAttrib DESC
        LIMIT :rowLimit
      ) AS top_places
      LEFT JOIN vec_history_mapping AS vec_map
      ON top_places.url_hash = vec_map.url_hash
      WHERE vec_map.url_hash IS NULL
    `,
      { samplingAttrib: this.#samplingAttrib, rowLimit: this.#rowLimit }
    );
    lazy.logger.info(`findAdds: Found ${addedRows.length} rows to add.`);
    return addedRows;
  }

  async findDeletes() {
    // find any deletes after successful checkForChanges
    const deletedRows = await this.#conn.execute(
      `
      SELECT url_hash
      FROM vec_history_mapping
      WHERE url_hash NOT IN  (
          SELECT url_hash
          FROM places.moz_places
          WHERE title NOTNULL
            AND length(title) > 2
            AND last_visit_date NOTNULL
          ORDER BY :samplingAttrib DESC
          LIMIT :rowLimit
        )
    `,
      { samplingAttrib: this.#samplingAttrib, rowLimit: this.#rowLimit }
    );
    lazy.logger.info(
      `findDeletes: Found ${deletedRows.length} rows to delete.`
    );
    return deletedRows;
  }

  async updateVectorDB(addedRows, deletedRows) {
    await this.embedder.createEngineIfNotPresent();
    // Instead of calling engineRun in a loop for each row,
    // you prepare an array of requests.
    if (addedRows.length) {
      const texts = addedRows.map(row => {
        const title = row.getResultByName("title") ?? "";
        const description = row.getResultByName("description") ?? "";
        return title + " " + description;
      });

      let batchTensors = await this.embedder.embedMany(texts);

      await this.#conn.executeTransaction(async () => {
        // Process each row and corresponding tensor.
        for (let i = 0; i < addedRows.length; i++) {
          const row = addedRows[i];
          const tensor = batchTensors[i];
          if (!Array.isArray(tensor) || tensor.length !== this.#embeddingSize) {
            lazy.logger.error(
              `Got tensor with invalid length: ${Array.isArray(tensor) ? tensor.length : "non-array value"}`
            );
            throw new Error("invalid tensor result");
          }
          const url_hash = row.getResultByName("url_hash");
          const vectorBindable = this.tensorToBindable(tensor);
          const result = await this.#conn.execute(
            `INSERT INTO vec_history(embedding, embedding_coarse)
             VALUES (:vector, vec_quantize_binary(:vector))
             RETURNING rowid`,
            { vector: vectorBindable }
          );
          const rowid = result[0].getResultByName("rowid");
          await this.#conn.execute(
            `INSERT INTO vec_history_mapping (rowid, url_hash)
             VALUES (:rowid, :url_hash)`,
            { rowid, url_hash }
          );
        }
      });
    }

    // apply deletes from Vector DB
    for (let drow of deletedRows) {
      const url_hash = drow.getResultByName("url_hash");
      if (!url_hash) {
        lazy.logger.warn(`No url_hash found for a deleted row, skipping.`);
        continue;
      }

      try {
        // Delete the mapping from vec_history_mapping table
        const mappingResult = await this.#conn.execute(
          `DELETE FROM vec_history_mapping 
           WHERE url_hash = :url_hash 
           RETURNING rowid`,
          { url_hash }
        );

        if (mappingResult.length === 0) {
          lazy.logger.warn(`No mapping found for url_hash: ${url_hash}`);
          continue;
        }

        const rowid = mappingResult[0].getResultByName("rowid");

        // Delete the embedding from vec_history table
        await this.#conn.execute(
          `DELETE FROM vec_history 
           WHERE rowid = :rowid`,
          { rowid }
        );

        lazy.logger.info(
          `Deleted embedding and mapping for url_hash: ${url_hash}`
        );
      } catch (error) {
        lazy.logger.error(
          `Failed to delete for url_hash: ${url_hash}. Error: ${error.message}`
        );
      }
    }
  }

  /**
   * Shuts down the manager, ensuring cleanup of tasks and connections.
   */
  async shutdown() {
    if (this.#updateTask) {
      await this.#updateTask.finalize();
    }

    if (this.#conn) {
      await this.#conn.close();
    }

    lazy.PlacesUtils.observers.removeListener(
      ["pages-rank-changed", "history-cleared"],
      this.handlePlacesEvents
    );

    lazy.logger.info("PlacesSemanticHistoryManager shut down.");
  }

  tensorToBindable(tensor) {
    if (!tensor) {
      throw new Error("tensorToBindable received an undefined tensor");
    }
    return new Uint8ClampedArray(new Float32Array(tensor).buffer);
  }

  /**
   * Executes an inference operation using the ML engine.
   *
   * This runs the engine's inference pipeline on the provided request and
   * checks if changes to the rank warrant triggering an update.
   *
   * @param {object} request - The request to run through the engine.
   * @returns {Promise<object>} - The result of the engine's inference pipeline.
   */
  async infer(queryContext) {
    let results = [];
    let tensor = await this.embedder.embed(queryContext.searchString);

    if (!tensor) {
      return results;
    }

    let metrics = tensor.metrics;

    // If tensor is a nested array with a single element, extract the inner array.
    if (
      Array.isArray(tensor) &&
      tensor.length === 1 &&
      Array.isArray(tensor[0])
    ) {
      tensor = tensor[0];
    }

    if (!Array.isArray(tensor) || tensor.length !== this.#embeddingSize) {
      lazy.logger.info(`Got tensor with length ${tensor.length}`);
      return results;
    }
    if (!this.#conn) {
      await this.getConnection();
    }

    let rows = await this.#conn.execute(
      `
       WITH coarse_matches AS (
        SELECT rowid,
               embedding
          FROM vec_history
          WHERE embedding_coarse match vec_quantize_binary(:vector)
          ORDER BY distance
          LIMIT 100
       )
        SELECT p.id,
               p.title,
               p.url,
               vec_res.cosine_distance as distance
        FROM (
          SELECT url_hash, cosine_distance
          FROM (
            SELECT v_hist.rowid,
                   v_hist_map.url_hash,
                   vec_distance_cosine(embedding, :vector) AS cosine_distance
              FROM coarse_matches v_hist
              JOIN vec_history_mapping v_hist_map
                ON v_hist.rowid = v_hist_map.rowid
             ORDER BY cosine_distance
             LIMIT 2
          ) AS NEIGHBORS
          WHERE cosine_distance <= :distanceThreshold
        ) AS vec_res
        JOIN places.moz_places p
          ON vec_res.url_hash = p.url_hash
      `,
      {
        vector: this.tensorToBindable(tensor),
        distanceThreshold: this.#distanceThreshold,
      }
    );

    for (let row of rows) {
      results.push({
        id: row.getResultByName("id"),
        title: row.getResultByName("title"),
        url: row.getResultByName("url"),
        distance: row.getResultByName("distance"),
      });
    }

    results.sort((a, b) => a.distance - b.distance);
    return { results, metrics };
  }

  // for easier testing purpose
  async engineRun(request) {
    return await this.#engine.run(request);
  }

  /**
   * Performs a WAL checkpoint to flush all pending writes from WAL to the main database file.
   * Then measures the final disk size of semantic.sqlite.
   * **This method is for test purposes only.**
   *
   * @returns {Promise<number>} - The size of `semantic.sqlite` in bytes after checkpointing.
   */
  async checkpointAndMeasureDbSize() {
    if (!this.#conn) {
      await this.getConnection();
    }

    try {
      lazy.logger.info("Starting WAL checkpoint on semantic.sqlite");

      // Perform a full checkpoint to move WAL data into the main database file
      await this.#conn.execute(`PRAGMA wal_checkpoint(FULL);`);
      await this.#conn.execute(`PRAGMA wal_checkpoint(TRUNCATE);`);

      // Ensure database is in WAL mode
      let journalMode = await this.#conn.execute(`PRAGMA journal_mode;`);
      lazy.logger.info(
        `Journal Mode after checkpoint: ${journalMode[0].getResultByName("journal_mode")}`
      );

      // Measure the size of `semantic.sqlite` after checkpoint
      const semanticDbPath = this.semanticDB.getDatabasePath();
      let { size } = await IOUtils.stat(semanticDbPath);
      const sizeInMB = size / ONE_MiB;

      lazy.logger.info(
        `Size of semantic.sqlite after checkpoint: ${sizeInMB} mb`
      );
      return sizeInMB;
    } catch (error) {
      lazy.logger.error(
        "Error during WAL checkpoint and size measurement:",
        error
      );
      return null;
    }
  }

  //getters
  getEmbeddingSize() {
    return this.#embeddingSize;
  }

  getRowLimit() {
    return this.#rowLimit;
  }

  getPrevPagesRankChangeCount() {
    return this.#prevPagesRankChangedCount;
  }

  getPendingUpdatesStatus() {
    return this.#pendingUpdates;
  }

  //setters for mocking from tests
  setConnection(mockConnection) {
    this.#conn = mockConnection;
  }

  //for test purposes
  stopProcess() {
    this.#finalize();
  }
}
