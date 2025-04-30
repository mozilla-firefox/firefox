/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint max-len: ["error", 80] */
/* import-globals-from aboutaddonsCommon.js */
/* import-globals-from abuse-reports.js */
/* import-globals-from view-controller.js */
/* global windowRoot */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  AMBrowserExtensionsImport: "resource://gre/modules/AddonManager.sys.mjs",
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AddonRepository: "resource://gre/modules/addons/AddonRepository.sys.mjs",
  BuiltInThemes: "resource:///modules/BuiltInThemes.sys.mjs",
  ClientID: "resource://gre/modules/ClientID.sys.mjs",
  ColorwayThemeMigration:
    "resource://gre/modules/ColorwayThemeMigration.sys.mjs",
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  ExtensionCommon: "resource://gre/modules/ExtensionCommon.sys.mjs",
  ExtensionParent: "resource://gre/modules/ExtensionParent.sys.mjs",
  ExtensionPermissions: "resource://gre/modules/ExtensionPermissions.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "manifestV3enabled",
  "extensions.manifestV3.enabled"
);

const UPDATES_RECENT_TIMESPAN = 2 * 24 * 3600000; // 2 days (in milliseconds)

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "ABUSE_REPORT_ENABLED",
  "extensions.abuseReport.enabled",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "LIST_RECOMMENDATIONS_ENABLED",
  "extensions.htmlaboutaddons.recommendations.enabled",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "DATA_COLLECTION_PERMISSIONS_ENABLED",
  "extensions.dataCollectionPermissions.enabled",
  false
);

const PLUGIN_ICON_URL = "chrome://global/skin/icons/plugin.svg";
const EXTENSION_ICON_URL =
  "chrome://mozapps/skin/extensions/extensionGeneric.svg";

const PERMISSION_MASKS = {
  enable: AddonManager.PERM_CAN_ENABLE,
  "always-activate": AddonManager.PERM_CAN_ENABLE,
  disable: AddonManager.PERM_CAN_DISABLE,
  "never-activate": AddonManager.PERM_CAN_DISABLE,
  uninstall: AddonManager.PERM_CAN_UNINSTALL,
  upgrade: AddonManager.PERM_CAN_UPGRADE,
  "change-privatebrowsing": AddonManager.PERM_CAN_CHANGE_PRIVATEBROWSING_ACCESS,
};

const PREF_DISCOVERY_API_URL = "extensions.getAddons.discovery.api_url";
const PREF_THEME_RECOMMENDATION_URL =
  "extensions.recommendations.themeRecommendationUrl";
const PREF_RECOMMENDATION_HIDE_NOTICE = "extensions.recommendations.hideNotice";
const PREF_PRIVACY_POLICY_URL = "extensions.recommendations.privacyPolicyUrl";
const PREF_RECOMMENDATION_ENABLED = "browser.discovery.enabled";
const PREF_TELEMETRY_ENABLED = "datareporting.healthreport.uploadEnabled";
const PRIVATE_BROWSING_PERM_NAME = "internal:privateBrowsingAllowed";
const PRIVATE_BROWSING_PERMS = {
  permissions: [PRIVATE_BROWSING_PERM_NAME],
  origins: [],
};

const L10N_ID_MAPPING = {
  "theme-disabled-heading": "theme-disabled-heading2",
};

function getL10nIdMapping(id) {
  return L10N_ID_MAPPING[id] || id;
}

function shouldSkipAnimations() {
  return (
    document.body.hasAttribute("skip-animations") ||
    window.matchMedia("(prefers-reduced-motion: reduce)").matches
  );
}

function callListeners(name, args, listeners) {
  for (let listener of listeners) {
    try {
      if (name in listener) {
        listener[name](...args);
      }
    } catch (e) {
      Cu.reportError(e);
    }
  }
}

function getUpdateInstall(addon) {
  return (
    // Install object for a pending update.
    addon.updateInstall ||
    // Install object for a postponed upgrade (only for extensions,
    // because is the only addon type that can postpone their own
    // updates).
    (addon.type === "extension" &&
      addon.pendingUpgrade &&
      addon.pendingUpgrade.install)
  );
}

function isManualUpdate(install) {
  let isManual =
    install.existingAddon &&
    !AddonManager.shouldAutoUpdate(install.existingAddon);
  let isExtension =
    install.existingAddon && install.existingAddon.type == "extension";
  return (
    (isManual && isInState(install, "available")) ||
    (isExtension && isInState(install, "postponed"))
  );
}

const AddonManagerListenerHandler = {
  listeners: new Set(),

  addListener(listener) {
    this.listeners.add(listener);
  },

  removeListener(listener) {
    this.listeners.delete(listener);
  },

  delegateEvent(name, args) {
    callListeners(name, args, this.listeners);
  },

  startup() {
    this._listener = new Proxy(
      {},
      {
        has: () => true,
        get:
          (_, name) =>
          (...args) =>
            this.delegateEvent(name, args),
      }
    );
    AddonManager.addAddonListener(this._listener);
    AddonManager.addInstallListener(this._listener);
    AddonManager.addManagerListener(this._listener);
    this._permissionHandler = (type, data) => {
      if (type == "change-permissions") {
        this.delegateEvent("onChangePermissions", [data]);
      }
    };
    ExtensionPermissions.addListener(this._permissionHandler);
  },

  shutdown() {
    AddonManager.removeAddonListener(this._listener);
    AddonManager.removeInstallListener(this._listener);
    AddonManager.removeManagerListener(this._listener);
    ExtensionPermissions.removeListener(this._permissionHandler);
  },
};

/**
 * This object wires the AddonManager event listeners into addon-card and
 * addon-details elements rather than needing to add/remove listeners all the
 * time as the view changes.
 */
const AddonCardListenerHandler = new Proxy(
  {},
  {
    has: () => true,
    get(_, name) {
      return (...args) => {
        let elements = [];
        let addonId;

        // We expect args[0] to be of type:
        // - AddonInstall, on AddonManager install events
        // - AddonWrapper, on AddonManager addon events
        // - undefined, on AddonManager manage events
        if (args[0]) {
          addonId =
            args[0].addon?.id ||
            args[0].existingAddon?.id ||
            args[0].extensionId ||
            args[0].id;
        }

        if (addonId) {
          let cardSelector = `addon-card[addon-id="${addonId}"]`;
          elements = document.querySelectorAll(
            `${cardSelector}, ${cardSelector} addon-details`
          );
        } else if (name == "onUpdateModeChanged") {
          elements = document.querySelectorAll("addon-card");
        }

        callListeners(name, args, elements);
      };
    },
  }
);
AddonManagerListenerHandler.addListener(AddonCardListenerHandler);

function isAbuseReportSupported(addon) {
  return (
    ABUSE_REPORT_ENABLED &&
    AbuseReporter.isSupportedAddonType(addon.type) &&
    !(addon.isBuiltin || addon.isSystem)
  );
}

async function isAllowedInPrivateBrowsing(addon) {
  // Use the Promise directly so this function stays sync for the other case.
  let perms = await ExtensionPermissions.get(addon.id);
  return perms.permissions.includes(PRIVATE_BROWSING_PERM_NAME);
}

function hasPermission(addon, permission) {
  return !!(addon.permissions & PERMISSION_MASKS[permission]);
}

function isInState(install, state) {
  return install.state == AddonManager["STATE_" + state.toUpperCase()];
}

async function getAddonMessageInfo(
  addon,
  { isCardExpanded, isInDisabledSection }
) {
  const { name } = addon;
  const { STATE_BLOCKED, STATE_SOFTBLOCKED } = Ci.nsIBlocklistService;

  if (addon.blocklistState === STATE_BLOCKED) {
    let typeSuffix = addon.type === "extension" ? "extension" : "other";
    return {
      linkUrl: await addon.getBlocklistURL(),
      linkId: "details-notification-blocked-link2",
      messageId: `details-notification-hard-blocked-${typeSuffix}`,
      type: "error",
    };
  } else if (isDisabledUnsigned(addon)) {
    return {
      linkSumoPage: "unsigned-addons",
      messageId: "details-notification-unsigned-and-disabled2",
      messageArgs: { name },
      type: "error",
    };
  } else if (
    !addon.isCompatible &&
    (AddonManager.checkCompatibility ||
      addon.blocklistState !== STATE_SOFTBLOCKED)
  ) {
    return {
      // TODO: (Bug 1921870) consider adding a SUMO page.
      // NOTE: this messagebar is customized by Thunderbird to include
      // a non-SUMO link (see Bug 1921870 comment 0).
      messageId: "details-notification-incompatible2",
      messageArgs: { name, version: Services.appinfo.version },
      type: "error",
    };
  } else if (!isCorrectlySigned(addon)) {
    return {
      linkSumoPage: "unsigned-addons",
      messageId: "details-notification-unsigned2",
      messageArgs: { name },
      type: "warning",
    };
  } else if (addon.blocklistState === STATE_SOFTBLOCKED) {
    const fluentBaseId = "details-notification-soft-blocked";
    let typeSuffix = addon.type === "extension" ? "extension" : "other";
    let stateSuffix;
    // If the Addon Card is not expanded, delay changing the messagebar
    // string to when the Addon card is refreshed as part of moving
    // it between the enabled and disabled sections.
    if (isCardExpanded) {
      stateSuffix = addon.isActive ? "enabled" : "disabled";
    } else {
      stateSuffix = !isInDisabledSection ? "enabled" : "disabled";
    }
    let messageId = `${fluentBaseId}-${typeSuffix}-${stateSuffix}`;

    return {
      linkUrl: await addon.getBlocklistURL(),
      linkId: "details-notification-softblocked-link2",
      messageId,
      type: "warning",
    };
  } else if (addon.isGMPlugin && !addon.isInstalled && addon.isActive) {
    return {
      messageId: "details-notification-gmp-pending2",
      messageArgs: { name },
      type: "warning",
    };
  }
  return {};
}

function checkForUpdate(addon) {
  return new Promise(resolve => {
    let listener = {
      onUpdateAvailable(addon, install) {
        if (AddonManager.shouldAutoUpdate(addon)) {
          // Make sure that an update handler is attached to all the install
          // objects when updated xpis are going to be installed automatically.
          attachUpdateHandler(install);

          let failed = () => {
            detachUpdateHandler(install);
            install.removeListener(updateListener);
            resolve({ installed: false, pending: false, found: true });
          };
          let updateListener = {
            onDownloadFailed: failed,
            onInstallCancelled: failed,
            onInstallFailed: failed,
            onInstallEnded: () => {
              detachUpdateHandler(install);
              install.removeListener(updateListener);
              resolve({ installed: true, pending: false, found: true });
            },
            onInstallPostponed: () => {
              detachUpdateHandler(install);
              install.removeListener(updateListener);
              resolve({ installed: false, pending: true, found: true });
            },
          };
          install.addListener(updateListener);
          install.install();
        } else {
          resolve({ installed: false, pending: true, found: true });
        }
      },
      onNoUpdateAvailable() {
        resolve({ found: false });
      },
    };
    addon.findUpdates(listener, AddonManager.UPDATE_WHEN_USER_REQUESTED);
  });
}

async function checkForUpdates() {
  let addons = await AddonManager.getAddonsByTypes(null);
  addons = addons.filter(addon => hasPermission(addon, "upgrade"));
  let updates = await Promise.all(addons.map(addon => checkForUpdate(addon)));
  gViewController.notifyEMUpdateCheckFinished();
  return updates.reduce(
    (counts, update) => ({
      installed: counts.installed + (update.installed ? 1 : 0),
      pending: counts.pending + (update.pending ? 1 : 0),
      found: counts.found + (update.found ? 1 : 0),
    }),
    { installed: 0, pending: 0, found: 0 }
  );
}

// Don't change how we handle this while the page is open.
const INLINE_OPTIONS_ENABLED = Services.prefs.getBoolPref(
  "extensions.htmlaboutaddons.inline-options.enabled"
);
const OPTIONS_TYPE_MAP = {
  [AddonManager.OPTIONS_TYPE_TAB]: "tab",
  [AddonManager.OPTIONS_TYPE_INLINE_BROWSER]: INLINE_OPTIONS_ENABLED
    ? "inline"
    : "tab",
};

// Check if an add-on has the provided options type, accounting for the pref
// to disable inline options.
function getOptionsType(addon) {
  return OPTIONS_TYPE_MAP[addon.optionsType];
}

// Check whether the options page can be loaded in the current browser window.
async function isAddonOptionsUIAllowed(addon) {
  if (addon.type !== "extension" || !getOptionsType(addon)) {
    // Themes never have options pages.
    // Some plugins have preference pages, and they can always be shown.
    // Extensions do not need to be checked if they do not have options pages.
    return true;
  }
  if (!PrivateBrowsingUtils.isContentWindowPrivate(window)) {
    return true;
  }
  if (addon.incognito === "not_allowed") {
    return false;
  }
  // The current page is in a private browsing window, and the add-on does not
  // have the permission to access private browsing windows. Block access.
  return (
    // Note: This function is async because isAllowedInPrivateBrowsing is async.
    isAllowedInPrivateBrowsing(addon)
  );
}

let _templates = {};

/**
 * Import a template from the main document.
 */
function importTemplate(name) {
  if (!_templates.hasOwnProperty(name)) {
    _templates[name] = document.querySelector(`template[name="${name}"]`);
  }
  let template = _templates[name];
  if (template) {
    return document.importNode(template.content, true);
  }
  throw new Error(`Unknown template: ${name}`);
}

function nl2br(text) {
  let frag = document.createDocumentFragment();
  let hasAppended = false;
  for (let part of text.split("\n")) {
    if (hasAppended) {
      frag.appendChild(document.createElement("br"));
    }
    frag.appendChild(new Text(part));
    hasAppended = true;
  }
  return frag;
}

/**
 * Select the screeenshot to display above an add-on card.
 *
 * @param {AddonWrapper|DiscoAddonWrapper} addon
 * @returns {string|null}
 *          The URL of the best fitting screenshot, if any.
 */
function getScreenshotUrlForAddon(addon) {
  if (addon.id == "default-theme@mozilla.org") {
    return "chrome://mozapps/content/extensions/default-theme/preview.svg";
  }
  const builtInThemePreview = BuiltInThemes.previewForBuiltInThemeId(addon.id);
  if (builtInThemePreview) {
    return builtInThemePreview;
  }

  let { screenshots } = addon;
  if (!screenshots || !screenshots.length) {
    return null;
  }

  // The image size is defined at .card-heading-image in aboutaddons.css, and
  // is based on the aspect ratio for a 680x92 image. Use the image if possible,
  // and otherwise fall back to the first image and hope for the best.
  let screenshot = screenshots.find(s => s.width === 680 && s.height === 92);
  if (!screenshot) {
    console.warn(`Did not find screenshot with desired size for ${addon.id}.`);
    screenshot = screenshots[0];
  }
  return screenshot.url;
}

/**
 * Adds UTM parameters to a given URL, if it is an AMO URL.
 *
 * @param {string} contentAttribute
 *        Identifies the part of the UI with which the link is associated.
 * @param {string} url
 * @returns {string}
 *          The url with UTM parameters if it is an AMO URL.
 *          Otherwise the url in unmodified form.
 */
function formatUTMParams(contentAttribute, url) {
  let parsedUrl = new URL(url);
  let domain = `.${parsedUrl.hostname}`;
  if (
    !domain.endsWith(".mozilla.org") &&
    // For testing: addons-dev.allizom.org and addons.allizom.org
    !domain.endsWith(".allizom.org")
  ) {
    return url;
  }

  parsedUrl.searchParams.set("utm_source", "firefox-browser");
  parsedUrl.searchParams.set("utm_medium", "firefox-browser");
  parsedUrl.searchParams.set("utm_content", contentAttribute);
  return parsedUrl.href;
}

