/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  LocationHelper: "moz-src:///dom/base/LocationHelper.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

/**
 * @typedef {import("../../services/settings/RemoteSettingsClient.sys.mjs").RemoteSettingsClient} RemoteSettingsClient
 */

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "wifiScanningEnabled",
  "browser.region.network.scan",
  true
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "networkTimeout",
  "browser.region.timeout",
  5000
);

// Retry the region lookup every hour on failure, a failure
// is likely to be a service failure so this gives the
// service some time to restore. Setting to 0 disabled retries.
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "retryTimeout",
  "browser.region.retry-timeout",
  60 * 60 * 1000
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "loggingEnabled",
  "browser.region.log",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "cacheBustEnabled",
  "browser.region.update.enabled",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "updateDebounce",
  "browser.region.update.debounce",
  60 * 60 * 24
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "lastUpdated",
  "browser.region.update.updated",
  0
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "localGeocodingEnabled",
  "browser.region.local-geocoding",
  false
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "timerManager",
  "@mozilla.org/updates/timer-manager;1",
  Ci.nsIUpdateTimerManager
);

const log = console.createInstance({
  logModule: "Region.sys.mjs",
  maxLogLevel: lazy.loggingEnabled ? "All" : "Warn",
});

const REGION_PREF = "browser.search.region";
const COLLECTION_ID = "regions";
const GEOLOCATION_TOPIC = "geolocation-position-events";

// Prefix for all the region updating related preferences.
const UPDATE_PREFIX = "browser.region.update";

// The amount of time (in seconds) we need to be in a new
// location before we update the home region.
// Currently set to 2 weeks.
const UPDATE_INTERVAL = 60 * 60 * 24 * 14;

const MAX_RETRIES = 3;

// If the user never uses geolocation, or geolocation is disabled, schedule a
// periodic update to check the current location (in seconds).
const UPDATE_CHECK_NAME = "region-update-timer";
const UPDATE_CHECK_INTERVAL = 60 * 60 * 24 * 7;

// Let child processes read the current home value
// but dont trigger redundant updates in them.
let inChildProcess =
  Services.appinfo.processType != Ci.nsIXULRuntime.PROCESS_TYPE_DEFAULT;

/**
 * This module keeps track of the users current region (country).
 * so the SearchService and other consumers can apply region
 * specific customisations.
 */
class RegionDetector {
  /**
   * The users home location.
   *
   * @type {string}
   */
  #home = null;
  /**
   * The most recent location the user was detected.
   *
   * @type {string|null}
   */
  #current = null;
  /**
   * The RemoteSettings client used to sync region files.
   *
   * @type {RemoteSettingsClient}
   */
  #rsClient;
  /**
   * The resolver for when wifi data is received.
   *
   * @type {?(value: any) => void}
   */
  #wifiDataPromiseResolver = null;
  /**
   * Keeps track of how many times we have tried to fetch the users region during
   * failure. Exposed for tests
   */
  _retryCount = 0;
  /**
   * @type {Promise}
   *   Allow tests to wait for init to be complete.
   */
  #initPromise = null;
  // Topic for Observer events fired by Region.sys.mjs.
  REGION_TOPIC = "browser-region-updated";
  // Values for telemetry.
  TELEMETRY = {
    SUCCESS: 0,
    NO_RESULT: 1,
    TIMEOUT: 2,
    ERROR: 3,
  };

