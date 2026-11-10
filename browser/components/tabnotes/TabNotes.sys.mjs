/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import { OpenedConnection } from "resource://gre/modules/Sqlite.sys.mjs" */

import { Sqlite } from "resource://gre/modules/Sqlite.sys.mjs";

/**
 * @param {string} url
 *   The canonical URL of a tab note to look up
 */
const GET_NOTE_BY_URL = `
SELECT
  id,
  canonical_url,
  created,
  note_text
FROM tabnotes
WHERE
  canonical_url = :url
`;

/**
 * @param {string} url
 *   The canonical URL to associate the new tab note
 * @param {string} note
 *   The sanitized text for a tab note
 */
const CREATE_NOTE = `
INSERT INTO tabnotes
  (canonical_url, created, note_text)
VALUES
  (:url, unixepoch("now"), :note)
RETURNING
  id, canonical_url, created, note_text

`;

/**
 * @param {string} url
 *   The canonical URL for the existing tab note
 * @param {string} note
 *   The sanitized text for a tab note
 */
const UPDATE_NOTE = `
UPDATE
  tabnotes
SET
  note_text = :note
WHERE
  canonical_url = :url
RETURNING
  id, canonical_url, created, note_text
`;

/**
 * @param {string} url
 *   The canonical URL of a tab note to delete
 */
const DELETE_NOTE = `
DELETE FROM
  tabnotes
WHERE
  canonical_url = :url
RETURNING
  id, canonical_url, created, note_text
`;

const GET_NOTE_COUNT = `
SELECT COUNT(*) FROM tabnotes
`;

/**
 * Provides the CRUD interface for tab notes.
 */
export class TabNotesStorage {
  DATABASE_FILE_NAME = Object.freeze("tabnotes.sqlite");
  TELEMETRY_SOURCE = Object.freeze({
    TAB_CONTEXT_MENU: "context_menu",
    TAB_HOVER_PREVIEW_PANEL: "hover_menu",
    TAB_NOTE_PREVIEW_PANEL: "note_preview",
  });

  /**
   * In-flight or completed init Promise. Concurrent callers to `init()` share
   * this Promise so that only one `Sqlite.openConnection()` is ever started.
   *
   * @type {Promise<void>|undefined}
   */
  #initPromise;

  /**
   * Single instance of SQLite connection to tabnotes.sqlite. Do not use this
   * directly because it could be undefined; instead, `await this.#connection`
   * to ensure that you always get an initialized connection.
   *
   * @type {OpenedConnection|undefined}
   */
  #databaseConnection;

  /**
   * Reference to the shutdown blocker function, used to remove the blocker
   * when deinit() is called.
   *
   * @see {Sqlite.shutdown}
   * @type {Function|undefined}
   */
  #shutdownBlocker;

  /**
   * Close of the previous connection, which a new `init()` waits for.
   *
   * @type {Promise<void>|undefined}
   */
  #closePromise;

  /** @type {Promise<OpenedConnection>} */
  get #connection() {
    return this.init().then(() => this.#databaseConnection);
  }

  /**
   * @param {object} [options={}]
   * @param {string} [options.basePath=PathUtils.profileDir]
   *   Base file path to a folder where the database file should live.
   *   Defaults to the current profile's root directory.
   * @returns {Promise<void>}
   *   After this Promise completes, TabNotes API functions will work.
   */
  init(options) {
    if (this.#initPromise) {
      return this.#initPromise;
    }

    const basePath = options?.basePath ?? PathUtils.profileDir;
    this.dbPath = PathUtils.join(basePath, this.DATABASE_FILE_NAME);
    const opened = Promise.resolve(this.#closePromise)
      .catch(() => {})
      .then(() => Sqlite.openConnection({ path: this.dbPath }));
    this.#initPromise = opened.then(async connection => {
      this.#databaseConnection = connection;
      this.#shutdownBlocker = () => this.deinit();
      Sqlite.shutdown.addBlocker(
        "Closing tabnotes database",
        this.#shutdownBlocker
      );

      await connection.execute("PRAGMA journal_mode = WAL");
      await connection.execute("PRAGMA wal_autocheckpoint = 16");

      let currentVersion = await connection.getSchemaVersion();

      if (currentVersion == 1) {
        // tabnotes schema is up to date
        return;
      }

      if (currentVersion == 0) {
        // version 0: create `tabnotes` table
        await connection.executeTransaction(async () => {
          await connection.execute(`
          CREATE TABLE IF NOT EXISTS "tabnotes" (
            id            INTEGER PRIMARY KEY,
            canonical_url TEXT NOT NULL,
            created       INTEGER NOT NULL,
            note_text     TEXT NOT NULL
          );`);
          await connection.setSchemaVersion(1);
        });
      }
    });

