/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Shared half of the Rust-backed autofill storage adapters.
 *
 * Each collection in the vendored application-services autofill `Store` is
 * presented to desktop through the subset of the `AutofillRecords` interface
 * its consumers use. Everything in that presentation which does not depend on
 * the collection lives here; a subclass supplies only what the Rust schema
 * makes specific to its own records.
 *
 * A subclass must provide, as getters rather than instance fields -- the
 * constructor below reads `_dataType`, and an instance field is not initialised
 * until after it returns:
 *
 *  - `_dataType` and `_logger`.
 *  - `_recordApi`, the shared record module that normalises and computes fields.
 *  - `_validFields`, the canonical field list.
 *
 * and as methods:
 *
 *  - `_recordFromRust()`, `_normalize()`, and the store calls listed under
 *    "Store operations". `_recordFromRust()` is handed getAll()'s options, so
 *    a collection that hides a stored field on read reports it under
 *    `rawData`; one that hides nothing ignores them.
 *
 * `static _instance = null` is worth declaring for the reader, though
 * `getInstance()` stores per subclass either way.
 *
 * A subclass may override the three merge hooks -- `_prepareStoredForMerge()`,
 * `_validateRecord()` and `_recomputeBeforeWrite()` -- which do nothing here,
 * and `_fieldsKeptWhenOmitted`, which is empty here.
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AutofillDataTypes: "resource://gre/modules/shared/AutofillDataTypes.sys.mjs",
  AutofillTelemetry: "resource://gre/modules/shared/AutofillTelemetry.sys.mjs",
  NoSuchRecord:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs",
  RustAutofillStore: "resource://autofill/RustAutofillStore.sys.mjs",
});

export const INTERNAL_FIELDS = new Set([
  "guid",
  "version",
  "timeCreated",
  "timeLastUsed",
  "timeLastModified",
  "timesUsed",
]);

export class RustAutofillAdapterBase {
  /**
   * The profile's adapter, built without waiting for the store to open.
   *
   * There is one autofill.db per profile, so there is one adapter per
   * collection; callers retrieve it rather than being handed one. `_instance` is
   * underscore-named so a test can reset it. The assignment below lands on the
   * subclass `getInstance()` was called on, so collections do not share one.
   */
  static getInstance() {
    return (this._instance ??= new this(
      new lazy.RustAutofillStore().ensureOpen()
    ));
  }

  // Resolves to the Application Services autofill store. Held unresolved: the
  // store opens asynchronously but the getters that hand out a collection are
  // synchronous, so awaiting per operation is what lets a caller be handed the
  // collection before the database is ready.
  //
  // It also decides what a database that will not open looks like. Every
  // operation rejects, rather than the adapter being absent and the JSON store
  // quietly taking over -- that snapshot froze when Rust took over, and letting
  // the user edit it would strand those edits the next time the store opens.
  // Its lifecycle and shutdown blocker are owned by RustAutofillStore.
  //
  // Addresses, credit cards and passports are collections inside one store, so
  // every adapter reads the same autofill.db.
  #storePromise;
  #collectionName;
  // How many records the collection holds, so isEmpty() can answer without
  // awaiting. Null until primed. See isEmpty().
  #count = null;

  /**
   * @param {Promise<Store>} storePromise The Application Services autofill
   *   store, still opening. RustAutofillStore owns the single per-profile
   *   autofill.db connection; a test may pass its own.
   */
  constructor(storePromise) {
    this.#storePromise = storePromise;
    const dataType = this._dataType;
    if (dataType === undefined) {
      // Either this class is abstract, or the subclass declared _dataType as an
      // instance field, which is still uninitialised here.
      throw new Error(
        `${this.constructor.name} needs a _dataType getter on its prototype`
      );
    }
    this.#collectionName = lazy.AutofillDataTypes.get(dataType).collectionName;
  }

  /**
   * The store, once open. For subclasses; the base reaches it the same way.
   *
   * @returns {Promise<Store>}
   */
  _store() {
    return this.#storePromise;
  }

  /**
   * Whether NSS is withholding the store's key; see
   * RustAutofillStore.primaryPasswordLocked.
   *
   * Underscore-named because a test store's key is its own and never NSS's, so
   * a test that has to answer for the token replaces this.
   *
   * @returns {boolean}
   */
  _primaryPasswordLocked() {
    return lazy.RustAutofillStore.primaryPasswordLocked;
  }

