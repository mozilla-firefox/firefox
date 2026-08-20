/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var { ExtensionError, promiseObserved } = ExtensionUtils;

ChromeUtils.defineESModuleGetters(this, {
  AddonManagerPrivate: "resource://gre/modules/AddonManager.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
});

const SS_ON_CLOSED_OBJECTS_CHANGED = "sessionstore-closed-objects-changed";

const getRecentlyClosed = (maxResults, extension) => {
  let recentlyClosed = [];

  // Get closed windows
  // Closed private windows are not stored in sessionstore, we do
  // not need to check access for that.
  let closedWindowData = SessionStore.getClosedWindowData();
  for (let window of closedWindowData) {
    recentlyClosed.push({
      lastModified: window.closedAt,
      window: Window.convertFromSessionStoreClosedData(extension, window),
    });
  }

  // Get closed tabs
  // Private closed tabs are in sessionstore if the owning window is still open .
  for (let window of windowTracker.browserWindows()) {
    if (!extension.canAccessWindow(window)) {
      continue;
    }
    let closedTabData = SessionStore.getClosedTabDataForWindow(window);
    for (let tab of closedTabData) {
      recentlyClosed.push({
        lastModified: tab.closedAt,
        tab: Tab.convertFromSessionStoreClosedData(extension, tab, window),
      });
    }
  }

  // Sort windows and tabs
  recentlyClosed.sort((a, b) => b.lastModified - a.lastModified);
  return recentlyClosed.slice(0, maxResults);
};

const createSession = async function createSession(
  restored,
  extension,
  sessionId
) {
  if (!restored) {
    throw new ExtensionError(
      `Could not restore object using sessionId ${sessionId}.`
    );
  }
  let sessionObj = { lastModified: Date.now() };
  if (restored.isChromeWindow) {
    await promiseObserved(
      "sessionstore-single-window-restored",
      subject => subject == restored
    );
    sessionObj.window = extension.windowManager.convert(restored, {
      populate: true,
    });
    return sessionObj;
  }
  sessionObj.tab = extension.tabManager.convert(restored);
  return sessionObj;
};

const getEncodedKey = function getEncodedKey(extensionId, key) {
  // Throw if using a temporary extension id.
  if (AddonManagerPrivate.isTemporaryInstallID(extensionId)) {
    let message =
      "Sessions API storage methods will not work with a temporary addon ID. " +
      "Please add an explicit addon ID to your manifest.";
    throw new ExtensionError(message);
  }

  return `extension:${extensionId}:${key}`;
};

