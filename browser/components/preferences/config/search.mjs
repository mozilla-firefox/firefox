/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { Preferences } from "chrome://global/content/preferences/Preferences.mjs";
import { SettingGroupManager } from "chrome://browser/content/preferences/config/SettingGroupManager.mjs";

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

const lazy = XPCOMUtils.declareLazy({
  AddonSearchEngine:
    "moz-src:///toolkit/components/search/AddonSearchEngine.sys.mjs",
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  QuickSuggest: "moz-src:///browser/components/urlbar/QuickSuggest.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarUtils: "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs",
});

/**
 * @import { SearchEngine } from "moz-src:///toolkit/components/search/SearchEngine.sys.mjs"
 * @import { SettingControlConfig } from "chrome://browser/content/preferences/widgets/setting-control.mjs"
 */

Preferences.addAll([
  { id: "browser.search.suggest.enabled", type: "bool" },
  { id: "browser.urlbar.suggest.searches", type: "bool" },
  { id: "browser.search.suggest.enabled.private", type: "bool" },
  { id: "browser.urlbar.showSearchSuggestionsFirst", type: "bool" },
  { id: "browser.urlbar.showSearchTerms.enabled", type: "bool" },
  { id: "browser.urlbar.showSearchTerms.featureGate", type: "bool" },
  { id: "browser.search.separatePrivateDefault", type: "bool" },
  { id: "browser.search.separatePrivateDefault.ui.enabled", type: "bool" },
  { id: "browser.urlbar.suggest.trending", type: "bool" },
  { id: "browser.urlbar.trending.featureGate", type: "bool" },
  { id: "browser.urlbar.recentsearches.featureGate", type: "bool" },
  { id: "browser.urlbar.suggest.recentsearches", type: "bool" },
  { id: "browser.urlbar.scotchBonnet.enableOverride", type: "bool" },

  // Suggest Section.
  { id: "browser.urlbar.suggest.bookmark", type: "bool" },
  { id: "browser.urlbar.suggest.clipboard", type: "bool" },
  { id: "browser.urlbar.clipboard.featureGate", type: "bool" },
  { id: "browser.urlbar.suggest.history", type: "bool" },
  { id: "browser.urlbar.suggest.openpage", type: "bool" },
  { id: "browser.urlbar.suggest.topsites", type: "bool" },
  { id: "browser.urlbar.suggest.engines", type: "bool" },
  { id: "browser.urlbar.quickactions.showPrefs", type: "bool" },
  { id: "browser.urlbar.suggest.quickactions", type: "bool" },
  { id: "browser.urlbar.quicksuggest.settingsUi", type: "int" },
  { id: "browser.urlbar.quicksuggest.enabled", type: "bool" },
  { id: "browser.urlbar.suggest.quicksuggest.all", type: "bool" },
  { id: "browser.urlbar.suggest.quicksuggest.sponsored", type: "bool" },
  { id: "browser.urlbar.quicksuggest.online.enabled", type: "bool" },
]);

/**
 * Gets the icon to use for a particular engine, falling back to the placeholder
 * if necessary.
 *
 * @param {SearchEngine} engine
 * @param {number} [width]
 *   The display width of the icon. @see {SearchEngine.getIconURL}
 */
async function getEngineIcon(engine, width) {
  let iconURL = await engine.getIconURL(width);

  return (
    iconURL ??
    (window.devicePixelRatio > 1
      ? "chrome://browser/skin/search-engine-placeholder@2x.png"
      : "chrome://browser/skin/search-engine-placeholder.png")
  );
}

/**
 * Generates the config needed to populate the dropdowns for the user's
 * default search engine and separate private default search engine.
 *
 * @param {object} options
 *   Options for creating the config.
 * @param {string} options.settingId
 *   The id for the particular setting.
 * @param {() => Promise<SearchEngine>} options.getEngine
 *   The method used to get the engine from the Search Service.
 * @param {(id: string) => Promise<void>} options.setEngine
 *   The method used to set a new engine.
 * @returns {typeof Preferences.AsyncSetting}
 */
