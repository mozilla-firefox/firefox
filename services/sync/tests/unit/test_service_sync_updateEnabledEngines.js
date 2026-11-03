/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const { Service } = ChromeUtils.importESModule(
  "resource://services-sync/service.sys.mjs"
);

const { EngineSynchronizer } = ChromeUtils.importESModule(
  "resource://services-sync/stages/enginesync.sys.mjs"
);

function QuietStore() {
  Store.call("Quiet");
}
QuietStore.prototype = {
  async getAllIDs() {
    return [];
  },
};

function SteamEngine() {
  SyncEngine.call(this, "Steam", Service);
}
SteamEngine.prototype = {
  // We're not interested in engine sync but what the service does.
  _storeObj: QuietStore,

  _sync: async function _sync() {
    await this._syncStartup();
  },
};
Object.setPrototypeOf(SteamEngine.prototype, SyncEngine.prototype);

function StirlingEngine() {
  SyncEngine.call(this, "Stirling", Service);
}
StirlingEngine.prototype = {
  // This engine's enabled state is the same as the SteamEngine's.
  get prefName() {
    return "steam";
  },
};
Object.setPrototypeOf(StirlingEngine.prototype, SteamEngine.prototype);

function PetrolEngine() {
  SyncEngine.call(this, "Petrol", Service);
}
Object.setPrototypeOf(PetrolEngine.prototype, SteamEngine.prototype);

// Tracking info/collections.
var collectionsHelper = track_collections_helper();
var upd = collectionsHelper.with_updated_collection;

function sync_httpd_setup(handlers) {
  handlers["/1.1/johndoe/info/collections"] = collectionsHelper.handler;
  delete collectionsHelper.collections.crypto;
  delete collectionsHelper.collections.meta;

  let cr = new ServerWBO("keys");
  handlers["/1.1/johndoe/storage/crypto/keys"] = upd("crypto", cr.handler());

  let cl = new ServerCollection();
  handlers["/1.1/johndoe/storage/clients"] = upd("clients", cl.handler());

  return httpd_setup(handlers);
}

async function setUp(server) {
  await SyncTestingInfrastructure(server, "johndoe", "ilovejane");
  // Ensure that the server has valid keys so that logging in will work and not
  // result in a server wipe, rendering many of these tests useless.
  await generateNewKeys(Service.collectionKeys);
  let serverKeys = Service.collectionKeys.asWBO("crypto", "keys");
  await serverKeys.encrypt(Service.identity.syncKeyBundle);
  let { success } = await serverKeys.upload(
    Service.resource(Service.cryptoKeysURL)
  );
  ok(success);
}

const PAYLOAD = 42;

add_task(async function setup() {
  await Service.engineManager.clear();
  validate_all_future_pings();

  await Service.engineManager.register(SteamEngine);
  await Service.engineManager.register(StirlingEngine);
});

add_task(async function test_newAccount() {
  enableValidationPrefs();

  _("Test: New account does not disable locally enabled engines.");
  let engine = Service.engineManager.get("steam");
  let server = sync_httpd_setup({
    "/1.1/johndoe/storage/meta/global": new ServerWBO("global", {}).handler(),
    "/1.1/johndoe/storage/steam": new ServerWBO("steam", {}).handler(),
  });
  await setUp(server);

  try {
    _("Engine is enabled from the beginning.");
    Service._ignorePrefObserver = true;
    engine.enabled = true;
    Service._ignorePrefObserver = false;

    _("Sync.");
    await Service.sync();

    _("Engine continues to be enabled.");
    Assert.ok(engine.enabled);
  } finally {
    await Service.startOver();
    await promiseStopServer(server);
  }
});

add_task(async function test_enabledLocally() {
  enableValidationPrefs();

  _("Test: Engine is disabled on remote clients and enabled locally");
  Service.syncID = "abcdefghij";
  let engine = Service.engineManager.get("steam");
  let metaWBO = new ServerWBO("global", {
    syncID: Service.syncID,
    storageVersion: STORAGE_VERSION,
    engines: {},
  });
  let server = sync_httpd_setup({
    "/1.1/johndoe/storage/meta/global": metaWBO.handler(),
    "/1.1/johndoe/storage/steam": new ServerWBO("steam", {}).handler(),
  });
  await setUp(server);

  try {
    _("Enable engine locally.");
    engine.enabled = true;

    _("Sync.");
    await Service.sync();

    _("Meta record now contains the new engine.");
    Assert.ok(!!metaWBO.data.engines.steam);

    _("Engine continues to be enabled.");
    Assert.ok(engine.enabled);
  } finally {
    await Service.startOver();
    await promiseStopServer(server);
  }
});

