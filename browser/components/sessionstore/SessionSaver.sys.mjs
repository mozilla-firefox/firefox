/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  cancelIdleCallback,
  clearTimeout,
  requestIdleCallback,
  setTimeout,
} from "resource://gre/modules/Timer.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

/*
 * Minimal interval between two save operations (in milliseconds).
 *
 * To save system resources, we generally do not save changes immediately when
 * a change is detected. Rather, we wait a little to see if this change is
 * followed by other changes, in which case only the last write is necessary.
 * This delay is defined by "browser.sessionstore.interval".
 *
 * Furthermore, when the user is not actively using the computer, webpages
 * may still perform changes that require (re)writing to sessionstore, e.g.
 * updating Session Cookies or DOM Session Storage, or refreshing, etc. We
 * expect that these changes are much less critical to the user and do not
 * need to be saved as often. In such cases, we increase the delay to
 *  "browser.sessionstore.interval.idle".
 *
 * When the user returns to the computer, if a save is pending, we reschedule
 * it to happen soon, with "browser.sessionstore.interval".
 */
const PREF_INTERVAL_ACTIVE = "browser.sessionstore.interval";
const PREF_INTERVAL_IDLE = "browser.sessionstore.interval.idle";
const PREF_IDLE_DELAY = "browser.sessionstore.idleDelay";

const lazy = XPCOMUtils.declareLazy({
  PrivacyFilter: "resource://gre/modules/sessionstore/PrivacyFilter.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  RunState: "moz-src:///browser/components/sessionstore/RunState.sys.mjs",
  SessionFile: "moz-src:///browser/components/sessionstore/SessionFile.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  sessionStoreLogger:
    "moz-src:///browser/components/sessionstore/SessionLogger.sys.mjs",
  // Minimal interval between two save operations (in ms), while the user is
  // active.
  intervalWhileActive: {
    pref: PREF_INTERVAL_ACTIVE,
    default: 15000 /* 15 seconds */,
    onUpdate: () => {
      // Cancel any pending runs and call runDelayed() with
      // zero to apply the newly configured interval.
      SessionSaver.cancel();
      SessionSaver.runDelayed(0);
    },
  },
  // Minimal interval between two save operations (in ms), while the user is
  // idle.
  intervalWhileIdle: { pref: PREF_INTERVAL_IDLE, default: 3600000 /* 1 h */ },
  // How long before we assume that the user is idle (s).
  idleDelay: {
    pref: PREF_IDLE_DELAY,
    default: 180 /* 3 minutes */,
    onUpdate: (key, previous, latest) => {
      // Update the idle observer for the new `PREF_IDLE_DELAY` value. Here we
      // need to re-fetch the service instead of the original one in use; This
      // is for a case that the Mock service in the unit test needs to be
      // fetched to replace the original one.
      var idleService = Cc["@mozilla.org/widget/useridleservice;1"].getService(
        Ci.nsIUserIdleService
      );
      if (previous != undefined) {
        idleService.removeIdleObserver(SessionSaver, previous);
      }
      if (latest != undefined) {
        idleService.addIdleObserver(SessionSaver, latest);
      }
    },
  },
});

// Notify observers about a given topic with a given subject.
function notify(subject, topic) {
  Services.obs.notifyObservers(subject, topic);
}

/**
 * The external API implemented by the SessionSaver module.
 */
class _SessionSaver {
  /**
   * The timeout ID referencing an active timer for a delayed save. When no
   * save is pending, this is null.
   */
  #timeoutID = null;

  /**
   * The idle callback ID referencing an active idle callback. When no idle
   * callback is pending, this is null.
   */
  #idleCallbackID = null;

  /**
   * A timestamp that keeps track of when we saved the session last. We will
   * this to determine the correct interval between delayed saves to not deceed
   * the configured session write interval.
   */
  #lastSaveTime = 0;

  /**
   * `true` if the user has been idle for at least
   * `browser.sessionstore.idleDelay` seconds. Idleness is computed
   * with `nsIUserIdleService`.
   */
  #isIdle = false;

