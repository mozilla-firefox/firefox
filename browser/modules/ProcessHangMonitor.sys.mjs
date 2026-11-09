/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Elides the middle of a string by replacing it with an elipsis if it is
 * longer than `threshold` characters. Does its best to not break up grapheme
 * clusters.
 */
function elideMiddleOfString(str, threshold) {
  const searchDistance = 5;
  const stubLength = threshold / 2 - searchDistance;
  if (str.length <= threshold || stubLength < searchDistance) {
    return str;
  }

  function searchElisionPoint(position) {
    let unsplittableCharacter = c => /[\p{M}\uDC00-\uDFFF]/u.test(c);
    for (let i = 0; i < searchDistance; i++) {
      if (!unsplittableCharacter(str[position + i])) {
        return position + i;
      }

      if (!unsplittableCharacter(str[position - i])) {
        return position - i;
      }
    }
    return position;
  }

  let elisionStart = searchElisionPoint(stubLength);
  let elisionEnd = searchElisionPoint(str.length - stubLength);
  if (elisionStart < elisionEnd) {
    str = str.slice(0, elisionStart) + "\u2026" + str.slice(elisionEnd);
  }
  return str;
}

/**
 * This JSM is responsible for observing content process hang reports
 * and asking the user what to do about them. See nsIHangReport for
 * the platform interface.
 */

