/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Various utilities for search related UI.
 */

/**
 * @import { SearchUtils } from "moz-src:///toolkit/components/search/SearchUtils.sys.mjs"
 * @import { UrlbarInput } from "chrome://browser/content/urlbar/UrlbarInput.mjs"
 * @import { SearchEngine } from "moz-src:///toolkit/components/search/SearchEngine.sys.mjs"
 */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import {
  AboutNewTabComponentRegistry,
  BaseAboutNewTabComponentRegistrant,
} from "moz-src:///browser/components/newtab/AboutNewTabComponents.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  BrowserSearchTelemetry:
    "moz-src:///browser/components/search/BrowserSearchTelemetry.sys.mjs",
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  ConfigSearchEngine:
    "moz-src:///toolkit/components/search/ConfigSearchEngine.sys.mjs",
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SearchEngineInstallError:
    "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  SearchUIUtilsL10n: () => {
    return new Localization(["browser/search.ftl", "branding/brand.ftl"]);
  },
});

export var SearchUIUtils = {
  initialized: false,

  init() {
    if (!this.initialized) {
      Services.obs.addObserver(this, "browser-search-engine-modified");
      this.initialized = true;

      // On startup, cache urlbar placeholder for regular and private windows.
      this.updatePlaceholderNamePreference(false);
      this.updatePlaceholderNamePreference(true);
    }
  },

  /**
   * @param {{wrappedJSObject: SearchEngine}} subject
   * @param {"browser-search-engine-modified"} topic
   * @param {string} data
   */
  observe(subject, topic, data) {
    switch (data) {
      case "engine-default":
        this.updatePlaceholderNamePreference(false);
        break;
      case "engine-default-private":
        this.updatePlaceholderNamePreference(true);
        break;
    }
  },

  /**
   * This function is called by the category manager for the
   * `search-service-notification` category.
   *
   * It allows the SearchService (in toolkit) to display
   * notifications in the browser for certain events.
   *
   * @param {string} notificationType
   *   Determines the function displaying the notification.
   * @param  {...any} args
   *   The arguments for that function.
   */
  showSearchServiceNotification(notificationType, ...args) {
    switch (notificationType) {
      case "search-engine-removal": {
        let [oldEngine, newEngine] = args;
        this.removalOfSearchEngineNotificationBox(oldEngine, newEngine);
        break;
      }
      case "search-settings-reset": {
        let [newEngine] = args;
        this.searchSettingsResetNotificationBox(newEngine);
        break;
      }
    }
  },

  /**
   * Infobar to notify the user's search engine has been removed
   * and replaced with an application default search engine.
   *
   * @param {string} oldEngine
   *   name of the engine to be moved and replaced.
   * @param {string} newEngine
   *   name of the application default engine to replaced the removed engine.
   */
  async removalOfSearchEngineNotificationBox(oldEngine, newEngine) {
    let win = lazy.BrowserWindowTracker.getTopWindow({
      allowFromInactiveWorkspace: true,
    });

    if (win) {
      let buttons = [
        {
          "l10n-id": "remove-search-engine-button",
          primary: true,
          callback() {
            const notificationBox =
              win.gNotificationBox.getNotificationWithValue(
                "search-engine-removal"
              );
            win.gNotificationBox.removeNotification(notificationBox);
          },
        },
        {
          supportPage: "search-engine-removal",
        },
      ];

      await win.gNotificationBox.appendNotification(
        "search-engine-removal",
        {
          label: {
            "l10n-id": "removed-search-engine-message2",
            "l10n-args": { oldEngine, newEngine },
          },
          priority: win.gNotificationBox.PRIORITY_SYSTEM,
        },
        buttons
      );
    }

    for (let openWin of lazy.BrowserWindowTracker.orderedWindows) {
      openWin.gURLBar?.updatePlaceholder();
    }
  },

  /**
   * Infobar informing the user that the search settings had to be reset
   * and what their new default engine is.
   *
   * @param {string} newEngine
   *   Name of the new default engine.
   */
  async searchSettingsResetNotificationBox(newEngine) {
    let win = lazy.BrowserWindowTracker.getTopWindow({
      allowFromInactiveWorkspace: true,
    });

    let buttons = [
      {
        "l10n-id": "reset-search-settings-button",
        primary: true,
        callback() {
          const notificationBox = win.gNotificationBox.getNotificationWithValue(
            "search-settings-reset"
          );
          win.gNotificationBox.removeNotification(notificationBox);
        },
      },
      {
        supportPage: "prefs-search",
      },
    ];

    await win.gNotificationBox.appendNotification(
      "search-settings-reset",
      {
        label: {
          "l10n-id": "reset-search-settings-message",
          "l10n-args": { newEngine },
        },
        priority: win.gNotificationBox.PRIORITY_SYSTEM,
      },
      buttons
    );
  },

  /**
   * Adds an open search engine and handles error UI.
   *
   * @param {string} locationURL
   *   The URL where the OpenSearch definition is located.
   * @param {string} image
   *   A URL string to an icon file to be used as the search engine's
   *   icon. This value may be overridden by an icon specified in the
   *   engine description file.
   * @param {object} browsingContext
   *   The browsing context any error prompt should be opened for.
   * @returns {Promise<boolean>}
   *   Returns true if the engine was added.
   */
  async addOpenSearchEngine(locationURL, image, browsingContext) {
    try {
      await lazy.SearchService.addOpenSearchEngine(
        locationURL,
        image,
        browsingContext?.embedderElement?.contentPrincipal?.originAttributes
      );
    } catch (ex) {
      // Use a general download error message, unless we have something more
      // specific.
      let titleMsgName = "opensearch-error-download-title";
      let descMsgName = "opensearch-error-download-desc";

      if (ex instanceof lazy.SearchEngineInstallError) {
        switch (ex.type) {
          case "duplicate-title":
            titleMsgName = "opensearch-error-duplicate-title";
            descMsgName = "opensearch-error-duplicate-desc";
            break;
          case "corrupted":
            titleMsgName = "opensearch-error-format-title";
            descMsgName = "opensearch-error-format-desc";
            break;
          default:
          // e.g. download failure, use the more general message.
        }
      }

      let [title, text] = await lazy.SearchUIUtilsL10n.formatValues([
        {
          id: titleMsgName,
        },
        {
          id: descMsgName,
          args: {
            "location-url": locationURL,
          },
        },
      ]);

      Services.prompt.alertBC(
        browsingContext,
        Ci.nsIPrompt.MODAL_TYPE_CONTENT,
        title,
        text
      );
      return false;
    }
    return true;
  },

  /**
   * Returns the URL to use for where to get more search engines.
   *
   * @returns {string}
   */
  get searchEnginesURL() {
    return Services.urlFormatter.formatURLPref(
      "browser.search.searchEnginesURL"
    );
  },

  /**
   * Update the placeholderName preference for the urlbar's placeholder.
   *
   * @param {boolean} isPrivate Whether this change applies to private windows.
   */
  async updatePlaceholderNamePreference(isPrivate) {
    let prefName =
      "browser.urlbar.placeholderName" + (isPrivate ? ".private" : "");
    try {
      await lazy.SearchService.init();
    } catch {
      // Search service failed.
      Services.prefs.clearUserPref(prefName);
      return;
    }

    let engine = isPrivate
      ? lazy.SearchService.defaultPrivateEngine
      : lazy.SearchService.defaultEngine;
    if (engine instanceof lazy.ConfigSearchEngine) {
      Services.prefs.setStringPref(prefName, engine.name);
    } else {
      Services.prefs.clearUserPref(prefName);
    }
  },

  /**
   * Focuses the search bar if present on the toolbar, or the address bar,
   * putting it in search mode. Will do so in an existing non-popup browser
   * window or open a new one if necessary.
   *
   * @param {WindowProxy} window
   *   The window where the seach was triggered.
   */
  webSearch(window) {
    if (
      window.location.href != AppConstants.BROWSER_CHROME_URL ||
      window.gURLBar.readOnly
    ) {
      let topWindow = lazy.BrowserWindowTracker.getTopWindow();
      if (topWindow && !topWindow.gURLBar.readOnly) {
        // If there's an open browser window, it should handle this command.
        topWindow.focus();
        SearchUIUtils.webSearch(topWindow);
      } else {
        // If there are no open browser windows, open a new one.
        let newWindow = window.openDialog(
          AppConstants.BROWSER_CHROME_URL,
          "_blank",
          "chrome,all,dialog=no",
          "about:blank"
        );

        let observer = subject => {
          if (subject == newWindow) {
            SearchUIUtils.webSearch(newWindow);
            Services.obs.removeObserver(
              observer,
              "browser-delayed-startup-finished"
            );
          }
        };
        Services.obs.addObserver(observer, "browser-delayed-startup-finished");
      }
      return;
    }

    /** @type {(searchBar: MozSearchbar | UrlbarInput) => void} */
    let focusUrlBarIfSearchFieldIsNotActive = function (searchBar) {
      if (!searchBar || window.document.activeElement != searchBar.inputField) {
        // Limit the results to search suggestions, like the search bar.
        window.gURLBar.searchModeShortcut();
      }
    };

    let searchBar = /** @type {MozSearchbar | UrlbarInput} */ (
      window.document.getElementById(
        Services.prefs.getBoolPref("browser.search.widget.new")
          ? "searchbar-new"
          : "searchbar"
      )
    );
    let placement =
      lazy.CustomizableUI.getPlacementOfWidget("search-container");
    let focusSearchBar = () => {
      searchBar = /** @type {MozSearchbar | UrlbarInput} */ (
        window.document.getElementById(
          Services.prefs.getBoolPref("browser.search.widget.new")
            ? "searchbar-new"
            : "searchbar"
        )
      );
      searchBar.select();
      focusUrlBarIfSearchFieldIsNotActive(searchBar);
    };
    if (
      placement &&
      searchBar &&
      ((searchBar.parentElement.getAttribute("overflowedItem") == "true" &&
        placement.area == lazy.CustomizableUI.AREA_NAVBAR) ||
        placement.area == lazy.CustomizableUI.AREA_FIXED_OVERFLOW_PANEL)
    ) {
      let navBar = window.document.getElementById(
        lazy.CustomizableUI.AREA_NAVBAR
      );
      // @ts-expect-error - Navbar receives the overflowable property upon registration.
      navBar.overflowable.show().then(focusSearchBar);
      return;
    }
    if (searchBar) {
      if (window.fullScreen) {
        window.FullScreen.showNavToolbox();
      }
      searchBar.select();
    }
    focusUrlBarIfSearchFieldIsNotActive(searchBar);
  },

  /**
   * Opens a search results page, given a set of search terms.
   *
   * @param {object} options
   *   Options objects.
   * @param {WindowProxy} options.window
   *   The window where the search was triggered.
   * @param {string} options.searchText
   *   The search terms to use for the search.
   * @param {?string} [options.where]
   *   String indicating where the search should load. Most commonly used
   *   are ``tab`` or ``window``, defaults to ``current``.
   * @param {boolean} [options.usePrivateWindow]
   *   Whether to open the window in private browsing mode (if opening a window).
   *   Defaults to the type of window that ``options.window` is.
   * @param {nsIPrincipal} options.triggeringPrincipal
   *   The principal to use for a new window or tab.
   * @param {nsIPolicyContainer} [options.policyContainer]
   *   The policyContainer to use for a new window or tab.
   * @param {boolean} [options.inBackground]
   *   Set to true for the tab to be loaded in the background.
   * @param {?SearchEngine} [options.engine]
   *   The search engine to use for the search. If not supplied, this will default
   *   to the default search engine for normal or private mode, depending on
   *   ``options.usePrivateWindow``.
   * @param {?MozTabbrowserTab} [options.tab]
   *   The tab to show the search result.
   * @param {?Values<typeof SearchUtils.URL_TYPE>} [options.searchUrlType]
   *   A `SearchUtils.URL_TYPE` value indicating the type of search that should
   *   be performed. A falsey value is equivalent to
   *   `SearchUtils.URL_TYPE.SEARCH`, which will perform a usual web search.
   * @param {keyof typeof lazy.BrowserSearchTelemetry.KNOWN_SEARCH_SOURCES} options.sapSource
   *   The search access point source.
   * @param {boolean} [options.avoidBrowserFocus]
   *   When loading into the current tab, skip focusing the target browser
   *   element so keyboard focus stays where it was. Used by callers (e.g.
   *   the Smart Window assistant) that drive a search without user keyboard
   *   intent and need focus to remain with the initiating UI.
   */
  async loadSearch({
    window,
    searchText,
    where,
    usePrivateWindow = lazy.PrivateBrowsingUtils.isWindowPrivate(window),
    triggeringPrincipal,
    policyContainer,
    inBackground = false,
    engine,
    tab,
    searchUrlType,
    sapSource,
    avoidBrowserFocus = false,
  }) {
    if (!triggeringPrincipal) {
      throw new Error(
        "Required argument triggeringPrincipal missing within loadSearch"
      );
    }

    if (!engine) {
      engine = usePrivateWindow
        ? await lazy.SearchService.getDefaultPrivate()
        : await lazy.SearchService.getDefault();
    }

    let submission = engine.getSubmission(searchText, searchUrlType);

    // getSubmission can return null if the engine doesn't have a URL
    // for the given response type. This is an error if it occurs, since
    // we should only get here if the engine supports the URL type begin
    // passed.
    if (!submission) {
      throw new Error(`No submission URL found for ${searchUrlType}`);
    }

    window.openLinkIn(submission.uri.spec, where || "current", {
      private: usePrivateWindow,
      postData: submission.postData,
      inBackground,
      relatedToCurrent: true,
      triggeringPrincipal,
      policyContainer,
      targetBrowser: tab?.linkedBrowser,
      avoidBrowserFocus,
      globalHistoryOptions: {
        triggeringSearchEngine: engine.name,
      },
    });

    lazy.BrowserSearchTelemetry.recordSearch(
      window.gBrowser.selectedBrowser,
      engine,
      sapSource,
      { searchUrlType }
    );
  },

  /**
   * Perform a search initiated from the context menu.
   * Note: This should only be called from the context menu.
   *
   * @param {object} options
   *   Options object.
   * @param {SearchEngine} options.engine
   *   The engine to search with.
   * @param {WindowProxy} options.window
   *   The window where the search was triggered.
   * @param {string} options.searchText
   *   The search terms to use for the search.
   * @param {boolean} [options.usePrivateWindow]
   *   Whether to open the window in private browsing mode (if opening a window).
   *   Defaults to the type of window that ``options.window` is.
   * @param {nsIPrincipal} options.triggeringPrincipal
   *   The principal of the document whose context menu was clicked.
   * @param {nsIPolicyContainer} options.policyContainer
   *   The policyContainer to use for a new window or tab.
   * @param {XULCommandEvent|PointerEvent} options.event
   *   The event triggering the search.
   * @param {?Values<typeof SearchUtils.URL_TYPE>} [options.searchUrlType]
   *   A `SearchUtils.URL_TYPE` value indicating the type of search that should
   *   be performed. A falsey value is equivalent to
   *   `SearchUtils.URL_TYPE.SEARCH` and will perform a usual web search.
   */
  async loadSearchFromContext({
    window,
    engine,
    searchText,
    usePrivateWindow,
    triggeringPrincipal,
    policyContainer,
    event,
    searchUrlType = null,
  }) {
    event = lazy.BrowserUtils.getRootEvent(event);
    let where = lazy.BrowserUtils.whereToOpenLink(event);
    if (where == "current") {
      // override: historically search opens in new tab
      where = "tab";
    }
    if (
      usePrivateWindow &&
      !lazy.PrivateBrowsingUtils.isWindowPrivate(window)
    ) {
      where = "window";
    }
    let inBackground = Services.prefs.getBoolPref(
      "browser.search.context.loadInBackground"
    );
    if (event.button == 1 || event.ctrlKey) {
      inBackground = !inBackground;
    }

    return this.loadSearch({
      window,
      engine,
      searchText,
      searchUrlType,
      where,
      usePrivateWindow,
      triggeringPrincipal: Services.scriptSecurityManager.createNullPrincipal(
        triggeringPrincipal.originAttributes
      ),
      policyContainer,
      inBackground,
      sapSource:
        searchUrlType == lazy.SearchUtils.URL_TYPE.VISUAL_SEARCH
          ? "contextmenu_visual"
          : "contextmenu",
    });
  },
};

