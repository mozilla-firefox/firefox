/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  FirefoxLabs: "resource://nimbus/FirefoxLabs.sys.mjs",
  NimbusEnrollments: "resource://nimbus/lib/Enrollments.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusTelemetry: "resource://nimbus/lib/Telemetry.sys.mjs",
  ProfilesDatastoreService:
    "moz-src:///toolkit/profile/ProfilesDatastoreService.sys.mjs",
  RemoteSettingsSyncError:
    "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs",
  UnenrollmentCause: "resource://nimbus/lib/ExperimentManager.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  const { Logger } = ChromeUtils.importESModule(
    "resource://messaging-system/lib/Logger.sys.mjs"
  );
  return new Logger("NimbusMigrations");
});

function isBackgroundTaskMode() {
  const bts = Cc["@mozilla.org/backgroundtasks;1"]?.getService(
    Ci.nsIBackgroundTasks
  );

  return bts?.isBackgroundTaskMode ?? false;
}

/**
 * A named migration.
 *
 * @typedef {object} Migration
 *
 * @property {string} name The name of the migration. This will be reported in
 * telemetry.
 *
 * @property {function(string): void} fn The migration implementation.
 */

/**
 * Construct a {@link Migration} with a specific name.
 *
 * @param {string} name The name of the migration.
 * @param {function(name: string): void} fn The migration function.
 *
 * @returns {Migration} The migration.
 */
function migration(name, fn) {
  return { name, fn };
}

const Phase = Object.freeze({
  INIT_STARTED: "init-started",
  AFTER_STORE_INITIALIZED: "after-store-initialized",
  AFTER_REMOTE_SETTINGS_UPDATE: "after-remote-settings-update",
});

/**
 * An initialization phase.
 *
 * @typedef {typeof Phase[keyof typeof Phase]} Phase
 */

export const LEGACY_NIMBUS_MIGRATION_PREF = "nimbus.migrations.latest";

/** @type {Record<Phase, string>} */
export const NIMBUS_MIGRATION_PREFS = Object.fromEntries(
  Object.entries(Phase).map(([, v]) => [v, `nimbus.migrations.${v}`])
);

export const LABS_MIGRATION_FEATURE_MAP = (function () {
  const featureMap = {
    "auto-pip": "firefox-labs-auto-pip",
    "urlbar-ime-search": "firefox-labs-urlbar-ime-search",
  };

  // The jpeg-xl feature is gated on MOZ_JXL; the relevant prefs do not exist
  // when MOZ_JXL is not defined.
  if (AppConstants.MOZ_JXL) {
    featureMap["jpeg-xl"] = "firefox-labs-jpeg-xl";
  }

  return Object.freeze(featureMap);
})();

/**
 * Migrate from the legacy migration state to multi-phase migration state.
 *
 * Previously there was only a single set of migrations that ran at the end of
 * `ExperimentAPI.init()`, which is now the "after-remote-settings-update" phase.
 */
function migrateMultiphase() {
  const latestMigration = Services.prefs.getIntPref(
    LEGACY_NIMBUS_MIGRATION_PREF,
    -1
  );
  if (latestMigration >= 0) {
    Services.prefs.setIntPref(
      NIMBUS_MIGRATION_PREFS[Phase.AFTER_REMOTE_SETTINGS_UPDATE],
      latestMigration
    );
    Services.prefs.clearUserPref(LEGACY_NIMBUS_MIGRATION_PREF);
  }
}

/**
 * Disable rollouts if the user has previously opted out of studies.
 */
function migrateSeparateRolloutOptOut() {
  if (!Services.prefs.getBoolPref("app.shield.optoutstudies.enabled")) {
    Services.prefs.setBoolPref("nimbus.rollouts.enabled", false);
  }
}

function migrateNoop() {
  // This migration intentionally left blank.
  //
  // Use this migration to replace outdated migrations that should not be run on
  // new clients.
}

