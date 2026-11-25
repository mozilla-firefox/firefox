/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

/**
 * @typedef {object} TabStateData
 *   State of a tab, validated in the session file by the `tab` definition in
 *   `session.schema.json`.
 * @property {object[]} entries
 *   Session history entries, oldest first, matching the schema's `entry`
 *   definition.
 * @property {number} lastAccessed
 *   Timestamp from `Date.now()` of when the tab was last selected.
 * @property {number} [index]
 *   1-based index of the active entry in `entries`.
 * @property {number} [requestedIndex]
 *   1-based index of the entry a pending restore is navigating to.
 * @property {boolean} [hidden]
 *   Whether the tab is hidden from the tab strip.
 * @property {boolean} [pinned]
 *   Whether the tab is pinned.
 * @property {boolean} [muted]
 *   Whether the tab was muted through the tab, i.e. by the user or an
 *   extension, as opposed to a mute the browser applies internally, such as
 *   the one a closing tab gets.
 * @property {string} [muteReason]
 *   Extension that muted the tab, if an extension did.
 * @property {TabGroupId} [groupId]
 *   Tab group the tab belongs to.
 * @property {number} [splitViewId]
 *   Split view the tab belongs to.
 * @property {string} [canonicalUrl]
 *   Canonical URL of the tab's document, subject to the privacy level.
 * @property {object} [searchMode]
 *   Address bar search mode the tab is in.
 * @property {number} [userContextId]
 *   Container the tab is in, 0 for the default context.
 * @property {Record<string, string>} [attributes]
 *   Persisted tab attributes, collected by `TabAttributes`.
 * @property {Record<string, string>} [extData]
 *   Values set through `SessionStore.setCustomTabValue`.
 * @property {string} [image]
 *   URL of the tab's icon.
 * @property {string} [userTypedValue]
 *   Address bar input that hasn't been loaded.
 * @property {number} [userTypedClear]
 *   1 if a load started after `userTypedValue` was typed, 0 otherwise.
 * @property {object} [storage]
 *   Session storage of the tab's document, keyed by origin.
 * @property {object} [formdata]
 *   Form data of the tab's document.
 * @property {object} [scroll]
 *   Scroll position of the tab's document.
 * @property {string} [disallow]
 *   Comma-separated docshell capabilities to turn off on restore.
 * @property {boolean} [isPrivate]
 *   Whether the tab belongs to a private window.
 * @property {boolean} [connectionPrepared]
 *   Whether a speculative connection has been made for the pending restore.
 * @property {boolean} [removeAfterRestore]
 *   Whether the tab is to be closed once it has been restored.
 */

/**
 * @typedef {object} ClosedTabStateData
 *   State of a closed tab, kept in a window's `_closedTabs` or in a closed or
 *   saved tab group's `tabs`, and validated in the session file by the
 *   `closedTab` definition in `session.schema.json`.
 * @property {TabStateData} state
 *   State the tab had when it closed.
 * @property {number} closedId
 *   ID identifying the closed tab, unique across windows.
 * @property {number} closedAt
 *   Timestamp from `Date.now()`.
 * @property {string} title
 *   Label the tab had when it closed.
 * @property {string} [image]
 *   URL of the tab's icon.
 * @property {number} pos
 *   Position the tab had in the tab strip.
 * @property {WindowID|number} sourceWindowId
 *   Window the tab closed in, or the `closedId` of that window when it was
 *   restored as a closed window from an earlier session.
 * @property {number} [sourceClosedId]
 *   `closedId` of the closed window the tab closed in. Set only on the copies
 *   `SessionStore.getClosedTabDataFromClosedWindows` and
 *   `SessionStore.getClosedTabGroups` return, never on stored state.
 * @property {boolean} [closedInGroup]
 *   Whether the tab closed along with the other selected tabs.
 * @property {TabGroupId} [closedInTabGroupId]
 *   Tab group whose closing closed this tab.
 * @property {boolean} [removeAfterRestore]
 *   Whether the tab was open at the end of the last session, so restoring that
 *   session reopens it and drops it from the closed tabs.
 * @property {object} [permanentKey]
 *   Permanent key of the tab's browser, dropped once the browser's final state
 *   update has arrived.
 * @property {number} [_originalStateIndex]
 *   Index of the tab within the list it came from, set while closed tabs from
 *   a window and its closed tab groups are merged into one list.
 * @property {number} [_originalGroupStateIndex]
 *   Index of the closed tab group the tab came from, set along with
 *   `_originalStateIndex` and left unset for tabs that closed on their own.
 */

const lazy = XPCOMUtils.declareLazy({
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
   * Processes a data update collected for a browser.
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
