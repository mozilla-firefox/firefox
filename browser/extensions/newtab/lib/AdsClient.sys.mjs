/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @import {MozAdsCacheConfig, MozAdsCallbackOptions, MozAdsClient, MozAdsEnvironment, MozAdsRequestOptions, MozAdsTelemetry} from "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAdsClient.sys.mjs"
 */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  MozAdsCacheConfig:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAdsClient.sys.mjs",
  MozAdsCallbackOptions:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAdsClient.sys.mjs",
  MozAdsClientBuilder:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAdsClient.sys.mjs",
  MozAdsEnvironment:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAdsClient.sys.mjs",
  MozAdsRequestOptions:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAdsClient.sys.mjs",
  MozAdsTelemetry:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAdsClient.sys.mjs",
  OhttpConfig:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustViaduct.sys.mjs",
  configureOhttpChannel:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustViaduct.sys.mjs",
});

// Short pref names pulled from a store's `Prefs.values`
const PREF_ADSCLIENT_ENABLED = "unifiedAds.adsClient.enabled";
const PREF_BLOCKED_LIST = "unifiedAds.blockedAds";

// Full pref names pulled from `Services.prefs`
const PREF_ADSCLIENT_LOG =
  "browser.newtabpage.activity-stream.unifiedAds.adsClient.log";
const PREF_OHTTP_ENABLED =
  "browser.newtabpage.activity-stream.unifiedAds.ohttp.enabled";
const PREF_OHTTP_RELAY_URL =
  "browser.newtabpage.activity-stream.discoverystream.ohttp.relayURL";
const PREF_OHTTP_CONFIG_URL =
  "browser.newtabpage.activity-stream.discoverystream.ohttp.configURL";
const PREF_ENDPOINT = "browser.newtabpage.activity-stream.unifiedAds.endpoint";

// Viaduct OHTTP channel the ads-client sends over (matches OHTTP_CHANNEL_ID in
// the vendored ads-client crate).
const OHTTP_CHANNEL_ID = "ads-client";

const CACHE_DB_NAME = "ads-client.sqlite";

// Standard MARS backend URLs
const PROD_MARS_URL = "https://ads.mozilla.org/";
const STAGE_MARS_URL = "https://ads.allizom.org/";

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    prefix: "AdsClient",
    maxLogLevel: Services.prefs.getBoolPref(PREF_ADSCLIENT_LOG, false)
      ? "Debug"
      : "Warn",
  });
});

/**
 * Manages the process-wide MozAdsClient singleton, exported below as
 * `AdsClient`.
 */
export class _AdsClient {
  #client;

  // Flag that gets set by `uninit` function, marking Whether or not the module has shut down.
  // This is for testing use, and if necessary, future usage in `uninit()` logic.
  #hasShutdown = false;

  get hasShutdown() {
    return this.#hasShutdown;
  }

  /**
   * @param {object} prefValues The New Tab store's Prefs.values.
   * @returns {boolean}
   */
  isEnabled(prefValues) {
    return Boolean(
      prefValues?.trainhopConfig?.adsClient?.enabled ||
      prefValues?.[PREF_ADSCLIENT_ENABLED]
    );
  }

  /**
   * @param {object} prefValues The New Tab store's Prefs.values.
   * @param {string | Array<string>} additionalBlocks Additional value(s) to add to blocks.
   * @returns {Array<string>} Trimmed, non-empty, blocked ads.
   */
  getBlocks(prefValues, additionalBlocks = []) {
    return Array.from(
      new Set(
        (prefValues[PREF_BLOCKED_LIST] ?? "")
          .split(",")
          .concat(additionalBlocks)
          .map(block => block.trim())
          .filter(block => Boolean(block))
      )
    );
  }

  /**
   * Build (once) and return the MozAdsClient singleton.
   *
   * @returns {?MozAdsClient} null if the bindings are unavailable.
   */
  getClient() {
    if (this.#client === undefined) {
      this.#client = this.#build();
    }
    return this.#client;
  }

  /**
   * Configuration for the ads-client's SQLite HTTP response cache, kept in the
   * local profile directory since it is regenerable. TTL and max size are left
   * to the component's defaults.
   *
   * @returns {MozAdsCacheConfig}
   */
  get cacheConfig() {
    return new lazy.MozAdsCacheConfig({
      dbPath: PathUtils.join(PathUtils.localProfileDir, CACHE_DB_NAME),
    });
  }

  /**
   * Configuration for the ads-client's environment.
   *
   * @returns {MozAdsEnvironment}
   */
  get environment() {
    const endpoint = Services.prefs.getStringPref(PREF_ENDPOINT, "");

    if (endpoint === PROD_MARS_URL) {
      return new lazy.MozAdsEnvironment.Prod();
    } else if (endpoint === STAGE_MARS_URL) {
      return new lazy.MozAdsEnvironment.Staging();
    }
    return new lazy.MozAdsEnvironment.Custom({ v0: endpoint });
  }

  /**
   * Options for requestTileAds/requestSpocAds, with the OHTTP channel
   * configured from prefs, and flags from passed in prefValues.
   *
   * @param {object} prefValues The New Tab store's Prefs.values.
   * @param {string | Array<string>} additionalBlocks Additional value(s) to add to blocks.
   * @param {object} relevance Relevance scores keyed by
   *  target. Only sent when the request goes over OHTTP.
   * @returns {MozAdsRequestOptions}
   */
  requestOptions(prefValues, additionalBlocks = [], relevance = {}) {
    const ohttp = this.#configureOhttp();
    return new lazy.MozAdsRequestOptions({
      blocks: this.getBlocks(prefValues, additionalBlocks),
      flags: new Map(Object.entries(prefValues?.adsBackendConfig || {})),
      ohttp,
      relevance: new Map(ohttp ? Object.entries(relevance) : []),
    });
  }

