/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * PermissionUI is responsible for exposing both a prototype
 * PermissionPrompt that can be used by arbitrary browser
 * components and add-ons, but also hosts the implementations of
 * built-in permission prompts.
 *
 * If you're developing a feature that requires web content to ask
 * for special permissions from the user, this module is for you.
 *
 * Suppose a system add-on wants to add a new prompt for a new request
 * for getting more low-level access to the user's sound card, and the
 * permission request is coming up from content by way of the
 * nsContentPermissionHelper. The system add-on could then do the following:
 *
 * const { Integration } = ChromeUtils.importESModule(
 *   "resource://gre/modules/Integration.sys.mjs"
 * );
 * const { PermissionUI } = ChromeUtils.importESModule(
 *   "resource:///modules/PermissionUI.sys.mjs"
 * );
 *
 * const SoundCardIntegration = base => {
 *   let soundCardObj = {
 *     createPermissionPrompt(type, request) {
 *       if (type != "sound-api") {
 *         return super.createPermissionPrompt(...arguments);
 *       }
 *
 *       let permissionPrompt = {
 *         get permissionKey() {
 *           return "sound-permission";
 *         }
 *         // etc - see the documentation for PermissionPrompt for
 *         // a better idea of what things one can and should override.
 *       };
 *       Object.setPrototypeOf(
 *         permissionPrompt,
 *         PermissionUI.PermissionPromptForRequest
 *       );
 *       return permissionPrompt;
 *     },
 *   };
 *   Object.setPrototypeOf(soundCardObj, base);
 *   return soundCardObj;
 * };
 *
 * // Add-on startup:
 * Integration.contentPermission.register(SoundCardIntegration);
 * // ...
 * // Add-on shutdown:
 * Integration.contentPermission.unregister(SoundCardIntegration);
 *
 * Note that PermissionPromptForRequest must be used as the
 * prototype, since the prompt is wrapping an nsIContentPermissionRequest,
 * and going through nsIContentPermissionPrompt.
 *
 * It is, however, possible to take advantage of PermissionPrompt without
 * having to go through nsIContentPermissionPrompt or with a
 * nsIContentPermissionRequest. The PermissionPrompt can be
 * imported, subclassed, and have prompt() called directly, without
 * the caller having called into createPermissionPrompt.
 */
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  DownloadUtils: "resource://gre/modules/DownloadUtils.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SitePermissions: "resource:///modules/SitePermissions.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  PERMISSION_UI_FEATURE_ID:
    "resource:///modules/PermissionPromptTargeting.sys.mjs",
  evalPermissionPromptTargeting:
    "resource:///modules/PermissionPromptTargeting.sys.mjs",
  isValidLogoUrl: "resource:///modules/PermissionPromptTargeting.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "IDNService",
  "@mozilla.org/network/idn-service;1",
  Ci.nsIIDNService
);
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "ContentPrefService2",
  "@mozilla.org/content-pref/service;1",
  Ci.nsIContentPrefService2
);
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "SiteCategory",
  "@mozilla.org/site-category;1",
  Ci.nsISiteCategory
);

ChromeUtils.defineLazyGetter(lazy, "gBrandBundle", function () {
  return Services.strings.createBundle(
    "chrome://branding/locale/brand.properties"
  );
});

ChromeUtils.defineLazyGetter(lazy, "gBrowserBundle", function () {
  return Services.strings.createBundle(
    "chrome://browser/locale/browser.properties"
  );
});

ChromeUtils.defineLazyGetter(lazy, "gFluentStrings", function () {
  return new Localization(
    ["branding/brand.ftl", "browser/permissions.ftl"],
    true /* aSync */
  );
});

import { SITEPERMS_ADDON_PROVIDER_PREF } from "resource://gre/modules/addons/siteperms-addon-utils.sys.mjs";

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "sitePermsAddonsProviderEnabled",
  SITEPERMS_ADDON_PROVIDER_PREF,
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "lnaPromptTimeoutMs",
  "network.lna.prompt.timeout",
  300000
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "lnaTemporaryPermissionExpireTimeMs",
  "network.lna.temporary_permission_expire_time_ms",
  24 * 3600 * 1000 // 24 hours
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "webserialGated",
  "dom.webserial.gated",
  true
);

/**
 * PermissionPrompt should be subclassed by callers that
 * want to display prompts to the user. See each method and property
 * below for guidance on what to override.
 *
 * Note that if you're creating a prompt for an
 * nsIContentPermissionRequest, you'll want to subclass
 * PermissionPromptForRequest instead.
 */
class PermissionPrompt {
  /**
   * Returns the associated <xul:browser> for the request. This should
   * work for the e10s and non-e10s case.
   *
   * Subclasses must override this.
   *
   * @return {<xul:browser>}
   */
  get browser() {
    throw new Error("Not implemented.");
  }

  /**
   * Returns the nsIPrincipal associated with the request.
   *
   * Subclasses must override this.
   *
   * @return {nsIPrincipal}
   */
  get principal() {
    throw new Error("Not implemented.");
  }

  /**
   * Indicates the type of the permission request from content. This type might
   * be different from the permission key used in the permissions database.
   */
  get type() {
    return undefined;
  }

  /**
   * If the nsIPermissionManager is being queried and written
   * to for this permission request, set this to the key to be
   * used. If this is undefined, no integration with temporary
   * permissions infrastructure will be provided.
   *
   * Note that if a permission is set, in any follow-up
   * prompting within the expiry window of that permission,
   * the prompt will be skipped and the allow or deny choice
   * will be selected automatically.
   */
  get permissionKey() {
    return undefined;
  }

  /**
   * If true, user permissions will be read from and written to.
   * When this is false, we still provide integration with
   * infrastructure such as temporary permissions. permissionKey should
   * still return a valid name in those cases for that integration to work.
   */
  get usePermissionManager() {
    return true;
  }

  /**
   * Indicates what URI should be used as the scope when using temporary
   * permissions. If undefined, it defaults to the browser.currentURI.
   */
  get temporaryPermissionURI() {
    return undefined;
  }

  /**
   * Custom expiration time in milliseconds for temporary permissions.
   * If undefined, uses the default from SitePermissions.temporaryPermissionExpireTime.
   */
  get temporaryPermissionExpireTimeMS() {
    return undefined;
  }

  /**
   * These are the options that will be passed to the PopupNotification when it
   * is shown. See the documentation of `PopupNotifications_show` in
   * PopupNotifications.sys.mjs for details.
   *
   * Note that prompt() will automatically set displayURI to
   * be the URI of the requesting pricipal, unless the displayURI is exactly
   * set to false.
   */
  get popupOptions() {
    return {};
  }

  /**
   * If true, automatically denied permission requests will
   * spawn a "post-prompt" that allows the user to correct the
   * automatic denial by giving permanent permission access to
   * the site.
   *
   * Note that if this function returns true, the permissionKey
   * and postPromptActions attributes must be implemented.
   */
  get postPromptEnabled() {
    return false;
  }

  /**
   * If true, the prompt will be cancelled automatically unless
   * request.hasValidTransientUserGestureActivation is true.
   */
  get requiresUserInput() {
    return false;
  }

  /**
   * PopupNotification requires a unique ID to open the notification.
   * You must return a unique ID string here, for which PopupNotification
   * will then create a <xul:popupnotification> node with the ID
   * "<notificationID>-notification".
   *
   * If there's a custom <xul:popupnotification> you're hoping to show,
   * then you need to make sure its ID has the "-notification" suffix,
   * and then return the prefix here.
   *
   * See PopupNotifications.sys.mjs for more details.
   *
   * @return {string}
   *         The unique ID that will be used to as the
   *         "<unique ID>-notification" ID for the <xul:popupnotification>
   *         to use or create.
   */
  get notificationID() {
    throw new Error("Not implemented.");
  }

  /**
   * The ID of the element to anchor the PopupNotification to.
   *
   * @return {string}
   */
  get anchorID() {
    return "default-notification-icon";
  }

  /**
   * The message to show to the user in the PopupNotification, see
   * `PopupNotifications_show` in PopupNotifications.sys.mjs.
   *
   * Subclasses must override this.
   *
   * @return {string}
   */
  get message() {
    throw new Error("Not implemented.");
  }

  /**
   * The hint text to show to the user in the PopupNotification, see
   * `PopupNotifications_show` in PopupNotifications.sys.mjs.
   * By default, no hint is shown.
   *
   * @return {string}
   */
  get hintText() {
    return undefined;
  }

  /**
   * Provides the preferred name to use in the permission popups,
   * based on the principal URI (the URI.hostPort for any URI scheme
   * besides the moz-extension one which should default to the
   * extension name).
   */
  getPrincipalName(principal = this.principal) {
    if (principal.addonPolicy) {
      return principal.addonPolicy.name;
    }

    return principal.hostPort;
  }

  /**
   * This will be called if the request is to be cancelled.
   *
   * Subclasses only need to override this if they provide a
   * permissionKey.
   */
  cancel() {
    throw new Error("Not implemented.");
  }

  /**
   * This will be called if the request is to be allowed.
   *
   * Subclasses only need to override this if they provide a
   * permissionKey.
   */
  allow() {
    throw new Error("Not implemented.");
  }

  /**
   * The actions that will be displayed in the PopupNotification
   * via a dropdown menu. The first item in this array will be
   * the default selection. Each action is an Object with the
   * following properties:
   *
   *  label (string):
   *    The label that will be displayed for this choice.
   *  accessKey (string):
   *    The access key character that will be used for this choice.
   *  action (SitePermissions state)
   *    The action that will be associated with this choice.
   *    This should be either SitePermissions.ALLOW or SitePermissions.BLOCK.
   *  scope (SitePermissions scope)
   *    The scope of the associated action (e.g. SitePermissions.SCOPE_PERSISTENT)
   *
   *  callback (function, optional)
   *    A callback function that will fire if the user makes this choice, with
   *    a single parameter, state. State is an Object that contains the property
   *    checkboxChecked, which identifies whether the checkbox to remember this
   *    decision was checked.
   */
  get promptActions() {
    return [];
  }