async function migrateEnrollmentsToSql() {
  if (!lazy.NimbusEnrollments.databaseEnabled) {
    // We are in an xpcshell test that has not initialized the
    // ProfilesDatastoreService.
    //
    // TODO(bug 1967779): require the ProfilesDatastoreService to be initialized
    // and remove this check.
    return;
  }

  const profileId = lazy.ExperimentAPI.profileId;

  // This migration runs before the ExperimentManager is fully initialized. We
  // need to initialize *just* the ExperimentStore so that we can copy its
  // enrollments to the SQL database. This must occur *before* the
  // ExperimentManager is initialized because that may cause unenrollments and
  // those enrollments need to exist in both the ExperimentStore and SQL
  // database.

  await lazy.ExperimentAPI.manager.store.ready();

  // Ensure we are copying the data from the JSONFile explicitly because if the
  // NimbusEnrollments table is the source of truth, then getAll() will return
  // an empty array.
  const enrollments = Object.values(
    lazy.ExperimentAPI.manager.store._jsonFile.data
  );

  // If there are no enrollments we can skip the rest of the migration.
  if (enrollments.length === 0) {
    return;
  }

  let recipes;
  try {
    recipes = await lazy.ExperimentAPI._rsLoader.getRecipesFromAllCollections({
      trigger: "migration",
    });
  } catch (e) {
    if (e instanceof lazy.RemoteSettingsSyncError) {
      throw new MigrationError(e.reason);
    }

    throw e;
  }

  const recipesBySlug = new Map(recipes.map(r => [r.slug, r]));

  const rows = enrollments.map(enrollment => {
    const { active, slug, source } = enrollment;

    let recipe;
    if (source === "rs-loader") {
      recipe = recipesBySlug.get(slug);
    }
    if (!recipe) {
      // If this enrollment is not from the RemoteSettingsExperimentLoader or
      // the experiment has since ended we re-create as much of the recipe as we
      // can from the enrollment.
      //
      // We are early in Nimbus startup and we have not yet called
      // ExperimentManager.onStartup. When the ExperimentManager is initialized
      // later in ExperimentAPI.init() it may cause unenrollments due to state
      // being changed (e.g., if studies have been disabled). To process those
      // unenrollments, there needs to be an enrollment record
      // in the database for *every* enrollment in the JSON store *and* each
      // needs to have a valid `recipe` field because in bug 1956082 we will
      // stop using ExperimentStoreData.json as the source-of-truth and rely
      // entirely on the NimbusEnrollments table.
      recipe = {
        slug,
        userFacingName: enrollment.userFacingName,
        userFacingDescription: enrollment.userFacingDescription,
        featureIds: enrollment.featureIds,
        isRollout: enrollment.isRollout ?? false,
        localizations: enrollment.localizations ?? null,
        isEnrollmentPaused: true,
        isFirefoxLabsOptIn: enrollment.isFirefoxLabsOptIn ?? false,
        firefoxLabsTitle: enrollment.firefoxLabsTitle ?? null,
        firefoxLabsDescription: enrollment.firefoxLabsDescription ?? null,
        firefoxLabsDescriptionLinks:
          enrollment.firefoxLabsDescriptionLinks ?? null,
        firefoxLabsGroup: enrollment.firefoxLabsGroup ?? null,
        requiresRestart: enrollment.requiresRestart ?? false,
        branches: [
          {
            ...enrollment.branch,
            ratio: enrollment.branch.ratio ?? 1,
          },
        ],
      };
    }

    return {
      profileId,
      slug,
      branchSlug: enrollment.branch.slug,
      recipe: recipe ? JSON.stringify(recipe) : null,
      active,
      unenrollReason: active ? null : enrollment.unenrollReason,
      lastSeen: enrollment.lastSeen ?? new Date().toJSON(),
      setPrefs: enrollment.prefs ? JSON.stringify(enrollment.prefs) : null,
      prefFlips: enrollment.prefFlips
        ? JSON.stringify(enrollment.prefFlips)
        : null,
      source,
    };
  });

  const conn = await lazy.ProfilesDatastoreService.getConnection();
  await conn.executeTransaction(async () => {
    for (const row of rows) {
      await conn.execute(
        `
        INSERT INTO NimbusEnrollments VALUES(
          null,
          :profileId,
          :slug,
          :branchSlug,
          jsonb(:recipe),
          :active,
          :unenrollReason,
          :lastSeen,
          jsonb(:setPrefs),
          jsonb(:prefFlips),
          :source
        );`,
        row
      );
    }
  });

  if (lazy.NimbusEnrollments.readFromDatabaseEnabled) {
    // These now exist in the database and in the ExperimentStore's JSONFile
    // data. However, the regular ExperimentStore data will not have been
    // populated yet (because it will have read zero rows from the database
    // during `SharedDataMap.init()`.
    const store = lazy.ExperimentAPI.manager.store;
    store._data = structuredClone(store._jsonFile.data);
    store._syncToChildren({ flush: true });
  }

  await lazy.ExperimentAPI.manager.store._reportStartupDatabaseConsistency(
    "migration"
  );
}

