/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import {
  getSearchProvider,
  SEARCH_SHORTCUTS_EXPERIMENT,
} from "resource://gre/modules/SearchShortcuts.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BinarySearch: "resource://gre/modules/BinarySearch.sys.mjs",
  PageThumbs: "resource://gre/modules/PageThumbs.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  Pocket: "chrome://pocket/content/Pocket.sys.mjs",
  pktApi: "chrome://pocket/content/pktApi.sys.mjs",
});

let BrowserWindowTracker;
try {
  BrowserWindowTracker = ChromeUtils.importESModule(
    "resource:///modules/BrowserWindowTracker.sys.mjs"
  ).BrowserWindowTracker;
} catch (e) {
  // BrowserWindowTracker is used to determine devicePixelRatio in
  // _addFavicons. We fallback to the value 2 if we can't find a window,
  // so it's safe to do nothing with this here.
}

ChromeUtils.defineLazyGetter(lazy, "gCryptoHash", function () {
  return Cc["@mozilla.org/security/hash;1"].createInstance(Ci.nsICryptoHash);
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "IDNService",
  "@mozilla.org/network/idn-service;1",
  "nsIIDNService"
);

// Boolean preferences that control newtab content
const PREF_NEWTAB_ENABLED = "browser.newtabpage.enabled";

// The maximum number of results PlacesProvider retrieves from history.
const HISTORY_RESULTS_LIMIT = 100;

// The maximum number of links Links.getLinks will return.
const LINKS_GET_LINKS_LIMIT = 100;

// The gather telemetry topic.
const TOPIC_GATHER_TELEMETRY = "gather-telemetry";

// Some default frecency threshold for Activity Stream requests
const ACTIVITY_STREAM_DEFAULT_FRECENCY = 150;

// Some default query limit for Activity Stream requests
const ACTIVITY_STREAM_DEFAULT_LIMIT = 12;

// Some default seconds ago for Activity Stream recent requests
const ACTIVITY_STREAM_DEFAULT_RECENT = 5 * 24 * 60 * 60;

// The fallback value for the width of smallFavicon in pixels.
// This value will be multiplied by the current window's devicePixelRatio.
// If devicePixelRatio cannot be found, it will be multiplied by 2.
const DEFAULT_SMALL_FAVICON_WIDTH = 16;

const POCKET_UPDATE_TIME = 24 * 60 * 60 * 1000; // 1 day
const POCKET_INACTIVE_TIME = 7 * 24 * 60 * 60 * 1000; // 1 week
const PREF_POCKET_LATEST_SINCE = "extensions.pocket.settings.latestSince";

/**
 * Calculate the MD5 hash for a string.
 * @param aValue
 *        The string to convert.
 * @return The base64 representation of the MD5 hash.
 */
function toHash(aValue) {
  let value = new TextEncoder().encode(aValue);
  lazy.gCryptoHash.init(lazy.gCryptoHash.MD5);
  lazy.gCryptoHash.update(value, value.length);
  return lazy.gCryptoHash.finish(true);
}

/**
 * Properly convert internationalized domain names.
 * @param {string} host Domain hostname.
 * @returns {string} Hostname suitable to be displayed.
 */
function handleIDNHost(hostname) {
  try {
    return lazy.IDNService.convertToDisplayIDN(hostname);
  } catch (e) {
    // If something goes wrong (e.g. host is an IP address) just fail back
    // to the full domain.
    return hostname;
  }
}

/**
 * Singleton that provides storage functionality.
 */
ChromeUtils.defineLazyGetter(lazy, "Storage", function () {
  return new LinksStorage();
});

function LinksStorage() {
  // Handle migration of data across versions.
  try {
    if (this._storedVersion < this._version) {
      // This is either an upgrade, or version information is missing.
      if (this._storedVersion < 1) {
        // Version 1 moved data from DOM Storage to prefs.  Since migrating from
        // version 0 is no more supported, we just reportError a dataloss later.
        throw new Error("Unsupported newTab storage version");
      }
      // Add further migration steps here.
    } else {
      // This is a downgrade.  Since we cannot predict future, upgrades should
      // be backwards compatible.  We will set the version to the old value
      // regardless, so, on next upgrade, the migration steps will run again.
      // For this reason, they should also be able to run multiple times, even
      // on top of an already up-to-date storage.
    }
  } catch (ex) {
    // Something went wrong in the update process, we can't recover from here,
    // so just clear the storage and start from scratch (dataloss!).
    console.error(
      "Unable to migrate the newTab storage to the current version. " +
        "Restarting from scratch.\n",
      ex
    );
    this.clear();
  }

  // Set the version to the current one.
  this._storedVersion = this._version;
}

LinksStorage.prototype = {
  get _version() {
    return 1;
  },

  get _prefs() {
    return Object.freeze({
      pinnedLinks: "browser.newtabpage.pinned",
      blockedLinks: "browser.newtabpage.blocked",
    });
  },

  get _storedVersion() {
    if (this.__storedVersion === undefined) {
      // When the pref is not set, the storage version is unknown, so either:
      // - it's a new profile
      // - it's a profile where versioning information got lost
      // In this case we still run through all of the valid migrations,
      // starting from 1, as if it was a downgrade.  As previously stated the
      // migrations should already support running on an updated store.
      this.__storedVersion = Services.prefs.getIntPref(
        "browser.newtabpage.storageVersion",
        1
      );
    }
    return this.__storedVersion;
  },
  set _storedVersion(aValue) {
    Services.prefs.setIntPref("browser.newtabpage.storageVersion", aValue);
    this.__storedVersion = aValue;
  },

  /**
   * Gets the value for a given key from the storage.
   * @param aKey The storage key (a string).
   * @param aDefault A default value if the key doesn't exist.
   * @return The value for the given key.
   */
  get: function Storage_get(aKey, aDefault) {
    let value;
    try {
      let prefValue = Services.prefs.getStringPref(this._prefs[aKey]);
      value = JSON.parse(prefValue);
    } catch (e) {}
    return value || aDefault;
  },

  /**
   * Sets the storage value for a given key.
   * @param aKey The storage key (a string).
   * @param aValue The value to set.
   */
  set: function Storage_set(aKey, aValue) {
    // Page titles may contain unicode, thus use complex values.
    Services.prefs.setStringPref(this._prefs[aKey], JSON.stringify(aValue));
  },

  /**
   * Removes the storage value for a given key.
   * @param aKey The storage key (a string).
   */
  remove: function Storage_remove(aKey) {
    Services.prefs.clearUserPref(this._prefs[aKey]);
  },

  /**
   * Clears the storage and removes all values.
   */
  clear: function Storage_clear() {
    for (let key in this._prefs) {
      this.remove(key);
    }
  },
};

/**
 * Singleton that serves as a registry for all open 'New Tab Page's.
 */
var AllPages = {
  /**
   * The array containing all active pages.
   */
  _pages: [],

  /**
   * Cached value that tells whether the New Tab Page feature is enabled.
   */
  _enabled: null,

  /**
   * Adds a page to the internal list of pages.
   * @param aPage The page to register.
   */
  register: function AllPages_register(aPage) {
    this._pages.push(aPage);
    this._addObserver();
  },

  /**
   * Removes a page from the internal list of pages.
   * @param aPage The page to unregister.
   */
  unregister: function AllPages_unregister(aPage) {
    let index = this._pages.indexOf(aPage);
    if (index > -1) {
      this._pages.splice(index, 1);
    }
  },

  /**
   * Returns whether the 'New Tab Page' is enabled.
   */
  get enabled() {
    if (this._enabled === null) {
      this._enabled = Services.prefs.getBoolPref(PREF_NEWTAB_ENABLED, false);
    }

    return this._enabled;
  },

  /**
   * Enables or disables the 'New Tab Page' feature.
   */
  set enabled(aEnabled) {
    if (this.enabled != aEnabled) {
      Services.prefs.setBoolPref(PREF_NEWTAB_ENABLED, !!aEnabled);
    }
  },

  /**
   * Returns the number of registered New Tab Pages (i.e. the number of open
   * about:newtab instances).
   */
  get length() {
    return this._pages.length;
  },

  /**
   * Updates all currently active pages but the given one.
   * @param aExceptPage The page to exclude from updating.
   * @param aReason The reason for updating all pages.
   */
  update(aExceptPage, aReason = "") {
    for (let page of this._pages.slice()) {
      if (aExceptPage != page) {
        page.update(aReason);
      }
    }
  },

  /**
   * Implements the nsIObserver interface to get notified when the preference
   * value changes or when a new copy of a page thumbnail is available.
   */
  observe: function AllPages_observe(aSubject, aTopic, aData) {
    if (aTopic == "nsPref:changed") {
      // Clear the cached value.
      switch (aData) {
        case PREF_NEWTAB_ENABLED:
          this._enabled = null;
          break;
      }
    }
    // and all notifications get forwarded to each page.
    this._pages.forEach(function (aPage) {
      aPage.observe(aSubject, aTopic, aData);
    }, this);
  },

  /**
   * Adds a preference and new thumbnail observer and turns itself into a
   * no-op after the first invokation.
   */
  _addObserver: function AllPages_addObserver() {
    Services.prefs.addObserver(PREF_NEWTAB_ENABLED, this);
    Services.obs.addObserver(this, "page-thumbnail:create");
    this._addObserver = function () {};
  },

  QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),
};

