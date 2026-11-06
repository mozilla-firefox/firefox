/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Exercises RustAutofillCreditCardsAdapter: JS credit card records round-trip
 * through the Rust Store, the number is stored encrypted and read back masked,
 * computed fields are reconstructed on read, and storage-changed observers
 * fire.
 */

const { RustAutofillCreditCardsAdapter } = ChromeUtils.importESModule(
  "resource://autofill/RustAutofillCreditCardStorage.sys.mjs"
);
const { RustAutofillAdapterBase } = ChromeUtils.importESModule(
  "resource://autofill/RustAutofillAdapterBase.sys.mjs"
);
const {
  CryptoError,
  createAutofillKey,
  createAutofillStoreWithStaticKeyManager,
} = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs"
);
const { initialize: initRustComponents } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustInitRustComponents.sys.mjs"
);
const TEST_NUMBER = "4111111111111111";
// 15 digits, so it exercises the mask this store cannot rebuild at its original
// width. See MASKED_NUMBER_LENGTH.
const TEST_NUMBER_AMEX = "378282246310005";

const TEST_RECORD = {
  "cc-name": "Jane Doe",
  "cc-number": TEST_NUMBER,
  "cc-exp-month": 4,
  "cc-exp-year": 2030,
};

// What a number reads back as: this store keeps only the last four digits, so
// the mask is rebuilt at a fixed width rather than the card's own.
function maskFor(number) {
  return "•".repeat(16 - 4) + number.slice(-4);
}

const CVV_SUPPORTED_PREF = "extensions.formautofill.creditCards.cvv.supported";
const CVV_ENABLED_PREF = "extensions.formautofill.creditCards.cvv.enabled";
const RUST_ACTIVE_PREF =
  "extensions.formautofill.creditCards.storage.rust.active";

registerCleanupFunction(() => {
  Services.prefs.clearUserPref(CVV_SUPPORTED_PREF);
  Services.prefs.clearUserPref(CVV_ENABLED_PREF);
  Services.prefs.clearUserPref(RUST_ACTIVE_PREF);
});

// Started up front: events recorded before it would replay on testResetFOG().
add_setup(() => {
  Services.fog.initializeFOG();
});

// The adapter is driven directly here, so nothing switches the profile to it:
// claim it is the store serving, which CVV support requires.
function enableSecurityCode() {
  Services.prefs.setStringPref(CVV_SUPPORTED_PREF, "on");
  Services.prefs.setBoolPref(CVV_ENABLED_PREF, true);
  Services.prefs.setBoolPref(RUST_ACTIVE_PREF, true);
}

function waitForStorageChanged(expectedAction) {
  return new Promise(resolve => {
    Services.obs.addObserver(function obs(subject, _topic, action) {
      if (action != expectedAction) {
        return;
      }
      Services.obs.removeObserver(obs, "formautofill-storage-changed");
      resolve(subject.wrappedJSObject);
    }, "formautofill-storage-changed");
  });
}

/**
 * Make the store's reads fail the way they do when the key cannot be used: a
 * crypto error, whether the user declined the primary password or the data is
 * unreadable, and the token's state is what says which it was.
 *
 * @param {Store} store The test store to break.
 * @param {object} token The adapter's stand-in NSS token.
 * @param {object} options
 * @param {boolean} options.canceled Whether the token is left locked.
 * @returns {object} `attempts`, and `restore()` to put the store back.
 */
function breakDecryption(store, token, { canceled }) {
  const state = {
    attempts: 0,
    restore() {
      delete store.getCreditCard;
      delete store.getAllCreditCards;
      // The token is unlocked again, as it would be once the user answers.
      token.locked = false;
    },
  };
  const read = async () => {
    state.attempts++;
    if (canceled) {
      token.locked = true;
    }
    throw new CryptoError("decryption failed");
  };
  store.getCreditCard = read;
  store.getAllCreditCards = read;
  return state;
}

/**
 * Make the store's writes fail the way they do when the key cannot be used: a
 * crypto error, the same whether or not the user declined.
 *
 * @param {Store} store The test store to break.
 * @param {object} token The adapter's stand-in NSS token.
 * @param {object} options
 * @param {boolean} options.canceled Whether the token is left locked.
 * @returns {object} `restore()` to put the store back.
 */
function breakEncryption(store, token, { canceled }) {
  store.addCreditCard = store.updateCreditCard = async () => {
    if (canceled) {
      token.locked = true;
    }
    throw new Error("crypto error");
  };
  return {
    restore() {
      delete store.addCreditCard;
      delete store.updateCreditCard;
      token.locked = false;
    },
  };
}

// The duplicate finders are generators, and what the assertions want is a list.
async function collect(asyncIterator) {
  const records = [];
  for await (const record of asyncIterator) {
    records.push(record);
  }
  return records;
}

// The store holds the key, so a test store is given one of its own rather than
// the NSS key the profile's store uses. Generating it still goes through NSS.
async function newAdapter(name) {
  await initRustComponents(PathUtils.profileDir);
  const store = await createAutofillStoreWithStaticKeyManager(
    FileTestUtils.getTempFile(name).path,
    createAutofillKey()
  );
  const adapter = new RustAutofillCreditCardsAdapter(store);
  // This store's key is its own rather than NSS's, so the real token says
  // nothing about it and a test that has to answer for one supplies this.
  const token = { locked: false };
  adapter._primaryPasswordLocked = () => token.locked;
  return { store, token, adapter };
}

// Records carry a mask, so what the store holds is asked of the store.
async function storedNumber(store, guid) {
  return (await store.getCreditCard(guid)).ccNumber;
}

add_task(async function test_duplicates_are_found_by_the_number() {
  const { adapter } = await newAdapter("autofill-cc-adapter-dupe.sqlite");
  const guid = await adapter.add(TEST_RECORD);
  await adapter.add({ ...TEST_RECORD, "cc-number": TEST_NUMBER_AMEX });

  // Same card, restated with a later expiry: a duplicate, because the number
  // is what identifies it.
  const found = await collect(
    adapter.getDuplicateRecords({
      ...TEST_RECORD,
      "cc-exp-year": 2031,
    })
  );
  Assert.deepEqual(
    found.map(r => r.guid),
    [guid],
    "the card with that number, and not the other one"
  );

  Assert.deepEqual(
    await collect(adapter.getDuplicateRecords({ "cc-name": "Jane Doe" })),
    [],
    "a record with no number matches nothing"
  );
});

