//creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);
const { RemoteSettingsExperimentLoader } = ChromeUtils.importESModule(
  "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs"
);

async function setup(recipes) {
  const sandbox = sinon.createSandbox();

  sandbox.stub(ExperimentManager, "forceEnroll");

  const client = RemoteSettings("nimbus-desktop-experiments");
  await client.db.importChanges({}, Date.now(), recipes, {
    clear: true,
  });

  return async function cleanup() {
    await client.db.clear();
    sandbox.restore();
  };
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["messaging-system.log", "all"],
      ["datareporting.healthreport.uploadEnabled", true],
      ["app.shield.optoutstudies.enabled", true],
      ["nimbus.debug", true],
    ],
  });

  await RemoteSettingsExperimentLoader.finishedUpdating();

  registerCleanupFunction(async () => {
    await SpecialPowers.popPrefEnv();
  });
});

add_task(async function test_fetch_recipe_and_branch_no_debug() {
  Services.prefs.setBoolPref("nimbus.debug", false);

  const slug = "test_fetch_recipe_and_branch_no_debug";
  const recipe = NimbusTestUtils.factories.recipe(slug, { targeting: "false" });
  const cleanup = await setup([recipe]);

  await Assert.rejects(
    RemoteSettingsExperimentLoader.optInToExperiment({
      slug,
      branch: "control",
    }),
    /Could not opt in/,
    "should throw an error"
  );

  Assert.ok(
    ExperimentManager.forceEnroll.notCalled,
    "forceEnroll is not called"
  );

  Services.prefs.setBoolPref("nimbus.debug", true);

  await RemoteSettingsExperimentLoader.optInToExperiment({
    slug,
    branch: "control",
  });

  Assert.ok(ExperimentManager.forceEnroll.called, "forceEnroll is called");

  await cleanup();
});

add_task(async function test_fetch_recipe_and_branch_badslug() {
  const cleanup = await setup([]);

  await Assert.rejects(
    RemoteSettingsExperimentLoader.optInToExperiment({
      slug: "other_slug",
      branch: "control",
    }),
    /Could not find experiment slug other_slug/,
    "should throw an error"
  );

  Assert.ok(
    ExperimentManager.forceEnroll.notCalled,
    "forceEnroll is not called"
  );

  await cleanup();
});

add_task(async function test_fetch_recipe_and_branch_badbranch() {
  const slug = "test_fetch_recipe_and_branch_badbranch";
  const recipe = NimbusTestUtils.factories.recipe(slug, { targeting: "false" });
  const cleanup = await setup([recipe]);

  await Assert.rejects(
    RemoteSettingsExperimentLoader.optInToExperiment({
      slug,
      branch: "other_branch",
    }),
    new RegExp(`Could not find branch slug other_branch in ${slug}`),
    "should throw an error"
  );

  Assert.ok(
    ExperimentManager.forceEnroll.notCalled,
    "forceEnroll is not called"
  );

  await cleanup();
});

add_task(async function test_fetch_recipe_and_branch() {
  const slug = "test_fetch_recipe_and_branch";
  const recipe = NimbusTestUtils.factories.recipe(slug, { targeting: "false" });
  const cleanup = await setup([recipe]);

  await RemoteSettingsExperimentLoader.optInToExperiment({
    slug,
    branch: "control",
  });

  Assert.ok(
    ExperimentManager.forceEnroll.calledOnceWithExactly(
      recipe,
      recipe.branches[0]
    ),
    "Called forceEnroll"
  );

  await cleanup();
});

add_task(async function test_invalid_recipe() {
  const slug = "test_invalid_recipe";
  const recipe = NimbusTestUtils.factories.recipe("test_invalid_recipe", {
    targeting: "false",
  });
  delete recipe.branches;

  const cleanup = await setup([recipe]);

  await Assert.rejects(
    RemoteSettingsExperimentLoader.optInToExperiment({
      slug,
      branch: "control",
    }),
    /failed validation/
  );

  Assert.ok(ExperimentManager.forceEnroll.notCalled, "forceEnroll not called");

  await cleanup();
});

add_task(async function test_invalid_branch_variablesOnly() {
  const slug = "test_invalid_branch_variablesonly";
  const recipe = NimbusTestUtils.factories.recipe(slug, {
    branches: [
      {
        ratio: 1,
        slug: "control",
        features: [
          {
            featureId: "testFeature",
            value: {
              enabled: "foo",
              testInt: true,
              testSetString: 123,
            },
          },
        ],
      },
    ],
    targeting: "false",
  });

  const cleanup = await setup([recipe]);

  await Assert.rejects(
    RemoteSettingsExperimentLoader.optInToExperiment({
      slug,
      branch: "control",
    }),
    /failed validation/
  );

  Assert.ok(ExperimentManager.forceEnroll.notCalled, "forceEnroll not called");

  await cleanup();
});

add_task(async function test_invalid_branch_schema() {
  const slug = "test_invalid_branch_schema";
  const recipe = NimbusTestUtils.factories.recipe(slug, {
    branches: [
      {
        ratio: 1,
        slug: "control",
        features: [
          {
            featureId: "legacyHeartbeat",
            value: {
              foo: "bar",
            },
          },
        ],
      },
    ],
  });

  const cleanup = await setup([recipe]);

  await Assert.rejects(
    RemoteSettingsExperimentLoader.optInToExperiment({
      slug,
      branch: "control",
    }),
    /failed validation/
  );

  Assert.ok(ExperimentManager.forceEnroll.notCalled, "forceEnroll not called");

  await cleanup();
});

add_task(async function test_invalid_branch_featureId() {
  const slug = "test_invalid_branch_featureId";
  const recipe = NimbusTestUtils.factories.recipe(slug, {
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [
          {
            featureId: "UNKNOWN",
            value: {},
          },
        ],
      },
    ],
    targeting: "false",
  });

  const cleanup = await setup([recipe]);

  await Assert.rejects(
    RemoteSettingsExperimentLoader.optInToExperiment({
      slug,
      branch: "control",
    }),
    /failed validation/
  );

  Assert.ok(ExperimentManager.forceEnroll.notCalled, "forceEnroll not called");

  await cleanup();
});
