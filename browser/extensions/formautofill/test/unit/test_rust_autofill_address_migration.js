/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * The one-time JSON->Rust address migration, and what it gates.
 *
 * `storage.rust.enabled` bulk-copies the pre-existing JSON addresses into Rust
 * once, preserving guids, timestamps and sync metadata, and then lets Rust serve
 * addresses. Success is recorded in `storage.rust.active`, which is the latch:
 * anything short of a complete copy leaves it unset so the next launch retries,
 * and leaves the profile on JSON rather than serving a partial copy. Once it is
 * set the copy never runs again -- the JSON store froze when Rust took over.
 *
 * The copy is also verified field by field, but that only produces telemetry --
 * enabling Rust is the decision to switch, and a complete copy is the only bar.
 *
 * `storage.rust.runMigrationTest` is the same copy run only to measure it: it
 * reports, wipes itself, and records itself on its own generation counter so it
 * can never be mistaken for a real migration, and so a later build that fixes a
 * migration bug can bump that counter and measure the same profiles again.
 */

const { FormAutofillStorage, formAutofillStorage } = ChromeUtils.importESModule(
  "resource://autofill/FormAutofillStorage.sys.mjs"
);
const { RustAutofillAddressesAdapter } = ChromeUtils.importESModule(
  "resource://autofill/RustAutofillAddressStorage.sys.mjs"
);
const { RustAutofillStore } = ChromeUtils.importESModule(
  "resource://autofill/RustAutofillStore.sys.mjs"
);
const { SqlError } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs"
);
const { AddressStorageMigrator, AutofillStorageMigrator, MAX_ATTEMPTS } =
  ChromeUtils.importESModule(
    "resource://autofill/AutofillStorageMigrator.sys.mjs"
  );
const { FormAutofill } = ChromeUtils.importESModule(
  "resource://autofill/FormAutofill.sys.mjs"
);

// A region ICU can name, so it survives normalization on write, but one with no
// bundled address metadata, so it is absent from FormAutofill.countries and
// neither store reports it on read.
const UNSUPPORTED_COUNTRY = "XK";