add_task(async function test_disabledLocally() {
  enableValidationPrefs();

  _("Test: Engine is enabled on remote clients and disabled locally");
  Service.syncID = "abcdefghij";
  let engine = Service.engineManager.get("steam");
  let syncID = await engine.resetLocalSyncID();
  let metaWBO = new ServerWBO("global", {
    syncID: Service.syncID,
    storageVersion: STORAGE_VERSION,
    engines: { steam: { syncID, version: engine.version } },
  });
  let steamCollection = new ServerWBO("steam", PAYLOAD);

  let server = sync_httpd_setup({
    "/1.1/johndoe/storage/meta/global": metaWBO.handler(),
    "/1.1/johndoe/storage/steam": steamCollection.handler(),
  });
  await setUp(server);

  try {
    _("Disable engine locally.");
    Service._ignorePrefObserver = true;
    engine.enabled = true;
    Service._ignorePrefObserver = false;
    engine.enabled = false;

    _("Sync.");
    await Service.sync();

    _("Meta record no longer contains engine.");
    Assert.ok(!metaWBO.data.engines.steam);

    _("Server records are wiped.");
    Assert.equal(steamCollection.payload, undefined);

    _("Engine continues to be disabled.");
    Assert.ok(!engine.enabled);
  } finally {
    await Service.startOver();
    await promiseStopServer(server);
  }
});

add_task(async function test_disabledLocally_wipe503() {
  enableValidationPrefs();

  _("Test: Engine is enabled on remote clients and disabled locally");
  Service.syncID = "abcdefghij";
  let engine = Service.engineManager.get("steam");
  let syncID = await engine.resetLocalSyncID();
  let metaWBO = new ServerWBO("global", {
    syncID: Service.syncID,
    storageVersion: STORAGE_VERSION,
    engines: { steam: { syncID, version: engine.version } },
  });

  function service_unavailable(request, response) {
    let body = "Service Unavailable";
    response.setStatusLine(request.httpVersion, 503, "Service Unavailable");
    response.setHeader("Retry-After", "23");
    response.bodyOutputStream.write(body, body.length);
  }

  let server = sync_httpd_setup({
    "/1.1/johndoe/storage/meta/global": metaWBO.handler(),
    "/1.1/johndoe/storage/steam": service_unavailable,
  });
  await setUp(server);

  _("Disable engine locally.");
  Service._ignorePrefObserver = true;
  engine.enabled = true;
  Service._ignorePrefObserver = false;
  engine.enabled = false;

  _("Sync.");
  await Service.sync();
  Assert.equal(Service.status.sync, SERVER_MAINTENANCE);

  await Service.startOver();
  await promiseStopServer(server);
});

add_task(async function test_enabledRemotely() {
  enableValidationPrefs();

  _("Test: Engine is disabled locally and enabled on a remote client");
  Service.syncID = "abcdefghij";
  let engine = Service.engineManager.get("steam");
  let syncID = await engine.resetLocalSyncID();
  let metaWBO = new ServerWBO("global", {
    syncID: Service.syncID,
    storageVersion: STORAGE_VERSION,
    engines: { steam: { syncID, version: engine.version } },
  });
  let server = sync_httpd_setup({
    "/1.1/johndoe/storage/meta/global": upd("meta", metaWBO.handler()),

    "/1.1/johndoe/storage/steam": upd(
      "steam",
      new ServerWBO("steam", {}).handler()
    ),
  });
  await setUp(server);

  // We need to be very careful how we do this, so that we don't trigger a
  // fresh start!
  try {
    _("Upload some keys to avoid a fresh start.");
    let wbo = await Service.collectionKeys.generateNewKeysWBO();
    await wbo.encrypt(Service.identity.syncKeyBundle);
    Assert.equal(
      200,
      (await wbo.upload(Service.resource(Service.cryptoKeysURL))).status
    );

    _("Engine is disabled.");
    Assert.ok(!engine.enabled);

    _("Sync.");
    await Service.sync();

    _("Engine is enabled.");
    Assert.ok(engine.enabled);

    _("Meta record still present.");
    Assert.equal(metaWBO.data.engines.steam.syncID, await engine.getSyncID());
  } finally {
    await Service.startOver();
    await promiseStopServer(server);
  }
});

