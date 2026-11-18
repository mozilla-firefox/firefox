/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", () =>
  console.createInstance({
    logModule: "MiniWindowManager",
    maxLogLevel: Services.prefs.getBoolPref("browser.mini-window.log", false)
      ? "Debug"
      : "Error",
  })
);

ChromeUtils.defineESModuleGetters(lazy, {
  MiniWindow: "moz-src:///browser/components/miniwindow/MiniWindow.sys.mjs",
});

/**
 * Cross-window manager for Mini Windows. Owns the registry of
 * open mini windows, the per-origin lifecycle observers, and SessionStore integration.
 */
class MiniWindowManagerClass {
  /**
   * Live MiniWindow instances.
   *
   * @type {Set<object>}
   */
  _miniwindows = new Set();

  /**
   * Each origin window's live mini windows.
   *
   * @type {WeakMap<Window, Set<object>>}
   */
  #originWinToMinis = new WeakMap();

  /**
   * Origin windows with lifecycle hooks installed, mapped to the controller
   * that removes them.
   *
   * @type {WeakMap<Window, AbortController>}
   */
  #originWinHooks = new WeakMap();

  #observing = false;

  get _enabled() {
    return Services.prefs.getBoolPref("browser.mini-window.enabled", false);
  }

  get _log() {
    return lazy.logConsole;
  }

  /**
   * Pop a cropped region of `tab` into a new mini window.
   *
   * @param {MozTabbrowserTab} tab - the tab to move.
   * @param {object} cropInfo - see MiniWindow.
   * @param {string} entryPoint - where the pop was initiated.
   * @returns {Promise<object|null>} the MiniWindow, or null.
   */
  async popRegion(tab, cropInfo, entryPoint) {
    tab.splitview?.unsplitTabs("mini_window");

    return this.#pop(tab, cropInfo, entryPoint);
  }

  /**
   * Pop the whole `tab` into a new always-on-top window.
   *
   * @param {MozTabbrowserTab} tab - the tab to move.
   * @param {string} entryPoint - where the pop was initiated.
   * @returns {Promise<object|null>} the MiniWindow, or null.
   */
  async popTab(tab, entryPoint) {
    return this.#pop(tab, null, entryPoint);
  }

  /**
   * The tabs popTabs would move. If tabs are in a splitview state,
   * separates them in the results.
   *
   * @param {MozTabbrowserTab[]} tabs
   * @returns {MozTabbrowserTab[]}
   */
  tabsToPop(tabs) {
    let result = new Set();
    for (let tab of tabs) {
      for (let t of tab.splitview?.tabs ?? [tab]) {
        // about:opentabs is used by splitview when the user
        // hasn't selected a second tab yet.
        if (t.linkedBrowser?.currentURI?.spec !== "about:opentabs") {
          result.add(t);
        }
      }
    }
    return [...result];
  }

  /**
   * Pop each of `tabs`, into their own full-tab mini window. Splitview tabs
   * are moved into their own mini windows.
   *
   * @param {MozTabbrowserTab[]} tabs
   * @param {string} entryPoint - where the pop was initiated.
   */
  async popTabs(tabs, entryPoint) {
    let toPop = this.tabsToPop(tabs);
    for (let splitview of new Set(tabs.map(t => t.splitview).filter(Boolean))) {
      splitview.unsplitTabs("mini_window");
    }
    for (let tab of toPop) {
      await this.popTab(tab, entryPoint);
    }
  }

  /**
   * Opens a tab in a new Mini Window. Optionally, pop's just a region
   * that is passed in for the tab.
   *
   * @param {MozTabbrowserTab} tab - the tab to move.
   * @param {object|null} cropInfo - the region to frame (see MiniWindow),
   *   or null for a full-tab mini window.
   * @param {string} entryPoint - where the pop was initiated.
   * @returns {Promise<object|null>} the MiniWindow, or null.
   */
  async #pop(tab, cropInfo, entryPoint) {
    let browser = tab.linkedBrowser;
    this._log.debug("pop: We're about to pop a mini window out: ", {
      url: browser.currentURI?.spec,
      crop: cropInfo,
    });

    if (!this._enabled) {
      return null;
    }

    this.#ensureObservers();

    let originWin = browser.documentGlobal;
    let wasLastTab = originWin.gBrowser.tabs.length === 1;

    // Instantiate the per-window controller
    let miniwindow = new lazy.MiniWindow(this, originWin, tab, cropInfo);

    // Register before the multi-tick open() so the SSWindowClosing/quit
    // teardown below already knows about this popup if the origin window
    // starts closing while the popup is still opening.
    //
    // We want to move originWin's popped tabs home before SessionStore
    // collects it, whether it closes on its own or as part of quitting. Setup
    // the event listeners to move them back.
    this._miniwindows.add(miniwindow);
    let minisForOriginWin = this.#originWinToMinis.get(originWin);
    if (!minisForOriginWin) {
      minisForOriginWin = new Set();
      this.#originWinToMinis.set(originWin, minisForOriginWin);
    }
    minisForOriginWin.add(miniwindow);
    if (!this.#originWinHooks.has(originWin)) {
      this.#hookOriginWin(originWin);
    }

