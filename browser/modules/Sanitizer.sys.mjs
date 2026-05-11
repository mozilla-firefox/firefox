/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  ChatStore:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs",
  ContextualIdentityService:
    "resource://gre/modules/ContextualIdentityService.sys.mjs",
  FormHistory: "resource://gre/modules/FormHistory.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  PrincipalsCollector: "resource://gre/modules/PrincipalsCollector.sys.mjs",
});

var logConsole;
function log(...msgs) {
  if (!logConsole) {
    logConsole = console.createInstance({
      prefix: "Sanitizer",
      maxLogLevelPref: "browser.sanitizer.loglevel",
    });
  }

  logConsole.log(...msgs);
}

// Used as unique id for pending sanitizations.
var gPendingSanitizationSerial = 0;

var gPrincipalsCollector = null;

export var Sanitizer = {
  /**
   * Whether we should sanitize on shutdown.
   */
  PREF_SANITIZE_ON_SHUTDOWN: "privacy.sanitize.sanitizeOnShutdown",

  /**
   * During a sanitization this is set to a JSON containing an array of the
   * pending sanitizations. This allows to retry sanitizations on startup in
   * case they dind't run or were interrupted by a crash.
   * Use addPendingSanitization and removePendingSanitization to manage it.
   */
  PREF_PENDING_SANITIZATIONS: "privacy.sanitize.pending",

  /**
   * Pref branches to fetch sanitization options from.
   */
  PREF_CPD_BRANCH: "privacy.cpd.",
  PREF_SHUTDOWN_BRANCH: "privacy.clearOnShutdown_v2.",

  /**
   * The fallback timestamp used when no argument is given to
   * Sanitizer.getClearRange.
   */
  PREF_TIMESPAN: "privacy.sanitize.timeSpan",

  /**
   * Pref to newTab segregation. If true, on shutdown, the private container
   * used in about:newtab is cleaned up.  Exposed because used in tests.
   */
  PREF_NEWTAB_SEGREGATION:
    "privacy.usercontext.about_newtab_segregation.enabled",

  /**
   * Time span constants corresponding to values of the privacy.sanitize.timeSpan
   * pref.  Used to determine how much history to clear, for various items
   */
  TIMESPAN_EVERYTHING: 0,
  TIMESPAN_HOUR: 1,
  TIMESPAN_2HOURS: 2,
  TIMESPAN_4HOURS: 3,
  TIMESPAN_TODAY: 4,
  TIMESPAN_5MIN: 5,
  TIMESPAN_24HOURS: 6,

  /**
   * Mapping time span constants to get total time in ms from the selected
   * time spans
   */
  timeSpanMsMap: {
    TIMESPAN_5MIN: 300000, // 5*60*1000
    TIMESPAN_HOUR: 3600000, // 60*60*1000
    TIMESPAN_2HOURS: 7200000, // 2*60*60*1000
    TIMESPAN_4HOURS: 14400000, // 4*60*60*1000
    TIMESPAN_24HOURS: 86400000, // 24*60*60*1000
    get TIMESPAN_TODAY() {
      return Date.now() - new Date().setHours(0, 0, 0, 0);
    }, // time spent today
  },

  /**
   * Whether we should sanitize on shutdown.
   * When this is set, a pending sanitization should also be added and removed
   * when shutdown sanitization is complete. This allows to retry incomplete
   * sanitizations on startup.
   */
  shouldSanitizeOnShutdown: false,

  /**
   * Whether we should sanitize the private container for about:newtab.
   */
  shouldSanitizeNewTabContainer: false,

  /**
   * Shows a sanitization dialog to the user. Returns after the dialog box has
   * closed.
   *
   * @param parentWindow the browser window to use as parent for the created
   *        dialog.
   * @param {string} mode - flag to let the dialog know if it is opened
   *        using the clear on shutdown (clearOnShutdown) settings option
   *        in about:preferences or in a clear site data context (clearSiteData)
   * @returns {"accept" | "cancel"} - The selected dialog box option.
   *
   * @throws if parentWindow is undefined or doesn't have a gDialogBox.
   */
  async showUI(parentWindow, mode) {
    // Treat the hidden window as not being a parent window:
    if (
      parentWindow?.document.documentURI ==
      "chrome://browser/content/hiddenWindowMac.xhtml"
    ) {
      parentWindow = null;
    }

    let dialogFile = "sanitize_v2.xhtml";
    let deferred = Promise.withResolvers();

    if (parentWindow?.gDialogBox) {
      parentWindow.gDialogBox.open(`chrome://browser/content/${dialogFile}`, {
        inBrowserWindow: true,
        mode,
        onAccept: () => deferred.resolve("accept"),
        onCancel: () => deferred.resolve("cancel"),
      });
    } else {
      Services.ww.openWindow(
        parentWindow,
        `chrome://browser/content/${dialogFile}`,
        "Sanitize",
        "chrome,titlebar,dialog,centerscreen,modal",
        {
          needNativeUI: true,
          mode,
          onAccept: () => deferred.resolve("accept"),
          onCancel: () => deferred.resolve("cancel"),
        }
      );
    }

    return deferred.promise;
  },

  /**
   * Performs startup tasks:
   *  - Checks if sanitizations were not completed during the last session.
   *  - Registers sanitize-on-shutdown.
   */
  async onStartup() {
    // First, collect pending sanitizations from the last session, before we
    // add pending sanitizations for this session.
    let pendingSanitizations = getAndClearPendingSanitizations();
    log("Pending sanitizations:", pendingSanitizations);

    // Check if we should sanitize on shutdown.
    this.shouldSanitizeOnShutdown = Services.prefs.getBoolPref(
      Sanitizer.PREF_SANITIZE_ON_SHUTDOWN,
      false
    );
    Services.prefs.addObserver(Sanitizer.PREF_SANITIZE_ON_SHUTDOWN, this, true);
    // Add a pending shutdown sanitization, if necessary.
    if (this.shouldSanitizeOnShutdown) {
      let itemsToClear = getItemsToClearFromPrefBranch(
        Sanitizer.PREF_SHUTDOWN_BRANCH
      );
      addPendingSanitization("shutdown", itemsToClear, {});
    }
    // Shutdown sanitization is always pending, but the user may change the
    // sanitize on shutdown prefs during the session. Then the pending
    // sanitization would become stale and must be updated.
    Services.prefs.addObserver(Sanitizer.PREF_SHUTDOWN_BRANCH, this, true);

    // Make sure that we are triggered during shutdown.
    let shutdownClient = lazy.PlacesUtils.history.shutdownClient.jsclient;
    // We need to pass to sanitize() (through sanitizeOnShutdown) a state object
    // that tracks the status of the shutdown blocker. This `progress` object
    // will be updated during sanitization and reported with the crash in case of
    // a shutdown timeout.
    // We use the `options` argument to pass the `progress` object to sanitize().
    let progress = { isShutdown: true, clearHonoringExceptions: true };
    shutdownClient.addBlocker(
      "sanitize.js: Sanitize on shutdown",
      () => sanitizeOnShutdown(progress),
      { fetchState: () => ({ progress }) }
    );

    this.shouldSanitizeNewTabContainer = Services.prefs.getBoolPref(
      this.PREF_NEWTAB_SEGREGATION,
      false
    );
    if (this.shouldSanitizeNewTabContainer) {
      addPendingSanitization("newtab-container", [], {});
    }

    let i = pendingSanitizations.findIndex(s => s.id == "newtab-container");
    if (i != -1) {
      pendingSanitizations.splice(i, 1);
      sanitizeNewTabSegregation();
    }

    // Finally, run the sanitizations that were left pending, because we crashed
    // before completing them.
    for (let { itemsToClear, options } of pendingSanitizations) {
      try {
        // We need to set this flag to watch out for the users exceptions like we do on shutdown
        options.progress = { clearHonoringExceptions: true };
        await this.sanitize(itemsToClear, options);
      } catch (ex) {
        console.error(
          "A previously pending sanitization failed: ",
          itemsToClear,
          ex
        );
      }
    }
    await cleanupAfterSanitization(Ci.nsIClearDataService.CLEAR_ALL);
  },

  /**
   * Returns a 2 element array representing the start and end times,
   * in the uSec-since-epoch format that PRTime likes. If we should
   * clear everything, this function returns null.
   *
   * @param ts [optional] a timespan to convert to start and end time.
   *                      Falls back to the privacy.sanitize.timeSpan preference
   *                      if this argument is omitted.
   *                      If this argument is provided, it has to be one of the
   *                      Sanitizer.TIMESPAN_* constants. This function will
   *                      throw an error otherwise.
   *
   * @return {Array} a 2-element Array containing the start and end times.
   */
  getClearRange(ts) {
    if (ts === undefined) {
      ts = Services.prefs.getIntPref(Sanitizer.PREF_TIMESPAN);
    }
    if (ts === Sanitizer.TIMESPAN_EVERYTHING) {
      return null;
    }

    // PRTime is microseconds while JS time is milliseconds
    var endDate = Date.now() * 1000;
    switch (ts) {
      case Sanitizer.TIMESPAN_5MIN:
        var startDate = endDate - 300000000; // 5*60*1000000
        break;
      case Sanitizer.TIMESPAN_HOUR:
        startDate = endDate - 3600000000; // 1*60*60*1000000
        break;
      case Sanitizer.TIMESPAN_2HOURS:
        startDate = endDate - 7200000000; // 2*60*60*1000000
        break;
      case Sanitizer.TIMESPAN_4HOURS:
        startDate = endDate - 14400000000; // 4*60*60*1000000
        break;
      case Sanitizer.TIMESPAN_TODAY:
        var d = new Date(); // Start with today
        d.setHours(0); // zero us back to midnight...
        d.setMinutes(0);
        d.setSeconds(0);
        d.setMilliseconds(0);
        startDate = d.valueOf() * 1000; // convert to epoch usec
        break;
      case Sanitizer.TIMESPAN_24HOURS:
        startDate = endDate - 86400000000; // 24*60*60*1000000
        break;
      default:
        throw new Error("Invalid time span for clear private data: " + ts);
    }
    return [startDate, endDate];
  },

  /**
   * Deletes privacy sensitive data in a batch, according to user preferences.
   * Returns a promise which is resolved if no errors occurred.  If an error
   * occurs, a message is reported to the console and all other items are still
   * cleared before the promise is finally rejected.
   *
   * @param [optional] itemsToClear
   *        Array of items to be cleared. if specified only those
   *        items get cleared, irrespectively of the preference settings.
   * @param [optional] options
   *        Object whose properties are options for this sanitization:
   *         - ignoreTimespan (default: true): Time span only makes sense in
   *           certain cases.  Consumers who want to only clear some private
   *           data can opt in by setting this to false, and can optionally
   *           specify a specific range.
   *           If timespan is not ignored, and range is not set, sanitize() will
   *           use the value of the timespan pref to determine a range.
   *         - range (default: null): array-tuple of [from, to] timestamps
   *         - privateStateForNewWindow (default: "non-private"): when clearing
   *           open windows, defines the private state for the newly opened window.
   * @returns {object} An object containing debug information about the
   *          sanitization progress. This state object is also used as
   *          AsyncShutdown metadata.
   */
  async sanitize(itemsToClear = null, options = {}) {
    let progress = options.progress;
    // initialise the principals collector
    gPrincipalsCollector = new lazy.PrincipalsCollector();
    if (!progress) {
      progress = options.progress = {};
    }

    if (!itemsToClear) {
      itemsToClear = getItemsToClearFromPrefBranch(this.PREF_CPD_BRANCH);
    }
    let promise = sanitizeInternal(this.items, itemsToClear, options);

    // Depending on preferences, the sanitizer may perform asynchronous
    // work before it starts cleaning up the Places database (e.g. closing
    // windows). We need to make sure that the connection to that database
    // hasn't been closed by the time we use it.
    // Though, if this is a sanitize on shutdown, we already have a blocker.
    if (!progress.isShutdown) {
      let shutdownClient = lazy.PlacesUtils.history.shutdownClient.jsclient;
      shutdownClient.addBlocker("sanitize.js: Sanitize", promise, {
        fetchState: () => ({ progress }),
      });
    }

    try {
      await promise;
    } finally {
      Services.obs.notifyObservers(null, "sanitizer-sanitization-complete");
    }
    return progress;
  },

  observe(subject, topic, data) {
    if (topic == "nsPref:changed") {
      if (
        data.startsWith(this.PREF_SHUTDOWN_BRANCH) &&
        this.shouldSanitizeOnShutdown
      ) {
        // Update the pending shutdown sanitization.
        removePendingSanitization("shutdown");
        let itemsToClear = getItemsToClearFromPrefBranch(
          Sanitizer.PREF_SHUTDOWN_BRANCH
        );
        addPendingSanitization("shutdown", itemsToClear, {});
      } else if (data == this.PREF_SANITIZE_ON_SHUTDOWN) {
        this.shouldSanitizeOnShutdown = Services.prefs.getBoolPref(
          Sanitizer.PREF_SANITIZE_ON_SHUTDOWN,
          false
        );
        removePendingSanitization("shutdown");
        if (this.shouldSanitizeOnShutdown) {
          let itemsToClear = getItemsToClearFromPrefBranch(
            Sanitizer.PREF_SHUTDOWN_BRANCH
          );
          addPendingSanitization("shutdown", itemsToClear, {});
        }
      } else if (data == this.PREF_NEWTAB_SEGREGATION) {
        this.shouldSanitizeNewTabContainer = Services.prefs.getBoolPref(
          this.PREF_NEWTAB_SEGREGATION,
          false
        );
        removePendingSanitization("newtab-container");
        if (this.shouldSanitizeNewTabContainer) {
          addPendingSanitization("newtab-container", [], {});
        }
      }
    }
  },

  QueryInterface: ChromeUtils.generateQI([
    "nsIObserver",
    "nsISupportsWeakReference",
  ]),

  // This method is meant to be used by tests.
  async runSanitizeOnShutdown() {
    // The collector needs to be reset for each test, as the collection only happens
    // once and does not update after that.
    // Pretend that it has never been initialized to mimic the actual browser behavior
    // by setting it to null.
    // The actually initialization will happen either via sanitize() or directly in
    // sanitizeOnShutdown.
    gPrincipalsCollector = null;
    return sanitizeOnShutdown({
      isShutdown: true,
      clearHonoringExceptions: true,
    });
  },

  /**
   * Migrate old sanitize prefs to the new prefs for the new
   * clear history dialog. Does nothing if the migration was completed before
   * based on the pref privacy.sanitize.cpd.hasMigratedToNewPrefs3 or
   * privacy.sanitize.clearOnShutdown.hasMigratedToNewPrefs3
   *
   * @param {string} context - one of "clearOnShutdown" or "cpd", which indicates which
   *      pref branch to migrate prefs from based on the dialog context
   */
  maybeMigratePrefs(context) {
    // We are going to be migrating once more due to a backout in Bug 1894933
    // The new migration prefs have a 2 appended to the context
    if (
      Services.prefs.getBoolPref(
        `privacy.sanitize.${context}.hasMigratedToNewPrefs3`
      )
    ) {
      return;
    }

    let newContext =
      context == "clearOnShutdown" ? "clearOnShutdown_v2" : "clearHistory";

    let formData = Services.prefs.getBoolPref(`privacy.${context}.formdata`);
    // Bug 1888466 lead to splitting the clearhistory v2 history, formdata and downloads pref into history and formdata
    // so we have to now check for both the old pref and the new pref
    let history = Services.prefs.getBoolPref(`privacy.${context}.history`);
    // if the user has migrated to using historyFormDataAndDownloads, then we should follow what
    // the new dialog prefs are set to
    if (
      Services.prefs.getBoolPref(
        `privacy.sanitize.${context}.hasMigratedToNewPrefs2`
      )
    ) {
      let formDataContext =
        context == "cpd" ? "clearHistory" : "clearOnShutdown_v2";
      history = Services.prefs.getBoolPref(
        `privacy.${formDataContext}.historyFormDataAndDownloads`
      );
      formData = history;
    } else {
      // migrate from v1 (old dialog) to v3 (latest version of new dialog)
      // hasMigratedToNewPrefs3 == false and hasMigratedToNewPrefs2 == false

      // Get all the old pref values to migrate to the new ones
      let cookies = Services.prefs.getBoolPref(`privacy.${context}.cookies`);
      let cache = Services.prefs.getBoolPref(`privacy.${context}.cache`);
      let siteSettings = Services.prefs.getBoolPref(
        `privacy.${context}.siteSettings`
      );

      // We set cookiesAndStorage to true if cookies are enabled for clearing
      // regardless of what sessions and offlineApps are set to
      // This is because cookie clearing behaviour takes precedence over sessions and offlineApps clearing.
      Services.prefs.setBoolPref(
        `privacy.${newContext}.cookiesAndStorage`,
        cookies
      );

      // cache, siteSettings and formdata follow the old dialog prefs
      Services.prefs.setBoolPref(`privacy.${newContext}.cache`, cache);

      Services.prefs.setBoolPref(
        `privacy.${newContext}.siteSettings`,
        siteSettings
      );
    }

    // we set browsingHistoryAndDownloads to true if history is enabled for clearing, regardless of what downloads is set to.
    Services.prefs.setBoolPref(
      `privacy.${newContext}.browsingHistoryAndDownloads`,
      history
    );

    Services.prefs.setBoolPref(`privacy.${newContext}.formdata`, formData);

    // We have to remove the old privacy.sanitize.${context}.hasMigratedToNewPrefs (2) pref
    // if the user has them on their system
    Services.prefs.clearUserPref(
      `privacy.sanitize.${context}.hasMigratedToNewPrefs`
    );
    Services.prefs.clearUserPref(
      `privacy.sanitize.${context}.hasMigratedToNewPrefs2`
    );

    Services.prefs.setBoolPref(
      `privacy.sanitize.${context}.hasMigratedToNewPrefs3`,
      true
    );
  },

  // When making any changes to the sanitize implementations here,
  // please check whether the changes are applicable to Android
  // (mobile/shared/modules/geckoview/GeckoViewStorageController.sys.mjs) as well.

  items: {
    cache: {
      async clear(range) {
        let timerId = Glean.browserSanitizer.cache.start();
        await clearData(range, Ci.nsIClearDataService.CLEAR_ALL_CACHES);
        Glean.browserSanitizer.cache.stopAndAccumulate(timerId);
      },
    },

    cookies: {
      async clear(range, { progress }, clearHonoringExceptions) {
        let timerId = Glean.browserSanitizer.cookies.start();
        // This is true if called by sanitizeOnShutdown.
        // On shutdown we clear by principal to be able to honor the users exceptions
        if (clearHonoringExceptions) {
          progress.step = "getAllPrincipals";
          let principalsForShutdownClearing =
            await gPrincipalsCollector.getAllPrincipals(progress);
          await maybeSanitizeSessionPrincipals(
            progress,
            principalsForShutdownClearing,
            Ci.nsIClearDataService.CLEAR_COOKIES |
              Ci.nsIClearDataService.CLEAR_COOKIE_BANNER_EXECUTED_RECORD |
              Ci.nsIClearDataService.CLEAR_FINGERPRINTING_PROTECTION_STATE |
              Ci.nsIClearDataService.CLEAR_BOUNCE_TRACKING_PROTECTION_STATE
          );
        } else {
          // Not on shutdown
          await clearData(
            range,
            Ci.nsIClearDataService.CLEAR_COOKIES |
              Ci.nsIClearDataService.CLEAR_COOKIE_BANNER_EXECUTED_RECORD |
              Ci.nsIClearDataService.CLEAR_FINGERPRINTING_PROTECTION_STATE |
              Ci.nsIClearDataService.CLEAR_BOUNCE_TRACKING_PROTECTION_STATE
          );
        }
        await clearData(range, Ci.nsIClearDataService.CLEAR_MEDIA_DEVICES);
        Glean.browserSanitizer.cookies.stopAndAccumulate(timerId);
      },
    },

    offlineApps: {
      async clear(range, { progress }, clearHonoringExceptions) {
        // This is true if called by sanitizeOnShutdown.
        // On shutdown we clear by principal to be able to honor the users exceptions
        if (clearHonoringExceptions) {
          progress.step = "getAllPrincipals";
          let principalsForShutdownClearing =
            await gPrincipalsCollector.getAllPrincipals(progress);
          await maybeSanitizeSessionPrincipals(
            progress,
            principalsForShutdownClearing,
            Ci.nsIClearDataService.CLEAR_DOM_STORAGES |
              Ci.nsIClearDataService.CLEAR_COOKIE_BANNER_EXECUTED_RECORD |
              Ci.nsIClearDataService.CLEAR_FINGERPRINTING_PROTECTION_STATE
          );
        } else {
          // Not on shutdown
          await clearData(
            range,
            Ci.nsIClearDataService.CLEAR_DOM_STORAGES |
              Ci.nsIClearDataService.CLEAR_COOKIE_BANNER_EXECUTED_RECORD |
              Ci.nsIClearDataService.CLEAR_FINGERPRINTING_PROTECTION_STATE
          );
        }
      },
    },

    history: {
      async clear(range, { progress }) {
        // TODO: This check is needed for the case that this method is invoked directly and not via the sanitizer.sanitize API.
        // This can be removed once bug 1803799 has landed.
        if (!gPrincipalsCollector) {
          gPrincipalsCollector = new lazy.PrincipalsCollector();
        }
        progress.step = "getAllPrincipals";
        let principals = await gPrincipalsCollector.getAllPrincipals(progress);
        let timerId = Glean.browserSanitizer.history.start();
        progress.step = "clearing browsing history";
        await clearData(
          range,
          Ci.nsIClearDataService.CLEAR_HISTORY |
            Ci.nsIClearDataService.CLEAR_CONTENT_BLOCKING_RECORDS
        );

        // storageAccessAPI permissions record every site that the user
        // interacted with and thus mirror history quite closely. It makes
        // sense to clear them when we clear history. However, since their absence
        // indicates that we can purge cookies and site data for tracking origins without
        // user interaction, we need to ensure that we only delete those permissions that
        // do not have any existing storage.
        progress.step = "clearing user interaction";
        await new Promise(resolve => {
          Services.clearData.deleteUserInteractionForClearingHistory(
            principals,
            range ? range[0] : 0,
            resolve
          );
        });
        Glean.browserSanitizer.history.stopAndAccumulate(timerId);
      },
    },

    formdata: {
      async clear(range) {
        let seenException;
        let timerId = Glean.browserSanitizer.formdata.start();
        try {
          // Clear undo history of all search bars.
          for (let currentWindow of Services.wm.getEnumerator(
            "navigator:browser"
          )) {
            let currentDocument = currentWindow.document;

            // searchBar may not exist if it's in the customize mode.
            let searchBar = currentDocument.getElementById("searchbar");
            if (searchBar) {
              let input = searchBar.textbox;
              input.value = "";
              input.editor?.clearUndoRedo();
            }

            let tabBrowser = currentWindow.gBrowser;
            if (!tabBrowser) {
              // No tab browser? This means that it's too early during startup (typically,
              // Session Restore hasn't completed yet). Since we don't have find
              // bars at that stage and since Session Restore will not restore
              // find bars further down during startup, we have nothing to clear.
              continue;
            }
            for (let tab of tabBrowser.tabs) {
              if (tabBrowser.isFindBarInitialized(tab)) {
                tabBrowser.getCachedFindBar(tab).clear();
              }
            }
            // Clear any saved find value
            tabBrowser._lastFindValue = "";
          }
        } catch (ex) {
          seenException = ex;
        }

        try {
          let change = { op: "remove" };
          if (range) {
            [change.firstUsedStart, change.firstUsedEnd] = range;
          }
          await lazy.FormHistory.update(change).catch(e => {
            seenException = new Error("Error " + e.result + ": " + e.message);
          });
        } catch (ex) {
          seenException = ex;
        }

        Glean.browserSanitizer.formdata.stopAndAccumulate(timerId);
        if (seenException) {
          throw seenException;
        }
      },
    },

    downloads: {
      async clear(range) {
        let timerId = Glean.browserSanitizer.downloads.start();
        await clearData(range, Ci.nsIClearDataService.CLEAR_DOWNLOADS);
        Glean.browserSanitizer.downloads.stopAndAccumulate(timerId);
      },
    },

    sessions: {
      async clear(range) {
        let timerId = Glean.browserSanitizer.sessions.start();
        await clearData(
          range,
          Ci.nsIClearDataService.CLEAR_AUTH_TOKENS |
            Ci.nsIClearDataService.CLEAR_AUTH_CACHE
        );
        Glean.browserSanitizer.sessions.stopAndAccumulate(timerId);
      },
    },

    siteSettings: {
      async clear(range) {
        let timerId = Glean.browserSanitizer.sitesettings.start();
        await clearData(
          range,
          Ci.nsIClearDataService.CLEAR_SITE_PERMISSIONS |
            Ci.nsIClearDataService.CLEAR_CONTENT_PREFERENCES |
            Ci.nsIClearDataService.CLEAR_DOM_PUSH_NOTIFICATIONS |
            Ci.nsIClearDataService.CLEAR_CLIENT_AUTH_REMEMBER_SERVICE |
            Ci.nsIClearDataService.CLEAR_CERT_EXCEPTIONS |
            Ci.nsIClearDataService.CLEAR_CREDENTIAL_MANAGER_STATE |
            Ci.nsIClearDataService.CLEAR_COOKIE_BANNER_EXCEPTION |
            Ci.nsIClearDataService.CLEAR_FINGERPRINTING_PROTECTION_STATE
        );
        Glean.browserSanitizer.sitesettings.stopAndAccumulate(timerId);
      },
    },

    openWindows: {
      _canCloseWindow(win) {
        if (win.CanCloseWindow()) {
          // We already showed PermitUnload for the window, so let's
          // make sure we don't do it again when we actually close the
          // window.
          win.skipNextCanClose = true;
          return true;
        }
        return false;
      },
      _resetAllWindowClosures(windowList) {
        for (let win of windowList) {
          win.skipNextCanClose = false;
        }
      },
      async clear(range, { privateStateForNewWindow = "non-private" }) {
        // NB: this closes all *browser* windows, not other windows like the library, about window,
        // browser console, etc.

        // Keep track of the time in case we get stuck in la-la-land because of onbeforeunload
        // dialogs
        let startDate = Date.now();

        // First check if all these windows are OK with being closed:
        let windowList = [];
        for (let someWin of Services.wm.getEnumerator("navigator:browser")) {
          windowList.push(someWin);
          // If someone says "no" to a beforeunload prompt, we abort here:
          if (!this._canCloseWindow(someWin)) {
            this._resetAllWindowClosures(windowList);
            throw new Error(
              "Sanitize could not close windows: cancelled by user"
            );
          }

          // ...however, beforeunload prompts spin the event loop, and so the code here won't get
          // hit until the prompt has been dismissed. If more than 1 minute has elapsed since we
          // started prompting, stop, because the user might not even remember initiating the
          // 'forget', and the timespans will be all wrong by now anyway:
          if (Date.now() > startDate + 60 * 1000) {
            this._resetAllWindowClosures(windowList);
            throw new Error("Sanitize could not close windows: timeout");
          }
        }

        if (!windowList.length) {
          return;
        }

        // If/once we get here, we should actually be able to close all windows.

        let timerId = Glean.browserSanitizer.openwindows.start();

        // First create a new window. We do this first so that on non-mac, we don't
        // accidentally close the app by closing all the windows.
        let handler = Cc["@mozilla.org/browser/clh;1"].getService(
          Ci.nsIBrowserHandler
        );
        let defaultArgs = handler.defaultArgs;
        let features = "chrome,all,dialog=no," + privateStateForNewWindow;
        let newWindow = windowList[0].openDialog(
          AppConstants.BROWSER_CHROME_URL,
          "_blank",
          features,
          defaultArgs
        );

        let onFullScreen = null;
        if (AppConstants.platform == "macosx") {
          onFullScreen = function (e) {
            newWindow.removeEventListener("fullscreen", onFullScreen);
            let docEl = newWindow.document.documentElement;
            let sizemode = docEl.getAttribute("sizemode");
            if (!newWindow.fullScreen && sizemode == "fullscreen") {
              docEl.setAttribute("sizemode", "normal");
              e.preventDefault();
              e.stopPropagation();
              return false;
            }
            return undefined;
          };
          newWindow.addEventListener("fullscreen", onFullScreen);
        }

        let promiseReady = new Promise(resolve => {
          // Window creation and destruction is asynchronous. We need to wait
          // until all existing windows are fully closed, and the new window is
          // fully open, before continuing. Otherwise the rest of the sanitizer
          // could run too early (and miss new cookies being set when a page
          // closes) and/or run too late (and not have a fully-formed window yet
          // in existence). See bug 1088137.
          let newWindowOpened = false;
          let onWindowOpened = function (subject) {
            if (subject != newWindow) {
              return;
            }

            Services.obs.removeObserver(
              onWindowOpened,
              "browser-delayed-startup-finished"
            );
            if (AppConstants.platform == "macosx") {
              newWindow.removeEventListener("fullscreen", onFullScreen);
            }
            newWindowOpened = true;
            // If we're the last thing to happen, invoke callback.
            if (numWindowsClosing == 0) {
              Glean.browserSanitizer.openwindows.stopAndAccumulate(timerId);
              resolve();
            }
          };

          let numWindowsClosing = windowList.length;
          let onWindowClosed = function () {
            numWindowsClosing--;
            if (numWindowsClosing == 0) {
              Services.obs.removeObserver(
                onWindowClosed,
                "xul-window-destroyed"
              );
              // If we're the last thing to happen, invoke callback.
              if (newWindowOpened) {
                Glean.browserSanitizer.openwindows.stopAndAccumulate(timerId);
                resolve();
              }
            }
          };
          Services.obs.addObserver(
            onWindowOpened,
            "browser-delayed-startup-finished"
          );
          Services.obs.addObserver(onWindowClosed, "xul-window-destroyed");
        });

        // Start the process of closing windows
        while (windowList.length) {
          windowList.pop().close();
        }
        newWindow.focus();
        await promiseReady;
      },
    },

    pluginData: {
      async clear() {},
    },

    // Combine History and Form Data clearing for the
    // new clear history dialog box.
    browsingHistoryAndDownloads: {
      async clear(range, { progress }) {
        progress.step = "getAllPrincipals";
        let principals = await gPrincipalsCollector.getAllPrincipals(progress);
        let timerId = Glean.browserSanitizer.history.start();
        progress.step = "clearing browsing history";
        await clearData(
          range,
          Ci.nsIClearDataService.CLEAR_HISTORY |
            Ci.nsIClearDataService.CLEAR_CONTENT_BLOCKING_RECORDS
        );

        // storageAccessAPI permissions record every site that the user
        // interacted with and thus mirror history quite closely. It makes
        // sense to clear them when we clear history. However, since their absence
        // indicates that we can purge cookies and site data for tracking origins without
        // user interaction, we need to ensure that we only delete those permissions that
        // do not have any existing storage.
        progress.step = "clearing user interaction";
        await new Promise(resolve => {
          Services.clearData.deleteUserInteractionForClearingHistory(
            principals,
            range ? range[0] : 0,
            resolve
          );
        });
        Glean.browserSanitizer.history.stopAndAccumulate(timerId);

        // clear Downloads
        timerId = Glean.browserSanitizer.downloads.start();
        await clearData(range, Ci.nsIClearDataService.CLEAR_DOWNLOADS);
        Glean.browserSanitizer.downloads.stopAndAccumulate(timerId);

        await clearChatConversations(range, progress);
      },
    },

    cookiesAndStorage: {
      async clear(range, { progress }, clearHonoringExceptions) {
        let timerId = Glean.browserSanitizer.cookies.start();
        // This is true if called by sanitizeOnShutdown.
        // On shutdown we clear by principal to be able to honor the users exceptions
        if (clearHonoringExceptions) {
          progress.step = "getAllPrincipals";
          let principalsForShutdownClearing =
            await gPrincipalsCollector.getAllPrincipals(progress);
          await maybeSanitizeSessionPrincipals(
            progress,
            principalsForShutdownClearing,
            Ci.nsIClearDataService.CLEAR_COOKIES_AND_SITE_DATA
          );
        } else {
          // Not on shutdown
          await clearData(
            range,
            Ci.nsIClearDataService.CLEAR_COOKIES_AND_SITE_DATA
          );
        }
        await clearData(range, Ci.nsIClearDataService.CLEAR_MEDIA_DEVICES);
        Glean.browserSanitizer.cookies.stopAndAccumulate(timerId);

        await clearChatConversations(range, progress);
      },
    },
  },
};

