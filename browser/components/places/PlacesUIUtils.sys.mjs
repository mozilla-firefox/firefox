/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  CLIENT_NOT_CONFIGURED: "resource://services-sync/constants.sys.mjs",
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  CustomizableUI: "resource:///modules/CustomizableUI.sys.mjs",
  MigrationUtils: "resource:///modules/MigrationUtils.sys.mjs",
  OpenInTabsUtils:
    "moz-src:///browser/components/tabbrowser/OpenInTabsUtils.sys.mjs",
  PlacesTransactions: "resource://gre/modules/PlacesTransactions.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  Weave: "resource://services-sync/main.sys.mjs",
});

const ITEM_CHANGED_BATCH_NOTIFICATION_THRESHOLD = 10;

// copied from utilityOverlay.js
const TAB_DROP_TYPE = "application/x-moz-tabbrowser-tab";

/**
 * Collects all information for a bookmark and performs editmethods
 */
class BookmarkState {
  /**
   * Construct a new BookmarkState.
   *
   * @param {object} options
   *   The constructor options.
   * @param {object} options.info
   *   Either a result node or a node-like object representing the item to be edited.
   * @param {string} [options.tags]
   *   Tags (if any) for the bookmark in a comma separated string. Empty tags are
   *   skipped
   * @param {string} [options.keyword]
   *   Existing (if there are any) keyword for bookmark
   * @param {boolean} [options.isFolder]
   *   If the item is a folder.
   * @param {Array<{ title: string; url: nsIURI; }>} [options.children]
   *   The list of child URIs to bookmark within the folder.
   * @param {boolean} [options.autosave]
   *   If changes to bookmark fields should be saved immediately after calling
   *   its respective "changed" method, rather than waiting for save() to be
   *   called.
   * @param {number} [options.index]
   *   The insertion point index of the bookmark.
   */
  constructor({
    info,
    tags = "",
    keyword = "",
    isFolder = false,
    children = [],
    autosave = false,
    index,
  }) {
    this._guid = info.itemGuid;
    this._postData = info.postData;
    this._isTagContainer = info.isTag;
    this._bulkTaggingUrls = info.uris?.map(uri => uri.spec);
    this._isFolder = isFolder;
    this._children = children;
    this._autosave = autosave;

    // Original Bookmark
    this._originalState = {
      title: this._isTagContainer ? info.tag : info.title,
      uri: info.uri?.spec,
      tags: tags
        .trim()
        .split(/\s*,\s*/)
        .filter(tag => !!tag.length),
      keyword,
      parentGuid: info.parentGuid,
      index,
    };

    // Edited bookmark
    this._newState = {};
  }

  /**
   * Save edited title for the bookmark
   *
   * @param {string} title
   *   The title of the bookmark
   */
  async _titleChanged(title) {
    this._newState.title = title;
    await this._maybeSave();
  }

  /**
   * Save edited location for the bookmark
   *
   * @param {string} location
   *   The location of the bookmark
   */
  async _locationChanged(location) {
    this._newState.uri = location;
    await this._maybeSave();
  }

  /**
   * Save edited tags for the bookmark
   *
   * @param {string} tags
   *    Comma separated list of tags
   */
  async _tagsChanged(tags) {
    this._newState.tags = tags;
    await this._maybeSave();
  }

  /**
   * Save edited keyword for the bookmark
   *
   * @param {string} keyword
   *   The keyword of the bookmark
   */
  async _keywordChanged(keyword) {
    this._newState.keyword = keyword;
    await this._maybeSave();
  }

  /**
   * Save edited parentGuid for the bookmark
   *
   * @param {string} parentGuid
   *   The parentGuid of the bookmark
   */
  async _parentGuidChanged(parentGuid) {
    this._newState.parentGuid = parentGuid;
    await this._maybeSave();
  }

  /**
   * Save changes if autosave is enabled.
   */
  async _maybeSave() {
    if (this._autosave) {
      await this.save();
    }
  }

  /**
   * Create a new bookmark.
   *
   * @returns {string} The bookmark's GUID.
   */
  async _createBookmark() {
    let transactions = [
      lazy.PlacesTransactions.NewBookmark({
        parentGuid: this.parentGuid,
        tags: this._newState.tags,
        title: this._newState.title ?? this._originalState.title,
        url: this._newState.uri ?? this._originalState.uri,
        index: this._originalState.index,
      }),
    ];
    if (this._newState.keyword) {
      transactions.push(previousResults =>
        lazy.PlacesTransactions.EditKeyword({
          guid: previousResults[0],
          keyword: this._newState.keyword,
          postData: this._postData,
        })
      );
    }
    let results = await lazy.PlacesTransactions.batch(
      transactions,
      "BookmarkState::createBookmark"
    );
    this._guid = results?.[0];
    return this._guid;
  }

  /**
   * Create a new folder.
   *
   * @returns {string} The folder's GUID.
   */
  async _createFolder() {
    let transactions = [
      lazy.PlacesTransactions.NewFolder({
        parentGuid: this.parentGuid,
        title: this._newState.title ?? this._originalState.title,
        children: this._children,
        index: this._originalState.index,
        tags: this._newState.tags,
      }),
    ];

    if (this._bulkTaggingUrls) {
      this._appendTagsTransactions({
        transactions,
        newTags: this._newState.tags,
        originalTags: this._originalState.tags,
        urls: this._bulkTaggingUrls,
      });
    }

    let results = await lazy.PlacesTransactions.batch(
      transactions,
      "BookmarkState::save::createFolder"
    );
    this._guid = results[0];
    return this._guid;
  }

  get parentGuid() {
    return this._newState.parentGuid ?? this._originalState.parentGuid;
  }

  /**
   * Save() API function for bookmark.
   *
   * @returns {string} bookmark.guid
   */
  async save() {
    if (this._guid === lazy.PlacesUtils.bookmarks.unsavedGuid) {
      return this._isFolder ? this._createFolder() : this._createBookmark();
    }

    if (!Object.keys(this._newState).length) {
      return this._guid;
    }

    if (this._isTagContainer && this._newState.title) {
      await lazy.PlacesTransactions.RenameTag({
        oldTag: this._originalState.title,
        tag: this._newState.title,
      })
        .transact()
        .catch(console.error);
      return this._guid;
    }

    let url = this._newState.uri || this._originalState.uri;
    let transactions = [];

    if (this._newState.uri) {
      transactions.push(
        lazy.PlacesTransactions.EditUrl({
          guid: this._guid,
          url,
        })
      );
    }

    for (const [key, value] of Object.entries(this._newState)) {
      switch (key) {
        case "title":
          transactions.push(
            lazy.PlacesTransactions.EditTitle({
              guid: this._guid,
              title: value,
            })
          );
          break;
        case "tags": {
          this._appendTagsTransactions({
            transactions,
            newTags: value,
            originalTags: this._originalState.tags,
            urls: this._bulkTaggingUrls || [url],
          });
          break;
        }
        case "keyword":
          transactions.push(
            lazy.PlacesTransactions.EditKeyword({
              guid: this._guid,
              keyword: value,
              postData: this._postData,
              oldKeyword: this._originalState.keyword,
            })
          );
          break;
        case "parentGuid":
          transactions.push(
            lazy.PlacesTransactions.Move({
              guid: this._guid,
              newParentGuid: this._newState.parentGuid,
            })
          );
          break;
      }
    }
    if (transactions.length) {
      await lazy.PlacesTransactions.batch(transactions, "BookmarkState::save");
    }

    this._originalState = { ...this._originalState, ...this._newState };
    this._newState = {};
    return this._guid;
  }

  /**
   * Append transactions to update tags by given information.
   *
   * @param {object} parameters
   *   The parameters object containing:
   * @param {object[]} parameters.transactions
   *   Array that transactions will be appended to.
   * @param {string[]} parameters.newTags
   *   Tags that will be appended to the given urls.
   * @param {string[]} parameters.originalTags
   *   Tags that had been appended to the given urls.
   * @param {string[]} parameters.urls
   *   URLs that will be updated.
   */
  _appendTagsTransactions({
    transactions,
    newTags = [],
    originalTags = [],
    urls,
  }) {
    const addedTags = newTags.filter(tag => !originalTags.includes(tag));
    const removedTags = originalTags.filter(tag => !newTags.includes(tag));
    if (addedTags.length) {
      transactions.push(
        lazy.PlacesTransactions.Tag({
          urls,
          tags: addedTags,
        })
      );
    }
    if (removedTags.length) {
      transactions.push(
        lazy.PlacesTransactions.Untag({
          urls,
          tags: removedTags,
        })
      );
    }
  }
}

