/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Rust-backed address storage adapter.
 *
 * The address half of `RustAutofillAdapterBase`, which holds everything that is
 * not specific to this collection. Every canonical address field is a plain
 * string column.
 */

import {
  RustAutofillAdapterBase,
  INTERNAL_FIELDS,
} from "resource://autofill/RustAutofillAdapterBase.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ADDRESS_SCHEMA_VERSION: "resource://autofill/FormAutofillStorageBase.sys.mjs",
  VALID_ADDRESS_FIELDS: "resource://autofill/FormAutofillStorageBase.sys.mjs",
  AddressBulkResultEntry:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs",
  AddressBulkTombstoneResultEntry:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs",
  AddressMeta:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs",
  AddressRecord: "resource://gre/modules/shared/AddressRecord.sys.mjs",
  AddressTombstone:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs",
  AutofillDataTypes: "resource://gre/modules/shared/AutofillDataTypes.sys.mjs",
  UpdatableAddressFields:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs",
  UpdatableAddressFieldsWithMeta:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs",
});

const logger = console.createInstance({
  logModule: "RustAutofillAddressStorage",
  maxLogLevelPref: "extensions.formautofill.loglevel",
});

// Canonical (stored) address fields: JS hyphenated key <-> Rust camelCase field.
const JS_TO_RUST_FIELD = {
  name: "name",
  organization: "organization",
  "street-address": "streetAddress",
  "address-level3": "addressLevel3",
  "address-level2": "addressLevel2",
  "address-level1": "addressLevel1",
  "postal-code": "postalCode",
  country: "country",
  tel: "tel",
  email: "email",
};

const storedFields = new Set([
  ...Object.keys(JS_TO_RUST_FIELD),
  ...INTERNAL_FIELDS,
]);

/**
 * Whether this store keeps a column for a field, as opposed to deriving it on
 * read or not recognising it at all.
 *
 * Used when comparing a record here against the same record in the JSON store,
 * to decide whether a difference means the copy is unfaithful. Only a stored
 * field can answer that. The copy carries the canonical fields and metadata and
 * nothing else, so those are the only ones a failed copy can corrupt.
 *
 * A derived field -- country-name, address-line*, the name, street and tel
 * components -- is deliberately excluded even though this store understands it.
 * The JSON store persists those at write time and hands back what it wrote,
 * while this one recomputes them on every read, so the two disagree whenever
 * the derivation has since changed. The common case is the app locale: a
 * profile that saved "US" addresses under en-US holds country-name "United
 * States", and the same records read here in a de build derive "Vereinigte
 * Staaten". Nothing was lost in the copy, but counting that as unfaithful
 * refused the migration and, because the generation had still been recorded,
 * refused it permanently.
 *
 * A migration does not compare them at all, for the same reason: the copy never
 * carried them.
 *
 * @param {string} field
 * @returns {boolean}
 */
export function isStoredAddressField(field) {
  return storedFields.has(field);
}

/**
 * Convert a JS address record (hyphenated keys) to the Rust
 * `UpdatableAddressFields`. All Rust fields are non-optional strings, so absent
 * values become "".
 */
function jsRecordToUpdatableAddressFields(record) {
  const fields = {};
  for (const [jsKey, rustKey] of Object.entries(JS_TO_RUST_FIELD)) {
    fields[rustKey] = record[jsKey] ?? "";
  }
  return new lazy.UpdatableAddressFields(fields);
}

/**
 * Convert a Rust `Address` to a JS address record: canonical fields, metadata,
 * schema version and computed fields.
 */
function addressToJsRecord(address) {
  const record = { guid: address.guid, version: lazy.ADDRESS_SCHEMA_VERSION };

  for (const [jsKey, rustKey] of Object.entries(JS_TO_RUST_FIELD)) {
    const value = address[rustKey];
    if (value !== undefined && value !== "") {
      record[jsKey] = value;
    }
  }

  record.timeCreated = address.timeCreated;
  record.timeLastUsed = address.timeLastUsed ?? 0;
  record.timeLastModified = address.timeLastModified;
  record.timesUsed = address.timesUsed;

  // Only the canonical fields are stored; the computed ones (country-name,
  // address-line*, *-name, tel-*) are derived on read.
  lazy.AddressRecord.computeFields(record);

  // computeFields leaves an empty placeholder for each field it could not
  // derive. A record handed to a consumer carries no empty or hidden keys.
  for (const key of Object.keys(record)) {
    if (key.startsWith("_") || record[key] === "") {
      delete record[key];
    }
  }

  return record;
}

