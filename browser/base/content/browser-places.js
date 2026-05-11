/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "NEWTAB_ENABLED",
  "browser.newtabpage.enabled",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "SHOW_OTHER_BOOKMARKS",
  "browser.toolbars.bookmarks.showOtherBookmarks",
  true,
  () => {
    BookmarkingUI.maybeShowOtherBookmarksFolder().then(() => {
      document
        .getElementById("PlacesToolbar")
        ?._placesView?.updateNodesVisibility();
    }, console.error);
  }
);

// Set by sync after syncing bookmarks successfully once.
XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "SHOW_MOBILE_BOOKMARKS",
  "browser.bookmarks.showMobileBookmarks",
  false
);

ChromeUtils.defineESModuleGetters(this, {
  PanelMultiView:
    "moz-src:///browser/components/customizableui/PanelMultiView.sys.mjs",
  RecentlyClosedTabsAndWindowsMenuUtils:
    "resource:///modules/sessionstore/RecentlyClosedTabsAndWindowsMenuUtils.sys.mjs",
});

var StarUI = {
  userHasTags: undefined,
  _itemGuids: null,
  _isNewBookmark: false,
  _isComposing: false,
  _autoCloseTimer: 0,
  // The autoclose timer is diasbled if the user interacts with the
  // popup, such as making a change through typing or clicking on
  // the popup.
  _autoCloseTimerEnabled: true,
  // The autoclose timeout length.
  _autoCloseTimeout: 3500,
  _removeBookmarksOnPopupHidden: false,

  _element(aID) {
    return document.getElementById(aID);
  },

  // Edit-bookmark panel
  get panel() {
    delete this.panel;
    this._createPanelIfNeeded();
    var element = this._element("editBookmarkPanel");
    // initially the panel is hidden
    // to avoid impacting startup / new window performance
    element.hidden = false;
    element.addEventListener("keypress", this, { mozSystemGroup: true });
    element.addEventListener("mousedown", this);
    element.addEventListener("mouseout", this);
    element.addEventListener("mousemove", this);
    element.addEventListener("compositionstart", this);
    element.addEventListener("compositionend", this);
    element.addEventListener("input", this);
    element.addEventListener("popuphidden", this);
    element.addEventListener("popupshown", this);
    return (this.panel = element);
  },

  // nsIDOMEventListener
  handleEvent(aEvent) {
    switch (aEvent.type) {
      case "mousemove":
        clearTimeout(this._autoCloseTimer);
        // The autoclose timer is not disabled on generic mouseout
        // because the user may not have actually interacted with the popup.
        break;
      case "popuphidden": {
        clearTimeout(this._autoCloseTimer);
        if (aEvent.originalTarget == this.panel) {
          this._handlePopupHiddenEvent().catch(console.error);
        }
        break;
      }
      case "keypress":
        clearTimeout(this._autoCloseTimer);
        this._autoCloseTimerEnabled = false;

        if (aEvent.defaultPrevented) {
          // The event has already been consumed inside of the panel.
          break;
        }

        switch (aEvent.keyCode) {
          case KeyEvent.DOM_VK_ESCAPE:
            if (this._isNewBookmark) {
              this._removeBookmarksOnPopupHidden = true;
            } else {
              this._cancelOnPopupHidden = true;
            }
            this.panel.hidePopup();
            break;
          case KeyEvent.DOM_VK_RETURN:
            if (
              aEvent.target.classList.contains("expander-up") ||
              aEvent.target.classList.contains("expander-down") ||
              aEvent.target.id == "editBMPanel_newFolderButton" ||
              aEvent.target.id == "editBookmarkPanelRemoveButton"
            ) {
              // XXX Why is this necessary? The defaultPrevented check should
              //    be enough.
              break;
            }
            this.panel.hidePopup();
            break;
          // This case is for catching character-generating keypresses
          case 0: {
            let accessKey = document.getElementById("key_close");
            if (eventMatchesKey(aEvent, accessKey)) {
              this.panel.hidePopup();
            }
            break;
          }
        }
        break;
      case "compositionend":
        // After composition is committed, "mouseout" or something can set
        // auto close timer.
        this._isComposing = false;
        break;
      case "compositionstart":
        if (aEvent.defaultPrevented) {
          // If the composition was canceled, nothing to do here.
          break;
        }
        this._isComposing = true;
      // Explicit fall-through, during composition, panel shouldn't be hidden automatically.
      case "input":
      // Might have edited some text without keyboard events nor composition
      // events. Fall-through to cancel auto close in such case.
      case "mousedown":
        clearTimeout(this._autoCloseTimer);
        this._autoCloseTimerEnabled = false;
        break;
      case "mouseout":
        if (!this._autoCloseTimerEnabled) {
          // Don't autoclose the popup if the user has made a selection
          // or keypress and then subsequently mouseout.
          break;
        }
      // Explicit fall-through
      case "popupshown":
        // Don't handle events for descendent elements.
        if (aEvent.target != aEvent.currentTarget) {
          break;
        }
        // auto-close if new and not interacted with
        if (this._isNewBookmark && !this._isComposing) {
          let delay = this._autoCloseTimeout;
          if (this._closePanelQuickForTesting) {
            delay /= 10;
          }
          clearTimeout(this._autoCloseTimer);
          this._autoCloseTimer = setTimeout(() => {
            if (!this.panel.matches(":hover")) {
              this.panel.hidePopup(true);
            }
          }, delay);
          this._autoCloseTimerEnabled = true;
        }
        break;
    }
  },

  /**
   * Handle popup hidden event.
   */
  async _handlePopupHiddenEvent() {
    const { bookmarkState, didChangeFolder, selectedFolderGuid } =
      gEditItemOverlay;
    gEditItemOverlay.uninitPanel(true);

    if (this._cancelOnPopupHidden) {
      this._cancelOnPopupHidden = false;
      return;
    }

    // Capture _removeBookmarksOnPopupHidden and _itemGuids values. Reset them
    // before we handle the next popup.
    const removeBookmarksOnPopupHidden = this._removeBookmarksOnPopupHidden;
    this._removeBookmarksOnPopupHidden = false;
    const guidsForRemoval = this._itemGuids;
    this._itemGuids = null;

    if (removeBookmarksOnPopupHidden && guidsForRemoval) {
      if (!this._isNewBookmark) {
        // Remove all bookmarks for the bookmark's url, this also removes
        // the tags for the url.
        await PlacesTransactions.Remove(guidsForRemoval).transact();
      } else {
        BookmarkingUI.star.removeAttribute("starred");
      }
      return;
    }

    Services.prefs.setBoolPref(
      "browser.bookmarks.editDialog.showForNewBookmarks",
      this._element("editBookmarkPanel_showForNewBookmarks").checked
    );
    await this._storeRecentlyUsedFolder(selectedFolderGuid, didChangeFolder);
    await bookmarkState.save();

    if (this._isNewBookmark) {
      this.showConfirmation();
    }
  },

  async showEditBookmarkPopup(aNode, aIsNewBookmark, aUrl) {
    // Slow double-clicks (not true double-clicks) shouldn't
    // cause the panel to flicker.
    if (this.panel.state != "closed") {
      return;
    }

    this._isNewBookmark = aIsNewBookmark;
    this._itemGuids = null;

    let titleL10nID = this._isNewBookmark
      ? "bookmarks-add-bookmark"
      : "bookmarks-edit-bookmark";
    document.l10n.setAttributes(
      this._element("editBookmarkPanelTitle"),
      titleL10nID
    );

    this._element("editBookmarkPanel_showForNewBookmarks").checked =
      this.showForNewBookmarks;

    this._itemGuids = [];
    await PlacesUtils.bookmarks.fetch({ url: aUrl }, bookmark =>
      this._itemGuids.push(bookmark.guid)
    );

    let removeButton = this._element("editBookmarkPanelRemoveButton");
    if (this._isNewBookmark) {
      document.l10n.setAttributes(removeButton, "bookmark-panel-cancel");
    } else {
      // The label of the remove button differs if the URI is bookmarked
      // multiple times.
      document.l10n.setAttributes(removeButton, "bookmark-panel-remove", {
        count: this._itemGuids.length,
      });
    }

    let onPanelReady = fn => {
      let target = this.panel;
      if (target.parentNode) {
        // By targeting the panel's parent and using a capturing listener, we
        // can have our listener called before others waiting for the panel to
        // be shown (which probably expect the panel to be fully initialized)
        target = target.parentNode;
      }
      target.addEventListener(
        "popupshown",
        function () {
          fn();
        },
        { capture: true, once: true }
      );
    };

    let hiddenRows = ["location", "keyword"];

    if (this.userHasTags === undefined) {
      // Cache must be initialized
      const fetchedTags = await PlacesUtils.bookmarks.fetchTags();
      this.userHasTags = !!fetchedTags.length;
    }

    if (!this.userHasTags) {
      // Hide tags ui because user has no tags defined
      hiddenRows.push("tags");
    }

    await gEditItemOverlay.initPanel({
      node: aNode,
      onPanelReady,
      hiddenRows,
      focusedElement: "preferred",
      isNewBookmark: this._isNewBookmark,
    });

    this.panel.openPopup(BookmarkingUI.anchor, "bottomright topright");
  },

  _createPanelIfNeeded() {
    // Lazy load the editBookmarkPanel the first time we need to display it.
    if (!this._element("editBookmarkPanel")) {
      MozXULElement.insertFTLIfNeeded("browser/editBookmarkOverlay.ftl");
      let template = this._element("editBookmarkPanelTemplate");
      let clone = template.content.cloneNode(true);
      template.replaceWith(clone);
    }
  },

  removeBookmarkButtonCommand: function SU_removeBookmarkButtonCommand() {
    this._removeBookmarksOnPopupHidden = true;
    this.panel.hidePopup();
  },

  async _storeRecentlyUsedFolder(selectedFolderGuid, didChangeFolder) {
    if (!selectedFolderGuid) {
      return;
    }

    // If we're changing where a bookmark gets saved, persist that location.
    if (didChangeFolder) {
      Services.prefs.setCharPref(
        "browser.bookmarks.defaultLocation",
        selectedFolderGuid
      );
    }

    // Don't store folders that are always displayed in "Recent Folders".
    if (PlacesUtils.bookmarks.userContentRoots.includes(selectedFolderGuid)) {
      return;
    }

    // List of recently used folders:
    let lastUsedFolderGuids = await PlacesUtils.metadata.get(
      PlacesUIUtils.LAST_USED_FOLDERS_META_KEY,
      []
    );

    let index = lastUsedFolderGuids.indexOf(selectedFolderGuid);
    if (index > 1) {
      // The guid is in the array but not the most recent.
      lastUsedFolderGuids.splice(index, 1);
      lastUsedFolderGuids.unshift(selectedFolderGuid);
    } else if (index == -1) {
      lastUsedFolderGuids.unshift(selectedFolderGuid);
    }
    while (lastUsedFolderGuids.length > PlacesUIUtils.maxRecentFolders) {
      lastUsedFolderGuids.pop();
    }

    await PlacesUtils.metadata.set(
      PlacesUIUtils.LAST_USED_FOLDERS_META_KEY,
      lastUsedFolderGuids
    );
  },

  showConfirmation() {
    // Show the "Saved to bookmarks" hint for the first three times
    const HINT_COUNT_PREF =
      "browser.bookmarks.editDialog.confirmationHintShowCount";
    const HINT_COUNT = Services.prefs.getIntPref(HINT_COUNT_PREF, 0);

    if (HINT_COUNT >= 3) {
      return;
    }
    Services.prefs.setIntPref(HINT_COUNT_PREF, HINT_COUNT + 1);

    let anchor;
    if (window.toolbar.visible) {
      for (let id of ["library-button", "bookmarks-menu-button"]) {
        let element = document.getElementById(id);
        if (
          element &&
          element.getAttribute("cui-areatype") != "panel" &&
          element.getAttribute("overflowedItem") != "true"
        ) {
          anchor = element;
          break;
        }
      }
    }
    if (!anchor) {
      anchor = document.getElementById("PanelUI-menu-button");
    }
    ConfirmationHint.show(anchor, "confirmation-hint-page-bookmarked");
  },
};

