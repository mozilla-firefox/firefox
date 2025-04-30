/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  Sqlite: "resource://gre/modules/Sqlite.sys.mjs",
});

export class PlacesSemanticHistoryDatabase {
  #conn;
  dbDirPath;
  #placesDbPath;
  #semanticDbPath;
  #embeddingSize;

  constructor(embeddingSize, semanticDbFullPath) {
    this.#embeddingSize = embeddingSize;
    this.#semanticDbPath = semanticDbFullPath;
    this.dbDirPath = PathUtils.parent(semanticDbFullPath);
  }

  /**
   * Creates the necessary virtual tables in the semantic.sqlite database.
   */
  async #createVirtualVecTables(conn, embeddingSize) {
    await conn.executeTransaction(async () => {
      await conn.execute(`
          CREATE VIRTUAL TABLE vec_history USING vec0(
          embedding FLOAT[${embeddingSize}],
          embedding_coarse bit[${embeddingSize}]
          );`);
      await conn.execute(`
          CREATE TABLE vec_history_mapping (
          rowid INTEGER PRIMARY KEY,
          url_hash INTEGER NOT NULL UNIQUE
          );`);
    });
  }

  /**
   * Connects to the semantic.sqlite database and attaches the Places DB.
   *
   * @returns {Promise<object>}
   *   A promise resolving to the database connection.
   */
  async getConnection() {
    // Connect to the database
    this.#conn = await lazy.Sqlite.openConnection({
      path: this.#semanticDbPath,
      extensions: ["vec"],
    });

    this.#placesDbPath = PathUtils.join(this.dbDirPath, "places.sqlite");
    await this.attachPlacesDb();

    // Add shutdown blocker to close connection gracefully
    lazy.Sqlite.shutdown.addBlocker(
      "PlacesSemanticHistoryDatabase: Shutdown",
      () => this.#conn.close()
    );
    return this.#conn;
  }

  /**
   * Attaches the Places database to the semantic.sqlite connection.
   */
  async attachPlacesDb() {
    await this.#conn.execute(
      `ATTACH DATABASE '${this.#placesDbPath}' AS places`
    );
  }

  /**
   * Initializes the semantic database, creating virtual tables if needed.
   */
  async initVectorDatabase() {
    // Connect to the database
    let version = await this.#conn.getSchemaVersion();
    if (version == 0) {
      await this.#createVirtualVecTables(this.#conn, this.#embeddingSize);
      version = 1;
      await this.#conn.setSchemaVersion(version);
    }
  }

  // util method to get the path to semanticDB
  getDatabasePath() {
    return this.#semanticDbPath;
  }

  /**
   * Drops the history vector tables and resets schema version
   */
  async dropSchema() {
    await this.#conn.executeTransaction(async () => {
      await this.#conn.execute(`DROP TABLE IF EXISTS vec_history;`);
      await this.#conn.execute(`DROP TABLE IF EXISTS vec_history_mapping;`);
    });
    await this.#conn.setSchemaVersion(0);
  }
}
