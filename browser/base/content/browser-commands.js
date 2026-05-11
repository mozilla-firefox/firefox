/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var kSkipCacheFlags =
  Ci.nsIWebNavigation.LOAD_FLAGS_BYPASS_PROXY |
  Ci.nsIWebNavigation.LOAD_FLAGS_BYPASS_CACHE;

var BrowserCommands = {
  back(aEvent) {
    const where = BrowserUtils.whereToOpenLink(aEvent, false, true);

    if (where == "current") {
      try {
        gBrowser.goBack();
      } catch (ex) {}
    } else {
      duplicateTabIn(gBrowser.selectedTab, where, -1);
    }
  },

  forward(aEvent) {
    const where = BrowserUtils.whereToOpenLink(aEvent, false, true);

    if (where == "current") {
      try {
        gBrowser.goForward();
      } catch (ex) {}
    } else {
      duplicateTabIn(gBrowser.selectedTab, where, 1);
    }
  },

  handleBackspace() {
    switch (Services.prefs.getIntPref("browser.backspace_action")) {
      case 0:
        this.back();
        break;
      case 1:
        goDoCommand("cmd_scrollPageUp");
        break;
    }
  },

  handleShiftBackspace() {
    switch (Services.prefs.getIntPref("browser.backspace_action")) {
      case 0:
        this.forward();
        break;
      case 1:
        goDoCommand("cmd_scrollPageDown");
        break;
    }
  },

  gotoHistoryIndex(aEvent) {
    aEvent = BrowserUtils.getRootEvent(aEvent);

    const index = aEvent.target.getAttribute("index");
    if (!index) {
      return false;
    }

    const where = BrowserUtils.whereToOpenLink(aEvent);

    if (where == "current") {
      // Normal click. Go there in the current tab and update session history.

      try {
        gBrowser.gotoIndex(index);
      } catch (ex) {
        return false;
      }
      return true;
    }
    // Modified click. Go there in a new tab/window.

    const historyindex = aEvent.target.getAttribute("historyindex");
    duplicateTabIn(gBrowser.selectedTab, where, Number(historyindex));
    return true;
  },

  duplicateTab() {
    duplicateTabIn(gBrowser.selectedTab, "tab");
  },

  reloadOrDuplicate(aEvent) {
    aEvent = BrowserUtils.getRootEvent(aEvent);
    const accelKeyPressed =
      AppConstants.platform == "macosx" ? aEvent.metaKey : aEvent.ctrlKey;
    const backgroundTabModifier = aEvent.button == 1 || accelKeyPressed;

    if (aEvent.shiftKey && !backgroundTabModifier) {
      this.reloadSkipCache();
      return;
    }

    const where = BrowserUtils.whereToOpenLink(aEvent, false, true);
    if (where == "current") {
      this.reload();
    } else {
      duplicateTabIn(gBrowser.selectedTab, where);
    }
  },

  reload() {
    if (gBrowser.currentURI.schemeIs("view-source")) {
      // Bug 1167797: For view source, we always skip the cache
      this.reloadSkipCache();
      return;
    }
    gBrowser.reloadWithFlags(Ci.nsIWebNavigation.LOAD_FLAGS_NONE);
  },

  reloadSkipCache() {
    // Bypass proxy and cache.
    gBrowser.reloadWithFlags(kSkipCacheFlags);
  },

  stop() {
    gBrowser.webNavigation.stop(Ci.nsIWebNavigation.STOP_ALL);
  },

  home(aEvent) {
    if (aEvent?.button == 2) {
      // right-click: do nothing
      return;
    }

    const homePage = HomePage.get(window);
    let where = BrowserUtils.whereToOpenLink(aEvent, false, true);

    // Don't load the home page in pinned or hidden tabs (e.g. Firefox View).
    if (
      where == "current" &&
      (gBrowser?.selectedTab.pinned || gBrowser?.selectedTab.hidden)
    ) {
      where = "tab";
    }

    // openTrustedLinkIn in utilityOverlay.js doesn't handle loading multiple pages
    let notifyObservers;
    switch (where) {
      case "current":
        // If we're going to load an initial page in the current tab as the
        // home page, we set initialPageLoadedFromURLBar so that the URL
        // bar is cleared properly (even during a remoteness flip).
        if (isInitialPage(homePage)) {
          gBrowser.selectedBrowser.initialPageLoadedFromUserAction = homePage;
        }
        loadOneOrMoreURIs(homePage);
        if (isBlankPageURL(homePage)) {
          gURLBar.select();
        } else {
          gBrowser.selectedBrowser.focus();
        }
        notifyObservers = true;
        aEvent?.preventDefault();
        break;
      case "tabshifted":
      case "tab": {
        const urls = homePage.split("|");
        const loadInBackground = Services.prefs.getBoolPref(
          "browser.tabs.loadBookmarksInBackground",
          false
        );
        // The homepage observer event should only be triggered when the homepage opens
        // in the foreground. This is mostly to support the homepage changed by extension
        // doorhanger which doesn't currently support background pages. This may change in
        // bug 1438396.
        notifyObservers = !loadInBackground;
        gBrowser.loadTabs(urls, {
          inBackground: loadInBackground,
          triggeringPrincipal:
            Services.scriptSecurityManager.getSystemPrincipal(),
        });
        if (!loadInBackground) {
          if (isBlankPageURL(homePage)) {
            gURLBar.select();
          } else {
            gBrowser.selectedBrowser.focus();
          }
        }
        aEvent?.preventDefault();
        break;
      }
      case "window":
        // OpenBrowserWindow will trigger the observer event, so no need to do so here.
        notifyObservers = false;
        OpenBrowserWindow();
        aEvent?.preventDefault();
        break;
    }

    if (notifyObservers) {
      // A notification for when a user has triggered their homepage. This is used
      // to display a doorhanger explaining that an extension has modified the
      // homepage, if necessary. Observers are only notified if the homepage
      // becomes the active page.
      Services.obs.notifyObservers(null, "browser-open-homepage-start");
    }
  },

  openTab({ event, url } = {}) {
    let werePassedURL = !!url;
    url ??= BROWSER_NEW_TAB_URL;
    let searchClipboard =
      event?.button == 1 &&
      Services.prefs.getBoolPref("middlemouse.paste") &&
      gMiddleClickNewTabUsesPasteboard;

    let relatedToCurrent = false;
    let where = "tab";

    if (event) {
      where = BrowserUtils.whereToOpenLink(event, false, true);

      switch (where) {
        case "tab":
        case "tabshifted":
          // When accel-click or middle-click are used, open the new tab as
          // related to the current tab.
          relatedToCurrent = true;
          break;
        case "current":
          where = "tab";
          break;
      }
    }

    // A notification intended to be useful for modular peformance tracking
    // starting as close as is reasonably possible to the time when the user
    // expressed the intent to open a new tab.  Since there are a lot of
    // entry points, this won't catch every single tab created, but most
    // initiated by the user should go through here.
    //
    // Note 1: This notification gets notified with a promise that resolves
    //         with the linked browser when the tab gets created
    // Note 2: This is also used to notify a user that an extension has changed
    //         the New Tab page.
    Services.obs.notifyObservers(
      {
        wrappedJSObject: new Promise(resolve => {
          let options = {
            relatedToCurrent,
            resolveOnNewTabCreated: resolve,
          };
          if (!werePassedURL && searchClipboard) {
            let clipboard = readFromClipboard();
            clipboard =
              UrlbarUtils.stripUnsafeProtocolOnPaste(clipboard).trim();
            if (clipboard) {
              url = clipboard;
              options.allowThirdPartyFixup = true;
            }
          }
          openTrustedLinkIn(url, where, options);
        }),
      },
      "browser-open-newtab-start"
    );
  },

  openFileWindow() {
    // The file picker is presented as a sheet attached to its parent
    // browsingContext's window. When this command is dispatched from a
    // non-browser chrome window (the macOS hidden menubar window when
    // all browsers are closed, or auxiliary windows like Library /
    // About when one of them holds the keyboard focus), we must
    // redirect the call to a real browser window — otherwise the
    // sheet either attaches off-screen (hidden window) or to the
    // wrong window (Library/About). Mirrors openLocation()'s pattern.
    if (window.location.href != AppConstants.BROWSER_CHROME_URL) {
      let targetWin = URILoadingHelper.getTargetWindow(window);
      if (targetWin) {
        targetWin.focus();
        targetWin.BrowserCommands.openFileWindow();
        return;
      }
      let newWin = window.openDialog(
        AppConstants.BROWSER_CHROME_URL,
        "_blank",
        "chrome,all,dialog=no",
        "about:blank"
      );
      newWin.addEventListener(
        "load",
        () => {
          newWin.focus();
          newWin.BrowserCommands.openFileWindow();
        },
        { once: true }
      );
      return;
    }

    // Get filepicker component.
    try {
      const nsIFilePicker = Ci.nsIFilePicker;
      const fp = Cc["@mozilla.org/filepicker;1"].createInstance(nsIFilePicker);
      const fpCallback = function fpCallback_done(aResult) {
        if (aResult == nsIFilePicker.returnOK) {
          try {
            if (fp.file) {
              gLastOpenDirectory.path = fp.file.parent.QueryInterface(
                Ci.nsIFile
              );
            }
          } catch (ex) {}
          openTrustedLinkIn(fp.fileURL.spec, "current");
        }
      };

      fp.init(
        window.browsingContext,
        gNavigatorBundle.getString("openFile"),
        nsIFilePicker.modeOpen
      );
      fp.appendFilters(
        nsIFilePicker.filterAll |
          nsIFilePicker.filterText |
          nsIFilePicker.filterImages |
          nsIFilePicker.filterXML |
          nsIFilePicker.filterHTML |
          nsIFilePicker.filterPDF
      );
      fp.displayDirectory = gLastOpenDirectory.path;
      fp.open(fpCallback);
    } catch (ex) {}
  },

  closeTabOrWindow(event) {
    // If we're not a browser window, just close the window.
    if (window.location.href != AppConstants.BROWSER_CHROME_URL) {
      closeWindow(true);
      return;
    }

    // In a multi-select context, close all selected tabs
    if (gBrowser.multiSelectedTabsCount) {
      gBrowser.removeMultiSelectedTabs(
        gBrowser.TabMetrics.userTriggeredContext()
      );
      return;
    }

    // Keyboard shortcuts that would close a tab that is pinned select the first
    // unpinned tab instead.
    if (
      event &&
      (event.ctrlKey || event.metaKey || event.altKey) &&
      gBrowser.selectedTab.pinned
    ) {
      if (gBrowser.visibleTabs.length > gBrowser.pinnedTabCount) {
        gBrowser.tabContainer.selectedIndex = gBrowser.pinnedTabCount;
      }
      return;
    }

    // If the current tab is the last one, this will close the window.
    gBrowser.removeCurrentTab({
      animate: true,
      ...gBrowser.TabMetrics.userTriggeredContext(),
    });
  },

  tryToCloseWindow(event) {
    if (WindowIsClosing(event)) {
      window.close();
    } // WindowIsClosing does all the necessary checks
  },

  /**
   * This is part of the Document Picture-in-Picture API.
   * Close this window and bring the opener tab back to foreground.
   *
   * @param event
   *        Used to determine how close was triggered.
   */
  returnToOpenerFromPiP(event) {
    // Switch to and focus the opener
    const openerBC = gBrowser.selectedBrowser.browsingContext.opener;
    const openerBrowser = openerBC.embedderElement;
    const openerWindow = openerBrowser.documentGlobal;
    const openerTab = openerWindow.gBrowser.getTabForBrowser(openerBrowser);
    openerWindow.gBrowser.selectedTab = openerTab;
    openerWindow.focus();

    this.tryToCloseWindow(event);
  },

  /**
   * Open the View Source dialog.
   *
   * @param args
   *        An object with the following properties:
   *
   *        URL (required):
   *          A string URL for the page we'd like to view the source of.
   *        browser (optional):
   *          The browser containing the document that we would like to view the
   *          source of. This is required if outerWindowID is passed.
   *        outerWindowID (optional):
   *          The outerWindowID of the content window containing the document that
   *          we want to view the source of. You only need to provide this if you
   *          want to attempt to retrieve the document source from the network
   *          cache.
   *        lineNumber (optional):
   *          The line number to focus on once the source is loaded.
   */
  async viewSourceOfDocument(args) {
    // Check if external view source is enabled.  If so, try it.  If it fails,
    // fallback to internal view source.
    if (Services.prefs.getBoolPref("view_source.editor.external")) {
      try {
        await top.gViewSourceUtils.openInExternalEditor(args);
        return;
      } catch (data) {}
    }

    let tabBrowser = gBrowser;
    let preferredRemoteType;
    let initialBrowsingContextGroupId;
    if (args.browser) {
      preferredRemoteType = args.browser.remoteType;
      initialBrowsingContextGroupId = args.browser.browsingContext.group.id;
    } else {
      if (!tabBrowser) {
        throw new Error(
          "viewSourceOfDocument should be passed the " +
            "subject browser if called from a window without " +
            "gBrowser defined."
        );
      }
      // Some internal URLs (such as specific chrome: and about: URLs that are
      // not yet remote ready) cannot be loaded in a remote browser.  View
      // source in tab expects the new view source browser's remoteness to match
      // that of the original URL, so disable remoteness if necessary for this
      // URL.
      const oa = E10SUtils.predictOriginAttributes({ window });
      preferredRemoteType = E10SUtils.getRemoteTypeForURI(
        args.URL,
        gMultiProcessBrowser,
        gFissionBrowser,
        E10SUtils.DEFAULT_REMOTE_TYPE,
        null,
        oa
      );
    }

    // In the case of popups, we need to find a non-popup browser window.
    if (!tabBrowser || !window.toolbar.visible) {
      // This returns only non-popup browser windows by default.
      const browserWindow =
        BrowserWindowTracker.getTopWindow() ??
        (await BrowserWindowTracker.promiseOpenWindow());
      tabBrowser = browserWindow.gBrowser;
    }

    const inNewWindow = !Services.prefs.getBoolPref("view_source.tab");

    // `viewSourceInBrowser` will load the source content from the page
    // descriptor for the tab (when possible) or fallback to the network if
    // that fails.  Either way, the view source module will manage the tab's
    // location, so use "about:blank" here to avoid unnecessary redundant
    // requests.
    const tab = tabBrowser.addTab("about:blank", {
      relatedToCurrent: true,
      inBackground: inNewWindow,
      skipAnimation: inNewWindow,
      preferredRemoteType,
      initialBrowsingContextGroupId,
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
      skipLoad: true,
    });
    args.viewSourceBrowser = tabBrowser.getBrowserForTab(tab);
    top.gViewSourceUtils.viewSourceInBrowser(args);

    if (inNewWindow) {
      tabBrowser.hideTab(tab);
      tabBrowser.replaceTabWithWindow(tab);
    }
  },

  /**
   * Opens the View Source dialog for the source loaded in the root
   * top-level document of the browser. This is really just a
   * convenience wrapper around viewSourceOfDocument.
   *
   * @param browser
   *        The browser that we want to load the source of.
   */
  viewSource(browser) {
    this.viewSourceOfDocument({
      browser,
      outerWindowID: browser.outerWindowID,
      URL: browser.currentURI.spec,
    });
  },

  /**
   * @param documentURL URL of the document to view, or null for this window's document
   * @param initialTab name of the initial tab to display, or null for the first tab
   * @param imageElement image to load in the Media Tab of the Page Info window; can be null/omitted
   * @param browsingContext the browsingContext of the frame that we want to view information about; can be null/omitted
   * @param browser the browser containing the document we're interested in inspecting; can be null/omitted
   */
  pageInfo(documentURL, initialTab, imageElement, browsingContext, browser) {
    const args = { initialTab, imageElement, browsingContext, browser };

    documentURL =
      documentURL || window.gBrowser.selectedBrowser.currentURI.spec;

    const isPrivate = PrivateBrowsingUtils.isWindowPrivate(window);

    // Check for windows matching the url
    for (const currentWindow of Services.wm.getEnumerator(
      "Browser:page-info"
    )) {
      if (currentWindow.closed) {
        continue;
      }
      if (
        currentWindow.document.documentElement.getAttribute("relatedUrl") ==
          documentURL &&
        PrivateBrowsingUtils.isWindowPrivate(currentWindow) == isPrivate
      ) {
        currentWindow.focus();
        currentWindow.resetPageInfo(args);
        return currentWindow;
      }
    }

    // We didn't find a matching window, so open a new one.
    let options = "chrome,toolbar,dialog=no,resizable";

    // Ensure the window groups correctly in the Windows taskbar
    if (isPrivate) {
      options += ",private";
    }
    return openDialog(
      "chrome://browser/content/pageinfo/pageInfo.xhtml",
      "",
      options,
      args
    );
  },

  fullScreen() {
    window.fullScreen = !window.fullScreen || BrowserHandler.kiosk;
  },

  downloadsUI() {
    if (PrivateBrowsingUtils.isWindowPrivate(window)) {
      openTrustedLinkIn("about:downloads", "tab");
    } else {
      PlacesCommandHook.showPlacesOrganizer("Downloads");
    }
  },

  forceEncodingDetection() {
    gBrowser.selectedBrowser.forceEncodingDetection();
    gBrowser.reloadWithFlags(Ci.nsIWebNavigation.LOAD_FLAGS_CHARSET_CHANGE);
  },

  processCloseRequest() {
    gBrowser.selectedBrowser.processCloseRequest();
  },
};
