/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

// This file tests that cert_storage does not look for obsolete entries in a
// database that it has marked as already pruned (see
// test_cert_storage_prune.js).

add_task(async function test_marked_database_is_not_pruned() {
  let entries = [
    [PRUNED_MARKER.key, PRUNED_MARKER.value],
    [
      bytes("is", "issuer 1", "serial 1"),
      i64Value(Ci.nsICertStorage.STATE_UNSET),
    ],
    [
      bytes("crlite", "subject 1", "pubKeyHash 1"),
      i64Value(Ci.nsICertStorage.STATE_ENFORCE),
    ],
    [bytes("subject", "subject 2"), blobValue([])],
  ];
  await writeDatabase(entries);
  await openDatabase();

  let stored = await readDatabase();
  Assert.equal(stored.size, entries.length, "should not remove any entries");
  for (let [key, value] of entries) {
    Assert.equal(
      stored.get(hex(key)),
      hex(value),
      "should keep every entry unchanged"
    );
  }
});
