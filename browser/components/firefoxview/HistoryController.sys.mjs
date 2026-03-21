/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

import { getLogger } from "chrome://browser/content/firefoxview/helpers.mjs";

ChromeUtils.defineESModuleGetters(lazy, {
  PlacesQuery: "resource://gre/modules/PlacesQuery.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
});



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

export class HistoryController {
  /**
   * @type {{ entries: CardEntry[]; searchQuery: string; sortOption: string; }}
   */
  historyCache;
  host;
  searchQuery;
  sortOption;
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

    host.addController(this);
  }

  hostConnected() {
    this.placesQuery.observeHistory(historyMap => this.updateCache(historyMap));
  }

  hostDisconnected() {
    this.placesQuery.close();
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
   * Update cached history.
   *
   * @param {CachedHistory} [historyMap]
   *   If provided, performs an update using the given data (instead of fetching
   *   it from the db).
   */
  async updateCache(historyMap) {
    const { searchQuery, sortOption } = this;
    const entries = searchQuery
      ? await this.#getVisitsForSearchQuery(searchQuery)
      : await this.#getVisitsForSortOption(sortOption, historyMap);
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
        switch (sortOption) {
          case "datesite": {
            // item is a [ domain, visit[] ] entry.
            const [, visits] = item;
            for (const visit of visits) {
              this.#normalizeVisit(visit);
            }
            break;
          }
          default:
            // item is a single visit.
            this.#normalizeVisit(item);
        }
      }
    }
    this.historyCache = { entries, searchQuery, sortOption };
    this.host.requestUpdate();
  }

  /**
   * Normalize data for fxview-tabs-list.
   *
   * @param {HistoryVisit} visit
   *   The visit to format.
   */
  #normalizeVisit(visit) {
    visit.time = visit.date.getTime();
    visit.title = visit.title || visit.url;
    visit.icon = `page-icon:${visit.url}`;
    visit.primaryL10nId = "fxviewtabrow-tabs-list-tab";
    visit.primaryL10nArgs = JSON.stringify({
      targetURI: visit.url,
    });
    visit.secondaryL10nId = "fxviewtabrow-options-menu-button";
    visit.secondaryL10nArgs = JSON.stringify({
      tabTitle: visit.title || visit.url,
    });
  }

  async #getVisitsForSearchQuery(searchQuery) {
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
        return this.#getVisitsForLastVisited(historyMap);
      default:
        return [];
    }
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
