/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
 * vim: sw=2 ts=2 sts=2 et */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const AUTH_TYPE = {
  SCHEME_HTML: 0,
  SCHEME_BASIC: 1,
  SCHEME_DIGEST: 2,
};

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { MigrationUtils } from "resource:///modules/MigrationUtils.sys.mjs";
import { MigratorBase } from "resource:///modules/MigratorBase.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ChromeMigrationUtils: "resource:///modules/ChromeMigrationUtils.sys.mjs",
  FormHistory: "resource://gre/modules/FormHistory.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  Qihoo360seMigrationUtils: "resource:///modules/360seMigrationUtils.sys.mjs",
  MigrationWizardConstants:
    "chrome://browser/content/migration/migration-wizard-constants.mjs",
});

/**
 * Converts an array of chrome bookmark objects into one our own places code
 * understands.
 *
 * @param {object[]} items Chrome Bookmark items to be inserted on this parent
 * @param {set} bookmarkURLAccumulator Accumulate all imported bookmark urls to be used for importing favicons
 * @param {Function} errorAccumulator function that gets called with any errors
 *   thrown so we don't drop them on the floor.
 * @returns {object[]}
 */
function convertBookmarks(items, bookmarkURLAccumulator, errorAccumulator) {
  let itemsToInsert = [];
  for (let item of items) {
    try {
      if (item.type == "url") {
        if (item.url.trim().startsWith("chrome:")) {
          // Skip invalid internal URIs. Creating an actual URI always reports
          // messages to the console because Gecko has its own concept of how
          // chrome:// URIs should be formed, so we avoid doing that.
          continue;
        }
        if (item.url.trim().startsWith("edge:")) {
          // Don't import internal Microsoft Edge URIs as they won't resolve within Firefox.
          continue;
        }
        itemsToInsert.push({ url: item.url, title: item.name });
        bookmarkURLAccumulator.add({ url: item.url });
      } else if (item.type == "folder") {
        let folderItem = {
          type: lazy.PlacesUtils.bookmarks.TYPE_FOLDER,
          title: item.name,
        };
        folderItem.children = convertBookmarks(
          item.children,
          bookmarkURLAccumulator,
          errorAccumulator
        );
        itemsToInsert.push(folderItem);
      }
    } catch (ex) {
      console.error(ex);
      errorAccumulator(ex);
    }
  }
  return itemsToInsert;
}

/**
 * Chrome profile migrator. This can also be used as a parent class for
 * migrators for browsers that are variants of Chrome.
 */
export class ChromeProfileMigrator extends MigratorBase {
  /**
   * On Ubuntu Linux, when the browser is installed as a Snap package,
   * we must request permission to read data from other browsers. We
   * make that request by opening up a native file picker in folder
   * selection mode and instructing the user to navigate to the folder
   * that the other browser's user data resides in.
   *
   * For Snap packages, this gives the browser read access - but it does
   * so through a temporary symlink that does not match the original user
   * data path. Effectively, the user data directory is remapped to a
   * temporary location on the file system. We record these remaps here,
   * keyed on the original data directory.
   *
   * @type {Map<string, string>}
   */
  #dataPathRemappings = new Map();

  static get key() {
    return "chrome";
  }

  static get displayNameL10nID() {
    return "migration-wizard-migrator-display-name-chrome";
  }

  static get brandImage() {
    return "chrome://browser/content/migration/brands/chrome.png";
  }

  get _chromeUserDataPathSuffix() {
    return "Chrome";
  }

  async hasPermissions() {
    let dataPath = await this._getChromeUserDataPathIfExists();
    if (!dataPath) {
      return true;
    }

    let localStatePath = PathUtils.join(dataPath, "Local State");
    try {
      // Read one byte since on snap we can check existence even without being able
      // to read the file.
      await IOUtils.read(localStatePath, { maxBytes: 1 });
      return true;
    } catch (ex) {
      console.error("No permissions for local state folder.");
    }
    return false;
  }

  async getPermissions(win) {
    // Get the original path to the user data and ignore any existing remapping.
    // This allows us to set a new remapping if the user navigates the platforms
    // filepicker to a different directory on a second permission request attempt.
    let originalDataPath = await this._getChromeUserDataPathIfExists(
      true /* noRemapping */
    );
    // Keep prompting the user until they pick something that grants us access
    // to Chrome's local state directory.
    while (!(await this.hasPermissions())) {
      let fp = Cc["@mozilla.org/filepicker;1"].createInstance(Ci.nsIFilePicker);
      fp.init(win?.browsingContext, "", Ci.nsIFilePicker.modeGetFolder);
      fp.filterIndex = 1;
      // Now wait for the filepicker to open and close. If the user picks
      // the local state folder, the OS should grant us read access to everything
      // inside, so we don't need to check or do anything else with what's
      // returned by the filepicker.
      let result = await new Promise(resolve => fp.open(resolve));
      // Bail if the user cancels the dialog:
      if (result == Ci.nsIFilePicker.returnCancel) {
        return false;
      }

      let file = fp.file;
      if (file && file.path != originalDataPath) {
        this.#dataPathRemappings.set(originalDataPath, file.path);
      }
    }
    return true;
  }

