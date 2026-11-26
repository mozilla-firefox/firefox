/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports a provider that offers bookmarks with keywords.
 */

import {
  UrlbarProvider,
  UrlbarUtils,
} from "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  KeywordUtils: "resource://gre/modules/KeywordUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
});

/**
 * Class used to create the provider.
 */
export class UrlbarProviderBookmarkKeywords extends UrlbarProvider {
  /**
   * @returns {Values<typeof lazy.UrlbarShared.PROVIDER_TYPE>}
   */
  get type() {
    return lazy.UrlbarShared.PROVIDER_TYPE.HEURISTIC;
  }

  /**
   * Whether this provider should be invoked for the given context.
   * If this method returns false, the providers manager won't start a query
   * with this provider, to save on resources.
   *
   * @param {UrlbarQueryContext} queryContext The query context object
   */
  async isActive(queryContext) {
    return (
      (!queryContext.restrictSource ||
        queryContext.restrictSource ==
          lazy.UrlbarShared.RESULT_SOURCE.BOOKMARKS) &&
      !queryContext.restrictInSearchMode() &&
      !!queryContext.tokens.length
    );
  }

  /**
   * Starts querying.
   *
   * @param {UrlbarQueryContext} queryContext
   * @param {(provider: UrlbarProvider, result: UrlbarResult) => void} addCallback
   *   Callback invoked by the provider to add a new result.
   */
  async startQuery(queryContext, addCallback) {
    let keyword = queryContext.tokens[0]?.value;

    let searchString = UrlbarUtils.substringAfter(
      queryContext.searchString,
      keyword
    ).trim();
    let { entry, url, postData } = await lazy.KeywordUtils.getBindableKeyword(
      keyword,
      searchString
    );
    if (!entry || !url) {
      return;
    }

    let title;
    if (entry.url.host && searchString) {
      // If we have a search string, the result has the title
      // "host: searchString".
      title = UrlbarUtils.strings.formatStringFromName(
        "bookmarkKeywordSearch",
        [
          entry.url.host,
          queryContext.tokens
            .slice(1)
            .map(t => t.value)
            .join(" "),
        ]
      );
    } else {
      title = lazy.UrlbarShared.prepareUrlForDisplay(url);
    }

    let bookmark = await lazy.PlacesUtils.bookmarks.fetch({ url: entry.url });
    let result = new lazy.UrlbarResult({
      type: lazy.UrlbarShared.RESULT_TYPE.KEYWORD,
      source: lazy.UrlbarShared.RESULT_SOURCE.BOOKMARKS,
      heuristic: true,
      payload: {
        title,
        url,
        keyword,
        input: queryContext.searchString,
        postData,
        icon: lazy.UrlbarShared.getIconForUrl(entry.url),
        bookmarkDateMs: bookmark ? bookmark.dateAdded.getTime() : undefined,
      },
      highlights: {
        title: lazy.UrlbarShared.HIGHLIGHT.TYPED,
        url: lazy.UrlbarShared.HIGHLIGHT.TYPED,
        keyword: lazy.UrlbarShared.HIGHLIGHT.TYPED,
      },
    });
    addCallback(this, result);
  }
}
