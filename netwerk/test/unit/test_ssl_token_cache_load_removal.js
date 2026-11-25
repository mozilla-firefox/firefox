/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Host and site removals made while the startup load is held also drop the
// matching rows it loads afterwards, in memory and on disk.

"use strict";

const kSiteRemoved = [
  "anon:site-b.org:443",
  "site-b.org:443",
  "sub.site-b.org:443",
];
const kKept = [
  "host-a.example.com:443^userContextId=1",
  "notsite-b.org:443",
  "other.example.com:443",
  "sub.host-a.example.com:443",
];

add_task(async function test_ssl_token_cache_removals_during_load() {
  let { cache, testCache } = startHeldLoad([
    ...kHostARows,
    ...kSiteRemoved,
    ...kKept,
  ]);
  Assert.equal(testCache.countSSLTokens(), 0, "the held load loaded nothing");

  cache.removeSSLTokensByHostAndOriginAttributesPattern(
    "host-a.example.com",
    JSON.stringify({ userContextId: 0 })
  );
  cache.removeSSLTokensBySiteAndOriginAttributesPattern("site-b.org", "{}");
  await releaseHeldLoad(testCache);

  Assert.equal(testCache.countSSLTokens(), 4, "only kKept was loaded");
  Assert.deepEqual(
    await readCacheKeys(),
    kKept,
    "the removed rows are deleted"
  );
});

add_task(async function test_ssl_token_cache_shutdown_after_load_removal() {
  await triggerProfileBeforeChange();
  Assert.deepEqual(
    await readCacheKeys(),
    kKept,
    "the final snapshot lacks the removed rows"
  );
});