add_task(async function test_a_match_has_to_agree_on_every_stated_field() {
  const { adapter } = await newAdapter("autofill-cc-adapter-match.sqlite");
  const guid = await adapter.add(TEST_RECORD);

  Assert.deepEqual(
    (await collect(adapter.getMatchRecords({ ...TEST_RECORD }))).map(
      r => r.guid
    ),
    [guid],
    "the same card matches"
  );
  Assert.deepEqual(
    await collect(
      adapter.getMatchRecords({ ...TEST_RECORD, "cc-exp-year": 2031 })
    ),
    [],
    "a card that differs on a stated field does not, though it is a duplicate"
  );
  Assert.deepEqual(
    (
      await collect(
        adapter.getMatchRecords({ "cc-number": TEST_NUMBER, "cc-name": "" })
      )
    ).map(r => r.guid),
    [guid],
    "a field the caller did not state is not compared"
  );
});

add_task(async function test_adapter_crud_and_computed_fields() {
  const { store, adapter } = await newAdapter("autofill-cc-adapter.sqlite");

  // isEmpty() answers from a cached count and reports empty until something
  // primes it, so prime it first: without this the assertion passes whatever
  // the store holds.
  await adapter.refreshCount();
  Assert.ok(adapter.isEmpty(), "adapter starts empty");

  const addNotified = waitForStorageChanged("add");
  const guid = await adapter.add(TEST_RECORD);
  const addSubject = await addNotified;
  Assert.ok(guid, "add returns a guid");
  Assert.equal(
    addSubject.collectionName,
    "creditCards",
    "notifies creditCards"
  );
  Assert.equal(addSubject.guid, guid, "notifies the added guid");
  Assert.ok(!adapter.isEmpty(), "not empty after add");

  const fetched = await adapter.get(guid);
  Assert.equal(fetched.guid, guid, "guid round-trips");
  Assert.equal(fetched["cc-name"], "Jane Doe", "cc-name");
  Assert.equal(fetched["cc-exp-month"], 4, "cc-exp-month round-trips as i64");
  Assert.equal(fetched["cc-exp-year"], 2030, "cc-exp-year round-trips as i64");
  Assert.equal(fetched.timesUsed, 0, "timesUsed starts at 0");

  // The plaintext number is never handed back.
  Assert.notEqual(fetched["cc-number"], TEST_NUMBER, "number is not in clear");
  Assert.ok(
    !("cc-number-encrypted" in fetched),
    "nor is a ciphertext, which the store keeps to itself"
  );
  Assert.equal(
    fetched["cc-number"],
    maskFor(TEST_NUMBER),
    "masked number keeps the last four digits and nothing else"
  );
  Assert.equal(
    await storedNumber(store, guid),
    TEST_NUMBER,
    "the store holds the number that was written"
  );

  // Computed fields reconstructed by CreditCardRecord.computeFields.
  Assert.equal(fetched["cc-given-name"], "Jane", "computed cc-given-name");
  Assert.equal(fetched["cc-family-name"], "Doe", "computed cc-family-name");
  Assert.equal(fetched["cc-exp"], "2030-04", "computed cc-exp");
  // cc-type is stored, not re-detected: the number is masked by read time, so
  // re-detecting would clear it.
  Assert.equal(fetched["cc-type"], "visa", "cc-type survives the round trip");

  const all = await adapter.getAll();
  Assert.equal(all.length, 1, "getAll returns one");

  const names = await adapter.getSavedFieldNames();
  Assert.ok(names.has("cc-name"), "saved field names include cc-name");
  Assert.ok(
    names.has("cc-exp-month"),
    "saved field names include cc-exp-month"
  );

  await adapter.notifyUsed(guid);
  Assert.equal(
    (await adapter.get(guid)).timesUsed,
    1,
    "timesUsed incremented by notifyUsed"
  );

  const removeNotified = waitForStorageChanged("remove");
  await adapter.remove(guid);
  await removeNotified;
  Assert.ok(adapter.isEmpty(), "empty after remove");
});

add_task(async function test_update_without_restating_the_number() {
  const { store, adapter } = await newAdapter(
    "autofill-cc-adapter-update.sqlite"
  );
  const guid = await adapter.add(TEST_RECORD);

  // preserveOldProperties is how a caller edits one field without restating the
  // number. The stored cc-number is the mask, so the merge has to pick up the
  // decrypted number instead -- encrypting the mask would destroy the number.
  await adapter.update(guid, { "cc-name": "Jane Q. Doe" }, true);

  const updated = await adapter.get(guid);
  Assert.equal(updated["cc-name"], "Jane Q. Doe", "update persisted cc-name");
  Assert.equal(
    await storedNumber(store, guid),
    TEST_NUMBER,
    "the number survives an edit that did not restate it"
  );
  Assert.equal(
    updated["cc-number"],
    maskFor(TEST_NUMBER),
    "and the mask still shows the right last four"
  );
  // normalizeFields re-derives cc-type from the number, and the merged record
  // carries the decrypted one, so the type survives.
  Assert.equal(updated["cc-type"], "visa", "cc-type is not cleared");
  // The expiry does not, and this matches the JSON store rather than diverging
  // from it: normalizeFields writes "" into cc-exp-month and cc-exp-year for a
  // record that supplied neither, and "" is not undefined, so
  // preserveOldProperties never falls back to the stored value and the merge
  // drops the field. CreditCardsBase.update loses it the same way. Asserted so
  // the parity is pinned -- if the JSON store is ever fixed, this should fail.
  Assert.ok(
    !("cc-exp-month" in updated),
    "cc-exp-month is cleared, as it is in the JSON store"
  );
  Assert.ok(
    !("cc-exp-year" in updated),
    "cc-exp-year is cleared, as it is in the JSON store"
  );
});

add_task(async function test_update_after_a_scrub_does_not_store_the_mask() {
  const { store, adapter } = await newAdapter(
    "autofill-cc-adapter-scrubbed.sqlite"
  );
  const guid = await adapter.add(TEST_RECORD);

  // scrubEncryptedData() blanks cc_number_enc and leaves cc_number_last_4, so
  // the record still reads back with a mask but has no number behind it. A
  // merge that kept that mask would store it as the number.
  await store.scrubEncryptedData();

  Assert.equal(
    await storedNumber(store, guid),
    "",
    "the number is gone after a scrub"
  );
  const scrubbed = await adapter.get(guid);
  Assert.ok(scrubbed["cc-number"], "but the mask is still derived from last4");

  await Assert.rejects(
    adapter.update(guid, { "cc-name": "Jane Q. Doe" }, true),
    /Missing\/invalid cc-number/,
    "an update with no number to restore is refused rather than storing the mask"
  );
});

