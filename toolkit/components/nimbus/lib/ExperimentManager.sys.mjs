/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  ClientEnvironmentBase:
    "resource://gre/modules/components-utils/ClientEnvironment.sys.mjs",
  ClientID: "resource://gre/modules/ClientID.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  ExperimentStore: "resource://nimbus/lib/ExperimentStore.sys.mjs",
  FirstStartup: "resource://gre/modules/FirstStartup.sys.mjs",
  NimbusEnrollments: "resource://nimbus/lib/Enrollments.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusTelemetry: "resource://nimbus/lib/Telemetry.sys.mjs",
  NormandyUtils: "resource://normandy/lib/NormandyUtils.sys.mjs",
  PrefFlipsFeature: "resource://nimbus/lib/PrefFlipsFeature.sys.mjs",
  PrefUtils: "moz-src:///toolkit/modules/PrefUtils.sys.mjs",
  EnrollmentsContext:
    "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs",
  MatchStatus: "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs",
  Sampling: "resource://gre/modules/components-utils/Sampling.sys.mjs",
  ShutdownStartedError:
    "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs",

  log: () => {
    const { Logger } = ChromeUtils.importESModule(
      "resource://messaging-system/lib/Logger.sys.mjs"
    );
    return new Logger("ExperimentManager");
  },
});

/** @typedef {import("./PrefFlipsFeature.sys.mjs").PrefBranch} PrefBranch */

const CannotEnrollFeatureReason = Object.freeze({
  /**
   * The feature does not exist.
   */
  DOES_NOT_EXIST: "does-not-exist",

  /**
   * There is already another experiment or recipe enrolled in features and the
   * feature does not support co-enrollment.
   */
  ENROLLED_IN_FEATURE: "enrolled-in-feature",

  /**
   * The enrollment is paused.
   */
  ENROLLMENT_PAUSED: "enrollment-paused",
});

/**
 * @typedef {T[keyof T]} EnumValuesOf
 * @template {type} T
 */

/** @typedef {EnumValuesOf<typeof CannotEnrollFeatureReason>} CannotEnrollFeatureReason */

/**
 * @typedef {object} _CanEnrollResult
 * @property {true} ok Whether or not enrollment is possible.
 */

/**
 * @typedef {object} _CannotEnrollResult
 * @property {false} ok Whether or not enrollment is possible.
 * @property {string|undefined} featureId If reason = DOES_NOT_EXIST, the
 * feature that does not exist.
 * @property {Set<string>|undefined} conflictingEnrollments
 * If reason = ENROLLED_IN_FEATURE, an array of slugs that conflict based on
 * feature ID.
 * @property {CannotEnrollFeatureReason} reason Why enrollment is not possible.
 * @property {string | undefined} slug Optionally, a slug of a conflicting
 * enrollment.
 */

/**
 * Whether or not enrollment is possible in a given recipe.
 *
 * @typedef {_CanEnrollResult | _CannotEnrollResult} CanEnrollResult
 * @property {boolean} ok
 */

const IS_MAIN_PROCESS =
  Services.appinfo.processType === Services.appinfo.PROCESS_TYPE_DEFAULT;

export const UnenrollmentCause = {
  fromCheckRecipeResult(result) {
    const { UnenrollReason } = lazy.NimbusTelemetry;

    let reason;
    const extra = {};

    if (result.ok) {
      switch (result.status) {
        case lazy.MatchStatus.NOT_SEEN:
          reason = UnenrollReason.RECIPE_NOT_SEEN;
          break;

        case lazy.MatchStatus.NO_MATCH:
          reason = UnenrollReason.TARGETING_MISMATCH;
          break;

        case lazy.MatchStatus.TARGETING_ONLY:
          reason = UnenrollReason.BUCKETING;
          break;

        case lazy.MatchStatus.UNENROLLED_IN_ANOTHER_PROFILE:
          reason = UnenrollReason.UNENROLLED_IN_ANOTHER_PROFILE;
          break;

        // TARGETING_AND_BUCKETING cannot cause unenrollment.
      }
    } else {
      reason = result.reason;

      switch (reason) {
        case UnenrollReason.L10N_MISSING_ENTRY:
        case UnenrollReason.L10N_MISSING_LOCALE:
          extra.locale = result.locale;
          break;
      }
    }

    return { reason, ...extra };
  },

  fromReason(reason) {
    return { reason };
  },

  /**
   * An unenrollment caused by a pref change.
   *
   * @param {object} changedPref
   * @param {string} changedPref.name The pref that changed.
   * @param {string} changedPref.branch The branch on which the pref
   * was changed.
   * @param {boolean} isAboutConfigChange Whether or not the change was caused
   * by the user via about:config.
   *
   * @returns {object} The unenrollment cause.
   */
  ChangedPref(changedPref, isAboutConfigChange) {
    return {
      reason: lazy.NimbusTelemetry.UnenrollReason.CHANGED_PREF,
      changedPref,
      isAboutConfigChange,
    };
  },

  MissingLocale(locale) {
    return {
      reason: lazy.NimbusTelemetry.UnenrollReason.L10N_MISSING_LOCALE,
      locale,
    };
  },

  PrefFlipsConflict(conflictingSlug) {
    return {
      reason: lazy.NimbusTelemetry.UnenrollReason.PREF_FLIPS_CONFLICT,
      conflictingSlug,
    };
  },

  PrefFlipsFailed(prefName, prefType) {
    return {
      reason: lazy.NimbusTelemetry.UnenrollReason.PREF_FLIPS_FAILED,
      prefName,
      prefType,
    };
  },

  Migration(migration) {
    return {
      reason: lazy.NimbusTelemetry.UnenrollReason.MIGRATION,
      migration,
    };
  },

  Unknown() {
    return {
      reason: lazy.NimbusTelemetry.UnenrollReason.UNKNOWN,
    };
  },
};

/**
 * An entry in the list of opt-in recipes, which includes the recipe and its
 * source.
 *
 * @typedef {object} OptInEntry
 * @property {object} recipe
 * @property {string} source
 */

/**
 * A module for processes Experiment recipes, choosing and storing enrollment state,
 * and sending experiment-related Telemetry.
 */
export class ExperimentManager {
  /** @type AboutConfigObserver */
  #aboutConfigObserver;

  constructor({ store } = {}) {
    this.store = store || new lazy.ExperimentStore();

    /** @type {OptInEntry[]} */
    this.optIns = [];
    // By default, no extra context.
    this.extraContext = {};

    // A Map from pref names to pref observers and metadata. See
    // `_updatePrefObservers` for the full structure.
    //
    // This can only be used in the parent process ExperimentManager.
    this._prefs = null;

    // A Map from enrollment slugs to a Set of prefs that enrollment is setting
    // or would set (e.g., if the enrollment is a rollout and there wasn't an
    // active experiment already setting it).
    //
    // This can only be used in the parent process ExperimentManager.
    this._prefsBySlug = null;

    // The PrefFlipsFeature instance for managing arbitrary pref flips.
    //
    // This can only be used in the parent process ExperimentManager.
    this._prefFlips = null;

    this.#aboutConfigObserver = new AboutConfigObserver();
  }

  /**
   * Creates a targeting context with following filters:
   *
   *   * `activeExperiments`: an array of slugs of all the active experiments
   *   * `isFirstStartup`: a boolean indicating whether or not the current enrollment
   *      is performed during the first startup
   *
   * @returns {object} A context object
   */
  createTargetingContext() {
    let context = {
      ...this.extraContext,

      isFirstStartup: lazy.FirstStartup.state === lazy.FirstStartup.IN_PROGRESS,

      isNonStubFirstRun: !Services.prefs.getBoolPref(
        "nimbus.firstUpdateComplete",
        false
      ),

      get currentDate() {
        return new Date();
      },
    };
    Object.defineProperty(context, "activeExperiments", {
      enumerable: true,
      get: async () => {
        await this.store.ready();
        return this.store.getAllActiveExperiments().map(exp => exp.slug);
      },
    });
    Object.defineProperty(context, "activeRollouts", {
      enumerable: true,
      get: async () => {
        await this.store.ready();
        return this.store.getAllActiveRollouts().map(rollout => rollout.slug);
      },
    });
    Object.defineProperty(context, "previousExperiments", {
      enumerable: true,
      get: async () => {
        await this.store.ready();
        return this.store
          .getAll()
          .filter(enrollment => !enrollment.active && !enrollment.isRollout)
          .map(exp => exp.slug);
      },
    });
    Object.defineProperty(context, "previousRollouts", {
      enumerable: true,
      get: async () => {
        await this.store.ready();
        return this.store
          .getAll()
          .filter(enrollment => !enrollment.active && enrollment.isRollout)
          .map(rollout => rollout.slug);
      },
    });
    Object.defineProperty(context, "enrollments", {
      enumerable: true,
      get: async () => {
        await this.store.ready();
        return this.store.getAll().map(enrollment => enrollment.slug);
      },
    });
    Object.defineProperty(context, "enrollmentsMap", {
      enumerable: true,
      get: async () => {
        await this.store.ready();
        return this.store.getAll().reduce((acc, enrollment) => {
          acc[enrollment.slug] = enrollment.branch.slug;
          return acc;
        }, {});
      },
    });
    return context;
  }

