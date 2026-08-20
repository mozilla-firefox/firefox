/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  CrashReports: "moz-src:///toolkit/crashreporter/CrashReports.sys.mjs",
  CrashSubmit: "moz-src:///toolkit/crashreporter/CrashSubmit.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  RemoteSettingsCrashPull:
    "resource://gre/modules/RemoteSettingsCrashPull.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

// We don't process crash reports older than 28 days, so don't bother
// submitting them
const PENDING_CRASH_REPORT_DAYS = 28;
const DAY = 24 * 60 * 60 * 1000; // milliseconds
const DAYS_TO_SUPPRESS = 30;
const MAX_UNSEEN_CRASHED_CHILD_IDS = 20;
const MAX_UNSEEN_CRASHED_SUBFRAME_IDS = 10;

// Make sure that we will not prompt the user more than once a week
const SILENCE_FOR_DAYS_IN_S = 7 * 86400;

// Time after which we will begin scanning for unsubmitted crash reports
const CHECK_FOR_UNSUBMITTED_CRASH_REPORTS_DELAY_MS = 60 * 10000; // 10 minutes

// Time after which CrashFileCleaner runs its periodic cleanup pass.
const CRASH_FILE_CLEANUP_DELAY_MS = (23 + 5 * 60) * 1000; // A little past 5mn
const CLEANUP_INTERVAL_DAYS = 7;
const INSTALL_TIME_MAX_AGE_MS = 90 * DAY; // ~3 months
const REPORT_MAX_AGE_MS = 180 * DAY; // ~6 months
const MAX_PENDING_REPORTS = 30;

// This is SIGUSR1 and indicates a user-invoked crash
const EXIT_CODE_CONTENT_CRASHED = 245;

const TABCRASHED_ICON_URI = "chrome://browser/skin/tab-crashed.svg";

const SUBFRAMECRASH_LEARNMORE_URI =
  "https://support.mozilla.org/kb/firefox-crashes-troubleshoot-prevent-and-get-help";

/**
 * BrowserWeakMap is exactly like a WeakMap, but expects <xul:browser>
 * objects only.
 *
 * Under the hood, BrowserWeakMap keys the map off of the <xul:browser>
 * permanentKey. If, however, the browser has never gotten a permanentKey,
 * it falls back to keying on the <xul:browser> element itself.
 */
class BrowserWeakMap extends WeakMap {
  get(browser) {
    if (browser.permanentKey) {
      return super.get(browser.permanentKey);
    }
    return super.get(browser);
  }

  set(browser, value) {
    if (browser.permanentKey) {
      return super.set(browser.permanentKey, value);
    }
    return super.set(browser, value);
  }

  delete(browser) {
    if (browser.permanentKey) {
      return super.delete(browser.permanentKey);
    }
    return super.delete(browser);
  }
}

