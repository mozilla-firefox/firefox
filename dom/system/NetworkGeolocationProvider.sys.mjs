/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { NetworkGeoPositionObject } from "moz-src:///dom/system/NetworkGeoPosition.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  LocationHelper: "moz-src:///dom/base/LocationHelper.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  NetworkGeolocationCache:
    "moz-src:///dom/system/NetworkGeolocationCache.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

// GeolocationPositionError has no interface object, so we can't use that here.
const POSITION_UNAVAILABLE = 2;

ChromeUtils.defineLazyGetter(lazy, "log", () =>
  console.createInstance({
    maxLogLevelPref: "geo.provider.network.loglevel",
    logModule: "NetworkGeolocationProvider",
  })
);

// Registrable domains of the network geolocation services we report on
// individually, mapped to their geolocation.network_provider Glean label.
const KNOWN_PROVIDER_DOMAINS = new Map([
  ["googleapis.com", "google"],
  ["beacondb.net", "beacondb"],
]);

/**
 * Categorize the configured network geolocation endpoint for telemetry.
 *
 * @param   {string} url The url
 * @returns {string} The geolocation.network_provider label to record:
 *                   a value in KNOWN_PROVIDER_DOMAINS, "other" for an
 *                   unrecognized host, or "unknown" if the URL has no host we
 *                   can parse.
 */
export function networkProviderLabel(url) {
  let host;
  try {
    host = Services.io.newURI(url).host;
  } catch {
    return "unknown";
  }

  if (!host) {
    return "unknown";
  }

  for (let [domain, label] of KNOWN_PROVIDER_DOMAINS) {
    if (host == domain || host.endsWith("." + domain)) {
      return label;
    }
  }

  return "other";
}

export function NetworkGeolocationProvider() {
  /*
    The _wifiMonitorTimeout controls how long we wait on receiving an update
    from the Wifi subsystem.  If this timer fires, we believe the Wifi scan has
    had a problem and we no longer can use Wifi to position the user this time
    around (we will continue to be hopeful that Wifi will recover).
  */
  XPCOMUtils.defineLazyPreferenceGetter(
    this,
    "_wifiMonitorTimeout",
    "geo.provider.network.timeToWaitBeforeSending",
    5000
  );

  XPCOMUtils.defineLazyPreferenceGetter(
    this,
    "_wifiScanningEnabled",
    "geo.provider.network.scan",
    true
  );

  // Upper bound for the exponential backoff applied to the repeating request
  // timer after consecutive network failures.
  XPCOMUtils.defineLazyPreferenceGetter(
    this,
    "_backoffMaxMs",
    "geo.provider.network.backoffMaxMs",
    20 * 1000 // 20sec
  );

  // Rate at which to scale the repeating request timer duration after
  // consecutive network failures.
  XPCOMUtils.defineLazyPreferenceGetter(
    this,
    "_backoffScale",
    "geo.provider.network.backoffScale",
    1.1 // 10% increase
  );

  this.wifiService = null;
  this.timer = null;
  this.started = false;
  this._shutdownController = null;
  // Current repeating-timer interval; grows on failure (up to _backoffMaxMs),
  // resets to _wifiMonitorTimeout on a new request or a success.
  this._currentTimerInterval = null;
}

