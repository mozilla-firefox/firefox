/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  TabMetrics: "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs",
});

/**
 * This module handles UI interactions for session restore features,
 * primarily for interacting with browser windows to restore closed tabs
 * and sessions.
 */
export var SessionWindowUI = {
  /**
   * Applies only to the cmd|ctrl + shift + T keyboard shortcut
   * Undo the last action that was taken - either closing the last tab or closing the last window;
   * If none of those were the last actions, restore the last session if possible.
   */
  restoreLastClosedTabOrWindowOrSession(window) {
    let lastActionTaken = lazy.SessionStore.popLastClosedAction();
    if (lastActionTaken) {
      switch (lastActionTaken.type) {
        case lazy.SessionStore.LAST_ACTION_CLOSED_TAB:
          {
            const sourceWindow = lazy.SessionStore.getWindowForTabClosedId(
              lastActionTaken.closedId
            );
            this.undoCloseTab(window, undefined, sourceWindow?.__SSi);
          }
          break;
        case lazy.SessionStore.LAST_ACTION_CLOSED_WINDOW: {
          this.undoCloseWindow();
          break;
        }
      }
    } else {
      let closedTabCount = lazy.SessionStore.getLastClosedTabCount(window);
      if (lazy.SessionStore.canRestoreLastSession) {
        lazy.SessionStore.restoreLastSession();
      } else if (closedTabCount) {
        // we need to support users who have automatic session restore enabled
        this.undoCloseTab(window);
      }
    }
  },

  /**
   * Re-open a closed tab into the current window.
   *
   * @param window
   *        Window reference
   * @param [aIndex]
   *        The index of the tab (via SessionStore.getClosedTabData).
   *        When undefined, the first n closed tabs will be re-opened, where n is provided by getLastClosedTabCount.
   * @param {string} [sourceWindowSSId]
   *        An optional sessionstore id to identify the source window for the tab.
   *        I.e. the window the tab belonged to when closed.
   *        When undefined we'll use the current window
   * @returns a reference to the reopened tab.
   */
  undoCloseTab(window, aIndex, sourceWindowSSId) {
    // the window we'll open the tab into
    let targetWindow = window;
    // the window the tab was closed from
    let sourceWindow;
    if (sourceWindowSSId) {
      sourceWindow = lazy.SessionStore.getWindowById(sourceWindowSSId);
      if (!sourceWindow) {
        throw new Error(
          "sourceWindowSSId argument to undoCloseTab didn't resolve to a window"
        );
      }
    } else {
      sourceWindow = window;
    }

    // wallpaper patch to prevent an unnecessary blank tab (bug 343895)
    let blankTabToRemove = null;
    if (
      targetWindow.gBrowser.visibleTabs.length == 1 &&
      targetWindow.gBrowser.selectedTab.isEmpty
    ) {
      blankTabToRemove = targetWindow.gBrowser.selectedTab;
    }

    let tabsRemoved = false;
    let tab = null;
    const lastClosedTabGroupId =
      lazy.SessionStore.getLastClosedTabGroupId(sourceWindow);
    if (aIndex === undefined && lastClosedTabGroupId) {
      let group;
      if (lazy.SessionStore.getSavedTabGroup(lastClosedTabGroupId)) {
        group = lazy.SessionStore.openSavedTabGroup(
          lastClosedTabGroupId,
          targetWindow,
          {
            source: lazy.TabMetrics.METRIC_SOURCE.RECENT_TABS,
          }
        );
      } else {
        group = lazy.SessionStore.undoCloseTabGroup(
          window,
          lastClosedTabGroupId,
          targetWindow
        );
      }
      tabsRemoved = true;
      tab = group.tabs.at(-1);
    } else {
      // We are specifically interested in the lastClosedTabCount for the source window.
      // When aIndex is undefined, we restore all the lastClosedTabCount tabs.
      let lastClosedTabCount =
        lazy.SessionStore.getLastClosedTabCount(sourceWindow);
      // aIndex is undefined if the function is called without a specific tab to restore.
      let tabsToRemove =
        aIndex !== undefined ? [aIndex] : new Array(lastClosedTabCount).fill(0);
      for (let index of tabsToRemove) {
        if (
          lazy.SessionStore.getClosedTabCountForWindow(sourceWindow) > index
        ) {
          tab = lazy.SessionStore.undoCloseTab(
            sourceWindow,
            index,
            targetWindow
          );
          tabsRemoved = true;
        }
      }
    }

    if (tabsRemoved && blankTabToRemove) {
      targetWindow.gBrowser.removeTab(blankTabToRemove);
    }

    return tab;
  },

  /**
   * Re-open a closed window.
   *
   * @param aIndex
   *        The index of the window (via SessionStore.getClosedWindowData)
   * @returns a reference to the reopened window.
   */
  undoCloseWindow(aIndex) {
    let restoredWindow = null;
    if (lazy.SessionStore.getClosedWindowCount() > (aIndex || 0)) {
      restoredWindow = lazy.SessionStore.undoCloseWindow(aIndex || 0);
    }

    return restoredWindow;
  },

  /**
   * Only show the infobar when canRestoreLastSession and the pref value == 1
   */
  async maybeShowRestoreSessionInfoBar() {
    let win = lazy.BrowserWindowTracker.getTopWindow({
      allowFromInactiveWorkspace: true,
    });
    let count = Services.prefs.getIntPref(
      "browser.startup.couldRestoreSession.count",
      0
    );
    if (count < 0 || count >= 2) {
      return;
    }
    if (count == 0) {
      // We don't show the infobar right after the update which establishes this pref
      // Increment the counter so we can consider it next time
      Services.prefs.setIntPref(
        "browser.startup.couldRestoreSession.count",
        ++count
      );
      return;
    }

    // We've restarted at least once; we will show the notification if possible.
    // We can't do that if there's no session to restore, or this is a private window.
    if (
      !lazy.SessionStore.canRestoreLastSession ||
      lazy.PrivateBrowsingUtils.isWindowPrivate(win)
    ) {
      return;
    }

    Services.prefs.setIntPref(
      "browser.startup.couldRestoreSession.count",
      ++count
    );

    const messageFragment = win.document.createDocumentFragment();
    const message = win.document.createElement("span");
    const icon = win.document.createElement("img");
    icon.src = "chrome://browser/skin/menu.svg";
    icon.setAttribute("data-l10n-name", "icon");
    icon.className = "inline-icon";
    message.appendChild(icon);
    messageFragment.appendChild(message);
    win.document.l10n.setAttributes(
      message,
      "restore-session-startup-suggestion-message"
    );

    const buttons = [
      {
        "l10n-id": "restore-session-startup-suggestion-button",
        primary: true,
        callback: () => {
          win.PanelUI.selectAndMarkItem([
            "appMenu-history-button",
            "appMenu-restoreSession",
          ]);
        },
      },
    ];

    const notifyBox = win.gBrowser.getNotificationBox();
    const notification = await notifyBox.appendNotification(
      "startup-restore-session-suggestion",
      {
        label: messageFragment,
        priority: notifyBox.PRIORITY_INFO_MEDIUM,
      },
      buttons
    );
    // Don't allow it to be immediately hidden:
    notification.timeout = Date.now() + 3000;
  },
};