XPCOMUtils.defineLazyPreferenceGetter(
  StarUI,
  "showForNewBookmarks",
  "browser.bookmarks.editDialog.showForNewBookmarks"
);

var PlacesCommandHook = {
  /**
   * Adds a bookmark to the page loaded in the current browser.
   */
  async bookmarkPage() {
    let browser = gBrowser.selectedBrowser;
    let url = URL.fromURI(Services.io.createExposableURI(browser.currentURI));
    let info = await PlacesUtils.bookmarks.fetch({ url });
    let isNewBookmark = !info;
    let showEditUI = !isNewBookmark || StarUI.showForNewBookmarks;
    if (isNewBookmark) {
      // This is async because we have to validate the guid
      // coming from prefs.
      let parentGuid = await PlacesUIUtils.defaultParentGuid;
      info = { url, parentGuid };
      // Bug 1148838 - Make this code work for full page plugins.
      let charset = null;

      let isErrorPage = false;
      if (browser.documentURI) {
        isErrorPage = /^about:(neterror|certerror|blocked)/.test(
          browser.documentURI.spec
        );
      }

      try {
        if (isErrorPage) {
          let entry = await PlacesUtils.history.fetch(browser.currentURI);
          if (entry) {
            info.title = entry.title;
          }
        } else {
          info.title = browser.contentTitle;
        }
        info.title = info.title || url.href;
        charset = browser.characterSet;
      } catch (e) {
        console.error(e);
      }

      if (!StarUI.showForNewBookmarks) {
        info.guid = await PlacesTransactions.NewBookmark(info).transact();
      } else {
        info.guid = PlacesUtils.bookmarks.unsavedGuid;
        BookmarkingUI.star.setAttribute("starred", "true");
      }

      if (charset) {
        PlacesUIUtils.setCharsetForPage(url, charset, window).catch(
          console.error
        );
      }
    }

    // Revert the contents of the location bar
    gURLBar.handleRevert();

    // If it was not requested to open directly in "edit" mode, we are done.
    if (!showEditUI) {
      StarUI.showConfirmation();
      return;
    }

    let node = await PlacesUIUtils.promiseNodeLikeFromFetchInfo(info);

    await StarUI.showEditBookmarkPopup(node, isNewBookmark, url);
  },

  /**
   * Adds a bookmark to the page targeted by a link.
   *
   * @param url (string)
   *        the address of the link target
   * @param title
   *        The link text
   * @returns {string}
   *        The bookmark's guid if the dialog was confirmed, otherwise undefined.
   */
  async bookmarkLink(url, title) {
    let bm = await PlacesUtils.bookmarks.fetch({ url });
    let guid;
    if (bm) {
      let node = await PlacesUIUtils.promiseNodeLikeFromFetchInfo(bm);
      guid = await PlacesUIUtils.showBookmarkDialog(
        { action: "edit", node },
        window.top
      );
      return guid;
    }

    let parentGuid = await PlacesUIUtils.defaultParentGuid;
    let defaultInsertionPoint = new PlacesInsertionPoint({
      parentGuid,
    });
    guid = await PlacesUIUtils.showBookmarkDialog(
      {
        action: "add",
        type: "bookmark",
        uri: Services.io.newURI(url),
        title,
        defaultInsertionPoint,
        hiddenRows: ["location", "keyword"],
      },
      window.top
    );
    return guid;
  },

  /**
   * Bookmarks the given tabs loaded in the current browser.
   *
   * @param {Array} tabs
   *        If no given tabs, bookmark all current tabs.
   */
  async bookmarkTabs(tabs) {
    tabs = tabs ?? gBrowser.visibleTabs.filter(tab => !tab.pinned);
    let pages = PlacesCommandHook.getUniquePages(tabs).map(
      // Bookmark exposable url.
      page =>
        Object.assign(page, { uri: Services.io.createExposableURI(page.uri) })
    );
    await PlacesUIUtils.showBookmarkPagesDialog(pages);
  },

  /**
   * List of nsIURI objects characterizing tabs given in param.
   * Duplicates are discarded.
   */
  getUniquePages(tabs) {
    let uniquePages = {};
    let URIs = [];

    tabs.forEach(tab => {
      let browser = tab.linkedBrowser;
      let uri = browser.currentURI;
      let title = browser.contentTitle || tab.label;
      let spec = uri.spec;
      if (!(spec in uniquePages)) {
        uniquePages[spec] = null;
        URIs.push({ uri, title });
      }
    });
    return URIs;
  },

  /**
   * Opens the Places Organizer.
   *
   * @param {string} item The item to select in the organizer window,
   *                      options are (case sensitive):
   *                      BookmarksMenu, BookmarksToolbar, UnfiledBookmarks,
   *                      AllBookmarks, History, Downloads.
   */
  showPlacesOrganizer(item) {
    var organizer = Services.wm.getMostRecentWindow("Places:Organizer");
    // Due to bug 528706, getMostRecentWindow can return closed windows.
    if (!organizer || organizer.closed) {
      // No currently open places window, so open one with the specified mode.
      openDialog(
        "chrome://browser/content/places/places.xhtml",
        "",
        "chrome,toolbar=yes,dialog=no,resizable",
        item
      );
    } else {
      organizer.PlacesOrganizer.selectLeftPaneContainerByHierarchy(item);
      organizer.focus();
    }
  },

  async searchBookmarks() {
    let win =
      BrowserWindowTracker.getTopWindow() ??
      (await BrowserWindowTracker.promiseOpenWindow());
    win.gURLBar.search(UrlbarTokenizer.RESTRICT.BOOKMARK, {
      searchModeEntry: "bookmarkmenu",
    });
  },

  async searchHistory() {
    let win =
      BrowserWindowTracker.getTopWindow() ??
      (await BrowserWindowTracker.promiseOpenWindow());
    win.gURLBar.search(UrlbarTokenizer.RESTRICT.HISTORY, {
      searchModeEntry: "historymenu",
    });
  },
};

// View for the history menu.
class HistoryMenu extends PlacesMenu {
  constructor(aPopupShowingEvent) {
    super(aPopupShowingEvent, "place:sort=4&maxResults=15");
  }

