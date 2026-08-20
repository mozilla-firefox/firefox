/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports a provider that might show a tip when the user opens
 * the newtab or starts an organic search with their default search engine.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { UrlbarProvider } from "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs";
import { UrlbarShared } from "chrome://browser/content/urlbar/UrlbarShared.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AppMenuNotifications: "resource://gre/modules/AppMenuNotifications.sys.mjs",
  DefaultBrowserCheck:
    "moz-src:///browser/components/DefaultBrowserCheck.sys.mjs",
  LaterRun: "resource:///modules/LaterRun.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  SearchStaticData:
    "moz-src:///toolkit/components/search/SearchStaticData.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarProviderTopSites:
    "moz-src:///browser/components/urlbar/UrlbarProviderTopSites.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "cfrFeaturesUserPref",
  "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features",
  true
);

ChromeUtils.defineLazyGetter(lazy, "SUPPORTED_ENGINES", () => {
  // Converts a list of Google domains to a pipe separated string of escaped TLDs.
  // [www.google.com, ..., www.google.co.uk] => "com|...|co\.uk"
  const googleTLDs = lazy.SearchStaticData.getAlternateDomains("www.google.com")
    .map(str => str.slice("www.google.".length).replaceAll(".", "\\."))
    .join("|");

  // This maps engine names to regexes matching their homepages. We show the
  // redirect tip on these pages.
  return new Map([
    ["Bing", { domainPath: /^www\.bing\.com\/$/ }],
    [
      "DuckDuckGo",
      {
        domainPath: /^(start\.)?duckduckgo\.com\/$/,
        prohibitedSearchParams: ["q"],
      },
    ],
    [
      "Google",
      {
        domainPath: new RegExp(`^www\.google\.(?:${googleTLDs})\/(webhp)?$`),
      },
    ],
  ]);
});

// The maximum number of times we'll show a tip across all sessions.
const MAX_SHOWN_COUNT = 4;

// Amount of time to wait before showing a tip after selecting a tab or
// navigating to a page where we should show a tip.
const SHOW_TIP_DELAY_MS = 200;

// We won't show a tip if the browser has been updated in the past
// LAST_UPDATE_THRESHOLD_HOURS.
const LAST_UPDATE_THRESHOLD_HOURS = 24;

/**
 * A provider that sometimes returns a tip result when the user visits the
 * newtab page or their default search engine's homepage.
 *
 * This class supports only one instance.
 */
export class UrlbarProviderSearchTips extends UrlbarProvider {
  /** @type {?UrlbarProviderSearchTips} */
  static #instance = null;

  /**
   * Whether and what kind of tip we've shown in the current engagement.
   *
   * @type {Values<typeof UrlbarShared.SEARCH_TIP_TYPE>}
   */
  showedTipTypeInCurrentEngagement = UrlbarShared.SEARCH_TIP_TYPE.NONE;

  /** @type {Values<typeof UrlbarShared.SEARCH_TIP_TYPE>} */
  currentTip = UrlbarShared.SEARCH_TIP_TYPE.NONE;

  constructor() {
    super();
    if (UrlbarProviderSearchTips.#instance) {
      throw new Error("Can only have one instance of UrlbarProviderSearchTips");
    }
    UrlbarProviderSearchTips.#instance = this;

    // Whether we should disable tips for the current browser session, for
    // example because a tip was already shown.
    this.disableTipsForCurrentSession = true;
    for (let tip of Object.values(UrlbarShared.SEARCH_TIP_TYPE)) {
      if (
        tip &&
        lazy.UrlbarPrefs.get(`tipShownCount.${tip}`) < MAX_SHOWN_COUNT
      ) {
        this.disableTipsForCurrentSession = false;
        break;
      }
    }

    // Used to track browser windows we've seen.
    this._seenWindows = new WeakSet();
  }

  static get PRIORITY() {
    // Search tips are prioritized over the Places and top sites providers.
    return lazy.UrlbarProviderTopSites.PRIORITY + 1;
  }

