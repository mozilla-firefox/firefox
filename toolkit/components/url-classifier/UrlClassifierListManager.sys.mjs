/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

// This is the only implementation of nsIUrlListManager.
// A class that manages lists, namely exception and block lists for
// phishing or malware protection. The ListManager knows how to fetch,
// update, and store lists.
//
// There is a single listmanager for the whole application.
//
// TODO more comprehensive update tests, for example add unittest check
//      that the listmanagers tables are properly written on updates

// Lower and upper limits on the server-provided polling frequency
const minDelayMs = 5 * 60 * 1000;
const maxDelayMs = 24 * 60 * 60 * 1000;
const defaultUpdateIntervalMs = 30 * 60 * 1000;
// The threshold to check if the browser is idle. We will defer the update in
// order to save the power consumption if the browser has been idle for one hour
// because it's likely that the browser will keep idle for a longer period.
const browserIdleThresholdMs = 60 * 60 * 1000;
const PREF_DEBUG_ENABLED = "browser.safebrowsing.debug";
const PREF_TEST_NOTIFICATIONS =
  "browser.safebrowsing.test-notifications.enabled";

let loggingEnabled = false;

// Variables imported from library.
let RequestBackoffV4;

// Log only if browser.safebrowsing.debug is true
function log(...stuff) {
  if (!loggingEnabled) {
    return;
  }

  var d = new Date();
  let msg = "listmanager: " + d.toTimeString() + ": " + stuff.join(" ");
  msg = Services.urlFormatter.trimSensitiveURLs(msg);
  Services.console.logStringMessage(msg);
  dump(msg + "\n");
}

/**
 * A ListManager keeps track of exception and block lists and knows
 * how to update them.
 */
class PROT_ListManager {
  // A map of tableNames to objects of type
  // { updateUrl: <updateUrl>, gethashUrl: <gethashUrl> }
  tablesData = {};

  // A map of updateUrls to maps of tables requiring updates, e.g.
  // { safebrowsing-update-url: { goog-phish-shavar: true,
  //                              goog-malware-shavar: true }
  #needsUpdate = {};

  // A map of updateUrls to single-use nsITimer. An entry exists if and only if
  // there is at least one table with updates enabled for that url. nsITimers
  // are reset when enabling/disabling updates or on update callbacks (update
  // success, update failure, download error).
  #updateCheckers = {};
  #requestBackoffs = {};

  // This is only used by testcases to ensure SafeBrowsing.sys.mjs is inited
  registered = false;

  constructor() {
    loggingEnabled = Services.prefs.getBoolPref(PREF_DEBUG_ENABLED);

    log("Initializing list manager");

    this.dbService_ = Cc["@mozilla.org/url-classifier/dbservice;1"].getService(
      Ci.nsIUrlClassifierDBService
    );

    this.idleService_ = Cc["@mozilla.org/widget/useridleservice;1"].getService(
      Ci.nsIUserIdleService
    );

    Services.obs.addObserver(this, "quit-application");
    Services.prefs.addObserver(PREF_DEBUG_ENABLED, this);
  }

  /**
   * Register a new table table
   *
   * @param tableName - the name of the table
   * @param updateUrl - the url for updating the table
   * @param gethashUrl - the url for fetching hash completions
   * @returns true if the table could be created; false otherwise
   */
  registerTable(tableName, providerName, updateUrl, gethashUrl) {
    this.registered = true;

    const previousUpdateUrl = this.tablesData[tableName]?.updateUrl;
    if (previousUpdateUrl && previousUpdateUrl != updateUrl) {
      this.#removeFromUpdateUrl(tableName, previousUpdateUrl);
    }

    this.tablesData[tableName] = {};
    if (!updateUrl) {
      log("Can't register table " + tableName + " without updateUrl");
      return false;
    }
    log("registering " + tableName + " with " + updateUrl);
    this.tablesData[tableName].updateUrl = updateUrl;
    this.tablesData[tableName].gethashUrl = gethashUrl;
    this.tablesData[tableName].provider = providerName;

    // Keep track of all of our update URLs.
    if (!this.#needsUpdate[updateUrl]) {
      this.#needsUpdate[updateUrl] = {};

      // Using the V4 backoff algorithm for both V2 and V4. See bug 1273398.
      this.#requestBackoffs[updateUrl] = new RequestBackoffV4(
        4 /* num requests */,
        60 * 60 * 1000 /* request time, 60 min */,
        providerName /* used by testcase */
      );
    }
    this.#needsUpdate[updateUrl][tableName] = false;

