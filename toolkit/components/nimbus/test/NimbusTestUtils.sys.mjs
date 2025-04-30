/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  ExperimentAPI,
  NimbusFeatures,
} from "resource://nimbus/ExperimentAPI.sys.mjs";
import { ExperimentStore } from "resource://nimbus/lib/ExperimentStore.sys.mjs";
import { FileTestUtils } from "resource://testing-common/FileTestUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  FeatureManifest: "resource://nimbus/FeatureManifest.sys.mjs",
  JsonSchema: "resource://gre/modules/JsonSchema.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  NormandyUtils: "resource://normandy/lib/NormandyUtils.sys.mjs",
  _ExperimentManager: "resource://nimbus/lib/ExperimentManager.sys.mjs",
  _RemoteSettingsExperimentLoader:
    "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

function fetchSchemaSync(uri) {
  // Yes, this is doing a sync load, but this is only done *once* and we cache
  // the result after *and* it is test-only.
  const channel = lazy.NetUtil.newChannel({
    uri,
    loadUsingSystemPrincipal: true,
  });
  const stream = Cc["@mozilla.org/scriptableinputstream;1"].createInstance(
    Ci.nsIScriptableInputStream
  );

  stream.init(channel.open());

  const available = stream.available();
  const json = stream.read(available);
  stream.close();

  return JSON.parse(json);
}

ChromeUtils.defineLazyGetter(lazy, "enrollmentSchema", () => {
  return fetchSchemaSync(
    "resource://nimbus/schemas/NimbusEnrollment.schema.json"
  );
});

const { SYNC_DATA_PREF_BRANCH, SYNC_DEFAULTS_PREF_BRANCH } = ExperimentStore;

async function fetchSchema(url) {
  const response = await fetch(url);
  const schema = await response.json();
  if (!schema) {
    throw new Error(`Failed to load ${url}`);
  }
  return schema;
}

function validateSchema(schema, value, errorMsg) {
  const result = lazy.JsonSchema.validate(value, schema, {
    shortCircuit: false,
  });
  if (result.errors.length) {
    throw new Error(
      `${errorMsg}: ${JSON.stringify(result.errors, undefined, 2)}`
    );
  }
  return value;
}

function validateFeatureValueEnum({ branch }) {
  let { features } = branch;
  for (let feature of features) {
    // If we're not using a real feature skip this check
    if (!lazy.FeatureManifest[feature.featureId]) {
      return;
    }
    let { variables } = lazy.FeatureManifest[feature.featureId];
    for (let varName of Object.keys(variables)) {
      let varValue = feature.value[varName];
      if (
        varValue &&
        variables[varName].enum &&
        !variables[varName].enum.includes(varValue)
      ) {
        throw new Error(
          `${varName} should have one of the following values: ${JSON.stringify(
            variables[varName].enum
          )} but has value '${varValue}'`
        );
      }
    }
  }
}

