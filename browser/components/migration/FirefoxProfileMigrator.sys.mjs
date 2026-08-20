/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Migrates from a Firefox profile in a lossy manner in order to clean up a
 * user's profile.  Data is only migrated where the benefits outweigh the
 * potential problems caused by importing undesired/invalid configurations
 * from the source profile.
 */

import { MigrationUtils } from "resource:///modules/MigrationUtils.sys.mjs";

import { MigratorBase } from "resource:///modules/MigratorBase.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  PlacesBackups: "resource://gre/modules/PlacesBackups.sys.mjs",
  ProfileAge: "resource://gre/modules/ProfileAge.sys.mjs",
  SessionMigration:
    "moz-src:///browser/components/sessionstore/SessionMigration.sys.mjs",
});

/**
 * Firefox profile migrator. Currently, this class only does "pave over"
 * migrations, where various parts of an old profile overwrite a new
 * profile. This is distinct from other migrators which attempt to import
 * old profile data into the existing profile.
 *
 * This migrator is what powers the "Profile Refresh" mechanism.
 */
export class FirefoxProfileMigrator extends MigratorBase {
  static get key() {
    return "firefox";
  }

  static get displayNameL10nID() {
    return "migration-wizard-migrator-display-name-firefox";
  }

  static get brandImage() {
    return "chrome://branding/content/icon128.png";
  }

  /**
   * Return a map of all migratable Firefox profiles.
   *
   * Subclasses can override this to provide profiles from different sources
   * (e.g. selectable profiles instead of toolkit profiles).
   *
   * @returns {Promise<Map<string, object>>} A map keyed by absolute profile path, where
   *   each value is an object with the following properties:
   *   - {string} id - The absolute profile path (same as the key).
   *   - {string} name - The display name of the profile.
   *   - {nsIFile} rootDir - The root directory of the profile.
   */
  async getAllProfiles() {
    let allProfiles = new Map();
    let profileService = Cc[
      "@mozilla.org/toolkit/profile-service;1"
    ].getService(Ci.nsIToolkitProfileService);
    for (let profile of profileService.profiles) {
      let rootDir = profile.rootDir;
      let path = rootDir.path;

      if (
        rootDir.exists() &&
        rootDir.isReadable() &&
        !rootDir.equals(MigrationUtils.profileStartup.directory)
      ) {
        allProfiles.set(path, {
          id: path,
          name: profile.name,
          rootDir,
        });
      }
    }
    return allProfiles;
  }

  async getSourceProfiles() {
    let sorter = (a, b) => {
      return a.name
        .toLocaleLowerCase()
        .localeCompare(b.name.toLocaleLowerCase());
    };

    let profiles = await this.getAllProfiles();

    return [...profiles.values()]
      .map(p => ({
        id: p.id,
        name: p.name,
      }))
      .sort(sorter);
  }

  _getFileObject(dir, fileName) {
    let file = dir.clone();
    file.append(fileName);

    // File resources are monolithic.  We don't make partial copies since
    // they are not expected to work alone. Return null to avoid trying to
    // copy non-existing files.
    return file.exists() ? file : null;
  }

  /**
   * Gets the source profile directory for an object representing a profile.
   *
   * @param {object} aProfile An object containing an "id".
   *   The id must be an absolute path string to the source profile directory.
   * @returns {nsIFile} The directory of the source profile
   */
  async getSourceProfileDir(aProfile) {
    let sourceProfileDir = Cc["@mozilla.org/file/local;1"].createInstance(
      Ci.nsIFile
    );
    sourceProfileDir.initWithPath(aProfile.id);

    return sourceProfileDir;
  }

  async getResources(aProfile) {
    let sourceProfileDir = await this.getSourceProfileDir(aProfile);
    if (
      !sourceProfileDir ||
      !sourceProfileDir.exists() ||
      !sourceProfileDir.isReadable()
    ) {
      return null;
    }

    // Being a startup-only migrator, we can rely on
    // MigrationUtils.profileStartup being set.
    let currentProfileDir = MigrationUtils.profileStartup.directory;

    // Surely data cannot be imported from the current profile.
    if (sourceProfileDir.equals(currentProfileDir)) {
      return null;
    }

    return this.getResourcesInternal(sourceProfileDir, currentProfileDir);
  }

  getLastUsedDate() {
    // We always pretend we're really old, so that we don't mess
    // up the determination of which browser is the most 'recent'
    // to import from.
    return Promise.resolve(new Date(0));
  }