function createSearchEngineConfig({ settingId, getEngine, setEngine }) {
  return class extends Preferences.AsyncSetting {
    static id = settingId;

    /** @type {Partial<SettingControlConfig>} */
    defaultGetControlConfig = { options: [] };

    async get() {
      let engine = await getEngine();
      return engine?.id;
    }

    /** @param {string} id */
    async set(id) {
      await setEngine(id);
    }

    async getControlConfig() {
      let engines = await lazy.SearchService.getVisibleEngines();
      let optionsInfo = await Promise.allSettled(
        engines.map(async engine => {
          let url = await getEngineIcon(engine);
          return {
            value: engine.id,
            controlAttrs: { label: engine.name },
            iconSrc: url,
          };
        })
      );

      /** @type {Partial<SettingControlConfig>} */
      return {
        options: optionsInfo
          .filter(o => o.status == "fulfilled")
          .map(o => o.value),
      };
    }

    setup() {
      Services.obs.addObserver(this, lazy.SearchUtils.TOPIC_ENGINE_MODIFIED);
      return () =>
        Services.obs.removeObserver(
          this,
          lazy.SearchUtils.TOPIC_ENGINE_MODIFIED
        );
    }

    /**
     * @param {nsISupports} _subject
     * @param {"browser-search-service"|"browser-search-engine-modified"} topic
     * @param {string} _data
     */
    observe(_subject, topic, _data) {
      if (topic == lazy.SearchUtils.TOPIC_ENGINE_MODIFIED) {
        // Always emit change for any change that could affect the engine list
        // or default.
        this.emitChange();
      }
    }
  };
}

Preferences.addSetting(
  createSearchEngineConfig({
    settingId: "defaultEngineNormal",
    getEngine: async () => {
      let engine = await lazy.SearchService.getDefault();
      if (!engine) {
        throw new Error("Unable to get default engine.");
      }
      return engine;
    },
    setEngine: async id => {
      let engine = lazy.SearchService.getEngineById(id);
      if (!engine) {
        throw new Error("Unable to get engine by id.");
      }
      await lazy.SearchService.setDefault(
        engine,
        lazy.SearchService.CHANGE_REASON.USER
      );
    },
  })
);

Preferences.addSetting({
  id: "scotchBonnetEnabled",
  pref: "browser.urlbar.scotchBonnet.enableOverride",
});

Preferences.addSetting({
  id: "showSearchTermsFeatureGate",
  pref: "browser.urlbar.showSearchTerms.featureGate",
});

Preferences.addSetting({
  id: "searchShowSearchTermCheckbox",
  pref: "browser.urlbar.showSearchTerms.enabled",
  deps: ["scotchBonnetEnabled", "showSearchTermsFeatureGate"],
  visible: ({ scotchBonnetEnabled, showSearchTermsFeatureGate }) => {
    if (lazy.CustomizableUI.getPlacementOfWidget("search-container")) {
      return false;
    }
    return showSearchTermsFeatureGate.value || scotchBonnetEnabled.value;
  },
  setup: onChange => {
    // Add observer of CustomizableUI as showSearchTerms checkbox should be
    // hidden while searchbar is enabled.
    /** @type {Parameters<typeof lazy.CustomizableUI.addListener>[0]} */
    let customizableUIListener = {
      onWidgetAfterDOMChange: node => {
        if (node.id == "search-container") {
          onChange();
        }
      },
    };
    lazy.CustomizableUI.addListener(customizableUIListener);
    return () => lazy.CustomizableUI.removeListener(customizableUIListener);
  },
});

Preferences.addSetting({
  id: "separatePrivateDefaultUI",
  pref: "browser.search.separatePrivateDefault.ui.enabled",
});

Preferences.addSetting({
  id: "browserSeparateDefaultEngine",
  pref: "browser.search.separatePrivateDefault",
  deps: ["separatePrivateDefaultUI"],
  visible: ({ separatePrivateDefaultUI }) => {
    return separatePrivateDefaultUI.value;
  },
});

Preferences.addSetting(
  createSearchEngineConfig({
    settingId: "defaultPrivateEngine",
    getEngine: async () => {
      let engine = await lazy.SearchService.getDefaultPrivate();
      if (!engine) {
        throw new Error("Unable to get default private engine.");
      }
      return engine;
    },
    setEngine: async id => {
      let engine = lazy.SearchService.getEngineById(id);
      if (!engine) {
        throw new Error("Unable to get engine by id.");
      }
      await lazy.SearchService.setDefaultPrivate(
        engine,
        lazy.SearchService.CHANGE_REASON.USER
      );
    },
  })
);

Preferences.addSetting({
  id: "searchSuggestionsEnabledPref",
  pref: "browser.search.suggest.enabled",
});

Preferences.addSetting({
  id: "permanentPBEnabledPref",
  pref: "browser.privatebrowsing.autostart",
});

Preferences.addSetting({
  id: "urlbarSuggestionsEnabledPref",
  pref: "browser.urlbar.suggest.searches",
});

Preferences.addSetting({
  id: "trendingFeaturegatePref",
  pref: "browser.urlbar.trending.featureGate",
});

