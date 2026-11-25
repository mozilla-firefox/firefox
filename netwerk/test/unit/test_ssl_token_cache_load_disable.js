/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Turning persistence off while the startup load is held loads nothing and
// removes the DB; turning it back on doesn't load either.

"use strict";

const kKeys = ["host-a.example.com:443", "other.example.com:443"];

add_task(async function test_ssl_token_cache_disable_during_load() {
  let { testCache } = startHeldLoad(kKeys);

  Services.prefs.setBoolPref("network.ssl_tokens_cache_persistence", false);
  await releaseHeldLoad(testCache);

  Assert.equal(testCache.countSSLTokens(), 0, "nothing was loaded");
  await TestUtils.waitForCondition(
    () => !getCacheFile().exists(),
    "the DB is removed"
  );
});

add_task(async function test_ssl_token_cache_reenable_does_not_load() {
  let { testCache } = getCacheServices();
  seedCacheRows(kKeys);
  Services.prefs.setBoolPref("network.ssl_tokens_cache_persistence", true);

  // The snapshot is queued after any load, so it is empty only if none ran.
  Services.obs.notifyObservers(null, "idle-daily");
  await waitForCacheKeys([], "the snapshot is empty");
  Assert.equal(testCache.countSSLTokens(), 0, "nothing was loaded");
});
