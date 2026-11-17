/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * The one-time JSON->Rust credit card migration, and what it gates.
 *
 * Same shape as the address migration: `storage.rust.enabled` bulk-copies the
 * pre-existing JSON cards into Rust once, preserving guids, timestamps and sync
 * metadata, and `storage.rust.active` latches the success.
 *
 * What is specific to cards is the number. Each store holds its own key -- the
 * JSON store the OS key store's, the Rust store one of its own in NSS -- so the
 * copy reads the number in the clear out of one and hands it to the other:
 * decrypt then encrypt, the shape LoginStorageMigrator uses. That needs the
 * source's key readable, which is settled once before any copying starts, and
 * it means a card whose number will not decrypt must be refused rather than
 * copied without it.
 */

const { FormAutofillStorage, formAutofillStorage, MIGRATION_SAFE_POINT_TOPIC } =
  ChromeUtils.importESModule("resource://autofill/FormAutofillStorage.sys.mjs");
const { RustAutofillCreditCardsAdapter } = ChromeUtils.importESModule(
  "resource://autofill/RustAutofillCreditCardStorage.sys.mjs"
);
const { CryptoError } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs"
);
const { OSKeyStore } = ChromeUtils.importESModule(
  "resource://gre/modules/OSKeyStore.sys.mjs"
);
const { RustAutofillStore } = ChromeUtils.importESModule(
  "resource://autofill/RustAutofillStore.sys.mjs"
);
const { MAX_ATTEMPTS } = ChromeUtils.importESModule(
  "resource://autofill/AutofillStorageMigrator.sys.mjs"
);

// See the address migration test: the per-process storage singleton would
// migrate its own empty store alongside the throwaway ones here.
add_setup(async function () {
  try {
    await formAutofillStorage._finalize();
  } catch (_) {}
  formAutofillStorage.initialize = () => Promise.resolve();
  const noFieldNames = { getSavedFieldNames: async () => new Set() };
  for (const collection of ["addresses", "creditCards"]) {
    Object.defineProperty(formAutofillStorage, collection, {
      configurable: true,
      get: () => noFieldNames,
    });
  }
});

const ENABLED_PREF = "extensions.formautofill.creditCards.storage.rust.enabled";
const ACTIVE_PREF = "extensions.formautofill.creditCards.storage.rust.active";
const TEST_MODE_PREF =
  "extensions.formautofill.creditCards.storage.rust.runMigrationTest";
const TEST_VERSION_PREF =
  "extensions.formautofill.creditCards.storage.rust.migrationTestVersion";
const ATTEMPTS_PREF =
  "extensions.formautofill.creditCards.storage.rust.migrationAttempts";
const TEST_ATTEMPTS_PREF =
  "extensions.formautofill.creditCards.storage.rust.migrationTestAttempts";

const CVV_SUPPORTED_PREF = "extensions.formautofill.creditCards.cvv.supported";
const CVV_ENABLED_PREF = "extensions.formautofill.creditCards.cvv.enabled";

const getBool = (pref, def = false) => Services.prefs.getBoolPref(pref, def);

const getInt = (pref, def = 0) => Services.prefs.getIntPref(pref, def);

function setSecurityCodeEnabled(enabled) {
  if (enabled) {
    Services.prefs.setStringPref(CVV_SUPPORTED_PREF, "on");
    Services.prefs.setBoolPref(CVV_ENABLED_PREF, true);
  } else {
    Services.prefs.clearUserPref(CVV_SUPPORTED_PREF);
    Services.prefs.clearUserPref(CVV_ENABLED_PREF);
  }
}

registerCleanupFunction(() => {
  for (const pref of [
    ENABLED_PREF,
    ACTIVE_PREF,
    TEST_MODE_PREF,
    TEST_VERSION_PREF,
    TEST_ATTEMPTS_PREF,
    ATTEMPTS_PREF,
    CVV_SUPPORTED_PREF,
    CVV_ENABLED_PREF,
  ]) {
    Services.prefs.clearUserPref(pref);
  }
});

const NUMBERS = {
  "Ann One": "4111111111111111",
  "Bob Two": "5555555555554444",
  // 15 digits, so the mask this store rebuilds is wider than the card.
  "Cy Three": "378282246310005",
};

function card(name) {
  return {
    "cc-name": name,
    "cc-number": NUMBERS[name],
    "cc-exp-month": 4,
    "cc-exp-year": 2030,
  };
}

// Each store holds its own key: the JSON store's records carry ciphertext the
// OS key store reads back, while the Rust store's carry a mask and the store
// hands the number over itself.
const decryptNumber = cipher => OSKeyStore.decrypt(cipher, "formautofill_cc");

const rustStore = () => new RustAutofillStore().ensureOpen();

const rustNumber = async guid =>
  (await (await rustStore()).getCreditCard(guid)).ccNumber;

