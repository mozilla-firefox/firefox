"use strict";

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const { FeatureManifest } = ChromeUtils.importESModule(
  "resource://nimbus/FeatureManifest.sys.mjs"
);

const FEATURE_ID = "testfeature1";
// Note: this gets deleted at the end of tests
const TEST_PREF_BRANCH = "testfeature1.";
const TEST_FEATURE = new ExperimentFeature(FEATURE_ID, {
  variables: {
    enabled: {
      type: "boolean",
      fallbackPref: `${TEST_PREF_BRANCH}enabled`,
    },
    name: {
      type: "string",
      fallbackPref: `${TEST_PREF_BRANCH}name`,
    },
    count: {
      type: "int",
      fallbackPref: `${TEST_PREF_BRANCH}count`,
    },
    items: {
      type: "json",
      fallbackPref: `${TEST_PREF_BRANCH}items`,
    },
    jsonNoFallback: {
      type: "json",
    },
  },
});

add_setup(() => {
  const cleanupFeature = NimbusTestUtils.addTestFeatures(TEST_FEATURE);
  FeatureManifest[FEATURE_ID] = TEST_FEATURE.manifest;

  registerCleanupFunction(() => {
    cleanupFeature();
    delete FeatureManifest[FEATURE_ID];
  });
});

add_task(async function test_ExperimentFeature_getFallbackPrefName() {
  Assert.equal(
    TEST_FEATURE.getFallbackPrefName("enabled"),
    "testfeature1.enabled",
    "should return the fallback preference name"
  );
});

add_task(
  {
    skip_if: () => !AppConstants.NIGHTLY_BUILD,
  },
  async function test_ExperimentFeature_getVariable_notRegistered() {
    Assert.throws(
      () => {
        TEST_FEATURE.getVariable("non_existant_variable");
      },
      /Nimbus: Warning - variable "non_existant_variable" is not defined in FeatureManifest\.yaml/,
      "should throw in automation for variables not defined in the manifest"
    );
  }
);

add_task(async function test_ExperimentFeature_getVariable_noFallbackPref() {
  const { cleanup } = await NimbusTestUtils.setupTest();

  Assert.equal(
    TEST_FEATURE.getVariable("jsonNoFallback"),
    undefined,
    "should return undefined if no values are set and no fallback pref is defined"
  );

  cleanup();
});

add_task(async function test_ExperimentFeature_getVariable_precedence() {
  const { manager, cleanup } = await NimbusTestUtils.setupTest();

  const prefName = TEST_FEATURE.manifest.variables.items.fallbackPref;
  const rollout = ExperimentFakes.rollout(`${FEATURE_ID}-rollout`, {
    branch: {
      slug: "slug",
      ratio: 1,
      features: [
        {
          featureId: FEATURE_ID,
          value: { items: [4, 5, 6] },
        },
      ],
    },
  });

  Services.prefs.clearUserPref(prefName);

  Assert.equal(
    TEST_FEATURE.getVariable("items"),
    undefined,
    "should return undefined if the fallback pref is not set"
  );

  // Default pref values
  Services.prefs.setStringPref(prefName, JSON.stringify([1, 2, 3]));

  Assert.deepEqual(
    TEST_FEATURE.getVariable("items"),
    [1, 2, 3],
    "should return the default pref value"
  );

  manager.store.addEnrollment(rollout);

  Assert.deepEqual(
    TEST_FEATURE.getVariable("items"),
    [4, 5, 6],
    "should return the remote default value over the default pref value"
  );

  // Experiment values
  const doExperimentCleanup = await ExperimentFakes.enrollWithFeatureConfig(
    {
      featureId: FEATURE_ID,
      value: {
        items: [7, 8, 9],
      },
    },
    { manager }
  );

  Assert.deepEqual(
    TEST_FEATURE.getVariable("items"),
    [7, 8, 9],
    "should return the experiment value over the remote value"
  );

  Services.prefs.deleteBranch(TEST_PREF_BRANCH);
  doExperimentCleanup();
  manager.unenroll(rollout.slug);
  cleanup();
});

add_task(async function test_ExperimentFeature_getVariable_partial_values() {
  const { manager, cleanup } = await NimbusTestUtils.setupTest();
  const rollout = ExperimentFakes.rollout(`${FEATURE_ID}-rollout`, {
    branch: {
      slug: "slug",
      ratio: 1,
      features: [
        {
          featureId: FEATURE_ID,
          value: { name: "abc" },
        },
      ],
    },
  });

  // Set up a pref value for .enabled,
  // a remote value for .name,
  // an experiment value for .items
  Services.prefs.setBoolPref(
    TEST_FEATURE.manifest.variables.enabled.fallbackPref,
    true
  );
  manager.store.addEnrollment(rollout);
  const doExperimentCleanup = await ExperimentFakes.enrollWithFeatureConfig(
    {
      featureId: FEATURE_ID,
      value: {},
    },
    { manager }
  );

  Assert.equal(
    TEST_FEATURE.getVariable("enabled"),
    true,
    "should skip missing variables from remote defaults"
  );

  Assert.equal(
    TEST_FEATURE.getVariable("name"),
    "abc",
    "should skip missing variables from experiments"
  );

  Services.prefs.getDefaultBranch(null).deleteBranch(TEST_PREF_BRANCH);
  Services.prefs.deleteBranch(TEST_PREF_BRANCH);
  doExperimentCleanup();
  manager.unenroll(rollout.slug);
  cleanup();
});

add_task(async function testGetVariableCoenrolling() {
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
    () => NimbusFeatures.foo.getVariable("bar"),
    /Co-enrolling features must use the getAllEnrollments API/
  );

  cleanupFeature();
});
