/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { NetworkGeoPositionObject } from "moz-src:///dom/system/NetworkGeoPosition.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  JSONFile: "resource://gre/modules/JSONFile.sys.mjs",
});

// A cached location is only valid on the network it was obtained from, so the
// cache drops itself when the network link changes and the public IP (or set of
// visible access points) may differ. See nsINetworkLinkService.
const NETWORK_LINK_TOPIC = "network:link-status-changed";

// Sent when nsINetworkLinkService.networkID changes, including when it is first
// determined after startup. Until then it is empty and a persisted entry cannot
// be checked against the current network.
const NETWORK_ID_TOPIC = "network:networkid-changed";

// Backup cache to file, so it persists beyond the browser session.
const CACHE_FILE = "networkGeolocationCache.json";
const CACHE_FILE_VERSION = 1;

// nsINetworkLinkService.networkID is mixed with a per-session seed, so the ID
// persisted with the entry only identifies the current network when compared
// through nsINetworkLinkService.matchNetworkID with the seed of the session
// that stored it.  This pref is not declared so it is not available in
// content processes.
const CACHE_SEED_PREF = "geo.provider.network.requestCache.networkIDSeed";

// Bounds on geo.provider.network.requestCache.maxAgeDays, in days.
const CACHE_MAX_AGE_DAYS_LOWER_LIMIT = 1;
const CACHE_MAX_AGE_DAYS_UPPER_LIMIT = 30;
const MS_PER_DAY = 24 * 60 * 60 * 1000;

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "gCacheMaxAgeDays",
  "geo.provider.network.requestCache.maxAgeDays",
  30
);

// Mochitest disables the request cache to simulate request failures.
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "gRequestCacheEnabled",
  "geo.provider.network.debug.requestCache.enabled",
  true
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gNetworkLinkService",
  "@mozilla.org/network/network-link-service;1",
  Ci.nsINetworkLinkService
);

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  let consoleOptions = {
    maxLogLevelPref: "geo.provider.network.loglevel",
    prefix: "NetworkGeolocationCache",
  };
  return console.createInstance(consoleOptions);
});

// Build a Set of access-point MAC addresses from a wifi list.
function wifiMacSet(wifiList = []) {
  return new Set(wifiList.map(ap => ap.macAddress));
}

// Two sets are approximately equal if at least 50% of the larger set is common
// to both.
function wifiSetsApproxEqual(setA, setB) {
  if (!setA.size || !setB.size) {
    return false;
  }

  let common = setA.intersection(setB).size;
  let kPercentMatch = 0.5;
  return common >= Math.max(setA.size, setB.size) * kPercentMatch;
}

// geo.provider.network.requestCache.maxAgeDays, clamped to
// [CACHE_MAX_AGE_DAYS_LOWER_LIMIT, CACHE_MAX_AGE_DAYS_UPPER_LIMIT], in
// milliseconds.
function cacheMaxAgeMs() {
  let days = Math.min(
    Math.max(lazy.gCacheMaxAgeDays, CACHE_MAX_AGE_DAYS_LOWER_LIMIT),
    CACHE_MAX_AGE_DAYS_UPPER_LIMIT
  );
  return days * MS_PER_DAY;
}

// nsINetworkLinkService.networkID, or "" if the service cannot provide one.
function currentNetworkID() {
  try {
    return lazy.gNetworkLinkService.networkID;
  } catch (err) {
    lazy.log.warn("Could not read the network ID", err);
    return "";
  }
}

// Compares a network ID persisted by an earlier session with the current
// network. Returns null while the current network ID is not yet determined,
// otherwise the current network ID if they are the same network, or "".
function matchPersistedNetworkID(networkID) {
  try {
    let current = {};
    if (
      !lazy.gNetworkLinkService.matchNetworkID(
        networkID,
        Services.prefs.getStringPref(CACHE_SEED_PREF, ""),
        current
      )
    ) {
      return null;
    }
    return current.value;
  } catch (err) {
    lazy.log.warn(
      "Could not compare the persisted network ID (should be impossible)",
      err
    );
    return "";
  }
}

// The file holds { version, entry? }; returns true if the data has that shape
// and any entry has the fields store() writes.
function isValidPersistedData(data) {
  if (data?.version !== CACHE_FILE_VERSION) {
    return false;
  }
  let entry = data.entry;
  if (entry === undefined) {
    return true;
  }
  return (
    typeof entry?.lat == "number" &&
    typeof entry.lng == "number" &&
    (entry.accuracy === undefined || typeof entry.accuracy == "number") &&
    typeof entry.timestamp == "number" &&
    typeof entry.networkID == "string" &&
    Array.isArray(entry.wifis) &&
    entry.wifis.every(mac => typeof mac == "string")
  );
}

