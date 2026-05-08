/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { LinkPreview } = ChromeUtils.importESModule(
  "moz-src:///browser/components/genai/LinkPreview.sys.mjs"
);
const { MLUninstallService } = ChromeUtils.importESModule(
  "chrome://global/content/ml/Utils.sys.mjs"
);
const { FEATURES } = ChromeUtils.importESModule(
  "chrome://global/content/ml/EngineProcess.sys.mjs"
);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

/**
 * Test the AIFeature id getter.
 */
add_task(async function test_aifeature_id() {
  is(LinkPreview.id, "link-preview", "id getter should return 'link-preview'");
});

/**
 * Test the AIFeature enable() method.
 */
add_task(async function test_aifeature_enable() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.prefetchOnEnable", false],
      ["browser.ml.linkPreview.enabled", false],
      ["browser.ml.linkPreview.optin", false],
    ],
  });

  await LinkPreview.enable();

  is(
    Services.prefs.getBoolPref("browser.ml.linkPreview.enabled"),
    true,
    "enable() should set enabled pref to true"
  );
  is(
    Services.prefs.getBoolPref("browser.ml.linkPreview.optin"),
    true,
    "enable() should set optin pref to true"
  );

  is(
    LinkPreview.aiControlState,
    "enabled",
    "enable() should set the AI Controls state to enabled"
  );
});

/**
 * Test the AIFeature disable() method.
 */
add_task(async function test_aifeature_disable() {
  const uninstallStub = sinon.stub(MLUninstallService, "uninstall").resolves();

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.prefetchOnEnable", false],
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", true],
    ],
  });

  await LinkPreview.block();

  is(
    Services.prefs.getBoolPref("browser.ml.linkPreview.enabled"),
    false,
    "disable() should set enabled pref to false"
  );
  is(
    Services.prefs.getBoolPref("browser.ml.linkPreview.optin"),
    false,
    "disable() should set optin pref to false"
  );
  is(LinkPreview.isBlocked, true, "block() should set the blocked state");

  is(
    LinkPreview.aiControlState,
    "blocked",
    "block() should set the AI Controls state to blocked"
  );

  is(
    uninstallStub.callCount,
    1,
    "MLUninstallService.uninstall should be called once"
  );

  const uninstallArgs = uninstallStub.getCall(0).args[0];
  ok(
    uninstallArgs.engineIds.includes(FEATURES["link-preview"].engineId),
    "uninstall called with correct engine ID"
  );
  is(
    uninstallArgs.actor,
    "LinkPreview",
    "uninstall called with correct actor name"
  );

  uninstallStub.restore();
});

/**
 * Test the AIFeature reset() method.
 */
add_task(async function test_aifeature_reset() {
  const uninstallStub = sinon.stub(MLUninstallService, "uninstall").resolves();

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.prefetchOnEnable", false],
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", true],
      ["browser.ml.linkPreview.collapsed", true],
      ["browser.ml.linkPreview.shift", true],
      ["browser.ml.linkPreview.shiftAlt", false],
      ["browser.ml.linkPreview.longPress", true],
      ["browser.ml.linkPreview.labs", 1],
      ["browser.ml.linkPreview.onboardingTimes", "123,456"],
      ["browser.ml.linkPreview.nimbus", "test:branch"],
    ],
  });

  await LinkPreview.makeAvailable();

  is(
    Services.prefs.getBoolPref("browser.ml.linkPreview.enabled"),
    true,
    "makeAvailable() should set enabled pref to true"
  );
  ok(
    !Services.prefs.prefHasUserValue("browser.ml.linkPreview.optin"),
    "makeAvailable() should clear the optin user pref"
  );
  ok(
    !Services.prefs.prefHasUserValue("browser.ml.linkPreview.collapsed"),
    "reset() should clear collapsed pref"
  );
  ok(
    !Services.prefs.prefHasUserValue("browser.ml.linkPreview.shift"),
    "reset() should clear shift pref"
  );
  ok(
    !Services.prefs.prefHasUserValue("browser.ml.linkPreview.shiftAlt"),
    "reset() should clear shiftAlt pref"
  );
  ok(
    !Services.prefs.prefHasUserValue("browser.ml.linkPreview.longPress"),
    "reset() should clear longPress pref"
  );
  ok(
    !Services.prefs.prefHasUserValue("browser.ml.linkPreview.labs"),
    "reset() should clear labs pref"
  );
  ok(
    !Services.prefs.prefHasUserValue("browser.ml.linkPreview.onboardingTimes"),
    "reset() should clear onboardingTimes pref"
  );
  ok(
    !Services.prefs.prefHasUserValue("browser.ml.linkPreview.nimbus"),
    "reset() should clear nimbus pref"
  );

  is(
    uninstallStub.callCount,
    1,
    "MLUninstallService.uninstall should be called once during reset"
  );

  uninstallStub.restore();
});

