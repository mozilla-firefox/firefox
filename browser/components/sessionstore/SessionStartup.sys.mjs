/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Session Storage and Restoration
 *
 * Overview
 * This service reads user's session file at startup, and makes a determination
 * as to whether the session should be restored. It will restore the session
 * under the circumstances described below.  If the auto-start Private Browsing
 * mode is active, however, the session is never restored.
 *
 * Crash Detection
 * The CrashMonitor is used to check if the final session state was successfully
 * written at shutdown of the last session. If we did not reach
 * 'sessionstore-final-state-write-complete', then it's assumed that the browser
 * has previously crashed and we should restore the session.
 *
 * Forced Restarts
 * In the event that a restart is required due to application update or extension
 * installation, set the browser.sessionstore.resume_session_once pref to true,
 * and the session will be restored the next time the browser starts.
 *
 * Always Resume
 * This service will always resume the session if the integer pref
 * browser.startup.page is set to 3.
 */

/* :::::::: Constants and Helpers ::::::::::::::: */

const lazy = {};
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserUsageTelemetry: "resource:///modules/BrowserUsageTelemetry.sys.mjs",
  CrashMonitor: "resource://gre/modules/CrashMonitor.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SessionFile: "moz-src:///browser/components/sessionstore/SessionFile.sys.mjs",
  StartupPerformance:
    "moz-src:///browser/components/sessionstore/StartupPerformance.sys.mjs",
  sessionStoreLogger:
    "moz-src:///browser/components/sessionstore/SessionLogger.sys.mjs",
});

const STATE_RUNNING_STR = "running";

const TYPE_NO_SESSION = 0;
const TYPE_RECOVER_SESSION = 1;
const TYPE_RESUME_SESSION = 2;
const TYPE_DEFER_SESSION = 3;

// 'browser.startup.page' preference value to resume the previous session.
const BROWSER_STARTUP_RESUME_SESSION = 3;

var gOnceInitializedDeferred = Promise.withResolvers();

/* :::::::: The Service ::::::::::::::: */

