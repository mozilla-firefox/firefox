/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { MatchStatus } = ChromeUtils.importESModule(
  "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs"
);
const { TelemetryTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryTestUtils.sys.mjs"
);

const LOCALIZATIONS = {
  "en-US": {
    foo: "localized foo text",
    qux: "localized qux text",
    grault: "localized grault text",
    waldo: "localized waldo text",
  },
};

const DEEPLY_NESTED_VALUE = {
  foo: {
    $l10n: {
      id: "foo",
      comment: "foo comment",
      text: "original foo text",
    },
  },
  bar: {
    qux: {
      $l10n: {
        id: "qux",
        comment: "qux comment",
        text: "original qux text",
      },
    },
    quux: {
      grault: {
        $l10n: {
          id: "grault",
          comment: "grault comment",
          text: "orginal grault text",
        },
      },
      garply: "original garply text",
    },
    corge: "original corge text",
  },
  baz: "original baz text",
  waldo: [
    {
      $l10n: {
        id: "waldo",
        comment: "waldo comment",
        text: "original waldo text",
      },
    },
  ],
};

const LOCALIZED_DEEPLY_NESTED_VALUE = {
  foo: "localized foo text",
  bar: {
    qux: "localized qux text",
    quux: {
      grault: "localized grault text",
      garply: "original garply text",
    },
    corge: "original corge text",
  },
  baz: "original baz text",
  waldo: ["localized waldo text"],
};

const FEATURE_ID = "testfeature1";
const TEST_PREF_BRANCH = "testfeature1.";
const FEATURE = new ExperimentFeature(FEATURE_ID, {
  isEarlyStartup: false,
  variables: {
    foo: {
      type: "string",
      fallbackPref: `${TEST_PREF_BRANCH}foo`,
    },
    bar: {
      type: "json",
      fallbackPref: `${TEST_PREF_BRANCH}bar`,
    },
    baz: {
      type: "string",
      fallbackPref: `${TEST_PREF_BRANCH}baz`,
    },
    waldo: {
      type: "json",
      fallbackPref: `${TEST_PREF_BRANCH}waldo`,
    },
  },
});

add_setup(function setup() {
  Services.fog.initializeFOG();

  registerCleanupFunction(ExperimentTestUtils.addTestFeatures(FEATURE));
});

function setupTest({ ...args } = {}) {
  return NimbusTestUtils.setupTest({ ...args, clearTelemetry: true });
}

add_task(async function test_schema() {
  const recipe = ExperimentFakes.recipe("foo");

  info("Testing recipe without a localizations entry");
  await ExperimentTestUtils.validateExperiment(recipe);

  info("Testing recipe with a 'null' localizations entry");
  await ExperimentTestUtils.validateExperiment({
    ...recipe,
    localizations: null,
  });

  info("Testing recipe with a valid localizations entry");
  await ExperimentTestUtils.validateExperiment({
    ...recipe,
    localizations: LOCALIZATIONS,
  });

  info("Testing recipe with an invalid localizations entry");
  await Assert.rejects(
    ExperimentTestUtils.validateExperiment({
      ...recipe,
      localizations: [],
    }),
    /Experiment foo not valid/
  );
});

add_task(function test_substituteLocalizations() {
  Assert.equal(
    ExperimentFeature.substituteLocalizations("string", LOCALIZATIONS["en-US"]),
    "string",
    "String values should not be subsituted"
  );

  Assert.equal(
    ExperimentFeature.substituteLocalizations(
      {
        $l10n: {
          id: "foo",
          comment: "foo comment",
          text: "original foo text",
        },
      },
      LOCALIZATIONS["en-US"]
    ),
    "localized foo text",
    "$l10n objects should be substituted"
  );

  Assert.deepEqual(
    ExperimentFeature.substituteLocalizations(
      DEEPLY_NESTED_VALUE,
      LOCALIZATIONS["en-US"]
    ),
    LOCALIZED_DEEPLY_NESTED_VALUE,
    "Supports nested substitutions"
  );

  Assert.throws(
    () =>
      ExperimentFeature.substituteLocalizations(
        {
          foo: {
            $l10n: {
              id: "BOGUS",
              comment: "A variable with a missing id",
              text: "Original text",
            },
          },
        },
        LOCALIZATIONS["en-US"]
      ),
    ex => ex.reason === "l10n-missing-entry"
  );
});