export var TabCrashHandler = {
  _crashedTabCount: 0,
  childMap: new Map(),
  browserMap: new BrowserWeakMap(),
  notificationsMap: new Map(),
  unseenCrashedChildIDs: [],
  pendingSubFrameCrashes: new Map(),
  pendingSubFrameCrashesIDs: [],
  crashedBrowserQueues: new Map(),
  restartRequiredBrowsers: new WeakSet(),
  testBuildIDMismatch: false,

  get prefs() {
    delete this.prefs;
    return (this.prefs = Services.prefs.getBranch(
      "browser.tabs.crashReporting."
    ));
  },

  init() {
    if (this.initialized) {
      return;
    }
    this.initialized = true;

    Services.obs.addObserver(this, "ipc:content-shutdown");
    Services.obs.addObserver(this, "oop-frameloader-crashed");
  },

  observe(aSubject, aTopic) {
    switch (aTopic) {
      case "ipc:content-shutdown": {
        aSubject.QueryInterface(Ci.nsIPropertyBag2);

        if (!aSubject.get("abnormal")) {
          return;
        }

        let childID = aSubject.get("childID");
        let dumpID = aSubject.get("dumpID");

        // Get and remove the subframe crash info first.
        let subframeCrashItem = this.getAndRemoveSubframeCrash(childID);

        if (!dumpID) {
          Glean.browserContentCrash.dumpUnavailable.add(1);
        } else if (AppConstants.MOZ_CRASHREPORTER) {
          this.childMap.set(childID, dumpID);

          // If this is a subframe crash, show the crash notification. Only
          // show subframe notifications when there is a minidump available.
          if (subframeCrashItem) {
            let browsers =
              ChromeUtils.nondeterministicGetWeakMapKeys(subframeCrashItem) ||
              [];
            for (let browserItem of browsers) {
              let browser = subframeCrashItem.get(browserItem);
              if (browser.isConnected && !browser.documentGlobal.closed) {
                this.showSubFrameNotification(browser, childID, dumpID);
              }
            }
          }
        }

        if (!this.flushCrashedBrowserQueue(childID)) {
          this.unseenCrashedChildIDs.push(childID);
          // The elements in unseenCrashedChildIDs will only be removed if
          // the tab crash page is shown. However, ipc:content-shutdown might
          // be fired for processes for which we'll never show the tab crash
          // page - for example, the thumbnailing process. Another case to
          // consider is if the user is configured to submit backlogged crash
          // reports automatically, and a background tab crashes. In that case,
          // we will never show the tab crash page, and never remove the element
          // from the list.
          //
          // Instead of trying to account for all of those cases, we prevent
          // this list from getting too large by putting a reasonable upper
          // limit on how many childIDs we track. It's unlikely that this
          // array would ever get so large as to be unwieldy (that'd be a lot
          // or crashes!), but a leak is a leak.
          if (
            this.unseenCrashedChildIDs.length > MAX_UNSEEN_CRASHED_CHILD_IDS
          ) {
            this.unseenCrashedChildIDs.shift();
          }
        }

        // check for environment affecting crash reporting
        let shutdown = Services.env.exists("MOZ_CRASHREPORTER_SHUTDOWN");

        if (shutdown) {
          dump(
            "A content process crashed and MOZ_CRASHREPORTER_SHUTDOWN is " +
              "set, shutting down\n"
          );
          Services.startup.quit(
            Ci.nsIAppStartup.eForceQuit,
            EXIT_CODE_CONTENT_CRASHED
          );
        }

        break;
      }
      case "oop-frameloader-crashed": {
        let browser = aSubject.ownerElement;
        if (!browser) {
          return;
        }

        this.browserMap.set(browser, aSubject.childID);
        break;
      }
    }
  },

  /**
   * This should be called once a content process has finished
   * shutting down abnormally. Any tabbrowser browsers that were
   * selected at the time of the crash will then be sent to
   * the crashed tab page.
   *
   * @param childID (int)
   *        The childID of the content process that just crashed.
   * @returns boolean
   *        True if one or more browsers were sent to the tab crashed
   *        page.
   */
  flushCrashedBrowserQueue(childID) {
    let browserQueue = this.crashedBrowserQueues.get(childID);
    if (!browserQueue) {
      return false;
    }

    this.crashedBrowserQueues.delete(childID);

    let sentBrowser = false;
    for (let weakBrowser of browserQueue) {
      let browser = weakBrowser.get();
      if (browser) {
        if (
          this.restartRequiredBrowsers.has(browser) ||
          this.testBuildIDMismatch
        ) {
          this.sendToRestartRequiredPage(browser);
        } else {
          this.sendToTabCrashedPage(browser);
        }
        sentBrowser = true;
      }
    }

    return sentBrowser;
  },

  /**
   * Called by a tabbrowser when it notices that its selected browser
   * has crashed. This will queue the browser to show the tab crash
   * page once the content process has finished tearing down.
   *
   * @param browser (<xul:browser>)
   *        The selected browser that just crashed.
   * @param restartRequired (bool)
   *        Whether or not a browser restart is required to recover.
   */
  onSelectedBrowserCrash(browser, restartRequired) {
    if (!browser.isRemoteBrowser) {
      console.error("Selected crashed browser is not remote.");
      return;
    }
    if (!browser.frameLoader) {
      console.error("Selected crashed browser has no frameloader.");
      return;
    }

    let childID = browser.frameLoader.childID;

    let browserQueue = this.crashedBrowserQueues.get(childID);
    if (!browserQueue) {
      browserQueue = [];
      this.crashedBrowserQueues.set(childID, browserQueue);
    }
    // It's probably unnecessary to store this browser as a
    // weak reference, since the content process should complete
    // its teardown in the same tick of the event loop, and then
    // this queue will be flushed. The weak reference is to avoid
    // leaking browsers in case anything goes wrong during this
    // teardown process.
    browserQueue.push(Cu.getWeakReference(browser));

    if (restartRequired) {
      this.restartRequiredBrowsers.add(browser);
    }

    // In the event that the content process failed to launch, then
    // the childID will be 0. In that case, we will never receive
    // a dumpID nor an ipc:content-shutdown observer notification,
    // so we should flush the queue for childID 0 immediately.
    if (childID == 0) {
      this.flushCrashedBrowserQueue(0);
    }
  },

  /**
   * Called by a tabbrowser when it notices that a background browser
   * has crashed. This will flip its remoteness to non-remote, and attempt
   * to revive the crashed tab so that upon selection the tab either shows
   * an error page, or automatically restores.
   *
   * @param browser (<xul:browser>)
   *        The background browser that just crashed.
   * @param restartRequired (bool)
   *        Whether or not a browser restart is required to recover.
   */
  onBackgroundBrowserCrash(browser, restartRequired) {
    if (restartRequired) {
      this.restartRequiredBrowsers.add(browser);
    }

    let gBrowser = browser.getTabBrowser();
    let tab = gBrowser.getTabForBrowser(browser);

    gBrowser.updateBrowserRemoteness(browser, {
      remoteType: lazy.E10SUtils.NOT_REMOTE,
    });

    lazy.SessionStore.reviveCrashedTab(tab);
  },

  /**
   * Called when a subframe crashes. If the dump is available, shows a subframe
   * crashed notification, otherwise waits for one to be available.
   *
   * @param browser (<xul:browser>)
   *        The browser containing the frame that just crashed.
   * @param childId
   *        The id of the process that just crashed.
   */
  async onSubFrameCrash(browser, childID) {
    if (!AppConstants.MOZ_CRASHREPORTER) {
      return;
    }

    // If a crash dump is available, use it. Otherwise, add the child id to the pending
    // subframe crashes list, and wait for the crash "ipc:content-shutdown" notification
    // to get the minidump. If it never arrives, don't show the notification.
    let dumpID = this.childMap.get(childID);
    if (dumpID) {
      this.showSubFrameNotification(browser, childID, dumpID);
    } else {
      let item = this.pendingSubFrameCrashes.get(childID);
      if (!item) {
        item = new BrowserWeakMap();
        this.pendingSubFrameCrashes.set(childID, item);

        // Add the childID to an array that only has room for MAX_UNSEEN_CRASHED_SUBFRAME_IDS
        // items. If there is no more room, pop the oldest off and remove it. This technique
        // is used instead of a timeout.
        if (
          this.pendingSubFrameCrashesIDs.length >=
          MAX_UNSEEN_CRASHED_SUBFRAME_IDS
        ) {
          let idToDelete = this.pendingSubFrameCrashesIDs.shift();
          this.pendingSubFrameCrashes.delete(idToDelete);
        }
        this.pendingSubFrameCrashesIDs.push(childID);
      }
      item.set(browser, browser);
    }
  },

  /**
   * Given a childID, retrieve the subframe crash info for it
   * from the pendingSubFrameCrashes map. The data is removed
   * from the map and returned.
   *
   * @param childID number
   *        childID of the content that crashed.
   * @returns subframe crash info added by previous call to onSubFrameCrash.
   */
  getAndRemoveSubframeCrash(childID) {
    let item = this.pendingSubFrameCrashes.get(childID);
    if (item) {
      this.pendingSubFrameCrashes.delete(childID);
      let idx = this.pendingSubFrameCrashesIDs.indexOf(childID);
      if (idx >= 0) {
        this.pendingSubFrameCrashesIDs.splice(idx, 1);
      }
    }

    return item;
  },

  /**
   * Called to indicate that a subframe within a browser has crashed. A notification
   * bar will be shown.
   *
   * @param browser (<xul:browser>)
   *        The browser containing the frame that just crashed.
   * @param childId
   *        The id of the process that just crashed.
   * @param dumpID
   *        Minidump id of the crash.
   */
  async showSubFrameNotification(browser, childID, dumpID) {
    let gBrowser = browser.getTabBrowser();
    let notificationBox = gBrowser.getNotificationBox(browser);

    const value = "subframe-crashed";
    let notification = notificationBox.getNotificationWithValue(value);
    if (notification) {
      // Don't show multiple notifications for a browser.
      return;
    }

    let closeAllNotifications = () => {
      // Close all other notifications on other tabs that might
      // be open for the same crashed process.
      let existingItem = this.notificationsMap.get(childID);
      if (existingItem) {
        for (let notif of existingItem.slice()) {
          notif.close();
        }
      }
    };

    gBrowser.documentGlobal.MozXULElement.insertFTLIfNeeded(
      "browser/contentCrash.ftl"
    );

    let buttons = [
      {
        "l10n-id": "crashed-subframe-learnmore-link",
        popup: null,
        link: SUBFRAMECRASH_LEARNMORE_URI,
      },
      {
        "l10n-id": "crashed-subframe-submit",
        popup: null,
        callback: async () => {
          if (dumpID) {
            UnsubmittedCrashHandler.submitReports(
              [dumpID],
              lazy.CrashSubmit.SUBMITTED_FROM_CRASH_TAB
            );
          }
          closeAllNotifications();
        },
      },
    ];

    notification = await notificationBox.appendNotification(
      value,
      {
        label: { "l10n-id": "crashed-subframe-message" },
        image: TABCRASHED_ICON_URI,
        priority: notificationBox.PRIORITY_INFO_MEDIUM,
        eventCallback: eventName => {
          if (eventName == "disconnected") {
            let existingItem = this.notificationsMap.get(childID);
            if (existingItem) {
              let idx = existingItem.indexOf(notification);
              if (idx >= 0) {
                existingItem.splice(idx, 1);
              }

              if (!existingItem.length) {
                this.notificationsMap.delete(childID);
              }
            }
          } else if (eventName == "dismissed") {
            if (dumpID) {
              lazy.CrashSubmit.ignore(dumpID);
              this.childMap.delete(childID);
            }

            closeAllNotifications();
          }
        },
      },
      buttons
    );

    let existingItem = this.notificationsMap.get(childID);
    if (existingItem) {
      existingItem.push(notification);
    } else {
      this.notificationsMap.set(childID, [notification]);
    }
  },

  /**
   * This method is exposed for SessionStore to call if the user selects
   * a tab which will restore on demand. It's possible that the tab
   * is in this state because it recently crashed. If that's the case, then
   * it's also possible that the user has not seen the tab crash page for
   * that particular crash, in which case, we might show it to them instead
   * of restoring the tab.
   *
   * @param browser (<xul:browser>)
   *        A browser from a browser tab that the user has just selected
   *        to restore on demand.
   * @returns (boolean)
   *        True if TabCrashHandler will send the user to the tab crash
   *        page instead.
   */
  willShowCrashedTab(browser) {
    let childID = this.browserMap.get(browser);
    // We will only show the tab crash page if:
    // 1) We are aware that this browser crashed
    // 2) We know we've never shown the tab crash page for the
    //    crash yet
    // 3) The user is not configured to automatically submit backlogged
    //    crash reports. If they are, we'll send the crash report
    //    immediately.
    if (childID && this.unseenCrashedChildIDs.includes(childID)) {
      if (UnsubmittedCrashHandler.autoSubmit) {
        let dumpID = this.childMap.get(childID);
        if (dumpID) {
          UnsubmittedCrashHandler.submitReports(
            [dumpID],
            lazy.CrashSubmit.SUBMITTED_FROM_AUTO
          );
        }
      } else {
        this.sendToTabCrashedPage(browser);
        return true;
      }
    } else if (childID === 0) {
      if (this.restartRequiredBrowsers.has(browser)) {
        this.sendToRestartRequiredPage(browser);
      } else {
        this.sendToTabCrashedPage(browser);
      }
      return true;
    }

    return false;
  },

  sendToRestartRequiredPage(browser) {
    let uri = browser.currentURI;
    let gBrowser = browser.getTabBrowser();
    let tab = gBrowser.getTabForBrowser(browser);
    // The restart required page is non-remote by default.
    gBrowser.updateBrowserRemoteness(browser, {
      remoteType: lazy.E10SUtils.NOT_REMOTE,
    });

    browser.docShell.displayLoadError(Cr.NS_ERROR_BUILDID_MISMATCH, uri, null);
    tab.setAttribute("crashed", true);
  },

  /**
   * We show a special page to users when a normal browser tab has crashed.
   * This method should be called to send a browser to that page once the
   * process has completely closed.
   *
   * @param browser (<xul:browser>)
   *        The browser that has recently crashed.
   */
  sendToTabCrashedPage(browser) {
    let title = browser.contentTitle;
    let uri = browser.currentURI;
    let gBrowser = browser.getTabBrowser();
    let tab = gBrowser.getTabForBrowser(browser);
    // The tab crashed page is non-remote by default.
    gBrowser.updateBrowserRemoteness(browser, {
      remoteType: lazy.E10SUtils.NOT_REMOTE,
    });

    browser.setAttribute("crashedPageTitle", title);
    browser.docShell.displayLoadError(Cr.NS_ERROR_CONTENT_CRASHED, uri, null);
    browser.removeAttribute("crashedPageTitle");
    tab.setAttribute("crashed", true);
  },

  /**
   * Submits a crash report from about:tabcrashed, if the crash
   * reporter is enabled and a crash report can be found.
   *
   * @param browser
   *        The <xul:browser> that the report was sent from.
   * @param message
   *        Message data with the following properties:
   *
   *        includeURL (bool):
   *          Whether to include the URL that the user was on
   *          in the crashed tab before the crash occurred.
   *        URL (String)
   *          The URL that the user was on in the crashed tab
   *          before the crash occurred.
   *        comments (String):
   *          Any additional comments from the user.
   *
   *        Note that it is expected that all properties are set,
   *        even if they are empty.
   */
  maybeSendCrashReport(browser, message) {
    if (!AppConstants.MOZ_CRASHREPORTER) {
      return;
    }

    if (!message.data.hasReport) {
      // There was no report, so nothing to do.
      return;
    }

    if (message.data.autoSubmit) {
      // The user has opted in to autosubmitted backlogged
      // crash reports in the future.
      UnsubmittedCrashHandler.autoSubmit = true;
    }

    let childID = this.browserMap.get(browser);
    let dumpID = this.childMap.get(childID);
    if (!dumpID) {
      return;
    }

    if (!message.data.sendReport) {
      Glean.browserContentCrash.notSubmitted.add(1);
      this.prefs.setBoolPref("sendReport", false);
      return;
    }

    // eslint-disable-next-line no-shadow
    let { includeURL, comments, URL } = message.data;

    let extraExtraKeyVals = {
      Comments: comments,
      URL,
    };

    // For the entries in extraExtraKeyVals, we only want to submit the
    // extra data values where they are not the empty string.
    for (let key in extraExtraKeyVals) {
      let val = extraExtraKeyVals[key].trim();
      if (!val) {
        delete extraExtraKeyVals[key];
      }
    }

    // URL is special, since it's already been written to extra data by
    // default. In order to make sure we don't send it, we overwrite it
    // with the empty string.
    if (!includeURL) {
      extraExtraKeyVals.URL = "";
    }

    lazy.CrashSubmit.submit(dumpID, lazy.CrashSubmit.SUBMITTED_FROM_CRASH_TAB, {
      recordSubmission: true,
      extraExtraKeyVals,
    }).catch(console.error);

    this.prefs.setBoolPref("sendReport", true);
    this.prefs.setBoolPref("includeURL", includeURL);

    this.childMap.set(childID, null); // Avoid resubmission.
    this.removeSubmitCheckboxesForSameCrash(childID);
  },

  removeSubmitCheckboxesForSameCrash(childID) {
    for (let window of Services.wm.getEnumerator("navigator:browser")) {
      if (!window.gMultiProcessBrowser) {
        continue;
      }

      for (let browser of window.gBrowser.browsers) {
        if (browser.isRemoteBrowser) {
          continue;
        }

        let doc = browser.contentDocument;
        if (!doc.documentURI.startsWith("about:tabcrashed")) {
          continue;
        }

        if (this.browserMap.get(browser) == childID) {
          this.browserMap.delete(browser);
          browser.sendMessageToActor("CrashReportSent", {}, "AboutTabCrashed");
        }
      }
    }
  },

  /**
   * Process a crashed tab loaded into a browser.
   *
   * @param browser
   *        The <xul:browser> containing the page that crashed.
   * @returns crash data
   *        Message data containing information about the crash.
   */
  onAboutTabCrashedLoad(browser) {
    this._crashedTabCount++;

    let window = browser.documentGlobal;

    // Reset the zoom for the tabcrashed page.
    window.ZoomManager.setZoomForBrowser(browser, 1);

    let childID = this.browserMap.get(browser);
    let index = this.unseenCrashedChildIDs.indexOf(childID);
    if (index != -1) {
      this.unseenCrashedChildIDs.splice(index, 1);
    }

    let dumpID = this.getDumpID(browser);
    if (!dumpID) {
      return {
        hasReport: false,
      };
    }

    let requestAutoSubmit = !UnsubmittedCrashHandler.autoSubmit;
    let sendReport = this.prefs.getBoolPref("sendReport");
    let includeURL = this.prefs.getBoolPref("includeURL");

    let data = {
      hasReport: true,
      sendReport,
      includeURL,
      requestAutoSubmit,
    };

    return data;
  },

  onAboutTabCrashedUnload(browser) {
    if (!this._crashedTabCount) {
      console.error("Can not decrement crashed tab count to below 0");
      return;
    }
    this._crashedTabCount--;

    let childID = this.browserMap.get(browser);

    // Make sure to only count once even if there are multiple windows
    // that will all show about:tabcrashed.
    if (this._crashedTabCount == 0 && childID) {
      Glean.browserContentCrash.notSubmitted.add(1);
    }
  },

  /**
   * For some <xul:browser>, return a crash report dump ID for that browser
   * if we have been informed of one. Otherwise, return null.
   *
   * @param browser (<xul:browser)
   *        The browser to try to get the dump ID for
   * @returns dumpID (String)
   */
  getDumpID(browser) {
    if (!AppConstants.MOZ_CRASHREPORTER) {
      return null;
    }

    return this.childMap.get(this.browserMap.get(browser));
  },

  /**
   * This is intended for TESTING ONLY. It returns the amount of
   * content processes that have crashed such that we're still waiting
   * for dump IDs for their crash reports.
   *
   * For our automated tests, accessing the crashed content process
   * count helps us test the behaviour when content processes crash due
   * to launch failure, since in those cases we should not increase the
   * crashed browser queue (since we never receive dump IDs for launch
   * failures).
   */
  get queuedCrashedBrowsers() {
    return this.crashedBrowserQueues.size;
  },
};

