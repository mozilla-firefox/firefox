/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PrivacyFilter: "resource://gre/modules/sessionstore/PrivacyFilter.sys.mjs",
  TabAttributes:
    "moz-src:///browser/components/sessionstore/TabAttributes.sys.mjs",
  TabStateCache:
    "moz-src:///browser/components/sessionstore/TabStateCache.sys.mjs",
  sessionStoreLogger:
    "moz-src:///browser/components/sessionstore/SessionLogger.sys.mjs",
});

/**
 * Module that contains tab state collection methods.
 */
class _TabState {
  /**
   * Processes a data update sent by the content script.
   */
  update(permanentKey, { data }) {
    lazy.TabStateCache.update(permanentKey, data);
  }

  /**
   * Collect data related to a single tab, synchronously.
   *
   * @param tab
   *        tabbrowser tab
   * @param [extData]
   *        optional dictionary object, containing custom tab values.
   *
   * @returns {TabStateData} An object with the data for this tab.  If the
   * tab has not been invalidated since the last call to
   * collect(aTab), the same object is returned.
   */
  collect(tab, extData) {
    return this.#collectBaseTabData(tab, { extData });
  }

  /**
   * Collect data related to a single tab, including private data.
   * Use with caution.
   *
   * @param tab
   *        tabbrowser tab
   * @param [extData]
   *        optional dictionary object, containing custom tab values.
   *
   * @returns {object} An object with the data for this tab. This data is never
   *                   cached, it will always be read from the tab and thus be
   *                   up-to-date.
   */
  clone(tab, extData) {
    return this.#collectBaseTabData(tab, { extData, includePrivateData: true });
  }

  /**
   * Collects basic tab data for a given tab.
   *
   * @param tab
   *        tabbrowser tab
   * @param options (object)
   *        {extData: object} optional dictionary object, containing custom tab values
   *        {includePrivateData: true} to always include private data
   *
   * @returns {TabStateData} An object with the basic data for this tab.
   */
  #collectBaseTabData(tab, options) {
    let tabData = { entries: [], lastAccessed: tab.lastAccessed };
    let browser = tab.linkedBrowser;

    if (tab.pinned) {
      tabData.pinned = true;
    }

    tabData.hidden = tab.hidden;

    // Collect tab.muted, the muted attribute that only muting through the tab
    // sets, rather than browser.audioMuted, which is a live read of the media
    // controller and so also covers internal mutes, such as the one a closing
    // tab gets, that must not outlive the tab.
    if (tab.muted) {
      tabData.muted = true;
      tabData.muteReason = tab.muteReason;
    }

    if (tab.group) {
      tabData.groupId = tab.group.id;
    }

    if (tab.splitview) {
      tabData.splitViewId = tab.splitview.splitViewId;
    }

    if (tab.canonicalUrl) {
      let canonicalUrl = tab.canonicalUrl;
      if (!options.includePrivateData) {
        canonicalUrl = lazy.PrivacyFilter.filterCanonicalUrl(canonicalUrl);
      }
      if (canonicalUrl) {
        tabData.canonicalUrl = canonicalUrl;
      }
    }

    tabData.searchMode = tab.documentGlobal.gURLBar.getSearchMode(
      browser,
      true
    );

    tabData.userContextId = tab.userContextId || 0;

    // Save tab attributes.
    tabData.attributes = lazy.TabAttributes.get(tab);

    if (options.extData) {
      tabData.extData = options.extData;
    }

    // Copy data from the tab state cache only if the tab has fully finished
    // restoring. We don't want to overwrite data contained in __SS_data.
    this.copyFromCache(browser.permanentKey, tabData, options);

    // After copyFromCache() was called we check for properties that are kept
    // in the cache only while the tab is pending or restoring. Once that
    // happened those properties will be removed from the cache and will
    // be read from the tab/browser every time we collect data.