// Clear chat conversations if AI Window is enabled.
// (ChatStore uses milliseconds.)
async function clearChatConversations(range, progress) {
  if (!lazy.AIWindow.isEnabled) {
    return;
  }
  progress.step = "clearing Smart Window chat conversations";
  try {
    if (range) {
      let startDate = new Date(range[0] / 1000);
      let endDate = new Date(range[1] / 1000);
      await lazy.ChatStore.deleteConversationsByDateRange(startDate, endDate);
    } else {
      await lazy.ChatStore.deleteAllConversations();
    }
  } catch (ex) {
    log("Failed to clear chat conversations", ex);
  }
}

async function sanitizeInternal(items, aItemsToClear, options) {
  let { ignoreTimespan = true, range, progress } = options;
  let seenError = false;
  // Shallow copy the array, as we are going to modify it in place later.
  if (!Array.isArray(aItemsToClear)) {
    throw new Error("Must pass an array of items to clear.");
  }
  let itemsToClear = [...aItemsToClear];

  // Store the list of items to clear, in case we are killed before we
  // get a chance to complete.
  let uid = gPendingSanitizationSerial++;
  // Shutdown sanitization is managed outside.
  if (!progress.isShutdown) {
    addPendingSanitization(uid, itemsToClear, options);
  }

  // Store the list of items to clear, for debugging/forensics purposes
  for (let k of itemsToClear) {
    progress[k] = "ready";
    // Create a progress object specific to each cleaner. We'll pass down this
    // to the cleaners instead of the main progress object, so they don't end
    // up overriding properties each other.
    // This specific progress is deleted if the cleaner completes successfully,
    // so the metadata will only contain progress of unresolved cleaners.
    progress[k + "Progress"] = {};
  }

  // Ensure open windows get cleared first, if they're in our list, so that
  // they don't stick around in the recently closed windows list, and so we
  // can cancel the whole thing if the user selects to keep a window open
  // from a beforeunload prompt.
  let openWindowsIndex = itemsToClear.indexOf("openWindows");
  if (openWindowsIndex != -1) {
    itemsToClear.splice(openWindowsIndex, 1);
    await items.openWindows.clear(
      null,
      Object.assign(options, { progress: progress.openWindowsProgress })
    );
    progress.openWindows = "cleared";
    delete progress.openWindowsProgress;
  }

  // If we ignore timespan, clear everything,
  // otherwise, pick a range.
  if (!ignoreTimespan && !range) {
    range = Sanitizer.getClearRange();
  }

  // For performance reasons we start all the clear tasks at once, then wait
  // for their promises later.
  // Some of the clear() calls may raise exceptions (for example bug 265028),
  // we catch and store them, but continue to sanitize as much as possible.
  // Callers should check returned errors and give user feedback
  // about items that could not be sanitized
  let timerId = Glean.browserSanitizer.total.start();

  let annotateError = (name, ex) => {
    progress[name] = "failed";
    seenError = true;
    console.error("Error sanitizing " + name, ex);
  };

  // Array of objects in form { name, promise }.
  // `name` is the item's name and `promise` may be a promise, if the
  // sanitization is asynchronous, or the function return value, otherwise.
  log("Running sanitization for:", itemsToClear);
  let handles = [];
  for (let name of itemsToClear) {
    progress[name] = "blocking";
    let item = items[name];
    try {
      // Catch errors here, so later we can just loop through these.
      handles.push({
        name,
        promise: item
          .clear(
            range,
            Object.assign(options, { progress: progress[name + "Progress"] }),
            progress.clearHonoringExceptions
          )
          .then(
            () => {
              progress[name] = "cleared";
              delete progress[name + "Progress"];
            },
            ex => annotateError(name, ex)
          ),
      });
    } catch (ex) {
      annotateError(name, ex);
    }
  }
  await Promise.all(handles.map(h => h.promise));

  log("All sanitizations are complete");
  Glean.browserSanitizer.total.stopAndAccumulate(timerId);
  if (!progress.isShutdown) {
    removePendingSanitization(uid);
  }
  progress = {};
  if (seenError) {
    throw new Error("Error sanitizing");
  }
}