// Caches the most recent network-geolocation response so repeated lookups can
// reuse it instead of re-querying.
class CachedResponse {
  QueryInterface = ChromeUtils.generateQI(["nsIObserver"]);

  location = null;
  #wifis = new Set();
  #timestamp = 0;
  // The network ID the entry was obtained on, or "" if it was stored before the
  // ID had been determined.
  #networkID = "";
  /** @type {JSONFile?} null if the profile has no usable directory. */
  #file = null;
  // False until the persisted entry has been adopted, discarded or superseded.
  #fileChecked = false;
  // Resolves once the persisted entry has been read or is determined invalid.
  loaded;

  constructor() {
    Services.obs.addObserver(this, NETWORK_LINK_TOPIC);
    Services.obs.addObserver(this, NETWORK_ID_TOPIC);
    try {
      this.#file = new lazy.JSONFile({
        path: PathUtils.join(PathUtils.profileDir, CACHE_FILE),
        dataPostProcessor: data =>
          isValidPersistedData(data) ? data : { version: CACHE_FILE_VERSION },
      });
    } catch (err) {
      lazy.log.warn("The request cache will not be persisted", err);
      this.#fileChecked = true;
    }
    this.loaded = this.#load();
  }

  async #load() {
    if (!this.#file) {
      return;
    }
    await this.#file.load();
    this.#maybeAdoptPersistedEntry();
  }

  // Adopts the persisted entry if it was obtained on the current network
  // (as determined by network ID) and discards it otherwise.
  #maybeAdoptPersistedEntry() {
    if (this.#fileChecked || !this.#file?.dataReady) {
      return;
    }
    let { entry } = this.#file.data;
    if (!entry) {
      this.#fileChecked = true;
      Glean.geolocation.persistedCache.absent.add();
      return;
    }

    let networkID = matchPersistedNetworkID(entry.networkID);
    if (networkID === null) {
      lazy.log.debug("Persisted request cache awaits the network ID");
      return;
    }
    this.#fileChecked = true;

    if (!networkID) {
      lazy.log.debug("Discarding persisted request cache: network changed");
      Glean.geolocation.persistedCache.rejected.add();
      this.clear();
      return;
    }

    this.location = new NetworkGeoPositionObject(
      entry.lat,
      entry.lng,
      entry.accuracy
    );
    this.#wifis = new Set(entry.wifis);
    this.#timestamp = entry.timestamp;
    this.#networkID = networkID;
    Glean.geolocation.persistedCache.adopted.add();
    lazy.log.debug("Loaded persisted request cache");
  }

  // True if the cache file holds a location that has not yet been checked
  // against the current network.
  #hasUncheckedPersistedEntry() {
    // !#fileChecked means that the cache file hasn't been checked since the
    // current network's identity was established.
    // dataReady means the cache file has been read (it may not have existed)
    // and data.entry means that it holds a location.
    return (
      !this.#fileChecked && this.#file?.dataReady && !!this.#file.data.entry
    );
  }

  store(location, wifiList) {
    if (this.#hasUncheckedPersistedEntry()) {
      Glean.geolocation.persistedCache.superseded.add();
    }
    this.location = location;
    this.#wifis = wifiMacSet(wifiList);
    this.#timestamp = Date.now();
    this.#networkID = currentNetworkID();
    this.#fileChecked = true;
    this.#persist();
  }

  clear() {
    this.location = null;
    this.#wifis = new Set();
    this.#timestamp = 0;
    this.#networkID = "";
    this.#fileChecked = true;
    this.#persist();
  }

  // Setting JSONFile.data before load() resolves makes load() a no-op, so a
  // store() or clear() that arrives before the file was finished reading
  // correctly overrides the persisted data.
  #persist() {
    if (!this.#file) {
      return;
    }
    let entry;
    if (this.location) {
      entry = {
        lat: this.location.coords.latitude,
        lng: this.location.coords.longitude,
        accuracy: this.location.coords.accuracy,
        wifis: [...this.#wifis],
        networkID: this.#networkID,
        timestamp: this.#timestamp,
      };
    }
    this.#file.data = { version: CACHE_FILE_VERSION, entry };
    this.#file.saveSoon();

    if (entry?.networkID) {
      try {
        Services.prefs.setStringPref(
          CACHE_SEED_PREF,
          lazy.gNetworkLinkService.networkIDSeed
        );
      } catch (err) {
        lazy.log.warn("Could not record the network ID seed", err);
      }
    }
  }

  #onNetworkIDChanged() {
    let networkID = currentNetworkID();
    // The ID is empty when the link is down, which does not imply a different
    // network; see the handling of "down" in observe().
    if (!networkID) {
      return;
    }

    if (!this.location) {
      this.#maybeAdoptPersistedEntry();
      return;
    }

    if (!this.#networkID) {
      // The entry was obtained before the ID was determined. A real network
      // change since then would have cleared the entry, so it corresponds to
      // the current network ID.
      this.#networkID = networkID;
      this.#persist();
    } else if (this.#networkID != networkID) {
      this.clear();
    }
  }

  hasLocation() {
    return !!this.location;
  }

  // True if the entry is older than the configured maximum age, or is dated in
  // the future, which would otherwise let a clock change keep it past the limit.
  isExpired() {
    let age = Date.now() - this.#timestamp;
    return age < 0 || age >= cacheMaxAgeMs();
  }

  hasWifis() {
    return this.#wifis.size > 0;
  }

  isGeoip() {
    return !this.hasWifis();
  }

  isWifiApproxEqual(wifiList) {
    return wifiSetsApproxEqual(this.#wifis, wifiMacSet(wifiList));
  }

  observe(subject, topic, data) {
    if (topic === NETWORK_ID_TOPIC) {
      this.#onNetworkIDChanged();
      return;
    }
    if (topic !== NETWORK_LINK_TOPIC) {
      return;
    }

    Glean.geolocation.networkLinkChange[data].add();

    // "down"/"unknown" do not imply a different network. Losing the link cannot
    // make a cached position wrong, and up/down are edge-triggered, so any
    // return of the link fires "up" and invalidates before the first request
    // that could have been served from the stale entry.
    if (data === "changed" || data === "up") {
      this.clear();
    }
  }

  // The instance is unusable after this.
  async finalizeForTesting() {
    Services.obs.removeObserver(this, NETWORK_LINK_TOPIC);
    Services.obs.removeObserver(this, NETWORK_ID_TOPIC);
    await this.#file?.finalize();
  }
}

