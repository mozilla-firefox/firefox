/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Removals pending at shutdown don't survive on disk when the final snapshot
// can't open the DB.

"use strict";

const kKept = ["other.example.com:443"];

add_task(async function test_ssl_token_cache_failed_shutdown_snapshot() {
  let { cache, testCache } = startHeldLoad([...kHostARows, ...kKept]);
  await releaseHeldLoad(testCache);

  cache.removeSSLTokensByHostAndOriginAttributesPattern(
    "host-a.example.com",
    "{}"
  );
  testCache.failDBOpensForTest(1000);
  await triggerProfileBeforeChange();

  let keys = (await readCacheKeys()) ?? [];
  Assert.deepEqual(
    keys.filter(k => kHostARows.includes(k)),
    [],
    "no removed row survives shutdown"
  );
});
