/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import { EnrollmentStatusEntry, NimbusEnrollments } from "../lib/Enrollments.sys.mjs" */
/** @import { _ExperimentFeature } from "../ExperimentAPI.sys.mjs" */
/** @import { Phase } from "../lib/Migrations.sys.mjs" */

import {
  ExperimentAPI,
  NimbusFeatures,
} from "resource://nimbus/ExperimentAPI.sys.mjs";
import { ExperimentStore } from "resource://nimbus/lib/ExperimentStore.sys.mjs";
import { FileTestUtils } from "resource://testing-common/FileTestUtils.sys.mjs";
import enrollmentSchema from "resource://testing-common/nimbus/schemas/NimbusEnrollment.schema.json" with { type: "json" };
import featureSchema from "resource://testing-common/nimbus/schemas/ExperimentFeature.schema.json" with { type: "json" };
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  FeatureManifest: "resource://nimbus/FeatureManifest.sys.mjs",
  JsonSchema: "resource://gre/modules/JsonSchema.sys.mjs",
  NimbusEnrollments: "resource://nimbus/lib/Enrollments.sys.mjs",
  NimbusMigrations: "resource://nimbus/lib/Migrations.sys.mjs",
  NimbusTelemetry: "resource://nimbus/lib/Telemetry.sys.mjs",
  ExperimentManager: "resource://nimbus/lib/ExperimentManager.sys.mjs",
  ProfilesDatastoreService:
    "moz-src:///toolkit/profile/ProfilesDatastoreService.sys.mjs",
  RemoteSettingsExperimentLoader:
    "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
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

function validateSchema(schemaOrValidator, value, errorMsg) {
  const validator =
    schemaOrValidator instanceof lazy.JsonSchema.Validator
      ? schemaOrValidator
      : new lazy.JsonSchema.Validator(schemaOrValidator);

  const result = validator.validate(value, { shortCircuit: false });
  if (result.errors.length) {
    throw new Error(
      `${errorMsg}: ${JSON.stringify(result.errors, undefined, 2)}`
    );
  }
  return value;
}

/**
 * Wait until enrollments can be added to `manager`, or to the global
 * ExperimentManager if the caller did not pass one.
 *
 * @param {ExperimentManager?} manager
 *        A manager the caller has initialized itself.
 */