  /**
   * The actions that will be displayed in the PopupNotification
   * for post-prompt notifications via a dropdown menu.
   * The first item in this array will be the default selection.
   * Each action is an Object with the following properties:
   *
   *  label (string):
   *    The label that will be displayed for this choice.
   *  accessKey (string):
   *    The access key character that will be used for this choice.
   *  action (SitePermissions state)
   *    The action that will be associated with this choice.
   *    This should be either SitePermissions.ALLOW or SitePermissions.BLOCK.
   *    Note that the scope of this action will always be persistent.
   *
   *  callback (function, optional)
   *    A callback function that will fire if the user makes this choice.
   */
  get postPromptActions() {
    return null;
  }

  /**
   * If the prompt will be shown to the user, this callback will
   * be called just before. Subclasses may want to override this
   * in order to, for example, bump a counter Telemetry probe for
   * how often a particular permission request is seen.
   *
   * If this returns false, it cancels the process of showing the prompt.  In
   * that case, it is the responsibility of the onBeforeShow() implementation
   * to ensure that allow() or cancel() are called on the object appropriately.
   */
  onBeforeShow() {
    return true;
  }

  /**
   * Async pre-show hook. Called by prompt() after it has decided the prompt
   * (or post-prompt) will actually be shown, but before any of the UI getters
   * (message, promptActions, popupOptions, hintText) are read. Subclasses that
   * need to resolve asynchronous state the UI depends on override this and
   * return a promise; prompt() awaits it. The default is a synchronous no-op
   * that returns nothing, so prompt() stays fully synchronous for every prompt
   * that doesn't override it.
   *
   * @returns {?Promise} A promise to await before showing, or nothing.
   */
  onBeforeShowAsync() {}

  /**
   * If the prompt was shown to the user, this callback will be called just
   * after it's been shown.
   */
  onShown() {}

  /**
   * If the prompt was shown to the user, this callback will be called just
   * after it's been hidden.
   */
  onAfterShow() {}

  /**
   * Will determine if a prompt should be shown to the user, and if so,
   * will show it.
   *
   * If a permissionKey is defined prompt() might automatically
   * allow or cancel itself based on the user's current
   * permission settings without displaying the prompt.
   *
   * If the permission is not already set and the <xul:browser> that the request
   * is associated with does not belong to a browser window with the
   * PopupNotifications global set, the prompt request is ignored.
   */
  async prompt() {
    // We ignore requests from non-nsIStandardURLs
    let requestingURI = this.principal.URI;
    if (!(requestingURI instanceof Ci.nsIStandardURL)) {
      return;
    }

    if (this.usePermissionManager && this.permissionKey) {
      // If we're reading and setting permissions, then we need
      // to check to see if we already have a permission setting
      // for this particular principal.
      let { state } = lazy.SitePermissions.getForPrincipal(
        this.principal,
        this.permissionKey,
        this.browser,
        this.temporaryPermissionURI
      );

      if (state == lazy.SitePermissions.BLOCK) {
        this.cancel();
        return;
      }

      if (
        state == lazy.SitePermissions.ALLOW &&
        !this.request.isRequestDelegatedToUnsafeThirdParty &&
        !this.request.ignoreAllowSitePermission
      ) {
        this.allow();
        return;
      }
    } else if (this.permissionKey) {
      // If we're reading a permission which already has a temporary value,
      // see if we can use the temporary value.
      let { state } = lazy.SitePermissions.getForPrincipal(
        null,
        this.permissionKey,
        this.browser,
        this.temporaryPermissionURI
      );

      if (state == lazy.SitePermissions.BLOCK) {
        this.cancel();
        return;
      }
    }

    if (
      this.requiresUserInput &&
      !this.request.hasValidTransientUserGestureActivation
    ) {
      if (this.postPromptEnabled) {
        // Let subclasses resolve async state before postPrompt() reads the UI
        // getters. Only awaited when a subclass returns a promise (see the
        // onBeforeShowAsync() default no-op).
        let beforeShow = this.onBeforeShowAsync();
        if (beforeShow) {
          await beforeShow;
        }
        this.postPrompt();
      }
      this.cancel();
      return;
    }

    let chromeWin = this.browser.documentGlobal;
    if (!chromeWin.PopupNotifications) {
      this.cancel();
      return;
    }

    // Let subclasses resolve async state before we read the UI getters below.
    // Only awaited when a subclass returns a promise, so prompts that don't
    // override onBeforeShowAsync() stay fully synchronous through to show().
    let beforeShow = this.onBeforeShowAsync();
    if (beforeShow) {
      await beforeShow;
    }

    // Transform the PermissionPrompt actions into PopupNotification actions.
    let popupNotificationActions = [];
    for (let promptAction of this.promptActions) {
      let action = {
        label: promptAction.label,
        accessKey: promptAction.accessKey,
        callback: state => {
          if (promptAction.callback) {
            promptAction.callback();
          }

          if (this.usePermissionManager && this.permissionKey) {
            if (
              (state && state.checkboxChecked && state.source != "esc-press") ||
              promptAction.scope == lazy.SitePermissions.SCOPE_PERSISTENT
            ) {
              // Permanently store permission.
              let scope = lazy.SitePermissions.SCOPE_PERSISTENT;
              // Only remember permission for session if in PB mode.
              if (lazy.PrivateBrowsingUtils.isBrowserPrivate(this.browser)) {
                scope = lazy.SitePermissions.SCOPE_SESSION;
              }
              lazy.SitePermissions.setForPrincipal(
                this.principal,
                this.permissionKey,
                promptAction.action,
                scope
              );
            } else if (this.browser.contentPrincipal.equals(this.principal)) {
              // Browser-scoped permissions are only ever keyed to a tab's
              // top-level document. A delegated request from a cross-origin
              // subframe carries that subframe's own principal, and an entry
              // stored under it would outlive the frame, be readable under a
              // different first party, and be invisible to every path that
              // lists or revokes permissions. Grant the request in flight and
              // store nothing, so the next one prompts again.
              lazy.SitePermissions.setForPrincipal(
                this.principal,
                this.permissionKey,
                promptAction.action,
                lazy.SitePermissions.SCOPE_TEMPORARY,
                this.browser,
                this.temporaryPermissionExpireTimeMS
              );
            }

            // Grant permission if action is ALLOW.
            if (promptAction.action == lazy.SitePermissions.ALLOW) {
              this.allow();
            } else {
              this.cancel();
            }
          } else if (this.permissionKey) {
            lazy.SitePermissions.setForPrincipal(
              null,
              this.permissionKey,
              promptAction.action,
              lazy.SitePermissions.SCOPE_TEMPORARY,
              this.browser,
              this.temporaryPermissionExpireTimeMS
            );
          }
        },
      };
      if (promptAction.dismiss) {
        action.dismiss = promptAction.dismiss;
      }

      // Deny actions are not a clickjacking target (an attacker has nothing
      // to gain by tricking a user into denying a permission), so they fire
      // immediately without the security delay. See bug 2035581.
      if (promptAction.action === lazy.SitePermissions.BLOCK) {
        action.disableSecurityDelay = true;
      }

      popupNotificationActions.push(action);
    }

    this.#showNotification(popupNotificationActions);
  }

  postPrompt() {
    let browser = this.browser;
    let principal = this.principal;
    let chromeWin = browser.documentGlobal;
    if (!chromeWin.PopupNotifications) {
      return;
    }

    if (!this.permissionKey) {
      throw new Error("permissionKey is required to show a post-prompt");
    }

    if (!this.postPromptActions) {
      throw new Error("postPromptActions are required to show a post-prompt");
    }

    // Transform the PermissionPrompt actions into PopupNotification actions.
    let popupNotificationActions = [];
    for (let promptAction of this.postPromptActions) {
      let action = {
        label: promptAction.label,
        accessKey: promptAction.accessKey,
        callback: () => {
          if (promptAction.callback) {
            promptAction.callback();
          }

          // Post-prompt permissions are stored permanently by default.
          // Since we can not reply to the original permission request anymore,
          // the page will need to listen for permission changes which are triggered
          // by permanent entries in the permission manager.
          let scope = lazy.SitePermissions.SCOPE_PERSISTENT;
          // Only remember permission for session if in PB mode.
          if (lazy.PrivateBrowsingUtils.isBrowserPrivate(browser)) {
            scope = lazy.SitePermissions.SCOPE_SESSION;
          }
          lazy.SitePermissions.setForPrincipal(
            principal,
            this.permissionKey,
            promptAction.action,
            scope
          );
        },
      };
      popupNotificationActions.push(action);
    }

    // Post-prompt animation
    if (!chromeWin.gReduceMotion) {
      let anchor = chromeWin.document.getElementById(this.anchorID);
      // Only show the animation on the first request, not after e.g. tab switching.
      anchor.addEventListener(
        "animationend",
        () => anchor.removeAttribute("animate"),
        { once: true }
      );
      anchor.setAttribute("animate", "true");
    }

    this.#showNotification(popupNotificationActions, true);
  }

  #showNotification(actions, postPrompt = false) {
    let chromeWin = this.browser.documentGlobal;
    let mainAction = actions.length ? actions[0] : null;
    let secondaryActions = actions.splice(1);

    let options = this.popupOptions;

    if (!options.hasOwnProperty("displayURI") || options.displayURI) {
      options.displayURI = this.principal.URI;
    }

    if (!postPrompt) {
      // Permission prompts are always persistent; the close button is controlled by a pref.
      options.persistent = true;
      options.hideClose = true;
    }

    options.eventCallback = (topic, nextRemovalReason, withoutUserResponse) => {
      // When the docshell of the browser is aboout to be swapped to another one,
      // the "swapping" event is called. Returning true causes the notification
      // to be moved to the new browser.
      if (topic == "swapping") {
        return true;
      }
      // The prompt has been shown, notify the PermissionUI.
      // onShown() is currently not called for post-prompts,
      // because there is no prompt that would make use of this.
      // You can remove this restriction if you need it, but be
      // mindful of other consumers.
      if (topic == "shown" && !postPrompt) {
        this.onShown();
      }
      // The prompt has been removed, notify the PermissionUI.
      // onAfterShow() is currently not called for post-prompts,
      // because there is no prompt that would make use of this.
      // You can remove this restriction if you need it, but be
      // mindful of other consumers.
      if (topic == "removed" && !postPrompt) {
        if (withoutUserResponse) {
          this.cancel();
        }
        this.onAfterShow();
      }
      return false;
    };