async function setupStorageWithCards(fileName, names) {
  Services.prefs.setBoolPref(ENABLED_PREF, false);
  Services.prefs.setBoolPref(TEST_MODE_PREF, false);
  Services.prefs.clearUserPref(ACTIVE_PREF);
  Services.prefs.clearUserPref(TEST_VERSION_PREF);
  Services.prefs.clearUserPref(TEST_ATTEMPTS_PREF);
  Services.prefs.clearUserPref(ATTEMPTS_PREF);
  Services.fog.testResetFOG();
  RustAutofillCreditCardsAdapter._instance = null;

  const jsonPath = FileTestUtils.getTempFile(fileName).path;
  const s = new FormAutofillStorage(jsonPath);
  await s.initialize();
  Assert.ok(!getBool(ACTIVE_PREF), "JSON is serving while the pref is off");

  const guids = [];
  for (const name of names) {
    guids.push(await s.creditCards.add(card(name)));
  }
  return { s, guids };
}

async function restart(s, prefs = {}) {
  await s._finalize();
  for (const [pref, value] of Object.entries(prefs)) {
    Services.prefs.setBoolPref(pref, value);
  }
  const next = new FormAutofillStorage(s._path);
  await next.initialize();
  return next;
}

const enableRust = s => restart(s, { [ENABLED_PREF]: true });
const disableRust = s => restart(s, { [ENABLED_PREF]: false });

const migrationEvents = () =>
  Glean.formautofillCreditcards.migrateToRust.testGetValue() ?? [];

const divergenceEvents = () =>
  Glean.formautofillCreditcards.migrateRecordDivergence.testGetValue() ?? [];

add_task(async function test_migration_populates_rust() {
  let { s, guids } = await setupStorageWithCards("cc-mig-pre.json", [
    "Ann One",
    "Bob Two",
    "Cy Three",
  ]);

  Assert.ok(!getBool(ACTIVE_PREF), "not migrated yet");

  s = await enableRust(s);
  Assert.ok(getBool(ACTIVE_PREF), "rust.active set on success");

  const rust = s.creditCards;
  Assert.equal((await rust.getAll()).length, 3, "all 3 cards copied");
  for (const guid of guids) {
    Assert.ok(await rust.get(guid), `card ${guid} migrated with guid parity`);
  }

  const events = migrationEvents();
  Assert.equal(events.length, 1, "one migration telemetry event");
  Assert.equal(events[0].extra.source_total, "3", "telemetry total");
  Assert.equal(events[0].extra.target_total, "3", "telemetry target total");
  Assert.equal(events[0].extra.migrated, "3", "telemetry migrated");
  Assert.equal(events[0].extra.failed, "0", "telemetry failed");
  Assert.equal(events[0].extra.diverged, "0", "no divergence reported");
  Assert.equal(events[0].extra.result, "ok", "telemetry result");
  Assert.equal(events[0].extra.direction, "to_rust", "telemetry direction");

  await s._finalize();
});

add_task(async function test_the_number_is_re_encrypted_by_the_target() {
  let { s, guids } = await setupStorageWithCards("cc-mig-cipher.json", [
    "Ann One",
  ]);
  const before = await s.creditCards.get(guids[0]);

  s = await enableRust(s);

  const after = await s.creditCards.get(guids[0]);
  Assert.ok(
    before["cc-number-encrypted"],
    "the source held the number as its own ciphertext"
  );
  // The receiving store encrypts under its own key, so the source's ciphertext
  // does not come along.
  Assert.ok(
    !("cc-number-encrypted" in after),
    "the target was handed the number rather than the ciphertext"
  );
  // What has to survive is the number, not the bytes.
  Assert.equal(
    await rustNumber(guids[0]),
    NUMBERS["Ann One"],
    "and it holds the number that was saved"
  );

  await s._finalize();
});

add_task(async function test_toggling_the_pref_moves_the_profile_both_ways() {
  let { s, guids } = await setupStorageWithCards("cc-mig-toggle.json", [
    "Ann One",
    "Cy Three",
  ]);

  s = await enableRust(s);
  Assert.ok(getBool(ACTIVE_PREF), "moved to Rust");

  s = await disableRust(s);
  Assert.ok(!getBool(ACTIVE_PREF), "moved back to JSON");

  const json = s.creditCards;
  Assert.equal((await json.getAll()).length, 2, "both cards came back");
  for (const [i, name] of ["Ann One", "Cy Three"].entries()) {
    const back = await json.get(guids[i]);
    Assert.ok(back, `${name} kept its guid across the round trip`);
    Assert.equal(
      await decryptNumber(back["cc-number-encrypted"]),
      NUMBERS[name],
      `${name}'s number survived the round trip`
    );
  }

  await s._finalize();
});