export var PlacesUIUtils = {
  BookmarkState,
  _bookmarkToolbarTelemetryListening: false,
  LAST_USED_FOLDERS_META_KEY: "bookmarks/lastusedfolders",

  lastContextMenuTriggerNode: null,

  lastContextMenuCommand: null,

  // This allows to await for all the relevant bookmark changes to be applied
  // when a bookmark dialog is closed. It is resolved to the bookmark guid,
  // if a bookmark was created or modified.
  lastBookmarkDialogDeferred: null,

  /**
   * Obfuscates a place: URL to use it in xulstore without the risk of
   leaking browsing information. Uses md5 to hash the query string.
   *
   * @param {URL} url
   *        the URL for xulstore with place: key pairs.
   * @returns {string} "place:[md5_hash]" hashed url
   */

  obfuscateUrlForXulStore(url) {
    if (!url.startsWith("place:")) {
      throw new Error("Method must be used to only obfuscate place: uris!");
    }
    let urlNoProtocol = url.substring(url.indexOf(":") + 1);
    let hashedURL = lazy.PlacesUtils.md5(urlNoProtocol);

    return `place:${hashedURL}`;
  },

  /**
   * Shows the bookmark dialog corresponding to the specified info.
   *
   * @param {object} aInfo
   *        Describes the item to be edited/added in the dialog.
   *        See documentation at the top of bookmarkProperties.js
   * @param {DOMWindow} [aParentWindow]
   *        Owner window for the new dialog.
   *
   * @see documentation at the top of bookmarkProperties.js
   * @returns {string} The guid of the item that was created or edited,
   *                   undefined otherwise.
   */
  async showBookmarkDialog(aInfo, aParentWindow = null) {
    this.lastBookmarkDialogDeferred = Promise.withResolvers();

    let dialogURL = "chrome://browser/content/places/bookmarkProperties.xhtml";
    let features = "centerscreen,chrome,modal,resizable=no";
    let bookmarkGuid;

    if (!aParentWindow) {
      aParentWindow = Services.wm.getMostRecentWindow(null);
    }

    if (aParentWindow.gDialogBox) {
      await aParentWindow.gDialogBox.open(dialogURL, aInfo);
    } else {
      aParentWindow.openDialog(dialogURL, "", features, aInfo);
    }

    if (aInfo.bookmarkState) {
      bookmarkGuid = await aInfo.bookmarkState.save();
      this.lastBookmarkDialogDeferred.resolve(bookmarkGuid);
      return bookmarkGuid;
    }
    bookmarkGuid = undefined;
    this.lastBookmarkDialogDeferred.resolve(bookmarkGuid);
    return bookmarkGuid;
  },

  /**
   * Bookmarks one or more pages. If there is more than one, this will create
   * the bookmarks in a new folder.
   *
   * @param {Array.<nsIURI>} URIList
   *   The list of URIs to bookmark.
   * @param {Array.<string>} [hiddenRows]
   *   An array of rows to be hidden.
   * @param {DOMWindow} [win]
   *   The window to use as the parent to display the bookmark dialog.
   */
  async showBookmarkPagesDialog(URIList, hiddenRows = [], win = null) {
    if (!URIList.length) {
      return;
    }

    const bookmarkDialogInfo = { action: "add", hiddenRows };
    if (URIList.length > 1) {
      bookmarkDialogInfo.type = "folder";
      bookmarkDialogInfo.URIList = URIList;
    } else {
      bookmarkDialogInfo.type = "bookmark";
      bookmarkDialogInfo.title = URIList[0].title;
      bookmarkDialogInfo.uri = URIList[0].uri;
    }

    await PlacesUIUtils.showBookmarkDialog(bookmarkDialogInfo, win);
  },

  /**
   * Returns the closet ancestor places view for the given DOM node
   *
   * @param {DOMNode} aNode
   *        a DOM node
   * @returns {DOMNode} the closest ancestor places view if exists, null otherwsie.
   */
  getViewForNode: function PUIU_getViewForNode(aNode) {
    let node = aNode;

    if (Cu.isDeadWrapper(node)) {
      return null;
    }

    if (node.localName == "panelview" && node._placesView) {
      return node._placesView;
    }

    // The view for a <menu> of which its associated menupopup is a places
    // view, is the menupopup.
    if (
      node.localName == "menu" &&
      !node._placesNode &&
      node.menupopup._placesView
    ) {
      return node.menupopup._placesView;
    }

    while (Element.isInstance(node)) {
      if (node._placesView) {
        return node._placesView;
      }
      if (
        node.localName == "tree" &&
        node.getAttribute("is") == "places-tree"
      ) {
        return node;
      }

      node = node.parentNode;
    }

    return null;
  },

  /**
   * Returns the active PlacesController for a given command.
   *
   * @param {DOMWindow} win The window containing the affected view
   * @param {string} command The command
   * @returns {PlacesController} a places controller
   */
  getControllerForCommand(win, command) {
    // If we're building a context menu for a non-focusable view, for example
    // a menupopup, we must return the view that triggered the context menu.
    let popupNode = PlacesUIUtils.lastContextMenuTriggerNode;
    if (popupNode) {
      let isManaged = !!popupNode.closest("#managed-bookmarks");
      if (isManaged) {
        return this.managedBookmarksController;
      }
      let view = this.getViewForNode(popupNode);
      if (view && view._contextMenuShown) {
        return view.controllers.getControllerForCommand(command);
      }
    }

    // When we're not building a context menu, only focusable views
    // are possible.  Thus, we can safely use the command dispatcher.
    let controller =
      win.top.document.commandDispatcher.getControllerForCommand(command);
    return controller || null;
  },

  /**
   * Update all the Places commands for the given window.
   *
   * @param {DOMWindow} win The window to update.
   */
  updateCommands(win) {
    // Get the controller for one of the places commands.
    let controller = this.getControllerForCommand(win, "placesCmd_open");
    for (let command of [
      "placesCmd_open",
      "placesCmd_open:window",
      "placesCmd_open:privatewindow",
      "placesCmd_open:tab",
      "placesCmd_new:folder",
      "placesCmd_new:bookmark",
      "placesCmd_new:separator",
      "placesCmd_show:info",
      "placesCmd_reload",
      "placesCmd_sortBy:name",
      "placesCmd_cut",
      "placesCmd_copy",
      "placesCmd_paste",
      "placesCmd_delete",
      "placesCmd_showInFolder",
    ]) {
      win.goSetCommandEnabled(
        command,
        controller && controller.isCommandEnabled(command)
      );
    }
  },

  /**
   * Executes the given command on the currently active controller.
   *
   * @param {DOMWindow} win The window containing the affected view
   * @param {string} command The command to execute
   */
  doCommand(win, command) {
    let controller = this.getControllerForCommand(win, command);
    if (controller && controller.isCommandEnabled(command)) {
      PlacesUIUtils.lastContextMenuCommand = command;
      controller.doCommand(command);
    }
  },

  /**
   * By calling this before visiting an URL, the visit will be associated to a
   * TRANSITION_TYPED transition (if there is no a referrer).
   * This is used when visiting pages from the history menu, history sidebar,
   * url bar, url autocomplete results, and history searches from the places
   * organizer.  If this is not called visits will be marked as
   * TRANSITION_LINK.
   *
   * @param {string} aURL
   *   The URL to mark as typed.
   */
  markPageAsTyped: function PUIU_markPageAsTyped(aURL) {
    lazy.PlacesUtils.history.markPageAsTyped(
      Services.uriFixup.getFixupURIInfo(aURL).preferredURI
    );
  },

  /**
   * By calling this before visiting an URL, the visit will be associated to a
   * TRANSITION_BOOKMARK transition.
   * This is used when visiting pages from the bookmarks menu,
   * personal toolbar, and bookmarks from within the places organizer.
   * If this is not called visits will be marked as TRANSITION_LINK.
   *
   * @param {string} aURL
   *   The URL to mark as TRANSITION_BOOKMARK.
   */
  markPageAsFollowedBookmark: function PUIU_markPageAsFollowedBookmark(aURL) {
    lazy.PlacesUtils.history.markPageAsFollowedBookmark(
      Services.uriFixup.getFixupURIInfo(aURL).preferredURI
    );
  },

  /**
   * By calling this before visiting an URL, any visit in frames will be
   * associated to a TRANSITION_FRAMED_LINK transition.
   * This is actually used to distinguish user-initiated visits in frames
   * so automatic visits can be correctly ignored.
   *
   * @param {string} aURL
   *   The URL to mark as TRANSITION_FRAMED_LINK.
   */
  markPageAsFollowedLink: function PUIU_markPageAsFollowedLink(aURL) {
    lazy.PlacesUtils.history.markPageAsFollowedLink(
      Services.uriFixup.getFixupURIInfo(aURL).preferredURI
    );
  },

  /**
   * Sets the character-set for a page. The character set will not be saved
   * if the window is determined to be a private browsing window.
   *
   * @param {string|URL|nsIURI} url The URL of the page to set the charset on.
   * @param {string} charset character-set value.
   * @param {DOMWindow} window The window that the charset is being set from.
   * @returns {Promise}
   */
  async setCharsetForPage(url, charset, window) {
    if (lazy.PrivateBrowsingUtils.isWindowPrivate(window)) {
      return;
    }

    // UTF-8 is the default. If we are passed the value then set it to null,
    // to ensure any charset is removed from the database.
    if (charset.toLowerCase() == "utf-8") {
      charset = null;
    }

    await lazy.PlacesUtils.history.update({
      url,
      annotations: new Map([[lazy.PlacesUtils.CHARSET_ANNO, charset]]),
    });
  },

  /**
   * Allows opening of javascript/data URI only if the given node is
   * bookmarked (see bug 224521).
   *
   * @param {object} aURINode
   *        a URI node
   * @param {DOMWindow} aWindow
   *        a window on which a potential error alert is shown on.
   * @returns {boolean} true if it's safe to open the node in the browser, false otherwise.
   */
  checkURLSecurity: function PUIU_checkURLSecurity(aURINode, aWindow) {
    if (lazy.PlacesUtils.nodeIsBookmark(aURINode)) {
      return true;
    }

    var uri = Services.io.newURI(aURINode.uri);
    if (uri.schemeIs("javascript") || uri.schemeIs("data")) {
      const [title, errorStr] =
        PlacesUIUtils.promptLocalization.formatValuesSync([
          "places-error-title",
          "places-load-js-data-url-error",
        ]);
      Services.prompt.alert(aWindow, title, errorStr);
      return false;
    }
    return true;
  },

  /**
   * Check whether or not the given node represents a removable entry (either in
   * history or in bookmarks).
   *
   * @param {object} aNode
   *        a node, except the root node of a query.
   * @returns {boolean} true if the aNode represents a removable entry, false otherwise.
   */
  canUserRemove(aNode) {
    let parentNode = aNode.parent;
    if (!parentNode) {
      // canUserRemove doesn't accept root nodes.
      return false;
    }

    // Is it a query pointing to one of the special root folders?
    if (lazy.PlacesUtils.nodeIsQuery(parentNode)) {
      if (lazy.PlacesUtils.nodeIsFolderOrShortcut(aNode)) {
        let guid = lazy.PlacesUtils.getConcreteItemGuid(aNode);
        // If the parent folder is not a folder, it must be a query, and so this node
        // cannot be removed.
        if (lazy.PlacesUtils.isRootItem(guid)) {
          return false;
        }
      } else if (lazy.PlacesUtils.isVirtualLeftPaneItem(aNode.bookmarkGuid)) {
        // If the item is a left-pane top-level item, it can't be removed.
        return false;
      }
    }

    // If it's not a bookmark, or it's child of a query, we can remove it.
    if (aNode.itemId == -1 || lazy.PlacesUtils.nodeIsQuery(parentNode)) {
      return true;
    }

    // Otherwise it has to be a child of an editable folder.
    return !this.isFolderReadOnly(parentNode);
  },

  /**
   * DO NOT USE THIS API IN ADDONS. IT IS VERY LIKELY TO CHANGE WHEN THE SWITCH
   * TO GUIDS IS COMPLETE (BUG 1071511).
   *
   * Check whether or not the given Places node points to a folder which
   * should not be modified by the user (i.e. its children should be unremovable
   * and unmovable, new children should be disallowed, etc).
   * These semantics are not inherited, meaning that read-only folder may
   * contain editable items (for instance, the places root is read-only, but all
   * of its direct children aren't).
   *
   * You should only pass folder nodes.
   *
   * @param {object} placesNode
   *        any folder result node.
   * @throws if placesNode is not a folder result node or views is invalid.
   * @returns {boolean} true if placesNode is a read-only folder, false otherwise.
   */
  isFolderReadOnly(placesNode) {
    if (
      typeof placesNode != "object" ||
      !lazy.PlacesUtils.nodeIsFolderOrShortcut(placesNode)
    ) {
      throw new Error("invalid value for placesNode");
    }

    return (
      lazy.PlacesUtils.getConcreteItemGuid(placesNode) ==
      lazy.PlacesUtils.bookmarks.rootGuid
    );
  },

  /**
   * @param {Array<object>} aItemsToOpen
   *   needs to be an array of objects of the form:
   *   {uri: string, isBookmark: boolean}
   * @param {object} aEvent
   *   The associated event triggering the open.
   * @param {DOMWindow} aWindow
   *   The window associated with the event.
   */
  openTabset(aItemsToOpen, aEvent, aWindow) {
    if (!aItemsToOpen.length) {
      return;
    }

    let browserWindow = getBrowserWindow(aWindow);
    var urls = [];
    let isPrivate =
      browserWindow && lazy.PrivateBrowsingUtils.isWindowPrivate(browserWindow);
    for (let item of aItemsToOpen) {
      urls.push(item.uri);
      if (isPrivate) {
        continue;
      }

      if (item.isBookmark) {
        this.markPageAsFollowedBookmark(item.uri);
      } else {
        this.markPageAsTyped(item.uri);
      }
    }

    // whereToOpenLink doesn't return "window" when there's no browser window
    // open (Bug 630255).
    var where = browserWindow
      ? lazy.BrowserUtils.whereToOpenLink(aEvent, false, true)
      : "window";
    if (where == "window") {
      // There is no browser window open, thus open a new one.
      let args = Cc["@mozilla.org/array;1"].createInstance(Ci.nsIMutableArray);
      let stringsToLoad = Cc["@mozilla.org/array;1"].createInstance(
        Ci.nsIMutableArray
      );
      urls.forEach(url =>
        stringsToLoad.appendElement(lazy.PlacesUtils.toISupportsString(url))
      );
      args.appendElement(stringsToLoad);

      let features = "chrome,dialog=no,all";
      if (isPrivate) {
        features += ",private";
      }

      browserWindow = Services.ww.openWindow(
        aWindow,
        AppConstants.BROWSER_CHROME_URL,
        null,
        features,
        args
      );
      return;
    }

    var loadInBackground = where == "tabshifted";
    // For consistency, we want all the bookmarks to open in new tabs, instead
    // of having one of them replace the currently focused tab.  Hence we call
    // loadTabs with aReplace set to false.
    browserWindow.gBrowser.loadTabs(urls, {
      inBackground: loadInBackground,
      replace: false,
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });
  },

  /**
   * Loads a selected node's or nodes' URLs in tabs,
   * warning the user when lots of URLs are being opened
   *
   * @param {object | Array} nodeOrNodes
   *          Contains the node or nodes that we're opening in tabs
   * @param {event} event
   *          The DOM mouse/key event with modifier keys set that track the
   *          user's preferred destination window or tab.
   * @param {object} view
   *          The current view that contains the node or nodes selected for
   *          opening
   */
  openMultipleLinksInTabs(nodeOrNodes, event, view) {
    let window = view.ownerWindow;
    let urlsToOpen = [];

    if (lazy.PlacesUtils.nodeIsContainer(nodeOrNodes)) {
      urlsToOpen = lazy.PlacesUtils.getURLsForContainerNode(nodeOrNodes);
    } else {
      for (var i = 0; i < nodeOrNodes.length; i++) {
        // Skip over separators and folders.
        if (lazy.PlacesUtils.nodeIsURI(nodeOrNodes[i])) {
          urlsToOpen.push({
            uri: nodeOrNodes[i].uri,
            isBookmark: lazy.PlacesUtils.nodeIsBookmark(nodeOrNodes[i]),
          });
        }
      }
    }
    if (lazy.OpenInTabsUtils.confirmOpenInTabs(urlsToOpen.length, window)) {
      if (window.updateTelemetry) {
        window.updateTelemetry(urlsToOpen);
      }
      this.openTabset(urlsToOpen, event, window);
    }
  },

  /**
   * Loads the node's URL in the appropriate tab or window given the
   * user's preference specified by modifier keys tracked by a
   * DOM mouse/key event.
   *
   * @param {object} aNode
   *          An uri result node.
   * @param {object} aEvent
   *          The DOM mouse/key event with modifier keys set that track the
   *          user's preferred destination window or tab.
   */
  openNodeWithEvent: function PUIU_openNodeWithEvent(aNode, aEvent) {
    let window = aEvent.target.ownerGlobal;

    let where = lazy.BrowserUtils.whereToOpenLink(aEvent, false, true);
    if (this.loadBookmarksInTabs && lazy.PlacesUtils.nodeIsBookmark(aNode)) {
      if (where == "current" && !aNode.uri.startsWith("javascript:")) {
        where = "tab";
      }
      let browserWindow = getBrowserWindow(window);
      if (where == "tab" && browserWindow?.gBrowser.selectedTab.isEmpty) {
        where = "current";
      }
    }

    this._openNodeIn(aNode, where, window);
  },

  /**
   * Loads the node's URL in the appropriate tab or window.
   * see also URILoadingHelper's openWebLinkIn
   *
   * @param {object} aNode
   *        An uri result node.
   * @param {string} aWhere
   *        Where to open the URL.
   * @param {object} aView
   *        The associated view of the node being opened.
   * @param {boolean} aPrivate
   *        True if the window being opened is private.
   */
  openNodeIn: function PUIU_openNodeIn(aNode, aWhere, aView, aPrivate) {
    let window = aView.ownerWindow;
    this._openNodeIn(aNode, aWhere, window, { aPrivate });
  },

  _openNodeIn: function PUIU__openNodeIn(
    aNode,
    aWhere,
    aWindow,
    { aPrivate = false, userContextId = 0 } = {}
  ) {
    if (
      aNode &&
      this.checkURLSecurity(aNode, aWindow) &&
      this.isURILike(aNode)
    ) {
      let isBookmark = lazy.PlacesUtils.nodeIsBookmark(aNode);

      if (!lazy.PrivateBrowsingUtils.isWindowPrivate(aWindow)) {
        if (isBookmark) {
          this.markPageAsFollowedBookmark(aNode.uri);
        } else {
          this.markPageAsTyped(aNode.uri);
        }
      } else {
        // This is a targeted fix for bug 1792163, where it was discovered
        // that if you open the Library from a Private Browsing window, and then
        // use the "Open in New Window" context menu item to open a new window,
        // that the window will open under the wrong icon on the Windows taskbar.
        aPrivate = true;
      }

      const isJavaScriptURL = aNode.uri.startsWith("javascript:");
      aWindow.openTrustedLinkIn(aNode.uri, aWhere, {
        allowPopups: isJavaScriptURL,
        inBackground: this.loadBookmarksInBackground,
        allowInheritPrincipal: isJavaScriptURL,
        private: aPrivate,
        userContextId,
      });
      if (aWindow.updateTelemetry) {
        aWindow.updateTelemetry([aNode]);
      }
    }
  },

  /**
   * Determines whether a node contains a uri
   *
   * @param {nsINavHistoryResultNode | DOMElement} aNode A result node.
   * @returns {boolean} whether the node contains a uri
   */
  isURILike(aNode) {
    return lazy.PlacesUtils.nodeIsURI(aNode) || !!aNode.uri;
  },

  /**
   * Helper for guessing scheme from an url string.
   * Used to avoid nsIURI overhead in frequently called UI functions. This is not
   * supposed be perfect, so use it only for UI purposes.
   *
   * @param {string} href The url to guess the scheme from.
   * @returns {string} guessed scheme for this url string.
   */
  guessUrlSchemeForUI(href) {
    return href.substr(0, href.indexOf(":"));
  },

  getBestTitle: function PUIU_getBestTitle(aNode, aDoNotCutTitle) {
    var title;
    if (!aNode.title && lazy.PlacesUtils.nodeIsURI(aNode)) {
      // if node title is empty, try to set the label using host and filename
      // Services.io.newURI will throw if aNode.uri is not a valid URI
      try {
        var uri = Services.io.newURI(aNode.uri);
        var host = uri.host;
        var fileName = uri.QueryInterface(Ci.nsIURL).fileName;
        // if fileName is empty, use path to distinguish labels
        if (aDoNotCutTitle) {
          title = host + uri.pathQueryRef;
        } else {
          title =
            host +
            (fileName
              ? (host ? "/" + this.ellipsis + "/" : "") + fileName
              : uri.pathQueryRef);
        }
      } catch (e) {
        // Use (no title) for non-standard URIs (data:, javascript:, ...)
        title = "";
      }
    } else {
      title = aNode.title;
    }

    return title || this.promptLocalization.formatValueSync("places-no-title");
  },

  shouldShowTabsFromOtherComputersMenuitem() {
    let weaveOK =
      lazy.Weave.Status.checkSetup() != lazy.CLIENT_NOT_CONFIGURED &&
      lazy.Weave.Svc.PrefBranch.getCharPref("firstSync", "") != "notReady";
    return weaveOK;
  },

  /**
   * WARNING TO ADDON AUTHORS: DO NOT USE THIS METHOD. IT'S LIKELY TO BE REMOVED IN A
   * FUTURE RELEASE.
   *
   * Checks if a place: href represents a folder shortcut.
   *
   * @param {string} queryString
   *        the query string to check (a place: href)
   * @returns {boolean} whether or not queryString represents a folder shortcut.
   * @throws if queryString is malformed.
   */
  isFolderShortcutQueryString(queryString) {
    // Based on GetSimpleBookmarksQueryFolder in nsNavHistory.cpp.

    let query = {},
      options = {};
    lazy.PlacesUtils.history.queryStringToQuery(queryString, query, options);
    query = query.value;
    options = options.value;
    return (
      query.folderCount == 1 &&
      !query.hasBeginTime &&
      !query.hasEndTime &&
      !query.hasDomain &&
      !query.hasURI &&
      !query.hasSearchTerms &&
      !query.tags.length == 0 &&
      options.maxResults == 0
    );
  },

  /**
   * Helpers for consumers of editBookmarkOverlay which don't have a node as their input.
   *
   * Given a bookmark object for either a url bookmark or a folder, returned by
   * Bookmarks.fetch (see Bookmark.sys.mjs), this creates a node-like object
   * suitable for initialising the edit overlay with it.
   *
   * @param {object} aFetchInfo
   *        a bookmark object returned by Bookmarks.fetch.
   * @returns {object} a node-like object suitable for initialising editBookmarkOverlay.
   * @throws if aFetchInfo is representing a separator.
   */
  async promiseNodeLikeFromFetchInfo(aFetchInfo) {
    if (aFetchInfo.itemType == lazy.PlacesUtils.bookmarks.TYPE_SEPARATOR) {
      throw new Error("promiseNodeLike doesn't support separators");
    }

    let parent = {
      bookmarkGuid: aFetchInfo.parentGuid,
      type: Ci.nsINavHistoryResultNode.RESULT_TYPE_FOLDER,
    };

    return Object.freeze({
      bookmarkGuid: aFetchInfo.guid,
      title: aFetchInfo.title,
      uri: aFetchInfo.url !== undefined ? aFetchInfo.url.href : "",

      get type() {
        if (aFetchInfo.itemType == lazy.PlacesUtils.bookmarks.TYPE_FOLDER) {
          return Ci.nsINavHistoryResultNode.RESULT_TYPE_FOLDER;
        }

        if (!this.uri.length) {
          throw new Error("Unexpected item type");
        }

        if (/^place:/.test(this.uri)) {
          if (this.isFolderShortcutQueryString(this.uri)) {
            return Ci.nsINavHistoryResultNode.RESULT_TYPE_FOLDER_SHORTCUT;
          }

          return Ci.nsINavHistoryResultNode.RESULT_TYPE_QUERY;
        }

        return Ci.nsINavHistoryResultNode.RESULT_TYPE_URI;
      },

      get parent() {
        return parent;
      },
    });
  },

  /**
   * This function wraps potentially large places transaction operations
   * with batch notifications to the result node, hence switching the views
   * to batch mode. If resultNode is not supplied, the function will
   * pass-through to functionToWrap.
   *
   * @param {nsINavHistoryResult} resultNode The result node to turn on batching.
   * @param {number} itemsBeingChanged The count of items being changed. If the
   *                                    count is lower than a threshold, then
   *                                    batching won't be set.
   * @param {Function} functionToWrap The function to
   * @returns {object} forwards the functionToWrap return value.
   */
  async batchUpdatesForNode(resultNode, itemsBeingChanged, functionToWrap) {
    if (!resultNode) {
      return functionToWrap();
    }

    if (itemsBeingChanged > ITEM_CHANGED_BATCH_NOTIFICATION_THRESHOLD) {
      resultNode.onBeginUpdateBatch();
    }

    try {
      return await functionToWrap();
    } finally {
      if (itemsBeingChanged > ITEM_CHANGED_BATCH_NOTIFICATION_THRESHOLD) {
        resultNode.onEndUpdateBatch();
      }
    }
  },

  /**
   * Processes a set of transfer items that have been dropped or pasted.
   * Batching will be applied where necessary.
   *
   * @param {Array} items A list of unwrapped nodes to process.
   * @param {object} insertionPoint The requested point for insertion.
   * @param {boolean} doCopy Set to true to copy the items, false will move them
   *                         if possible.
   * @param {object} view The view that should be used for batching.
   * @returns {Array} Returns an empty array when the insertion point is a tag, else
   *                 returns an array of copied or moved guids.
   */
  async handleTransferItems(items, insertionPoint, doCopy, view) {
    let transactions;
    let itemsCount;
    if (insertionPoint.isTag) {
      let urls = items.filter(item => "uri" in item).map(item => item.uri);
      itemsCount = urls.length;
      transactions = [
        lazy.PlacesTransactions.Tag({ urls, tag: insertionPoint.tagName }),
      ];
    } else {
      let insertionIndex = await insertionPoint.getIndex();
      itemsCount = items.length;
      transactions = getTransactionsForTransferItems(
        items,
        insertionIndex,
        insertionPoint.guid,
        !doCopy
      );
    }

    // Check if we actually have something to add, if we don't it probably wasn't
    // valid, or it was moving to the same location, so just ignore it.
    if (!transactions.length) {
      return [];
    }

    let guidsToSelect = await this.batchUpdatesForNode(
      getResultForBatching(view),
      itemsCount,
      async () =>
        lazy.PlacesTransactions.batch(transactions, "handleTransferItems")
    );

    // If we're inserting into a tag, we don't get the resulting guids.
    return insertionPoint.isTag ? [] : guidsToSelect.flat();
  },

  onSidebarTreeClick(event) {
    // right-clicks are not handled here
    if (event.button == 2) {
      return;
    }

    let tree = event.target.parentNode;
    let cell = tree.getCellAt(event.clientX, event.clientY);
    if (cell.row == -1 || cell.childElt == "twisty") {
      return;
    }

    // getCoordsForCellItem returns the x coordinate in logical coordinates
    // (i.e., starting from the left and right sides in LTR and RTL modes,
    // respectively.)  Therefore, we make sure to exclude the blank area
    // before the tree item icon (that is, to the left or right of it in
    // LTR and RTL modes, respectively) from the click target area.
    let win = tree.ownerGlobal;
    let rect = tree.getCoordsForCellItem(cell.row, cell.col, "image");
    let isRTL = win.getComputedStyle(tree).direction == "rtl";
    let mouseInGutter = isRTL ? event.clientX > rect.x : event.clientX < rect.x;

    let metaKey =
      AppConstants.platform === "macosx" ? event.metaKey : event.ctrlKey;
    let modifKey = metaKey || event.shiftKey;
    let isContainer = tree.view.isContainer(cell.row);
    let openInTabs =
      isContainer &&
      (event.button == 1 || (event.button == 0 && modifKey)) &&
      lazy.PlacesUtils.hasChildURIs(tree.view.nodeForTreeIndex(cell.row));

    if (event.button == 0 && isContainer && !openInTabs) {
      tree.view.toggleOpenState(cell.row);
    } else if (
      !mouseInGutter &&
      openInTabs &&
      event.originalTarget.localName == "treechildren"
    ) {
      tree.view.selection.select(cell.row);
      this.openMultipleLinksInTabs(tree.selectedNode, event, tree);
    } else if (
      !mouseInGutter &&
      !isContainer &&
      event.originalTarget.localName == "treechildren"
    ) {
      // Clear all other selection since we're loading a link now. We must
      // do this *before* attempting to load the link since openURL uses
      // selection as an indication of which link to load.
      tree.view.selection.select(cell.row);
      this.openNodeWithEvent(tree.selectedNode, event);
    }
  },

  onSidebarTreeKeyPress(event) {
    let node = event.target.selectedNode;
    if (node) {
      if (event.keyCode == event.DOM_VK_RETURN) {
        PlacesUIUtils.openNodeWithEvent(node, event);
      }
    }
  },

  /**
   * The following function displays the URL of a node that is being
   * hovered over.
   *
   * @param {object} event
   *   The event that triggered the hover.
   */
  onSidebarTreeMouseMove(event) {
    let treechildren = event.target;
    if (treechildren.localName != "treechildren") {
      return;
    }

    let tree = treechildren.parentNode;
    let cell = tree.getCellAt(event.clientX, event.clientY);

    // cell.row is -1 when the mouse is hovering an empty area within the tree.
    // To avoid showing a URL from a previously hovered node for a currently
    // hovered non-url node, we must clear the moused-over URL in these cases.
    if (cell.row != -1) {
      let node = tree.view.nodeForTreeIndex(cell.row);
      if (lazy.PlacesUtils.nodeIsURI(node)) {
        this.setMouseoverURL(node.uri, tree.ownerGlobal);
        return;
      }
    }
    this.setMouseoverURL("", tree.ownerGlobal);
  },

  setMouseoverURL(url, win) {
    // When the browser window is closed with an open sidebar, the sidebar
    // unload event happens after the browser's one.  In this case
    // top.XULBrowserWindow has been nullified already.
    if (win.top.XULBrowserWindow) {
      win.top.XULBrowserWindow.setOverLink(url);
    }
  },

  /**
   * Uncollapses PersonalToolbar if its collapsed status is not
   * persisted, and user customized it or changed default bookmarks.
   *
   * If the user does not have a persisted value for the toolbar's
   * "collapsed" attribute, try to determine whether it's customized.
   *
   * @param {boolean} aForceVisible Set to true to ignore if the user had
   * previously collapsed the toolbar manually.
   */
  NUM_TOOLBAR_BOOKMARKS_TO_UNHIDE: 3,
  async maybeToggleBookmarkToolbarVisibility(aForceVisible = false) {
    const BROWSER_DOCURL = AppConstants.BROWSER_CHROME_URL;
    let xulStore = Services.xulStore;

    if (
      aForceVisible ||
      !xulStore.hasValue(BROWSER_DOCURL, "PersonalToolbar", "collapsed")
    ) {
      function uncollapseToolbar() {
        Services.obs.notifyObservers(
          null,
          "browser-set-toolbar-visibility",
          JSON.stringify([lazy.CustomizableUI.AREA_BOOKMARKS, "true"])
        );
      }
      // We consider the toolbar customized if it has more than
      // NUM_TOOLBAR_BOOKMARKS_TO_UNHIDE children, or if it has a persisted
      // currentset value.
      let toolbarIsCustomized = xulStore.hasValue(
        BROWSER_DOCURL,
        "PersonalToolbar",
        "currentset"
      );
      if (aForceVisible || toolbarIsCustomized) {
        uncollapseToolbar();
        return;
      }

      let numBookmarksOnToolbar = (
        await lazy.PlacesUtils.bookmarks.fetch(
          lazy.PlacesUtils.bookmarks.toolbarGuid
        )
      ).childCount;
      if (numBookmarksOnToolbar > this.NUM_TOOLBAR_BOOKMARKS_TO_UNHIDE) {
        uncollapseToolbar();
      }
    }
  },

  /**
   * Determines whether the given "placesContext" menu item would open a link
   * under some special conditions, but those special conditions cannot be met.
   *
   * @param {Element} item The menu or menu item to decide for.
   *
   * @returns {boolean} Whether the item is an "open" item that should be
   *   hidden.
   */
  shouldHideOpenMenuItem(item) {
    if (
      item.hasAttribute("hide-if-disabled-private-browsing") &&
      !lazy.PrivateBrowsingUtils.enabled
    ) {
      return true;
    }

    if (
      item.hasAttribute("hide-if-private-browsing") &&
      lazy.PrivateBrowsingUtils.isWindowPrivate(item.ownerGlobal)
    ) {
      return true;
    }

    if (
      item.hasAttribute("hide-if-usercontext-disabled") &&
      !Services.prefs.getBoolPref("privacy.userContext.enabled", false)
    ) {
      return true;
    }

    return false;
  },

  async managedPlacesContextShowing(event) {
    let menupopup = event.target;
    let document = menupopup.ownerDocument;
    let window = menupopup.ownerGlobal;
    // We need to populate the submenus in order to have information
    // to show the context menu.
    if (
      menupopup.triggerNode.id == "managed-bookmarks" &&
      !menupopup.triggerNode.menupopup.hasAttribute("hasbeenopened")
    ) {
      await window.PlacesToolbarHelper.populateManagedBookmarks(
        menupopup.triggerNode.menupopup
      );
    }
    // Hide everything. We'll unhide the things we need.
    Array.from(menupopup.children).forEach(function (child) {
      child.hidden = true;
    });
    // Store triggerNode in controller for checking if commands are enabled
    this.managedBookmarksController.triggerNode = menupopup.triggerNode;
    // Container in this context means a folder.
    let isFolder = menupopup.triggerNode.hasAttribute("container");
    if (isFolder) {
      // Disable the openContainerInTabs menuitem if there
      // are no children of the menu that have links.
      let openContainerInTabs_menuitem = document.getElementById(
        "placesContext_openContainer:tabs"
      );
      let menuitems = menupopup.triggerNode.menupopup.children;
      let openContainerInTabs = Array.from(menuitems).some(
        menuitem => menuitem.link
      );
      openContainerInTabs_menuitem.disabled = !openContainerInTabs;
      openContainerInTabs_menuitem.hidden = false;
    } else {
      for (let id of [
        "placesContext_open:newtab",
        "placesContext_open:newcontainertab",
        "placesContext_open:newwindow",
        "placesContext_open:newprivatewindow",
      ]) {
        let item = document.getElementById(id);
        item.hidden = this.shouldHideOpenMenuItem(item);
      }
      for (let id of ["placesContext_openSeparator", "placesContext_copy"]) {
        document.getElementById(id).hidden = false;
      }
    }

    event.target.ownerGlobal.updateCommands("places");
  },

  placesContextShowing(event) {
    let menupopup = event.target;
    if (
      !["placesContext", "sidebar-history-context-menu"].includes(menupopup.id)
    ) {
      // Ignore any popupshowing events from submenus
      return;
    }

    if (menupopup.id == "sidebar-history-context-menu") {
      PlacesUIUtils.lastContextMenuTriggerNode =
        menupopup.triggerNode.triggerNode;
      return;
    }

    PlacesUIUtils.lastContextMenuTriggerNode = menupopup.triggerNode;

    if (Services.prefs.getBoolPref("browser.tabs.loadBookmarksInTabs", false)) {
      menupopup.ownerDocument
        .getElementById("placesContext_open")
        .removeAttribute("default");
      menupopup.ownerDocument
        .getElementById("placesContext_open:newtab")
        .setAttribute("default", "true");
      // else clause ensures correct behavior if pref is repeatedly toggled
    } else {
      menupopup.ownerDocument
        .getElementById("placesContext_open:newtab")
        .removeAttribute("default");
      menupopup.ownerDocument
        .getElementById("placesContext_open")
        .setAttribute("default", "true");
    }

    let isManaged = !!menupopup.triggerNode.closest("#managed-bookmarks");
    if (isManaged) {
      this.managedPlacesContextShowing(event);
      return;
    }
    menupopup._view = this.getViewForNode(menupopup.triggerNode);
    if (!menupopup._view) {
      // This can happen if we try to invoke the context menu on
      // an uninitialized places toolbar. Just bail out:
      event.preventDefault();
      return;
    }
    if (!this.openInTabClosesMenu) {
      menupopup.ownerDocument
        .getElementById("placesContext_open:newtab")
        .setAttribute("closemenu", "single");
    }
    if (!menupopup._view.buildContextMenu(menupopup)) {
      event.preventDefault();
    }
  },

  placesContextHiding(event) {
    let menupopup = event.target;
    if (menupopup._view) {
      menupopup._view.destroyContextMenu();
    }

    if (
      [
        "sidebar-history-context-menu",
        "placesContext",
        "sidebar-synced-tabs-context-menu",
      ].includes(menupopup.id)
    ) {
      PlacesUIUtils.lastContextMenuTriggerNode = null;
      PlacesUIUtils.lastContextMenuCommand = null;
    }
  },

  createContainerTabMenu(event) {
    let window = event.target.ownerGlobal;
    return window.createUserContextMenu(event, { isContextMenu: true });
  },

  openInContainerTab(event) {
    PlacesUIUtils.lastContextMenuCommand = "placesCmd_open:newcontainertab";
    let userContextId = parseInt(
      event.target.getAttribute("data-usercontextid")
    );
    let triggerNode = this.lastContextMenuTriggerNode;
    let isManaged = !!triggerNode?.closest("#managed-bookmarks");
    if (isManaged) {
      let window = triggerNode.ownerGlobal;
      window.openTrustedLinkIn(triggerNode.link, "tab", { userContextId });
      return;
    }
    let view = this.getViewForNode(triggerNode);
    this._openNodeIn(
      view?.selectedNode || triggerNode,
      "tab",
      view?.ownerWindow || triggerNode.ownerGlobal.top,
      {
        userContextId,
      }
    );
  },

  openSelectionInTabs(event) {
    let isManaged =
      !!event.target.parentNode.triggerNode.closest("#managed-bookmarks");
    let controller;
    if (isManaged) {
      controller = this.managedBookmarksController;
    } else {
      controller = PlacesUIUtils.getViewForNode(
        PlacesUIUtils.lastContextMenuTriggerNode
      ).controller;
    }
    controller.openSelectionInTabs(event);
  },

  managedBookmarksController: {
    triggerNode: null,

    openSelectionInTabs(event) {
      let window = event.target.ownerGlobal;
      let menuitems = event.target.parentNode.triggerNode.menupopup.children;
      let items = [];
      for (let i = 0; i < menuitems.length; i++) {
        if (menuitems[i].link) {
          let item = {};
          item.uri = menuitems[i].link;
          item.isBookmark = true;
          items.push(item);
        }
      }
      PlacesUIUtils.openTabset(items, event, window);
    },

    isCommandEnabled(command) {
      switch (command) {
        case "placesCmd_copy":
        case "placesCmd_open:window":
        case "placesCmd_open:privatewindow":
        case "placesCmd_open:tab": {
          return true;
        }
      }
      return false;
    },

    doCommand(command) {
      let window = this.triggerNode.ownerGlobal;
      switch (command) {
        case "placesCmd_copy": {
          lazy.BrowserUtils.copyLink(
            this.triggerNode.link,
            this.triggerNode.label
          );
          break;
        }
        case "placesCmd_open:privatewindow":
          window.openTrustedLinkIn(this.triggerNode.link, "window", {
            private: true,
          });
          break;
        case "placesCmd_open:window":
          window.openTrustedLinkIn(this.triggerNode.link, "window", {
            private: false,
          });
          break;
        case "placesCmd_open:tab": {
          window.openTrustedLinkIn(this.triggerNode.link, "tab");
        }
      }
    },
  },

  async maybeAddImportButton() {
    if (!Services.policies.isAllowed("profileImport")) {
      return;
    }

    let numberOfBookmarks = await lazy.PlacesUtils.withConnectionWrapper(
      "PlacesUIUtils: maybeAddImportButton",
      async db => {
        let rows = await db.execute(
          `SELECT COUNT(*) as n FROM moz_bookmarks b
           JOIN moz_bookmarks p ON p.id = b.parent
           WHERE p.guid = :guid`,
          { guid: lazy.PlacesUtils.bookmarks.toolbarGuid }
        );
        return rows[0].getResultByName("n");
      }
    ).catch(e => {
      // We want to report errors, but we still want to add the button then:
      console.error(e);
      return 0;
    });

    if (numberOfBookmarks < 3) {
      lazy.CustomizableUI.addWidgetToArea(
        "import-button",
        lazy.CustomizableUI.AREA_BOOKMARKS,
        0
      );
      Services.prefs.setBoolPref("browser.bookmarks.addedImportButton", true);
      this.removeImportButtonWhenImportSucceeds();
    }
  },

  removeImportButtonWhenImportSucceeds() {
    // If the user (re)moved the button, clear the pref and stop worrying about
    // moving the item.
    let placement = lazy.CustomizableUI.getPlacementOfWidget("import-button");
    if (placement?.area != lazy.CustomizableUI.AREA_BOOKMARKS) {
      Services.prefs.clearUserPref("browser.bookmarks.addedImportButton");
      return;
    }
    // Otherwise, wait for a successful migration:
    let obs = (subject, topic, data) => {
      if (
        data == lazy.MigrationUtils.resourceTypes.BOOKMARKS &&
        lazy.MigrationUtils.getImportedCount("bookmarks") > 0
      ) {
        lazy.CustomizableUI.removeWidgetFromArea("import-button");
        Services.prefs.clearUserPref("browser.bookmarks.addedImportButton");
        Services.obs.removeObserver(obs, "Migration:ItemAfterMigrate");
        Services.obs.removeObserver(obs, "Migration:ItemError");
      }
    };
    Services.obs.addObserver(obs, "Migration:ItemAfterMigrate");
    Services.obs.addObserver(obs, "Migration:ItemError");
  },

  /**
   * Tries to initiate a speculative connection to a given url. This is not
   * infallible, if a speculative connection cannot be initialized, it will be a
   * no-op.
   *
   * @param {nsIURI|URL|string} url entity to initiate
   *        a speculative connection for.
   * @param {window} window the window from where the connection is initialized.
   */
  setupSpeculativeConnection(url, window) {
    if (
      !Services.prefs.getBoolPref(
        "browser.places.speculativeConnect.enabled",
        true
      )
    ) {
      return;
    }
    if (!url.startsWith("http")) {
      return;
    }
    try {
      let uri = url instanceof Ci.nsIURI ? url : Services.io.newURI(url);
      Services.io.speculativeConnect(
        uri,
        window.gBrowser.contentPrincipal,
        null,
        false
      );
    } catch (ex) {
      // Can't setup speculative connection for this url, just ignore it.
    }
  },

  /**
   * Sets up a speculative connection to the target of a
   * clicked places DOM node on left and middle click.
   *
   * @param {event} event the mousedown event.
   */
  maybeSpeculativeConnectOnMouseDown(event) {
    if (
      event.type == "mousedown" &&
      event.target._placesNode?.uri &&
      event.button != 2
    ) {
      PlacesUIUtils.setupSpeculativeConnection(
        event.target._placesNode.uri,
        event.target.ownerGlobal
      );
    }
  },

  /**
   * Generates a cached-favicon: link for an icon URL, that will allow to fetch
   * the icon from the local favicons cache, rather than from the network.
   * If the icon URL is invalid, fallbacks to the default favicon URL.
   *
   * @param {string} icon The url of the icon to load from local cache.
   * @returns {string} a "cached-favicon:" prefixed URL, unless the original
   *   URL protocol refers to a local resource, then it will just pass-through
   *   unchanged.
   */
  getImageURL(icon) {
    // don't initiate a connection just to fetch a favicon (see bug 467828)
    try {
      return lazy.PlacesUtils.favicons.getFaviconLinkForIcon(
        Services.io.newURI(icon)
      ).spec;
    } catch (ex) {}
    return lazy.PlacesUtils.favicons.defaultFavicon.spec;
  },

  /**
   * Determines the string indexes where titles differ from similar titles (where
   * the first n characters are the same) in the provided list of items, and
   * adds that into the item.
   *
   * This assumes the titles will be displayed along the lines of
   * `Start of title ... place where differs` the index would be reference
   * the `p` here.
   *
   * @param {object[]} candidates
   *   An array of candidates to modify. The candidates should have a `title`
   *   property which should be a string or null.
   *   The order of the array does not matter. The objects are modified
   *   in-place.
   *   If a difference to other similar titles is found then a
   *   `titleDifferentIndex` property will be inserted into all similar
   *   candidates with the index of the start of the difference.
   */
  insertTitleStartDiffs(candidates) {
    function findStartDifference(a, b) {
      let i;
      // We already know the start is the same, so skip that part.
      for (i = PlacesUIUtils.similarTitlesMinChars; i < a.length; i++) {
        if (a[i] != b[i]) {
          return i;
        }
      }
      if (b.length > i) {
        return i;
      }
      // They are the same.
      return -1;
    }

    let longTitles = new Map();

    for (let candidate of candidates) {
      // Title is too short for us to care about, simply continue.
      if (
        !candidate.title ||
        candidate.title.length < this.similarTitlesMinChars
      ) {
        continue;
      }
      let titleBeginning = candidate.title.slice(0, this.similarTitlesMinChars);
      let matches = longTitles.get(titleBeginning);
      if (matches) {
        for (let match of matches) {
          let startDiff = findStartDifference(candidate.title, match.title);
          if (startDiff > 0) {
            candidate.titleDifferentIndex = startDiff;
            // If we have an existing difference index for the match, move
            // it forward if this one is earlier in the string.
            if (
              !("titleDifferentIndex" in match) ||
              match.titleDifferentIndex > startDiff
            ) {
              match.titleDifferentIndex = startDiff;
            }
          }
        }

        matches.push(candidate);
      } else {
        longTitles.set(titleBeginning, [candidate]);
      }
    }
  },
};