  /**
   * Runs on startup, including before first run.
   *
   * @param {object} extraContext extra targeting context provided by the
   * ambient environment.
   */
  async onStartup(extraContext = {}) {
    if (!IS_MAIN_PROCESS) {
      throw new Error(
        "ExperimentManager.onStartup() can only be called from the main process"
      );
    }

    lazy.log.debug("onStartup");

    this._prefs = new Map();
    this._prefsBySlug = new Map();
    this._prefFlips = new lazy.PrefFlipsFeature({ manager: this });

    await this.store.ready();
    this.extraContext = extraContext;

    const restoredExperiments = this.store.getAllActiveExperiments();
    const restoredRollouts = this.store.getAllActiveRollouts();

    for (const experiment of restoredExperiments) {
      lazy.NimbusTelemetry.setExperimentActive(experiment);
      if (await this._restoreEnrollmentPrefs(experiment)) {
        this._updatePrefObservers(experiment);
      }
    }
    for (const rollout of restoredRollouts) {
      lazy.NimbusTelemetry.setExperimentActive(rollout);
      if (await this._restoreEnrollmentPrefs(rollout)) {
        this._updatePrefObservers(rollout);
      }
    }

    if (
      lazy.ExperimentAPI.labsEnabled &&
      lazy.NimbusEnrollments.persistenceEnabled
    ) {
      // If labs are disabled, we will immediately clear the list of opt-in
      // recipes after initialization.
      this.optIns = await lazy.NimbusEnrollments.loadThirdPartyOptInRecipes();
    }

    this._prefFlips.init();

    if (!lazy.ExperimentAPI.labsEnabled) {
      this._handleLabsDisabled();
    }

    if (!lazy.ExperimentAPI.rolloutsEnabled) {
      this._handleRolloutsOptOut();
    }

    if (!lazy.ExperimentAPI.studiesEnabled) {
      this._handleStudiesOptOut();
    }

    lazy.NimbusFeatures.nimbusTelemetry.onUpdate(() => {
      // Providing default values ensure we disable metrics when unenrolling.
      const cfg = {
        metrics_enabled: {
          "nimbus_targeting_environment.targeting_context_value": false,
        },
      };

      const overrides =
        lazy.NimbusFeatures.nimbusTelemetry.getVariable(
          "gleanMetricConfiguration"
        ) ?? {};

      for (const [key, value] of Object.entries(overrides)) {
        cfg[key] = { ...(cfg[key] ?? {}), ...value };
      }

      Services.fog.applyServerKnobsConfig(JSON.stringify(cfg));
    });
  }

  /**
   * Handle a recipe from a source.
   *
   * If the recipe is already enrolled we will update the enrollment. Otherwise
   * enrollment will be attempted.
   *
   * @param {object} recipe
   *        The recipe.
   *
   * @param {string} source
   *         The source of the recipe, e.g., "rs-loader".
   *
   * @param {object} result
   *        The result of validation, targeting, and bucketing.
   *
   *        See `CheckRecipeResult` for details.
   */
  async onRecipe(recipe, source, result) {
    const { EnrollmentStatus, EnrollmentStatusReason } = lazy.NimbusTelemetry;
    const enrollment = this.store.get(recipe.slug);
    if (enrollment) {
      await this.updateEnrollment(enrollment, recipe, source, result);
      return;
    }

    if (!result.ok) {
      this.store._db?.recordEnrollmentStatus(recipe.slug, {
        status: EnrollmentStatus.DISQUALIFIED,
        reason: EnrollmentStatusReason.ERROR,
        error: result.reason,
      });

      return;
    }

    if (
      recipe.isFirefoxLabsOptIn &&
      result.status !== lazy.MatchStatus.DISABLED
    ) {
      if (!this.registerOptIn(recipe, source)) {
        lazy.log.error(
          `Could not register opt-in with slug ${recipe.slug}: an opt-in already exists with that slug`
        );
      }

      return;
    }

    switch (result.status) {
      case lazy.MatchStatus.DISABLED:
        this.store._db?.recordEnrollmentStatus(recipe.slug, {
          status: EnrollmentStatus.NOT_ENROLLED,
          reason: EnrollmentStatusReason.OPT_OUT,
        });
        break;

      case lazy.MatchStatus.ENROLLMENT_PAUSED:
        this.store._db?.recordEnrollmentStatus(recipe.slug, {
          status: EnrollmentStatus.NOT_ENROLLED,
          reason: EnrollmentStatusReason.ENROLLMENTS_PAUSED,
        });
        break;

      case lazy.MatchStatus.NO_MATCH:
        this.store._db?.recordEnrollmentStatus(recipe.slug, {
          status: EnrollmentStatus.NOT_ENROLLED,
          reason: EnrollmentStatusReason.NOT_TARGETED,
        });
        break;

      case lazy.MatchStatus.TARGETING_ONLY:
        this.store._db?.recordEnrollmentStatus(recipe.slug, {
          status: EnrollmentStatus.NOT_ENROLLED,
          reason: EnrollmentStatusReason.NOT_SELECTED,
        });
        break;

      case lazy.MatchStatus.UNENROLLED_IN_ANOTHER_PROFILE:
        this.store._db?.recordEnrollmentStatus(recipe.slug, {
          status: EnrollmentStatus.NOT_ENROLLED,
          reason: EnrollmentStatusReason.UNENROLLED_IN_ANOTHER_PROFILE,
        });
        break;

      case lazy.MatchStatus.TARGETING_AND_BUCKETING:
        await this.enroll(recipe, source);
        break;

      // This function will not be called with MatchStatus.NOT_SEEN --
      // RemoteSettingsExperimentLoader will call updateEnrollment directly
      // instead.
    }
  }

  /**
   * Determine userId based on bucketConfig.randomizationUnit;
   * either "normandy_id" or "group_id".
   *
   * @param {object} bucketConfig
   */
  async getUserId(bucketConfig) {
    let id;
    if (bucketConfig.randomizationUnit === "normandy_id") {
      id = lazy.ClientEnvironmentBase.legacyRandomizationId;
    } else if (bucketConfig.randomizationUnit === "group_id") {
      id = await lazy.ClientID.getProfileGroupID();
    } else {
      // Others not currently supported.
      lazy.log.debug(
        `Invalid randomizationUnit: ${bucketConfig.randomizationUnit}`
      );
    }
    return id;
  }

  /**
   * Get the list of opt-ins that are available for enrollment.
   *
   * This may return an empty list if called during shutdown.
   *
   * @returns {OptInEntry[]} The opt-in recipes and their sources.
   */
  async getAvailableOptIns() {
    const enrollmentsCtx = new lazy.EnrollmentsContext(this, null, {
      validationEnabled: false,
    });

    // RemoteSettingsExperimentLoader could be in a middle of updating recipes
    // so let's wait for the update to finish and this promise to resolve.
    await lazy.ExperimentAPI._rsLoader.finishedUpdating();

    // RemoteSettingsExperimentLoader should have finished updating at least
    // once. Prevent concurrent updates while we filter through the list of
    // available opt-in recipes.
    let entries;
    try {
      entries = await lazy.ExperimentAPI._rsLoader.withUpdateLock(
        async () => {
          const filtered = [];

          for (const entry of this.optIns) {
            if (
              (await enrollmentsCtx.checkTargeting(entry.recipe)) &&
              (await this.isInBucketAllocation(entry.recipe.bucketConfig)) &&
              (this.store.get(entry.recipe.slug)?.active ||
                this.canEnroll(entry.recipe).ok)
            ) {
              filtered.push(entry);
            }
          }

          return filtered;
        },
        { mode: "shared" }
      );
    } catch (e) {
      // If we're in shutdown, there is no point presenting opt-ins to the user
      // because we don't want to change enrollment state.
      if (e instanceof lazy.ShutdownStartedError) {
        return [];
      }

      throw e;
    }

    entries.sort(
      (a, b) =>
        new Date(a.recipe.publishedDate ?? 0) -
        new Date(b.recipe.publishedDate ?? 0)
    );

    return entries;
  }

