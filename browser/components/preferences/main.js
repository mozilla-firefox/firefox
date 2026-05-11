/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import MozBoxItem from 'moz-src:///toolkit/content/widgets/moz-box-item/moz-box-item.mjs';*/
/** @import { MozOption } from 'moz-src:///toolkit/content/widgets/moz-select/moz-select.mjs';*/
/** @import MozSelect from 'moz-src:///toolkit/content/widgets/moz-select/moz-select.mjs';*/
/** @import MozBoxGroup from 'chrome://global/content/elements/moz-box-group.mjs'; */
/** @import { AsyncSettingHandler } from 'chrome://global/content/preferences/AsyncSetting.mjs'; */
/** @import { HandlerInfoWrapper, ApplicationListItem } from './config/downloads.mjs';*/

/* import-globals-from extensionControlled.js */
/* import-globals-from preferences.js */
/* import-globals-from /toolkit/mozapps/preferences/fontbuilder.js */
/* import-globals-from /browser/base/content/aboutDialog-appUpdater.js */
/* global MozXULElement */

/**
 * @import { Setting } from "chrome://global/content/preferences/Setting.mjs"
 */

const { Multilingual } = ChromeUtils.importESModule(
  "chrome://browser/content/preferences/config/languages.mjs",
  { global: "current" }
);

ChromeUtils.defineESModuleGetters(this, {
  BackgroundUpdate: "resource://gre/modules/BackgroundUpdate.sys.mjs",
  UpdateListener: "resource://gre/modules/UpdateListener.sys.mjs",
  LinkPreview: "moz-src:///browser/components/genai/LinkPreview.sys.mjs",
  MigrationUtils: "resource:///modules/MigrationUtils.sys.mjs",
  TranslationsParent: "resource://gre/actors/TranslationsParent.sys.mjs",
  TranslationsUtils:
    "chrome://global/content/translations/TranslationsUtils.mjs",
  WindowsLaunchOnLogin: "resource://gre/modules/WindowsLaunchOnLogin.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  FormAutofillPreferences:
    "resource://autofill/FormAutofillPreferences.sys.mjs",
});

ChromeUtils.importESModule(
  "chrome://browser/content/preferences/config/accessibility.mjs",
  { global: "current" }
);
ChromeUtils.importESModule(
  "chrome://browser/content/preferences/config/about-firefox.mjs",
  { global: "current" }
);

ChromeUtils.importESModule(
  "chrome://browser/content/preferences/config/appearance.mjs",
  { global: "current" }
);

ChromeUtils.importESModule(
  "chrome://browser/content/preferences/config/tabs-browsing.mjs",
  { global: "current" }
);

// Constants & Enumeration Values
const TYPE_PDF = "application/pdf";

const PREF_PDFJS_DISABLED = "pdfjs.disabled";

// Pref for when containers is being controlled
const PREF_CONTAINERS_EXTENSION = "privacy.userContext.extension";

// Strings to identify ExtensionSettingsStore overrides
const CONTAINERS_KEY = "privacy.containers";

const ICON_URL_APP =
  AppConstants.platform == "linux"
    ? "moz-icon://dummy.exe?size=16"
    : "chrome://browser/skin/preferences/application.png";

// For CSS. Can be one of "ask", "save" or "handleInternally". If absent, the icon URL
// was set by us to a custom handler icon and CSS should not try to override it.
const APP_ICON_ATTR_NAME = "appHandlerIcon";

const OPEN_EXTERNAL_LINK_NEXT_TO_ACTIVE_TAB_VALUE =
  Ci.nsIBrowserDOMWindow.OPEN_NEWTAB_AFTER_CURRENT;

/**
 * @param {Setting} featureSetting
 * @param {Setting} defaultSetting
 */
function canShowAiFeature(featureSetting, defaultSetting) {
  return (
    featureSetting.value != "blocked" &&
    !(featureSetting.value == "default" && defaultSetting.value == "blocked")
  );
}

Preferences.addAll([
  // Startup
  { id: "browser.startup.page", type: "int" },
  { id: "browser.startup.windowsLaunchOnLogin.enabled", type: "bool" },
  { id: "browser.privatebrowsing.autostart", type: "bool" },

  // AI Controls, these pref values can affect settings on the main pane and
  // have base Settings here
  { id: "browser.ai.control.default", type: "string" },
  { id: "browser.ai.control.translations", type: "string" },
  { id: "browser.ai.control.pdfjsAltText", type: "string" },
  { id: "browser.ai.control.smartTabGroups", type: "string" },
  { id: "browser.ai.control.linkPreviewKeyPoints", type: "string" },
  { id: "browser.ai.control.sidebarChatbot", type: "string" },
  { id: "browser.ai.control.smartWindow", type: "string" },

  // Update
  { id: "browser.preferences.advanced.selectedTabIndex", type: "int" },
  { id: "browser.search.update", type: "bool" },

  {
    id: "privacy.userContext.newTabContainerOnLeftClick.enabled",
    type: "bool",
  },
]);

if (AppConstants.HAVE_SHELL_SERVICE) {
  Preferences.addAll([
    { id: "browser.shell.checkDefaultBrowser", type: "bool" },
    { id: "pref.general.disable_button.default_browser", type: "bool" },
  ]);
}

Preferences.addSetting({
  id: "privateBrowsingAutoStart",
  pref: "browser.privatebrowsing.autostart",
});

Preferences.addSetting(
  /** @type {{ _getLaunchOnLoginApprovedCachedValue: boolean } & SettingConfig} */ ({
    id: "launchOnLoginApproved",
    _getLaunchOnLoginApprovedCachedValue: true,
    get() {
      return this._getLaunchOnLoginApprovedCachedValue;
    },
    // Check for a launch on login registry key
    // This accounts for if a user manually changes it in the registry
    // Disabling in Task Manager works outside of just deleting the registry key
    // in HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run
    // but it is not possible to change it back to enabled as the disabled value is just a random
    // hexadecimal number
    setup() {
      if (AppConstants.platform !== "win") {
        /**
         * WindowsLaunchOnLogin isnt available if not on windows
         * but this setup function still fires, so must prevent
         * WindowsLaunchOnLogin.getLaunchOnLoginApproved
         * below from executing unnecessarily.
         */
        return;
      }
      // @ts-ignore bug 1996860
      WindowsLaunchOnLogin.getLaunchOnLoginApproved().then(val => {
        this._getLaunchOnLoginApprovedCachedValue = val;
      });
    },
  })
);

Preferences.addSetting({
  id: "windowsLaunchOnLoginEnabled",
  pref: "browser.startup.windowsLaunchOnLogin.enabled",
});

Preferences.addSetting(
  /** @type {{_getLaunchOnLoginEnabledValue: boolean, startWithLastProfile: boolean} & SettingConfig} */ ({
    id: "windowsLaunchOnLogin",
    deps: ["launchOnLoginApproved", "windowsLaunchOnLoginEnabled"],
    _getLaunchOnLoginEnabledValue: false,
    get startWithLastProfile() {
      return Cc["@mozilla.org/toolkit/profile-service;1"].getService(
        Ci.nsIToolkitProfileService
      ).startWithLastProfile;
    },
    get() {
      return this._getLaunchOnLoginEnabledValue;
    },
    setup(emitChange) {
      if (AppConstants.platform !== "win") {
        /**
         * WindowsLaunchOnLogin isnt available if not on windows
         * but this setup function still fires, so must prevent
         * WindowsLaunchOnLogin.getLaunchOnLoginEnabled
         * below from executing unnecessarily.
         */
        return;
      }

      /** @type {boolean} */
      let getLaunchOnLoginEnabledValue;
      let maybeEmitChange = () => {
        if (
          getLaunchOnLoginEnabledValue !== this._getLaunchOnLoginEnabledValue
        ) {
          this._getLaunchOnLoginEnabledValue = getLaunchOnLoginEnabledValue;
          emitChange();
        }
      };
      if (!this.startWithLastProfile) {
        getLaunchOnLoginEnabledValue = false;
        maybeEmitChange();
      } else {
        // @ts-ignore bug 1996860
        WindowsLaunchOnLogin.getLaunchOnLoginEnabled().then(val => {
          getLaunchOnLoginEnabledValue = val;
          maybeEmitChange();
        });
      }
    },
    visible: ({ windowsLaunchOnLoginEnabled }) => {
      let isVisible =
        AppConstants.platform === "win" && windowsLaunchOnLoginEnabled.value;
      if (isVisible) {
        // @ts-ignore bug 1996860
        NimbusFeatures.windowsLaunchOnLogin.recordExposureEvent({
          once: true,
        });
      }
      return isVisible;
    },
    disabled({ launchOnLoginApproved }) {
      return !this.startWithLastProfile || !launchOnLoginApproved.value;
    },
    onUserChange(checked) {
      Glean.launchOnLogin.userToggle.record({ enabled: checked });
      if (checked) {
        // windowsLaunchOnLogin has been checked: create registry key or shortcut
        // The shortcut is created with the same AUMID as Firefox itself. However,
        // this is not set during browser tests and the fallback of checking the
        // registry fails. As such we pass an arbitrary AUMID for the purpose
        // of testing.
        // @ts-ignore bug 1996860
        WindowsLaunchOnLogin.createLaunchOnLogin();
        Services.prefs.setBoolPref(
          "browser.startup.windowsLaunchOnLogin.disableLaunchOnLoginPrompt",
          true
        );
      } else {
        // windowsLaunchOnLogin has been unchecked: delete registry key and shortcut
        // @ts-ignore bug 1996860
        WindowsLaunchOnLogin.removeLaunchOnLogin();
      }
    },
  })
);

Preferences.addSetting({
  id: "windowsLaunchOnLoginDisabledProfileBox",
  deps: ["windowsLaunchOnLoginEnabled"],
  visible: ({ windowsLaunchOnLoginEnabled }) => {
    if (AppConstants.platform !== "win") {
      return false;
    }
    let startWithLastProfile = Cc[
      "@mozilla.org/toolkit/profile-service;1"
    ].getService(Ci.nsIToolkitProfileService).startWithLastProfile;

    return !startWithLastProfile && windowsLaunchOnLoginEnabled.value;
  },
});

Preferences.addSetting({
  id: "windowsLaunchOnLoginDisabledBox",
  deps: ["launchOnLoginApproved", "windowsLaunchOnLoginEnabled"],
  visible: ({ launchOnLoginApproved, windowsLaunchOnLoginEnabled }) => {
    if (AppConstants.platform !== "win") {
      return false;
    }
    let startWithLastProfile = Cc[
      "@mozilla.org/toolkit/profile-service;1"
    ].getService(Ci.nsIToolkitProfileService).startWithLastProfile;

    return (
      startWithLastProfile &&
      !launchOnLoginApproved.value &&
      windowsLaunchOnLoginEnabled.value
    );
  },
});

