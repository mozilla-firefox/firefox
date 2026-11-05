/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

// Stop updating jumplists after some idle time.
const IDLE_TIMEOUT_SECONDS = 5 * 60;

// Prefs
const PREF_TASKBAR_BRANCH = "browser.taskbar.lists.";
const PREF_TASKBAR_ENABLED = "enabled";
const PREF_TASKBAR_ITEMCOUNT = "maxListItemCount";
const PREF_TASKBAR_FREQUENT = "frequent.enabled";
const PREF_TASKBAR_RECENT = "recent.enabled";
const PREF_TASKBAR_TASKS = "tasks.enabled";
const PREF_TASKBAR_REFRESH = "refreshInSeconds";

/**
 * Exports
 */

const lazy = {};

/**
 * Smart getters
 */

ChromeUtils.defineLazyGetter(lazy, "_prefs", function () {
  return Services.prefs.getBranch(PREF_TASKBAR_BRANCH);
});

ChromeUtils.defineLazyGetter(lazy, "_stringBundle", function () {
  return Services.strings.createBundle(
    "chrome://browser/locale/taskbar.properties"
  );
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "WindowsJumpLists",
    maxLogLevel: Services.prefs.getBoolPref("browser.taskbar.log", false)
      ? "Debug"
      : "Warn",
  });
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "_idle",
  "@mozilla.org/widget/useridleservice;1",
  Ci.nsIUserIdleService
);
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "_taskbarService",
  "@mozilla.org/windows-taskbar;1",
  Ci.nsIWinTaskbar
);

ChromeUtils.defineESModuleGetters(lazy, {
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
});

/**
 * Global functions
 */

function _getString(name) {
  return lazy._stringBundle.GetStringFromName(name);
}

// Task list configuration data object.

var tasksCfg = [
  /**
   * Task configuration options: title, description, args, iconIndex, open, close.
   *
   * title       - Task title displayed in the list. (strings in the table are temp fillers.)
   * description - Tooltip description on the list item.
   * args        - Command line args to invoke the task.
   * iconIndex   - Optional win icon index into the main application for the
   *               list item.
   * open        - Boolean indicates if the command should be visible after the browser opens.
   * close       - Boolean indicates if the command should be visible after the browser closes.
   */
  // Open new tab
  {
    get title() {
      return _getString("taskbar.tasks.newTab.label");
    },
    get description() {
      return _getString("taskbar.tasks.newTab.description");
    },
    args: ["-new-tab", "about:blank"],
    iconIndex: 3, // New window icon
    open: true,
    close: true, // The jump list already has an app launch icon, but
    // we don't always update the list on shutdown.
    // Thus true for consistency.
  },

  // Open new window
  {
    get title() {
      return _getString("taskbar.tasks.newWindow.label");
    },
    get description() {
      return _getString("taskbar.tasks.newWindow.description");
    },
    args: ["-browser"],
    iconIndex: 2, // New tab icon
    open: true,
    close: true, // No point, but we don't always update the list on
    // shutdown. Thus true for consistency.
  },
];

// Open new private window
let privateWindowTask = {
  get title() {
    return _getString("taskbar.tasks.newPrivateWindow.label");
  },
  get description() {
    return _getString("taskbar.tasks.newPrivateWindow.description");
  },
  args: ["-private-window"],
  iconIndex: 4, // Private browsing mode icon
  open: true,
  close: true, // No point, but we don't always update the list on
  // shutdown. Thus true for consistency.
};

// Implementation