  async canGetPermissions() {
    if (
      !Services.prefs.getBoolPref(
        "browser.migrate.chrome.get_permissions.enabled"
      )
    ) {
      return false;
    }

    if (await MigrationUtils.canGetPermissionsOnPlatform()) {
      let dataPath = await this._getChromeUserDataPathIfExists();
      if (dataPath) {
        let localStatePath = PathUtils.join(dataPath, "Local State");
        if (await IOUtils.exists(localStatePath)) {
          return dataPath;
        }
      }
    }
    return false;
  }

  _keychainServiceName = "Chrome Safe Storage";

  _keychainAccountName = "Chrome";

  /**
   * Returns a Promise that resolves to the data path containing the
   * Local State and profile directories for this browser.
   *
   * @param {boolean} [noRemapping=false]
   *   Set to true to bypass any remapping that might have occurred on
   *   platforms where the data path changes once permission has been
   *   granted.
   * @returns {Promise<string>}
   */
  async _getChromeUserDataPathIfExists(noRemapping = false) {
    if (this._chromeUserDataPath) {
      // Skip looking up any remapping if `noRemapping` was passed. This is
      // helpful if the caller needs create a new remapping and overwrite
      // an old remapping, as "real" user data path is used as a key for
      // the remapping.
      if (noRemapping) {
        return this._chromeUserDataPath;
      }

      let remappedPath = this.#dataPathRemappings.get(this._chromeUserDataPath);
      return remappedPath || this._chromeUserDataPath;
    }
    let path = await lazy.ChromeMigrationUtils.getDataPath(
      this._chromeUserDataPathSuffix
    );
    let exists = path && (await IOUtils.exists(path));
    if (exists) {
      this._chromeUserDataPath = path;
    } else {
      this._chromeUserDataPath = null;
    }
    return this._chromeUserDataPath;
  }

  async getResources(aProfile) {
    if (!(await this.hasPermissions())) {
      return [];
    }

    let chromeUserDataPath = await this._getChromeUserDataPathIfExists();
    if (chromeUserDataPath) {
      let profileFolder = chromeUserDataPath;
      if (aProfile) {
        profileFolder = PathUtils.join(chromeUserDataPath, aProfile.id);
      }
      if (await IOUtils.exists(profileFolder)) {
        let possibleResourcePromises = [
          GetBookmarksResource(profileFolder, this.constructor.key),
          GetHistoryResource(profileFolder),
          GetFormdataResource(profileFolder),
          GetExtensionsResource(aProfile.id, this.constructor.key),
        ];
        if (lazy.ChromeMigrationUtils.supportsLoginsForPlatform) {
          possibleResourcePromises.push(
            this._GetPasswordsResource(profileFolder),
            this._GetPaymentMethodsResource(profileFolder)
          );
        }

        // Some of these Promises might reject due to things like database
        // corruptions. We absorb those rejections here and filter them
        // out so that we only try to import the resources that don't appear
        // corrupted.
        let possibleResources = await Promise.allSettled(
          possibleResourcePromises
        );
        return possibleResources
          .filter(promise => {
            return promise.status == "fulfilled" && promise.value !== null;
          })
          .map(promise => promise.value);
      }
    }
    return [];
  }

  async getLastUsedDate() {
    let sourceProfiles = await this.getSourceProfiles();
    if (!sourceProfiles) {
      return new Date(0);
    }
    let chromeUserDataPath = await this._getChromeUserDataPathIfExists();
    if (!chromeUserDataPath) {
      return new Date(0);
    }
    let datePromises = sourceProfiles.map(async profile => {
      let basePath = PathUtils.join(chromeUserDataPath, profile.id);
      let fileDatePromises = ["Bookmarks", "History", "Cookies"].map(
        async leafName => {
          let path = PathUtils.join(basePath, leafName);
          let info = await IOUtils.stat(path).catch(() => null);
          return info ? info.lastModified : 0;
        }
      );
      let dates = await Promise.all(fileDatePromises);
      return Math.max(...dates);
    });
    let datesOuter = await Promise.all(datePromises);
    datesOuter.push(0);
    return new Date(Math.max(...datesOuter));
  }

