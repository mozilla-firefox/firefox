/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const {
  LABS_MIGRATION_FEATURE_MAP,
  MigrationError,
  NIMBUS_MIGRATION_PREF,
  NimbusMigrations,
} = ChromeUtils.importESModule("resource://nimbus/lib/Migrations.sys.mjs");

function mockLabsRecipes(targeting = "true") {
  return Object.entries(LABS_MIGRATION_FEATURE_MAP).map(([featureId, slug]) =>
    ExperimentFakes.recipe(slug, {
      isRollout: true,
      isFirefoxLabsOptIn: true,
      firefoxLabsTitle: `${featureId}-placeholder-title`,
      firefoxLabsDescription: `${featureId}-placeholder-desc`,
      firefoxLabsDescriptionLinks: null,
      firefoxLabsGroup: "placeholder",
      bucketConfig: {
        ...ExperimentFakes.recipe.bucketConfig,
        count: 1000,
      },
      branches: [
        {
          slug: "control",
          ratio: 1,
          features: [
            {
              featureId,
              value: {
                enabled: true,
              },
            },
          ],
        },
      ],
      targeting,
    })
  );
}

function getEnabledPrefForFeature(featureId) {
  return NimbusFeatures[featureId].manifest.variables.enabled.setPref.pref;
}

function removeExperimentManagerListeners(manager) {
  // This is a giant hack to remove pref listeners from the global ExperimentManager or an
  // ExperimentManager from a previous test (because the nsIObserverService holds a strong reference
  // to all these listeners);
  //
  // See https://bugzilla.mozilla.org/show_bug.cgi?id=1950237 for a long-term solution to this.
  Services.prefs.removeObserver(
    "datareporting.healthreport.uploadEnabled",
    manager
  );
  Services.prefs.removeObserver("app.shield.optoutstudies.enabled", manager);
}

add_setup(function setup() {
  Services.fog.initializeFOG();
  removeExperimentManagerListeners(ExperimentAPI._manager);
});

/**
 * Setup a test environment.
 *
 * @param {object} options
 * @param {number?} options.latestMigration
 *        The value that should be set for the latest Nimbus migration pref. If
 *        not provided, the pref will be unset.
 * @param {object[]} options.migrations
 *        An array of migrations that will replace the regular set of migrations
 *        for the duration of the test.
 * @param {object[]} options.recipes
 *        An array of experiment recipes that will be returned by the
 *        RemoteSettingsExperimentLoader for the duration of the test.
 * @param {object} options.args
 *       Options to pass to to NimbusTestutils.setupNimbusTest.
 */

async function setupTest({
  latestMigration,
  migrations,
  init = true,
  ...args
} = {}) {
  Assert.ok(
    !Services.prefs.prefHasUserValue(NIMBUS_MIGRATION_PREF),
    "migration pref should be unset"
  );

  const {
    initExperimentAPI,
    cleanup: baseCleanup,
    ...ctx
  } = await NimbusTestUtils.setupTest({
    init: false,
    clearTelemetry: true,
    ...args,
  });

  const { sandbox } = ctx;

  if (typeof latestMigration !== "undefined") {
    Services.prefs.setIntPref(NIMBUS_MIGRATION_PREF, latestMigration);
  }

  if (Array.isArray(migrations)) {
    sandbox.stub(NimbusMigrations, "MIGRATIONS").get(() => migrations);
  }

  if (init) {
    await initExperimentAPI();
  } else {
    ctx.initExperimentAPI = initExperimentAPI;
  }

  return {
    ...ctx,
    cleanup() {
      baseCleanup();
      removeExperimentManagerListeners(ctx.manager);
      Services.prefs.deleteBranch(NIMBUS_MIGRATION_PREF);
    },
  };
}

function makeMigrations(count) {
  const migrations = [];
  for (let i = 0; i < count; i++) {
    migrations.push(
      NimbusMigrations.migration(`test-migration-${i}`, sinon.stub())
    );
  }
  return migrations;
}