Preferences.addSetting({
  /**
   * The "Open previous windows and tabs" option on about:preferences page.
   */
  id: "browserRestoreSession",
  pref: "browser.startup.page",
  deps: ["privateBrowsingAutoStart"],
  get:
    /**
     * Returns the value of the "Open previous windows and tabs" option based
     * on the value of the browser.privatebrowsing.autostart pref.
     *
     * @param {number | undefined} value
     * @returns {boolean}
     */
    value => {
      const pbAutoStartPref = Preferences.get(
        "browser.privatebrowsing.autostart"
      );
      let newValue = pbAutoStartPref.value
        ? false
        : value === gMainPane.STARTUP_PREF_RESTORE_SESSION;

      return newValue;
    },
  set: checked => {
    const startupPref = Preferences.get("browser.startup.page");
    let newValue;

    if (checked) {
      // We need to restore the blank homepage setting in our other pref
      if (startupPref.value === gMainPane.STARTUP_PREF_BLANK) {
        // @ts-ignore bug 1996860
        HomePage.safeSet("about:blank");
      }
      newValue = gMainPane.STARTUP_PREF_RESTORE_SESSION;
    } else {
      newValue = gMainPane.STARTUP_PREF_HOMEPAGE;
    }
    return newValue;
  },
  disabled: deps => {
    return deps.privateBrowsingAutoStart.value;
  },
});

Preferences.addSetting({
  id: "containersPane",
  onUserClick(e) {
    e.preventDefault();
    gotoPref("paneContainers2");
  },
});
Preferences.addSetting({ id: "containersPlaceholder" });
Preferences.addSetting({
  id: "legacyTranslationsVisible",
  deps: ["aiControlDefault", "aiControlTranslations"],
  visible: ({ aiControlDefault, aiControlTranslations }) =>
    !Services.prefs.getBoolPref("browser.settings-redesign.enabled", false) &&
    canShowAiFeature(aiControlTranslations, aiControlDefault),
});

Preferences.addSetting({
  id: "connectionSettings",
  onUserClick: () => gMainPane.showConnections(),
  controllingExtensionInfo: {
    storeId: PROXY_KEY,
    l10nId: "extension-controlling-proxy-config",
    allowControl: true,
  },
});

/**
 * A helper object containing all logic related to
 * setting the browser as the user's default.
 */
const DefaultBrowserHelper = {
  /**
   * @type {number}
   */
  _backoffIndex: 0,

  /**
   * @type {number | undefined}
   */
  _pollingTimer: undefined,

  /**
   * Keeps track of the last known browser
   * default value set to compare while polling.
   *
   * @type {boolean | undefined}
   */
  _lastPolledIsDefault: undefined,

  /**
   * @type {typeof import('../shell/ShellService.sys.mjs').ShellService | undefined}
   */
  get shellSvc() {
    return (
      AppConstants.HAVE_SHELL_SERVICE &&
      // @ts-ignore from utilityOverlay.js
      getShellService()
    );
  },

  /**
   * Sets up polling of whether the browser is set to default,
   * and calls provided hasChanged function when the state changes.
   *
   * @param {Function} hasChanged
   */
  pollForDefaultChanges(hasChanged) {
    if (this._pollingTimer) {
      return;
    }
    this._lastPolledIsDefault = this.isBrowserDefault;

    // Exponential backoff mechanism will delay the polling times if user doesn't
    // trigger SetDefaultBrowser for a long time.
    const backoffTimes = [
      1000, 1000, 1000, 1000, 2000, 2000, 2000, 5000, 5000, 10000,
    ];

    const pollForDefaultBrowser = () => {
      if (
        (location.hash == "" ||
          location.hash == "#general" ||
          location.hash == "#sync") &&
        document.visibilityState == "visible"
      ) {
        const { isBrowserDefault } = this;
        if (isBrowserDefault !== this._lastPolledIsDefault) {
          this._lastPolledIsDefault = isBrowserDefault;
          hasChanged();
        }
      }

      if (!this._pollingTimer) {
        return;
      }

      // approximately a "requestIdleInterval"
      this._pollingTimer = window.setTimeout(
        () => {
          window.requestIdleCallback(pollForDefaultBrowser);
        },
        backoffTimes[
          this._backoffIndex + 1 < backoffTimes.length
            ? this._backoffIndex++
            : backoffTimes.length - 1
        ]
      );
    };

    this._pollingTimer = window.setTimeout(() => {
      window.requestIdleCallback(pollForDefaultBrowser);
    }, backoffTimes[this._backoffIndex]);
  },

  /**
   * Stops timer for polling changes.
   */
  clearPollingForDefaultChanges() {
    if (this._pollingTimer) {
      clearTimeout(this._pollingTimer);
      this._pollingTimer = undefined;
    }
  },

  /**
   *  Checks if the browser is default through the shell service.
   */
  get isBrowserDefault() {
    if (!this.canCheck) {
      return false;
    }
    return this.shellSvc?.isDefaultBrowser(false, true);
  },

  /**
   * Attempts to set the browser as the user's
   * default through the shell service.
   *
   * @returns {Promise<void>}
   */
  async setDefaultBrowser() {
    // Reset exponential backoff delay time in order to do visual update in pollForDefaultBrowser.
    this._backoffIndex = 0;

    try {
      await this.shellSvc?.setDefaultBrowser(false);
    } catch (e) {
      console.error(e);
    }
  },

  /**
   * Checks whether the browser is capable of being made default.
   *
   * @type {boolean}
   */
  get canCheck() {
    return (
      this.shellSvc &&
      /**
       * Flatpak does not support setting nor detection of default browser
       */
      !gGIOService?.isRunningUnderFlatpak
    );
  },
};

Preferences.addSetting({
  id: "alwaysCheckDefault",
  pref: "browser.shell.checkDefaultBrowser",
  setup: emitChange => {
    if (!DefaultBrowserHelper.canCheck) {
      return;
    }
    DefaultBrowserHelper.pollForDefaultChanges(emitChange);
    // eslint-disable-next-line consistent-return
    return () => DefaultBrowserHelper.clearPollingForDefaultChanges();
  },
  /**
   * Show button for setting browser as default browser or information that
   * browser is already the default browser.
   */
  visible: () => DefaultBrowserHelper.canCheck,
  disabled: (_, setting) =>
    !DefaultBrowserHelper.canCheck ||
    setting.locked ||
    DefaultBrowserHelper.isBrowserDefault,
});

Preferences.addSetting({
  id: "isDefaultPane",
  deps: ["alwaysCheckDefault"],
  visible: () =>
    DefaultBrowserHelper.canCheck && DefaultBrowserHelper.isBrowserDefault,
});

Preferences.addSetting({
  id: "isNotDefaultPane",
  deps: ["alwaysCheckDefault"],
  visible: () =>
    DefaultBrowserHelper.canCheck && !DefaultBrowserHelper.isBrowserDefault,
  onUserClick: (e, { alwaysCheckDefault }) => {
    if (!DefaultBrowserHelper.canCheck) {
      return;
    }
    const setDefaultButton = /** @type {MozButton} */ (e.target);

    if (!setDefaultButton) {
      return;
    }
    if (setDefaultButton.disabled) {
      return;
    }

    /**
     * Disable the set default button, so that the user
     * doesn't try to hit it again while browser is being set to default.
     */
    setDefaultButton.disabled = true;
    alwaysCheckDefault.value = true;
    DefaultBrowserHelper.setDefaultBrowser().finally(() => {
      setDefaultButton.disabled = false;
    });
  },
});

// AI Control pref settings
Preferences.addSetting({
  id: "aiControlDefault",
  pref: "browser.ai.control.default",
});
Preferences.addSetting({
  id: "aiControlTranslations",
  pref: "browser.ai.control.translations",
});
Preferences.addSetting({
  id: "aiControlPdfjsAltText",
  pref: "browser.ai.control.pdfjsAltText",
});
Preferences.addSetting({
  id: "aiControlSmartTabGroups",
  pref: "browser.ai.control.smartTabGroups",
});
Preferences.addSetting({
  id: "aiControlLinkPreviews",
  pref: "browser.ai.control.linkPreviewKeyPoints",
});
Preferences.addSetting({
  id: "aiControlSidebarChatbot",
  pref: "browser.ai.control.sidebarChatbot",
});
Preferences.addSetting({
  id: "aiControlSmartWindow",
  pref: "browser.ai.control.smartWindow",
});

function createDefaultBrowserConfig({
  includeIsDefaultPane = true,
  inProgress = false,
  hiddenFromSearch = false,
} = {}) {
  const isDefaultPane = {
    id: "isDefaultPane",
    l10nId: "is-default-browser-2",
    control: "moz-promo",
    controlAttrs: {
      imagesrc: "chrome://global/skin/illustrations/kit-happy.svg",
      imagedisplay: "cover",
    },
  };

  const isNotDefaultPane = {
    id: "isNotDefaultPane",
    l10nId: "is-not-default-browser-2",
    control: "moz-promo",
    options: [
      {
        control: "moz-button",
        l10nId: "set-as-my-default-browser-2",
        id: "setDefaultButton",
        slot: "actions",

        controlAttrs: {
          type: "primary",
        },
      },
    ],
    controlAttrs: {
      imagesrc: "chrome://global/skin/illustrations/kit-concerned.svg",
      imagedisplay: "cover",
    },
  };

  const items = includeIsDefaultPane
    ? [isDefaultPane, isNotDefaultPane]
    : [isNotDefaultPane];

  return {
    l10nId: "home-default-browser-title",
    headingLevel: 2,
    items,
    ...(inProgress && { inProgress }),
    ...(hiddenFromSearch && { hiddenFromSearch }),
  };
}

function createStartupConfig(hidden = false) {
  return {
    l10nId: "startup-group",
    headingLevel: 2,
    hidden,
    items: [
      {
        id: "browserRestoreSession",
        l10nId: "startup-restore-windows-and-tabs",
      },
      {
        id: "windowsLaunchOnLogin",
        l10nId: "windows-launch-on-login",
      },
      {
        id: "windowsLaunchOnLoginDisabledBox",
        control: "moz-message-bar",
        options: [
          {
            control: "span",
            l10nId: "windows-launch-on-login-disabled",
            slot: "message",
            options: [
              {
                control: "a",
                controlAttrs: {
                  "data-l10n-name": "startup-link",
                  href: "ms-settings:startupapps",
                  target: "_self",
                },
              },
            ],
          },
        ],
      },
      {
        id: "windowsLaunchOnLoginDisabledProfileBox",
        control: "moz-message-bar",
        l10nId: "startup-windows-launch-on-login-profile-disabled",
      },
      {
        id: "alwaysCheckDefault",
        l10nId: "always-check-default",
      },
    ],
  };
}

