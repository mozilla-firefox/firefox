/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  FirstStartup: "resource://gre/modules/FirstStartup.sys.mjs",
  TelemetryEnvironment: "resource://gre/modules/TelemetryEnvironment.sys.mjs",
});

const EXPERIMENT_ACTIVE_PREFIX = "nimbus-";

const EnrollmentStatus = Object.freeze({
  ENROLLED: "Enrolled",
  NOT_ENROLLED: "NotEnrolled",
  DISQUALIFIED: "Disqualified",
  WAS_ENROLLED: "WasEnrolled",
  ERROR: "Error",
});

const EnrollmentStatusReason = Object.freeze({
  CHANGED_PREF: "ChangedPref",
  QUALIFIED: "Qualified",
  OPT_IN: "OptIn",
  OPT_OUT: "OptOut",
  NOT_SELECTED: "NotSelected",
  NOT_TARGETED: "NotTargeted",
  ENROLLMENTS_PAUSED: "EnrollmentsPaused",
  FEATURE_CONFLICT: "FeatureConflict",
  FORCE_ENROLLMENT: "ForceEnrollment",
  NAME_CONFLICT: "NameConflict",
  PREF_FLIPS_CONFLICT: "PrefFlipsConflict",
  ERROR: "Error",
  UNENROLLED_IN_ANOTHER_PROFILE: "UnenrolledInAnotherProfile",
  MIGRATION: "Migration",
});

const EnrollmentFailureReason = Object.freeze({
  FEATURE_CONFLICT: "feature-conflict",
  NAME_CONFLICT: "name-conflict",
});

const EnrollmentSource = Object.freeze({
  RS_LOADER: "rs-loader",
  FORCE_ENROLLMENT: "force-enrollment",
});

const RemoteSettingsSyncErrorReason = Object.freeze({
  BACKWARDS_SYNC: "backwards-sync",
  EMPTY: "empty",
  GET_EXCEPTION: "get-exception",
  INVALID_DATA: "invalid-data",
  INVALID_LAST_MODIFIED: "invalid-last-modified",
  LAST_MODIFIED_EXCEPTION: "last-modified-exception",
  NOT_YET_SYNCED: "not-yet-synced",
  NULL_LAST_MODIFIED: "null-last-modified",
  MISSING_SIGNATURE: "missing-signature",
});

/**
 * @typedef {T[keyof T]} EnumValuesOf
 * @template {type} T
 */

/** @typedef {EnumValuesOf<typeof RemoteSettingsSyncErrorReason>} RemoteSettingsSyncErrorReason */

const UnenrollmentFailureReason = Object.freeze({
  ALREADY_UNENROLLED: "already-unenrolled",
  DOES_NOT_EXIST: "does-not-exist",
});

const ValidationFailureReason = Object.freeze({
  INVALID_BRANCH: "invalid-branch",
  INVALID_FEATURE: "invalid-feature",
  INVALID_RECIPE: "invalid-recipe",
  L10N_MISSING_ENTRY: "l10n-missing-entry",
  L10N_MISSING_LOCALE: "l10n-missing-locale",
  UNSUPPORTED_FEATURES: "unsupported-feature",
});

const UnenrollReason = Object.freeze({
  BRANCH_REMOVED: "branch-removed",
  BUCKETING: "bucketing",
  CHANGED_PREF: "changed-pref",
  FORCE_ENROLLMENT: "force-enrollment",
  INDIVIDUAL_OPT_OUT: "individual-opt-out",
  LABS_DIABLED: "labs-disabled",
  LABS_OPT_OUT: "labs-opt-out",
  MIGRATION: "migration",
  PREF_FLIPS_CONFLICT: "prefFlips-conflict",
  PREF_FLIPS_FAILED: "prefFlips-failed",
  PREF_VARIABLE_CHANGED: "pref-variable-changed",
  PREF_VARIABLE_MISSING: "pref-variable-missing",
  PREF_VARIABLE_NO_LONGER: "pref-variable-no-longer",
  RECIPE_NOT_SEEN: "recipe-not-seen",
  ROLLOUTS_OPT_OUT: "rollouts-opt-out",
  STUDIES_OPT_OUT: "studies-opt-out",
  TARGETING_MISMATCH: "targeting-mismatch",
  UNENROLLED_IN_ANOTHER_PROFILE: "unenrolled-in-another-profile",
  UNKNOWN: "unknown",

  // Validation failure can cause unenrollment.
  ...ValidationFailureReason,
});