/**
 * This component is responsible for scanning the pending
 * crash report directory for reports, and (if enabled), to
 * prompt the user to submit those reports. It might also
 * submit those reports automatically without prompting if
 * the user has opted in.
 */
export var UnsubmittedCrashHandler = {
  get prefs() {
    delete this.prefs;
    return (this.prefs = Services.prefs.getBranch(
      "browser.crashReports.unsubmittedCheck."
    ));
  },

  get enabled() {
    return this.prefs.getBoolPref("enabled");
  },

  // showingNotification is set to true once a notification
  // is successfully shown, and then set back to false if
  // the notification is dismissed by an action by the user.
  showingNotification: false,
  // suppressed is true if we've determined that we've shown
  // the notification too many times across too many days without
  // user interaction, so we're suppressing the notification for
  // some number of days. See the documentation for
  // shouldShowPendingSubmissionsNotification().
  suppressed: false,

  _checkTimeout: null,

  log: null,

  init() {
    if (this.initialized) {
      return;
    }

    this.initialized = true;

    this.log = console.createInstance({
      prefix: "UnsubmittedCrashHandler",
      maxLogLevel: this.prefs.getStringPref("loglevel", "Error"),
    });

    lazy.RemoteSettingsCrashPull.start(
      this.showRequestedSubmissionsNotification.bind(this)
    );

    // UnsubmittedCrashHandler can be initialized but still be disabled.
    // This is intentional, as this makes simulating UnsubmittedCrashHandler's
    // reactions to browser startup and shutdown easier in test automation.
    //
    // UnsubmittedCrashHandler, when initialized but not enabled, is inert.
    if (this.enabled) {
      if (this.prefs.prefHasUserValue("suppressUntilDate")) {
        if (this.prefs.getCharPref("suppressUntilDate") > this.dateString()) {
          // We'll be suppressing any notifications until after suppressedDate,
          // so there's no need to do anything more.
          this.suppressed = true;
          this.log.debug("suppressing crash handler due to suppressUntilDate");
          return;
        }

        // We're done suppressing, so we don't need this pref anymore.
        this.prefs.clearUserPref("suppressUntilDate");
      }

      Services.obs.addObserver(this, "profile-before-change");
    } else {
      this.log.debug("not enabled");
    }
  },

  uninit() {
    if (!this.initialized) {
      return;
    }

    this.initialized = false;

    this.log = null;

    if (this._checkTimeout) {
      lazy.clearTimeout(this._checkTimeout);
      this._checkTimeout = null;
    }

    lazy.RemoteSettingsCrashPull.stop();

    if (!this.enabled) {
      return;
    }

    if (this.suppressed) {
      this.suppressed = false;
      // No need to do any more clean-up, since we were suppressed.
      return;
    }

    if (this.showingNotification) {
      this.prefs.setBoolPref("shutdownWhileShowing", true);
      this.showingNotification = false;
    }

    Services.obs.removeObserver(this, "profile-before-change");
  },

  observe(subject, topic) {
    switch (topic) {
      case "profile-before-change": {
        this.uninit();
        break;
      }
    }
  },

  scheduleCheckForUnsubmittedCrashReports() {
    this._checkTimeout = lazy.setTimeout(() => {
      Services.tm.idleDispatchToMainThread(() => {
        this.checkForUnsubmittedCrashReports();
      });
    }, CHECK_FOR_UNSUBMITTED_CRASH_REPORTS_DELAY_MS);
  },

  /**
   * Scans the profile directory for unsubmitted crash reports
   * within the past PENDING_CRASH_REPORT_DAYS days. If it
   * finds any, it will, if necessary, attempt to open a notification
   * bar to prompt the user to submit them.
   *
   * @returns Promise
   *          Resolves with the <xul:notification> after it tries to
   *          show a notification on the most recent browser window.
   *          If a notification cannot be shown, will resolve with null.
   */
  async checkForUnsubmittedCrashReports() {
    if (!this.enabled || this.suppressed) {
      return null;
    }

    this.log.debug("checking for unsubmitted crash reports");

    let dateLimit = new Date();
    dateLimit.setDate(dateLimit.getDate() - PENDING_CRASH_REPORT_DAYS);

    let reportIDs = [];
    try {
      reportIDs = await lazy.CrashSubmit.pendingIDs(dateLimit);
    } catch (e) {
      this.log.error(e);
      return null;
    }

    if (reportIDs.length) {
      this.log.debug("found ", reportIDs.length, " unsubmitted crash reports");
      Glean.crashSubmission.pending.add(reportIDs.length);
      if (this.autoSubmit) {
        this.log.debug("auto submitted crash reports");
        this.submitReports(reportIDs, lazy.CrashSubmit.SUBMITTED_FROM_AUTO);
      } else if (this.shouldShowPendingSubmissionsNotification()) {
        return this.showPendingSubmissionsNotification(reportIDs);
      }
    }
    return null;
  },

  /**
   * Returns true if the notification should be shown.
   * shouldShowPendingSubmissionsNotification makes this decision
   * by looking at whether or not the user has seen the notification
   * over several days without ever interacting with it. If this occurs
   * too many times, we suppress the notification for DAYS_TO_SUPPRESS
   * days.
   *
   * @returns bool
   */
  shouldShowPendingSubmissionsNotification() {
    // If user is already presented with a remote settings request, do not show
    if (this._requestedSubmission.notification) {
      return false;
    }

    if (!this.prefs.prefHasUserValue("shutdownWhileShowing")) {
      return true;
    }

    let shutdownWhileShowing = this.prefs.getBoolPref("shutdownWhileShowing");
    this.prefs.clearUserPref("shutdownWhileShowing");

    if (!this.prefs.prefHasUserValue("lastShownDate")) {
      // This isn't expected, but we're being defensive here. We'll
      // opt for showing the notification in this case.
      return true;
    }

    let lastShownDate = this.prefs.getCharPref("lastShownDate");
    if (this.dateString() > lastShownDate && shutdownWhileShowing) {
      // We're on a newer day then when we last showed the
      // notification without closing it. We don't want to do
      // this too many times, so we'll decrement a counter for
      // this situation. Too many of these, and we'll assume the
      // user doesn't know or care about unsubmitted notifications,
      // and we'll suppress the notification for a while.
      let chances = this.prefs.getIntPref("chancesUntilSuppress");
      if (--chances < 0) {
        // We're out of chances!
        this.prefs.clearUserPref("chancesUntilSuppress");
        // We'll suppress for DAYS_TO_SUPPRESS days.
        let suppressUntil = this.dateString(
          new Date(Date.now() + DAY * DAYS_TO_SUPPRESS)
        );
        this.prefs.setCharPref("suppressUntilDate", suppressUntil);
        return false;
      }
      this.prefs.setIntPref("chancesUntilSuppress", chances);
    }

    return true;
  },

  /**
   * Given an array of unsubmitted crash report IDs, try to open
   * up a notification asking the user to submit them.
   *
   * @param reportIDs (Array<string>)
   *        The Array of report IDs to offer the user to send.
   * @returns The <xul:notification> if one is shown. null otherwise.
   */
  async showPendingSubmissionsNotification(reportIDs) {
    if (!reportIDs.length) {
      return null;
    }

    this.log.debug("showing pending submissions notification");

    let notification = await this.show({
      notificationID: "pending-crash-reports",
      reportIDs,
      onAction: () => {
        this.showingNotification = false;
      },
    });

    if (notification) {
      this.showingNotification = true;
      this.prefs.setCharPref("lastShownDate", this.dateString());
    }

    return notification;
  },

  removeExistingNotification(aNotification) {
    if (aNotification) {
      let chromeWin = lazy.BrowserWindowTracker.getTopWindow({
        allowFromInactiveWorkspace: true,
      });
      if (!chromeWin) {
        return false;
      }

      chromeWin.gNotificationBox.removeNotification(aNotification, true);
      aNotification = null;
    }
    return true;
  },

  _requestedSubmission: {
    notification: null,
    reportIDs: [],
  },

  /**
   * Given an array of interesting unsubmitted crash report IDs, try to open
   * up a notification asking the user to submit them.
   *
   * @param newReportIDs (Array<string>)
   *        The Array of report IDs to offer the user to send.
   * @returns The <xul:notification> if one is shown. null otherwise.
   */
  async showRequestedSubmissionsNotification(newReportIDs) {
    if (!newReportIDs.length) {
      return null;
    }

    this.log.debug("showing requested pending notification");

    if (
      !this.removeExistingNotification(this._requestedSubmission.notification)
    ) {
      return null;
    }

    const dontShowBefore = Services.prefs.getIntPref(
      "browser.crashReports.dontShowBefore"
    );
    const now = Math.trunc(Date.now() / 1000);
    if (now < dontShowBefore) {
      return null;
    }

    this._requestedSubmission.reportIDs.push(...newReportIDs);
    this._requestedSubmission.notification = await this.show({
      notificationID: "pending-crash-reports-req",
      reportIDs: this._requestedSubmission.reportIDs,
      onAction: () => {
        this._requestedSubmission.notification = null;
        this._requestedSubmission.reportIDs = [];
        Services.prefs.setIntPref(
          "browser.crashReports.dontShowBefore",
          now + SILENCE_FOR_DAYS_IN_S
        );
      },
      requestedByDevs: true,
    });

    return this._requestedSubmission.notification;
  },

  /**
   * Returns a string representation of a Date in the format
   * YYYYMMDD.
   *
   * @param someDate (Date, optional)
   *        The Date to convert to the string. If not provided,
   *        defaults to today's date.
   * @returns String
   */
  dateString(someDate = new Date()) {
    let year = String(someDate.getFullYear()).padStart(4, "0");
    let month = String(someDate.getMonth() + 1).padStart(2, "0");
    let day = String(someDate.getDate()).padStart(2, "0");
    return year + month + day;
  },

  /**
   * Attempts to show a notification bar to the user in the most
   * recent browser window asking them to submit some crash report
   * IDs. If a notification cannot be shown (for example, there
   * is no browser window), this method exits silently.
   *
   * The notification will allow the user to submit their crash
   * reports. If the user dismissed the notification, the crash
   * reports will be marked to be ignored (though they can
   * still be manually submitted via about:crashes).
   *
   * @param JS Object
   *        An Object with the following properties:
   *
   *        notificationID (string)
   *          The ID for the notification to be opened.
   *
   *        reportIDs (Array<string>)
   *          The array of report IDs to offer to the user.
   *
   *        onAction (function, optional)
   *          A callback to fire once the user performs an
   *          action on the notification bar (this includes
   *          dismissing the notification).
   *
   *        requestedByDevs (boolean)
   *          Does it match a Remote Settings request.
   *          If true, a few impactful differences on the notification:
   *           - message will have a different wording
   *           - will not include the "always send" and "view all" buttons.
   *           - when clicking "send", the crash reports will disable throttling
   *
   * @returns The <xul:notification> if one is shown. null otherwise.
   */
  show({ notificationID, reportIDs, onAction, requestedByDevs }) {
    let chromeWin = lazy.BrowserWindowTracker.getTopWindow({
      allowFromInactiveWorkspace: true,
    });
    if (!chromeWin) {
      // Can't show a notification in this case. We'll hopefully
      // get another opportunity to have the user submit their
      // crash reports later.
      return null;
    }

    let notification =
      chromeWin.gNotificationBox.getNotificationWithValue(notificationID);
    if (notification) {
      return null;
    }

    chromeWin.MozXULElement.insertFTLIfNeeded("browser/contentCrash.ftl");

    const pendingCrashReportsSend = {
      "l10n-id": "pending-crash-reports-send",
      callback: () => {
        this.submitReports(
          reportIDs,
          lazy.CrashSubmit.SUBMITTED_FROM_INFOBAR,
          requestedByDevs ? { noThrottle: true } : {}
        );
        if (onAction) {
          onAction();
        }
      },
    };
    const pendingCrashReportsAlwaysSend = {
      "l10n-id": "pending-crash-reports-always-send",
      callback: () => {
        this.autoSubmit = true;
        this.submitReports(reportIDs, lazy.CrashSubmit.SUBMITTED_FROM_INFOBAR);
        if (onAction) {
          onAction();
        }
      },
    };
    const pendingCrashReportsViewAll = {
      "l10n-id": "pending-crash-reports-view-all",
      callback() {
        chromeWin.openTrustedLinkIn("about:crashes", "tab");
        return true;
      },
    };

    const requestedCrashSupport = {
      supportPage: "unsent-crash-reports-in-firefox",
    };
    const requestedCrashReportsDontShowAgain = {
      "l10n-id": "requested-crash-reports-dont-show-again",
      callback: () => {
        this.requestedNeverShowAgain = true;
        if (onAction) {
          onAction();
        }
      },
    };

    let buttons = [];
    if (requestedByDevs) {
      // Somehow needs to be the first one
      buttons.push(requestedCrashSupport);
    }

    buttons.push(pendingCrashReportsSend);

    if (!requestedByDevs) {
      buttons.push(pendingCrashReportsAlwaysSend, pendingCrashReportsViewAll);
    } else {
      buttons.push(requestedCrashReportsDontShowAgain);
    }

    let eventCallback = eventType => {
      if (eventType == "dismissed") {
        // The user intentionally dismissed the notification,
        // which we interpret as meaning that they don't care
        // to submit the reports. We'll ignore these particular
        // reports going forward.
        reportIDs.forEach(function (reportID) {
          lazy.CrashSubmit.ignore(reportID);
        });
        if (onAction) {
          onAction();
        }
      }
    };

    return chromeWin.gNotificationBox.appendNotification(
      notificationID,
      {
        label: {
          "l10n-id": requestedByDevs
            ? "requested-crash-reports-message-new"
            : "pending-crash-reports-message-new",
          "l10n-args": { reportCount: reportIDs.length },
        },
        image: TABCRASHED_ICON_URI,
        priority: chromeWin.gNotificationBox.PRIORITY_INFO_HIGH,
        eventCallback,
      },
      buttons
    );
  },

  get autoSubmit() {
    return Services.prefs.getBoolPref(
      "browser.crashReports.unsubmittedCheck.autoSubmit2"
    );
  },

  set autoSubmit(val) {
    Services.prefs.setBoolPref(
      "browser.crashReports.unsubmittedCheck.autoSubmit2",
      val
    );
  },

  set requestedNeverShowAgain(val) {
    Services.prefs.setBoolPref(
      "browser.crashReports.requestedNeverShowAgain",
      val
    );
  },

  /**
   * Attempt to submit reports to the crash report server.
   *
   * @param reportIDs (Array<string>)
   *        The array of reportIDs to submit.
   * @param submittedFrom (string)
   *        One of the CrashSubmit.SUBMITTED_FROM_* constants representing
   *        how this crash was submitted.
   * @param params (object)
   *        Parameters to be passed to CrashSubmit.submit(), e.g. 'noThrottle'.
   */
  submitReports(reportIDs, submittedFrom, params) {
    this.log.debug(
      "submitting ",
      reportIDs.length,
      " reports from ",
      submittedFrom
    );
    for (let reportID of reportIDs) {
      lazy.CrashSubmit.submit(reportID, submittedFrom, params).catch(
        this.log.error.bind(this.log)
      );
    }
  },
};