add_task(async function test_update_without_a_number_is_refused() {
  const { adapter } = await newAdapter("autofill-cc-adapter-nonumber.sqlite");
  const guid = await adapter.add(TEST_RECORD);

  // Without preserveOldProperties the merge takes every field from the caller,
  // and a masked cc-number does not validate, so nothing supplies a number.
  // CreditCardsBase.update refuses the same call for the same reason.
  await Assert.rejects(
    adapter.update(guid, {
      "cc-name": "Jane Q. Doe",
      "cc-number": (await adapter.get(guid))["cc-number"],
    }),
    /Missing\/invalid cc-number/,
    "an update that carries only the mask is refused"
  );
});

add_task(async function test_update_replacing_the_number() {
  const { store, adapter } = await newAdapter(
    "autofill-cc-adapter-renumber.sqlite"
  );
  const guid = await adapter.add(TEST_RECORD);

  const replacement = "5555555555554444";
  await adapter.update(guid, { ...TEST_RECORD, "cc-number": replacement });

  const updated = await adapter.get(guid);
  Assert.equal(
    await storedNumber(store, guid),
    replacement,
    "a restated number is re-encrypted"
  );
  Assert.equal(
    updated["cc-number"],
    maskFor(replacement),
    "and the mask follows the new number"
  );
  Assert.equal(updated["cc-type"], "mastercard", "cc-type follows the number");
});

add_task(async function test_shorter_number_reads_back_at_the_fixed_width() {
  const { store, adapter } = await newAdapter(
    "autofill-cc-adapter-amex.sqlite"
  );
  const guid = await adapter.add({
    ...TEST_RECORD,
    "cc-number": TEST_NUMBER_AMEX,
  });

  // Only the last four digits are stored, so the original length is gone and a
  // 15-digit card reads back one character wider than the JSON store wrote it.
  // Accepted, and the reason cc-number is not a stored field: a migration must
  // not read this as an unfaithful copy.
  const fetched = await adapter.get(guid);
  Assert.equal(
    fetched["cc-number"],
    maskFor(TEST_NUMBER_AMEX),
    "a 15-digit card is masked to the same width as a 16-digit one"
  );
  Assert.equal(
    fetched["cc-number"].length,
    16,
    "which is wider than the card it came from"
  );
  Assert.equal(fetched["cc-type"], "amex", "cc-type still detected");
  Assert.equal(
    await storedNumber(store, guid),
    TEST_NUMBER_AMEX,
    "and the number itself is unharmed"
  );
});

add_task(async function test_record_without_an_expiry() {
  const { adapter } = await newAdapter("autofill-cc-adapter-noexp.sqlite");
  const guid = await adapter.add({
    "cc-name": "Jane Doe",
    "cc-number": TEST_NUMBER,
  });

  // The expiry columns are non-optional i64, so an absent expiry is stored as
  // 0 and has to read back as absent rather than as month 0 of year 0.
  const fetched = await adapter.get(guid);
  Assert.ok(!("cc-exp-month" in fetched), "no cc-exp-month");
  Assert.ok(!("cc-exp-year" in fetched), "no cc-exp-year");
  Assert.ok(!("cc-exp" in fetched), "and nothing computed from them");
  Assert.equal(fetched["cc-name"], "Jane Doe", "the rest of the record stands");
});

add_task(async function test_security_code_round_trips() {
  enableSecurityCode();
  const { store, adapter } = await newAdapter("autofill-cc-adapter-csc.sqlite");
  const guid = await adapter.add({ ...TEST_RECORD, "cc-csc": "123" });
  Assert.equal(
    (await adapter.get(guid))["cc-csc"],
    "123",
    "the security code reads back in the clear"
  );
  Assert.equal(
    (await store.getCreditCard(guid)).ccCvv,
    "123",
    "and is what the store holds"
  );

  const without = await adapter.add({
    ...TEST_RECORD,
    "cc-number": TEST_NUMBER_AMEX,
    "cc-csc": "",
  });
  Assert.ok(
    !("cc-csc" in (await adapter.get(without))),
    "an empty code is stored as none"
  );

  await adapter.update(without, { "cc-csc": " 4567 " }, true);
  Assert.equal(
    (await adapter.get(without))["cc-csc"],
    "4567",
    "a code is trimmed"
  );
  await adapter.update(without, { "cc-csc": "12a" }, true);
  await adapter.update(without, { "cc-csc": "12345" }, true);
  Assert.equal(
    (await adapter.get(without))["cc-csc"],
    "4567",
    "and one that is not 3 or 4 digits is not written"
  );
});

add_task(async function test_update_and_the_security_code() {
  enableSecurityCode();
  const { adapter } = await newAdapter("autofill-cc-adapter-csc-update.sqlite");
  const guid = await adapter.add({ ...TEST_RECORD, "cc-csc": "123" });
  const code = async () => (await adapter.get(guid))["cc-csc"];

  await adapter.update(
    guid,
    { ...TEST_RECORD, "cc-name": "Jane Q. Doe" },
    true
  );
  Assert.equal(await code(), "123", "an omitted code is kept when preserving");

  await adapter.update(guid, { "cc-csc": "456" }, true);
  Assert.equal(await code(), "456", "a new code replaces it");

  await adapter.update(guid, { ...TEST_RECORD, "cc-csc": "" }, true);
  Assert.equal(await code(), undefined, "the empty string clears it");

  await adapter.update(guid, { ...TEST_RECORD, "cc-csc": "789" });
  await adapter.update(guid, TEST_RECORD);
  Assert.equal(
    await code(),
    "789",
    "a replacing update keeps an omitted code, as the JSON store does"
  );

  await adapter.update(guid, { ...TEST_RECORD, "cc-csc": "" });
  Assert.equal(await code(), undefined, "and only an empty one clears it");
});