/**
 * Migrate enrollment from the firefox-labs-auto-pip rolloutl.
 *
 * This feature is becoming a regular setting in about:preferences in Firefox
 * 147. We need to unenroll users without resetting the prefs controlled by the
 * feature.
 *
 * This migration must run before the `ExperimentManager.onStartup` has run
 * because this feature might be removed and we cannot guarantee that users will
 * update to exactly 145.
 */
function migrateGraduateFirefoxLabsAutoPip(migration) {
  if (isBackgroundTaskMode()) {
    // This migration does not apply to background task mode.
    lazy.log.debug(`${migration}: skipping (is background task mode)`);
    return;
  }

  const enrollment = lazy.ExperimentAPI.manager.store.get(
    "firefox-labs-auto-pip"
  );
  if (!enrollment?.active) {
    lazy.log.debug(`${migration}: skipping (no or inactive enrollment)`);
    return;
  }

  lazy.ExperimentAPI.manager._unenroll(
    enrollment,
    lazy.UnenrollmentCause.Migration(migration),
    { unsetEnrollmentPrefs: false }
  );
}

/**
 * Migrate the pre-Nimbus Firefox Labs experiences into Nimbus enrollments.
 *
 * Previously Firefox Labs had a one-to-one correlation between Labs Experiments
 * and prefs being set. If any of those prefs are set, attempt to enroll in the
 * corresponding live Nimbus rollout.
 *
 * Once these rollouts end (i.e., because the features are generally available
 * and no longer in Labs) they can be removed from {@link
 * LABS_MIGRATION_FEATURE_MAP} and once that map is empty this migration can be
 * replaced with a no-op.
 */
async function migrateFirefoxLabsEnrollments() {
  if (isBackgroundTaskMode()) {
    // This migration does not apply to background task mode.
    return;
  }

  await lazy.ExperimentAPI._rsLoader.finishedUpdating();
  await lazy.ExperimentAPI._rsLoader.withUpdateLock(
    async () => {
      const labs = await lazy.FirefoxLabs.create();

      let didEnroll = false;

      for (const [feature, slug] of Object.entries(
        LABS_MIGRATION_FEATURE_MAP
      )) {
        const pref =
          lazy.NimbusFeatures[feature].manifest.variables.enabled.setPref.pref;

        if (!labs.get(slug)) {
          // If the recipe is not available then either it is no longer live or
          // the targeting did not match.
          continue;
        }

        if (!Services.prefs.getBoolPref(pref, false)) {
          // Only enroll if the migration pref is set.
          continue;
        }

        await labs.enroll(slug, "control");

        // We need to overwrite the original pref value stored in the
        // ExperimentStore so that unenrolling will disable the feature.
        const enrollment = lazy.ExperimentAPI.manager.store.get(slug);
        if (!enrollment) {
          lazy.log.error(`Enrollment with ${slug} should exist but does not`);
          continue;
        }
        if (!enrollment.active) {
          lazy.log.error(
            `Enrollment with slug ${slug} should be active but is not.`
          );
          continue;
        }

        const prefEntry = enrollment.prefs?.find(entry => entry.name === pref);
        if (!prefEntry) {
          lazy.log.error(
            `Enrollment with slug ${slug} does not set pref ${pref}`
          );
          continue;
        }

        didEnroll = true;
        prefEntry.originalValue = false;
      }

      if (didEnroll) {
        // Trigger a save of the ExperimentStore since we've changed some data
        // structures without using set().
        // We do not have to sync these changes to child processes because the
        // data is only used in the parent process.
        lazy.ExperimentAPI.manager.store._jsonFile.saveSoon();
      }
    },
    { mode: "shared" }
  );
}