add_task(async function test_getLocalizedValue() {
  const { manager, cleanup } = await setupTest();

  const experiment = ExperimentFakes.recipe("experiment", {
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [
          {
            featureId: FEATURE_ID,
            value: DEEPLY_NESTED_VALUE,
          },
        ],
      },
    ],
    localizations: LOCALIZATIONS,
  });

  const doExperimentCleanup =
    await ExperimentFakes.enrollmentHelper(experiment);

  const enrollment = manager.store.getExperimentForFeature(FEATURE_ID);

  Assert.deepEqual(
    FEATURE._getLocalizedValue(enrollment),
    LOCALIZED_DEEPLY_NESTED_VALUE,
    "_getLocalizedValue() for all values"
  );

  Assert.deepEqual(
    FEATURE._getLocalizedValue(enrollment, "foo"),
    LOCALIZED_DEEPLY_NESTED_VALUE.foo,
    "_getLocalizedValue() with a top-level localized variable"
  );

  Assert.deepEqual(
    FEATURE._getLocalizedValue(enrollment, "bar"),
    LOCALIZED_DEEPLY_NESTED_VALUE.bar,
    "_getLocalizedValue() with a nested localization"
  );

  doExperimentCleanup();
  cleanup();
});

add_task(async function test_getLocalizedValue_unenroll_missingEntry() {
  const { manager, cleanup } = await setupTest();

  const experiment = ExperimentFakes.recipe("experiment", {
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [
          {
            featureId: FEATURE_ID,
            value: {
              bar: {
                $l10n: {
                  id: "BOGUS",
                  comment: "Bogus localization",
                  text: "Original text",
                },
              },
            },
          },
        ],
      },
    ],
    localizations: LOCALIZATIONS,
  });

  await ExperimentFakes.enrollmentHelper(experiment);

  const enrollment = manager.store.getExperimentForFeature(FEATURE_ID);

  Assert.deepEqual(
    FEATURE._getLocalizedValue(enrollment),
    undefined,
    "_getLocalizedValue() with a bogus localization"
  );

  Assert.equal(
    manager.store.getExperimentForFeature(FEATURE_ID),
    null,
    "Experiment should be unenrolled"
  );

  const gleanEvents = Glean.nimbusEvents.unenrollment.testGetValue("events");
  Assert.equal(gleanEvents.length, 1, "Should be one unenrollment event");
  Assert.equal(
    gleanEvents[0].extra.reason,
    "l10n-missing-entry",
    "Reason should match"
  );
  Assert.equal(
    gleanEvents[0].extra.experiment,
    "experiment",
    "Slug should match"
  );

  TelemetryTestUtils.assertEvents(
    [
      {
        value: "experiment",
        extra: { reason: "l10n-missing-entry" },
      },
    ],
    {
      category: "normandy",
      method: "unenroll",
      object: "nimbus_experiment",
    }
  );

  cleanup();
});

add_task(async function test_getLocalizedValue_unenroll_missingEntry() {
  const { manager, cleanup } = await setupTest();

  await manager.onStartup();
  await manager.store.ready();

  const experiment = ExperimentFakes.recipe("experiment", {
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [
          {
            featureId: FEATURE_ID,
            value: {
              bar: {
                $l10n: {
                  id: "BOGUS",
                  comment: "Bogus localization",
                  text: "Original text",
                },
              },
            },
          },
        ],
      },
    ],
    localizations: {
      "en-CA": {},
    },
  });

  await ExperimentFakes.enrollmentHelper(experiment);

  const enrollment = manager.store.getExperimentForFeature(FEATURE_ID);

  Assert.deepEqual(
    FEATURE._getLocalizedValue(enrollment),
    undefined,
    "_getLocalizedValue() with a bogus localization"
  );

  Assert.equal(
    manager.store.getExperimentForFeature(FEATURE_ID),
    null,
    "Experiment should be unenrolled"
  );

  const gleanEvents = Glean.nimbusEvents.unenrollment.testGetValue("events");
  Assert.equal(gleanEvents.length, 1, "Should be one unenrollment event");
  Assert.equal(
    gleanEvents[0].extra.reason,
    "l10n-missing-locale",
    "Reason should match"
  );
  Assert.equal(
    gleanEvents[0].extra.experiment,
    "experiment",
    "Slug should match"
  );

  TelemetryTestUtils.assertEvents(
    [
      {
        value: "experiment",
        extra: { reason: "l10n-missing-locale" },
      },
    ],
    {
      category: "normandy",
      method: "unenroll",
      object: "nimbus_experiment",
    }
  );

  cleanup();
});