// A wrapper around an item from the "results" array from AMO's discovery API.
// See https://addons-server.readthedocs.io/en/latest/topics/api/discovery.html
class DiscoAddonWrapper {
  /**
   * @param {object} details
   *        An item in the "results" array from AMO's discovery API.
   */
  constructor(details) {
    // Reuse AddonRepository._parseAddon to have the AMO response parsing logic
    // in one place.
    let repositoryAddon = AddonRepository._parseAddon(details.addon);

    // Note: Any property used by RecommendedAddonCard should appear here.
    // The property names and values should have the same semantics as
    // AddonWrapper, to ease the reuse of helper functions in this file.
    this.id = repositoryAddon.id;
    this.type = repositoryAddon.type;
    this.name = repositoryAddon.name;
    this.screenshots = repositoryAddon.screenshots;
    this.sourceURI = repositoryAddon.sourceURI;
    this.creator = repositoryAddon.creator;
    this.averageRating = repositoryAddon.averageRating;

    this.dailyUsers = details.addon.average_daily_users;

    this.editorialDescription = details.description_text;
    this.iconURL = details.addon.icon_url;
    this.amoListingUrl = details.addon.url;

    this.taarRecommended = details.is_recommendation;
  }
}

/**
 * A helper to retrieve the list of recommended add-ons via AMO's discovery API.
 */
var DiscoveryAPI = {
  // Map<boolean, Promise> Promises from fetching the API results with or
  // without a client ID. The `false` (no client ID) case could actually
  // have been fetched with a client ID. See getResults() for more info.
  _resultPromises: new Map(),

  /**
   * Fetch the list of recommended add-ons. The results are cached.
   *
   * Pending requests are coalesced, so there is only one request at any given
   * time. If a request fails, the pending promises are rejected, but a new
   * call will result in a new request. A succesful response is cached for the
   * lifetime of the document.
   *
   * @param {boolean} preferClientId
   *                  A boolean indicating a preference for using a client ID.
   *                  This will not overwrite the user preference but will
   *                  avoid sending a client ID if no request has been made yet.
   * @returns {Promise<DiscoAddonWrapper[]>}
   */
  async getResults(preferClientId = true) {
    // Allow a caller to set preferClientId to false, but not true if discovery
    // is disabled.
    preferClientId = preferClientId && this.clientIdDiscoveryEnabled;

    // Reuse a request for this preference first.
    let resultPromise =
      this._resultPromises.get(preferClientId) ||
      // If the client ID isn't preferred, we can still reuse a request with the
      // client ID.
      (!preferClientId && this._resultPromises.get(true));

    if (resultPromise) {
      return resultPromise;
    }

    // Nothing is prepared for this preference, make a new request.
    resultPromise = this._fetchRecommendedAddons(preferClientId).catch(e => {
      // Delete the pending promise, so _fetchRecommendedAddons can be
      // called again at the next property access.
      this._resultPromises.delete(preferClientId);
      Cu.reportError(e);
      throw e;
    });

    // Store the new result for the preference.
    this._resultPromises.set(preferClientId, resultPromise);

    return resultPromise;
  },

  get clientIdDiscoveryEnabled() {
    // These prefs match Discovery.sys.mjs for enabling clientId cookies.
    return (
      Services.prefs.getBoolPref(PREF_RECOMMENDATION_ENABLED, false) &&
      Services.prefs.getBoolPref(PREF_TELEMETRY_ENABLED, false) &&
      !PrivateBrowsingUtils.isContentWindowPrivate(window)
    );
  },

  async _fetchRecommendedAddons(useClientId) {
    let discoveryApiUrl = new URL(
      Services.urlFormatter.formatURLPref(PREF_DISCOVERY_API_URL)
    );

    if (useClientId) {
      let clientId = await ClientID.getClientIdHash();
      discoveryApiUrl.searchParams.set("telemetry-client-id", clientId);
    }
    let res = await fetch(discoveryApiUrl.href, {
      credentials: "omit",
    });
    if (!res.ok) {
      throw new Error(`Failed to fetch recommended add-ons, ${res.status}`);
    }
    let { results } = await res.json();
    return results.map(details => new DiscoAddonWrapper(details));
  },
};

class SearchAddons extends HTMLElement {
  connectedCallback() {
    if (this.childElementCount === 0) {
      this.input = document.createXULElement("search-textbox");
      this.input.setAttribute("searchbutton", true);
      this.input.setAttribute("maxlength", 100);
      this.input.setAttribute("data-l10n-attrs", "placeholder");
      document.l10n.setAttributes(this.input, "addons-heading-search-input");
      this.append(this.input);
    }
    this.input.addEventListener("command", this);
  }

  disconnectedCallback() {
    this.input.removeEventListener("command", this);
  }

  handleEvent(e) {
    if (e.type === "command") {
      this.searchAddons(this.value);
    }
  }

  get value() {
    return this.input.value;
  }

  searchAddons(query) {
    if (query.length === 0) {
      return;
    }

    let url = formatUTMParams(
      "addons-manager-search",
      AddonRepository.getSearchURL(query)
    );

    let browser = getBrowserElement();
    let chromewin = browser.ownerGlobal;
    chromewin.openWebLinkIn(url, "tab");
  }
}
customElements.define("search-addons", SearchAddons);

class MessageBarStackElement extends HTMLElement {
  constructor() {
    super();
    this._observer = null;
    const shadowRoot = this.attachShadow({ mode: "open" });
    shadowRoot.append(this.constructor.template.content.cloneNode(true));
  }

  connectedCallback() {
    // Close any message bar that should be allowed based on the
    // maximum number of message bars.
    this.closeMessageBars();

    // Observe mutations to close older bars when new ones have been
    // added.
    this._observer = new MutationObserver(() => {
      this._observer.disconnect();
      this.closeMessageBars();
      this._observer.observe(this, { childList: true });
    });
    this._observer.observe(this, { childList: true });
  }

  disconnectedCallback() {
    this._observer.disconnect();
    this._observer = null;
  }

  closeMessageBars() {
    const { maxMessageBarCount } = this;
    if (maxMessageBarCount > 1) {
      // Remove the older message bars if the stack reached the
      // maximum number of message bars allowed.
      while (this.childElementCount > maxMessageBarCount) {
        this.firstElementChild.remove();
      }
    }
  }

  get maxMessageBarCount() {
    return parseInt(this.getAttribute("max-message-bar-count"), 10);
  }

  static get template() {
    const template = document.createElement("template");

    const style = document.createElement("style");
    // Render the stack in the reverse order if the stack has the
    // reverse attribute set.
    style.textContent = `
      :host {
        display: block;
      }
      :host([reverse]) > slot {
        display: flex;
        flex-direction: column-reverse;
      }
    `;
    template.content.append(style);
    template.content.append(document.createElement("slot"));

    Object.defineProperty(this, "template", {
      value: template,
    });

    return template;
  }
}

customElements.define("message-bar-stack", MessageBarStackElement);

class GlobalWarnings extends MessageBarStackElement {
  constructor() {
    super();
    // This won't change at runtime, but we'll want to fake it in tests.
    this.inSafeMode = Services.appinfo.inSafeMode;
    this.globalWarning = null;
  }

  connectedCallback() {
    this.refresh();
    this.addEventListener("click", this);
    AddonManagerListenerHandler.addListener(this);
  }

  disconnectedCallback() {
    this.removeEventListener("click", this);
    AddonManagerListenerHandler.removeListener(this);
  }

  refresh() {
    if (this.inSafeMode) {
      this.setWarning("safe-mode");
    } else if (
      AddonManager.checkUpdateSecurityDefault &&
      !AddonManager.checkUpdateSecurity
    ) {
      this.setWarning("update-security", { action: true });
    } else if (!AddonManager.checkCompatibility) {
      this.setWarning("check-compatibility", { action: true });
    } else if (AMBrowserExtensionsImport.canCompleteOrCancelInstalls) {
      this.setWarning("imported-addons", { action: true });
    } else {
      this.removeWarning();
    }
  }

  setWarning(type, opts) {
    if (
      this.globalWarning &&
      this.globalWarning.getAttribute("warning-type") !== type
    ) {
      this.removeWarning();
    }
    if (!this.globalWarning) {
      this.globalWarning = document.createElement("moz-message-bar");
      this.globalWarning.setAttribute("warning-type", type);
      let { messageId, buttonId } = this.getGlobalWarningL10nIds(type);
      document.l10n.setAttributes(this.globalWarning, messageId);
      this.globalWarning.setAttribute("data-l10n-attrs", "message");
      if (opts && opts.action) {
        let button = document.createElement("button");
        document.l10n.setAttributes(button, buttonId);
        button.setAttribute("action", type);
        button.setAttribute("slot", "actions");
        this.globalWarning.appendChild(button);
      }
      this.appendChild(this.globalWarning);
    }
  }

  getGlobalWarningL10nIds(type) {
    const WARNING_TYPE_TO_L10NID_MAPPING = {
      "safe-mode": {
        messageId: "extensions-warning-safe-mode2",
      },
      "update-security": {
        messageId: "extensions-warning-update-security2",
        buttonId: "extensions-warning-update-security-button",
      },
      "check-compatibility": {
        messageId: "extensions-warning-check-compatibility2",
        buttonId: "extensions-warning-check-compatibility-button",
      },
      "imported-addons": {
        messageId: "extensions-warning-imported-addons2",
        buttonId: "extensions-warning-imported-addons-button",
      },
    };

    return WARNING_TYPE_TO_L10NID_MAPPING[type];
  }

  removeWarning() {
    if (this.globalWarning) {
      this.globalWarning.remove();
      this.globalWarning = null;
    }
  }

  handleEvent(e) {
    if (e.type === "click") {
      switch (e.target.getAttribute("action")) {
        case "update-security":
          AddonManager.checkUpdateSecurity = true;
          break;
        case "check-compatibility":
          AddonManager.checkCompatibility = true;
          break;
        case "imported-addons":
          AMBrowserExtensionsImport.completeInstalls();
          break;
      }
    }
  }

  /**
   * AddonManager listener events.
   */

  onCompatibilityModeChanged() {
    this.refresh();
  }

  onCheckUpdateSecurityChanged() {
    this.refresh();
  }

  onBrowserExtensionsImportChanged() {
    this.refresh();
  }
}
customElements.define("global-warnings", GlobalWarnings);

class AddonPageHeader extends HTMLElement {
  connectedCallback() {
    if (this.childElementCount === 0) {
      this.appendChild(importTemplate("addon-page-header"));
      this.heading = this.querySelector(".header-name");
      this.backButton = this.querySelector(".back-button");
      this.pageOptionsMenuButton = this.querySelector(
        '[action="page-options"]'
      );
      // The addon-page-options element is outside of this element since this is
      // position: sticky and that would break the positioning of the menu.
      this.pageOptionsMenu = document.getElementById(
        this.getAttribute("page-options-id")
      );
    }
    document.addEventListener("view-selected", this);
    this.addEventListener("click", this);
    this.addEventListener("mousedown", this);
    // Use capture since the event is actually triggered on the internal
    // panel-list and it doesn't bubble.
    this.pageOptionsMenu.addEventListener("shown", this, true);
    this.pageOptionsMenu.addEventListener("hidden", this, true);
  }

  disconnectedCallback() {
    document.removeEventListener("view-selected", this);
    this.removeEventListener("click", this);
    this.removeEventListener("mousedown", this);
    this.pageOptionsMenu.removeEventListener("shown", this, true);
    this.pageOptionsMenu.removeEventListener("hidden", this, true);
  }

  setViewInfo({ type, param }) {
    this.setAttribute("current-view", type);
    this.setAttribute("current-param", param);
    let viewType = type === "list" ? param : type;
    this.setAttribute("type", viewType);

    this.heading.hidden = viewType === "detail";
    this.backButton.hidden = viewType !== "detail" && viewType !== "shortcuts";

    this.backButton.disabled = !history.state?.previousView;

    if (viewType !== "detail") {
      document.l10n.setAttributes(this.heading, `${viewType}-heading`);
    }
  }

  handleEvent(e) {
    let { backButton, pageOptionsMenu, pageOptionsMenuButton } = this;
    if (e.type === "click") {
      switch (e.target) {
        case backButton:
          window.history.back();
          break;
        case pageOptionsMenuButton:
          if (e.inputSource == MouseEvent.MOZ_SOURCE_KEYBOARD) {
            this.pageOptionsMenu.toggle(e);
          }
          break;
      }
    } else if (
      e.type == "mousedown" &&
      e.target == pageOptionsMenuButton &&
      e.button == 0
    ) {
      this.pageOptionsMenu.toggle(e);
    } else if (
      e.target == pageOptionsMenu.panel &&
      (e.type == "shown" || e.type == "hidden")
    ) {
      this.pageOptionsMenuButton.setAttribute(
        "aria-expanded",
        this.pageOptionsMenu.open
      );
    } else if (e.target == document && e.type == "view-selected") {
      const { type, param } = e.detail;
      this.setViewInfo({ type, param });
    }
  }
}
customElements.define("addon-page-header", AddonPageHeader);

class AddonUpdatesMessage extends HTMLElement {
  static get observedAttributes() {
    return ["state"];
  }

  constructor() {
    super();
    this.attachShadow({ mode: "open" });
    let style = document.createElement("style");
    style.textContent = `
      @import "chrome://global/skin/in-content/common.css";
      button {
        margin: 0;
      }
    `;
    this.message = document.createElement("span");
    this.message.hidden = true;
    this.button = document.createElement("button");
    this.button.addEventListener("click", e => {
      if (e.button === 0) {
        gViewController.loadView("updates/available");
      }
    });
    this.button.hidden = true;
    this.shadowRoot.append(style, this.message, this.button);
  }

  connectedCallback() {
    document.l10n.connectRoot(this.shadowRoot);
    document.l10n.translateFragment(this.shadowRoot);
  }

  disconnectedCallback() {
    document.l10n.disconnectRoot(this.shadowRoot);
  }

  attributeChangedCallback(name, oldVal, newVal) {
    if (name === "state" && oldVal !== newVal) {
      let l10nId = `addon-updates-${newVal}`;
      switch (newVal) {
        case "updating":
        case "installed":
        case "none-found":
          this.button.hidden = true;
          this.message.hidden = false;
          document.l10n.setAttributes(this.message, l10nId);
          break;
        case "manual-updates-found":
          this.message.hidden = true;
          this.button.hidden = false;
          document.l10n.setAttributes(this.button, l10nId);
          break;
      }
    }
  }

  set state(val) {
    this.setAttribute("state", val);
  }
}
customElements.define("addon-updates-message", AddonUpdatesMessage);

class AddonPageOptions extends HTMLElement {
  connectedCallback() {
    if (this.childElementCount === 0) {
      this.render();
    }
    this.addEventListener("click", this);
    this.panel.addEventListener("showing", this);
    AddonManagerListenerHandler.addListener(this);
  }

  disconnectedCallback() {
    this.removeEventListener("click", this);
    this.panel.removeEventListener("showing", this);
    AddonManagerListenerHandler.removeListener(this);
  }

  toggle(...args) {
    return this.panel.toggle(...args);
  }

  get open() {
    return this.panel.open;
  }

  render() {
    this.appendChild(importTemplate("addon-page-options"));
    this.panel = this.querySelector("panel-list");
    this.installFromFile = this.querySelector('[action="install-from-file"]');
    this.toggleUpdatesEl = this.querySelector(
      '[action="set-update-automatically"]'
    );
    this.resetUpdatesEl = this.querySelector('[action="reset-update-states"]');
    this.onUpdateModeChanged();
  }