add_task(async function test_migration_unset() {
  info("Testing NimbusMigrations with no migration pref set");
  const migrations = makeMigrations(2);
  const { cleanup } = await setupTest({ migrations });

  Assert.ok(
    migrations[0].fn.calledOnce,
    `${migrations[0].name} should be called once`
  );
  Assert.ok(
    migrations[1].fn.calledOnce,
    `${migrations[1].name} should be called once`
  );
  Assert.equal(
    Services.prefs.getIntPref(NIMBUS_MIGRATION_PREF),
    1,
    "Migration pref should be updated"
  );

  Assert.deepEqual(
    Glean.nimbusEvents.migration.testGetValue().map(event => event.extra),
    [
      {
        success: "true",
        migration_id: migrations[0].name,
      },
      {
        success: "true",
        migration_id: migrations[1].name,
      },
    ]
  );

  cleanup();
});

add_task(async function test_migration_partially_done() {
  info("Testing NimbusMigrations with some migrations completed");
  const migrations = makeMigrations(2);
  const { cleanup } = await setupTest({ latestMigration: 0, migrations });

  Assert.ok(
    migrations[0].fn.notCalled,
    `${migrations[0].name} should not be called`
  );
  Assert.ok(
    migrations[1].fn.calledOnce,
    `${migrations[1].name} should be called once`
  );

  Assert.deepEqual(
    Glean.nimbusEvents.migration.testGetValue().map(event => event.extra),
    [
      {
        success: "true",
        migration_id: migrations[1].name,
      },
    ]
  );

  cleanup();
});

add_task(async function test_migration_throws() {
  info(
    "Testing NimbusMigrations with a migration that throws an unknown error"
  );
  const migrations = makeMigrations(3);
  migrations[1].fn.throws(new Error(`${migrations[1].name} failed`));
  const { cleanup } = await setupTest({ migrations });

  Assert.ok(
    migrations[0].fn.calledOnce,
    `${migrations[0].name} should be called once`
  );
  Assert.ok(
    migrations[1].fn.calledOnce,
    `${migrations[1].name} should be called once`
  );
  Assert.ok(
    migrations[2].fn.notCalled,
    `${migrations[2].name} should not be called`
  );

  Assert.equal(
    Services.prefs.getIntPref(NIMBUS_MIGRATION_PREF),
    0,
    "Migration pref should only be set to 0"
  );

  Assert.deepEqual(
    Glean.nimbusEvents.migration.testGetValue().map(event => event.extra),
    [
      {
        success: "true",
        migration_id: migrations[0].name,
      },
      {
        success: "false",
        migration_id: migrations[1].name,
        error_reason: MigrationError.Reason.UNKNOWN,
      },
    ]
  );

  cleanup();
});

add_task(async function test_migration_throws_MigrationError() {
  info(
    "Testing NimbusMigrations with a migration that throws a MigrationError"
  );
  const migrations = makeMigrations(3);
  migrations[1].fn.throws(new MigrationError("bogus"));
  const { cleanup } = await setupTest({ migrations });

  Assert.ok(
    migrations[0].fn.calledOnce,
    `${migrations[0].name} should be called once`
  );
  Assert.ok(
    migrations[1].fn.calledOnce,
    `${migrations[1].name} should be called once`
  );
  Assert.ok(
    migrations[2].fn.notCalled,
    `${migrations[2].name} should not be called`
  );

  Assert.equal(
    Services.prefs.getIntPref(NIMBUS_MIGRATION_PREF),
    0,
    "Migration pref should only be set to 0"
  );

  Assert.deepEqual(
    Glean.nimbusEvents.migration.testGetValue().map(event => event.extra),
    [
      {
        success: "true",
        migration_id: migrations[0].name,
      },
      {
        success: "false",
        migration_id: migrations[1].name,
        error_reason: "bogus",
      },
    ]
  );

  cleanup();
});