var Builder = class {
  constructor(builder) {
    this._builder = builder;
    this._tasks = null;
    this._shuttingDown = false;
    // These are ultimately controlled by prefs, so we disable
    // everything until is read from there
    this._showTasks = false;
    this._showFrequent = false;
    this._showRecent = false;
    this._maxItemCount = 0;
    this._isBuilding = false;
  }

  refreshPrefs(showTasks, showFrequent, showRecent, maxItemCount) {
    this._showTasks = showTasks;
    this._showFrequent = showFrequent;
    this._showRecent = showRecent;
    this._maxItemCount = maxItemCount;
  }

  updateShutdownState(shuttingDown) {
    this._shuttingDown = shuttingDown;
  }

  delete() {
    delete this._builder;
  }

  /**
   * Constructs the tasks and recent history items to display in the JumpList,
   * and then sends those lists to the nsIJumpListBuilder to be written.
   *
   * @returns {Promise<undefined>}
   *   The Promise resolves once the JumpList has been written, and any
   *   items that the user remove from the recent history list have been
   *   removed from Places. The Promise may reject if any part of constructing
   *   the tasks or sending them to the builder thread failed.
   */
  async buildList() {
    if (!(this._builder instanceof Ci.nsIJumpListBuilder)) {
      console.error(
        "Expected nsIJumpListBuilder. The builder is of the wrong type."
      );
      return;
    }

    if (!this._showRecent) {
      // Clear the recents list, so it won't appear if we disable frequents
      // and tasks.  Recents will only appear if we disable frequents and
      // tasks _and_ we do not clear the list here.
      this._clearRecentsList();
    }

    // anything to build?
    if (!this._showFrequent && !this._showTasks) {
      // don't leave the last list hanging on the taskbar.
      this._deleteActiveJumpList();
      return;
    }

    // Are we in the midst of building an earlier iteration of this list? If
    // so, bail out. Same if we're shutting down.
    if (this._isBuilding || this._shuttingDown) {
      return;
    }

    this._isBuilding = true;

    try {
      let removedURLs = await this._builder.checkForRemovals();
      if (removedURLs.length) {
        await this._clearHistory(removedURLs);
      }

      let selfPath = Services.dirsvc.get("XREExeF", Ci.nsIFile).path;

      let taskDescriptions = [];

      if (this._showTasks) {
        taskDescriptions = this._tasks.map(task => {
          return {
            title: task.title,
            description: task.description,
            path: selfPath,
            arguments: task.args,
            fallbackIconIndex: task.iconIndex,
          };
        });
      }

      let customTitle = "";
      let customDescriptions = [];

      if (this._showFrequent) {
        let conn = await lazy.PlacesUtils.promiseDBConnection();
        let rows = await conn.executeCached(
          "SELECT p.url, IFNULL(p.title, p.url) as title " +
            "FROM moz_places p WHERE p.hidden = 0 " +
            "AND EXISTS (" +
            "SELECT id FROM moz_historyvisits WHERE " +
            "place_id = p.id AND " +
            "visit_type NOT IN (" +
            "0, " +
            `${Ci.nsINavHistoryService.TRANSITION_EMBED}, ` +
            `${Ci.nsINavHistoryService.TRANSITION_FRAMED_LINK}` +
            ")" +
            "LIMIT 1" +
            ") " +
            "ORDER BY p.visit_count DESC LIMIT :limit",
          {
            limit: this._maxItemCount,
          }
        );

        for (let row of rows) {
          let uri = Services.io.newURI(row.getResultByName("url"));
          let iconPath = "";
          try {
            iconPath = await this._builder.obtainAndCacheFaviconAsync(uri);
          } catch (e) {
            // obtainAndCacheFaviconAsync may throw NS_ERROR_NOT_AVAILABLE if
            // the icon doesn't yet exist on the disk, but has been requested.
            // It might also throw an exception if there was a problem fetching
            // the favicon from the database and writing it to the disk. Either
            // case is non-fatal, so we ignore them here.
            lazy.logConsole.warn("Failed to fetch favicon for ", uri.spec, e);
          }

          customDescriptions.push({
            title: row.getResultByName("title"),
            description: row.getResultByName("title"),
            path: selfPath,
            arguments: ["-osint", "-url", row.getResultByName("url")],
            fallbackIconIndex: 1,
            iconPath,
          });
        }

        customTitle = _getString("taskbar.frequent.label");
      }

      if (!this._shuttingDown) {
        await this._builder.populateJumpList(
          taskDescriptions,
          customTitle,
          customDescriptions
        );
      }
    } catch (e) {
      console.error("buildList failed: ", e);
    } finally {
      this._isBuilding = false;
    }
  }

  _clearRecentsList() {
    this._builder.clearRecentsList();
  }

  _deleteActiveJumpList() {
    this._builder.clearJumpList();
  }

  /**
   * Removes URLs from history in Places that the user has requested to clear
   * from their Jump List. We must do this before recomputing which history
   * to put into the Jump List, because if we ever include items that have
   * recently been removed, Windows will not allow us to proceed.
   * Please see
   * https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-icustomdestinationlist-beginlist
   * for more details.
   *
   * The returned Promise never rejects, but may report console errors in the
   * event of removal failure.
   *
   * @param {string[]} uriSpecsToRemove
   *   The URLs to be removed from Places history.
   * @returns {Promise<undefined>}
   */
  _clearHistory(uriSpecsToRemove) {
    let URIsToRemove = uriSpecsToRemove
      .map(spec => URL.parse(spec)?.URI)
      .filter(uri => !!uri);

    if (URIsToRemove.length) {
      return lazy.PlacesUtils.history.remove(URIsToRemove).catch(console.error);
    }
    return Promise.resolve();
  }
};

