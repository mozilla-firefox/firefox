/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * This module runs the automated heuristics to enable/disable DoH on different
 * networks. Heuristics are run at startup and upon network changes.
 * Heuristics are disabled if the user sets their DoH provider or mode manually.
 */
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  ClientID: "resource://gre/modules/ClientID.sys.mjs",
  DoHConfigController: "resource://gre/modules/DoHConfig.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  Heuristics: "resource://gre/modules/DoHHeuristics.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  Preferences: "resource://gre/modules/Preferences.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

// When this is set we suppress automatic TRR selection beyond dry-run as well
// as sending observer notifications during heuristics throttling.
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "kIsInAutomation",
  "doh-rollout._testing",
  false
);

// We wait until the network has been stably up for this many milliseconds
// before triggering a heuristics run.
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "kNetworkDebounceTimeout",
  "doh-rollout.network-debounce-timeout",
  1000
);

// If consecutive heuristics runs are attempted within this period after a first,
// we suppress them for this duration, at the end of which point we decide whether
// to do one coalesced run or to extend the timer if the rate limit was exceeded.
// Note that the very first run is allowed, after which we start the timer.
// This throttling is necessary due to evidence of clients that experience
// network volatility leading to thousands of runs per hour. See bug 1626083.
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "kHeuristicsThrottleTimeout",
  "doh-rollout.heuristics-throttle-timeout",
  15000
);

// After the throttle timeout described above, if there are more than this many
// heuristics attempts during the timeout, we restart the timer without running
// heuristics. Thus, heuristics are suppressed completely as long as the rate
// exceeds this limit.
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "kHeuristicsRateLimit",
  "doh-rollout.heuristics-throttle-rate-limit",
  2
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gCaptivePortalService",
  "@mozilla.org/network/captive-portal-service;1",
  "nsICaptivePortalService"
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gNetworkLinkService",
  "@mozilla.org/network/network-link-service;1",
  "nsINetworkLinkService"
);

// Stores whether we've done first-run.
const FIRST_RUN_PREF = "doh-rollout.doneFirstRun";

// Set when we detect that the user set their DoH provider or mode manually.
// If set, we don't run heuristics.
const DISABLED_PREF = "doh-rollout.disable-heuristics";

// Set when we detect either a non-DoH enterprise policy, or a DoH policy that
// tells us to disable it. This pref's effect is to suppress the opt-out CFR.
const SKIP_HEURISTICS_PREF = "doh-rollout.skipHeuristicsCheck";

// Whether to clear doh-rollout.mode on shutdown. When false, the mode value
// that exists at shutdown will be used at startup until heuristics re-run.
const CLEAR_ON_SHUTDOWN_PREF = "doh-rollout.clearModeOnShutdown";

const BREADCRUMB_PREF = "doh-rollout.self-enabled";

// Necko TRR prefs to watch for user-set values.
const NETWORK_TRR_MODE_PREF = "network.trr.mode";
const NETWORK_TRR_URI_PREF = "network.trr.uri";

const ROLLOUT_MODE_PREF = "doh-rollout.mode";
const ROLLOUT_URI_PREF = "doh-rollout.uri";

const TRR_SELECT_DRY_RUN_RESULT_PREF =
  "doh-rollout.trr-selection.dry-run-result";

const kLinkStatusChangedTopic = "network:link-status-changed";
const kConnectivityTopic = "network:captive-portal-connectivity-changed";
const kPrefChangedTopic = "nsPref:changed";

// Helper function to hash the network ID concatenated with telemetry client ID.
// This prevents us from being able to tell if 2 clients are on the same network.
function getHashedNetworkID() {
  let currentNetworkID = lazy.gNetworkLinkService.networkID;
  if (!currentNetworkID) {
    return "";
  }

  let hasher = Cc["@mozilla.org/security/hash;1"].createInstance(
    Ci.nsICryptoHash
  );

  hasher.init(Ci.nsICryptoHash.SHA256);
  // Concat the client ID with the network ID before hashing.
  let clientNetworkID = lazy.ClientID.getClientID() + currentNetworkID;
  hasher.update(
    clientNetworkID.split("").map(c => c.charCodeAt(0)),
    clientNetworkID.length
  );
  return hasher.finish(true);
}