/**
 * Singleton that keeps track of all pinned links and their positions in the
 * grid.
 */
var PinnedLinks = {
  /**
   * The cached list of pinned links.
   */
  _links: null,

  /**
   * The array of pinned links.
   */
  get links() {
    if (!this._links) {
      this._links = lazy.Storage.get("pinnedLinks", []);
    }

    return this._links;
  },

  /**
   * Pins a link at the given position.
   * @param aLink The link to pin.
   * @param aIndex The grid index to pin the cell at.
   * @return true if link changes, false otherwise
   */
  pin: function PinnedLinks_pin(aLink, aIndex) {
    // Clear the link's old position, if any.
    this.unpin(aLink);

    // change pinned link into a history link
    let changed = this._makeHistoryLink(aLink);
    this.links[aIndex] = aLink;
    this.save();
    return changed;
  },

  /**
   * Unpins a given link.
   * @param aLink The link to unpin.
   */
  unpin: function PinnedLinks_unpin(aLink) {
    let index = this._indexOfLink(aLink);
    if (index == -1) {
      return;
    }
    let links = this.links;
    links[index] = null;
    // trim trailing nulls
    let i = links.length - 1;
    while (i >= 0 && links[i] == null) {
      i--;
    }
    links.splice(i + 1);
    this.save();
  },

  /**
   * Saves the current list of pinned links.
   */
  save: function PinnedLinks_save() {
    lazy.Storage.set("pinnedLinks", this.links);
  },

  /**
   * Checks whether a given link is pinned.
   * @params aLink The link to check.
   * @return whether The link is pinned.
   */
  isPinned: function PinnedLinks_isPinned(aLink) {
    return this._indexOfLink(aLink) != -1;
  },

  /**
   * Resets the links cache.
   */
  resetCache: function PinnedLinks_resetCache() {
    this._links = null;
  },

  /**
   * Finds the index of a given link in the list of pinned links.
   * @param aLink The link to find an index for.
   * @return The link's index.
   */
  _indexOfLink: function PinnedLinks_indexOfLink(aLink) {
    for (let i = 0; i < this.links.length; i++) {
      let link = this.links[i];
      if (link && link.url == aLink.url) {
        return i;
      }
    }

    // The given link is unpinned.
    return -1;
  },

  /**
   * Transforms link into a "history" link
   * @param aLink The link to change
   * @return true if link changes, false otherwise
   */
  _makeHistoryLink: function PinnedLinks_makeHistoryLink(aLink) {
    if (!aLink.type || aLink.type == "history") {
      return false;
    }
    aLink.type = "history";
    return true;
  },

  /**
   * Replaces existing link with another link.
   * @param aUrl The url of existing link
   * @param aLink The replacement link
   */
  replace: function PinnedLinks_replace(aUrl, aLink) {
    let index = this._indexOfLink({ url: aUrl });
    if (index == -1) {
      return;
    }
    this.links[index] = aLink;
    this.save();
  },
};

/**
 * Singleton that keeps track of all blocked links in the grid.
 */
var BlockedLinks = {
  /**
   * A list of objects that are observing blocked link changes.
   */
  _observers: [],

  /**
   * The cached list of blocked links.
   */
  _links: null,

  /**
   * Registers an object that will be notified when the blocked links change.
   */
  addObserver(aObserver) {
    this._observers.push(aObserver);
  },

  /**
   * Remove the observers.
   */
  removeObservers() {
    this._observers = [];
  },

  /**
   * The list of blocked links.
   */
  get links() {
    if (!this._links) {
      this._links = lazy.Storage.get("blockedLinks", {});
    }

    return this._links;
  },

  /**
   * Blocks a given link. Adjusts siteMap accordingly, and notifies listeners.
   * @param aLink The link to block.
   */
  block: function BlockedLinks_block(aLink) {
    this._callObservers("onLinkBlocked", aLink);
    this.links[toHash(aLink.url)] = 1;
    this.save();

    // Make sure we unpin blocked links.
    PinnedLinks.unpin(aLink);
  },

  /**
   * Unblocks a given link. Adjusts siteMap accordingly, and notifies listeners.
   * @param aLink The link to unblock.
   */
  unblock: function BlockedLinks_unblock(aLink) {
    if (this.isBlocked(aLink)) {
      delete this.links[toHash(aLink.url)];
      this.save();
      this._callObservers("onLinkUnblocked", aLink);
    }
  },

  /**
   * Saves the current list of blocked links.
   */
  save: function BlockedLinks_save() {
    lazy.Storage.set("blockedLinks", this.links);
  },

  /**
   * Returns whether a given link is blocked.
   * @param aLink The link to check.
   */
  isBlocked: function BlockedLinks_isBlocked(aLink) {
    return toHash(aLink.url) in this.links;
  },

  /**
   * Checks whether the list of blocked links is empty.
   * @return Whether the list is empty.
   */
  isEmpty: function BlockedLinks_isEmpty() {
    return !Object.keys(this.links).length;
  },

  /**
   * Resets the links cache.
   */
  resetCache: function BlockedLinks_resetCache() {
    this._links = null;
  },

  _callObservers(methodName, ...args) {
    for (let obs of this._observers) {
      if (typeof obs[methodName] == "function") {
        try {
          obs[methodName](...args);
        } catch (err) {
          console.error(err);
        }
      }
    }
  },
};

/**
 * Singleton that serves as the default link provider for the grid. It queries
 * the history to retrieve the most frequently visited sites.
 */
var PlacesProvider = {
  /**
   * Set this to change the maximum number of links the provider will provide.
   */
  maxNumLinks: HISTORY_RESULTS_LIMIT,

  /**
   * Must be called before the provider is used.
   */
  init: function PlacesProvider_init() {
    this._placesObserver = new PlacesWeakCallbackWrapper(
      this.handlePlacesEvents.bind(this)
    );
    PlacesObservers.addListener(
      ["page-visited", "page-title-changed", "pages-rank-changed"],
      this._placesObserver
    );
  },

  /**
   * Gets the current set of links delivered by this provider.
   * @param aCallback The function that the array of links is passed to.
   */
  getLinks: function PlacesProvider_getLinks(aCallback) {
    let options = lazy.PlacesUtils.history.getNewQueryOptions();
    options.maxResults = this.maxNumLinks;

    // Sort by frecency, descending.
    options.sortingMode =
      Ci.nsINavHistoryQueryOptions.SORT_BY_FRECENCY_DESCENDING;

    let links = [];

    let callback = {
      handleResult(aResultSet) {
        let row;

        while ((row = aResultSet.getNextRow())) {
          let url = row.getResultByIndex(1);
          if (LinkChecker.checkLoadURI(url)) {
            let title = row.getResultByIndex(2);
            let frecency = row.getResultByIndex(12);
            let lastVisitDate = row.getResultByIndex(5);
            links.push({
              url,
              title,
              frecency,
              lastVisitDate,
              type: "history",
            });
          }
        }
      },

      handleError() {
        // Should we somehow handle this error?
        aCallback([]);
      },

      handleCompletion() {
        // The Places query breaks ties in frecency by place ID descending, but
        // that's different from how Links.compareLinks breaks ties, because
        // compareLinks doesn't have access to place IDs.  It's very important
        // that the initial list of links is sorted in the same order imposed by
        // compareLinks, because Links uses compareLinks to perform binary
        // searches on the list.  So, ensure the list is so ordered.
        let i = 1;
        let outOfOrder = [];
        while (i < links.length) {
          if (Links.compareLinks(links[i - 1], links[i]) > 0) {
            outOfOrder.push(links.splice(i, 1)[0]);
          } else {
            i++;
          }
        }
        for (let link of outOfOrder) {
          i = lazy.BinarySearch.insertionIndexOf(
            Links.compareLinks,
            links,
            link
          );
          links.splice(i, 0, link);
        }

        aCallback(links);
      },
    };

    // Execute the query.
    let query = lazy.PlacesUtils.history.getNewQuery();
    lazy.PlacesUtils.history.asyncExecuteLegacyQuery(query, options, callback);
  },

  /**
   * Registers an object that will be notified when the provider's links change.
   * @param aObserver An object with the following optional properties:
   *        * onLinkChanged: A function that's called when a single link
   *          changes.  It's passed the provider and the link object.  Only the
   *          link's `url` property is guaranteed to be present.  If its `title`
   *          property is present, then its title has changed, and the
   *          property's value is the new title.  If any sort properties are
   *          present, then its position within the provider's list of links may
   *          have changed, and the properties' values are the new sort-related
   *          values.  Note that this link may not necessarily have been present
   *          in the lists returned from any previous calls to getLinks.
   *        * onManyLinksChanged: A function that's called when many links
   *          change at once.  It's passed the provider.  You should call
   *          getLinks to get the provider's new list of links.
   */
  addObserver: function PlacesProvider_addObserver(aObserver) {
    this._observers.push(aObserver);
  },

  _observers: [],

  handlePlacesEvents(aEvents) {
    for (let event of aEvents) {
      switch (event.type) {
        case "page-visited": {
          if (event.visitCount == 1 && event.lastKnownTitle) {
            this._callObservers("onLinkChanged", {
              url: event.url,
              title: event.lastKnownTitle,
            });
          }
          break;
        }
        case "page-title-changed": {
          this._callObservers("onLinkChanged", {
            url: event.url,
            title: event.title,
          });
          break;
        }
        case "pages-rank-changed": {
          this._callObservers("onManyLinksChanged");
          break;
        }
      }
    }
  },

  _callObservers: function PlacesProvider__callObservers(aMethodName, aArg) {
    for (let obs of this._observers) {
      if (obs[aMethodName]) {
        try {
          obs[aMethodName](this, aArg);
        } catch (err) {
          console.error(err);
        }
      }
    }
  },
};