    return this.#initPromise;
  }

  /**
   * @returns {Promise<void>}
   */
  deinit() {
    if (!this.#initPromise) {
      return this.#closePromise ?? Promise.resolve();
    }
    // Close whatever the current init opens, even if it is still in flight,
    // and let a later init() start a new connection once that is done.
    const pendingInit = this.#initPromise;
    this.#initPromise = undefined;
    const closePromise = pendingInit
      .catch(() => {})
      .then(() => {
        if (this.#shutdownBlocker) {
          Sqlite.shutdown.removeBlocker(this.#shutdownBlocker);
          this.#shutdownBlocker = null;
        }
        const connection = this.#databaseConnection;
        this.#databaseConnection = null;
        return connection?.close();
      })
      .finally(() => {
        if (this.#closePromise == closePromise) {
          this.#closePromise = undefined;
        }
      });
    this.#closePromise = closePromise;
    return closePromise;
  }

  /**
   * @param {MozTabbrowserTab} tab
   * @returns {boolean}
   */
  isEligible(tab) {
    if (tab?.canonicalUrl && URL.canParse(tab.canonicalUrl)) {
      return true;
    }
    return false;
  }

  /**
   * Retrieve a note for a tab, if it exists.
   *
   * @param {MozTabbrowserTab} tab
   *   The tab to check for a note
   * @returns {Promise<TabNoteRecord|undefined>}
   */
  async get(tab) {
    if (!this.isEligible(tab)) {
      return undefined;
    }
    const connection = await this.#connection;
    const results = await connection.executeCached(GET_NOTE_BY_URL, {
      url: tab.canonicalUrl,
    });
    if (!results?.length) {
      return undefined;
    }
    const [result] = results;
    const record = this.#mapDbRowToRecord(result);
    return record;
  }

  /**
   * Set a note for a tab.
   *
   * @param {MozTabbrowserTab} tab
   *   The tab that the note should be associated with
   * @param {string} note
   *   The note itself
   * @param {object} [options]
   * @param {TabNoteTelemetrySource} [options.telemetrySource]
   *   The UI surface that requested to set a note.
   * @returns {Promise<TabNoteRecord>}
   *   The actual note that was set after sanitization
   * @throws {RangeError}
   *   if `tab` is not eligible for a tab note or `note` is empty
   */
  async set(tab, note, options = {}) {
    if (!this.isEligible(tab)) {
      throw new RangeError("Tab notes must be associated to an eligible tab");
    }
    if (!note) {
      throw new RangeError("Tab note text must be provided");
    }

    let existingNote = await this.get(tab);
    let sanitized = this.#sanitizeInput(note);

    if (existingNote && existingNote.text == sanitized) {
      return existingNote;
    }

    const connection = await this.#connection;
    return connection.executeTransaction(async () => {
      if (!existingNote) {
        const insertResult = await connection.executeCached(CREATE_NOTE, {
          url: tab.canonicalUrl,
          note: sanitized,
        });

        const insertedRecord = this.#mapDbRowToRecord(insertResult[0]);
        tab.dispatchEvent(
          new CustomEvent("TabNote:Created", {
            bubbles: true,
            detail: {
              note: insertedRecord,
              telemetrySource: options.telemetrySource,
            },
          })
        );
        return insertedRecord;
      }

      const updateResult = await connection.executeCached(UPDATE_NOTE, {
        url: tab.canonicalUrl,
        note: sanitized,
      });

      const updatedRecord = this.#mapDbRowToRecord(updateResult[0]);
      tab.dispatchEvent(
        new CustomEvent("TabNote:Edited", {
          bubbles: true,
          detail: {
            note: updatedRecord,
            telemetrySource: options.telemetrySource,
          },
        })
      );
      return updatedRecord;
    });
  }

  /**
   * Delete a note for a tab.
   *
   * @param {MozTabbrowserTab} tab
   *   The tab that has a note
   * @param {object} [options]
   * @param {TabNoteTelemetrySource} [options.telemetrySource]
   *   The UI surface that requested to delete a note.
   * @returns {Promise<boolean>}
   *   True if there was a note and it was deleted; false otherwise
   */
  async delete(tab, options = {}) {
    const connection = await this.#connection;
    /** @type {mozIStorageRow[]} */
    const deleteResult = await connection.executeCached(DELETE_NOTE, {
      url: tab.canonicalUrl,
    });

    if (deleteResult?.length > 0) {
      const deletedRecord = this.#mapDbRowToRecord(deleteResult[0]);
      tab.dispatchEvent(
        new CustomEvent("TabNote:Removed", {
          bubbles: true,
          detail: {
            note: deletedRecord,
            telemetrySource: options.telemetrySource,
          },
        })
      );
      return true;
    }

    return false;
  }

  /**
   * Check if a tab has a note.
   *
   * @param {MozTabbrowserTab} tab
   *   The tab to check for a tab note
   * @returns {Promise<boolean>}
   *   True if a note is associated with this URL; false otherwise
   */
  async has(tab) {
    const record = await this.get(tab);
    return record !== undefined;
  }

  /**
   * @returns {Promise<number>}
   *   The number of stored tab notes. Returns 0 if there was a problem
   *   retrieving the real count.
   */
  async count() {
    try {
      const connection = await this.#connection;
      /** @type {mozIStorageRow[]} */
      const countResult = await connection.executeCached(GET_NOTE_COUNT);
      if (countResult?.length == 1) {
        return countResult[0].getDouble(0);
      }
    } catch {}
    return 0;
  }

  /**
   * Clear all notes for all URLs.
   *
   * @returns {Promise<void>}
   */
  async reset() {
    const connection = await this.#connection;
    return connection.execute(`
      DELETE FROM "tabnotes"`);
  }

  /**
   * Given user-supplied note text, returns sanitized note text.
   *
   * @param {string} value
   * @returns {string}
   */
  #sanitizeInput(value) {
    return value.slice(0, 1000);
  }

  /**
   * @param {mozIStorageRow} row
   *   Row returned with the following data shape:
   *   [id: number, canonical_url: string, created: number, note_text: string]
   * @returns {TabNoteRecord}
   */
  #mapDbRowToRecord(row) {
    return {
      id: row.getDouble(0),
      canonicalUrl: row.getString(1),
      created: Temporal.Instant.fromEpochMilliseconds(row.getDouble(2) * 1000),
      text: row.getString(3),
    };
  }
}

// Singleton object accessible from all windows
export const TabNotes = new TabNotesStorage();
