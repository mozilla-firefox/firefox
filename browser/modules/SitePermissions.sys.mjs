/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

var gStringBundle = Services.strings.createBundle(
  "chrome://browser/locale/sitePermissions.properties"
);

// Observer for browser-scoped permission changes. Dispatches
// PermissionStateChange events on the associated browser element so that
// UI consumers (identity panel, etc.) update.
Services.obs.addObserver(
  {
    observe(subject, _topic, data) {
      // A bulk clear reports the affected tab as a bare browserId rather than
      // as an nsIPermission, since no single permission describes it.
      let browserId =
        data == "cleared"
          ? subject?.QueryInterface(Ci.nsISupportsPRUint64).data
          : subject.QueryInterface(Ci.nsIPermission).browserId;
      if (!browserId) {
        return;
      }
      let bc = BrowsingContext.getCurrentTopByBrowserId(browserId);
      let browser = bc?.embedderElement;
      if (browser?.documentGlobal) {
        browser.dispatchEvent(
          new browser.documentGlobal.CustomEvent("PermissionStateChange")
        );
      }
    },
  },
  "browser-perm-changed"
);

// This hold a flag per browser to indicate whether we should show the
// user a notification as a permission has been requested that has been
// blocked globally. We only want to notify the user in the case that
// they actually requested the permission within the current page load
// so will clear the flag on navigation.
const GloballyBlockedPermissions = {
  _stateByBrowser: new WeakMap(),

  /**
   * @returns {boolean} whether the permission was removed.
   */
  set(browser, id) {
    if (!this._stateByBrowser.has(browser)) {
      this._stateByBrowser.set(browser, {});
    }
    let entry = this._stateByBrowser.get(browser);
    let origin = browser.contentPrincipal.origin;
    if (!entry[origin]) {
      entry[origin] = {};
    }

    if (entry[origin][id]) {
      return false;
    }
    entry[origin][id] = true;

    // Clear the flag and remove the listener once the user has navigated.
    // WebProgress will report various things including hashchanges to us, the
    // navigation we care about is either leaving the current page or reloading.
    let { prePath } = browser.currentURI;
    browser.addProgressListener(
      {
        QueryInterface: ChromeUtils.generateQI([
          "nsIWebProgressListener",
          "nsISupportsWeakReference",
        ]),
        onLocationChange(aWebProgress, aRequest, aLocation, aFlags) {
          let hasLeftPage =
            aLocation.prePath != prePath ||
            !(aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT);
          let isReload = !!(
            aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_RELOAD
          );

          if (aWebProgress.isTopLevel && (hasLeftPage || isReload)) {
            GloballyBlockedPermissions.remove(browser, id, origin);
            browser.removeProgressListener(this);
          }
        },
      },
      Ci.nsIWebProgress.NOTIFY_LOCATION
    );
    return true;
  },

  // Removes a permission with the specified id for the specified browser.
  remove(browser, id, origin = null) {
    let entry = this._stateByBrowser.get(browser);
    if (!origin) {
      origin = browser.contentPrincipal.origin;
    }
    if (entry && entry[origin]) {
      delete entry[origin][id];
    }
  },

  // Gets all permissions for the specified browser.
  // Note that only permissions that apply to the current URI
  // of the passed browser element will be returned.
  getAll(browser) {
    let permissions = [];
    let entry = this._stateByBrowser.get(browser);
    let origin = browser.contentPrincipal.origin;
    if (entry && entry[origin]) {
      let timeStamps = entry[origin];
      for (let id of Object.keys(timeStamps)) {
        permissions.push({
          id,
          state: gPermissions.get(id).getDefault(),
          scope: SitePermissions.SCOPE_GLOBAL,
        });
      }
    }
    return permissions;
  },

  // Copies the globally blocked permission state of one browser
  // into a new entry for the other browser.
  copy(browser, newBrowser) {
    let entry = this._stateByBrowser.get(browser);
    if (entry) {
      this._stateByBrowser.set(newBrowser, entry);
    }
  },
};

/**
 * A module to manage permanent and temporary permissions
 * by URI and browser.
 *
 * Some methods have the side effect of dispatching a "PermissionStateChange"
 * event on changes to temporary permissions, as mentioned in the respective docs.
 */
