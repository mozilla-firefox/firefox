/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests for Bug 2035453: SSLTokensCache persistence.
//
// Verifies ssl_tokens_cache.sqlite is written for each of the three write
// triggers: "idle-daily", "application-background", and profile-before-change
// via AsyncShutdown.
//
// The DB file is created as soon as persistence activates, before any token is
// written, so each test polls the row count rather than checking existence.
// The read-back uses the Services.storage API, since
// Sqlite.sys.mjs refuses new connections during profile-before-change.

"use strict";

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

add_setup({ skip_if: () => AppConstants.MOZ_SYSTEM_NSS }, async () => {
  const { HttpServer } = ChromeUtils.importESModule(
    "resource://testing-common/httpd.sys.mjs"
  );

  // head_http3.js and head_trr.js call do_get_profile() at load time, setting
  // _profileInitialized = true.  A subsequent do_get_profile(true) early-
  // returns without firing profile-after-change.  Fire it explicitly so that
  // SSLTokensCache::Observe() sets up persistence and schedules the
  // shutdown blocker.
  do_get_profile();
  Services.obs.notifyObservers(
    null,
    "profile-after-change",
    "xpcshell-persist-test"
  );

  // Yield so async setup triggered by the profile-after-change handler
  // (background load, etc.) has a chance to execute.
  await new Promise(resolve => do_timeout(0, resolve));

  let httpServer = new HttpServer();
  httpServer.registerPathHandler("/", (req, resp) => {
    resp.setStatusLine(req.httpVersion, 200, "OK");
    resp.setHeader("Content-Type", "text/plain");
    resp.bodyOutputStream.write("OK", 2);
  });
  httpServer.start(-1);
  registerCleanupFunction(async () => httpServer.stop());

  await asyncSetupFaultyServer(httpServer);
});

// Makes one HTTPS connection to a FaultyServer host.  The first connection
// always succeeds and the server issues a NewSessionTicket (MOZ_TLS_SERVER_0RTT
// is set by asyncSetupFaultyServer), populating the in-memory cache.
async function makeConnection() {
  const kHost = "decrypt-error-on-resume.example.com";
  Services.prefs.setCharPref("network.dns.localDomains", kHost);
  registerCleanupFunction(() =>
    Services.prefs.clearUserPref("network.dns.localDomains")
  );
  let chan = makeChan(`https://${kHost}:8443/`);
  let [, buf] = await channelOpenPromise(chan, CL_ALLOW_UNKNOWN_CL);
  ok(buf, "connection succeeded and NewSessionTicket was issued");
}

async function readCacheRowCount() {
  let values = await queryCacheColumn("SELECT COUNT(*) FROM ssl_tokens");
  return values ? values[0] : null;
}

// Polls the row count until it reaches aMinCount or times out, returning the
// last count seen.
function waitForCacheRows(aMinCount = 1) {
  return pollCache(readCacheRowCount, count => count >= aMinCount, 0);
}

// --- Host removal after the load -----------------------------------------

const kHostA = "host-a.example.com:443";
const kHostB = "host-b.example.com:443";
const kOther = "other.example.com:443";

// Replaces the cache with aKeys (sorted) and waits for the snapshot to write
// them.
async function persistRows(aCache, aTestCache, aKeys) {
  aCache.clearSSLExternalAndInternalSessionCache();
  for (let key of aKeys) {
    aTestCache.putSSLTokenForTest(key);
  }
  Services.obs.notifyObservers(null, "idle-daily");
  await waitForCacheKeys(aKeys);
}

// Persists kHostA, kHostB and kOther, then evicts them from memory so that only
// a DELETE, not a snapshot, can remove rows while kOther survives.
async function setUpEvictedRows(aCache, aTestCache) {
  await persistRows(aCache, aTestCache, [kHostA, kHostB, kOther]);
  Services.prefs.setIntPref("network.ssl_tokens_cache_capacity", 0);
  aTestCache.putSSLTokenForTest("trigger.example.com:443");
  Services.prefs.clearUserPref("network.ssl_tokens_cache_capacity");
  Assert.equal(aTestCache.countSSLTokens(), 0, "all records were evicted");
  Assert.deepEqual(await readCacheKeys(), [kHostA, kHostB, kOther]);
}

function removeHost(aCache, aHost) {
  aCache.removeSSLTokensByHostAndOriginAttributesPattern(
    aHost,
    JSON.stringify({ userContextId: 0 })
  );
}

