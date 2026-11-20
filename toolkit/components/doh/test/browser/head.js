"use strict";

ChromeUtils.defineESModuleGetters(this, {
  DoHConfigController: "moz-src:///toolkit/components/doh/DoHConfig.sys.mjs",
  DoHController: "moz-src:///toolkit/components/doh/DoHController.sys.mjs",
  DoHTestUtils: "resource://testing-common/DoHTestUtils.sys.mjs",
  Heuristics: "moz-src:///toolkit/components/doh/DoHHeuristics.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  RegionTestUtils: "resource://testing-common/RegionTestUtils.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
});

// Map DoH state names (as used by DoHController.setState) to their Glean
// metric properties on `Glean.doh`.
const DOH_STATE_TO_GLEAN_METRIC = {
  enabled: "stateEnabled",
  disabled: "stateDisabled",
  manuallyDisabled: "stateManuallyDisabled",
  policyDisabled: "statePolicyDisabled",
  uninstalled: "stateUninstalled",
  UIOk: "stateUiok",
  UIDisabled: "stateUidisabled",
  rollback: "stateRollback",
  shutdown: "stateShutdown",
};
const ALL_DOH_STATES = Object.keys(DOH_STATE_TO_GLEAN_METRIC);

function gleanStateMetricFor(state) {
  return Glean.doh[DOH_STATE_TO_GLEAN_METRIC[state]];
}

XPCOMUtils.defineLazyServiceGetter(
  this,
  "gDNSOverride",
  "@mozilla.org/network/native-dns-override;1",
  Ci.nsINativeDNSResolverOverride
);

const { CommonUtils } = ChromeUtils.importESModule(
  "resource://services-common/utils.sys.mjs"
);

const EXAMPLE_URL = "https://example.com/";

const prefs = {
  TESTING_PREF: "doh-rollout._testing",
  ENABLED_PREF: "doh-rollout.enabled",
  ROLLOUT_TRR_MODE_PREF: "doh-rollout.mode",
  NETWORK_TRR_MODE_PREF: "network.trr.mode",
  CONFIRMATION_NS_PREF: "network.trr.confirmationNS",
  BREADCRUMB_PREF: "doh-rollout.self-enabled",
  DOORHANGER_USER_DECISION_PREF: "doh-rollout.doorhanger-decision",
  DISABLED_PREF: "doh-rollout.disable-heuristics",
  SKIP_HEURISTICS_PREF: "doh-rollout.skipHeuristicsCheck",
  CLEAR_ON_SHUTDOWN_PREF: "doh-rollout.clearModeOnShutdown",
  FIRST_RUN_PREF: "doh-rollout.doneFirstRun",
  PROVIDER_LIST_PREF: "doh-rollout.provider-list",
  TRR_SELECT_ENABLED_PREF: "doh-rollout.trr-selection.enabled",
  TRR_SELECT_URI_PREF: "doh-rollout.uri",
  TRR_SELECT_COMMIT_PREF: "doh-rollout.trr-selection.commit-result",
  TRR_SELECT_DRY_RUN_RESULT_PREF: "doh-rollout.trr-selection.dry-run-result",
  PROVIDER_STEERING_PREF: "doh-rollout.provider-steering.enabled",
  PROVIDER_STEERING_LIST_PREF: "doh-rollout.provider-steering.provider-list",
  NETWORK_DEBOUNCE_TIMEOUT_PREF: "doh-rollout.network-debounce-timeout",
  HEURISTICS_THROTTLE_TIMEOUT_PREF: "doh-rollout.heuristics-throttle-timeout",
  HEURISTICS_THROTTLE_RATE_LIMIT_PREF:
    "doh-rollout.heuristics-throttle-rate-limit",
};

