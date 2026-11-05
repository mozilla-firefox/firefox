/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const {
  getRemoteRecords,
  _setRemoteClientForTesting,
  _clearRemoteClientForTesting,
  _setRecordsIdleMsForTesting,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);

registerCleanupFunction(() => {
  _clearRemoteClientForTesting();
  _setRecordsIdleMsForTesting(null);
});

add_task(async function test_getRemoteRecords_releases_cache_once_idle() {
  let reads = 0;
  let published = [{ id: "before" }];
  _setRemoteClientForTesting({
    get: async () => {
      reads++;
      return published;
    },
  });
  _setRecordsIdleMsForTesting(0);

  try {
    await getRemoteRecords();
    Assert.equal(reads, 1, "The first caller reads the collection");

    published = [{ id: "after" }];
    let records;
    await TestUtils.waitForCondition(async () => {
      records = await getRemoteRecords();
      return records[0].id === "after";
    }, "Wait for the records cache to be released after going idle");

    Assert.equal(
      records[0].id,
      "after",
      "Going idle releases the records rather than holding them for the session"
    );
    Assert.equal(reads, 2, "The released cache costs exactly one re-read");
  } finally {
    _setRecordsIdleMsForTesting(null);
    _clearRemoteClientForTesting();
  }
});

add_task(async function test_getRemoteRecords_keeps_cache_while_active() {
  let reads = 0;
  _setRemoteClientForTesting({
    get: async () => {
      reads++;
      return [{ id: "cached" }];
    },
  });

  try {
    await getRemoteRecords();
    await getRemoteRecords();

    Assert.equal(
      reads,
      1,
      "Repeat reads inside one turn still share a single collection read"
    );
  } finally {
    _clearRemoteClientForTesting();
  }
});
