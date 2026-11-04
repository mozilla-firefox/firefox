/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { ServiceRequest } from "resource://gre/modules/ServiceRequest.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { setTimeout } from "resource://gre/modules/Timer.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  SharedUtils: "resource://services-settings/SharedUtils.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "CaptivePortalService",
  "@mozilla.org/network/captive-portal-service;1",
  Ci.nsICaptivePortalService
);
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gNetworkLinkService",
  "@mozilla.org/network/network-link-service;1",
  Ci.nsINetworkLinkService
);

// Create a new instance of the ConsoleAPI so we can control the maxLogLevel with a pref.
// See LOG_LEVELS in Console.sys.mjs. Common examples: "all", "debug", "info",
// "warn", "error".
const log = (() => {
  const { ConsoleAPI } = ChromeUtils.importESModule(
    "resource://gre/modules/Console.sys.mjs"
  );
  return new ConsoleAPI({
    maxLogLevel: "warn",
    maxLogLevelPref: "services.settings.loglevel",
    prefix: "services.settings",
  });
})();

ChromeUtils.defineLazyGetter(lazy, "isRunningTests", () => {
  if (Services.env.get("MOZ_DISABLE_NONLOCAL_CONNECTIONS") === "1") {
    // Allow to override the server URL if non-local connections are disabled,
    // usually true when running tests.
    return true;
  }
  return false;
});

const REMOTE_ACTIVITY_DISABLED =
  Services.env.get("MOZ_REMOTE_SETTINGS_DISABLED") === "1";

