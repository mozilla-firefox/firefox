/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { IPProtectionPanel } = ChromeUtils.importESModule(
  "moz-src:///browser/components/ipprotection/IPProtectionPanel.sys.mjs"
);
const { IPProtectionServerlist } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPProtectionServerlist.sys.mjs"
);
const { ShellService } = ChromeUtils.importESModule(
  "moz-src:///browser/components/shell/ShellService.sys.mjs"
);

const EGRESS_LOCATION_PREF = "browser.ipProtection.egressLocation";

/**
 * A class that mocks the IP Protection panel.
 */
class FakeIPProtectionPanelElement {
  constructor() {
    this.state = {
      isProtectionEnabled: false,
    };
    this.isConnected = false;
    this.ownerDocument = {
      removeEventListener() {
        /* NOOP */
      },
    };
  }

  requestUpdate() {
    /* NOOP */
  }

  remove() {
    /* NOOP */
  }
}

/**
 * A class that mocks the IP Protection panel.
 */
class FakeIPProtectionPanelView {
  constructor() {
    this.state = "open";
    this.ownerDocument = {
      removeEventListener() {
        /* NOOP */
      },
    };
  }

  hidePopup() {
    /* NOOP */
  }
}

add_setup(async function () {
  // FxAccountsStorage.sys.mjs requires a profile directory.
  do_get_profile();
  await putServerInRemoteSettings();

  await IPProtectionService.init();

  registerCleanupFunction(async () => {
    IPProtectionService.uninit();
  });
});

/**
 * Tests that we can set a state and pass it to a fake element.
 */
add_task(async function test_setState() {
  let ipProtectionPanel = new IPProtectionPanel();
  let fakeElement = new FakeIPProtectionPanelElement();
  ipProtectionPanel.components.add(fakeElement);
  ipProtectionPanel.panel = new FakeIPProtectionPanelView();

  ipProtectionPanel.state = {};
  fakeElement.state = {};

  ipProtectionPanel.setState({
    foo: "bar",
  });

  Assert.deepEqual(
    ipProtectionPanel.state,
    { foo: "bar" },
    "The state should be set on the IPProtectionPanel instance"
  );

  Assert.deepEqual(
    fakeElement.state,
    {},
    "The state should not be set on the fake element, as it is not connected"
  );

  fakeElement.isConnected = true;

  ipProtectionPanel.setState({
    isFoo: true,
  });

  Assert.deepEqual(
    ipProtectionPanel.state,
    { foo: "bar", isFoo: true },
    "The state should be set on the IPProtectionPanel instance"
  );

  Assert.deepEqual(
    fakeElement.state,
    { foo: "bar", isFoo: true },
    "The state should be set on the fake element"
  );
});

/**
 * Tests that the whole state will be updated when calling updateState directly.
 */
add_task(async function test_updateState() {
  let ipProtectionPanel = new IPProtectionPanel();
  let fakeElement = new FakeIPProtectionPanelElement();
  ipProtectionPanel.components.add(fakeElement);
  ipProtectionPanel.panel = new FakeIPProtectionPanelView();

  ipProtectionPanel.state = {};
  fakeElement.state = {};

  ipProtectionPanel.setState({
    foo: "bar",
  });

  Assert.deepEqual(
    fakeElement.state,
    {},
    "The state should not be set on the fake element, as it is not connected"
  );

  fakeElement.isConnected = true;
  ipProtectionPanel.updateState();

  Assert.deepEqual(
    fakeElement.state,
    { foo: "bar" },
    "The state should be set on the fake element"
  );
});

/**
 * Tests that we can set a state on multiple fake elements.
 */
add_task(async function test_updateComponentState() {
  let ipProtectionPanel = new IPProtectionPanel();
  let fakeElementA = new FakeIPProtectionPanelElement();
  let fakeElementB = new FakeIPProtectionPanelElement();

  ipProtectionPanel.panel = new FakeIPProtectionPanelView();
  ipProtectionPanel.state = {
    foo: "bar",
  };
  fakeElementA.state = {};
  fakeElementA.isConnected = true;
  fakeElementB.state = {};
  fakeElementB.isConnected = true;

  ipProtectionPanel.updateComponentState(fakeElementA);

  Assert.ok(
    ipProtectionPanel.components.has(fakeElementA),
    "The fake element A should be in the components set"
  );

  Assert.deepEqual(
    fakeElementA.state,
    { foo: "bar" },
    "The state should be set on the fake element A"
  );

  Assert.deepEqual(
    fakeElementB.state,
    {},
    "The state should not be set on the fake element B"
  );

  ipProtectionPanel.updateComponentState(fakeElementB);

  Assert.ok(
    ipProtectionPanel.components.has(fakeElementB),
    "The fake element B should be in the components set"
  );

  Assert.deepEqual(
    fakeElementB.state,
    { foo: "bar" },
    "The state should be set on the fake element B"
  );

  // Updating the state now should update both elements.
  ipProtectionPanel.setState({
    isFoo: true,
  });

  Assert.deepEqual(
    fakeElementA.state,
    { foo: "bar", isFoo: true },
    "The state should be set on the fake element A"
  );

  Assert.deepEqual(
    fakeElementB.state,
    { foo: "bar", isFoo: true },
    "The state should be set on the fake element B"
  );
});