// The show search suggestion box behaves differently depending on whether the
// separate search bar is shown. When the separate search bar is shown, it
// controls just the search suggestion preference, and the
// `urlBarSuggestionCheckbox` handles the urlbar suggestions. When the separate
// search bar is not shown, this checkbox toggles both preferences to ensure
// that the urlbar suggestion preference is set correctly, since that will be
// the only bar visible.
Preferences.addSetting({
  id: "suggestionsInSearchFieldsCheckbox",
  deps: ["searchSuggestionsEnabledPref", "urlbarSuggestionsEnabledPref"],
  get(_, deps) {
    let searchBarVisible =
      !!lazy.CustomizableUI.getPlacementOfWidget("search-container");
    return (
      deps.searchSuggestionsEnabledPref.value &&
      (searchBarVisible || deps.urlbarSuggestionsEnabledPref.value)
    );
  },
  set(newCheckedValue, deps) {
    let searchBarVisible =
      !!lazy.CustomizableUI.getPlacementOfWidget("search-container");
    if (!searchBarVisible) {
      deps.urlbarSuggestionsEnabledPref.value = newCheckedValue;
    }
    deps.searchSuggestionsEnabledPref.value = newCheckedValue;
    return newCheckedValue;
  },
});

Preferences.addSetting({
  id: "urlBarSuggestionCheckbox",
  deps: [
    "urlbarSuggestionsEnabledPref",
    "suggestionsInSearchFieldsCheckbox",
    "searchSuggestionsEnabledPref",
    "permanentPBEnabledPref",
  ],
  get: (_, deps) => {
    let searchBarVisible =
      !!lazy.CustomizableUI.getPlacementOfWidget("search-container");
    if (
      deps.suggestionsInSearchFieldsCheckbox.value &&
      searchBarVisible &&
      deps.urlbarSuggestionsEnabledPref.value
    ) {
      return true;
    }
    return false;
  },
  set: (newCheckedValue, deps, setting) => {
    if (setting.disabled) {
      deps.urlbarSuggestionsEnabledPref.value = false;
      return false;
    }

    let searchBarVisible =
      !!lazy.CustomizableUI.getPlacementOfWidget("search-container");
    if (deps.suggestionsInSearchFieldsCheckbox.value && searchBarVisible) {
      deps.urlbarSuggestionsEnabledPref.value = newCheckedValue;
    }
    return newCheckedValue;
  },
  setup: onChange => {
    // Add observer of CustomizableUI as checkbox should be hidden while
    // searchbar is enabled.
    /** @type {Parameters<typeof lazy.CustomizableUI.addListener>[0]} */
    let customizableUIListener = {
      onWidgetAfterDOMChange: node => {
        if (node.id == "search-container") {
          onChange();
        }
      },
    };
    lazy.CustomizableUI.addListener(customizableUIListener);
    return () => lazy.CustomizableUI.removeListener(customizableUIListener);
  },
  disabled: deps => {
    return (
      !deps.searchSuggestionsEnabledPref.value ||
      deps.permanentPBEnabledPref.value
    );
  },
  visible: () => {
    let searchBarVisible =
      !!lazy.CustomizableUI.getPlacementOfWidget("search-container");
    return searchBarVisible;
  },
});

Preferences.addSetting({
  id: "showSearchSuggestionsFirstCheckbox",
  pref: "browser.urlbar.showSearchSuggestionsFirst",
  deps: [
    "suggestionsInSearchFieldsCheckbox",
    "urlbarSuggestionsEnabledPref",
    "searchSuggestionsEnabledPref",
    "permanentPBEnabledPref",
  ],
  get: (newCheckedValue, deps) => {
    if (!deps.searchSuggestionsEnabledPref.value) {
      return false;
    }
    return deps.urlbarSuggestionsEnabledPref.value ? newCheckedValue : false;
  },
  disabled: deps => {
    return (
      !deps.suggestionsInSearchFieldsCheckbox.value ||
      !deps.urlbarSuggestionsEnabledPref.value ||
      deps.permanentPBEnabledPref.value
    );
  },
});

Preferences.addSetting({
  id: "showSearchSuggestionsPrivateWindowsCheckbox",
  pref: "browser.search.suggest.enabled.private",
  deps: ["searchSuggestionsEnabledPref"],
  disabled: deps => {
    return !deps.searchSuggestionsEnabledPref.value;
  },
});

Preferences.addSetting({
  id: "showTrendingSuggestionsCheckbox",
  pref: "browser.urlbar.suggest.trending",
  deps: [
    "searchSuggestionsEnabledPref",
    "permanentPBEnabledPref",
    // Required to dynamically update the disabled state when the default engine is changed.
    "defaultEngineNormal",
    "trendingFeaturegatePref",
  ],
  visible: deps => deps.trendingFeaturegatePref.value,
  disabled: deps => {
    let trendingSupported =
      lazy.SearchService.defaultEngine?.supportsResponseType(
        lazy.SearchUtils.URL_TYPE.TRENDING_JSON
      );
    return (
      !deps.searchSuggestionsEnabledPref.value ||
      deps.permanentPBEnabledPref.value ||
      !trendingSupported
    );
  },
});