const EXPERIMENT_TYPE_ROLLOUT = "rollout";
const EXPERIMENT_TYPE_NIMBUS = "nimbus";

/**
 * @typedef {object} EnrollmentStatus
 * An enrollment status event without the slug.
 *
 * @property {keyof typeof EnrollmentStatus} status
 * The kind of enrollment status event.
 *
 * @property {keyof typeof EnrollmentStatusReason | undefined} reason
 * The reason this enrollment status occurred, if applicable.
 *
 * @property {string | undefined} branch
 * The branch that was enrolled, if applicable.
 *
 * @property {string | undefined} error
 * What error triggered this enrollment status, if applicable.
 *
 * This corresponds to the `error_string` field on the enrollment status metric.
 *
 * @property {string[] | undefined} conflictSlugs
 *
 * This corresponds to the `conflict_slug` field on the enrollment status
 * metric.
 *
 * @property {string | undefined} migration
 * A migration that caused the enrollment status event to occur.
 */

export const NimbusTelemetry = {
  EnrollmentFailureReason,
  EnrollmentSource,
  EnrollmentStatus,
  EnrollmentStatusReason,
  RemoteSettingsSyncErrorReason,
  UnenrollReason,
  UnenrollmentFailureReason,
  ValidationFailureReason,

  /**
   * Record an enrollment event.
   *
   * @property {object} enrollment
   * The enrollment that enrolled.
   *
   * @returns {EnrollmentStatus}
   * An enrollment status event to also be submitted.
   */
  recordEnrollment(enrollment) {
    this.setExperimentActive(enrollment);

    const experimentType = enrollment.isRollout
      ? EXPERIMENT_TYPE_ROLLOUT
      : EXPERIMENT_TYPE_NIMBUS;

    Glean.normandy.enrollNimbusExperiment.record({
      value: enrollment.slug,
      branch: enrollment.branch.slug,
      experimentType,
    });
    Glean.nimbusEvents.enrollment.record({
      experiment: enrollment.slug,
      branch: enrollment.branch.slug,
      experiment_type: experimentType,
    });

    return {
      branch: enrollment.branch.slug,
      status: EnrollmentStatus.ENROLLED,
      reason:
        enrollment.isFirefoxLabsOptIn ||
        enrollment.source === EnrollmentSource.FORCE_ENROLLMENT
          ? EnrollmentStatusReason.OPT_IN
          : EnrollmentStatusReason.QUALIFIED,
    };
  },

  recordEnrollmentFailure(slug, reason) {
    Glean.normandy.enrollFailedNimbusExperiment.record({
      value: slug,
      reason,
    });
    Glean.nimbusEvents.enrollFailed.record({
      experiment: slug,
      reason,
    });
  },

  recordExposure(slug, branchSlug, featureId) {
    Glean.normandy.exposeNimbusExperiment.record({
      value: slug,
      branchSlug,
      featureId,
    });
    Glean.nimbusEvents.exposure.record({
      experiment: slug,
      branch: branchSlug,
      feature_id: featureId,
    });
  },

  /**
   * Record the result of a migration.
   *
   * @param {string} migration The name of the migration.
   * @param {number} duration The duration of the migration.
   * @param {string | undefined} The reason the migration failed, if any.
   */
  recordMigration(migration, duration, errorReason) {
    Glean.nimbusEvents.migration.record(
      Object.assign(
        {
          migration_id: migration,
          success: typeof errorReason === "undefined",
          duration,
          is_first_startup:
            lazy.FirstStartup.state === lazy.FirstStartup.IN_PROGRESS,
        },
        typeof errorReason !== "undefined" ? { error_reason: errorReason } : {}
      )
    );
  },

  /**
   * Record an error that occurred during
   *
   * @param {string} collection The name of the collection.
   * @param {RemoteSettingsSyncErrorReason} reason The reason why the sync failed.
   * @param {object} options
   * @param {boolean} options.forceSync Whether or not the sync was forced.
   * @param {string | undefined} options.trigger What event triggered the sync.
   */
  recordRemoteSettingsSyncError(
    collection,
    reason,
    { forceSync = false, trigger } = {}
  ) {
    const event = {
      force_sync: forceSync,
      collection,
      reason,
    };

    if (typeof trigger === "string") {
      event.trigger = trigger;
    }

    Glean.nimbusEvents.remoteSettingsSyncError.record(event);
  },

  /**
   * Record an unenrollment event.
   *
   * @property {object} enrollment
   * The enrollment that unenrolled.
   *
   * @property {object} cause
   * The cause for unenrollment.
   *
   * @returns {EnrollmentStatus}
   * An enrollment status event to also be submitted.
   */
  recordUnenrollment(enrollment, cause) {
    lazy.TelemetryEnvironment.setExperimentInactive(enrollment.slug);
    Services.fog.setExperimentInactive(enrollment.slug);

    const legacyEvent = {
      value: enrollment.slug,
      branch: enrollment.branch.slug,
      reason: cause.reason,
    };
    const gleanEvent = {
      experiment: enrollment.slug,
      branch: enrollment.branch.slug,
      reason: cause.reason,
    };

    switch (cause.reason) {
      case UnenrollReason.CHANGED_PREF:
        legacyEvent.changedPref = cause.changedPref.name;
        gleanEvent.changed_pref = cause.changedPref.name;

        // We've hit the limit of extra keys that can go on the legacy
        // unenrollment event, so this key does not get mirrored.
        gleanEvent.about_config_change = cause.isAboutConfigChange;
        break;

      case UnenrollReason.PREF_FLIPS_CONFLICT:
        legacyEvent.conflictingSlug = cause.conflictingSlug;
        gleanEvent.conflicting_slug = cause.conflictingSlug;
        break;

      case UnenrollReason.PREF_FLIPS_FAILED:
        legacyEvent.prefType = cause.prefType;
        gleanEvent.pref_type = cause.prefType;

        legacyEvent.prefName = cause.prefName;
        gleanEvent.pref_name = cause.prefName;
        break;

      case UnenrollReason.L10N_MISSING_ENTRY:
      case UnenrollReason.L10N_MISSING_LOCALE:
        gleanEvent.locale = cause.locale;
        break;

      case UnenrollReason.MIGRATION:
        gleanEvent.migration = cause.migration;
        break;
    }

    Glean.normandy.unenrollNimbusExperiment.record(legacyEvent);
    Glean.nimbusEvents.unenrollment.record(gleanEvent);

    /** @type {EnrollmentStatus} */
    const enrollmentStatus = {
      branch: enrollment.branch.slug,
    };

    switch (cause.reason) {
      case UnenrollReason.BUCKETING:
        enrollmentStatus.status = EnrollmentStatus.DISQUALIFIED;
        enrollmentStatus.reason = EnrollmentStatusReason.NOT_SELECTED;
        break;

      case UnenrollReason.RECIPE_NOT_SEEN:
        enrollmentStatus.status = EnrollmentStatus.WAS_ENROLLED;
        break;

      case UnenrollReason.TARGETING_MISMATCH:
        enrollmentStatus.status = EnrollmentStatus.DISQUALIFIED;
        enrollmentStatus.reason = EnrollmentStatusReason.NOT_TARGETED;
        break;

      case UnenrollReason.INDIVIDUAL_OPT_OUT:
      case UnenrollReason.LABS_OPT_OUT:
      case UnenrollReason.ROLLOUTS_OPT_OUT:
      case UnenrollReason.STUDIES_OPT_OUT:
        enrollmentStatus.status = EnrollmentStatus.DISQUALIFIED;
        enrollmentStatus.reason = EnrollmentStatusReason.OPT_OUT;
        break;

      case UnenrollReason.CHANGED_PREF:
        enrollmentStatus.status = EnrollmentStatus.DISQUALIFIED;
        enrollmentStatus.reason = EnrollmentStatusReason.CHANGED_PREF;
        break;

      case UnenrollReason.FORCE_ENROLLMENT:
        enrollmentStatus.status = EnrollmentStatus.DISQUALIFIED;
        enrollmentStatus.reason = EnrollmentStatusReason.FORCE_ENROLLMENT;
        break;

      case UnenrollReason.PREF_FLIPS_CONFLICT:
        enrollmentStatus.status = EnrollmentStatus.DISQUALIFIED;
        enrollmentStatus.reason = EnrollmentStatusReason.PREF_FLIPS_CONFLICT;
        enrollmentStatus.conflictSlugs = [cause.conflictingSlug];
        break;

      case UnenrollReason.UNENROLLED_IN_ANOTHER_PROFILE:
        enrollmentStatus.status = EnrollmentStatus.DISQUALIFIED;
        enrollmentStatus.reason =
          EnrollmentStatusReason.UNENROLLED_IN_ANOTHER_PROFILE;
        break;

      case UnenrollReason.MIGRATION:
        enrollmentStatus.status = EnrollmentStatus.WAS_ENROLLED;
        enrollmentStatus.reason = EnrollmentStatusReason.MIGRATION;
        enrollmentStatus.migration = cause.migration;
        break;

      default:
        enrollmentStatus.status = EnrollmentStatus.DISQUALIFIED;
        enrollmentStatus.reason = EnrollmentStatusReason.ERROR;
        enrollmentStatus.error = cause.reason;
    }

    return enrollmentStatus;
  },

  recordUnenrollmentFailure(slug, reason) {
    Glean.normandy.unenrollFailedNimbusExperiment.record({
      value: slug,
      reason,
    });
    Glean.nimbusEvents.unenrollFailed.record({
      experiment: slug,
      reason,
    });
  },

  setExperimentActive(enrollment) {
    const experimentType = enrollment.isRollout
      ? EXPERIMENT_TYPE_ROLLOUT
      : EXPERIMENT_TYPE_NIMBUS;
    const type = `${EXPERIMENT_ACTIVE_PREFIX}${experimentType}`;
    lazy.TelemetryEnvironment.setExperimentActive(
      enrollment.slug,
      enrollment.branch.slug,
      { type }
    );

    Services.fog.setExperimentActive(enrollment.slug, enrollment.branch.slug, {
      type,
    });
  },

  recordValidationFailure(
    slug,
    reason,
    { branch, locale, l10nIds: l10n_ids } = {}
  ) {
    // Do not record invalid feature telemetry.
    if (reason === ValidationFailureReason.INVALID_FEATURE) {
      return;
    }

    // Do not record unsupported feature telemetry.
    if (reason === ValidationFailureReason.UNSUPPORTED_FEATURES) {
      return;
    }

    const extra = Object.assign(
      { reason },
      reason === ValidationFailureReason.INVALID_BRANCH ? { branch } : {},
      reason === ValidationFailureReason.L10N_MISSING_ENTRY
        ? { l10n_ids, locale }
        : {},
      reason === ValidationFailureReason.L10N_MISSING_LOCALE ? { locale } : {}
    );

    Glean.normandy.validationFailedNimbusExperiment.record({
      value: slug,
      ...extra,
    });

    Glean.nimbusEvents.validationFailed.record({
      experiment: slug,
      ...extra,
    });
  },
};
