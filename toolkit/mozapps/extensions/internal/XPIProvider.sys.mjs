/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This file contains most of the logic required to load and run
 * extensions at startup. Anything which is not required immediately at
 * startup should go in XPIInstall.sys.mjs or XPIDatabase.sys.mjs if at all
 * possible, in order to minimize the impact on startup performance.
 */

/**
 * @typedef {number} integer
 */

/* eslint "valid-jsdoc": [2, {requireReturn: false, requireReturnDescription: false, prefer: {return: "returns"}}] */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { XPIExports } from "resource://gre/modules/addons/XPIExports.sys.mjs";
import {
  AddonManager,
  AddonManagerPrivate,
} from "resource://gre/modules/AddonManager.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddonSettings: "resource://gre/modules/addons/AddonSettings.sys.mjs",
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  Dictionary: "resource://gre/modules/Extension.sys.mjs",
  Extension: "resource://gre/modules/Extension.sys.mjs",
  ExtensionData: "resource://gre/modules/Extension.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  JSONFile: "resource://gre/modules/JSONFile.sys.mjs",
  Langpack: "resource://gre/modules/Extension.sys.mjs",
  TelemetrySession: "resource://gre/modules/TelemetrySession.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetters(lazy, {
  aomStartup: [
    "@mozilla.org/addons/addon-manager-startup;1",
    "amIAddonManagerStartup",
  ],
  resProto: [
    "@mozilla.org/network/protocol;1?name=resource",
    "nsISubstitutingProtocolHandler",
  ],
  spellCheck: ["@mozilla.org/spellchecker/engine;1", "mozISpellCheckingEngine"],
  timerManager: [
    "@mozilla.org/updates/timer-manager;1",
    "nsIUpdateTimerManager",
  ],
});

const nsIFile = Components.Constructor(
  "@mozilla.org/file/local;1",
  "nsIFile",
  "initWithPath"
);
const FileInputStream = Components.Constructor(
  "@mozilla.org/network/file-input-stream;1",
  "nsIFileInputStream",
  "init"
);

const PREF_DB_SCHEMA = "extensions.databaseSchema";
const PREF_PENDING_OPERATIONS = "extensions.pendingOperations";
const PREF_EM_ENABLED_SCOPES = "extensions.enabledScopes";
const PREF_EM_STARTUP_SCAN_SCOPES = "extensions.startupScanScopes";
// xpinstall.signatures.required only supported in dev builds
const PREF_XPI_SIGNATURES_REQUIRED = "xpinstall.signatures.required";
const PREF_LANGPACK_SIGNATURES = "extensions.langpacks.signatures.required";
const PREF_INSTALL_DISTRO_ADDONS = "extensions.installDistroAddons";
const PREF_BRANCH_INSTALLED_ADDON = "extensions.installedDistroAddon.";
const PREF_SYSTEM_ADDON_SET = "extensions.systemAddonSet";

const PREF_EM_LAST_APP_BUILD_ID = "extensions.lastAppBuildId";

const PREF_DATA_COLLECTION_PERMISSIONS_ENABLED =
  "extensions.dataCollectionPermissions.enabled";

// Specify a list of valid built-in add-ons to load.
const BUILT_IN_ADDONS_URI = "chrome://browser/content/built_in_addons.json";

const DIR_EXTENSIONS = "extensions";
const DIR_SYSTEM_ADDONS = "features";
const DIR_APP_SYSTEM_PROFILE = "system-extensions";
const DIR_STAGE = "staged";
const DIR_TRASH = "trash";

const FILE_XPI_STATES = "addonStartup.json.lz4";

const KEY_PROFILEDIR = "ProfD";
const KEY_ADDON_APP_DIR = "XREAddonAppDir";
const KEY_APP_DISTRIBUTION = "XREAppDist";

const KEY_APP_PROFILE = "app-profile";
// Location of add-ons included in the omni jar and listed in built_in_addons.json.
// TODO: consider renaming to `KEY_APP_BUILTIN_ADDONS` when `KEY_APP_BUILTINS`
// has been removed (since it would be confusing to have two similar `KEY_APP_`
// constants while `KEY_APP_BUILTINS` is still defined).
const KEY_APP_SYSTEM_BUILTINS = "app-builtin-addons";
const KEY_APP_SYSTEM_PROFILE = "app-system-profile";
// Location of add-on xpi files signed with a system signature downloaded from balrog.
const KEY_APP_SYSTEM_ADDONS = "app-system-addons";
// Location of add-ons included in the omni jar and manually installed through maybeInstallBuiltinAddon method.
const KEY_APP_BUILTINS = "app-builtin";
const KEY_APP_GLOBAL = "app-global";
const KEY_APP_SYSTEM_LOCAL = "app-system-local";
const KEY_APP_SYSTEM_SHARE = "app-system-share";
const KEY_APP_SYSTEM_USER = "app-system-user";
const KEY_APP_TEMPORARY = "app-temporary";

const TEMPORARY_ADDON_SUFFIX = "@temporary-addon";

const STARTUP_MTIME_SCOPES = [
  KEY_APP_GLOBAL,
  KEY_APP_SYSTEM_LOCAL,
  KEY_APP_SYSTEM_SHARE,
  KEY_APP_SYSTEM_USER,
];

const NOTIFICATION_FLUSH_PERMISSIONS = "flush-pending-permissions";
const XPI_PERMISSION = "install";

// This preference name is from UpdateTimerManager.sys.mjs; it stores the
// timestamp (seconds) of the last periodic signature check.
const PREF_LAST_SIGNATURE_CHECK_TIME =
  "app.update.lastUpdateTime.xpi-signature-verification";
const PREF_LAST_SIGNATURE_CHECKPOINT = "extensions.signatureCheckpoint";
// SIGNATURE_CHECKPOINT should be incremented whenever an implementation change
// happens that affects the validity of add-on signatures of already-installed
// add-ons. This forces all add-on signatures to be verified again.
const XPI_SIGNATURE_CHECKPOINT = 1;

const XPI_SIGNATURE_CHECK_PERIOD = 24 * 60 * 60;

const DB_SCHEMA = 37;

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "enabledScopesPref",
  PREF_EM_ENABLED_SCOPES,
  AddonManager.SCOPE_ALL
);

// An hidden pref that can be used in tests (in particular
// xpcshell-tests unit tests) that may need to opt-out from
// XPIProvider startup logic that will be auto-installing
// the default theme.
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "skipDefaultThemeInstall",
  "extensions.skipInstallDefaultThemeForTests",
  false
);

Object.defineProperty(lazy, "enabledScopes", {
  get() {
    // The profile location is always enabled
    return lazy.enabledScopesPref | AddonManager.SCOPE_PROFILE;
  },
});

function encoded(strings, ...values) {
  let result = [];

  for (let [i, string] of strings.entries()) {
    result.push(string);
    if (i < values.length) {
      result.push(encodeURIComponent(values[i]));
    }
  }

  return result.join("");
}

const BOOTSTRAP_REASONS = {
  APP_STARTUP: 1,
  APP_SHUTDOWN: 2,
  ADDON_ENABLE: 3,
  ADDON_DISABLE: 4,
  ADDON_INSTALL: 5,
  ADDON_UNINSTALL: 6,
  ADDON_UPGRADE: 7,
  ADDON_DOWNGRADE: 8,
};

// All addonTypes supported by the XPIProvider. These values can be passed to
// AddonManager.getAddonsByTypes in order to get XPIProvider.getAddonsByTypes
// to return only supported add-ons. Without these, it is possible for
// AddonManager.getAddonsByTypes to return addons from other providers, or even
// add-on types that are no longer supported by XPIProvider.
const ALL_XPI_TYPES = new Set(["dictionary", "extension", "locale", "theme"]);

/**
 * Valid IDs fit this pattern.
 */