Preferences.addSetting({
  id: "urlBarSuggestionPermanentPBMessage",
  deps: ["urlBarSuggestionCheckbox", "permanentPBEnabledPref"],
  visible: deps => {
    return (
      deps.urlBarSuggestionCheckbox.visible && deps.permanentPBEnabledPref.value
    );
  },
});

Preferences.addSetting({
  id: "quickSuggestEnabledPref",
  pref: "browser.urlbar.quicksuggest.enabled",
});

Preferences.addSetting({
  id: "quickSuggestSettingsUiPref",
  pref: "browser.urlbar.quicksuggest.settingsUi",
});

Preferences.addSetting({
  id: "nimbusListener",
  setup(onChange) {
    window.NimbusFeatures.urlbar.onUpdate(onChange);
    return () => window.NimbusFeatures.urlbar.offUpdate(onChange);
  },
});

Preferences.addSetting({
  id: "locationBarGroupHeader",
  deps: [
    "quickSuggestEnabledPref",
    "quickSuggestSettingsUiPref",
    "nimbusListener",
  ],
  getControlConfig(config) {
    let l10nId =
      lazy.UrlbarPrefs.get("quickSuggestEnabled") &&
      lazy.UrlbarPrefs.get("quickSuggestSettingsUi") !=
        lazy.QuickSuggest.SETTINGS_UI.NONE
        ? "addressbar-header-firefox-suggest-2"
        : "addressbar-header-1";

    /** @type {Partial<SettingControlConfig>} */
    return { ...config, l10nId };
  },
});

Preferences.addSetting({
  id: "historySuggestion",
  pref: "browser.urlbar.suggest.history",
});

Preferences.addSetting({
  id: "bookmarkSuggestion",
  pref: "browser.urlbar.suggest.bookmark",
});

Preferences.addSetting({
  id: "clipboardFeaturegate",
  pref: "browser.urlbar.clipboard.featureGate",
});

Preferences.addSetting({
  id: "clipboardSuggestion",
  pref: "browser.urlbar.suggest.clipboard",
  deps: ["clipboardFeaturegate"],
  visible: deps => {
    return deps.clipboardFeaturegate.value;
  },
});

Preferences.addSetting({
  id: "openpageSuggestion",
  pref: "browser.urlbar.suggest.openpage",
});

Preferences.addSetting({
  id: "topSitesSuggestion",
  pref: "browser.urlbar.suggest.topsites",
});

Preferences.addSetting({
  id: "enableRecentSearchesFeatureGate",
  pref: "browser.urlbar.recentsearches.featureGate",
});

Preferences.addSetting({
  id: "enableRecentSearches",
  pref: "browser.urlbar.suggest.recentsearches",
  deps: ["enableRecentSearchesFeatureGate"],
  visible: deps => {
    return deps.enableRecentSearchesFeatureGate.value;
  },
});

Preferences.addSetting({
  id: "enginesSuggestion",
  pref: "browser.urlbar.suggest.engines",
});

Preferences.addSetting({
  id: "quickActionsShowPrefs",
  pref: "browser.urlbar.quickactions.showPrefs",
});

Preferences.addSetting({
  id: "enableQuickActions",
  pref: "browser.urlbar.suggest.quickactions",
  deps: ["quickActionsShowPrefs", "scotchBonnetEnabled"],
  visible: deps => {
    return deps.quickActionsShowPrefs.value || deps.scotchBonnetEnabled.value;
  },
});

function determineSuggestionSettingsVisibility() {
  if (!lazy.UrlbarPrefs.get("quickSuggestEnabled")) {
    return false;
  } else if (
    lazy.UrlbarPrefs.get("quickSuggestSettingsUi") ==
    lazy.QuickSuggest.SETTINGS_UI.NONE
  ) {
    return false;
  }
  return true;
}

Preferences.addSetting({
  id: "firefoxSuggestAll",
  pref: "browser.urlbar.suggest.quicksuggest.all",
  deps: [
    "quickSuggestEnabledPref",
    "quickSuggestSettingsUiPref",
    "nimbusListener",
  ],
  visible: determineSuggestionSettingsVisibility,
});

Preferences.addSetting({
  id: "firefoxSuggestSponsored",
  pref: "browser.urlbar.suggest.quicksuggest.sponsored",
  deps: [
    "firefoxSuggestAll",
    "quickSuggestEnabledPref",
    "quickSuggestSettingsUiPref",
    "nimbusListener",
  ],
  visible: determineSuggestionSettingsVisibility,
  disabled: deps => {
    return !deps.firefoxSuggestAll.value;
  },
});