export var WinTaskbarJumpList = {
  // We build two separate jump lists -- one for the regular Firefox icon
  // and one for the Private Browsing icon
  _builder: null,
  _pbBuilder: null,
  _builtPb: false,
  // Is showing jump lists currently blocked, such as when waiting for the user
  // to interact with the preonboarding modal?
  _blocked: false,
  _shuttingDown: false,

  /**
   * Startup, shutdown, and update
   */

  startup: async function WTBJL_startup() {
    if (!lazy._taskbarService.available) {
      return;
    }
    // exit if initting the taskbar failed for some reason.
    if (!(await this._initTaskbar())) {
      return;
    }

    if (lazy.PrivateBrowsingUtils.enabled) {
      tasksCfg.push(privateWindowTask);
    }
    // Store our task list config data
    this._builder._tasks = tasksCfg;
    this._pbBuilder._tasks = tasksCfg;

    // retrieve taskbar related prefs.
    this._refreshPrefs();

    // observer for private browsing and our prefs branch
    this._initObs();

    // jump list refresh timer
    this._updateTimer();

    if (this._blocked) {
      this._builder._deleteActiveJumpList();
    }
  },

  update: function WTBJL_update() {
    // are we disabled via prefs or currently blocked? don't do anything!
    if (!this._enabled || this._blocked) {
      return;
    }

    if (this._shuttingDown) {
      return;
    }

    this._builder.buildList();

    // We only ever need to do this once because the private browsing window
    // jumplist only ever shows the static task list, which never changes,
    // so it doesn't need to be updated over time.
    if (!this._builtPb) {
      this._pbBuilder.buildList();
      this._builtPb = true;
    }
  },

  _shutdown: function WTBJL__shutdown() {
    this._builder.updateShutdownState(true);
    this._pbBuilder.updateShutdownState(true);
    this._shuttingDown = true;
    this._free();
  },

  /**
   * Prefs utilities
   */

  _refreshPrefs: function WTBJL__refreshPrefs() {
    this._enabled = lazy._prefs.getBoolPref(PREF_TASKBAR_ENABLED);
    var showTasks = lazy._prefs.getBoolPref(PREF_TASKBAR_TASKS);
    this._builder.refreshPrefs(
      showTasks,
      lazy._prefs.getBoolPref(PREF_TASKBAR_FREQUENT),
      lazy._prefs.getBoolPref(PREF_TASKBAR_RECENT),
      lazy._prefs.getIntPref(PREF_TASKBAR_ITEMCOUNT)
    );
    // showTasks is the only relevant pref for the Private Browsing Jump List
    // the others are are related to frequent/recent entries, which are
    // explicitly disabled for it
    this._pbBuilder.refreshPrefs(showTasks, false, false, 0);
  },

  /**
   * Init and shutdown utilities
   */

  _initTaskbar: async function WTBJL__initTaskbar() {
    let builder;
    let pbBuilder;

    builder = lazy._taskbarService.createJumpListBuilder(false);
    pbBuilder = lazy._taskbarService.createJumpListBuilder(true);
    if (!builder || !pbBuilder) {
      return false;
    }
    let [builderAvailable, pbBuilderAvailable] = await Promise.all([
      builder.isAvailable(),
      pbBuilder.isAvailable(),
    ]);
    if (!builderAvailable || !pbBuilderAvailable) {
      return false;
    }

    this._builder = new Builder(builder);
    this._pbBuilder = new Builder(pbBuilder);

    return true;
  },

  _initObs: function WTBJL__initObs() {
    // If the browser is closed while in private browsing mode, the "exit"
    // notification is fired on quit-application-granted.
    // History cleanup can happen at profile-change-teardown.
    Services.obs.addObserver(this, "profile-before-change");
    Services.obs.addObserver(this, "browser:purge-session-history");
    lazy._prefs.addObserver("", this);
    this._placesObserver = new PlacesWeakCallbackWrapper(
      this.update.bind(this)
    );
    lazy.PlacesUtils.observers.addListener(
      ["history-cleared"],
      this._placesObserver
    );
  },

  _freeObs: function WTBJL__freeObs() {
    Services.obs.removeObserver(this, "profile-before-change");
    Services.obs.removeObserver(this, "browser:purge-session-history");
    lazy._prefs.removeObserver("", this);
    if (this._placesObserver) {
      lazy.PlacesUtils.observers.removeListener(
        ["history-cleared"],
        this._placesObserver
      );
    }
  },

  _updateTimer: function WTBJL__updateTimer() {
    if (this._enabled && !this._shuttingDown && !this._timer) {
      this._timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
      this._timer.initWithCallback(
        this,
        lazy._prefs.getIntPref(PREF_TASKBAR_REFRESH) * 1000,
        this._timer.TYPE_REPEATING_SLACK
      );
    } else if ((!this._enabled || this._shuttingDown) && this._timer) {
      this._timer.cancel();
      delete this._timer;
    }
  },

  _hasIdleObserver: false,
  _updateIdleObserver: function WTBJL__updateIdleObserver() {
    if (this._enabled && !this._shuttingDown && !this._hasIdleObserver) {
      lazy._idle.addIdleObserver(this, IDLE_TIMEOUT_SECONDS);
      this._hasIdleObserver = true;
    } else if (
      (!this._enabled || this._shuttingDown) &&
      this._hasIdleObserver
    ) {
      lazy._idle.removeIdleObserver(this, IDLE_TIMEOUT_SECONDS);
      this._hasIdleObserver = false;
    }
  },

  _free: function WTBJL__free() {
    this._freeObs();
    this._updateTimer();
    this._updateIdleObserver();
    this._builder.delete();
    this._pbBuilder.delete();
  },

  QueryInterface: ChromeUtils.generateQI([
    "nsINamed",
    "nsIObserver",
    "nsITimerCallback",
  ]),

  name: "WinTaskbarJumpList",

  blockJumpList: async function WTBJL_clearJumpList(unblockPromise) {
    this._blocked = true;
    if (unblockPromise) {
      try {
        await unblockPromise;
      } catch (e) {
        console.error("Unblock promise error, reinstating jump list: ", e);
      }
    }
    this._unblockJumpList();
  },

  _unblockJumpList: function WTBJL_updateJumpList() {
    this._blocked = false;
    this.update();
  },

  notify: function WTBJL_notify() {
    // Add idle observer on the first notification so it doesn't hit startup.
    this._updateIdleObserver();
    Services.tm.idleDispatchToMainThread(() => {
      this.update();
    });
  },

  observe: function WTBJL_observe(aSubject, aTopic) {
    switch (aTopic) {
      case "nsPref:changed":
        if (this._enabled && !lazy._prefs.getBoolPref(PREF_TASKBAR_ENABLED)) {
          this._builder._deleteActiveJumpList();
        }
        this._refreshPrefs();
        this._updateTimer();
        this._updateIdleObserver();
        Services.tm.idleDispatchToMainThread(() => {
          this.update();
        });
        break;

      case "profile-before-change":
        this._shutdown();
        break;

      case "browser:purge-session-history":
        this.update();
        break;
      case "idle":
        if (this._timer) {
          this._timer.cancel();
          delete this._timer;
        }
        break;

      case "active":
        this._updateTimer();
        break;
    }
  },
};
