/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const BYTES_PER_MEBIBYTE = 1048576;
const MS_PER_DAY = 86400000;
// Threshold value for removeOldCorruptDBs.
// Corrupt DBs older than this value are removed.
const CORRUPT_DB_RETAIN_DAYS = 14;

// Seconds between maintenance runs.
const MAINTENANCE_INTERVAL_SECONDS = 7 * 86400;

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PlacesPreviews: "resource://gre/modules/PlacesPreviews.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  Sqlite: "resource://gre/modules/Sqlite.sys.mjs",
});

export var PlacesDBUtils = {
  _isShuttingDown: false,

  _clearTaskQueue: false,
  clearPendingTasks() {
    PlacesDBUtils._clearTaskQueue = true;
  },

  /**
   * Executes integrity check and common maintenance tasks.
   */
  async maintenanceOnIdle() {
    let tasks = [
      this.checkIntegrity,
      this.checkCoherence,
      this._refreshUI,
      this.incrementalVacuum,
      this.optimize,
      this.removeOldCorruptDBs,
      this.deleteOrphanPreviews,
    ];
    let timerId = Glean.places.idleMaintenanceTime.start();
    let taskStatusMap = await PlacesDBUtils.runTasks(tasks);

    Services.prefs.setIntPref(
      "places.database.lastMaintenance",
      Math.floor(Date.now() / 1000)
    );
    Glean.places.idleMaintenanceTime.stopAndAccumulate(timerId);
    return taskStatusMap;
  },

  /**
   * Executes integrity check, common and advanced maintenance tasks (like
   * expiration and vacuum).  Will also collect statistics on the database.
   *
   * Note: although this function isn't actually async, we keep it async to
   * allow us to maintain a simple, consistent API for the tasks within this object.
   *
   * @returns {Promise}
   *        A promise that resolves with a Map[taskName(String) -> Object].
   *        The Object has the following properties:
   *         - succeeded: boolean
   *         - logs: an array of strings containing the messages logged by the task.
   */
  async checkAndFixDatabase() {
    let tasks = [
      this.checkIntegrity,
      this.checkCoherence,
      this.expire,
      this.vacuum,
      this.stats,
      this._refreshUI,
    ];
    return PlacesDBUtils.runTasks(tasks);
  },

  /**
   * Forces a full refresh of Places views.
   *
   * Note: although this function isn't actually async, we keep it async to
   * allow us to maintain a simple, consistent API for the tasks within this object.
   *
   * @returns {Promise<void[]>} An empty array.
   */
  async _refreshUI() {
    PlacesObservers.notifyListeners([new PlacesPurgeCaches()]);
    return [];
  },

  /**
   * Checks integrity and tries to fix the database through a reindex.
   *
   * @returns {Promise<string[]>}
   *   Resolves with the logs if database is sane or is made sane. Rejects if
   *   we're unable to fix corruption or unable to check status.
   */
  async checkIntegrity() {
    let logs = [];

    async function check(dbName) {
      try {
        await integrity(dbName);
        logs.push(`The ${dbName} database is sane`);
      } catch (ex) {
        PlacesDBUtils.clearPendingTasks();
        if (ex.result == Cr.NS_ERROR_FILE_CORRUPTED) {
          logs.push(`The ${dbName} database is corrupt`);
          Services.prefs.setCharPref(
            "places.database.replaceDatabaseOnStartup",
            dbName
          );
          throw new Error(
            `Unable to fix corruption, ${dbName} will be replaced on next startup`
          );
        }
        throw new Error(`Unable to check ${dbName} integrity: ${ex}`);
      }
    }

    await check("places.sqlite");
    await check("favicons.sqlite");

    return logs;
  },

  /**
   * Checks data coherence and tries to fix most common errors.
   *
   * @returns {Promise<string[]>}
   *   Resolves with the logs when coherence is checked. Rejects if database is
   *   not coherent.
   */
  async checkCoherence() {
    let logs = [];
    let stmts = await PlacesDBUtils._getCoherenceStatements();
    let coherenceCheck = true;
    await lazy.PlacesUtils.withConnectionWrapper(
      "PlacesDBUtils: coherence check:",
      db =>
        db.executeTransaction(async () => {
          for (let { query, params } of stmts) {
            try {
              await db.execute(query, params || null);
            } catch (ex) {
              console.error(ex);
              coherenceCheck = false;
            }
          }
        })
    );

    if (coherenceCheck) {
      logs.push("The database is coherent");
    } else {
      PlacesDBUtils.clearPendingTasks();
      throw new Error("Unable to complete the coherence check");
    }
    return logs;
  },

  /**
   * Runs incremental vacuum on databases supporting it.
   *
   * @returns {Promise<string[]>}
   *   Resolves with the logs when done. Rejects if we were unable to vacuum.
   */
  async incrementalVacuum() {
    let logs = [];
    return lazy.PlacesUtils.withConnectionWrapper(
      "PlacesDBUtils: incrementalVacuum",
      async db => {
        let count = (
          await db.execute("PRAGMA favicons.freelist_count")
        )[0].getResultByIndex(0);
        if (count < 10) {
          logs.push(
            `The favicons database has only ${count} free pages, not vacuuming.`
          );
        } else {
          logs.push(
            `The favicons database has ${count} free pages, vacuuming.`
          );
          await db.execute("PRAGMA favicons.incremental_vacuum");
          count = (
            await db.execute("PRAGMA favicons.freelist_count")
          )[0].getResultByIndex(0);
          logs.push(
            `The database has been vacuumed and has now ${count} free pages.`
          );
        }
        return logs;
      }
    ).catch(ex => {
      PlacesDBUtils.clearPendingTasks();
      throw new Error(
        "Unable to incrementally vacuum the favicons database " + ex
      );
    });
  },

  /**
   * Expire orphan previews that don't have a Places entry anymore.
   *
   * @returns {Promise<string[]>}
   *   Resolves with the logs when done.
   */
  async deleteOrphanPreviews() {
    let logs = [];
    try {
      let deleted = await lazy.PlacesPreviews.deleteOrphans();
      if (deleted) {
        logs.push(`Orphan previews deleted.`);
      }
    } catch (ex) {
      throw new Error("Unable to delete orphan previews " + ex);
    }
    return logs;
  },

  /**
   * Run PRAGMA optimize to update query planner statistics.
   *
   * @returns {Promise<string[]>}
   *   Resolves with the logs when done.
   */
  async optimize() {
    let logs = [];
    return lazy.PlacesUtils.withConnectionWrapper(
      "PlacesDBUtils: optimize",
      async db => {
        // 0x10012: run ANALYZE on tables that might benefit (0x02), with a row
        // limit to keep runtime bounded (0x10), including tables not queried
        // during this connection (0x10000).
        await db.execute("PRAGMA optimize(0x10012)");
        logs.push("The database has been optimized.");
        return logs;
      }
    ).catch(ex => {
      PlacesDBUtils.clearPendingTasks();
      throw new Error("Unable to optimize the database " + ex);
    });
  },

  async _getCoherenceStatements() {
    let cleanupStatements = [
      // MOZ_PLACES
      // L.1 remove duplicate URLs.
      // This task uses a temp table of potential dupes, and a trigger to remove
      // them. It runs first because it relies on subsequent tasks to clean up
      // orphaned foreign key references. The task works like this: first, we
      // insert all rows with the same hash into the temp table. This lets
      // SQLite use the `url_hash` index for scanning `moz_places`. Hashes
      // aren't unique, so two different URLs might have the same hash. To find
      // the actual dupes, we use a unique constraint on the URL in the temp
      // table. If that fails, we bump the dupe count. Then, we delete all dupes
      // from the table. This fires the cleanup trigger, which updates all
      // foreign key references to point to one of the duplicate Places, then
      // deletes the others.
      {
        query: `CREATE TEMP TABLE IF NOT EXISTS moz_places_dupes_temp(
          id INTEGER PRIMARY KEY
        , hash INTEGER NOT NULL
        , url TEXT UNIQUE NOT NULL
        , count INTEGER NOT NULL DEFAULT 0
        )`,
      },
      {
        query: `CREATE TEMP TRIGGER IF NOT EXISTS moz_places_remove_dupes_temp_trigger
        AFTER DELETE ON moz_places_dupes_temp
        FOR EACH ROW
        BEGIN
          /* Reassign history visits. */
          UPDATE moz_historyvisits SET
            place_id = OLD.id
          WHERE place_id IN (SELECT id FROM moz_places
                             WHERE id <> OLD.id AND
                                   url_hash = OLD.hash AND
                                   url = OLD.url);

          /* Merge autocomplete history entries. */
          INSERT INTO moz_inputhistory(place_id, input, use_count)
          SELECT OLD.id, a.input, a.use_count
          FROM moz_inputhistory a
          JOIN moz_places h ON h.id = a.place_id
          WHERE h.id <> OLD.id AND
                h.url_hash = OLD.hash AND
                h.url = OLD.url
          ON CONFLICT(place_id, input) DO UPDATE SET
            place_id = excluded.place_id,
            use_count = use_count + excluded.use_count;

          /* Merge page annos, ignoring annos with the same name that are
             already set on the destination. */
          INSERT OR IGNORE INTO moz_annos(id, place_id, anno_attribute_id,
                                          content, flags, expiration, type,
                                          dateAdded, lastModified)
          SELECT (SELECT k.id FROM moz_annos k
                  WHERE k.place_id = OLD.id AND
                        k.anno_attribute_id = a.anno_attribute_id), OLD.id,
                 a.anno_attribute_id, a.content, a.flags, a.expiration, a.type,
                 a.dateAdded, a.lastModified
          FROM moz_annos a
          JOIN moz_places h ON h.id = a.place_id
          WHERE h.id <> OLD.id AND
                url_hash = OLD.hash AND
                url = OLD.url;

          /* Reassign bookmarks, and bump the Sync change counter just in case
             we have new keywords. */
          UPDATE moz_bookmarks SET
            fk = OLD.id,
            syncChangeCounter = syncChangeCounter + 1
          WHERE fk IN (SELECT id FROM moz_places
                       WHERE url_hash = OLD.hash AND
                             url = OLD.url);

          /* Reassign keywords. */
          UPDATE moz_keywords SET
            place_id = OLD.id
          WHERE place_id IN (SELECT id FROM moz_places
                             WHERE id <> OLD.id AND
                                   url_hash = OLD.hash AND
                                   url = OLD.url);

          /* Now that we've updated foreign key references, drop the
             conflicting source. */
          DELETE FROM moz_places
          WHERE id <> OLD.id AND
                url_hash = OLD.hash AND
                url = OLD.url;

          /* Recalculate frecency for the destination. */
          UPDATE moz_places SET recalc_frecency = 1, recalc_alt_frecency = 1
          WHERE id = OLD.id;
        END`,
      },
      {
        query: `INSERT INTO moz_places_dupes_temp(id, hash, url, count)
        SELECT h.id, h.url_hash, h.url, 1
        FROM moz_places h
        JOIN (SELECT url_hash FROM moz_places
              GROUP BY url_hash
              HAVING count(*) > 1) d ON d.url_hash = h.url_hash
        ON CONFLICT(url) DO UPDATE SET
          count = count + 1`,
      },
      { query: `DELETE FROM moz_places_dupes_temp WHERE count > 1` },
      { query: `DROP TABLE moz_places_dupes_temp` },

      // MOZ_ANNO_ATTRIBUTES
      // A.1 remove obsolete annotations from moz_annos.
      // The 'weave0' idiom exploits character ordering (0 follows /) to
      // efficiently select all annos with a 'weave/' prefix.
      {
        query: `DELETE FROM moz_annos
        WHERE type = 4 OR anno_attribute_id IN (
          SELECT id FROM moz_anno_attributes
          WHERE name = 'downloads/destinationFileName' OR
                name BETWEEN 'weave/' AND 'weave0'
        )`,
      },

      // A.3 remove unused attributes.
      {
        query: `DELETE FROM moz_anno_attributes WHERE id IN (
          SELECT id FROM moz_anno_attributes n
          WHERE NOT EXISTS
              (SELECT id FROM moz_annos WHERE anno_attribute_id = n.id LIMIT 1)
        )`,
      },

      // MOZ_ANNOS
      // B.1 remove annos with an invalid attribute
      {
        query: `DELETE FROM moz_annos WHERE id IN (
          SELECT id FROM moz_annos a
          WHERE NOT EXISTS
            (SELECT id FROM moz_anno_attributes
              WHERE id = a.anno_attribute_id LIMIT 1)
        )`,
      },

      // B.2 remove orphan annos
      {
        query: `DELETE FROM moz_annos WHERE id IN (
          SELECT id FROM moz_annos a
          WHERE NOT EXISTS
            (SELECT id FROM moz_places WHERE id = a.place_id LIMIT 1)
        )`,
      },

      // D.1 remove items that are not uri bookmarks from tag containers
      {
        query: `DELETE FROM moz_bookmarks WHERE guid NOT IN (
          :rootGuid, :menuGuid, :toolbarGuid, :unfiledGuid, :tagsGuid  /* skip roots */
        ) AND id IN (
          SELECT b.id FROM moz_bookmarks b
          WHERE b.parent IN
            (SELECT id FROM moz_bookmarks WHERE parent = :tags_folder)
            AND b.type <> :bookmark_type
        )`,
        params: {
          tags_folder: lazy.PlacesUtils.tagsFolderId,
          bookmark_type: lazy.PlacesUtils.bookmarks.TYPE_BOOKMARK,
          rootGuid: lazy.PlacesUtils.bookmarks.rootGuid,
          menuGuid: lazy.PlacesUtils.bookmarks.menuGuid,
          toolbarGuid: lazy.PlacesUtils.bookmarks.toolbarGuid,
          unfiledGuid: lazy.PlacesUtils.bookmarks.unfiledGuid,
          tagsGuid: lazy.PlacesUtils.bookmarks.tagsGuid,
        },
      },

      // D.1.1 remove tag entries for urls that are not bookmarked
      // A bookmark with a missing parent still counts as a bookmark, D.3 will
      // move it to the unfiled folder.
      {
        query: `DELETE FROM moz_bookmarks WHERE id IN (
          SELECT b.id FROM moz_bookmarks b
          JOIN moz_bookmarks t ON t.id = b.parent
          WHERE t.parent = :tags_folder
            AND NOT EXISTS (
              SELECT 1 FROM moz_bookmarks b2
              LEFT JOIN moz_bookmarks p2 ON p2.id = b2.parent
              WHERE b2.fk = b.fk
                AND p2.parent IS NOT :tags_folder
            )
        )`,
        params: {
          tags_folder: lazy.PlacesUtils.tagsFolderId,
        },
      },

      // D.2 remove empty tags
      {
        query: `DELETE FROM moz_bookmarks WHERE guid NOT IN (
          :rootGuid, :menuGuid, :toolbarGuid, :unfiledGuid, :tagsGuid  /* skip roots */
        ) AND id IN (
          SELECT b.id FROM moz_bookmarks b
          WHERE b.id IN
            (SELECT id FROM moz_bookmarks WHERE parent = :tags_folder)
            AND NOT EXISTS
              (SELECT id from moz_bookmarks WHERE parent = b.id LIMIT 1)
        )`,
        params: {
          tags_folder: lazy.PlacesUtils.tagsFolderId,
          rootGuid: lazy.PlacesUtils.bookmarks.rootGuid,
          menuGuid: lazy.PlacesUtils.bookmarks.menuGuid,
          toolbarGuid: lazy.PlacesUtils.bookmarks.toolbarGuid,
          unfiledGuid: lazy.PlacesUtils.bookmarks.unfiledGuid,
          tagsGuid: lazy.PlacesUtils.bookmarks.tagsGuid,
        },
      },

      // D.3 move orphan items to unsorted folder
      {
        query: `UPDATE moz_bookmarks SET
          parent = (SELECT id FROM moz_bookmarks WHERE guid = :unfiledGuid)
        WHERE guid NOT IN (
          :rootGuid, :menuGuid, :toolbarGuid, :unfiledGuid, :tagsGuid  /* skip roots */
        ) AND id IN (
          SELECT b.id FROM moz_bookmarks b
          WHERE NOT EXISTS
            (SELECT id FROM moz_bookmarks WHERE id = b.parent LIMIT 1)
        )`,
        params: {
          rootGuid: lazy.PlacesUtils.bookmarks.rootGuid,
          menuGuid: lazy.PlacesUtils.bookmarks.menuGuid,
          toolbarGuid: lazy.PlacesUtils.bookmarks.toolbarGuid,
          unfiledGuid: lazy.PlacesUtils.bookmarks.unfiledGuid,
          tagsGuid: lazy.PlacesUtils.bookmarks.tagsGuid,
        },
      },

      // D.4 Insert tombstones for any synced items with the wrong type.
      // Sync doesn't support changing the type of an existing item while
      // keeping its GUID. To avoid confusing other clients, we insert
      // tombstones for all synced items with the wrong type, so that we
      // can reupload them with the correct type and a new GUID.
      {
        query: `INSERT OR IGNORE INTO moz_bookmarks_deleted(guid, dateRemoved)
                SELECT guid, :dateRemoved
                FROM moz_bookmarks
                WHERE syncStatus <> :syncStatus AND
                      ((type IN (:folder_type, :separator_type) AND
                        fk NOTNULL) OR
                       (type = :bookmark_type AND
                        fk IS NULL) OR
                       type IS NULL)`,
        params: {
          dateRemoved: lazy.PlacesUtils.toPRTime(new Date()),
          syncStatus: lazy.PlacesUtils.bookmarks.SYNC_STATUS.NEW,
          bookmark_type: lazy.PlacesUtils.bookmarks.TYPE_BOOKMARK,
          folder_type: lazy.PlacesUtils.bookmarks.TYPE_FOLDER,
          separator_type: lazy.PlacesUtils.bookmarks.TYPE_SEPARATOR,
        },
      },

      // D.5 fix wrong item types
      // Folders and separators should not have an fk.
      // If they have a valid fk, convert them to bookmarks, and give them new
      // GUIDs. If the item has children, we'll move them to the unfiled root
      // in D.8. If the `fk` doesn't exist in `moz_places`, we'll remove the
      // item in D.9.
      {
        query: `UPDATE moz_bookmarks
        SET guid = GENERATE_GUID(),
            type = :bookmark_type
        WHERE guid NOT IN (
          :rootGuid, :menuGuid, :toolbarGuid, :unfiledGuid, :tagsGuid  /* skip roots */
        ) AND id IN (
          SELECT id FROM moz_bookmarks b
          WHERE type IN (:folder_type, :separator_type)
            AND fk NOTNULL
        )`,
        params: {
          bookmark_type: lazy.PlacesUtils.bookmarks.TYPE_BOOKMARK,
          folder_type: lazy.PlacesUtils.bookmarks.TYPE_FOLDER,
          separator_type: lazy.PlacesUtils.bookmarks.TYPE_SEPARATOR,
          rootGuid: lazy.PlacesUtils.bookmarks.rootGuid,
          menuGuid: lazy.PlacesUtils.bookmarks.menuGuid,
          toolbarGuid: lazy.PlacesUtils.bookmarks.toolbarGuid,
          unfiledGuid: lazy.PlacesUtils.bookmarks.unfiledGuid,
          tagsGuid: lazy.PlacesUtils.bookmarks.tagsGuid,
        },
      },

      // D.6 fix wrong item types
      // Bookmarks should have an fk, if they don't have any, convert them to
      // folders.
      {
        query: `UPDATE moz_bookmarks
        SET guid = GENERATE_GUID(),
            type = :folder_type
        WHERE guid NOT IN (
          :rootGuid, :menuGuid, :toolbarGuid, :unfiledGuid, :tagsGuid  /* skip roots */
        ) AND id IN (
          SELECT id FROM moz_bookmarks b
          WHERE type = :bookmark_type
            AND fk IS NULL
        )`,
        params: {
          bookmark_type: lazy.PlacesUtils.bookmarks.TYPE_BOOKMARK,
          folder_type: lazy.PlacesUtils.bookmarks.TYPE_FOLDER,
          rootGuid: lazy.PlacesUtils.bookmarks.rootGuid,
          menuGuid: lazy.PlacesUtils.bookmarks.menuGuid,
          toolbarGuid: lazy.PlacesUtils.bookmarks.toolbarGuid,
          unfiledGuid: lazy.PlacesUtils.bookmarks.unfiledGuid,
          tagsGuid: lazy.PlacesUtils.bookmarks.tagsGuid,
        },
      },

      // D.7 fix wrong item types
      // `moz_bookmarks.type` doesn't have a NOT NULL constraint, so it's
      // possible for an item to not have a type (bug 1586427).
      {
        query: `UPDATE moz_bookmarks
        SET guid = GENERATE_GUID(),
            type = CASE WHEN fk NOT NULL THEN :bookmark_type ELSE :folder_type END
        WHERE guid NOT IN (
         :rootGuid, :menuGuid, :toolbarGuid, :unfiledGuid, :tagsGuid  /* skip roots */
        ) AND type IS NULL`,
        params: {
          bookmark_type: lazy.PlacesUtils.bookmarks.TYPE_BOOKMARK,
          folder_type: lazy.PlacesUtils.bookmarks.TYPE_FOLDER,
          rootGuid: lazy.PlacesUtils.bookmarks.rootGuid,
          menuGuid: lazy.PlacesUtils.bookmarks.menuGuid,
          toolbarGuid: lazy.PlacesUtils.bookmarks.toolbarGuid,
          unfiledGuid: lazy.PlacesUtils.bookmarks.unfiledGuid,
          tagsGuid: lazy.PlacesUtils.bookmarks.tagsGuid,
        },
      },

      // D.8 fix wrong parents
      // Items cannot have separators or other bookmarks
      // as parent, if they have bad parent move them to unsorted bookmarks.
      {
        query: `UPDATE moz_bookmarks SET
          parent = (SELECT id FROM moz_bookmarks WHERE guid = :unfiledGuid)
        WHERE guid NOT IN (
          :rootGuid, :menuGuid, :toolbarGuid, :unfiledGuid, :tagsGuid  /* skip roots */
        ) AND id IN (
          SELECT id FROM moz_bookmarks b
          WHERE EXISTS
            (SELECT id FROM moz_bookmarks WHERE id = b.parent
              AND type IN (:bookmark_type, :separator_type)
              LIMIT 1)
        )`,
        params: {
          bookmark_type: lazy.PlacesUtils.bookmarks.TYPE_BOOKMARK,
          separator_type: lazy.PlacesUtils.bookmarks.TYPE_SEPARATOR,
          rootGuid: lazy.PlacesUtils.bookmarks.rootGuid,
          menuGuid: lazy.PlacesUtils.bookmarks.menuGuid,
          toolbarGuid: lazy.PlacesUtils.bookmarks.toolbarGuid,
          unfiledGuid: lazy.PlacesUtils.bookmarks.unfiledGuid,
          tagsGuid: lazy.PlacesUtils.bookmarks.tagsGuid,
        },
      },

      // D.9 remove items without a valid place
      // We've already converted folders with an `fk` to bookmarks in D.5,
      // and bookmarks without an `fk` to folders in D.6. However, the `fk`
      // might not reference an existing `moz_places.id`, even if it's
      // NOT NULL. This statement takes care of those.
      {
        query: `DELETE FROM moz_bookmarks AS b
        WHERE b.guid NOT IN (
          :rootGuid, :menuGuid, :toolbarGuid, :unfiledGuid, :tagsGuid  /* skip roots */
        ) AND b.fk NOT NULL
          AND b.type = :bookmark_type
          AND NOT EXISTS (SELECT 1 FROM moz_places h WHERE h.id = b.fk)`,
        params: {
          bookmark_type: lazy.PlacesUtils.bookmarks.TYPE_BOOKMARK,
          rootGuid: lazy.PlacesUtils.bookmarks.rootGuid,
          menuGuid: lazy.PlacesUtils.bookmarks.menuGuid,
          toolbarGuid: lazy.PlacesUtils.bookmarks.toolbarGuid,
          unfiledGuid: lazy.PlacesUtils.bookmarks.unfiledGuid,
          tagsGuid: lazy.PlacesUtils.bookmarks.tagsGuid,
        },
      },

      // D.10 fix non-consecutive positions.
      {
        query: `
          WITH positions(item_id, pos, seq) AS (
            SELECT id, position AS pos,
                   (row_number() OVER (PARTITION BY parent ORDER BY position)) - 1 AS seq
            FROM moz_bookmarks
          )
          UPDATE moz_bookmarks
          SET position = seq
          FROM positions
          WHERE item_id = moz_bookmarks.id AND seq <> pos`,
      },

      // D.12 Fix empty-named tags.
      // Tags were allowed to have empty names due to a UI bug.  Fix them by
      // replacing their title with "(notitle)", and bumping the change counter
      // for all bookmarks with the fixed tags.
      {
        query: `UPDATE moz_bookmarks SET syncChangeCounter = syncChangeCounter + 1
         WHERE fk IN (SELECT b.fk FROM moz_bookmarks b
                      JOIN moz_bookmarks p ON p.id = b.parent
                      WHERE length(p.title) = 0 AND p.type = :folder_type AND
                            p.parent = :tags_folder)`,
        params: {
          folder_type: lazy.PlacesUtils.bookmarks.TYPE_FOLDER,
          tags_folder: lazy.PlacesUtils.tagsFolderId,
        },
      },
      {
        query: `UPDATE moz_bookmarks SET title = :empty_title
        WHERE length(title) = 0 AND type = :folder_type
          AND parent = :tags_folder`,
        params: {
          empty_title: "(notitle)",
          folder_type: lazy.PlacesUtils.bookmarks.TYPE_FOLDER,
          tags_folder: lazy.PlacesUtils.tagsFolderId,
        },
      },

      // MOZ_ICONS
      // E.1 remove orphan icon entries.
      {
        query: `DELETE FROM moz_pages_w_icons WHERE page_url_hash NOT IN (
          SELECT url_hash FROM moz_places
        )`,
      },

      // Remove orphan icons.
      {
        query: `DELETE FROM moz_icons WHERE id IN (
          SELECT id FROM moz_orphan_icons
        )`,
      },

      // MOZ_HISTORYVISITS
      // F.1 remove orphan visits
      {
        query: `DELETE FROM moz_historyvisits WHERE id IN (
          SELECT id FROM moz_historyvisits v
          WHERE NOT EXISTS
            (SELECT id FROM moz_places WHERE id = v.place_id LIMIT 1)
        )`,
      },

      // MOZ_INPUTHISTORY
      // G.1 remove orphan input history
      {
        query: `DELETE FROM moz_inputhistory WHERE place_id IN (
          SELECT place_id FROM moz_inputhistory i
          WHERE NOT EXISTS
            (SELECT id FROM moz_places WHERE id = i.place_id LIMIT 1)
        )`,
      },

      // MOZ_KEYWORDS
      // I.1 remove unused keywords
      {
        query: `DELETE FROM moz_keywords WHERE id IN (
          SELECT id FROM moz_keywords k
          WHERE NOT EXISTS
            (SELECT 1 FROM moz_places h WHERE k.place_id = h.id)
        )`,
      },

      // MOZ_PLACES
      // L.2 recalculate visit_count and last_visit_date
      {
        query: `UPDATE moz_places
        SET visit_count = (SELECT count(*) FROM moz_historyvisits
                            WHERE place_id = moz_places.id AND visit_type NOT IN (0,4,7,8,9)),
            last_visit_date = (SELECT MAX(visit_date) FROM moz_historyvisits
                                WHERE place_id = moz_places.id)
        WHERE id IN (
          SELECT h.id FROM moz_places h
          WHERE visit_count <> (SELECT count(*) FROM moz_historyvisits v
                                WHERE v.place_id = h.id AND visit_type NOT IN (0,4,7,8,9))
              OR last_visit_date IS NOT
                (SELECT MAX(visit_date) FROM moz_historyvisits v WHERE v.place_id = h.id)
        )`,
      },

      // L.3 recalculate hidden for redirects.
      {
        query: `UPDATE moz_places
        SET hidden = 1
        WHERE id IN (
          SELECT h.id FROM moz_places h
          JOIN moz_historyvisits src ON src.place_id = h.id
          JOIN moz_historyvisits dst ON dst.from_visit = src.id AND dst.visit_type IN (5,6)
          LEFT JOIN moz_bookmarks on fk = h.id AND fk ISNULL
          GROUP BY src.place_id HAVING count(*) = visit_count
        )`,
      },

      // L.4 recalculate foreign_count.
      {
        query: `UPDATE moz_places SET foreign_count =
          (SELECT count(*) FROM moz_bookmarks WHERE fk = moz_places.id ) +
          (SELECT count(*) FROM moz_keywords WHERE place_id = moz_places.id )`,
      },

      // L.5 recalculate missing hashes.
      {
        query: `UPDATE moz_places SET url_hash = hash(url) WHERE url_hash = 0`,
      },

      // L.6 fix invalid Place GUIDs.
      {
        query: `UPDATE moz_places
        SET guid = GENERATE_GUID()
        WHERE guid IS NULL OR
              NOT IS_VALID_GUID(guid)`,
      },

      // MOZ_BOOKMARKS
      // S.1 fix invalid GUIDs for synced bookmarks.
      // This requires multiple related statements.
      // First, we insert tombstones for all synced bookmarks with invalid
      // GUIDs, so that we can delete them on the server. Second, we add a
      // temporary trigger to bump the change counter for the parents of any
      // items we update, since Sync stores the list of child GUIDs on the
      // parent. Finally, we assign new GUIDs for all items with missing and
      // invalid GUIDs, bump their change counters, and reset their sync
      // statuses to NEW so that they're considered for deduping.
      {
        query: `INSERT OR IGNORE INTO moz_bookmarks_deleted(guid, dateRemoved)
        SELECT guid, :dateRemoved
        FROM moz_bookmarks
        WHERE syncStatus <> :syncStatus AND
              guid NOT NULL AND
              NOT IS_VALID_GUID(guid)`,
        params: {
          dateRemoved: lazy.PlacesUtils.toPRTime(new Date()),
          syncStatus: lazy.PlacesUtils.bookmarks.SYNC_STATUS.NEW,
        },
      },
      {
        query: `UPDATE moz_bookmarks
        SET guid = GENERATE_GUID(),
            syncStatus = :syncStatus
        WHERE guid IS NULL OR
              NOT IS_VALID_GUID(guid)`,
        params: {
          syncStatus: lazy.PlacesUtils.bookmarks.SYNC_STATUS.NEW,
        },
      },

      // S.2 drop tombstones for bookmarks that aren't deleted.
      {
        query: `DELETE FROM moz_bookmarks_deleted
        WHERE guid IN (SELECT guid FROM moz_bookmarks)`,
      },

      // S.3 set missing added and last modified dates.
      {
        query: `UPDATE moz_bookmarks
        SET dateAdded = COALESCE(NULLIF(dateAdded, 0), NULLIF(lastModified, 0), NULLIF((
              SELECT MIN(visit_date) FROM moz_historyvisits
              WHERE place_id = fk
            ), 0), STRFTIME('%s', 'now', 'localtime', 'utc') * 1000000),
            lastModified = COALESCE(NULLIF(lastModified, 0), NULLIF(dateAdded, 0), NULLIF((
              SELECT MAX(visit_date) FROM moz_historyvisits
              WHERE place_id = fk
            ), 0), STRFTIME('%s', 'now', 'localtime', 'utc') * 1000000)
        WHERE NULLIF(dateAdded, 0) IS NULL OR
              NULLIF(lastModified, 0) IS NULL`,
      },

      // S.4 reset added dates that are ahead of last modified dates.
      {
        query: `UPDATE moz_bookmarks
         SET dateAdded = lastModified
         WHERE dateAdded > lastModified`,
      },
    ];

    // Create triggers for updating Sync metadata. The "sync change" trigger
    // bumps the parent's change counter when we update a GUID or move an item
    // to a different folder, since Sync stores the list of child GUIDs on the
    // parent. The "sync tombstone" trigger inserts tombstones for deleted
    // synced bookmarks.
    cleanupStatements.unshift({
      query: `CREATE TEMP TRIGGER IF NOT EXISTS moz_bm_sync_change_temp_trigger
      AFTER UPDATE OF guid, parent, position ON moz_bookmarks
      FOR EACH ROW
      BEGIN
        UPDATE moz_bookmarks
        SET syncChangeCounter = syncChangeCounter + 1
        WHERE id IN (OLD.parent, NEW.parent, NEW.id);
      END`,
    });
    cleanupStatements.unshift({
      query: `CREATE TEMP TRIGGER IF NOT EXISTS moz_bm_sync_tombstone_temp_trigger
      AFTER DELETE ON moz_bookmarks
      FOR EACH ROW WHEN OLD.guid NOT NULL AND
                        OLD.syncStatus <> 1
      BEGIN
        UPDATE moz_bookmarks
        SET syncChangeCounter = syncChangeCounter + 1
        WHERE id = OLD.parent;

        INSERT INTO moz_bookmarks_deleted(guid, dateRemoved)
        VALUES(OLD.guid, STRFTIME('%s', 'now', 'localtime', 'utc') * 1000000);
      END`,
    });
    cleanupStatements.push({
      query: `DROP TRIGGER moz_bm_sync_change_temp_trigger`,
    });
    cleanupStatements.push({
      query: `DROP TRIGGER moz_bm_sync_tombstone_temp_trigger`,
    });

    return cleanupStatements;
  },

  /**
   * Tries to vacuum the database.
   *
   * Note: although this function isn't actually async, we keep it async to
   * allow us to maintain a simple, consistent API for the tasks within this object.
   *
   * @returns {Promise<string[]>}
   *   Resolves when database is vacuumed to an array of logs for this task.
   *   Rejects if we are unable to vacuum database.
   */
  async vacuum() {
    let logs = [];
    let placesDbPath = PathUtils.join(PathUtils.profileDir, "places.sqlite");
    let info = await IOUtils.stat(placesDbPath);
    logs.push(`Initial database size is ${Math.floor(info.size / 1024)}KiB`);
    return lazy.PlacesUtils.withConnectionWrapper(
      "PlacesDBUtils: vacuum",
      async db => {
        await db.execute("VACUUM");
        logs.push("The database has been vacuumed");
        info = await IOUtils.stat(placesDbPath);
        logs.push(`Final database size is ${Math.floor(info.size / 1024)}KiB`);
        return logs;
      }
    ).catch(() => {
      PlacesDBUtils.clearPendingTasks();
      throw new Error("Unable to vacuum database");
    });
  },

  /**
   * Forces a full expiration on the database.
   *
   * Note: although this function isn't actually async, we keep it async to
   * allow us to maintain a simple, consistent API for the tasks within this object.
   *
   * @returns {Promise<string[]>}
   *   Resolves with the logs when the database in cleaned up.
   */
  async expire() {
    let logs = [];

    let expiration = Cc["@mozilla.org/places/expiration;1"].getService(
      Ci.nsIObserver
    );

    let returnPromise = new Promise(res => {
      let observer = (subject, topic) => {
        Services.obs.removeObserver(observer, topic);
        logs.push("Database cleaned up");
        res(logs);
      };
      Services.obs.addObserver(
        observer,
        lazy.PlacesUtils.TOPIC_EXPIRATION_FINISHED
      );
    });

    // Typescript sees that expiration can either be an object with the observe
    // method or a function with the signature of observe.
    if (typeof expiration !== "function") {
      expiration.observe(null, "places-debug-start-expiration", "0");
    }
    return returnPromise;
  },

  /**
   * Collects statistical data on the database.
   *
   * @returns {Promise<string[]>}
   *   Resolves with the logs when statistics are collected. Rejects if we are
   *   unable to collect stats for some reason.
   */
  async stats() {
    let logs = [];
    let placesDbPath = PathUtils.join(PathUtils.profileDir, "places.sqlite");
    let info = await IOUtils.stat(placesDbPath);
    logs.push(`Places.sqlite size is ${Math.floor(info.size / 1024)}KiB`);
    let faviconsDbPath = PathUtils.join(
      PathUtils.profileDir,
      "favicons.sqlite"
    );
    info = await IOUtils.stat(faviconsDbPath);
    logs.push(`Favicons.sqlite size is ${Math.floor(info.size / 1024)}KiB`);

    // Execute each step async.
    let pragmas = [
      "user_version",
      "page_size",
      "cache_size",
      "journal_mode",
      "synchronous",
    ].map(p => `pragma_${p}`);
    let pragmaQuery = `SELECT * FROM ${pragmas.join(", ")}`;
    await lazy.PlacesUtils.withConnectionWrapper(
      "PlacesDBUtils: pragma for stats",
      async db => {
        let row = (await db.execute(pragmaQuery))[0];
        for (let i = 0; i != pragmas.length; i++) {
          logs.push(`${pragmas[i]} is ${row.getResultByIndex(i)}`);
        }
      }
    ).catch(() => {
      logs.push("Could not set pragma for stat collection");
    });

    // Get maximum number of unique URIs.
    try {
      /**
       * A partial shape of nsPlacesExpiration, just for what we use in stats.
       *
       * @typedef {object} ExpirationWrappedJSObject
       * @property {function(): Promise<number>} getPagesLimit
       *   A function that returns the maximum number of pages that should be
       *   retained.
       */

      // This has to be type cast because wrappedJSObject is an object.
      let expiration = /** @type {ExpirationWrappedJSObject} */ (
        Cc["@mozilla.org/places/expiration;1"].getService(Ci.nsISupports)
          .wrappedJSObject
      );
      let limitURIs = await expiration.getPagesLimit();
      logs.push(
        "History can store a maximum of " + limitURIs + " unique pages"
      );
    } catch (ex) {}

    let query = "SELECT name FROM sqlite_master WHERE type = :type";
    let params = {};
    let _getTableCount = async tableName => {
      let db = await lazy.PlacesUtils.promiseDBConnection();
      let rows = await db.execute(`SELECT count(*) FROM ${tableName}`);
      logs.push(
        `Table ${tableName} has ${rows[0].getResultByIndex(0)} records`
      );
    };

    try {
      params.type = "table";
      let db = await lazy.PlacesUtils.promiseDBConnection();
      await db.execute(query, params, r =>
        _getTableCount(r.getResultByIndex(0))
      );
    } catch (ex) {
      throw new Error("Unable to collect stats.");
    }

    let details = await PlacesDBUtils.getEntitiesStats();
    logs.push(
      `Pages sequentiality: ${details.get("moz_places").sequentialityPerc}`
    );
    let entities = Array.from(details.keys()).sort((a, b) => {
      return details.get(a).sizePerc - details.get(b).sizePerc;
    });
    for (let key of entities) {
      let value = details.get(key);
      logs.push(
        `${key}: ${value.sizeBytes / 1024}KiB (${value.sizePerc}%), ${
          value.efficiencyPerc
        }% eff.`
      );
    }

    return logs;
  },

  /**
   * Collects telemetry data and reports it to Telemetry.
   *
   * Note: although this function isn't actually async, we keep it async to
   * allow us to maintain a simple, consistent API for the tasks within this object.
   *
   */
  async telemetry() {
    // The following array contains an ordered list of entries that are
    // processed to collect telemetry data.  Each entry has these properties:
    //
    //  distribution: The Glean distribution metric to update.
    //  quantity:  The Glean quantity metric to update.
    //  query:     This is optional.  If present, contains a database command
    //             that will be executed asynchronously, and whose result will
    //             be added to the telemetry histogram.
    //  callback:  This is optional.  If present, contains a function that must
    //             return the value that will be added to the glean metric.
    //             If a query is also present, its result is passed
    //             as the first argument of the function.  If the function
    //             raises an exception, no data is added to the metric.
    let probes = [
      {
        distribution: Glean.places.pagesCount,
        query: "SELECT count(*) FROM moz_places",
      },

      {
        distribution: Glean.places.bookmarksCount,
        query: `SELECT count(*) FROM moz_bookmarks b
                    JOIN moz_bookmarks t ON t.id = b.parent
                    AND t.parent <> :tags_folder
                    WHERE b.type = :type_bookmark`,
        params: {
          tags_folder: lazy.PlacesUtils.tagsFolderId,
          type_bookmark: lazy.PlacesUtils.bookmarks.TYPE_BOOKMARK,
        },
      },

      {
        distribution: Glean.places.tagsCount,
        query: `SELECT count(*) FROM moz_bookmarks
                    WHERE parent = :tags_folder`,
        params: {
          tags_folder: lazy.PlacesUtils.tagsFolderId,
        },
      },

      {
        distribution: Glean.places.keywordsCount,
        query: "SELECT count(*) FROM moz_keywords",
      },

      {
        distribution: Glean.places.sortedBookmarksPerc,
        query: `SELECT IFNULL(ROUND((
                      SELECT count(*) FROM moz_bookmarks b
                      JOIN moz_bookmarks p ON p.id = b.parent
                      JOIN moz_bookmarks g ON g.id = p.parent
                      WHERE g.guid <> :root_guid
                        AND g.guid <> :tags_guid
                        AND b.type  = :type_bookmark
                      ) * 100 / (
                      SELECT count(*) FROM moz_bookmarks b
                      JOIN moz_bookmarks p ON p.id = b.parent
                      JOIN moz_bookmarks g ON g.id = p.parent
                      AND g.guid <> :tags_guid
                      WHERE b.type = :type_bookmark
                    )), 0)`,
        params: {
          root_guid: lazy.PlacesUtils.bookmarks.rootGuid,
          tags_guid: lazy.PlacesUtils.bookmarks.tagsGuid,
          type_bookmark: lazy.PlacesUtils.bookmarks.TYPE_BOOKMARK,
        },
      },

      {
        distribution: Glean.places.taggedBookmarksPerc,
        query: `SELECT IFNULL(ROUND((
                      SELECT count(*) FROM moz_bookmarks b
                      JOIN moz_bookmarks t ON t.id = b.parent
                      AND t.parent = :tags_folder
                      ) * 100 / (
                      SELECT count(*) FROM moz_bookmarks b
                      JOIN moz_bookmarks t ON t.id = b.parent
                      AND t.parent <> :tags_folder
                      WHERE b.type = :type_bookmark
                    )), 0)`,
        params: {
          tags_folder: lazy.PlacesUtils.tagsFolderId,
          type_bookmark: lazy.PlacesUtils.bookmarks.TYPE_BOOKMARK,
        },
      },

      {
        distribution: Glean.places.databaseFilesize,
        async callback() {
          let placesDbPath = PathUtils.join(
            PathUtils.profileDir,
            "places.sqlite"
          );
          let info = await IOUtils.stat(placesDbPath);
          return Math.floor(info.size / BYTES_PER_MEBIBYTE);
        },
      },

      {
        distribution: Glean.places.databaseFaviconsFilesize,
        async callback() {
          let faviconsDbPath = PathUtils.join(
            PathUtils.profileDir,
            "favicons.sqlite"
          );
          let info = await IOUtils.stat(faviconsDbPath);
          return Math.floor(info.size / BYTES_PER_MEBIBYTE);
        },
      },

      {
        distribution: Glean.places.databaseSemanticHistoryFilesize,
        async callback() {
          let semanticDbPath = PathUtils.join(
            PathUtils.profileDir,
            "places_semantic.sqlite"
          );
          let info = await IOUtils.stat(semanticDbPath);
          return Math.floor(info.size / BYTES_PER_MEBIBYTE);
        },
      },

      {
        distribution: Glean.places.annosPagesCount,
        query: "SELECT count(*) FROM moz_annos",
      },

      {
        distribution: Glean.places.maintenanceDaysfromlast,
        callback() {
          try {
            let lastMaintenance = Services.prefs.getIntPref(
              "places.database.lastMaintenance"
            );
            let nowSeconds = Math.floor(Date.now() / 1000);
            return Math.floor((nowSeconds - lastMaintenance) / 86400);
          } catch (ex) {
            return 60;
          }
        },
      },
      {
        quantity: Glean.places.pagesNeedFrecencyRecalculation,
        query: "SELECT count(*) FROM moz_places WHERE recalc_frecency = 1",
      },
      {
        quantity: Glean.places.previousdayVisits,
        query: `SELECT COUNT(*) from moz_places
                      WHERE hidden=0 AND last_visit_date < (strftime('%s', 'now', 'start of day') * 1000000)
                      AND last_visit_date > (strftime('%s', 'now', 'start of day', '-1 day') * 1000000)
                      AND last_visit_date IS NOT NULL;`,
      },
    ];

    for (let probe of probes) {
      try {
        let val;
        if ("query" in probe) {
          let db = await lazy.PlacesUtils.promiseDBConnection();
          val = (
            await db.execute(probe.query, probe.params || {})
          )[0].getResultByIndex(0);
        }
        // Report the result of the probe through Telemetry.
        // The resulting promise cannot reject.
        if ("callback" in probe) {
          val = await probe.callback();
        }
        if (probe.distribution) {
          // Memory distributions have the method named 'accumulate'
          // instead of 'accumulateSingleSample'.
          if ("accumulateSingleSample" in probe.distribution) {
            probe.distribution.accumulateSingleSample(val);
          } else if ("accumulate" in probe.distribution) {
            probe.distribution.accumulate(val);
          }
        } else if (probe.quantity) {
          probe.quantity.set(val);
        } else {
          throw new Error("Unknwon telemetry probe type");
        }
      } catch (ex) {
        // We want to execute the next steps anyway, for certain probes it is
        // expected that the file may be missing.
        if (ex.code != DOMException.NOT_FOUND_ERR) {
          console.error(ex);
        }
      }
    }
  },

  /**
   * Remove old and useless places.sqlite.corrupt files.
   *
   * @returns {Promise<Array<string>>}
   *   Resolves to an array of logs for this task.
   */
  async removeOldCorruptDBs() {
    let logs = [];
    logs.push(
      "> Cleanup profile from places.sqlite.corrupt files older than " +
        CORRUPT_DB_RETAIN_DAYS +
        " days."
    );
    let re = /places\.sqlite(-\d)?\.corrupt$/;
    let currentTime = Date.now();
    let children = (await IOUtils.getChildren(PathUtils.profileDir)).filter(
      entry => re.test(entry)
    );
    for (let entry of children) {
      let fileInfo;
      try {
        fileInfo = await IOUtils.stat(entry);
      } catch (error) {
        logs.push("Could not stat file: " + entry, error);
        continue;
      }
      if (fileInfo.type == "regular") {
        try {
          // Convert milliseconds to days.
          let days = Math.ceil(
            (currentTime - fileInfo.lastModified) / MS_PER_DAY
          );
          if (days >= CORRUPT_DB_RETAIN_DAYS || days < 0) {
            await IOUtils.remove(entry);
          }
        } catch (error) {
          logs.push("Could not remove file: " + entry, error);
        }
      }
    }
    return logs;
  },

  /**
   * Gets detailed statistics about database entities like tables and indices.
   *
   * @returns {Promise<Map<string, object>>}
   *   A Map by table name, containing an object with the following properties:
   *   - efficiencyPerc: percentage filling of pages, an high efficiency means
   *     most pages are filled up almost completely. This value is not
   *     particularly useful with a low number of pages.
   *   - sizeBytes: size of the entity in bytes
   *   - pages: number of pages of the entity
   *   - sizePerc: percentage of the total database size
   *   - sequentialityPerc: percentage of sequential pages, this is a global
   *     value of the database, thus it's the same for every entity, and it can
   *     be used to evaluate fragmentation and the need for vacuum.
   */
  async getEntitiesStats() {
    let db = await lazy.PlacesUtils.promiseDBConnection();
    let rows = await db.execute(`
      /* do not warn (bug no): no need for index */
      SELECT name,
      round((pgsize - unused) * 100.0 / pgsize, 1) as efficiency_perc,
      pgsize as size_bytes, pageno as pages,
      round(pgsize * 100.0 / (SELECT sum(pgsize) FROM dbstat WHERE aggregate = TRUE), 1) as size_perc,
      round((
        WITH s(row, pageno) AS (
          SELECT row_number() OVER (ORDER BY path), pageno FROM dbstat ORDER BY path
        )
        SELECT sum(s1.pageno+1==s2.pageno)*100.0/count(*)
        FROM s AS s1, s AS s2
        WHERE s1.row+1=s2.row
      ), 1) AS sequentiality_perc
      FROM dbstat
      WHERE aggregate = TRUE
    `);
    let entitiesByName = new Map();
    for (let row of rows) {
      let details = {
        efficiencyPerc: row.getResultByName("efficiency_perc"),
        pages: row.getResultByName("pages"),
        sizeBytes: row.getResultByName("size_bytes"),
        sizePerc: row.getResultByName("size_perc"),
        sequentialityPerc: row.getResultByName("sequentiality_perc"),
      };
      entitiesByName.set(row.getResultByName("name"), details);
    }
    return entitiesByName;
  },

  /**
   * Gets detailed statistics about database entities and their respective row
   * counts.
   *
   * @returns {Promise<Array<{entity: string, count: number}>>}
   *   An array that augments each object returned by {@link getEntitiesStats}
   *   with the following extra properties:
   *   - entity: name of the entity
   *   - count: row count of the entity
   */
  async getEntitiesStatsAndCounts() {
    let stats = await PlacesDBUtils.getEntitiesStats();
    let data = [];
    let db = await lazy.PlacesUtils.promiseDBConnection();
    for (let [entity, value] of stats) {
      let count = "-";
      try {
        if (
          entity.startsWith("moz_") &&
          !entity.endsWith("index") &&
          entity != "moz_places_visitcount" /* bug in index name */
        ) {
          count = (
            await db.execute(`SELECT count(*) FROM ${entity}`)
          )[0].getResultByIndex(0);
        }
      } catch (ex) {
        console.error(
          "Error raised while gathering stats on the Places database: ",
          ex
        );
      }
      data.push(Object.assign(value, { entity, count }));
    }
    return data;
  },

  /**
   * Runs a list of tasks, returning a Map when done.
   *
   * @param {Array<Function>} tasks
   *   An array of tasks to be executed, in the form of pointers to methods in
   *   this module.
   *
   * @returns {Promise<Map<string, {succeeded: boolean, logs: Array<string>}>>}
   *   A promise that resolves with a Map. The key is the taskname (a string)
   *   and the value is an object with the following properties:
   *   - succeeded: Whether the task succeeded.
   *   - logs: An array of strings containing the messages logged by the task.
   */
  async runTasks(tasks) {
    if (!this._registeredShutdownObserver) {
      this._registeredShutdownObserver = true;
      lazy.PlacesUtils.registerShutdownFunction(() => {
        this._isShuttingDown = true;
      });
    }
    PlacesDBUtils._clearTaskQueue = false;
    let tasksMap = new Map();
    for (let task of tasks) {
      if (PlacesDBUtils._isShuttingDown) {
        tasksMap.set(task.name, {
          succeeded: false,
          logs: ["Shutting down, will not schedule the task."],
        });
        continue;
      }

      if (PlacesDBUtils._clearTaskQueue) {
        tasksMap.set(task.name, {
          succeeded: false,
          logs: ["The task queue was cleared by an error in another task."],
        });
        continue;
      }

      let result = await task()
        .then((logs = [`${task.name} complete`]) => ({ succeeded: true, logs }))
        .catch(err => ({ succeeded: false, logs: [err.message] }));
      tasksMap.set(task.name, result);
    }
    return tasksMap;
  },

  /**
   * Helper used by FxBackup to remove downloads metadata from a copy of the Places
   * database, for profile migration to another install (such as on another
   * machine).
   *
   * @param {string} placesDbPath Full path to places.sqlite database to filter
   *                              downloads metadata from.
   */
  async removeDownloadsMetadataFromDb(placesDbPath) {
    // Don't create the database if it doesn't exist.
    if (!(await IOUtils.exists(placesDbPath))) {
      return;
    }

    let connection;
    try {
      connection = await lazy.Sqlite.openConnection({
        path: placesDbPath,
      });
      const removeDownloads = `
        -- Find download annotations
        WITH found_annos AS (
            SELECT a.id AS anno_id
            FROM moz_annos a
            JOIN moz_anno_attributes attr
              ON a.anno_attribute_id = attr.id
            WHERE INSTR(attr.name, 'downloads/') = 1
        )
        -- Delete downloads from moz_annos but leave the URLs in moz_places history
        DELETE FROM moz_annos
        WHERE id IN (SELECT anno_id FROM found_annos);
      `;
      await connection.execute(removeDownloads);
    } finally {
      await connection?.close();
    }
  },
};

