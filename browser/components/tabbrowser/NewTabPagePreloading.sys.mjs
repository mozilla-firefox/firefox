/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * This module is in charge of preloading 'new tab' pages for use when
 * the user opens a new tab.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
});

export let NewTabPagePreloading = {
  // Maximum number of instances of a given page we'll preload at any time.
  // Because we preload about:newtab for normal windows, and about:privatebrowsing
  // for private ones, we could have 3 of each.
  MAX_COUNT: 3,

  // How many preloaded tabs we have, across all windows, for the private and non-private
  // case:
  browserCounts: {
    normal: 0,
    private: 0,
  },

  get enabled() {
    return (
      this.prefEnabled &&
      this.newTabEnabled &&
      !lazy.AboutNewTab.newTabURLOverridden
    );
  },

  /**
   * Create a browser in the right process type.
   */
  _createBrowser(win) {
    const {
      gBrowser,
      gMultiProcessBrowser,
      gFissionBrowser,
      BROWSER_NEW_TAB_URL,
    } = win;

    let oa = lazy.E10SUtils.predictOriginAttributes({ window: win });

    let remoteType = lazy.E10SUtils.getRemoteTypeForURI(
      BROWSER_NEW_TAB_URL,
      gMultiProcessBrowser,
      gFissionBrowser,
      lazy.E10SUtils.DEFAULT_REMOTE_TYPE,
      null,
      oa
    );
    let browser = gBrowser.createBrowser({
      isPreloadBrowser: true,
      remoteType,
    });
    gBrowser.preloadedBrowser = browser;

    let panel = gBrowser.getPanel(browser);
    gBrowser.tabpanels.appendChild(panel);

    return browser;
  },

  /**
   * Move the contents of a preload browser across to a different window.
   */
  _adoptBrowserFromOtherWindow(window) {
    let winPrivate = lazy.PrivateBrowsingUtils.isWindowPrivate(window);
    let winAIWindow = lazy.AIWindow.isAIWindowActive(window);
    // Grab the least-recently-focused window with a preloaded browser:
    let oldWin = lazy.BrowserWindowTracker.orderedWindows
      .filter(w => {
        return (
          winPrivate == lazy.PrivateBrowsingUtils.isWindowPrivate(w) &&
          winAIWindow == lazy.AIWindow.isAIWindowActive(w) &&
          w.gBrowser &&
          w.gBrowser.preloadedBrowser
        );
      })
      .pop();
    if (!oldWin) {
      return null;
    }
    // Don't call getPreloadedBrowser because it'll consume the browser:
    let oldBrowser = oldWin.gBrowser.preloadedBrowser;
    oldWin.gBrowser.preloadedBrowser = null;

    let newBrowser = this._createBrowser(window);

    oldBrowser.swapBrowsers(newBrowser);

    newBrowser.permanentKey = oldBrowser.permanentKey;

    oldWin.gBrowser.getPanel(oldBrowser).remove();
    return newBrowser;
  },

  maybeCreatePreloadedBrowser(window) {
    // If we're not enabled, have already got one, are in a popup window, or the
    // window is minimized / occluded, don't bother creating a preload browser -
    // there's no point.
    if (
      !this.enabled ||
      window.gBrowser.preloadedBrowser ||
      !window.toolbar.visible ||
      window.document.hidden
    ) {
      return;
    }

    // Don't bother creating a preload browser if we're not in the top set of windows:
    let windowPrivate = lazy.PrivateBrowsingUtils.isWindowPrivate(window);
    let windowAIWindow = lazy.AIWindow.isAIWindowActive(window);
    let countKey = windowPrivate ? "private" : "normal";
    let topWindows = lazy.BrowserWindowTracker.orderedWindows.filter(
      w =>
        lazy.PrivateBrowsingUtils.isWindowPrivate(w) == windowPrivate &&
        lazy.AIWindow.isAIWindowActive(w) == windowAIWindow
    );
    if (topWindows.indexOf(window) >= this.MAX_COUNT) {
      return;
    }

    // If we're in the top set of windows, and we already have enough preloaded
    // tabs, don't create yet another one, just steal an existing one:
    if (this.browserCounts[countKey] >= this.MAX_COUNT) {
      let browser = this._adoptBrowserFromOtherWindow(window);
      // We can potentially get null here if we couldn't actually find another
      // browser to adopt from. This can be the case when there's a mix of
      // private and non-private windows, for instance.
      if (browser) {
        return;
      }
    }

    let browser = this._createBrowser(window);
    let tabURI = Services.io.newURI(window.BROWSER_NEW_TAB_URL);
    browser.loadURI(tabURI, {
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });
    browser.docShellIsActive = false;

    window.gURLBar.getBrowserState(browser).urlbarFocused = true;

    // Make sure the preloaded browser is loaded with desired zoom level
    window.FullZoom.onLocationChange(tabURI, false, browser);

    this.browserCounts[countKey]++;
  },

  getPreloadedBrowser(window) {
    if (!this.enabled) {
      return null;
    }

    // The preloaded browser might be null.
    let browser = window.gBrowser.preloadedBrowser;

    // Consume the browser.
    window.gBrowser.preloadedBrowser = null;

    // Attach the nsIFormFillController now that we know the browser
    // will be used. If we do that before and the preloaded browser
    // won't be consumed until shutdown then we leak a docShell.
    // Also, we do not need to take care of attaching nsIFormFillControllers
    // in the case that the browser is remote, as remote browsers take
    // care of that themselves.
    if (browser) {
      let countKey = lazy.PrivateBrowsingUtils.isWindowPrivate(window)
        ? "private"
        : "normal";
      this.browserCounts[countKey]--;
      browser.removeAttribute("preloadedState");
      browser.setAttribute("autocompletepopup", "PopupAutoComplete");
    }

    return browser;
  },

  removePreloadedBrowser(window) {
    let browser = this.getPreloadedBrowser(window);
    if (browser) {
      window.gBrowser.getPanel(browser).remove();
    }
  },
};

XPCOMUtils.defineLazyPreferenceGetter(
  NewTabPagePreloading,
  "prefEnabled",
  "browser.newtab.preload",
  true
);
XPCOMUtils.defineLazyPreferenceGetter(
  NewTabPagePreloading,
  "newTabEnabled",
  "browser.newtabpage.enabled",
  true
);
