/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const STORAGE_MAX_EVENTS = 1000;

var _consoleStorage = new Map();

// The process type never changes, so read it once rather than paying an
// XPConnect getter on every recorded console event.
const _isParentProcess =
  Services.appinfo.processType === Services.appinfo.PROCESS_TYPE_DEFAULT;

/**
 * Check whether aId is the inner window ID of a window that no longer exists.
 *
 * @param string aId
 *        The inner window ID to check.
 * @returns boolean
 *          true if aId identifies a destroyed inner window.
 */
function _isDestroyedInnerWindow(aId) {
  // Inner window IDs are positive; "" (a chrome call without a stack frame)
  // would otherwise read as 0.
  let innerId = Number(aId);
  if (!Number.isInteger(innerId) || innerId <= 0) {
    return false;
  }
  return _isParentProcess
    ? !WindowGlobalParent.getByInnerWindowId(innerId)
    : !WindowGlobalChild.getByInnerWindowId(innerId);
}

// NOTE: these listeners used to just be added as observers and notified via
// Services.obs.notifyObservers. However, that has enough overhead to be a
// problem for this. Using an explicit global array is much cheaper, and
// should be equivalent.
var _logEventListeners = [];

const CONSOLEAPISTORAGE_CID = Components.ID(
  "{96cf7855-dfa9-4c6d-8276-f9705b4890f2}"
);

/**
 * The ConsoleAPIStorage is meant to cache window.console API calls for later
 * reuse by other components when needed. For example, the Web Console code can
 * display the cached messages when it opens for the active tab.
 *
 * ConsoleAPI messages are stored as they come from the ConsoleAPI code, with
 * all their properties. They are kept around until the inner window object that
 * created the messages is destroyed. Messages are indexed by the inner window
 * ID.
 *
 * Usage:
 *    let ConsoleAPIStorage = Cc["@mozilla.org/consoleAPI-storage;1"]
 *                              .getService(Ci.nsIConsoleAPIStorage);
 *
 *    // Get the cached events array for the window you want (use the inner
 *    // window ID).
 *    let events = ConsoleAPIStorage.getEvents(innerWindowID);
 *    events.forEach(function(event) { ... });
 *
 *    // Clear the events for the given inner window ID.
 *    ConsoleAPIStorage.clearEvents(innerWindowID);
 */
export function ConsoleAPIStorageService() {
  this.init();
}

ConsoleAPIStorageService.prototype = {
  classID: CONSOLEAPISTORAGE_CID,
  QueryInterface: ChromeUtils.generateQI([
    "nsIConsoleAPIStorage",
    "nsIObserver",
  ]),

  observe: function CS_observe(aSubject, aTopic) {
    if (aTopic == "xpcom-shutdown") {
      Services.obs.removeObserver(this, "xpcom-shutdown");
      Services.obs.removeObserver(this, "inner-window-destroyed");
      Services.obs.removeObserver(this, "memory-pressure");
    } else if (aTopic == "inner-window-destroyed") {
      let innerWindowID = aSubject.QueryInterface(Ci.nsISupportsPRUint64).data;
      this.clearEvents(innerWindowID + "");
    } else if (aTopic == "memory-pressure") {
      this.clearEvents();
    }
  },

  /** @private */
  init: function CS_init() {
    Services.obs.addObserver(this, "xpcom-shutdown");
    Services.obs.addObserver(this, "inner-window-destroyed");
    Services.obs.addObserver(this, "memory-pressure");
  },

  /**
   * Get the events array by inner window ID or all events from all windows.
   *
   * @param string [aId]
   *        Optional, the inner window ID for which you want to get the array of
   *        cached events.
   * @returns array
   *          The array of cached events for the given window. If no |aId| is
   *          given this function returns all of the cached events, from any
   *          window.
   */
  getEvents: function CS_getEvents(aId) {
    if (aId != null) {
      return (_consoleStorage.get(aId) || []).slice(0);
    }

    let result = [];

    for (let [, events] of _consoleStorage) {
      result.push.apply(result, events);
    }

    return result.sort(function (a, b) {
      return a.timeStamp - b.timeStamp;
    });
  },

  /**
   * Adds a listener to be notified of log events.
   *
   * @param jsval [aListener]
   *        A JS listener which will be notified with the message object when
   *        a log event occurs.
   * @param nsIPrincipal [aPrincipal]
   *        The principal of the listener - used to determine if we need to
   *        clone the message before forwarding it.
   */
  addLogEventListener: function CS_addLogEventListener(aListener, aPrincipal) {
    // If our listener has a less-privileged principal than us, then they won't
    // be able to access the log event object which was populated for our
    // scope. Accordingly we need to clone it for these listeners.
    //
    // XXX: AFAICT these listeners which we need to clone messages for are all
    // tests. Alternative solutions are welcome.
    const clone = !aPrincipal.subsumes(
      Cc["@mozilla.org/systemprincipal;1"].createInstance(Ci.nsIPrincipal)
    );
    _logEventListeners.push({
      callback: aListener,
      clone,
    });
  },

  /**
   * Removes a listener added with `addLogEventListener`.
   *
   * @param jsval [aListener]
   *        A JS listener which was added with `addLogEventListener`.
   */
  removeLogEventListener: function CS_removeLogEventListener(aListener) {
    const index = _logEventListeners.findIndex(l => l.callback === aListener);
    if (index != -1) {
      _logEventListeners.splice(index, 1);
    } else {
      console.error(
        "Attempted to remove a log event listener that does not exist."
      );
    }
  },

  /**
   * Record an event associated with the given window ID.
   *
   * @param string aId
   *        The ID of the inner window for which the event occurred or "jsm" for
   *        messages logged from JavaScript modules..
   * @param object aEvent
   *        A JavaScript object you want to store.
   */
  recordEvent: function CS_recordEvent(aId, aEvent) {
    // Skip the cache for late events from already-destroyed inner windows:
    // caching would keep the destroyed window's global alive. Listeners
    // (e.g. devtools) are still notified.
    if (!_isDestroyedInnerWindow(aId)) {
      if (!_consoleStorage.has(aId)) {
        _consoleStorage.set(aId, []);
      }

      let storage = _consoleStorage.get(aId);

      storage.push(aEvent);

      // truncate
      if (storage.length > STORAGE_MAX_EVENTS) {
        storage.shift();
      }
    }

    for (let { callback, clone } of _logEventListeners) {
      try {
        if (clone) {
          callback(Cu.cloneInto(aEvent, callback));
        } else {
          callback(aEvent);
        }
      } catch (e) {
        // A failing listener should not prevent from calling other listeners.
        console.error(e);
      }
    }
  },

  /**
   * Clear storage data for the given window.
   *
   * @param string [aId]
   *        Optional, the inner window ID for which you want to clear the
   *        messages. If this is not specified all of the cached messages are
   *        cleared, from all window objects.
   */
  clearEvents: function CS_clearEvents(aId) {
    if (aId != null) {
      _consoleStorage.delete(aId);
    } else {
      _consoleStorage.clear();
      Services.obs.notifyObservers(null, "console-storage-reset");
    }
  },
};