add_task(async function test_getVariables() {
  const { cleanup } = await setupTest();

  const experiment = ExperimentFakes.recipe("experiment", {
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [
          {
            featureId: FEATURE_ID,
            value: DEEPLY_NESTED_VALUE,
          },
        ],
      },
    ],
    localizations: LOCALIZATIONS,
  });

  const doExperimentCleanup =
    await ExperimentFakes.enrollmentHelper(experiment);

  Assert.deepEqual(
    FEATURE.getAllVariables(),
    LOCALIZED_DEEPLY_NESTED_VALUE,
    "getAllVariables() returns subsituted values"
  );

  Assert.equal(
    FEATURE.getVariable("foo"),
    LOCALIZED_DEEPLY_NESTED_VALUE.foo,
    "getVariable() returns a top-level substituted value"
  );

  Assert.deepEqual(
    FEATURE.getVariable("bar"),
    LOCALIZED_DEEPLY_NESTED_VALUE.bar,
    "getVariable() returns a nested substitution"
  );

  Assert.deepEqual(
    FEATURE.getVariable("baz"),
    DEEPLY_NESTED_VALUE.baz,
    "getVariable() returns non-localized variables unmodified"
  );

  Assert.deepEqual(
    FEATURE.getVariable("waldo"),
    LOCALIZED_DEEPLY_NESTED_VALUE.waldo,
    "getVariable() returns substitutions inside arrays"
  );

  doExperimentCleanup();
  cleanup();
});

