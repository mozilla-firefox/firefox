/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { FirefoxLabs } = ChromeUtils.importESModule(
  "resource://nimbus/FirefoxLabs.sys.mjs"
);

function setupTest({ ...ctx }) {
  return NimbusTestUtils.setupTest({ ...ctx, clearTelemetry: true });
}

add_task(async function test_all() {
  const { sandbox, manager, initExperimentAPI, cleanup } = await setupTest({
    experiments: [
      ExperimentFakes.recipe("opt-in-rollout", {
        bucketConfig: {
          ...ExperimentFakes.recipe.bucketConfig,
          count: 1000,
        },
        isRollout: true,
        isFirefoxLabsOptIn: true,
        firefoxLabsTitle: "title",
        firefoxLabsDescription: "description",
        firefoxLabsDescriptionLinks: null,
        firefoxLabsGroup: "group",
        requiresRestart: false,
      }),
      ExperimentFakes.recipe("opt-in-experiment", {
        bucketConfig: {
          ...ExperimentFakes.recipe.bucketConfig,
          count: 1000,
        },
        branches: [
          {
            ...ExperimentFakes.recipe.branches[0],
            firefoxLabsTitle: "title",
          },
        ],
        isFirefoxLabsOptIn: true,
        firefoxLabsTitle: "title",
        firefoxLabsDescription: "description",
        firefoxLabsDescriptionLinks: null,
        firefoxLabsGroup: "group",
        requiresRestart: false,
      }),
      ExperimentFakes.recipe("targeting-fail", {
        bucketConfig: {
          ...ExperimentFakes.recipe.bucketConfig,
          count: 1000,
        },
        targeting: "false",
        isRollout: true,
        isFirefoxLabsOptIn: true,
        firefoxLabsTitle: "title",
        firefoxLabsDescription: "description",
        firefoxLabsDescriptionLinks: null,
        firefoxLabsGroup: "group",
        requiresRestart: false,
      }),
      ExperimentFakes.recipe("bucketing-fail", {
        bucketConfig: {
          ...ExperimentFakes.recipe.bucketConfig,
          count: 0,
        },
        isRollout: true,
        isFirefoxLabsOptIn: true,
        firefoxLabsTitle: "title",
        firefoxLabsDescription: "description",
        firefoxLabsDescriptionLinks: null,
        firefoxLabsGroup: "group",
        requiresRestart: false,
      }),
      ExperimentFakes.recipe("experiment"),
      ExperimentFakes.recipe("rollout", { isRollout: true }),
    ],
    init: false,
  });

  // Stub out enrollment because we don't care.
  sandbox.stub(manager, "enroll");

  await initExperimentAPI();

  const labs = await FirefoxLabs.create();
  const availableSlugs = Array.from(labs.all(), recipe => recipe.slug).sort();

  Assert.deepEqual(
    availableSlugs,
    ["opt-in-rollout", "opt-in-experiment"].sort(),
    "Should return all opt in recipes that match targeting and bucketing"
  );

  cleanup();
});

add_task(async function test_enroll() {
  const recipe = ExperimentFakes.recipe("opt-in", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [{ featureId: "nimbus-qa-1", value: {} }],
      },
    ],
    isRollout: true,
    isFirefoxLabsOptIn: true,
    firefoxLabsTitle: "placeholder",
    firefoxLabsDescription: "placeholder",
    firefoxLabsDescriptionLinks: null,
    firefoxLabsGroup: "placeholder",
    requiresRestart: false,
  });

  const { sandbox, manager, initExperimentAPI, cleanup } = await setupTest({
    experiments: [recipe],
    init: false,
  });

  const enrollSpy = sandbox.spy(manager, "enroll");

  await initExperimentAPI();

  const labs = await FirefoxLabs.create();

  Services.fog.applyServerKnobsConfig(
    JSON.stringify({
      metrics_enabled: {
        "nimbus_events.enrollment_status": true,
      },
    })
  );

  await Assert.rejects(
    labs.enroll(),
    /enroll: slug and branchSlug are required/,
    "Should throw when enroll() is called without a slug"
  );

  await Assert.rejects(
    labs.enroll("opt-in"),
    /enroll: slug and branchSlug are required/,
    "Should throw when enroll() is called without a branch slug"
  );

  await labs.enroll("bogus", "bogus");
  Assert.ok(
    enrollSpy.notCalled,
    "ExperimentManager.enroll not called for unknown recipe"
  );

  await labs.enroll(recipe.slug, "bogus");
  Assert.ok(
    enrollSpy.notCalled,
    "ExperimentManager.enroll not called for invalid branch slug"
  );

  await labs.enroll(recipe.slug, recipe.branches[0].slug);
  Assert.ok(
    enrollSpy.calledOnceWith(recipe, "rs-loader", { branchSlug: "control" }),
    "ExperimentManager.enroll called"
  );

  Assert.deepEqual(
    Glean.nimbusEvents.enrollmentStatus
      .testGetValue("events")
      ?.map(ev => ev.extra),
    [
      {
        slug: recipe.slug,
        branch: "control",
        status: "Enrolled",
        reason: "OptIn",
      },
    ]
  );

  Assert.ok(manager.store.get(recipe.slug)?.active, "Active enrollment exists");

  labs.unenroll(recipe.slug);

  cleanup();
});