add_task(async function test_a_match_compares_the_security_code() {
  enableSecurityCode();
  const { adapter } = await newAdapter("autofill-cc-adapter-csc-match.sqlite");
  const guid = await adapter.add({ ...TEST_RECORD, "cc-csc": "123" });
  const matching = async record =>
    (await collect(adapter.getMatchRecords(record))).map(r => r.guid);

  // A match means nothing new was submitted and no doorhanger is shown, so a
  // different code has to miss for the update doorhanger to offer saving it.
  Assert.deepEqual(
    await matching({ ...TEST_RECORD, "cc-csc": "999" }),
    [],
    "a different code is not a match"
  );
  Assert.deepEqual(
    await matching({ ...TEST_RECORD, "cc-csc": "123" }),
    [guid],
    "the same code is"
  );
  Assert.deepEqual(
    await matching(TEST_RECORD),
    [guid],
    "and so is a submission without a code"
  );
});

add_task(async function test_security_code_while_disabled() {
  const { store, adapter } = await newAdapter(
    "autofill-cc-adapter-csc-disabled.sqlite"
  );
  enableSecurityCode();
  const guid = await adapter.add({ ...TEST_RECORD, "cc-csc": "123" });
  Services.prefs.setStringPref(CVV_SUPPORTED_PREF, "off");
  const storedCode = async () => (await store.getCreditCard(guid)).ccCvv;

  Assert.ok(
    !("cc-csc" in (await adapter.get(guid))),
    "get() hides the stored code"
  );
  Assert.ok(!("cc-csc" in (await adapter.getAll())[0]), "and so does getAll()");
  Assert.ok(
    (await adapter.getSavedFieldNames()).has("cc-csc"),
    "getSavedFieldNames still reports it, as what the store holds"
  );
  Assert.equal(
    (await adapter.get(guid, { rawData: true }))["cc-csc"],
    "123",
    "a raw read still reports it"
  );

  await adapter.update(guid, { ...TEST_RECORD, "cc-name": "Jane Q. Doe" });
  Assert.equal(
    await storedCode(),
    "123",
    "a replacing update without a code keeps the stored one"
  );
  await adapter.update(guid, { ...TEST_RECORD, "cc-csc": "" });
  await adapter.update(guid, { ...TEST_RECORD, "cc-csc": "456" }, true);
  Assert.equal(await storedCode(), "123", "nor can an update change it");

  const added = await adapter.add({
    ...TEST_RECORD,
    "cc-number": TEST_NUMBER_AMEX,
    "cc-csc": "789",
  });
  Assert.equal(
    (await store.getCreditCard(added)).ccCvv,
    null,
    "add() does not store a code"
  );
  Services.prefs.clearUserPref(CVV_SUPPORTED_PREF);
});

add_task(async function test_add_rejects_a_record_without_a_valid_number() {
  const { adapter } = await newAdapter("autofill-cc-adapter-invalid.sqlite");

  await Assert.rejects(
    adapter.add({ "cc-name": "Jane Doe", "cc-exp-month": 4 }),
    /Missing\/invalid cc-number/,
    "a record with no number is refused"
  );
  await Assert.rejects(
    adapter.add({ ...TEST_RECORD, "cc-number": "1234" }),
    /Missing\/invalid cc-number/,
    "a record whose number does not validate is refused"
  );
});

add_task(async function test_billing_address_guid_does_not_survive() {
  const { adapter } = await newAdapter("autofill-cc-adapter-billing.sqlite");

  // The Rust credit card schema has no column for it, so it is not in
  // JS_TO_RUST_FIELD and the write drops it. Asserted so that the day
  // application-services grows the column, this test is what says so.
  const guid = await adapter.add({
    ...TEST_RECORD,
    billingAddressGUID: "SomeAddressGuid",
  });
  Assert.ok(
    !("billingAddressGUID" in (await adapter.get(guid))),
    "billingAddressGUID is absent from the stored record"
  );
});

add_task(async function test_add_many_with_meta_bulk_import() {
  const { store, adapter } = await newAdapter(
    "autofill-cc-adapter-bulk.sqlite"
  );

  const records = [
    {
      ...TEST_RECORD,
      guid: "BulkGuid0001",
      timeCreated: 1600000000000,
      timeLastModified: 1600000000000,
      timesUsed: 1,
    },
    {
      ...TEST_RECORD,
      "cc-name": "John Roe",
      guid: "BulkGuid0002",
      timeCreated: 1600000002000,
      timeLastModified: 1600000002000,
      timesUsed: 5,
    },
  ];

  const results = await adapter.addManyWithMeta(records);
  Assert.equal(results.length, 2, "one result per input record");
  Assert.deepEqual(
    results.map(r => r.guid),
    ["BulkGuid0001", "BulkGuid0002"],
    "each result reports its preserved guid, in order"
  );
  Assert.ok(
    results.every(r => !r.error),
    "no per-record errors for valid records"
  );

  const all = await adapter.getAll();
  Assert.equal(all.length, 2, "both records imported");
  const byGuid = Object.fromEntries(all.map(r => [r.guid, r]));
  Assert.equal(byGuid.BulkGuid0002["cc-name"], "John Roe", "fields preserved");
  Assert.equal(byGuid.BulkGuid0002.timesUsed, 5, "timesUsed preserved");
  Assert.equal(
    byGuid.BulkGuid0001.timeCreated,
    1600000000000,
    "timeCreated preserved"
  );
  Assert.equal(
    byGuid.BulkGuid0001.timeLastModified,
    1600000000000,
    "timeLastModified preserved, rather than stamped with now"
  );
  // The number arrives in the clear and is encrypted on the way in, so what is
  // stored is this store's own ciphertext rather than the caller's.
  Assert.equal(
    await storedNumber(store, "BulkGuid0001"),
    TEST_NUMBER,
    "and the number was encrypted by this store"
  );
});

add_task(async function test_add_many_with_meta_isolates_per_record_failure() {
  const { adapter } = await newAdapter("autofill-cc-adapter-bulkfail.sqlite");

  // Two records sharing a guid: the second insert collides and must be
  // reported as an error without aborting the good one, and without leaving a
  // half-written row behind -- which is what the store's savepoint is for.
  const results = await adapter.addManyWithMeta([
    {
      ...TEST_RECORD,
      guid: "DupGuid00001",
      timeCreated: 1,
      timeLastModified: 1,
    },
    {
      ...TEST_RECORD,
      guid: "DupGuid00001",
      timeCreated: 2,
      timeLastModified: 2,
    },
  ]);

  Assert.equal(results.length, 2, "one result per input record");
  Assert.ok(!results[0].error, "first record succeeds");
  Assert.ok(
    results[1].error,
    "second (duplicate guid) record reports an error"
  );
  Assert.equal(
    (await adapter.getAll()).length,
    1,
    "the good record is still persisted despite the sibling failure"
  );
});

