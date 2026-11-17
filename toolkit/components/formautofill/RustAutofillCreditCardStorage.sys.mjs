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
 * clear and read back as a mask of those digits. A record this adapter hands
 * out never carries the number itself; #numberOf is where it is read.
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
// stored encrypted as ccCvv and read back in the clear.

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

// The fields creditCardToJsRecord derives with CreditCardRecord.computeFields.
const COMPUTED_FIELDS = [
  "cc-given-name",
  "cc-additional-name",
  "cc-family-name",
  "cc-exp",
];

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
 * Convert a Rust `CreditCard` to a JS credit card record: stored fields,
 * metadata, schema version and computed fields.
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

  if (creditCard.ccCvv) {
    record["cc-csc"] = creditCard.ccCvv;
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

  _get(store, guid) {
    return this.#read(store, () => store.getCreditCard(guid), guid);
  }

  _getAll(store) {
    return this.#read(store, () => store.getAllCreditCards());
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
   * Reading a card decrypts its number, so with the token locked a read shows
   * the primary password prompt.
   *
   * @param {string} guid
   * @param {object} [options]
   * @param {boolean} [options.rawData=false]
   * @param {boolean} [options.prompt=true] Whether the read may prompt. A read
   *   nobody asked for passes false, and rejects with NS_ERROR_ABORT while the
   *   token is locked.
   */
  get(guid, { rawData = false, prompt = true } = {}) {
    return prompt
      ? this._allowingPrompt(() => super.get(guid, { rawData }))
      : super.get(guid, { rawData });
  }

  /**
   * @see get
   * @param {object} [options]
   * @param {boolean} [options.rawData=false]
   * @param {boolean} [options.prompt=true]
   */
  getAll({ rawData = false, prompt = true } = {}) {
    return prompt
      ? this._allowingPrompt(() => super.getAll({ rawData }))
      : super.getAll({ rawData });
  }

  /**
   * Runs at startup and on every change, so it never prompts. With the token
   * locked no card can be read, and every field is reported as saved for as
   * long as there are cards: a form is then still offered autofill, and opening
   * it is what asks for the primary password.
   */
  async getSavedFieldNames() {
    try {
      return await super.getSavedFieldNames();
    } catch (e) {
      if (e.result != Cr.NS_ERROR_ABORT) {
        throw e;
      }
      if (!(await this.refreshCount())) {
        return new Set();
      }
      return new Set([...this._validFields, ...COMPUTED_FIELDS]);
    }
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
   * The stored cards holding the number this record carries.
   *
   * The counterpart of CreditCardsBase.getDuplicateRecords, comparing what it
   * compares: the number itself. The mask keeps only the last four digits, so
   * nothing a record carries answers the question, and the cards are read from
   * the store as it hands them over rather than through getAll().
   *
   * A card the store hands over without a number is passed over: it never had
   * one, or it was scrubbed. So is a card whose number the unlocked key cannot
   * decrypt. This runs on every credit card submission, and one such card must
   * not cost the user the save prompt for a different one, or for the one that
   * replaces it. A key that is unavailable stops the search: it is not one card
   * that cannot be read, it is all of them.
   *
   * @param {object} record A normalised credit card record.
   * @yields {object} Each stored record holding the same number.
   * @throws NS_ERROR_ABORT if the user declined to unlock the key.
   */
  async *getDuplicateRecords(record) {
    if (!record["cc-number"]) {
      return;
    }
    const store = await this._store();
    let cards;
    try {
      cards = await this.#withKey(() => this._getAll(store));
    } catch (e) {
      if (!(e instanceof lazy.CryptoError)) {
        throw e;
      }
      cards = await this.#readableCards(store);
    }
    for (const card of cards) {
      if (card.ccNumber && card.ccNumber == record["cc-number"]) {
        yield this._recordFromRust(card);
      }
    }
  }

  /**
   * Every stored card the key can decrypt, read one at a time. The store fails
   * a read of every card on any one it cannot decrypt, and that read has
   * already recorded the store error, so these reads go to the store directly.
   *
   * @param {Store} store
   * @returns {Promise<Array<CreditCard>>}
   */
  async #readableCards(store) {
    const cards = [];
    for (const { guid } of await store.listCreditCardCandidates()) {
      try {
        cards.push(await store.getCreditCard(guid));
      } catch (e) {
        if (
          !(e instanceof lazy.CryptoError) &&
          !(e instanceof lazy.NoSuchRecord)
        ) {
          throw e;
        }
      }
    }
    return cards;
  }

  /**
   * The stored cards that are this record rather than merely sharing its
   * number: every field the record states has to agree. The counterpart of
   * CreditCardsBase.getMatchRecords.
   *
   * @param {object} record A normalised credit card record.
   * @yields {object} Each stored record the given one is a subset of.
   */
  async *getMatchRecords(record) {
    const fields = this._validFields.filter(field => field != "cc-number");
    for await (const stored of this.getDuplicateRecords(record)) {
      if (
        fields.every(field => !record[field] || record[field] == stored[field])
      ) {
        yield stored;
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
   * Run a store read, telling a locked key from a lost one when it fails.
   *
   * The store decrypts every number it hands over, and fails the whole read on
   * one it cannot decrypt. With the token locked that is every card, and the
   * caller owes NS_ERROR_ABORT. Otherwise the key cannot read what another key
   * wrote -- the token was reset, or key4.db replaced -- and the CryptoError is
   * passed on. The stored numbers are left as they are, as passwordmgr leaves
   * logins it cannot decrypt.
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
      const cards = await store.listCreditCardCandidates();
      return cards.filter(card => !guid || card.guid === guid);
    } catch {
      return null;
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
   * The number of a stored card, in the clear.
   *
   * With getDuplicateRecords, the only place this adapter reads a number. The
   * store decrypts every card it hands over, and every record built from one
   * carries a mask instead, so the number goes no further than the caller that
   * asked for it.
   *
   * @param {string} guid
   * @returns {Promise<?string>} The number, or null if the store handed the
   *   card over without one: it never had one, or it was scrubbed.
   * @throws NS_ERROR_ABORT if the user declined to unlock the key.
   */
  async #numberOf(guid) {
    const card = await this.#withKey(async () =>
      this._get(await this._store(), guid)
    );
    return card.ccNumber || null;
  }

  /**
   * The cleartext of an encrypted field of a stored record. The counterpart of
   * CreditCardsBase.decryptField.
   *
   * The record carries a mask rather than a ciphertext, so the number is read
   * from the store by the record's guid. The key is the store's rather than the
   * OS key store's, so OS re-authentication is a step of its own instead of
   * something decrypting does on the way past.
   *
   * @param {object} record A record this store handed out.
   * @param {string} field The field to read, e.g. "cc-number".
   * @param {object} [options]
   * @param {string|false} [options.reauth] The OS re-authentication prompt to
   *   show first, or false to read it without one. A `trigger` is accepted and
   *   ignored: it labels the OS key store's telemetry, and this store does not
   *   reach the OS key store.
   * @returns {Promise<?string>} The cleartext, or null if the store holds none
   *   for the record. Only a record this store handed out with a number carries
   *   a mask, so one without holds none here, and nothing is asked of the user
   *   for it.
   * @throws NS_ERROR_ABORT if the user declined either prompt.
   */
  async decryptField(record, field, { reauth = false } = {}) {
    // Refuses a field no store keeps encrypted.
    lazy.CreditCardRecord.ciphertextField(field);
    if (!isMasked(record[field])) {
      return null;
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
    return this.#numberOf(record.guid);
  }

  /**
   * Restore the number, so a stored record can be merged with. Mirrors
   * CreditCardsBase._stripComputedFields: `cc-number` is held masked, so a
   * merge that kept the mask would write it as though it were the number.
   *
   * The mask goes first and is put back only as a number the store read. A card
   * can hold a mask with no number behind it -- scrubEncryptedData() blanks the
   * ciphertext and keeps the last four digits -- and leaving the mask would
   * store it as the number. _validateRecord refuses the update instead, unless
   * the caller supplies a new number.
   *
   * @param {object} record The record, modified in place.
   * @throws NS_ERROR_ABORT if the user declined to unlock the key.
   */
  async _prepareStoredForMerge(record) {
    const masked = record["cc-number"];
    delete record["cc-number"];
    if (!masked) {
      return;
    }
    const number = await this.#numberOf(record.guid);
    if (number) {
      record["cc-number"] = number;
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
    // A record that never had a number has no mask either, and crosses
    // unchanged.
    if (!exported["cc-number"]) {
      return exported;
    }
    const number = await this.#numberOf(record.guid);
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