  /**
   * Determine if this client falls into the bucketing specified in bucketConfig
   *
   * @param {object} bucketConfig
   * @param {string} bucketConfig.randomizationUnit
   *                 The randomization unit to use for bucketing. This must be
   *                 either "normandy_id" or "group_id".
   * @param {number} bucketConfig.start
   *                 The start of the bucketing range (inclusive).
   * @param {number} bucketConfig.count
   *                 The number of buckets in the range.
   * @param {number} bucketConfig.total
   *                 The total number of buckets.
   * @param {string} bucketConfig.namespace
   *                 A namespace used to seed the RNG used in the sampling
   *                 algorithm. Given an otherwise identical bucketConfig with
   *                 different namespaces, the client will fall into different a
   *                 different bucket.
   * @returns {Promise<boolean>}
   *          Whether or not the client falls into the bucketing range.
   */
  async isInBucketAllocation(bucketConfig) {
    if (!bucketConfig) {
      lazy.log.debug("Cannot enroll if recipe bucketConfig is not set.");
      return false;
    }

    const id = await this.getUserId(bucketConfig);
    if (!id) {
      return false;
    }

    return lazy.Sampling.bucketSample(
      [id, bucketConfig.namespace],
      bucketConfig.start,
      bucketConfig.count,
      bucketConfig.total
    );
  }

  /**
   * Determine if enrollment in the given recipe is possible.
   *
   * @param {object} recipe The recipe in question.
   *
   * @returns {CanEnrollResult} Whether or not we can enroll into a given recipe.
   */
  canEnroll(recipe) {
    const storeLookupByFeature = recipe.isRollout
      ? this.store.getRolloutForFeature.bind(this.store)
      : this.store.getExperimentForFeature.bind(this.store);

    if (recipe.isEnrollmentPaused) {
      return {
        ok: false,
        reason: CannotEnrollFeatureReason.ENROLLMENT_PAUSED,
      };
    }

    const conflictingEnrollments = new Set();

    for (const featureId of recipe.featureIds) {
      const feature = lazy.NimbusFeatures[featureId];

      if (!feature) {
        return {
          ok: false,
          reason: CannotEnrollFeatureReason.DOES_NOT_EXIST,
          featureId,
        };
      }

      if (feature.allowCoenrollment) {
        continue;
      }

      const enrollment = storeLookupByFeature(featureId);
      if (enrollment) {
        conflictingEnrollments.add(enrollment.slug);
      }
    }

    if (conflictingEnrollments.size) {
      return {
        ok: false,
        reason: CannotEnrollFeatureReason.ENROLLED_IN_FEATURE,
        conflictingEnrollments,
      };
    }

    return { ok: true };
  }

  /**
   * Start a new experiment by enrolling the users
   *
   * @param {object} recipe
   * The recipe to enroll in.
   *
   * @param {string} source
   * The source of the experiment (e.g., "rs-loader" for recipes from Remote
   * Settings).
   *
   * @param {object} options
   *
   * @param {boolean} options.reenroll
   * Allow re-enrollment. Only supported for rollouts.
   *
   * @param {string} options.branchSlug
   * If enrolling in a Firefox Labs opt-in, this option is required and will
   * dictate which branch to enroll in.
   *
   * @param {Map<string, unknown> | undefined} options.originalPrefValues
   * If enrolling in a Firefox labs opt-in, this option allows Labs to specify
   * original values for prefs.
   *
   * @returns {Promise<Enrollment>}
   * The experiment object stored in the data store.
   *
   * @throws {Error}
   * If a recipe already exists in the store with the same slug as `recipe` and
   * re-enrollment is prevented.
   */
  async enroll(
    recipe,
    source,
    { reenroll = false, branchSlug, originalPrefValues } = {}
  ) {
    if (typeof source !== "string") {
      throw new Error("source is required");
    }

    let { slug, branches, bucketConfig, isFirefoxLabsOptIn } = recipe;

    const enrollment = this.store.get(slug);

    if (
      enrollment &&
      (enrollment.active ||
        (!isFirefoxLabsOptIn && (!enrollment.isRollout || !reenroll)))
    ) {
      lazy.NimbusTelemetry.recordEnrollmentFailure(
        slug,
        lazy.NimbusTelemetry.EnrollmentFailureReason.NAME_CONFLICT
      );
      this.store._db?.recordEnrollmentStatus(slug, {
        status: lazy.NimbusTelemetry.EnrollmentStatus.NOT_ENROLLED,
        reason: lazy.NimbusTelemetry.EnrollmentStatusReason.NAME_CONFLICT,
      });

      throw new Error(`An experiment with the slug "${slug}" already exists.`);
    }

    let branch;

    if (isFirefoxLabsOptIn) {
      if (typeof branchSlug === "undefined") {
        throw new TypeError(
          `Branch slug not provided for Firefox Labs opt in recipe: "${slug}"`
        );
      } else {
        branch = branches.find(branch => branch.slug === branchSlug);

        if (!branch) {
          throw new Error(
            `Invalid branch slug provided for Firefox Labs opt in recipe: "${slug}"`
          );
        }
      }
    } else if (typeof branchSlug !== "undefined") {
      throw new TypeError(
        "branchSlug only supported for recipes with isFirefoxLabsOptIn = true"
      );
    } else {
      // Here recipe is not a Firefox Labs opt-in, so we use a ratio sampled
      // branch.
      const userId = await this.getUserId(bucketConfig);
      branch = await this.chooseBranch(slug, branches, userId);
    }

    const result = this.canEnroll(recipe);
    if (!result.canEnroll) {
      switch (result.reason) {
        case CannotEnrollFeatureReason.DOES_NOT_EXIST:
          // We do not submit telemetry about this because, if validation was
          // enabled, we would have already rejected the recipe in
          // RemoteSettingsExperimentLoader. This will likely only happen in a
          // test where enroll is called directly.
          lazy.log.debug(
            `Skipping enrollment for ${slug}: no such feature ${result.featureId}`
          );
          return null;

        case CannotEnrollFeatureReason.ENROLLED_IN_FEATURE:
          lazy.log.debug(
            `Skipping enrollment for "${slug}" because there is an existing ${
              recipe.isRollout ? "rollout" : "experiment"
            } for this feature.`
          );
          lazy.NimbusTelemetry.recordEnrollmentFailure(
            slug,
            lazy.NimbusTelemetry.EnrollmentFailureReason.FEATURE_CONFLICT
          );
          this.store._db?.recordEnrollmentStatus(slug, {
            status: lazy.NimbusTelemetry.EnrollmentStatus.NOT_ENROLLED,
            reason:
              lazy.NimbusTelemetry.EnrollmentStatusReason.FEATURE_CONFLICT,
            conflictSlugs: Array.from(result.conflictingEnrollments).sort(),
          });
          return null;
      }
    }

    return this._enroll(
      recipe,
      branch.slug,
      source,
      recipe.isFirefoxLabsOptIn ? originalPrefValues : undefined
    );
  }

  /**
   * Enroll in a specific branch of a recipe.
   *
   * @param {object} recipe
   * The experiment recipe.
   *
   * @param {string} branchSlug
   * The slug of the branch to enroll in. This must exist in the recipe.
   *
   * @param {string} source
   * The source associated with the enrollment.
   *
   * @param {Map<string, unknown> | undefined} originalPrefValues
   * If enrolling in a Firefox labs opt-in, this option allows Labs to specify
   * original values for prefs.
   *
   * @returns {object} The computed enrollment.
   */
  _enroll(recipe, branchSlug, source, originalPrefValues = undefined) {
    const { slug, isRollout } = recipe;
    const { enrollment, prefsToSet } = this.createEnrollment(
      recipe,
      branchSlug,
      source,
      undefined,
      originalPrefValues
    );

    // Unenroll in any conflicting prefFlips enrollments.
    if (prefsToSet.length) {
      this._prefFlips._handleSetPrefConflict(
        slug,
        enrollment.prefs.map(p => p.name)
      );
    }

    this.store.addEnrollment(enrollment, recipe);

    this._setEnrollmentPrefs(prefsToSet);
    this._updatePrefObservers(enrollment);

    const enrollmentStatus = lazy.NimbusTelemetry.recordEnrollment(enrollment);
    this.store._db?.recordEnrollmentStatus(enrollment.slug, enrollmentStatus);

    lazy.log.debug(
      `New ${isRollout ? "rollout" : "experiment"} started: ${slug}, ${
        branchSlug
      }`
    );

    return enrollment;
  }