  /**
   * `true` if the user was idle when we last scheduled a delayed save.
   * See `_isIdle` for details on idleness.
   */
  #wasIdle = false;

  constructor() {
    let idleService = Cc["@mozilla.org/widget/useridleservice;1"].getService(
      Ci.nsIUserIdleService
    );
    idleService.addIdleObserver(this, lazy.idleDelay);
  }

  /**
   * Immediately saves the current session to disk.
   */
  run() {
    if (!lazy.RunState.isRunning) {
      lazy.sessionStoreLogger.debug("SessionSave run called during shutdown");
    }
    return this.#saveState(true /* force-update all windows */);
  }

  /**
   * Saves the current session to disk delayed by a given amount of time. Should
   * another delayed run be scheduled already, we will ignore the given delay
   * and state saving may occur a little earlier.
   *
   * @param delay (optional)
   *        The minimum delay in milliseconds to wait for until we collect and
   *        save the current session.
   */
  runDelayed(delay = 2000) {
    // Bail out if there's a pending run.
    if (this.#timeoutID) {
      return;
    }

    // Interval until the next disk operation is allowed.
    let interval = this.#isIdle
      ? lazy.intervalWhileIdle
      : lazy.intervalWhileActive;
    delay = Math.max(this.#lastSaveTime + interval - Date.now(), delay, 0);

    // Schedule a state save.
    this.#wasIdle = this.#isIdle;
    if (!lazy.RunState.isRunning) {
      lazy.sessionStoreLogger.debug(
        "SessionSaver scheduling a state save during shutdown"
      );
    }
    this.#timeoutID = setTimeout(() => {
      // Execute _saveStateAsync when we have idle time.
      let saveStateAsyncWhenIdle = () => {
        if (!lazy.RunState.isRunning) {
          lazy.sessionStoreLogger.debug(
            "SessionSaver saveStateAsyncWhenIdle callback during shutdown"
          );
        }
        this.#saveStateAsync();
      };

      this.#idleCallbackID = requestIdleCallback(saveStateAsyncWhenIdle);
    }, delay);
  }

  /**
   * Returns the timestamp that keeps track of the last time we attempted to save the session.
   */
  get lastSaveTime() {
    return this.#lastSaveTime;
  }

  /**
   * Sets the last save time to the current time. This will cause us to wait for
   * at least the configured interval when runDelayed() is called next.
   */
  updateLastSaveTime() {
    this.#lastSaveTime = Date.now();
  }

  /**
   * Cancels all pending session saves.
   */
  cancel() {
    clearTimeout(this.#timeoutID);
    this.#timeoutID = null;
    cancelIdleCallback(this.#idleCallbackID);
    this.#idleCallbackID = null;
  }

  /**
   * Observe idle/ active notifications.
   */
  observe(subject, topic) {
    switch (topic) {
      case "idle":
        this.#isIdle = true;
        break;
      case "active":
        this.#isIdle = false;
        if (this.#timeoutID && this.#wasIdle) {
          // A state save has been scheduled while we were idle.
          // Replace it by an active save.
          clearTimeout(this.#timeoutID);
          this.#timeoutID = null;
          this.runDelayed();
        }
        break;
      default:
        throw new Error(`Unexpected change value ${topic}`);
    }
  }

  /**
   * Saves the current session state. Collects data and writes to disk.
   *
   * @param forceUpdateAllWindows (optional)
   *        Forces us to recollect data for all windows and will bypass and
   *        update the corresponding caches.
   */
  #saveState(forceUpdateAllWindows = false) {
    // Cancel any pending timeouts.
    this.cancel();

    if (lazy.PrivateBrowsingUtils.permanentPrivateBrowsing) {
      // Don't save (or even collect) anything in permanent private
      // browsing mode

      this.updateLastSaveTime();
      return Promise.resolve();
    }

    let timerId = Glean.sessionRestore.collectData.start();
    let state = lazy.SessionStore.getCurrentState(forceUpdateAllWindows);
    lazy.PrivacyFilter.filterPrivateWindowsAndTabs(state);

    // Make sure we only write worth saving tabs to disk.
    lazy.SessionStore.keepOnlyWorthSavingTabs(state);

    // Make sure that we keep the previous session if we started with a single
    // private window and no non-private windows have been opened, yet.
    if (state.deferredInitialState) {
      state.windows = state.deferredInitialState.windows || [];
      delete state.deferredInitialState;
    }

    if (AppConstants.platform != "macosx") {
      // We want to restore closed windows that are marked with _shouldRestore.
      // We're doing this here because we want to control this only when saving
      // the file.
      if (lazy.sessionStoreLogger.debugEnabled) {
        lazy.sessionStoreLogger.debug(
          "SessionSaver._saveState, closed windows:"
        );
        for (let closedWin of state._closedWindows) {
          lazy.sessionStoreLogger.debug(
            `\t${closedWin.closedId}\t${closedWin.closedAt}\t${closedWin._shouldRestore}`
          );
        }
      }

      while (state._closedWindows.length) {
        let i = state._closedWindows.length - 1;

        if (!state._closedWindows[i]._shouldRestore) {
          // We only need to go until _shouldRestore
          // is falsy since we're going in reverse.
          break;
        }

        delete state._closedWindows[i]._shouldRestore;
        state.windows.unshift(state._closedWindows.pop());
      }
    }

    // Clear cookies and storage on clean shutdown.
    this.#maybeClearCookiesAndStorage(state);

    Glean.sessionRestore.collectData.stopAndAccumulate(timerId);
    return this.#writeState(state);
  }

  /**
   * Purges cookies and DOMSessionStorage data from the session on clean
   * shutdown, only if requested by the user's preferences.
   */
  #maybeClearCookiesAndStorage(state) {
    // Only do this on shutdown.
    if (!lazy.RunState.isClosing) {
      return;
    }

    // Don't clear when restarting.
    if (
      Services.prefs.getBoolPref("browser.sessionstore.resume_session_once")
    ) {
      return;
    }
    let sanitizeCookies =
      Services.prefs.getBoolPref("privacy.sanitize.sanitizeOnShutdown") &&
      Services.prefs.getBoolPref("privacy.clearOnShutdown.cookies");

    if (sanitizeCookies) {
      // Remove cookies.
      delete state.cookies;

      // Remove DOMSessionStorage data.
      for (let window of state.windows) {
        for (let tab of window.tabs) {
          delete tab.storage;
        }
      }
    }
  }

  /**
   * Saves the current session state. Collects data asynchronously and calls
   * _saveState() to collect data again (with a cache hit rate of hopefully
   * 100%) and write to disk afterwards.
   */
  #saveStateAsync() {
    // Allow scheduling delayed saves again.
    this.#timeoutID = null;

    // Write to disk.
    this.#saveState();
  }

  /**
   * Write the given state object to disk.
   */
  #writeState(state) {
    if (!lazy.RunState.isRunning) {
      lazy.sessionStoreLogger.debug(
        "SessionSaver writing state during shutdown"
      );
    }
    // We update the time stamp before writing so that we don't write again
    // too soon, if saving is requested before the write completes. Without
    // this update we may save repeatedly if actions cause a runDelayed
    // before writing has completed. See Bug 902280
    this.updateLastSaveTime();

    // Write (atomically) to a session file, using a tmp file. Once the session
    // file is successfully updated, save the time stamp of the last save and
    // notify the observers.
    return lazy.SessionFile.write(state).then(
      () => {
        this.updateLastSaveTime();
        if (!lazy.RunState.isRunning) {
          lazy.sessionStoreLogger.debug(
            "SessionSaver sessionstore-state-write-complete during shutdown"
          );
        }
        notify(null, "sessionstore-state-write-complete");
      },
      err => {
        lazy.sessionStoreLogger.error(
          "SessionSaver write() rejected with error",
          err
        );
      }
    );
  }
}

export const SessionSaver = new _SessionSaver();
