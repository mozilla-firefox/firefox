/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * This file is the main entry point for extensions. When an extension
 * loads, its bootstrap.js file creates a Extension instance
 * and calls .startup() on it. It calls .shutdown() when the extension
 * unloads. Extension manages any extension-specific state in
 * the chrome process.
 *
 * TODO(rpl): we are current restricting the extensions to a single process
 * (set as the current default value of the "dom.ipc.processCount.extension"
 * preference), if we switch to use more than one extension process, we have to
 * be sure that all the browser's frameLoader are associated to the same process,
 * e.g. by enabling the `maychangeremoteness` attribute, and/or setting
 * `initialBrowsingContextGroupId` attribute to the correct value.
 *
 * At that point we are going to keep track of the existing browsers associated to
 * a webextension to ensure that they are all running in the same process (and we
 * are also going to do the same with the browser element provided to the
 * addon debugging Remote Debugging actor, e.g. because the addon has been
 * reloaded by the user, we have to  ensure that the new extension pages are going
 * to run in the same process of the existing addon debugging browser element).
 */
/* eslint-disable mozilla/valid-lazy */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { ExtensionCommon } from "resource://gre/modules/ExtensionCommon.sys.mjs";
import { ExtensionParent } from "resource://gre/modules/ExtensionParent.sys.mjs";
import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";
import { Log } from "resource://gre/modules/Log.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AddonManagerPrivate: "resource://gre/modules/AddonManager.sys.mjs",
  AddonSettings: "resource://gre/modules/addons/AddonSettings.sys.mjs",
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  ExtensionDNR: "resource://gre/modules/ExtensionDNR.sys.mjs",
  ExtensionDNRStore: "resource://gre/modules/ExtensionDNRStore.sys.mjs",
  ExtensionMenus: "resource://gre/modules/ExtensionMenus.sys.mjs",
  ExtensionPermissions: "resource://gre/modules/ExtensionPermissions.sys.mjs",
  ExtensionPreferencesManager:
    "resource://gre/modules/ExtensionPreferencesManager.sys.mjs",
  ExtensionProcessScript:
    "resource://gre/modules/ExtensionProcessScript.sys.mjs",
  ExtensionScriptingStore:
    "resource://gre/modules/ExtensionScriptingStore.sys.mjs",
  ExtensionStorage: "resource://gre/modules/ExtensionStorage.sys.mjs",
  ExtensionStorageIDB: "resource://gre/modules/ExtensionStorageIDB.sys.mjs",
  ExtensionUserScripts: "resource://gre/modules/ExtensionUserScripts.sys.mjs",
  ExtensionTelemetry: "resource://gre/modules/ExtensionTelemetry.sys.mjs",
  LightweightThemeManager:
    "resource://gre/modules/LightweightThemeManager.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  SITEPERMS_ADDON_TYPE:
    "resource://gre/modules/addons/siteperms-addon-utils.sys.mjs",
  Schemas: "resource://gre/modules/Schemas.sys.mjs",
  ServiceWorkerCleanUp: "resource://gre/modules/ServiceWorkerCleanUp.sys.mjs",
  extensionStorageSync: "resource://gre/modules/ExtensionStorageSync.sys.mjs",
  PERMISSION_L10N: "resource://gre/modules/ExtensionPermissionMessages.sys.mjs",
  permissionToL10nId:
    "resource://gre/modules/ExtensionPermissionMessages.sys.mjs",
  QuarantinedDomains: "resource://gre/modules/ExtensionPermissions.sys.mjs",

  resourceProtocol: () =>
    Services.io
      .getProtocolHandler("resource")
      .QueryInterface(Ci.nsIResProtocolHandler),

  aomStartup: {
    service: "@mozilla.org/addons/addon-manager-startup;1",
    iid: Ci.amIAddonManagerStartup,
  },
  spellCheck: {
    service: "@mozilla.org/spellchecker/engine;1",
    iid: Ci.mozISpellCheckingEngine,
  },

  processCount: { pref: "dom.ipc.processCount.extension", default: 1 },

  userContextIsolation: {
    pref: "extensions.userContextIsolation.enabled",
    default: false,
  },
  userContextIsolationDefaultRestricted: {
    pref: "extensions.userContextIsolation.defaults.restricted",
    default: "[]",
  },

  dnrEnabled: { pref: "extensions.dnr.enabled", default: true },

  // All functionality is gated by the "userScripts" permission, and forgetting
  // about its existence is enough to hide all userScripts functionality.
  // MV3 userScripts API in development (bug 1875475), off by default.
  // Not to be confused with MV2 and extensions.webextensions.userScripts.enabled!
  userScriptsMV3Enabled: {
    pref: "extensions.userScripts.mv3.enabled",
    default: false,
  },

  // This pref modifies behavior for MV2.  MV3 is enabled regardless.
  eventPagesEnabled: { pref: "extensions.eventPages.enabled", default: true },

  // This pref is used to check if storage.sync is still the Kinto-based backend
  // (GeckoView should be the only one still using it).
  storageSyncOldKintoBackend: {
    pref: "webextensions.storage.sync.kinto",
    default: true,
  },

  // Deprecation of browser_style, through .supported & .same_as_mv2 prefs:
  // - true true  = warn only: deprecation message only (no behavioral changes).
  // - true false = deprecate: default to false, even if default was true in MV2.
  // - false      = remove: always use false, even when true is specified.
  //                (if .same_as_mv2 is set, also warn if the default changed)
  // Deprecation plan: https://bugzilla.mozilla.org/show_bug.cgi?id=1827910#c1
  browserStyleMV3supported: {
    pref: "extensions.browser_style_mv3.supported",
    default: false,
  },
  browserStyleMV3sameAsMV2: {
    pref: "extensions.browser_style_mv3.same_as_mv2",
    default: false,
  },

  // The default number of times an extension process is allowed to crash
  // within a timeframe.
  processCrashThreshold: {
    pref: "extensions.webextensions.crash.threshold",
    default: 5,
  },
  // The default timeframe used to count crashes, in milliseconds.
  processCrashTimeframe: {
    pref: "extensions.webextensions.crash.timeframe",
    default: 30 * 1000,
  },

  installIncludesOrigins: {
    pref: "extensions.originControls.grantByDefault",
    default: false,
  },

  async NO_PROMPT_PERMISSIONS() {
    // Wait until all extension API schemas have been loaded and parsed.
    await Management.lazyInit();
    return new Set(
      lazy.Schemas.getPermissionNames([
        "PermissionNoPrompt",
        "OptionalPermissionNoPrompt",
        "PermissionPrivileged",
      ])
    );
  },

  dataCollectionPermissionsEnabled: {
    pref: "extensions.dataCollectionPermissions.enabled",
    default: false,
  },
});

var {
  GlobalManager,
  IconDetails,
  ParentAPIManager,
  StartupCache,
  apiManager: Management,
} = ExtensionParent;

export { Management };

const { getUniqueId, promiseTimeout } = ExtensionUtils;
const { EventEmitter, redefineGetter, updateAllowedOrigins } = ExtensionCommon;
const { sharedData } = Services.ppmm;

const PRIVATE_ALLOWED_PERMISSION = "internal:privateBrowsingAllowed";
const SVG_CONTEXT_PROPERTIES_PERMISSION =
  "internal:svgContextPropertiesAllowed";

// The userContextID reserved for the extension storage (its purpose is ensuring that the IndexedDB
// storage used by the browser.storage.local API is not directly accessible from the extension code,
// it is defined and reserved as "userContextIdInternal.webextStorageLocal" in ContextualIdentityService.sys.mjs).
const WEBEXT_STORAGE_USER_CONTEXT_ID = -1 >>> 0;

// The maximum time to wait for extension child shutdown blockers to complete.
const CHILD_SHUTDOWN_TIMEOUT_MS = 8000;

// Permissions that are only available to privileged extensions.
const PRIVILEGED_PERMS = new Set([
  "activityLog",
  "mozillaAddons",
  "networkStatus",
  "normandyAddonStudy",
  "telemetry",
]);

const PRIVILEGED_PERMS_ANDROID_ONLY = new Set([
  "geckoViewAddons",
  "nativeMessagingFromContent",
  "nativeMessaging",
]);

const PRIVILEGED_PERMS_DESKTOP_ONLY = new Set(["normandyAddonStudy"]);

if (AppConstants.platform == "android") {
  for (const perm of PRIVILEGED_PERMS_ANDROID_ONLY) {
    PRIVILEGED_PERMS.add(perm);
  }
}

if (
  AppConstants.MOZ_APP_NAME != "firefox" ||
  AppConstants.platform == "android"
) {
  for (const perm of PRIVILEGED_PERMS_DESKTOP_ONLY) {
    PRIVILEGED_PERMS.delete(perm);
  }
}

// Permissions that are not available in manifest version 2.
const PERMS_NOT_IN_MV2 = new Set([
  // MV2 had a userScripts API, tied to "user_scripts" manifest key. In MV3 the
  // userScripts API availability is gated by the "userScripts" permission.
  "userScripts",
]);

// Message included in warnings and errors related to privileged permissions and
// privileged manifest properties. Provides a link to the firefox-source-docs.mozilla.org
// section related to developing and sign Privileged Add-ons.
const PRIVILEGED_ADDONS_DEVDOCS_MESSAGE =
  "See https://mzl.la/3NS9KJd for more details about how to develop a privileged add-on.";

const INSTALL_AND_UPDATE_STARTUP_REASONS = new Set([
  "ADDON_INSTALL",
  "ADDON_UPGRADE",
  "ADDON_DOWNGRADE",
]);

const PROTOCOL_HANDLER_OPEN_PERM_KEY = "open-protocol-handler";
const PERMISSION_KEY_DELIMITER = "^";

// These are used for manipulating jar entry paths, which always use Unix
// separators (originally copied from `ospath_unix.jsm` as part of the "OS.Path
// to PathUtils" migration).

/**
 * Return the final part of the path.
 * The final part of the path is everything after the last "/".
 */
function basename(path) {
  return path.slice(path.lastIndexOf("/") + 1);
}

/**
 * Return the directory part of the path.
 * The directory part of the path is everything before the last
 * "/". If the last few characters of this part are also "/",
 * they are ignored.
 *
 * If the path contains no directory, return ".".
 */
function dirname(path) {
  let index = path.lastIndexOf("/");
  if (index == -1) {
    return ".";
  }
  while (index >= 0 && path[index] == "/") {
    --index;
  }
  return path.slice(0, index + 1);
}

// Returns true if the extension is owned by Mozilla (is either privileged,
// using one of the @mozilla.com/@mozilla.org protected addon id suffixes).
//
// This method throws if the extension's startupReason is not one of the
// expected ones (either ADDON_INSTALL, ADDON_UPGRADE or ADDON_DOWNGRADE).
//
// TODO(Bug 1835787): Consider to remove the restriction based on the
// startupReason now that the recommendationState property is always
// included in the addonData with any of the startupReason.
function isMozillaExtension(extension) {
  const { addonData, id, isPrivileged, startupReason } = extension;

  if (!INSTALL_AND_UPDATE_STARTUP_REASONS.has(startupReason)) {
    throw new Error(
      `isMozillaExtension called with unexpected startupReason: ${startupReason}`
    );
  }

  if (isPrivileged) {
    return true;
  }

  if (id.endsWith("@mozilla.com") || id.endsWith("@mozilla.org")) {
    return true;
  }

  // This check is a subset of what is being checked in AddonWrapper's
  // recommendationStates (states expire dates for line extensions are
  // not considered important in determining that the extension is
  // provided by mozilla, and so they are omitted here on purpose).
  const isMozillaLineExtension =
    addonData.recommendationState?.states?.includes("line");
  const isSigned =
    addonData.signedState > lazy.AddonManager.SIGNEDSTATE_MISSING;

  return isSigned && isMozillaLineExtension;
}

/**
 * Classify an individual permission from a webextension manifest
 * as a host/origin permission, an api permission, or a regular permission.
 *
 * @param {string} perm  The permission string to classify
 * @param {boolean} restrictSchemes
 * @param {boolean} isPrivileged whether or not the webextension is privileged
 *
 * @returns {object}
 *          An object with exactly one of the following properties:
 *          "origin" to indicate this is a host/origin permission.
 *          "api" to indicate this is an api permission
 *                (as used for webextensions experiments).
 *          "permission" to indicate this is a regular permission.
 *          "invalid" to indicate that the given permission cannot be used.
 */
function classifyPermission(perm, restrictSchemes, isPrivileged) {
  let match = /^(\w+)(?:\.(\w+)(?:\.\w+)*)?$/.exec(perm);
  if (!match) {
    try {
      let { pattern } = new MatchPattern(perm, {
        restrictSchemes,
        ignorePath: true,
      });
      return { origin: pattern };
    } catch (e) {
      return { invalid: perm };
    }
  } else if (match[1] == "experiments" && match[2]) {
    return { api: match[2] };
  } else if (!isPrivileged && PRIVILEGED_PERMS.has(match[1])) {
    return { invalid: perm, privileged: true };
  } else if (perm.startsWith("declarativeNetRequest") && !lazy.dnrEnabled) {
    return { invalid: perm };
  } else if (perm === "userScripts" && !lazy.userScriptsMV3Enabled) {
    return { invalid: perm };
  }
  return { permission: perm };
}

function stripCommentsFromJSON(text) {
  for (let i = 0; i < text.length; ++i) {
    let c = text[i];
    if (c == '"') {
      let escaped;
      do {
        i = text.indexOf('"', i + 1);
        if (i === -1) {
          throw new Error("Invalid JSON: Unterminated string literal");
        }
        // Find if quote is escaped: preceded by an unpaired backslash.
        escaped = false;
        for (let k = i - 1; text[k] === "\\"; --k) {
          escaped = !escaped;
        }
      } while (escaped);
      // Next iteration will continue after the " that terminates the string.
    } else if (c === "/") {
      if (text[i + 1] !== "/") {
        // A "/" can only appear outside of a string if it starts a //-comment.
        throw new Error("Invalid JSON: Unexpected /");
      }
      let indexAfterComment = text.indexOf("\n", i + 2);
      if (indexAfterComment === -1) {
        indexAfterComment = text.length;
      }
      // Discard //-comment:
      text = text.slice(0, i) + text.slice(indexAfterComment);
      // text[i] is now "\n" or at end of string.
      // Next iteration (if any) will continue after the "\n".
    }
  }
  return text;
}

const LOGGER_ID_BASE = "addons.webextension.";
const UUID_MAP_PREF = "extensions.webextensions.uuids";
const LEAVE_STORAGE_PREF = "extensions.webextensions.keepStorageOnUninstall";
const LEAVE_UUID_PREF = "extensions.webextensions.keepUuidOnUninstall";

// All moz-extension URIs use a machine-specific UUID rather than the
// extension's own ID in the host component. This makes it more
// difficult for web pages to detect whether a user has a given add-on
// installed (by trying to load a moz-extension URI referring to a
// web_accessible_resource from the extension). UUIDMap.get()
// returns the UUID for a given add-on ID.
var UUIDMap = {
  _read() {
    let pref = Services.prefs.getStringPref(UUID_MAP_PREF, "{}");
    try {
      return JSON.parse(pref);
    } catch (e) {
      Cu.reportError(`Error parsing ${UUID_MAP_PREF}.`);
      return {};
    }
  },

  _write(map) {
    Services.prefs.setStringPref(UUID_MAP_PREF, JSON.stringify(map));
  },

  get(id, create = true) {
    let map = this._read();

    if (id in map) {
      return map[id];
    }

    let uuid = null;
    if (create) {
      uuid = Services.uuid.generateUUID().number;
      uuid = uuid.slice(1, -1); // Strip { and } off the UUID.

      map[id] = uuid;
      this._write(map);
    }
    return uuid;
  },

  remove(id) {
    let map = this._read();
    delete map[id];
    this._write(map);
  },
};

function clearCacheForExtensionPrincipal(principal, clearAll = false) {
  if (!principal.schemeIs("moz-extension")) {
    return Promise.reject(new Error("Unexpected non extension principal"));
  }

  // TODO(Bug 1750053): replace the two specific flags with a "clear all caches one"
  // (along with covering the other kind of cached data with tests).
  const clearDataFlags = clearAll
    ? Ci.nsIClearDataService.CLEAR_ALL_CACHES
    : Ci.nsIClearDataService.CLEAR_IMAGE_CACHE |
      Ci.nsIClearDataService.CLEAR_CSS_CACHE |
      Ci.nsIClearDataService.CLEAR_JS_CACHE;

  return new Promise(resolve =>
    Services.clearData.deleteDataFromPrincipal(
      principal,
      false,
      clearDataFlags,
      () => resolve()
    )
  );
}

/**
 * Observer AddonManager events and translate them into extension events,
 * as well as handle any last cleanup after uninstalling an extension.
 */