  async handleEvent(e) {
    if (e.type === "click") {
      e.target.disabled = true;
      try {
        await this.onClick(e);
      } finally {
        e.target.disabled = false;
      }
    } else if (e.type === "showing") {
      this.installFromFile.setAttribute(
        "data-l10n-id",
        PREFER_UPDATE_OVER_INSTALL_FOR_EXISTING_ADDON
          ? "addon-install-or-update-from-file"
          : "addon-install-from-file"
      );
      this.installFromFile.hidden = !XPINSTALL_ENABLED;
    }
  }

  async onClick(e) {
    switch (e.target.getAttribute("action")) {
      case "check-for-updates":
        await this.checkForUpdates();
        break;
      case "view-recent-updates":
        gViewController.loadView("updates/recent");
        break;
      case "install-from-file":
        if (XPINSTALL_ENABLED) {
          installAddonsFromFilePicker();
        }
        break;
      case "debug-addons":
        this.openAboutDebugging();
        break;
      case "set-update-automatically":
        await this.toggleAutomaticUpdates();
        break;
      case "reset-update-states":
        await this.resetAutomaticUpdates();
        break;
      case "manage-shortcuts":
        gViewController.loadView("shortcuts/shortcuts");
        break;
    }
  }

  async checkForUpdates() {
    let message = document.getElementById("updates-message");
    message.state = "updating";
    message.hidden = false;
    let { installed, pending } = await checkForUpdates();
    if (pending > 0) {
      message.state = "manual-updates-found";
    } else if (installed > 0) {
      message.state = "installed";
    } else {
      message.state = "none-found";
    }
  }

  openAboutDebugging() {
    let mainWindow = window.windowRoot.ownerGlobal;
    if ("switchToTabHavingURI" in mainWindow) {
      let principal = Services.scriptSecurityManager.getSystemPrincipal();
      mainWindow.switchToTabHavingURI(
        `about:debugging#/runtime/this-firefox`,
        true,
        {
          ignoreFragment: "whenComparing",
          triggeringPrincipal: principal,
        }
      );
    }
  }

  automaticUpdatesEnabled() {
    return AddonManager.updateEnabled && AddonManager.autoUpdateDefault;
  }

  toggleAutomaticUpdates() {
    if (!this.automaticUpdatesEnabled()) {
      // One or both of the prefs is false, i.e. the checkbox is not
      // checked. Now toggle both to true. If the user wants us to
      // auto-update add-ons, we also need to auto-check for updates.
      AddonManager.updateEnabled = true;
      AddonManager.autoUpdateDefault = true;
    } else {
      // Both prefs are true, i.e. the checkbox is checked.
      // Toggle the auto pref to false, but don't touch the enabled check.
      AddonManager.autoUpdateDefault = false;
    }
  }

  async resetAutomaticUpdates() {
    let addons = await AddonManager.getAllAddons();
    for (let addon of addons) {
      if ("applyBackgroundUpdates" in addon) {
        addon.applyBackgroundUpdates = AddonManager.AUTOUPDATE_DEFAULT;
      }
    }
  }

  /**
   * AddonManager listener events.
   */

  onUpdateModeChanged() {
    let updatesEnabled = this.automaticUpdatesEnabled();
    this.toggleUpdatesEl.checked = updatesEnabled;
    let resetType = updatesEnabled ? "automatic" : "manual";
    let resetStringId = `addon-updates-reset-updates-to-${resetType}`;
    document.l10n.setAttributes(this.resetUpdatesEl, resetStringId);
  }
}
customElements.define("addon-page-options", AddonPageOptions);

class CategoryButton extends HTMLButtonElement {
  connectedCallback() {
    if (this.childElementCount != 0) {
      return;
    }

    // Make sure the aria-selected attribute is set correctly.
    this.selected = this.hasAttribute("selected");

    document.l10n.setAttributes(this, `addon-category-${this.name}-title`);

    let text = document.createElement("span");
    text.classList.add("category-name");
    document.l10n.setAttributes(text, `addon-category-${this.name}`);

    this.append(text);
  }

  load() {
    gViewController.loadView(this.viewId);
  }

  get isVisible() {
    // Make a category button visible only if the related addon type is
    // supported by the AddonManager Providers actually registered to
    // the AddonManager.
    return AddonManager.hasAddonType(this.name);
  }

  get badgeCount() {
    return parseInt(this.getAttribute("badge-count"), 10) || 0;
  }

  set badgeCount(val) {
    let count = parseInt(val, 10);
    if (count) {
      this.setAttribute("badge-count", count);
    } else {
      this.removeAttribute("badge-count");
    }
  }

  get selected() {
    return this.hasAttribute("selected");
  }

  set selected(val) {
    this.toggleAttribute("selected", !!val);
    this.setAttribute("aria-selected", !!val);
  }

  get name() {
    return this.getAttribute("name");
  }

  get viewId() {
    return this.getAttribute("viewid");
  }

  // Just setting the hidden attribute isn't enough in case the category gets
  // hidden while about:addons is closed since it could be the last active view
  // which will unhide the button when it gets selected.
  get defaultHidden() {
    return this.hasAttribute("default-hidden");
  }
}
customElements.define("category-button", CategoryButton, { extends: "button" });

class DiscoverButton extends CategoryButton {
  get isVisible() {
    return isDiscoverEnabled();
  }
}
customElements.define("discover-button", DiscoverButton, { extends: "button" });

// Create the button-group element so it gets loaded.
document.createElement("button-group");
class CategoriesBox extends customElements.get("button-group") {
  constructor() {
    super();
    // This will resolve when the initial category states have been set from
    // our cached prefs. This is intended for use in testing to verify that we
    // are caching the previous state.
    this.promiseRendered = new Promise(resolve => {
      this._resolveRendered = resolve;
    });
  }

  handleEvent(e) {
    if (e.target == document && e.type == "view-selected") {
      const { type, param } = e.detail;
      this.select(`addons://${type}/${param}`);
      return;
    }

    if (e.target == this && e.type == "button-group:key-selected") {
      this.activeChild.load();
      return;
    }

    if (e.type == "click") {
      const button = e.target.closest("[viewid]");
      if (button) {
        button.load();
        return;
      }
    }

    // Forward the unhandled events to the button-group custom element.
    super.handleEvent(e);
  }

  disconnectedCallback() {
    document.removeEventListener("view-selected", this);
    this.removeEventListener("button-group:key-selected", this);
    this.removeEventListener("click", this);
    AddonManagerListenerHandler.removeListener(this);
    super.disconnectedCallback();
  }

  async initialize() {
    let hiddenTypes = new Set([]);

    for (let button of this.children) {
      let { defaultHidden, name } = button;
      button.hidden =
        !button.isVisible || (defaultHidden && this.shouldHideCategory(name));

      if (defaultHidden && AddonManager.hasAddonType(name)) {
        hiddenTypes.add(name);
      }
    }

    let hiddenUpdated;
    if (hiddenTypes.size) {
      hiddenUpdated = this.updateHiddenCategories(Array.from(hiddenTypes));
    }

    this.updateAvailableCount();

    document.addEventListener("view-selected", this);
    this.addEventListener("button-group:key-selected", this);
    this.addEventListener("click", this);
    AddonManagerListenerHandler.addListener(this);

    this._resolveRendered();
    await hiddenUpdated;
  }

  shouldHideCategory(name) {
    return Services.prefs.getBoolPref(`extensions.ui.${name}.hidden`, true);
  }

  setShouldHideCategory(name, hide) {
    Services.prefs.setBoolPref(`extensions.ui.${name}.hidden`, hide);
  }

  getButtonByName(name) {
    return this.querySelector(`[name="${name}"]`);
  }

  get selectedChild() {
    return this._selectedChild;
  }

  set selectedChild(node) {
    if (node && this.contains(node)) {
      if (this._selectedChild) {
        this._selectedChild.selected = false;
      }
      this._selectedChild = node;
      this._selectedChild.selected = true;
    }
  }

  select(viewId) {
    let button = this.querySelector(`[viewid="${viewId}"]`);
    if (button) {
      this.activeChild = button;
      this.selectedChild = button;
      button.hidden = false;
      Services.prefs.setStringPref(PREF_UI_LASTCATEGORY, viewId);
    }
  }

  selectType(type) {
    this.select(`addons://list/${type}`);
  }

  onInstalled(addon) {
    let button = this.getButtonByName(addon.type);
    if (button) {
      button.hidden = false;
      this.setShouldHideCategory(addon.type, false);
    }
    this.updateAvailableCount();
  }

  onInstallStarted(install) {
    this.onInstalled(install);
  }

  onNewInstall() {
    this.updateAvailableCount();
  }

  onInstallPostponed() {
    this.updateAvailableCount();
  }

  onInstallCancelled() {
    this.updateAvailableCount();
  }

  async updateAvailableCount() {
    let installs = await AddonManager.getAllInstalls();
    var count = installs.filter(install => {
      return isManualUpdate(install) && !install.installed;
    }).length;
    let availableButton = this.getButtonByName("available-updates");
    availableButton.hidden = !availableButton.selected && count == 0;
    availableButton.badgeCount = count;
  }

  async updateHiddenCategories(types) {
    let hiddenTypes = new Set(types);
    let getAddons = AddonManager.getAddonsByTypes(types);
    let getInstalls = AddonManager.getInstallsByTypes(types);

    for (let addon of await getAddons) {
      if (addon.hidden) {
        continue;
      }

      this.onInstalled(addon);
      hiddenTypes.delete(addon.type);

      if (!hiddenTypes.size) {
        return;
      }
    }

    for (let install of await getInstalls) {
      if (
        install.existingAddon ||
        install.state == AddonManager.STATE_AVAILABLE
      ) {
        continue;
      }

      this.onInstalled(install);
      hiddenTypes.delete(install.type);

      if (!hiddenTypes.size) {
        return;
      }
    }

    for (let type of hiddenTypes) {
      let button = this.getButtonByName(type);
      if (button.selected) {
        // Cancel the load if this view should be hidden.
        gViewController.resetState();
      }
      this.setShouldHideCategory(type, true);
      button.hidden = true;
    }
  }
}
customElements.define("categories-box", CategoriesBox);

class SidebarFooter extends HTMLElement {
  connectedCallback() {
    let list = document.createElement("ul");
    list.classList.add("sidebar-footer-list");

    let systemPrincipal = Services.scriptSecurityManager.getSystemPrincipal();
    let prefsItem = this.createItem({
      icon: "chrome://global/skin/icons/settings.svg",
      createLinkElement: () => {
        let link = document.createElement("a");
        link.href = "about:preferences";
        link.id = "preferencesButton";
        return link;
      },
      titleL10nId: "sidebar-settings-button-title",
      labelL10nId: "addons-settings-button",
      onClick: e => {
        e.preventDefault();
        let hasAboutSettings = windowRoot.ownerGlobal.switchToTabHavingURI(
          "about:settings",
          false,
          {
            ignoreFragment: "whenComparing",
          }
        );
        if (!hasAboutSettings) {
          windowRoot.ownerGlobal.switchToTabHavingURI(
            "about:preferences",
            true,
            {
              ignoreFragment: "whenComparing",
              triggeringPrincipal: systemPrincipal,
            }
          );
        }
      },
    });

    let supportItem = this.createItem({
      icon: "chrome://global/skin/icons/help.svg",
      createLinkElement: () => {
        let link = document.createElement("a", { is: "moz-support-link" });
        link.setAttribute("support-page", "addons-help");
        link.id = "help-button";
        return link;
      },
      titleL10nId: "sidebar-help-button-title",
      labelL10nId: "help-button",
    });

    list.append(prefsItem, supportItem);
    this.append(list);
  }

  createItem({ onClick, titleL10nId, labelL10nId, icon, createLinkElement }) {
    let listItem = document.createElement("li");

    let link = createLinkElement();
    link.classList.add("sidebar-footer-link");
    link.addEventListener("click", onClick);
    document.l10n.setAttributes(link, titleL10nId);

    let img = document.createElement("img");
    img.src = icon;
    img.className = "sidebar-footer-icon";

    let label = document.createElement("span");
    label.className = "sidebar-footer-label";
    document.l10n.setAttributes(label, labelL10nId);

    link.append(img, label);
    listItem.append(link);
    return listItem;
  }
}
customElements.define("sidebar-footer", SidebarFooter, { extends: "footer" });

class AddonOptions extends HTMLElement {
  connectedCallback() {
    if (!this.children.length) {
      this.render();
    }
  }

  get panel() {
    return this.querySelector("panel-list");
  }

  updateSeparatorsVisibility() {
    let lastSeparator;
    let elWasVisible = false;

    // Collect the panel-list children that are not already hidden.
    const children = Array.from(this.panel.children).filter(el => !el.hidden);

    for (let child of children) {
      if (child.localName == "hr") {
        child.hidden = !elWasVisible;
        if (!child.hidden) {
          lastSeparator = child;
        }
        elWasVisible = false;
      } else {
        elWasVisible = true;
      }
    }
    if (!elWasVisible && lastSeparator) {
      lastSeparator.hidden = true;
    }
  }

  get template() {
    return "addon-options";
  }

  render() {
    this.appendChild(importTemplate(this.template));
  }

  setElementState(el, card, addon, updateInstall) {
    switch (el.getAttribute("action")) {
      case "remove":
        if (hasPermission(addon, "uninstall")) {
          // Regular add-on that can be uninstalled.
          el.disabled = false;
          el.hidden = false;
          document.l10n.setAttributes(el, "remove-addon-button");
        } else if (addon.isBuiltin) {
          // Likely the built-in themes, can't be removed, that's fine.
          el.hidden = true;
        } else {
          // Likely sideloaded, mention that it can't be removed with a link.
          el.hidden = false;
          el.disabled = true;
          if (!el.querySelector('[slot="support-link"]')) {
            let link = document.createElement("a", { is: "moz-support-link" });
            link.setAttribute("data-l10n-name", "link");
            link.setAttribute("support-page", "cant-remove-addon");
            link.setAttribute("slot", "support-link");
            el.appendChild(link);
            document.l10n.setAttributes(el, "remove-addon-disabled-button");
          }
        }
        break;
      case "report":
        el.hidden = !isAbuseReportSupported(addon);
        break;
      case "install-update":
        el.hidden = !updateInstall;
        break;
      case "expand":
        el.hidden = card.expanded;
        break;
      case "preferences":
        el.hidden =
          getOptionsType(addon) !== "tab" &&
          (getOptionsType(addon) !== "inline" || card.expanded);
        if (!el.hidden) {
          isAddonOptionsUIAllowed(addon).then(allowed => {
            el.hidden = !allowed;
          });
        }
        break;
    }
  }

  update(card, addon, updateInstall) {
    for (let el of this.items) {
      this.setElementState(el, card, addon, updateInstall);
    }

    // Update the separators visibility based on the updated visibility
    // of the actions in the panel-list.
    this.updateSeparatorsVisibility();
  }

  get items() {
    return this.querySelectorAll("panel-item");
  }

  get visibleItems() {
    return Array.from(this.items).filter(item => !item.hidden);
  }
}
customElements.define("addon-options", AddonOptions);

class PluginOptions extends AddonOptions {
  get template() {
    return "plugin-options";
  }

  setElementState(el, card, addon) {
    const userDisabledStates = {
      "always-activate": false,
      "never-activate": true,
    };
    const action = el.getAttribute("action");
    if (action in userDisabledStates) {
      let userDisabled = userDisabledStates[action];
      el.checked = addon.userDisabled === userDisabled;
      el.disabled = !(el.checked || hasPermission(addon, action));
    } else {
      super.setElementState(el, card, addon);
    }
  }
}
customElements.define("plugin-options", PluginOptions);

