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
// The read-back uses the synchronous Services.storage API, since
// Sqlite.sys.mjs refuses new connections during profile-before-change.

"use strict";

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const { AsyncShutdown } = ChromeUtils.importESModule(
  "resource://gre/modules/AsyncShutdown.sys.mjs"
);

let gProfileDir = null;
let gCacheFile = null;

add_setup({ skip_if: () => AppConstants.MOZ_SYSTEM_NSS }, async () => {
  const { HttpServer } = ChromeUtils.importESModule(
    "resource://testing-common/httpd.sys.mjs"
  );

  // head_http3.js and head_trr.js call do_get_profile() at load time, setting
  // _profileInitialized = true.  A subsequent do_get_profile(true) early-
  // returns without firing profile-after-change.  Fire it explicitly so that
  // SSLTokensCache::Observe() sets up persistence and schedules the
  // shutdown blocker.
  gProfileDir = do_get_profile();
  gCacheFile = PathUtils.join(gProfileDir.path, "ssl_tokens_cache.sqlite");
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

// Row count via a throwaway connection, or null if the file/table isn't there
// yet or is briefly locked by a concurrent write.
function readCacheRowCount() {
  let file = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
  file.initWithPath(gCacheFile);
  if (!file.exists()) {
    return null;
  }
  let conn;
  try {
    conn = Services.storage.openDatabase(file);
  } catch (e) {
    return null;
  }
  try {
    let stmt = conn.createStatement("SELECT COUNT(*) FROM ssl_tokens");
    try {
      stmt.executeStep();
      return stmt.getInt32(0);
    } finally {
      stmt.reset();
      stmt.finalize();
    }
  } catch (e) {
    return null;
  } finally {
    conn.close();
  }
}

// Polls the row count until it reaches aMinCount or times out, returning the
// last count seen.
async function waitForCacheRows(aMinCount = 1) {
  let lastCount = 0;
  for (let i = 0; i < 50; i++) {
    let count = readCacheRowCount();
    if (count !== null) {
      lastCount = count;
      if (count >= aMinCount) {
        return count;
      }
    }
    await new Promise(resolve => do_timeout(100, resolve));
  }
  return lastCount;
}

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
    Services.prefs.setBoolPref("toolkit.asyncshutdown.testing", true);
    registerCleanupFunction(() =>
      Services.prefs.clearUserPref("toolkit.asyncshutdown.testing")
    );
    await AsyncShutdown.profileBeforeChange._trigger();

    ok(
      await IOUtils.exists(gCacheFile),
      "ssl_tokens_cache.sqlite exists after profile-before-change"
    );
    Assert.greaterOrEqual(
      await waitForCacheRows(1),
      1,
      "ssl_tokens_cache.sqlite has rows after profile-before-change"
    );
  }
);