var gIDTest =
  /^(\{[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\}|[a-z0-9-\._]*\@[a-z0-9-\._]+)$/i;

import { Log } from "resource://gre/modules/Log.sys.mjs";

const LOGGER_ID = "addons.xpi";

// Create a new logger for use by all objects in this Addons XPI Provider module
// (Requires AddonManager.sys.mjs)
var logger = Log.repository.getLogger(LOGGER_ID);

/**
 * Spins the event loop until the given promise resolves, and then eiter returns
 * its success value or throws its rejection value.
 *
 * @param {Promise} promise
 *        The promise to await.
 * @returns {any}
 *        The promise's resolution value, if any.
 */
function awaitPromise(promise) {
  let success = undefined;
  let result = null;

  promise.then(
    val => {
      success = true;
      result = val;
    },
    val => {
      success = false;
      result = val;
    }
  );

  Services.tm.spinEventLoopUntil(
    "XPIProvider.sys.mjs:awaitPromise",
    () => success !== undefined
  );

  if (!success) {
    throw result;
  }
  return result;
}

/**
 * Returns a nsIFile instance for the given path, relative to the given
 * base file, if provided.
 *
 * @param {string} path
 *        The (possibly relative) path of the file.
 * @param {nsIFile} [base]
 *        An optional file to use as a base path if `path` is relative.
 * @returns {nsIFile}
 */
function getFile(path, base = null) {
  // First try for an absolute path, as we get in the case of proxy
  // files. Ideally we would try a relative path first, but on Windows,
  // paths which begin with a drive letter are valid as relative paths,
  // and treated as such.
  try {
    return new nsIFile(path);
  } catch (e) {
    // Ignore invalid relative paths. The only other error we should see
    // here is EOM, and either way, any errors that we care about should
    // be re-thrown below.
  }

  // If the path isn't absolute, we must have a base path.
  let file = base.clone();
  file.appendRelativePath(path);
  return file;
}

/**
 * Returns true if the given file, based on its name, should be treated
 * as an XPI. If the file does not have an appropriate extension, it is
 * assumed to be an unpacked add-on.
 *
 * @param {string} filename
 *        The filename to check.
 * @param {boolean} [strict = false]
 *        If true, this file is in a location maintained by the browser, and
 *        must have a strict, lower-case ".xpi" extension.
 * @returns {boolean}
 *        True if the file is an XPI.
 */
function isXPI(filename, strict) {
  if (strict) {
    return filename.endsWith(".xpi");
  }
  let ext = filename.slice(-4).toLowerCase();
  return ext === ".xpi" || ext === ".zip";
}

/**
 * Returns the extension expected ID for a given file in an extension install
 * directory.
 *
 * @param {nsIFile} file
 *        The extension XPI file or unpacked directory.
 * @returns {AddonId?}
 *        The add-on ID, if valid, or null otherwise.
 */
function getExpectedID(file) {
  let { leafName } = file;
  let id = isXPI(leafName, true) ? leafName.slice(0, -4) : leafName;
  if (gIDTest.test(id)) {
    return id;
  }
  return null;
}

/**
 * Evaluates whether an add-on is allowed to run in safe mode.
 *
 * @param {AddonInternal} aAddon
 *        The add-on to check
 * @returns {boolean}
 *        True if the add-on should run in safe mode
 */
function canRunInSafeMode(aAddon) {
  let location = aAddon.location || null;
  if (!location) {
    return false;
  }

  // Even though the updated system add-ons aren't generally run in safe mode we
  // include them here so their uninstall functions get called when switching
  // back to the default set.

  // TODO product should make the call about temporary add-ons running
  // in safe mode. assuming for now that they are.
  return location.isTemporary || location.isSystem || location.isBuiltin;
}

/**
 * Gets an nsIURI for a file within another file, either a directory or an XPI
 * file. If aFile is a directory then this will return a file: URI, if it is an
 * XPI file then it will return a jar: URI.
 *
 * @param {nsIFile} aFile
 *        The file containing the resources, must be either a directory or an
 *        XPI file
 * @param {string} aPath
 *        The path to find the resource at, "/" separated. If aPath is empty
 *        then the uri to the root of the contained files will be returned
 * @returns {nsIURI}
 *        An nsIURI pointing at the resource
 */
function getURIForResourceInFile(aFile, aPath) {
  if (!isXPI(aFile.leafName)) {
    let resource = aFile.clone();
    if (aPath) {
      aPath.split("/").forEach(part => resource.append(part));
    }

    return Services.io.newFileURI(resource);
  }

  return buildJarURI(aFile, aPath);
}

/**
 * Creates a jar: URI for a file inside a ZIP file.
 *
 * @param {nsIFile} aJarfile
 *        The ZIP file as an nsIFile
 * @param {string} aPath
 *        The path inside the ZIP file
 * @returns {nsIURI}
 *        An nsIURI for the file
 */
function buildJarURI(aJarfile, aPath) {
  let uri = Services.io.newFileURI(aJarfile);
  uri = "jar:" + uri.spec + "!/" + aPath;
  return Services.io.newURI(uri);
}

function maybeResolveURI(uri) {
  if (uri.schemeIs("resource")) {
    return Services.io.newURI(lazy.resProto.resolveURI(uri));
  }
  return uri;
}

/**
 * Iterates over the entries in a given directory.
 *
 * Fails silently if the given directory does not exist.
 *
 * @param {nsIFile} aDir
 *        Directory to iterate.
 */
function* iterDirectory(aDir) {
  let dirEnum;
  try {
    dirEnum = aDir.directoryEntries;
    let file;
    while ((file = dirEnum.nextFile)) {
      yield file;
    }
  } catch (e) {
    if (aDir.exists()) {
      logger.warn(`Can't iterate directory ${aDir.path}`, e);
    }
  } finally {
    if (dirEnum) {
      dirEnum.close();
    }
  }
}

/**
 * Migrate data about an addon to match the change made in bug 857456
 * in which "webextension-foo" types were converted to "foo" and the
 * "loader" property was added to distinguish different addon types.
 *
 * @param {Object} addon  The addon info to migrate.
 * @returns {boolean} True if the addon data was converted, false if not.
 */
function migrateAddonLoader(addon) {
  if (addon.hasOwnProperty("loader")) {
    return false;
  }

  switch (addon.type) {
    case "extension":
    case "dictionary":
    case "locale":
    case "theme":
      addon.loader = "bootstrap";
      break;

    case "webextension":
      addon.type = "extension";
      addon.loader = null;
      break;

    case "webextension-dictionary":
      addon.type = "dictionary";
      addon.loader = null;
      break;

    case "webextension-langpack":
      addon.type = "locale";
      addon.loader = null;
      break;

    case "webextension-theme":
      addon.type = "theme";
      addon.loader = null;
      break;

    default:
      logger.warn(`Not converting unknown addon type ${addon.type}`);
  }
  return true;
}

/**
 * The on-disk state of an individual XPI, created from an Object
 * as stored in the addonStartup.json file.
 */
const JSON_FIELDS = Object.freeze([
  "blocklistState",
  "dependencies",
  "enabled",
  "file",
  "loader",
  "lastModifiedTime",
  "path",
  "recommendationState",
  "rootURI",
  "runInSafeMode",
  "signedState",
  "signedDate",
  "startupData",
  "telemetryKey",
  "type",
  "version",
]);

class XPIState {
  constructor(location, id, saved = {}) {
    this.location = location;
    this.id = id;

    // Set default values.
    this.type = "extension";

    for (let prop of JSON_FIELDS) {
      if (prop in saved) {
        this[prop] = saved[prop];
      }
    }

    // Builds prior to be 1512436 did not include the rootURI property.
    // If we're updating from such a build, add that property now.
    if (!("rootURI" in this) && this.file) {
      this.rootURI = getURIForResourceInFile(this.file, "").spec;
    }

    if (!this.telemetryKey) {
      this.telemetryKey = this.getTelemetryKey();
    }

    if (
      saved.currentModifiedTime &&
      saved.currentModifiedTime != this.lastModifiedTime
    ) {
      this.lastModifiedTime = saved.currentModifiedTime;
    } else if (saved.currentModifiedTime === null) {
      this.missing = true;
    }
  }

  // Compatibility shim getters for legacy callers in XPIDatabase.sys.mjs.
  get mtime() {
    return this.lastModifiedTime;
  }
  get active() {
    return this.enabled;
  }

  /**
   * @property {string} path
   *        The full on-disk path of the add-on.
   */
  get path() {
    return this.file && this.file.path;
  }
  set path(path) {
    this.file = path ? getFile(path, this.location.dir) : null;
  }

  /**
   * @property {string} relativePath
   *        The path to the add-on relative to its parent location, or
   *        the full path if its parent location has no on-disk path.
   */
  get relativePath() {
    if (this.location.dir && this.location.dir.contains(this.file)) {
      let path = this.file.getRelativePath(this.location.dir);
      if (AppConstants.platform == "win") {
        path = path.replace(/\//g, "\\");
      }
      return path;
    }
    return this.path;
  }

  /**
   * Returns a JSON-compatible representation of this add-on's state
   * data, to be saved to addonStartup.json.
   *
   * @returns {Object}
   */
  toJSON() {
    let json = {
      blocklistState: this.blocklistState,
      dependencies: this.dependencies,
      enabled: this.enabled,
      lastModifiedTime: this.lastModifiedTime,
      loader: this.loader,
      path: this.relativePath,
      recommendationState: this.recommendationState,
      rootURI: this.rootURI,
      runInSafeMode: this.runInSafeMode,
      signedState: this.signedState,
      signedDate: this.signedDate,
      telemetryKey: this.telemetryKey,
      version: this.version,
    };
    if (this.type != "extension") {
      json.type = this.type;
    }
    if (this.startupData) {
      json.startupData = this.startupData;
    }
    return json;
  }

  get isWebExtension() {
    return this.loader == null;
  }

  get isPrivileged() {
    return lazy.ExtensionData.getIsPrivileged({
      signedState: this.signedState,
      builtIn: this.location.isBuiltin,
      temporarilyInstalled: this.location.isTemporary,
    });
  }

  /**
   * Update the last modified time for an add-on on disk.
   *
   * @param {nsIFile} aFile
   *        The location of the add-on.
   * @returns {boolean}
   *       True if the time stamp has changed.
   */
  getModTime(aFile) {
    let mtime = 0;
    try {
      // Clone the file object so we always get the actual mtime, rather
      // than whatever value it may have cached.
      mtime = aFile.clone().lastModifiedTime;
    } catch (e) {
      logger.warn("Can't get modified time of ${path} ${e}", {
        path: aFile?.path,
        e,
      });
    }

    let changed = mtime != this.lastModifiedTime;
    this.lastModifiedTime = mtime;
    return changed;
  }

  /**
   * Returns a string key by which to identify this add-on in telemetry
   * and crash reports.
   *
   * @returns {string}
   */
  getTelemetryKey() {
    return encoded`${this.id}:${this.version}`;
  }

  get resolvedRootURI() {
    return maybeResolveURI(Services.io.newURI(this.rootURI));
  }

  /**
   * Update the XPIState to match an XPIDatabase entry; if 'enabled' is changed to true,
   * update the last-modified time. This should probably be made async, but for now we
   * don't want to maintain parallel sync and async versions of the scan.
   *
   * Caller is responsible for doing XPIStates.save() if necessary.
   *
   * @param {DBAddonInternal} aDBAddon
   *        The DBAddonInternal for this add-on.
   * @param {boolean} [aUpdated = false]
   *        The add-on was updated, so we must record new modified time.
   */
  syncWithDB(aDBAddon, aUpdated = false) {
    logger.debug("Updating XPIState for " + JSON.stringify(aDBAddon));
    // If the add-on changes from disabled to enabled, we should re-check the modified time.
    // If this is a newly found add-on, it won't have an 'enabled' field but we
    // did a full recursive scan in that case, so we don't need to do it again.
    // We don't use aDBAddon.active here because it's not updated until after restart.
    let mustGetMod = aDBAddon.visible && !aDBAddon.disabled && !this.enabled;

    this.enabled = aDBAddon.visible && !aDBAddon.disabled;

    this.version = aDBAddon.version;
    this.type = aDBAddon.type;
    this.loader = aDBAddon.loader;

    if (aDBAddon.startupData) {
      this.startupData = aDBAddon.startupData;
    }

    this.telemetryKey = this.getTelemetryKey();

    this.dependencies = aDBAddon.dependencies;
    this.runInSafeMode = canRunInSafeMode(aDBAddon);
    this.signedState = aDBAddon.signedState;
    this.signedDate = aDBAddon.signedDate;
    this.file = aDBAddon._sourceBundle;
    this.rootURI = aDBAddon.rootURI;
    this.recommendationState = aDBAddon.recommendationState;
    this.blocklistState = aDBAddon.blocklistState;

    if ((aUpdated || mustGetMod) && this.file) {
      this.getModTime(this.file);
      if (this.lastModifiedTime != aDBAddon.updateDate) {
        aDBAddon.updateDate = this.lastModifiedTime;
        if (XPIExports.XPIDatabase.initialized) {
          XPIExports.XPIDatabase.saveChanges();
        }
      }
    }
  }
}

/**
 * Manages the state data for add-ons in a given install location.
 *
 * @param {string} name
 *        The name of the install location (e.g., "app-profile").
 * @param {string | nsIFile | null} path
 *        The on-disk path of the install location. May be null for some
 *        locations which do not map to a specific on-disk path.
 * @param {integer} scope
 *        The scope of add-ons installed in this location.
 * @param {object} [saved]
 *        The persisted JSON state data to restore.
 */
class XPIStateLocation extends Map {
  constructor(name, path, scope, saved) {
    super();

    this.name = name;
    this.scope = scope;
    if (path instanceof Ci.nsIFile) {
      this.dir = path;
      this.path = path.path;
    } else {
      this.path = path;
      this.dir = this.path && new nsIFile(this.path);
    }
    this.staged = {};
    this.changed = false;

    if (saved) {
      this.restore(saved);
    }

    this._installer = undefined;
  }

  hasPrecedence(otherLocation) {
    let locations = Array.from(XPIStates.locations());
    return locations.indexOf(this) <= locations.indexOf(otherLocation);
  }

  get installer() {
    if (this._installer === undefined) {
      this._installer = this.makeInstaller();
    }
    return this._installer;
  }

  makeInstaller() {
    return null;
  }

  restore(saved) {
    if (!this.path && saved.path) {
      this.path = saved.path;
      this.dir = new nsIFile(this.path);
    }
    this.staged = saved.staged || {};
    this.changed = saved.changed || false;

    for (let [id, data] of Object.entries(saved.addons || {})) {
      let xpiState = this._addState(id, data);

      // Make a note that this state was restored from saved data. But
      // only if this location hasn't moved since the last startup,
      // since that causes problems for new system add-on bundles.
      if (!this.path || this.path == saved.path) {
        xpiState.wasRestored = true;
      }
    }
  }

  /**
   * Returns a JSON-compatible representation of this location's state
   * data, to be saved to addonStartup.json.
   *
   * @returns {Object}
   */
  toJSON() {
    let json = {
      addons: {},
      staged: this.staged,
    };

    if (this.path) {
      json.path = this.path;
    }

    if (STARTUP_MTIME_SCOPES.includes(this.name)) {
      json.checkStartupModifications = true;
    }

    for (let [id, addon] of this.entries()) {
      json.addons[id] = addon;
    }
    return json;
  }

  get hasStaged() {
    for (let key in this.staged) {
      return true;
    }
    return false;
  }

  _addState(addonId, saved) {
    let xpiState = new XPIState(this, addonId, saved);
    this.set(addonId, xpiState);
    return xpiState;
  }

  /**
   * Adds state data for the given DB add-on to the DB.
   *
   * @param {DBAddon} addon
   *        The DBAddon to add.
   */
  addAddon(addon) {
    logger.debug(
      "XPIStates adding add-on ${id} in ${location}: ${path}",
      addon
    );

    XPIProvider.persistStartupData(addon);

    let xpiState = this._addState(addon.id, { file: addon._sourceBundle });
    xpiState.syncWithDB(addon, true);

    XPIProvider.addTelemetry(addon.id, { location: this.name });
  }

  /**
   * Remove the XPIState for an add-on and save the new state.
   *
   * @param {string} aId
   *        The ID of the add-on.
   */
  removeAddon(aId) {
    if (this.has(aId)) {
      this.delete(aId);
      XPIStates.save();
    }
  }

  /**
   * Adds stub state data for the local file to the DB.
   *
   * @param {string} addonId
   *        The ID of the add-on represented by the given file.
   * @param {nsIFile} file
   *        The local file or directory containing the add-on.
   * @returns {XPIState}
   */
  addFile(addonId, file) {
    let xpiState = this._addState(addonId, {
      enabled: false,
      file: file.clone(),
    });
    xpiState.getModTime(xpiState.file);
    return xpiState;
  }

  /**
   * Adds metadata for a staged install which should be performed after
   * the next restart.
   *
   * @param {string} addonId
   *        The ID of the staged install. The leaf name of the XPI
   *        within the location's staging directory must correspond to
   *        this ID.
   * @param {object} metadata
   *        The JSON metadata of the parsed install, to be used during
   *        the next startup.
   */
  stageAddon(addonId, metadata) {
    this.staged[addonId] = metadata;
    XPIStates.save();
  }

  /**
   * Removes staged install metadata for the given add-on ID.
   *
   * @param {string} addonId
   *        The ID of the staged install.
   */
  unstageAddon(addonId) {
    if (addonId in this.staged) {
      delete this.staged[addonId];
      XPIStates.save();
    }
  }

  *getStagedAddons() {
    for (let [id, metadata] of Object.entries(this.staged)) {
      yield [id, metadata];
    }
  }

  /**
   * Returns true if the given addon was installed in this location by a text
   * file pointing to its real path.
   *
   * @param {string} aId
   *        The ID of the addon
   * @returns {boolean}
   */
  isLinkedAddon(aId) {
    if (!this.dir) {
      return true;
    }
    return this.has(aId) && !this.dir.contains(this.get(aId).file);
  }

  get isTemporary() {
    return false;
  }

  get isSystem() {
    return false;
  }

  get isBuiltin() {
    return false;
  }

  get hidden() {
    return this.isBuiltin;
  }

  // If this property is false, it does not implement readAddons()
  // interface.  This is used for the temporary and built-in locations
  // that do not correspond to a physical location that can be scanned.
  get enumerable() {
    return true;
  }
}

class TemporaryLocation extends XPIStateLocation {
  /**
   * @param {string} name
   *        The string identifier for the install location.
   */
  constructor(name) {
    super(name, null, AddonManager.SCOPE_TEMPORARY);
    this.locked = false;
  }

  makeInstaller() {
    // Installs are a no-op. We only register that add-ons exist, and
    // run them from their current location.
    return {
      installAddon() {},
      uninstallAddon() {},
    };
  }

  toJSON() {
    return {};
  }

  get isTemporary() {
    return true;
  }

  get enumerable() {
    return false;
  }
}

var TemporaryInstallLocation = new TemporaryLocation(KEY_APP_TEMPORARY);

/**
 * A "location" for addons installed from assets packged into the app.
 */
var BuiltInLocation = new (class _BuiltInLocation extends XPIStateLocation {
  constructor() {
    super(KEY_APP_BUILTINS, null, AddonManager.SCOPE_APPLICATION);
    this.locked = false;
  }

  // The installer object is responsible for moving files around on disk
  // when (un)installing an addon.  Since this location handles only addons
  // that are embedded within the browser, these are no-ops.
  makeInstaller() {
    return {
      installAddon() {},
      uninstallAddon() {},
    };
  }

  get hidden() {
    return false;
  }

  get isBuiltin() {
    return true;
  }

  get enumerable() {
    return false;
  }

  // Builtin addons are never linked, return false
  // here for correct behavior elsewhere.
  isLinkedAddon(/* aId */) {
    return false;
  }
})();

/**
 * A "location" for system addons installed from assets packaged into the app.
 */
var SystemBuiltInLocation =
  new (class _SystemBuiltInLocation extends XPIStateLocation {
    constructor() {
      super(KEY_APP_SYSTEM_BUILTINS, null, AddonManager.SCOPE_APPLICATION);
      // This location is locked and system addons are added and removed
      // from this location through XPIProvider.scanForChanges and
      // XPIDatabaseReconcile.processFileChanges based on what readAddons
      // methods return.
      this.locked = true;
    }

    // The installer object is responsible for moving files around on disk
    // when (un)installing an addon.  Since this location handles only addons
    // that are embedded within the browser, these are no-ops.
    makeInstaller() {
      return {
        installAddon() {},
        uninstallAddon() {},
      };
    }

    /**
     * Finds all the add-ons installed in this location.
     *
     * @returns {Map<AddonID, {builtin: { addon_version: String, res_url: String}}>}
     *        A map of add-ons present in this location.
     */
    readAddons() {
      let addons = new Map();

      let manifest = XPIProvider.builtInAddons;

      if (!("builtins" in manifest)) {
        logger.debug("No list of valid builtins add-ons found.");
        return addons;
      }

      for (let { addon_id, addon_version, res_url } of manifest.builtins) {
        addons.set(addon_id, { builtin: { addon_version, res_url } });
      }

      return addons;
    }

    get hidden() {
      return true;
    }

    get isBuiltin() {
      return true;
    }

    get isSystem() {
      return true;
    }

    get enumerable() {
      return true;
    }

    // Builtin addons are never linked, return false
    // here for correct behavior elsewhere.
    isLinkedAddon(/* aId */) {
      return false;
    }
  })();

/**
 * An object which identifies a directory install location for add-ons. The
 * location consists of a directory which contains the add-ons installed in the
 * location.
 *
 */
class DirectoryLocation extends XPIStateLocation {
  /**
   * Each add-on installed in the location is either a directory containing the
   * add-on's files or a text file containing an absolute path to the directory
   * containing the add-ons files. The directory or text file must have the same
   * name as the add-on's ID.
   *
   * @param {string} name
   *        The string identifier for the install location.
   * @param {nsIFile} dir
   *        The directory for the install location.
   * @param {integer} scope
   *        The scope of add-ons installed in this location.
   * @param {boolean} [locked = true]
   *        If false, the location accepts new add-on installs.
   * @param {boolean} [system = false]
   *        If true, the location is a system addon location.
   */
  constructor(name, dir, scope, locked = true, system = false) {
    super(name, dir, scope);
    this.locked = locked;
    this._isSystem = system;
  }

  makeInstaller() {
    if (this.locked) {
      return null;
    }
    return new XPIExports.XPIInstall.DirectoryInstaller(this);
  }

  /**
   * Reads a single-line file containing the path to a directory, and
   * returns an nsIFile pointing to that directory, if successful.
   *
   * @param {nsIFile} aFile
   *        The file containing the directory path
   * @returns {nsIFile?}
   *        An nsIFile object representing the linked directory, or null
   *        on error.
   */
  _readLinkFile(aFile) {
    let linkedDirectory;
    if (aFile.isSymlink()) {
      linkedDirectory = aFile.clone();
      try {
        linkedDirectory.normalize();
      } catch (e) {
        logger.warn(
          `Symbolic link ${aFile.path} points to a path ` +
            `which does not exist`
        );
        return null;
      }
    } else {
      let fis = new FileInputStream(aFile, -1, -1, false);
      let line = {};
      fis.QueryInterface(Ci.nsILineInputStream).readLine(line);
      fis.close();

      if (line.value) {
        linkedDirectory = Cc["@mozilla.org/file/local;1"].createInstance(
          Ci.nsIFile
        );
        try {
          linkedDirectory.initWithPath(line.value);
        } catch (e) {
          linkedDirectory.setRelativeDescriptor(aFile.parent, line.value);
        }
      }
    }

    if (linkedDirectory) {
      if (!linkedDirectory.exists()) {
        logger.warn(
          `File pointer ${aFile.path} points to ${linkedDirectory.path} ` +
            "which does not exist"
        );
        return null;
      }

      if (!linkedDirectory.isDirectory()) {
        logger.warn(
          `File pointer ${aFile.path} points to ${linkedDirectory.path} ` +
            "which is not a directory"
        );
        return null;
      }

      return linkedDirectory;
    }

    logger.warn(`File pointer ${aFile.path} does not contain a path`);
    return null;
  }

  /**
   * Finds all the add-ons installed in this location.
   *
   * @returns {Map<AddonID, nsIFile>}
   *        A map of add-ons present in this location.
   */
  readAddons() {
    let addons = new Map();

    if (!this.dir) {
      return addons;
    }

    // Use a snapshot of the directory contents to avoid possible issues with
    // iterating over a directory while removing files from it (the YAFFS2
    // embedded filesystem has this issue, see bug 772238).
    for (let entry of Array.from(iterDirectory(this.dir))) {
      let id = getExpectedID(entry);
      if (!id) {
        if (![DIR_STAGE, DIR_TRASH].includes(entry.leafName)) {
          logger.debug(
            "Ignoring file: name is not a valid add-on ID: ${}",
            entry.path
          );
        }
        continue;
      }

      if (id == entry.leafName && (entry.isFile() || entry.isSymlink())) {
        let newEntry = this._readLinkFile(entry);
        if (!newEntry) {
          logger.debug(`Deleting stale pointer file ${entry.path}`);
          try {
            entry.remove(true);
          } catch (e) {
            logger.warn(`Failed to remove stale pointer file ${entry.path}`, e);
            // Failing to remove the stale pointer file is ignorable
          }
          continue;
        }

        entry = newEntry;
      }

      addons.set(id, entry);
    }
    return addons;
  }

  get isSystem() {
    return this._isSystem;
  }
}

/**
 * An object which identifies a directory install location for system add-ons
 * updates.
 */
class SystemAddonLocation extends DirectoryLocation {
  /**
   * The location consists of a directory which contains the add-ons installed.
   *
   * @param {string} name
   *        The string identifier for the install location.
   * @param {nsIFile} dir
   *        The directory for the install location.
   * @param {integer} scope
   *        The scope of add-ons installed in this location.
   * @param {boolean} resetSet
   *        True to throw away the current add-on set
   */
  constructor(name, dir, scope, resetSet) {
    let addonSet = SystemAddonLocation._loadAddonSet();
    let directory = null;

    // The system add-on update directory is stored in a pref.
    // Therefore, this is looked up before calling the
    // constructor on the superclass.
    if (addonSet.directory) {
      directory = getFile(addonSet.directory, dir);
      logger.info(`SystemAddonLocation scanning directory ${directory.path}`);
    } else {
      logger.info("SystemAddonLocation directory is missing");
    }

    super(name, directory, scope, false);

    this._addonSet = addonSet;
    this._baseDir = dir;

    if (resetSet) {
      this.installer.resetAddonSet();
    }
  }

  makeInstaller() {
    if (this.locked) {
      return null;
    }
    return new XPIExports.XPIInstall.SystemAddonInstaller(this);
  }

  /**
   * Reads the current set of system add-ons
   *
   * @returns {Object}
   */
  static _loadAddonSet() {
    try {
      let setStr = Services.prefs.getStringPref(PREF_SYSTEM_ADDON_SET, null);
      if (setStr) {
        let addonSet = JSON.parse(setStr);
        if (typeof addonSet == "object" && addonSet.schema == 1) {
          return addonSet;
        }
      }
    } catch (e) {
      logger.error("Malformed system add-on set, resetting.");
    }

    return { schema: 1, addons: {} };
  }

  readAddons() {
    // Updated system add-ons are ignored in safe mode
    if (Services.appinfo.inSafeMode) {
      return new Map();
    }

    let addons = super.readAddons();

    // Strip out any unexpected add-ons from the list
    for (let id of addons.keys()) {
      if (!(id in this._addonSet.addons)) {
        addons.delete(id);
      }
    }

    return addons;
  }

  /**
   * Tests whether updated system add-ons are expected.
   *
   * @returns {boolean}
   */
  isActive() {
    return this.dir != null;
  }

  get isSystem() {
    return true;
  }

  get isBuiltin() {
    return true;
  }
}

/**
 * An object that identifies a registry install location for add-ons. The location
 * consists of a registry key which contains string values mapping ID to the
 * path where an add-on is installed
 *
 */
class WinRegLocation extends XPIStateLocation {
  /**
   * @param {string} name
   *        The string identifier for the install location.
   * @param {integer} rootKey
   *        The root key (one of the ROOT_KEY_ values from nsIWindowsRegKey).
   * @param {integer} scope
   *        The scope of add-ons installed in this location.
   */
  constructor(name, rootKey, scope) {
    super(name, undefined, scope);

    this.locked = true;
    this._rootKey = rootKey;
  }

  /**
   * Retrieves the path of this Application's data key in the registry.
   */
  get _appKeyPath() {
    let appVendor = Services.appinfo.vendor;
    let appName = Services.appinfo.name;

    // XXX Thunderbird doesn't specify a vendor string
    if (appVendor == "" && AppConstants.MOZ_APP_NAME == "thunderbird") {
      appVendor = "Mozilla";
    }

    return `SOFTWARE\\${appVendor}\\${appName}`;
  }

  /**
   * Read the registry and build a mapping between ID and path for each
   * installed add-on.
   *
   * @returns {Map<AddonID, nsIFile>}
   *        A map of add-ons in this location.
   */
  readAddons() {
    let addons = new Map();

    let path = `${this._appKeyPath}\\Extensions`;
    let key = Cc["@mozilla.org/windows-registry-key;1"].createInstance(
      Ci.nsIWindowsRegKey
    );

    // Reading the registry may throw an exception, and that's ok.  In error
    // cases, we just leave ourselves in the empty state.
    try {
      key.open(this._rootKey, path, Ci.nsIWindowsRegKey.ACCESS_READ);
    } catch (e) {
      return addons;
    }

    try {
      let count = key.valueCount;
      for (let i = 0; i < count; ++i) {
        let id = key.getValueName(i);
        let file = new nsIFile(key.readStringValue(id));
        if (!file.exists()) {
          logger.warn(`Ignoring missing add-on in ${file.path}`);
          continue;
        }

        addons.set(id, file);
      }
    } finally {
      key.close();
    }

    return addons;
  }
}

/**
 * Keeps track of the state of XPI add-ons on the file system.
 */
var XPIStates = {
  // Map(location-name -> XPIStateLocation)
  db: new Map(),

  _jsonFile: null,

  /**
   * @property {Map<string, XPIState>} sideLoadedAddons
   *        A map of new add-ons detected during install location
   *        directory scans. Keys are add-on IDs, values are XPIState
   *        objects corresponding to those add-ons.
   */
  sideLoadedAddons: new Map(),

  get size() {
    let count = 0;
    for (let location of this.locations()) {
      count += location.size;
    }
    return count;
  },

  /**
   * Load extension state data from addonStartup.json.
   *
   * @returns {Object}
   */
  loadExtensionState() {
    let state;
    try {
      state = lazy.aomStartup.readStartupData();
    } catch (e) {
      logger.warn("Error parsing extensions state: ${error}", { error: e });
    }

    // When upgrading from a build prior to bug 857456, convert startup
    // metadata.
    let done = false;
    for (let location of Object.values(state || {})) {
      for (let data of Object.values(location.addons || {})) {
        if (!migrateAddonLoader(data)) {
          done = true;
          break;
        }
      }
      if (done) {
        break;
      }
    }

    logger.debug("Loaded add-on state: ${}", state);
    return state || {};
  },

  /**
   * Walk through all install locations, highest priority first,
   * comparing the on-disk state of extensions to what is stored in prefs.
   *
   * @param {boolean} [ignoreSideloads = true]
   *        If true, ignore changes in scopes where we don't accept
   *        side-loads.
   *
   * @returns {boolean}
   *        True if anything has changed.
   */
  scanForChanges(ignoreSideloads = true) {
    let oldState = this.initialStateData || this.loadExtensionState();
    // We're called twice, do not restore the second time as new data
    // may have been inserted since the first call.
    let shouldRestoreLocationData = !this.initialStateData;
    this.initialStateData = oldState;

    let changed = false;
    let oldLocations = new Set(Object.keys(oldState));

    let startupScanScopes;
    let buildIdChanged = false;
    if (
      Services.appinfo.appBuildID ==
      Services.prefs.getCharPref(PREF_EM_LAST_APP_BUILD_ID, "")
    ) {
      startupScanScopes = Services.prefs.getIntPref(
        PREF_EM_STARTUP_SCAN_SCOPES,
        0
      );
    } else {
      buildIdChanged = true;
      // If the build id has changed, we need to do a full scan on first startup.
      Services.prefs.setCharPref(
        PREF_EM_LAST_APP_BUILD_ID,
        Services.appinfo.appBuildID
      );
      startupScanScopes = AddonManager.SCOPE_ALL;
    }

    for (let loc of XPIStates.locations()) {
      oldLocations.delete(loc.name);

      if (shouldRestoreLocationData && oldState[loc.name]) {
        loc.restore(oldState[loc.name]);
      }
      changed = changed || loc.changed;

      // Don't bother checking scopes where we don't accept side-loads.
      if (ignoreSideloads && !(loc.scope & startupScanScopes)) {
        continue;
      }

      if (!loc.enumerable) {
        continue;
      }

      const isEnumerableBuiltin = loc.enumerable && loc.isBuiltin;

      // Don't bother scanning scopes where we don't have addons installed if they
      // do not allow sideloading new addons.  Once we have an addon in one of those
      // locations, we need to check the location for changes (updates/deletions).
      if (
        !isEnumerableBuiltin &&
        !loc.size &&
        !(loc.scope & lazy.AddonSettings.SCOPES_SIDELOAD)
      ) {
        continue;
      }

      let knownIds = new Set(loc.keys());

      // readAddons() returns a Map of entries. These entries can be nsIFile or
      // objects with a rel_url string property.
      for (let [id, entry] of loc.readAddons()) {
        knownIds.delete(id);

        let xpiState = loc.get(id);
        if (!xpiState) {
          // If the location is not supported for sideloading, skip new
          // addons.  We handle this here so changes for existing sideloads
          // will function.
          if (
            !loc.isSystem &&
            !(loc.scope & lazy.AddonSettings.SCOPES_SIDELOAD)
          ) {
            continue;
          }
          logger.debug("New add-on ${id} in ${loc}", { id, loc: loc.name });

          changed = true;
          if (entry instanceof Ci.nsIFile) {
            xpiState = loc.addFile(id, entry);
            xpiState.getModTime(xpiState.file);
          } else {
            xpiState = loc._addState(id, {
              enabled: false,
              rootURI: entry.builtin.res_url,
            });
          }
          if (!loc.isSystem) {
            this.sideLoadedAddons.set(id, xpiState);
          }
        } else {
          let addonChanged = false;
          if (entry instanceof Ci.nsIFile) {
            addonChanged =
              xpiState.getModTime(entry) || entry.path != xpiState.path;
            xpiState.file = entry.clone();
          } else {
            addonChanged =
              buildIdChanged ||
              entry.builtin.addon_version != xpiState.version ||
              entry.builtin.res_url != xpiState.rootURI;
            xpiState.version = entry.builtin.addon_version;
            xpiState.rootURI = entry.builtin.res_url;
          }

          if (addonChanged) {
            changed = true;
            logger.debug("Changed add-on ${id} in ${loc}", {
              id,
              loc: loc.name,
            });
          } else {
            logger.debug("Existing add-on ${id} in ${loc}", {
              id,
              loc: loc.name,
            });
          }
        }
        XPIProvider.addTelemetry(id, { location: loc.name });
      }

      // Anything left behind in oldState was removed from the file system.
      for (let id of knownIds) {
        loc.delete(id);
        changed = true;
      }
    }

    // If there's anything left in oldState, an install location that held add-ons
    // was removed from the browser configuration.
    changed = changed || oldLocations.size > 0;

    logger.debug("scanForChanges changed: ${rv}, state: ${state}", {
      rv: changed,
      state: this.db,
    });
    return changed;
  },

  locations() {
    return this.db.values();
  },

  /**
   * @param {string} name
   *        The location name.
   * @param {XPIStateLocation} location
   *        The location object.
   */
  addLocation(name, location) {
    if (this.db.has(name)) {
      throw new Error(`Trying to add duplicate location: ${name}`);
    }
    this.db.set(name, location);
  },

  /**
   * Get the Map of XPI states for a particular location.
   *
   * @param {string} name
   *        The name of the install location.
   *
   * @returns {XPIStateLocation?}
   *        (id -> XPIState) or null if there are no add-ons in the location.
   */
  getLocation(name) {
    return this.db.get(name);
  },

  /**
   * Get the XPI state for a specific add-on in a location.
   * If the state is not in our cache, return null.
   *
   * @param {string} aLocation
   *        The name of the location where the add-on is installed.
   * @param {string} aId
   *        The add-on ID
   *
   * @returns {XPIState?}
   *        The XPIState entry for the add-on, or null.
   */
  getAddon(aLocation, aId) {
    let location = this.db.get(aLocation);
    return location && location.get(aId);
  },

  /**
   * Find the highest priority location of an add-on by ID and return the
   * XPIState.
   * @param {string} aId
   *        The add-on IDa
   * @param {function} aFilter
   *        An optional filter to apply to install locations.  If provided,
   *        addons in locations that do not match the filter are not considered.
   *
   * @returns {XPIState?}
   */
  findAddon(aId, aFilter = () => true) {
    // Fortunately the Map iterator returns in order of insertion, which is
    // also our highest -> lowest priority order.
    for (let location of this.locations()) {
      if (!aFilter(location)) {
        continue;
      }
      if (location.has(aId)) {
        return location.get(aId);
      }
    }
    return undefined;
  },

  /**
   * Iterates over the list of all enabled add-ons in any location.
   */
  *enabledAddons() {
    for (let location of this.locations()) {
      for (let entry of location.values()) {
        if (entry.enabled) {
          yield entry;
        }
      }
    }
  },

  /**
   * Add a new XPIState for an add-on and synchronize it with the DBAddonInternal.
   *
   * @param {DBAddonInternal} aAddon
   *        The add-on to add.
   */
  addAddon(aAddon) {
    aAddon.location.addAddon(aAddon);
  },

  /**
   * Save the current state of installed add-ons.
   */
  save() {
    if (!this._jsonFile) {
      this._jsonFile = new lazy.JSONFile({
        path: PathUtils.join(
          Services.dirsvc.get("ProfD", Ci.nsIFile).path,
          FILE_XPI_STATES
        ),
        finalizeAt: AddonManagerPrivate.finalShutdown,
        compression: "lz4",
      });
      this._jsonFile.data = this;
    }

    this._jsonFile.saveSoon();
  },

  toJSON() {
    let data = {};
    for (let [key, loc] of this.db.entries()) {
      if (!loc.isTemporary && (loc.size || loc.hasStaged)) {
        data[key] = loc;
      }
    }
    return data;
  },

  /**
   * Remove the XPIState for an add-on and save the new state.
   *
   * @param {string} aLocation
   *        The name of the add-on location.
   * @param {string} aId
   *        The ID of the add-on.
   *
   */
  removeAddon(aLocation, aId) {
    logger.debug(`Removing XPIState for ${aLocation}: ${aId}`);
    let location = this.db.get(aLocation);
    if (location) {
      location.removeAddon(aId);
      this.save();
    }
  },

  /**
   * Disable the XPIState for an add-on.
   *
   * @param {string} aId
   *        The ID of the add-on.
   */
  disableAddon(aId) {
    logger.debug(`Disabling XPIState for ${aId}`);
    let state = this.findAddon(aId);
    if (state) {
      state.enabled = false;
    }
  },
};

/**
 * A helper class to manage the lifetime of and interaction with
 * bootstrap scopes for an add-on.
 *
 * @param {Object} addon
 *        The add-on which owns this scope. Should be either an
 *        AddonInternal or XPIState object.
 */
class BootstrapScope {
  constructor(addon) {
    if (!addon.id || !addon.version || !addon.type) {
      throw new Error("Addon must include an id, version, and type");
    }

    this.addon = addon;
    this.instanceID = null;
    this.scope = null;
    this.started = false;
  }

  /**
   * Returns a BootstrapScope object for the given add-on. If an active
   * scope exists, it is returned. Otherwise a new one is created.
   *
   * @param {Object} addon
   *        The add-on which owns this scope, as accepted by the
   *        constructor.
   * @returns {BootstrapScope}
   */
  static get(addon) {
    let scope = XPIProvider.activeAddons.get(addon.id);
    if (!scope) {
      scope = new this(addon);
    }
    return scope;
  }

  get file() {
    return this.addon.file || this.addon._sourceBundle;
  }

  get runInSafeMode() {
    return "runInSafeMode" in this.addon
      ? this.addon.runInSafeMode
      : canRunInSafeMode(this.addon);
  }

  /**
   * Returns state information for use by an AsyncShutdown blocker. If
   * the wrapped bootstrap scope has a fetchState method, it is called,
   * and its result returned. If not, returns null.
   *
   * @returns {Object|null}
   */
  fetchState() {
    if (this.scope && this.scope.fetchState) {
      return this.scope.fetchState();
    }
    return null;
  }

  /**
   * Calls a bootstrap method for an add-on.
   *
   * @param {string} aMethod
   *        The name of the bootstrap method to call
   * @param {integer} aReason
   *        The reason flag to pass to the bootstrap's startup method
   * @param {Object} [aExtraParams = {}]
   *        An object of additional key/value pairs to pass to the method in
   *        the params argument
   * @returns {any}
   *        The return value of the bootstrap method.
   */
  async callBootstrapMethod(aMethod, aReason, aExtraParams = {}) {
    let { addon, runInSafeMode } = this;
    if (
      Services.appinfo.inSafeMode &&
      !runInSafeMode &&
      aMethod !== "uninstall"
    ) {
      return null;
    }

    try {
      if (!this.scope) {
        this.loadBootstrapScope(aReason);
      }

      if (aMethod == "startup" || aMethod == "shutdown") {
        aExtraParams.instanceID = this.instanceID;
      }

      let method = undefined;
      let { scope } = this;
      try {
        method = scope[aMethod];
      } catch (e) {
        // An exception will be caught if the expected method is not defined.
        // That will be logged below.
      }

      if (aMethod == "startup") {
        this.started = true;
      } else if (aMethod == "shutdown") {
        this.started = false;

        // Extensions are automatically deinitialized in the correct order at shutdown.
        if (aReason != BOOTSTRAP_REASONS.APP_SHUTDOWN) {
          this._pendingDisable = true;
          for (let addon of XPIProvider.getDependentAddons(this.addon)) {
            if (addon.active) {
              await XPIExports.XPIDatabase.updateAddonDisabledState(addon);
            }
          }
        }
      }

      // NOTE: Make sure the properties meant to be consistently passed to
      // the bootstrap startup method to be part of the XPIStates JSON_FIELDS
      // and to have been propagated from the db properties stored in the DB
      // to the startupCache XPIStates by the syncWithDB method (because of
      // browser startup the properties for the already installed addons
      // are going to be retrieved from the XPIStates before the addonDB
      // has been fully loaded).
      let params = {
        id: addon.id,
        version: addon.version,
        type: addon.type,
        resourceURI: addon.resolvedRootURI,
        signedState: addon.signedState,
        temporarilyInstalled: addon.location.isTemporary,
        builtIn: addon.location.isBuiltin,
        isSystem: addon.location.isSystem,
        isPrivileged: addon.isPrivileged,
        locationHidden: addon.location.hidden,
        recommendationState: addon.recommendationState,
        blocklistState: addon.blocklistState,
      };

      if (aMethod == "startup" && addon.startupData) {
        params.startupData = addon.startupData;
      }

      Object.assign(params, aExtraParams);

      let result;
      if (!method) {
        logger.warn(
          `Add-on ${addon.id} is missing bootstrap method ${aMethod}`
        );
      } else {
        logger.debug(
          `Calling bootstrap method ${aMethod} on ${addon.id} version ${addon.version}`
        );

        this._beforeCallBootstrapMethod(aMethod, params, aReason);

        try {
          result = await method.call(scope, params, aReason);
        } catch (e) {
          logger.warn(
            `Exception running bootstrap method ${aMethod} on ${addon.id}`,
            e
          );
        }
      }
      return result;
    } finally {
      // Extensions are automatically initialized in the correct order at startup.
      if (aMethod == "startup" && aReason != BOOTSTRAP_REASONS.APP_STARTUP) {
        for (let addon of XPIProvider.getDependentAddons(this.addon)) {
          XPIExports.XPIDatabase.updateAddonDisabledState(addon);
        }
      }
    }
  }

  // No-op method to be overridden by tests.
  _beforeCallBootstrapMethod() {}

  /**
   * Loads a bootstrapped add-on's bootstrap.js into a sandbox and the reason
   * values as constants in the scope.
   *
   * @param {integer?} [aReason]
   *        The reason this bootstrap is being loaded, as passed to a
   *        bootstrap method.
   */
  loadBootstrapScope(aReason) {
    this.instanceID = Symbol(this.addon.id);
    this._pendingDisable = false;

    XPIProvider.activeAddons.set(this.addon.id, this);

    // Mark the add-on as active for the crash reporter before loading.
    // But not at app startup, since we'll already have added all of our
    // annotations before starting any loads.
    if (aReason !== BOOTSTRAP_REASONS.APP_STARTUP) {
      XPIProvider.addAddonsToCrashReporter();
    }

    logger.debug(`Loading bootstrap scope from ${this.addon.rootURI}`);

    if (this.addon.isWebExtension) {
      switch (this.addon.type) {
        case "extension":
        case "theme":
          this.scope = lazy.Extension.getBootstrapScope();
          break;

        case "locale":
          this.scope = lazy.Langpack.getBootstrapScope();
          break;

        case "dictionary":
          this.scope = lazy.Dictionary.getBootstrapScope();
          break;

        default:
          throw new Error(`Unknown webextension type ${this.addon.type}`);
      }
    } else {
      let loader = AddonManagerPrivate.externalExtensionLoaders.get(
        this.addon.loader
      );
      if (!loader) {
        throw new Error(`Cannot find loader for ${this.addon.loader}`);
      }

      this.scope = loader.loadScope(this.addon);
    }
  }

  /**
   * Unloads a bootstrap scope by dropping all references to it and then
   * updating the list of active add-ons with the crash reporter.
   */
  unloadBootstrapScope() {
    XPIProvider.activeAddons.delete(this.addon.id);
    XPIProvider.addAddonsToCrashReporter();

    this.scope = null;
    this.startupPromise = null;
    this.instanceID = null;
  }

  /**
   * Calls the bootstrap scope's startup method, with the given reason
   * and extra parameters.
   *
   * @param {integer} reason
   *        The reason code for the startup call.
   * @param {Object} [aExtraParams]
   *        Optional extra parameters to pass to the bootstrap method.
   * @returns {Promise}
   *        Resolves when the startup method has run to completion, rejects
   *        if called late during shutdown.
   */
  async startup(reason, aExtraParams) {
    if (this.shutdownPromise) {
      await this.shutdownPromise;
    }

    if (
      Services.startup.isInOrBeyondShutdownPhase(
        Ci.nsIAppStartup.SHUTDOWN_PHASE_APPSHUTDOWNCONFIRMED
      )
    ) {
      let err = new Error(
        `XPIProvider can't start bootstrap scope for ${this.addon.id} after shutdown was already granted`
      );
      logger.warn("BoostrapScope startup failure: ${error}", { error: err });
      this.startupPromise = Promise.reject(err);
    } else {
      this.startupPromise = this.callBootstrapMethod(
        "startup",
        reason,
        aExtraParams
      );
    }

    return this.startupPromise;
  }

  /**
   * Calls the bootstrap scope's shutdown method, with the given reason
   * and extra parameters.
   *
   * @param {integer} reason
   *        The reason code for the shutdown call.
   * @param {Object} [aExtraParams]
   *        Optional extra parameters to pass to the bootstrap method.
   */
  async shutdown(reason, aExtraParams) {
    this.shutdownPromise = this._shutdown(reason, aExtraParams);
    await this.shutdownPromise;
    this.shutdownPromise = null;
  }

  async _shutdown(reason, aExtraParams) {
    await this.startupPromise;
    return this.callBootstrapMethod("shutdown", reason, aExtraParams);
  }

  /**
   * If the add-on is already running, calls its "shutdown" method, and
   * unloads its bootstrap scope.
   *
   * @param {integer} reason
   *        The reason code for the shutdown call.
   * @param {Object} [aExtraParams]
   *        Optional extra parameters to pass to the bootstrap method.
   */
  async disable() {
    if (this.started) {
      await this.shutdown(BOOTSTRAP_REASONS.ADDON_DISABLE);
      // If we disable and re-enable very quickly, it's possible that
      // the next startup() method will be called immediately after this
      // shutdown method finishes. This almost never happens outside of
      // tests. In tests, alas...
      if (!this.started) {
        this.unloadBootstrapScope();
      }
    }
  }

  /**
   * Calls the bootstrap scope's install method, and optionally its
   * startup method.
   *
   * @param {integer} reason
   *        The reason code for the calls.
   * @param {boolean} [startup = false]
   *        If true, and the add-on is active, calls its startup method
   *        after its install method.
   * @param {Object} [extraArgs]
   *        Optional extra parameters to pass to the bootstrap method.
   * @returns {Promise}
   *        Resolves when the startup method has run to completion, if
   *        startup is required.
   */
  install(reason = BOOTSTRAP_REASONS.ADDON_INSTALL, startup, extraArgs) {
    return this._install(reason, false, startup, extraArgs);
  }

  async _install(reason, callUpdate, startup, extraArgs) {
    if (callUpdate) {
      await this.callBootstrapMethod("update", reason, extraArgs);
    } else {
      this.callBootstrapMethod("install", reason, extraArgs);
    }

    if (startup && this.addon.active) {
      await this.startup(reason, extraArgs);
    } else if (this.addon.disabled) {
      this.unloadBootstrapScope();
    }
  }

  /**
   * Calls the bootstrap scope's uninstall method, and unloads its
   * bootstrap scope. If the extension is already running, its shutdown
   * method is called before its uninstall method.
   *
   * @param {integer} reason
   *        The reason code for the calls.
   * @param {Object} [extraArgs]
   *        Optional extra parameters to pass to the bootstrap method.
   * @returns {Promise}
   *        Resolves when the shutdown method has run to completion, if
   *        shutdown is required, and the uninstall method has been
   *        called.
   */
  uninstall(reason = BOOTSTRAP_REASONS.ADDON_UNINSTALL, extraArgs) {
    return this._uninstall(reason, false, extraArgs);
  }

  async _uninstall(reason, callUpdate, extraArgs) {
    if (this.started) {
      await this.shutdown(reason, extraArgs);
    }
    if (!callUpdate) {
      this.callBootstrapMethod("uninstall", reason, extraArgs);
    }
    this.unloadBootstrapScope();

    if (this.file) {
      XPIExports.XPIInstall.flushJarCache(this.file);
    }
  }

  /**
   * Calls the appropriate sequence of shutdown, uninstall, update,
   * startup, and install methods for updating the current scope's
   * add-on to the given new add-on, depending on the current state of
   * the scope.
   *
   * @param {XPIState} newAddon
   *        The new add-on which is being installed, as expected by the
   *        constructor.
   * @param {boolean} [startup = false]
   *        If true, and the new add-on is enabled, calls its startup
   *        method as its final operation.
   * @param {function} [updateCallback]
   *        An optional callback function to call between uninstalling
   *        the old add-on and installing the new one. This callback
   *        should update any database state which is necessary for the
   *        startup of the new add-on.
   * @returns {Promise}
   *        Resolves when all required bootstrap callbacks have
   *        completed.
   */
  async update(newAddon, startup = false, updateCallback) {
    let reason = XPIExports.XPIInstall.newVersionReason(
      this.addon.version,
      newAddon.version
    );

    let callUpdate = this.addon.isWebExtension && newAddon.isWebExtension;

    // BootstrapScope gets either an XPIState instance or an AddonInternal
    // instance, when we update, we need the latter to access permissions
    // from the manifest.
    let existingAddon = this.addon;

    let extraArgs = {
      oldVersion: existingAddon.version,
      newVersion: newAddon.version,
    };

    // If we're updating an extension, we may need to read data to
    // calculate permission changes.
    if (callUpdate && existingAddon.type === "extension") {
      if (this.addon instanceof XPIState) {
        // The existing addon will be cached in the database.
        existingAddon = await XPIExports.XPIDatabase.getAddonByID(
          this.addon.id
        );
      }

      if (newAddon instanceof XPIState) {
        newAddon = await XPIExports.XPIInstall.loadManifestFromFile(
          newAddon.file,
          newAddon.location
        );
      }

      Object.assign(extraArgs, {
        userPermissions: newAddon.userPermissions,
        optionalPermissions: newAddon.optionalPermissions,
        oldPermissions: existingAddon.userPermissions,
        oldOptionalPermissions: existingAddon.optionalPermissions,
      });
    }

    await this._uninstall(reason, callUpdate, extraArgs);

    if (updateCallback) {
      await updateCallback();
    }

    this.addon = newAddon;
    return this._install(reason, callUpdate, startup, extraArgs);
  }
}

let resolveDBReady;
let dbReadyPromise = new Promise(resolve => {
  resolveDBReady = resolve;
});
let resolveProviderReady;
let providerReadyPromise = new Promise(resolve => {
  resolveProviderReady = resolve;
});

export var XPIProvider = {
  get name() {
    return "XPIProvider";
  },

  BOOTSTRAP_REASONS: Object.freeze(BOOTSTRAP_REASONS),

  // A Map of active addons to their bootstrapScope by ID
  activeAddons: new Map(),
  // Per-addon telemetry information
  _telemetryDetails: {},
  // Have we started shutting down bootstrap add-ons?
  _closing: false,

  // Promises awaited by the XPIProvider before resolving providerReadyPromise,
  // (pushed into the array by XPIProvider maybeInstallBuiltinAddon and startup
  // methods).
  startupPromises: [],

  // Array of the bootstrap startup promises for the enabled addons being
  // initiated during the XPIProvider startup.
  //
  // NOTE: XPIProvider will wait for these promises (and the startupPromises one)
  // to have settled before allowing the application to proceed with shutting down
  // (see appShutdownConfirmed blocker at the end of the XPIProvider.startup).
  enabledAddonsStartupPromises: [],

  databaseReady: Promise.all([dbReadyPromise, providerReadyPromise]),

  registerProvider() {
    AddonManagerPrivate.registerProvider(this, Array.from(ALL_XPI_TYPES));
  },

  // Check if the XPIDatabase has been loaded (without actually
  // triggering unwanted imports or I/O)
  get isDBLoaded() {
    // Make sure we don't touch the XPIDatabase getter before it's
    // actually loaded, and force an early load.
    return (
      (Object.getOwnPropertyDescriptor(XPIExports, "XPIDatabase").value &&
        XPIExports.XPIDatabase.initialized) ||
      false
    );
  },

  /**
   * Returns true if the add-on with the given ID is currently active,
   * without forcing the add-ons database to load.
   *
   * @param {string} addonId
   *        The ID of the add-on to check.
   * @returns {boolean}
   */
  addonIsActive(addonId) {
    let state = XPIStates.findAddon(addonId);
    return state && state.enabled;
  },

  /**
   * Returns an array of the add-on values in `enabledAddons`,
   * sorted so that all of an add-on's dependencies appear in the array
   * before itself.
   *
   * @returns {Array<object>}
   *   A sorted array of add-on objects. Each value is a copy of the
   *   corresponding value in the `enabledAddons` object, with an
   *   additional `id` property, which corresponds to the key in that
   *   object, which is the same as the add-ons ID.
   */
  sortBootstrappedAddons() {
    function compare(a, b) {
      if (a === b) {
        return 0;
      }
      return a < b ? -1 : 1;
    }

    // Sort the list so that ordering is deterministic.
    let list = Array.from(XPIStates.enabledAddons());
    list.sort((a, b) => compare(a.id, b.id));

    let addons = {};
    for (let entry of list) {
      addons[entry.id] = entry;
    }

    let res = new Set();
    let seen = new Set();

    let add = addon => {
      seen.add(addon.id);

      for (let id of addon.dependencies || []) {
        if (id in addons && !seen.has(id)) {
          add(addons[id]);
        }
      }

      res.add(addon.id);
    };

    Object.values(addons).forEach(add);

    return Array.from(res, id => addons[id]);
  },

  /*
   * Adds metadata to the telemetry payload for the given add-on.
   */
  addTelemetry(aId, aPayload) {
    if (!this._telemetryDetails[aId]) {
      this._telemetryDetails[aId] = {};
    }
    Object.assign(this._telemetryDetails[aId], aPayload);
  },

  setupInstallLocations(aAppChanged) {
    function DirectoryLoc(aName, aScope, aKey, aPaths, aLocked, aIsSystem) {
      try {
        var dir = lazy.FileUtils.getDir(aKey, aPaths);
      } catch (e) {
        return null;
      }
      return new DirectoryLocation(aName, dir, aScope, aLocked, aIsSystem);
    }

    function SystemLoc(aName, aScope, aKey, aPaths) {
      try {
        var dir = lazy.FileUtils.getDir(aKey, aPaths);
      } catch (e) {
        return null;
      }
      return new SystemAddonLocation(aName, dir, aScope, aAppChanged !== false);
    }

    function RegistryLoc(aName, aScope, aKey) {
      if ("nsIWindowsRegKey" in Ci) {
        return new WinRegLocation(aName, Ci.nsIWindowsRegKey[aKey], aScope);
      }
    }

    // These must be in order of priority, highest to lowest,
    // for processFileChanges etc. to work
    let locations = [
      [() => TemporaryInstallLocation, TemporaryInstallLocation.name, null],

      [
        DirectoryLoc,
        KEY_APP_PROFILE,
        AddonManager.SCOPE_PROFILE,
        KEY_PROFILEDIR,
        [DIR_EXTENSIONS],
        false,
      ],

      [
        DirectoryLoc,
        KEY_APP_SYSTEM_PROFILE,
        AddonManager.SCOPE_APPLICATION,
        KEY_PROFILEDIR,
        [DIR_APP_SYSTEM_PROFILE],
        false,
        true,
      ],

      [
        SystemLoc,
        KEY_APP_SYSTEM_ADDONS,
        AddonManager.SCOPE_PROFILE,
        KEY_PROFILEDIR,
        [DIR_SYSTEM_ADDONS],
      ],

      [
        () => SystemBuiltInLocation,
        KEY_APP_SYSTEM_BUILTINS,
        AddonManager.SCOPE_APPLICATION,
      ],

      [() => BuiltInLocation, KEY_APP_BUILTINS, AddonManager.SCOPE_APPLICATION],

      [
        DirectoryLoc,
        KEY_APP_SYSTEM_USER,
        AddonManager.SCOPE_USER,
        "XREUSysExt",
        [Services.appinfo.ID],
        true,
      ],

      [
        RegistryLoc,
        "winreg-app-user",
        AddonManager.SCOPE_USER,
        "ROOT_KEY_CURRENT_USER",
      ],

      [
        DirectoryLoc,
        KEY_APP_GLOBAL,
        AddonManager.SCOPE_APPLICATION,
        KEY_ADDON_APP_DIR,
        [DIR_EXTENSIONS],
        true,
      ],

      [
        DirectoryLoc,
        KEY_APP_SYSTEM_SHARE,
        AddonManager.SCOPE_SYSTEM,
        "XRESysSExtPD",
        [Services.appinfo.ID],
        true,
      ],

      [
        DirectoryLoc,
        KEY_APP_SYSTEM_LOCAL,
        AddonManager.SCOPE_SYSTEM,
        "XRESysLExtPD",
        [Services.appinfo.ID],
        true,
      ],

      [
        RegistryLoc,
        "winreg-app-global",
        AddonManager.SCOPE_SYSTEM,
        "ROOT_KEY_LOCAL_MACHINE",
      ],
    ];

    for (let [constructor, name, scope, ...args] of locations) {
      if (!scope || lazy.enabledScopes & scope) {
        try {
          let loc = constructor(name, scope, ...args);
          if (loc) {
            XPIStates.addLocation(name, loc);
          }
        } catch (e) {
          logger.warn(
            `Failed to add ${constructor.name} install location ${name}`,
            e
          );
        }
      }
    }
  },

  /**
   * Registers the built-in set of dictionaries with the spell check
   * service.
   */
  registerBuiltinDictionaries() {
    this.dictionaries = {};
    for (let [lang, path] of Object.entries(
      this.builtInAddons.dictionaries || {}
    )) {
      path = path.slice(0, -4) + ".aff";
      let uri = Services.io.newURI(`resource://gre/${path}`);

      this.dictionaries[lang] = uri;
      lazy.spellCheck.addDictionary(lang, uri);
    }
  },

  /**
   * Unregisters the dictionaries in the given object, and re-registers
   * any built-in dictionaries in their place, when they exist.
   *
   * @param {Object<nsIURI>} aDicts
   *        An object containing a property with a dictionary language
   *        code and a nsIURI value for each dictionary to be
   *        unregistered.
   */
  unregisterDictionaries(aDicts) {
    let origDicts = lazy.spellCheck.dictionaries.slice();
    let toRemove = [];

    for (let [lang, uri] of Object.entries(aDicts)) {
      if (
        lazy.spellCheck.removeDictionary(lang, uri) &&
        this.dictionaries.hasOwnProperty(lang)
      ) {
        lazy.spellCheck.addDictionary(lang, this.dictionaries[lang]);
      } else {
        toRemove.push(lang);
      }
    }

    lazy.spellCheck.dictionaries = origDicts.filter(
      lang => !toRemove.includes(lang)
    );
  },

  /**
   * Starts the XPI provider initializes the install locations and prefs.
   *
   * @param {boolean?} aAppChanged
   *        A tri-state value. Undefined means the current profile was created
   *        for this session, true means the profile already existed but was
   *        last used with an application with a different version number,
   *        false means that the profile was last used by this version of the
   *        application.
   * @param {string?} [aOldAppVersion]
   *        The version of the application last run with this profile or null
   *        if it is a new profile or the version is unknown
   * @param {string?} [aOldPlatformVersion]
   *        The version of the platform last run with this profile or null
   *        if it is a new profile or the version is unknown
   */
  startup(aAppChanged, aOldAppVersion, aOldPlatformVersion) {
    try {
      AddonManagerPrivate.recordTimestamp("XPI_startup_begin");

      logger.debug("startup");

      this.builtInAddons = {};
      try {
        let url = Services.io.newURI(BUILT_IN_ADDONS_URI);
        let data = Cu.readUTF8URI(url);
        this.builtInAddons = JSON.parse(data);
      } catch (e) {
        if (AppConstants.DEBUG) {
          logger.debug("List of built-in add-ons is missing or invalid.", e);
        }
      }

      this.registerBuiltinDictionaries();

      // Clear this at startup for xpcshell test restarts
      this._telemetryDetails = {};
      // Register our details structure with AddonManager
      AddonManagerPrivate.setTelemetryDetails("XPI", this._telemetryDetails);

      this.setupInstallLocations(aAppChanged);

      if (!AppConstants.MOZ_REQUIRE_SIGNING || Cu.isInAutomation) {
        Services.prefs.addObserver(PREF_XPI_SIGNATURES_REQUIRED, this);
      }
      Services.prefs.addObserver(PREF_LANGPACK_SIGNATURES, this);
      Services.obs.addObserver(this, NOTIFICATION_FLUSH_PERMISSIONS);

      Services.prefs.addObserver(
        PREF_DATA_COLLECTION_PERMISSIONS_ENABLED,
        this
      );

      this.checkForChanges(aAppChanged, aOldAppVersion, aOldPlatformVersion);

      AddonManagerPrivate.markProviderSafe(this);

      const lastTheme = Services.prefs.getCharPref(
        "extensions.activeThemeID",
        null
      );

      if (
        lastTheme === "recommended-1" ||
        lastTheme === "recommended-2" ||
        lastTheme === "recommended-3" ||
        lastTheme === "recommended-4" ||
        lastTheme === "recommended-5"
      ) {
        // The user is using a theme that was once bundled with Firefox, but no longer
        // is. Clear their theme so that they will be forced to reset to the default.
        this.startupPromises.push(
          AddonManagerPrivate.notifyAddonChanged(null, "theme")
        );
      }

      const isInAutomationOrXPCShellTests =
        Cu.isInAutomation || Services.env.exists("XPCSHELL_TEST_PROFILE_DIR");
      if (
        AppConstants.platform != "android" &&
        !(isInAutomationOrXPCShellTests && lazy.skipDefaultThemeInstall)
      ) {
        // Keep version in sync with toolkit/mozapps/extensions/default-theme/manifest.json
        this.maybeInstallBuiltinAddon(
          "default-theme@mozilla.org",
          "1.4.1",
          "resource://default-theme/"
        );
      }

      resolveProviderReady(Promise.all(this.startupPromises));

      if (AppConstants.MOZ_CRASHREPORTER) {
        // Annotate the crash report with relevant add-on information.
        try {
          // The `EMCheckCompatibility` annotation represents a boolean, but
          // we've historically set it as a string so keep doing it for the
          // time being.
          Services.appinfo.annotateCrashReport(
            "EMCheckCompatibility",
            AddonManager.checkCompatibility.toString()
          );
        } catch (e) {}
        this.addAddonsToCrashReporter();
      }

      try {
        AddonManagerPrivate.recordTimestamp("XPI_bootstrap_addons_begin");

        for (let addon of this.sortBootstrappedAddons()) {
          // The startup update check above may have already started some
          // extensions, make sure not to try to start them twice.
          let activeAddon = this.activeAddons.get(addon.id);
          if (activeAddon && activeAddon.started) {
            continue;
          }
          try {
            let reason = BOOTSTRAP_REASONS.APP_STARTUP;
            // Eventually set INSTALLED reason when a bootstrap addon
            // is dropped in profile folder and automatically installed
            if (
              AddonManager.getStartupChanges(
                AddonManager.STARTUP_CHANGE_INSTALLED
              ).includes(addon.id)
            ) {
              reason = BOOTSTRAP_REASONS.ADDON_INSTALL;
            } else if (
              AddonManager.getStartupChanges(
                AddonManager.STARTUP_CHANGE_ENABLED
              ).includes(addon.id)
            ) {
              reason = BOOTSTRAP_REASONS.ADDON_ENABLE;
            }
            this.enabledAddonsStartupPromises.push(
              BootstrapScope.get(addon).startup(reason)
            );
          } catch (e) {
            logger.error(
              "Failed to load bootstrap addon " +
                addon.id +
                " from " +
                addon.descriptor,
              e
            );
          }
        }
        AddonManagerPrivate.recordTimestamp("XPI_bootstrap_addons_end");
      } catch (e) {
        logger.error("bootstrap startup failed", e);
        AddonManagerPrivate.recordException(
          "XPI-BOOTSTRAP",
          "startup failed",
          e
        );
      }

      // Let these shutdown a little earlier when they still have access to most
      // of XPCOM
      lazy.AsyncShutdown.appShutdownConfirmed.addBlocker(
        "XPIProvider shutdown",
        async () => {
          // Do not enter shutdown before we actually finished starting as this
          // can lead to hangs as seen in bug 1814104.
          await Promise.allSettled([
            ...this.startupPromises,
            ...this.enabledAddonsStartupPromises,
          ]);

          XPIProvider._closing = true;

          await XPIProvider.cleanupTemporaryAddons();
          for (let addon of XPIProvider.sortBootstrappedAddons().reverse()) {
            // If no scope has been loaded for this add-on then there is no need
            // to shut it down (should only happen when a bootstrapped add-on is
            // pending enable)
            let activeAddon = XPIProvider.activeAddons.get(addon.id);
            if (!activeAddon || !activeAddon.started) {
              continue;
            }

            // If the add-on was pending disable then shut it down and remove it
            // from the persisted data.
            let reason = BOOTSTRAP_REASONS.APP_SHUTDOWN;
            if (addon._pendingDisable) {
              reason = BOOTSTRAP_REASONS.ADDON_DISABLE;
            } else if (addon.location.name == KEY_APP_TEMPORARY) {
              reason = BOOTSTRAP_REASONS.ADDON_UNINSTALL;
              let existing = XPIStates.findAddon(
                addon.id,
                loc => !loc.isTemporary
              );
              if (existing) {
                reason = XPIExports.XPIInstall.newVersionReason(
                  addon.version,
                  existing.version
                );
              }
            }

            let scope = BootstrapScope.get(addon);
            let promise = scope.shutdown(reason);
            lazy.AsyncShutdown.profileChangeTeardown.addBlocker(
              `Extension shutdown: ${addon.id}`,
              promise,
              {
                fetchState: scope.fetchState.bind(scope),
              }
            );
          }
        }
      );

      // Detect final-ui-startup for telemetry reporting
      Services.obs.addObserver(function observer() {
        AddonManagerPrivate.recordTimestamp("XPI_finalUIStartup");
        Services.obs.removeObserver(observer, "final-ui-startup");
      }, "final-ui-startup");

      // If we haven't yet loaded the XPI database, schedule loading it
      // to occur once other important startup work is finished.  We want
      // this to happen relatively quickly after startup so the telemetry
      // environment has complete addon information.
      //
      // Unfortunately we have to use a variety of ways do detect when it
      // is time to load.  In a regular browser process we just wait for
      // sessionstore-windows-restored.  In a browser toolbox process
      // we wait for the toolbox to show up, based on xul-window-visible
      // and a visible toolbox window.
      //
      // TelemetryEnvironment's EnvironmentAddonBuilder awaits databaseReady
      // before releasing a blocker on AddonManager.beforeShutdown, which in its
      // turn is a blocker of a shutdown blocker at "profile-before-change".
      // To avoid a deadlock, trigger the DB load at "profile-before-change" if
      // the database hasn't started loading yet.
      //
      // Finally, we have a test-only event called test-load-xpi-database
      // as a temporary workaround for bug 1372845.  The latter can be
      // cleaned up when that bug is resolved.
      if (!this.isDBLoaded) {
        const EVENTS = [
          "sessionstore-windows-restored",
          "xul-window-visible",
          "profile-before-change",
          "test-load-xpi-database",
        ];
        let observer = (subject, topic) => {
          if (
            topic == "xul-window-visible" &&
            !Services.wm.getMostRecentWindow("devtools:toolbox")
          ) {
            return;
          }

          for (let event of EVENTS) {
            Services.obs.removeObserver(observer, event);
          }

          XPIExports.XPIDatabase.asyncLoadDB();
        };
        for (let event of EVENTS) {
          Services.obs.addObserver(observer, event);
        }
      }

      AddonManagerPrivate.recordTimestamp("XPI_startup_end");

      if (
        Services.prefs.getIntPref(PREF_LAST_SIGNATURE_CHECKPOINT, 0) !==
        XPI_SIGNATURE_CHECKPOINT
      ) {
        Services.prefs.setIntPref(
          PREF_LAST_SIGNATURE_CHECKPOINT,
          XPI_SIGNATURE_CHECKPOINT
        );
        if (aAppChanged !== undefined) {
          XPIExports.XPIDatabase.verifySignatures();

          // Mark timer as fired so that timerManager won't also retrigger the
          // same validation for the next XPI_SIGNATURE_CHECKPOINT seconds.
          const NOW_SECS = Math.round(Date.now() / 1000);
          Services.prefs.setIntPref(PREF_LAST_SIGNATURE_CHECK_TIME, NOW_SECS);
        }
      }
      lazy.timerManager.registerTimer(
        "xpi-signature-verification",
        () => {
          XPIExports.XPIDatabase.verifySignatures();
        },
        XPI_SIGNATURE_CHECK_PERIOD
      );
    } catch (e) {
      logger.error("startup failed", e);
      AddonManagerPrivate.recordException("XPI", "startup failed", e);
    }
  },

  /**
   * Shuts down the database and releases all references.
   * Return: Promise{integer} resolves / rejects with the result of
   *                          flushing the XPI Database if it was loaded,
   *                          0 otherwise.
   */
  async shutdown() {
    logger.debug("shutdown");

    this.activeAddons.clear();
    this.allAppGlobal = true;

    // Stop anything we were doing asynchronously
    XPIExports.XPIInstall.cancelAll();

    Services.prefs.removeObserver(
      PREF_DATA_COLLECTION_PERMISSIONS_ENABLED,
      this
    );

    for (let install of XPIExports.XPIInstall.installs) {
      if (install.onShutdown()) {
        install.onShutdown();
      }
    }

    // If there are pending operations then we must update the list of active
    // add-ons
    if (Services.prefs.getBoolPref(PREF_PENDING_OPERATIONS, false)) {
      XPIExports.XPIDatabase.updateActiveAddons();
      Services.prefs.setBoolPref(PREF_PENDING_OPERATIONS, false);
    }

    await XPIExports.XPIDatabase.shutdown();
  },

  cleanupTemporaryAddons() {
    let promises = [];
    let tempLocation = TemporaryInstallLocation;
    for (let [id, addon] of tempLocation.entries()) {
      tempLocation.delete(id);

      let bootstrap = BootstrapScope.get(addon);
      let existing = XPIStates.findAddon(id, loc => !loc.isTemporary);

      let cleanup = () => {
        tempLocation.installer.uninstallAddon(id);
        tempLocation.removeAddon(id);
      };

      let promise;
      if (existing) {
        promise = bootstrap.update(existing, false, () => {
          cleanup();
          XPIExports.XPIDatabase.makeAddonLocationVisible(
            id,
            existing.location
          );
        });
      } else {
        promise = bootstrap.uninstall().then(cleanup);
      }
      lazy.AsyncShutdown.profileChangeTeardown.addBlocker(
        `Temporary extension shutdown: ${addon.id}`,
        promise
      );
      promises.push(promise);
    }
    return Promise.all(promises);
  },

  /**
   * Adds a list of currently active add-ons to the next crash report.
   */
  addAddonsToCrashReporter() {
    void (Services.appinfo instanceof Ci.nsICrashReporter);
    if (!Services.appinfo.annotateCrashReport || Services.appinfo.inSafeMode) {
      return;
    }

    let data = Array.from(XPIStates.enabledAddons(), a => a.telemetryKey).join(
      ","
    );

    try {
      Services.appinfo.annotateCrashReport("Add-ons", data);
    } catch (e) {}

    lazy.TelemetrySession.setAddOns(data);
  },

  /**
   * Check the staging directories of install locations for any add-ons to be
   * installed or add-ons to be uninstalled.
   *
   * @param {Object} aManifests
   *         A dictionary to add detected install manifests to for the purpose
   *         of passing through updated compatibility information
   * @returns {boolean}
   *        True if an add-on was installed or uninstalled
   */
  processPendingFileChanges(aManifests) {
    let changed = false;
    for (let loc of XPIStates.locations()) {
      aManifests[loc.name] = {};
      // We can't install or uninstall anything in locked locations
      if (loc.locked) {
        continue;
      }

      // Collect any install errors for specific removal from the staged directory
      // during cleanStagingDir.  Successful installs remove the files.
      let stagedFailureNames = [];
      let promises = [];
      for (let [id, metadata] of loc.getStagedAddons()) {
        loc.unstageAddon(id);

        aManifests[loc.name][id] = null;
        promises.push(
          XPIExports.XPIInstall.installStagedAddon(id, metadata, loc).then(
            addon => {
              aManifests[loc.name][id] = addon;
            },
            error => {
              delete aManifests[loc.name][id];
              stagedFailureNames.push(`${id}.xpi`);

              logger.error(
                `Failed to install staged add-on ${id} in ${loc.name}`,
                error
              );
            }
          )
        );
      }

      if (promises.length) {
        changed = true;
        awaitPromise(Promise.all(promises));
      }

      try {
        if (changed || stagedFailureNames.length) {
          loc.installer.cleanStagingDir(stagedFailureNames);
        }
      } catch (e) {
        // Non-critical, just saves some perf on startup if we clean this up.
        logger.debug("Error cleaning staging dir", e);
      }
    }
    return changed;
  },

  /**
   * Installs any add-ons located in the extensions directory of the
   * application's distribution specific directory into the profile unless a
   * newer version already exists or the user has previously uninstalled the
   * distributed add-on.
   *
   * @param {Object} aManifests
   *        A dictionary to add new install manifests to to save having to
   *        reload them later
   * @param {string} [aAppChanged]
   *        See checkForChanges
   * @param {string?} [aOldAppVersion]
   *        The version of the application last run with this profile or null
   *        if it is a new profile or the version is unknown
   * @returns {boolean}
   *        True if any new add-ons were installed
   */
  installDistributionAddons(aManifests, aAppChanged, aOldAppVersion) {
    let distroDirs = [];
    try {
      distroDirs.push(
        lazy.FileUtils.getDir(KEY_APP_DISTRIBUTION, [DIR_EXTENSIONS])
      );
    } catch (e) {
      return false;
    }

    let availableLocales = [];
    for (let file of iterDirectory(distroDirs[0])) {
      if (file.isDirectory() && file.leafName.startsWith("locale-")) {
        availableLocales.push(file.leafName.replace("locale-", ""));
      }
    }

    let locales = Services.locale.negotiateLanguages(
      Services.locale.requestedLocales,
      availableLocales,
      undefined,
      Services.locale.langNegStrategyMatching
    );

    // Also install addons from subdirectories that correspond to the requested
    // locales. This allows for installing language packs and dictionaries.
    for (let locale of locales) {
      let langPackDir = distroDirs[0].clone();
      langPackDir.append(`locale-${locale}`);
      distroDirs.push(langPackDir);
    }

    let changed = false;
    for (let distroDir of distroDirs) {
      logger.warn(`Checking ${distroDir.path} for addons`);
      for (let file of iterDirectory(distroDir)) {
        if (!isXPI(file.leafName, true)) {
          // Only warn for files, not directories
          if (!file.isDirectory()) {
            logger.warn(`Ignoring distribution: not an XPI: ${file.path}`);
          }
          continue;
        }

        let id = getExpectedID(file);
        if (!id) {
          logger.warn(
            `Ignoring distribution: name is not a valid add-on ID: ${file.path}`
          );
          continue;
        }

        /* If this is not an upgrade and we've already handled this extension
         * just continue */
        if (
          !aAppChanged &&
          Services.prefs.prefHasUserValue(PREF_BRANCH_INSTALLED_ADDON + id)
        ) {
          continue;
        }

        try {
          let loc = XPIStates.getLocation(KEY_APP_PROFILE);
          let addon = awaitPromise(
            XPIExports.XPIInstall.installDistributionAddon(
              id,
              file,
              loc,
              aOldAppVersion
            )
          );

          if (addon) {
            // aManifests may contain a copy of a newly installed add-on's manifest
            // and we'll have overwritten that so instead cache our install manifest
            // which will later be put into the database in processFileChanges
            if (!(loc.name in aManifests)) {
              aManifests[loc.name] = {};
            }
            aManifests[loc.name][id] = addon;
            changed = true;
          }
        } catch (e) {
          logger.error(`Failed to install distribution add-on ${file.path}`, e);
        }
      }
    }

    return changed;
  },

  /**
   * Like `installBuiltinAddon`, but only installs the addon at `aBase`
   * if an existing built-in addon with the ID `aID` and version doesn't
   * already exist.
   *
   * @param {string} aID
   *        The ID of the add-on being registered.
   * @param {string} aVersion
   *        The version of the add-on being registered.
   * @param {string} aBase
   *        A string containing the base URL.  Must be a resource: URL.
   * @returns {Promise<Addon>} a Promise that resolves when the addon is installed.
   */
  async maybeInstallBuiltinAddon(aID, aVersion, aBase) {
    let installed;
    if (lazy.enabledScopes & BuiltInLocation.scope) {
      let existing = BuiltInLocation.get(aID);
      if (!existing || existing.version != aVersion) {
        installed = this.installBuiltinAddon(aBase);
        this.startupPromises.push(installed);
      }
    }
    return installed;
  },

  getDependentAddons(aAddon) {
    return Array.from(XPIExports.XPIDatabase.getAddons()).filter(addon =>
      addon.dependencies.includes(aAddon.id)
    );
  },

  /**
   * Checks for any changes that have occurred since the last time the
   * application was launched.
   *
   * @param {boolean?} [aAppChanged]
   *        A tri-state value. Undefined means the current profile was created
   *        for this session, true means the profile already existed but was
   *        last used with an application with a different version number,
   *        false means that the profile was last used by this version of the
   *        application.
   * @param {string?} [aOldAppVersion]
   *        The version of the application last run with this profile or null
   *        if it is a new profile or the version is unknown
   * @param {string?} [aOldPlatformVersion]
   *        The version of the platform last run with this profile or null
   *        if it is a new profile or the version is unknown
   */
  checkForChanges(aAppChanged, aOldAppVersion, aOldPlatformVersion) {
    logger.debug("checkForChanges");

    // Keep track of whether and why we need to open and update the database at
    // startup time.
    let updateReasons = [];
    if (aAppChanged) {
      updateReasons.push("appChanged");
    }

    let installChanged = XPIStates.scanForChanges(aAppChanged === false);
    if (installChanged) {
      updateReasons.push("directoryState");
    }

    // First install any new add-ons into the locations, if there are any
    // changes then we must update the database with the information in the
    // install locations
    let manifests = {};
    let updated = this.processPendingFileChanges(manifests);
    if (updated) {
      updateReasons.push("pendingFileChanges");
    }

    // This will be true if the previous session made changes that affect the
    // active state of add-ons but didn't commit them properly (normally due
    // to the application crashing)
    let hasPendingChanges = Services.prefs.getBoolPref(
      PREF_PENDING_OPERATIONS,
      false
    );
    if (hasPendingChanges) {
      updateReasons.push("hasPendingChanges");
    }

    // If the application has changed then check for new distribution add-ons
    if (Services.prefs.getBoolPref(PREF_INSTALL_DISTRO_ADDONS, true)) {
      updated = this.installDistributionAddons(
        manifests,
        aAppChanged,
        aOldAppVersion
      );
      if (updated) {
        updateReasons.push("installDistributionAddons");
      }
    }

    // If the schema appears to have changed then we should update the database
    if (DB_SCHEMA != Services.prefs.getIntPref(PREF_DB_SCHEMA, 0)) {
      // If we don't have any add-ons, just update the pref, since we don't need to
      // write the database
      if (!XPIStates.size) {
        logger.debug(
          "Empty XPI database, setting schema version preference to " +
            DB_SCHEMA
        );
        Services.prefs.setIntPref(PREF_DB_SCHEMA, DB_SCHEMA);
      } else {
        updateReasons.push("schemaChanged");
      }
    }

    // Catch and log any errors during the main startup
    try {
      let extensionListChanged = false;
      // If the database needs to be updated then open it and then update it
      // from the filesystem
      if (updateReasons.length) {
        AddonManagerPrivate.recordSimpleMeasure(
          "XPIDB_startup_load_reasons",
          updateReasons
        );
        XPIExports.XPIDatabase.syncLoadDB(false);
        try {
          extensionListChanged =
            XPIExports.XPIDatabaseReconcile.processFileChanges(
              manifests,
              aAppChanged,
              aOldAppVersion,
              aOldPlatformVersion,
              updateReasons.includes("schemaChanged")
            );
        } catch (e) {
          logger.error("Failed to process extension changes at startup", e);
        }
      }

      // If the application crashed before completing any pending operations then
      // we should perform them now.
      if (extensionListChanged || hasPendingChanges) {
        XPIExports.XPIDatabase.updateActiveAddons();
        return;
      }

      logger.debug("No changes found");
    } catch (e) {
      logger.error("Error during startup file checks", e);
    }
  },

  /**
   * Gets an array of add-ons which were placed in a known install location
   * prior to startup of the current session, were detected by a directory scan
   * of those locations, and are currently disabled.
   *
   * @returns {Promise<Array<Addon>>}
   */
  async getNewSideloads() {
    if (XPIStates.scanForChanges(false)) {
      // We detected changes. Update the database to account for them.
      await this._updateDatabase({ aSchemaChange: false });
    }

    let addons = await Promise.all(
      Array.from(XPIStates.sideLoadedAddons.keys(), id => this.getAddonByID(id))
    );

    return addons.filter(
      addon =>
        addon &&
        addon.seen === false &&
        addon.permissions & AddonManager.PERM_CAN_ENABLE
    );
  },

  /**
   * Called to test whether this provider supports installing a particular
   * mimetype.
   *
   * @param {string} aMimetype
   *        The mimetype to check for
   * @returns {boolean}
   *        True if the mimetype is application/x-xpinstall
   */
  supportsMimetype(aMimetype) {
    return aMimetype == "application/x-xpinstall";
  },

  // Identify temporary install IDs.
  isTemporaryInstallID(id) {
    return id.endsWith(TEMPORARY_ADDON_SUFFIX);
  },

  /**
   * Sets startupData for the given addon.  The provided data will be stored
   * in addonsStartup.json so it is available early during browser startup.
   * Note that this file is read synchronously at startup, so startupData
   * should be used with care.
   *
   * @param {string} aID
   *         The id of the addon to save startup data for.
   * @param {any} aData
   *        The data to store.  Must be JSON serializable.
   */
  setStartupData(aID, aData) {
    let state = XPIStates.findAddon(aID);
    state.startupData = aData;
    XPIStates.save();
  },

  /**
   * Persists some startupData into an addon if it is available in the current
   * XPIState for the addon id.
   *
   * @param {AddonInternal} addon An addon to receive the startup data, typically an update that is occuring.
   * @param {XPIState} state optional
   */
  persistStartupData(addon, state) {
    if (!addon.startupData) {
      state = state || XPIStates.findAddon(addon.id);
      if (state?.enabled) {
        // Save persistent listener data if available.  It will be
        // removed later if necessary.
        let persistentListeners = state.startupData?.persistentListeners;
        addon.startupData = { persistentListeners };
      }
    }
  },

  getAddonIDByInstanceID(aInstanceID) {
    if (!aInstanceID || typeof aInstanceID != "symbol") {
      throw Components.Exception(
        "aInstanceID must be a Symbol()",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    for (let [id, val] of this.activeAddons) {
      if (aInstanceID == val.instanceID) {
        return id;
      }
    }

    return null;
  },

  async getAddonsByTypes(aTypes) {
    if (aTypes && !aTypes.some(type => ALL_XPI_TYPES.has(type))) {
      return [];
    }
    return XPIExports.XPIDatabase.getAddonsByTypes(aTypes);
  },

  /**
   * Called to get active Addons of a particular type
   *
   * @param {Array<string>?} aTypes
   *        An array of types to fetch. Can be null to get all types.
   * @returns {Promise<Array<Addon>>}
   */
  async getActiveAddons(aTypes) {
    // If we already have the database loaded, returning full info is fast.
    if (this.isDBLoaded) {
      let addons = await this.getAddonsByTypes(aTypes);
      return {
        addons: addons.filter(addon => addon.isActive),
        fullData: true,
      };
    }

    let result = [];
    for (let addon of XPIStates.enabledAddons()) {
      if (aTypes && !aTypes.includes(addon.type)) {
        continue;
      }
      let { scope, isSystem } = addon.location;
      result.push({
        id: addon.id,
        version: addon.version,
        type: addon.type,
        updateDate: addon.lastModifiedTime,
        scope,
        isSystem,
        isWebExtension: addon.isWebExtension,
      });
    }

    return { addons: result, fullData: false };
  },

  shouldShowBlocklistAttention() {
    return XPIExports.XPIDatabase.shouldShowBlocklistAttention();
  },

  getBlocklistAttentionInfo() {
    return XPIExports.XPIDatabase.getBlocklistAttentionInfo();
  },

  /*
   * Notified when a preference we're interested in has changed.
   *
   * @see nsIObserver
   */
  observe(aSubject, aTopic, aData) {
    switch (aTopic) {
      case NOTIFICATION_FLUSH_PERMISSIONS:
        if (!aData || aData == XPI_PERMISSION) {
          XPIExports.XPIDatabase.importPermissions();
        }
        break;

      case "nsPref:changed":
        switch (aData) {
          case PREF_XPI_SIGNATURES_REQUIRED:
          case PREF_LANGPACK_SIGNATURES:
            XPIExports.XPIDatabase.updateAddonAppDisabledStates();
            break;

          case PREF_DATA_COLLECTION_PERMISSIONS_ENABLED:
            // When this pref is enabled, we need to update the DB. It is fine
            // to only do this when the pref is enabled because the UI and APIs
            // are backward compatible.
            if (
              Services.prefs.getBoolPref(
                PREF_DATA_COLLECTION_PERMISSIONS_ENABLED,
                false
              )
            ) {
              this._updateDatabase({ aSchemaChange: true });
            }
            break;
        }
    }
  },

  uninstallSystemProfileAddon(aID) {
    let location = XPIStates.getLocation(KEY_APP_SYSTEM_PROFILE);
    return XPIExports.XPIInstall.uninstallAddonFromLocation(aID, location);
  },

  async _updateDatabase({ aSchemaChange }) {
    await XPIExports.XPIDatabase.asyncLoadDB(false);
    XPIExports.XPIDatabaseReconcile.processFileChanges(
      /* aManifests */ {},
      /* aAppChanged */ false,
      /* aOldAppVersion, */ false,
      /* aOldPlatformVersion */ false,
      aSchemaChange
    );
    XPIExports.XPIDatabase.updateActiveAddons();
    Services.obs.notifyObservers(null, "xpi-provider:database-updated");
  },
};

for (let meth of [
  "getInstallForFile",
  "getInstallForURL",
  "getInstallsByTypes",
  "installTemporaryAddon",
  "installBuiltinAddon",
  "isInstallAllowed",
  "isInstallEnabled",
  "updateSystemAddons",
  "stageLangpacksForAppUpdate",
]) {
  XPIProvider[meth] = function () {
    return XPIExports.XPIInstall[meth](...arguments);
  };
}

for (let meth of [
  "addonChanged",
  "getAddonByID",
  "getAddonBySyncGUID",
  "updateAddonRepositoryData",
  "updateAddonAppDisabledStates",
]) {
  XPIProvider[meth] = function () {
    return XPIExports.XPIDatabase[meth](...arguments);
  };
}

export var XPIInternal = {
  BOOTSTRAP_REASONS,
  BootstrapScope,
  BuiltInLocation,
  DB_SCHEMA,
  DIR_STAGE,
  DIR_TRASH,
  KEY_APP_BUILTINS,
  KEY_APP_PROFILE,
  KEY_APP_SYSTEM_ADDONS,
  KEY_APP_SYSTEM_BUILTINS,
  KEY_APP_SYSTEM_PROFILE,
  PREF_BRANCH_INSTALLED_ADDON,
  PREF_SYSTEM_ADDON_SET,
  SystemAddonLocation,
  TEMPORARY_ADDON_SUFFIX,
  TemporaryInstallLocation,
  XPIStates,
  XPI_PERMISSION,
  awaitPromise,
  canRunInSafeMode,
  getURIForResourceInFile,
  isXPI,
  iterDirectory,
  maybeResolveURI,
  migrateAddonLoader,
  resolveDBReady,

  // Used by tests to shut down AddonManager.
  overrideAsyncShutdown(mockAsyncShutdown) {
    lazy.AsyncShutdown = mockAsyncShutdown;
  },
};
