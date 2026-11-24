/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Rust-backed credit card storage adapter.
 *
 * The credit card half of `RustAutofillAdapterBase`, which holds everything
 * that is not specific to this collection. What is here follows from the Rust
 * credit card schema: the store encrypts the number under its own key and
 * keeps the last four digits in the clear, so `cc-number` is written in the
 * clear and read back as a mask of those digits. The security code is
 * encrypted too; a record only says whether the card has one, as
 * `hasSecurityCode`. A record this adapter hands out is built from a
 * CreditCardCandidate and holds neither secret, except that
 * getDuplicateRecords adds the security code for the save doorhanger to
 * compare; #secretsOf is where they are read.
 */

import {
  RustAutofillAdapterBase,
  INTERNAL_FIELDS,
} from "resource://autofill/RustAutofillAdapterBase.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  CREDIT_CARD_SCHEMA_VERSION:
    "resource://autofill/FormAutofillStorageBase.sys.mjs",
  AutofillApiError:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs",
  AutofillDataTypes: "resource://gre/modules/shared/AutofillDataTypes.sys.mjs",
  CreditCardRecord: "resource://gre/modules/shared/CreditCardRecord.sys.mjs",
  CreditCardBulkResultEntry:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs",
  CreditCardBulkTombstoneResultEntry:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs",
  CreditCardMeta:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs",
  CreditCardTombstone:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs",
  CryptoError:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs",
  UpdatableCreditCardFieldsWithMeta:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs",
  FormAutofill: "resource://autofill/FormAutofill.sys.mjs",
  FormAutofillUtils: "resource://gre/modules/shared/FormAutofillUtils.sys.mjs",
  NoSuchRecord:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs",
  UpdatableCreditCardFields:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs",
});

const logger = console.createInstance({
  logModule: "RustAutofillCreditCardStorage",
  maxLogLevelPref: "extensions.formautofill.loglevel",
});

// Canonical string fields: JS hyphenated key <-> Rust camelCase field. The
// expiry pair is handled apart from these because its columns are i64, and the
// number apart from those because it is stored encrypted.
const JS_TO_RUST_STRING_FIELD = {
  "cc-name": "ccName",
  "cc-type": "ccType",
};

// Split from the strings above only because these columns are i64: an empty
// string is what an absent string field is written as, and would not convert.
const JS_TO_RUST_INTEGER_FIELD = {
  "cc-exp-month": "ccExpMonth",
  "cc-exp-year": "ccExpYear",
};

// cc-number and cc-csc are in neither map. cc-number is written in the clear
// as ccNumber, which the store encrypts, and read back as a mask of
// ccNumberLast4; the migration compares it by the number each store decrypts
// rather than by the mask -- see AutofillStorageMigrator.#verify. cc-csc is
// stored encrypted as ccCvv; a record reports only whether the card has one,
// as hasSecurityCode.

// The canonical fields: the ones this store has a column for. Unlike
// VALID_CREDIT_CARD_FIELDS this has cc-csc, which the JSON store keeps only as
// an unknown field, and not billingAddressGUID, which this store has no column
// for.
const VALID_FIELDS = [
  ...Object.keys(JS_TO_RUST_STRING_FIELD),
  ...Object.keys(JS_TO_RUST_INTEGER_FIELD),
  "cc-number",
  "cc-csc",
];

const storedFields = new Set([
  ...VALID_FIELDS.filter(field => field != "cc-csc"),
  ...INTERNAL_FIELDS,
]);

/**
 * Whether this store holds a field of this name, as opposed to deriving it on
 * read or not recognising it at all.
 *
 * Used when comparing a record here against the same record in the JSON store,
 * to decide whether a difference means the copy is unfaithful. Only a stored
 * field can answer that. The copy carries the canonical fields and metadata and
 * nothing else, so those are the only ones a failed copy can corrupt.
 *
 * A derived field is deliberately excluded even though this store understands
 * it. The JSON store persists what it computed at write time and hands back
 * what it wrote, while this one recomputes on every read, so the two disagree
 * whenever the derivation has since changed -- for the whole population at
 * once, not for the records a copy damaged. Two cases:
 *
 *  - cc-type is re-detected from the number on every normalise, so a change to
 *    the detection table moves every stored card.
 * The name components and cc-exp are excluded for that reason.
 *
 * cc-csc is excluded too: the copy out of this store leaves it behind -- see
 * _recordForMigrationExport.
 *
 * cc-number is included, but compared by its cleartext rather than its mask
 * -- see AutofillStorageMigrator.#verify.
 *
 * @param {string} field
 * @returns {boolean}
 */