export const DoHController = {
  _heuristicsAreEnabled: false,

  async init() {
    await lazy.DoHConfigController.initComplete;

    Services.obs.addObserver(this, lazy.DoHConfigController.kConfigUpdateTopic);
    lazy.Preferences.observe(NETWORK_TRR_MODE_PREF, this);
    lazy.Preferences.observe(NETWORK_TRR_URI_PREF, this);

    if (lazy.DoHConfigController.currentConfig.enabled) {
      // At init time set these heuristics to false if we may run heuristics
      for (let key of lazy.Heuristics.Telemetry.heuristicNames()) {
        Glean.networking.dohHeuristicEverTripped[key].set(false);
      }

      await this.maybeEnableHeuristics();
    } else if (lazy.Preferences.get(FIRST_RUN_PREF, false)) {
      await this.rollback();
    }

    this._asyncShutdownBlocker = async () => {
      await this.disableHeuristics("shutdown");
    };

    lazy.AsyncShutdown.profileBeforeChange.addBlocker(
      "DoHController: clear state and remove observers",
      this._asyncShutdownBlocker
    );

    lazy.Preferences.set(FIRST_RUN_PREF, true);
  },

  // Also used by tests to reset DoHController state (prefs are not cleared
  // here - tests do that when needed between _uninit and init).
  async _uninit() {
    Services.obs.removeObserver(
      this,
      lazy.DoHConfigController.kConfigUpdateTopic
    );
    lazy.Preferences.ignore(NETWORK_TRR_MODE_PREF, this);
    lazy.Preferences.ignore(NETWORK_TRR_URI_PREF, this);
    lazy.AsyncShutdown.profileBeforeChange.removeBlocker(
      this._asyncShutdownBlocker
    );
    await this.disableHeuristics("shutdown");
  },

  // Called to reset state when a new config is available.
  resetPromise: Promise.resolve(),
  async reset() {
    this.resetPromise = this.resetPromise.then(async () => {
      await this._uninit();
      await this.init();
      Services.obs.notifyObservers(null, "doh:controller-reloaded");
    });

    return this.resetPromise;
  },

  // The "maybe" is because there are two cases when we don't enable heuristics:
  // 1. If we detect that TRR mode or URI have user values, or we previously
  //    detected this (i.e. DISABLED_PREF is true)
  // 2. If there are any non-DoH enterprise policies active
  async maybeEnableHeuristics() {
    if (lazy.Preferences.get(DISABLED_PREF)) {
      return;
    }

    let policyResult = await lazy.Heuristics.checkEnterprisePolicy();

    if (policyResult != "no_policy_set") {
      switch (policyResult) {
        case "policy_without_doh":
          Glean.networking.dohHeuristicsResult.set(
            lazy.Heuristics.Telemetry.enterprisePresent
          );
          await this.setState("policyDisabled");
          break;
        case "disable_doh":
          Glean.networking.dohHeuristicsResult.set(
            lazy.Heuristics.Telemetry.enterpriseDisabled
          );
          await this.setState("policyDisabled");
          break;
        case "enable_doh":
          // The TRR mode has already been set, so theoretically we should not get here.
          // XXX: should we skip heuristics or continue?
          // TODO: Make sure we use the correct URL if the policy defines one.
          Glean.networking.dohHeuristicsResult.set(
            lazy.Heuristics.Telemetry.enterpriseEnabled
          );
          break;
      }
      lazy.Preferences.set(SKIP_HEURISTICS_PREF, true);
      return;
    }

    lazy.Preferences.reset(SKIP_HEURISTICS_PREF);

    if (
      lazy.Preferences.isSet(NETWORK_TRR_MODE_PREF) ||
      lazy.Preferences.isSet(NETWORK_TRR_URI_PREF)
    ) {
      await this.setState("manuallyDisabled");
      lazy.Preferences.set(DISABLED_PREF, true);
      return;
    }

    await this.runTRRSelection();
    // If we enter this branch it means that no automatic selection was possible.
    // In this case, we try to set a fallback (as defined by DoHConfigController).
    if (!lazy.Preferences.isSet(ROLLOUT_URI_PREF)) {
      let uri = lazy.DoHConfigController.currentConfig.fallbackProviderURI;

      // If part of the treatment branch use the URL from the experiment.
      try {
        let ohttpURI = lazy.NimbusFeatures.dooh.getVariable("ohttpUri");
        if (ohttpURI) {
          uri = ohttpURI;
        }
      } catch (e) {
        console.error(`Error getting dooh.ohttpURI: ${e.message}`);
      }

      lazy.Preferences.set(ROLLOUT_URI_PREF, uri || "");
    }
    this.runHeuristicsThrottled("startup");
    Services.obs.addObserver(this, kLinkStatusChangedTopic);
    Services.obs.addObserver(this, kConnectivityTopic);

    this._heuristicsAreEnabled = true;
  },

  _runsWhileThrottling: 0,
  _wasThrottleExtended: false,
  _throttleHeuristics() {
    if (lazy.kHeuristicsThrottleTimeout < 0) {
      // Skip throttling in tests that set timeout to a negative value.
      return false;
    }

    if (this._throttleTimer) {
      // Already throttling - nothing to do.
      this._runsWhileThrottling++;
      return true;
    }

    this._runsWhileThrottling = 0;

    this._throttleTimer = lazy.setTimeout(
      this._handleThrottleTimeout.bind(this),
      lazy.kHeuristicsThrottleTimeout
    );

    return false;
  },

  _handleThrottleTimeout() {
    delete this._throttleTimer;
    if (this._runsWhileThrottling > lazy.kHeuristicsRateLimit) {
      // During the throttle period, we saw that the rate limit was exceeded.
      // We extend the throttle period, and don't bother running heuristics yet.
      this._wasThrottleExtended = true;
      // Restart the throttle timer.
      this._throttleHeuristics();
      if (lazy.kIsInAutomation) {
        Services.obs.notifyObservers(null, "doh:heuristics-throttle-extend");
      }
      return;
    }

    // If this was an extended throttle and there were no runs during the
    // extended period, we still want to run heuristics, since the extended
    // throttle implies we had a non-zero number of attempts before extension.
    if (this._runsWhileThrottling > 0 || this._wasThrottleExtended) {
      this.runHeuristicsThrottled("throttled");
    }

    this._wasThrottleExtended = false;

    if (lazy.kIsInAutomation) {
      Services.obs.notifyObservers(null, "doh:heuristics-throttle-done");
    }
  },

  runHeuristicsThrottled(evaluateReason) {
    // _throttleHeuristics returns true if we've already witnessed a run and the
    // timeout period hasn't lapsed yet. If it does so, we suppress this run.
    if (this._throttleHeuristics()) {
      return;
    }

    // _throttleHeuristics returned false - we're good to run heuristics.
    // At this point the timer has been started and subsequent calls will be
    // suppressed if it hasn't fired yet.
    this.runHeuristics(evaluateReason);
  },

  async runHeuristics(evaluateReason) {
    let start = Date.now();

    Glean.networking.dohHeuristicsAttempts.add(1);
    Glean.networking.dohHeuristicsResult.set(
      lazy.Heuristics.Telemetry.incomplete
    );
    let results = await lazy.Heuristics.run();

    if (
      !lazy.gNetworkLinkService.isLinkUp ||
      this._lastDebounceTimestamp > start ||
      lazy.gCaptivePortalService.state ==
        lazy.gCaptivePortalService.LOCKED_PORTAL
    ) {
      // If the network is currently down or there was a debounce triggered
      // while we were running heuristics, it means the network fluctuated
      // during this heuristics run. We simply discard the results in this case.
      // Same thing if there was another heuristics run triggered or if we have
      // detected a locked captive portal while this one was ongoing.
      Glean.networking.dohHeuristicsResult.set(
        lazy.Heuristics.Telemetry.ignored
      );
      return;
    }

    let decision = Object.values(results).includes(lazy.Heuristics.DISABLE_DOH)
      ? lazy.Heuristics.DISABLE_DOH
      : lazy.Heuristics.ENABLE_DOH;

    let getCaptiveStateString = () => {
      switch (lazy.gCaptivePortalService.state) {
        case lazy.gCaptivePortalService.NOT_CAPTIVE:
          return "not_captive";
        case lazy.gCaptivePortalService.UNLOCKED_PORTAL:
          return "unlocked";
        case lazy.gCaptivePortalService.LOCKED_PORTAL:
          return "locked";
        default:
          return "unknown";
      }
    };

    let resultsForTelemetry = {
      evaluateReason,
      steeredProvider: "",
      captiveState: getCaptiveStateString(),
      // NOTE: This might not yet be available after a network change. We mainly
      // care about the startup case though - we want to look at whether the
      // heuristics result is consistent for networkIDs often seen at startup.
      // TODO: Use this data to implement cached results to use early at startup.
      networkID: getHashedNetworkID(),
    };

    const oHTTPexperiment = lazy.ExperimentAPI.getExperimentMetaData({
      featureId: "dooh",
    });

    // When the OHTTP experiment is active we don't want to enable steering.
    if (results.steeredProvider && !oHTTPexperiment) {
      Services.dns.setDetectedTrrURI(results.steeredProvider.uri);
      resultsForTelemetry.steeredProvider = results.steeredProvider.id;
    }

    this.setHeuristicResult(Ci.nsITRRSkipReason.TRR_UNSET);
    if (decision === lazy.Heuristics.DISABLE_DOH) {
      Glean.networking.dohHeuristicsResult.set(
        lazy.Heuristics.Telemetry.fromResults(results)
      );
      await this.setState("disabled");
    } else {
      Glean.networking.dohHeuristicsResult.set(lazy.Heuristics.Telemetry.pass);
      Glean.networking.dohHeuristicsPassCount.add(1);
      await this.setState("enabled");
    }

    // For telemetry, we group the heuristics results into three categories.
    // Only heuristics with a DISABLE_DOH result are included.
    // Each category is finally included in the event as a comma-separated list.
    let canaries = [];
    let filtering = [];
    let enterprise = [];
    let platform = [];

    for (let [heuristicName, result] of Object.entries(results)) {
      if (result !== lazy.Heuristics.DISABLE_DOH) {
        continue;
      }

      if (["canary", "zscalerCanary"].includes(heuristicName)) {
        canaries.push(heuristicName);
      } else if (
        ["browserParent", "google", "youtube"].includes(heuristicName)
      ) {
        filtering.push(heuristicName);
      } else if (
        ["policy", "modifiedRoots", "thirdPartyRoots"].includes(heuristicName)
      ) {
        enterprise.push(heuristicName);
      } else if (["vpn", "proxy", "nrpt"].includes(heuristicName)) {
        platform.push(heuristicName);
      }

      if (lazy.Heuristics.Telemetry.heuristicNames().includes(heuristicName)) {
        Glean.networking.dohHeuristicEverTripped[heuristicName].set(true);
      }
    }

    resultsForTelemetry.canaries = canaries.join(",");
    resultsForTelemetry.filtering = filtering.join(",");
    resultsForTelemetry.enterprise = enterprise.join(",");
    resultsForTelemetry.platform = platform.join(",");
    resultsForTelemetry.value = decision;

    Glean.doh.evaluateV2Heuristics.record(resultsForTelemetry);
  },

  async setState(state) {
    switch (state) {
      case "disabled":
        lazy.Preferences.set(ROLLOUT_MODE_PREF, 0);
        break;
      case "enabled":
        lazy.Preferences.set(ROLLOUT_MODE_PREF, 2);
        lazy.Preferences.set(BREADCRUMB_PREF, true);
        break;
      case "policyDisabled":
      case "manuallyDisabled":
      case "UIDisabled":
        lazy.Preferences.reset(BREADCRUMB_PREF);
      // Fall through.
      case "rollback":
        this.setHeuristicResult(Ci.nsITRRSkipReason.TRR_UNSET);
        lazy.Preferences.reset(ROLLOUT_MODE_PREF);
        break;
      case "shutdown":
        this.setHeuristicResult(Ci.nsITRRSkipReason.TRR_UNSET);
        if (lazy.Preferences.get(CLEAR_ON_SHUTDOWN_PREF, true)) {
          lazy.Preferences.reset(ROLLOUT_MODE_PREF);
        }
        break;
    }

    Glean.doh["state" + state[0].toUpperCase() + state.slice(1)].record({
      value: "null",
    });

    let modePref = lazy.Preferences.get(NETWORK_TRR_MODE_PREF);
    if (state == "manuallyDisabled") {
      if (
        modePref == Ci.nsIDNSService.MODE_TRRFIRST ||
        modePref == Ci.nsIDNSService.MODE_TRRONLY
      ) {
        Glean.networking.dohHeuristicsResult.set(
          lazy.Heuristics.Telemetry.manuallyEnabled
        );
      } else if (
        lazy.Preferences.get("doh-rollout.doorhanger-decision", "") ==
        "UIDisabled"
      ) {
        Glean.networking.dohHeuristicsResult.set(
          lazy.Heuristics.Telemetry.optOut
        );
      } else {
        Glean.networking.dohHeuristicsResult.set(
          lazy.Heuristics.Telemetry.manuallyDisabled
        );
      }
    }
  },

  async disableHeuristics(state) {
    await this.setState(state);

    if (!this._heuristicsAreEnabled) {
      return;
    }

    Services.obs.removeObserver(this, kLinkStatusChangedTopic);
    Services.obs.removeObserver(this, kConnectivityTopic);
    if (this._debounceTimer) {
      lazy.clearTimeout(this._debounceTimer);
      delete this._debounceTimer;
    }
    if (this._throttleTimer) {
      lazy.clearTimeout(this._throttleTimer);
      delete this._throttleTimer;
    }
    this._heuristicsAreEnabled = false;
  },

  async rollback() {
    await this.disableHeuristics("rollback");
  },

  async runTRRSelection() {
    // If persisting the selection is disabled, clear the existing
    // selection.
    if (!lazy.DoHConfigController.currentConfig.trrSelection.commitResult) {
      lazy.Preferences.reset(ROLLOUT_URI_PREF);
    }

    if (!lazy.DoHConfigController.currentConfig.trrSelection.enabled) {
      return;
    }

    if (
      lazy.Preferences.isSet(ROLLOUT_URI_PREF) &&
      lazy.Preferences.get(ROLLOUT_URI_PREF) ==
        lazy.Preferences.get(TRR_SELECT_DRY_RUN_RESULT_PREF)
    ) {
      return;
    }

    await this.runTRRSelectionDryRun();

    // If persisting the selection is disabled, don't commit the value.
    if (!lazy.DoHConfigController.currentConfig.trrSelection.commitResult) {
      return;
    }

    lazy.Preferences.set(
      ROLLOUT_URI_PREF,
      lazy.Preferences.get(TRR_SELECT_DRY_RUN_RESULT_PREF)
    );
  },

  async runTRRSelectionDryRun() {
    if (lazy.Preferences.isSet(TRR_SELECT_DRY_RUN_RESULT_PREF)) {
      // Check whether the existing dry-run-result is in the default
      // list of TRRs. If it is, all good. Else, run the dry run again.
      let dryRunResult = lazy.Preferences.get(TRR_SELECT_DRY_RUN_RESULT_PREF);
      let dryRunResultIsValid =
        lazy.DoHConfigController.currentConfig.providerList.some(
          trr => trr.uri == dryRunResult
        );
      if (dryRunResultIsValid) {
        return;
      }
    }

    let setDryRunResultAndRecordTelemetry = trrUri => {
      lazy.Preferences.set(TRR_SELECT_DRY_RUN_RESULT_PREF, trrUri);
      Glean.securityDohTrrPerformance.trrselectDryrunresult.record({
        value: trrUri.substring(0, 40), // Telemetry payload max length
      });
    };

    if (lazy.kIsInAutomation) {
      // For mochitests, just record telemetry with a dummy result.
      // TRRPerformance.sys.mjs is tested in xpcshell.
      setDryRunResultAndRecordTelemetry("https://example.com/dns-query");
      return;
    }

    // Importing the module here saves us from having to do it at startup, and
    // ensures tests have time to set prefs before the module initializes.
    let { TRRRacer } = ChromeUtils.importESModule(
      "resource://gre/modules/TRRPerformance.sys.mjs"
    );
    await new Promise(resolve => {
      let trrList =
        lazy.DoHConfigController.currentConfig.trrSelection.providerList.map(
          trr => trr.uri
        );
      let racer = new TRRRacer(() => {
        setDryRunResultAndRecordTelemetry(racer.getFastestTRR(true));
        resolve();
      }, trrList);
      racer.run();
    });
  },

  observe(subject, topic, data) {
    switch (topic) {
      case kLinkStatusChangedTopic:
        this.onConnectionChanged();
        break;
      case kConnectivityTopic:
        this.onConnectivityAvailable();
        break;
      case kPrefChangedTopic:
        this.onPrefChanged(data);
        break;
      case lazy.DoHConfigController.kConfigUpdateTopic:
        this.reset();
        break;
    }
  },

  setHeuristicResult(skipReason) {
    try {
      Services.dns.setHeuristicDetectionResult(skipReason);
    } catch (e) {}
  },

  async onPrefChanged(pref) {
    switch (pref) {
      case NETWORK_TRR_URI_PREF:
      case NETWORK_TRR_MODE_PREF:
        lazy.Preferences.set(DISABLED_PREF, true);
        await this.disableHeuristics("manuallyDisabled");
        break;
    }
  },

  // Connection change events are debounced to allow the network to settle.
  // We wait for the network to be up for a period of kDebounceTimeout before
  // handling the change. The timer is canceled when the network goes down and
  // restarted the first time we learn that it went back up.
  _debounceTimer: null,
  _cancelDebounce() {
    if (!this._debounceTimer) {
      return;
    }

    lazy.clearTimeout(this._debounceTimer);
    this._debounceTimer = null;
  },

  _lastDebounceTimestamp: 0,
  onConnectionChanged() {
    if (!lazy.gNetworkLinkService.isLinkUp) {
      // Network is down - reset debounce timer.
      this._cancelDebounce();
      return;
    }

    if (this._debounceTimer) {
      // Already debouncing - nothing to do.
      return;
    }

    if (lazy.kNetworkDebounceTimeout < 0) {
      // Skip debouncing in tests that set timeout to a negative value.
      this.onConnectionChangedDebounced();
      return;
    }

    this._lastDebounceTimestamp = Date.now();
    this._debounceTimer = lazy.setTimeout(() => {
      this._cancelDebounce();
      this.onConnectionChangedDebounced();
    }, lazy.kNetworkDebounceTimeout);
  },

  onConnectionChangedDebounced() {
    if (!lazy.gNetworkLinkService.isLinkUp) {
      return;
    }

    if (
      lazy.gCaptivePortalService.state ==
      lazy.gCaptivePortalService.LOCKED_PORTAL
    ) {
      return;
    }

    // The network is up and we don't know that we're in a locked portal.
    // Run heuristics. If we detect a portal later, we'll run heuristics again
    // when it's unlocked. In that case, this run will likely have failed.
    this.runHeuristicsThrottled("netchange");
  },

  onConnectivityAvailable() {
    if (this._debounceTimer) {
      // Already debouncing - nothing to do.
      return;
    }

    this.runHeuristicsThrottled("connectivity");
  },
};