// The prefs are global but the storage singleton is per-process, so it would
// migrate its own empty store alongside the throwaway ones these tests create,
// emitting a stray source_total=0 event and masking the result. Production has
// only the singleton, so no such race exists there.
add_setup(async function () {
  // Two ways in, and both have to be closed:
  //  1. the pref observer. _finalize() removes it, then throws on the
  //     uninitialized singleton's _store -- swallow that, the observer is gone.
  //  2. initialize(), which FormAutofillParent calls the first time any store
  //     fires formautofill-storage-changed, so the adds below trigger it. Stub
  //     it and the collections updateSavedFieldNames() reads.
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

const ENABLED_PREF = "extensions.formautofill.addresses.storage.rust.enabled";
const ACTIVE_PREF = "extensions.formautofill.addresses.storage.rust.active";
const TEST_MODE_PREF =
  "extensions.formautofill.addresses.storage.rust.runMigrationTest";
const TEST_VERSION_PREF =
  "extensions.formautofill.addresses.storage.rust.migrationTestVersion";
const ATTEMPTS_PREF =
  "extensions.formautofill.addresses.storage.rust.migrationAttempts";
const TEST_ATTEMPTS_PREF =
  "extensions.formautofill.addresses.storage.rust.migrationTestAttempts";

const getInt = (pref, def = 0) => Services.prefs.getIntPref(pref, def);

const getBool = (pref, def = false) => Services.prefs.getBoolPref(pref, def);

registerCleanupFunction(() => {
  for (const pref of [
    ENABLED_PREF,
    ACTIVE_PREF,
    TEST_MODE_PREF,
    TEST_VERSION_PREF,
    ATTEMPTS_PREF,
    TEST_ATTEMPTS_PREF,
  ]) {
    Services.prefs.clearUserPref(pref);
  }
});

function addr(name) {
  return {
    name,
    "street-address": "1 Test Rd",
    "address-level2": "Town",
    "address-level1": "CA",
    "postal-code": "90001",
    country: "US",
  };
}

// A deliberately awkward corpus: unicode/CJK/RTL, emoji, SQL metacharacters,
// newlines/whitespace, very long values, and minimal records. Migration must
// bulk-copy all of these successfully and preserve them byte-for-byte, so no
// address style can break or corrupt the JSON->Rust move.
const FORMAT_CORPUS = [
  {
    label: "full US",
    record: {
      name: "Jane Doe",
      organization: "Mozilla",
      "street-address": "331 E Evelyn Ave",
      "address-level2": "Mountain View",
      "address-level1": "CA",
      "postal-code": "94041",
      country: "US",
      tel: "+16505551234",
      email: "jane@example.com",
    },
  },
  {
    label: "latin unicode (accents/eszett/ñ)",
    record: {
      name: "José Núñez Müller",
      organization: "Universität Straße",
      "street-address": "Calle Ñoño 5",
      "address-level2": "München",
      "postal-code": "80331",
      country: "DE",
    },
  },
  {
    label: "CJK",
    record: {
      name: "山田太郎",
      organization: "株式会社モジラ",
      "street-address": "東京都渋谷区桜丘町1-1",
      "address-level1": "東京都",
      country: "JP",
    },
  },
  {
    label: "RTL Arabic",
    record: {
      name: "محمد عبد الله",
      "street-address": "شارع الملك فهد 12",
      "address-level2": "الرياض",
      country: "SA",
    },
  },
  {
    label: "SQL metacharacters",
    record: {
      name: "Robert'); DROP TABLE addresses_data;--",
      organization: 'He said "hi" \\ back\\slash',
      "street-address": "O'Brien Rd; 50% off & <b>bold</b>",
      "address-level2": "Town",
      country: "US",
    },
  },
  {
    label: "emoji + symbols",
    record: {
      name: "🏠 Home Owner 😀",
      "street-address": "42 Café Straße №7 — Ste. #3",
      "address-level2": "Springfield",
      country: "US",
    },
  },
  {
    label: "multiline + tabs + whitespace",
    record: {
      name: "  Padded  Name  ",
      "street-address": "123 Main St\nApt\t4B\r\nFloor 2",
      "address-level2": "Springfield",
      "address-level1": "IL",
      "postal-code": "62704",
      country: "US",
    },
  },
  {
    label: "very long values",
    record: {
      name: "Ludwig " + "van ".repeat(60) + "Beethoven",
      "street-address": "A".repeat(500) + " Boulevard",
      "address-level2": "Metropolis",
      country: "US",
    },
  },
  {
    label: "minimal (name only)",
    record: { name: "Solo", country: "US" },
  },
  {
    label: "org + email, no name",
    record: {
      organization: "Acme Corp",
      email: "info@acme.example",
      country: "US",
    },
  },
];

function diffRecords(a, b) {
  const keys = new Set([...Object.keys(a), ...Object.keys(b)]);
  const diffs = [];
  for (const key of keys) {
    if (a[key] !== b[key]) {
      diffs.push(
        `${key}: json=${JSON.stringify(a[key])} rust=${JSON.stringify(b[key])}`
      );
    }
  }
  return diffs;
}

// Fresh store, Rust off; add records to JSON only, so a later migration has
// pre-existing data to copy that Rust does not yet have.
async function setupStorageWithFullRecords(fileName, records) {
  Services.prefs.setBoolPref(ENABLED_PREF, false);
  Services.prefs.setBoolPref(TEST_MODE_PREF, false);
  Services.prefs.clearUserPref(ACTIVE_PREF);
  Services.prefs.clearUserPref(TEST_VERSION_PREF);
  Services.prefs.clearUserPref(ATTEMPTS_PREF);
  Services.prefs.clearUserPref(TEST_ATTEMPTS_PREF);
  Services.fog.testResetFOG();
  // The adapter is a per-process singleton, so a case that reuses one cached by
  // an earlier case never opens the store and cannot observe that work.
  RustAutofillAddressesAdapter._instance = null;

  const jsonPath = FileTestUtils.getTempFile(fileName).path;
  const s = new FormAutofillStorage(jsonPath);
  await s.initialize();
  Assert.ok(!getBool(ACTIVE_PREF), "JSON is serving while the pref is off");

  const guids = [];
  for (const record of records) {
    guids.push(await s.addresses.add(record));
  }
  return { s, guids };
}

async function setupStorageWithRecords(fileName, names) {
  return setupStorageWithFullRecords(fileName, names.map(addr));
}

// A restart. initialize() reads the prefs once and memoizes, so a new storage
// object over the same file is the only way to see a new pref value -- which is
// also true in production, where only a new session does.
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

// Force another migration. Clearing rust.active is the point: restarting alone
// skips the copy, because an activated profile never migrates again.
async function forceReMigration(s) {
  Services.prefs.clearUserPref(ACTIVE_PREF);
  return restart(s);
}

// The store serving addresses. After a verified migration this is the Rust
// adapter, reached the way a consumer reaches it rather than through a private.
function activeRust(s) {
  Assert.ok(getBool(ACTIVE_PREF), "Rust is the active address store");
  return s.addresses;
}

const migrationEvents = () =>
  Glean.formautofillAddresses.migrateToRust.testGetValue() ?? [];

const divergenceEvents = () =>
  Glean.formautofillAddresses.migrateRecordDivergence.testGetValue() ?? [];

add_task(async function test_migration_populates_rust() {
  let { s, guids } = await setupStorageWithRecords("mig-pre.json", [
    "Ann One",
    "Bob Two",
    "Cy Three",
  ]);

  Assert.ok(!getBool(ACTIVE_PREF), "not migrated yet");

  s = await enableRust(s);
  const rust = activeRust(s);

  Assert.equal(
    (await rust.getAll()).length,
    3,
    "migration copied all 3 records"
  );
  for (const guid of guids) {
    Assert.ok(await rust.get(guid), `record ${guid} migrated with guid parity`);
  }
  Assert.ok(getBool(ACTIVE_PREF), "rust.active set on success");

  // Telemetry: one migration event describing the run.
  const events = migrationEvents();
  Assert.equal(events.length, 1, "one migration telemetry event");
  Assert.equal(events[0].extra.source_total, "3", "telemetry total");
  Assert.equal(events[0].extra.target_total, "3", "telemetry target total");
  Assert.equal(events[0].extra.migrated, "3", "telemetry migrated");
  Assert.equal(events[0].extra.failed, "0", "telemetry failed");
  Assert.equal(events[0].extra.diverged, "0", "telemetry diverged");
  Assert.equal(events[0].extra.result, "ok", "telemetry result");
  Assert.equal(events[0].extra.attempt, "0", "first attempt");
  Assert.greaterOrEqual(
    Number(events[0].extra.duration_ms),
    0,
    "telemetry timed the copy"
  );

  await s._finalize();
});

add_task(async function test_no_migration_while_disabled() {
  const { s } = await setupStorageWithRecords("mig-none.json", ["X", "Y"]);

  // Disabled: no Rust store is built and nothing is copied.
  Assert.ok(!getBool(ACTIVE_PREF), "JSON still serves addresses");
  Assert.ok(!getBool(ACTIVE_PREF), "no migration recorded");
  Assert.equal(migrationEvents().length, 0, "and nothing reported");

  await s._finalize();
});

add_task(async function test_dry_run_measures_and_then_undoes_itself() {
  // What rides out ahead of the real move: measure the copy on a real profile
  // without ever reading from Rust or leaving anything behind.
  let { s } = await setupStorageWithRecords("mig-dryrun.json", [
    "Dry One",
    "Dry Two",
  ]);

  s = await restart(s, { [TEST_MODE_PREF]: true });

  const events = migrationEvents();
  Assert.equal(events.length, 1, "the dry run reported itself");
  Assert.equal(events[0].extra.source_total, "2", "it copied the real records");
  Assert.equal(events[0].extra.result, "ok", "and verified them");
  Assert.equal(events[0].extra.diverged, "0", "with nothing diverging");

  Assert.ok(!getBool(ACTIVE_PREF), "reads never moved to Rust");
  Assert.equal(
    getInt(TEST_VERSION_PREF),
    1,
    "and it is recorded on its own generation, not as a migration"
  );

  const rust = await RustAutofillAddressesAdapter.getInstance();
  Assert.equal(
    (await rust.getAll()).length,
    0,
    "the copy was wiped, so nothing unread is left on disk"
  );

  // One shot per generation: a second launch does not re-measure.
  s = await restart(s);
  Assert.equal(migrationEvents().length, 1, "the dry run does not repeat");

  await s._finalize();
});

add_task(async function test_a_measured_profile_leaves_the_store_shut() {
  // With the dry run on by default, what a profile that has already measured
  // pays is what most launches pay. Reaching the migrator at all would open
  // autofill.db, and the wipe that follows a dry run would empty it, both for a
  // run that returns on its first line.
  let { s } = await setupStorageWithRecords("mig-dryrun-once.json", ["Once"]);

  s = await restart(s, { [TEST_MODE_PREF]: true });
  Assert.equal(migrationEvents().length, 1, "the dry run ran");

  RustAutofillAddressesAdapter._instance = null;
  s = await restart(s);

  Assert.equal(migrationEvents().length, 1, "the next launch does not measure");
  Assert.equal(
    RustAutofillAddressesAdapter._instance,
    null,
    "and never opens the Rust store"
  );

  await s._finalize();
});

add_task(async function test_dry_run_does_not_block_a_later_migration() {
  // The trap this pref pair exists to avoid. If a dry run recorded itself as a
  // real migration, enabling later would find the profile already migrated,
  // skip the copy, and serve a snapshot that went stale the day the dry run
  // ran -- silently losing every address written in between.
  let { s } = await setupStorageWithRecords("mig-dryrun-then.json", ["Before"]);

  s = await restart(s, { [TEST_MODE_PREF]: true });
  Assert.equal(
    getInt(TEST_VERSION_PREF),
    1,
    "the dry-run generation is recorded"
  );
  Assert.ok(!getBool(ACTIVE_PREF), "and not recorded as a real migration");

  // An address saved after the dry run, which the wiped copy never held.
  await s.addresses.add(addr("After The Dry Run"));

  s = await restart(s, { [ENABLED_PREF]: true });

  const rust = activeRust(s);
  Assert.ok(getBool(ACTIVE_PREF), "enabling still performed a real migration");
  const names = (await rust.getAll()).map(r => r.name).sort();
  Assert.deepEqual(
    names,
    ["After The Dry Run", "Before"],
    "including the record added after the dry run"
  );

  await s._finalize();
});

add_task(async function test_partial_failure_is_not_recorded() {
  let { s } = await setupStorageWithRecords("mig-fail.json", [
    "Keep One",
    "Fail Me",
  ]);
  // Inject the failure before enabling: a successful first attempt would
  // activate the profile and skip every later one.
  const orig = RustAutofillAddressesAdapter.prototype.addManyWithMeta;
  RustAutofillAddressesAdapter.prototype.addManyWithMeta = async function (
    records
  ) {
    const results = await orig.call(this, records);
    return records.map((r, i) =>
      r.name === "Fail Me" ? { error: "injected failure" } : results[i]
    );
  };

  try {
    s = await enableRust(s);
    Assert.ok(
      !getBool(ACTIVE_PREF),
      "rust.active left unset when a record fails, so the next launch retries"
    );
    Assert.ok(getBool(ENABLED_PREF), "still enabled");
    Assert.ok(
      !getBool(ACTIVE_PREF),
      "and the profile stays on JSON rather than serving a partial copy"
    );

    const migEvents = migrationEvents();
    Assert.equal(
      migEvents.at(-1).extra.failed,
      "1",
      "telemetry records 1 failure"
    );
    Assert.equal(migEvents.at(-1).extra.result, "error", "telemetry result");
    Assert.stringContains(
      migEvents.at(-1).extra.error_message,
      "injected failure",
      "and the message summarizes why the record was rejected"
    );
  } finally {
    RustAutofillAddressesAdapter.prototype.addManyWithMeta = orig;
  }

  // Recover: a clean retry migrates everything and activates.
  s = await forceReMigration(s);
  Assert.ok(
    getBool(ACTIVE_PREF),
    "rust.active is set after a successful retry"
  );
  Assert.equal(
    (await activeRust(s).getAll()).length,
    2,
    "all records present after recovery"
  );

  await s._finalize();
});

add_task(async function test_toggling_the_pref_moves_the_profile_both_ways() {
  let { s } = await setupStorageWithRecords("mig-idem.json", ["One", "Two"]);
  s = await enableRust(s);

  Assert.ok(getBool(ACTIVE_PREF), "rust.active is set after the migration");
  Assert.equal(
    (await activeRust(s).getAll()).length,
    2,
    "first migration: 2 records"
  );
  Assert.equal(migrationEvents().length, 1, "one copy so far");

  // Off again: the addresses come back to JSON rather than being stranded in a
  // store the profile has stopped reading.
  s = await restart(s, { [ENABLED_PREF]: false });
  Assert.ok(!getBool(ACTIVE_PREF), "turning it off hands the profile to JSON");
  Assert.equal(
    (await s.addresses.getAll()).length,
    2,
    "which shows every record"
  );

  // And on again, without the records multiplying on the way.
  s = await restart(s, { [ENABLED_PREF]: true });
  Assert.ok(getBool(ACTIVE_PREF), "turning it on hands them back to Rust");
  Assert.equal(
    (await activeRust(s).getAll()).length,
    2,
    "still 2 records, no duplicates"
  );
  Assert.equal(migrationEvents().length, 3, "one copy reported per move");

  await s._finalize();
});

add_task(async function test_an_active_profile_never_copies_again() {
  let { s } = await setupStorageWithRecords("mig-nocopy.json", ["One", "Two"]);
  s = await enableRust(s);
  Assert.equal((await activeRust(s).getAll()).length, 2, "migrated 2 records");
  Assert.equal(migrationEvents().length, 1, "one copy so far");

  // A write that only ever lands in Rust. The JSON snapshot froze at
  // activation and does not have it, so any later pass over that snapshot
  // would drop this record.
  const rustOnly = await activeRust(s).add(addr("Three"));
  Assert.equal((await activeRust(s).getAll()).length, 3, "3 records in Rust");

  s = await restart(s);

  const after = await activeRust(s).getAll();
  Assert.equal(after.length, 3, "the Rust-only record survived the restart");
  Assert.ok(
    after.some(record => record.guid === rustOnly),
    "and it is the same record, not a re-import of the JSON two"
  );
  Assert.equal(migrationEvents().length, 1, "and no second copy was attempted");

  await s._finalize();
});

add_task(async function test_the_dry_run_waits_until_json_is_serving_again() {
  let { s } = await setupStorageWithRecords("mig-drytest.json", ["One", "Two"]);
  s = await enableRust(s);
  Assert.equal((await activeRust(s).getAll()).length, 2, "migrated 2 records");

  // The rollout is rolled back while the dry run is turned on to measure it.
  // The dry run ends by wiping the store it just filled, so it must not run
  // against the one the profile is reading: the copy back comes first, and the
  // measuring waits for the launch after it.
  Services.prefs.setBoolPref(TEST_MODE_PREF, true);
  Services.prefs.clearUserPref(TEST_VERSION_PREF);
  s = await restart(s, { [ENABLED_PREF]: false });

  Assert.ok(!getBool(ACTIVE_PREF), "the profile came back to JSON");
  Assert.equal(
    (await s.addresses.getAll()).length,
    2,
    "which holds both records"
  );
  Assert.equal(
    getInt(TEST_VERSION_PREF),
    0,
    "and the dry run held off while Rust was still serving"
  );

  // JSON is serving now, so there is nothing left for a dry run to strand.
  s = await restart(s);

  Assert.equal(
    getInt(TEST_VERSION_PREF),
    1,
    "the next launch measured it instead"
  );
  Assert.ok(!getBool(ACTIVE_PREF), "without switching the profile over");
  Assert.equal(
    (await s.addresses.getAll()).length,
    2,
    "and left the addresses where they are"
  );

  await s._finalize();
});

add_task(async function test_migration_succeeds_on_an_empty_store() {
  // The dominant case: a profile with no saved addresses. Nothing to copy still
  // has to count as a verified success, or the migration retries every launch.
  let { s } = await setupStorageWithRecords("mig-empty.json", []);
  s = await enableRust(s);

  Assert.ok(
    getBool(ACTIVE_PREF),
    "an empty profile still records the migration"
  );
  Assert.equal(
    (await activeRust(s).getAll()).length,
    0,
    "Rust is empty, as JSON was"
  );

  const events = migrationEvents();
  Assert.equal(events.at(-1).extra.source_total, "0", "telemetry total is 0");
  Assert.equal(events.at(-1).extra.result, "ok", "telemetry result");

  await s._finalize();
});

add_task(async function test_count_mismatch_blocks_the_migration() {
  let { s } = await setupStorageWithRecords("mig-count.json", [
    "A One",
    "B Two",
    "C Three",
  ]);
  // Silently drop one record from the batch but report it as a success. Every
  // per-record result is clean, so the `failed === 0` half of the check passes
  // and only the count half can catch the loss. Injected before enabling, since
  // an activated profile would skip later attempts.
  const orig = RustAutofillAddressesAdapter.prototype.addManyWithMeta;
  RustAutofillAddressesAdapter.prototype.addManyWithMeta = async function (
    records
  ) {
    const results = await orig.call(this, records.slice(0, -1));
    return [...results, { guid: records.at(-1).guid }];
  };

  try {
    s = await enableRust(s);
    Assert.ok(
      !getBool(ACTIVE_PREF),
      "not recorded when Rust holds fewer records than JSON"
    );
    Assert.ok(
      getBool(ENABLED_PREF),
      "still enabled, so a later launch retries"
    );
    Assert.ok(!getBool(ACTIVE_PREF), "and the profile stays on JSON");

    const events = migrationEvents();
    Assert.equal(
      events.at(-1).extra.failed,
      "0",
      "no per-record failure was reported"
    );
    Assert.equal(
      events.at(-1).extra.migrated,
      "3",
      "the batch claimed every record migrated"
    );
    Assert.equal(
      events.at(-1).extra.result,
      "error",
      "the count check still marks the run failed"
    );
    Assert.equal(
      events.at(-1).extra.target_total,
      "2",
      "and the store holding fewer than it was given is what says so"
    );
    Assert.equal(
      events.at(-1).extra.error_code,
      undefined,
      "no code: nothing threw, and the counts describe it"
    );
  } finally {
    RustAutofillAddressesAdapter.prototype.addManyWithMeta = orig;
  }

  s = await forceReMigration(s);
  Assert.ok(getBool(ACTIVE_PREF), "a clean retry records the migration");
  Assert.equal(
    (await activeRust(s).getAll()).length,
    3,
    "all records present after recovery"
  );

  await s._finalize();
});

add_task(async function test_stale_derived_field_does_not_block_activation() {
  let { s } = await setupStorageWithRecords("mig-locale.json", ["Ann One"]);

  // What an app-locale change leaves behind. The JSON store persisted
  // country-name when the record was saved and hands back what it wrote; the
  // Rust adapter derives it fresh on every read. A profile that saved US
  // addresses under en-US and now runs a de build disagrees on every one.
  //
  s.addresses._data[0]["country-name"] = "Vereinigte Staaten";
  s.addresses._store.saveSoon();

  s = await enableRust(s);

  const event = migrationEvents().at(-1);
  Assert.equal(event.extra.result, "ok", "the copy is complete");
  Assert.equal(
    event.extra.diverged,
    "0",
    "a derived field is not an unfaithful copy"
  );
  Assert.deepEqual(
    divergenceEvents(),
    [],
    "and not reported at all: the copy never carried the field"
  );
  Assert.ok(getBool(ACTIVE_PREF), "and the profile activates");

  await s._finalize();
});

add_task(async function test_divergence_is_reported_and_does_not_hold_back() {
  // A record that reaches Rust holding a different value is what this phase
  // exists to measure. It is reported and counted, and that is all it does.
  let { s } = await setupStorageWithRecords("mig-diverge.json", ["Ann One"]);

  const orig = RustAutofillAddressesAdapter.prototype.addManyWithMeta;
  RustAutofillAddressesAdapter.prototype.addManyWithMeta = function (records) {
    return orig.call(
      this,
      records.map(r => ({ ...r, organization: "Changed By Rust" }))
    );
  };

  try {
    s = await enableRust(s);
  } finally {
    RustAutofillAddressesAdapter.prototype.addManyWithMeta = orig;
  }

  const event = migrationEvents().at(-1);
  Assert.equal(event.extra.result, "ok", "a divergence does not fail the run");
  Assert.equal(event.extra.diverged, "1", "but the record is counted");

  const divergences = divergenceEvents();
  Assert.equal(divergences.length, 1, "one event, for the one record");
  Assert.equal(
    divergences[0].extra.added,
    "organization",
    "naming the field, and that JSON was empty where Rust was not"
  );
  Assert.equal(
    divergences[0].extra.changed,
    undefined,
    "which is not the same finding as a value coming back different"
  );
  Assert.ok(divergences[0].extra.run_id, "the divergence carries a run id");
  Assert.equal(
    divergences[0].extra.run_id,
    event.extra.run_id,
    "the run's own, which is the only thing joining the two events"
  );
  Assert.ok(
    getBool(ACTIVE_PREF),
    "rust.active is set, so this is not retried forever"
  );
  Assert.ok(
    getBool(ACTIVE_PREF),
    "and the profile switches: a divergence is data to collect, not a veto"
  );

  s = await restart(s);
  Assert.ok(getBool(ACTIVE_PREF), "a later startup still serves Rust");
  Assert.equal(
    migrationEvents().length,
    1,
    "without re-running or re-reporting the migration"
  );

  await s._finalize();
});

add_task(async function test_unknown_fields_are_tolerated() {
  // The JSON store round-trips fields it does not know, so a record synced from
  // a newer client survives a trip through it. The Rust store has columns rather
  // than a blob and drops them, but its sync mirror carries them to the server
  // on upload, so nobody loses data and the migration is not held back.
  let { s } = await setupStorageWithFullRecords("mig-unknown.json", [
    {
      name: "Future Person",
      "street-address": "1 Future Way",
      country: "US",
      "some-future-field": "important value",
    },
  ]);
  Assert.equal(
    (await s._addresses.getAll())[0]["some-future-field"],
    "important value",
    "the JSON store kept the field it does not understand"
  );

  s = await enableRust(s);

  const event = migrationEvents().at(-1);
  Assert.equal(
    event.extra.diverged,
    "0",
    "a field Rust has no column for is not a divergence"
  );
  Assert.deepEqual(
    divergenceEvents(),
    [],
    "and is not reported: it was never Rust's to keep"
  );
  Assert.ok(getBool(ACTIVE_PREF), "and Rust serves the profile");

  await s._finalize();
});

add_task(async function test_a_record_diverging_both_ways_counts_as_real() {
  // One record, one field Rust holds differently and one it cannot hold at all.
  // It is counted once, under the serious one, so the two counts stay a
  // partition of the records rather than overlapping.
  let { s } = await setupStorageWithFullRecords("mig-both.json", [
    {
      name: "Both Ways",
      organization: "Original Org",
      "street-address": "1 Both Way",
      country: "US",
      "some-future-field": "important value",
    },
  ]);

  const orig = RustAutofillAddressesAdapter.prototype.addManyWithMeta;
  RustAutofillAddressesAdapter.prototype.addManyWithMeta = function (records) {
    return orig.call(
      this,
      records.map(r => ({ ...r, organization: "Changed By Rust" }))
    );
  };
  try {
    s = await enableRust(s);
  } finally {
    RustAutofillAddressesAdapter.prototype.addManyWithMeta = orig;
  }

  const event = migrationEvents().at(-1);
  Assert.equal(event.extra.diverged, "1", "counted as a divergence");
  const divergences = divergenceEvents();
  Assert.equal(divergences.length, 1, "one event, for the one record");
  Assert.equal(
    divergences[0].extra.changed,
    "organization",
    "naming only the field Rust stores"
  );
  Assert.equal(
    divergences[0].extra.dropped,
    undefined,
    "and not the one it has no column for"
  );
  Assert.ok(
    getBool(ACTIVE_PREF),
    "and the profile still switches: the counts are data, not a verdict"
  );

  await s._finalize();
});

add_task(async function test_repeated_initialize_migrates_once() {
  const { s, guids } = await setupStorageWithRecords("mig-toggle.json", [
    "Same One",
    "Same Two",
  ]);

  // initialize() is called from several places. Overlapping calls must not each
  // run the setup: that would put two adapters on the same sqlite file and let
  // two migrations wipe and rebuild at once. Count the store handovers to prove
  // they collapse into one.
  const origEnsureOpen = RustAutofillStore.prototype.ensureOpen;
  let opens = 0;
  RustAutofillStore.prototype.ensureOpen = function (...args) {
    opens++;
    return origEnsureOpen.apply(this, args);
  };

  let next;
  try {
    await s._finalize();
    Services.prefs.setBoolPref(ENABLED_PREF, true);
    next = new FormAutofillStorage(s._path);
    await Promise.all([
      next.initialize(),
      next.initialize(),
      next.initialize(),
    ]);
  } finally {
    RustAutofillStore.prototype.ensureOpen = origEnsureOpen;
  }

  Assert.equal(
    opens,
    1,
    "overlapping initialize() calls open exactly one store"
  );
  Assert.ok(
    getBool(ACTIVE_PREF),
    "migration completed once despite the repeated calls"
  );

  const rust = activeRust(next);
  Assert.equal((await rust.getAll()).length, 2, "each record copied once");
  for (const guid of guids) {
    Assert.ok(await rust.get(guid), `record ${guid} copied`);
  }

  await next._finalize();
});

add_task(async function test_migration_wipes_divergent_rust_rows() {
  let { s, guids } = await setupStorageWithRecords("mig-wipe.json", [
    "Real One",
    "Real Two",
  ]);
  s = await enableRust(s);
  const rust = activeRust(s);

  // A row Rust holds that JSON never had -- exactly the divergence the
  // wipe-then-rebuild exists to clear. Written straight to the adapter so the
  // JSON store never learns about it.
  const orphanGuid = await rust.add(addr("Orphan Row"));
  Assert.ok(await rust.get(orphanGuid), "orphan seeded in Rust");

  s = await forceReMigration(s);

  Assert.equal(
    await rust.get(orphanGuid),
    null,
    "the orphan was wiped, not carried over"
  );
  Assert.equal(
    (await rust.getAll()).length,
    2,
    "Rust holds exactly the JSON records"
  );
  for (const guid of guids) {
    Assert.ok(
      await rust.get(guid),
      `JSON record ${guid} present after the rebuild`
    );
  }
  Assert.ok(
    getBool(ACTIVE_PREF),
    "verification passes once Rust matches JSON again"
  );

  await s._finalize();
});

add_task(async function test_remigration_clears_a_blocking_tombstone() {
  let { s, guids } = await setupStorageWithRecords("mig-blocked.json", [
    "Blocked One",
  ]);
  s = await enableRust(s);
  const rust = activeRust(s);

  // A tombstone for a guid the source still holds. Sync leaves this behind when
  // a record is deleted and then resurrected, and the store refuses to insert a
  // guid it has a tombstone for -- so unless the wipe clears tombstones as well
  // as records, this record can never be re-imported and every later migration
  // fails on it.
  await rust.wipe();
  await rust.addManyTombstones([{ guid: guids[0], timeDeleted: 1 }]);

  s = await forceReMigration(s);

  Assert.ok(getBool(ACTIVE_PREF), "the re-migration completed");
  Assert.ok(
    await rust.get(guids[0]),
    "the record whose guid was tombstoned came back"
  );

  await s._finalize();
});

add_task(async function test_tombstones_are_not_migrated() {
  let { s, guids } = await setupStorageWithRecords("mig-tombstone.json", [
    "Live One",
    "Live Two",
    "Gone Three",
  ]);

  // Sync metadata makes remove() leave a tombstone rather than hard-delete, so
  // JSON ends up holding 2 live records plus 1 deleted marker.
  const json = s._addresses;
  json.pullSyncChanges();
  json.remove(guids[2]);
  Assert.equal((await json.getAll()).length, 2, "2 live records");
  Assert.equal(
    (await json.getAll({ includeDeleted: true })).length,
    3,
    "plus a tombstone"
  );

  s = await enableRust(s);

  // Live records and tombstones are split, so source_total counts the 2 live
  // ones and the deleted marker is imported as a tombstone instead.
  Assert.ok(getBool(ACTIVE_PREF), "a tombstone does not break verification");

  const rust = activeRust(s);
  Assert.equal(
    (await rust.getAll()).length,
    2,
    "only the live records reached Rust"
  );
  Assert.equal(
    await rust.get(guids[2]),
    null,
    "the deleted record was not resurrected in Rust"
  );

  Assert.equal(
    migrationEvents().at(-1).extra.source_total,
    "2",
    "telemetry counts live records only"
  );

  await s._finalize();
});

add_task(async function test_a_rejected_tombstone_is_counted_but_not_fatal() {
  let { s, guids } = await setupStorageWithRecords("mig-tombstone-fail.json", [
    "Live One",
    "Gone Two",
  ]);

  const json = s._addresses;
  json.pullSyncChanges();
  json.remove(guids[1]);

  const orig = RustAutofillAddressesAdapter.prototype.addManyTombstones;
  RustAutofillAddressesAdapter.prototype.addManyTombstones = tombstones =>
    Promise.resolve(
      tombstones.map(() => ({ error: "injected tombstone failure" }))
    );

  try {
    s = await enableRust(s);
    Assert.ok(
      getBool(ACTIVE_PREF),
      "a rejected tombstone costs a resurrected deletion, not a record, so " +
        "the profile still switches"
    );

    const event = migrationEvents().at(-1);
    Assert.equal(event.extra.result, "ok", "and the run counts as a success");
    Assert.equal(
      event.extra.failed_deletions,
      "1",
      "the rejected tombstone is counted"
    );
    Assert.equal(event.extra.failed, "0", "and not as a failed record");
    Assert.stringContains(
      event.extra.error_message,
      "injected tombstone failure",
      "the summary still says why it was rejected"
    );
  } finally {
    RustAutofillAddressesAdapter.prototype.addManyTombstones = orig;
  }

  await s._finalize();
});

add_task(async function test_migration_skips_deleted_records() {
  let { s, guids } = await setupStorageWithRecords("mig-syncmeta.json", [
    "Clean One",
    "Dirty Two",
    "Gone Three",
  ]);

  const json = s._addresses;
  // pullSyncChanges mints sync metadata for every record; remove() then leaves
  // a tombstone rather than hard-deleting.
  json.pullSyncChanges();
  json.remove(guids[2]);

  s = await enableRust(s);
  Assert.ok(getBool(ACTIVE_PREF), "migration recorded");

  const migrated = (await activeRust(s).getAll()).map(r => r.guid);
  Assert.deepEqual(
    migrated.sort(),
    [guids[0], guids[1]].sort(),
    "live records migrated; the deleted one is not imported as a record"
  );
  // The imported tombstone and the migrated sync change counters are not
  // readable from JS, so the a-s crate asserts those in its own tests.

  await s._finalize();
});

add_task(async function test_migration_preserves_diverse_formats() {
  let { s, guids } = await setupStorageWithFullRecords(
    "mig-formats.json",
    FORMAT_CORPUS.map(c => c.record)
  );
  s = await enableRust(s);
  // Read both off the post-restart store: the restart finalized the previous
  // one, and its collections throw once the JSON file behind them is closed.
  const json = s._addresses;
  const rust = activeRust(s);

  // Every awkward record migrated cleanly, and none of them came out different.
  Assert.ok(
    getBool(ACTIVE_PREF),
    "the whole diverse corpus migrated and verified"
  );
  Assert.equal(
    (await rust.getAll()).length,
    FORMAT_CORPUS.length,
    "every record present in Rust after migration"
  );

  const event = migrationEvents().at(-1);
  Assert.equal(
    event.extra.migrated,
    String(FORMAT_CORPUS.length),
    "telemetry: all records migrated"
  );
  Assert.equal(event.extra.failed, "0", "telemetry: zero failures");
  Assert.equal(event.extra.diverged, "0", "telemetry: zero divergences");

  // Each record round-trips byte-for-byte: the Rust read equals the JSON read,
  // no matter the character set, length, or metacharacters in the values.
  for (let i = 0; i < guids.length; i++) {
    const guid = guids[i];
    const label = FORMAT_CORPUS[i].label;
    const jsonRecord = await json.get(guid);
    const rustRecord = await rust.get(guid);
    Assert.ok(rustRecord, `[${label}] record migrated to Rust`);
    Assert.deepEqual(
      diffRecords(jsonRecord, rustRecord),
      [],
      `[${label}] Rust read is identical to JSON read after migration`
    );
  }

  await s._finalize();
});

add_task(async function test_attempts_are_counted_and_then_given_up_on() {
  let { s } = await setupStorageWithRecords("mig-attempts.json", ["One"]);

  const orig = RustAutofillAddressesAdapter.prototype.addManyWithMeta;
  RustAutofillAddressesAdapter.prototype.addManyWithMeta = records =>
    Promise.resolve(records.map(() => ({ error: "injected failure" })));

  try {
    s = await enableRust(s);
    Assert.equal(getInt(ATTEMPTS_PREF), 1, "a failure counted an attempt");
    Assert.equal(
      migrationEvents().at(-1).extra.attempt,
      "0",
      "reported as attempt 0"
    );

    // Every launch retries while there is budget left, and each attempt is
    // distinguishable from the last.
    s = await restart(s);
    Assert.equal(getInt(ATTEMPTS_PREF), 2, "and again");
    Assert.equal(
      migrationEvents().at(-1).extra.attempt,
      "1",
      "reported as attempt 1"
    );

    // Out of budget: no more work, and nothing more reported.
    Services.prefs.setIntPref(ATTEMPTS_PREF, MAX_ATTEMPTS);
    const before = migrationEvents().length;
    s = await restart(s);
    Assert.equal(
      migrationEvents().length,
      before,
      "a profile out of budget stops re-importing on every startup"
    );
    Assert.equal(getInt(ATTEMPTS_PREF), MAX_ATTEMPTS, "and stops counting");
  } finally {
    RustAutofillAddressesAdapter.prototype.addManyWithMeta = orig;
  }

  // Succeeding clears the budget, so a later migration starts fresh rather than
  // inheriting these failures.
  Services.prefs.setIntPref(ATTEMPTS_PREF, 2);
  s = await forceReMigration(s);
  Assert.ok(getBool(ACTIVE_PREF), "the retry succeeded");
  Assert.equal(getInt(ATTEMPTS_PREF), 0, "and reset the attempt budget");

  await s._finalize();
});

add_task(async function test_a_failed_migration_leaves_the_profile_on_json() {
  let { s } = await setupStorageWithRecords("mig-nojson.json", [
    "Kept One",
    "Kept Two",
  ]);
  const orig = RustAutofillAddressesAdapter.prototype.addManyWithMeta;
  RustAutofillAddressesAdapter.prototype.addManyWithMeta = records =>
    Promise.resolve(records.map(() => ({ error: "injected failure" })));

  try {
    s = await enableRust(s);

    Assert.ok(
      !getBool(ACTIVE_PREF),
      "a copy that did not complete does not switch the profile"
    );
    Assert.equal(
      (await s.addresses.getAll()).length,
      2,
      "reads still return every address, from JSON"
    );

    // And writes still land in JSON, which is still authoritative.
    const guid = await s.addresses.add(addr("Added After"));
    Assert.ok(await s._addresses.get(guid), "the write reached JSON");
    Assert.equal(
      (await s.addresses.getAll()).length,
      3,
      "and is visible immediately"
    );
  } finally {
    RustAutofillAddressesAdapter.prototype.addManyWithMeta = orig;
    Services.prefs.clearUserPref(ACTIVE_PREF);
  }

  await s._finalize();
});

add_task(async function test_a_write_during_the_copy_fails_it() {
  let { s, guids } = await setupStorageWithRecords("mig-midwrite.json", [
    "Used Mid Copy",
  ]);

  // The JSON store keeps serving while the copy runs, and the address is used
  // after it was read. Switching would lose that use, so the copy fails like
  // any other, and the next attempt carries it.
  const proto = Object.getPrototypeOf(s.addresses);
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
  Assert.equal(getInt(ATTEMPTS_PREF), 1, "spending an attempt");

  s = await restart(s);
  Assert.equal(
    (await activeRust(s).get(guids[0])).timesUsed,
    1,
    "the next launch migrates, with the use made during the failed copy"
  );

  await s._finalize();
});

// The no-wipe copy is driven through the migrator directly: the startup path
// always wipes, and what these cover is the other half of that switch. The
// profile's autofill.db outlives a task, so each starts from a known store.
async function nowipeSetup(fileName, names) {
  const { s, guids } = await setupStorageWithRecords(fileName, names);
  const rust = RustAutofillAddressesAdapter.getInstance();
  await rust.wipe();
  return { s, guids, json: s._addresses, rust };
}

add_task(async function test_no_wipe_leaves_the_target_holding_the_source() {
  const { s, guids, json, rust } = await nowipeSetup("mig-nowipe.json", [
    "Source One",
  ]);

  // One record only the target has, and an older copy of one the source holds.
  const strayGuid = await rust.add(addr("Only In Rust"));
  const source = await json.get(guids[0]);
  const [stale] = await rust.addManyWithMeta([
    {
      ...source,
      organization: "Stale Org",
      timeLastModified: source.timeLastModified - 1000,
    },
  ]);
  Assert.ok(stale.guid, "the target holds an older copy of the source record");

  const migrator = new AddressStorageMigrator(json, rust);
  Assert.ok(await migrator.maybeRun({ wipe: false }), "the copy completed");

  Assert.equal(
    await rust.get(strayGuid),
    null,
    "the record the source does not have is gone: the source is authoritative"
  );
  Assert.ok(
    !("organization" in (await rust.get(guids[0]))),
    "and the source won on the record they both hold, being the newer one"
  );
  Assert.equal((await rust.getAll()).length, 1, "so the two stores match");

  await s._finalize();
});

add_task(async function test_no_wipe_applies_the_source_deletions() {
  const { s, guids, json, rust } = await nowipeSetup("mig-nowipe-del.json", [
    "Kept One",
    "Deleted Two",
  ]);

  const migrator = new AddressStorageMigrator(json, rust);
  Assert.ok(await migrator.maybeRun({ wipe: false }), "the first copy landed");
  Assert.equal((await rust.getAll()).length, 2, "both records reached Rust");

  // Sync metadata makes remove() leave a tombstone rather than hard-delete.
  json.pullSyncChanges();
  json.remove(guids[1]);

  Assert.ok(await migrator.maybeRun({ wipe: false }), "the second copy landed");
  Assert.ok(await rust.get(guids[0]), "the live record is still there");
  Assert.equal(
    await rust.get(guids[1]),
    null,
    "and the source's deletion was applied rather than left behind"
  );

  await s._finalize();
});

add_task(async function test_no_wipe_cannot_clear_a_blocking_tombstone() {
  const { s, guids, json, rust } = await nowipeSetup("mig-nowipe-block.json", [
    "Blocked One",
  ]);

  // A tombstone for a guid the source still holds, which sync leaves behind
  // when a record is deleted and then resurrected. The store refuses to insert
  // a guid it has a tombstone for, and there is no API to clear one, so only a
  // wipe can get this record across.
  await rust.addManyTombstones([{ guid: guids[0], timeDeleted: 1 }]);

  const migrator = new AddressStorageMigrator(json, rust);
  Assert.ok(
    !(await migrator.maybeRun({ wipe: false })),
    "the copy did not complete"
  );
  const event = migrationEvents().at(-1);
  Assert.equal(event.extra.failed, "1", "the record is reported as rejected");
  Assert.stringContains(
    event.extra.error_message,
    "tombstones",
    "naming the tombstone that blocked it"
  );

  Assert.ok(
    await migrator.maybeRun({ wipe: true }),
    "and wiping clears the tombstone, so the same copy completes"
  );
  Assert.ok(await rust.get(guids[0]), "with the record present");

  await s._finalize();
});

add_task(async function test_flipping_the_pref_switches_a_running_session() {
  const { s, guids } = await setupStorageWithRecords("mig-switch.json", [
    "Switch One",
    "Switch Two",
  ]);
  // The switch copies without wiping, and the profile's store outlives a task.
  await RustAutofillAddressesAdapter.getInstance().wipe();
  Assert.ok(!getBool(ACTIVE_PREF), "JSON is serving");

  // Flipped underneath a running storage rather than at a restart.
  Services.prefs.setBoolPref(ENABLED_PREF, true);
  await s._addressSwitch;

  Assert.ok(getBool(ACTIVE_PREF), "the profile switched over");
  const rust = s.addresses;
  Assert.equal((await rust.getAll()).length, 2, "the addresses came across");
  Assert.ok(await rust.get(guids[0]), "with their guids preserved");
  Assert.equal(
    migrationEvents().length,
    1,
    "and the switch reported itself once"
  );

  await s._finalize();
});

add_task(async function test_flipping_the_pref_back_copies_to_json() {
  const { s, guids } = await setupStorageWithRecords("mig-switch-back.json", [
    "Stays One",
  ]);
  await RustAutofillAddressesAdapter.getInstance().wipe();

  Services.prefs.setBoolPref(ENABLED_PREF, true);
  await s._addressSwitch;
  Assert.ok(getBool(ACTIVE_PREF), "Rust is serving");

  // Written while Rust was serving, so JSON has never seen it, and one of the
  // records JSON does have is deleted where only Rust can see it.
  const addedGuid = await s.addresses.add(addr("Added In Rust"));
  await s.addresses.remove(guids[0]);

  Services.prefs.setBoolPref(ENABLED_PREF, false);
  await s._addressSwitch;

  Assert.ok(!getBool(ACTIVE_PREF), "the profile reads from JSON again");
  Assert.ok(
    await s.addresses.get(addedGuid),
    "the record written while Rust was serving came back with it"
  );
  Assert.equal(
    await s.addresses.get(guids[0]),
    null,
    "and the one deleted there did not come back"
  );
  Assert.equal((await s.addresses.getAll()).length, 1, "so the stores match");

  await s._finalize();
});

add_task(async function test_a_failed_copy_back_still_returns_to_json() {
  const { s, guids } = await setupStorageWithRecords(
    "mig-switch-back-fail.json",
    ["Stays One"]
  );
  await RustAutofillAddressesAdapter.getInstance().wipe();

  Services.prefs.setBoolPref(ENABLED_PREF, true);
  await s._addressSwitch;
  Assert.ok(getBool(ACTIVE_PREF), "Rust is serving");
  const addedGuid = await s.addresses.add(addr("Added In Rust"));
  const before = migrationEvents().length;

  s._addresses.addManyWithMeta = records =>
    Promise.resolve(records.map(() => ({ error: "injected failure" })));
  try {
    Services.prefs.setBoolPref(ENABLED_PREF, false);
    await s._addressSwitch;
  } finally {
    delete s._addresses.addManyWithMeta;
  }

  Assert.ok(!getBool(ACTIVE_PREF), "the profile reads from JSON again");
  const event = migrationEvents()[before];
  Assert.equal(event?.extra.direction, "to_json", "the copy back was tried");
  Assert.equal(event.extra.result, "error", "and reported as a failure");
  Assert.equal(getInt(ATTEMPTS_PREF), 0, "without spending an attempt");
  Assert.ok(await s.addresses.get(guids[0]), "JSON serves what it held");
  Assert.equal(
    await s.addresses.get(addedGuid),
    null,
    "without the record that did not make it back"
  );

  await s._finalize();
});

add_task(async function test_a_deletion_that_fails_fails_the_copy() {
  const { s, guids, json, rust } = await nowipeSetup("mig-nodelete.json", [
    "Kept One",
  ]);

  // A record only the target holds, which the copy has to delete for the two
  // to match. Every other failure path is an import; this one is a removal,
  // and leaving it behind puts an address the user deleted back on screen.
  const strayGuid = await rust.add(addr("Deleted Elsewhere"));
  const orig = RustAutofillAddressesAdapter.prototype.removeMany;
  RustAutofillAddressesAdapter.prototype.removeMany = guidsToRemove =>
    Promise.resolve(guidsToRemove.map(() => ({ error: "injected failure" })));

  try {
    const migrator = new AddressStorageMigrator(json, rust);
    Assert.ok(
      !(await migrator.maybeRun({ wipe: false })),
      "the copy did not complete"
    );

    const event = migrationEvents().at(-1);
    Assert.equal(event.extra.result, "error", "and is reported as an error");
    Assert.equal(
      event.extra.failed_deletions,
      "1",
      "counting the deletion rather than the records, which all arrived"
    );
    Assert.equal(event.extra.failed, "0", "no record was rejected");
    Assert.ok(
      await rust.get(strayGuid),
      "the record that should have gone is still there"
    );
    Assert.ok(await rust.get(guids[0]), "alongside the one that was copied");
  } finally {
    RustAutofillAddressesAdapter.prototype.removeMany = orig;
  }

  await s._finalize();
});

add_task(async function test_a_round_trip_keeps_a_field_rust_cannot_hold() {
  const { s, guids } = await setupStorageWithFullRecords("mig-roundtrip.json", [
    {
      name: "Future Person",
      "street-address": "1 Future Way",
      country: "US",
      "some-future-field": "important value",
    },
  ]);
  await RustAutofillAddressesAdapter.getInstance().wipe();

  // Out to Rust, which has no column for the field, and back again. The JSON
  // record is overwritten by the copy coming back, so unless the field is
  // carried across the round trip destroys it.
  Services.prefs.setBoolPref(ENABLED_PREF, true);
  await s._addressSwitch;
  Assert.ok(getBool(ACTIVE_PREF), "Rust is serving");
  Assert.ok(
    !("some-future-field" in (await s.addresses.get(guids[0]))),
    "and cannot hand the field back while it serves"
  );

  Services.prefs.setBoolPref(ENABLED_PREF, false);
  await s._addressSwitch;

  Assert.ok(!getBool(ACTIVE_PREF), "JSON is serving again");
  Assert.equal(
    (await s.addresses.get(guids[0]))["some-future-field"],
    "important value",
    "with the field it kept for the client that understands it"
  );

  await s._finalize();
});

add_task(async function test_a_copy_back_keeps_an_unsupported_country() {
  const { s, json, rust } = await nowipeSetup("mig-country-back.json", []);
  Assert.ok(
    !FormAutofill.countries.has(UNSUPPORTED_COUNTRY),
    `${UNSUPPORTED_COUNTRY} has no bundled address metadata`
  );

  // A record only Rust holds, so the copy back inserts it: what the source
  // read reports is the whole of what reaches the disk. Hiding the code on
  // read must not lose it here -- the record would come back holding no
  // country at all, and the next edit would stamp it with the default region.
  const guid = await rust.add({
    name: "Blerim Gashi",
    "street-address": "Rruga C 7",
    "address-level2": "Prizren",
    country: UNSUPPORTED_COUNTRY,
  });
  Assert.equal(
    (await rust._get(await rust._store(), guid)).country,
    UNSUPPORTED_COUNTRY,
    "Rust holds the code"
  );
  Assert.ok(
    !("country" in (await rust.get(guid))),
    "and does not report it on read"
  );

  const migrator = new AddressStorageMigrator(rust, json);
  Assert.ok(await migrator.maybeRun({ wipe: false }), "the copy completed");

  Assert.equal(
    json._data.find(record => record.guid == guid).country,
    UNSUPPORTED_COUNTRY,
    "the code reached the disk on the other side"
  );

  await s._finalize();
});

add_task(async function test_a_record_with_the_same_timestamp_is_left_alone() {
  const { s, guids, json, rust } = await nowipeSetup("mig-sametime.json", [
    "Same Time",
  ]);

  // The copy takes timeLastModified as the record's identity: same guid, same
  // timestamp, same record. A target holding something else under both is
  // left as it is, which is what keeps a field the source cannot represent
  // alive across a round trip -- and means a difference that arrived without
  // a write, such as one store normalising a value differently, is not
  // corrected by switching.
  const source = await json.get(guids[0]);
  const [held] = await rust.addManyWithMeta([
    { ...source, organization: "Untouched" },
  ]);
  Assert.ok(held.guid, "the target holds its own version of the record");

  const migrator = new AddressStorageMigrator(json, rust);
  Assert.ok(await migrator.maybeRun({ wipe: false }), "the copy completed");

  Assert.equal(
    (await rust.get(guids[0])).organization,
    "Untouched",
    "the target kept what it had"
  );
  const event = migrationEvents().at(-1);
  Assert.equal(event.extra.migrated, "0", "and nothing was written");
  Assert.equal(event.extra.source_total, "1", "though the record was counted");

  await s._finalize();
});

add_task(async function test_a_failed_switch_does_not_disable_later_ones() {
  let { s, guids } = await setupStorageWithRecords("mig-switchfail.json", [
    "Switch After Failure",
  ]);
  await RustAutofillAddressesAdapter.getInstance().wipe();

  // The switch chains each flip onto the last, so one that rejects would take
  // every later flip with it.
  const orig = RustAutofillAddressesAdapter.getInstance;
  RustAutofillAddressesAdapter.getInstance = () => {
    throw new Error("injected failure");
  };
  try {
    Services.prefs.setBoolPref(ENABLED_PREF, true);
    await s._addressSwitch;
    Assert.ok(!getBool(ACTIVE_PREF), "the failed switch left the profile put");
  } finally {
    RustAutofillAddressesAdapter.getInstance = orig;
  }

  Services.prefs.setBoolPref(ENABLED_PREF, false);
  await s._addressSwitch;
  Services.prefs.setBoolPref(ENABLED_PREF, true);
  await s._addressSwitch;

  Assert.ok(getBool(ACTIVE_PREF), "and a later flip is still honoured");
  Assert.ok(
    await s.addresses.get(guids[0]),
    "with the addresses copied across"
  );

  await s._finalize();
});

add_task(async function test_a_tombstone_does_not_block_the_copy_back() {
  const { s, guids, json, rust } = await nowipeSetup("mig-tombback.json", [
    "Resurrected One",
  ]);

  // What sync leaves behind when a record is deleted here and comes back from
  // another device while the other store is serving: a tombstone on this side,
  // the record still live on that one.
  const record = await json.get(guids[0]);
  json.pullSyncChanges();
  json.remove(guids[0]);
  const [live] = await rust.addManyWithMeta([record]);
  Assert.ok(live.guid, "the record is still live in the source");

  const migrator = new AddressStorageMigrator(rust, json);
  Assert.ok(await migrator.maybeRun({ wipe: false }), "the copy completed");
  Assert.ok(
    await json.get(guids[0]),
    "and the record came back over the tombstone"
  );

  await s._finalize();
});

add_task(async function test_startup_copies_back_when_the_pref_went_off() {
  let { s, guids } = await setupStorageWithRecords("mig-offwhileclosed.json", [
    "Was In JSON",
  ]);
  await RustAutofillAddressesAdapter.getInstance().wipe();

  s = await enableRust(s);
  Assert.ok(getBool(ACTIVE_PREF), "Rust is serving");
  const addedInRust = await s.addresses.add(addr("Added While Rust Served"));

  // Turned off with no session to notice: prefs.js edited by hand, a policy, or
  // a session that ended before the switch. The next startup has to settle it,
  // since no pref change will arrive to trigger one.
  s = await restart(s, { [ENABLED_PREF]: false });

  Assert.ok(!getBool(ACTIVE_PREF), "startup handed the profile back to JSON");
  Assert.ok(
    await s.addresses.get(addedInRust),
    "with the record only Rust had"
  );
  Assert.ok(await s.addresses.get(guids[0]), "alongside the original");
  Assert.equal((await s.addresses.getAll()).length, 2, "and nothing else");

  await s._finalize();
});

add_task(
  async function test_a_copy_that_throws_is_reported_without_the_guids() {
    let { s, guids } = await setupStorageWithRecords("mig-throws.json", [
      "One",
    ]);

    // Not a rejected record but a failed batch, which is what a database that
    // cannot be written looks like. The message carries a guid, as the store's
    // own errors do.
    const orig = RustAutofillAddressesAdapter.prototype.addManyWithMeta;
    RustAutofillAddressesAdapter.prototype.addManyWithMeta = () => {
      throw new SqlError(`no such column while writing ${guids[0]}`);
    };

    try {
      s = await enableRust(s);
      Assert.ok(!getBool(ACTIVE_PREF), "the profile stays on JSON");

      const event = migrationEvents().at(-1);
      Assert.equal(event.extra.result, "error", "reported as an error");
      Assert.equal(
        event.extra.error_code,
        "SqlError",
        "under the error's own variant, which is what groups them"
      );
      Assert.ok(
        event.extra.error_message.includes("no such column"),
        "with the text of what went wrong"
      );
      Assert.ok(
        !event.extra.error_message.includes(guids[0]),
        "and the guid replaced rather than reported"
      );
      Assert.ok(
        event.extra.error_message.includes("<id>"),
        "by the placeholder that says one was there"
      );
      Assert.equal(event.extra.migrated, "0", "nothing was written");
    } finally {
      RustAutofillAddressesAdapter.prototype.addManyWithMeta = orig;
    }

    // Recover, so the failure is a launch rather than a profile.
    s = await forceReMigration(s);
    Assert.ok(getBool(ACTIVE_PREF), "and a clean retry migrates");

    await s._finalize();
  }
);

add_task(async function test_a_reporting_failure_still_counts_the_attempt() {
  let { s } = await setupStorageWithRecords("mig-noreport.json", ["One"]);

  // Telemetry must not decide the outcome. Stubbed at the migrator, since
  // Glean's own metrics cannot be replaced from a test.
  const orig = AutofillStorageMigrator.prototype._report;
  AutofillStorageMigrator.prototype._report = () => {
    throw new Error("injected telemetry failure");
  };

  try {
    s = await enableRust(s);
    Assert.ok(getBool(ACTIVE_PREF), "the copy still handed the profile over");
    Assert.equal(
      (await activeRust(s).getAll()).length,
      1,
      "with the record copied"
    );
    Assert.equal(getInt(ATTEMPTS_PREF), 0, "and nothing held against it");
  } finally {
    AutofillStorageMigrator.prototype._report = orig;
  }

  await s._finalize();
});

add_task(async function test_a_reporting_failure_still_ends_the_dry_run() {
  let { s } = await setupStorageWithRecords("mig-noreport-dry.json", ["One"]);

  const orig = AutofillStorageMigrator.prototype._report;
  AutofillStorageMigrator.prototype._report = () => {
    throw new Error("injected telemetry failure");
  };

  try {
    // Without the generation recorded, the dry run would copy and wipe the
    // store again on every launch, for as long as the reporting kept failing.
    s = await restart(s, { [TEST_MODE_PREF]: true });
    Assert.equal(
      getInt(TEST_VERSION_PREF),
      1,
      "the dry run recorded its generation anyway"
    );
    Assert.ok(!getBool(ACTIVE_PREF), "and did not switch the profile");
  } finally {
    AutofillStorageMigrator.prototype._report = orig;
  }

  await s._finalize();
});

add_task(async function test_lone_surrogates_are_not_a_divergence() {
  let { s, guids } = await setupStorageWithRecords("mig-surrogates.json", [
    "Ann One",
  ]);
  // A record stored before lone surrogates were refused, which add() no longer
  // lets through.
  s.addresses._data[0].organization = "Mozilla\uD83D";
  s.addresses._store.saveSoon();

  s = await enableRust(s);
  const rust = activeRust(s);

  Assert.equal(
    (await rust.get(guids[0])).organization,
    "Mozilla\uFFFD",
    "the copy holds U+FFFD in place of the lone surrogate"
  );
  const event = migrationEvents().at(-1);
  Assert.equal(event.extra.result, "ok", "the migration completes");
  Assert.equal(event.extra.failed, "0", "the record did not fail");
  Assert.equal(
    event.extra.diverged,
    "0",
    "the replacement is not a divergence"
  );
});
