/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * The internal pages the chat may link to, one check each. Callers compose the
 * set they trust: AIChatContentParent gates navigation, ai-chat-message gates
 * whether a link stays clickable and additionally requires a generated page to
 * have been seen, which the parent process has no access to.
 */

// A generated page's slug, as minted by AITabStore.
const PAGE_NAME_REGEX = /^[\w-]+(\.html)?$/;

/**
 * The browser settings page, under either of its names.
 *
 * @param {?URL} url
 * @returns {boolean}
 */
export function isSettingsURL(url) {
  return (
    url?.protocol === "about:" &&
    (url.pathname === "preferences" || url.pathname === "settings")
  );
}

/**
 * The tasks page, which is internal UI for configuring tasks and renders no
 * content from the pages it is configured with.
 *
 * @param {?URL} url
 * @returns {boolean}
 */
export function isTasksURL(url) {
  return url?.protocol === "about:" && url.pathname === "smartwindowtasks";
}

/**
 * The slug a generated page URL addresses, or null when the URL does not name
 * a page that could be stored. Validated here so a page name the model
 * invented cannot reach the store or be treated as trusted.
 *
 * @param {?URL} url
 * @returns {?string}
 */
export function getSmartPageName(url) {
  if (url?.protocol !== "about:" || url.pathname !== "smartpage") {
    return null;
  }

  const pageName = url.searchParams.get("page");
  return pageName && PAGE_NAME_REGEX.test(pageName) ? pageName : null;
}

/**
 * A generated page (about:smartpage?page=[slug]). The page's own content is
 * untrusted: it is displayed without being interpreted as markup and its links
 * are handled as web navigation.
 *
 * @param {?URL} url
 * @returns {boolean}
 */
export function isSmartPageURL(url) {
  return !!getSmartPageName(url);
}
