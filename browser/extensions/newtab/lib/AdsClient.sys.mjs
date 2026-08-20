/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
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

const PREF_ADSCLIENT_ENABLED = "unifiedAds.adsClient.enabled";
const PREF_ADSCLIENT_LOG =
  "browser.newtabpage.activity-stream.unifiedAds.adsClient.log";

// Viaduct OHTTP channel the ads-client sends over (matches OHTTP_CHANNEL_ID in
// the vendored ads-client crate).
const OHTTP_CHANNEL_ID = "ads-client";
const PREF_OHTTP_ENABLED =
  "browser.newtabpage.activity-stream.unifiedAds.ohttp.enabled";
const PREF_OHTTP_RELAY_URL =
  "browser.newtabpage.activity-stream.discoverystream.ohttp.relayURL";
const PREF_OHTTP_CONFIG_URL =
  "browser.newtabpage.activity-stream.discoverystream.ohttp.configURL";

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
   * Options for requestTileAds/requestSpocAds, with the OHTTP channel
   * configured from prefs, and flags from passed in prefValues.
   *
   * @param {object} prefValues The New Tab store's Prefs.values.
   * @returns {MozAdsRequestOptions}
   */
  requestOptions(prefValues) {
    return new lazy.MozAdsRequestOptions({
      flags: new Map(Object.entries(prefValues?.adsBackendConfig || {})),
      ohttp: this.#configureOhttp(),
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
      return lazy.MozAdsClientBuilder.init()
        .environment(lazy.MozAdsEnvironment.PROD)
        .telemetry(this.buildTelemetry())
        .build();
    } catch (error) {
      console.error("MozAdsClient failed to initialize", error);
      return null;
    }
  }
}

export const AdsClient = new _AdsClient();