/**
 * Promise used by the toolbar view browser-places to determine whether we
 * can start loading its content (which involves IO, and so is postponed
 * during startup).
 */
PlacesUIUtils.canLoadToolbarContentPromise = new Promise(resolve => {
  PlacesUIUtils.unblockToolbars = resolve;
});

// These are lazy getters to avoid importing PlacesUtils immediately.
ChromeUtils.defineLazyGetter(PlacesUIUtils, "PLACES_FLAVORS", () => {
  return [
    lazy.PlacesUtils.TYPE_X_MOZ_PLACE_CONTAINER,
    lazy.PlacesUtils.TYPE_X_MOZ_PLACE_SEPARATOR,
    lazy.PlacesUtils.TYPE_X_MOZ_PLACE,
  ];
});
ChromeUtils.defineLazyGetter(PlacesUIUtils, "URI_FLAVORS", () => {
  return [
    lazy.PlacesUtils.TYPE_X_MOZ_URL,
    TAB_DROP_TYPE,
    lazy.PlacesUtils.TYPE_PLAINTEXT,
  ];
});
ChromeUtils.defineLazyGetter(PlacesUIUtils, "SUPPORTED_FLAVORS", () => {
  return [...PlacesUIUtils.PLACES_FLAVORS, ...PlacesUIUtils.URI_FLAVORS];
});

