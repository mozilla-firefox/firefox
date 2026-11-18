/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Asynchronous API for managing history.
 * The API makes use of `PageInfo` and `VisitInfo` objects.
 *
 * Each successful operation notifies through the PlacesObservers.
 * To listen to such notifications, you must register using
 * PlacesObservers `addListener` and `removeListener` methods.
 *
 * @see PlacesObservers
 */

/**
 * @typedef PageInfo
 * A `PageInfo` object is any object that contains A SUBSET of the
 * following properties. See the documentation of individual methods
 * to find out which properties are required for `PageInfo` arguments
 * or returned for `PageInfo` results.
 *
 * @property {string} [guid]
 *  The globally unique id of the page.
 * @property {string|URL|nsIURI} [url]
 *  The full URI of the page. Note that `PageInfo` values passed as
 *  argument may hold `nsIURI` or `string` values for property `url`,
 *  but `PageInfo` objects returned by this module always hold `URL`
 *  values.
 * @property {string} [title]
 *  The title associated with the page, if any.
 * @property {string} [description]
 *  The description of the page, if any.
 * @property {number} [placeId]
 *  The machine-local (internal) id of the place.
 * @property {string|URL|nsIURI} [previewImageURL]
 *  The preview image URL of the page, if any.
 * @property {string} [siteName]
 *  The name of the site, if any.
 * @property {number} [frecency]
 *  The frecency of the page, if any.
 *  See https://firefox-source-docs.mozilla.org/browser/urlbar/ranking.html.
 *  Note that this property may not be used to change the actual frecency
 *  score of a page, only to retrieve it. In other words, any `frecency` field
 *  passed as argument to a function of this API will be ignored.
 * @property {VisitInfo[]} [visits]
 *  All the visits for this page, if any.
 * @property {Map} [annotations]
 *  A map containing key/value pairs of the annotations for this page, if any.
 */

/**
 * @typedef VisitInfo
 * A `VisitInfo` object is any object that contains A SUBSET of the following
 * properties. See the documentation of individual methods to find out which
 * properties are required for `VisitInfo` arguments or returned for `VisitInfo`
 * results.
 *
 * @property {Date} [date]
 * The time the visit occurred.
 * @property {nsINavHistoryService.TransitionType} [transition]
 *  How the user reached the page.
 * @property {string|URL|nsIURI} [referrer]
 *  The referring URI of this visit. Note that `VisitInfo` passed
 *  as argument may hold `nsIURI` or `string` values for property `referrer`,
 *  but `VisitInfo` objects returned by this module always hold `URL`
 *  values.
 * @property {number} [userContextId]
 *  The userContextId of the visit. Missing or 0 means the default container.
 */

/**
 * @import {OpenedConnection} from "resource://gre/modules/Sqlite.sys.mjs"
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "asyncHistory",
  "@mozilla.org/browser/history;1",
  Ci.mozIAsyncHistory
);

/**
 * Whenever we update numerous pages, it is preferable to yield time to the main
 * thread every so often to avoid janking.
 * These constants determine the maximal number of notifications we
 * may emit before we yield.
 */
const ONRESULT_CHUNK_SIZE = 300;

// This constant determines the maximum number of remove pages before we cycle.
const REMOVE_PAGES_CHUNKLEN = 300;