  /**
   * @typedef {object} CreateEnrollmentResult
   *
   * @property {object} enrollment
   * The created enrollment.
   *
   * @property {PrefToSet[] | null} prefsToSet
   * Prefs that should be set upon enrollment.
   */

  /**
   * Create an enrollment
   *
   * @param {object} recipe
   * The experiment recipe.
   *
   * @param {string} branchSlug
   * The slug of the branch to enroll in. This must exist in the recipe.
   *
   * @param {string} source
   * The source associated with the enrollment.
   *
   * @param {object} extra
   * Additional properties to set on the enrollment.
   *
   * @param {Map<string, unknown> | undefined} originalPrefValues
   * When creating a Firefox labs opt-in, this option allows Labs to specify
   * original values for prefs.
   *
   * @returns {CreateEnrollmentResult}
   * The resulting enrollment and the prefs that should be set.
   *
   * @throws {Error}
   * If the branch does not exist.
   */
  createEnrollment(
    recipe,
    branchSlug,
    source,
    extra = {},
    originalPrefValues = undefined
  ) {
    const enrollment = ExperimentManager.createIncompleteEnrollment(
      recipe,
      branchSlug,
      source,
      extra
    );

    let prefsToSet = null;
    if (enrollment.active) {
      this._prefFlips._annotateEnrollment(enrollment);

      const result = this._getPrefsForBranch(
        enrollment.branch,
        recipe.isRollout,
        originalPrefValues
      );

      enrollment.prefs = result.prefs;
      prefsToSet = result.prefsToSet;
    }

    return { enrollment, prefsToSet };
  }

  /**
   * Create an incomplete enrollment.
   *
   * The enrollment will not be annotated with correct values for the `prefs` or
   * `prefFlips` fields.
   *
   * This function exists to provide a common implementation for creating
   * enrollments in {@link ExperimentManager.createEnrollment} and tests.
   *
   * @param {object} recipe
   * The recipe.
   *
   * @param {string} branchSlug
   * The slug of the branch to enroll in. This must exist in the recipe.
   *
   * @param {string} source
   * The source associated with the enrollment.
   *
   * @param {object} extra
   * Additional properties to set on the enrollment.
   *
   * @returns {object}
   * An incomplete enrollment.
   */
  static createIncompleteEnrollment(recipe, branchSlug, source, extra = {}) {
    const branch = recipe.branches.find(b => b.slug === branchSlug);
    if (typeof branch === "undefined") {
      throw new Error(`${recipe.slug}: no such branch ${branchSlug}`);
    }

    const { active = true, ...rest } = extra;

    const {
      slug,
      userFacingName,
      userFacingDescription,
      featureIds,
      isRollout = false,
      localizations = null,
      isFirefoxLabsOptIn,
      firefoxLabsTitle,
      firefoxLabsDescription,
      firefoxLabsDescriptionLinks,
      firefoxLabsGroup,
      requiresRestart,
    } = recipe;

    const enrollment = Object.assign(
      {
        slug,
        source,
        userFacingName,
        userFacingDescription,
        lastSeen: new Date().toJSON(),
        featureIds,
        isRollout,
        active,
        branch,
        localizations,
      },
      typeof isFirefoxLabsOptIn !== "undefined"
        ? {
            isFirefoxLabsOptIn,
            firefoxLabsTitle,
            firefoxLabsDescription,
            firefoxLabsDescriptionLinks,
            firefoxLabsGroup,
            requiresRestart,
          }
        : {},
      rest
    );

    if (
      !enrollment.active &&
      typeof enrollment.unenrollReason === "undefined"
    ) {
      enrollment.unenrollReason = lazy.NimbusTelemetry.UnenrollReason.UNKNOWN;
    }

    return enrollment;
  }

  /**
   * Force enrollment in a recipe.
   *
   * The resulting enrollment will have a slug prefixed with `optin-` to
   * distinguish it from regular enrollments in telemetry.
   *
   * @param {object} recipe The recipe to enroll in.
   * @param {object|string} branchOrBranchSlug Either the slug of the branch to
   * enroll in or the branch object.
   *
   * @returns {object} The resulting enrollment.
   */
  forceEnroll(recipe, branchOrBranchSlug) {
    let branch;
    if (typeof branchOrBranchSlug === "string") {
      branch = recipe.branches.find(b => b.slug === branchOrBranchSlug);

      if (!branch) {
        throw new Error(
          `Could not force enroll into ${recipe.slug}: no such branch ${branchOrBranchSlug}`
        );
      }
    } else {
      lazy.log.warn(
        "forceEnroll with an object branch is deprecated and will be removed in a future version"
      );
      branch = branchOrBranchSlug;
    }

    const result = this.canEnroll(recipe);
    if (!result.ok) {
      switch (result.reason) {
        case CannotEnrollFeatureReason.ENROLLMENT_PAUSED:
          // Ignore this reason.
          break;

        case CannotEnrollFeatureReason.DOES_NOT_EXIST:
          throw new Error(
            `Cannot enroll in recipe ${recipe.slug}: feature ${result.featureId} does not exist`
          );

        case CannotEnrollFeatureReason.ENROLLED_IN_FEATURE:
          for (const conflictingSlug of result.conflictingEnrollments) {
            lazy.log.debug(
              `Existing ${
                recipe.isRollout ? "rollout" : "experiment"
              } ${conflictingSlug} found for the same feature, unenrolling.`
            );
            this.unenroll(
              conflictingSlug,
              UnenrollmentCause.fromReason(
                lazy.NimbusTelemetry.UnenrollReason.FORCE_ENROLLMENT
              )
            );
          }
          break;
      }
    }

    const optInRecipe = structuredClone(recipe);
    optInRecipe.userFacingName = `${recipe.userFacingName} - Forced enrollment`;
    optInRecipe.slug = `optin-${recipe.slug}`;

    // If there is an existing active enrollment with this slug, we must
    // unenroll from it first, otherwise _enroll() will overwrite it without
    // going through the appropriate flow (e.g., updating the enrollment store
    // and triggering update callbacks).
    const existingEnrollment = this.store.get(optInRecipe.slug);
    if (existingEnrollment?.active) {
      // We need only unenroll -- when we call _enroll() below, we will
      // overwrite the existing enrollment.
      this.unenroll(
        optInRecipe.slug,
        UnenrollmentCause.fromReason(
          lazy.NimbusTelemetry.UnenrollReason.FORCE_ENROLLMENT
        )
      );
    }

    // If there is an existing Firefox Labs entry for a recipe with this slug,
    // we must remove it because we are replacing the enrollment.
    this.unregisterOptIn(optInRecipe.slug);

    const enrollment = this._enroll(
      optInRecipe,
      branch.slug,
      lazy.NimbusTelemetry.EnrollmentSource.FORCE_ENROLLMENT
    );

    // The entry must be registered *after* enrollment so that the new
    // enrollment lines up correctly with the recipe.
    if (optInRecipe.isFirefoxLabsOptIn) {
      this.registerOptIn(
        optInRecipe,
        lazy.NimbusTelemetry.EnrollmentSource.FORCE_ENROLLMENT
      );
    }

    Services.obs.notifyObservers(
      null,
      "nimbus:enrollments-updated",
      optInRecipe.slug
    );

    return enrollment;
  }