add_task(async function test_disabledRemotelyTwoClients() {
  enableValidationPrefs();

  _(
    "Test: Engine is enabled locally and disabled on a remote client... with two clients."
  );
  Service.syncID = "abcdefghij";
  let engine = Service.engineManager.get("steam");
  let metaWBO = new ServerWBO("global", {
    syncID: Service.syncID,
    storageVersion: STORAGE_VERSION,
    engines: {},
  });
  let server = sync_httpd_setup({
    "/1.1/johndoe/storage/meta/global": upd("meta", metaWBO.handler()),

    "/1.1/johndoe/storage/steam": upd(
      "steam",
      new ServerWBO("steam", {}).handler()
    ),
  });
  await setUp(server);

  try {
    _("Enable engine locally.");
    Service._ignorePrefObserver = true;
    engine.enabled = true;
    Service._ignorePrefObserver = false;

    _("Sync.");
    await Service.sync();

    _("Disable engine by deleting from meta/global.");
    let d = metaWBO.data;
    delete d.engines.steam;
    metaWBO.payload = JSON.stringify(d);
    metaWBO.modified = Date.now() / 1000;

    _("Add a second client and verify that the local pref is changed.");
    Service.clientsEngine._store._remoteClients.foobar = {
      name: "foobar",
      type: "desktop",
    };
    await Service.sync();

    _("Engine is disabled.");
    Assert.ok(!engine.enabled);
  } finally {
    await Service.startOver();
    await promiseStopServer(server);
  }
});

add_task(async function test_disabledRemotely() {
  enableValidationPrefs();

  _("Test: Engine is enabled locally and disabled on a remote client");
  Service.syncID = "abcdefghij";
  let engine = Service.engineManager.get("steam");
  let metaWBO = new ServerWBO("global", {
    syncID: Service.syncID,
    storageVersion: STORAGE_VERSION,
    engines: {},
  });
  let server = sync_httpd_setup({
    "/1.1/johndoe/storage/meta/global": metaWBO.handler(),
    "/1.1/johndoe/storage/steam": new ServerWBO("steam", {}).handler(),
  });
  await setUp(server);

  try {
    _("Enable engine locally.");
    Service._ignorePrefObserver = true;
    engine.enabled = true;
    Service._ignorePrefObserver = false;

    _("Sync.");
    await Service.sync();

    _("Engine is not disabled: only one client.");
    Assert.ok(engine.enabled);
  } finally {
    await Service.startOver();
    await promiseStopServer(server);
  }
});

add_task(async function test_dependentEnginesEnabledLocally() {
  enableValidationPrefs();

  _("Test: Engine is disabled on remote clients and enabled locally");
  Service.syncID = "abcdefghij";
  let steamEngine = Service.engineManager.get("steam");
  let stirlingEngine = Service.engineManager.get("stirling");
  let metaWBO = new ServerWBO("global", {
    syncID: Service.syncID,
    storageVersion: STORAGE_VERSION,
    engines: {},
  });
  let server = sync_httpd_setup({
    "/1.1/johndoe/storage/meta/global": metaWBO.handler(),
    "/1.1/johndoe/storage/steam": new ServerWBO("steam", {}).handler(),
    "/1.1/johndoe/storage/stirling": new ServerWBO("stirling", {}).handler(),
  });
  await setUp(server);

  try {
    _("Enable engine locally. Doing it on one is enough.");
    steamEngine.enabled = true;

    _("Sync.");
    await Service.sync();

    _("Meta record now contains the new engines.");
    Assert.ok(!!metaWBO.data.engines.steam);
    Assert.ok(!!metaWBO.data.engines.stirling);

    _("Engines continue to be enabled.");
    Assert.ok(steamEngine.enabled);
    Assert.ok(stirlingEngine.enabled);
  } finally {
    await Service.startOver();
    await promiseStopServer(server);
  }
});