  async getSourceProfiles() {
    if ("__sourceProfiles" in this) {
      return this.__sourceProfiles;
    }

    let chromeUserDataPath = await this._getChromeUserDataPathIfExists();
    if (!chromeUserDataPath) {
      return [];
    }

    let localState;
    let profiles = [];
    try {
      localState = await lazy.ChromeMigrationUtils.getLocalState(
        this._chromeUserDataPathSuffix,
        chromeUserDataPath
      );
      let info_cache = localState.profile.info_cache;
      for (let profileFolderName in info_cache) {
        profiles.push({
          id: profileFolderName,
          name: info_cache[profileFolderName].name || profileFolderName,
        });
      }
    } catch (e) {
      // Avoid reporting NotFoundErrors from trying to get local state.
      if (localState || e.name != "NotFoundError") {
        console.error("Error detecting Chrome profiles: ", e);
      }

      // If we didn't have permission to read the local state, return the
      // empty array. The user might have the opportunity to request
      // permission using `hasPermission` and `getPermission`.
      if (e.name == "NotAllowedError") {
        return [];
      }

      // If we weren't able to detect any profiles above, fallback to the Default profile.
      let defaultProfilePath = PathUtils.join(chromeUserDataPath, "Default");
      if (await IOUtils.exists(defaultProfilePath)) {
        profiles = [
          {
            id: "Default",
            name: "Default",
          },
        ];
      }
    }

    let profileResources = await Promise.all(
      profiles.map(async profile => ({
        profile,
        resources: await this.getResources(profile),
      }))
    );

    // Only list profiles from which any data can be imported
    this.__sourceProfiles = profileResources
      .filter(({ resources }) => {
        return resources && !!resources.length;
      }, this)
      .map(({ profile }) => profile);
    return this.__sourceProfiles;
  }

