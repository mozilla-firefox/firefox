/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { FirefoxLabs } = ChromeUtils.importESModule(
  "resource://nimbus/FirefoxLabs.sys.mjs"
);

function setupTest({ ...ctx }) {
  return NimbusTestUtils.setupTest({ ...ctx, clearTelemetry: true });
}

add_setup(function () {
  Services.fog.initializeFOG();
});

add_task(async function test_all() {
  // This recipe conflicts with `feature-conflict`, defined below.
  const preexisting = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "preexisting-rollout",
    { featureId: "nimbus-qa-1" },
    { isRollout: true }
  );

  const alreadyEnrolled = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "already-enrolled-opt-in",
    { featureId: "nimbus-qa-2" },
    {
      isFirefoxLabsOptIn: true,
      isRollout: true,
    }
  );

  const preexistingPaused = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "preexisting-paused",
    { featureId: "no-feature-firefox-desktop" },
    {
      isEnrollmentPaused: true,
      isFirefoxLabsOptIn: true,
      isRollout: true,
    }
  );

  const { cleanup } = await setupTest({
    init: false,
    populateStore: store => {
      NimbusTestUtils.addEnrollmentForRecipe(preexisting, { store });
      NimbusTestUtils.addEnrollmentForRecipe(alreadyEnrolled, { store });
      NimbusTestUtils.addEnrollmentForRecipe(preexistingPaused, {
        store,
      });
    },
    experiments: [
      NimbusTestUtils.factories.recipe("opt-in-rollout", {
        isFirefoxLabsOptIn: true,
        isRollout: true,
      }),
      {
        ...NimbusTestUtils.factories.recipe("opt-in-experiment", {
          branches: [
            NimbusTestUtils.factories.branch("control", {
              firefoxLabsTitle: "title",
            }),
          ],
        }),
        isFirefoxLabsOptIn: true,
        firefoxLabsTitle: "title",
        firefoxLabsDescription: "description",
        firefoxLabsDescriptionLinks: null,
        firefoxLabsGroup: "group",
        requiresRestart: false,
      },
      NimbusTestUtils.factories.recipe("targeting-fail", {
        isFirefoxLabsOptIn: true,
        isRollout: true,
        targeting: "false",
      }),
      NimbusTestUtils.factories.recipe("bucketing-fail", {
        bucketConfig: NimbusTestUtils.factories.bucketConfig({
          count: 0,
        }),
        isFirefoxLabsOptIn: true,
        isRollout: true,
      }),
      NimbusTestUtils.factories.recipe.withFeatureConfig(
        "feature-does-not-exist",
        { featureId: "bogus" },
        {
          isFirefoxLabsOptIn: true,
          isRollout: true,
        }
      ),
      NimbusTestUtils.factories.recipe.withFeatureConfig(
        "feature-conflict",
        { featureId: "nimbus-qa-1" },
        { isRollout: true }
      ),
      NimbusTestUtils.factories.recipe.withFeatureConfig("experiment", {
        featureId: "no-feature-firefox-desktop",
      }),
      NimbusTestUtils.factories.recipe.withFeatureConfig(
        "rollout",
        { featureId: "no-feature-firefox-desktop" },
        { isRollout: true }
      ),
      NimbusTestUtils.factories.recipe.withFeatureConfig(
        "paused",
        { featureId: "no-feature-firefox-desktop" },
        {
          isEnrollmentPaused: true,
          isFirefoxLabsOptIn: true,
          isRollout: true,
        }
      ),
      preexisting, // Prevent unenrollment.
      preexistingPaused,
      alreadyEnrolled,
    ],
    migrationState: NimbusTestUtils.migrationState.LATEST,
  });

  await ExperimentAPI.init();

  const labs = await FirefoxLabs.create();
  const availableSlugs = Array.from(labs.all(), recipe => recipe.slug).sort();

  Assert.deepEqual(
    availableSlugs,
    [
      "opt-in-rollout",
      "opt-in-experiment",
      "already-enrolled-opt-in",
      "preexisting-paused",
    ].sort(),
    "Should return all opt in recipes that match targeting and bucketing"
  );

  await NimbusTestUtils.cleanupManager([
    "already-enrolled-opt-in",
    "experiment",
    "rollout",
    "preexisting-rollout",
    "preexisting-paused",
  ]);

  await cleanup();
});

