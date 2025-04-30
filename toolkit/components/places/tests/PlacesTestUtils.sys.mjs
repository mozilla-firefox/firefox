const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  TestUtils: "resource://testing-common/TestUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "PlacesFrecencyRecalculator", () => {
  return Cc["@mozilla.org/places/frecency-recalculator;1"].getService(
    Ci.nsIObserver
  ).wrappedJSObject;
});

export var PlacesTestUtils = Object.freeze({
  /**
   * Asynchronously adds visits to a page.
   *
   * @param {*} aPlaceInfo
   *        A string URL, nsIURI, Window.URL object, info object (explained
   *        below), or an array of any of those.  Info objects describe the
   *        visits to add more fully than URLs/URIs alone and look like this:
   *
   *          {
   *            uri|url: href, URL or nsIURI of the page,
   *            [optional] transition: one of the TRANSITION_* from nsINavHistoryService,
   *            [optional] title: title of the page,
   *            [optional] visitDate: visit date, either in microseconds from the epoch or as a date object
   *            [optional] referrer: nsIURI of the referrer for this visit
   *          }
   *
   * @return {Promise}
   * @resolves When all visits have been added successfully.
   * @rejects JavaScript exception.
   */
  async addVisits(placeInfo) {
    let places = [];
    let infos = [];

    if (Array.isArray(placeInfo)) {
      places.push(...placeInfo);
    } else {
      places.push(placeInfo);
    }

    // Create a PageInfo for each entry.
    let seenUrls = new Set();
    let lastStoredVisit;
    for (let obj of places) {
      let place;
      if (
        obj instanceof Ci.nsIURI ||
        URL.isInstance(obj) ||
        typeof obj == "string"
      ) {
        place = { uri: obj };
      } else if (typeof obj == "object" && (obj.uri || obj.url)) {
        place = obj;
      } else {
        throw new Error("Unsupported type passed to addVisits");
      }

      let referrer = place.referrer
        ? lazy.PlacesUtils.toURI(place.referrer)
        : null;
      let info = { url: place.uri || place.url };
      let spec =
        info.url instanceof Ci.nsIURI ? info.url.spec : new URL(info.url).href;
      info.exposableURI = Services.io.createExposableURI(
        Services.io.newURI(spec)
      );
      info.title = "title" in place ? place.title : "test visit for " + spec;
      let visitDate = place.visitDate;
      if (visitDate) {
        if (visitDate.constructor.name != "Date") {
          // visitDate should be in microseconds. It's easy to do the wrong thing
          // and pass milliseconds, so we lazily check for that.
          // While it's not easily distinguishable, since both are integers, we
          // can check if the value is very far in the past, and assume it's
          // probably a mistake.
          if (visitDate <= Date.now()) {
            throw new Error(
              "AddVisits expects a Date object or _micro_seconds!"
            );
          }
          visitDate = lazy.PlacesUtils.toDate(visitDate);
        }
      } else {
        visitDate = new Date();
      }
      info.visits = [
        {
          transition: place.transition,
          date: visitDate,
          referrer,
        },
      ];
      seenUrls.add(info.url);
      infos.push(info);
      if (
        !place.transition ||
        place.transition != lazy.PlacesUtils.history.TRANSITIONS.EMBED
      ) {
        lastStoredVisit = info;
      }
    }
    await lazy.PlacesUtils.history.insertMany(infos);
    if (seenUrls.size > 1) {
      // If there's only one URL then history has updated frecency already,
      // otherwise we must force a recalculation.
      await lazy.PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
    }
    if (lastStoredVisit) {
      await lazy.TestUtils.waitForCondition(
        () => lazy.PlacesUtils.history.fetch(lastStoredVisit.exposableURI),
        "Ensure history has been updated and is visible to read-only connections"
      );
    }
  },

  /*
   * Add Favicons
   *
   * @param {Map} faviconURLs  keys are page URLs, values are their
   *                           associated favicon URLs.
   */

  async addFavicons(faviconURLs) {
    let faviconPromises = [];

    // If no favicons were provided, we do not want to continue on
    if (!faviconURLs) {
      throw new Error("No favicon URLs were provided");
    }
    for (let [key, val] of faviconURLs) {
      if (!val) {
        throw new Error("URL does not exist");
      }

      let uri = Services.io.newURI(key);
      let faviconURI = Services.io.newURI(val);
      if (!faviconURI.schemeIs("data")) {
        throw new Error(`Favicon URL should be data URL [${faviconURI.spec}]`);
      }

      faviconPromises.push(
        lazy.PlacesUtils.favicons.setFaviconForPage(uri, faviconURI, faviconURI)
      );
    }
    await Promise.all(faviconPromises);
  },

  /*
   * Helper function to call PlacesUtils.favicons.setFaviconForPage() and waits
   * finishing setting. This function throws an error if the status of
   * PlacesUtils.favicons.setFaviconForPage() is not success.
   *
   * @param {string or nsIURI} pageURI
   * @param {string or nsIURI} faviconURI
   * @param {string or nsIURI} faviconDataURL
   * @param {Number} [optional] expiration
   * @return {Promise} waits for finishing setting
   */
  setFaviconForPage(
    pageURI,
    faviconURI,
    faviconDataURL,
    expiration = 0,
    isRichIcon = false
  ) {
    return lazy.PlacesUtils.favicons.setFaviconForPage(
      lazy.PlacesUtils.toURI(pageURI),
      lazy.PlacesUtils.toURI(faviconURI),
      lazy.PlacesUtils.toURI(faviconDataURL),
      expiration,
      isRichIcon
    );
  },

  /*
   * Helper function to call PlacesUtils.favicons.getFaviconForPage(). This
   * function throws an error if the status of
   * PlacesUtils.favicons.setFaviconForPage() is not success.
   *
   * @param {string or URL or nsIURI} pageURI
   * @param {Number} [optional] preferredWidth
   * @return {Promise<nsIFavicon>} resolved with favicon data
   */
  getFaviconForPage(pageURI, preferredWidth = 0) {
    return lazy.PlacesUtils.favicons.getFaviconForPage(
      lazy.PlacesUtils.toURI(pageURI),
      preferredWidth
    );
  },

  /**
   * Get favicon data for given URL from database.
   *
   * @param {string or nsIURI} faviconURI
   *        uri for the favicon
   * @return {nsIURI} data URL
   */
  async getFaviconDataURLFromDB(faviconURI) {
    faviconURI = lazy.PlacesUtils.toURI(faviconURI);

    const db = await lazy.PlacesUtils.promiseDBConnection();
    const rows = await db.executeCached(
      `SELECT data, width
       FROM moz_icons
       WHERE fixed_icon_url_hash = hash(fixup_url(:url))
       AND icon_url = :url
       ORDER BY width DESC`,
      { url: faviconURI.spec }
    );

    if (!rows.length) {
      return null;
    }

    const row = rows[0];
    const data = row.getResultByName("data");
    if (!data.length) {
      return null;
    }

    const UINT64_MAX = 65535;
    const width = row.getResultByName("width");
    const contentType = width === UINT64_MAX ? "image/svg+xml" : "image/png";

    return await PlacesTestUtils.fileDataToDataURL(data, contentType);
  },

  /**
   * Get favicon data for given URL from network.
   *
   * @param {string or nsIURI} faviconURI
   *        nsIURI for the favicon.
   * @param {nsIPrincipal} [optional] loadingPrincipal
   *        The principal to load from network. If no, use system principal.
   * @return {nsIURI} data URL
   *
   * @note This fetching code is for test-code only and should not be copied to
   *       production code, as a proper principal and loadGroup, or ohttp, should
   *       be used by the browser when fetching from the network.
   */
  async getFaviconDataURLFromNetwork(
    faviconURI,
    loadingPrincipal = Services.scriptSecurityManager.getSystemPrincipal()
  ) {
    faviconURI = lazy.PlacesUtils.toURI(faviconURI);
    if (faviconURI.schemeIs("data")) {
      return faviconURI;
    }

    let channel = lazy.NetUtil.newChannel({
      uri: faviconURI,
      loadingPrincipal,
      securityFlags:
        Ci.nsILoadInfo.SEC_REQUIRE_CORS_INHERITS_SEC_CONTEXT |
        Ci.nsILoadInfo.SEC_ALLOW_CHROME |
        Ci.nsILoadInfo.SEC_DISALLOW_SCRIPT,
      contentPolicyType: Ci.nsIContentPolicy.TYPE_INTERNAL_IMAGE_FAVICON,
    });

    let resolver = Promise.withResolvers();

    lazy.NetUtil.asyncFetch(channel, async (input, status, request) => {
      if (!Components.isSuccessCode(status)) {
        resolver.reject(status);
        return;
      }

      try {
        let data = lazy.NetUtil.readInputStream(input, input.available());
        let contentType = request.QueryInterface(Ci.nsIChannel).contentType;
        input.close();

        let dataURL = await PlacesTestUtils.fileDataToDataURL(
          data,
          contentType
        );
        resolver.resolve(dataURL);
      } catch (e) {
        resolver.reject(e);
      }
    });

    return resolver.promise;
  },

  /**
   * Clears any favicons stored in the database.
   */
  async clearFavicons() {
    return new Promise(resolve => {
      Services.obs.addObserver(function observer() {
        Services.obs.removeObserver(observer, "places-favicons-expired");
        resolve();
      }, "places-favicons-expired");
      lazy.PlacesUtils.favicons.expireAllFavicons();
    });
  },

  /**
   * Converts the given data to the data URL.
   *
   * @param data
   *        The file data.
   * @param mimeType
   *        The mime type of the file content.
   * @return Promise that retunes data URL.
   */
  async fileDataToDataURL(data, mimeType) {
    const dataURL = await new Promise(resolve => {
      const buffer = new Uint8ClampedArray(data);
      const blob = new Blob([buffer], { type: mimeType });
      const reader = new FileReader();
      reader.onload = e => {
        resolve(Services.io.newURI(e.target.result));
      };
      reader.readAsDataURL(blob);
    });
    return dataURL;
  },

  /**
   * Adds a bookmark to the database. This should only be used when you need to
   * add keywords. Otherwise, use `PlacesUtils.bookmarks.insert()`.
   * @param {string} aBookmarkObj.uri
   * @param {string} [aBookmarkObj.title]
   * @param {string} [aBookmarkObj.keyword]
   */
  async addBookmarkWithDetails(aBookmarkObj) {
    await lazy.PlacesUtils.bookmarks.insert({
      parentGuid: lazy.PlacesUtils.bookmarks.unfiledGuid,
      title: aBookmarkObj.title || "A bookmark",
      url: aBookmarkObj.uri,
    });

    if (aBookmarkObj.keyword) {
      await lazy.PlacesUtils.keywords.insert({
        keyword: aBookmarkObj.keyword,
        url:
          aBookmarkObj.uri instanceof Ci.nsIURI
            ? aBookmarkObj.uri.spec
            : aBookmarkObj.uri,
        postData: aBookmarkObj.postData,
      });
    }

    if (aBookmarkObj.tags) {
      let uri =
        aBookmarkObj.uri instanceof Ci.nsIURI
          ? aBookmarkObj.uri
          : Services.io.newURI(aBookmarkObj.uri);
      lazy.PlacesUtils.tagging.tagURI(uri, aBookmarkObj.tags);
    }
  },

  /**
   * Waits for all pending async statements on the default connection.
   *
   * @return {Promise}
   * @resolves When all pending async statements finished.
   * @rejects Never.
   *
   * @note The result is achieved by asynchronously executing a query requiring
   *       a write lock.  Since all statements on the same connection are
   *       serialized, the end of this write operation means that all writes are
   *       complete.  Note that WAL makes so that writers don't block readers, but
   *       this is a problem only across different connections.
   */
  promiseAsyncUpdates() {
    return lazy.PlacesUtils.withConnectionWrapper(
      "promiseAsyncUpdates",
      async function (db) {
        try {
          await db.executeCached("BEGIN EXCLUSIVE");
          await db.executeCached("COMMIT");
        } catch (ex) {
          // If we fail to start a transaction, it's because there is already one.
          // In such a case we should not try to commit the existing transaction.
        }
      }
    );
  },

  /**
   * Asynchronously checks if an address is found in the database.
   * @param aURI
   *        nsIURI or address to look for.
   *
   * @return {Promise}
   * @resolves Returns true if the page is found.
   * @rejects JavaScript exception.
   */
  async isPageInDB(aURI) {
    return (
      (await this.getDatabaseValue("moz_places", "id", { url: aURI })) !==
      undefined
    );
  },

  /**
   * Asynchronously checks how many visits exist for a specified page.
   * @param aURI
   *        nsIURI or address to look for.
   *
   * @return {Promise}
   * @resolves Returns the number of visits found.
   * @rejects JavaScript exception.
   */
  async visitsInDB(aURI) {
    let url = aURI instanceof Ci.nsIURI ? aURI.spec : aURI;
    let db = await lazy.PlacesUtils.promiseDBConnection();
    let rows = await db.executeCached(
      `SELECT count(*) FROM moz_historyvisits v
       JOIN moz_places h ON h.id = v.place_id
       WHERE url_hash = hash(:url) AND url = :url`,
      { url }
    );
    return rows[0].getResultByIndex(0);
  },

  /**
   * Marks all syncable bookmarks as synced by setting their sync statuses to
   * "NORMAL", resetting their change counters, and removing all tombstones.
   * Used by tests to avoid calling `PlacesSyncUtils.bookmarks.pullChanges`
   * and `PlacesSyncUtils.bookmarks.pushChanges`.
   *
   * @resolves When all bookmarks have been updated.
   * @rejects JavaScript exception.
   */
  markBookmarksAsSynced() {
    return lazy.PlacesUtils.withConnectionWrapper(
      "PlacesTestUtils: markBookmarksAsSynced",
      function (db) {
        return db.executeTransaction(async function () {
          await db.executeCached(
            `WITH RECURSIVE
           syncedItems(id) AS (
             SELECT b.id FROM moz_bookmarks b
             WHERE b.guid IN ('menu________', 'toolbar_____', 'unfiled_____',
                              'mobile______')
             UNION ALL
             SELECT b.id FROM moz_bookmarks b
             JOIN syncedItems s ON b.parent = s.id
           )
           UPDATE moz_bookmarks
           SET syncChangeCounter = 0,
               syncStatus = :syncStatus
           WHERE id IN syncedItems`,
            { syncStatus: lazy.PlacesUtils.bookmarks.SYNC_STATUS.NORMAL }
          );
          await db.executeCached("DELETE FROM moz_bookmarks_deleted");
        });
      }
    );
  },

  /**
   * Sets sync fields for multiple bookmarks.
   * @param aStatusInfos
   *        One or more objects with the following properties:
   *          { [required] guid: The bookmark's GUID,
   *            syncStatus: An `nsINavBookmarksService::SYNC_STATUS_*` constant,
   *            syncChangeCounter: The sync change counter value,
   *            lastModified: The last modified time,
   *            dateAdded: The date added time.
   *          }
   *
   * @resolves When all bookmarks have been updated.
   * @rejects JavaScript exception.
   */
  setBookmarkSyncFields(...aFieldInfos) {
    return lazy.PlacesUtils.withConnectionWrapper(
      "PlacesTestUtils: setBookmarkSyncFields",
      function (db) {
        return db.executeTransaction(async function () {
          for (let info of aFieldInfos) {
            if (!lazy.PlacesUtils.isValidGuid(info.guid)) {
              throw new Error(`Invalid GUID: ${info.guid}`);
            }
            await db.executeCached(
              `UPDATE moz_bookmarks
             SET syncStatus = IFNULL(:syncStatus, syncStatus),
                 syncChangeCounter = IFNULL(:syncChangeCounter, syncChangeCounter),
                 lastModified = IFNULL(:lastModified, lastModified),
                 dateAdded = IFNULL(:dateAdded, dateAdded)
             WHERE guid = :guid`,
              {
                guid: info.guid,
                syncChangeCounter: info.syncChangeCounter,
                syncStatus: "syncStatus" in info ? info.syncStatus : null,
                lastModified:
                  "lastModified" in info
                    ? lazy.PlacesUtils.toPRTime(info.lastModified)
                    : null,
                dateAdded:
                  "dateAdded" in info
                    ? lazy.PlacesUtils.toPRTime(info.dateAdded)
                    : null,
              }
            );
          }
        });
      }
    );
  },

  async fetchBookmarkSyncFields(...aGuids) {
    let db = await lazy.PlacesUtils.promiseDBConnection();
    let results = [];
    for (let guid of aGuids) {
      let rows = await db.executeCached(
        `
        SELECT syncStatus, syncChangeCounter, lastModified, dateAdded
        FROM moz_bookmarks
        WHERE guid = :guid`,
        { guid }
      );
      if (!rows.length) {
        throw new Error(`Bookmark ${guid} does not exist`);
      }
      results.push({
        guid,
        syncStatus: rows[0].getResultByName("syncStatus"),
        syncChangeCounter: rows[0].getResultByName("syncChangeCounter"),
        lastModified: lazy.PlacesUtils.toDate(
          rows[0].getResultByName("lastModified")
        ),
        dateAdded: lazy.PlacesUtils.toDate(
          rows[0].getResultByName("dateAdded")
        ),
      });
    }
    return results;
  },

  async fetchSyncTombstones() {
    let db = await lazy.PlacesUtils.promiseDBConnection();
    let rows = await db.executeCached(`
      SELECT guid, dateRemoved
      FROM moz_bookmarks_deleted
      ORDER BY guid`);
    return rows.map(row => ({
      guid: row.getResultByName("guid"),
      dateRemoved: lazy.PlacesUtils.toDate(row.getResultByName("dateRemoved")),
    }));
  },

  /**
   * Returns a promise that waits until happening Places events specified by
   * notification parameter.
   *
   * @param {string} notification
   *        Available values are:
   *          bookmark-added
   *          bookmark-removed
   *          bookmark-moved
   *          bookmark-guid_changed
   *          bookmark-keyword_changed
   *          bookmark-tags_changed
   *          bookmark-time_changed
   *          bookmark-title_changed
   *          bookmark-url_changed
   *          favicon-changed
   *          history-cleared
   *          page-removed
   *          page-title-changed
   *          page-visited
   *          pages-rank-changed
   *          purge-caches
   * @param {Function} conditionFn [optional]
   *        If need some more condition to wait, please use conditionFn.
   *        This is an optional, but if set, should returns true when the wait
   *        condition is met.
   * @return {Promise}
   *         A promise that resolved if the wait condition is met.
   *         The resolved value is an array of PlacesEvent object.
   */
  waitForNotification(notification, conditionFn) {
    return new Promise(resolve => {
      function listener(events) {
        if (!conditionFn || conditionFn(events)) {
          PlacesObservers.removeListener([notification], listener);
          resolve(events);
        }
      }
      PlacesObservers.addListener([notification], listener);
    });
  },

  /**
   * A debugging helper that dumps the contents of an SQLite table.
   *
   * @param {String} table
   *        The table name.
   * @param {Sqlite.OpenedConnection} [db]
   *        The mirror database connection.
   * @param {String[]} [columns]
   *        Clumns to be printed, defaults to all.
   */
  async dumpTable({ table, db, columns }) {
    if (!table) {
      throw new Error("Must pass a `table` name");
    }
    if (!db) {
      db = await lazy.PlacesUtils.promiseDBConnection();
    }
    if (!columns) {
      columns = (await db.execute(`PRAGMA table_info('${table}')`)).map(r =>
        r.getResultByName("name")
      );
    }
    let results = [columns.join("\t")];

    let rows = await db.execute(`SELECT ${columns.join()} FROM ${table}`);
    dump(`>> Table ${table} contains ${rows.length} rows\n`);

    for (let row of rows) {
      let numColumns = row.numEntries;
      let rowValues = [];
      for (let i = 0; i < numColumns; ++i) {
        let value = "N/A";
        switch (row.getTypeOfIndex(i)) {
          case Ci.mozIStorageValueArray.VALUE_TYPE_NULL:
            value = "NULL";
            break;
          case Ci.mozIStorageValueArray.VALUE_TYPE_INTEGER:
            value = row.getInt64(i);
            break;
          case Ci.mozIStorageValueArray.VALUE_TYPE_FLOAT:
            value = row.getDouble(i);
            break;
          case Ci.mozIStorageValueArray.VALUE_TYPE_TEXT:
            value = JSON.stringify(row.getString(i));
            break;
        }
        rowValues.push(value.toString().padStart(columns[i].length, " "));
      }
      results.push(rowValues.join("\t"));
    }
    results.push("\n");
    dump(results.join("\n"));
  },

  /**
   * Removes all stored metadata.
   */
  clearMetadata() {
    return lazy.PlacesUtils.withConnectionWrapper(
      "PlacesTestUtils: clearMetadata",
      async db => {
        await db.execute(`DELETE FROM moz_meta`);
        lazy.PlacesUtils.metadata.cache.clear();
      }
    );
  },

  /**
   * Clear moz_inputhistory table.
   */
  async clearInputHistory() {
    await lazy.PlacesUtils.withConnectionWrapper(
      "test:clearInputHistory",
      db => {
        return db.executeCached("DELETE FROM moz_inputhistory");
      }
    );
  },

  /**
   * Clear moz_historyvisits table.
   */
  async clearHistoryVisits() {
    await lazy.PlacesUtils.withConnectionWrapper(
      "test:clearHistoryVisits",
      db => {
        return db.executeCached("DELETE FROM moz_historyvisits");
      }
    );
  },

  /**
   * Compares 2 place: URLs ignoring the order of their params.
   * @param url1 First URL to compare
   * @param url2 Second URL to compare
   * @return whether the URLs are the same
   */
  ComparePlacesURIs(url1, url2) {
    url1 = url1 instanceof Ci.nsIURI ? url1.spec : new URL(url1);
    if (url1.protocol != "place:") {
      throw new Error("Expected a place: uri, got " + url1.href);
    }
    url2 = url2 instanceof Ci.nsIURI ? url2.spec : new URL(url2);
    if (url2.protocol != "place:") {
      throw new Error("Expected a place: uri, got " + url2.href);
    }
    let tokens1 = url1.pathname.split("&").sort().join("&");
    let tokens2 = url2.pathname.split("&").sort().join("&");
    if (tokens1 != tokens2) {
      dump(`Failed comparison between:\n${tokens1}\n${tokens2}\n`);
      return false;
    }
    return true;
  },

  /**
   * Retrieves a single value from a specified field in a database table, based
   * on the given conditions.
   * @param {string} table - The name of the database table to query.
   * @param {string} field - The name of the field to retrieve a value from.
   * @param {Object} [conditions] - An object containing the conditions to
   * filter the query results. The keys represent the names of the columns to
   * filter by, and the values represent the filter values.  It's possible to
   * pass an array as value where the first element is an operator
   * (e.g. "<", ">") and the second element is the actual value.
   * @return {Promise} A Promise that resolves to the value of the specified
   * field from the database table, or null if the query returns no results.
   * @throws If more than one result is found for the given conditions.
   */
  async getDatabaseValue(table, field, conditions = {}) {
    let { fragment: where, params } = this._buildWhereClause(table, conditions);
    let query = `SELECT ${field} FROM ${table} ${where}`;
    let conn = await lazy.PlacesUtils.promiseDBConnection();
    let rows = await conn.executeCached(query, params);
    if (rows.length > 1) {
      throw new Error(
        "getDatabaseValue doesn't support returning multiple results"
      );
    }
    return rows[0]?.getResultByIndex(0);
  },

  /**
   * Updates specified fields in a database table, based on the given
   * conditions.
   * @param {string} table - The name of the database table to add to.
   * @param {string} fields - an object with field, value pairs
   * @param {Object} [conditions] - An object containing the conditions to
   * filter the query results. The keys represent the names of the columns to
   * filter by, and the values represent the filter values. It's possible to
   * pass an array as value where the first element is an operator
   * (e.g. "<", ">") and the second element is the actual value.
   * @return {Promise} A Promise that resolves to the number of affected rows.
   * @throws If no rows were affected.
   */
  async updateDatabaseValues(table, fields, conditions = {}) {
    let { fragment: where, params } = this._buildWhereClause(table, conditions);
    let query = `UPDATE ${table} SET ${Object.keys(fields)
      .map(f => f + " = :" + f)
      .join()} ${where} RETURNING rowid`;
    params = Object.assign(fields, params);
    return lazy.PlacesUtils.withConnectionWrapper(
      "setDatabaseValue",
      async conn => {
        let rows = await conn.executeCached(query, params);
        if (!rows.length) {
          throw new Error("setDatabaseValue didn't update any value");
        }
        return rows.length;
      }
    );
  },

  async promiseItemId(guid) {
    return this.getDatabaseValue("moz_bookmarks", "id", { guid });
  },

  async promiseItemGuid(id) {
    return this.getDatabaseValue("moz_bookmarks", "guid", { id });
  },

  async promiseManyItemIds(guids) {
    let conn = await lazy.PlacesUtils.promiseDBConnection();
    let rows = await conn.executeCached(`
      SELECT guid, id FROM moz_bookmarks WHERE guid IN (${guids
        .map(guid => "'" + guid + "'")
        .join()}
      )`);
    return new Map(
      rows.map(r => [r.getResultByName("guid"), r.getResultByName("id")])
    );
  },

  _buildWhereClause(table, conditions) {
    let fragments = [];
    let params = {};
    for (let [column, value] of Object.entries(conditions)) {
      if (column == "url") {
        if (value instanceof Ci.nsIURI) {
          value = value.spec;
        } else if (URL.isInstance(value)) {
          value = value.href;
        }
      }
      if (column == "url" && table == "moz_places") {
        fragments.push("url_hash = hash(:url) AND url = :url");
      } else if (Array.isArray(value)) {
        // First element is the operator, second element is the value.
        let [op, actualValue] = value;
        fragments.push(`${column} ${op} :${column}`);
        value = actualValue;
      } else {
        fragments.push(`${column} = :${column}`);
      }
      params[column] = value;
    }
    return {
      fragment: fragments.length ? `WHERE ${fragments.join(" AND ")}` : "",
      params,
    };
  },
});
