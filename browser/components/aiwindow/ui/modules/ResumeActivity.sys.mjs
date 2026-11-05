/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// Process-wide PUWYLO state shared across AI Window tabs and windows.
const _dismissedResumeMemoryIds = new Set();
let _resumeSectionHiddenForSession = false;

export const ResumeActivity = {
  /**
   * Marks a resume pill as dismissed for this session.
   * Remove with the pill fallback in Bug 2077652.
   *
   * @param {string} memoryId
   */
  dismissMemory(memoryId) {
    _dismissedResumeMemoryIds.add(memoryId);
  },

  /**
   * @param {string} memoryId
   * @returns {boolean} Whether the memory was dismissed this session.
   */
  isMemoryDismissed(memoryId) {
    return _dismissedResumeMemoryIds.has(memoryId);
  },

  /**
   * Hides empty resume states across AI Window tabs until suggestions become
   * available or the browser restarts.
   *
   * TODO Bug 2064698: Persist this state and use journey availability
   * instead of a locally tracked flag.
   */
  hideSectionForSession() {
    _resumeSectionHiddenForSession = true;
  },

  /**
   * Clears a previous empty-state hide when suggestions become available.
   */
  clearSectionHiddenForSession() {
    _resumeSectionHiddenForSession = false;
  },

  /**
   * @returns {boolean} Whether the resume section was hidden this session.
   */
  isSectionHiddenForSession() {
    return _resumeSectionHiddenForSession;
  },
};

export function _clearDismissedResumeMemoriesForTesting() {
  _dismissedResumeMemoryIds.clear();
}

export function _resetResumeSectionHiddenForTesting() {
  _resumeSectionHiddenForSession = false;
}
