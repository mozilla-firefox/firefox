"use strict";

const FEATURE_ID = "aboutwelcome";
const TEST_FALLBACK_PREF = "browser.aboutwelcome.screens";
const FAKE_FEATURE_MANIFEST = {
  variables: {
    screens: {
      type: "json",
      fallbackPref: TEST_FALLBACK_PREF,
    },
    source: {
      type: "string",
    },
  },
};

add_task(
  async function test_ExperimentFeature_getAllVariables_prefsOverDefaults() {
    const { cleanup } = await NimbusTestUtils.setupTest();

    const featureInstance = new ExperimentFeature(
      FEATURE_ID,
      FAKE_FEATURE_MANIFEST
    );

    Services.prefs.clearUserPref(TEST_FALLBACK_PREF);

    Assert.equal(
      featureInstance.getAllVariables().screens?.length,
      undefined,
      "pref is not set"
    );

    Services.prefs.setStringPref(TEST_FALLBACK_PREF, "[]");

    Assert.deepEqual(
      featureInstance.getAllVariables().screens.length,
      0,
      "should return the user pref value over the defaults"
    );

    Services.prefs.clearUserPref(TEST_FALLBACK_PREF);

    cleanup();
  }
);

add_task(
  async function test_ExperimentFeature_getAllVariables_experimentOverPref() {
    const { manager, cleanup } = await NimbusTestUtils.setupTest();
    const recipe = ExperimentFakes.experiment("awexperiment", {
      branch: {
        slug: "treatment",
        ratio: 1,
        features: [
          {
            featureId: "aboutwelcome",
            value: { screens: ["test-value"] },
          },
        ],
      },
    });

    await manager.store.addEnrollment(recipe);

    const featureInstance = new ExperimentFeature(
      FEATURE_ID,
      FAKE_FEATURE_MANIFEST
    );

    Services.prefs.clearUserPref(TEST_FALLBACK_PREF);

    Assert.ok(
      !!featureInstance.getAllVariables().screens,
      "should return the AW experiment value"
    );

    Assert.equal(
      featureInstance.getAllVariables().screens[0],
      "test-value",
      "should return the AW experiment value"
    );

    Services.prefs.setStringPref(TEST_FALLBACK_PREF, "[]");
    Assert.equal(
      featureInstance.getAllVariables().screens[0],
      "test-value",
      "should return the AW experiment value"
    );

    ExperimentFakes.cleanupAll([recipe.slug], { manager });
    Assert.deepEqual(
      featureInstance.getAllVariables().screens.length,
      0,
      "should return the user pref value"
    );

    Services.prefs.clearUserPref(TEST_FALLBACK_PREF);

    cleanup();
  }
);

add_task(
  async function test_ExperimentFeature_getAllVariables_experimentOverRemote() {
    Services.prefs.clearUserPref(TEST_FALLBACK_PREF);
    const { manager, cleanup } = await NimbusTestUtils.setupTest();
    const featureInstance = new ExperimentFeature(
      FEATURE_ID,
      FAKE_FEATURE_MANIFEST
    );
    const recipe = ExperimentFakes.experiment("aw-experiment", {
      branch: {
        slug: "treatment",
        ratio: 1,
        features: [
          {
            featureId: FEATURE_ID,
            value: { screens: ["test-value"] },
          },
        ],
      },
    });
    const rollout = ExperimentFakes.rollout("aw-rollout", {
      branch: {
        slug: "treatment",
        ratio: 1,
        features: [
          { featureId: FEATURE_ID, value: { screens: [], source: "rollout" } },
        ],
      },
    });
    // We're using the store in this test we need to wait for it to load
    await manager.store.ready();

    manager.store.addEnrollment(recipe);
    manager.store.addEnrollment(rollout);

    const allVariables = featureInstance.getAllVariables();

    Assert.equal(allVariables.screens.length, 1, "Returns experiment value");
    Assert.ok(!allVariables.source, "Does not include rollout value");

    ExperimentFakes.cleanupAll([recipe.slug, rollout.slug], { manager });

    cleanup();
  }
);

add_task(
  async function test_ExperimentFeature_getAllVariables_rolloutOverPrefDefaults() {
    const { manager, cleanup } = await NimbusTestUtils.setupTest();
    const featureInstance = new ExperimentFeature(
      FEATURE_ID,
      FAKE_FEATURE_MANIFEST
    );
    const rollout = ExperimentFakes.rollout("foo-aw", {
      branch: {
        slug: "getAllVariables",
        ratio: 1,
        features: [{ featureId: FEATURE_ID, value: { screens: [] } }],
      },
    });

    Services.prefs.clearUserPref(TEST_FALLBACK_PREF);

    Assert.equal(
      featureInstance.getAllVariables().screens?.length,
      undefined,
      "Pref is not set"
    );

    manager.store.addEnrollment(rollout);

    Assert.deepEqual(
      featureInstance.getAllVariables().screens?.length,
      0,
      "Should return the rollout value over the defaults"
    );

    Services.prefs.setStringPref(TEST_FALLBACK_PREF, "[1,2,3]");

    Assert.deepEqual(
      featureInstance.getAllVariables().screens.length,
      0,
      "should return the rollout value over the user pref"
    );

    Services.prefs.clearUserPref(TEST_FALLBACK_PREF);

    manager.unenroll(rollout.slug);
    cleanup();
  }
);

add_task(
  async function test_ExperimentFeature_getAllVariables_defaultValuesParam() {
    const { cleanup } = await NimbusTestUtils.setupTest();
    const featureInstance = new ExperimentFeature(
      FEATURE_ID,
      FAKE_FEATURE_MANIFEST
    );

    Services.prefs.clearUserPref(TEST_FALLBACK_PREF);

    Assert.equal(
      featureInstance.getAllVariables({ defaultValues: { screens: null } })
        .screens,
      null,
      "should return defaultValues param over default pref settings"
    );

    cleanup();
  }
);

add_task(async function testGetAllVariablesCoenrolling() {
  const cleanupFeature = NimbusTestUtils.addTestFeatures(
    new ExperimentFeature("foo", {
      allowCoenrollment: true,
      variables: {
        bar: {
          type: "string",
        },
      },
    })
  );

  Assert.throws(
    () => NimbusFeatures.foo.getAllVariables(),
    /Co-enrolling features must use the getAllEnrollments API/
  );

  cleanupFeature();
});
