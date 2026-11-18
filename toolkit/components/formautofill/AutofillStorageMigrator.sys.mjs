/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Copies a profile's autofill records from one store to the other, and checks
 * field by field that they came out the same.
 *
 * The copy itself does not depend on which collection is being copied, so it
 * lives once here; `AddressStorageMigrator` and `CreditCardStorageMigrator` at
 * the bottom of this file supply what does -- the prefs that budget the run,
 * the Glean category it reports to, and how two records are compared.
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AutofillApiError:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs",
  creditCardFieldDiffers:
    "resource://autofill/RustAutofillCreditCardStorage.sys.mjs",
  isStoredAddressField:
    "resource://autofill/RustAutofillAddressStorage.sys.mjs",
  isStoredCreditCardField:
    "resource://autofill/RustAutofillCreditCardStorage.sys.mjs",
  RustAutofillAddressesAdapter:
    "resource://autofill/RustAutofillAddressStorage.sys.mjs",
  RustAutofillCreditCardsAdapter:
    "resource://autofill/RustAutofillCreditCardStorage.sys.mjs",
});

// Each attempt re-imports the whole profile, so a store that can never be
// written must not pay that on every startup. Budgets the real migration and
// the dry run separately.
export const MAX_ATTEMPTS = 15;

// The error_code of a copy into Rust whose source was written to while it ran.
const SOURCE_CHANGED = "SourceChanged";

// Twelve or more base64 characters including at least one digit: a guid. The
// digit is what keeps CamelCase error type names out of the match.
const GUID_LIKE = /\b(?=[A-Za-z0-9+/]*[0-9])[A-Za-z0-9+/]{12,}\b/g;

// A failure is reported as a code to group on and a message to read. The
// message is free text -- a store's own error can name the record it was
// writing -- so blank the guids out of it and cap it. An XPCOM exception can
// carry an empty message, and then its result name is what says why.
const errorCodeOf = e =>
  e instanceof lazy.AutofillApiError ? e.constructor.name : "Unknown";

// How a JSON store's export fails when OSKeyStore cannot decrypt a number: a
// key that does not open the ciphertext, or a ciphertext too short to hold one.
// Anything else -- a cancelled unlock, a shutdown -- says the key was not
// available, not that the number cannot be read.
const isUndecryptable = e =>
  e?.result == Cr.NS_ERROR_FAILURE || e?.result == Cr.NS_ERROR_INVALID_ARG;

const errorTextOf = e =>
  String(e?.message || e?.name || e)
    .replace(GUID_LIKE, "<id>")
    .slice(0, 200);

// Encoding a string for the Rust store replaces each lone surrogate with U+FFFD,
// so a value is compared in that form: the replacement is not a divergence.
const wellFormed = value =>
  typeof value == "string" ? value.toWellFormed() : value;

/**
 * Copy a profile's records from one store to the other, and check field by
 * field that they came out the same. A dry run does the copy only to measure it,
 * and leaves the caller to empty the target again.
 *
 * Runs at startup for a profile that has not migrated, and again each time the
 * pref that chooses the store is flipped -- in whichever direction that pref
 * has just gone, since the copy is written against a source and a target.
 *
 * A subclass supplies what differs by collection, as getters rather than
 * instance fields so they are readable from here:
 *
 *  - `config` (static), holding metrics, rustAdapter, collectionName,
 *    testVersionPref, testVersion, testAttemptsPref, attemptsPref and, for a
 *    collection that holds encrypted values,
 *    dropUndecryptablePref, dropUndecryptableAfterAttemptsPref and
 *    undecryptableAttemptsPref. Rebuilt
 *    on each read, so the Glean category and the lazy module getters inside it
 *    resolve at call time rather than at load.
 *  - `logger`.
 *  - the `isStoredField()` method, and `fieldDiffers()` if the collection has a
 *    field that does not compare as a string.
 */
export class AutofillStorageMigrator {
  #source = null;
  #target = null;
  #attempt = 0;