  /**
   * @returns {Values<typeof UrlbarShared.PROVIDER_TYPE>}
   */
  get type() {
    return UrlbarShared.PROVIDER_TYPE.PROFILE;
  }

  /**
   * Whether this provider should be invoked for the given context.
   * If this method returns false, the providers manager won't start a query
   * with this provider, to save on resources.
   */
  async isActive() {
    return !!this.currentTip && lazy.cfrFeaturesUserPref;
  }

  /**
   * Gets the provider's priority.
   *
   * @returns {number} The provider's priority for the given query.
   */
  getPriority() {
    return UrlbarProviderSearchTips.PRIORITY;
  }

  /**
   * Starts querying.
   *
   * @param {UrlbarQueryContext} queryContext
   * @param {(provider: UrlbarProvider, result: UrlbarResult) => void} addCallback
   *   Callback invoked by the provider to add a new result.
   */
  async startQuery(queryContext, addCallback) {
    let instance = this.queryInstance;

    let tip = this.currentTip;
    this.showedTipTypeInCurrentEngagement = this.currentTip;
    this.currentTip = UrlbarShared.SEARCH_TIP_TYPE.NONE;

    let defaultEngine = await lazy.SearchService.getDefault();
    let icon = await defaultEngine.getIconURL();
    if (instance != this.queryInstance) {
      return;
    }

    let result;
    switch (tip) {
      case UrlbarShared.SEARCH_TIP_TYPE.ONBOARD:
        result = this.#makeResult({
          tip,
          icon,
          titleL10n: {
            id: "urlbar-search-tips-onboard",
            args: {
              engineName: defaultEngine.name,
            },
          },
          heuristic: true,
        });
        break;
      case UrlbarShared.SEARCH_TIP_TYPE.REDIRECT:
        result = this.#makeResult({
          tip,
          icon,
          titleL10n: {
            id: "urlbar-search-tips-redirect-2",
            args: {
              engineName: defaultEngine.name,
            },
          },
        });
        break;
    }
    addCallback(this, result);
  }

  /**
   * Called when the tip is selected.
   *
   * @param {UrlbarResult} result
   *   The result that was picked.
   * @param {window} window
   *   The browser window in which the tip is being displayed.
   */
  #pickResult(result, window) {
    window.gURLBar.value = "";
    window.gURLBar.setPageProxyState("invalid");
    window.gURLBar.removeAttribute("suppress-focus-border");
    window.gURLBar.focus();

    // The user either clicked the tip's "Okay, Got It" button, or they clicked
    // in the urlbar while the tip was showing. We treat both as the user's
    // acknowledgment of the tip, and we don't show tips again in any session.
    // Set the shown count to the max.
    lazy.UrlbarPrefs.set(
      `tipShownCount.${result.payload.type}`,
      MAX_SHOWN_COUNT
    );
  }

  onEngagement(queryContext, controller, details) {
    this.#pickResult(details.result, controller.browserWindow);
  }

  onSearchSessionEnd() {
    this.showedTipTypeInCurrentEngagement = UrlbarShared.SEARCH_TIP_TYPE.NONE;
  }

  /**
   * Called from `onLocationChange` in browser.js.
   *
   * @param {window} window
   *  The browser window where the location change happened.
   * @param {nsIURI} uri
   *  The URI being navigated to.
   * @param {nsIWebProgress} webProgress
   *   The progress object, which can have event listeners added to it.
   * @param {number} flags
   *   Load flags. See nsIWebProgressListener.idl for possible values.
   */
  static async onLocationChange(window, uri, webProgress, flags) {
    if (UrlbarProviderSearchTips.#instance) {
      UrlbarProviderSearchTips.#instance.onLocationChange(
        window,
        uri,
        webProgress,
        flags
      );
    }
  }

  /**
   * Called by the static function with the same name.
   *
   * @param {window} window
   *  The browser window where the location change happened.
   * @param {nsIURI} uri
   *  The URI being navigated to.
   * @param {nsIWebProgress} webProgress
   *   The progress object, which can have event listeners added to it.
   * @param {number} flags
   *   Load flags. See nsIWebProgressListener.idl for possible values.
   */
  async onLocationChange(window, uri, webProgress, flags) {
    let instance = (this._onLocationChangeInstance = {});

    // If this is the first time we've seen this browser window, we take some
    // precautions to avoid impacting ts_paint.
    if (!this._seenWindows.has(window)) {
      this._seenWindows.add(window);

      // First, wait until MozAfterPaint is fired in the current content window.
      await window.gBrowserInit.firstContentWindowPaintPromise;
      if (instance != this._onLocationChangeInstance) {
        return;
      }

      // Second, wait 500ms.  ts_paint waits at most 500ms after MozAfterPaint
      // before ending.  We use XPCOM directly instead of Timer.sys.mjs to avoid the
      // perf impact of loading Timer.sys.mjs, in case it's not already loaded.
      await new Promise(resolve => {
        let timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
        timer.initWithCallback(resolve, 500, Ci.nsITimer.TYPE_ONE_SHOT);
      });
      if (instance != this._onLocationChangeInstance) {
        return;
      }
    }

    // Ignore events that don't change the document. Google is known to do this.
    // Also ignore changes in sub-frames. See bug 1623978.
    if (
      flags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT ||
      !webProgress.isTopLevel
    ) {
      return;
    }

    // The UrlbarView is usually closed on location change when the input is
    // blurred. Since we open the view to show the redirect tip without focusing
    // the input, the view won't close in that case. We need to close it
    // manually.
    if (
      this.showedTipTypeInCurrentEngagement != UrlbarShared.SEARCH_TIP_TYPE.NONE
    ) {
      window.gURLBar.view.close();
    }

    // Check if we are supposed to show a tip for the current session.
    if (
      !lazy.cfrFeaturesUserPref ||
      (this.disableTipsForCurrentSession &&
        !lazy.UrlbarPrefs.get("searchTips.test.ignoreShowLimits"))
    ) {
      return;
    }

    this._maybeShowTipForUrl(uri.spec, window).catch(ex =>
      this.logger.error(ex)
    );
  }

  /**
   * Determines whether we should show a tip for the current tab, sets
   * this.currentTip, and starts a search on an empty string.
   *
   * @param {string} urlStr
   *   The URL of the page being loaded, in string form.
   * @param {window} window
   *   The browser window in which the tip is being displayed.
   */
  async _maybeShowTipForUrl(urlStr, window) {
    let instance = {};
    this._maybeShowTipForUrlInstance = instance;

    let ignoreShowLimits = lazy.UrlbarPrefs.get(
      "searchTips.test.ignoreShowLimits"
    );

    // Determine which tip we should show for the tab.  Do this check first
    // before the others below.  It has less of a performance impact than the
    // others, so in the common case where the URL is not one we're interested
    // in, we can return immediately.
    let tip;
    let isNewtab = ["about:newtab", "about:home"].includes(urlStr);
    let isSearchHomepage = !isNewtab && (await isDefaultEngineHomepage(urlStr));

    if (isNewtab) {
      tip = UrlbarShared.SEARCH_TIP_TYPE.ONBOARD;
    } else if (isSearchHomepage) {
      tip = UrlbarShared.SEARCH_TIP_TYPE.REDIRECT;
    } else {
      // No tip.
      return;
    }

    // If we've shown this type of tip the maximum number of times over all
    // sessions, don't show it again.
    let shownCount = lazy.UrlbarPrefs.get(`tipShownCount.${tip}`);
    if (shownCount >= MAX_SHOWN_COUNT && !ignoreShowLimits) {
      return;
    }

    // Don't show a tip if the browser has been updated recently.
    let hoursSinceUpdate = Math.min(
      lazy.LaterRun.hoursSinceInstall,
      lazy.LaterRun.hoursSinceUpdate
    );
    if (hoursSinceUpdate < LAST_UPDATE_THRESHOLD_HOURS && !ignoreShowLimits) {
      return;
    }

    // Start a search.
    lazy.setTimeout(async () => {
      if (this._maybeShowTipForUrlInstance != instance) {
        return;
      }

      // We don't want to interrupt a user's typed query with a Search Tip.
      // See bugs 1613662 and 1619547.
      if (
        window.gURLBar.getAttribute("pageproxystate") == "invalid" &&
        window.gURLBar.value != ""
      ) {
        return;
      }

      // Don't show a tip if the browser is already showing some other
      // notification.
      if (
        (!ignoreShowLimits && (await isBrowserShowingNotification(window))) ||
        this._maybeShowTipForUrlInstance != instance
      ) {
        return;
      }

      // At this point, we're showing a tip.
      this.disableTipsForCurrentSession = true;

      // Store the new shown count.
      lazy.UrlbarPrefs.set(`tipShownCount.${tip}`, shownCount + 1);

      this.currentTip = tip;

      window.gURLBar.search("", {
        focus: tip == UrlbarShared.SEARCH_TIP_TYPE.ONBOARD,
      });
    }, SHOW_TIP_DELAY_MS);
  }

  #makeResult({ tip, icon, titleL10n, heuristic = false }) {
    return new lazy.UrlbarResult({
      type: UrlbarShared.RESULT_TYPE.TIP,
      source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
      heuristic,
      payload: {
        type: tip,
        buttons: [{ l10n: { id: "urlbar-search-tips-confirm" } }],
        icon,
        titleL10n,
      },
    });
  }
}