/**
 * Adapter presenting the AutofillRecords address interface over the Rust Store.
 */
export class RustAutofillAddressesAdapter extends RustAutofillAdapterBase {
  static _instance = null;

  get _dataType() {
    return lazy.AutofillDataTypes.ADDRESS;
  }

  get _logger() {
    return logger;
  }

  get _recordApi() {
    return lazy.AddressRecord;
  }

  get _validFields() {
    return lazy.VALID_ADDRESS_FIELDS;
  }

  _recordFromRust(address, { rawData = false } = {}) {
    const record = addressToJsRecord(address);
    if (!rawData) {
      lazy.AddressRecord.hideCountryWithoutMetaData(record);
    }
    return record;
  }

  _normalize(record, preserveEmptyFields = false) {
    lazy.AddressRecord.normalizeFields(record);
    this._normalizeCanonicalFields(record, preserveEmptyFields);
    const keys = Object.keys(record);
    // normalizeFields always leaves a country behind, falling back to the
    // app's default region when the record named none, so a record holding
    // nothing but a country is an empty record rather than a country-only one.
    // Same rule, for the same reason, as AutofillRecords._normalizeRecord.
    if (!keys.length || (keys.length == 1 && keys[0] == "country")) {
      throw new Error("Record contains no valid field.");
    }
    return record;
  }

  // ---- Store operations -------------------------------------------------

  _countAll(store) {
    return store.countAllAddresses();
  }

  _get(store, guid) {
    return store.getAddress(guid);
  }

  _getAll(store) {
    return store.getAllAddresses();
  }

  _delete(store, guid) {
    return store.deleteAddress(guid);
  }

  _deleteAll(store) {
    return store.deleteAllAddresses();
  }

  _touch(store, guid) {
    return store.touchAddress(guid);
  }

  _add(store, record) {
    return store.addAddress(jsRecordToUpdatableAddressFields(record));
  }

  _update(store, guid, record) {
    return store.updateAddress(guid, jsRecordToUpdatableAddressFields(record));
  }

  _addManyWithMeta(store, entries) {
    return store.addManyAddressesWithMeta(entries);
  }

  _toBulkOutcome(result) {
    return result instanceof lazy.AddressBulkResultEntry.Error
      ? { error: result.message }
      : { guid: result.address.guid };
  }

  _addManyTombstones(store, tombstones) {
    return store.addManyAddressTombstones(
      tombstones.map(
        t =>
          new lazy.AddressTombstone({
            guid: t.guid,
            timeDeleted: t.timeDeleted,
          })
      )
    );
  }

  _toBulkTombstoneOutcome(result) {
    return result instanceof lazy.AddressBulkTombstoneResultEntry.Error
      ? { error: result.message }
      : { guid: result.guid };
  }

  _updateWithMeta(store, entry) {
    return store.updateAddressWithMeta(entry);
  }

  /**
   * For the migration only. The metadata is taken from the record as given
   * rather than advanced, so unlike a user's edit this neither refreshes
   * timeLastModified nor increments the sync change counter.
   *
   * @param {object} record
   */
  _fieldsWithMeta(record) {
    return new lazy.UpdatableAddressFieldsWithMeta({
      fields: jsRecordToUpdatableAddressFields(record),
      meta: new lazy.AddressMeta({
        guid: record.guid,
        timeCreated: record.timeCreated ?? 0,
        timeLastUsed: record.timeLastUsed || null,
        timeLastModified: record.timeLastModified ?? record.timeCreated ?? 0,
        timesUsed: record.timesUsed ?? 0,
        // A record with no `_sync` metadata has never been synced, which counts
        // as one change pending upload rather than none.
        syncChangeCounter: record._sync?.changeCounter ?? 1,
      }),
    });
  }

  // ---- Address specifics ------------------------------------------------

  /**
   * The store's bridged sync engine, driven through mozIBridgedSyncEngine.
   *
   * This is how the Rust store syncs: change detection and reconciliation
   * happen inside Rust, so none of the per-record sync methods on the JSON
   * collection have a counterpart here. BridgedAddressesEngine drives it.
   *
   * @returns {Promise<AddressesBridgedEngine>}
   */
  async bridgedEngine() {
    return (await this._store()).addressesBridgedEngine();
  }
}