  /**
   * @param {object} source The store to copy from: getAll(), plus `_data` if it
   *   keeps its records in one.
   * @param {object} target The store to copy into: addManyWithMeta(),
   *   updateManyWithMeta(), removeMany(), getAll(). A copy that wipes also needs
   *   wipe(); a source that has tombstones to hand over needs
   *   addManyTombstones(); a store that caches its record count needs
   *   refreshCount().
   */
  constructor(source, target) {
    this.#source = source;
    this.#target = target;
  }

  /**
   * The subclass's config, for the instance methods that read it.
   */
  get config() {
    return this.constructor.config;
  }

  /**
   * Whether this profile still owes a dry run of the current generation.
   *
   * Static so a caller can ask before it builds the stores: reaching the
   * migrator means constructing the Rust adapter, which opens autofill.db, and
   * the dry run is followed by a wipe. A profile that has already measured must
   * pay neither, every launch, for a run that is not going to happen.
   *
   * @returns {boolean}
   */
  static get dryRunPending() {
    const { testVersionPref, testVersion } = this.config;
    return Services.prefs.getIntPref(testVersionPref, 0) < testVersion;
  }

  /**
   * Whether the copy into Rust has failed MAX_ATTEMPTS times and will not be
   * tried again. A copy back to JSON is not counted, and dry runs are counted
   * separately, on testAttemptsPref.
   *
   * @returns {boolean}
   */
  static get attemptsExhausted() {
    const { attemptsPref } = this.config;
    return Services.prefs.getIntPref(attemptsPref, 0) >= MAX_ATTEMPTS;
  }