async function setup() {
  try {
    await DoHController._uninit();
    await DoHConfigController._uninit();
  } catch (e) {}

  // The shipped doh-config dump enables the rollout in the US, which is the
  // region test profiles run in, so DoH may have already self-enabled and
  // written doh-rollout prefs before the first test in the file runs. Keep
  // FIRST_RUN_PREF, which DoHController.init() always sets and which gates
  // the rollback that setup() waits for below.
  for (let pref of Object.values(prefs)) {
    if (pref != prefs.FIRST_RUN_PREF) {
      Services.prefs.clearUserPref(pref);
    }
  }

  SpecialPowers.pushPrefEnv({
    set: [["security.notification_enable_delay", 0]],
  });
  Services.fog.testResetFOG();
  _resetConsumed();

  // Tell DoHController that this isn't real life.
  Services.prefs.setBoolPref(prefs.TESTING_PREF, true);

  // Avoid non-local connections to the TRR endpoint.
  Services.prefs.setStringPref(prefs.CONFIRMATION_NS_PREF, "skip");

  // Enable trr selection and provider steeringfor tests. This is off
  // by default so it can be controlled via Nimbus.
  Services.prefs.setBoolPref(prefs.TRR_SELECT_ENABLED_PREF, true);
  Services.prefs.setBoolPref(prefs.PROVIDER_STEERING_PREF, true);

  // Enable committing the TRR selection. This pref ships false by default so
  // it can be controlled e.g. via Nimbus, but for testing let's set enable.
  Services.prefs.setBoolPref(prefs.TRR_SELECT_COMMIT_PREF, true);

  // Clear mode on shutdown by default.
  Services.prefs.setBoolPref(prefs.CLEAR_ON_SHUTDOWN_PREF, true);

  // Generally don't bother with debouncing or throttling.
  // The throttling test will set this explicitly.
  Services.prefs.setIntPref(prefs.NETWORK_DEBOUNCE_TIMEOUT_PREF, -1);
  Services.prefs.setIntPref(prefs.HEURISTICS_THROTTLE_TIMEOUT_PREF, -1);

  // Set up heuristics, all passing by default.

  // Google safesearch overrides
  gDNSOverride.addIPOverride("www.google.com.", "1.1.1.1");
  gDNSOverride.addIPOverride("google.com.", "1.1.1.1");
  gDNSOverride.addIPOverride("forcesafesearch.google.com.", "1.1.1.2");

  // YouTube safesearch overrides
  gDNSOverride.addIPOverride("www.youtube.com.", "2.1.1.1");
  gDNSOverride.addIPOverride("m.youtube.com.", "2.1.1.1");
  gDNSOverride.addIPOverride("youtubei.googleapis.com.", "2.1.1.1");
  gDNSOverride.addIPOverride("youtube.googleapis.com.", "2.1.1.1");
  gDNSOverride.addIPOverride("www.youtube-nocookie.com.", "2.1.1.1");
  gDNSOverride.addIPOverride("restrict.youtube.com.", "2.1.1.2");
  gDNSOverride.addIPOverride("restrictmoderate.youtube.com.", "2.1.1.2");

  // Zscaler override
  gDNSOverride.addIPOverride("sitereview.zscaler.com.", "3.1.1.1");

  // Helper domains when checking the canary
  gDNSOverride.addIPOverride("firefox.com.", "9.8.7.6");
  gDNSOverride.addIPOverride("mozilla.org.", "8.7.6.5");
  // Global canary
  gDNSOverride.addIPOverride("use-application-dns.net.", "4.1.1.1");

  await DoHTestUtils.resetRemoteSettingsConfig(false);

  Services.fog.testResetFOG();
  _resetConsumed();

  await DoHConfigController.init();
  await DoHController.init();

  await waitForStateTelemetry(["rollback"]);

  registerCleanupFunction(async () => {
    Services.fog.testResetFOG();
    _resetConsumed();
    gDNSOverride.clearOverrides();
    await DoHController._uninit();
    Services.fog.testResetFOG();
    _resetConsumed();
    for (let pref of Object.values(prefs)) {
      Services.prefs.clearUserPref(pref);
    }
    await DoHTestUtils.resetRemoteSettingsConfig(false);
    await DoHController.init();
  });
}

const kTestRegion = "DE";
const kRegionalPrefNamespace = `doh-rollout.${kTestRegion.toLowerCase()}`;

async function setupRegion() {
  Region._setHomeRegion(null, false);
  RegionTestUtils.setNetworkRegion(kTestRegion);
  await Region._fetchRegion();
  is(Region.home, kTestRegion, "Should have correct region");
  Services.prefs.clearUserPref("doh-rollout.home-region");
  await DoHConfigController.loadRegion();
}

// Glean events are not cleared per-metric, and `Services.fog.testResetFOG()`
// would also wipe the scalar/counter values that other helpers verify. To
// preserve the "check one event at a time" semantics of the legacy helpers,
// track how many events have already been verified and only inspect new ones.
let _consumedHeuristics = 0;
let _consumedTrrSelect = 0;
let _consumedStates = {};
function _resetConsumed() {
  _consumedHeuristics = 0;
  _consumedTrrSelect = 0;
  _consumedStates = {};
}