add_task(
  { skip_if: () => AppConstants.MOZ_SYSTEM_NSS },
  async function test_ssl_token_cache_host_removal_deletes_rows() {
    const kOtherContainer = "host-a.example.com:443^userContextId=1";
    let { cache, testCache } = getCacheServices();

    // The snapshot is queued after the startup load, so the load is done once
    // the rows appear.
    await persistRows(cache, testCache, [kHostA, kOtherContainer, kOther]);

    removeHost(cache, "host-a.example.com");
    Assert.equal(testCache.countSSLTokens(), 2, "memory is updated at once");
    await waitForCacheKeys(
      [kOtherContainer, kOther],
      "the delayed DELETE removes only the matching row"
    );

    // Leave nothing behind for the connection-based tests below.
    cache.clearSSLExternalAndInternalSessionCache();
    await waitForCacheKeys([]);
  }
);

add_task(
  { skip_if: () => AppConstants.MOZ_SYSTEM_NSS },
  async function test_ssl_token_cache_host_removal_deletes_evicted_rows() {
    let { cache, testCache } = getCacheServices();
    await setUpEvictedRows(cache, testCache);

    removeHost(cache, "host-a.example.com");
    await waitForCacheKeys(
      [kHostB, kOther],
      "the delayed DELETE removes the evicted matching row"
    );

    cache.clearSSLExternalAndInternalSessionCache();
    await waitForCacheKeys([]);
  }
);

// Checks that the DB failure that aTrigger causes removes the DB file.
async function checkFailureRemovesDB(aWhat, aTrigger) {
  let { cache, testCache } = getCacheServices();
  await setUpEvictedRows(cache, testCache);

  let removed = TestUtils.topicObserved(kDBRemovedTopic);
  aTrigger(cache, testCache);
  await removed;
  Assert.ok(!getCacheFile().exists(), `the failed ${aWhat} removes the DB`);

  Services.obs.notifyObservers(null, "idle-daily");
  await waitForCacheKeys([], "the next snapshot recreates the DB");
}

add_task(
  { skip_if: () => AppConstants.MOZ_SYSTEM_NSS },
  async function test_ssl_token_cache_host_removal_failed_delete_removes_db() {
    await checkFailureRemovesDB("DELETE", (cache, testCache) => {
      testCache.failDBOpensForTest(1);
      removeHost(cache, "host-a.example.com");
    });
  }
);

add_task(
  { skip_if: () => AppConstants.MOZ_SYSTEM_NSS },
  async function test_ssl_token_cache_host_removal_failed_snapshot_removes_db() {
    await checkFailureRemovesDB("snapshot", (cache, testCache) => {
      removeHost(cache, "host-a.example.com");
      testCache.failDBOpensForTest(1);
      Services.obs.notifyObservers(null, "idle-daily");
    });
  }
);

add_task(
  { skip_if: () => AppConstants.MOZ_SYSTEM_NSS },
  async function test_ssl_token_cache_failed_clear_removes_db() {
    await checkFailureRemovesDB("clear", (cache, testCache) => {
      testCache.failDBOpensForTest(1);
      cache.clearSSLExternalAndInternalSessionCache();
    });
  }
);

// --- Test 1: idle-daily --------------------------------------------------

add_task(
  { skip_if: () => AppConstants.MOZ_SYSTEM_NSS },
  async function test_ssl_token_cache_written_on_idle_daily() {
    await makeConnection();

    Services.obs.notifyObservers(null, "idle-daily");

    Assert.greaterOrEqual(
      await waitForCacheRows(1),
      1,
      "ssl_tokens_cache.sqlite has rows after idle-daily"
    );
  }
);

// --- Test 2: application-background -------------------------------------

add_task(
  { skip_if: () => AppConstants.MOZ_SYSTEM_NSS },
  async function test_ssl_token_cache_written_on_application_background() {
    await makeConnection();

    Services.obs.notifyObservers(null, "application-background");

    Assert.greaterOrEqual(
      await waitForCacheRows(1),
      1,
      "ssl_tokens_cache.sqlite has rows after application-background"
    );
  }
);

// --- Test 3: profile-before-change (on-quit) ----------------------------

add_task(
  { skip_if: () => AppConstants.MOZ_SYSTEM_NSS },
  async function test_ssl_token_cache_written_on_quit() {
    await makeConnection();

    // _trigger() awaits all blockers, so when it resolves SSLTokensCache's
    // BlockShutdown has written the final snapshot.
    await triggerProfileBeforeChange();

    ok(
      await IOUtils.exists(getCacheFile().path),
      "ssl_tokens_cache.sqlite exists after profile-before-change"
    );
    Assert.greaterOrEqual(
      await waitForCacheRows(1),
      1,
      "ssl_tokens_cache.sqlite has rows after profile-before-change"
    );
  }
);
