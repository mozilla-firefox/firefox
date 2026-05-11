/* - This Source Code Form is subject to the terms of the Mozilla Public
   - License, v. 2.0. If a copy of the MPL was not distributed with this file,
   - You can obtain one at http://mozilla.org/MPL/2.0/. */

// Import globals from the files imported by the .xul files.
/* import-globals-from main.js */
/* import-globals-from home.js */
/* import-globals-from search.js */
/* import-globals-from containers.js */
/* import-globals-from privacy.js */
/* import-globals-from sync.js */
/* import-globals-from moreFromMozilla.js */
/* import-globals-from findInPage.js */
/* import-globals-from /browser/base/content/utilityOverlay.js */
/* import-globals-from /toolkit/content/preferencesBindings.js */

/** @import MozButton from "chrome://global/content/elements/moz-button.mjs" */
/** @import {SettingConfig, SettingEmitChange} from "chrome://global/content/preferences/Setting.mjs" */
/** @import {SettingControlConfig, SettingOptionConfig} from "chrome://browser/content/preferences/widgets/setting-control.mjs" */
/** @import {SettingGroup} from "chrome://browser/content/preferences/widgets/setting-group.mjs" */
/** @import {SettingPane, SettingPaneConfig} from "chrome://browser/content/preferences/widgets/setting-pane.mjs" */

"use strict";

var { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

var { Downloads } = ChromeUtils.importESModule(
  "resource://gre/modules/Downloads.sys.mjs"
);
var { Integration } = ChromeUtils.importESModule(
  "resource://gre/modules/Integration.sys.mjs"
);
/* global DownloadIntegration */
Integration.downloads.defineESModuleGetter(
  this,
  "DownloadIntegration",
  "resource://gre/modules/DownloadIntegration.sys.mjs"
);

var { PrivateBrowsingUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/PrivateBrowsingUtils.sys.mjs"
);

var { Weave } = ChromeUtils.importESModule(
  "resource://services-sync/main.sys.mjs"
);

var { FxAccounts, getFxAccountsSingleton } = ChromeUtils.importESModule(
  "resource://gre/modules/FxAccounts.sys.mjs"
);
var fxAccounts = getFxAccountsSingleton();

var TAB_SESSION_ID = crypto.randomUUID();

XPCOMUtils.defineLazyServiceGetters(this, {
  gApplicationUpdateService: [
    "@mozilla.org/updates/update-service;1",
    Ci.nsIApplicationUpdateService,
  ],

  listManager: [
    "@mozilla.org/url-classifier/listmanager;1",
    Ci.nsIUrlListManager,
  ],
  gHandlerService: [
    "@mozilla.org/uriloader/handler-service;1",
    Ci.nsIHandlerService,
  ],
  gMIMEService: ["@mozilla.org/mime;1", Ci.nsIMIMEService],
});

if (Cc["@mozilla.org/gio-service;1"]) {
  XPCOMUtils.defineLazyServiceGetter(
    this,
    "gGIOService",
    "@mozilla.org/gio-service;1",
    Ci.nsIGIOService
  );
} else {
  this.gGIOService = null;
}

ChromeUtils.defineESModuleGetters(this, {
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  ContextualIdentityService:
    "resource://gre/modules/ContextualIdentityService.sys.mjs",
  DownloadUtils: "resource://gre/modules/DownloadUtils.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  ExtensionPreferencesManager:
    "resource://gre/modules/ExtensionPreferencesManager.sys.mjs",
  ExtensionSettingsStore:
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  FirefoxRelay: "resource://gre/modules/FirefoxRelay.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  LangPackMatcher: "resource://gre/modules/LangPackMatcher.sys.mjs",
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  OSKeyStore: "resource://gre/modules/OSKeyStore.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  SelectionChangedMenulist:
    "resource:///modules/SelectionChangedMenulist.sys.mjs",
  ShortcutUtils: "resource://gre/modules/ShortcutUtils.sys.mjs",
  SiteDataManager: "resource:///modules/SiteDataManager.sys.mjs",
  TransientPrefs: "resource:///modules/TransientPrefs.sys.mjs",
  UIState: "resource://services-sync/UIState.sys.mjs",
  UpdateUtils: "resource://gre/modules/UpdateUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(this, "gSubDialog", function () {
  const { SubDialogManager } = ChromeUtils.importESModule(
    "resource://gre/modules/SubDialog.sys.mjs"
  );
  return new SubDialogManager({
    dialogStack: document.getElementById("dialogStack"),
    dialogTemplate: document.getElementById("dialogTemplate"),
    dialogOptions: {
      styleSheets: [
        "chrome://browser/skin/preferences/dialog.css",
        "chrome://browser/skin/preferences/preferences.css",
      ],
      resizeCallback: async ({ title, frame }) => {
        // Search within main document and highlight matched keyword.
        await gSearchResultsPane.searchWithinNode(
          title,
          gSearchResultsPane.query
        );

        // Search within sub-dialog document and highlight matched keyword.
        await gSearchResultsPane.searchWithinNode(
          frame.contentDocument.firstElementChild,
          gSearchResultsPane.query
        );

        // Creating tooltips for all the instances found
        for (let node of gSearchResultsPane.listSearchTooltips) {
          if (!node.tooltipNode) {
            gSearchResultsPane.createSearchTooltip(
              node,
              gSearchResultsPane.query
            );
          }
        }
      },
    },
  });
});

/** @type {Record<string, boolean>} */
const srdSectionPrefs = {};
XPCOMUtils.defineLazyPreferenceGetter(
  srdSectionPrefs,
  "all",
  "browser.settings-redesign.enabled",
  false
);

/**
 * @param {string} section
 */
function srdSectionEnabled(section) {
  if (!(section in srdSectionPrefs)) {
    XPCOMUtils.defineLazyPreferenceGetter(
      srdSectionPrefs,
      section,
      `browser.settings-redesign.${section}.enabled`,
      false
    );
  }
  return srdSectionPrefs.all || srdSectionPrefs[section];
}

var { SettingPaneManager, friendlyPrefCategoryNameToInternalName } =
  ChromeUtils.importESModule(
    "chrome://browser/content/preferences/config/SettingPaneManager.mjs",
    {
      global: "current",
    }
  );

var SettingGroupManager = ChromeUtils.importESModule(
  "chrome://browser/content/preferences/config/SettingGroupManager.mjs",
  {
    global: "current",
  }
).SettingGroupManager;

/**
 * Register initial config-based setting panes here. If you need to register a
 * pane elsewhere, use {@link SettingPaneManager['registerPane']}.
 *
 * @type {Record<string, SettingPaneConfig>}
 */
const CONFIG_PANES = Object.freeze({
  about: {
    l10nId: "about-firefox-header",
    iconSrc: "chrome://browser/skin/sidebar/firefox.svg",
    groupIds: ["updates", "support"],
    module: "chrome://browser/content/preferences/config/about-firefox.mjs",
    visible: () =>
      Services.prefs.getBoolPref("browser.settings-redesign.enabled", false),
  },
  accessibility: {
    l10nId: "preferences-accessibility-header",
    groupIds: [
      "zoom",
      "fonts",
      "contrast",
      "keyboardAndScrolling",
      "motionAndLink",
    ],
    module: "chrome://browser/content/preferences/config/accessibility.mjs",
    iconSrc: "chrome://browser/skin/preferences/category-accessibility.svg",
    visible: () =>
      Services.prefs.getBoolPref("browser.settings-redesign.enabled", false),
  },
  appearance: {
    l10nId: "preferences-appearance-header",
    groupIds: [
      "appearance",
      "browserTheme",
      "browserLayout",
      "relatedSettings",
    ],
    module: "chrome://browser/content/preferences/config/appearance.mjs",
    iconSrc: "chrome://global/skin/icons/eye.svg",
    visible: () =>
      Services.prefs.getBoolPref("browser.settings-redesign.enabled", false),
  },
  ai: {
    l10nId: "preferences-ai-controls-header",
    iconSrc: "chrome://global/skin/icons/highlights.svg",
    groupIds: ["aiControlsDescription", "aiFeatures", "aiStatesDescription"],
    module: "chrome://browser/content/preferences/config/aiFeatures.mjs",
    visible: () =>
      Services.prefs.getBoolPref("browser.preferences.aiControls", false),
  },
  downloads: {
    l10nId: "pane-downloads",
    iconSrc: "chrome://browser/skin/downloads/downloads.svg",
    groupIds: ["downloads", "applications"],
    module: "chrome://browser/content/preferences/config/downloads.mjs",
    visible: () =>
      Services.prefs.getBoolPref("browser.settings-redesign.enabled", false),
  },
  connectionSecurity: {
    parent: "privacy",
    l10nId: "preferences-connection-header",
    groupIds: [
      "httpsOnly",
      "networkProxy",
      "privacyPanel",
      "browsingProtection",
      "certificates",
    ],
    replaces: "privacy",
  },
  dnsOverHttps: {
    parent: "privacy",
    l10nId: "preferences-doh-header2",
    groupIds: ["dnsOverHttpsAdvanced"],
    replaces: "privacy",
  },
  etp: {
    parent: "privacy",
    l10nId: "preferences-etp-header",
    groupIds: ["etpBanner", "etpAdvanced"],
    replaces: "privacy",
  },
  etpCustomize: {
    parent: "etp",
    l10nId: "preferences-etp-customize-header",
    groupIds: ["etpCustomize", "etpReset"],
    replaces: "privacy",
  },
  general: {
    l10nId: "pane-general-title",
    groupIds: [],
  },
  experimental: {
    l10nId: "settings-pane-labs-header",
    iconSrc: "chrome://browser/skin/labs-16.svg",
    groupIds: ["firefoxLabsFeatures"],
    module: "chrome://browser/content/preferences/config/firefoxLabs.mjs",
  },
  history: {
    parent: "privacy",
    l10nId: "history-header2",
    groupIds: ["historyAdvanced"],
    replaces: "privacy",
  },
  home: {
    l10nId: "home-section",
    iconSrc: "chrome://browser/skin/home.svg",
    groupIds: ["defaultBrowserHome", "startupHome", "homepage", "home"],
    module: "chrome://browser/content/preferences/config/home-startup.mjs",
    replaces: "home",
  },
  languages: {
    l10nId: "preferences-languages-header",
    iconSrc: "chrome://browser/skin/translations.svg",
    groupIds: [
      "browserLanguage",
      "websiteLanguage",
      "translations",
      "spellCheck",
    ],
    module: "chrome://browser/content/preferences/config/languages.mjs",
    visible: () => srdSectionEnabled("languages"),
  },
  manageAddresses: {
    parent: "passwordsAutofill",
    l10nId: "autofill-addresses-manage-addresses-title",
    groupIds: ["manageAddresses"],
    iconSrc: "chrome://browser/skin/notification-icons/geo.svg",
    module:
      "chrome://browser/content/preferences/config/passwords-autofill.mjs",
  },
  manageMemories: {
    parent: "personalizeSmartWindow",
    l10nId: "ai-window-manage-memories-header",
    groupIds: ["manageMemories"],
    module: "chrome://browser/content/preferences/config/aiFeatures.mjs",
    supportPage: "smart-window-memories",
  },
  managePayments: {
    parent: "passwordsAutofill",
    l10nId: "autofill-payment-methods-manage-payments-title",
    groupIds: ["managePayments"],
    iconSrc: "chrome://browser/skin/payment-methods-16.svg",
    module:
      "chrome://browser/content/preferences/config/passwords-autofill.mjs",
  },
  profiles: {
    parent: srdSectionEnabled("sync") ? "sync" : "general",
    l10nId: "preferences-profiles-group-header",
    groupIds: ["profilePane"],
  },
  permissionsData: {
    l10nId: "permissions-data-section",
    iconSrc: "chrome://browser/skin/permissions.svg",
    groupIds: ["permissions", "dataCollection"],
    module: "chrome://browser/content/preferences/config/permissions-data.mjs",
    visible: () => srdSectionEnabled("permissionsData"),
  },
  personalizeSmartWindow: {
    parent: "ai",
    l10nId: "ai-window-personalize-header",
    iconSrc: "chrome://browser/skin/smart-window-mono.svg",
    badge: "beta",
    groupIds: ["assistantDefaultGroup", "assistantModelGroup", "memoriesGroup"],
    module: "chrome://browser/content/preferences/config/aiFeatures.mjs",
  },
  passwordsAutofill: {
    l10nId: "preferences-passwords-autofill-header",
    iconSrc: "chrome://browser/skin/login.svg",
    groupIds: ["passwords", "addresses", "payments"],
    module:
      "chrome://browser/content/preferences/config/passwords-autofill.mjs",
    visible: () => srdSectionEnabled("passwordsAutofill"),
  },
  privacy: {
    l10nId: "pane-privacy-section",
    iconSrc: "chrome://browser/skin/preferences/category-privacy-security.svg",
    groupIds: [
      "securityPrivacyStatus",
      "securityPrivacyWarnings",
      "etpStatus",
      "ipprotection",
      "cookiesAndSiteData2",
      "history2",
      "nonTechnicalPrivacy2",
      "dnsOverHttps",
      "connectionLink",
    ],
    module: "chrome://browser/content/preferences/config/privacy.mjs",
    replaces: "privacy",
  },
  search: {
    l10nId: "search-section",
    groupIds: [
      "defaultEngine",
      "searchShortcuts",
      "searchSuggestions",
      "firefoxSuggest",
    ],
    iconSrc: "chrome://browser/skin/preferences/category-search.svg",
    module: "chrome://browser/content/preferences/config/search.mjs",
    replaces: "search",
  },
  sync: {
    l10nId: "account-sync-section",
    iconSrc: "chrome://browser/skin/fxa/avatar-empty.svg",
    groupIds: [
      "defaultBrowserSync",
      "account",
      "sync",
      "importBrowserData",
      "profiles",
      "backup",
    ],
    module: "chrome://browser/content/preferences/config/account-sync.mjs",
    replaces: "sync",
  },
  moreFromMozilla: {
    l10nId: "more-from-moz-page-header",
    iconSrc: "chrome://browser/skin/preferences/mozilla-16.svg",
    groupIds: ["moreFromMozillaPromo", "moreFromMozillaProducts"],
    module: "chrome://browser/content/preferences/config/moreFromMozilla.mjs",
    visible: () => NimbusFeatures.moreFromMozilla.getVariable("enabled"),
    replaces: "moreFromMozilla",
  },
  tabsBrowsing: {
    l10nId: "tabs-browsing-section",
    groupIds: [
      "tabs",
      "pageNavigation",
      "media",
      "performance",
      "recommendations",
    ],
    iconSrc: "chrome://global/skin/icons/cursor-arrow.svg",
    module: "chrome://browser/content/preferences/config/tabs-browsing.mjs",
    visible: () => srdSectionEnabled("tabsBrowsing"),
  },
  translations: {
    parent: srdSectionEnabled("languages") ? "languages" : "general",
    l10nId: "settings-translations-subpage-header",
    groupIds: [
      "translationsAutomaticTranslation",
      "translationsDownloadLanguages",
    ],
    iconSrc: "chrome://browser/skin/translations.svg",
    module: "chrome://browser/content/preferences/config/translations.mjs",
    visible: () => srdSectionEnabled("translations"),
  },
});

var gLastCategory = { category: undefined, subcategory: undefined };
const gXULDOMParser = new DOMParser();
var gCategoryModules = new Map();
var gCategoryInits = new Map();

function register_module(categoryName, categoryObject) {
  gCategoryModules.set(categoryName, categoryObject);
  gCategoryInits.set(categoryName, {
    _initted: false,
    init() {
      let startTime = ChromeUtils.now();
      if (this._initted) {
        return;
      }
      this._initted = true;
      let template = document.getElementById("template-" + categoryName);
      if (template) {
        // Replace the template element with the nodes inside of it.
        template.replaceWith(template.content);

        // We've inserted elements that rely on 'preference' attributes.
        // So we need to update those by reading from the prefs.
        // The bindings will do this using idle dispatch and avoid
        // repeated runs if called multiple times before the task runs.
        Preferences.queueUpdateOfAllElements();
      }

      categoryObject.init();
      ChromeUtils.addProfilerMarker(
        "Preferences",
        { startTime },
        categoryName + " init"
      );
    },
  });
}

document.addEventListener("DOMContentLoaded", init_all, { once: true });

function init_all() {
  Preferences.forceEnableInstantApply();

  // Asks Preferences to queue an update of the attribute values of
  // the entire document.
  Preferences.queueUpdateOfAllElements();

  register_module("paneGeneral", gMainPane);
  register_module("paneHome", gHomePane);
  register_module("paneSearch", gSearchPane);
  register_module("panePrivacy", gPrivacyPane);
  register_module("paneContainers", gContainersPane);

  // Restore the cached Firefox Labs nav button visibility so it shows
  // immediately when recipes are expected to be available, before
  // firefoxLabs.mjs loads on first navigation. The module itself updates
  // this cache when features are (un)available.
  if (ExperimentAPI.labsEnabled) {
    document.getElementById("category-experimental").hidden =
      Services.prefs.getBoolPref(
        "browser.preferences.experimental.hidden",
        false
      );
  }

  // The Sync category needs to be the last of the "real" categories
  // registered and initialized since many tests wait for the
  // "sync-pane-loaded" observer notification before starting the test.
  let redesignEnabled = Services.prefs.getBoolPref(
    "browser.settings-redesign.enabled"
  );
  let accountsEnabled = Services.prefs.getBoolPref(
    "identity.fxaccounts.enabled"
  );
  let categorySync = document.getElementById("category-sync");
  if (redesignEnabled) {
    categorySync.setAttribute("data-l10n-id", "pane-account-sync-title");
    categorySync.iconSrc = "chrome://browser/skin/fxa/avatar-empty.svg";
    categorySync.hidden = false;
  } else if (accountsEnabled) {
    categorySync.hidden = false;
    register_module("paneSync", gSyncPane);
  }
  register_module("paneSearchResults", gSearchResultsPane);
  for (let [id, config] of Object.entries(CONFIG_PANES)) {
    if (!redesignEnabled && config.replaces) {
      continue;
    }

    SettingPaneManager.registerPane(id, config);
  }

  // customHomepage is registered separately because its groups are set up by
  // AboutPreferences.observe(), which only fires in the redesign path.
  if (redesignEnabled) {
    SettingPaneManager.registerPane("customHomepage", {
      parent: "home",
      l10nId: "home-custom-homepage-subpage",
      groupIds: ["customHomepage"],
      module: "chrome://browser/content/preferences/config/home-startup.mjs",
    });
  } else {
    NimbusFeatures.moreFromMozilla.recordExposureEvent({ once: true });
    if (NimbusFeatures.moreFromMozilla.getVariable("enabled")) {
      document.getElementById("category-more-from-mozilla").hidden = false;
      gMoreFromMozillaPane.option =
        NimbusFeatures.moreFromMozilla.getVariable("template");
      register_module("paneMoreFromMozilla", gMoreFromMozillaPane);
    }
  }

  gSearchResultsPane.init();
  gMainPane.preInit();

  let categories = document.getElementById("categories");
  categories.addEventListener("change-view", event => {
    gotoPref(event.target.view);
  });

  maybeDisplayPoliciesNotice();

  window.addEventListener("hashchange", onHashChange);
  window.addEventListener("beforeunload", onBeforeunload);

  document.getElementById("focusSearch1").addEventListener("command", () => {
    gSearchResultsPane.searchInput.focus();
  });

  gotoPref().then(() => {
    document.getElementById("addonsButton").addEventListener("click", e => {
      e.preventDefault();
      if (e.button >= 2) {
        // Ignore right clicks.
        return;
      }
      let mainWindow = window.browsingContext.topChromeWindow;
      mainWindow.BrowserAddonUI.openAddonsMgr();
    });

    document.dispatchEvent(
      new CustomEvent("Initialized", {
        bubbles: true,
        cancelable: true,
      })
    );
  });
}

function onHashChange() {
  gotoPref(null, "Hash");
}

function onBeforeunload() {
  Glean.aboutpreferences.close.record({ session: TAB_SESSION_ID });
}

/**
 * This is called by BrowserUsageTelemetry when it would record a change as a
 * labelled_counter. This could potentially integrate with Setting instead, but
 * we would miss changes for settings that haven't been converted to Setting.
 *
 * @param {string} id The Setting id or telemetry id for the change.
 */
function recordSettingChangeTelemetry(id) {
  Glean.aboutpreferences.change.record({
    session: TAB_SESSION_ID,
    setting: id,
    pane: gLastCategory.category,
  });
}

/**
 * @param {string} [aCategory] The pane to show, defaults to the hash of URL or general
 * @param {"Click"|"Initial"|"Hash"} [aShowReason]
 *   What triggered the navigation. Defaults to "Click" if aCategory is provided,
 *   otherwise "Initial".
 */
async function gotoPref(
  aCategory,
  aShowReason = aCategory ? "Click" : "Initial"
) {
  let categories = document.getElementById("categories");
  const kDefaultCategoryInternalName = "paneGeneral";
  const kDefaultCategory = "general";
  let hash = document.location.hash;
  let category = aCategory || hash.substring(1) || kDefaultCategoryInternalName;

  let breakIndex = category.indexOf("-");
  // Subcategories allow for selecting smaller sections of the preferences
  // until proper search support is enabled (bug 1353954).
  let subcategory = breakIndex != -1 && category.substring(breakIndex + 1);
  if (subcategory) {
    category = category.substring(0, breakIndex);
  }
  category = friendlyPrefCategoryNameToInternalName(category);
  if (category != "paneSearchResults") {
    gSearchResultsPane.query = null;
    gSearchResultsPane.searchInput.value = "";
    gSearchResultsPane.removeAllSearchIndicators(window, true);
  } else if (!gSearchResultsPane.searchInput.value) {
    // Something tried to send us to the search results pane without
    // a query string. Default to the General pane instead.
    category = kDefaultCategoryInternalName;
    document.location.hash = kDefaultCategory;
    gSearchResultsPane.query = null;
  }

  // Updating the hash (below) or changing the selected category
  // will re-enter gotoPref.
  if (gLastCategory.category == category && !subcategory) {
    return;
  }

  let item;
  let unknownCategory = false;
  if (category != "paneSearchResults") {
    // Hide second level headers in normal view
    for (let element of document.querySelectorAll(".search-header")) {
      element.hidden = true;
    }

    item = /** @type {HTMLElement} */ (
      categories.querySelector(
        'moz-page-nav-button[view="' + CSS.escape(category) + '"]'
      )
    );
    if (!item || item.hidden) {
      unknownCategory = true;
      category = kDefaultCategoryInternalName;
      item = categories.querySelector(
        'moz-page-nav-button[view="' + category + '"]'
      );
    }
  }

  if (
    gLastCategory.category ||
    unknownCategory ||
    category != kDefaultCategoryInternalName ||
    subcategory
  ) {
    let friendlyName = internalPrefCategoryNameToFriendlyName(category);
    // Overwrite the hash, unless there is no hash and we're switching to the
    // default category, e.g. by using the 'back' button after navigating to
    // a different category.

    // Note: Bug 1983388 - If there is an element in the DOM that has the same
    // ID as the `friendlyName`, then focus will be lost when navigating the
    // category navigation via keyboard when that `friendlyName` category is selected.
    if (
      !(!document.location.hash && category == kDefaultCategoryInternalName)
    ) {
      document.location.hash = friendlyName;
    }
  }
  // Need to set the gLastCategory before setting categories.currentView since
  // the change-view event will re-enter the gotoPref codepath.
  gLastCategory.category = category;
  gLastCategory.subcategory = subcategory;
  categories.currentView = item ? item.getAttribute("view") : category;
  window.history.replaceState(category, document.title);

  let categoryInfo = gCategoryInits.get(category);
  if (!categoryInfo) {
    let err = new Error(
      "Unknown in-content prefs category! Can't init " + category
    );
    console.error(err);
    throw err;
  }
  categoryInfo.init();

  if (document.hasPendingL10nMutations) {
    await new Promise(r =>
      document.addEventListener("L10nMutationsFinished", r, { once: true })
    );
    // Bail out of this goToPref if the category
    // or subcategory changed during async operation.
    if (
      gLastCategory.category !== category ||
      gLastCategory.subcategory !== subcategory
    ) {
      return;
    }
  }

  search(category, "data-category");

  if (aShowReason != "Initial") {
    document.querySelector(".main-content").scrollTop = 0;
  }

  // Check to see if the category module wants to do any special
  // handling of the subcategory - for example, opening a SubDialog.
  //
  // If not, just do a normal spotlight on the subcategory.
  let categoryModule = gCategoryModules.get(category);
  if (!categoryModule.handleSubcategory?.(subcategory)) {
    spotlight(subcategory, category);
  }

  // Record which category is shown
  let gleanId = /** @type {"showClick" | "showHash" | "showInitial"} */ (
    "show" + aShowReason
  );
  Glean.aboutpreferences[gleanId].record({
    value: category,
    session: TAB_SESSION_ID,
  });

  document.dispatchEvent(
    new CustomEvent("paneshown", {
      bubbles: true,
      cancelable: true,
      detail: {
        category,
      },
    })
  );
}

/**
 * @param {string} aQuery
 * @param {string} aAttribute
 */
function search(aQuery, aAttribute) {
  let mainPrefPane = document.getElementById("mainPrefPane");
  let elements = /** @type {HTMLElement[]} */ (
    Array.from(mainPrefPane.children)
  );
  for (let element of elements) {
    // If the "data-hidden-from-search" is "true", the
    // element will not get considered during search.
    if (
      element.getAttribute("data-hidden-from-search") != "true" ||
      element.getAttribute("data-subpanel") == "true"
    ) {
      let attributeValue = element.getAttribute(aAttribute);
      if (attributeValue == aQuery) {
        element.hidden = false;
      } else {
        element.hidden = true;
      }
    } else if (
      element.getAttribute("data-hidden-from-search") == "true" &&
      !element.hidden
    ) {
      element.hidden = true;
    }
    element.classList.remove("visually-hidden");
  }
}

function spotlight(subcategory, category) {
  let highlightedElements = document.querySelectorAll(".spotlight");
  if (highlightedElements.length) {
    for (let element of highlightedElements) {
      element.classList.remove("spotlight");
    }
  }
  if (subcategory) {
    scrollAndHighlight(subcategory, category);
  }
}

function scrollAndHighlight(subcategory) {
  let elements = document.querySelectorAll(
    `[data-subcategory~="${subcategory}"]`
  );
  if (!elements.length) {
    return;
  }

  elements[0].scrollIntoView({
    behavior: "smooth",
    block: "center",
  });
  for (let element of elements) {
    element.classList.add("spotlight");
  }
}

// This function is duplicated inside of utilityOverlay.js's openPreferences.
function internalPrefCategoryNameToFriendlyName(aName) {
  return (aName || "").replace(/^pane./, function (toReplace) {
    return toReplace[4].toLowerCase();
  });
}

// Put up a confirm dialog with "ok to restart", "revert without restarting"
// and "restart later" buttons and returns the index of the button chosen.
// We can choose not to display the "restart later", or "revert" buttons,
// altough the later still lets us revert by using the escape key.
//
// The constants are useful to interpret the return value of the function.
const CONFIRM_RESTART_PROMPT_RESTART_NOW = 0;
const CONFIRM_RESTART_PROMPT_CANCEL = 1;
const CONFIRM_RESTART_PROMPT_RESTART_LATER = 2;
async function confirmRestartPrompt(
  aRestartToEnable,
  aDefaultButtonIndex,
  aWantRevertAsCancelButton,
  aWantRestartLaterButton
) {
  let [
    msg,
    title,
    restartButtonText,
    noRestartButtonText,
    restartLaterButtonText,
  ] = await document.l10n.formatValues([
    {
      id: aRestartToEnable
        ? "feature-enable-requires-restart"
        : "feature-disable-requires-restart",
    },
    { id: "should-restart-title" },
    { id: "should-restart-ok" },
    { id: "cancel-no-restart-button" },
    { id: "restart-later" },
  ]);

  // Set up the first (index 0) button:
  let buttonFlags =
    Services.prompt.BUTTON_POS_0 * Services.prompt.BUTTON_TITLE_IS_STRING;

  // Set up the second (index 1) button:
  if (aWantRevertAsCancelButton) {
    buttonFlags +=
      Services.prompt.BUTTON_POS_1 * Services.prompt.BUTTON_TITLE_IS_STRING;
  } else {
    noRestartButtonText = null;
    buttonFlags +=
      Services.prompt.BUTTON_POS_1 * Services.prompt.BUTTON_TITLE_CANCEL;
  }

  // Set up the third (index 2) button:
  if (aWantRestartLaterButton) {
    buttonFlags +=
      Services.prompt.BUTTON_POS_2 * Services.prompt.BUTTON_TITLE_IS_STRING;
  } else {
    restartLaterButtonText = null;
  }

  switch (aDefaultButtonIndex) {
    case 0:
      buttonFlags += Services.prompt.BUTTON_POS_0_DEFAULT;
      break;
    case 1:
      buttonFlags += Services.prompt.BUTTON_POS_1_DEFAULT;
      break;
    case 2:
      buttonFlags += Services.prompt.BUTTON_POS_2_DEFAULT;
      break;
    default:
      break;
  }

  let button = await Services.prompt.asyncConfirmEx(
    window.browsingContext,
    Ci.nsIPrompt.MODAL_TYPE_CONTENT,
    title,
    msg,
    buttonFlags,
    restartButtonText,
    noRestartButtonText,
    restartLaterButtonText,
    null,
    {}
  );

  let buttonIndex = button.get("buttonNumClicked");

  // If we have the second confirmation dialog for restart, see if the user
  // cancels out at that point.
  if (buttonIndex == CONFIRM_RESTART_PROMPT_RESTART_NOW) {
    let cancelQuit = Cc["@mozilla.org/supports-PRBool;1"].createInstance(
      Ci.nsISupportsPRBool
    );
    Services.obs.notifyObservers(
      cancelQuit,
      "quit-application-requested",
      "restart"
    );
    if (cancelQuit.data) {
      buttonIndex = CONFIRM_RESTART_PROMPT_CANCEL;
    }
  }
  return buttonIndex;
}

// This function is used to append search keywords found
// in the related subdialog to the button that will activate the subdialog.
function appendSearchKeywords(aId, keywords) {
  let element = document.getElementById(aId);
  let searchKeywords = element.getAttribute("searchkeywords");
  if (searchKeywords) {
    keywords.push(searchKeywords);
  }
  element.setAttribute("searchkeywords", keywords.join(" "));
}

function maybeDisplayPoliciesNotice() {
  if (Services.policies.status == Services.policies.ACTIVE) {
    document.getElementById("policies-container").removeAttribute("hidden");
    document
      .getElementById("policies-container-content")
      .removeAttribute("hidden");
  }
}