/**
 * Queries history to retrieve the most frecent sites. Emits events when the
 * history changes.
 */
var ActivityStreamProvider = {
  THUMB_FAVICON_SIZE: 96,

  /**
   * Shared adjustment for selecting potentially blocked links.
   */
  _adjustLimitForBlocked({ ignoreBlocked, numItems }) {
    // Just use the usual number if blocked links won't be filtered out
    if (ignoreBlocked) {
      return numItems;
    }
    // Additionally select the number of blocked links in case they're removed
    return Object.keys(BlockedLinks.links).length + numItems;
  },

  /**
   * Shared sub-SELECT to get the guid of a bookmark of the current url while
   * avoiding LEFT JOINs on moz_bookmarks. This avoids gettings tags. The guid
   * could be one of multiple possible guids. Assumes `moz_places h` is in FROM.
   */
  _commonBookmarkGuidSelect: `(
    SELECT guid
    FROM moz_bookmarks b
    WHERE fk = h.id
      AND type = :bookmarkType
      AND (
        SELECT id
        FROM moz_bookmarks p
        WHERE p.id = b.parent
          AND p.parent <> :tagsFolderId
      ) NOTNULL
    ) AS bookmarkGuid`,

  /**
   * Shared WHERE expression filtering out undesired pages, e.g., hidden,
   * unvisited, and non-http/s urls. Assumes moz_places is in FROM / JOIN.
   *
   * NB: SUBSTR(url) is used even without an index instead of url_hash because
   * most desired pages will match http/s, so it will only run on the ~10s of
   * rows matched. If url_hash were to be used, it should probably *not* be used
   * by the query optimizer as we primarily want it optimized for the other
   * conditions, e.g., most frecent first.
   */
  _commonPlacesWhere: `
    AND hidden = 0
    AND last_visit_date > 0
    AND (SUBSTR(url, 1, 6) == "https:"
      OR SUBSTR(url, 1, 5) == "http:")
  `,

  /**
   * Shared parameters for getting correct bookmarks and LIMITed queries.
   */
  _getCommonParams(aOptions, aParams = {}) {
    return Object.assign(
      {
        bookmarkType: lazy.PlacesUtils.bookmarks.TYPE_BOOKMARK,
        limit: this._adjustLimitForBlocked(aOptions),
        tagsFolderId: lazy.PlacesUtils.tagsFolderId,
      },
      aParams
    );
  },

  /**
   * Shared columns for Highlights related queries.
   */
  _highlightsColumns: [
    "bookmarkGuid",
    "description",
    "guid",
    "preview_image_url",
    "title",
    "url",
  ],

  /**
   * Shared post-processing of Highlights links.
   */
  _processHighlights(aLinks, aOptions, aType) {
    // Filter out blocked if necessary
    if (!aOptions.ignoreBlocked) {
      aLinks = aLinks.filter(
        link =>
          !BlockedLinks.isBlocked(
            link.pocket_id ? { url: link.open_url } : link
          )
      );
    }

    // Limit the results to the requested number and set a type corresponding to
    // which query selected it
    return aLinks.slice(0, aOptions.numItems).map(item =>
      Object.assign(item, {
        type: aType,
      })
    );
  },

  /**
   * From an Array of links, if favicons are present, convert to data URIs
   *
   * @param {Array} aLinks
   *          an array containing objects with favicon data and mimeTypes
   *
   * @returns {Array} an array of links with favicons as data uri
   */
  _faviconBytesToDataURI(aLinks) {
    return aLinks.map(link => {
      if (link.favicon) {
        let encodedData = btoa(String.fromCharCode.apply(null, link.favicon));
        link.favicon = `data:${link.mimeType};base64,${encodedData}`;
        delete link.mimeType;
      }

      if (link.smallFavicon) {
        let encodedData = btoa(
          String.fromCharCode.apply(null, link.smallFavicon)
        );
        link.smallFavicon = `data:${link.smallFaviconMimeType};base64,${encodedData}`;
        delete link.smallFaviconMimeType;
      }

      return link;
    });
  },

  /**
   * Get favicon data (and metadata) for a uri. Fetches both the largest favicon
   * available, for Activity Stream; and a normal-sized favicon, for the Urlbar.
   *
   * @param {nsIURI} aUri Page to check for favicon data
   * @param {number} preferredFaviconWidth
   *   The preferred width of the of the normal-sized favicon in pixels.
   * @returns A promise of an object (possibly empty) containing the data.
   */
  async _loadIcons(aUri, preferredFaviconWidth) {
    let iconData = {};
    // Fetch the largest icon available.
    try {
      let faviconData = await lazy.PlacesUtils.favicons.getFaviconForPage(
        aUri,
        this.THUMB_FAVICON_SIZE
      );
      let rawData = faviconData.rawData;
      Object.assign(iconData, {
        favicon: rawData,
        faviconLength: rawData.length,
        faviconRef: faviconData.uri.ref,
        faviconSize: faviconData.width,
        mimeType: faviconData.mimeType,
      });
    } catch (e) {
      // Return early because fetching the largest favicon is the primary
      // purpose of NewTabUtils.
      return null;
    }

    // Also fetch a smaller icon.
    try {
      let faviconData = await lazy.PlacesUtils.favicons.getFaviconForPage(
        aUri,
        preferredFaviconWidth
      );
      let rawData = faviconData.rawData;
      Object.assign(iconData, {
        smallFavicon: rawData,
        smallFaviconLength: rawData.length,
        smallFaviconRef: faviconData.uri.ref,
        smallFaviconSize: faviconData.width,
        smallFaviconMimeType: faviconData.mimeType,
      });
    } catch (e) {
      // Do nothing with the error since we still have the large favicon fields.
    }

    return iconData;
  },

  /**
   * Computes favicon data for each url in a set of links
   *
   * @param {Array} links
   *          an array containing objects without favicon data or mimeTypes yet
   *
   * @returns {Promise} Returns a promise with the array of links with the largest
   *                    favicon available (as a byte array), mimeType, byte array
   *                    length, and favicon size (width)
   */
  _addFavicons(aLinks) {
    let win;
    if (BrowserWindowTracker) {
      win = BrowserWindowTracker.getTopWindow();
    }
    // We fetch two copies of a page's favicon: the largest available, for
    // Activity Stream; and a smaller size appropriate for the Urlbar.
    const preferredFaviconWidth =
      DEFAULT_SMALL_FAVICON_WIDTH * (win ? win.devicePixelRatio : 2);
    // Each link in the array needs a favicon for it's page - so we fire off a
    // promise for each link to compute the favicon data and attach it back to
    // the original link object. We must wait until all favicons for the array
    // of links are computed before returning
    return Promise.all(
      aLinks.map(
        link =>
          // eslint-disable-next-line no-async-promise-executor
          new Promise(async resolve => {
            // Never add favicon data for pocket items
            if (link.type === "pocket") {
              resolve(link);
              return;
            }
            let iconData;
            try {
              let linkUri = Services.io.newURI(link.url);
              iconData = await this._loadIcons(linkUri, preferredFaviconWidth);

              // Switch the scheme to try again with the other
              if (!iconData) {
                linkUri = linkUri
                  .mutate()
                  .setScheme(linkUri.scheme === "https" ? "http" : "https")
                  .finalize();
                iconData = await this._loadIcons(
                  linkUri,
                  preferredFaviconWidth
                );
              }
            } catch (e) {
              // We just won't put icon data on the link
            }

            // Add the icon data to the link if we have any
            resolve(Object.assign(link, iconData));
          })
      )
    );
  },

  /**
   * Helper function which makes the call to the Pocket API to fetch the user's
   * saved Pocket items.
   */
  fetchSavedPocketItems(requestData) {
    const latestSince =
      Services.prefs.getStringPref(PREF_POCKET_LATEST_SINCE, 0) * 1000;

    // Do not fetch Pocket items for users that have been inactive for too long, or are not logged in
    if (
      !lazy.pktApi.isUserLoggedIn() ||
      Date.now() - latestSince > POCKET_INACTIVE_TIME
    ) {
      return Promise.resolve(null);
    }

    return new Promise((resolve, reject) => {
      lazy.pktApi.retrieve(requestData, {
        success(data) {
          resolve(data);
        },
        error(error) {
          reject(error);
        },
      });
    });
  },

  /**
   * Get the most recently Pocket-ed items from a user's Pocket list. See:
   * https://getpocket.com/developer/docs/v3/retrieve for details
   *
   * @param {Object} aOptions
   *   {int} numItems: The max number of pocket items to fetch
   */
  async getRecentlyPocketed(aOptions) {
    const pocketSecondsAgo =
      Math.floor(Date.now() / 1000) - ACTIVITY_STREAM_DEFAULT_RECENT;
    const requestData = {
      detailType: "complete",
      count: aOptions.numItems,
      since: pocketSecondsAgo,
    };
    let data;
    try {
      data = await this.fetchSavedPocketItems(requestData);
      if (!data) {
        return [];
      }
    } catch (e) {
      console.error(e);
      return [];
    }
    /* Extract relevant parts needed to show this card as a highlight:
     * url, preview image, title, description, and the unique item_id
     * necessary for Pocket to identify the item
     */
    let items = Object.values(data.list)
      // status "0" means not archived or deleted
      .filter(item => item.status === "0")
      .map(item => ({
        date_added: item.time_added * 1000,
        description: item.excerpt,
        preview_image_url: item.image && item.image.src,
        title: item.resolved_title,
        url: item.resolved_url,
        pocket_id: item.item_id,
        open_url: item.open_url,
      }));

    // Append the query param to let Pocket know this item came from highlights
    for (let item of items) {
      let url = new URL(item.open_url);
      url.searchParams.append("src", "fx_new_tab");
      item.open_url = url.href;
    }

    return this._processHighlights(items, aOptions, "pocket");
  },

  /**
   * Get most-recently-created visited bookmarks for Activity Stream.
   *
   * @param {Object} aOptions
   *   {num}  bookmarkSecondsAgo: Maximum age of added bookmark.
   *   {bool} ignoreBlocked: Do not filter out blocked links.
   *   {int}  numItems: Maximum number of items to return.
   */
  async getRecentBookmarks(aOptions) {
    const options = Object.assign(
      {
        bookmarkSecondsAgo: ACTIVITY_STREAM_DEFAULT_RECENT,
        ignoreBlocked: false,
        numItems: ACTIVITY_STREAM_DEFAULT_LIMIT,
      },
      aOptions || {}
    );

    const sqlQuery = `
      SELECT
        b.guid AS bookmarkGuid,
        description,
        h.guid,
        preview_image_url,
        b.title,
        b.dateAdded / 1000 AS date_added,
        url
      FROM moz_bookmarks b
      JOIN moz_bookmarks p
        ON p.id = b.parent
      JOIN moz_places h
        ON h.id = b.fk
      WHERE b.dateAdded >= :dateAddedThreshold
        AND b.title NOTNULL
        AND b.type = :bookmarkType
        AND p.parent <> :tagsFolderId
        ${this._commonPlacesWhere}
      ORDER BY b.dateAdded DESC
      LIMIT :limit
    `;

    return this._processHighlights(
      await this.executePlacesQuery(sqlQuery, {
        columns: [...this._highlightsColumns, "date_added"],
        params: this._getCommonParams(options, {
          dateAddedThreshold:
            (Date.now() - options.bookmarkSecondsAgo * 1000) * 1000,
        }),
      }),
      options,
      "bookmark"
    );
  },

  /**
   * Get total count of all bookmarks.
   * Note: this includes default bookmarks
   *
   * @return {int} The number bookmarks in the places DB.
   */
  async getTotalBookmarksCount() {
    let sqlQuery = `
      SELECT count(*) FROM moz_bookmarks b
      JOIN moz_bookmarks t ON t.id = b.parent
      AND t.parent <> :tags_folder
     WHERE b.type = :type_bookmark
    `;

    const result = await this.executePlacesQuery(sqlQuery, {
      params: {
        tags_folder: lazy.PlacesUtils.tagsFolderId,
        type_bookmark: lazy.PlacesUtils.bookmarks.TYPE_BOOKMARK,
      },
    });

    return result[0][0];
  },

  /**
   * Get most-recently-visited history with metadata for Activity Stream.
   *
   * @param {Object} aOptions
   *   {bool} ignoreBlocked: Do not filter out blocked links.
   *   {int}  numItems: Maximum number of items to return.
   */
  async getRecentHistory(aOptions) {
    const options = Object.assign(
      {
        ignoreBlocked: false,
        numItems: ACTIVITY_STREAM_DEFAULT_LIMIT,
      },
      aOptions || {}
    );

    const sqlQuery = `
      SELECT
        ${this._commonBookmarkGuidSelect},
        description,
        guid,
        preview_image_url,
        title,
        url
      FROM moz_places h
      WHERE description NOTNULL
        AND preview_image_url NOTNULL
        ${this._commonPlacesWhere}
      ORDER BY last_visit_date DESC
      LIMIT :limit
    `;

    return this._processHighlights(
      await this.executePlacesQuery(sqlQuery, {
        columns: this._highlightsColumns,
        params: this._getCommonParams(options),
      }),
      options,
      "history"
    );
  },

  /*
   * Gets the top frecent sites for Activity Stream.
   *
   * @param {Object} aOptions
   *   {bool} ignoreBlocked: Do not filter out blocked links.
   *   {int}  numItems: Maximum number of items to return.
   *   {int}  topsiteFrecency: Minimum amount of frecency for a site.
   *   {bool} onePerDomain: Dedupe the resulting list.
   *   {bool} includeFavicon: Include favicons if available.
   *   {string} hideWithSearchParam: URLs that contain this search param will be
   *     excluded from the returned links. This value should be either undefined
   *     or a string with one of the following forms:
   *     - undefined: Fall back to the value of pref
   *       `browser.newtabpage.activity-stream.hideTopSitesWithSearchParam`
   *     - "" (empty) - Disable this feature
   *     - "key" - Search param named "key" with any or no value
   *     - "key=" - Search param named "key" with no value
   *     - "key=value" - Search param named "key" with value "value"
   *
   * @returns {Promise} Returns a promise with the array of links as payload.
   */
  async getTopFrecentSites(aOptions) {
    const options = Object.assign(
      {
        ignoreBlocked: false,
        numItems: ACTIVITY_STREAM_DEFAULT_LIMIT,
        topsiteFrecency: ACTIVITY_STREAM_DEFAULT_FRECENCY,
        onePerDomain: true,
        includeFavicon: true,
        hideWithSearchParam: Services.prefs.getCharPref(
          "browser.newtabpage.activity-stream.hideTopSitesWithSearchParam",
          ""
        ),
      },
      aOptions || {}
    );

    // Double the item count in case the host is deduped between with www or
    // not-www (i.e., 2 hosts) and an extra buffer for multiple pages per host.
    const origNumItems = options.numItems;
    if (options.onePerDomain) {
      options.numItems *= 2 * 10;
    }

    // Keep this query fast with frecency-indexed lookups (even with excess
    // rows) and shift the more complex logic to post-processing afterwards
    const sqlQuery = `
      SELECT
        ${this._commonBookmarkGuidSelect},
        frecency,
        guid,
        last_visit_date / 1000 AS lastVisitDate,
        rev_host,
        title,
        url,
        "history" as type
      FROM moz_places h
      WHERE frecency >= :frecencyThreshold
        ${this._commonPlacesWhere}
      ORDER BY frecency DESC
      LIMIT :limit
    `;

    let links = await this.executePlacesQuery(sqlQuery, {
      columns: [
        "bookmarkGuid",
        "frecency",
        "guid",
        "lastVisitDate",
        "title",
        "url",
        "type",
      ],
      params: this._getCommonParams(options, {
        frecencyThreshold: options.topsiteFrecency,
      }),
    });

    // Determine if the other link is "better" (larger frecency, more recent,
    // lexicographically earlier url)
    function isOtherBetter(link, other) {
      if (other.frecency === link.frecency) {
        if (other.lastVisitDate === link.lastVisitDate) {
          return other.url < link.url;
        }
        return other.lastVisitDate > link.lastVisitDate;
      }
      return other.frecency > link.frecency;
    }

    // Update a host Map with the better link
    function setBetterLink(map, link, hostMatcher, combiner = () => {}) {
      const host = hostMatcher(link.url)[1];
      if (map.has(host)) {
        const other = map.get(host);
        if (isOtherBetter(link, other)) {
          link = other;
        }
        combiner(link, other);
      }
      map.set(host, link);
    }

    // Convert all links that are supposed to be a seach shortcut to its canonical URL
    if (
      Services.prefs.getBoolPref(
        `browser.newtabpage.activity-stream.${SEARCH_SHORTCUTS_EXPERIMENT}`,
        false
      )
    ) {
      links.forEach(link => {
        let searchProvider = getSearchProvider(NewTabUtils.shortURL(link));
        if (searchProvider) {
          link.url = searchProvider.url;
        }
      });
    }

    // Remove links that contain the hide-with search param.
    if (options.hideWithSearchParam) {
      let [key, value] = options.hideWithSearchParam.split("=");
      links = links.filter(link => {
        let searchParams = URL.parse(link.url)?.searchParams;
        if (searchParams) {
          return value === undefined
            ? !searchParams.has(key)
            : !searchParams.getAll(key).includes(value);
        }
        return true;
      });
    }

    // Remove any blocked links.
    if (!options.ignoreBlocked) {
      links = links.filter(link => !BlockedLinks.isBlocked(link));
    }

    if (options.onePerDomain) {
      // De-dup the links.
      const exactHosts = new Map();
      for (const link of links) {
        // First we want to find the best link for an exact host
        setBetterLink(exactHosts, link, url => url.match(/:\/\/([^\/]+)/));
      }

      // Clean up exact hosts to dedupe as non-www hosts
      const hosts = new Map();
      for (const link of exactHosts.values()) {
        setBetterLink(
          hosts,
          link,
          url => url.match(/:\/\/(?:www\.)?([^\/]+)/),
          // Combine frecencies when deduping these links
          (targetLink, otherLink) => {
            targetLink.frecency = link.frecency + otherLink.frecency;
          }
        );
      }

      links = [...hosts.values()];
    }
    // Pick out the top links using the same comparer as before
    links = links.sort(isOtherBetter).slice(0, origNumItems);

    if (!options.includeFavicon) {
      return links;
    }
    // Get the favicons as data URI for now (until we use the favicon protocol)
    return this._faviconBytesToDataURI(await this._addFavicons(links));
  },

  /**
   * Gets a specific bookmark given some info about it
   *
   * @param {Obj} aInfo
   *          An object with one and only one of the following properties:
   *            - url
   *            - guid
   *            - parentGuid and index
   */
  async getBookmark(aInfo) {
    let bookmark = await lazy.PlacesUtils.bookmarks.fetch(aInfo);
    if (!bookmark) {
      return null;
    }
    let result = {};
    result.bookmarkGuid = bookmark.guid;
    result.bookmarkTitle = bookmark.title;
    result.lastModified = bookmark.lastModified.getTime();
    result.url = bookmark.url.href;
    return result;
  },

  /**
   * Count the number of visited urls grouped by day
   */
  getUserMonthlyActivity() {
    let sqlQuery = `
      SELECT count(*),
        strftime('%Y-%m-%d', visit_date/1000000.0, 'unixepoch', 'localtime') as date_format
      FROM moz_historyvisits
      WHERE visit_date > 0
      AND visit_date > strftime('%s','now','localtime','start of day','-27 days','utc') * 1000000
      GROUP BY date_format
      ORDER BY date_format ASC
    `;

    return this.executePlacesQuery(sqlQuery);
  },

  /**
   * Executes arbitrary query against places database
   *
   * @param {String} aQuery
   *        SQL query to execute
   * @param {Object} [optional] aOptions
   *          aOptions.columns - an array of column names. if supplied the return
   *          items will consists of objects keyed on column names. Otherwise
   *          array of raw values is returned in the select order
   *          aOptions.param - an object of SQL binding parameters
   *
   * @returns {Promise} Returns a promise with the array of retrieved items
   */
  async executePlacesQuery(aQuery, aOptions = {}) {
    let { columns, params } = aOptions;
    let items = [];
    let queryError = null;
    let conn = await lazy.PlacesUtils.promiseDBConnection();
    await conn.executeCached(aQuery, params, (aRow, aCancel) => {
      try {
        let item = null;
        // if columns array is given construct an object
        if (columns && Array.isArray(columns)) {
          item = {};
          columns.forEach(column => {
            item[column] = aRow.getResultByName(column);
          });
        } else {
          // if no columns - make an array of raw values
          item = [];
          for (let i = 0; i < aRow.numEntries; i++) {
            item.push(aRow.getResultByIndex(i));
          }
        }
        items.push(item);
      } catch (e) {
        queryError = e;
        aCancel();
      }
    });
    if (queryError) {
      throw new Error(queryError);
    }
    return items;
  },
};