/**
 * Test the AIFeature isEnabled getter.
 */
add_task(async function test_aifeature_isEnabled() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", false],
      ["browser.ml.linkPreview.optin", false],
    ],
  });

  is(
    LinkPreview.isEnabled,
    false,
    "isEnabled should be false when both prefs are false"
  );

  Services.prefs.setBoolPref("browser.ml.linkPreview.enabled", true);
  is(
    LinkPreview.isEnabled,
    false,
    "isEnabled should be false when only enabled is true"
  );

  Services.prefs.setBoolPref("browser.ml.linkPreview.enabled", false);
  Services.prefs.setBoolPref("browser.ml.linkPreview.optin", true);
  is(
    LinkPreview.isEnabled,
    false,
    "isEnabled should be false when only optin is true"
  );

  Services.prefs.setBoolPref("browser.ml.linkPreview.enabled", true);
  Services.prefs.setBoolPref("browser.ml.linkPreview.optin", true);
  is(
    LinkPreview.isEnabled,
    true,
    "isEnabled should be true when both prefs are true"
  );
});

/**
 * Test the AIFeature isAllowed method in supported region and locale.
 */
add_task(async function test_aifeature_isAllowed() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.optin", true]],
  });

  const regionStub = sinon
    .stub(LinkPreview, "_isRegionSupported")
    .returns(true);
  const localeStub = sinon
    .stub(LinkPreview, "_isLocaleSupported")
    .returns(true);

  is(
    LinkPreview.isAllowed,
    true,
    "isAllowed returns true for supported region and locale"
  );

  is(
    LinkPreview.isAllowed,
    LinkPreview.canShowKeyPoints,
    "isAllowed should match canShowKeyPoints"
  );

  localeStub.restore();
  regionStub.restore();
});

/**
 * Test the AIFeature isAllowed method in unsupported region.
 */
add_task(async function test_aifeature_isAllowed_unsupported_region() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.optin", true]],
  });

  const regionStub = sinon
    .stub(LinkPreview, "_isRegionSupported")
    .returns(false);
  const localeStub = sinon
    .stub(LinkPreview, "_isLocaleSupported")
    .returns(true);

  is(
    LinkPreview.isAllowed,
    false,
    "isAllowed returns false for unsupported region"
  );

  is(
    LinkPreview.isAllowed,
    LinkPreview.canShowKeyPoints,
    "isAllowed matches canShowKeyPoints in unsupported region"
  );

  localeStub.restore();
  regionStub.restore();
});

/**
 * Test the AIFeature isAllowed method in unsupported locale.
 */
add_task(async function test_aifeature_isAllowed_unsupported_locale() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.optin", true]],
  });

  const regionStub = sinon
    .stub(LinkPreview, "_isRegionSupported")
    .returns(true);
  const localeStub = sinon
    .stub(LinkPreview, "_isLocaleSupported")
    .returns(false);

  is(
    LinkPreview.isAllowed,
    false,
    "isAllowed returns false for unsupported locale"
  );

  is(
    LinkPreview.isAllowed,
    LinkPreview.canShowKeyPoints,
    "isAllowed matches canShowKeyPoints for unsupported locale"
  );

  localeStub.restore();
  regionStub.restore();
});

/**
 * Test the AIFeature isManagedByPolicy method when disabled by policy.
 */
add_task(async function test_aifeature_isAllowed_policy_disabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.linkPreview.optin", false]],
  });

  Services.prefs.lockPref("browser.ml.linkPreview.optin");

  const regionStub = sinon
    .stub(LinkPreview, "_isRegionSupported")
    .returns(true);
  const localeStub = sinon
    .stub(LinkPreview, "_isLocaleSupported")
    .returns(true);

  is(
    LinkPreview.isAllowed,
    true,
    "isAllowed returns true when disabled by policy"
  );
  is(
    LinkPreview.isManagedByPolicy,
    true,
    "isManagedByPolicy returns false when disabled by policy"
  );

  is(
    LinkPreview.canShowKeyPoints,
    false,
    "canShowKeyPoints returns false when disabled by policy"
  );

  Services.prefs.unlockPref("browser.ml.linkPreview.optin");
  localeStub.restore();
  regionStub.restore();
});