  /**
   * An internal function specific to FirefoxProfileMigrator that gets the
   * resources from the sourceProfileDir and copies the resources into
   * currentProfileDir when the migration resources are invoked.
   *
   * @param {nsIFile} sourceProfileDir The profile we are migrating from
   * @param {nsIFile} currentProfileDir The profile we are migrating into
   * @returns {Array<object>} An array of "migration resources" objects for the
   * sourceProfileDir to migrate into the currentProfileDir
   */
  getResourcesInternal(sourceProfileDir, currentProfileDir) {
    let getFileResource = (aMigrationType, aFileNames) => {
      let files = [];
      for (let fileName of aFileNames) {
        let file = this._getFileObject(sourceProfileDir, fileName);
        if (file) {
          files.push(file);
        }
      }
      if (!files.length) {
        return null;
      }
      return {
        type: aMigrationType,
        migrate(aCallback) {
          for (let file of files) {
            file.copyTo(currentProfileDir, "");
          }
          aCallback(true);
        },
      };
    };

    let _oldRawPrefsMemoized = null;
    async function readOldPrefs() {
      if (!_oldRawPrefsMemoized) {
        let prefsPath = PathUtils.join(sourceProfileDir.path, "prefs.js");
        if (await IOUtils.exists(prefsPath)) {
          _oldRawPrefsMemoized = await IOUtils.readUTF8(prefsPath, {
            encoding: "utf-8",
          });
        }
      }

      return _oldRawPrefsMemoized;
    }

    function savePrefs() {
      // If we've used the pref service to write prefs for the new profile, it's too
      // early in startup for the service to have a profile directory, so we have to
      // manually tell it where to save the prefs file.
      let newPrefsFile = currentProfileDir.clone();
      newPrefsFile.append("prefs.js");
      Services.prefs.savePrefFile(newPrefsFile);
    }

    function configureHomepage(resetSession) {
      // We just refreshed the profile, so don't show the profile reset prompt
      // on the homepage.
      Services.prefs.setBoolPref("browser.disableResetPrompt", true);
      if (resetSession) {
        // We're resetting the user's session, not creating a new one. Set the
        // homepage_override prefs so that the browser doesn't override our
        // session with an unwanted homepage.
        let buildID = Services.appinfo.platformBuildID;
        let mstone = Services.appinfo.platformVersion;
        Services.prefs.setCharPref(
          "browser.startup.homepage_override.mstone",
          mstone
        );
        Services.prefs.setCharPref(
          "browser.startup.homepage_override.buildID",
          buildID
        );
      }
    }

    let types = MigrationUtils.resourceTypes;
    let places = getFileResource(types.HISTORY, [
      "places.sqlite",
      "places.sqlite-wal",
    ]);
    let favicons = getFileResource(types.HISTORY, [
      "favicons.sqlite",
      "favicons.sqlite-wal",
    ]);
    let cookies = getFileResource(types.COOKIES, [
      "cookies.sqlite",
      "cookies.sqlite-wal",
    ]);
    let passwords = {
      name: "passwords", // name is used only by tests.
      type: types.PASSWORDS,
      migrate: async aCallback => {
        try {
          for (let fileName of [
            "logins.json",
            "logins.db",
            "logins.db-wal",
            "key3.db",
            "key4.db",
          ]) {
            let file = this._getFileObject(sourceProfileDir, fileName);
            if (file) {
              file.copyTo(currentProfileDir, "");
            }
          }

          // If the source profile had the Rust logins backend active, carry
          // that state over so the new profile adopts the copied logins.db
          // directly instead of re-migrating from the (now stale) logins.json
          // and wiping the copied store.
          let oldRawPrefs = await readOldPrefs();
          if (
            /^user_pref\("signon\.storage\.rust\.active",\s*true\)/m.test(
              oldRawPrefs
            )
          ) {
            Services.prefs.setBoolPref("signon.storage.rust.active", true);
            savePrefs();
          }
        } catch (e) {
          aCallback(false);
          return;
        }
        aCallback(true);
      },
    };
    let formData = getFileResource(types.FORMDATA, [
      "formhistory.sqlite",
      "autofill-profiles.json",
    ]);
    let bookmarksBackups = getFileResource(types.OTHERDATA, [
      lazy.PlacesBackups.profileRelativeFolderPath,
    ]);
    let dictionary = getFileResource(types.OTHERDATA, ["persdict.dat"]);

    // Determine if we want to restore the previous session or start a new one
    const NEW_SESSION = "0";
    const RESTORE_SESSION = "1";
    let resetSession = Services.env.get("MOZ_RESET_PROFILE_SESSION");
    Services.env.set("MOZ_RESET_PROFILE_SESSION", "");

    let session;
    if (resetSession === RESTORE_SESSION) {
      // We only want to restore the previous firefox session if the profile
      // refresh was triggered by the user, such as through about:support. In
      // these cases, MOZ_RESET_PROFILE_SESSION is set to restore, signaling
      // that session data migration is required.
      let sessionCheckpoints = this._getFileObject(
        sourceProfileDir,
        "sessionCheckpoints.json"
      );
      let sessionFile = this._getFileObject(
        sourceProfileDir,
        "sessionstore.jsonlz4"
      );
      if (sessionFile) {
        session = {
          type: types.SESSION,
          migrate(aCallback) {
            sessionCheckpoints.copyTo(
              currentProfileDir,
              "sessionCheckpoints.json"
            );
            let newSessionFile = currentProfileDir.clone();
            newSessionFile.append("sessionstore.jsonlz4");
            let migrationPromise = lazy.SessionMigration.migrate(
              sessionFile.path,
              newSessionFile.path
            );
            migrationPromise.then(
              function () {
                // Force the browser to one-off resume the session that we give it:
                Services.prefs.setBoolPref(
                  "browser.sessionstore.resume_session_once",
                  true
                );
                configureHomepage(true);
                savePrefs();
                aCallback(true);
              },
              function () {
                aCallback(false);
              }
            );
          },
        };
      }
    } else if (resetSession === NEW_SESSION) {
      // If this is first startup and the profile refresh was triggered via the
      // command line, such as through the stub installer, we do not restore the
      // previous session.
      configureHomepage();
      savePrefs();
    }

    // Sync/FxA related data
    let sync = {
      name: "sync", // name is used only by tests.
      type: types.OTHERDATA,
      migrate: async aCallback => {
        // Try and parse a signedInUser.json file from the source directory and
        // if we can, copy it to the new profile and set sync's username pref
        // (which acts as a de-facto flag to indicate if sync is configured)
        try {
          let oldPath = PathUtils.join(
            sourceProfileDir.path,
            "signedInUser.json"
          );
          let exists = await IOUtils.exists(oldPath);
          if (exists) {
            let data = await IOUtils.readJSON(oldPath);
            if (data && data.accountData && data.accountData.email) {
              let username = data.accountData.email;
              // copy the file itself.
              await IOUtils.copy(
                oldPath,
                PathUtils.join(currentProfileDir.path, "signedInUser.json")
              );
              // Now we need to know whether Sync is actually configured for this
              // user. The only way we know is by looking at the prefs file from
              // the old profile. We avoid trying to do a full parse of the prefs
              // file and even avoid parsing the single string value we care
              // about.
              let oldRawPrefs = await readOldPrefs();
              if (/^user_pref\("services\.sync\.username"/m.test(oldRawPrefs)) {
                // sync's configured in the source profile - ensure it is in the
                // new profile too.
                // Write it to prefs.js and flush the file.
                Services.prefs.setStringPref(
                  "services.sync.username",
                  username
                );
                savePrefs();
              }
            }
          }
        } catch (ex) {
          aCallback(false);
          return;
        }
        aCallback(true);
      },
    };

    // Telemetry related migrations.
    let times = {
      name: "times", // name is used only by tests.
      type: types.OTHERDATA,
      migrate: aCallback => {
        let file = this._getFileObject(sourceProfileDir, "times.json");
        if (file) {
          file.copyTo(currentProfileDir, "");
        }
        // And record the fact a migration (ie, a reset) happened.
        let recordMigration = async () => {
          try {
            let profileTimes = await lazy.ProfileAge(currentProfileDir.path);
            await profileTimes.recordProfileReset();
            aCallback(true);
          } catch (e) {
            aCallback(false);
          }
        };

        recordMigration();
      },
    };
    let telemetry = {
      name: "telemetry", // name is used only by tests...
      type: types.OTHERDATA,
      migrate: async aCallback => {
        let createSubDir = name => {
          let dir = currentProfileDir.clone();
          dir.append(name);
          dir.create(Ci.nsIFile.DIRECTORY_TYPE, lazy.FileUtils.PERMS_DIRECTORY);
          return dir;
        };

        // If the 'datareporting' directory exists we migrate files from it.
        let dataReportingDir = this._getFileObject(
          sourceProfileDir,
          "datareporting"
        );
        if (dataReportingDir && dataReportingDir.isDirectory()) {
          // Copy only specific files.
          let toCopy = ["state.json", "session-state.json"];

          let dest = createSubDir("datareporting");
          let enumerator = dataReportingDir.directoryEntries;
          while (enumerator.hasMoreElements()) {
            let file = enumerator.nextFile;
            if (file.isDirectory() || !toCopy.includes(file.leafName)) {
              continue;
            }
            file.copyTo(dest, "");
          }
        }

        try {
          let oldRawPrefs = await readOldPrefs();
          let writePrefs = false;
          const PREFS = ["bookmarks", "csvpasswords", "history", "passwords"];

          for (let pref of PREFS) {
            let fullPref = `browser\.migrate\.interactions\.${pref}`;
            let regex = new RegExp('^user_pref\\("' + fullPref, "m");
            if (regex.test(oldRawPrefs)) {
              Services.prefs.setBoolPref(fullPref, true);
              writePrefs = true;
            }
          }

          if (writePrefs) {
            savePrefs();
          }
        } catch (e) {
          aCallback(false);
          return;
        }

        aCallback(true);
      },
    };

    return [
      places,
      cookies,
      passwords,
      formData,
      dictionary,
      bookmarksBackups,
      session,
      sync,
      times,
      telemetry,
      favicons,
    ].filter(r => r);
  }

  get startupOnlyMigrator() {
    return true;
  }
}