/**
 * A set of actions which influence what sites shown on the Activity Stream page
 */
var ActivityStreamLinks = {
  _savedPocketStories: null,
  _pocketLastUpdated: 0,
  _pocketLastLatest: 0,

  /**
   * Block a url
   *
   * @param {Object} aLink
   *          The link which contains a URL to add to the block list
   */
  blockURL(aLink) {
    BlockedLinks.block(aLink);
    // If we're blocking a pocket item, invalidate the cache too
    if (aLink.pocket_id) {
      this._savedPocketStories = null;
    }
  },

  onLinkBlocked(aLink) {
    Services.obs.notifyObservers(null, "newtab-linkBlocked", aLink.url);
  },

  /**
   * Adds a bookmark and opens up the Bookmark Dialog to show feedback that
   * the bookmarking action has been successful
   *
   * @param {Object} aData
   *          aData.url The url to bookmark
   *          aData.title The title of the page to bookmark
   * @param {Window} aBrowserWindow
   *          The current browser chrome window
   *
   * @returns {Promise} Returns a promise set to an object representing the bookmark
   */
  addBookmark(aData, aBrowserWindow) {
    const { url, title } = aData;
    return aBrowserWindow.PlacesCommandHook.bookmarkLink(url, title);
  },

  /**
   * Removes a bookmark
   *
   * @param {String} aBookmarkGuid
   *          The bookmark guid associated with the bookmark to remove
   *
   * @returns {Promise} Returns a promise at completion.
   */
  deleteBookmark(aBookmarkGuid) {
    return lazy.PlacesUtils.bookmarks.remove(aBookmarkGuid);
  },

  /**
   * Removes a history link and unpins the URL if previously pinned
   *
   * @param {String} aUrl
   *           The url to be removed from history
   *
   * @returns {Promise} Returns a promise set to true if link was removed
   */
  deleteHistoryEntry(aUrl) {
    const url = aUrl;
    PinnedLinks.unpin({ url });
    return lazy.PlacesUtils.history.remove(url);
  },

  /**
   * Helper function which makes the call to the Pocket API to delete an item from
   * a user's saved to Pocket feed. Also, invalidate the Pocket stories cache
   *
   * @param {Integer} aItemID
   *           The unique pocket ID used to find the item to be deleted
   *
   *@returns {Promise} Returns a promise at completion
   */
  deletePocketEntry(aItemID) {
    this._savedPocketStories = null;
    return new Promise((success, error) =>
      lazy.pktApi.deleteItem(aItemID, { success, error })
    );
  },

  /**
   * Helper function which makes the call to the Pocket API to archive an item from
   * a user's saved to Pocket feed. Also, invalidate the Pocket stories cache
   *
   * @param {Integer} aItemID
   *           The unique pocket ID used to find the item to be archived
   *
   *@returns {Promise} Returns a promise at completion
   */
  archivePocketEntry(aItemID) {
    this._savedPocketStories = null;
    return new Promise((success, error) =>
      lazy.pktApi.archiveItem(aItemID, { success, error })
    );
  },

  /**
   * Helper function which makes the call to the Pocket API to save an item to
   * a user's saved to Pocket feed if they are logged in. Also, invalidate the
   * Pocket stories cache
   *
   * @param {String} aUrl
   *           The URL belonging to the story being saved
   * @param {String} aTitle
   *           The title belonging to the story being saved
   * @param {Browser} aBrowser
   *           The target browser to show the doorhanger in
   *
   *@returns {Promise} Returns a promise at completion
   */
  addPocketEntry(aUrl, aTitle, aBrowser) {
    // If the user is not logged in, show the panel to prompt them to log in
    if (!lazy.pktApi.isUserLoggedIn()) {
      lazy.Pocket.savePage(aBrowser, aUrl, aTitle);
      return Promise.resolve(null);
    }

    // If the user is logged in, just save the link to Pocket and Activity Stream
    // will update the page
    this._savedPocketStories = null;
    return new Promise((success, error) => {
      lazy.pktApi.addLink(aUrl, {
        title: aTitle,
        success,
        error,
      });
    });
  },

  /**
   * Get the Highlights links to show on Activity Stream
   *
   * @param {Object} aOptions
   *   {bool} excludeBookmarks: Don't add bookmark items.
   *   {bool} excludeHistory: Don't add history items.
   *   {bool} excludePocket: Don't add Pocket items.
   *   {bool} withFavicons: Add favicon data: URIs, when possible.
   *   {int}  numItems: Maximum number of (bookmark or history) items to return.
   *
   * @return {Promise} Returns a promise with the array of links as the payload
   */
  async getHighlights(aOptions = {}) {
    aOptions.numItems = aOptions.numItems || ACTIVITY_STREAM_DEFAULT_LIMIT;
    const results = [];

    // First get bookmarks if we want them
    if (!aOptions.excludeBookmarks) {
      results.push(
        ...(await ActivityStreamProvider.getRecentBookmarks(aOptions))
      );
    }

    // Add the Pocket items if we need more and want them
    if (aOptions.numItems - results.length > 0 && !aOptions.excludePocket) {
      const latestSince = ~~Services.prefs.getStringPref(
        PREF_POCKET_LATEST_SINCE,
        0
      );
      // Invalidate the cache, get new stories, and update timestamps if:
      //  1. we do not have saved to Pocket stories already cached OR
      //  2. it has been too long since we last got Pocket stories OR
      //  3. there has been a paged saved to pocket since we last got new stories
      if (
        !this._savedPocketStories ||
        Date.now() - this._pocketLastUpdated > POCKET_UPDATE_TIME ||
        this._pocketLastLatest < latestSince
      ) {
        this._savedPocketStories =
          await ActivityStreamProvider.getRecentlyPocketed(aOptions);
        this._pocketLastUpdated = Date.now();
        this._pocketLastLatest = latestSince;
      }
      results.push(...this._savedPocketStories);
    }

    // Add in history if we need more and want them
    if (aOptions.numItems - results.length > 0 && !aOptions.excludeHistory) {
      // Use the same numItems as bookmarks above in case we remove duplicates
      const history = await ActivityStreamProvider.getRecentHistory(aOptions);

      // Only include a url once in the result preferring the bookmark
      const bookmarkUrls = new Set(results.map(({ url }) => url));
      for (const page of history) {
        if (!bookmarkUrls.has(page.url)) {
          results.push(page);

          // Stop adding pages once we reach the desired maximum
          if (results.length === aOptions.numItems) {
            break;
          }
        }
      }
    }

    if (aOptions.withFavicons) {
      return ActivityStreamProvider._faviconBytesToDataURI(
        await ActivityStreamProvider._addFavicons(results)
      );
    }

    return results;
  },

  /**
   * Get the top sites to show on Activity Stream
   *
   * @return {Promise} Returns a promise with the array of links as the payload
   */
  async getTopSites(aOptions = {}) {
    return ActivityStreamProvider.getTopFrecentSites(aOptions);
  },
};

