/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { getLogger } from "chrome://browser/content/firefoxview/helpers.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

/**
 * @import { HistoryDateContainer } from "moz-src:///browser/components/places/history/HistoryDateContainer.sys.mjs"
 * @import { HistoryDateRegistry } from "moz-src:///browser/components/places/history/HistoryDateRegistry.sys.mjs"
 */

const lazy = XPCOMUtils.declareLazy({
  HistoryDateRegistry:
    "moz-src:///browser/components/places/history/HistoryDateRegistry.sys.mjs",
  PlacesQuery: "resource://gre/modules/PlacesQuery.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  ProfileAge: "resource://gre/modules/ProfileAge.sys.mjs",
});

const HAS_IMPORTED_HISTORY_PREF = "browser.migrate.interactions.history";
const IMPORT_HISTORY_DISMISSED_PREF =
  "browser.tabs.firefox-view.importHistory.dismissed";
const IMPORT_BANNER_PROFILE_AGE_CUTOFF_DAYS = 8;
const MS_PER_DAY = 24 * 60 * 60 * 1000;

const HISTORY_MAP_L10N_IDS = {
  sidebar: {
    "history-date-today": "sidebar-history-date-today",
    "history-date-yesterday": "sidebar-history-date-yesterday",
    "history-date-this-month": "sidebar-history-date-this-month",
    "history-date-prev-month": "sidebar-history-date-prev-month",
  },
  firefoxview: {
    "history-date-today": "firefoxview-history-date-today",
    "history-date-yesterday": "firefoxview-history-date-yesterday",
    "history-date-this-month": "firefoxview-history-date-this-month",
    "history-date-prev-month": "firefoxview-history-date-prev-month",
  },
};

/**
 * When sorting by date or site, each card "item" is a single visit.
 *
 * When sorting by date *and* site, each card "item" is a mapping of site
 * domains to their respective list of visits.
 *
 * @typedef {HistoryVisit | [string, HistoryVisit[]]} CardItem
 */

/**
 * A list of visits displayed on a card.
 *
 * @typedef {object} CardEntry
 *
 * @property {string} domain
 * @property {CardItem[]} items
 * @property {string} l10nId
 */

/**
 * A "date" card, which holds a list of visits that occurred during a specific timeframe.
 *
 * @typedef {CardEntry & {
 *   startDate: Date,
 *   container: HistoryDateContainer,
 * }} DateCardEntry
 */

export class HistoryController {
  /**
   * @type {{ entries: CardEntry[]; searchQuery: string; sortOption: string; }}
   */
  historyCache;
  /** @type {HistoryDateRegistry?} */
  #registry = null;
  /** @type {mozIStoragePendingStatement?} */
  #pendingSearch = null;
  host;
  searchQuery;
  sortOption;
  #profileAge = Infinity;
  #todaysDate;
  #yesterdaysDate;

