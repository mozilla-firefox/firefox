/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const { TelemetryTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryTestUtils.sys.mjs"
);
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

add_task(async function test_telemetry_basic() {
  await setupPolicyEngineWithJson({
    policies: {
      BlockAboutSupport: true,
    },
  });

  TelemetryTestUtils.assertScalar(
    TelemetryTestUtils.getProcessScalars("parent"),
    "policies.is_enterprise",
    true
  );
  equal(Services.policies.isEnterprise, true);
});

add_task(async function test_telemetry_count_ignores_onMissing() {
  do_get_profile();
  Services.fog.initializeFOG();

  // The second run has previouslyApplied set, so onMissing() defaults
  // (e.g. SitePolicies) are injected.
  await setupPolicyEngineWithJson({ policies: { BlockAboutSupport: true } });
  await setupPolicyEngineWithJson({ policies: { BlockAboutSupport: true } });

  Assert.deepEqual(Object.keys(Services.policies.getActivePolicies()), [
    "BlockAboutSupport",
  ]);
  Assert.equal(Glean.policies.count.testGetValue(), 1);
});

add_task(async function test_telemetry_roots_plus_policy() {
  await setupPolicyEngineWithJson({
    policies: {
      BlockAboutSupport: true,
      Certificates: {
        ImportEnterpriseRoots: true,
      },
    },
  });

  TelemetryTestUtils.assertScalar(
    TelemetryTestUtils.getProcessScalars("parent"),
    "policies.is_enterprise",
    true
  );
  equal(Services.policies.isEnterprise, true);
});

add_task(async function test_telemetry_esr() {
  await setupPolicyEngineWithJson({});
  TelemetryTestUtils.assertScalar(
    TelemetryTestUtils.getProcessScalars("parent"),
    "policies.is_enterprise",
    AppConstants.IS_ESR
  );
  equal(Services.policies.isEnterprise, AppConstants.IS_ESR);
});

add_task(async function test_telemetry_esr_mac_eol() {
  Services.prefs
    .getDefaultBranch(null)
    .setCharPref("distribution.id", "mozilla-mac-eol-esr115");
  await setupPolicyEngineWithJson({});
  TelemetryTestUtils.assertScalar(
    TelemetryTestUtils.getProcessScalars("parent"),
    "policies.is_enterprise",
    false
  );
  equal(Services.policies.isEnterprise, false);
});

add_task(async function test_telemetry_esr_win_eol() {
  Services.prefs
    .getDefaultBranch(null)
    .setCharPref("distribution.id", "mozilla-win-eol-esr115");
  await setupPolicyEngineWithJson({});
  TelemetryTestUtils.assertScalar(
    TelemetryTestUtils.getProcessScalars("parent"),
    "policies.is_enterprise",
    false
  );
  equal(Services.policies.isEnterprise, false);
});

add_task(async function test_telemetry_esr_distro() {
  Services.prefs
    .getDefaultBranch(null)
    .setCharPref("distribution.id", "any-other-distribution-id");
  await setupPolicyEngineWithJson({});
  TelemetryTestUtils.assertScalar(
    TelemetryTestUtils.getProcessScalars("parent"),
    "policies.is_enterprise",
    AppConstants.IS_ESR
  );
  equal(Services.policies.isEnterprise, AppConstants.IS_ESR);
});
