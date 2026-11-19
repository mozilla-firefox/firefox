/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * The feature that owns a stored page. Stored as an integer in
 * aitab_pages.page_type, so a value must never be reused or renumbered once it
 * has shipped.
 */
export const AITAB_PAGE_TYPES = Object.freeze({
  CHAT: 1,
  RESEARCH: 2,
  DIGEST: 3,
});

/**
 * The type a page gets when none is given, and the one every row stored
 * before the page_type column existed is backfilled with.
 */
export const DEFAULT_AITAB_PAGE_TYPE = AITAB_PAGE_TYPES.CHAT;

/**
 * Per-type behaviour for stored pages.
 *
 * @typedef {object} AITabPageTypeDescriptor
 * @property {number} type - One of AITAB_PAGE_TYPES.
 * @property {string} name - Stable name, for logs and error messages.
 * @property {boolean} editable - Whether a stored page can be revised into a
 *   new version, from chat or from the page's own controls.
 * @property {number} sourceTextBudget - Total characters of source text sent
 *   to the model for one generation, shared among the sources.
 */

// Sized so that ten long pages still get about 2,500 characters each: below
// that, facts further down a page never reach the model, and above it
// generation gets slower without the page getting better. Measured after URL
// tokenization, since URLs only reach the model as tokens.
const DEFAULT_SOURCE_TEXT_BUDGET = 25000;

/** @type {Map<number, AITabPageTypeDescriptor>} */
const DESCRIPTORS = new Map(
  [
    {
      type: AITAB_PAGE_TYPES.CHAT,
      name: "chat",
      editable: true,
      sourceTextBudget: DEFAULT_SOURCE_TEXT_BUDGET,
    },
    {
      type: AITAB_PAGE_TYPES.RESEARCH,
      name: "research",
      editable: false,
      sourceTextBudget: DEFAULT_SOURCE_TEXT_BUDGET,
    },
    {
      type: AITAB_PAGE_TYPES.DIGEST,
      name: "digest",
      editable: false,
      sourceTextBudget: DEFAULT_SOURCE_TEXT_BUDGET,
    },
  ].map(descriptor => [descriptor.type, Object.freeze(descriptor)])
);

/**
 * @param {number} pageType
 * @returns {AITabPageTypeDescriptor|null} Null for a value this build does not
 *   know, such as one written by a newer version before a downgrade.
 */
export function getAITabPageType(pageType) {
  return DESCRIPTORS.get(pageType) ?? null;
}

/**
 * Whether a page of `pageType` can be revised. An unknown type is not, since
 * nothing here can say what revising it would mean.
 *
 * @param {number} pageType
 * @returns {boolean}
 */
export function isAITabPageEditable(pageType) {
  return !!getAITabPageType(pageType)?.editable;
}
