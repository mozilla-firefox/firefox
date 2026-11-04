/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  FirefoxLabs: "resource://nimbus/FirefoxLabs.sys.mjs",
  NimbusEnrollments: "resource://nimbus/lib/Enrollments.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusTelemetry: "resource://nimbus/lib/Telemetry.sys.mjs",
  ProfilesDatastoreService:
    "moz-src:///toolkit/profile/ProfilesDatastoreService.sys.mjs",
  RemoteSettingsSyncError:
    "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs",
  ShutdownStartedError:
    "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs",
  UnenrollmentCause: "resource://nimbus/lib/ExperimentManager.sys.mjs",

  log: () => {
    const { Logger } = ChromeUtils.importESModule(
      "resource://messaging-system/lib/Logger.sys.mjs"
    );
    return new Logger("NimbusMigrations");
  },
});

function isBackgroundTaskMode() {
  const bts = Cc["@mozilla.org/backgroundtasks;1"]?.getService(
    Ci.nsIBackgroundTasks
  );

  return bts?.isBackgroundTaskMode ?? false;
}

function getLegacyStorePath() {
  if (Cu.isInAutomation) {
    try {
      return Services.prefs.getStringPref("nimbus.persistence.legacyStorePath");
    } catch {}
  }
  return PathUtils.join(PathUtils.profileDir, "ExperimentDataStore.json");
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
    "urlbar-ime-search": "firefox-labs-urlbar-ime-search",
  };

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

/**
 * Delete the databases, files and preferences left behind by AddonStudies,
 * AddonRollouts, PreferenceExperiments, PreferenceRollouts and Storage which
 * were removed in bug 2059778.
 */
async function migrateRemoveNormandyDatabases() {
  function deleteDatabase(name) {
    return new Promise((resolve, reject) => {
      const request = indexedDB.deleteDatabase(name);
      request.onsuccess = () => resolve();
      request.onblocked = () => reject(new Error(`Cannot delete DB ${name}`));
      request.onerror = () => reject(request.error);
    });
  }

  function deleteProfileFile(name) {
    return IOUtils.remove(PathUtils.join(PathUtils.profileDir, name));
  }

  await Promise.allSettled([
    ...["shield", "normandy-addon-rollout", "normandy-preference-rollout"].map(
      deleteDatabase
    ),
    ...["shield-preference-experiments.json", "shield-recipe-client.json"].map(
      deleteProfileFile
    ),
  ]);

  Services.prefs.deleteBranch("app.normandy.startupExperimentPrefs.");
  Services.prefs.deleteBranch("app.normandy.startupRolloutPrefs.");
}

