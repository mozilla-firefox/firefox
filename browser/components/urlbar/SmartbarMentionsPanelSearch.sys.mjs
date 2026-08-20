/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
  UrlbarTokenizer:
    "moz-src:///browser/components/urlbar/UrlbarTokenizer.sys.mjs",
});

export const MENTION_TYPE = /** @type {const} */ ({
  TAB_OPEN: "TAB_OPEN",
  TAB_RECENTLY_CLOSED: "TAB_RECENTLY_CLOSED",
});

// URLs that should not be offered as mention suggestions in addition to the
// initial browsers pages.
const ADDITIONAL_EXCLUDED_MENTION_URLS = new Set(["about:aichatcontent"]);

/**
 * Whether a tab URL should be excluded from the mention suggestions.
 *
 * @param {Window} browserWindow
 * @param {string} url
 * @returns {boolean}
 */
function isExcludedMentionUrl(browserWindow, url) {
  if (!url || browserWindow.isInitialPage(url)) {
    return true;
  }
  try {
    const { prePath, filePath } = Services.io.newURI(url);
    return ADDITIONAL_EXCLUDED_MENTION_URLS.has(`${prePath}${filePath}`);
  } catch {
    return false;
  }
}

/**
 * @typedef {object} TabResult
 * @property {string} url - Tab URL
 * @property {string} title - Tab title
 * @property {string} icon - Tab icon
 * @property {(typeof MENTION_TYPE)[keyof typeof MENTION_TYPE]} type - Tab type
 * @property {number} timestamp - Tab timestamp
 */

/**
 * Mentions suggestions search for the Smartbar mentions panel.
 *
 * Fetches and filters open and recently closed tabs for display in the
 * mentions suggestions panel in the Smartbar.
 *
 * NOTE: This provider is not compatible with UrlbarProvidersManager and only
 * intended for standalone use within the Smartbar mentions feature.
 */
export class SmartbarMentionsPanelSearch {
  #tabs = null;

  constructor(browserWindow) {
    this.#tabs = this.#getOpenAndClosedTabs(browserWindow);
  }

  /**
   * Return filtered tabs sorted by recency.
   *
   * @param {string} searchString
   * @returns {TabResult[]}
   */
  startQuery(searchString) {
    return this.#filterTabs(searchString).sort(
      (a, b) => b.timestamp - a.timestamp
    );
  }

  #filterTabs(searchString) {
    if (!this.#tabs) {
      return [];
    }

    if (!searchString) {
      return this.#tabs;
    }

    const truncatedSearch = searchString.substring(
      0,
      lazy.UrlbarShared.MAX_TEXT_LENGTH
    );
    const tokens = lazy.UrlbarTokenizer.tokenize({
      searchString: truncatedSearch,
      trimmedSearchString: truncatedSearch.trim(),
    });

    if (!tokens.length) {
      return this.#tabs;
    }

    return this.#tabs.filter(tab => {
      const normalizedUrl = this.#normalizeUrl(tab.url);
      const searchText = `${tab.title} ${normalizedUrl}`
        .substring(0, lazy.UrlbarShared.MAX_TEXT_LENGTH)
        .toLowerCase();

      // Check if ALL tokens appear in the search text
      return tokens.every(token =>
        searchText.includes(token.value.toLowerCase())
      );
    });
  }

  #getOpenAndClosedTabs(browserWindow) {
    const results = [];

    // Open tabs
    for (const tab of browserWindow.gBrowser.tabs) {
      const url = tab.linkedBrowser?.currentURI?.spec;
      if (isExcludedMentionUrl(browserWindow, url)) {
        continue;
      }

      results.push({
        url,
        title: tab.label || url,
        // Try to avoid fetching remote images. tab.image should normally be a data uri or a
        // moz-remote-image data uri. If it's an http(s) url fallback to the page-icon protocol.
        icon:
          tab.image && !tab.image.startsWith("http")
            ? tab.image
            : lazy.UrlbarShared.getIconForUrl(url),
        type: MENTION_TYPE.TAB_OPEN,
        timestamp: tab.lastAccessed,
      });
    }

    // Recently closed tabs
    try {
      const closedTabData =
        lazy.SessionStore.getClosedTabDataForWindow(browserWindow);

      for (const closedTab of closedTabData) {
        const state = closedTab.state;

        // Get the active history entry using the same pattern as in
        // RecentlyClosedTabsAndWindowsMenuUtils.
        const activeIndex = (state.index || state.entries.length) - 1;
        if (activeIndex < 0 || !state.entries[activeIndex]) {
          continue;
        }

        const entry = state.entries[activeIndex];
        const url = entry.url;
        if (isExcludedMentionUrl(browserWindow, url)) {
          continue;
        }

        results.push({
          url,
          title: entry.title || url,
          icon: lazy.UrlbarShared.getIconForUrl(url),
          type: MENTION_TYPE.TAB_RECENTLY_CLOSED,
          timestamp: closedTab.closedAt,
        });
      }
    } catch (e) {
      console.error("Error getting recently closed tabs:", e);
    }

    return results;
  }

  #normalizeUrl(url) {
    try {
      const [stripped] = lazy.UrlbarShared.stripPrefixAndTrim(url, {
        stripHttp: true,
        stripHttps: true,
        trimSlash: true,
        trimEmptyQuery: true,
        trimEmptyHash: true,
      });
      return stripped;
    } catch {
      return url;
    }
  }
}