export class RestoreLastSessionObserver {
  constructor(window) {
    this._window = window;
    this._window.addEventListener("unload", this);
    this._observersAdded = false;
  }

  init() {
    if (
      lazy.SessionStore.canRestoreLastSession &&
      !lazy.PrivateBrowsingUtils.isWindowPrivate(this._window)
    ) {
      Services.obs.addObserver(this, "sessionstore-last-session-cleared", true);
      Services.obs.addObserver(
        this,
        "sessionstore-last-session-re-enable",
        true
      );
      this._observersAdded = true;
      this._window.goSetCommandEnabled("Browser:RestoreLastSession", true);
    } else if (lazy.SessionStore.willAutoRestore) {
      this._window.document.getElementById(
        "Browser:RestoreLastSession"
      ).hidden = true;
    }
  }

  uninit() {
    if (this._window) {
      if (this._observersAdded) {
        Services.obs.removeObserver(this, "sessionstore-last-session-cleared");
        Services.obs.removeObserver(
          this,
          "sessionstore-last-session-re-enable"
        );
        this._observersAdded = false;
      }

      this._window.removeEventListener("unload", this);
      this._window = null;
    }
  }

  handleEvent(event) {
    if (event.type === "unload") {
      this.uninit();
    }
  }

  observe(aSubject, aTopic) {
    if (!this._window) {
      return;
    }

    switch (aTopic) {
      case "sessionstore-last-session-cleared":
        this._window.goSetCommandEnabled("Browser:RestoreLastSession", false);
        break;
      case "sessionstore-last-session-re-enable":
        this._window.goSetCommandEnabled("Browser:RestoreLastSession", true);
        break;
    }
  }

  QueryInterface = ChromeUtils.generateQI([
    "nsIObserver",
    "nsISupportsWeakReference",
  ]);
}