  /**
   * Options for recordClick/recordImpression/reportAd, with the OHTTP channel
   * configured from prefs.
   *
   * @returns {MozAdsCallbackOptions}
   */
  callbackOptions() {
    return new lazy.MozAdsCallbackOptions({
      ohttp: this.#configureOhttp(),
    });
  }

  /**
   * Configure the viaduct OHTTP channel the ads-client sends over.
   *
   * @returns {boolean} Whether OHTTP is configured and should be requested.
   */
  #configureOhttp() {
    const enabled = Services.prefs.getBoolPref(PREF_OHTTP_ENABLED, false);
    const relayUrl = Services.prefs.getStringPref(PREF_OHTTP_RELAY_URL, "");
    const configUrl = Services.prefs.getStringPref(PREF_OHTTP_CONFIG_URL, "");

    if (!enabled || !relayUrl || !configUrl) {
      return false;
    }

    try {
      lazy.configureOhttpChannel(
        OHTTP_CHANNEL_ID,
        new lazy.OhttpConfig({
          relayUrl,
          gatewayHost: new URL(configUrl).host,
        })
      );
      return true;
    } catch (error) {
      console.error("MozAdsClient failed to configure OHTTP channel", error);
      return false;
    }
  }

  /**
   * The Glean-backed MozAdsTelemetry the client reports through, mirroring the
   * Android wrapper in AdsClientTelemetry.kt. The class is declared inside the
   * method rather than at module scope so the lazily-loaded bindings are only
   * touched when a client is actually built.
   *
   * Recording from JS through a callback interface is a workaround for the
   * component not being able to record its own metrics; bug 2012752 is adding
   * that capability, at which point this whole class can go away.
   *
   * @param {Function} [getMetrics] Resolves the ads_client metric category. Called
   *   per recording rather than cached, so metrics backfilled by the trainhop
   *   runtime registration are picked up. Overridden in tests.
   * @returns {MozAdsTelemetry}
   */
  buildTelemetry(getMetrics = () => Glean.adsClient) {
    class GleanTelemetry extends lazy.MozAdsTelemetry {
      recordBuildCacheError(label, value) {
        this.#record("buildCacheError", label, value, m => m.set(value));
      }
      recordClientError(label, value) {
        this.#record("clientError", label, value, m => m.set(value));
      }
      recordClientOperationTotal(label) {
        this.#record("clientOperationTotal", label, "", m => m.add(1));
      }
      recordDeserializationError(label, value) {
        this.#record("deserializationError", label, value, m => m.set(value));
      }
      recordHttpCacheOutcome(label, value) {
        this.#record("httpCacheOutcome", label, value, m => m.set(value));
      }

      /**
       * @param {string} metric camelCase name of the ads_client metric.
       * @param {string} label Label the component reported the event under.
       * @param {string} value Reported value, logged for debugging.
       * @param {Function} record Called with the labeled metric to record on.
       */
      #record(metric, label, value, record) {
        lazy.logConsole.debug(`${metric}[${label}]`, value);
        try {
          record(getMetrics()[metric][label]);
        } catch (error) {
          // These callbacks are FireAndForget, so throwing here escapes at the
          // FFI boundary instead of reaching the component. Glean.adsClient is
          // also absent until the trainhop runtime registration in
          // AboutNewTabResourceMapping has run, which is not awaited.
          lazy.logConsole.error(`${metric}[${label}] not recorded`, error);
        }
      }
    }

    return new GleanTelemetry();
  }

  #build() {
    try {
      if (lazy.AsyncShutdown.profileChangeTeardown.isClosed) {
        // Corner case, where we're already in the shutdown phase while being constructed.
        // In this case, do not initialize.
        // (https://bugzilla.mozilla.org/show_bug.cgi?id=1990569#c11)
        return null;
      }

      const builtAdsClient = lazy.MozAdsClientBuilder.init()
        .environment(this.environment)
        .cacheConfig(this.cacheConfig)
        .telemetry(this.buildTelemetry())
        .build();

      // If we're not in the above corner case, then register a shutdown blocker to uninitialize.
      // Interrupt sooner prior to the `profile-before-change` phase to allow
      // all the in-progress IOs to exit.
      lazy.AsyncShutdown.profileChangeTeardown.addBlocker(
        "AdsClient: Drop uniffi callbacks and close database connections",
        async () => {
          await this.uninit(builtAdsClient);
        }
      );

      return builtAdsClient;
    } catch (error) {
      console.error("MozAdsClient failed to initialize", error);
      return null;
    }
  }

  /**
   * Uninitialize the ads-client and allow it to release any necessary resources.
   *
   * @param {?MozAdsClient} [client] Optional client passed to shutdown,
   *   defaulting to `this.#client` (eg: if `this.#client` is not set yet).
   */
  async uninit(client) {
    lazy.logConsole.info(`Uninitializing ads-client`);
    if (client) {
      await client.shutdown();
    } else if (this.#client) {
      await this.#client.shutdown();
    }
    this.#hasShutdown = true;
  }
}

export const AdsClient = new _AdsClient();