export var SitePermissions = {
  // Permission states.
  UNKNOWN: Services.perms.UNKNOWN_ACTION,
  ALLOW: Services.perms.ALLOW_ACTION,
  BLOCK: Services.perms.DENY_ACTION,
  PROMPT: Services.perms.PROMPT_ACTION,
  ALLOW_COOKIES_FOR_SESSION: Ci.nsICookiePermission.ACCESS_SESSION,
  AUTOPLAY_BLOCKED_ALL: Ci.nsIAutoplay.BLOCKED_ALL,

  // Permission scopes.
  SCOPE_REQUEST: "{SitePermissions.SCOPE_REQUEST}",
  SCOPE_TEMPORARY: "{SitePermissions.SCOPE_TEMPORARY}",
  SCOPE_SESSION: "{SitePermissions.SCOPE_SESSION}",
  SCOPE_PERSISTENT: "{SitePermissions.SCOPE_PERSISTENT}",
  SCOPE_POLICY: "{SitePermissions.SCOPE_POLICY}",
  SCOPE_GLOBAL: "{SitePermissions.SCOPE_GLOBAL}",

  // The delimiter used for double keyed permissions.
  // For example: open-protocol-handler^irc
  PERM_KEY_DELIMITER: "^",

  _permissionsArray: null,
  _defaultPrefBranch: Services.prefs.getBranch("permissions.default."),

  /**
   * Gets all custom permissions for a given principal.
   * Install addon permission is excluded, check bug 1303108.
   *
   * @return {Array} a list of objects with the keys:
   *          - id: the permissionId of the permission
   *          - scope: the scope of the permission (e.g. SitePermissions.SCOPE_TEMPORARY)
   *          - state: a constant representing the current permission state
   *            (e.g. SitePermissions.ALLOW)
   */
  getAllByPrincipal(principal) {
    if (!principal) {
      throw new Error("principal argument cannot be null.");
    }
    if (!this.isSupportedPrincipal(principal)) {
      return [];
    }

    // Get all permissions from the permission manager by principal, excluding
    // the ones set to be disabled.
    let permissions = Services.perms
      .getAllForPrincipal(principal)
      .filter(permission => {
        let entry = gPermissions.get(permission.type);
        if (!entry || entry.disabled) {
          return false;
        }
        let type = entry.id;

        /* Hide persistent storage permission when extension principal
         * have WebExtensions-unlimitedStorage permission. */
        if (
          type == "persistent-storage" &&
          SitePermissions.getForPrincipal(
            principal,
            "WebExtensions-unlimitedStorage"
          ).state == SitePermissions.ALLOW
        ) {
          return false;
        }

        return true;
      });

    return permissions.map(permission => {
      let scope = this.SCOPE_PERSISTENT;
      if (permission.expireType == Services.perms.EXPIRE_SESSION) {
        scope = this.SCOPE_SESSION;
      } else if (permission.expireType == Services.perms.EXPIRE_POLICY) {
        scope = this.SCOPE_POLICY;
      }

      return {
        id: permission.type,
        scope,
        state: permission.capability,
      };
    });
  },

  /**
   * Returns all custom permissions for a given browser.
   *
   * To receive a more detailed, albeit less performant listing see
   * SitePermissions.getAllPermissionDetailsForBrowser().
   *
   * @param {Browser} browser
   *        The browser to fetch permission for.
   *
   * @return {Array} a list of objects with the keys:
   *         - id: the permissionId of the permission
   *         - state: a constant representing the current permission state
   *           (e.g. SitePermissions.ALLOW)
   *         - scope: a constant representing how long the permission will
   *           be kept.
   */
  getAllForBrowser(browser) {
    let permissions = {};

    let browserId = browser.browserId;
    if (browserId && this.isSupportedPrincipal(browser.contentPrincipal)) {
      let bcPerms = Services.perms.getAllForBrowser(
        browser.contentPrincipal,
        browserId
      );
      for (let perm of bcPerms) {
        permissions[perm.type] = {
          id: perm.type,
          state: perm.capability,
          scope: this.SCOPE_TEMPORARY,
        };
      }
    }

    for (let permission of GloballyBlockedPermissions.getAll(browser)) {
      permissions[permission.id] = permission;
    }

    for (let permission of this.getAllByPrincipal(browser.contentPrincipal)) {
      permissions[permission.id] = permission;
    }

    return Object.values(permissions);
  },

  /**
   * Returns a list of objects with detailed information on all permissions
   * that are currently set for the given browser.
   *
   * @param {Browser} browser
   *        The browser to fetch permission for.
   *
   * @return {Array<object>} a list of objects with the keys:
   *           - id: the permissionID of the permission
   *           - state: a constant representing the current permission state
   *             (e.g. SitePermissions.ALLOW)
   *           - scope: a constant representing how long the permission will
   *             be kept.
   *           - label: the localized label, or null if none is available.
   */
  getAllPermissionDetailsForBrowser(browser) {
    return this.getAllForBrowser(browser).map(({ id, scope, state }) => ({
      id,
      scope,
      state,
      label: this.getPermissionLabel(id),
    }));
  },

  /**
   * Checks whether a UI for managing permissions should be exposed for a given
   * principal.
   *
   * @param {nsIPrincipal} principal
   *        The principal to check.
   *
   * @return {boolean} if the principal is supported.
   */
  isSupportedPrincipal(principal) {
    if (!principal) {
      return false;
    }
    if (!(principal instanceof Ci.nsIPrincipal)) {
      throw new Error(
        "Argument passed as principal is not an instance of Ci.nsIPrincipal"
      );
    }
    return this.isSupportedScheme(principal.scheme);
  },

  /**
   * Checks whether we support managing permissions for a specific scheme.
   *
   * @param {string} scheme - Scheme to test.
   * @returns {boolean} Whether the scheme is supported.
   */
  isSupportedScheme(scheme) {
    return ["http", "https", "moz-extension", "file"].includes(scheme);
  },

  /**
   * Gets an array of all permission IDs.
   *
   * @return {Array<string>} an array of all permission IDs.
   */
  listPermissions() {
    if (this._permissionsArray === null) {
      this._permissionsArray = gPermissions.getEnabledPermissions();
    }
    return this._permissionsArray;
  },

  /**
   * Test whether a permission is managed by SitePermissions.
   *
   * @param {string} type - Permission type.
   * @returns {boolean}
   */
  isSitePermission(type) {
    return gPermissions.has(type);
  },

  /**
   * Called when a preference changes its value.
   *
   * @param {string} data
   *        The last argument passed to the preference change observer
   * @param {string} previous
   *        The previous value of the preference
   * @param {string} latest
   *        The latest value of the preference
   */
  invalidatePermissionList() {
    // Ensure that listPermissions() will reconstruct its return value the next
    // time it's called.
    this._permissionsArray = null;
  },

  /**
   * Returns an array of permission states to be exposed to the user for a
   * permission with the given ID.
   *
   * @param {string} permissionID
   *        The ID to get permission states for.
   *
   * @return {Array<SitePermissions state>} an array of all permission states.
   */
  getAvailableStates(permissionID) {
    if (
      gPermissions.has(permissionID) &&
      gPermissions.get(permissionID).states
    ) {
      return gPermissions.get(permissionID).states;
    }

    /* Since the permissions we are dealing with have adopted the convention
     * of treating UNKNOWN == PROMPT, we only include one of either UNKNOWN
     * or PROMPT in this list, to avoid duplicating states. */
    if (this.getDefault(permissionID) == this.UNKNOWN) {
      return [
        SitePermissions.UNKNOWN,
        SitePermissions.ALLOW,
        SitePermissions.BLOCK,
      ];
    }

    return [
      SitePermissions.PROMPT,
      SitePermissions.ALLOW,
      SitePermissions.BLOCK,
    ];
  },

  /**
   * Returns the default state of a particular permission.
   *
   * @param {string} permissionID
   *        The ID to get the default for.
   *
   * @return {SitePermissions.state} the default state.
   */
  getDefault(permissionID) {
    // If the permission has custom logic for getting its default value,
    // try that first.
    if (
      gPermissions.has(permissionID) &&
      gPermissions.get(permissionID).getDefault
    ) {
      return gPermissions.get(permissionID).getDefault();
    }

    // Otherwise try to get the default preference for that permission.
    return this._defaultPrefBranch.getIntPref(permissionID, this.UNKNOWN);
  },

  /**
   * Set the default state of a particular permission.
   *
   * @param {string} permissionID
   *        The ID to set the default for.
   *
   * @param {string} state
   *        The state to set.
   */
  setDefault(permissionID, state) {
    if (
      gPermissions.has(permissionID) &&
      gPermissions.get(permissionID).setDefault
    ) {
      return gPermissions.get(permissionID).setDefault(state);
    }
    let key = "permissions.default." + permissionID;
    return Services.prefs.setIntPref(key, state);
  },

  /**
   * Returns the state and scope of a particular permission for a given principal.
   *
   * This method will NOT dispatch a "PermissionStateChange" event on the specified
   * browser if a temporary permission was removed because it has expired.
   *
   * @param {nsIPrincipal} principal
   *        The principal to check.
   * @param {string} permissionID
   *        The id of the permission.
   * @param {Browser} [browser] The browser object to check for temporary
   *        permissions.
   *
   * @return {object} an object with the keys:
   *           - state: The current state of the permission
   *             (e.g. SitePermissions.ALLOW)
   *           - scope: The scope of the permission
   *             (e.g. SitePermissions.SCOPE_PERSISTENT)
   */
  getForPrincipal(principal, permissionID, browser) {
    if (!principal && !browser) {
      throw new Error(
        "Atleast one of the arguments, either principal or browser should not be null."
      );
    }
    let defaultState = this.getDefault(permissionID);
    let result = { state: defaultState, scope: this.SCOPE_PERSISTENT };
    if (this.isSupportedPrincipal(principal)) {
      let permission = null;
      if (
        gPermissions.has(permissionID) &&
        gPermissions.get(permissionID).exactHostMatch
      ) {
        permission = Services.perms.getPermissionObject(
          principal,
          permissionID,
          true
        );
      } else {
        permission = Services.perms.getPermissionObject(
          principal,
          permissionID,
          false
        );
      }

      if (permission) {
        result.state = permission.capability;
        if (permission.expireType == Services.perms.EXPIRE_SESSION) {
          result.scope = this.SCOPE_SESSION;
        } else if (permission.expireType == Services.perms.EXPIRE_POLICY) {
          result.scope = this.SCOPE_POLICY;
        }
      }
    }

    if (
      result.state == defaultState ||
      result.state == SitePermissions.PROMPT
    ) {
      // If there's no persistent permission saved, or if the persistent permission
      // saved is merely PROMPT (aka "Always Ask" when persisted for camera and
      // microphone), then check if we have something set temporarily.
      //
      // This way, a temporary ALLOW or BLOCK trumps a persisted PROMPT. While
      // having overlap would be a bug (because any ALLOW or BLOCK user action should
      // really clear PROMPT), this order seems safer than the other way around.
      if (browser) {
        let browserId = browser.browserId;
        if (browserId) {
          let tempPerm = Services.perms.getForBrowser(
            principal ?? browser.contentPrincipal,
            permissionID,
            browserId
          );
          if (tempPerm) {
            result.state = tempPerm.capability;
            result.scope = this.SCOPE_TEMPORARY;
          }
        }
      }
    }

    return result;
  },

  /**
   * Sets the state of a particular permission for a given principal or browser.
   * This method will dispatch a "PermissionStateChange" event on the specified
   * browser if a temporary permission was set
   *
   * @param {nsIPrincipal} [principal] The principal to set the permission for.
   *        When setting temporary permissions passing a principal is optional.
   *        If the principal is still passed here it takes precedence over the
   *        browser's contentPrincipal for permission keying. This can be
   *        helpful in situations where the browser has already navigated away
   *        from a site you want to set a permission for.
   * @param {string} permissionID The id of the permission.
   * @param {SitePermissions state} state The state of the permission.
   * @param {SitePermissions scope} [scope] The scope of the permission.
   *        Defaults to SCOPE_PERSISTENT.
   * @param {Browser} [browser] The browser object to set temporary permissions
   *        on. This needs to be provided if the scope is SCOPE_TEMPORARY!
   * @param {number} [expireTimeMS] If setting a temporary permission, how many
   *        milliseconds it should be valid for. The default is controlled by
   *        the 'privacy.temporary_permission_expire_time_ms' pref.
   */
  setForPrincipal(
    principal,
    permissionID,
    state,
    scope = this.SCOPE_PERSISTENT,
    browser = null,
    expireTimeMS = SitePermissions.temporaryPermissionExpireTime
  ) {
    if (!principal && !browser) {
      throw new Error(
        "Atleast one of the arguments, either principal or browser should not be null."
      );
    }
    if (scope == this.SCOPE_GLOBAL && state == this.BLOCK) {
      if (GloballyBlockedPermissions.set(browser, permissionID)) {
        browser.dispatchEvent(
          new browser.documentGlobal.CustomEvent("PermissionStateChange")
        );
      }
      return;
    }

    if (state == this.UNKNOWN || state == this.getDefault(permissionID)) {
      // Because they are controlled by two prefs with many states that do not
      // correspond to the classical ALLOW/DENY/PROMPT model, we want to always
      // allow the user to add exceptions to their cookie rules without removing them.
      if (permissionID != "cookie") {
        this.removeFromPrincipal(principal, permissionID, browser);
        return;
      }
    }

    if (state == this.ALLOW_COOKIES_FOR_SESSION && permissionID != "cookie") {
      throw new Error(
        "ALLOW_COOKIES_FOR_SESSION can only be set on the cookie permission"
      );
    }

    // Save temporary permissions.
    if (scope == this.SCOPE_TEMPORARY) {
      if (!browser) {
        throw new Error(
          "TEMPORARY scoped permissions require a browser object"
        );
      }
      if (!Number.isInteger(expireTimeMS) || expireTimeMS <= 0) {
        throw new Error("expireTime must be a positive integer");
      }

      let browserId = browser.browserId;
      if (browserId) {
        Services.perms.addFromPrincipalForBrowser(
          principal ?? browser.contentPrincipal,
          permissionID,
          state,
          browserId,
          expireTimeMS
        );
      }
    } else if (this.isSupportedPrincipal(principal)) {
      let perms_scope = Services.perms.EXPIRE_NEVER;
      if (scope == this.SCOPE_SESSION) {
        perms_scope = Services.perms.EXPIRE_SESSION;
      } else if (scope == this.SCOPE_POLICY) {
        perms_scope = Services.perms.EXPIRE_POLICY;
      }

      Services.perms.addFromPrincipal(
        principal,
        permissionID,
        state,
        perms_scope
      );
    }
  },

  /**
   * Removes the saved state of a particular permission for a given principal and/or browser.
   * This method will dispatch a "PermissionStateChange" event on the specified
   * browser if a temporary permission was removed.
   *
   * @param {nsIPrincipal} principal
   *        The principal to remove the permission for.
   * @param {string} permissionID
   *        The id of the permission.
   * @param {Browser} browser (optional)
   *        The browser object to remove temporary permissions on.
   */
  removeFromPrincipal(principal, permissionID, browser) {
    if (!principal && !browser) {
      throw new Error(
        "Atleast one of the arguments, either principal or browser should not be null."
      );
    }
    if (this.isSupportedPrincipal(principal)) {
      Services.perms.removeFromPrincipal(principal, permissionID);
    }

    if (browser) {
      let browserId = browser.browserId;
      if (browserId) {
        Services.perms.removeFromPrincipalForBrowser(
          principal ?? browser.contentPrincipal,
          permissionID,
          browserId
        );
      }
    }
  },

  /**
   * Clears all block permissions that were temporarily saved.
   *
   * @param {Browser} browser
   *        The browser object to clear.
   */
  clearTemporaryBlockPermissions(browser) {
    let browserId = browser.browserId;
    if (browserId) {
      Services.perms.removeByActionForBrowser(
        browserId,
        Services.perms.DENY_ACTION
      );
    }
  },

  /**
   * Copy all permissions that were temporarily saved on one
   * browser object to a new browser.
   *
   * @param {number} srcBrowserId
   *        The source BrowserId (captured before any swap).
   * @param {Browser} srcBrowser
   *        The source browser object (for GloballyBlockedPermissions).
   * @param {Browser} destBrowser
   *        The destination browser object.
   */
  copyTemporaryPermissions(srcBrowserId, srcBrowser, destBrowser) {
    let destBrowserId = destBrowser.browserId;
    if (srcBrowserId && destBrowserId && srcBrowserId !== destBrowserId) {
      Services.perms.copyBrowserPermissions(srcBrowserId, destBrowserId);
    }
    GloballyBlockedPermissions.copy(srcBrowser, destBrowser);
  },

  /**
   * Returns the localized label for the permission with the given ID, to be
   * used in a UI for managing permissions.
   * If a permission is double keyed (has an additional key in the ID), the
   * second key is split off and supplied to the string formatter as a variable.
   *
   * @param {string} permissionID
   *        The permission to get the label for. May include second key.
   *
   * @return {string} the localized label or null if none is available.
   */
  getPermissionLabel(permissionID) {
    let [id, key] = permissionID.split(this.PERM_KEY_DELIMITER);
    if (!gPermissions.has(id)) {
      // Permission can't be found.
      return null;
    }
    if (
      "labelID" in gPermissions.get(id) &&
      gPermissions.get(id).labelID === null
    ) {
      // Permission doesn't support having a label.
      return null;
    }
    if (id == "3rdPartyStorage" || id == "3rdPartyFrameStorage") {
      // The key is the 3rd party origin or site, which we use for the label.
      return key;
    }
    let labelID = gPermissions.get(id).labelID || id;
    return gStringBundle.formatStringFromName(`permission.${labelID}.label`, [
      key,
    ]);
  },

  /**
   * Returns the localized label for the given permission state, to be used in
   * a UI for managing permissions.
   *
   * @param {string} permissionID
   *        The permission to get the label for.
   *
   * @param {SitePermissions state} state
   *        The state to get the label for.
   *
   * @return {string | null} the localized label or null if an
   *         unknown state was passed.
   */
  getMultichoiceStateLabel(permissionID, state) {
    // If the permission has custom logic for getting its default value,
    // try that first.
    if (
      gPermissions.has(permissionID) &&
      gPermissions.get(permissionID).getMultichoiceStateLabel
    ) {
      return gPermissions.get(permissionID).getMultichoiceStateLabel(state);
    }

    switch (state) {
      case this.UNKNOWN:
      case this.PROMPT:
        return gStringBundle.GetStringFromName("state.multichoice.alwaysAsk");
      case this.ALLOW:
        return gStringBundle.GetStringFromName("state.multichoice.allow");
      case this.ALLOW_COOKIES_FOR_SESSION:
        return gStringBundle.GetStringFromName(
          "state.multichoice.allowForSession"
        );
      case this.BLOCK:
        return gStringBundle.GetStringFromName("state.multichoice.block");
      default:
        return null;
    }
  },

  /**
   * Returns the localized label for a permission's current state.
   *
   * @param {SitePermissions state} state
   *        The state to get the label for.
   * @param {string} id
   *        The permission to get the state label for.
   * @param {SitePermissions scope} scope (optional)
   *        The scope to get the label for.
   *
   * @return {string | null} the localized label or null if an
   *         unknown state was passed.
   */
  getCurrentStateLabel(state, id, scope = null) {
    switch (state) {
      case this.PROMPT:
        return gStringBundle.GetStringFromName("state.current.prompt");
      case this.ALLOW:
        if (
          scope &&
          scope != this.SCOPE_PERSISTENT &&
          scope != this.SCOPE_POLICY
        ) {
          return gStringBundle.GetStringFromName(
            "state.current.allowedTemporarily"
          );
        }
        return gStringBundle.GetStringFromName("state.current.allowed");
      case this.ALLOW_COOKIES_FOR_SESSION:
        return gStringBundle.GetStringFromName(
          "state.current.allowedForSession"
        );
      case this.BLOCK:
        if (
          scope &&
          scope != this.SCOPE_PERSISTENT &&
          scope != this.SCOPE_POLICY &&
          scope != this.SCOPE_GLOBAL
        ) {
          return gStringBundle.GetStringFromName(
            "state.current.blockedTemporarily"
          );
        }
        return gStringBundle.GetStringFromName("state.current.blocked");
      default:
        return null;
    }
  },
};

