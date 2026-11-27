/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { ctypes } from "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs";
import { MigrationUtils } from "moz-src:///browser/components/migration/MigrationUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  WindowsRegistry: "resource://gre/modules/WindowsRegistry.sys.mjs",
});

const EDGE_FAVORITES = "AC\\MicrosoftEdge\\User\\Default\\Favorites";
const FREE_CLOSE_FAILED = 0;
const INTERNET_EXPLORER_EDGE_GUID = [
  0x3ccd5499, 0x4b1087a8, 0x886015a2, 0x553bdd88,
];
const RESULT_SUCCESS = 0;
const VAULT_ENUMERATE_ALL_ITEMS = 512;
const WEB_CREDENTIALS_VAULT_ID = [
  0x4bf4c442, 0x41a09b8a, 0x4add80b3, 0x28db4d70,
];

const wintypes = {
  BOOL: ctypes.int,
  DWORD: ctypes.uint32_t,
  DWORDLONG: ctypes.uint64_t,
  CHAR: ctypes.char,
  PCHAR: ctypes.char.ptr,
  LPCWSTR: ctypes.char16_t.ptr,
  PDWORD: ctypes.uint32_t.ptr,
  VOIDP: ctypes.voidptr_t,
  WORD: ctypes.uint16_t,
};

// TODO: Bug 1202978 - Refactor MSMigrationUtils ctypes helpers
function CtypesKernelHelpers() {
  this._structs = {};
  this._functions = {};
  this._libs = {};

  this._structs.SYSTEMTIME = new ctypes.StructType("SYSTEMTIME", [
    { wYear: wintypes.WORD },
    { wMonth: wintypes.WORD },
    { wDayOfWeek: wintypes.WORD },
    { wDay: wintypes.WORD },
    { wHour: wintypes.WORD },
    { wMinute: wintypes.WORD },
    { wSecond: wintypes.WORD },
    { wMilliseconds: wintypes.WORD },
  ]);

  this._structs.FILETIME = new ctypes.StructType("FILETIME", [
    { dwLowDateTime: wintypes.DWORD },
    { dwHighDateTime: wintypes.DWORD },
  ]);

  try {
    this._libs.kernel32 = ctypes.open("Kernel32");

    this._functions.FileTimeToSystemTime = this._libs.kernel32.declare(
      "FileTimeToSystemTime",
      ctypes.winapi_abi,
      wintypes.BOOL,
      this._structs.FILETIME.ptr,
      this._structs.SYSTEMTIME.ptr
    );
  } catch (ex) {
    this.finalize();
  }
}

CtypesKernelHelpers.prototype = {
  /**
   * Must be invoked once after last use of any of the provided helpers.
   */
  finalize() {
    this._structs = {};
    this._functions = {};
    for (let key in this._libs) {
      let lib = this._libs[key];
      try {
        lib.close();
      } catch (ex) {}
    }
    this._libs = {};
  },

  /**
   * Converts a FILETIME struct (2 DWORDS), to a SYSTEMTIME struct,
   * and then deduces the number of seconds since the epoch (which
   * is the data we want for the cookie expiry date).
   *
   * @param {number} aTimeHi
   *        Least significant DWORD.
   * @param {number} aTimeLo
   *        Most significant DWORD.
   * @returns {number} the number of seconds since the epoch
   */
  fileTimeToSecondsSinceEpoch(aTimeHi, aTimeLo) {
    let fileTime = this._structs.FILETIME();
    fileTime.dwLowDateTime = aTimeLo;
    fileTime.dwHighDateTime = aTimeHi;
    let systemTime = this._structs.SYSTEMTIME();
    let result = this._functions.FileTimeToSystemTime(
      fileTime.address(),
      systemTime.address()
    );
    if (result == 0) {
      throw new Error(ctypes.winLastError);
    }

    // System time is in UTC, so we use Date.UTC to get milliseconds from epoch,
    // then divide by 1000 to get seconds, and round down:
    return Math.floor(
      Date.UTC(
        systemTime.wYear,
        systemTime.wMonth - 1,
        systemTime.wDay,
        systemTime.wHour,
        systemTime.wMinute,
        systemTime.wSecond,
        systemTime.wMilliseconds
      ) / 1000
    );
  },
};