add_task(async function test_reenroll() {
  const recipe = ExperimentFakes.recipe("opt-in", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
    branches: [
      {
        ...ExperimentFakes.recipe.branches[0],
        slug: "control",
      },
    ],
    isFirefoxLabsOptIn: true,
    firefoxLabsTitle: "placeholder",
    firefoxLabsDescription: "placeholder",
    firefoxLabsDescriptionLinks: null,
    firefoxLabsGroup: "placeholder",
    requiresRestart: false,
    isRollout: true,
  });

  const { manager, cleanup } = await setupTest({ experiments: [recipe] });

  const labs = await FirefoxLabs.create();

  Assert.ok(
    typeof manager.store.get(recipe.slug) === "undefined",
    `No enrollment for ${recipe.slug}`
  );

  await labs.enroll(recipe.slug, "control");
  Assert.ok(
    manager.store.get(recipe.slug)?.active,
    `Active enrollment for ${recipe.slug}`
  );

  labs.unenroll(recipe.slug);
  Assert.ok(
    manager.store.get(recipe.slug)?.active === false,
    `Inactive enrollment for ${recipe.slug}`
  );

  await ExperimentAPI._rsLoader.updateRecipes();
  Assert.ok(
    manager.store.get(recipe.slug)?.active === false,
    `Inactive enrollment for ${recipe.slug} after updateRecipes()`
  );

  await labs.enroll(recipe.slug, "control");
  Assert.ok(
    manager.store.get(recipe.slug)?.active,
    `Active enrollment for ${recipe.slug}`
  );

  labs.unenroll(recipe.slug);

  cleanup();
});

add_task(async function test_unenroll() {
  const { manager, cleanup } = await setupTest({
    experiments: [
      ExperimentFakes.recipe("rollout", {
        bucketConfig: {
          ...ExperimentFakes.recipe.bucketConfig,
          count: 1000,
        },
        isRollout: true,
        branches: [
          {
            slug: "control",
            ratio: 1,
            features: [
              {
                featureId: "nimbus-qa-1",
                value: {},
              },
            ],
          },
        ],
      }),
      ExperimentFakes.recipe("opt-in", {
        bucketConfig: {
          ...ExperimentFakes.recipe.bucketConfig,
          count: 1000,
        },
        isRollout: true,
        branches: [
          {
            slug: "control",
            ratio: 1,
            features: [
              {
                featureId: "nimbus-qa-2",
                value: {},
              },
            ],
          },
        ],
        isFirefoxLabsOptIn: true,
        firefoxLabsTitle: "title",
        firefoxLabsDescription: "description",
        firefoxLabsDescriptionLinks: null,
        firefoxLabsGroup: "group",
        requiresRestart: false,
      }),
    ],
  });

  const labs = await FirefoxLabs.create();

  Assert.ok(manager.store.get("rollout")?.active, "Enrolled in rollout");
  Assert.ok(
    typeof manager.store.get("opt-in") === "undefined",
    "Did not enroll in rollout"
  );

  await labs.enroll("opt-in", "control");
  Assert.ok(manager.store.get("opt-in")?.active, "Enrolled in opt-in");

  Services.fog.applyServerKnobsConfig(
    JSON.stringify({
      metrics_enabled: {
        "nimbus_events.enrollment_status": true,
      },
    })
  );

  // Should not throw.
  labs.unenroll("bogus");

  // Should not throw.
  labs.unenroll("rollout");
  Assert.ok(
    manager.store.get("rollout").active,
    "Enrolled in rollout after attempting to unenroll with incorrect API"
  );

  labs.unenroll("opt-in");
  Assert.ok(!manager.store.get("opt-in").active, "Unenrolled from opt-in");

  // Should not throw.
  labs.unenroll("opt-in");

  Assert.deepEqual(
    Glean.nimbusEvents.enrollmentStatus
      .testGetValue("events")
      ?.map(ev => ev.extra),
    [
      {
        slug: "opt-in",
        branch: "control",
        status: "Disqualified",
        reason: "OptOut",
      },
    ]
  );

  manager.unenroll("rollout");
  cleanup();
});