// eslint-disable-next-line no-shadow
export var History = Object.freeze({
  ANNOTATION_EXPIRE_NEVER: 4,
  // Constants for the type of annotation.
  ANNOTATION_TYPE_STRING: 3,
  ANNOTATION_TYPE_INT64: 5,

  /**
   * Fetch the available information for one page.
   *
   * @param {URL|nsIURI|string} guidOrURI
   *   Either the full URI of the page or the GUID of the page.
   * @param {object} [options]
   * @param {boolean} [options.includeVisits]
   *   Set this to true if `visits` in the PageInfo needs to contain VisitInfo
   *   in a reverse chronological order. By default, `visits` is undefined
   *   inside the returned `PageInfo`.
   * @param {boolean} [options.includeMeta]
   *   Set this to true to fetch page meta fields, i.e. `description`,
   *   `site_name` and `preview_image_url`.
   * @param {boolean} [options.includeAnnotations]
   *   Set this to true to fetch any annotations that are associated with the
   *   page.
   *
   * @returns {Promise<?PageInfo>}
   *   Resolve with the page information if found.
   *
   *   Note: the VisitInfo objects returned while fetching visits do not
   *   contain the property `referrer`.
   *   TODO: Add `referrer` to VisitInfo. See Bug #1365913.
   *
   *   Note: the visits returned will not contain `TRANSITION_EMBED` visits.
   *
   * @throws (Error)
   *   If `guidOrURI` does not have the expected type or if it is a string that
   *   may be parsed neither as a valid URL nor as a valid GUID.
   */
  fetch(guidOrURI, options = {}) {
    // First, normalize to guid or string, and throw if not possible
    guidOrURI = lazy.PlacesUtils.normalizeToURLOrGUID(guidOrURI);

    // See if options exists and make sense
    if (!options || typeof options !== "object") {
      throw new TypeError("options should be an object and not null");
    }

    let hasIncludeVisits = "includeVisits" in options;
    if (hasIncludeVisits && typeof options.includeVisits !== "boolean") {
      throw new TypeError("includeVisits should be a boolean if exists");
    }

    let hasIncludeMeta = "includeMeta" in options;
    if (hasIncludeMeta && typeof options.includeMeta !== "boolean") {
      throw new TypeError("includeMeta should be a boolean if exists");
    }

    let hasIncludeAnnotations = "includeAnnotations" in options;
    if (
      hasIncludeAnnotations &&
      typeof options.includeAnnotations !== "boolean"
    ) {
      throw new TypeError("includeAnnotations should be a boolean if exists");
    }

    return lazy.PlacesUtils.promiseDBConnection().then(db =>
      innerFetch(db, guidOrURI, options)
    );
  },

  /**
   * Fetches all pages which have one or more of the specified annotations.
   *
   * @param {string[]} annotations
   *   An array of strings containing the annotation names to find.
   * @returns {Promise<Map<string, object[]>>}
   *   A Map containing the annotations, pages and their contents, e.g.
   *      Map("anno1" => [{page, content}, {page, content}]), "anno2" => ....);
   */
  fetchAnnotatedPages(annotations) {
    // See if options exists and make sense
    if (!annotations || !Array.isArray(annotations)) {
      throw new TypeError("annotations should be an Array and not null");
    }
    if (annotations.some(name => typeof name !== "string")) {
      throw new TypeError("all annotation values should be strings");
    }

    return lazy.PlacesUtils.promiseDBConnection().then(db =>
      fetchAnnotatedPages(db, annotations)
    );
  },

  /**
   * Fetch multiple pages.
   *
   * @param {string[]|nsIURI[]|URL[]} guidOrURIs
   *   The GUIDs or URLs to fetch pages for.
   * @returns {Promise<Map<string, PageInfo>>}
   *   A map of PageInfos, keyed by the input info, either guid or href. We don't
   *   use nsIURI or URL as keys to avoid complexity, in all the cases the caller
   *   knows which objects is handling, and can unwrap them. Unknown input pages
   *   will have no entry in the Map.
   * @throws (Error)
   *   If input is invalid, for example not a valid GUID or URL.
   */
  fetchMany(guidOrURIs) {
    if (!Array.isArray(guidOrURIs)) {
      throw new TypeError("Input is not an array");
    }
    // First, normalize to guid or URL, throw if not possible
    guidOrURIs = guidOrURIs.map(v => lazy.PlacesUtils.normalizeToURLOrGUID(v));
    return lazy.PlacesUtils.promiseDBConnection().then(db =>
      fetchMany(db, guidOrURIs)
    );
  },

  /**
   * Adds a number of visits for a single page.
   *
   * Any change may be observed through PlacesObservers.
   *
   * @param {PageInfo} pageInfo
   *   Information on a page. This `PageInfo` MUST contain
   *   - a property `url`, as specified by the definition of `PageInfo`.
   *   - a property `visits`, as specified by the definition of `PageInfo`,
   *     which MUST contain at least one visit.
   *   If a property `title` is provided, the title of the page is updated.
   *   If the `date` of a visit is not provided, it defaults to now.
   *   If the `transition` of a visit is not provided, it defaults to TRANSITION_LINK.
   *
   * @returns {Promise<PageInfo>}
   *   A PageInfo object populated with data after the insert is complete. Rejects
   *   if the insert was unsuccessful.
   *
   * @throws (Error)
   *      If the `url` specified was for a protocol that should not be
   *      stored. @see nsNavHistory::CanAddURI
   * @throws (Error)
   *      If `pageInfo` has an unexpected type.
   * @throws (Error)
   *      If `pageInfo` does not have a `url`.
   * @throws (Error)
   *      If `pageInfo` does not have a `visits` property or if the
   *      value of `visits` is ill-typed or is an empty array.
   * @throws (Error)
   *      If an element of `visits` has an invalid `date`.
   * @throws (Error)
   *      If an element of `visits` has an invalid `transition`.
   */
  insert(pageInfo) {
    let info = lazy.PlacesUtils.validatePageInfo(pageInfo);

    return lazy.PlacesUtils.withConnectionWrapper(
      "History.sys.mjs: insert",
      db => insert(db, info)
    );
  },

  /**
   * Adds a number of visits for a number of pages.
   *
   * Any change may be observed through PlacesObservers.
   *
   * @param {Array<PageInfo>} pageInfos
   *   Information on a page. This `PageInfo` MUST contain
   *   - a property `url`, as specified by the definition of `PageInfo`.
   *   - a property `visits`, as specified by the definition of `PageInfo`,
   *     which MUST contain at least one visit.
   *   If a property `title` is provided, the title of the page is updated.
   *   If the `date` of a visit is not provided, it defaults to now.
   *   If the `transition` of a visit is not provided, it defaults to TRANSITION_LINK.
   * @param {(info: PageInfo) => void} [onResult]
   *   A callback invoked for each page inserted.
   * @param {(info: PageInfo) => void} [onError]
   *   A callback invoked for each page which generated an error when an insert
   *   was attempted.
   *
   * @returns {Promise<void>}
   *   A promise resolved once the operation is complete. Rejects only if all of
   *   the inserts were unsuccessful.
   *
   * @throws (Error)
   *      If the `url` specified was for a protocol that should not be
   *      stored. @see nsNavHistory::CanAddURI
   * @throws (Error)
   *      If `pageInfos` has an unexpected type.
   * @throws (Error)
   *      If a `pageInfo` does not have a `url`.
   * @throws (Error)
   *      If a `PageInfo` does not have a `visits` property or if the
   *      value of `visits` is ill-typed or is an empty array.
   * @throws (Error)
   *      If an element of `visits` has an invalid `date`.
   * @throws (Error)
   *      If an element of `visits` has an invalid `transition`.
   */
  insertMany(pageInfos, onResult, onError) {
    let infos = [];

    if (!Array.isArray(pageInfos)) {
      throw new TypeError("pageInfos must be an array");
    }
    if (!pageInfos.length) {
      throw new TypeError("pageInfos may not be an empty array");
    }

    if (onResult && typeof onResult != "function") {
      throw new TypeError(`onResult: ${onResult} is not a valid function`);
    }
    if (onError && typeof onError != "function") {
      throw new TypeError(`onError: ${onError} is not a valid function`);
    }

    for (let pageInfo of pageInfos) {
      let info = lazy.PlacesUtils.validatePageInfo(pageInfo);
      infos.push(info);
    }

    return lazy.PlacesUtils.withConnectionWrapper(
      "History.sys.mjs: insertMany",
      db => insertMany(db, infos, onResult, onError)
    );
  },

  /**
   * Remove pages from the database.
   *
   * Any change may be observed through PlacesObservers.
   *
   * @param {string|nsIURI|URL|string[]|nsIURI[]|URL[]} guidOrURIs
   *   The GUIDs or URLs to remove from the database.
   * @param {(info: PageInfo) => void} [onResult]
   *   A callback invoked for each page found.
   *
   * @returns {Promise<boolean>}
   *   `true` if at least one page was removed, `false` otherwise.
   * @throws (TypeError)
   *   If `pages` has an unexpected type or if a string provided is neither a
   *   valid GUID nor a valid URI or if `pages` is an empty array.
   */
  remove(guidOrURIs, onResult = null) {
    // Normalize and type-check arguments
    let guidOrURIsArray;
    if (Array.isArray(guidOrURIs)) {
      if (!guidOrURIs.length) {
        throw new TypeError("Expected at least one page");
      }
      guidOrURIsArray = guidOrURIs;
    } else {
      guidOrURIsArray = [guidOrURIs];
    }

    let guids = [];
    let urls = [];
    for (let page of guidOrURIsArray) {
      // Normalize to URL or GUID, or throw if `page` cannot
      // be normalized.
      let normalized = lazy.PlacesUtils.normalizeToURLOrGUID(page);
      if (typeof normalized === "string") {
        guids.push(normalized);
      } else {
        urls.push(normalized.href);
      }
    }

    // At this stage, we know that either `guids` is not-empty
    // or `urls` is not-empty.

    if (onResult && typeof onResult != "function") {
      throw new TypeError("Invalid function: " + onResult);
    }

    return (async function () {
      let removedPages = false;
      let count = 0;
      while (guids.length || urls.length) {
        if (count && count % 2 == 0) {
          // Every few cycles, yield time back to the main
          // thread to avoid jank.
          await Promise.resolve();
        }
        count++;
        let guidsSlice = guids.splice(0, REMOVE_PAGES_CHUNKLEN);
        let urlsSlice = [];
        if (guidsSlice.length < REMOVE_PAGES_CHUNKLEN) {
          urlsSlice = urls.splice(0, REMOVE_PAGES_CHUNKLEN - guidsSlice.length);
        }
        let pagesToRemove = { guids: guidsSlice, urls: urlsSlice };
        let result = await lazy.PlacesUtils.withConnectionWrapper(
          "History.sys.mjs: remove",
          db => remove(db, pagesToRemove, onResult)
        );

        removedPages = removedPages || result;
      }
      return removedPages;
    })();
  },

  /**
   * Remove visits matching specific characteristics.
   *
   * Any change may be observed through PlacesObservers.
   *
   * @param {object} filter
   *   A filter with details on how to remove the visits. If both `beginDate` and
   *   `endDate` are specified, visits between `beginDate` (inclusive) and `end`
   *   (inclusive) are removed.
   * @param {Date} filter.beginDate
   *   Remove visits that have been added since this date (inclusive).
   * @param {Date} filter.endDate
   *   Remove visits that have been added before this date (inclusive).
   * @param {number} filter.limit
   *   Limit the number of visits we remove to this number.
   * @param {string|nsIURI|URL} filter.url
   *   Only remove visits to this URL.
   * @param {nsINavHistoryService.TransitionType} filter.transition
   *   The type of the transition (see TRANSITIONS below).
   * @param {(visit: VisitInfo) => void} [onResult]
   *   A callback invoked for each visit found and removed. Note that the
   *   referrer property of `VisitInfo` is NOT populated.
   * @returns {Promise<boolean>}
   *   `true` if at least one visit was removed, `false` otherwise.
   * @throws (TypeError)
   *   If `filter` does not have the expected type, in particular if the `object`
   *   is empty.
   */
  removeVisitsByFilter(filter, onResult = null) {
    if (!filter || typeof filter != "object") {
      throw new TypeError("Expected a filter");
    }

    let hasBeginDate = "beginDate" in filter;
    let hasEndDate = "endDate" in filter;
    let hasURL = "url" in filter;
    let hasLimit = "limit" in filter;
    let hasTransition = "transition" in filter;
    if (hasBeginDate) {
      this.ensureDate(filter.beginDate);
    }
    if (hasEndDate) {
      this.ensureDate(filter.endDate);
    }
    if (hasBeginDate && hasEndDate && filter.beginDate > filter.endDate) {
      throw new TypeError("`beginDate` should be at least as old as `endDate`");
    }
    if (hasTransition && !this.isValidTransition(filter.transition)) {
      throw new TypeError("`transition` should be valid");
    }
    if (
      !hasBeginDate &&
      !hasEndDate &&
      !hasURL &&
      !hasLimit &&
      !hasTransition
    ) {
      throw new TypeError("Expected a non-empty filter");
    }

    if (
      hasURL &&
      !URL.isInstance(filter.url) &&
      typeof filter.url != "string" &&
      !(filter.url instanceof Ci.nsIURI)
    ) {
      throw new TypeError("Expected a valid URL for `url`");
    }

    if (
      hasLimit &&
      (typeof filter.limit != "number" ||
        filter.limit <= 0 ||
        !Number.isInteger(filter.limit))
    ) {
      throw new TypeError("Expected a non-zero positive integer as a limit");
    }

    if (onResult && typeof onResult != "function") {
      throw new TypeError("Invalid function: " + onResult);
    }

    return lazy.PlacesUtils.withConnectionWrapper(
      "History.sys.mjs: removeVisitsByFilter",
      db => removeVisitsByFilter(db, filter, onResult)
    );
  },

  /**
   * Remove pages from the database based on a filter.
   *
   * Any change may be observed through PlacesObservers
   *
   * Note: This removes pages with at least one visit inside the timeframe.
   * Any visits outside the timeframe will also be removed with the page.
   *
   * @param {object} filter
   *   An object containing details of the filter wen removing pages.
   * @param {string} filter.host
   *   A hostname with or without subhost. Examples:
   *   - "mozilla.org" removes pages from mozilla.org but not its subdomains
   *   - ".mozilla.org" removes pages from mozilla.org and its subdomains
   *   - "." removes local files
   * @param {Date} filter.beginDate
   *   The first time the page was visited (inclusive).
   * @param {Date} filter.endDate
   *   The last time the page was visited (inclusive)
   * @param {(info: PageInfo) => void} [onResult]
   *   A callback invoked for each page found.
   * @returns {Promise<boolean>}
   *  `true` if at least one page was removed, `false` otherwise.
   * @throws (TypeError)
   *   If `filter` does not have the expected type, in particular if the `object`
   *   is empty, or its components do not satisfy the criteria given above.
   */
  removeByFilter(filter, onResult) {
    if (!filter || typeof filter !== "object") {
      throw new TypeError("Expected a filter object");
    }

    let hasHost = filter.host;
    if (hasHost) {
      if (typeof filter.host !== "string") {
        throw new TypeError("`host` should be a string");
      }
      filter.host = filter.host.toLowerCase();
      if (filter.host.length > 1 && filter.host.lastIndexOf(".") == 0) {
        // The input contains only an initial period, thus it may be a
        // wildcarded local host, like ".localhost". Ideally the consumer should
        // pass just "localhost", because there is no concept of subhosts for
        // it, but we are being more lenient to allow for simpler input.
        // Anyway, in this case we remove the wildcard to avoid clearing too
        // much if the consumer wrongly passes in things like ".com".
        filter.host = filter.host.slice(1);
      }
    }

    let hasBeginDate = "beginDate" in filter;
    if (hasBeginDate) {
      this.ensureDate(filter.beginDate);
    }

    let hasEndDate = "endDate" in filter;
    if (hasEndDate) {
      this.ensureDate(filter.endDate);
    }

    if (hasBeginDate && hasEndDate && filter.beginDate > filter.endDate) {
      throw new TypeError("`beginDate` should be at least as old as `endDate`");
    }

    if (!hasBeginDate && !hasEndDate && !hasHost) {
      throw new TypeError("Expected a non-empty filter");
    }

    // Check the host format.
    // Either it has no dots, or has multiple dots, or it's a single dot char.
    if (
      hasHost &&
      (!/^(\.?([.a-z0-9-]+\.[a-z0-9-]+)?|[a-z0-9-]+)\.?$/.test(filter.host) ||
        filter.host.includes(".."))
    ) {
      throw new TypeError(
        "Expected well formed hostname string for `host` with atmost 1 wildcard."
      );
    }

    if (onResult && typeof onResult != "function") {
      throw new TypeError("Invalid function: " + onResult);
    }

    return lazy.PlacesUtils.withConnectionWrapper(
      "History.sys.mjs: removeByFilter",
      db => removeByFilter(db, filter, onResult)
    );
  },

  /**
   * Determine if a page has been visited.
   *
   * @param {string|nsIURI|URL} guidOrURI
   *   Either the full URI of the page or the GUID of the page.
   * @returns {Promise<boolean>}
   *   `true` if the page has been visited, `false` otherwise.
   * @throws (Error)
   *   If `guidOrURI` has an unexpected type or if a string provided is neither
   *   not a valid GUID nor a valid URI.
   */
  hasVisits(guidOrURI) {
    // Quick fallback to the cpp version.
    if (guidOrURI instanceof Ci.nsIURI) {
      return new Promise(resolve => {
        lazy.asyncHistory.isURIVisited(guidOrURI, (aURI, aIsVisited) => {
          resolve(aIsVisited);
        });
      });
    }

    let guidOrURL = lazy.PlacesUtils.normalizeToURLOrGUID(guidOrURI);
    let isGuid = typeof guidOrURL == "string";
    let sqlFragment = isGuid
      ? "guid = :val"
      : "url_hash = hash(:val) AND url = :val ";

    return lazy.PlacesUtils.promiseDBConnection().then(async db => {
      let rows = await db.executeCached(
        `SELECT 1 FROM moz_places
                                         WHERE ${sqlFragment}
                                         AND last_visit_date NOTNULL`,
        { val: isGuid ? guidOrURL : guidOrURL.href }
      );
      return !!rows.length;
    });
  },

  /**
   * Clear all history.
   *
   * @returns {Promise<void>}
   *   A promise resolved once the operation is complete.
   */
  clear() {
    return lazy.PlacesUtils.withConnectionWrapper(
      "History.sys.mjs: clear",
      clear
    );
  },

  /**
   * Is a value a valid transition type?
   *
   * @param {nsINavHistoryService.TransitionType} transition
   * @returns {boolean}
   */
  isValidTransition(transition) {
    return Object.values(History.TRANSITIONS).includes(transition);
  },

  /**
   * Throw if an object is not a Date object.
   *
   * @param {any} arg
   */
  ensureDate(arg) {
    if (
      !arg ||
      typeof arg != "object" ||
      arg.constructor.name != "Date" ||
      isNaN(arg)
    ) {
      throw new TypeError("Expected a valid Date, got " + arg);
    }
  },

  /**
   * Update information for a page.
   *
   * Currently, it supports updating the description, preview image URL and annotations
   * for a page, any other fields will be ignored.
   *
   * Note that this function will ignore the update if the target page has not
   * yet been stored in the database. `History.fetch` could be used to check
   * whether the page and its meta information exist or not. Beware that
   * fetch&update might fail as they are not executed in a single transaction.
   *
   * @param {PageInfo} pageInfo
   *   pageInfo must contain a URL of the target page. It will be ignored if a
   *   valid page `guid` is also provided.
   *
   *   If a property `description` is provided, the description of the page is
   *   updated. Note that:
   *     1). An empty string or null `description` will clear the existing
   *         value in the database.
   *     2). Descriptions longer than DB_DESCRIPTION_LENGTH_MAX will be
   *         truncated.
   *
   *   If a property `siteName` is provided, the site name of the page is
   *   updated. Note that:
   *     1). An empty string or null `siteName` will clear the existing
   *         value in the database.
   *     2). Descriptions longer than DB_SITENAME_LENGTH_MAX will be
   *         truncated.
   *
   *   If a property `previewImageURL` is provided, the preview imaged URL of
   *   the page is updated. Note that:
   *     1). A null `previewImageURL` will clear the existing value in the
   *         database.
   *     2). It throws if its length is greater than DB_URL_LENGTH_MAX
   *         defined in PlacesUtils.sys.mjs.
   *
   *   If a property `annotations` is provided, the annotations will be updated.
   *   Note that:
   *     1). It should be a Map containing key/value pairs to be updated.
   *     2). If the value is falsy, the annotation will be removed.
   *     3). If the value is non-falsy, the annotation will be added or updated.
   *     For `annotations` the keys must all be strings, the values should be
   *     Boolean, Number or Strings. null and undefined are supported as falsy values.
   *
   * @returns {Promise<void>}
   *   A promise resolved once the update is complete. Rejects if the update was
   *   unsuccessful.
   *
   * @throws (Error)
   *   If `pageInfo` has an unexpected type.
   * @throws (Error)
   *   If `pageInfo` has an invalid `url` or an invalid `guid`.
   * @throws (Error)
   *   If `pageInfo` has neither `description` nor `previewImageURL`.
   * @throws (Error)
   *   If the length of `pageInfo.previewImageURL` is greater than
   *   DB_URL_LENGTH_MAX defined in PlacesUtils.sys.mjs.
   */
  update(pageInfo) {
    let info = lazy.PlacesUtils.validatePageInfo(pageInfo, false);

    if (
      info.description === undefined &&
      info.siteName === undefined &&
      info.previewImageURL === undefined &&
      info.annotations === undefined
    ) {
      throw new TypeError(
        "pageInfo object must at least have either a description, siteName, previewImageURL or annotations property."
      );
    }

    return lazy.PlacesUtils.withConnectionWrapper(
      "History.sys.mjs: update",
      db => update(db, info)
    );
  },

  /**
   * Possible values for the `transition` property of `VisitInfo`
   * objects.
   */

  TRANSITIONS: Object.freeze({
    /**
     * The user followed a link and got a new toplevel window.
     */
    LINK: Ci.nsINavHistoryService.TRANSITION_LINK,

    /**
     * The user typed the page's URL in the URL bar or selected it from
     * URL bar autocomplete results, clicked on it from a history query
     * (from the History sidebar, History menu, or history query in the
     * personal toolbar or Places organizer.
     */
    TYPED: Ci.nsINavHistoryService.TRANSITION_TYPED,

    /**
     * The user followed a bookmark to get to the page.
     */
    BOOKMARK: Ci.nsINavHistoryService.TRANSITION_BOOKMARK,

    /**
     * Some inner content is loaded. This is true of all images on a
     * page, and the contents of the iframe. It is also true of any
     * content in a frame if the user did not explicitly follow a link
     * to get there.
     */
    EMBED: Ci.nsINavHistoryService.TRANSITION_EMBED,

    /**
     * Set when the transition was a permanent redirect.
     */
    REDIRECT_PERMANENT: Ci.nsINavHistoryService.TRANSITION_REDIRECT_PERMANENT,

    /**
     * Set when the transition was a temporary redirect.
     */
    REDIRECT_TEMPORARY: Ci.nsINavHistoryService.TRANSITION_REDIRECT_TEMPORARY,

    /**
     * Set when the transition is a download.
     */
    DOWNLOAD: Ci.nsINavHistoryService.TRANSITION_DOWNLOAD,

    /**
     * The user followed a link and got a visit in a frame.
     */
    FRAMED_LINK: Ci.nsINavHistoryService.TRANSITION_FRAMED_LINK,

    /**
     * The user reloaded a page.
     */
    RELOAD: Ci.nsINavHistoryService.TRANSITION_RELOAD,
  }),
});

