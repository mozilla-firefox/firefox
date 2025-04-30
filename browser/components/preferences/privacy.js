/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from extensionControlled.js */
/* import-globals-from preferences.js */

const PREF_UPLOAD_ENABLED = "datareporting.healthreport.uploadEnabled";

const TRACKING_PROTECTION_KEY = "websites.trackingProtectionMode";
const TRACKING_PROTECTION_PREFS = [
  "privacy.trackingprotection.enabled",
  "privacy.trackingprotection.pbmode.enabled",
];
const CONTENT_BLOCKING_PREFS = [
  "privacy.trackingprotection.enabled",
  "privacy.trackingprotection.pbmode.enabled",
  "network.cookie.cookieBehavior",
  "privacy.trackingprotection.fingerprinting.enabled",
  "privacy.trackingprotection.cryptomining.enabled",
  "privacy.firstparty.isolate",
  "privacy.trackingprotection.emailtracking.enabled",
  "privacy.trackingprotection.emailtracking.pbmode.enabled",
  "privacy.fingerprintingProtection",
  "privacy.fingerprintingProtection.pbmode",
];

const PREF_OPT_OUT_STUDIES_ENABLED = "app.shield.optoutstudies.enabled";
const PREF_NORMANDY_ENABLED = "app.normandy.enabled";

const PREF_ADDON_RECOMMENDATIONS_ENABLED = "browser.discovery.enabled";
const PREF_PRIVATE_ATTRIBUTION_ENABLED =
  "dom.private-attribution.submission.enabled";

const PREF_PASSWORD_GENERATION_AVAILABLE = "signon.generation.available";
const { BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN } = Ci.nsICookieService;

const PASSWORD_MANAGER_PREF_ID = "services.passwordSavingEnabled";

ChromeUtils.defineLazyGetter(this, "AlertsServiceDND", function () {
  try {
    let alertsService = Cc["@mozilla.org/alerts-service;1"]
      .getService(Ci.nsIAlertsService)
      .QueryInterface(Ci.nsIAlertsDoNotDisturb);
    // This will throw if manualDoNotDisturb isn't implemented.
    alertsService.manualDoNotDisturb;
    return alertsService;
  } catch (ex) {
    return undefined;
  }
});

ChromeUtils.defineLazyGetter(lazy, "AboutLoginsL10n", () => {
  return new Localization(["branding/brand.ftl", "browser/aboutLogins.ftl"]);
});

ChromeUtils.defineLazyGetter(lazy, "gParentalControlsService", () =>
  "@mozilla.org/parental-controls-service;1" in Cc
    ? Cc["@mozilla.org/parental-controls-service;1"].getService(
        Ci.nsIParentalControlsService
      )
    : null
);

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "gIsFirstPartyIsolated",
  "privacy.firstparty.isolate",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "useOldClearHistoryDialog",
  "privacy.sanitize.useOldClearHistoryDialog",
  false
);

ChromeUtils.defineESModuleGetters(this, {
  DoHConfigController: "resource://gre/modules/DoHConfig.sys.mjs",
  Sanitizer: "resource:///modules/Sanitizer.sys.mjs",
  SelectableProfileService:
    "resource:///modules/profiles/SelectableProfileService.sys.mjs",
});

const SANITIZE_ON_SHUTDOWN_MAPPINGS = {
  history: "privacy.clearOnShutdown.history",
  downloads: "privacy.clearOnShutdown.downloads",
  formdata: "privacy.clearOnShutdown.formdata",
  sessions: "privacy.clearOnShutdown.sessions",
  siteSettings: "privacy.clearOnShutdown.siteSettings",
  cookies: "privacy.clearOnShutdown.cookies",
  cache: "privacy.clearOnShutdown.cache",
  offlineApps: "privacy.clearOnShutdown.offlineApps",
};

/*
 * Prefs that are unique to sanitizeOnShutdown and are not shared
 * with the deleteOnClose mechanism like privacy.clearOnShutdown.cookies, -cache and -offlineApps
 */
const SANITIZE_ON_SHUTDOWN_PREFS_ONLY = [
  "privacy.clearOnShutdown.history",
  "privacy.clearOnShutdown.downloads",
  "privacy.clearOnShutdown.sessions",
  "privacy.clearOnShutdown.formdata",
  "privacy.clearOnShutdown.siteSettings",
];

const SANITIZE_ON_SHUTDOWN_PREFS_ONLY_V2 = [
  "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads",
  "privacy.clearOnShutdown_v2.siteSettings",
];

Preferences.addAll([
  // Content blocking / Tracking Protection
  { id: "privacy.trackingprotection.enabled", type: "bool" },
  { id: "privacy.trackingprotection.pbmode.enabled", type: "bool" },
  { id: "privacy.trackingprotection.fingerprinting.enabled", type: "bool" },
  { id: "privacy.trackingprotection.cryptomining.enabled", type: "bool" },
  { id: "privacy.trackingprotection.emailtracking.enabled", type: "bool" },
  {
    id: "privacy.trackingprotection.emailtracking.pbmode.enabled",
    type: "bool",
  },

  // Fingerprinting Protection
  { id: "privacy.fingerprintingProtection", type: "bool" },
  { id: "privacy.fingerprintingProtection.pbmode", type: "bool" },

  // Resist Fingerprinting
  { id: "privacy.resistFingerprinting", type: "bool" },
  { id: "privacy.resistFingerprinting.pbmode", type: "bool" },

  // Social tracking
  { id: "privacy.trackingprotection.socialtracking.enabled", type: "bool" },
  { id: "privacy.socialtracking.block_cookies.enabled", type: "bool" },

  // Tracker list
  { id: "urlclassifier.trackingTable", type: "string" },

  // Button prefs
  { id: "pref.privacy.disable_button.cookie_exceptions", type: "bool" },
  {
    id: "pref.privacy.disable_button.tracking_protection_exceptions",
    type: "bool",
  },

  // Location Bar
  { id: "browser.urlbar.suggest.bookmark", type: "bool" },
  { id: "browser.urlbar.suggest.clipboard", type: "bool" },
  { id: "browser.urlbar.suggest.history", type: "bool" },
  { id: "browser.urlbar.suggest.openpage", type: "bool" },
  { id: "browser.urlbar.suggest.topsites", type: "bool" },
  { id: "browser.urlbar.suggest.engines", type: "bool" },
  { id: "browser.urlbar.suggest.quicksuggest.nonsponsored", type: "bool" },
  { id: "browser.urlbar.suggest.quicksuggest.sponsored", type: "bool" },
  { id: "browser.urlbar.quicksuggest.dataCollection.enabled", type: "bool" },

  // History
  { id: "places.history.enabled", type: "bool" },
  { id: "browser.formfill.enable", type: "bool" },
  { id: "privacy.history.custom", type: "bool" },

  // Cookies
  { id: "network.cookie.cookieBehavior", type: "int" },
  { id: "network.cookie.blockFutureCookies", type: "bool" },
  // Content blocking category
  { id: "browser.contentblocking.category", type: "string" },
  { id: "browser.contentblocking.features.strict", type: "string" },

  // Clear Private Data
  { id: "privacy.sanitize.sanitizeOnShutdown", type: "bool" },
  { id: "privacy.sanitize.timeSpan", type: "int" },
  { id: "privacy.clearOnShutdown.cookies", type: "bool" },
  { id: "privacy.clearOnShutdown_v2.cookiesAndStorage", type: "bool" },
  { id: "privacy.clearOnShutdown.cache", type: "bool" },
  { id: "privacy.clearOnShutdown_v2.cache", type: "bool" },
  { id: "privacy.clearOnShutdown.offlineApps", type: "bool" },
  { id: "privacy.clearOnShutdown.history", type: "bool" },
  {
    id: "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads",
    type: "bool",
  },
  { id: "privacy.clearOnShutdown.downloads", type: "bool" },
  { id: "privacy.clearOnShutdown.sessions", type: "bool" },
  { id: "privacy.clearOnShutdown.formdata", type: "bool" },
  { id: "privacy.clearOnShutdown.siteSettings", type: "bool" },
  { id: "privacy.clearOnShutdown_v2.siteSettings", type: "bool" },

  // Do not track
  { id: "privacy.donottrackheader.enabled", type: "bool" },

  // Global Privacy Control
  { id: "privacy.globalprivacycontrol.enabled", type: "bool" },

  // Media
  { id: "media.autoplay.default", type: "int" },

  // Popups
  { id: "dom.disable_open_during_load", type: "bool" },
  // Passwords
  { id: "signon.rememberSignons", type: "bool" },
  { id: "signon.generation.enabled", type: "bool" },
  { id: "signon.autofillForms", type: "bool" },
  { id: "signon.management.page.breach-alerts.enabled", type: "bool" },
  { id: "signon.firefoxRelay.feature", type: "string" },

  // Buttons
  { id: "pref.privacy.disable_button.view_passwords", type: "bool" },
  { id: "pref.privacy.disable_button.view_passwords_exceptions", type: "bool" },

  /* Certificates tab
   * security.default_personal_cert
   *   - a string:
   *       "Select Automatically"   select a certificate automatically when a site
   *                                requests one
   *       "Ask Every Time"         present a dialog to the user so he can select
   *                                the certificate to use on a site which
   *                                requests one
   */
  { id: "security.default_personal_cert", type: "string" },

  { id: "security.disable_button.openCertManager", type: "bool" },

  { id: "security.disable_button.openDeviceManager", type: "bool" },

  { id: "security.OCSP.enabled", type: "int" },

  { id: "security.enterprise_roots.enabled", type: "bool" },

  // Add-ons, malware, phishing
  { id: "xpinstall.whitelist.required", type: "bool" },

  { id: "browser.safebrowsing.malware.enabled", type: "bool" },
  { id: "browser.safebrowsing.phishing.enabled", type: "bool" },

  { id: "browser.safebrowsing.downloads.enabled", type: "bool" },

  { id: "urlclassifier.malwareTable", type: "string" },

  {
    id: "browser.safebrowsing.downloads.remote.block_potentially_unwanted",
    type: "bool",
  },
  { id: "browser.safebrowsing.downloads.remote.block_uncommon", type: "bool" },

  // First-Party Isolation
  { id: "privacy.firstparty.isolate", type: "bool" },

  // HTTPS-Only
  { id: "dom.security.https_only_mode", type: "bool" },
  { id: "dom.security.https_only_mode_pbm", type: "bool" },
  { id: "dom.security.https_first", type: "bool" },
  { id: "dom.security.https_first_pbm", type: "bool" },

  // Windows SSO
  { id: "network.http.windows-sso.enabled", type: "bool" },

  // Quick Actions
  { id: "browser.urlbar.quickactions.showPrefs", type: "bool" },
  { id: "browser.urlbar.suggest.quickactions", type: "bool" },

  // Cookie Banner Handling
  { id: "cookiebanners.ui.desktop.enabled", type: "bool" },
  { id: "cookiebanners.service.mode.privateBrowsing", type: "int" },

  // DoH
  { id: "network.trr.mode", type: "int" },
  { id: "network.trr.uri", type: "string" },
  { id: "network.trr.default_provider_uri", type: "string" },
  { id: "network.trr.custom_uri", type: "string" },
  { id: "doh-rollout.disable-heuristics", type: "bool" },
]);

// Study opt out
if (AppConstants.MOZ_DATA_REPORTING) {
  Preferences.addAll([
    // Preference instances for prefs that we need to monitor while the page is open.
    { id: PREF_OPT_OUT_STUDIES_ENABLED, type: "bool" },
    { id: PREF_ADDON_RECOMMENDATIONS_ENABLED, type: "bool" },
    { id: PREF_UPLOAD_ENABLED, type: "bool" },
    { id: "datareporting.usage.uploadEnabled", type: "bool" },
    { id: "dom.private-attribution.submission.enabled", type: "bool" },
  ]);
}
// Privacy segmentation section
Preferences.add({
  id: "browser.dataFeatureRecommendations.enabled",
  type: "bool",
});

// Data Choices tab
if (AppConstants.MOZ_CRASHREPORTER) {
  Preferences.add({
    id: "browser.crashReports.unsubmittedCheck.autoSubmit2",
    type: "bool",
  });
}

function setEventListener(aId, aEventType, aCallback) {
  document
    .getElementById(aId)
    .addEventListener(aEventType, aCallback.bind(gPrivacyPane));
}

function setSyncFromPrefListener(aId, aCallback) {
  Preferences.addSyncFromPrefListener(document.getElementById(aId), aCallback);
}

function setSyncToPrefListener(aId, aCallback) {
  Preferences.addSyncToPrefListener(document.getElementById(aId), aCallback);
}

function dataCollectionCheckboxHandler({
  checkbox,
  pref,
  matchPref = () => true,
  isDisabled = () => false,
}) {
  function updateCheckbox() {
    let collectionEnabled = Services.prefs.getBoolPref(
      PREF_UPLOAD_ENABLED,
      false
    );

    if (collectionEnabled && matchPref()) {
      if (Services.prefs.getBoolPref(pref, false)) {
        checkbox.setAttribute("checked", "true");
      } else {
        checkbox.removeAttribute("checked");
      }
      checkbox.setAttribute("preference", pref);
    } else {
      checkbox.removeAttribute("preference");
      checkbox.removeAttribute("checked");
    }

    checkbox.disabled =
      !collectionEnabled || Services.prefs.prefIsLocked(pref) || isDisabled();
  }

  Preferences.get(PREF_UPLOAD_ENABLED).on("change", updateCheckbox);
  updateCheckbox();
}

// Sets the "Learn how" SUMO link in the Strict/Custom options of Content Blocking.
function setUpContentBlockingWarnings() {
  document.getElementById("fpiIncompatibilityWarning").hidden =
    !gIsFirstPartyIsolated;

  document.getElementById("rfpIncompatibilityWarning").hidden =
    !Preferences.get("privacy.resistFingerprinting").value &&
    !Preferences.get("privacy.resistFingerprinting.pbmode").value;
}

function initTCPStandardSection() {
  let cookieBehaviorPref = Preferences.get("network.cookie.cookieBehavior");
  let updateTCPSectionVisibilityState = () => {
    document.getElementById("etpStandardTCPBox").hidden =
      cookieBehaviorPref.value !=
      Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN;
  };

  cookieBehaviorPref.on("change", updateTCPSectionVisibilityState);

  updateTCPSectionVisibilityState();
}