Preferences.addSetting({
  id: "firefoxSuggestOnlineEnabledToggle",
  pref: "browser.urlbar.quicksuggest.online.enabled",
  deps: [
    "firefoxSuggestAll",
    "quickSuggestEnabledPref",
    "quickSuggestSettingsUiPref",
    "nimbusListener",
  ],
  visible: () => {
    if (!lazy.UrlbarPrefs.get("quickSuggestEnabled")) {
      return false;
    } else if (
      lazy.UrlbarPrefs.get("quickSuggestSettingsUi") ==
      lazy.QuickSuggest.SETTINGS_UI.NONE
    ) {
      return false;
    }
    return (
      lazy.UrlbarPrefs.get("quickSuggestSettingsUi") ==
      lazy.QuickSuggest.SETTINGS_UI.FULL
    );
  },
  disabled: deps => {
    return !deps.firefoxSuggestAll.value;
  },
});

Preferences.addSetting(
  class extends Preferences.AsyncSetting {
    static id = "restoreDismissedSuggestions";
    setup() {
      Services.obs.addObserver(
        this.emitChange,
        "quicksuggest-dismissals-changed"
      );
      return () => {
        Services.obs.removeObserver(
          this.emitChange,
          "quicksuggest-dismissals-changed"
        );
      };
    }
    async disabled() {
      return !(await lazy.QuickSuggest.canClearDismissedSuggestions());
    }
    onUserClick() {
      lazy.QuickSuggest.clearDismissedSuggestions();
    }
  }
);

Preferences.addSetting({
  id: "dismissedSuggestionsDescription",
  deps: [
    "firefoxSuggestAll",
    "quickSuggestEnabledPref",
    "quickSuggestSettingsUiPref",
    "nimbusListener",
  ],
  visible: determineSuggestionSettingsVisibility,
});

/**
 * @type {?() => void}
 *   Enables notification for an engine update from outside the setting.
 */
let searchEngineUpdateNotifier;
Preferences.addSetting(
  /** @type {{ _engineUpdateTriggered: boolean, _emitChange: ?Function } & SettingConfig} */ ({
    id: "updateSearchEngineSuccess",
    _engineUpdateTriggered: false,
    _emitChange: null,
    setup(emitChange) {
      this._emitChange = emitChange;
      searchEngineUpdateNotifier = () => {
        this._engineUpdateTriggered = true;
        emitChange();
      };
      return () => {
        searchEngineUpdateNotifier = null;
        this._emitChange = null;
      };
    },
    onMessageBarDismiss(e) {
      e.preventDefault();
      this._engineUpdateTriggered = false;
      this._emitChange?.();
    },
    visible() {
      return this._engineUpdateTriggered;
    },
  })
);

/**
 * Creates an AsyncSetting to handle an individual item in the search engine
 * list.
 *
 * @param {string} settingId
 * @param {SearchEngine} engine
 */
function EngineListItemSetting(settingId, engine) {
  return class extends Preferences.AsyncSetting {
    static id = settingId;

    setup() {
      /** @type {(subject: {wrappedJSObject: SearchEngine}, topic: string, data: string) => void} */
      let onTargetEngineChanged = (subject, _topic, data) => {
        if (
          (data == lazy.SearchUtils.MODIFIED_TYPE.CHANGED ||
            data == lazy.SearchUtils.MODIFIED_TYPE.ICON_CHANGED) &&
          subject.wrappedJSObject == engine
        ) {
          this.emitChange();
        }
      };

      Services.obs.addObserver(
        onTargetEngineChanged,
        lazy.SearchUtils.TOPIC_ENGINE_MODIFIED
      );
      return () =>
        Services.obs.removeObserver(
          onTargetEngineChanged,
          lazy.SearchUtils.TOPIC_ENGINE_MODIFIED
        );
    }

    async getControlConfig() {
      /** @type {Partial<SettingControlConfig>} */
      return {
        // 24 is the same size as `--icon-size-large`.
        iconSrc: await getEngineIcon(engine, 24),
        controlAttrs: {
          class: engine.hidden ? "description-deemphasized" : "",
          label: engine.name,
          description: engine.aliases.join(", "),
          layout: "medium-icon",
        },
      };
    }
  };
}

Preferences.addSetting({
  id: "addEngineButton",
  onUserClick() {
    window.gSubDialog.open(
      "chrome://browser/content/search/addEngine.xhtml",
      { features: "resizable=no, modal=yes" },
      { mode: "NEW" }
    );
  },
});