  async _GetPasswordsResource(aProfileFolder) {
    let loginPath = PathUtils.join(aProfileFolder, "Login Data");
    if (!(await IOUtils.exists(loginPath))) {
      return null;
    }

    let tempFilePath = null;
    if (MigrationUtils.IS_LINUX_SNAP_PACKAGE) {
      tempFilePath = await IOUtils.createUniqueFile(
        PathUtils.tempDir,
        "Login Data"
      );
      await IOUtils.copy(loginPath, tempFilePath);
      loginPath = tempFilePath;
    }

    let {
      _chromeUserDataPathSuffix,
      _keychainServiceName,
      _keychainAccountName,
      _keychainMockPassphrase = null,
    } = this;

    let countQuery = `SELECT COUNT(*) FROM logins WHERE blacklisted_by_user = 0`;

    let countRows = await MigrationUtils.getRowsFromDBWithoutLocks(
      loginPath,
      "Chrome passwords",
      countQuery
    );

    if (!countRows[0].getResultByName("COUNT(*)")) {
      return null;
    }

    return {
      type: MigrationUtils.resourceTypes.PASSWORDS,

      async migrate(aCallback) {
        let rows = await MigrationUtils.getRowsFromDBWithoutLocks(
          loginPath,
          "Chrome passwords",
          `SELECT origin_url, action_url, username_element, username_value,
          password_element, password_value, signon_realm, scheme, date_created,
          times_used FROM logins WHERE blacklisted_by_user = 0`
        )
          .catch(ex => {
            console.error(ex);
            aCallback(false);
          })
          .finally(() => {
            return tempFilePath && IOUtils.remove(tempFilePath);
          });

        // If the promise was rejected we will have already called aCallback,
        // so we can just return here.
        if (!rows) {
          return;
        }

        // If there are no relevant rows, return before initializing crypto and
        // thus prompting for Keychain access on macOS.
        if (!rows.length) {
          aCallback(true);
          return;
        }

        let loginCrypto;
        try {
          if (AppConstants.platform == "win") {
            let { ChromeWindowsLoginCrypto } = ChromeUtils.importESModule(
              "resource:///modules/ChromeWindowsLoginCrypto.sys.mjs"
            );
            loginCrypto = new ChromeWindowsLoginCrypto(
              _chromeUserDataPathSuffix
            );
          } else if (AppConstants.platform == "macosx") {
            let { ChromeMacOSLoginCrypto } = ChromeUtils.importESModule(
              "resource:///modules/ChromeMacOSLoginCrypto.sys.mjs"
            );
            loginCrypto = new ChromeMacOSLoginCrypto(
              _keychainServiceName,
              _keychainAccountName,
              _keychainMockPassphrase
            );
          } else {
            aCallback(false);
            return;
          }
        } catch (ex) {
          // Handle the user canceling Keychain access or other OSCrypto errors.
          console.error(ex);
          aCallback(false);
          return;
        }

        let logins = [];
        let fallbackCreationDate = new Date();
        const kValidSchemes = new Set(["https", "http", "ftp"]);
        for (let row of rows) {
          try {
            let origin_url = lazy.NetUtil.newURI(
              row.getResultByName("origin_url")
            );
            // Ignore entries for non-http(s)/ftp URLs because we likely can't
            // use them anyway.
            if (!kValidSchemes.has(origin_url.scheme)) {
              continue;
            }
            let loginInfo = {
              username: row.getResultByName("username_value"),
              password: await loginCrypto.decryptData(
                row.getResultByName("password_value"),
                null
              ),
              origin: origin_url.prePath,
              formActionOrigin: null,
              httpRealm: null,
              usernameElement: row.getResultByName("username_element"),
              passwordElement: row.getResultByName("password_element"),
              timeCreated: lazy.ChromeMigrationUtils.chromeTimeToDate(
                row.getResultByName("date_created") + 0,
                fallbackCreationDate
              ).getTime(),
              timesUsed: row.getResultByName("times_used") + 0,
            };

            switch (row.getResultByName("scheme")) {
              case AUTH_TYPE.SCHEME_HTML:
                let action_url = row.getResultByName("action_url");
                if (!action_url) {
                  // If there is no action_url, store the wildcard "" value.
                  // See the `formActionOrigin` IDL comments.
                  loginInfo.formActionOrigin = "";
                  break;
                }
                let action_uri = lazy.NetUtil.newURI(action_url);
                if (!kValidSchemes.has(action_uri.scheme)) {
                  continue; // This continues the outer for loop.
                }
                loginInfo.formActionOrigin = action_uri.prePath;
                break;
              case AUTH_TYPE.SCHEME_BASIC:
              case AUTH_TYPE.SCHEME_DIGEST:
                // signon_realm format is URIrealm, so we need remove URI
                loginInfo.httpRealm = row
                  .getResultByName("signon_realm")
                  .substring(loginInfo.origin.length + 1);
                break;
              default:
                throw new Error(
                  "Login data scheme type not supported: " +
                    row.getResultByName("scheme")
                );
            }
            logins.push(loginInfo);
          } catch (e) {
            console.error(e);
          }
        }
        try {
          if (logins.length) {
            await MigrationUtils.insertLoginsWrapper(logins);
          }
        } catch (e) {
          console.error(e);
        }
        if (crypto.finalize) {
          crypto.finalize();
        }
        aCallback(true);
      },
    };
  }
  async _GetPaymentMethodsResource(aProfileFolder) {
    if (
      !Services.prefs.getBoolPref(
        "browser.migrate.chrome.payment_methods.enabled",
        false
      )
    ) {
      return null;
    }

    let paymentMethodsPath = PathUtils.join(aProfileFolder, "Web Data");

    if (!(await IOUtils.exists(paymentMethodsPath))) {
      return null;
    }

    let tempFilePath = null;
    if (MigrationUtils.IS_LINUX_SNAP_PACKAGE) {
      tempFilePath = await IOUtils.createUniqueFile(
        PathUtils.tempDir,
        "Web Data"
      );
      await IOUtils.copy(paymentMethodsPath, tempFilePath);
      paymentMethodsPath = tempFilePath;
    }

    let rows = await MigrationUtils.getRowsFromDBWithoutLocks(
      paymentMethodsPath,
      "Chrome Credit Cards",
      "SELECT name_on_card, card_number_encrypted, expiration_month, expiration_year FROM credit_cards"
    )
      .catch(ex => {
        console.error(ex);
      })
      .finally(() => {
        return tempFilePath && IOUtils.remove(tempFilePath);
      });

    if (!rows?.length) {
      return null;
    }

    let {
      _chromeUserDataPathSuffix,
      _keychainServiceName,
      _keychainAccountName,
      _keychainMockPassphrase = null,
    } = this;

    return {
      type: MigrationUtils.resourceTypes.PAYMENT_METHODS,

      async migrate(aCallback) {
        let loginCrypto;
        try {
          if (AppConstants.platform == "win") {
            let { ChromeWindowsLoginCrypto } = ChromeUtils.importESModule(
              "resource:///modules/ChromeWindowsLoginCrypto.sys.mjs"
            );
            loginCrypto = new ChromeWindowsLoginCrypto(
              _chromeUserDataPathSuffix
            );
          } else if (AppConstants.platform == "macosx") {
            let { ChromeMacOSLoginCrypto } = ChromeUtils.importESModule(
              "resource:///modules/ChromeMacOSLoginCrypto.sys.mjs"
            );
            loginCrypto = new ChromeMacOSLoginCrypto(
              _keychainServiceName,
              _keychainAccountName,
              _keychainMockPassphrase
            );
          } else {
            aCallback(false);
            return;
          }
        } catch (ex) {
          // Handle the user canceling Keychain access or other OSCrypto errors.
          console.error(ex);
          aCallback(false);
          return;
        }

        let cards = [];
        for (let row of rows) {
          try {
            cards.push({
              "cc-name": row.getResultByName("name_on_card"),
              "cc-number": await loginCrypto.decryptData(
                row.getResultByName("card_number_encrypted"),
                null
              ),
              "cc-exp-month": parseInt(
                row.getResultByName("expiration_month"),
                10
              ),
              "cc-exp-year": parseInt(
                row.getResultByName("expiration_year"),
                10
              ),
            });
          } catch (e) {
            console.error(e);
          }
        }

        await MigrationUtils.insertCreditCardsWrapper(cards);
        aCallback(true);
      },
    };
  }
}

