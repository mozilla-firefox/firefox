/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import { EnrollmentStatus } from "./Telemetry.sys.mjs" */
/** @import { DeferredTask } from "../../../modules/DeferredTask.sys.mjs" */
/** @import { OpenedConnection, Sqlite } from "../../../modules/Sqlite.sys.mjs" */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusTelemetry: "resource://nimbus/lib/Telemetry.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  ProfilesDatastoreService:
    "moz-src:///toolkit/profile/ProfilesDatastoreService.sys.mjs",
  RemoteSettingsSyncError:
    "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs",

  log: () => {
    const { Logger } = ChromeUtils.importESModule(
      "resource://messaging-system/lib/Logger.sys.mjs"
    );
    return new Logger("NimbusEnrollments");
  },
});

/**
 * How long should we wait before flushing changes to the database.
 *
 * This was copied from a similar constant in JSONFile.sys.mjs.
 */
const FLUSH_DELAY_MS = 1500;

const PERSISTENCE_ENABLED = Services.prefs.getBoolPref(
  "nimbus.persistence.enabled",
  true
);

const PendingWriteKind = Object.freeze({
  INSERT: "insert",
  UPDATE: "update",
  DELETE: "delete",
});

/**
 * @typedef {object} _PendingInsert
 * A pending write for an enrollment or an unenrollment that has not yet had its
 * enrollment written to the database.
 *
 * @property {"insert"} kind
 * The kind of the pending write.
 *
 * @property {object} enrollment
 * The enrollment being recorded in the database.
 *
 * @property {object} recipe
 * The recipe for the enrollment.
 */

/**
 * @typedef {object} _PendingUpdate
 * A pending write for an unenrollment.
 *
 * @property {"update"} kind
 * The kind of the pending write.
 *
 * @property {object} enrollment
 * The enrollment being updated to inactive in the database.
 */

/**
 * @typedef {object} _PendingDelete
 * A pending delete for an enrollment.
 *
 * @property {"delete"} kind
 * The kind of the pending write.
 */

/**
 * @typedef {_PendingInsert | _PendingUpdate | _PendingDelete} PendingWrite
 *
 * A pending write to the `NimbusEnrollments` database.
 */

/**
 * @typedef {object} EnrollmentStatusEntry
 * A database entry for an enrollment status that occurred in the past.
 *
 * @property {EnrollmentStatus} status
 * The enrollment status event payload.
 *
 * @property {Date} timestamp
 * The timestamp when the enrollment status event was recorded.
 *
 * @property {number} index
 * A per-slug monotonically increasing identifier.
 *
 * This field is used to determine which enrollment status entry is the latest
 * so that we do not have to deal with time-specific issues (e.g., clock drift),
 * or the reuse of `rowid`s.
 */

export class PendingWrites {
  static Kind = PendingWriteKind;

  /**
   * Create a new `PendingWrites` object.
   *
   * @param {PendingWrites} other A `PendingWrites` object to clone.
   */
  constructor(other = undefined) {
    /**
     * Enrollment changes to flush to the database.
     *
     * @type {Map<string, PendingWrite>}
     */
    this.enrollments = new Map(other?.enrollments.entries() ?? []);

    /**
     * Sync timestamps to flush to the database.
     *
     * @type {Map<string, object | null>}
     */
    this.syncTimestamps = new Map(other?.syncTimestamps.entries() ?? []);

    /**
     * @type {Map<string, EnrollmentStatusEntry[]>}
     */
    this.enrollmentStatuses = new Map(
      other?.enrollmentStatuses.entries() ?? []
    );
  }

  /**
   * Whether or not there are pending writes.
   *
   * @returns {boolean}
   */
  get hasPendingWrites() {
    return (
      this.enrollments.size > 0 ||
      this.syncTimestamps.size > 0 ||
      this.enrollmentStatuses.size > 0
    );
  }

  /**
   * Record a new enrollment or re-enrollment.
   *
   * @param {object} enrollment The enrollment.
   * @param {string} enrollment.slug The slug of the enrollment.
   * @param {object} recipe The recipe for the enrollment.
   */
  recordEnrollment(enrollment, recipe) {
    this.enrollments.set(enrollment.slug, {
      kind: PendingWriteKind.INSERT,
      enrollment: structuredClone(enrollment),
      recipe,
    });
  }