ChromeUtils.defineLazyGetter(PlacesUIUtils, "ellipsis", function () {
  return Services.prefs.getComplexValue(
    "intl.ellipsis",
    Ci.nsIPrefLocalizedString
  ).data;
});

ChromeUtils.defineLazyGetter(PlacesUIUtils, "promptLocalization", () => {
  return new Localization(
    ["browser/placesPrompts.ftl", "branding/brand.ftl"],
    true
  );
});

XPCOMUtils.defineLazyPreferenceGetter(
  PlacesUIUtils,
  "similarTitlesMinChars",
  "browser.places.similarTitlesMinChars",
  20
);
XPCOMUtils.defineLazyPreferenceGetter(
  PlacesUIUtils,
  "loadBookmarksInBackground",
  "browser.tabs.loadBookmarksInBackground",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  PlacesUIUtils,
  "loadBookmarksInTabs",
  "browser.tabs.loadBookmarksInTabs",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  PlacesUIUtils,
  "openInTabClosesMenu",
  "browser.bookmarks.openInTabClosesMenu",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  PlacesUIUtils,
  "maxRecentFolders",
  "browser.bookmarks.editDialog.maxRecentFolders",
  7
);

XPCOMUtils.defineLazyPreferenceGetter(
  PlacesUIUtils,
  "defaultParentGuid",
  "browser.bookmarks.defaultLocation",
  "", // Avoid eagerly loading PlacesUtils.
  null,
  async prefValue => {
    if (!prefValue) {
      return lazy.PlacesUtils.bookmarks.toolbarGuid;
    }
    if (["toolbar", "menu", "unfiled"].includes(prefValue)) {
      return lazy.PlacesUtils.bookmarks[prefValue + "Guid"];
    }

    try {
      return await lazy.PlacesUtils.bookmarks
        .fetch({ guid: prefValue })
        .then(bm => bm.guid);
    } catch (ex) {
      // The guid may have an invalid format.
      return lazy.PlacesUtils.bookmarks.toolbarGuid;
    }
  }
);

