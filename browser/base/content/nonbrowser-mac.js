/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var NonBrowserWindow = {
  delayedStartupTimeoutId: null,
  MAC_HIDDEN_WINDOW: "chrome://browser/content/hiddenWindowMac.xhtml",

  openBrowserWindowFromDockMenu(options = {}) {
    let existingWindow = BrowserWindowTracker.getTopWindow();
    options.openerWindow = existingWindow || window;
    let win = OpenBrowserWindow(options);
    win.addEventListener(
      "load",
      () => this.dockSupport.activateApplication(true),
      { once: true }
    );

    return win;
  },

  startup() {
    // Disable inappropriate commands / submenus
    var disabledItems = [
      "Browser:SavePage",
      "Browser:SendLink",
      "cmd_pageSetup",
      "cmd_print",
      "cmd_find",
      "cmd_findAgain",
      "viewToolbarsMenu",
      "viewSidebarMenuMenu",
      "Browser:Reload",
      "viewFullZoomMenu",
      "pageStyleMenu",
      "repair-text-encoding",
      "View:PageSource",
      "View:FullScreen",
      "enterFullScreenItem",
      "viewHistorySidebar",
      "Browser:AddBookmarkAs",
      "Browser:BookmarkAllTabs",
      "View:PageInfo",
      "History:UndoCloseTab",
      "menu_openFirefoxView",
    ];
    var element;

    for (let disabledItem of disabledItems) {
      element = document.getElementById(disabledItem);
      if (element) {
        element.setAttribute("disabled", "true");
      }
    }

    // Show menus that are only visible in non-browser windows
    let shownItems = ["menu_openLocation"];
    for (let shownItem of shownItems) {
      element = document.getElementById(shownItem);
      if (element) {
        element.removeAttribute("hidden");
      }
    }

    if (window.location.href == this.MAC_HIDDEN_WINDOW) {
      // If no windows are active (i.e. we're the hidden window), disable the
      // close, minimize and zoom menu commands as well.
      var hiddenWindowDisabledItems = [
        "cmd_close",
        "cmd_minimizeWindow",
        "zoomWindow",
      ];
      for (let hiddenWindowDisabledItem of hiddenWindowDisabledItems) {
        element = document.getElementById(hiddenWindowDisabledItem);
        if (element) {
          element.setAttribute("disabled", "true");
        }
      }

      // Also hide the window-list separator.
      element = document.getElementById("sep-window-list");
      element.hidden = true;

      // Setup the dock menu.
      let dockMenuElement = document.getElementById("menu_mac_dockmenu");
      if (dockMenuElement != null) {
        let nativeMenu = Cc[
          "@mozilla.org/widget/standalonenativemenu;1"
        ].createInstance(Ci.nsIStandaloneNativeMenu);

        try {
          nativeMenu.init(dockMenuElement);
          this.dockSupport.dockMenu = nativeMenu;
        } catch (e) {}
      }

      dockMenuElement.addEventListener("command", this);

      // Hide menuitems that don't apply to private contexts.
      if (PrivateBrowsingUtils.permanentPrivateBrowsing) {
        document.getElementById("macDockMenuNewWindow").hidden = true;
      }
      if (!PrivateBrowsingUtils.enabled) {
        // Disable shortcuts and options that require private browsing to be enabled.
        document.getElementById("macDockMenuNewPrivateWindow").hidden = true;
        // This mirrors browser-init.js, as the hidden window shares the main menubar.
        document.getElementById("Tools:PrivateBrowsing").hidden = true;
        document.getElementById("menu_newPrivateWindow").hidden = true;
        document.getElementById("key_privatebrowsing").remove();
      }
      if (BrowserUIUtils.quitShortcutDisabled) {
        document.getElementById("key_quitApplication").remove();
        document.getElementById("menu_FileQuitItem").removeAttribute("key");
      }
    }

    this.delayedStartupTimeoutId = setTimeout(() => this.delayedStartup(), 0);
  },

  delayedStartup() {
    this.delayedStartupTimeoutId = null;

    // initialise the offline listener
    BrowserOffline.init();

    // Initialize the private browsing UI only if window is private
    if (PrivateBrowsingUtils.isWindowPrivate(window)) {
      PrivateBrowsingUI.init(window);
    }
  },

  shutdown() {
    // If this is the hidden window being closed, release our reference to
    // the dock menu element to prevent leaks on shutdown
    if (window.location.href == this.MAC_HIDDEN_WINDOW) {
      this.dockSupport.dockMenu = null;
    }

    // If nonBrowserWindowDelayedStartup hasn't run yet, we have no work to do -
    // just cancel the pending timeout and return;
    if (this.delayedStartupTimeoutId) {
      clearTimeout(this.delayedStartupTimeoutId);
      return;
    }

    BrowserOffline.uninit();
  },

  handleEvent(event) {
    switch (event.type) {
      case "load":
        this.startup();
        break;
      case "unload":
        this.shutdown();
        break;
      case "command":
        if (event.target.id.startsWith("macDockMenuNew")) {
          let wantPrivate = event.target.id == "macDockMenuNewPrivateWindow";
          this.openBrowserWindowFromDockMenu(
            wantPrivate ? { private: true } : {}
          );
        }
        break;
    }
  },
};

addEventListener("load", NonBrowserWindow, false);
addEventListener("unload", NonBrowserWindow, false);
XPCOMUtils.defineLazyServiceGetter(
  NonBrowserWindow,
  "dockSupport",
  "@mozilla.org/widget/macdocksupport;1",
  Ci.nsIMacDockSupport
);