// Mark all currently-recorded state events as consumed. The original tests
// called Services.telemetry.clearEvents() in each "check..." helper, which
// dropped any incidental state events that fired between checks. We do the
// same here so that waitForStateTelemetry sees only the events that fired
// since the last "check..." helper completed.
async function _consumeAllStateEvents() {
  for (let state of ALL_DOH_STATES) {
    let events = (await gleanStateMetricFor(state).testGetValue()) ?? [];
    _consumedStates[state] = events.length;
  }
}

async function checkTRRSelectionTelemetry() {
  let events;
  await TestUtils.waitForCondition(async () => {
    events =
      (await Glean.securityDohTrrPerformance.trrselectDryrunresult.testGetValue()) ??
      [];
    return events.length > _consumedTrrSelect;
  });
  let newEvents = events.slice(_consumedTrrSelect);
  is(newEvents.length, 1, "Found the expected trrselect event.");
  is(
    newEvents[0].extra.value,
    "https://example.com/dns-query",
    "The event records the expected decision"
  );
  _consumedTrrSelect = events.length;
}

async function ensureNoTRRSelectionTelemetry() {
  let events =
    (await Glean.securityDohTrrPerformance.trrselectDryrunresult.testGetValue()) ??
    [];
  is(events.length - _consumedTrrSelect, 0, "Found no trrselect events.");
}

async function checkHeuristicsTelemetry(
  decision,
  evaluateReason,
  steeredProvider = ""
) {
  let events;
  await TestUtils.waitForCondition(async () => {
    events = (await Glean.doh.evaluateV2Heuristics.testGetValue()) ?? [];
    return events.length > _consumedHeuristics;
  });
  let newEvents = events.slice(_consumedHeuristics);
  is(newEvents.length, 1, "Found the expected heuristics event.");
  is(
    newEvents[0].extra.value,
    decision,
    "The event records the expected decision"
  );
  if (evaluateReason) {
    is(
      newEvents[0].extra.evaluateReason,
      evaluateReason,
      "Got the expected reason."
    );
  }
  is(
    newEvents[0].extra.steeredProvider ?? "",
    steeredProvider,
    "Got expected provider."
  );
  _consumedHeuristics = events.length;
  await _consumeAllStateEvents();
}

// Assert a list of Glean metric values. Each entry is `[metric, expected]`
// or `[metric, expected, message]`. Boolean expectations treat a null
// testGetValue (metric never set) as false.
async function assertGleanValues(expectations) {
  for (let [metric, expected, message] of expectations) {
    let actual = await metric.testGetValue();
    if (typeof expected === "boolean") {
      actual = actual ?? false;
    }
    is(actual, expected, message);
  }
}

// Returns Glean expectations asserting all `dohHeuristicEverTripped` keys
// are false, except those listed in `except`.
function allHeuristicsFalseExpectations(except = []) {
  return Heuristics.Telemetry.heuristicNames()
    .filter(name => !except.includes(name))
    .map(name => [
      Glean.networking.dohHeuristicEverTripped[name],
      false,
      `dohHeuristicEverTripped[${name}] should be false`,
    ]);
}

async function checkHeuristicsTelemetryMultiple(expectedEvaluateReasons) {
  let newEvents;
  await TestUtils.waitForCondition(async () => {
    let events = (await Glean.doh.evaluateV2Heuristics.testGetValue()) ?? [];
    newEvents = events.slice(_consumedHeuristics);
    if (newEvents.length == expectedEvaluateReasons.length) {
      _consumedHeuristics = events.length;
      return true;
    }
    return false;
  });
  is(
    newEvents.length,
    expectedEvaluateReasons.length,
    "Found the expected heuristics events."
  );
  for (let reason of expectedEvaluateReasons) {
    let event = newEvents.find(e => e.extra.evaluateReason == reason);
    is(event.extra.evaluateReason, reason, `${reason} event found`);
  }
  await _consumeAllStateEvents();
}

async function ensureNoHeuristicsTelemetry() {
  let events = (await Glean.doh.evaluateV2Heuristics.testGetValue()) ?? [];
  is(events.length - _consumedHeuristics, 0, "Found no heuristics events.");
}