function CtypesVaultHelpers() {
  this._structs = {};
  this._functions = {};

  this._structs.GUID = new ctypes.StructType("GUID", [
    { id: wintypes.DWORD.array(4) },
  ]);

  this._structs.VAULT_ITEM_ELEMENT = new ctypes.StructType(
    "VAULT_ITEM_ELEMENT",
    [
      // not documented
      { schemaElementId: wintypes.DWORD },
      // not documented
      { unknown1: wintypes.DWORD },
      // vault type
      { type: wintypes.DWORD },
      // not documented
      { unknown2: wintypes.DWORD },
      // value of the item
      { itemValue: wintypes.LPCWSTR },
      // not documented
      { unknown3: wintypes.CHAR.array(12) },
    ]
  );

  this._structs.VAULT_ELEMENT = new ctypes.StructType("VAULT_ELEMENT", [
    // vault item schemaId
    { schemaId: this._structs.GUID },
    // a pointer to the name of the browser VAULT_ITEM_ELEMENT
    { pszCredentialFriendlyName: wintypes.LPCWSTR },
    // a pointer to the url VAULT_ITEM_ELEMENT
    { pResourceElement: this._structs.VAULT_ITEM_ELEMENT.ptr },
    // a pointer to the username VAULT_ITEM_ELEMENT
    { pIdentityElement: this._structs.VAULT_ITEM_ELEMENT.ptr },
    // not documented
    { pAuthenticatorElement: this._structs.VAULT_ITEM_ELEMENT.ptr },
    // not documented
    { pPackageSid: this._structs.VAULT_ITEM_ELEMENT.ptr },
    // time stamp in local format
    { lowLastModified: wintypes.DWORD },
    { highLastModified: wintypes.DWORD },
    // not documented
    { flags: wintypes.DWORD },
    // not documented
    { dwPropertiesCount: wintypes.DWORD },
    // not documented
    { pPropertyElements: this._structs.VAULT_ITEM_ELEMENT.ptr },
  ]);

  try {
    this._vaultcliLib = ctypes.open("vaultcli.dll");

    this._functions.VaultOpenVault = this._vaultcliLib.declare(
      "VaultOpenVault",
      ctypes.winapi_abi,
      wintypes.DWORD,
      // GUID
      this._structs.GUID.ptr,
      // Flags
      wintypes.DWORD,
      // Vault Handle
      wintypes.VOIDP.ptr
    );
    this._functions.VaultEnumerateItems = this._vaultcliLib.declare(
      "VaultEnumerateItems",
      ctypes.winapi_abi,
      wintypes.DWORD,
      // Vault Handle
      wintypes.VOIDP,
      // Flags
      wintypes.DWORD,
      // Items Count
      wintypes.PDWORD,
      // Items
      ctypes.voidptr_t
    );
    this._functions.VaultCloseVault = this._vaultcliLib.declare(
      "VaultCloseVault",
      ctypes.winapi_abi,
      wintypes.DWORD,
      // Vault Handle
      wintypes.VOIDP
    );
    this._functions.VaultGetItem = this._vaultcliLib.declare(
      "VaultGetItem",
      ctypes.winapi_abi,
      wintypes.DWORD,
      // Vault Handle
      wintypes.VOIDP,
      // Schema Id
      this._structs.GUID.ptr,
      // Resource
      this._structs.VAULT_ITEM_ELEMENT.ptr,
      // Identity
      this._structs.VAULT_ITEM_ELEMENT.ptr,
      // Package Sid
      this._structs.VAULT_ITEM_ELEMENT.ptr,
      // HWND Owner
      wintypes.DWORD,
      // Flags
      wintypes.DWORD,
      // Items
      this._structs.VAULT_ELEMENT.ptr.ptr
    );
    this._functions.VaultFree = this._vaultcliLib.declare(
      "VaultFree",
      ctypes.winapi_abi,
      wintypes.DWORD,
      // Memory
      this._structs.VAULT_ELEMENT.ptr
    );
  } catch (ex) {
    this.finalize();
  }
}

CtypesVaultHelpers.prototype = {
  /**
   * Must be invoked once after last use of any of the provided helpers.
   */
  finalize() {
    this._structs = {};
    this._functions = {};
    try {
      this._vaultcliLib.close();
    } catch (ex) {}
    this._vaultcliLib = null;
  },
};