    options.hintText = this.hintText;
    // Post-prompts show up as dismissed.
    options.dismissed = postPrompt;

    // onBeforeShow() is currently not called for post-prompts,
    // because there is no prompt that would make use of this.
    // You can remove this restriction if you need it, but be
    // mindful of other consumers.
    if (postPrompt || this.onBeforeShow() !== false) {
      chromeWin.PopupNotifications.show(
        this.browser,
        this.notificationID,
        this.message,
        this.anchorID,
        mainAction,
        secondaryActions,
        options
      );
    }
  }
}

/**
 * A subclass of PermissionPrompt that assumes
 * that this.request is an nsIContentPermissionRequest
 * and fills in some of the required properties on the
 * PermissionPrompt. For callers that are wrapping an
 * nsIContentPermissionRequest, this should be subclassed
 * rather than PermissionPrompt.
 */
class PermissionPromptForRequest extends PermissionPrompt {
  get browser() {
    // In the e10s-case, the <xul:browser> will be at request.element.
    // In the single-process case, we have to use some XPCOM incantations
    // to resolve to the <xul:browser>.
    if (this.request.element) {
      return this.request.element;
    }
    return this.request.window.docShell.chromeEventHandler;
  }

  get principal() {
    let request = this.request.QueryInterface(Ci.nsIContentPermissionRequest);
    return request.getDelegatePrincipal(this.type);
  }

  cancel() {
    this.request.cancel();
  }

  allow(choices) {
    this.request.allow(choices);
  }
}

/**
 * A subclass of PermissionPromptForRequest that prompts
 * for a Synthetic SitePermsAddon addon type and starts a synthetic
 * addon install flow.
 */
class SitePermsAddonInstallRequest extends PermissionPromptForRequest {
  /**
   * Triggers the addon install flow and calls the appropriate callback based on the result.
   * This method encapsulates the common logic for installing a SitePermsAddon and handling
   * success/failure cases, including error logging.
   *
   * @param {Function} onSuccess - Callback to invoke if the addon is installed successfully
   * @param {Function} onError - Callback to invoke if the addon installation fails
   */
  async installSitePermAddon(onSuccess, onError) {
    // The user chose to never allow add-on installs from this site, so the
    // site permission add-on install would be denied anyway.
    if (
      Services.perms.testPermissionFromPrincipal(this.principal, "install") ==
      Services.perms.DENY_ACTION
    ) {
      this.cancelWithRandomizedDelay();
      return;
    }

    try {
      await lazy.AddonManager.installSitePermsAddonFromWebpage(
        this.browser,
        this.principal,
        this.permName
      );
      onSuccess();
    } catch (err) {
      onError();

      let scriptErrorClass = Cc["@mozilla.org/scripterror;1"];
      let errorMessage =
        this.getInstallErrorMessage(err) ||
        `${this.permName} access was rejected: ${err.message}`;

      let scriptError = scriptErrorClass.createInstance(Ci.nsIScriptError);
      scriptError.initWithWindowID(
        errorMessage,
        null,
        0,
        0,
        0,
        "content javascript",
        this.browser.browsingContext.currentWindowGlobal.innerWindowId
      );
      Services.console.logMessage(scriptError);
    }
  }

  prompt() {
    // fallback to regular permission prompt for localhost,
    // or when the SitePermsAddonProvider is not enabled.
    if (this.principal.isLoopbackHost || !lazy.sitePermsAddonsProviderEnabled) {
      super.prompt();
      return;
    }

    // Otherwise, we'll use the addon install flow.
    this.installSitePermAddon(
      () => {
        this.allow();
      },
      () => {
        this.cancel();
      }
    );
  }

  /**
   * Cancels the request after a randomized delay between 3 and 13 seconds,
   * which models the time the user might spend considering a prompt before
   * denying it. This prevents sites from discerning that no prompt was shown.
   */
  cancelWithRandomizedDelay() {
    const baseDelayMS = 3 * 1000;
    const randomDelayMS = Cu.isInAutomation
      ? 0
      : Math.floor(Math.random() * 10 * 1000);
    lazy.setTimeout(() => this.cancel(), baseDelayMS + randomDelayMS);
  }

  /**
   * Returns an error message that will be printed to the console given a passed Component.Exception.
   * This should be overriden by children classes.
   *
   * @param {Components.Exception} err
   * @returns {string} The error message
   */
  getInstallErrorMessage() {
    return null;
  }
}

/**
 * Creates a PermissionPrompt for a nsIContentPermissionRequest for
 * the GeoLocation API.
 *
 * @param request (nsIContentPermissionRequest)
 *        The request for a permission from content.
 */
class GeolocationPermissionPrompt extends PermissionPromptForRequest {
  constructor(request) {
    super();
    this.request = request;
    let types = request.types.QueryInterface(Ci.nsIArray);
    let perm = types.queryElementAt(0, Ci.nsIContentPermissionType);
    if (perm.options.length) {
      this.systemPermissionMsg = perm.options.queryElementAt(
        0,
        Ci.nsISupportsString
      );
    }
  }

  get type() {
    return "geo";
  }

  get permissionKey() {
    return "geo";
  }

  get popupOptions() {
    let pref = "browser.geolocation.warning.infoURL";
    let options = {
      learnMoreURL: Services.urlFormatter.formatURLPref(pref),
      displayURI: false,
      name: this.getPrincipalName(),
    };

    // Don't offer "always remember" action in PB mode
    options.checkbox = {
      show: !lazy.PrivateBrowsingUtils.isWindowPrivate(
        this.browser.documentGlobal
      ),
    };

    if (this.request.isRequestDelegatedToUnsafeThirdParty) {
      // Second name should be the third party origin
      options.secondName = this.getPrincipalName(this.request.principal);
      options.checkbox = { show: false };
    }

    if (options.checkbox.show) {
      options.checkbox.label = lazy.gBrowserBundle.GetStringFromName(
        "geolocation.remember"
      );
    }

    return options;
  }

  get notificationID() {
    return "geolocation";
  }

  get anchorID() {
    return "geo-notification-icon";
  }

  get message() {
    if (this.principal.schemeIs("file")) {
      return lazy.gBrowserBundle.GetStringFromName(
        "geolocation.shareWithFile4"
      );
    }

    if (this.request.isRequestDelegatedToUnsafeThirdParty) {
      return lazy.gBrowserBundle.formatStringFromName(
        "geolocation.shareWithSiteUnsafeDelegation2",
        ["<>", "{}"]
      );
    }

    return lazy.gBrowserBundle.formatStringFromName(
      "geolocation.shareWithSite4",
      ["<>"]
    );
  }

  get hintText() {
    let productName = lazy.gBrandBundle.GetStringFromName("brandShortName");

    if (this.systemPermissionMsg == "sysdlg") {
      return lazy.gBrowserBundle.formatStringFromName(
        "geolocation.systemWillRequestPermission",
        [productName]
      );
    }

    if (this.systemPermissionMsg == "syssetting") {
      return lazy.gBrowserBundle.formatStringFromName(
        "geolocation.needsSystemSetting",
        [productName]
      );
    }

    return undefined;
  }

  get promptActions() {
    return [
      {
        label: lazy.gBrowserBundle.GetStringFromName("geolocation.allow"),
        accessKey: lazy.gBrowserBundle.GetStringFromName(
          "geolocation.allow.accesskey"
        ),
        action: lazy.SitePermissions.ALLOW,
      },
      {
        label: lazy.gBrowserBundle.GetStringFromName("geolocation.block"),
        accessKey: lazy.gBrowserBundle.GetStringFromName(
          "geolocation.block.accesskey"
        ),
        action: lazy.SitePermissions.BLOCK,
      },
    ];
  }

  #updateGeoSharing(state) {
    let gBrowser = this.browser.documentGlobal.gBrowser;
    if (gBrowser == null) {
      return;
    }
    gBrowser.updateBrowserSharing(this.browser, { geo: state });

    // Update last access timestamp
    let host;
    try {
      host = this.browser.currentURI.host;
    } catch (e) {
      return;
    }
    if (host == null || host == "") {
      return;
    }
    lazy.ContentPrefService2.set(
      this.browser.currentURI.host,
      "permissions.geoLocation.lastAccess",
      new Date().toString(),
      this.browser.loadContext
    );
  }

  allow(...args) {
    this.#updateGeoSharing(true);
    super.allow(...args);
  }

  cancel(...args) {
    this.#updateGeoSharing(false);
    super.cancel(...args);
  }

  ignoreAllowSitePermission() {
    return this.request.ignoreAllowSitePermission;
  }
}

/**
 * Creates a PermissionPrompt for a nsIContentPermissionRequest for
 * the WebXR API.
 *
 * @param request (nsIContentPermissionRequest)
 *        The request for a permission from content.
 */
class XRPermissionPrompt extends PermissionPromptForRequest {
  constructor(request) {
    super();
    this.request = request;
  }

  get type() {
    return "xr";
  }

  get permissionKey() {
    return "xr";
  }

  get popupOptions() {
    let pref = "browser.xr.warning.infoURL";
    let options = {
      learnMoreURL: Services.urlFormatter.formatURLPref(pref),
      displayURI: false,
      name: this.getPrincipalName(),
    };

    // Don't offer "always remember" action in PB mode
    options.checkbox = {
      show: !lazy.PrivateBrowsingUtils.isWindowPrivate(
        this.browser.documentGlobal
      ),
    };

    if (options.checkbox.show) {
      options.checkbox.label =
        lazy.gBrowserBundle.GetStringFromName("xr.remember");
    }

    return options;
  }

  get notificationID() {
    return "xr";
  }

  get anchorID() {
    return "xr-notification-icon";
  }

  get message() {
    if (this.principal.schemeIs("file")) {
      return lazy.gBrowserBundle.GetStringFromName("xr.shareWithFile4");
    }

    return lazy.gBrowserBundle.formatStringFromName("xr.shareWithSite4", [
      "<>",
    ]);
  }