async function migrateEnrollmentsToSql() {
  if (!lazy.NimbusEnrollments.persistenceEnabled) {
    // We are in a test.
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

  let legacyStoreData = null;

  try {
    legacyStoreData = await IOUtils.readJSON(getLegacyStorePath());
  } catch (e) {
    if (DOMException.isInstance(e) && e.name === "NotFoundError") {
      // The legacy store does not exist. There is nothing to migrate.
      return;
    }
  }

  // Ensure we are copying the data from the JSONFile explicitly because if the
  // NimbusEnrollments table is the source of truth, then getAll() will return
  // an empty array.
  const enrollments = Object.values(legacyStoreData);

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

  // These now exist in the database and in the ExperimentStore's JSONFile
  // data. However, the regular ExperimentStore data will not have been
  // populated yet (because it will have read zero rows from the database
  // during `SharedDataMap.init()`.
  const store = lazy.ExperimentAPI.manager.store;
  store._data = legacyStoreData;
  store._syncToChildren({ flush: true });
}

/**
 * Graduate a Firefox Labs opt-in! 🎓
 *
 * Migrations should use this function when features controlled by labs become
 * default-enabled. The user will be unenrolled from the rollout while keeping
 * the prefs set by the enrollment.
 *
 * @param {string} migration The name of the migration.
 * @param {string[]} slugs The slug of the rollout(s) to migrate out of.
 */
function graduateLabs(migration, slugs) {
  if (isBackgroundTaskMode()) {
    // This migration does not apply to background task mode.
    lazy.log.debug(`${migration}: skipping (is background task mode)`);
    return;
  }

  for (const slug of slugs) {
    const enrollment = lazy.ExperimentAPI.manager.store.get(slug);
    if (!enrollment?.active) {
      continue;
    }

    lazy.ExperimentAPI.manager._unenroll(
      enrollment,
      lazy.UnenrollmentCause.Migration(migration),
      { unsetEnrollmentPrefs: false }
    );
  }
}

/**
 * Migrate enrollment from the firefox-labs-auto-pip rollout.
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
  graduateLabs(migration, ["firefox-labs-auto-pip"]);
}

/**
 * Migrate enrollment from the firefox-labs-jpeg-xl rollout.
 *
 * This rollout was only available in Nightly and the feature is becoming on by
 * default in Nightly. We need to unenroll users without resetting the prefs
 * controlled by the feature.
 */
function migrateGraduateFirefoxLabsJPEGXLNightly(migration) {
  if (!AppConstants.MOZ_JXL) {
    lazy.log.debug(`${migration}: skipping (MOZ_JXL disabled)`);
    return;
  }

  graduateLabs(migration, ["firefox-labs-jpeg-xl"]);
}

function migrateRestorePrefFlipsBug2054546(migration) {
  const experimentManager = lazy.ExperimentAPI.manager;

  function unenrollForMigration(enrollment) {
    // UnenrollmentCause.Migration was added in Firefox 147.
    experimentManager._unenroll(enrollment, { reason: "migration", migration });
  }

  {
    const enrollment = experimentManager.store.get(
      "detectportal-fastly-testing"
    );
    if (enrollment) {
      const pref = "captivedetect.canonicalURL";
      if (enrollment.active) {
        // The prefFlips feature is not fully initialized at this point and
        // won't reset the pref when we unenroll, so we have to do it ourselves.
        unenrollForMigration(enrollment);
        Services.prefs.clearUserPref(pref);
      } else if (
        Services.prefs.getStringPref(pref) ===
        "http://detectportal.firefox.com/canonical.html"
      ) {
        Services.prefs.clearUserPref(pref);
      }
    }
  }

  {
    const originalEnrollment = experimentManager.store.get(
      "hnt-nova-pref-flip-rollout-content-market-widget-exp-cohort"
    );
    if (originalEnrollment) {
      if (originalEnrollment.active) {
        unenrollForMigration(originalEnrollment);
      }

      const fixupEnrollment = lazy.ExperimentAPI.manager.store.get(
        "hnt-nova-pref-flip-restore"
      );
      if (fixupEnrollment?.active) {
        unenrollForMigration(fixupEnrollment);
      }

      Services.prefs.clearUserPref(
        "browser.newtabpage.activity-stream.nova.enabled"
      );
    }
  }

  {
    // disabling-chips-for-v131 ended 2024-12-17 and thus will no longer be
    // stored in the enrollment history.
    const enrollment = experimentManager.store.get("restore-chips-133");
    if (enrollment) {
      if (enrollment?.active) {
        // The prefFlips feature is not fully initialized at this point and
        // won't reset the pref when we unenroll, so we have to do it ourselves.
        unenrollForMigration(enrollment);
      }
    }
    // Unfortunately we can't determine if the user was affected, so we must
    // reset this pref for all users.
    Services.prefs.clearUserPref("network.cookie.CHIPS.enabled");
  }
}

function migrateGraduateFirefoxLabsJPEGXLAllChannels(migration) {
  if (!AppConstants.MOZ_JXL) {
    lazy.log.debug(`${migration}: skipping (MOZ_JXL disabled)`);
    return;
  }

  graduateLabs(migration, [
    "firefox-labs-jpeg-xl-beta",
    "firefox-labs-jpeg-xl-deved",
    "firefox-labs-jpeg-xl-release",
  ]);
}

async function migrateRemoveLegacyStore() {
  try {
    await IOUtils.remove(getLegacyStorePath(), { ignoreAbsent: true });
  } catch {}
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

        // Set the original value to `null` instead of `false` so that it will
        // restore by removing the user branch value instead of setting it to
        // false if the default branch value changes.
        //
        // TODO(bug 2086737): Remove support for originalPrefValues in
        // the entire enrollment pipeline.
        await labs.enroll(slug, "control", {
          originalPrefValues: new Map([[pref, null]]),
        });
      }
    },
    { mode: "shared" }
  );
}

export class MigrationError extends Error {
  static Reason = Object.freeze({
    UNKNOWN: "unknown",
    SHUTDOWN_STARTED: "shutdown-started",
  });

  constructor(reason) {
    super(`Migration error: ${reason}`);
    this.reason = reason;
  }

  static getReason(e) {
    if (e instanceof MigrationError) {
      return e.reason;
    }

    if (e instanceof lazy.ShutdownStartedError) {
      return MigrationError.Reason.SHUTDOWN_STARTED;
    }

    return MigrationError.Reason.UNKNOWN;
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

      const migrationStart = ChromeUtils.now();
      let duration;
      try {
        // Not all migrations are async fns, so coerce them.
        await Promise.try(migration.fn, migration.name).finally(() => {
          duration = Math.ceil(ChromeUtils.now() - migrationStart);
        });
      } catch (e) {
        lazy.log.error(
          `applyMigrations: error running migration ${i} (${migration.name}): ${e}`
        );

        const reason = MigrationError.getReason(e);
        lazy.NimbusTelemetry.recordMigration(migration.name, duration, reason);

        break;
      }

      lastSuccess = i;

      lazy.log.debug(
        `applyMigrations: applied migration ${i}: ${migration.name}`
      );

      lazy.NimbusTelemetry.recordMigration(migration.name, duration);
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
      migration("remove-normandy-databases", migrateRemoveNormandyDatabases),
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
      migration(
        "graduate-firefox-labs-jpeg-xl",
        migrateGraduateFirefoxLabsJPEGXLNightly
      ),
      migration("bug-2054546-mitigation", migrateRestorePrefFlipsBug2054546),
      migration(
        "graduate-firefox-labs-jpeg-xl-all-channels",
        migrateGraduateFirefoxLabsJPEGXLAllChannels
      ),
      migration("remove-legacy-store", migrateRemoveLegacyStore),
    ],

    [Phase.AFTER_REMOTE_SETTINGS_UPDATE]: [
      migration("firefox-labs-enrollments", migrateFirefoxLabsEnrollments),
    ],
  },
};