var ExtensionAddonObserver = {
  initialized: false,

  init() {
    if (!this.initialized) {
      lazy.AddonManager.addAddonListener(this);
      this.initialized = true;
    }
  },

  // AddonTestUtils will call this as necessary.
  uninit() {
    if (this.initialized) {
      lazy.AddonManager.removeAddonListener(this);
      this.initialized = false;
    }
  },

  onEnabling(addon) {
    if (addon.type !== "extension") {
      return;
    }
    Management._callHandlers([addon.id], "enabling", "onEnabling");
  },

  onDisabled(addon) {
    if (addon.type !== "extension") {
      return;
    }
    if (Services.appinfo.inSafeMode) {
      // Ensure ExtensionPreferencesManager updates its data and
      // modules can run any disable logic they need to.  We only
      // handle safeMode here because there is a bunch of additional
      // logic that happens in Extension.shutdown when running in
      // normal mode.
      Management._callHandlers([addon.id], "disable", "onDisable");
    }
  },

  onUninstalling(addon) {
    let extension = GlobalManager.extensionMap.get(addon.id);
    if (extension) {
      // Let any other interested listeners respond
      // (e.g., display the uninstall URL)
      Management.emit("uninstalling", extension);
    }
  },

  onUninstalled(addon) {
    this.clearOnUninstall(addon.id);
  },

  /**
   * Clears persistent state from the add-on post install.
   *
   * @param {string} addonId The ID of the addon that has been uninstalled.
   */
  clearOnUninstall(addonId) {
    const tasks = [];
    function addShutdownBlocker(name, promise) {
      lazy.AsyncShutdown.profileChangeTeardown.addBlocker(name, promise);
      tasks.push({ name, promise });
    }
    function notifyUninstallTaskObservers() {
      Management.emit("cleanupAfterUninstall", addonId, tasks);
    }

    // Cleanup anything that is used by non-extension addon types
    // since only extensions have uuid's.
    addShutdownBlocker(
      `Clear ExtensionPermissions for ${addonId}`,
      lazy.ExtensionPermissions.removeAll(addonId)
    );

    lazy.QuarantinedDomains.clearUserPref(addonId);

    let uuid = UUIDMap.get(addonId, false);
    if (!uuid) {
      notifyUninstallTaskObservers();
      return;
    }

    let baseURI = Services.io.newURI(`moz-extension://${uuid}/`);
    let principal = Services.scriptSecurityManager.createContentPrincipal(
      baseURI,
      {}
    );

    // Clear all cached resources (e.g. CSS and images);
    addShutdownBlocker(
      `Clear cache for ${addonId}`,
      clearCacheForExtensionPrincipal(principal, /* clearAll */ true)
    );

    // Clear all the registered service workers for the extension
    // principal (the one that may have been registered through the
    // manifest.json file and the ones that may have been registered
    // from an extension page through the service worker API).
    //
    // Any stored data would be cleared below (if the pref
    // "extensions.webextensions.keepStorageOnUninstall has not been
    // explicitly set to true, which is usually only done in
    // tests and by some extensions developers for testing purpose).
    //
    // TODO: ServiceWorkerCleanUp may go away once Bug 1183245
    // is fixed, and so this may actually go away, replaced by
    // marking the registration as disabled or to be removed on
    // shutdown (where we do know if the extension is shutting
    // down because is being uninstalled) and then cleared from
    // the persisted serviceworker registration on the next
    // startup.
    addShutdownBlocker(
      `Clear ServiceWorkers for ${addonId}`,
      lazy.ServiceWorkerCleanUp.removeFromPrincipal(principal)
    );

    // Clear the persisted menus created with the menus/contextMenus API (if any).
    addShutdownBlocker(
      `Clear menus store for ${addonId}`,
      lazy.ExtensionMenus.clearPersistedMenusOnUninstall(addonId)
    );

    // Clear the persisted dynamic content scripts created with the scripting
    // API (if any).
    addShutdownBlocker(
      `Clear scripting store for ${addonId}`,
      lazy.ExtensionScriptingStore.clearOnUninstall(addonId)
    );

    // Clear MV3 userScripts API data, if any.
    addShutdownBlocker(
      `Clear user scripts for ${addonId}`,
      lazy.ExtensionUserScripts.clearOnUninstall(addonId)
    );

    // Clear the DNR API's rules data persisted on disk (if any).
    addShutdownBlocker(
      `Clear declarativeNetRequest store for ${addonId}`,
      lazy.ExtensionDNRStore.clearOnUninstall(uuid)
    );

    if (!Services.prefs.getBoolPref(LEAVE_STORAGE_PREF, false)) {
      // Clear browser.storage.local backends.
      addShutdownBlocker(
        `Clear Extension Storage ${addonId} (File Backend)`,
        lazy.ExtensionStorage.clear(addonId, { shouldNotifyListeners: false })
      );

      // Clear browser.storage.sync rust-based backend.
      // (storage.sync clearOnUninstall will resolve and log an error on the
      // browser console in case of unexpected failures).
      if (!lazy.storageSyncOldKintoBackend) {
        addShutdownBlocker(
          `Clear Extension StorageSync ${addonId}`,
          lazy.extensionStorageSync.clearOnUninstall(addonId)
        );
      }

      // Clear any IndexedDB and Cache API storage created by the extension.
      // If LSNG is enabled, this also clears localStorage.
      Services.qms.clearStoragesForPrincipal(principal);

      // Clear any storage.local data stored in the IDBBackend.
      let storagePrincipal =
        Services.scriptSecurityManager.createContentPrincipal(baseURI, {
          userContextId: WEBEXT_STORAGE_USER_CONTEXT_ID,
        });
      Services.qms.clearStoragesForPrincipal(storagePrincipal);

      lazy.ExtensionStorageIDB.clearMigratedExtensionPref(addonId);

      // If LSNG is not enabled, we need to clear localStorage explicitly using
      // the old API.
      if (!Services.domStorageManager.nextGenLocalStorageEnabled) {
        // Clear localStorage created by the extension
        let storage = Services.domStorageManager.getStorage(
          null,
          principal,
          principal
        );
        if (storage) {
          storage.clear();
        }
      }

      // Remove any permissions related to the unlimitedStorage permission
      // if we are also removing all the data stored by the extension.
      Services.perms.removeFromPrincipal(
        principal,
        "WebExtensions-unlimitedStorage"
      );
      Services.perms.removeFromPrincipal(principal, "persistent-storage");
    }

    // Clear any protocol handler permissions granted to this add-on.
    let permissions = Services.perms.getAllWithTypePrefix(
      PROTOCOL_HANDLER_OPEN_PERM_KEY + PERMISSION_KEY_DELIMITER
    );
    for (let perm of permissions) {
      if (perm.principal.equalsURI(baseURI)) {
        Services.perms.removePermission(perm);
      }
    }

    if (!Services.prefs.getBoolPref(LEAVE_UUID_PREF, false)) {
      // Clear the entry in the UUID map
      UUIDMap.remove(addonId);
    }

    notifyUninstallTaskObservers();
  },

  onPropertyChanged(addon, properties) {
    let extension = GlobalManager.extensionMap.get(addon.id);
    if (!extension) {
      return;
    }

    if (properties.includes("quarantineIgnoredByUser")) {
      extension.ignoreQuarantine = addon.quarantineIgnoredByUser;
      extension.policy.ignoreQuarantine = addon.quarantineIgnoredByUser;

      extension.setSharedData("", extension.serialize());
      Services.ppmm.sharedData.flush();

      extension.emit("update-ignore-quarantine");
      extension.broadcast("Extension:UpdateIgnoreQuarantine", {
        id: extension.id,
        ignoreQuarantine: addon.quarantineIgnoredByUser,
      });
    }

    if (properties.includes("blocklistState")) {
      extension.blocklistState = addon.blocklistState;
      extension.emit("update-blocklist-state");
    }
  },
};

ExtensionAddonObserver.init();

/**
 * Observer ExtensionProcess crashes and notify all the extensions
 * using a Management event named "extension-process-crash".
 */
export var ExtensionProcessCrashObserver = {
  initialized: false,

  // For Android apps we initially consider the app as always starting
  // in the background, then we expect to be setting it to foreground
  // when GeckoView LifecycleListener onResume method is called on the
  // Android app first startup. After the application has got on the
  // foreground for the first time then onPause/onResumed LifecycleListener
  // are called, the application-foreground/-background topics will be
  // notified to Gecko and this flag will be updated accordingly.
  _appInForeground: AppConstants.platform !== "android",
  _isAndroid: AppConstants.platform === "android",
  _processSpawningDisabled: false,

  // Technically there is at most one child extension process,
  // but we may need to adjust this assumption to account for more
  // than one if that ever changes in the future.
  currentProcessChildID: undefined,
  lastCrashedProcessChildID: undefined,
  QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),

  // Collect the timestamps of the crashes happened over the last
  // `processCrashTimeframe` milliseconds.
  lastCrashTimestamps: [],

  logger: Log.repository.getLogger("addons.process-crash-observer"),

  init() {
    if (!this.initialized) {
      Services.obs.addObserver(this, "ipc:content-created");
      Services.obs.addObserver(this, "process-type-set");
      Services.obs.addObserver(this, "ipc:content-shutdown");
      if (this._isAndroid) {
        Services.obs.addObserver(this, "geckoview-initial-foreground");
        Services.obs.addObserver(this, "application-foreground");
        Services.obs.addObserver(this, "application-background");
      }
      this.initialized = true;
    }
  },

  uninit() {
    if (this.initialized) {
      try {
        Services.obs.removeObserver(this, "ipc:content-created");
        Services.obs.removeObserver(this, "process-type-set");
        Services.obs.removeObserver(this, "ipc:content-shutdown");
        if (this._isAndroid) {
          Services.obs.removeObserver(this, "geckoview-initial-foreground");
          Services.obs.removeObserver(this, "application-foreground");
          Services.obs.removeObserver(this, "application-background");
        }
      } catch (err) {
        // Removing the observer may fail if they are not registered anymore,
        // this shouldn't happen in practice, but let's still log the error
        // in case it does.
        Cu.reportError(err);
      }
      this.initialized = false;
    }
  },

  observe(subject, topic, data) {
    let childID = data;
    switch (topic) {
      case "geckoview-initial-foreground":
        this._appInForeground = true;
        this.logger.debug(
          `Detected Android application moved in the foreground (geckoview-initial-foreground)`
        );
        break;
      case "application-foreground":
      // Intentional fall-through
      case "application-background":
        this._appInForeground = topic === "application-foreground";
        this.logger.debug(
          `Detected Android application moved in the ${
            this._appInForeground ? "foreground" : "background"
          }`
        );
        if (this._appInForeground) {
          Management.emit("application-foreground", {
            appInForeground: this._appInForeground,
            childID: this.currentProcessChildID,
            processSpawningDisabled: this.processSpawningDisabled,
          });
        }
        break;
      case "process-type-set":
      // Intentional fall-through
      case "ipc:content-created": {
        let pp = subject.QueryInterface(Ci.nsIDOMProcessParent);
        if (pp.remoteType === "extension") {
          this.currentProcessChildID = childID;
          Glean.extensions.processEvent[
            this.appInForeground ? "created_fg" : "created_bg"
          ].add(1);
        }
        break;
      }
      case "ipc:content-shutdown": {
        if (Services.startup.shuttingDown) {
          // The application is shutting down, don't bother
          // signaling process crashes anymore.
          return;
        }
        if (this.currentProcessChildID !== childID) {
          // Ignore non-extension child process shutdowns.
          return;
        }

        // At this point we are sure that the current extension
        // process is gone, and so even if the process did shutdown
        // cleanly instead of crashing, we can clear the property
        // that keeps track of the current extension process childID.
        this.currentProcessChildID = undefined;

        subject.QueryInterface(Ci.nsIPropertyBag2);
        if (!subject.get("abnormal")) {
          // Ignore non-abnormal child process shutdowns.
          return;
        }

        this.lastCrashedProcessChildID = childID;

        const now = Cu.now();
        // Filter crash timestamps older than processCrashTimeframe.
        this.lastCrashTimestamps = this.lastCrashTimestamps.filter(
          timestamp => now - timestamp < lazy.processCrashTimeframe
        );
        // Push the new timeframe.
        this.lastCrashTimestamps.push(now);
        // Set the flag that disable process spawning when we exceed the
        // `processCrashThreshold`.
        this._processSpawningDisabled =
          this.lastCrashTimestamps.length > lazy.processCrashThreshold;

        this.logger.debug(
          `Extension process crashed ${this.lastCrashTimestamps.length} times over the last ${lazy.processCrashTimeframe}ms`
        );

        const { appInForeground } = this;

        if (this.processSpawningDisabled) {
          if (appInForeground) {
            Glean.extensions.processEvent.crashed_over_threshold_fg.add(1);
          } else {
            Glean.extensions.processEvent.crashed_over_threshold_bg.add(1);
          }
          this.logger.warn(
            `Extension process respawning disabled because it crashed too often in the last ${lazy.processCrashTimeframe}ms (${this.lastCrashTimestamps.length} > ${lazy.processCrashThreshold}).`
          );
        }

        Glean.extensions.processEvent[
          appInForeground ? "crashed_fg" : "crashed_bg"
        ].add(1);
        Management.emit("extension-process-crash", {
          childID,
          processSpawningDisabled: this.processSpawningDisabled,
          appInForeground,
        });
        break;
      }
    }
  },

  enableProcessSpawning() {
    const crashCounter = this.lastCrashTimestamps.length;
    this.lastCrashTimestamps = [];
    this.logger.debug(`reset crash counter (was ${crashCounter})`);
    this._processSpawningDisabled = false;
    Management.emit("extension-enable-process-spawning");
  },

  get appInForeground() {
    // Only account for application in the background for
    // android builds.
    return this._isAndroid ? this._appInForeground : true;
  },

  get processSpawningDisabled() {
    return this._processSpawningDisabled;
  },
};

ExtensionProcessCrashObserver.init();

const manifestTypes = new Map([
  ["theme", "manifest.ThemeManifest"],
  ["locale", "manifest.WebExtensionLangpackManifest"],
  ["dictionary", "manifest.WebExtensionDictionaryManifest"],
  ["extension", "manifest.WebExtensionManifest"],
]);

/**
 * Represents the data contained in an extension, contained either
 * in a directory or a zip file, which may or may not be installed.
 * This class implements the functionality of the Extension class,
 * primarily related to manifest parsing and localization, which is
 * useful prior to extension installation or initialization.
 *
 * No functionality of this class is guaranteed to work before
 * `loadManifest` has been called, and completed.
 */
export class ExtensionData {
  /**
   * Note: These fields are only available and meant to be used on Extension
   * instances, declared here because methods from this class reference them.
   */
  /** @type {object} TODO: move to the Extension class, bug 1871094. */
  addonData;
  /** @type {nsIURI} */
  baseURI;
  /** @type {nsIPrincipal} */
  principal;
  /** @type {boolean} */
  temporarilyInstalled;

  constructor(rootURI, isPrivileged = false) {
    this.rootURI = rootURI;
    this.resourceURL = rootURI.spec;
    this.isPrivileged = isPrivileged;

    this.manifest = null;
    this.type = null;
    this.id = null;
    this.uuid = null;
    this.localeData = null;
    this.fluentL10n = null;
    this._promiseLocales = null;

    this.apiNames = new Set();
    this.dependencies = new Set();
    this.permissions = new Set();
    this.dataCollectionPermissions = new Set();

    this.startupData = null;

    this.errors = [];
    this.warnings = [];
    this.eventPagesEnabled = lazy.eventPagesEnabled;
  }

  /**
   * A factory function that allows the construction of ExtensionData, with
   * the isPrivileged flag computed asynchronously.
   *
   * @param {object} options
   * @param {nsIURI} options.rootURI
   *  The URI pointing to the extension root.
   * @param {function(type, id): boolean} options.checkPrivileged
   *  An (async) function that takes the addon type and addon ID and returns
   *  whether the given add-on is privileged.
   * @param {boolean} options.temporarilyInstalled
   *  whether the given add-on is installed as temporary.
   * @returns {Promise<ExtensionData>}
   */
  static async constructAsync({
    rootURI,
    checkPrivileged,
    temporarilyInstalled,
  }) {
    let extension = new ExtensionData(rootURI);
    // checkPrivileged depends on the extension type and id.
    await extension.initializeAddonTypeAndID();
    let { type, id } = extension;
    extension.isPrivileged = await checkPrivileged(type, id);
    extension.temporarilyInstalled = temporarilyInstalled;
    return extension;
  }

  static getIsPrivileged({ signedState, builtIn, temporarilyInstalled }) {
    return (
      signedState === lazy.AddonManager.SIGNEDSTATE_PRIVILEGED ||
      signedState === lazy.AddonManager.SIGNEDSTATE_SYSTEM ||
      builtIn ||
      (lazy.AddonSettings.EXPERIMENTS_ENABLED && temporarilyInstalled)
    );
  }

  get builtinMessages() {
    return null;
  }

  get logger() {
    let id = this.id || "<unknown>";
    return Log.repository.getLogger(LOGGER_ID_BASE + id);
  }

  /**
   * Report an error about the extension's manifest file.
   *
   * @param {string} message The error message
   */
  manifestError(message) {
    this.packagingError(`Reading manifest: ${message}`);
  }

  /**
   * Report a warning about the extension's manifest file.
   *
   * @param {string} message The warning message
   */
  manifestWarning(message) {
    this.packagingWarning(`Reading manifest: ${message}`);
  }

  // Report an error about the extension's general packaging.
  packagingError(message) {
    this.errors.push(message);
    this.logError(message);
  }

  packagingWarning(message) {
    this.warnings.push(message);
    this.logWarning(message);
  }

  logWarning(message) {
    this._logMessage(message, "warn");
  }

  logError(message) {
    this._logMessage(message, "error");
  }

  _logMessage(message, severity) {
    this.logger[severity](`Loading extension '${this.id}': ${message}`);
  }

  ensureNoErrors() {
    if (this.errors.length) {
      // startup() repeatedly checks whether there are errors after parsing the
      // extension/manifest before proceeding with starting up.
      throw new Error(this.errors.join("\n"));
    }
  }

  /**
   * Returns the moz-extension: URL for the given path within this
   * extension.
   *
   * Must not be called unless either the `id` or `uuid` property has
   * already been set.
   *
   * @param {string} path The path portion of the URL.
   * @returns {string}
   */
  getURL(path = "") {
    if (!(this.id || this.uuid)) {
      throw new Error(
        "getURL may not be called before an `id` or `uuid` has been set"
      );
    }
    if (!this.uuid) {
      this.uuid = UUIDMap.get(this.id);
    }
    return `moz-extension://${this.uuid}/${path}`;
  }