/**
 * Singleton that provides access to all links contained in the grid (including
 * the ones that don't fit on the grid). A link is a plain object that looks
 * like this:
 *
 * {
 *   url: "http://www.mozilla.org/",
 *   title: "Mozilla",
 *   frecency: 1337,
 *   lastVisitDate: 1394678824766431,
 * }
 */
var Links = {
  /**
   * The maximum number of links returned by getLinks.
   */
  maxNumLinks: LINKS_GET_LINKS_LIMIT,

  /**
   * A mapping from each provider to an object { sortedLinks, siteMap, linkMap }.
   * sortedLinks is the cached, sorted array of links for the provider.
   * siteMap is a mapping from base domains to URL count associated with the domain.
   *         The count does not include blocked URLs. siteMap is used to look up a
   *         user's top sites that can be targeted with a suggested tile.
   * linkMap is a Map from link URLs to link objects.
   */
  _providers: new Map(),

  /**
   * The properties of link objects used to sort them.
   */
  _sortProperties: ["frecency", "lastVisitDate", "url"],

  /**
   * List of callbacks waiting for the cache to be populated.
   */
  _populateCallbacks: [],

  /**
   * A list of objects that are observing links updates.
   */
  _observers: [],

  /**
   * Registers an object that will be notified when links updates.
   */
  addObserver(aObserver) {
    this._observers.push(aObserver);
  },

  /**
   * Adds a link provider.
   * @param aProvider The link provider.
   */
  addProvider: function Links_addProvider(aProvider) {
    this._providers.set(aProvider, null);
    aProvider.addObserver(this);
  },

  /**
   * Removes a link provider.
   * @param aProvider The link provider.
   */
  removeProvider: function Links_removeProvider(aProvider) {
    if (!this._providers.delete(aProvider)) {
      throw new Error("Unknown provider");
    }
  },

  /**
   * Populates the cache with fresh links from the providers.
   * @param aCallback The callback to call when finished (optional).
   * @param aForce When true, populates the cache even when it's already filled.
   */
  populateCache: function Links_populateCache(aCallback, aForce) {
    let callbacks = this._populateCallbacks;

    // Enqueue the current callback.
    callbacks.push(aCallback);

    // There was a callback waiting already, thus the cache has not yet been
    // populated.
    if (callbacks.length > 1) {
      return;
    }

    function executeCallbacks() {
      while (callbacks.length) {
        let callback = callbacks.shift();
        if (callback) {
          try {
            callback();
          } catch (e) {
            // We want to proceed even if a callback fails.
          }
        }
      }
    }

    let numProvidersRemaining = this._providers.size;
    for (let [provider /* , links */] of this._providers) {
      this._populateProviderCache(
        provider,
        () => {
          if (--numProvidersRemaining == 0) {
            executeCallbacks();
          }
        },
        aForce
      );
    }

    this._addObserver();
  },

  /**
   * Gets the current set of links contained in the grid.
   * @return The links in the grid.
   */
  getLinks: function Links_getLinks() {
    let pinnedLinks = Array.from(PinnedLinks.links);
    let links = this._getMergedProviderLinks();

    let sites = new Set();
    for (let link of pinnedLinks) {
      if (link) {
        sites.add(NewTabUtils.extractSite(link.url));
      }
    }

    // Filter blocked and pinned links and duplicate base domains.
    links = links.filter(function (link) {
      let site = NewTabUtils.extractSite(link.url);
      if (site == null || sites.has(site)) {
        return false;
      }
      sites.add(site);

      return !BlockedLinks.isBlocked(link) && !PinnedLinks.isPinned(link);
    });

    // Try to fill the gaps between pinned links.
    for (let i = 0; i < pinnedLinks.length && links.length; i++) {
      if (!pinnedLinks[i]) {
        pinnedLinks[i] = links.shift();
      }
    }

    // Append the remaining links if any.
    if (links.length) {
      pinnedLinks = pinnedLinks.concat(links);
    }

    for (let link of pinnedLinks) {
      if (link) {
        link.baseDomain = NewTabUtils.extractSite(link.url);
      }
    }
    return pinnedLinks;
  },

  /**
   * Resets the links cache.
   */
  resetCache: function Links_resetCache() {
    for (let provider of this._providers.keys()) {
      this._providers.set(provider, null);
    }
  },

  /**
   * Compares two links.
   * @param aLink1 The first link.
   * @param aLink2 The second link.
   * @return A negative number if aLink1 is ordered before aLink2, zero if
   *         aLink1 and aLink2 have the same ordering, or a positive number if
   *         aLink1 is ordered after aLink2.
   *
   * @note compareLinks's this object is bound to Links below.
   */
  compareLinks: function Links_compareLinks(aLink1, aLink2) {
    for (let prop of this._sortProperties) {
      if (!(prop in aLink1) || !(prop in aLink2)) {
        throw new Error("Comparable link missing required property: " + prop);
      }
    }
    return (
      aLink2.frecency - aLink1.frecency ||
      aLink2.lastVisitDate - aLink1.lastVisitDate ||
      aLink1.url.localeCompare(aLink2.url)
    );
  },

  _incrementSiteMap(map, link) {
    if (NewTabUtils.blockedLinks.isBlocked(link)) {
      // Don't count blocked URLs.
      return;
    }
    let site = NewTabUtils.extractSite(link.url);
    map.set(site, (map.get(site) || 0) + 1);
  },

  _decrementSiteMap(map, link) {
    if (NewTabUtils.blockedLinks.isBlocked(link)) {
      // Blocked URLs are not included in map.
      return;
    }
    let site = NewTabUtils.extractSite(link.url);
    let previousURLCount = map.get(site);
    if (previousURLCount === 1) {
      map.delete(site);
    } else {
      map.set(site, previousURLCount - 1);
    }
  },

  /**
   * Update the siteMap cache based on the link given and whether we need
   * to increment or decrement it. We do this by iterating over all stored providers
   * to find which provider this link already exists in. For providers that
   * have this link, we will adjust siteMap for them accordingly.
   *
   * @param aLink The link that will affect siteMap
   * @param increment A boolean for whether to increment or decrement siteMap
   */
  _adjustSiteMapAndNotify(aLink, increment = true) {
    for (let [, /* provider */ cache] of this._providers) {
      // We only update siteMap if aLink is already stored in linkMap.
      if (cache.linkMap.get(aLink.url)) {
        if (increment) {
          this._incrementSiteMap(cache.siteMap, aLink);
          continue;
        }
        this._decrementSiteMap(cache.siteMap, aLink);
      }
    }
    this._callObservers("onLinkChanged", aLink);
  },

  onLinkBlocked(aLink) {
    this._adjustSiteMapAndNotify(aLink, false);
  },

  onLinkUnblocked(aLink) {
    this._adjustSiteMapAndNotify(aLink);
  },

  populateProviderCache(provider, callback) {
    if (!this._providers.has(provider)) {
      throw new Error(
        "Can only populate provider cache for existing provider."
      );
    }

    return this._populateProviderCache(provider, callback, false);
  },

  /**
   * Calls getLinks on the given provider and populates our cache for it.
   * @param aProvider The provider whose cache will be populated.
   * @param aCallback The callback to call when finished.
   * @param aForce When true, populates the provider's cache even when it's
   *               already filled.
   */
  _populateProviderCache(aProvider, aCallback, aForce) {
    let cache = this._providers.get(aProvider);
    let createCache = !cache;
    if (createCache) {
      cache = {
        // Start with a resolved promise.
        populatePromise: new Promise(resolve => resolve()),
      };
      this._providers.set(aProvider, cache);
    }
    // Chain the populatePromise so that calls are effectively queued.
    cache.populatePromise = cache.populatePromise.then(() => {
      return new Promise(resolve => {
        if (!createCache && !aForce) {
          aCallback();
          resolve();
          return;
        }
        aProvider.getLinks(links => {
          // Filter out null and undefined links so we don't have to deal with
          // them in getLinks when merging links from providers.
          links = links.filter(link => !!link);
          cache.sortedLinks = links;
          cache.siteMap = links.reduce((map, link) => {
            this._incrementSiteMap(map, link);
            return map;
          }, new Map());
          cache.linkMap = links.reduce((map, link) => {
            map.set(link.url, link);
            return map;
          }, new Map());
          aCallback();
          resolve();
        });
      });
    });
  },

  /**
   * Merges the cached lists of links from all providers whose lists are cached.
   * @return The merged list.
   */
  _getMergedProviderLinks: function Links__getMergedProviderLinks() {
    // Build a list containing a copy of each provider's sortedLinks list.
    let linkLists = [];
    for (let provider of this._providers.keys()) {
      let links = this._providers.get(provider);
      if (links && links.sortedLinks) {
        linkLists.push(links.sortedLinks.slice());
      }
    }

    return this.mergeLinkLists(linkLists);
  },

  mergeLinkLists: function Links_mergeLinkLists(linkLists) {
    if (linkLists.length == 1) {
      return linkLists[0];
    }

    function getNextLink() {
      let minLinks = null;
      for (let links of linkLists) {
        if (
          links.length &&
          (!minLinks || Links.compareLinks(links[0], minLinks[0]) < 0)
        ) {
          minLinks = links;
        }
      }
      return minLinks ? minLinks.shift() : null;
    }

    let finalLinks = [];
    for (
      let nextLink = getNextLink();
      nextLink && finalLinks.length < this.maxNumLinks;
      nextLink = getNextLink()
    ) {
      finalLinks.push(nextLink);
    }

    return finalLinks;
  },

  /**
   * Called by a provider to notify us when a single link changes.
   * @param aProvider The provider whose link changed.
   * @param aLink The link that changed.  If the link is new, it must have all
   *              of the _sortProperties.  Otherwise, it may have as few or as
   *              many as is convenient.
   * @param aIndex The current index of the changed link in the sortedLinks
                   cache in _providers. Defaults to -1 if the provider doesn't know the index
   * @param aDeleted Boolean indicating if the provider has deleted the link.
   */
  onLinkChanged: function Links_onLinkChanged(
    aProvider,
    aLink,
    aIndex = -1,
    aDeleted = false
  ) {
    if (!("url" in aLink)) {
      throw new Error("Changed links must have a url property");
    }

    let links = this._providers.get(aProvider);
    if (!links) {
      // This is not an error, it just means that between the time the provider
      // was added and the future time we call getLinks on it, it notified us of
      // a change.
      return;
    }

    let { sortedLinks, siteMap, linkMap } = links;
    let existingLink = linkMap.get(aLink.url);
    let insertionLink = null;
    let updatePages = false;

    if (existingLink) {
      // Update our copy's position in O(lg n) by first removing it from its
      // list.  It's important to do this before modifying its properties.
      if (this._sortProperties.some(prop => prop in aLink)) {
        let idx = aIndex;
        if (idx < 0) {
          idx = this._indexOf(sortedLinks, existingLink);
        } else if (this.compareLinks(aLink, sortedLinks[idx]) != 0) {
          throw new Error("aLink should be the same as sortedLinks[idx]");
        }

        if (idx < 0) {
          throw new Error("Link should be in _sortedLinks if in _linkMap");
        }
        sortedLinks.splice(idx, 1);

        if (aDeleted) {
          updatePages = true;
          linkMap.delete(existingLink.url);
          this._decrementSiteMap(siteMap, existingLink);
        } else {
          // Update our copy's properties.
          Object.assign(existingLink, aLink);

          // Finally, reinsert our copy below.
          insertionLink = existingLink;
        }
      }
      // Update our copy's title in O(1).
      if ("title" in aLink && aLink.title != existingLink.title) {
        existingLink.title = aLink.title;
        updatePages = true;
      }
    } else if (this._sortProperties.every(prop => prop in aLink)) {
      // Before doing the O(lg n) insertion below, do an O(1) check for the
      // common case where the new link is too low-ranked to be in the list.
      if (sortedLinks.length && sortedLinks.length == aProvider.maxNumLinks) {
        let lastLink = sortedLinks[sortedLinks.length - 1];
        if (this.compareLinks(lastLink, aLink) < 0) {
          return;
        }
      }
      // Copy the link object so that changes later made to it by the caller
      // don't affect our copy.
      insertionLink = {};
      for (let prop in aLink) {
        insertionLink[prop] = aLink[prop];
      }
      linkMap.set(aLink.url, insertionLink);
      this._incrementSiteMap(siteMap, aLink);
    }

    if (insertionLink) {
      let idx = this._insertionIndexOf(sortedLinks, insertionLink);
      sortedLinks.splice(idx, 0, insertionLink);
      if (sortedLinks.length > aProvider.maxNumLinks) {
        let lastLink = sortedLinks.pop();
        linkMap.delete(lastLink.url);
        this._decrementSiteMap(siteMap, lastLink);
      }
      updatePages = true;
    }

    if (updatePages) {
      AllPages.update(null, "links-changed");
    }
  },

  /**
   * Called by a provider to notify us when many links change.
   */
  onManyLinksChanged: function Links_onManyLinksChanged(aProvider) {
    this._populateProviderCache(
      aProvider,
      () => {
        AllPages.update(null, "links-changed");
      },
      true
    );
  },

  _indexOf: function Links__indexOf(aArray, aLink) {
    return this._binsearch(aArray, aLink, "indexOf");
  },

  _insertionIndexOf: function Links__insertionIndexOf(aArray, aLink) {
    return this._binsearch(aArray, aLink, "insertionIndexOf");
  },

  _binsearch: function Links__binsearch(aArray, aLink, aMethod) {
    return lazy.BinarySearch[aMethod](this.compareLinks, aArray, aLink);
  },

  /**
   * Implements the nsIObserver interface to get notified about browser history
   * sanitization.
   */
  observe: function Links_observe() {
    // Make sure to update open about:newtab instances. If there are no opened
    // pages we can just wait for the next new tab to populate the cache again.
    if (AllPages.length && AllPages.enabled) {
      this.populateCache(function () {
        AllPages.update();
      }, true);
    } else {
      this.resetCache();
    }
  },

  _callObservers(methodName, ...args) {
    for (let obs of this._observers) {
      if (typeof obs[methodName] == "function") {
        try {
          obs[methodName](this, ...args);
        } catch (err) {
          console.error(err);
        }
      }
    }
  },

  /**
   * Adds a sanitization observer and turns itself into a no-op after the first
   * invokation.
   */
  _addObserver: function Links_addObserver() {
    Services.obs.addObserver(this, "browser:purge-session-history");
    this._addObserver = function () {};
  },

  QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),
};