add_task(async function test_dependentEnginesDisabledLocally() {
  enableValidationPrefs();

  _(
    "Test: Two dependent engines are enabled on remote clients and disabled locally"
  );
  Service.syncID = "abcdefghij";
  let steamEngine = Service.engineManager.get("steam");
  let steamSyncID = await steamEngine.resetLocalSyncID();
  let stirlingEngine = Service.engineManager.get("stirling");
  let stirlingSyncID = await stirlingEngine.resetLocalSyncID();
  let metaWBO = new ServerWBO("global", {
    syncID: Service.syncID,
    storageVersion: STORAGE_VERSION,
    engines: {
      steam: { syncID: steamSyncID, version: steamEngine.version },
      stirling: { syncID: stirlingSyncID, version: stirlingEngine.version },
    },
  });

  let steamCollection = new ServerWBO("steam", PAYLOAD);
  let stirlingCollection = new ServerWBO("stirling", PAYLOAD);

  let server = sync_httpd_setup({
    "/1.1/johndoe/storage/meta/global": metaWBO.handler(),
    "/1.1/johndoe/storage/steam": steamCollection.handler(),
    "/1.1/johndoe/storage/stirling": stirlingCollection.handler(),
  });
  await setUp(server);

  try {
    _("Disable engines locally. Doing it on one is enough.");
    Service._ignorePrefObserver = true;
    steamEngine.enabled = true;
    Assert.ok(stirlingEngine.enabled);
    Service._ignorePrefObserver = false;
    steamEngine.enabled = false;
    Assert.ok(!stirlingEngine.enabled);

    _("Sync.");
    await Service.sync();

    _("Meta record no longer contains engines.");
    Assert.ok(!metaWBO.data.engines.steam);
    Assert.ok(!metaWBO.data.engines.stirling);

    _("Server records are wiped.");
    Assert.equal(steamCollection.payload, undefined);
    Assert.equal(stirlingCollection.payload, undefined);

    _("Engines continue to be disabled.");
    Assert.ok(!steamEngine.enabled);
    Assert.ok(!stirlingEngine.enabled);
  } finally {
    await Service.startOver();
    await promiseStopServer(server);
  }
});

add_task(async function test_service_updateLocalEnginesState() {
  Service.syncID = "abcdefghij";
  const engine = Service.engineManager.get("steam");
  const metaWBO = new ServerWBO("global", {
    syncID: Service.syncID,
    storageVersion: STORAGE_VERSION,
    declined: ["steam"],
    engines: {},
  });
  const server = httpd_setup({
    "/1.1/johndoe/storage/meta/global": metaWBO.handler(),
  });
  await SyncTestingInfrastructure(server, "johndoe");

  // Disconnect sync.
  await Service.startOver();
  Service._ignorePrefObserver = true;
  // Steam engine is enabled on our machine.
  engine.enabled = true;
  Service._ignorePrefObserver = false;
  Service.identity._findCluster = () => server.baseURI + "/1.1/johndoe/";

  // Update engine state from the server.
  await Service.updateLocalEnginesState();
  // Now disabled.
  Assert.ok(!engine.enabled);
});

add_task(async function test_service_enableAfterUpdateState() {
  Service.syncID = "abcdefghij";
  const engine = Service.engineManager.get("steam");
  const metaWBO = new ServerWBO("global", {
    syncID: Service.syncID,
    storageVersion: STORAGE_VERSION,
    declined: ["steam"],
    engines: { someengine: {} },
  });
  const server = httpd_setup({
    "/1.1/johndoe/storage/meta/global": metaWBO.handler(),
  });
  await SyncTestingInfrastructure(server, "johndoe");

  // Disconnect sync.
  await Service.startOver();
  Service.identity._findCluster = () => server.baseURI + "/1.1/johndoe/";

  // Update engine state from the server.
  await Service.updateLocalEnginesState();
  // Now disabled, reflecting what's on the server.
  Assert.ok(!engine.enabled);
  // Enable the engine, as though the user selected it via CWTS.
  engine.enabled = true;

  // Do the "reconcile local and remote states" dance.
  let engineSync = new EngineSynchronizer(Service);
  await engineSync._updateEnabledEngines();
  await Service._maybeUpdateDeclined();
  // engine should remain enabled.
  Assert.ok(engine.enabled);
  // engine should no longer appear in declined on the server.
  Assert.deepEqual(metaWBO.data.declined, []);
});

