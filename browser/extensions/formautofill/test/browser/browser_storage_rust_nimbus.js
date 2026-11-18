/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusTestUtils: "resource://testing-common/NimbusTestUtils.sys.mjs",
});

const TEST_ADDRESS = {
  "given-name": "Timothy",
  "family-name": "Berners-Lee",
  "street-address": "32 Vassar Street",
  "address-level2": "Cambridge",
  "address-level1": "MA",
  "postal-code": "02139",
  country: "US",
};

add_setup(async function () {
  await ExperimentAPI.ready();
  // The storage observes the enabled prefs only once initialize() resolves.
  await formAutofillStorage.initialize();
});

/**
 * Enroll in `featureId`, check that the storage moves to Rust, then unenroll
 * and check that it moves back to JSON.
 *
 * @param {string} featureId
 * @param {string} enabledPref The pref the feature sets.
 * @param {string} activePref The pref saying which store is serving.
 */
async function checkEnrollmentSwitchesStorage(
  featureId,
  enabledPref,
  activePref
) {
  Assert.ok(!Services.prefs.getBoolPref(activePref), "JSON is serving");

  let migrated = waitForStorageChangedEvents("migrate");
  let doCleanup = await NimbusTestUtils.enrollWithFeatureConfig(
    { featureId, value: { enabled: true } },
    { isRollout: true }
  );
  registerCleanupFunction(async () => {
    if (doCleanup) {
      await doCleanup();
      await formAutofillStorage._addressSwitch;
      await formAutofillStorage._creditCardSwitch;
    }
  });
  await migrated;

  Assert.ok(
    Services.prefs.prefHasUserValue(enabledPref),
    `${featureId} sets ${enabledPref} on the user branch`
  );
  Assert.ok(Services.prefs.getBoolPref(activePref), "Rust is serving");

  migrated = waitForStorageChangedEvents("migrate");
  await doCleanup();
  doCleanup = null;
  await migrated;

  Assert.ok(
    !Services.prefs.prefHasUserValue(enabledPref),
    `Unenrolling clears ${enabledPref}`
  );
  Assert.ok(!Services.prefs.getBoolPref(activePref), "JSON is serving again");
}

add_task(async function test_addresses_follow_nimbus() {
  registerCleanupFunction(removeAllRecords);
  await setStorage(TEST_ADDRESS);

  await checkEnrollmentSwitchesStorage(
    "autofill-addresses-rust-storage",
    "extensions.formautofill.addresses.storage.rust.enabled",
    "extensions.formautofill.addresses.storage.rust.active"
  );

  const addresses = await getAddresses();
  Assert.equal(addresses.length, 1, "The address survived both switches");
  Assert.equal(
    addresses[0]["street-address"],
    TEST_ADDRESS["street-address"],
    "with its fields intact"
  );
});

add_task(async function test_credit_cards_follow_nimbus() {
  await checkEnrollmentSwitchesStorage(
    "autofill-credit-cards-rust-storage",
    "extensions.formautofill.creditCards.storage.rust.enabled",
    "extensions.formautofill.creditCards.storage.rust.active"
  );
});

add_task(async function test_dropping_undecryptable_cards_follows_nimbus() {
  const enabledPref =
    "extensions.formautofill.creditCards.storage.rust.dropUndecryptable";
  const afterAttemptsPref =
    "extensions.formautofill.creditCards.storage.rust.dropUndecryptableAfterAttempts";
  Assert.ok(!Services.prefs.getBoolPref(enabledPref), "off by default");

  const doCleanup = await NimbusTestUtils.enrollWithFeatureConfig(
    {
      featureId: "autofill-credit-cards-drop-undecryptable",
      value: { enabled: true, afterAttempts: 5 },
    },
    { isRollout: true }
  );
  Assert.ok(Services.prefs.getBoolPref(enabledPref), "enabled sets its pref");
  Assert.equal(
    Services.prefs.getIntPref(afterAttemptsPref),
    5,
    "afterAttempts sets its pref"
  );

  await doCleanup();
  Assert.ok(
    !Services.prefs.prefHasUserValue(enabledPref) &&
      !Services.prefs.prefHasUserValue(afterAttemptsPref),
    "Unenrolling clears both"
  );
});