export const ExperimentTestUtils = {
  validateExperiment(experiment) {
    return NimbusTestUtils.validateExperiment(experiment);
  },
  validateEnrollment(enrollment) {
    return NimbusTestUtils.validateEnrollment(enrollment);
  },
  addTestFeatures(...features) {
    return NimbusTestUtils.addTestFeatures(...features);
  },
};
export const ExperimentFakes = {
  manager(store) {
    return NimbusTestUtils.stubs.manager(store);
  },
  store(path) {
    return NimbusTestUtils.stubs.store(path);
  },
  enrollWithFeatureConfig(...args) {
    return NimbusTestUtils.enrollWithFeatureConfig(...args);
  },
  enrollmentHelper(...args) {
    return NimbusTestUtils.enroll(...args);
  },
  cleanupAll(...args) {
    return NimbusTestUtils.cleanupManager(...args);
  },
  cleanupStorePrefCache() {
    return NimbusTestUtils.cleanupStorePrefCache();
  },
  rsLoader() {
    return NimbusTestUtils.stubs.rsLoader();
  },
  experiment(slug, props = {}) {
    return {
      slug,
      active: true,
      branch: {
        slug: "treatment",
        ratio: 1,
        features: [
          {
            featureId: "testFeature",
            value: { testInt: 123, enabled: true },
          },
        ],
        ...props,
      },
      source: "NimbusTestUtils",
      isEnrollmentPaused: true,
      experimentType: "NimbusTestUtils experiment",
      userFacingName: "NimbusTestUtils experiment",
      userFacingDescription: "NimbusTestUtils",
      lastSeen: new Date().toJSON(),
      featureIds: props?.branch?.features?.map(f => f.featureId) || [
        "testFeature",
      ],
      ...props,
    };
  },
  rollout(slug, props = {}) {
    return {
      slug,
      active: true,
      isRollout: true,
      branch: {
        slug: "treatment",
        ratio: 1,
        features: [
          {
            featureId: "testFeature",
            value: { testInt: 123, enabled: true },
          },
        ],
        ...props,
      },
      source: "NimbusTestUtils",
      isEnrollmentPaused: true,
      experimentType: "rollout",
      userFacingName: "NimbusTestUtils rollout",
      userFacingDescription: "NimbusTestUtils rollout",
      lastSeen: new Date().toJSON(),
      featureIds: (props?.branch?.features || props?.features)?.map(
        f => f.featureId
      ) || ["testFeature"],
      ...props,
    };
  },
  recipe(slug = lazy.NormandyUtils.generateUuid(), props = {}) {
    return NimbusTestUtils.factories.recipe(slug, {
      bucketConfig: ExperimentFakes.recipe.bucketConfig,
      ...props,
    });
  },
};
Object.defineProperty(ExperimentFakes.recipe, "bucketConfig", {
  get() {
    return {
      namespace: "nimbus-test-utils",
      randomizationUnit: "normandy_id",
      start: 0,
      count: 100,
      total: 1000,
    };
  },
});

Object.defineProperty(ExperimentFakes.recipe, "branches", {
  get() {
    return NimbusTestUtils.factories.recipe.branches;
  },
});

let _testSuite = null;