export function isStoredCreditCardField(field) {
  return storedFields.has(field);
}

const isEmptyExpiry = v => v === undefined || v === null || v === "" || v === 0;

/**
 * Whether a field disagrees between a record and its copy, for the fields
 * where string equality is the wrong question.
 *
 * The expiry columns are non-optional integers, so an absent expiry is stored
 * as 0 and read back without the field -- see creditCardToJsRecord. The JSON
 * store holds 0 as it was given, and treats it as absent too. Only that empty
 * pair is equal; any other mismatch, a string against its number included, is
 * still reported.
 *
 * @param {string} field
 * @param {*} a The value on the record.
 * @param {*} b The value on its copy.
 * @returns {boolean}
 */
export function creditCardFieldDiffers(field, a, b) {
  switch (field) {
    case "cc-exp-month":
    case "cc-exp-year":
      if (isEmptyExpiry(a) && isEmptyExpiry(b)) {
        return false;
      }
      return a !== b;
    default:
      return a !== b;
  }
}

/**
 * How wide to rebuild the mask on a number this store no longer holds in full.
 *
 * The JSON store masks the number it was given, so its `cc-number` is as long
 * as the card was. This store keeps only the last four digits, so the length is
 * gone and the mask is rebuilt at the width of the common case. A 14- or
 * 15-digit card reads back wider here than it was written.
 *
 * Consumers use `cc-number` for display, and the ones that need the number
 * itself read it from the store, so this costs presentation rather than
 * correctness.
 */
const MASKED_NUMBER_LENGTH = 16;

// What the mask is built from, and so what tells a masked number from a real
// one. See isMasked.
const MASK_CHARACTER = "•";

// A masked value is not a number: it is what every record reads back as. The
// store encrypts whatever number it is given, so writing a mask would store it
// in place of the card, and the one conversion every write goes through refuses
// it.
const isMasked = value => String(value ?? "").includes(MASK_CHARACTER);

// The fields this store keeps encrypted, and the CreditCard property holding
// each one in the clear. Every field of CreditCardRecord.SECRET_FIELDS.
const SECRET_FIELDS = new Map([
  ["cc-number", "ccNumber"],
  ["cc-csc", "ccCvv"],
]);

/**
 * The error a caller owes for a key NSS will not hand over: what crypto-SDR
 * and the JSON store report, and what tells a caller to try again later rather
 * than treat one record as lost.
 *
 * @returns {Error}
 */
const primaryPasswordLockedError = () =>
  Components.Exception("Primary password locked", Cr.NS_ERROR_ABORT);

/**
 * Convert a JS credit card record to the Rust `UpdatableCreditCardFields`.
 *
 * @param {object} record
 * @throws if the record's number is a mask.
 */
function jsRecordToUpdatableCreditCardFields(record) {
  if (isMasked(record["cc-number"])) {
    throw new Error("Got a masked cc-number when writing");
  }
  const fields = {};
  for (const [jsKey, rustKey] of Object.entries(JS_TO_RUST_STRING_FIELD)) {
    fields[rustKey] = record[jsKey] ?? "";
  }
  for (const [jsKey, rustKey] of Object.entries(JS_TO_RUST_INTEGER_FIELD)) {
    fields[rustKey] = Number(record[jsKey]) || 0;
  }
  fields.ccNumber = record["cc-number"] ?? "";
  // The store keeps its CVV on null and clears it on "". update() has already
  // merged in a stored cc-csc the caller omitted, so none here means no CVV.
  fields.ccCvv = record["cc-csc"] ? String(record["cc-csc"]) : "";
  return new lazy.UpdatableCreditCardFields(fields);
}

/**
 * Convert a Rust `CreditCardCandidate` to a JS credit card record: stored
 * fields, metadata, schema version and computed fields. The candidate holds
 * no security code, only whether one is stored, which the record reports as
 * `hasSecurityCode`.
 */