async function integrity(dbName) {
  async function check(db) {
    /** @type {mozIStorageRow?} */
    let row;
    await db.execute("PRAGMA integrity_check", null, (r, cancel) => {
      row = r;
      cancel();
    });
    // @ts-ignore - nsIVariant has no overlap with other Javascript types
    return row?.getResultByIndex(0) === "ok";
  }

  // Create a new connection for this check, so we can operate independently
  // from a broken Places service.
  // openConnection returns an exception with .result == Cr.NS_ERROR_FILE_CORRUPTED,
  // we should do the same everywhere we want maintenance to try replacing the
  // database on next startup.
  let path = PathUtils.join(PathUtils.profileDir, dbName);
  let db = await lazy.Sqlite.openConnection({ path });
  try {
    if (await check(db)) {
      return;
    }

    // We stopped due to an integrity corruption, try to fix it if possible.
    // First, try to reindex, this often fixes simple indices problems.
    try {
      await db.execute("REINDEX");
    } catch (ex) {
      throw Components.Exception(
        "Impossible to reindex database",
        Cr.NS_ERROR_FILE_CORRUPTED
      );
    }

    // Check again.
    if (!(await check(db))) {
      throw Components.Exception(
        "The database is still corrupt",
        Cr.NS_ERROR_FILE_CORRUPTED
      );
    }
  } finally {
    await db.close();
  }
}

export function PlacesDBUtilsIdleMaintenance() {}

PlacesDBUtilsIdleMaintenance.prototype = {
  observe(subject, topic) {
    switch (topic) {
      case "idle-daily": {
        // Once a week run places.sqlite maintenance tasks.
        let lastMaintenance = Services.prefs.getIntPref(
          "places.database.lastMaintenance",
          0
        );
        let nowSeconds = Math.floor(Date.now() / 1000);
        if (lastMaintenance < nowSeconds - MAINTENANCE_INTERVAL_SECONDS) {
          PlacesDBUtils.maintenanceOnIdle();
        }
        break;
      }
      default:
        throw new Error("Trying to handle an unknown category.");
    }
  },
  classID: Components.ID("d38926e0-29c1-11eb-8588-0800200c9a66"),
  QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),
};
