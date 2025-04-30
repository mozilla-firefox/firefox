/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { PrefFlipsFeature } from "resource://nimbus/lib/PrefFlipsFeature.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ClientEnvironment: "resource://normandy/lib/ClientEnvironment.sys.mjs",
  ClientID: "resource://gre/modules/ClientID.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  ExperimentStore: "resource://nimbus/lib/ExperimentStore.sys.mjs",
  FirstStartup: "resource://gre/modules/FirstStartup.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusTelemetry: "resource://nimbus/lib/Telemetry.sys.mjs",
  NormandyUtils: "resource://normandy/lib/NormandyUtils.sys.mjs",
  PrefUtils: "resource://normandy/lib/PrefUtils.sys.mjs",
  EnrollmentsContext:
    "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs",
  MatchStatus: "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs",
  Sampling: "resource://gre/modules/components-utils/Sampling.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  const { Logger } = ChromeUtils.importESModule(
    "resource://messaging-system/lib/Logger.sys.mjs"
  );
  return new Logger("ExperimentManager");
});

const TELEMETRY_DEFAULT_EXPERIMENT_TYPE = "nimbus";

const UPLOAD_ENABLED_PREF = "datareporting.healthreport.uploadEnabled";
const STUDIES_OPT_OUT_PREF = "app.shield.optoutstudies.enabled";

const STUDIES_ENABLED_CHANGED = "nimbus:studies-enabled-changed";