function creditCardToJsRecord(creditCard) {
  const record = {
    guid: creditCard.guid,
    version: lazy.CREDIT_CARD_SCHEMA_VERSION,
  };

  for (const [jsKey, rustKey] of Object.entries(JS_TO_RUST_STRING_FIELD)) {
    const value = creditCard[rustKey];
    if (value !== undefined && value !== "") {
      record[jsKey] = value;
    }
  }

  // 0 is how an absent expiry reaches us: the columns are non-optional i64 and
  // a record written without one stored zero.
  for (const [jsKey, rustKey] of Object.entries(JS_TO_RUST_INTEGER_FIELD)) {
    if (creditCard[rustKey]) {
      record[jsKey] = creditCard[rustKey];
    }
  }

  if (creditCard.ccNumberLast4) {
    record["cc-number"] =
      MASK_CHARACTER.repeat(
        Math.max(0, MASKED_NUMBER_LENGTH - creditCard.ccNumberLast4.length)
      ) + creditCard.ccNumberLast4;
  }

  if (creditCard.hasCvv) {
    record.hasSecurityCode = true;
  }

  record.timeCreated = creditCard.timeCreated;
  record.timeLastUsed = creditCard.timeLastUsed ?? 0;
  record.timeLastModified = creditCard.timeLastModified;
  record.timesUsed = creditCard.timesUsed;

  // Only the name components and cc-exp are derived here. cc-type is stored:
  // computeFields re-detects it from cc-number, which is masked by now, and
  // leaves it alone when it cannot read one.
  lazy.CreditCardRecord.computeFields(record);

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
 * Adapter presenting the AutofillRecords credit card interface over the Rust
 * Store.
 */
export class RustAutofillCreditCardsAdapter extends RustAutofillAdapterBase {
  static _instance = null;

  get _dataType() {
    return lazy.AutofillDataTypes.CREDIT_CARD;
  }

  get _logger() {
    return logger;
  }

  get _recordApi() {
    return lazy.CreditCardRecord;
  }

  get _validFields() {
    return VALID_FIELDS;
  }

  // The JSON store keeps cc-csc as an unknown field, which its update() never
  // clears.
  get _fieldsKeptWhenOmitted() {
    return ["cc-csc"];
  }

  _recordFromRust(creditCard, { rawData = false } = {}) {
    const record = creditCardToJsRecord(creditCard);
    if (!rawData) {
      lazy.CreditCardRecord.stripSecurityCodeWhenDisabled(record);
    }
    return record;
  }

  _normalize(record, preserveEmptyFields = false) {
    lazy.CreditCardRecord.normalizeFields(record);
    this._normalizeCanonicalFields(record, preserveEmptyFields);
    if (!Object.keys(record).length) {
      throw new Error("Record contains no valid field.");
    }
    return record;
  }

  // ---- Store operations -------------------------------------------------

  _countAll(store) {
    return store.countAllCreditCards();
  }

  // Every record is built from a CreditCardCandidate, which holds no encrypted
  // field and never needs the key.
  _get(store, guid) {
    return this.#read(store, () => store.getCreditCardCandidate(guid), guid);
  }

  _getAll(store) {
    return this.#read(store, () => store.listCreditCardCandidates());
  }

  _delete(store, guid) {
    return store.deleteCreditCard(guid);
  }

  _deleteAll(store) {
    return store.deleteAllCreditCards();
  }

  _touch(store, guid) {
    return store.touchCreditCard(guid);
  }

  // Writing needs the key as much as reading does, so a save can be declined.
  async _add(store, record) {
    const fields = jsRecordToUpdatableCreditCardFields(record);
    try {
      return await this.#withKey(() => store.addCreditCard(fields));
    } catch (e) {
      this.#recordStoreError("add", e, [record]);
      throw e;
    }
  }

  async _update(store, guid, record, stored) {
    const fields = jsRecordToUpdatableCreditCardFields(record);
    try {
      return await this.#withKey(() => store.updateCreditCard(guid, fields));
    } catch (e) {
      this.#recordStoreError("update", e, [record, stored]);
      throw e;
    }
  }

  _addManyWithMeta(store, entries) {
    return store.addManyCreditCardsWithMeta(entries);
  }

  _toBulkOutcome(result) {
    return result instanceof lazy.CreditCardBulkResultEntry.Error
      ? { error: result.message }
      : { guid: result.creditCard.guid };
  }

  _addManyTombstones(store, tombstones) {
    return store.addManyCreditCardTombstones(
      tombstones.map(
        t =>
          new lazy.CreditCardTombstone({
            guid: t.guid,
            timeDeleted: t.timeDeleted,
          })
      )
    );
  }

  _toBulkTombstoneOutcome(result) {
    return result instanceof lazy.CreditCardBulkTombstoneResultEntry.Error
      ? { error: result.message }
      : { guid: result.guid };
  }

  _updateWithMeta(store, entry) {
    return store.updateCreditCardWithMeta(entry);
  }

  // ---- Credit card specifics --------------------------------------------

  /**
   * As RustAutofillAdapterBase.getSavedFieldNames, except that a record says
   * it has a security code with hasSecurityCode rather than holding cc-csc.
   */
  async getSavedFieldNames() {
    const fieldNames = await super.getSavedFieldNames();
    if (fieldNames.delete("hasSecurityCode")) {
      fieldNames.add("cc-csc");
    }
    return fieldNames;
  }

  /**
   * Reads no encrypted field, so it never needs the key or prompts.
   *
   * @returns {Promise<{total: number, withSecurityCode: number}>}
   */
  async getSavedCounts() {
    const cards = await (await this._store()).listCreditCardCandidates();
    return {
      total: cards.length,
      withSecurityCode: cards.filter(card => card.hasCvv).length,
    };
  }

  /**
   * Find the stored cards with the same card number as the given record.
   *
   * The counterpart of CreditCardsBase.getDuplicateRecords, comparing what it
   * compares: the full number. Records read from this store hold only a masked
   * number, which keeps the last four digits, so the stored numbers have to be
   * decrypted to be compared.
   *
   * The store derives ccNumberLast4 from the number on every write, so only
   * the cards ending in the same four digits can hold the number, and only
   * those are decrypted, as logins decrypts only the candidates that match. A
   * number no stored card ends in is answered without the key. This runs on
   * every credit card submission, and a new card must not show the primary
   * password prompt before the user has chosen to save it.
   *
   * A card without a number is passed over: it never had one, or it was
   * scrubbed. So is a card whose number the unlocked key cannot decrypt: one
   * such card must not cost the user the save prompt for a different one, or
   * for the one that replaces it. A key that is unavailable stops the search:
   * it is not one card that cannot be read, it is all of them.
   *
   * A duplicate holds its security code in the clear, read with its number:
   * the caller compares it with the submitted one to decide what the update
   * doorhanger offers -- see CreditCardSecurityCodeUpdateDoorhanger.isNeeded.
   *
   * @param {object} record A normalised credit card record.
   * @yields {object} Each stored record holding the same number.
   * @throws NS_ERROR_ABORT if the user declined to unlock the key.
   */
  async *getDuplicateRecords(record) {
    for await (const { candidate, secrets } of this.#duplicates(record)) {
      const duplicate = this._recordFromRust(candidate);
      // A read leaves hasSecurityCode out while codes are disabled.
      if (duplicate.hasSecurityCode && secrets["cc-csc"]) {
        duplicate["cc-csc"] = secrets["cc-csc"];
      }
      yield duplicate;
    }
  }

  /**
   * The stored cards that are this record rather than merely sharing its
   * number: every field the record states has to agree. The counterpart of
   * CreditCardsBase.getMatchRecords.
   *
   * The secrets are compared in the clear: the stored record does not hold
   * them.
   *
   * @param {object} record A normalised credit card record.
   * @yields {object} Each stored record the given one is a subset of.
   */
  async *getMatchRecords(record) {
    const agrees = (field, value) => !record[field] || record[field] == value;
    for await (const { candidate, secrets } of this.#duplicates(record)) {
      const stored = this._recordFromRust(candidate);
      if (
        this._validFields.every(field =>
          agrees(
            field,
            SECRET_FIELDS.has(field) ? secrets[field] : stored[field]
          )
        )
      ) {
        yield stored;
      }
    }
  }

  /**
   * What getDuplicateRecords and getMatchRecords share: the stored cards
   * with the same number as this record, found as getDuplicateRecords
   * describes. Each comes with its secrets, so that getMatchRecords can
   * compare the security code without reading it again.
   *
   * @param {object} record A normalised credit card record.
   * @yields {{candidate: CreditCardCandidate, secrets: object}} Each stored
   *   card holding the same number, with its secrets from #secretsOf.
   */
  async *#duplicates(record) {
    const number = record["cc-number"];
    if (!number) {
      return;
    }
    const last4 = String(number).slice(-4);
    const candidates = (await this._getAll(await this._store())).filter(
      candidate => candidate.ccNumberLast4 == last4
    );
    if (!candidates.length) {
      return;
    }
    const secrets = await this.#secretsOf(
      candidates.map(c => c.guid),
      { skipUnreadable: true }
    );
    for (const candidate of candidates) {
      const cardSecrets = secrets.get(candidate.guid);
      if (cardSecrets?.["cc-number"] == number) {
        yield { candidate, secrets: cardSecrets };
      }
    }
  }

  /**
   * Reject a complete record that cannot be stored. Mirrors
   * CreditCardsBase._validateFields, and is called where that is: on the whole
   * record, after a merge rather than before one, because the field it requires
   * can arrive from either side.
   *
   * normalizeFields drops a number that does not validate, so an invalid number
   * and a missing one reach this the same way.
   *
   * @param {object} record
   */
  _validateRecord(record) {
    if (!record["cc-number"]) {
      throw new Error("Missing/invalid cc-number");
    }
  }

  /**
   * Run a store read, telling a locked key from a lost one when it fails, and
   * report the failure.
   *
   * A read that decrypts fails as a whole on one number it cannot decrypt.
   * With the token locked that is every card, and the caller owes
   * NS_ERROR_ABORT. Otherwise the key cannot read what another key wrote --
   * the token was reset, or key4.db replaced -- and the CryptoError is passed
   * on. The stored numbers are left as they are, as passwordmgr leaves logins
   * it cannot decrypt.
   *
   * @param {Store} store
   * @param {Function} operation An async function making the store call.
   * @param {string} [guid] The card read, or none for every card.
   * @returns {Promise<*>} What `operation` resolves to.
   * @throws NS_ERROR_ABORT if the token is locked, or the store's CryptoError
   *   if a number cannot be decrypted with it unlocked.
   */
  async #read(store, operation, guid) {
    try {
      return await operation();
    } catch (e) {
      const error =
        e instanceof lazy.CryptoError && this._primaryPasswordLocked()
          ? primaryPasswordLockedError()
          : e;
      if (!(error instanceof lazy.NoSuchRecord)) {
        this.#recordStoreError(
          "read",
          error,
          await this.#candidates(store, guid)
        );
      }
      throw error;
    }
  }

  /**
   * The cards a read covered, listed without the key, so this works when
   * decrypting failed.
   *
   * @param {Store} store
   * @param {string} [guid] The card read, or none for every card.
   * @returns {Promise<?Array<CreditCardCandidate>>} Null if the listing fails.
   */
  async #candidates(store, guid) {
    try {
      if (guid) {
        return [await store.getCreditCardCandidate(guid)];
      }
      return await store.listCreditCardCandidates();
    } catch (e) {
      return e instanceof lazy.NoSuchRecord ? [] : null;
    }
  }

  /**
   * Record a failed store call. Only the error category is recorded, never a
   * card value.
   *
   * @param {string} operation "add", "update" or "read".
   * @param {Error} e
   * @param {?Array<object>} records The records involved: for an update, the
   *   record written, then the stored one. Null if unknown.
   */
  #recordStoreError(operation, e, records) {
    // A candidate from the listing has hasCvv instead of the code.
    const hasCode = record => Boolean(record?.["cc-csc"] || record?.hasCvv);
    if (operation === "update" && !hasCode(records[0]) && hasCode(records[1])) {
      operation = "clear";
    }
    let errorType = "Unknown";
    if (e instanceof lazy.AutofillApiError) {
      errorType = e.constructor.name;
    } else if (e?.result === Cr.NS_ERROR_ABORT) {
      errorType = "PrimaryPasswordLocked";
    }
    Glean.formautofillCreditcards.storeError.record({
      operation,
      error_type: errorType,
      ...(records ? { has_cvv: records.some(hasCode) } : {}),
    });
  }

  /**
   * Run a store operation that needs the key, allowing the primary password
   * prompt for it.
   *
   * @param {Function} operation An async function making the store call.
   * @returns {Promise<*>} What `operation` resolves to.
   * @throws NS_ERROR_ABORT if the operation failed on a locked key.
   */
  async #withKey(operation) {
    try {
      return await this._allowingPrompt(operation);
    } catch (e) {
      throw this._primaryPasswordLocked() ? primaryPasswordLockedError() : e;
    }
  }

  /**
   * The secrets of stored cards, in the clear.
   *
   * The only read in this adapter that decrypts, and so the only one that can
   * show the primary password prompt. Only the secrets leave it, never a
   * whole card: records are built from CreditCardCandidates.
   *
   * @param {Array<string>} guids
   * @param {object} [options]
   * @param {boolean} [options.skipUnreadable] Leave out a card the unlocked
   *   key cannot decrypt instead of failing the read.
   * @returns {Promise<Map<string, object>>} Each card's SECRET_FIELDS, keyed
   *   by field, null where the card has none: it never had one, or it was
   *   scrubbed. A guid the store no longer holds is left out.
   * @throws NS_ERROR_ABORT if the user declined to unlock the key.
   */
  #secretsOf(guids, { skipUnreadable = false } = {}) {
    return this.#withKey(async () => {
      const store = await this._store();
      const secrets = new Map();
      for (const guid of guids) {
        try {
          const card = await this.#read(
            store,
            () => store.getCreditCard(guid),
            guid
          );
          secrets.set(
            guid,
            Object.fromEntries(
              Array.from(SECRET_FIELDS, ([field, property]) => [
                field,
                card[property] || null,
              ])
            )
          );
        } catch (e) {
          // #read reports a locked key as NS_ERROR_ABORT, so a CryptoError is
          // a card this key cannot decrypt.
          const skip =
            e instanceof lazy.NoSuchRecord ||
            (skipUnreadable && e instanceof lazy.CryptoError);
          if (!skip) {
            throw e;
          }
        }
      }
      return secrets;
    });
  }

  /**
   * The secrets of one stored card. See #secretsOf.
   *
   * @param {string} guid
   * @returns {Promise<object>} The card's SECRET_FIELDS, keyed by field.
   * @throws NoSuchRecord if the store does not hold the card, or
   *   NS_ERROR_ABORT if the user declined to unlock the key.
   */
  async #secretsOfCard(guid) {
    const secrets = await this.#secretsOf([guid]);
    if (!secrets.has(guid)) {
      throw new lazy.NoSuchRecord(guid);
    }
    return secrets.get(guid);
  }

  /**
   * The cleartext of encrypted fields of a stored record. The counterpart of
   * CreditCardsBase.decryptFields.
   *
   * The record holds no ciphertext, so the secrets are read from the store by
   * the record's guid, all of them in one read. What the record holds for
   * them is for display and is not consulted. The key is the store's rather
   * than the OS key store's, so OS re-authentication is a step of its own
   * instead of something decrypting does on the way past.
   *
   * @param {object} record A record this store handed out.
   * @param {Array<string>} fields The fields to read, from SECRET_FIELDS.
   * @param {object} [options]
   * @param {string|false} [options.reauth] The OS re-authentication prompt to
   *   show first, or false to read them without one. A `trigger` is accepted
   *   and ignored: it labels the OS key store's telemetry, and this store does
   *   not reach the OS key store.
   * @returns {Promise<object>} Each field's cleartext, or null if the store
   *   holds none for the card, or it is a security code while codes are
   *   disabled. A record with no guid is not one the store holds: it reads as
   *   null throughout and asks nothing of the user.
   * @throws NoSuchRecord if the store no longer holds the card, or
   *   NS_ERROR_ABORT if the user declined either prompt.
   */
  async decryptFields(record, fields, { reauth = false } = {}) {
    for (const field of fields) {
      if (!SECRET_FIELDS.has(field)) {
        throw new Error(`${field} is not stored encrypted`);
      }
    }
    const cleartext = Object.fromEntries(fields.map(field => [field, null]));
    // The security code is not handed out while codes are disabled, as every
    // consumer read leaves it out then -- see
    // CreditCardRecord.stripSecurityCodeWhenDisabled.
    const wanted = fields.filter(
      field =>
        field != "cc-csc" || lazy.FormAutofill.isAutofillCreditCardCVVEnabled
    );
    if (!record.guid || !wanted.length) {
      return cleartext;
    }
    // TODO: Bug 2073416 - authenticating the user does not belong here. This
    // store's key is NSS's, so the OS prompt guards nothing it reads; the
    // option exists because CreditCardsBase cannot separate the prompt from
    // OSKeyStore.decrypt, and the fill path passes it to both stores alike.
    if (reauth) {
      const authenticated = await lazy.FormAutofillUtils.verifyUserOSAuth(
        lazy.FormAutofill.AUTOFILL_CREDITCARDS_OS_AUTH_LOCKED_PREF,
        reauth,
        "",
        null,
        // A key provisioned here would sit in the user's keychain unread.
        false
      );
      if (!authenticated) {
        throw Components.Exception(
          "User canceled OS unlock entry",
          Cr.NS_ERROR_ABORT
        );
      }
    }
    const secrets = await this.#secretsOfCard(record.guid);
    for (const field of wanted) {
      cleartext[field] = secrets[field];
    }
    return cleartext;
  }

  /**
   * Decrypt a single field of a stored record. See decryptFields.
   *
   * @param {object} record
   * @param {string} field
   * @param {object} [options] As for decryptFields.
   * @returns {Promise<?string>}
   */
  async decryptField(record, field, options) {
    return (await this.decryptFields(record, [field], options))[field];
  }

  /**
   * Restore the secrets, so a stored record can be merged with. Mirrors
   * CreditCardsBase._stripComputedFields: `cc-number` is held masked, so a
   * merge that kept the mask would write it as though it were the number.
   *
   * What the record holds for a secret is for display, so it goes first, and
   * is put back only as a value the store read. A card can have no number
   * behind its mask -- scrubEncryptedData() blanks the ciphertext and keeps
   * the last four digits -- and keeping the mask would store it as the number.
   * _validateRecord refuses the update instead, unless the caller supplies a
   * new number.
   *
   * The security code is restored the same way. It is in
   * _fieldsKeptWhenOmitted, so an update that omits it writes back what is
   * restored here, and without this would clear the stored code.
   *
   * @param {object} record The record, modified in place.
   * @throws NS_ERROR_ABORT if the user declined to unlock the key.
   */
  async _prepareStoredForMerge(record) {
    for (const field of SECRET_FIELDS.keys()) {
      delete record[field];
    }
    const secrets = await this.#secretsOfCard(record.guid);
    for (const field of SECRET_FIELDS.keys()) {
      if (secrets[field]) {
        record[field] = secrets[field];
      }
    }
  }

  /**
   * As the JSON store does before it saves: normalizeFields derives cc-type
   * from the number, so an edit that did not carry one left it empty and the
   * merge dropped it. Without this the stored type is cleared.
   *
   * @param {object} record The record, modified in place.
   */
  _recomputeBeforeWrite(record) {
    lazy.CreditCardRecord.computeFields(record);
  }

  /**
   * For the migration only. The metadata is taken from the record as given
   * rather than advanced, so unlike a user's edit this neither refreshes
   * timeLastModified nor increments the sync change counter.
   *
   * The record arrives with its number in the clear, from the source store's
   * _recordForMigrationExport, and the store encrypts it under its own key
   * rather than inheriting the other's ciphertext.
   *
   * @param {object} record
   */
  _fieldsWithMeta(record) {
    return new lazy.UpdatableCreditCardFieldsWithMeta({
      fields: jsRecordToUpdatableCreditCardFields(record),
      meta: new lazy.CreditCardMeta({
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

  /**
   * Hand a card over with its number in the clear, so the receiving store can
   * encrypt it under its own key. The counterpart of
   * CreditCardsBase._recordForMigrationExport, and refuses the same way: a
   * record whose number cannot be read throws rather than being copied without
   * it.
   *
   * The security code is left behind: the JSON store would keep it in the
   * clear.
   *
   * @param {object} record
   * @returns {Promise<object>}
   */
  async _recordForMigrationExport(record) {
    const exported = { ...record };
    delete exported["cc-csc"];
    delete exported.hasSecurityCode;
    // A record that never had a number has no mask either, and crosses
    // unchanged.
    if (!exported["cc-number"]) {
      return exported;
    }
    const number = (await this.#secretsOfCard(record.guid))["cc-number"];
    if (!number) {
      // The mask has no number behind it, so handing it over would have the
      // receiving store encrypt the mask in place of the card.
      throw new Error("Got a masked cc-number when exporting");
    }
    exported["cc-number"] = number;
    return exported;
  }

  /**
   * The store's bridged sync engine, driven through mozIBridgedSyncEngine.
   *
   * This is how the Rust store syncs: change detection and reconciliation
   * happen inside Rust, so none of the per-record sync methods on the JSON
   * collection have a counterpart here. BridgedCreditCardsEngine drives it.
   *
   * @returns {Promise<CreditCardsBridgedEngine>}
   */
  async bridgedEngine() {
    return (await this._store()).creditCardsBridgedEngine();
  }
}