/**
 * Tests that IPProtectionService ready state event updates the state.
 */
add_task(async function test_IPProtectionPanel_signedIn() {
  IPPDummyAuthProvider.simulateSignIn(true);
  IPPDummyAuthProvider.setEntitlement(createTestEntitlement(), {
    silent: true,
  });
  IPPDummyAuthProvider.setGetEntitlementResponse({
    entitlement: createTestEntitlement(),
  });

  let ipProtectionPanel = new IPProtectionPanel();
  let fakeElement = new FakeIPProtectionPanelElement();
  ipProtectionPanel.components.add(fakeElement);
  ipProtectionPanel.panel = new FakeIPProtectionPanelView();
  fakeElement.isConnected = true;

  let signedInEventPromise = waitForEvent(
    IPProtectionService,
    "IPProtectionService:StateChanged",
    () => IPProtectionService.state === IPProtectionStates.READY
  );
  IPProtectionService.updateState();

  await signedInEventPromise;

  Assert.equal(
    ipProtectionPanel.state.unauthenticated,
    false,
    "unauthenticated should be false in the IPProtectionPanel state"
  );

  Assert.equal(
    fakeElement.state.unauthenticated,
    false,
    "unauthenticated should be false in the fake elements state"
  );
});

/**
 * Tests that IPProtectionService unauthenticated state event updates the state.
 */
add_task(async function test_IPProtectionPanel_signedOut() {
  IPPDummyAuthProvider.simulateSignIn(false);

  let ipProtectionPanel = new IPProtectionPanel();
  let fakeElement = new FakeIPProtectionPanelElement();
  ipProtectionPanel.components.add(fakeElement);
  ipProtectionPanel.panel = new FakeIPProtectionPanelView();
  fakeElement.isConnected = true;

  IPProtectionService.setState(IPProtectionStates.READY);
  let signedOutEventPromise = waitForEvent(
    IPProtectionService,
    "IPProtectionService:StateChanged",
    () => IPProtectionService.state === IPProtectionStates.UNAUTHENTICATED
  );
  IPProtectionService.updateState();

  await signedOutEventPromise;

  Assert.equal(
    ipProtectionPanel.state.unauthenticated,
    true,
    "unauthenticated should be true in the IPProtectionPanel state"
  );

  Assert.equal(
    fakeElement.state.unauthenticated,
    true,
    "unauthenticated should be true in the fake elements state"
  );
});

/**
 * Tests that start and stopping the IPProtectionService updates the state.
 */
add_task(async function test_IPProtectionPanel_started_stopped() {
  let ipProtectionPanel = new IPProtectionPanel();
  let fakeElement = new FakeIPProtectionPanelElement();
  ipProtectionPanel.components.add(fakeElement);
  ipProtectionPanel.panel = new FakeIPProtectionPanelView();
  fakeElement.isConnected = true;

  IPPDummyAuthProvider.simulateSignIn(true);
  IPPDummyAuthProvider.setEntitlement(createTestEntitlement(), {
    silent: true,
  });
  IPPDummyAuthProvider.setGetEntitlementResponse({
    entitlement: createTestEntitlement(),
  });
  IPPDummyAuthProvider.setProxyPass({
    status: 200,
    error: undefined,
    pass: new ProxyPass(createProxyPassToken()),
    usage: new ProxyUsage(
      "5368709120",
      "4294967296",
      "2026-02-01T00:00:00.000Z"
    ),
  });

  IPProtectionService.updateState();

  Assert.equal(
    IPProtectionService.state,
    IPProtectionStates.READY,
    "IP Protection service should be in READY state before starting"
  );

  let startedEventPromise = waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:StateChanged",
    () => IPPProxyManager.state === IPPProxyStates.ACTIVE
  );

  IPPProxyManager.start();

  await startedEventPromise;

  Assert.equal(
    ipProtectionPanel.state.isProtectionEnabled,
    true,
    "isProtectionEnabled should be true in the IPProtectionPanel state"
  );

  Assert.equal(
    fakeElement.state.isProtectionEnabled,
    true,
    "isProtectionEnabled should be true in the fake elements state"
  );

  let stoppedEventPromise = waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:StateChanged",
    () => IPPProxyManager.state !== IPPProxyStates.ACTIVE
  );

  await IPPProxyManager.stop();

  await stoppedEventPromise;

  Assert.equal(
    ipProtectionPanel.state.isProtectionEnabled,
    false,
    "isProtectionEnabled should be false in the IPProtectionPanel state"
  );

  Assert.equal(
    fakeElement.state.isProtectionEnabled,
    false,
    "isProtectionEnabled should be false in the fake elements state"
  );
});