add_task(async function test_update_many_with_meta_replaces_metadata() {
  const { adapter } = await newAdapter("autofill-cc-adapter-updmany.sqlite");
  await adapter.addManyWithMeta([
    {
      ...TEST_RECORD,
      guid: "UpdGuid00001",
      timeCreated: 1,
      timeLastModified: 1,
    },
  ]);

  // Unlike update(), the metadata comes from the record rather than the store:
  // this is replicating another store, not recording an edit.
  const results = await adapter.updateManyWithMeta([
    {
      ...TEST_RECORD,
      "cc-name": "Renamed",
      guid: "UpdGuid00001",
      timeCreated: 1,
      timeLastModified: 999,
      timesUsed: 7,
    },
  ]);
  Assert.deepEqual(results, [{ guid: "UpdGuid00001" }], "reports the guid");

  const updated = await adapter.get("UpdGuid00001");
  Assert.equal(updated["cc-name"], "Renamed", "fields replaced");
  Assert.equal(
    updated.timeLastModified,
    999,
    "timeLastModified taken as given"
  );
  Assert.equal(updated.timesUsed, 7, "timesUsed taken as given");

  // A guid the store does not hold is an error, not an insert: the caller is
  // replicating, so a disagreement has to be reported rather than repaired.
  const missing = await adapter.updateManyWithMeta([
    { ...TEST_RECORD, guid: "NoSuchGuid01" },
  ]);
  Assert.ok(missing[0].error, "a missing guid reports an error");
  Assert.equal((await adapter.getAll()).length, 1, "and inserts nothing");
});

add_task(async function test_remove_many_and_tombstones() {
  const { adapter } = await newAdapter("autofill-cc-adapter-rmmany.sqlite");
  await adapter.addManyWithMeta([
    {
      ...TEST_RECORD,
      guid: "RmGuid000001",
      timeCreated: 1,
      timeLastModified: 1,
    },
    {
      ...TEST_RECORD,
      guid: "RmGuid000002",
      timeCreated: 1,
      timeLastModified: 1,
    },
  ]);

  const removed = await adapter.removeMany(["RmGuid000001"]);
  Assert.deepEqual(removed, [{ guid: "RmGuid000001" }], "reports the guid");
  Assert.equal((await adapter.getAll()).length, 1, "only that one is gone");

  // Tombstones for records deleted elsewhere and not yet uploaded. A guid the
  // store still holds is refused, and must not take the batch down with it.
  const results = await adapter.addManyTombstones([
    { guid: "RmGuid000002", timeDeleted: 1234 },
    { guid: "GoneGuid0001", timeDeleted: 5678 },
  ]);
  Assert.equal(results.length, 2, "one result per tombstone");
  Assert.ok(results[0].error, "a guid the store still holds is refused");
  Assert.deepEqual(results[1], { guid: "GoneGuid0001" }, "the other is taken");
  Assert.equal(
    (await adapter.getAll()).length,
    1,
    "and the live record is untouched"
  );

  // wipe() clears the tombstones too, so the same guids can be re-imported.
  await adapter.wipe();
  const reimported = await adapter.addManyWithMeta([
    {
      ...TEST_RECORD,
      guid: "GoneGuid0001",
      timeCreated: 1,
      timeLastModified: 1,
    },
  ]);
  Assert.ok(!reimported[0].error, "a wiped tombstone no longer blocks a guid");
});

add_task(async function test_record_for_migration_export() {
  const { adapter } = await newAdapter("autofill-cc-adapter-export.sqlite");
  const guid = await adapter.add(TEST_RECORD);

  const stored = await adapter.get(guid);
  const exported = await adapter._recordForMigrationExport(stored);

  // The receiving store encrypts under its own key, so the number crosses in
  // the clear.
  Assert.equal(
    exported["cc-number"],
    TEST_NUMBER,
    "the number is handed over in the clear"
  );
  Assert.equal(exported.guid, guid, "the rest of the record comes along");
  Assert.equal(exported["cc-name"], "Jane Doe", "including the name");

  // A copy, so the caller is still holding the record it read.
  Assert.equal(
    stored["cc-number"],
    maskFor(TEST_NUMBER),
    "the record passed in keeps its mask"
  );
});

add_task(async function test_a_number_another_key_wrote_fails_the_read() {
  // Two stores over one file, each with a key of its own: the second holds a
  // card it cannot read, as a store does whose key changed under it.
  const path = FileTestUtils.getTempFile(
    "autofill-cc-adapter-exportbad.sqlite"
  ).path;
  await initRustComponents(PathUtils.profileDir);
  const writer = new RustAutofillCreditCardsAdapter(
    await createAutofillStoreWithStaticKeyManager(path, createAutofillKey())
  );
  const guid = await writer.add(TEST_RECORD);
  const reader = new RustAutofillCreditCardsAdapter(
    await createAutofillStoreWithStaticKeyManager(path, createAutofillKey())
  );

  // The token is unlocked, so the failure is the data's own rather than a
  // declined prompt, and it reaches the caller as it is.
  await Assert.rejects(
    reader.get(guid),
    e => e instanceof CryptoError,
    "a card another key wrote fails the read with the store's error"
  );
  Assert.equal(
    await storedNumber(await writer._store(), guid),
    TEST_NUMBER,
    "and its number is left in place for the key that wrote it"
  );
});

add_task(async function test_migration_export_refuses_a_scrubbed_record() {
  const { store, adapter } = await newAdapter(
    "autofill-cc-adapter-exportscrub.sqlite"
  );
  const guid = await adapter.add(TEST_RECORD);

  // A scrub blanks cc_number_enc and leaves cc_number_last_4, so get() still
  // derives a mask but there is no number behind it. Handing that mask
  // over would have the receiving store encrypt it in place of the card, so
  // the export refuses it the same way it refuses a number that will not
  // decrypt.
  await store.scrubEncryptedData();
  const scrubbed = await adapter.get(guid);
  Assert.equal(
    scrubbed["cc-number"],
    maskFor(TEST_NUMBER),
    "the scrubbed record still reads back with a mask"
  );

  await Assert.rejects(
    adapter._recordForMigrationExport(scrubbed),
    /Got a masked cc-number when exporting/,
    "which the export refuses rather than handing over"
  );
});

