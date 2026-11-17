/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);
const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const COLLECTION_NAME = "safebrowsing-v5-rt-key";
const KEY_CHANGED_TOPIC = "safebrowsing-realtime-key-changed";

function makeKeyConfig(keyId, fill) {
  return new Uint8Array([
    keyId,
    0x00,
    0x20,
    ...new Array(32).fill(fill),
    0x00,
    0x04,
    0x00,
    0x01,
    0x00,
    0x01,
  ]);
}

function makeRecord(keyId, config) {
  return { id: "latest", key_id: keyId, key_config: config.toBase64() };
}

function getKeyConfig() {
  let service = Cc[
    "@mozilla.org/url-classifier/realtime-key-service;1"
  ].getService(Ci.nsIUrlClassifierRealTimeKeyService);
  return service.getKeyConfig();
}

let lastModified = 42;

async function emitSync(current) {
  let client = RemoteSettings(COLLECTION_NAME);
  await client.db.importChanges({}, ++lastModified, current, { clear: true });
  await client.emit("sync", {
    data: { current, created: [], updated: [], deleted: [] },
  });
}

add_setup(async function () {
  let db = RemoteSettings(COLLECTION_NAME).db;
  await db.importChanges({}, lastModified, [
    makeRecord(1, makeKeyConfig(1, 0xaa)),
  ]);
});

add_task(async function test_retry_after_failed_load() {
  let stub = sinon.stub(RemoteSettings(COLLECTION_NAME), "get");
  stub.onFirstCall().resolves([]);
  stub.callThrough();
  try {
    Assert.strictEqual(
      await getKeyConfig(),
      null,
      "Resolves to null when the load fails"
    );
    Assert.deepEqual(
      await getKeyConfig(),
      makeKeyConfig(1, 0xaa),
      "Retries the load on the next call"
    );
    await getKeyConfig();
    Assert.equal(stub.callCount, 2, "Does not reload once the key is loaded");
  } finally {
    stub.restore();
  }
});

add_task(async function test_local_record() {
  let config = await getKeyConfig();
  Assert.equal(
    ChromeUtils.getClassName(config),
    "Uint8Array",
    "Resolves to a Uint8Array"
  );
  Assert.deepEqual(
    config,
    makeKeyConfig(1, 0xaa),
    "Returns the decoded key config from the local record"
  );
});

add_task(async function test_result_is_a_copy() {
  let config = await getKeyConfig();
  config.fill(0);
  Assert.deepEqual(
    await getKeyConfig(),
    makeKeyConfig(1, 0xaa),
    "Modifying a returned key config does not change later results"
  );
});

add_task(async function test_sync_replaces_config() {
  let changed = TestUtils.topicObserved(KEY_CHANGED_TOPIC);
  await emitSync([makeRecord(2, makeKeyConfig(2, 0xbb))]);
  await changed;
  Assert.deepEqual(
    await getKeyConfig(),
    makeKeyConfig(2, 0xbb),
    "Sync replaces the key config"
  );
});

add_task(async function test_unchanged_key_does_not_notify() {
  let notifications = 0;
  let observer = () => notifications++;
  Services.obs.addObserver(observer, KEY_CHANGED_TOPIC);
  try {
    await emitSync([makeRecord(2, makeKeyConfig(2, 0xbb))]);
    Assert.equal(notifications, 0, "Re-syncing the same key does not notify");

    await emitSync([makeRecord(7, makeKeyConfig(7, 0xbb))]);
    Assert.equal(notifications, 1, "Syncing a different key notifies once");
  } finally {
    Services.obs.removeObserver(observer, KEY_CHANGED_TOPIC);
  }
});

add_task(async function test_key_id_mismatch() {
  let changed = TestUtils.topicObserved(KEY_CHANGED_TOPIC);
  await emitSync([makeRecord(3, makeKeyConfig(4, 0xcc))]);
  await changed;
  Assert.strictEqual(
    await getKeyConfig(),
    null,
    "key_id not matching the first config byte is rejected"
  );
});

add_task(async function test_invalid_base64() {
  await emitSync([makeRecord(5, makeKeyConfig(5, 0xdd))]);
  Assert.ok(await getKeyConfig(), "Valid again");

  await emitSync([{ id: "latest", key_id: 5, key_config: "not base64!" }]);
  Assert.strictEqual(await getKeyConfig(), null, "Invalid base64 is rejected");
});

add_task(async function test_record_deleted() {
  await emitSync([makeRecord(6, makeKeyConfig(6, 0xee))]);
  Assert.ok(await getKeyConfig(), "Valid again");

  await emitSync([]);
  Assert.strictEqual(
    await getKeyConfig(),
    null,
    "Missing latest record gives null"
  );
});