add_task(async function test_getVariables_fallback() {
  const { cleanup } = await setupTest();

  Services.prefs.setStringPref(
    FEATURE.manifest.variables.foo.fallbackPref,
    "fallback-foo-pref-value"
  );
  Services.prefs.setStringPref(
    FEATURE.manifest.variables.baz.fallbackPref,
    "fallback-baz-pref-value"
  );

  const recipes = {
    experiment: ExperimentFakes.recipe("experiment", {
      branches: [
        {
          slug: "control",
          ratio: 1,
          features: [
            {
              featureId: FEATURE_ID,
              value: {
                foo: DEEPLY_NESTED_VALUE.foo,
              },
            },
          ],
        },
      ],
      localizations: {
        "en-US": {
          foo: LOCALIZATIONS["en-US"].foo,
        },
      },
    }),

    rollout: ExperimentFakes.recipe("rollout", {
      isRollout: true,
      branches: [
        {
          slug: "control",
          ratio: 1,
          features: [
            {
              featureId: FEATURE_ID,
              value: {
                bar: DEEPLY_NESTED_VALUE.bar,
              },
            },
          ],
        },
      ],
      localizations: {
        "en-US": {
          qux: LOCALIZATIONS["en-US"].qux,
          grault: LOCALIZATIONS["en-US"].grault,
        },
      },
    }),
  };

  const experimentCleanup = {};

  Assert.deepEqual(
    FEATURE.getAllVariables({ defaultValues: { waldo: ["default-value"] } }),
    {
      foo: "fallback-foo-pref-value",
      bar: null,
      baz: "fallback-baz-pref-value",
      waldo: ["default-value"],
    },
    "getAllVariables() returns only values from prefs and defaults"
  );

  Assert.equal(
    FEATURE.getVariable("foo"),
    "fallback-foo-pref-value",
    "variable foo returned from prefs"
  );
  Assert.equal(
    FEATURE.getVariable("bar"),
    undefined,
    "variable bar returned from rollout"
  );
  Assert.equal(
    FEATURE.getVariable("baz"),
    "fallback-baz-pref-value",
    "variable baz returned from prefs"
  );

  // Enroll in the rollout.
  experimentCleanup.rollout = await ExperimentFakes.enrollmentHelper(
    recipes.rollout
  );

  Assert.deepEqual(
    FEATURE.getAllVariables({ defaultValues: { waldo: ["default-value"] } }),
    {
      foo: "fallback-foo-pref-value",
      bar: LOCALIZED_DEEPLY_NESTED_VALUE.bar,
      baz: "fallback-baz-pref-value",
      waldo: ["default-value"],
    },
    "getAllVariables() returns subsituted values from the rollout"
  );

  Assert.equal(
    FEATURE.getVariable("foo"),
    "fallback-foo-pref-value",
    "variable foo returned from prefs"
  );
  Assert.deepEqual(
    FEATURE.getVariable("bar"),
    LOCALIZED_DEEPLY_NESTED_VALUE.bar,
    "variable bar returned from rollout"
  );
  Assert.equal(
    FEATURE.getVariable("baz"),
    "fallback-baz-pref-value",
    "variable baz returned from prefs"
  );

  // Enroll in the experiment.
  experimentCleanup.experiment = await ExperimentFakes.enrollmentHelper(
    recipes.experiment
  );

  Assert.deepEqual(
    FEATURE.getAllVariables({ defaultValues: { waldo: ["default-value"] } }),
    {
      foo: LOCALIZED_DEEPLY_NESTED_VALUE.foo,
      bar: null,
      baz: "fallback-baz-pref-value",
      waldo: ["default-value"],
    },
    "getAllVariables() returns subsituted values from the experiment"
  );

  Assert.equal(
    FEATURE.getVariable("foo"),
    LOCALIZED_DEEPLY_NESTED_VALUE.foo,
    "variable foo returned from experiment"
  );
  Assert.deepEqual(
    FEATURE.getVariable("bar"),
    LOCALIZED_DEEPLY_NESTED_VALUE.bar,
    "variable bar returned from rollout"
  );
  Assert.equal(
    FEATURE.getVariable("baz"),
    "fallback-baz-pref-value",
    "variable baz returned from prefs"
  );

  // Unenroll from the rollout so we are only enrolled in an experiment.
  await experimentCleanup.rollout();

  Assert.deepEqual(
    FEATURE.getAllVariables({ defaultValues: { waldo: ["default-value"] } }),
    {
      foo: LOCALIZED_DEEPLY_NESTED_VALUE.foo,
      bar: null,
      baz: "fallback-baz-pref-value",
      waldo: ["default-value"],
    },
    "getAllVariables() returns substituted values from the experiment"
  );

  Assert.equal(
    FEATURE.getVariable("foo"),
    LOCALIZED_DEEPLY_NESTED_VALUE.foo,
    "variable foo returned from experiment"
  );
  Assert.equal(
    FEATURE.getVariable("bar"),
    undefined,
    "variable bar is not set"
  );
  Assert.equal(
    FEATURE.getVariable("baz"),
    "fallback-baz-pref-value",
    "variable baz returned from prefs"
  );

  // Unenroll from experiment. We are enrolled in nothing.
  await experimentCleanup.experiment();

  Assert.deepEqual(
    FEATURE.getAllVariables({ defaultValues: { waldo: ["default-value"] } }),
    {
      foo: "fallback-foo-pref-value",
      bar: null,
      baz: "fallback-baz-pref-value",
      waldo: ["default-value"],
    },
    "getAllVariables() returns only values from prefs and defaults"
  );

  Assert.equal(
    FEATURE.getVariable("foo"),
    "fallback-foo-pref-value",
    "variable foo returned from prefs"
  );
  Assert.equal(
    FEATURE.getVariable("bar"),
    undefined,
    "variable bar returned from rollout"
  );
  Assert.equal(
    FEATURE.getVariable("baz"),
    "fallback-baz-pref-value",
    "variable baz returned from prefs"
  );

  Services.prefs.clearUserPref(FEATURE.manifest.variables.foo.fallbackPref);
  Services.prefs.clearUserPref(FEATURE.manifest.variables.baz.fallbackPref);

  cleanup();
});

