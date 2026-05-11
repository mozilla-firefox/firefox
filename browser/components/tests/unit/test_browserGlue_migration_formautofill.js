/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TOPIC_BROWSERGLUE_TEST = "browser-glue-test";
const TOPICDATA_BROWSERGLUE_TEST = "force-ui-migration";

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const gBrowserGlue = Cc["@mozilla.org/browser/browserglue;1"].getService(
  Ci.nsIObserver
);
const UI_VERSION = 124;

function ensureOldPrefsAreCleared() {
  Assert.ok(
    !Services.prefs.prefHasUserValue("extensions.formautofill.available"),
    "main module available pref should have been cleared"
  );
  Assert.ok(
    !Services.prefs.prefHasUserValue(
      "extensions.formautofill.creditCards.available"
    ),
    "old credit card available pref should have been cleared"
  );
}

add_task(async function setup() {
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("browser.migration.version");
    Services.prefs.clearUserPref("extensions.formautofill.available");
    Services.prefs.clearUserPref(
      "extensions.formautofill.creditCards.available"
    );
    Services.prefs.clearUserPref(
      "extensions.formautofill.creditCards.supported"
    );
  });
});

add_task(async function test_check_form_autofill_module_detect() {
  Services.prefs.setIntPref("browser.migration.version", UI_VERSION - 1);
  Services.prefs.setCharPref("extensions.formautofill.available", "detect");
  // Simulate a migration.
  gBrowserGlue.observe(
    null,
    TOPIC_BROWSERGLUE_TEST,
    TOPICDATA_BROWSERGLUE_TEST
  );
  // old credit card available should migrate to "detect" due to
  // "extensions.formautofill.available" being "on".
  Assert.equal(
    Services.prefs.getCharPref("extensions.formautofill.creditCards.supported"),
    "on"
  );
  // old address available pref follows the main module pref
  Assert.equal(
    Services.prefs.getCharPref("extensions.formautofill.addresses.supported"),
    AppConstants.NIGHTLY_BUILD ? "on" : "detect"
  );
  ensureOldPrefsAreCleared();
});

add_task(async function test_check_old_form_autofill_module_off() {
  Services.prefs.setIntPref("browser.migration.version", UI_VERSION - 1);
  Services.prefs.setCharPref("extensions.formautofill.available", "off");

  // Simulate a migration.
  gBrowserGlue.observe(
    null,
    TOPIC_BROWSERGLUE_TEST,
    TOPICDATA_BROWSERGLUE_TEST
  );

  // old credit card available should migrate to off due to
  // "extensions.formautofill.available" being off.
  Assert.equal(
    Services.prefs.getCharPref("extensions.formautofill.creditCards.supported"),
    "off"
  );
  // old address available pref follows the main module pref
  Assert.equal(
    Services.prefs.getCharPref("extensions.formautofill.addresses.supported"),
    "off"
  );
  ensureOldPrefsAreCleared();
});

add_task(async function test_check_old_form_autofill_module_on_cc_on() {
  Services.prefs.setIntPref("browser.migration.version", UI_VERSION - 1);
  Services.prefs.setCharPref("extensions.formautofill.available", "on");
  Services.prefs.setBoolPref(
    "extensions.formautofill.creditCards.available",
    true
  );

  // Simulate a migration.
  gBrowserGlue.observe(
    null,
    TOPIC_BROWSERGLUE_TEST,
    TOPICDATA_BROWSERGLUE_TEST
  );

  // old credit card available should migrate to "on" due to
  // "extensions.formautofill.available" being on and
  // "extensions.formautofill.creditCards.available" having a default value of true.
  Assert.equal(
    Services.prefs.getCharPref("extensions.formautofill.creditCards.supported"),
    "on"
  );
  // old address available pref follows the main module pref
  Assert.equal(
    Services.prefs.getCharPref("extensions.formautofill.addresses.supported"),
    "on"
  );
  ensureOldPrefsAreCleared();
});

add_task(async function test_check_old_form_autofill_module_on_cc_off() {
  Services.prefs.setIntPref("browser.migration.version", UI_VERSION - 1);
  Services.prefs.setCharPref("extensions.formautofill.available", "on");
  Services.prefs.setBoolPref(
    "extensions.formautofill.creditCards.available",
    false
  );

  // Simulate a migration.
  gBrowserGlue.observe(
    null,
    TOPIC_BROWSERGLUE_TEST,
    TOPICDATA_BROWSERGLUE_TEST
  );

  // old credit card available should migrate to "off" due to
  // "extensions.formautofill.available" being on and
  // "extensions.formautofill.creditCards.available" having a user set value of false.
  Assert.equal(
    Services.prefs.getCharPref("extensions.formautofill.creditCards.supported"),
    "off"
  );

  ensureOldPrefsAreCleared();
});