this.sessions = class extends ExtensionAPIPersistent {
  PERSISTENT_EVENTS = {
    onChanged({ fire }) {
      let observer = () => {
        fire.async();
      };

      Services.obs.addObserver(observer, SS_ON_CLOSED_OBJECTS_CHANGED);
      return {
        unregister() {
          Services.obs.removeObserver(observer, SS_ON_CLOSED_OBJECTS_CHANGED);
        },
        convert(_fire) {
          fire = _fire;
        },
      };
    },
  };

  getAPI(context) {
    let { extension } = context;

    function getTabParams(key, id) {
      let encodedKey = getEncodedKey(extension.id, key);
      let tab = tabTracker.getTab(id);
      if (!context.canAccessWindow(tab.documentGlobal)) {
        throw new ExtensionError(`Invalid tab ID: ${id}`);
      }
      return { encodedKey, tab };
    }

    function getWindowParams(key, id) {
      let encodedKey = getEncodedKey(extension.id, key);
      let win = windowTracker.getWindow(id, context);
      return { encodedKey, win };
    }

    function getClosedIdFromSessionId(sessionId) {
      // sessionId is a string, but internally closedId values are integers.
      // convertFromSessionStoreClosedData in ext-browser.js does the opposite conversion.
      let closedId = parseInt(sessionId, 10);
      if (Number.isInteger(closedId)) {
        return closedId;
      }
      throw new ExtensionError(`Invalid sessionId: ${sessionId}.`);
    }

    return {
      sessions: {
        async getRecentlyClosed(filter) {
          await SessionStore.promiseInitialized;
          // TODO bug 1764376: should be at most MAX_SESSION_RESULTS
          let maxResults = filter.maxResults ?? Infinity;
          return getRecentlyClosed(maxResults, extension);
        },

        async forgetClosedTab(windowId, sessionId) {
          await SessionStore.promiseInitialized;
          let window = windowTracker.getWindow(windowId, context);
          let closedTabData = SessionStore.getClosedTabDataForWindow(window);
          let closedId = getClosedIdFromSessionId(sessionId);

          let closedTabIndex = closedTabData.findIndex(closedTab => {
            return closedTab.closedId === closedId;
          });

          if (closedTabIndex < 0) {
            throw new ExtensionError(
              `Could not find closed tab using sessionId ${sessionId}.`
            );
          }

          SessionStore.forgetClosedTab(window, closedTabIndex);
        },

        async forgetClosedWindow(sessionId) {
          await SessionStore.promiseInitialized;
          let closedWindowData = SessionStore.getClosedWindowData();
          let closedId = getClosedIdFromSessionId(sessionId);
          let closedWindowIndex = closedWindowData.findIndex(closedWindow => {
            return closedWindow.closedId === closedId;
          });

          if (closedWindowIndex < 0) {
            throw new ExtensionError(
              `Could not find closed window using sessionId ${sessionId}.`
            );
          }

          SessionStore.forgetClosedWindow(closedWindowIndex);
        },

        async restore(sessionId) {
          await SessionStore.promiseInitialized;
          let session;
          let closedId;
          if (sessionId) {
            closedId = getClosedIdFromSessionId(sessionId);
          }
          let targetWindow;

          // closedId is internally represented as an integer and could be 0.
          if (closedId !== undefined) {
            if (SessionStore.getObjectTypeForClosedId(closedId) == "tab") {
              // we want to restore the tab to the original window is was closed from
              targetWindow = SessionStore.getWindowForTabClosedId(
                closedId,
                extension.privateBrowsingAllowed
              );
            }
            session = SessionStore.undoCloseById(
              closedId,
              extension.privateBrowsingAllowed,
              targetWindow // ignored if we are restoring a window
            );
          } else if (SessionStore.lastClosedObjectType == "window") {
            // If the most recently closed object is a window, just undo closing the most recent window.
            session = SessionStore.undoCloseWindow(0);
          } else {
            // It is a tab, and we cannot call SessionStore.undoCloseTab without a window,
            // so we must find the tab in which case we can just use its closedId.
            let recentlyClosedTabs = [];
            for (let window of windowTracker.browserWindows()) {
              let closedTabData =
                SessionStore.getClosedTabDataForWindow(window);
              for (let tab of closedTabData) {
                recentlyClosedTabs.push(tab);
              }
            }

            if (recentlyClosedTabs.length) {
              // Sort the tabs.
              recentlyClosedTabs.sort((a, b) => b.closedAt - a.closedAt);

              // Use the closedId of the most recently closed tab to restore it.
              closedId = recentlyClosedTabs[0].closedId;
              // we want the tab to be re-opened into the same window it was closed from
              targetWindow = SessionStore.getWindowForTabClosedId(
                closedId,
                extension.privateBrowsingAllowed
              );
              session = SessionStore.undoCloseById(
                closedId,
                extension.privateBrowsingAllowed,
                targetWindow
              );
            }
          }
          return createSession(session, extension, closedId);
        },

        setTabValue(tabId, key, value) {
          let { tab, encodedKey } = getTabParams(key, tabId);

          SessionStore.setCustomTabValue(
            tab,
            encodedKey,
            JSON.stringify(value)
          );
        },

        async getTabValue(tabId, key) {
          let { tab, encodedKey } = getTabParams(key, tabId);

          let value = SessionStore.getCustomTabValue(tab, encodedKey);
          if (value) {
            return JSON.parse(value);
          }

          return undefined;
        },

        removeTabValue(tabId, key) {
          let { tab, encodedKey } = getTabParams(key, tabId);

          SessionStore.deleteCustomTabValue(tab, encodedKey);
        },

        setWindowValue(windowId, key, value) {
          let { win, encodedKey } = getWindowParams(key, windowId);

          SessionStore.setCustomWindowValue(
            win,
            encodedKey,
            JSON.stringify(value)
          );
        },

        async getWindowValue(windowId, key) {
          let { win, encodedKey } = getWindowParams(key, windowId);

          let value = SessionStore.getCustomWindowValue(win, encodedKey);
          if (value) {
            return JSON.parse(value);
          }

          return undefined;
        },

        removeWindowValue(windowId, key) {
          let { win, encodedKey } = getWindowParams(key, windowId);

          SessionStore.deleteCustomWindowValue(win, encodedKey);
        },

        onChanged: new EventManager({
          context,
          module: "sessions",
          event: "onChanged",
          extensionApi: this,
        }).api(),
      },
    };
  }
};