add_task(async function test_getVariables_fallback_unenroll() {
  const { manager, cleanup } = await setupTest();

  Services.prefs.setStringPref(
    FEATURE.manifest.variables.foo.fallbackPref,
    "fallback-foo-pref-value"
  );
  Services.prefs.setStringPref(
    FEATURE.manifest.variables.bar.fallbackPref,
    `"fallback-bar-pref-value"`
  );
  Services.prefs.setStringPref(
    FEATURE.manifest.variables.baz.fallbackPref,
    "fallback-baz-pref-value"
  );
  Services.prefs.setStringPref(
    FEATURE.manifest.variables.waldo.fallbackPref,
    JSON.stringify(["fallback-waldo-pref-value"])
  );

  const recipes = [
    ExperimentFakes.recipe("experiment", {
      branches: [
        {
          slug: "control",
          ratio: 1,
          features: [
            {
              featureId: FEATURE_ID,
              value: {
                foo: DEEPLY_NESTED_VALUE.foo,
              },
            },
          ],
        },
      ],
      localizations: {},
    }),

    ExperimentFakes.recipe("rollout", {
      isRollout: true,
      branches: [
        {
          slug: "control",
          ratio: 1,
          features: [
            {
              featureId: FEATURE_ID,
              value: {
                bar: DEEPLY_NESTED_VALUE.bar,
              },
            },
          ],
        },
      ],
      localizations: {
        "en-US": {},
      },
    }),
  ];

  for (const recipe of recipes) {
    await ExperimentFakes.enrollmentHelper(recipe);
  }

  Assert.deepEqual(FEATURE.getAllVariables(), {
    foo: "fallback-foo-pref-value",
    bar: "fallback-bar-pref-value",
    baz: "fallback-baz-pref-value",
    waldo: ["fallback-waldo-pref-value"],
  });

  Assert.equal(
    manager.store.getExperimentForFeature(FEATURE_ID),
    null,
    "Experiment should be unenrolled"
  );

  Assert.equal(
    manager.store.getRolloutForFeature(FEATURE_ID),
    null,
    "Rollout should be unenrolled"
  );

  const gleanEvents = Glean.nimbusEvents.unenrollment.testGetValue("events");
  Assert.equal(gleanEvents.length, 2, "Should be two unenrollment events");
  Assert.equal(
    gleanEvents[0].extra.reason,
    "l10n-missing-locale",
    "Reason should match"
  );
  Assert.equal(
    gleanEvents[0].extra.experiment,
    "experiment",
    "Slug should match"
  );
  Assert.equal(
    gleanEvents[1].extra.reason,
    "l10n-missing-entry",
    "Reason should match"
  );
  Assert.equal(gleanEvents[1].extra.experiment, "rollout", "Slug should match");

  TelemetryTestUtils.assertEvents(
    [
      {
        value: "experiment",
        extra: { reason: "l10n-missing-locale" },
      },
      {
        value: "rollout",
        extra: { reason: "l10n-missing-entry" },
      },
    ],
    {
      category: "normandy",
      method: "unenroll",
      object: "nimbus_experiment",
    }
  );

  Services.prefs.clearUserPref(FEATURE.manifest.variables.foo.fallbackPref);
  Services.prefs.clearUserPref(FEATURE.manifest.variables.bar.fallbackPref);
  Services.prefs.clearUserPref(FEATURE.manifest.variables.baz.fallbackPref);
  Services.prefs.clearUserPref(FEATURE.manifest.variables.waldo.fallbackPref);

  cleanup();
});

add_task(async function test_updateRecipes() {
  const { sandbox, loader, manager, cleanup } = await setupTest();

  sandbox.stub(manager, "onRecipe");

  const recipe = ExperimentFakes.recipe("foo", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
    branches: [
      {
        slug: "control",
        features: [
          {
            featureId: FEATURE_ID,
            value: DEEPLY_NESTED_VALUE,
          },
        ],
        ratio: 1,
      },
    ],
    localizations: LOCALIZATIONS,
  });

  loader.remoteSettingsClients.experiments.get.resolves([recipe]);
  await loader.updateRecipes();

  Assert.ok(
    manager.onRecipe.calledOnceWith(recipe, "rs-loader", {
      ok: true,
      status: MatchStatus.TARGETING_AND_BUCKETING,
    }),
    "would enroll"
  );

  cleanup();
});

async function test_updateRecipes_missingLocale({
  featureValidationOptOut = false,
} = {}) {
  const { sandbox, loader, manager, cleanup } = await setupTest();

  const recipe = ExperimentFakes.recipe("foo", {
    bucketConfig: {
      ...ExperimentFakes.recipe.bucketConfig,
      count: 1000,
    },
    branches: [
      {
        slug: "control",
        features: [
          {
            featureId: FEATURE_ID,
            value: DEEPLY_NESTED_VALUE,
          },
        ],
        ratio: 1,
      },
    ],
    localizations: {},
    featureValidationOptOut,
  });

  sandbox.spy(manager, "onRecipe");
  sandbox.stub(manager, "enroll");
  loader.remoteSettingsClients.experiments.get.resolves([recipe]);

  await loader.updateRecipes();

  Assert.ok(
    manager.onRecipe.calledOnceWith(recipe, "rs-loader", {
      ok: false,
      reason: "l10n-missing-locale",
      locale: "en-US",
    }),
    "Called onRecipe with missing locale"
  );
  Assert.ok(manager.enroll.notCalled, "Did not enroll");

  const gleanEvents =
    Glean.nimbusEvents.validationFailed.testGetValue("events");
  Assert.equal(gleanEvents.length, 1, "Should be one validationFailed event");
  Assert.equal(
    gleanEvents[0].extra.experiment,
    "foo",
    "Experiment slug should match"
  );
  Assert.equal(
    gleanEvents[0].extra.reason,
    "l10n-missing-locale",
    "Reason should match"
  );
  Assert.equal(gleanEvents[0].extra.locale, "en-US", "Locale should match");

  TelemetryTestUtils.assertEvents(
    [
      {
        value: "foo",
      },
    ],
    {
      category: "normandy",
      method: "validationFailed",
      object: "nimbus_experiment",
    }
  );

  cleanup();
}