/**
 * Convert a PageInfo object into the format expected by updatePlaces.
 *
 * Note: this assumes that the PageInfo object has already been validated
 * via PlacesUtils.validatePageInfo.
 *
 * @param {PageInfo} pageInfo
 *   The pageInfo to be converted.
 * @returns {object}
 */
function convertForUpdatePlaces(pageInfo) {
  let info = {
    guid: pageInfo.guid,
    uri: lazy.PlacesUtils.toURI(pageInfo.url),
    title: pageInfo.title,
    visits: [],
  };

  for (let inVisit of pageInfo.visits) {
    let visit = {
      visitDate: lazy.PlacesUtils.toPRTime(inVisit.date),
      transitionType: inVisit.transition,
      referrerURI: inVisit.referrer
        ? lazy.PlacesUtils.toURI(inVisit.referrer)
        : undefined,
      userContextId: inVisit.userContextId,
    };
    info.visits.push(visit);
  }
  return info;
}

// Inner implementation of History.clear().
var clear = async function (db) {
  await db.executeTransaction(async function () {
    // Since all metadata must be removed, remove it before pages, to save on
    // foreign key delete cascading.
    await db.execute("DELETE FROM moz_places_metadata");

    // Remove all non-bookmarked places entries first, this will speed up the
    // triggers work.
    await db.execute(`DELETE FROM moz_places WHERE foreign_count = 0`);
    await db.execute(`DELETE FROM moz_updateoriginsdelete_temp`);

    // Expire orphan icons.
    await db.executeCached(`DELETE FROM moz_pages_w_icons
                            WHERE page_url_hash NOT IN (SELECT url_hash FROM moz_places)`);
    await removeOrphanIcons(db);

    // Expire annotations.
    await db.execute(`DELETE FROM moz_annos WHERE NOT EXISTS (
                        SELECT 1 FROM moz_places WHERE id = place_id
                      )`);

    // Expire inputhistory.
    await db.execute(`DELETE FROM moz_inputhistory WHERE place_id IN (
                        SELECT i.place_id FROM moz_inputhistory i
                        LEFT JOIN moz_places h ON h.id = i.place_id
                        WHERE h.id IS NULL)`);

    // Remove all history.
    await db.execute("DELETE FROM moz_historyvisits");
  });

  PlacesObservers.notifyListeners([new PlacesHistoryCleared()]);
};