/**
 * Determines if an unwrapped node can be moved.
 *
 * @param {object} unwrappedNode
 *        A node unwrapped by PlacesUtils.unwrapNodes().
 * @returns {boolean} True if the node can be moved, false otherwise.
 */
function canMoveUnwrappedNode(unwrappedNode) {
  if (
    (unwrappedNode.concreteGuid &&
      lazy.PlacesUtils.isRootItem(unwrappedNode.concreteGuid)) ||
    (unwrappedNode.guid && lazy.PlacesUtils.isRootItem(unwrappedNode.guid))
  ) {
    return false;
  }

  let parentGuid = unwrappedNode.parentGuid;
  if (parentGuid == lazy.PlacesUtils.bookmarks.rootGuid) {
    return false;
  }

  return true;
}

/**
 * This gets the most appropriate item for using for batching. In the case of multiple
 * views being related, the method returns the most expensive result to batch.
 * For example, if it detects the left-hand library pane, then it will look for
 * and return the reference to the right-hand pane.
 *
 * @param {object} viewOrElement The item to check.
 * @returns {object} Will return the best result node to batch, or null
 *                  if one could not be found.
 */
function getResultForBatching(viewOrElement) {
  if (
    viewOrElement &&
    Element.isInstance(viewOrElement) &&
    viewOrElement.id === "placesList"
  ) {
    // Note: fall back to the existing item if we can't find the right-hane pane.
    viewOrElement =
      viewOrElement.ownerDocument.getElementById("placeContent") ||
      viewOrElement;
  }

  if (viewOrElement && viewOrElement.result) {
    return viewOrElement.result;
  }

  return null;
}