  /**
   * Discovers the file names within a directory or JAR file.
   *
   * @param {string} path
   *   The path to the directory or jar file to look at.
   * @param {boolean} [directoriesOnly]
   *   If true, this will return only the directories present within the directory.
   * @returns {Promise<string[]>}
   *   An array of names of files/directories (only the name, not the path).
   */
  async _readDirectory(path, directoriesOnly = false) {
    if (this.rootURI instanceof Ci.nsIFileURL) {
      let uri = Services.io.newURI("./" + path, null, this.rootURI);
      let fullPath = uri.QueryInterface(Ci.nsIFileURL).file.path;

      let results = [];
      try {
        let children = await IOUtils.getChildren(fullPath);
        for (let child of children) {
          if (
            !directoriesOnly ||
            (await IOUtils.stat(child)).type == "directory"
          ) {
            results.push(PathUtils.filename(child));
          }
        }
      } catch (ex) {
        // Fall-through, return what we have.
      }
      return results;
    }

    let uri = this.rootURI.QueryInterface(Ci.nsIJARURI);

    // Append the sub-directory path to the base JAR URI and normalize the
    // result.
    let entry = `${uri.JAREntry}/${path}/`
      .replace(/\/\/+/g, "/")
      .replace(/^\//, "");
    uri = Services.io.newURI(`jar:${uri.JARFile.spec}!/${entry}`);

    let results = [];
    for (let name of lazy.aomStartup.enumerateJARSubtree(uri)) {
      if (!name.startsWith(entry)) {
        throw new Error("Unexpected ZipReader entry");
      }

      // The enumerator returns the full path of all entries.
      // Trim off the leading path, and filter out entries from
      // subdirectories.
      name = name.slice(entry.length);
      if (
        name &&
        !/\/./.test(name) &&
        (!directoriesOnly || name.endsWith("/"))
      ) {
        results.push(name.replace("/", ""));
      }
    }

    return results;
  }

  readJSON(path) {
    return new Promise((resolve, reject) => {
      let uri = this.rootURI.resolve(`./${path}`);

      lazy.NetUtil.asyncFetch(
        { uri, loadUsingSystemPrincipal: true },
        (inputStream, status) => {
          if (!Components.isSuccessCode(status)) {
            // Convert status code to a string
            let e = Components.Exception("", status);
            reject(new Error(`Error while loading '${uri}' (${e.name})`));
            return;
          }
          try {
            let text = lazy.NetUtil.readInputStreamToString(
              inputStream,
              inputStream.available(),
              { charset: "utf-8" }
            );

            text = stripCommentsFromJSON(text);

            resolve(JSON.parse(text));
          } catch (e) {
            reject(e);
          }
        }
      );
    });
  }

  get restrictSchemes() {
    return !(this.isPrivileged && this.hasPermission("mozillaAddons"));
  }

  get optionsPageProperties() {
    let page = this.manifest.options_ui?.page ?? this.manifest.options_page;
    if (!page) {
      return null;
    }
    return {
      page,
      open_in_tab: this.manifest.options_ui
        ? (this.manifest.options_ui.open_in_tab ?? false)
        : true,
      // `options_ui.browser_style` is assigned the proper default value
      // (true for MV2 and false for MV3 when not explicitly set),
      // in `#parseBrowserStyleInManifest` (called when we are loading
      // and parse manifest data from the `parseManifest` method).
      browser_style: this.manifest.options_ui?.browser_style ?? false,
    };
  }

  /**
   * Given an array of host and permissions, generate a structured permissions object
   * that contains seperate host origins and permissions arrays.
   *
   * @param {Array} permissionsArray
   * @param {Array} [hostPermissions]
   * @returns {object} permissions object
   */
  permissionsObject(permissionsArray = [], hostPermissions = []) {
    let permissions = new Set();
    let origins = new Set();
    let { restrictSchemes, isPrivileged } = this;
    let isMV2 = this.manifestVersion === 2;

    for (let perm of permissionsArray.concat(hostPermissions)) {
      let type = classifyPermission(perm, restrictSchemes, isPrivileged);
      if (type.origin) {
        origins.add(perm);
      } else if (type.permission) {
        if (isMV2 && PERMS_NOT_IN_MV2.has(perm)) {
          // Skip, without warning (parseManifest warns if needed).
          continue;
        }
        permissions.add(perm);
      }
    }

    return {
      permissions,
      origins,
    };
  }

  /**
   * Returns an object representing any capabilities that the extension
   * has access to based on fixed properties in the manifest.  The result
   * includes the contents of the "permissions" property as well as other
   * capabilities that are derived from manifest fields that users should
   * be informed of (e.g., origins where content scripts are injected).
   *
   * For MV3 extensions with origin controls, this does not include origins.
   */
  getRequiredPermissions() {
    if (this.type !== "extension") {
      return null;
    }

    let { permissions } = this.permissionsObject(this.manifest.permissions);

    if (
      this.manifest.devtools_page &&
      !this.manifest.optional_permissions.includes("devtools")
    ) {
      permissions.add("devtools");
    }

    return {
      permissions: Array.from(permissions),
      origins: this.originControls ? [] : this.getManifestOrigins(),
      data_collection: lazy.dataCollectionPermissionsEnabled
        ? this.getDataCollectionPermissions().required
        : [],
    };
  }

  /**
   * @param {object} manifest A normalized manifest (which, in this case, means
   * that `browser_specific_settings` was folded into `applications`).
   *
   * @returns {{required:Array<string>, optional: Array<string>}} an object
   * containing the `required` and `optional` data collection permissions
   * listed in the manifest.
   */
  getDataCollectionPermissions(manifest = this.manifest) {
    if (this.type !== "extension") {
      return { required: [], optional: [] };
    }

    const data_collection_permissions =
      manifest.applications?.gecko?.data_collection_permissions;

    return {
      required: Array.from(new Set(data_collection_permissions?.required)),
      optional: Array.from(new Set(data_collection_permissions?.optional)),
    };
  }

  /**
   * @returns {string[]} all origins that are referenced in manifest via
   * permissions, host_permissions, or content_scripts keys.
   */
  getManifestOrigins() {
    if (this.type !== "extension") {
      return null;
    }

    let { origins } = this.permissionsObject(
      this.manifest.permissions,
      this.manifest.host_permissions
    );

    for (let entry of this.manifest.content_scripts || []) {
      for (let origin of entry.matches) {
        origins.add(origin);
      }
    }

    return Array.from(origins);
  }

  /**
   * @returns {MatchPatternSet} MatchPatternSet for only the origins that are
   * referenced in manifest via permissions, host_permissions, or content_scripts keys.
   */
  getManifestOriginsMatchPatternSet() {
    if (this.type !== "extension") {
      return null;
    }
    if (this._manifestOriginsMatchPatternSet) {
      return this._manifestOriginsMatchPatternSet;
    }
    this._manifestOriginsMatchPatternSet = new MatchPatternSet(
      this.getManifestOrigins(),
      {
        restrictSchemes: this.restrictSchemes,
        ignorePath: true,
      }
    );
    return this._manifestOriginsMatchPatternSet;
  }

  /**
   * Returns additional permissions that extensions is requesting based on its
   * manifest. For now, this is host_permissions (and content scripts) in mv3,
   * and the "technicalAndInteraction" optional data collection permission.
   *
   * @returns {null | Permissions}
   */
  getRequestedPermissions() {
    if (this.type !== "extension") {
      return null;
    }

    // We unconditionally return a list of `data_collection` so that we don't
    // have to check the presence of `data_collection` everywhere. For example,
    // `data_collection` is used for the `installPermissions` property of the
    // add-on wrapper, defined in `XPIDatabase`.
    const data_collection = lazy.dataCollectionPermissionsEnabled
      ? this.getDataCollectionPermissions().optional.filter(
          perm => perm === "technicalAndInteraction"
        )
      : [];

    if (this.originControls && lazy.installIncludesOrigins) {
      return {
        permissions: [],
        origins: this.getManifestOrigins(),
        data_collection,
      };
    }
    return {
      permissions: [],
      origins: [],
      data_collection,
    };
  }

  /**
   * Returns optional permissions from the manifest, including host permissions
   * if originControls is true, and optional data collection (if enabled).
   *
   * @returns {null | Permissions}
   */
  get manifestOptionalPermissions() {
    if (this.type !== "extension") {
      return null;
    }

    let { permissions, origins } = this.permissionsObject(
      this.manifest.optional_permissions,
      this.manifest.optional_host_permissions
    );
    if (this.originControls) {
      for (let origin of this.getManifestOrigins()) {
        origins.add(origin);
      }
    }

    const data_collection = lazy.dataCollectionPermissionsEnabled
      ? this.getDataCollectionPermissions().optional
      : [];

    return {
      permissions: Array.from(permissions),
      origins: Array.from(origins),
      data_collection,
    };
  }

  /**
   * Returns an object representing all capabilities this extension has
   * access to, including fixed ones from the manifest as well as dynamically
   * granted permissions.
   */
  get activePermissions() {
    if (this.type !== "extension") {
      return null;
    }

    let result = {
      origins: this.allowedOrigins.patterns
        .map(matcher => matcher.pattern)
        // moz-extension://id/* is always added to allowedOrigins, but it
        // is not a valid host permission in the API. So, remove it.
        .filter(pattern => !pattern.startsWith("moz-extension:")),
      apis: [...this.apiNames],
    };

    if (lazy.dataCollectionPermissionsEnabled) {
      result.data_collection = Array.from(this.dataCollectionPermissions);
    }

    const EXP_PATTERN = /^experiments\.\w+/;
    result.permissions = [...this.permissions].filter(
      p => !result.origins.includes(p) && !EXP_PATTERN.test(p)
    );
    return result;
  }

  // Returns whether the front end should prompt for this permission
  static async shouldPromptFor(permission) {
    return !(await lazy.NO_PROMPT_PERMISSIONS).has(permission);
  }

  // Compute the difference between two sets of permissions, suitable
  // for presenting to the user.
  static comparePermissions(oldPermissions, newPermissions) {
    let oldMatcher = new MatchPatternSet(oldPermissions.origins, {
      restrictSchemes: false,
    });
    return {
      // formatPermissionStrings ignores any scheme, so only look at the domain.
      origins: newPermissions.origins.filter(
        perm =>
          !oldMatcher.subsumesDomain(
            new MatchPattern(perm, { restrictSchemes: false })
          )
      ),
      permissions: newPermissions.permissions.filter(
        perm => !oldPermissions.permissions.includes(perm)
      ),
      data_collection: newPermissions.data_collection.filter(
        perm => newPermissions.data_collection.includes(perm) && perm !== "none"
      ),
    };
  }

  // Return those permissions in oldPermissions that also exist in newPermissions.
  static intersectPermissions(oldPermissions, newPermissions) {
    let matcher = new MatchPatternSet(newPermissions.origins, {
      restrictSchemes: false,
    });

    return {
      origins: oldPermissions.origins.filter(perm =>
        matcher.subsumesDomain(
          new MatchPattern(perm, { restrictSchemes: false })
        )
      ),
      permissions: oldPermissions.permissions.filter(perm =>
        newPermissions.permissions.includes(perm)
      ),
      data_collection: oldPermissions.data_collection.filter(
        perm => newPermissions.data_collection.includes(perm) && perm !== "none"
      ),
    };
  }

  /**
   * When updating the addon, find and migrate permissions that have moved from required
   * to optional.  This also handles any updates required for permission removal.
   *
   * @param {string} id The id of the addon being updated
   * @param {object} oldPermissions
   * @param {object} oldOptionalPermissions
   * @param {object} newPermissions
   * @param {object} newOptionalPermissions
   */
  static async migratePermissions(
    id,
    oldPermissions,
    oldOptionalPermissions,
    newPermissions,
    newOptionalPermissions
  ) {
    let migrated = ExtensionData.intersectPermissions(
      oldPermissions,
      newOptionalPermissions
    );
    // If a permission is optional in this version and was mandatory in the previous
    // version, it was already accepted by the user at install time so add it to the
    // list of granted optional permissions now.
    await lazy.ExtensionPermissions.add(id, migrated);

    // Now we need to update ExtensionPreferencesManager, removing any settings
    // for old permissions that no longer exist.
    let permSet = new Set(
      newPermissions.permissions.concat(newOptionalPermissions.permissions)
    );
    let oldPerms = oldPermissions.permissions.concat(
      oldOptionalPermissions.permissions
    );

    let removed = oldPerms.filter(x => !permSet.has(x));
    // Force the removal here to ensure the settings are removed prior
    // to startup.  This will remove both required or optional permissions,
    // whereas the call from within ExtensionPermissions would only result
    // in a removal for optional permissions that were removed.
    await lazy.ExtensionPreferencesManager.removeSettingsForPermissions(
      id,
      removed
    );

    // Remove any optional permissions that have been removed from the manifest.
    await lazy.ExtensionPermissions.remove(id, {
      permissions: removed,
      origins: [],
    });
  }

  canUseAPIExperiment() {
    return (
      this.type == "extension" &&
      (this.isPrivileged ||
        // TODO(Bug 1771341): Allowing the "experiment_apis" property when only
        // AddonSettings.EXPERIMENTS_ENABLED is true is currently needed to allow,
        // while running under automation, the test harness extensions (like mochikit
        // and specialpowers) to use that privileged manifest property.
        lazy.AddonSettings.EXPERIMENTS_ENABLED)
    );
  }

  canUseThemeExperiment() {
    return (
      ["extension", "theme"].includes(this.type) &&
      (this.isPrivileged ||
        // "theme_experiment" MDN docs are currently explicitly mentioning this is expected
        // to be allowed also for non-signed extensions installed non-temporarily on builds
        // where the signature checks can be disabled).
        //
        // NOTE: be careful to don't regress "theme_experiment" (see Bug 1773076) while changing
        // AddonSettings.EXPERIMENTS_ENABLED (e.g. as part of fixing Bug 1771341).
        lazy.AddonSettings.EXPERIMENTS_ENABLED)
    );
  }

  get manifestVersion() {
    return this.manifest.manifest_version;
  }

  get workerBackground() {
    const background = this.manifest.background;

    const hasServiceWorker =
      background?.service_worker &&
      WebExtensionPolicy.backgroundServiceWorkerEnabled;
    if (!hasServiceWorker) {
      return false;
    }

    const hasDocument = background.scripts || background.page;
    if (!hasDocument) {
      return true;
    }

    // assurance: both "document" and "service_worker" environment specified in manifest

    for (let environment of background.preferred_environment || []) {
      if (environment === "document") {
        return false;
      }
      if (environment === "service_worker") {
        return true;
      }
    }

    // When not specified, prefer the the "document" environment
    // aka event page by default. This is consistent with Safari 18.

    return false;
  }

  get persistentBackground() {
    if (
      !this.manifest.background ||
      this.manifestVersion > 2 ||
      this.workerBackground
    ) {
      return false;
    }
    // V2 addons can only use event pages if the pref is also flipped and
    // persistent is explicilty set to false.
    return !this.eventPagesEnabled || this.manifest.background.persistent;
  }

  /**
   * backgroundState can be starting, running, suspending or stopped.
   * It is undefined if the extension has no background page.
   * See ext-backgroundPage.js for more details.
   *
   * @param {string} state starting, running, suspending or stopped
   */
  set backgroundState(state) {
    this._backgroundState = state;
  }

  get backgroundState() {
    return this._backgroundState;
  }

  async getExtensionVersionWithoutValidation() {
    return (await this.readJSON("manifest.json")).version;
  }

  /**
   * Load a locale and return a localized manifest.  The extension must
   * be initialized, and manifest parsed prior to calling.
   *
   * @param {string} locale to load, if necessary.
   * @returns {Promise<object>} normalized manifest.
   */
  async getLocalizedManifest(locale) {
    if (!this.type || !this.localeData) {
      throw new Error("The extension has not been initialized.");
    }
    // Upon update or reinstall, the Extension.manifest may be read from
    // StartupCache.manifest, however rawManifest is *not*.  We need the
    // raw manifest in order to get a localized manifest.
    if (!this.rawManifest) {
      this.rawManifest = await this.readJSON("manifest.json");
    }

    if (!this.localeData.has(locale)) {
      // Locales are not avialable until some additional
      // initialization is done.  We could just call initAllLocales,
      // but that is heavy handed, especially when we likely only
      // need one out of 20.
      let locales = await this.promiseLocales();
      if (locales.get(locale)) {
        await this.initLocale(locale);
      }
      if (!this.localeData.has(locale)) {
        throw new Error(`The extension does not contain the locale ${locale}`);
      }
    }
    let normalized = await this._getNormalizedManifest(locale);
    if (normalized.error) {
      throw new Error(normalized.error);
    }
    return normalized.value;
  }

  async _getNormalizedManifest(locale) {
    let manifestType = manifestTypes.get(this.type);

    let context = {
      url: this.baseURI && this.baseURI.spec,
      principal: this.principal,
      logError: error => {
        this.manifestWarning(error);
      },
      preprocessors: {},
      manifestVersion: this.manifestVersion,
      // We introduced this context param in Bug 1831417.
      ignoreUnrecognizedProperties: false,
    };

    if (this.fluentL10n || this.localeData) {
      context.preprocessors.localize = value => this.localize(value, locale);
    }

    return lazy.Schemas.normalize(this.rawManifest, manifestType, context);
  }

  #parseBrowserStyleInManifest(manifest, manifestKey, defaultValueInMV2) {
    const obj = manifest[manifestKey];
    if (!obj) {
      return;
    }
    const browserStyleIsVoid = obj.browser_style == null;
    obj.browser_style ??= defaultValueInMV2;
    if (this.manifestVersion < 3 || !obj.browser_style) {
      // MV2 (true or false), or MV3 (false set explicitly or default false).
      // No changes in observed behavior, return now to avoid logspam.
      return;
    }
    // Now there are two cases (MV3 only):
    // - browser_style was not specified, but defaults to true.
    // - browser_style was set to true by the extension.
    //
    // These will eventually be deprecated. For the deprecation plan, see
    // https://bugzilla.mozilla.org/show_bug.cgi?id=1827910#c1
    let warning;
    if (!lazy.browserStyleMV3supported) {
      obj.browser_style = false;
      if (browserStyleIsVoid && !lazy.browserStyleMV3sameAsMV2) {
        // defaultValueInMV2 is true, but there was no intent to use these
        // defaults. Don't warn.
        return;
      }
      warning = `"browser_style:true" is no longer supported in Manifest Version 3.`;
    } else {
      warning = `"browser_style:true" has been deprecated in Manifest Version 3 and will be unsupported in the near future.`;
    }
    if (browserStyleIsVoid) {
      warning += ` While "${manifestKey}.browser_style" was not explicitly specified in manifest.json, its default value was true.`;
      if (!lazy.browserStyleMV3sameAsMV2) {
        obj.browser_style = false;
        warning += ` The default value of "${manifestKey}.browser_style" has changed from true to false in Manifest Version 3.`;
      } else {
        warning += ` Its default will change to false in Manifest Version 3 starting from Firefox 115.`;
      }
    }

    this.manifestWarning(
      `Warning processing ${manifestKey}.browser_style: ${warning}`
    );
  }