async function sanitizeOnShutdown(progress) {
  log("Sanitizing on shutdown");
  // Perform a migration if this is the first time sanitizeOnShutdown is
  // running for the user with the new dialog
  Sanitizer.maybeMigratePrefs("clearOnShutdown");

  progress.sanitizationPrefs = {
    privacy_sanitize_sanitizeOnShutdown: Services.prefs.getBoolPref(
      "privacy.sanitize.sanitizeOnShutdown"
    ),
    privacy_clearOnShutdown_v2_cookiesAndStorage: Services.prefs.getBoolPref(
      "privacy.clearOnShutdown_v2.cookiesAndStorage"
    ),
    privacy_clearOnShutdown_v2_browsingHistoryAndDownloads:
      Services.prefs.getBoolPref(
        "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads"
      ),
    privacy_clearOnShutdown_v2_cache: Services.prefs.getBoolPref(
      "privacy.clearOnShutdown_v2.cache"
    ),
    privacy_clearOnShutdown_v2_formdata: Services.prefs.getBoolPref(
      "privacy.clearOnShutdown_v2.formdata"
    ),
    privacy_clearOnShutdown_v2_siteSettings: Services.prefs.getBoolPref(
      "privacy.clearOnShutdown_v2.siteSettings"
    ),
  };

  let needsSyncSavePrefs = false;
  if (Sanitizer.shouldSanitizeOnShutdown) {
    // Need to sanitize upon shutdown
    progress.advancement = "shutdown-cleaner";

    let itemsToClear = getItemsToClearFromPrefBranch(
      Sanitizer.PREF_SHUTDOWN_BRANCH
    );
    await Sanitizer.sanitize(itemsToClear, { progress });

    // We didn't crash during shutdown sanitization, so annotate it to avoid
    // sanitizing again on startup.
    removePendingSanitization("shutdown");
    needsSyncSavePrefs = true;
  }

  if (Sanitizer.shouldSanitizeNewTabContainer) {
    progress.advancement = "newtab-segregation";
    sanitizeNewTabSegregation();
    removePendingSanitization("newtab-container");
    needsSyncSavePrefs = true;
  }

  if (needsSyncSavePrefs) {
    Services.prefs.savePrefFile(null);
  }

  if (!Sanitizer.shouldSanitizeOnShutdown) {
    // In case the user has not activated sanitizeOnShutdown but has explicitely set exceptions
    // to always clear particular origins, we clear those here

    progress.advancement = "session-permission";

    let exceptions = 0;
    let selectedPrincipals = [];
    // Let's see if we have to forget some particular site.
    for (let permission of Services.perms.all) {
      if (
        permission.type != "cookie" ||
        permission.capability != Ci.nsICookiePermission.ACCESS_SESSION
      ) {
        continue;
      }

      // We consider just permissions set for http, https and file URLs.
      if (!isSupportedPrincipal(permission.principal)) {
        continue;
      }

      log(
        "Custom session cookie permission detected for: " +
          permission.principal.asciiSpec
      );
      exceptions++;

      // We use just the URI here, because permissions ignore OriginAttributes.
      // The principalsCollector is lazy, this is computed only once
      if (!gPrincipalsCollector) {
        gPrincipalsCollector = new lazy.PrincipalsCollector();
      }
      let principals = await gPrincipalsCollector.getAllPrincipals(progress);
      selectedPrincipals.push(
        ...extractMatchingPrincipals(principals, permission.principal.host)
      );
    }
    await maybeSanitizeSessionPrincipals(
      progress,
      selectedPrincipals,
      Ci.nsIClearDataService.CLEAR_ALL_CACHES |
        Ci.nsIClearDataService.CLEAR_COOKIES |
        Ci.nsIClearDataService.CLEAR_DOM_STORAGES |
        Ci.nsIClearDataService.CLEAR_EME |
        Ci.nsIClearDataService.CLEAR_BOUNCE_TRACKING_PROTECTION_STATE
    );
    progress.sanitizationPrefs.session_permission_exceptions = exceptions;
  }

  await cleanupAfterSanitization(Ci.nsIClearDataService.CLEAR_ALL);

  progress.advancement = "done";
}