/**
 * Clean up pages whose history has been modified, by either
 * removing them entirely (if they are marked for removal,
 * typically because all visits have been removed and there
 * are no more foreign keys such as bookmarks) or updating
 * their frecency (otherwise).
 *
 * @param {OpenedConnection} db
 *   The database.
 * @param {object[]} pages
 *   Pages that have been touched and that need cleaning up.
 * @param {number} pages.id
 *   The `moz_places` identifier for the place.
 * @param {number} pages.hash
 *   The `url_hash` of the page, used to expire orphan icons.
 * @param {boolean} pages.hasVisits
 *   If `true`, there remains at least one visit to this page, so the page
 *   should be kept and its frecency updated.
 * @param {boolean} pages.hasForeign
 *   If `true`, the page has at least one foreign reference (i.e. a bookmark),
 *   so the page should be kept and its frecency updated.
 * @returns {Promise<void>}
 */
var cleanupPages = async function (db, pages) {
  let pagesToRemove = pages.filter(p => !p.hasForeign && !p.hasVisits);
  if (!pagesToRemove.length) {
    return;
  }

  // Collect the icons that may become orphans, before removing the pages they're
  // related to.
  let iconIds = [];
  await db.executeCached(
    `SELECT DISTINCT icon_id FROM moz_icons_to_pages
     JOIN moz_pages_w_icons ON id = page_id
     WHERE page_url_hash IN carray(:hashes)`,
    { hashes: pagesToRemove.map(p => p.hash) },
    row => iconIds.push(row.getResultByIndex(0))
  );

  // Note, we are already in a transaction, since callers create it.
  // Check relations regardless, to avoid creating orphans in case of
  // async race conditions.
  await db.executeCached(
    `DELETE FROM moz_places
       WHERE id IN carray(:ids)
         AND foreign_count = 0 AND last_visit_date ISNULL`,
    { ids: pagesToRemove.map(p => p.id) }
  );

  // Expire orphans.
  await db.executeCached(
    `DELETE FROM moz_pages_w_icons
       WHERE page_url_hash IN carray(:hashes)`,
    { hashes: pagesToRemove.map(p => p.hash) }
  );

  await db.executeCached(
    `DELETE FROM moz_annos
       WHERE place_id IN carray(:ids)`,
    { ids: pagesToRemove.map(p => p.id) }
  );
  await db.executeCached(
    `DELETE FROM moz_inputhistory
       WHERE place_id IN carray(:ids)`,
    { ids: pagesToRemove.map(p => p.id) }
  );
  // Hosts accumulated during the places delete are updated through a trigger
  // (see nsPlacesTriggers.h). Their root icons may become orphans.
  let hosts = [];
  await db.executeCached(
    `SELECT DISTINCT fixup_url(host) FROM moz_updateoriginsdelete_temp`,
    null,
    row => hosts.push(row.getResultByIndex(0))
  );
  await db.executeCached(`DELETE FROM moz_updateoriginsdelete_temp`);

  await removeOrphanIconsByIds(db, iconIds);
  await removeOrphanRootIconsByHosts(db, hosts);
};