/**
 * @param {SettingConfig} config
 */
function maybeMakeSetting(config) {
  if (!Preferences.getSetting(config.id)) {
    Preferences.addSetting(config);
  }
}

/**
 * Creates a Setting to handle an individual toggle within the list item for
 * a search engine in the engine list.
 *
 * @param {string} toggleId
 * @param {SearchEngine} engine
 * @returns {SettingConfig}
 */
function ToggleSetting(toggleId, engine) {
  return {
    id: toggleId,
    setup(emitChange) {
      /** @type {(subject: {wrappedJSObject: SearchEngine}, topic: string, data: string) => void} */
      let onTargetEngineChanged = (subject, _topic, data) => {
        if (
          (data == lazy.SearchUtils.MODIFIED_TYPE.CHANGED ||
            data == lazy.SearchUtils.MODIFIED_TYPE.ICON_CHANGED) &&
          subject.wrappedJSObject == engine
        ) {
          emitChange();
        }
      };

      Services.obs.addObserver(
        onTargetEngineChanged,
        lazy.SearchUtils.TOPIC_ENGINE_MODIFIED
      );
      return () =>
        Services.obs.removeObserver(
          onTargetEngineChanged,
          lazy.SearchUtils.TOPIC_ENGINE_MODIFIED
        );
    },
    get() {
      return !engine.hidden;
    },
    onUserChange() {
      engine.hidden = !engine.hidden;
    },
  };
}

