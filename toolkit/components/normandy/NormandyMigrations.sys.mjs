/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { Log } from "resource://gre/modules/Log.sys.mjs";

const BOOTSTRAP_LOGGER_NAME = "app.normandy.bootstrap";

const PREF_PREFIX = "app.normandy";
const LEGACY_PREF_PREFIX = "extensions.shield-recipe-client";
const PREF_LOGGING_LEVEL = "app.normandy.logging.level";
const PREF_MIGRATIONS_APPLIED = "app.normandy.migrationsApplied";

// Logging
const log = Log.repository.getLogger(BOOTSTRAP_LOGGER_NAME);
log.addAppender(new Log.ConsoleAppender(new Log.BasicFormatter()));
log.level = Services.prefs.getIntPref(PREF_LOGGING_LEVEL, Log.Level.Warn);

export const NormandyMigrations = {
  async applyAll() {
    let migrationsApplied = Services.prefs.getIntPref(
      PREF_MIGRATIONS_APPLIED,
      0
    );

    for (let i = migrationsApplied; i < this.migrations.length; i++) {
      await this.applyOne(i);
      migrationsApplied++;
      Services.prefs.setIntPref(PREF_MIGRATIONS_APPLIED, migrationsApplied);
    }
  },

  async applyOne(id) {
    const migration = this.migrations[id];
    log.debug(`Running Normandy migration ${migration.name}`);
    await migration();
  },

  migrations: [
    migrateShieldPrefs,
    migrateStudiesEnabledWithoutHealthReporting,
    migrateAddonStudyFieldsToSlugAndUserFacingFields,
    migrateNoop,
    migrateNoop,
    migrateNoop,
    migrateNoop,
    migrateNoop,
    migrateRemoveOldAddonStudyAction,
    migrateRemoveLastBuildIdPref,
    migrateNoop,
    migrateNoop,
  ],
};

function migrateShieldPrefs() {
  const legacyBranch = Services.prefs.getBranch(LEGACY_PREF_PREFIX + ".");
  const newBranch = Services.prefs.getBranch(PREF_PREFIX + ".");

  for (const prefName of legacyBranch.getChildList("")) {
    const legacyPrefType = legacyBranch.getPrefType(prefName);
    const newPrefType = newBranch.getPrefType(prefName);

    // If new preference exists and is not the same as the legacy pref, skip it
    if (
      newPrefType !== Services.prefs.PREF_INVALID &&
      newPrefType !== legacyPrefType
    ) {
      log.error(
        `Error migrating normandy pref ${prefName}; pref type does not match.`
      );
      continue;
    }

    // Now move the value over. If it matches the default, this will be a no-op
    switch (legacyPrefType) {
      case Services.prefs.PREF_STRING:
        newBranch.setCharPref(prefName, legacyBranch.getCharPref(prefName));
        break;

      case Services.prefs.PREF_INT:
        newBranch.setIntPref(prefName, legacyBranch.getIntPref(prefName));
        break;

      case Services.prefs.PREF_BOOL:
        newBranch.setBoolPref(prefName, legacyBranch.getBoolPref(prefName));
        break;

      case Services.prefs.PREF_INVALID:
        // This should never happen.
        log.error(
          `Error migrating pref ${prefName}; pref type is invalid (${legacyPrefType}).`
        );
        break;

      default:
        // This should never happen either.
        log.error(
          `Error getting startup pref ${prefName}; unknown value type ${legacyPrefType}.`
        );
    }

    legacyBranch.clearUserPref(prefName);
  }
}

function migrateAddonStudyFieldsToSlugAndUserFacingFields() {
  // Removed with AddonStudies. This migration renamed fields in the "shield"
  // database, which the Nimbus remove-normandy-databases migration now deletes.
}

function migrateNoop() {
  // Removed with PreferenceExperiments. These migrations updated the data in
  // shield-preference-experiments.json which the Nimbus
  // remove-normandy-databases migration now deletes.
}

function migrateRemoveOldAddonStudyAction() {
  // Removed with the branched-addon-study action. This migration was in place to
  // unenroll clients from studies enrolled by the old unbranched add-on study
  // action.
}

function migrateStudiesEnabledWithoutHealthReporting() {
  // Removed in https://bugzilla.mozilla.org/show_bug.cgi?id=1950452
  // This migration was in place so that users with telemetry disabled would not receive studies.
  // However, the result is that a new build in 2025 with telemetry disabled will permanently
  // disable studies until the user turns on both telemetry *and* studies. There are no longer any
  // normandy studies and so removing this should have no affect.
  // This is a hot fix until we remove the rest of normandy.
}

/**
 * Tracking last build ID is now done by comparing Services.appinfo.appBuildID
 * and Services.appinfo.lastAppBuildID. Remove the manual tracking.
 */
function migrateRemoveLastBuildIdPref() {
  Services.prefs.clearUserPref("app.normandy.last_seen_buildid");
}
