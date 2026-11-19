/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

export const DIGEST_HIGHLIGHT_DURATION_MS = 20 * 60 * 1000;
export const DIGEST_HIGHLIGHT_CLEARED = "smartwindow-digest-highlight-cleared";
export const DIGEST_DISMISSED = "smartwindow-digest-dismissed";

const _clearedHighlightDigestIds = new Set();

export const DigestActivity = {
  /**
   * A digest created this session is highlighted for
   * DIGEST_HIGHLIGHT_DURATION_MS, until its pill is first clicked in any
   * window, or until the browser restarts.
   *
   * @param {{id: string, created_at: number}} digest
   * @returns {number} Epoch milliseconds when the highlight ends, or 0 for no
   *   highlight.
   */
  getHighlightExpiresAt({ id, created_at }) {
    const sessionStart = Services.startup.getStartupInfo().process.getTime();
    if (created_at < sessionStart || _clearedHighlightDigestIds.has(id)) {
      return 0;
    }
    return created_at + DIGEST_HIGHLIGHT_DURATION_MS;
  },

  /**
   * Clears a digest's highlight in every window for the rest of the session.
   *
   * @param {string} digestId
   */
  clearHighlight(digestId) {
    if (_clearedHighlightDigestIds.has(digestId)) {
      return;
    }
    _clearedHighlightDigestIds.add(digestId);
    Services.obs.notifyObservers(null, DIGEST_HIGHLIGHT_CLEARED, digestId);
  },

  /**
   * Removes a dismissed digest's pill from every window.
   *
   * @param {string} digestId
   */
  dismiss(digestId) {
    Services.obs.notifyObservers(null, DIGEST_DISMISSED, digestId);
  },
};

export function _clearDigestHighlightsForTesting() {
  _clearedHighlightDigestIds.clear();
}