  get promptActions() {
    return [
      {
        label: lazy.gBrowserBundle.GetStringFromName("xr.allow2"),
        accessKey: lazy.gBrowserBundle.GetStringFromName("xr.allow2.accesskey"),
        action: lazy.SitePermissions.ALLOW,
      },
      {
        label: lazy.gBrowserBundle.GetStringFromName("xr.block"),
        accessKey: lazy.gBrowserBundle.GetStringFromName("xr.block.accesskey"),
        action: lazy.SitePermissions.BLOCK,
      },
    ];
  }

  #updateXRSharing(state) {
    let gBrowser = this.browser.documentGlobal.gBrowser;
    if (gBrowser == null) {
      return;
    }
    gBrowser.updateBrowserSharing(this.browser, { xr: state });

    let devicePermOrigins = this.browser.getDevicePermissionOrigins("xr");
    if (!state) {
      devicePermOrigins.delete(this.principal.origin);
      return;
    }
    devicePermOrigins.add(this.principal.origin);
  }

  allow(...args) {
    this.#updateXRSharing(true);
    super.allow(...args);
  }

  cancel(...args) {
    this.#updateXRSharing(false);
    super.cancel(...args);
  }
}

/**
 * Base class for Local Network Access (LNA) permission prompts.
 * Provides automatic timeout handling for LNA prompts.
 *
 * If the user doesn't respond to the prompt within the timeout period,
 * the prompt is automatically cancelled and the network request fails.
 */
class LNAPermissionPromptBase extends PermissionPromptForRequest {
  static DEFAULT_PROMPT_TIMEOUT_MS = 300000;

  #timeoutTimer = null;

  constructor(request) {
    super();
    this.request = request;
  }

  onBeforeShow() {
    // Notify LNAPermissionRequest that the prompt is being shown.
    // This triggers telemetry recording and notifies nsHttpChannel.
    if (typeof this.request.notifyShown === "function") {
      this.request.notifyShown();
    }
    return true;
  }

  onShown() {
    this.#startTimeoutTimer();
  }

  onAfterShow() {
    this.#clearTimeoutTimer();
  }

  cancel() {
    super.cancel();
  }

  allow(choices) {
    super.allow(choices);
  }

  get temporaryPermissionExpireTimeMS() {
    // LNA temporary permissions have a custom expiration time (default 24 hours)
    return lazy.lnaTemporaryPermissionExpireTimeMs;
  }

  #startTimeoutTimer() {
    this.#clearTimeoutTimer();

    this.#timeoutTimer = lazy.setTimeout(() => {
      let scriptError = Cc["@mozilla.org/scripterror;1"].createInstance(
        Ci.nsIScriptError
      );
      scriptError.initWithWindowID(
        `LNA permission prompt timed out after ${lazy.lnaPromptTimeoutMs / 1000} seconds`,
        null,
        0,
        0,
        Ci.nsIScriptError.warningFlag,
        "content javascript",
        this.browser.browsingContext.currentWindowGlobal.innerWindowId
      );
      Services.console.logMessage(scriptError);

      this.#removePrompt();
      this.cancel();
    }, lazy.lnaPromptTimeoutMs);
  }

  #removePrompt() {
    let chromeWin = this.browser?.documentGlobal;
    let notification = chromeWin?.PopupNotifications.getNotification(
      this.notificationID,
      this.browser
    );
    if (notification) {
      chromeWin.PopupNotifications.remove(notification);
    }
  }

  #clearTimeoutTimer() {
    if (this.#timeoutTimer) {
      lazy.clearTimeout(this.#timeoutTimer);
      this.#timeoutTimer = null;
    }
  }
}

/**
 * Creates a PermissionPrompt for a nsIContentPermissionRequest for
 * the Local Host Access.
 *
 * @param request (nsIContentPermissionRequest)
 *        The request for a permission from content.
 */
class LoopbackNetworkPermissionPrompt extends LNAPermissionPromptBase {
  get type() {
    return "loopback-network";
  }

  get permissionKey() {
    return "loopback-network";
  }

  get popupOptions() {
    let options = {
      learnMoreURL: Services.urlFormatter.formatURLPref(
        "browser.lna.warning.infoURL"
      ),
      displayURI: false,
      name: this.getPrincipalName(),
    };

    // Don't offer "always remember" action in PB mode
    options.checkbox = {
      show: !lazy.PrivateBrowsingUtils.isWindowPrivate(
        this.browser.documentGlobal
      ),
    };

    if (options.checkbox.show) {
      options.checkbox.label = lazy.gBrowserBundle.GetStringFromName(
        "localhost.remember2"
      );
    }

    return options;
  }

  get notificationID() {
    return "loopback-network";
  }

  get anchorID() {
    return "loopback-network-notification-icon";
  }

  get message() {
    return lazy.gBrowserBundle.formatStringFromName(
      "localhost.allowWithSite2",
      ["<>"]
    );
  }

  get promptActions() {
    return [
      {
        label: lazy.gBrowserBundle.GetStringFromName("localhost.allowlabel"),
        accessKey: lazy.gBrowserBundle.GetStringFromName(
          "localhost.allow.accesskey"
        ),
        action: lazy.SitePermissions.ALLOW,
      },
      {
        label: lazy.gBrowserBundle.GetStringFromName("localhost.blocklabel"),
        accessKey: lazy.gBrowserBundle.GetStringFromName(
          "localhost.block.accesskey"
        ),
        action: lazy.SitePermissions.BLOCK,
      },
    ];
  }
}

/**
 * Creates a PermissionPrompt for a nsIContentPermissionRequest for
 * the Desktop Notification API.
 *
 * @param request (nsIContentPermissionRequest)
 *        The request for a permission from content.
 * @return {PermissionPrompt} (see documentation in header)
 */
class DesktopNotificationPermissionPrompt extends PermissionPromptForRequest {
  // Nimbus state resolved at prompt-shown time (see #resolveTreatment).
  // #treatment null means "render the default copy"; #exposureQualified false
  // means "record no exposure".
  #treatment = null;
  #exposureQualified = false;