Preferences.addSetting(
  class extends Preferences.AsyncSetting {
    static id = "engineList";

    /**
     * @type {?Map<Values<typeof lazy.UrlbarUtils.RESULT_SOURCE>, string[]>}
     *   This maps local shortcut sources to their l10n names. The first item
     *   in the string array is the display name for the local source.
     *   All items in the string should be used for displaying as aliases.
     */
    #localShortcutL10nNames = null;

    setup() {
      Services.obs.addObserver(
        this.emitChange,
        "browser-search-engine-modified"
      );
      return () =>
        Services.obs.removeObserver(
          this.emitChange,
          "browser-search-engine-modified"
        );
    }

    /**
     * Gets and caches the l10n names for the local shortcut sources.
     */
    async getL10nNames() {
      if (this.#localShortcutL10nNames) {
        return this.#localShortcutL10nNames;
      }
      this.#localShortcutL10nNames = new Map();

      let getIDs = (suffix = "") =>
        lazy.UrlbarUtils.LOCAL_SEARCH_MODES.map(mode => {
          let sourceName = lazy.UrlbarUtils.getResultSourceName(mode.source);
          return { id: `urlbar-search-mode-${sourceName}${suffix}` };
        });

      try {
        let localizedIDs = getIDs();
        let englishIDs = getIDs("-en");

        let englishSearchStrings = new Localization([
          "preview/enUS-searchFeatures.ftl",
        ]);
        let localizedNames = await document.l10n.formatValues(localizedIDs);
        let englishNames = await englishSearchStrings.formatValues(englishIDs);

        lazy.UrlbarUtils.LOCAL_SEARCH_MODES.forEach(({ source }, index) => {
          let localizedName = localizedNames[index];
          let englishName = englishNames[index];

          // Add only the English name if localized and English are the same.
          let names =
            /** @type {string[]} */
            (
              localizedName === englishName
                ? [englishName]
                : [localizedName, englishName]
            );

          this.#localShortcutL10nNames?.set(source, names);
        });
      } catch (ex) {
        console.error("Error loading l10n names", ex);
      }
      return this.#localShortcutL10nNames;
    }

    /**
     * Handles options for deleting and removing search engines.
     *
     * @param {SearchEngine} engine
     *   The engine to add settings for.
     */
    handleDeletionOptions(engine) {
      /** @type {SettingControlConfig} */
      let deletionOptions;
      if (engine.isConfigEngine) {
        let toggleId = `toggleEngine-${engine.id}`;
        maybeMakeSetting(ToggleSetting(toggleId, engine));

        deletionOptions = {
          id: toggleId,
          l10nId: "search-enable-engine",
          control: "moz-toggle",
          slot: "actions",
        };
      } else {
        let deletionId = `deleteEngine-${engine.id}`;
        maybeMakeSetting({
          id: deletionId,
          async onUserClick() {
            let [body, removeLabel] = await document.l10n.formatValues([
              "remove-engine-confirmation",
              "remove-engine-remove",
            ]);

            let button = Services.prompt.confirmExBC(
              window.browsingContext,
              Services.prompt.MODAL_TYPE_CONTENT,
              null,
              body,
              (Services.prompt.BUTTON_TITLE_IS_STRING *
                Services.prompt.BUTTON_POS_0) |
                (Services.prompt.BUTTON_TITLE_CANCEL *
                  Services.prompt.BUTTON_POS_1),
              removeLabel,
              null,
              null,
              null,
              {}
            );

            if (button == 0) {
              await lazy.SearchService.removeEngine(
                engine,
                lazy.SearchService.CHANGE_REASON.USER
              );
            }
          },
        });

        deletionOptions = {
          id: deletionId,
          l10nId: "search-delete-engine",
          control: "moz-button",
          iconSrc: "chrome://global/skin/icons/delete.svg",
          slot: "actions",
        };
      }

      return deletionOptions;
    }

    /**
     * Curates the configuration for the list of search engines for display in
     * the group box.
     */
    async makeEngineList() {
      /** @type {SettingControlConfig[]} */
      let configs = [];
      for (let engine of await lazy.SearchService.getEngines()) {
        let settingId = `engineList-${engine.id}`;
        let editId = `editEngine-${engine.id}`;
        let outlinkId = `outlink-${engine.id}`;

        maybeMakeSetting(EngineListItemSetting(settingId, engine));
        maybeMakeSetting({
          id: editId,
          disabled: () => engine.hidden,
          onUserClick() {
            window.gSubDialog.open(
              "chrome://browser/content/search/addEngine.xhtml",
              {
                features: "resizable=no, modal=yes",
                /** @param {CustomEvent} event */
                closingCallback: event => {
                  if (event.detail.button == "accept") {
                    searchEngineUpdateNotifier?.();
                  }
                },
              },
              { engine, mode: "EDIT" }
            );
          },
        });

        /** @type {SettingControlConfig & {items: ?SettingControlConfig[]}} */
        let config = {
          id: settingId,
          control: "moz-box-item",
          items: [
            {
              id: editId,
              l10nId: "search-edit-engine-2",
              control: "moz-button",
              iconSrc: "chrome://global/skin/icons/edit-outline.svg",
              slot: "actions",
            },
          ],
        };

        // Addon search engines need an edit button to edit the alias names
        // and an outlink icon, but they should not have a toggle or a delete
        // button.
        if (!(engine instanceof lazy.AddonSearchEngine)) {
          config.items.push(this.handleDeletionOptions(engine));
        } else {
          maybeMakeSetting({
            id: outlinkId,
            onUserClick(e) {
              e.preventDefault();
              // @ts-expect-error topChromeWindow global
              window.browsingContext.topChromeWindow.BrowserAddonUI.manageAddon(
                engine.extensionID
              );
            },
          });

          config.items.push({
            id: outlinkId,
            l10nId: "search-outlink-to-extensions-page",
            control: "moz-button",
            iconSrc: "chrome://global/skin/icons/open-in-new.svg",
            slot: "actions",
          });
        }

        configs.push(config);
      }

      return configs;
    }

    /**
     * Curates the configuration for the list of search modes for display in
     * the group box.
     */
    async makeSearchModesList() {
      let l10nNames = await this.getL10nNames();

      /** @type {SettingControlConfig[]} */
      let configs = [];
      for (let searchMode of lazy.UrlbarUtils.LOCAL_SEARCH_MODES) {
        let id = `searchmode-${searchMode.telemetryLabel}`;
        maybeMakeSetting({ id });

        // Convert the localized words into lowercase keywords prepended with
        // an @ symbol.
        let names = l10nNames.get(searchMode.source);
        if (!names) {
          continue;
        }
        let keywords = names
          .map(keyword => `@${keyword.toLowerCase()}`)
          .join(", ");

        // Add the restrict token as a keyword option as well.
        keywords += `, ${searchMode.restrict}`;

        configs.push({
          id,
          control: "moz-box-item",
          slot: "static",
          iconSrc: searchMode.icon,
          controlAttrs: {
            label: l10nNames.get(searchMode.source)?.[0] ?? "",
            description: keywords,
            layout: "medium-icon",
          },
        });
      }

      return configs;
    }

    /** @param {CustomEvent} event */
    async onUserReorder(event) {
      const { draggedElement, targetIndex } = event.detail;
      let draggedEngineName = draggedElement.label;
      let draggedEngine = lazy.SearchService.getEngineByName(draggedEngineName);
      if (!draggedEngine) {
        return;
      }
      await lazy.SearchService.moveEngine(draggedEngine, targetIndex);
    }
    async getControlConfig() {
      /** @type {Partial<SettingControlConfig>} */
      return {
        items: [
          ...(await this.makeEngineList()),
          ...(await this.makeSearchModesList()),
        ],
      };
    }
  }
);

