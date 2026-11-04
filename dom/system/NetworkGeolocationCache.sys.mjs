/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// A cached location is only valid on the network it was obtained from, so the
// cache drops itself when the network link changes and the public IP (or set of
// visible access points) may differ. See nsINetworkLinkService.
const NETWORK_LINK_TOPIC = "network:link-status-changed";

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

// Caches the most recent network-geolocation response so repeated lookups can
// reuse it instead of re-querying.
class CachedResponse {
  QueryInterface = ChromeUtils.generateQI(["nsIObserver"]);

  location = null;
  #wifis = new Set();

  constructor() {
    Services.obs.addObserver(this, NETWORK_LINK_TOPIC);
  }

  store(location, wifiList) {
    this.location = location;
    this.#wifis = wifiMacSet(wifiList);
  }

  clear() {
    this.location = null;
    this.#wifis = new Set();
  }

  hasLocation() {
    return !!this.location;
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

  // Returns the cached location to reuse for a request with the given wifi list,
  // or null if the cache is unusable: disabled, empty, or insufficiently
  // accurate to service the new request (as determined by isWifiApproxEqual).
  getValidLocation(newWifiList) {
    gDebugCacheReasoning = "";
    let isNetworkRequestCacheEnabled = Services.prefs.getBoolPref(
      "geo.provider.network.debug.requestCache.enabled",
      true
    );
    // Mochitest needs this pref to simulate request failure
    if (!isNetworkRequestCacheEnabled) {
      gCachedResponse?.clear();
    }

    if (!gCachedResponse?.hasLocation() || !isNetworkRequestCacheEnabled) {
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
