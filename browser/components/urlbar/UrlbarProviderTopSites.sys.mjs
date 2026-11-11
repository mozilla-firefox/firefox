/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports a provider returning the user's newtab Top Sites.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import {
  UrlbarProvider,
  UrlbarUtils,
} from "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  TopSites: "moz-src:///browser/components/topsites/TopSites.sys.mjs",
  TOP_SITES_DEFAULT_ROWS:
    "moz-src:///browser/components/topsites/constants.mjs",
  TOP_SITES_MAX_SITES_PER_ROW:
    "moz-src:///browser/components/topsites/constants.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarProviderOpenTabs:
    "moz-src:///browser/components/urlbar/UrlbarProviderOpenTabs.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
  UrlbarSearchUtils:
    "moz-src:///browser/components/urlbar/UrlbarSearchUtils.sys.mjs",
});

// These prefs must be true for the provider to return results. They are assumed
// to be booleans. We check `system.topsites` because if it is disabled we would
// get stale or empty top sites data.
const TOP_SITES_ENABLED_PREFS = [
  "browser.urlbar.suggest.topsites",
  "browser.newtabpage.activity-stream.feeds.system.topsites",
];

// Helper function to compare 2 URLs without refs.
function sameUrlIgnoringRef(url1, url2) {
  if (!url1 || !url2) {
    return false;
  }

  let cleanUrl1 = url1.replace(/#.*$/, "");
  let cleanUrl2 = url2.replace(/#.*$/, "");

  return cleanUrl1 == cleanUrl2;
}

/**
 * A provider that returns the Top Sites shown on about:newtab.
 */
export class UrlbarProviderTopSites extends UrlbarProvider {
  constructor() {
    super();
  }

  static get PRIORITY() {
    // Top sites are prioritized over the UrlbarProviderPlaces provider.
    return 1;
  }

  /**
   * @returns {Values<typeof lazy.UrlbarShared.PROVIDER_TYPE>}
   */
  get type() {
    return lazy.UrlbarShared.PROVIDER_TYPE.PROFILE;
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
      !queryContext.restrictSource &&
      !queryContext.searchString &&
      !queryContext.restrictInSearchMode()
    );
  }

  /**
   * Gets the provider's priority.
   *
   * @returns {number} The provider's priority for the given query.
   */
  getPriority() {
    return UrlbarProviderTopSites.PRIORITY;
  }

  /**
   * Starts querying.
   *
   * @param {UrlbarQueryContext} queryContext
   * @param {(provider: UrlbarProvider, result: UrlbarResult) => void} addCallback
   *   Callback invoked by the provider to add a new result.
   */
  async startQuery(queryContext, addCallback) {
    // Bail if Top Sites are not enabled. We check this condition here instead
    // of in isActive because we still want this provider to be restricting even
    // if this is not true. If it wasn't restricting, we would show the results
    // from UrlbarProviderPlaces's empty search behaviour. We aren't interested
    // in those since they are very similar to Top Sites and thus might be
    // confusing, especially since users can configure Top Sites but cannot
    // configure the default empty search results. See bug 1623666.
    let enabled = TOP_SITES_ENABLED_PREFS.every(p =>
      Services.prefs.getBoolPref(p, false)
    );
    if (!enabled) {
      return;
    }

    let sites;
    if (Services.prefs.getBoolPref("browser.topsites.component.enabled")) {
      sites = await lazy.TopSites.getSites();
    } else {
      sites = lazy.AboutNewTab.getTopSites();
    }

    let instance = this.queryInstance;

    // Filter out empty values. Site is empty when there's a gap between tiles
    // on about:newtab.
    sites = sites.filter(site => site);

    if (!lazy.UrlbarPrefs.get("sponsoredTopSites")) {
      sites = sites.filter(site => !site.sponsored_position);
    }

    // This is done here, rather than in the global scope, because
    // TOP_SITES_DEFAULT_ROWS causes import of topsites constants.mjs, and we want to
    // do that only when actually querying for Top Sites.
    if (UrlbarProviderTopSites.topSitesRows === undefined) {
      XPCOMUtils.defineLazyPreferenceGetter(
        UrlbarProviderTopSites,
        "topSitesRows",
        "browser.newtabpage.activity-stream.topSitesRows",
        lazy.TOP_SITES_DEFAULT_ROWS
      );
    }

    // We usually respect maxRichResults, though we never show a number of Top
    // Sites greater than what is visible in the New Tab Page, because the
    // additional ones couldn't be managed from the page.
    let numTopSites = Math.min(
      queryContext.maxResults,
      lazy.TOP_SITES_MAX_SITES_PER_ROW * UrlbarProviderTopSites.topSitesRows
    );
    sites = sites.slice(0, numTopSites);

    sites = sites.map(link => {
      let site = {
        type: link.searchTopSite ? "search" : "url",
        subtype: link.type,
        url: link.url_urlbar || link.url,
        isPinned: !!link.isPinned,
        isSponsored: !!link.sponsored_position,
        // The newtab page allows the user to set custom site titles, which
        // are stored in `label`, so prefer it.  Search top sites currently
        // don't have titles but `hostname` instead.
        title: link.label || link.title || link.hostname || "",
        favicon: link.smallFavicon || link.favicon || undefined,
        lastVisitDate: link.lastVisitDate,
      };
      if (site.isSponsored) {
        let {
          sponsored_tile_id,
          sponsored_impression_url,
          sponsored_click_url,
        } = link;
        site = {
          ...site,
          sponsoredTileId: sponsored_tile_id,
          sponsoredImpressionUrl: sponsored_impression_url,
          sponsoredClickUrl: sponsored_click_url,
        };
      }
      return site;
    });

    let tabUrlsToContextIds = new Map();
    if (lazy.UrlbarPrefs.get("suggest.openpage")) {
      lazy.UrlbarProviderOpenTabs.getOpenTabUrls(
        queryContext.isPrivate
      ).forEach((userContextAndGroupIds, url) => {
        let userContextIds = new Set();
        for (let [userContextId] of userContextAndGroupIds) {
          userContextIds.add(userContextId);
        }
        tabUrlsToContextIds.set(url, userContextIds);
      });
    }

    for (let site of sites) {
      switch (site.type) {
        case "url": {
          let payload = {
            title: site.title,
            url: site.url,
            icon: site.favicon,
            isPinned: site.isPinned,
            isSponsored: site.isSponsored,
          };

          // Fuzzy match both the URL as-is, and the URL without ref, then
          // generate a merged Set.
          if (tabUrlsToContextIds) {
            let tabUserContextIds = new Set([
              ...(tabUrlsToContextIds.get(site.url) ?? []),
              ...(tabUrlsToContextIds.get(site.url.replace(/#.*$/, "")) ?? []),
            ]);
            if (tabUserContextIds.size) {
              let switchToTabResultAdded = false;
              for (let userContextId of tabUserContextIds) {
                if (
                  sameUrlIgnoringRef(queryContext.currentPage, site.url) &&
                  queryContext.userContextId == userContextId
                ) {
                  // Don't suggest switching to the current tab.
                  continue;
                }
                payload.userContext =
                  UrlbarUtils.getUserContextData(userContextId);
                let result = new lazy.UrlbarResult({
                  type: lazy.UrlbarShared.RESULT_TYPE.TAB_SWITCH,
                  source: lazy.UrlbarShared.RESULT_SOURCE.TABS,
                  payload,
                });
                addCallback(this, result);
                switchToTabResultAdded = true;
              }
              // Avoid adding url result if Switch to Tab result was added.
              if (switchToTabResultAdded) {
                break;
              }
            }
          }

          if (site.isSponsored) {
            payload.sponsoredTileId = site.sponsoredTileId;
            payload.sponsoredClickUrl = site.sponsoredClickUrl;
          }

          // The "last visited" result explanation needs a fresh visit date.
          // Top Sites data is cached and isn't refreshed on every visit, and a
          // manually added shortcut's URL can differ from the URL recorded in
          // Places (a trailing slash, an http->https upgrade, or another
          // redirect).  Look the date up live, following the Places redirect
          // chain, so the explanation is accurate.
          if (
            lazy.UrlbarPrefs.get("resultExplanationsFeatureGate") &&
            lazy.UrlbarPrefs.get("suggest.history")
          ) {
            let lastVisit = await this.#fetchLastVisit(payload.url);
            if (instance != this.queryInstance) {
              break;
            }
            payload.lastVisit = lastVisit ?? site.lastVisitDate;
          }

          // Figure out what the source of this result should be. When bookmark
          // or history results are disabled, we use `RESULT_SOURCE.OTHER_LOCAL`
          // so that the muxer and providers manager add the result anyway. That
          // seems wrong but appears to be how this provider has always worked.
          // See also bug 1631281.
          /** @type {Values<typeof lazy.UrlbarShared.RESULT_SOURCE>} */
          let resultSource = lazy.UrlbarShared.RESULT_SOURCE.OTHER_LOCAL;
          if (
            lazy.UrlbarPrefs.get("suggest.history") &&
            site.subtype == "history"
          ) {
            resultSource = lazy.UrlbarShared.RESULT_SOURCE.HISTORY;
          }
          if (lazy.UrlbarPrefs.get("suggest.bookmark")) {
            let bookmark = await lazy.PlacesUtils.bookmarks.fetch({
              url: new URL(payload.url),
            });
            // Check if query has been cancelled.
            if (instance != this.queryInstance) {
              break;
            }
            if (bookmark) {
              resultSource = lazy.UrlbarShared.RESULT_SOURCE.BOOKMARKS;
              payload.bookmarkDateMs = bookmark.dateAdded.getTime();
            }
          }

          if (
            resultSource == lazy.UrlbarShared.RESULT_SOURCE.HISTORY ||
            resultSource == lazy.UrlbarShared.RESULT_SOURCE.BOOKMARKS
          ) {
            payload.isBlockable = true;
          }

          let result = new lazy.UrlbarResult({
            type: lazy.UrlbarShared.RESULT_TYPE.URL,
            source: resultSource,
            payload,
          });
          addCallback(this, result);
          break;
        }
        case "search": {
          let engine = await lazy.UrlbarSearchUtils.engineForAlias(site.title);

          if (!engine && site.url) {
            // Look up the engine by its domain.
            let host = URL.parse(site.url)?.hostname;
            if (host) {
              engine = (
                await lazy.UrlbarSearchUtils.enginesForDomainPrefix(host)
              )[0];
            }
          }

          if (!engine) {
            // No engine found. We skip this Top Site.
            break;
          }

          if (instance != this.queryInstance) {
            break;
          }

          let result = new lazy.UrlbarResult({
            type: lazy.UrlbarShared.RESULT_TYPE.SEARCH,
            source: lazy.UrlbarShared.RESULT_SOURCE.SEARCH,
            payload: {
              keyword: site.title,
              providesSearchMode: true,
              engine: engine.name,
              query: "",
              icon: site.favicon,
              isPinned: site.isPinned,
            },
          });
          addCallback(this, result);
          break;
        }
        default:
          this.logger.error(`Unknown Top Site type: ${site.type}`);
          break;
      }
    }
  }

  /**
   * @param {string} state
   * @param {UrlbarQueryContext} queryContext
   * @param {UrlbarParentController} controller
   * @param {{index: number, result: UrlbarResult}[]} providerVisibleResults
   */
  onImpression(state, queryContext, controller, providerVisibleResults) {
    if (queryContext.isPrivate) {
      return;
    }

    providerVisibleResults.forEach(({ index, result }) => {
      if (result?.payload.isSponsored) {
        Glean.contextualServicesTopsites.impression[`urlbar_${index}`].add(1);
      }
    });
  }

  /**
   * @param {UrlbarQueryContext} queryContext
   * @param {UrlbarParentController} controller
   * @param {object} details
   * @param {UrlbarResult} details.result
   * @param {string} details.selType
   */
  async onEngagement(queryContext, controller, { result, selType }) {
    if (!result.payload.isBlockable) {
      return;
    }

    switch (selType) {
      case "dismiss": {
        lazy.NewTabUtils.activityStreamLinks.blockURL({
          url: result.payload.url,
        });
        controller.removeResult(result);
        break;
      }
      case "remove_history": {
        await lazy.PlacesUtils.history.remove(result.payload.url);
        controller.removeResult(result);
        break;
      }
    }
  }

  /**
   * @param {UrlbarResult} result
   * @returns {?UrlbarResultCommand[]}
   */
  getResultCommands(result) {
    if (!result.payload.isBlockable) {
      return null;
    }

    return [
      {
        name: "dismiss",
        l10n: { id: "urlbar-result-menu-remove-top-site" },
      },
      {
        name: "remove_history",
        l10n: { id: "urlbar-result-menu-remove-from-history2" },
      },
    ];
  }

  async #fetchLastVisit(url) {
    let canonicalURL;
    try {
      canonicalURL = new URL(url).href;
    } catch (e) {
      return null;
    }
    let db = await lazy.PlacesUtils.promiseDBConnection();
    let rows = await db.execute(
      `WITH RECURSIVE redirect_chain(visit_id, visit_date) AS (
         SELECT v.id, v.visit_date
         FROM moz_places h
         JOIN moz_historyvisits v ON v.place_id = h.id
         WHERE h.url_hash = hash(:url) AND h.url = :url
         UNION
         SELECT v.id, v.visit_date
         FROM moz_historyvisits v
         JOIN redirect_chain c ON v.from_visit = c.visit_id
         WHERE v.visit_type IN (:redirectPermanent, :redirectTemporary)
       )
       SELECT MAX(visit_date) / 1000 AS lastVisit FROM redirect_chain`,
      {
        url: canonicalURL,
        redirectPermanent:
          lazy.PlacesUtils.history.TRANSITIONS.REDIRECT_PERMANENT,
        redirectTemporary:
          lazy.PlacesUtils.history.TRANSITIONS.REDIRECT_TEMPORARY,
      }
    );
    return rows[0]?.getResultByName("lastVisit");
  }

  /**
   * Once initialized, contains an array of weak
   * references of top sites listener functions.
   *
   * @type {?{get: Function}[]}
   */
  static #topSitesListeners = null;

  /**
   * Adds a listener function that will be called when the top sites change or
   * they are enabled/disabled. This class will hold a weak reference to the
   * listener, so you do not need to unregister it, but you or someone else must
   * keep a strong reference to it to keep it from being immediately garbage
   * collected.
   *
   * @param {Function} callback
   *   The listener function. This class will hold a weak reference to it.
   */
  static addTopSitesListener(callback) {
    // Lazily init observers.
    if (!UrlbarProviderTopSites.#topSitesListeners) {
      UrlbarProviderTopSites.#topSitesListeners = [];
      let callListeners = UrlbarProviderTopSites.#callTopSitesListeners;
      if (Services.prefs.getBoolPref("browser.topsites.component.enabled")) {
        Services.obs.addObserver(callListeners, "topsites-refreshed");
      } else {
        Services.obs.addObserver(callListeners, "newtab-top-sites-changed");
      }
      for (let pref of TOP_SITES_ENABLED_PREFS) {
        Services.prefs.addObserver(pref, callListeners);
      }
    }
    UrlbarProviderTopSites.#topSitesListeners.push(
      Cu.getWeakReference(callback)
    );
  }

  static #callTopSitesListeners() {
    for (let i = 0; i < UrlbarProviderTopSites.#topSitesListeners.length; ) {
      let listener = UrlbarProviderTopSites.#topSitesListeners[i].get();
      if (!listener) {
        // The listener has been GC'ed, so remove it from our list.
        UrlbarProviderTopSites.#topSitesListeners.splice(i, 1);
      } else {
        listener();
        ++i;
      }
    }
  }

  /**
   * The number of top site rows to display by default.
   *
   * @type {number|undefined}
   */
  static topSitesRows;
}