/**
 * Tests that locationsList is populated from IPProtectionServerlist and
 * kept in sync with IPProtectionServerlist:ListChanged events.
 */
add_task(async function test_IPProtectionPanel_locationsList() {
  await IPProtectionServerlist.maybeFetchList(true);

  let ipProtectionPanel = new IPProtectionPanel();
  let fakeElement = new FakeIPProtectionPanelElement();
  ipProtectionPanel.components.add(fakeElement);
  ipProtectionPanel.panel = new FakeIPProtectionPanelView();
  fakeElement.isConnected = true;

  Assert.deepEqual(
    ipProtectionPanel.state.locationsList,
    IPProtectionServerlist.countries,
    "locationsList should be set to IPProtectionServerlist.countries at construction"
  );
  Assert.ok(
    ipProtectionPanel.state.locationsList.some(c => c.code === "US"),
    "locationsList should include the seeded US country"
  );

  IPProtectionServerlist.dispatchEvent(
    new Event("IPProtectionServerlist:ListChanged")
  );

  Assert.deepEqual(
    ipProtectionPanel.state.locationsList,
    IPProtectionServerlist.countries,
    "locationsList should be refreshed when ListChanged fires"
  );
  Assert.deepEqual(
    fakeElement.state.locationsList,
    IPProtectionServerlist.countries,
    "locationsList should propagate to the connected element"
  );

  ipProtectionPanel.uninit();
});

/**
 * Tests that UsageChanged events with BigInt(0) remaining bandwidth
 * are processed correctly (not treated as falsy and skipped).
 *
 * Regression test: BigInt(0) is falsy in JavaScript, so a guard like
 * `!usage.remaining` would incorrectly bail out when remaining is exactly 0.
 */
add_task(async function test_IPProtectionPanel_usage_zero_remaining() {
  setupStubs();

  Services.prefs.setBoolPref("browser.ipProtection.bandwidth.enabled", true);

  let ipProtectionPanel = new IPProtectionPanel();
  let fakeElement = new FakeIPProtectionPanelElement();
  ipProtectionPanel.components.add(fakeElement);
  ipProtectionPanel.panel = new FakeIPProtectionPanelView();
  fakeElement.isConnected = true;

  Services.prefs.clearUserPref("browser.ipProtection.bandwidthThreshold");

  // Create a usage object with remaining = 0 (BigInt)
  const usage = new ProxyUsage("5368709120", "0", "3026-02-01T00:00:00.000Z");
  Assert.equal(usage.remaining, BigInt(0), "remaining should be BigInt(0)");

  // Dispatch a UsageChanged event with zero remaining bandwidth
  IPPProxyManager.dispatchEvent(
    new CustomEvent("IPPProxyManager:UsageChanged", {
      bubbles: true,
      composed: true,
      detail: { usage },
    })
  );

  // With 0 bytes remaining out of 5GB, remainingPercent = 0.
  // This is <= THIRD_THRESHOLD (0.1), so threshold should be set to 90.
  const threshold = Services.prefs.getIntPref(
    "browser.ipProtection.bandwidthThreshold",
    0
  );
  Assert.equal(
    threshold,
    100,
    "bandwidthThreshold pref should be 100 when remaining bandwidth is zero"
  );

  ipProtectionPanel.uninit();
  Services.prefs.clearUserPref("browser.ipProtection.bandwidthThreshold");
  Services.prefs.clearUserPref("browser.ipProtection.bandwidth.enabled");
});

/**
 * Tests that opening the panel while paused re-checks usage.
 */
