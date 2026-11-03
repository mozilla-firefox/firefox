/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
  MigrationUtils:
    "moz-src:///browser/components/migration/MigrationUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
});

const S100NS_FROM1601TO1970 = 0x19db1ded53e8000;
const S100NS_PER_MS = 10;
const CHROME_PAGE_TRANSITION_CORE_MASK = 0xff;

export var ChromeMigrationUtils = {
  // Supported browsers with importable logins.
  CONTEXTUAL_LOGIN_IMPORT_BROWSERS: ["chrome", "chromium-edge", "chromium"],

  _extensionVersionDirectoryNames: {},

  // The cache for the locale strings.
  // For example, the data could be:
  // {
  //   "profile-id-1": {
  //     "extension-id-1": {
  //       "name": {
  //         "message": "Fake App 1"
  //       }
  //   },
  // }
  _extensionLocaleStrings: {},

  get supportsLoginsForPlatform() {
    return ["macosx", "win"].includes(AppConstants.platform);
  },

  /**
   * Get all extensions installed in a specific profile.
   *
   * @param {string} profileId - A Chrome user profile ID. For example, "Profile 1".
   * @returns {Array} All installed Chrome extensions information.
   */
  async getExtensionList(profileId) {
    if (profileId === undefined) {
      profileId = await this.getLastUsedProfileId();
    }
    let path = await this.getExtensionPath(profileId);
    let extensionList = [];
    try {
      for (const child of await IOUtils.getChildren(path)) {
        const info = await IOUtils.stat(child);
        if (info.type === "directory") {
          const name = PathUtils.filename(child);
          let extensionInformation = await this.getExtensionInformation(
            name,
            profileId
          );
          if (extensionInformation) {
            extensionList.push(extensionInformation);
          }
        }
      }
    } catch (ex) {
      console.error(ex);
    }
    return extensionList;
  },

  /**
   * Get information of a specific Chrome extension.
   *
   * @param {string} extensionId - The extension ID.
   * @param {string} profileId - The user profile's ID.
   * @returns {object} The Chrome extension information.
   */
  async getExtensionInformation(extensionId, profileId) {
    if (profileId === undefined) {
      profileId = await this.getLastUsedProfileId();
    }
    let extensionInformation = null;
    try {
      let manifestPath = await this.getExtensionPath(profileId);
      manifestPath = PathUtils.join(manifestPath, extensionId);
      // If there are multiple sub-directories in the extension directory,
      // read the files in the latest directory.
      let directories =
        await this._getSortedByVersionSubDirectoryNames(manifestPath);
      if (!directories[0]) {
        return null;
      }

      manifestPath = PathUtils.join(
        manifestPath,
        directories[0],
        "manifest.json"
      );
      let manifest = await IOUtils.readJSON(manifestPath);
      // No app attribute means this is a Chrome extension not a Chrome app.
      if (!manifest.app) {
        const DEFAULT_LOCALE = manifest.default_locale;
        let name = await this._getLocaleString(
          manifest.name,
          DEFAULT_LOCALE,
          extensionId,
          profileId
        );
        let description = await this._getLocaleString(
          manifest.description,
          DEFAULT_LOCALE,
          extensionId,
          profileId
        );
        if (name) {
          extensionInformation = {
            id: extensionId,
            name,
            description,
          };
        } else {
          throw new Error("Cannot read the Chrome extension's name property.");
        }
      }
    } catch (ex) {
      console.error(ex);
    }
    return extensionInformation;
  },

  /**
   * Get the manifest's locale string.
   *
   * @param {string} key - The key of a locale string, for example __MSG_name__.
   * @param {string} locale - The specific language of locale string.
   * @param {string} extensionId - The extension ID.
   * @param {string} profileId - The user profile's ID.
   * @returns {string|null} The locale string.
   */
  async _getLocaleString(key, locale, extensionId, profileId) {
    if (typeof key !== "string") {
      console.debug("invalid manifest key");
      return null;
    }
    // Return the key string if it is not a locale key.
    // The key string starts with "__MSG_" and ends with "__".
    // For example, "__MSG_name__".
    // https://developer.chrome.com/apps/i18n
    if (!key.startsWith("__MSG_") || !key.endsWith("__")) {
      return key;
    }

    let localeString = null;
    try {
      let localeFile;
      if (
        this._extensionLocaleStrings[profileId] &&
        this._extensionLocaleStrings[profileId][extensionId]
      ) {
        localeFile = this._extensionLocaleStrings[profileId][extensionId];
      } else {
        if (!this._extensionLocaleStrings[profileId]) {
          this._extensionLocaleStrings[profileId] = {};
        }
        let localeFilePath = await this.getExtensionPath(profileId);
        localeFilePath = PathUtils.join(localeFilePath, extensionId);
        let directories =
          await this._getSortedByVersionSubDirectoryNames(localeFilePath);
        // If there are multiple sub-directories in the extension directory,
        // read the files in the latest directory.
        localeFilePath = PathUtils.join(
          localeFilePath,
          directories[0],
          "_locales",
          locale,
          "messages.json"
        );
        localeFile = await IOUtils.readJSON(localeFilePath);
        this._extensionLocaleStrings[profileId][extensionId] = localeFile;
      }
      const PREFIX_LENGTH = 6;
      const SUFFIX_LENGTH = 2;
      // Get the locale key from the string with locale prefix and suffix.
      // For example, it will get the "name" sub-string from the "__MSG_name__" string.
      key = key.substring(PREFIX_LENGTH, key.length - SUFFIX_LENGTH);
      if (localeFile[key] && localeFile[key].message) {
        localeString = localeFile[key].message;
      }
    } catch (ex) {
      console.error(ex);
    }
    return localeString;
  },

  /**
   * Check that a specific extension is installed or not.
   *
   * @param {string} extensionId - The extension ID.
   * @param {string} profileId - The user profile's ID.
   * @returns {boolean} Return true if the extension is installed otherwise return false.
   */
  async isExtensionInstalled(extensionId, profileId) {
    if (profileId === undefined) {
      profileId = await this.getLastUsedProfileId();
    }
    let extensionPath = await this.getExtensionPath(profileId);
    let isInstalled = await IOUtils.exists(
      PathUtils.join(extensionPath, extensionId)
    );
    return isInstalled;
  },

  /**
   * Get the last used user profile's ID.
   *
   * @returns {string} The last used user profile's ID.
   */
  async getLastUsedProfileId() {
    let localState = await this.getLocalState();
    return localState ? localState.profile.last_used : "Default";
  },

  /**
   * Get the local state file content.
   *
   * @param {string} chromeProjectName
   *   The type of Chrome data we're looking for (Chromium, Canary, etc.)
   * @param {string} [dataPath=undefined]
   *   The data path that should be used as the parent directory when getting
   *   the local state. If not supplied, the data path is calculated using
   *   getDataPath and the chromeProjectName.
   * @returns {object} The JSON-based content.
   */
  async getLocalState(chromeProjectName = "Chrome", dataPath) {
    let localState = null;
    try {
      if (!dataPath) {
        dataPath = await this.getDataPath(chromeProjectName);
      }
      let localStatePath = PathUtils.join(dataPath, "Local State");
      localState = JSON.parse(await IOUtils.readUTF8(localStatePath));
    } catch (ex) {
      // Don't report the error if it's just a file not existing.
      if (ex.name != "NotFoundError") {
        console.error(ex);
      }
      throw ex;
    }
    return localState;
  },

  /**
   * Get the path of Chrome extension directory.
   *
   * @param {string} profileId - The user profile's ID.
   * @returns {string} The path of Chrome extension directory.
   */
  async getExtensionPath(profileId) {
    return PathUtils.join(await this.getDataPath(), profileId, "Extensions");
  },

  /**
   * Get the path of an application data directory.
   *
   * @param {string} chromeProjectName - The Chrome project name, e.g. "Chrome", "Canary", etc.
   *                                     Defaults to "Chrome".
   * @returns {string} The path of application data directory.
   */
  async getDataPath(chromeProjectName = "Chrome") {
    const SNAP_REAL_HOME = "SNAP_REAL_HOME";

    const SUB_DIRECTORIES = {
      win: {
        Brave: [
          ["LocalAppData", "BraveSoftware", "Brave-Browser", "User Data"],
        ],
        Chrome: [["LocalAppData", "Google", "Chrome", "User Data"]],
        "Chrome Beta": [["LocalAppData", "Google", "Chrome Beta", "User Data"]],
        Chromium: [["LocalAppData", "Chromium", "User Data"]],
        Canary: [["LocalAppData", "Google", "Chrome SxS", "User Data"]],
        Edge: [["LocalAppData", "Microsoft", "Edge", "User Data"]],
        "Edge Beta": [["LocalAppData", "Microsoft", "Edge Beta", "User Data"]],
        "360 SE": [["AppData", "360se6", "User Data"]],
        Opera: [["AppData", "Opera Software", "Opera Stable"]],
        "Opera GX": [["AppData", "Opera Software", "Opera GX Stable"]],
        Vivaldi: [["LocalAppData", "Vivaldi", "User Data"]],
      },
      macosx: {
        Brave: [
          ["ULibDir", "Application Support", "BraveSoftware", "Brave-Browser"],
        ],
        Chrome: [["ULibDir", "Application Support", "Google", "Chrome"]],
        Chromium: [["ULibDir", "Application Support", "Chromium"]],
        Canary: [["ULibDir", "Application Support", "Google", "Chrome Canary"]],
        Edge: [["ULibDir", "Application Support", "Microsoft Edge"]],
        "Edge Beta": [
          ["ULibDir", "Application Support", "Microsoft Edge Beta"],
        ],
        "Opera GX": [
          ["ULibDir", "Application Support", "com.operasoftware.OperaGX"],
        ],
        Opera: [["ULibDir", "Application Support", "com.operasoftware.Opera"]],
        Vivaldi: [["ULibDir", "Application Support", "Vivaldi"]],
      },
      linux: {
        Brave: [["Home", ".config", "BraveSoftware", "Brave-Browser"]],
        Chrome: [["Home", ".config", "google-chrome"]],
        "Chrome Beta": [["Home", ".config", "google-chrome-beta"]],
        "Chrome Dev": [["Home", ".config", "google-chrome-unstable"]],
        Chromium: [
          ["Home", ".config", "chromium"],

          // If we're installed normally, we can look for Chromium installed
          // as a Snap on Ubuntu Linux by looking here.
          ["Home", "snap", "chromium", "common", "chromium"],

          // If we're installed as a Snap, "Home" is a special place that
          // the Snap environment has given us, and the Chromium data is
          // not within it. We want to, instead, start at the path set
          // on the environment variable "SNAP_REAL_HOME".
          // See: https://snapcraft.io/docs/environment-variables#heading--snap-real-home
          [SNAP_REAL_HOME, "snap", "chromium", "common", "chromium"],
        ],
        // Opera GX is not available on Linux.
        // Canary is not available on Linux.

        Edge: [["Home", ".config", "microsoft-edge"]],
        Opera: [["Home", ".config", "opera"]],
        Vivaldi: [["Home", ".config", "vivaldi"]],
      },
    };
    let options = SUB_DIRECTORIES[AppConstants.platform][chromeProjectName];
    if (!options) {
      return null;
    }

    for (let subfolders of options) {
      let rootDir = subfolders[0];
      try {
        let targetPath;

        if (rootDir == SNAP_REAL_HOME) {
          targetPath = Services.env.get("SNAP_REAL_HOME");
        } else {
          targetPath = Services.dirsvc.get(rootDir, Ci.nsIFile).path;
        }

        targetPath = PathUtils.join(targetPath, ...subfolders.slice(1));
        if (await IOUtils.exists(targetPath)) {
          return targetPath;
        }
      } catch (ex) {
        // The path logic here shouldn't error, so log it:
        console.error(ex);
      }
    }
    return null;
  },

  /**
   * Get the directory objects sorted by version number.
   *
   * @param {string} path - The path to the extension directory.
   * otherwise return all file/directory object.
   * @returns {Array} The file/directory object array.
   */
  async _getSortedByVersionSubDirectoryNames(path) {
    if (this._extensionVersionDirectoryNames[path]) {
      return this._extensionVersionDirectoryNames[path];
    }

    let entries = [];
    try {
      for (const child of await IOUtils.getChildren(path)) {
        const info = await IOUtils.stat(child);
        if (info.type === "directory") {
          const name = PathUtils.filename(child);
          entries.push(name);
        }
      }
    } catch (ex) {
      console.error(ex);
      entries = [];
    }

    // The directory name is the version number string of the extension.
    // For example, it could be "1.0_0", "1.0_1", "1.0_2", 1.1_0, 1.1_1, or 1.1_2.
    // The "1.0_1" strings mean that the "1.0_0" directory is existed and you install the version 1.0 again.
    // https://chromium.googlesource.com/chromium/src/+/0b58a813992b539a6b555ad7959adfad744b095a/chrome/common/extensions/extension_file_util_unittest.cc
    entries.sort((a, b) => Services.vc.compare(b, a));

    this._extensionVersionDirectoryNames[path] = entries;
    return entries;
  },

  /**
   * Convert Chrome time format to Date object. Google Chrome uses FILETIME / 10 as time.
   * FILETIME is based on the same structure of Windows.
   *
   * @param {number} aTime Chrome time
   * @param {string|number|Date} aFallbackValue a date or timestamp (valid argument
   *   for the Date constructor) that will be used if the chrometime value passed is
   *   invalid.
   * @returns {Date} converted Date object
   */
  chromeTimeToDate(aTime, aFallbackValue) {
    // The date value may be 0 in some cases. Because of the subtraction below,
    // that'd generate a date before the unix epoch, which can upset consumers
    // due to the unix timestamp then being negative. Catch this case:
    if (!aTime) {
      return new Date(aFallbackValue);
    }
    return new Date((aTime * S100NS_PER_MS - S100NS_FROM1601TO1970) / 10000);
  },

  /**
   * Convert Date object to Chrome time format. For details on Chrome time, see
   * chromeTimeToDate.
   *
   * @param {Date|number} aDate Date object or integer equivalent
   * @returns {number} Chrome time
   */
  dateToChromeTime(aDate) {
    return (aDate * 10000 + S100NS_FROM1601TO1970) / S100NS_PER_MS;
  },

  /**
   * Convert a Chrome visit transition to the closest Places transition type.
   * Only the core transition (the low byte) is considered; qualifier bits are
   * ignored. The Chrome core transition values are defined in:
   * https://source.chromium.org/chromium/chromium/src/+/main:ui/base/page_transition_types.h;l=29-113;drc=23246e7a66e250e1221b9488c0ff3a44d83af097
   *
   * @param {number|null} chromeTransition
   *   The `transition` column of the Chrome `visits` table, or null if
   *   unavailable.
   * @param {number} typedCount
   *   The `typed_count` of the URL, used as a fallback when
   *   `chromeTransition` is null.
   * @returns {number} One of `PlacesUtils.history.TRANSITIONS`.
   */
  chromeTransitionToPlaces(chromeTransition, typedCount) {
    const { TRANSITIONS } = lazy.PlacesUtils.history;
    if (chromeTransition == null) {
      return typedCount > 0 ? TRANSITIONS.TYPED : TRANSITIONS.LINK;
    }
    switch (chromeTransition & CHROME_PAGE_TRANSITION_CORE_MASK) {
      case 1: // TYPED
      case 5: // GENERATED
      case 9: // KEYWORD
      case 10: // KEYWORD_GENERATED
        return TRANSITIONS.TYPED;
      case 2: // AUTO_BOOKMARK
        return TRANSITIONS.BOOKMARK;
      case 3: // AUTO_SUBFRAME
        return TRANSITIONS.EMBED;
      case 4: // MANUAL_SUBFRAME
        return TRANSITIONS.FRAMED_LINK;
      case 8: // RELOAD
        return TRANSITIONS.RELOAD;
      default:
        return TRANSITIONS.LINK;
    }
  },

  /**
   * Merge the children of the same Chrome bookmark root coming from several
   * bookmark files (e.g. "Bookmarks" and "AccountBookmarks"). URL items are
   * concatenated in order; folders with the same name are merged recursively
   * so that a subfolder present in more than one file yields a single folder
   * holding the items from all of them. The input arrays are not modified.
   *
   * @param {Array<Array<object>|undefined>} childrenArrays
   *   The per-file children arrays for a given root, in import order.
   * @returns {Array<object>} The merged list of Chrome bookmark items.
   */
  mergeBookmarkChildren(childrenArrays) {
    let merged = [];
    let foldersByName = new Map();
    for (let children of childrenArrays) {
      if (!children) {
        continue;
      }
      for (let item of children) {
        if (item.type == "folder") {
          let existing = foldersByName.get(item.name);
          if (existing) {
            existing.children = this.mergeBookmarkChildren([
              existing.children,
              item.children,
            ]);
            continue;
          }
          let folder = {
            ...item,
            children: this.mergeBookmarkChildren([item.children]),
          };
          foldersByName.set(item.name, folder);
          merged.push(folder);
        } else {
          merged.push(item);
        }
      }
    }
    return merged;
  },

  /**
   * Returns an array of chromium browser ids that have importable logins.
   */
  _importableLoginsCache: null,
  async getImportableLogins(formOrigin) {
    // Only provide importable if we actually support importing.
    if (!this.supportsLoginsForPlatform) {
      return undefined;
    }

    // Lazily fill the cache with all importable login browsers.
    if (!this._importableLoginsCache) {
      this._importableLoginsCache = new Map();

      // Just handle these chromium-based browsers for now.
      for (const browserId of this.CONTEXTUAL_LOGIN_IMPORT_BROWSERS) {
        // Skip if there's no profile data.
        const migrator = await lazy.MigrationUtils.getMigrator(browserId);
        if (!migrator) {
          continue;
        }

        // Check each profile for logins.
        const dataPath = await migrator._getChromeUserDataPathIfExists();
        for (const profile of await migrator.getSourceProfiles()) {
          const path = PathUtils.join(dataPath, profile.id, "Login Data");
          // Skip if login data is missing.
          if (!(await IOUtils.exists(path))) {
            console.error(`Missing file at ${path}`);
            continue;
          }

          try {
            for (const row of await lazy.MigrationUtils.getRowsFromDBWithoutLocks(
              path,
              `Importable ${browserId} logins`,
              `SELECT origin_url
               FROM logins
               WHERE blacklisted_by_user = 0`
            )) {
              const url = row.getString(0);
              try {
                // Initialize an array if it doesn't exist for the origin yet.
                const origin = lazy.LoginHelper.getLoginOrigin(url);
                const entries = this._importableLoginsCache.get(origin) || [];
                if (!entries.length) {
                  this._importableLoginsCache.set(origin, entries);
                }

                // Add the browser if it doesn't exist yet.
                if (!entries.includes(browserId)) {
                  entries.push(browserId);
                }
              } catch (ex) {
                console.error(
                  `Failed to process importable url ${url} from ${browserId}`,
                  ex
                );
              }
            }
          } catch (ex) {
            console.error(
              `Failed to get importable logins from ${browserId}`,
              ex
            );
          }
        }
      }
    }
    return this._importableLoginsCache.get(formOrigin);
  },
};