  constructor(request) {
    super();
    this.request = request;

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "requiresUserInput",
      "dom.webnotifications.requireuserinteraction"
    );
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "postPromptEnabled",
      "permissions.desktop-notification.postPrompt.enabled"
    );
  }

  get type() {
    return "desktop-notification";
  }

  get permissionKey() {
    return "desktop-notification";
  }

  /**
   * Resolve an in-tree Fluent id (from browser/permissions.ftl) to its value
   * and optional accesskey attribute. Returns null when the id is unset or
   * cannot be resolved, so callers fall back to a raw string or the default.
   *
   * @param {string} id - Fluent message id.
   * @param {object} [args] - Fluent variables.
   * @returns {?{value: string, accessKey: ?string}}
   */
  #resolveL10n(id, args) {
    if (!id) {
      return null;
    }
    try {
      let [message] = lazy.gFluentStrings.formatMessagesSync([{ id, args }]);
      if (!message) {
        return null;
      }
      let accessKey = message.attributes?.find(
        attr => attr.name === "accesskey"
      )?.value;
      return { value: message.value, accessKey };
    } catch (e) {
      console.error(
        `Failed to resolve notification prompt l10n id "${id}":`,
        e
      );
      return null;
    }
  }

  /**
   * Relabel the Allow / (always-)block CTAs from the Nimbus treatment, in place.
   * Shared by the modal prompt and the quiet post-prompt so both surfaces show
   * the same labels. Each label may come from a Fluent id (*L10nId, resolved
   * in-tree, with the accesskey taken from the message's .accesskey attribute)
   * or a raw string; the id takes precedence. We only relabel (never touch
   * action/scope/callback), so the permission semantics are unchanged. Per
   * product, the block label is left at its default in private browsing, where
   * the wording ("Block" vs "Always Block") reflects the real
   * session-vs-persistent BLOCK semantics.
   *
   * @param {object} allowAction - The Allow action to relabel.
   * @param {object} blockAction - The (always-)block action to relabel.
   */
  #applyTreatmentLabels(allowAction, blockAction) {
    if (!this.#treatment) {
      return;
    }
    let t = this.#treatment;

    let primary = this.#resolveL10n(t.primaryCtaLabelL10nId) ?? {
      value: t.primaryCtaLabel,
      accessKey: t.primaryCtaAccessKey,
    };
    if (primary.value) {
      allowAction.label = primary.value;
      if (primary.accessKey) {
        allowAction.accessKey = primary.accessKey;
      }
    }

    // Leave the block label at its default in private browsing: there the
    // wording ("Block") reflects the session-scoped BLOCK semantics rather than
    // the persistent "Always Block", so we don't let the treatment relabel it.
    if (lazy.PrivateBrowsingUtils.isBrowserPrivate(this.browser)) {
      return;
    }
    let secondary = this.#resolveL10n(t.secondaryCtaLabelL10nId) ?? {
      value: t.secondaryCtaLabel,
      accessKey: t.secondaryCtaAccessKey,
    };
    if (secondary.value) {
      blockAction.label = secondary.value;
      if (secondary.accessKey) {
        blockAction.accessKey = secondary.accessKey;
      }
    }
  }

  get popupOptions() {
    let learnMoreURL =
      Services.urlFormatter.formatURLPref("app.support.baseURL") + "push";

    return {
      learnMoreURL,
      displayURI: false,
      name: this.getPrincipalName(),
      // Render the experiment logo as the prompt icon. #resolveTreatment stores
      // an invalid logo as null, which falls back to the default icon.
      popupIconURL: this.#treatment?.logoUrl,
    };
  }

  get notificationID() {
    return "web-notifications";
  }

  get anchorID() {
    return "web-notifications-notification-icon";
  }

  get message() {
    // The headline must contain "<>", the token PopupNotifications swaps for
    // the site name. This prompt sets displayURI: false, so the headline is the
    // only place the origin appears; like every other permission prompt, it
    // must name the requesting site. A treatment headline (raw or resolved from
    // a Fluent id, which may inject "<>" via the $host variable) that omits the
    // token falls back to the default copy rather than show a consent prompt
    // with no origin.
    let resolved = this.#resolveL10n(this.#treatment?.headlineL10nId, {
      host: "<>",
    });
    let candidate = resolved?.value || this.#treatment?.headline;

    if (candidate?.includes("<>")) {
      return candidate;
    }

    return lazy.gBrowserBundle.formatStringFromName(
      "webNotifications.receiveFromSite3",
      ["<>"]
    );
  }

  get hintText() {
    return (
      this.#resolveL10n(this.#treatment?.bodyL10nId)?.value ||
      this.#treatment?.body ||
      undefined
    );
  }

  get promptActions() {
    // Capture the site category now, while this.principal is still valid
    let siteCategory = lazy.SiteCategory.getCategory(this.principal);

    let allowAction = {
      label: lazy.gBrowserBundle.GetStringFromName("webNotifications.allow2"),
      accessKey: lazy.gBrowserBundle.GetStringFromName(
        "webNotifications.allow2.accesskey"
      ),
      action: lazy.SitePermissions.ALLOW,
      scope: lazy.SitePermissions.SCOPE_PERSISTENT,
      callback: () => {
        Glean.webNotificationPermission.promptInteraction.record({
          site_category: siteCategory,
          action: "allow",
          is_persistent: true,
        });
      },
    };
    let actions = [allowAction];

    let isBrowserPrivate = lazy.PrivateBrowsingUtils.isBrowserPrivate(
      this.browser
    );
    let blockAction = {
      label: isBrowserPrivate
        ? lazy.gBrowserBundle.GetStringFromName("webNotifications.block")
        : lazy.gBrowserBundle.GetStringFromName("webNotifications.alwaysBlock"),
      accessKey: isBrowserPrivate
        ? lazy.gBrowserBundle.GetStringFromName(
            "webNotifications.block.accesskey"
          )
        : lazy.gBrowserBundle.GetStringFromName(
            "webNotifications.alwaysBlock.accesskey"
          ),
      action: lazy.SitePermissions.BLOCK,
      scope: isBrowserPrivate
        ? lazy.SitePermissions.SCOPE_SESSION
        : lazy.SitePermissions.SCOPE_PERSISTENT,
      callback: () => {
        Glean.webNotificationPermission.promptInteraction.record({
          site_category: siteCategory,
          action: "block",
          is_persistent: true,
        });
      },
    };
    actions.push(blockAction);

    this.#applyTreatmentLabels(allowAction, blockAction);

    return actions;
  }

  get postPromptActions() {
    // Capture the site category now, while this.principal is still valid
    let siteCategory = lazy.SiteCategory.getCategory(this.principal);

    let allowAction = {
      label: lazy.gBrowserBundle.GetStringFromName("webNotifications.allow2"),
      accessKey: lazy.gBrowserBundle.GetStringFromName(
        "webNotifications.allow2.accesskey"
      ),
      action: lazy.SitePermissions.ALLOW,
      callback: () => {
        Glean.webNotificationPermission.promptInteraction.record({
          site_category: siteCategory,
          action: "allow",
          is_persistent: true,
        });
      },
    };
    let actions = [allowAction];

    let isBrowserPrivate = lazy.PrivateBrowsingUtils.isBrowserPrivate(
      this.browser
    );
    let blockAction = {
      label: isBrowserPrivate
        ? lazy.gBrowserBundle.GetStringFromName("webNotifications.block")
        : lazy.gBrowserBundle.GetStringFromName("webNotifications.alwaysBlock"),
      accessKey: isBrowserPrivate
        ? lazy.gBrowserBundle.GetStringFromName(
            "webNotifications.block.accesskey"
          )
        : lazy.gBrowserBundle.GetStringFromName(
            "webNotifications.alwaysBlock.accesskey"
          ),
      action: lazy.SitePermissions.BLOCK,
      callback: () => {
        Glean.webNotificationPermission.promptInteraction.record({
          site_category: siteCategory,
          action: "block",
          is_persistent: true,
        });
      },
    };
    actions.push(blockAction);

    this.#applyTreatmentLabels(allowAction, blockAction);

    return actions;
  }

  /**
   * Resolve the Nimbus enrollment for this prompt, distinguishing two concerns:
   *
   *  - this.#exposureQualified: the user is enrolled (experiment or rollout) AND
   *    the activationTargeting expression passed. This drives the exposure event
   *    and fires for BOTH branches, including an empty-content control branch, so
   *    control and treatment are comparable among the same qualified population.
   *
   *  - this.#treatment: the copy/logo overrides to render, set only when the
   *    qualified enrollment actually provides content. A qualified control branch
   *    with no content stays null and renders the default prompt.
   *
   * Both stay falsy for unenrolled users or when targeting fails (default prompt,
   * no exposure). The async targeting eval is why prompt() awaits this before
   * super.prompt() reads the synchronous message/promptActions/popupOptions.
   */
  async #resolveTreatment() {
    this.#treatment = null;
    this.#exposureQualified = false;

    // Bail unless the user is enrolled in an experiment or rollout for this
    // feature: getEnrollmentMetadata() returns null otherwise. This is a cheap
    // synchronous check that keeps resolution a no-op (default prompt, no
    // exposure) for everyone not in the experiment.
    let feature = lazy.NimbusFeatures[lazy.PERMISSION_UI_FEATURE_ID];
    if (!feature.getEnrollmentMetadata()) {
      return;
    }

    let vars = feature.getAllVariables() ?? {};

    let applies = await lazy.evalPermissionPromptTargeting(
      vars.activationTargeting,
      lazy.SiteCategory.getCategory(this.principal)
    );
    if (!applies) {
      return;
    }
    this.#exposureQualified = true;

    let hasContent =
      vars.headline ||
      vars.body ||
      vars.primaryCtaLabel ||
      vars.secondaryCtaLabel ||
      vars.logoUrl ||
      vars.useSiteFavicon ||
      vars.headlineL10nId ||
      vars.bodyL10nId ||
      vars.primaryCtaLabelL10nId ||
      vars.secondaryCtaLabelL10nId;
    if (hasContent) {
      this.#treatment = {
        ...vars,
        logoUrl: await this.#resolveLogoUrl(vars),
      };
    }
  }

  /**
   * Resolve the prompt icon from the treatment, in this order: a valid Nimbus
   * logoUrl (chrome:// or resource:// only), else the requesting site's
   * own favicon when useSiteFavicon is set, else null (default icon).
   *
   * The favicon prefers the persistent page-icon: cache when the site has a
   * cached favicon (the common case, and the same lookup the site permissions
   * manager uses). It only falls back to the tab's live in-memory icon when
   * nothing is cached such as a first-time private-browsing visit, where the
   * cache is empty but the tab has already loaded the icon. If neither resolves,
   * returns null and the default favicon is shown.
   *
   * @param {object} vars - The Nimbus feature variables.
   * @returns {Promise<?string>} The icon URL, or null to show the default icon.
   */
  async #resolveLogoUrl(vars) {
    if (lazy.isValidLogoUrl(vars.logoUrl)) {
      return vars.logoUrl;
    }
    if (!vars.useSiteFavicon) {
      return null;
    }

    // Primary: page-icon: keyed on the requesting page's URI spec. Only use it
    // when a favicon is actually cached, so we don't render the default globe
    // when the live tab icon (below) could show the real one.
    let pageURI = this.principal.URI;
    let cachedFavicon = await lazy.PlacesUtils.favicons
      .getFaviconForPage(pageURI)
      .catch(() => null);
    if (cachedFavicon) {
      return `page-icon:${pageURI.spec}`;
    }

    // Fallback: the tab's already-loaded icon, populated on page load even when
    // the persistent cache is empty (first-time private visit). setIcon() only
    // stores local-scheme URLs (data:/chrome:/resource:) in mIconURL, so this
    // never makes the consent prompt fetch a remote URL. Require the requester
    // to be same-origin with the tab's top-level document: mIconURL is that
    // top-level icon, and we must not render one origin's icon next to another
    // origin's name in a consent prompt.
    if (
      this.browser.mIconURL &&
      this.browser.contentPrincipal.equals(this.principal)
    ) {
      return this.browser.mIconURL;
    }
    return null;
  }

  /**
   * Resolve the treatment here (rather than in prompt()) so it runs only after
   * the base prompt() has decided the prompt will actually be shown, and before
   * it reads the synchronous message/promptActions/popupOptions/hintText
   * getters. Any failure falls back to the default prompt; we must not let a
   * rejection escape, since prompt() is ultimately called fire-and-forget from
   * ContentPermissionPrompt.
   */
  async onBeforeShowAsync() {
    try {
      await this.#resolveTreatment();
    } catch (e) {
      console.error("Failed to resolve notification prompt treatment:", e);
      this.#treatment = null;
      this.#exposureQualified = false;
    }
  }

  prompt() {
    // Capture site category early (before this.principal becomes invalid)
    let siteCategory = lazy.SiteCategory.getCategory(this.principal);

    // Determine the blocking reason (check most specific to least specific)
    let blockReason = null;

    // Only check for pre-existing BLOCK if the parent hasn't already handled this
    // The parent's prompt() will cancel immediately if state is BLOCK, so we need
    // to detect this AFTER the parent runs, not before

    // Check for no user gesture (will be blocked by parent)
    if (
      this.requiresUserInput &&
      !this.request.hasValidTransientUserGestureActivation
    ) {
      blockReason = "no_user_gesture";

      Glean.webNotificationPermission.promptBlocked.record({
        site_category: siteCategory,
        reason: blockReason,
      });
    }

    // Call parent prompt()
    return super.prompt();
  }

  postPrompt() {
    Glean.webNotificationPermission.iconShown.record({
      site_category: lazy.SiteCategory.getCategory(this.principal),
    });

    if (this.#exposureQualified) {
      lazy.NimbusFeatures[lazy.PERMISSION_UI_FEATURE_ID].recordExposureEvent({
        once: true,
      });
    }

    return super.postPrompt();
  }

  onShown() {
    // Determine if this was triggered by icon click or script
    // If requiresUserInput is enabled and the request lacks a user gesture,
    // but we're showing the prompt anyway, it means the user clicked the icon
    let trigger = "script";
    if (
      this.requiresUserInput &&
      !this.request.hasValidTransientUserGestureActivation
    ) {
      trigger = "icon_click";

      // Record icon_clicked telemetry when user clicks the post-prompt icon
      Glean.webNotificationPermission.iconClicked.record({
        site_category: lazy.SiteCategory.getCategory(this.principal),
      });
    }

    Glean.webNotificationPermission.promptShown.record({
      site_category: lazy.SiteCategory.getCategory(this.principal),
      trigger,
    });

    // Record the Nimbus exposure for any enrolled user who passed targeting and
    // is being shown the prompt, regardless of whether content was overridden.
    // This fires for the empty-content control branch too, so it is comparable
    // to the treatment branch. onShown covers the modal; the post-prompt records
    // its own exposure in postPrompt(). onShown can re-fire for an already-shown
    // notification (e.g. on tab switch), so once: true dedupes to a single
    // exposure per enrollment.
    if (this.#exposureQualified) {
      lazy.NimbusFeatures[lazy.PERMISSION_UI_FEATURE_ID].recordExposureEvent({
        once: true,
      });
    }
  }
}