add_task(
  async function test_a_decrypt_failure_skips_the_card_and_still_returns_to_json() {
    let { s, guids } = await setupStorageWithCards("cc-mig-partial.json", [
      "Ann One",
      "Bob Two",
    ]);
    s = await enableRust(s);

    // The source is the Rust store here, so it is its key that stops answering.
    // It answers for the first card and then fails the read of the second. That
    // number cannot be recovered, so the card must not be copied at all: writing
    // it with a re-derived number would encrypt the mask in place of the card,
    // and that damage is permanent where the failure was not.
    const store = await rustStore();
    const realGet = store.getCreditCard.bind(store);
    let calls = 0;
    store.getCreditCard = async function (guid) {
      // The first record's export.
      if (++calls > 1) {
        throw new CryptoError("decryption failed");
      }
      return realGet(guid);
    };
    try {
      s = await disableRust(s);
    } finally {
      delete store.getCreditCard;
    }

    // An incomplete copy back still moves the profile.
    Assert.ok(!getBool(ACTIVE_PREF), "the profile is back on JSON");
    const event = migrationEvents().at(-1);
    Assert.equal(event.extra.result, "error", "reported as a failure");
    Assert.equal(event.extra.failed, "1", "with the unreadable record failed");
    // The source cannot read its own numbers back either, so there is nothing to
    // check the copies against, rather than two copies that gained a number.
    Assert.equal(
      event.extra.diverged,
      "0",
      "and no copy is judged without one"
    );

    // JSON serves the card that failed as it held it before Rust did, and the
    // Rust records are untouched.
    for (const [i, name] of ["Ann One", "Bob Two"].entries()) {
      const after = await s.creditCards.get(guids[i]);
      Assert.equal(
        await decryptNumber(after["cc-number-encrypted"]),
        NUMBERS[name],
        `JSON serves ${name} with its number`
      );
      Assert.equal(
        await rustNumber(guids[i]),
        NUMBERS[name],
        `${name}'s number is still readable`
      );
    }

    await s._finalize();
  }
);

add_task(async function test_unreadable_numbers_fail_the_attempt() {
  let { s } = await setupStorageWithCards("cc-mig-locked.json", ["Ann One"]);

  // The OS key store is unlocked, so nothing defers the copy, but no number
  // decrypts. The copy fails rather than carrying the card without its number:
  // reported, counted against the retry budget, and the profile left on JSON.
  // A key store that is merely locked defers instead, without spending one.
  const realDecrypt = OSKeyStore.decrypt;
  // Shaped like the native key store's rejection, which has no message.
  OSKeyStore.decrypt = async function () {
    throw Components.Exception("", Cr.NS_ERROR_FAILURE);
  };
  try {
    s = await enableRust(s);
  } finally {
    OSKeyStore.decrypt = realDecrypt;
  }

  Assert.ok(!getBool(ACTIVE_PREF), "the migration did not happen");
  const [event] = migrationEvents();
  Assert.equal(event?.extra.result, "error", "and was reported as a failure");
  Assert.equal(event.extra.failed, "1", "with the unreadable card failed");
  Assert.equal(
    event.extra.error_message,
    "NS_ERROR_FAILURE",
    "named by its result, for want of a message"
  );
  Assert.equal(
    Services.prefs.getIntPref(ATTEMPTS_PREF, 0),
    1,
    "spending an attempt"
  );

  // Readable again, the next launch migrates normally.
  s = await restart(s);
  Assert.ok(getBool(ACTIVE_PREF), "and the next launch migrates");

  await s._finalize();
});

add_task(async function test_a_write_during_the_copy_fails_it() {
  let { s, guids } = await setupStorageWithCards("cc-mig-midwrite.json", [
    "Ann One",
  ]);

  // The JSON store keeps serving while the copy runs: here the card is used
  // after it was read and before Rust is switched to, as the fill that unlocks
  // the OS key store does. Switching would lose that use, so the copy fails
  // like any other, and the next attempt carries it.
  const proto = Object.getPrototypeOf(s.creditCards);
  const realExport = proto._recordForMigrationExport;
  proto._recordForMigrationExport = async function (record) {
    const exported = await realExport.call(this, record);
    this.notifyUsed(record.guid);
    return exported;
  };
  try {
    s = await enableRust(s);
  } finally {
    delete proto._recordForMigrationExport;
  }

  const [event] = migrationEvents();
  Assert.equal(event?.extra.result, "error", "the copy was not accepted");
  Assert.equal(
    event.extra.error_code,
    "SourceChanged",
    "because the source changed under it"
  );
  Assert.ok(!getBool(ACTIVE_PREF), "the profile stays on JSON");
  Assert.equal(
    Services.prefs.getIntPref(ATTEMPTS_PREF, 0),
    1,
    "spending an attempt"
  );

  s = await restart(s);
  Assert.ok(getBool(ACTIVE_PREF), "the next launch migrates");
  Assert.equal(
    (await s.creditCards.get(guids[0])).timesUsed,
    1,
    "with the use made during the failed copy"
  );

  await s._finalize();
});