add_task(async function test_service_disableAfterUpdateState() {
  Service.syncID = "abcdefghij";
  const engine = Service.engineManager.get("steam");
  const metaWBO = new ServerWBO("global", {
    syncID: Service.syncID,
    storageVersion: STORAGE_VERSION,
    declined: [],
    engines: { steam: {} },
  });
  const server = httpd_setup({
    "/1.1/johndoe/storage/meta/global": metaWBO.handler(),
  });
  await SyncTestingInfrastructure(server, "johndoe");

  // Disconnect sync.
  await Service.startOver();
  Service.identity._findCluster = () => server.baseURI + "/1.1/johndoe/";

  // Update engine state from the server.
  await Service.updateLocalEnginesState();
  // Now enabled, reflecting what's on the server.
  Assert.ok(engine.enabled);
  // Disable the engine, as though via CWTS.
  engine.enabled = false;

  // Do the "reconcile local and remote states" dance.
  let engineSync = new EngineSynchronizer(Service);
  await engineSync._updateEnabledEngines();
  await Service._maybeUpdateDeclined();
  // engine should remain disabled.
  Assert.ok(!engine.enabled);
  // engine should now appear in declined on the server.
  Assert.deepEqual(metaWBO.data.declined, ["steam"]);
  // and should have been removed from engines.
  Assert.deepEqual(metaWBO.data.engines, {});
});

add_task(async function test_service_updateLocalEnginesState_no_meta_global() {
  Service.syncID = "abcdefghij";
  const engine = Service.engineManager.get("steam");
  // The server doesn't contain /meta/global (sync was never enabled).
  const server = httpd_setup({});
  await SyncTestingInfrastructure(server, "johndoe");

  // Disconnect sync.
  await Service.startOver();
  Service._ignorePrefObserver = true;
  // Steam engine is enabled on our machine.
  engine.enabled = true;
  Service._ignorePrefObserver = false;
  Service.identity._findCluster = () => server.baseURI + "/1.1/johndoe/";

  // Update engine state from the server.
  await Service.updateLocalEnginesState();
  // Still enabled.
  Assert.ok(engine.enabled);
});

const PER_DEVICE_PREF = "services.sync.perDeviceEngineChoices";

function enablePerDeviceSync() {
  Services.prefs.setBoolPref(PER_DEVICE_PREF, true);
}

registerCleanupFunction(() => {
  Services.prefs.clearUserPref(PER_DEVICE_PREF);
});

async function setUpPerDevice(engines, handlers = {}) {
  enableValidationPrefs();
  enablePerDeviceSync();
  let metaWBO = new ServerWBO("global", {
    syncID: Service.syncID,
    storageVersion: STORAGE_VERSION,
    engines,
  });
  let server = sync_httpd_setup({
    "/1.1/johndoe/storage/meta/global": metaWBO.handler(),
    "/1.1/johndoe/storage/steam": new ServerWBO("steam", {}).handler(),
    ...handlers,
  });
  await setUp(server);
  return { metaWBO, server };
}

add_task(async function test_perDevice_disabledLocally_keepsServerData() {
  _("Test: Locally disabling an engine leaves the server alone.");
  Service.syncID = "abcdefghij";
  let engine = Service.engineManager.get("steam");
  let syncID = await engine.resetLocalSyncID();
  let steamCollection = new ServerWBO("steam", PAYLOAD);
  let { metaWBO, server } = await setUpPerDevice(
    { steam: { syncID, version: engine.version } },
    { "/1.1/johndoe/storage/steam": steamCollection.handler() }
  );

  try {
    _("Disable engine locally.");
    Service._ignorePrefObserver = true;
    engine.enabled = true;
    Service._ignorePrefObserver = false;
    engine.enabled = false;

    _("Sync.");
    await Service.sync();

    _("Server records are not wiped.");
    Assert.equal(steamCollection.payload, PAYLOAD);

    _("Meta record still contains the engine.");
    Assert.ok(!!metaWBO.data.engines.steam);

    _("Engine continues to be disabled.");
    Assert.ok(!engine.enabled);
  } finally {
    await Service.startOver();
    await promiseStopServer(server);
  }
});

add_task(async function test_perDevice_reenablingResetsLastSync() {
  _("Test: Changes made while disabled are uploaded after re-enabling.");
  enablePerDeviceSync();
  let engine = Service.engineManager.get("steam");
  try {
    engine.enabled = true;
    await engine.setLastSync(1234);
    engine.enabled = false;
    engine.enabled = true;
    await TestUtils.waitForCondition(
      async () => (await engine.getLastSync()) == 0,
      "The engine uploads everything on its next sync."
    );
  } finally {
    await Service.startOver();
  }
});