// The single cache instance, created lazily to avoid needlessly registering
// the NetworkLinkService observer.
/** @type {CachedResponse?} */
var gCachedResponse = null;
var gDebugCacheReasoning = ""; // for logging the caching logic

function ensureCachedResponse() {
  if (!gCachedResponse) {
    gCachedResponse = new CachedResponse();
  }
}

export const NetworkGeolocationCache = {
  get debugReason() {
    return gDebugCacheReasoning;
  },

  store(location, wifiList) {
    ensureCachedResponse();
    gCachedResponse.store(location, wifiList);
  },

  // Drops the cache, its file and the recorded network ID seed, for when the
  // user clears cached data.
  clear() {
    // Do this to guarantee that any cache file we can access is cleared.
    ensureCachedResponse();
    gCachedResponse.clear();
    Services.prefs.clearUserPref(CACHE_SEED_PREF);
  },

  // Drops the in-memory cache so the next lookup reloads it from disk.
  async resetForTesting() {
    await gCachedResponse?.finalizeForTesting();
    gCachedResponse = null;
  },

  // Returns the cached location to reuse for a request with the given wifi list,
  // or null if the cache is unusable: disabled, empty, expired, or
  // insufficiently accurate to service the new request (as determined by
  // isWifiApproxEqual).
  async getValidLocation(newWifiList) {
    gDebugCacheReasoning = "";
    if (!lazy.gRequestCacheEnabled) {
      gCachedResponse?.clear();
      gDebugCacheReasoning = "Cache disabled";
      return null;
    }

    ensureCachedResponse();
    await gCachedResponse.loaded;

    if (gCachedResponse.hasLocation() && gCachedResponse.isExpired()) {
      gCachedResponse.clear();
      gDebugCacheReasoning = "Cached data expired";
      return null;
    }

    if (!gCachedResponse.hasLocation()) {
      gDebugCacheReasoning = "No cached data";
      return null;
    }

    if (!newWifiList) {
      gDebugCacheReasoning = "New req. is GeoIP.";
      return gCachedResponse.location;
    }

    let hasEqualWifis = gCachedResponse.isWifiApproxEqual(newWifiList);

    gDebugCacheReasoning = `EqualWifis: ${hasEqualWifis}`;

    if (gCachedResponse.hasWifis() && hasEqualWifis) {
      gDebugCacheReasoning += ", Wifi only.";
      return gCachedResponse.location;
    }

    return null;
  },
};