  // Called by the base class (PlacesViewBase) so we can initialize some
  // element references before the several superclass constructors call our
  // methods which depend on these.
  _init() {
    super._init();
    let elements = {
      undoTabMenu: "historyUndoMenu",
      hiddenTabsMenu: "hiddenTabsMenu",
      undoWindowMenu: "historyUndoWindowMenu",
      syncTabsMenuitem: "sync-tabs-menuitem",
    };
    for (let [key, elemId] of Object.entries(elements)) {
      this[key] = document.getElementById(elemId);
    }
  }

  toggleHiddenTabs() {
    const isShown =
      window.gBrowser && gBrowser.visibleTabs.length < gBrowser.tabs.length;
    this.hiddenTabsMenu.hidden = !isShown;
  }

  toggleRecentlyClosedTabs() {
    // enable/disable the Recently Closed Tabs sub menu
    // no restorable tabs, so disable menu
    if (SessionStore.getClosedTabCount() == 0) {
      this.undoTabMenu.setAttribute("disabled", true);
    } else {
      this.undoTabMenu.removeAttribute("disabled");
    }
  }

  /**
   * Populate when the history menu is opened
   */
  populateUndoSubmenu() {
    var undoPopup = this.undoTabMenu.menupopup;

    // remove existing menu items
    while (undoPopup.hasChildNodes()) {
      undoPopup.firstChild.remove();
    }

    // no restorable tabs, so make sure menu is disabled, and return
    if (SessionStore.getClosedTabCount() == 0) {
      this.undoTabMenu.setAttribute("disabled", true);
      return;
    }

    // enable menu
    this.undoTabMenu.removeAttribute("disabled");

    // populate menu
    let tabsFragment = RecentlyClosedTabsAndWindowsMenuUtils.getTabsFragment(
      window,
      "menuitem"
    );
    undoPopup.appendChild(tabsFragment);
  }

  toggleRecentlyClosedWindows() {
    // enable/disable the Recently Closed Windows sub menu
    // no restorable windows, so disable menu
    if (SessionStore.getClosedWindowCount() == 0) {
      this.undoWindowMenu.setAttribute("disabled", true);
    } else {
      this.undoWindowMenu.removeAttribute("disabled");
    }
  }

  /**
   * Populate when the history menu is opened
   */
  populateUndoWindowSubmenu() {
    let undoPopup = this.undoWindowMenu.menupopup;

    // remove existing menu items
    while (undoPopup.hasChildNodes()) {
      undoPopup.firstChild.remove();
    }

    // no restorable windows, so make sure menu is disabled, and return
    if (SessionStore.getClosedWindowCount() == 0) {
      this.undoWindowMenu.setAttribute("disabled", true);
      return;
    }

    // enable menu
    this.undoWindowMenu.removeAttribute("disabled");

    // populate menu
    let windowsFragment =
      RecentlyClosedTabsAndWindowsMenuUtils.getWindowsFragment(
        window,
        "menuitem",
        /* aPrefixRestoreAll = */ false
      );
    undoPopup.appendChild(windowsFragment);
  }

  toggleTabsFromOtherComputers() {
    // Enable/disable the Tabs From Other Computers menu. Some of the menus handled
    // by HistoryMenu do not have this menuitem.
    if (!this.syncTabsMenuitem) {
      return;
    }

    if (!PlacesUIUtils.shouldShowTabsFromOtherComputersMenuitem()) {
      this.syncTabsMenuitem.hidden = true;
      return;
    }

    this.syncTabsMenuitem.hidden = false;
  }

  _onPopupShowing(aEvent) {
    super._onPopupShowing(aEvent);

    // Don't handle events for submenus.
    if (aEvent.target != this.rootElement) {
      return;
    }

    this.toggleHiddenTabs();
    this.toggleRecentlyClosedTabs();
    this.toggleRecentlyClosedWindows();
    this.toggleTabsFromOtherComputers();
  }

  _onCommand(aEvent) {
    aEvent = BrowserUtils.getRootEvent(aEvent);
    let placesNode = aEvent.target._placesNode;
    if (placesNode) {
      if (!PrivateBrowsingUtils.isWindowPrivate(window)) {
        PlacesUIUtils.markPageAsTyped(placesNode.uri);
      }
      openUILink(placesNode.uri, aEvent, {
        ignoreAlt: true,
        triggeringPrincipal:
          Services.scriptSecurityManager.getSystemPrincipal(),
      });
    }
  }
}

/**
 * Functions for handling events in the Bookmarks Toolbar and menu.
 */
var BookmarksEventHandler = {
  /**
   * Handler for click event for an item in the bookmarks toolbar or menu.
   * Menus and submenus from the folder buttons bubble up to this handler.
   * Left-click is handled in the onCommand function.
   * When items are middle-clicked (or clicked with modifier), open in tabs.
   * If the click came through a menu, close the menu.
   *
   * @param aEvent
   *        DOMEvent for the click
   * @param aView
   *        The places view which aEvent should be associated with.
   */

  onMouseUp(aEvent) {
    // Handles middle-click or left-click with modifier if not browser.bookmarks.openInTabClosesMenu.
    if (aEvent.button == 2 || PlacesUIUtils.openInTabClosesMenu) {
      return;
    }
    let target = aEvent.originalTarget;
    if (target.tagName != "menuitem") {
      return;
    }
    let modifKey =
      AppConstants.platform === "macosx" ? aEvent.metaKey : aEvent.ctrlKey;
    if (modifKey || aEvent.button == 1) {
      target.setAttribute("closemenu", "none");
      var menupopup = target.parentNode;
      menupopup.addEventListener(
        "popuphidden",
        () => {
          target.removeAttribute("closemenu");
        },
        { once: true }
      );
    } else {
      // Handles edge case where same menuitem was opened previously
      // while menu was kept open, but now menu should close.
      target.removeAttribute("closemenu");
    }
  },

  onClick: function BEH_onClick(aEvent, aView) {
    // Only handle middle-click or left-click with modifiers.
    let modifKey;
    if (AppConstants.platform == "macosx") {
      modifKey = aEvent.metaKey || aEvent.shiftKey;
    } else {
      modifKey = aEvent.ctrlKey || aEvent.shiftKey;
    }

    if (aEvent.button == 2 || (aEvent.button == 0 && !modifKey)) {
      return;
    }

    var target = aEvent.originalTarget;
    // If this event bubbled up from a menu or menuitem,
    // close the menus if browser.bookmarks.openInTabClosesMenu.
    var tag = target.tagName;
    if (
      PlacesUIUtils.openInTabClosesMenu &&
      (tag == "menuitem" || tag == "menu")
    ) {
      closeMenus(aEvent.target);
    }

    if (target._placesNode && PlacesUtils.nodeIsContainer(target._placesNode)) {
      // Don't open the root folder in tabs when the empty area on the toolbar
      // is middle-clicked or when a non-bookmark item (except for Open in Tabs)
      // in a bookmarks menupopup is middle-clicked.
      if (target.localName == "menu" || target.localName == "toolbarbutton") {
        PlacesUIUtils.openMultipleLinksInTabs(
          target._placesNode,
          aEvent,
          aView
        );
      }
    } else if (aEvent.button == 1 && !(tag == "menuitem" || tag == "menu")) {
      // Call onCommand in the cases where it's not called automatically:
      // Middle-clicks outside of menus.
      this.onCommand(aEvent);
      aEvent.preventDefault();
      aEvent.stopPropagation();
    }
  },

  /**
   * Handler for command event for an item in the bookmarks toolbar.
   * Menus and submenus from the folder buttons bubble up to this handler.
   * Opens the item.
   *
   * @param aEvent
   *        DOMEvent for the command
   */
  onCommand: function BEH_onCommand(aEvent) {
    var target = aEvent.originalTarget;
    if (target._placesNode) {
      PlacesUIUtils.openNodeWithEvent(target._placesNode, aEvent);
      // Only record interactions through the Bookmarks Toolbar
      if (target.closest("#PersonalToolbar")) {
        Glean.browserEngagement.bookmarksToolbarBookmarkOpened.add(1);
      }
    }
  },

  fillInBHTooltip: function BEH_fillInBHTooltip(aTooltip, aEvent) {
    var node;
    var cropped = false;
    var targetURI;

    if (aTooltip.triggerNode.localName == "treechildren") {
      var tree = aTooltip.triggerNode.parentNode;
      var cell = tree.getCellAt(aEvent.clientX, aEvent.clientY);
      if (cell.row == -1) {
        aEvent.preventDefault();
        return;
      }
      node = tree.view.nodeForTreeIndex(cell.row);
      cropped = tree.isCellCropped(cell.row, cell.col);
    } else {
      // Check whether the tooltipNode is a Places node.
      // In such a case use it, otherwise check for targetURI attribute.
      var tooltipNode = aTooltip.triggerNode;
      if (tooltipNode._placesNode) {
        node = tooltipNode._placesNode;
      } else {
        // This is a static non-Places node.
        targetURI = tooltipNode.getAttribute("targetURI");
      }
    }

    if (!node && !targetURI) {
      aEvent.preventDefault();
      return;
    }

    // Show node.label as tooltip's title for non-Places nodes.
    var title = node ? node.title : tooltipNode.label;

    // Show URL only for Places URI-nodes or nodes with a targetURI attribute.
    var url;
    if (targetURI || PlacesUtils.nodeIsURI(node)) {
      url = targetURI || node.uri;
    }

    // Show tooltip for containers only if their title is cropped.
    if (!cropped && !url) {
      aEvent.preventDefault();
      return;
    }

    let tooltipTitle = aEvent.target.querySelector(".places-tooltip-title");
    tooltipTitle.hidden = !title || title == url;
    if (!tooltipTitle.hidden) {
      tooltipTitle.textContent = title;
    }

    let tooltipUrl = aEvent.target.querySelector(".places-tooltip-uri");
    tooltipUrl.hidden = !url;
    if (!tooltipUrl.hidden) {
      // Use `value` instead of `textContent` so cropping will apply
      tooltipUrl.value = url;
    }

    // Show tooltip.
  },
};