Links.compareLinks = Links.compareLinks.bind(Links);

/**
 * Singleton used to collect telemetry data.
 *
 */
var Telemetry = {
  /**
   * Initializes object.
   */
  init: function Telemetry_init() {
    Services.obs.addObserver(this, TOPIC_GATHER_TELEMETRY);
  },

  uninit: function Telemetry_uninit() {
    Services.obs.removeObserver(this, TOPIC_GATHER_TELEMETRY);
  },

  /**
   * Collects data.
   */
  _collect: function Telemetry_collect() {
    Glean.newtabPage.pinnedSitesCount.accumulateSingleSample(
      PinnedLinks.links.length
    );
    Glean.newtabPage.blockedSitesCount.accumulateSingleSample(
      Object.keys(BlockedLinks.links).length
    );
  },

  /**
   * Listens for gather telemetry topic.
   */
  observe: function Telemetry_observe() {
    this._collect();
  },
};

/**
 * Singleton that checks if a given link should be displayed on about:newtab
 * or if we should rather not do it for security reasons. URIs that inherit
 * their caller's principal will be filtered.
 */
var LinkChecker = {
  _cache: {},

  get flags() {
    return (
      Ci.nsIScriptSecurityManager.DISALLOW_INHERIT_PRINCIPAL |
      Ci.nsIScriptSecurityManager.DONT_REPORT_ERRORS
    );
  },

  checkLoadURI: function LinkChecker_checkLoadURI(aURI) {
    if (!(aURI in this._cache)) {
      this._cache[aURI] = this._doCheckLoadURI(aURI);
    }

    return this._cache[aURI];
  },

  _doCheckLoadURI: function Links_doCheckLoadURI(aURI) {
    try {
      // about:newtab is currently privileged. In any case, it should be
      // possible for tiles to point to pretty much everything - but not
      // to stuff that inherits the system principal, so we check:
      let systemPrincipal = Services.scriptSecurityManager.getSystemPrincipal();
      Services.scriptSecurityManager.checkLoadURIStrWithPrincipal(
        systemPrincipal,
        aURI,
        this.flags
      );
      return true;
    } catch (e) {
      // We got a weird URI or one that would inherit the caller's principal.
      return false;
    }
  },
};