/**
 * Creates a PermissionPrompt for a nsIContentPermissionRequest for
 * the Local Network Access.
 *
 * @param request (nsIContentPermissionRequest)
 *        The request for a permission from content.
 */
class LocalNetworkPermissionPrompt extends LNAPermissionPromptBase {
  get type() {
    return "local-network";
  }

  get permissionKey() {
    return "local-network";
  }

  get popupOptions() {
    let options = {
      learnMoreURL: Services.urlFormatter.formatURLPref(
        "browser.lna.warning.infoURL"
      ),
      displayURI: false,
      name: this.getPrincipalName(),
    };

    // Don't offer "always remember" action in PB mode
    options.checkbox = {
      show: !lazy.PrivateBrowsingUtils.isWindowPrivate(
        this.browser.documentGlobal
      ),
    };

    if (options.checkbox.show) {
      options.checkbox.label = lazy.gBrowserBundle.GetStringFromName(
        "localNetwork.remember2"
      );
    }

    return options;
  }

  get notificationID() {
    return "local-network";
  }

  get anchorID() {
    return "local-network-notification-icon";
  }

  get message() {
    return lazy.gBrowserBundle.formatStringFromName(
      "localNetwork.allowWithSite2",
      ["<>"]
    );
  }

  get promptActions() {
    return [
      {
        label: lazy.gBrowserBundle.GetStringFromName("localNetwork.allowLabel"),
        accessKey: lazy.gBrowserBundle.GetStringFromName(
          "localNetwork.allow.accesskey"
        ),
        action: lazy.SitePermissions.ALLOW,
      },
      {
        label: lazy.gBrowserBundle.GetStringFromName("localNetwork.blockLabel"),
        accessKey: lazy.gBrowserBundle.GetStringFromName(
          "localNetwork.block.accesskey"
        ),
        action: lazy.SitePermissions.BLOCK,
      },
    ];
  }
}
/**
 * Creates a PermissionPrompt for a nsIContentPermissionRequest for
 * the persistent-storage API.
 *
 * @param request (nsIContentPermissionRequest)
 *        The request for a permission from content.
 */
class PersistentStoragePermissionPrompt extends PermissionPromptForRequest {
  constructor(request) {
    super();
    this.request = request;
  }

  get type() {
    return "persistent-storage";
  }

  get permissionKey() {
    return "persistent-storage";
  }

  get popupOptions() {
    let learnMoreURL =
      Services.urlFormatter.formatURLPref("app.support.baseURL") +
      "storage-permissions";
    let options = {
      learnMoreURL,
      displayURI: false,
      name: this.getPrincipalName(),
    };

    options.checkbox = {
      show: !lazy.PrivateBrowsingUtils.isWindowPrivate(
        this.browser.documentGlobal
      ),
    };

    if (options.checkbox.show) {
      options.checkbox.label = lazy.gFluentStrings.formatValueSync(
        "perm-persistent-storage-remember"
      );
    }

    return options;
  }

  get notificationID() {
    return "persistent-storage";
  }

  get anchorID() {
    return "persistent-storage-notification-icon";
  }

  get message() {
    return lazy.gBrowserBundle.formatStringFromName(
      "persistentStorage.allowWithSite2",
      ["<>"]
    );
  }

  get promptActions() {
    return [
      {
        label: lazy.gBrowserBundle.GetStringFromName("persistentStorage.allow"),
        accessKey: lazy.gBrowserBundle.GetStringFromName(
          "persistentStorage.allow.accesskey"
        ),
        action: Ci.nsIPermissionManager.ALLOW_ACTION,
        scope: lazy.SitePermissions.SCOPE_PERSISTENT,
      },
      {
        label: lazy.gBrowserBundle.GetStringFromName(
          "persistentStorage.block.label"
        ),
        accessKey: lazy.gBrowserBundle.GetStringFromName(
          "persistentStorage.block.accesskey"
        ),
        action: lazy.SitePermissions.BLOCK,
      },
    ];
  }
}

/**
 * Creates a PermissionPrompt for a nsIContentPermissionRequest for
 * the WebMIDI API.
 *
 * @param request (nsIContentPermissionRequest)
 *        The request for a permission from content.
 */
class MIDIPermissionPrompt extends SitePermsAddonInstallRequest {
  constructor(request) {
    super();
    this.request = request;
    let types = request.types.QueryInterface(Ci.nsIArray);
    let perm = types.queryElementAt(0, Ci.nsIContentPermissionType);
    this.isSysexPerm =
      !!perm.options.length &&
      perm.options.queryElementAt(0, Ci.nsISupportsString) == "sysex";
    this.permName = "midi";
    if (this.isSysexPerm) {
      this.permName = "midi-sysex";
    }
  }

  get type() {
    return "midi";
  }

  get permissionKey() {
    return this.permName;
  }

  get popupOptions() {
    // TODO (bug 1433235) We need a security/permissions explanation URL for this
    let options = {
      displayURI: false,
      name: this.getPrincipalName(),
    };

    // Don't offer "always remember" action in PB mode
    options.checkbox = {
      show: !lazy.PrivateBrowsingUtils.isWindowPrivate(
        this.browser.documentGlobal
      ),
    };

    if (options.checkbox.show) {
      options.checkbox.label =
        lazy.gBrowserBundle.GetStringFromName("midi.remember");
    }

    return options;
  }

  get notificationID() {
    return "midi";
  }

  get anchorID() {
    return "midi-notification-icon";
  }

  get message() {
    let message;
    if (this.principal.schemeIs("file")) {
      if (this.isSysexPerm) {
        message = lazy.gBrowserBundle.GetStringFromName(
          "midi.shareSysexWithFile"
        );
      } else {
        message = lazy.gBrowserBundle.GetStringFromName("midi.shareWithFile");
      }
    } else if (this.isSysexPerm) {
      message = lazy.gBrowserBundle.formatStringFromName(
        "midi.shareSysexWithSite",
        ["<>"]
      );
    } else {
      message = lazy.gBrowserBundle.formatStringFromName("midi.shareWithSite", [
        "<>",
      ]);
    }
    return message;
  }

  get promptActions() {
    return [
      {
        label: lazy.gBrowserBundle.GetStringFromName("midi.allow.label"),
        accessKey: lazy.gBrowserBundle.GetStringFromName(
          "midi.allow.accesskey"
        ),
        action: Ci.nsIPermissionManager.ALLOW_ACTION,
      },
      {
        label: lazy.gBrowserBundle.GetStringFromName("midi.block.label"),
        accessKey: lazy.gBrowserBundle.GetStringFromName(
          "midi.block.accesskey"
        ),
        action: Ci.nsIPermissionManager.DENY_ACTION,
      },
    ];
  }

  /**
   * @override
   * @param {Components.Exception} err
   * @returns {string}
   */
  getInstallErrorMessage(err) {
    return `WebMIDI access request was denied: ❝${err.message}❞. See https://developer.mozilla.org/docs/Web/API/Navigator/requestMIDIAccess for more information`;
  }
}

/**
 * Creates a PermissionPrompt for a nsIContentPermissionRequest for
 * the WebSerial API.
 *
 * @param request (nsIContentPermissionRequest)
 *        The request for a permission from content.
 */
class SerialPermissionPrompt extends SitePermsAddonInstallRequest {
  constructor(request) {
    super();
    this.request = request;
    this.permName = "serial";
  }

  get type() {
    return "serial";
  }

  get permissionKey() {
    return this.permName;
  }

  get popupOptions() {
    return {
      displayURI: false,
      name: this.getPrincipalName(),
      // Don't offer "always remember" action
      checkbox: false,
    };
  }

  _populateDeviceList(notification) {
    let document = notification.browser.ownerDocument;
    let menulist = document.getElementById("webSerial-selectPort-menulist");
    let menupopup = document.getElementById("webSerial-selectPort-menupopup");
    let noPortsMsg = document.getElementById("webSerial-no-ports-available");

    if (!menulist || !menupopup || !noPortsMsg) {
      console.error("[WebSerial] Failed to find required UI elements");
      return;
    }

    // Clear existing items
    while (menupopup.firstChild) {
      menupopup.firstChild.remove();
    }

    // Get port data from the permission request's options array
    let types = this.request.types.QueryInterface(Ci.nsIArray);
    let perm = types.queryElementAt(0, Ci.nsIContentPermissionType);
    let options = perm.options.QueryInterface(Ci.nsIArray);

    let ports = [];
    this._autoselect = false;
    for (let i = 0; i < options.length; i++) {
      let optionStr = options.queryElementAt(i, Ci.nsISupportsString).data;
      let parsed = JSON.parse(optionStr);
      if (parsed.__autoselect__) {
        this._autoselect = true;
        continue;
      }
      ports.push(parsed);
    }

    // Handle no devices case
    const anyPorts = ports.length !== 0;
    menulist.hidden = !anyPorts;
    noPortsMsg.hidden = anyPorts;
    notification.mainAction.disabled = !anyPorts;
    if (!anyPorts) {
      return;
    }

    // Populate menulist with devices
    for (let i = 0; i < ports.length; i++) {
      let menuitem = document.createXULElement("menuitem");
      let port = ports[i];
      let label = this._getDeviceDisplayName(port);
      menuitem.setAttribute("label", label);
      menuitem.setAttribute("value", i.toString());
      menupopup.appendChild(menuitem);
    }

    // Select first item by default
    menulist.selectedIndex = 0;
  }