  /**
   * Update an existing enrollment.
   *
   * @param {object} enrollment
   *        The enrollment to update.
   *
   * @param {object?} recipe
   *        The recipe to update the enrollment with, if any
   *
   * @param {string} source
   *        The source of the recipe, e.g., "rs-loader".
   *
   * @param {object} result
   *        The result of validation, targeting, and bucketing.
   *
   *        See `CheckRecipeResult` for details.
   */
  async updateEnrollment(enrollment, recipe, source, result) {
    const { EnrollmentStatus, EnrollmentStatusReason, UnenrollReason } =
      lazy.NimbusTelemetry;

    if (enrollment.source !== source) {
      // In practice this function is only called with source == "rs-loader".
      // Therefore this condition can only really happen if the user has
      // force-enrolled into an experiment via the console or nimbus-devtools.
      //
      // Either way their state is "corrupted" and the only way to fix it is to
      // manually delete the entry from the enrollment database.
      //
      // Report the error and move on.
      lazy.log.error(
        `Refusing to update enrollment for recipe ${recipe.slug} from source ${source}: the existing enrollment has a different source (${enrollment.source})`
      );
      return;
    }

    if (result.ok) {
      // Unenrollment due to studies or rollouts becoming disabled are handled in
      // `_handleStudiesOptOut` or `_handleRolloutsOptOut` respectively.
      if (result.status === lazy.MatchStatus.DISABLED) {
        this.store._db?.recordEnrollmentStatus(recipe.slug, {
          status: EnrollmentStatus.NOT_ENROLLED,
          reason: EnrollmentStatusReason.OPT_OUT,
        });
        return;
      }

      if (recipe?.isFirefoxLabsOptIn && !this.registerOptIn(recipe, source)) {
        // This *should* be unreachable because:
        //
        // * we have already returned if enrollment.source !== source;
        // * this function is in practice only called with source = "rs-loader"
        //   (either directly or indirectly from
        //   RemoteSettingsExerimentLoader.updateRecipes())
        // * we clear the opt-in list for "rs-loader" in updateRecipes(); and
        // * slugs are unique across Remote Settings records.
        //
        // However, the user could be playing with devtools at precisely the
        // wrong time, so we must make sure to handle this gracefully.

        lazy.log.error(
          `Unexpected error: could not register opt-in with slug ${recipe.slug}: an opt-in already exists with that slug`
        );
        return;
      }
    }

    if (enrollment.active) {
      if (!result.ok) {
        // If the recipe failed validation then we must unenroll.
        this._unenroll(
          enrollment,
          UnenrollmentCause.fromCheckRecipeResult(result)
        );
        return;
      }

      if (result.status === lazy.MatchStatus.NOT_SEEN) {
        // If the recipe was not present in the source we must unenroll.
        this._unenroll(
          enrollment,
          UnenrollmentCause.fromCheckRecipeResult(result)
        );
        return;
      }

      if (!recipe.branches.find(b => b.slug === enrollment.branch.slug)) {
        // Our branch has been removed so we must unenroll.
        //
        // This should not happen in practice.
        this._unenroll(
          enrollment,
          UnenrollmentCause.fromReason(UnenrollReason.BRANCH_REMOVED)
        );
        return;
      }

      if (result.status === lazy.MatchStatus.NO_MATCH) {
        // If we have an active enrollment and we no longer match targeting we
        // must unenroll.
        this._unenroll(
          enrollment,
          UnenrollmentCause.fromCheckRecipeResult(result)
        );
        return;
      }

      if (
        enrollment.isRollout &&
        result.status === lazy.MatchStatus.TARGETING_ONLY
      ) {
        // If we no longer fall in the bucketing allocation for this rollout we
        // must unenroll.
        this._unenroll(
          enrollment,
          UnenrollmentCause.fromCheckRecipeResult(result)
        );
        return;
      }

      if (result.status === lazy.MatchStatus.UNENROLLED_IN_ANOTHER_PROFILE) {
        this._unenroll(
          enrollment,
          UnenrollmentCause.fromCheckRecipeResult(result)
        );
        return;
      }

      if (result.status === lazy.MatchStatus.TARGETING_AND_BUCKETING) {
        this.store._db?.recordEnrollmentStatus(enrollment.slug, {
          branch: enrollment.branch.slug,
          status: EnrollmentStatus.ENROLLED,
          reason: EnrollmentStatusReason.QUALIFIED,
        });
      }

      // Either this recipe is not a rollout or both targeting matches and we
      // are in the bucket allocation. For the former, we do not re-evaluate
      // bucketing for experiments because the bucketing cannot change. For the
      // latter, we are already active so we don't need to enroll.
      return;
    }

    if (!enrollment.isRollout || enrollment.isFirefoxLabsOptIn) {
      // We can only re-enroll into rollouts and we do not enroll directly into
      // Firefox Labs Opt-Ins.
      return;
    }

    if (
      !enrollment.active &&
      result.status === lazy.MatchStatus.TARGETING_AND_BUCKETING &&
      [
        UnenrollReason.BUCKETING,
        UnenrollReason.TARGETING_MISMATCH,
        UnenrollReason.ROLLOUTS_OPT_OUT,
        UnenrollReason.STUDIES_OPT_OUT,
        UnenrollReason.RECIPE_NOT_SEEN,
      ].includes(enrollment.unenrollReason)
    ) {
      // We only re-enroll if we match targeting and bucketing and the unenroll
      // reason is one of the above reasons.
      lazy.log.debug(`Re-enrolling in rollout "${recipe.slug}`);
      await this.enroll(recipe, source, { reenroll: true });
    }
  }

  /**
   * Stop an enrollment that is currently active
   *
   * @param {string} slug
   *        The slug of the enrollment to stop.
   * @param {object?} cause
   *        The cause of this unenrollment. All non-object causes will be
   *        coerced into the "unknown" reason.
   *
   *        See `UnenrollCause` for details.
   */
  unenroll(slug, cause) {
    const enrollment = this.store.get(slug);
    if (!enrollment) {
      lazy.NimbusTelemetry.recordUnenrollmentFailure(
        slug,
        lazy.NimbusTelemetry.UnenrollmentFailureReason.DOES_NOT_EXIST
      );
      lazy.log.error(`Could not find an experiment with the slug "${slug}"`);
      return null;
    }

    return this._unenroll(
      enrollment,
      typeof cause === "object" && cause !== null
        ? cause
        : UnenrollmentCause.Unknown()
    );
  }

  /**
   * Stop an enrollment that is currently active.
   *
   * @param {Enrollment} enrollment
   *        The enrollment to end.
   *
   * @param {object} cause
   *        The cause of this unenrollment.
   *
   *        See `UnenrollmentCause` for details.
   *
   * @param {object?} options
   *
   * @param {boolean} options.duringRestore
   *        If true, this indicates that this was during the call to
   *        `_restoreEnrollmentPrefs`.
   *
   * @param {boolean} options.unsetEnrollmentPrefs
   *        Whether or not to unset the prefs set by enrollment.
   */
  _unenroll(
    enrollment,
    cause,
    { duringRestore = false, unsetEnrollmentPrefs = true } = {}
  ) {
    const { slug } = enrollment;

    if (!enrollment.active) {
      lazy.NimbusTelemetry.recordUnenrollmentFailure(
        slug,
        lazy.NimbusTelemetry.UnenrollmentFailureReason.ALREADY_UNENROLLED
      );
      throw new Error(
        `Cannot stop experiment "${slug}" because it is already expired`
      );
    }

    this.store.deactivateEnrollment(slug, cause.reason);

    const enrollmentStatus = lazy.NimbusTelemetry.recordUnenrollment(
      enrollment,
      cause
    );
    this.store._db?.recordEnrollmentStatus(enrollment.slug, enrollmentStatus);

    if (unsetEnrollmentPrefs) {
      this._unsetEnrollmentPrefs(enrollment, cause, { duringRestore });
    } else if (enrollment.prefs) {
      // If we're not unsetting enrollment prefs, we must remove our listeners.
      for (const pref of enrollment.prefs) {
        this._removePrefObserver(pref.name, enrollment.slug);
      }
    }

    lazy.log.debug(`Recipe unenrolled: ${slug} (${cause.reason})`);
  }

  /**
   * Unenroll from all active rollouts if user opts out.
   */
  _handleRolloutsOptOut() {
    const enrollments = this.store
      .getAll()
      .filter(e => e.active && !e.isFirefoxLabsOptIn && e.isRollout);

    for (const enrollment of enrollments) {
      this._unenroll(
        enrollment,
        UnenrollmentCause.fromReason(
          lazy.NimbusTelemetry.UnenrollReason.ROLLOUTS_OPT_OUT
        )
      );
    }
  }

  /**
   * Unenroll from all active studies if user opts out.
   */
  _handleStudiesOptOut() {
    const enrollments = this.store
      .getAll()
      .filter(e => e.active && !e.isFirefoxLabsOptIn && !e.isRollout);

    for (const enrollment of enrollments) {
      this._unenroll(
        enrollment,
        UnenrollmentCause.fromReason(
          lazy.NimbusTelemetry.UnenrollReason.STUDIES_OPT_OUT
        )
      );
    }
  }

  /**
   * Unenroll from all active Firefox Labs opt-ins if Labs becomes disabled.
   */
  _handleLabsDisabled() {
    const enrollments = this.store
      .getAll()
      .filter(e => e.active && e.isFirefoxLabsOptIn);

    for (const enrollment of enrollments) {
      this._unenroll(
        enrollment,
        UnenrollmentCause.fromReason(
          lazy.NimbusTelemetry.UnenrollReason.LABS_DISABLED
        )
      );
    }

    this.optIns = [];
  }