let gPermissions = {
  _getId(type) {
    // Split off second key (if it exists).
    let [id] = type.split(SitePermissions.PERM_KEY_DELIMITER);
    return id;
  },

  has(type) {
    return this._getId(type) in this._permissions;
  },

  get(type) {
    let id = this._getId(type);
    let perm = this._permissions[id];
    if (perm) {
      perm.id = id;
    }
    return perm;
  },

  getEnabledPermissions() {
    return Object.keys(this._permissions).filter(
      id => !this._permissions[id].disabled
    );
  },

  /* Holds permission ID => options pairs.
   *
   * Supported options:
   *
   *  - exactHostMatch
   *    Allows sub domains to have their own permissions.
   *    Defaults to false.
   *
   *  - getDefault
   *    Called to get the permission's default state.
   *    Defaults to UNKNOWN, indicating that the user will be asked each time
   *    a page asks for that permissions.
   *
   *  - labelID
   *    Use the given ID instead of the permission name for looking up strings.
   *    e.g. "desktop-notification2" to use permission.desktop-notification2.label
   *
   *  - states
   *    Array of permission states to be exposed to the user.
   *    Defaults to ALLOW, BLOCK and the default state (see getDefault).
   *
   *  - getMultichoiceStateLabel
   *    Optional method to overwrite SitePermissions#getMultichoiceStateLabel with custom label logic.
   */
  _permissions: {
    "autoplay-media": {
      exactHostMatch: true,
      getDefault() {
        let pref = Services.prefs.getIntPref(
          "media.autoplay.default",
          Ci.nsIAutoplay.BLOCKED
        );
        if (pref == Ci.nsIAutoplay.ALLOWED) {
          return SitePermissions.ALLOW;
        }
        if (pref == Ci.nsIAutoplay.BLOCKED_ALL) {
          return SitePermissions.AUTOPLAY_BLOCKED_ALL;
        }
        return SitePermissions.BLOCK;
      },
      setDefault(value) {
        let prefValue = Ci.nsIAutoplay.BLOCKED;
        if (value == SitePermissions.ALLOW) {
          prefValue = Ci.nsIAutoplay.ALLOWED;
        } else if (value == SitePermissions.AUTOPLAY_BLOCKED_ALL) {
          prefValue = Ci.nsIAutoplay.BLOCKED_ALL;
        }
        Services.prefs.setIntPref("media.autoplay.default", prefValue);
      },
      labelID: "autoplay",
      states: [
        SitePermissions.ALLOW,
        SitePermissions.BLOCK,
        SitePermissions.AUTOPLAY_BLOCKED_ALL,
      ],
      getMultichoiceStateLabel(state) {
        switch (state) {
          case SitePermissions.AUTOPLAY_BLOCKED_ALL:
            return gStringBundle.GetStringFromName(
              "state.multichoice.autoplayblockall"
            );
          case SitePermissions.BLOCK:
            return gStringBundle.GetStringFromName(
              "state.multichoice.autoplayblock"
            );
          case SitePermissions.ALLOW:
            return gStringBundle.GetStringFromName(
              "state.multichoice.autoplayallow"
            );
        }
        throw new Error(`Unknown state: ${state}`);
      },
    },

    cookie: {
      states: [
        SitePermissions.ALLOW,
        SitePermissions.ALLOW_COOKIES_FOR_SESSION,
        SitePermissions.BLOCK,
      ],
      getDefault() {
        if (
          Services.cookies.getCookieBehavior(false) ==
          Ci.nsICookieService.BEHAVIOR_REJECT
        ) {
          return SitePermissions.BLOCK;
        }

        return SitePermissions.ALLOW;
      },
    },

    "desktop-notification": {
      exactHostMatch: true,
      labelID: "desktop-notification3",
    },

    camera: {
      exactHostMatch: true,
    },

    "loopback-network": {
      exactHostMatch: true,
      labelID: "localhost",
      get disabled() {
        return !SitePermissions.localNetworkAccessPermissionsEnabled;
      },
    },

    "local-network": {
      exactHostMatch: true,
      get disabled() {
        return !SitePermissions.localNetworkAccessPermissionsEnabled;
      },
    },

    microphone: {
      exactHostMatch: true,
    },

    screen: {
      exactHostMatch: true,
      states: [SitePermissions.UNKNOWN, SitePermissions.BLOCK],
    },

    speaker: {
      exactHostMatch: true,
      states: [SitePermissions.UNKNOWN, SitePermissions.BLOCK],
      get disabled() {
        return !SitePermissions.setSinkIdEnabled;
      },
    },

    popup: {
      // Contrary to the name, this permission controls exceptions for both the
      // pop-up blocking and the framebusting intervention (also called
      // "third-party redirects" in the UI).
      // This permission is checked in WindowContext::CanShowPopup and
      // WindowContext::CanFramebust.
      get labelID() {
        // Use a different label if one of the two prefs is disabled. The
        // permission will only control the other pref in that case.
        if (
          SitePermissions.popupBlockerEnabled &&
          !SitePermissions.framebustingInterventionEnabled
        ) {
          return "popup-only";
        }
        if (
          !SitePermissions.popupBlockerEnabled &&
          SitePermissions.framebustingInterventionEnabled
        ) {
          return "framebusting-only";
        }
        return "popup-and-framebusting";
      },
      states: [SitePermissions.ALLOW, SitePermissions.BLOCK],
      get disabled() {
        return (
          !SitePermissions.popupBlockerEnabled &&
          !SitePermissions.framebustingInterventionEnabled
        );
      },
      getDefault() {
        return SitePermissions.BLOCK;
      },
    },

    install: {
      getDefault() {
        return Services.prefs.getBoolPref("xpinstall.whitelist.required")
          ? SitePermissions.UNKNOWN
          : SitePermissions.ALLOW;
      },
    },

    geo: {
      exactHostMatch: true,
    },

    "open-protocol-handler": {
      labelID: "open-protocol-handler",
      exactHostMatch: true,
      states: [SitePermissions.UNKNOWN, SitePermissions.ALLOW],
    },

    xr: {
      exactHostMatch: true,
    },

    "focus-tab-by-prompt": {
      exactHostMatch: true,
      states: [SitePermissions.UNKNOWN, SitePermissions.ALLOW],
    },
    "persistent-storage": {
      exactHostMatch: true,
    },

    // Clear-on-shutdown exception. ALLOW means the site's data is kept when
    // the browser closes; absent (the default) means it is cleared. The
    // Sanitizer reads this via testPermissionFromPrincipal (base-domain
    // matching), so exactHostMatch is false to mirror that read path. Only
    // relevant while data is actually cleared on shutdown, so it's hidden
    // (from Page Info and the permission panel) when that's off.
    "persist-data-on-shutdown": {
      exactHostMatch: false,
      get disabled() {
        return !SitePermissions.sanitizeOnShutdownEnabled;
      },
      getDefault() {
        return SitePermissions.UNKNOWN;
      },
      states: [SitePermissions.UNKNOWN, SitePermissions.ALLOW],
      getMultichoiceStateLabel(state) {
        switch (state) {
          case SitePermissions.UNKNOWN:
            return gStringBundle.GetStringFromName(
              "state.multichoice.clearDataOnShutdown"
            );
          case SitePermissions.ALLOW:
            return gStringBundle.GetStringFromName(
              "state.multichoice.persistDataOnShutdown"
            );
        }
        throw new Error(`Unknown state: ${state}`);
      },
    },

    shortcuts: {
      states: [SitePermissions.ALLOW, SitePermissions.BLOCK],
    },

    canvas: {
      get disabled() {
        return !SitePermissions.resistFingerprinting;
      },
    },

    midi: {
      exactHostMatch: true,
      get disabled() {
        return !SitePermissions.midiPermissionEnabled;
      },
    },

    "midi-sysex": {
      exactHostMatch: true,
      get disabled() {
        return !SitePermissions.midiPermissionEnabled;
      },
    },

    serial: {
      exactHostMatch: true,
      get disabled() {
        return !SitePermissions.serialPermissionEnabled;
      },
    },

    "storage-access": {
      labelID: null,
      getDefault() {
        return SitePermissions.UNKNOWN;
      },
    },

    "3rdPartyStorage": {},
    "3rdPartyFrameStorage": {},
  },
};