var gPrivacyPane = {
  _pane: null,

  /**
   * Whether the prompt to restart Firefox should appear when changing the autostart pref.
   */
  _shouldPromptForRestart: true,

  /**
   * Update the tracking protection UI to deal with extension control.
   */
  _updateTrackingProtectionUI() {
    let cBPrefisLocked = CONTENT_BLOCKING_PREFS.some(pref =>
      Services.prefs.prefIsLocked(pref)
    );
    let tPPrefisLocked = TRACKING_PROTECTION_PREFS.some(pref =>
      Services.prefs.prefIsLocked(pref)
    );

    function setInputsDisabledState(isControlled) {
      let tpDisabled = tPPrefisLocked || isControlled;
      let disabled = cBPrefisLocked || isControlled;
      let tpCheckbox = document.getElementById(
        "contentBlockingTrackingProtectionCheckbox"
      );
      // Only enable the TP menu if Detect All Trackers is enabled.
      document.getElementById("trackingProtectionMenu").disabled =
        tpDisabled || !tpCheckbox.checked;
      tpCheckbox.disabled = tpDisabled;

      document.getElementById("standardRadio").disabled = disabled;
      document.getElementById("strictRadio").disabled = disabled;
      document
        .getElementById("contentBlockingOptionStrict")
        .classList.toggle("disabled", disabled);
      document
        .getElementById("contentBlockingOptionStandard")
        .classList.toggle("disabled", disabled);
      let arrowButtons = document.querySelectorAll("button.arrowhead");
      for (let button of arrowButtons) {
        button.disabled = disabled;
      }

      // Notify observers that the TP UI has been updated.
      // This is needed since our tests need to be notified about the
      // trackingProtectionMenu element getting disabled/enabled at the right time.
      Services.obs.notifyObservers(window, "privacy-pane-tp-ui-updated");
    }

    let policy = Services.policies.getActivePolicies();
    if (
      policy &&
      ((policy.EnableTrackingProtection &&
        policy.EnableTrackingProtection.Locked) ||
        (policy.Cookies && policy.Cookies.Locked))
    ) {
      setInputsDisabledState(true);
    }
    if (tPPrefisLocked) {
      // An extension can't control this setting if either pref is locked.
      hideControllingExtension(TRACKING_PROTECTION_KEY);
      setInputsDisabledState(false);
    } else {
      handleControllingExtension(
        PREF_SETTING_TYPE,
        TRACKING_PROTECTION_KEY
      ).then(setInputsDisabledState);
    }
  },

  /**
   * Hide the "Change Block List" link for trackers/tracking content in the
   * custom Content Blocking/ETP panel. By default, it will not be visible.
   */
  _showCustomBlockList() {
    let prefValue = Services.prefs.getBoolPref(
      "browser.contentblocking.customBlockList.preferences.ui.enabled"
    );
    if (!prefValue) {
      document.getElementById("changeBlockListLink").style.display = "none";
    } else {
      setEventListener("changeBlockListLink", "click", this.showBlockLists);
    }
  },

  /**
   * Set up handlers for showing and hiding controlling extension info
   * for tracking protection.
   */
  _initTrackingProtectionExtensionControl() {
    setEventListener(
      "contentBlockingDisableTrackingProtectionExtension",
      "command",
      makeDisableControllingExtension(
        PREF_SETTING_TYPE,
        TRACKING_PROTECTION_KEY
      )
    );

    let trackingProtectionObserver = {
      observe() {
        gPrivacyPane._updateTrackingProtectionUI();
      },
    };

    for (let pref of TRACKING_PROTECTION_PREFS) {
      Services.prefs.addObserver(pref, trackingProtectionObserver);
    }
    window.addEventListener("unload", () => {
      for (let pref of TRACKING_PROTECTION_PREFS) {
        Services.prefs.removeObserver(pref, trackingProtectionObserver);
      }
    });
  },

  _initThirdPartyCertsToggle() {
    // Third-party certificate import is only implemented for Windows and Mac,
    // and we should not expose this as a user-configurable setting if there's
    // an enterprise policy controlling it (either to enable _or_ disable it).
    let canConfigureThirdPartyCerts =
      (AppConstants.platform == "win" || AppConstants.platform == "macosx") &&
      typeof Services.policies.getActivePolicies()?.Certificates
        ?.ImportEnterpriseRoots == "undefined";

    document.getElementById("certEnableThirdPartyToggleBox").hidden =
      !canConfigureThirdPartyCerts;
  },

  syncFromHttpsOnlyPref() {
    let httpsOnlyOnPref = Services.prefs.getBoolPref(
      "dom.security.https_only_mode"
    );
    let httpsOnlyOnPBMPref = Services.prefs.getBoolPref(
      "dom.security.https_only_mode_pbm"
    );
    let httpsFirstOnPref = Services.prefs.getBoolPref(
      "dom.security.https_first"
    );
    let httpsFirstOnPBMPref = Services.prefs.getBoolPref(
      "dom.security.https_first_pbm"
    );
    let httpsOnlyRadioGroup = document.getElementById("httpsOnlyRadioGroup");
    let httpsOnlyExceptionButton = document.getElementById(
      "httpsOnlyExceptionButton"
    );

    if (httpsOnlyOnPref) {
      httpsOnlyRadioGroup.value = "enabled";
    } else if (httpsOnlyOnPBMPref) {
      httpsOnlyRadioGroup.value = "privateOnly";
    } else {
      httpsOnlyRadioGroup.value = "disabled";
    }

    httpsOnlyExceptionButton.disabled =
      !httpsOnlyOnPref &&
      !httpsFirstOnPref &&
      !httpsOnlyOnPBMPref &&
      !httpsFirstOnPBMPref;

    if (
      Services.prefs.prefIsLocked("dom.security.https_only_mode") ||
      Services.prefs.prefIsLocked("dom.security.https_only_mode_pbm")
    ) {
      httpsOnlyRadioGroup.disabled = true;
    }
  },

  syncToHttpsOnlyPref() {
    let value = document.getElementById("httpsOnlyRadioGroup").value;
    Services.prefs.setBoolPref(
      "dom.security.https_only_mode_pbm",
      value == "privateOnly"
    );
    Services.prefs.setBoolPref(
      "dom.security.https_only_mode",
      value == "enabled"
    );
  },

  /**
   * Init HTTPS-Only mode and corresponding prefs
   */
  initHttpsOnly() {
    // Set radio-value based on the pref value
    this.syncFromHttpsOnlyPref();

    // Create event listener for when the user clicks
    // on one of the radio buttons
    setEventListener("httpsOnlyRadioGroup", "change", this.syncToHttpsOnlyPref);
    // Update radio-value when the pref changes
    Preferences.get("dom.security.https_only_mode").on("change", () =>
      this.syncFromHttpsOnlyPref()
    );
    Preferences.get("dom.security.https_only_mode_pbm").on("change", () =>
      this.syncFromHttpsOnlyPref()
    );
    Preferences.get("dom.security.https_first").on("change", () =>
      this.syncFromHttpsOnlyPref()
    );
    Preferences.get("dom.security.https_first_pbm").on("change", () =>
      this.syncFromHttpsOnlyPref()
    );
  },

  get dnsOverHttpsResolvers() {
    let providers = DoHConfigController.currentConfig.providerList;
    // if there's no default, we'll hold its position with an empty string
    let defaultURI = DoHConfigController.currentConfig.fallbackProviderURI;
    let defaultIndex = providers.findIndex(p => p.uri == defaultURI);
    if (defaultIndex == -1 && defaultURI) {
      // the default value for the pref isn't included in the resolvers list
      // so we'll make a stub for it. Without an id, we'll have to use the url as the label
      providers.unshift({ uri: defaultURI });
    }
    return providers;
  },

  updateDoHResolverList(mode) {
    let resolvers = this.dnsOverHttpsResolvers;
    let currentURI = Preferences.get("network.trr.uri").value;
    if (!currentURI) {
      currentURI = Preferences.get("network.trr.default_provider_uri").value;
    }
    let menu = document.getElementById(`${mode}ResolverChoices`);

    let selectedIndex = currentURI
      ? resolvers.findIndex(r => r.uri == currentURI)
      : 0;
    if (selectedIndex == -1) {
      // select the last "Custom" item
      selectedIndex = menu.itemCount - 1;
    }
    menu.selectedIndex = selectedIndex;

    let customInput = document.getElementById(`${mode}InputField`);
    customInput.hidden = menu.value != "custom";
  },

  populateDoHResolverList(mode) {
    let resolvers = this.dnsOverHttpsResolvers;
    let defaultURI = DoHConfigController.currentConfig.fallbackProviderURI;
    let menu = document.getElementById(`${mode}ResolverChoices`);

    // populate the DNS-Over-HTTPS resolver list
    menu.removeAllItems();
    for (let resolver of resolvers) {
      let item = menu.appendItem(undefined, resolver.uri);
      if (resolver.uri == defaultURI) {
        document.l10n.setAttributes(
          item,
          "connection-dns-over-https-url-item-default",
          {
            name: resolver.UIName || resolver.uri,
          }
        );
      } else {
        item.label = resolver.UIName || resolver.uri;
      }
    }
    let lastItem = menu.appendItem(undefined, "custom");
    document.l10n.setAttributes(
      lastItem,
      "connection-dns-over-https-url-custom"
    );

    // set initial selection in the resolver provider picker
    this.updateDoHResolverList(mode);

    let customInput = document.getElementById(`${mode}InputField`);

    function updateURIPref() {
      if (customInput.value == "") {
        // Setting the pref to empty string will make it have the default
        // pref value which makes us fallback to using the default TRR
        // resolver in network.trr.default_provider_uri.
        // If the input is empty we set it to "(space)" which is essentially
        // the same.
        Services.prefs.setStringPref("network.trr.uri", " ");
      } else {
        Services.prefs.setStringPref("network.trr.uri", customInput.value);
      }
    }

    menu.addEventListener("command", () => {
      if (menu.value == "custom") {
        customInput.hidden = false;
        updateURIPref();
      } else {
        customInput.hidden = true;
        Services.prefs.setStringPref("network.trr.uri", menu.value);
      }
      Glean.securityDohSettings.providerChoiceValue.record({
        value: menu.value,
      });

      // Update other menu too.
      let otherMode = mode == "dohEnabled" ? "dohStrict" : "dohEnabled";
      let otherMenu = document.getElementById(`${otherMode}ResolverChoices`);
      let otherInput = document.getElementById(`${otherMode}InputField`);
      otherMenu.value = menu.value;
      otherInput.hidden = otherMenu.value != "custom";
    });

    // Change the URL when you press ENTER in the input field it or loses focus
    customInput.addEventListener("change", () => {
      updateURIPref();
    });
  },

  async updateDoHStatus() {
    let trrURI = Services.dns.currentTrrURI;
    let hostname = URL.parse(trrURI)?.hostname;
    if (!hostname) {
      hostname = await document.l10n.formatValue("preferences-doh-bad-url");
    }

    let steering = document.getElementById("dohSteeringStatus");
    steering.hidden = true;

    let dohResolver = document.getElementById("dohResolver");
    dohResolver.hidden = true;

    let status = document.getElementById("dohStatus");

    async function setStatus(localizedStringName, options) {
      let opts = options || {};
      let statusString = await document.l10n.formatValue(
        localizedStringName,
        opts
      );
      document.l10n.setAttributes(status, "preferences-doh-status", {
        status: statusString,
      });
    }

    function computeStatus() {
      let mode = Services.dns.currentTrrMode;
      if (
        mode == Ci.nsIDNSService.MODE_TRRFIRST ||
        mode == Ci.nsIDNSService.MODE_TRRONLY
      ) {
        if (lazy.gParentalControlsService?.parentalControlsEnabled) {
          return "preferences-doh-status-not-active";
        }
        let confirmationState = Services.dns.currentTrrConfirmationState;
        switch (confirmationState) {
          case Ci.nsIDNSService.CONFIRM_TRYING_OK:
          case Ci.nsIDNSService.CONFIRM_OK:
          case Ci.nsIDNSService.CONFIRM_DISABLED:
            return "preferences-doh-status-active";
          default:
            return "preferences-doh-status-not-active";
        }
      }

      return "preferences-doh-status-disabled";
    }

    let errReason = "";
    let confirmationStatus = Services.dns.lastConfirmationStatus;
    let mode = Services.dns.currentTrrMode;
    if (
      (mode == Ci.nsIDNSService.MODE_TRRFIRST ||
        mode == Ci.nsIDNSService.MODE_TRRONLY) &&
      lazy.gParentalControlsService?.parentalControlsEnabled
    ) {
      errReason = Services.dns.getTRRSkipReasonName(
        Ci.nsITRRSkipReason.TRR_PARENTAL_CONTROL
      );
    } else if (confirmationStatus != Cr.NS_OK) {
      errReason = ChromeUtils.getXPCOMErrorName(confirmationStatus);
    } else {
      errReason = Services.dns.getTRRSkipReasonName(
        Services.dns.lastConfirmationSkipReason
      );
    }
    let statusLabel = computeStatus();
    // setStatus will format and set the statusLabel asynchronously.
    setStatus(statusLabel, { reason: errReason });
    dohResolver.hidden = statusLabel == "preferences-doh-status-disabled";

    let statusLearnMore = document.getElementById("dohStatusLearnMore");
    statusLearnMore.hidden = statusLabel != "preferences-doh-status-not-active";

    // No need to set the resolver name since we're not going to show it.
    if (statusLabel == "preferences-doh-status-disabled") {
      return;
    }

    function nameOrDomain() {
      for (let resolver of DoHConfigController.currentConfig.providerList) {
        if (resolver.uri == trrURI) {
          return resolver.UIName || hostname || trrURI;
        }
      }

      // Also check if this is a steering provider.
      for (let resolver of DoHConfigController.currentConfig.providerSteering
        .providerList) {
        if (resolver.uri == trrURI) {
          steering.hidden = false;
          return resolver.UIName || hostname || trrURI;
        }
      }

      return hostname;
    }

    let resolverNameOrDomain = nameOrDomain();
    document.l10n.setAttributes(dohResolver, "preferences-doh-resolver", {
      name: resolverNameOrDomain,
    });
  },

  highlightDoHCategoryAndUpdateStatus() {
    let value = Preferences.get("network.trr.mode").value;
    let defaultOption = document.getElementById("dohOptionDefault");
    let enabledOption = document.getElementById("dohOptionEnabled");
    let strictOption = document.getElementById("dohOptionStrict");
    let offOption = document.getElementById("dohOptionOff");
    defaultOption.classList.remove("selected");
    enabledOption.classList.remove("selected");
    strictOption.classList.remove("selected");
    offOption.classList.remove("selected");

    switch (value) {
      case Ci.nsIDNSService.MODE_NATIVEONLY:
        defaultOption.classList.add("selected");
        break;
      case Ci.nsIDNSService.MODE_TRRFIRST:
        enabledOption.classList.add("selected");
        break;
      case Ci.nsIDNSService.MODE_TRRONLY:
        strictOption.classList.add("selected");
        break;
      case Ci.nsIDNSService.MODE_TRROFF:
        offOption.classList.add("selected");
        break;
      default:
        // The pref is set to a random value.
        // This shouldn't happen, but let's make sure off is selected.
        offOption.classList.add("selected");
        document.getElementById("dohCategoryRadioGroup").selectedIndex = 3;
        break;
    }

    // When the mode is set to 0 we need to clear the URI so
    // doh-rollout can kick in.
    if (value == Ci.nsIDNSService.MODE_NATIVEONLY) {
      Services.prefs.clearUserPref("network.trr.uri");
      Services.prefs.clearUserPref("doh-rollout.disable-heuristics");
    }

    // Bug 1861285
    // When the mode is set to 2 or 3, we need to check if network.trr.uri is a empty string.
    // In this case, we need to update network.trr.uri to default to fallbackProviderURI.
    // This occurs when the mode is previously set to 0 (Default Protection).
    if (
      value == Ci.nsIDNSService.MODE_TRRFIRST ||
      value == Ci.nsIDNSService.MODE_TRRONLY
    ) {
      if (!Services.prefs.getStringPref("network.trr.uri")) {
        Services.prefs.setStringPref(
          "network.trr.uri",
          DoHConfigController.currentConfig.fallbackProviderURI
        );
      }
    }

    // Bug 1900672
    // When the mode is set to 5, clear the pref to ensure that
    // network.trr.uri is set to fallbackProviderURIwhen the mode is set to 2 or 3 afterwards
    if (value == Ci.nsIDNSService.MODE_TRROFF) {
      Services.prefs.clearUserPref("network.trr.uri");
    }

    gPrivacyPane.updateDoHStatus();
  },

  /**
   * Init DoH corresponding prefs
   */
  initDoH() {
    setEventListener("dohDefaultArrow", "command", this.toggleExpansion);
    setEventListener("dohEnabledArrow", "command", this.toggleExpansion);
    setEventListener("dohStrictArrow", "command", this.toggleExpansion);

    function modeButtonPressed(e) {
      // Clicking the active mode again should not generate another event
      if (
        parseInt(e.target.value) == Preferences.get("network.trr.mode").value
      ) {
        return;
      }
      Glean.securityDohSettings.modeChangedButton.record({
        value: e.target.id,
      });
    }

    setEventListener("dohDefaultRadio", "command", modeButtonPressed);
    setEventListener("dohEnabledRadio", "command", modeButtonPressed);
    setEventListener("dohStrictRadio", "command", modeButtonPressed);
    setEventListener("dohOffRadio", "command", modeButtonPressed);

    this.populateDoHResolverList("dohEnabled");
    this.populateDoHResolverList("dohStrict");

    Preferences.get("network.trr.uri").on("change", () => {
      gPrivacyPane.updateDoHResolverList("dohEnabled");
      gPrivacyPane.updateDoHResolverList("dohStrict");
      gPrivacyPane.updateDoHStatus();
    });

    // Update status box and hightlightling when the pref changes
    Preferences.get("network.trr.mode").on(
      "change",
      gPrivacyPane.highlightDoHCategoryAndUpdateStatus
    );
    this.highlightDoHCategoryAndUpdateStatus();

    Services.obs.addObserver(this, "network:trr-uri-changed");
    Services.obs.addObserver(this, "network:trr-mode-changed");
    Services.obs.addObserver(this, "network:trr-confirmation");
    let unload = () => {
      Services.obs.removeObserver(this, "network:trr-uri-changed");
      Services.obs.removeObserver(this, "network:trr-mode-changed");
      Services.obs.removeObserver(this, "network:trr-confirmation");
    };
    window.addEventListener("unload", unload, { once: true });

    let uriPref = Services.prefs.getStringPref("network.trr.uri");
    // If the value isn't one of the providers, we need to update the
    // custom_uri pref to make sure the input box contains the correct URL.
    if (uriPref && !this.dnsOverHttpsResolvers.some(e => e.uri == uriPref)) {
      Services.prefs.setStringPref(
        "network.trr.custom_uri",
        Services.prefs.getStringPref("network.trr.uri")
      );
    }

    if (Services.prefs.prefIsLocked("network.trr.mode")) {
      document.getElementById("dohCategoryRadioGroup").disabled = true;
      Services.prefs.setStringPref("network.trr.custom_uri", uriPref);
    }
  },

  initWebAuthn() {
    document.getElementById("openWindowsPasskeySettings").hidden =
      !Services.prefs.getBoolPref(
        "security.webauthn.show_ms_settings_link",
        true
      );
  },

  /**
   * Sets up the UI for the number of days of history to keep, and updates the
   * label of the "Clear Now..." button.
   */
  init() {
    this._updateSanitizeSettingsButton();
    this.initDeleteOnCloseBox();
    this.syncSanitizationPrefsWithDeleteOnClose();
    this.initializeHistoryMode();
    this.updateHistoryModePane();
    this.updatePrivacyMicroControls();
    this.initAutoStartPrivateBrowsingReverter();

    /* Initialize Content Blocking */
    this.initContentBlocking();

    this._showCustomBlockList();
    this.trackingProtectionReadPrefs();
    this.fingerprintingProtectionReadPrefs();
    this.networkCookieBehaviorReadPrefs();
    this._initTrackingProtectionExtensionControl();
    this._initThirdPartyCertsToggle();
    this._initProfilesInfo();

    Preferences.get("privacy.trackingprotection.enabled").on(
      "change",
      gPrivacyPane.trackingProtectionReadPrefs.bind(gPrivacyPane)
    );
    Preferences.get("privacy.trackingprotection.pbmode.enabled").on(
      "change",
      gPrivacyPane.trackingProtectionReadPrefs.bind(gPrivacyPane)
    );

    // Watch all of the prefs that the new Cookies & Site Data UI depends on
    Preferences.get("network.cookie.cookieBehavior").on(
      "change",
      gPrivacyPane.networkCookieBehaviorReadPrefs.bind(gPrivacyPane)
    );
    Preferences.get("browser.privatebrowsing.autostart").on(
      "change",
      gPrivacyPane.networkCookieBehaviorReadPrefs.bind(gPrivacyPane)
    );
    Preferences.get("privacy.firstparty.isolate").on(
      "change",
      gPrivacyPane.networkCookieBehaviorReadPrefs.bind(gPrivacyPane)
    );

    Preferences.get("privacy.fingerprintingProtection").on(
      "change",
      gPrivacyPane.fingerprintingProtectionReadPrefs.bind(gPrivacyPane)
    );
    Preferences.get("privacy.fingerprintingProtection.pbmode").on(
      "change",
      gPrivacyPane.fingerprintingProtectionReadPrefs.bind(gPrivacyPane)
    );

    setEventListener(
      "trackingProtectionExceptions",
      "command",
      gPrivacyPane.showTrackingProtectionExceptions
    );

    Preferences.get("privacy.sanitize.sanitizeOnShutdown").on(
      "change",
      gPrivacyPane._updateSanitizeSettingsButton.bind(gPrivacyPane)
    );
    Preferences.get("browser.privatebrowsing.autostart").on(
      "change",
      gPrivacyPane.updatePrivacyMicroControls.bind(gPrivacyPane)
    );
    setEventListener("historyMode", "command", function () {
      gPrivacyPane.updateHistoryModePane();
      gPrivacyPane.updateHistoryModePrefs();
      gPrivacyPane.updatePrivacyMicroControls();
      gPrivacyPane.updateAutostart();
    });
    setEventListener("clearHistoryButton", "command", function () {
      let historyMode = document.getElementById("historyMode");
      // Select "everything" in the clear history dialog if the
      // user has set their history mode to never remember history.
      gPrivacyPane.clearPrivateDataNow(historyMode.value == "dontremember");
    });
    setEventListener(
      "privateBrowsingAutoStart",
      "command",
      gPrivacyPane.updateAutostart
    );
    setEventListener(
      "cookieExceptions",
      "command",
      gPrivacyPane.showCookieExceptions
    );
    setEventListener(
      "httpsOnlyExceptionButton",
      "command",
      gPrivacyPane.showHttpsOnlyModeExceptions
    );
    setEventListener(
      "dohExceptionsButton",
      "command",
      gPrivacyPane.showDoHExceptions
    );
    setEventListener(
      "clearDataSettings",
      "command",
      gPrivacyPane.showClearPrivateDataSettings
    );
    setEventListener(
      "passwordExceptions",
      "command",
      gPrivacyPane.showPasswordExceptions
    );
    setEventListener(
      "useMasterPassword",
      "command",
      gPrivacyPane.updateMasterPasswordButton
    );
    setEventListener(
      "changeMasterPassword",
      "command",
      gPrivacyPane.changeMasterPassword
    );
    setEventListener("showPasswords", "command", gPrivacyPane.showPasswords);
    setEventListener(
      "addonExceptions",
      "command",
      gPrivacyPane.showAddonExceptions
    );
    setEventListener(
      "viewCertificatesButton",
      "command",
      gPrivacyPane.showCertificates
    );
    setEventListener(
      "viewSecurityDevicesButton",
      "command",
      gPrivacyPane.showSecurityDevices
    );

    this._pane = document.getElementById("panePrivacy");

    this._initGlobalPrivacyControlUI();
    this._initPasswordGenerationUI();
    this._initRelayIntegrationUI();
    this._initMasterPasswordUI();
    this._initOSAuthentication();

    this.initListenersForExtensionControllingPasswordManager();

    this._initSafeBrowsing();

    setEventListener(
      "autoplaySettingsButton",
      "command",
      gPrivacyPane.showAutoplayMediaExceptions
    );
    setEventListener(
      "notificationSettingsButton",
      "command",
      gPrivacyPane.showNotificationExceptions
    );
    setEventListener(
      "locationSettingsButton",
      "command",
      gPrivacyPane.showLocationExceptions
    );
    setEventListener(
      "xrSettingsButton",
      "command",
      gPrivacyPane.showXRExceptions
    );
    setEventListener(
      "cameraSettingsButton",
      "command",
      gPrivacyPane.showCameraExceptions
    );
    setEventListener(
      "microphoneSettingsButton",
      "command",
      gPrivacyPane.showMicrophoneExceptions
    );
    document.getElementById("speakerSettingsRow").hidden =
      !Services.prefs.getBoolPref("media.setsinkid.enabled", false);
    setEventListener(
      "speakerSettingsButton",
      "command",
      gPrivacyPane.showSpeakerExceptions
    );
    setEventListener(
      "popupPolicyButton",
      "command",
      gPrivacyPane.showPopupExceptions
    );
    setEventListener(
      "notificationsDoNotDisturb",
      "command",
      gPrivacyPane.toggleDoNotDisturbNotifications
    );

    setSyncFromPrefListener("contentBlockingBlockCookiesCheckbox", () =>
      this.readBlockCookies()
    );
    setSyncToPrefListener("contentBlockingBlockCookiesCheckbox", () =>
      this.writeBlockCookies()
    );
    setSyncFromPrefListener("blockCookiesMenu", () =>
      this.readBlockCookiesFrom()
    );
    setSyncToPrefListener("blockCookiesMenu", () =>
      this.writeBlockCookiesFrom()
    );

    setSyncFromPrefListener("savePasswords", () => this.readSavePasswords());

    let microControlHandler = el =>
      this.ensurePrivacyMicroControlUncheckedWhenDisabled(el);
    setSyncFromPrefListener("rememberHistory", microControlHandler);
    setSyncFromPrefListener("rememberForms", microControlHandler);
    setSyncFromPrefListener("alwaysClear", microControlHandler);

    setSyncFromPrefListener("popupPolicy", () =>
      this.updateButtons("popupPolicyButton", "dom.disable_open_during_load")
    );
    setSyncFromPrefListener("warnAddonInstall", () =>
      this.readWarnAddonInstall()
    );
    setSyncFromPrefListener("enableOCSP", () => this.readEnableOCSP());
    setSyncToPrefListener("enableOCSP", () => this.writeEnableOCSP());

    if (AlertsServiceDND) {
      let notificationsDoNotDisturbBox = document.getElementById(
        "notificationsDoNotDisturbBox"
      );
      notificationsDoNotDisturbBox.removeAttribute("hidden");
      let checkbox = document.getElementById("notificationsDoNotDisturb");
      document.l10n.setAttributes(checkbox, "permissions-notification-pause");
      if (AlertsServiceDND.manualDoNotDisturb) {
        let notificationsDoNotDisturb = document.getElementById(
          "notificationsDoNotDisturb"
        );
        notificationsDoNotDisturb.setAttribute("checked", true);
      }
    }

    let onNimbus = () => this._updateFirefoxSuggestToggle();
    NimbusFeatures.urlbar.onUpdate(onNimbus);
    this._updateFirefoxSuggestToggle();
    window.addEventListener("unload", () => {
      NimbusFeatures.urlbar.offUpdate(onNimbus);
    });

    this.initSiteDataControls();
    setEventListener(
      "clearSiteDataButton",
      "command",
      gPrivacyPane.clearSiteData
    );
    setEventListener(
      "siteDataSettings",
      "command",
      gPrivacyPane.showSiteDataSettings
    );

    this.initCookieBannerHandling();

    this.initDataCollection();

    if (AppConstants.MOZ_DATA_REPORTING) {
      this.updateSubmitHealthReportFromPref();
      Preferences.get(PREF_UPLOAD_ENABLED).on(
        "change",
        gPrivacyPane.updateSubmitHealthReportFromPref
      );
      setEventListener(
        "submitHealthReportBox",
        "command",
        gPrivacyPane.updateSubmitHealthReportToPref
      );
      if (AppConstants.MOZ_NORMANDY) {
        this.initOptOutStudyCheckbox();
      }
      this.initAddonRecommendationsCheckbox();
      this.initPrivateAttributionCheckbox();
    }

    let signonBundle = document.getElementById("signonBundle");
    let pkiBundle = document.getElementById("pkiBundle");
    appendSearchKeywords("showPasswords", [
      signonBundle.getString("loginsDescriptionAll2"),
    ]);
    appendSearchKeywords("viewSecurityDevicesButton", [
      pkiBundle.getString("enable_fips"),
    ]);

    if (!PrivateBrowsingUtils.enabled) {
      document.getElementById("privateBrowsingAutoStart").hidden = true;
      document.querySelector("menuitem[value='dontremember']").hidden = true;
    }

    let privateBrowsingPref = Preferences.get(
      "browser.privatebrowsing.autostart"
    );

    if (privateBrowsingPref.locked) {
      // If permanent private browsing mode is locked to off,
      // disable the "Never Remember History" option
      document.querySelector("menuitem[value='dontremember']").disabled =
        !privateBrowsingPref.value;

      // If we're locked in permanent private browsing mode,
      // disable the dropdown menu completely
      document.getElementById("historyMode").disabled =
        privateBrowsingPref.value;
    }

    /* init HTTPS-Only mode */
    this.initHttpsOnly();

    this.initDoH();

    this.initWebAuthn();

    // Notify observers that the UI is now ready
    Services.obs.notifyObservers(window, "privacy-pane-loaded");
  },

  initSiteDataControls() {
    Services.obs.addObserver(this, "sitedatamanager:sites-updated");
    Services.obs.addObserver(this, "sitedatamanager:updating-sites");
    let unload = () => {
      window.removeEventListener("unload", unload);
      Services.obs.removeObserver(this, "sitedatamanager:sites-updated");
      Services.obs.removeObserver(this, "sitedatamanager:updating-sites");
    };
    window.addEventListener("unload", unload);
    SiteDataManager.updateSites();
  },

  // CONTENT BLOCKING

  /**
   * Initializes the content blocking section.
   */
  initContentBlocking() {
    setEventListener(
      "contentBlockingTrackingProtectionCheckbox",
      "command",
      this.trackingProtectionWritePrefs
    );
    setEventListener(
      "contentBlockingTrackingProtectionCheckbox",
      "command",
      this._updateTrackingProtectionUI
    );
    setEventListener(
      "contentBlockingCryptominersCheckbox",
      "command",
      this.updateCryptominingLists
    );
    setEventListener(
      "contentBlockingFingerprintersCheckbox",
      "command",
      this.updateFingerprintingLists
    );
    setEventListener(
      "trackingProtectionMenu",
      "command",
      this.trackingProtectionWritePrefs
    );
    setEventListener(
      "contentBlockingFingerprintingProtectionCheckbox",
      "command",
      e => {
        const extra = { checked: e.target.checked };
        Glean.privacyUiFppClick.checkbox.record(extra);
        this.fingerprintingProtectionWritePrefs();
      }
    );
    setEventListener("fingerprintingProtectionMenu", "command", e => {
      const extra = { value: e.target.value };
      Glean.privacyUiFppClick.menu.record(extra);
      this.fingerprintingProtectionWritePrefs();
    });
    setEventListener("standardArrow", "command", this.toggleExpansion);
    setEventListener("strictArrow", "command", this.toggleExpansion);
    setEventListener("customArrow", "command", this.toggleExpansion);

    Preferences.get("network.cookie.cookieBehavior").on(
      "change",
      gPrivacyPane.readBlockCookies.bind(gPrivacyPane)
    );
    Preferences.get("browser.contentblocking.category").on(
      "change",
      gPrivacyPane.highlightCBCategory
    );

    // If any relevant content blocking pref changes, show a warning that the changes will
    // not be implemented until they refresh their tabs.
    for (let pref of CONTENT_BLOCKING_PREFS) {
      Preferences.get(pref).on("change", gPrivacyPane.maybeNotifyUserToReload);
      // If the value changes, run populateCategoryContents, since that change might have been
      // triggered by a default value changing in the standard category.
      Preferences.get(pref).on("change", gPrivacyPane.populateCategoryContents);
    }
    Preferences.get("urlclassifier.trackingTable").on(
      "change",
      gPrivacyPane.maybeNotifyUserToReload
    );
    for (let button of document.querySelectorAll(".reload-tabs-button")) {
      button.addEventListener("command", gPrivacyPane.reloadAllOtherTabs);
    }

    let cryptoMinersOption = document.getElementById(
      "contentBlockingCryptominersOption"
    );
    let fingerprintersOption = document.getElementById(
      "contentBlockingFingerprintersOption"
    );
    let trackingAndIsolateOption = document.querySelector(
      "#blockCookiesMenu menuitem[value='trackers-plus-isolate']"
    );
    cryptoMinersOption.hidden = !Services.prefs.getBoolPref(
      "browser.contentblocking.cryptomining.preferences.ui.enabled"
    );
    fingerprintersOption.hidden = !Services.prefs.getBoolPref(
      "browser.contentblocking.fingerprinting.preferences.ui.enabled"
    );
    let updateTrackingAndIsolateOption = () => {
      trackingAndIsolateOption.hidden =
        !Services.prefs.getBoolPref(
          "browser.contentblocking.reject-and-isolate-cookies.preferences.ui.enabled",
          false
        ) || gIsFirstPartyIsolated;
    };
    Preferences.get("privacy.firstparty.isolate").on(
      "change",
      updateTrackingAndIsolateOption
    );
    updateTrackingAndIsolateOption();

    Preferences.get("browser.contentblocking.features.strict").on(
      "change",
      this.populateCategoryContents
    );
    this.populateCategoryContents();
    this.highlightCBCategory();
    this.readBlockCookies();

    // Toggles the text "Cross-site and social media trackers" based on the
    // social tracking pref. If the pref is false, the text reads
    // "Cross-site trackers".
    const STP_COOKIES_PREF = "privacy.socialtracking.block_cookies.enabled";
    if (Services.prefs.getBoolPref(STP_COOKIES_PREF)) {
      let contentBlockOptionSocialMedia = document.getElementById(
        "blockCookiesSocialMedia"
      );

      document.l10n.setAttributes(
        contentBlockOptionSocialMedia,
        "sitedata-option-block-cross-site-tracking-cookies"
      );
    }

    Preferences.get("privacy.resistFingerprinting").on(
      "change",
      setUpContentBlockingWarnings
    );
    Preferences.get("privacy.resistFingerprinting.pbmode").on(
      "change",
      setUpContentBlockingWarnings
    );

    setUpContentBlockingWarnings();

    initTCPStandardSection();
  },

  populateCategoryContents() {
    for (let type of ["strict", "standard"]) {
      let rulesArray = [];
      let selector;
      if (type == "strict") {
        selector = "#contentBlockingOptionStrict";
        rulesArray = Services.prefs
          .getStringPref("browser.contentblocking.features.strict")
          .split(",");
        if (gIsFirstPartyIsolated) {
          let idx = rulesArray.indexOf("cookieBehavior5");
          if (idx != -1) {
            rulesArray[idx] = "cookieBehavior4";
          }
        }
      } else {
        selector = "#contentBlockingOptionStandard";
        // In standard show/hide UI items based on the default values of the relevant prefs.
        let defaults = Services.prefs.getDefaultBranch("");

        let cookieBehavior = defaults.getIntPref(
          "network.cookie.cookieBehavior"
        );
        switch (cookieBehavior) {
          case Ci.nsICookieService.BEHAVIOR_ACCEPT:
            rulesArray.push("cookieBehavior0");
            break;
          case Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN:
            rulesArray.push("cookieBehavior1");
            break;
          case Ci.nsICookieService.BEHAVIOR_REJECT:
            rulesArray.push("cookieBehavior2");
            break;
          case Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN:
            rulesArray.push("cookieBehavior3");
            break;
          case Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER:
            rulesArray.push("cookieBehavior4");
            break;
          case BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN:
            rulesArray.push(
              gIsFirstPartyIsolated ? "cookieBehavior4" : "cookieBehavior5"
            );
            break;
        }
        let cookieBehaviorPBM = defaults.getIntPref(
          "network.cookie.cookieBehavior.pbmode"
        );
        switch (cookieBehaviorPBM) {
          case Ci.nsICookieService.BEHAVIOR_ACCEPT:
            rulesArray.push("cookieBehaviorPBM0");
            break;
          case Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN:
            rulesArray.push("cookieBehaviorPBM1");
            break;
          case Ci.nsICookieService.BEHAVIOR_REJECT:
            rulesArray.push("cookieBehaviorPBM2");
            break;
          case Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN:
            rulesArray.push("cookieBehaviorPBM3");
            break;
          case Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER:
            rulesArray.push("cookieBehaviorPBM4");
            break;
          case BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN:
            rulesArray.push(
              gIsFirstPartyIsolated
                ? "cookieBehaviorPBM4"
                : "cookieBehaviorPBM5"
            );
            break;
        }
        rulesArray.push(
          defaults.getBoolPref(
            "privacy.trackingprotection.cryptomining.enabled"
          )
            ? "cm"
            : "-cm"
        );
        rulesArray.push(
          defaults.getBoolPref(
            "privacy.trackingprotection.fingerprinting.enabled"
          )
            ? "fp"
            : "-fp"
        );
        rulesArray.push(
          Services.prefs.getBoolPref(
            "privacy.socialtracking.block_cookies.enabled"
          )
            ? "stp"
            : "-stp"
        );
        rulesArray.push(
          defaults.getBoolPref("privacy.trackingprotection.enabled")
            ? "tp"
            : "-tp"
        );
        rulesArray.push(
          defaults.getBoolPref("privacy.trackingprotection.pbmode.enabled")
            ? "tpPrivate"
            : "-tpPrivate"
        );
      }

      // Hide all cookie options first, until we learn which one should be showing.
      document.querySelector(selector + " .all-cookies-option").hidden = true;
      document.querySelector(selector + " .unvisited-cookies-option").hidden =
        true;
      document.querySelector(selector + " .cross-site-cookies-option").hidden =
        true;
      document.querySelector(
        selector + " .third-party-tracking-cookies-option"
      ).hidden = true;
      document.querySelector(
        selector + " .all-third-party-cookies-private-windows-option"
      ).hidden = true;
      document.querySelector(
        selector + " .all-third-party-cookies-option"
      ).hidden = true;
      document.querySelector(selector + " .social-media-option").hidden = true;

      for (let item of rulesArray) {
        // Note "cookieBehavior0", will result in no UI changes, so is not listed here.
        switch (item) {
          case "tp":
            document.querySelector(selector + " .trackers-option").hidden =
              false;
            break;
          case "-tp":
            document.querySelector(selector + " .trackers-option").hidden =
              true;
            break;
          case "tpPrivate":
            document.querySelector(selector + " .pb-trackers-option").hidden =
              false;
            break;
          case "-tpPrivate":
            document.querySelector(selector + " .pb-trackers-option").hidden =
              true;
            break;
          case "fp":
            document.querySelector(
              selector + " .fingerprinters-option"
            ).hidden = false;
            break;
          case "-fp":
            document.querySelector(
              selector + " .fingerprinters-option"
            ).hidden = true;
            break;
          case "cm":
            document.querySelector(selector + " .cryptominers-option").hidden =
              false;
            break;
          case "-cm":
            document.querySelector(selector + " .cryptominers-option").hidden =
              true;
            break;
          case "stp": {
            // Store social tracking cookies pref
            const STP_COOKIES_PREF =
              "privacy.socialtracking.block_cookies.enabled";

            if (Services.prefs.getBoolPref(STP_COOKIES_PREF)) {
              document.querySelector(
                selector + " .social-media-option"
              ).hidden = false;
            }
            break;
          }
          case "-stp":
            // Store social tracking cookies pref
            document.querySelector(selector + " .social-media-option").hidden =
              true;
            break;
          case "cookieBehavior1":
            document.querySelector(
              selector + " .all-third-party-cookies-option"
            ).hidden = false;
            break;
          case "cookieBehavior2":
            document.querySelector(selector + " .all-cookies-option").hidden =
              false;
            break;
          case "cookieBehavior3":
            document.querySelector(
              selector + " .unvisited-cookies-option"
            ).hidden = false;
            break;
          case "cookieBehavior4":
            document.querySelector(
              selector + " .third-party-tracking-cookies-option"
            ).hidden = false;
            break;
          case "cookieBehavior5":
            document.querySelector(
              selector + " .cross-site-cookies-option"
            ).hidden = false;
            break;
          case "cookieBehaviorPBM5":
            // We only need to show the cookie option for private windows if the
            // cookieBehaviors are different between regular windows and private
            // windows.
            if (!rulesArray.includes("cookieBehavior5")) {
              document.querySelector(
                selector + " .all-third-party-cookies-private-windows-option"
              ).hidden = false;
            }
            break;
        }
      }
      // Hide the "tracking protection in private browsing" list item
      // if the "tracking protection enabled in all windows" list item is showing.
      if (!document.querySelector(selector + " .trackers-option").hidden) {
        document.querySelector(selector + " .pb-trackers-option").hidden = true;
      }
    }
  },

  highlightCBCategory() {
    let value = Preferences.get("browser.contentblocking.category").value;
    let standardEl = document.getElementById("contentBlockingOptionStandard");
    let strictEl = document.getElementById("contentBlockingOptionStrict");
    let customEl = document.getElementById("contentBlockingOptionCustom");
    standardEl.classList.remove("selected");
    strictEl.classList.remove("selected");
    customEl.classList.remove("selected");

    switch (value) {
      case "strict":
        strictEl.classList.add("selected");
        break;
      case "custom":
        customEl.classList.add("selected");
        break;
      case "standard":
      /* fall through */
      default:
        standardEl.classList.add("selected");
        break;
    }
  },

  updateCryptominingLists() {
    let listPrefs = [
      "urlclassifier.features.cryptomining.blacklistTables",
      "urlclassifier.features.cryptomining.whitelistTables",
    ];

    let listValue = listPrefs
      .map(l => Services.prefs.getStringPref(l))
      .join(",");
    listManager.forceUpdates(listValue);
  },

  updateFingerprintingLists() {
    let listPrefs = [
      "urlclassifier.features.fingerprinting.blacklistTables",
      "urlclassifier.features.fingerprinting.whitelistTables",
    ];

    let listValue = listPrefs
      .map(l => Services.prefs.getStringPref(l))
      .join(",");
    listManager.forceUpdates(listValue);
  },

  // TRACKING PROTECTION MODE

  /**
   * Selects the right item of the Tracking Protection menulist and checkbox.
   */
  trackingProtectionReadPrefs() {
    let enabledPref = Preferences.get("privacy.trackingprotection.enabled");
    let pbmPref = Preferences.get("privacy.trackingprotection.pbmode.enabled");
    let tpMenu = document.getElementById("trackingProtectionMenu");
    let tpCheckbox = document.getElementById(
      "contentBlockingTrackingProtectionCheckbox"
    );

    this._updateTrackingProtectionUI();

    // Global enable takes precedence over enabled in Private Browsing.
    if (enabledPref.value) {
      tpMenu.value = "always";
      tpCheckbox.checked = true;
    } else if (pbmPref.value) {
      tpMenu.value = "private";
      tpCheckbox.checked = true;
    } else {
      tpMenu.value = "never";
      tpCheckbox.checked = false;
    }
  },

  /**
   * Selects the right item of the Fingerprinting Protection menulist and
   * checkbox.
   */
  fingerprintingProtectionReadPrefs() {
    let enabledPref = Preferences.get("privacy.fingerprintingProtection");
    let pbmPref = Preferences.get("privacy.fingerprintingProtection.pbmode");
    let fppMenu = document.getElementById("fingerprintingProtectionMenu");
    let fppCheckbox = document.getElementById(
      "contentBlockingFingerprintingProtectionCheckbox"
    );

    // Global enable takes precedence over enabled in Private Browsing.
    if (enabledPref.value) {
      fppMenu.value = "always";
      fppCheckbox.checked = true;
    } else if (pbmPref.value) {
      fppMenu.value = "private";
      fppCheckbox.checked = true;
    } else {
      fppMenu.value = "never";
      fppCheckbox.checked = false;
    }
    fppMenu.disabled = !fppCheckbox.checked;
  },

  /**
   * Selects the right items of the new Cookies & Site Data UI.
   */
  networkCookieBehaviorReadPrefs() {
    let behavior = Services.cookies.getCookieBehavior(false);
    let blockCookiesMenu = document.getElementById("blockCookiesMenu");
    let deleteOnCloseCheckbox = document.getElementById("deleteOnClose");
    let deleteOnCloseNote = document.getElementById("deleteOnCloseNote");
    let blockCookies = behavior != Ci.nsICookieService.BEHAVIOR_ACCEPT;
    let cookieBehaviorLocked = Services.prefs.prefIsLocked(
      "network.cookie.cookieBehavior"
    );
    let blockCookiesControlsDisabled = !blockCookies || cookieBehaviorLocked;
    blockCookiesMenu.disabled = blockCookiesControlsDisabled;

    let completelyBlockCookies =
      behavior == Ci.nsICookieService.BEHAVIOR_REJECT;
    let privateBrowsing = Preferences.get(
      "browser.privatebrowsing.autostart"
    ).value;
    deleteOnCloseCheckbox.disabled = privateBrowsing || completelyBlockCookies;
    deleteOnCloseNote.hidden = !privateBrowsing;

    switch (behavior) {
      case Ci.nsICookieService.BEHAVIOR_ACCEPT:
        break;
      case Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN:
        blockCookiesMenu.value = "all-third-parties";
        break;
      case Ci.nsICookieService.BEHAVIOR_REJECT:
        blockCookiesMenu.value = "always";
        break;
      case Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN:
        blockCookiesMenu.value = "unvisited";
        break;
      case Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER:
        blockCookiesMenu.value = "trackers";
        break;
      case BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN:
        blockCookiesMenu.value = "trackers-plus-isolate";
        break;
    }
  },

  /**
   * Sets the pref values based on the selected item of the radiogroup.
   */
  trackingProtectionWritePrefs() {
    let enabledPref = Preferences.get("privacy.trackingprotection.enabled");
    let pbmPref = Preferences.get("privacy.trackingprotection.pbmode.enabled");
    let stpPref = Preferences.get(
      "privacy.trackingprotection.socialtracking.enabled"
    );
    let stpCookiePref = Preferences.get(
      "privacy.socialtracking.block_cookies.enabled"
    );
    // Currently, we don't expose the email tracking protection setting on our
    // privacy UI. Instead, we use the existing tracking protection checkbox to
    // control the email tracking protection.
    let emailTPPref = Preferences.get(
      "privacy.trackingprotection.emailtracking.enabled"
    );
    let emailTPPBMPref = Preferences.get(
      "privacy.trackingprotection.emailtracking.pbmode.enabled"
    );
    let tpMenu = document.getElementById("trackingProtectionMenu");
    let tpCheckbox = document.getElementById(
      "contentBlockingTrackingProtectionCheckbox"
    );

    let value;
    if (tpCheckbox.checked) {
      if (tpMenu.value == "never") {
        tpMenu.value = "private";
      }
      value = tpMenu.value;
    } else {
      tpMenu.value = "never";
      value = "never";
    }

    switch (value) {
      case "always":
        enabledPref.value = true;
        pbmPref.value = true;
        emailTPPref.value = true;
        emailTPPBMPref.value = true;
        if (stpCookiePref.value) {
          stpPref.value = true;
        }
        break;
      case "private":
        enabledPref.value = false;
        pbmPref.value = true;
        emailTPPref.value = false;
        emailTPPBMPref.value = true;
        if (stpCookiePref.value) {
          stpPref.value = false;
        }
        break;
      case "never":
        enabledPref.value = false;
        pbmPref.value = false;
        emailTPPref.value = false;
        emailTPPBMPref.value = false;
        if (stpCookiePref.value) {
          stpPref.value = false;
        }
        break;
    }
  },

  fingerprintingProtectionWritePrefs() {
    let enabledPref = Preferences.get("privacy.fingerprintingProtection");
    let pbmPref = Preferences.get("privacy.fingerprintingProtection.pbmode");
    let fppMenu = document.getElementById("fingerprintingProtectionMenu");
    let fppCheckbox = document.getElementById(
      "contentBlockingFingerprintingProtectionCheckbox"
    );

    let value;
    if (fppCheckbox.checked) {
      if (fppMenu.value == "never") {
        fppMenu.value = "private";
      }
      value = fppMenu.value;
    } else {
      fppMenu.value = "never";
      value = "never";
    }

    fppMenu.disabled = !fppCheckbox.checked;

    switch (value) {
      case "always":
        enabledPref.value = true;
        pbmPref.value = true;
        break;
      case "private":
        enabledPref.value = false;
        pbmPref.value = true;
        break;
      case "never":
        enabledPref.value = false;
        pbmPref.value = false;
        break;
    }
  },

  toggleExpansion(e) {
    let carat = e.target;
    carat.classList.toggle("up");
    carat.closest(".privacy-detailedoption").classList.toggle("expanded");
    carat.setAttribute(
      "aria-expanded",
      carat.getAttribute("aria-expanded") === "false"
    );
  },

  // HISTORY MODE

  /**
   * The list of preferences which affect the initial history mode settings.
   * If the auto start private browsing mode pref is active, the initial
   * history mode would be set to "Don't remember anything".
   * If ALL of these preferences are set to the values that correspond
   * to keeping some part of history, and the auto-start
   * private browsing mode is not active, the initial history mode would be
   * set to "Remember everything".
   * Otherwise, the initial history mode would be set to "Custom".
   *
   * Extensions adding their own preferences can set values here if needed.
   */
  prefsForKeepingHistory: {
    "places.history.enabled": true, // History is enabled
    "browser.formfill.enable": true, // Form information is saved
    "privacy.sanitize.sanitizeOnShutdown": false, // Private date is NOT cleared on shutdown
  },

  /**
   * The list of control IDs which are dependent on the auto-start private
   * browsing setting, such that in "Custom" mode they would be disabled if
   * the auto-start private browsing checkbox is checked, and enabled otherwise.
   *
   * Extensions adding their own controls can append their IDs to this array if needed.
   */
  dependentControls: [
    "rememberHistory",
    "rememberForms",
    "alwaysClear",
    "clearDataSettings",
  ],

  /**
   * Check whether preferences values are set to keep history
   *
   * @param aPrefs an array of pref names to check for
   * @returns boolean true if all of the prefs are set to keep history,
   *                  false otherwise
   */
  _checkHistoryValues(aPrefs) {
    for (let pref of Object.keys(aPrefs)) {
      if (Preferences.get(pref).value != aPrefs[pref]) {
        return false;
      }
    }
    return true;
  },

  /**
   * Initialize the history mode menulist based on the privacy preferences
   */
  initializeHistoryMode() {
    let mode;
    let getVal = aPref => Preferences.get(aPref).value;

    if (getVal("privacy.history.custom")) {
      mode = "custom";
    } else if (this._checkHistoryValues(this.prefsForKeepingHistory)) {
      if (getVal("browser.privatebrowsing.autostart")) {
        mode = "dontremember";
      } else {
        mode = "remember";
      }
    } else {
      mode = "custom";
    }

    document.getElementById("historyMode").value = mode;
  },

  /**
   * Update the selected pane based on the history mode menulist
   */
  updateHistoryModePane() {
    let selectedIndex = -1;
    switch (document.getElementById("historyMode").value) {
      case "remember":
        selectedIndex = 0;
        break;
      case "dontremember":
        selectedIndex = 1;
        break;
      case "custom":
        selectedIndex = 2;
        break;
    }
    document.getElementById("historyPane").selectedIndex = selectedIndex;
    Preferences.get("privacy.history.custom").value = selectedIndex == 2;
  },

  /**
   * Update the private browsing auto-start pref and the history mode
   * micro-management prefs based on the history mode menulist
   */
  updateHistoryModePrefs() {
    let pref = Preferences.get("browser.privatebrowsing.autostart");
    switch (document.getElementById("historyMode").value) {
      case "remember":
        if (pref.value) {
          pref.value = false;
        }

        // select the remember history option if needed
        Preferences.get("places.history.enabled").value = true;

        // select the remember forms history option
        Preferences.get("browser.formfill.enable").value = true;

        // select the clear on close option
        Preferences.get("privacy.sanitize.sanitizeOnShutdown").value = false;
        break;
      case "dontremember":
        if (!pref.value) {
          pref.value = true;
        }
        break;
    }
  },

  /**
   * Update the privacy micro-management controls based on the
   * value of the private browsing auto-start preference.
   */
  updatePrivacyMicroControls() {
    let clearDataSettings = document.getElementById("clearDataSettings");

    if (document.getElementById("historyMode").value == "custom") {
      let disabled = Preferences.get("browser.privatebrowsing.autostart").value;
      this.dependentControls.forEach(aElement => {
        let control = document.getElementById(aElement);
        let preferenceId = control.getAttribute("preference");
        if (!preferenceId) {
          let dependentControlId = control.getAttribute("control");
          if (dependentControlId) {
            let dependentControl = document.getElementById(dependentControlId);
            preferenceId = dependentControl.getAttribute("preference");
          }
        }

        let preference = preferenceId ? Preferences.get(preferenceId) : {};
        control.disabled = disabled || preference.locked;
        if (control != clearDataSettings) {
          this.ensurePrivacyMicroControlUncheckedWhenDisabled(control);
        }
      });

      clearDataSettings.removeAttribute("hidden");

      if (!disabled) {
        // adjust the Settings button for sanitizeOnShutdown
        this._updateSanitizeSettingsButton();
      }
    } else {
      clearDataSettings.hidden = true;
    }
  },

  ensurePrivacyMicroControlUncheckedWhenDisabled(el) {
    if (Preferences.get("browser.privatebrowsing.autostart").value) {
      // Set checked to false when called from updatePrivacyMicroControls
      el.checked = false;
      // return false for the onsyncfrompreference case:
      return false;
    }
    return undefined; // tell preferencesBindings to assign the 'right' value.
  },

  // CLEAR PRIVATE DATA

  /*
   * Preferences:
   *
   * privacy.sanitize.sanitizeOnShutdown
   * - true if the user's private data is cleared on startup according to the
   *   Clear Private Data settings, false otherwise
   */

  /**
   * Displays the Clear Private Data settings dialog.
   */
  showClearPrivateDataSettings() {
    let dialogFile = useOldClearHistoryDialog
      ? "chrome://browser/content/preferences/dialogs/sanitize.xhtml"
      : "chrome://browser/content/sanitize_v2.xhtml";

    gSubDialog.open(
      dialogFile,
      {
        features: "resizable=no",
      },
      {
        mode: "clearOnShutdown",
      }
    );
  },

  /**
   * Displays a dialog from which individual parts of private data may be
   * cleared.
   */
  clearPrivateDataNow(aClearEverything) {
    var ts = Preferences.get("privacy.sanitize.timeSpan");
    var timeSpanOrig = ts.value;

    if (aClearEverything) {
      ts.value = 0;
    }

    // Bug 1856418 We intend to remove the old dialog box
    let dialogFile = useOldClearHistoryDialog
      ? "chrome://browser/content/sanitize.xhtml"
      : "chrome://browser/content/sanitize_v2.xhtml";

    gSubDialog.open(dialogFile, {
      features: "resizable=no",
      closingCallback: () => {
        // reset the timeSpan pref
        if (aClearEverything) {
          ts.value = timeSpanOrig;
        }

        Services.obs.notifyObservers(null, "clear-private-data");
      },
    });
  },

  /*
   * On loading the page, assigns the state to the deleteOnClose checkbox that fits the pref selection
   */
  initDeleteOnCloseBox() {
    // Make sure to do the migration for the clear history dialog before implementing logic for delete on close
    // This needs to be done to make sure the migration is done before any pref changes are made to avoid unintentionally
    // overwriting prefs
    Sanitizer.maybeMigratePrefs("clearOnShutdown");

    let deleteOnCloseBox = document.getElementById("deleteOnClose");

    // We have to branch between the old clear on shutdown prefs and new prefs after the clear history revamp (Bug 1853996)
    // Once the old dialog is deprecated, we can remove these branches.
    let isCookiesAndStorageClearingOnShutdown;
    if (useOldClearHistoryDialog) {
      isCookiesAndStorageClearingOnShutdown =
        Preferences.get("privacy.sanitize.sanitizeOnShutdown").value &&
        Preferences.get("privacy.clearOnShutdown.cookies").value &&
        Preferences.get("privacy.clearOnShutdown.cache").value &&
        Preferences.get("privacy.clearOnShutdown.offlineApps").value;
    } else {
      isCookiesAndStorageClearingOnShutdown =
        Preferences.get("privacy.sanitize.sanitizeOnShutdown").value &&
        Preferences.get("privacy.clearOnShutdown_v2.cookiesAndStorage").value &&
        Preferences.get("privacy.clearOnShutdown_v2.cache").value;
    }

    deleteOnCloseBox.checked =
      isCookiesAndStorageClearingOnShutdown ||
      Preferences.get("browser.privatebrowsing.autostart").value;
  },

  /*
   * Keeps the state of the deleteOnClose checkbox in sync with the pref selection
   */
  syncSanitizationPrefsWithDeleteOnClose() {
    let deleteOnCloseBox = document.getElementById("deleteOnClose");
    let historyMode = Preferences.get("privacy.history.custom");
    let sanitizeOnShutdownPref = Preferences.get(
      "privacy.sanitize.sanitizeOnShutdown"
    );

    // ClearOnClose cleaning categories
    let cookiePref = useOldClearHistoryDialog
      ? Preferences.get("privacy.clearOnShutdown.cookies")
      : Preferences.get("privacy.clearOnShutdown_v2.cookiesAndStorage");
    let cachePref = useOldClearHistoryDialog
      ? Preferences.get("privacy.clearOnShutdown.cache")
      : Preferences.get("privacy.clearOnShutdown_v2.cache");
    let offlineAppsPref = useOldClearHistoryDialog
      ? Preferences.get("privacy.clearOnShutdown.offlineApps")
      : Preferences.get("privacy.clearOnShutdown_v2.cookiesAndStorage");

    // Sync the cleaning prefs with the deleteOnClose box
    deleteOnCloseBox.addEventListener("command", () => {
      let { checked } = deleteOnCloseBox;
      cookiePref.value = checked;
      cachePref.value = checked;
      offlineAppsPref.value = checked;
      // Forget the current pref selection if sanitizeOnShutdown is disabled,
      // to not over clear when it gets enabled by the sync mechanism
      if (!sanitizeOnShutdownPref.value) {
        this._resetCleaningPrefs();
      }
      // If no other cleaning category is selected, sanitizeOnShutdown gets synced with deleteOnClose
      sanitizeOnShutdownPref.value =
        this._isCustomCleaningPrefPresent() || checked;

      // Update the view of the history settings
      if (checked && !historyMode.value) {
        historyMode.value = "custom";
        this.initializeHistoryMode();
        this.updateHistoryModePane();
        this.updatePrivacyMicroControls();
      }
    });

    cookiePref.on("change", this._onSanitizePrefChangeSyncClearOnClose);
    cachePref.on("change", this._onSanitizePrefChangeSyncClearOnClose);
    offlineAppsPref.on("change", this._onSanitizePrefChangeSyncClearOnClose);
    sanitizeOnShutdownPref.on(
      "change",
      this._onSanitizePrefChangeSyncClearOnClose
    );
  },

  /*
   * Sync the deleteOnClose box to its cleaning prefs
   */
  _onSanitizePrefChangeSyncClearOnClose() {
    let deleteOnCloseBox = document.getElementById("deleteOnClose");

    // We have to branch between the old clear on shutdown prefs and new prefs after the clear history revamp (Bug 1853996)
    // Once the old dialog is deprecated, we can remove these branches.
    if (useOldClearHistoryDialog) {
      deleteOnCloseBox.checked =
        Preferences.get("privacy.sanitize.sanitizeOnShutdown").value &&
        Preferences.get("privacy.clearOnShutdown.cookies").value &&
        Preferences.get("privacy.clearOnShutdown.cache").value &&
        Preferences.get("privacy.clearOnShutdown.offlineApps").value;
    } else {
      deleteOnCloseBox.checked =
        Preferences.get("privacy.sanitize.sanitizeOnShutdown").value &&
        Preferences.get("privacy.clearOnShutdown_v2.cookiesAndStorage").value &&
        Preferences.get("privacy.clearOnShutdown_v2.cache").value;
    }
  },

  /*
   * Unsets cleaning prefs that do not belong to DeleteOnClose
   */
  _resetCleaningPrefs() {
    let sanitizeOnShutdownPrefsArray = useOldClearHistoryDialog
      ? SANITIZE_ON_SHUTDOWN_PREFS_ONLY
      : SANITIZE_ON_SHUTDOWN_PREFS_ONLY_V2;

    return sanitizeOnShutdownPrefsArray.forEach(
      pref => (Preferences.get(pref).value = false)
    );
  },

  /*
   Checks if the user set cleaning prefs that do not belong to DeleteOnClose
   */
  _isCustomCleaningPrefPresent() {
    let sanitizeOnShutdownPrefsArray = useOldClearHistoryDialog
      ? SANITIZE_ON_SHUTDOWN_PREFS_ONLY
      : SANITIZE_ON_SHUTDOWN_PREFS_ONLY_V2;

    return sanitizeOnShutdownPrefsArray.some(
      pref => Preferences.get(pref).value
    );
  },

  /**
   * Enables or disables the "Settings..." button depending
   * on the privacy.sanitize.sanitizeOnShutdown preference value
   */
  _updateSanitizeSettingsButton() {
    var settingsButton = document.getElementById("clearDataSettings");
    var sanitizeOnShutdownPref = Preferences.get(
      "privacy.sanitize.sanitizeOnShutdown"
    );

    settingsButton.disabled = !sanitizeOnShutdownPref.value;
  },

  toggleDoNotDisturbNotifications(event) {
    AlertsServiceDND.manualDoNotDisturb = event.target.checked;
  },

  // PRIVATE BROWSING

  /**
   * Initialize the starting state for the auto-start private browsing mode pref reverter.
   */
  initAutoStartPrivateBrowsingReverter() {
    // We determine the mode in initializeHistoryMode, which is guaranteed to have been
    // called before now, so this is up-to-date.
    let mode = document.getElementById("historyMode");
    this._lastMode = mode.selectedIndex;
    // The value of the autostart pref, on the other hand, is gotten from Preferences,
    // which updates the DOM asynchronously, so we can't rely on the DOM. Get it directly
    // from the prefs.
    this._lastCheckState = Preferences.get(
      "browser.privatebrowsing.autostart"
    ).value;
  },

  _lastMode: null,
  _lastCheckState: null,
  async updateAutostart() {
    let mode = document.getElementById("historyMode");
    let autoStart = document.getElementById("privateBrowsingAutoStart");
    let pref = Preferences.get("browser.privatebrowsing.autostart");
    if (
      (mode.value == "custom" && this._lastCheckState == autoStart.checked) ||
      (mode.value == "remember" && !this._lastCheckState) ||
      (mode.value == "dontremember" && this._lastCheckState)
    ) {
      // These are all no-op changes, so we don't need to prompt.
      this._lastMode = mode.selectedIndex;
      this._lastCheckState = autoStart.hasAttribute("checked");
      return;
    }

    if (!this._shouldPromptForRestart) {
      // We're performing a revert. Just let it happen.
      return;
    }

    let buttonIndex = await confirmRestartPrompt(
      autoStart.checked,
      1,
      true,
      false
    );
    if (buttonIndex == CONFIRM_RESTART_PROMPT_RESTART_NOW) {
      pref.value = autoStart.hasAttribute("checked");
      Services.startup.quit(
        Ci.nsIAppStartup.eAttemptQuit | Ci.nsIAppStartup.eRestart
      );
      return;
    }

    this._shouldPromptForRestart = false;

    if (this._lastCheckState) {
      autoStart.checked = "checked";
    } else {
      autoStart.removeAttribute("checked");
    }
    pref.value = autoStart.hasAttribute("checked");
    mode.selectedIndex = this._lastMode;
    mode.doCommand();

    this._shouldPromptForRestart = true;
  },

  /**
   * Displays fine-grained, per-site preferences for tracking protection.
   */
  showTrackingProtectionExceptions() {
    let params = {
      permissionType: "trackingprotection",
      disableETPVisible: true,
      prefilledHost: "",
      hideStatusColumn: true,
    };
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/permissions.xhtml",
      undefined,
      params
    );
  },

  /**
   * Displays the available block lists for tracking protection.
   */
  showBlockLists() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/blocklists.xhtml"
    );
  },

  // COOKIES AND SITE DATA

  /*
   * Preferences:
   *
   * network.cookie.cookieBehavior
   * - determines how the browser should handle cookies:
   *     0   means enable all cookies
   *     1   means reject all third party cookies
   *     2   means disable all cookies
   *     3   means reject third party cookies unless at least one is already set for the eTLD
   *     4   means reject all trackers
   *     5   means reject all trackers and partition third-party cookies
   *         see netwerk/cookie/src/CookieService.cpp for details
   */

  /**
   * Reads the network.cookie.cookieBehavior preference value and
   * enables/disables the "blockCookiesMenu" menulist accordingly.
   */
  readBlockCookies() {
    let bcControl = document.getElementById("blockCookiesMenu");
    bcControl.disabled =
      Services.cookies.getCookieBehavior(false) ==
      Ci.nsICookieService.BEHAVIOR_ACCEPT;
  },

  /**
   * Updates the "accept third party cookies" menu based on whether the
   * "contentBlockingBlockCookiesCheckbox" checkbox is checked.
   */
  writeBlockCookies() {
    let block = document.getElementById("contentBlockingBlockCookiesCheckbox");
    let blockCookiesMenu = document.getElementById("blockCookiesMenu");

    if (block.checked) {
      // Automatically select 'third-party trackers' as the default.
      blockCookiesMenu.selectedIndex = 0;
      return this.writeBlockCookiesFrom();
    }
    return Ci.nsICookieService.BEHAVIOR_ACCEPT;
  },

  readBlockCookiesFrom() {
    switch (Services.cookies.getCookieBehavior(false)) {
      case Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN:
        return "all-third-parties";
      case Ci.nsICookieService.BEHAVIOR_REJECT:
        return "always";
      case Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN:
        return "unvisited";
      case Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER:
        return "trackers";
      case BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN:
        return "trackers-plus-isolate";
      default:
        return undefined;
    }
  },

  writeBlockCookiesFrom() {
    let block = document.getElementById("blockCookiesMenu").selectedItem;
    switch (block.value) {
      case "trackers":
        return Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER;
      case "unvisited":
        return Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN;
      case "always":
        return Ci.nsICookieService.BEHAVIOR_REJECT;
      case "all-third-parties":
        return Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN;
      case "trackers-plus-isolate":
        return Ci.nsICookieService
          .BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN;
      default:
        return undefined;
    }
  },

  /**
   * Discard the browsers of all tabs in all windows. Pinned tabs, as
   * well as tabs for which discarding doesn't succeed (e.g. selected
   * tabs, tabs with beforeunload listeners), are reloaded.
   */
  reloadAllOtherTabs() {
    let ourTab = BrowserWindowTracker.getTopWindow().gBrowser.selectedTab;
    BrowserWindowTracker.orderedWindows.forEach(win => {
      let otherGBrowser = win.gBrowser;
      for (let tab of otherGBrowser.tabs) {
        if (tab == ourTab) {
          // Don't reload our preferences tab.
          continue;
        }

        if (tab.pinned || tab.selected) {
          otherGBrowser.reloadTab(tab);
        } else {
          otherGBrowser.discardBrowser(tab);
        }
      }
    });

    for (let notification of document.querySelectorAll(".reload-tabs")) {
      notification.hidden = true;
    }
  },

  /**
   * If there are more tabs than just the preferences tab, show a warning to the user that
   * they need to reload their tabs to apply the setting.
   */
  maybeNotifyUserToReload() {
    let shouldShow = false;
    if (window.BrowserWindowTracker.orderedWindows.length > 1) {
      shouldShow = true;
    } else {
      let tabbrowser = window.BrowserWindowTracker.getTopWindow().gBrowser;
      if (tabbrowser.tabs.length > 1) {
        shouldShow = true;
      }
    }
    if (shouldShow) {
      for (let notification of document.querySelectorAll(".reload-tabs")) {
        notification.hidden = false;
      }
    }
  },

  /**
   * Displays fine-grained, per-site preferences for cookies.
   */
  showCookieExceptions() {
    var params = {
      blockVisible: true,
      sessionVisible: true,
      allowVisible: true,
      prefilledHost: "",
      permissionType: "cookie",
    };
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/permissions.xhtml",
      undefined,
      params
    );
  },

  /**
   * Displays per-site preferences for HTTPS-Only Mode exceptions.
   */
  showHttpsOnlyModeExceptions() {
    var params = {
      blockVisible: false,
      sessionVisible: true,
      allowVisible: false,
      prefilledHost: "",
      permissionType: "https-only-load-insecure",
      forcedHTTP: true,
    };
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/permissions.xhtml",
      undefined,
      params
    );
  },

  showDoHExceptions() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/dohExceptions.xhtml",
      undefined
    );
  },

  showSiteDataSettings() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/siteDataSettings.xhtml"
    );
  },

  toggleSiteData(shouldShow) {
    let clearButton = document.getElementById("clearSiteDataButton");
    let settingsButton = document.getElementById("siteDataSettings");
    clearButton.disabled = !shouldShow;
    settingsButton.disabled = !shouldShow;
  },

  showSiteDataLoading() {
    let totalSiteDataSizeLabel = document.getElementById("totalSiteDataSize");
    document.l10n.setAttributes(
      totalSiteDataSizeLabel,
      "sitedata-total-size-calculating"
    );
  },

  updateTotalDataSizeLabel(siteDataUsage) {
    SiteDataManager.getCacheSize().then(function (cacheUsage) {
      let totalSiteDataSizeLabel = document.getElementById("totalSiteDataSize");
      let totalUsage = siteDataUsage + cacheUsage;
      let [value, unit] = DownloadUtils.convertByteUnits(totalUsage);
      document.l10n.setAttributes(
        totalSiteDataSizeLabel,
        "sitedata-total-size",
        {
          value,
          unit,
        }
      );
    });
  },

  clearSiteData() {
    // We have to use the full path name to avoid getting errors in
    // browser/base/content/test/static/browser_all_files_referenced.js
    let dialogFile = useOldClearHistoryDialog
      ? "chrome://browser/content/preferences/dialogs/clearSiteData.xhtml"
      : "chrome://browser/content/sanitize_v2.xhtml";

    gSubDialog.open(
      dialogFile,
      {
        features: "resizable=no",
      },
      {
        mode: "clearSiteData",
      }
    );
  },

  /**
   * Initializes the cookie banner handling subgroup on the privacy pane.
   *
   * This UI is shown if the "cookiebanners.ui.desktop.enabled" pref is true.
   *
   * The cookie banner handling checkbox reflects the cookie banner feature
   * state. It is enabled when the service enabled via the
   * cookiebanners.service.mode pref. If detection-only mode is enabled the
   * checkbox is unchecked, since in this mode no banners are handled. It is
   * only used for detection for banners which means we may prompt the user to
   * enable the feature via other UI surfaces such as the onboarding doorhanger.
   *
   * If the user checks the checkbox, the pref value is set to
   * nsICookieBannerService.MODE_REJECT_OR_ACCEPT.
   *
   * If the user unchecks the checkbox, the mode pref value is set to
   * nsICookieBannerService.MODE_DISABLED.
   *
   * Advanced users can choose other int-valued modes via about:config.
   */
  initCookieBannerHandling() {
    setSyncFromPrefListener("handleCookieBanners", () =>
      this.readCookieBannerMode()
    );
    setSyncToPrefListener("handleCookieBanners", () =>
      this.writeCookieBannerMode()
    );

    let preference = Preferences.get("cookiebanners.ui.desktop.enabled");
    preference.on("change", () => this.updateCookieBannerHandlingVisibility());

    this.updateCookieBannerHandlingVisibility();
  },

  /**
   * Reads the cookiebanners.service.mode.privateBrowsing pref,
   * interpreting the multiple modes as a true/false value
   */
  readCookieBannerMode() {
    return (
      Preferences.get("cookiebanners.service.mode.privateBrowsing").value !=
      Ci.nsICookieBannerService.MODE_DISABLED
    );
  },

  /**
   * Translates user clicks on the cookie banner handling checkbox to the
   * corresponding integer-valued cookie banner mode preference.
   */
  writeCookieBannerMode() {
    let checkbox = document.getElementById("handleCookieBanners");
    if (!checkbox.checked) {
      /* because we removed UI control for the non-PBM pref, disabling it here
         provides an off-ramp for profiles where it had previously been enabled from the UI */
      Services.prefs.setIntPref(
        "cookiebanners.service.mode",
        Ci.nsICookieBannerService.MODE_DISABLED
      );
      return Ci.nsICookieBannerService.MODE_DISABLED;
    }
    return Ci.nsICookieBannerService.MODE_REJECT;
  },

  /**
   * Shows or hides the cookie banner handling section based on the value of
   * the "cookiebanners.ui.desktop.enabled" pref.
   */
  updateCookieBannerHandlingVisibility() {
    let groupbox = document.getElementById("cookieBannerHandlingGroup");
    let isEnabled = Preferences.get("cookiebanners.ui.desktop.enabled").value;

    // Because the top-level pane showing code unsets the hidden attribute, we
    // manually hide the section when cookie banner handling is preffed off.
    if (isEnabled) {
      groupbox.removeAttribute("style");
    } else {
      groupbox.setAttribute("style", "display: none !important");
    }
  },

  /**
   * Updates the visibility of the Firefox Suggest Privacy Container
   * based on the user's Quick Suggest settings.
   */
  _updateFirefoxSuggestToggle() {
    document.getElementById(
      "firefoxSuggestDataCollectionPrivacyToggle"
    ).hidden =
      !UrlbarPrefs.get("quickSuggestEnabled") ||
      UrlbarPrefs.get("quickSuggestSettingsUi") !=
        QuickSuggest.SETTINGS_UI.FULL;
  },

  // GEOLOCATION

  /**
   * Displays the location exceptions dialog where specific site location
   * preferences can be set.
   */
  showLocationExceptions() {
    let params = { permissionType: "geo" };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },

  // XR

  /**
   * Displays the XR exceptions dialog where specific site XR
   * preferences can be set.
   */
  showXRExceptions() {
    let params = { permissionType: "xr" };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },

  // CAMERA

  /**
   * Displays the camera exceptions dialog where specific site camera
   * preferences can be set.
   */
  showCameraExceptions() {
    let params = { permissionType: "camera" };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },

  // MICROPHONE

  /**
   * Displays the microphone exceptions dialog where specific site microphone
   * preferences can be set.
   */
  showMicrophoneExceptions() {
    let params = { permissionType: "microphone" };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },

  // SPEAKER

  /**
   * Displays the speaker exceptions dialog where specific site speaker
   * preferences can be set.
   */
  showSpeakerExceptions() {
    let params = { permissionType: "speaker" };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },

  // NOTIFICATIONS

  /**
   * Displays the notifications exceptions dialog where specific site notification
   * preferences can be set.
   */
  showNotificationExceptions() {
    let params = { permissionType: "desktop-notification" };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },

  // MEDIA

  showAutoplayMediaExceptions() {
    var params = { permissionType: "autoplay-media" };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },

  // POP-UPS

  /**
   * Displays the popup exceptions dialog where specific site popup preferences
   * can be set.
   */
  showPopupExceptions() {
    var params = {
      blockVisible: false,
      sessionVisible: false,
      allowVisible: true,
      prefilledHost: "",
      permissionType: "popup",
    };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/permissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },

  // UTILITY FUNCTIONS

  /**
   * Utility function to enable/disable the button specified by aButtonID based
   * on the value of the Boolean preference specified by aPreferenceID.
   */
  updateButtons(aButtonID, aPreferenceID) {
    var button = document.getElementById(aButtonID);
    var preference = Preferences.get(aPreferenceID);
    button.disabled = !preference.value || preference.locked;
    return undefined;
  },

  // BEGIN UI CODE

  /*
   * Preferences:
   *
   * dom.disable_open_during_load
   * - true if popups are blocked by default, false otherwise
   */

  // POP-UPS

  /**
   * Displays a dialog in which the user can view and modify the list of sites
   * where passwords are never saved.
   */
  showPasswordExceptions() {
    var params = {
      blockVisible: true,
      sessionVisible: false,
      allowVisible: false,
      hideStatusColumn: true,
      prefilledHost: "",
      permissionType: "login-saving",
    };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/permissions.xhtml",
      undefined,
      params
    );
  },

  /**
   * Initializes master password UI: the "use master password" checkbox, selects
   * the master password button to show, and enables/disables it as necessary.
   * The master password is controlled by various bits of NSS functionality, so
   * the UI for it can't be controlled by the normal preference bindings.
   */
  _initMasterPasswordUI() {
    var noMP = !LoginHelper.isPrimaryPasswordSet();

    var button = document.getElementById("changeMasterPassword");
    button.disabled = noMP;

    var checkbox = document.getElementById("useMasterPassword");
    checkbox.checked = !noMP;
    checkbox.disabled =
      (noMP && !Services.policies.isAllowed("createMasterPassword")) ||
      (!noMP && !Services.policies.isAllowed("removeMasterPassword"));
  },

  /**
   * Enables/disables the master password button depending on the state of the
   * "use master password" checkbox, and prompts for master password removal if
   * one is set.
   */
  async updateMasterPasswordButton() {
    var checkbox = document.getElementById("useMasterPassword");
    var button = document.getElementById("changeMasterPassword");
    button.disabled = !checkbox.checked;

    // unchecking the checkbox should try to immediately remove the master
    // password, because it's impossible to non-destructively remove the master
    // password used to encrypt all the passwords without providing it (by
    // design), and it would be extremely odd to pop up that dialog when the
    // user closes the prefwindow and saves his settings
    if (!checkbox.checked) {
      await this._removeMasterPassword();
    } else {
      await this.changeMasterPassword();
    }

    this._initMasterPasswordUI();
  },

  /**
   * Displays the "remove master password" dialog to allow the user to remove
   * the current master password.  When the dialog is dismissed, master password
   * UI is automatically updated.
   */
  async _removeMasterPassword() {
    var secmodDB = Cc["@mozilla.org/security/pkcs11moduledb;1"].getService(
      Ci.nsIPKCS11ModuleDB
    );
    if (secmodDB.isFIPSEnabled) {
      let title = document.getElementById("fips-title").textContent;
      let desc = document.getElementById("fips-desc").textContent;
      Services.prompt.alert(window, title, desc);
      this._initMasterPasswordUI();
    } else {
      gSubDialog.open("chrome://mozapps/content/preferences/removemp.xhtml", {
        closingCallback: this._initMasterPasswordUI.bind(this),
      });
    }
  },

  /**
   * Displays a dialog in which the primary password may be changed.
   */
  async changeMasterPassword() {
    // Require OS authentication before the user can set a Primary Password.
    // OS reauthenticate functionality is not available on Linux yet (bug 1527745)
    if (
      !LoginHelper.isPrimaryPasswordSet() &&
      LoginHelper.getOSAuthEnabled(LoginHelper.OS_AUTH_FOR_PASSWORDS_PREF)
    ) {
      // Uses primary-password-os-auth-dialog-message-win and
      // primary-password-os-auth-dialog-message-macosx via concatenation:
      let messageId =
        "primary-password-os-auth-dialog-message-" + AppConstants.platform;
      let [messageText, captionText] = await document.l10n.formatMessages([
        {
          id: messageId,
        },
        {
          id: "master-password-os-auth-dialog-caption",
        },
      ]);
      let win = Services.wm.getMostRecentBrowserWindow();

      // Note on Glean collection: because OSKeyStore.ensureLoggedIn() is not wrapped in
      // verifyOSAuth(), it will be documenting "success" for unsupported platforms
      // and won't record "fail_error", only "fail_user_canceled"
      let loggedIn = await OSKeyStore.ensureLoggedIn(
        messageText.value,
        captionText.value,
        win,
        false
      );

      const result = loggedIn.authenticated ? "success" : "fail_user_canceled";
      Glean.pwmgr.promptShownOsReauth.record({
        trigger: "toggle_pref_primary_password",
        result,
      });

      if (!loggedIn.authenticated) {
        return;
      }
    }

    gSubDialog.open("chrome://mozapps/content/preferences/changemp.xhtml", {
      features: "resizable=no",
      closingCallback: this._initMasterPasswordUI.bind(this),
    });
  },

  /**
   * Set up the initial state for the GPC/DNT UI.
   * The GPC part should only appear if the functionality is
   * enabled.
   */
  _initGlobalPrivacyControlUI() {
    let gpcEnabledPrefValue = Services.prefs.getBoolPref(
      "privacy.globalprivacycontrol.functionality.enabled",
      false
    );
    let dntEnabledPrefValue = Services.prefs.getBoolPref(
      "privacy.donottrackheader.enabled",
      false
    );
    document.getElementById("doNotTrackBox").hidden = !dntEnabledPrefValue;
    // We can't rely on the hidden attribute for groupboxes because the pane
    // hiding/showing code can interfere (and fires after this).
    if (gpcEnabledPrefValue) {
      document
        .getElementById("nonTechnicalPrivacyGroup")
        .removeAttribute("style");
    } else {
      document
        .getElementById("nonTechnicalPrivacyGroup")
        .setAttribute("style", "display: none !important");
    }
  },

  /**
   * Set up the initial state for the password generation UI.
   * It will be hidden unless the .available pref is true
   */
  _initPasswordGenerationUI() {
    // we don't watch the .available pref for runtime changes
    let prefValue = Services.prefs.getBoolPref(
      PREF_PASSWORD_GENERATION_AVAILABLE,
      false
    );
    document.getElementById("generatePasswordsBox").hidden = !prefValue;
  },

  toggleRelayIntegration() {
    const checkbox = document.getElementById("relayIntegration");
    if (checkbox.checked) {
      FirefoxRelay.markAsAvailable();
      Glean.relayIntegration.enabledPrefChange.record();
    } else {
      FirefoxRelay.markAsDisabled();
      Glean.relayIntegration.disabledPrefChange.record();
    }
  },

  _updateRelayIntegrationUI() {
    document.getElementById("relayIntegrationBox").hidden =
      !FirefoxRelay.isAvailable;
    document.getElementById("relayIntegration").checked =
      FirefoxRelay.isAvailable && !FirefoxRelay.isDisabled;
  },

  _initRelayIntegrationUI() {
    document
      .getElementById("relayIntegrationLearnMoreLink")
      .setAttribute("href", FirefoxRelay.learnMoreUrl);

    setEventListener(
      "relayIntegration",
      "command",
      gPrivacyPane.toggleRelayIntegration.bind(gPrivacyPane)
    );
    Preferences.get("signon.firefoxRelay.feature").on(
      "change",
      gPrivacyPane._updateRelayIntegrationUI.bind(gPrivacyPane)
    );

    this._updateRelayIntegrationUI();
  },

  async _toggleOSAuth() {
    let osReauthCheckbox = document.getElementById("osReauthCheckbox");

    const messageText = await lazy.AboutLoginsL10n.formatValue(
      "about-logins-os-auth-dialog-message"
    );
    const captionText = await lazy.AboutLoginsL10n.formatValue(
      "about-logins-os-auth-dialog-caption"
    );
    let win =
      osReauthCheckbox.ownerGlobal.docShell.chromeEventHandler.ownerGlobal;

    // Calling OSKeyStore.ensureLoggedIn() instead of LoginHelper.verifyOSAuth()
    // since we want to authenticate user each time this setting is changed.

    // Note on Glean collection: because OSKeyStore.ensureLoggedIn() is not wrapped in
    // verifyOSAuth(), it will be documenting "success" for unsupported platforms
    // and won't record "fail_error", only "fail_user_canceled"
    let isAuthorized = (
      await OSKeyStore.ensureLoggedIn(messageText, captionText, win, false)
    ).authenticated;

    Glean.pwmgr.promptShownOsReauth.record({
      trigger: "toggle_pref_os_auth",
      result: isAuthorized ? "success" : "fail_user_canceled",
    });

    if (!isAuthorized) {
      osReauthCheckbox.checked = !osReauthCheckbox.checked;
      return;
    }

    // If osReauthCheckbox is checked enable osauth.
    LoginHelper.setOSAuthEnabled(
      LoginHelper.OS_AUTH_FOR_PASSWORDS_PREF,
      osReauthCheckbox.checked
    );

    Glean.pwmgr.requireOsReauthToggle.record({
      toggle_state: osReauthCheckbox.checked,
    });
  },

  _initOSAuthentication() {
    let osReauthCheckbox = document.getElementById("osReauthCheckbox");
    if (
      !OSKeyStore.canReauth() ||
      Services.prefs.getBoolPref("security.nocertdb", false)
    ) {
      osReauthCheckbox.hidden = true;
      return;
    }

    osReauthCheckbox.setAttribute(
      "checked",
      LoginHelper.getOSAuthEnabled(LoginHelper.OS_AUTH_FOR_PASSWORDS_PREF)
    );

    setEventListener(
      "osReauthCheckbox",
      "command",
      gPrivacyPane._toggleOSAuth.bind(gPrivacyPane)
    );
  },

  /**
   * Shows the sites where the user has saved passwords and the associated login
   * information.
   */
  showPasswords() {
    let loginManager = window.windowGlobalChild.getActor("LoginManager");
    loginManager.sendAsyncMessage("PasswordManager:OpenPreferences", {
      entryPoint: "Preferences",
    });
  },

  /**
   * Enables/disables dependent controls related to password saving
   * When password saving is not enabled, we need to also disable the password generation checkbox
   * The Exceptions button is used to configure sites where passwords are never saved.
   */
  readSavePasswords() {
    var prefValue = Preferences.get("signon.rememberSignons").value;
    document.getElementById("passwordExceptions").disabled = !prefValue;
    document.getElementById("generatePasswords").disabled = !prefValue;
    document.getElementById("passwordAutofillCheckbox").disabled = !prefValue;
    document.getElementById("relayIntegration").disabled =
      !prefValue || Services.prefs.prefIsLocked("signon.firefoxRelay.feature");
    // don't override pref value in UI
    return undefined;
  },

  /**
   * Initalizes pref listeners for the password manager.
   *
   * This ensures that the user is always notified if an extension is controlling the password manager.
   */
  initListenersForExtensionControllingPasswordManager() {
    this._passwordManagerCheckbox = document.getElementById("savePasswords");
    this._disableExtensionButton = document.getElementById(
      "disablePasswordManagerExtension"
    );

    this._disableExtensionButton.addEventListener(
      "command",
      makeDisableControllingExtension(
        PREF_SETTING_TYPE,
        PASSWORD_MANAGER_PREF_ID
      )
    );

    initListenersForPrefChange(
      PREF_SETTING_TYPE,
      PASSWORD_MANAGER_PREF_ID,
      this._passwordManagerCheckbox
    );
  },

  /**
   * Enables/disables the add-ons Exceptions button depending on whether
   * or not add-on installation warnings are displayed.
   */
  readWarnAddonInstall() {
    var warn = Preferences.get("xpinstall.whitelist.required");
    var exceptions = document.getElementById("addonExceptions");

    exceptions.disabled = !warn.value || warn.locked;

    // don't override the preference value
    return undefined;
  },

  _initSafeBrowsing() {
    let enableSafeBrowsing = document.getElementById("enableSafeBrowsing");
    let blockDownloads = document.getElementById("blockDownloads");
    let blockUncommonUnwanted = document.getElementById(
      "blockUncommonUnwanted"
    );

    let safeBrowsingPhishingPref = Preferences.get(
      "browser.safebrowsing.phishing.enabled"
    );
    let safeBrowsingMalwarePref = Preferences.get(
      "browser.safebrowsing.malware.enabled"
    );

    let blockDownloadsPref = Preferences.get(
      "browser.safebrowsing.downloads.enabled"
    );
    let malwareTable = Preferences.get("urlclassifier.malwareTable");

    let blockUnwantedPref = Preferences.get(
      "browser.safebrowsing.downloads.remote.block_potentially_unwanted"
    );
    let blockUncommonPref = Preferences.get(
      "browser.safebrowsing.downloads.remote.block_uncommon"
    );

    enableSafeBrowsing.addEventListener("command", function () {
      safeBrowsingPhishingPref.value = enableSafeBrowsing.checked;
      safeBrowsingMalwarePref.value = enableSafeBrowsing.checked;

      blockDownloads.disabled =
        !enableSafeBrowsing.checked || blockDownloadsPref.locked;
      blockUncommonUnwanted.disabled =
        !blockDownloads.checked ||
        !enableSafeBrowsing.checked ||
        blockUnwantedPref.locked ||
        blockUncommonPref.locked;
    });

    blockDownloads.addEventListener("command", function () {
      blockDownloadsPref.value = blockDownloads.checked;
      blockUncommonUnwanted.disabled =
        !blockDownloads.checked ||
        blockUnwantedPref.locked ||
        blockUncommonPref.locked;
    });

    blockUncommonUnwanted.addEventListener("command", function () {
      blockUnwantedPref.value = blockUncommonUnwanted.checked;
      blockUncommonPref.value = blockUncommonUnwanted.checked;

      let malware = malwareTable.value
        .split(",")
        .filter(
          x =>
            x !== "goog-unwanted-proto" &&
            x !== "goog-unwanted-shavar" &&
            x !== "moztest-unwanted-simple"
        );

      if (blockUncommonUnwanted.checked) {
        if (malware.includes("goog-malware-shavar")) {
          malware.push("goog-unwanted-shavar");
        } else {
          malware.push("goog-unwanted-proto");
        }

        malware.push("moztest-unwanted-simple");
      }

      // sort alphabetically to keep the pref consistent
      malware.sort();

      malwareTable.value = malware.join(",");

      // Force an update after changing the malware table.
      listManager.forceUpdates(malwareTable.value);
    });

    // set initial values

    enableSafeBrowsing.checked =
      safeBrowsingPhishingPref.value && safeBrowsingMalwarePref.value;
    if (!enableSafeBrowsing.checked) {
      blockDownloads.setAttribute("disabled", "true");
      blockUncommonUnwanted.setAttribute("disabled", "true");
    }

    blockDownloads.checked = blockDownloadsPref.value;
    if (!blockDownloadsPref.value) {
      blockUncommonUnwanted.setAttribute("disabled", "true");
    }
    blockUncommonUnwanted.checked =
      blockUnwantedPref.value && blockUncommonPref.value;

    if (safeBrowsingPhishingPref.locked || safeBrowsingMalwarePref.locked) {
      enableSafeBrowsing.disabled = true;
    }
    if (blockDownloadsPref.locked) {
      blockDownloads.disabled = true;
    }
    if (blockUnwantedPref.locked || blockUncommonPref.locked) {
      blockUncommonUnwanted.disabled = true;
    }
  },

  /**
   * Displays the exceptions lists for add-on installation warnings.
   */
  showAddonExceptions() {
    var params = this._addonParams;

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/permissions.xhtml",
      undefined,
      params
    );
  },

  /**
   * Parameters for the add-on install permissions dialog.
   */
  _addonParams: {
    blockVisible: false,
    sessionVisible: false,
    allowVisible: true,
    prefilledHost: "",
    permissionType: "install",
  },

  /**
   * readEnableOCSP is used by the preferences UI to determine whether or not
   * the checkbox for OCSP fetching should be checked (it returns true if it
   * should be checked and false otherwise). The about:config preference
   * "security.OCSP.enabled" is an integer rather than a boolean, so it can't be
   * directly mapped from {true,false} to {checked,unchecked}. The possible
   * values for "security.OCSP.enabled" are:
   * 0: fetching is disabled
   * 1: fetch for all certificates
   * 2: fetch only for EV certificates
   * Hence, if "security.OCSP.enabled" is non-zero, the checkbox should be
   * checked. Otherwise, it should be unchecked.
   */
  readEnableOCSP() {
    var preference = Preferences.get("security.OCSP.enabled");
    // This is the case if the preference is the default value.
    if (preference.value === undefined) {
      return true;
    }
    return preference.value != 0;
  },

  /**
   * writeEnableOCSP is used by the preferences UI to map the checked/unchecked
   * state of the OCSP fetching checkbox to the value that the preference
   * "security.OCSP.enabled" should be set to (it returns that value). See the
   * readEnableOCSP documentation for more background. We unfortunately don't
   * have enough information to map from {true,false} to all possible values for
   * "security.OCSP.enabled", but a reasonable alternative is to map from
   * {true,false} to {<the default value>,0}. That is, if the box is checked,
   * "security.OCSP.enabled" will be set to whatever default it should be, given
   * the platform and channel. If the box is unchecked, the preference will be
   * set to 0. Obviously this won't work if the default is 0, so we will have to
   * revisit this if we ever set it to 0.
   */
  writeEnableOCSP() {
    var checkbox = document.getElementById("enableOCSP");
    var defaults = Services.prefs.getDefaultBranch(null);
    var defaultValue = defaults.getIntPref("security.OCSP.enabled");
    return checkbox.checked ? defaultValue : 0;
  },

  /**
   * Displays the user's certificates and associated options.
   */
  showCertificates() {
    gSubDialog.open("chrome://pippki/content/certManager.xhtml");
  },

  /**
   * Displays a dialog from which the user can manage his security devices.
   */
  showSecurityDevices() {
    gSubDialog.open("chrome://pippki/content/device_manager.xhtml");
  },

  initDataCollection() {
    if (
      !AppConstants.MOZ_DATA_REPORTING &&
      !Services.prefs.getBoolPref(
        "browser.privacySegmentation.preferences.show"
      )
    ) {
      // Nothing to control in the data collection section, remove it.
      document.getElementById("dataCollectionCategory").remove();
      document.getElementById("dataCollectionGroup").remove();
      return;
    }

    this._setupLearnMoreLink(
      "toolkit.datacollection.infoURL",
      "dataCollectionPrivacyNotice"
    );
    this.initPrivacySegmentation();
  },

  initPrivacySegmentation() {
    // Section visibility
    let section = document.getElementById("privacySegmentationSection");
    let updatePrivacySegmentationSectionVisibilityState = () => {
      section.hidden = !Services.prefs.getBoolPref(
        "browser.privacySegmentation.preferences.show"
      );
    };

    Services.prefs.addObserver(
      "browser.privacySegmentation.preferences.show",
      updatePrivacySegmentationSectionVisibilityState
    );

    window.addEventListener("unload", () => {
      Services.prefs.removeObserver(
        "browser.privacySegmentation.preferences.show",
        updatePrivacySegmentationSectionVisibilityState
      );
    });

    updatePrivacySegmentationSectionVisibilityState();
  },

  /**
   * Set up or hide the Learn More links for various data collection options
   */
  _setupLearnMoreLink(pref, element) {
    // set up the Learn More link with the correct URL
    let url = Services.urlFormatter.formatURLPref(pref);
    let el = document.getElementById(element);

    if (url) {
      el.setAttribute("href", url);
    } else {
      el.hidden = true;
    }
  },

  /**
   * Update the health report service checkbox from preference.
   */
  updateSubmitHealthReportFromPref() {
    let checkbox = document.getElementById("submitHealthReportBox");
    let telemetryContainer = document.getElementById("telemetry-container");

    // Telemetry is only sending data if MOZ_TELEMETRY_REPORTING is defined.
    // We still want to display the preferences panel if that's not the case, but
    // we want it to be disabled and unchecked.
    if (
      Services.prefs.prefIsLocked(PREF_UPLOAD_ENABLED) ||
      !AppConstants.MOZ_TELEMETRY_REPORTING
    ) {
      checkbox.setAttribute("disabled", "true");
      return;
    }

    checkbox.checked =
      Services.prefs.getBoolPref(PREF_UPLOAD_ENABLED) &&
      AppConstants.MOZ_TELEMETRY_REPORTING;
    telemetryContainer.hidden = checkbox.checked;
  },

  /**
   * Update the health report preference with state from checkbox.
   */
  updateSubmitHealthReportToPref() {
    let checkbox = document.getElementById("submitHealthReportBox");
    let telemetryContainer = document.getElementById("telemetry-container");

    Services.prefs.setBoolPref(PREF_UPLOAD_ENABLED, checkbox.checked);
    telemetryContainer.hidden = checkbox.checked;
  },

  /**
   * Initialize the opt-out-study preference checkbox into about:preferences and
   * handles events coming from the UI for it.
   */
  initOptOutStudyCheckbox() {
    // The checkbox should be disabled if any of the below are true. This
    // prevents the user from changing the value in the box.
    //
    // * the policy forbids shield
    // * Normandy is disabled
    //
    // The checkbox should match the value of the preference only if all of
    // these are true. Otherwise, the checkbox should remain unchecked. This
    // is because in these situations, Shield studies are always disabled, and
    // so showing a checkbox would be confusing.
    //
    // * the policy allows Shield
    // * Normandy is enabled

    const allowedByPolicy = Services.policies.isAllowed("Shield");
    const checkbox = document.getElementById("optOutStudiesEnabled");

    function updateCheckbox() {
      if (
        allowedByPolicy &&
        Services.prefs.getBoolPref(PREF_UPLOAD_ENABLED, false) &&
        Services.prefs.getBoolPref(PREF_NORMANDY_ENABLED, false)
      ) {
        if (Services.prefs.getBoolPref(PREF_OPT_OUT_STUDIES_ENABLED, false)) {
          checkbox.setAttribute("checked", "true");
        } else {
          checkbox.removeAttribute("checked");
        }
        checkbox.setAttribute("preference", PREF_OPT_OUT_STUDIES_ENABLED);
        checkbox.removeAttribute("disabled");
      } else {
        checkbox.removeAttribute("preference");
        checkbox.removeAttribute("checked");
        checkbox.setAttribute("disabled", "true");
      }
    }
    Preferences.get(PREF_UPLOAD_ENABLED).on("change", updateCheckbox);
    updateCheckbox();
  },

  initAddonRecommendationsCheckbox() {
    // Setup the checkbox.
    dataCollectionCheckboxHandler({
      checkbox: document.getElementById("addonRecommendationEnabled"),
      pref: PREF_ADDON_RECOMMENDATIONS_ENABLED,
    });
  },

  initPrivateAttributionCheckbox() {
    dataCollectionCheckboxHandler({
      checkbox: document.getElementById("privateAttribution"),
      pref: PREF_PRIVATE_ATTRIBUTION_ENABLED,
      matchPref() {
        return AppConstants.MOZ_TELEMETRY_REPORTING;
      },
      isDisabled() {
        return !AppConstants.MOZ_TELEMETRY_REPORTING;
      },
    });
  },

  observe(aSubject, aTopic) {
    switch (aTopic) {
      case "sitedatamanager:updating-sites":
        // While updating, we want to disable this section and display loading message until updated
        this.toggleSiteData(false);
        this.showSiteDataLoading();
        break;

      case "sitedatamanager:sites-updated":
        this.toggleSiteData(true);
        SiteDataManager.getTotalUsage().then(
          this.updateTotalDataSizeLabel.bind(this)
        );
        break;
      case "network:trr-uri-changed":
      case "network:trr-mode-changed":
      case "network:trr-confirmation":
        gPrivacyPane.updateDoHStatus();
        break;
    }
  },

  _initProfilesInfo() {
    setEventListener(
      "dataCollectionViewProfiles",
      "click",
      gMainPane.manageProfiles
    );

    let listener = () => gPrivacyPane.updateProfilesPrivacyInfo();
    SelectableProfileService.on("enableChanged", listener);
    window.addEventListener("unload", () =>
      SelectableProfileService.off("enableChanged", listener)
    );
    this.updateProfilesPrivacyInfo();
  },

  updateProfilesPrivacyInfo() {
    let profilesInfo = document.getElementById("preferences-privacy-profiles");
    profilesInfo.hidden = !SelectableProfileService.isEnabled;
  },
};