/**
 * Remove the given icons if they're orphans, see moz_orphan_icons.
 *
 * @param {OpenedConnection} db
 *   The database.
 * @param {number[]} iconIds
 *   Ids of icons that were related to removed pages.
 */
async function removeOrphanIconsByIds(db, iconIds) {
  if (!iconIds.length) {
    return;
  }
  await db.executeCached(
    `DELETE FROM moz_icons WHERE id IN (
       SELECT id FROM moz_orphan_icons WHERE id IN carray(:iconIds)
     )`,
    { iconIds }
  );
}

/**
 * Remove the root icons of the given hosts if they're orphans, see
 * moz_orphan_icons.
 *
 * @param {OpenedConnection} db
 *   The database.
 * @param {string[]} hosts
 *   Hosts of removed origins, as returned by fixup_url().
 */
async function removeOrphanRootIconsByHosts(db, hosts) {
  if (!hosts.length) {
    return;
  }
  await db.executeCached(
    `DELETE FROM moz_icons WHERE id IN (
       SELECT id FROM moz_orphan_icons WHERE id IN (
         SELECT i.id FROM carray(:hosts) h
         JOIN moz_icons i
           ON i.fixed_icon_url_hash = hash(h.value || '/favicon.ico')
          AND fixup_url(i.icon_url) = h.value || '/favicon.ico'
         WHERE i.root = 1
       )
     )`,
    { hosts }
  );
}

/**
 * Remove all the orphan icons, see moz_orphan_icons.
 *
 * @param {OpenedConnection} db
 *   The database.
 */
function removeOrphanIcons(db) {
  return db.executeCached(
    `DELETE FROM moz_icons WHERE id IN (SELECT id FROM moz_orphan_icons)`
  );
}

/**
 * Notify observers that pages have been removed/updated.
 *
 * @param {OpenedConnection} db
 *   The database.
 * @param {object[]} pages
 *   Pages that have been touched and that need cleaning up.
 * @param {number} pages.id
 *   The `moz_places` identifier for the place.
 * @param {string} pages.guid
 *   The guid of the page.
 * @param {URL} pages.url
 *   The URL of the page.
 * @param {boolean} pages.hasVisits
 *   If `true`, there remains at least one visit to this page, so the page
 *   should be kept and its frecency updated.
 * @param {boolean} pages.hasForeign
 *   If `true`, the page has at least one foreign reference (i.e. a bookmark),
 *   so the page should be kept and its frecency updated.
 * @param {nsINavHistoryService.TransitionType|0} transitionType
 *   Set to a valid TRANSITIONS value to indicate all transitions of a certain
 *   type have been removed, otherwise defaults to 0 (unknown value).
 * @returns {Promise<void>}
 */
var notifyCleanup = async function (db, pages, transitionType = 0) {
  const notifications = [];

  for (let page of pages) {
    const isRemovedFromStore = !page.hasVisits && !page.hasForeign;
    notifications.push(
      new PlacesVisitRemoved({
        url: page.url.href,
        pageGuid: page.guid,
        reason: PlacesVisitRemoved.REASON_DELETED,
        transitionType,
        isRemovedFromStore,
        isPartialVisistsRemoval: !isRemovedFromStore && !!page.hasVisits,
      })
    );
  }

  PlacesObservers.notifyListeners(notifications);
};

/**
 * Notify an `onResult` callback of a set of operations
 * that just took place.
 *
 * @template T
 * @param {T[]} data
 *   The data to send to the callback.
 * @param {(info: T) => void} onResult
 *   If provided, call `onResult` with `data[0]`, `data[1]`, etc. Otherwise,
 *   do nothing.
 */