add_task(async function test_enroll() {
  const recipe = NimbusTestUtils.factories.recipe.withFeatureConfig(
    "opt-in",
    { featureId: "nimbus-qa-1" },
    {
      isRollout: true,
      isFirefoxLabsOptIn: true,
    }
  );

  const { sandbox, manager, cleanup } = await setupTest({
    experiments: [recipe],
    init: false,
  });

  const enrollSpy = sandbox.spy(manager, "enroll");

  await ExperimentAPI.init();

  const labs = await FirefoxLabs.create();

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
    enrollSpy.calledOnceWithExactly(recipe, "rs-loader", {
      branchSlug: "control",
      originalPrefValues: undefined,
    }),
    "ExperimentManager.enroll called"
  );

  Assert.deepEqual(
    Glean.nimbusEvents.enrollmentStatus
      .testGetValue("nimbus-targeting-context")
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

  await cleanup();
});

add_task(async function test_reenroll() {
  const recipe = NimbusTestUtils.factories.recipe("opt-in", {
    isFirefoxLabsOptIn: true,
    isRollout: true,
  });

  const { manager, cleanup } = await setupTest({ experiments: [recipe] });

  const labs = await FirefoxLabs.create();

  Assert.strictEqual(
    typeof manager.store.get(recipe.slug),
    "undefined",
    `No enrollment for ${recipe.slug}`
  );

  await labs.enroll(recipe.slug, "control");
  Assert.ok(
    manager.store.get(recipe.slug)?.active,
    `Active enrollment for ${recipe.slug}`
  );

  labs.unenroll(recipe.slug);
  Assert.strictEqual(
    manager.store.get(recipe.slug)?.active,
    false,
    `Inactive enrollment for ${recipe.slug}`
  );

  await ExperimentAPI._rsLoader.updateRecipes();
  Assert.strictEqual(
    manager.store.get(recipe.slug)?.active,
    false,
    `Inactive enrollment for ${recipe.slug} after updateRecipes()`
  );

  await labs.enroll(recipe.slug, "control");
  Assert.ok(
    manager.store.get(recipe.slug)?.active,
    `Active enrollment for ${recipe.slug}`
  );

  labs.unenroll(recipe.slug);

  await cleanup();
});

add_task(async function test_unenroll() {
  const { manager, cleanup } = await setupTest({
    experiments: [
      NimbusTestUtils.factories.recipe.withFeatureConfig(
        "rollout",
        { featureId: "nimbus-qa-1" },
        { isRollout: true }
      ),
      NimbusTestUtils.factories.recipe.withFeatureConfig(
        "opt-in",
        { featureId: "nimbus-qa-2" },
        {
          isRollout: true,
          isFirefoxLabsOptIn: true,
        }
      ),
    ],
  });

  const labs = await FirefoxLabs.create();

  Assert.ok(manager.store.get("rollout")?.active, "Enrolled in rollout");
  Assert.strictEqual(
    typeof manager.store.get("opt-in"),
    "undefined",
    "Did not enroll in rollout"
  );

  await labs.enroll("opt-in", "control");
  Assert.ok(manager.store.get("opt-in")?.active, "Enrolled in opt-in");

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
      .testGetValue("nimbus-targeting-context")
      ?.map(ev => ev.extra),
    [
      {
        status: "Enrolled",
        slug: "opt-in",
        reason: "OptIn",
        branch: "control",
      },
      {
        branch: "control",
        reason: "OptOut",
        slug: "opt-in",
        status: "Disqualified",
      },
    ]
  );

  manager.unenroll("rollout");
  await cleanup();
});

add_task(async function test_reenroll_quickly() {
  const { cleanup } = await setupTest({
    experiments: [
      NimbusTestUtils.factories.recipe.withFeatureConfig(
        "optin",
        { featureId: "nimbus-qa-2" },
        {
          isRollout: true,
          isFirefoxLabsOptIn: true,
        }
      ),
    ],
  });

  const labs = await FirefoxLabs.create();

  Assert.equal(
    await NimbusTestUtils.queryEnrollment("optin"),
    null,
    "Enrollment does not exist"
  );

  info("Enrolling in optin");
  await labs.enroll("optin", "control");
  await NimbusTestUtils.flushStore();

  {
    const enrollment = await NimbusTestUtils.queryEnrollment("optin");
    Assert.ok(enrollment, "Enrollment exists in database");
    Assert.ok(enrollment.active, "Enrollment is active");
  }

  info("Unenrolling and re-enrolling");
  labs.unenroll("optin");
  await labs.enroll("optin", "control");
  await NimbusTestUtils.flushStore();

  {
    const enrollment = await NimbusTestUtils.queryEnrollment("optin");
    Assert.ok(enrollment, "Enrollment exists in database");
    Assert.ok(enrollment.active, "Enrollment is active");
  }

  labs.unenroll("optin");

  await cleanup();
});
