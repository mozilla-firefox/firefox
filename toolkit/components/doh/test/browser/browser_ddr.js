/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// Tests DoHController's RFC 9462 DDR mode. Discovery and verification are
// covered by netwerk/test/unit/test_ddr_*.js, so they are stubbed here.

"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  DoHDiscovery: "moz-src:///toolkit/components/doh/DoHDiscovery.sys.mjs",
});

const DDR_ENABLED_PREF = "network.trr.ddr.enabled";
const DDR_USE_RESOLVER_PREF = "network.trr.ddr.use_resolver";
const DDR_URI = "https://ddr.example.com/dns-query";

let sandbox;
let discoverStub;
let verifyStub;
let networkIDStub;

function discovered(resolverAddrs = ["192.0.2.1"]) {
  return { uri: DDR_URI, resolverAddrs };
}

add_setup(async function () {
  await setup();

  sandbox = sinon.createSandbox();
  discoverStub = sandbox.stub(DoHDiscovery, "discover");
  verifyStub = sandbox.stub(DoHDiscovery, "verify");
  networkIDStub = sandbox
    .stub(DoHController, "_getNetworkID")
    .returns("network-a");

  registerCleanupFunction(() => {
    sandbox.restore();
    Services.prefs.clearUserPref(DDR_ENABLED_PREF);
    Services.prefs.clearUserPref(DDR_USE_RESOLVER_PREF);
  });
});

async function startDDR() {
  Services.prefs.setBoolPref(DDR_ENABLED_PREF, true);
  await DoHController._uninit();
  await DoHController.init();
}

add_task(async function test_ddr_enables_verified_resolver() {
  setPassingHeuristics();
  Services.prefs.setBoolPref(DDR_USE_RESOLVER_PREF, true);
  discoverStub.resolves(discovered());
  verifyStub.resolves(true);

  await startDDR();
  await ensureTRRMode(2);
  is(
    Services.dns.currentTrrURI,
    DDR_URI,
    "the designated resolver is used as the TRR URI"
  );
  Assert.ok(verifyStub.calledOnce, "the designated resolver was verified");
  Assert.deepEqual(
    verifyStub.firstCall.args[0],
    discovered(),
    "verify gets the discovery result"
  );
});

add_task(async function test_ddr_unverified_resolver_is_not_used() {
  setPassingHeuristics();
  Services.prefs.setBoolPref(DDR_USE_RESOLVER_PREF, true);
  discoverStub.resolves(discovered());
  verifyStub.resolves(false);

  await startDDR();
  await ensureTRRMode(0);
});

add_task(async function test_ddr_telemetry_only() {
  setPassingHeuristics();
  Services.prefs.setBoolPref(DDR_USE_RESOLVER_PREF, false);
  discoverStub.resolves(discovered());
  verifyStub.resetHistory();
  verifyStub.resolves(true);

  await startDDR();
  await ensureTRRMode(0);
  Assert.ok(verifyStub.calledOnce, "verification still runs");
});

add_task(async function test_ddr_disabled_rolls_back() {
  // A rollout mode left behind by a previous session must not survive when DDR
  // can't run.
  Services.prefs.setBoolPref(DDR_ENABLED_PREF, true);
  await DoHController._uninit();
  Services.prefs.setIntPref(prefs.ROLLOUT_TRR_MODE_PREF, 2);
  Services.prefs.setBoolPref(prefs.DISABLED_PREF, true);
  discoverStub.resetHistory();

  await DoHController.init();
  Assert.ok(
    !Services.prefs.prefHasUserValue(prefs.ROLLOUT_TRR_MODE_PREF),
    "the rollout mode was rolled back"
  );
  Assert.ok(discoverStub.notCalled, "DDR did not run");

  Services.prefs.clearUserPref(prefs.DISABLED_PREF);
});

add_task(async function test_ddr_failing_heuristics() {
  setFailingHeuristics();
  Services.prefs.setBoolPref(DDR_USE_RESOLVER_PREF, true);
  discoverStub.resetHistory();
  discoverStub.resolves(discovered());
  verifyStub.resolves(true);

  await startDDR();
  await ensureTRRMode(0);
  Assert.ok(discoverStub.notCalled, "no discovery when heuristics fail");
  setPassingHeuristics();
});