  /**
   * Whether this copy goes into the Rust store rather than back to JSON. Read
   * off the target, so it cannot disagree with the copy it describes.
   *
   * @returns {boolean}
   */
  get #intoRust() {
    return this.#target instanceof this.config.rustAdapter;
  }

  /**
   * Whether a copy may leave out records whose values the source cannot
   * decrypt at all. Set through Nimbus; a collection without
   * dropUndecryptablePref never does.
   *
   * @returns {boolean}
   */
  get #dropUndecryptableEnabled() {
    const { dropUndecryptablePref } = this.config;
    return (
      !!dropUndecryptablePref &&
      Services.prefs.getBoolPref(dropUndecryptablePref, false)
    );
  }

  /**
   * Copy the records across, unless this is a copy into Rust and this profile
   * has spent its attempts. A copy back to JSON is neither refused nor counted.
   *
   * Divergence is measured and reported, not acted on: a complete copy is the
   * only bar. A source written to while a copy into Rust ran fails it too, as
   * the write is in the source and may not be in the target.
   *
   * While dropUndecryptablePref is set, a copy drops the records whose values
   * the source cannot decrypt, rather than failing on them, when another
   * record's value did decrypt: the key
   * store works, so those were encrypted under a key it no longer holds. Once
   * as many attempts have failed as dropUndecryptableAfterAttemptsPref says,
   * and the last of them failed to decrypt a record too, a copy into Rust
   * drops them even when none decrypted: a failure that has outlasted that
   * many launches is not expected to clear, and giving up would strand the
   * whole profile on JSON for the sake of records it cannot read either.
   *
   * @param {object} [options]
   * @param {boolean} [options.dryRun=false] Copy only to measure it: reports
   *   the run, empties the target again and returns false whatever the
   *   outcome. A failed run is retried on later launches, counted on
   *   testAttemptsPref rather than attemptsPref; once it succeeds or spends
   *   MAX_ATTEMPTS, testVersion is recorded so the same generation never
   *   measures again.
   * @param {boolean} [options.wipe=true] Empty the target first. See #migrate.
   * @returns {Promise<boolean>} Whether the copy completed. Always false for a
   *   dry run.
   */
  async maybeRun({ dryRun = false, wipe = true } = {}) {
    try {
      if (dryRun) {
        if (!this.constructor.dryRunPending) {
          return false;
        }
        const { testVersionPref, testVersion, testAttemptsPref } = this.config;
        this.#attempt = Services.prefs.getIntPref(testAttemptsPref, 0);
        const { ok } = await this.#migrateAndReport({ wipe, dryRun: true });
        // Measured, not read: put the target back the way it was found. Safe to
        // empty because nothing is serving from it -- the dry run only runs
        // while rust.enabled is off -- and the run is recorded on its own pref,
        // so a later real migration still does its own copy.
        await this.wipe();
        const next = this.#attempt + 1;
        if (ok || next >= MAX_ATTEMPTS) {
          Services.prefs.setIntPref(testVersionPref, testVersion);
          Services.prefs.clearUserPref(testAttemptsPref);
        } else {
          Services.prefs.setIntPref(testAttemptsPref, next);
        }
        return false;
      }

      const {
        attemptsPref,
        dropUndecryptableAfterAttemptsPref,
        undecryptableAttemptsPref,
      } = this.config;
      if (this.#intoRust) {
        this.#attempt = Services.prefs.getIntPref(attemptsPref, 0);
        if (this.constructor.attemptsExhausted) {
          this.logger.warn(
            `Not migrating: ${this.#attempt} attempts already failed.`
          );
          return false;
        }
      }

      let dropUndecryptable = false;
      let undecryptableAttempts = 0;
      if (this.#intoRust) {
        if (undecryptableAttemptsPref) {
          undecryptableAttempts = Services.prefs.getIntPref(
            undecryptableAttemptsPref,
            0
          );
        }
        dropUndecryptable =
          undecryptableAttempts > 0 &&
          !!dropUndecryptableAfterAttemptsPref &&
          this.#attempt >=
            Services.prefs.getIntPref(
              dropUndecryptableAfterAttemptsPref,
              MAX_ATTEMPTS
            );
      }
      const { ok, undecryptable } = await this.#migrateAndReport({
        wipe,
        dropUndecryptable,
      });

      if (this.#intoRust) {
        if (ok) {
          Services.prefs.clearUserPref(attemptsPref);
        } else {
          Services.prefs.setIntPref(attemptsPref, this.#attempt + 1);
        }
        if (undecryptableAttemptsPref) {
          if (!ok && undecryptable) {
            Services.prefs.setIntPref(
              undecryptableAttemptsPref,
              undecryptableAttempts + 1
            );
          } else {
            Services.prefs.clearUserPref(undecryptableAttemptsPref);
          }
        }
      }
      return ok;
    } catch (e) {
      this.logger.error("Could not run the migration", e);
      return false;
    }
  }

  /**
   * Empty the target, records and tombstones both, and refresh its count.
   */
  async wipe() {
    try {
      await this.#target.wipe();
      await this.#target.refreshCount?.();
    } catch (e) {
      this.logger.error("Could not wipe the store", e);
    }
  }

  /**
   * Run the copy, then report it, and keep the reporting from deciding what the
   * copy did.
   *
   * @param {object} [options]
   * @param {boolean} [options.wipe=true] Handed to #migrate.
   * @param {boolean} [options.dropUndecryptable=false] Handed to #migrate.
   * @param {boolean} [options.dryRun=false] Whether this is a dry run, which
   *   reports no drop_reason, so every one reported is a real migration that
   *   completed.
   * @returns {Promise<object>} What #migrate found. A copy can be complete
   *   (`ok`) and still not be faithful (`diverged`).
   */
  async #migrateAndReport({
    wipe = true,
    dropUndecryptable = false,
    dryRun = false,
  } = {}) {
    const startedAt = ChromeUtils.now();
    const result = await this.#migrate(this.#source, this.#target, {
      wipe,
      dropUndecryptable,
    });
    // Another record decrypting is the stronger evidence, so it is the one
    // reported when the attempt threshold was reached as well.
    if (result.ok && result.dropped && !dryRun) {
      result.dropReason = result.anyDecrypted
        ? "others_decrypted"
        : "attempt_threshold";
    }

    // Reporting cannot decide the outcome. A failure here -- Glean refusing an
    // event, say -- would otherwise leave the caller's bookkeeping undone: a
    // completed copy never activated, an attempt never counted, a dry run
    // measuring itself again on every launch.
    try {
      this._report(result, startedAt);
    } catch (e) {
      this.logger.error("Could not report the migration", e);
    }

    return result;
  }

  /**
   * Record what the copy did: one migrate_to_rust event, and one
   * migrate_record_divergence per record that came out different, carrying the
   * same run id so the two join.
   *
   * Underscore-named so a test can replace it. Glean's own metrics cannot be
   * stubbed -- a category is read-only and each metric lookup returns a fresh
   * object -- so this is the only seam a test has for a reporting failure.
   *
   * @param {object} result What #migrate found.
   * @param {number} startedAt A ChromeUtils.now() reading from when the copy
   *   began, for duration_ms.
   */
  _report(result, startedAt) {
    // Read at call time, not held: a Glean category is read-only and each
    // metric lookup returns a fresh object.
    const { metrics } = this.config;
    const direction = this.#intoRust ? "to_rust" : "to_json";
    // Joins the run to the divergences it found.
    const runId = Services.uuid.generateUUID().toString();

    for (const divergence of result.divergences) {
      // Only the kinds that have anything, so the rest stay absent rather than
      // empty.
      const fields = {};
      for (const kind of ["changed", "dropped", "added"]) {
        if (divergence[kind].length) {
          fields[kind] = divergence[kind].join(",");
        }
      }

      metrics.migrateRecordDivergence.record({
        run_id: runId,
        ...fields,
      });
    }

    // The counts say what failed; the message says why, when there is anything
    // to add.
    const errorMessage = result.threw ?? result.firstCause;

    metrics.migrateToRust.record({
      run_id: runId,
      direction,
      attempt: this.#attempt,
      result: result.ok ? "ok" : "error",
      source_total: result.sourceTotal,
      target_total: result.targetTotal,
      migrated: result.migrated,
      failed: result.failed,
      ...(result.dropReason ? { drop_reason: result.dropReason } : {}),
      failed_deletions: result.failedDeletions,
      diverged: result.diverged,
      duration_ms: Math.round(ChromeUtils.now() - startedAt),
      ...(result.errorCode ? { error_code: result.errorCode } : {}),
      ...(errorMessage ? { error_message: errorMessage } : {}),
    });
  }

  /**
   * Bulk-copy one store's records into another, preserving guids, timestamps
   * and sync metadata, and then read them back and compare them field by field.
   *
   * The source is authoritative either way, its deletions included: a record it
   * holds ends up in the target holding the source's values, and a record it has
   * deleted ends up deleted there too.
   *
   * What differs is how the target gets there. Wiping first makes a retry
   * idempotent and clears the tombstones that would otherwise stop a guid being
   * re-inserted, at the cost of emptying the target before anything is written
   * to it. Without a wipe the target is brought to the same place a record at a
   * time -- inserted, overwritten or deleted according to what it already holds
   * -- so a copy that goes wrong leaves it no emptier than it found it.
   *
   * Everything it finds is returned rather than reported.
   *
   * @param {object} source The store being copied from.
   * @param {object} target The store being copied into.
   * @param {object} [options]
   * @param {boolean} [options.wipe=true] Empty the target before copying.
   * @param {boolean} [options.dropUndecryptable=false] Leave out a record whose
   *   value the source cannot decrypt (see isUndecryptable), as if the source
   *   did not hold it, instead of failing the copy on it, even when no other
   *   record's value decrypted. Any other export failure still fails the
   *   copy.
   * @returns {Promise<object>} What the copy did: the counts either side of it,
   *   whether every record arrived, the first reason one was rejected, the
   *   records that came back different, and the error where it threw or the
   *   source was written to while it ran.
   */
  async #migrate(
    source,
    target,
    { wipe = true, dropUndecryptable = false } = {}
  ) {
    // The source keeps serving while the copy runs. Only a copy into Rust is
    // checked for writes to it: the JSON store announces every write but the
    // sync bookkeeping as it makes it, and the Rust adapter's bulk writes are
    // silent, so whatever is heard here came from the source. A copy back is
    // not: the Rust adapter announces a write only after refreshing its count,
    // and the JSON store's removeMany announces the copy's own deletions.
    // "migrate" announces a completed switch, not a write.
    const { collectionName, rustAdapter } = this.config;
    const watchSource = target instanceof rustAdapter;
    let sourceChanged = false;
    const onChange = (subject, _topic, data) => {
      if (
        data != "migrate" &&
        subject.wrappedJSObject?.collectionName == collectionName
      ) {
        sourceChanged = true;
      }
    };
    if (watchSource) {
      Services.obs.addObserver(onChange, "formautofill-storage-changed");
    }

    let sourceTotal = 0;
    let targetTotal = 0;
    let migrated = 0;
    let failed = 0;
    let undecryptable = 0;
    let dropped = 0;
    let anyDecrypted = false;
    let failedDeletions = 0;
    let ok = false;
    let errorCode = null;
    let threw = null;
    let divergences = [];
    // Rejected records in one batch nearly always share a cause, so the first
    // is as much as the report needs.
    let firstCause = null;

    try {
      // `_data` for a store that keeps one: it is the only read that carries
      // the hidden `_sync` metadata the copy has to take across, and the only
      // one that includes the tombstones, both of which getAll() drops. A store
      // without one answers a raw read instead and has no tombstones to give --
      // raw because a store may hide a stored field on read, and copying from
      // the filtered read would write that field out of existence here.
      const raw = source._data ?? (await source.getAll({ rawData: true }));
      let records = raw.filter(record => !record.deleted);
      const tombstones = raw
        .filter(record => record.deleted)
        .map(record => ({
          guid: record.guid,
          timeDeleted: record.timeLastModified ?? 0,
        }));
      sourceTotal = records.length;

      // Ask the source to hand each record over in the form the target can take
      // it. A store that holds a value only it can read -- an encrypted card
      // number -- returns it in the clear here, so the target can store it under
      // its own scheme rather than inheriting one it cannot read. A store with
      // nothing to convert has no _recordForMigrationExport, or the trivial one
      // that returns the record, and this is a copy either way.
      //
      // A record that cannot be exported is left out: it is better to fail the
      // run than to copy the record without the value. Where the caller allows
      // a record that will not decrypt to be dropped instead, it is treated
      // from here on as a record the source does not hold.
      const exportable = [];
      const undecryptableRecords = [];
      for (const record of records) {
        try {
          exportable.push(
            (await source._recordForMigrationExport?.(record)) ?? record
          );
          // Only a credit card holds an encrypted value.
          anyDecrypted ||= !!record["cc-number-encrypted"];
        } catch (e) {
          firstCause ??= errorTextOf(e);
          this.logger.error(
            `Could not export a record for migration: ${errorTextOf(e)}`
          );
          if (isUndecryptable(e)) {
            undecryptableRecords.push(record);
          } else {
            failed++;
          }
        }
      }

      undecryptable = undecryptableRecords.length;
      if (
        undecryptable &&
        this.#dropUndecryptableEnabled &&
        (dropUndecryptable || anyDecrypted)
      ) {
        dropped = undecryptable;
        records = records.filter(
          record => !undecryptableRecords.includes(record)
        );
        this.logger.warn(
          `Dropping ${dropped} record(s) that cannot be decrypted.`
        );
      } else {
        failed += undecryptable;
      }

      if (wipe) {
        // Silent, like the bulk writes below: announcing this as a removeAll
        // would report the profile's records as cleared mid-startup.
        await target.wipe();
      }

      // What the target already holds decides how each record gets there. A
      // wipe leaves nothing, so everything is an insert.
      const heldRecords = new Map(
        wipe ? [] : (await target.getAll()).map(record => [record.guid, record])
      );
      const held = new Set(heldRecords.keys());
      const fresh = exportable.filter(record => !held.has(record.guid));

      // A record the target holds under the same timestamp is the same record,
      // so it is left alone rather than written again. That keeps a switch off
      // the disk for the records nobody touched, and keeps whatever the target
      // holds that the source cannot represent -- a field invented by a newer
      // client survives a trip through the store that has no column for it.
      //
      // A missing timestamp on either side counts as changed: two records that
      // cannot say when they were written are not known to be the same one.
      const overwrite = exportable.filter(record => {
        if (!held.has(record.guid)) {
          return false;
        }
        const there = heldRecords.get(record.guid);
        return (
          !record.timeLastModified ||
          !there.timeLastModified ||
          record.timeLastModified !== there.timeLastModified
        );
      });

      const results = [
        ...(fresh.length ? await target.addManyWithMeta(fresh) : []),
        ...(overwrite.length ? await target.updateManyWithMeta(overwrite) : []),
      ];

      // An exported record carries in the clear what the source holds
      // encrypted, so let go of them here rather than at the end of the copy:
      // the verify and the deletions below run for as long again, and none of
      // it needs them. `records` holds the originals, still encrypted.
      exportable.length = 0;
      fresh.length = 0;
      overwrite.length = 0;

      for (const result of results) {
        if (result.error) {
          failed++;
          firstCause ??= errorTextOf(result.error);
          this.logger.error(`Migration failed for a record: ${result.error}`);
        } else {
          migrated++;
        }
      }

      // The source is authoritative, so anything the target holds that the
      // source does not is stale -- a record the source has deleted, or one an
      // earlier copy left behind -- and is deleted rather than tombstoned. A
      // tombstone for a guid the target still holds is rejected anyway, and
      // deleting leaves one behind wherever the store keeps them, so the
      // deletion still reaches the server.
      const sourceGuids = new Set(records.map(record => record.guid));
      const stale = [...held].filter(guid => !sourceGuids.has(guid));

      // The rest of the source's deletions are imported as tombstones, so the
      // target does not upload records the source has already deleted. A source
      // with no tombstones to give has already had its deletions applied above,
      // as records the target holds and it does not.
      const unheld = tombstones.filter(t => !held.has(t.guid));

      // Both kinds count together. What tells them apart is the verdict: a
      // record that would not delete is still there, so the run fails on it,
      // while a tombstone that would not import leaves the target correct and
      // costs a deletion coming back at the next sync.
      const deletionResults = [
        ...(stale.length ? await target.removeMany(stale) : []),
        ...(unheld.length ? await target.addManyTombstones(unheld) : []),
      ];
      for (const result of deletionResults) {
        if (result.error) {
          failedDeletions++;
          firstCause ??= errorTextOf(result.error);
          this.logger.error(`Could not apply a deletion: ${result.error}`);
        }
      }

      // Only a store that caches its count has anything to prime.
      await target.refreshCount?.();
      const stored = await target.getAll();
      targetTotal = stored.length;
      divergences = await this.#verify(source, target, stored);

      // Both ways round, and by guid rather than by count: a target holding the
      // right number of the wrong records has not copied everything, and one
      // still holding what the source deleted has copied too much. `extra`
      // catches a deletion that failed silently as well as one that reported
      // it.
      const storedGuids = new Set(stored.map(record => record.guid));
      const missing = records.filter(
        record => !storedGuids.has(record.guid)
      ).length;
      const extra = stored.filter(
        record => !sourceGuids.has(record.guid)
      ).length;
      // Nothing awaits from here on, so no write can land after this check.
      ok = failed === 0 && missing === 0 && extra === 0 && !sourceChanged;
      if (sourceChanged) {
        errorCode = SOURCE_CHANGED;
        this.logger.warn(
          "The source was written to while it was being copied."
        );
      }

      this.logger.log(
        `Migrated ${migrated}/${sourceTotal} records ` +
          `(${failed} failed, ${dropped} dropped, ` +
          `${failedDeletions} deletions failed, ` +
          `${missing} missing, ${extra} extra, count=${targetTotal}, ` +
          `diverged=${divergences.length}, verified=${ok}).`
      );
    } catch (e) {
      ok = false;
      errorCode = errorCodeOf(e);
      threw = errorTextOf(e);
      this.logger.error("Migration failed", e);
    } finally {
      if (watchSource) {
        Services.obs.removeObserver(onChange, "formautofill-storage-changed");
      }
    }

    return {
      ok,
      sourceTotal,
      targetTotal,
      migrated,
      failed,
      undecryptable,
      dropped,
      anyDecrypted,
      failedDeletions,
      diverged: divergences.length,
      divergences,
      firstCause,
      errorCode,
      threw,
    };
  }

  /**
   * Whether a field disagrees between a record and its copy. A string
   * comparison unless a collection holds a field that cannot answer that --
   * see CreditCardStorageMigrator.
   *
   * @param {string} _field
   * @param {*} a The value on the record.
   * @param {*} b The value on its copy.
   * @returns {boolean}
   */
  fieldDiffers(_field, a, b) {
    return a !== b;
  }

  /**
   * Check what the target holds against what the source holds, field by field,
   * and say which way each difference went.
   *
   * Both sides are read through getAll() rather than the raw rows the copy was
   * made from: only a difference a caller can see counts, and the two raw
   * shapes differ by design.
   *
   * Only the fields the copy carries are compared, which is the same set either
   * way round: the canonical fields and the metadata. One store derives the
   * presentational fields on read while the other persists what it computed, so
   * those differ whenever the derivation has changed -- most often with the app
   * locale -- and a field neither has a column for was never carried at all.
   *
   * cc-number is handed out as a mask, which each store builds its own way, so
   * it is compared by the number each store decrypts for the record instead.
   *
   * @param {object} source The store copied from.
   * @param {object} target The store copied into.
   * @param {Array<object>} stored What the target holds after the copy.
   * @returns {Promise<Array<object>>} One entry per record that disagrees: the
   *   fields whose values differ, the ones the copy lost, and the ones it
   *   gained.
   */
  async #verify(source, target, stored) {
    const byGUID = new Map(stored.map(record => [record.guid, record]));
    const divergences = [];

    for (let record of await source.getAll()) {
      let copy = byGUID.get(record.guid);
      if (!copy) {
        // A record that never arrived is a count mismatch, not a divergence.
        continue;
      }
      // Only a credit card store holds cc-number, so this is what keeps the
      // other collections from being asked to decrypt one.
      if (this.isStoredField("cc-number")) {
        // Shallow copies, so the cleartext does not outlive this iteration. A
        // source that cannot read its own number has nothing to check the
        // copy's against, so neither side is compared.
        const sent = await this.#numberOf(source, record);
        let kept;
        if (sent !== undefined) {
          kept = await this.#numberOf(target, copy);
        }
        record = { ...record, "cc-number": sent };
        copy = { ...copy, "cc-number": kept };
      }
      // The union of both key sets, so a field the copy dropped and one it
      // invented are both caught, and only the fields the target is answerable
      // for: one it derives on read or has no column for cannot have been lost
      // by the copy.
      const differing = [
        ...new Set([...Object.keys(record), ...Object.keys(copy)]),
      ].filter(
        field =>
          this.isStoredField(field) &&
          this.fieldDiffers(
            field,
            wellFormed(record[field]),
            wellFormed(copy[field])
          )
      );
      if (!differing.length) {
        continue;
      }

      // Which way each field went: a name in `dropped` with its components in
      // `added` is a record the target decomposed rather than one it damaged.
      const divergence = { changed: [], dropped: [], added: [] };
      for (const field of differing) {
        if (!record[field]) {
          divergence.added.push(field);
        } else if (!copy[field]) {
          divergence.dropped.push(field);
        } else {
          divergence.changed.push(field);
        }
      }
      divergences.push(divergence);

      this.logger.warn(
        `Migrated record differs from its source on: ` +
          `${differing.join(", ")}`
      );
    }
    return divergences;
  }

  /**
   * The cleartext card number a store holds for one of its records.
   *
   * @param {object} store
   * @param {object} record A record the store handed out.
   * @returns {Promise<?string|undefined>} The number, null if the record has
   *   none, or undefined if it cannot be read.
   */
  async #numberOf(store, record) {
    try {
      return (
        (await store.decryptField(record, "cc-number", {
          trigger: "formautofill_migration",
        })) || null
      );
    } catch (e) {
      this.logger.error("Could not read back a card number", e);
      return undefined;
    }
  }
}