add_task(async function test_migration_export_allows_a_record_with_no_number() {
  const { adapter } = await newAdapter("autofill-cc-adapter-exportnone.sqlite");

  // No number and no mask either: nothing was lost, so there is nothing to
  // refuse. This is what keeps the guard above from failing a whole migration
  // over a record that never carried a number.
  const exported = await adapter._recordForMigrationExport({
    guid: "NoNumber0001",
    "cc-name": "Jane Doe",
  });
  Assert.equal(exported["cc-name"], "Jane Doe", "the record crosses unchanged");
  Assert.ok(!("cc-number" in exported), "still with no number");
});

add_task(async function test_bulk_import_refuses_a_masked_number() {
  const { adapter } = await newAdapter("autofill-cc-adapter-bulkmask.sqlite");

  // addManyWithMeta does not go through _validateRecord, so the mask is
  // refused in _fieldsWithMeta instead. A migration exports before it copies,
  // so this should be unreachable -- but storing a mask in place of the card
  // cannot be undone, and failing the run leaves the profile where it is.
  await Assert.rejects(
    adapter.addManyWithMeta([
      {
        ...TEST_RECORD,
        guid: "MaskGuid0001",
        "cc-number": maskFor(TEST_NUMBER),
        timeCreated: 1,
        timeLastModified: 1,
      },
    ]),
    /Got a masked cc-number when writing/,
    "the batch fails rather than storing the mask as the card number"
  );
  Assert.equal((await adapter.getAll()).length, 0, "and nothing was written");
});

add_task(async function test_update_cannot_store_a_mask_as_the_number() {
  const { store } = await newAdapter("autofill-cc-adapter-maskguard.sqlite");

  // A subclass that does not restore the number must still be unable to store
  // the mask as the card. The check lives where the invariant is asserted, not
  // where it is prepared, so it does not depend on the hook having run.
  class NoRestore extends RustAutofillCreditCardsAdapter {
    async _prepareStoredForMerge() {}
  }

  const adapter = new NoRestore(store);
  const guid = await adapter.add(TEST_RECORD);
  await Assert.rejects(
    adapter.update(guid, { "cc-name": "Jane Q. Doe" }, true),
    /Got a masked cc-number when writing/,
    "a masked number is refused rather than stored as the card"
  );
  Assert.equal(
    await storedNumber(store, guid),
    TEST_NUMBER,
    "and the stored number is untouched"
  );
});

add_task(async function test_get_instance_is_memoized_per_collection() {
  const sentinel = {};
  RustAutofillCreditCardsAdapter._instance = sentinel;
  Assert.equal(
    RustAutofillCreditCardsAdapter.getInstance(),
    sentinel,
    "getInstance hands back the adapter it already built"
  );
  // The base holding none is what keeps two collections from sharing one.
  Assert.equal(
    RustAutofillAdapterBase._instance,
    undefined,
    "the memoized adapter is stored on the subclass, not the base"
  );
  RustAutofillCreditCardsAdapter._instance = null;
});

add_task(async function test_subclass_must_define_a_data_type_getter() {
  class NoDataType extends RustAutofillAdapterBase {}
  Assert.throws(
    () => new NoDataType(Promise.resolve({})),
    /_dataType getter/,
    "the constructor refuses a subclass with no _dataType, rather than failing later"
  );
});

add_task(async function test_get_distinguishes_missing_from_unreadable() {
  const { adapter } = await newAdapter("autofill-cc-adapter-get.sqlite");

  Assert.equal(
    await adapter.get("NoSuchGuid01"),
    null,
    "a guid the store does not hold reads as null"
  );

  // A store that cannot be read must not answer "no such record": callers take
  // that as an empty slot, open the edit dialog blank as though the user were
  // creating a card, and saving writes a second record on top of the one that
  // could not be read.
  const unreadable = new RustAutofillCreditCardsAdapter({
    getCreditCard: () => Promise.reject(new Error("database is locked")),
  });
  await Assert.rejects(
    unreadable.get("AnyGuid00001"),
    /database is locked/,
    "a failure to read propagates rather than reading as not-found"
  );
});

add_task(async function test_a_declined_write_reports_abort() {
  const { store, token, adapter } = await newAdapter(
    "autofill-cc-adapter-declined-write.sqlite"
  );

  // Writing unwraps the key as much as reading does, so a save is something
  // the user can decline.
  const broken = breakEncryption(store, token, { canceled: true });
  let error;
  try {
    error = await adapter.add(TEST_RECORD).then(
      () => null,
      err => err
    );
  } finally {
    broken.restore();
  }

  Assert.equal(
    error.result,
    Cr.NS_ERROR_ABORT,
    "a save the user declined reports NS_ERROR_ABORT, not a crypto error"
  );
});

add_task(async function test_one_decline_answers_for_every_read_waiting() {
  const { store, token, adapter } = await newAdapter(
    "autofill-cc-adapter-concurrent.sqlite"
  );
  const guid = await adapter.add(TEST_RECORD);
  const stored = await adapter.get(guid);

  // Two reads can be waiting on one prompt. Declining it locks the token for
  // both, and the store tells neither why its card came back without a
  // number, so both owe NS_ERROR_ABORT -- a record of the decline that the
  // first reader consumed would leave the second reporting its failure as
  // unreadable data.
  const broken = breakDecryption(store, token, { canceled: true });
  try {
    const errors = await Promise.all(
      [1, 2].map(() =>
        adapter.decryptField(stored, "cc-number").then(
          () => null,
          e => e
        )
      )
    );
    Assert.equal(broken.attempts, 2, "both reads reached the store");
    Assert.deepEqual(
      errors.map(e => e?.result),
      [Cr.NS_ERROR_ABORT, Cr.NS_ERROR_ABORT],
      "and both report the declined prompt rather than only the first"
    );
  } finally {
    broken.restore();
  }
});

add_task(async function test_an_unavailable_key_stops_the_duplicate_search() {
  const { store, token, adapter } = await newAdapter(
    "autofill-cc-adapter-locked.sqlite"
  );
  await adapter.add(TEST_RECORD);
  await adapter.add({ ...TEST_RECORD, "cc-number": TEST_NUMBER_AMEX });

  // A key the user declined to unlock is not one record's problem: carrying on
  // would compare none of the cards and report no duplicates.
  const broken = breakDecryption(store, token, { canceled: true });
  try {
    await Assert.rejects(
      collect(adapter.getDuplicateRecords(TEST_RECORD)),
      /Primary password locked/,
      "the search gives up rather than reporting no duplicates"
    );
  } finally {
    broken.restore();
  }
  Assert.equal(broken.attempts, 1, "and read the store once");
});