class ProxyContextMenu extends HTMLElement {
  openPopupAtScreen(...args) {
    // prettier-ignore
    const parentContextMenuPopup =
      windowRoot.ownerGlobal.document.getElementById("contentAreaContextMenu");
    return parentContextMenuPopup.openPopupAtScreen(...args);
  }
}
customElements.define("proxy-context-menu", ProxyContextMenu);

class InlineOptionsBrowser extends HTMLElement {
  constructor() {
    super();
    // Force the options_ui remote browser to recompute window.mozInnerScreenX
    // and window.mozInnerScreenY when the "addon details" page has been
    // scrolled (See Bug 1390445 for rationale).
    // Also force a repaint to fix an issue where the click location was
    // getting out of sync (see bug 1548687).
    this.updatePositionTask = new DeferredTask(() => {
      if (this.browser && this.browser.isRemoteBrowser) {
        // Select boxes can appear in the wrong spot after scrolling, this will
        // clear that up. Bug 1390445.
        this.browser.frameLoader.requestUpdatePosition();
      }
    }, 100);

    this._embedderElement = null;
    this._promiseDisconnected = new Promise(
      resolve => (this._resolveDisconnected = resolve)
    );
  }

  connectedCallback() {
    window.addEventListener("scroll", this, true);
    const { embedderElement } = top.browsingContext;
    this._embedderElement = embedderElement;
    embedderElement.addEventListener("FullZoomChange", this);
    embedderElement.addEventListener("TextZoomChange", this);
  }

  disconnectedCallback() {
    this._resolveDisconnected();
    window.removeEventListener("scroll", this, true);
    this._embedderElement?.removeEventListener("FullZoomChange", this);
    this._embedderElement?.removeEventListener("TextZoomChange", this);
    this._embedderElement = null;
  }

  handleEvent(e) {
    switch (e.type) {
      case "scroll":
        return this.updatePositionTask.arm();
      case "FullZoomChange":
      case "TextZoomChange":
        return this.maybeUpdateZoom();
    }
    return undefined;
  }

  maybeUpdateZoom() {
    let bc = this.browser?.browsingContext;
    let topBc = top.browsingContext;
    if (!bc || !topBc) {
      return;
    }
    // Use the same full-zoom as our top window.
    bc.fullZoom = topBc.fullZoom;
    bc.textZoom = topBc.textZoom;
  }

  setAddon(addon) {
    this.addon = addon;
  }

  destroyBrowser() {
    this.textContent = "";
  }

  ensureBrowserCreated() {
    if (this.childElementCount === 0) {
      this.render();
    }
  }

  async render() {
    let { addon } = this;
    if (!addon) {
      throw new Error("addon required to create inline options");
    }

    let browser = document.createXULElement("browser");
    browser.setAttribute("type", "content");
    browser.setAttribute("disableglobalhistory", "true");
    browser.setAttribute("messagemanagergroup", "webext-browsers");
    browser.setAttribute("id", "addon-inline-options");
    browser.setAttribute("class", "addon-inline-options");
    browser.setAttribute("transparent", "true");
    browser.setAttribute("forcemessagemanager", "true");
    browser.setAttribute("autocompletepopup", "PopupAutoComplete");

    let { optionsURL, optionsBrowserStyle } = addon;
    if (addon.isWebExtension) {
      let policy = ExtensionParent.WebExtensionPolicy.getByID(addon.id);
      browser.setAttribute(
        "initialBrowsingContextGroupId",
        policy.browsingContextGroupId
      );
    }

    let readyPromise;
    let remoteSubframes = window.docShell.QueryInterface(
      Ci.nsILoadContext
    ).useRemoteSubframes;
    // For now originAttributes have no effect, which will change if the
    // optionsURL becomes anything but moz-extension* or we start considering
    // OA for extensions.
    var oa = E10SUtils.predictOriginAttributes({ browser });
    let loadRemote = E10SUtils.canLoadURIInRemoteType(
      optionsURL,
      remoteSubframes,
      E10SUtils.EXTENSION_REMOTE_TYPE,
      oa
    );
    if (loadRemote) {
      browser.setAttribute("remote", "true");
      browser.setAttribute("remoteType", E10SUtils.EXTENSION_REMOTE_TYPE);

      readyPromise = promiseEvent("XULFrameLoaderCreated", browser);
    } else {
      readyPromise = promiseEvent("load", browser, true);
    }

    this.appendChild(browser);
    this.browser = browser;

    // Force bindings to apply synchronously.
    browser.clientTop;

    await readyPromise;

    this.maybeUpdateZoom();

    if (!browser.messageManager) {
      // If the browser.messageManager is undefined, the browser element has
      // been removed from the document in the meantime (e.g. due to a rapid
      // sequence of addon reload), return null.
      return;
    }

    ExtensionParent.apiManager.emit("extension-browser-inserted", browser);

    await new Promise(resolve => {
      let messageListener = {
        receiveMessage({ name, data }) {
          if (name === "Extension:BrowserResized") {
            browser.style.height = `${data.height}px`;
          } else if (name === "Extension:BrowserContentLoaded") {
            resolve();
          }
        },
      };

      let mm = browser.messageManager;

      if (!mm) {
        // If the browser.messageManager is undefined, the browser element has
        // been removed from the document in the meantime (e.g. due to a rapid
        // sequence of addon reload), return null.
        resolve();
        return;
      }

      mm.loadFrameScript(
        "chrome://extensions/content/ext-browser-content.js",
        false,
        true
      );
      mm.addMessageListener("Extension:BrowserContentLoaded", messageListener);
      mm.addMessageListener("Extension:BrowserResized", messageListener);

      let browserOptions = {
        fixedWidth: true,
        isInline: true,
      };

      if (optionsBrowserStyle) {
        // aboutaddons.js is not used on Android. extension.css is included in
        // Firefox desktop and Thunderbird.
        // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
        browserOptions.stylesheets = ["chrome://browser/content/extension.css"];
      }

      mm.sendAsyncMessage("Extension:InitBrowser", browserOptions);

      if (browser.isConnectedAndReady) {
        this.fixupAndLoadURIString(optionsURL);
      } else {
        // browser custom element does opt-in the delayConnectedCallback
        // behavior (see connectedCallback in the custom element definition
        // from browser-custom-element.js) and so calling browser.loadURI
        // would fail if the about:addons document is not yet fully loaded.
        Promise.race([
          promiseEvent("DOMContentLoaded", document),
          this._promiseDisconnected,
        ]).then(() => {
          this.fixupAndLoadURIString(optionsURL);
        });
      }
    });
  }

  fixupAndLoadURIString(uriString) {
    if (!this.browser || !this.browser.isConnectedAndReady) {
      throw new Error("Fail to loadURI");
    }

    this.browser.fixupAndLoadURIString(uriString, {
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });
  }
}
customElements.define("inline-options-browser", InlineOptionsBrowser);

class UpdateReleaseNotes extends HTMLElement {
  connectedCallback() {
    this.addEventListener("click", this);
  }

  disconnectedCallback() {
    this.removeEventListener("click", this);
  }

  handleEvent(e) {
    // We used to strip links, but ParserUtils.parseFragment() leaves them in,
    // so just make sure we open them using the null principal in a new tab.
    if (e.type == "click" && e.target.localName == "a" && e.target.href) {
      e.preventDefault();
      e.stopPropagation();
      windowRoot.ownerGlobal.openWebLinkIn(e.target.href, "tab");
    }
  }

  async loadForUri(uri) {
    // Can't load the release notes without a URL to load.
    if (!uri || !uri.spec) {
      this.setErrorMessage();
      this.dispatchEvent(new CustomEvent("release-notes-error"));
      return;
    }

    // Don't try to load for the same update a second time.
    if (this.url == uri.spec) {
      this.dispatchEvent(new CustomEvent("release-notes-cached"));
      return;
    }

    // Store the URL to skip the network if loaded again.
    this.url = uri.spec;

    // Set the loading message before hitting the network.
    this.setLoadingMessage();
    this.dispatchEvent(new CustomEvent("release-notes-loading"));

    try {
      // loadReleaseNotes will fetch and sanitize the release notes.
      let fragment = await loadReleaseNotes(uri);
      this.textContent = "";
      this.appendChild(fragment);
      this.dispatchEvent(new CustomEvent("release-notes-loaded"));
    } catch (e) {
      this.setErrorMessage();
      this.dispatchEvent(new CustomEvent("release-notes-error"));
    }
  }

  setMessage(id) {
    this.textContent = "";
    let message = document.createElement("p");
    document.l10n.setAttributes(message, id);
    this.appendChild(message);
  }

  setLoadingMessage() {
    this.setMessage("release-notes-loading");
  }

  setErrorMessage() {
    this.setMessage("release-notes-error");
  }
}
customElements.define("update-release-notes", UpdateReleaseNotes);

class AddonPermissionsList extends HTMLElement {
  setAddon(addon) {
    this.addon = addon;
    this.render();
  }

  async render() {
    let empty = { origins: [], permissions: [], data_collection: [] };
    let requiredPerms = { ...(this.addon.userPermissions ?? empty) };
    let optionalPerms = { ...(this.addon.optionalPermissions ?? empty) };
    let grantedPerms = await ExtensionPermissions.get(this.addon.id);

    if (manifestV3enabled) {
      // If optional permissions include <all_urls>, extension can request and
      // be granted permission for individual sites not listed in the manifest.
      // Include them as well in the optional origins list.
      let origins = [
        ...(this.addon.optionalOriginsNormalized ?? []),
        ...grantedPerms.origins.filter(o => !requiredPerms.origins.includes(o)),
      ];
      optionalPerms.origins = [...new Set(origins)];
    }

    let permissions = Extension.formatPermissionStrings(
      {
        permissions: requiredPerms,
        optionalPermissions: optionalPerms,
      },
      { buildOptionalOrigins: manifestV3enabled }
    );
    let optionalEntries = [
      ...Object.entries(permissions.optionalPermissions),
      ...Object.entries(permissions.optionalOrigins),
      ...Object.entries(permissions.optionalDataCollectionPermissions),
    ];

    this.textContent = "";
    let frag = importTemplate("addon-permissions-list");

    if (permissions.msgs.length) {
      let section = frag.querySelector(".addon-permissions-required");
      section.hidden = false;
      let list = section.querySelector(".addon-permissions-list");
      for (const msg of permissions.msgs) {
        let item = document.createElement("li");
        item.classList.add("permission-info", "permission-checked");
        item.appendChild(document.createTextNode(msg));
        list.appendChild(item);
      }
    }

    if (permissions.dataCollectionPermissions?.msg) {
      let section = frag.querySelector(
        ".addon-data-collection-permissions-required"
      );
      section.hidden = false;
      let list = section.querySelector(".addon-permissions-list");
      let item = document.createElement("li");
      item.classList.add("permission-info", "permission-checked");
      item.appendChild(
        document.createTextNode(permissions.dataCollectionPermissions.msg)
      );
      list.appendChild(item);
    }

    if (optionalEntries.length) {
      let section = frag.querySelector(".addon-permissions-optional");
      let list = section.querySelector(".addon-permissions-list");

      let dataCollectionSection = frag.querySelector(
        ".addon-data-collection-permissions-optional"
      );
      let dataCollectionList = dataCollectionSection.querySelector(
        ".addon-permissions-list"
      );

      for (let id = 0; id < optionalEntries.length; id++) {
        let [perm, msg] = optionalEntries[id];

        let type = "permission";
        if (permissions.optionalOrigins[perm]) {
          type = "origin";
        } else if (permissions.optionalDataCollectionPermissions[perm]) {
          type = "data_collection";
        }
        let item = document.createElement("li");
        item.classList.add("permission-info");

        let toggle = document.createElement("moz-toggle");
        toggle.setAttribute("label", msg);
        toggle.id = `permission-${id}`;
        toggle.setAttribute("permission-type", type);

        let checked =
          grantedPerms.permissions.includes(perm) ||
          grantedPerms.origins.includes(perm) ||
          grantedPerms.data_collection.includes(perm);

        // If this is one of the "all sites" permissions
        if (Extension.isAllSitesPermission(perm)) {
          // mark it as checked if ANY of the "all sites" permission is granted.
          checked = await AddonCard.optionalAllSitesGranted(this.addon.id);
          toggle.toggleAttribute("permission-all-sites", true);
        }

        toggle.pressed = checked;

        toggle.setAttribute("permission-key", perm);
        toggle.setAttribute("action", "toggle-permission");

        if (perm === "userScripts") {
          let mb = document.createElement("moz-message-bar");
          mb.setAttribute("type", "warning");
          mb.messageL10nId = "webext-perms-extra-warning-userScripts-long";
          mb.slot = "nested";
          toggle.append(mb);
        }
        item.appendChild(toggle);

        if (type === "data_collection") {
          dataCollectionSection.hidden = false;
          dataCollectionList.appendChild(item);
        } else {
          section.hidden = false;
          list.appendChild(item);
        }
      }
    }

    if (
      !permissions.msgs.length &&
      !optionalEntries.length &&
      !permissions.dataCollectionPermissions?.msg
    ) {
      let row = frag.querySelector(".addon-permissions-empty");
      row.hidden = false;
    }

    this.appendChild(frag);
  }
}
customElements.define("addon-permissions-list", AddonPermissionsList);

class AddonSitePermissionsList extends HTMLElement {
  setAddon(addon) {
    this.addon = addon;
    this.render();
  }

  async render() {
    let permissions = Extension.formatPermissionStrings({
      sitePermissions: this.addon.sitePermissions,
      siteOrigin: this.addon.siteOrigin,
    });

    this.textContent = "";
    let frag = importTemplate("addon-sitepermissions-list");

    if (permissions.msgs.length) {
      let section = frag.querySelector(".addon-permissions-required");
      section.hidden = false;
      let list = section.querySelector(".addon-permissions-list");
      let header = section.querySelector(".permission-header");
      document.l10n.setAttributes(header, "addon-sitepermissions-required", {
        hostname: new URL(this.addon.siteOrigin).hostname,
      });

      for (let msg of permissions.msgs) {
        let item = document.createElement("li");
        item.classList.add("permission-info", "permission-checked");
        item.appendChild(document.createTextNode(msg));
        list.appendChild(item);
      }
    }

    this.appendChild(frag);
  }
}
customElements.define("addon-sitepermissions-list", AddonSitePermissionsList);

class AddonDetails extends HTMLElement {
  connectedCallback() {
    if (!this.children.length) {
      this.render();
    }
    this.deck.addEventListener("view-changed", this);
    this.descriptionShowMoreButton.addEventListener("click", this);
  }

  disconnectedCallback() {
    this.inlineOptions.destroyBrowser();
    this.deck.removeEventListener("view-changed", this);
    this.descriptionShowMoreButton.removeEventListener("click", this);
  }

  handleEvent(e) {
    if (e.type == "view-changed" && e.target == this.deck) {
      switch (this.deck.selectedViewName) {
        case "release-notes":
          let releaseNotes = this.querySelector("update-release-notes");
          let uri = this.releaseNotesUri;
          if (uri) {
            releaseNotes.loadForUri(uri);
          }
          break;
        case "preferences":
          if (getOptionsType(this.addon) == "inline") {
            this.inlineOptions.ensureBrowserCreated();
          }
          break;
      }

      // When a details view is rendered again, the default details view is
      // unconditionally shown. So if any other tab is selected, do not save
      // the current scroll offset, but start at the top of the page instead.
      ScrollOffsets.canRestore = this.deck.selectedViewName === "details";
    } else if (
      e.type == "click" &&
      e.target == this.descriptionShowMoreButton
    ) {
      this.toggleDescription();
    }
  }

  onInstalled() {
    let policy = WebExtensionPolicy.getByID(this.addon.id);
    let extension = policy && policy.extension;
    if (extension && extension.startupReason === "ADDON_UPGRADE") {
      // Ensure the options browser is recreated when a new version starts.
      this.extensionShutdown();
      this.extensionStartup();
    }
  }