// Handles special drag and drop functionality for Places menus that are not
// part of a Places view (e.g. the bookmarks menu in the menubar).
var PlacesMenuDNDHandler = {
  _springLoadDelayMs: 350,
  _closeDelayMs: 500,
  _loadTimer: null,
  _closeTimer: null,
  _closingTimerNode: null,

  /**
   * Called when the user enters the <menu> element during a drag.
   *
   * @param   event
   *          The DragEnter event that spawned the opening.
   */
  onDragEnter: function PMDH_onDragEnter(event) {
    // Opening menus in a Places popup is handled by the view itself.
    if (!this._isStaticContainer(event.target)) {
      return;
    }

    // If we re-enter the same menu or anchor before the close timer runs out,
    // we should ensure that we do not close:
    if (this._closeTimer && this._closingTimerNode === event.currentTarget) {
      this._closeTimer.cancel();
      this._closingTimerNode = null;
      this._closeTimer = null;
    }

    PlacesControllerDragHelper.currentDropTarget = event.target;
    let popup = event.target.menupopup;
    if (
      this._loadTimer ||
      popup.state === "showing" ||
      popup.state === "open"
    ) {
      return;
    }

    this._loadTimer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    this._loadTimer.initWithCallback(
      () => {
        this._loadTimer = null;
        popup.setAttribute("autoopened", "true");
        popup.openPopup();
      },
      this._springLoadDelayMs,
      Ci.nsITimer.TYPE_ONE_SHOT
    );
    event.preventDefault();
    event.stopPropagation();
  },

  /**
   * Handles dragleave on the <menu> element.
   */
  onDragLeave: function PMDH_onDragLeave(event) {
    // Handle menu-button separate targets.
    if (
      event.relatedTarget === event.currentTarget ||
      (event.relatedTarget &&
        event.relatedTarget.parentNode === event.currentTarget)
    ) {
      return;
    }

    // Closing menus in a Places popup is handled by the view itself.
    if (!this._isStaticContainer(event.target)) {
      return;
    }

    PlacesControllerDragHelper.currentDropTarget = null;
    let popup = event.target.menupopup;

    if (this._loadTimer) {
      this._loadTimer.cancel();
      this._loadTimer = null;
    }
    this._closeTimer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    this._closingTimerNode = event.currentTarget;
    this._closeTimer.initWithCallback(
      function () {
        this._closeTimer = null;
        this._closingTimerNode = null;
        let node = PlacesControllerDragHelper.currentDropTarget;
        let inHierarchy = false;
        while (node && !inHierarchy) {
          inHierarchy = node == event.target;
          node = node.parentNode;
        }
        if (!inHierarchy && popup && popup.hasAttribute("autoopened")) {
          popup.removeAttribute("autoopened");
          popup.hidePopup();
        }
      },
      this._closeDelayMs,
      Ci.nsITimer.TYPE_ONE_SHOT
    );
  },

  /**
   * Determines if a XUL element represents a static container.
   *
   * @returns true if the element is a container element (menu or
   *`         menu-toolbarbutton), false otherwise.
   */
  _isStaticContainer: function PMDH__isContainer(node) {
    let isMenu =
      node.localName == "menu" ||
      (node.localName == "toolbarbutton" &&
        node.getAttribute("type") == "menu");
    let isStatic =
      !("_placesNode" in node) &&
      node.menupopup &&
      node.menupopup.hasAttribute("placespopup") &&
      !node.parentNode.hasAttribute("placespopup");
    return isMenu && isStatic;
  },

  /**
   * Called when the user drags over the <menu> element.
   *
   * @param   event
   *          The DragOver event.
   */
  onDragOver: function PMDH_onDragOver(event) {
    PlacesControllerDragHelper.currentDropTarget = event.target;
    let ip = new PlacesInsertionPoint({
      parentGuid: PlacesUtils.bookmarks.menuGuid,
    });
    if (ip && PlacesControllerDragHelper.canDrop(ip, event.dataTransfer)) {
      event.preventDefault();
    }

    event.stopPropagation();
  },

  /**
   * Called when the user drops on the <menu> element.
   *
   * @param   event
   *          The Drop event.
   */
  onDrop: function PMDH_onDrop(event) {
    // Put the item at the end of bookmark menu.
    let ip = new PlacesInsertionPoint({
      parentGuid: PlacesUtils.bookmarks.menuGuid,
    });
    PlacesControllerDragHelper.onDrop(ip, event.dataTransfer);
    PlacesControllerDragHelper.currentDropTarget = null;
    event.stopPropagation();
  },
};

/**
 * This object handles the initialization and uninitialization of the bookmarks
 * toolbar. It also has helper functions for the managed bookmarks button.
 */
var PlacesToolbarHelper = {
  get _viewElt() {
    return document.getElementById("PlacesToolbar");
  },

  /**
   * Initialize. This will check whether we've finished startup and can
   * show toolbars.
   */
  async init() {
    await PlacesUIUtils.canLoadToolbarContentPromise;
    this._realInit();
  },

  /**
   * Actually initialize the places view (if needed; we might still no-op).
   */
  _realInit() {
    let viewElt = this._viewElt;
    if (!viewElt || viewElt._placesView || window.closed) {
      return;
    }

    // CustomizableUI.addListener is idempotent, so we can safely
    // call this multiple times.
    CustomizableUI.addListener(this);

    if (!this._isObservingToolbars) {
      this._isObservingToolbars = true;
      window.addEventListener("toolbarvisibilitychange", this);
    }

    // If the bookmarks toolbar item is:
    // - not in a toolbar, or;
    // - the toolbar is collapsed, or;
    // - the toolbar is hidden some other way:
    // don't initialize.  Also, there is no need to initialize the toolbar if
    // customizing, because that will happen when the customization is done.
    let toolbar = this._getParentToolbar(viewElt);
    if (
      !toolbar ||
      toolbar.collapsed ||
      this._isCustomizing ||
      getComputedStyle(toolbar, "").display == "none"
    ) {
      return;
    }

    new PlacesToolbar(
      `place:parent=${PlacesUtils.bookmarks.toolbarGuid}`,
      document.getElementById("PlacesToolbarItems"),
      viewElt
    );

    if (toolbar.id == "PersonalToolbar") {
      // We just created a new view, thus we must check again the empty toolbar
      // message, regardless of "initialized".
      BookmarkingUI.updateEmptyToolbarMessage()
        .finally(() => {
          toolbar.toggleAttribute("initialized", true);
        })
        .catch(console.error);
    }
  },

  async getIsEmpty() {
    if (!this._viewElt._placesView) {
      return true;
    }
    await this._viewElt._placesView.promiseRebuilt();
    return !document.getElementById("PlacesToolbarItems").hasChildNodes();
  },

  handleEvent(event) {
    switch (event.type) {
      case "toolbarvisibilitychange":
        if (event.target == this._getParentToolbar(this._viewElt)) {
          this._resetView();
        }
        break;
    }
  },

  /**
   * This is a no-op if we haven't been initialized.
   */
  uninit: function PTH_uninit() {
    if (this._isObservingToolbars) {
      delete this._isObservingToolbars;
      window.removeEventListener("toolbarvisibilitychange", this);
    }
    CustomizableUI.removeListener(this);
  },

  customizeStart: function PTH_customizeStart() {
    try {
      let viewElt = this._viewElt;
      if (viewElt && viewElt._placesView) {
        viewElt._placesView.uninit();
      }
    } finally {
      this._isCustomizing = true;
    }
  },

  customizeDone: function PTH_customizeDone() {
    this._isCustomizing = false;
    this.init();
  },

  onPlaceholderCommand() {
    let widgetGroup = CustomizableUI.getWidget("personal-bookmarks");
    let widget = widgetGroup.forWindow(window);
    if (
      widget.overflowed ||
      widgetGroup.areaType == CustomizableUI.TYPE_PANEL
    ) {
      PlacesCommandHook.showPlacesOrganizer("BookmarksToolbar");
    }
  },

  _getParentToolbar(element) {
    while (element) {
      if (element.localName == "toolbar") {
        return element;
      }
      element = element.parentNode;
    }
    return null;
  },

  onWidgetUnderflow(aNode) {
    // The view gets broken by being removed and reinserted by the overflowable
    // toolbar, so we have to force an uninit and reinit.
    let win = aNode.documentGlobal;
    if (aNode.id == "personal-bookmarks" && win == window) {
      this._resetView();
    }
  },

  onWidgetAdded(aWidgetId) {
    if (aWidgetId == "personal-bookmarks" && !this._isCustomizing) {
      // It's possible (with the "Add to Menu", "Add to Toolbar" context
      // options) that the Places Toolbar Items have been moved without
      // letting us prepare and handle it with with customizeStart and
      // customizeDone. If that's the case, we need to reset the views
      // since they're probably broken from the DOM reparenting.
      this._resetView();
    }
  },

  _resetView() {
    if (this._viewElt) {
      // It's possible that the placesView might not exist, and we need to
      // do a full init. This could happen if the Bookmarks Toolbar Items are
      // moved to the Menu Panel, and then to the toolbar with the "Add to Toolbar"
      // context menu option, outside of customize mode.
      if (this._viewElt._placesView) {
        this._viewElt._placesView.uninit();
      }
      this.init();
    }
  },

  async populateManagedBookmarks(popup) {
    if (popup.hasChildNodes()) {
      return;
    }
    // Show item's uri in the status bar when hovering, and clear on exit
    popup.addEventListener("DOMMenuItemActive", function (event) {
      XULBrowserWindow.setOverLink(event.target.link);
    });
    popup.addEventListener("DOMMenuItemInactive", function () {
      XULBrowserWindow.setOverLink("");
    });
    let fragment = document.createDocumentFragment();
    await this.addManagedBookmarks(
      fragment,
      Services.policies.getActivePolicies().ManagedBookmarks
    );
    popup.appendChild(fragment);
  },

  async addManagedBookmarks(menu, children) {
    for (let i = 0; i < children.length; i++) {
      let entry = children[i];
      if (entry.children) {
        // It's a folder.
        let submenu = document.createXULElement("menu");
        if (entry.name) {
          submenu.setAttribute("label", entry.name);
        } else {
          document.l10n.setAttributes(submenu, "managed-bookmarks-subfolder");
        }
        submenu.setAttribute("container", "true");
        submenu.classList.add("menu-iconic", "bookmark-item");
        let submenupopup = document.createXULElement("menupopup");
        submenu.appendChild(submenupopup);
        menu.appendChild(submenu);
        this.addManagedBookmarks(submenupopup, entry.children);
      } else if (entry.name && entry.url) {
        // It's bookmark.
        let { preferredURI } = Services.uriFixup.getFixupURIInfo(entry.url);
        let menuitem = document.createXULElement("menuitem");
        menuitem.setAttribute("label", entry.name);
        menuitem.setAttribute(
          "image",
          "page-icon:" + ChromeUtils.encodeURIForSrcset(preferredURI.spec)
        );
        menuitem.classList.add(
          "menuitem-iconic",
          "menuitem-with-favicon",
          "bookmark-item"
        );
        menuitem.link = preferredURI.spec;
        menu.appendChild(menuitem);
      }
    }
  },

  openManagedBookmark(event) {
    openUILink(event.target.link, event, {
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });
  },

  onDragStartManaged(event) {
    if (!event.target.link) {
      return;
    }

    let dt = event.dataTransfer;

    let node = {};
    node.type = 0;
    node.title = event.target.label;
    node.uri = event.target.link;

    function addData(type, index) {
      let wrapNode = PlacesUtils.wrapNode(node, type);
      dt.mozSetDataAt(type, wrapNode, index);
    }

    addData(PlacesUtils.TYPE_X_MOZ_URL, 0);
    addData(PlacesUtils.TYPE_PLAINTEXT, 0);
    addData(PlacesUtils.TYPE_HTML, 0);
  },
};

