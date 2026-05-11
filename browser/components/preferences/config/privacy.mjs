/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global gSubDialog, gotoPref, confirmRestartPrompt, CONFIRM_RESTART_PROMPT_RESTART_NOW, srdSectionEnabled */

import { SettingGroupManager } from "chrome://browser/content/preferences/config/SettingGroupManager.mjs";
import { Preferences } from "chrome://global/content/preferences/Preferences.mjs";

const XPCOMUtils = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
).XPCOMUtils;

const PrivateBrowsingUtils = ChromeUtils.importESModule(
  "resource://gre/modules/PrivateBrowsingUtils.sys.mjs"
).PrivateBrowsingUtils;

const lazy = XPCOMUtils.declareLazy({
  AppConstants: "resource://gre/modules/AppConstants.sys.mjs",
  AppUpdater: "resource://gre/modules/AppUpdater.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  DoHConfigController: "moz-src:///toolkit/components/doh/DoHConfig.sys.mjs",
  DownloadUtils: "resource://gre/modules/DownloadUtils.sys.mjs",
  FirefoxRelay: "resource://gre/modules/FirefoxRelay.sys.mjs",
  Sanitizer: "resource:///modules/Sanitizer.sys.mjs",
  SiteDataManager: "resource:///modules/SiteDataManager.sys.mjs",
  IPProtection:
    "moz-src:///browser/components/ipprotection/IPProtection.sys.mjs",
  BANDWIDTH: "chrome://browser/content/ipprotection/ipprotection-constants.mjs",
  TrackingDBService: {
    service: "@mozilla.org/tracking-db-service;1",
    iid: Ci.nsITrackingDBService,
  },
  listManager: {
    service: "@mozilla.org/url-classifier/listmanager;1",
    iid: Ci.nsIUrlListManager,
  },
  gParentalControlsService: () =>
    "@mozilla.org/parental-controls-service;1" in Cc
      ? Cc["@mozilla.org/parental-controls-service;1"].getService(
          Ci.nsIParentalControlsService
        )
      : null,
  isPackagedApp: () => Services.sysinfo.getProperty("isPackagedApp"),
});

const SANITIZE_ON_SHUTDOWN_PREFS_ONLY_V2 = [
  "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads",
  "privacy.clearOnShutdown_v2.siteSettings",
];

export class PrivacySettingHelpers {
  /**
   * Displays per-site preferences for HTTPS-Only Mode exceptions.
   */
  static showHttpsOnlyModeExceptions() {
    let params = {
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
  }

  /*
   * Checks if the user set cleaning prefs that do not belong to DeleteOnClose.
   */
  static _isCustomCleaningPrefPresent() {
    return SANITIZE_ON_SHUTDOWN_PREFS_ONLY_V2.some(
      pref => Preferences.get(pref).value
    );
  }

  /*
   * Unsets cleaning prefs that do not belong to DeleteOnClose
   */
  static resetCleaningPrefs() {
    return SANITIZE_ON_SHUTDOWN_PREFS_ONLY_V2.forEach(
      pref => (Preferences.get(pref).value = false)
    );
  }

  /**
   * Displays a dialog from which individual parts of private data may be
   * cleared.
   */
  static clearPrivateDataNow(aClearEverything) {
    let ts = Preferences.get("privacy.sanitize.timeSpan");
    let timeSpanOrig = ts.value;
    if (aClearEverything) {
      ts.value = 0;
    }
    let dialogFile = "chrome://browser/content/sanitize_v2.xhtml";
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
  }

  /**
   * Displays the user's certificates and associated options.
   */
  static showCertificates() {
    gSubDialog.open("chrome://pippki/content/certManager.xhtml");
  }

  /**
   * Displays a dialog from which the user can manage his security devices.
   */
  static showSecurityDevices() {
    gSubDialog.open("chrome://pippki/content/device_manager.xhtml");
  }

  static showDoHExceptions() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/dohExceptions.xhtml",
      undefined
    );
  }

  /**
   * Discard the browsers of all tabs in all windows. Pinned tabs, as
   * well as tabs for which discarding doesn't succeed (e.g. selected
   * tabs, tabs with beforeunload listeners), are reloaded.
   */
  static reloadAllOtherTabs() {
    let ourTab = window.browsingContext.topChromeWindow.gBrowser.selectedTab;
    lazy.BrowserWindowTracker.orderedWindows.forEach(win => {
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
    Preferences.getSetting("reloadTabsHint").value = false;
  }

  /**
   * If there are more tabs than just the preferences tab, show a warning to the user that
   * they need to reload their tabs to apply the setting.
   */
  static maybeNotifyUserToReload() {
    let shouldShow = false;
    if (lazy.BrowserWindowTracker.orderedWindows.length > 1) {
      shouldShow = true;
    } else {
      let tabbrowser = window.browsingContext.topChromeWindow.gBrowser;
      if (tabbrowser.tabs.length > 1) {
        shouldShow = true;
      }
    }
    if (shouldShow) {
      for (let notification of document.querySelectorAll(".reload-tabs")) {
        notification.hidden = false;
      }
    }
    Preferences.getSetting("reloadTabsHint").value = true;
  }

  static async onBaselineAllowListSettingChange(value, setting) {
    if (value) {
      PrivacySettingHelpers.maybeNotifyUserToReload();
      return;
    }
    const confirmed =
      await PrivacySettingHelpers._confirmBaselineAllowListDisable();
    if (confirmed) {
      PrivacySettingHelpers.maybeNotifyUserToReload();
      return;
    }
    setting.value = true;
  }

  /**
   * Handles change events on baseline and convenience exception checkboxes for content blocking preferences.
   *
   * - For baseline checkboxes: If the user attempts to uncheck, shows a confirmation dialog.
   *   If confirmed, disables the baseline allow list preference.
   * - For other cases: Toggles the checkbox and updates the corresponding preference.
   *
   * @param {Event} event - The change event triggered by the checkbox.
   */
  static async onBaselineCheckboxChange(event) {
    // Ignore events from nested checkboxes
    if (event.target.slot === "nested") {
      return;
    }
    // If the user is checking the checkbox, don't show a confirmation prompt.
    if (event.target.checked) {
      PrivacySettingHelpers.maybeNotifyUserToReload();
      return;
    }
    const confirmed =
      await PrivacySettingHelpers._confirmBaselineAllowListDisable();
    if (confirmed) {
      // User confirmed, set the checkbox to false.
      event.target.checked = false;
      PrivacySettingHelpers.maybeNotifyUserToReload();
    } else {
      // User cancelled, set the checkbox and the baseline pref to true.
      event.target.checked = true;
      Services.prefs.setBoolPref(
        "privacy.trackingprotection.allow_list.baseline.enabled",
        true
      );
    }
  }

  static async _confirmBaselineAllowListDisable() {
    let [title, body, okButtonText, cancelButtonText] =
      await document.l10n.formatValues([
        { id: "content-blocking-baseline-uncheck-warning-dialog-title" },
        { id: "content-blocking-baseline-uncheck-warning-dialog-body" },
        { id: "content-blocking-baseline-uncheck-warning-dialog-ok-button" },
        {
          id: "content-blocking-baseline-uncheck-warning-dialog-cancel-button",
        },
      ]);
    let flags =
      Services.prompt.BUTTON_TITLE_IS_STRING * Services.prompt.BUTTON_POS_1 +
      Services.prompt.BUTTON_TITLE_IS_STRING * Services.prompt.BUTTON_POS_0 +
      Services.prompt.BUTTON_POS_0_DEFAULT;
    const result = await Services.prompt.asyncConfirmEx(
      window.browsingContext,
      Services.prompt.MODAL_TYPE_CONTENT,
      title,
      body,
      flags,
      cancelButtonText,
      okButtonText,
      null,
      null,
      false,
      { useTitle: true }
    );
    const propertyBag = result.QueryInterface(Ci.nsIPropertyBag2);
    return propertyBag.get("buttonNumClicked") == 1;
  }

  static shouldDisableETPCategoryControls() {
    let policy = Services.policies.getActivePolicies();
    return policy?.EnableTrackingProtection?.Locked || policy?.Cookies?.Locked;
  }
}

const SECURITY_PRIVACY_STATUS_CARD_ENABLED =
  Services.prefs.getBoolPref("browser.settings-redesign.enabled", false) ||
  Services.prefs.getBoolPref(
    "browser.settings-redesign.securityPrivacyStatus.enabled",
    false
  );

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
  {
    id: "privacy.trackingprotection.allow_list.baseline.enabled",
    type: "bool",
  },
  {
    id: "privacy.trackingprotection.allow_list.convenience.enabled",
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

  // Trust Panel
  { id: "browser.urlbar.trustPanel.breachAlerts", type: "bool" },
  { id: "browser.urlbar.trustPanel.featureGate", type: "bool" },

  // Button prefs
  { id: "pref.privacy.disable_button.cookie_exceptions", type: "bool" },
  {
    id: "pref.privacy.disable_button.tracking_protection_exceptions",
    type: "bool",
  },

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

  // Do not track and Global Privacy Control
  { id: "privacy.donottrackheader.enabled", type: "bool" },
  { id: "privacy.globalprivacycontrol.functionality.enabled", type: "bool" },
  { id: "privacy.globalprivacycontrol.enabled", type: "bool" },
  {
    id: "browser.preferences.config_warning.donottrackheader.dismissed",
    type: "bool",
  },

  // Firefox VPN
  { id: "browser.ipProtection.enabled", type: "bool" },
  { id: "browser.ipProtection.entitlementCache", type: "string" },
  { id: "browser.ipProtection.features.siteExceptions", type: "bool" },
  { id: "browser.ipProtection.features.autoStart", type: "bool" },
  { id: "browser.ipProtection.autoStartEnabled", type: "bool" },
  { id: "browser.ipProtection.autoStartPrivateEnabled", type: "bool" },
  { id: "browser.ipProtection.bandwidth.enabled", type: "bool" },
  { id: "browser.ipProtection.usageCache", type: "string" },

  // Media
  { id: "media.autoplay.default", type: "int" },

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

  { id: "security.enterprise_roots.enabled", type: "bool" },

  // Add-ons, malware, phishing
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

  // Cookie Banner Handling
  { id: "cookiebanners.ui.desktop.enabled", type: "bool" },
  { id: "cookiebanners.service.mode.privateBrowsing", type: "int" },

  // DoH
  { id: "network.trr.mode", type: "int" },
  { id: "network.trr.uri", type: "string" },
  { id: "network.trr.default_provider_uri", type: "string" },
  { id: "network.trr.custom_uri", type: "string" },
  { id: "network.trr_ui.fallback_was_checked", type: "bool" },
  { id: "doh-rollout.disable-heuristics", type: "bool" },

  // Security and Privacy Warnings
  { id: "browser.preferences.config_warning.dismissAll", type: "bool" },
  {
    id: "browser.preferences.config_warning.warningSafeBrowsing.dismissed",
    type: "bool",
  },

  // Privacy segmentation section
  { id: "browser.dataFeatureRecommendations.enabled", type: "bool" },
]);

if (SECURITY_PRIVACY_STATUS_CARD_ENABLED) {
  Preferences.addAll([
    // Security and Privacy Warnings
    { id: "privacy.ui.status_card.testing.show_issue", type: "bool" },
    {
      id: "browser.preferences.config_warning.warningTest.dismissed",
      type: "bool",
    },
    {
      id: "browser.preferences.config_warning.warningAllowFingerprinters.dismissed",
      type: "bool",
    },
    {
      id: "browser.preferences.config_warning.warningThirdPartyCookies.dismissed",
      type: "bool",
    },
    {
      id: "browser.preferences.config_warning.warningPasswordManager.dismissed",
      type: "bool",
    },
    {
      id: "browser.preferences.config_warning.warningPopupBlocker.dismissed",
      type: "bool",
    },
    {
      id: "browser.preferences.config_warning.warningExtensionInstall.dismissed",
      type: "bool",
    },
    {
      id: "browser.preferences.config_warning.warningDoH.dismissed",
      type: "bool",
    },
    {
      id: "browser.preferences.config_warning.warningECH.dismissed",
      type: "bool",
    },
    {
      id: "browser.preferences.config_warning.warningProxyAutodetection.dismissed",
      type: "bool",
    },
    {
      id: "services.passwordSavingEnabled",
      type: "bool",
    },
    {
      id: "network.dns.echconfig.enabled",
      type: "bool",
    },
    {
      id: "network.dns.http3_echconfig.enabled",
      type: "bool",
    },
    {
      id: "network.proxy.type",
      type: "int",
    },
  ]);
}