  /**
   * Record an unenrollment.
   *
   * @param {object} enrollment The enrollment.
   * @param {string} enrollment.slug The slug of the enrollment.
   */
  recordUnenrollment(enrollment) {
    // If there is no existing pending write for this enrollment, create a new
    // update.
    //
    // Otherwise if there is a pending insertion, update the associated
    // unenrollment, which will cause the insert to write an inactive enrollment.
    //
    // It is **impossible** for an enrollment:
    // - to follow a previous enrollment; or
    // - to follow a deletion.
    const existingWrite = this.enrollments.get(enrollment.slug);
    if (!existingWrite) {
      this.enrollments.set(enrollment.slug, {
        kind: PendingWriteKind.UPDATE,
        enrollment: structuredClone(enrollment),
      });
    } else if (existingWrite.kind === PendingWriteKind.INSERT) {
      existingWrite.enrollment = structuredClone(enrollment);
    }
  }

  /**
   * Delete an enrollmment and all its enrollment statuses.
   *
   * This only happens when garbage collecting old enrollments.
   *
   * @param {string} slug The slug of the enrollment to remove.
   */
  deleteEnrollment(slug) {
    this.enrollments.set(slug, { kind: PendingWriteKind.DELETE });
    this.enrollmentStatuses.delete(slug);
  }

  /**
   * Record an enrollment status entry.
   *
   * @param {string} slug
   * The slug of enrollment whose status is being updated.
   *
   * @param {EnrollmentStatusEntry} entry
   * The enrollment status entry to record.
   */
  recordEnrollmentStatus(slug, entry) {
    this.enrollmentStatuses.getOrInsertComputed(slug, () => []).push(entry);
  }

  /**
   * Merge the two collections of pending writes together.
   *
   * @param {PendingWrites} first The first set of pending writes to apply.
   * @param {PendingWrites} second The second set of pending writes to apply.
   * Pending syncTimestamps in this set will override those set in `first`.
   *
   * @returns {PendingWrites}
   */
  static merge(first, second) {
    if (!first.hasPendingWrites) {
      return second;
    } else if (!second.hasPendingWrites) {
      return first;
    }

    const merged = new PendingWrites(first);

    // `deleteEnrollment` also deletes the corresponding enrollment statuses.
    // Merge the enrollment statuses first so that we do not end up with
    // dangling statuses, e.g., if `first` contains an enrollment and `second`
    // contains a delete.
    for (const [slug, entries] of second.enrollmentStatuses.entries()) {
      merged.enrollmentStatuses
        .getOrInsertComputed(slug, () => [])
        .push(...entries);
    }

    for (const [slug, pendingWrite] of second.enrollments.entries()) {
      switch (pendingWrite.kind) {
        case PendingWriteKind.INSERT:
          merged.recordEnrollment(pendingWrite.enrollment, pendingWrite.recipe);
          break;

        case PendingWriteKind.UPDATE:
          merged.recordUnenrollment(pendingWrite.enrollment);
          break;

        case PendingWriteKind.DELETE:
          merged.deleteEnrollment(slug);
          break;
      }
    }

    for (const [collection, lastModified] of second.syncTimestamps.entries()) {
      merged.syncTimestamps.set(collection, lastModified);
    }

    return merged;
  }
}

/**
 * Handles queueing changes to the NimbusEnrollments database table in the
 * shared profiles database.
 */
export class NimbusEnrollments {
  /**
   * Whether the NimbusEvents instance is initialized or not.
   *
   * @type {boolean}
   */
  #initialized;

  /**
   * The task that will flush the changes to the database on a timer.
   *
   * @type {DeferredTask}
   */
  #flushTask;

  /**
   * Our shutdown blocker that will ensure we flush pending writes before the
   * ProfilesDatastoreService closes its database connection.
   *
   * @type {(function(): void) | null}
   */
  #shutdownBlocker;

  /**
   * Whether or not we've started shutdown and will accept new pending writes.
   *
   * @type {boolean}
   */
  #finalized;

  /**
   * Pending writes that will be flushed in `#flush()`.
   *
   * @type {PendingWrites}
   */
  #pendingWrites;

  /**
   * The lastModified times of the Nimbus Remote Settings collections.
   *
   * This will always contain the most up to date set of timestamps, even if
   * they are not flushed to the database.
   *
   * @type {Map<string, number> | null}
   */
  #syncTimestamps;

  /**
   * The most recent enrollment status entry for each slug.
   *
   * Recency is determined by the {@link EnrollmentStatusEntry.index index}
   * field.
   *
   * @type {Map<string, EnrollmentStatusEntry>}
   */
  #latestEnrollmentStatuses;