/**
 * Handles the bookmarks menu-button in the toolbar.
 */

var BookmarkingUI = {
  STAR_ID: "star-button",
  STAR_BOX_ID: "star-button-box",
  BOOKMARK_BUTTON_ID: "bookmarks-menu-button",
  BOOKMARK_BUTTON_SHORTCUT: "addBookmarkAsKb",
  get button() {
    delete this.button;
    let widgetGroup = CustomizableUI.getWidget(this.BOOKMARK_BUTTON_ID);
    return (this.button = widgetGroup.forWindow(window).node);
  },

  get star() {
    delete this.star;
    return (this.star = document.getElementById(this.STAR_ID));
  },

  get starBox() {
    delete this.starBox;
    return (this.starBox = document.getElementById(this.STAR_BOX_ID));
  },

  get anchor() {
    let action = PageActions.actionForID(PageActions.ACTION_ID_BOOKMARK);
    return BrowserPageActions.panelAnchorNodeForAction(action);
  },

  get stringbundleset() {
    delete this.stringbundleset;
    return (this.stringbundleset = document.getElementById("stringbundleset"));
  },

  get toolbar() {
    delete this.toolbar;
    return (this.toolbar = document.getElementById("PersonalToolbar"));
  },

  STATUS_UPDATING: -1,
  STATUS_UNSTARRED: 0,
  STATUS_STARRED: 1,
  get status() {
    if (this._pendingUpdate) {
      return this.STATUS_UPDATING;
    }
    return this.star.hasAttribute("starred")
      ? this.STATUS_STARRED
      : this.STATUS_UNSTARRED;
  },

  onPopupShowing: function BUI_onPopupShowing(event) {
    // Don't handle events for submenus.
    if (event.target.id != "BMB_bookmarksPopup") {
      return;
    }

    // On non-photon, this code should never be reached. However, if you click
    // the outer button's border, some cpp code for the menu button's XBL
    // binding decides to open the popup even though the dropmarker is invisible.
    //
    // Separately, in Photon, if the button is in the dynamic portion of the
    // overflow panel, we want to show a subview instead.
    if (
      this.button.getAttribute("cui-areatype") == CustomizableUI.TYPE_PANEL ||
      this.button.hasAttribute("overflowedItem")
    ) {
      this._showSubView();
      event.preventDefault();
      event.stopPropagation();
      return;
    }

    let widget = CustomizableUI.getWidget(this.BOOKMARK_BUTTON_ID).forWindow(
      window
    );
    if (widget.overflowed) {
      // Don't open a popup in the overflow popup, rather just open the Library.
      event.preventDefault();
      widget.node.removeAttribute("closemenu");
      PlacesCommandHook.showPlacesOrganizer("BookmarksMenu");
      return;
    }

    document.getElementById("BMB_mobileBookmarks").hidden =
      !SHOW_MOBILE_BOOKMARKS;

    this.updateLabel(
      "BMB_viewBookmarksSidebar",
      SidebarController.currentID == "viewBookmarksSidebar"
    );
    this.updateLabel("BMB_viewBookmarksToolbar", !this.toolbar.collapsed);
  },

  updateLabel(elementId, visible) {
    let element = PanelMultiView.getViewNode(document, elementId);
    let l10nID = element.getAttribute("data-l10n-id");
    document.l10n.setAttributes(element, l10nID, { isVisible: !!visible });
  },

  toggleBookmarksToolbar(reason) {
    let newState = this.toolbar.collapsed ? "always" : "never";
    Services.prefs.setCharPref(
      "browser.toolbars.bookmarks.visibility",
      // See firefox.js for possible values
      newState
    );

    CustomizableUI.setToolbarVisibility(this.toolbar.id, newState, false);
    BrowserUsageTelemetry.recordToolbarVisibility(
      this.toolbar.id,
      newState,
      reason
    );
  },

  isOnNewTabPage(uri) {
    if (!uri) {
      return false;
    }
    // Prevent loading AboutNewTab.sys.mjs during startup path if it
    // is only the newTabURL getter we are interested in.
    let newTabURL = Cu.isESModuleLoaded(
      "resource:///modules/AboutNewTab.sys.mjs"
    )
      ? AboutNewTab.newTabURL
      : "about:newtab";
    // Don't treat a custom "about:blank" new tab URL as the "New Tab Page"
    // due to about:blank being used in different contexts and the
    // difficulty in determining if the eventual page load is
    // about:blank or if the about:blank load is just temporary.
    if (newTabURL == "about:blank") {
      newTabURL = "about:newtab";
    }
    let newTabURLs = [
      newTabURL,
      "about:home",
      "chrome://browser/content/blanktab.html",
    ];
    if (PrivateBrowsingUtils.isWindowPrivate(window)) {
      newTabURLs.push("about:privatebrowsing");
    }
    return newTabURLs.some(newTabUriString =>
      this._newTabURI(newTabUriString)?.equalsExceptRef(uri)
    );
  },

  _newTabURI(uriString) {
    let uri = this._newTabURICache.get(uriString);
    if (uri === undefined) {
      uri = Services.io.newURI(uriString);
      this._newTabURICache.set(uriString, uri);
    }
    return uri;
  },
  _newTabURICache: new Map(),

  buildBookmarksToolbarSubmenu(toolbar) {
    let alwaysShowMenuItem = document.createXULElement("menuitem");
    let alwaysHideMenuItem = document.createXULElement("menuitem");
    let showOnNewTabMenuItem = document.createXULElement("menuitem");
    let menuPopup = document.createXULElement("menupopup");
    menuPopup.append(
      alwaysShowMenuItem,
      showOnNewTabMenuItem,
      alwaysHideMenuItem
    );
    let menu = document.createXULElement("menu");
    menu.appendChild(menuPopup);

    menu.setAttribute("label", toolbar.getAttribute("toolbarname"));
    menu.setAttribute("id", "toggle_" + toolbar.id);
    menu.setAttribute("accesskey", toolbar.getAttribute("accesskey"));
    menu.setAttribute("toolbarId", toolbar.id);

    // Used by the Places context menu in the Bookmarks Toolbar
    // when nothing is selected
    menu.setAttribute("selection-type", "none|single");

    MozXULElement.insertFTLIfNeeded("browser/toolbarContextMenu.ftl");
    let menuItems = [
      [
        showOnNewTabMenuItem,
        "toolbar-context-menu-bookmarks-toolbar-on-new-tab-2",
        "newtab",
      ],
      [
        alwaysShowMenuItem,
        "toolbar-context-menu-bookmarks-toolbar-always-show-2",
        "always",
      ],
      [
        alwaysHideMenuItem,
        "toolbar-context-menu-bookmarks-toolbar-never-show-2",
        "never",
      ],
    ];
    menuItems.map(([menuItem, l10nId, visibilityEnum]) => {
      document.l10n.setAttributes(menuItem, l10nId);
      menuItem.setAttribute("type", "radio");
      // The persisted state of the PersonalToolbar is stored in
      // "browser.toolbars.bookmarks.visibility".
      menuItem.toggleAttribute(
        "checked",
        gBookmarksToolbarVisibility == visibilityEnum
      );
      // Identify these items for "onViewToolbarCommand" so
      // we know to check the visibilityEnum value.
      menuItem.dataset.bookmarksToolbarVisibility = true;
      menuItem.dataset.visibilityEnum = visibilityEnum;
      menuItem.addEventListener("command", onViewToolbarCommand);
    });
    let menuItemForNextStateFromKbShortcut =
      gBookmarksToolbarVisibility == "never"
        ? alwaysShowMenuItem
        : alwaysHideMenuItem;
    menuItemForNextStateFromKbShortcut.setAttribute(
      "key",
      "viewBookmarksToolbarKb"
    );

    return menu;
  },

  /**
   * Check if we need to make the empty toolbar message `hidden`.
   * We'll have it unhidden during startup, to make sure the toolbar
   * has height, and we'll unhide it if there is nothing else on the toolbar.
   * We hide it in customize mode, unless there's nothing on the toolbar.
   */
  async updateEmptyToolbarMessage() {
    let { initialHiddenState, checkHasBookmarks } = (() => {
      // Do we have visible kids?
      if (
        this.toolbar.querySelector(
          `:scope > toolbarpaletteitem > toolbarbutton:not([hidden]),
           :scope > toolbarpaletteitem > toolbaritem:not([hidden], #personal-bookmarks),
           :scope > toolbarbutton:not([hidden]),
           :scope > toolbaritem:not([hidden], #personal-bookmarks)`
        )
      ) {
        return { initialHiddenState: true, checkHasBookmarks: false };
      }

      if (this._isCustomizing) {
        return { initialHiddenState: true, checkHasBookmarks: false };
      }

      // If bookmarks have been moved out of the toolbar, we show the message.
      let bookmarksToolbarItemsPlacement =
        CustomizableUI.getPlacementOfWidget("personal-bookmarks");
      let bookmarksItemInToolbar =
        bookmarksToolbarItemsPlacement?.area == CustomizableUI.AREA_BOOKMARKS;
      if (!bookmarksItemInToolbar) {
        return { initialHiddenState: false, checkHasBookmarks: false };
      }

      if (!this.toolbar.hasAttribute("initialized")) {
        // If the toolbar has not been initialized yet, unhide the message, it
        // will be made 0-width and visibility: hidden anyway, to keep the
        // toolbar height stable.
        return { initialHiddenState: false, checkHasBookmarks: true };
      }

      // Check visible bookmark nodes.
      if (
        this.toolbar.querySelector(
          `#PlacesToolbarItems > toolbarseparator,
           #PlacesToolbarItems > toolbarbutton`
        )
      ) {
        return { initialHiddenState: true, checkHasBookmarks: false };
      }
      return { initialHiddenState: true, checkHasBookmarks: true };
    })();

    let emptyMsg = document.getElementById("personal-toolbar-empty");
    emptyMsg.hidden = initialHiddenState;
    if (checkHasBookmarks) {
      emptyMsg.hidden = !(await PlacesToolbarHelper.getIsEmpty());
    }
  },

  _uninitView: function BUI__uninitView() {
    // When an element with a placesView attached is removed and re-inserted,
    // XBL reapplies the binding causing any kind of issues and possible leaks,
    // so kill current view and let popupshowing generate a new one.
    if (this.button._placesView) {
      this.button._placesView.uninit();
    }
    // Also uninit the main menubar placesView, since it would have the same
    // issues.
    let menubar = document.getElementById("bookmarksMenu");
    if (menubar && menubar._placesView) {
      menubar._placesView.uninit();
    }

    // We have to do the same thing for the "special" views underneath the
    // the bookmarks menu.
    const kSpecialViewNodeIDs = [
      "BMB_bookmarksToolbar",
      "BMB_unsortedBookmarks",
    ];
    for (let viewNodeID of kSpecialViewNodeIDs) {
      let elem = document.getElementById(viewNodeID);
      if (elem && elem._placesView) {
        elem._placesView.uninit();
      }
    }
  },

  onCustomizeStart: function BUI_customizeStart(aWindow) {
    if (aWindow == window) {
      this._uninitView();
      this._isCustomizing = true;

      this.updateEmptyToolbarMessage().catch(console.error);

      let isVisible =
        Services.prefs.getCharPref(
          "browser.toolbars.bookmarks.visibility",
          "newtab"
        ) != "never";
      // Temporarily show the bookmarks toolbar in Customize mode if
      // the toolbar isn't set to Never. We don't have to worry about
      // hiding when leaving customize mode since the toolbar will
      // hide itself on location change.
      setToolbarVisibility(this.toolbar, isVisible, false);
    }
  },

  onWidgetAdded: function BUI_widgetAdded(aWidgetId, aArea) {
    if (aWidgetId == this.BOOKMARK_BUTTON_ID) {
      this._onWidgetWasMoved();
    }
    if (aArea == CustomizableUI.AREA_BOOKMARKS) {
      this.updateEmptyToolbarMessage().catch(console.error);
    }
  },

  onWidgetRemoved: function BUI_widgetRemoved(aWidgetId, aOldArea) {
    if (aWidgetId == this.BOOKMARK_BUTTON_ID) {
      this._onWidgetWasMoved();
    }
    if (aOldArea == CustomizableUI.AREA_BOOKMARKS) {
      this.updateEmptyToolbarMessage().catch(console.error);
    }
  },

  onWidgetReset: function BUI_widgetReset(aNode) {
    if (aNode == this.button) {
      this._onWidgetWasMoved();
    }
  },

  onWidgetUndoMove: function BUI_undoWidgetUndoMove(aNode) {
    if (aNode == this.button) {
      this._onWidgetWasMoved();
    }
  },

  onWidgetBeforeDOMChange: function BUI_onWidgetBeforeDOMChange(
    aNode,
    aNextNode,
    aContainer,
    aIsRemoval
  ) {
    if (aNode.id == "import-button") {
      this._updateImportButton(aNode, aIsRemoval ? null : aContainer);
    }
  },

  _updateImportButton: function BUI_updateImportButton(aNode, aContainer) {
    // The import button behaves like a bookmark item when in the bookmarks
    // toolbar, otherwise like a regular toolbar button.
    let isBookmarkItem = aContainer == this.toolbar;
    aNode.classList.toggle("toolbarbutton-1", !isBookmarkItem);
    aNode.classList.toggle("bookmark-item", isBookmarkItem);
  },

  _onWidgetWasMoved: function BUI_widgetWasMoved() {
    // If we're moved outside of customize mode, we need to uninit
    // our view so it gets reconstructed.
    if (!this._isCustomizing) {
      this._uninitView();
    }
  },

  onCustomizeEnd: function BUI_customizeEnd(aWindow) {
    if (aWindow == window) {
      this._isCustomizing = false;
      this.updateEmptyToolbarMessage().catch(console.error);
    }
  },

  init() {
    CustomizableUI.addListener(this);
    let importButton = document.getElementById("import-button");
    if (importButton) {
      this._updateImportButton(importButton, importButton.parentNode);
    }
    this.updateEmptyToolbarMessage().catch(console.error);
  },

  _hasBookmarksObserver: false,
  _itemGuids: new Set(),
  uninit: function BUI_uninit() {
    this.updateBookmarkPageMenuItem(true);
    CustomizableUI.removeListener(this);

    this._uninitView();

    if (this._hasBookmarksObserver) {
      PlacesUtils.observers.removeListener(
        [
          "bookmark-added",
          "bookmark-removed",
          "bookmark-moved",
          "bookmark-url-changed",
        ],
        this.handlePlacesEvents
      );
    }

    if (this._pendingUpdate) {
      delete this._pendingUpdate;
    }
  },

  onLocationChange: function BUI_onLocationChange() {
    if (this._uri && gBrowser.currentURI.equals(this._uri)) {
      return;
    }
    this.updateStarState();
  },

  updateStarState: function BUI_updateStarState() {
    this._uri = gBrowser.currentURI;
    this._itemGuids.clear();
    let guids = new Set();

    // those objects are use to check if we are in the current iteration before
    // returning any result.
    let pendingUpdate = (this._pendingUpdate = {});

    PlacesUtils.bookmarks
      .fetch({ url: this._uri }, b => guids.add(b.guid), { concurrent: true })
      .catch(console.error)
      .then(() => {
        if (pendingUpdate != this._pendingUpdate) {
          return;
        }

        // It's possible that "bookmark-added" gets called before the async statement
        // calls back.  For such an edge case, retain all unique entries from the
        // array.
        if (this._itemGuids.size > 0) {
          this._itemGuids = new Set(...this._itemGuids, ...guids);
        } else {
          this._itemGuids = guids;
        }

        this._updateStar();

        // Start observing bookmarks if needed.
        if (!this._hasBookmarksObserver) {
          try {
            this.handlePlacesEvents = this.handlePlacesEvents.bind(this);
            PlacesUtils.observers.addListener(
              [
                "bookmark-added",
                "bookmark-removed",
                "bookmark-moved",
                "bookmark-url-changed",
              ],
              this.handlePlacesEvents
            );
            this._hasBookmarksObserver = true;
          } catch (ex) {
            console.error(
              "BookmarkingUI failed adding a bookmarks observer: ",
              ex
            );
          }
        }

        delete this._pendingUpdate;
      });
  },

  _updateStar: function BUI__updateStar() {
    let starred = this._itemGuids.size > 0;

    // Update the image for all elements.
    for (let element of [
      this.star,
      document.getElementById("context-bookmarkpage"),
      PanelMultiView.getViewNode(document, "panelMenuBookmarkThisPage"),
      document.getElementById("pageAction-panel-bookmark"),
    ]) {
      if (!element) {
        // The page action panel element may not have been created yet.
        continue;
      }
      element.toggleAttribute("starred", starred);
    }

    if (!this.starBox) {
      // The BOOKMARK_BUTTON_SHORTCUT exists only in browser.xhtml.
      // Return early if we're not in this context, but still reset the
      // Bookmark Page items.
      this.updateBookmarkPageMenuItem(true);
      return;
    }

    // Update the tooltip for elements that require it.
    let shortcut = document.getElementById(this.BOOKMARK_BUTTON_SHORTCUT);
    let l10nArgs = {
      shortcut: ShortcutUtils.prettifyShortcut(shortcut),
    };
    document.l10n.setAttributes(
      this.starBox,
      starred ? "urlbar-star-edit-bookmark" : "urlbar-star-add-bookmark",
      l10nArgs
    );

    // Update the Bookmark Page menuitem when bookmarked state changes.
    this.updateBookmarkPageMenuItem();

    Services.obs.notifyObservers(
      null,
      "bookmark-icon-updated",
      starred ? "starred" : "unstarred"
    );
  },

  /**
   * Update the "Bookmark Page…" menuitems on the menubar, panels, context
   * menu and page actions.
   *
   * @param {boolean} [forceReset] passed when we're destroyed and the label
   * should go back to the default (Bookmark Page), for MacOS.
   */
  updateBookmarkPageMenuItem(forceReset = false) {
    let isStarred = !forceReset && this._itemGuids.size > 0;
    // Define the l10n id which will be used to localize elements
    // that only require a label using the menubar.ftl messages.
    let menuItemL10nId = isStarred ? "menu-edit-bookmark" : "menu-bookmark-tab";
    let menuItem = document.getElementById("menu_bookmarkThisPage");
    if (menuItem) {
      // Localize the menubar item.
      document.l10n.setAttributes(menuItem, menuItemL10nId);
    }

    let panelMenuItemL10nId = isStarred
      ? "bookmarks-subview-edit-bookmark"
      : "bookmarks-subview-bookmark-tab";
    let panelMenuToolbarButton = PanelMultiView.getViewNode(
      document,
      "panelMenuBookmarkThisPage"
    );
    if (panelMenuToolbarButton) {
      document.l10n.setAttributes(panelMenuToolbarButton, panelMenuItemL10nId);
    }

    // Localize the context menu item element.
    let contextItem = document.getElementById("context-bookmarkpage");
    // On macOS regular menuitems are used and the shortcut isn't added
    if (contextItem) {
      if (AppConstants.platform == "macosx") {
        let contextItemL10nId = isStarred
          ? "main-context-menu-edit-bookmark-mac"
          : "main-context-menu-bookmark-page-mac";
        document.l10n.setAttributes(contextItem, contextItemL10nId);
      } else {
        let shortcutElem = document.getElementById(
          this.BOOKMARK_BUTTON_SHORTCUT
        );
        if (shortcutElem) {
          let shortcut = ShortcutUtils.prettifyShortcut(shortcutElem);
          let contextItemL10nId = isStarred
            ? "main-context-menu-edit-bookmark-with-shortcut"
            : "main-context-menu-bookmark-page-with-shortcut";
          let l10nArgs = { shortcut };
          document.l10n.setAttributes(contextItem, contextItemL10nId, l10nArgs);
        } else {
          let contextItemL10nId = isStarred
            ? "main-context-menu-edit-bookmark"
            : "main-context-menu-bookmark-page";
          document.l10n.setAttributes(contextItem, contextItemL10nId);
        }
      }
    }

    // Update Page Actions.
    if (document.getElementById("page-action-buttons")) {
      // Fetch the label attribute value of the message and
      // apply it on the star title.
      //
      // Note: This should be updated once bug 1608198 is fixed.
      this._latestMenuItemL10nId = menuItemL10nId;
      document.l10n.formatMessages([{ id: menuItemL10nId }]).then(l10n => {
        // It's possible for this promise to be scheduled multiple times.
        // In such a case, we'd like to avoid setting the title if there's
        // a newer l10n id pending to be set.
        if (this._latestMenuItemL10nId != menuItemL10nId) {
          return;
        }

        // We assume that menuItemL10nId has a single attribute.
        let label = l10n[0].attributes[0].value;

        // Update the label for the page action panel.
        let panelButton = BrowserPageActions.panelButtonNodeForActionID(
          PageActions.ACTION_ID_BOOKMARK
        );
        if (panelButton) {
          panelButton.setAttribute("label", label);
        }
      });
    }
  },

  onMainMenuPopupShowing: function BUI_onMainMenuPopupShowing(event) {
    // Don't handle events for submenus.
    if (event.target.id != "bookmarksMenuPopup") {
      return;
    }

    document.getElementById("menu_mobileBookmarks").hidden =
      !SHOW_MOBILE_BOOKMARKS;
  },

  showSubView(anchor) {
    this._showSubView(null, anchor);
  },

  _showSubView(
    event,
    anchor = document.getElementById(this.BOOKMARK_BUTTON_ID)
  ) {
    let view = PanelMultiView.getViewNode(document, "PanelUI-bookmarks");
    view.addEventListener("ViewShowing", this);
    view.addEventListener("ViewHiding", this);
    anchor.setAttribute("closemenu", "none");
    this.updateLabel("panelMenu_viewBookmarksToolbar", !this.toolbar.collapsed);
    PanelUI.showSubView("PanelUI-bookmarks", anchor, event);
  },

  onCommand: function BUI_onCommand(aEvent) {
    if (aEvent.target != aEvent.currentTarget) {
      return;
    }

    // Handle special case when the button is in the panel.
    if (this.button.getAttribute("cui-areatype") == CustomizableUI.TYPE_PANEL) {
      this._showSubView(aEvent);
      return;
    }
    let widget = CustomizableUI.getWidget(this.BOOKMARK_BUTTON_ID).forWindow(
      window
    );
    if (widget.overflowed) {
      // Close the overflow panel because the Edit Bookmark panel will appear.
      widget.node.removeAttribute("closemenu");
    }
    this.onStarCommand(aEvent);
  },

  onStarCommand(aEvent) {
    // Ignore non-left clicks on the star, or if we are updating its state.
    if (
      !this._pendingUpdate &&
      (aEvent.type != "click" || aEvent.button == 0)
    ) {
      PlacesCommandHook.bookmarkPage();
    }
  },

  handleEvent: function BUI_handleEvent(aEvent) {
    switch (aEvent.type) {
      case "ViewShowing":
        this.onPanelMenuViewShowing(aEvent);
        break;
      case "ViewHiding":
        this.onPanelMenuViewHiding(aEvent);
        break;
      case "command":
        if (aEvent.target.id == "panelMenu_searchBookmarks") {
          PlacesCommandHook.searchBookmarks();
        } else if (aEvent.target.id == "panelMenu_viewBookmarksToolbar") {
          this.toggleBookmarksToolbar("bookmark-tools");
        }
        break;
    }
  },

  onPanelMenuViewShowing: function BUI_onViewShowing(aEvent) {
    let panelview = aEvent.target;

    // Get all statically placed buttons to supply them with keyboard shortcuts.
    let staticButtons = panelview.getElementsByTagName("toolbarbutton");
    for (let i = 0, l = staticButtons.length; i < l; ++i) {
      CustomizableUI.addShortcut(staticButtons[i]);
    }

    // Setup the Places view.
    // We restrict the amount of results to 42. Not 50, but 42. Why? Because 42.
    let query =
      "place:queryType=" +
      Ci.nsINavHistoryQueryOptions.QUERY_TYPE_BOOKMARKS +
      "&sort=" +
      Ci.nsINavHistoryQueryOptions.SORT_BY_DATEADDED_DESCENDING +
      "&maxResults=42&excludeQueries=1";

    this._panelMenuView = new PlacesPanelview(
      query,
      document.getElementById("panelMenu_bookmarksMenu"),
      panelview
    );
    panelview.removeEventListener("ViewShowing", this);
    panelview.addEventListener("command", this);
  },

  onPanelMenuViewHiding: function BUI_onViewHiding(aEvent) {
    this._panelMenuView.uninit();
    delete this._panelMenuView;
    let panelview = aEvent.target;
    panelview.removeEventListener("ViewHiding", this);
    panelview.removeEventListener("command", this);
  },

  handlePlacesEvents(aEvents) {
    let isStarUpdateNeeded = false;
    let affectsOtherBookmarksFolder = false;
    let affectsBookmarksToolbarFolder = false;

    for (let ev of aEvents) {
      switch (ev.type) {
        case "bookmark-added":
          // Only need to update the UI if it wasn't marked as starred before:
          if (this._itemGuids.size == 0) {
            if (ev.url && ev.url == this._uri.spec) {
              // If a new bookmark has been added to the tracked uri, register it.
              if (!this._itemGuids.has(ev.guid)) {
                this._itemGuids.add(ev.guid);
                isStarUpdateNeeded = true;
              }
            }
          }

          if (ev.parentGuid === PlacesUtils.bookmarks.toolbarGuid) {
            Glean.browserEngagement.bookmarksToolbarBookmarkAdded.add(1);
          }
          if (ev.parentGuid == PlacesUtils.bookmarks.tagsGuid) {
            StarUI.userHasTags = true;
          }
          break;
        case "bookmark-removed":
          // If one of the tracked bookmarks has been removed, unregister it.
          if (this._itemGuids.has(ev.guid)) {
            this._itemGuids.delete(ev.guid);
            // Only need to update the UI if the page is no longer starred
            if (this._itemGuids.size == 0) {
              isStarUpdateNeeded = true;
            }
          }

          // Reset the default location if it is equal to the folder
          // being deleted. Just check the preference directly since we
          // do not want to do a asynchronous db lookup.
          PlacesUIUtils.defaultParentGuid.then(parentGuid => {
            if (
              ev.itemType == PlacesUtils.bookmarks.TYPE_FOLDER &&
              ev.guid == parentGuid
            ) {
              Services.prefs.setCharPref(
                "browser.bookmarks.defaultLocation",
                PlacesUtils.bookmarks.toolbarGuid
              );
            }
          });
          break;
        case "bookmark-moved":
          if (
            ev.parentGuid === PlacesUtils.bookmarks.unfiledGuid ||
            ev.oldParentGuid === PlacesUtils.bookmarks.unfiledGuid
          ) {
            affectsOtherBookmarksFolder = true;
          }

          if (
            ev.parentGuid == PlacesUtils.bookmarks.toolbarGuid ||
            ev.oldParentGuid == PlacesUtils.bookmarks.toolbarGuid
          ) {
            affectsBookmarksToolbarFolder = true;
            if (ev.oldParentGuid != PlacesUtils.bookmarks.toolbarGuid) {
              Glean.browserEngagement.bookmarksToolbarBookmarkAdded.add(1);
            }
          }
          break;
        case "bookmark-url-changed":
          // If the changed bookmark was tracked, check if it is now pointing to
          // a different uri and unregister it.
          if (this._itemGuids.has(ev.guid) && ev.url != this._uri.spec) {
            this._itemGuids.delete(ev.guid);
            // Only need to update the UI if the page is no longer starred
            if (this._itemGuids.size == 0) {
              this._updateStar();
            }
          } else if (
            !this._itemGuids.has(ev.guid) &&
            ev.url == this._uri.spec
          ) {
            // If another bookmark is now pointing to the tracked uri, register it.
            this._itemGuids.add(ev.guid);
            // Only need to update the UI if it wasn't marked as starred before:
            if (this._itemGuids.size == 1) {
              this._updateStar();
            }
          }

          break;
      }

      if (ev.parentGuid == PlacesUtils.bookmarks.unfiledGuid) {
        affectsOtherBookmarksFolder = true;
      } else if (ev.parentGuid == PlacesUtils.bookmarks.toolbarGuid) {
        affectsBookmarksToolbarFolder = true;
      }
    }

    if (isStarUpdateNeeded) {
      this._updateStar();
    }

    // Run after the notification has been handled by the views.
    Services.tm.dispatchToMainThread(() => {
      if (affectsOtherBookmarksFolder) {
        this.maybeShowOtherBookmarksFolder().catch(console.error);
      }
      if (affectsBookmarksToolbarFolder) {
        this.updateEmptyToolbarMessage().catch(console.error);
      }
    });
  },

  onWidgetUnderflow(aNode) {
    let win = aNode.documentGlobal;
    if (aNode.id != this.BOOKMARK_BUTTON_ID || win != window) {
      return;
    }

    // The view gets broken by being removed and reinserted. Uninit
    // here so popupshowing will generate a new one:
    this._uninitView();
  },

  async maybeShowOtherBookmarksFolder() {
    // PlacesToolbar._placesView can be undefined if the toolbar isn't initialized,
    // collapsed, or hidden in some other way.
    let toolbar = document.getElementById("PlacesToolbar");
    if (!toolbar?._placesView) {
      return;
    }

    let placement = CustomizableUI.getPlacementOfWidget("personal-bookmarks");
    let otherBookmarks = document.getElementById("OtherBookmarks");
    if (
      !SHOW_OTHER_BOOKMARKS ||
      placement?.area != CustomizableUI.AREA_BOOKMARKS
    ) {
      if (otherBookmarks) {
        otherBookmarks.hidden = true;
      }
      return;
    }

    let instance = (this._showOtherBookmarksInstance = {});
    let unfiledGuid = PlacesUtils.bookmarks.unfiledGuid;
    let numberOfBookmarks = (await PlacesUtils.bookmarks.fetch(unfiledGuid))
      .childCount;
    if (instance != this._showOtherBookmarksInstance) {
      return;
    }

    if (numberOfBookmarks > 0) {
      // Build the "Other Bookmarks" button if it doesn't exist.
      if (!otherBookmarks) {
        const node = PlacesUtils.getFolderContents(unfiledGuid).root;
        otherBookmarks = this.buildOtherBookmarksFolder(node);
      }
      otherBookmarks.hidden = false;
    } else if (otherBookmarks) {
      otherBookmarks.hidden = true;
    }
  },

  buildShowOtherBookmarksMenuItem() {
    // Building this only if there's bookmarks in unfiled would cause
    // synchronous IO, thus we just add it as disabled and enable it once the
    // information is available.
    let menuItem = document.createXULElement("menuitem");

    menuItem.setAttribute("id", "show-other-bookmarks_PersonalToolbar");
    menuItem.setAttribute("toolbarId", "PersonalToolbar");
    menuItem.setAttribute("type", "checkbox");
    menuItem.setAttribute("selection-type", "none|single");
    menuItem.setAttribute("start-disabled", "true");
    menuItem.toggleAttribute("checked", SHOW_OTHER_BOOKMARKS);

    MozXULElement.insertFTLIfNeeded("browser/toolbarContextMenu.ftl");
    document.l10n.setAttributes(
      menuItem,
      "toolbar-context-menu-bookmarks-show-other-bookmarks"
    );
    menuItem.addEventListener("command", () => {
      Services.prefs.setBoolPref(
        "browser.toolbars.bookmarks.showOtherBookmarks",
        !SHOW_OTHER_BOOKMARKS
      );
    });
    // Enable the menuItem if there's unfiled bookmarks
    PlacesUtils.bookmarks.fetch(PlacesUtils.bookmarks.unfiledGuid).then(bm => {
      if (bm.childCount) {
        menuItem.disabled = false;
      }
    });

    return menuItem;
  },

  buildOtherBookmarksFolder(node) {
    let otherBookmarksButton = document.createXULElement("toolbarbutton");
    otherBookmarksButton.setAttribute("type", "menu");
    otherBookmarksButton.setAttribute("container", "true");
    otherBookmarksButton.id = "OtherBookmarks";
    otherBookmarksButton.className = "bookmark-item";
    otherBookmarksButton.hidden = "true";
    otherBookmarksButton.addEventListener("popupshowing", event =>
      document
        .getElementById("PlacesToolbar")
        ._placesView._onOtherBookmarksPopupShowing(event)
    );

    MozXULElement.insertFTLIfNeeded("browser/places.ftl");
    document.l10n.setAttributes(otherBookmarksButton, "other-bookmarks-folder");

    let otherBookmarksPopup = document.createXULElement("menupopup", {
      is: "places-popup",
    });
    otherBookmarksPopup.setAttribute("placespopup", "true");
    otherBookmarksPopup.toggleAttribute("nonnative", true);
    otherBookmarksPopup.setAttribute("context", "placesContext");
    otherBookmarksPopup.classList.add("toolbar-menupopup");
    otherBookmarksPopup.id = "OtherBookmarksPopup";

    otherBookmarksPopup._placesNode = PlacesUtils.asContainer(node);
    otherBookmarksButton._placesNode = PlacesUtils.asContainer(node);

    otherBookmarksButton.appendChild(otherBookmarksPopup);

    let chevronButton = document.getElementById("PlacesChevron");
    chevronButton.parentNode.append(otherBookmarksButton);

    let placesToolbar = document.getElementById("PlacesToolbar");
    placesToolbar._placesView._otherBookmarks = otherBookmarksButton;
    placesToolbar._placesView._otherBookmarksPopup = otherBookmarksPopup;
    return otherBookmarksButton;
  },
};