    let win = await miniwindow.open();
    if (!win) {
      this._log.debug("pop: open failed, returning null");
      this._unregister(miniwindow);
      return null;
    }

    this._log.debug("pop: mini window opened");
    Glean.miniWindow.created.record({
      type: cropInfo ? "fragment" : "full_tab",
      entry_point: entryPoint,
      was_last_tab: wasLastTab,
      concurrent_open: this._miniwindows.size,
    });
    return miniwindow;
  }

  /**
   * @param {XULBrowserElement} browser
   * @returns {object|undefined} the mini window hosting `browser`, if any. Keyed on
   * the mini window's <browser> element.
   */
  _miniWindowForBrowser(browser) {
    for (let mini of this._miniwindows) {
      if (mini.browser === browser) {
        return mini;
      }
    }
    return undefined;
  }

  _unregister(miniwindow) {
    this._miniwindows.delete(miniwindow);
    let minisForOriginWin = this.#originWinToMinis.get(miniwindow.originWin);
    if (!minisForOriginWin) {
      this._log.debug("_unregister", { remaining: this._miniwindows.size });
      return;
    }
    minisForOriginWin.delete(miniwindow);
    if (!minisForOriginWin.size) {
      this.#originWinToMinis.delete(miniwindow.originWin);
      this.#unhookOriginWin(miniwindow.originWin);
    }
    this._log.debug("_unregister", { remaining: this._miniwindows.size });
  }

  /** Register the shutdown observer. */
  #ensureObservers() {
    if (this.#observing) {
      return;
    }
    this.#observing = true;
    Services.obs.addObserver(this, "quit-application-granted");
  }

  observe(_subject, topic) {
    if (topic === "quit-application-granted") {
      this._log.debug(
        "observe: quit-application-granted, returning tabs home",
        {
          count: this._miniwindows.size,
        }
      );
      // Preserve the popped tabs by putting them back in their origin window.
      for (let mini of [...this._miniwindows]) {
        mini.returnToOriginWin(false, "origin_window_closed");
      }
    }
  }

  /**
   * Install the per-origin-window lifecycle hooks. Every listener added here
   * must pass the controller's signal so #unhookOriginWin can drop them all.
   *
   * @param {Window} originWin
   */
  #hookOriginWin(originWin) {
    let abortController = new AbortController();
    originWin.addEventListener(
      "SSWindowClosing",
      () => this.#putBackMinisForOriginWins(originWin),
      { signal: abortController.signal }
    );
    this.#originWinHooks.set(originWin, abortController);
  }

  /**
   * Undo #hookOriginWin. No-op while the origin window still has mini windows -
   * they depend on the SSWindowClosing teardown, so removing it early would
   * leak them if the window closed.
   *
   * @param {Window} originWin
   */
  #unhookOriginWin(originWin) {
    if (this.#originWinToMinis.get(originWin)?.size) {
      return;
    }

    let abortController = this.#originWinHooks.get(originWin);
    if (!abortController) {
      return;
    }

    this.#originWinHooks.delete(originWin);
    abortController.abort();
  }

  /**
   * The always-on-top windows `win` should try not to cover.
   *
   * @param {Window} win - the window doing the avoiding; never included.
   * @returns {Window[]}
   */
  windowsToAvoid(win) {
    let wins = [];
    for (let mini of this._miniwindows) {
      if (mini.miniWin && mini.miniWin !== win && !mini.miniWin.closed) {
        wins.push(mini.miniWin);
      }
    }
    for (let player of Services.wm.getEnumerator("Toolkit:PictureInPicture")) {
      if (player.closed) {
        continue;
      }

      wins.push(player);
    }
    return wins;
  }

  /**
   * Moves the oldest popped tab back to the origin window.
   *
   * @param {Window} originWin
   * @returns {boolean} true if a popped tab was moved home.
   */
  maybeMoveOldestMiniWindow(originWin) {
    // The per-origin-window set preserves insertion order.
    let oldest = this.#originWinToMinis.get(originWin)?.values().next().value;
    if (!oldest) {
      // Looks like there's no more to put back.
      return false;
    }
    oldest.returnToOriginWin(true, "auto_return_guardrail");
    return true;
  }

  #putBackMinisForOriginWins(originWin) {
    let minis = this.#minisForOriginWin(originWin);
    this._log.debug("#putBackMinisForOriginWins", { count: minis.length });

    for (let miniWin of minis) {
      miniWin.returnToOriginWin(false, "origin_window_closed");
    }
  }

  /**
   * @param {Window} originWin
   * @returns {object[]} live popups whose originWin is `originWin` (a copy, so
   *   callers can iterate while teardown removes popups from the live set).
   */
  #minisForOriginWin(originWin) {
    let minis = this.#originWinToMinis.get(originWin);
    return minis ? [...minis] : [];
  }
}

export const MiniWindowManager = new MiniWindowManagerClass();
