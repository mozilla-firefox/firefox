/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports a provider that offers remote tabs.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { UrlbarProvider } from "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  SyncedTabs: "resource://services-sync/SyncedTabs.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
  UrlbarTokenizer:
    "moz-src:///browser/components/urlbar/UrlbarTokenizer.sys.mjs",
  weaveXPCService() {
    try {
      let service = Cc["@mozilla.org/weave/service;1"].getService(
        Ci.nsISupports
      );
      return /** @type {any} */ (service).wrappedJSObject;
    } catch (ex) {
      // The app didn't build Sync.
    }
    return null;
  },
  showRemoteIconsPref: {
    pref: "services.sync.syncedTabs.showRemoteIcons",
    default: true,
  },
  syncUsernamePref: { pref: "services.sync.username" },
});

// By default, we add remote tabs that have been used more recently than this
// time ago. Any remaining remote tabs are added in queue if no other results
// are found.
const RECENT_REMOTE_TAB_THRESHOLD_MS = 72 * 60 * 60 * 1000; // 72 hours.

/**
 * Singleton class to cache the latest remote tab data.
 */
class _cache {
  /** @type {{tab: object, client: object}[]} */
  #tabsData = null;

  constructor() {
    Services.obs.addObserver(
      this.observe.bind(this),
      "weave:engine:sync:finish"
    );
    Services.obs.addObserver(
      this.observe.bind(this),
      "weave:service:start-over"
    );
  }

  /**
   * Build the in-memory structure we use.
   */
  async #buildItems() {
    // This is sorted by most recent client, most recent tab.
    let tabsData = [];
    // If Sync isn't initialized (either due to lag at startup or due to no user
    // being signed in), don't reach in to Weave.Service as that may initialize
    // Sync unnecessarily - we'll get an observer notification later when it
    // becomes ready and has synced a list of tabs.
    if (lazy.weaveXPCService.ready) {
      let clients = await lazy.SyncedTabs.getTabClients();
      lazy.SyncedTabs.sortTabClientsByLastUsed(clients);
      for (let client of clients) {
        for (let tab of client.tabs) {
          tabsData.push({ tab, client });
        }
      }
    }
    this.#tabsData = tabsData;
  }

  observe(subject, topic, data) {
    switch (topic) {
      case "weave:engine:sync:finish":
        if (data == "tabs") {
          // The tabs engine just finished syncing, so may have a different list
          // of tabs then we previously cached.
          this.#tabsData = null;
        }
        break;
      case "weave:service:start-over":
        // Sync is being reset due to the user disconnecting - we must invalidate
        // the cache so we don't supply tabs from a different user.
        this.#tabsData = null;
        break;
      default:
        break;
    }
  }

  /** @type {?_cache} */
  static #instance;
  /**
   * Build (if necessary) and return tabs data.
   *
   * @returns {Promise<{tab: object, client: object}[]>}
   */
  static async get() {
    _cache.#instance ??= new _cache();

    if (!_cache.#instance.#tabsData) {
      await _cache.#instance.#buildItems();
    }
    return _cache.#instance.#tabsData;
  }
}

/**
 * Class used to create the provider.
 */
export class UrlbarProviderRemoteTabs extends UrlbarProvider {
  constructor() {
    super();
  }

  /**
   * @returns {Values<typeof lazy.UrlbarShared.PROVIDER_TYPE>}
   */
  get type() {
    return lazy.UrlbarShared.PROVIDER_TYPE.NETWORK;
  }

  /**
   * Whether this provider should be invoked for the given context.
   * If this method returns false, the providers manager won't start a query
   * with this provider, to save on resources.
   *
   * @param {UrlbarQueryContext} queryContext The query context object
   */
  async isActive(queryContext) {
    return (
      lazy.syncUsernamePref &&
      lazy.UrlbarPrefs.get("suggest.remotetab") &&
      queryContext.sources.includes(lazy.UrlbarShared.RESULT_SOURCE.TABS) &&
      lazy.weaveXPCService &&
      lazy.weaveXPCService.ready &&
      lazy.weaveXPCService.enabled
    );
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

    let tokens = queryContext.restrictToken
      ? queryContext.tokens.filter(
          t => !lazy.UrlbarTokenizer.isRestrictionToken(t)
        )
      : queryContext.tokens;

    let tabsData = await _cache.get();
    if (instance != this.queryInstance) {
      return;
    }

    let resultsAdded = 0;
    let staleTabs = [];
    for (let { tab, client } of tabsData) {
      let url = tab.url.toLocaleLowerCase();
      let title = tab.title.toLocaleLowerCase();
      if (
        tokens.every(
          t =>
            url.includes(t.lowerCaseValue) || title.includes(t.lowerCaseValue)
        )
      ) {
        // Bug 2017798 - we need to determine how to safely show remote favicons here,
        // but until then, we show a generic favicon for the site if possible.
        let icon = lazy.showRemoteIconsPref
          ? lazy.UrlbarShared.getIconForUrl(tab.url)
          : "";
        let result = new lazy.UrlbarResult({
          type: lazy.UrlbarShared.RESULT_TYPE.REMOTE_TAB,
          source: lazy.UrlbarShared.RESULT_SOURCE.TABS,
          payload: {
            url: tab.url,
            title: tab.title,
            device: client.name,
            icon,
            lastUsed: (tab.lastUsed || 0) * 1000,
          },
          highlights: {
            url: lazy.UrlbarShared.HIGHLIGHT.TYPED,
            title: lazy.UrlbarShared.HIGHLIGHT.TYPED,
          },
        });

        // We want to return the most relevant remote tabs and thus the most
        // recent ones. While SyncedTabs.sys.mjs returns tabs that are sorted by
        // most recent client, then most recent tab, we can do better. For
        // example, the most recent client might have one recent tab and then
        // many very stale tabs. Those very stale tabs will push out more recent
        // tabs from staler clients. This provider first returns tabs from the
        // last 72 hours, sorted by client recency. Then, it adds remaining
        // tabs. We are not concerned about filling the remote tabs group with
        // stale tabs, because the muxer ensures remote tabs flex with other
        // results. It will only show the stale tabs if it has nothing else
        // to show.
        if (
          tab.lastUsed <=
          (Date.now() - RECENT_REMOTE_TAB_THRESHOLD_MS) / 1000
        ) {
          staleTabs.push(result);
        } else {
          addCallback(this, result);
          resultsAdded++;
        }
      }

      if (resultsAdded == queryContext.maxResults) {
        break;
      }
    }

    while (staleTabs.length && resultsAdded < queryContext.maxResults) {
      addCallback(this, staleTabs.shift());
      resultsAdded++;
    }
  }
}
