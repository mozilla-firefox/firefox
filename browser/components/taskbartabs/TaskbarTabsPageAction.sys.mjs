/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const kWidgetId = "taskbar-tabs-button";

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

let lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  TaskbarTabs: "resource:///modules/taskbartabs/TaskbarTabs.sys.mjs",
  TaskbarTabsUtils: "resource:///modules/taskbartabs/TaskbarTabsUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", () => {
  return console.createInstance({
    prefix: "TaskbarTabs",
    maxLogLevel: "Warn",
  });
});

/**
 * Object which handles Taskbar Tabs page actions.
 */
export const TaskbarTabsPageAction = {
  // Set of tabs currently being processed due to a page action event.
  _processingTabs: new Set(),

  /**
   * Connects a listener to the Taskbar Tabs page action.
   *
   * @param {DOMWindow} aWindow - The browser window.
   */
  init(aWindow) {
    let isPopupWindow = !aWindow.toolbar.visible;
    let isPrivate = lazy.PrivateBrowsingUtils.isWindowPrivate(aWindow);
    let isSupportedPlatform = ["win", "linux"].includes(AppConstants.platform);

    if (isPopupWindow || isPrivate || !isSupportedPlatform) {
      lazy.logConsole.info("Not initializing Taskbar Tabs Page Action.");
      return;
    }

    lazy.logConsole.info("Initializing Taskbar Tabs Page Action.");

    let taskbarTabsButton = aWindow.document.getElementById(kWidgetId);
    taskbarTabsButton.addEventListener("click", this, true);

    if (lazy.TaskbarTabsUtils.isTaskbarTabWindow(aWindow)) {
      taskbarTabsButton.setAttribute(
        "data-l10n-id",
        "taskbar-tab-urlbar-button-close"
      );
    }

    initVisibilityChanges(aWindow, taskbarTabsButton);
  },

  /**
   * Handles the clicking of the page action button associated with Taskbar
   * Tabs.
   *
   * @param {Event} aEvent - The event triggered by the Taskbar Tabs page
   * action.
   * @returns {Promise} Resolves once the event has been handled.
   */
  async handleEvent(aEvent) {
    if (aEvent.button != 0) {
      // Only handle left-click events.
      return;
    }

    let window = aEvent.target.documentGlobal;
    let currentTab = window.gBrowser.selectedTab;

    if (this._processingTabs.has(currentTab)) {
      // Button was clicked before last input finished processing for the tab,
      // discard to avoid racing. Don't bother buffering input - clicking
      // repeatedly before input is processed is not meaningful.
      lazy.logConsole.debug(
        `Page Action still processing for tab, dropping input.`
      );
      return;
    }
    lazy.logConsole.debug(`Blocking Page Action input for tab.`);
    this._processingTabs.add(currentTab);

    try {
      let isTaskbarTabWindow = lazy.TaskbarTabsUtils.isTaskbarTabWindow(window);

      if (!isTaskbarTabWindow) {
        lazy.logConsole.info("Opening new Taskbar Tab via Page Action.");
        await lazy.TaskbarTabs.moveTabIntoTaskbarTab(currentTab);
      } else {
        lazy.logConsole.info("Closing Taskbar Tab via Page Action.");

        // Move tab to a regular browser window.
        let id = lazy.TaskbarTabsUtils.getTaskbarTabIdFromWindow(window);

        await lazy.TaskbarTabs.ejectWindow(window);

        if (!(await lazy.TaskbarTabs.getCountForId(id))) {
          lazy.logConsole.info("Uninstalling Taskbar Tab via Page Action.");
          await lazy.TaskbarTabs.removeTaskbarTab(id);
        }
      }
    } finally {
      lazy.logConsole.debug(`Unblocking Page Action input for tab.`);
      this._processingTabs.delete(currentTab);
    }
  },
};

/**
 * Shows or hides the page action as the user navigates.
 *
 * @param {Window} aWindow - The window that contains the page action.
 * @param {Element} aElement - The element that makes up the page action.
 */
function initVisibilityChanges(aWindow, aElement) {
  // Filled in at the end; memoized to avoid performance failures.
  let isTaskbarTabsEnabled = false;

  const shouldShow = aLocation => {
    if (!isTaskbarTabsEnabled) {
      return false;
    }

    // Forcefully initialize Taskbar Tabs. At some point, this will also affect
    // the page action; in the meantime, ensures that telemetry info is
    // prepared whenever the pref is enabled.
    //
    // This is a promise, but we don't care when it finishes. It's a no-op if
    // TaskbarTabs already initialized.
    lazy.TaskbarTabs.waitUntilReady();

    if (!(aLocation instanceof Ci.nsIURL)) {
      return false;
    }

    return ["http", "https", "moz-extension"].includes(aLocation.scheme);
  };

  aWindow.gBrowser.addProgressListener({
    onLocationChange(aWebProgress, aRequest, aLocation) {
      if (aWebProgress.isTopLevel) {
        aElement.hidden = !shouldShow(aLocation);
      }
    },
  });

  const observer = () => {
    isTaskbarTabsEnabled = lazy.TaskbarTabsUtils.isEnabled();
    aElement.hidden = !shouldShow(aWindow.gBrowser.currentURI);
  };

  Services.prefs.addObserver("browser.taskbarTabs.enabled", observer);
  aWindow.addEventListener("unload", function () {
    Services.prefs.removeObserver("browser.taskbarTabs.enabled", observer);
  });

  observer();
}