/**
 * A registrant that adds the handoff search bar to about:newtab / about:home.
 * It stands down while `browser.urlbar.newtab.featureGate` is enabled, which
 * puts `<moz-urlbar>` on those pages instead. New Tab admits only one component
 * per type, so both sides have to honor the gate: without this one standing
 * down, whichever registrant the category happens to enumerate first would win.
 */
export class SearchNewTabComponentsRegistrant extends BaseAboutNewTabComponentRegistrant {
  constructor() {
    super();
    // Wire up a lazy preference getter, primarily so that we have an easy way
    // of updating our external component registration if the pref changes.
    this.lazy = XPCOMUtils.declareLazy({
      prefHandoffToAwesomebar: {
        pref: "browser.newtabpage.activity-stream.improvesearch.handoffToAwesomebar",
        default: true,
        onUpdate: () => {
          this.updated();
        },
      },
    });
    lazy.UrlbarPrefs.addObserver(this);
  }

  destroy() {
    lazy.UrlbarPrefs.removeObserver(this);
  }

  onPrefChanged(pref) {
    if (pref == "newtab.featureGate") {
      this.updated();
    }
  }

  getComponents() {
    if (lazy.UrlbarPrefs.get("newtab.featureGate")) {
      return [];
    }

    const { caretBlinkCount, caretBlinkTime } = Services.appinfo;

    return [
      {
        type: AboutNewTabComponentRegistry.TYPES.SEARCH,
        l10nURLs: [],
        componentURL: "chrome://browser/content/contentSearchHandoffUI.mjs",
        tagName: "content-search-handoff-ui",
        cssVariables: {
          "--caret-blink-count":
            caretBlinkCount > -1 ? caretBlinkCount : "infinite",
          "--caret-blink-time":
            caretBlinkTime > 0 ? `${caretBlinkTime * 2}ms` : `${1134}ms`,
        },
        attributes: {
          nonhandoff: !this.lazy.prefHandoffToAwesomebar,
        },
      },
    ];
  }
}