  // AMO enforces a maximum length of 45 on the name since at least 2017, via
  // https://github.com/mozilla/addons-linter/blame/c4507688899aaafe29c522f1b1aec94b78b8a095/src/schema/updates/manifest.json#L111
  // added in https://github.com/mozilla/addons-linter/pull/1169
  // To avoid breaking add-ons that do not go through AMO (e.g. temporarily
  // loaded extensions), we enforce the limit by truncating and warning if
  // needed, instead enforcing a maxLength on "name" in schemas/manifest.json.
  //
  // We set the limit to 75, which is a safe limit that matches the CWS,
  // see https://bugzilla.mozilla.org/show_bug.cgi?id=1939087#c5
  static EXT_NAME_MAX_LEN = 75;

  async initializeAddonTypeAndID() {
    if (this.type) {
      // Already initialized.
      return;
    }
    this.rawManifest = await this.readJSON("manifest.json");
    let manifest = this.rawManifest;

    if (manifest.theme) {
      this.type = "theme";
    } else if (manifest.langpack_id) {
      this.type = "locale";
    } else if (manifest.dictionaries) {
      this.type = "dictionary";
    } else {
      this.type = "extension";
    }

    if (!this.id) {
      let bss =
        manifest.browser_specific_settings?.gecko ||
        manifest.applications?.gecko;
      let id = bss?.id;
      // This is a basic type check.
      // When parseManifest is called, the ID is validated more thoroughly
      // because the id is defined to be an ExtensionID type in
      // toolkit/components/extensions/schemas/manifest.json
      if (typeof id == "string") {
        this.id = id;
      }
    }
  }