var notifyOnResult = async function (data, onResult) {
  if (!onResult) {
    return;
  }
  let notifiedCount = 0;
  for (let info of data) {
    try {
      onResult(info);
    } catch (ex) {
      // Errors should be reported but should not stop the operation.
      Promise.reject(ex);
    }
    if (++notifiedCount % ONRESULT_CHUNK_SIZE == 0) {
      // Every few notifications, yield time back to the main
      // thread to avoid jank.
      await Promise.resolve();
    }
  }
};

// Inner implementation of History.fetch.
var innerFetch = async function (db, guidOrURL, options) {
  let whereClauseFragment = "";
  let params = {};
  if (URL.isInstance(guidOrURL)) {
    whereClauseFragment = "WHERE h.url_hash = hash(:url) AND h.url = :url";
    params.url = guidOrURL.href;
  } else {
    whereClauseFragment = "WHERE h.guid = :guid";
    params.guid = guidOrURL;
  }

  let visitSelectionFragment = "";
  let joinFragment = "";
  let visitOrderFragment = "";
  if (options.includeVisits) {
    visitSelectionFragment = ", v.visit_date, v.visit_type";
    joinFragment = "JOIN moz_historyvisits v ON h.id = v.place_id";
    visitOrderFragment = "ORDER BY v.visit_date DESC";
  }

  let pageMetaSelectionFragment = "";
  if (options.includeMeta) {
    pageMetaSelectionFragment = ", description, site_name, preview_image_url";
  }

  let query = `SELECT h.id, guid, url, title, frecency
               ${pageMetaSelectionFragment} ${visitSelectionFragment}
               FROM moz_places h ${joinFragment}
               ${whereClauseFragment}
               ${visitOrderFragment}`;
  /** @type {PageInfo} */
  let pageInfo = null;
  let placeId = null;
  await db.executeCached(query, params, row => {
    if (pageInfo === null) {
      // This means we're on the first row, so we need to get all the page info.
      pageInfo = {
        guid: row.getResultByName("guid"),
        url: new URL(row.getResultByName("url")),
        frecency: row.getResultByName("frecency"),
        title: row.getResultByName("title") || "",
      };
      placeId = row.getResultByName("id");
    }
    if (options.includeMeta) {
      pageInfo.description = row.getResultByName("description") || "";
      pageInfo.siteName = row.getResultByName("site_name") || "";
      let previewImageURL = row.getResultByName("preview_image_url");
      pageInfo.previewImageURL = previewImageURL
        ? new URL(previewImageURL)
        : null;
    }
    if (options.includeVisits) {
      // On every row (not just the first), we need to collect visit data.
      if (!("visits" in pageInfo)) {
        pageInfo.visits = [];
      }
      let date = lazy.PlacesUtils.toDate(row.getResultByName("visit_date"));
      let transition = row.getResultByName("visit_type");

      // TODO: Bug #1365913 add referrer URL to the `VisitInfo` data as well.
      pageInfo.visits.push({ date, transition });
    }
  });

  // Only try to get annotations if requested, and if there's an actual page found.
  if (pageInfo && options.includeAnnotations) {
    let rows = await db.executeCached(
      `
      SELECT n.name, a.content FROM moz_anno_attributes n
      JOIN moz_annos a ON n.id = a.anno_attribute_id
      WHERE a.place_id = :placeId
    `,
      { placeId }
    );

    pageInfo.annotations = new Map(
      rows.map(row => [
        row.getResultByName("name"),
        row.getResultByName("content"),
      ])
    );
  }
  return pageInfo;
};

// Inner implementation of History.fetchAnnotatedPages.
var fetchAnnotatedPages = async function (db, annotations) {
  let result = new Map();
  let rows = await db.execute(
    `
    SELECT n.name, h.url, a.content FROM moz_anno_attributes n
    JOIN moz_annos a ON n.id = a.anno_attribute_id
    JOIN moz_places h ON h.id = a.place_id
    WHERE n.name IN (${new Array(annotations.length).fill("?").join(",")})
  `,
    annotations
  );

  for (let row of rows) {
    let uri = URL.parse(row.getResultByName("url"));
    if (!uri) {
      console.error("Invalid URL read from database in fetchAnnotatedPages");
      continue;
    }

    let anno = {
      uri,
      content: row.getResultByName("content"),
    };
    let annoName = row.getResultByName("name");
    let pageAnnos = result.get(annoName);
    if (!pageAnnos) {
      pageAnnos = [];
      result.set(annoName, pageAnnos);
    }
    pageAnnos.push(anno);
  }

  return result;
};

// Inner implementation of History.fetchMany.
var fetchMany = async function (db, guidOrURLs) {
  let resultsMap = new Map();
  let urls = [];
  let guids = [];
  for (let v of guidOrURLs) {
    if (URL.isInstance(v)) {
      urls.push(v);
    } else {
      guids.push(v);
    }
  }
  let wheres = [];
  let params = {};
  if (urls.length) {
    wheres.push(`
      (
        url_hash IN (SELECT hash(value) FROM carray(:urls)) AND
        url IN carray(:urls)
      )`);
    params.urls = urls.map(u => u.href);
  }
  if (guids.length) {
    wheres.push(`guid IN carray(:guids)`);
    params.guids = guids;
  }
  if (!wheres.length) {
    return resultsMap;
  }

  let rows = await db.executeCached(
    `
    SELECT h.id, guid, url, title, frecency
    FROM moz_places h
    WHERE ${wheres.join(" OR ")}
  `,
    params
  );
  let guidSet = new Set(guids);
  for (let row of rows) {
    let pageInfo = {
      placeId: row.getResultByName("id"),
      guid: row.getResultByName("guid"),
      url: new URL(row.getResultByName("url")),
      frecency: row.getResultByName("frecency"),
      title: row.getResultByName("title") || "",
    };
    if (guidSet.has(pageInfo.guid)) {
      resultsMap.set(pageInfo.guid, pageInfo);
    } else {
      resultsMap.set(pageInfo.url.href, pageInfo);
    }
  }
  return resultsMap;
};