SettingGroupManager.registerGroups({
  defaultBrowser: createDefaultBrowserConfig(),
  startup: createStartupConfig(
    Services.prefs.getBoolPref("browser-settings-redesign.enabled", false)
  ),
});

/**
 * @param {string} id - ID of {@link SettingGroup} custom element.
 */
function initSettingGroup(id) {
  /** @type {SettingGroup[]} */
  let groups = document.querySelectorAll(`setting-group[groupid=${id}]`);
  const config = SettingGroupManager.get(id);
  for (let group of groups) {
    if (group && config) {
      let sectionEnabled = srdSectionEnabled(id);

      if (
        (sectionEnabled && group.hasAttribute("data-srd-migrated")) ||
        (config.inProgress && !sectionEnabled)
      ) {
        group.remove();
      }

      let legacySections = document.querySelectorAll(
        `[data-srd-groupid=${id}]`
      );
      for (let section of legacySections) {
        if (sectionEnabled) {
          section.hidden = true;
          section.removeAttribute("data-category");
          section.setAttribute("data-hidden-from-search", "true");
        }
      }
      group.config = config;
      group.getSetting = Preferences.getSetting.bind(Preferences);
      group.srdEnabled = srdSectionPrefs.all;
    }
  }
}

// A promise that resolves when the list of application handlers is loaded.
// We store this in a global so tests can await it.
var promiseLoadHandlersList;

// Load the preferences string bundle for other locales with fallbacks.
function getBundleForLocales(newLocales) {
  let locales = Array.from(
    new Set([
      ...newLocales,
      ...Services.locale.requestedLocales,
      Services.locale.lastFallbackLocale,
    ])
  );
  return new Localization(
    ["browser/preferences/preferences.ftl", "branding/brand.ftl"],
    false,
    undefined,
    locales
  );
}

var gNodeToObjectMap = new WeakMap();