    return true;
  }

  /**
   * Unregister a table table from list
   */
  unregisterTable(tableName) {
    log("unregistering " + tableName);
    const table = this.tablesData[tableName];
    if (table) {
      this.#removeFromUpdateUrl(tableName, table.updateUrl);
      this.#clearNextUpdateTimes([tableName]);
    }
    delete this.tablesData[tableName];
  }

  /**
   * Stop tracking a table under updateUrl, and stop the update checker of
   * updateUrl if no other table needs it.
   */
  #removeFromUpdateUrl(tableName, updateUrl) {
    delete this.#needsUpdate[updateUrl][tableName];
    if (!this.#updatesNeeded(updateUrl) && this.#updateCheckers[updateUrl]) {
      this.#updateCheckers[updateUrl].cancel();
      this.#updateCheckers[updateUrl] = null;
    }
  }

  /**
   * Delete all of our data tables which seem to leak otherwise.
   * Remove observers
   */
  #shutdown() {
    this.stopUpdateCheckers();
    for (const name in this.tablesData) {
      delete this.tablesData[name];
    }
    Services.obs.removeObserver(this, "quit-application");
    Services.prefs.removeObserver(PREF_DEBUG_ENABLED, this);
  }

  /**
   * xpcom-shutdown callback
   */
  observe(aSubject, aTopic, aData) {
    switch (aTopic) {
      case "quit-application":
        this.#shutdown();
        break;
      case "nsPref:changed":
        if (aData == PREF_DEBUG_ENABLED) {
          loggingEnabled = Services.prefs.getBoolPref(PREF_DEBUG_ENABLED);
        }
        break;
    }
  }

  getGethashUrl(tableName) {
    if (this.tablesData[tableName] && this.tablesData[tableName].gethashUrl) {
      return this.tablesData[tableName].gethashUrl;
    }
    return "";
  }

  getUpdateUrl(tableName) {
    if (this.tablesData[tableName] && this.tablesData[tableName].updateUrl) {
      return this.tablesData[tableName].updateUrl;
    }
    return "";
  }

  /**
   * Enable updates for a single table.
   */
  enableUpdate(tableName) {
    let table = this.tablesData[tableName];
    if (table) {
      log("Enabling table updates for " + tableName);
      this.#needsUpdate[table.updateUrl][tableName] = true;
    }
  }

  isRegistered() {
    return this.registered;
  }

  /**
   * Returns true if any table associated with the updateUrl requires updates.
   *
   * @param updateUrl - the updateUrl
   */
  #updatesNeeded(updateUrl) {
    let updatesNeeded = false;
    for (const tableName in this.#needsUpdate[updateUrl]) {
      if (this.#needsUpdate[updateUrl][tableName]) {
        updatesNeeded = true;
      }
    }
    return updatesNeeded;
  }

  /**
   * Disable updates for all tables.
   */
  disableAllUpdates() {
    for (const tableName of Object.keys(this.tablesData)) {
      this.disableUpdate(tableName);
    }
  }

  /**
   * Disables updates for a single table. Avoid this internal function
   * and use disableAllUpdates() instead.
   */
  disableUpdate(tableName) {
    const table = this.tablesData[tableName];
    if (table) {
      log("Disabling table updates for " + tableName);
      this.#needsUpdate[table.updateUrl][tableName] = false;
      if (
        !this.#updatesNeeded(table.updateUrl) &&
        this.#updateCheckers[table.updateUrl]
      ) {
        this.#updateCheckers[table.updateUrl].cancel();
        this.#updateCheckers[table.updateUrl] = null;
      }
    }
  }

  /**
   * Determine if we have some tables that need updating.
   */
  requireTableUpdates() {
    for (const name in this.tablesData) {
      if (this.#needsUpdate[this.tablesData[name].updateUrl][name]) {
        return true;
      }
    }
    return false;
  }

  /**
   * The pref holding the next update time of a provider, or of a single one of
   * its tables when |tableName| is given.
   */
  #nextUpdateTimePref(provider, tableName) {
    let pref = "browser.safebrowsing.provider." + provider + ".nextupdatetime";
    return tableName ? pref + "." + tableName : pref;
  }

  /**
   * The time at which |tableName| may be requested again. Tables updating at
   * the shortest cadence of their provider have no pref of their own and fall
   * back to the provider-wide next update time.
   */
  #getNextUpdateTime(tableName) {
    let provider = this.tablesData[tableName].provider;
    let value = Services.prefs.getCharPref(
      this.#nextUpdateTimePref(provider, tableName),
      ""
    );
    if (!value) {
      value = Services.prefs.getCharPref(
        this.#nextUpdateTimePref(provider),
        ""
      );
    }
    return value ? parseInt(value, 10) : 0;
  }

  /**
   * Stores the next update time of every table of a provider. The earliest one
   * goes to the provider-wide pref, which is what drives the update timer and
   * what about:url-classifier displays; only the tables that have to wait
   * longer than that get a pref of their own.
   *
   * @param nextUpdateTimes Object A map of table name to next update time.
   * @return Number The earliest of the given times.
   */
  #setNextUpdateTimes(provider, nextUpdateTimes) {
    let times = Object.values(nextUpdateTimes);
    if (!times.length) {
      return null;
    }

    let earliest = Math.min(...times);
    Services.prefs.setCharPref(
      this.#nextUpdateTimePref(provider),
      earliest.toString()
    );

    for (let [tableName, time] of Object.entries(nextUpdateTimes)) {
      let pref = this.#nextUpdateTimePref(provider, tableName);
      if (time > earliest) {
        log("Setting next update of " + tableName + " to " + time);
        Services.prefs.setCharPref(pref, time.toString());
      } else if (Services.prefs.prefHasUserValue(pref)) {
        Services.prefs.clearUserPref(pref);
      }
    }

    return earliest;
  }

  /**
   * Forgets the stored next update times of the given tables, so that they are
   * all considered due on the next check.
   *
   * @param clearProviderTime Boolean Also forget the provider-wide time, which
   *        is what the tables without a pref of their own fall back to.
   */
  #clearNextUpdateTimes(tables, clearProviderTime = false) {
    let clear = pref => {
      if (Services.prefs.prefHasUserValue(pref)) {
        Services.prefs.clearUserPref(pref);
      }
    };

    let providers = new Set();
    for (let tableName of tables) {
      if (!this.tablesData[tableName]) {
        continue;
      }
      let provider = this.tablesData[tableName].provider;
      providers.add(provider);
      clear(this.#nextUpdateTimePref(provider, tableName));
    }

    if (clearProviderTime) {
      for (let provider of providers) {
        clear(this.#nextUpdateTimePref(provider));
      }
    }
  }

  /**
   * Arms the update timer of |updateUrl| for the earliest next update time of
   * the tables it serves.
   */
  #scheduleNextUpdateCheck(updateUrl, minimumDelay = 0) {
    let earliest = null;
    for (const tableName in this.tablesData) {
      if (
        this.tablesData[tableName].updateUrl != updateUrl ||
        !this.#needsUpdate[updateUrl][tableName]
      ) {
        continue;
      }
      let time = this.#getNextUpdateTime(tableName);
      if (earliest === null || time < earliest) {
        earliest = time;
      }
    }

    let delay =
      earliest === null
        ? defaultUpdateIntervalMs
        : Math.min(maxDelayMs, Math.max(0, earliest - Date.now()));
    this.setUpdateCheckTimer(updateUrl, Math.max(delay, minimumDelay));
  }

  /**
   * Clamps a server-provided wait duration to something sane (5 min to 1 day).
   *
   * @param waitForUpdateSec String|undefined The number of seconds the server
   *        asked us to wait, if it said anything at all.
   * @return Number The delay to use, in milliseconds.
   */
  #clampUpdateDelay(waitForUpdateSec) {
    // The time units below are all milliseconds if not specified.
    let delay = 0;
    if (waitForUpdateSec) {
      delay = parseInt(waitForUpdateSec, 10) * 1000;
    }

    // As long as the delay is something sane (5 min to 1 day), use the delay
    // time the server requested.
    if (delay > maxDelayMs) {
      log(
        "Ignoring delay from server (too long), waiting " +
          Math.round(maxDelayMs / 60000) +
          "min"
      );
      return maxDelayMs;
    }
    if (delay < minDelayMs) {
      log(
        "Ignoring delay from server (too short), waiting " +
          Math.round(defaultUpdateIntervalMs / 60000) +
          "min"
      );
      return defaultUpdateIntervalMs;
    }

    log("Waiting " + Math.round(delay / 60000) + "min");
    return delay;
  }

  /**
   *  Set timer to check update after delay
   */
  setUpdateCheckTimer(updateUrl, delay) {
    this.#updateCheckers[updateUrl] = Cc["@mozilla.org/timer;1"].createInstance(
      Ci.nsITimer
    );

    // A helper function to trigger the table update.
    let update = () => {
      if (!this.checkForUpdates(updateUrl)) {
        // Make another attempt later.
        this.setUpdateCheckTimer(updateUrl, defaultUpdateIntervalMs);
      }
    };

    this.#updateCheckers[updateUrl].initWithCallback(
      () => {
        this.#updateCheckers[updateUrl] = null;
        // Check if we are in the idle mode. We will stop the current update and
        // defer it to the next user interaction active if the browser is
        // considered in idle mode.
        if (this.idleService_.idleTime > browserIdleThresholdMs) {
          let observer = function () {
            Services.obs.removeObserver(observer, "user-interaction-active");
            update();
          };

          Services.obs.addObserver(observer, "user-interaction-active");
          return;
        }

        update();
      },
      delay,
      Ci.nsITimer.TYPE_ONE_SHOT
    );
  }

  /**
   * Acts as a nsIUrlClassifierCallback for getTables.
   */
  #kickoffUpdate() {
    this.startingUpdate_ = false;
    let initialUpdateDelay = 3000;
    // Add a fuzz of 0-1 minutes for both v2 and v4 according to Bug 1305478.
    initialUpdateDelay += Math.floor(Math.random() * (1 * 60 * 1000));

    // If the user has never downloaded tables, do the check now.
    log("needsUpdate: " + JSON.stringify(this.#needsUpdate, undefined, 2));
    for (const updateUrl in this.#needsUpdate) {
      // If we haven't already kicked off updates for this updateUrl, set a
      // non-repeating timer for it. The timer delay will be reset either on
      // updateSuccess to the default update interval, or backed off on
      // downloadError. Don't set the updateChecker unless at least one table has
      // updates enabled.
      if (this.#updatesNeeded(updateUrl) && !this.#updateCheckers[updateUrl]) {
        let provider = null;
        Object.keys(this.tablesData).forEach(function (table) {
          if (this.tablesData[table].updateUrl === updateUrl) {
            let newProvider = this.tablesData[table].provider;
            if (provider) {
              if (newProvider !== provider) {
                log(
                  "Multiple tables for the same updateURL have a different provider?!"
                );
              }
            } else {
              provider = newProvider;
            }
          }
        }, this);
        log(
          "Initializing update checker for " +
            updateUrl +
            " provided by " +
            provider
        );

        // Use the initialUpdateDelay + fuzz unless we had previous updates
        // and the server told us when to try again.
        let updateDelay = initialUpdateDelay;
        let nextUpdatePref =
          "browser.safebrowsing.provider." + provider + ".nextupdatetime";
        let nextUpdate = Services.prefs.getCharPref(nextUpdatePref, "");

        if (nextUpdate) {
          updateDelay = Math.min(
            maxDelayMs,
            Math.max(0, nextUpdate - Date.now())
          );
          log("Next update at " + nextUpdate);
        }
        log("Next update " + Math.round(updateDelay / 60000) + "min from now");

        this.setUpdateCheckTimer(updateUrl, updateDelay);
      } else {
        log("No updates needed or already initialized for " + updateUrl);
      }
    }
  }

  stopUpdateCheckers() {
    log("Stopping updates");
    for (const updateUrl in this.#updateCheckers) {
      if (this.#updateCheckers[updateUrl]) {
        this.#updateCheckers[updateUrl].cancel();
        this.#updateCheckers[updateUrl] = null;
      }
    }
  }

  /**
   * Determine if we have any tables that require updating.  Different
   * Wardens may call us with new tables that need to be updated.
   */
  maybeToggleUpdateChecking() {
    // We update tables if we have some tables that want updates.  If there
    // are no tables that want to be updated - we dont need to check anything.
    if (this.requireTableUpdates()) {
      log("Starting managing lists");

      // Get the list of existing tables from the DBService before making any
      // update requests.
      if (!this.startingUpdate_) {
        this.startingUpdate_ = true;
        // check the current state of tables in the database
        this.#kickoffUpdate();
      }
    } else {
      log("Stopping managing lists (if currently active)");
      this.stopUpdateCheckers(); // Cancel pending updates
    }
  }

  /**
   * Force updates for the given tables. This API may trigger more than one update
   * if the table lists provided belong to multiple updateurl (multiple provider).
   * Return false when any update is fail due to back-off algorithm.
   */
  forceUpdates(tables) {
    log("forceUpdates with " + tables);
    if (!tables) {
      return false;
    }

    let updateUrls = new Set();
    tables.split(",").forEach(table => {
      if (this.tablesData[table]) {
        updateUrls.add(this.tablesData[table].updateUrl);
      }
    });

    // Every table is due now, otherwise a forced update would only refresh
    // the ones that happen to have reached their next update time.
    this.#clearNextUpdateTimes(tables.split(","), true);

    let ret = true;

    updateUrls.forEach(url => {
      // Cancel current update timer for the url because we are forcing an update.
      if (this.#updateCheckers[url]) {
        this.#updateCheckers[url].cancel();
        this.#updateCheckers[url] = null;
      }

      // Trigger an update for the given url.
      if (!this.checkForUpdates(url, true)) {
        ret = false;
      }
    });

    return ret;
  }

  /**
   * Updates our internal tables from the update server
   *
   * @param updateUrl: request updates for tables associated with that url, or
   * for all tables if the url is empty.
   * @param manual: the update is triggered manually
   */
  checkForUpdates(updateUrl, manual = false) {
    log("checkForUpdates with " + updateUrl);
    // See if we've triggered the request backoff logic.
    if (!updateUrl) {
      return false;
    }

    // Disable SafeBrowsing updates in Safe Mode, but still allow manually
    // triggering an update for debugging.
    if (Services.appinfo.inSafeMode && !manual) {
      log("update is disabled in Safe Mode");
      return false;
    }

    if (lazy.enableTestNotifications) {
      Services.obs.notifyObservers(
        null,
        "safebrowsing-update-attempt",
        updateUrl
      );
    }

    if (
      !this.#requestBackoffs[updateUrl] ||
      !this.#requestBackoffs[updateUrl].canMakeRequest()
    ) {
      log("Can't make update request");
      return false;
    }
    // Grab the current state of the tables from the database
    this.dbService_.getTables(tableData => {
      this.#makeUpdateRequest(updateUrl, tableData);
    });
    return true;
  }

  /**
   * Method that fires the actual HTTP update request.
   * First we reset any tables that have disappeared.
   *
   * @param tableData List of table data already in the database, in the form
   *        tablename;<chunk ranges>\n
   */
  #makeUpdateRequest(updateUrl, tableData) {
    log("this.tablesData: " + JSON.stringify(this.tablesData, undefined, 2));
    log("existing chunks: " + tableData + "\n");
    // Disallow blank updateUrls
    if (!updateUrl) {
      return;
    }
    // An object of the form
    // {
    //   tableList: comma-separated list of tables to request,
    //   tableNames: map of tables that need updating,
    //   requestPayload: the request payload for the update request,
    //   requestQueryParameters: the query parameters for the update request,
    //   isPostRequest: true if the request is a POST request, false if it's a GET request,
    // }
    var streamerMap = {
      tableList: null,
      tableNames: {},
      requestPayload: "",
      requestQueryParameters: "",
      isPostRequest: true,
    };

    let useProtobuf = false;
    let onceThru = false;
    let provider;
    let now = Date.now();
    for (const tableName in this.tablesData) {
      // Skip tables not matching this update url
      if (this.tablesData[tableName].updateUrl != updateUrl) {
        continue;
      }

      if (provider && this.tablesData[tableName].provider != provider) {
        log(
          "ERROR: Cannot mix tables with different providers within the same update URL."
        );
        continue;
      }

      provider = this.tablesData[tableName].provider;

      // Check if the table is for 'proto'.
      let isCurTableProto = tableName.endsWith("-proto");
      if (!onceThru) {
        useProtobuf = isCurTableProto;
        onceThru = true;
      } else if (useProtobuf !== isCurTableProto) {
        log(
          'ERROR: Cannot mix "proto" tables with other types ' +
            "within the same provider."
        );
      }

      // Each table has its own next update time, because the server can ask
      // us to wait a different amount of time for each of them. Leave out the
      // ones that are not due yet: they stay in the batch of a later request.
      if (
        !this.#needsUpdate[updateUrl][tableName] ||
        this.#getNextUpdateTime(tableName) > now
      ) {
        continue;
      }

      streamerMap.tableNames[tableName] = true;
      if (!streamerMap.tableList) {
        streamerMap.tableList = tableName;
      } else {
        streamerMap.tableList += "," + tableName;
      }
    }

    if (useProtobuf) {
      let tableArray = [];
      Object.keys(streamerMap.tableNames).forEach(aTableName => {
        if (streamerMap.tableNames[aTableName]) {
          tableArray.push(aTableName);
        }
      });

      // Build the <tablename, stateBase64> mapping.
      let tableState = {};
      tableData.split("\n").forEach(line => {
        let p = line.indexOf(";");
        if (-1 === p) {
          return;
        }
        let tableName = line.substring(0, p);
        if (tableName in streamerMap.tableNames) {
          let metadata = line.substring(p + 1).split(":");
          let stateBase64 = metadata[0];
          log(tableName + " ==> " + stateBase64);
          tableState[tableName] = stateBase64;
        }
      });

      // The state is a byte stream which server told us from the
      // last table update. The state would be used to do the partial
      // update and the empty string means the table has
      // never been downloaded. See Bug 1287058 for supporting
      // partial update.
      let stateArray = [];
      tableArray.forEach(listName => {
        stateArray.push(tableState[listName] || "");
      });

      log("stateArray: " + stateArray);

      let urlUtils = Cc["@mozilla.org/url-classifier/utils;1"].getService(
        Ci.nsIUrlClassifierUtils
      );

      if (provider === "google4") {
        streamerMap.requestPayload = urlUtils.makeUpdateRequestV4(
          tableArray,
          stateArray
        );
        streamerMap.isPostRequest = false;
      } else if (provider === "google5") {
        // The request body for v5 is empty and it uses the query parameters to
        // pass the table lists and their versions. We need to encode the
        // versions with URL encoding to avoid issues with special characters.
        stateArray = stateArray.map(encodeURIComponent);

        streamerMap.requestPayload = "";
        streamerMap.requestQueryParameters = urlUtils.makeUpdateRequestV5(
          tableArray,
          stateArray
        );
        // Setting isPostRequest to true to use GET method for v5 requests.
        streamerMap.isPostRequest = true;
      } else if (provider === "test") {
        streamerMap.requestPayload = urlUtils.makeUpdateRequestV4(
          tableArray,
          stateArray
        );
        streamerMap.isPostRequest = false;
      } else {
        log("Unknown provider for protobuf: " + provider);
      }
    } else {
      // Build the request. For each table already in the database, include the
      // chunk data from the database
      let lines = tableData.split("\n");
      for (let i = 0; i < lines.length; i++) {
        let fields = lines[i].split(";");
        let name = fields[0];
        if (streamerMap.tableNames[name]) {
          streamerMap.requestPayload += lines[i] + "\n";
          delete streamerMap.tableNames[name];
        }
      }
      // For each requested table that didn't have chunk data in the database,
      // request it fresh
      for (let tableName in streamerMap.tableNames) {
        streamerMap.requestPayload += tableName + ";\n";
      }

      streamerMap.isPostRequest = true;
    }

    log("update request: " + JSON.stringify(streamerMap, undefined, 2) + "\n");

    // Don't send an empty request. Note that a V4 request carries client info
    // and so is never empty, even when no table was selected.
    if (
      streamerMap.tableList &&
      (streamerMap.requestPayload.length ||
        streamerMap.requestQueryParameters.length)
    ) {
      this.#makeUpdateRequestForEntry(
        updateUrl,
        streamerMap.tableList,
        streamerMap.requestPayload,
        streamerMap.requestQueryParameters,
        streamerMap.isPostRequest,
        provider
      );
    } else {
      // Either no table has reached its next update time yet, or we were
      // disabled between kicking off getTables and now. Either way nothing
      // will call us back, so re-arm the timer ourselves. Never come back
      // sooner than minDelayMs, so that a table we cannot build a request for
      // can't spin the timer.
      log("Not sending empty request");
      this.#scheduleNextUpdateCheck(updateUrl, minDelayMs);
    }
  }

  #makeUpdateRequestForEntry(
    updateUrl,
    tableList,
    requestPayload,
    requestQueryParameters,
    isPostRequest,
    safeBrowsingProvider
  ) {
    log(
      "makeUpdateRequestForEntry: requestPayload " +
        requestPayload +
        " requestQueryParameters " +
        requestQueryParameters +
        " update: " +
        updateUrl +
        " tablelist: " +
        tableList +
        "\n"
    );
    let streamer = Cc["@mozilla.org/url-classifier/streamupdater;1"].getService(
      Ci.nsIUrlClassifierStreamUpdater
    );

    this.#requestBackoffs[updateUrl].noteRequest();

    if (
      !streamer.downloadUpdates(
        tableList,
        requestPayload,
        requestQueryParameters,
        isPostRequest,
        safeBrowsingProvider,
        updateUrl,
        waitForUpdateSec =>
          this.#updateSuccess(tableList, updateUrl, waitForUpdateSec),
        result => this.#updateError(tableList, updateUrl, result),
        status => this.#downloadError(tableList, updateUrl, status)
      )
    ) {
      // Our alarm gets reset in one of the 3 callbacks.
      log("pending update, queued request until later");
    } else {
      let table = Object.keys(this.tablesData).find(key => {
        return this.tablesData[key].updateUrl === updateUrl;
      });
      let provider = this.tablesData[table].provider;
      Services.obs.notifyObservers(null, "safebrowsing-update-begin", provider);
    }
  }

  /**
   * Callback function if the update request succeeded.
   *
   * @param waitForUpdateSec String The number of seconds that the client
   *        should wait before requesting each table again, as a
   *        comma-separated list of "table:seconds" pairs.
   */
  #updateSuccess(tableList, updateUrl, waitForUpdateSec) {
    log(
      "update success for " +
        tableList +
        " from " +
        updateUrl +
        ": " +
        waitForUpdateSec +
        "\n"
    );

    // Let the backoff object know that we completed successfully.
    this.#requestBackoffs[updateUrl].noteServerResponse(200);

    // Get the provider for these tables, check for consistency
    let updatedTables = tableList.split(",");
    let provider = null;
    for (let table of updatedTables) {
      let newProvider = this.tablesData[table].provider;
      if (provider) {
        if (newProvider !== provider) {
          log(
            "Multiple tables for the same updateURL have a different provider?!"
          );
        }
      } else {
        provider = newProvider;
      }
    }

    // The server sends one wait duration per table. A table we asked for but
    // heard nothing about is left out of the map and falls back to the
    // default interval below.
    let waitSecByTable = {};
    for (let entry of waitForUpdateSec.split(",")) {
      let p = entry.indexOf(":");
      if (p > 0) {
        waitSecByTable[entry.substring(0, p)] = entry.substring(p + 1);
      }
    }

    // Work out when each table of this provider may be requested again. The
    // ones we just updated get a new time; the ones that were not part of
    // this request keep the time they already had.
    let now = Date.now();
    let nextUpdateTimes = {};
    for (const tableName in this.tablesData) {
      if (
        this.tablesData[tableName].updateUrl != updateUrl ||
        !this.#needsUpdate[updateUrl][tableName]
      ) {
        continue;
      }
      // A table enabled after this request was built has no time of its own
      // yet, so it reads back the provider-wide one, which has just elapsed.
      // Clamp it to now: the table is due either way, but the pref never holds
      // a time from the past.
      nextUpdateTimes[tableName] = updatedTables.includes(tableName)
        ? now + this.#clampUpdateDelay(waitSecByTable[tableName])
        : Math.max(this.#getNextUpdateTime(tableName), now);
    }

    // Store the last update time (needed to know if the table is "fresh")
    // and the next update times (to know when to update next).
    let lastUpdatePref =
      "browser.safebrowsing.provider." + provider + ".lastupdatetime";
    log("Setting last update of " + provider + " to " + now);
    Services.prefs.setCharPref(lastUpdatePref, now.toString());

    let earliest = this.#setNextUpdateTimes(provider, nextUpdateTimes);

    // We always use a non-repeating timer since the delay is set differently
    // at every callback. Wake up when the first table comes due.
    let delay = earliest === null ? defaultUpdateIntervalMs : earliest - now;
    log(
      "Next update of " + provider + " in " + Math.round(delay / 60000) + "min"
    );
    this.setUpdateCheckTimer(updateUrl, Math.max(0, delay));

    Services.obs.notifyObservers(
      null,
      "safebrowsing-update-finished",
      "success:" + provider
    );
  }

  /**
   * Callback function if the update request succeeded.
   *
   * @param result String The error code of the failure
   */
  #updateError(table, updateUrl, result) {
    log(
      "update error for " + table + " from " + updateUrl + ": " + result + "\n"
    );
    // There was some trouble applying the updates. Don't try again for at least
    // updateInterval milliseconds.
    this.setUpdateCheckTimer(updateUrl, defaultUpdateIntervalMs);

    Services.obs.notifyObservers(
      null,
      "safebrowsing-update-finished",
      "update error: " + result
    );
  }

  /**
   * Callback function when the download failed
   *
   * @param status String http status or an empty string if connection refused.
   */
  #downloadError(table, updateUrl, status) {
    log("download error for " + table + ": " + status + "\n");
    // If status is empty, then we assume that we got an NS_CONNECTION_REFUSED
    // error.  In this case, we treat this is a http 500 error.
    if (!status) {
      status = 500;
    }
    status = parseInt(status, 10);
    this.#requestBackoffs[updateUrl].noteServerResponse(status);
    let delay = defaultUpdateIntervalMs;
    if (this.#requestBackoffs[updateUrl].isErrorStatus(status)) {
      // Schedule an update for when our backoff is complete
      delay = this.#requestBackoffs[updateUrl].nextRequestDelay();
    } else {
      log("Got non error status for error callback?!");
    }

    this.setUpdateCheckTimer(updateUrl, delay);

    Services.obs.notifyObservers(
      null,
      "safebrowsing-update-finished",
      "download error: " + status
    );
  }

  /**
   * Get back-off time for the given provider.
   * Return 0 if we are not in back-off mode.
   */
  getBackOffTime(provider) {
    let updateUrl = "";
    for (const table in this.tablesData) {
      if (this.tablesData[table].provider == provider) {
        updateUrl = this.tablesData[table].updateUrl;
        break;
      }
    }

    if (!updateUrl || !this.#requestBackoffs[updateUrl]) {
      return 0;
    }

    let delay = this.#requestBackoffs[updateUrl].nextRequestDelay();
    return delay == 0 ? 0 : Date.now() + delay;
  }

  QueryInterface = ChromeUtils.generateQI([
    "nsIUrlListManager",
    "nsIObserver",
    "nsITimerCallback",
  ]);
}

let initialized = false;
function Init() {
  if (initialized) {
    return;
  }

  // Pull the library in.
  var jslib =
    Cc["@mozilla.org/url-classifier/jslib;1"].getService().wrappedJSObject;
  RequestBackoffV4 = jslib.RequestBackoffV4;

  initialized = true;
}

export function RegistrationData() {
  Init();
  return new PROT_ListManager();
}

const lazy = {};

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "enableTestNotifications",
  PREF_TEST_NOTIFICATIONS,
  false
);