export const NimbusTestUtils = {
  init(testCase) {
    _testSuite = testCase;

    Object.defineProperty(NimbusTestUtils, "Assert", {
      configurable: true,
      get: () => _testSuite.Assert,
    });
  },

  get Assert() {
    // This gets replaced in NimbusTestUtils.init().
    throw new Error("You must call NimbusTestUtils.init(this)");
  },

  assert: {
    /**
     * Assert that the store has no active enrollments and then clean up the
     * store.
     *
     * This function will also clean up the isEarlyStartup cache.
     *
     * @param {object} store
     *        The `ExperimentStore`.
     */
    storeIsEmpty(store) {
      NimbusTestUtils.Assert.deepEqual(
        store
          .getAll()
          .filter(e => e.active)
          .map(e => e.slug),
        [],
        "Store should have no active enrollments"
      );

      store
        .getAll()
        .filter(e => !e.active)
        .forEach(e => store._deleteForTests(e.slug));

      NimbusTestUtils.Assert.deepEqual(
        store
          .getAll()
          .filter(e => !e.active)
          .map(e => e.slug),
        [],
        "Store should have no inactive enrollments"
      );

      NimbusTestUtils.cleanupStorePrefCache();
    },
  },

  factories: {
    /**
     * Create a experiment enrollment for an `ExperimentStore`.
     *
     * @param {string} slug
     *        The slug for the created enrollment.
     *
     * @param {object?} props
     *        Additional properties to splat into the created enrollment.
     */
    experiment(slug, props = {}) {
      const { isRollout = false } = props;

      const experimentType = isRollout ? "rollout" : "experiment";
      const userFacingName = `NimbusTestUtils ${experimentType}`;
      const userFacingDescription = `NimbusTestUtils ${experimentType}`;

      return {
        slug,
        active: true,
        branch: {
          slug: "treatment",
          ratio: 1,
          features: [
            {
              featureId: "testFeature",
              value: { testInt: 123, enabled: true },
            },
          ],
        },
        source: "NimbusTestUtils",
        isEnrollmentPaused: true,
        experimentType,
        userFacingName,
        userFacingDescription,
        lastSeen: new Date().toJSON(),
        featureIds: props?.branch?.features?.map(f => f.featureId) ?? [
          "testFeature",
        ],
        ...props,
      };
    },

    /**
     * Create a rollout enrollment for an `ExperimentStore`.
     *
     * @param {string} slug
     *        The slug for the created enrollment.
     *
     * @param {object?} props
     *        Additional properties to splat into the created enrollment.
     */
    rollout(slug, props = {}) {
      return NimbusTestUtils.factories.experiment(slug, {
        ...props,
        isRollout: true,
      });
    },

    /**
     * Create a recipe.
     *
     * @param {string} slug
     *        The slug for the created recipe.
     *
     * @param {object?} props
     *        Additional properties to splat into to the
     */
    recipe(slug, props = {}) {
      return {
        id: slug,
        schemaVersion: "1.7.0",
        appName: "firefox_desktop",
        appId: "firefox-desktop",
        channel: "nightly",
        slug,
        isEnrollmentPaused: false,
        probeSets: [],
        startDate: null,
        endDate: null,
        proposedEnrollment: 7,
        referenceBranch: "control",
        application: "firefox-desktop",
        branches: props?.isRollout
          ? [NimbusTestUtils.factories.recipe.branches[0]]
          : NimbusTestUtils.factories.recipe.branches,
        bucketConfig: NimbusTestUtils.factories.recipe.bucketConfig,
        userFacingName: "NimbusTestUtils recipe",
        userFacingDescription: "NimbusTestUtils recipe",
        featureIds: props?.branches?.[0].features?.map(f => f.featureId) || [
          "testFeature",
        ],
        targeting: "true",
        isRollout: false,
        ...props,
      };
    },
  },

  stubs: {
    store(path) {
      return new ExperimentStore("ExperimentStoreData", {
        path: path ?? FileTestUtils.getTempFile("test-experiment-store").path,
      });
    },

    manager(store) {
      const manager = new lazy._ExperimentManager({
        store: store ?? NimbusTestUtils.stubs.store(),
      });
      const addEnrollment = manager.store.addEnrollment.bind(manager.store);

      // We want calls to `store.addEnrollment` to implicitly validate the
      // enrollment before saving to store
      lazy.sinon.stub(manager.store, "addEnrollment").callsFake(enrollment => {
        ExperimentTestUtils.validateEnrollment(enrollment);
        return addEnrollment(enrollment);
      });

      return manager;
    },

    rsLoader(manager) {
      const loader = new lazy._RemoteSettingsExperimentLoader(
        manager ?? NimbusTestUtils.stubs.manager()
      );

      Object.defineProperties(loader.remoteSettingsClients, {
        experiments: {
          value: {
            collectionName: "nimbus-desktop-experiments (stubbed)",
            get: () => Promise.resolve([]),
          },
        },

        secureExperiments: {
          value: {
            collectionName: "nimbus-secure-experiments (stubbed)",
            get: () => Promise.resolve([]),
          },
        },
      });

      return loader;
    },
  },

  /**
   * Add features for tests.
   *
   * NB: These features will only be visible to the JS Nimbus client. The native
   * Nimbus client will have no access.
   *
   * @params {...object} features
   *         A list of `_NimbusFeature`s.
   *
   * @returns {function(): void}
   *          A cleanup function to remove the features once the test has completed.
   */
  addTestFeatures(...features) {
    for (const feature of features) {
      if (Object.hasOwn(NimbusFeatures, feature.featureId)) {
        throw new Error(
          `Cannot add feature ${feature.featureId} -- a feature with this ID already exists!`
        );
      }
      NimbusFeatures[feature.featureId] = feature;
    }
    return () => {
      for (const { featureId } of features) {
        delete NimbusFeatures[featureId];
      }
    };
  },

  /**
   * Unenroll from all the given slugs and assert that the store is now empty.
   *
   * @params {string[]} slugs
   *         The slugs to unenroll from.
   *
   * @params {object?} options
   *
   * @params {object?} options.manager
   *         The ExperimentManager to clean up. Defaults to the global
   *         ExperimentManager.
   */
  cleanupManager(slugs, { manager = ExperimentAPI._manager } = {}) {
    for (const slug of slugs) {
      manager.unenroll(slug);
    }

    NimbusTestUtils.assert.storeIsEmpty(manager.store);
  },

  /**
   * Cleanup any isEarlyStartup features cached in prefs.
   */
  cleanupStorePrefCache() {
    // These may throw if nothing is cached, but it is harmless.

    try {
      Services.prefs.deleteBranch(SYNC_DATA_PREF_BRANCH);
    } catch (e) {}
    try {
      Services.prefs.deleteBranch(SYNC_DEFAULTS_PREF_BRANCH);
    } catch (e) {}
  },

  /**
   * Enroll in the given recipe.
   *
   * @param {object} recipe
   *        The recipe to enroll in.
   *
   * @param {object?} options
   *
   * @param {object?} options.manager
   *        The ExperimentManager to use for enrollment. If not provided, the
   *        global ExperimentManager will be used.
   *
   * @param {string?} options.source
   *        The source to attribute to the enrollment.
   *
   * @returns {Promise<function(): void>}
   *          A cleanup function that will unenroll from the enrolled recipe and
   *          remove it from the store.
   */
  async enroll(
    recipe,
    { manager = ExperimentAPI._manager, source = "nimbus-test-utils" } = {}
  ) {
    if (!recipe?.slug) {
      throw new Error("Experiment with slug is required");
    }

    const enrollment = await manager.enroll(recipe, source);
    manager.store._syncToChildren({ flush: true });

    return function doEnrollmentCleanup() {
      manager.unenroll(enrollment.slug);
      manager.store._deleteForTests(enrollment.slug);
    };
  },

  /**
   * Enroll in an automatically-generated recipe with the given feature
   * configuration.
   *
   * @param {object} featureConfig
   *
   * @param {string} featureConfig.featureId
   *                 The name of the feature.
   *
   * @param {object} featureConfig.value
   *                 The feature value.
   *
   * @param {object?} options
   *
   * @param {object?} options.manager
   *        The ExperimentManager to use for enrollment. If not provided, the
   *        global ExperimentManager will be used.
   *
   * @param {string?} options.source
   *        The source to attribute to the enrollment.
   *
   * @param {branchSlug?} options.slug
   *        The slug to use for the recipe. If not provided one will be
   *        generated based on `featureId`.
   *
   * @param {string?} options.branchSlug
   *        The slug to use for the enrolled branch. Defaults to "control".
   *
   * @param {boolean?} options.isRollout
   *        If true, the enrolled recipe will be a rollout.
   *
   * @returns {Promise<function(): void>}
   *          A cleanup function that will unenroll from the enrolled recipe and
   *          remove it from the store.
   */
  async enrollWithFeatureConfig(
    featureConfig,
    {
      manager = ExperimentAPI._manager,
      source,
      slug,
      branchSlug = "control",
      isRollout = false,
    } = {}
  ) {
    await manager.store.ready();

    const experimentId =
      slug ??
      `${featureConfig.featureId}-${
        isRollout ? "rollout" : "experiment"
      }-${Math.random()}`;

    const recipe = NimbusTestUtils.factories.recipe(experimentId, {
      bucketConfig: {
        ...NimbusTestUtils.factories.recipe.bucketConfig,
        count: 1000,
      },
      branches: [
        {
          slug: branchSlug,
          ratio: 1,
          features: [featureConfig],
        },
      ],
      isRollout,
    });

    return NimbusTestUtils.enroll(recipe, {
      manager,
      source,
    });
  },

  /**
   * Remove the ExperimentStore file.
   *
   * If the store contains active enrollments this function will cause the test
   * to fail.
   *
   * @params {ExperimentStore} store
   *         The store to delete.
   */
  async removeStore(store) {
    NimbusTestUtils.assert.storeIsEmpty(store);

    // Prevent the next save from happening.
    store._store._saver.disarm();

    // If we're too late to stop the save from happening then we need to wait
    // for it to finish. Otherwise the saver might recreate the file on disk
    // after we delete it.
    if (store._store._saver.isRunning) {
      await store._store._saver._runningPromise;
    }

    await IOUtils.remove(store._store.path);
  },

  /**
   * Save the store to disk
   *
   * @param {ExperimentStore} store
   *        The store to save.
   *
   * @returns {string} The path to the file on disk.
   */
  async saveStore(store) {
    const jsonFile = store._store;

    if (jsonFile._saver.isRunning) {
      // It is possible that the store has been updated since we started writing
      // to disk. If we've already started writing, wait for that to finish.
      await jsonFile._saver._runningPromise;
    } else if (jsonFile._saver.isArmed) {
      // Otherwise, if we have a pending write we cancel it.
      jsonFile._saver.disarm();
    }

    await jsonFile._save();

    return store._store.path;
  },

  /**
   * @typedef {object} TestContext
   *
   * @property {object} sandbox
   *           A sinon sandbox.
   *
   * @property {_RemoteSettingsExperimentLoader} loader
   *           A RemoteSettingsExperimentLoader instance that has stubbed
   *           RemoteSettings clients.
   *
   * @property {_ExperimentManager} maanger
   *           An ExperimentManager instance that will validate all enrollments
   *           added to its store.
   *
   * @property {(function(): void)?} initExperimentAPI
   *           A function that will complete ExperimentAPI initialization.
   *
   * @property {function(): void} cleanup
   *           A cleanup function that should be called at the end of the test.
   */

  /**
   * Set a Nimbus testing environment.
   *
   * This is intended to be used inside xpcshell tests -- browser mochitests
   * already have a Nimbus environment.
   *
   * @param {object?} options
   *
   * @param {boolean?} options.init
   *        Initialize the Experiment API.
   *
   *        If false, the returned context will return an `initExperimentAPI` member that
   *        will complete the initialization.
   *
   * @param {string?} options.storePath
   *        An optional path to an existing ExperimentStore to use for the
   *        ExperimentManager.
   *
   * @param {object[]?} options.experiments
   *        If provided, these recipes will be returned by the RemoteSettings
   *        experiments client.
   *
   * @param {object[]?} options.secureExperiments
   *        If provided, these recipes will be returned by the RemoteSetings
   *        secureExperiments client.
   *
   * @param {boolean?} clearTelemetry
   *        If true, telemetry will be reset in the cleanup function.
   *
   * @returns {TestContext}
   *          Everything you need to write a test using Nimbus.
   */
  async setupTest({
    init = true,
    storePath,
    experiments,
    secureExperiments,
    clearTelemetry = false,
  } = {}) {
    const sandbox = lazy.sinon.createSandbox();

    const store = NimbusTestUtils.stubs.store(storePath);
    const manager = NimbusTestUtils.stubs.manager(store);
    const loader = NimbusTestUtils.stubs.rsLoader(manager);

    sandbox.stub(ExperimentAPI, "_rsLoader").get(() => loader);
    sandbox.stub(ExperimentAPI, "_manager").get(() => manager);
    sandbox
      .stub(loader.remoteSettingsClients.experiments, "get")
      .resolves(Array.isArray(experiments) ? experiments : []);
    sandbox
      .stub(loader.remoteSettingsClients.secureExperiments, "get")
      .resolves(Array.isArray(secureExperiments) ? secureExperiments : []);

    const ctx = {
      sandbox,
      loader,
      manager,
      cleanup() {
        NimbusTestUtils.assert.storeIsEmpty(manager.store);
        ExperimentAPI._resetForTests();
        sandbox.restore();

        if (clearTelemetry) {
          Services.fog.testResetFOG();
          Services.telemetry.clearEvents();
        }
      },
    };

    const initExperimentAPI = () => ExperimentAPI.init();

    if (init) {
      await initExperimentAPI();
    } else {
      ctx.initExperimentAPI = initExperimentAPI;
    }

    return ctx;
  },

  /**
   * Validate an enrollment matches the Nimbus enrollment schema.
   *
   * @params {object} enrollment
   *         The enrollment to validate.
   *
   * @throws If the enrollment does not validate or its feature configurations
   *         contain invalid enum variants.
   */
  validateEnrollment(enrollment) {
    // We still have single feature experiment recipes for backwards
    // compatibility testing but we don't do schema validation
    if (!enrollment.branch.features && enrollment.branch.feature) {
      return;
    }

    validateFeatureValueEnum(enrollment);
    validateSchema(
      lazy.enrollmentSchema,
      enrollment,
      `Enrollment ${enrollment.slug} is not valid`
    );
  },

  /**
   * Validate the experiment matches the Nimbus experiment schema.
   *
   * @param {object} experiment
   *        The experiment to validate.
   *
   * @throws If the experiment does not validate or it includes unknown feature
   *         IDs.
   */
  async validateExperiment(experiment) {
    const schema = await fetchSchema(
      "resource://nimbus/schemas/NimbusExperiment.schema.json"
    );

    // Ensure that the `featureIds` field is properly set
    const { branches } = experiment;
    branches.forEach(branch => {
      branch.features.map(({ featureId }) => {
        if (!experiment.featureIds.includes(featureId)) {
          throw new Error(
            `Branch(${branch.slug}) contains feature(${featureId}) but that's not declared in recipe(${experiment.slug}).featureIds`
          );
        }
      });
    });

    validateSchema(
      schema,
      experiment,
      `Experiment ${experiment.slug} not valid`
    );
  },
};

