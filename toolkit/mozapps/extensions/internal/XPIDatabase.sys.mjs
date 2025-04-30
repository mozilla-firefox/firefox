/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This file contains most of the logic required to maintain the
 * extensions database, including querying and modifying extension
 * metadata. In general, we try to avoid loading it during startup when
 * at all possible. Please keep that in mind when deciding whether to
 * add code here or elsewhere.
 */

/* eslint "valid-jsdoc": [2, {requireReturn: false, requireReturnDescription: false, prefer: {return: "returns"}}] */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { XPIExports } from "resource://gre/modules/addons/XPIExports.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AddonManagerPrivate: "resource://gre/modules/AddonManager.sys.mjs",
  AddonRepository: "resource://gre/modules/addons/AddonRepository.sys.mjs",
  AddonSettings: "resource://gre/modules/addons/AddonSettings.sys.mjs",
  Blocklist: "resource://gre/modules/Blocklist.sys.mjs",
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  ExtensionData: "resource://gre/modules/Extension.sys.mjs",
  ExtensionUtils: "resource://gre/modules/ExtensionUtils.sys.mjs",
  ObjectUtils: "resource://gre/modules/ObjectUtils.sys.mjs",
  PermissionsUtils: "resource://gre/modules/PermissionsUtils.sys.mjs",
  QuarantinedDomains: "resource://gre/modules/ExtensionPermissions.sys.mjs",
  ExtensionPermissions: "resource://gre/modules/ExtensionPermissions.sys.mjs",
});

// A temporary hidden pref just meant to be used as a last resort, in case
// we need to force-disable the "per-addon quarantined domains user controls"
// feature during the beta cycle, e.g. if unexpected issues are caught late and
// it shouldn't  ride the train.
//
// TODO(Bug 1839616): remove this pref after the user controls features have been
// released.
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "isQuarantineUIDisabled",
  "extensions.quarantinedDomains.uiDisabled",
  false
);

const { nsIBlocklistService } = Ci;

import { Log } from "resource://gre/modules/Log.sys.mjs";

const LOGGER_ID = "addons.xpi-utils";

const nsIFile = Components.Constructor(
  "@mozilla.org/file/local;1",
  "nsIFile",
  "initWithPath"
);

// Create a new logger for use by the Addons XPI Provider Utils
// (Requires AddonManager.sys.mjs)
var logger = Log.repository.getLogger(LOGGER_ID);

const FILE_JSON_DB = "extensions.json";

const PREF_DB_SCHEMA = "extensions.databaseSchema";
const PREF_EM_AUTO_DISABLED_SCOPES = "extensions.autoDisableScopes";
const PREF_PENDING_OPERATIONS = "extensions.pendingOperations";
const PREF_XPI_PERMISSIONS_BRANCH = "xpinstall.";
const PREF_XPI_SIGNATURES_DEV_ROOT = "xpinstall.signatures.dev-root";

const TOOLKIT_ID = "toolkit@mozilla.org";

const KEY_APP_SYSTEM_ADDONS = "app-system-addons";
const KEY_APP_SYSTEM_BUILTINS = "app-builtin-addons";
const KEY_APP_SYSTEM_PROFILE = "app-system-profile";
const KEY_APP_BUILTINS = "app-builtin";
const KEY_APP_SYSTEM_LOCAL = "app-system-local";
const KEY_APP_SYSTEM_SHARE = "app-system-share";
const KEY_APP_GLOBAL = "app-global";
const KEY_APP_PROFILE = "app-profile";
const KEY_APP_TEMPORARY = "app-temporary";

const DEFAULT_THEME_ID = "default-theme@mozilla.org";

// Properties to cache and reload when an addon installation is pending
const PENDING_INSTALL_METADATA = [
  "syncGUID",
  "targetApplications",
  "userDisabled",
  "softDisabled",
  "embedderDisabled",
  "sourceURI",
  "releaseNotesURI",
  "installDate",
  "updateDate",
  "applyBackgroundUpdates",
  "installTelemetryInfo",
];

// Properties to save in JSON file
const PROP_JSON_FIELDS = [
  "id",
  "syncGUID",
  "version",
  "type",
  "loader",
  "updateURL",
  "installOrigins",
  "manifestVersion",
  "optionsURL",
  "optionsType",
  "optionsBrowserStyle",
  "aboutURL",
  "defaultLocale",
  "visible",
  "active",
  "userDisabled",
  "appDisabled",
  "embedderDisabled",
  "pendingUninstall",
  "installDate",
  "updateDate",
  "applyBackgroundUpdates",
  "path",
  "skinnable",
  "sourceURI",
  "releaseNotesURI",
  "softDisabled",
  "foreignInstall",
  "strictCompatibility",
  "locales",
  "targetApplications",
  "targetPlatforms",
  "signedState",
  "signedTypes",
  "signedDate",
  "seen",
  "dependencies",
  "incognito",
  "userPermissions",
  "optionalPermissions",
  "requestedPermissions",
  "icons",
  "iconURL",
  "blocklistAttentionDismissed",
  "blocklistState",
  "blocklistURL",
  "startupData",
  "previewImage",
  "hidden",
  "installTelemetryInfo",
  "recommendationState",
  "rootURI",
];

const SIGNED_TYPES = new Set(["extension", "locale", "theme"]);

// Time to wait before async save of XPI JSON database, in milliseconds
const ASYNC_SAVE_DELAY_MS = 20;

/**
 * Schedules an idle task, and returns a promise which resolves to an
 * IdleDeadline when an idle slice is available. The caller should
 * perform all of its idle work in the same micro-task, before the
 * deadline is reached.
 *
 * @returns {Promise<IdleDeadline>}
 */
function promiseIdleSlice() {
  return new Promise(resolve => {
    ChromeUtils.idleDispatch(resolve);
  });
}

let arrayForEach = Function.call.bind(Array.prototype.forEach);

/**
 * Loops over the given array, in the same way as Array forEach, but
 * splitting the work among idle tasks.
 *
 * @param {Array} array
 *        The array to loop over.
 * @param {function} func
 *        The function to call on each array element.
 * @param {integer} [taskTimeMS = 5]
 *        The minimum time to allocate to each task. If less time than
 *        this is available in a given idle slice, and there are more
 *        elements to loop over, they will be deferred until the next
 *        idle slice.
 */
async function idleForEach(array, func, taskTimeMS = 5) {
  let deadline;
  for (let i = 0; i < array.length; i++) {
    if (!deadline || deadline.timeRemaining() < taskTimeMS) {
      deadline = await promiseIdleSlice();
    }
    func(array[i], i);
  }
}

/**
 * Asynchronously fill in the _repositoryAddon field for one addon
 *
 * @param {AddonInternal} aAddon
 *        The add-on to annotate.
 * @returns {AddonInternal}
 *        The annotated add-on.
 */
async function getRepositoryAddon(aAddon) {
  if (aAddon) {
    aAddon._repositoryAddon = await lazy.AddonRepository.getCachedAddonByID(
      aAddon.id
    );
  }
  return aAddon;
}

/**
 * Copies properties from one object to another. If no target object is passed
 * a new object will be created and returned.
 *
 * @param {object} aObject
 *        An object to copy from
 * @param {string[]} aProperties
 *        An array of properties to be copied
 * @param {object?} [aTarget]
 *        An optional target object to copy the properties to
 * @returns {Object}
 *        The object that the properties were copied onto
 */
function copyProperties(aObject, aProperties, aTarget) {
  if (!aTarget) {
    aTarget = {};
  }
  aProperties.forEach(function (aProp) {
    if (aProp in aObject) {
      aTarget[aProp] = aObject[aProp];
    }
  });
  return aTarget;
}

// Maps instances of AddonInternal to AddonWrapper
const wrapperMap = new WeakMap();
let addonFor = wrapper => wrapperMap.get(wrapper);

const EMPTY_ARRAY = Object.freeze([]);

let AddonWrapper;

/**
 * The AddonInternal is an internal only representation of add-ons. It
 * may have come from the database or an extension manifest.
 */
export class AddonInternal {
  constructor(addonData) {
    this._wrapper = null;
    this._selectedLocale = null;
    this.active = false;
    this.visible = false;
    this.userDisabled = false;
    this.appDisabled = false;
    this.softDisabled = false;
    this.embedderDisabled = false;
    this.blocklistAttentionDismissed = false;
    this.blocklistState = nsIBlocklistService.STATE_NOT_BLOCKED;
    this.blocklistURL = null;
    this.sourceURI = null;
    this.releaseNotesURI = null;
    this.foreignInstall = false;
    this.seen = true;
    this.skinnable = false;
    this.startupData = null;
    this._hidden = false;
    this.installTelemetryInfo = null;
    this.rootURI = null;
    this._updateInstall = null;
    this.recommendationState = null;

    this.inDatabase = false;

    /**
     * @property {Array<string>} dependencies
     *   An array of bootstrapped add-on IDs on which this add-on depends.
     *   The add-on will remain appDisabled if any of the dependent
     *   add-ons is not installed and enabled.
     */
    this.dependencies = EMPTY_ARRAY;

    if (addonData) {
      copyProperties(addonData, PROP_JSON_FIELDS, this);
      this.location = addonData.location;

      if (!this.dependencies) {
        this.dependencies = [];
      }
      Object.freeze(this.dependencies);

      if (this.location) {
        this.addedToDatabase();
      }

      this.sourceBundle = addonData._sourceBundle;
    }
  }

  get sourceBundle() {
    return this._sourceBundle;
  }

  set sourceBundle(file) {
    this._sourceBundle = file;
    if (file) {
      this.rootURI = XPIExports.XPIInternal.getURIForResourceInFile(
        file,
        ""
      ).spec;
    }
  }

  get wrapper() {
    if (!this._wrapper) {
      this._wrapper = new AddonWrapper(this);
    }
    return this._wrapper;
  }

  get resolvedRootURI() {
    return XPIExports.XPIInternal.maybeResolveURI(
      Services.io.newURI(this.rootURI)
    );
  }

  get isBuiltinColorwayTheme() {
    return (
      this.type === "theme" &&
      this.location.isBuiltin &&
      this.id.endsWith("-colorway@mozilla.org")
    );
  }

  /**
   * Validate a list of origins are contained in the installOrigins array (defined in manifest.json).
   *
   * SitePermission addons are a special case, where the triggering install site may be a subdomain
   * of a valid xpi origin.
   *
   * @param {Object}  origins             Object containing URIs related to install.
   * @params {nsIURI} origins.installFrom The nsIURI of the website that has triggered the install flow.
   * @params {nsIURI} origins.source      The nsIURI where the xpi is hosted.
   * @returns {boolean}
   */
  validInstallOrigins({ installFrom, source }) {
    if (
      !Services.prefs.getBoolPref("extensions.install_origins.enabled", true)
    ) {
      return true;
    }

    let { installOrigins, manifestVersion } = this;
    if (!installOrigins) {
      // Install origins are mandatory in MV3 and optional
      // in MV2.  Old addons need to keep installing per the
      // old install flow.
      return manifestVersion < 3;
    }
    // An empty install_origins prevents any install from 3rd party websites.
    if (!installOrigins.length) {
      return false;
    }

    for (const [name, uri] of Object.entries({ installFrom, source })) {
      if (!installOrigins.includes(URL.fromURI(uri).origin)) {
        logger.warn(
          `Addon ${this.id} Installation not allowed, ${name} "${uri.spec}" is not included in the Addon install_origins`
        );
        return false;
      }
    }
    return true;
  }

  addedToDatabase() {
    this._key = `${this.location.name}:${this.id}`;
    this.inDatabase = true;
  }

  get isWebExtension() {
    return this.loader == null;
  }

  get selectedLocale() {
    if (this._selectedLocale) {
      return this._selectedLocale;
    }

    /**
     * this.locales is a list of objects that have property `locales`.
     * It's value is an array of locale codes.
     *
     * First, we reduce this nested structure to a flat list of locale codes.
     */
    const locales = [].concat(...this.locales.map(loc => loc.locales));

    let requestedLocales = Services.locale.requestedLocales;

    /**
     * If en-US is not in the list, add it as the last fallback.
     */
    if (!requestedLocales.includes("en-US")) {
      requestedLocales.push("en-US");
    }

    /**
     * Then we negotiate best locale code matching the app locales.
     */
    let bestLocale = Services.locale.negotiateLanguages(
      requestedLocales,
      locales,
      "und",
      Services.locale.langNegStrategyLookup
    )[0];

    /**
     * If no match has been found, we'll assign the default locale as
     * the selected one.
     */
    if (bestLocale === "und") {
      this._selectedLocale = this.defaultLocale;
    } else {
      /**
       * Otherwise, we'll go through all locale entries looking for the one
       * that has the best match in it's locales list.
       */
      this._selectedLocale = this.locales.find(loc =>
        loc.locales.includes(bestLocale)
      );
    }

    return this._selectedLocale;
  }

  get providesUpdatesSecurely() {
    return !this.updateURL || this.updateURL.startsWith("https:");
  }

  get isCorrectlySigned() {
    switch (this.location.name) {
      case KEY_APP_SYSTEM_PROFILE:
        // Add-ons installed via Normandy must be signed by the system
        // key or the "Mozilla Extensions" key.
        return [
          lazy.AddonManager.SIGNEDSTATE_SYSTEM,
          lazy.AddonManager.SIGNEDSTATE_PRIVILEGED,
        ].includes(this.signedState);
      case KEY_APP_SYSTEM_ADDONS:
        // System add-ons must be signed by the system key.
        return this.signedState == lazy.AddonManager.SIGNEDSTATE_SYSTEM;

      case KEY_APP_SYSTEM_BUILTINS:
      case KEY_APP_BUILTINS:
      case KEY_APP_TEMPORARY:
        // Temporary and built-in add-ons do not require signing.
        return true;

      case KEY_APP_SYSTEM_SHARE:
      case KEY_APP_SYSTEM_LOCAL:
        // On UNIX platforms except OSX, an additional location for system
        // add-ons exists in /usr/{lib,share}/mozilla/extensions. Add-ons
        // installed there do not require signing.
        if (Services.appinfo.OS != "Darwin") {
          return true;
        }
        break;
    }

    if (this.signedState === lazy.AddonManager.SIGNEDSTATE_NOT_REQUIRED) {
      return true;
    }
    return this.signedState > lazy.AddonManager.SIGNEDSTATE_MISSING;
  }

  get isCompatible() {
    return this.isCompatibleWith();
  }

  get isPrivileged() {
    return lazy.ExtensionData.getIsPrivileged({
      signedState: this.signedState,
      builtIn: this.location.isBuiltin,
      temporarilyInstalled: this.location.isTemporary,
    });
  }

  get hidden() {
    return (
      this.location.hidden ||
      // The hidden flag is intended to only be used for features that are part
      // of the application. Temporary add-ons should not be hidden.
      (this._hidden && this.isPrivileged && !this.location.isTemporary) ||
      false
    );
  }

