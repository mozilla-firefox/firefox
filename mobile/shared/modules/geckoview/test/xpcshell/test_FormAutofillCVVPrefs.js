/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const { FormAutofill } = ChromeUtils.importESModule(
  "resource://autofill/FormAutofill.sys.mjs"
);

const SUPPORTED_PREF = "extensions.formautofill.creditCards.cvv.supported";
const ENABLED_PREF = "extensions.formautofill.creditCards.cvv.enabled";
const RUST_ACTIVE_PREF =
  "extensions.formautofill.creditCards.storage.rust.active";

add_setup(function () {
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(SUPPORTED_PREF);
    Services.prefs.clearUserPref(ENABLED_PREF);
  });
});

add_task(function test_default() {
  Assert.equal(
    Services.prefs.getCharPref(SUPPORTED_PREF),
    "off",
    "Security codes are not rolled out by default"
  );
  Assert.ok(
    !FormAutofill.isAutofillCreditCardCVVEnabled,
    "So security codes are not captured by default"
  );
});

add_task(function test_rollout_without_rust_storage_pref() {
  Assert.ok(
    !Services.prefs.getBoolPref(RUST_ACTIVE_PREF),
    "Android never marks the desktop Rust store as active"
  );

  Services.prefs.setCharPref(SUPPORTED_PREF, "on");
  Services.prefs.setBoolPref(ENABLED_PREF, true);
  Assert.ok(
    FormAutofill.isAutofillCreditCardCVVSupported,
    "The rollout alone makes security codes supported"
  );
  Assert.ok(
    FormAutofill.isAutofillCreditCardCVVEnabled,
    "Security codes are captured when the user has not declined"
  );

  Services.prefs.setBoolPref(ENABLED_PREF, false);
  Assert.ok(
    !FormAutofill.isAutofillCreditCardCVVEnabled,
    "Security codes are not captured when the user declined"
  );
});
