/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Content-blocking logs of the pages visited on the current site, per tab.
 *
 * Keyed by the browser's `permanentKey`, which moves with the tab between
 * windows, so the trust panel's cumulative tracker count survives moving a
 * tab. Lives in a module rather than in a window so that every window sees the
 * same logs, and so they outlive the window a tab was moved out of.
 *
 * @type {WeakMap<object, {site: string, logs: Map<number, object>}>}
 * `logs` maps inner window IDs to their parsed log, in order of first visit.
 */
const siteLogsByTab = new WeakMap();

/**
 * The site each tab's tracker count started showing a visit to, keyed like
 * `siteLogsByTab`.
 *
 * @type {WeakMap<object, string>}
 */
const visitedSiteByTab = new WeakMap();

export const TrustPanelTrackerLogs = {
  /**
   * Store the content-blocking log of a page. If the page is on a different
   * site than the logs stored for its tab, those are dropped first.
   *
   * @param {MozBrowser} browser - The page's browser.
   * @param {string} site - The site the page is on.
   * @param {number} innerWindowId - The page's inner window ID.
   * @param {string} log - The page's content-blocking log, as JSON.
   */
  record(browser, site, innerWindowId, log) {
    const key = browser.permanentKey;
    if (!key) {
      return;
    }
    let siteLogs = siteLogsByTab.get(key);
    if (siteLogs?.site !== site) {
      siteLogs = { site, logs: new Map() };
      siteLogsByTab.set(key, siteLogs);
    }
    // Parsed here so the logs belong to this module rather than to the window
    // the tab was in, which may close after the tab is moved.
    siteLogs.logs.set(innerWindowId, JSON.parse(log));
  },

  /**
   * @param {MozBrowser} browser
   * @returns {object[]} The logs stored for the browser's tab, in order of
   * first visit.
   */
  getLogs(browser) {
    const key = browser.permanentKey;
    return key ? [...(siteLogsByTab.get(key)?.logs.values() ?? [])] : [];
  },

  /**
   * Store the site a tab's tracker count started showing a visit to.
   *
   * @param {MozBrowser} browser
   * @param {string} site
   */
  setVisitedSite(browser, site) {
    if (browser.permanentKey) {
      visitedSiteByTab.set(browser.permanentKey, site);
    }
  },

  /**
   * @param {MozBrowser} browser
   * @returns {string|null} The site the browser's tab tracker count is showing
   * a visit to, or null if it hasn't shown one yet, e.g. because the tab was
   * restored or loaded before the tracker count was enabled.
   */
  getVisitedSite(browser) {
    const key = browser.permanentKey;
    return (key && visitedSiteByTab.get(key)) ?? null;
  },
};