add_task(async function test_perDevice_enabledLocally_stillPublishesEngine() {
  enableValidationPrefs();
  enablePerDeviceSync();

  _("Test: A locally enabled engine is still advertised in meta/global.");
  Service.syncID = "abcdefghij";
  let engine = Service.engineManager.get("steam");
  let metaWBO = new ServerWBO("global", {
    syncID: Service.syncID,
    storageVersion: STORAGE_VERSION,
    engines: {},
  });
  let server = sync_httpd_setup({
    "/1.1/johndoe/storage/meta/global": metaWBO.handler(),
    "/1.1/johndoe/storage/steam": new ServerWBO("steam", {}).handler(),
  });
  await setUp(server);

  try {
    _("Enable engine locally.");
    engine.enabled = true;

    _("Sync.");
    await Service.sync();

    // Legacy clients rely on this to avoid disabling the engine themselves.
    _("Meta record contains the engine.");
    Assert.ok(!!metaWBO.data.engines.steam);
  } finally {
    Services.prefs.clearUserPref(PER_DEVICE_PREF);
    await Service.startOver();
    await promiseStopServer(server);
  }
});

add_task(async function test_perDevice_updateLocalEnginesStateAppliesSeed() {
  _("Test: The setup dialog shows the choices the first sync will apply.");
  Service.syncID = "abcdefghij";
  const engine = Service.engineManager.get("steam");
  const metaWBO = new ServerWBO("global", {
    syncID: Service.syncID,
    storageVersion: STORAGE_VERSION,
    declined: ["steam"],
    engines: {},
  });
  const server = httpd_setup({
    "/1.1/johndoe/storage/meta/global": metaWBO.handler(),
  });
  await SyncTestingInfrastructure(server, "johndoe");

  try {
    await Service.startOver();
    // startOver() clears services.sync.* user prefs.
    enablePerDeviceSync();
    Service._ignorePrefObserver = true;
    engine.enabled = true;
    Service._ignorePrefObserver = false;
    Service.identity._findCluster = () => server.baseURI + "/1.1/johndoe/";

    await Service.updateLocalEnginesState();

    Assert.ok(!engine.enabled, "The account's decline is applied.");
  } finally {
    Services.prefs.clearUserPref(PER_DEVICE_PREF);
    await promiseStopServer(server);
  }
});

add_task(async function test_perDevice_freshMetaGlobalOmitsDeclined() {
  enableValidationPrefs();

  _("Test: A newly created meta/global does not carry local declines.");
  let server = sync_httpd_setup({
    "/1.1/johndoe/storage/meta/global": new ServerWBO("global", {}).handler(),
    "/1.1/johndoe/storage/steam": new ServerWBO("steam", {}).handler(),
  });
  await setUp(server);

  try {
    Service.engineManager.setDeclined(["localdecline"]);
    enablePerDeviceSync();

    await Service._uploadNewMetaGlobal();

    let meta = await Service.recordManager.get(Service.metaURL);
    Assert.ok(!Object.hasOwn(meta.payload, "declined"));

    _("With the pref off the local declined set is still submitted.");
    Services.prefs.clearUserPref(PER_DEVICE_PREF);
    Service.recordManager.clearCache();
    await Service._uploadNewMetaGlobal();

    meta = await Service.recordManager.get(Service.metaURL);
    Assert.deepEqual(meta.payload.declined, ["localdecline"]);
  } finally {
    Services.prefs.clearUserPref(PER_DEVICE_PREF);
    await Service.startOver();
    await promiseStopServer(server);
  }
});