  _getDeviceDisplayName(port) {
    if (port.friendlyName?.length) {
      return port.friendlyName;
    }
    let path = port.path;
    let vid = port.usbVendorId;
    let pid = port.usbProductId;

    // If USB device, show VID/PID
    if (vid != null && pid != null && vid !== 0 && pid !== 0) {
      let vidHex = vid.toString(16).padStart(4, "0");
      let pidHex = pid.toString(16).padStart(4, "0");
      return `${path} (${vidHex}:${pidHex})`;
    }

    return path;
  }

  async prompt() {
    // For localhost or file or when addon provider is disabled, show device picker directly
    if (
      this.principal.isLoopbackHost ||
      this.principal.schemeIs("file") ||
      !lazy.sitePermsAddonsProviderEnabled
    ) {
      this._showDevicePicker();
      return;
    }

    let hasAddon =
      Services.perms.testPermissionFromPrincipal(
        this.principal,
        this.permName
      ) === Services.perms.ALLOW_ACTION;

    if (hasAddon || !lazy.webserialGated) {
      // Addon already installed, just show device picker
      this._showDevicePicker();
      return;
    }

    // Need to install addon first
    this.installSitePermAddon(
      () => {
        // Addon installed successfully, now show device picker
        this._showDevicePicker();
      },
      () => {
        this.cancel();
      }
    );
  }

  _showDevicePicker() {
    // Show device picker doorhanger
    try {
      let selectAction = {
        label: lazy.gBrowserBundle.GetStringFromName("serial.allow.label"),
        accessKey: lazy.gBrowserBundle.GetStringFromName(
          "serial.allow.accesskey"
        ),
        callback: () => {
          // Get selected device index from menulist
          let document = this.browser.ownerDocument;

          let menulist = document.getElementById(
            "webSerial-selectPort-menulist"
          );

          let selectedIndex = menulist.selectedIndex;

          // Call allow with selected device index
          // TranslateChoices expects the permission type as the key
          let choices = { serial: selectedIndex.toString() };
          this.allow(choices);
        },
      };

      let cancelAction = {
        label: lazy.gBrowserBundle.GetStringFromName("serial.block.label"),
        accessKey: lazy.gBrowserBundle.GetStringFromName(
          "serial.block.accesskey"
        ),
        callback: () => {
          this.cancel();
        },
      };

      let notification;
      let promptInstance = this;
      let options = {
        displayURI: false,
        name: this.getPrincipalName(),
        persistent: true,
        hideClose: true,
        popupIconURL: "chrome://browser/skin/notification-icons/serial.svg",
        eventCallback(topic) {
          if (topic === "showing") {
            try {
              promptInstance._populateDeviceList(this);

              // Auto-select first port if testing autoselect is enabled
              if (promptInstance._autoselect) {
                // Auto-select first port after short delay to ensure UI is ready
                lazy.setTimeout(() => {
                  let document = promptInstance.browser.ownerDocument;
                  let menulist = document.getElementById(
                    "webSerial-selectPort-menulist"
                  );
                  if (menulist && menulist.itemCount > 0) {
                    selectAction.callback();
                  } else {
                    // No items in list, so auto-cancel
                    cancelAction.callback();
                  }
                  // We're calling these callbacks directly, so remove the notification
                  notification.remove();
                }, 100);
              }
            } catch (ex) {
              console.error("[WebSerial] Failed to populate device list:", ex);
            }
          } else if (topic === "removed") {
            // PopupNotifications removes the notification on location change
            // and other non-user-driven teardown paths. Cancel the underlying
            // request so the content-process requestPort() promise settles
            // instead of waiting for the destructor fallback.
            promptInstance.cancel();
          }
        },
      };

      let chromeWin = this.browser.documentGlobal;
      notification = chromeWin.PopupNotifications.show(
        this.browser,
        this.notificationID,
        this.message,
        this.anchorID,
        selectAction,
        [cancelAction],
        options
      );
    } catch (ex) {
      console.error("[WebSerial] Failed to show device picker:", ex);
      this.cancel();
    }
  }

  get notificationID() {
    return "webSerial-choosePort";
  }

  get anchorID() {
    return "serial-notification-icon";
  }

  get message() {
    let message;
    if (this.principal.schemeIs("file")) {
      message = lazy.gBrowserBundle.GetStringFromName("serial.shareWithFile");
    } else {
      message = lazy.gBrowserBundle.formatStringFromName(
        "serial.shareWithSite",
        ["<>"]
      );
    }
    return message;
  }

  /**
   * @override
   * @param {Components.Exception} err
   * @returns {string}
   */
  getInstallErrorMessage(err) {
    return `WebSerial access request was denied: ❝${err.message}❞. See https://developer.mozilla.org/en-US/docs/Web/API/Serial/requestPort for more information`;
  }
}

class StorageAccessPermissionPrompt extends PermissionPromptForRequest {
  #permissionKey;

  constructor(request) {
    super();
    this.request = request;
    this.siteOption = null;
    this.#permissionKey = `3rdPartyStorage${lazy.SitePermissions.PERM_KEY_DELIMITER}${this.principal.origin}`;

    let types = this.request.types.QueryInterface(Ci.nsIArray);
    let perm = types.queryElementAt(0, Ci.nsIContentPermissionType);
    let options = perm.options.QueryInterface(Ci.nsIArray);
    // If we have an option, the permission request is different in some way.
    // We may be in a call from a frame-scoped request, which means that the
    // embedding principal is not the current top-level or the permission key is
    // different.
    if (options.length != 2) {
      return;
    }

    let topLevelOption = options.queryElementAt(0, Ci.nsISupportsString).data;
    if (topLevelOption) {
      this.siteOption = topLevelOption;
    }
    let frameOption = options.queryElementAt(1, Ci.nsISupportsString).data;
    if (frameOption) {
      // We replace the permission key with a frame-specific one that only has a site after the delimiter
      this.#permissionKey = `3rdPartyFrameStorage${lazy.SitePermissions.PERM_KEY_DELIMITER}${this.principal.siteOrigin}`;
    }
  }

  get usePermissionManager() {
    return false;
  }

  get type() {
    return "storage-access";
  }

  get permissionKey() {
    // Make sure this name is unique per each third-party tracker
    return this.#permissionKey;
  }

  get temporaryPermissionURI() {
    if (this.siteOption) {
      return Services.io.newURI(this.siteOption);
    }
    return undefined;
  }

  prettifyHostPort(hostport) {
    let [host, port] = hostport.split(":");
    host = lazy.IDNService.convertToDisplayIDN(host);
    if (port) {
      return `${host}:${port}`;
    }
    return host;
  }

  get popupOptions() {
    let learnMoreURL =
      Services.urlFormatter.formatURLPref("app.support.baseURL") +
      "third-party-cookies";
    let hostPort = this.prettifyHostPort(this.principal.hostPort);
    let hintText = lazy.gBrowserBundle.formatStringFromName(
      "storageAccess1.hintText",
      [hostPort]
    );
    return {
      learnMoreURL,
      displayURI: false,
      hintText,
      escAction: "secondarybuttoncommand",
    };
  }

  get notificationID() {
    return "storage-access";
  }

  get anchorID() {
    return "storage-access-notification-icon";
  }

  get message() {
    let embeddingHost = this.topLevelPrincipal.host;

    if (this.siteOption) {
      embeddingHost = this.siteOption.split("://").at(-1);
    }

    return lazy.gBrowserBundle.formatStringFromName("storageAccess4.message", [
      this.prettifyHostPort(this.principal.hostPort),
      this.prettifyHostPort(embeddingHost),
    ]);
  }

  get promptActions() {
    let self = this;

    return [
      {
        label: lazy.gBrowserBundle.GetStringFromName(
          "storageAccess1.Allow.label"
        ),
        accessKey: lazy.gBrowserBundle.GetStringFromName(
          "storageAccess1.Allow.accesskey"
        ),
        action: Ci.nsIPermissionManager.ALLOW_ACTION,
        callback() {
          self.allow({ "storage-access": "allow" });
        },
      },
      {
        label: lazy.gBrowserBundle.GetStringFromName(
          "storageAccess1.DontAllow.label"
        ),
        accessKey: lazy.gBrowserBundle.GetStringFromName(
          "storageAccess1.DontAllow.accesskey"
        ),
        action: Ci.nsIPermissionManager.DENY_ACTION,
        callback() {
          self.cancel();
        },
      },
    ];
  }

  get topLevelPrincipal() {
    return this.request.topLevelPrincipal;
  }
}

// How long the success message stays up before the prompt goes away by itself.
// WCAG 2.2 "Timing Adjustable" asks for at least five seconds for a message
// that disappears on its own. A failed download is only ever surfaced here, so
// that prompt stays up until the user dismisses it.
const MODEL_DOWNLOAD_COMPLETE_TIMEOUT_MS = 5000;
const MODEL_DOWNLOAD_NOTIFICATION_ID = "speech-recognition-model-download";
const MODEL_DOWNLOAD_PROGRESS_NOTIFICATION_ID =
  "speech-recognition-model-download-progress";
const MODEL_DOWNLOAD_PROGRESS_TOPIC = "ml-model-download-progress";

class SpeechRecognitionModelDownloadPermissionPrompt extends PermissionPromptForRequest {
  #sizeMB;
  #progressToken;
  #cancelMessage;
  #okMessage;
  #downloadStartedAt;
  #notification;
  #progress;
  #renderedPercent;
  #requestSettled = false;
  #observingProgress = false;
  // Feeds DownloadUtils' hysteresis on the time estimate, so it does not jump
  // around between updates. Infinity means "no estimate yet".
  #lastSec = Infinity;

