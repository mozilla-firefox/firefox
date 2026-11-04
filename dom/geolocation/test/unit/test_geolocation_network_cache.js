/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);
const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);
const { NetworkGeolocationCache } = ChromeUtils.importESModule(
  "moz-src:///dom/system/NetworkGeolocationCache.sys.mjs"
);
const { MockRegistrar } = ChromeUtils.importESModule(
  "resource://testing-common/MockRegistrar.sys.mjs"
);

const MS_PER_DAY = 24 * 60 * 60 * 1000;
const MAX_AGE_PREF = "geo.provider.network.requestCache.maxAgeDays";
const SEED_PREF = "geo.provider.network.requestCache.networkIDSeed";
const THIS_NETWORK = "test-network";
const OTHER_NETWORK = "a-network-we-are-no-longer-on";
const PREVIOUS_SEED = "seed-of-an-earlier-session";

let geolocation = null;
let requestCount = 0;
let failNextRequest = false;

// A mock network link service so the test can control the ID.
let mockNetwork = THIS_NETWORK;
let mockSeed = "seed-of-this-session";
function mockNetworkIDFor(network, seed) {
  // In nsINetworkLinkService, an empty network stands for an ID not yet
  // determined.
  return network ? `${network}@${seed}` : "";
}
const mockNetworkLinkService = {
  QueryInterface: ChromeUtils.generateQI(["nsINetworkLinkService"]),
  get isLinkUp() {
    return true;
  },
  get linkStatusKnown() {
    return true;
  },
  get linkType() {
    return Ci.nsINetworkLinkService.LINK_TYPE_UNKNOWN;
  },
  get networkID() {
    return mockNetworkIDFor(mockNetwork, mockSeed);
  },
  get networkIDSeed() {
    return mockSeed;
  },
  matchNetworkID(networkID, seed, current) {
    if (!mockNetwork) {
      return false;
    }
    current.value =
      networkID == mockNetworkIDFor(mockNetwork, seed)
        ? mockNetworkIDFor(mockNetwork, mockSeed)
        : "";
    return true;
  },
  get dnsSuffixList() {
    return [];
  },
  get resolvers() {
    return [];
  },
  get platformDNSIndications() {
    return 0;
  },
};

// Sets the mock's network and announces the new ID the way the platform does.
function setNetwork(network) {
  mockNetwork = network;
  Services.obs.notifyObservers(null, "network:networkid-changed");
}

function geoHandler(metadata, response) {
  requestCount++;
  if (failNextRequest) {
    failNextRequest = false;
    response.setStatusLine("1.0", 500, "Internal Server Error");
    return;
  }
  let georesponse = JSON.stringify({
    status: "OK",
    location: { lat: 42, lng: 42 },
    accuracy: 42,
  });
  response.setStatusLine("1.0", 200, "OK");
  response.setHeader("Cache-Control", "no-cache", false);
  response.setHeader("Content-Type", "application/x-javascript", false);
  response.write(georesponse);
}

function getCurrentPosition() {
  return new Promise((resolve, reject) => {
    // maximumAge: 0 bypasses the geolocation service's own position cache so we
    // exercise the NetworkGeolocationProvider request cache specifically.
    geolocation.getCurrentPosition(resolve, reject, { maximumAge: 0 });
  });
}

async function expectLookupFailure(msg) {
  failNextRequest = true;
  await getCurrentPosition().then(
    () => Assert.ok(false, msg),
    err => Assert.equal(err.code, err.POSITION_UNAVAILABLE, msg)
  );
}

function cachePath() {
  return PathUtils.join(PathUtils.profileDir, "networkGeolocationCache.json");
}

