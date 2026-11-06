/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// eslint-disable-next-line mozilla/use-static-import
const { newTabAttributionService } = ChromeUtils.importESModule(
  "resource://newtab/lib/NewTabAttributionService.sys.mjs"
);

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "NewTabAttributionParent",
    // @backward-compat { version 162 }
    // Older versions ignore logModule and name the module after the prefix.
    // Remove prefix: once 162 reaches Release.
    prefix: "NewTabAttributionParent",
    maxLogLevel: "Warn",
  });
});

ChromeUtils.defineESModuleGetters(lazy, {
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
});

/**
 * Allowed fields in the conversion event payload from advertisers.
 * - partnerId: Mozilla-generated UUID associated with the advertiser
 * - impressionType: How attribution should be determined (view/click/default)
 * - lookbackDays: Number of days in the past to look for an attributable interaction (1, 7, 14, or 30)
 */
const CONVERSION_KEYS = new Set([
  "partnerId",
  "impressionType",
  "lookbackDays",
]);

/**
 * Checks if an object is a plain object (not null, not an array, not a function).
 * This is necessary because JSWindowActor messages use structured clones,
 * which have a different prototypes than normal objects
 *
 * @param {object} obj - The value to check.
 * @returns {boolean} True if obj is a plain object, false otherwise.
 */
function isPlainObject(obj) {
  return (
    typeof obj === "object" &&
    obj !== null &&
    !Array.isArray(obj) &&
    Object.prototype.toString.call(obj) === "[object Object]"
  );
}

const ATTRIBUTION_ALLOWLIST_COLLECTION = "newtab-attribution-allowlist";

let gAllowList = new Set([]);
let gAllowListClient = null;

/**
 * Parent-side JSWindowActor for handling attribution conversion events.
 *
 * This actor receives FirefoxConversionNotification custom events from advertiser websites
 *
 * Upon successful validation, the conversion data is passed to NewTabAttributionService
 */
export class AttributionParent extends JSWindowActorParent {
  constructor() {
    super();
    this._onSync = this.onSync.bind(this);
  }

  /**
   * TEST-ONLY: Override the allowlist from a test.
   *
   * @param {Array<string>} origins - Array of origin strings to allow.
   */
  setAllowListForTest(origins = []) {
    gAllowList = new Set(origins);
  }

  /**
   * TEST-ONLY: Reset the Remote Settings client.
   */
  resetRemoteSettingsClientForTest() {
    gAllowListClient = null;
  }

  /**
   * This thin wrapper around lazy.RemoteSettings makes it easier for us to write
   * automated tests that simulate responses from this fetch.
   */
  RemoteSettings(...args) {
    return lazy.RemoteSettings(...args);
  }

  /**
   * Updates the global allowlist with the provided records.
   *
   * @param {Array} records - Array of Remote Settings records containing domain fields.
   */
  updateAllowList(records) {
    if (records?.length) {
      const domains = records.map(record => record.domain);
      gAllowList = new Set(domains);
    } else {
      gAllowList = new Set([]);
    }
  }

  /**
   * Retrieves the allow list of advertiser origins from Remote Settings.
   * Populates the internal gAllowList set with the retrieved origins.
   */
  async retrieveAllowList() {
    try {
      if (!gAllowListClient) {
        gAllowListClient = this.RemoteSettings(
          ATTRIBUTION_ALLOWLIST_COLLECTION
        );
        gAllowListClient.on("sync", this._onSync);
        const records = await gAllowListClient.get();
        this.updateAllowList(records);
      }
    } catch (error) {
      lazy.logConsole.error(
        `AttributionParent: failed to retrieve allow list: ${error}`
      );
    }
  }

  /**
   * Handles Remote Settings sync events.
   * Updates the allow list when the collection changes.
   *
   * @param {object} event - The sync event object.
   * @param {object} event.data - The sync event data.
   * @param {Array} event.data.current - The current records after sync.
   */
  onSync({ data: { current } }) {
    this.updateAllowList(current);
  }

  didDestroy() {
    if (gAllowListClient) {
      gAllowListClient.off("sync", this._onSync);
    }
  }

  /**
   * Validates a conversion event payload from an advertiser.
   * Ensures all required fields are present, correctly typed, and within valid ranges.
   *
   * @param {*} data - The conversion data to validate.
   * @returns {object|null} The validated conversion data object, or null if validation fails.
   *
   * Validation checks:
   * - Must be a plain object
   * - Must contain only allowed keys (partnerId, impressionType, lookbackDays)
   * - partnerId: must be a non-empty string
   * - impressionType: must be a string
   * - lookbackDays: must be a positive number
   */
  validateConversion(data) {
    // confirm that data is an object
    if (!isPlainObject(data)) {
      return null;
    }

    // Check that only allowed keys are present
    for (const key of Object.keys(data)) {
      if (!CONVERSION_KEYS.has(key)) {
        return null;
      }
    }

    // Validate required fields are present
    if (
      !data.partnerId ||
      !data.impressionType ||
      data.lookbackDays === undefined
    ) {
      return null;
    }

    // Validate types
    if (typeof data.partnerId !== "string") {
      return null;
    }

    if (typeof data.impressionType !== "string") {
      return null;
    }

    if (typeof data.lookbackDays !== "number" || data.lookbackDays <= 0) {
      return null;
    }

    return data;
  }

  /**
   * Receives and processes conversion event messages from the child actor.
   * This method is called when a FirefoxConversionNotification custom event is triggered
   * on an advertiser's website.
   *
   * @param {object} message - The message from the child actor.
   * @param {object} message.data - The message data.
   * @param {object} message.data.detail - The custom event detail.
   * @param {object} message.data.detail.conversion - The conversion payload.
   * @returns {Promise}
   */
  async receiveMessage(message) {
    let principal = this.manager.documentPrincipal;

    // Only accept conversion events from secure origins (HTTPS)
    if (!principal.isOriginPotentiallyTrustworthy) {
      lazy.logConsole.error(
        `AttributionParent: conversion events must be sent over HTTPS`
      );
      return;
    }

    if (!gAllowList.size) {
      await this.retrieveAllowList();
    }

    // Only accept conversion events from allowlisted origins
    if (!gAllowList.has(principal.originNoSuffix)) {
      lazy.logConsole.error(
        `AttributionParent: conversion events must come from the allow list`
      );
      return;
    }

    const { detail } = message.data || {};

    if (detail) {
      const validatedConversion = this.validateConversion(detail);

      if (!validatedConversion) {
        lazy.logConsole.error(
          `AttributionParent: rejected invalid conversion payload from ${principal}`
        );
        return;
      }

      const { partnerId, lookbackDays, impressionType } = validatedConversion;
      await newTabAttributionService.onAttributionConversion(
        partnerId,
        lookbackDays,
        impressionType
      );
    }
  }
}