add_task(async function test_an_empty_profile_never_touches_the_key_store() {
  let { s } = await setupStorageWithCards("cc-mig-empty.json", []);
  // As a session starts, before anything has used the key store: were the
  // no-records shortcut gone, the copy would wait on an unlock that no card
  // is there to cause.
  OSKeyStore._isLocked = true;

  // Most profiles have no cards. Asking the key store whether it can decrypt
  // nothing would create a key store entry for all of them, so it is not asked.
  const realDecrypt = OSKeyStore.decrypt;
  const realEnsure = OSKeyStore.ensureLoggedIn;
  let touched = 0;
  OSKeyStore.decrypt = async function (...args) {
    touched++;
    return realDecrypt.apply(this, args);
  };
  OSKeyStore.ensureLoggedIn = async function (...args) {
    touched++;
    return realEnsure.apply(this, args);
  };
  try {
    s = await enableRust(s);
  } finally {
    OSKeyStore.decrypt = realDecrypt;
    OSKeyStore.ensureLoggedIn = realEnsure;
  }

  Assert.equal(touched, 0, "the key store was not consulted");
  Assert.ok(getBool(ACTIVE_PREF), "and an empty profile still migrates");
  Assert.ok(!waitsForKeyStore(), "without waiting for the key store");

  await s._finalize();
});

add_task(async function test_flipping_the_pref_switches_a_running_session() {
  const { s, guids } = await setupStorageWithCards("cc-mig-switch.json", [
    "Ann One",
    "Bob Two",
  ]);
  // The switch copies without wiping, and the profile's store outlives a task.
  await RustAutofillCreditCardsAdapter.getInstance().wipe();
  Assert.ok(!getBool(ACTIVE_PREF), "JSON is serving");

  // Flipped underneath a running storage rather than at a restart, which is the
  // pref observer's path rather than initialize()'s.
  Services.prefs.setBoolPref(ENABLED_PREF, true);
  await s._creditCardSwitch;

  Assert.ok(getBool(ACTIVE_PREF), "the profile switched over");
  const rust = s.creditCards;
  Assert.equal((await rust.getAll()).length, 2, "the cards came across");
  Assert.ok(await rust.get(guids[0]), "with their guids preserved");
  Assert.equal(
    await rustNumber(guids[0]),
    NUMBERS["Ann One"],
    "and their numbers re-encrypted into the target"
  );
  Assert.equal(migrationEvents().length, 1, "reported itself once");

  await s._finalize();
});

add_task(async function test_flipping_the_pref_back_copies_to_json() {
  const { s, guids } = await setupStorageWithCards("cc-mig-switchback.json", [
    "Ann One",
  ]);
  await RustAutofillCreditCardsAdapter.getInstance().wipe();

  Services.prefs.setBoolPref(ENABLED_PREF, true);
  await s._creditCardSwitch;
  Assert.ok(getBool(ACTIVE_PREF), "Rust is serving");

  // Added while Rust was serving, so the JSON store has never seen it.
  const addedOnRust = await s.creditCards.add(card("Bob Two"));

  Services.prefs.setBoolPref(ENABLED_PREF, false);
  await s._creditCardSwitch;
  Assert.ok(!getBool(ACTIVE_PREF), "JSON is serving again");

  const json = s.creditCards;
  Assert.equal((await json.getAll()).length, 2, "both cards are in JSON");
  Assert.equal(
    await decryptNumber((await json.get(addedOnRust))["cc-number-encrypted"]),
    NUMBERS["Bob Two"],
    "including the one JSON had never seen, with its number intact"
  );
  Assert.equal(
    await decryptNumber((await json.get(guids[0]))["cc-number-encrypted"]),
    NUMBERS["Ann One"],
    "and the one that made the round trip"
  );

  await s._finalize();
});

add_task(async function test_an_unreadable_store_still_returns_to_json() {
  let { s, guids } = await setupStorageWithCards("cc-mig-unreadable.json", [
    "Ann One",
  ]);
  s = await enableRust(s);
  const before = migrationEvents().length;

  // As a store whose key can no longer decrypt its cards answers a read.
  const store = await rustStore();
  store.getAllCreditCards = async () => {
    throw new CryptoError("decryption failed");
  };
  try {
    s = await disableRust(s);
  } finally {
    delete store.getAllCreditCards;
  }

  Assert.ok(!getBool(ACTIVE_PREF), "the profile is back on JSON");
  const event = migrationEvents()[before];
  Assert.equal(event?.extra.direction, "to_json", "the copy back was tried");
  Assert.equal(event.extra.result, "error", "and reported as a failure");
  Assert.equal(
    Services.prefs.getIntPref(ATTEMPTS_PREF, 0),
    0,
    "without spending an attempt"
  );
  Assert.equal(
    await decryptNumber(
      (await s.creditCards.get(guids[0]))["cc-number-encrypted"]
    ),
    NUMBERS["Ann One"],
    "and JSON serves the card it held before Rust did"
  );

  await s._finalize();
});