add_task(async function test_a_declined_primary_password_reports_abort() {
  const { store, token, adapter } = await newAdapter(
    "autofill-cc-adapter-prp.sqlite"
  );
  const guid = await adapter.add(TEST_RECORD);

  // Callers tell a declined prompt from unreadable data by NS_ERROR_ABORT,
  // which is what crypto-SDR and the JSON store report, so a card the store
  // hands over without its number has to be split back into the two.
  let broken = breakDecryption(store, token, { canceled: true });
  try {
    const e = await adapter
      .update(guid, { "cc-name": "Jane Q. Doe" }, true)
      .then(
        () => null,
        err => err
      );
    Assert.equal(
      e.result,
      Cr.NS_ERROR_ABORT,
      "an update that needed the stored number reports NS_ERROR_ABORT"
    );
  } finally {
    broken.restore();
  }
  Assert.equal(
    await storedNumber(store, guid),
    TEST_NUMBER,
    "so the number is still there once the token is unlocked"
  );

  // The same failure without a decline behind it is the record's own, and is
  // not reported as one.
  broken = breakDecryption(store, token, { canceled: false });
  try {
    await Assert.rejects(
      adapter.update(guid, TEST_RECORD, true),
      e => e instanceof CryptoError,
      "unreadable data fails the update with the store's error"
    );
  } finally {
    broken.restore();
  }
  Assert.equal(
    await storedNumber(store, guid),
    TEST_NUMBER,
    "and leaves the stored number alone"
  );
});

add_task(
  async function test_a_card_whose_number_cannot_be_read_is_passed_over() {
    const { store, adapter } = await newAdapter(
      "autofill-cc-adapter-unread.sqlite"
    );
    const guid = await adapter.add(TEST_RECORD);
    await store.scrubEncryptedData();

    // A submission has to keep working when the store holds a card that cannot
    // be decrypted: the scrubbed card is skipped rather than throwing.
    Assert.deepEqual(
      await collect(adapter.getDuplicateRecords(TEST_RECORD)),
      [],
      "the scrubbed card is not offered as a duplicate"
    );
    Assert.ok(await adapter.get(guid), "and it is still there");
  }
);

add_task(async function test_saved_field_names_never_prompt() {
  const { store, token, adapter } = await newAdapter(
    "autofill-cc-adapter-fieldnames.sqlite"
  );
  await adapter.add(TEST_RECORD);
  let prompts = 0;
  const realAllowingPrompt = adapter._allowingPrompt.bind(adapter);
  adapter._allowingPrompt = operation => {
    prompts++;
    return realAllowingPrompt(operation);
  };

  // No card can be read while the token is locked, so every field is reported,
  // and the form that offers them is what asks for the password.
  const broken = breakDecryption(store, token, { canceled: true });
  try {
    const names = await adapter.getSavedFieldNames();
    for (const field of ["cc-name", "cc-number", "cc-exp", "cc-given-name"]) {
      Assert.ok(names.has(field), `${field} is reported while locked`);
    }
    Assert.equal(prompts, 0, "without the read being allowed to prompt");
  } finally {
    broken.restore();
  }

  await adapter.remove((await adapter.getAll())[0].guid);
  const emptyBroken = breakDecryption(store, token, { canceled: true });
  try {
    Assert.deepEqual(
      [...(await adapter.getSavedFieldNames())],
      [],
      "with no cards, nothing is reported"
    );
  } finally {
    emptyBroken.restore();
  }
});

add_task(async function test_a_declined_os_prompt_reports_abort() {
  const { adapter } = await newAdapter("autofill-cc-adapter-osauth.sqlite");
  await adapter.add(TEST_RECORD);
  const stored = (await adapter.getAll())[0];

  // OS re-authentication is a step of its own here rather than something the
  // decryption does on the way past, so declining it has to fail the same way
  // OSKeyStore fails: NS_ERROR_ABORT, and no number handed back.
  // head.js declares FormAutofillUtils but only assigns it once profile
  // storage has been initialised, which this test does not do.
  const { FormAutofillUtils: utils } = ChromeUtils.importESModule(
    "resource://gre/modules/shared/FormAutofillUtils.sys.mjs"
  );
  const realVerify = utils.verifyUserOSAuth;
  let generateKeyIfNotAvailable = true;
  utils.verifyUserOSAuth = async (...args) => {
    generateKeyIfNotAvailable = args[4];
    return false;
  };
  try {
    const e = await adapter
      .decryptField(stored, "cc-number", { reauth: "unlock to continue" })
      .then(
        () => null,
        err => err
      );
    Assert.equal(e.result, Cr.NS_ERROR_ABORT, "a declined prompt aborts");
  } finally {
    utils.verifyUserOSAuth = realVerify;
  }
  Assert.equal(
    generateKeyIfNotAvailable,
    false,
    "and the prompt is not allowed to create an OS key store key this " +
      "store would never read"
  );

  // A record holding nothing under that field is not worth a prompt.
  Assert.equal(
    await adapter.decryptField({}, "cc-number", { reauth: "unlock" }),
    null,
    "a record with no number reads as null without asking"
  );
  await Assert.rejects(
    adapter.decryptField(stored, "cc-name"),
    /not stored encrypted/,
    "and a field this store does not keep encrypted is a programming error"
  );
});

function storeErrors() {
  return (Glean.formautofillCreditcards.storeError.testGetValue() ?? []).map(
    e => e.extra
  );
}

// Fails writes, and reads unless `reads` is false, with an unlocked token.
function failStore(store, error, { reads = true } = {}) {
  const fail = async () => {
    throw error;
  };
  store.addCreditCard = store.updateCreditCard = fail;
  if (reads) {
    store.getCreditCard = store.getAllCreditCards = fail;
  }
  return {
    restore() {
      delete store.addCreditCard;
      delete store.updateCreditCard;
      delete store.getCreditCard;
      delete store.getAllCreditCards;
    },
  };
}