  /**
   * Run `operation` with the primary password prompt allowed; see
   * RustAutofillStore.allowingPrompt. Underscore-named for the same reason as
   * _primaryPasswordLocked.
   *
   * @param {Function} operation An async function.
   * @returns {Promise<*>} What `operation` resolves to.
   */
  _allowingPrompt(operation) {
    return new lazy.RustAutofillStore().allowingPrompt(operation);
  }

  /**
   * Fire storage-changed and refresh the saved-profile count.
   *
   * AutofillRecords.observe() handles the same topic and records the count from
   * the JSON collection, which holds nothing while this store is the active one.
   * Reading the count before notifying and recording it synchronously afterwards
   * leaves this value as the one that stands.
   *
   * @param {string} guid
   * @param {boolean} sourceSync
   * @param {string} action
   * @param {object} [options]
   * @param {boolean} [options.countChanged=true] Whether the operation can have
   *   changed how many records the collection holds. update() and notifyUsed()
   *   cannot, so they answer from the cached count rather than paying a
   *   round-trip for a number that has not moved. The count is still recorded
   *   either way: the JSON collection observes these events too and records its
   *   own zero, and this is what has to land after it.
   */
  async #notifyAndRecordCount(
    guid,
    sourceSync,
    action,
    { countChanged = true } = {}
  ) {
    if (countChanged || this.#count === null) {
      await this.refreshCount();
    }
    this.#notify(guid, sourceSync, action);
    lazy.AutofillTelemetry.recordAutofillProfileCount(
      this._dataType,
      this.#count
    );
  }

  /**
   * Announce that sync changed the records underneath this adapter.
   *
   * A bridged engine reconciles inside Rust, so none of the writes on this
   * adapter ran and nothing fired formautofill-storage-changed. The JSON store
   * notifies for every record sync applies, and FormAutofillStatus listens for
   * that to refresh FormAutofill:savedFieldNames -- without it, a profile that
   * receives its records from sync is offered no autofill until the next
   * restart.
   *
   * One announcement per sync rather than one per record: the bridge does not
   * report which guids it touched. "update" for the same reason -- it is the
   * action that claims least, and BackupService only regenerates on "remove",
   * which this must not do speculatively.
   *
   * sourceSync so the engine's tracker leaves the score alone: records that
   * arrived from the server are not a reason to schedule another sync.
   */
  async notifySyncApplied() {
    await this.#notifyAndRecordCount(null, true, "update", {
      countChanged: true,
    });
  }

  #notify(guid, sourceSync, action) {
    Services.obs.notifyObservers(
      {
        wrappedJSObject: {
          sourceSync,
          guid,
          collectionName: this.#collectionName,
        },
      },
      "formautofill-storage-changed",
      action
    );
  }

  /**
   * Get the collection ready to be read from, the same contract the JSON
   * collection answers: FormAutofillStorageBase.initialize() calls this on
   * whichever store the getter hands back.
   *
   * Priming the count is all it takes -- isEmpty() is synchronous and answers
   * from it.
   *
   * Does not throw. It runs inside the storage's memoized initialize(), which
   * FormAutofillParent starts and does not catch, so a rejection would be handed
   * to every later caller -- every collection, not just this one -- for the life
   * of the process. A store that will not open reports itself on the first read
   * instead.
   */
  async initialize() {
    try {
      await this.refreshCount();
    } catch (e) {
      this._logger.error(
        `Could not read the Rust ${this.#collectionName} store`,
        e
      );
    }
  }

  /**
   * Read the record count from the store and remember it, so that the
   * synchronous isEmpty() has an answer.
   *
   * Called on initialize() and on every write that notifies, and by the caller
   * of a bulk write -- wipe() and addMany*() -- which do not notify.
   */
  async refreshCount() {
    this.#count = await this._countAll(await this._store());
    return this.#count;
  }

  /**
   * Synchronous, matching the JSON collection: callers render from it without
   * awaiting -- FormAutofillPrompter.renderDescription() is called from a
   * synchronous render() -- and a promise there would read as "always
   * non-empty". Answers from the count refreshCount() and every notified write
   * maintain, and reports empty until something primes it.
   *
   * @returns {boolean}
   */
  isEmpty() {
    return (this.#count ?? 0) === 0;
  }

  // ---- CRUD / UI --------------------------------------------------------

  /**
   * Add a new record and return the guid the store assigns it.
   *
   * Replicating a record that already has an identity is addManyWithMeta's job.
   */
  async add(record, { sourceSync = false, action = "add" } = {}) {
    const normalized = this._normalize(structuredClone(record));
    // Before the write and not after a merge, unlike update(): there is only
    // one side here, and _normalize has already derived whatever it can.
    this._validateRecord(normalized);
    const added = await this._add(await this._store(), normalized);
    await this.#notifyAndRecordCount(added.guid, sourceSync, action);
    return added.guid;
  }

  /**
   * Apply `record`'s fields to the stored record `guid`. The store keeps the
   * guid and the metadata, refreshes timeLastModified, and counts the edit as a
   * change pending upload.
   *
   * With `preserveOldProperties`, a field the caller omits keeps its stored
   * value; without it, an omitted or empty field is cleared. Mirrors
   * AutofillRecords.update(). A field in `_fieldsKeptWhenOmitted` keeps its
   * stored value when omitted either way, and is cleared only by an empty
   * value.
   */
  async update(
    guid,
    record,
    preserveOldProperties = false,
    { sourceSync = false, action = "update" } = {}
  ) {
    const stored = await this.get(guid, { rawData: true });
    if (!stored) {
      // The same error the store would raise for the write below, so a missing
      // record groups under one code however it was detected.
      throw new lazy.NoSuchRecord(guid);
    }
    // The merge below takes canonical fields only, so the derived ones are
    // dropped by construction. This is for a collection where a derived field
    // stands in for a canonical one and has to be restored first.
    await this._prepareStoredForMerge(stored);

    const merged = { guid, version: stored.version };
    const incoming = this._normalize(structuredClone(record), true);

    let hasValidField = false;
    for (const field of this._validFields) {
      let value = incoming[field];
      if (
        value === undefined &&
        (preserveOldProperties || this._fieldsKeptWhenOmitted.includes(field))
      ) {
        value = stored[field];
      }
      if (value !== undefined && value !== "") {
        hasValidField = true;
        merged[field] = value;
      }
    }
    if (!hasValidField) {
      throw new Error("Record contains no valid field.");
    }

    // On the merged record rather than the caller's, because a field the caller
    // did not restate can arrive from the stored side.
    this._validateRecord(merged);
    this._recomputeBeforeWrite(merged);

    // The content columns only. The plain update leaves the metadata alone,
    // refreshes time_last_modified and increments the change counter, which is
    // what an edit means; the with-meta call would replace that counter with a
    // value this side cannot read.
    await this._update(await this._store(), guid, merged, stored);
    await this.#notifyAndRecordCount(guid, sourceSync, action, {
      countChanged: false,
    });
  }

  /**
   * @returns {Promise<boolean>} Whether a record was deleted. False means the
   *   guid was not present.
   */
  async remove(guid, { sourceSync = false, action = "remove" } = {}) {
    const removed = guid && (await this._delete(await this._store(), guid));
    // Nothing changed, so nothing to announce. The JSON collection returns
    // without notifying for a guid it does not hold, and an event here would
    // put every observer -- including the saved-field-names recompute -- to
    // work over a store that has not moved.
    if (!removed) {
      this._logger.warn("attempting to remove non-existing entry", guid);
      return false;
    }
    await this.#notifyAndRecordCount(guid, sourceSync, action);
    return removed;
  }

  /**
   * Remove every record, one at a time, leaving a tombstone for each so the
   * deletions reach the server. This is the user asking for their records to be
   * deleted; wipe() is the one that discards that bookkeeping.
   */
  async removeAll({ sourceSync = false, action = "removeAll" } = {}) {
    const store = await this._store();
    for (const record of await this._getAll(store)) {
      await this._delete(store, record.guid);
    }
    await this.#notifyAndRecordCount(null, sourceSync, action);
  }

  /**
   * @param {string} guid
   * @param {object} [options]
   * @param {boolean} [options.rawData=false] As for getAll().
   * @returns {Promise<?object>}
   */
  async get(guid, { rawData = false } = {}) {
    // The UniFFI binding throws for a missing guid.
    if (!guid) {
      return null;
    }
    let found;
    try {
      found = await this._get(await this._store(), guid);
    } catch (e) {
      // Only a missing record is an answer; anything else is a failure to read,
      // and must not be reported as one. A locked or corrupt database returned
      // as "no such record" opens the edit dialog blank, as though the caller
      // were creating a new record, and saving that writes a second record on
      // top of the one that could not be read. It also has update() raise
      // NoSuchRecord for what was really a SQL error, so the migration
      // attributes the failure to the wrong error_code.
      if (e instanceof lazy.NoSuchRecord) {
        return null;
      }
      throw e;
    }
    return found && this._recordFromRust(found, { rawData });
  }

  /**
   * @param {object} [options]
   * @param {boolean} [options.rawData=false] Keep the fields the collection
   *   hides from a consumer on read, so that every stored field is reported.
   *   What a copy out of this store is built from: a filtered read would write
   *   the hidden fields out of existence on the other side. A collection that
   *   hides nothing reads the same either way.
   *
   *   Narrower than the JSON collection's option of the same name, which also
   *   strips the computed fields. The records here carry them either way.
   * @returns {Promise<Array<object>>}
   */
  async getAll({ rawData = false } = {}) {
    const records = await this._getAll(await this._store());
    return records.map(record => this._recordFromRust(record, { rawData }));
  }

  async notifyUsed(guid, { sourceSync = false, action = "notifyUsed" } = {}) {
    // The touch is a bare UPDATE ... WHERE guid, so it reports nothing back for
    // a guid the store does not hold and cannot be used to detect one. Read
    // first, matching the JSON collection, which returns without notifying when
    // the record has been deleted since it was filled.
    if (!(await this.get(guid))) {
      this._logger.debug("Cannot notify. No record found with guid:", guid);
      return;
    }
    // The touch bumps the change counter as well as the use count, where the
    // JSON collection leaves it alone: filling a form queues the record for
    // upload at the next sync. Nothing here can separate the two -- they are one
    // statement in the store.
    await this._touch(await this._store(), guid);
    await this.#notifyAndRecordCount(guid, sourceSync, action, {
      countChanged: false,
    });
  }

  async getSavedFieldNames() {
    // Every non-empty field that is not internal metadata, computed fields
    // included: autofill decides which form fields it can offer from this set.
    // Read raw, so a field a read hides is still reported: the set is only
    // recomputed when the store changes, not when what a read hides does, and
    // whether to offer a field is decided where it would be filled.
    const fieldNames = new Set();
    for (const record of await this.getAll({ rawData: true, prompt: false })) {
      for (const [key, value] of Object.entries(record)) {
        if (value && !INTERNAL_FIELDS.has(key)) {
          fieldNames.add(key);
        }
      }
    }
    return fieldNames;
  }

  /**
   * Bring a record to its canonical form and reject one that cannot be stored.
   * Mirrors AutofillRecords._normalizeRecord: the canonical fields are
   * normalised, unknown fields are left alone to round-trip, and metadata may
   * not be set through this path.
   *
   * @param {object} record The record, normalised in place.
   * @param {boolean} [preserveEmptyFields=false] Keep a field whose value is the
   *   empty string instead of dropping it. update() passes true, because there
   *   an empty value means "clear this field" and has to survive as far as the
   *   merge; on a new record the same empty value only means the field was never
   *   supplied, and storing it would be noise.
   * @returns {object} The same record.
   */
  _normalizeRecord(record, preserveEmptyFields = false) {
    return this._normalize(record, preserveEmptyFields);
  }

  /**
   * Fill in the fields derived from the canonical ones, in place.
   *
   * Callers that build a record from a form rather than reading one from here
   * -- FormAutofillParent, deciding whether a submitted record is worth a
   * doorhanger -- normalise and compute it themselves before comparing it
   * against what is stored, so both entry points have to exist on whichever
   * store is active.
   *
   * @param {object} record
   */
  async computeFields(record) {
    if (!record.deleted) {
      this._recordApi.computeFields(record);
    }
  }

  /**
   * The shared half of a subclass's `_normalize`: reject an unknown internal
   * field, reject a value of the wrong type, and drop the empty ones.
   *
   * The caller runs `normalizeFields` first and decides what counts as an empty
   * record afterwards.
   *
   * @param {object} record The record, modified in place.
   * @param {boolean} preserveEmptyFields
   */
  _normalizeCanonicalFields(record, preserveEmptyFields) {
    for (const key of Object.keys(record)) {
      if (!this._validFields.includes(key)) {
        if (INTERNAL_FIELDS.has(key)) {
          throw new Error(`"${key}" is not a valid field.`);
        }
        continue;
      }
      if (typeof record[key] !== "string" && typeof record[key] !== "number") {
        throw new Error(
          `"${key}" contains invalid data type: ${typeof record[key]}`
        );
      }
      if (!preserveEmptyFields && record[key] === "") {
        delete record[key];
      }
    }
  }

  // ---- Bulk / migration -------------------------------------------------

  /**
   * Bulk-import records exactly as given, keeping the guid, timestamps and
   * change counter each arrives with, in a single Rust transaction. Returns a
   * per-record `{ guid }` or `{ error }`, so one bad record does not abort the
   * batch. Emits no observer events.
   *
   * This is how the migration copies records the JSON store created and still
   * identifies. Both stores have to hold a record under the same guid, or they
   * hold two unrelated datasets and no record in one can be compared against,
   * or updated from, its counterpart in the other.
   *
   * @param {Array<object>} records
   * @returns {Promise<Array<{guid: string}|{error: string}>>}
   */
  async addManyWithMeta(records) {
    const entries = [];
    for (const record of records) {
      // Awaited one at a time: a subclass may have to reach the key store to
      // build an entry, and this keeps the order of the results the order of
      // the records.
      entries.push(await this._fieldsWithMeta(record));
    }
    const results = await this._addManyWithMeta(await this._store(), entries);
    return results.map(result => this._toBulkOutcome(result));
  }

  /**
   * Bulk-import tombstones for records deleted locally but not yet uploaded, in
   * a single Rust transaction. Same per-record result shape as addManyWithMeta.
   * A profile without them re-uploads the records it has already deleted.
   *
   * @param {Array<{guid: string, timeDeleted: number}>} tombstones
   * @returns {Promise<Array<{guid: string}|{error: string}>>}
   */
  async addManyTombstones(tombstones) {
    const results = await this._addManyTombstones(
      await this._store(),
      tombstones
    );
    return results.map(result => this._toBulkTombstoneOutcome(result));
  }

  /**
   * Bulk-replace stored records with the ones handed in, one at a time. Same
   * per-record result shape as addManyWithMeta, and silent in the same way: no
   * notification and no count refresh.
   *
   * The metadata is replaced rather than merged: timestamps, use count and sync
   * change counter all come from the record handed in, where update() leaves
   * all of them to the store.
   *
   * A guid this store does not hold reports an error rather than being
   * inserted.
   *
   * @param {Array<object>} records
   * @returns {Promise<Array<{guid: string}|{error: string}>>}
   */
  async updateManyWithMeta(records) {
    const store = await this._store();
    const results = [];
    for (const record of records) {
      try {
        await this._updateWithMeta(store, await this._fieldsWithMeta(record));
        results.push({ guid: record.guid });
      } catch (e) {
        results.push({ error: String(e?.message ?? e) });
      }
    }
    return results;
  }

  /**
   * Delete records by guid, one at a time, without announcing them. Leaves a
   * tombstone for each guid the server knows about, as remove() does.
   *
   * @param {Array<string>} guids
   * @returns {Promise<Array<{guid: string}|{error: string}>>}
   */
  async removeMany(guids) {
    const store = await this._store();
    const results = [];
    for (const guid of guids) {
      try {
        await this._delete(store, guid);
        results.push({ guid });
      } catch (e) {
        results.push({ error: String(e?.message ?? e) });
      }
    }
    return results;
  }

  /**
   * Drop every record and every tombstone in the collection, silently, so it
   * can be rebuilt from scratch. Temporary, and goes away with the JSON store.
   *
   * The tombstones have to go too. Deleting a record sync knows about leaves one
   * behind, and the store refuses to insert a guid it holds a tombstone for, so
   * a wipe that kept them could only ever run once.
   */
  async wipe() {
    await this._deleteAll(await this._store());
  }

  // ---- Merge hooks, for a subclass that needs them -----------------------

  /**
   * The canonical fields update() keeps as stored when the record it is given
   * omits them, even without `preserveOldProperties`.
   *
   * @returns {Array<string>}
   */
  get _fieldsKeptWhenOmitted() {
    return [];
  }

  /**
   * Restore whatever the subclass derives on read, so a stored record can be
   * merged with. Nothing to do unless a derived field would be written back as
   * though it were canonical.
   *
   * @param {object} _record The stored record, modified in place.
   */
  async _prepareStoredForMerge(_record) {}

  /**
   * Reject a record that cannot be stored, on the whole record rather than a
   * single field.
   *
   * @param {object} _record
   */
  _validateRecord(_record) {}

  /**
   * Re-derive what a merge may have dropped, immediately before the write.
   *
   * @param {object} _record The record, modified in place.
   */
  _recomputeBeforeWrite(_record) {}
}