add_task(test_updateRecipes_missingLocale);

add_task(async function test_updateRecipes_missingEntry() {
  const { sandbox, loader, manager, cleanup } = await setupTest();

  const recipe = ExperimentFakes.recipe("foo", {
    branches: [
      {
        slug: "control",
        features: [
          {
            featureId: FEATURE_ID,
            value: DEEPLY_NESTED_VALUE,
          },
        ],
        ratio: 1,
      },
    ],
    localizations: {
      "en-US": {},
    },
  });

  sandbox.spy(manager, "onRecipe");
  sandbox.stub(manager, "enroll");
  loader.remoteSettingsClients.experiments.get.resolves([recipe]);

  await loader.updateRecipes();

  Assert.ok(
    manager.onRecipe.calledOnceWith(
      recipe,
      "rs-loader",
      sinon.match({
        ok: false,
        reason: "l10n-missing-entry",
        locale: "en-US",
        missingL10nIds: sinon.match.array.contains([
          "foo",
          "qux",
          "grault",
          "waldo",
        ]),
      })
    ),
    "Called onRecipe with missing l10n ids"
  );
  Assert.ok(manager.enroll.notCalled, "Did not enroll");

  const gleanEvents =
    Glean.nimbusEvents.validationFailed.testGetValue("events");
  Assert.equal(gleanEvents.length, 1, "Should be one validationFailed event");
  Assert.equal(
    gleanEvents[0].extra.experiment,
    "foo",
    "Experiment slug should match"
  );
  Assert.equal(
    gleanEvents[0].extra.reason,
    "l10n-missing-entry",
    "Reason should match"
  );
  Assert.equal(
    gleanEvents[0].extra.l10n_ids,
    "foo,qux,grault,waldo",
    "Missing IDs should match"
  );
  Assert.equal(gleanEvents[0].extra.locale, "en-US", "Locale should match");

  TelemetryTestUtils.assertEvents(
    [
      {
        value: "foo",
        extra: {
          reason: "l10n-missing-entry",
          locale: "en-US",
          l10n_ids: "foo,qux,grault,waldo",
        },
      },
    ],
    {
      category: "normandy",
      method: "validationFailed",
      object: "nimbus_experiment",
    }
  );

  cleanup();
});

add_task(async function test_updateRecipes_validationDisabled_pref() {
  Services.prefs.setBoolPref("nimbus.validation.enabled", false);

  await test_updateRecipes_missingLocale();

  Services.prefs.clearUserPref("nimbus.validation.enabled");
});

add_task(async function test_updateRecipes_validationDisabled_flag() {
  await test_updateRecipes_missingLocale({ featureValidationOptOut: true });
});

