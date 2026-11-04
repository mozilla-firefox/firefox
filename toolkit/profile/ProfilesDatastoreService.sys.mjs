/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { DeferredTask } from "resource://gre/modules/DeferredTask.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const NOTIFY_TIMEOUT = 200;
const STOREID_PREF_NAME = "toolkit.profiles.storeID";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  Sqlite: "resource://gre/modules/Sqlite.sys.mjs",
});
ChromeUtils.defineLazyGetter(lazy, "MigrationUtils", () => {
  // MigrationUtils is currently only available in browser builds.
  if (AppConstants.MOZ_BUILD_APP !== "browser") {
    return undefined;
  }

  try {
    let { MigrationUtils } = ChromeUtils.importESModule(
      // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
      "moz-src:///browser/components/migration/MigrationUtils.sys.mjs"
    );
    return MigrationUtils;
  } catch (e) {
    console.error(`Unable to load MigrationUtils.sys.mjs: ${e}`);
  }
  return undefined;
});

/**
 * Gets the path to the shared profiles database for a storeID.
 *
 * @param {string} storeID The store ID
 * @returns {string} The path to the database file
 */
function getSharedProfilesStorePath(storeID) {
  return PathUtils.join(
    ProfilesDatastoreServiceClass.PROFILE_GROUPS_DIR,
    `${storeID}.sqlite`
  );
}

/**
 * Opens the datastore.
 *
 * @param {string} path The path to the database file
 * @returns {Promise<Connection>} The database connection
 */
async function openDatastoreConnection(path) {
  let connection = await lazy.Sqlite.openConnection({
    path,
    openNotExclusive: true,
  });

  await connection.execute("PRAGMA journal_mode = WAL");
  await connection.execute("PRAGMA wal_autocheckpoint = 16");

  return connection;
}

/**
 * Tests whether a profile can be deleted safely, i.e. whether it is the only
 * profile in its profile store.
 *
 * @param {nsIToolkitProfile} profile The profile to test
 * @returns {Promise<boolean>} True if the profile can be deleted safely
 */
export async function canDeleteProfile(profile) {
  if (!profile.storeID) {
    // If there is no storeID then there can be no linked profiles.
    return true;
  }

  let dbPath = getSharedProfilesStorePath(profile.storeID);
  if (!(await IOUtils.exists(dbPath))) {
    // If the database doesn't exist then there can be no linked profiles.
    return true;
  }

  try {
    let connection = await openDatastoreConnection(dbPath);

    try {
      let rows = await connection.executeCached(
        'SELECT COUNT(*) AS "count" FROM "Profiles";'
      );

      let profileCount = rows[0]?.getResultByName("count") ?? 0;

      // The database should only contain the profile being deleted.
      return profileCount <= 1;
    } finally {
      await connection.close();
    }
  } catch (e) {
    console.error(e);
    return true;
  }
}

/**
 * Attempts to delete a file but ignores any errors.
 *
 * @param {string} path The path to the file to delete
 */
async function deleteFile(path) {
  try {
    await IOUtils.remove(path);
  } catch (e) {
    // Ignore errors
  }
}

/**
 * Attempts to delete the shared profiles store for the given store ID. No error
 * will be thrown if deletion fails.
 *
 * @param {string} storeID The store ID
 */
export async function deleteSharedProfilesStore(storeID) {
  let dbPath = getSharedProfilesStorePath(storeID);
  await deleteFile(dbPath);
  await deleteFile(dbPath + "-shm");
  await deleteFile(dbPath + "-wal");
}

/**
 * An API that allows access to the cross-profile SQLite database shared
 * between a group of profiles. This database always exists, even if the
 * user does not have the multiple profiles feature enabled.
 */