add_task(async function test_perDevice_upgradePreservesSelections() {
  enableValidationPrefs();

  _("Test: Enabling per-device sync preserves this installation's choices.");
  Service.syncID = "abcdefghij";
  await Service.engineManager.register(PetrolEngine);
  let steam = Service.engineManager.get("steam");
  let petrol = Service.engineManager.get("petrol");
  let steamSyncID = await steam.resetLocalSyncID();

  // Remote selections oppose both local selections.
  let metaWBO = new ServerWBO("global", {
    syncID: Service.syncID,
    storageVersion: STORAGE_VERSION,
    declined: ["remotedecline"],
    engines: { steam: { syncID: steamSyncID, version: steam.version } },
  });
  let server = sync_httpd_setup({
    "/1.1/johndoe/storage/meta/global": metaWBO.handler(),
    "/1.1/johndoe/storage/steam": new ServerWBO("steam", PAYLOAD).handler(),
    "/1.1/johndoe/storage/petrol": new ServerWBO("petrol", {}).handler(),
  });
  await setUp(server);

  try {
    _("Existing selections, made before per-device sync was enabled.");
    Service._ignorePrefObserver = true;
    steam.enabled = false;
    petrol.enabled = true;
    Service._ignorePrefObserver = false;
    Service.engineManager.setDeclined(["localdecline"]);
    Svc.PrefBranch.setStringPref("lastSync", new Date().toString());

    _("Upgrade: turn per-device sync on.");
    enablePerDeviceSync();

    _("Sync.");
    await Service.sync();

    _("Every selection survived enablement.");
    Assert.ok(!steam.enabled);
    Assert.ok(petrol.enabled);
    Assert.deepEqual(Service.engineManager.getDeclined(), ["localdecline"]);
    Assert.deepEqual(metaWBO.data.declined, ["remotedecline"]);
  } finally {
    await Service.engineManager.unregister("petrol");
    await Service.startOver();
    await promiseStopServer(server);
  }
});

add_task(async function test_perDevice_firstSyncAppliesDeclinedEngines() {
  enableValidationPrefs();

  _("Test: The first sync after sign-in inherits the account's choices.");
  Service.syncID = "abcdefghij";
  await Service.engineManager.register(PetrolEngine);
  let steam = Service.engineManager.get("steam");
  let petrol = Service.engineManager.get("petrol");
  // Exercise shared prefs: Stirling uses Steam's, just as Forms uses History's.
  let stirling = Service.engineManager.get("stirling");
  let steamSyncID = await steam.resetLocalSyncID();
  let metaWBO = new ServerWBO("global", {
    syncID: Service.syncID,
    storageVersion: STORAGE_VERSION,
    declined: ["stirling"],
    engines: {
      steam: { syncID: steamSyncID, version: steam.version },
      petrol: { syncID: "petrolsync01", version: petrol.version },
    },
  });
  let server = sync_httpd_setup({
    "/1.1/johndoe/storage/meta/global": metaWBO.handler(),
    "/1.1/johndoe/storage/steam": new ServerWBO("steam", PAYLOAD).handler(),
    "/1.1/johndoe/storage/petrol": new ServerWBO("petrol", {}).handler(),
  });
  await setUp(server);

  try {
    enablePerDeviceSync();
    Service._ignorePrefObserver = true;
    // Petrol represents a default-off engine, such as creditcards.
    steam.enabled = true;
    petrol.enabled = false;
    Service._ignorePrefObserver = false;

    _("Sync.");
    await Service.sync();

    _("The account's declined engine is now disabled locally.");
    Assert.ok(!stirling.enabled);
    Assert.ok(!steam.enabled, "The decline wins over the shared pref.");

    _("A default-off engine the account syncs is enabled.");
    Assert.ok(petrol.enabled);
  } finally {
    await Service.engineManager.unregister("petrol");
    await Service.startOver();
    await promiseStopServer(server);
  }
});

add_task(async function test_perDevice_engineStateWaitsForMeta() {
  enableValidationPrefs();

  _("Test: Initial engine state waits for meta/global.");
  Service.syncID = "abcdefghij";
  let steam = Service.engineManager.get("steam");
  let metaWBO = new ServerWBO("global", {
    syncID: Service.syncID,
    storageVersion: STORAGE_VERSION,
    declined: ["steam"],
    engines: {},
  });
  let metaHandler = httpd_handler(503, "Service Unavailable");
  let server = sync_httpd_setup({
    "/1.1/johndoe/storage/meta/global": (req, res) => metaHandler(req, res),
    "/1.1/johndoe/storage/steam": new ServerWBO("steam", {}).handler(),
  });
  await setUp(server);

  try {
    enablePerDeviceSync();
    Service._ignorePrefObserver = true;
    steam.enabled = true;
    Service._ignorePrefObserver = false;

    _("Sync, which cannot fetch meta/global.");
    await Service.sync();

    _("Nothing was inherited, so local state is untouched.");
    Assert.ok(steam.enabled);

    _("meta/global becomes available.");
    metaHandler = metaWBO.handler();
    await Service.sync();

    _("The account's choices are inherited now.");
    Assert.ok(!steam.enabled);
  } finally {
    await Service.startOver();
    await promiseStopServer(server);
  }
});