// Inner implementation of History.removeVisitsByFilter.
var removeVisitsByFilter = async function (db, filter, onResult = null) {
  // 1. Determine visits that took place during the interval.  Note
  // that the database uses microseconds, while JS uses milliseconds,
  // so we need to *1000 one way and /1000 the other way.
  let conditions = [];
  let args = {};
  let transition = -1;
  if ("beginDate" in filter) {
    conditions.push("v.visit_date >= :begin * 1000");
    args.begin = Number(filter.beginDate);
  }
  if ("endDate" in filter) {
    conditions.push("v.visit_date <= :end * 1000");
    args.end = Number(filter.endDate);
  }
  if ("limit" in filter) {
    args.limit = Number(filter.limit);
  }
  if ("transition" in filter) {
    conditions.push("v.visit_type = :transition");
    args.transition = filter.transition;
    transition = filter.transition;
  }

  let optionalJoin = "";
  if ("url" in filter) {
    let url = filter.url;
    if (url instanceof Ci.nsIURI) {
      url = filter.url.spec;
    } else {
      url = new URL(url).href;
    }
    optionalJoin = `JOIN moz_places h ON h.id = v.place_id`;
    conditions.push("h.url_hash = hash(:url)", "h.url = :url");
    args.url = url;
  }

  let visitsToRemove = [];
  let pagesToInspect = new Set();
  let onResultData = onResult ? [] : null;

  await db.executeCached(
    `SELECT v.id, place_id, visit_date / 1000 AS date, visit_type FROM moz_historyvisits v
             ${optionalJoin}
             WHERE ${conditions.join(" AND ")}${
               args.limit ? " LIMIT :limit" : ""
             }`,
    args,
    row => {
      let id = row.getResultByName("id");
      let place_id = row.getResultByName("place_id");
      visitsToRemove.push(id);
      pagesToInspect.add(place_id);

      if (onResult) {
        onResultData.push({
          date: new Date(row.getResultByName("date")),
          transition: row.getResultByName("visit_type"),
        });
      }
    }
  );

  if (!visitsToRemove.length) {
    // Nothing to do
    return false;
  }

  let pages = [];
  await db.executeTransaction(async function () {
    // 2. Remove all offending visits.
    await db.executeCached(
      `DELETE FROM moz_historyvisits
         WHERE id IN carray(:visitsToRemove)`,
      { visitsToRemove }
    );

    // 3. Find out which pages have been orphaned
    if (pagesToInspect.size) {
      await db.executeCached(
        `SELECT id, url, url_hash, guid,
          (foreign_count != 0) AS has_foreign,
          (last_visit_date NOTNULL) as has_visits
         FROM moz_places
         WHERE id IN carray(:pagesToInspect)`,
        { pagesToInspect: [...pagesToInspect] },
        row => {
          let page = {
            id: row.getResultByName("id"),
            guid: row.getResultByName("guid"),
            hasForeign: row.getResultByName("has_foreign"),
            hasVisits: row.getResultByName("has_visits"),
            url: new URL(row.getResultByName("url")),
            hash: row.getResultByName("url_hash"),
          };
          pages.push(page);
        }
      );
    }

    // 4. Clean up and notify
    await cleanupPages(db, pages);
  });

  notifyCleanup(db, pages, transition);
  notifyOnResult(onResultData, onResult); // don't wait

  return !!visitsToRemove.length;
};

/**
 * Removes all the visits to the given pages, and the pages that are not
 * bookmarked, then cleans up their relations. The pages are removed before
 * their visits, so the visits delete trigger doesn't have to update them.
 *
 * @param {OpenedConnection} db
 *   The database.
 * @param {object[]} pages
 *   The pages to remove, as expected by cleanupPages.
 */
async function removePagesWithVisits(db, pages) {
  let removableIds = pages.filter(p => !p.hasForeign).map(p => p.id);
  if (removableIds.length) {
    await db.executeCached(
      `DELETE FROM moz_places
       WHERE id IN carray(:ids) AND foreign_count = 0`,
      { ids: removableIds }
    );
  }
  await db.executeCached(
    `DELETE FROM moz_historyvisits WHERE place_id IN carray(:ids)`,
    { ids: pages.map(p => p.id) }
  );
  await cleanupPages(db, pages);
}

// Inner implementation of History.removeByFilter
var removeByFilter = async function (db, filter, onResult = null) {
  // 1. Create fragment for date filtration
  let dateFilterSQLFragment = "";
  let conditions = [];
  let params = {};
  if ("beginDate" in filter) {
    conditions.push("v.visit_date >= :begin");
    params.begin = lazy.PlacesUtils.toPRTime(filter.beginDate);
  }
  if ("endDate" in filter) {
    conditions.push("v.visit_date <= :end");
    params.end = lazy.PlacesUtils.toPRTime(filter.endDate);
  }

  if (conditions.length !== 0) {
    dateFilterSQLFragment = `EXISTS
         (SELECT id FROM moz_historyvisits v WHERE v.place_id = h.id AND
         ${conditions.join(" AND ")}
         LIMIT 1)`;
  }

  // 2. Create fragment for host and subhost filtering
  let hostFilterSQLFragment = "";
  if (filter.host) {
    // There are four cases that we need to consider:
    // mozilla.org, .mozilla.org, localhost, and local files
    let revHost = filter.host.split("").reverse().join("");
    if (filter.host == ".") {
      // Local files.
      hostFilterSQLFragment = `h.rev_host = :revHost`;
    } else if (filter.host.startsWith(".")) {
      // Remove the subhost wildcard.
      revHost = revHost.slice(0, -1);
      hostFilterSQLFragment = `h.rev_host between :revHost || "." and :revHost || "/"`;
    } else {
      // This covers non-wildcarded hosts (e.g.: mozilla.org, localhost)
      hostFilterSQLFragment = `h.rev_host = :revHost || "."`;
    }
    params.revHost = revHost;
  }

  // 3. Find out what needs to be removed
  let fragmentArray = [hostFilterSQLFragment, dateFilterSQLFragment];
  let query = `SELECT h.id, url, url_hash, rev_host, guid, title, frecency, foreign_count
       FROM moz_places h WHERE
       (${fragmentArray.filter(f => f !== "").join(") AND (")})`;
  let onResultData = onResult ? [] : null;
  let pages = [];
  let hasPagesToRemove = false;

  await db.executeCached(query, params, row => {
    let hasForeign = row.getResultByName("foreign_count") != 0;
    if (!hasForeign) {
      hasPagesToRemove = true;
    }
    let id = row.getResultByName("id");
    let guid = row.getResultByName("guid");
    let url = row.getResultByName("url");
    let page = {
      id,
      guid,
      hasForeign,
      hasVisits: false,
      url: new URL(url),
      hash: row.getResultByName("url_hash"),
    };
    pages.push(page);
    if (onResult) {
      onResultData.push({
        guid,
        title: row.getResultByName("title"),
        frecency: row.getResultByName("frecency"),
        url: new URL(url),
      });
    }
  });

  if (pages.length === 0) {
    // Nothing to do
    return false;
  }

  await db.executeTransaction(async function () {
    // 4. Actually remove the pages and their visits.
    await removePagesWithVisits(db, pages);
  });

  notifyCleanup(db, pages);
  notifyOnResult(onResultData, onResult);

  return hasPagesToRemove;
};

// Inner implementation of History.remove.
var remove = async function (db, { guids, urls }, onResult = null) {
  // 1. Find out what needs to be removed
  let onResultData = onResult ? [] : null;
  let pages = [];
  let hasPagesToRemove = false;
  function onRow(row) {
    let hasForeign = row.getResultByName("foreign_count") != 0;
    if (!hasForeign) {
      hasPagesToRemove = true;
    }
    let id = row.getResultByName("id");
    let guid = row.getResultByName("guid");
    let url = row.getResultByName("url");
    let page = {
      id,
      guid,
      hasForeign,
      hasVisits: false,
      url: new URL(url),
      hash: row.getResultByName("url_hash"),
    };
    pages.push(page);
    if (onResult) {
      onResultData.push({
        guid,
        title: row.getResultByName("title"),
        frecency: row.getResultByName("frecency"),
        url: new URL(url),
      });
    }
  }
  if (guids.length) {
    await db.executeCached(
      `SELECT id, url, url_hash, guid, foreign_count, title, frecency
         FROM moz_places
         WHERE guid IN carray(:guids)`,
      { guids },
      onRow
    );
  }
  if (urls.length) {
    await db.executeCached(
      `SELECT id, url, url_hash, guid, foreign_count, title, frecency
         FROM moz_places
         WHERE url_hash IN (SELECT hash(value) FROM carray(:urls))
           AND url IN carray(:urls)`,
      { urls },
      onRow
    );
  }

  if (!pages.length) {
    // Nothing to do
    return false;
  }

  await db.executeTransaction(async function () {
    // 2. Remove the pages and all their visits.
    await removePagesWithVisits(db, pages);
  });

  notifyCleanup(db, pages);
  notifyOnResult(onResultData, onResult); // don't wait

  return hasPagesToRemove;
};

