/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { DeferredTask } from "resource://gre/modules/DeferredTask.sys.mjs";

const NOTIFY_TIMEOUT = 200;
const STOREID_PREF_NAME = "toolkit.profiles.storeID";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  Sqlite: "resource://gre/modules/Sqlite.sys.mjs",
});

class ProfilesDatastoreServiceClass {
  #connection = null;
  #asyncShutdownBlocker = null;
  #initialized = false;
  #storeID = null;
  #initPromise = null;
  #notifyTask = null;
  #profileService = null;
  static #dirSvc = null;

  /**
   * Gets a connection to the database.
   *
   * Use this connection to query existing tables, but never to create or
   * modify schemas. Any schema changes should be added as a new migration,
   * see `createTables()`.
   *
   * Rethrows errors thrown by `Sqlite.openConnection()`; see details in
   * `Sqlite.sys.mjs`. TODO: document and handle errors (bug 1960963).
   */
  async getConnection() {
    await this.init();
    return this.#connection;
  }

  /**
   * Create or update tables in this shared cross-profile database.
   *
   * Includes simple forward-only migration support which applies any new
   * migrations based on the schema version.
   *
   * Notes for migration authors:
   *
   * Since a mix of Firefox versions may access the database at any time, all
   * schema changes must be backwards-compatible.
   *
   * Please keep your schemas as simple as possible, to reduce the odds of
   * corruption affecting all users of the database.
   */
  async createTables() {
    // TODO: (Bug 1902320) Handle exceptions on connection opening
    let currentVersion = await this.#connection.getSchemaVersion();
    if (currentVersion == 1) {
      return;
    }

    if (currentVersion < 1) {
      // Brand new database or created prior to migration support.
      await this.#connection.executeTransaction(async () => {
        const createProfilesTable = `
            CREATE TABLE IF NOT EXISTS "Profiles" (
              id  INTEGER NOT NULL,
              path	TEXT NOT NULL UNIQUE,
              name	TEXT NOT NULL,
              avatar	TEXT NOT NULL,
              themeId	TEXT NOT NULL,
              themeFg	TEXT NOT NULL,
              themeBg	TEXT NOT NULL,
              PRIMARY KEY(id)
            );`;

        await this.#connection.execute(createProfilesTable);

        const createSharedPrefsTable = `
            CREATE TABLE IF NOT EXISTS "SharedPrefs" (
              id	INTEGER NOT NULL,
              name	TEXT NOT NULL UNIQUE,
              value	BLOB,
              isBoolean	INTEGER,
              PRIMARY KEY(id)
            );`;

        await this.#connection.execute(createSharedPrefsTable);
      });
    }

    await this.#connection.setSchemaVersion(1);
  }

  /**
   * Trigger an async cross-instance notification that the data in the
   * datastore has been changed.
   *
   * Two different nsIObserver events may be fired, depending on whether the
   * change originated in the current Firefox instance or in another instance.
   *
   * Changes to the datastore made by the current instance will trigger the
   * "pds-datastore-changed" nsIObserver event; see `#datastoreChanged` for
   * details. These events are fired whether or not the multiple profiles
   * feature is enabled.
   *
   * If the multiple profiles feature is enabled, then changes to the datastore
   * made either by the current instance or another instance in the profile
   * group will trigger the "sps-profiles-updated" nsIObserver event. See
   * `SelectableProfileService.databaseChanged` for details.
   */
  notify() {
    this.#notifyTask.arm();
  }

  /**
   * Notify datastore observers that the data has changed by firing
   * the "pds-datastore-changed" nsIObserver signal.
   *
   *@param {"local"|"shutdown"} source The source of the
   *   notification. Either "local" meaning that the change was made in this
   *   process and "shutdown" meaning we are closing the connection and
   *   shutting down.
   */
  #datastoreChanged(source) {
    Services.obs.notifyObservers(null, "pds-datastore-changed", source);
  }

  get storeID() {
    return new Promise(resolve => {
      this.init().then(() => {
        resolve(this.#storeID);
      });
    });
  }

  get initialized() {
    return this.#initialized;
  }

  static get PROFILE_GROUPS_DIR() {
    if (this.#dirSvc && "ProfileGroups" in this.#dirSvc) {
      return this.#dirSvc.ProfileGroups;
    }

    return PathUtils.join(
      ProfilesDatastoreServiceClass.getDirectory("UAppData").path,
      "Profile Groups"
    );
  }

  overrideDirectoryService(dirSvc) {
    if (!Cu.isInAutomation) {
      return;
    }

    ProfilesDatastoreServiceClass.#dirSvc = dirSvc;
  }

  static getDirectory(id) {
    if (this.#dirSvc) {
      if (id in this.#dirSvc) {
        return this.#dirSvc[id].clone();
      }
    }

    return Services.dirsvc.get(id, Ci.nsIFile);
  }

  /**
   * For use in testing only, override the profile service with a mock version
   * and reset state accordingly.
   *
   * @param {Ci.nsIToolkitProfileService} profileService The mock profile service
   */
  async resetProfileService(profileService) {
    if (!Cu.isInAutomation) {
      return;
    }

    await this.uninit();
    this.#profileService =
      profileService ??
      Cc["@mozilla.org/toolkit/profile-service;1"].getService(
        Ci.nsIToolkitProfileService
      );
    await this.init();
  }

  get toolkitProfileService() {
    return this.#profileService;
  }

  constructor() {
    this.#asyncShutdownBlocker = () => this.uninit();
    this.#profileService = Cc[
      "@mozilla.org/toolkit/profile-service;1"
    ].getService(Ci.nsIToolkitProfileService);
  }

  /**
   * At startup, get the groupDBPath from the prefs, and connect to it.
   *
   * @returns {Promise}
   */
  init() {
    if (!this.#initPromise) {
      this.#initPromise = this.#init().finally(
        () => (this.#initPromise = null)
      );
    }

    return this.#initPromise;
  }

  async #init() {
    if (this.#initialized) {
      return;
    }

    // We read the store ID from prefs but in early startup (for example in the profile selector)
    // this is not available so we get it from the current profile.
    this.#storeID = Services.startup.startingUp
      ? this.#profileService.currentProfile?.storeID
      : Services.prefs.getStringPref(STOREID_PREF_NAME, "");

    // This could fail if we're adding it during shutdown. In this case,
    // don't throw but don't continue initialization.
    try {
      lazy.AsyncShutdown.profileChangeTeardown.addBlocker(
        "ProfilesDatastoreService uninit",
        this.#asyncShutdownBlocker
      );
    } catch (ex) {
      console.error(ex);
      return;
    }

    this.#notifyTask = new DeferredTask(async () => {
      this.#datastoreChanged("local");
    }, NOTIFY_TIMEOUT);

    try {
      await this.#initConnection();
    } catch (e) {
      console.error(e);

      await this.uninit();
      return;
    }

    this.#initialized = true;
  }

  async uninit() {
    lazy.AsyncShutdown.profileChangeTeardown.removeBlocker(
      this.#asyncShutdownBlocker
    );

    // During shutdown we don't need to notify ourselves, just other instances
    // so rather than finalizing the task just disarm it and do the notification
    // manually.
    if (this.#notifyTask.isArmed) {
      this.#notifyTask.disarm();
      this.#datastoreChanged("shutdown");
    }

    await this.closeConnection();

    this.#storeID = null;

    this.#initialized = false;
  }

  async #initConnection() {
    if (this.#connection) {
      return;
    }

    let path = await this.getProfilesStorePath();

    // TODO: (Bug 1902320) Handle exceptions on connection opening
    // This could fail if the store is corrupted.
    this.#connection = await lazy.Sqlite.openConnection({
      path,
      openNotExclusive: true,
    });

    await this.#connection.execute("PRAGMA journal_mode = WAL");
    await this.#connection.execute("PRAGMA wal_autocheckpoint = 16");

    await this.createTables();
  }

  async closeConnection() {
    if (!this.#connection) {
      return;
    }

    // An error could occur while closing the connection. We suppress the
    // error since it is not a critical part of the browser.
    try {
      await this.#connection.close();
    } catch (ex) {}
    this.#connection = null;
  }

  async maybeCreateProfilesStorePath() {
    if (this.#storeID) {
      return;
    }

    await IOUtils.makeDirectory(
      ProfilesDatastoreServiceClass.PROFILE_GROUPS_DIR
    );

    const storageID = Services.uuid
      .generateUUID()
      .toString()
      .replace("{", "")
      .split("-")[0];

    this.#storeID = storageID;
    Services.prefs.setStringPref(STOREID_PREF_NAME, storageID);
  }

  async getProfilesStorePath() {
    await this.maybeCreateProfilesStorePath();

    // If we are not running in a named nsIToolkitProfile, the datastore path
    // should be in the profile directory. This is true in a local build or a
    // CI test build, for example.
    if (
      !this.#profileService.currentProfile &&
      !this.#profileService.groupProfile
    ) {
      return PathUtils.join(
        ProfilesDatastoreServiceClass.getDirectory("ProfD").path,
        `${this.#storeID}.sqlite`
      );
    }

    return PathUtils.join(
      ProfilesDatastoreServiceClass.PROFILE_GROUPS_DIR,
      `${this.#storeID}.sqlite`
    );
  }
}

const ProfilesDatastoreService = new ProfilesDatastoreServiceClass();
export { ProfilesDatastoreService };