  // eslint-disable-next-line complexity
  async parseManifest() {
    await Promise.all([this.initializeAddonTypeAndID(), Management.lazyInit()]);

    let manifest = this.rawManifest;
    this.manifest = manifest;

    if (manifest.default_locale) {
      await this.initLocale();
    }

    if (manifest.l10n_resources) {
      if (this.isPrivileged) {
        // TODO (Bug 1733466): For historical reasons fluent isn't being used to
        // localize manifest properties read from the add-on manager (e.g., author,
        // homepage, etc.), the changes introduced by Bug 1734987 does now ensure
        // that isPrivileged will be set while parsing the manifest and so this
        // can be now supported but requires some additional changes, being tracked
        // by Bug 1733466.
        if (this.constructor != ExtensionData) {
          this.fluentL10n = new Localization(manifest.l10n_resources, true);
        }
      } else if (this.temporarilyInstalled) {
        this.manifestError(
          `Using 'l10n_resources' requires a privileged add-on. ` +
            PRIVILEGED_ADDONS_DEVDOCS_MESSAGE
        );
      } else {
        // Warn but don't make this fatal.
        this.manifestWarning(
          "Ignoring l10n_resources in unprivileged extension"
        );
      }
    }

    let normalized = await this._getNormalizedManifest();
    if (normalized.error) {
      this.manifestError(normalized.error);
      return null;
    }

    manifest = normalized.value;

    const isMV2 = this.manifestVersion < 3;

    // `browser_specific_settings` is the recommended key to use in the
    // manifest, and the only possible choice in MV3+. For MV2 extensions, we
    // still allow `applications`, though. Because `applications` used to be
    // the only key in the distant past, most internal code is written using
    // applications. That's why we end up re-assigning `browser_specific_settings`
    // to `applications` below.
    //
    // Also, when a MV3+ extension specifies `applications`, the key isn't
    // recognized and therefore filtered out from the normalized manifest as
    // part of the JSONSchema normalization.
    if (manifest.browser_specific_settings?.gecko) {
      if (manifest.applications) {
        this.manifestWarning(
          `"applications" property ignored and overridden by "browser_specific_settings"`
        );
      }
      manifest.applications = manifest.browser_specific_settings;
    }

    // On Android, override the browser specific settings with those found in
    // `bss.gecko_android`, if any.
    //
    // It is also worth noting that the `gecko_android` key in `applications`
    // is marked as "unsupported" in the JSON schema.
    if (
      AppConstants.platform == "android" &&
      manifest.browser_specific_settings?.gecko_android
    ) {
      const { strict_min_version, strict_max_version } =
        manifest.browser_specific_settings.gecko_android;

      // When the manifest doesn't define `browser_specific_settings.gecko`, it
      // is still possible to reach this block but `manifest.applications`
      // won't be defined yet.
      if (!manifest?.applications) {
        manifest.applications = {
          // All properties should be optional in `gecko` so we omit them here.
          gecko: {},
        };
      }

      if (strict_min_version?.length) {
        manifest.applications.gecko.strict_min_version = strict_min_version;
      }

      if (strict_max_version?.length) {
        manifest.applications.gecko.strict_max_version = strict_max_version;
      }
    }

    if (manifest.name.length > ExtensionData.EXT_NAME_MAX_LEN) {
      // Truncate and warn - see comment in EXT_NAME_MAX_LEN.
      manifest.name = manifest.name.slice(0, ExtensionData.EXT_NAME_MAX_LEN);
      this.manifestWarning(
        `Warning processing "name": must be shorter than ${ExtensionData.EXT_NAME_MAX_LEN}`
      );
    }

    if (manifest.background) {
      const background = manifest.background;

      if (background.page && background.scripts) {
        // both page and scripts are specified, educate the author on the deterministic behaviour
        // Note: in Chrome and Safari, the precedence is inverted.
        this.manifestWarning(
          `Warning processing background: Both background.page and background.scripts specified. background.scripts will be ignored.`
        );
      }

      // take the presence of preferred_environment as clue the author knows what it is doing
      if (
        !background.preferred_environment &&
        background.service_worker &&
        (background.page || background.scripts) &&
        WebExtensionPolicy.backgroundServiceWorkerEnabled
      ) {
        // both serviceWorker and document are specified, educate the author on the deterministic behaviour
        const documentType = background.page ? "page" : "scripts";
        this.manifestWarning(
          `Warning processing background: with both background.service_worker and background.${documentType}, only background.${documentType} will be loaded. This can be changed with background.preferred_environment.`
        );
      }

      if (
        this.manifestVersion < 3 &&
        !this.eventPagesEnabled &&
        !background.persistent
      ) {
        this.logWarning("Event pages are not currently supported.");
      }
    }

    if (
      this.isPrivileged &&
      manifest.hidden &&
      (manifest.action || manifest.browser_action || manifest.page_action)
    ) {
      this.manifestError(
        "Cannot use browser and/or page actions in hidden add-ons"
      );
    }

    // manifest.options_page opens the extension page in a new tab
    // and so we will not need to special handling browser_style.
    if (manifest.options_ui) {
      if (manifest.options_ui.open_in_tab) {
        // browser_style:true has no effect when open_in_tab is true.
        manifest.options_ui.browser_style = false;
      } else {
        this.#parseBrowserStyleInManifest(manifest, "options_ui", true);
      }
    }
    if (this.manifestVersion < 3) {
      this.#parseBrowserStyleInManifest(manifest, "browser_action", false);
    } else {
      this.#parseBrowserStyleInManifest(manifest, "action", false);
    }
    this.#parseBrowserStyleInManifest(manifest, "page_action", false);
    if (AppConstants.MOZ_BUILD_APP === "browser") {
      this.#parseBrowserStyleInManifest(manifest, "sidebar_action", true);
    }

    let apiNames = new Set();
    let dependencies = new Set();
    let originPermissions = new Set();
    let permissions = new Set();
    let dataCollectionPermissions = new Set();
    let webAccessibleResources = [];

    let schemaPromises = new Map();

    // Note: this.id and this.type were computed in initializeAddonTypeAndID.
    // The format of `this.id` was confirmed to be a valid extensionID by the
    // Schema validation as part of the _getNormalizedManifest() call.
    let result = {
      apiNames,
      dependencies,
      id: this.id,
      manifest,
      modules: null,
      // Whether to treat all origin permissions (including content scripts)
      // from the manifestas as optional, and enable users to control them.
      originControls: this.manifestVersion >= 3 && this.type === "extension",
      originPermissions,
      permissions,
      dataCollectionPermissions,
      schemaURLs: null,
      type: this.type,
      webAccessibleResources,
    };

    if (this.type === "extension") {
      let { isPrivileged } = this;
      let restrictSchemes = !(
        isPrivileged && manifest.permissions.includes("mozillaAddons")
      );

      // Privileged and temporary extensions still get OriginControls, but
      // can have host permissions automatically granted during install.
      // For all other cases, ensure granted_host_permissions is false.
      if (!isPrivileged && !this.temporarilyInstalled) {
        manifest.granted_host_permissions = false;
      }

      let host_permissions = manifest.host_permissions ?? [];

      for (let perm of manifest.permissions.concat(host_permissions)) {
        if (perm === "geckoProfiler" && !isPrivileged) {
          const acceptedExtensions = Services.prefs.getStringPref(
            "extensions.geckoProfiler.acceptedExtensionIds",
            ""
          );
          if (!acceptedExtensions.split(",").includes(this.id)) {
            this.manifestError(
              "Only specific extensions are allowed to access the geckoProfiler."
            );
            continue;
          }
        }

        let type = classifyPermission(perm, restrictSchemes, isPrivileged);
        if (type.origin) {
          perm = type.origin;
          if (!result.originControls) {
            originPermissions.add(perm);
          }
        } else if (type.api) {
          apiNames.add(type.api);
        } else if (type.invalid) {
          // If EXPERIMENTS_ENABLED is not enabled prevent the install
          // to ensure developer awareness.
          if (this.temporarilyInstalled && type.privileged) {
            this.manifestError(
              `Using the privileged permission '${perm}' requires a privileged add-on. ` +
                PRIVILEGED_ADDONS_DEVDOCS_MESSAGE
            );
            continue;
          }
          this.manifestWarning(`Invalid extension permission: ${perm}`);
          continue;
        } else if (type.permission && isMV2 && PERMS_NOT_IN_MV2.has(perm)) {
          this.manifestWarning(
            `Permission "${perm}" requires Manifest Version 3.`
          );
          continue;
        }

        // Unfortunately, we treat <all_urls> as an API permission as well.
        if (!type.origin || (perm === "<all_urls>" && !result.originControls)) {
          permissions.add(perm);
        }
      }

      const shouldIgnorePermission = (perm, verbose = true) => {
        if (perm === "userScripts" && !lazy.userScriptsMV3Enabled) {
          if (verbose) {
            this.manifestWarning(`Unavailable extension permission: ${perm}`);
          }
          return true;
        }
        if (isMV2 && PERMS_NOT_IN_MV2.has(perm)) {
          if (verbose) {
            this.manifestWarning(
              `Permission "${perm}" requires Manifest Version 3.`
            );
          }
          return true;
        }
        return false;
      };

      for (let i = manifest.optional_permissions.length - 1; i >= 0; --i) {
        if (shouldIgnorePermission(manifest.optional_permissions[i])) {
          manifest.optional_permissions.splice(i, 1);
        }
      }

      if (lazy.dataCollectionPermissionsEnabled) {
        const { required } = this.getDataCollectionPermissions(manifest);

        for (const permission of required.filter(perm => perm !== "none")) {
          dataCollectionPermissions.add(permission);
        }
      }

      if (this.id) {
        // An extension always gets permission to its own url.
        let matcher = new MatchPattern(this.getURL(), { ignorePath: true });
        originPermissions.add(matcher.pattern);

        // Apply optional permissions
        // TODO bug 1766915: Validate that the permissions are available.
        let perms = await lazy.ExtensionPermissions.get(this.id);
        for (let perm of perms.permissions) {
          if (shouldIgnorePermission(perm, /* verbose */ false)) {
            continue;
          }
          permissions.add(perm);
        }
        for (let origin of perms.origins) {
          originPermissions.add(origin);
        }
        for (let perm of perms.data_collection) {
          dataCollectionPermissions.add(perm);
        }
      }

      for (let api of apiNames) {
        dependencies.add(`${api}@experiments.addons.mozilla.org`);
      }

      let moduleData = data => ({
        url: this.rootURI.resolve(data.script),
        events: data.events,
        paths: data.paths,
        scopes: data.scopes,
      });

      let computeModuleInit = (scope, modules) => {
        let manager = new ExtensionCommon.SchemaAPIManager(scope);
        return manager.initModuleJSON([modules]);
      };

      result.contentScripts = [];
      for (let options of manifest.content_scripts || []) {
        let { match_about_blank, match_origin_as_fallback } = options;
        if (match_origin_as_fallback !== null) {
          // match_about_blank is ignored when match_origin_as_fallback is set.
          // When match_about_blank=true and match_origin_as_fallback=false,
          // then match_about_blank should be treated as false.
          match_about_blank = false;
        }
        result.contentScripts.push({
          allFrames: options.all_frames,
          matchAboutBlank: match_about_blank,
          matchOriginAsFallback: match_origin_as_fallback,
          frameID: options.frame_id,
          runAt: options.run_at,
          world: options.world,

          matches: options.matches,
          excludeMatches: options.exclude_matches || [],
          includeGlobs: options.include_globs,
          excludeGlobs: options.exclude_globs,

          jsPaths: options.js || [],
          cssPaths: options.css || [],
        });
      }

      if (manifest.experiment_apis) {
        if (this.canUseAPIExperiment()) {
          let parentModules = {};
          let childModules = {};

          for (let [name, data] of Object.entries(manifest.experiment_apis)) {
            let schema = this.getURL(data.schema);

            if (!schemaPromises.has(schema)) {
              schemaPromises.set(
                schema,
                this.readJSON(data.schema).then(json =>
                  lazy.Schemas.processSchema(json)
                )
              );
            }

            if (data.parent) {
              parentModules[name] = moduleData(data.parent);
            }

            if (data.child) {
              childModules[name] = moduleData(data.child);
            }
          }

          result.modules = {
            child: computeModuleInit("addon_child", childModules),
            parent: computeModuleInit("addon_parent", parentModules),
          };
        } else if (this.temporarilyInstalled) {
          // Hard error for un-privileged temporary installs using experimental apis.
          this.manifestError(
            `Using 'experiment_apis' requires a privileged add-on. ` +
              PRIVILEGED_ADDONS_DEVDOCS_MESSAGE
          );
        } else {
          this.manifestWarning(
            `Using experimental APIs requires a privileged add-on.`
          );
        }
      }

      // Normalize all patterns to contain a single leading /
      if (manifest.web_accessible_resources) {
        // Normalize into V3 objects
        let wac =
          this.manifestVersion >= 3
            ? manifest.web_accessible_resources
            : [{ resources: manifest.web_accessible_resources }];
        webAccessibleResources.push(
          ...wac.map(obj => {
            obj.resources = obj.resources.map(path =>
              path.replace(/^\/*/, "/")
            );
            return obj;
          })
        );
      }
    } else if (this.type == "locale") {
      // Langpack startup is performance critical, so we want to compute as much
      // as possible here to make startup not trigger async DB reads.
      // We'll store the four items below in the startupData.

      // 1. Compute the chrome resources to be registered for this langpack.
      const platform = AppConstants.platform;
      const chromeEntries = [];
      for (const [language, entry] of Object.entries(manifest.languages)) {
        for (const [alias, path] of Object.entries(
          entry.chrome_resources || {}
        )) {
          if (typeof path === "string") {
            chromeEntries.push(["locale", alias, language, path]);
          } else if (platform in path) {
            // If the path is not a string, it's an object with path per
            // platform where the keys are taken from AppConstants.platform
            chromeEntries.push(["locale", alias, language, path[platform]]);
          }
        }
      }

      // 2. Compute langpack ID.
      const productCodeName = AppConstants.MOZ_BUILD_APP.replace("/", "-");

      // The result path looks like this:
      //   Firefox - `langpack-pl-browser`
      //   Fennec - `langpack-pl-mobile-android`
      const langpackId = `langpack-${manifest.langpack_id}-${productCodeName}`;

      // 3. Compute L10nRegistry sources for this langpack.
      const l10nRegistrySources = {};

      // Check if there's a root directory `/localization` in the langpack.
      // If there is one, add it with the name `toolkit` as a FileSource.
      const entries = await this._readDirectory("localization");
      if (entries.length) {
        l10nRegistrySources.toolkit = "";
      }

      // Add any additional sources listed in the manifest
      if (manifest.sources) {
        for (const [sourceName, { base_path }] of Object.entries(
          manifest.sources
        )) {
          l10nRegistrySources[sourceName] = base_path;
        }
      }

      // 4. Save the list of languages handled by this langpack.
      const languages = Object.keys(manifest.languages);

      this.startupData = {
        chromeEntries,
        langpackId,
        l10nRegistrySources,
        languages,
      };
    } else if (this.type == "dictionary") {
      let dictionaries = {};
      for (let [lang, path] of Object.entries(manifest.dictionaries)) {
        path = path.replace(/^\/+/, "");

        let dir = dirname(path);
        if (dir === ".") {
          dir = "";
        }
        let leafName = basename(path);
        let affixPath = leafName.slice(0, -3) + "aff";

        let entries = await this._readDirectory(dir);
        if (!entries.includes(leafName)) {
          this.manifestError(
            `Invalid dictionary path specified for '${lang}': ${path}`
          );
        }
        if (!entries.includes(affixPath)) {
          this.manifestError(
            `Invalid dictionary path specified for '${lang}': Missing affix file: ${path}`
          );
        }

        dictionaries[lang] = path;
      }

      this.startupData = { dictionaries };
    }

    if (schemaPromises.size) {
      let schemas = new Map();
      for (let [url, promise] of schemaPromises) {
        schemas.set(url, await promise);
      }
      result.schemaURLs = schemas;
    }

    return result;
  }

  // Reads the extension's |manifest.json| file, and stores its
  // parsed contents in |this.manifest|.
  async loadManifest() {
    let [manifestData] = await Promise.all([
      this.parseManifest(),
      Management.lazyInit(),
    ]);

    if (!manifestData) {
      return;
    }

    // Do not override the add-on id that has been already assigned.
    if (!this.id) {
      this.id = manifestData.id;
    }

    this.manifest = manifestData.manifest;
    this.apiNames = manifestData.apiNames;
    this.contentScripts = manifestData.contentScripts;
    this.dependencies = manifestData.dependencies;
    this.permissions = manifestData.permissions;
    this.schemaURLs = manifestData.schemaURLs;
    this.type = manifestData.type;

    this.modules = manifestData.modules;

    this.apiManager = this.getAPIManager();
    await this.apiManager.lazyInit();

    this.webAccessibleResources = manifestData.webAccessibleResources;

    this.originControls = manifestData.originControls;
    this.allowedOrigins = new MatchPatternSet(manifestData.originPermissions, {
      restrictSchemes: this.restrictSchemes,
    });
    this.dataCollectionPermissions = manifestData.dataCollectionPermissions;

    return this.manifest;
  }

  hasPermission(perm, includeOptional = false) {
    // If the permission is a "manifest property" permission, we check if the extension
    // does have the required property in its manifest.
    let manifest_ = "manifest:";
    if (perm.startsWith(manifest_)) {
      // Handle nested "manifest property" permission (e.g. as in "manifest:property.nested").
      let value = this.manifest;
      for (let prop of perm.substr(manifest_.length).split(".")) {
        if (!value) {
          break;
        }
        value = value[prop];
      }

      return value != null;
    }

    if (this.permissions.has(perm)) {
      return true;
    }

    if (includeOptional && this.manifest.optional_permissions.includes(perm)) {
      return true;
    }

    return false;
  }

  getAPIManager() {
    /** @type {(InstanceType<typeof ExtensionCommon.LazyAPIManager>)[]} */
    let apiManagers = [Management];

    for (let id of this.dependencies) {
      let policy = WebExtensionPolicy.getByID(id);
      if (policy) {
        if (policy.extension.experimentAPIManager) {
          apiManagers.push(policy.extension.experimentAPIManager);
        } else if (AppConstants.DEBUG) {
          Cu.reportError(`Cannot find experimental API exported from ${id}`);
        }
      }
    }

    if (this.modules) {
      this.experimentAPIManager = new ExtensionCommon.LazyAPIManager(
        "main",
        this.modules.parent,
        this.schemaURLs
      );

      apiManagers.push(this.experimentAPIManager);
    }

    if (apiManagers.length == 1) {
      return apiManagers[0];
    }

    return new ExtensionCommon.MultiAPIManager("main", apiManagers.reverse());
  }

  localizeMessage(...args) {
    return this.localeData.localizeMessage(...args);
  }

  localize(str, locale) {
    // If the extension declares fluent resources in the manifest, try
    // first to localize with fluent.  Also use the original webextension
    // method (_locales/xx.json) so extensions can migrate bit by bit.
    // Note also that fluent keys typically use hyphense, so hyphens are
    // allowed in the __MSG_foo__ keys used by fluent, though they are
    // not allowed in the keys used for json translations.
    if (this.fluentL10n) {
      str = str.replace(/__MSG_([-A-Za-z0-9@_]+?)__/g, (matched, message) => {
        let translation = this.fluentL10n.formatValueSync(message);
        return translation !== undefined ? translation : matched;
      });
    }
    if (this.localeData) {
      str = this.localeData.localize(str, locale);
    }
    return str;
  }

  // If a "default_locale" is specified in that manifest, returns it
  // as a Gecko-compatible locale string. Otherwise, returns null.
  get defaultLocale() {
    if (this.manifest.default_locale != null) {
      return this.normalizeLocaleCode(this.manifest.default_locale);
    }

    return null;
  }

  // Returns true if an addon is builtin to Firefox or
  // distributed via Normandy into a system location.
  get isAppProvided() {
    return this.addonData.builtIn || this.addonData.isSystem;
  }

  get isHidden() {
    return (
      this.addonData.locationHidden ||
      (this.isPrivileged && this.manifest.hidden)
    );
  }

  // Normalizes a Chrome-compatible locale code to the appropriate
  // Gecko-compatible variant. Currently, this means simply
  // replacing underscores with hyphens.
  normalizeLocaleCode(locale) {
    return locale.replace(/_/g, "-");
  }

  // Reads the locale file for the given Gecko-compatible locale code, and
  // stores its parsed contents in |this.localeMessages.get(locale)|.
  async readLocaleFile(locale) {
    let locales = await this.promiseLocales();
    let dir = locales.get(locale) || locale;
    let file = `_locales/${dir}/messages.json`;

    try {
      let messages = await this.readJSON(file);
      return this.localeData.addLocale(locale, messages, this);
    } catch (e) {
      this.packagingError(`Loading locale file ${file}: ${e}`);
      return new Map();
    }
  }

  async _promiseLocaleMap() {
    let locales = new Map();

    let entries = await this._readDirectory("_locales", true);
    for (let name of entries) {
      let locale = this.normalizeLocaleCode(name);
      locales.set(locale, name);
    }

    return locales;
  }

  _setupLocaleData(locales) {
    if (this.localeData) {
      return this.localeData.locales;
    }

    this.localeData = new ExtensionCommon.LocaleData({
      defaultLocale: this.defaultLocale,
      locales,
      builtinMessages: this.builtinMessages,
    });

    return locales;
  }

  // Reads the list of locales available in the extension, and returns a
  // Promise which resolves to a Map upon completion.
  // Each map key is a Gecko-compatible locale code, and each value is the
  // "_locales" subdirectory containing that locale:
  //
  // Map(gecko-locale-code -> locale-directory-name)
  promiseLocales() {
    if (!this._promiseLocales) {
      this._promiseLocales = (async () => {
        let locales = this._promiseLocaleMap();
        return this._setupLocaleData(locales);
      })();
    }

    return this._promiseLocales;
  }

  // Reads the locale messages for all locales, and returns a promise which
  // resolves to a Map of locale messages upon completion. Each key in the map
  // is a Gecko-compatible locale code, and each value is a locale data object
  // as returned by |readLocaleFile|.
  async initAllLocales() {
    let locales = await this.promiseLocales();

    await Promise.all(
      Array.from(locales.keys(), locale => this.readLocaleFile(locale))
    );

    let defaultLocale = this.defaultLocale;
    if (defaultLocale) {
      if (!locales.has(defaultLocale)) {
        this.manifestError(
          'Value for "default_locale" property must correspond to ' +
            'a directory in "_locales/". Not found: ' +
            JSON.stringify(`_locales/${this.manifest.default_locale}/`)
        );
      }
    } else if (locales.size) {
      this.manifestError(
        'The "default_locale" property is required when a ' +
          '"_locales/" directory is present.'
      );
    }

    return this.localeData.messages;
  }

  // Reads the locale file for the given Gecko-compatible locale code, or the
  // default locale if no locale code is given, and sets it as the currently
  // selected locale on success.
  //
  // Pre-loads the default locale for fallback message processing, regardless
  // of the locale specified.
  async initLocale(locale = this.defaultLocale) {
    if (locale == null) {
      return null;
    }

    const availableMessageFileLocales = await this.promiseLocales();

    const localesToLoad = ExtensionCommon.LocaleData.listLocaleVariations(
      locale
    ).filter(item => availableMessageFileLocales.has(item));

    const { defaultLocale } = this;
    if (!localesToLoad.includes(defaultLocale)) {
      localesToLoad.push(defaultLocale);
    }

    await Promise.all(
      localesToLoad.map(item => {
        // Avoid loading locales that we have already read before.
        return !this.localeData.has(item) && this.readLocaleFile(item);
      })
    );

    this.localeData.selectedLocale = locale;
  }

  /**
   * @param {string} origin
   * @returns {boolean}       If this is one of the "all sites" permission.
   */
  static isAllSitesPermission(origin) {
    try {
      let info = ExtensionData.classifyOriginPermissions([origin], true);
      return !!info.allUrls;
    } catch (e) {
      // Passed string is not an origin permission.
      return false;
    }
  }

  /**
   * @typedef {object} HostPermissions
   * @param {string} allUrls   permission used to obtain all urls access
   * @param {Set} wildcards    set contains permissions with wildcards
   * @param {Set} sites        set contains explicit host permissions
   * @param {Map} wildcardsMap mapping origin wildcards to labels
   * @param {Map} sitesMap     mapping origin patterns to labels
   */

  /**
   * Classify host permissions
   *
   * @param {Array<string>} origins
   *                        permission origins
   * @param {boolean}       ignoreNonWebSchemes
   *                        return only these schemes: *, http, https, ws, wss
   *
   * @returns {HostPermissions}
   */
  static classifyOriginPermissions(origins = [], ignoreNonWebSchemes = false) {
    let allUrls = null,
      wildcards = new Set(),
      sites = new Set(),
      // TODO: use map.values() instead of these sets.  Note: account for two
      // match patterns producing the same permission string, see bug 1765828.
      wildcardsMap = new Map(),
      sitesMap = new Map();

    // https://searchfox.org/mozilla-central/rev/6f6cf28107/toolkit/components/extensions/MatchPattern.cpp#235
    const wildcardSchemes = ["*", "http", "https", "ws", "wss"];

    for (let permission of origins) {
      if (permission == "<all_urls>") {
        allUrls = permission;
        continue;
      }

      // Privileged extensions may request access to "about:"-URLs, such as
      // about:reader.
      let match = /^([a-z*]+):\/\/([^/]*)\/|^about:/.exec(permission);
      if (!match) {
        throw new Error(`Unparseable host permission ${permission}`);
      }

      // Note: the scheme is ignored in the permission warnings. If this ever
      // changes, update the comparePermissions method as needed.
      let [, scheme, host] = match;
      if (ignoreNonWebSchemes && !wildcardSchemes.includes(scheme)) {
        continue;
      }

      if (!host || host == "*") {
        if (!allUrls) {
          allUrls = permission;
        }
      } else if (host.startsWith("*.")) {
        wildcards.add(host.slice(2));
        // Using MatchPattern to normalize the pattern string.
        let pat = new MatchPattern(permission, { ignorePath: true });
        wildcardsMap.set(pat.pattern, `${scheme}://${host.slice(2)}`);
      } else {
        sites.add(host);
        let pat = new MatchPattern(permission, {
          ignorePath: true,
          // Safe because used just for normalization, not for granting access.
          restrictSchemes: false,
        });
        sitesMap.set(pat.pattern, `${scheme}://${host}`);
      }
    }
    return { allUrls, wildcards, sites, wildcardsMap, sitesMap };
  }

  /**
   * @typedef {object} Permissions
   * @property {Array<string>} origins Origin permissions.
   * @property {Array<string>} permissions Regular (non-origin) permissions.
   * @property {Array<string>} data_collection Data collection permissions.
   */

  /**
   * Formats all the strings for a permissions dialog/notification.
   *
   * @param {object} info Information about the permissions being requested.
   *
   * @param {object} [info.addon] Optional information about the addon.
   * @param {Permissions} [info.optionalPermissions]
   *                      Optional permissions listed in the manifest.
   * @param {Permissions} info.permissions Requested permissions.
   * @param {string} info.siteOrigin
   * @param {Array<string>} [info.sitePermissions]
   * @param {boolean} info.unsigned
   *                  True if the prompt is for installing an unsigned addon.
   * @param {string} info.type
   *                 The type of prompt being shown.  May be one of "update",
   *                 "sideload", "optional", or omitted for a regular
   *                 install prompt.
   * @param {object} options
   * @param {boolean} [options.buildOptionalOrigins]
   *                  Wether to build optional origins Maps for permission
   *                  controls.  Defaults to false.
   * @param {boolean} [options.fullDomainsList]
   *                  Wether to include the full domains set in the returned
   *                  results.  Defaults to false.
   *
   * @typedef {object} PermissionStrings
   * @property {Array<string>} msgs an array of localized strings describing
   * required permissions
   * @property {Record<string, string>} optionalPermissions a map of permission
   * name to localized strings describing the permission
   * @property {Record<string, string>} optionalOrigins a map of a host
   * permission to localized strings describing the host permission, where
   * appropriate. Currently only all url style permissions are included
   * @property {string} text a localized string
   * @property {string} listIntro a localized string that should be displayed
   * before the list of permissions
   * @property {{ msg?: string, collectsTechnicalAndInteractionData?: boolean }} dataCollectionPermissions
   * an object containing information about data permissions to be displayed.
   * It contains a message string, and whether the extension collects technical
   * and interaction data, which needs to be handled differently
   * @property {Record<string, string>} optionalDataCollectionPermissions a map
   * of data collection permission names to localized strings
   * @property {{ domainsSet: Set, msgIdIndex: number }=} fullDomainsList an
   * object with a Set of the full domains list (with the property name
   * "domainsSet") and the index of the corresponding message string (with the
   * property name "msgIdIndex"). This property is expected to be set only if
   * "options.fullDomainsList" is passed as true and the extension doesn't
   * include allUrls origin permissions
   *
   * @returns {PermissionStrings} An object with properties containing
   *                             localized strings for various elements of a
   *                             permission dialog. The "header" property on
   *                             this object is the notification header text
   *                             and it has the string "<>" as a placeholder
   *                             for the addon name.
   */
  static formatPermissionStrings(
    {
      addon,
      optionalPermissions,
      permissions,
      siteOrigin,
      sitePermissions,
      type,
      unsigned,
    },
    { buildOptionalOrigins = false, fullDomainsList = false } = {}
  ) {
    const l10n = lazy.PERMISSION_L10N;

    const msgIds = [];
    const headerArgs = { extension: "<>" };
    let acceptId = "webext-perms-add";
    let cancelId = "webext-perms-cancel";

    const result = {
      msgs: [],
      optionalPermissions: {},
      optionalOrigins: {},
      text: "",
      listIntro: "",
      dataCollectionPermissions: {},
      optionalDataCollectionPermissions: {},
    };

    // To keep the label & accesskey in sync for localizations,
    // they need to be stored as attributes of the same Fluent message.
    // This unpacks them into the shape expected of them in `result`.
    function setAcceptCancel(acceptId, cancelId) {
      const haveAccessKeys = AppConstants.platform !== "android";

      const [accept, cancel] = l10n.formatMessagesSync([
        { id: acceptId },
        { id: cancelId },
      ]);

      for (let { name, value } of accept.attributes) {
        if (name === "label") {
          result.acceptText = value;
        } else if (name === "accesskey" && haveAccessKeys) {
          result.acceptKey = value;
        }
      }

      for (let { name, value } of cancel.attributes) {
        if (name === "label") {
          result.cancelText = value;
        } else if (name === "accesskey" && haveAccessKeys) {
          result.cancelKey = value;
        }
      }
    }

    // Synthetic addon install can only grant access to a single permission so we can have
    // a less-generic message than addons with site permissions.
    // NOTE: this is used as part of the synthetic addon install flow implemented for the
    // SitePermissionAddonProvider.
    // FIXME
    if (addon?.type === lazy.SITEPERMS_ADDON_TYPE) {
      // We simplify the origin to make it more user friendly. The origin is assured to be
      // available because the SitePermsAddon install is always expected to be triggered
      // from a website, making the siteOrigin always available through the installing principal.
      headerArgs.hostname = new URL(siteOrigin).hostname;

      // messages are specific to the type of gated permission being installed
      const headerId =
        sitePermissions[0] === "midi-sysex"
          ? "webext-site-perms-header-with-gated-perms-midi-sysex"
          : "webext-site-perms-header-with-gated-perms-midi";
      result.header = l10n.formatValueSync(headerId, headerArgs);

      // We use the same string for midi and midi-sysex, and don't support any
      // other types of site permission add-ons. So we just hard-code the
      // descriptor for now. See bug 1826747.
      result.text = l10n.formatValueSync(
        "webext-site-perms-description-gated-perms-midi"
      );

      setAcceptCancel(acceptId, cancelId);
      return result;
    }

    // NOTE: this is used as part of the synthetic addon implemented for the
    // SitePermissionAddonProvider to render the site permissions in the
    // about:addon detail view for the synthetic addon entries.
    if (sitePermissions) {
      for (let permission of sitePermissions) {
        let permMsg;
        switch (permission) {
          case "midi":
            permMsg = l10n.formatValueSync("webext-site-perms-midi");
            break;
          case "midi-sysex":
            permMsg = l10n.formatValueSync("webext-site-perms-midi-sysex");
            break;
          default:
            Cu.reportError(
              `site_permission ${permission} missing readable text property`
            );
            // We must never have a DOM api permission that is hidden so in
            // the case of any error, we'll use the plain permission string.
            // test_ext_sitepermissions.js tests for no missing messages, this
            // is just an extra fallback.
            permMsg = permission;
        }
        result.msgs.push(permMsg);
      }

      // We simplify the origin to make it more user friendly.  The origin is
      // assured to be available via schema requirement.
      headerArgs.hostname = new URL(siteOrigin).hostname;

      const headerId = unsigned
        ? "webext-site-perms-header-unsigned-with-perms"
        : "webext-site-perms-header-with-perms";
      result.header = l10n.formatValueSync(headerId, headerArgs);
      setAcceptCancel(acceptId, cancelId);
      return result;
    }

    if (permissions) {
      // First classify our host permissions
      let { allUrls, wildcards, sites } =
        ExtensionData.classifyOriginPermissions(permissions.origins);

      // Format the host permissions.  If we have a wildcard for all urls,
      // a single string will suffice.  Otherwise, show domain wildcards
      // first, then individual host permissions.
      if (allUrls) {
        msgIds.push("webext-perms-host-description-all-urls");
      } else if (!fullDomainsList) {
        // Formats a list of host permissions.  If we have 4 or fewer, display
        // them all, otherwise display the first 3 followed by an item that
        // says "...plus N others"
        const addMessages = (set, l10nId) => {
          for (let domain of set) {
            msgIds.push({ id: l10nId, args: { domain } });
          }
        };

        addMessages(wildcards, "webext-perms-host-description-wildcard");
        addMessages(sites, "webext-perms-host-description-one-site");
      }

      if (!allUrls && fullDomainsList) {
        const allHostPermissions = wildcards.union(sites);
        if (allHostPermissions.size > 1) {
          msgIds.push({
            id: "webext-perms-host-description-multiple-domains",
            args: {
              domainCount: allHostPermissions.size,
            },
          });
          result.fullDomainsList = {
            domainsSet: allHostPermissions,
            msgIdIndex: msgIds.length - 1,
          };
        } else if (allHostPermissions.size) {
          msgIds.push({
            id: "webext-perms-host-description-one-domain",
            args: {
              domain: Array.from(allHostPermissions)[0],
            },
          });
        }
      }

      // Finally, show remaining permissions, in the same order as AMO.
      // The permissions are sorted alphabetically by the permission
      // string to match AMO.
      // Show the native messaging permission first if it is present.
      const NATIVE_MSG_PERM = "nativeMessaging";
      const permissionsSorted = permissions.permissions.sort((a, b) => {
        if (a === NATIVE_MSG_PERM) {
          return -1;
        } else if (b === NATIVE_MSG_PERM) {
          return 1;
        }
        return a < b ? -1 : 1;
      });

      for (let permission of permissionsSorted) {
        const l10nId = lazy.permissionToL10nId(permission);
        // We deliberately do not include all permissions in the prompt.
        // So if we don't find one then just skip it.
        if (l10nId) {
          msgIds.push(l10nId);
        }
      }

      if (
        lazy.dataCollectionPermissionsEnabled &&
        permissions.data_collection?.length
      ) {
        result.dataCollectionPermissions =
          this._formatDataCollectionPermissions(
            permissions.data_collection,
            type
          );
      }
    }

    if (optionalPermissions) {
      // Generate a map of permission names to permission strings for optional
      // permissions.  The key is necessary to handle toggling those permissions.
      const opKeys = [];
      const opL10nIds = [];
      for (let permission of optionalPermissions.permissions) {
        const l10nId = lazy.permissionToL10nId(permission);
        // We deliberately do not include all permissions in the prompt.
        // So if we don't find one then just skip it.
        if (l10nId) {
          opKeys.push(permission);
          opL10nIds.push(l10nId);
        }
      }
      if (opKeys.length) {
        const opRes = l10n.formatValuesSync(opL10nIds);
        for (let i = 0; i < opKeys.length; ++i) {
          result.optionalPermissions[opKeys[i]] = opRes[i];
        }
      }

      const { allUrls, sitesMap, wildcardsMap } =
        ExtensionData.classifyOriginPermissions(
          optionalPermissions.origins,
          true
        );
      const ooKeys = [];
      const ooL10nIds = [];
      if (allUrls) {
        ooKeys.push(allUrls);
        ooL10nIds.push("webext-perms-host-description-all-urls");
      }

      // Current UX controls are meant for developer testing with mv3.
      if (buildOptionalOrigins) {
        for (let [pattern, domain] of wildcardsMap.entries()) {
          ooKeys.push(pattern);
          ooL10nIds.push({
            id: "webext-perms-host-description-wildcard",
            args: { domain },
          });
        }
        for (let [pattern, domain] of sitesMap.entries()) {
          ooKeys.push(pattern);
          ooL10nIds.push({
            id: "webext-perms-host-description-one-site",
            args: { domain },
          });
        }
      }

      if (ooKeys.length) {
        const res = l10n.formatValuesSync(ooL10nIds);
        for (let i = 0; i < res.length; ++i) {
          result.optionalOrigins[ooKeys[i]] = res[i];
        }
      }

      if (
        lazy.dataCollectionPermissionsEnabled &&
        optionalPermissions.data_collection?.length
      ) {
        result.optionalDataCollectionPermissions =
          this._formatOptionalDataCollectionPermissions(
            optionalPermissions.data_collection
          );
      }
    }

    const hasDataCollectionOnly =
      lazy.dataCollectionPermissionsEnabled &&
      msgIds.length === 0 &&
      result.dataCollectionPermissions.msg;

    switch (type) {
      case "sideload":
        acceptId = "webext-perms-sideload-enable";
        cancelId = "webext-perms-sideload-cancel";
        result.text = l10n.formatValueSync(
          msgIds.length
            ? "webext-perms-sideload-text"
            : "webext-perms-sideload-text-no-perms"
        );
        break;
      case "update": {
        acceptId = "webext-perms-update-accept";
        break;
      }
      case "optional": {
        acceptId = "webext-perms-optional-perms-allow";
        cancelId = "webext-perms-optional-perms-deny";
        if (!hasDataCollectionOnly) {
          result.listIntro = l10n.formatValueSync(
            "webext-perms-optional-perms-list-intro"
          );
        }
        break;
      }
      default:
    }

    result.header = l10n.formatValueSync(
      this._getHeaderFluentId({
        type,
        hasDataCollectionOnly,
        hasPermissions: msgIds.length,
        unsigned,
      }),
      headerArgs
    );
    result.msgs = l10n.formatValuesSync(msgIds);
    setAcceptCancel(acceptId, cancelId);
    return result;
  }

  /**
   * Helper function to return the right header fluent ID for a permission
   * prompt, depending on the type, whether it has permissions and/or data
   * collection only, and also whether the add-on is signed or not.
   */
  static _getHeaderFluentId({
    type,
    hasDataCollectionOnly,
    hasPermissions,
    unsigned,
  }) {
    switch (type) {
      case "sideload":
        return "webext-perms-sideload-header";

      case "update":
        if (!lazy.dataCollectionPermissionsEnabled) {
          return "webext-perms-update-text";
        }
        return hasDataCollectionOnly
          ? "webext-perms-update-data-collection-only-text"
          : "webext-perms-update-data-collection-text";

      case "optional":
        if (!lazy.dataCollectionPermissionsEnabled) {
          return "webext-perms-optional-perms-header";
        }
        return hasDataCollectionOnly
          ? "webext-perms-optional-data-collection-only-text"
          : "webext-perms-optional-data-collection-text";
    }

    if (hasPermissions && !hasDataCollectionOnly) {
      return unsigned
        ? "webext-perms-header-unsigned-with-perms"
        : "webext-perms-header-with-perms";
    }

    return unsigned ? "webext-perms-header-unsigned" : "webext-perms-header";
  }

  /**
   * @param {Array<string>} dataPermissions An array of data collection permissions.
   *
   * @returns {{msg: string, collectsTechnicalAndInteractionData: boolean}} An
   * object with information about data collection permissions for the UI.
   */
  static _formatDataCollectionPermissions(dataPermissions, type) {
    const dataCollectionPermissions = {};
    const permissions = new Set(dataPermissions);

    // This data permission is opt-in by default, but users can opt-out, making
    // it special compared to the other permissions.
    if (type !== "optional" && permissions.delete("technicalAndInteraction")) {
      dataCollectionPermissions.collectsTechnicalAndInteractionData = true;
    }

    if (permissions.has("none")) {
      const [localizedMsg] = lazy.PERMISSION_L10N.formatValuesSync([
        "webext-perms-description-data-none",
      ]);
      dataCollectionPermissions.msg = localizedMsg;
    } else if (permissions.size) {
      // When we have data collection permissions and it isn't the "no data
      // collected" one, we build a list of localized permission strings that
      // we can format with `Intl.ListFormat()` and append to a localized
      // message.
      const dataMsgIds = [];
      for (const permission of permissions) {
        const l10nId = lazy.permissionToL10nId(permission, /* short */ true);
        // We deliberately do not include all permissions in the prompt. So
        // if we don't find one then just skip it.
        if (l10nId) {
          dataMsgIds.push(l10nId);
        }
      }

      let id;
      switch (type) {
        case "optional":
          id = "webext-perms-description-data-some-optional";
          break;
        case "update":
          id = "webext-perms-description-data-some-update";
          break;
        default:
          id = "webext-perms-description-data-some";
      }

      const fluentIdAndArgs = {
        id,
        args: {
          permissions: new Intl.ListFormat(undefined, {
            style: "narrow",
          }).format(lazy.PERMISSION_L10N.formatValuesSync(dataMsgIds)),
        },
      };
      const [localizedMsg] = lazy.PERMISSION_L10N.formatValuesSync([
        fluentIdAndArgs,
      ]);
      dataCollectionPermissions.msg = localizedMsg;
    }

    return dataCollectionPermissions;
  }

  /**
   * @param {Array<string>} permissions A list of optional data collection
   * permissions.
   *
   * @returns {Record<string, string>} A map of permission names to localized
   * strings representing the optional data collection permissions.
   */
  static _formatOptionalDataCollectionPermissions(permissions) {
    const optionalDataCollectionPermissions = {};

    const odcKeys = [];
    const odcL10nIds = [];
    for (let permission of permissions) {
      const l10nId = lazy.permissionToL10nId(permission, /* short */ false);
      odcKeys.push(permission);
      odcL10nIds.push(l10nId);
    }

    if (odcKeys.length) {
      const res = lazy.PERMISSION_L10N.formatValuesSync(odcL10nIds);
      for (let i = 0; i < res.length; ++i) {
        optionalDataCollectionPermissions[odcKeys[i]] = res[i];
      }
    }

    return optionalDataCollectionPermissions;
  }
}

const PROXIED_EVENTS = new Set([
  "test-harness-message",
  "background-script-suspend",
  "background-script-suspend-canceled",
  "background-script-suspend-ignored",
]);

class BootstrapScope {
  install() {}
  uninstall(data) {
    lazy.AsyncShutdown.profileChangeTeardown.addBlocker(
      `Uninstalling add-on: ${data.id}`,
      Management.emit("uninstall", { id: data.id }).then(() => {
        Management.emit("uninstall-complete", { id: data.id });
      })
    );
  }

  fetchState() {
    if (this.extension) {
      return { state: this.extension.state };
    }
    return null;
  }

  async update(data, reason) {
    // For updates that happen during startup, such as sideloads
    // and staged updates, the extension startupReason will be
    // APP_STARTED.  In some situations, such as background and
    // persisted listeners, we also need to know that the addon
    // was updated.
    this.updateReason = BootstrapScope.BOOTSTRAP_REASON_MAP[reason];
    // Retain any previously granted permissions that may have migrated
    // into the optional list.
    if (data.oldPermissions) {
      // New permissions may be null, ensure we have an empty
      // permission set in that case.
      let emptyPermissions = { permissions: [], origins: [] };
      await ExtensionData.migratePermissions(
        data.id,
        data.oldPermissions,
        data.oldOptionalPermissions,
        data.userPermissions || emptyPermissions,
        data.optionalPermissions || emptyPermissions
      );
    }

    return Management.emit("update", {
      id: data.id,
      resourceURI: data.resourceURI,
      isPrivileged: data.isPrivileged,
    });
  }

  startup(data, reason) {
    // eslint-disable-next-line no-use-before-define
    this.extension = new Extension(
      data,
      BootstrapScope.BOOTSTRAP_REASON_MAP[reason],
      this.updateReason
    );
    return this.extension.startup();
  }

  async shutdown(data, reason) {
    let result = await this.extension.shutdown(
      BootstrapScope.BOOTSTRAP_REASON_MAP[reason]
    );
    this.extension = null;
    return result;
  }

  static get BOOTSTRAP_REASON_MAP() {
    const BR = lazy.AddonManagerPrivate.BOOTSTRAP_REASONS;
    const value = Object.freeze({
      [BR.APP_STARTUP]: "APP_STARTUP",
      [BR.APP_SHUTDOWN]: "APP_SHUTDOWN",
      [BR.ADDON_ENABLE]: "ADDON_ENABLE",
      [BR.ADDON_DISABLE]: "ADDON_DISABLE",
      [BR.ADDON_INSTALL]: "ADDON_INSTALL",
      [BR.ADDON_UNINSTALL]: "ADDON_UNINSTALL",
      [BR.ADDON_UPGRADE]: "ADDON_UPGRADE",
      [BR.ADDON_DOWNGRADE]: "ADDON_DOWNGRADE",
    });
    return redefineGetter(this, "BOOTSTRAP_REASON_MAP", value);
  }
}

class DictionaryBootstrapScope extends BootstrapScope {
  install() {}
  uninstall() {}

  startup(data) {
    // eslint-disable-next-line no-use-before-define
    this.dictionary = new Dictionary(data);
    return this.dictionary.startup();
  }

  async shutdown(data, reason) {
    this.dictionary.shutdown(BootstrapScope.BOOTSTRAP_REASON_MAP[reason]);
    this.dictionary = null;
  }
}

class LangpackBootstrapScope extends BootstrapScope {
  install() {}
  uninstall() {}
  async update() {}

  startup(data) {
    // eslint-disable-next-line no-use-before-define
    this.langpack = new Langpack(data);
    return this.langpack.startup();
  }

  async shutdown(data, reason) {
    this.langpack.shutdown(BootstrapScope.BOOTSTRAP_REASON_MAP[reason]);
    this.langpack = null;
  }
}

let activeExtensionIDs = new Set();

let pendingExtensions = new Map();

/**
 * This class is the main representation of an active WebExtension
 * in the main process.
 *
 * @augments ExtensionData
 */
export class Extension extends ExtensionData {
  /** @type {Map<string, Map<string, any>>} */
  persistentListeners;

  /** @type {import("ExtensionShortcuts.sys.mjs").ExtensionShortcuts} */
  shortcuts;

  /**
   * Extension's TabManager, initialized at "startup" event of Management.
   *
   * @type {TabManagerBase}
   */
  tabManager;

  /**
   * Extension's WindowManager, initialized at "startup" event of Management.
   *
   * @type {WindowManagerBase}
   */
  windowManager;

  /** @type {(options?: { ignoreDevToolsAttached?: boolean, disableResetIdleForTest?: boolean }) => Promise} */
  terminateBackground;

  constructor(addonData, startupReason, updateReason) {
    super(addonData.resourceURI, addonData.isPrivileged);

    this.startupStates = new Set();
    this.state = "Not started";
    this.userContextIsolation = lazy.userContextIsolation;

    this.sharedDataKeys = new Set();

    this.uuid = UUIDMap.get(addonData.id);
    this.instanceId = getUniqueId();

    this.MESSAGE_EMIT_EVENT = `Extension:EmitEvent:${this.instanceId}`;
    Services.ppmm.addMessageListener(this.MESSAGE_EMIT_EVENT, this);

    if (addonData.cleanupFile) {
      Services.obs.addObserver(this, "xpcom-shutdown");
      this.cleanupFile = addonData.cleanupFile || null;
      delete addonData.cleanupFile;
    }

    if (addonData.TEST_NO_ADDON_MANAGER) {
      this.dontSaveStartupData = true;
    }
    if (addonData.TEST_NO_DELAYED_STARTUP) {
      this.testNoDelayedStartup = true;
    }

    this.addonData = addonData;
    this.startupData = addonData.startupData || {};
    this.blocklistState = addonData.blocklistState;
    this.startupReason = startupReason;
    this.updateReason = updateReason;
    this.temporarilyInstalled = !!addonData.temporarilyInstalled;

    if (
      updateReason ||
      ["ADDON_UPGRADE", "ADDON_DOWNGRADE"].includes(startupReason)
    ) {
      this.startupClearCachePromise = StartupCache.clearAddonData(addonData.id);
    }

    this.remote = !WebExtensionPolicy.isExtensionProcess;
    this.remoteType = this.remote ? lazy.E10SUtils.EXTENSION_REMOTE_TYPE : null;

    if (this.remote && lazy.processCount !== 1) {
      throw new Error(
        "Out-of-process WebExtensions are not supported with multiple child processes"
      );
    }

    // This is filled in the first time an extension child is created.
    this.parentMessageManager = null;

    this.id = addonData.id;
    this.version = addonData.version;
    this.baseURL = this.getURL("");
    this.baseURI = Services.io.newURI(this.baseURL).QueryInterface(Ci.nsIURL);
    this.principal = this.createPrincipal();

    // Privileged extensions and any extensions with a recommendation state are
    // exempt from the quarantined domains.
    // NOTE: privileged extensions are also exempted from quarantined domains
    // by the WebExtensionPolicy internal logic and so ignoreQuarantine set to
    // false for a privileged extension does not make any difference in
    // practice (but we still set the ignoreQuarantine flag here accordingly
    // to the expected behavior for consistency).
    this.ignoreQuarantine =
      addonData.isPrivileged ||
      !!addonData.recommendationState?.states?.length ||
      lazy.QuarantinedDomains.isUserAllowedAddonId(this.id);

    this.views = new Set();
    this._backgroundPageFrameLoader = null;

    this.onStartup = null;

    this.hasShutdown = false;
    this.onShutdown = new Set();

    this.uninstallURL = null;

    this.allowedOrigins = null;
    this._optionalOrigins = null;
    this.webAccessibleResources = null;

    this.registeredContentScripts = new Map();

    this.emitter = new EventEmitter();

    /* eslint-disable mozilla/balanced-listeners */
    this.on("add-permissions", (ignoreEvent, permissions) => {
      for (let perm of permissions.permissions) {
        this.permissions.add(perm);
      }
      for (let perm of permissions.data_collection) {
        this.dataCollectionPermissions.add(perm);
      }
      this.policy.permissions = Array.from(this.permissions);

      updateAllowedOrigins(this.policy, permissions.origins, /* isAdd */ true);
      this.allowedOrigins = this.policy.allowedOrigins;

      if (this.policy.active) {
        this.setSharedData("", this.serialize());
        Services.ppmm.sharedData.flush();
        this.broadcast("Extension:UpdatePermissions", {
          id: this.id,
          origins: permissions.origins,
          permissions: permissions.permissions,
          add: true,
        });
      }

      this.cachePermissions();
      this.updatePermissions();
    });

    this.on("remove-permissions", (ignoreEvent, permissions) => {
      for (let perm of permissions.permissions) {
        this.permissions.delete(perm);
      }
      for (let perm of permissions.data_collection) {
        this.dataCollectionPermissions.delete(perm);
      }
      this.policy.permissions = Array.from(this.permissions);

      updateAllowedOrigins(this.policy, permissions.origins, /* isAdd */ false);
      this.allowedOrigins = this.policy.allowedOrigins;

      if (this.policy.active) {
        this.setSharedData("", this.serialize());
        Services.ppmm.sharedData.flush();
        this.broadcast("Extension:UpdatePermissions", {
          id: this.id,
          origins: permissions.origins,
          permissions: permissions.permissions,
          add: false,
        });
      }

      this.cachePermissions();
      this.updatePermissions();
    });
    /* eslint-enable mozilla/balanced-listeners */
  }

  set state(startupState) {
    this.startupStates.clear();
    this.startupStates.add(startupState);
  }

  get state() {
    return `${Array.from(this.startupStates).join(", ")}`;
  }

  async addStartupStatePromise(name, fn) {
    this.startupStates.add(name);
    try {
      await fn();
    } finally {
      this.startupStates.delete(name);
    }
  }

  // Some helpful properties added elsewhere:

  static getBootstrapScope() {
    return new BootstrapScope();
  }

  get browsingContextGroupId() {
    return this.policy.browsingContextGroupId;
  }

  get groupFrameLoader() {
    let frameLoader = this._backgroundPageFrameLoader;
    for (let view of this.views) {
      if (view.viewType === "background" && view.xulBrowser) {
        return view.xulBrowser.frameLoader;
      }
      if (!frameLoader && view.xulBrowser) {
        frameLoader = view.xulBrowser.frameLoader;
      }
    }
    return frameLoader || ExtensionParent.DebugUtils.getFrameLoader(this.id);
  }

  get backgroundContext() {
    for (let view of this.views) {
      if (view.isBackgroundContext) {
        return view;
      }
    }
    return undefined;
  }

  get isSoftBlocked() {
    return this.blocklistState === Ci.nsIBlocklistService.STATE_SOFTBLOCKED;
  }

  on(hook, f) {
    return this.emitter.on(hook, f);
  }

  off(hook, f) {
    return this.emitter.off(hook, f);
  }

  once(hook, f) {
    return this.emitter.once(hook, f);
  }

  emit(event, ...args) {
    if (PROXIED_EVENTS.has(event)) {
      Services.ppmm.broadcastAsyncMessage(this.MESSAGE_EMIT_EVENT, {
        event,
        args,
      });
    }

    return this.emitter.emit(event, ...args);
  }

  receiveMessage({ name, data }) {
    if (name === this.MESSAGE_EMIT_EVENT) {
      this.emitter.emit(data.event, ...data.args);
    }
  }

  testMessage(...args) {
    this.emit("test-harness-message", ...args);
  }

  createPrincipal(uri = this.baseURI, originAttributes = {}) {
    return Services.scriptSecurityManager.createContentPrincipal(
      uri,
      originAttributes
    );
  }

  // Checks that the given URL is a child of our baseURI.
  isExtensionURL(url) {
    let uri = Services.io.newURI(url);

    let common = this.baseURI.getCommonBaseSpec(uri);
    return common == this.baseURL;
  }

  checkLoadURI(uri, options = {}) {
    return ExtensionCommon.checkLoadURI(uri, this.principal, options);
  }

  // Note: use checkLoadURI instead of checkLoadURL if you already have a URI.
  checkLoadURL(url, options = {}) {
    // As an optimization, if the URL starts with the extension's base URL,
    // don't do any further checks. It's always allowed to load it.
    if (url.startsWith(this.baseURL)) {
      return true;
    }

    return ExtensionCommon.checkLoadURL(url, this.principal, options);
  }

  async promiseLocales() {
    let locales = await StartupCache.locales.get(
      [this.id, "@@all_locales"],
      () => this._promiseLocaleMap()
    );

    return this._setupLocaleData(locales);
  }

  readLocaleFile(locale) {
    return StartupCache.locales
      .get([this.id, this.version, locale], () => super.readLocaleFile(locale))
      .then(result => {
        this.localeData.messages.set(locale, result);
        return result;
      });
  }

  get manifestCacheKey() {
    return [this.id, this.version, Services.locale.appLocaleAsBCP47];
  }

  saveStartupData() {
    if (this.dontSaveStartupData) {
      return;
    }
    lazy.AddonManagerPrivate.setAddonStartupData(this.id, this.startupData);
  }

  async parseManifest() {
    await this.startupClearCachePromise;
    return StartupCache.manifests.get(this.manifestCacheKey, () =>
      super.parseManifest()
    );
  }

  async cachePermissions() {
    let manifestData = await this.parseManifest();

    manifestData.originPermissions = this.allowedOrigins.patterns.map(
      pat => pat.pattern
    );
    manifestData.permissions = this.permissions;
    manifestData.dataCollectionPermissions = this.dataCollectionPermissions;
    return StartupCache.manifests.set(this.manifestCacheKey, manifestData);
  }

  async loadManifest() {
    let manifest = await super.loadManifest();

    this.ensureNoErrors();

    return manifest;
  }

  get extensionPageCSP() {
    const { content_security_policy } = this.manifest;
    // While only manifest v3 should contain an object,
    // we'll remain lenient here.
    if (
      content_security_policy &&
      typeof content_security_policy === "object"
    ) {
      return content_security_policy.extension_pages;
    }
    return content_security_policy;
  }

  get backgroundScripts() {
    return this.manifest.background?.scripts;
  }

  get backgroundTypeModule() {
    return this.manifest.background?.type === "module";
  }

  get backgroundWorkerScript() {
    return this.manifest.background?.service_worker;
  }

  get optionalPermissions() {
    return this.manifest.optional_permissions;
  }

  get optionalDataCollectionPermissions() {
    return this.getDataCollectionPermissions().optional;
  }

  get privateBrowsingAllowed() {
    return this.policy.privateBrowsingAllowed;
  }

  canAccessWindow(window) {
    return this.policy.canAccessWindow(window);
  }

  // TODO bug 1699481: move this logic to WebExtensionPolicy
  canAccessContainer(userContextId) {
    userContextId = userContextId ?? 0; // firefox-default has userContextId as 0.
    let defaultRestrictedContainers = JSON.parse(
      lazy.userContextIsolationDefaultRestricted
    );
    let extensionRestrictedContainers = JSON.parse(
      Services.prefs.getStringPref(
        `extensions.userContextIsolation.${this.id}.restricted`,
        "[]"
      )
    );
    if (
      extensionRestrictedContainers.includes(userContextId) ||
      defaultRestrictedContainers.includes(userContextId)
    ) {
      return false;
    }

    return true;
  }

  // Representation of the extension to send to content
  // processes. This should include anything the content process might
  // need.
  serialize() {
    return {
      id: this.id,
      uuid: this.uuid,
      name: this.name,
      type: this.type,
      manifestVersion: this.manifestVersion,
      extensionPageCSP: this.extensionPageCSP,
      instanceId: this.instanceId,
      resourceURL: this.resourceURL,
      contentScripts: this.contentScripts,
      webAccessibleResources: this.webAccessibleResources,
      allowedOrigins: this.allowedOrigins.patterns.map(pat => pat.pattern),
      permissions: this.permissions,
      optionalPermissions: this.optionalPermissions,
      isPrivileged: this.isPrivileged,
      ignoreQuarantine: this.ignoreQuarantine,
      temporarilyInstalled: this.temporarilyInstalled,
    };
  }

  /**
   * Extended serialized data which is only needed in the extensions process,
   * and is never deserialized in web content processes.
   * Keep in sync with @see {ExtensionChild}.
   */
  serializeExtended() {
    return {
      backgroundScripts: this.backgroundScripts,
      backgroundWorkerScript: this.backgroundWorkerScript,
      backgroundTypeModule: this.backgroundTypeModule,
      childModules: this.modules && this.modules.child,
      dependencies: this.dependencies,
      persistentBackground: this.persistentBackground,
      schemaURLs: this.schemaURLs,
    };
  }

  broadcast(msg, data) {
    return new Promise(resolve => {
      let { ppmm } = Services;
      let children = new Set();
      for (let i = 0; i < ppmm.childCount; i++) {
        children.add(ppmm.getChildAt(i));
      }

      let maybeResolve;
      function listener(data) {
        children.delete(data.target);
        maybeResolve();
      }
      function observer(subject) {
        children.delete(subject);
        maybeResolve();
      }

      maybeResolve = () => {
        if (children.size === 0) {
          ppmm.removeMessageListener(msg + "Complete", listener);
          Services.obs.removeObserver(observer, "message-manager-close");
          Services.obs.removeObserver(observer, "message-manager-disconnect");
          resolve();
        }
      };
      ppmm.addMessageListener(msg + "Complete", listener, true);
      Services.obs.addObserver(observer, "message-manager-close");
      Services.obs.addObserver(observer, "message-manager-disconnect");

      ppmm.broadcastAsyncMessage(msg, data);
    });
  }

  setSharedData(key, value) {
    key = `extension/${this.id}/${key}`;
    this.sharedDataKeys.add(key);

    sharedData.set(key, value);
  }

  getSharedData(key) {
    key = `extension/${this.id}/${key}`;
    return sharedData.get(key);
  }

  initSharedData() {
    this.setSharedData("", this.serialize());
    this.setSharedData("extendedData", this.serializeExtended());
    this.setSharedData("locales", this.localeData.serialize());
    this.setSharedData("manifest", this.manifest);
    this.updateContentScripts();
  }

  shouldSendSharedData() {
    return (
      // If not started or already shutdown, don't bother.
      !!this.policy?.active &&
      // If startup() has been called but we have not reached the end of
      // runManifest() yet, then we have not notified the content process of
      // via "Extension:Startup", and therefore do not need to notify of
      // updated sharedData.
      !pendingExtensions.has(this.id)
    );
  }

  updateContentScripts() {
    this.setSharedData("contentScripts", this.registeredContentScripts);
  }

  runManifest(manifest) {
    let promises = [];
    let addPromise = (name, fn) => {
      promises.push(this.addStartupStatePromise(name, fn));
    };

    for (let directive in manifest) {
      if (manifest[directive] !== null) {
        addPromise(`asyncEmitManifestEntry("${directive}")`, () =>
          Management.asyncEmitManifestEntry(this, directive)
        );
      }
    }

    activeExtensionIDs.add(this.id);
    sharedData.set("extensions/activeIDs", activeExtensionIDs);

    pendingExtensions.delete(this.id);
    sharedData.set("extensions/pending", pendingExtensions);

    Services.ppmm.sharedData.flush();
    this.broadcast("Extension:Startup", this.id);

    return Promise.all(promises);
  }

  /**
   * Call the close() method on the given object when this extension
   * is shut down.  This can happen during browser shutdown, or when
   * an extension is manually disabled or uninstalled.
   *
   * @param {object} obj
   *        An object on which to call the close() method when this
   *        extension is shut down.
   */
  callOnClose(obj) {
    this.onShutdown.add(obj);
  }

  forgetOnClose(obj) {
    this.onShutdown.delete(obj);
  }

  get builtinMessages() {
    return new Map([["@@extension_id", this.uuid]]);
  }

  // Reads the locale file for the given Gecko-compatible locale code, or if
  // no locale is given, the available locale closest to the UI locale.
  // Sets the currently selected locale on success.
  async initLocale(locale = undefined) {
    if (locale === undefined) {
      let locales = await this.promiseLocales();

      let matches = Services.locale.negotiateLanguages(
        Services.locale.appLocalesAsBCP47,
        Array.from(locales.keys()),
        this.defaultLocale
      );

      locale = matches[0];
    }

    return super.initLocale(locale);
  }

  /**
   * Clear cached resources associated to the extension principal
   * when an extension is installed (in case we were unable to do that at
   * uninstall time) or when it is being upgraded or downgraded.
   *
   * @param {string|undefined} reason
   *        BOOTSTRAP_REASON string, if provided. The value is expected to be
   *        `undefined` for extension objects without a corresponding AddonManager
   *        addon wrapper (e.g. test extensions created using `ExtensionTestUtils`
   *        without `useAddonManager` optional property).
   *
   * @returns {Promise<void>}
   *        Promise resolved when the nsIClearDataService async method call
   *        has been completed.
   */
  async clearCache(reason) {
    switch (reason) {
      case "ADDON_INSTALL":
      case "ADDON_UPGRADE":
      case "ADDON_DOWNGRADE":
        return clearCacheForExtensionPrincipal(this.principal);
    }
  }

  /**
   * Update site permissions as necessary.
   *
   * @param {string} [reason]
   *        If provided, this is a BOOTSTRAP_REASON string.  If reason is undefined,
   *        addon permissions are being added or removed that may effect the site permissions.
   */
  updatePermissions(reason) {
    const { principal } = this;

    const testPermission = perm =>
      Services.perms.testPermissionFromPrincipal(principal, perm);

    const addUnlimitedStoragePermissions = () => {
      // Set the indexedDB permission and a custom "WebExtensions-unlimitedStorage" to
      // remember that the permission hasn't been selected manually by the user.
      Services.perms.addFromPrincipal(
        principal,
        "WebExtensions-unlimitedStorage",
        Services.perms.ALLOW_ACTION
      );
      Services.perms.addFromPrincipal(
        principal,
        "persistent-storage",
        Services.perms.ALLOW_ACTION
      );
    };

    // Only update storage permissions when the extension changes in
    // some way.
    if (reason !== "APP_STARTUP" && reason !== "APP_SHUTDOWN") {
      if (this.hasPermission("unlimitedStorage")) {
        addUnlimitedStoragePermissions();
      } else {
        // Remove the indexedDB permission if it has been enabled using the
        // unlimitedStorage WebExtensions permissions.
        Services.perms.removeFromPrincipal(
          principal,
          "WebExtensions-unlimitedStorage"
        );
        Services.perms.removeFromPrincipal(principal, "persistent-storage");
      }
    } else if (
      reason === "APP_STARTUP" &&
      this.hasPermission("unlimitedStorage") &&
      testPermission("persistent-storage") !== Services.perms.ALLOW_ACTION
    ) {
      // If the extension does have the unlimitedStorage permission, but the
      // expected site permissions are missing during the app startup, then
      // add them back (See Bug 1454192).
      addUnlimitedStoragePermissions();
    }

    // Never change geolocation permissions at shutdown, since it uses a
    // session-only permission.
    if (reason !== "APP_SHUTDOWN") {
      if (this.hasPermission("geolocation")) {
        if (testPermission("geo") === Services.perms.UNKNOWN_ACTION) {
          Services.perms.addFromPrincipal(
            principal,
            "geo",
            Services.perms.ALLOW_ACTION,
            Services.perms.EXPIRE_SESSION
          );
        }
      } else if (
        reason !== "APP_STARTUP" &&
        testPermission("geo") === Services.perms.ALLOW_ACTION
      ) {
        Services.perms.removeFromPrincipal(principal, "geo");
      }
    }
  }

  async startup() {
    this.state = "Startup";

    // readyPromise is resolved with the policy upon success,
    // and with null if startup was interrupted.
    /** @type {callback} */
    let resolveReadyPromise;
    let readyPromise = new Promise(resolve => {
      resolveReadyPromise = resolve;
    });

    // Create a temporary policy object for the devtools and add-on
    // manager callers that depend on it being available early.
    this.policy = new WebExtensionPolicy({
      id: this.id,
      mozExtensionHostname: this.uuid,
      baseURL: this.resourceURL,
      isPrivileged: this.isPrivileged,
      ignoreQuarantine: this.ignoreQuarantine,
      temporarilyInstalled: this.temporarilyInstalled,
      allowedOrigins: new MatchPatternSet([]),
      localizeCallback: () => "",
      readyPromise,
    });

    this.policy.extension = this;
    if (!WebExtensionPolicy.getByID(this.id)) {
      this.policy.active = true;
    }

    pendingExtensions.set(this.id, {
      mozExtensionHostname: this.uuid,
      baseURL: this.resourceURL,
      isPrivileged: this.isPrivileged,
      ignoreQuarantine: this.ignoreQuarantine,
    });
    sharedData.set("extensions/pending", pendingExtensions);

    if (
      // Cannot use this.type because we haven't parsed the manifest yet.
      this.addonData.type === "theme" &&
      this.startupData.lwtData &&
      this.startupReason == "APP_STARTUP"
    ) {
      // Avoid FOUC at browser startup by setting the fallback theme data as
      // soon as the static theme is starting. Not doing so can result in a
      // FOUC because loadManifest + runManifest (and other steps) are async.
      lazy.LightweightThemeManager.fallbackThemeData = this.startupData.lwtData;
    }

    lazy.ExtensionTelemetry.extensionStartup.stopwatchStart(this);
    try {
      this.state = "Startup: Loading manifest";
      await this.loadManifest();
      this.state = "Startup: Loaded manifest";

      if (!this.hasShutdown) {
        this.state = "Startup: Init locale";
        await this.initLocale();
        this.state = "Startup: Initted locale";
      }

      this.ensureNoErrors();

      if (this.hasShutdown) {
        // Startup was interrupted and shutdown() has taken care of unloading
        // the extension and running cleanup logic.
        return;
      }

      await this.clearCache(this.startupReason);
      this._setupStartupPermissions();

      GlobalManager.init(this);

      if (this.hasPermission("scripting")) {
        this.state = "Startup: Initialize scripting store";
        // We have to await here because `initSharedData` depends on the data
        // fetched from the scripting store. This has to be done early because
        // we need the data to run the content scripts in existing pages at
        // startup.
        try {
          await lazy.ExtensionScriptingStore.initExtension(this);
          this.state = "Startup: Scripting store initialized";
        } catch (err) {
          this.logError(`Failed to initialize scripting store: ${err}`);
        }
      }

      if (this.hasPermission("userScripts")) {
        this.state = "Startup: Initialize user scripts";
        // TODO: Parallelize with ExtensionScriptingStore.initExtension?
        try {
          await lazy.ExtensionUserScripts.initExtension(this);
          this.state = "Startup: User scripts initialized";
        } catch (err) {
          this.logError(`Failed to initialize user scripts: ${err}`);
        }
      }

      this.initSharedData();

      this.policy.active = false;
      this.policy = lazy.ExtensionProcessScript.initExtension(this);
      this.policy.extension = this;

      this.updatePermissions(this.startupReason);

      // Select the storage.local backend if it is already known,
      // and start the data migration if needed.
      if (this.hasPermission("storage")) {
        if (!lazy.ExtensionStorageIDB.isBackendEnabled) {
          this.setSharedData("storageIDBBackend", false);
        } else if (lazy.ExtensionStorageIDB.isMigratedExtension(this)) {
          this.setSharedData("storageIDBBackend", true);
          this.setSharedData(
            "storageIDBPrincipal",
            lazy.ExtensionStorageIDB.getStoragePrincipal(this)
          );
        } else if (
          this.startupReason === "ADDON_INSTALL" &&
          !Services.prefs.getBoolPref(LEAVE_STORAGE_PREF, false)
        ) {
          // If the extension has been just installed, set it as migrated,
          // because there will not be any data to migrate.
          lazy.ExtensionStorageIDB.setMigratedExtensionPref(this, true);
          this.setSharedData("storageIDBBackend", true);
          this.setSharedData(
            "storageIDBPrincipal",
            lazy.ExtensionStorageIDB.getStoragePrincipal(this)
          );
        }
      }

      // Initialize DNR for the extension, only if the extension
      // has the required DNR permissions and without blocking
      // the extension startup on DNR being fully initialized.
      if (
        this.hasPermission("declarativeNetRequest") ||
        this.hasPermission("declarativeNetRequestWithHostAccess")
      ) {
        lazy.ExtensionDNR.ensureInitialized(this);
      }

      resolveReadyPromise(this.policy);

      // The "startup" Management event sent on the extension instance itself
      // is emitted just before the Management "startup" event,
      // and it is used to run code that needs to be executed before
      // any of the "startup" listeners.
      this.emit("startup", this);

      this.startupStates.clear();
      await Promise.all([
        this.addStartupStatePromise("Startup: Emit startup", () =>
          Management.emit("startup", this)
        ),
        this.addStartupStatePromise("Startup: Run manifest", () =>
          this.runManifest(this.manifest)
        ),
      ]);
      this.state = "Startup: Ran manifest";

      Management.emit("ready", this);
      this.emit("ready");

      this.state = "Startup: Complete";
    } catch (e) {
      this.state = `Startup: Error: ${e}`;

      Cu.reportError(e);

      if (this.policy) {
        this.policy.active = false;
      }

      this.cleanupGeneratedFile();

      throw e;
    } finally {
      lazy.ExtensionTelemetry.extensionStartup.stopwatchFinish(this);
      // Mark readyPromise as resolved in case it has not happened before,
      // e.g. due to an early return or an error.
      resolveReadyPromise(null);
    }
  }

  // Setup initial permissions on extension startup based on manifest
  // and potentially previous manifest and permissions values. None of
  // the ExtensionPermissions.add/remove() calls are are awaited here
  // because we update the in-memory representation at the same time.
  _setupStartupPermissions() {
    // If we add/remove permissions conditionally based on startupReason,
    // we need to update the cache, or changes will be lost after restart.
    let updateCache = false;

    // We automatically add permissions to system/built-in extensions.
    // Extensions expliticy stating not_allowed will never get permission.
    let isAllowed = this.permissions.has(PRIVATE_ALLOWED_PERMISSION);
    const hasIncognitoNotAllowed = this.manifest.incognito === "not_allowed";
    if (hasIncognitoNotAllowed) {
      // If an extension previously had permission, but upgrades/downgrades to
      // a version that specifies "not_allowed" in manifest, remove the
      // permission.
      if (isAllowed) {
        lazy.ExtensionPermissions.remove(this.id, {
          permissions: [PRIVATE_ALLOWED_PERMISSION],
          origins: [],
        });
        this.permissions.delete(PRIVATE_ALLOWED_PERMISSION);
      }
    } else if (!isAllowed && this.isPrivileged && !this.temporarilyInstalled) {
      // Add to EP so it is preserved after ADDON_INSTALL.
      lazy.ExtensionPermissions.add(this.id, {
        permissions: [PRIVATE_ALLOWED_PERMISSION],
        origins: [],
      });
      this.permissions.add(PRIVATE_ALLOWED_PERMISSION);
    }

    // Allow other extensions to access static themes in private browsing windows
    // (See Bug 1790115).
    if (this.type === "theme") {
      this.permissions.add(PRIVATE_ALLOWED_PERMISSION);
    }

    // On builds where Enterprise Policies are supported, grant or revoke
    // the private browsing access for extensions that are not app provided
    // (system and builtin add-ons) or hidden.
    if (
      Services.policies &&
      !this.isAppProvided &&
      !this.isHidden &&
      !hasIncognitoNotAllowed &&
      this.type === "extension"
    ) {
      const settings = Services.policies.getExtensionSettings(this.id);
      if (settings?.private_browsing) {
        lazy.ExtensionPermissions.add(this.id, {
          permissions: [PRIVATE_ALLOWED_PERMISSION],
          origins: [],
        });
        this.permissions.add(PRIVATE_ALLOWED_PERMISSION);
      } else if (settings?.private_browsing === false) {
        lazy.ExtensionPermissions.remove(this.id, {
          permissions: [PRIVATE_ALLOWED_PERMISSION],
          origins: [],
        });
        this.permissions.delete(PRIVATE_ALLOWED_PERMISSION);
      }
    }

    // We only want to update the SVG_CONTEXT_PROPERTIES_PERMISSION during
    // install and upgrade/downgrade startups.
    if (INSTALL_AND_UPDATE_STARTUP_REASONS.has(this.startupReason)) {
      if (isMozillaExtension(this)) {
        // Add to EP so it is preserved after ADDON_INSTALL.
        lazy.ExtensionPermissions.add(this.id, {
          permissions: [SVG_CONTEXT_PROPERTIES_PERMISSION],
          origins: [],
        });
        this.permissions.add(SVG_CONTEXT_PROPERTIES_PERMISSION);
      } else {
        lazy.ExtensionPermissions.remove(this.id, {
          permissions: [SVG_CONTEXT_PROPERTIES_PERMISSION],
          origins: [],
        });
        this.permissions.delete(SVG_CONTEXT_PROPERTIES_PERMISSION);
      }
      updateCache = true;
    }

    // Ensure devtools permission is set.
    if (
      this.manifest.devtools_page &&
      !this.manifest.optional_permissions.includes("devtools")
    ) {
      lazy.ExtensionPermissions.add(this.id, {
        permissions: ["devtools"],
        origins: [],
      });
      this.permissions.add("devtools");
    }

    if (
      this.originControls &&
      this.startupReason === "ADDON_INSTALL" &&
      (this.manifest.granted_host_permissions || lazy.installIncludesOrigins)
    ) {
      let origins = this.getManifestOrigins();
      lazy.ExtensionPermissions.add(this.id, { permissions: [], origins });
      updateCache = true;

      let allowed = this.allowedOrigins.patterns.map(p => p.pattern);
      this.allowedOrigins = new MatchPatternSet(origins.concat(allowed), {
        restrictSchemes: this.restrictSchemes,
        ignorePath: true,
      });
    }

    if (updateCache) {
      this.cachePermissions();
    }
  }

  cleanupGeneratedFile() {
    if (!this.cleanupFile) {
      return;
    }

    let file = this.cleanupFile;
    this.cleanupFile = null;

    Services.obs.removeObserver(this, "xpcom-shutdown");

    return this.broadcast("Extension:FlushJarCache", { path: file.path })
      .then(() => {
        // We can't delete this file until everyone using it has
        // closed it (because Windows is dumb). So we wait for all the
        // child processes (including the parent) to flush their JAR
        // caches. These caches may keep the file open.
        file.remove(false);
      })
      .catch(Cu.reportError);
  }

  async shutdown(reason) {
    this.state = "Shutdown";

    this.hasShutdown = true;

    if (!this.policy) {
      return;
    }

    if (
      this.hasPermission("storage") &&
      lazy.ExtensionStorageIDB.selectedBackendPromises.has(this)
    ) {
      this.state = "Shutdown: Storage";

      // Wait the data migration to complete.
      try {
        await lazy.ExtensionStorageIDB.selectedBackendPromises.get(this);
      } catch (err) {
        Cu.reportError(
          `Error while waiting for extension data migration on shutdown: ${this.policy.debugName} - ${err.message}::${err.stack}`
        );
      }
      this.state = "Shutdown: Storage complete";
    }

    if (this.rootURI instanceof Ci.nsIJARURI) {
      this.state = "Shutdown: Flush jar cache";
      let file = this.rootURI.JARFile.QueryInterface(Ci.nsIFileURL).file;
      Services.ppmm.broadcastAsyncMessage("Extension:FlushJarCache", {
        path: file.path,
      });
      this.state = "Shutdown: Flushed jar cache";
    }

    const isAppShutdown = reason === "APP_SHUTDOWN";
    if (this.cleanupFile || !isAppShutdown) {
      StartupCache.clearAddonData(this.id);
    }

    activeExtensionIDs.delete(this.id);
    sharedData.set("extensions/activeIDs", activeExtensionIDs);

    for (let key of this.sharedDataKeys) {
      sharedData.delete(key);
    }

    Services.ppmm.removeMessageListener(this.MESSAGE_EMIT_EVENT, this);

    this.updatePermissions(reason);

    // The service worker registrations related to the extensions are unregistered
    // only when the extension is not shutting down as part of the application
    // shutdown (a previously registered service worker is expected to stay
    // active across browser restarts), the service worker may have been
    // registered through the manifest.json background.service_worker property
    // or from an extension page through the service worker API if allowed
    // through the about:config pref.
    if (!isAppShutdown) {
      this.state = "Shutdown: ServiceWorkers";
      // TODO: ServiceWorkerCleanUp may go away once Bug 1183245 is fixed.
      await lazy.ServiceWorkerCleanUp.removeFromPrincipal(this.principal);
      this.state = "Shutdown: ServiceWorkers completed";
    }

    if (!this.manifest) {
      this.state = "Shutdown: Complete: No manifest";
      this.policy.active = false;

      return this.cleanupGeneratedFile();
    }

    GlobalManager.uninit(this);

    for (let obj of this.onShutdown) {
      obj.close();
    }

    ParentAPIManager.shutdownExtension(this.id, reason);

    Management.emit("shutdown", this);
    this.emit("shutdown", isAppShutdown);

    const TIMED_OUT = Symbol();

    this.state = "Shutdown: Emit shutdown";
    let result = await Promise.race([
      this.broadcast("Extension:Shutdown", { id: this.id }),
      promiseTimeout(CHILD_SHUTDOWN_TIMEOUT_MS).then(() => TIMED_OUT),
    ]);
    this.state = `Shutdown: Emitted shutdown: ${result === TIMED_OUT}`;
    if (result === TIMED_OUT) {
      Cu.reportError(
        `Timeout while waiting for extension child to shutdown: ${this.policy.debugName}`
      );
    }

    this.policy.active = false;

    this.state = `Shutdown: Complete (${this.cleanupFile})`;
    return this.cleanupGeneratedFile();
  }

  observe(subject, topic) {
    if (topic === "xpcom-shutdown") {
      this.cleanupGeneratedFile();
    }
  }

  get name() {
    return this.manifest.name;
  }

  get optionalOrigins() {
    if (this._optionalOrigins == null) {
      let { origins } = this.manifestOptionalPermissions;
      this._optionalOrigins = new MatchPatternSet(origins, {
        restrictSchemes: this.restrictSchemes,
        ignorePath: true,
      });
    }
    return this._optionalOrigins;
  }

  get hasBrowserActionUI() {
    return this.manifest.browser_action || this.manifest.action;
  }

  getPreferredIcon(size = 16) {
    return IconDetails.getPreferredIcon(this.manifest.icons ?? {}, this, size)
      .icon;
  }
}

export class Dictionary extends ExtensionData {
  constructor(addonData) {
    super(addonData.resourceURI);
    this.id = addonData.id;
    this.startupData = addonData.startupData;
  }

  static getBootstrapScope() {
    return new DictionaryBootstrapScope();
  }

  async startup() {
    this.dictionaries = {};
    for (let [lang, path] of Object.entries(this.startupData.dictionaries)) {
      let uri = Services.io.newURI(
        path.slice(0, -4) + ".aff",
        null,
        this.rootURI
      );
      this.dictionaries[lang] = uri;

      lazy.spellCheck.addDictionary(lang, uri);
    }

    Management.emit("ready", this);
  }

  async shutdown(reason) {
    if (reason !== "APP_SHUTDOWN") {
      lazy.AddonManagerPrivate.unregisterDictionaries(this.dictionaries);
    }
  }
}

export class Langpack extends ExtensionData {
  constructor(addonData) {
    super(addonData.resourceURI);
    this.startupData = addonData.startupData;
    this.manifestCacheKey = [addonData.id, addonData.version];
  }

  static getBootstrapScope() {
    return new LangpackBootstrapScope();
  }

  async promiseLocales() {
    let locales = await StartupCache.locales.get(
      [this.id, "@@all_locales"],
      () => this._promiseLocaleMap()
    );

    return this._setupLocaleData(locales);
  }

  parseManifest() {
    return StartupCache.manifests.get(this.manifestCacheKey, () =>
      super.parseManifest()
    );
  }

  async startup() {
    this.chromeRegistryHandle = null;
    if (this.startupData.chromeEntries.length) {
      const manifestURI = Services.io.newURI(
        "manifest.json",
        null,
        this.rootURI
      );
      this.chromeRegistryHandle = lazy.aomStartup.registerChrome(
        manifestURI,
        this.startupData.chromeEntries
      );
    }

    const langpackId = this.startupData.langpackId;
    const l10nRegistrySources = this.startupData.l10nRegistrySources;

    lazy.resourceProtocol.setSubstitution(langpackId, this.rootURI);

    const fileSources = Object.entries(l10nRegistrySources).map(entry => {
      const [sourceName, basePath] = entry;
      return new L10nFileSource(
        `${sourceName}-${langpackId}`,
        langpackId,
        this.startupData.languages,
        `resource://${langpackId}/${basePath}localization/{locale}/`
      );
    });

    L10nRegistry.getInstance().registerSources(fileSources);

    Services.obs.notifyObservers(
      { wrappedJSObject: { langpack: this } },
      "webextension-langpack-startup"
    );
  }

  async shutdown(reason) {
    if (reason === "APP_SHUTDOWN") {
      // If we're shutting down, let's not bother updating the state of each
      // system.
      return;
    }

    const sourcesToRemove = Object.keys(
      this.startupData.l10nRegistrySources
    ).map(sourceName => `${sourceName}-${this.startupData.langpackId}`);
    L10nRegistry.getInstance().removeSources(sourcesToRemove);

    if (this.chromeRegistryHandle) {
      this.chromeRegistryHandle.destruct();
      this.chromeRegistryHandle = null;
    }

    lazy.resourceProtocol.setSubstitution(this.startupData.langpackId, null);
  }
}

// Exported for testing purposes.
export { ExtensionAddonObserver, PRIVILEGED_PERMS };