async function isBrowserShowingNotification(window) {
  // urlbar view and notification box (info bar)
  if (
    window.gURLBar.view.isOpen ||
    window.gNotificationBox.currentNotification ||
    window.gBrowser.getNotificationBox().currentNotification
  ) {
    return true;
  }

  // app menu notification doorhanger
  if (
    lazy.AppMenuNotifications.activeNotification &&
    !lazy.AppMenuNotifications.activeNotification.dismissed &&
    !lazy.AppMenuNotifications.activeNotification.options.badgeOnly
  ) {
    return true;
  }

  // PopupNotifications (e.g. Tracking Protection, Identity Box Doorhangers)
  if (window.PopupNotifications.isPanelOpen) {
    return true;
  }

  // page action button panels
  let pageActions = window.document.getElementById("page-action-buttons");
  if (pageActions) {
    for (let child of pageActions.childNodes) {
      if (child.getAttribute("open") == "true") {
        return true;
      }
    }
  }

  // toolbar button panels
  let navbar = window.document.getElementById("nav-bar-customization-target");
  for (let node of navbar.querySelectorAll("toolbarbutton")) {
    if (node.getAttribute("open") == "true") {
      return true;
    }
  }

  // Other modals like spotlight messages or default browser prompt
  // can be shown at startup
  if (window.gDialogBox.isOpen) {
    return true;
  }

  // On startup, the default browser check normally opens after the Search Tip.
  // As a result, we can't check for the prompt's presence, but we can check if
  // it plans on opening.
  const willPrompt = await lazy.DefaultBrowserCheck.willCheckDefaultBrowser(
    /* isStartupCheck */ false
  );
  if (willPrompt) {
    return true;
  }

  return false;
}

/**
 * Checks if the given URL is the homepage of the current default search engine.
 * Returns false if the default engine is not listed in SUPPORTED_ENGINES.
 *
 * @param {string} urlStr
 *   The URL to check, in string form.
 *
 * @returns {Promise<boolean>}
 */
async function isDefaultEngineHomepage(urlStr) {
  let defaultEngine = await lazy.SearchService.getDefault();
  if (!defaultEngine) {
    return false;
  }

  let homepageMatches = lazy.SUPPORTED_ENGINES.get(defaultEngine.name);
  if (!homepageMatches) {
    return false;
  }

  let url = URL.parse(urlStr);
  if (!url) {
    return false;
  }

  if (url.searchParams.has(homepageMatches.prohibitedSearchParams)) {
    return false;
  }

  // Strip protocol and query params.
  urlStr = url.hostname.concat(url.pathname);

  return homepageMatches.domainPath.test(urlStr);
}