SettingGroupManager.registerGroups({
  httpsOnly: {
    l10nId: "httpsonly-group",
    supportPage: "https-only-prefs",
    headingLevel: 2,
    items: [
      {
        id: "httpsOnlyRadioGroup",
        control: "moz-radio-group",
        l10nId: "httpsonly-label2",
        options: [
          {
            id: "httpsOnlyRadioEnabled",
            value: "enabled",
            l10nId: "httpsonly-radio-enabled",
          },
          {
            id: "httpsOnlyRadioEnabledPBM",
            value: "privateOnly",
            l10nId: "httpsonly-radio-enabled-pbm",
          },
          {
            id: "httpsOnlyRadioDisabled",
            value: "disabled",
            l10nId: "httpsonly-radio-disabled3",
            supportPage: "connection-upgrades",
          },
        ],
      },
      {
        id: "httpsOnlyExceptionButton",
        l10nId: "sitedata-cookies-exceptions",
        control: "moz-box-button",
        controlAttrs: {
          "search-l10n-ids":
            "permissions-address,permissions-allow.label,permissions-remove.label,permissions-remove-all.label,permissions-exceptions-https-only-desc2",
        },
      },
    ],
  },
  certificates: {
    l10nId: "certs-description3",
    supportPage: "secure-website-certificate",
    headingLevel: 2,
    items: [
      {
        id: "certEnableThirdPartyToggle",
        l10nId: "certs-thirdparty-toggle",
        supportPage: "automatically-trust-third-party-certificates",
      },
      {
        id: "certificateButtonGroup",
        control: "moz-box-group",
        items: [
          {
            id: "viewCertificatesButton",
            l10nId: "certs-view2",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids":
                "certmgr-tab-mine.label,certmgr-tab-people.label,certmgr-tab-servers.label,certmgr-tab-ca.label,certmgr-mine,certmgr-people,certmgr-server,certmgr-ca,certmgr-cert-name.label,certmgr-token-name.label,certmgr-view.label,certmgr-export.label,certmgr-delete.label",
            },
          },
          {
            id: "viewSecurityDevicesButton",
            l10nId: "certs-devices2",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids":
                "devmgr-window.title,devmgr-devlist.label,devmgr-header-details.label,devmgr-header-value.label,devmgr-button-login.label,devmgr-button-logout.label,devmgr-button-changepw.label,devmgr-button-load.label,devmgr-button-unload.label,certs-devices-enable-fips",
            },
          },
        ],
      },
    ],
  },
  browsingProtection: {
    l10nId: "browsing-protection-group2",
    headingLevel: 2,
    items: [
      {
        id: "enableSafeBrowsing",
        l10nId: "security-enable-safe-browsing",
        supportPage: "phishing-malware",
        control: "moz-checkbox",
        items: [
          {
            id: "blockDownloads",
            l10nId: "security-block-downloads",
          },
          {
            id: "blockUncommonUnwanted",
            l10nId: "security-block-uncommon-software",
          },
        ],
      },
      {
        id: "safeBrowsingWarningMessageBox",
        l10nId: "security-safe-browsing-warning",
        control: "moz-message-bar",
        controlAttrs: {
          type: "warning",
          dismissable: true,
        },
      },
    ],
  },
  nonTechnicalPrivacy: {
    l10nId: "non-technical-privacy-group",
    headingLevel: 2,
    items: [
      {
        id: "gpcEnabled",
        l10nId: "global-privacy-control-description",
        supportPage: "global-privacy-control",
        controlAttrs: {
          "search-l10n-ids": "global-privacy-control-search",
        },
      },
      {
        id: "dntRemoval",
        l10nId: "do-not-track-removal2",
        control: "moz-box-link",
        supportPage: "how-do-i-turn-do-not-track-feature",
      },
    ],
  },
  nonTechnicalPrivacy2: {
    inProgress: true,
    l10nId: "non-technical-privacy-heading",
    iconSrc: "chrome://browser/skin/controlcenter/tracking-protection.svg",
    headingLevel: 2,
    items: [
      {
        id: "gpcEnabled",
        l10nId: "global-privacy-control-description",
        supportPage: "global-privacy-control",
        controlAttrs: {
          "search-l10n-ids": "global-privacy-control-search",
        },
      },
      {
        id: "relayIntegration",
        l10nId: "preferences-privacy-relay-available",
        supportPage: "firefox-relay-integration",
      },
      {
        id: "dntRemoval",
        l10nId: "do-not-track-removal3",
        control: "moz-message-bar",
        supportPage: "how-do-i-turn-do-not-track-feature",
        controlAttrs: {
          dismissable: true,
        },
      },
    ],
  },
  securityPrivacyStatus: {
    inProgress: true,
    card: "never",
    items: [
      {
        id: "privacyCard",
        control: "security-privacy-card",
      },
    ],
  },
  securityPrivacyWarnings: {
    inProgress: true,
    card: "never",
    items: [
      {
        id: "warningCard",
        l10nId: "security-privacy-issue-card",
        control: "moz-card",
        controlAttrs: {
          type: "accordion",
        },
        items: [
          {
            id: "securityWarningsGroup",
            control: "moz-box-group",
            controlAttrs: {
              type: "list",
            },
          },
        ],
      },
    ],
  },
  cookiesAndSiteData: {
    l10nId: "cookies-site-data-group",
    headingLevel: 2,
    subcategory: "sitedata",
    items: [
      {
        id: "clearSiteDataButton",
        l10nId: "sitedata-clear2",
        control: "moz-box-button",
        iconSrc: "chrome://browser/skin/flame.svg",
        controlAttrs: {
          "search-l10n-ids": `
            clear-site-data-cookies-empty.label,
            clear-site-data-cache-empty.label
          `,
        },
      },
      {
        id: "deleteOnCloseInfo",
        l10nId: "sitedata-delete-on-close-private-browsing3",
        control: "moz-message-bar",
      },
      {
        id: "manageDataSettingsGroup",
        control: "moz-box-group",
        controlAttrs: {
          type: "default",
        },
        items: [
          {
            id: "siteDataSize",
            l10nId: "sitedata-total-size-calculating",
            control: "moz-box-item",
            supportPage: "sitedata-learn-more",
          },
          {
            id: "siteDataSettings",
            l10nId: "sitedata-settings2",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids": `
                site-data-settings-window.title,
                site-data-column-host.label,
                site-data-column-cookies.label,
                site-data-column-storage.label,
                site-data-settings-description,
                site-data-remove-all.label,
              `,
            },
          },
          {
            id: "cookieExceptions",
            l10nId: "sitedata-cookies-exceptions2",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids": `
                permissions-address,
                permissions-block.label,
                permissions-allow.label,
                permissions-remove.label,
                permissions-remove-all.label,
                permissions-exceptions-cookie-desc
              `,
            },
          },
        ],
      },
      {
        id: "deleteOnClose",
        l10nId: "sitedata-delete-on-close2",
      },
    ],
  },
  cookiesAndSiteData2: {
    inProgress: true,
    l10nId: "sitedata-heading",
    iconSrc: "chrome://browser/skin/controlcenter/3rdpartycookies.svg",
    headingLevel: 2,
    items: [
      {
        id: "siteDataSize",
        l10nId: "sitedata-total-size-calculating",
        control: "moz-box-item",
        supportPage: "sitedata-learn-more",
      },
      {
        id: "manageDataSettingsGroup",
        control: "moz-box-group",
        controlAttrs: {
          type: "default",
        },
        items: [
          {
            id: "clearSiteDataButton",
            l10nId: "sitedata-clear2",
            control: "moz-box-button",
            iconSrc: "chrome://browser/skin/flame.svg",
            controlAttrs: {
              "search-l10n-ids": `
                clear-site-data-cookies-empty.label,
                clear-site-data-cache-empty.label
              `,
            },
          },
          {
            id: "siteDataSettings",
            l10nId: "sitedata-settings3",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids": `
                site-data-settings-window.title,
                site-data-column-host.label,
                site-data-column-cookies.label,
                site-data-column-storage.label,
                site-data-settings-description,
                site-data-remove-all.label,
              `,
            },
          },
          {
            id: "cookieExceptions",
            l10nId: "sitedata-cookies-exceptions3",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids": `
                permissions-address,
                permissions-block.label,
                permissions-allow.label,
                permissions-remove.label,
                permissions-remove-all.label,
                permissions-exceptions-cookie-desc
              `,
            },
          },
        ],
      },
      {
        id: "deleteOnClose",
        l10nId: "sitedata-delete-on-close2",
      },
    ],
  },
  networkProxy: {
    l10nId: "network-proxy-group2",
    iconSrc: "chrome://devtools/skin/images/globe.svg",
    headingLevel: 1,
    supportPage: "prefs-connection-settings",
    subcategory: "netsettings",
    items: [
      {
        id: "connectionSettings",
        l10nId: "network-proxy-connection-settings2",
        control: "moz-box-button",
        controlAttrs: {
          "search-l10n-ids":
            "connection-window2.title,connection-proxy-option-no.label,connection-proxy-option-auto.label,connection-proxy-option-system.label,connection-proxy-option-wpad.label,connection-proxy-option-manual.label,connection-proxy-http,connection-proxy-https,connection-proxy-http-port,connection-proxy-socks,connection-proxy-socks4,connection-proxy-socks5,connection-proxy-noproxy,connection-proxy-noproxy-desc,connection-proxy-https-sharing.label,connection-proxy-autotype.label,connection-proxy-reload.label,connection-proxy-autologin-checkbox.label,connection-proxy-socks-remote-dns.label",
        },
      },
    ],
  },
  history: {
    l10nId: "history-group",
    headingLevel: 2,
    items: [
      {
        id: "historyMode",
        control: "moz-select",
        options: [
          {
            value: "remember",
            l10nId: "history-remember-option-all2",
          },
          { value: "dontremember", l10nId: "history-remember-option-never2" },
          { value: "custom", l10nId: "history-remember-option-custom2" },
        ],
        controlAttrs: {
          "search-l10n-ids": `
            history-remember-description4,
            history-dontremember-description4,
            history-custom-description4,
            history-private-browsing-permanent.label,
            history-remember-browser-option.label,
            history-remember-search-option.label,
            history-clear-on-close-option.label,
            history-clear-on-close-settings.label
          `,
        },
      },
      {
        id: "privateBrowsingAutoStart",
        l10nId: "history-private-browsing-permanent",
      },
      {
        id: "rememberHistory",
        l10nId: "history-remember-browser-option",
      },
      {
        id: "rememberForms",
        l10nId: "history-remember-search-option",
      },
      {
        id: "alwaysClear",
        l10nId: "history-clear-on-close-option",
      },
      {
        id: "clearDataSettings",
        l10nId: "history-clear-on-close-settings",
        control: "moz-box-button",
        controlAttrs: {
          "search-l10n-ids": `
            clear-data-settings-label,
            history-section-label,
            item-history-and-downloads.label,
            item-cookies.label,
            item-active-logins.label,
            item-cache.label,
            item-form-search-history.label,
            data-section-label,
            item-site-settings.label,
            item-offline-apps.label
          `,
        },
      },
      {
        id: "clearHistoryButton",
        l10nId: "history-clear-button",
        control: "moz-box-button",
      },
    ],
  },
  history2: {
    inProgress: true,
    l10nId: "history-section-header",
    iconSrc: "chrome://browser/skin/history.svg",
    items: [
      {
        id: "deleteOnCloseInfo",
        l10nId: "sitedata-delete-on-close-private-browsing4",
        control: "moz-message-bar",
      },
      {
        id: "historyMode",
        control: "moz-radio-group",
        options: [
          {
            value: "remember",
            l10nId: "history-remember-option-all2",
          },
          {
            value: "custom",
            l10nId: "history-remember-option-custom2",
            items: [
              {
                id: "customHistoryButton",
                control: "moz-box-button",
                l10nId: "history-custom-button",
              },
            ],
          },
          { value: "dontremember", l10nId: "history-remember-option-never2" },
        ],
        controlAttrs: {
          "search-l10n-ids": `
            history-remember-description3,
            history-dontremember-description3,
            history-private-browsing-permanent.label,
            history-remember-browser-option.label,
            history-remember-search-option.label,
            history-clear-on-close-option.label,
            history-clear-on-close-settings.label
          `,
        },
      },
    ],
  },
  historyAdvanced: {
    l10nId: "history-custom-section-header",
    headingLevel: 2,
    items: [
      {
        id: "privateBrowsingAutoStart",
        l10nId: "history-private-browsing-permanent",
      },
      {
        id: "rememberHistory",
        l10nId: "history-remember-browser-option",
      },
      {
        id: "rememberForms",
        l10nId: "history-remember-search-option",
      },
      {
        id: "alwaysClear",
        l10nId: "history-clear-on-close-option",
        items: [
          {
            id: "clearDataSettings",
            l10nId: "history-clear-on-close-settings",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids": `
                    clear-data-settings-label,
                    history-section-label,
                    item-history-and-downloads.label,
                    item-cookies.label,
                    item-active-logins.label,
                    item-cache.label,
                    item-form-search-history.label,
                    data-section-label,
                    item-site-settings.label,
                    item-offline-apps.label
                  `,
            },
          },
        ],
      },
    ],
  },
  dnsOverHttps: {
    l10nId: "dns-over-https-group2",
    headingLevel: 1,
    inProgress: true,
    items: [
      {
        id: "dohBox",
        control: "moz-box-group",
        controlAttrs: { searchkeywords: "doh trr" },
        items: [
          {
            id: "dohModeBoxItem",
            control: "moz-box-item",
          },
          {
            id: "dohAdvancedButton",
            l10nId: "preferences-doh-advanced-button",
            control: "moz-box-button",
          },
        ],
      },
    ],
  },
  dnsOverHttpsAdvanced: {
    inProgress: true,
    l10nId: "preferences-doh-advanced-section",
    supportPage: "dns-over-https",
    headingLevel: 2,
    items: [
      {
        id: "dohStatusBox",
        control: "moz-message-bar",
      },
      {
        id: "dohRadioGroup",
        control: "moz-radio-group",
        options: [
          {
            id: "dohRadioDefault",
            value: "default",
            l10nId: "preferences-doh-radio-default",
          },
          {
            id: "dohRadioCustom",
            value: "custom",
            l10nId: "preferences-doh-radio-custom",
            items: [
              {
                id: "dohFallbackIfCustom",
                l10nId: "preferences-doh-fallback-label",
              },
              {
                id: "dohProviderSelect",
                l10nId: "preferences-doh-select-resolver-label",
                control: "moz-select",
              },
              {
                id: "dohCustomProvider",
                control: "moz-input-text",
                l10nId: "preferences-doh-custom-provider-label",
              },
            ],
          },
          {
            id: "dohRadioOff",
            value: "off",
            l10nId: "preferences-doh-radio-off",
          },
        ],
      },
      {
        id: "dohExceptionsButton",
        l10nId: "preferences-doh-manage-exceptions2",
        control: "moz-box-button",
        controlAttrs: {
          "search-l10n-ids":
            "permissions-doh-entry-field,permissions-doh-add-exception.label,permissions-doh-remove.label,permissions-doh-remove-all.label,permissions-exceptions-doh-window.title,permissions-exceptions-manage-doh-desc,",
        },
      },
    ],
  },
  etpStatus: {
    inProgress: true,
    headingLevel: 2,
    l10nId: "preferences-etp-status-header",
    supportPage: "enhanced-tracking-protection",
    iconSrc: "chrome://browser/skin/controlcenter/tracking-protection.svg",
    items: [
      {
        id: "etpStatusBoxGroup",
        control: "moz-box-group",
        items: [
          {
            id: "etpStatusItem",
            l10nId: "preferences-etp-level-standard",
            control: "moz-box-item",
          },
          {
            id: "etpStatusAdvancedButton",
            l10nId: "preferences-etp-status-advanced-button",
            control: "moz-box-button",
          },
        ],
      },
      {
        id: "protectionsDashboardLink",
        l10nId: "preferences-etp-status-protections-dashboard-link",
        control: "moz-box-link",
        controlAttrs: {
          href: "about:protections",
        },
      },
    ],
  },
  etpBanner: {
    inProgress: true,
    card: "never",
    items: [
      {
        id: "etpBannerEl",
        control: "moz-card",
      },
    ],
  },
  etpAdvanced: {
    inProgress: true,
    headingLevel: 2,
    l10nId: "preferences-etp-advanced-settings-group",
    supportPage: "enhanced-tracking-protection",
    items: [
      {
        id: "contentBlockingCategoryRadioGroup",
        control: "moz-radio-group",
        options: [
          {
            id: "etpLevelStandard",
            value: "standard",
            l10nId: "preferences-etp-level-standard",
          },
          {
            id: "etpLevelStrict",
            value: "strict",
            l10nId: "preferences-etp-level-strict",
            items: [
              {
                id: "etpAllowListBaselineEnabled",
                l10nId: "content-blocking-baseline-exceptions-3",
                supportPage: "manage-enhanced-tracking-protection-exceptions",
                control: "moz-checkbox",
                items: [
                  {
                    id: "etpAllowListConvenienceEnabled",
                    l10nId: "content-blocking-convenience-exceptions-3",
                    control: "moz-checkbox",
                  },
                ],
              },
            ],
          },
          {
            id: "etpLevelCustom",
            value: "custom",
            l10nId: "preferences-etp-level-custom",
            items: [
              {
                id: "etpCustomizeButton",
                l10nId: "preferences-etp-customize-button",
                control: "moz-box-button",
              },
            ],
          },
        ],
      },
      {
        id: "reloadTabsHint",
        control: "moz-message-bar",
        l10nId: "preferences-etp-reload-tabs-hint",
        options: [
          {
            control: "moz-button",
            l10nId: "preferences-etp-reload-tabs-hint-button",
            slot: "actions",
          },
        ],
      },
      {
        id: "rfpWarning",
        control: "moz-message-bar",
        l10nId: "preferences-etp-rfp-warning-message",
        supportPage: "resist-fingerprinting",
      },
      {
        id: "etpLevelWarning",
        control: "moz-promo",
        l10nId: "preferences-etp-level-warning-message",
        controlAttrs: {
          ".imageAlignment": "end",
          ".imageSrc":
            "chrome://browser/content/preferences/etp-toggle-promo.svg",
        },
      },
      {
        id: "etpManageExceptionsButton",
        l10nId: "preferences-etp-manage-exceptions-button",
        control: "moz-box-button",
      },
    ],
  },
  etpReset: {
    inProgress: true,
    headingLevel: 2,
    l10nId: "preferences-etp-reset",
    items: [
      {
        id: "etpResetButtonGroup",
        control: "div",
        items: [
          {
            id: "etpResetStandardButton",
            control: "moz-button",
            l10nId: "preferences-etp-reset-standard-button",
          },
          {
            id: "etpResetStrictButton",
            control: "moz-button",
            l10nId: "preferences-etp-reset-strict-button",
          },
        ],
      },
    ],
  },
  etpCustomize: {
    inProgress: true,
    headingLevel: 2,
    l10nId: "preferences-etp-custom-control-group",
    items: [
      {
        id: "etpAllowListBaselineEnabledCustom",
        l10nId: "content-blocking-baseline-exceptions-3",
        supportPage: "manage-enhanced-tracking-protection-exceptions",
        control: "moz-checkbox",
        items: [
          {
            id: "etpAllowListConvenienceEnabledCustom",
            l10nId: "content-blocking-convenience-exceptions-3",
            control: "moz-checkbox",
          },
        ],
      },
      {
        id: "etpCustomCookiesEnabled",
        l10nId: "preferences-etp-custom-cookies-enabled",
        control: "moz-toggle",
        items: [
          {
            id: "cookieBehavior",
            l10nId: "preferences-etp-custom-cookie-behavior",
            control: "moz-select",
            options: [
              {
                value: Ci.nsICookieService.BEHAVIOR_ACCEPT.toString(),
                l10nId: "preferences-etpc-custom-cookie-behavior-accept-all",
              },
              {
                value: Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER.toString(),
                l10nId: "sitedata-option-block-cross-site-trackers",
              },
              {
                value:
                  Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN.toString(),
                l10nId: "sitedata-option-block-cross-site-cookies2",
              },
              {
                value: Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN.toString(),
                l10nId: "sitedata-option-block-unvisited",
              },
              {
                value: Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN.toString(),
                l10nId: "sitedata-option-block-all-cross-site-cookies",
              },
              {
                value: Ci.nsICookieService.BEHAVIOR_REJECT.toString(),
                l10nId: "sitedata-option-block-all",
              },
            ],
          },
        ],
      },
      {
        id: "etpCustomTrackingProtectionEnabled",
        l10nId: "preferences-etp-custom-tracking-protection-enabled",
        control: "moz-toggle",
        items: [
          {
            id: "etpCustomTrackingProtectionEnabledContext",
            l10nId:
              "preferences-etp-custom-tracking-protection-enabled-context",
            control: "moz-select",
            options: [
              {
                value: "all",
                l10nId:
                  "content-blocking-tracking-protection-option-all-windows",
              },
              {
                value: "pbmOnly",
                l10nId: "content-blocking-option-private",
              },
            ],
          },
        ],
      },
      {
        id: "etpCustomCryptominingProtectionEnabled",
        l10nId: "preferences-etp-custom-crypto-mining-protection-enabled",
        control: "moz-toggle",
      },
      {
        id: "etpCustomKnownFingerprintingProtectionEnabled",
        l10nId:
          "preferences-etp-custom-known-fingerprinting-protection-enabled",
        control: "moz-toggle",
      },
      {
        id: "etpCustomSuspectFingerprintingProtectionEnabled",
        l10nId:
          "preferences-etp-custom-suspect-fingerprinting-protection-enabled",
        control: "moz-toggle",
        items: [
          {
            id: "etpCustomSuspectFingerprintingProtectionEnabledContext",
            l10nId:
              "preferences-etp-custom-suspect-fingerprinting-protection-enabled-context",
            control: "moz-select",
            options: [
              {
                value: "all",
                l10nId:
                  "content-blocking-tracking-protection-option-all-windows",
              },
              {
                value: "pbmOnly",
                l10nId: "content-blocking-option-private",
              },
            ],
          },
        ],
      },
    ],
  },
  connectionLink: {
    l10nId: "preferences-connection-link-section",
    iconSrc: "chrome://devtools/skin/images/globe.svg",
    items: [
      {
        id: "connectionLinkButton",
        l10nId: "preferences-connection-link-button",
        control: "moz-box-button",
      },
    ],
  },
  ipprotection: {
    l10nId: "ip-protection-description-1",
    headingLevel: 2,
    supportPage: "built-in-vpn",
    items: [
      {
        id: "ipProtectionNotOptedInSection",
        l10nId: "ip-protection-not-opted-in-3",
        l10nArgs: {
          maxUsage: "50",
        },
        control: "moz-promo",
        controlAttrs: {
          imagesrc:
            "chrome://browser/content/ipprotection/assets/vpn-settings-get-started.svg",
          imagealignment: "end",
          imagewidth: "large",
        },
        items: [
          {
            id: "getStartedButton",
            l10nId: "ip-protection-not-opted-in-button",
            control: "moz-button",
            slot: "actions",
            controlAttrs: {
              type: "primary",
            },
          },
        ],
      },
      {
        id: "ipProtectionExceptions",
        control: "moz-fieldset",
        controlAttrs: {
          ".headingLevel": 3,
        },
        items: [
          {
            id: "ipProtectionExceptionAllListButton",
            control: "moz-box-button",
          },
        ],
      },
      {
        id: "ipProtectionAutoStart",
        l10nId: "ip-protection-autostart",
        control: "moz-fieldset",
        items: [
          {
            id: "ipProtectionAutoStartCheckbox",
            l10nId: "ip-protection-autostart-checkbox",
            control: "moz-checkbox",
          },
          {
            id: "ipProtectionAutoStartPrivateCheckbox",
            l10nId: "ip-protection-autostart-private-checkbox",
            control: "moz-checkbox",
          },
        ],
      },
      {
        id: "ipProtectionBandwidthSection",
        control: "moz-box-item",
        items: [{ id: "ipProtectionBandwidth", control: "bandwidth-usage" }],
      },
      {
        id: "ipProtectionLinks",
        control: "moz-box-link",
        l10nId: "ip-protection-vpn-upgrade-link",
        controlAttrs: {
          href: "https://www.mozilla.org/products/vpn/?utm_medium=fx-desktop&utm_campaign=fx-vpn&utm_source=settings",
        },
      },
    ],
  },
  privacyPanel: {
    iconSrc: "chrome://devtools/skin/images/globe.svg",
    l10nId: "privacy-panel-settings-header",
    headingLevel: 2,
    supportPage: "breach-alerts-privacy-panel",
    items: [
      {
        id: "trustPanelBreachAlertsMain",
        l10nId: "privacy-panel-breach-alerts",
      },
    ],
  },
});

Preferences.addSetting({
  id: "trustPanelFeatureGate",
  pref: "browser.urlbar.trustPanel.featureGate",
});

Preferences.addSetting({
  id: "trustPanelBreachAlertsMain",
  pref: "browser.urlbar.trustPanel.breachAlerts",
  deps: ["trustPanelFeatureGate"],
  visible: ({ trustPanelFeatureGate }) => trustPanelFeatureGate.value,
});

/**
 * This class is used to create Settings that are used to warn the user about
 * potential misconfigurations. It should be passed into Preferences.addSetting
 * to create the Preference for a <moz-box-item> because it creates
 * separate members on pref.config
 *
 * @implements {SettingConfig}
 */
class WarningSettingConfig {
  /**
   * This callback type specifies the most important part of a WarningSettingConfig: how to know
   * when to warn.
   *
   * @callback problematicCallback
   * @param {WarningSettingConfig} self - this is a Setting config created by the constructor below,
   * that has been `setup` and not yet cleaned up. Its prefMapping is setup into its properties.
   * @returns {boolean} Should this Setting show a warning to the user if not yet dismissed?
   */

  /**
   *
   * @param {string} id - The unique setting ID for the setting created by this config
   * @param {{[key: string]: string}} prefMapping - A map from member name (to be used in the
   * `problematic` arg's arg) to pref string, containing all of the preferences this Setting
   * relies upon. On setup, this object will create properties for each entry here, where the
   * value is the result of Preferences.get(key).
   * @param {problematicCallback} problematic - How we determine whether or not to show this
   * setting initially
   * @param {boolean} isDismissable - A boolean indicating whether or not we should support dismissing
   * this setting
   */
  constructor(id, prefMapping, problematic, isDismissable) {
    this.id = id;
    this.prefMapping = prefMapping;
    if (isDismissable) {
      this.dismissedPrefId = `browser.preferences.config_warning.${this.id}.dismissed`;
      this.prefMapping.dismissed = this.dismissedPrefId;
      this.dismissAllPrefId = `browser.preferences.config_warning.dismissAll`;
      this.prefMapping.dismissAll = this.dismissAllPrefId;
    }
    this.problematic = problematic;
  }

  /**
   * This item in a warning moz-box-group should be visible if the `problematic` argument
   * from the constructor says we should, and it isn't hidden.
   *
   * @returns {boolean} Whether or not to show this configuration as a warning to the user
   */
  visible() {
    return (
      !this.dismissAll?.value &&
      !this.dismissed?.value &&
      this.problematic(this)
    );
  }

  /**
   * This resets all of the preferernces in the `prefMapping` from the constructor that have
   * user-specified values. This includes the dismiss pref as well.
   */
  reset() {
    for (let getter of Object.keys(this.prefMapping)) {
      if (this[getter].hasUserValue) {
        this[getter].reset();
      }
    }
  }

  /**
   * When invoked, this sets a pref that persistently hides this setting. See visible().
   */
  dismiss() {
    if (this.dismissed) {
      this.dismissed.value = true;
    }
  }

  /**
   * This initializes the Setting created with this config, starting listeners for all dependent
   * Preferences and providing a cleanup callback to remove them
   *
   * @param {() => any} emitChange - a callback to be invoked any time that the Setting created
   * with this config is changed
   * @returns {() => any} a function that cleans up the state from this Setting, namely pref change listeners.
   */
  setup(emitChange) {
    for (let [getter, prefId] of Object.entries(this.prefMapping)) {
      this[getter] = Preferences.get(prefId);
      this[getter].on("change", emitChange);
    }
    return () => {
      for (let getter of Object.keys(this.prefMapping)) {
        this[getter].off(emitChange);
      }
    };
  }

  /**
   * Setting helper to handle clicks of our warning. They may be a "reset" or
   * "dismiss" action depending on the target, and those callbacks are defined
   * in this class.
   *
   * @param {PointerEvent} event - The event for the user click
   */
  onUserClick(event) {
    switch (event.target.id) {
      case "reset": {
        this.reset();
        Glean.securityPreferencesWarnings.warningFixed.record();
        break;
      }
      case "dismiss": {
        this.dismiss();
        Glean.securityPreferencesWarnings.warningDismissed.record();
        break;
      }
    }
  }
}

if (SECURITY_PRIVACY_STATUS_CARD_ENABLED) {
  Preferences.addSetting(
    new WarningSettingConfig(
      "warningTest",
      {
        showIssue: "privacy.ui.status_card.testing.show_issue",
      },
      ({ showIssue }) => showIssue.hasUserValue && !showIssue.locked,
      true
    )
  );

  Preferences.addSetting(
    new WarningSettingConfig(
      "warningAllowFingerprinters",
      {
        fingerprintingEnabled:
          "privacy.trackingprotection.fingerprinting.enabled",
      },
      ({ fingerprintingEnabled }) =>
        !fingerprintingEnabled.value && !fingerprintingEnabled.locked,
      true
    )
  );

  Preferences.addSetting(
    new WarningSettingConfig(
      "warningThirdPartyCookies",
      {
        cookieBehavior: "network.cookie.cookieBehavior",
      },
      ({ cookieBehavior }) =>
        (cookieBehavior.value == 0 ||
          cookieBehavior.value == 3 ||
          cookieBehavior.value == 4) &&
        !cookieBehavior.locked,
      true
    )
  );

  Preferences.addSetting(
    new WarningSettingConfig(
      "warningPasswordManager",
      {
        enabled: "signon.rememberSignons",
        extentionAllows: "services.passwordSavingEnabled",
      },
      ({ enabled, extentionAllows }) =>
        !enabled.value && !enabled.locked && !extentionAllows.value,
      true
    )
  );

  Preferences.addSetting(
    new WarningSettingConfig(
      "warningPopupBlocker",
      {
        enabled: "dom.disable_open_during_load",
      },
      ({ enabled }) => !enabled.value && !enabled.locked,
      true
    )
  );

  Preferences.addSetting(
    new WarningSettingConfig(
      "warningExtensionInstall",
      {
        blockInstalls: "xpinstall.whitelist.required",
      },
      ({ blockInstalls }) => !blockInstalls.value && !blockInstalls.locked,
      true
    )
  );

  Preferences.addSetting(
    new WarningSettingConfig(
      "warningDoH",
      {
        dohMode: "network.trr.mode",
      },
      ({ dohMode }) => dohMode.value == 5 && !dohMode.locked,
      true
    )
  );

  Preferences.addSetting(
    new WarningSettingConfig(
      "warningECH",
      {
        echEnabled: "network.dns.echconfig.enabled",
        https3echEnabled: "network.dns.http3_echconfig.enabled",
      },
      ({ echEnabled, https3echEnabled }) =>
        (!echEnabled.value && !echEnabled.locked) ||
        (!https3echEnabled.value && !https3echEnabled.locked),
      true
    )
  );

  Preferences.addSetting(
    new WarningSettingConfig(
      "warningProxyAutodetection",
      {
        proxyType: "network.proxy.type",
      },
      ({ proxyType }) => proxyType.value == 2 && !proxyType.locked,
      true
    )
  );

  /** @type {SettingControlConfig[]} */
  const SECURITY_WARNINGS = [
    {
      l10nId: "security-privacy-issue-warning-test",
      id: "warningTest",
    },
    {
      l10nId: "security-privacy-issue-warning-fingerprinters",
      id: "warningAllowFingerprinters",
    },
    {
      l10nId: "security-privacy-issue-warning-third-party-cookies",
      id: "warningThirdPartyCookies",
    },
    {
      l10nId: "security-privacy-issue-warning-password-manager",
      id: "warningPasswordManager",
    },
    {
      l10nId: "security-privacy-issue-warning-popup-blocker",
      id: "warningPopupBlocker",
    },
    {
      l10nId: "security-privacy-issue-warning-extension-install",
      id: "warningExtensionInstall",
    },
    {
      l10nId: "security-privacy-issue-warning-safe-browsing",
      id: "warningSafeBrowsing",
    },
    {
      l10nId: "security-privacy-issue-warning-doh",
      id: "warningDoH",
    },
    {
      l10nId: "security-privacy-issue-warning-ech",
      id: "warningECH",
    },

    {
      l10nId: "security-privacy-issue-warning-proxy-autodetection",
      id: "warningProxyAutodetection",
    },
  ];

  Preferences.addSetting(
    /** @type {{ makeSecurityWarningItems: () => SettingControlConfig[] } & SettingConfig} */ ({
      id: "securityWarningsGroup",
      makeSecurityWarningItems() {
        return SECURITY_WARNINGS.map(({ id, l10nId }) => ({
          id,
          l10nId,
          control: "moz-box-item",
          options: [
            {
              control: "moz-button",
              l10nId: "issue-card-reset-button",
              controlAttrs: { slot: "actions", size: "small", id: "reset" },
            },
            {
              control: "moz-button",
              l10nId: "issue-card-dismiss-button",
              controlAttrs: {
                slot: "actions",
                size: "small",
                iconsrc: "chrome://global/skin/icons/close.svg",
                id: "dismiss",
              },
            },
          ],
        }));
      },
      getControlConfig(config) {
        if (!config.items) {
          return { ...config, items: this.makeSecurityWarningItems() };
        }
        return config;
      },
    })
  );

  Preferences.addSetting({
    id: "etpStrictEnabled",
    pref: "browser.contentblocking.category",
    get: prefValue => prefValue == "strict",
  });

  Preferences.addSetting({
    id: "etpCustomEnabled",
    pref: "browser.contentblocking.category",
    get: prefValue => prefValue == "custom",
  });

  Preferences.addSetting(
    /** @type {{ cachedValue: number, loadTrackerCount: (emitChange: SettingEmitChange) => Promise<void> } & SettingConfig} */ ({
      id: "trackerCount",
      cachedValue: null,
      async loadTrackerCount(emitChange) {
        const now = new Date();
        const aMonthAgo = new Date(now.getTime() - 30 * 24 * 60 * 60 * 1000);
        /** @type {{ getResultByName: (_: string) => number }[]} */
        const events = await lazy.TrackingDBService.getEventsByDateRange(
          aMonthAgo,
          now
        );

        const total = events.reduce((acc, day) => {
          return acc + day.getResultByName("count");
        }, 0);
        this.cachedValue = total;
        emitChange();
      },
      setup(emitChange) {
        this.loadTrackerCount(emitChange);
      },
      get() {
        return this.cachedValue;
      },
    })
  );

  Preferences.addSetting(
    /** @type {{ cachedValue: any } & SettingConfig} */ ({
      id: "appUpdateStatus",
      cachedValue: undefined,
      setup(emitChange) {
        if (lazy.AppConstants.MOZ_UPDATER && !lazy.isPackagedApp) {
          let appUpdater = new lazy.AppUpdater();
          /**
           * @param {number} appStatus
           * @param {any[]} _args
           */
          let listener = (appStatus, ..._args) => {
            this.cachedValue = appStatus;
            emitChange();
          };
          appUpdater.addListener(listener);
          appUpdater.check();
          return () => {
            appUpdater.removeListener(listener);
            appUpdater.stop();
          };
        }
        return () => {};
      },
      get() {
        return this.cachedValue;
      },
      set(value) {
        this.cachedValue = value;
      },
    })
  );

  Preferences.addSetting({
    id: "privacyCard",
    deps: [
      "appUpdateStatus",
      "trackerCount",
      "etpStrictEnabled",
      "etpCustomEnabled",
      ...SECURITY_WARNINGS.map(warning => warning.id),
    ],
  });

  Preferences.addSetting({
    id: "warningCard",
    deps: SECURITY_WARNINGS.map(warning => warning.id),
    _telemetrySent: false,
    visible(deps) {
      const count = Object.values(deps).filter(
        depSetting => depSetting.visible
      ).length;
      if (!this._telemetrySent) {
        Glean.securityPreferencesWarnings.warningsShown.record({ count });
        this._telemetrySent = true;
      }
      return count > 0;
    },
  });
}

Preferences.addSetting({
  id: "ipProtectionVisible",
  pref: "browser.ipProtection.enabled",
});
Preferences.addSetting({
  id: "ipProtectionNotOptedIn",
  pref: "browser.ipProtection.entitlementCache",
  get: prefVal => !prefVal,
});
Preferences.addSetting({
  id: "ipProtectionSubscribedToVpn",
  pref: "browser.ipProtection.entitlementCache",
  get: cacheObj => {
    try {
      // subscribed property should be a boolean, so assume not subscribed
      // if the property is somehow an invalid type (eg. string).
      return JSON.parse(cacheObj)?.subscribed === true;
    } catch {
      // Assume not subscribed if cache is missing or malformed.
      return false;
    }
  },
});
Preferences.addSetting({
  id: "ipProtectionNotOptedInSection",
  deps: ["ipProtectionVisible", "ipProtectionNotOptedIn"],
  visible: ({ ipProtectionVisible, ipProtectionNotOptedIn }) =>
    ipProtectionVisible.value && ipProtectionNotOptedIn.value,
});
Preferences.addSetting({
  id: "getStartedButton",
  deps: ["ipProtectionVisible", "ipProtectionNotOptedIn"],
  visible: ({ ipProtectionVisible, ipProtectionNotOptedIn }) =>
    ipProtectionVisible.value && ipProtectionNotOptedIn.value,
  onUserClick() {
    lazy.IPProtection.getPanel(window.browsingContext.topChromeWindow)?.enroll({
      entrypoint: "vpn_integration_settings",
      utm_source: "settings",
    });
  },
});

Preferences.addSetting({
  id: "ipProtectionSiteExceptionsFeatureEnabled",
  pref: "browser.ipProtection.features.siteExceptions",
});
Preferences.addSetting({
  id: "ipProtectionExceptions",
  deps: [
    "ipProtectionVisible",
    "ipProtectionSiteExceptionsFeatureEnabled",
    "ipProtectionNotOptedIn",
  ],
  visible: ({
    ipProtectionVisible,
    ipProtectionSiteExceptionsFeatureEnabled,
    ipProtectionNotOptedIn,
  }) =>
    ipProtectionVisible.value &&
    ipProtectionSiteExceptionsFeatureEnabled.value &&
    !ipProtectionNotOptedIn.value,
});

Preferences.addSetting({
  id: "ipProtectionExceptionAllListButton",
  deps: [
    "ipProtectionVisible",
    "ipProtectionSiteExceptionsFeatureEnabled",
    "ipProtectionNotOptedIn",
  ],
  setup(emitChange) {
    let permObserver = {
      observe(subject, topic, _data) {
        if (subject && topic === "perm-changed") {
          let permission = subject.QueryInterface(Ci.nsIPermission);
          if (permission.type === "ipp-vpn") {
            emitChange();
          }
        }
      },
    };
    Services.obs.addObserver(permObserver, "perm-changed");
    return () => {
      Services.obs.removeObserver(permObserver, "perm-changed");
    };
  },
  visible: ({
    ipProtectionVisible,
    ipProtectionSiteExceptionsFeatureEnabled,
    ipProtectionNotOptedIn,
  }) =>
    ipProtectionVisible.value &&
    ipProtectionSiteExceptionsFeatureEnabled.value &&
    !ipProtectionNotOptedIn.value,
  onUserClick() {
    let params = {
      addVisible: true,
      hideStatusColumn: true,
      prefilledHost: "",
      permissionType: "ipp-vpn",
      capabilityFilter: Ci.nsIPermissionManager.DENY_ACTION,
    };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/permissions.xhtml",
      { features: "resizable=yes" },
      params
    );
  },
  getControlConfig(config) {
    let l10nId = "ip-protection-site-exceptions-all-sites-button";

    let savedExceptions = Services.perms.getAllByTypes(["ipp-vpn"]);
    let numberOfExclusions = savedExceptions.filter(
      perm => perm.capability === Ci.nsIPermissionManager.DENY_ACTION
    ).length;

    let l10nArgs = {
      count: numberOfExclusions,
    };

    return {
      ...config,
      l10nId,
      l10nArgs,
    };
  },
});
Preferences.addSetting({
  id: "ipProtectionAutoStartFeatureEnabled",
  pref: "browser.ipProtection.features.autoStart",
  get: prefVal => prefVal,
});
Preferences.addSetting({
  id: "ipProtectionAutoStart",
  deps: [
    "ipProtectionVisible",
    "ipProtectionAutoStartFeatureEnabled",
    "ipProtectionNotOptedIn",
  ],
  visible: ({
    ipProtectionVisible,
    ipProtectionAutoStartFeatureEnabled,
    ipProtectionNotOptedIn,
  }) =>
    ipProtectionVisible.value &&
    ipProtectionAutoStartFeatureEnabled.value &&
    !ipProtectionNotOptedIn.value,
});
Preferences.addSetting({
  id: "ipProtectionAutoStartCheckbox",
  pref: "browser.ipProtection.autoStartEnabled",
  deps: [
    "ipProtectionVisible",
    "ipProtectionAutoStart",
    "ipProtectionNotOptedIn",
  ],
  visible: ({ ipProtectionVisible, ipProtectionNotOptedIn }) =>
    ipProtectionVisible.value && !ipProtectionNotOptedIn.value,
});
Preferences.addSetting({
  id: "ipProtectionAutoStartPrivateCheckbox",
  pref: "browser.ipProtection.autoStartPrivateEnabled",
  deps: [
    "ipProtectionVisible",
    "ipProtectionAutoStart",
    "ipProtectionNotOptedIn",
  ],
  visible: ({ ipProtectionVisible, ipProtectionNotOptedIn }) =>
    ipProtectionVisible.value && !ipProtectionNotOptedIn.value,
});
Preferences.addSetting({
  id: "ipProtectionBandwidthVisible",
  deps: ["ipProtectionVisible"],
  pref: "browser.ipProtection.bandwidth.enabled",
});
Preferences.addSetting({
  id: "ipProtectionBandwidthSection",
  deps: [
    "ipProtectionVisible",
    "ipProtectionBandwidthVisible",
    "ipProtectionNotOptedIn",
  ],
  visible: ({
    ipProtectionVisible,
    ipProtectionBandwidthVisible,
    ipProtectionNotOptedIn,
  }) =>
    ipProtectionVisible.value &&
    ipProtectionBandwidthVisible.value &&
    !ipProtectionNotOptedIn.value,
});
Preferences.addSetting({
  id: "ipProtectionBandwidth",
  deps: [
    "ipProtectionVisible",
    "ipProtectionBandwidthVisible",
    "ipProtectionBandwidthSection",
    "ipProtectionNotOptedIn",
  ],
  visible: ({
    ipProtectionVisible,
    ipProtectionBandwidthVisible,
    ipProtectionNotOptedIn,
  }) =>
    ipProtectionVisible.value &&
    ipProtectionBandwidthVisible.value &&
    !ipProtectionNotOptedIn.value,
  pref: "browser.ipProtection.usageCache",
  getControlConfig: config => {
    const usagePref = Services.prefs.getStringPref(
      "browser.ipProtection.usageCache",
      ""
    );
    let usage;
    if (usagePref) {
      usage = JSON.parse(usagePref);
    } else {
      usage = {
        max: lazy.BANDWIDTH.MAX_IN_GB * lazy.BANDWIDTH.BYTES_IN_GB,
        remaining: lazy.BANDWIDTH.MAX_IN_GB * lazy.BANDWIDTH.BYTES_IN_GB,
      };
    }

    return {
      ...config,
      controlAttrs: usage,
    };
  },
});
Preferences.addSetting({
  id: "ipProtectionLinks",
  deps: [
    "ipProtectionVisible",
    "ipProtectionNotOptedIn",
    "ipProtectionSubscribedToVpn",
  ],
  visible: ({
    ipProtectionVisible,
    ipProtectionNotOptedIn,
    ipProtectionSubscribedToVpn,
  }) =>
    ipProtectionVisible.value &&
    !ipProtectionNotOptedIn.value &&
    !ipProtectionSubscribedToVpn.value,
});

Preferences.addSetting({
  id: "gpcFunctionalityEnabled",
  pref: "privacy.globalprivacycontrol.functionality.enabled",
});
Preferences.addSetting({
  id: "gpcEnabled",
  pref: "privacy.globalprivacycontrol.enabled",
  deps: ["gpcFunctionalityEnabled"],
  visible: ({ gpcFunctionalityEnabled }) => {
    return gpcFunctionalityEnabled.value;
  },
});
Preferences.addSetting({
  id: "relayFeature",
  pref: "signon.firefoxRelay.feature",
});
Preferences.addSetting({
  id: "relayIntegration",
  deps: ["savePasswords", "relayFeature"],
  visible: () => {
    return lazy.FirefoxRelay.isAvailable;
  },
  disabled: ({ savePasswords, relayFeature }) => {
    return !savePasswords.value || relayFeature.pref.locked;
  },
  get() {
    return lazy.FirefoxRelay.isAvailable && !lazy.FirefoxRelay.isDisabled;
  },
  set(checked) {
    if (checked) {
      lazy.FirefoxRelay.markAsAvailable();
    } else {
      lazy.FirefoxRelay.markAsDisabled();
    }
  },
  onUserChange(checked) {
    if (checked) {
      Glean.relayIntegration.enabledPrefChange.record();
    } else {
      Glean.relayIntegration.disabledPrefChange.record();
    }
  },
});
Preferences.addSetting({
  id: "dntHeaderEnabled",
  pref: "privacy.donottrackheader.enabled",
});
Preferences.addSetting({
  id: "dntRemoval",
  pref: "browser.preferences.config_warning.donottrackheader.dismissed",
  deps: ["dntHeaderEnabled"],
  visible: ({ dntHeaderEnabled }, setting) => {
    return dntHeaderEnabled.value && !setting.value;
  },
  onUserClick: (event, _deps, setting) => {
    let dismissButton = event.target?.shadowRoot?.querySelector(".close");
    if (
      dismissButton?.shadowRoot &&
      event.originalTarget &&
      dismissButton.shadowRoot.contains(event.originalTarget)
    ) {
      setting.value = true;
    }
  },
});

Preferences.addSetting({
  id: "httpsOnlyEnabled",
  pref: "dom.security.https_only_mode",
});
Preferences.addSetting({
  id: "httpsOnlyEnabledPBM",
  pref: "dom.security.https_only_mode_pbm",
});
Preferences.addSetting({
  id: "httpsOnlyRadioGroup",
  deps: ["httpsOnlyEnabled", "httpsOnlyEnabledPBM"],
  get: (_value, deps) => {
    if (deps.httpsOnlyEnabled.value) {
      return "enabled";
    }
    if (deps.httpsOnlyEnabledPBM.value) {
      return "privateOnly";
    }
    return "disabled";
  },
  set: (value, deps) => {
    if (value == "enabled") {
      deps.httpsOnlyEnabled.value = true;
      deps.httpsOnlyEnabledPBM.value = false;
    } else if (value == "privateOnly") {
      deps.httpsOnlyEnabled.value = false;
      deps.httpsOnlyEnabledPBM.value = true;
    } else if (value == "disabled") {
      deps.httpsOnlyEnabled.value = false;
      deps.httpsOnlyEnabledPBM.value = false;
    }
  },
  disabled: deps => {
    return deps.httpsOnlyEnabled.locked || deps.httpsOnlyEnabledPBM.locked;
  },
});
Preferences.addSetting({
  id: "httpsFirstEnabled",
  pref: "dom.security.https_first",
});
Preferences.addSetting({
  id: "httpsFirstEnabledPBM",
  pref: "dom.security.https_first_pbm",
});
Preferences.addSetting({
  id: "httpsOnlyExceptionButton",
  deps: [
    "httpsOnlyEnabled",
    "httpsOnlyEnabledPBM",
    "httpsFirstEnabled",
    "httpsFirstEnabledPBM",
  ],
  disabled: deps => {
    return (
      !deps.httpsOnlyEnabled.value &&
      !deps.httpsOnlyEnabledPBM.value &&
      !deps.httpsFirstEnabled.value &&
      !deps.httpsFirstEnabledPBM.value
    );
  },
  onUserClick: () => {
    PrivacySettingHelpers.showHttpsOnlyModeExceptions();
  },
});

Preferences.addSetting({
  id: "enableSafeBrowsingPhishing",
  pref: "browser.safebrowsing.phishing.enabled",
});
Preferences.addSetting({
  id: "enableSafeBrowsingMalware",
  pref: "browser.safebrowsing.malware.enabled",
});
Preferences.addSetting({
  id: "enableSafeBrowsing",
  deps: ["enableSafeBrowsingPhishing", "enableSafeBrowsingMalware"],
  get: (_value, deps) => {
    return (
      deps.enableSafeBrowsingPhishing.value &&
      deps.enableSafeBrowsingMalware.value
    );
  },
  set: (value, deps) => {
    deps.enableSafeBrowsingPhishing.value = value;
    deps.enableSafeBrowsingMalware.value = value;
  },
  disabled: deps => {
    return (
      deps.enableSafeBrowsingPhishing.locked ||
      deps.enableSafeBrowsingMalware.locked
    );
  },
});
Preferences.addSetting(
  new WarningSettingConfig(
    "warningSafeBrowsing",
    {
      malware: "browser.safebrowsing.malware.enabled",
      phishing: "browser.safebrowsing.phishing.enabled",
      downloads: "browser.safebrowsing.downloads.enabled",
      unwantedDownloads:
        "browser.safebrowsing.downloads.remote.block_potentially_unwanted",
      uncommonDownloads:
        "browser.safebrowsing.downloads.remote.block_potentially_unwanted",
    },
    ({ malware, phishing, downloads, unwantedDownloads, uncommonDownloads }) =>
      (!malware.value && !malware.locked) ||
      (!phishing.value && !phishing.locked) ||
      (!downloads.value && !downloads.locked) ||
      (!unwantedDownloads.value && !unwantedDownloads.locked) ||
      (!uncommonDownloads.value && !uncommonDownloads.locked),
    true
  )
);
Preferences.addSetting({
  id: "safeBrowsingWarningMessageBox",
  deps: ["warningSafeBrowsing"],
  visible({ warningSafeBrowsing }) {
    return warningSafeBrowsing.visible;
  },
  onMessageBarDismiss(_, { warningSafeBrowsing }) {
    warningSafeBrowsing.config.dismiss();
  },
});
Preferences.addSetting({
  id: "blockDownloads",
  pref: "browser.safebrowsing.downloads.enabled",
  deps: ["enableSafeBrowsing"],
  disabled: (deps, selfSetting) => {
    return !deps.enableSafeBrowsing.value || selfSetting.locked;
  },
});
Preferences.addSetting({
  id: "malwareTable",
  pref: "urlclassifier.malwareTable",
});
Preferences.addSetting({
  id: "blockUncommonDownloads",
  pref: "browser.safebrowsing.downloads.remote.block_uncommon",
});
Preferences.addSetting({
  id: "blockUnwantedDownloads",
  pref: "browser.safebrowsing.downloads.remote.block_potentially_unwanted",
});
Preferences.addSetting({
  id: "blockUncommonUnwanted",
  deps: [
    "enableSafeBrowsing",
    "blockDownloads",
    "blockUncommonDownloads",
    "blockUnwantedDownloads",
  ],
  get: (_value, deps) => {
    return (
      deps.blockUncommonDownloads.value && deps.blockUnwantedDownloads.value
    );
  },
  set: (value, deps) => {
    deps.blockUncommonDownloads.value = value;
    deps.blockUnwantedDownloads.value = value;

    let malwareTable = Preferences.get("urlclassifier.malwareTable");
    let malware = /** @type {string} */ (malwareTable.value)
      .split(",")
      .filter(
        x =>
          x !== "goog-unwanted-proto" &&
          x !== "goog-unwanted-shavar" &&
          x !== "moztest-unwanted-simple"
      );

    if (value) {
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
    lazy.listManager.forceUpdates(malwareTable.value);
  },
  disabled: deps => {
    return (
      !deps.enableSafeBrowsing.value ||
      !deps.blockDownloads.value ||
      deps.blockUncommonDownloads.locked ||
      deps.blockUnwantedDownloads.locked
    );
  },
});
Preferences.addSetting({
  id: "manageDataSettingsGroup",
});
Preferences.addSetting(
  /** @type {{ isUpdatingSites: boolean, usage: { value: number, unit: string } | void } & SettingConfig} */ ({
    id: "siteDataSize",
    usage: null,
    isUpdatingSites: false,
    setup(emitChange) {
      let onUsageChanged = async () => {
        let [siteDataUsage, cacheUsage] = await Promise.all([
          lazy.SiteDataManager.getTotalUsage(),
          lazy.SiteDataManager.getCacheSize(),
        ]);
        let totalUsage = siteDataUsage + cacheUsage;
        let [value, unit] = lazy.DownloadUtils.convertByteUnits(totalUsage);
        this.usage = { value, unit };

        this.isUpdatingSites = false;
        emitChange();
      };

      let onUpdatingSites = () => {
        this.isUpdatingSites = true;
        emitChange();
      };

      Services.obs.addObserver(onUsageChanged, "sitedatamanager:sites-updated");
      Services.obs.addObserver(
        onUpdatingSites,
        "sitedatamanager:updating-sites"
      );

      return () => {
        Services.obs.removeObserver(
          onUsageChanged,
          "sitedatamanager:sites-updated"
        );
        Services.obs.removeObserver(
          onUpdatingSites,
          "sitedatamanager:updating-sites"
        );
      };
    },
    getControlConfig(config) {
      if (this.isUpdatingSites || !this.usage) {
        // Data not retrieved yet, show a loading state.
        return {
          ...config,
          l10nId: "sitedata-total-size-calculating",
        };
      }

      let { value, unit } = this.usage;
      return {
        ...config,
        l10nId: "sitedata-total-size3",
        l10nArgs: {
          value,
          unit,
        },
      };
    },
  })
);

Preferences.addSetting({
  id: "deleteOnCloseInfo",
  deps: ["privateBrowsingAutoStart"],
  visible({ privateBrowsingAutoStart }) {
    return privateBrowsingAutoStart.value;
  },
});

Preferences.addSetting(
  /** @type {{ isUpdatingSites: boolean } & SettingConfig} */ ({
    id: "clearSiteDataButton",
    isUpdatingSites: false,
    setup(emitChange) {
      let onSitesUpdated = async () => {
        this.isUpdatingSites = false;
        emitChange();
      };

      let onUpdatingSites = () => {
        this.isUpdatingSites = true;
        emitChange();
      };

      Services.obs.addObserver(onSitesUpdated, "sitedatamanager:sites-updated");
      Services.obs.addObserver(
        onUpdatingSites,
        "sitedatamanager:updating-sites"
      );

      return () => {
        Services.obs.removeObserver(
          onSitesUpdated,
          "sitedatamanager:sites-updated"
        );
        Services.obs.removeObserver(
          onUpdatingSites,
          "sitedatamanager:updating-sites"
        );
      };
    },
    onUserClick() {
      gSubDialog.open(
        "chrome://browser/content/sanitize_v2.xhtml",
        {
          features: "resizable=no",
        },
        {
          mode: "clearSiteData",
        }
      );
    },
    disabled() {
      return this.isUpdatingSites;
    },
  })
);
Preferences.addSetting(
  /** @type {{ isUpdatingSites: boolean } & SettingConfig} */ ({
    id: "siteDataSettings",
    isUpdatingSites: false,
    setup(emitChange) {
      let onSitesUpdated = async () => {
        this.isUpdatingSites = false;
        emitChange();
      };

      let onUpdatingSites = () => {
        this.isUpdatingSites = true;
        emitChange();
      };

      Services.obs.addObserver(onSitesUpdated, "sitedatamanager:sites-updated");
      Services.obs.addObserver(
        onUpdatingSites,
        "sitedatamanager:updating-sites"
      );

      return () => {
        Services.obs.removeObserver(
          onSitesUpdated,
          "sitedatamanager:sites-updated"
        );
        Services.obs.removeObserver(
          onUpdatingSites,
          "sitedatamanager:updating-sites"
        );
      };
    },
    onUserClick() {
      gSubDialog.open(
        "chrome://browser/content/preferences/dialogs/siteDataSettings.xhtml"
      );
    },
    disabled() {
      return this.isUpdatingSites;
    },
  })
);
Preferences.addSetting({
  id: "cookieExceptions",
  onUserClick() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/permissions.xhtml",
      {},
      {
        blockVisible: true,
        sessionVisible: true,
        allowVisible: true,
        prefilledHost: "",
        permissionType: "cookie",
      }
    );
  },
});

function isCookiesAndStorageClearingOnShutdown() {
  return (
    Preferences.get("privacy.sanitize.sanitizeOnShutdown").value &&
    Preferences.get("privacy.clearOnShutdown_v2.cookiesAndStorage").value &&
    Preferences.get("privacy.clearOnShutdown_v2.cache").value
  );
}

Preferences.addSetting({
  id: "clearOnCloseCookies",
  pref: "privacy.clearOnShutdown_v2.cookiesAndStorage",
});
Preferences.addSetting({
  id: "clearOnCloseCache",
  pref: "privacy.clearOnShutdown_v2.cache",
});
Preferences.addSetting({
  id: "clearOnCloseStorage",
  pref: "privacy.clearOnShutdown_v2.cookiesAndStorage",
});
Preferences.addSetting({
  id: "sanitizeOnShutdown",
  pref: "privacy.sanitize.sanitizeOnShutdown",
});
Preferences.addSetting({
  id: "historyModeCustom",
  pref: "privacy.history.custom",
});
Preferences.addSetting({
  id: "cookieBehavior",
  pref: "network.cookie.cookieBehavior",
});
Preferences.addSetting({
  id: "deleteOnClose",
  deps: [
    "clearOnCloseCookies",
    "clearOnCloseCache",
    "clearOnCloseStorage",
    "sanitizeOnShutdown",
    "privateBrowsingAutoStart",
    "cookieBehavior",
    "alwaysClear",
  ],
  setup() {
    // Make sure to do the migration for the clear history dialog before implementing logic for delete on close
    // This needs to be done to make sure the migration is done before any pref changes are made to avoid unintentionally
    // overwriting prefs
    lazy.Sanitizer.maybeMigratePrefs("clearOnShutdown");
  },
  disabled({ privateBrowsingAutoStart, cookieBehavior }) {
    return (
      privateBrowsingAutoStart.value ||
      cookieBehavior.value == Ci.nsICookieService.BEHAVIOR_REJECT
    );
  },
  get(_, { privateBrowsingAutoStart }) {
    return (
      isCookiesAndStorageClearingOnShutdown() || privateBrowsingAutoStart.value
    );
  },
  set(
    value,
    {
      clearOnCloseCookies,
      clearOnCloseCache,
      clearOnCloseStorage,
      sanitizeOnShutdown,
    }
  ) {
    clearOnCloseCookies.value = value;
    clearOnCloseCache.value = value;
    clearOnCloseStorage.value = value;

    // Sync the cleaning prefs with the deleteOnClose box.

    // Forget the current pref selection if sanitizeOnShutdown is disabled,
    // to not over clear when it gets enabled by the sync mechanism
    if (!sanitizeOnShutdown.value) {
      PrivacySettingHelpers.resetCleaningPrefs();
    }
    // If no other cleaning category is selected, sanitizeOnShutdown gets synced with deleteOnClose
    sanitizeOnShutdown.value =
      PrivacySettingHelpers._isCustomCleaningPrefPresent() || value;
  },
});

Preferences.addSetting({
  id: "historyModeCustom",
  pref: "privacy.history.custom",
});
Preferences.addSetting({
  id: "historyEnabled",
  pref: "places.history.enabled",
});
Preferences.addSetting({
  id: "formFillEnabled",
  pref: "browser.formfill.enable",
});

// Store this on the window so tests can suppress the prompt.
window._shouldPromptForRestartPBM = true;
async function onChangePrivateBrowsingAutoStart(value, revertFn) {
  if (!window._shouldPromptForRestartPBM) {
    return false;
  }

  // The PBM autostart pref has changed so we need to prompt for restart.
  let buttonIndex = await confirmRestartPrompt(value, 1, true, false);

  // User accepts, restart the browser.
  if (buttonIndex == CONFIRM_RESTART_PROMPT_RESTART_NOW) {
    Services.startup.quit(
      Ci.nsIAppStartup.eAttemptQuit | Ci.nsIAppStartup.eRestart
    );
    return false;
  }

  // Don't prompt for the revert operation itself.
  window._shouldPromptForRestartPBM = false;
  revertFn();
  window._shouldPromptForRestartPBM = true;

  // User cancels, do nothing. The caller will clean up the pref change.
  return true;
}

Preferences.addSetting({
  id: "historyMode",
  deps: [
    "historyModeCustom",
    "privateBrowsingAutoStart",
    "historyEnabled",
    "formFillEnabled",
    "sanitizeOnShutdown",
  ],
  get(
    _,
    {
      historyModeCustom,
      privateBrowsingAutoStart,
      historyEnabled,
      formFillEnabled,
      sanitizeOnShutdown,
    }
  ) {
    if (historyModeCustom.value) {
      return "custom";
    }

    if (privateBrowsingAutoStart.value) {
      return "dontremember";
    }

    if (
      historyEnabled.value &&
      formFillEnabled.value &&
      !sanitizeOnShutdown.value
    ) {
      return "remember";
    }

    return "custom";
  },
  set(
    value,
    {
      historyModeCustom,
      privateBrowsingAutoStart,
      historyEnabled,
      formFillEnabled,
      sanitizeOnShutdown,
    },
    setting
  ) {
    let lastHistoryModeCustom = historyModeCustom.value;
    let lastHistoryEnabled = historyEnabled.value;
    let lastFormFillEnabled = formFillEnabled.value;
    let lastSanitizeOnShutdown = sanitizeOnShutdown.value;
    let lastPrivateBrowsingAutoStart = privateBrowsingAutoStart.value;

    let lastValue = setting.value;
    let optionGroupElement = document.activeElement?.parentElement;
    if (optionGroupElement.localName != "moz-radio-group") {
      optionGroupElement = null;
    }
    let cancelFocusElement = optionGroupElement?.childElements.find(
      option => option.value == lastValue
    );

    historyModeCustom.value = value == "custom";

    if (value == "remember") {
      historyEnabled.value = true;
      formFillEnabled.value = true;
      sanitizeOnShutdown.value = false;
      privateBrowsingAutoStart.value = false;
    } else if (value == "dontremember") {
      privateBrowsingAutoStart.value = true;
    }

    if (privateBrowsingAutoStart.value !== lastPrivateBrowsingAutoStart) {
      // The PBM autostart pref has changed so we need to prompt for restart.
      onChangePrivateBrowsingAutoStart(privateBrowsingAutoStart.value, () => {
        // User cancelled the action, revert the change.
        // Simply reverting the setting value itself is not enough, because a
        // state transition to "custom" does not override any of the sub-prefs.
        // We need to update them all manually.
        historyModeCustom.value = lastHistoryModeCustom;
        historyEnabled.value = lastHistoryEnabled;
        formFillEnabled.value = lastFormFillEnabled;
        sanitizeOnShutdown.value = lastSanitizeOnShutdown;
        privateBrowsingAutoStart.value = lastPrivateBrowsingAutoStart;
        if (cancelFocusElement) {
          cancelFocusElement.focus();
        }
      });
    }
  },
  disabled({ privateBrowsingAutoStart }) {
    // Disable history dropdown if PBM autostart is locked on.
    return privateBrowsingAutoStart.locked && privateBrowsingAutoStart.value;
  },
  getControlConfig(config, { privateBrowsingAutoStart }, setting) {
    let l10nId = null;
    if (!srdSectionEnabled("history2")) {
      if (setting.value == "remember") {
        l10nId = "history-remember-description4";
      } else if (setting.value == "dontremember") {
        l10nId = "history-dontremember-description4";
      } else if (setting.value == "custom") {
        l10nId = "history-custom-description4";
      }
    }

    let dontRememberOption = config.options.find(
      opt => opt.value == "dontremember"
    );

    // If PBM is unavailable hide the "Never remember history" option.
    dontRememberOption.hidden = !PrivateBrowsingUtils.enabled;

    // If the PBM autostart pref is locked disable the "Never remember history"
    // option.
    dontRememberOption.disabled =
      privateBrowsingAutoStart.locked && !privateBrowsingAutoStart.value;

    return {
      ...config,
      l10nId,
    };
  },
});

Preferences.addSetting({
  id: "customHistoryButton",
  onUserClick(e) {
    e.preventDefault();
    gotoPref("paneHistory");
  },
});

Preferences.addSetting({
  id: "privateBrowsingAutoStart",
  pref: "browser.privatebrowsing.autostart",
  deps: ["historyMode"],
  onUserChange(value, _, setting) {
    onChangePrivateBrowsingAutoStart(value, () => {
      // User cancelled the action, revert the setting.
      setting.value = !value;
    });
  },
  visible({ historyMode }) {
    return PrivateBrowsingUtils.enabled && historyMode.value == "custom";
  },
});
Preferences.addSetting({
  id: "rememberHistory",
  pref: "places.history.enabled",
  deps: ["historyMode", "privateBrowsingAutoStart"],
  visible({ historyMode }) {
    return historyMode.value == "custom";
  },
  disabled({ privateBrowsingAutoStart }) {
    return privateBrowsingAutoStart.value;
  },
});
Preferences.addSetting({
  id: "rememberForms",
  pref: "browser.formfill.enable",
  deps: ["historyMode", "privateBrowsingAutoStart"],
  visible({ historyMode }) {
    return historyMode.value == "custom";
  },
  disabled({ privateBrowsingAutoStart }) {
    return privateBrowsingAutoStart.value;
  },
});
Preferences.addSetting({
  id: "alwaysClear",
  pref: "privacy.sanitize.sanitizeOnShutdown",
  deps: ["historyMode", "privateBrowsingAutoStart"],
  visible({ historyMode }) {
    return historyMode.value == "custom";
  },
  disabled({ privateBrowsingAutoStart }) {
    return privateBrowsingAutoStart.value;
  },
});

Preferences.addSetting({
  id: "clearDataSettings",
  deps: ["historyMode", "alwaysClear"],
  visible({ historyMode }) {
    return historyMode.value == "custom";
  },
  disabled({ alwaysClear }) {
    return !alwaysClear.value || alwaysClear.disabled;
  },
  onUserClick() {
    gSubDialog.open(
      "chrome://browser/content/sanitize_v2.xhtml",
      {
        features: "resizable=no",
      },
      {
        mode: "clearOnShutdown",
      }
    );
  },
});

Preferences.addSetting({
  id: "clearHistoryButton",
  deps: ["historyMode"],
  onUserClick(_, { historyMode }) {
    PrivacySettingHelpers.clearPrivateDataNow(
      historyMode.value == "dontremember"
    );
  },
});

Preferences.addSetting({
  id: "certificateButtonGroup",
});
Preferences.addSetting({
  id: "disableOpenCertManager",
  pref: "security.disable_button.openCertManager",
});
Preferences.addSetting({
  id: "disableOpenDeviceManager",
  pref: "security.disable_button.openDeviceManager",
});
Preferences.addSetting({
  id: "viewCertificatesButton",
  deps: ["disableOpenCertManager"],
  disabled: deps => {
    return deps.disableOpenCertManager.value;
  },
  onUserClick: () => {
    PrivacySettingHelpers.showCertificates();
  },
});
Preferences.addSetting({
  id: "viewSecurityDevicesButton",
  deps: ["disableOpenDeviceManager"],
  disabled: deps => {
    return deps.disableOpenDeviceManager.value;
  },
  onUserClick: () => {
    PrivacySettingHelpers.showSecurityDevices();
  },
});
Preferences.addSetting({
  id: "certEnableThirdPartyToggle",
  pref: "security.enterprise_roots.enabled",
  visible: () => {
    // Third-party certificate import is only implemented for Windows and Mac,
    // and we should not expose this as a user-configurable setting if there's
    // an enterprise policy controlling it (either to enable _or_ disable it).
    return (
      (lazy.AppConstants.platform == "win" ||
        lazy.AppConstants.platform == "macosx") &&
      typeof Services.policies.getActivePolicies()?.Certificates
        ?.ImportEnterpriseRoots == "undefined"
    );
  },
});

Preferences.addSetting({
  id: "dohBox",
});

Preferences.addSetting({
  id: "dohAdvancedButton",
  onUserClick(e) {
    e.preventDefault();
    gotoPref("paneDnsOverHttps");
  },
});

Preferences.addSetting({
  id: "dohExceptionsButton",
  onUserClick: () => PrivacySettingHelpers.showDoHExceptions(),
});

Preferences.addSetting({
  id: "dohMode",
  pref: "network.trr.mode",
  setup(emitChange) {
    Services.obs.addObserver(emitChange, "network:trr-mode-changed");
    Services.obs.addObserver(emitChange, "network:trr-confirmation");
    return () => {
      Services.obs.removeObserver(emitChange, "network:trr-mode-changed");
      Services.obs.removeObserver(emitChange, "network:trr-confirmation");
    };
  },
});

Preferences.addSetting({
  id: "dohURL",
  pref: "network.trr.uri",
  setup(emitChange) {
    Services.obs.addObserver(emitChange, "network:trr-uri-changed");
    Services.obs.addObserver(emitChange, "network:trr-confirmation");
    return () => {
      Services.obs.removeObserver(emitChange, "network:trr-uri-changed");
      Services.obs.removeObserver(emitChange, "network:trr-confirmation");
    };
  },
});

Preferences.addSetting({
  id: "dohDefaultURL",
  pref: "network.trr.default_provider_uri",
});

Preferences.addSetting({
  id: "dohDisableHeuristics",
  pref: "doh-rollout.disable-heuristics",
});

Preferences.addSetting({
  id: "dohModeBoxItem",
  deps: ["dohMode"],
  getControlConfig: (config, deps) => {
    let l10nId = "preferences-doh-overview-off";
    if (deps.dohMode.value == Ci.nsIDNSService.MODE_NATIVEONLY) {
      l10nId = "preferences-doh-overview-default";
    } else if (
      deps.dohMode.value == Ci.nsIDNSService.MODE_TRRFIRST ||
      deps.dohMode.value == Ci.nsIDNSService.MODE_TRRONLY
    ) {
      l10nId = "preferences-doh-overview-custom";
    }
    return {
      ...config,
      l10nId,
    };
  },
});

Preferences.addSetting({
  id: "dohStatusBox",
  deps: ["dohMode", "dohURL"],
  getControlConfig: config => {
    let l10nId = "preferences-doh-status-item-off";
    let l10nArgs = {};
    let supportPage = "";
    let controlAttrs = { type: "info" };

    let trrURI = Services.dns.currentTrrURI;
    let hostname = URL.parse(trrURI)?.hostname;

    let displayName = hostname || trrURI;
    let nameFound = false;
    let steering = false;
    for (let resolver of lazy.DoHConfigController.currentConfig.providerList) {
      if (resolver.uri == trrURI) {
        displayName = resolver.UIName || displayName;
        nameFound = true;
        break;
      }
    }
    if (!nameFound) {
      for (let resolver of lazy.DoHConfigController.currentConfig
        .providerSteering.providerList) {
        if (resolver.uri == trrURI) {
          steering = true;
          displayName = resolver.UIName || displayName;
          break;
        }
      }
    }

    let mode = Services.dns.currentTrrMode;
    if (
      (mode == Ci.nsIDNSService.MODE_TRRFIRST ||
        mode == Ci.nsIDNSService.MODE_TRRONLY) &&
      lazy.gParentalControlsService?.parentalControlsEnabled
    ) {
      l10nId = "preferences-doh-status-item-not-active";
      supportPage = "doh-status";
      l10nArgs = {
        reason: Services.dns.getTRRSkipReasonName(
          Ci.nsITRRSkipReason.TRR_PARENTAL_CONTROL
        ),
        displayName,
      };
    } else {
      let confirmationState = Services.dns.currentTrrConfirmationState;
      if (
        mode != Ci.nsIDNSService.MODE_TRRFIRST &&
        mode != Ci.nsIDNSService.MODE_TRRONLY
      ) {
        l10nId = "preferences-doh-status-item-off";
      } else if (
        confirmationState == Ci.nsIDNSService.CONFIRM_TRYING_OK ||
        confirmationState == Ci.nsIDNSService.CONFIRM_OK ||
        confirmationState == Ci.nsIDNSService.CONFIRM_DISABLED
      ) {
        if (steering) {
          l10nId = "preferences-doh-status-item-active-local";
          controlAttrs = { type: "success" };
        } else {
          l10nId = "preferences-doh-status-item-active";
          controlAttrs = { type: "success" };
        }
      } else if (steering) {
        l10nId = "preferences-doh-status-item-not-active-local";
        supportPage = "doh-status";
        controlAttrs = { type: "warning" };
      } else {
        l10nId = "preferences-doh-status-item-not-active";
        supportPage = "doh-status";
        controlAttrs = { type: "warning" };
      }

      let confirmationStatus = Services.dns.lastConfirmationStatus;
      if (confirmationStatus != Cr.NS_OK) {
        l10nArgs = {
          reason: ChromeUtils.getXPCOMErrorName(confirmationStatus),
          name,
        };
      } else {
        l10nArgs = {
          reason: Services.dns.getTRRSkipReasonName(
            Services.dns.lastConfirmationSkipReason
          ),
          name,
        };
        if (
          Services.dns.lastConfirmationSkipReason ==
            Ci.nsITRRSkipReason.TRR_BAD_URL ||
          !name
        ) {
          l10nId = "preferences-doh-status-item-not-active-bad-url";
          supportPage = "doh-status";
          controlAttrs = { type: "warning" };
        }
      }
    }

    return {
      ...config,
      l10nId,
      l10nArgs,
      supportPage,
      controlAttrs,
    };
  },
});

Preferences.addSetting({
  id: "dohRadioGroup",
  // These deps are complicated:
  // this radio group, along with dohFallbackIfCustom controls the mode and URL.
  // Therefore, we set dohMode and dohURL as deps here. This is a smell, but needed
  // for the mismatch of control-to-pref.
  deps: ["dohFallbackIfCustom", "dohMode", "dohURL"],
  onUserChange: (val, deps) => {
    let value = null;
    if (val == "default") {
      value = "dohDefaultRadio";
    } else if (val == "off") {
      value = "dohOffRadio";
    } else if (val == "custom" && deps.dohFallbackIfCustom.value) {
      value = "dohEnabledRadio";
    } else if (val == "custom" && !deps.dohFallbackIfCustom.value) {
      value = "dohStrictRadio";
    }
    if (value) {
      Glean.securityDohSettings.modeChangedButton.record({
        value,
      });
    }
  },
  get: (_val, deps) => {
    switch (deps.dohMode.value) {
      case Ci.nsIDNSService.MODE_NATIVEONLY:
        return "default";
      case Ci.nsIDNSService.MODE_TRRFIRST:
      case Ci.nsIDNSService.MODE_TRRONLY:
        return "custom";
      case Ci.nsIDNSService.MODE_TRROFF:
      case Ci.nsIDNSService.MODE_RESERVED1:
      case Ci.nsIDNSService.MODE_RESERVED4:
      default:
        return "off";
    }
  },
  set: (val, deps) => {
    if (val == "custom") {
      if (deps.dohFallbackIfCustom.value) {
        deps.dohMode.value = Ci.nsIDNSService.MODE_TRRFIRST;
      } else {
        deps.dohMode.value = Ci.nsIDNSService.MODE_TRRONLY;
      }
    } else if (val == "off") {
      deps.dohMode.value = Ci.nsIDNSService.MODE_TRROFF;
    } else {
      deps.dohMode.value = Ci.nsIDNSService.MODE_NATIVEONLY;
    }

    // When the mode is set to 0 we need to clear the URI so
    // doh-rollout can kick in.
    if (deps.dohMode.value == Ci.nsIDNSService.MODE_NATIVEONLY) {
      deps.dohURL.pref.value = undefined;
      Services.prefs.clearUserPref("doh-rollout.disable-heuristics");
    }

    // Bug 1861285
    // When the mode is set to 2 or 3, we need to check if network.trr.uri is a empty string.
    // In this case, we need to update network.trr.uri to default to fallbackProviderURI.
    // This occurs when the mode is previously set to 0 (Default Protection).
    if (
      deps.dohMode.value == Ci.nsIDNSService.MODE_TRRFIRST ||
      deps.dohMode.value == Ci.nsIDNSService.MODE_TRRONLY
    ) {
      if (!deps.dohURL.value) {
        deps.dohURL.value =
          lazy.DoHConfigController.currentConfig.fallbackProviderURI;
      }
    }

    // Bug 1900672
    // When the mode is set to 5, clear the pref to ensure that
    // network.trr.uri is set to fallbackProviderURIwhen the mode is set to 2 or 3 afterwards
    if (deps.dohMode.value == Ci.nsIDNSService.MODE_TRROFF) {
      deps.dohURL.pref.value = undefined;
    }
  },
});

Preferences.addSetting({
  id: "dohFallbackIfCustom",
  pref: "network.trr_ui.fallback_was_checked",
  // These deps are complicated:
  // this checkbox, along with dohRadioGroup controls the mode and URL.
  // Therefore, we set dohMode as a dep here. This is a smell, but needed
  // for the mismatch of control-to-pref.
  deps: ["dohMode"],
  onUserChange: val => {
    if (val) {
      Glean.securityDohSettings.modeChangedButton.record({
        value: "dohEnabledRadio",
      });
    } else {
      Glean.securityDohSettings.modeChangedButton.record({
        value: "dohStrictRadio",
      });
    }
  },
  get: (val, deps) => {
    // If we are in a custom mode, we need to get the value from the Setting
    if (deps.dohMode.value == Ci.nsIDNSService.MODE_TRRFIRST) {
      return true;
    }
    if (deps.dohMode.value == Ci.nsIDNSService.MODE_TRRONLY) {
      return false;
    }

    // Propagate the preference otherwise
    return val;
  },
  set: (val, deps) => {
    // Toggle the preference that controls the setting if are in a custom mode
    // This should be the only case where the checkbox is enabled, but we can be
    // careful and test.
    if (deps.dohMode.value == Ci.nsIDNSService.MODE_TRRFIRST && !val) {
      deps.dohMode.value = Ci.nsIDNSService.MODE_TRRONLY;
    } else if (deps.dohMode.value == Ci.nsIDNSService.MODE_TRRONLY && val) {
      deps.dohMode.value = Ci.nsIDNSService.MODE_TRRFIRST;
    }
    // Propagate to the real preference
    return val;
  },
});

Preferences.addSetting({
  id: "dohCustomProvider",
  deps: ["dohProviderSelect", "dohURL"],
  _value: null,
  visible: deps => {
    return deps.dohProviderSelect.value == "custom";
  },
  get(_val, deps) {
    if (this._value === null) {
      return deps.dohURL.value;
    }
    return this._value;
  },
  set(val, deps) {
    this._value = val;
    if (val == "") {
      val = " ";
    }
    deps.dohURL.value = val;
  },
});

Preferences.addSetting({
  id: "dohProviderSelect",
  deps: ["dohURL", "dohDefaultURL"],
  _custom: false,
  onUserChange: value => {
    Glean.securityDohSettings.providerChoiceValue.record({
      value,
    });
  },
  getControlConfig(config, deps) {
    let options = [];

    let resolvers = lazy.DoHConfigController.currentConfig.providerList;
    // if there's no default, we'll hold its position with an empty string
    let defaultURI = lazy.DoHConfigController.currentConfig.fallbackProviderURI;
    let defaultFound = resolvers.some(p => p.uri == defaultURI);
    if (!defaultFound && defaultURI) {
      // the default value for the pref isn't included in the resolvers list
      // so we'll make a stub for it. Without an id, we'll have to use the url as the label
      resolvers.unshift({ uri: defaultURI });
    }
    let currentURI = deps.dohURL.value;
    if (currentURI && !resolvers.some(p => p.uri == currentURI)) {
      this._custom = true;
    }

    options = resolvers.map(resolver => {
      let option = {
        value: resolver.uri,
        l10nArgs: {
          name: resolver.UIName || resolver.uri,
        },
      };
      if (resolver.uri == defaultURI) {
        option.l10nId = "connection-dns-over-https-url-item-default";
      } else {
        option.l10nId = "connection-dns-over-https-url-item";
      }
      return option;
    });
    options.push({
      value: "custom",
      l10nId: "connection-dns-over-https-url-custom",
    });

    return {
      options,
      ...config,
    };
  },
  get(_val, deps) {
    if (this._custom) {
      return "custom";
    }
    let currentURI = deps.dohURL.value;
    if (!currentURI) {
      currentURI = deps.dohDefaultURL.value;
    }
    return currentURI;
  },
  set(val, deps, setting) {
    if (val != "custom") {
      this._custom = false;
      deps.dohURL.value = val;
    } else {
      this._custom = true;
    }
    setting.emit("change");
    return val;
  },
});

Preferences.addSetting({
  id: "connectionLinkButton",
  onUserClick(e) {
    e.preventDefault();
    gotoPref("paneConnectionSecurity");
  },
});

Preferences.addSetting({
  id: "contentBlockingCategory",
  pref: "browser.contentblocking.category",
});

// We need a separate setting for the radio group for custom disable behavior.
// Setter and getter simply write to the pref.
Preferences.addSetting({
  id: "contentBlockingCategoryRadioGroup",
  deps: ["contentBlockingCategory"],
  get(_, { contentBlockingCategory }) {
    return contentBlockingCategory.value;
  },
  set(value, { contentBlockingCategory }) {
    contentBlockingCategory.value = value;
  },
  getControlConfig(config, _, setting) {
    if (!PrivacySettingHelpers.shouldDisableETPCategoryControls()) {
      return config;
    }

    let { options } = config;

    // If ETP level is set to custom keep the radio button enabled so the "customize" button works even when the category selection itself is locked.
    for (let option of options) {
      option.disabled =
        option.id != "etpLevelCustom" || setting.value != "custom";
    }

    return config;
  },
});

Preferences.addSetting({
  id: "etpStatusBoxGroup",
});

Preferences.addSetting({
  id: "etpStatusItem",
  deps: ["contentBlockingCategory"],
  getControlConfig(config, { contentBlockingCategory }) {
    // Display a different description and label depending on the content blocking category (= ETP level).
    let categoryToL10nId = {
      standard: "preferences-etp-level-standard",
      strict: "preferences-etp-level-strict",
      custom: "preferences-etp-level-custom",
    };

    return {
      ...config,
      l10nId:
        categoryToL10nId[contentBlockingCategory.value] ??
        "preferences-etp-level-standard",
    };
  },
});

Preferences.addSetting({
  id: "etpStatusAdvancedButton",
  onUserClick(e) {
    e.preventDefault();
    gotoPref("etp");
  },
});

Preferences.addSetting({
  id: "protectionsDashboardLink",
});

Preferences.addSetting({
  id: "etpBannerEl",
});

Preferences.addSetting({
  id: "etpAllowListBaselineEnabled",
  pref: "privacy.trackingprotection.allow_list.baseline.enabled",
  deps: ["contentBlockingCategory"],
  visible({ contentBlockingCategory }) {
    return contentBlockingCategory.value == "strict";
  },
  onUserChange(value, _deps, setting) {
    PrivacySettingHelpers.onBaselineAllowListSettingChange(value, setting);
  },
});

Preferences.addSetting({
  id: "etpAllowListConvenienceEnabled",
  pref: "privacy.trackingprotection.allow_list.convenience.enabled",
  onUserChange() {
    PrivacySettingHelpers.maybeNotifyUserToReload();
  },
});

Preferences.addSetting({
  id: "etpCustomizeButton",
  onUserClick(e) {
    e.preventDefault();
    gotoPref("etpCustomize");
  },
});

Preferences.addSetting({
  id: "reloadTabsHint",
  _showHint: false,
  set(value, _, setting) {
    this._showHint = value;
    setting.emit("change");
  },
  get() {
    return this._showHint;
  },
  visible(_, setting) {
    return setting.value;
  },
  onUserClick() {
    PrivacySettingHelpers.reloadAllOtherTabs();
  },
});

Preferences.addSetting({
  id: "resistFingerprinting",
  pref: "privacy.resistFingerprinting",
});

Preferences.addSetting({
  id: "resistFingerprintingPBM",
  pref: "privacy.resistFingerprinting.pbmode",
});

Preferences.addSetting({
  id: "rfpWarning",
  deps: ["resistFingerprinting", "resistFingerprintingPBM"],
  visible({ resistFingerprinting, resistFingerprintingPBM }) {
    return resistFingerprinting.value || resistFingerprintingPBM.value;
  },
});

Preferences.addSetting({
  id: "etpLevelWarning",
  deps: ["contentBlockingCategory"],
  visible({ contentBlockingCategory }) {
    return contentBlockingCategory.value != "standard";
  },
});

Preferences.addSetting({
  id: "etpManageExceptionsButton",
  onUserClick() {
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
});

Preferences.addSetting({
  id: "etpResetButtonGroup",
});

Preferences.addSetting({
  id: "etpResetStandardButton",
  deps: ["contentBlockingCategory"],
  onUserClick(_, { contentBlockingCategory }) {
    contentBlockingCategory.value = "standard";
  },
  disabled({ contentBlockingCategory }) {
    return (
      contentBlockingCategory.value == "standard" ||
      PrivacySettingHelpers.shouldDisableETPCategoryControls()
    );
  },
});

Preferences.addSetting({
  id: "etpResetStrictButton",
  deps: ["contentBlockingCategory"],
  onUserClick(_, { contentBlockingCategory }) {
    contentBlockingCategory.value = "strict";
  },
  disabled({ contentBlockingCategory }) {
    return (
      contentBlockingCategory.value == "strict" ||
      PrivacySettingHelpers.shouldDisableETPCategoryControls()
    );
  },
});

Preferences.addSetting({
  id: "etpAllowListBaselineEnabledCustom",
  pref: "privacy.trackingprotection.allow_list.baseline.enabled",
  onUserChange(value, _deps, setting) {
    PrivacySettingHelpers.onBaselineAllowListSettingChange(value, setting);
  },
});

Preferences.addSetting({
  id: "etpAllowListConvenienceEnabledCustom",
  pref: "privacy.trackingprotection.allow_list.convenience.enabled",
  onUserChange() {
    PrivacySettingHelpers.maybeNotifyUserToReload();
  },
});

Preferences.addSetting({
  id: "etpCustomCookiesEnabled",
  deps: ["cookieBehavior"],
  disabled: ({ cookieBehavior }) => {
    return cookieBehavior.locked;
  },
  get(_, { cookieBehavior }) {
    return cookieBehavior.value != Ci.nsICookieService.BEHAVIOR_ACCEPT;
  },
  set(value, { cookieBehavior }) {
    if (!value) {
      cookieBehavior.value = Ci.nsICookieService.BEHAVIOR_ACCEPT;
    } else {
      // When the user enabled cookie blocking, set the cookie behavior to the default.
      cookieBehavior.value = cookieBehavior.pref.defaultValue;
    }
  },
});

Preferences.addSetting({
  id: "trackingProtectionEnabled",
  pref: "privacy.trackingprotection.enabled",
});

Preferences.addSetting({
  id: "trackingProtectionEnabledPBM",
  pref: "privacy.trackingprotection.pbmode.enabled",
});

Preferences.addSetting({
  id: "etpCustomTrackingProtectionEnabledContext",
  deps: ["trackingProtectionEnabled", "trackingProtectionEnabledPBM"],
  get(_, { trackingProtectionEnabled, trackingProtectionEnabledPBM }) {
    if (trackingProtectionEnabled.value && trackingProtectionEnabledPBM.value) {
      return "all";
    } else if (trackingProtectionEnabledPBM.value) {
      return "pbmOnly";
    }
    return null;
  },
  set(value, { trackingProtectionEnabled, trackingProtectionEnabledPBM }) {
    if (value == "all") {
      trackingProtectionEnabled.value = true;
      trackingProtectionEnabledPBM.value = true;
    } else if (value == "pbmOnly") {
      trackingProtectionEnabled.value = false;
      trackingProtectionEnabledPBM.value = true;
    }
  },
});

Preferences.addSetting({
  id: "etpCustomTrackingProtectionEnabled",
  deps: ["trackingProtectionEnabled", "trackingProtectionEnabledPBM"],
  disabled: ({ trackingProtectionEnabled, trackingProtectionEnabledPBM }) => {
    return (
      trackingProtectionEnabled.locked || trackingProtectionEnabledPBM.locked
    );
  },
  get(_, { trackingProtectionEnabled, trackingProtectionEnabledPBM }) {
    return (
      trackingProtectionEnabled.value || trackingProtectionEnabledPBM.value
    );
  },
  set(value, { trackingProtectionEnabled, trackingProtectionEnabledPBM }) {
    if (value) {
      trackingProtectionEnabled.value = false;
      trackingProtectionEnabledPBM.value = true;
    } else {
      trackingProtectionEnabled.value = false;
      trackingProtectionEnabledPBM.value = false;
    }
  },
});

Preferences.addSetting({
  id: "etpCustomCryptominingProtectionEnabled",
  pref: "privacy.trackingprotection.cryptomining.enabled",
});

Preferences.addSetting({
  id: "etpCustomKnownFingerprintingProtectionEnabled",
  pref: "privacy.trackingprotection.fingerprinting.enabled",
});

Preferences.addSetting({
  id: "etpCustomFingerprintingProtectionEnabled",
  pref: "privacy.fingerprintingProtection",
});

Preferences.addSetting({
  id: "etpCustomFingerprintingProtectionEnabledPBM",
  pref: "privacy.fingerprintingProtection.pbmode",
});

Preferences.addSetting({
  id: "etpCustomSuspectFingerprintingProtectionEnabled",
  deps: [
    "etpCustomFingerprintingProtectionEnabled",
    "etpCustomFingerprintingProtectionEnabledPBM",
  ],
  disabled({
    etpCustomFingerprintingProtectionEnabled,
    etpCustomFingerprintingProtectionEnabledPBM,
  }) {
    return (
      etpCustomFingerprintingProtectionEnabled.locked ||
      etpCustomFingerprintingProtectionEnabledPBM.locked
    );
  },
  get(
    _,
    {
      etpCustomFingerprintingProtectionEnabled,
      etpCustomFingerprintingProtectionEnabledPBM,
    }
  ) {
    return (
      etpCustomFingerprintingProtectionEnabled.value ||
      etpCustomFingerprintingProtectionEnabledPBM.value
    );
  },
  set(
    value,
    {
      etpCustomFingerprintingProtectionEnabled,
      etpCustomFingerprintingProtectionEnabledPBM,
    }
  ) {
    if (value) {
      etpCustomFingerprintingProtectionEnabled.value = false;
      etpCustomFingerprintingProtectionEnabledPBM.value = true;
    } else {
      etpCustomFingerprintingProtectionEnabled.value = false;
      etpCustomFingerprintingProtectionEnabledPBM.value = false;
    }
  },
});

Preferences.addSetting({
  id: "etpCustomSuspectFingerprintingProtectionEnabledContext",
  deps: [
    "etpCustomFingerprintingProtectionEnabled",
    "etpCustomFingerprintingProtectionEnabledPBM",
  ],
  get(
    _,
    {
      etpCustomFingerprintingProtectionEnabled,
      etpCustomFingerprintingProtectionEnabledPBM,
    }
  ) {
    if (
      etpCustomFingerprintingProtectionEnabled.value &&
      etpCustomFingerprintingProtectionEnabledPBM.value
    ) {
      return "all";
    } else if (etpCustomFingerprintingProtectionEnabledPBM.value) {
      return "pbmOnly";
    }
    return null;
  },
  set(
    value,
    {
      etpCustomFingerprintingProtectionEnabled,
      etpCustomFingerprintingProtectionEnabledPBM,
    }
  ) {
    if (value == "all") {
      etpCustomFingerprintingProtectionEnabled.value = true;
      etpCustomFingerprintingProtectionEnabledPBM.value = true;
    } else if (value == "pbmOnly") {
      etpCustomFingerprintingProtectionEnabled.value = false;
      etpCustomFingerprintingProtectionEnabledPBM.value = true;
    }
  },
});