NetworkGeolocationProvider.prototype = {
  classID: Components.ID("{77DA64D3-7458-4920-9491-86CC9914F904}"),
  name: "NetworkGeolocationProvider",
  QueryInterface: ChromeUtils.generateQI([
    "nsIGeolocationProvider",
    "nsIWifiListener",
    "nsITimerCallback",
    "nsIObserver",
    "nsINamed",
  ]),
  listener: null,

  get isWifiScanningEnabled() {
    return Cc["@mozilla.org/wifi/monitor;1"] && this._wifiScanningEnabled;
  },

  resetTimer() {
    if (this.timer) {
      this.timer.cancel();
      this.timer = null;
    }
    // A request that settles after shutdown() must not re-arm the timer.
    if (!this.started) {
      return;
    }
    if (this._currentTimerInterval == null) {
      this._currentTimerInterval = this._wifiMonitorTimeout;
    }
    // Wifi thread triggers NetworkGeolocationProvider to proceed. With no wifi,
    // do manual timeout. The interval is extended by _increaseBackoff() while
    // requests are failing and restored by _resetBackoff().
    this.timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    this.timer.initWithCallback(
      this,
      this._currentTimerInterval,
      this.timer.TYPE_REPEATING_SLACK
    );
  },

  _resetBackoff() {
    this._currentTimerInterval = this._wifiMonitorTimeout;
  },

  _increaseBackoff() {
    let current = this._currentTimerInterval || this._wifiMonitorTimeout;
    this._currentTimerInterval = Math.min(
      current * this._backoffScale,
      this._backoffMaxMs
    );
  },

  startup() {
    lazy.log.debug("startup called.");

    // startup() may be called again for each new geolocation request (per
    // nsIGeolocationProvider). Treat it as a fresh request and restart the
    // failure backoff so the request is served at the normal cadence instead
    // of waiting out a prior failure's backoff.
    this._resetBackoff();

    if (this.started) {
      // Already running: re-arm the repeating timer at the reset interval.
      this.resetTimer();
      return;
    }

    this.started = true;
    this._shutdownController = new AbortController();

    if (this.isWifiScanningEnabled) {
      if (this.wifiService) {
        this.wifiService.stopWatching(this);
      }
      this.wifiService = Cc["@mozilla.org/wifi/monitor;1"].getService(
        Ci.nsIWifiMonitor
      );
      this.wifiService.startWatching(this, false);
    }

    this.resetTimer();
  },

  watch(c) {
    lazy.log.debug("watch called");
    this.listener = c;
    this.notify();
    this.resetTimer();
  },

  shutdown() {
    lazy.log.debug("shutdown called");
    if (!this.started) {
      return;
    }

    if (this.timer) {
      this.timer.cancel();
      this.timer = null;
    }

    if (this.wifiService) {
      this.wifiService.stopWatching(this);
      this.wifiService = null;
    }

    this._shutdownController.abort();

    this.listener = null;
    this.started = false;
  },

  setHighAccuracy(enable) {
    // Mochitest wants to check this value
    if (Services.prefs.getBoolPref("geo.provider.testing", false)) {
      Services.obs.notifyObservers(
        null,
        "testing-geolocation-high-accuracy",
        enable
      );
    }
  },

  onChange(accessPoints) {
    // we got some wifi data, rearm the timer.
    this.resetTimer();

    let wifiData = null;
    if (accessPoints) {
      wifiData = lazy.LocationHelper.formatWifiAccessPoints(accessPoints);
    }
    this.sendLocationRequest(wifiData);
  },

  onError(code) {
    lazy.log.debug("wifi error: " + code);
    this.sendLocationRequest(null);
  },

  onStatus(err, statusMessage) {
    if (!this.listener) {
      return;
    }
    lazy.log.debug("onStatus called." + statusMessage);

    if (statusMessage && this.listener.notifyStatus) {
      this.listener.notifyStatus(statusMessage);
    }

    if (err && this.listener.notifyError) {
      this.listener.notifyError(POSITION_UNAVAILABLE, statusMessage);
    }
  },

  notify() {
    this.onStatus(false, "wifi-timeout");
    this.sendLocationRequest(null);
  },

  /**
   * After wifi data has been gathered, this method is invoked to perform the
   * request to network geolocation provider.
   * The result of each request is sent to all registered listener (@see watch)
   * by invoking its respective `update`, `notifyError` or `notifyStatus`
   * callbacks.
   * `update` is called upon a successful request with its response data; this will be a `NetworkGeoPositionObject` instance.
   * `notifyError` is called whenever the request gets an error from the local
   * network subsystem, the server or simply times out.
   * `notifyStatus` is called for each status change of the request that may be
   * of interest to the consumer of this class. Currently the following status
   * changes are reported: 'xhr-start', 'xhr-timeout', 'xhr-error' and
   * 'xhr-empty'.
   *
   * @param  {Array} wifiData Optional set of publicly available wifi networks
   *                          in the following structure:
   *                          <code>
   *                          [
   *                            { macAddress: <mac1>, signalStrength: <signal1> },
   *                            { macAddress: <mac2>, signalStrength: <signal2> }
   *                          ]
   *                          </code>
   */
  async sendLocationRequest(wifiData) {
    let data = { wifiAccessPoints: undefined };
    // Zero or one entry means lookup is only by our own IP address.
    if (wifiData && wifiData.length >= 2) {
      data.wifiAccessPoints = wifiData;
    }

    let cachedLocation = await lazy.NetworkGeolocationCache.getValidLocation(
      data.wifiAccessPoints
    );

    lazy.log.debug(
      "Use request cache:" +
        !!cachedLocation +
        " reason:" +
        lazy.NetworkGeolocationCache.debugReason
    );

    if (cachedLocation) {
      Glean.geolocation.geolocationCacheHit.NetworkGeolocationProvider.add();

      cachedLocation.timestamp = Date.now();
      if (this.listener) {
        this.listener.update(cachedLocation);
      }
      return;
    }

    // From here on, do a network geolocation request //
    let url = Services.urlFormatter.formatURLPref("geo.provider.network.url");

    let result;
    try {
      // formatURLPref() returns about:blank for a pref without a value.
      if (!url || url == "about:blank") {
        throw new Error("No network geolocation service URL is configured");
      }

      let logStr = data.wifiAccessPoints ? " with wifi APs" : "";
      lazy.log.info(
        `Sending IP-address-based geolocation request${logStr} to network service: ${url}`
      );

      result = await this.fetchLocation(url, wifiData);
      lazy.log.info(
        `geo provider reported: ${result.location.lng}:${result.location.lat}`
      );
      let newLocation = new NetworkGeoPositionObject(
        result.location.lat,
        result.location.lng,
        result.accuracy
      );

      if (this.listener) {
        this.listener.update(newLocation);
      }

      lazy.NetworkGeolocationCache.store(newLocation, data.wifiAccessPoints);

      // Recovered: if we had backed off, return the timer to normal cadence.
      if (this._currentTimerInterval !== this._wifiMonitorTimeout) {
        this._resetBackoff();
        this.resetTimer();
      }
    } catch (err) {
      lazy.log.error("Location request hit error: " + err.name);
      console.error(err);
      if (err.name == "AbortError") {
        this.onStatus(true, "xhr-timeout");
      } else {
        this.onStatus(true, "xhr-error");
      }
      // Slow down the repeating retry timer while the endpoint keeps failing,
      // to avoid hammering it (and draining battery). Capped at _backoffMaxMs.
      let prevInterval = this._currentTimerInterval;
      this._increaseBackoff();
      if (this._currentTimerInterval !== prevInterval) {
        this.resetTimer();
      }
    }
  },

  async fetchLocation(url, wifiData) {
    this.onStatus(false, "xhr-start");

    let fetchController = new AbortController();
    let fetchOpts = {
      method: "POST",
      headers: { "Content-Type": "application/json; charset=UTF-8" },
      credentials: "omit",
      signal: AbortSignal.any([
        fetchController.signal,
        this._shutdownController.signal,
      ]),
    };

    if (wifiData) {
      fetchOpts.body = JSON.stringify({ wifiAccessPoints: wifiData });
    }

    let timeoutId = lazy.setTimeout(
      () => fetchController.abort(),
      Services.prefs.getIntPref("geo.provider.network.timeout", 60000)
    );

    let isWifi = wifiData && wifiData.length >= 2;
    let label = isWifi ? "network_wifi_and_ip" : "network_ip";
    Glean.geolocation.geolocationService[label].add();
    Glean.geolocation.networkProvider[networkProviderLabel(url)].add();

    let response;
    try {
      response = await fetch(url, fetchOpts);
    } catch (err) {
      Glean.geolocation.networkFailures[label].add();
      throw err;
    } finally {
      lazy.clearTimeout(timeoutId);
    }

    if (!response.ok) {
      Glean.geolocation.networkFailures[label].add();
      throw new Error(
        `The geolocation provider returned a non-ok status ${response.status}`,
        { cause: await response.text() }
      );
    }

    let result;
    try {
      result = await response.json();
    } catch (err) {
      Glean.geolocation.networkFailures[label].add();
      throw new Error("The geolocation provider returned a non-JSON response", {
        cause: err,
      });
    }

    if (
      typeof result?.location?.lat != "number" ||
      typeof result.location.lng != "number"
    ) {
      Glean.geolocation.networkFailures[label].add();
      throw new Error(
        "The geolocation provider returned a response without a location"
      );
    }

    return result;
  },
};