const addressLogger = console.createInstance({
  logModule: "AddressStorageMigrator",
  maxLogLevelPref: "extensions.formautofill.loglevel",
});

const creditCardLogger = console.createInstance({
  logModule: "CreditCardStorageMigrator",
  maxLogLevelPref: "extensions.formautofill.loglevel",
});

export class AddressStorageMigrator extends AutofillStorageMigrator {
  get logger() {
    return addressLogger;
  }

  static get config() {
    return {
      metrics: Glean.formautofillAddresses,
      rustAdapter: lazy.RustAutofillAddressesAdapter,
      collectionName: "addresses",
      // Which generation of the dry run a profile has done, so a build that
      // fixes a migration bug can bump testVersion and measure the same
      // profiles again. Kept apart from rust.active: a dry run wipes the store
      // when it is done, so counting it as a migration would let a later
      // rust.enabled=true skip the copy and serve an empty store.
      testVersionPref:
        "extensions.formautofill.addresses.storage.rust.migrationTestVersion",
      testVersion: 1,
      // How many launches have already tried and failed the dry run of the
      // current generation.
      testAttemptsPref:
        "extensions.formautofill.addresses.storage.rust.migrationTestAttempts",
      // How many launches have already tried and failed.
      attemptsPref:
        "extensions.formautofill.addresses.storage.rust.migrationAttempts",
    };
  }