export var ProcessHangMonitor = {
  /**
   * This timeout is the wait period applied after a user selects "Wait" in
   * an existing notification.
   */
  get WAIT_EXPIRATION_TIME() {
    try {
      return Services.prefs.getIntPref("browser.hangNotification.waitPeriod");
    } catch (ex) {
      return 10000;
    }
  },

  /**
   * Should only be set to true once the quit-application-granted notification
   * has been fired.
   */
  _shuttingDown: false,

  /**
   * Collection of hang reports that haven't expired or been dismissed
   * by the user. These are nsIHangReports. They are mapped to objects
   * containing:
   * - notificationTime: when (ChromeUtils.now()) we first showed a notification
   * - waitCount: how often the user asked to wait for the script to finish
   * - lastReportFromChild: when (ChromeUtils.now()) we last got hang info from the
   *   child.
   */
  _activeReports: new Map(),

  /**
   * Collection of hang reports that have been suppressed for a short
   * period of time. Value is an object like in _activeReports, but also
   * including a `timer` prop, which is an nsITimer for when the wait time
   * expires.
   */
  _pausedReports: new Map(),

  /**
   * Initialize hang reporting. Called once in the parent process.
   */
  init() {
    Services.obs.addObserver(this, "process-hang-report");
    Services.obs.addObserver(this, "clear-hang-report");
    Services.obs.addObserver(this, "quit-application-granted");
    Services.obs.addObserver(this, "xpcom-shutdown");
    Services.ww.registerNotification(this);
  },

  /**
   * Terminate JavaScript associated with the hang being reported for
   * the selected browser in |win|.
   */
  terminateScript(win) {
    this.handleUserInput(win, report => report.terminateScript());
  },

  /**
   * Dismiss the browser notification and invoke an appropriate action based on
   * the hang type.
   */
  stopIt(win) {
    let report = this.findActiveReport(win.gBrowser.selectedBrowser);
    if (!report) {
      return;
    }

    this._recordTelemetryForReport(report, "user-aborted");
    this.terminateScript(win);
  },

  /**
   * Terminate the script causing this report. This is done without
   * updating any report notifications.
   */
  stopHang(report, endReason, backupInfo) {
    this._recordTelemetryForReport(report, endReason, backupInfo);
    report.terminateScript();
  },

  /**
   * Dismiss the notification, clear the report from the active list and set up
   * a new timer to track a wait period during which we won't notify.
   */
  waitLonger(win) {
    let report = this.findActiveReport(win.gBrowser.selectedBrowser);
    if (!report) {
      return;
    }
    // Update the other info we keep.
    let reportInfo = this._activeReports.get(report);
    reportInfo.waitCount++;

    // Remove the report from the active list.
    this.removeActiveReport(report);

    // NOTE, we didn't call userCanceled on nsIHangReport here. This insures
    // we don't repeatedly generate and cache crash report data for this hang
    // in the process hang reporter. It already has one report for the browser
    // process we want it hold onto.

    // Create a new wait timer with notify callback
    let timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    timer.initWithCallback(
      () => {
        for (let [stashedReport, pausedInfo] of this._pausedReports) {
          if (pausedInfo.timer === timer) {
            this.removePausedReport(stashedReport);

            // We're still hung, so move the report back to the active
            // list and update the UI.
            this._activeReports.set(report, pausedInfo);
            this.updateWindows();
            break;
          }
        }
      },
      this.WAIT_EXPIRATION_TIME,
      timer.TYPE_ONE_SHOT
    );

    reportInfo.timer = timer;
    this._pausedReports.set(report, reportInfo);

    // remove the browser notification associated with this hang
    this.updateWindows();
  },

  /**
   * If there is a hang report associated with the selected browser in
   * |win|, invoke |func| on that report and stop notifying the user
   * about it.
   */
  handleUserInput(win, func) {
    let report = this.findActiveReport(win.gBrowser.selectedBrowser);
    if (!report) {
      return null;
    }
    this.removeActiveReport(report);

    return func(report);
  },

  observe(subject, topic) {
    switch (topic) {
      case "xpcom-shutdown": {
        Services.obs.removeObserver(this, "xpcom-shutdown");
        Services.obs.removeObserver(this, "process-hang-report");
        Services.obs.removeObserver(this, "clear-hang-report");
        Services.obs.removeObserver(this, "quit-application-granted");
        Services.ww.unregisterNotification(this);
        break;
      }

      case "quit-application-granted": {
        this.onQuitApplicationGranted();
        break;
      }

      case "process-hang-report": {
        this.reportHang(subject.QueryInterface(Ci.nsIHangReport));
        break;
      }

      case "clear-hang-report": {
        this.clearHang(subject.QueryInterface(Ci.nsIHangReport));
        break;
      }

      case "domwindowopened": {
        // Install event listeners on the new window in case one of
        // its tabs is already hung.
        let win = subject;
        let listener = () => {
          win.removeEventListener("load", listener, true);
          this.updateWindows();
        };
        win.addEventListener("load", listener, true);
        break;
      }

      case "domwindowclosed": {
        let win = subject;
        this.onWindowClosed(win);
        break;
      }
    }
  },

  /**
   * Called early on in the shutdown sequence. We take this opportunity to
   * take any pre-existing hang reports, and terminate them. We also put
   * ourselves in a state so that if any more hang reports show up while
   * we're shutting down, we terminate them immediately.
   */
  onQuitApplicationGranted() {
    this._shuttingDown = true;
    this.stopAllHangs("quit-application-granted");
    this.updateWindows();
  },

  onWindowClosed(win) {
    let maybeStopHang = report => {
      let hungBrowserWindow = null;
      try {
        hungBrowserWindow = report.scriptBrowser.documentGlobal;
      } catch (e) {
        // Ignore failures to get the script browser - we'll be
        // conservative, and assume that if we cannot access the
        // window that belongs to this report that we should stop
        // the hang.
      }
      if (!hungBrowserWindow || hungBrowserWindow == win) {
        this.stopHang(report, "window-closed");
        return true;
      }
      return false;
    };

    // If there are any script hangs for browsers that are in this window
    // that is closing, we can stop them now.
    for (let [report] of this._activeReports) {
      if (maybeStopHang(report)) {
        this._activeReports.delete(report);
      }
    }

    for (let [pausedReport] of this._pausedReports) {
      if (maybeStopHang(pausedReport)) {
        this.removePausedReport(pausedReport);
      }
    }

    this.updateWindows();
  },

  stopAllHangs(endReason) {
    for (let [report] of this._activeReports) {
      this.stopHang(report, endReason);
    }

    this._activeReports = new Map();

    for (let [pausedReport] of this._pausedReports) {
      this.stopHang(pausedReport, endReason);
      this.removePausedReport(pausedReport);
    }
  },

  /**
   * Find a active hang report for the given <browser> element.
   */
  findActiveReport(browser) {
    let frameLoader = browser.frameLoader;
    for (let report of this._activeReports.keys()) {
      if (report.isReportForBrowserOrChildren(frameLoader)) {
        return report;
      }
    }
    return null;
  },

  /**
   * Find a paused hang report for the given <browser> element.
   */
  findPausedReport(browser) {
    let frameLoader = browser.frameLoader;
    for (let [report] of this._pausedReports) {
      if (report.isReportForBrowserOrChildren(frameLoader)) {
        return report;
      }
    }
    return null;
  },

  /**
   * Tell telemetry about the report.
   */
  _recordTelemetryForReport(report, endReason, backupInfo) {
    let info =
      this._activeReports.get(report) ||
      this._pausedReports.get(report) ||
      backupInfo;
    if (!info) {
      return;
    }
    try {
      let uri_type;
      if (report.addonId) {
        uri_type = "extension";
      } else if (report.scriptFileName?.startsWith("debugger")) {
        uri_type = "devtools";
      } else if (
        report.scriptFileName?.startsWith(
          "resource://pdf.js/build/pdf.scripting.mjs"
        )
      ) {
        // PDF scripts are content, but their reported filename is the PDF.js
        // scripting bundle.
        uri_type = "content";
      } else {
        try {
          let url = new URL(report.scriptFileName);
          if (url.protocol == "chrome:" || url.protocol == "resource:") {
            uri_type = "browser";
          } else {
            uri_type = "content";
          }
        } catch (ex) {
          console.error(ex);
          uri_type = "unknown";
        }
      }
      let uptime = 0;
      if (info.notificationTime) {
        uptime = ChromeUtils.now() - info.notificationTime;
      }
      uptime = "" + uptime;
      // We combine the duration of the hang in the content process with the
      // time since we were last told about the hang in the parent. This is
      // not the same as the time we showed a notification, as we only do that
      // for the currently selected browser. It's as messy as it is because
      // there is no cross-process monotonically increasing timestamp we can
      // use. :-(
      let hangDuration =
        report.hangDuration + ChromeUtils.now() - info.lastReportFromChild;
      Glean.slowScriptWarning.shownContent.record({
        end_reason: endReason,
        hang_duration: hangDuration,
        n_tab_deselect: info.deselectCount,
        uri_type,
        uptime,
        wait_count: info.waitCount,
      });
    } catch (ex) {
      console.error(ex);
    }
  },

  /**
   * Remove an active hang report from the active list and cancel the timer
   * associated with it.
   */
  removeActiveReport(report) {
    this._activeReports.delete(report);
    this.updateWindows();
  },

  /**
   * Remove a paused hang report from the paused list and cancel the timer
   * associated with it.
   */
  removePausedReport(report) {
    let info = this._pausedReports.get(report);
    info?.timer?.cancel();
    this._pausedReports.delete(report);
  },

  /**
   * Iterate over all XUL windows and ensure that the proper hang
   * reports are shown for each one. Also install event handlers in
   * each window to watch for events that would cause a different hang
   * report to be displayed.
   */
  updateWindows() {
    let e = Services.wm.getEnumerator("navigator:browser");

    // If it turns out we have no windows (this can happen on macOS),
    // we have no opportunity to ask the user whether or not they want
    // to stop the hang or wait, so we'll opt for stopping the hang.
    if (!e.hasMoreElements()) {
      this.stopAllHangs("no-windows-left");
      return;
    }

    for (let win of e) {
      this.updateWindow(win);

      // Only listen for these events if there are active hang reports.
      if (this._activeReports.size) {
        this.trackWindow(win);
      } else {
        this.untrackWindow(win);
      }
    }
  },

  /**
   * If there is a hang report for the current tab in |win|, display it.
   */
  updateWindow(win) {
    let report = this.findActiveReport(win.gBrowser.selectedBrowser);

    if (report) {
      let info = this._activeReports.get(report);
      if (info && !info.notificationTime) {
        info.notificationTime = ChromeUtils.now();
      }
      this.showNotification(win, report);
    } else {
      this.hideNotification(win);
    }
  },

  /**
   * Show the notification for a hang.
   */
  async showNotification(win, report) {
    let buttons = [
      {
        "l10n-id": "process-hang-stop-button",
        callback() {
          ProcessHangMonitor.stopIt(win);
        },
      },
    ];

    let message;
    let notificationTag;
    if (report.addonId) {
      notificationTag = report.addonId;
      let aps = Cc["@mozilla.org/addons/policy-service;1"].getService(
        Ci.nsIAddonPolicyService
      );

      let addonName;
      try {
        addonName = aps.getExtensionName(report.addonId);
      } catch (e) {
        // The extension may have been unloaded while its script is hung.
        addonName = report.addonId;
      }

      message = {
        "l10n-id": "process-hang-addon-message",
        "l10n-args": { addonName },
      };

      buttons.unshift({
        "l10n-id": "process-hang-addon-learn-more",
        link: "https://support.mozilla.org/kb/warning-unresponsive-script#w_other-causes",
      });
    } else {
      let scriptBrowser = report.scriptBrowser;
      if (scriptBrowser == win.gBrowser?.selectedBrowser) {
        notificationTag = "selected-tab";
        message = { "l10n-id": "process-hang-selected-tab-message" };
      } else {
        let tab =
          scriptBrowser?.documentGlobal.gBrowser?.getTabForBrowser(
            scriptBrowser
          );
        if (!tab) {
          notificationTag = "nonspecific_tab";
          message = { "l10n-id": "process-hang-nonspecific-tab-message" };
        } else {
          notificationTag = scriptBrowser.browserId.toString();
          let title = tab.getAttribute("label");
          title = elideMiddleOfString(title, 60);
          message = {
            "l10n-id": "process-hang-specific-tab-message",
            "l10n-args": { title },
          };
        }
      }
    }

    let notification =
      win.gNotificationBox.getNotificationWithValue("process-hang");
    if (notificationTag == notification?.getAttribute("notification-tag")) {
      return;
    }

    if (notification) {
      notification.label = message;
      notification.setAttribute("notification-tag", notificationTag);
      return;
    }

    // Sometimes the window may have closed already, in which case we won't
    // be able to create a message bar so we need to handle any related errors.
    try {
      let hangNotification = await win.gNotificationBox.appendNotification(
        "process-hang",
        {
          label: message,
          image: "chrome://browser/content/aboutRobots-icon.png",
          priority: win.gNotificationBox.PRIORITY_INFO_HIGH,
          eventCallback: event => {
            if (event == "dismissed") {
              ProcessHangMonitor.waitLonger(win);
            }
          },
        },
        buttons
      );
      hangNotification.setAttribute("notification-tag", notificationTag);
    } catch (err) {
      console.warn(err);
    }
  },

  /**
   * Ensure that no hang notifications are visible in |win|.
   */
  hideNotification(win) {
    let notification =
      win.gNotificationBox.getNotificationWithValue("process-hang");
    if (notification) {
      win.gNotificationBox.removeNotification(notification);
    }
  },

  /**
   * Install event handlers on |win| to watch for events that would
   * cause a different hang report to be displayed.
   */
  trackWindow(win) {
    win.gBrowser.tabContainer.addEventListener("TabSelect", this, true);
    win.gBrowser.tabContainer.addEventListener(
      "TabRemotenessChange",
      this,
      true
    );
  },

  untrackWindow(win) {
    win.gBrowser.tabContainer.removeEventListener("TabSelect", this, true);
    win.gBrowser.tabContainer.removeEventListener(
      "TabRemotenessChange",
      this,
      true
    );
  },

  handleEvent(event) {
    let win = event.target.documentGlobal;

    // If a new tab is selected or if a tab changes remoteness, then
    // we may need to show or hide a hang notification.
    if (event.type == "TabSelect" || event.type == "TabRemotenessChange") {
      if (event.type == "TabSelect" && event.detail.previousTab) {
        // If we've got a notification, check the previous tab's report and
        // indicate the user switched tabs while the notification was up.
        let r =
          this.findActiveReport(event.detail.previousTab.linkedBrowser) ||
          this.findPausedReport(event.detail.previousTab.linkedBrowser);
        if (r) {
          let info = this._activeReports.get(r) || this._pausedReports.get(r);
          info.deselectCount++;
        }
      }
      this.updateWindow(win);
    }
  },

  /**
   * Handle a potentially new hang report. If it hasn't been seen
   * before, show a notification for it in all open XUL windows.
   */
  reportHang(report) {
    let now = ChromeUtils.now();
    if (this._shuttingDown) {
      this.stopHang(report, "shutdown-in-progress", {
        lastReportFromChild: now,
        waitCount: 0,
        deselectCount: 0,
      });
      return;
    }

    // If this hang was already reported reset the timer for it.
    if (this._activeReports.has(report)) {
      this._activeReports.get(report).lastReportFromChild = now;
      // if this report is in active but doesn't have a notification associated
      // with it, display a notification.
      this.updateWindows();
      return;
    }

    // If this hang was already reported and paused by the user ignore it.
    if (this._pausedReports.has(report)) {
      this._pausedReports.get(report).lastReportFromChild = now;
      return;
    }

    // On e10s this counts slow-script notice only once.
    // This code is not reached on non-e10s.
    Glean.dom.slowScriptNoticeCount.add(1);

    this._activeReports.set(report, {
      deselectCount: 0,
      lastReportFromChild: now,
      waitCount: 0,
    });
    this.updateWindows();
  },

  clearHang(report) {
    this._recordTelemetryForReport(report, "cleared");

    this.removeActiveReport(report);
    this.removePausedReport(report);
    report.userCanceled();
  },
};