add_task(async function test_showing_refreshes_usage_when_paused() {
  let ipProtectionPanel = new IPProtectionPanel();
  ipProtectionPanel.panel = new FakeIPProtectionPanelView();

  let refreshUsageStub = sinon.stub(IPPProxyManager, "refreshUsage").resolves();

  ipProtectionPanel.state.paused = false;
  await ipProtectionPanel.showing(ipProtectionPanel.panel);

  Assert.ok(
    refreshUsageStub.notCalled,
    "refreshUsage should not be called when opening the panel while not paused"
  );

  ipProtectionPanel.state.paused = true;
  await ipProtectionPanel.showing(ipProtectionPanel.panel);
  Assert.ok(
    refreshUsageStub.calledOnce,
    "refreshUsage should be called when opening the panel while paused"
  );

  refreshUsageStub.restore();
  ipProtectionPanel.uninit();
  Services.prefs.clearUserPref("browser.ipProtection.everOpenedPanel");
});

/**
 * Tests that opening the panel calls isPremium on panel open.
 */
add_task(async function test_showing_checks_is_premium_once() {
  let ipProtectionPanel = new IPProtectionPanel();
  ipProtectionPanel.panel = new FakeIPProtectionPanelView();

  // Check that the state is set on panel open
  let premiumValue = true;
  let isPremiumSpy = sinon.spy(async () => premiumValue);
  let isPremiumStub = sinon
    .stub(IPProtectionPanel.prototype, "isPremium")
    .callsFake(isPremiumSpy);

  await ipProtectionPanel.showing(ipProtectionPanel.panel);

  Assert.equal(
    isPremiumSpy.callCount,
    1,
    "isPremium should be called when the panel opens"
  );
  Assert.equal(
    ipProtectionPanel.state.isPremium,
    true,
    "isPremium state should be set from the method's result"
  );

  // Checks that the state is re-set on panel open if it was previously true
  premiumValue = false;
  await ipProtectionPanel.showing(ipProtectionPanel.panel);

  Assert.equal(
    isPremiumSpy.callCount,
    2,
    "isPremium should be called once more on the second panel open"
  );
  Assert.equal(
    ipProtectionPanel.state.isPremium,
    false,
    "isPremium state should follow the method's result when it changes between opens"
  );

  isPremiumStub.restore();
  ipProtectionPanel.uninit();
  Services.prefs.clearUserPref("browser.ipProtection.everOpenedPanel");
});

const LOCATIONS = {
  REC: { country: { code: "REC", locked: false } },
  CA: { country: { code: "CA", locked: true } },
};

/**
 * Opens the panel with an active connection on `current`, the locked CA as
 * the picked location, and premium coming from the default browser check.
 *
 * @param {object} options
 * @param {string} options.current
 *   Key in LOCATIONS of the location the connection routes through.
 * @param {boolean} options.isDefaultBrowser
 *   Whether Firefox is the default browser.
 * @returns {Promise<sinon.SinonStub>} The IPPProxyManager.switch stub.
 */
async function showPanelWithLocation({ current, isDefaultBrowser }) {
  let ipProtectionPanel = new IPProtectionPanel();
  ipProtectionPanel.panel = new FakeIPProtectionPanelView();
  let sandbox = sinon.createSandbox();
  Services.prefs.setStringPref(EGRESS_LOCATION_PREF, "CA");
  sandbox
    .stub(IPProtectionService, "authProvider")
    .get(() => ({ hasUpgraded: false }));
  sandbox
    .stub(ShellService, "isDefaultBrowserAsync")
    .resolves(isDefaultBrowser);
  sandbox
    .stub(IPProtectionServerlist, "getLocation")
    .callsFake(code => LOCATIONS[code] ?? null);
  sandbox
    .stub(IPProtectionServerlist, "getRecommendedLocation")
    .returns(LOCATIONS.REC);
  sandbox.stub(IPPProxyManager, "location").get(() => LOCATIONS[current]);
  let switchStub = sandbox.stub(IPPProxyManager, "switch");

  await ipProtectionPanel.showing(ipProtectionPanel.panel);

  sandbox.restore();
  ipProtectionPanel.uninit();
  Services.prefs.clearUserPref(EGRESS_LOCATION_PREF);
  Services.prefs.clearUserPref("browser.ipProtection.everOpenedPanel");
  return switchStub;
}

/**
 * Tests that opening the panel after losing default browser status moves a
 * connection off a locked location.
 */