SitePermissions.midiPermissionEnabled = Services.prefs.getBoolPref(
  "dom.webmidi.enabled"
);
SitePermissions.serialPermissionEnabled = Services.prefs.getBoolPref(
  "dom.webserial.enabled"
);

XPCOMUtils.defineLazyPreferenceGetter(
  SitePermissions,
  "temporaryPermissionExpireTime",
  "privacy.temporary_permission_expire_time_ms",
  3600 * 1000
);
XPCOMUtils.defineLazyPreferenceGetter(
  SitePermissions,
  "setSinkIdEnabled",
  "media.setsinkid.enabled",
  false,
  SitePermissions.invalidatePermissionList.bind(SitePermissions)
);
XPCOMUtils.defineLazyPreferenceGetter(
  SitePermissions,
  "resistFingerprinting",
  "privacy.resistFingerprinting",
  false,
  SitePermissions.invalidatePermissionList.bind(SitePermissions)
);

XPCOMUtils.defineLazyPreferenceGetter(
  SitePermissions,
  "localNetworkAccessPermissionsEnabled",
  "network.lna.blocking",
  false,
  SitePermissions.invalidatePermissionList.bind(SitePermissions)
);

XPCOMUtils.defineLazyPreferenceGetter(
  SitePermissions,
  "sanitizeOnShutdownEnabled",
  "privacy.sanitize.sanitizeOnShutdown",
  false,
  SitePermissions.invalidatePermissionList.bind(SitePermissions)
);
XPCOMUtils.defineLazyPreferenceGetter(
  SitePermissions,
  "popupBlockerEnabled",
  "dom.disable_open_during_load",
  true,
  SitePermissions.invalidatePermissionList.bind(SitePermissions)
);
XPCOMUtils.defineLazyPreferenceGetter(
  SitePermissions,
  "framebustingInterventionEnabled",
  "dom.security.framebusting_intervention.enabled",
  true,
  SitePermissions.invalidatePermissionList.bind(SitePermissions)
);