add_task(async function test_a_spent_budget_does_not_block_the_copy_back() {
  let { s, guids } = await setupStorageWithCards("cc-mig-budget.json", [
    "Ann One",
  ]);
  s = await enableRust(s);

  // The budget only limits copies into Rust.
  Services.prefs.setIntPref(ATTEMPTS_PREF, MAX_ATTEMPTS);
  s = await disableRust(s);

  Assert.ok(!getBool(ACTIVE_PREF), "the profile is back on JSON");
  Assert.equal(
    migrationEvents().at(-1).extra.attempt,
    "0",
    "reported as attempt 0"
  );
  Assert.equal(
    Services.prefs.getIntPref(ATTEMPTS_PREF, 0),
    MAX_ATTEMPTS,
    "and the budget is left as it was"
  );
  Assert.ok(await s.creditCards.get(guids[0]), "with the card");

  await s._finalize();
});

add_task(async function test_no_migration_while_disabled() {
  const { s } = await setupStorageWithCards("cc-mig-none.json", ["Ann One"]);

  Assert.ok(!getBool(ACTIVE_PREF), "JSON still serves credit cards");
  Assert.equal(migrationEvents().length, 0, "and nothing reported");

  await s._finalize();
});

add_task(async function test_dry_run_measures_and_then_undoes_itself() {
  let { s } = await setupStorageWithCards("cc-mig-dry.json", [
    "Ann One",
    "Bob Two",
  ]);

  await s._finalize();
  Services.prefs.setBoolPref(TEST_MODE_PREF, true);
  s = new FormAutofillStorage(s._path);
  await s.initialize();

  Assert.ok(!getBool(ACTIVE_PREF), "a dry run does not activate Rust");
  const events = migrationEvents();
  Assert.equal(events.length, 1, "the dry run reported itself");
  Assert.equal(events[0].extra.source_total, "2", "and measured the profile");

  // Wiped afterwards, so a later real migration starts from an empty store.
  const rust = RustAutofillCreditCardsAdapter.getInstance();
  Assert.equal((await rust.getAll()).length, 0, "the dry run emptied itself");

  await s._finalize();
});

// Every card the copy writes into Rust is rejected until the returned function
// is called.
function failCopiesIntoRust() {
  const orig = RustAutofillCreditCardsAdapter.prototype.addManyWithMeta;
  RustAutofillCreditCardsAdapter.prototype.addManyWithMeta = records =>
    Promise.resolve(records.map(() => ({ error: "injected failure" })));
  return () => {
    RustAutofillCreditCardsAdapter.prototype.addManyWithMeta = orig;
  };
}

add_task(async function test_a_failed_dry_run_is_retried_up_to_the_limit() {
  let { s } = await setupStorageWithCards("cc-mig-dry-retry.json", ["Ann One"]);
  const restore = failCopiesIntoRust();

  try {
    s = await restart(s, { [TEST_MODE_PREF]: true });
    Assert.equal(migrationEvents().at(-1).extra.result, "error", "it failed");
    Assert.equal(
      migrationEvents().at(-1).extra.attempt,
      "0",
      "reported as attempt 0"
    );
    Assert.equal(getInt(TEST_ATTEMPTS_PREF), 1, "the failure was counted");
    Assert.equal(getInt(TEST_VERSION_PREF), 0, "and the generation is owed");
    Assert.equal(getInt(ATTEMPTS_PREF), 0, "from its own budget");

    s = await restart(s);
    Assert.equal(migrationEvents().length, 2, "the next launch retried");
    Assert.equal(
      migrationEvents().at(-1).extra.attempt,
      "1",
      "reported as attempt 1"
    );
    Assert.equal(getInt(TEST_ATTEMPTS_PREF), 2, "and counted it");

    Services.prefs.setIntPref(TEST_ATTEMPTS_PREF, MAX_ATTEMPTS - 1);
    s = await restart(s);
    Assert.equal(
      migrationEvents().at(-1).extra.attempt,
      String(MAX_ATTEMPTS - 1),
      "the last attempt is reported"
    );
    Assert.equal(
      getInt(TEST_VERSION_PREF),
      1,
      "and the generation is recorded once the budget is spent"
    );
    Assert.ok(
      !Services.prefs.prefHasUserValue(TEST_ATTEMPTS_PREF),
      "with its attempt count cleared"
    );

    const before = migrationEvents().length;
    s = await restart(s);
    Assert.equal(migrationEvents().length, before, "and it is not run again");
  } finally {
    restore();
  }

  Assert.ok(!getBool(ACTIVE_PREF), "the dry run never activated Rust");
  await s._finalize();
});

