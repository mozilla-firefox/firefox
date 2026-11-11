/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @import { SearchEngine } from "moz-src:///toolkit/components/search/SearchEngine.sys.mjs";
 * @import { UrlbarInputBase } from "moz-src:///browser/components/urlbar/content/UrlbarInputBase.mjs";
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
});

/**
 * @typedef {object} OpenSearchData
 * @property {string} uri
 *   The uri of the opensearch XML.
 * @property {string} title
 *   The name of the engine.
 * @property {string} icon
 *   Data URI containing the engine's icon.
 */

/**
 * Manages the set of available opensearch engines per browser.
 */
class _OpenSearchManager {
  /**
   * @type {WeakMap<MozBrowser, OpenSearchData[]>}
   */
  #offeredEngines = new WeakMap();

  /**
   * @type {WeakMap<MozBrowser, OpenSearchData[]>}
   */
  #hiddenEngines = new WeakMap();

  constructor() {
    Services.obs.addObserver(this, "browser-search-engine-modified");
  }

  /**
   * Observer for browser-search-engine-modified.
   *
   * @param {{wrappedJSObject: SearchEngine}} subject
   *   The modified engine.
   * @param {string} _topic
   *   Always browser-search-engine-modified.
   * @param {string} data
   *   The type of modification.
   */
  observe(subject, _topic, data) {
    switch (data) {
      case "engine-added":
      case "engine-changed":
        // If a page is offering an engine that is now installed, it needs to be
        // moved from the browser's offered engines to its hidden engines.
        this.#hideInstalledEngines();
        break;
      case "engine-removed":
        // If a page is offering an engine that is no longer installed, it needs
        // to be moved back from the browser's hidden engines to its offered
        // engines.
        this.#offerUninstalledEngines();
        break;
    }
  }

  /**
   * Whether an engine offered by a page is already installed. Pages may offer
   * an engine under a different title to the name in its OpenSearch
   * description, so the url of the description is checked as well.
   *
   * @param {OpenSearchData} engine
   *   The engine offered by the page.
   * @returns {boolean}
   */
  #isInstalled(engine) {
    return !!(
      lazy.SearchService.getEngineByName(engine.title) ||
      lazy.SearchService.getEngineByInstallURL(engine.uri)
    );
  }

  /**
   * Adds an open search engine to the list of available engines for a browser.
   * If the engine is already installed, adds it to the list of hidden engines
   * instead.
   *
   * @param {MozBrowser} browser
   *   The browser offering the engine.
   * @param {{title: string, href: string}} engine
   *   The title of the engine and the url to the opensearch XML.
   */
  addEngine(browser, engine) {
    if (!lazy.SearchService.hasSuccessfullyInitialized) {
      // We haven't finished initializing search yet. This means we can't
      // call getEngineByName here. Since this is only on start-up and unlikely
      // to happen in the normal case, we'll just return early rather than
      // trying to handle it asynchronously.
      return;
    }
    // Check to see whether we've already added an engine with this title
    if (this.#offeredEngines.get(browser)?.some(e => e.title == engine.title)) {
      return;
    }

    // Caching open search data in sys modules must not keep the browser alive.
    let browserRef = new WeakRef(browser);
    let data = {
      uri: engine.href,
      title: engine.title,
      get icon() {
        return browserRef.deref()?.mIconURL;
      },
    };

    // If this engine is already installed, add it to the list of hidden
    // engines rather than to the main list.
    let shouldBeHidden = this.#isInstalled(data);

    let engines =
      (shouldBeHidden
        ? this.#hiddenEngines.get(browser)
        : this.#offeredEngines.get(browser)) || [];
    engines.push(data);

    if (shouldBeHidden) {
      this.#hiddenEngines.set(browser, engines);
    } else {
      let win = browser.documentGlobal;
      this.#offeredEngines.set(browser, engines);
      if (browser == win.gBrowser.selectedBrowser) {
        this.updateOpenSearchBadge(win);
      }
    }
  }

  /**
   * Updates the browser UI to show whether or not additional engines are
   * available when a page is loaded or the user switches tabs to a page that
   * has open search engines.
   *
   * @param {WindowProxy} win
   *   The window whose UI should be updated.
   */
  updateOpenSearchBadge(win) {
    let engines = this.getInstallableEngines(win.gBrowser.selectedBrowser);
    for (let urlbar of /** @type {NodeListOf<UrlbarInputBase>} */ (
      win.document.querySelectorAll(".urlbar")
    )) {
      if (!urlbar.controller) {
        // This means it is not initialized and happens
        // if the new searchbar is disabled.
        continue;
      }
      urlbar.addSearchEngineHelper.setEnginesFromBrowser(
        win.gBrowser.selectedBrowser,
        engines || []
      );
      urlbar.searchModeSwitcher.toggleAddEnginesBadge(!!engines?.length);
    }

    let searchBar = win.document.getElementById("searchbar");
    if (!searchBar) {
      return;
    }

    if (engines && engines.length) {
      searchBar.setAttribute("addengines", "true");
    } else {
      searchBar.removeAttribute("addengines");
    }
  }

  #offerUninstalledEngines() {
    this.#moveEngines(
      this.#hiddenEngines,
      this.#offeredEngines,
      engine => !this.#isInstalled(engine)
    );
  }

  #hideInstalledEngines() {
    this.#moveEngines(this.#offeredEngines, this.#hiddenEngines, engine =>
      this.#isInstalled(engine)
    );
  }

  /**
   * Moves the engines matching a condition from one list to the other for
   * every browser, updating the badge of browsers whose offered engines change.
   *
   * @param {WeakMap<MozBrowser, OpenSearchData[]>} from
   *   The lists to move engines from.
   * @param {WeakMap<MozBrowser, OpenSearchData[]>} to
   *   The lists to move engines to.
   * @param {(engine: OpenSearchData) => boolean} shouldMove
   *   Whether an engine should be moved.
   */
  #moveEngines(from, to, shouldMove) {
    for (let win of lazy.BrowserWindowTracker.orderedWindows) {
      for (let browser of win.gBrowser.browsers) {
        let fromEngines = from.get(browser) || [];
        let moved = fromEngines.filter(shouldMove);
        if (!moved.length) {
          continue;
        }

        from.set(
          browser,
          fromEngines.filter(e => !moved.includes(e))
        );
        to.set(browser, [...(to.get(browser) || []), ...moved]);
        if (browser == win.gBrowser.selectedBrowser) {
          this.updateOpenSearchBadge(win);
        }
      }
    }
  }

  /**
   * Get the open search engines offered by a certain browser.
   *
   * @param {MozBrowser} browser
   *   The browser for which to get the engines.
   * @returns {OpenSearchData[]}
   *   The open search engines.
   */
  getEngines(browser) {
    return this.#offeredEngines.get(browser) || [];
  }

  /**
   * Get the open search engines offered by a browser that the user is allowed
   * to install. Returns an empty list when the installSearchEngine policy
   * disallows installing engines, even though the engines remain available for
   * one-off searches (e.g. contextual search).
   *
   * @param {MozBrowser} browser
   *   The browser for which to get the engines.
   * @returns {OpenSearchData[]}
   *   The installable open search engines.
   */
  getInstallableEngines(browser) {
    if (!Services.policies.isAllowed("installSearchEngine")) {
      return [];
    }
    return this.getEngines(browser);
  }

  clearEngines(browser) {
    this.#offeredEngines.delete(browser);
    this.#hiddenEngines.delete(browser);
  }
}

export const OpenSearchManager = new _OpenSearchManager();