var gEdgeDir;
function getEdgeLocalDataFolder() {
  if (gEdgeDir) {
    return gEdgeDir.clone();
  }
  let packages = Services.dirsvc.get("LocalAppData", Ci.nsIFile);
  packages.append("Packages");
  let edgeDir = packages.clone();
  edgeDir.append("Microsoft.MicrosoftEdge_8wekyb3d8bbwe");
  try {
    if (edgeDir.exists() && edgeDir.isReadable() && edgeDir.isDirectory()) {
      gEdgeDir = edgeDir;
      return edgeDir.clone();
    }

    // Let's try the long way:
    let dirEntries = packages.directoryEntries;
    while (dirEntries.hasMoreElements()) {
      let subDir = dirEntries.nextFile;
      if (
        subDir.leafName.startsWith("Microsoft.MicrosoftEdge") &&
        subDir.isReadable() &&
        subDir.isDirectory()
      ) {
        gEdgeDir = subDir;
        return subDir.clone();
      }
    }
  } catch (ex) {
    console.error(
      "Exception trying to find the Edge favorites directory: ",
      ex
    );
  }
  return null;
}

function Bookmarks(migrationType) {
  this._migrationType = migrationType;
}

Bookmarks.prototype = {
  type: MigrationUtils.resourceTypes.BOOKMARKS,

  get exists() {
    return !!this._favoritesFolder;
  },

  get importedAppLabel() {
    return this._migrationType == MSMigrationUtils.MIGRATION_TYPE_IE
      ? "IE"
      : "Edge";
  },

  __favoritesFolder: null,
  get _favoritesFolder() {
    if (!this.__favoritesFolder) {
      if (this._migrationType == MSMigrationUtils.MIGRATION_TYPE_IE) {
        let favoritesFolder = Services.dirsvc.get("Favs", Ci.nsIFile);
        if (favoritesFolder.exists() && favoritesFolder.isReadable()) {
          this.__favoritesFolder = favoritesFolder;
        }
      } else if (this._migrationType == MSMigrationUtils.MIGRATION_TYPE_EDGE) {
        let edgeDir = getEdgeLocalDataFolder();
        if (edgeDir) {
          edgeDir.appendRelativePath(EDGE_FAVORITES);
          if (
            edgeDir.exists() &&
            edgeDir.isReadable() &&
            edgeDir.isDirectory()
          ) {
            this.__favoritesFolder = edgeDir;
          }
        }
      }
    }
    return this.__favoritesFolder;
  },

  __toolbarFolderName: null,
  get _toolbarFolderName() {
    if (!this.__toolbarFolderName) {
      if (this._migrationType == MSMigrationUtils.MIGRATION_TYPE_IE) {
        // Retrieve the name of IE's favorites subfolder that holds the bookmarks
        // in the toolbar. This was previously stored in the registry and changed
        // in IE7 to always be called "Links".
        let folderName = lazy.WindowsRegistry.readRegKey(
          Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
          "Software\\Microsoft\\Internet Explorer\\Toolbar",
          "LinksFolderName"
        );
        this.__toolbarFolderName = folderName || "Links";
      } else {
        this.__toolbarFolderName = "Links";
      }
    }
    return this.__toolbarFolderName;
  },

  migrate: function B_migrate(aCallback) {
    return (async () => {
      // Import to the bookmarks menu.
      let folderGuid = lazy.PlacesUtils.bookmarks.menuGuid;
      await this._migrateFolder(this._favoritesFolder, folderGuid);
    })().then(
      () => aCallback(true),
      e => {
        console.error(e);
        aCallback(false);
      }
    );
  },

  async _migrateFolder(aSourceFolder, aDestFolderGuid) {
    let { bookmarks, favicons } =
      await this._getBookmarksInFolder(aSourceFolder);
    if (!bookmarks.length) {
      return;
    }

    await MigrationUtils.insertManyBookmarksWrapper(bookmarks, aDestFolderGuid);
    MigrationUtils.insertManyFavicons(favicons).catch(console.error);
  },

  /**
   * Iterates through a bookmark folder to obtain whatever information from each bookmark is needed elsewhere. This function also recurses into child folders.
   *
   * @param {nsIFile} aSourceFolder the folder to search for bookmarks and subfolders.
   * @returns {Promise<object>} An object with the following properties:
   * {Object[]} bookmarks:
   *   An array of Objects with these properties:
   *     {number} type: A type mapping to one of the types in nsINavBookmarksService
   *     {string} title: The title of the bookmark
   *     {Object[]} children: An array of objects with the same structure as this one.
   *
   * {Object[]} favicons
   *   An array of Objects with these properties:
   *     {Uint8Array} faviconData: The binary data of a favicon
   *     {nsIURI} uri: The URI of the associated bookmark
   */
  async _getBookmarksInFolder(aSourceFolder) {
    // TODO (bug 741993): the favorites order is stored in the Registry, at
    // HCU\Software\Microsoft\Windows\CurrentVersion\Explorer\MenuOrder\Favorites
    // for IE, and in a similar location for Edge.
    // Until we support it, bookmarks are imported in alphabetical order.
    let entries = aSourceFolder.directoryEntries;
    let rv = [];
    let favicons = [];
    while (entries.hasMoreElements()) {
      let entry = entries.nextFile;
      try {
        // Make sure that entry.path == entry.target to not follow .lnk folder
        // shortcuts which could lead to infinite cycles.
        // Don't use isSymlink(), since it would throw for invalid
        // lnk files pointing to URLs or to unresolvable paths.
        if (entry.path == entry.target && entry.isDirectory()) {
          let isBookmarksFolder =
            entry.leafName == this._toolbarFolderName &&
            entry.parent.equals(this._favoritesFolder);
          if (isBookmarksFolder && entry.isReadable()) {
            // Import to the bookmarks toolbar.
            let folderGuid = lazy.PlacesUtils.bookmarks.toolbarGuid;
            await this._migrateFolder(entry, folderGuid);
          } else if (entry.isReadable()) {
            let { bookmarks: childBookmarks, favicons: childFavicons } =
              await this._getBookmarksInFolder(entry);
            favicons = favicons.concat(childFavicons);
            rv.push({
              type: lazy.PlacesUtils.bookmarks.TYPE_FOLDER,
              title: entry.leafName,
              children: childBookmarks,
            });
          }
        } else {
          // Strip the .url extension, to both check this is a valid link file,
          // and get the associated title.
          let matches = entry.leafName.match(/(.+)\.url$/i);
          if (matches) {
            let fileHandler = Cc[
              "@mozilla.org/network/protocol;1?name=file"
            ].getService(Ci.nsIFileProtocolHandler);
            let uri = fileHandler.readURLFile(entry);
            // Silently failing in the event that the alternative data stream for the favicon doesn't exist
            try {
              let faviconData = await IOUtils.read(entry.path + ":favicon");
              favicons.push({ faviconData, uri });
            } catch {}

            rv.push({ url: uri, title: matches[1] });
          }
        }
      } catch (ex) {
        console.error(
          "Unable to import ",
          this.importedAppLabel,
          " favorite (",
          entry.leafName,
          "): ",
          ex
        );
      }
    }
    return { bookmarks: rv, favicons };
  },
};