add_task(async function test_store_error_on_write() {
  enableSecurityCode();
  const { store, token, adapter } = await newAdapter(
    "autofill-cc-adapter-store-error-write.sqlite"
  );
  const withCode = await adapter.add({ ...TEST_RECORD, "cc-csc": "123" });
  const withoutCode = await adapter.add({
    ...TEST_RECORD,
    "cc-number": TEST_NUMBER_AMEX,
  });
  const attempt = promise => promise.catch(() => {});
  const crypto = (operation, has_cvv) => ({
    operation,
    error_type: "CryptoError",
    has_cvv: String(has_cvv),
  });

  Services.fog.testResetFOG();
  let broken = failStore(store, new CryptoError("encryption failed"), {
    reads: false,
  });
  try {
    await attempt(adapter.add({ ...TEST_RECORD, "cc-csc": "456" }));
    await attempt(adapter.add(TEST_RECORD));
    await attempt(adapter.update(withoutCode, { "cc-csc": "789" }, true));
    await attempt(adapter.update(withoutCode, { "cc-name": "Jo Doe" }, true));
    await attempt(adapter.update(withCode, { "cc-name": "Jo Doe" }, true));
    await attempt(adapter.update(withCode, { "cc-csc": "" }, true));
  } finally {
    broken.restore();
  }
  Assert.deepEqual(
    storeErrors(),
    [
      crypto("add", true),
      crypto("add", false),
      crypto("update", true),
      crypto("update", false),
      crypto("update", true),
      crypto("clear", true),
    ],
    "every failed write is recorded, with whether it involved a code"
  );

  Services.fog.testResetFOG();
  broken = failStore(store, new Error("bad"), { reads: false });
  try {
    await attempt(adapter.add(TEST_RECORD));
  } finally {
    broken.restore();
  }
  Assert.deepEqual(
    storeErrors(),
    [{ operation: "add", error_type: "Unknown", has_cvv: "false" }],
    "an error the store did not raise is Unknown"
  );

  Services.fog.testResetFOG();
  broken = breakEncryption(store, token, { canceled: true });
  try {
    await attempt(adapter.add({ ...TEST_RECORD, "cc-csc": "456" }));
  } finally {
    broken.restore();
  }
  Assert.deepEqual(
    storeErrors(),
    [
      {
        operation: "add",
        error_type: "PrimaryPasswordLocked",
        has_cvv: "true",
      },
    ],
    "a declined primary password is recorded as its own category"
  );
});

add_task(async function test_store_error_on_read() {
  enableSecurityCode();
  const { store, token, adapter } = await newAdapter(
    "autofill-cc-adapter-store-error-read.sqlite"
  );
  const withCode = await adapter.add({ ...TEST_RECORD, "cc-csc": "123" });
  const withoutCode = await adapter.add({
    ...TEST_RECORD,
    "cc-number": TEST_NUMBER_AMEX,
  });
  const attempt = promise => promise.catch(() => {});
  const read = has_cvv => ({
    operation: "read",
    error_type: "CryptoError",
    has_cvv: String(has_cvv),
  });

  Services.fog.testResetFOG();
  let broken = failStore(store, new CryptoError("decryption failed"));
  try {
    await attempt(adapter.get(withCode));
    await attempt(adapter.get(withoutCode));
    await attempt(adapter.getAll());
  } finally {
    broken.restore();
  }
  Assert.deepEqual(
    storeErrors(),
    [read(true), read(false), read(true)],
    "every failed read is recorded, with whether a card it covered has a code"
  );

  Services.fog.testResetFOG();
  broken = failStore(store, new CryptoError("decryption failed"));
  store.listCreditCardCandidates = async () => {
    throw new Error("cannot list");
  };
  try {
    await attempt(adapter.get(withCode));
  } finally {
    broken.restore();
    delete store.listCreditCardCandidates;
  }
  Assert.deepEqual(
    storeErrors(),
    [{ operation: "read", error_type: "CryptoError" }],
    "has_cvv is left out when the read cannot tell"
  );

  Services.fog.testResetFOG();
  broken = breakDecryption(store, token, { canceled: true });
  try {
    await attempt(adapter.get(withCode));
  } finally {
    broken.restore();
  }
  Assert.deepEqual(
    storeErrors(),
    [
      {
        operation: "read",
        error_type: "PrimaryPasswordLocked",
        has_cvv: "true",
      },
    ],
    "a locked primary password is recorded as its own category"
  );

  Services.fog.testResetFOG();
  Assert.equal(await adapter.get("NoSuchGuid01"), null);
  Assert.deepEqual(storeErrors(), [], "nor a missing card");
});

add_task(async function test_store_error_for_a_card_another_key_wrote() {
  enableSecurityCode();
  const path = FileTestUtils.getTempFile(
    "autofill-cc-adapter-store-error-key.sqlite"
  ).path;
  await initRustComponents(PathUtils.profileDir);
  const writer = new RustAutofillCreditCardsAdapter(
    await createAutofillStoreWithStaticKeyManager(path, createAutofillKey())
  );
  const guid = await writer.add({ ...TEST_RECORD, "cc-csc": "123" });
  const reader = new RustAutofillCreditCardsAdapter(
    await createAutofillStoreWithStaticKeyManager(path, createAutofillKey())
  );
  reader._primaryPasswordLocked = () => false;

  Services.fog.testResetFOG();
  await Assert.rejects(reader.get(guid), e => e instanceof CryptoError);
  Assert.deepEqual(
    storeErrors(),
    [{ operation: "read", error_type: "CryptoError", has_cvv: "true" }],
    "a card the store cannot decrypt is recorded"
  );
});

add_task(async function test_lone_surrogates_are_refused() {
  const { adapter } = await newAdapter("autofill-cc-adapter-surrogates.sqlite");

  await Assert.rejects(
    adapter.add({ ...TEST_RECORD, "cc-name": "Jane Doe\uD83D" }),
    /"cc-name" contains lone surrogates/,
    "a record with a lone surrogate is not added"
  );
  Assert.equal((await adapter.getAll()).length, 0, "nothing is stored");

  const guid = await adapter.add(TEST_RECORD);
  await Assert.rejects(
    adapter.update(guid, { "cc-name": "\uDE00Jane Doe" }, true),
    /"cc-name" contains lone surrogates/,
    "an update with a lone surrogate is refused"
  );
  Assert.equal(
    (await adapter.get(guid))["cc-name"],
    TEST_RECORD["cc-name"],
    "the stored record is unchanged"
  );
});
