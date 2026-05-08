/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { BackupResource } from "resource:///modules/backup/BackupResource.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  SelectableProfileService:
    "resource:///modules/profiles/SelectableProfileService.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    prefix: "PreferencesBackupResource",
    maxLogLevel: Services.prefs.getBoolPref("browser.backup.log", false)
      ? "Debug"
      : "Warn",
  });
});

const PROFILE_RESTORATION_DATE_PREF = "browser.backup.profile-restoration-date";
const PROFILES_ENABLED_PREF = "browser.profiles.enabled";
const PROFILES_CREATED_PREF = "browser.profiles.created";
const STOREID_PREF = "toolkit.profiles.storeID";
const WALLPAPER_TYPE_PREF =
  "browser.newtabpage.activity-stream.newtabWallpapers.wallpaper";
const CUSTOM_WALLPAPER_UUID_PREF =
  "browser.newtabpage.activity-stream.newtabWallpapers.customWallpaper.uuid";
const CUSTOM_WALLPAPER_FOLDER = "wallpaper";

/**
 * Class representing files that modify preferences and permissions within a user profile.
 */
export class PreferencesBackupResource extends BackupResource {
  static get key() {
    return "preferences";
  }

  static get requiresEncryption() {
    return false;
  }

  static get dataCollectionPrefs() {
    return [
      "browser.discovery.enabled",
      "app.shield.optoutstudies.enabled",
      "datareporting.healthreport.uploadEnabled",
      "datareporting.usage.uploadEnabled",
      "browser.crashReports.unsubmittedCheck.autoSubmit2",
    ];
  }

  /**
   * Adds prefs to the override map that are currently set but should not be
   * included in the backup.  Override them with null values to prevent
   * serialization.
   *
   * @param {nsIPrefOverrideMap} prefsOverrideMap
   * @returns {nsIPrefOverrideMap} prefsOverrideMap with ignored prefs added
   */
  static addPrefsToIgnoreInBackup(prefsOverrideMap) {
    // List of prefs we never backup.
    let kIgnoredPrefs = [
      "app.normandy.user_id",
      "toolkit.telemetry.cachedClientID",
      "toolkit.telemetry.cachedProfileGroupID",
      // We don't want any recovered profiles to manage the original profile's shortcut.
      "browser.profiles.shortcutFileName",
      PROFILE_RESTORATION_DATE_PREF,
    ];

    const backupPrefs = Services.prefs.getChildList("browser.backup.");
    kIgnoredPrefs = kIgnoredPrefs.concat(backupPrefs);

    for (const pref of kIgnoredPrefs) {
      if (Services.prefs.getPrefType(pref) !== Services.prefs.PREF_INVALID) {
        prefsOverrideMap.addEntry(pref, null);
      }
    }

    // Prefs with this prefix are always overriden.
    const kNimbusMetadataPrefPrefix = "nimbus.";
    const kNimbusPrefExceptionList = ["nimbus.rollouts.enabled"];

    const nimbusPrefs = Services.prefs.getChildList(kNimbusMetadataPrefPrefix);
    for (const pref of nimbusPrefs) {
      if (kNimbusPrefExceptionList.includes(pref)) {
        continue;
      }

      prefsOverrideMap.addEntry(pref, null);
    }

    return prefsOverrideMap;
  }

  /**
   * Parses preferences from a prefs.js file buffer.
   *
   * @param {Uint8Array} prefsBuffer - The raw bytes of a prefs.js file.
   * @param {string[]} [prefNames] - Optional list of pref names to extract.
   *                                 If not provided, returns all prefs.
   * @returns {Map<string, *>} Map of pref names to their values.
   */
  static getPrefsFromBuffer(prefsBuffer, prefNames = null) {
    const prefSet = prefNames ? new Set(prefNames) : null;
    const prefs = new Map();

    const addPref = (_kind, name, value) => {
      if (!prefSet || prefSet.has(name)) {
        prefs.set(name, value);
      }
    };

    Services.prefs.parsePrefsFromBuffer(prefsBuffer, {
      onStringPref: addPref,
      onIntPref: addPref,
      onBoolPref: addPref,
      onError(_message) {
        // ignore any errors here, we'll use the default value when evaluating
      },
    });

    return prefs;
  }