async function GetBookmarksResource(aProfileFolder, aBrowserKey) {
  let bookmarksPath = PathUtils.join(aProfileFolder, "Bookmarks");
  let faviconsPath = PathUtils.join(aProfileFolder, "Favicons");

  if (aBrowserKey === "chromium-360se") {
    let localState = {};
    try {
      localState = await lazy.ChromeMigrationUtils.getLocalState("360 SE");
    } catch (ex) {
      console.error(ex);
    }

    let alternativeBookmarks =
      await lazy.Qihoo360seMigrationUtils.getAlternativeBookmarks({
        bookmarksPath,
        localState,
      });
    if (alternativeBookmarks.resource) {
      return alternativeBookmarks.resource;
    }

    bookmarksPath = alternativeBookmarks.path;
  }

  if (!(await IOUtils.exists(bookmarksPath))) {
    return null;
  }

  let tempFilePath = null;
  if (MigrationUtils.IS_LINUX_SNAP_PACKAGE) {
    tempFilePath = await IOUtils.createUniqueFile(
      PathUtils.tempDir,
      "Favicons"
    );
    await IOUtils.copy(faviconsPath, tempFilePath);
    faviconsPath = tempFilePath;
  }

  // check to read JSON bookmarks structure and see if any bookmarks exist else return null
  // Parse Chrome bookmark file that is JSON format
  let bookmarkJSON = await IOUtils.readJSON(bookmarksPath);
  let other = bookmarkJSON.roots.other.children.length;
  let bookmarkBar = bookmarkJSON.roots.bookmark_bar.children.length;
  let synced = bookmarkJSON.roots.synced.children.length;

  if (!other && !bookmarkBar && !synced) {
    return null;
  }
  return {
    type: MigrationUtils.resourceTypes.BOOKMARKS,

    migrate(aCallback) {
      return (async function () {
        let gotErrors = false;
        let errorGatherer = function () {
          gotErrors = true;
        };

        let faviconRows = [];
        try {
          faviconRows = await MigrationUtils.getRowsFromDBWithoutLocks(
            faviconsPath,
            "Chrome Bookmark Favicons",
            `select fav.id, fav.url, map.page_url, bit.image_data FROM favicons as fav
              INNER JOIN favicon_bitmaps bit ON (fav.id = bit.icon_id)
              INNER JOIN icon_mapping map ON (map.icon_id = bit.icon_id)`
          );
        } catch (ex) {
          console.error(ex);
        } finally {
          if (tempFilePath) {
            await IOUtils.remove(tempFilePath);
          }
        }

        // Create Hashmap for favicons
        let faviconMap = new Map();
        for (let faviconRow of faviconRows) {
          // First, try to normalize the URI:
          try {
            let uri = lazy.NetUtil.newURI(
              faviconRow.getResultByName("page_url")
            );
            faviconMap.set(uri.spec, {
              faviconData: faviconRow.getResultByName("image_data"),
              uri,
            });
          } catch (e) {
            // Couldn't parse the URI, so just skip it.
            continue;
          }
        }

        let roots = bookmarkJSON.roots;
        let bookmarkURLAccumulator = new Set();

        // Importing bookmark bar items
        if (roots.bookmark_bar.children && roots.bookmark_bar.children.length) {
          // Toolbar
          let parentGuid = lazy.PlacesUtils.bookmarks.toolbarGuid;
          let bookmarks = convertBookmarks(
            roots.bookmark_bar.children,
            bookmarkURLAccumulator,
            errorGatherer
          );
          await MigrationUtils.insertManyBookmarksWrapper(
            bookmarks,
            parentGuid
          );
        }

        // Importing Other Bookmarks items
        if (roots.other.children && roots.other.children.length) {
          // Other Bookmarks
          let parentGuid = lazy.PlacesUtils.bookmarks.unfiledGuid;
          let bookmarks = convertBookmarks(
            roots.other.children,
            bookmarkURLAccumulator,
            errorGatherer
          );
          await MigrationUtils.insertManyBookmarksWrapper(
            bookmarks,
            parentGuid
          );
        }

        // Importing synced Bookmarks items
        if (roots.synced.children && roots.synced.children.length) {
          // Synced  Bookmarks
          let parentGuid = lazy.PlacesUtils.bookmarks.unfiledGuid;
          let bookmarks = convertBookmarks(
            roots.synced.children,
            bookmarkURLAccumulator,
            errorGatherer
          );
          await MigrationUtils.insertManyBookmarksWrapper(
            bookmarks,
            parentGuid
          );
        }

        // Find all favicons with associated bookmarks
        let favicons = [];
        for (let bookmark of bookmarkURLAccumulator) {
          try {
            let uri = lazy.NetUtil.newURI(bookmark.url);
            let favicon = faviconMap.get(uri.spec);
            if (favicon) {
              favicons.push(favicon);
            }
          } catch (e) {
            // Couldn't parse the bookmark URI, so just skip
            continue;
          }
        }

        // Import Bookmark Favicons
        MigrationUtils.insertManyFavicons(favicons).catch(console.error);

        if (gotErrors) {
          throw new Error("The migration included errors.");
        }
      })().then(
        () => aCallback(true),
        () => aCallback(false)
      );
    },
  };
}