var ExpirationFilter = {
  init: function ExpirationFilter_init() {
    lazy.PageThumbs.addExpirationFilter(this);
  },

  filterForThumbnailExpiration:
    function ExpirationFilter_filterForThumbnailExpiration(aCallback) {
      if (!AllPages.enabled) {
        aCallback([]);
        return;
      }

      Links.populateCache(function () {
        let urls = [];

        // Add all URLs to the list that we want to keep thumbnails for.
        for (let link of Links.getLinks().slice(0, 25)) {
          if (link && link.url) {
            urls.push(link.url);
          }
        }

        aCallback(urls);
      });
    },
};

/**
 * Singleton that provides the public API of this JSM.
 */
export var NewTabUtils = {
  _initialized: false,

  /**
   * Extract a "site" from a url in a way that multiple urls of a "site" returns
   * the same "site."
   * @param aUrl Url spec string
   * @return The "site" string or null
   */
  extractSite: function Links_extractSite(url) {
    let host;
    try {
      // Note that nsIURI.asciiHost throws NS_ERROR_FAILURE for some types of
      // URIs, including jar and moz-icon URIs.
      host = Services.io.newURI(url).asciiHost;
    } catch (ex) {
      return null;
    }

    // Strip off common subdomains of the same site (e.g., www, load balancer)
    return host.replace(/^(m|mobile|www\d*)\./, "");
  },

  init: function NewTabUtils_init() {
    if (this.initWithoutProviders()) {
      PlacesProvider.init();
      Links.addProvider(PlacesProvider);
      BlockedLinks.addObserver(Links);
      BlockedLinks.addObserver(ActivityStreamLinks);
    }
  },

  initWithoutProviders: function NewTabUtils_initWithoutProviders() {
    if (!this._initialized) {
      this._initialized = true;
      ExpirationFilter.init();
      Telemetry.init();
      return true;
    }
    return false;
  },

  uninit: function NewTabUtils_uninit() {
    if (this.initialized) {
      Telemetry.uninit();
      BlockedLinks.removeObservers();
    }
  },

  getProviderLinks(aProvider) {
    let cache = Links._providers.get(aProvider);
    if (cache && cache.sortedLinks) {
      return cache.sortedLinks;
    }
    return [];
  },

  isTopSiteGivenProvider(aSite, aProvider) {
    let cache = Links._providers.get(aProvider);
    if (cache && cache.siteMap) {
      return cache.siteMap.has(aSite);
    }
    return false;
  },

  isTopPlacesSite(aSite) {
    return this.isTopSiteGivenProvider(aSite, PlacesProvider);
  },

  /**
   * Restores all sites that have been removed from the grid.
   */
  restore: function NewTabUtils_restore() {
    lazy.Storage.clear();
    Links.resetCache();
    PinnedLinks.resetCache();
    BlockedLinks.resetCache();

    Links.populateCache(function () {
      AllPages.update();
    }, true);
  },

  /**
   * Undoes all sites that have been removed from the grid and keep the pinned
   * tabs.
   * @param aCallback the callback method.
   */
  undoAll: function NewTabUtils_undoAll(aCallback) {
    lazy.Storage.remove("blockedLinks");
    Links.resetCache();
    BlockedLinks.resetCache();
    Links.populateCache(aCallback, true);
  },

  /**
   * Get the effective top level domain of a host.
   * @param {string} host The host to be analyzed.
   * @return {str} The suffix or empty string if there's no suffix.
   */
  getETLD: function NewTabUtils_getETLD(host) {
    try {
      return Services.eTLD.getPublicSuffixFromHost(host);
    } catch (err) {
      return "";
    }
  },

  /**
   * shortHostname - Creates a short version of a hostname, used for display purposes
   *            e.g. "www.foosite.com"  =>  "foosite"
   *
   * @param {string} hostname The full hostname
   * @returns {string} The shortened hostname
   */
  shortHostname: function NewTabUtils_shortHostname(hostname) {
    if (!hostname) {
      return "";
    }

    const newHostname = hostname.replace(/^www\./i, "").toLowerCase();

    // Remove the eTLD (e.g., com, net) and the preceding period from the hostname
    const eTLD = this.getETLD(newHostname);
    const eTLDExtra =
      eTLD.length && newHostname.endsWith(eTLD) ? -(eTLD.length + 1) : Infinity;

    return handleIDNHost(newHostname.slice(0, eTLDExtra) || newHostname);
  },

  /**
   * shortURL - Creates a short version of a link's url, used for display purposes
   *            e.g. {url: http://www.foosite.com}  =>  "foosite"
   *
   * @param  {obj} link A link object
   *         {str} link.url (required)- The url of the link
   * @return {str}   A short url
   */
  shortURL: function NewTabUtils_shortURL({ url }) {
    if (!url) {
      return "";
    }

    // Make sure we have a valid / parseable url
    let parsed;
    try {
      parsed = new URL(url);
    } catch (ex) {
      // Not entirely sure what we have, but just give it back
      return url;
    }

    // Ideally get the short eTLD-less host but fall back to longer url parts
    return (
      this.shortHostname(parsed.hostname) || parsed.pathname || parsed.href
    );
  },

  /**
   * retrieves positive UTC offset, rounded to the nearest integer number greater than 0.
   * (If less than 0, then add 24.)
   * @returns {Number} utc_offset
   */
  getUtcOffset() {
    const offsetInMinutes = new Date().getTimezoneOffset(); // in minutes, positive behind UTC
    const offsetInHours = -offsetInMinutes / 60; // convert to hours, now positive *ahead* of UTC
    let utc_offset = Math.round(offsetInHours);

    if (utc_offset <= 0) {
      utc_offset += 24;
    }

    return utc_offset;
  },

  /**
   *  Returns a normalized OS string used in the newtab-content ping
   * Borrowed from https://github.com/mozilla/gcp-ingestion/ingestion-beam/
   * src/main/java/com/mozilla/telemetry/transforms/NormalizeAttributes.java
   * @returns {String} Normalized OS string mac|win|linux|android|ios|other
   */
  normalizeOs() {
    const osString = Services.appinfo.OS;
    if (osString.startsWith("Windows") || osString.startsWith("WINNT")) {
      return "windows";
    } else if (osString.startsWith("Darwin")) {
      return "mac";
    } else if (
      osString.includes("Linux") ||
      osString.includes("BSD") ||
      osString.includes("SunOS") ||
      osString.includes("Solaris")
    ) {
      return "Linux";
    } else if (osString.startsWith("iOS") || osString.includes("iPhone")) {
      return "ios";
    } else if (osString.startsWith("Android")) {
      return "android";
    }
    return "other";
  },

  links: Links,
  allPages: AllPages,
  pinnedLinks: PinnedLinks,
  blockedLinks: BlockedLinks,
  activityStreamLinks: ActivityStreamLinks,
  activityStreamProvider: ActivityStreamProvider,
};