  /**
   * Generate Normandy UserId respective to a branch
   * for a given experiment.
   *
   * @param {string} slug
   * @param {Array<{slug: string; ratio: number}>} branches
   * @param {string} namespace
   * @param {number} start
   * @param {number} count
   * @param {number} total
   * @returns {Promise<{[branchName: string]: string}>} An object where
   * the keys are branch names and the values are user IDs that will enroll
   * a user for that particular branch. Also includes a `notInExperiment` value
   * that will not enroll the user in the experiment if not 100% enrollment.
   */
  async generateTestIds(recipe) {
    // Older recipe structure had bucket config values at the top level while
    // newer recipes group them into a bucketConfig object
    const { slug, branches, namespace, start, count, total } = {
      ...recipe,
      ...recipe.bucketConfig,
    };
    const branchValues = {};
    const includeNot = count < total;

    if (!slug || !namespace) {
      throw new Error(`slug, namespace not in expected format`);
    }

    if (!(start < total && count <= total)) {
      throw new Error("Must include start, count, and total as integers");
    }

    if (
      !Array.isArray(branches) ||
      branches.filter(branch => branch.slug && branch.ratio).length !==
        branches.length
    ) {
      throw new Error("branches parameter not in expected format");
    }

    while (Object.keys(branchValues).length < branches.length + includeNot) {
      const id = lazy.NormandyUtils.generateUuid();
      const enrolls = await lazy.Sampling.bucketSample(
        [id, namespace],
        start,
        count,
        total
      );
      // Does this id enroll the user in the experiment
      if (enrolls) {
        // Choose a random branch
        const { slug: pickedBranch } = await this.chooseBranch(
          slug,
          branches,
          id
        );

        if (!Object.keys(branchValues).includes(pickedBranch)) {
          branchValues[pickedBranch] = id;
          lazy.log.debug(`Found a value for "${pickedBranch}"`);
        }
      } else if (!branchValues.notInExperiment) {
        branchValues.notInExperiment = id;
      }
    }
    return branchValues;
  }

  /**
   * Choose a branch randomly.
   *
   * @param {string} slug
   * @param {Branch[]} branches
   * @param {string} userId
   * @returns {Promise<Branch>}
   */
  async chooseBranch(
    slug,
    branches,
    userId = lazy.ClientEnvironmentBase.legacyRandomizationId
  ) {
    const ratios = branches.map(({ ratio = 1 }) => ratio);

    // It's important that the input be:
    // - Unique per-user (no one is bucketed alike)
    // - Unique per-experiment (bucketing differs across multiple experiments)
    // - Differs from the input used for sampling the recipe (otherwise only
    //   branches that contain the same buckets as the recipe sampling will
    //   receive users)
    const input = `experimentmanager-${userId}-${slug}-branch`;

    const index = await lazy.Sampling.ratioSample(input, ratios);
    return branches[index];
  }

  /**
   * An annotation generated for a setPref variable for an enrollment.
   *
   * @typedef {object} SetPrefAnnotation
   *
   * @property {string} name
   * The name of the pref.
   *
   * @property {"user"|"default"}
   * The branch the pref is to be set on.
   *
   * @property {string} featureId
   * The featureId of the variable controlling this pref.
   *
   * @property {string} variable
   * The variable controlling this pref.
   *
   * @property {string|number|boolean|null} originalvalue
   * The original value of the pref.
   */

  /**
   * Information about a pref that should be set upon enrollment in a recipe.
   *
   * @typedef {object} PrefToSet
   *
   * @property {string} name
   * The name of the pref.
   *
   * @property {string|number|boolean} value
   * The value of the pref.
   *
   * @property {"user"|"default"} prefBranch
   * The branch on which the pref should be set.
   */

  /**
   * Information about what prefs should be set as a result of enrollment in a
   * specific branch.
   *
   * @typedef {object} PrefsForBranch
   *
   * @property {SetPrefAnnotation[]} prefs
   * Pref annotations to be added to the enrollment.
   *
   * This list will include prefs that will not be set because the enrollment
   * corresponds to a rollout and there is an active experiment controlling the
   * same pref.
   *
   * @property {PrefToSet[]} prefsToSet
   * Prefs that should be set upon enrollment.
   */

  /**
   * Generate the list of prefs a recipe will set.
   *
   * @param {object} branch
   * The recipe branch that will be enrolled.
   *
   * @param {boolean} isRollout
   * Whether or not this recipe is a rollout.
   *
   * @param {Map<string, unknown> | undefined} originalPrefValues
   * If provided, this will be queried for the original values of prefs before
   * any other source (existing enrollments, actual pref values).
   *
   * @returns {PrefsForBranch}
   */
  _getPrefsForBranch(
    branch,
    isRollout = false,
    originalPrefValues = undefined
  ) {
    const prefs = [];
    const prefsToSet = [];

    const getConflictingEnrollment = this._makeEnrollmentCache(isRollout);

    for (const { featureId, value: featureValue } of branch.features) {
      const feature = lazy.NimbusFeatures[featureId];

      if (!feature) {
        continue;
      }

      // It is possible to enroll in both an experiment and a rollout, so we
      // need to check if we have another enrollment for the same feature.
      const conflictingEnrollment = getConflictingEnrollment(featureId);

      for (let [variable, value] of Object.entries(featureValue)) {
        const setPref = feature.getSetPref(variable);

        if (setPref) {
          const { pref: prefName, branch: prefBranch } = setPref;

          let originalValue;
          let conflictingPref = undefined;

          if (originalPrefValues?.has(prefName)) {
            originalValue = originalPrefValues.get(prefName);
          } else {
            conflictingPref = conflictingEnrollment?.prefs?.find(
              p => p.name === prefName
            );

            if (conflictingPref) {
              // If there is another enrollment that has already set the pref we
              // care about, we use its stored originalValue.
              originalValue = conflictingPref.originalValue;
            } else {
              // If there is an active prefFlips experiment for this pref on this
              // branch, we must use its originalValue.
              const prefFlipValue = this._prefFlips._getOriginalValue(
                prefName,
                prefBranch
              );
              if (typeof prefFlipValue !== "undefined") {
                originalValue = prefFlipValue;
              } else {
                originalValue = lazy.PrefUtils.getPrefStrict(
                  prefName,
                  prefBranch
                );
              }
            }
          }

          prefs.push({
            name: prefName,
            branch: prefBranch,
            featureId,
            variable,
            originalValue,
          });

          // An experiment takes precedence if there is already a pref set.
          if (!isRollout || !conflictingPref) {
            if (
              lazy.NimbusFeatures[featureId].manifest.variables[variable]
                .type === "json"
            ) {
              value = JSON.stringify(value);
            }

            prefsToSet.push({
              name: prefName,
              value,
              prefBranch,
            });
          }
        }
      }
    }

    return { prefs, prefsToSet };
  }

  /**
   * Set a list of prefs from enrolling in an experiment or rollout.
   *
   * The ExperimentManager's pref observers will be disabled while setting each
   * pref so as not to accidentally unenroll an existing rollout that an
   * experiment would override.
   *
   * @param {PrefToSet[]} prefsToSet
   * An array of the prefs that should be set.
   */
  _setEnrollmentPrefs(prefsToSet) {
    for (const { name, value, prefBranch } of prefsToSet) {
      const entry = this._prefs.get(name);

      // If another enrollment exists that has set this pref, temporarily
      // disable the pref observer so as not to cause unenrollment.
      if (entry) {
        entry.enrollmentChanging = true;
      }

      lazy.PrefUtils.setPref(name, value, { branch: prefBranch });

      if (entry) {
        entry.enrollmentChanging = false;
      }
    }
  }