async function GetHistoryResource(aProfileFolder) {
  let historyPath = PathUtils.join(aProfileFolder, "History");
  if (!(await IOUtils.exists(historyPath))) {
    return null;
  }

  let tempFilePath = null;
  if (MigrationUtils.IS_LINUX_SNAP_PACKAGE) {
    tempFilePath = await IOUtils.createUniqueFile(PathUtils.tempDir, "History");
    await IOUtils.copy(historyPath, tempFilePath);
    historyPath = tempFilePath;
  }

  let countQuery = "SELECT COUNT(*) FROM urls WHERE hidden = 0";

  let countRows = await MigrationUtils.getRowsFromDBWithoutLocks(
    historyPath,
    "Chrome history",
    countQuery
  );
  if (!countRows[0].getResultByName("COUNT(*)")) {
    return null;
  }
  return {
    type: MigrationUtils.resourceTypes.HISTORY,

    migrate(aCallback) {
      (async function () {
        const LIMIT = Services.prefs.getIntPref(
          "browser.migrate.chrome.history.limit"
        );

        let query =
          "SELECT url, title, last_visit_time, typed_count FROM urls WHERE hidden = 0";
        let maxAge = lazy.ChromeMigrationUtils.dateToChromeTime(
          Date.now() - MigrationUtils.HISTORY_MAX_AGE_IN_MILLISECONDS
        );
        query += " AND last_visit_time > " + maxAge;

        if (LIMIT) {
          query += " ORDER BY last_visit_time DESC LIMIT " + LIMIT;
        }

        let rows;
        try {
          rows = await MigrationUtils.getRowsFromDBWithoutLocks(
            historyPath,
            "Chrome history",
            query
          );
        } finally {
          if (tempFilePath) {
            await IOUtils.remove(tempFilePath);
          }
        }

        let pageInfos = [];
        let fallbackVisitDate = new Date();
        for (let row of rows) {
          try {
            // if having typed_count, we changes transition type to typed.
            let transition = lazy.PlacesUtils.history.TRANSITIONS.LINK;
            if (row.getResultByName("typed_count") > 0) {
              transition = lazy.PlacesUtils.history.TRANSITIONS.TYPED;
            }

            pageInfos.push({
              title: row.getResultByName("title"),
              url: new URL(row.getResultByName("url")),
              visits: [
                {
                  transition,
                  date: lazy.ChromeMigrationUtils.chromeTimeToDate(
                    row.getResultByName("last_visit_time"),
                    fallbackVisitDate
                  ),
                },
              ],
            });
          } catch (e) {
            console.error(e);
          }
        }

        if (pageInfos.length) {
          await MigrationUtils.insertVisitsWrapper(pageInfos);
        }
      })().then(
        () => {
          aCallback(true);
        },
        ex => {
          console.error(ex);
          aCallback(false);
        }
      );
    },
  };
}