/**
 * Processes a set of transfer items and returns transactions to insert or
 * move them.
 *
 * @param {Array} items A list of unwrapped nodes to get transactions for.
 * @param {number} insertionIndex The requested index for insertion.
 * @param {string} insertionParentGuid The guid of the parent folder to insert
 *                                     or move the items to.
 * @param {boolean} doMove Set to true to MOVE the items if possible, false will
 *                         copy them.
 * @returns {Array} Returns an array of created PlacesTransactions.
 */
function getTransactionsForTransferItems(
  items,
  insertionIndex,
  insertionParentGuid,
  doMove
) {
  let canMove = true;
  for (let item of items) {
    if (!PlacesUIUtils.SUPPORTED_FLAVORS.includes(item.type)) {
      throw new Error(`Unsupported '${item.type}' data type`);
    }

    // Work out if this is data from the same app session we're running in.
    if (
      !("instanceId" in item) ||
      item.instanceId != lazy.PlacesUtils.instanceId
    ) {
      if (item.type == lazy.PlacesUtils.TYPE_X_MOZ_PLACE_CONTAINER) {
        throw new Error(
          "Can't copy a container from a legacy-transactions build"
        );
      }
      // Only log if this is one of "our" types as external items, e.g. drag from
      // url bar to toolbar, shouldn't complain.
      if (PlacesUIUtils.PLACES_FLAVORS.includes(item.type)) {
        console.error(
          "Tried to move an unmovable Places " +
            "node, reverting to a copy operation."
        );
      }

      // We can never move from an external copy.
      canMove = false;
    }

    if (doMove && canMove) {
      canMove = canMoveUnwrappedNode(item);
    }
  }

  if (doMove && !canMove) {
    doMove = false;
  }

  if (doMove) {
    // Move is simple, we pass the transaction a list of GUIDs and where to move
    // them to.
    return [
      lazy.PlacesTransactions.Move({
        guids: items.map(item => item.itemGuid),
        newParentGuid: insertionParentGuid,
        newIndex: insertionIndex,
      }),
    ];
  }

  return getTransactionsForCopy(items, insertionIndex, insertionParentGuid);
}