// Overriding the server URL is normally disabled on Beta and Release channels,
// except under some conditions.
ChromeUtils.defineLazyGetter(lazy, "allowServerURL", () => {
  if (!AppConstants.RELEASE_OR_BETA) {
    // Always allow to override the server URL on Nightly/DevEdition.
    return true;
  }

  if (lazy.isRunningTests) {
    return true;
  }

  if (Services.env.get("MOZ_REMOTE_SETTINGS_DEVTOOLS") === "1") {
    // Allow to override the server URL when using remote settings devtools.
    return true;
  }

  // eslint-disable-next-line mozilla/valid-lazy
  if (AppConstants.REMOTE_SETTINGS_SERVER_URLS.includes(lazy.gServerURL)) {
    return true;
  }

  log.warn("Ignoring preference override of remote settings server");
  log.warn(
    "Allow by setting MOZ_REMOTE_SETTINGS_DEVTOOLS=1 in the environment"
  );
  return false;
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "gServerURL",
  "services.settings.server",
  AppConstants.REMOTE_SETTINGS_SERVER_URLS[0]
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "gPreviewEnabled",
  "services.settings.preview_enabled",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "gAttachmentsBaseUrl",
  "services.settings.base_attachments_url",
  ""
);

function _isUndefined(value) {
  return typeof value === "undefined";
}

export var Utils = {
  get SERVER_URL() {
    return lazy.allowServerURL
      ? // eslint-disable-next-line mozilla/valid-lazy
        lazy.gServerURL
      : AppConstants.REMOTE_SETTINGS_SERVER_URLS[0];
  },

  CHANGES_PATH: "/buckets/monitor/collections/changes/changeset",

  /**
   * Logger instance.
   */
  log,

  get shouldSkipRemoteActivity() {
    if (REMOTE_ACTIVITY_DISABLED) {
      return true;
    }
    if (
      (lazy.isRunningTests || Cu.isInAutomation) &&
      this.SERVER_URL == "data:,#remote-settings-dummy/v1"
    ) {
      return true;
    }
    return Services.policies?.isAllowed("remoteSettings") === false;
  },

  get CERT_CHAIN_ROOT_IDENTIFIER() {
    if (Services.env.exists("XPCSHELL_TEST_PROFILE_DIR")) {
      return Ci.nsIX509CertDB.AppXPCShellRoot;
    }
    if (
      this.SERVER_URL.match(
        /^https?:\/\/(remote-settings\.localhost|127\.0\.0\.1|localhost)(:\d+)?\/v1/
      )
    ) {
      return Ci.nsIContentSignatureVerifier.ContentSignatureLocalRoot;
    }
    if (this.SERVER_URL.includes("allizom.")) {
      return Ci.nsIContentSignatureVerifier.ContentSignatureStageRoot;
    }
    if (this.SERVER_URL.includes("dev.")) {
      return Ci.nsIContentSignatureVerifier.ContentSignatureDevRoot;
    }
    return Ci.nsIContentSignatureVerifier.ContentSignatureProdRoot;
  },

  get LOAD_DUMPS() {
    // Load dumps only if pulling data from the production server, or in tests.
    return (
      AppConstants.REMOTE_SETTINGS_SERVER_URLS.includes(this.SERVER_URL) ||
      lazy.isRunningTests
    );
  },

  get PREVIEW_MODE() {
    // Release and beta require dev-tools or tests to use preview
    if (
      AppConstants.RELEASE_OR_BETA &&
      !lazy.isRunningTests &&
      Services.env.get("MOZ_REMOTE_SETTINGS_DEVTOOLS") !== "1"
    ) {
      return false;
    }
    // We want to offer the ability to set preview mode via a preference
    // for consumers who want to pull from the preview bucket on startup.
    if (_isUndefined(this._previewModeEnabled)) {
      return lazy.gPreviewEnabled;
    }
    return !!this._previewModeEnabled;
  },

  /**
   * Internal method to enable pulling data from preview buckets.
   *
   * @param enabled
   */
  enablePreviewMode(enabled) {
    const bool2str = v =>
      // eslint-disable-next-line no-nested-ternary
      _isUndefined(v) ? "unset" : v ? "enabled" : "disabled";
    this.log.debug(
      `Preview mode: ${bool2str(this._previewModeEnabled)} -> ${bool2str(
        enabled
      )}`
    );
    this._previewModeEnabled = enabled;
  },

  /**
   * Returns the actual bucket name to be used. When preview mode is enabled,
   * this adds the *preview* suffix.
   *
   * See also `SharedUtils.loadJSONDump()` which strips the preview suffix to identify
   * the packaged JSON file.
   *
   * @param bucketName the client bucket
   * @returns the final client bucket depending whether preview mode is enabled.
   */
  actualBucketName(bucketName) {
    let actual = bucketName.replace("-preview", "");
    if (this.PREVIEW_MODE) {
      actual += "-preview";
    }
    return actual;
  },

  /**
   * Check if network is down.
   *
   * Note that if this returns false, it does not guarantee
   * that network is up.
   *
   * @return {bool} Whether network is down or not.
   */
  get isOffline() {
    try {
      return (
        Services.io.offline ||
        lazy.CaptivePortalService.state ==
          lazy.CaptivePortalService.LOCKED_PORTAL ||
        !lazy.gNetworkLinkService.isLinkUp
      );
    } catch (ex) {
      log.warn("Could not determine network status.", ex);
    }
    return false;
  },

  /**
   * A wrapper around `ServiceRequest` that behaves like `fetch()`.
   *
   * Use this in order to leverage the `beConservative` flag, for
   * example to avoid using HTTP3 to fetch critical data.
   *
   * @param input a resource
   * @param init request options
   * @returns a Response object
   */
  async fetch(input, init = {}) {
    return new Promise(function (resolve, reject) {
      const request = new ServiceRequest();
      const { method = "GET", headers = {}, bypassProxy = false } = init;
      function fallbackOrReject(err) {
        if (
          // At most one recursive Utils.fetch call (bypassProxy=false to true).
          bypassProxy ||
          Services.startup.shuttingDown ||
          Utils.isOffline ||
          !request.isProxied ||
          !request.bypassProxyEnabled
        ) {
          reject(err);
          return;
        }
        ServiceRequest.logProxySource(request.channel, "remote-settings");
        resolve(Utils.fetch(input, { ...init, bypassProxy: true }));
      }

      request.onerror = () =>
        fallbackOrReject(new TypeError("NetworkError: Network request failed"));
      request.ontimeout = () =>
        fallbackOrReject(new TypeError("Timeout: Network request failed"));
      request.onabort = () =>
        fallbackOrReject(new DOMException("Aborted", "AbortError"));
      request.onload = () => {
        // Parse raw response headers into `Headers` object.
        const headers = new Headers();
        const rawHeaders = request.getAllResponseHeaders();
        rawHeaders
          .trim()
          .split(/[\r\n]+/)
          .forEach(line => {
            const parts = line.split(": ");
            const header = parts.shift();
            const value = parts.join(": ");
            headers.set(header, value);
          });

        const responseAttributes = {
          status: request.status,
          statusText: request.statusText,
          url: request.responseURL,
          headers,
        };
        resolve(new Response(request.response, responseAttributes));
      };

      request.open(method, input, { bypassProxy });
      // By default, XMLHttpRequest converts the response based on the
      // Content-Type header, or UTF-8 otherwise. This may mangle binary
      // responses. Avoid that by requesting the raw bytes.
      request.responseType = "arraybuffer";

      for (const [name, value] of Object.entries(headers)) {
        request.setRequestHeader(name, value);
      }

      request.send();
    });
  },

  _baseAttachmentsURLPromise: null,

  /**
   * Retrieves the base URL for attachments from the server configuration.
   *
   * If the URL has been previously fetched and cached, it returns the cached URL.
   *
   * Note: it is important to not hard-code this value in consumer code.
   *
   * @async
   * @function baseAttachmentsURL
   * @memberof Utils
   * @param {object} options Some download options.
   * @param {number} options.retries Number of times server fetch should be retried (default: `0`)
   * @param {number} options.retryWaitMsec Wait milliseconds between each retry (default: `1000`)
   * @returns {Promise<string>} A promise that resolves to the base URL for attachments.
   *
   * @throws {Error} If there is an error fetching or parsing the server response.
   *
   * @example
   * const attachmentsURL = await Downloader.baseAttachmentsURL();
   * console.log(attachmentsURL);
   */
  async baseAttachmentsURL(options = {}) {
    if (Utils._baseAttachmentsURLPromise) {
      // Wait for ongoing call to finish.
      return Utils._baseAttachmentsURLPromise;
    }

    // We save the obtained value in a pref, so that it's only fetched from
    // the server on first use. When the remote server changes (eg. using the DevTools for QA)
    // the value will be fetched again from the server.
    const [server_url = "", attachments_url = ""] =
      lazy.gAttachmentsBaseUrl.split("|", 2);
    if (
      server_url == Utils.SERVER_URL &&
      /^https?:\/\/(.+)\/$/.test(attachments_url)
    ) {
      return attachments_url;
    }

    // Saved attachments URL does not match current server (eg. on empty profile, or
    // user switched remote environment), fetch it again from the server.
    const { retries = 0, retryWaitMsec = 1000 } = options;
    Utils._baseAttachmentsURLPromise = (async () => {
      let retried = 0;
      let serverInfo;
      while (retried <= retries) {
        try {
          const resp = await Utils.fetch(`${Utils.SERVER_URL}/`);
          if (!resp.ok) {
            throw new Error(`Failed to fetch server info: ${resp.status}`);
          }
          serverInfo = await resp.json();
          break;
        } catch (error) {
          retried++;
          if (retried > retries) {
            throw error;
          }
          await new Promise(resolve =>
            setTimeout(resolve, retryWaitMsec * retried)
          );
        }
      }
      // Server capabilities expose attachments configuration.
      const {
        capabilities: {
          attachments: { base_url },
        },
      } = serverInfo;
      Services.prefs.setStringPref(
        "services.settings.base_attachments_url",
        `${Utils.SERVER_URL}|${base_url}`
      );
      return base_url;
    })();

    try {
      return await Utils._baseAttachmentsURLPromise;
    } finally {
      Utils._baseAttachmentsURLPromise = null;
    }
  },

  /**
   * Check if local data exist for the specified client.
   *
   * @param {RemoteSettingsClient} client
   * @return {bool} Whether it exists or not.
   */
  async hasLocalData(client) {
    const timestamp = await client.db.getLastModified();
    return timestamp !== null;
  },

  /**
   * Check if we ship a JSON dump for the specified bucket and collection.
   *
   * @param {string} bucket
   * @param {string} collection
   * @return {bool} Whether it is present or not.
   */
  async hasLocalDump(bucket, collection) {
    try {
      await fetch(
        `resource://app/defaults/settings/${bucket}/${collection}.json`,
        {
          method: "HEAD",
        }
      );
      return true;
    } catch (e) {
      return false;
    }
  },

  /**
   * Look up the last modification time of the JSON dump.
   *
   * @param {string} bucket
   * @param {string} collection
   * @return {int} The last modification time of the dump. -1 if non-existent.
   */
  async getLocalDumpLastModified(bucket, collection) {
    if (!this._dumpStats) {
      if (!this._dumpStatsInitPromise) {
        this._dumpStatsInitPromise = (async () => {
          try {
            let res = await fetch(
              "resource://app/defaults/settings/last_modified.json"
            );
            this._dumpStats = await res.json();
          } catch (e) {
            log.warn(`Failed to load last_modified.json: ${e}`);
            this._dumpStats = {};
          }
          delete this._dumpStatsInitPromise;
        })();
      }
      await this._dumpStatsInitPromise;
    }
    const identifier = `${bucket}/${collection}`;
    let lastModified = this._dumpStats[identifier];
    if (lastModified === undefined) {
      const { timestamp: dumpTimestamp } = await lazy.SharedUtils.loadJSONDump(
        bucket,
        collection
      );
      // Client recognize -1 as missing dump.
      lastModified = dumpTimestamp ?? -1;
      this._dumpStats[identifier] = lastModified;
    }
    return lastModified;
  },

  /**
   * Compute the current server time from the response headers.
   *
   * @param {Headers} headers response headers
   * @returns {number} server time in milliseconds
   */
  serverTimeMillis(headers) {
    const serverTimeMillis = Date.parse(headers.get("Date"));
    // Since the response is served via a CDN, the Date header value could have been cached.
    const cacheAgeSeconds = headers.has("Age")
      ? parseInt(headers.get("Age"), 10)
      : 0;
    return serverTimeMillis + cacheAgeSeconds * 1000;
  },

  /**
   * Fetch the list of remote collections and their timestamp.
   * ```
   *   {
   *     "timestamp": 1486545678,
   *     "changes":[
   *       {
   *         "host":"kinto-ota.dev.mozaws.net",
   *         "last_modified":1450717104423,
   *         "bucket":"blocklists",
   *         "collection":"certificates"
   *       },
   *       ...
   *     ],
   *     "metadata": {}
   *   }
   * ```
   *
   * @param {string} serverUrl         The server URL (eg. `https://server.org/v1`)
   * @param {int}    expectedTimestamp The timestamp that the server is supposed to return.
   *                                   We obtained it from the Megaphone notification payload,
   *                                   and we use it only for cache busting (Bug 1497159).
   * @param {string} lastEtag          (optional) The Etag of the latest poll to be matched
   *                                   by the server (eg. `"123456789"`).
   * @param {object} filters
   */
  async fetchLatestChanges(serverUrl, options = {}) {
    const { expectedTimestamp, lastEtag = "", filters = {} } = options;

    let url = serverUrl + Utils.CHANGES_PATH;
    const params = {
      ...filters,
      _expected: expectedTimestamp ?? 0,
    };
    if (lastEtag != "") {
      params._since = lastEtag;
    }
    if (params) {
      url +=
        "?" +
        Object.entries(params)
          .map(([k, v]) => `${k}=${encodeURIComponent(v)}`)
          .join("&");
    }
    const response = await Utils.fetch(url);

    if (response.status >= 500) {
      throw new Error(`Server error ${response.status} ${response.statusText}`);
    }

    const is404FromCustomServer =
      response.status == 404 &&
      Services.prefs.prefHasUserValue("services.settings.server");

    const ct = response.headers.get("Content-Type");
    if (!is404FromCustomServer && (!ct || !ct.includes("application/json"))) {
      throw new Error(`Unexpected content-type "${ct}"`);
    }

    let payload;
    try {
      payload = await response.json();
    } catch (e) {
      payload = e.message;
    }

    if (!payload.hasOwnProperty("changes")) {
      // If the server is failing, the JSON response might not contain the
      // expected data. For example, real server errors (Bug 1259145)
      // or dummy local server for tests (Bug 1481348)
      if (!is404FromCustomServer) {
        throw new Error(
          `Server error ${url} ${response.status} ${
            response.statusText
          }: ${JSON.stringify(payload)}`
        );
      }
    }

    const { changes = [], timestamp } = payload;

    const serverTimeMillis = Utils.serverTimeMillis(response.headers);

    // Age of data (time between publication and now).
    const ageSeconds = (serverTimeMillis - timestamp) / 1000;

    // Check if the server asked the clients to back off.
    let backoffSeconds;
    if (response.headers.has("Backoff")) {
      const value = parseInt(response.headers.get("Backoff"), 10);
      if (!isNaN(value)) {
        backoffSeconds = value;
      }
    }

    return {
      changes,
      timestamp,
      serverTimeMillis,
      backoffSeconds,
      ageSeconds,
    };
  },

  /**
   * Test if a single object matches all given filters.
   *
   * @param  {object} filters  The filters object.
   * @param  {object} entry    The object to filter.
   * @return {boolean}
   */
  filterObject(filters, entry) {
    return Object.entries(filters).every(([filter, value]) => {
      if (Array.isArray(value)) {
        return value.some(candidate => candidate === entry[filter]);
      } else if (typeof value === "object") {
        return Utils.filterObject(value, entry[filter]);
      } else if (!Object.prototype.hasOwnProperty.call(entry, filter)) {
        log.debug(`The property ${filter} does not exist`);
        return false;
      }
      return entry[filter] === value;
    });
  },

  /**
   * Sorts records in a list according to a given ordering.
   *
   * @param  {string} order The ordering, eg. `-last_modified`.
   * @param  {Array}  list  The collection to order.
   * @return {Array}
   */
  sortObjects(order, list) {
    const hasDash = order[0] === "-";
    const field = hasDash ? order.slice(1) : order;
    const direction = hasDash ? -1 : 1;
    return list.slice().sort((a, b) => {
      if (a[field] && _isUndefined(b[field])) {
        return direction;
      }
      if (b[field] && _isUndefined(a[field])) {
        return -direction;
      }
      if (_isUndefined(a[field]) && _isUndefined(b[field])) {
        return 0;
      }
      return a[field] > b[field] ? direction : -direction;
    });
  },

  /**
   * Fetches and extracts a bundle of changesets from the server.
   *
   * This function downloads a JSON file with all changesets required during startup, compressed as LZ4.
   * It writes the LZ4 file to a temporary location, extracts it, and return the array of changesets.
   * We chose to use LZ4 instead of Zip because extraction can happen off the main thread.
   *
   * @async
   * @function fetchChangesetsBundle
   * @memberof Utils
   * @returns {Promise<Array<object>>} A promise that resolves to an array of parsed changesets.
   *
   * @throws {Error} Throws an error if there is an issue fetching the server info or the changeset bundle,
   *                 or if there is an error during the extraction and parsing of the changesets.
   */
  async fetchChangesetsBundle() {
    const tmpLz4File = await IOUtils.createUniqueFile(
      PathUtils.tempDir,
      "remote-settings-startup-bundle-"
    );
    try {
      const baseUrl = await Utils.baseAttachmentsURL();
      const bundleUrl = `${baseUrl}bundles/startup.json.mozlz4`;
      const bundleResp = await Utils.fetch(bundleUrl);
      if (!bundleResp.ok) {
        throw new Error(`Cannot fetch changeset bundle from ${bundleUrl}`);
      }
      // Write down the LZ4 in a temporary file.
      const downloaded = await bundleResp.arrayBuffer();
      await IOUtils.write(tmpLz4File, new Uint8Array(downloaded), {
        tmpPath: `${tmpLz4File}.tmp`,
      });
      // Decompress using LZ4
      const changesetsJson = await IOUtils.readUTF8(tmpLz4File, {
        decompress: true,
      });
      // Parse JSON from string
      return JSON.parse(changesetsJson);
    } finally {
      await IOUtils.remove(tmpLz4File, { ignoreAbsent: true });
    }
  },
};