SettingGroupManager.registerGroups({
  defaultEngine: {
    l10nId: "search-engine-group",
    headingLevel: 2,
    items: [
      {
        id: "defaultEngineNormal",
        l10nId: "search-default-engine",
        control: "moz-select",
      },
      {
        id: "searchShowSearchTermCheckbox",
        l10nId: "search-show-search-term-option-2",
      },
      {
        id: "browserSeparateDefaultEngine",
        l10nId: "search-separate-default-engine-2",
        items: [
          {
            id: "defaultPrivateEngine",
            l10nId: "search-separate-default-engine-dropdown",
            control: "moz-select",
          },
        ],
      },
    ],
  },
  searchShortcuts: {
    inProgress: true,
    l10nId: "search-one-click-header-3",
    headingLevel: 2,
    items: [
      {
        id: "updateSearchEngineSuccess",
        l10nId: "update-search-engine-success",
        control: "moz-message-bar",
        controlAttrs: {
          type: "success",
          dismissable: true,
        },
      },
      {
        id: "addEngineButton",
        l10nId: "search-add-engine-2",
        control: "moz-button",
        iconSrc: "chrome://global/skin/icons/plus.svg",
        controlAttrs: {
          "search-l10n-ids": "search-filtering-for-add-engine",
        },
      },
      {
        id: "engineList",
        control: "moz-box-group",
        controlAttrs: {
          type: "reorderable-list",
        },
      },
    ],
  },
  searchSuggestions: {
    l10nId: "search-suggestions-header-2",
    headingLevel: 2,
    items: [
      {
        id: "suggestionsInSearchFieldsCheckbox",
        l10nId: "search-show-suggestions-option",
        items: [
          {
            id: "urlBarSuggestionCheckbox",
            l10nId: "search-show-suggestions-url-bar-option",
          },
          {
            id: "showSearchSuggestionsFirstCheckbox",
            l10nId: "search-show-suggestions-above-history-option-2",
          },
          {
            id: "showSearchSuggestionsPrivateWindowsCheckbox",
            l10nId: "search-show-suggestions-private-windows-2",
          },
          {
            id: "showTrendingSuggestionsCheckbox",
            l10nId: "addressbar-locbar-showtrendingsuggestions-option-2",
            supportPage: "google-trending-searches-on-awesomebar",
          },
          {
            id: "urlBarSuggestionPermanentPBMessage",
            l10nId: "search-suggestions-cant-show-2",
            control: "moz-message-bar",
          },
        ],
      },
    ],
  },
  firefoxSuggest: {
    id: "locationBarGroup",
    items: [
      {
        id: "locationBarGroupHeader",
        l10nId: "addressbar-header-1",
        supportPage: "firefox-suggest",
        control: "moz-fieldset",
        controlAttrs: {
          headinglevel: 2,
        },
        items: [
          {
            id: "historySuggestion",
            l10nId: "addressbar-locbar-history-option",
          },
          {
            id: "bookmarkSuggestion",
            l10nId: "addressbar-locbar-bookmarks-option",
          },
          {
            id: "clipboardSuggestion",
            l10nId: "addressbar-locbar-clipboard-option",
          },
          {
            id: "openpageSuggestion",
            l10nId: "addressbar-locbar-openpage-option",
          },
          {
            id: "topSitesSuggestion",
            l10nId: "addressbar-locbar-shortcuts-option",
          },
          {
            id: "enableRecentSearches",
            l10nId: "addressbar-locbar-showrecentsearches-option-2",
          },
          {
            id: "enginesSuggestion",
            l10nId: "addressbar-locbar-engines-option-1",
          },
          {
            id: "enableQuickActions",
            l10nId: "addressbar-locbar-quickactions-option",
            supportPage: "quick-actions-firefox-search-bar",
          },
          {
            id: "firefoxSuggestAll",
            l10nId: "addressbar-locbar-suggest-all-option-2",
            items: [
              {
                id: "firefoxSuggestSponsored",
                l10nId: "addressbar-locbar-suggest-sponsored-option-2",
              },
              {
                id: "firefoxSuggestOnlineEnabledToggle",
                l10nId: "addressbar-firefox-suggest-online",
                supportPage: "firefox-suggest",
                subcategory: "w_what-is-firefox-suggest",
              },
            ],
          },
          {
            id: "dismissedSuggestionsDescription",
            l10nId: "addressbar-dismissed-suggestions-label-2",
            control: "moz-fieldset",
            controlAttrs: {
              headinglevel: 3,
            },
            items: [
              {
                id: "restoreDismissedSuggestions",
                l10nId: "addressbar-restore-dismissed-suggestions-button-2",
                control: "moz-button",
                iconSrc:
                  "chrome://global/skin/icons/arrow-counterclockwise-16.svg",
              },
            ],
          },
        ],
      },
    ],
  },
});