async function GetFormdataResource(aProfileFolder) {
  let formdataPath = PathUtils.join(aProfileFolder, "Web Data");
  if (!(await IOUtils.exists(formdataPath))) {
    return null;
  }
  let countQuery = "SELECT COUNT(*) FROM autofill";

  let tempFilePath = null;
  if (MigrationUtils.IS_LINUX_SNAP_PACKAGE) {
    tempFilePath = await IOUtils.createUniqueFile(
      PathUtils.tempDir,
      "Web Data"
    );
    await IOUtils.copy(formdataPath, tempFilePath);
    formdataPath = tempFilePath;
  }

  let countRows = await MigrationUtils.getRowsFromDBWithoutLocks(
    formdataPath,
    "Chrome formdata",
    countQuery
  );
  if (!countRows[0].getResultByName("COUNT(*)")) {
    return null;
  }
  return {
    type: MigrationUtils.resourceTypes.FORMDATA,

    async migrate(aCallback) {
      let query =
        "SELECT name, value, count, date_created, date_last_used FROM autofill";
      let rows;

      try {
        rows = await MigrationUtils.getRowsFromDBWithoutLocks(
          formdataPath,
          "Chrome formdata",
          query
        );
      } finally {
        if (tempFilePath) {
          await IOUtils.remove(tempFilePath);
        }
      }

      let addOps = [];
      for (let row of rows) {
        try {
          let fieldname = row.getResultByName("name");
          let value = row.getResultByName("value");
          if (fieldname && value) {
            addOps.push({
              op: "add",
              fieldname,
              value,
              timesUsed: row.getResultByName("count"),
              firstUsed: row.getResultByName("date_created") * 1000,
              lastUsed: row.getResultByName("date_last_used") * 1000,
            });
          }
        } catch (e) {
          console.error(e);
        }
      }

      try {
        await lazy.FormHistory.update(addOps);
      } catch (e) {
        console.error(e);
        aCallback(false);
        return;
      }

      aCallback(true);
    },
  };
}

async function GetExtensionsResource(aProfileId, aBrowserKey = "chrome") {
  if (
    !Services.prefs.getBoolPref(
      "browser.migrate.chrome.extensions.enabled",
      false
    )
  ) {
    return null;
  }
  let extensions = await lazy.ChromeMigrationUtils.getExtensionList(aProfileId);
  if (!extensions.length || aBrowserKey !== "chrome") {
    return null;
  }

  return {
    type: MigrationUtils.resourceTypes.EXTENSIONS,
    async migrate(callback) {
      let ids = extensions.map(extension => extension.id);
      let [progressValue, importedExtensions] =
        await MigrationUtils.installExtensionsWrapper(aBrowserKey, ids);
      let details = {
        progressValue,
        totalExtensions: extensions,
        importedExtensions,
      };
      if (
        progressValue == lazy.MigrationWizardConstants.PROGRESS_VALUE.INFO ||
        progressValue == lazy.MigrationWizardConstants.PROGRESS_VALUE.SUCCESS
      ) {
        callback(true, details);
      } else {
        callback(false);
      }
    },
  };
}

/**
 * Chromium migrator
 */
export class ChromiumProfileMigrator extends ChromeProfileMigrator {
  static get key() {
    return "chromium";
  }

  static get displayNameL10nID() {
    return "migration-wizard-migrator-display-name-chromium";
  }

  static get brandImage() {
    return "chrome://browser/content/migration/brands/chromium.png";
  }

  _chromeUserDataPathSuffix = "Chromium";
  _keychainServiceName = "Chromium Safe Storage";
  _keychainAccountName = "Chromium";
}

/**
 * Chrome Canary
 * Not available on Linux
 */
export class CanaryProfileMigrator extends ChromeProfileMigrator {
  static get key() {
    return "canary";
  }

  static get displayNameL10nID() {
    return "migration-wizard-migrator-display-name-canary";
  }

  static get brandImage() {
    return "chrome://browser/content/migration/brands/canary.png";
  }

  get _chromeUserDataPathSuffix() {
    return "Canary";
  }

  get _keychainServiceName() {
    return "Chromium Safe Storage";
  }

  get _keychainAccountName() {
    return "Chromium";
  }
}

/**
 * Chrome Dev - Linux only (not available in Mac and Windows)
 */
export class ChromeDevMigrator extends ChromeProfileMigrator {
  static get key() {
    return "chrome-dev";
  }

