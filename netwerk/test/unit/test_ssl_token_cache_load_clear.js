/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Clearing while the startup load is held discards everything it loads and
// empties the persisted cache.

"use strict";

add_task(async function test_ssl_token_cache_clear_during_load() {
  let { cache, testCache } = startHeldLoad([
    "host-a.example.com:443",
    "other.example.com:443",
  ]);

  cache.clearSSLExternalAndInternalSessionCache();
  await releaseHeldLoad(testCache);

  Assert.equal(testCache.countSSLTokens(), 0, "nothing was loaded");
  await waitForCacheKeys([], "the DB is emptied");
});