var gMainPane = {
  // browser.startup.page values
  STARTUP_PREF_BLANK: 0,
  STARTUP_PREF_HOMEPAGE: 1,
  STARTUP_PREF_RESTORE_SESSION: 3,

  /**
   * Initialization of gMainPane.
   */
  init() {
    /**
     * @param {string} aId
     * @param {string} aEventType
     * @param {(ev: Event) => void} aCallback
     */
    function setEventListener(aId, aEventType, aCallback) {
      document
        .getElementById(aId)
        .addEventListener(aEventType, aCallback.bind(gMainPane));
    }

    this.displayUseSystemLocale();

    if (Services.prefs.getBoolPref("intl.multilingual.enabled")) {
      gMainPane.initPrimaryBrowserLanguageUI();
    }

    gMainPane.initTranslations();

    // Initialize settings groups from the config object.
    initSettingGroup("browserLayout");
    initSettingGroup("appearance");
    initSettingGroup("drm");
    initSettingGroup("contrast");
    initSettingGroup("zoom");
    initSettingGroup("fonts");
    initSettingGroup("browserLanguage");
    initSettingGroup("websiteLanguage");
    initSettingGroup("browsing");
    initSettingGroup("keyboardAndScrolling");
    initSettingGroup("motionAndLink");
    initSettingGroup("updates");
    initSettingGroup("translations");
    initSettingGroup("spellCheck");
    initSettingGroup("performance");
    initSettingGroup("defaultBrowser");
    initSettingGroup("startup");
    initSettingGroup("importBrowserData");
    initSettingGroup("tabs");
    initSettingGroup("profiles");
    initSettingGroup("profilePane");

    setEventListener("manageBrowserLanguagesButton", "command", function () {
      gMainPane.showBrowserLanguagesSubDialog({ search: false });
    });

    setEventListener("chooseLanguage", "command", gMainPane.showLanguages);
    // TODO (Bug 1817084) Remove this code when we disable the extension
    setEventListener(
      "fxtranslateButton",
      "command",
      gMainPane.showTranslationExceptions
    );

    document
      .getElementById("migrationWizardDialog")
      .addEventListener("MigrationWizard:Close", function (e) {
        e.currentTarget.close();
      });

    // Firefox Translations settings panel
    // TODO (Bug 1817084) Remove this code when we disable the extension
    const fxtranslationsDisabledPrefName = "extensions.translations.disabled";
    if (!Services.prefs.getBoolPref(fxtranslationsDisabledPrefName, true)) {
      let fxtranslationRow = document.getElementById("fxtranslationsBox");
      fxtranslationRow.hidden = false;
    }

    // Initilize Application section.

    if (!srdSectionEnabled("applications")) {
      AppFileHandler._init();
    }

    // Listen for window unload so we can remove our preference observers.
    window.addEventListener("unload", this);

    // Notify observers that the UI is now ready
    Services.obs.notifyObservers(window, "main-pane-loaded");
    this.setInitialized();
  },

  preInit() {
    promiseLoadHandlersList = new Promise((resolve, reject) => {
      window.addEventListener(
        "pageshow",
        async () => {
          await this.initialized;
          try {
            if (!srdSectionEnabled("applications")) {
              await AppFileHandler.preInit();
              Services.obs.notifyObservers(window, "app-handler-loaded");
            }
            resolve();
          } catch (ex) {
            reject(ex);
          }
        },
        { once: true }
      );
    });
  },

  handleSubcategory(subcategory) {
    if (Services.policies && !Services.policies.isAllowed("profileImport")) {
      return false;
    }
    if (subcategory == "migrate") {
      this.showMigrationWizardDialog();
      return true;
    }

    if (subcategory == "migrate-autoclose") {
      this.showMigrationWizardDialog({ closeTabWhenDone: true });
    }

    return false;
  },

  // CONTAINERS

  /*
   * preferences:
   *
   * privacy.userContext.enabled
   * - true if containers is enabled
   */

  async onGetStarted() {
    if (!AppConstants.MOZ_DEV_EDITION) {
      return;
    }
    const win = Services.wm.getMostRecentWindow("navigator:browser");
    if (!win) {
      return;
    }
    const user = await fxAccounts.getSignedInUser();
    if (user) {
      // We have a user, open Sync preferences in the same tab
      win.openTrustedLinkIn("about:preferences#sync", "current");
      return;
    }
    if (!(await FxAccounts.canConnectAccount())) {
      return;
    }
    let url =
      await FxAccounts.config.promiseConnectAccountURI("dev-edition-setup");
    let accountsTab = win.gBrowser.addWebTab(url);
    win.gBrowser.selectedTab = accountsTab;
  },

  // HOME PAGE
  /*
   * Preferences:
   *
   * browser.startup.page
   * - what page(s) to show when the user starts the application, as an integer:
   *
   *     0: a blank page (DEPRECATED - this can be set via browser.startup.homepage)
   *     1: the home page (as set by the browser.startup.homepage pref)
   *     2: the last page the user visited (DEPRECATED)
   *     3: windows and tabs from the last session (a.k.a. session restore)
   *
   *   The deprecated option is not exposed in UI; however, if the user has it
   *   selected and doesn't change the UI for this preference, the deprecated
   *   option is preserved.
   */

  /**
   * Utility function to enable/disable the button specified by aButtonID based
   * on the value of the Boolean preference specified by aPreferenceID.
   */
  updateButtons(aButtonID, aPreferenceID) {
    var button = document.getElementById(aButtonID);
    var preference = Preferences.get(aPreferenceID);
    button.disabled = !preference.value;
    return undefined;
  },

  /**
   * Initialize the translations view.
   */
  async initTranslations() {
    let legacyTranslationsVisible = Preferences.getSetting(
      "legacyTranslationsVisible"
    );
    /**
     * Which phase a language download is in.
     *
     * @typedef {"downloaded" | "loading" | "uninstalled"} DownloadPhase
     */

    let translationsGroup = document.getElementById("translationsGroup");
    let setTranslationsGroupVisbility = () => {
      // Immediately show the group so that the async load of the component does
      // not cause the layout to jump. The group will be empty initially.
      translationsGroup.hidden = !legacyTranslationsVisible.visible;
      translationsGroup.classList.toggle(
        "setting-hidden",
        translationsGroup.hidden
      );
    };
    setTranslationsGroupVisbility();

    legacyTranslationsVisible.on("change", setTranslationsGroupVisbility);
    window.addEventListener(
      "unload",
      () =>
        legacyTranslationsVisible.off("change", setTranslationsGroupVisbility),
      { once: true }
    );

    class TranslationsState {
      /**
       * The fully initialized state.
       *
       * @param {object} supportedLanguages
       * @param {Array<{ langTag: string, displayName: string}>} languageList
       * @param {Map<string, DownloadPhase>} downloadPhases
       */
      constructor(supportedLanguages, languageList, downloadPhases) {
        this.supportedLanguages = supportedLanguages;
        this.languageList = languageList;
        this.downloadPhases = downloadPhases;
      }

      /**
       * Handles all of the async initialization logic.
       */
      static async create() {
        const supportedLanguages =
          await TranslationsParent.getSupportedLanguages();
        const languageList =
          TranslationsParent.getLanguageList(supportedLanguages);
        const downloadPhases =
          await TranslationsState.createDownloadPhases(languageList);

        if (supportedLanguages.languagePairs.length === 0) {
          throw new Error(
            "The supported languages list was empty. RemoteSettings may not be available at the moment."
          );
        }

        return new TranslationsState(
          supportedLanguages,
          languageList,
          downloadPhases
        );
      }

      /**
       * Determine the download phase of each language file.
       *
       * @param {Array<{ langTag: string, displayName: string}>} languageList
       * @returns {Promise<Map<string, DownloadPhase>>} Map the language tag to whether it is downloaded.
       */
      static async createDownloadPhases(languageList) {
        const downloadPhases = new Map();
        for (const { langTag } of languageList) {
          downloadPhases.set(
            langTag,
            (await TranslationsParent.hasAllFilesForLanguage(langTag))
              ? "downloaded"
              : "uninstalled"
          );
        }
        return downloadPhases;
      }
    }

    class TranslationsView {
      /** @type {Map<string, XULButton>} */
      deleteButtons = new Map();
      /** @type {Map<string, XULButton>} */
      downloadButtons = new Map();

      /**
       * @param {TranslationsState} state
       */
      constructor(state) {
        this.state = state;
        this.elements = {
          settingsButton: document.getElementById(
            "translations-manage-settings-button"
          ),
          installList: document.getElementById(
            "translations-manage-install-list"
          ),
          installAll: document.getElementById(
            "translations-manage-install-all"
          ),
          deleteAll: document.getElementById("translations-manage-delete-all"),
          error: document.getElementById("translations-manage-error"),
        };
        this.setup();
      }

      setup() {
        this.buildLanguageList();

        this.elements.settingsButton.addEventListener(
          "command",
          gMainPane.showTranslationsSettings
        );
        this.elements.installAll.addEventListener(
          "command",
          this.handleInstallAll
        );
        this.elements.deleteAll.addEventListener(
          "command",
          this.handleDeleteAll
        );

        Services.obs.addObserver(this, "intl:app-locales-changed");
      }

      destroy() {
        Services.obs.removeObserver(this, "intl:app-locales-changed");
      }

      handleInstallAll = async () => {
        this.hideError();
        this.disableButtons(true);
        try {
          await TranslationsParent.downloadAllFiles();
          this.markAllDownloadPhases("downloaded");
        } catch (error) {
          TranslationsView.showError(
            "translations-manage-error-download",
            error
          );
          await this.reloadDownloadPhases();
          this.updateAllButtons();
        }
        this.disableButtons(false);
      };

      handleDeleteAll = async () => {
        this.hideError();
        this.disableButtons(true);
        try {
          await TranslationsUtils.deleteAllLanguageFiles();
          this.markAllDownloadPhases("uninstalled");
        } catch (error) {
          TranslationsView.showError("translations-manage-error-remove", error);
          // The download phases are invalidated with the error and must be reloaded.
          await this.reloadDownloadPhases();
          console.error(error);
        }
        this.disableButtons(false);
      };

      /**
       * @param {string} langTag
       * @returns {Function}
       */
      getDownloadButtonHandler(langTag) {
        return async () => {
          this.hideError();
          this.updateDownloadPhase(langTag, "loading");
          try {
            await TranslationsParent.downloadLanguageFiles(langTag);
            this.updateDownloadPhase(langTag, "downloaded");
          } catch (error) {
            TranslationsView.showError(
              "translations-manage-error-download",
              error
            );
            this.updateDownloadPhase(langTag, "uninstalled");
          }
        };
      }

      /**
       * @param {string} langTag
       * @returns {Function}
       */
      getDeleteButtonHandler(langTag) {
        return async () => {
          this.hideError();
          this.updateDownloadPhase(langTag, "loading");
          try {
            await TranslationsParent.deleteLanguageFiles(langTag);
            this.updateDownloadPhase(langTag, "uninstalled");
          } catch (error) {
            TranslationsView.showError(
              "translations-manage-error-remove",
              error
            );
            // The download phases are invalidated with the error and must be reloaded.
            await this.reloadDownloadPhases();
          }
        };
      }

      buildLanguageList() {
        const listFragment = document.createDocumentFragment();

        for (const { langTag, displayName } of this.state.languageList) {
          const hboxRow = document.createXULElement("hbox");
          hboxRow.classList.add("translations-manage-language");
          hboxRow.setAttribute("data-lang-tag", langTag);

          const languageLabel = document.createXULElement("label");
          languageLabel.textContent = displayName; // The display name is already localized.

          const downloadButton = document.createXULElement("button");
          const deleteButton = document.createXULElement("button");

          downloadButton.addEventListener(
            "command",
            this.getDownloadButtonHandler(langTag)
          );
          deleteButton.addEventListener(
            "command",
            this.getDeleteButtonHandler(langTag)
          );

          document.l10n.setAttributes(
            downloadButton,
            "translations-manage-language-download-button"
          );
          document.l10n.setAttributes(
            deleteButton,
            "translations-manage-language-remove-button"
          );

          downloadButton.hidden = true;
          deleteButton.hidden = true;

          this.deleteButtons.set(langTag, deleteButton);
          this.downloadButtons.set(langTag, downloadButton);

          hboxRow.appendChild(languageLabel);
          hboxRow.appendChild(downloadButton);
          hboxRow.appendChild(deleteButton);
          listFragment.appendChild(hboxRow);
        }
        this.updateAllButtons();
        this.elements.installList.appendChild(listFragment);
      }

      /**
       * Update the DownloadPhase for a single langTag.
       *
       * @param {string} langTag
       * @param {DownloadPhase} downloadPhase
       */
      updateDownloadPhase(langTag, downloadPhase) {
        this.state.downloadPhases.set(langTag, downloadPhase);
        this.updateButton(langTag, downloadPhase);
        this.updateHeaderButtons();
      }

      /**
       * Recreates the download map when the state is invalidated.
       */
      async reloadDownloadPhases() {
        this.state.downloadPhases =
          await TranslationsState.createDownloadPhases(this.state.languageList);
        this.updateAllButtons();
      }

      /**
       * Set all the downloads.
       *
       * @param {DownloadPhase} downloadPhase
       */
      markAllDownloadPhases(downloadPhase) {
        const { downloadPhases } = this.state;
        for (const key of downloadPhases.keys()) {
          downloadPhases.set(key, downloadPhase);
        }
        this.updateAllButtons();
      }

      /**
       * If all languages are downloaded, or no languages are downloaded then
       * the visibility of the buttons need to change.
       */
      updateHeaderButtons() {
        let allDownloaded = true;
        let allUninstalled = true;
        for (const downloadPhase of this.state.downloadPhases.values()) {
          if (downloadPhase === "loading") {
            // Don't count loading towards this calculation.
            continue;
          }
          allDownloaded &&= downloadPhase === "downloaded";
          allUninstalled &&= downloadPhase === "uninstalled";
        }

        this.elements.installAll.hidden = allDownloaded;
        this.elements.deleteAll.hidden = allUninstalled;
      }

      /**
       * Update the buttons according to their download state.
       */
      updateAllButtons() {
        this.updateHeaderButtons();
        for (const [langTag, downloadPhase] of this.state.downloadPhases) {
          this.updateButton(langTag, downloadPhase);
        }
      }

      /**
       * @param {string} langTag
       * @param {DownloadPhase} downloadPhase
       */
      updateButton(langTag, downloadPhase) {
        const downloadButton = this.downloadButtons.get(langTag);
        const deleteButton = this.deleteButtons.get(langTag);
        switch (downloadPhase) {
          case "downloaded":
            downloadButton.hidden = true;
            deleteButton.hidden = false;
            downloadButton.removeAttribute("disabled");
            break;
          case "uninstalled":
            downloadButton.hidden = false;
            deleteButton.hidden = true;
            downloadButton.removeAttribute("disabled");
            break;
          case "loading":
            downloadButton.hidden = false;
            deleteButton.hidden = true;
            downloadButton.setAttribute("disabled", "true");
            break;
        }
      }

      /**
       * @param {boolean} isDisabled
       */
      disableButtons(isDisabled) {
        this.elements.installAll.disabled = isDisabled;
        this.elements.deleteAll.disabled = isDisabled;
        for (const button of this.downloadButtons.values()) {
          button.disabled = isDisabled;
        }
        for (const button of this.deleteButtons.values()) {
          button.disabled = isDisabled;
        }
      }

      /**
       * This method is static in case an error happens during the creation of the
       * TranslationsState.
       *
       * @param {string} l10nId
       * @param {Error} error
       */
      static showError(l10nId, error) {
        console.error(error);
        const errorMessage = document.getElementById(
          "translations-manage-error"
        );
        errorMessage.hidden = false;
        document.l10n.setAttributes(errorMessage, l10nId);
      }

      hideError() {
        this.elements.error.hidden = true;
      }

      observe(_subject, topic, _data) {
        if (topic === "intl:app-locales-changed") {
          this.refreshLanguageListDisplay();
        }
      }

      refreshLanguageListDisplay() {
        try {
          const languageDisplayNames =
            TranslationsParent.createLanguageDisplayNames();

          for (const row of this.elements.installList.children) {
            const rowLangTag = row.getAttribute("data-lang-tag");
            if (!rowLangTag) {
              continue;
            }

            const label = row.querySelector("label");
            if (label) {
              const newDisplayName = languageDisplayNames.of(rowLangTag);
              if (label.textContent !== newDisplayName) {
                label.textContent = newDisplayName;
              }
            }
          }
        } catch (error) {
          console.error(error);
        }
      }
    }

    TranslationsState.create().then(
      state => {
        this._translationsView = new TranslationsView(state);
      },
      error => {
        // This error can happen when a user is not connected to the internet, or
        // RemoteSettings is down for some reason.
        TranslationsView.showError("translations-manage-error-list", error);
      }
    );
  },

  initPrimaryBrowserLanguageUI() {
    // This will register the "command" listener.
    let menulist = document.getElementById("primaryBrowserLocale");
    new SelectionChangedMenulist(menulist, event => {
      gMainPane.onPrimaryBrowserLanguageMenuChange(event);
    });

    gMainPane.updatePrimaryBrowserLanguageUI(Services.locale.appLocaleAsBCP47);
  },

  /**
   * Update the available list of locales and select the locale that the user
   * is "selecting". This could be the currently requested locale or a locale
   * that the user would like to switch to after confirmation.
   *
   * @param {string} selected - The selected BCP 47 locale.
   */
  async updatePrimaryBrowserLanguageUI(selected) {
    let available = await LangPackMatcher.getAvailableLocales();
    let localeNames = Services.intl.getLocaleDisplayNames(
      undefined,
      available,
      { preferNative: true }
    );
    let locales = available.map((code, i) => ({ code, name: localeNames[i] }));
    locales.sort((a, b) => a.name > b.name);

    let fragment = document.createDocumentFragment();
    for (let { code, name } of locales) {
      let menuitem = document.createXULElement("menuitem");
      menuitem.setAttribute("value", code);
      menuitem.setAttribute("label", name);
      fragment.appendChild(menuitem);
    }

    // Add an option to search for more languages if downloading is supported.
    if (Services.prefs.getBoolPref("intl.multilingual.downloadEnabled")) {
      let menuitem = document.createXULElement("menuitem");
      menuitem.id = "primaryBrowserLocaleSearch";
      menuitem.setAttribute(
        "label",
        await document.l10n.formatValue("browser-languages-search")
      );
      menuitem.setAttribute("value", "search");
      fragment.appendChild(menuitem);
    }

    let menulist = document.getElementById("primaryBrowserLocale");
    let menupopup = menulist.querySelector("menupopup");
    menupopup.textContent = "";
    menupopup.appendChild(fragment);
    menulist.value = selected;

    document.getElementById("browserLanguagesBox").hidden = false;
  },

  /* Show the confirmation message bar to allow a restart into the new locales. */
  async showConfirmLanguageChangeMessageBar(locales) {
    let messageBar = document.getElementById("confirmBrowserLanguage");

    // Get the bundle for the new locale.
    let newBundle = getBundleForLocales(locales);

    // Find the messages and labels.
    let messages = await Promise.all(
      [newBundle, document.l10n].map(async bundle =>
        bundle.formatValue("confirm-browser-language-change-description")
      )
    );
    let buttonLabels = await Promise.all(
      [newBundle, document.l10n].map(async bundle =>
        bundle.formatValue("confirm-browser-language-change-button")
      )
    );

    // If both the message and label are the same, just include one row.
    if (messages[0] == messages[1] && buttonLabels[0] == buttonLabels[1]) {
      messages.pop();
      buttonLabels.pop();
    }

    let contentContainer = messageBar.querySelector(
      ".message-bar-content-container"
    );
    contentContainer.textContent = "";

    for (let i = 0; i < messages.length; i++) {
      let messageContainer = document.createXULElement("hbox");
      messageContainer.classList.add("message-bar-content");
      messageContainer.style.flex = "1 50%";
      messageContainer.setAttribute("align", "center");

      let description = document.createXULElement("description");
      description.classList.add("message-bar-description");

      if (i == 0 && Services.intl.getScriptDirection(locales[0]) === "rtl") {
        description.classList.add("rtl-locale");
      }
      description.setAttribute("flex", "1");
      description.textContent = messages[i];
      messageContainer.appendChild(description);

      let button = document.createXULElement("button");
      button.addEventListener(
        "command",
        gMainPane.confirmBrowserLanguageChange
      );
      button.classList.add("message-bar-button");
      button.setAttribute("locales", locales.join(","));
      button.setAttribute("label", buttonLabels[i]);
      messageContainer.appendChild(button);

      contentContainer.appendChild(messageContainer);
    }

    messageBar.hidden = false;
    gMainPane.selectedLocalesForRestart = locales;
  },

  hideConfirmLanguageChangeMessageBar() {
    let messageBar = document.getElementById("confirmBrowserLanguage");
    messageBar.hidden = true;
    let contentContainer = messageBar.querySelector(
      ".message-bar-content-container"
    );
    contentContainer.textContent = "";
    gMainPane.requestingLocales = null;
  },

  /* Confirm the locale change and restart the browser in the new locale. */
  confirmBrowserLanguageChange(event) {
    let localesString = (event.target.getAttribute("locales") || "").trim();
    if (!localesString || !localesString.length) {
      return;
    }
    let locales = localesString.split(",");
    Multilingual.applyAndRestart(locales);
  },

  /* Show or hide the confirm change message bar based on the new locale. */
  onPrimaryBrowserLanguageMenuChange(event) {
    let locale = event.target.value;

    if (locale == "search") {
      gMainPane.showBrowserLanguagesSubDialog({ search: true });
      return;
    } else if (locale == Services.locale.appLocaleAsBCP47) {
      this.hideConfirmLanguageChangeMessageBar();
      return;
    }

    let newLocales = Array.from(
      new Set([locale, ...Services.locale.requestedLocales]).values()
    );

    Multilingual.recordTelemetry("reorder");

    switch (Multilingual.getTransitionType(newLocales)) {
      case Multilingual.TransitionType.RestartRequired:
        // Prepare to change the locales, as they were different.
        gMainPane.showConfirmLanguageChangeMessageBar(newLocales);
        gMainPane.updatePrimaryBrowserLanguageUI(newLocales[0]);
        break;
      case Multilingual.TransitionType.LiveReload:
        Services.locale.requestedLocales = newLocales;
        gMainPane.updatePrimaryBrowserLanguageUI(
          Services.locale.appLocaleAsBCP47
        );
        gMainPane.hideConfirmLanguageChangeMessageBar();
        break;
      case Multilingual.TransitionType.LocalesMatch:
        // They matched, so we can reset the UI.
        gMainPane.updatePrimaryBrowserLanguageUI(
          Services.locale.appLocaleAsBCP47
        );
        gMainPane.hideConfirmLanguageChangeMessageBar();
        break;
      default:
        throw new Error("Unhandled transition type.");
    }
  },

  /**
   *  Shows a window dialog containing the profile selector page.
   */
  manageProfiles() {
    const win = window.browsingContext.topChromeWindow;

    win.toOpenWindowByType(
      "about:profilemanager",
      "about:profilemanager",
      "chrome,extrachrome,menubar,resizable,scrollbars,status,toolbar,centerscreen"
    );
  },

  /**
   * Shows a dialog in which the preferred language for web content may be set.
   */
  showLanguages() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/languages.xhtml"
    );
  },

  /**
   * Open the browser languages sub dialog in either the normal mode, or search mode.
   * The search mode is only available from the menu to change the primary browser
   * language.
   *
   * @param {{ search: boolean }} search
   */
  showBrowserLanguagesSubDialog({ search }) {
    // Record the telemetry event with an id to associate related actions.
    let telemetryId = parseInt(
      Services.telemetry.msSinceProcessStart(),
      10
    ).toString();
    let method = search ? "search" : "manage";
    Multilingual.recordTelemetry(method, telemetryId);

    let opts = {
      selectedLocalesForRestart: gMainPane.selectedLocalesForRestart,
      search,
      telemetryId,
    };
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/browserLanguages.xhtml",
      { closingCallback: this.browserLanguagesClosed },
      opts
    );
  },

  /* Show or hide the confirm change message bar based on the updated ordering. */
  browserLanguagesClosed() {
    // When the subdialog is closed, settings are stored on gBrowserLanguagesDialog.
    // The next time the dialog is opened, a new gBrowserLanguagesDialog is created.
    let { selected } = this.gBrowserLanguagesDialog;

    this.gBrowserLanguagesDialog.recordTelemetry(
      selected ? "accept" : "cancel"
    );

    if (!selected) {
      // No locales were selected. Cancel the operation.
      return;
    }

    // Track how often locale fallback order is changed.
    // Drop the first locale and filter to only include the overlapping set
    const prevLocales = Services.locale.requestedLocales.filter(
      lc => selected.indexOf(lc) > 0
    );
    const newLocales = selected.filter(
      (lc, i) => i > 0 && prevLocales.includes(lc)
    );
    if (prevLocales.some((lc, i) => newLocales[i] != lc)) {
      this.gBrowserLanguagesDialog.recordTelemetry("setFallback");
    }

    switch (Multilingual.getTransitionType(selected)) {
      case Multilingual.TransitionType.RestartRequired:
        gMainPane.showConfirmLanguageChangeMessageBar(selected);
        gMainPane.updatePrimaryBrowserLanguageUI(selected[0]);
        break;
      case Multilingual.TransitionType.LiveReload:
        Services.locale.requestedLocales = selected;

        gMainPane.updatePrimaryBrowserLanguageUI(
          Services.locale.appLocaleAsBCP47
        );
        gMainPane.hideConfirmLanguageChangeMessageBar();
        break;
      case Multilingual.TransitionType.LocalesMatch:
        // They matched, so we can reset the UI.
        gMainPane.updatePrimaryBrowserLanguageUI(
          Services.locale.appLocaleAsBCP47
        );
        gMainPane.hideConfirmLanguageChangeMessageBar();
        break;
      default:
        throw new Error("Unhandled transition type.");
    }
  },

  displayUseSystemLocale() {
    let appLocale = Services.locale.appLocaleAsBCP47;
    let regionalPrefsLocales = Services.locale.regionalPrefsLocales;
    if (!regionalPrefsLocales.length) {
      return;
    }
    let systemLocale = regionalPrefsLocales[0];
    let localeDisplayname = Services.intl.getLocaleDisplayNames(
      undefined,
      [systemLocale],
      { preferNative: true }
    );
    if (!localeDisplayname.length) {
      return;
    }
    let localeName = localeDisplayname[0];
    if (appLocale.split("-u-")[0] != systemLocale.split("-u-")[0]) {
      let checkbox = document.getElementById("useSystemLocale");
      document.l10n.setAttributes(checkbox, "use-system-locale", {
        localeName,
      });
      checkbox.hidden = false;
    }
  },

  /**
   * Displays the translation exceptions dialog where specific site and language
   * translation preferences can be set.
   */
  // TODO (Bug 1817084) Remove this code when we disable the extension
  showTranslationExceptions() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/translationExceptions.xhtml"
    );
  },

  showTranslationsSettings() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/translations.xhtml"
    );
  },

  // NETWORK
  /**
   * Displays a dialog in which proxy settings may be changed.
   */
  showConnections() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/connection.xhtml"
    );
  },

  /**
   * Displays the migration wizard dialog in an HTML dialog.
   */
  async showMigrationWizardDialog({ closeTabWhenDone = false } = {}) {
    let migrationWizardDialog = document.getElementById(
      "migrationWizardDialog"
    );

    if (migrationWizardDialog.open) {
      return;
    }

    await customElements.whenDefined("migration-wizard");

    // If we've been opened before, remove the old wizard and insert a
    // new one to put it back into its starting state.
    if (!migrationWizardDialog.firstElementChild) {
      let wizard = document.createElement("migration-wizard");
      wizard.toggleAttribute("dialog-mode", true);
      migrationWizardDialog.appendChild(wizard);
    }
    migrationWizardDialog.firstElementChild.requestState();

    migrationWizardDialog.addEventListener(
      "close",
      () => {
        // Let others know that the wizard is closed -- potentially because of a
        // user action within the dialog that dispatches "MigrationWizard:Close"
        // but this also covers cases like hitting Escape.
        Services.obs.notifyObservers(
          migrationWizardDialog,
          "MigrationWizard:Closed"
        );
        if (closeTabWhenDone) {
          window.close();
        }
      },
      { once: true }
    );

    migrationWizardDialog.showModal();
  },

  destroy() {
    window.removeEventListener("unload", this);

    // Clean up the TranslationsView instance if it exists
    if (this._translationsView) {
      this._translationsView.destroy();
      this._translationsView = null;
    }
  },

  // nsISupports

  QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),

  // nsIObserver

  async observe(_, aTopic, aData) {
    if (aTopic == "nsPref:changed") {
      if (aData == PREF_CONTAINERS_EXTENSION) {
        return;
      }
      // Rebuild the list when there are changes to preferences that influence
      // whether or not to show certain entries in the list.
      if (
        !srdSectionEnabled("applications") &&
        !AppFileHandler._storingAction
      ) {
        await AppFileHandler._rebuildView();
      }
    }
  },

  // EventListener

  handleEvent(aEvent) {
    if (aEvent.type == "unload") {
      this.destroy();
      if (AppConstants.MOZ_UPDATER) {
        onUnload();
      }
    }
  },

  /**
   * Whether or not the given handler app is valid.
   *
   * @todo remove this method and use {@link AppFileHandler.isValidHandlerApp} when #appList dialog is no longer using it.
   * @param aHandlerApp {nsIHandlerApp} the handler app in question
   * @returns {boolean} whether or not it's valid
   */
  isValidHandlerApp(aHandlerApp) {
    return AppFileHandler.isValidHandlerApp(aHandlerApp);
  },

  /**
   * @todo remove this method and use {@link AppFileHandler._getIconURLForHandlerApp} when #appList dialog is no longer using it.
   * @param aHandlerApp {nsIHandlerApp}
   * @returns {string}
   */
  _getIconURLForHandlerApp(aHandlerApp) {
    return getIconURLForHandlerApp(aHandlerApp);
  },
};
gMainPane.initialized = new Promise(res => {
  gMainPane.setInitialized = res;
});