add_task(async function test_a_dry_run_that_recovers_is_recorded() {
  let { s } = await setupStorageWithCards("cc-mig-dry-recover.json", [
    "Ann One",
  ]);

  const restore = failCopiesIntoRust();
  try {
    s = await restart(s, { [TEST_MODE_PREF]: true });
  } finally {
    restore();
  }
  Assert.equal(getInt(TEST_ATTEMPTS_PREF), 1, "the first dry run failed");

  s = await restart(s);
  const event = migrationEvents().at(-1);
  Assert.equal(event.extra.result, "ok", "the retry succeeded");
  Assert.equal(event.extra.attempt, "1", "reported as attempt 1");
  Assert.equal(getInt(TEST_VERSION_PREF), 1, "the generation is recorded");
  Assert.ok(
    !Services.prefs.prefHasUserValue(TEST_ATTEMPTS_PREF),
    "and the attempt count cleared"
  );
  Assert.ok(!getBool(ACTIVE_PREF), "without activating Rust");

  await s._finalize();
});

add_task(async function test_the_migration_gives_up_and_stays_on_json() {
  let { s } = await setupStorageWithCards("cc-mig-give-up.json", ["Ann One"]);
  const restore = failCopiesIntoRust();

  try {
    Services.prefs.setIntPref(ATTEMPTS_PREF, MAX_ATTEMPTS - 1);
    s = await enableRust(s);
    Assert.equal(
      migrationEvents().at(-1).extra.attempt,
      String(MAX_ATTEMPTS - 1),
      "the last attempt is reported"
    );
    Assert.equal(getInt(ATTEMPTS_PREF), MAX_ATTEMPTS, "and counted");
    Assert.ok(!getBool(ACTIVE_PREF), "the profile stayed on JSON");
    Assert.ok(
      getBool(ENABLED_PREF),
      "and rust.enabled was left as Nimbus set it"
    );
  } finally {
    restore();
  }

  // With the copy now able to succeed, neither a re-flip in a running session
  // nor the next launch tries it again.
  const before = migrationEvents().length;
  Services.prefs.setBoolPref(ENABLED_PREF, false);
  await s._creditCardSwitch;
  Services.prefs.setBoolPref(ENABLED_PREF, true);
  await s._creditCardSwitch;
  Assert.equal(migrationEvents().length, before, "a re-flip does not copy");
  Assert.ok(!getBool(ACTIVE_PREF), "or switch the store");

  s = await restart(s);
  Assert.equal(migrationEvents().length, before, "nor does a launch");

  s = await restart(s, { [ENABLED_PREF]: false, [TEST_MODE_PREF]: true });
  Assert.equal(
    migrationEvents().length,
    before,
    "nor a dry run once rust.enabled is off"
  );
  Assert.ok(!getBool(ACTIVE_PREF), "so JSON keeps serving");
  Assert.equal(
    (await s.creditCards.getAll()).length,
    1,
    "with every card still readable"
  );

  await s._finalize();
});

// Only a storage's credit card migration observes this topic, and only while
// a copy waits for the key store.
const waitsForKeyStore = () =>
  !![...Services.obs.enumerateObservers(MIGRATION_SAFE_POINT_TOPIC)].length;

add_task(async function test_a_changed_number_is_reported_as_divergence() {
  let { s } = await setupStorageWithCards("cc-mig-diverge.json", ["Ann One"]);

  // The copy is checked by the number each store decrypts for it, not by the
  // masks they hand out. A card that reached Rust holding a different number
  // is what that comparison exists to catch, even one whose mask still ends
  // in the same last four digits.
  const orig = RustAutofillCreditCardsAdapter.prototype.addManyWithMeta;
  RustAutofillCreditCardsAdapter.prototype.addManyWithMeta = function (
    records
  ) {
    return orig.call(
      this,
      records.map(r => ({ ...r, "cc-number": "4000000000001111" }))
    );
  };
  try {
    s = await enableRust(s);
  } finally {
    RustAutofillCreditCardsAdapter.prototype.addManyWithMeta = orig;
  }

  const event = migrationEvents().at(-1);
  Assert.equal(event.extra.result, "ok", "a divergence does not fail the run");
  Assert.equal(event.extra.diverged, "1", "but the record is counted");
  const divergences = divergenceEvents();
  Assert.equal(divergences.length, 1, "one event, for the one record");
  Assert.ok(
    divergences[0].extra.changed.split(",").includes("cc-number"),
    "naming the number, which came back reading as a different card"
  );

  await s._finalize();
});