export const UnenrollmentCause = {
  fromCheckRecipeResult(result) {
    const { UnenrollReason } = lazy.NimbusTelemetry;

    let reason;

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

        // TARGETING_AND_BUCKETING cannot cause unenrollment.
      }
    } else {
      reason = result.reason;
    }

    return { reason };
  },

  fromReason(reason) {
    return { reason };
  },

  ChangedPref(pref) {
    return {
      reason: lazy.NimbusTelemetry.UnenrollReason.CHANGED_PREF,
      changedPref: pref,
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

  Unknown() {
    return {
      reason: lazy.NimbusTelemetry.UnenrollReason.UNKNOWN,
    };
  },
};

/**
 * A module for processes Experiment recipes, choosing and storing enrollment state,
 * and sending experiment-related Telemetry.
 */
export class _ExperimentManager {
  constructor({ id = "experimentmanager", store } = {}) {
    this.id = id;
    this.store = store || new lazy.ExperimentStore();
    this.optInRecipes = [];
    // By default, no extra context.
    this.extraContext = {};
    Services.prefs.addObserver(UPLOAD_ENABLED_PREF, this);
    Services.prefs.addObserver(STUDIES_OPT_OUT_PREF, this);

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
  }

  get studiesEnabled() {
    return (
      Services.prefs.getBoolPref(UPLOAD_ENABLED_PREF, false) &&
      Services.prefs.getBoolPref(STUDIES_OPT_OUT_PREF, false) &&
      Services.policies.isAllowed("Shield")
    );
  }

  /**
   * Creates a targeting context with following filters:
   *
   *   * `activeExperiments`: an array of slugs of all the active experiments
   *   * `isFirstStartup`: a boolean indicating whether or not the current enrollment
   *      is performed during the first startup
   *
   * @returns {Object} A context object
   * @memberof _ExperimentManager
   */
  createTargetingContext() {
    let context = {
      ...this.extraContext,

      isFirstStartup: lazy.FirstStartup.state === lazy.FirstStartup.IN_PROGRESS,

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
    this._prefs = new Map();
    this._prefsBySlug = new Map();
    this._prefFlips = new PrefFlipsFeature({ manager: this });

    await this.store.init();
    this.extraContext = extraContext;

    const restoredExperiments = this.store.getAllActiveExperiments();
    const restoredRollouts = this.store.getAllActiveRollouts();

    for (const experiment of restoredExperiments) {
      lazy.NimbusTelemetry.setExperimentActive(experiment);
      if (this._restoreEnrollmentPrefs(experiment)) {
        this._updatePrefObservers(experiment);
      }
    }
    for (const rollout of restoredRollouts) {
      lazy.NimbusTelemetry.setExperimentActive(rollout);
      if (this._restoreEnrollmentPrefs(rollout)) {
        this._updatePrefObservers(rollout);
      }
    }

    this._prefFlips.init();

    if (!this.studiesEnabled) {
      this._handleStudiesOptOut();
    }

    lazy.NimbusFeatures.nimbusTelemetry.onUpdate(() => {
      // Providing default values ensure we disable metrics when unenrolling.
      const cfg = {
        metrics_enabled: {
          "nimbus_targeting_environment.targeting_context_value": false,
          "nimbus_events.enrollment_status": false,
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

    if (result.ok && recipe.isFirefoxLabsOptIn) {
      this.optInRecipes.push(recipe);
    }

    if (!result.ok) {
      lazy.NimbusTelemetry.recordEnrollmentStatus({
        slug: recipe.slug,
        status: EnrollmentStatus.DISQUALIFIED,
        reason: EnrollmentStatusReason.ERROR,
        error_string: result.reason,
      });
      return;
    }

    if (recipe.isFirefoxLabsOptIn) {
      // We do not enroll directly into Firefox Labs opt-ins.
      return;
    }

    switch (result.status) {
      case lazy.MatchStatus.ENROLLMENT_PAUSED:
        lazy.NimbusTelemetry.recordEnrollmentStatus({
          slug: recipe.slug,
          status: EnrollmentStatus.NOT_ENROLLED,
          reason: EnrollmentStatusReason.ENROLLMENTS_PAUSED,
        });
        break;

      case lazy.MatchStatus.NO_MATCH:
        lazy.NimbusTelemetry.recordEnrollmentStatus({
          slug: recipe.slug,
          status: EnrollmentStatus.NOT_ENROLLED,
          reason: EnrollmentStatusReason.NOT_TARGETED,
        });
        break;

      case lazy.MatchStatus.TARGETING_ONLY:
        lazy.NimbusTelemetry.recordEnrollmentStatus({
          slug: recipe.slug,
          status: EnrollmentStatus.NOT_ENROLLED,
          reason: EnrollmentStatusReason.NOT_SELECTED,
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
   *
   */
  async getUserId(bucketConfig) {
    let id;
    if (bucketConfig.randomizationUnit === "normandy_id") {
      id = lazy.ClientEnvironment.userId;
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
   * Get all of the opt-in recipes that match targeting and bucketing.
   *
   * @returns opt in recipes
   */
  async getAllOptInRecipes() {
    const enrollmentsCtx = new lazy.EnrollmentsContext(this, null, {
      validationEnabled: false,
    });

    // RemoteSettingsExperimentLoader could be in a middle of updating recipes
    // so let's wait for the update to finish and this promise to resolve.
    await lazy.ExperimentAPI._rsLoader.finishedUpdating();

    // RemoteSettingsExperimentLoader should have finished updating at least
    // once. Prevent concurrent updates while we filter through the list of
    // available opt-in recipes.
    return lazy.ExperimentAPI._rsLoader.withUpdateLock(
      async () => {
        const filtered = [];

        for (const recipe of this.optInRecipes) {
          if (
            (await enrollmentsCtx.checkTargeting(recipe)) &&
            (await this.isInBucketAllocation(recipe.bucketConfig))
          ) {
            filtered.push(recipe);
          }
        }

        return filtered;
      },
      { mode: "shared" }
    );
  }

  /**
   * Get a single opt in recipe given its slug.
   *
   * @returns a single opt in recipe or undefined if not found.
   */
  async getSingleOptInRecipe(slug) {
    if (!slug) {
      throw new Error("Slug required for .getSingleOptInRecipe");
    }

    // RemoteSettingsExperimentLoader could be in a middle of updating recipes
    // so let's wait for the update to finish and this promise to resolve.
    await lazy.ExperimentAPI._rsLoader.finishedUpdating();

    // We don't need to hold the RSEL lock here because we are not doing any async work.
    return this.optInRecipes.find(recipe => recipe.slug === slug);
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
   * Start a new experiment by enrolling the users
   *
   * @param {object} recipe
   *                 The recipe to enroll in.
   * @param {string} source
   *                 The source of the experiment (e.g., "rs-loader" for recipes
   *                 from Remote Settings).
   * @param {object} options
   * @param {boolean} options.reenroll
   *                  Allow re-enrollment. Only supported for rollouts.
   * @param {string} options.branchSlug
   *                 If enrolling in a Firefox Labs opt-in experiment, this
   *                 option is required and will dictate which branch to enroll
   *                 in.
   *
   * @returns {Promise<Enrollment>}
   *          The experiment object stored in the data store.
   *
   * @throws {Error} If a recipe already exists in the store with the same slug
   *                 as `recipe` and re-enrollment is prevented.
   */
  async enroll(recipe, source, { reenroll = false, branchSlug } = {}) {
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
      lazy.NimbusTelemetry.recordEnrollmentStatus({
        slug,
        status: lazy.NimbusTelemetry.EnrollmentStatus.NOT_ENROLLED,
        reason: lazy.NimbusTelemetry.EnrollmentStatusReason.NAME_CONFLICT,
      });

      throw new Error(`An experiment with the slug "${slug}" already exists.`);
    }

    let storeLookupByFeature = recipe.isRollout
      ? this.store.getRolloutForFeature.bind(this.store)
      : this.store.getExperimentForFeature.bind(this.store);
    const userId = await this.getUserId(bucketConfig);

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
      // recipe is not an opt in recipe hence use a ratio sampled branch
      branch = await this.chooseBranch(slug, branches, userId);
    }

    for (const { featureId } of branch.features) {
      if (lazy.NimbusFeatures[featureId]?.allowCoenrollment) {
        continue;
      }

      const existingEnrollment = storeLookupByFeature(featureId);
      if (existingEnrollment) {
        lazy.log.debug(
          `Skipping enrollment for "${slug}" because there is an existing ${
            recipe.isRollout ? "rollout" : "experiment"
          } for this feature.`
        );
        lazy.NimbusTelemetry.recordEnrollmentFailure(
          slug,
          lazy.NimbusTelemetry.EnrollmentFailureReason.FEATURE_CONFLICT
        );
        lazy.NimbusTelemetry.recordEnrollmentStatus({
          slug,
          status: lazy.NimbusTelemetry.EnrollmentStatus.NOT_ENROLLED,
          reason: lazy.NimbusTelemetry.EnrollmentStatusReason.FEATURE_CONFLICT,
          conflict_slug: existingEnrollment.slug,
        });
        return null;
      }
    }

    return this._enroll(recipe, branch, source);
  }

  _enroll(
    {
      slug,
      experimentType = TELEMETRY_DEFAULT_EXPERIMENT_TYPE,
      userFacingName,
      userFacingDescription,
      featureIds,
      isRollout,
      localizations,
      isFirefoxLabsOptIn,
      firefoxLabsTitle,
      firefoxLabsDescription,
      firefoxLabsDescriptionLinks = null,
      firefoxLabsGroup,
      requiresRestart = false,
    },
    branch,
    source
  ) {
    const { prefs, prefsToSet } = this._getPrefsForBranch(branch, isRollout);
    const prefNames = new Set(prefs.map(entry => entry.name));

    // Unenroll in any conflicting prefFlips enrollments. Even though the
    // rollout does not have an effect, if it also *would* control any of the
    // same prefs, it would cause a conflict when it became active.
    const prefFlipEnrollments = [
      this.store.getRolloutForFeature(PrefFlipsFeature.FEATURE_ID),
      this.store.getExperimentForFeature(PrefFlipsFeature.FEATURE_ID),
    ].filter(enrollment => enrollment);

    for (const enrollment of prefFlipEnrollments) {
      const featureValue = _ExperimentManager.getFeatureConfigFromBranch(
        enrollment.branch,
        PrefFlipsFeature.FEATURE_ID
      ).value;

      for (const prefName of Object.keys(featureValue.prefs)) {
        if (prefNames.has(prefName)) {
          this._unenroll(enrollment, UnenrollmentCause.PrefFlipsConflict(slug));
          break;
        }
      }
    }

    /** @type {Enrollment} */
    const enrollment = {
      slug,
      branch,
      active: true,
      experimentType,
      source,
      userFacingName,
      userFacingDescription,
      lastSeen: new Date().toJSON(),
      featureIds,
      prefs,
    };

    if (localizations) {
      enrollment.localizations = localizations;
    }

    if (typeof isFirefoxLabsOptIn !== "undefined") {
      Object.assign(enrollment, {
        isFirefoxLabsOptIn,
        firefoxLabsTitle,
        firefoxLabsDescription,
        firefoxLabsDescriptionLinks,
        firefoxLabsGroup,
        requiresRestart,
      });
    }

    if (typeof isRollout !== "undefined") {
      enrollment.isRollout = isRollout;
    }

    if (isRollout) {
      enrollment.experimentType = "rollout";
      this.store.addEnrollment(enrollment);
    } else {
      this.store.addEnrollment(enrollment);
    }

    lazy.NimbusTelemetry.recordEnrollment(enrollment);

    this._setEnrollmentPrefs(prefsToSet);
    this._updatePrefObservers(enrollment);

    lazy.log.debug(
      `New ${isRollout ? "rollout" : "experiment"} started: ${slug}, ${
        branch.slug
      }`
    );

    return enrollment;
  }

  forceEnroll(recipe, branch) {
    /**
     * If we happen to be enrolled in an experiment for the same feature
     * we need to unenroll from that experiment.
     * If the experiment has the same slug after unenrollment adding it to the
     * store will overwrite the initial experiment.
     */
    for (let feature of branch.features) {
      const isRollout = recipe.isRollout ?? false;
      let enrollment = isRollout
        ? this.store.getRolloutForFeature(feature?.featureId)
        : this.store.getExperimentForFeature(feature?.featureId);
      if (enrollment) {
        lazy.log.debug(
          `Existing ${
            isRollout ? "rollout" : "experiment"
          } found for the same feature ${feature.featureId}, unenrolling.`
        );

        this._unenroll(
          enrollment,
          UnenrollmentCause.fromReason(
            lazy.NimbusTelemetry.UnenrollReason.FORCE_ENROLLMENT
          )
        );
      }
    }

    recipe.userFacingName = `${recipe.userFacingName} - Forced enrollment`;

    const slug = `optin-${recipe.slug}`;
    const enrollment = this._enroll(
      {
        ...recipe,
        slug,
      },
      branch,
      lazy.NimbusTelemetry.EnrollmentSource.FORCE_ENROLLMENT
    );

    Services.obs.notifyObservers(null, "nimbus:enrollments-updated", slug);

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
   *
   * @returns {boolean}
   *          Whether the enrollment is active.
   */
  async updateEnrollment(enrollment, recipe, source, result) {
    const { EnrollmentStatus, EnrollmentStatusReason, UnenrollReason } =
      lazy.NimbusTelemetry;

    if (result.ok && recipe?.isFirefoxLabsOptIn) {
      this.optInRecipes.push(recipe);
    }

    if (enrollment.active) {
      if (!result.ok) {
        // If the recipe failed validation then we must unenroll.
        this._unenroll(
          enrollment,
          UnenrollmentCause.fromCheckRecipeResult(result)
        );
        return false;
      }

      if (result.status === lazy.MatchStatus.NOT_SEEN) {
        // If the recipe was not present in the source we must unenroll.
        this._unenroll(
          enrollment,
          UnenrollmentCause.fromCheckRecipeResult(result)
        );
        return false;
      }

      if (!recipe.branches.find(b => b.slug === enrollment.branch.slug)) {
        // Our branch has been removed so we must unenroll.
        //
        // This should not happen in practice.
        this._unenroll(
          enrollment,
          UnenrollmentCause.fromReason(UnenrollReason.BRANCH_REMOVED)
        );
        return false;
      }

      if (result.status === lazy.MatchStatus.NO_MATCH) {
        // If we have an active enrollment and we no longer match targeting we
        // must unenroll.
        this._unenroll(
          enrollment,
          UnenrollmentCause.fromCheckRecipeResult(result)
        );
        return false;
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
        return false;
      }

      if (result.status === lazy.MatchStatus.TARGETING_AND_BUCKETING) {
        lazy.NimbusTelemetry.recordEnrollmentStatus({
          slug: enrollment.slug,
          branch: enrollment.branch.slug,
          status: EnrollmentStatus.ENROLLED,
          reason: EnrollmentStatusReason.QUALIFIED,
        });
      }

      // Either this recipe is not a rollout or both targeting matches and we
      // are in the bucket allocation. For the former, we do not re-evaluate
      // bucketing for experiments because the bucketing cannot change. For the
      // latter, we are already active so we don't need to enroll.
      return true;
    }

    if (!enrollment.isRollout || enrollment.isFirefoxLabsOptIn) {
      // We can only re-enroll into rollouts and we do not enroll directly into
      // Firefox Labs Opt-Ins.
      return false;
    }

    if (
      !enrollment.active &&
      result.status === lazy.MatchStatus.TARGETING_AND_BUCKETING &&
      enrollment.unenrollReason !== UnenrollReason.INDIVIDUAL_OPT_OUT
    ) {
      // We only re-enroll if we match targeting and bucketing and the user did
      // not purposefully opt out via about:studies.
      lazy.log.debug(`Re-enrolling in rollout "${recipe.slug}`);
      return !!(await this.enroll(recipe, source, { reenroll: true }));
    }

    return false;
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
      return;
    }

    this._unenroll(
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
   */
  _unenroll(enrollment, cause, { duringRestore = false } = {}) {
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

    this.store.updateExperiment(slug, {
      active: false,
      unenrollReason: cause.reason,
    });

    lazy.NimbusTelemetry.recordUnenrollment(enrollment, cause);

    this._unsetEnrollmentPrefs(enrollment, cause, { duringRestore });

    lazy.log.debug(`Recipe unenrolled: ${slug}`);
  }

  observe() {
    if (!this.studiesEnabled) {
      this._handleStudiesOptOut();
    }

    Services.obs.notifyObservers(null, STUDIES_ENABLED_CHANGED);
  }

  /**
   * Unenroll from all active studies if user opts out.
   */
  _handleStudiesOptOut() {
    for (const enrollment of this.store.getAllActiveExperiments()) {
      this._unenroll(
        enrollment,
        UnenrollmentCause.fromReason(
          lazy.NimbusTelemetry.UnenrollReason.STUDIES_OPT_OUT
        )
      );
    }
    for (const enrollment of this.store.getAllActiveRollouts()) {
      this._unenroll(
        enrollment,
        UnenrollmentCause.fromReason(
          lazy.NimbusTelemetry.UnenrollReason.STUDIES_OPT_OUT
        )
      );
    }

    this.optInRecipes = [];
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
   * @memberof _ExperimentManager
   */
  async chooseBranch(slug, branches, userId = lazy.ClientEnvironment.userId) {
    const ratios = branches.map(({ ratio = 1 }) => ratio);

    // It's important that the input be:
    // - Unique per-user (no one is bucketed alike)
    // - Unique per-experiment (bucketing differs across multiple experiments)
    // - Differs from the input used for sampling the recipe (otherwise only
    //   branches that contain the same buckets as the recipe sampling will
    //   receive users)
    const input = `${this.id}-${userId}-${slug}-branch`;

    const index = await lazy.Sampling.ratioSample(input, ratios);
    return branches[index];
  }

  /**
   * Generate the list of prefs a recipe will set.
   *
   * @params {object} branch The recipe branch that will be enrolled.
   * @params {boolean} isRollout Whether or not this recipe is a rollout.
   *
   * @returns {object} An object with the following keys:
   *
   *                   `prefs`:
   *                        The full list of prefs that this recipe would set,
   *                        if there are no conflicts. This will include prefs
   *                        that, for example, will not be set because this
   *                        enrollment is a rollout and there is an active
   *                        experiment that set the same pref.
   *
   *                   `prefsToSet`:
   *                        Prefs that should be set once enrollment is
   *                        complete.
   */
  _getPrefsForBranch(branch, isRollout = false) {
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
          const conflictingPref = conflictingEnrollment?.prefs?.find(
            p => p.name === prefName
          );

          if (conflictingPref) {
            // If there is another enrollment that has already set the pref we
            // care about, we use its stored originalValue.
            originalValue = conflictingPref.originalValue;
          } else if (
            prefBranch === "user" &&
            !Services.prefs.prefHasUserValue(prefName)
          ) {
            // If there is a default value set, then attempting to read the user
            // branch would result in returning the default branch value.
            originalValue = null;
          } else {
            // If there is an active prefFlips experiment for this pref on this
            // branch, we must use its originalValue.
            const prefFlip = this._prefFlips._prefs.get(prefName);
            if (prefFlip?.branch === prefBranch) {
              originalValue = prefFlip.originalValue;
            } else {
              originalValue = lazy.PrefUtils.getPref(prefName, {
                branch: prefBranch,
              });
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
   * @param {object[]} prefsToSet
   *                   A list of objects containing the prefs to set.
   *
   *                   Each object has the following properties:
   *
   *                   * `name`: The name of the pref.
   *                   * `value`: The value of the pref.
   *                   * `prefBranch`: The branch to set the pref on (either "user" or "default").
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
            newValue = _ExperimentManager.getFeatureConfigFromBranch(
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
   * @returns {boolean} Whether the restore was successful. If false, the
   *                    enrollment has ended.
   */
  _restoreEnrollmentPrefs(enrollment) {
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

    const feature = _ExperimentManager.getFeatureConfigFromBranch(
      enrollments.at(-1).branch,
      pref.featureId
    );

    const changedPref = {
      name: pref.name,
      branch: PrefFlipsFeature.determinePrefChangeBranch(
        pref.name,
        pref.branch,
        feature.value[pref.variable]
      ),
    };

    for (const enrollment of enrollments) {
      this._unenroll(enrollment, UnenrollmentCause.ChangedPref(changedPref));
    }
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

export const ExperimentManager = new _ExperimentManager();
