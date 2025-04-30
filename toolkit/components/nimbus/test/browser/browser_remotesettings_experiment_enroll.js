"use strict";

const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);
const { RemoteSettingsExperimentLoader } = ChromeUtils.importESModule(
  "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs"
);

let rsClient;

add_setup(async function () {
  rsClient = RemoteSettings("nimbus-desktop-experiments");
  await rsClient.db.importChanges({}, Date.now(), [], { clear: true });

  await SpecialPowers.pushPrefEnv({
    set: [
      ["messaging-system.log", "all"],
      ["datareporting.healthreport.uploadEnabled", true],
      ["app.shield.optoutstudies.enabled", true],
    ],
  });

  await ExperimentAPI.ready();
  await RemoteSettingsExperimentLoader.finishedUpdating();

  registerCleanupFunction(async () => {
    await SpecialPowers.popPrefEnv();
    await rsClient.db.clear();
  });
});

add_task(async function test_experimentEnrollment() {
  // Need to randomize the slug so subsequent test runs don't skip enrollment
  // due to a conflicting slug
  const recipe = ExperimentFakes.recipe("foo" + Math.random(), {
    bucketConfig: {
      start: 0,
      // Make sure the experiment enrolls
      count: 10000,
      total: 10000,
      namespace: "mochitest",
      randomizationUnit: "normandy_id",
    },
  });
  await rsClient.db.importChanges({}, Date.now(), [recipe], {
    clear: true,
  });

  await RemoteSettingsExperimentLoader.updateRecipes("mochitest");

  let experiment = ExperimentAPI.getExperimentMetaData({
    slug: recipe.slug,
  });

  Assert.ok(experiment.active, "Should be enrolled in the experiment");

  ExperimentManager.unenroll(recipe.slug);

  experiment = ExperimentAPI.getExperimentMetaData({
    slug: recipe.slug,
  });

  Assert.ok(!experiment.active, "Experiment is no longer active");

  assertEmptyStore(ExperimentManager.store);
});

add_task(async function test_experimentEnrollment_startup() {
  // Studies pref can turn the feature off but if the feature pref is off
  // then it stays off.
  await SpecialPowers.pushPrefEnv({
    set: [["app.shield.optoutstudies.enabled", false]],
  });

  Assert.ok(!RemoteSettingsExperimentLoader._enabled, "Should be disabled");

  await SpecialPowers.pushPrefEnv({
    set: [["app.shield.optoutstudies.enabled", true]],
  });

  Assert.ok(RemoteSettingsExperimentLoader._enabled, "Should be enabled");
});