function getTypedURLs(registryKeyPath) {
  // The list of typed URLs is a sort of annotation stored in the registry.
  // The number of entries stored is not UI-configurable, but has changed
  // between different Windows versions. We just keep reading up to the first
  // non-existing entry to support different limits / states of the registry.
  let typedURLs = new Map();
  let typedURLKey = Cc["@mozilla.org/windows-registry-key;1"].createInstance(
    Ci.nsIWindowsRegKey
  );
  let typedURLTimeKey = Cc[
    "@mozilla.org/windows-registry-key;1"
  ].createInstance(Ci.nsIWindowsRegKey);
  let cTypes = new CtypesKernelHelpers();
  try {
    try {
      typedURLKey.open(
        Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
        registryKeyPath + "\\TypedURLs",
        Ci.nsIWindowsRegKey.ACCESS_READ
      );
    } catch (ex) {
      // Ignore errors opening this registry key - if it doesn't work, there's
      // no way we can get useful info here.
      return typedURLs;
    }
    try {
      typedURLTimeKey.open(
        Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
        registryKeyPath + "\\TypedURLsTime",
        Ci.nsIWindowsRegKey.ACCESS_READ
      );
    } catch (ex) {
      typedURLTimeKey = null;
    }
    let entryName;
    for (
      let entry = 1;
      typedURLKey.hasValue((entryName = "url" + entry));
      entry++
    ) {
      let url = typedURLKey.readStringValue(entryName);
      // If we can't get a date for whatever reason, default to 6 months ago
      let timeTyped = Date.now() - 31536000 / 2;
      if (typedURLTimeKey && typedURLTimeKey.hasValue(entryName)) {
        let urlTime = "";
        try {
          urlTime = typedURLTimeKey.readBinaryValue(entryName);
        } catch (ex) {
          console.error("Couldn't read url time for ", entryName);
        }
        if (urlTime.length == 8) {
          let urlTimeHex = [];
          for (let i = 0; i < 8; i++) {
            let c = urlTime.charCodeAt(i).toString(16);
            if (c.length == 1) {
              c = "0" + c;
            }
            urlTimeHex.unshift(c);
          }
          try {
            let hi = parseInt(urlTimeHex.slice(0, 4).join(""), 16);
            let lo = parseInt(urlTimeHex.slice(4, 8).join(""), 16);
            // Convert to seconds since epoch:
            let secondsSinceEpoch = cTypes.fileTimeToSecondsSinceEpoch(hi, lo);

            // If the date is very far in the past, just use the default
            if (secondsSinceEpoch > Date.now() / 1000000) {
              // Callers expect PRTime, which is microseconds since epoch:
              timeTyped = secondsSinceEpoch * 1000;
            }
          } catch (ex) {
            // Ignore conversion exceptions. Callers will have to deal
            // with the fallback value.
          }
        }
      }
      typedURLs.set(url, timeTyped * 1000);
    }
  } catch (ex) {
    console.error("Error reading typed URL history: ", ex);
  } finally {
    if (typedURLKey) {
      typedURLKey.close();
    }
    if (typedURLTimeKey) {
      typedURLTimeKey.close();
    }
    cTypes.finalize();
  }
  return typedURLs;
}