  /**
   * Read currently stored region data and if needed trigger background
   * region detection.
   */
  init() {
    // If we're running in the child process, then all `Region` does is act
    // as a proxy for the browser.search.region preference.
    if (inChildProcess) {
      this.#home = Services.prefs.getCharPref(REGION_PREF, null);
      return Promise.resolve();
    }

    if (this.#initPromise) {
      return this.#initPromise;
    }
    if (lazy.cacheBustEnabled) {
      Services.tm.idleDispatchToMainThread(() => {
        lazy.timerManager.registerTimer(
          UPDATE_CHECK_NAME,
          () => this._updateTimer(),
          UPDATE_CHECK_INTERVAL
        );
      });
    }
    let promises = [];
    this.#home = Services.prefs.getCharPref(REGION_PREF, null);
    if (this.#home) {
      // On startup, ensure the Glean probe knows the home region from preferences.
      Glean.region.homeRegion.set(this.#home);
    } else {
      promises.push(this.#idleDispatch(() => this._fetchRegion()));
    }
    if (lazy.localGeocodingEnabled) {
      promises.push(this.#idleDispatch(() => this.#setupRemoteSettings()));
    }
    return (this.#initPromise = Promise.all(promises));
  }

  /**
   * Get the region we currently consider the users home.
   *
   * @returns {?string}
   *   The users current home region.
   */
  get home() {
    return this.#home;
  }

  /**
   * Get the last region we detected the user to be in.
   *
   * @returns {?string}
   *   The users current region.
   */
  get current() {
    return this.#current;
  }

  /**
   * Triggers fetching of the users current region. Exposed for tests.
   */
  async _fetchRegion() {
    if (this._retryCount >= MAX_RETRIES) {
      return;
    }
    let startTime = Date.now();
    let telemetryResult = this.TELEMETRY.SUCCESS;
    let result = null;

    try {
      result = await this.#getRegion();
    } catch (err) {
      telemetryResult = this.TELEMETRY[err.message] || this.TELEMETRY.ERROR;
      log.error("Failed to fetch region", err);
      if (lazy.retryTimeout) {
        this._retryCount++;
        lazy.setTimeout(() => {
          Services.tm.idleDispatchToMainThread(this._fetchRegion.bind(this));
        }, lazy.retryTimeout);
      }
    }

    let took = Date.now() - startTime;
    if (result) {
      await this.#storeRegion(result);
    }
    Glean.region.fetchTime.accumulateSingleSample(took);

    Glean.region.fetchResult.accumulateSingleSample(telemetryResult);
  }

  /**
   * Validate then store the region and report telemetry.
   *
   * @param {string} region
   *   The region to store.
   */
  async #storeRegion(region) {
    let isTimezoneUS = this._isUSTimezone();
    // If it's a US region, but not a US timezone, we don't store
    // the value. This works because no region defaults to
    // ZZ (unknown) in nsURLFormatter
    if (region != "US") {
      this._setCurrentRegion(region);
      Glean.region.storeRegionResult.setForRestOfWorld.add();
    } else if (isTimezoneUS) {
      this._setCurrentRegion(region);
      Glean.region.storeRegionResult.setForUnitedStates.add();
    } else {
      Glean.region.storeRegionResult.ignoredUnitedStatesIncorrectTimezone.add();
    }
  }

  /**
   * Save the update current region and check if the home region
   * also needs an update. Exposed for tests.
   *
   * @param {string} region
   *   The region to store.
   */
  _setCurrentRegion(region = "") {
    log.info("Setting current region:", region);
    this.#current = region;

    let now = Math.round(Date.now() / 1000);
    let prefs = Services.prefs;
    prefs.setIntPref(`${UPDATE_PREFIX}.updated`, now);

    // Interval is in seconds.
    let interval = prefs.getIntPref(
      `${UPDATE_PREFIX}.interval`,
      UPDATE_INTERVAL
    );
    let seenRegion = prefs.getCharPref(`${UPDATE_PREFIX}.region`, null);
    let firstSeen = prefs.getIntPref(`${UPDATE_PREFIX}.first-seen`, 0);

    // If we don't have a value for .home we can set it immediately.
    if (!this.#home) {
      this._setHomeRegion(region);
    } else if (region != this.#home && region != seenRegion) {
      // If we are in a different region than what is currently
      // considered home, then keep track of when we first
      // seen the new location.
      prefs.setCharPref(`${UPDATE_PREFIX}.region`, region);
      prefs.setIntPref(`${UPDATE_PREFIX}.first-seen`, now);
    } else if (region != this.#home && region == seenRegion) {
      // If we have been in the new region for longer than
      // a specified time period, then set that as the new home.
      if (now >= firstSeen + interval) {
        this._setHomeRegion(region);
      }
    } else {
      // If we are at home again, stop tracking the seen region.
      prefs.clearUserPref(`${UPDATE_PREFIX}.region`);
      prefs.clearUserPref(`${UPDATE_PREFIX}.first-seen`);
    }
  }

  /**
   * Wrap a string as a nsISupports.
   *
   * @param {string} data
   */
  #createSupportsString(data) {
    let string = Cc["@mozilla.org/supports-string;1"].createInstance(
      Ci.nsISupportsString
    );
    string.data = data;
    return string;
  }

  /**
   * Save the updated home region and notify observers. Exposed for tests.
   *
   * @param {string} region
   *   The region to store.
   * @param {boolean} [notify]
   *   Tests can disable the notification for convenience as it
   *   may trigger an engines reload.
   */
  _setHomeRegion(region, notify = true) {
    if (region == this.#home) {
      return;
    }
    log.info("Updating home region:", region);
    this.#home = region;
    Services.prefs.setCharPref("browser.search.region", region);
    Glean.region.homeRegion.set(region);
    if (notify) {
      Services.obs.notifyObservers(
        this.#createSupportsString(region),
        this.REGION_TOPIC
      );
    }
  }

  /**
   * Make the request to fetch the region from the configured service.
   */
  async #getRegion() {
    log.info("#getRegion called");
    let fetchOpts = {
      headers: { "Content-Type": "application/json" },
      credentials: "omit",
    };
    if (lazy.wifiScanningEnabled) {
      let wifiData = await this.#fetchWifiData();
      if (wifiData) {
        let postData = JSON.stringify({ wifiAccessPoints: wifiData });
        log.info("Sending wifi details: ", wifiData);
        fetchOpts.method = "POST";
        fetchOpts.body = postData;
      }
    }
    let url = Services.urlFormatter.formatURLPref("browser.region.network.url");
    log.info("#getRegion url is: ", url);

    if (!url) {
      return null;
    }

    try {
      let req = await this.#fetchTimeout(url, fetchOpts, lazy.networkTimeout);
      // @ts-ignore
      let res = /** @type {{country_code:string}} */ (await req.json());
      log.info("_#getRegion returning ", res.country_code);
      return res.country_code;
    } catch (err) {
      log.error("Error fetching region", err);
      let errCode = err.message in this.TELEMETRY ? err.message : "NO_RESULT";
      throw new Error(errCode);
    }
  }

  /**
   * Setup the RemoteSetting client + sync listener and ensure
   * the map files are downloaded.
   */
  async #setupRemoteSettings() {
    log.info("#setupRemoteSettings");
    this.#rsClient = lazy.RemoteSettings(COLLECTION_ID);
    this.#rsClient.on("sync", this._onRegionFilesSync.bind(this));
    await this.#ensureRegionFilesDownloaded();
    // Start listening to geolocation events only after
    // we know the maps are downloded.
    Services.obs.addObserver(this, GEOLOCATION_TOPIC);
  }