/**
 * Merges an updateInfo object, as returned by asyncHistory.updatePlaces
 * into a PageInfo object as defined in this file.
 *
 * @param {mozIPlaceInfo} updateInfo
 *   An object that represents a page that is generated by
 *   asyncHistory.updatePlaces.
 * @param {PageInfo} pageInfo
 *   An PageInfo object into which to merge the data from updateInfo. Defaults
 *   to an empty object so that this method can be used to simply convert an
 *   updateInfo object into a PageInfo object.
 * @returns {PageInfo}
 *   A PageInfo object populated with data from updateInfo.
 */
function mergeUpdateInfoIntoPageInfo(updateInfo, pageInfo = {}) {
  pageInfo.guid = updateInfo.guid;
  pageInfo.title = updateInfo.title;
  if (!pageInfo.url) {
    pageInfo.url = URL.fromURI(updateInfo.uri);
    pageInfo.title = updateInfo.title;
    pageInfo.placeId = updateInfo.placeId;
    pageInfo.visits = updateInfo.visits.map(visit => {
      return {
        visitId: visit.visitId,
        date: lazy.PlacesUtils.toDate(visit.visitDate),
        transition: visit.transitionType,
        referrer: visit.referrerURI ? URL.fromURI(visit.referrerURI) : null,
      };
    });
  }
  return pageInfo;
}

// Inner implementation of History.insert.
var insert = function (db, pageInfo) {
  let info = convertForUpdatePlaces(pageInfo);

  return new Promise((resolve, reject) => {
    lazy.asyncHistory.updatePlaces(info, {
      handleError: error => {
        reject(error);
      },
      handleResult: result => {
        pageInfo = mergeUpdateInfoIntoPageInfo(result, pageInfo);
      },
      handleCompletion: () => {
        resolve(pageInfo);
      },
    });
  });
};

// Inner implementation of History.insertMany.
var insertMany = function (db, pageInfos, onResult, onError) {
  let infos = [];
  let onResultData = [];
  let onErrorData = [];

  for (let pageInfo of pageInfos) {
    let info = convertForUpdatePlaces(pageInfo);
    infos.push(info);
  }

  return new Promise((resolve, reject) => {
    lazy.asyncHistory.updatePlaces(infos, {
      handleError: (resultCode, result) => {
        let pageInfo = mergeUpdateInfoIntoPageInfo(result);
        onErrorData.push(pageInfo);
      },
      handleResult: result => {
        let pageInfo = mergeUpdateInfoIntoPageInfo(result);
        onResultData.push(pageInfo);
      },
      ignoreErrors: !onError,
      ignoreResults: !onResult,
      handleCompletion: updatedCount => {
        notifyOnResult(onResultData, onResult);
        notifyOnResult(onErrorData, onError);
        if (updatedCount > 0) {
          resolve();
        } else {
          reject({ message: "No items were added to history." });
        }
      },
    });
  });
};

// Inner implementation of History.update.
var update = async function (db, pageInfo) {
  // Check for page existence first; we can skip most of the work if it doesn't
  // exist and anyway we'll need the place id multiple times later.
  // Prefer GUID over url if it's present.
  let id;
  if (typeof pageInfo.guid === "string") {
    let rows = await db.executeCached(
      "SELECT id FROM moz_places WHERE guid = :guid",
      { guid: pageInfo.guid }
    );
    id = rows.length ? rows[0].getResultByName("id") : null;
  } else {
    let rows = await db.executeCached(
      "SELECT id FROM moz_places WHERE url_hash = hash(:url) AND url = :url",
      { url: pageInfo.url.href }
    );
    id = rows.length ? rows[0].getResultByName("id") : null;
  }
  if (!id) {
    return;
  }

  let updateFragments = [];
  let params = {};
  if ("description" in pageInfo) {
    updateFragments.push("description");
    params.description = pageInfo.description;
  }
  if ("siteName" in pageInfo) {
    updateFragments.push("site_name");
    params.site_name = pageInfo.siteName;
  }
  if ("previewImageURL" in pageInfo) {
    updateFragments.push("preview_image_url");
    params.preview_image_url = pageInfo.previewImageURL
      ? pageInfo.previewImageURL.href
      : null;
  }
  if (updateFragments.length) {
    // Since this data may be written at every visit and is textual, avoid
    // overwriting the existing record if it didn't change.
    await db.execute(
      `
      UPDATE moz_places
      SET ${updateFragments.map(v => `${v} = :${v}`).join(", ")}
      WHERE id = :id
        AND (${updateFragments
          .map(v => `IFNULL(${v}, '') <> IFNULL(:${v}, '')`)
          .join(" OR ")})
    `,
      { id, ...params }
    );
  }

  if (pageInfo.annotations) {
    let annosToRemove = [];
    let annosToUpdate = [];

    for (let anno of pageInfo.annotations) {
      anno[1] ? annosToUpdate.push(anno[0]) : annosToRemove.push(anno[0]);
    }

    await db.executeTransaction(async function () {
      if (annosToUpdate.length) {
        await db.execute(
          `
          INSERT OR IGNORE INTO moz_anno_attributes (name)
          VALUES ${Array.from(annosToUpdate.keys())
            .map(k => `(:${k})`)
            .join(", ")}
        `,
          Object.assign({}, annosToUpdate)
        );

        for (let anno of annosToUpdate) {
          let content = pageInfo.annotations.get(anno);
          // TODO: We only really need to save the type whilst we still support
          // accessing page annotations via the annotation service.
          let type =
            typeof content == "string"
              ? History.ANNOTATION_TYPE_STRING
              : History.ANNOTATION_TYPE_INT64;
          let date = lazy.PlacesUtils.toPRTime(new Date());

          // This will replace the id every time an annotation is updated. This is
          // not currently an issue as we're not joining on the id field.
          await db.execute(
            `
            INSERT OR REPLACE INTO moz_annos
              (place_id, anno_attribute_id, content, flags,
               expiration, type, dateAdded, lastModified)
            VALUES (:id,
                    (SELECT id FROM moz_anno_attributes WHERE name = :anno_name),
                    :content, 0, :expiration, :type, :date_added,
                    :last_modified)
          `,
            {
              id,
              anno_name: anno,
              content,
              expiration: History.ANNOTATION_EXPIRE_NEVER,
              type,
              // The date fields are unused, so we just set them both to the latest.
              date_added: date,
              last_modified: date,
            }
          );
        }
      }

      for (let anno of annosToRemove) {
        // We don't remove anything from the moz_anno_attributes table. If we
        // delete the last item of a given name, that item really should go away.
        // It will be cleaned up by expiration.
        await db.execute(
          `
          DELETE FROM moz_annos
          WHERE place_id = :id
          AND anno_attribute_id =
            (SELECT id FROM moz_anno_attributes WHERE name = :anno_name)
        `,
          { id, anno_name: anno }
        );
      }
    });
  }
};
