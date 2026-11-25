/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// A removal in the middle of the startup load drops both the row loaded
// before it and the row loaded after it, in memory and on disk.

"use strict";

// Both match the removal, since the load reads rows in no defined order.
const kKeys = ["anon:host-a.example.com:443", "host-a.example.com:443"];

add_task(async function test_ssl_token_cache_removal_mid_load() {
  let { cache, testCache } = getCacheServices();
  seedCacheRows(kKeys);
  testCache.pauseLoadAfterRowsForTest(1);
  activatePersistence();
  await TestUtils.waitForCondition(
    () => testCache.countSSLTokens() === 1,
    "the load paused after one row"
  );

  cache.removeSSLTokensByHostAndOriginAttributesPattern(
    "host-a.example.com",
    "{}"
  );
  Assert.equal(testCache.countSSLTokens(), 0, "the loaded row is removed");
  await releaseHeldLoad(testCache);

  Assert.equal(testCache.countSSLTokens(), 0, "the later row isn't loaded");
  Assert.deepEqual(await readCacheKeys(), [], "both rows are deleted");
});