/**
 * Test the AIFeature isBlocked getter.
 */
add_task(async function test_aifeature_isBlocked() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", false],
    ],
  });

  is(
    LinkPreview.isBlocked,
    false,
    "isBlocked should be false when the feature is available"
  );

  Services.prefs.setBoolPref("browser.ml.linkPreview.enabled", false);

  is(
    LinkPreview.isBlocked,
    true,
    "isBlocked should be true when the feature is disabled"
  );
});

/**
 * Test disable -> re-enable -> link preview workflow using AIFeature interface.
 */
add_task(async function test_aifeature_disable_enable_workflow() {
  const uninstallStub = sinon.stub(MLUninstallService, "uninstall").resolves();

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.prefetchOnEnable", false],
      ["browser.ml.linkPreview.enabled", false],
      ["browser.ml.linkPreview.optin", false],
    ],
  });

  is(LinkPreview.isEnabled, false, "starts disabled");

  await LinkPreview.enable();

  is(LinkPreview.isEnabled, true, "feature enabled after calling enable()");
  is(uninstallStub.callCount, 0, "uninstall not called during enable");

  // Disable the feature
  await LinkPreview.block();

  is(LinkPreview.isEnabled, false, "feature disabled after calling disable()");
  is(uninstallStub.callCount, 1, "uninstall called once during disable");

  // Re-enable the feature
  uninstallStub.resetHistory();

  await LinkPreview.enable();

  is(LinkPreview.isEnabled, true, "feature re-enabled after calling enable()");
  is(uninstallStub.callCount, 0, "uninstall not called during second enable");

  uninstallStub.restore();
});

/**
 * Test that disable() calls uninstall.
 */
add_task(async function test_aifeature_disable_calls_uninstall() {
  const uninstallStub = sinon.stub(MLUninstallService, "uninstall").resolves();

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.prefetchOnEnable", false],
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", true],
    ],
  });

  await LinkPreview.block();

  is(
    uninstallStub.callCount,
    1,
    "uninstallModel should be called once during disable"
  );

  uninstallStub.restore();
});

/**
 * Test that reset() calls uninstall.
 */
add_task(async function test_aifeature_reset_calls_uninstall() {
  const uninstallStub = sinon.stub(MLUninstallService, "uninstall").resolves();

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.prefetchOnEnable", false],
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", true],
    ],
  });

  await LinkPreview.makeAvailable();

  is(
    uninstallStub.callCount,
    1,
    "uninstallModel should be called once during reset"
  );

  uninstallStub.restore();
});

/**
 * Test that setting pref directly doesn't call uninstall.
 */
add_task(async function test_pref_change_no_uninstall() {
  const uninstallStub = sinon.stub(MLUninstallService, "uninstall").resolves();

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.prefetchOnEnable", false],
      ["browser.ml.linkPreview.enabled", true],
      ["browser.ml.linkPreview.optin", true],
    ],
  });

  // Directly set pref to false (not using disable() method)
  Services.prefs.setBoolPref("browser.ml.linkPreview.enabled", false);

  is(
    uninstallStub.callCount,
    0,
    "uninstall should not be called when pref is set directly"
  );

  uninstallStub.restore();
});

/**
 * Test that enable() and disable() can be called sequentially.
 */
add_task(async function test_aifeature_sequential_enable_disable() {
  const uninstallStub = sinon.stub(MLUninstallService, "uninstall").resolves();

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.linkPreview.prefetchOnEnable", false],
      ["browser.ml.linkPreview.enabled", false],
      ["browser.ml.linkPreview.optin", false],
    ],
  });

  await LinkPreview.enable();
  is(LinkPreview.isEnabled, true, "Should be enabled after enable()");

  await LinkPreview.block();
  is(LinkPreview.isEnabled, false, "Should be disabled after disable()");

  await LinkPreview.enable();
  is(
    LinkPreview.isEnabled,
    true,
    "Should be enabled again after second enable()"
  );

  uninstallStub.restore();
});