// Drops the in-memory cache, then writes a persisted entry for the next lookup
// to load, along with the seed pref of the session that stored it. The entry is
// for THIS_NETWORK, stored by this session, and one day old unless overridden.
async function seedCache({
  network = THIS_NETWORK,
  seed = mockSeed,
  ...overrides
} = {}) {
  await NetworkGeolocationCache.resetForTesting();
  Services.prefs.setStringPref(SEED_PREF, seed);
  await IOUtils.writeJSON(cachePath(), {
    version: 1,
    entry: {
      lat: 1,
      lng: 2,
      accuracy: 3,
      wifis: [],
      networkID: mockNetworkIDFor(network, seed),
      timestamp: Date.now() - MS_PER_DAY,
      ...overrides,
    },
  });
}

// Resolves with the persisted entry once the file reports one matching the
// predicate, or with undefined once it reports none if the predicate is null.
// This is needed because JSONFile saves on a delay.
async function waitForPersistedEntry(predicate, msg) {
  let entry;
  await TestUtils.waitForCondition(async () => {
    if (!(await IOUtils.exists(cachePath()))) {
      return false;
    }
    entry = (await IOUtils.readJSON(cachePath())).entry;
    return predicate ? !!entry && predicate(entry) : !entry;
  }, msg);
  return entry;
}

function cacheHits() {
  return (
    Glean.geolocation.geolocationCacheHit.NetworkGeolocationProvider.testGetValue() ??
    0
  );
}

add_setup(async function () {
  do_get_profile();
  Services.fog.initializeFOG();

  let linkServiceCID = MockRegistrar.register(
    "@mozilla.org/network/network-link-service;1",
    mockNetworkLinkService
  );
  registerCleanupFunction(() => MockRegistrar.unregister(linkServiceCID));

  let httpserver = new HttpServer();
  httpserver.registerPathHandler("/geo", geoHandler);
  httpserver.start(-1);

  Services.prefs.setCharPref(
    "geo.provider.network.url",
    `http://localhost:${httpserver.identity.primaryPort}/geo`
  );
  // IP-only lookups, so a repeated request is a deterministic cache hit.
  Services.prefs.setBoolPref("geo.provider.network.scan", false);
  Services.prefs.setBoolPref(
    "geo.provider.network.debug.requestCache.enabled",
    true
  );

  geolocation = Cc["@mozilla.org/geolocation;1"].getService(Ci.nsISupports);

  registerCleanupFunction(
    () => new Promise(resolve => httpserver.stop(resolve))
  );
});

add_task(async function cache_is_reused() {
  await getCurrentPosition();
  Assert.equal(requestCount, 1, "first lookup performs a network request");
  Assert.equal(
    Glean.geolocation.persistedCache.absent.testGetValue(),
    1,
    "counted the absent persisted entry"
  );

  await getCurrentPosition();
  Assert.equal(requestCount, 1, "second lookup is served from the cache");
});

add_task(async function cache_is_persisted() {
  let before = Date.now();
  let entry = await waitForPersistedEntry(
    () => true,
    "the cache file is written after a successful lookup"
  );
  Assert.equal(entry.lat, 42, "entry has the latitude");
  Assert.equal(entry.lng, 42, "entry has the longitude");
  Assert.equal(entry.accuracy, 42, "entry has the accuracy");
  Assert.deepEqual(entry.wifis, [], "an IP-only lookup stores no wifis");
  Assert.equal(
    entry.networkID,
    mockNetworkIDFor(THIS_NETWORK, mockSeed),
    "entry has the network"
  );
  Assert.lessOrEqual(entry.timestamp, before, "entry is dated when stored");
  Assert.equal(
    Services.prefs.getStringPref(SEED_PREF),
    mockSeed,
    "the seed of the session that stored the entry is recorded"
  );
});