add_task(async function test_a_copy_whose_number_cannot_be_read_dropped_it() {
  let { s } = await setupStorageWithCards("cc-mig-unreadable.json", [
    "Ann One",
  ]);

  // The copy is written through the bulk import, and read back one record at
  // a time, which is where the key stops answering.
  const store = await rustStore();
  store.getCreditCard = async function () {
    throw new CryptoError("decryption failed");
  };
  try {
    s = await enableRust(s);
  } finally {
    delete store.getCreditCard;
  }

  const event = migrationEvents().at(-1);
  Assert.equal(event.extra.result, "ok", "a divergence does not fail the run");
  Assert.equal(event.extra.diverged, "1", "but the record is counted");
  Assert.equal(
    divergenceEvents()[0].extra.dropped,
    "cc-number",
    "as having lost its number"
  );

  await s._finalize();
});

add_task(async function test_what_each_store_derives_is_not_a_divergence() {
  // The two stores disagree about the number by construction: the mask is
  // rebuilt at a fixed width here and at the card's own width in JSON, and the
  // ciphertext differs on every write. Neither is a divergence, and a 15-digit
  // card is what makes the widths differ.
  let { s, guids } = await setupStorageWithCards("cc-mig-nodiverge.json", [
    "Ann One",
    "Cy Three",
  ]);
  const before = await Promise.all(guids.map(g => s.creditCards.get(g)));

  s = await enableRust(s);

  const event = migrationEvents().at(-1);
  Assert.equal(event.extra.diverged, "0", "nothing came back different");
  Assert.deepEqual(divergenceEvents(), [], "and no record was reported");

  // cc-type is the one field the two stores reach differently: JSON re-derives
  // it from the number, and the Rust store keeps what it was given because by
  // read time the number is a mask. A copy that lost it would read as a
  // divergence on a stored field.
  for (const [i, saved] of before.entries()) {
    const after = await s.creditCards.get(guids[i]);
    Assert.equal(
      after["cc-type"],
      saved["cc-type"],
      `${saved["cc-name"]} kept the network it was detected as`
    );
  }

  await s._finalize();
});

add_task(async function test_the_security_code_moves_into_rust_only() {
  // CVV needs the Rust store, so the JSON store takes no code through add().
  // It holds one only from before that, kept in the clear as an unknown field,
  // with the empty string for a form submitted without one; those are written
  // into its records directly. The Rust store holds the code encrypted, and no
  // code for the empty string. The copy into Rust carries the code whether or
  // not the CVV prefs let a consumer read it, so it is checked through raw
  // reads. The copy back leaves a code behind, and keeps what the JSON store
  // still holds for a card nobody edited.
  let { s, guids } = await setupStorageWithCards("cc-mig-csc.json", [
    "Ann One",
    "Bob Two",
  ]);
  s.creditCards._data[0]["cc-csc"] = "123";
  s.creditCards._data[1]["cc-csc"] = "";

  s = await enableRust(s);
  Assert.ok(getBool(ACTIVE_PREF), "moved to Rust");
  Assert.deepEqual(divergenceEvents(), [], "no record came back different");
  Assert.equal(
    (await s.creditCards.get(guids[0], { rawData: true }))["cc-csc"],
    "123",
    "the code came across"
  );
  Assert.equal(
    (await s.creditCards.get(guids[0]))["cc-csc"],
    undefined,
    "a consumer read hides it while CVV is off"
  );
  Assert.equal(
    (await s.creditCards.get(guids[1], { rawData: true }))["cc-csc"],
    undefined,
    "an empty code came across as none"
  );

  setSecurityCodeEnabled(true);
  await s.creditCards.update(guids[1], { "cc-csc": "456" }, true);
  setSecurityCodeEnabled(false);

  s = await disableRust(s);
  Assert.ok(!getBool(ACTIVE_PREF), "moved back to JSON");
  Assert.deepEqual(divergenceEvents(), [], "no record came back different");
  Assert.equal(
    (await s.creditCards.get(guids[1], { rawData: true }))["cc-csc"],
    undefined,
    "a code saved in Rust is not copied back into the JSON store"
  );
  Assert.equal(
    (await s.creditCards.get(guids[0], { rawData: true }))["cc-csc"],
    "123",
    "and an unedited card keeps what the JSON store held"
  );

  await s._finalize();
});

// Rewrite one stored JSON record in place, as an older build or a synced
// record left it, bypassing the normalization add() applies.
async function rewriteJSONRecord(s, guid, rewrite) {
  const data = s._store.data.creditCards;
  const i = data.findIndex(r => r.guid == guid);
  data[i] = await rewrite(data[i]);
  await s._store._save();
}