export class MigrationError extends Error {
  static Reason = Object.freeze({
    UNKNOWN: "unknown",
  });

  constructor(reason) {
    super(`Migration error: ${reason}`);
    this.reason = reason;
  }
}

export const NimbusMigrations = {
  Phase,
  migration,

  NIMBUS_MIGRATION_PREFS,

  /**
   * Apply any outstanding migrations for the given phase.
   *
   * The first migration in the phase to report an error will halt the
   * application of further migrations in the phase.
   *
   * @param {Phase} phase The phase of migrations to apply.
   */
  async applyMigrations(phase) {
    const phasePref = NIMBUS_MIGRATION_PREFS[phase];
    const latestMigration = Services.prefs.getIntPref(phasePref, -1);
    let lastSuccess = latestMigration;

    lazy.log.debug(
      `applyMigrations ${phase}: latestMigration = ${latestMigration}`
    );

    for (let i = latestMigration + 1; i < this.MIGRATIONS[phase].length; i++) {
      const migration = this.MIGRATIONS[phase][i];

      lazy.log.debug(
        `applyMigrations ${phase}: applying migration ${i}: ${migration.name}`
      );

      try {
        await migration.fn(migration.name);
      } catch (e) {
        lazy.log.error(
          `applyMigrations: error running migration ${i} (${migration.name}): ${e}`
        );

        let reason = MigrationError.Reason.UNKNOWN;
        if (e instanceof MigrationError) {
          reason = e.reason;
        }

        lazy.NimbusTelemetry.recordMigration(migration.name, reason);

        break;
      }

      lastSuccess = i;

      lazy.log.debug(
        `applyMigrations: applied migration ${i}: ${migration.name}`
      );

      lazy.NimbusTelemetry.recordMigration(migration.name);
    }

    if (latestMigration != lastSuccess) {
      Services.prefs.setIntPref(phasePref, lastSuccess);
    }
  },

  /**
   * Return whether or not a specific migration has been completed.
   *
   * @param {Phase} phase The migration phase the specified migration occurs in.
   * @param {string} The name of the migration.
   *
   * @returns {boolean} Whether or not the migration was completed.
   */
  isMigrationCompleted(phase, name) {
    const phasePref = NIMBUS_MIGRATION_PREFS[phase];
    const progress = Services.prefs.getIntPref(phasePref, -1);

    const migrationIndex = this.MIGRATIONS[phase]?.findIndex(
      m => m.name === name
    );

    if (migrationIndex === -1) {
      return false;
    }

    return progress >= migrationIndex;
  },

  /**
   * A mapping of each Nimbus initialization phase to the mirgations that will occur.
   *
   * N.B.: Migrations *cannot* be removed from this. If you want to remove a
   * migration, you *must* replace it with {@link migrateNoop} otherwise clients
   * that have already migrated will get out of sync and not perform new
   * migrations correctly.
   *
   * @type {Record<Phase, Migration[]>}
   */
  MIGRATIONS: {
    [Phase.INIT_STARTED]: [
      migration("multi-phase-migrations", migrateMultiphase),
      migration("separate-rollout-opt-out", migrateSeparateRolloutOptOut),
    ],

    [Phase.AFTER_STORE_INITIALIZED]: [
      // Bug 1969994: This used to be migrateEnrollmentsToSql, but we had to
      // wipe the NimbusEnrollments table and re-run the migration to work
      // around ExperimentAPI.profileId not being persistent.
      migration("noop", migrateNoop),
      // Bug 1972427: This used to be migrateEnrollmentsToSql, but we had to
      // re-create the NimbusEnrollments table with an altered schema and re-run
      // the migration to ensure recipe was never null.
      migration("noop", migrateNoop),
      migration("import-enrollments-to-sql", migrateEnrollmentsToSql),
      migration(
        "graduate-firefox-labs-auto-pip",
        migrateGraduateFirefoxLabsAutoPip
      ),
    ],

    [Phase.AFTER_REMOTE_SETTINGS_UPDATE]: [
      migration("firefox-labs-enrollments", migrateFirefoxLabsEnrollments),
    ],
  },
};