  /**
   * Unset prefs set during this enrollment.
   *
   * If this enrollment is an experiment and there is an existing rollout that
   * would set a pref that was covered by this enrollment, the pref will be
   * updated to that rollout's value.
   *
   * Otherwise, it will be set to the original value from before the enrollment
   * began.
   *
   * @param {object} enrollment
   *        The enrollment that has ended.
   *
   * @param {object} cause
   *        The cause of the unenrollment.
   *
   *        See `UnenrollmentCause` for details.
   *
   * @param {object} options
   *
   * @param {boolean} options.duringRestore
   *        The unenrollment was caused during restore.
   */
  _unsetEnrollmentPrefs(enrollment, cause, { duringRestore } = {}) {
    if (!enrollment.prefs?.length) {
      return;
    }

    const getConflictingEnrollment = this._makeEnrollmentCache(
      enrollment.isRollout
    );

    for (const pref of enrollment.prefs) {
      this._removePrefObserver(pref.name, enrollment.slug);

      if (
        cause.reason === lazy.NimbusTelemetry.UnenrollReason.CHANGED_PREF &&
        cause.changedPref.name === pref.name &&
        cause.changedPref.branch === pref.branch
      ) {
        // Resetting the original value would overwite the pref the user just
        // set. Skip it.
        continue;
      }

      let newValue = pref.originalValue;

      // If we are unenrolling from an experiment during a restore, we must
      // ignore any potential conflicting rollout in the store, because its
      // hasn't gone through `_restoreEnrollmentPrefs`, which might also cause
      // it to unenroll.
      //
      // Both enrollments will have the same `originalValue` stored, so it will
      // always be restored.
      if (!duringRestore || enrollment.isRollout) {
        const conflictingEnrollment = getConflictingEnrollment(pref.featureId);
        const conflictingPref = conflictingEnrollment?.prefs?.find(
          p => p.name === pref.name
        );

        if (conflictingPref) {
          if (enrollment.isRollout) {
            // If we are unenrolling from a rollout, we have an experiment that
            // has set the pref. Since experiments take priority, we do not unset
            // it.
            continue;
          } else {
            // If we are an unenrolling from an experiment, we have a rollout that would
            // set the same pref, so we update the pref to that value instead of
            // the original value.
            newValue = ExperimentManager.getFeatureConfigFromBranch(
              conflictingEnrollment.branch,
              pref.featureId
            ).value[pref.variable];
          }
        }
      }

      // If another enrollment exists that has set this pref, temporarily
      // disable the pref observer so as not to cause unenrollment when we
      // update the pref to its value.
      const entry = this._prefs.get(pref.name);
      if (entry) {
        entry.enrollmentChanging = true;
      }

      lazy.PrefUtils.setPref(pref.name, newValue, {
        branch: pref.branch,
      });

      if (entry) {
        entry.enrollmentChanging = false;
      }
    }
  }

  /**
   * Restore the prefs set by an enrollment.
   *
   * @param {object} enrollment The enrollment.
   * @param {object} enrollment.branch The branch that was enrolled.
   * @param {object[]} enrollment.prefs The prefs that are set by the enrollment.
   * @param {object[]} enrollment.isRollout The prefs that are set by the enrollment.
   *
   * @returns {Promise<boolean>} Whether the restore was successful. If false, the
   *                             enrollment has ended.
   */
  async _restoreEnrollmentPrefs(enrollment) {
    const { UnenrollReason } = lazy.NimbusTelemetry;

    const { branch, prefs = [], isRollout } = enrollment;

    if (!prefs?.length) {
      return false;
    }

    const featuresById = Object.fromEntries(
      branch.features.map(f => [f.featureId, f])
    );

    for (const { name, featureId, variable } of prefs) {
      // If the feature no longer exists, unenroll.
      if (!Object.hasOwn(lazy.NimbusFeatures, featureId)) {
        this._unenroll(
          enrollment,
          UnenrollmentCause.fromReason(UnenrollReason.INVALID_FEATURE),
          { duringRestore: true }
        );
        return false;
      }

      const variables = lazy.NimbusFeatures[featureId].manifest.variables;

      // If the feature is missing a variable that set a pref, unenroll.
      if (!Object.hasOwn(variables, variable)) {
        this._unenroll(
          enrollment,
          UnenrollmentCause.fromReason(UnenrollReason.PREF_VARIABLE_MISSING),
          { duringRestore: true }
        );
        return false;
      }

      const variableDef = variables[variable];

      // If the variable is no longer a pref-setting variable, unenroll.
      if (!Object.hasOwn(variableDef, "setPref")) {
        this._unenroll(
          enrollment,
          UnenrollmentCause.fromReason(UnenrollReason.PREF_VARIABLE_NO_LONGER),
          { duringRestore: true }
        );
        return false;
      }

      // If the variable is setting a different preference, unenroll.
      const prefName =
        typeof variableDef.setPref === "object"
          ? variableDef.setPref.pref
          : variableDef.setPref;

      if (prefName !== name) {
        this._unenroll(
          enrollment,
          UnenrollmentCause.fromReason(UnenrollReason.PREF_VARIABLE_CHANGED),
          { duringRestore: true }
        );
        return false;
      }
    }

    for (const { name, branch: prefBranch, featureId, variable } of prefs) {
      // User prefs are already persisted.
      if (prefBranch === "user") {
        continue;
      }

      // If we are a rollout, we need to check for an existing experiment that
      // has set the same pref. If so, we do not need to set the pref because
      // experiments take priority.
      if (isRollout) {
        const conflictingEnrollment =
          this.store.getExperimentForFeature(featureId);
        const conflictingPref = conflictingEnrollment?.prefs?.find(
          p => p.name === name
        );

        if (conflictingPref) {
          continue;
        }
      }

      let value = featuresById[featureId].value[variable];
      if (
        lazy.NimbusFeatures[featureId].manifest.variables[variable].type ===
        "json"
      ) {
        value = JSON.stringify(value);
      }

      if (prefBranch !== "user") {
        lazy.PrefUtils.setPref(name, value, { branch: prefBranch });
      }
    }

    return true;
  }

  /**
   * Make a cache to look up enrollments of the oppposite kind by feature ID.
   *
   * @param {boolean} isRollout Whether or not the current enrollment is a
   *                            rollout. If true, the cache will return
   *                            experiments. If false, the cache will return
   *                            rollouts.
   *
   * @returns {function} The cache, as a callable function.
   */
  _makeEnrollmentCache(isRollout) {
    const getOtherEnrollment = (
      isRollout
        ? this.store.getExperimentForFeature
        : this.store.getRolloutForFeature
    ).bind(this.store);

    const conflictingEnrollments = {};
    return featureId => {
      if (!Object.hasOwn(conflictingEnrollments, featureId)) {
        conflictingEnrollments[featureId] = getOtherEnrollment(featureId);
      }

      return conflictingEnrollments[featureId];
    };
  }

  /**
   * Update the set of observers with prefs set by the given enrollment.
   *
   * @param {Enrollment} enrollment
   *        The enrollment that is setting prefs.
   */
  _updatePrefObservers({ slug, prefs }) {
    if (!prefs?.length) {
      return;
    }

    for (const pref of prefs) {
      const { name } = pref;

      if (!this._prefs.has(name)) {
        const observer = (aSubject, aTopic, aData) => {
          // This observer will be called for changes to `name` as well as any
          // other pref that begins with `name.`, so we have to filter to
          // exactly the pref we care about.
          if (aData === name) {
            this._onExperimentPrefChanged(pref);
          }
        };
        const entry = {
          slugs: new Set([slug]),
          enrollmentChanging: false,
          observer,
        };

        Services.prefs.addObserver(name, observer);

        this._prefs.set(name, entry);
      } else {
        this._prefs.get(name).slugs.add(slug);
      }

      if (!this._prefsBySlug.has(slug)) {
        this._prefsBySlug.set(slug, new Set([name]));
      } else {
        this._prefsBySlug.get(slug).add(name);
      }
    }
  }

  /**
   * Remove an entry for the pref observer for the given pref and slug.
   *
   * If there are no more enrollments listening to a pref, the observer will be removed.
   *
   * This is called when an enrollment is ending.
   *
   * @param {string} name The name of the pref.
   * @param {string} slug The slug of the enrollment that is being unenrolled.
   */
  _removePrefObserver(name, slug) {
    /// This may be called before the ExperimentManager has finished initializing.
    if (!this._prefs) {
      return;
    }

    // Update the pref observer that the current enrollment is no longer
    // involved in the pref.
    //
    // If no enrollments have a variable setting the pref, then we can remove
    // the observers.
    const entry = this._prefs.get(name);

    // If this is happening due to a pref change, the observers will already be removed.
    if (entry) {
      entry.slugs.delete(slug);
      if (entry.slugs.size == 0) {
        Services.prefs.removeObserver(name, entry.observer);
        this._prefs.delete(name);
      }
    }

    const bySlug = this._prefsBySlug.get(slug);
    if (bySlug) {
      bySlug.delete(name);
      if (bySlug.size == 0) {
        this._prefsBySlug.delete(slug);
      }
    }
  }