class ProfilesDatastoreServiceClass {
  #connection = null;
  #asyncShutdownBlocker = null;
  #asyncShutdownBarrier = null;
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
   * Return the AsyncShutdown client for the ProfilesDatastoreService.
   *
   * Consumers can register blockers with this barrier that will block the
   * ProfilesDatastoreService from closing its connection.
   */
  get shutdown() {
    return this.#asyncShutdownBarrier?.client;
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
    if (currentVersion == 8) {
      return;
    }

    if (currentVersion < 1) {
      // Brand new database or created prior to migration support.
      await this.#connection.executeTransaction(async () => {
        const createProfilesTable = `
            CREATE TABLE IF NOT EXISTS "Profiles" (
              id      INTEGER NOT NULL,
              path    TEXT NOT NULL UNIQUE,
              name    TEXT NOT NULL,
              avatar  TEXT NOT NULL,
              themeId TEXT NOT NULL,
              themeFg TEXT NOT NULL,
              themeBg TEXT NOT NULL,
              PRIMARY KEY(id)
            );`;

        await this.#connection.execute(createProfilesTable);

        const createSharedPrefsTable = `
            CREATE TABLE IF NOT EXISTS "SharedPrefs" (
              id        INTEGER NOT NULL,
              name      TEXT NOT NULL UNIQUE,
              value     BLOB,
              isBoolean INTEGER,
              PRIMARY KEY(id)
            );`;

        await this.#connection.execute(createSharedPrefsTable);
      });