add_task(async function cache_is_dropped_on_network_change() {
  Assert.equal(requestCount, 1, "a cached position is in place");

  // Losing the link does not make the cached position wrong.
  Services.obs.notifyObservers(null, "network:link-status-changed", "down");

  await getCurrentPosition();
  Assert.equal(requestCount, 1, '"down" leaves the cache in place');

  Services.obs.notifyObservers(null, "network:link-status-changed", "changed");

  await waitForPersistedEntry(
    null,
    "the persisted entry is dropped after a network change"
  );

  await getCurrentPosition();
  Assert.equal(
    requestCount,
    2,
    "lookup after a network change performs a fresh request"
  );

  Services.obs.notifyObservers(null, "network:link-status-changed", "up");

  await getCurrentPosition();
  Assert.equal(
    requestCount,
    3,
    "lookup after the link comes back performs a fresh request"
  );

  let linkChanges = Glean.geolocation.networkLinkChange;
  Assert.equal(linkChanges.down.testGetValue(), 1, "counted the down");
  Assert.equal(linkChanges.changed.testGetValue(), 1, "counted the change");
  Assert.equal(linkChanges.up.testGetValue(), 1, "counted the up");
});

add_task(async function persisted_cache_is_reused() {
  await seedCache();
  let hitsBefore = cacheHits();

  let position = await getCurrentPosition();
  Assert.equal(requestCount, 3, "first lookup is served from the file");
  Assert.equal(position.coords.latitude, 1, "position has the file latitude");
  Assert.equal(position.coords.longitude, 2, "position has the file longitude");
  Assert.equal(position.coords.accuracy, 3, "position has the file accuracy");
  Assert.equal(cacheHits(), hitsBefore + 1, "counted the cache hit");
  Assert.equal(
    Glean.geolocation.persistedCache.adopted.testGetValue(),
    1,
    "counted the adopted persisted entry"
  );
});

// An entry from an earlier session carries an ID made with that session's seed.
add_task(async function persisted_cache_from_earlier_session_is_reused() {
  await seedCache({ seed: PREVIOUS_SEED });

  let position = await getCurrentPosition();
  Assert.equal(requestCount, 3, "first lookup is served from the file");
  Assert.equal(position.coords.latitude, 1, "position has the file latitude");

  // Finalizing flushes any pending save, so the file reflects every write.
  await NetworkGeolocationCache.resetForTesting();
  let entry = (await IOUtils.readJSON(cachePath())).entry;
  Assert.equal(
    entry.networkID,
    mockNetworkIDFor(THIS_NETWORK, PREVIOUS_SEED),
    "the entry on disk keeps the ID of the session that stored it"
  );
  Assert.equal(
    Services.prefs.getStringPref(SEED_PREF),
    PREVIOUS_SEED,
    "the recorded seed keeps the session that stored the entry"
  );
});

add_task(async function expired_persisted_cache_is_dropped() {
  // Beyond the 30 day limit, which must hold even when the pref asks for more.
  Services.prefs.setIntPref(MAX_AGE_PREF, 100);
  await seedCache({ timestamp: Date.now() - 31 * MS_PER_DAY });

  let position = await getCurrentPosition();
  Assert.equal(requestCount, 4, "first lookup performs a network request");
  Assert.equal(position.coords.latitude, 42, "position is the fresh one");

  await waitForPersistedEntry(
    entry => entry.lat == 42,
    "the fresh position replaces the expired entry on disk"
  );

  Services.prefs.clearUserPref(MAX_AGE_PREF);
});

add_task(async function persisted_cache_from_other_network_is_dropped() {
  await seedCache({ network: OTHER_NETWORK, seed: PREVIOUS_SEED });

  let position = await getCurrentPosition();
  Assert.equal(requestCount, 5, "first lookup performs a network request");
  Assert.equal(position.coords.latitude, 42, "position is the fresh one");
  Assert.equal(
    Glean.geolocation.persistedCache.rejected.testGetValue(),
    1,
    "counted the rejected persisted entry"
  );
});

