/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests migration of the `quicksuggest.blockedDigests` pref to dismissal keys
// in the Rust component.

"use strict";

const PREF = "browser.urlbar.quicksuggest.blockedDigests";

add_setup(async function () {
  await QuickSuggestTestUtils.ensureQuickSuggestInit();

  Assert.ok(
    !Services.prefs.prefHasDefaultValue(PREF),
    "Sanity check: Pref should not have a default-branch value"
  );
});

add_task(async function notDefined() {
  await doTest(null, []);
});

add_task(async function emptyString() {
  await doTest("", []);
});

add_task(async function invalidJson1() {
  await doTest("bogus", []);
});

add_task(async function invalidJson2() {
  await doTest("{", []);
});

add_task(async function falsey() {
  await doTest(JSON.stringify(null), []);
});

add_task(async function notAnArray1() {
  await doTest(JSON.stringify(123), []);
});

add_task(async function notAnArray2() {
  await doTest(JSON.stringify({}), []);
});

add_task(async function notAnArrayOfStrings() {
  await doTest(JSON.stringify([123, 456]), []);
});

add_task(async function oneKey() {
  // The string values in the array don't actually need to be digests, so don't
  // bother generating actual digests.
  let keys = ["aaa"];
  await doTest(JSON.stringify(keys), keys);
});

add_task(async function manyKeys() {
  let keys = ["aaa", "bbb", "ccc", "ddd", "eee"];
  await doTest(JSON.stringify(keys), keys);
});

add_task(async function manyKeysSomeInvalid() {
  await doTest(JSON.stringify(["aaa", 123, "bbb", 456, "ccc"]), [
    "aaa",
    "bbb",
    "ccc",
  ]);
});

add_task(async function keysAlreadyPresent() {
  await QuickSuggest.rustBackend.dismissByKey("yyy");
  await QuickSuggest.rustBackend.dismissByKey("zzz");
  await doTest(JSON.stringify(["aaa", "bbb", "ccc"]), [
    "aaa",
    "bbb",
    "ccc",
    "yyy",
    "zzz",
  ]);
});

async function doTest(prefValue, expectedDismissalKeys) {
  let { rustBackend } = QuickSuggest;

  // Disable the backend.
  rustBackend.enable(false);

  // Set the pref.
  if (prefValue === null) {
    Services.prefs.clearUserPref(PREF);
    Assert.ok(
      !Services.prefs.prefHasUserValue(PREF),
      "Sanity check: Pref should not have user value after clearing it"
    );
  } else {
    Services.prefs.setCharPref(PREF, prefValue);
    Assert.ok(
      Services.prefs.prefHasUserValue(PREF),
      "Sanity check: Pref should have user value after setting it"
    );
  }

  // Enable the backend, which will trigger migration.
  rustBackend.enable(true);

  // This test doesn't ingest anything but make sure the backend isn't doing any
  // async activity past this point.
  await rustBackend._test_ingest();

  await TestUtils.waitForCondition(
    () => !Services.prefs.prefHasUserValue(PREF),
    "Waiting for the Rust backend to complete the dismissals migration"
  );
  if (Services.prefs.prefHasUserValue(PREF)) {
    Assert.ok(false, "The backend should have completed the migration");
    return;
  }

  // Check the newly recorded dismissal keys.
  Assert.equal(
    await rustBackend.anyDismissedSuggestions(),
    !!expectedDismissalKeys.length,
    "anyDismissedSuggestions should match expectedDismissalKeys"
  );
  for (let key of expectedDismissalKeys) {
    Assert.ok(
      await rustBackend.isDismissedByKey(key),
      "Dismissal key should be recorded: " + key
    );
  }

  await rustBackend.clearDismissedSuggestions();
}