      await this.#connection.setSchemaVersion(1);
    }

    if (currentVersion < 2) {
      await this.#connection.executeTransaction(async () => {
        // NB: The profileId field is actually TEXT (a string UUID).
        const createEnrollmentsTable = `
          CREATE TABLE IF NOT EXISTS "NimbusEnrollments" (
            id             INTEGER NOT NULL,
            profileId      INTEGER NOT NULL,
            slug           TEXT NOT NULL,
            branchSlug     TEXT NOT NULL,
            recipe         JSONB,
            active         BOOLEAN NOT NULL,
            unenrollReason TEXT,
            lastSeen       TEXT NOT NULL,
            setPrefs       JSONB,
            prefFlips      JSONB,
            source         TEXT NOT NULL,
            PRIMARY KEY(id),
            UNIQUE (profileId, slug) ON CONFLICT FAIL
          );
        `;

        await this.#connection.execute(createEnrollmentsTable);
      });

      await this.#connection.setSchemaVersion(2);
    }

    if (currentVersion < 3) {
      await this.#connection.executeTransaction(async () => {
        await this.#connection.execute("DELETE FROM NimbusEnrollments;");
      });
      await this.#connection.setSchemaVersion(3);
    }

    if (currentVersion < 4) {
      await this.#connection.executeTransaction(async () => {
        await this.#connection.execute("DELETE FROM NimbusEnrollments;");
      });
      await this.#connection.setSchemaVersion(4);
    }

    if (currentVersion < 5) {
      await this.#connection.executeTransaction(async () => {
        const createHeartbeatTable = `
            CREATE TABLE IF NOT EXISTS "Heartbeats" (
              id              INTEGER NOT NULL,
              recipeId        TEXT NOT NULL UNIQUE,
              lastShown       INTEGER,
              lastInteraction	INTEGER,
              PRIMARY KEY(id)
            );`;

        await this.#connection.execute(createHeartbeatTable);
      });

      await this.#connection.setSchemaVersion(5);
    }

    if (currentVersion < 6) {
      await this.#connection.executeTransaction(async () => {
        const createMessageImpressionsTable = `
          CREATE TABLE IF NOT EXISTS "MessagingSystemMessageImpressions" (
            id                  INTEGER PRIMARY KEY,
            messageId           TEXT UNIQUE NOT NULL,
            impressions         JSONB
          );
        `;

        const createMessageBlocklistTable = `
          CREATE TABLE IF NOT EXISTS "MessagingSystemMessageBlocklist" (
            id                  INTEGER PRIMARY KEY,
            messageId           TEXT UNIQUE NOT NULL
          );
        `;

        await this.#connection.execute(createMessageImpressionsTable);
        await this.#connection.execute(createMessageBlocklistTable);
      });

      await this.#connection.setSchemaVersion(6);
    }

    if (currentVersion < 7) {
      await this.#connection.executeTransaction(async () => {
        const createNimbusSyncTable = `
          CREATE TABLE IF NOT EXISTS "NimbusSyncTimestamps" (
            id                  INTEGER NOT NULL,
            profileId           TEXT NOT NULL,
            collection          TEXT NOT NULL,
            lastModified        INTEGER NOT NULL,
            PRIMARY KEY(id),
            UNIQUE (profileId, collection) ON CONFLICT FAIL
          );
        `;

        await this.#connection.execute(createNimbusSyncTable);
      });

      await this.#connection.setSchemaVersion(7);
    }

    if (currentVersion < 8) {
      await this.#connection.executeTransaction(async () => {
        const createStatusesTable = `
          CREATE TABLE IF NOT EXISTS "NimbusEnrollmentStatuses" (
            id                  INTEGER NOT NULL,
            profileId           TEXT NOT NULL,
            slug                TEXT NOT NULL,
            timestamp           TEXT NOT NULL,
            entryIndex          INTEGER NOT NULL,
            status              JSONB NOT NULL,
            PRIMARY KEY(id),
            UNIQUE (profileId, slug, entryIndex) ON CONFLICT FAIL
          );
        `;

        await this.#connection.execute(createStatusesTable);

        await this.#connection.setSchemaVersion(8);
      });
    }
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
    this.#asyncShutdownBarrier = new lazy.AsyncShutdown.Barrier(
      "ProfilesDatastoreService: waiting for clients to finish pending writes"
    );
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

      await this.#uninit();
      return;
    }

    this.#initialized = true;
  }

  async getStartupMigrationConnection() {
    if (!lazy.MigrationUtils?.isStartupMigration) {
      return null;
    }

    this.#storeID = Services.env.get("SELECTABLE_PROFILE_RESET_STORE_ID");
    await this.#initConnection();
    return this.#connection;
  }

  async uninit() {
    if (this.#initPromise) {
      await this.#initPromise;
    }

    await this.#uninit();
  }

  async #uninit() {
    if (this.#asyncShutdownBarrier) {
      await this.#asyncShutdownBarrier.wait();
    }

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

    this.#asyncShutdownBarrier = new lazy.AsyncShutdown.Barrier(
      "ProfilesDatastoreService: waiting for clients to finish pending writes"
    );

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
    this.#connection = await openDatastoreConnection(path);

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

  maybeCreateStoreID() {
    if (this.#storeID) {
      return;
    }

    const storageID = Services.uuid
      .generateUUID()
      .toString()
      .replace("{", "")
      .split("-")[0];

    this.#storeID = storageID;
    Services.prefs.setStringPref(STOREID_PREF_NAME, storageID);
  }

  async getProfilesStorePath() {
    this.maybeCreateStoreID();

    // If we are not running in a named nsIToolkitProfile, the datastore path
    // should be in the profile directory. This is true in a local build or a
    // CI test build, for example. If isStartupMigration is true, we should use
    // the shared profile store path regardless of if we have a profile or not
    if (
      !lazy.MigrationUtils?.isStartupMigration &&
      !this.#profileService.currentProfile
    ) {
      return PathUtils.join(
        ProfilesDatastoreServiceClass.getDirectory("ProfD").path,
        `${this.#storeID}.sqlite`
      );
    }

    await IOUtils.makeDirectory(
      ProfilesDatastoreServiceClass.PROFILE_GROUPS_DIR
    );

    return getSharedProfilesStorePath(this.#storeID);
  }
}

const ProfilesDatastoreService = new ProfilesDatastoreServiceClass();
export { ProfilesDatastoreService };
