/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  searchBrowsingHistory:
    "moz-src:///browser/components/aiwindow/models/SearchBrowsingHistory.sys.mjs",
});

/**
 * Thin wrapper over the aiwindow `searchBrowsingHistory` model export.
 *
 * @param {object} [options]
 * @param {string} [options.searchTerm]
 * @param {number|null} [options.startTs]
 * @param {number|null} [options.endTs]
 * @param {number} [options.historyLimit]
 * @returns {Promise<{ ok: true, result: object[] }>}
 */
export async function searchBrowsingHistory(options = {}) {
  const {
    searchTerm = "",
    startTs = null,
    endTs = null,
    historyLimit = 15,
  } = options;

  const result = await lazy.searchBrowsingHistory({
    searchTerm,
    startTs,
    endTs,
    historyLimit,
  });

  return { ok: true, result };
}