    // Store the tab icon.
    if (!("image" in tabData)) {
      let tabbrowser = tab.documentGlobal.gBrowser;
      tabData.image = tabbrowser.getIcon(tab);
    }

    // If there is a userTypedValue set, then either the user has typed something
    // in the URL bar, or a new tab was opened with a URI to load.
    // If so, we also track whether we were still in the process of loading something.
    if (!("userTypedValue" in tabData) && browser.userTypedValue) {
      tabData.userTypedValue = browser.userTypedValue;
      // We always used to keep track of the loading state as an integer, where
      // '0' indicated the user had typed since the last load (or no load was
      // ongoing), and any positive value indicated we had started a load since
      // the last time the user typed in the URL bar. Mimic this to keep the
      // session store representation in sync, even though we now represent this
      // more explicitly:
      tabData.userTypedClear = browser.didStartLoadSinceLastUserTyping()
        ? 1
        : 0;
    }

    return tabData;
  }

  processAboutRestartrequiredEnties(aEntries) {
    // Find if there are some entries that matches (contains) the
    // about:restartrequired page. It can be plain about:restartrequired
    // or something more complicated like about:restartrequired?e=restartrequired&u=about%3Ablank&c=UTF-8&d=%20
    if (
      !aEntries.some(e => e.url && e.url.startsWith("about:restartrequired"))
    ) {
      return aEntries;
    }

    // now we need a deep copy
    let newEntries = structuredClone(aEntries);
    newEntries.forEach((item, index, object) => {
      if (item.url === "about:restartrequired") {
        object.splice(index, 1);
      } else if (item.url.startsWith("about:restartrequired")) {
        try {
          const parsedURL = new URL(item.url);
          if (parsedURL && parsedURL.searchParams.has("u")) {
            const previousURL = parsedURL.searchParams.get("u");
            object[index].url = previousURL;
          }
        } catch (ex) {
          lazy.sessionStoreLogger.error(
            `Exception when parsing "${item.url}"`,
            ex
          );
        }
      }
    });

    return newEntries;
  }

  /**
   * Copy data for the given |browser| from the cache to |tabData|.
   *
   * @param permanentKey (object)
   *        The browser belonging to the given |tabData| object.
   * @param tabData (object)
   *        The tab data belonging to the given |tab|.
   * @param options (object)
   *        {includePrivateData: true} to always include private data
   */
  copyFromCache(permanentKey, tabData, options = {}) {
    let data = lazy.TabStateCache.get(permanentKey);
    if (!data) {
      return;
    }

    // The caller may explicitly request to omit privacy checks.
    let includePrivateData = options && options.includePrivateData;

    for (let key of Object.keys(data)) {
      let value = data[key];

      // Filter sensitive data according to the current privacy level.
      if (!includePrivateData) {
        if (key === "storage") {
          value = lazy.PrivacyFilter.filterSessionStorageData(value);
        } else if (key === "formdata") {
          value = lazy.PrivacyFilter.filterFormData(value);
        }
      }

      if (key === "history") {
        // Make a shallow copy of the entries array. We (currently) don't update
        // entries in place, so we don't have to worry about performing a deep
        // copy.
        tabData.entries = [...value.entries];

        if (value.hasOwnProperty("index")) {
          tabData.index = value.index;
        }

        if (value.hasOwnProperty("requestedIndex")) {
          tabData.requestedIndex = value.requestedIndex;
        }

        tabData.entries = this.processAboutRestartrequiredEnties(value.entries);
      } else if (!value && (key == "scroll" || key == "formdata")) {
        // [Bug 1554512]

        // If scroll or formdata null it indicates that the update to
        // be performed is to remove them, and not copy a null
        // value. Scroll will be null when the position is at the top
        // of the document, formdata will be null when there is only
        // default data.
        delete tabData[key];
      } else {
        tabData[key] = value;
      }
    }
  }
}

export const TabState = new _TabState();
