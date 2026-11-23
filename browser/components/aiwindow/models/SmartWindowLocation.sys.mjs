/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  GeolocationUtils:
    "moz-src:///browser/components/urlbar/private/GeolocationUtils.sys.mjs",
});

const LOCATION_ENABLED_PREF = "browser.smartwindow.location.enabled";

const RETRY_INTERVAL_MS = 5 * 60 * 1000;
const MAX_FIELD_LENGTH = 100;
const CONTROL_OR_WHITESPACE = /[\p{Cc}\s]+/gu;

let retryIntervalMs = RETRY_INTERVAL_MS;

/**
 * Keeps the client's approximate location for Smart Window in memory. The
 * location comes from Merino's IP-based geolocation and is reduced to city,
 * region and country. It is never written to prefs, so it is not shared with
 * content processes.
 */
class _SmartWindowLocation {
  /**
   * The location as "city, region, country", omitting unknown parts.
   *
   * @type {string|null}
   */
  #location = null;

  /** @type {Promise<void>|null} */
  #refreshPromise = null;

  /**
   * Monotonic time of the last lookup that did not produce a location, or
   * null. Cleared when a lookup succeeds.
   *
   * @type {number|null}
   */
  #lastFailedAttemptMs = null;

  constructor() {
    Services.prefs.addObserver(LOCATION_ENABLED_PREF, this);
  }

  /**
   * Forgets the location as soon as the feature is disabled.
   *
   * @param {nsISupports} _subject - Unused.
   * @param {string} topic - Notification topic.
   */
  observe(_subject, topic) {
    if (topic == "nsPref:changed" && !isEnabled()) {
      this.#forget();
    }
  }

  /**
   * Returns the location without fetching.
   *
   * @returns {string|null} The location as "city, region, country", or null
   *   when no location is known. The location is forgotten as soon as the
   *   feature is disabled.
   */
  getCachedLocation() {
    return this.#location;
  }

  /**
   * Refreshes the location from Merino. GeolocationUtils caches Merino's
   * response, so repeated calls rarely reach the network. After a failed
   * lookup, no new lookup is made until the retry interval has passed. Calls
   * made while a refresh is pending wait for it instead of fetching again.
   * Never rejects, so callers need not await.
   *
   * @returns {Promise<void>}
   */
  async refresh() {
    if (!isEnabled()) {
      return;
    }
    if (!this.#refreshPromise) {
      if (
        this.#lastFailedAttemptMs !== null &&
        ChromeUtils.now() - this.#lastFailedAttemptMs < retryIntervalMs
      ) {
        return;
      }
      this.#refreshPromise = this.#refresh().finally(() => {
        this.#refreshPromise = null;
      });
    }
    await this.#refreshPromise;
  }

  /**
   * Fetches the geolocation and keeps its city, region and country. A failed
   * fetch keeps the previously known location.
   *
   * @returns {Promise<void>}
   */
  async #refresh() {
    let geolocation = null;
    try {
      geolocation = await lazy.GeolocationUtils.geolocation();
    } catch (error) {
      console.warn("Failed to fetch Smart Window location", error);
    }
    const location = formatLocation(
      geolocation?.city,
      geolocation?.region || geolocation?.region_code,
      geolocation?.country || geolocation?.country_code
    );
    if (!location) {
      this.#lastFailedAttemptMs = ChromeUtils.now();
      return;
    }
    this.#lastFailedAttemptMs = null;
    // The feature may have been disabled while the lookup was in flight.
    if (isEnabled()) {
      this.#location = location;
    }
  }

  /**
   * Forgets the location and allows the next refresh to fetch immediately.
   */
  #forget() {
    this.#location = null;
    this.#lastFailedAttemptMs = null;
  }
}

/**
 * Returns whether the location feature is enabled.
 *
 * @returns {boolean}
 */
function isEnabled() {
  return Services.prefs.getBoolPref(LOCATION_ENABLED_PREF, false);
}

/**
 * Formats a location as "city, region, country". Values that are not strings
 * are discarded, the others are normalized to plain single-line text, and
 * empty parts are omitted.
 *
 * @param {*} city - Candidate city name.
 * @param {*} region - Candidate region name or code.
 * @param {*} country - Candidate country name or code.
 * @returns {string|null} The formatted location, or null when every value is
 *   empty after normalization.
 */
function formatLocation(city, region, country) {
  const parts = [city, region, country].map(normalizeField).filter(Boolean);
  return parts.length ? parts.join(", ") : null;
}

/**
 * Normalizes a location value to plain single-line text. Runs of control
 * characters and whitespace become one space, the value is trimmed and then
 * capped in length.
 *
 * @param {*} value - Candidate value.
 * @returns {string} The normalized text, or an empty string when the value is
 *   not a string.
 */
function normalizeField(value) {
  if (typeof value != "string") {
    return "";
  }
  return value
    .replace(CONTROL_OR_WHITESPACE, " ")
    .trim()
    .slice(0, MAX_FIELD_LENGTH)
    .trimEnd()
    .toWellFormed();
}

/**
 * Test-only seam: overrides how long a failed lookup suppresses retries.
 *
 * @param {number|null} ms - Interval in milliseconds, or null to restore the
 *   default.
 */
export function _setRetryIntervalForTesting(ms) {
  retryIntervalMs = ms ?? RETRY_INTERVAL_MS;
}

export const SmartWindowLocation = new _SmartWindowLocation();
