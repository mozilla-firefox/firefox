/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import MozCheckbox from "chrome://global/content/elements/moz-checkbox.mjs";*/
/** @import MozMessageBar from "chrome://global/content/elements/moz-message-bar.mjs";*/
/** @import {SettingValue, SettingDeps, SettingEmitChange} from "chrome://global/content/preferences/Setting.mjs";*/

/* import-globals-from extensionControlled.js */
/* import-globals-from preferences.js */

// import the new Permissions & Data settings pane code.
const { PRIVACY_SEGMENTATION_PREF } = ChromeUtils.importESModule(
  "chrome://browser/content/preferences/config/permissions-data.mjs",
  { global: "current" }
);
const { PasswordSettingHelpers } = ChromeUtils.importESModule(
  "chrome://browser/content/preferences/config/passwords-autofill.mjs",
  { global: "current" }
);
const { PrivacySettingHelpers } = ChromeUtils.importESModule(
  "chrome://browser/content/preferences/config/privacy.mjs",
  { global: "current" }
);

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
  "privacy.trackingprotection.allow_list.baseline.enabled",
  "privacy.trackingprotection.allow_list.convenience.enabled",
];

const PREF_PASSWORD_GENERATION_AVAILABLE = "signon.generation.available";
const { BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN } = Ci.nsICookieService;

const PASSWORD_MANAGER_PREF_ID = "services.passwordSavingEnabled";

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

