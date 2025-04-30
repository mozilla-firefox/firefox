/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

const { ExperimentFakes } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);

/**
 * Check that enrolling into sidebar experiments sets user prefs
 */
add_task(async function test_nimbus_user_prefs() {
  const nimbus = "sidebar.nimbus";
  const vertical = VERTICAL_TABS_PREF;

  Assert.ok(
    !Services.prefs.prefHasUserValue(nimbus),
    "No user nimbus pref yet"
  );

  let cleanup = await ExperimentFakes.enrollWithFeatureConfig({
    featureId: "sidebar",
    value: {
      verticalTabs: true,
    },
  });

  const nimbusValue = Services.prefs.getStringPref(nimbus);

  Assert.ok(nimbusValue, "Set some nimbus slug");
  Assert.ok(Services.prefs.getBoolPref(vertical), "vertical set to true");
  Assert.ok(
    Services.prefs.prefHasUserValue(vertical),
    "vertical pref has user value"
  );

  cleanup();
  Services.prefs.clearUserPref(nimbus);
  Services.prefs.clearUserPref(vertical);
});

/**
 * Check that rollout sets prefs then prefer experiment
 */
add_task(async function test_nimbus_rollout_experiment() {
  const revamp = "sidebar.revamp";
  const nimbus = "sidebar.nimbus";
  await SpecialPowers.pushPrefEnv({ clear: [[revamp]] });

  const cleanRollout = await ExperimentFakes.enrollWithFeatureConfig(
    {
      featureId: "sidebar",
      value: { revamp: true },
    },
    { isRollout: true }
  );

  Assert.ok(Services.prefs.getBoolPref(revamp), "Set user pref with rollout");
  const nimbusValue = Services.prefs.getStringPref(nimbus);
  Assert.ok(nimbusValue, "Set some nimbus slug");

  const cleanExperiment = await ExperimentFakes.enrollWithFeatureConfig({
    featureId: "sidebar",
    value: { revamp: false },
  });

  Assert.ok(
    !Services.prefs.getBoolPref(revamp),
    "revamp pref flipped by experiment"
  );
  Assert.notEqual(
    Services.prefs.getStringPref(nimbus),
    nimbusValue,
    "nimbus pref changed by experiment"
  );

  cleanRollout();
  cleanExperiment();
  Services.prefs.clearUserPref(nimbus);
});

/**
 * Check that multi-feature chatbot sets prefs
 */
add_task(async function test_nimbus_multi_feature() {
  const chatbot = "browser.ml.chat.test";
  Assert.ok(!Services.prefs.prefHasUserValue(chatbot), "chatbot is default");

  const cleanup = await ExperimentFakes.enrollmentHelper(
    ExperimentFakes.recipe("foo", {
      branches: [
        {
          slug: "variant",
          features: [
            {
              featureId: "chatbot",
              value: { prefs: { test: { value: true } } },
            },
          ],
        },
      ],
    })
  );

  Assert.ok(Services.prefs.prefHasUserValue(chatbot), "chatbot user pref set");

  cleanup();

  Assert.ok(Services.prefs.prefHasUserValue(chatbot), "chatbot pref still set");

  Services.prefs.clearUserPref(chatbot);
  Services.prefs.clearUserPref("browser.ml.chat.nimbus");
  Services.prefs.clearUserPref("sidebar.nimbus");
});

/**
 * Check that minimum versions get enforced
 */
add_task(async function test_nimbus_minimum_version() {
  const revamp = "sidebar.revamp";
  const nimbus = "sidebar.nimbus";
  await SpecialPowers.pushPrefEnv({ clear: [[revamp]] });
  let cleanup = await ExperimentFakes.enrollWithFeatureConfig({
    featureId: "sidebar",
    value: {
      minVersion: AppConstants.MOZ_APP_VERSION_DISPLAY + ".1",
      revamp: true,
    },
  });

  Assert.ok(
    !Services.prefs.getBoolPref(revamp),
    "revamp pref not set for version"
  );
  Assert.ok(!Services.prefs.prefHasUserValue(nimbus), "nimbus pref not set");

  cleanup();

  cleanup = await ExperimentFakes.enrollWithFeatureConfig({
    featureId: "sidebar",
    value: {
      minVersion: AppConstants.MOZ_APP_VERSION_DISPLAY,
      revamp: true,
    },
  });

  Assert.ok(Services.prefs.getBoolPref(revamp), "revamp pref set for version");
  Assert.ok(Services.prefs.getStringPref(nimbus), "nimbus pref set");

  cleanup();
  Services.prefs.clearUserPref(nimbus);
  Services.prefs.clearUserPref(revamp);
});