  onDisabled() {
    this.extensionShutdown();
  }

  onEnabled() {
    this.extensionStartup();
  }

  extensionShutdown() {
    this.inlineOptions.destroyBrowser();
  }

  extensionStartup() {
    if (this.deck.selectedViewName === "preferences") {
      this.inlineOptions.ensureBrowserCreated();
    }
  }

  toggleDescription() {
    this.descriptionCollapsed = !this.descriptionCollapsed;

    this.descriptionWrapper.classList.toggle(
      "addon-detail-description-collapse",
      this.descriptionCollapsed
    );

    this.descriptionShowMoreButton.hidden = false;
    document.l10n.setAttributes(
      this.descriptionShowMoreButton,
      this.descriptionCollapsed
        ? "addon-detail-description-expand"
        : "addon-detail-description-collapse"
    );
  }

  get releaseNotesUri() {
    let { releaseNotesURI } = getUpdateInstall(this.addon) || this.addon;
    return releaseNotesURI;
  }

  setAddon(addon) {
    this.addon = addon;
  }

  update() {
    let { addon } = this;

    // Hide tab buttons that won't have any content.
    let getButtonByName = name =>
      this.tabGroup.querySelector(`[name="${name}"]`);
    let permsBtn = getButtonByName("permissions");
    permsBtn.hidden = addon.type != "extension";
    let notesBtn = getButtonByName("release-notes");
    notesBtn.hidden = !this.releaseNotesUri;
    let prefsBtn = getButtonByName("preferences");
    prefsBtn.hidden = getOptionsType(addon) !== "inline";
    if (prefsBtn.hidden) {
      if (this.deck.selectedViewName === "preferences") {
        this.deck.selectedViewName = "details";
      }
    } else {
      isAddonOptionsUIAllowed(addon).then(allowed => {
        prefsBtn.hidden = !allowed;
      });
    }

    // Override the deck button string when the feature is enabled, which isn't
    // the case by default for now.
    if (DATA_COLLECTION_PERMISSIONS_ENABLED) {
      permsBtn.setAttribute("data-l10n-id", "permissions-data-addon-button");
    }

    // Hide the tab group if "details" is the only visible button.
    let tabGroupButtons = this.tabGroup.querySelectorAll(".tab-button");
    this.tabGroup.hidden = Array.from(tabGroupButtons).every(button => {
      return button.name == "details" || button.hidden;
    });

    // Show the update check button if necessary. The button might not exist if
    // the add-on doesn't support updates.
    let updateButton = this.querySelector('[action="update-check"]');
    if (updateButton) {
      updateButton.hidden =
        this.addon.updateInstall || AddonManager.shouldAutoUpdate(this.addon);
    }

    // Set the value for auto updates.
    let inputs = this.querySelectorAll(".addon-detail-row-updates input");
    for (let input of inputs) {
      input.checked = input.value == addon.applyBackgroundUpdates;
    }
  }

  renderDescription(addon) {
    this.descriptionWrapper = this.querySelector(
      ".addon-detail-description-wrapper"
    );
    this.descriptionContents = this.querySelector(".addon-detail-description");
    this.descriptionShowMoreButton = this.querySelector(
      ".addon-detail-description-toggle"
    );

    if (addon.getFullDescription) {
      this.descriptionContents.appendChild(addon.getFullDescription(document));
    } else if (addon.fullDescription) {
      this.descriptionContents.appendChild(nl2br(addon.fullDescription));
    }

    this.descriptionCollapsed = false;

    requestAnimationFrame(() => {
      const remSize = parseFloat(
        getComputedStyle(document.documentElement).fontSize
      );
      const { height } = this.descriptionContents.getBoundingClientRect();

      // collapse description if there are too many lines,i.e. height > (20 rem)
      if (height > 20 * remSize) {
        this.toggleDescription();
      }
    });
  }

  updateQuarantinedDomainsUserAllowed() {
    const { addon } = this;
    let quarantinedDomainsUserAllowedRow = this.querySelector(
      ".addon-detail-row-quarantined-domains"
    );
    if (addon.canChangeQuarantineIgnored) {
      quarantinedDomainsUserAllowedRow.hidden = false;
      quarantinedDomainsUserAllowedRow.nextElementSibling.hidden = false;
      quarantinedDomainsUserAllowedRow.querySelector(
        `[value="${addon.quarantineIgnoredByUser ? 1 : 0}"]`
      ).checked = true;
    } else {
      quarantinedDomainsUserAllowedRow.hidden = true;
      quarantinedDomainsUserAllowedRow.nextElementSibling.hidden = true;
    }
  }

  async render() {
    let { addon } = this;
    if (!addon) {
      throw new Error("addon-details must be initialized by setAddon");
    }

    this.textContent = "";
    this.appendChild(importTemplate("addon-details"));

    this.deck = this.querySelector("named-deck");
    this.tabGroup = this.querySelector(".tab-group");

    // Set the add-on for the permissions section.
    this.permissionsList = this.querySelector("addon-permissions-list");
    this.permissionsList.setAddon(addon);

    // Set the add-on for the sitepermissions section.
    this.sitePermissionsList = this.querySelector("addon-sitepermissions-list");
    if (addon.type == "sitepermission") {
      this.sitePermissionsList.setAddon(addon);
    }
    this.querySelector(".addon-detail-sitepermissions").hidden =
      addon.type !== "sitepermission";

    // Set the add-on for the mlmodel details.
    this.mlModelDetails = this.querySelector("addon-mlmodel-details");
    this.mlModelDetails.addon = addon.type == "mlmodel" ? addon : null;
    this.querySelector(".addon-detail-mlmodel").hidden =
      addon.type != "mlmodel";

    // Set the add-on for the preferences section.
    this.inlineOptions = this.querySelector("inline-options-browser");
    this.inlineOptions.setAddon(addon);

    // Full description.
    this.renderDescription(addon);
    this.querySelector(".addon-detail-contribute").hidden =
      !addon.contributionURL;
    this.querySelector(".addon-detail-row-updates").hidden = !hasPermission(
      addon,
      "upgrade"
    );

    if (addon.type != "extension") {
      // Don't show any private browsing related section for non-extension
      // addon types, because not relevant or they are either always allowed
      // (e.g. static themes).
      //
      // TODO(Bug 1799090): introduce ad-hoc UI for "sitepermission" addon type.
    } else if (addon.incognito == "not_allowed") {
      let pbRowNotAllowed = this.querySelector(
        ".addon-detail-row-private-browsing-disallowed"
      );
      pbRowNotAllowed.hidden = false;
      pbRowNotAllowed.nextElementSibling.hidden = false;
    } else if (!hasPermission(addon, "change-privatebrowsing")) {
      let pbRowRequired = this.querySelector(
        ".addon-detail-row-private-browsing-required"
      );
      pbRowRequired.hidden = false;
      pbRowRequired.nextElementSibling.hidden = false;
    } else {
      let pbRow = this.querySelector(".addon-detail-row-private-browsing");
      pbRow.hidden = false;
      pbRow.nextElementSibling.hidden = false;
      let isAllowed = await isAllowedInPrivateBrowsing(addon);
      pbRow.querySelector(`[value="${isAllowed ? 1 : 0}"]`).checked = true;
    }

    this.updateQuarantinedDomainsUserAllowed();

    // Author.
    let creatorRow = this.querySelector(".addon-detail-row-author");
    if (addon.creator) {
      let link = creatorRow.querySelector("a");
      link.hidden = !addon.creator.url;
      if (link.hidden) {
        creatorRow.appendChild(new Text(addon.creator.name));
      } else {
        link.href = formatUTMParams(
          "addons-manager-user-profile-link",
          addon.creator.url
        );
        link.target = "_blank";
        link.textContent = addon.creator.name;
      }
    } else {
      creatorRow.hidden = true;
    }

    // Version. Don't show a version for LWTs.
    let version = this.querySelector(".addon-detail-row-version");
    if (addon.version && !/@personas\.mozilla\.org/.test(addon.id)) {
      version.appendChild(new Text(addon.version));
    } else {
      version.hidden = true;
    }

    // Last updated.
    let updateDate = this.querySelector(".addon-detail-row-lastUpdated");
    if (addon.updateDate) {
      let lastUpdated = addon.updateDate.toLocaleDateString(undefined, {
        year: "numeric",
        month: "long",
        day: "numeric",
      });
      updateDate.appendChild(new Text(lastUpdated));
    } else {
      updateDate.hidden = true;
    }

    // Homepage.
    let homepageRow = this.querySelector(".addon-detail-row-homepage");
    if (addon.homepageURL) {
      let homepageURL = homepageRow.querySelector("a");
      homepageURL.href = addon.homepageURL;
      homepageURL.textContent = addon.homepageURL;
    } else {
      homepageRow.hidden = true;
    }

    // Rating.
    let ratingRow = this.querySelector(".addon-detail-row-rating");
    if (addon.reviewURL) {
      ratingRow.querySelector("moz-five-star").rating = addon.averageRating;
      let reviews = ratingRow.querySelector("a");
      reviews.href = formatUTMParams(
        "addons-manager-reviews-link",
        addon.reviewURL
      );
      document.l10n.setAttributes(reviews, "addon-detail-reviews-link", {
        numberOfReviews: addon.reviewCount,
      });
    } else {
      ratingRow.hidden = true;
    }

    this.update();
  }

  showPrefs() {
    if (getOptionsType(this.addon) == "inline") {
      this.deck.selectedViewName = "preferences";
      this.inlineOptions.ensureBrowserCreated();
    }
  }
}
customElements.define("addon-details", AddonDetails);

/**
 * A card component for managing an add-on. It should be initialized by setting
 * the add-on with `setAddon()` before being connected to the document.
 *
 *    let card = document.createElement("addon-card");
 *    card.setAddon(addon);
 *    document.body.appendChild(card);
 */
class AddonCard extends HTMLElement {
  connectedCallback() {
    // If we've already rendered we can just update, otherwise render.
    if (this.children.length) {
      this.update();
    } else {
      this.render();
    }
    this.registerListeners();
  }

  disconnectedCallback() {
    this.removeListeners();
  }

  get expanded() {
    return this.hasAttribute("expanded");
  }

  set expanded(val) {
    if (val) {
      this.setAttribute("expanded", "true");
    } else {
      this.removeAttribute("expanded");
    }
  }

  get updateInstall() {
    return this._updateInstall;
  }

  set updateInstall(install) {
    this._updateInstall = install;
    if (this.children.length) {
      this.update();
    }
  }

  get reloading() {
    return this.hasAttribute("reloading");
  }

  set reloading(val) {
    this.toggleAttribute("reloading", val);
  }

  /**
   * Set the add-on for this card. The card will be populated based on the
   * add-on when it is connected to the DOM.
   *
   * @param {AddonWrapper} addon The add-on to use.
   */
  setAddon(addon) {
    this.addon = addon;
    let install = getUpdateInstall(addon);
    if (
      install &&
      (isInState(install, "available") || isInState(install, "postponed"))
    ) {
      this.updateInstall = install;
    } else {
      this.updateInstall = null;
    }
    if (this.children.length) {
      this.render();
    }
  }

  async setAddonPermission(permission, type, action) {
    let { addon } = this;
    let perms = { origins: [], permissions: [] };

    if (!["add", "remove"].includes(action)) {
      throw new Error("invalid action for permission change");
    }

    if (type === "permission") {
      perms.permissions = [permission];
    } else if (type === "origin") {
      perms.origins = [permission];
    } else if (type === "data_collection") {
      perms.data_collection = [permission];
    } else {
      throw new Error("unknown permission type changed");
    }

    let normalized = ExtensionPermissions.normalizeOptional(
      perms,
      addon.optionalPermissions
    );

    let policy = WebExtensionPolicy.getByID(addon.id);
    ExtensionPermissions[action](addon.id, normalized, policy?.extension);
  }

  async handleEvent(e) {
    let { addon } = this;
    let action = e.target.getAttribute("action");

    if (e.type == "click") {
      switch (action) {
        case "toggle-disabled":
          // Keep the checked state the same until the add-on's state changes.
          e.target.checked = !addon.userDisabled;
          if (addon.userDisabled) {
            if (shouldShowPermissionsPrompt(addon)) {
              await showPermissionsPrompt(addon);
            } else {
              await addon.enable();
            }
          } else {
            await addon.disable();
          }
          break;
        case "always-activate":
          addon.userDisabled = false;
          break;
        case "never-activate":
          addon.userDisabled = true;
          break;
        case "update-check": {
          let { found } = await checkForUpdate(addon);
          if (!found) {
            this.sendEvent("no-update");
          }
          break;
        }
        case "install-postponed": {
          const { updateInstall } = this;
          if (updateInstall && isInState(updateInstall, "postponed")) {
            updateInstall.continuePostponedInstall();
          }
          break;
        }
        case "install-update":
          // Make sure that an update handler is attached to the install object
          // before starting the update installation (otherwise the user would
          // not be prompted for the new permissions requested if necessary),
          // and also make sure that a prompt handler attached from a closed
          // about:addons tab is replaced by the one attached by the currently
          // active about:addons tab.
          attachUpdateHandler(this.updateInstall);
          this.updateInstall.install().then(
            () => {
              detachUpdateHandler(this.updateInstall);
              // The card will update with the new add-on when it gets
              // installed.
              this.sendEvent("update-installed");
            },
            () => {
              detachUpdateHandler(this.updateInstall);
              // Update our state if the install is cancelled.
              this.update();
              this.sendEvent("update-cancelled");
            }
          );
          // Clear the install since it will be removed from the global list of
          // available updates (whether it succeeds or fails).
          this.updateInstall = null;
          break;
        case "contribute":
          windowRoot.ownerGlobal.openWebLinkIn(addon.contributionURL, "tab");
          break;
        case "preferences":
          if (getOptionsType(addon) == "tab") {
            openOptionsInTab(addon.optionsURL);
          } else if (getOptionsType(addon) == "inline") {
            gViewController.loadView(`detail/${this.addon.id}/preferences`);
          }
          break;
        case "remove":
          {
            this.panel.hide();
            if (!hasPermission(addon, "uninstall")) {
              this.sendEvent("remove-disabled");
              return;
            }
            let { BrowserAddonUI } = windowRoot.ownerGlobal;
            let { remove, report } =
              await BrowserAddonUI.promptRemoveExtension(addon);
            if (remove) {
              await addon.uninstall(true);
              this.sendEvent("remove");
              if (report) {
                openAbuseReport({
                  addonId: addon.id,
                  reportEntryPoint: "uninstall",
                });
              }
            } else {
              this.sendEvent("remove-cancelled");
            }
          }
          break;
        case "expand":
          gViewController.loadView(`detail/${this.addon.id}`);
          break;
        case "more-options":
          // Open panel on click from the keyboard.
          if (e.inputSource == MouseEvent.MOZ_SOURCE_KEYBOARD) {
            this.panel.toggle(e);
          }
          break;
        case "report":
          this.panel.hide();
          openAbuseReport({ addonId: addon.id, reportEntryPoint: "menu" });
          break;
        case "link":
          if (e.target.getAttribute("url")) {
            windowRoot.ownerGlobal.openWebLinkIn(
              e.target.getAttribute("url"),
              "tab"
            );
          }
          break;
        default:
          // Handle a click on the card itself.
          if (
            !this.expanded &&
            (e.target === this.addonNameEl || !e.target.closest("a"))
          ) {
            e.preventDefault();
            gViewController.loadView(`detail/${this.addon.id}`);
          }
          break;
      }
    } else if (e.type == "toggle" && action == "toggle-permission") {
      let permission = e.target.getAttribute("permission-key");
      let type = e.target.getAttribute("permission-type");
      let fname = e.target.pressed ? "add" : "remove";
      this.setAddonPermission(permission, type, fname);
    } else if (e.type == "change") {
      let { name } = e.target;
      switch (name) {
        case "autoupdate": {
          addon.applyBackgroundUpdates = e.target.value;
          break;
        }
        case "private-browsing": {
          let policy = WebExtensionPolicy.getByID(addon.id);
          let extension = policy && policy.extension;

          if (e.target.value == "1") {
            await ExtensionPermissions.add(
              addon.id,
              PRIVATE_BROWSING_PERMS,
              extension
            );
          } else {
            await ExtensionPermissions.remove(
              addon.id,
              PRIVATE_BROWSING_PERMS,
              extension
            );
          }
          // Reload the extension if it is already enabled. This ensures any
          // change on the private browsing permission is properly handled.
          if (addon.isActive) {
            this.reloading = true;
            // Reloading will trigger an enable and update the card.
            addon.reload();
          } else {
            // Update the card if the add-on isn't active.
            this.update();
          }
          break;
        }
        case "quarantined-domains-user-allowed": {
          addon.quarantineIgnoredByUser = e.target.value == "1";
          break;
        }
      }
    } else if (e.type == "mousedown") {
      // Open panel on mousedown when the mouse is used.
      if (action == "more-options" && e.button == 0) {
        this.panel.toggle(e);
      }
    } else if (e.type === "shown" || e.type === "hidden") {
      let panelOpen = e.type === "shown";
      // The card will be dimmed if it's disabled, but when the panel is open
      // that should be reverted so the menu items can be easily read.
      this.toggleAttribute("panelopen", panelOpen);
      this.optionsButton.setAttribute("aria-expanded", panelOpen);
    }
  }

