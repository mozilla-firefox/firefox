/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import { MozSelect } from 'moz-src:///toolkit/content/widgets/moz-select/moz-select.mjs';*/
/** @import { MozBoxGroup } from 'chrome://global/content/elements/moz-box-group.mjs'; */
/** @import { AsyncSettingHandler } from 'chrome://global/content/preferences/AsyncSetting.mjs'; */

/* import-globals-from extensionControlled.js */
/* import-globals-from preferences.js */
/* import-globals-from /toolkit/mozapps/preferences/fontbuilder.js */
/* import-globals-from /browser/base/content/aboutDialog-appUpdater.js */

/**
 * @import { Setting } from "chrome://global/content/preferences/Setting.mjs"
 */

/**
 * Imports a module into this window's global.
 *
 * These imports spin the event loop until the module graph has been fetched, so
 * the tab can be closed while one of them is in flight. Once that has happened
 * there is nothing left to set up, and evaluating the top level code of another
 * module against the torn down window only produces errors, so stop importing.
 *
 * @param {string} uri
 * @returns {object}
 */
function importIntoWindow(uri) {
  if (window.closed) {
    return {};
  }
  return ChromeUtils.importESModule(uri, { global: "current" });
}

const { Multilingual } = importIntoWindow(
  "chrome://browser/content/preferences/config/languages.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  BackgroundUpdate: "resource://gre/modules/BackgroundUpdate.sys.mjs",
  UpdateListener: "resource://gre/modules/UpdateListener.sys.mjs",
  LinkPreview: "moz-src:///browser/components/genai/LinkPreview.sys.mjs",
  MigrationUtils:
    "moz-src:///browser/components/migration/MigrationUtils.sys.mjs",
  TranslationsParent: "resource://gre/actors/TranslationsParent.sys.mjs",
  TranslationsUtils:
    "chrome://global/content/translations/TranslationsUtils.mjs",
  FormAutofillPreferences:
    "resource://autofill/FormAutofillPreferences.sys.mjs",
});

importIntoWindow(
  "chrome://browser/content/preferences/config/accessibility.mjs"
);
importIntoWindow(
  "chrome://browser/content/preferences/config/about-firefox.mjs"
);
importIntoWindow("chrome://browser/content/preferences/config/appearance.mjs");
importIntoWindow(
  "chrome://browser/content/preferences/config/tabs-browsing.mjs"
);

// Strings to identify ExtensionSettingsStore overrides
const CONTAINERS_KEY = "privacy.containers";

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
  { id: "browser.privatebrowsing.autostart", type: "bool" },

  // AI Controls, these pref values can affect settings on the main pane and
  // have base Settings here
  { id: "browser.ai.control.default", type: "string" },
  { id: "browser.ai.control.translations", type: "string" },
  { id: "browser.ai.control.pdfjsAltText", type: "string" },
  { id: "browser.ai.control.smartTabGroups", type: "string" },
  { id: "browser.ai.control.linkPreviewKeyPoints", type: "string" },
  { id: "browser.ai.control.speechRecognition", type: "string" },
  { id: "browser.ai.control.generatedWidgets", type: "string" },
  { id: "browser.ai.control.sidebarChatbot", type: "string" },
  { id: "browser.ai.control.smartWindow", type: "string" },
  { id: "browser.ml.chat.provider", type: "string" },

  // Update
  { id: "browser.preferences.advanced.selectedTabIndex", type: "int" },
  { id: "browser.search.update", type: "bool" },

  {
    id: "privacy.userContext.newTabContainerOnLeftClick.enabled",
    type: "bool",
  },
]);

