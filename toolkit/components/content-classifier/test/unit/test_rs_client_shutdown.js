/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ContentClassifierRemoteSettingsClient } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/content-classifier/ContentClassifierRemoteSettingsClient.sys.mjs"
);
const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const rs = RemoteSettings("content-classifier-lists");

function makeFakeService() {
  return {
    calls: [],
    onListsChanged(updated, removed) {
      this.calls.push({ updated, removed });
    },
  };
}

add_setup(() => {
  do_get_profile();
  registerCleanupFunction(() => sinon.restore());
});

add_task(async function test_import_notifies_service() {
  sinon.stub(rs, "get").resolves([{ Name: "list-a" }]);

  const client = new ContentClassifierRemoteSettingsClient();
  const service = makeFakeService();
  await client.init(service);

  Assert.deepEqual(
    service.calls,
    [{ updated: ["list-a"], removed: [] }],
    "the initial import notifies the service"
  );

  client.shutdown();
  sinon.restore();
});

add_task(async function test_import_pending_across_shutdown() {
  const { promise, resolve } = Promise.withResolvers();
  sinon.stub(rs, "get").returns(promise);

  const client = new ContentClassifierRemoteSettingsClient();
  const service = makeFakeService();
  const initPromise = client.init(service);

  client.shutdown();
  resolve([{ Name: "list-a" }]);
  await initPromise;

  Assert.deepEqual(
    service.calls,
    [],
    "an import finishing after shutdown() does not notify the service"
  );

  sinon.restore();
});

add_task(async function test_sync_pending_across_shutdown() {
  sinon.stub(rs, "get").resolves([]);

  const client = new ContentClassifierRemoteSettingsClient();
  const service = makeFakeService();
  await client.init(service);
  service.calls = [];

  const { promise, resolve } = Promise.withResolvers();
  sinon.stub(rs.attachments, "deleteDownloaded").returns(promise);

  const syncPromise = client.onSync({
    data: { deleted: [{ Name: "list-b" }] },
  });

  client.shutdown();
  resolve();
  await syncPromise;

  Assert.deepEqual(
    service.calls,
    [],
    "a sync finishing after shutdown() does not notify the service"
  );

  sinon.restore();
});