  get panel() {
    return this.card.querySelector("panel-list");
  }

  get postponedMessageBar() {
    return this.card.querySelector(".update-postponed-bar");
  }

  registerListeners() {
    this.addEventListener("change", this);
    this.addEventListener("click", this);
    this.addEventListener("mousedown", this);
    this.addEventListener("toggle", this);
    this.panel.addEventListener("shown", this);
    this.panel.addEventListener("hidden", this);
  }

  removeListeners() {
    this.removeEventListener("change", this);
    this.removeEventListener("click", this);
    this.removeEventListener("mousedown", this);
    this.removeEventListener("toggle", this);
    this.panel.removeEventListener("shown", this);
    this.panel.removeEventListener("hidden", this);
  }

  /**
   * Update the card's contents based on the previously set add-on. This should
   * be called if there has been a change to the add-on.
   */
  update() {
    let { addon, card } = this;

    card.setAttribute("active", addon.isActive);

    // Set the icon or theme preview.
    let iconEl = card.querySelector(".addon-icon");
    let preview = card.querySelector(".card-heading-image");
    if (addon.type == "theme") {
      iconEl.hidden = true;
      let screenshotUrl = getScreenshotUrlForAddon(addon);
      if (screenshotUrl) {
        preview.src = screenshotUrl;
      }
      preview.hidden = !screenshotUrl;
    } else {
      preview.hidden = true;
      iconEl.hidden = false;
      if (addon.type == "plugin") {
        iconEl.src = PLUGIN_ICON_URL;
      } else {
        iconEl.src =
          AddonManager.getPreferredIconURL(addon, 32, window) ||
          EXTENSION_ICON_URL;
      }
    }

    // Update the name.
    let name = this.addonNameEl;
    let setDisabledStyle = !(addon.isActive || addon.type === "theme");
    if (!setDisabledStyle) {
      name.textContent = addon.name;
      name.removeAttribute("data-l10n-id");
    } else {
      document.l10n.setAttributes(name, "addon-name-disabled", {
        name: addon.name,
      });
    }
    name.title = `${addon.name} ${addon.version}`;

    let toggleDisabledButton = card.querySelector('[action="toggle-disabled"]');
    if (toggleDisabledButton) {
      let toggleDisabledAction = addon.userDisabled ? "enable" : "disable";
      toggleDisabledButton.hidden = !hasPermission(addon, toggleDisabledAction);
      if (addon.type === "theme") {
        document.l10n.setAttributes(
          toggleDisabledButton,
          `${toggleDisabledAction}-addon-button`
        );
      } else if (
        addon.type === "extension" ||
        addon.type === "sitepermission"
      ) {
        toggleDisabledButton.pressed = !addon.userDisabled;
      }
    }

    // Set the items in the more options menu.
    this.options.update(this, addon, this.updateInstall);

    // Badge the more options button if there's an update.
    let moreOptionsButton = card.querySelector(".more-options-button");
    moreOptionsButton.classList.toggle(
      "more-options-button-badged",
      !!(this.updateInstall && isInState(this.updateInstall, "available"))
    );

    // Postponed update addon card message bar.
    const hasPostponedInstall =
      this.updateInstall && isInState(this.updateInstall, "postponed");
    this.postponedMessageBar.hidden = !hasPostponedInstall;

    // Hide the more options button if it's empty.
    moreOptionsButton.hidden = this.options.visibleItems.length === 0;

    // Ensure all badges are initially hidden.
    for (let node of card.querySelectorAll(".addon-badge")) {
      node.hidden = true;
    }

    // Set the private browsing badge visibility.
    // TODO: We don't show the badge for SitePermsAddon for now, but this should
    // be handled in Bug 1799090.
    if (addon.incognito != "not_allowed" && addon.type == "extension") {
      // Keep update synchronous, the badge can appear later.
      isAllowedInPrivateBrowsing(addon).then(isAllowed => {
        card.querySelector(".addon-badge-private-browsing-allowed").hidden =
          !isAllowed;
      });
    }

    // Show the recommended badges if needed.
    // Plugins don't have recommendationStates, so ensure a default.
    let states = addon.recommendationStates || [];
    for (let badgeName of states) {
      let badge = card.querySelector(`.addon-badge-${badgeName}`);
      if (badge) {
        badge.hidden = false;
      }
    }

    // Update description.
    card.querySelector(".addon-description").textContent = addon.description;

    this.updateMessage();

    // Update the details if they're shown.
    if (this.details) {
      this.details.update();
    }

    this.sendEvent("update");
  }

  async updateMessage() {
    const messageBar = this.card.querySelector(".addon-card-message");

    const {
      linkUrl,
      linkId,
      linkSumoPage,
      messageId,
      messageArgs,
      type = "",
    } = await getAddonMessageInfo(this.addon, {
      isCardExpanded: this.expanded,
      isInDisabledSection:
        !this.expanded &&
        !!this.closest(`section.${this.addon.type}-disabled-section`),
    });

    if (messageId) {
      document.l10n.pauseObserving();
      document.l10n.setAttributes(messageBar, messageId, messageArgs);
      messageBar.setAttribute("data-l10n-attrs", "message");

      messageBar.innerHTML = "";
      if (linkUrl) {
        const linkButton = document.createElement("button");
        document.l10n.setAttributes(linkButton, linkId);
        linkButton.setAttribute("action", "link");
        linkButton.setAttribute("url", linkUrl);
        linkButton.setAttribute("slot", "actions");
        messageBar.append(linkButton);
      }

      if (linkSumoPage) {
        const sumoLinkEl = document.createElement("a", {
          is: "moz-support-link",
        });
        sumoLinkEl.setAttribute("support-page", linkSumoPage);
        sumoLinkEl.setAttribute("slot", "support-link");
        // Set a custom fluent id for the learn more if there
        // is one (otherwise moz-support-link custom element
        // will use the default "Learn more" localized string).
        if (linkId) {
          document.l10n.setAttributes(sumoLinkEl, linkId);
        }
        messageBar.append(sumoLinkEl);
      }

      document.l10n.resumeObserving();
      await document.l10n.translateFragment(messageBar);
      messageBar.setAttribute("type", type);
      messageBar.hidden = false;
    } else {
      messageBar.hidden = true;
    }
  }

  showPrefs() {
    this.details.showPrefs();
  }

  expand() {
    if (!this.children.length) {
      this.expanded = true;
    } else {
      throw new Error("expand() is only supported before render()");
    }
  }

  render() {
    this.textContent = "";

    let { addon } = this;
    if (!addon) {
      throw new Error("addon-card must be initialized with setAddon()");
    }

    this.setAttribute("addon-id", addon.id);

    this.card = importTemplate("card").firstElementChild;
    let headingId = ExtensionCommon.makeWidgetId(`${addon.id}-heading`);
    this.card.setAttribute("aria-labelledby", headingId);

    // Remove the toggle-disabled button(s) based on type.
    if (addon.type != "theme") {
      this.card.querySelector(".theme-enable-button").remove();
    }
    if (addon.type != "extension" && addon.type != "sitepermission") {
      this.card.querySelector(".extension-enable-button").remove();
    }

    let nameContainer = this.card.querySelector(".addon-name-container");
    let headingLevel = this.expanded ? "h1" : "h3";
    let nameHeading = document.createElement(headingLevel);
    nameHeading.classList.add("addon-name");
    nameHeading.id = headingId;
    if (!this.expanded) {
      let name = document.createElement("a");
      name.classList.add("addon-name-link");
      name.href = `addons://detail/${addon.id}`;
      nameHeading.appendChild(name);
      this.addonNameEl = name;
    } else {
      this.addonNameEl = nameHeading;
    }
    nameContainer.prepend(nameHeading);

    let panelType = addon.type == "plugin" ? "plugin-options" : "addon-options";
    this.options = document.createElement(panelType);
    this.options.render();
    this.card.appendChild(this.options);
    this.optionsButton = this.card.querySelector(".more-options-button");

    // Set the contents.
    this.update();

    let doneRenderPromise = Promise.resolve();
    if (this.expanded) {
      if (!this.details) {
        this.details = document.createElement("addon-details");
      }
      this.details.setAddon(this.addon);
      doneRenderPromise = this.details.render();

      // If we're re-rendering we still need to append the details since the
      // entire card was emptied at the beginning of the render.
      this.card.appendChild(this.details);
    }

    this.appendChild(this.card);

    if (this.expanded) {
      requestAnimationFrame(() => this.optionsButton.focus());
    }

    // Return the promise of details rendering to wait on in DetailView.
    return doneRenderPromise;
  }

  sendEvent(name, detail) {
    this.dispatchEvent(new CustomEvent(name, { detail }));
  }

  /**
   * AddonManager listener events.
   */

  onNewInstall(install) {
    this.updateInstall = install;
    this.sendEvent("update-found");
  }

  onInstallEnded(install) {
    this.setAddon(install.addon);
  }

  onInstallPostponed(install) {
    this.updateInstall = install;
    this.sendEvent("update-postponed");
  }

  onDisabled() {
    if (!this.reloading) {
      this.update();
    }
  }

  onEnabled() {
    this.reloading = false;
    this.update();
  }

  onInstalled() {
    // When a temporary addon is reloaded, onInstalled is triggered instead of
    // onEnabled.
    this.reloading = false;
    this.update();
  }

  onUninstalling() {
    // Dispatch a remove event, the DetailView is listening for this to get us
    // back to the list view when the current add-on is removed.
    this.sendEvent("remove");
  }

  onUpdateModeChanged() {
    this.update();
  }

  onPropertyChanged(addon, changed) {
    if (this.details && changed.includes("applyBackgroundUpdates")) {
      this.details.update();
    } else if (addon.type == "plugin" && changed.includes("userDisabled")) {
      this.update();
    }

    if (this.details && changed.includes("quarantineIgnoredByUser")) {
      this.details.updateQuarantinedDomainsUserAllowed();
    }

    if (changed.includes("blocklistState")) {
      this.update();
    }
  }

  /* Extension Permission change listener */
  async onChangePermissions(data) {
    let perms = data.added || data.removed;
    let hasAllSites = false;
    for (let permission of perms.permissions.concat(perms.origins)) {
      if (Extension.isAllSitesPermission(permission)) {
        hasAllSites = true;
        continue;
      }
      let target = document.querySelector(`[permission-key="${permission}"]`);
      let checked = !data.removed;
      if (target) {
        target.pressed = checked;
      }
    }
    if (hasAllSites) {
      // special-case for finding the all-sites target by attribute.
      let target = document.querySelector("[permission-all-sites]");
      let checked = await AddonCard.optionalAllSitesGranted(this.addon.id);
      target.pressed = checked;
    }
  }

  // Only covers optional_permissions in MV2 and all host permissions in MV3.
  static async optionalAllSitesGranted(addonId) {
    let granted = await ExtensionPermissions.get(addonId);
    return granted.origins.some(perm => Extension.isAllSitesPermission(perm));
  }
}
customElements.define("addon-card", AddonCard);

/**
 * A child element of `<recommended-addon-list>`. It should be initialized
 * by calling `setDiscoAddon()` first. Call `setAddon(addon)` if it has been
 * installed, and call `setAddon(null)` upon uninstall.
 *
 *    let discoAddon = new DiscoAddonWrapper({ ... });
 *    let card = document.createElement("recommended-addon-card");
 *    card.setDiscoAddon(discoAddon);
 *    document.body.appendChild(card);
 *
 *    AddonManager.getAddonsByID(discoAddon.id)
 *      .then(addon => card.setAddon(addon));
 */
class RecommendedAddonCard extends HTMLElement {
  /**
   * @param {DiscoAddonWrapper} addon
   *        The details of the add-on that should be rendered in the card.
   */
  setDiscoAddon(addon) {
    this.addonId = addon.id;

    // Save the information so we can install.
    this.discoAddon = addon;

    let card = importTemplate("card").firstElementChild;
    let heading = card.querySelector(".addon-name-container");
    heading.textContent = "";
    heading.append(importTemplate("addon-name-container-in-disco-card"));

    this.setCardContent(card, addon);
    if (addon.type != "theme") {
      card
        .querySelector(".addon-description")
        .append(importTemplate("addon-description-in-disco-card"));
      this.setCardDescription(card, addon);
    }
    this.registerButtons(card, addon);

    this.textContent = "";
    this.append(card);

    // We initially assume that the add-on is not installed.
    this.setAddon(null);
  }

  /**
   * Fills in all static parts of the card.
   *
   * @param {HTMLElement} card
   *        The primary content of this card.
   * @param {DiscoAddonWrapper} addon
   */
  setCardContent(card, addon) {
    // Set the icon.
    if (addon.type == "theme") {
      card.querySelector(".addon-icon").hidden = true;
    } else {
      card.querySelector(".addon-icon").src = AddonManager.getPreferredIconURL(
        addon,
        32,
        window
      );
    }

    // Set the theme preview.
    let preview = card.querySelector(".card-heading-image");
    if (addon.type == "theme") {
      let screenshotUrl = getScreenshotUrlForAddon(addon);
      if (screenshotUrl) {
        preview.src = screenshotUrl;
        preview.hidden = false;
      }
    } else {
      preview.hidden = true;
    }

    // Set the name.
    card.querySelector(".disco-addon-name").textContent = addon.name;

    // Set the author name and link to AMO.
    if (addon.creator) {
      let authorInfo = card.querySelector(".disco-addon-author");
      document.l10n.setAttributes(authorInfo, "created-by-author", {
        author: addon.creator.name,
      });
      // This is intentionally a link to the add-on listing instead of the
      // author page, because the add-on listing provides more relevant info.
      authorInfo.querySelector("a").href = formatUTMParams(
        "discopane-entry-link",
        addon.amoListingUrl
      );
      authorInfo.hidden = false;
    }
  }

