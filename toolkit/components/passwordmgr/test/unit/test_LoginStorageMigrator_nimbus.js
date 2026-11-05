/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ExperimentAPI } = ChromeUtils.importESModule(
  "resource://nimbus/ExperimentAPI.sys.mjs"
);
const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);
const { LoginStorageMigrator } = ChromeUtils.importESModule(
  "resource://gre/modules/LoginStorageMigrator.sys.mjs"
);

const FEATURE_ID = "logins-rust-storage";
const PREF_ENABLED = "signon.storage.rust.enabled";
const PREF_ACTIVE = "signon.storage.rust.active";
const PREF_ATTEMPTS = "signon.storage.rust.migrationAttempts";
const MAX_MIGRATION_ATTEMPTS = 10;

const defaultEnabled = Services.prefs
  .getDefaultBranch(null)
  .getBoolPref(PREF_ENABLED);

NimbusTestUtils.init(this);

add_setup(async function () {
  const { cleanup } = await NimbusTestUtils.setupTest();
  await ExperimentAPI.ready();

  registerCleanupFunction(async () => {
    await cleanup();
    Services.prefs.clearUserPref(PREF_ENABLED);
    Services.prefs.clearUserPref(PREF_ACTIVE);
    Services.prefs.clearUserPref(PREF_ATTEMPTS);
  });
});

add_task(async function test_enrollment_sets_and_restores_pref() {
  Services.prefs.clearUserPref(PREF_ENABLED);

  const doCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: FEATURE_ID,
    value: { enabled: !defaultEnabled },
  });

  Assert.ok(
    Services.prefs.prefHasUserValue(PREF_ENABLED),
    "the enrollment writes the user branch"
  );
  Assert.equal(
    Services.prefs.getBoolPref(PREF_ENABLED),
    !defaultEnabled,
    "the enrollment sets the pref"
  );

  await doCleanup();

  Assert.ok(
    !Services.prefs.prefHasUserValue(PREF_ENABLED),
    "unenrolling clears the user value"
  );
  Assert.equal(
    Services.prefs.getBoolPref(PREF_ENABLED),
    defaultEnabled,
    "unenrolling restores the default"
  );
});

add_task(async function test_exceeded_budget_keeps_enrollment() {
  Services.prefs.clearUserPref(PREF_ENABLED);
  Services.prefs.setBoolPref(PREF_ACTIVE, false);
  Services.prefs.setIntPref(PREF_ATTEMPTS, MAX_MIGRATION_ATTEMPTS);

  const slug = "logins-rust-storage-budget";
  const doCleanup = await NimbusTestUtils.enrollWithFeatureConfig(
    { featureId: FEATURE_ID, value: { enabled: true } },
    { slug }
  );
  Assert.ok(Services.prefs.getBoolPref(PREF_ENABLED), "the enrollment applies");

  const json = {};
  const store = await new LoginStorageMigrator(json, {}).run();

  Assert.equal(store, json, "the JSON storage stays primary");
  Assert.ok(
    ExperimentAPI.manager.store.get(slug).active,
    "the client stays enrolled"
  );
  Assert.ok(
    Services.prefs.getBoolPref(PREF_ENABLED),
    "the pref keeps the enrollment value"
  );

  await doCleanup();
});