add_task(async function test_showing_leaves_locked_location() {
  let switchStub = await showPanelWithLocation({
    current: "CA",
    isDefaultBrowser: false,
  });

  Assert.ok(switchStub.calledOnce, "The connection should be switched");
  Assert.strictEqual(
    switchStub.firstCall.args[0],
    undefined,
    "The connection should be switched to the recommended location"
  );
});

/**
 * Tests that opening the panel after becoming the default browser again moves
 * the connection back to the picked locked location.
 */
add_task(async function test_showing_returns_to_locked_location() {
  let switchStub = await showPanelWithLocation({
    current: "REC",
    isDefaultBrowser: true,
  });

  Assert.ok(
    switchStub.calledOnceWithExactly("CA"),
    "The connection should be switched back to the picked location"
  );
});

/**
 * Tests that opening the panel keeps a connection already on the location the
 * panel shows.
 */
add_task(async function test_showing_keeps_matching_location() {
  let switchStub = await showPanelWithLocation({
    current: "CA",
    isDefaultBrowser: true,
  });

  Assert.ok(switchStub.notCalled, "The connection should not be switched");
});

/**
 * Tests that opening the panel doesn't update premium status if hidden while
 * the premium check is pending.
 */
add_task(async function test_showing_aborts_when_hidden() {
  let ipProtectionPanel = new IPProtectionPanel();
  ipProtectionPanel.panel = new FakeIPProtectionPanelView();

  let premiumCheck = Promise.withResolvers();
  let isPremiumStub = sinon
    .stub(ipProtectionPanel, "isPremium")
    .returns(premiumCheck.promise);

  let showingPromise = ipProtectionPanel.showing(ipProtectionPanel.panel);
  ipProtectionPanel.hiding();
  premiumCheck.resolve(true);
  await showingPromise;

  Assert.equal(
    ipProtectionPanel.state.isPremium,
    false,
    "isPremium state should not be updated when opening the panel is cancelled"
  );

  isPremiumStub.restore();
  ipProtectionPanel.uninit();
  Services.prefs.clearUserPref("browser.ipProtection.everOpenedPanel");
});

function dispatchUsageEvent(max, remaining) {
  IPPProxyManager.dispatchEvent(
    new CustomEvent("IPPProxyManager:UsageChanged", {
      bubbles: true,
      composed: true,
      detail: {
        usage: new ProxyUsage(
          String(max),
          String(remaining),
          "3026-03-01T00:00:00.000Z"
        ),
      },
    })
  );
}

/**
 * Tests that bandwidth threshold telemetry events fire at 50%, 75%, and 90%.
 */
add_task(async function test_bandwidth_used_threshold_events() {
  Services.fog.initializeFOG();
  Services.fog.testResetFOG();

  Services.prefs.setBoolPref("browser.ipProtection.bandwidth.enabled", true);

  let ipProtectionPanel = new IPProtectionPanel();

  // 40% used (60% remaining) - no thresholds crossed
  dispatchUsageEvent(1000000, 600000);
  Assert.equal(
    Glean.ipprotection.bandwidthUsedThreshold.testGetValue(),
    null,
    "No threshold event should fire at 40% used"
  );

  // 55% used (45% remaining) - crosses 50%
  dispatchUsageEvent(1000000, 450000);
  let events = Glean.ipprotection.bandwidthUsedThreshold.testGetValue();
  Assert.equal(events.length, 1, "One threshold event should fire at 55% used");
  Assert.equal(events[0].extra.percentage, "50", "Should report 50% threshold");

  // 80% used (20% remaining) - crosses 75%
  dispatchUsageEvent(1000000, 200000);
  events = Glean.ipprotection.bandwidthUsedThreshold.testGetValue();
  Assert.equal(events.length, 2, "Two threshold events total at 80% used");
  Assert.equal(events[1].extra.percentage, "75", "Should report 75% threshold");

  // 95% used (5% remaining) - crosses 90%
  dispatchUsageEvent(1000000, 50000);
  events = Glean.ipprotection.bandwidthUsedThreshold.testGetValue();
  Assert.equal(events.length, 3, "Three threshold events total at 95% used");
  Assert.equal(events[2].extra.percentage, "90", "Should report 90% threshold");

  ipProtectionPanel.uninit();
  Services.prefs.clearUserPref("browser.ipProtection.bandwidthThreshold");
  Services.prefs.clearUserPref("browser.ipProtection.bandwidth.enabled");
  Services.fog.testResetFOG();
});

/**
 * Tests that threshold events are not re-fired within the same usage period.
 */
