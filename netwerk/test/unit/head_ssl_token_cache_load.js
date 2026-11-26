/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Helpers for the tests of changes made during the SSLTokensCache startup load.

/* import-globals-from head_channels.js */

"use strict";

const { AsyncShutdown } = ChromeUtils.importESModule(
  "resource://gre/modules/AsyncShutdown.sys.mjs"
);
const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

const kLoadCompleteTopic = "ssl-tokens-cache-load-complete-for-test";
const kDBRemovedTopic = "ssl-tokens-cache-db-removed-for-test";
// Rows of host-a.example.com in the default user context.
const kHostARows = ["anon:host-a.example.com:443", "host-a.example.com:443"];

function getCacheFile() {
  let file = do_get_profile().clone();
  file.append("ssl_tokens_cache.sqlite");
  return file;
}

let gPersistenceActivated = false;

// Writes unexpired rows for aKeys; the load stores payloads undecoded. Only
// while the cache cannot be using the DB, so the write never contends with it.
function seedCacheRows(aKeys) {
  Assert.ok(
    !gPersistenceActivated ||
      !Services.prefs.getBoolPref("network.ssl_tokens_cache_persistence"),
    "seeding before activation or while persistence is off"
  );
  let conn = Services.storage.openUnsharedDatabase(getCacheFile());
  try {
    if (!conn.tableExists("ssl_tokens")) {
      conn.schemaVersion = 3;
      conn.executeSimpleSQL(
        "CREATE TABLE ssl_tokens (key TEXT NOT NULL," +
          "expiration_time INTEGER NOT NULL," +
          "overridable_error INTEGER NOT NULL," +
          "cert_hashes BLOB," +
          "payload BLOB NOT NULL);" +
          "CREATE INDEX ssl_tokens_key ON ssl_tokens (key);" +
          "CREATE TABLE ssl_certs (hash BLOB PRIMARY KEY NOT NULL," +
          "der BLOB NOT NULL)"
      );
    }
    let stmt = conn.createStatement(
      "INSERT INTO ssl_tokens (key, expiration_time, overridable_error, " +
        "payload) VALUES (:key, :expiration_time, 0, :payload)"
    );
    try {
      for (let key of aKeys) {
        stmt.bindByName("key", key);
        stmt.bindByName("expiration_time", (Date.now() + 3600 * 1000) * 1000);
        // A payload starts with its uncompressed size as a 4-byte
        // little-endian integer, which the load checks against the size limit.
        // The load never decompresses, so the bytes after it can be anything.
        stmt.bindBlobByName("payload", [4, 0, 0, 0, 0xde, 0xad, 0xbe, 0xef]);
        stmt.execute();
      }
    } finally {
      stmt.finalize();
    }
  } finally {
    conn.close();
  }
}

// First-column values of aSql via a throwaway connection, or null if the
// file/table isn't there yet or is briefly locked by a concurrent write. The
// connection is unshared, since a shared-cache one joins the cache's own in
// debug builds and makes its writes fail with SQLITE_LOCKED.
async function queryCacheColumn(aSql) {
  let file = getCacheFile();
  if (!file.exists()) {
    return null;
  }
  let conn = await new Promise(resolve =>
    Services.storage.openAsyncDatabase(
      file,
      Ci.mozIStorageService.OPEN_READONLY,
      Ci.mozIStorageService.CONNECTION_DEFAULT,
      (status, connection) =>
        resolve(
          Components.isSuccessCode(status)
            ? connection.QueryInterface(Ci.mozIStorageAsyncConnection)
            : null
        )
    )
  );
  if (!conn) {
    return null;
  }
  try {
    return await new Promise(resolve => {
      let values = [];
      let stmt;
      try {
        stmt = conn.createAsyncStatement(aSql);
      } catch (e) {
        resolve(null);
        return;
      }
      stmt.executeAsync({
        handleResult(aResultSet) {
          for (let row; (row = aResultSet.getNextRow()); ) {
            values.push(row.getResultByIndex(0));
          }
        },
        handleError() {},
        handleCompletion(aReason) {
          stmt.finalize();
          resolve(
            aReason == Ci.mozIStorageStatementCallback.REASON_FINISHED
              ? values
              : null
          );
        },
      });
    });
  } finally {
    await new Promise(resolve => conn.asyncClose(resolve));
  }
}

// Sorted row keys, or null like queryCacheColumn().
function readCacheKeys() {
  return queryCacheColumn("SELECT key FROM ssl_tokens ORDER BY key");
}

// Polls aRead() until aDone() accepts its value or times out, returning the
// last non-null value seen, or aInitial.
async function pollCache(aRead, aDone, aInitial) {
  let last = aInitial;
  for (let i = 0; i < 50; i++) {
    let value = await aRead();
    if (value !== null) {
      last = value;
      if (aDone(value)) {
        return value;
      }
    }
    await new Promise(resolve => do_timeout(100, resolve));
  }
  return last;
}

// Polls the row keys until they equal aExpected or times out, then asserts
// that the last keys seen equal aExpected.
async function waitForCacheKeys(aExpected, aMessage) {
  let lastKeys = await pollCache(
    readCacheKeys,
    keys => JSON.stringify(keys) === JSON.stringify(aExpected),
    null
  );
  Assert.deepEqual(lastKeys, aExpected, aMessage);
}

// Lets a load held with holdLoadForTest() proceed and waits for it to finish.
async function releaseHeldLoad(aTestCache) {
  let loaded = TestUtils.topicObserved(kLoadCompleteTopic);
  aTestCache.holdLoadForTest(false);
  await loaded;
}

// Runs the startup activation, the only one that loads from disk.
function activatePersistence() {
  gPersistenceActivated = true;
  Services.obs.notifyObservers(null, "profile-after-change", "xpcshell-test");
}

// Seeds aKeys, then runs the startup activation with its load held.
function startHeldLoad(aKeys) {
  let services = getCacheServices();
  seedCacheRows(aKeys);
  services.testCache.holdLoadForTest(true);
  activatePersistence();
  return services;
}

async function triggerProfileBeforeChange() {
  Services.prefs.setBoolPref("toolkit.asyncshutdown.testing", true);
  registerCleanupFunction(() =>
    Services.prefs.clearUserPref("toolkit.asyncshutdown.testing")
  );
  await AsyncShutdown.profileBeforeChange._trigger();
}