// Migrator for form passwords
function WindowsVaultFormPasswords() {}

WindowsVaultFormPasswords.prototype = {
  type: MigrationUtils.resourceTypes.PASSWORDS,

  get exists() {
    // check if there are passwords available for migration.
    return this.migrate(() => {}, true);
  },

  /**
   * If aOnlyCheckExists is false, import the form passwords from the vault
   * and then call the aCallback.
   * Otherwise, check if there are passwords in the vault.
   *
   * @param {Function} aCallback - a callback called when the migration is done.
   * @param {boolean} [aOnlyCheckExists=false] - if aOnlyCheckExists is true, just check if there are some
   * passwords to migrate. Import the passwords from the vault and call aCallback otherwise.
   * @returns {boolean} true if there are passwords in the vault and aOnlyCheckExists is set to true,
   * false if there is no password in the vault and aOnlyCheckExists is set to true, undefined if
   * aOnlyCheckExists is set to false.
   */
  async migrate(aCallback, aOnlyCheckExists = false) {
    // check if the vault item is an IE/Edge one
    function _isIEOrEdgePassword(id) {
      return (
        id[0] == INTERNET_EXPLORER_EDGE_GUID[0] &&
        id[1] == INTERNET_EXPLORER_EDGE_GUID[1] &&
        id[2] == INTERNET_EXPLORER_EDGE_GUID[2] &&
        id[3] == INTERNET_EXPLORER_EDGE_GUID[3]
      );
    }

    let ctypesVaultHelpers = new CtypesVaultHelpers();
    let ctypesKernelHelpers = new CtypesKernelHelpers();
    let migrationSucceeded = true;
    let successfulVaultOpen = false;
    let error, vault;
    try {
      // web credentials vault id
      let vaultGuid = new ctypesVaultHelpers._structs.GUID(
        WEB_CREDENTIALS_VAULT_ID
      );
      error = new wintypes.DWORD();
      // web credentials vault
      vault = new wintypes.VOIDP();
      // open the current vault using the vaultGuid
      error = ctypesVaultHelpers._functions.VaultOpenVault(
        vaultGuid.address(),
        0,
        vault.address()
      );
      if (error != RESULT_SUCCESS) {
        throw new Error("Unable to open Vault: " + error);
      }
      successfulVaultOpen = true;

      let item = new ctypesVaultHelpers._structs.VAULT_ELEMENT.ptr();
      let itemCount = new wintypes.DWORD();
      // enumerate all the available items. This api is going to return a table of all the
      // available items and item is going to point to the first element of this table.
      error = ctypesVaultHelpers._functions.VaultEnumerateItems(
        vault,
        VAULT_ENUMERATE_ALL_ITEMS,
        itemCount.address(),
        item.address()
      );
      if (error != RESULT_SUCCESS) {
        throw new Error("Unable to enumerate Vault items: " + error);
      }

      let logins = [];
      for (let j = 0; j < itemCount.value; j++) {
        try {
          // if it's not an ie/edge password, skip it
          if (!_isIEOrEdgePassword(item.contents.schemaId.id)) {
            continue;
          }
          let url =
            item.contents.pResourceElement.contents.itemValue.readString();
          let realURL = URL.parse(url);
          if (
            !realURL ||
            !["http:", "https:", "ftp:"].includes(realURL.protocol)
          ) {
            // Ignore items for non-URLs or URLs that aren't HTTP(S)/FTP
            continue;
          }

          // if aOnlyCheckExists is set to true, the purpose of the call is to return true if there is at
          // least a password which is true in this case because a password was by now already found
          if (aOnlyCheckExists) {
            return true;
          }
          let username =
            item.contents.pIdentityElement.contents.itemValue.readString();
          // the current login credential object
          let credential = new ctypesVaultHelpers._structs.VAULT_ELEMENT.ptr();
          error = ctypesVaultHelpers._functions.VaultGetItem(
            vault,
            item.contents.schemaId.address(),
            item.contents.pResourceElement,
            item.contents.pIdentityElement,
            null,
            0,
            0,
            credential.address()
          );
          if (error != RESULT_SUCCESS) {
            throw new Error("Unable to get item: " + error);
          }

          let password =
            credential.contents.pAuthenticatorElement.contents.itemValue.readString();
          let creation = Date.now();
          try {
            // login manager wants time in milliseconds since epoch, so convert
            // to seconds since epoch and multiply to get milliseconds:
            creation =
              ctypesKernelHelpers.fileTimeToSecondsSinceEpoch(
                item.contents.highLastModified,
                item.contents.lowLastModified
              ) * 1000;
          } catch (ex) {
            // Ignore exceptions in the dates and just create the login for right now.
          }
          // create a new login
          logins.push({
            username,
            password,
            origin: realURL.URI.prePath,
            timeCreated: creation,
          });

          // close current item
          error = ctypesVaultHelpers._functions.VaultFree(credential);
          if (error == FREE_CLOSE_FAILED) {
            throw new Error("Unable to free item: " + error);
          }
        } catch (e) {
          migrationSucceeded = false;
          console.error(e);
        } finally {
          // move to next item in the table returned by VaultEnumerateItems
          item = item.increment();
        }
      }

      if (logins.length) {
        await MigrationUtils.insertLoginsWrapper(logins);
      }
    } catch (e) {
      console.error(e);
      migrationSucceeded = false;
    } finally {
      if (successfulVaultOpen) {
        // close current vault
        error = ctypesVaultHelpers._functions.VaultCloseVault(vault);
        if (error == FREE_CLOSE_FAILED) {
          console.error("Unable to close vault: ", error);
        }
      }
      ctypesKernelHelpers.finalize();
      ctypesVaultHelpers.finalize();
      aCallback(migrationSucceeded);
    }
    if (aOnlyCheckExists) {
      return false;
    }
    return undefined;
  },
};

export var MSMigrationUtils = {
  MIGRATION_TYPE_IE: 1,
  MIGRATION_TYPE_EDGE: 2,
  CtypesKernelHelpers,
  getBookmarksMigrator(migrationType = this.MIGRATION_TYPE_IE) {
    return new Bookmarks(migrationType);
  },
  getWindowsVaultFormPasswordsMigrator() {
    return new WindowsVaultFormPasswords();
  },
  getTypedURLs,
  getEdgeLocalDataFolder,
};