add_task(async function test_updateRecipes_unenroll_missingEntry() {
  const { sandbox, loader, manager, cleanup } = await setupTest();

  const recipe = ExperimentFakes.recipe("foo", {
    branches: [
      {
        slug: "control",
        features: [
          {
            featureId: FEATURE_ID,
            value: DEEPLY_NESTED_VALUE,
          },
        ],
        ratio: 1,
      },
    ],
    localizations: LOCALIZATIONS,
  });

  const badRecipe = { ...recipe, localizations: { "en-US": {} } };

  sandbox.spy(manager, "updateEnrollment");
  sandbox.spy(manager, "_unenroll");
  loader.remoteSettingsClients.experiments.get.resolves([badRecipe]);

  await manager.enroll(recipe, "rs-loader");
  Assert.ok(
    !!manager.store.getExperimentForFeature(FEATURE_ID),
    "Should be enrolled in the experiment"
  );

  await loader.updateRecipes();

  Assert.ok(
    manager.updateEnrollment.calledOnceWith(
      sinon.match({ slug: recipe.slug }),
      badRecipe,
      "rs-loader",
      sinon.match({
        ok: false,
        reason: "l10n-missing-entry",
        locale: "en-US",
        missingL10nIds: sinon.match.array.contains([
          "foo",
          "qux",
          "grault",
          "waldo",
        ]),
      })
    ),
    "Should call .onRecipe with the missing l10n entries"
  );

  Assert.ok(
    manager._unenroll.calledOnceWith(sinon.match({ slug: recipe.slug }), {
      reason: "l10n-missing-entry",
    })
  );

  Assert.equal(
    manager.store.getExperimentForFeature(FEATURE_ID),
    null,
    "Should no longer be enrolled in the experiment"
  );

  const unenrollEvents = Glean.nimbusEvents.unenrollment.testGetValue("events");
  Assert.equal(unenrollEvents.length, 1, "Should be one unenroll event");
  Assert.equal(
    unenrollEvents[0].extra.experiment,
    "foo",
    "Experiment slug should match"
  );
  Assert.equal(
    unenrollEvents[0].extra.reason,
    "l10n-missing-entry",
    "Reason should match"
  );

  const validationFailedEvents =
    Glean.nimbusEvents.validationFailed.testGetValue("events");
  Assert.equal(
    validationFailedEvents.length,
    1,
    "Should be one validation failed event"
  );
  Assert.equal(
    validationFailedEvents[0].extra.experiment,
    "foo",
    "Experiment slug should match"
  );
  Assert.equal(
    validationFailedEvents[0].extra.reason,
    "l10n-missing-entry",
    "Reason should match"
  );
  Assert.equal(
    validationFailedEvents[0].extra.l10n_ids,
    "foo,qux,grault,waldo",
    "Missing IDs should match"
  );
  Assert.equal(
    validationFailedEvents[0].extra.locale,
    "en-US",
    "Locale should match"
  );

  TelemetryTestUtils.assertEvents(
    [
      {
        value: "foo",
        extra: {
          reason: "l10n-missing-entry",
        },
      },
    ],
    {
      category: "normandy",
      method: "unenroll",
      object: "nimbus_experiment",
    },
    { clear: false }
  );

  TelemetryTestUtils.assertEvents(
    [
      {
        value: "foo",
        extra: {
          reason: "l10n-missing-entry",
          l10n_ids: "foo,qux,grault,waldo",
          locale: "en-US",
        },
      },
    ],
    {
      category: "normandy",
      method: "validationFailed",
      object: "nimbus_experiment",
    }
  );

  cleanup();
});

add_task(async function test_updateRecipes_unenroll_missingLocale() {
  const { sandbox, manager, loader, cleanup } = await setupTest();

  const recipe = ExperimentFakes.recipe("foo", {
    branches: [
      {
        slug: "control",
        features: [
          {
            featureId: FEATURE_ID,
            value: DEEPLY_NESTED_VALUE,
          },
        ],
        ratio: 1,
      },
    ],
    localizations: LOCALIZATIONS,
  });

  const badRecipe = { ...recipe, localizations: {} };

  sandbox.spy(manager, "updateEnrollment");
  sandbox.spy(manager, "_unenroll");
  loader.remoteSettingsClients.experiments.get.resolves([badRecipe]);

  await manager.enroll(recipe, "rs-loader");
  Assert.ok(
    !!manager.store.getExperimentForFeature(FEATURE_ID),
    "Should be enrolled in the experiment"
  );

  await loader.updateRecipes();

  Assert.ok(
    manager.updateEnrollment.calledOnceWith(
      sinon.match({ slug: recipe.slug }),
      badRecipe,
      "rs-loader",
      {
        ok: false,
        reason: "l10n-missing-locale",
        locale: "en-US",
      }
    ),
    "Should call .onFinal with missing-locale"
  );

  Assert.ok(
    manager._unenroll.calledWith(sinon.match({ slug: recipe.slug }), {
      reason: "l10n-missing-locale",
    })
  );

  Assert.equal(
    manager.store.getExperimentForFeature(FEATURE_ID),
    null,
    "Should no longer be enrolled in the experiment"
  );

  const unenrollEvents = Glean.nimbusEvents.unenrollment.testGetValue("events");
  Assert.equal(unenrollEvents.length, 1, "Should be one unenroll event");
  Assert.equal(
    unenrollEvents[0].extra.experiment,
    "foo",
    "Experiment slug should match"
  );
  Assert.equal(
    unenrollEvents[0].extra.reason,
    "l10n-missing-locale",
    "Reason should match"
  );

  const validationFailedEvents =
    Glean.nimbusEvents.validationFailed.testGetValue("events");
  Assert.equal(
    validationFailedEvents.length,
    1,
    "Should be one validation failed event"
  );
  Assert.equal(
    validationFailedEvents[0].extra.experiment,
    "foo",
    "Experiment slug should match"
  );
  Assert.equal(
    validationFailedEvents[0].extra.reason,
    "l10n-missing-locale",
    "Reason should match"
  );
  Assert.equal(
    validationFailedEvents[0].extra.locale,
    "en-US",
    "Locale should match"
  );

  TelemetryTestUtils.assertEvents(
    [
      {
        value: "foo",
        extra: {
          reason: "l10n-missing-locale",
        },
      },
    ],
    {
      category: "normandy",
      method: "unenroll",
      object: "nimbus_experiment",
    },
    { clear: false }
  );

  TelemetryTestUtils.assertEvents(
    [
      {
        value: "foo",
        extra: {
          reason: "l10n-missing-locale",
          locale: "en-US",
        },
      },
    ],
    {
      category: "normandy",
      method: "validationFailed",
      object: "nimbus_experiment",
    }
  );

  cleanup();
});