export var SessionStartup = {
  NO_SESSION: TYPE_NO_SESSION,
  RECOVER_SESSION: TYPE_RECOVER_SESSION,
  RESUME_SESSION: TYPE_RESUME_SESSION,
  DEFER_SESSION: TYPE_DEFER_SESSION,

  // The state to restore at startup.
  _initialState: null,
  _sessionType: null,
  _initialized: false,

  // Stores whether the previous session crashed.
  _previousSessionCrashed: null,

  _resumeSessionEnabled: null,

  /* ........ Global Event Handlers .............. */

  /**
   * Initialize the component
   */
  init() {
    Services.obs.notifyObservers(null, "sessionstore-init-started");

    if (!AppConstants.DEBUG) {
      lazy.StartupPerformance.init();
    }

    // do not need to initialize anything in auto-started private browsing sessions
    if (lazy.PrivateBrowsingUtils.permanentPrivateBrowsing) {
      this._initialized = true;
      gOnceInitializedDeferred.resolve();
      return;
    }

    if (
      Services.prefs.getBoolPref(
        "browser.sessionstore.resuming_after_os_restart"
      )
    ) {
      lazy.sessionStoreLogger.debug("resuming_after_os_restart");
      if (!Services.appinfo.restartedByOS) {
        // We had set resume_session_once in order to resume after an OS restart,
        // but we aren't automatically started by the OS (or else appinfo.restartedByOS
        // would have been set). Therefore we should clear resume_session_once
        // to avoid forcing a resume for a normal startup.
        Services.prefs.setBoolPref(
          "browser.sessionstore.resume_session_once",
          false
        );
      }
      Services.prefs.setBoolPref(
        "browser.sessionstore.resuming_after_os_restart",
        false
      );
    }

    lazy.SessionFile.read().then(
      result => {
        lazy.sessionStoreLogger.debug(
          `Completed SessionFile.read() with result.origin: ${result.origin}`
        );
        return this._onSessionFileRead(result);
      },
      err => {
        // SessionFile.read catches most expected failures,
        // so a promise rejection here should be logged as an error
        lazy.sessionStoreLogger.error("Failure from _onSessionFileRead", err);
      }
    );
  },

  // Wrap a string as a nsISupports.
  _createSupportsString(data) {
    let string = Cc["@mozilla.org/supports-string;1"].createInstance(
      Ci.nsISupportsString
    );
    string.data = data;
    return string;
  },

  /**
   * Complete initialization once the Session File has been read.
   *
   * @param source The Session State string read from disk.
   * @param parsed The object obtained by parsing |source| as JSON.
   */
  _onSessionFileRead({ source, parsed, noFilesFound }) {
    this._initialized = true;
    const crashReasons = {
      FINAL_STATE_WRITING_INCOMPLETE: "final-state-write-incomplete",
      SESSION_STATE_FLAG_MISSING:
        "session-state-missing-or-running-at-last-write",
    };

    // Let observers modify the state before it is used
    let supportsStateString = this._createSupportsString(source);
    Services.obs.notifyObservers(
      supportsStateString,
      "sessionstore-state-read"
    );
    let stateString = supportsStateString.data;

    if (stateString != source) {
      // The session has been modified by an add-on, reparse.
      lazy.sessionStoreLogger.debug(
        "After sessionstore-state-read, session has been modified"
      );
      try {
        this._initialState = JSON.parse(stateString);
      } catch (ex) {
        // That's not very good, an add-on has rewritten the initial
        // state to something that won't parse.
        lazy.sessionStoreLogger.error(
          "'sessionstore-state-read' observer rewrote the state to something that won't parse",
          ex
        );
      }
    } else {
      // No need to reparse
      this._initialState = parsed;
    }

    if (this._initialState == null) {
      // No valid session found.
      this._sessionType = this.NO_SESSION;
      lazy.sessionStoreLogger.debug("No valid session found");
      Services.obs.notifyObservers(null, "sessionstore-state-finalized");
      gOnceInitializedDeferred.resolve();
      return;
    }

    let initialState = this._initialState;
    Services.tm.idleDispatchToMainThread(() => {
      let pinnedTabCount = initialState.windows.reduce((winAcc, win) => {
        return (
          winAcc +
          win.tabs.reduce((tabAcc, tab) => {
            return tabAcc + (tab.pinned ? 1 : 0);
          }, 0)
        );
      }, 0);
      lazy.sessionStoreLogger.debug(
        `initialState contains ${pinnedTabCount} pinned tabs`
      );

      lazy.BrowserUsageTelemetry.updateMaxTabPinnedCount(pinnedTabCount);
    }, 60000);

    let isAutomaticRestoreEnabled = this.isAutomaticRestoreEnabled();
    lazy.sessionStoreLogger.debug(
      `isAutomaticRestoreEnabled: ${isAutomaticRestoreEnabled}`
    );
    // If this is a normal restore then throw away any previous session.
    if (!isAutomaticRestoreEnabled && this._initialState) {
      lazy.sessionStoreLogger.debug(
        "Discarding previous session as we have initialState"
      );
      delete this._initialState.lastSessionState;
    }

    let previousSessionCrashedReason = "N/A";
    lazy.CrashMonitor.previousCheckpoints.then(checkpoints => {
      if (checkpoints) {
        // If the previous session finished writing the final state, we'll
        // assume there was no crash.
        this._previousSessionCrashed =
          !checkpoints["sessionstore-final-state-write-complete"];
        if (!checkpoints["sessionstore-final-state-write-complete"]) {
          previousSessionCrashedReason =
            crashReasons.FINAL_STATE_WRITING_INCOMPLETE;
        }
      } else if (noFilesFound) {
        // If the Crash Monitor could not load a checkpoints file it will
        // provide null. This could occur on the first run after updating to
        // a version including the Crash Monitor, or if the checkpoints file
        // was removed, or on first startup with this profile, or after Firefox Reset.

        // There was no checkpoints file and no sessionstore.js or its backups,
        // so we will assume that this was a fresh profile.
        this._previousSessionCrashed = false;
      } else {
        // If this is the first run after an update, sessionstore.js should
        // still contain the session.state flag to indicate if the session
        // crashed. If it is not present, we will assume this was not the first
        // run after update and the checkpoints file was somehow corrupted or
        // removed by a crash.
        //
        // If the session.state flag is present, we will fallback to using it
        // for crash detection - If the last write of sessionstore.js had it
        // set to "running", we crashed.
        let stateFlagPresent =
          this._initialState.session && this._initialState.session.state;

        this._previousSessionCrashed =
          !stateFlagPresent ||
          this._initialState.session.state == STATE_RUNNING_STR;
        if (
          !stateFlagPresent ||
          this._initialState.session.state == STATE_RUNNING_STR
        ) {
          previousSessionCrashedReason =
            crashReasons.SESSION_STATE_FLAG_MISSING;
        }
      }

      // Report shutdown success via telemetry. Shortcoming here are
      // being-killed-by-OS-shutdown-logic, shutdown freezing after
      // session restore was written, etc.
      Glean.sessionRestore.shutdownOk[
        this._previousSessionCrashed ? "false" : "true"
      ].add();
      Glean.sessionRestore.shutdownSuccessSessionStartup.record({
        shutdown_ok: this._previousSessionCrashed.toString(),
        shutdown_reason: previousSessionCrashedReason,
      });
      lazy.sessionStoreLogger.debug(
        `Previous shutdown ok? ${this._previousSessionCrashed}, reason: ${previousSessionCrashedReason}`
      );

      Services.obs.addObserver(this, "sessionstore-windows-restored", true);

      if (this.sessionType == this.NO_SESSION) {
        lazy.sessionStoreLogger.debug("Will restore no session");
        this._initialState = null; // Reset the state.
      } else {
        Services.obs.addObserver(this, "browser:purge-session-history", true);
      }

      // We're ready. Notify everyone else.
      Services.obs.notifyObservers(null, "sessionstore-state-finalized");

      gOnceInitializedDeferred.resolve();
    });
  },

  /**
   * Handle notifications
   */
  observe(subject, topic) {
    switch (topic) {
      case "sessionstore-windows-restored":
        Services.obs.removeObserver(this, "sessionstore-windows-restored");
        lazy.sessionStoreLogger.debug(`sessionstore-windows-restored`);
        // Free _initialState after nsSessionStore is done with it.
        this._initialState = null;
        this._didRestore = true;
        break;
      case "browser:purge-session-history":
        Services.obs.removeObserver(this, "browser:purge-session-history");
        // Reset all state on sanitization.
        this._sessionType = this.NO_SESSION;
        break;
    }
  },

  /* ........ Public API ................*/

  get onceInitialized() {
    return gOnceInitializedDeferred.promise;
  },

  /**
   * Get the session state as a jsval
   */
  get state() {
    return this._initialState;
  },

  /**
   * Determines whether automatic session restoration is enabled for this
   * launch of the browser. This does not include crash restoration. In
   * particular, if session restore is configured to restore only in case of
   * crash, this method returns false.
   *
   * @returns bool
   */
  isAutomaticRestoreEnabled() {
    if (this._resumeSessionEnabled === null) {
      this._resumeSessionEnabled =
        !lazy.PrivateBrowsingUtils.permanentPrivateBrowsing &&
        (Services.prefs.getBoolPref(
          "browser.sessionstore.resume_session_once"
        ) ||
          Services.prefs.getIntPref("browser.startup.page") ==
            BROWSER_STARTUP_RESUME_SESSION);
    }

    return this._resumeSessionEnabled;
  },

  /**
   * Determines whether there is a pending session restore.
   *
   * @returns bool
   */
  willRestore() {
    return (
      this.sessionType == this.RECOVER_SESSION ||
      this.sessionType == this.RESUME_SESSION
    );
  },

  /**
   * Determines whether there is a pending session restore and if that will refer
   * back to a crash.
   *
   * @returns bool
   */
  willRestoreAsCrashed() {
    return this.sessionType == this.RECOVER_SESSION;
  },

  /**
   * Returns a boolean or a promise that resolves to a boolean, indicating
   * whether we will restore a session that ends up replacing the homepage.
   * True guarantees that we'll restore a session; false means that we
   * /probably/ won't do so.
   * The browser uses this to avoid unnecessarily loading the homepage when
   * restoring a session.
   */
  get willOverrideHomepage() {
    // If the session file hasn't been read yet and resuming the session isn't
    // enabled via prefs, go ahead and load the homepage. We may still replace
    // it when recovering from a crash, which we'll only know after reading the
    // session file, but waiting for that would delay loading the homepage in
    // the non-crash case.
    if (!this._initialState && !this.isAutomaticRestoreEnabled()) {
      return false;
    }
    // If we've already restored the session, we won't override again.
    if (this._didRestore) {
      return false;
    }

    return new Promise(resolve => {
      this.onceInitialized.then(() => {
        // If there are valid windows with not only pinned tabs, signal that we
        // will override the default homepage by restoring a session.
        resolve(
          this.willRestore() &&
            this._initialState &&
            this._initialState.windows &&
            (!this.willRestoreAsCrashed()
              ? this._initialState.windows.filter(w => !w._maybeDontRestoreTabs)
              : this._initialState.windows
            ).some(w => w.tabs.some(t => !t.pinned))
        );
      });
    });
  },

  /**
   * Get the type of pending session store, if any.
   */
  get sessionType() {
    if (this._sessionType === null) {
      let resumeFromCrash = Services.prefs.getBoolPref(
        "browser.sessionstore.resume_from_crash"
      );
      // Set the startup type.
      if (this.isAutomaticRestoreEnabled()) {
        this._sessionType = this.RESUME_SESSION;
      } else if (this._previousSessionCrashed && resumeFromCrash) {
        this._sessionType = this.RECOVER_SESSION;
      } else if (this._initialState) {
        this._sessionType = this.DEFER_SESSION;
      } else {
        this._sessionType = this.NO_SESSION;
      }
    }

    return this._sessionType;
  },

  /**
   * Get whether the previous session crashed.
   */
  get previousSessionCrashed() {
    return this._previousSessionCrashed;
  },

  resetForTest() {
    this._resumeSessionEnabled = null;
    this._sessionType = null;
  },

  QueryInterface: ChromeUtils.generateQI([
    "nsIObserver",
    "nsISupportsWeakReference",
  ]),
};