  /**
   * Called when RemoteSettings syncs new data, clean up any
   * stale attachments and download any new ones.
   *
   * @param {object} syncData
   *   Object describing the data that has just been synced.
   * @param {object} syncData.data
   * @param {object[]} syncData.data.deleted
   */
  async _onRegionFilesSync({ data: { deleted } }) {
    log.info("_onRegionFilesSync");
    const toDelete = deleted.filter(d => d.attachment);
    // Remove local files of deleted records
    await Promise.all(
      toDelete.map(entry => this.#rsClient.attachments.deleteDownloaded(entry))
    );
    await this.#ensureRegionFilesDownloaded();
  }

  /**
   * Download the RemoteSetting record attachments, when they are
   * successfully downloaded set a flag so we can start using them
   * for geocoding.
   */
  async #ensureRegionFilesDownloaded() {
    log.info("#ensureRegionFilesDownloaded");
    let records = (await this.#rsClient.get()).filter(d => d.attachment);
    log.info("#ensureRegionFilesDownloaded", records);
    if (!records.length) {
      log.info("#ensureRegionFilesDownloaded: Nothing to download");
      return;
    }
    await Promise.all(records.map(r => this.#rsClient.attachments.download(r)));
    log.info("#ensureRegionFilesDownloaded complete");
    this._regionFilesReady = true;
  }

  /**
   * Fetch an attachment from RemoteSettings.
   *
   * @param {string} id
   *   The id of the record to fetch the attachment from.
   */
  async #fetchAttachment(id) {
    let record = (await this.#rsClient.get({ filters: { id } })).pop();
    let { buffer } = await this.#rsClient.attachments.download(record);
    let text = new TextDecoder("utf-8").decode(buffer);
    return JSON.parse(text);
  }

  /**
   * Get a map of the world with region definitions. Exposed for tests.
   */
  async _getPlainMap() {
    return this.#fetchAttachment("world");
  }

  /**
   * Get a map with the regions expanded by a few km to help
   * fallback lookups when a location is not within a region. Exposed for tests.
   */
  async _getBufferedMap() {
    return this.#fetchAttachment("world-buffered");
  }

  /**
   * Gets the users current location using the same reverse IP
   * request that is used for GeoLocation requests. Exposed for tests.
   *
   * @returns {Promise<object>} location
   *   Object representing the user location, with a location key
   *   that contains the lat / lng coordinates.
   */
  async _getLocation() {
    log.info("_getLocation called");
    let fetchOpts = { headers: { "Content-Type": "application/json" } };
    let url = Services.urlFormatter.formatURLPref("geo.provider.network.url");
    let req = await this.#fetchTimeout(url, fetchOpts, lazy.networkTimeout);
    let result = await req.json();
    log.info("_getLocation returning", result);
    return result;
  }

  /**
   * Return the users current region using request that is used for GeoLocation
   * requests. Exposed for tests.
   *
   * @returns {Promise<string>}
   *   A 2 character string representing a region.
   */
  async _getRegionLocally() {
    let { location } = await this._getLocation();
    return this.#geoCode(location);
  }

  /**
   * Take a location and return the region code for that location
   * by looking up the coordinates in geojson map files.
   * Inspired by https://github.com/mozilla/ichnaea/blob/874e8284f0dfa1868e79aae64e14707eed660efe/ichnaea/geocode.py#L114
   *
   * @param {object} location
   *   A location object containing lat + lng coordinates.
   *
   * @returns {Promise<string>}
   *   A 2 character string representing a region.
   */
  async #geoCode(location) {
    let plainMap = await this._getPlainMap();
    let polygons = this.#getPolygonsContainingPoint(location, plainMap);
    if (polygons.length == 1) {
      log.info("Found in single exact region");
      return polygons[0].properties.alpha2;
    }
    if (polygons.length) {
      log.info("Found in ", polygons.length, "overlapping exact regions");
      return this.#findFurthest(location, polygons);
    }

    // We haven't found a match in the exact map, use the buffered map
    // to see if the point is close to a region.
    let bufferedMap = await this._getBufferedMap();
    polygons = this.#getPolygonsContainingPoint(location, bufferedMap);

    if (polygons.length === 1) {
      log.info("Found in single buffered region");
      return polygons[0].properties.alpha2;
    }

    // Matched more than one region, which one of those regions
    // is it closest to without the buffer.
    if (polygons.length) {
      log.info("Found in ", polygons.length, "overlapping buffered regions");
      let regions = polygons.map(polygon => polygon.properties.alpha2);
      let unBufferedRegions = plainMap.features.filter(feature =>
        regions.includes(feature.properties.alpha2)
      );
      return this.#findClosest(location, unBufferedRegions);
    }
    return null;
  }

  /**
   * Find all the polygons that contain a single point, return
   * an array of those polygons along with the region that
   * they define
   *
   * @param {object} point
   *   A lat + lng coordinate.
   * @param {object} map
   *   Geojson object that defined seperate regions with a list
   *   of polygons.
   *
   * @returns {Array}
   *   An array of polygons that contain the point, along with the
   *   region they define.
   */
  #getPolygonsContainingPoint(point, map) {
    let polygons = [];
    for (const feature of map.features) {
      let coords = feature.geometry.coordinates;
      if (feature.geometry.type === "Polygon") {
        if (this.#polygonInPoint(point, coords[0])) {
          polygons.push(feature);
        }
      } else if (feature.geometry.type === "MultiPolygon") {
        for (const innerCoords of coords) {
          if (this.#polygonInPoint(point, innerCoords[0])) {
            polygons.push(feature);
          }
        }
      }
    }
    return polygons;
  }

  /**
   * Find the largest distance between a point and any of the points that
   * make up an array of regions.
   *
   * @param {object} location
   *   A lat + lng coordinate.
   * @param {Array} regions
   *   An array of GeoJSON region definitions.
   *
   * @returns {string}
   *   A 2 character string representing a region.
   */
  #findFurthest(location, regions) {
    let max = { distance: 0, region: null };
    this.#traverse(regions, ({ lat, lng, region }) => {
      let distance = this.#distanceBetween(location, { lng, lat });
      if (distance > max.distance) {
        max = { distance, region };
      }
    });
    return max.region;
  }

  /**
   * Find the smallest distance between a point and any of the points that
   * make up an array of regions.
   *
   * @param {object} location
   *   A lat + lng coordinate.
   * @param {Array} regions
   *   An array of GeoJSON region definitions.
   *
   * @returns {string}
   *   A 2 character string representing a region.
   */
  #findClosest(location, regions) {
    let min = { distance: Infinity, region: null };
    this.#traverse(regions, ({ lat, lng, region }) => {
      let distance = this.#distanceBetween(location, { lng, lat });
      if (distance < min.distance) {
        min = { distance, region };
      }
    });
    return min.region;
  }

  /**
   * Utility function to loop over all the coordinate points in an
   * array of polygons and call a function on them.
   *
   * @param {Array} regions
   *   An array of GeoJSON region definitions.
   * @param {Function} fun
   *   Function to call on individual coordinates.
   */
  #traverse(regions, fun) {
    for (const region of regions) {
      if (region.geometry.type === "Polygon") {
        for (const [lng, lat] of region.geometry.coordinates[0]) {
          fun({ lat, lng, region: region.properties.alpha2 });
        }
      } else if (region.geometry.type === "MultiPolygon") {
        for (const innerCoords of region.geometry.coordinates) {
          for (const [lng, lat] of innerCoords[0]) {
            fun({ lat, lng, region: region.properties.alpha2 });
          }
        }
      }
    }
  }

  /**
   * Check whether a point is contained within a polygon using the
   * point in polygon algorithm:
   * https://en.wikipedia.org/wiki/Point_in_polygon
   * This casts a ray from the point and counts how many times
   * that ray intersects with the polygons borders, if it is
   * an odd number of times the point is inside the polygon.
   *
   * @param {object} location
   *   A lat + lng coordinate.
   * @param {number} location.lng
   * @param {number} location.lat
   * @param {object} poly
   *   Array of coordinates that define the boundaries of a polygon.
   * @returns {boolean}
   *   Whether the point is within the polygon.
   */
  #polygonInPoint({ lng, lat }, poly) {
    let inside = false;
    // For each edge of the polygon.
    for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
      let xi = poly[i][0];
      let yi = poly[i][1];
      let xj = poly[j][0];
      let yj = poly[j][1];
      // Does a ray cast from the point intersect with this polygon edge.
      let intersect =
        yi > lat != yj > lat && lng < ((xj - xi) * (lat - yi)) / (yj - yi) + xi;
      // If so toggle result, an odd number of intersections
      // means the point is inside.
      if (intersect) {
        inside = !inside;
      }
    }
    return inside;
  }

  /**
   * Find the distance between 2 points.
   *
   * @param {object} p1
   *   A lat + lng coordinate.
   * @param {object} p2
   *   A lat + lng coordinate.
   *
   * @returns {number}
   *   The distance between the 2 points.
   */
  #distanceBetween(p1, p2) {
    return Math.hypot(p2.lng - p1.lng, p2.lat - p1.lat);
  }

  /**
   * A wrapper around fetch that implements a timeout, will throw
   * a TIMEOUT error if the request is not completed in time.
   *
   * @param {string} url
   *   The time url to fetch.
   * @param {object} opts
   *   The options object passed to the call to fetch.
   * @param {number} timeout
   *   The time in ms to wait for the request to complete.
   * @returns {Promise<Response>}
   */
  async #fetchTimeout(url, opts, timeout) {
    let controller = new AbortController();
    opts.signal = controller.signal;
    // Casted to Promise<Response> because `this.#timeout` will not return void,
    // but reject if it wins the race.
    return /** @type {Promise<Response>} */ (
      Promise.race([fetch(url, opts), this.#timeout(timeout, controller)])
    );
  }

  /**
   * Implement the timeout for network requests. This will be run for
   * all network requests, but the error will only be returned if it
   * completes first.
   *
   * @param {number} timeout
   *   The time in ms to wait for the request to complete.
   * @param {object} controller
   *   The AbortController passed to the fetch request that
   *   allows us to abort the request.
   */
  async #timeout(timeout, controller) {
    await new Promise(resolve => lazy.setTimeout(resolve, timeout));
    if (controller) {
      // Yield so it is the TIMEOUT that is returned and not
      // the result of the abort().
      lazy.setTimeout(() => controller.abort(), 0);
    }
    throw new Error("TIMEOUT");
  }

  async #fetchWifiData() {
    log.info("fetchWifiData called");
    this.wifiService = Cc["@mozilla.org/wifi/monitor;1"].getService(
      Ci.nsIWifiMonitor
    );
    this.wifiService.startWatching(this, false);

    return new Promise(resolve => {
      this.#wifiDataPromiseResolver = resolve;
    });
  }

  /**
   * If the user is using geolocation then we will see frequent updates
   * debounce those so we aren't processing them constantly.
   *
   * @returns {boolean}
   *   Whether we should continue the update check.
   */
  #needsUpdateCheck() {
    let sinceUpdate = Math.round(Date.now() / 1000) - lazy.lastUpdated;
    let needsUpdate = sinceUpdate >= lazy.updateDebounce;
    if (!needsUpdate) {
      log.info(`Ignoring update check, last seen ${sinceUpdate} seconds ago`);
    }
    return needsUpdate;
  }

  /**
   * Dispatch a promise returning function to the main thread and
   * resolve when it is completed.
   *
   * @param {() => Promise<void>} fun
   */
  #idleDispatch(fun) {
    return new Promise(resolve => {
      Services.tm.idleDispatchToMainThread(fun().then(resolve));
    });
  }

  /**
   * timerManager will call this periodically to update the region
   * in case the user never users geolocation. Exposed for tests.
   */
  async _updateTimer() {
    if (this.#needsUpdateCheck()) {
      await this._fetchRegion();
    }
  }

  /**
   * Called when we see geolocation updates.
   * in case the user never users geolocation.
   *
   * @param {object} location
   *   A location object containing lat + lng coordinates.
   */
  async #seenLocation(location) {
    log.info(`Got location update: ${location.lat}:${location.lng}`);
    if (this.#needsUpdateCheck()) {
      let region = await this.#geoCode(location);
      if (region) {
        this._setCurrentRegion(region);
      }
    }
  }

  onChange(accessPoints) {
    log.info("onChange called");
    if (!accessPoints || !this.#wifiDataPromiseResolver) {
      return;
    }

    if (this.wifiService) {
      this.wifiService.stopWatching(this);
      this.wifiService = null;
    }

    if (this.#wifiDataPromiseResolver) {
      let data = lazy.LocationHelper.formatWifiAccessPoints(accessPoints);
      this.#wifiDataPromiseResolver(data);
      this.#wifiDataPromiseResolver = null;
    }
  }

  /**
   * Implemented for nsIWifiListener.
   */
  onError() {}

  /**
   * A method that tries to determine if this user is in a US geography according
   * to their timezones.
   *
   * This is exposed so that tests may override it to avoid timezone issues when
   * testing.
   *
   * @returns {boolean}
   */
  _isUSTimezone() {
    // Timezone assumptions! We assume that if the system clock's timezone is
    // between Newfoundland and Hawaii, that the user is in North America.

    // This includes all of South America as well, but we have relatively few
    // en-US users there, so that's OK.

    // 150 minutes = 2.5 hours (UTC-2.5), which is
    // Newfoundland Daylight Time (http://www.timeanddate.com/time/zones/ndt)

    // 600 minutes = 10 hours (UTC-10), which is
    // Hawaii-Aleutian Standard Time (http://www.timeanddate.com/time/zones/hast)

    let UTCOffset = new Date().getTimezoneOffset();
    return UTCOffset >= 150 && UTCOffset <= 600;
  }

  observe(aSubject, aTopic) {
    log.info(`Observed ${aTopic}`);
    switch (aTopic) {
      case GEOLOCATION_TOPIC: {
        // aSubject from GeoLocation.cpp will be a GeoPosition
        // DOM Object, but from tests we will receive a
        // wrappedJSObject so handle both here.
        let coords = aSubject.coords || aSubject.wrappedJSObject.coords;
        this.#seenLocation({
          lat: coords.latitude,
          lng: coords.longitude,
        });
        break;
      }
    }
  }

  // For tests to create blank new instances.
  newInstance() {
    return new RegionDetector();
  }
}

export let Region = new RegionDetector();
Region.init();