async function waitForStateTelemetry(expectedStates) {
  let stateCounts = {};

  // Wait for at least the expected number of new state events to land.
  await TestUtils.waitForCondition(async () => {
    let newCount = 0;
    for (let state of ALL_DOH_STATES) {
      let events = (await gleanStateMetricFor(state).testGetValue()) ?? [];
      stateCounts[state] = events.length;
      newCount += events.length - (_consumedStates[state] ?? 0);
    }
    return newCount >= expectedStates.length;
  });

  let totalNew = 0;
  for (let state of ALL_DOH_STATES) {
    let consumed = _consumedStates[state] ?? 0;
    let newForState = stateCounts[state] - consumed;
    let expected = expectedStates.filter(s => s == state).length;
    is(
      newForState,
      expected,
      `${state}: expected ${expected} new state event(s)`
    );
    totalNew += newForState;
    _consumedStates[state] = stateCounts[state];
  }
  is(totalNew, expectedStates.length, "Found the expected state events.");
}

async function restartDoHController() {
  let oldMode = Services.prefs.prefHasUserValue(prefs.ROLLOUT_TRR_MODE_PREF)
    ? Services.prefs.getIntPref(prefs.ROLLOUT_TRR_MODE_PREF)
    : undefined;
  await DoHController._uninit();
  let newMode = Services.prefs.prefHasUserValue(prefs.ROLLOUT_TRR_MODE_PREF)
    ? Services.prefs.getIntPref(prefs.ROLLOUT_TRR_MODE_PREF)
    : undefined;
  let expectClear = Services.prefs.getBoolPref(prefs.CLEAR_ON_SHUTDOWN_PREF);
  is(
    newMode,
    expectClear ? undefined : oldMode,
    `Mode was ${expectClear ? "cleared" : "persisted"} on shutdown.`
  );
  await DoHController.init();
}

// setPassing/FailingHeuristics are used generically to test that DoH is enabled
// or disabled correctly. We use the zscaler canary arbitrarily here, individual
// heuristics are tested separately.
function setPassingHeuristics() {
  gDNSOverride.clearHostOverride("sitereview.zscaler.com.");
  gDNSOverride.addIPOverride("sitereview.zscaler.com.", "3.1.1.1");
}

function setFailingHeuristics() {
  gDNSOverride.clearHostOverride("sitereview.zscaler.com.");
  gDNSOverride.addIPOverride("sitereview.zscaler.com.", "213.152.228.242");
}

// Mirrors what the ACCEPT_DOH / DISABLE_DOH special message actions set.
function simulateUserDecision(accepted) {
  if (accepted) {
    Services.prefs.setStringPref(prefs.DOORHANGER_USER_DECISION_PREF, "UIOk");
    return;
  }
  Services.prefs.setStringPref(
    prefs.DOORHANGER_USER_DECISION_PREF,
    "UIDisabled"
  );
  Services.prefs.setIntPref(prefs.NETWORK_TRR_MODE_PREF, 5);
}

function simulateNetworkChange() {
  // The networkStatus API does not actually propagate the link status we supply
  // here, but rather sends the link status from the NetworkLinkService.
  // This means there's no point sending a down and then an up - the extension
  // will just receive "up" twice.
  // TODO: Implement a mock NetworkLinkService and use it to also simulate
  // network down events.
  Services.obs.notifyObservers(null, "network:link-status-changed", "up");
}

async function ensureTRRMode(mode) {
  await TestUtils.waitForCondition(() => {
    let val = Services.prefs.prefHasUserValue(prefs.ROLLOUT_TRR_MODE_PREF)
      ? Services.prefs.getIntPref(prefs.ROLLOUT_TRR_MODE_PREF)
      : undefined;
    return val === mode;
  });
  let actualMode = Services.prefs.prefHasUserValue(prefs.ROLLOUT_TRR_MODE_PREF)
    ? Services.prefs.getIntPref(prefs.ROLLOUT_TRR_MODE_PREF)
    : undefined;
  is(actualMode, mode, `TRR mode is ${mode}`);
}

async function ensureNoTRRModeChange(mode) {
  try {
    // Try and wait for the TRR pref to change... waitForCondition should throw
    // after trying for a while.
    await TestUtils.waitForCondition(() => {
      let val = Services.prefs.prefHasUserValue(prefs.ROLLOUT_TRR_MODE_PREF)
        ? Services.prefs.getIntPref(prefs.ROLLOUT_TRR_MODE_PREF)
        : undefined;
      return val !== mode;
    });
    // If we reach this, the waitForCondition didn't throw. Fail!
    ok(false, "TRR mode changed when it shouldn't have!");
  } catch (e) {
    // Assert for clarity.
    let actualMode = Services.prefs.prefHasUserValue(
      prefs.ROLLOUT_TRR_MODE_PREF
    )
      ? Services.prefs.getIntPref(prefs.ROLLOUT_TRR_MODE_PREF)
      : undefined;
    is(actualMode, mode, "No change in TRR mode");
  }
}