add_task(async function test_bandwidth_thresholds_not_repeated_same_period() {
  Services.fog.testResetFOG();

  Services.prefs.setBoolPref("browser.ipProtection.bandwidth.enabled", true);

  let ipProtectionPanel = new IPProtectionPanel();

  // Cross 50% threshold
  dispatchUsageEvent(1000000, 400000);
  let events = Glean.ipprotection.bandwidthUsedThreshold.testGetValue();
  Assert.equal(events.length, 1, "One event after first call at 60% used");

  // Same usage dispatched again - should not re-fire
  dispatchUsageEvent(1000000, 400000);
  events = Glean.ipprotection.bandwidthUsedThreshold.testGetValue();
  Assert.equal(
    events.length,
    1,
    "No additional event when threshold already reported"
  );

  ipProtectionPanel.uninit();
  Services.prefs.clearUserPref("browser.ipProtection.bandwidthThreshold");
  Services.prefs.clearUserPref("browser.ipProtection.bandwidth.enabled");
  Services.fog.testResetFOG();
});

/**
 * Tests that thresholds reset when a new usage period begins.
 */
add_task(async function test_bandwidth_thresholds_reset_on_new_period() {
  Services.fog.testResetFOG();

  Services.prefs.setBoolPref("browser.ipProtection.bandwidth.enabled", true);

  let ipProtectionPanel = new IPProtectionPanel();

  // Cross 50% in the current period
  dispatchUsageEvent(1000000, 400000);
  let events = Glean.ipprotection.bandwidthUsedThreshold.testGetValue();
  Assert.equal(events.length, 1, "One event in current period");
  Assert.equal(events[0].extra.percentage, "50");

  // Simulate a period reset by returning to full bandwidth (threshold drops to 0)
  dispatchUsageEvent(1000000, 1000000);
  Services.fog.testResetFOG();

  // 50% should fire again since the threshold pref was reset to 0
  dispatchUsageEvent(1000000, 400000);
  events = Glean.ipprotection.bandwidthUsedThreshold.testGetValue();
  Assert.equal(events.length, 1, "50% fires again after period reset");
  Assert.equal(events[0].extra.percentage, "50");

  ipProtectionPanel.uninit();
  Services.prefs.clearUserPref("browser.ipProtection.bandwidthThreshold");
  Services.prefs.clearUserPref("browser.ipProtection.bandwidth.enabled");
  Services.fog.testResetFOG();
});

/**
 * Tests that an unlimited UsageChanged event clears the bandwidth tracking
 * prefs and resets the bandwidthUsage state.
 */
add_task(async function test_bandwidth_unlimited_usage_clears_tracking() {
  Services.fog.initializeFOG();
  Services.fog.testResetFOG();

  Services.prefs.setBoolPref("browser.ipProtection.bandwidth.enabled", true);
  Services.prefs.setIntPref("browser.ipProtection.bandwidthThreshold", 75);
  Services.prefs.setStringPref(
    "browser.ipProtection.bandwidthResetDate",
    "3026-03-01T00:00:00.000Z"
  );

  let ipProtectionPanel = new IPProtectionPanel();
  ipProtectionPanel.setState({
    bandwidthUsage: { max: 1000000, remaining: 200000, reset: null },
  });

  IPPProxyManager.dispatchEvent(
    new CustomEvent("IPPProxyManager:UsageChanged", {
      bubbles: true,
      composed: true,
      detail: { usage: new ProxyUsage(null, null, null, true) },
    })
  );

  Assert.strictEqual(
    ipProtectionPanel.state.bandwidthUsage,
    null,
    "bandwidthUsage state should be reset for unlimited usage"
  );
  Assert.ok(
    !Services.prefs.prefHasUserValue("browser.ipProtection.bandwidthThreshold"),
    "bandwidthThreshold pref should be cleared for unlimited usage"
  );
  Assert.ok(
    !Services.prefs.prefHasUserValue("browser.ipProtection.bandwidthResetDate"),
    "bandwidthResetDate pref should be cleared for unlimited usage"
  );
  Assert.equal(
    Glean.ipprotection.bandwidthUsedThreshold.testGetValue(),
    null,
    "No threshold telemetry should be recorded for unlimited usage"
  );

  ipProtectionPanel.uninit();
  Services.prefs.clearUserPref("browser.ipProtection.bandwidth.enabled");
  Services.prefs.clearUserPref("browser.ipProtection.bandwidthThreshold");
  Services.prefs.clearUserPref("browser.ipProtection.bandwidthResetDate");
  Services.fog.testResetFOG();
});