ChromeUtils.defineLazyGetter(lazy, "cleanerPrefs", () =>
  Services.prefs.getBranch("browser.crashReports.cleanupCheck.")
);
ChromeUtils.defineLazyGetter(lazy, "cleanerLog", () =>
  console.createInstance({
    prefix: "CrashFileCleaner",
    maxLogLevel: lazy.cleanerPrefs.getStringPref("loglevel", "Error"),
  })
);
/**
 * `CrashFileCleaner` prunes stale crash-related files from the user's
 * profile on a recurring schedule.
 */
export var CrashFileCleaner = {
  get enabled() {
    return lazy.cleanerPrefs.getBoolPref("enabled");
  },

  _checkTimeout: null,

  init() {
    if (this.initialized) {
      return;
    }

    this.initialized = true;

    if (!this.enabled) {
      lazy.cleanerLog.debug("not enabled");
    }
  },

  uninit() {
    if (!this.initialized) {
      return;
    }

    this.initialized = false;

    if (this._checkTimeout) {
      lazy.clearTimeout(this._checkTimeout);
      this._checkTimeout = null;
    }
  },

  scheduleCleanup() {
    this._checkTimeout = lazy.setTimeout(() => {
      Services.tm.idleDispatchToMainThread(() => {
        this.runCleanup();
      });
    }, CRASH_FILE_CLEANUP_DELAY_MS);
  },

  _ranRecently() {
    if (!lazy.cleanerPrefs.prefHasUserValue("lastDate")) {
      return false;
    }
    const lastDate = Date.parse(lazy.cleanerPrefs.getCharPref("lastDate"));
    if (isNaN(lastDate)) {
      return false;
    }
    return Date.now() - lastDate < CLEANUP_INTERVAL_DAYS * DAY;
  },

  async pruneInstallTimeMarkers() {
    await lazy.CrashReports.pruneInstallTimeFiles(INSTALL_TIME_MAX_AGE_MS);
  },

  async pruneOldReports() {
    const cutoff = Date.now() - REPORT_MAX_AGE_MS;
    for (const report of lazy.CrashReports.getReports()) {
      if (report.date < cutoff) {
        if (report.pending) {
          lazy.CrashReports.deletePendingReport(report.id);
        } else {
          lazy.CrashReports.deleteSubmittedReport(report.id);
        }
      }
    }
  },

  /**
   * Ensure there isn't too many pending reports, removing the oldest first.
   */
  async enforcePendingCap() {
    const pending = lazy.CrashReports.getReports().filter(r => r.pending);
    if (pending.length <= MAX_PENDING_REPORTS) {
      return;
    }
    for (const report of pending.slice(MAX_PENDING_REPORTS)) {
      await lazy.CrashReports.deletePendingReport(report.id);
    }
  },

  async runCleanup() {
    if (!this.enabled) {
      return;
    }
    if (this._ranRecently()) {
      lazy.cleanerLog.debug("cleanup ran recently, skipping");
      return;
    }

    lazy.cleanerLog.debug("running cleanup");

    try {
      await this.pruneInstallTimeMarkers();
      await this.pruneOldReports();
      await this.enforcePendingCap();
      lazy.cleanerPrefs.setCharPref("lastDate", new Date().toISOString());
    } catch (e) {
      lazy.cleanerLog.error(e);
    }
  },
};