  isStoredField(field) {
    return lazy.isStoredAddressField(field);
  }

  // Every address field a store keeps compares as a string, so fieldDiffers is
  // left as the base has it.
}

export class CreditCardStorageMigrator extends AutofillStorageMigrator {
  get logger() {
    return creditCardLogger;
  }

  static get config() {
    return {
      metrics: Glean.formautofillCreditcards,
      rustAdapter: lazy.RustAutofillCreditCardsAdapter,
      collectionName: "creditCards",
      testVersionPref:
        "extensions.formautofill.creditCards.storage.rust.migrationTestVersion",
      testVersion: 1,
      testAttemptsPref:
        "extensions.formautofill.creditCards.storage.rust.migrationTestAttempts",
      attemptsPref:
        "extensions.formautofill.creditCards.storage.rust.migrationAttempts",
      dropUndecryptablePref:
        "extensions.formautofill.creditCards.storage.rust.dropUndecryptable",
      dropUndecryptableAfterAttemptsPref:
        "extensions.formautofill.creditCards.storage.rust.dropUndecryptableAfterAttempts",
      undecryptableAttemptsPref:
        "extensions.formautofill.creditCards.storage.rust.migrationUndecryptableAttempts",
    };
  }

  isStoredField(field) {
    return lazy.isStoredCreditCardField(field);
  }

  // An absent expiry is 0 on one side and missing on the other.
  fieldDiffers(field, a, b) {
    return lazy.creditCardFieldDiffers(field, a, b);
  }
}