function rolloutMode() {
  return Services.prefs.getIntPref(prefs.ROLLOUT_TRR_MODE_PREF, 0);
}

function simulateNetworkIDChange(networkID) {
  networkIDStub.returns(networkID);
  Services.obs.notifyObservers(null, "network:networkid-changed");
}

async function startDDRWithResolver(networkID) {
  setPassingHeuristics();
  Services.prefs.setBoolPref(DDR_USE_RESOLVER_PREF, true);
  networkIDStub.returns(networkID);
  discoverStub.resolves(discovered());
  verifyStub.resolves(true);
  await startDDR();
  await ensureTRRMode(2);
}

add_task(async function test_ddr_unrelated_link_change_keeps_resolver() {
  await startDDRWithResolver("network-a");

  // A link change that doesn't come with a new network ID, e.g. an address
  // change, keeps the designated resolver.
  simulateNetworkChange();
  is(rolloutMode(), 2, "DoH stays enabled on an unrelated link change");
  await ensureNoTRRModeChange(2);
});

add_task(async function test_ddr_network_id_change_disables_resolver() {
  await startDDRWithResolver("network-a");

  // The designated resolver belongs to the previous network: it must stop
  // being used as soon as the network ID changes, before heuristics and
  // discovery run again for the new network.
  let discovery = Promise.withResolvers();
  discoverStub.returns(discovery.promise);
  simulateNetworkIDChange("network-b");
  is(rolloutMode(), 0, "DoH is disabled synchronously on a new network");

  // The new network also designates a resolver.
  discovery.resolve(discovered());
  await ensureTRRMode(2);
});

add_task(async function test_ddr_same_network_id_keeps_resolver() {
  await startDDRWithResolver("network-a");
  discoverStub.resetHistory();

  simulateNetworkIDChange("network-a");
  is(rolloutMode(), 2, "DoH stays enabled when the network ID is the same");
  await ensureNoTRRModeChange(2);
  Assert.ok(discoverStub.notCalled, "no new discovery for the same network");
});

add_task(async function test_ddr_unknown_network_id_disables_resolver() {
  await startDDRWithResolver("");

  // Without a network ID we can't tell whether a link change is a new
  // network, so assume it is.
  let discovery = Promise.withResolvers();
  discoverStub.returns(discovery.promise);
  simulateNetworkChange();
  is(rolloutMode(), 0, "DoH is disabled synchronously on a link change");

  discovery.resolve(discovered());
  await ensureTRRMode(2);
});

add_task(async function test_ddr_network_change_during_discovery() {
  setPassingHeuristics();
  Services.prefs.setBoolPref(DDR_USE_RESOLVER_PREF, true);
  networkIDStub.returns("network-a");
  let firstDiscovery = Promise.withResolvers();
  discoverStub.resetHistory();
  discoverStub.returns(firstDiscovery.promise);
  verifyStub.resolves(true);

  await startDDR();
  await TestUtils.waitForCondition(() => discoverStub.called);

  // Platforms report the link change before the new network ID, so the
  // heuristics run for the link change still sees the previous network ID.
  let secondDiscovery = Promise.withResolvers();
  discoverStub.returns(secondDiscovery.promise);
  simulateNetworkChange();
  await TestUtils.waitForCondition(() => discoverStub.callCount == 2);

  // Once the new network ID arrives, both earlier attempts are discarded and
  // a new one decides.
  let thirdDiscovery = Promise.withResolvers();
  discoverStub.returns(thirdDiscovery.promise);
  simulateNetworkIDChange("network-b");
  await TestUtils.waitForCondition(() => discoverStub.callCount == 3);

  verifyStub.resetHistory();
  let networkChanged0 =
    Glean.networking.dohDdrResult.network_changed.testGetValue() ?? 0;
  firstDiscovery.resolve(discovered());
  secondDiscovery.resolve(discovered());
  await ensureNoTRRModeChange(undefined);

  // The discarded attempts are recorded as such, without being verified.
  Assert.ok(verifyStub.notCalled, "stale resolvers are not verified");
  Assert.equal(
    (Glean.networking.dohDdrResult.network_changed.testGetValue() ?? 0) -
      networkChanged0,
    2,
    "both discarded attempts recorded network_changed"
  );

  thirdDiscovery.resolve(null);
  await ensureTRRMode(0);
});