async function cleanupAfterSanitization(flags) {
  await new Promise(resolve =>
    Services.clearData.cleanupAfterDeletionAtShutdown(flags, resolve)
  );
}

// Extracts the principals matching matchUri as root domain.
function extractMatchingPrincipals(principals, matchHost) {
  return principals.filter(principal => {
    return Services.eTLD.hasRootDomain(matchHost, principal.host);
  });
}

/**
 * This method receives a list of principals and it checks if some of them or
 * some of their sub-domain need to be sanitize.
 *
 * @param {object} progress - Object to keep track of the sanitization progress, prefs and mode
 * @param {nsIPrincipal[]} principals - The principals generated by the PrincipalsCollector
 * @param {int} flags - The cleaning categories that need to be cleaned for the principals.
 * @returns {Promise} - Resolves once the clearing of the principals to be cleared is done
 */
async function maybeSanitizeSessionPrincipals(progress, principals, flags) {
  log("Sanitizing " + principals.length + " principals");

  let promises = [];
  let permissions = new Map();
  Services.perms.getAllWithTypePrefix("cookie").forEach(perm => {
    permissions.set(perm.principal.origin, perm);
  });

  principals.forEach(principal => {
    progress.step = "checking-principal";
    let cookieAllowed = cookiesAllowedForDomainOrSubDomain(
      principal,
      permissions
    );
    progress.step = "principal-checked:" + cookieAllowed;

    if (!cookieAllowed) {
      promises.push(sanitizeSessionPrincipal(progress, principal, flags));
    }
  });

  progress.step = "promises:" + promises.length;
  if (promises.length) {
    await Promise.all(promises);
  }
  progress.step = "promises resolved";
}