  setCardDescription(card, addon) {
    // Set the description. Note that this is the editorial description, not
    // the add-on's original description that would normally appear on a card.
    card.querySelector(".disco-description-main").textContent =
      addon.editorialDescription;

    let hasStats = false;
    if (addon.averageRating) {
      hasStats = true;
      card.querySelector("moz-five-star").rating = addon.averageRating;
    } else {
      card.querySelector("moz-five-star").hidden = true;
    }

    if (addon.dailyUsers) {
      hasStats = true;
      let userCountElem = card.querySelector(".disco-user-count");
      document.l10n.setAttributes(userCountElem, "user-count", {
        dailyUsers: addon.dailyUsers,
      });
    }

    card.querySelector(".disco-description-statistics").hidden = !hasStats;
  }

  registerButtons(card, addon) {
    let installButton = card.querySelector("[action='install-addon']");
    if (addon.type == "theme") {
      document.l10n.setAttributes(installButton, "install-theme-button");
    } else {
      document.l10n.setAttributes(installButton, "install-extension-button");
    }

    this.addEventListener("click", this);
  }

  handleEvent(event) {
    let action = event.target.getAttribute("action");
    switch (action) {
      case "install-addon":
        this.installDiscoAddon();
        break;
      case "manage-addon":
        gViewController.loadView(`detail/${this.addonId}`);
        break;
    }
  }

  async installDiscoAddon() {
    let addon = this.discoAddon;
    let url = addon.sourceURI.spec;
    let install = await AddonManager.getInstallForURL(url, {
      name: addon.name,
      telemetryInfo: {
        source: "disco",
        taarRecommended: addon.taarRecommended,
      },
    });
    // We are hosted in a <browser> in about:addons, but we can just use the
    // main tab's browser since all of it is using the system principal.
    let browser = window.docShell.chromeEventHandler;
    AddonManager.installAddonFromWebpage(
      "application/x-xpinstall",
      browser,
      Services.scriptSecurityManager.getSystemPrincipal(),
      install
    );
  }

  /**
   * @param {AddonWrapper|null} addon
   *        The add-on that has been installed; null if it has been removed.
   */
  setAddon(addon) {
    let card = this.firstElementChild;
    card.querySelector("[action='install-addon']").hidden = !!addon;
    card.querySelector("[action='manage-addon']").hidden = !addon;

    this.dispatchEvent(new CustomEvent("disco-card-updated")); // For testing.
  }
}
customElements.define("recommended-addon-card", RecommendedAddonCard);

/**
 * A list view for add-ons of a certain type. It should be initialized with the
 * type of add-on to render and have section data set before being connected to
 * the document.
 *
 *    let list = document.createElement("addon-list");
 *    list.type = "plugin";
 *    list.setSections([{
 *      headingId: "plugin-section-heading",
 *      filterFn: addon => !addon.isSystem,
 *    }]);
 *    document.body.appendChild(list);
 */
class AddonList extends HTMLElement {
  constructor() {
    super();
    this.sections = [];
    this.pendingUninstallAddons = new Set();
    this._addonsToUpdate = new Set();
    this._userFocusListenersAdded = false;
  }

  async connectedCallback() {
    // Register the listener and get the add-ons, these operations should
    // happpen as close to each other as possible.
    this.registerListener();
    // Don't render again if we were rendered prior to being inserted.
    if (!this.children.length) {
      // Render the initial view.
      this.render();
    }
  }

  disconnectedCallback() {
    // Remove content and stop listening until this is connected again.
    this.textContent = "";
    this.removeListener();

    // Process any pending uninstall related to this list.
    for (const addon of this.pendingUninstallAddons) {
      if (isPending(addon, "uninstall")) {
        addon.uninstall();
      }
    }
    this.pendingUninstallAddons.clear();
  }

  /**
   * Configure the sections in the list.
   *
   * @param {object[]} sections
   *        The options for the section. Each entry in the array should have:
   *          headingId: The fluent id for the section's heading.
   *          filterFn: A function that determines if an add-on belongs in
   *                    the section.
   */
  setSections(sections) {
    this.sections = sections.map(section => Object.assign({}, section));
  }

  /**
   * Set the add-on type for this list. This will be used to filter the add-ons
   * that are displayed.
   *
   * @param {string} val The type to filter on.
   */
  set type(val) {
    this.setAttribute("type", val);
  }

  get type() {
    return this.getAttribute("type");
  }

  getSection(index) {
    return this.sections[index].node;
  }

  getCards(section) {
    return section.querySelectorAll("addon-card");
  }

  getCard(addon) {
    return this.querySelector(`addon-card[addon-id="${addon.id}"]`);
  }

  getPendingUninstallBar(addon) {
    return this.querySelector(`moz-message-bar[addon-id="${addon.id}"]`);
  }

  sortByFn(aAddon, bAddon) {
    return aAddon.name.localeCompare(bAddon.name);
  }

  async getAddons() {
    if (!this.type) {
      throw new Error(`type must be set to find add-ons`);
    }

    // Find everything matching our type, null will find all types.
    let type = this.type == "all" ? null : [this.type];
    let addons = await AddonManager.getAddonsByTypes(type);

    if (type == "theme") {
      await BuiltInThemes.ensureBuiltInThemes();
    }

    // Put the add-ons into the sections, an add-on goes in the first section
    // that it matches the filterFn for. It might not go in any section.
    let sectionedAddons = this.sections.map(() => []);
    for (let addon of addons) {
      let index = this.sections.findIndex(({ filterFn }) => filterFn(addon));
      if (index != -1) {
        sectionedAddons[index].push(addon);
      } else if (isPending(addon, "uninstall")) {
        // A second tab may be opened on "about:addons" (or Firefox may
        // have crashed) while there are still "pending uninstall" add-ons.
        // Ensure to list them in the pendingUninstall message-bar-stack
        // when the AddonList is initially rendered.
        this.pendingUninstallAddons.add(addon);
      }
    }

    // Sort the add-ons in each section.
    for (let [index, section] of sectionedAddons.entries()) {
      let sortByFn = this.sections[index].sortByFn || this.sortByFn;
      section.sort(sortByFn);
    }

    return sectionedAddons;
  }

  createPendingUninstallStack() {
    const stack = document.createElement("message-bar-stack");
    stack.setAttribute("class", "pending-uninstall");
    stack.setAttribute("reverse", "");
    return stack;
  }

  addPendingUninstallBar(addon) {
    const stack = this.pendingUninstallStack;
    const mb = document.createElement("moz-message-bar");
    mb.setAttribute("addon-id", addon.id);
    mb.setAttribute("type", "info");

    const undo = document.createElement("button");
    undo.setAttribute("action", "undo");
    undo.addEventListener("click", () => {
      addon.cancelUninstall();
    });
    undo.setAttribute("slot", "actions");

    document.l10n.setAttributes(mb, "pending-uninstall-description2", {
      addon: addon.name,
    });
    mb.setAttribute("data-l10n-attrs", "message");
    document.l10n.setAttributes(undo, "pending-uninstall-undo-button");

    mb.appendChild(undo);
    stack.append(mb);
  }

  removePendingUninstallBar(addon) {
    const messagebar = this.getPendingUninstallBar(addon);
    if (messagebar) {
      messagebar.remove();
    }
  }

  createSectionHeading(headingIndex) {
    let { headingId, subheadingId } = this.sections[headingIndex];
    let frag = document.createDocumentFragment();
    let heading = document.createElement("h2");
    heading.classList.add("list-section-heading");
    document.l10n.setAttributes(heading, headingId);
    frag.append(heading);

    if (subheadingId) {
      heading.className = "header-name";
      let subheading = document.createElement("h3");
      subheading.classList.add("list-section-subheading");
      document.l10n.setAttributes(subheading, subheadingId);
      frag.append(subheading);
    }

    return frag;
  }

  createEmptyListMessage() {
    let emptyMessage = "list-empty-get-extensions-message";
    let linkPref = "extensions.getAddons.link.url";

    if (this.sections && this.sections.length) {
      if (this.sections[0].headingId == "locale-enabled-heading") {
        emptyMessage = "list-empty-get-language-packs-message";
        linkPref = "browser.dictionaries.download.url";
      } else if (this.sections[0].headingId == "dictionary-enabled-heading") {
        emptyMessage = "list-empty-get-dictionaries-message";
        linkPref = "browser.dictionaries.download.url";
      }
    }

    let messageContainer = document.createElement("p");
    messageContainer.id = "empty-addons-message";
    let a = document.createElement("a");
    a.href = Services.urlFormatter.formatURLPref(linkPref);
    a.setAttribute("target", "_blank");
    a.setAttribute("data-l10n-name", "get-extensions");
    document.l10n.setAttributes(messageContainer, emptyMessage, {
      domain: a.hostname,
    });
    messageContainer.appendChild(a);
    return messageContainer;
  }

  updateSectionIfEmpty(section) {
    // Clear the entire list if there are no `addon-card` childrens.
    if (!section.querySelectorAll("addon-card").length) {
      section.textContent = "";
    }
  }

  insertCardInto(card, sectionIndex) {
    let section = this.getSection(sectionIndex);
    let sectionCards = this.getCards(section);

    // If this is the first card in the section, create the heading.
    if (!sectionCards.length) {
      section.appendChild(this.createSectionHeading(sectionIndex));
    }

    // Find where to insert the card.
    let insertBefore = Array.from(sectionCards).find(
      otherCard => this.sortByFn(card.addon, otherCard.addon) < 0
    );
    // This will append if insertBefore is null.
    section.insertBefore(card, insertBefore || null);
  }

  addAddon(addon) {
    // Only insert add-ons of the right type.
    if (addon.type != this.type && this.type != "all") {
      this.sendEvent("skip-add", "type-mismatch");
      return;
    }

    let insertSection = this._addonSectionIndex(addon);

    // Don't add the add-on if it doesn't go in a section.
    if (insertSection == -1) {
      return;
    }

    // Create and insert the card.
    let card = document.createElement("addon-card");
    card.setAddon(addon);
    this.insertCardInto(card, insertSection);
    this.sendEvent("add", { id: addon.id });
  }

  sendEvent(name, detail) {
    this.dispatchEvent(new CustomEvent(name, { detail }));
  }

  removeAddon(addon) {
    let card = this.getCard(addon);
    if (card) {
      let section = card.parentNode;
      card.remove();
      this.updateSectionIfEmpty(section);
      this.sendEvent("remove", { id: addon.id });
    }
  }

  updateAddon(addon) {
    if (!this.getCard(addon)) {
      // Try to add the add-on right away.
      this.addAddon(addon);
    } else if (this._addonSectionIndex(addon) == -1) {
      // Try to remove the add-on right away.
      this._updateAddon(addon);
    } else if (this.isUserFocused) {
      // Queue up a change for when the focus is cleared.
      this.updateLater(addon);
    } else {
      // Not currently focused, make the change now.
      this.withCardAnimation(() => this._updateAddon(addon));
    }
  }

  updateLater(addon) {
    this._addonsToUpdate.add(addon);
    this._addUserFocusListeners();
  }

  _addUserFocusListeners() {
    if (this._userFocusListenersAdded) {
      return;
    }

    this._userFocusListenersAdded = true;
    this.addEventListener("mouseleave", this);
    this.addEventListener("hidden", this, true);
    this.addEventListener("focusout", this);
  }

  _removeUserFocusListeners() {
    if (!this._userFocusListenersAdded) {
      return;
    }

    this.removeEventListener("mouseleave", this);
    this.removeEventListener("hidden", this, true);
    this.removeEventListener("focusout", this);
    this._userFocusListenersAdded = false;
  }

  get hasMenuOpen() {
    return !!this.querySelector("panel-list[open]");
  }

  get isUserFocused() {
    return this.matches(":hover, :focus-within") || this.hasMenuOpen;
  }

  update() {
    if (this._addonsToUpdate.size) {
      this.withCardAnimation(() => {
        for (let addon of this._addonsToUpdate) {
          this._updateAddon(addon);
        }
        this._addonsToUpdate = new Set();
      });
    }
  }

  _getChildCoords() {
    let results = new Map();
    for (let child of this.querySelectorAll("addon-card")) {
      results.set(child, child.getBoundingClientRect());
    }
    return results;
  }

  withCardAnimation(changeFn) {
    if (shouldSkipAnimations()) {
      changeFn();
      return;
    }

    let origChildCoords = this._getChildCoords();

    changeFn();

    let newChildCoords = this._getChildCoords();
    let cards = this.querySelectorAll("addon-card");
    let transitionCards = [];
    for (let card of cards) {
      let orig = origChildCoords.get(card);
      let moved = newChildCoords.get(card);
      let changeY = moved.y - (orig || moved).y;
      let cardEl = card.firstElementChild;

      if (changeY != 0) {
        cardEl.style.transform = `translateY(${changeY * -1}px)`;
        transitionCards.push(card);
      }
    }
    requestAnimationFrame(() => {
      for (let card of transitionCards) {
        card.firstElementChild.style.transition = "transform 125ms";
      }

      requestAnimationFrame(() => {
        for (let card of transitionCards) {
          let cardEl = card.firstElementChild;
          cardEl.style.transform = "";
          cardEl.addEventListener("transitionend", function handler(e) {
            if (e.target == cardEl && e.propertyName == "transform") {
              cardEl.style.transition = "";
              cardEl.removeEventListener("transitionend", handler);
            }
          });
        }
      });
    });
  }

  _addonSectionIndex(addon) {
    return this.sections.findIndex(s => s.filterFn(addon));
  }

  _updateAddon(addon) {
    let card = this.getCard(addon);
    if (card) {
      let sectionIndex = this._addonSectionIndex(addon);
      if (sectionIndex != -1) {
        // Move the card, if needed. This will allow an animation between
        // page sections and provides clearer events for testing.
        if (card.parentNode.getAttribute("section") != sectionIndex) {
          let { activeElement } = document;
          let refocus = card.contains(activeElement);
          let oldSection = card.parentNode;
          this.insertCardInto(card, sectionIndex);
          this.updateSectionIfEmpty(oldSection);
          if (refocus) {
            activeElement.focus();
          }
          this.sendEvent("move", { id: addon.id });
        }
      } else {
        this.removeAddon(addon);
      }
    }
  }

  renderSection(addons, index) {
    const { sectionClass } = this.sections[index];

    let section = document.createElement("section");
    section.setAttribute("section", index);
    if (sectionClass) {
      section.setAttribute("class", sectionClass);
    }

    // Render the heading and add-ons if there are any, except for mlmodel list
    // view which only shows installed models.
    if (this.type != "mlmodel" && addons.length) {
      section.appendChild(this.createSectionHeading(index));
    }

    for (let addon of addons) {
      let card = document.createElement("addon-card");
      card.setAddon(addon);
      card.render();
      section.appendChild(card);
    }

    return section;
  }

  async render() {
    this.textContent = "";

    let sectionedAddons = await this.getAddons();

    let frag = document.createDocumentFragment();

    // Render the pending uninstall message-bar-stack.
    this.pendingUninstallStack = this.createPendingUninstallStack();
    for (let addon of this.pendingUninstallAddons) {
      this.addPendingUninstallBar(addon);
    }
    frag.appendChild(this.pendingUninstallStack);

    if (this.type == "mlmodel") {
      frag.appendChild(document.createElement("mlmodel-list-intro"));
    }

    // Render the sections.
    for (let i = 0; i < sectionedAddons.length; i++) {
      this.sections[i].node = this.renderSection(sectionedAddons[i], i);
      frag.appendChild(this.sections[i].node);
    }

    // Add the "empty list message" elements (but omit it in the list view
    // related to the "mlmodel" type).
    if (this.type != "mlmodel") {
      // Render the placeholder that is shown when all sections are empty.
      // This call is after rendering the sections, because its visibility
      // is controlled through the general sibling combinator relative to
      // the sections (section ~).
      let message = this.createEmptyListMessage();
      frag.appendChild(message);
    }

    // Make sure fluent has set all the strings before we render. This will
    // avoid the height changing as strings go from 0 height to having text.
    await document.l10n.translateFragment(frag);
    this.appendChild(frag);
  }

