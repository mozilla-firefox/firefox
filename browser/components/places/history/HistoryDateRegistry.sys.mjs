/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { HistoryContainerRegistry } from "moz-src:///browser/components/places/history/HistoryContainerBase.sys.mjs";
import { HistoryDateContainer } from "moz-src:///browser/components/places/history/HistoryDateContainer.sys.mjs";

/**
 * @import { DatePeriod } from "moz-src:///browser/components/places/history/HistoryDateContainer.sys.mjs"
 */

const lazy = XPCOMUtils.declareLazy({
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
});

/**
 * A registry of date containers in history.
 *
 * Upon creation, it populates itself with a list of periods that we expect
 * to show in the UI:
 *
 * - Today
 * - Yesterday
 * - (One day for each of the previous days this month)
 * - (One month for each of the previous months)
 *
 * The containers are initially unloaded. We wait for a container to load before
 * fetching any lists of visits.
 *
 * @augments {HistoryContainerRegistry<HistoryDateContainer>}
 */
export class HistoryDateRegistry extends HistoryContainerRegistry {
  /**
   * Construct a new registry, creating a new container for each of the date
   * periods given.
   *
   * @private
   * @param {DatePeriod[]} datePeriods
   */
  constructor(datePeriods) {
    super(datePeriods.map(period => new HistoryDateContainer(period)));
  }

  /**
   * Create the date groups we expect to show in the UI. Filter groups based on
   * whether they currently have any history visits.
   *
   * @param {object} [options]
   * @param {Date} [options.now] The date used to determine the current day.
   * @returns {Promise<HistoryDateRegistry>}
   */
  static async create({ now = new Date() } = {}) {
    const oldestDate = await HistoryDateRegistry.#getOldestDateInHistory();
    if (oldestDate === null) {
      return new HistoryDateRegistry([]);
    }

    const today = HistoryDateRegistry.#getDayPeriod(now);
    const yesterday = HistoryDateRegistry.#getDayPeriod(now, -1);
    const candidatePeriods = [
      today,
      yesterday,
      ...HistoryDateRegistry.#getDaysThisMonth(today.startDate, oldestDate),
      ...HistoryDateRegistry.#getPreviousMonths(today.startDate, oldestDate),
    ];
    const datePeriods =
      await HistoryDateRegistry.#getDatePeriodsWithVisits(candidatePeriods);

    return new HistoryDateRegistry(datePeriods);
  }

  /**
   * Find the date of the oldest visit in history.
   *
   * If there is no history, returns `null`.
   *
   * @returns {Promise<Date | null>}
   */
  static async #getOldestDateInHistory() {
    const db = await lazy.PlacesUtils.promiseDBConnection();
    const [row] = await db.executeCached(
      `SELECT v.visit_date
       FROM moz_historyvisits v JOIN moz_places h ON h.id = v.place_id
       WHERE h.hidden = 0
       ORDER BY v.visit_date ASC
       LIMIT 1`
    );
    if (!row) {
      return null;
    }
    return lazy.PlacesUtils.toDate(row.getResultByName("visit_date"));
  }

  /**
   * Filter through the given list of date periods, keeping those with at least
   * one visit in history.
   *
   * @param {DatePeriod[]} datePeriods
   * @returns {Promise<DatePeriod[]>}
   */
  static async #getDatePeriodsWithVisits(datePeriods) {
    // Construct a list of params to bind to the SQL query, i.e. a list of:
    // { startDate: Date; endDate: Date; index: number; }
    const paramsList = datePeriods.map(({ startDate, endDate }, index) => {
      return {
        startDate: lazy.PlacesUtils.toPRTime(startDate),
        endDate: lazy.PlacesUtils.toPRTime(endDate),
        index,
      };
    });
    if (!paramsList.length) {
      return datePeriods;
    }

    // Determine the periods to keep (by index).
    const validIndices = new Set();
    const db = await lazy.PlacesUtils.promiseDBConnection();
    await db.executeCached(
      `SELECT :index AS period_index
       WHERE EXISTS (
         SELECT 1 FROM moz_historyvisits v
         JOIN moz_places h ON h.id = v.place_id
         WHERE v.visit_date >= :startDate AND v.visit_date < :endDate
           AND h.hidden = 0
       )`,
      paramsList,
      row => validIndices.add(row.getResultByName("period_index"))
    );

    return datePeriods.filter((_, index) => validIndices.has(index));
  }

  /**
   * Get the period which spans the length of the given day.
   *
   * e.g. If given `date` is the current day, then we return a `DatePeriod`
   * where `startDate` is the exact start of today (0:00:00), and `endDate` is
   * the exact start of tomorrow.
   *
   * @param {Date} date
   * @param {number} [offset] Calendar days relative to the given date.
   * @returns {DatePeriod}
   */
  static #getDayPeriod(date, offset = 0) {
    const year = date.getFullYear();
    const month = date.getMonth();
    const day = date.getDate() + offset;
    return {
      startDate: new Date(year, month, day),
      endDate: new Date(year, month, day + 1),
    };
  }

  /**
   * Construct a `DatePeriod` for each of the earlier days in this month,
   * excluding today and yesterday, until `oldestDate` is reached.
   *
   * @param {Date} today
   * @param {Date} oldestDate
   * @returns {DatePeriod[]}
   */
  static #getDaysThisMonth(today, oldestDate) {
    const startOfCurrentMonth = new Date(
      today.getFullYear(),
      today.getMonth(),
      1
    );
    const datePeriods = [];
    let endDate = HistoryDateRegistry.#getDayPeriod(today, -1).startDate;
    while (endDate > startOfCurrentMonth && endDate > oldestDate) {
      const period = HistoryDateRegistry.#getDayPeriod(endDate, -1);
      datePeriods.push(period);
      endDate = period.startDate;
    }
    return datePeriods;
  }

  /**
   * Construct a `DatePeriod` for each of the previous months, until
   * `oldestDate` is reached.
   *
   * If yesterday falls on the previous month, it will *not* be included in its
   * respective `DatePeriod`.
   *
   * @param {Date} today
   * @param {Date} oldestDate
   * @returns {DatePeriod[]}
   */
  static #getPreviousMonths(today, oldestDate) {
    const startOfCurrentMonth = new Date(
      today.getFullYear(),
      today.getMonth(),
      1
    );
    const yesterday = HistoryDateRegistry.#getDayPeriod(today, -1).startDate;
    const datePeriods = [];
    let endDate =
      yesterday < startOfCurrentMonth ? yesterday : startOfCurrentMonth;
    while (endDate > oldestDate) {
      const previousDay = HistoryDateRegistry.#getDayPeriod(
        endDate,
        -1
      ).startDate;
      const startDate = new Date(
        previousDay.getFullYear(),
        previousDay.getMonth(),
        1
      );
      datePeriods.push({ startDate, endDate });
      endDate = startDate;
    }
    return datePeriods;
  }
}