ChromeUtils.defineESModuleGetters(this, {
  AppUpdater: "resource://gre/modules/AppUpdater.sys.mjs",
  DoHConfigController: "moz-src:///toolkit/components/doh/DoHConfig.sys.mjs",
  PreferencesBackupResource:
    "resource:///modules/backup/PreferencesBackupResource.sys.mjs",
  Sanitizer: "resource:///modules/Sanitizer.sys.mjs",
  SelectableProfileService:
    "resource:///modules/profiles/SelectableProfileService.sys.mjs",
  IPProtection:
    "moz-src:///browser/components/ipprotection/IPProtection.sys.mjs",
  BANDWIDTH: "chrome://browser/content/ipprotection/ipprotection-constants.mjs",
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

    if (PrivacySettingHelpers.shouldDisableETPCategoryControls()) {
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

  /**
   * Ensure the tracking protection exception list is migrated before the privacy
   * preferences UI is shown.
   * If the migration has already been run, this is a no-op.
   */
  _ensureTrackingProtectionExceptionListMigration() {
    // Let's check the migration pref here as well to avoid the extra xpcom call
    // for the common case where we've already migrated.
    if (
      Services.prefs.getBoolPref(
        "privacy.trackingprotection.allow_list.hasMigratedCategoryPrefs",
        false
      )
    ) {
      return;
    }

    let exceptionListService = Cc[
      "@mozilla.org/url-classifier/exception-list-service;1"
    ].getService(Ci.nsIUrlClassifierExceptionListService);

    exceptionListService.maybeMigrateCategoryPrefs();
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
    initSettingGroup("nonTechnicalPrivacy");
    initSettingGroup("nonTechnicalPrivacy2");
    initSettingGroup("securityPrivacyStatus");
    initSettingGroup("securityPrivacyWarnings");
    initSettingGroup("httpsOnly");
    initSettingGroup("browsingProtection");
    initSettingGroup("cookiesAndSiteData");
    initSettingGroup("cookiesAndSiteData2");
    initSettingGroup("certificates");
    initSettingGroup("ipprotection");
    initSettingGroup("history");
    initSettingGroup("history2");
    initSettingGroup("permissions");
    initSettingGroup("dataCollection");
    initSettingGroup("privacyPanel");
    initSettingGroup("dnsOverHttps");
    initSettingGroup("dnsOverHttpsAdvanced");
    initSettingGroup("etpStatus");
    initSettingGroup("etpBanner");
    initSettingGroup("etpAdvanced");
    initSettingGroup("etpReset");
    initSettingGroup("etpCustomize");
    initSettingGroup("networkProxy");

    /* Initialize Content Blocking */
    this.initContentBlocking();

    this.trackingProtectionReadPrefs();
    this.fingerprintingProtectionReadPrefs();
    this.networkCookieBehaviorReadPrefs();
    this._initTrackingProtectionExtensionControl();
    this._ensureTrackingProtectionExceptionListMigration();

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

    setEventListener(
      "dohExceptionsButton",
      "command",
      gPrivacyPane.showDoHExceptions
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

    this._pane = document.getElementById("panePrivacy");

    this._initPasswordGenerationUI();
    this._initRelayIntegrationUI();
    this._initMasterPasswordUI();
    this._initOSAuthentication();

    // Init passwords settings group
    initSettingGroup("passwords");

    this.initListenersForExtensionControllingPasswordManager();

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

    this.initSiteDataControls();

    this.initPrivacySegmentation();

    let signonBundle = document.getElementById("signonBundle");
    appendSearchKeywords("showPasswords", [
      signonBundle.getString("loginsDescriptionAll2"),
    ]);

    setEventListener(
      "contentBlockingBaselineExceptionsStrict",
      "change",
      gPrivacyPane.onBaselineCheckboxChange
    );

    setEventListener(
      "contentBlockingBaselineExceptionsCustom",
      "change",
      gPrivacyPane.onBaselineCheckboxChange
    );

    setEventListener(
      "contentBlockingConvenienceExceptionsStrict",
      "change",
      gPrivacyPane.maybeNotifyUserToReload
    );

    setEventListener(
      "contentBlockingConvenienceExceptionsCustom",
      "change",
      gPrivacyPane.maybeNotifyUserToReload
    );

    this.initDoH();

    this.initWebAuthn();

    // Notify observers that the UI is now ready
    Services.obs.notifyObservers(window, "privacy-pane-loaded");
  },

  initSiteDataControls() {
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
      // Skip registering change listeners for baseline and convenience allow list prefs.
      // Their UI is handled in gPrivacyPane.onBaselineCheckboxChange to prevent redundant reload
      // warnings when user toggles the checkboxes.
      if (
        pref == "privacy.trackingprotection.allow_list.baseline.enabled" ||
        pref == "privacy.trackingprotection.allow_list.convenience.enabled"
      ) {
        continue;
      }
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
            ? "cryptoTP"
            : "-cryptoTP"
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
          case "cryptoTP":
            document.querySelector(selector + " .cryptominers-option").hidden =
              false;
            break;
          case "-cryptoTP":
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
    fppMenu.disabled = !fppCheckbox.checked || enabledPref.locked;
    fppCheckbox.disabled = enabledPref.locked;
  },

  /**
   * Selects the right items of the new Cookies & Site Data UI.
   */
  networkCookieBehaviorReadPrefs() {
    let behavior = Services.cookies.getCookieBehavior(false);
    let blockCookiesMenu = document.getElementById("blockCookiesMenu");
    let blockCookies = behavior != Ci.nsICookieService.BEHAVIOR_ACCEPT;
    let cookieBehaviorLocked = Services.prefs.prefIsLocked(
      "network.cookie.cookieBehavior"
    );
    let blockCookiesControlsDisabled = !blockCookies || cookieBehaviorLocked;
    blockCookiesMenu.disabled = blockCookiesControlsDisabled;

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
    let dialogFile = "chrome://browser/content/sanitize_v2.xhtml";

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
    PrivacySettingHelpers.clearPrivateDataNow(aClearEverything);
  },

  _isCustomCleaningPrefPresent() {
    return PrivacySettingHelpers._isCustomCleaningPrefPresent();
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
    PrivacySettingHelpers.reloadAllOtherTabs();
  },

  /**
   * If there are more tabs than just the preferences tab, show a warning to the user that
   * they need to reload their tabs to apply the setting.
   */
  maybeNotifyUserToReload() {
    PrivacySettingHelpers.maybeNotifyUserToReload();
  },

  /**
   * Displays per-site preferences for HTTPS-Only Mode exceptions.
   */
  showHttpsOnlyModeExceptions() {
    PrivacySettingHelpers.showHttpsOnlyModeExceptions();
  },

  showDoHExceptions() {
    PrivacySettingHelpers.showDoHExceptions();
  },

  // GEOLOCATION

  /**
   * Displays the location exceptions dialog where specific site location
   * preferences can be set.
   */
  showLocationExceptions() {
    PrivacySettingHelpers.showLocationExceptions();
  },

  // LOOPBACK-NETWORK

  /**
   * Displays the loopback network exceptions dialog where specific site loopback network
   * preferences can be set.
   */
  showLoopbackNetworkExceptions() {
    PrivacySettingHelpers.showLoopbackNetworkExceptions();
  },

  // LOCAL-NETWORK

  /**
   * Displays the local network exceptions dialog where specific site local network
   * preferences can be set.
   */
  showLocalNetworkExceptions() {
    PrivacySettingHelpers.showLocalNetworkExceptions();
  },

  // XR

  /**
   * Displays the XR exceptions dialog where specific site XR
   * preferences can be set.
   */
  showXRExceptions() {
    PrivacySettingHelpers.showXRExceptions();
  },

  // CAMERA

  /**
   * Displays the camera exceptions dialog where specific site camera
   * preferences can be set.
   */
  showCameraExceptions() {
    PrivacySettingHelpers.showCameraExceptions();
  },

  // MICROPHONE

  /**
   * Displays the microphone exceptions dialog where specific site microphone
   * preferences can be set.
   */
  showMicrophoneExceptions() {
    PrivacySettingHelpers.showMicrophoneExceptions();
  },

  // SPEAKER

  /**
   * Displays the speaker exceptions dialog where specific site speaker
   * preferences can be set.
   */
  showSpeakerExceptions() {
    PrivacySettingHelpers.showSpeakerExceptions();
  },

  // NOTIFICATIONS

  /**
   * Displays the notifications exceptions dialog where specific site notification
   * preferences can be set.
   */
  showNotificationExceptions() {
    PrivacySettingHelpers.showNotificationExceptions();
  },

  // MEDIA

  showAutoplayMediaExceptions() {
    PrivacySettingHelpers.showAutoplayMediaExceptions();
  },

  // POP-UPS

  /**
   * Displays the popup exceptions dialog where specific site popup preferences
   * can be set.
   */
  showPopupExceptions() {
    PrivacySettingHelpers.showPopupExceptions();
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
    PasswordSettingHelpers.showPasswordExceptions();
  },

  /**
   * Initializes master password UI: the "use master password" checkbox, selects
   * the master password button to show, and enables/disables it as necessary.
   * The master password is controlled by various bits of NSS functionality, so
   * the UI for it can't be controlled by the normal preference bindings.
   */
  _initMasterPasswordUI() {
    PasswordSettingHelpers._initMasterPasswordUI();
  },

  /**
   * Enables/disables the master password button depending on the state of the
   * "use master password" checkbox, and prompts for master password removal if
   * one is set.
   */
  async updateMasterPasswordButton() {
    let checkbox = document.getElementById("useMasterPassword");
    let button = document.getElementById("changeMasterPassword");
    button.disabled = !checkbox.checked;
    if (!checkbox.checked) {
      await PasswordSettingHelpers._removeMasterPassword();
    } else {
      await PasswordSettingHelpers.changeMasterPassword();
    }
    this._initMasterPasswordUI();
  },

  async _removeMasterPassword() {
    await PasswordSettingHelpers._removeMasterPassword();
  },

  async changeMasterPassword() {
    await PasswordSettingHelpers.changeMasterPassword();
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
      osReauthCheckbox.documentGlobal.docShell.chromeEventHandler
        .documentGlobal;

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
    LoginHelper.setOSAuthEnabled(osReauthCheckbox.checked);

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

    osReauthCheckbox.toggleAttribute("checked", LoginHelper.getOSAuthEnabled());

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
    PasswordSettingHelpers.showPasswords();
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
   * Displays the exceptions lists for add-on installation warnings.
   */
  showAddonExceptions() {
    PrivacySettingHelpers.showAddonExceptions();
  },

  /**
   * Displays the user's certificates and associated options.
   */
  showCertificates() {
    PrivacySettingHelpers.showCertificates();
  },

  /**
   * Displays a dialog from which the user can manage his security devices.
   */
  showSecurityDevices() {
    PrivacySettingHelpers.showSecurityDevices();
  },

  initPrivacySegmentation() {
    if (
      !AppConstants.MOZ_DATA_REPORTING &&
      !Services.prefs.getBoolPref(PRIVACY_SEGMENTATION_PREF)
    ) {
      return;
    }

    // Section visibility
    let section = document.getElementById("privacySegmentationSection");
    let updatePrivacySegmentationSectionVisibilityState = () => {
      section.hidden = !Services.prefs.getBoolPref(PRIVACY_SEGMENTATION_PREF);
    };

    Services.prefs.addObserver(
      PRIVACY_SEGMENTATION_PREF,
      updatePrivacySegmentationSectionVisibilityState
    );

    window.addEventListener("unload", () => {
      Services.prefs.removeObserver(
        PRIVACY_SEGMENTATION_PREF,
        updatePrivacySegmentationSectionVisibilityState
      );
    });

    updatePrivacySegmentationSectionVisibilityState();
  },

  observe(aSubject, aTopic) {
    switch (aTopic) {
      case "network:trr-uri-changed":
      case "network:trr-mode-changed":
      case "network:trr-confirmation":
        gPrivacyPane.updateDoHStatus();
        break;
    }
  },

  /**
   * Handles change events on baseline and convenience exception checkboxes for content blocking preferences.
   *
   * - For baseline checkboxes: If the user attempts to uncheck, shows a confirmation dialog.
   *   If confirmed, disables the baseline allow list preference.
   * - For other cases: Toggles the checkbox and updates the corresponding preference.
   *
   * @param {Event} event - The change event triggered by the checkbox.
   */
  async onBaselineCheckboxChange(event) {
    await PrivacySettingHelpers.onBaselineCheckboxChange(event);
  },

  async onBaselineAllowListSettingChange(value, setting) {
    await PrivacySettingHelpers.onBaselineAllowListSettingChange(
      value,
      setting
    );
  },

  async _confirmBaselineAllowListDisable() {
    return PrivacySettingHelpers._confirmBaselineAllowListDisable();
  },
};
