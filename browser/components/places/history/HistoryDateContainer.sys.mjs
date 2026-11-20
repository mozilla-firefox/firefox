/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { HistoryContainerBase } from "moz-src:///browser/components/places/history/HistoryContainerBase.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
});

/**
 * @typedef {object} DatePeriod
 *   A defined period of time.
 * @property {Date} startDate
 *   The beginning of this period, inclusive.
 * @property {Date} endDate
 *   The end of this period, exclusive.
 */

/**
 * A history container for a fixed date period.
 */
export class HistoryDateContainer extends HistoryContainerBase {
  /** @type {number} */
  #startDateTimestamp;
  /** @type {number} */
  #endDateTimestamp;

  /**
   * Construct a new date container.
   *
   * The container's date period is copied from the one given here, and should
   * be considered immutable. Calling `setDate()` on start or end date after
   * construction has no effect.
   *
   * @param {DatePeriod} datePeriod
   */
  constructor(datePeriod) {
    super();
    this.#startDateTimestamp = datePeriod.startDate.getTime();
    this.#endDateTimestamp = datePeriod.endDate.getTime();
  }

  get datePeriod() {
    return {
      startDate: new Date(this.#startDateTimestamp),
      endDate: new Date(this.#endDateTimestamp),
    };
  }

  get startDate() {
    return new Date(this.#startDateTimestamp);
  }

  async fetchHistory() {
    const { startDate, endDate } = this.datePeriod;
    const db = await lazy.PlacesUtils.promiseDBConnection();
    const visits = [];
    await db.executeCached(
      `SELECT MAX(v.visit_date) AS visit_date, h.title, h.url, h.guid
       FROM moz_historyvisits v
       JOIN moz_places h ON h.id = v.place_id
       WHERE v.visit_date >= :startDate AND v.visit_date < :endDate
         AND h.hidden = 0
       GROUP BY v.place_id
       ORDER BY visit_date DESC, h.id DESC`,
      {
        startDate: lazy.PlacesUtils.toPRTime(startDate),
        endDate: lazy.PlacesUtils.toPRTime(endDate),
      },
      row => {
        visits.push({
          date: lazy.PlacesUtils.toDate(row.getResultByName("visit_date")),
          title: row.getResultByName("title"),
          url: row.getResultByName("url"),
          guid: row.getResultByName("guid"),
        });
      }
    );
    return visits;
  }
}
