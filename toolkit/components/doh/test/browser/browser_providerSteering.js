/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

const TEST_DOMAIN = "doh.test.";
const AUTO_TRR_URI = "https://example.com/dns-query";

add_task(setup);

add_task(async function testProviderSteering() {
  setPassingHeuristics();
  let prefPromise = TestUtils.waitForPrefChange(prefs.BREADCRUMB_PREF);
  Preferences.set(prefs.ENABLED_PREF, true);
  await prefPromise;
  is(Preferences.get(prefs.BREADCRUMB_PREF), true, "Breadcrumb saved.");
  await checkHeuristicsTelemetry("enable_doh", "startup");

  let providerTestcases = [
    {
      id: "provider1",
      canonicalName: "foo.provider1.com",
      uri: "https://foo.provider1.com/query",
    },
    {
      id: "provider2",
      canonicalName: "bar.provider2.com",
      uri: "https://bar.provider2.com/query",
    },
  ];
  let configFlushPromise = DoHTestUtils.waitForConfigFlush();
  Preferences.set(
    prefs.PROVIDER_STEERING_LIST_PREF,
    JSON.stringify(providerTestcases)
  );
  await configFlushPromise;
  await checkHeuristicsTelemetry("enable_doh", "startup");

  let testNetChangeResult = async (
    expectedURI,
    heuristicsDecision,
    providerName
  ) => {
    let trrURIChanged = TestUtils.topicObserved(
      "network:trr-uri-changed",
      () => {
        // We need this check because this topic is observed once immediately
        // after the network change when the URI is reset, and then when the
        // provider steering heuristic runs and sets it to our uri.
        return Services.dns.currentTrrURI == expectedURI;
      }
    );
    simulateNetworkChange();
    await trrURIChanged;
    is(
      Services.dns.currentTrrURI,
      expectedURI,
      `TRR URI set to ${expectedURI}`
    );
    await checkHeuristicsTelemetry(
      heuristicsDecision,
      "netchange",
      providerName
    );
  };

  for (let { id, canonicalName, uri } of providerTestcases) {
    gDNSOverride.addIPOverride(TEST_DOMAIN, "9.9.9.9");
    gDNSOverride.setCnameOverride(TEST_DOMAIN, canonicalName);
    await testNetChangeResult(uri, "enable_doh", id);
    gDNSOverride.clearHostOverride(TEST_DOMAIN);
  }

  await testNetChangeResult(AUTO_TRR_URI, "enable_doh");

  // Just use the first provider for the remaining checks.
  let provider = providerTestcases[0];
  gDNSOverride.addIPOverride(TEST_DOMAIN, "9.9.9.9");
  gDNSOverride.setCnameOverride(TEST_DOMAIN, provider.canonicalName);
  await testNetChangeResult(provider.uri, "enable_doh", provider.id);

  // Trigger safesearch heuristics and ensure provider steering is disabled.
  let googleDomain = "google.com.";
  let googleIP = "1.1.1.1";
  let googleSafeSearchIP = "1.1.1.2";
  gDNSOverride.clearHostOverride(googleDomain);
  gDNSOverride.addIPOverride(googleDomain, googleSafeSearchIP);
  await testNetChangeResult(AUTO_TRR_URI, "disable_doh");
  gDNSOverride.clearHostOverride(googleDomain);
  gDNSOverride.addIPOverride(googleDomain, googleIP);
  checkScalars(
    [
      [
        "networking.doh_heuristics_result",
        { value: Heuristics.Telemetry.google },
      ],
      ["networking.doh_heuristic_ever_tripped", { value: true, key: "google" }],
      // All of the other heuristics must be false.
    ].concat(falseExpectations(["google"]))
  );

  // Check that provider steering is enabled again after we reset above.
  await testNetChangeResult(provider.uri, "enable_doh", provider.id);

  // Finally, provider steering should be disabled once we clear the override.
  gDNSOverride.clearHostOverride(TEST_DOMAIN);
  await testNetChangeResult(AUTO_TRR_URI, "enable_doh");

  checkScalars(
    [
      [
        "networking.doh_heuristics_result",
        { value: Heuristics.Telemetry.pass },
      ],
      ["networking.doh_heuristic_ever_tripped", { value: true, key: "google" }],
      // All of the other heuristics must be false.
    ].concat(falseExpectations(["google"]))
  );
});