Preferences.addSetting({
  id: "privateBrowsingAutoStart",
  pref: "browser.privatebrowsing.autostart",
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
Preferences.addSetting({
  id: "chatbotProvider",
  pref: "browser.ml.chat.provider",
});

/**
 * @param {string} id - ID of {@link SettingGroup} custom element.
 */
function initSettingGroup(id) {
  /** @type {SettingGroup[]} */
  let groups = document.querySelectorAll(`setting-group[groupid=${id}]`);
  let config;
  try {
    config = SettingGroupManager.get(id);
  } catch (e) {
    // Downstream browsers (e.g. Tor) may exclude extensions that
    // register some setting groups. Treat missing as no-op, not error.
    config = null;
  }
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

var gMainPane = {
  /**
   * Initialization of gMainPane.
   */
  init() {
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

    // Listen for window unload so we can remove our preference observers.
    window.addEventListener("unload", this);

    // Notify observers that the UI is now ready
    Services.obs.notifyObservers(window, "main-pane-loaded");
  },

  handleSubcategory(subcategory) {
    if (Services.policies && !Services.policies.isAllowed("profileImport")) {
      return false;
    }
    if (subcategory == "migrate") {
      this.showMigrationWizardDialog({
        entrypoint: this.getMigrationEntrypoint(),
      });
      return true;
    }

    if (subcategory == "migrate-autoclose") {
      this.showMigrationWizardDialog({
        closeTabWhenDone: true,
        entrypoint: this.getMigrationEntrypoint(),
      });
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
    let url = await FxAccounts.config.promiseConnectAccountURI(
      "sync",
      "dev-edition-setup"
    );
    let accountsTab = win.gBrowser.addWebTab(url);
    win.gBrowser.selectedTab = accountsTab;
  },

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
    let messageBarContainer = document.getElementById("confirmBrowserLanguage");

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

    messageBarContainer.textContent = "";

    for (let i = 0; i < messages.length; i++) {
      let messageBar = document.createElement("moz-message-bar");
      messageBar.setAttribute("type", "info");
      messageBar.setAttribute("data-l10n-attrs", "message");
      messageBar.setAttribute("message", messages[i]);
      if (i == 0 && Services.intl.getScriptDirection(locales[0]) === "rtl") {
        messageBar.setAttribute("dir", "rtl");
      }

      let button = document.createXULElement("button");
      button.addEventListener(
        "command",
        gMainPane.confirmBrowserLanguageChange
      );
      button.setAttribute("locales", locales.join(","));
      button.setAttribute("label", buttonLabels[i]);
      button.setAttribute("slot", "actions");
      messageBar.appendChild(button);

      messageBarContainer.appendChild(messageBar);
    }

    messageBarContainer.hidden = false;
    gMainPane.selectedLocalesForRestart = locales;
  },

  hideConfirmLanguageChangeMessageBar() {
    let messageBarContainer = document.getElementById("confirmBrowserLanguage");
    messageBarContainer.hidden = true;
    messageBarContainer.textContent = "";
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
      "chrome,resizable,toolbar,centerscreen"
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

    if (!selected) {
      // No locales were selected. Cancel the operation.
      return;
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
   * Returns the entrypoint MigrationUtils.showMigrationWizard was called with
   * when it opened about:preferences, if any.
   *
   * @returns {string}
   */
  getMigrationEntrypoint() {
    return (
      MigrationUtils.consumePendingPreferencesEntrypoint() ??
      MigrationUtils.MIGRATION_ENTRYPOINTS.PREFERENCES
    );
  },

  /**
   * Displays the migration wizard dialog in an HTML dialog.
   *
   * @param {object} [options]
   * @param {boolean} [options.closeTabWhenDone=false]
   *   True if the tab should close once the dialog is closed.
   * @param {string} [options.entrypoint]
   *   The MigrationUtils.MIGRATION_ENTRYPOINTS value for where the wizard
   *   was opened from.
   */
  async showMigrationWizardDialog({
    closeTabWhenDone = false,
    entrypoint = MigrationUtils.MIGRATION_ENTRYPOINTS.PREFERENCES,
  } = {}) {
    let migrationWizardDialog = document.getElementById(
      "migrationWizardDialog"
    );

    if (migrationWizardDialog.open) {
      return;
    }

    await customElements.whenDefined("migration-wizard");

    // Only create the migration-wizard once.
    if (!migrationWizardDialog.firstElementChild) {
      let wizard = document.createElement("migration-wizard");
      wizard.toggleAttribute("dialog-mode", true);
      migrationWizardDialog.appendChild(wizard);
      migrationWizardDialog.addEventListener(
        "MigrationWizard:Close",
        function (e) {
          e.currentTarget.close();
        }
      );
    }
    migrationWizardDialog.firstElementChild.setAttribute(
      "entrypoint",
      entrypoint
    );
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

  // EventListener

  handleEvent(aEvent) {
    if (aEvent.type == "unload") {
      this.destroy();
      if (AppConstants.MOZ_UPDATER) {
        onUnload();
      }
    }
  },
};