  constructor(request) {
    super();
    this.request = request;
    let types = request.types.QueryInterface(Ci.nsIArray);
    let perm = types.queryElementAt(0, Ci.nsIContentPermissionType);
    this.#sizeMB = perm.options.queryElementAt(0, Ci.nsISupportsString).data;
    this.#progressToken = perm.options.queryElementAt(
      1,
      Ci.nsISupportsString
    ).data;
  }

  get type() {
    return MODEL_DOWNLOAD_NOTIFICATION_ID;
  }

  // No permissionKey on purpose: consenting to a download is a one-shot
  // decision, never written to the permission manager, so a grant cannot be
  // persisted and there is no "Remember this decision" checkbox.
  get popupOptions() {
    return {
      displayURI: false,
      checkbox: { show: false },
    };
  }

  get notificationID() {
    return MODEL_DOWNLOAD_NOTIFICATION_ID;
  }

  get anchorID() {
    return "default-notification-icon";
  }

  get message() {
    return lazy.gFluentStrings.formatValueSync(
      "speech-recognition-model-download-message",
      { sizeMB: this.#sizeMB }
    );
  }

  get promptActions() {
    let [allowMessage, notNowMessage, cancelMessage, okMessage] =
      lazy.gFluentStrings
        .formatMessagesSync([
          { id: "speech-recognition-model-download-allow" },
          { id: "speech-recognition-model-download-not-now" },
          { id: "speech-recognition-model-download-cancel" },
          { id: "speech-recognition-model-download-ok" },
        ])
        .map(msg =>
          msg.attributes.reduce(
            (acc, { name, value }) => ({ ...acc, [name]: value }),
            {}
          )
        );
    this.#cancelMessage = cancelMessage;
    this.#okMessage = okMessage;

    return [
      {
        label: allowMessage.label,
        accessKey: allowMessage.accesskey,
        action: lazy.SitePermissions.ALLOW,
        callback: () => {
          this.#showProgress(allowMessage);
          this.allow();
        },
      },
      {
        label: notNowMessage.label,
        accessKey: notNowMessage.accesskey,
        action: lazy.SitePermissions.BLOCK,
        callback: () => {
          this.cancel();
        },
      },
    ];
  }

  allow(choices) {
    if (this.#requestSettled) {
      return;
    }
    this.#requestSettled = true;
    super.allow(choices);
  }

  cancel() {
    if (this.#requestSettled) {
      return;
    }
    this.#requestSettled = true;
    super.cancel();
  }

  observe(subject, topic) {
    if (topic != MODEL_DOWNLOAD_PROGRESS_TOPIC) {
      return;
    }

    let props = subject.QueryInterface(Ci.nsIPropertyBag2);

    // The topic carries the progress of every model download in the browser,
    // not just ours: the token tells them apart.
    if (props.getPropertyAsAString("token") != this.#progressToken) {
      return;
    }

    this.#updateProgress({
      progress: props.getPropertyAsInt32("progress"),
      totalLoaded: props.getPropertyAsInt64("totalLoaded"),
      total: props.getPropertyAsInt64("total"),
      done: props.getPropertyAsBool("done"),
      ok: props.getPropertyAsBool("ok"),
    });
  }

  /**
   * The <popupnotification> element showing this progress notification, or null.
   */
  #notificationElement() {
    let browser = this.#notification?.browser;
    let notificationEl = browser?.documentGlobal?.document.getElementById(
      `${MODEL_DOWNLOAD_PROGRESS_NOTIFICATION_ID}-notification`
    );
    return notificationEl?.notification == this.#notification
      ? notificationEl
      : null;
  }

  #showProgress(allowMessage) {
    let browser = this.browser;
    let popupNotifications = browser.documentGlobal.PopupNotifications;
    let message = lazy.gFluentStrings.formatValueSync(
      "speech-recognition-model-download-progress-message"
    );
    let mainAction = {
      label: allowMessage.label,
      accessKey: allowMessage.accesskey,
      disabled: true,
      callback: () => {},
    };
    let cancelAction = {
      label: this.#cancelMessage.label,
      accessKey: this.#cancelMessage.accesskey,
      disableSecurityDelay: true,
      callback: () => {
        if (this.#observingProgress) {
          Cc["@mozilla.org/ml-modelhub;1"]
            .getService(Ci.nsIMLModelHub)
            .cancelDownload(this.#progressToken);
        }
      },
    };

    this.#downloadStartedAt = ChromeUtils.now();
    this.#progress = { progress: 0, totalLoaded: 0, total: 0 };
    this.#renderedPercent = null;
    this.#lastSec = Infinity;
    this.#notification = popupNotifications.show(
      browser,
      MODEL_DOWNLOAD_PROGRESS_NOTIFICATION_ID,
      message,
      this.anchorID,
      mainAction,
      [cancelAction],
      {
        displayURI: false,
        escAction: "secondarybuttoncommand",
        hideClose: true,
        persistent: true,
        eventCallback: topic => {
          if (topic == "swapping") {
            return true;
          }
          if (topic == "shown") {
            this.#renderProgressUI(true);
          }
          if (topic == "removed") {
            this.#stopObservingProgress();
            this.#notification = null;
          }
          return false;
        },
      }
    );

    this.#renderProgressUI(true);

    Services.obs.addObserver(this, MODEL_DOWNLOAD_PROGRESS_TOPIC);
    this.#observingProgress = true;
  }

  /**
   * Shows the progress content. Applied again if the element is re-rendered
   * while the download is running, for example because the tab moved to another
   * window, which uses that window's element.
   */
  #applyProgressUI() {
    let notificationEl = this.#notificationElement();
    if (!notificationEl) {
      return;
    }

    notificationEl.toggleAttribute("model-download-in-progress", true);
    notificationEl.querySelector(
      "#speech-recognition-model-download-progress-content"
    ).hidden = false;
  }

  /**
   * Relabels the secondary button. _refreshPanel() rebuilds the button from
   * the action, so the action has to change; the attributes update the button
   * that is already rendered.
   */
  #setSecondaryLabel(message) {
    let secondaryAction = this.#notification.secondaryActions[0];
    secondaryAction.label = message.label;
    secondaryAction.accessKey = message.accesskey;
    let notificationEl = this.#notificationElement();
    if (!notificationEl) {
      return;
    }
    notificationEl.setAttribute("secondarybuttonlabel", secondaryAction.label);
    notificationEl.setAttribute(
      "secondarybuttonaccesskey",
      secondaryAction.accessKey
    );
  }

  #resetProgressUI() {
    let notificationEl = this.#notificationElement();
    if (!notificationEl) {
      return;
    }

    notificationEl.toggleAttribute("model-download-in-progress", false);
    notificationEl.toggleAttribute("model-download-succeeded", false);
    notificationEl.toggleAttribute("model-download-failed", false);
    notificationEl.querySelector(
      "#speech-recognition-model-download-progress-content"
    ).hidden = true;
    notificationEl.querySelector(
      "#speech-recognition-model-download-progress"
    ).value = 0;
    notificationEl.querySelector(
      "#speech-recognition-model-download-progress-status"
    ).textContent = "";
  }

  #updateProgress(progress) {
    if (!this.#notification) {
      return;
    }
    this.#progress = progress;

    if (progress.done) {
      this.#stopObservingProgress();

      this.#notification.message = lazy.gFluentStrings.formatValueSync(
        progress.ok
          ? "speech-recognition-model-download-complete-message"
          : "speech-recognition-model-download-failed-message"
      );
      // There is nothing left to accept or cancel: the secondary button becomes
      // the acknowledgement. Escape uses that same action.
      this.#setSecondaryLabel(this.#okMessage);

      if (progress.ok) {
        lazy.setTimeout(
          () => this.#notification?.remove(),
          MODEL_DOWNLOAD_COMPLETE_TIMEOUT_MS
        );
      }
    }

    this.#renderProgressUI();
  }

  #renderProgressUI(force = false) {
    let notificationEl = this.#notificationElement();
    if (!notificationEl) {
      return;
    }

    if (force || !notificationEl.hasAttribute("model-download-in-progress")) {
      this.#resetProgressUI();
      this.#applyProgressUI();
      this.#renderedPercent = null;
      this.#lastSec = Infinity;
    }

    // The download reports progress for every chunk it receives, which is far
    // more often than the whole percent the bar is drawn at changes: only touch
    // the DOM when it does.
    let percent = Math.floor(
      Math.max(0, Math.min(100, this.#progress.progress ?? 0))
    );
    if (percent == this.#renderedPercent && !this.#progress.done) {
      return;
    }
    this.#renderedPercent = percent;

    let progressEl = notificationEl.querySelector(
      "#speech-recognition-model-download-progress"
    );
    let statusEl = notificationEl.querySelector(
      "#speech-recognition-model-download-progress-status"
    );

    progressEl.value = percent;
    this.#setProgressStatus(statusEl, this.#progress);

    if (!this.#progress.done) {
      return;
    }

    notificationEl.setAttribute("label", this.#notification.message);
    progressEl.value = this.#progress.ok ? 100 : percent;
    statusEl.textContent = "";

    notificationEl.toggleAttribute(
      this.#progress.ok ? "model-download-succeeded" : "model-download-failed",
      true
    );
  }

  /**
   * Writes the line below the progress bar, in the same shape the downloads
   * panel uses: "7s left — 59 of 141 MB (12.4 MB/sec)".
   */
  #setProgressStatus(statusEl, progress) {
    if (progress.total <= 0) {
      statusEl.textContent = "";
      return;
    }

    // Averaged over the download rather than taken between two notifications:
    // chunks arrive far too often for an instantaneous rate to sit still.
    let elapsedSeconds = (ChromeUtils.now() - this.#downloadStartedAt) / 1000;
    let speed = elapsedSeconds > 0 ? progress.totalLoaded / elapsedSeconds : -1;

    let [status, lastSec] = lazy.DownloadUtils.getDownloadStatus(
      progress.totalLoaded,
      progress.total,
      speed,
      this.#lastSec
    );
    this.#lastSec = lastSec;
    statusEl.textContent = status;
  }

  #stopObservingProgress() {
    if (!this.#observingProgress) {
      return;
    }
    this.#observingProgress = false;
    Services.obs.removeObserver(this, MODEL_DOWNLOAD_PROGRESS_TOPIC);
  }
}

export const PermissionUI = {
  PermissionPromptForRequest,
  GeolocationPermissionPrompt,
  XRPermissionPrompt,
  DesktopNotificationPermissionPrompt,
  PersistentStoragePermissionPrompt,
  MIDIPermissionPrompt,
  SerialPermissionPrompt,
  StorageAccessPermissionPrompt,
  LoopbackNetworkPermissionPrompt,
  LocalNetworkPermissionPrompt,
  SpeechRecognitionModelDownloadPermissionPrompt,
};