  async backup(
    stagingPath,
    profilePath = PathUtils.profileDir,
    _isEncrypting = false
  ) {
    // These are files that can be simply copied into the staging folder using
    // IOUtils.copy.
    const simpleCopyFiles = [
      "xulstore.json",
      "containers.json",
      "customKeys.json",
      "handlers.json",
      "search.json.mozlz4",
      "user.js",
      "chrome",
    ];
    await BackupResource.copyFiles(profilePath, stagingPath, simpleCopyFiles);

    const WALLPAPER_TYPE = Services.prefs.getStringPref(
      WALLPAPER_TYPE_PREF,
      ""
    );
    const WALLPAPER_UUID = Services.prefs.getStringPref(
      CUSTOM_WALLPAPER_UUID_PREF,
      ""
    );
    if (WALLPAPER_TYPE == "custom" && WALLPAPER_UUID) {
      await BackupResource.copyFiles(
        PathUtils.join(profilePath, CUSTOM_WALLPAPER_FOLDER),
        PathUtils.join(stagingPath, CUSTOM_WALLPAPER_FOLDER),
        [WALLPAPER_UUID]
      );
    }

    // prefs.js is a special case - we have a helper function to flush the
    // current prefs state to disk off of the main thread.
    let prefsDestPath = PathUtils.join(stagingPath, "prefs.js");
    let prefsDestFile = await IOUtils.getFile(prefsDestPath);
    await lazy.ExperimentAPI._rsLoader.withUpdateLock(async () => {
      await Services.prefs.backupPrefFile(
        prefsDestFile,
        PreferencesBackupResource.addPrefsToIgnoreInBackup(
          lazy.ExperimentAPI.manager.store.getOriginalPrefValuesForAllActiveEnrollments()
        )
      );
    });

    // During recovery, we need to recompute verification hashes for any
    // custom engines, but only for engines that were originally passing
    // verification. We store just the profile directory name rather than
    // the full path so that cross-platform recovery works (a macOS path
    // would not be parseable by PathUtils.filename on Windows).
    return { profileDirName: PathUtils.filename(profilePath) };
  }