add_task(async function test_an_all_star_mask_is_not_a_divergence() {
  let { s, guids } = await setupStorageWithCards("cc-mig-starmask.json", [
    "Ann One",
  ]);
  // CreditCards._encryptNumber masks a number that fails isValidNumber
  // entirely in `*`, so the JSON record has no last four digits to compare.
  const invalid = "123456781234";
  await rewriteJSONRecord(s, guids[0], async r => ({
    ...r,
    "cc-number": "*".repeat(invalid.length),
    "cc-number-encrypted": await OSKeyStore.encrypt(invalid),
  }));

  s = await enableRust(s);

  const event = migrationEvents().at(-1);
  Assert.equal(event.extra.result, "ok", "the card is copied");
  Assert.equal(event.extra.diverged, "0", "and not reported as different");
  Assert.deepEqual(divergenceEvents(), [], "no record was reported");
  Assert.equal(
    await rustNumber(guids[0]),
    invalid,
    "the number behind the mask is what reached Rust"
  );

  await s._finalize();
});

add_task(async function test_a_zero_expiry_is_not_a_divergence() {
  let { s, guids } = await setupStorageWithCards("cc-mig-zeroexp.json", [
    "Ann One",
    "Bob Two",
  ]);
  await rewriteJSONRecord(s, guids[0], r => ({
    ...r,
    "cc-exp-month": 0,
    "cc-exp-year": 0,
  }));
  await rewriteJSONRecord(s, guids[1], r => ({ ...r, "cc-exp-month": 0 }));

  s = await enableRust(s);

  const event = migrationEvents().at(-1);
  Assert.equal(event.extra.result, "ok", "both cards are copied");
  Assert.equal(event.extra.diverged, "0", "and neither reported as different");
  Assert.deepEqual(divergenceEvents(), [], "no record was reported");
  const copy = await s.creditCards.get(guids[0]);
  Assert.ok(!("cc-exp-month" in copy), "Rust reads the zero back as absent");

  await s._finalize();
});

add_task(async function test_a_scrubbed_store_still_returns_to_json() {
  let { s, guids } = await setupStorageWithCards("cc-mig-scrubbed.json", [
    "Ann One",
    "Bob Two",
  ]);
  s = await enableRust(s);
  const before = migrationEvents().length;

  // scrubEncryptedData() blanks the ciphertext and keeps the last four digits,
  // so every record reads back as a mask with no number behind it. Handing
  // that over would have the receiving store encrypt the mask in place of the
  // card, so each card fails its export and the copy fails with them.
  const store = await rustStore();
  await store.scrubEncryptedData();

  s = await disableRust(s);

  Assert.ok(!getBool(ACTIVE_PREF), "the profile is back on JSON");
  const event = migrationEvents()[before];
  Assert.equal(event?.extra.result, "error", "and the copy reported a failure");
  Assert.equal(event.extra.failed, "2", "with both cards failed");
  // JSON still holds both numbers from before Rust served, and Rust holds none.
  Assert.equal(event.extra.diverged, "2", "both cards differ between stores");
  Assert.deepEqual(
    divergenceEvents().map(e => e.extra.added),
    ["cc-number", "cc-number"],
    "by the number JSON has and Rust lost"
  );
  Assert.equal(
    Services.prefs.getIntPref(ATTEMPTS_PREF, 0),
    0,
    "without spending an attempt"
  );

  for (const [i, guid] of guids.entries()) {
    const after = await s.creditCards.get(guid);
    Assert.equal(
      await decryptNumber(after["cc-number-encrypted"]),
      NUMBERS[["Ann One", "Bob Two"][i]],
      `JSON serves ${guid} with the number it held before Rust did`
    );
    Assert.equal(
      await rustNumber(guid),
      "",
      "Rust is still scrubbed, and not rewritten with the mask as its number"
    );
  }

  await s._finalize();
});

add_task(async function test_the_active_collection_reads_a_number_back() {
  // What the edit dialog and the sidebar ask of whichever collection is
  // serving: name the field, get the number. They hold a record they were
  // handed and never see the key behind it.
  let { s, guids } = await setupStorageWithCards("cc-mig-readback.json", [
    "Ann One",
  ]);

  for (const [store, flip] of [
    ["JSON", disableRust],
    ["Rust", enableRust],
  ]) {
    s = await flip(s);
    const collection = s.creditCards;
    const record = await collection.get(guids[0]);
    Assert.equal(
      await collection.decryptField(record, "cc-number"),
      NUMBERS["Ann One"],
      `${store} reads the number back through the collection`
    );
  }

  // A record holding nothing under that field is not an error, and is what
  // tells a caller there is no number to show.
  const store = await rustStore();
  await store.scrubEncryptedData();
  Assert.equal(
    await s.creditCards.decryptField(
      await s.creditCards.get(guids[0]),
      "cc-number"
    ),
    null,
    "a scrubbed record reads back as null"
  );

  await s._finalize();
});