function cookiesAllowedForDomainOrSubDomain(principal, permissions) {
  log("Checking principal: " + principal.asciiSpec);

  // If we have the 'cookie' permission for this principal, let's return
  // immediately.
  let cookiePermission = checkIfCookiePermissionIsSet(principal);
  if (cookiePermission != null) {
    return cookiePermission;
  }

  for (let perm of permissions.values()) {
    if (perm.type != "cookie") {
      permissions.delete(perm.principal.origin);
      continue;
    }
    // We consider just permissions set for http, https and file URLs.
    if (!isSupportedPrincipal(perm.principal)) {
      permissions.delete(perm.principal.origin);
      continue;
    }

    // We don't care about scheme, port, and anything else.
    if (Services.eTLD.hasRootDomain(perm.principal.host, principal.host)) {
      log("Cookie check on principal: " + perm.principal.asciiSpec);
      let rootDomainCookiePermission = checkIfCookiePermissionIsSet(
        perm.principal
      );
      if (rootDomainCookiePermission != null) {
        return rootDomainCookiePermission;
      }
    }
  }

  log("Cookie not allowed.");
  return false;
}

/**
 * Checks if a cookie permission is set for a given principal
 *
 * @returns {boolean} - true: cookie permission "ACCESS_ALLOW", false: cookie permission "ACCESS_DENY"/"ACCESS_SESSION"
 * @returns {null} - No cookie permission is set for this principal
 */