add_task(async function test_migration_firefoxLabsEnrollments() {
  async function doTest(features) {
    info(
      `Testing NimbusMigrations migrates Firefox Labs features ${JSON.stringify(features)}`
    );
    const prefs = features.map(getEnabledPrefForFeature);
    for (const pref of prefs) {
      Services.prefs.setBoolPref(pref, true);
    }

    const { manager, cleanup } = await setupTest({
      experiments: mockLabsRecipes("true"),
    });

    Assert.deepEqual(
      await manager
        .getAllOptInRecipes()
        .then(recipes => recipes.map(recipe => recipe.slug).toSorted()),
      Object.values(LABS_MIGRATION_FEATURE_MAP).toSorted(),
      "The labs recipes should be available"
    );

    for (const [feature, slug] of Object.entries(LABS_MIGRATION_FEATURE_MAP)) {
      const enrollmentExpected = features.includes(feature);
      const metadata = ExperimentAPI.getRolloutMetaData({ slug });

      if (enrollmentExpected) {
        Assert.ok(!!metadata, `There should be an enrollment for slug ${slug}`);

        const pref = getEnabledPrefForFeature(feature);
        Assert.equal(
          Services.prefs.getBoolPref(pref),
          true,
          `Pref ${pref} should be set after enrollment`
        );

        manager.unenroll(slug);
        Assert.equal(
          Services.prefs.getBoolPref(pref),
          false,
          `Pref ${pref} should be unset after unenrollment`
        );
        Assert.ok(
          !Services.prefs.prefHasUserValue(pref),
          `Pref ${pref} should not be set on the user branch`
        );
      } else {
        Assert.ok(
          !metadata,
          `There should not be an enrollment for slug ${slug}`
        );
      }
    }

    Assert.deepEqual(
      Glean.nimbusEvents.migration.testGetValue().map(event => event.extra),
      [
        {
          success: "true",
          migration_id: "firefox-labs-enrollments",
        },
      ]
    );

    cleanup();
  }

  await doTest([]);

  for (const feature of Object.keys(LABS_MIGRATION_FEATURE_MAP)) {
    await doTest([feature]);
  }

  await doTest(Object.keys(LABS_MIGRATION_FEATURE_MAP));
});

add_task(async function test_migration_firefoxLabsEnrollments_falseTargeting() {
  // Some of the features will be limited to specific channels.
  // We don't need to test that targeting evaluation itself works, so we'll just
  // test with hardcoded targeting.
  info(
    `Testing NimbusMigration does not migrate Firefox Labs features when targeting is false`
  );
  const prefs = Object.keys(LABS_MIGRATION_FEATURE_MAP).map(
    getEnabledPrefForFeature
  );
  for (const pref of prefs) {
    Services.prefs.setBoolPref(pref, true);
  }
  const { manager, cleanup } = await setupTest({
    experiments: mockLabsRecipes("false"),
  });

  Assert.deepEqual(
    await manager.getAllOptInRecipes(),
    [],
    "There should be no opt-in recipes"
  );

  for (const pref of prefs) {
    Assert.ok(
      Services.prefs.getBoolPref(pref),
      `Pref ${pref} should be unchanged`
    );

    Services.prefs.clearUserPref(pref);
  }

  for (const slug of Object.values(LABS_MIGRATION_FEATURE_MAP)) {
    Assert.ok(
      typeof manager.store.get(slug) === "undefined",
      `There should be no store entry for ${slug}`
    );
  }

  Assert.deepEqual(
    Glean.nimbusEvents.migration.testGetValue().map(event => event.extra),
    [
      {
        success: "true",
        migration_id: "firefox-labs-enrollments",
      },
    ]
  );

  cleanup();
});

add_task(async function test_migration_firefoxLabsEnrollments_idempotent() {
  info("Testing the firefox-labs-enrollments migration is idempotent");

  const prefs = Object.keys(LABS_MIGRATION_FEATURE_MAP).map(
    getEnabledPrefForFeature
  );

  for (const pref of prefs) {
    Services.prefs.setBoolPref(pref, true);
  }

  const recipes = mockLabsRecipes("true");

  // Get the store into a partially migrated state (i.e., we have enrolled in at least one
  // experiment but the migration pref has not updated).
  {
    const manager = ExperimentFakes.manager();
    await manager.onStartup();

    manager.enroll(recipes[0], "rs-loader", { branchSlug: "control" });

    await NimbusTestUtils.saveStore(manager.store);

    removeExperimentManagerListeners(manager);
    removePrefObservers(manager);
    assertNoObservers(manager);
  }

  const { manager, cleanup } = await setupTest({
    experiments: recipes,
  });

  Assert.equal(
    Services.prefs.getIntPref(NIMBUS_MIGRATION_PREF),
    0,
    "Migration pref updated"
  );
  Assert.deepEqual(
    Glean.nimbusEvents.migration.testGetValue().map(ev => ev.extra),
    [
      {
        migration_id: "firefox-labs-enrollments",
        success: "true",
      },
    ]
  );

  for (const { slug } of recipes) {
    manager.unenroll(slug);
  }

  cleanup();

  for (const pref of prefs) {
    Services.prefs.clearUserPref(pref);
  }
});