  set hidden(val) {
    this._hidden = val;
  }

  get disabled() {
    return (
      this.userDisabled ||
      this.appDisabled ||
      this.softDisabled ||
      this.embedderDisabled
    );
  }

  get isPlatformCompatible() {
    if (!this.targetPlatforms.length) {
      return true;
    }

    let matchedOS = false;

    // If any targetPlatform matches the OS and contains an ABI then we will
    // only match a targetPlatform that contains both the current OS and ABI
    let needsABI = false;

    // Some platforms do not specify an ABI, test against null in that case.
    let abi = null;
    try {
      abi = Services.appinfo.XPCOMABI;
    } catch (e) {}

    // Something is causing errors in here
    try {
      for (let platform of this.targetPlatforms) {
        if (platform.os == Services.appinfo.OS) {
          if (platform.abi) {
            needsABI = true;
            if (platform.abi === abi) {
              return true;
            }
          } else {
            matchedOS = true;
          }
        }
      }
    } catch (e) {
      let message =
        "Problem with addon " +
        this.id +
        " targetPlatforms " +
        JSON.stringify(this.targetPlatforms);
      logger.error(message, e);
      lazy.AddonManagerPrivate.recordException("XPI", message, e);
      // don't trust this add-on
      return false;
    }

    return matchedOS && !needsABI;
  }

  isCompatibleWith(aAppVersion, aPlatformVersion) {
    let app = this.matchingTargetApplication;
    if (!app) {
      return false;
    }

    // set reasonable defaults for minVersion and maxVersion
    let minVersion = app.minVersion || "0";
    let maxVersion = app.maxVersion || "*";

    if (!aAppVersion) {
      aAppVersion = Services.appinfo.version;
    }
    if (!aPlatformVersion) {
      aPlatformVersion = Services.appinfo.platformVersion;
    }

    let version;
    if (app.id == Services.appinfo.ID) {
      version = aAppVersion;
    } else if (app.id == TOOLKIT_ID) {
      version = aPlatformVersion;
    }

    // Only extensions and dictionaries can be compatible by default; themes
    // and language packs always use strict compatibility checking.
    // Dictionaries are compatible by default unless requested by the dictinary.
    if (
      !this.strictCompatibility &&
      (!lazy.AddonManager.strictCompatibility || this.type == "dictionary")
    ) {
      return Services.vc.compare(version, minVersion) >= 0;
    }

    return (
      Services.vc.compare(version, minVersion) >= 0 &&
      Services.vc.compare(version, maxVersion) <= 0
    );
  }

  get matchingTargetApplication() {
    let app = null;
    for (let targetApp of this.targetApplications) {
      if (targetApp.id == Services.appinfo.ID) {
        return targetApp;
      }
      if (targetApp.id == TOOLKIT_ID) {
        app = targetApp;
      }
    }
    return app;
  }

  updateBlocklistAttentionDismissed(val) {
    if (!this.inDatabase || this.blocklistAttentionDismissed === val) {
      return;
    }
    this.blocklistAttentionDismissed = val;
    XPIDatabase.maybeUpdateBlocklistAttentionAddonIdsSet(this);
    XPIDatabase.saveChanges();
  }

  async findBlocklistEntry() {
    return lazy.Blocklist.getAddonBlocklistEntry(this.wrapper);
  }

  async updateBlocklistState(options = {}) {
    if (this.location.isSystem || this.location.isBuiltin) {
      return;
    }

    let { applySoftBlock = true, updateDatabase = true } = options;

    let oldState = this.blocklistState;

    let entry = await this.findBlocklistEntry();
    let newState = entry ? entry.state : Services.blocklist.STATE_NOT_BLOCKED;

    // Clear the blocklistAttentionDismissed flag if the blocklist state
    // is changing.
    if (this.blocklistState !== newState) {
      this.updateBlocklistAttentionDismissed(false);
    }

    this.blocklistState = newState;
    this.blocklistURL = entry && entry.url;

    let userDisabled, softDisabled;
    // After a blocklist update, the blocklist service manually applies
    // new soft blocks after displaying a UI, in which cases we need to
    // skip updating it here.
    if (applySoftBlock && oldState != newState) {
      if (newState == Services.blocklist.STATE_SOFTBLOCKED) {
        if (this.type == "theme") {
          userDisabled = true;
        } else {
          softDisabled = !this.userDisabled;
        }
      } else {
        softDisabled = false;
      }
    }

    if (this.inDatabase && updateDatabase) {
      await XPIDatabase.updateAddonDisabledState(this, {
        userDisabled,
        softDisabled,
      });
      XPIDatabase.saveChanges();
    } else {
      this.appDisabled = !XPIDatabase.isUsableAddon(this);
      if (userDisabled !== undefined) {
        this.userDisabled = userDisabled;
      }
      if (softDisabled !== undefined) {
        this.softDisabled = softDisabled;
      }
    }

    if (oldState != newState) {
      lazy.AddonManagerPrivate.callAddonListeners(
        "onPropertyChanged",
        this.wrapper,
        ["blocklistState"]
      );
      if (this.active) {
        // Make sure to sync the XPIState with the blocklistState
        // set in the AddonDB if the addon is active.
        XPIDatabase.updateXPIStates(this);
      }
    }
  }

  recordAddonBlockChangeTelemetry(reason) {
    lazy.Blocklist.recordAddonBlockChangeTelemetry(this.wrapper, reason);
  }

  async setUserDisabled(val, allowSystemAddons = false) {
    if (val == (this.userDisabled || this.softDisabled)) {
      return;
    }

    if (this.inDatabase) {
      // System add-ons should not be user disabled, as there is no UI to
      // re-enable them.
      if (this.location.isSystem && !allowSystemAddons) {
        throw new Error(`Cannot disable system add-on ${this.id}`);
      }
      await XPIDatabase.updateAddonDisabledState(this, { userDisabled: val });
    } else {
      this.userDisabled = val;
      // When enabling remove the softDisabled flag
      if (!val) {
        this.softDisabled = false;
      }
    }
  }

  applyCompatibilityUpdate(aUpdate, aSyncCompatibility) {
    let wasCompatible = this.isCompatible;

    for (let targetApp of this.targetApplications) {
      for (let updateTarget of aUpdate.targetApplications) {
        if (
          targetApp.id == updateTarget.id &&
          (aSyncCompatibility ||
            Services.vc.compare(targetApp.maxVersion, updateTarget.maxVersion) <
              0)
        ) {
          targetApp.minVersion = updateTarget.minVersion;
          targetApp.maxVersion = updateTarget.maxVersion;

          if (this.inDatabase) {
            XPIDatabase.saveChanges();
          }
        }
      }
    }

    if (wasCompatible != this.isCompatible) {
      if (this.inDatabase) {
        XPIDatabase.updateAddonDisabledState(this);
      } else {
        this.appDisabled = !XPIDatabase.isUsableAddon(this);
      }
    }
  }

  toJSON() {
    let obj = copyProperties(this, PROP_JSON_FIELDS);
    obj.location = this.location.name;
    return obj;
  }

  /**
   * When an add-on install is pending its metadata will be cached in a file.
   * This method reads particular properties of that metadata that may be newer
   * than that in the extension manifest, like compatibility information.
   *
   * @param {Object} aObj
   *        A JS object containing the cached metadata
   */
  importMetadata(aObj) {
    for (let prop of PENDING_INSTALL_METADATA) {
      if (!(prop in aObj)) {
        continue;
      }

      this[prop] = aObj[prop];
    }

    // Compatibility info may have changed so update appDisabled
    this.appDisabled = !XPIDatabase.isUsableAddon(this);
  }

  permissions() {
    let permissions = 0;

    let settings = Services.policies?.getExtensionSettings(this.id) || {};
    // The permission to "toggle the private browsing access" is locked down
    // when the extension has opted out or it gets the permission automatically
    // on every extension startup (as system, privileged and builtin addons) or
    // when private browsing access as been set and locke dthrough enterprise
    // policy settings.
    if (
      this.type === "extension" &&
      this.incognito !== "not_allowed" &&
      this.signedState !== lazy.AddonManager.SIGNEDSTATE_PRIVILEGED &&
      this.signedState !== lazy.AddonManager.SIGNEDSTATE_SYSTEM &&
      !this.location.isBuiltin &&
      !("private_browsing" in settings)
    ) {
      // NOTE: This permission is computed even for addons not in the database because
      // it is being used in the first dialog part of the install flow, when the addon
      // may not be installed yet (and so also not in the database), to determine if
      // the private browsing permission toggle button should be shown.
      permissions |= lazy.AddonManager.PERM_CAN_CHANGE_PRIVATEBROWSING_ACCESS;
    }

    // Add-ons that aren't installed cannot be modified in any way
    if (!this.inDatabase) {
      return permissions;
    }

    if (!this.appDisabled) {
      if (this.userDisabled || this.softDisabled) {
        permissions |= lazy.AddonManager.PERM_CAN_ENABLE;
      } else if (this.type != "theme" || this.id != DEFAULT_THEME_ID) {
        // We do not expose disabling the default theme.
        permissions |= lazy.AddonManager.PERM_CAN_DISABLE;
      }
    }

    // Add-ons that are in locked install locations, or are pending uninstall
    // cannot be uninstalled or upgraded.  One caveat is extensions sideloaded
    // from non-profile locations. Since Firefox 73(?), new sideloaded extensions
    // from outside the profile have not been installed so any such extensions
    // must be from an older profile. Users may uninstall such an extension which
    // removes the related state from this profile but leaves the actual file alone
    // (since it is outside this profile and may be in use in other profiles)
    let changesAllowed = !this.location.locked && !this.pendingUninstall;
    if (changesAllowed) {
      // System add-on upgrades are triggered through a different mechanism (see updateSystemAddons())
      // Builtin addons are only upgraded with Firefox (or app) updates.
      let isSystem = this.location.isSystem || this.location.isBuiltin;
      // Add-ons that are installed by a file link cannot be upgraded.
      if (!isSystem && !this.location.isLinkedAddon(this.id)) {
        permissions |= lazy.AddonManager.PERM_CAN_UPGRADE;
      }
    }

    // We allow uninstall of legacy sideloaded extensions, even when in locked locations,
    // but we do not remove the addon file in that case.
    let isLegacySideload =
      this.foreignInstall &&
      !(this.location.scope & lazy.AddonSettings.SCOPES_SIDELOAD);
    if (changesAllowed || isLegacySideload) {
      permissions |= lazy.AddonManager.PERM_API_CAN_UNINSTALL;
      if (!this.location.isBuiltin) {
        permissions |= lazy.AddonManager.PERM_CAN_UNINSTALL;
      }
    }

    if (Services.policies) {
      if (!Services.policies.isAllowed(`uninstall-extension:${this.id}`)) {
        permissions &= ~lazy.AddonManager.PERM_CAN_UNINSTALL;
      }
      if (!Services.policies.isAllowed(`disable-extension:${this.id}`)) {
        permissions &= ~lazy.AddonManager.PERM_CAN_DISABLE;
      }
      if (Services.policies.getExtensionSettings(this.id)?.updates_disabled) {
        permissions &= ~lazy.AddonManager.PERM_CAN_UPGRADE;
      }
    }

    return permissions;
  }

  propagateDisabledState(oldAddon) {
    if (oldAddon) {
      this.userDisabled = oldAddon.userDisabled;
      this.embedderDisabled = oldAddon.embedderDisabled;
      this.softDisabled = oldAddon.softDisabled;
      this.blocklistState = oldAddon.blocklistState;
    }
  }
}

/**
 * The AddonWrapper wraps an Addon to provide the data visible to consumers of
 * the public API.
 *
 * NOTE: Do not add any new logic here.  Add it to AddonInternal and expose
 * through defineAddonWrapperProperty after this class definition.
 *
 * @param {AddonInternal} aAddon
 *        The add-on object to wrap.
 */