  registerListener() {
    AddonManagerListenerHandler.addListener(this);
  }

  removeListener() {
    AddonManagerListenerHandler.removeListener(this);
  }

  handleEvent(e) {
    if (!this.isUserFocused || (e.type == "mouseleave" && !this.hasMenuOpen)) {
      this._removeUserFocusListeners();
      this.update();
    }
  }

  /**
   * AddonManager listener events.
   */

  onOperationCancelled(addon) {
    if (
      this.pendingUninstallAddons.has(addon) &&
      !isPending(addon, "uninstall")
    ) {
      this.pendingUninstallAddons.delete(addon);
      this.removePendingUninstallBar(addon);
    }
    this.updateAddon(addon);
  }

  onEnabled(addon) {
    this.updateAddon(addon);
  }

  onDisabled(addon) {
    this.updateAddon(addon);
  }

  onUninstalling(addon) {
    if (
      isPending(addon, "uninstall") &&
      (this.type === "all" || addon.type === this.type)
    ) {
      this.pendingUninstallAddons.add(addon);
      this.addPendingUninstallBar(addon);
      this.updateAddon(addon);
    }
  }

  onInstalled(addon) {
    if (this.querySelector(`addon-card[addon-id="${addon.id}"]`)) {
      return;
    }
    this.addAddon(addon);
  }

  onUninstalled(addon) {
    this.pendingUninstallAddons.delete(addon);
    this.removePendingUninstallBar(addon);
    this.removeAddon(addon);
  }
}
customElements.define("addon-list", AddonList);

class RecommendedAddonList extends HTMLElement {
  connectedCallback() {
    if (this.isConnected) {
      this.loadCardsIfNeeded();
      this.updateCardsWithAddonManager();
    }
    AddonManagerListenerHandler.addListener(this);
  }

  disconnectedCallback() {
    AddonManagerListenerHandler.removeListener(this);
  }

  get type() {
    return this.getAttribute("type");
  }

  /**
   * Set the add-on type for this list. This will be used to filter the add-ons
   * that are displayed.
   *
   * Must be set prior to the first render.
   *
   * @param {string} val The type to filter on.
   */
  set type(val) {
    this.setAttribute("type", val);
  }

  get hideInstalled() {
    return this.hasAttribute("hide-installed");
  }

  /**
   * Set whether installed add-ons should be hidden from the list. If false,
   * installed add-ons will be shown with a "Manage" button, otherwise they
   * will be hidden.
   *
   * Must be set prior to the first render.
   *
   * @param {boolean} val Whether to show installed add-ons.
   */
  set hideInstalled(val) {
    this.toggleAttribute("hide-installed", val);
  }

  getCardById(addonId) {
    for (let card of this.children) {
      if (card.addonId === addonId) {
        return card;
      }
    }
    return null;
  }

  setAddonForCard(card, addon) {
    card.setAddon(addon);

    let wasHidden = card.hidden;
    card.hidden = this.hideInstalled && addon;

    if (wasHidden != card.hidden) {
      let eventName = card.hidden ? "card-hidden" : "card-shown";
      this.dispatchEvent(new CustomEvent(eventName, { detail: { card } }));
    }
  }

  /**
   * Whether the client ID should be preferred. This is disabled for themes
   * since they don't use the telemetry data and don't show the TAAR notice.
   */
  get preferClientId() {
    return !this.type || this.type == "extension";
  }

  async updateCardsWithAddonManager() {
    let cards = Array.from(this.children);
    let addonIds = cards.map(card => card.addonId);
    let addons = await AddonManager.getAddonsByIDs(addonIds);
    for (let [i, card] of cards.entries()) {
      let addon = addons[i];
      this.setAddonForCard(card, addon);
      if (addon) {
        // Already installed, move card to end.
        this.append(card);
      }
    }
  }

  async loadCardsIfNeeded() {
    // Use promise as guard. Also used by tests to detect when load completes.
    if (!this.cardsReady) {
      this.cardsReady = this._loadCards();
    }
    return this.cardsReady;
  }

  async _loadCards() {
    let recommendedAddons;
    try {
      recommendedAddons = await DiscoveryAPI.getResults(this.preferClientId);
    } catch (e) {
      return;
    }

    let frag = document.createDocumentFragment();
    for (let addon of recommendedAddons) {
      if (this.type && addon.type != this.type) {
        continue;
      }
      let card = document.createElement("recommended-addon-card");
      card.setDiscoAddon(addon);
      frag.append(card);
    }
    this.append(frag);
    await this.updateCardsWithAddonManager();
  }

  /**
   * AddonManager listener events.
   */

  onInstalled(addon) {
    let card = this.getCardById(addon.id);
    if (card) {
      this.setAddonForCard(card, addon);
    }
  }

  onUninstalled(addon) {
    let card = this.getCardById(addon.id);
    if (card) {
      this.setAddonForCard(card, null);
    }
  }
}
customElements.define("recommended-addon-list", RecommendedAddonList);

class ColorwayRemovalNotice extends HTMLElement {
  connectedCallback() {
    // The pref CLEANUP_PREF is set by the
    // ColorwayThemeMigration.sys.mjs. We show the notice only if, during the
    // colorway theme uninstall, we detect some colorway builtin theme.
    if (
      Services.prefs.getIntPref(
        ColorwayThemeMigration.CLEANUP_PREF,
        ColorwayThemeMigration.CLEANUP_UNKNOWN
      ) != ColorwayThemeMigration.CLEANUP_COMPLETED_WITH_BUILTIN
    ) {
      return;
    }

    this.appendChild(importTemplate("colorway-removal-notice"));
    this.addEventListener("click", this);
    this.messageBar = this.querySelector("moz-message-bar");
    this.messageBar.addEventListener("message-bar:user-dismissed", this);
  }

  handleEvent(e) {
    if (e.type === "message-bar:user-dismissed") {
      Services.prefs.setIntPref(
        ColorwayThemeMigration.CLEANUP_PREF,
        ColorwayThemeMigration.CLEANUP_COMPLETED
      );
    }

    if (
      e.type === "click" &&
      e.target.getAttribute("action") === "open-amo-colorway-collection"
    ) {
      openAmoInTab(this, "collections/4757633/colorways");
    }
  }
}
customElements.define("colorway-removal-notice", ColorwayRemovalNotice);

class TaarMessageBar extends HTMLElement {
  connectedCallback() {
    this.hidden =
      Services.prefs.getBoolPref(PREF_RECOMMENDATION_HIDE_NOTICE, false) ||
      !DiscoveryAPI.clientIdDiscoveryEnabled;
    if (this.childElementCount == 0 && !this.hidden) {
      this.appendChild(importTemplate("taar-notice"));
      this.addEventListener("click", this);
      this.messageBar = this.querySelector("moz-message-bar");
      this.messageBar.addEventListener("message-bar:user-dismissed", this);
    }
  }

  handleEvent(e) {
    if (e.type == "message-bar:user-dismissed") {
      Services.prefs.setBoolPref(PREF_RECOMMENDATION_HIDE_NOTICE, true);
    }
  }
}
customElements.define("taar-notice", TaarMessageBar);

class RecommendedFooter extends HTMLElement {
  connectedCallback() {
    if (this.childElementCount == 0) {
      this.appendChild(importTemplate("recommended-footer"));
      this.querySelector(".privacy-policy-link").href =
        Services.prefs.getStringPref(PREF_PRIVACY_POLICY_URL);
      this.addEventListener("click", this);
    }
  }

  handleEvent(event) {
    let action = event.target.getAttribute("action");
    switch (action) {
      case "open-amo":
        openAmoInTab(this);
        break;
    }
  }
}
customElements.define("recommended-footer", RecommendedFooter, {
  extends: "footer",
});

class RecommendedThemesFooter extends HTMLElement {
  connectedCallback() {
    if (this.childElementCount == 0) {
      this.appendChild(importTemplate("recommended-themes-footer"));
      let themeRecommendationRow = this.querySelector(".theme-recommendation");
      let themeRecommendationUrl = Services.prefs.getStringPref(
        PREF_THEME_RECOMMENDATION_URL
      );
      if (themeRecommendationUrl) {
        themeRecommendationRow.querySelector("a").href = themeRecommendationUrl;
      }
      themeRecommendationRow.hidden = !themeRecommendationUrl;
      this.addEventListener("click", this);
    }
  }

  handleEvent(event) {
    let action = event.target.getAttribute("action");
    switch (action) {
      case "open-amo":
        openAmoInTab(this, "themes");
        break;
    }
  }
}
customElements.define("recommended-themes-footer", RecommendedThemesFooter, {
  extends: "footer",
});

/**
 * This element will handle showing recommendations with a
 * <recommended-addon-list> and a <footer>. The footer will be hidden until
 * the <recommended-addon-list> is done making its request so the footer
 * doesn't move around.
 *
 * Subclass this element to use it and define a `template` property to pull
 * the template from. Expected template:
 *
 * <h1>My extra content can go here.</h1>
 * <p>It can be anything but a footer or recommended-addon-list.</p>
 * <recommended-addon-list></recommended-addon-list>
 * <footer>My custom footer</footer>
 */
class RecommendedSection extends HTMLElement {
  connectedCallback() {
    if (this.childElementCount == 0) {
      this.render();
    }
  }

  get list() {
    return this.querySelector("recommended-addon-list");
  }

  get footer() {
    return this.querySelector("footer");
  }

  render() {
    this.appendChild(importTemplate(this.template));

    // Hide footer until the cards are loaded, to prevent the content from
    // suddenly shifting when the user attempts to interact with it.
    let { footer } = this;
    footer.hidden = true;
    this.list.loadCardsIfNeeded().finally(() => {
      footer.hidden = false;
    });
  }
}

class RecommendedExtensionsSection extends RecommendedSection {
  get template() {
    return "recommended-extensions-section";
  }
}
customElements.define(
  "recommended-extensions-section",
  RecommendedExtensionsSection
);

class RecommendedThemesSection extends RecommendedSection {
  get template() {
    return "recommended-themes-section";
  }
}
customElements.define("recommended-themes-section", RecommendedThemesSection);

class DiscoveryPane extends RecommendedSection {
  get template() {
    return "discopane";
  }
}
customElements.define("discovery-pane", DiscoveryPane);

// Define views
gViewController.defineView("list", async type => {
  if (!AddonManager.hasAddonType(type)) {
    return null;
  }

  let frag = document.createDocumentFragment();
  let list = document.createElement("addon-list");
  list.type = type;

  let sections = [
    {
      headingId: type + "-enabled-heading",
      sectionClass: `${type}-enabled-section`,
      filterFn: addon =>
        !addon.hidden && addon.isActive && !isPending(addon, "uninstall"),
    },
  ];

  const disabledAddonsFilterFn = addon =>
    !addon.hidden && !addon.isActive && !isPending(addon, "uninstall");

  sections.push({
    headingId: getL10nIdMapping(`${type}-disabled-heading`),
    sectionClass: `${type}-disabled-section`,
    filterFn: disabledAddonsFilterFn,
  });

  // Show the colorway warning only in themes list view
  if (type === "theme") {
    const warning = document.createElement("colorway-removal-notice");
    frag.appendChild(warning);
  }

  list.setSections(sections);
  frag.appendChild(list);

  // Show recommendations for themes and extensions.
  if (
    LIST_RECOMMENDATIONS_ENABLED &&
    (type == "extension" || type == "theme")
  ) {
    let elementName =
      type == "extension"
        ? "recommended-extensions-section"
        : "recommended-themes-section";
    let recommendations = document.createElement(elementName);
    // Start loading the recommendations. This can finish after the view load
    // event is sent.
    recommendations.render();
    frag.appendChild(recommendations);
  }

  await list.render();

  return frag;
});

gViewController.defineView("detail", async param => {
  let [id, selectedTab] = param.split("/");
  let addon = await AddonManager.getAddonByID(id);

  if (!addon) {
    return null;
  }

  let card = document.createElement("addon-card");

  // Ensure the category for this add-on type is selected.
  document.querySelector("categories-box").selectType(addon.type);

  // Go back to the list view when the add-on is removed.
  card.addEventListener("remove", () =>
    gViewController.loadView(`list/${addon.type}`)
  );

  card.setAddon(addon);
  card.expand();
  await card.render();
  if (selectedTab === "preferences" && (await isAddonOptionsUIAllowed(addon))) {
    card.showPrefs();
  }

  return card;
});

gViewController.defineView("updates", async param => {
  let list = document.createElement("addon-list");
  list.type = "all";
  if (param == "available") {
    list.setSections([
      {
        headingId: "available-updates-heading",
        filterFn: addon => {
          // Filter the addons visible in the updates view using the same
          // criteria that is being used to compute the counter on the
          // available updates category button badge.
          const install = getUpdateInstall(addon);
          return install && isManualUpdate(install) && !install.installed;
        },
      },
    ]);
  } else if (param == "recent") {
    list.sortByFn = (a, b) => {
      if (a.updateDate > b.updateDate) {
        return -1;
      }
      if (a.updateDate < b.updateDate) {
        return 1;
      }
      return 0;
    };
    let updateLimit = new Date() - UPDATES_RECENT_TIMESPAN;
    list.setSections([
      {
        headingId: "recent-updates-heading",
        filterFn: addon =>
          !addon.hidden && addon.updateDate && addon.updateDate > updateLimit,
      },
    ]);
  } else {
    throw new Error(`Unknown updates view ${param}`);
  }

  await list.render();
  return list;
});

gViewController.defineView("discover", async () => {
  let discopane = document.createElement("discovery-pane");
  discopane.render();
  await document.l10n.translateFragment(discopane);
  return discopane;
});

gViewController.defineView("shortcuts", async extensionId => {
  // Force the extension category to be selected, in the case of a reload,
  // restart, or if the view was opened from another category's page.
  document.querySelector("categories-box").selectType("extension");

  let view = document.createElement("addon-shortcuts");
  if (extensionId && extensionId !== "shortcuts") {
    view.setAttribute("extension-id", extensionId);
  }
  await view.render();
  await document.l10n.translateFragment(view);
  return view;
});

/**
 * @param {Element} el The button element.
 */
function openAmoInTab(el, path) {
  let amoUrl = Services.urlFormatter.formatURLPref(
    "extensions.getAddons.link.url"
  );

  if (path) {
    amoUrl += path;
  }

  amoUrl = formatUTMParams("find-more-link-bottom", amoUrl);
  windowRoot.ownerGlobal.openTrustedLinkIn(amoUrl, "tab");
}

/**
 * Called when about:addons is loaded.
 */
async function initialize() {
  window.addEventListener(
    "unload",
    () => {
      // Clear out the document so the disconnectedCallback will trigger
      // properly and all of the custom elements can cleanup.
      document.body.textContent = "";
      AddonManagerListenerHandler.shutdown();
    },
    { once: true }
  );

  // Init UI and view management
  gViewController.initialize(document.getElementById("main"));

  document.querySelector("categories-box").initialize();
  AddonManagerListenerHandler.startup();

  // browser.js may call loadView here if it expects an EM-loaded notification
  gViewController.notifyEMLoaded();

  // Select an initial view if no listener has set one so far
  if (!gViewController.currentViewId) {
    if (history.state) {
      // If there is a history state to restore then use that
      await gViewController.renderState(history.state);
    } else {
      // Fallback to the last category or first valid category view otherwise.
      await gViewController.loadView(
        Services.prefs.getStringPref(
          PREF_UI_LASTCATEGORY,
          gViewController.defaultViewId
        )
      );
    }
  }
}

window.promiseInitialized = new Promise(resolve => {
  window.addEventListener(
    "load",
    () => {
      initialize().then(resolve);
    },
    { once: true }
  );
});