  /**
   * Whether or not to submit reduced enrollment status telemetry.
   *
   * @type {boolean| undefined}
   */
  #reducedTelemetryEnabled;

  constructor() {
    this.#initialized = false;

    this.#flushTask = new lazy.DeferredTask(
      this.#flush.bind(this),
      FLUSH_DELAY_MS
    );

    this.#shutdownBlocker = this.finalize.bind(this);
    lazy.ProfilesDatastoreService.shutdown.addBlocker(
      "NimbusEnrollments: writing to database",
      this.#shutdownBlocker
    );
    this.#finalized = false;

    this.#pendingWrites = new PendingWrites();
    this.#syncTimestamps = null;
    this.#reducedTelemetryEnabled = undefined;
  }

  /**
   * Whether or not to submit reduced enrollment status telemetry.
   *
   * @returns {boolean}
   */
  get reducedTelemetryEnabled() {
    if (typeof this.#reducedTelemetryEnabled === "undefined") {
      this.#reducedTelemetryEnabled =
        lazy.NimbusFeatures.nimbusTelemetry.getVariable(
          "reducedEnrollmentStatusTelemetryEnabled"
        ) ?? false;
    }

    return this.#reducedTelemetryEnabled;
  }

  async init() {
    if (this.#initialized) {
      throw new Error("Already initialized");
    }

    this.#initialized = true;

    const conn = await lazy.ProfilesDatastoreService.getConnection();
    return conn.executeTransaction(async txn => {
      const enrollments = await NimbusEnrollments.loadEnrollments(txn);
      this.#syncTimestamps = await NimbusEnrollments.loadSyncTimestamps(txn);
      this.#latestEnrollmentStatuses =
        await NimbusEnrollments.loadEnrollmentStatuses(txn);

      return enrollments;
    });
  }

  /**
   * The number of pending writes.
   */
  get pendingWrites() {
    return this.#pendingWrites.enrollments.size;
  }

  /**
   * Queue writing an enrollment (or re-enrollment) to the database.
   *
   * @param {object} enrollment The enrollment.
   * @param {string} enrollment.slug The slug of the enrollment.
   * @param {object} recipe The recipe for the enrollment.
   */
  recordEnrollment(enrollment, recipe) {
    if (this.#finalized) {
      lazy.log.debug(
        `Did not queue update for enrollment ${enrollment.slug}: already finalized`
      );
      return;
    }

    this.#pendingWrites.recordEnrollment(enrollment, recipe);
    this.#flushSoon();

    lazy.log.debug(`Queued insert for enrollment ${enrollment.slug}`);
  }

  /**
   * Queue writing an unenrollment to the database.
   *
   * @param {object} enrollment The enrollment.
   * @param {string} enrollment.slug The slug of the enrollment.
   */
  recordUnenrollment(enrollment) {
    if (this.#finalized) {
      lazy.log.debug(
        `Did not queue update for enrollment ${enrollment.slug}: already finalized`
      );
      return;
    }

    this.#pendingWrites.recordUnenrollment(enrollment);
    this.#flushSoon();

    lazy.log.debug(`Queued update for enrollment ${enrollment.slug}`);
  }

  /**
   * Queue removal from the database for the enrollments pertaining to the given
   * slugs.
   *
   * @param {string[]} slugs The slugs of enrollments to remove.
   */
  deleteEnrollments(slugs) {
    if (!slugs.length) {
      return;
    }

    if (this.#finalized) {
      lazy.log.debug(`Did not queue removal: already finalized`);
      return;
    }

    for (const slug of slugs) {
      this.#pendingWrites.deleteEnrollment(slug);
      this.#latestEnrollmentStatuses.delete(slug);
    }
    this.#flushSoon();

    lazy.log.debug(`Queued removal for ${slugs.length} enrollments`);
  }

  /**
   * @param {string} slug
   * @param {EnrollmentStatus} status
   */
  recordEnrollmentStatus(slug, status) {
    const lastEnrollmentStatus = this.#latestEnrollmentStatuses.get(slug);

    if (
      !lastEnrollmentStatus ||
      !NimbusEnrollments.enrollmentStatusEqual(
        lastEnrollmentStatus.status,
        status
      )
    ) {
      const entry = {
        index: (lastEnrollmentStatus?.index ?? 0) + 1,
        status,
        timestamp: NimbusEnrollments.now(),
      };
      this.#pendingWrites.recordEnrollmentStatus(slug, entry);
      this.#latestEnrollmentStatuses.set(slug, entry);

      this.#flushSoon();

      if (this.reducedTelemetryEnabled) {
        NimbusEnrollments.#recordEnrollmentStatusTelemetry(slug, status);
      }
    }

    if (!this.reducedTelemetryEnabled) {
      NimbusEnrollments.#recordEnrollmentStatusTelemetry(slug, status);
    }
  }

  /**
   * Return a copy of the the latest enrollment status entry for the given slug.
   *
   * @param {string} slug The slug whose enrollment status is to be returned.
   *
   * @returns {EnrollmentStatusEntry | null}
   * The latest enrollment status entry for the given slug, or `null` if one
   * does not exist.
   */
  getLatestEnrollmentStatus(slug) {
    return structuredClone(this.#latestEnrollmentStatuses.get(slug));
  }

  /**
   * Update the known lastModified timestamps for the given collections.
   *
   * Omitted timestamps will not change.
   *
   * @param {Map<string, number>} timestamps The timestamps to update.
   *
   * @throws {RemoteSettingsSyncError} If any timestamps are behind currently known
   * timestamps or if any timestamps are invalid.
   */
  updateSyncTimestamps(timestamps) {
    if (!this.#initialized) {
      throw new Error("Not initialized");
    }

    const mergedTimestamps = new Map(this.#syncTimestamps.entries());
    let timestampsChanged = false;

    for (const [collection, timestamp] of timestamps) {
      const lastTimestamp = this.#syncTimestamps.get(collection);

      if (typeof timestamp !== "number" || isNaN(timestamp) || timestamp < 0) {
        throw new lazy.RemoteSettingsSyncError(
          collection,
          lazy.NimbusTelemetry.RemoteSettingsSyncErrorReason
            .INVALID_LAST_MODIFIED
        );
      } else if (
        typeof lastTimestamp === "undefined" ||
        timestamp > lastTimestamp
      ) {
        mergedTimestamps.set(collection, timestamp);
        this.#pendingWrites.syncTimestamps.set(collection, timestamp);

        timestampsChanged = true;
      } else if (timestamp < lastTimestamp) {
        throw new lazy.RemoteSettingsSyncError(
          collection,
          lazy.NimbusTelemetry.RemoteSettingsSyncErrorReason.BACKWARDS_SYNC
        );
      }
    }

    if (timestampsChanged) {
      this.#syncTimestamps = mergedTimestamps;
      this.#flushSoon();

      lazy.log.debug("Timestamps updated");
    }
  }

  /**
   * Immediately flush all pending updates to the NimbusEnrollments table.
   *
   * ** TEST ONLY **
   */
  async _flushNow() {
    // If the flush is already running, wait for it to finish.
    if (this.#flushTask.isRunning) {
      await this.#flushTask._runningPromise;
    }

    // If the flush task is armed, disarm it and run it immediately.
    if (this.#flushTask.isArmed) {
      this.#flushTask.disarm();
      await this.#flush();
    }
  }

  /**
   * Queue a flush to happen soon (within {@link FLUSH_DELAY_MS}).
   */
  #flushSoon() {
    if (this.#finalized) {
      return;
    }

    this.#flushTask.arm();
  }

  /**
   * Flush all pending updates to the NimbusEnrollments and NimbusSyncTimestamps
   * tables.
   *
   * The updates are done as a single transaction to ensure the database stays
   * in a consistent state.
   *
   * If the transaction fails, the write will be re-attempted after
   * {@link FLUSH_DELAY_MS}, unless we have already begun shutdown, in which we
   * will attempt to flush once more immediately.
   *
   * @param {object} options
   * @param {boolean} options.retryOnFailure Whether or not to retry flushing if
   * an error occurs. Should only be false if we have failed to flush once and
   * we've started shutting down.
   */
  async #flush({ retryOnFailure = true } = {}) {
    if (!this.#pendingWrites.hasPendingWrites) {
      lazy.log.debug(`Not flushing: no changes`);
      return;
    }

    // Swap the set of pending writes, if there are any, with default values.
    // While we are waiting for the writes to the database complete we may
    // receive more pending writes. If our write to the database fails, we need
    // to reconcile those changes by merging them.
    const pendingWrites = this.#pendingWrites;
    this.#pendingWrites = new PendingWrites();

    lazy.log.debug(
      `Flushing ${pendingWrites.enrollments.size} enrollments, ${pendingWrites.syncTimestamps.size} timestamps, ${pendingWrites.enrollmentStatuses.size} enrollment statuses to database`
    );

    let success = true;
    try {
      const conn = await lazy.ProfilesDatastoreService.getConnection();
      if (!conn) {
        // The database has already closed. There's nothing we can do.
        //
        // This *should not happen* since we have a shutdown blocker preventing
        // the connection from being closed, but it may happen if we're already
        // in shutdown when we try to flush for the first time, e.g., during a
        // very short-lived session.
        lazy.log.debug(
          `Not flushing changes to database: connection is closed`
        );
        return;
      }

      await conn.executeTransaction(async txn => {
        const deletedEnrollmentSlugs = new Set();
        for (const [
          slug,
          pendingWrite,
        ] of pendingWrites.enrollments.entries()) {
          switch (pendingWrite.kind) {
            case PendingWriteKind.INSERT:
              await NimbusEnrollments.#insertEnrollment(
                txn,
                pendingWrite.enrollment,
                pendingWrite.recipe
              );
              break;

            case PendingWriteKind.UPDATE:
              await NimbusEnrollments.#updateEnrollment(
                txn,
                pendingWrite.enrollment
              );
              break;

            case PendingWriteKind.DELETE:
              await NimbusEnrollments.#deleteEnrollment(txn, slug);
              deletedEnrollmentSlugs.add(slug);
              break;
          }
        }

        await NimbusEnrollments.#flushSyncTimestamps(
          txn,
          pendingWrites.syncTimestamps
        );

        await NimbusEnrollments.#flushEnrollmentStatuses(
          txn,
          pendingWrites.enrollmentStatuses,
          deletedEnrollmentSlugs
        );
      });
    } catch (e) {
      success = false;

      if (retryOnFailure) {
        // Re-queue all the pending writes that failed and merge any new changes
        // that happened while we attempted to save.
        this.#pendingWrites = PendingWrites.merge(
          pendingWrites,
          this.#pendingWrites
        );

        if (!this.#finalized) {
          lazy.log.error(
            `ExperimentStore: Failed writing enrollments to NimbusEnrollments; will retry soon`,
            e
          );

          // Ensure we try to flush again if we aren't in shutdown yet.
          this.#flushSoon();
        } else {
          lazy.log.error(
            `ExperimentStore: Failed writing enrollments to NimbusEnrollments during shutdown; retrying immediately`,
            e
          );

          // If we are in our shutdown blocker, we aren't going to get another
          // chance and there's not really anything we can do except try to
          // write again immediately.
          await this.#flush({ retryOnFailure: false });
        }
      } else {
        lazy.log.error(
          `ExperimentStore: Failed writing enrollments to NimbusEnrollments`,
          e
        );
      }
    }

    Glean.nimbusEvents.databaseWrite.record({ success });
  }

  /**
   * Record an enrollment or re-enrollment to the database.
   *
   * If a previous enrollment exists it will be overwritten.
   *
   * @param {OpenedConnection} conn
   * The database connection.
   *
   * @param {object} enrollment
   * The enrollment being written to the database.
   *
   * @param {object} recipe
   * The recipe for the enrollment.
   */
  static async #insertEnrollment(conn, enrollment, recipe) {
    await conn.executeCached(
      `
        INSERT INTO NimbusEnrollments(
          profileId,
          slug,
          branchSlug,
          recipe,
          active,
          unenrollReason,
          lastSeen,
          setPrefs,
          prefFlips,
          source
        )
        VALUES(
          :profileId,
          :slug,
          :branchSlug,
          jsonb(:recipe),
          :active,
          :unenrollReason,
          :lastSeen,
          jsonb(:setPrefs),
          jsonb(:prefFlips),
          :source
        )
        ON CONFLICT(profileId, slug)
        DO UPDATE SET
          branchSlug = excluded.branchSlug,
          recipe = excluded.recipe,
          active = excluded.active,
          unenrollReason = excluded.unenrollReason,
          lastSeen = excluded.lastSeen,
          setPrefs = excluded.setPrefs,
          prefFlips = excluded.prefFlips,
          source = excluded.source;
      `,
      {
        profileId: lazy.ExperimentAPI.profileId,
        slug: enrollment.slug,
        branchSlug: enrollment.branch.slug,
        recipe: JSON.stringify(recipe),
        active: enrollment.active,
        unenrollReason: enrollment.unenrollReason ?? null,
        lastSeen: enrollment.lastSeen,
        setPrefs:
          enrollment.active && enrollment.prefs
            ? JSON.stringify(enrollment.prefs)
            : null,
        prefFlips:
          enrollment.active && enrollment.prefFlips
            ? JSON.stringify(enrollment.prefFlips)
            : null,
        source: enrollment.source,
      }
    );

    lazy.log.debug(
      `Created ${enrollment.active ? "active" : "inactive"} enrollment ${enrollment.slug}`
    );
  }

  /**
   * Record an update to an existing enrollment.
   *
   * In practice, this always corresponds to an unenrollment.
   *
   * @param {OpenedConnection} conn
   * The database connection
   *
   * @param {object} enrollment
   * The enrollment being updated in the database.
   */
  static async #updateEnrollment(conn, enrollment) {
    await conn.executeCached(
      `
        UPDATE NimbusEnrollments SET
          active = false,
          unenrollReason = :unenrollReason,
          setPrefs = null,
          prefFlips = null
        WHERE
          profileId = :profileId AND
          slug = :slug;
      `,
      {
        profileId: lazy.ExperimentAPI.profileId,
        slug: enrollment.slug,
        unenrollReason: enrollment.unenrollReason,
      }
    );

    lazy.log.debug(`Updated enrollment ${enrollment.slug} to be inactive`);
  }

  /**
   * Remove an expired enrollment from the `NimbusEnrollments` table.
   *
   * This will also remove all `NimbusEnrollmentStatuses` rows for that slug.
   *
   * @param {OpenedConnection} conn The connection to the database.
   * @param {string} slug The slug of the enrollment to delete.
   */
  static async #deleteEnrollment(conn, slug) {
    await conn.execute(
      `
        DELETE FROM NimbusEnrollments
        WHERE
          profileId = :profileId AND
          slug = :slug;
      `,
      {
        profileId: lazy.ExperimentAPI.profileId,
        slug,
      }
    );

    await conn.execute(
      `
        DELETE FROM NimbusEnrollmentStatuses
        WHERE
          profileId = :profileId AND
          slug = :slug;
      `,
      { profileId: lazy.ExperimentAPI.profileId, slug }
    );

    lazy.log.debug(`Deleted expired enrollment ${slug}`);
  }

  /**
   * Finalize the flush task and ensure we flush all pending enrollment updates
   * to the NimbusEnrollments table.
   *
   * As soon as this function is called, all new enrollment updates will be
   * refused.
   *
   * This is used as a shutdown blocker that blocks the {@link Sqlite.shutdown}
   * shutdown barrier.
   */
  async finalize() {
    if (this.#finalized) {
      return;
    }

    this.#finalized = true;
    await this.#flushTask.finalize();

    lazy.ProfilesDatastoreService.shutdown.removeBlocker(this.#shutdownBlocker);
    this.#shutdownBlocker = null;
  }

  /**
   * Flush the sync timestamps to the database.
   *
   * @param {OpenedConnection} conn
   * The database connection.
   *
   * @param {Map<string, number>} timestamps
   * The sync timestamps.
   */
  static async #flushSyncTimestamps(conn, timestamps) {
    for (const [collection, lastModified] of timestamps) {
      await conn.executeCached(
        `
          INSERT INTO NimbusSyncTimestamps(
            profileId,
            collection,
            lastModified
          )
          VALUES(
            :profileId,
            :collection,
            :lastModified
          )
          ON CONFLICT(profileId, collection)
          DO UPDATE SET
            lastModified = excluded.lastModified;
        `,
        {
          profileId: lazy.ExperimentAPI.profileId,
          collection,
          lastModified,
        }
      );
    }
  }

  /**
   * Flush all pending enrollment status entries to the database.
   *
   * @param {OpenedConnection} conn
   * The database connection.
   *
   * @param {Map<string, EnrollmentStatusEntry[]>} entries
   * The entries to write to the database.
   */
  static async #flushEnrollmentStatuses(conn, statuses) {
    for (const [slug, entries] of statuses.entries()) {
      for (const entry of entries) {
        await conn.executeCached(
          `
          INSERT INTO NimbusEnrollmentStatuses(
            profileId,
            slug,
            timestamp,
            entryIndex,
            status
          )
          VALUES(
            :profileId,
            :slug,
            :timestamp,
            :entryIndex,
            jsonb(:status)
          );
          `,
          {
            profileId: lazy.ExperimentAPI.profileId,
            slug,
            timestamp: entry.timestamp.toISOString(),
            entryIndex: entry.index,
            status: JSON.stringify(entry.status),
          }
        );
      }
    }
  }

  /**
   * @param {string} slug
   * @param {EnrollmentStatus} enrollmentStatus
   */
  static #recordEnrollmentStatusTelemetry(slug, enrollmentStatus) {
    const { status, reason, branch, error, conflictSlugs, migration } =
      enrollmentStatus;

    // Do not record unsupported feature telemetry.
    if (
      reason === lazy.NimbusTelemetry.EnrollmentStatusReason.ERROR &&
      error ===
        lazy.NimbusTelemetry.ValidationFailureReason.UNSUPPORTED_FEATURES
    ) {
      return;
    }

    Glean.nimbusEvents.enrollmentStatus.record({
      slug,
      status,
      reason,
      branch,
      error_string: error,
      conflict_slug: conflictSlugs?.join(","),
      migration,
    });
  }

  /**
   * Whether or not reading from and writing to the NimbusEnrollments table is
   * enabled.
   *
   * This can only be false in test environments.
   */
  static get persistenceEnabled() {
    return PERSISTENCE_ENABLED;
  }

  /**
   * Load the enrollments from the NimbusEnrollments table.

   * @param {OpenedConnection} txn An optional connection, used when this is
   * called within a transaction.
   *
   * @returns {Promise<Record<string, object>>} The enrollments from the
   * NimbusEnrollments table.
   */
  static async loadEnrollments(txn) {
    function copyProperties(target, src, properties) {
      for (const property of properties) {
        target[property] = src[property];
      }
    }

    function processRow(row) {
      const enrollment = {};

      for (const key of ["slug", "lastSeen", "source"]) {
        enrollment[key] = row.getResultByName(key);
      }

      enrollment.active = Boolean(row.getResultByName("active"));

      const unenrollReason = row.getResultByName("unenrollReason");
      if (unenrollReason) {
        enrollment.unenrollReason = unenrollReason;
      }

      const setPrefs = JSON.parse(row.getResultByName("setPrefs"));
      if (setPrefs) {
        enrollment.prefs = setPrefs;
      }

      const prefFlips = JSON.parse(row.getResultByName("prefFlips"));
      if (prefFlips) {
        enrollment.prefFlips = prefFlips;
      }

      const recipe = JSON.parse(row.getResultByName("recipe"));
      const branchSlug = row.getResultByName("branchSlug");

      enrollment.branch = recipe.branches.find(b => b.slug === branchSlug);

      copyProperties(enrollment, recipe, [
        "userFacingName",
        "userFacingDescription",
        "featureIds",
        "isRollout",
        "localizations",
      ]);

      if (typeof recipe.isFirefoxLabsOptIn !== "undefined") {
        copyProperties(enrollment, recipe, [
          "isFirefoxLabsOptIn",
          "firefoxLabsTitle",
          "firefoxLabsDescription",
          "firefoxLabsDescriptionLinks",
          "firefoxLabsGroup",
          "requiresRestart",
        ]);
      }

      return [enrollment.slug, enrollment];
    }

    const conn = txn ?? (await lazy.ProfilesDatastoreService.getConnection());
    const rows = await conn.execute(
      `
      SELECT
        slug,
        branchSlug,
        active,
        unenrollReason,
        lastSeen,
        json(recipe) AS recipe,
        json(setPrefs) AS setPrefs,
        json(prefFlips) AS prefFlips,
        source
      FROM NimbusEnrollments
      WHERE
        profileId = :profileId;
      `,
      {
        profileId: lazy.ExperimentAPI.profileId,
      }
    );

    const enrollments = rows.map(processRow);

    lazy.log.debug(`Loaded ${enrollments.length} enrollments`);

    return Object.fromEntries(enrollments);
  }

  /**
   * Load third-party opt-in recipes from the database.
   *
   * Only opt-ins that are not from Remote Settings will be loaded (e.g., this
   * will load the opt-ins provided by nimbus-devtools).
   *
   * @returns {Promise<object[]>} The third-party opt-in recipes.
   */
  static async loadThirdPartyOptInRecipes() {
    // We only load third-party opt-ins (e.g., Nimbus Devtools, force
    // enrollment) because the RemoteSettingsExperimentLoader will update the
    // list with its opt-ins at enable time.
    function processRow(row) {
      const recipe = JSON.parse(row.getResultByName("recipe"));
      const source = row.getResultByName("source");

      return { recipe, source };
    }

    const conn = await lazy.ProfilesDatastoreService.getConnection();
    const rows = await conn.execute(
      `
      SELECT
        json(recipe) AS recipe,
        source
      FROM NimbusEnrollments
      WHERE
            profileId = :profileId
        AND recipe ->> "$.isFirefoxLabsOptIn"
        AND (
                 NOT (recipe ->> "$.isEnrollmentPaused")
              OR active
        )
        AND source != :source;
      `,
      {
        profileId: lazy.ExperimentAPI.profileId,
        source: lazy.NimbusTelemetry.EnrollmentSource.RS_LOADER,
      }
    );

    return rows.map(processRow);
  }

  /**
   * Load the last known lastModified timestamps of Nimbus collections from the
   * NimbusSyncTimestamps table.
   *
   * @param {OpenedConnection} txn An option connection, used when this is
   * called within a transaction.
   *
   * @returns {Promise<Map<string, number>>} The lastModified timestamps of the
   * Nimbus collections.
   */
  static async loadSyncTimestamps(txn) {
    const conn = txn ?? (await lazy.ProfilesDatastoreService.getConnection());
    const rows = await conn.execute(
      `
        SELECT
          collection,
          lastModified
        FROM NimbusSyncTimestamps
        WHERE
          profileId = :profileId;
      `,
      {
        profileId: lazy.ExperimentAPI.profileId,
      }
    );

    return new Map(
      rows.map(row => [
        row.getResultByName("collection"),
        row.getResultByName("lastModified"),
      ])
    );
  }

  /**
   *
   */
  static async loadEnrollmentStatuses(txn) {
    function processRow(row) {
      const slug = row.getResultByName("slug");
      const index = row.getResultByName("entryIndex");
      const timestamp = new Date(row.getResultByName("timestamp"));
      const status = JSON.parse(row.getResultByName("status"));

      return [
        slug,
        {
          index,
          timestamp,
          status,
        },
      ];
    }

    const conn = txn ?? (await lazy.ProfilesDatastoreService.getConnection());
    const rows = await conn.execute(
      `
        SELECT
          slug,
          entryIndex,
          timestamp,
          status
        FROM
          (
            SELECT
              slug,
              entryIndex,
              timestamp,
              json(status) AS status,
              RANK() OVER window AS entryRank
            FROM NimbusEnrollmentStatuses
            WHERE
              profileId = :profileId
            WINDOW window AS (PARTITION BY slug ORDER BY entryIndex DESC)
          )
        WHERE
          entryRank = 1;
      `,
      {
        profileId: lazy.ExperimentAPI.profileId,
      }
    );

    return new Map(rows.map(processRow));
  }

  /**
   * Load the slugs of all experiments from other profiles that have unenrolled.
   *
   * @returns {Promise<Set<string>>} The slugs of the experiments.
   */
  static async loadUnenrolledExperimentSlugsFromOtherProfiles() {
    const conn = await lazy.ProfilesDatastoreService.getConnection();
    const rows = await conn.execute(
      `
        SELECT DISTINCT
          slug
        FROM NimbusEnrollments
        WHERE
              NOT active
          AND NOT json_extract(recipe, "$.isRollout")
          AND profileId != :profileId;
      `,
      {
        profileId: lazy.ExperimentAPI.profileId,
      }
    );

    return new Set(rows.map(row => row.getResultByName("slug")));
  }

  /**
   * Return whether two enrollment statuses are equal.
   *
   * @param {EnrollmentStatus} prev
   * The most recent recorded enrollment status.
   *
   * @param {EnrollmentStatus} next
   * The new enrollment status.
   *
   * @returns {boolean}
   * Whether the enrollment statuses are equal
   */
  static enrollmentStatusEqual(prev, next) {
    return (
      prev.status === next.status &&
      prev.reason === next.reason &&
      prev.branch === next.branch &&
      prev.error === next.error &&
      ((prev.conflictSlugs === undefined && next.conflictSlugs === undefined) ||
        (prev.conflictSlugs.length === next.conflictSlugs.length &&
          prev.conflictSlugs.every(
            (slug, i) => slug === next.conflictSlugs[i]
          ))) &&
      prev.migration === next.migration
    );
  }

  /**
   * Return the next timestamp for an {@link EnrollmentStatusEntry}.
   *
   * N.B.: This function only exists to allow mocking in tests.
   */
  static now() {
    return new Date();
  }
}