add_task(async function max_age_is_at_least_one_day() {
  Services.prefs.setIntPref(MAX_AGE_PREF, 0);

  await seedCache({ timestamp: Date.now() - 2 * MS_PER_DAY });
  await getCurrentPosition();
  Assert.equal(
    requestCount,
    6,
    "a two day old entry is expired under the one day minimum"
  );

  await seedCache({ timestamp: Date.now() - MS_PER_DAY / 2 });
  let position = await getCurrentPosition();
  Assert.equal(
    requestCount,
    6,
    "a half day old entry is reused under the one day minimum"
  );
  Assert.equal(position.coords.latitude, 1, "position is the seeded one");

  Services.prefs.clearUserPref(MAX_AGE_PREF);
});

// A successful lookup before the platform has determined the network ID
// cannot use the persisted cache entry.  The fresh cache entry it stores has
// no network ID yet but adopts one found later.
add_task(async function entry_adopts_the_network_id_once_determined() {
  await seedCache();
  mockNetwork = "";

  let position = await getCurrentPosition();
  Assert.equal(requestCount, 7, "lookup without a network ID fetches");
  Assert.equal(
    Glean.geolocation.persistedCache.superseded.testGetValue(),
    1,
    "counted the superseded persisted entry"
  );
  Assert.equal(position.coords.latitude, 42, "position is the fresh one");
  await waitForPersistedEntry(
    entry => entry.lat == 42 && entry.networkID == "",
    "the fresh entry is stored without a network ID"
  );

  await getCurrentPosition();
  Assert.equal(
    requestCount,
    7,
    "the fresh entry is reused while the network ID is still unknown"
  );

  setNetwork(THIS_NETWORK);
  await waitForPersistedEntry(
    entry => entry.networkID == mockNetworkIDFor(THIS_NETWORK, mockSeed),
    "the entry adopts the network ID once it is determined"
  );

  position = await getCurrentPosition();
  Assert.equal(requestCount, 7, "the adopted entry is reused");
  Assert.equal(position.coords.latitude, 42, "position is the adopted one");
});

add_task(async function entry_is_dropped_if_the_network_id_differs() {
  Assert.equal(requestCount, 7, "a cached position is in place");

  setNetwork(OTHER_NETWORK);
  await waitForPersistedEntry(
    null,
    "the entry is dropped when the network ID turns out to differ"
  );

  await getCurrentPosition();
  Assert.equal(requestCount, 8, "lookup after the drop fetches");

  setNetwork(THIS_NETWORK);
});

// If a location request made before the network ID was determined fails, the
// persisted cache entry is still pending and is checked when the ID arrives.
add_task(
  async function persisted_cache_is_checked_once_network_id_determined() {
    await seedCache({ seed: PREVIOUS_SEED });
    mockNetwork = "";

    await expectLookupFailure("lookup without a network ID fails");
    Assert.equal(requestCount, 9, "the failed lookup went to the network");

    setNetwork(THIS_NETWORK);
    let position = await getCurrentPosition();
    Assert.equal(requestCount, 9, "the persisted entry is served once checked");
    Assert.equal(position.coords.latitude, 1, "position is the seeded one");
  }
);

add_task(async function persisted_cache_is_dropped_once_network_id_differs() {
  await seedCache({ network: OTHER_NETWORK, seed: PREVIOUS_SEED });
  mockNetwork = "";

  await expectLookupFailure("lookup without a network ID fails");
  Assert.equal(requestCount, 10, "the failed lookup went to the network");

  setNetwork(THIS_NETWORK);
  await waitForPersistedEntry(
    null,
    "the entry from another network is dropped once the ID is determined"
  );

  let position = await getCurrentPosition();
  Assert.equal(requestCount, 11, "lookup after the drop fetches");
  Assert.equal(position.coords.latitude, 42, "position is the fresh one");
});

// An entry dated in the future, as after the clock was set back, is expired.
add_task(async function persisted_cache_from_the_future_is_dropped() {
  await seedCache({ timestamp: Date.now() + MS_PER_DAY });

  let position = await getCurrentPosition();
  Assert.equal(requestCount, 12, "lookup performs a network request");
  Assert.equal(position.coords.latitude, 42, "position is the fresh one");
});