  static get displayNameL10nID() {
    return "migration-wizard-migrator-display-name-chrome-dev";
  }

  _chromeUserDataPathSuffix = "Chrome Dev";
  _keychainServiceName = "Chromium Safe Storage";
  _keychainAccountName = "Chromium";
}

/**
 * Chrome Beta migrator
 */
export class ChromeBetaMigrator extends ChromeProfileMigrator {
  static get key() {
    return "chrome-beta";
  }

  static get displayNameL10nID() {
    return "migration-wizard-migrator-display-name-chrome-beta";
  }

  _chromeUserDataPathSuffix = "Chrome Beta";
  _keychainServiceName = "Chromium Safe Storage";
  _keychainAccountName = "Chromium";
}

/**
 * Brave migrator
 */
export class BraveProfileMigrator extends ChromeProfileMigrator {
  static get key() {
    return "brave";
  }

  static get displayNameL10nID() {
    return "migration-wizard-migrator-display-name-brave";
  }

  static get brandImage() {
    return "chrome://browser/content/migration/brands/brave.png";
  }

  _chromeUserDataPathSuffix = "Brave";
  _keychainServiceName = "Brave Browser Safe Storage";
  _keychainAccountName = "Brave Browser";
}

/**
 * Edge (Chromium-based) migrator
 */
export class ChromiumEdgeMigrator extends ChromeProfileMigrator {
  static get key() {
    return "chromium-edge";
  }

  static get displayNameL10nID() {
    return "migration-wizard-migrator-display-name-chromium-edge";
  }

  static get brandImage() {
    return "chrome://browser/content/migration/brands/edge.png";
  }

  _chromeUserDataPathSuffix = "Edge";
  _keychainServiceName = "Microsoft Edge Safe Storage";
  _keychainAccountName = "Microsoft Edge";
}

/**
 * Edge Beta (Chromium-based) migrator
 */
export class ChromiumEdgeBetaMigrator extends ChromeProfileMigrator {
  static get key() {
    return "chromium-edge-beta";
  }

  static get displayNameL10nID() {
    return "migration-wizard-migrator-display-name-chromium-edge-beta";
  }

  static get brandImage() {
    return "chrome://browser/content/migration/brands/edgebeta.png";
  }

  _chromeUserDataPathSuffix = "Edge Beta";
  _keychainServiceName = "Microsoft Edge Safe Storage";
  _keychainAccountName = "Microsoft Edge";
}

/**
 * Chromium 360 migrator
 */
export class Chromium360seMigrator extends ChromeProfileMigrator {
  static get key() {
    return "chromium-360se";
  }

  static get displayNameL10nID() {
    return "migration-wizard-migrator-display-name-chromium-360se";
  }

  static get brandImage() {
    return "chrome://browser/content/migration/brands/360.png";
  }

  _chromeUserDataPathSuffix = "360 SE";
  _keychainServiceName = "Microsoft Edge Safe Storage";
  _keychainAccountName = "Microsoft Edge";
}

/**
 * Opera migrator
 */
export class OperaProfileMigrator extends ChromeProfileMigrator {
  static get key() {
    return "opera";
  }

  static get displayNameL10nID() {
    return "migration-wizard-migrator-display-name-opera";
  }

  static get brandImage() {
    return "chrome://browser/content/migration/brands/opera.png";
  }

  _chromeUserDataPathSuffix = "Opera";
  _keychainServiceName = "Opera Safe Storage";
  _keychainAccountName = "Opera";

  getSourceProfiles() {
    return null;
  }
}

/**
 * Opera GX migrator
 */
export class OperaGXProfileMigrator extends ChromeProfileMigrator {
  static get key() {
    return "opera-gx";
  }

  static get displayNameL10nID() {
    return "migration-wizard-migrator-display-name-opera-gx";
  }

  static get brandImage() {
    return "chrome://browser/content/migration/brands/operagx.png";
  }

  _chromeUserDataPathSuffix = "Opera GX";
  _keychainServiceName = "Opera Safe Storage";
  _keychainAccountName = "Opera";

  getSourceProfiles() {
    return null;
  }
}

/**
 * Vivaldi migrator
 */
export class VivaldiProfileMigrator extends ChromeProfileMigrator {
  static get key() {
    return "vivaldi";
  }

  static get displayNameL10nID() {
    return "migration-wizard-migrator-display-name-vivaldi";
  }

  static get brandImage() {
    return "chrome://browser/content/migration/brands/vivaldi.png";
  }

  _chromeUserDataPathSuffix = "Vivaldi";
  _keychainServiceName = "Vivaldi Safe Storage";
  _keychainAccountName = "Vivaldi";
}
