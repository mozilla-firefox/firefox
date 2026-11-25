/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Shutdown releases a held startup load, which still applies the removals
// made while it was held before the final snapshot.

"use strict";

const kKept = ["other.example.com:443"];

add_task(async function test_ssl_token_cache_shutdown_releases_held_load() {
  let { cache } = startHeldLoad([...kHostARows, ...kKept]);

  cache.removeSSLTokensByHostAndOriginAttributesPattern(
    "host-a.example.com",
    "{}"
  );
  let loaded = TestUtils.topicObserved(kLoadCompleteTopic);
  await triggerProfileBeforeChange();
  await loaded;

  Assert.deepEqual(
    await readCacheKeys(),
    kKept,
    "the final snapshot lacks the removed rows"
  );
});
