/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test clearing the NetworkGeolocationProvider request cache.
 */

"use strict";

const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);
const { NetworkGeolocationCache } = ChromeUtils.importESModule(
  "moz-src:///dom/system/NetworkGeolocationCache.sys.mjs"
);
const { NetworkGeoPositionObject } = ChromeUtils.importESModule(
  "moz-src:///dom/system/NetworkGeoPosition.sys.mjs"
);

const SEED_PREF = "geo.provider.network.requestCache.networkIDSeed";

// A site-specific clear, made as a user request or not.
const SITE_CLEARS = {
  site: isUserRequest =>
    new Promise(resolve =>
      Services.clearData.deleteDataFromSite(
        "example.com",
        {},
        isUserRequest,
        Ci.nsIClearDataService.CLEAR_GEOLOCATION_CACHE,
        resolve
      )
    ),
  principal: isUserRequest =>
    new Promise(resolve =>
      Services.clearData.deleteDataFromPrincipal(
        Services.scriptSecurityManager.createContentPrincipalFromOrigin(
          "https://example.com"
        ),
        isUserRequest,
        Ci.nsIClearDataService.CLEAR_GEOLOCATION_CACHE,
        resolve
      )
    ),
  host: isUserRequest =>
    new Promise(resolve =>
      Services.clearData.deleteDataFromHost(
        "example.com",
        isUserRequest,
        Ci.nsIClearDataService.CLEAR_GEOLOCATION_CACHE,
        resolve
      )
    ),
};

function cachePath() {
  return PathUtils.join(PathUtils.profileDir, "networkGeolocationCache.json");
}

// The cache file is written on a delay, so wait for it to reflect whether an
// entry is expected.
function waitForPersistedEntry(expectEntry, msg) {
  return TestUtils.waitForCondition(async () => {
    if (!(await IOUtils.exists(cachePath()))) {
      return false;
    }
    let { entry } = await IOUtils.readJSON(cachePath());
    return !!entry == expectEntry;
  }, msg);
}

add_task(async function test_clear_geolocation_cache() {
  Services.prefs.setStringPref(SEED_PREF, "some-seed");
  NetworkGeolocationCache.store(
    new NetworkGeoPositionObject(1, 2, 3),
    undefined
  );
  Assert.ok(
    await NetworkGeolocationCache.getValidLocation(undefined),
    "a location is cached"
  );
  await waitForPersistedEntry(true, "the location is persisted");

  await new Promise(resolve => {
    Services.clearData.deleteData(
      Ci.nsIClearDataService.CLEAR_GEOLOCATION_CACHE,
      failedFlags => {
        Assert.equal(failedFlags, 0, "clearing succeeded");
        resolve();
      }
    );
  });

  Assert.equal(
    await NetworkGeolocationCache.getValidLocation(undefined),
    null,
    "the cached location is gone"
  );
  await waitForPersistedEntry(false, "the persisted entry is gone");
  Assert.ok(
    !Services.prefs.prefHasUserValue(SEED_PREF),
    "the network ID seed is forgotten"
  );
});

async function storeLocation() {
  NetworkGeolocationCache.store(
    new NetworkGeoPositionObject(1, 2, 3),
    undefined
  );
  await waitForPersistedEntry(true, "the location is persisted");
}

// The cached location belongs to no site, so a user request to clear any site
// clears it.
add_task(async function test_user_site_clear_clears_geolocation_cache() {
  for (let [kind, clear] of Object.entries(SITE_CLEARS)) {
    await storeLocation();
    await clear(true);
    Assert.equal(
      await NetworkGeolocationCache.getValidLocation(undefined),
      null,
      `a user request to clear by ${kind} clears the cached location`
    );
    await waitForPersistedEntry(false, `the persisted entry is gone (${kind})`);
  }
});

// Requests not made by the user, such as a site's Clear-Site-Data header, leave
// it alone.
add_task(async function test_non_user_site_clear_keeps_geolocation_cache() {
  await storeLocation();
  for (let [kind, clear] of Object.entries(SITE_CLEARS)) {
    await clear(false);
    Assert.ok(
      await NetworkGeolocationCache.getValidLocation(undefined),
      `a non-user request to clear by ${kind} keeps the cached location`
    );
  }
});