async function waitForEnrollmentReady(manager) {
  if (manager) {
    await manager.store.ready();
  } else {
    // ExperimentStore.ready() resolves before ExperimentManager.onStartup()
    // has created the state enrolling needs.
    await ExperimentAPI.init();
  }
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

const NimbusLogging = {
  LOG_LEVEL_PREF: "messaging-system.log",

  originalLogLevel: null,
  outstandingResets: 0,

  /**
   * Enable logging, setting the log level to `all`.
   *
   * This function may be called multiple times and
   * {@link NimbusLogging.maybeResetLogLevel} must be called the same number of
   * time to reset the log level. This ensures that tests that call
   * {@link NimbusTestUtils.enroll} et al. multiple times do not reset the log
   * level until every cleanup handler is called.
   */
  enableLogging() {
    if (this.outstandingResets == 0) {
      if (
        Services.prefs.getPrefType(this.LOG_LEVEL_PREF) !=
        Ci.nsIPrefBranch.PREF_INVALID
      ) {
        this.originalLogLevel = Services.prefs.getStringPref(
          this.LOG_LEVEL_PREF
        );
      }
      Services.prefs.setStringPref(this.LOG_LEVEL_PREF, "all");
    }

    this.outstandingResets += 1;
  },

  /**
   * Reset the log level.
   *
   * This function must be called once for each call to
   * {@link NimbusLogging.enableLogging}.
   */
  maybeResetLogLevel() {
    if (this.outstandingResets > 0) {
      this.outstandingResets -= 1;

      if (this.outstandingResets == 0) {
        if (this.originalLogLevel !== null) {
          Services.prefs.setStringPref(
            this.LOG_LEVEL_PREF,
            this.originalLogLevel
          );
        } else {
          Services.prefs.clearUserPref(this.LOG_LEVEL_PREF);
        }
        this.originalLogLevel = null;
      }
    }
  },
};

export const NimbusTestUtils = {
  init(testCase) {
    const assert = testCase.Assert;
    Object.defineProperty(NimbusTestUtils, "Assert", {
      configurable: true,
      get: () => assert,
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
     * @param {object | undefined} store
     * The `ExperimentStore` to assert is empty. If not provided, the global
     * experiment store (`ExperimentAPI.manager.store`) will be used.
     *
     * @param {object} options
     * @param {boolean} options.allProfiles Whether to assert that no enrollments exist in any profiles in the group.
     */
    async storeIsEmpty(store, { allProfiles = false } = {}) {
      const experimentStore = store ?? ExperimentAPI.manager.store;

      NimbusTestUtils.Assert.deepEqual(
        experimentStore
          .getAll()
          .filter(e => e.active)
          .map(e => e.slug),
        [],
        "Store should have no active enrollments"
      );

      // Do *not* queue a removal from the store yet -- we'll handle that in
      // cleanupEnrollmentDatabase.
      experimentStore
        .getAll()
        .filter(e => !e.active)
        .forEach(e =>
          experimentStore._deleteForTests(e.slug, {
            removeFromNimbusEnrollments: false,
          })
        );

      NimbusTestUtils.Assert.deepEqual(
        experimentStore
          .getAll()
          .filter(e => !e.active)
          .map(e => e.slug),
        [],
        "Store should have no inactive enrollments"
      );

      NimbusTestUtils.cleanupStorePrefCache();

      if (lazy.NimbusEnrollments.persistenceEnabled) {
        await NimbusTestUtils.cleanupEnrollmentDatabase(experimentStore?._db);
        if (allProfiles) {
          const conn = await lazy.ProfilesDatastoreService.getConnection();
          const enrollmentsCount = await conn
            .execute("SELECT COUNT(*) AS count FROM NimbusEnrollments;")
            .then(([row]) => row.getResultByName("count"));

          NimbusTestUtils.Assert.equal(
            enrollmentsCount,
            0,
            "There should be zero enrollments in the NimbusEnrollments table"
          );

          const statusesCount = await conn
            .execute("SELECT COUNT(*) AS count from NimbusEnrollmentStatuses;")
            .then(([row]) => row.getResultByName("count"));

          NimbusTestUtils.Assert.equal(
            statusesCount,
            0,
            "There should be no entries in the NimbusEnrollmentStatusesTable"
          );
        }
      }
    },

    /**
     * Assert that the only active enrollments have the expected slugs.
     *
     * @param {string} expectedSlugs The slugs of the enrollments that we expect to be active.
     */
    async activeEnrollments(expectedSlugs) {
      await NimbusTestUtils.flushStore();

      const conn = await lazy.ProfilesDatastoreService.getConnection();
      const slugs = await conn
        .execute(
          `
            SELECT
              slug
            FROM NimbusEnrollments
            WHERE
              active = true AND
              profileId = :profileId;
          `,
          { profileId: ExperimentAPI.profileId }
        )
        .then(rows => rows.map(row => row.getResultByName("slug")));

      NimbusTestUtils.Assert.deepEqual(
        slugs.sort(),
        expectedSlugs.sort(),
        "Should only see expected active enrollments"
      );
    },

    /**
     * Assert that an enrollment exists in the NimbusEnrollments table.
     *
     * @param {string} slug The slug to check for.
     *
     * @param {object} options
     *
     * @param {boolean | undefined} options.active If provided, this function
     * will assert that the enrollment is active (if true) or inactive (if
     * false).
     *
     * @param {string} options.profileId The profile ID to query with. Defaults
     * to the current profile ID.
     */
    async enrollmentExists(
      slug,
      { active: expectedActive, profileId = ExperimentAPI.profileId } = {}
    ) {
      await NimbusTestUtils.flushStore();

      const conn = await lazy.ProfilesDatastoreService.getConnection();

      const result = await conn.execute(
        `
        SELECT
          active,
          unenrollReason
          FROM NimbusEnrollments
        WHERE
          slug = :slug AND
          profileId = :profileId;
        `,
        { slug, profileId }
      );

      NimbusTestUtils.Assert.ok(
        result.length === 1,
        `Enrollment for ${slug} in profile ${profileId} exists`
      );

      if (typeof expectedActive === "boolean") {
        const active = result[0].getResultByName("active");
        const unenrollReason = result[0].getResultByName("unenrollReason");

        NimbusTestUtils.Assert.equal(
          expectedActive,
          active,
          `Enrollment for ${slug} is ${expectedActive} -- unenrollReason = ${unenrollReason}`
        );
      }
    },

    /**
     * Assert that an enrollment does not exist in the NimbusEnrollments table.
     *
     * @param {string} slug The slug to check for.
     * @param {object} options
     * @param {string} options.profileId The profielID to query with. Defaults
     * to the current profile ID.
     */
    async enrollmentDoesNotExist(
      slug,
      { profileId = ExperimentAPI.profileId } = {}
    ) {
      const conn = await lazy.ProfilesDatastoreService.getConnection();

      const result = await conn.execute(
        `
          SELECT 1
          FROM NimbusEnrollments
          WHERE
            slug = :slug AND
            profileId = :profileId;
        `,
        { slug, profileId }
      );

      NimbusTestUtils.Assert.ok(
        result.length === 0,
        `Enrollment for ${slug} in profile ${profileId} does not exist`
      );
    },
  },

  factories: {
    /**
     * Create a branch with the given feature configuration.
     *
     * @param {string} slug
     * The branch slug.
     *
     * @param {object} options
     *
     * @param {number} options.ratio
     * The branch ratio.
     *
     * @param {object[]} option.features
     * The list of feature configurations.
     *
     * Mutually exclusive with `featureId` and `value`
     *
     * @param {string | undefined} options.featureId
     * The feature ID for the feature used by the branch.
     *
     * Requires `value`. Mutually exclusive with `features`.
     *
     * @param {object | undefined} options.value
     * The feature value for the feature used by the branch.
     *
     * Requires `featureId`. Mutually exclusive with `features`.
     *
     * @returns {object}
     * The created branch.
     */
    branch(slug = "control", { ratio = 1, featureId, value, features } = {}) {
      if (
        (typeof featureId === "undefined") !==
        (typeof value === "undefined")
      ) {
        throw new TypeError(
          "Either provide both featureId and value or neither"
        );
      }

      if (typeof features !== "undefined" && typeof featureId !== "undefined") {
        throw new TypeError("features and featureId are mutually exclusive");
      }

      return {
        slug,
        ratio,
        features: features ?? [
          {
            featureId: featureId ?? "testFeature",
            value: value ?? { testInt: 123, enabled: true },
          },
        ],
        firefoxLabsTitle: null,
      };
    },

    /**
     * Create a set of branches.
     *
     * @param {boolean} isRollout
     * Whether or not to create the branches for a rollout.
     *
     * @returns {object[]}
     * The branches.
     */
    branches(isRollout = false) {
      return isRollout
        ? [NimbusTestUtils.factories.branch("control")]
        : [
            NimbusTestUtils.factories.branch("control"),
            NimbusTestUtils.factories.branch("treatment"),
          ];
    },

    /**
     * A helper for generating valid bucketing configurations.
     *
     * By default, this bucketing configuration will always result in enrollment.
     *
     * @param {object} bucketConfig
     * The bucket configuration.
     *
     * @param {string} bucketConfig.namespace
     * The namespace used as a seed.
     *
     * @param {string} bucketConfig.randomizationUnit
     * The randomization unit, which determines what client ID is used in the
     * seed.
     *
     * @param {number} bucketConfig.start
     * The start of the enrolling buckets.
     *
     * @param {number} bucketConfig.count
     * The number of enrolling buckets.
     *
     * @param {number} bucketConfig.total
     * The total number of buckets.
     */
    bucketConfig(bucketConfig = {}) {
      return {
        namespace: bucketConfig.namespace ?? "nimbus-test-utils",
        randomizationUnit: bucketConfig.randomizationUnit ?? "normandy_id",
        start: bucketConfig.start ?? 0,
        count: bucketConfig.count ?? 1000,
        total: bucketConfig.total ?? 1000,
      };
    },

    /**
     * Create an enrollment from a recipe.
     *
     * @param {object} recipe
     * The recipe to create an enrollment from.
     *
     * @param {object} options
     *
     * @param {string} options.branchSlug
     * The slug of the branch to enroll in. Must be provdied if there is more
     * than one branch.
     *
     * @param {object} options.extra
     * Extra properties to override on the enrollment object.
     *
     * @returns {object}
     * The enrollment.
     */
    enrollment(recipe, { branchSlug, extra = {} } = {}) {
      let branch;
      if (branchSlug) {
        branch = recipe.branches.find(b => b.slug === branchSlug);
      } else if (recipe.branches.length === 1) {
        branch = recipe.branches[0];
      } else {
        throw new Error("branchSlug required for recipes with > 1 branch");
      }

      const {
        source = lazy.NimbusTelemetry.EnrollmentSource.RS_LOADER,
        ...rest
      } = extra;

      return lazy.ExperimentManager.createIncompleteEnrollment(
        recipe,
        branch.slug,
        source,
        rest
      );
    },

    recipe: Object.assign(
      /**
       * Create a recipe.
       *
       * The recipe will always enroll due to true targeting and 100% bucketing.
       *
       * @param {string} slug
       * The slug for the created recipe.
       *
       * @param {object} props
       * Additional properties to splat into to the recipe.
       *
       * @returns {object}
       * The recipe.
       */
      function recipe(slug, props = {}) {
        const {
          isFirefoxLabsOptIn = false,
          isRollout = false,
          ...restProps
        } = props;

        if (isFirefoxLabsOptIn && !isRollout) {
          throw new Error("isFirefoxLabsOptIn requires isRollout");
        }

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
          branches: NimbusTestUtils.factories.branches(isRollout),
          bucketConfig: NimbusTestUtils.factories.bucketConfig(),
          userFacingName: "NimbusTestUtils recipe",
          userFacingDescription: "NimbusTestUtils recipe",
          featureIds: props?.branches?.[0].features?.map(f => f.featureId) || [
            "testFeature",
          ],
          targeting: "true",
          isRollout,
          isFirefoxLabsOptIn,
          firefoxLabsTitle: isFirefoxLabsOptIn ? "placeholder-title" : null,
          firefoxLabsDescription: isFirefoxLabsOptIn
            ? "placeholder-description"
            : null,
          firefoxLabsDescriptionLinks: null,
          firefoxLabsGroup: isFirefoxLabsOptIn ? "placeholder-group" : null,
          requiresRestart: false,
          localizations: null,
          ...restProps,
        };
      },

      {
        /**
         * Create a recipe that has a single branch with the given feature config.
         *
         * @param {string} slug
         * The recipe slug.
         *
         * @param {object} options
         *
         * @param {string} options.branchSlug
         * The name of the branch. Defaults to "control".
         *
         * @param {string} options.featureId
         * The feature ID to configure.
         *
         * @param {object} options.value
         * The value for the feature.
         *
         * @param {object} props
         * Additional properties to splat into the recipe.
         *
         * @returns {object}
         * The recipe.
         */
        withFeatureConfig: function recipeWithFeatureConfig(
          slug,
          { branchSlug = "control", featureId, value = {} } = {},
          props = {}
        ) {
          return NimbusTestUtils.factories.recipe(slug, {
            branches: [
              NimbusTestUtils.factories.branch(branchSlug, {
                featureId,
                value,
              }),
            ],
            ...props,
          });
        },
      }
    ),
  },

  stubs: {
    store() {
      return new ExperimentStore("ExperimentStoreData");
    },

    manager(store) {
      const manager = new lazy.ExperimentManager({
        store: store ?? NimbusTestUtils.stubs.store(),
      });
      const addEnrollment = manager.store.addEnrollment.bind(manager.store);

      // We want calls to `store.addEnrollment` to implicitly validate the
      // enrollment before saving to store
      lazy.sinon
        .stub(manager.store, "addEnrollment")
        .callsFake((enrollment, recipe) => {
          NimbusTestUtils.validateEnrollment(enrollment);
          return addEnrollment(enrollment, recipe);
        });

      return manager;
    },

    rsLoader(manager) {
      const loader = new lazy.RemoteSettingsExperimentLoader(
        manager ?? NimbusTestUtils.stubs.manager()
      );

      Object.defineProperties(loader.remoteSettingsClients, {
        experiments: {
          value: {
            collectionName: "nimbus-desktop-experiments (stubbed)",
            get: () => Promise.resolve([]),
            db: { getLastModified: () => Promise.resolve(0) },
          },
        },

        secureExperiments: {
          value: {
            collectionName: "nimbus-secure-experiments (stubbed)",
            get: () => Promise.resolve([]),
            db: { getLastModified: () => Promise.resolve(0) },
          },
        },
      });

      return loader;
    },
  },

  migrationState: {
    /**
     * A migration state that represents no migrations.
     *
     * @type {Record<Phase, number>}
     */
    UNMIGRATED: Object.freeze({}),

    /**
     * A migration state that represents a successful import into the
     * NimbusEnrollments table.
     *
     * @type {Record<Phase, Number}>
     */
    get IMPORTED_ENROLLMENTS_TO_SQL() {
      const { Phase } = lazy.NimbusMigrations;

      return NimbusTestUtils.makeMigrationState({
        [Phase.INIT_STARTED]: "multi-phase-migrations",
        [Phase.AFTER_STORE_INITIALIZED]: "import-enrollments-to-sql",
        [Phase.AFTER_REMOTE_SETTINGS_UPDATE]: "firefox-labs-enrollments",
      });
    },

    get GRADUATED_FIREFOX_LABS_AUTO_PIP() {
      const { Phase } = lazy.NimbusMigrations;

      return NimbusTestUtils.makeMigrationState({
        [Phase.INIT_STARTED]: "multi-phase-migrations",
        [Phase.AFTER_STORE_INITIALIZED]: "graduate-firefox-labs-auto-pip",
        [Phase.AFTER_REMOTE_SETTINGS_UPDATE]: "firefox-labs-enrollments",
      });
    },

    get SEPARATE_ROLLOUT_OPT_OUT() {
      const { Phase } = lazy.NimbusMigrations;

      return NimbusTestUtils.makeMigrationState({
        [Phase.INIT_STARTED]: "separate-rollout-opt-out",
        [Phase.AFTER_STORE_INITIALIZED]: "graduate-firefox-labs-auto-pip",
        [Phase.AFTER_REMOTE_SETTINGS_UPDATE]: "firefox-labs-enrollments",
      });
    },

    get GRADUATED_FIREFOX_LABS_JPEG_XL() {
      const { Phase } = lazy.NimbusMigrations;

      return NimbusTestUtils.makeMigrationState({
        [Phase.INIT_STARTED]: "separate-rollout-opt-out",
        [Phase.AFTER_STORE_INITIALIZED]: "graduate-firefox-labs-jpeg-xl",
        [Phase.AFTER_REMOTE_SETTINGS_UPDATE]: "firefox-labs-enrollments",
      });
    },

    get PREFFLIPS_RESTORED() {
      const { Phase } = lazy.NimbusMigrations;

      return NimbusTestUtils.makeMigrationState({
        [Phase.INIT_STARTED]: "separate-rollout-opt-out",
        [Phase.AFTER_STORE_INITIALIZED]: "bug-2054546-mitigation",
        [Phase.AFTER_REMOTE_SETTINGS_UPDATE]: "firefox-labs-enrollments",
      });
    },

    get GRADUATED_FIREFOX_LABS_JPEG_XL_ALL_CHANNELS() {
      const { Phase } = lazy.NimbusMigrations;

      return NimbusTestUtils.makeMigrationState({
        [Phase.INIT_STARTED]: "separate-rollout-opt-out",
        [Phase.AFTER_STORE_INITIALIZED]:
          "graduate-firefox-labs-jpeg-xl-all-channels",
        [Phase.AFTER_REMOTE_SETTINGS_UPDATE]: "firefox-labs-enrollments",
      });
    },

    get REMOVED_NORMANDY_DATABASES() {
      const { Phase } = lazy.NimbusMigrations;

      return NimbusTestUtils.makeMigrationState({
        [Phase.INIT_STARTED]: "remove-normandy-databases",
        [Phase.AFTER_STORE_INITIALIZED]:
          "graduate-firefox-labs-jpeg-xl-all-channels",
        [Phase.AFTER_REMOTE_SETTINGS_UPDATE]: "firefox-labs-enrollments",
      });
    },

    get LEGACY_STORE_REMOVED() {
      const { Phase } = lazy.NimbusMigrations;

      return NimbusTestUtils.makeMigrationState({
        [Phase.INIT_STARTED]: "remove-normandy-databases",
        [Phase.AFTER_STORE_INITIALIZED]:
          "graduate-firefox-labs-jpeg-xl-all-channels",
        [Phase.AFTER_REMOTE_SETTINGS_UPDATE]: "firefox-labs-enrollments",
      });
    },

    /**
     * A migration state that represents all migrations applied.
     *
     * @type {Record<Phase, number>}
     */
    get LATEST() {
      return NimbusTestUtils.migrationState.LEGACY_STORE_REMOVED;
    },
  },

  /**
   * Create a migration state to pass to NimbusTestUtils.setupTest.
   *
   * @param {Record<Phase, string>} migrationsByPhase A map of the latest
   * completed migration by phase.
   *
   * @returns {Record<Phase, number>} The values to set for each migration pref.
   */
  makeMigrationState(migrationsByPhase) {
    const state = {};

    for (const [phase, migrationName] of Object.entries(migrationsByPhase)) {
      state[phase] = lazy.NimbusMigrations.MIGRATIONS[phase].findIndex(
        m => m.name === migrationName
      );
    }

    return state;
  },

  /**
   * Add an enrollment to the store without going through the entire enroll
   * flow.
   *
   * Using `ExperimentAPI.manager.enroll()` or {@link NimbusTestUtils.enroll}
   * (or similar hlpers) should be preferred in most cases.
   *
   * N.B.: The JSON store will not be immediately saved to disk, nor will the
   * NimbusEnrollments table. You must call {@link NimbusTestUtils.saveStore} or
   * wait for it to save on its own.
   *
   * @param {object} recipe
   * The recipe to add an enrollment for.
   *
   * @param {object} options
   *
   * @param {ExperimentStore} options.store
   * The store to add the enrollment to. Defaults to the global ExperimentStore
   * (`ExperimentAPI.manager.store`).
   *
   * @param {string} options.branchSlug
   * The slug of the branch to enroll in. Must be provided if there is more
   * than once branch.
   *
   * @param {object} options.extra
   * Extra properties to override on the enrollment object.
   *
   * @returns {object}
   * The enrollment.
   */
  addEnrollmentForRecipe(recipe, { store, branchSlug, extra = {} } = {}) {
    const enrollment = NimbusTestUtils.factories.enrollment(recipe, {
      branchSlug,
      extra,
    });

    (store ?? ExperimentAPI.manager.store).addEnrollment(enrollment, recipe);

    return enrollment;
  },

  /**
   * Add features for tests.
   *
   * NB: These features will only be visible to the JS Nimbus client. The native
   * Nimbus client will have no access.
   *
   * @param {...object} features
   *         A list of `_NimbusFeature`s.
   *
   * @returns {function(): void}
   *          A cleanup function to remove the features once the test has completed.
   */
  addTestFeatures(...features) {
    const validator = new lazy.JsonSchema.Validator(featureSchema);

    for (const feature of features) {
      if (Object.hasOwn(NimbusFeatures, feature.featureId)) {
        throw new Error(
          `Cannot add feature ${feature.featureId} -- a feature with this ID already exists!`
        );
      }

      // Stub out metadata-only properties.
      feature.manifest.owner ??= "owner@example.com";
      feature.manifest.description ??= `${feature.featureId} description`;
      feature.manifest.hasExposure ??= false;
      feature.manifest.exposureDescription ??= `${feature.featureId} exposure description`;

      feature.manifest.variables ??= {};
      for (const [name, variable] of Object.entries(
        feature.manifest?.variables
      )) {
        variable.description ??= `${name} variable`;
      }

      validateSchema(
        validator,
        feature.manifest,
        `Could not validate feature ${feature.featureId}`
      );
    }

    for (const feature of features) {
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
   * @param {string[]} slugs
   *         The slugs to unenroll from.
   *
   * @param {object?} options
   *
   * @param {object?} options.manager
   *         The ExperimentManager to clean up. Defaults to the global
   *         ExperimentManager.
   *
   * @returns {Promise<void>}
   *          A promise that resolves when all experiments have been unenrolled
   *          and the store is empty.
   */
  async cleanupManager(slugs, { manager } = {}) {
    const experimentManager = manager ?? ExperimentAPI.manager;

    for (const slug of slugs) {
      experimentManager.unenroll(slug);
    }

    await NimbusTestUtils.assert.storeIsEmpty(experimentManager.store);
  },

  /**
   * Clean up the enrollments database, removing all enrollments for the current
   * profile ID.
   *
   * The database flushing task will be finalized, preventing further writes.
   *
   * @param {NimbusEnrollments} db The NimbusEnrollments object.
   */
  async cleanupEnrollmentDatabase(db) {
    if (!lazy.NimbusEnrollments.persistenceEnabled) {
      return;
    }

    // Wait for all pending writes to complete and clean up shutdown blocker
    // state.
    await db.finalize();

    const profileId = ExperimentAPI.profileId;

    const conn = await lazy.ProfilesDatastoreService.getConnection();

    const activeSlugs = await conn
      .execute(
        `
        SELECT
          slug
        FROM NimbusEnrollments
        WHERE
          profileId = :profileId AND
          active = true;
      `,
        { profileId }
      )
      .then(rows => rows.map(row => row.getResultByName("slug")));

    NimbusTestUtils.Assert.deepEqual(
      activeSlugs,
      [],
      `No active slugs in NimbusEnrollments for ${profileId}`
    );

    await conn.execute(
      `
        DELETE FROM NimbusEnrollments
        WHERE
          profileId = :profileId AND
          active = false;
      `,
      { profileId }
    );

    await conn.execute(
      `
        DELETE FROM NimbusSyncTimestamps
        WHERE
          profileId = :profileId;
      `,
      { profileId }
    );

    await conn.execute(
      `
        DELETE FROM NimbusEnrollmentStatuses
        WHERE
          profileId = :profileId;
      `,
      { profileId }
    );
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
   * Create a legacy Nimbus store.
   *
   * @param {object[]} enrollments
   * The enrollments to write to the store.
   *
   * @returns {string}
   * The path to the created store.
   */
  async createLegacyStore(enrollments) {
    const path = FileTestUtils.getTempFile("test-experiment-store").path;

    await IOUtils.writeJSON(
      path,
      Object.fromEntries(enrollments.map(e => [e.slug, e]))
    );

    return path;
  },

  /**
   * @typedef {function(ExperimentStore): (Promise<void> | void)}
   * PopulateStoreFn
   */

  /**
   * Populate the Nimbus enrollment store.
   *
   * The store will be finalized to ensure that all pending writes are flushed.
   *
   * @param {PopulateStoreFn} populateStore
   * A function that will be called with the store.
   */
  async populateStore(populateStore) {
    const store = NimbusTestUtils.stubs.store();
    await store.init();
    await populateStore(store);
    await store._db.finalize();
  },

  async deleteEnrollmentsFromProfiles(profileIds) {
    const conn = await lazy.ProfilesDatastoreService.getConnection();
    if (!conn) {
      throw new Error("ProfilesDatastoreService connection is closed");
    }

    await conn.executeTransaction(async () => {
      for (const profileId of profileIds) {
        await conn.execute(
          `
            DELETE FROM NimbusEnrollments
            WHERE profileId = :profileId;
          `,
          { profileId }
        );
      }
    });
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
   * @returns {Promise<function(): Promise<void>>}
   *          A cleanup function that will unenroll from the enrolled recipe and
   *          remove it from the store.
   *
   * @throws {Error} If the recipe references a feature that does not exist or
   *                 if the recipe fails to enroll.
   */
  async enroll(recipe, { manager, source } = {}) {
    if (!recipe?.slug) {
      throw new Error("Experiment with slug is required");
    }

    for (const featureId of recipe.featureIds) {
      if (!Object.hasOwn(NimbusFeatures, featureId)) {
        throw new Error(
          `Refusing to enroll in ${recipe.slug}: feature ${featureId} does not exist`
        );
      }
    }

    NimbusLogging.enableLogging();

    await waitForEnrollmentReady(manager);
    const experimentManager = manager ?? ExperimentAPI.manager;

    const enrollment = await experimentManager.enroll(
      recipe,
      source ?? lazy.NimbusTelemetry.EnrollmentSource.RS_LOADER
    );

    if (!enrollment) {
      throw new Error(`Failed to enroll in ${recipe}`);
    }

    experimentManager.store._syncToChildren({ flush: true });

    return async function doEnrollmentCleanup() {
      experimentManager.unenroll(enrollment.slug);
      experimentManager.store._deleteForTests(enrollment.slug);

      await NimbusTestUtils.flushStore(experimentManager.store);

      NimbusLogging.maybeResetLogLevel();
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
   * @returns {Promise<function(): Promise<void>>}
   *          A cleanup function that will unenroll from the enrolled recipe and
   *          remove it from the store.
   *
   * @throws {Error} If the feature does not exist.
   */
  async enrollWithFeatureConfig(
    { featureId, value = {} },
    { manager, source, slug, branchSlug = "control", isRollout = false } = {}
  ) {
    await waitForEnrollmentReady(manager);
    const experimentManager = manager ?? ExperimentAPI.manager;

    const experimentType = isRollout ? "rollout" : "experiment";
    const experimentId =
      slug ?? `${featureId}-${experimentType}-${Math.random()}`;

    const recipe = NimbusTestUtils.factories.recipe(experimentId, {
      branches: [
        NimbusTestUtils.factories.branch(branchSlug, { featureId, value }),
      ],
      isRollout,
    });

    return NimbusTestUtils.enroll(recipe, {
      manager: experimentManager,
      source,
    });
  },

  async insertEnrollment(recipe, branchSlug, { extra = {}, profileId } = {}) {
    if (!recipe.branches.find(b => b.slug === branchSlug)) {
      throw new Error(`Branch with slug ${branchSlug} not found`);
    }

    const conn = await lazy.ProfilesDatastoreService.getConnection();
    if (!conn) {
      throw new Error("ProfilesDatastoreService connection is closed");
    }

    const active = extra.active ?? true;
    const unenrollReason = active ? null : (extra.unenrollReason ?? "unknown");
    const lastSeen = (extra.lastSeen ?? new Date()).toJSON();
    const setPrefs = active ? (extra.setPrefs ?? null) : null;
    const prefFlips = active ? (extra.prefFlips ?? null) : null;

    await conn.execute(
      `
        INSERT INTO NimbusEnrollments(
          profileId,
          slug,
          branchSlug,
          recipe,
          active,
          unenrollReason,
          lastSeen,
          setPrefs,
          prefFlips,
          source
        )
        VALUES(
          :profileId,
          :slug,
          :branchSlug,
          jsonb(:recipe),
          :active,
          :unenrollReason,
          :lastSeen,
          :setPrefs,
          :prefFlips,
          :source
        )
      `,
      {
        profileId: profileId ?? ExperimentAPI.profileId,
        slug: recipe.slug,
        branchSlug,
        recipe: JSON.stringify(recipe),
        active,
        unenrollReason,
        lastSeen,
        setPrefs: setPrefs ? JSON.stringify(setPrefs) : null,
        prefFlips: prefFlips ? JSON.stringify(prefFlips) : null,
        source: extra.source ?? lazy.NimbusTelemetry.EnrollmentSource.RS_LOADER,
      }
    );
  },

  /**
   * Return the enrollment from the database, if it exists.
   *
   * @param {string} slug The slug to query for.
   * @param {object} options
   * @param {string} options.profileId
   * The profile ID to query the enrollment for. Defaults to the current profile
   * ID.
   */
  async queryEnrollment(slug, { profileId } = {}) {
    const conn = await lazy.ProfilesDatastoreService.getConnection();
    const result = await conn.execute(
      `
      SELECT
        profileId,
        slug,
        branchSlug,
        json(recipe) AS recipe,
        active,
        unenrollReason,
        lastSeen,
        json(setPrefs) AS setPrefs,
        json(prefFlips) AS prefFlips,
        source
      FROM NimbusEnrollments
      WHERE
        slug = :slug AND
        profileId = :profileId;
      `,
      {
        slug,
        profileId: profileId ?? ExperimentAPI.profileId,
      }
    );

    if (!result.length) {
      return null;
    }

    const row = result[0];

    const fields = [
      "profileId",
      "slug",
      "branchSlug",
      "recipe",
      "active",
      "unenrollReason",
      "lastSeen",
      "setPrefs",
      "prefFlips",
      "source",
    ];

    const enrollment = {};

    for (const field of fields) {
      enrollment[field] = row.getResultByName(field);
    }

    enrollment.recipe = JSON.parse(enrollment.recipe);
    enrollment.setPrefs = JSON.parse(enrollment.setPrefs);
    enrollment.prefFlips = JSON.parse(enrollment.prefFlips);

    return enrollment;
  },

  /**
   * Flush the NimbusEnrollments table.
   *
   * @param {ExperimentStore} store
   * The store to save.
   */
  async saveStore(store) {
    await store._db?._flushNow();
  },

  /**
   * @typedef {object} TestContext
   *
   * @property {object} sandbox
   *           A sinon sandbox.
   *
   * @property {RemoteSettingsExperimentLoader} loader
   *           A RemoteSettingsExperimentLoader instance that has stubbed
   *           RemoteSettings clients.
   *
   * @property {ExperimentManager} manager
   *           An ExperimentManager instance that will validate all enrollments
   *           added to its store.
   *
   * @property {function(): Promise<void>} cleanup
   *           A cleanup function that should be called at the end of the test.
   */

  /**
   * @param {object} options
   * @param {boolean?} options.init
   * Initialize the Experiment API.
   *
   * If false, the caller must call {@link ExperimentAPI.init} to complete
   * initialization.
   *
   * @param {string?} options.legacyStorePath
   * If provided, this will be used as the path of the legacy experiment store
   * during migrations.
   *
   * If provided, the {@link options.migrationState} option must also be set.
   *
   * @param {PopulateStoreFn} options.populateStore
   * A function that will populate the Nimbus enrollment store prior to
   * initialization.
   *
   * If provided, the {@link options.migrationState} option must also be set.
   *
   * Note: The actual store object used by this function will *not*
   * be used by the instantiated ExperimentAPI.
   *
   * @param {(object[] | function(): object[])?} options.experiments
   * If provided, these recipes will be returned by the RemoteSettings
   * experiments client.
   *
   * @param {object[]?} options.secureExperiments
   * If provided, these recipes will be returned by the RemoteSetings
   * secureExperiments client.
   *
   * @param {boolean?} options.clearTelemetry
   * If true, telemetry will be reset in the cleanup function.
   *
   * @param {_ExperimentFeature[] | undefined} options.features
   * Features to add to NimbusFeatures.
   *
   * @param {Record<Phase, number>?} options.migrationState
   * The value that should be set for the Nimbus migration prefs. If not
   * provided, {@link NimbusTestUtils.migrationState.LATEST} will be used.
   *
   * Most tests will want to use either
   * {@link NimbusTestUtils.migrationState.UNMIGRATED} or
   * {@link NimbusTestUtils.migrationState.LATEST}, depending on whether or not
   * they are writing to the `NimbusEnrollments` database table.
   *
   * @throws {Error}
   * If the the arguments to this function are not consistent.
   *
   * @returns {TestContext}
   * Everything you need to write a test using Nimbus.
   */
  async setupTest({
    init = true,
    populateStore,
    legacyStorePath,
    experiments,
    secureExperiments,
    clearTelemetry = false,
    features,
    migrationState,
  } = {}) {
    if (populateStore || legacyStorePath) {
      if (!lazy.NimbusEnrollments.persistenceEnabled) {
        throw new Error("persistence is disabled");
      }

      if (typeof migrationState === "undefined") {
        throw new Error("setupTest: populateStore requires migrationState");
      }
    }

    NimbusLogging.enableLogging();

    if (populateStore) {
      await NimbusTestUtils.populateStore(populateStore);
    } else if (legacyStorePath) {
      Services.prefs.setStringPref(
        "nimbus.persistence.legacyStorePath",
        legacyStorePath
      );
    }

    const sandbox = lazy.sinon.createSandbox();

    let cleanupFeatures = null;
    if (Array.isArray(features)) {
      cleanupFeatures = NimbusTestUtils.addTestFeatures(...features);
    }

    const store = NimbusTestUtils.stubs.store();
    const manager = NimbusTestUtils.stubs.manager(store);
    const loader = NimbusTestUtils.stubs.rsLoader(manager);

    sandbox.stub(ExperimentAPI, "_rsLoader").get(() => loader);
    sandbox.stub(ExperimentAPI, "manager").get(() => manager);
    sandbox
      .stub(loader.remoteSettingsClients.experiments, "get")
      .resolves(Array.isArray(experiments) ? experiments : []);
    sandbox
      .stub(loader.remoteSettingsClients.experiments.db, "getLastModified")
      .resolves(0);
    sandbox
      .stub(loader.remoteSettingsClients.secureExperiments, "get")
      .resolves(Array.isArray(secureExperiments) ? secureExperiments : []);
    sandbox
      .stub(
        loader.remoteSettingsClients.secureExperiments.db,
        "getLastModified"
      )
      .resolves(0);

    if (typeof migrationState === "undefined") {
      migrationState = NimbusTestUtils.migrationState.LATEST;
    }

    for (const [phase, value] of Object.entries(migrationState)) {
      Services.prefs.setIntPref(
        lazy.NimbusMigrations.NIMBUS_MIGRATION_PREFS[phase],
        value
      );
    }

    const ctx = {
      sandbox,
      loader,
      manager,
      store,
      async cleanup() {
        await NimbusTestUtils.assert.storeIsEmpty(manager.store, {
          allProfiles: true,
        });

        ExperimentAPI._resetForTests();
        sandbox.restore();

        if (cleanupFeatures) {
          cleanupFeatures();
        }

        if (clearTelemetry) {
          Services.fog.testResetFOG();
          Services.telemetry.clearEvents();
        }

        // Remove all migration state.
        Services.prefs.deleteBranch("nimbus.migrations.");

        Services.prefs.clearUserPref("nimbus.firstUpdateComplete");

        if (legacyStorePath) {
          Services.prefs.clearUserPref("nimbus.persistence.legacyStorePath");

          // The legacy store may have already been deleted by a migration.
          await IOUtils.remove(legacyStorePath, { ignoreAbsent: true });
        }

        NimbusLogging.maybeResetLogLevel();
      },
    };

    if (init) {
      await ExperimentAPI.init();
    }

    return ctx;
  },

  /**
   * Validate an enrollment matches the Nimbus enrollment schema.
   *
   * @param {object} enrollment
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
      enrollmentSchema,
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

  async flushStore(store = null) {
    const db = (store ?? ExperimentAPI.manager.store)._db;

    await db?._flushNow();
  },

  /**
   * Temporarily disable signature verification for Remote Settings clients used
   * by Nimbus.
   *
   * NB: This is only required for browser tests.
   *
   * @returns {() => void} A callback that will restore signature verification
   * to its previous state.
   */
  disableSignatureVerification() {
    const { remoteSettingsClients } = ExperimentAPI._rsLoader;

    const originalValues = Object.fromEntries(
      Object.entries(remoteSettingsClients).map(([key, collection]) => [
        key,
        collection.verifySignature,
      ])
    );

    for (const client of Object.values(remoteSettingsClients)) {
      client.verifySignature = false;
    }

    return () => {
      for (const [key, client] of Object.entries(remoteSettingsClients)) {
        client.verifySignature = originalValues[key];
      }
    };
  },

  /**
   * Return all enrollment status entries
   *
   * @see {@link NimbusTestUtils.loadEnrollmentStatusEntriesForSlug}
   *
   * @param {string} filterSlug The slug to filter by.
   *
   * @returns {Promise<Map<string, EnrollmentStatusEntry[]>>}
   * A map mapping of slugs to their enrollment status entries, sorted by
   * {@link EnrollmentStatusEntry.index index}.
   */
  async loadEnrollmentStatusEntries(filterSlug = undefined) {
    const conn = await lazy.ProfilesDatastoreService.getConnection();

    let query = `
      SELECT
        slug,
        entryIndex,
        timestamp,
        json(status) AS status
      FROM NimbusEnrollmentStatuses
      WHERE
        profileId = :profileId
    `;

    if (filterSlug) {
      query += `
        AND slug = :slug
      `;
    }

    query += `
      ORDER BY entryIndex ASC;
    `;

    const rows = await conn.execute(query, {
      profileId: ExperimentAPI.profileId,
      slug: filterSlug,
    });

    const enrollmentStatuses = new Map();
    for (const row of rows) {
      const slug = row.getResultByName("slug");
      const timestamp = new Date(row.getResultByName("timestamp"));
      const index = row.getResultByName("entryIndex");
      const status = JSON.parse(row.getResultByName("status"));

      enrollmentStatuses
        .getOrInsertComputed(slug, () => [])
        .push({
          timestamp,
          index,
          status,
        });
    }

    return enrollmentStatuses;
  },

  /**
   * Return all enrollment status entries for the given slug.
   *
   * @param {string} slug The slug to filter by.
   *
   * @returns {Promise<EnrollmentStatusEntry[]>}
   * An array of all enrollment status entries for the given slug, sorted by
   * {@link EnrollmentStatusEntry.index index}.
   */
  async loadEnrollmentStatusEntriesForSlug(slug) {
    if (!slug) {
      throw new TypeError("slug is required");
    }

    const entryMap = await NimbusTestUtils.loadEnrollmentStatusEntries(slug);

    return entryMap.get(slug) ?? [];
  },
};