var {
  InternalHandlerInfoWrapper,
  getFileDisplayName,
  getLocalHandlerApp,
  HandlerServiceHelpers,
  getIconURLForHandlerApp,
} = ChromeUtils.importESModule(
  "chrome://browser/content/preferences/config/downloads.mjs",
  { global: "current" }
);

/**
 * @typedef {MozOption & {
 * handlerApp: nsIHandlerApp | null | void
 * }} ApplicationFileHandlerItemActionsMenuOption
 */

let gHandlerListItemFragment = window.MozXULElement.parseXULToFragment(`
  <richlistitem>
    <hbox class="typeContainer" flex="1" align="center">
      <html:img class="typeIcon" width="16" height="16" />
      <label class="typeDescription" flex="1" crop="end"/>
    </hbox>
    <hbox class="actionContainer" flex="1" align="center">
      <html:img class="actionIcon" width="16" height="16"/>
      <label class="actionDescription" flex="1" crop="end"/>
    </hbox>
    <hbox class="actionsMenuContainer" flex="1">
      <menulist class="actionsMenu" flex="1" crop="end" selectedIndex="1" aria-labelledby="actionColumn">
        <menupopup/>
      </menulist>
    </hbox>
  </richlistitem>
`);

/**
 * This is associated to <richlistitem> elements in the handlers view.
 * Maintained to support the legacy "Applications" section.
 */