  /**
   * Handle a change to a pref set by enrollments by ending those enrollments.
   *
   * @param {object} pref
   *        Information about the pref that was changed.
   *
   * @param {string} pref.name
   *        The name of the pref that was changed.
   *
   * @param {string} pref.branch
   *        The branch enrollments set the pref on.
   *
   * @param {string} pref.featureId
   *        The feature ID of the feature containing the variable that set the
   *        pref.
   *
   * @param {string} pref.variable
   *        The variable in the given feature whose value determined the pref's
   *        value.
   */
  _onExperimentPrefChanged(pref) {
    const entry = this._prefs.get(pref.name);
    // If this was triggered while we are enrolling or unenrolling from an
    // experiment, then we don't want to unenroll from the rollout because the
    // experiment's value is taking precendence.
    //
    // Otherwise, all enrollments that set the variable corresponding to this
    // pref must be unenrolled.
    if (entry.enrollmentChanging) {
      return;
    }

    // Copy the `Set` into an `Array` because we modify the set later in
    // `_removePrefObserver` and we need to iterate over it multiple times.
    const slugs = Array.from(entry.slugs);

    // Remove all pref observers set by enrollments. We are potentially about
    // to set these prefs during unenrollment, so we don't want to trigger
    // them and cause nested unenrollments.
    for (const slug of slugs) {
      const toRemove = Array.from(this._prefsBySlug.get(slug) ?? []);
      for (const name of toRemove) {
        this._removePrefObserver(name, slug);
      }
    }

    // Unenroll from the rollout first to save calls to setPref.
    const enrollments = Array.from(slugs).map(slug => this.store.get(slug));

    // There is a maximum of two enrollments (one experiment and one rollout).
    if (enrollments.length == 2) {
      // Order enrollments so that we unenroll from the rollout first.
      if (!enrollments[0].isRollout) {
        enrollments.reverse();
      }
    }

    const feature = ExperimentManager.getFeatureConfigFromBranch(
      enrollments.at(-1).branch,
      pref.featureId
    );

    const cause = UnenrollmentCause.ChangedPref(
      {
        name: pref.name,
        branch: lazy.PrefFlipsFeature.determinePrefChangeBranch(
          pref.name,
          pref.branch,
          feature.value[pref.variable]
        ),
      },
      this.isPrefBeingChangedViaAboutConfig(pref.name)
    );

    for (const enrollment of enrollments) {
      this._unenroll(enrollment, cause);
    }
  }

  /**
   * Handle a potential conflict between a setPref experiment and a prefFlips
   * rollout.
   *
   * This should only be called by this manager's `PrefFlipsFeature` instance.
   *
   * @param {string} conflictingSlug
   *        The enrolling prefFlips slug.
   *
   * @param {[string, PrefBranch][]>} prefs
   *        The prefs that will be set by the pref flip experiment, along with
   *        the branch each pref will be set on.
   *
   * @returns {Record<string, PrefValue>}
   *          The original values of any prefs that were being set by setPref
   *          enrollments.
   */
  _handlePrefFlipsConflict(conflictingSlug, prefs) {
    const originalValues = {};

    for (const [pref, branch] of prefs) {
      const entry = this._prefs.get(pref);

      if (!entry) {
        continue;
      }

      // We are going to unenroll even if the setPref experiment was using the
      // same pref on a different branch.
      for (const slug of entry.slugs) {
        const enrollment = this.store.get(slug);

        // The branch and originalValue are not stored in the entry, but are
        // instead stored on the enrollment.
        if (!Object.hasOwn(originalValues, pref)) {
          const prefInfo = enrollment.prefs.find(
            p => p.name === pref && p.branch === branch
          );

          if (prefInfo) {
            originalValues[pref] = prefInfo.originalValue;
          }
        }

        this._unenroll(
          enrollment,
          UnenrollmentCause.PrefFlipsConflict(conflictingSlug)
        );
      }
    }

    return originalValues;
  }

  /**
   * Return whether or not the given pref is being changed by a user on
   * about:config.
   *
   * @param {string} pref The preference to check.
   *
   * @returns {boolean}
   */
  isPrefBeingChangedViaAboutConfig(pref) {
    return this.#aboutConfigObserver.isBeingChanged(pref);
  }

  /**
   * Clear the opt-in list.
   *
   * @param {string} source
   * Only recipes from this source will be removed.
   * @param {object} options
   * @param {Set<string> | undefined} options.onlyFeatureIds
   * If provided, only recipes that contain at least one of the features in this
   * set will be removed.
   *
   * Otherwise, all recipes will be removed.
   */
  _clearOptIns(source, { onlyFeatureIds = undefined } = {}) {
    this.optIns = this.optIns.filter(
      entry =>
        source !== entry.source ||
        (typeof onlyFeatureIds !== "undefined" &&
          entry.recipe.featureIds.every(
            featureId => !onlyFeatureIds.has(featureId)
          ))
    );
  }

  /**
   * Register an opt-in recipe from a source.
   *
   * @param {object} recipe The recipe.
   * @param {string} source The source.
   *
   * @returns {boolean} True if the opt-in was registered or false if there was a conflict.
   */
  registerOptIn(recipe, source) {
    if (!recipe.isFirefoxLabsOptIn) {
      return false;
    }

    if (this.optIns.find(entry => entry.recipe.slug === recipe.slug)) {
      return false;
    }

    // Prevent enrollment if there is an existing enrollment that either does
    // not match the source or is not a Firefox Labs opt-in.
    const enrollment = this.store.get(recipe.slug);
    if (
      enrollment &&
      (enrollment.source !== source || !enrollment.isFirefoxLabsOptIn)
    ) {
      return false;
    }

    this.optIns.push({ recipe, source });
    return true;
  }

  /**
   * Unregister an opt-in recipe from a source.
   *
   * NB: This is only intended to be used during force enrollment or by
   * nimbus-devtools.
   *
   * @param {string} slug The slug of the recipe to remove.
   *
   * @returns {boolean} True if the opt-in was removed or false if it was not found.
   */
  unregisterOptIn(slug) {
    const index = this.optIns.findIndex(entry => entry.recipe.slug === slug);
    if (index >= 0) {
      this.optIns.splice(index, 1);
      return true;
    }

    return false;
  }

  /**
   * Return the feature configuration with the matching feature ID from the
   * given branch.
   *
   * @param {object} branch
   *        The branch object.
   *
   * @param {string} featureId
   *        The feature to search for.
   *
   * @returns {object}
   *          The feature configuration, including the feature ID and the value.
   */
  static getFeatureConfigFromBranch(branch, featureId) {
    return branch.features.find(f => f.featureId === featureId);
  }
}

const ABOUT_CONFIG_WILL_CHANGE_PREF_TOPIC = "about-config-will-change-pref";
const ABOUT_CONFIG_CHANGED_PREF_TOPIC = "about-config-changed-pref";

/**
 * Keep track of what prefs are being changed via about:config.
 */
class AboutConfigObserver {
  /**
   * Prefs that are currently being changed via about:config.
   *
   * @type Set<string>
   */
  #changes;

  constructor() {
    this.#changes = new Set();

    Services.obs.addObserver(this, ABOUT_CONFIG_WILL_CHANGE_PREF_TOPIC);
    Services.obs.addObserver(this, ABOUT_CONFIG_CHANGED_PREF_TOPIC);
  }

  /**
   * Handle a notification from about:config.
   *
   * @param {any} _subject Unused.
   * @param {string} topic The topic that indicates the rising vs. the falling
   * edge of the event.
   * @param {string} data The name of the pref being changed.
   * @returns
   */
  observe(_subject, topic, data) {
    switch (topic) {
      case ABOUT_CONFIG_WILL_CHANGE_PREF_TOPIC:
        this.#onWillChange(data);
        break;

      case ABOUT_CONFIG_CHANGED_PREF_TOPIC:
        this.#onChanged(data);
        break;
    }
  }

  /**
   * Record that a pref is about to change.
   *
   * @param {string} pref The pref.
   */
  #onWillChange(pref) {
    this.#changes.add(pref);
  }

  /**
   * Record that a pref has finished changing.
   *
   * @param {string} pref The pref.
   */
  #onChanged(pref) {
    this.#changes.delete(pref);
  }

  /**
   * Return whether or not a pref is in the process of being changed via
   * `about:config`.
   *
   * @param {string} pref The pref in question.
   *
   * @returns {boolean} Whether or not the pref is being changed.
   */
  isBeingChanged(pref) {
    return this.#changes.has(pref);
  }
}