add_task(async function testCoenrolling() {
  const { manager, cleanup } = await setupTest();

  const featureId = "coenrolling-feature";

  const cleanupFeature = NimbusTestUtils.addTestFeatures(
    new ExperimentFeature(featureId, {
      allowCoenrollment: true,
      isEarlyStartup: false,
      variables: {
        foo: {
          type: "string",
          fallbackPref: `${TEST_PREF_BRANCH}.coenrolling.foo`,
        },
        bar: {
          type: "json",
        },
        baz: {
          type: "string",
        },
        waldo: {
          type: "json",
        },
      },
    })
  );

  Services.prefs.setStringPref(
    NimbusFeatures["coenrolling-feature"].getFallbackPrefName("foo"),
    "fallback-foo-pref-value"
  );

  await manager.enroll(
    NimbusTestUtils.factories.recipe.withFeatureConfig("experiment-1", {
      branchSlug: "treatment-a",
      featureId,
      value: {
        foo: "foo",
        bar: "bar",
        baz: "baz",
        waldo: "waldo",
      },
    })
  );

  await manager.enroll(
    NimbusTestUtils.factories.recipe.withFeatureConfig(
      "experiment-2",
      {
        branchSlug: "treatment-b",
        featureId,
        value: {
          foo: {
            $l10n: {
              id: "foo",
              comment: "foo comment",
              text: "original foo",
            },
          },
          bar: "bar",
          baz: "baz",
          waldo: "waldo",
        },
      },
      {
        localizations: LOCALIZATIONS,
      }
    )
  );

  await manager.enroll(
    NimbusTestUtils.factories.recipe.withFeatureConfig(
      "rollout-1",
      {
        featureId,
        value: {
          bar: DEEPLY_NESTED_VALUE.bar,
        },
      },
      {
        localizations: LOCALIZATIONS,
      }
    )
  );

  await manager.enroll(
    NimbusTestUtils.factories.recipe.withFeatureConfig(
      "rollout-2",
      {
        featureId,
        value: DEEPLY_NESTED_VALUE,
      },
      {
        localizations: LOCALIZATIONS,
      }
    )
  );

  const enrollments = NimbusFeatures[featureId]
    .getAllEnrollments()
    .sort((a, b) => a.meta.slug.localeCompare(b.meta.slug));

  Assert.deepEqual(enrollments, [
    {
      meta: {
        slug: "experiment-1",
        branch: "treatment-a",
        isRollout: false,
      },
      value: {
        foo: "foo",
        bar: "bar",
        baz: "baz",
        waldo: "waldo",
      },
    },
    {
      meta: {
        slug: "experiment-2",
        branch: "treatment-b",
        isRollout: false,
      },
      value: {
        foo: LOCALIZED_DEEPLY_NESTED_VALUE.foo,
        bar: "bar",
        baz: "baz",
        waldo: "waldo",
      },
    },
    {
      meta: {
        slug: "rollout-1",
        branch: "control",
        isRollout: false,
      },
      value: {
        foo: "fallback-foo-pref-value",
        bar: LOCALIZED_DEEPLY_NESTED_VALUE.bar,
      },
    },
    {
      meta: {
        slug: "rollout-2",
        branch: "control",
        isRollout: false,
      },
      value: LOCALIZED_DEEPLY_NESTED_VALUE,
    },
  ]);

  NimbusTestUtils.cleanupManager([
    "experiment-1",
    "experiment-2",
    "rollout-1",
    "rollout-2",
  ]);

  Services.prefs.clearUserPref(
    NimbusFeatures["coenrolling-feature"].getFallbackPrefName("foo")
  );

  cleanupFeature();
  cleanup();
});