class HandlerListItem {
  /**
   * @param {Node} node
   * @returns {Node | undefined}
   */
  static forNode(node) {
    return gNodeToObjectMap.get(node);
  }

  /**
   * @param {HandlerInfoWrapper} handlerInfoWrapper
   */
  constructor(handlerInfoWrapper) {
    this.handlerInfoWrapper = handlerInfoWrapper;
  }

  /**
   *
   * @param {Array<[string, string, string]>} iterable
   */
  setOrRemoveAttributes(iterable) {
    for (let [selector, name, value] of iterable) {
      let node = selector ? this.node.querySelector(selector) : this.node;
      if (value) {
        node.setAttribute(name, value);
      } else {
        node.removeAttribute(name);
      }
    }
  }

  createNode(list) {
    list.appendChild(document.importNode(gHandlerListItemFragment, true));
    this.node = list.lastChild;
    gNodeToObjectMap.set(this.node, this);
  }

  setupNode() {
    this.node
      .querySelector(".actionsMenu")
      .addEventListener("command", event =>
        AppFileHandler.onSelectAction(event.originalTarget)
      );

    let typeDescription = this.handlerInfoWrapper.typeDescription;
    this.setOrRemoveAttributes([
      [null, "type", this.handlerInfoWrapper.type],
      [".typeIcon", "srcset", this.handlerInfoWrapper.iconSrcSet],
    ]);
    localizeElement(
      this.node.querySelector(".typeDescription"),
      typeDescription
    );
    this.showActionsMenu = false;
  }

  refreshAction() {
    let { actionIconClass } = this.handlerInfoWrapper;
    this.setOrRemoveAttributes([
      [null, APP_ICON_ATTR_NAME, actionIconClass],
      [
        ".actionIcon",
        "srcset",
        actionIconClass ? null : this.handlerInfoWrapper.actionIconSrcset,
      ],
    ]);
    const selectedItem = this.node.querySelector("[selected=true]");
    if (!selectedItem) {
      console.error("No selected item for " + this.handlerInfoWrapper.type);
      return;
    }
    const { id, args } = document.l10n.getAttributes(selectedItem);
    const messageIDs = {
      "applications-action-save": "applications-action-save-label",
      "applications-always-ask": "applications-always-ask-label",
      "applications-open-inapp": "applications-open-inapp-label",
      "applications-use-app-default": "applications-use-app-default-label",
      "applications-use-app": "applications-use-app-label",
      "applications-use-os-default": "applications-use-os-default-label",
      "applications-use-other": "applications-use-other-label",
    };
    localizeElement(this.node.querySelector(".actionDescription"), {
      id: messageIDs[id],
      args,
    });
    localizeElement(this.node.querySelector(".actionsMenu"), { id, args });
  }

  set showActionsMenu(value) {
    this.setOrRemoveAttributes([
      [".actionContainer", "hidden", value],
      [".actionsMenuContainer", "hidden", !value],
    ]);
  }
}

/**
 * This API facilitates dual-model of some localization APIs which
 * may operate on raw strings of l10n id/args pairs.
 *
 * The l10n can be:
 *
 * {raw: string} - raw strings to be used as text value of the element
 * {id: string} - l10n-id
 * {id: string, args: object} - l10n-id + l10n-args
 */
function localizeElement(node, l10n) {
  if (l10n.hasOwnProperty("raw")) {
    node.removeAttribute("data-l10n-id");
    node.textContent = l10n.raw;
  } else {
    document.l10n.setAttributes(node, l10n.id, l10n.args);
  }
}

/**
 * Handler class for the legacy "Applications" section of settings. This can be
 * removed when we ship the redesigned settings page.
 */