function checkIfCookiePermissionIsSet(principal) {
  let p = Services.perms.testPermissionFromPrincipal(principal, "cookie");

  if (p == Ci.nsICookiePermission.ACCESS_ALLOW) {
    log("Cookie allowed!");
    return true;
  }

  if (
    p == Ci.nsICookiePermission.ACCESS_DENY ||
    p == Ci.nsICookiePermission.ACCESS_SESSION
  ) {
    log("Cookie denied or session!");
    return false;
  }
  // This is an old profile with unsupported permission values
  if (p != Ci.nsICookiePermission.ACCESS_DEFAULT) {
    log("Not supported cookie permission: " + p);
    return false;
  }
  return null;
}

async function sanitizeSessionPrincipal(progress, principal, flags) {
  log("Sanitizing principal: " + principal.asciiSpec);

  await new Promise(resolve => {
    progress.sanitizePrincipal = "started";
    Services.clearData.deleteDataFromPrincipal(
      principal,
      true /* user request */,
      flags,
      resolve
    );
  });
  progress.sanitizePrincipal = "completed";
}

function sanitizeNewTabSegregation() {
  let identity = lazy.ContextualIdentityService.getPrivateIdentity(
    "userContextIdInternal.thumbnail"
  );
  if (identity) {
    Services.clearData.deleteDataFromOriginAttributesPattern({
      userContextId: identity.userContextId,
    });
  }
}