  async recover(manifestEntry, recoveryPath, destProfilePath) {
    const SEARCH_PREF_FILENAME = "search.json.mozlz4";
    const RECOVERY_SEARCH_PREF_PATH = PathUtils.join(
      recoveryPath,
      SEARCH_PREF_FILENAME
    );

    if (await IOUtils.exists(RECOVERY_SEARCH_PREF_PATH)) {
      // search.json.mozlz4 may contain hash values that need to be recomputed
      // now that the profile directory has changed.
      let searchPrefs = await IOUtils.readJSON(RECOVERY_SEARCH_PREF_PATH, {
        decompress: true,
      });

      // ... but we only want to do this for engines that had valid verification
      // hashes for the original profile directory.
      // Prefer profileDirName (cross-platform safe). Fall back to profilePath
      // for backups created before profileDirName was introduced.
      const ORIGINAL_DIR_NAME =
        manifestEntry.profileDirName ??
        (manifestEntry.profilePath
          ? PathUtils.filename(manifestEntry.profilePath)
          : null);

      if (ORIGINAL_DIR_NAME) {
        let destDirName = PathUtils.filename(destProfilePath);

        searchPrefs.engines = searchPrefs.engines.map(engine => {
          if (engine._metaData.loadPathHash) {
            let loadPath = engine._loadPath;
            if (
              engine._metaData.loadPathHash ==
              lazy.SearchUtils.getVerificationHash(loadPath, ORIGINAL_DIR_NAME)
            ) {
              engine._metaData.loadPathHash =
                lazy.SearchUtils.getVerificationHash(loadPath, destDirName);
            }
          }
          return engine;
        });

        if (
          searchPrefs.metaData.defaultEngineIdHash &&
          searchPrefs.metaData.defaultEngineIdHash ==
            lazy.SearchUtils.getVerificationHash(
              searchPrefs.metaData.defaultEngineId,
              ORIGINAL_DIR_NAME
            )
        ) {
          searchPrefs.metaData.defaultEngineIdHash =
            lazy.SearchUtils.getVerificationHash(
              searchPrefs.metaData.defaultEngineId,
              destDirName
            );
        }

        if (
          searchPrefs.metaData.privateDefaultEngineIdHash &&
          searchPrefs.metaData.privateDefaultEngineIdHash ==
            lazy.SearchUtils.getVerificationHash(
              searchPrefs.metaData.privateDefaultEngineId,
              ORIGINAL_DIR_NAME
            )
        ) {
          searchPrefs.metaData.privateDefaultEngineIdHash =
            lazy.SearchUtils.getVerificationHash(
              searchPrefs.metaData.privateDefaultEngineId,
              destDirName
            );
        }
      }

      await IOUtils.writeJSON(
        PathUtils.join(destProfilePath, SEARCH_PREF_FILENAME),
        searchPrefs,
        { compress: true }
      );
    }

    const simpleCopyFiles = [
      "prefs.js",
      "xulstore.json",
      "containers.json",
      "customKeys.json",
      "handlers.json",
      "user.js",
      "chrome",
      CUSTOM_WALLPAPER_FOLDER,
    ];
    await BackupResource.copyFiles(
      recoveryPath,
      destProfilePath,
      simpleCopyFiles
    );

    const LINEBREAK = AppConstants.platform === "win" ? "\r\n" : "\n";
    let prefsFile = await IOUtils.getFile(destProfilePath);
    prefsFile.append("prefs.js");
    // We should always have recovered a prefs.js but, if we didn't for any
    // reason, we can still write the timestamp.  Since we are creating the
    // prefs.js file, we need to add the preamble.
    const includePreamble = !(await IOUtils.exists(prefsFile.path));
    let addToPrefsJs = includePreamble ? Services.prefs.prefsJsPreamble : "";

    // Append browser.backup.scheduled.last-backup-file to prefs.js with the
    // current timestamp.
    addToPrefsJs += `user_pref("${PROFILE_RESTORATION_DATE_PREF}", ${Math.round(Date.now() / 1000)});${LINEBREAK}`;

    await IOUtils.writeUTF8(prefsFile.path, addToPrefsJs, {
      mode: "appendOrCreate",
    });

    // If selectable profile's aren't enabled on the current profile, we need to make sure that
    // we don't use stale prefs from the backup
    if (!lazy.SelectableProfileService.isEnabled) {
      let setToLegacyPrefs =
        `user_pref("${PROFILES_ENABLED_PREF}", ${Services.prefs.getBoolPref(PROFILES_ENABLED_PREF, false)});${LINEBREAK}` +
        `user_pref("${PROFILES_CREATED_PREF}", ${Services.prefs.getBoolPref(PROFILES_CREATED_PREF, false)});${LINEBREAK}` +
        `user_pref("${STOREID_PREF}", "");${LINEBREAK}`;

      await IOUtils.writeUTF8(prefsFile.path, setToLegacyPrefs, {
        mode: "appendOrCreate",
      });
    } else if (lazy.SelectableProfileService.currentProfile) {
      lazy.logConsole.debug(
        `We're recovering into a profile group, let's make sure to set the right selectable profile prefs`
      );

      // Before adding prefs to the db, let's make sure we choose the most restrictive settings
      // for data collection in the group.
      const dataCollectionPrefs = PreferencesBackupResource.dataCollectionPrefs;

      const prefsFilePath = PathUtils.join(recoveryPath, "prefs.js");
      const prefsBuffer = await IOUtils.read(prefsFilePath);
      const backupPrefs = PreferencesBackupResource.getPrefsFromBuffer(
        prefsBuffer,
        dataCollectionPrefs
      );
      const defaults = Services.prefs.getDefaultBranch(null);

      for (let pref of dataCollectionPrefs) {
        let groupPrefValue =
          await lazy.SelectableProfileService.getDBPref(pref);
        let backupPrefValue = backupPrefs.has(pref)
          ? backupPrefs.get(pref)
          : defaults.getBoolPref(pref, false);

        // the group has it enabled, but our backup has it disabled!
        if (groupPrefValue && !backupPrefValue) {
          Services.prefs.setBoolPref(pref, false);
        }
      }

      // Since the user might have messed with their prefs, let's make sure to
      // update the selectable profile specific ones (including the shared prefs db)
      await lazy.SelectableProfileService.addSelectableProfilePrefs(
        destProfilePath
      );
    }

    return null;
  }

  async measure(profilePath = PathUtils.profileDir) {
    const files = [
      "prefs.js",
      "xulstore.json",
      "containers.json",
      "customKeys.json",
      "handlers.json",
      "search.json.mozlz4",
      "user.js",
    ];
    let fullSize = 0;

    for (let filePath of files) {
      let resourcePath = PathUtils.join(profilePath, filePath);
      let resourceSize = await BackupResource.getFileSize(resourcePath);
      if (Number.isInteger(resourceSize)) {
        fullSize += resourceSize;
      }
    }

    const chromeDirectoryPath = PathUtils.join(profilePath, "chrome");
    let chromeDirectorySize =
      await BackupResource.getDirectorySize(chromeDirectoryPath);
    if (Number.isInteger(chromeDirectorySize)) {
      fullSize += chromeDirectorySize;
    }

    Glean.browserBackup.preferencesSize.set(fullSize);
  }
}