Object.defineProperties(NimbusTestUtils.factories.experiment, {
  withFeatureConfig: {
    value: function NimbusTestUtils_factories_experiment_withFeatureConfig(
      slug,
      { branchSlug = "control", featureId, value = {} } = {},
      props = {}
    ) {
      return NimbusTestUtils.factories.experiment(slug, {
        branch: {
          slug: branchSlug,
          ratio: 1,
          features: [
            {
              featureId,
              value,
            },
          ],
        },
        ...props,
      });
    },
  },
});

Object.defineProperties(NimbusTestUtils.factories.rollout, {
  withFeatureConfig: {
    value: function NimbusTestUtils_factories_rollout_withFeatureConfig(
      slug,
      { branchSlug = "control", featureId, value = {} } = {},
      props = {}
    ) {
      return NimbusTestUtils.factories.rollout(slug, {
        branch: {
          slug: branchSlug,
          ratio: 1,
          features: [
            {
              featureId,
              value,
            },
          ],
        },
        ...props,
      });
    },
  },
});

Object.defineProperties(NimbusTestUtils.factories.recipe, {
  bucketConfig: {
    /**
     * A helper for generating valid bucketing configurations.
     *
     * This bucketing configuration will always result in enrollment.
     */
    get() {
      return {
        namespace: "nimbus-test-utils",
        randomizationUnit: "normandy_id",
        start: 0,
        count: 1000,
        total: 1000,
      };
    },
  },

  /**
   * A helper for generating experiment branches.
   */
  branches: {
    get() {
      return [
        {
          slug: "control",
          ratio: 1,
          features: [
            {
              featureId: "testFeature",
              value: { testInt: 123, enabled: true },
            },
          ],
        },
        {
          slug: "treatment",
          ratio: 1,
          features: [
            {
              featureId: "testFeature",
              value: { testInt: 123, enabled: true },
            },
          ],
        },
      ];
    },
  },

  /**
   * A helper for generating a recipe that has a single branch with the given
   * feature config.
   */
  withFeatureConfig: {
    value: function NimbusTestUtils_factories_recipe_withFeatureConfig(
      slug,
      { branchSlug = "control", featureId, value = {} } = {},
      props = {}
    ) {
      return NimbusTestUtils.factories.recipe(slug, {
        branches: [
          {
            slug: branchSlug,
            ratio: 1,
            features: [
              {
                featureId,
                value,
              },
            ],
          },
        ],
        ...props,
      });
    },
  },
});