/**
 * Gets an array of items to clear from the given pref branch.
 *
 * @param branch The pref branch to fetch.
 * @return Array of items to clear
 */
function getItemsToClearFromPrefBranch(branch) {
  branch = Services.prefs.getBranch(branch);
  return Object.keys(Sanitizer.items).filter(itemName => {
    try {
      return branch.getBoolPref(itemName);
    } catch (ex) {
      return false;
    }
  });
}

/**
 * These functions are used to track pending sanitization on the next startup
 * in case of a crash before a sanitization could happen.
 *
 * @param id A unique id identifying the sanitization
 * @param itemsToClear The items to clear
 * @param options The Sanitize options
 */
function addPendingSanitization(id, itemsToClear, options) {
  let pendingSanitizations = safeGetPendingSanitizations();
  pendingSanitizations.push({ id, itemsToClear, options });
  Services.prefs.setStringPref(
    Sanitizer.PREF_PENDING_SANITIZATIONS,
    JSON.stringify(pendingSanitizations)
  );
}

function removePendingSanitization(id) {
  let pendingSanitizations = safeGetPendingSanitizations();
  let i = pendingSanitizations.findIndex(s => s.id == id);
  let [s] = pendingSanitizations.splice(i, 1);
  Services.prefs.setStringPref(
    Sanitizer.PREF_PENDING_SANITIZATIONS,
    JSON.stringify(pendingSanitizations)
  );
  return s;
}

function getAndClearPendingSanitizations() {
  let pendingSanitizations = safeGetPendingSanitizations();
  if (pendingSanitizations.length) {
    Services.prefs.clearUserPref(Sanitizer.PREF_PENDING_SANITIZATIONS);
  }
  return pendingSanitizations;
}

function safeGetPendingSanitizations() {
  try {
    return JSON.parse(
      Services.prefs.getStringPref(Sanitizer.PREF_PENDING_SANITIZATIONS, "[]")
    );
  } catch (ex) {
    console.error("Invalid JSON value for pending sanitizations: ", ex);
    return [];
  }
}

async function clearData(range, flags) {
  if (range) {
    await new Promise(resolve => {
      Services.clearData.deleteDataInTimeRange(
        range[0],
        range[1],
        true /* user request */,
        flags,
        resolve
      );
    });
  } else {
    await new Promise(resolve => {
      Services.clearData.deleteData(flags, resolve);
    });
  }
}

function isSupportedPrincipal(principal) {
  return ["http", "https", "file"].some(scheme => principal.schemeIs(scheme));
}