let AppFileHandler = (function () {
  return new (class Handler {
    /**
     * The set of types the app knows how to handle.  A hash of HandlerInfoWrapper
     * objects, indexed by type.
     *
     * @type {Record<string, any>}
     */
    _handledTypes = {};

    /**
     * The list of types we can show, sorted by the sort column/direction.
     * An array of HandlerInfoWrapper objects.  We build this list when we first
     * load the data and then rebuild it when users change a pref that affects
     * what types we can show or change the sort column/direction.
     * Note: this isn't necessarily the list of types we *will* show; if the user
     * provides a filter string, we'll only show the subset of types in this list
     * that match that string.
     *
     * @type {Array<any>}
     */
    _visibleTypes = [];

    /**
     * @type {HandlerListItem | null}
     */
    selectedHandlerListItem = null;

    // Sorting & Filtering

    /**
     * @type {Element | null}
     */
    _sortColumn = null;

    /**
     * Currently-showing handler items.
     *
     * @type {Array<ApplicationListItem>}
     */
    items = [];

    get _list() {
      return document.getElementById("handlersView");
    }

    get _filter() {
      return document.getElementById("filter");
    }

    initialized = false;

    async preInit() {
      this._initListEventHandlers();
      this._loadInternalHandlers();
      this._loadApplicationHandlers();

      await this._rebuildVisibleTypes();
      await this._rebuildView();
      await this._sortListView();
    }

    _init() {
      setEventListener("filter", "MozInputSearch:search", () => this.filter());
      setEventListener("typeColumn", "click", e => this.sort(e));
      setEventListener("actionColumn", "click", e => this.sort(e));

      // Figure out how we should be sorting the list.  We persist sort settings
      // across sessions, so we can't assume the default sort column/direction.
      // XXX should we be using the XUL sort service instead?
      if (
        document.getElementById("actionColumn").hasAttribute("sortDirection")
      ) {
        this._sortColumn = document.getElementById("actionColumn");
        // The typeColumn element always has a sortDirection attribute,
        // either because it was persisted or because the default value
        // from the xul file was used.  If we are sorting on the other
        // column, we should remove it.
        document.getElementById("typeColumn").removeAttribute("sortDirection");
      } else {
        this._sortColumn = document.getElementById("typeColumn");
      }
    }

    async _rebuildVisibleTypes() {
      this._visibleTypes = [];

      // Map whose keys are string descriptions and values are references to the
      // first visible HandlerInfoWrapper that has this description. We use this
      // to determine whether or not to annotate descriptions with their types to
      // distinguish duplicate descriptions from each other.
      let visibleDescriptions = new Map();
      for (let type in this._handledTypes) {
        // Yield before processing each handler info object to avoid monopolizing
        // the main thread, as the objects are retrieved lazily, and retrieval
        // can be expensive on Windows.
        await new Promise(resolve => Services.tm.dispatchToMainThread(resolve));

        let handlerInfo = this._handledTypes[type];

        // We couldn't find any reason to exclude the type, so include it.
        this._visibleTypes.push(handlerInfo);

        let key = JSON.stringify(handlerInfo.description);
        let otherHandlerInfo = visibleDescriptions.get(key);
        if (!otherHandlerInfo) {
          // This is the first type with this description that we encountered
          // while rebuilding the _visibleTypes array this time. Make sure the
          // flag is reset so we won't add the type to the description.
          handlerInfo.disambiguateDescription = false;
          visibleDescriptions.set(key, handlerInfo);
        } else {
          // There is at least another type with this description. Make sure we
          // add the type to the description on both HandlerInfoWrapper objects.
          handlerInfo.disambiguateDescription = true;
          otherHandlerInfo.disambiguateDescription = true;
        }
      }
    }

    _loadApplicationHandlers() {
      HandlerServiceHelpers.loadApplicationHandlers(this._handledTypes);
    }

    _initListEventHandlers() {
      this._list.addEventListener("select", event => {
        if (event.target != this._list) {
          return;
        }

        let handlerListItem =
          this._list.selectedItem &&
          HandlerListItem.forNode(this._list.selectedItem);
        if (this.selectedHandlerListItem == handlerListItem) {
          return;
        }

        if (this.selectedHandlerListItem) {
          this.selectedHandlerListItem.showActionsMenu = false;
        }
        this.selectedHandlerListItem = handlerListItem;
        if (handlerListItem) {
          this.rebuildActionsMenu();
          handlerListItem.showActionsMenu = true;
        }
      });
    }

    _loadInternalHandlers() {
      HandlerServiceHelpers.loadInternalHandlers(this._handledTypes);
    }

    async _rebuildView() {
      let lastSelectedType =
        this.selectedHandlerListItem &&
        this.selectedHandlerListItem.handlerInfoWrapper.type;
      this.selectedHandlerListItem = null;

      // Clear the list of entries.
      this._list.textContent = "";

      var visibleTypes = this._visibleTypes;

      let items = visibleTypes.map(
        visibleType => new HandlerListItem(visibleType)
      );
      let itemsFragment = document.createDocumentFragment();
      let lastSelectedItem;
      for (let item of items) {
        item.createNode(itemsFragment);
        if (item.handlerInfoWrapper.type == lastSelectedType) {
          lastSelectedItem = item;
        }
      }

      for (let item of items) {
        item.setupNode();
        this.rebuildActionsMenu(item.node, item.handlerInfoWrapper);
        item.refreshAction();
      }

      // If the user is filtering the list, then only show matching types.
      // If we filter, we need to first localize the fragment, to
      // be able to filter by localized values.
      if (this._filter.value) {
        await document.l10n.translateFragment(itemsFragment);

        this._filterView(itemsFragment);

        document.l10n.pauseObserving();
        this._list.appendChild(itemsFragment);
        document.l10n.resumeObserving();
      } else {
        // Otherwise we can just append the fragment and it'll
        // get localized via the Mutation Observer.
        this._list.appendChild(itemsFragment);
      }

      if (lastSelectedItem) {
        this._list.selectedItem = lastSelectedItem.node;
      }
    }

    /**
     * Sort the list when the user clicks on a column header.
     *
     * @param {CustomEvent} event
     */
    sort(event) {
      if (event.button != 0) {
        return;
      }
      var column = event.target;

      // If the user clicked on a new sort column, remove the direction indicator
      // from the old column.
      if (this._sortColumn && this._sortColumn != column) {
        this._sortColumn.removeAttribute("sortDirection");
      }

      this._sortColumn = column;

      // Set (or switch) the sort direction indicator.
      if (column.getAttribute("sortDirection") == "ascending") {
        column.setAttribute("sortDirection", "descending");
      } else {
        column.setAttribute("sortDirection", "ascending");
      }

      this._sortListView();
    }

    async _sortListView() {
      if (!this._sortColumn) {
        return;
      }
      let comp = new Services.intl.Collator(undefined, {
        usage: "sort",
      });

      await document.l10n.translateFragment(this._list);
      let items = Array.from(this._list.children);

      let textForNode;
      if (this._sortColumn.getAttribute("value") === "type") {
        textForNode = n => n.querySelector(".typeDescription").textContent;
      } else {
        textForNode = n =>
          n.querySelector(".actionsMenu").getAttribute("label");
      }

      let sortDir = this._sortColumn.getAttribute("sortDirection");
      let multiplier = sortDir == "descending" ? -1 : 1;
      items.sort(
        (a, b) => multiplier * comp.compare(textForNode(a), textForNode(b))
      );

      // Re-append items in the correct order:
      items.forEach(item => this._list.appendChild(item));
    }

    _filterView(frag = this._list) {
      const filterValue = this._filter.value.toLowerCase();
      for (let elem of frag.children) {
        const typeDescription =
          elem.querySelector(".typeDescription").textContent;
        const actionDescription = elem
          .querySelector(".actionDescription")
          .getAttribute("value");
        elem.hidden =
          !typeDescription.toLowerCase().includes(filterValue) &&
          !actionDescription.toLowerCase().includes(filterValue);
      }
    }

    /**
     * Creates the header item.
     *
     * @return {MozBoxItem}
     */
    _buildHeader() {
      const headerElement = /** @type {MozBoxItem} */ (
        document.createElement("moz-box-item")
      );
      headerElement.slot = "header";
      this.typeColumn = document.createElement("label");
      this.typeColumn.setAttribute("data-l10n-id", "applications-type-heading");
      headerElement.appendChild(this.typeColumn);

      this.actionColumn = document.createElement("label");
      this.actionColumn.slot = "actions";
      this.actionColumn.setAttribute(
        "data-l10n-id",
        "applications-action-heading"
      );
      headerElement.appendChild(this.actionColumn);

      return headerElement;
    }

    /**
     * Sorts the items alphabetically by their label.
     *
     * @param {Array<ApplicationFileHandlerItemActionsMenuOption>} unorderedItems
     * @returns {Array<ApplicationFileHandlerItemActionsMenuOption>}
     */
    _sortItems(unorderedItems) {
      let comp = new Services.intl.Collator(undefined, {
        usage: "sort",
      });
      const textForNode = item => item.getAttribute("label");
      let multiplier = 1;
      return unorderedItems.sort(
        (a, b) => multiplier * comp.compare(textForNode(a), textForNode(b))
      );
    }

    filter() {
      this._rebuildView();
    }

    focusFilterBox() {
      this._filter.focus();
      this._filter.select();
    }

    // Changes

    // Whether or not we are currently storing the action selected by the user.
    // We use this to suppress notification-triggered updates to the list when
    // we make changes that may spawn such updates.
    // XXXgijs: this was definitely necessary when we changed feed preferences
    // from within _storeAction and its calltree. Now, it may still be
    // necessary, to avoid calling _rebuildView. bug 1499350 has more details.
    _storingAction = false;

    onSelectAction(aActionItem) {
      this._storingAction = true;

      try {
        this._storeAction(aActionItem);
      } finally {
        this._storingAction = false;
      }
    }

    _storeAction(aActionItem) {
      var handlerInfo = this.selectedHandlerListItem.handlerInfoWrapper;

      let action = parseInt(aActionItem.getAttribute("action"));

      // Set the preferred application handler.
      // We leave the existing preferred app in the list when we set
      // the preferred action to something other than useHelperApp so that
      // legacy datastores that don't have the preferred app in the list
      // of possible apps still include the preferred app in the list of apps
      // the user can choose to handle the type.
      if (action == Ci.nsIHandlerInfo.useHelperApp) {
        handlerInfo.preferredApplicationHandler = aActionItem.handlerApp;
      }

      // Set the "always ask" flag.
      if (action == Ci.nsIHandlerInfo.alwaysAsk) {
        handlerInfo.alwaysAskBeforeHandling = true;
      } else {
        handlerInfo.alwaysAskBeforeHandling = false;
      }

      // Set the preferred action.
      handlerInfo.preferredAction = action;

      handlerInfo.store();

      // Update the action label and image to reflect the new preferred action.
      this.selectedHandlerListItem.refreshAction();
    }

    manageApp(aEvent) {
      // Don't let the normal "on select action" handler get this event,
      // as we handle it specially ourselves.
      aEvent.stopPropagation();

      var handlerInfo = this.selectedHandlerListItem.handlerInfoWrapper;

      let onComplete = () => {
        // Rebuild the actions menu so that we revert to the previous selection,
        // or "Always ask" if the previous default application has been removed
        this.rebuildActionsMenu();

        // update the richlistitem too. Will be visible when selecting another row
        this.selectedHandlerListItem.refreshAction();
      };

      gSubDialog.open(
        "chrome://browser/content/preferences/dialogs/applicationManager.xhtml",
        { features: "resizable=no", closingCallback: onComplete },
        handlerInfo
      );
    }

    async chooseApp(aEvent) {
      // Don't let the normal "on select action" handler get this event,
      // as we handle it specially ourselves.
      aEvent.stopPropagation();

      var handlerApp;
      let chooseAppCallback = aHandlerApp => {
        // Rebuild the actions menu whether the user picked an app or canceled.
        // If they picked an app, we want to add the app to the menu and select it.
        // If they canceled, we want to go back to their previous selection.
        this.rebuildActionsMenu();

        // If the user picked a new app from the menu, select it.
        if (aHandlerApp) {
          let typeItem = this._list.selectedItem;
          let actionsMenu = typeItem.querySelector(".actionsMenu");
          let menuItems = actionsMenu.menupopup.childNodes;
          for (let i = 0; i < menuItems.length; i++) {
            let menuItem = menuItems[i];
            if (
              menuItem.handlerApp &&
              menuItem.handlerApp.equals(aHandlerApp)
            ) {
              actionsMenu.selectedIndex = i;
              this.onSelectAction(menuItem);
              break;
            }
          }
        }
      };

      if (AppConstants.platform == "win") {
        var params = {};
        var handlerInfo = this.selectedHandlerListItem.handlerInfoWrapper;

        params.mimeInfo = handlerInfo.wrappedHandlerInfo;
        params.title = await document.l10n.formatValue(
          "applications-select-helper"
        );
        if ("id" in handlerInfo.description) {
          params.description = await document.l10n.formatValue(
            handlerInfo.description.id,
            handlerInfo.description.args
          );
        } else {
          params.description = handlerInfo.typeDescription.raw;
        }
        params.filename = null;
        params.handlerApp = null;

        let onAppSelected = () => {
          if (this.isValidHandlerApp(params.handlerApp)) {
            handlerApp = params.handlerApp;

            // Add the app to the type's list of possible handlers.
            handlerInfo.addPossibleApplicationHandler(handlerApp);
          }

          chooseAppCallback(handlerApp);
        };

        gSubDialog.open(
          "chrome://global/content/appPicker.xhtml",
          { closingCallback: onAppSelected },
          params
        );
      } else {
        let winTitle = await document.l10n.formatValue(
          "applications-select-helper"
        );
        let fp = Cc["@mozilla.org/filepicker;1"].createInstance(
          Ci.nsIFilePicker
        );
        let fpCallback = aResult => {
          if (
            aResult == Ci.nsIFilePicker.returnOK &&
            fp.file &&
            this._isValidHandlerExecutable(fp.file)
          ) {
            handlerApp = Cc[
              "@mozilla.org/uriloader/local-handler-app;1"
            ].createInstance(Ci.nsILocalHandlerApp);
            handlerApp.name = getFileDisplayName(fp.file);
            handlerApp.executable = fp.file;

            // Add the app to the type's list of possible handlers.
            let handler = this.selectedHandlerListItem.handlerInfoWrapper;
            handler.addPossibleApplicationHandler(handlerApp);

            chooseAppCallback(handlerApp);
          }
        };

        // Prompt the user to pick an app.  If they pick one, and it's a valid
        // selection, then add it to the list of possible handlers.
        fp.init(window.browsingContext, winTitle, Ci.nsIFilePicker.modeOpen);
        fp.appendFilters(Ci.nsIFilePicker.filterApps);
        fp.open(fpCallback);
      }
    }

    /**
     * Rebuild the actions menu for the selected entry.  Gets called by
     * the richlistitem constructor when an entry in the list gets selected.
     */
    rebuildActionsMenu(
      typeItem = this._list.selectedItem,
      handlerInfo = this.selectedHandlerListItem.handlerInfoWrapper
    ) {
      var menu = typeItem.querySelector(".actionsMenu");
      var menuPopup = menu.menupopup;

      // Clear out existing items.
      while (menuPopup.hasChildNodes()) {
        menuPopup.removeChild(menuPopup.lastChild);
      }

      let internalMenuItem;
      // Add the "Open in Firefox" option for optional internal handlers.
      if (
        handlerInfo instanceof InternalHandlerInfoWrapper &&
        !handlerInfo.preventInternalViewing
      ) {
        internalMenuItem = document.createXULElement("menuitem");
        internalMenuItem.setAttribute(
          "action",
          Ci.nsIHandlerInfo.handleInternally
        );
        internalMenuItem.className = "menuitem-iconic";
        document.l10n.setAttributes(
          internalMenuItem,
          "applications-open-inapp"
        );
        internalMenuItem.setAttribute(APP_ICON_ATTR_NAME, "handleInternally");
        menuPopup.appendChild(internalMenuItem);
      }

      var askMenuItem = document.createXULElement("menuitem");
      askMenuItem.setAttribute("action", Ci.nsIHandlerInfo.alwaysAsk);
      askMenuItem.className = "menuitem-iconic";
      document.l10n.setAttributes(askMenuItem, "applications-always-ask");
      askMenuItem.setAttribute(APP_ICON_ATTR_NAME, "ask");
      menuPopup.appendChild(askMenuItem);

      // Create a menu item for saving to disk.
      // Note: this option isn't available to protocol types, since we don't know
      // what it means to save a URL having a certain scheme to disk.
      if (handlerInfo.wrappedHandlerInfo instanceof Ci.nsIMIMEInfo) {
        var saveMenuItem = document.createXULElement("menuitem");
        saveMenuItem.setAttribute("action", Ci.nsIHandlerInfo.saveToDisk);
        document.l10n.setAttributes(saveMenuItem, "applications-action-save");
        saveMenuItem.setAttribute(APP_ICON_ATTR_NAME, "save");
        saveMenuItem.className = "menuitem-iconic";
        menuPopup.appendChild(saveMenuItem);
      }

      // Add a separator to distinguish these items from the helper app items
      // that follow them.
      let menuseparator = document.createXULElement("menuseparator");
      menuPopup.appendChild(menuseparator);

      // Create a menu item for the OS default application, if any.
      if (handlerInfo.hasDefaultHandler) {
        var defaultMenuItem = document.createXULElement("menuitem");
        defaultMenuItem.setAttribute(
          "action",
          Ci.nsIHandlerInfo.useSystemDefault
        );
        // If an internal option is available, don't show the application
        // name for the OS default to prevent two options from appearing
        // that may both say "Firefox".
        if (internalMenuItem) {
          document.l10n.setAttributes(
            defaultMenuItem,
            "applications-use-os-default"
          );
          defaultMenuItem.setAttribute("image", ICON_URL_APP);
        } else {
          document.l10n.setAttributes(
            defaultMenuItem,
            "applications-use-app-default",
            {
              "app-name": handlerInfo.defaultDescription,
            }
          );
          let image = handlerInfo.iconURLForSystemDefault;
          if (image) {
            defaultMenuItem.setAttribute("image", image);
          }
        }

        menuPopup.appendChild(defaultMenuItem);
      }

      // Create menu items for possible handlers.
      let preferredApp = handlerInfo.preferredApplicationHandler;
      var possibleAppMenuItems = [];
      for (let possibleApp of handlerInfo.possibleApplicationHandlers.enumerate()) {
        if (!this.isValidHandlerApp(possibleApp)) {
          continue;
        }

        let menuItem = document.createXULElement("menuitem");
        menuItem.setAttribute("action", Ci.nsIHandlerInfo.useHelperApp);
        let label;
        if (possibleApp instanceof Ci.nsILocalHandlerApp) {
          label = getFileDisplayName(possibleApp.executable);
        } else {
          label = possibleApp.name;
        }
        document.l10n.setAttributes(menuItem, "applications-use-app", {
          "app-name": label,
        });
        let image = getIconURLForHandlerApp(possibleApp);
        if (image) {
          menuItem.setAttribute("image", image);
        }

        // Attach the handler app object to the menu item so we can use it
        // to make changes to the datastore when the user selects the item.
        menuItem.handlerApp = possibleApp;

        menuPopup.appendChild(menuItem);
        possibleAppMenuItems.push(menuItem);
      }
      // Add gio handlers
      if (gGIOService) {
        var gioApps = gGIOService.getAppsForURIScheme(handlerInfo.type);
        let possibleHandlers = handlerInfo.possibleApplicationHandlers;
        for (let handler of gioApps.enumerate(Ci.nsIHandlerApp)) {
          // OS handler share the same name, it's most likely the same app, skipping...
          if (handler.name == handlerInfo.defaultDescription) {
            continue;
          }
          // Check if the handler is already in possibleHandlers
          let appAlreadyInHandlers = false;
          for (let i = possibleHandlers.length - 1; i >= 0; --i) {
            let app = possibleHandlers.queryElementAt(i, Ci.nsIHandlerApp);
            // nsGIOMimeApp::Equals is able to compare with nsILocalHandlerApp
            if (handler.equals(app)) {
              appAlreadyInHandlers = true;
              break;
            }
          }
          if (!appAlreadyInHandlers) {
            let menuItem = document.createXULElement("menuitem");
            menuItem.setAttribute("action", Ci.nsIHandlerInfo.useHelperApp);
            document.l10n.setAttributes(menuItem, "applications-use-app", {
              "app-name": handler.name,
            });

            let image = getIconURLForHandlerApp(handler);
            if (image) {
              menuItem.setAttribute("image", image);
            }

            // Attach the handler app object to the menu item so we can use it
            // to make changes to the datastore when the user selects the item.
            menuItem.handlerApp = handler;

            menuPopup.appendChild(menuItem);
            possibleAppMenuItems.push(menuItem);
          }
        }
      }

      // Create a menu item for selecting a local application.
      let canOpenWithOtherApp = true;
      if (AppConstants.platform == "win") {
        // On Windows, selecting an application to open another application
        // would be meaningless so we special case executables.
        let executableType = Cc["@mozilla.org/mime;1"]
          .getService(Ci.nsIMIMEService)
          .getTypeFromExtension("exe");
        canOpenWithOtherApp = handlerInfo.type != executableType;
      }
      if (canOpenWithOtherApp) {
        let menuItem = document.createXULElement("menuitem");
        menuItem.className = "choose-app-item";
        menuItem.addEventListener("command", function (e) {
          AppFileHandler.chooseApp(e);
        });
        document.l10n.setAttributes(menuItem, "applications-use-other");
        menuPopup.appendChild(menuItem);
      }

      // Create a menu item for managing applications.
      if (possibleAppMenuItems.length) {
        let menuItem = document.createXULElement("menuseparator");
        menuPopup.appendChild(menuItem);
        menuItem = document.createXULElement("menuitem");
        menuItem.className = "manage-app-item";
        menuItem.addEventListener("command", function (e) {
          AppFileHandler.manageApp(e);
        });
        document.l10n.setAttributes(menuItem, "applications-manage-app");
        menuPopup.appendChild(menuItem);
      }

      // Select the item corresponding to the preferred action.  If the always
      // ask flag is set, it overrides the preferred action.  Otherwise we pick
      // the item identified by the preferred action (when the preferred action
      // is to use a helper app, we have to pick the specific helper app item).
      if (handlerInfo.alwaysAskBeforeHandling) {
        menu.selectedItem = askMenuItem;
      } else {
        // The nsHandlerInfoAction enumeration values in nsIHandlerInfo identify
        // the actions the application can take with content of various types.
        // But since we've stopped support for plugins, there's no value
        // identifying the "use plugin" action, so we use this constant instead.
        const kActionUsePlugin = 5;

        switch (handlerInfo.preferredAction) {
          case Ci.nsIHandlerInfo.handleInternally:
            if (internalMenuItem) {
              menu.selectedItem = internalMenuItem;
            } else {
              console.error("No menu item defined to set!");
            }
            break;
          case Ci.nsIHandlerInfo.useSystemDefault:
            // We might not have a default item if we're not aware of an
            // OS-default handler for this type:
            menu.selectedItem = defaultMenuItem || askMenuItem;
            break;
          case Ci.nsIHandlerInfo.useHelperApp:
            if (preferredApp) {
              let preferredItem = possibleAppMenuItems.find(v =>
                v.handlerApp.equals(preferredApp)
              );
              if (preferredItem) {
                menu.selectedItem = preferredItem;
              } else {
                // This shouldn't happen, but let's make sure we end up with a
                // selected item:
                let possible = possibleAppMenuItems
                  .map(v => v.handlerApp && v.handlerApp.name)
                  .join(", ");
                console.error(
                  new Error(
                    `Preferred handler for ${handlerInfo.type} not in list of possible handlers!? (List: ${possible})`
                  )
                );
                menu.selectedItem = askMenuItem;
              }
            }
            break;
          case kActionUsePlugin:
            // We no longer support plugins, select "ask" instead:
            menu.selectedItem = askMenuItem;
            break;
          case Ci.nsIHandlerInfo.saveToDisk:
            menu.selectedItem = saveMenuItem;
            break;
        }
      }
    }

    /**
     * Whether or not the given handler app is valid.
     *
     * @param aHandlerApp {nsIHandlerApp} the handler app in question
     * @returns {boolean} whether or not it's valid
     */
    isValidHandlerApp(aHandlerApp) {
      if (!aHandlerApp) {
        return false;
      }

      if (aHandlerApp instanceof Ci.nsILocalHandlerApp) {
        return this._isValidHandlerExecutable(aHandlerApp.executable);
      }

      if (aHandlerApp instanceof Ci.nsIWebHandlerApp) {
        return aHandlerApp.uriTemplate;
      }

      if (aHandlerApp instanceof Ci.nsIGIOMimeApp) {
        return aHandlerApp.command;
      }
      if (aHandlerApp instanceof Ci.nsIGIOHandlerApp) {
        return aHandlerApp.id;
      }

      return false;
    }

    _isValidHandlerExecutable(aExecutable) {
      let leafName;
      if (AppConstants.platform == "win") {
        leafName = `${AppConstants.MOZ_APP_NAME}.exe`;
      } else if (AppConstants.platform == "macosx") {
        leafName = AppConstants.MOZ_MACBUNDLE_NAME;
      } else {
        leafName = `${AppConstants.MOZ_APP_NAME}-bin`;
      }
      return (
        aExecutable &&
        aExecutable.exists() &&
        aExecutable.isExecutable() &&
        // XXXben - we need to compare this with the running instance executable
        //          just don't know how to do that via script...
        // XXXmano TBD: can probably add this to nsIShellService
        aExecutable.leafName != leafName
      );
    }
  })();
})();