/**
 * Processes a set of transfer items and returns an array of transactions.
 *
 * @param {Array} items A list of unwrapped nodes to get transactions for.
 * @param {number} insertionIndex The requested index for insertion.
 * @param {string} insertionParentGuid The guid of the parent folder to insert
 *                                     or move the items to.
 * @returns {Array} Returns an array of created PlacesTransactions.
 */
function getTransactionsForCopy(items, insertionIndex, insertionParentGuid) {
  let transactions = [];
  let index = insertionIndex;

  for (let item of items) {
    let transaction;
    let guid = item.itemGuid;

    if (
      PlacesUIUtils.PLACES_FLAVORS.includes(item.type) &&
      // For anything that is comming from within this session, we do a
      // direct copy, otherwise we fallback and form a new item below.
      "instanceId" in item &&
      item.instanceId == lazy.PlacesUtils.instanceId &&
      // If the Item doesn't have a guid, this could be a virtual tag query or
      // other item, so fallback to inserting a new bookmark with the URI.
      guid &&
      // For virtual root items, we fallback to creating a new bookmark, as
      // we want a shortcut to be created, not a full tree copy.
      !lazy.PlacesUtils.bookmarks.isVirtualRootItem(guid) &&
      !lazy.PlacesUtils.isVirtualLeftPaneItem(guid)
    ) {
      transaction = lazy.PlacesTransactions.Copy({
        guid,
        newIndex: index,
        newParentGuid: insertionParentGuid,
      });
    } else if (item.type == lazy.PlacesUtils.TYPE_X_MOZ_PLACE_SEPARATOR) {
      transaction = lazy.PlacesTransactions.NewSeparator({
        index,
        parentGuid: insertionParentGuid,
      });
    } else {
      let title =
        item.type != lazy.PlacesUtils.TYPE_PLAINTEXT ? item.title : item.uri;
      transaction = lazy.PlacesTransactions.NewBookmark({
        index,
        parentGuid: insertionParentGuid,
        title,
        url: item.uri,
      });
    }

    transactions.push(transaction);

    if (index != -1) {
      index++;
    }
  }
  return transactions;
}

function getBrowserWindow(aWindow) {
  // Prefer the caller window if it's a browser window, otherwise use
  // the top browser window.
  return aWindow &&
    aWindow.document.documentElement.getAttribute("windowtype") ==
      "navigator:browser"
    ? aWindow
    : lazy.BrowserWindowTracker.getTopWindow();
}