AddonWrapper = class {
  constructor(aAddon) {
    wrapperMap.set(this, aAddon);
  }

  get __AddonInternal__() {
    return addonFor(this);
  }

  get quarantineIgnoredByApp() {
    return this.isPrivileged || !!this.recommendationStates?.length;
  }

  get quarantineIgnoredByUser() {
    // NOTE: confirm if this getter could be replaced by a
    // lazy preference getter and the addon wrapper to not be
    // kept around longer by the pref observer registered
    // internally by the lazy getter.
    return lazy.QuarantinedDomains.isUserAllowedAddonId(this.id);
  }

  set quarantineIgnoredByUser(val) {
    lazy.QuarantinedDomains.setUserAllowedAddonIdPref(this.id, !!val);
  }

  get canChangeQuarantineIgnored() {
    // Never show the quarantined domains user controls UI if the
    // quarantined domains feature is disabled.
    return (
      WebExtensionPolicy.quarantinedDomainsEnabled &&
      !lazy.isQuarantineUIDisabled &&
      this.type === "extension" &&
      !this.quarantineIgnoredByApp
    );
  }

  get previousActiveThemeID() {
    if (this.type === "theme") {
      return addonFor(this).previousActiveThemeID;
    }
    return null;
  }

  get seen() {
    return addonFor(this).seen;
  }

  markAsSeen() {
    addonFor(this).seen = true;
    XPIDatabase.saveChanges();
  }

  get installTelemetryInfo() {
    const addon = addonFor(this);
    if (!addon.installTelemetryInfo && addon.location) {
      if (addon.location.isSystem) {
        return { source: "system-addon" };
      }

      if (addon.location.isTemporary) {
        return { source: "temporary-addon" };
      }
    }

    return addon.installTelemetryInfo;
  }

  get temporarilyInstalled() {
    return addonFor(this).location.isTemporary;
  }

  get aboutURL() {
    return this.isActive ? addonFor(this).aboutURL : null;
  }

  get optionsURL() {
    if (!this.isActive) {
      return null;
    }

    let addon = addonFor(this);
    if (addon.optionsURL) {
      if (this.isWebExtension) {
        // The internal object's optionsURL property comes from the addons
        // DB and should be a relative URL.  However, extensions with
        // options pages installed before bug 1293721 was fixed got absolute
        // URLs in the addons db.  This code handles both cases.
        let policy = WebExtensionPolicy.getByID(addon.id);
        if (!policy) {
          return null;
        }
        let base = policy.getURL();
        return new URL(addon.optionsURL, base).href;
      }
      return addon.optionsURL;
    }

    return null;
  }

  get optionsType() {
    if (!this.isActive) {
      return null;
    }

    let addon = addonFor(this);
    let hasOptionsURL = !!this.optionsURL;

    if (addon.optionsType) {
      switch (parseInt(addon.optionsType, 10)) {
        case lazy.AddonManager.OPTIONS_TYPE_TAB:
        case lazy.AddonManager.OPTIONS_TYPE_INLINE_BROWSER:
          return hasOptionsURL ? addon.optionsType : null;
      }
      return null;
    }

    return null;
  }

  get optionsBrowserStyle() {
    let addon = addonFor(this);
    return addon.optionsBrowserStyle;
  }

  get incognito() {
    return addonFor(this).incognito;
  }

  async getBlocklistURL() {
    return addonFor(this).blocklistURL;
  }

  get iconURL() {
    return lazy.AddonManager.getPreferredIconURL(this, 48);
  }

  get icons() {
    let addon = addonFor(this);
    let icons = {};

    if (addon._repositoryAddon) {
      for (let size in addon._repositoryAddon.icons) {
        icons[size] = addon._repositoryAddon.icons[size];
      }
    }

    if (addon.icons) {
      for (let size in addon.icons) {
        let path = addon.icons[size].replace(/^\//, "");
        icons[size] = this.getResourceURI(path).spec;
      }
    }

    let canUseIconURLs = this.isActive;
    if (canUseIconURLs && addon.iconURL) {
      icons[32] = addon.iconURL;
      icons[48] = addon.iconURL;
    }

    Object.freeze(icons);
    return icons;
  }

  get screenshots() {
    let addon = addonFor(this);
    let repositoryAddon = addon._repositoryAddon;
    if (repositoryAddon && "screenshots" in repositoryAddon) {
      let repositoryScreenshots = repositoryAddon.screenshots;
      if (repositoryScreenshots && repositoryScreenshots.length) {
        return repositoryScreenshots;
      }
    }

    if (addon.previewImage) {
      let url = this.getResourceURI(addon.previewImage).spec;
      return [new lazy.AddonManagerPrivate.AddonScreenshot(url)];
    }

    return null;
  }

  get recommendationStates() {
    let addon = addonFor(this);
    let state = addon.recommendationState;
    if (
      state &&
      state.validNotBefore < addon.updateDate &&
      state.validNotAfter > addon.updateDate &&
      addon.isCorrectlySigned &&
      !this.temporarilyInstalled
    ) {
      return state.states;
    }
    return [];
  }

  // NOTE: this boolean getter doesn't return true for all recommendation
  // states at the moment. For the states actually supported on the autograph
  // side see:
  // https://github.com/mozilla-services/autograph/blob/8a34847a/autograph.yaml#L1456-L1460
  get isRecommended() {
    return this.recommendationStates.includes("recommended");
  }

  get canBypassThirdParyInstallPrompt() {
    // We only bypass if the extension is signed (to support distributions
    // that turn off the signing requirement) and has recommendation states,
    // or the extension is signed as privileged.
    return (
      this.signedState == lazy.AddonManager.SIGNEDSTATE_PRIVILEGED ||
      (this.signedState >= lazy.AddonManager.SIGNEDSTATE_SIGNED &&
        this.recommendationStates.length)
    );
  }

  get applyBackgroundUpdates() {
    return addonFor(this).applyBackgroundUpdates;
  }
  set applyBackgroundUpdates(val) {
    let addon = addonFor(this);
    if (
      val != lazy.AddonManager.AUTOUPDATE_DEFAULT &&
      val != lazy.AddonManager.AUTOUPDATE_DISABLE &&
      val != lazy.AddonManager.AUTOUPDATE_ENABLE
    ) {
      val = val
        ? lazy.AddonManager.AUTOUPDATE_DEFAULT
        : lazy.AddonManager.AUTOUPDATE_DISABLE;
    }

    if (val == addon.applyBackgroundUpdates) {
      return;
    }

    XPIDatabase.setAddonProperties(addon, {
      applyBackgroundUpdates: val,
    });
    lazy.AddonManagerPrivate.callAddonListeners("onPropertyChanged", this, [
      "applyBackgroundUpdates",
    ]);
  }

  set syncGUID(val) {
    let addon = addonFor(this);
    if (addon.syncGUID == val) {
      return;
    }

    if (addon.inDatabase) {
      XPIDatabase.setAddonSyncGUID(addon, val);
    }

    addon.syncGUID = val;
  }

  get install() {
    let addon = addonFor(this);
    if (!("_install" in addon) || !addon._install) {
      return null;
    }
    return addon._install.wrapper;
  }

  get updateInstall() {
    let addon = addonFor(this);
    return addon._updateInstall ? addon._updateInstall.wrapper : null;
  }

  get pendingUpgrade() {
    let addon = addonFor(this);
    return addon.pendingUpgrade ? addon.pendingUpgrade.wrapper : null;
  }

  get scope() {
    let addon = addonFor(this);
    if (addon.location) {
      return addon.location.scope;
    }

    return lazy.AddonManager.SCOPE_PROFILE;
  }

  get locationName() {
    let addon = addonFor(this);
    return addon.location.name;
  }

  get pendingOperations() {
    let addon = addonFor(this);
    let pending = 0;
    if (!addon.inDatabase) {
      // Add-on is pending install if there is no associated install (shouldn't
      // happen here) or if the install is in the process of or has successfully
      // completed the install. If an add-on is pending install then we ignore
      // any other pending operations.
      if (
        !addon._install ||
        addon._install.state == lazy.AddonManager.STATE_INSTALLING ||
        addon._install.state == lazy.AddonManager.STATE_INSTALLED
      ) {
        return lazy.AddonManager.PENDING_INSTALL;
      }
    } else if (addon.pendingUninstall) {
      // If an add-on is pending uninstall then we ignore any other pending
      // operations
      return lazy.AddonManager.PENDING_UNINSTALL;
    }

    if (addon.active && addon.disabled) {
      pending |= lazy.AddonManager.PENDING_DISABLE;
    } else if (!addon.active && !addon.disabled) {
      pending |= lazy.AddonManager.PENDING_ENABLE;
    }

    if (addon.pendingUpgrade) {
      pending |= lazy.AddonManager.PENDING_UPGRADE;
    }

    return pending;
  }

  get operationsRequiringRestart() {
    return 0;
  }

  get isDebuggable() {
    return this.isActive;
  }

  get permissions() {
    return addonFor(this).permissions();
  }

  get isActive() {
    let addon = addonFor(this);
    if (!addon.active) {
      return false;
    }
    if (!Services.appinfo.inSafeMode) {
      return true;
    }
    return XPIExports.XPIInternal.canRunInSafeMode(addon);
  }

  get startupPromise() {
    let addon = addonFor(this);
    if (!this.isActive) {
      return null;
    }

    let activeAddon = XPIExports.XPIProvider.activeAddons.get(addon.id);
    if (activeAddon) {
      return activeAddon.startupPromise || null;
    }
    return null;
  }

  get blocklistAttentionDismissed() {
    let addon = addonFor(this);
    return addon.blocklistAttentionDismissed;
  }

  set blocklistAttentionDismissed(val) {
    let addon = addonFor(this);
    addon.updateBlocklistAttentionDismissed(val);
  }

  updateBlocklistState(applySoftBlock = true) {
    return addonFor(this).updateBlocklistState({ applySoftBlock });
  }

  get userDisabled() {
    let addon = addonFor(this);
    return addon.softDisabled || addon.userDisabled;
  }

  /**
   * Get the embedderDisabled property for this addon.
   *
   * This is intended for embedders of Gecko like GeckoView apps to control
   * which addons are usable on their app.
   *
   * @returns {boolean}
   */
  get embedderDisabled() {
    if (!lazy.AddonSettings.IS_EMBEDDED) {
      return undefined;
    }

    return addonFor(this).embedderDisabled;
  }

  /**
   * Set the embedderDisabled property for this addon.
   *
   * This is intended for embedders of Gecko like GeckoView apps to control
   * which addons are usable on their app.
   *
   * Embedders can disable addons for various reasons, e.g. the addon is not
   * compatible with their implementation of the WebExtension API.
   *
   * When an addon is embedderDisabled it will behave like it was appDisabled.
   *
   * @param {boolean} val
   *        whether this addon should be embedder disabled or not.
   */
  async setEmbedderDisabled(val) {
    if (!lazy.AddonSettings.IS_EMBEDDED) {
      throw new Error("Setting embedder disabled while not embedding.");
    }

    let addon = addonFor(this);
    if (addon.embedderDisabled == val) {
      return val;
    }

    if (addon.inDatabase) {
      await XPIDatabase.updateAddonDisabledState(addon, {
        embedderDisabled: val,
      });
    } else {
      addon.embedderDisabled = val;
    }

    return val;
  }

  enable(options = {}) {
    const { allowSystemAddons = false } = options;
    return addonFor(this).setUserDisabled(false, allowSystemAddons);
  }

  disable(options = {}) {
    const { allowSystemAddons = false } = options;
    return addonFor(this).setUserDisabled(true, allowSystemAddons);
  }

  async setSoftDisabled(val) {
    let addon = addonFor(this);
    if (val == addon.softDisabled) {
      return val;
    }

    if (addon.inDatabase) {
      // When softDisabling a theme just enable the active theme
      if (addon.type === "theme" && val && !addon.userDisabled) {
        if (addon.isWebExtension) {
          await XPIDatabase.updateAddonDisabledState(addon, {
            softDisabled: val,
          });
        }
      } else {
        await XPIDatabase.updateAddonDisabledState(addon, {
          softDisabled: val,
        });
      }
    } else if (!addon.userDisabled) {
      // Only set softDisabled if not already disabled
      addon.softDisabled = val;
    }

    return val;
  }

  get isPrivileged() {
    return addonFor(this).isPrivileged;
  }

  get hidden() {
    return addonFor(this).hidden;
  }

  get isSystem() {
    let addon = addonFor(this);
    return addon.location.isSystem;
  }

  get isBuiltin() {
    return addonFor(this).location.isBuiltin;
  }

  // Returns true if Firefox Sync should sync this addon. Only addons
  // in the profile install location are considered syncable.
  get isSyncable() {
    let addon = addonFor(this);
    return addon.location.name == KEY_APP_PROFILE;
  }

  /**
   * Returns true if the addon is configured to be installed
   * by enterprise policy.
   */
  get isInstalledByEnterprisePolicy() {
    const policySettings = Services.policies?.getExtensionSettings(this.id);
    return ["force_installed", "normal_installed"].includes(
      policySettings?.installation_mode
    );
  }

  /**
   * Required permissions that extension has access to based on its manifest.
   * In mv3 this doesn't include host_permissions.
   */
  get userPermissions() {
    return addonFor(this).userPermissions;
  }

  get optionalPermissions() {
    return addonFor(this).optionalPermissions;
  }

  /**
   * Additional permissions that extension is requesting in its manifest.
   * Currently this is host_permissions in MV3.
   */
  get requestedPermissions() {
    return addonFor(this).requestedPermissions;
  }

  /**
   * A helper that returns all permissions for the install prompt.
   */
  get installPermissions() {
    let required = this.userPermissions;
    if (!required) {
      return null;
    }
    let requested = this.requestedPermissions;
    // Currently this can't result in duplicates, but if logic of what goes
    // into these lists changes, make sure to check for dupes.
    let perms = {
      origins: required.origins.concat(requested?.origins ?? []),
      permissions: required.permissions.concat(requested?.permissions ?? []),
      data_collection: required.data_collection.concat(
        requested?.data_collection ?? []
      ),
    };
    return perms;
  }

  get optionalOriginsNormalized() {
    const { permissions } = this.userPermissions ?? {};

    const priv = this.isPrivileged && permissions?.includes("mozillaAddons");
    const mps = new MatchPatternSet(this.optionalPermissions?.origins ?? [], {
      restrictSchemes: !priv,
      ignorePath: true,
    });

    let temp = [...lazy.ExtensionPermissions.tempOrigins.get(this.id)];
    let origins = [
      ...mps.patterns.map(matcher => matcher.pattern),
      ...temp.filter(o =>
        // Make sure origins are still in the current set of optional
        // permissions, which might have changed on extension update.
        mps.subsumes(new MatchPattern(o, { restrictSchemes: !priv }))
      ),
    ];

    // De-dup the normalized host permission patterns.
    return [...new Set(origins)];
  }

  isCompatibleWith(aAppVersion, aPlatformVersion) {
    return addonFor(this).isCompatibleWith(aAppVersion, aPlatformVersion);
  }

  async uninstall(alwaysAllowUndo) {
    let addon = addonFor(this);
    return XPIExports.XPIInstall.uninstallAddon(addon, alwaysAllowUndo);
  }

  cancelUninstall() {
    let addon = addonFor(this);
    XPIExports.XPIInstall.cancelUninstallAddon(addon);
  }

  findUpdates(aListener, aReason, aAppVersion, aPlatformVersion) {
    new XPIExports.UpdateChecker(
      addonFor(this),
      aListener,
      aReason,
      aAppVersion,
      aPlatformVersion
    );
  }

  // Returns true if there was an update in progress, false if there was no update to cancel
  cancelUpdate() {
    let addon = addonFor(this);
    if (addon._updateCheck) {
      addon._updateCheck.cancel();
      return true;
    }
    return false;
  }

  /**
   * Reloads the add-on.
   *
   * For temporarily installed add-ons, this uninstalls and re-installs the
   * add-on. Otherwise, the addon is disabled and then re-enabled, and the cache
   * is flushed.
   */
  async reload() {
    const addon = addonFor(this);

    logger.debug(`reloading add-on ${addon.id}`);

    if (!this.temporarilyInstalled) {
      await XPIDatabase.updateAddonDisabledState(addon, { userDisabled: true });
      await XPIDatabase.updateAddonDisabledState(addon, {
        userDisabled: false,
      });
    } else {
      // This function supports re-installing an existing add-on.
      await lazy.AddonManager.installTemporaryAddon(addon._sourceBundle);
    }
  }

  /**
   * Returns a URI to the selected resource or to the add-on bundle if aPath
   * is null. URIs to the bundle will always be file: URIs. URIs to resources
   * will be file: URIs if the add-on is unpacked or jar: URIs if the add-on is
   * still an XPI file.
   *
   * @param {string?} aPath
   *        The path in the add-on to get the URI for or null to get a URI to
   *        the file or directory the add-on is installed as.
   * @returns {nsIURI}
   */
  getResourceURI(aPath) {
    let addon = addonFor(this);
    let url = Services.io.newURI(addon.rootURI);
    if (aPath) {
      if (aPath.startsWith("/")) {
        throw new Error("getResourceURI() must receive a relative path");
      }
      url = Services.io.newURI(aPath, null, url);
    }
    return url;
  }
};

function chooseValue(aAddon, aObj, aProp) {
  let repositoryAddon = aAddon._repositoryAddon;
  let objValue = aObj[aProp];

  if (
    repositoryAddon &&
    aProp in repositoryAddon &&
    (aProp === "creator" || objValue == null)
  ) {
    return [repositoryAddon[aProp], true];
  }

  return [objValue, false];
}

function defineAddonWrapperProperty(name, getter) {
  Object.defineProperty(AddonWrapper.prototype, name, {
    get: getter,
    enumerable: true,
  });
}

[
  "id",
  "syncGUID",
  "version",
  "type",
  "isWebExtension",
  "isCompatible",
  "isPlatformCompatible",
  "providesUpdatesSecurely",
  "blocklistState",
  "appDisabled",
  "softDisabled",
  "skinnable",
  "foreignInstall",
  "strictCompatibility",
  "updateURL",
  "installOrigins",
  "manifestVersion",
  "validInstallOrigins",
  "dependencies",
  "signedState",
  "signedTypes",
  "isCorrectlySigned",
  "isBuiltinColorwayTheme",
].forEach(function (aProp) {
  defineAddonWrapperProperty(aProp, function () {
    let addon = addonFor(this);
    return aProp in addon ? addon[aProp] : undefined;
  });
});

[
  "fullDescription",
  "supportURL",
  "contributionURL",
  "averageRating",
  "reviewCount",
  "reviewURL",
  "weeklyDownloads",
  "amoListingURL",
].forEach(function (aProp) {
  defineAddonWrapperProperty(aProp, function () {
    let addon = addonFor(this);
    if (addon._repositoryAddon) {
      return addon._repositoryAddon[aProp];
    }

    return null;
  });
});

["installDate", "updateDate"].forEach(function (aProp) {
  defineAddonWrapperProperty(aProp, function () {
    let addon = addonFor(this);
    // installDate is always set, updateDate is sometimes missing.
    return new Date(addon[aProp] ?? addon.installDate);
  });
});

defineAddonWrapperProperty("signedDate", function () {
  let addon = addonFor(this);
  let { signedDate } = addon;
  if (signedDate != null) {
    return new Date(signedDate);
  }
  return null;
});

["sourceURI", "releaseNotesURI"].forEach(function (aProp) {
  defineAddonWrapperProperty(aProp, function () {
    let addon = addonFor(this);

    // Temporary Installed Addons do not have a "sourceURI",
    // But we can use the "_sourceBundle" as an alternative,
    // which points to the path of the addon xpi installed
    // or its source dir (if it has been installed from a
    // directory).
    if (aProp == "sourceURI" && this.temporarilyInstalled) {
      return Services.io.newFileURI(addon._sourceBundle);
    }

    let [target, fromRepo] = chooseValue(addon, addon, aProp);
    if (!target) {
      return null;
    }
    if (fromRepo) {
      return target;
    }
    return Services.io.newURI(target);
  });
});

// Add to this Map if you need to change an addon's Fluent ID. Keep it in sync
// with the list in browser_verify_l10n_strings.js
const updatedAddonFluentIds = new Map([
  ["extension-default-theme-name", "extension-default-theme-name-auto"],
]);

["name", "description", "creator", "homepageURL"].forEach(function (aProp) {
  defineAddonWrapperProperty(aProp, function () {
    let addon = addonFor(this);

    let formattedMessage;
    // We want to make sure that all built-in themes that are localizable can
    // actually localized, particularly those for thunderbird and desktop.
    if (
      (aProp === "name" || aProp === "description") &&
      addon.location.name === KEY_APP_BUILTINS &&
      addon.type === "theme"
    ) {
      // Built-in themes are localized with Fluent instead of the WebExtension API.
      let addonIdPrefix = addon.id.replace("@mozilla.org", "");
      let defaultFluentId = `extension-${addonIdPrefix}-${aProp}`;
      let fluentId =
        updatedAddonFluentIds.get(defaultFluentId) || defaultFluentId;
      try {
        const l10n = new Localization(["browser/appExtensionFields.ftl"], true);
        [formattedMessage] = l10n.formatMessagesSync([{ id: fluentId }]);
      } catch (e) {
        // Log a warning when no fluent string was found, but fallback to the value set
        // in the manifest field or values got from AMO and stored in the AddonRepository.
        logger.warn(
          `Failed to format fluent localized string for "${addon.id}" AddonWrapper property ${aProp}`,
          e
        );
      }
    }

    if (formattedMessage) {
      return formattedMessage.value;
    }

    let [result, usedRepository] = chooseValue(
      addon,
      addon.selectedLocale,
      aProp
    );

    if (result == null) {
      // Legacy add-ons may be partially localized. Fall back to the default
      // locale ensure that the result is a string where possible.
      [result, usedRepository] = chooseValue(addon, addon.defaultLocale, aProp);
    }

    if (result && !usedRepository && aProp == "creator") {
      return new lazy.AddonManagerPrivate.AddonAuthor(result);
    }

    return result;
  });
});

["developers", "translators", "contributors"].forEach(function (aProp) {
  defineAddonWrapperProperty(aProp, function () {
    let addon = addonFor(this);

    let [results, usedRepository] = chooseValue(
      addon,
      addon.selectedLocale,
      aProp
    );

    if (results && !usedRepository) {
      results = results.map(function (aResult) {
        return new lazy.AddonManagerPrivate.AddonAuthor(aResult);
      });
    }

    return results;
  });
});

/**
 * @typedef {Map<string, AddonInternal>} AddonDB
 */

/**
 * Internal interface: find an addon from an already loaded addonDB.
 *
 * @param {AddonDB} addonDB
 *        The add-on database.
 * @param {function(AddonInternal) : boolean} aFilter
 *        The filter predecate. The first add-on for which it returns
 *        true will be returned.
 * @returns {AddonInternal?}
 *        The first matching add-on, if one is found.
 */
function _findAddon(addonDB, aFilter) {
  for (let addon of addonDB.values()) {
    if (aFilter(addon)) {
      return addon;
    }
  }
  return null;
}

/**
 * Internal interface to get a filtered list of addons from a loaded addonDB
 *
 * @param {AddonDB} addonDB
 *        The add-on database.
 * @param {function(AddonInternal) : boolean} aFilter
 *        The filter predecate. Add-ons which match this predicate will
 *        be returned.
 * @returns {Array<AddonInternal>}
 *        The list of matching add-ons.
 */
function _filterDB(addonDB, aFilter) {
  return Array.from(addonDB.values()).filter(aFilter);
}

export const XPIDatabase = {
  // true if the database connection has been opened
  initialized: false,
  // The database file
  jsonFilePath: PathUtils.join(PathUtils.profileDir, FILE_JSON_DB),
  rebuildingDatabase: false,
  syncLoadingDB: false,
  // Add-ons from the database in locations which are no longer
  // supported.
  orphanedAddons: [],

  // Set of the add-on ids for all the add-ons of type extension that are appDisabled or softDisabled
  // through the blocklist, excluding the ones that the user has already explicitly dismissed before
  // (used for the blocklist attention dot and messagebar to be shown in the extensions button/panel).
  //
  // Set<addonId: string>
  blocklistAttentionAddonIdsSet: new Set(),

  _saveTask: null,

  // Saved error object if we fail to read an existing database
  _loadError: null,

  // Saved error object if we fail to save the database
  _saveError: null,

  // Error reported by our most recent attempt to read or write the database, if any
  get lastError() {
    if (this._loadError) {
      return this._loadError;
    }
    if (this._saveError) {
      return this._saveError;
    }
    return null;
  },

  async _saveNow() {
    try {
      await IOUtils.writeJSON(this.jsonFilePath, this, {
        tmpPath: `${this.jsonFilePath}.tmp`,
      });

      if (!this._schemaVersionSet) {
        // Update the XPIDB schema version preference the first time we
        // successfully save the database.
        logger.debug(
          "XPI Database saved, setting schema version preference to " +
            XPIExports.XPIInternal.DB_SCHEMA
        );
        Services.prefs.setIntPref(
          PREF_DB_SCHEMA,
          XPIExports.XPIInternal.DB_SCHEMA
        );
        this._schemaVersionSet = true;

        // Reading the DB worked once, so we don't need the load error
        this._loadError = null;
      }
    } catch (error) {
      logger.warn("Failed to save XPI database", error);
      this._saveError = error;

      if (!DOMException.isInstance(error) || error.name !== "AbortError") {
        throw error;
      }
    }
  },

  /**
   * Mark the current stored data dirty, and schedule a flush to disk
   */
  saveChanges() {
    if (!this.initialized) {
      throw new Error("Attempt to use XPI database when it is not initialized");
    }

    if (XPIExports.XPIProvider._closing) {
      // use an Error here so we get a stack trace.
      let err = new Error("XPI database modified after shutdown began");
      logger.warn(err);
      lazy.AddonManagerPrivate.recordSimpleMeasure(
        "XPIDB_late_stack",
        Log.stackTrace(err)
      );
    }

    if (!this._saveTask) {
      this._saveTask = new lazy.DeferredTask(
        () => this._saveNow(),
        ASYNC_SAVE_DELAY_MS
      );
    }

    this._saveTask.arm();
  },

  async finalize() {
    // handle the "in memory only" and "saveChanges never called" cases
    if (!this._saveTask) {
      return;
    }

    await this._saveTask.finalize();
  },

  /**
   * Converts the current internal state of the XPI addon database to
   * a JSON.stringify()-ready structure
   *
   * @returns {Object}
   */
  toJSON() {
    if (!this.addonDB) {
      // We never loaded the database?
      throw new Error("Attempt to save database without loading it first");
    }

    let toSave = {
      schemaVersion: XPIExports.XPIInternal.DB_SCHEMA,
      addons: Array.from(this.addonDB.values()).filter(
        addon => !addon.location.isTemporary
      ),
    };
    return toSave;
  },

  /**
   * Synchronously loads the database, by running the normal async load
   * operation with idle dispatch disabled, and spinning the event loop
   * until it finishes.
   *
   * @param {boolean} aRebuildOnError
   *        A boolean indicating whether add-on information should be loaded
   *        from the install locations if the database needs to be rebuilt.
   *        (if false, caller is XPIProvider.checkForChanges() which will rebuild)
   */
  syncLoadDB(aRebuildOnError) {
    let err = new Error("Synchronously loading the add-ons database");
    logger.debug(err.message);
    lazy.AddonManagerPrivate.recordSimpleMeasure(
      "XPIDB_sync_stack",
      Log.stackTrace(err)
    );
    try {
      this.syncLoadingDB = true;
      XPIExports.XPIInternal.awaitPromise(this.asyncLoadDB(aRebuildOnError));
    } finally {
      this.syncLoadingDB = false;
    }
  },

  _recordStartupError(reason) {
    lazy.AddonManagerPrivate.recordSimpleMeasure("XPIDB_startupError", reason);
  },

  /**
   * Parse loaded data, reconstructing the database if the loaded data is not valid
   *
   * @param {object} aInputAddons
   *        The add-on JSON to parse.
   * @param {boolean} aRebuildOnError
   *        If true, synchronously reconstruct the database from installed add-ons
   */
  async parseDB(aInputAddons, aRebuildOnError) {
    try {
      let parseTimer = lazy.AddonManagerPrivate.simpleTimer("XPIDB_parseDB_MS");

      if (!("schemaVersion" in aInputAddons) || !("addons" in aInputAddons)) {
        let error = new Error("Bad JSON file contents");
        error.rebuildReason = "XPIDB_rebuildBadJSON_MS";
        throw error;
      }

      if (aInputAddons.schemaVersion <= 27) {
        // Types were translated in bug 857456.
        for (let addon of aInputAddons.addons) {
          XPIExports.XPIInternal.migrateAddonLoader(addon);
        }
      } else if (
        aInputAddons.schemaVersion != XPIExports.XPIInternal.DB_SCHEMA
      ) {
        // For now, we assume compatibility for JSON data with a
        // mismatched schema version, though we throw away any fields we
        // don't know about (bug 902956)
        this._recordStartupError(
          `schemaMismatch-${aInputAddons.schemaVersion}`
        );
        logger.debug(
          `JSON schema mismatch: expected ${XPIExports.XPIInternal.DB_SCHEMA}, actual ${aInputAddons.schemaVersion}`
        );
      }

      let forEach = this.syncLoadingDB ? arrayForEach : idleForEach;

      this.clearBlocklistAttentionAddonIdsSet();

      // If we got here, we probably have good data
      // Make AddonInternal instances from the loaded data and save them
      let addonDB = new Map();
      await forEach(aInputAddons.addons, loadedAddon => {
        if (loadedAddon.path) {
          try {
            loadedAddon._sourceBundle = new nsIFile(loadedAddon.path);
          } catch (e) {
            // We can fail here when the path is invalid, usually from the
            // wrong OS
            logger.warn(
              "Could not find source bundle for add-on " + loadedAddon.id,
              e
            );
          }
        }
        loadedAddon.location = XPIExports.XPIInternal.XPIStates.getLocation(
          loadedAddon.location
        );

        let newAddon = new AddonInternal(loadedAddon);
        if (loadedAddon.location) {
          addonDB.set(newAddon._key, newAddon);
          this.maybeUpdateBlocklistAttentionAddonIdsSet(newAddon);
        } else {
          this.orphanedAddons.push(newAddon);
        }
      });

      parseTimer.done();
      this.addonDB = addonDB;
      logger.debug("Successfully read XPI database");
      this.initialized = true;
    } catch (e) {
      if (e.name == "SyntaxError") {
        logger.error("Syntax error parsing saved XPI JSON data");
        this._recordStartupError("syntax");
      } else {
        logger.error("Failed to load XPI JSON data from profile", e);
        this._recordStartupError("other");
      }

      this.timeRebuildDatabase(
        e.rebuildReason || "XPIDB_rebuildReadFailed_MS",
        aRebuildOnError
      );
    }
  },

  async maybeIdleDispatch() {
    if (!this.syncLoadingDB) {
      await promiseIdleSlice();
    }
  },

  /**
   * Open and read the XPI database asynchronously, upgrading if
   * necessary. If any DB load operation fails, we need to
   * synchronously rebuild the DB from the installed extensions.
   *
   * @param {boolean} [aRebuildOnError = true]
   *        A boolean indicating whether add-on information should be loaded
   *        from the install locations if the database needs to be rebuilt.
   *        (if false, caller is XPIProvider.checkForChanges() which will rebuild)
   * @returns {Promise<AddonDB>}
   *        Resolves to the Map of loaded JSON data stored in
   *        this.addonDB; rejects in case of shutdown.
   */
  asyncLoadDB(aRebuildOnError = true) {
    // Already started (and possibly finished) loading
    if (this._dbPromise) {
      return this._dbPromise;
    }

    if (XPIExports.XPIProvider._closing) {
      // use an Error here so we get a stack trace.
      let err = new Error(
        "XPIDatabase.asyncLoadDB attempt after XPIProvider shutdown."
      );
      logger.warn("Fail to load AddonDB: ${error}", { error: err });
      lazy.AddonManagerPrivate.recordSimpleMeasure(
        "XPIDB_late_load",
        Log.stackTrace(err)
      );
      this._dbPromise = Promise.reject(err);

      XPIExports.XPIInternal.resolveDBReady(this._dbPromise);

      return this._dbPromise;
    }

    logger.debug(`Starting async load of XPI database ${this.jsonFilePath}`);
    this._dbPromise = (async () => {
      try {
        let json = await IOUtils.readJSON(this.jsonFilePath);

        logger.debug("Finished async read of XPI database, parsing...");
        await this.maybeIdleDispatch();
        await this.parseDB(json, true);
      } catch (error) {
        if (DOMException.isInstance(error) && error.name === "NotFoundError") {
          if (Services.prefs.getIntPref(PREF_DB_SCHEMA, 0)) {
            this._recordStartupError("dbMissing");
          }
        } else {
          logger.warn(
            `Extensions database ${this.jsonFilePath} exists but is not readable; rebuilding`,
            error
          );
          this._loadError = error;
        }
        this.timeRebuildDatabase(
          "XPIDB_rebuildUnreadableDB_MS",
          aRebuildOnError
        );
      }
      return this.addonDB;
    })();

    XPIExports.XPIInternal.resolveDBReady(this._dbPromise);

    return this._dbPromise;
  },

  timeRebuildDatabase(timerName, rebuildOnError) {
    lazy.AddonManagerPrivate.recordTiming(timerName, () => {
      return this.rebuildDatabase(rebuildOnError);
    });
  },

  /**
   * Rebuild the database from addon install directories.
   *
   * @param {boolean} aRebuildOnError
   *        A boolean indicating whether add-on information should be loaded
   *        from the install locations if the database needs to be rebuilt.
   *        (if false, caller is XPIProvider.checkForChanges() which will rebuild)
   */
  rebuildDatabase(aRebuildOnError) {
    this.addonDB = new Map();
    this.initialized = true;

    if (XPIExports.XPIInternal.XPIStates.size == 0) {
      // No extensions installed, so we're done
      logger.debug("Rebuilding XPI database with no extensions");
      return;
    }

    this.rebuildingDatabase = !!aRebuildOnError;

    if (aRebuildOnError) {
      logger.warn("Rebuilding add-ons database from installed extensions.");
      try {
        XPIDatabaseReconcile.processFileChanges({}, false);
      } catch (e) {
        logger.error(
          "Failed to rebuild XPI database from installed extensions",
          e
        );
      }
      // Make sure to update the active add-ons and add-ons list on shutdown
      Services.prefs.setBoolPref(PREF_PENDING_OPERATIONS, true);
    }
  },

  /**
   * Shuts down the database connection and releases all cached objects.
   * Return: Promise{integer} resolves / rejects with the result of the DB
   *                          flush after the database is flushed and
   *                          all cleanup is done
   */
  async shutdown() {
    logger.debug("shutdown");
    if (this.initialized) {
      // If our last database I/O had an error, try one last time to save.
      if (this.lastError) {
        this.saveChanges();
      }

      this.initialized = false;

      // If we're shutting down while still loading, finish loading
      // before everything else!
      if (this._dbPromise) {
        await this._dbPromise;
      }

      // Await any pending DB writes and finish cleaning up.
      await this.finalize();

      if (this._saveError) {
        // If our last attempt to read or write the DB failed, force a new
        // extensions.ini to be written to disk on the next startup
        Services.prefs.setBoolPref(PREF_PENDING_OPERATIONS, true);
      }

      // Clear out the cached addons data loaded from JSON
      delete this.addonDB;
      delete this._dbPromise;
      // same for the deferred save
      delete this._saveTask;
      // re-enable the schema version setter
      delete this._schemaVersionSet;
    }
  },

  /**
   * Verifies that all installed add-ons are still correctly signed.
   */
  async verifySignatures() {
    try {
      let addons = await this.getAddonList(() => true);

      let changes = {
        enabled: [],
        disabled: [],
      };

      for (let addon of addons) {
        // The add-on might have vanished, we'll catch that on the next startup
        if (!addon._sourceBundle || !addon._sourceBundle.exists()) {
          continue;
        }

        let { signedState, signedTypes } =
          await XPIExports.verifyBundleSignedState(addon._sourceBundle, addon);

        const changedProperties = [];

        if (signedState != addon.signedState) {
          addon.signedState = signedState;
          changedProperties.push("signedState");
        }

        if (
          addon.signedState === lazy.AddonManager.SIGNEDSTATE_SIGNED &&
          Services.policies
        ) {
          // Manifest file for an installed extension can still become
          // invalid (e.g. due to backward incompatible changes between
          // Firefox versions).
          try {
            const addonDetailsFromFile =
              await XPIExports.XPIInstall.loadManifestFromFile(
                addon._sourceBundle,
                addon.location
              );
            addon.adminInstallOnly = addonDetailsFromFile.adminInstallOnly;
          } catch (err) {
            // Simply log the error as a warning to be able to check
            // the signature and potentially update the disabled state
            // accordingly.
            logger.warn(`XPI_verifySignature Warning on '${addon.id}': ${err}`);
          }
        }

        if (
          !lazy.ObjectUtils.deepEqual(
            signedTypes?.toSorted(),
            addon.signedTypes?.toSorted()
          )
        ) {
          addon.signedTypes = signedTypes;
          changedProperties.push("signedTypes");
        }

        if (changedProperties.length) {
          lazy.AddonManagerPrivate.callAddonListeners(
            "onPropertyChanged",
            addon.wrapper,
            changedProperties
          );
        }

        let disabled = await this.updateAddonDisabledState(addon);
        if (disabled !== undefined) {
          changes[disabled ? "disabled" : "enabled"].push(addon.id);
        }
      }

      this.saveChanges();

      Services.obs.notifyObservers(
        null,
        "xpi-signature-changed",
        JSON.stringify(changes)
      );
    } catch (err) {
      logger.error("XPI_verifySignature: " + err);
    }
  },

  /**
   * Imports the xpinstall permissions from preferences into the permissions
   * manager for the user to change later.
   */
  importPermissions() {
    lazy.PermissionsUtils.importFromPrefs(
      PREF_XPI_PERMISSIONS_BRANCH,
      XPIExports.XPIInternal.XPI_PERMISSION
    );
  },

  /**
   * Called when a new add-on has been enabled when only one add-on of that type
   * can be enabled.
   *
   * @param {string} aId
   *        The ID of the newly enabled add-on
   * @param {string} aType
   *        The type of the newly enabled add-on
   */
  async addonChanged(aId, aType) {
    // We only care about themes in this provider
    if (aType !== "theme") {
      return;
    }

    Services.prefs.setCharPref(
      "extensions.activeThemeID",
      aId || DEFAULT_THEME_ID
    );

    let enableTheme;

    let addons = this.getAddonsByType("theme");
    let updateDisabledStatePromises = [];

    for (let theme of addons) {
      if (theme.visible) {
        if (!aId && theme.id == DEFAULT_THEME_ID) {
          enableTheme = theme;
        } else if (theme.id != aId && !theme.pendingUninstall) {
          updateDisabledStatePromises.push(
            this.updateAddonDisabledState(theme, {
              userDisabled: true,
              becauseSelecting: true,
            })
          );
        }
      }
    }

    await Promise.all(updateDisabledStatePromises);

    if (enableTheme) {
      await this.updateAddonDisabledState(enableTheme, {
        userDisabled: false,
        becauseSelecting: true,
      });
    }
  },

  SIGNED_TYPES,

  /**
   * Asynchronously list all addons that match the filter function
   *
   * @param {function(AddonInternal) : boolean} aFilter
   *        Function that takes an addon instance and returns
   *        true if that addon should be included in the selected array
   *
   * @returns {Array<AddonInternal>}
   *        A Promise that resolves to the list of add-ons matching
   *        aFilter or an empty array if none match
   */
  async getAddonList(aFilter) {
    try {
      let addonDB = await this.asyncLoadDB();
      let addonList = _filterDB(addonDB, aFilter);
      let addons = await Promise.all(
        addonList.map(addon => getRepositoryAddon(addon))
      );
      return addons;
    } catch (error) {
      logger.error("getAddonList failed", error);
      return [];
    }
  },

  /**
   * Get the first addon that matches the filter function
   *
   * @param {function(AddonInternal) : boolean} aFilter
   *        Function that takes an addon instance and returns
   *        true if that addon should be selected
   * @returns {Promise<AddonInternal?>}
   */
  getAddon(aFilter) {
    return this.asyncLoadDB()
      .then(addonDB => getRepositoryAddon(_findAddon(addonDB, aFilter)))
      .catch(error => {
        logger.error("getAddon failed", error);
      });
  },

  /**
   * Asynchronously gets an add-on with a particular ID in a particular
   * install location.
   *
   * @param {string} aId
   *        The ID of the add-on to retrieve
   * @param {string} aLocation
   *        The name of the install location
   * @returns {Promise<AddonInternal?>}
   */
  getAddonInLocation(aId, aLocation) {
    return this.asyncLoadDB().then(addonDB =>
      getRepositoryAddon(addonDB.get(aLocation + ":" + aId))
    );
  },

  /**
   * Asynchronously get all the add-ons in a particular install location.
   *
   * @param {string} aLocation
   *        The name of the install location
   * @returns {Promise<Array<AddonInternal>>}
   */
  getAddonsInLocation(aLocation) {
    return this.getAddonsInLocations([aLocation]);
  },

  /**
   * Asynchronously get all the add-ons in an array of install locations.
   *
   * @param {Array<string>} aLocations
   *        The name of the install location
   * @returns {Promise<Array<AddonInternal>>}
   */
  getAddonsInLocations(aLocations) {
    return this.getAddonList(aAddon =>
      aLocations.includes(aAddon.location.name)
    );
  },

  /**
   * Asynchronously gets the add-on with the specified ID that is visible.
   *
   * @param {string} aId
   *        The ID of the add-on to retrieve
   * @returns {Promise<AddonInternal?>}
   */
  getVisibleAddonForID(aId) {
    return this.getAddon(aAddon => aAddon.id == aId && aAddon.visible);
  },

  /**
   * Asynchronously gets the visible add-ons, optionally restricting by type.
   *
   * @param {Set<string>?} aTypes
   *        An array of types to include or null to include all types
   * @returns {Promise<Array<AddonInternal>>}
   */
  getVisibleAddons(aTypes) {
    return this.getAddonList(
      aAddon => aAddon.visible && (!aTypes || aTypes.has(aAddon.type))
    );
  },

  /**
   * Synchronously gets all add-ons of a particular type(s).
   *
   * @param {Array<string>} aTypes
   *        The type(s) of add-on to retrieve
   * @returns {Array<AddonInternal>}
   */
  getAddonsByType(...aTypes) {
    if (!this.addonDB) {
      // jank-tastic! Must synchronously load DB if the theme switches from
      // an XPI theme to a lightweight theme before the DB has loaded,
      // because we're called from sync XPIProvider.addonChanged
      logger.warn(
        `Synchronous load of XPI database due to ` +
          `getAddonsByType([${aTypes.join(", ")}]) ` +
          `Stack: ${Error().stack}`
      );
      this.syncLoadDB(true);
    }

    return _filterDB(this.addonDB, aAddon => aTypes.includes(aAddon.type));
  },

  /**
   * Asynchronously gets all add-ons with pending operations.
   *
   * @param {Set<string>?} aTypes
   *        The types of add-ons to retrieve or null to get all types
   * @returns {Promise<Array<AddonInternal>>}
   */
  getVisibleAddonsWithPendingOperations(aTypes) {
    return this.getAddonList(
      aAddon =>
        aAddon.visible &&
        aAddon.pendingUninstall &&
        (!aTypes || aTypes.has(aAddon.type))
    );
  },

  shouldShowBlocklistAttention() {
    return !!this.blocklistAttentionAddonIdsSet.size;
  },

  shouldShowBlocklistAttentionForAddon(addonInternal) {
    return (
      !addonInternal.hidden &&
      !addonInternal.blocklistAttentionDismissed &&
      (addonInternal.appDisabled || addonInternal.softDisabled) &&
      addonInternal.blocklistState > nsIBlocklistService.STATE_NOT_BLOCKED &&
      // We currently only draw the attention of the users when new add-ons of
      // type "extension" are being disabled by the blocklist.
      addonInternal.type === "extension"
    );
  },

  clearBlocklistAttentionAddonIdsSet() {
    this.blocklistAttentionAddonIdsSet.clear();
  },

  maybeUpdateBlocklistAttentionAddonIdsSet(addonInternal) {
    const blocklistAttentionSet = this.blocklistAttentionAddonIdsSet;
    if (!this.shouldShowBlocklistAttentionForAddon(addonInternal)) {
      blocklistAttentionSet.delete(addonInternal.id);
      Services.obs.notifyObservers(
        null,
        "xpi-provider:blocklist-attention-updated"
      );
      return;
    }

    blocklistAttentionSet.add(addonInternal.id);
    Services.obs.notifyObservers(
      null,
      "xpi-provider:blocklist-attention-updated"
    );
  },

  removeFromBlocklistAttentionAddonIdsSet(addonInternal) {
    this.blocklistAttentionAddonIdsSet.delete(addonInternal.id);
    Services.obs.notifyObservers(
      null,
      "xpi-provider:blocklist-attention-updated"
    );
  },

  async getBlocklistAttentionInfo() {
    const attentionAddonIdsSet = this.blocklistAttentionAddonIdsSet;
    const addonFilter = addonInternal =>
      attentionAddonIdsSet.has(addonInternal.id) &&
      this.shouldShowBlocklistAttentionForAddon(addonInternal);
    let addons = attentionAddonIdsSet.size
      ? await this.getAddonList(addonFilter)
      : [];
    // Filter the add-ons list once more synchronously in case any change may have happened
    // while we were retrieving the add-ons list asynchronously and we may not need to include
    // some in the blocklist attention message anymore (e.g. because they have been already
    // dismissed, or changed blocklistState or soft-blocked addon being already re-enabled).
    addons = addons.filter(addonFilter);

    return {
      get shouldShow() {
        return addons.some(addonFilter);
      },
      get hasSoftBlocked() {
        return addons.some(
          addonInternal =>
            addonInternal.blocklistState ===
            nsIBlocklistService.STATE_SOFTBLOCKED
        );
      },
      get hasHardBlocked() {
        return addons.some(
          addonInternal =>
            addonInternal.blocklistState === nsIBlocklistService.STATE_BLOCKED
        );
      },
      get extensionsCount() {
        return addons.length;
      },
      get addons() {
        return addons.map(addonInternal => addonInternal.wrapper);
      },
      dismiss() {
        addons.forEach(addon => addon.updateBlocklistAttentionDismissed(true));
      },
    };
  },

  /**
   * Synchronously gets all add-ons in the database.
   * This is only called from the preference observer for the default
   * compatibility version preference, so we can return an empty list if
   * we haven't loaded the database yet.
   *
   * @returns {Array<AddonInternal>}
   */
  getAddons() {
    if (!this.addonDB) {
      return [];
    }
    return _filterDB(this.addonDB, () => true);
  },

  /**
   * Called to get an Addon with a particular ID.
   *
   * @param {string} aId
   *        The ID of the add-on to retrieve
   * @returns {Addon?}
   */
  async getAddonByID(aId) {
    let aAddon = await this.getVisibleAddonForID(aId);
    return aAddon ? aAddon.wrapper : null;
  },

  /**
   * Obtain an Addon having the specified Sync GUID.
   *
   * @param {string} aGUID
   *        String GUID of add-on to retrieve
   * @returns {Addon?}
   */
  async getAddonBySyncGUID(aGUID) {
    let addon = await this.getAddon(aAddon => aAddon.syncGUID == aGUID);
    return addon ? addon.wrapper : null;
  },

  /**
   * Called to get Addons of a particular type.
   *
   * @param {Array<string>?} aTypes
   *        An array of types to fetch. Can be null to get all types.
   * @returns {Addon[]}
   */
  async getAddonsByTypes(aTypes) {
    let addons = await this.getVisibleAddons(aTypes ? new Set(aTypes) : null);
    return addons.map(a => a.wrapper);
  },

  /**
   * Returns true if signing is required for the given add-on type.
   *
   * @param {string} aType
   *        The add-on type to check.
   * @returns {boolean}
   */
  mustSign(aType) {
    if (!SIGNED_TYPES.has(aType)) {
      return false;
    }

    if (aType == "locale") {
      return lazy.AddonSettings.LANGPACKS_REQUIRE_SIGNING;
    }

    return lazy.AddonSettings.REQUIRE_SIGNING;
  },

  /**
   * Determine if this addon should be disabled due to being legacy
   *
   * @param {Addon} addon The addon to check
   *
   * @returns {boolean} Whether the addon should be disabled for being legacy
   */
  isDisabledLegacy(addon) {
    // We still have tests that use a legacy addon type, allow them
    // if we're in automation.  Otherwise, disable if not a webextension.
    if (!Cu.isInAutomation) {
      return !addon.isWebExtension;
    }

    return (
      !addon.isWebExtension &&
      addon.type === "extension" &&
      // Test addons are privileged unless forced otherwise.
      addon.signedState !== lazy.AddonManager.SIGNEDSTATE_PRIVILEGED
    );
  },

  /**
   * Calculates whether an add-on should be appDisabled or not.
   *
   * @param {AddonInternal} aAddon
   *        The add-on to check
   * @returns {boolean}
   *        True if the add-on should not be appDisabled
   */
  isUsableAddon(aAddon) {
    if (this.mustSign(aAddon.type) && !aAddon.isCorrectlySigned) {
      logger.warn(`Add-on ${aAddon.id} is not correctly signed.`);
      if (Services.prefs.getBoolPref(PREF_XPI_SIGNATURES_DEV_ROOT, false)) {
        logger.warn(`Preference ${PREF_XPI_SIGNATURES_DEV_ROOT} is set.`);
      }
      return false;
    }

    // When signatures are required, and the addon has the adminInstallOnly
    // flag set to true, then we want to confirm if there is still an active
    // enterprise policy setting for the same addon id, otherwise we should
    // mark if as appDisabled.
    //
    // NOTE: the adminInstallOnly boolean flag is not being stored in the Addon DB,
    // it is instead computed only when installing the addon and when we are
    // re-verify the signatures once per day.
    if (
      this.mustSign(aAddon.type) &&
      aAddon.adminInstallOnly &&
      !aAddon.wrapper.isInstalledByEnterprisePolicy
    ) {
      logger.warn(
        `Add-on ${aAddon.id} is installable only from policies, but no policy extension settings have been found.`
      );
      return false;
    }

    if (aAddon.blocklistState == nsIBlocklistService.STATE_BLOCKED) {
      logger.warn(`Add-on ${aAddon.id} is blocklisted.`);
      return false;
    }

    // If we can't read it, it's not usable:
    if (aAddon.brokenManifest) {
      return false;
    }

    if (
      lazy.AddonManager.checkUpdateSecurity &&
      !aAddon.providesUpdatesSecurely
    ) {
      logger.warn(
        `Updates for add-on ${aAddon.id} must be provided over HTTPS.`
      );
      return false;
    }

    if (!aAddon.isPlatformCompatible) {
      logger.warn(`Add-on ${aAddon.id} is not compatible with platform.`);
      return false;
    }

    if (aAddon.dependencies.length) {
      let isActive = id => {
        let active = XPIExports.XPIProvider.activeAddons.get(id);
        return active && !active._pendingDisable;
      };

      if (aAddon.dependencies.some(id => !isActive(id))) {
        return false;
      }
    }

    if (this.isDisabledLegacy(aAddon)) {
      logger.warn(`disabling legacy extension ${aAddon.id}`);
      return false;
    }

    if (lazy.AddonManager.checkCompatibility) {
      if (!aAddon.isCompatible) {
        logger.warn(
          `Add-on ${aAddon.id} is not compatible with application version.`
        );
        return false;
      }
    } else {
      let app = aAddon.matchingTargetApplication;
      if (!app) {
        logger.warn(
          `Add-on ${aAddon.id} is not compatible with target application.`
        );
        return false;
      }
    }

    if (aAddon.location.isSystem || aAddon.location.isBuiltin) {
      return true;
    }

    if (Services.policies && !Services.policies.mayInstallAddon(aAddon)) {
      return false;
    }

    return true;
  },

  /**
   * Synchronously adds an AddonInternal's metadata to the database.
   *
   * @param {AddonInternal} aAddon
   *        AddonInternal to add
   * @param {string} aPath
   *        The file path of the add-on
   * @returns {AddonInternal}
   *        the AddonInternal that was added to the database
   */
  addToDatabase(aAddon, aPath) {
    aAddon.addedToDatabase();
    aAddon.path = aPath;
    this.addonDB.set(aAddon._key, aAddon);
    if (aAddon.visible) {
      this.makeAddonVisible(aAddon);
    }

    this.saveChanges();
    return aAddon;
  },

  /**
   * Synchronously updates an add-on's metadata in the database. Currently just
   * removes and recreates.
   *
   * @param {AddonInternal} aOldAddon
   *        The AddonInternal to be replaced
   * @param {AddonInternal} aNewAddon
   *        The new AddonInternal to add
   * @param {string} aPath
   *        The file path of the add-on
   * @returns {AddonInternal}
   *        The AddonInternal that was added to the database
   */
  updateAddonMetadata(aOldAddon, aNewAddon, aPath) {
    this.removeAddonMetadata(aOldAddon);
    aNewAddon.syncGUID = aOldAddon.syncGUID;
    aNewAddon.installDate = aOldAddon.installDate;
    aNewAddon.applyBackgroundUpdates = aOldAddon.applyBackgroundUpdates;
    aNewAddon.foreignInstall = aOldAddon.foreignInstall;
    aNewAddon.seen = aOldAddon.seen;
    aNewAddon.active =
      aNewAddon.visible && !aNewAddon.disabled && !aNewAddon.pendingUninstall;
    aNewAddon.installTelemetryInfo = aOldAddon.installTelemetryInfo;

    return this.addToDatabase(aNewAddon, aPath);
  },

  /**
   * Synchronously removes an add-on from the database.
   *
   * @param {AddonInternal} aAddon
   *        The AddonInternal being removed
   */
  removeAddonMetadata(aAddon) {
    this.addonDB.delete(aAddon._key);
    this.saveChanges();
    this.removeFromBlocklistAttentionAddonIdsSet(aAddon);
  },

  updateXPIStates(addon) {
    let state = addon.location && addon.location.get(addon.id);
    if (state) {
      state.syncWithDB(addon);
      XPIExports.XPIInternal.XPIStates.save();
    }
  },

  /**
   * Synchronously marks a AddonInternal as visible marking all other
   * instances with the same ID as not visible.
   *
   * @param {AddonInternal} aAddon
   *        The AddonInternal to make visible
   */
  makeAddonVisible(aAddon) {
    logger.debug("Make addon " + aAddon._key + " visible");
    for (let [, otherAddon] of this.addonDB) {
      if (otherAddon.id == aAddon.id && otherAddon._key != aAddon._key) {
        logger.debug("Hide addon " + otherAddon._key);
        otherAddon.visible = false;
        otherAddon.active = false;

        this.updateXPIStates(otherAddon);
      }
    }
    aAddon.visible = true;
    this.updateXPIStates(aAddon);
    this.saveChanges();
  },

  /**
   * Synchronously marks a given add-on ID visible in a given location,
   * instances with the same ID as not visible.
   *
   * @param {string} aId
   *        The ID of the add-on to make visible
   * @param {XPIStateLocation} aLocation
   *        The location in which to make the add-on visible.
   * @returns {AddonInternal?}
   *        The add-on instance which was marked visible, if any.
   */
  makeAddonLocationVisible(aId, aLocation) {
    logger.debug(`Make addon ${aId} visible in location ${aLocation}`);
    let result;
    for (let [, addon] of this.addonDB) {
      if (addon.id != aId) {
        continue;
      }
      if (addon.location == aLocation) {
        logger.debug("Reveal addon " + addon._key);
        addon.visible = true;
        addon.active = true;
        this.updateXPIStates(addon);
        result = addon;
      } else {
        logger.debug("Hide addon " + addon._key);
        addon.visible = false;
        addon.active = false;
        this.updateXPIStates(addon);
      }
    }
    this.saveChanges();
    return result;
  },

  /**
   * Synchronously sets properties for an add-on.
   *
   * @param {AddonInternal} aAddon
   *        The AddonInternal being updated
   * @param {Object} aProperties
   *        A dictionary of properties to set
   */
  setAddonProperties(aAddon, aProperties) {
    for (let key in aProperties) {
      aAddon[key] = aProperties[key];
    }
    this.saveChanges();
  },

  /**
   * Synchronously sets the Sync GUID for an add-on.
   * Only called when the database is already loaded.
   *
   * @param {AddonInternal} aAddon
   *        The AddonInternal being updated
   * @param {string} aGUID
   *        GUID string to set the value to
   * @throws if another addon already has the specified GUID
   */
  setAddonSyncGUID(aAddon, aGUID) {
    // Need to make sure no other addon has this GUID
    function excludeSyncGUID(otherAddon) {
      return otherAddon._key != aAddon._key && otherAddon.syncGUID == aGUID;
    }
    let otherAddon = _findAddon(this.addonDB, excludeSyncGUID);
    if (otherAddon) {
      throw new Error(
        "Addon sync GUID conflict for addon " +
          aAddon._key +
          ": " +
          otherAddon._key +
          " already has GUID " +
          aGUID
      );
    }
    aAddon.syncGUID = aGUID;
    this.saveChanges();
  },

  /**
   * Synchronously updates an add-on's active flag in the database.
   *
   * @param {AddonInternal} aAddon
   *        The AddonInternal to update
   * @param {boolean} aActive
   *        The new active state for the add-on.
   */
  updateAddonActive(aAddon, aActive) {
    logger.debug(
      "Updating active state for add-on " + aAddon.id + " to " + aActive
    );

    aAddon.active = aActive;
    this.saveChanges();
  },

  /**
   * Synchronously calculates and updates all the active flags in the database.
   */
  updateActiveAddons() {
    logger.debug("Updating add-on states");
    for (let [, addon] of this.addonDB) {
      let newActive =
        addon.visible && !addon.disabled && !addon.pendingUninstall;
      if (newActive != addon.active) {
        addon.active = newActive;
        this.saveChanges();
      }
    }

    Services.prefs.setBoolPref(PREF_PENDING_OPERATIONS, false);
  },

  /**
   * Updates the disabled state for an add-on. Its appDisabled property will be
   * calculated and if the add-on is changed the database will be saved and
   * appropriate notifications will be sent out to the registered AddonListeners.
   *
   * @param {AddonInternal} aAddon
   *        The AddonInternal to update
   * @param {Object} properties - Properties to set on the addon
   * @param {boolean?} [properties.userDisabled]
   *        Value for the userDisabled property. If undefined the value will
   *        not change
   * @param {boolean?} [properties.softDisabled]
   *        Value for the softDisabled property. If undefined the value will
   *        not change. If true this will force userDisabled to be true
   * @param {boolean?} [properties.embedderDisabled]
   *        Value for the embedderDisabled property. If undefined the value will
   *        not change.
   * @param {boolean?} [properties.becauseSelecting]
   *        True if we're disabling this add-on because we're selecting
   *        another.
   * @returns {Promise<boolean?>}
   *       A tri-state indicating the action taken for the add-on:
   *           - undefined: The add-on did not change state
   *           - true: The add-on became disabled
   *           - false: The add-on became enabled
   * @throws if addon is not a AddonInternal
   */
  async updateAddonDisabledState(
    aAddon,
    { userDisabled, softDisabled, embedderDisabled, becauseSelecting } = {}
  ) {
    if (!aAddon.inDatabase) {
      throw new Error("Can only update addon states for installed addons.");
    }
    if (userDisabled !== undefined && softDisabled !== undefined) {
      throw new Error(
        "Cannot change userDisabled and softDisabled at the same time"
      );
    }

    if (userDisabled === undefined) {
      userDisabled = aAddon.userDisabled;
    } else if (!userDisabled) {
      // If enabling the add-on then remove softDisabled
      softDisabled = false;
    }

    // If not changing softDisabled or the add-on is already userDisabled then
    // use the existing value for softDisabled
    if (softDisabled === undefined || userDisabled) {
      softDisabled = aAddon.softDisabled;
    }

    if (!lazy.AddonSettings.IS_EMBEDDED) {
      // If embedderDisabled was accidentally set somehow, this will revert it
      // back to false.
      embedderDisabled = false;
    } else if (embedderDisabled === undefined) {
      embedderDisabled = aAddon.embedderDisabled;
    }

    let appDisabled = !this.isUsableAddon(aAddon);
    // No change means nothing to do here
    if (
      aAddon.userDisabled == userDisabled &&
      aAddon.appDisabled == appDisabled &&
      aAddon.softDisabled == softDisabled &&
      aAddon.embedderDisabled == embedderDisabled
    ) {
      return undefined;
    }

    let wasDisabled = aAddon.disabled;
    let isDisabled =
      userDisabled || softDisabled || appDisabled || embedderDisabled;

    // If appDisabled changes but addon.disabled doesn't,
    // no onDisabling/onEnabling is sent - so send a onPropertyChanged.
    let appDisabledChanged = aAddon.appDisabled != appDisabled;

    // Update the properties in the database.
    this.setAddonProperties(aAddon, {
      userDisabled,
      appDisabled,
      softDisabled,
      embedderDisabled,
    });

    let wrapper = aAddon.wrapper;

    if (appDisabledChanged) {
      lazy.AddonManagerPrivate.callAddonListeners(
        "onPropertyChanged",
        wrapper,
        ["appDisabled"]
      );
    }

    // If the add-on is not visible or the add-on is not changing state then
    // there is no need to do anything else
    if (!aAddon.visible || wasDisabled == isDisabled) {
      return undefined;
    }

    // Flag that active states in the database need to be updated on shutdown
    Services.prefs.setBoolPref(PREF_PENDING_OPERATIONS, true);

    this.updateXPIStates(aAddon);

    // Have we just gone back to the current state?
    if (isDisabled != aAddon.active) {
      lazy.AddonManagerPrivate.callAddonListeners(
        "onOperationCancelled",
        wrapper
      );
    } else {
      if (isDisabled) {
        lazy.AddonManagerPrivate.callAddonListeners(
          "onDisabling",
          wrapper,
          false
        );
      } else {
        lazy.AddonManagerPrivate.callAddonListeners(
          "onEnabling",
          wrapper,
          false
        );
      }

      this.updateAddonActive(aAddon, !isDisabled);
      this.maybeUpdateBlocklistAttentionAddonIdsSet(aAddon);

      let bootstrap = XPIExports.XPIInternal.BootstrapScope.get(aAddon);
      if (isDisabled) {
        await bootstrap.disable();
        lazy.AddonManagerPrivate.callAddonListeners("onDisabled", wrapper);
      } else {
        await bootstrap.startup(
          XPIExports.XPIInternal.BOOTSTRAP_REASONS.ADDON_ENABLE
        );
        lazy.AddonManagerPrivate.callAddonListeners("onEnabled", wrapper);
      }
    }

    // Notify any other providers that a new theme has been enabled
    if (aAddon.type === "theme") {
      if (!isDisabled) {
        await lazy.AddonManagerPrivate.notifyAddonChanged(
          aAddon.id,
          aAddon.type
        );
      } else if (isDisabled && !becauseSelecting) {
        await lazy.AddonManagerPrivate.notifyAddonChanged(null, "theme");
      }
    }

    return isDisabled;
  },

  /**
   * Update the appDisabled property for all add-ons.
   */
  updateAddonAppDisabledStates() {
    for (let addon of this.getAddons()) {
      this.updateAddonDisabledState(addon);
    }
  },

  /**
   * Update the repositoryAddon property for all add-ons.
   */
  async updateAddonRepositoryData() {
    let addons = await this.getVisibleAddons(null);
    logger.debug(
      "updateAddonRepositoryData found " + addons.length + " visible add-ons"
    );

    await Promise.all(
      addons.map(addon =>
        lazy.AddonRepository.getCachedAddonByID(addon.id).then(aRepoAddon => {
          if (aRepoAddon) {
            logger.debug("updateAddonRepositoryData got info for " + addon.id);
            addon._repositoryAddon = aRepoAddon;
            return this.updateAddonDisabledState(addon);
          }
          return undefined;
        })
      )
    );
  },

  /**
   * Adds the add-on's name and creator to the telemetry payload.
   *
   * @param {AddonInternal} aAddon
   *        The addon to record
   */
  recordAddonTelemetry(aAddon) {
    let locale = aAddon.defaultLocale;
    XPIExports.XPIProvider.addTelemetry(aAddon.id, {
      name: locale.name,
      creator: locale.creator,
    });
  },
};

export const XPIDatabaseReconcile = {
  /**
   * Returns a map of ID -> add-on. When the same add-on ID exists in multiple
   * install locations the highest priority location is chosen.
   *
   * @param {Map<String, AddonInternal>} addonMap
   *        The add-on map to flatten.
   * @param {string?} [hideLocation]
   *        An optional location from which to hide any add-ons.
   * @returns {Map<string, AddonInternal>}
   */
  flattenByID(addonMap, hideLocation) {
    let map = new Map();

    for (let loc of XPIExports.XPIInternal.XPIStates.locations()) {
      if (loc.name == hideLocation) {
        continue;
      }

      let locationMap = addonMap.get(loc.name);
      if (!locationMap) {
        continue;
      }

      for (let [id, addon] of locationMap) {
        if (!map.has(id)) {
          map.set(id, addon);
        }
      }
    }

    return map;
  },

  /**
   * Finds the visible add-ons from the map.
   *
   * @param {Map<String, AddonInternal>} addonMap
   *        The add-on map to filter.
   * @returns {Map<string, AddonInternal>}
   */
  getVisibleAddons(addonMap) {
    let map = new Map();

    for (let addons of addonMap.values()) {
      for (let [id, addon] of addons) {
        if (!addon.visible) {
          continue;
        }

        if (map.has(id)) {
          logger.warn(
            "Previous database listed more than one visible add-on with id " +
              id
          );
          continue;
        }

        map.set(id, addon);
      }
    }

    return map;
  },

  /**
   * Called to add the metadata for an add-on in one of the install locations
   * to the database. This can be called in three different cases. Either an
   * add-on has been dropped into the location from outside of Firefox, or
   * an add-on has been installed through the application, or the database
   * has been upgraded or become corrupt and add-on data has to be reloaded
   * into it.
   *
   * @param {XPIStateLocation} aLocation
   *        The install location containing the add-on
   * @param {string} aId
   *        The ID of the add-on
   * @param {XPIState} aAddonState
   *        The new state of the add-on
   * @param {AddonInternal?} [aNewAddon]
   *        The manifest for the new add-on if it has already been loaded
   * @returns {boolean}
   *        A boolean indicating if flushing caches is required to complete
   *        changing this add-on
   */
  addMetadata(aLocation, aId, aAddonState, aNewAddon) {
    logger.debug(`New add-on ${aId} installed in ${aLocation.name}`);

    // We treat this is a new install if,
    //
    // a) It was explicitly registered as a staged install in the last
    //    session, or,
    // b) We're not currently migrating or rebuilding a corrupt database. In
    //    that case, we can assume this add-on was found during a routine
    //    directory scan.
    let isNewInstall = !!aNewAddon || !XPIDatabase.rebuildingDatabase;

    // If it's a new install and we haven't yet loaded the manifest then it
    // must be something dropped directly into the install location
    let isDetectedInstall = isNewInstall && !aNewAddon;

    // Load the manifest if necessary and sanity check the add-on ID
    let unsigned;
    try {
      // Do not allow third party installs if xpinstall is disabled by policy
      if (
        isDetectedInstall &&
        Services.policies &&
        !Services.policies.isAllowed("xpinstall")
      ) {
        throw new Error(
          "Extension installs are disabled by enterprise policy."
        );
      }

      if (!aNewAddon) {
        // Load the manifest from the add-on.
        aNewAddon = XPIExports.XPIInstall.syncLoadManifest(
          aAddonState,
          aLocation
        );
      }
      // The add-on in the manifest should match the add-on ID.
      if (aNewAddon.id != aId) {
        throw new Error(
          `Invalid addon ID: expected addon ID ${aId}, found ${aNewAddon.id} in manifest`
        );
      }

      unsigned =
        XPIDatabase.mustSign(aNewAddon.type) && !aNewAddon.isCorrectlySigned;
      if (unsigned) {
        throw Error(`Extension ${aNewAddon.id} is not correctly signed`);
      }
    } catch (e) {
      logger.warn(`addMetadata: Add-on ${aId} is invalid`, e);

      // Remove the invalid add-on from the install location if the install
      // location isn't locked
      if (
        aLocation.name === KEY_APP_BUILTINS ||
        aLocation.name === KEY_APP_SYSTEM_BUILTINS
      ) {
        // If a builtin has been removed from the build, we need to remove it from our
        // data sets.  We cannot use location.isBuiltin since the system addon locations
        // mix it up.
        // NOTE: for the add-ons installed in KEY_APP_SYSTEM_BUILTINS, this logic ensures
        // that we don't keep them as userDisabled in the add-on DB  when loading the
        // manifest fails. Otherwise, they would stay userDisabled even when the application
        // is updated and an updated manifest loads successfully for the new system built-in
        // add-on version (test_system_reset.js covers this corner case).
        XPIDatabase.removeAddonMetadata(aAddonState);
        aLocation.removeAddon(aId);
      } else if (aLocation.isLinkedAddon(aId)) {
        logger.warn("Not uninstalling invalid item because it is a proxy file");
      } else if (aLocation.locked) {
        logger.warn(
          "Could not uninstall invalid item from locked install location"
        );
      } else if (unsigned && !isNewInstall) {
        logger.warn("Not uninstalling existing unsigned add-on");
      } else {
        aLocation.installer.uninstallAddon(aId);
      }
      return null;
    }

    // Update the AddonInternal properties.
    aNewAddon.installDate = aAddonState.mtime;
    aNewAddon.updateDate = aAddonState.mtime;

    // Assume that add-ons in the system add-ons install location aren't
    // foreign and should default to enabled.
    aNewAddon.foreignInstall =
      isDetectedInstall && !aLocation.isSystem && !aLocation.isBuiltin;

    // appDisabled depends on whether the add-on is a foreignInstall so update
    aNewAddon.appDisabled = !XPIDatabase.isUsableAddon(aNewAddon);

    if (isDetectedInstall && aNewAddon.foreignInstall) {
      // Add the installation source info for the sideloaded extension.
      aNewAddon.installTelemetryInfo = {
        source: aLocation.name,
        method: "sideload",
      };

      // If the add-on is a foreign install and is in a scope where add-ons
      // that were dropped in should default to disabled then disable it
      let disablingScopes = Services.prefs.getIntPref(
        PREF_EM_AUTO_DISABLED_SCOPES,
        0
      );
      if (aLocation.scope & disablingScopes) {
        logger.warn(
          `Disabling foreign installed add-on ${aNewAddon.id} in ${aLocation.name}`
        );
        aNewAddon.userDisabled = true;
        aNewAddon.seen = false;
      }
    }

    return XPIDatabase.addToDatabase(aNewAddon, aAddonState.path);
  },

  /**
   * Called when an add-on has been removed.
   *
   * @param {AddonInternal} aOldAddon
   *        The AddonInternal as it appeared the last time the application
   *        ran
   */
  removeMetadata(aOldAddon) {
    // This add-on has disappeared
    logger.debug(
      "Add-on " + aOldAddon.id + " removed from " + aOldAddon.location.name
    );
    XPIDatabase.removeAddonMetadata(aOldAddon);
  },

  /**
   * Updates an add-on's metadata and determines. This is called when either the
   * add-on's install directory path or last modified time has changed.
   *
   * @param {XPIStateLocation} aLocation
   *        The install location containing the add-on
   * @param {AddonInternal} aOldAddon
   *        The AddonInternal as it appeared the last time the application
   *        ran
   * @param {XPIState} aAddonState
   *        The new state of the add-on
   * @param {AddonInternal?} [aNewAddon]
   *        The manifest for the new add-on if it has already been loaded
   * @returns {AddonInternal}
   *        The AddonInternal that was added to the database
   */
  updateMetadata(aLocation, aOldAddon, aAddonState, aNewAddon) {
    logger.debug(`Add-on ${aOldAddon.id} modified in ${aLocation.name}`);

    try {
      // If there isn't an updated install manifest for this add-on then load it.
      if (!aNewAddon) {
        aNewAddon = XPIExports.XPIInstall.syncLoadManifest(
          aAddonState,
          aLocation,
          aOldAddon
        );
      } else {
        aNewAddon.rootURI = aOldAddon.rootURI;
      }

      // The ID in the manifest that was loaded must match the ID of the old
      // add-on.
      if (aNewAddon.id != aOldAddon.id) {
        throw new Error(
          `Incorrect id in install manifest for existing add-on ${aOldAddon.id}`
        );
      }
    } catch (e) {
      logger.warn(`updateMetadata: Add-on ${aOldAddon.id} is invalid`, e);

      XPIDatabase.removeAddonMetadata(aOldAddon);
      aOldAddon.location.removeAddon(aOldAddon.id);

      if (!aLocation.locked) {
        aLocation.installer.uninstallAddon(aOldAddon.id);
      } else {
        logger.warn(
          "Could not uninstall invalid item from locked install location"
        );
      }

      return null;
    }

    // Set the additional properties on the new AddonInternal
    aNewAddon.updateDate = aAddonState.mtime;

    XPIExports.XPIProvider.persistStartupData(aNewAddon, aAddonState);

    // Update the database
    return XPIDatabase.updateAddonMetadata(
      aOldAddon,
      aNewAddon,
      aAddonState.path
    );
  },

  /**
   * Updates an add-on's path for when the add-on has moved in the
   * filesystem but hasn't changed in any other way.
   *
   * @param {XPIStateLocation} aLocation
   *        The install location containing the add-on
   * @param {AddonInternal} aOldAddon
   *        The AddonInternal as it appeared the last time the application
   *        ran
   * @param {XPIState} aAddonState
   *        The new state of the add-on
   * @returns {AddonInternal}
   */
  updatePath(aLocation, aOldAddon, aAddonState) {
    logger.debug(`Add-on ${aOldAddon.id} moved to ${aAddonState.path}`);
    aOldAddon.path = aAddonState.path;
    aOldAddon._sourceBundle = new nsIFile(aAddonState.path);
    aOldAddon.rootURI = XPIExports.XPIInternal.getURIForResourceInFile(
      aOldAddon._sourceBundle,
      ""
    ).spec;

    return aOldAddon;
  },

  /**
   * Called when no change has been detected for an add-on's metadata but the
   * application has changed so compatibility may have changed.
   *
   * @param {XPIStateLocation} aLocation
   *        The install location containing the add-on
   * @param {AddonInternal} aOldAddon
   *        The AddonInternal as it appeared the last time the application
   *        ran
   * @param {XPIState} aAddonState
   *        The new state of the add-on
   * @param {boolean} [aReloadMetadata = false]
   *        A boolean which indicates whether metadata should be reloaded from
   *        the addon manifests. Default to false.
   * @returns {AddonInternal}
   *        The new addon.
   */
  updateCompatibility(aLocation, aOldAddon, aAddonState, aReloadMetadata) {
    logger.debug(
      `Updating compatibility for add-on ${aOldAddon.id} in ${aLocation.name}`
    );

    let checkSigning =
      aOldAddon.signedState === undefined && SIGNED_TYPES.has(aOldAddon.type);
    // signedDate must be set if signedState is set.
    let signedDateMissing =
      aOldAddon.signedDate === undefined &&
      (aOldAddon.signedState || checkSigning);
    // signedTypes must be set if signedState is set.
    let signedTypesMissing =
      aOldAddon.signedTypes === undefined &&
      (aOldAddon.signedState || checkSigning);

    // If maxVersion was inadvertently updated for a locale, force a reload
    // from the manifest.  See Bug 1646016 for details.
    if (
      !aReloadMetadata &&
      aOldAddon.type === "locale" &&
      aOldAddon.matchingTargetApplication
    ) {
      aReloadMetadata = aOldAddon.matchingTargetApplication.maxVersion === "*";
    }

    let manifest = null;
    if (
      checkSigning ||
      aReloadMetadata ||
      signedDateMissing ||
      signedTypesMissing
    ) {
      try {
        manifest = XPIExports.XPIInstall.syncLoadManifest(
          aAddonState,
          aLocation
        );
      } catch (err) {
        // If we can no longer read the manifest, it is no longer compatible.
        aOldAddon.brokenManifest = true;
        aOldAddon.appDisabled = true;
        return aOldAddon;
      }
    }

    // If updating from a version of the app that didn't support signedState
    // then update that property now
    if (checkSigning) {
      aOldAddon.signedState = manifest.signedState;
    }

    if (signedDateMissing) {
      aOldAddon.signedDate = manifest.signedDate;
    }

    if (signedTypesMissing) {
      aOldAddon.signedTypes = manifest.signedTypes;
    }

    // May be updating from a version of the app that didn't support all the
    // properties of the currently-installed add-ons.
    if (aReloadMetadata) {
      // Avoid re-reading these properties from manifest,
      // use existing addon instead.
      let remove = [
        "syncGUID",
        "foreignInstall",
        "visible",
        "active",
        "userDisabled",
        "embedderDisabled",
        "applyBackgroundUpdates",
        "sourceURI",
        "releaseNotesURI",
        "installTelemetryInfo",
      ];

      // TODO - consider re-scanning for targetApplications for other addon types.
      if (aOldAddon.type !== "locale") {
        remove.push("targetApplications");
      }

      let props = PROP_JSON_FIELDS.filter(a => !remove.includes(a));
      copyProperties(manifest, props, aOldAddon);
    }

    aOldAddon.appDisabled = !XPIDatabase.isUsableAddon(aOldAddon);

    return aOldAddon;
  },

  /**
   * Returns true if this install location is part of the application
   * bundle. Add-ons in these locations are expected to change whenever
   * the application updates.
   *
   * @param {XPIStateLocation} location
   *        The install location to check.
   * @returns {boolean}
   *        True if this location is part of the application bundle.
   */
  isAppBundledLocation(location) {
    return (
      location.name == KEY_APP_GLOBAL ||
      location.name == KEY_APP_BUILTINS ||
      location.name == KEY_APP_SYSTEM_BUILTINS
    );
  },

  /**
   * Returns true if this install location holds system addons.
   *
   * @param {XPIStateLocation} location
   *        The install location to check.
   * @returns {boolean}
   *        True if this location contains system add-ons.
   */
  isSystemAddonLocation(location) {
    return (
      location.name === KEY_APP_SYSTEM_ADDONS ||
      location.name === KEY_APP_SYSTEM_BUILTINS
    );
  },

  /**
   * Updates the databse metadata for an existing add-on during database
   * reconciliation.
   *
   * @param {AddonInternal} oldAddon
   *        The existing database add-on entry.
   * @param {XPIState} xpiState
   *        The XPIStates entry for this add-on.
   * @param {AddonInternal?} newAddon
   *        The new add-on metadata for the add-on, as loaded from a
   *        staged update in addonStartup.json.
   * @param {boolean} aUpdateCompatibility
   *        true to update add-ons appDisabled property when the application
   *        version has changed
   * @param {boolean} aSchemaChange
   *        The schema has changed and all add-on manifests should be re-read.
   * @returns {AddonInternal?}
   *        The updated AddonInternal object for the add-on, if one
   *        could be created.
   */
  updateExistingAddon(
    oldAddon,
    xpiState,
    newAddon,
    aUpdateCompatibility,
    aSchemaChange
  ) {
    XPIDatabase.recordAddonTelemetry(oldAddon);

    let installLocation = oldAddon.location;

    // Update the add-on's database metadata from on-disk metadata if:
    //
    //  a) The add-on was staged for install in the last session,
    //  b) The add-on has been modified since the last session, or,
    //  c) The app has been updated since the last session, and the
    //     add-on is part of the application bundle (and has therefore
    //     likely been replaced in the update process).
    if (
      newAddon ||
      oldAddon.updateDate != xpiState.mtime ||
      (aUpdateCompatibility && this.isAppBundledLocation(installLocation)) ||
      // update addon metadata if the addon in bundled into
      // the omni jar and version or the resource URI pointing
      // to the extension assets has changed.
      (installLocation.name === KEY_APP_SYSTEM_BUILTINS &&
        (oldAddon.version != xpiState.version ||
          oldAddon.rootURI != xpiState.rootURI))
    ) {
      newAddon = this.updateMetadata(
        installLocation,
        oldAddon,
        xpiState,
        newAddon
      );
    } else if (oldAddon.path != xpiState.path) {
      newAddon = this.updatePath(installLocation, oldAddon, xpiState);
    } else if (aUpdateCompatibility || aSchemaChange) {
      newAddon = this.updateCompatibility(
        installLocation,
        oldAddon,
        xpiState,
        aSchemaChange
      );
    } else {
      newAddon = oldAddon;
    }

    if (newAddon) {
      newAddon.rootURI = newAddon.rootURI || xpiState.rootURI;
    }

    return newAddon;
  },

  /**
   * Compares the add-ons that are currently installed to those that were
   * known to be installed when the application last ran and applies any
   * changes found to the database.
   * Always called after XPIDatabase.sys.mjs and extensions.json have been
   * loaded.
   *
   * @param {Object} aManifests
   *        A dictionary of cached AddonInstalls for add-ons that have been
   *        installed
   * @param {boolean} aUpdateCompatibility
   *        true to update add-ons appDisabled property when the application
   *        version has changed
   * @param {string?} [aOldAppVersion]
   *        The version of the application last run with this profile or null
   *        if it is a new profile or the version is unknown
   * @param {string?} [aOldPlatformVersion]
   *        The version of the platform last run with this profile or null
   *        if it is a new profile or the version is unknown
   * @param {boolean} aSchemaChange
   *        The schema has changed and all add-on manifests should be re-read.
   * @returns {boolean}
   *        A boolean indicating if a change requiring flushing the caches was
   *        detected
   */
  processFileChanges(
    aManifests,
    aUpdateCompatibility,
    aOldAppVersion,
    aOldPlatformVersion,
    aSchemaChange
  ) {
    let findManifest = (loc, id) => {
      return (aManifests[loc.name] && aManifests[loc.name][id]) || null;
    };

    let previousAddons = new lazy.ExtensionUtils.DefaultMap(() => new Map());
    let currentAddons = new lazy.ExtensionUtils.DefaultMap(() => new Map());

    // Get the previous add-ons from the database and put them into maps by location
    for (let addon of XPIDatabase.getAddons()) {
      previousAddons.get(addon.location.name).set(addon.id, addon);
    }

    // Keep track of add-ons whose blocklist status may have changed. We'll check this
    // after everything else.
    let addonsToCheckAgainstBlocklist = [];

    // Build the list of current add-ons into similar maps. When add-ons are still
    // present we re-use the add-on objects from the database and update their
    // details directly
    let addonStates = new Map();
    for (let location of XPIExports.XPIInternal.XPIStates.locations()) {
      let locationAddons = currentAddons.get(location.name);

      // Get all the on-disk XPI states for this location, and keep track of which
      // ones we see in the database.
      let dbAddons = previousAddons.get(location.name) || new Map();
      for (let [id, oldAddon] of dbAddons) {
        // Check if the add-on is still installed
        let xpiState = location.get(id);
        if (xpiState && !xpiState.missing) {
          let newAddon = this.updateExistingAddon(
            oldAddon,
            xpiState,
            findManifest(location, id),
            aUpdateCompatibility,
            aSchemaChange
          );
          if (newAddon) {
            locationAddons.set(newAddon.id, newAddon);

            // We need to do a blocklist check later, but the add-on may have changed by then.
            // Avoid storing the current copy and just get one when we need one instead.
            addonsToCheckAgainstBlocklist.push(newAddon.id);
          }
        } else {
          // The add-on is in the DB, but not in xpiState (and thus not on disk).
          this.removeMetadata(oldAddon);
        }
      }

      for (let [id, xpiState] of location) {
        if (locationAddons.has(id) || xpiState.missing) {
          continue;
        }
        let newAddon = findManifest(location, id);
        let addon = this.addMetadata(
          location,
          id,
          xpiState,
          newAddon,
          aOldAppVersion,
          aOldPlatformVersion
        );
        if (addon) {
          locationAddons.set(addon.id, addon);
          addonStates.set(addon, xpiState);
        }
      }

      if (this.isSystemAddonLocation(location)) {
        for (let [id, addon] of locationAddons.entries()) {
          const pref = `extensions.${id.split("@")[0]}.enabled`;
          addon.userDisabled = !Services.prefs.getBoolPref(pref, true);
        }
      }
    }

    // Validate the updated system add-ons
    let hideLocation;
    {
      let systemAddonLocation = XPIExports.XPIInternal.XPIStates.getLocation(
        KEY_APP_SYSTEM_ADDONS
      );
      let addons = currentAddons.get(systemAddonLocation.name);

      if (!systemAddonLocation.installer.isValid(addons)) {
        // Hide the system add-on updates if any are invalid.
        logger.info(
          "One or more updated system add-ons invalid, falling back to defaults."
        );
        hideLocation = systemAddonLocation.name;
      }
    }

    // Apply startup changes to any currently-visible add-ons, and
    // uninstall any which were previously visible, but aren't anymore.
    let previousVisible = this.getVisibleAddons(previousAddons);
    let currentVisible = this.flattenByID(currentAddons, hideLocation);

    for (let addon of XPIDatabase.orphanedAddons.splice(0)) {
      if (addon.visible) {
        previousVisible.set(addon.id, addon);
      }
    }

    let promises = [];
    for (let [id, addon] of currentVisible) {
      // If we have a stored manifest for the add-on, it came from the
      // startup data cache, and supersedes any previous XPIStates entry.
      let xpiState =
        !findManifest(addon.location, id) && addonStates.get(addon);

      promises.push(
        this.applyStartupChange(addon, previousVisible.get(id), xpiState)
      );
      previousVisible.delete(id);
    }

    if (promises.some(p => p)) {
      XPIExports.XPIInternal.awaitPromise(Promise.all(promises));
    }

    for (let [id, addon] of previousVisible) {
      if (addon.location) {
        if (
          addon.location.name === KEY_APP_BUILTINS ||
          addon.location.name === KEY_APP_SYSTEM_BUILTINS
        ) {
          continue;
        }
        XPIExports.XPIInternal.BootstrapScope.get(addon).uninstall();
        addon.location.removeAddon(id);
        addon.visible = false;
        addon.active = false;
      }

      lazy.AddonManagerPrivate.addStartupChange(
        lazy.AddonManager.STARTUP_CHANGE_UNINSTALLED,
        id
      );
    }

    // Finally update XPIStates to match everything
    for (let [locationName, locationAddons] of currentAddons) {
      for (let [id, addon] of locationAddons) {
        let xpiState = XPIExports.XPIInternal.XPIStates.getAddon(
          locationName,
          id
        );
        xpiState.syncWithDB(addon);
      }
    }
    XPIExports.XPIInternal.XPIStates.save();
    XPIDatabase.saveChanges();
    XPIDatabase.rebuildingDatabase = false;

    if (aUpdateCompatibility || aSchemaChange) {
      // Do some blocklist checks. These will happen after we've just saved everything,
      // because they're async and depend on the blocklist loading. When we're done, save
      // the data if any of the add-ons' blocklist state has changed.
      lazy.AddonManager.beforeShutdown.addBlocker(
        "Update add-on blocklist state into add-on DB",
        (async () => {
          // Avoid querying the AddonManager immediately to give startup a chance
          // to complete.
          await Promise.resolve();

          let addons = await lazy.AddonManager.getAddonsByIDs(
            addonsToCheckAgainstBlocklist
          );
          await Promise.all(
            addons.map(async addon => {
              if (!addon) {
                return;
              }
              let oldState = addon.blocklistState;
              // TODO 1712316: updateBlocklistState with object parameter only
              // works if addon is an AddonInternal instance. But addon is an
              // AddonWrapper instead. Consequently updateDate:false is ignored.
              await addon.updateBlocklistState({ updateDatabase: false });
              if (oldState !== addon.blocklistState) {
                lazy.Blocklist.recordAddonBlockChangeTelemetry(
                  addon,
                  "addon_db_modified"
                );
              }
            })
          );

          XPIDatabase.saveChanges();
        })()
      );
    }

    return true;
  },

  /**
   * Applies a startup change for the given add-on.
   *
   * @param {AddonInternal} currentAddon
   *        The add-on as it exists in this session.
   * @param {AddonInternal?} previousAddon
   *        The add-on as it existed in the previous session.
   * @param {XPIState?} xpiState
   *        The XPIState entry for this add-on, if one exists.
   * @returns {Promise?}
   *        If an update was performed, returns a promise which resolves
   *        when the appropriate bootstrap methods have been called.
   */
  applyStartupChange(currentAddon, previousAddon, xpiState) {
    let promise;
    let { id } = currentAddon;

    let isActive = !currentAddon.disabled;
    let wasActive = previousAddon ? previousAddon.active : currentAddon.active;

    if (previousAddon) {
      if (previousAddon !== currentAddon) {
        lazy.AddonManagerPrivate.addStartupChange(
          lazy.AddonManager.STARTUP_CHANGE_CHANGED,
          id
        );

        // Bug 1664144:  If the addon changed on disk we will catch it during
        // the second scan initiated by getNewSideloads.  The addon may have
        // already started, if so we need to ensure it restarts during the
        // update, otherwise we're left in a state where the addon is enabled
        // but not started.  We use the bootstrap started state to check that.
        // isActive alone is not sufficient as that changes the characteristics
        // of other updates and breaks many tests.
        let restart =
          isActive &&
          XPIExports.XPIInternal.BootstrapScope.get(currentAddon).started;
        if (restart) {
          logger.warn(
            `Updating and restart addon ${previousAddon.id} that changed on disk after being already started.`
          );
        }
        promise = XPIExports.XPIInternal.BootstrapScope.get(
          previousAddon
        ).update(currentAddon, restart);
      }

      if (isActive != wasActive) {
        let change = isActive
          ? lazy.AddonManager.STARTUP_CHANGE_ENABLED
          : lazy.AddonManager.STARTUP_CHANGE_DISABLED;
        lazy.AddonManagerPrivate.addStartupChange(change, id);
      }
    } else if (xpiState && xpiState.wasRestored) {
      isActive = xpiState.enabled;

      if (currentAddon.isWebExtension && currentAddon.type == "theme") {
        currentAddon.userDisabled = !isActive;
      }

      // If the add-on wasn't active and it isn't already disabled in some way
      // then it was probably either softDisabled or userDisabled
      if (!isActive && !currentAddon.disabled) {
        // If the add-on is softblocked then assume it is softDisabled
        if (
          currentAddon.blocklistState == Services.blocklist.STATE_SOFTBLOCKED
        ) {
          currentAddon.softDisabled = true;
        } else {
          currentAddon.userDisabled = true;
        }
      }
    } else {
      lazy.AddonManagerPrivate.addStartupChange(
        lazy.AddonManager.STARTUP_CHANGE_INSTALLED,
        id
      );
      let scope = XPIExports.XPIInternal.BootstrapScope.get(currentAddon);
      scope.install();
    }

    XPIDatabase.makeAddonVisible(currentAddon);
    currentAddon.active = isActive;
    return promise;
  },
};