  /**
   * Construct a new HistoryController.
   *
   * @param {ReactiveControllerHost} host
   *   The component that this controller is connected to.
   * @param {object} [options]
   * @param {string} [options.sortOption]
   * @param {number} [options.searchResultsLimit]
   * @param {string} [options.component]
   */
  constructor(
    host,
    {
      sortOption = "date",
      searchResultsLimit = 300,
      component = "firefoxview",
    } = {}
  ) {
    this.placesQuery = new lazy.PlacesQuery();
    this.searchQuery = "";
    this.sortOption = sortOption;
    this.searchResultsLimit = searchResultsLimit;
    this.component = HISTORY_MAP_L10N_IDS[component]
      ? component
      : "firefoxview";
    this.historyCache = {
      entries: null,
      searchQuery: null,
      sortOption: null,
    };
    this.host = host;

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "importHistoryDismissed",
      IMPORT_HISTORY_DISMISSED_PREF,
      false,
      () => host.requestUpdate()
    );
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "hasImportedHistory",
      HAS_IMPORTED_HISTORY_PREF,
      false,
      () => host.requestUpdate()
    );

    host.addController(this);
  }

  hostConnected() {
    this.placesQuery.observeHistory(historyMap => {
      if (!this.#useNewHistoryAPI) {
        this.updateCache(historyMap);
      }
    });
    if (!this.importHistoryDismissed && !this.hasImportedHistory) {
      this.#updateProfileAge();
    }
  }

  hostDisconnected() {
    this.placesQuery.close();
    this.#registry?.unloadAll();
    if (this.#pendingSearch) {
      this.#pendingSearch.cancel();
      this.#pendingSearch = null;
    }
  }

  async #updateProfileAge() {
    const profileAccessor = await lazy.ProfileAge();
    const profileCreateTime = await profileAccessor.created;
    this.profileAge = (Date.now() - profileCreateTime) / MS_PER_DAY;
  }

  /**
   * The age of the current profile in days.
   *
   * @type {number}
   */
  get profileAge() {
    return this.#profileAge;
  }

  set profileAge(days) {
    this.#profileAge = days;
    this.host.requestUpdate();
  }

  /**
   * Whether to prompt the user to import history from another browser.
   *
   * @type {boolean}
   */
  get shouldShowImportBanner() {
    return (
      this.profileAge < IMPORT_BANNER_PROFILE_AGE_CUTOFF_DAYS &&
      !this.hasImportedHistory &&
      !this.importHistoryDismissed &&
      Services.policies.isAllowed("profileImport")
    );
  }

  dismissImportHistory() {
    Services.prefs.setBoolPref(IMPORT_HISTORY_DISMISSED_PREF, true);
  }

  deleteFromHistory() {
    return lazy.PlacesUtils.history.remove(this.host.triggerNode.url);
  }

  onSearchQuery(e) {
    this.searchQuery = e.detail.query;
    this.updateCache();
  }

  onChangeSortOption(e, value = e.target.value) {
    this.sortOption = value;
    this.updateCache();
  }

  get historyVisits() {
    return this.historyCache.entries || [];
  }

  get isHistoryPending() {
    return this.historyCache.entries === null;
  }

  get searchResults() {
    if (this.historyCache.searchQuery && this.historyCache.entries?.length) {
      return this.historyCache.entries[0].items;
    }
    return null;
  }

  get totalVisitsCount() {
    return this.historyVisits.reduce(
      (count, entry) => count + entry.items.length,
      0
    );
  }

  get isHistoryEmpty() {
    return !this.historyVisits.length;
  }

  /**
   * Determine whether the new history API is enabled and supports the current
   * component (i.e. Firefox View or Sidebar) and sort option.
   *
   * @returns {boolean}
   */
  get #useNewHistoryAPI() {
    return (
      this.component === "sidebar" &&
      this.sortOption === "date" &&
      Services.prefs.getBoolPref("sidebar.history.showAllHistory", false)
    );
  }

  /**
   * Update cached history.
   *
   * @param {CachedHistory} [historyMap]
   *   If provided, performs an update using the given data (instead of fetching
   *   it from the db).
   */
  async updateCache(historyMap) {
    const { searchQuery, sortOption } = this;
    let entries;
    let registry = null;
    if (this.#pendingSearch) {
      this.#pendingSearch.cancel();
      this.#pendingSearch = null;
    }
    if (!searchQuery && this.#useNewHistoryAPI) {
      ({ entries, registry } = await this.#getVisitsFromDateRegistry());
    } else {
      entries = searchQuery
        ? await this.#getVisitsForSearchQuery(searchQuery)
        : await this.#getVisitsForSortOption(sortOption, historyMap);
    }
    if (
      this.searchQuery !== searchQuery ||
      this.sortOption !== sortOption ||
      !entries
    ) {
      // This query is stale, discard results and do not update the cache / UI.
      return;
    }
    for (const { items } of entries) {
      for (const item of items) {
        if (sortOption === "datesite" && !searchQuery) {
          // item is a [ domain, visit[] ] entry.
          const [, visits] = item;
          for (const visit of visits) {
            this.#normalizeVisit(visit);
          }
        } else {
          // item is a single visit.
          this.#normalizeVisit(item);
        }
      }
    }
    this.#registry?.unloadAll();
    this.#registry = registry;
    this.historyCache = { entries, searchQuery, sortOption };
    this.host.requestUpdate();
  }

  /**
   * If this container is expanded, load its visits into memory and render
   * them in the UI. Otherwise, unload its visits.
   *
   * @param {DateCardEntry} dateCardEntry
   * @param {boolean} expanded
   * @returns {Promise<void>} - Resolves when async loading and UI updates are complete.
   */
  async toggleContainerExpanded(dateCardEntry, expanded) {
    if (expanded) {
      await dateCardEntry.container.load();
      if (!this.historyVisits.includes(dateCardEntry)) {
        // If history was refreshed during the load, and we're no longer showing
        // this card, then we should immediately unload it from memory.
        dateCardEntry.container.unload();
        return;
      }
      for (const visit of dateCardEntry.items) {
        this.#normalizeVisit(visit);
      }
      await this.host.requestUpdate();
    } else {
      dateCardEntry.container.unload();
      await this.host.requestUpdate();
    }
  }

  /**
   * Normalize data for fxview-tabs-list.
   *
   * @param {HistoryVisit} visit
   *   The visit to format.
   */
  #normalizeVisit(visit) {
    visit.time = visit.date.getTime();
    // Visits are cached and can be normalized more than once, so derive `guid`
    // from the page guid captured on the first pass rather than from the
    // (already time-suffixed) `guid`, which would otherwise keep growing.
    visit.pageGuid ??= visit.guid;
    // A row identifies a page, and for the date sorts a page per day, since
    // that is how the query groups visits. Keying on the visit time instead
    // would change a row's identity every time the page is visited again.
    visit.guid =
      this.sortOption === "date" || this.sortOption === "datesite"
        ? `${visit.pageGuid}|${this.placesQuery.getStartOfDayTimestamp(
            visit.date
          )}`
        : visit.pageGuid;
    visit.title = visit.title || visit.url;
    visit.icon = `page-icon:${visit.url}`;
    visit.secondaryL10nId = "fxviewtabrow-options-menu-button";
    visit.secondaryL10nArgs = JSON.stringify({
      tabTitle: visit.title || visit.url,
    });
  }

  async #getVisitsForSearchQuery(searchQuery) {
    if (this.#useNewHistoryAPI) {
      const items = await this.#searchHistory(searchQuery);
      return items ? [{ items }] : null;
    }
    let items = [];
    try {
      items = await this.placesQuery.searchHistory(
        searchQuery,
        this.searchResultsLimit
      );
    } catch (e) {
      getLogger("HistoryController").warn(
        "There is a new search query in progress, so cancelling this one.",
        e
      );
    }
    return [{ items }];
  }

  /**
   * Search history without requiring a `PlacesQuery` instance.
   *
   * @param {string} searchTerms
   * @returns {Promise<HistoryVisit[] | null>}
   *   Visits, or `null` if cancelled.
   */
  #searchHistory(searchTerms) {
    const query = Object.assign(lazy.PlacesUtils.history.getNewQuery(), {
      searchTerms,
    });
    const options = Object.assign(
      lazy.PlacesUtils.history.getNewQueryOptions(),
      {
        queryType: Ci.nsINavHistoryQueryOptions.QUERY_TYPE_HISTORY,
        resultType: Ci.nsINavHistoryQueryOptions.RESULTS_AS_URI,
        sortingMode: Ci.nsINavHistoryQueryOptions.SORT_BY_DATE_DESCENDING,
        maxResults: this.searchResultsLimit,
      }
    );
    return new Promise((resolve, reject) => {
      const items = [];
      const statement = lazy.PlacesUtils.history.asyncExecuteLegacyQuery(
        query,
        options,
        {
          handleResult(resultSet) {
            let row;
            while ((row = resultSet.getNextRow())) {
              items.push({
                date: lazy.PlacesUtils.toDate(
                  row.getResultByName("last_visit_date")
                ),
                title: row.getResultByName("page_title"),
                url: row.getResultByName("url"),
                guid: row.getResultByName("guid"),
              });
            }
          },
          handleError(error) {
            reject(new Error(error.message));
          },
          handleCompletion: reason => {
            if (this.#pendingSearch === statement) {
              this.#pendingSearch = null;
            }
            if (reason === Ci.mozIStorageStatementCallback.REASON_FINISHED) {
              resolve(items);
            } else if (
              reason === Ci.mozIStorageStatementCallback.REASON_CANCELED
            ) {
              resolve(null);
            } else {
              reject(
                new Error(`History search did not complete (reason: ${reason})`)
              );
            }
          },
        }
      );
      this.#pendingSearch = statement;
    });
  }

  async #getVisitsForSortOption(sortOption, historyMap) {
    if (!historyMap) {
      const fetchedHistory = await this.#fetchHistory();
      if (!fetchedHistory) {
        return null;
      }
      historyMap = fetchedHistory;
    }
    switch (sortOption) {
      case "date":
        this.#setTodaysDate();
        return this.#getVisitsForDate(historyMap);
      case "site":
        return this.#getVisitsForSite(historyMap);
      case "datesite":
        this.#setTodaysDate();
        return this.#getVisitsForDateSite(historyMap);
      case "lastvisited":
      case "mostvisited":
        return this.#getVisitsForLastVisited(historyMap);
      default:
        return [];
    }
  }

  /**
   * Use the `HistoryDateRegistry` API to fetch visits sorted by date.
   *
   * @returns {Promise<{ entries: DateCardEntry[], registry: HistoryDateRegistry }>}
   */
  async #getVisitsFromDateRegistry() {
    const now = new Date();
    const yesterday = new Date(
      now.getFullYear(),
      now.getMonth(),
      now.getDate() - 1
    );
    const registry = await lazy.HistoryDateRegistry.create({ now });
    const entries = registry.containers.map(container => {
      const { startDate } = container;
      let label = "history-date-prev-month";
      if (this.#isSameDate(startDate, now)) {
        label = "history-date-today";
      } else if (this.#isSameDate(startDate, yesterday)) {
        label = "history-date-yesterday";
      } else if (this.#isSameMonth(startDate, now)) {
        label = "history-date-this-month";
      }
      return {
        l10nId: HISTORY_MAP_L10N_IDS[this.component][label],
        startDate,
        container,
        get items() {
          return container.children;
        },
      };
    });
    return { entries, registry };
  }

  #setTodaysDate() {
    const now = new Date();
    this.#todaysDate = new Date(
      now.getFullYear(),
      now.getMonth(),
      now.getDate()
    );
    this.#yesterdaysDate = new Date(
      now.getFullYear(),
      now.getMonth(),
      now.getDate() - 1
    );
  }

  /**
   * Get a list of visits, sorted by date, in reverse chronological order.
   *
   * @param {Map<number, HistoryVisit[]>} historyMap
   * @returns {CardEntry[]}
   */
  #getVisitsForDate(historyMap) {
    const entries = [];
    const visitsFromToday = this.#getVisitsFromToday(historyMap);
    const visitsFromYesterday = this.#getVisitsFromYesterday(historyMap);
    const visitsByDay = this.#getVisitsByDay(historyMap);
    const visitsByMonth = this.#getVisitsByMonth(historyMap);

    // Add visits from today and yesterday.
    if (visitsFromToday.length) {
      entries.push({
        l10nId: HISTORY_MAP_L10N_IDS[this.component]["history-date-today"],
        items: visitsFromToday,
      });
    }
    if (visitsFromYesterday.length) {
      entries.push({
        l10nId: HISTORY_MAP_L10N_IDS[this.component]["history-date-yesterday"],
        items: visitsFromYesterday,
      });
    }

    // Add visits from this month, grouped by day.
    visitsByDay.forEach(visits => {
      entries.push({
        l10nId: HISTORY_MAP_L10N_IDS[this.component]["history-date-this-month"],
        items: visits,
      });
    });

    // Add visits from previous months, grouped by month.
    visitsByMonth.forEach(visits => {
      entries.push({
        l10nId: HISTORY_MAP_L10N_IDS[this.component]["history-date-prev-month"],
        items: visits,
      });
    });
    return entries;
  }

  #getVisitsFromToday(cachedHistory) {
    const mapKey = this.placesQuery.getStartOfDayTimestamp(this.#todaysDate);
    const visits = cachedHistory.get(mapKey) ?? [];
    return [...visits];
  }

  #getVisitsFromYesterday(cachedHistory) {
    const mapKey = this.placesQuery.getStartOfDayTimestamp(
      this.#yesterdaysDate
    );
    const visits = cachedHistory.get(mapKey) ?? [];
    return [...visits];
  }

  /**
   * Get a list of visits per day for each day on this month, excluding today
   * and yesterday.
   *
   * @param {CachedHistory} cachedHistory
   *   The history cache to process.
   * @returns {CardItem[]}
   *   A list of visits for each day.
   */
  #getVisitsByDay(cachedHistory) {
    const visitsPerDay = [];
    for (const [time, visits] of cachedHistory.entries()) {
      const date = new Date(time);
      if (
        this.#isSameDate(date, this.#todaysDate) ||
        this.#isSameDate(date, this.#yesterdaysDate)
      ) {
        continue;
      } else if (!this.#isSameMonth(date, this.#todaysDate)) {
        break;
      } else {
        visitsPerDay.push(visits);
      }
    }
    return visitsPerDay;
  }

  /**
   * Get a list of visits per month for each month, excluding this one, and
   * excluding yesterday's visits if yesterday happens to fall on the previous
   * month.
   *
   * @param {CachedHistory} cachedHistory
   *   The history cache to process.
   * @returns {CardItem[]}
   *   A list of visits for each month.
   */
  #getVisitsByMonth(cachedHistory) {
    const visitsPerMonth = [];
    let previousMonth = null;
    for (const [time, visits] of cachedHistory.entries()) {
      const date = new Date(time);
      if (
        this.#isSameMonth(date, this.#todaysDate) ||
        this.#isSameDate(date, this.#yesterdaysDate)
      ) {
        continue;
      }
      const month = this.placesQuery.getStartOfMonthTimestamp(date);
      if (month !== previousMonth) {
        visitsPerMonth.push(visits);
      } else if (this.sortOption === "datesite") {
        // CardItem type is currently Map<string, HistoryVisit[]>.
        visitsPerMonth[visitsPerMonth.length - 1] = this.#mergeMaps(
          visitsPerMonth.at(-1),
          visits
        );
      } else {
        visitsPerMonth[visitsPerMonth.length - 1] = visitsPerMonth
          .at(-1)
          .concat(visits);
      }
      previousMonth = month;
    }
    return visitsPerMonth;
  }

  /**
   * Given two date instances, check if their dates are equivalent.
   *
   * @param {Date} dateToCheck
   * @param {Date} date
   * @returns {boolean}
   *   Whether both date instances have equivalent dates.
   */
  #isSameDate(dateToCheck, date) {
    return (
      dateToCheck.getDate() === date.getDate() &&
      this.#isSameMonth(dateToCheck, date)
    );
  }

  /**
   * Given two date instances, check if their months are equivalent.
   *
   * @param {Date} dateToCheck
   * @param {Date} month
   * @returns {boolean}
   *   Whether both date instances have equivalent months.
   */
  #isSameMonth(dateToCheck, month) {
    return (
      dateToCheck.getMonth() === month.getMonth() &&
      dateToCheck.getFullYear() === month.getFullYear()
    );
  }

  /**
   * Merge two maps of (domain: string) => HistoryVisit[] into a single map.
   *
   * @param {Map<string, HistoryVisit[]>} oldMap
   * @param {Map<string, HistoryVisit[]>} newMap
   * @returns {Map<string, HistoryVisit[]>}
   */
  #mergeMaps(oldMap, newMap) {
    const map = new Map(oldMap);
    for (const [domain, newVisits] of newMap) {
      const oldVisits = map.get(domain);
      map.set(domain, oldVisits?.concat(newVisits) ?? newVisits);
    }
    return map;
  }

  /**
   * Get a list of visits, sorted by site, in alphabetical order.
   *
   * @param {Map<string, HistoryVisit[]>} historyMap
   * @returns {CardEntry[]}
   */
  #getVisitsForSite(historyMap) {
    return Array.from(historyMap.entries(), ([domain, items]) => ({
      domain,
      items,
      l10nId: domain ? null : "firefoxview-history-site-localhost",
    })).sort((a, b) => a.domain.localeCompare(b.domain));
  }

  /**
   * Get a list of visits, sorted by date and site, in reverse chronological
   * order.
   *
   * @param {Map<number, Map<string, HistoryVisit[]>>} historyMap
   * @returns {CardEntry[]}
   */
  #getVisitsForDateSite(historyMap) {
    const entries = [];
    const visitsFromToday = this.#getVisitsFromToday(historyMap);
    const visitsFromYesterday = this.#getVisitsFromYesterday(historyMap);
    const visitsByDay = this.#getVisitsByDay(historyMap);
    const visitsByMonth = this.#getVisitsByMonth(historyMap);

    /**
     * Sorts items alphabetically by domain name.
     *
     * @param {[string, HistoryVisit[]][]} items
     * @returns {[string, HistoryVisit[]][]} The items in sorted order.
     */
    function sortItems(items) {
      return items.sort(([aDomain], [bDomain]) =>
        aDomain.localeCompare(bDomain)
      );
    }

    // Add visits from today and yesterday.
    if (visitsFromToday.length) {
      entries.push({
        l10nId: HISTORY_MAP_L10N_IDS[this.component]["history-date-today"],
        items: sortItems(visitsFromToday),
      });
    }
    if (visitsFromYesterday.length) {
      entries.push({
        l10nId: HISTORY_MAP_L10N_IDS[this.component]["history-date-yesterday"],
        items: sortItems(visitsFromYesterday),
      });
    }

    // Add visits from this month, grouped by day.
    visitsByDay.forEach(visits => {
      entries.push({
        l10nId: HISTORY_MAP_L10N_IDS[this.component]["history-date-this-month"],
        items: sortItems([...visits]),
      });
    });

    // Add visits from previous months, grouped by month.
    visitsByMonth.forEach(visits => {
      entries.push({
        l10nId: HISTORY_MAP_L10N_IDS[this.component]["history-date-prev-month"],
        items: sortItems([...visits]),
      });
    });

    return entries;
  }

  /**
   * Get a list of visits sorted by recency.
   *
   * @param {HistoryVisit[]} items
   * @returns {CardEntry[]}
   */
  #getVisitsForLastVisited(items) {
    return [{ items }];
  }

  async #fetchHistory() {
    return this.placesQuery.getHistory({
      daysOld: 60,
      limit: -1,
      sortBy: this.sortOption,
    });
  }
}
