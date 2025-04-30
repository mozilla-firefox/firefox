/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  Heuristics: "resource://gre/modules/DoHHeuristics.sys.mjs",
});

add_task(setup);

add_task(async function testPlatformIndications() {
  // Check if the platform heuristics actually cause a "disable_doh" event
  let { MockRegistrar } = ChromeUtils.importESModule(
    "resource://testing-common/MockRegistrar.sys.mjs"
  );

  let mockedLinkService = {
    isLinkUp: true,
    linkStatusKnown: true,
    linkType: Ci.nsINetworkLinkService.LINK_TYPE_WIFI,
    networkID: "abcd",
    dnsSuffixList: [],
    platformDNSIndications: Ci.nsINetworkLinkService.NONE_DETECTED,
    QueryInterface: ChromeUtils.generateQI(["nsINetworkLinkService"]),
  };

  let networkLinkServiceCID = MockRegistrar.register(
    "@mozilla.org/network/network-link-service;1",
    mockedLinkService
  );

  Heuristics._setMockLinkService(mockedLinkService);
  registerCleanupFunction(async () => {
    MockRegistrar.unregister(networkLinkServiceCID);
    Heuristics._setMockLinkService(undefined);
  });

  setPassingHeuristics();
  let prefPromise = TestUtils.waitForPrefChange(prefs.BREADCRUMB_PREF);
  Preferences.set(prefs.ENABLED_PREF, true);
  await prefPromise;
  is(Preferences.get(prefs.BREADCRUMB_PREF), true, "Breadcrumb saved.");
  await checkHeuristicsTelemetry("enable_doh", "startup");

  checkScalars([
    ["networking.doh_heuristics_attempts", { value: 1 }],
    ["networking.doh_heuristics_pass_count", { value: 1 }],
    ["networking.doh_heuristics_result", { value: Heuristics.Telemetry.pass }],
    // All of the heuristics must be false.
    falseExpectations([]),
  ]);

  await ensureTRRMode(2);

  mockedLinkService.platformDNSIndications =
    Ci.nsINetworkLinkService.VPN_DETECTED;
  simulateNetworkChange();
  await ensureTRRMode(0);
  await checkHeuristicsTelemetry("disable_doh", "netchange");
  checkScalars(
    [
      ["networking.doh_heuristics_attempts", { value: 2 }],
      ["networking.doh_heuristics_pass_count", { value: 1 }],
      ["networking.doh_heuristics_result", { value: Heuristics.Telemetry.vpn }],
      ["networking.doh_heuristic_ever_tripped", { value: true, key: "vpn" }],
    ].concat(falseExpectations(["vpn"]))
  );

  mockedLinkService.platformDNSIndications =
    Ci.nsINetworkLinkService.PROXY_DETECTED;
  simulateNetworkChange();
  await ensureNoTRRModeChange(0);
  await checkHeuristicsTelemetry("disable_doh", "netchange");
  checkScalars(
    [
      ["networking.doh_heuristics_attempts", { value: 3 }],
      ["networking.doh_heuristics_pass_count", { value: 1 }],
      [
        "networking.doh_heuristics_result",
        { value: Heuristics.Telemetry.proxy },
      ],
      ["networking.doh_heuristic_ever_tripped", { value: true, key: "vpn" }], // Was tripped earlier this session
      ["networking.doh_heuristic_ever_tripped", { value: true, key: "proxy" }],
    ].concat(falseExpectations(["vpn", "proxy"]))
  );

  mockedLinkService.platformDNSIndications =
    Ci.nsINetworkLinkService.NRPT_DETECTED;
  simulateNetworkChange();
  await ensureNoTRRModeChange(0);
  await checkHeuristicsTelemetry("disable_doh", "netchange");
  checkScalars(
    [
      ["networking.doh_heuristics_attempts", { value: 4 }],
      ["networking.doh_heuristics_pass_count", { value: 1 }],
      [
        "networking.doh_heuristics_result",
        { value: Heuristics.Telemetry.nrpt },
      ],
      ["networking.doh_heuristic_ever_tripped", { value: true, key: "vpn" }], // Was tripped earlier this session
      ["networking.doh_heuristic_ever_tripped", { value: true, key: "proxy" }], // Was tripped earlier this session
      ["networking.doh_heuristic_ever_tripped", { value: true, key: "nrpt" }],
    ].concat(falseExpectations(["vpn", "proxy", "nrpt"]))
  );

  mockedLinkService.platformDNSIndications =
    Ci.nsINetworkLinkService.NONE_DETECTED;
  simulateNetworkChange();
  await ensureTRRMode(2);
  await checkHeuristicsTelemetry("enable_doh", "netchange");
  checkScalars(
    [
      ["networking.doh_heuristics_attempts", { value: 5 }],
      ["networking.doh_heuristics_pass_count", { value: 2 }],
      [
        "networking.doh_heuristics_result",
        { value: Heuristics.Telemetry.pass },
      ],
      ["networking.doh_heuristic_ever_tripped", { value: true, key: "vpn" }], // Was tripped earlier this session
      ["networking.doh_heuristic_ever_tripped", { value: true, key: "proxy" }], // Was tripped earlier this session
      ["networking.doh_heuristic_ever_tripped", { value: true, key: "nrpt" }], // Was tripped earlier this session
    ].concat(falseExpectations(["vpn", "proxy", "nrpt"]))
  );
});
