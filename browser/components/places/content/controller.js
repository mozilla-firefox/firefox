/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

ChromeUtils.defineESModuleGetters(this, {
  PlacesTransactions: "resource://gre/modules/PlacesTransactions.sys.mjs",
  PlacesUIUtils: "resource:///modules/PlacesUIUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
});

/* import-globals-from /browser/base/content/utilityOverlay.js */
/* import-globals-from ./places.js */

/**
 * Represents an insertion point within a container where we can insert
 * items.
 *
 * @param {object} options an object containing the following properties:
 * @param {string} options.parentGuid
 *     The unique identifier of the parent container
 * @param {number} [options.index]
 *     The index within the container where to insert, defaults to appending
 * @param {number} [options.orientation]
 *     The orientation of the insertion. NOTE: the adjustments to the
 *     insertion point to accommodate the orientation should be done by
 *     the person who constructs the IP, not the user. The orientation
 *     is provided for informational purposes only! Defaults to DROP_ON.
 * @param {string} [options.tagName]
 *     The tag name if this IP is set to a tag, null otherwise.
 * @param {*} [options.dropNearNode]
 *     When defined index will be calculated based on this node
 */
function PlacesInsertionPoint({
  parentGuid,
  index = PlacesUtils.bookmarks.DEFAULT_INDEX,
  orientation = Ci.nsITreeView.DROP_ON,
  tagName = null,
  dropNearNode = null,
}) {
  this.guid = parentGuid;
  this._index = index;
  this.orientation = orientation;
  this.tagName = tagName;
  this.dropNearNode = dropNearNode;
}

PlacesInsertionPoint.prototype = {
  set index(val) {
    this._index = val;
  },

  async getIndex() {
    if (this.dropNearNode) {
      // If dropNearNode is set up we must calculate the index of the item near
      // which we will drop.
      let index = (
        await PlacesUtils.bookmarks.fetch(this.dropNearNode.bookmarkGuid)
      ).index;
      return this.orientation == Ci.nsITreeView.DROP_BEFORE ? index : index + 1;
    }
    return this._index;
  },

  get isTag() {
    return typeof this.tagName == "string";
  },
};

/**
 * Places Controller
 */

function PlacesController(aView) {
  this._view = aView;
  ChromeUtils.defineLazyGetter(this, "profileName", function () {
    return Services.dirsvc.get("ProfD", Ci.nsIFile).leafName;
  });

  ChromeUtils.defineESModuleGetters(this, {
    ForgetAboutSite: "resource://gre/modules/ForgetAboutSite.sys.mjs",
  });
}

PlacesController.prototype = {
  /**
   * The places view.
   */
  _view: null,

  // This is used in certain views to disable user actions on the places tree
  // views. This avoids accidental deletion/modification when the user is not
  // actually organising the trees.
  disableUserActions: false,

  QueryInterface: ChromeUtils.generateQI(["nsIClipboardOwner"]),

  // nsIClipboardOwner
  LosingOwnership: function PC_LosingOwnership() {
    this.cutNodes = [];
  },

  terminate: function PC_terminate() {
    this._releaseClipboardOwnership();
  },

  supportsCommand: function PC_supportsCommand(aCommand) {
    if (this.disableUserActions) {
      return false;
    }
    // Non-Places specific commands that we also support
    switch (aCommand) {
      case "cmd_undo":
      case "cmd_redo":
      case "cmd_cut":
      case "cmd_copy":
      case "cmd_paste":
      case "cmd_delete":
      case "cmd_selectAll":
        return true;
    }

    // All other Places Commands are prefixed with "placesCmd_" ... this
    // filters out other commands that we do _not_ support (see 329587).
    const CMD_PREFIX = "placesCmd_";
    return aCommand.substr(0, CMD_PREFIX.length) == CMD_PREFIX;
  },

  isCommandEnabled: function PC_isCommandEnabled(aCommand) {
    // Determine whether or not nodes can be inserted.
    let ip = this._view.insertionPoint;
    let canInsert = ip && (aCommand.endsWith("_paste") || !ip.isTag);

    switch (aCommand) {
      case "cmd_undo":
        return PlacesTransactions.topUndoEntry != null;
      case "cmd_redo":
        return PlacesTransactions.topRedoEntry != null;
      case "cmd_cut":
      case "placesCmd_cut":
        for (let node of this._view.selectedNodes) {
          // If selection includes history nodes or tags-as-bookmark, disallow
          // cutting.
          if (
            node.itemId == -1 ||
            (node.parent && PlacesUtils.nodeIsTagQuery(node.parent))
          ) {
            return false;
          }
        }
      // Otherwise fall through the cmd_delete check.
      case "cmd_delete":
      case "placesCmd_delete":
      case "placesCmd_deleteDataHost":
        return this._hasRemovableSelection();
      case "cmd_copy":
      case "placesCmd_copy":
      case "placesCmd_showInFolder":
        return this._view.hasSelection;
      case "cmd_paste":
      case "placesCmd_paste":
        // If the clipboard contains a Places flavor it is definitely pasteable,
        // otherwise we also allow pasting "text/plain" and "text/x-moz-url" data.
        // We don't check if the data is valid here, because the clipboard may
        // contain very large blobs that would largely slowdown commands updating.
        // Of course later paste() should ignore any invalid data.
        return (
          canInsert &&
          Services.clipboard.hasDataMatchingFlavors(
            [
              ...PlacesUIUtils.PLACES_FLAVORS,
              PlacesUtils.TYPE_X_MOZ_URL,
              PlacesUtils.TYPE_PLAINTEXT,
            ],
            Ci.nsIClipboard.kGlobalClipboard
          )
        );
      case "cmd_selectAll":
        if (this._view.selType != "single") {
          let rootNode = this._view.result.root;
          if (rootNode.containerOpen && rootNode.childCount > 0) {
            return true;
          }
        }
        return false;
      case "placesCmd_open":
      case "placesCmd_open:window":
      case "placesCmd_open:privatewindow":
      case "placesCmd_open:tab": {
        let selectedNode = this._view.selectedNode;
        return selectedNode && PlacesUtils.nodeIsURI(selectedNode);
      }
      case "placesCmd_new:folder":
        return canInsert;
      case "placesCmd_new:bookmark":
        return canInsert;
      case "placesCmd_new:separator":
        return (
          canInsert &&
          !PlacesUtils.asQuery(this._view.result.root).queryOptions
            .excludeItems &&
          this._view.result.sortingMode ==
            Ci.nsINavHistoryQueryOptions.SORT_BY_NONE
        );
      case "placesCmd_show:info": {
        let selectedNode = this._view.selectedNode;
        return (
          selectedNode &&
          !PlacesUtils.isRootItem(
            PlacesUtils.getConcreteItemGuid(selectedNode)
          ) &&
          (PlacesUtils.nodeIsTagQuery(selectedNode) ||
            PlacesUtils.nodeIsBookmark(selectedNode) ||
            (PlacesUtils.nodeIsFolderOrShortcut(selectedNode) &&
              !PlacesUtils.nodeIsQueryGeneratedFolder(selectedNode)))
        );
      }
      case "placesCmd_sortBy:name": {
        let selectedNode = this._view.selectedNode;
        return (
          selectedNode &&
          PlacesUtils.nodeIsFolderOrShortcut(selectedNode) &&
          !PlacesUIUtils.isFolderReadOnly(selectedNode) &&
          this._view.result.sortingMode ==
            Ci.nsINavHistoryQueryOptions.SORT_BY_NONE
        );
      }
      case "placesCmd_createBookmark": {
        return !this._view.selectedNodes.some(
          node => !PlacesUtils.nodeIsURI(node) || node.itemId != -1
        );
      }
      default:
        return false;
    }
  },

  doCommand: function PC_doCommand(aCommand) {
    if (aCommand != "cmd_delete" && aCommand != "placesCmd_delete") {
      // Clear out last removal fingerprint if any other commands arrives.
      // This covers sequences like: remove, undo, remove, where the removal
      // commands are not immediately adjacent.
      this._lastRemoveOperationFingerprint = null;
    }
    switch (aCommand) {
      case "cmd_undo":
        PlacesTransactions.undo().catch(console.error);
        break;
      case "cmd_redo":
        PlacesTransactions.redo().catch(console.error);
        break;
      case "cmd_cut":
      case "placesCmd_cut":
        this.cut();
        break;
      case "cmd_copy":
      case "placesCmd_copy":
        this.copy();
        break;
      case "cmd_paste":
      case "placesCmd_paste":
        this.paste().catch(console.error);
        break;
      case "cmd_delete":
      case "placesCmd_delete":
        this.remove("Remove Selection").catch(console.error);
        break;
      case "placesCmd_deleteDataHost":
        this.forgetAboutThisSite().catch(console.error);
        break;
      case "cmd_selectAll":
        this.selectAll();
        break;
      case "placesCmd_open":
        PlacesUIUtils.openNodeIn(
          this._view.selectedNode,
          "current",
          this._view
        );
        break;
      case "placesCmd_open:window":
        PlacesUIUtils.openNodeIn(this._view.selectedNode, "window", this._view);
        break;
      case "placesCmd_open:privatewindow":
        PlacesUIUtils.openNodeIn(
          this._view.selectedNode,
          "window",
          this._view,
          true
        );
        break;
      case "placesCmd_open:tab":
        PlacesUIUtils.openNodeIn(this._view.selectedNode, "tab", this._view);
        break;
      case "placesCmd_new:folder":
        this.newItem("folder").catch(console.error);
        break;
      case "placesCmd_new:bookmark":
        this.newItem("bookmark").catch(console.error);
        break;
      case "placesCmd_new:separator":
        this.newSeparator().catch(console.error);
        break;
      case "placesCmd_show:info":
        this.showBookmarkPropertiesForSelection();
        break;
      case "placesCmd_sortBy:name":
        this.sortFolderByName().catch(console.error);
        break;
      case "placesCmd_createBookmark": {
        const nodes = this._view.selectedNodes.map(node => {
          return {
            uri: Services.io.newURI(node.uri),
            title: node.title,
          };
        });
        PlacesUIUtils.showBookmarkPagesDialog(
          nodes,
          ["keyword", "location"],
          window.top
        );
        break;
      }
      case "placesCmd_showInFolder":
        this.showInFolder(this._view.selectedNode.bookmarkGuid);
        break;
    }
  },

  onEvent: function PC_onEvent() {},

  /**
   * Determine whether or not the selection can be removed, either by the
   * delete or cut operations based on whether or not any of its contents
   * are non-removable. We don't need to worry about recursion here since it
   * is a policy decision that a removable item not be placed inside a non-
   * removable item.
   *
   * @returns {boolean} true if all nodes in the selection can be removed,
   *         false otherwise.
   */
  _hasRemovableSelection() {
    var ranges = this._view.removableSelectionRanges;
    if (!ranges.length) {
      return false;
    }

    var root = this._view.result.root;

    for (var j = 0; j < ranges.length; j++) {
      var nodes = ranges[j];
      for (var i = 0; i < nodes.length; ++i) {
        // Disallow removing the view's root node
        if (nodes[i] == root) {
          return false;
        }

        if (!PlacesUIUtils.canUserRemove(nodes[i])) {
          return false;
        }
      }
    }

    return true;
  },

  /**
   * This helper can be used to avoid handling repeated remove operations.
   * Clear this._lastRemoveOperationFingerprint if another operation happens.
   *
   * @returns {boolean} whether the removal is the same as the last one.
   */
  _isRepeatedRemoveOperation() {
    let lastRemoveOperationFingerprint = this._lastRemoveOperationFingerprint;
    // .bookmarkGuid and .pageGuid may either be null or an empty string. While
    // that should probably change, it's safer to use || here.
    this._lastRemoveOperationFingerprint = PlacesUtils.sha256(
      this._view.selectedNodes
        .map(n => n.bookmarkGuid || (n.pageGuid || n.uri) + n.time)
        .join()
    );
    return (
      lastRemoveOperationFingerprint == this._lastRemoveOperationFingerprint
    );
  },

  /**
   * Gathers information about the selected nodes according to the following
   * rules:
   *    "link"              node is a URI
   *    "bookmark"          node is a bookmark
   *    "tagChild"          node is a child of a tag
   *    "folder"            node is a folder
   *    "query"             node is a query
   *    "separator"         node is a separator line
   *    "host"              node is a host
   *
   * @returns {Array} an array of objects corresponding the selected nodes. Each
   *         object has each of the properties above set if its corresponding
   *         node matches the rule. In addition, the annotations names for each
   *         node are set on its corresponding object as properties.
   * Notes:
   *   1) This can be slow, so don't call it anywhere performance critical!
   */
  _buildSelectionMetadata() {
    return this._view.selectedNodes.map(n => this._selectionMetadataForNode(n));
  },

  _selectionMetadataForNode(node) {
    let nodeData = {};
    // We don't use the nodeIs* methods here to avoid going through the type
    // property way too often
    switch (node.type) {
      case Ci.nsINavHistoryResultNode.RESULT_TYPE_QUERY:
        nodeData.query = true;
        if (node.parent) {
          switch (PlacesUtils.asQuery(node.parent).queryOptions.resultType) {
            case Ci.nsINavHistoryQueryOptions.RESULTS_AS_SITE_QUERY:
              nodeData.query_host = true;
              break;
            case Ci.nsINavHistoryQueryOptions.RESULTS_AS_DATE_SITE_QUERY:
            case Ci.nsINavHistoryQueryOptions.RESULTS_AS_DATE_QUERY:
              nodeData.query_day = true;
              break;
            case Ci.nsINavHistoryQueryOptions.RESULTS_AS_TAGS_ROOT:
              nodeData.query_tag = true;
          }
        }
        break;
      case Ci.nsINavHistoryResultNode.RESULT_TYPE_FOLDER:
      case Ci.nsINavHistoryResultNode.RESULT_TYPE_FOLDER_SHORTCUT:
        nodeData.folder = true;
        break;
      case Ci.nsINavHistoryResultNode.RESULT_TYPE_SEPARATOR:
        nodeData.separator = true;
        break;
      case Ci.nsINavHistoryResultNode.RESULT_TYPE_URI:
        nodeData.link = true;
        if (PlacesUtils.nodeIsBookmark(node)) {
          nodeData.link_bookmark = true;
          var parentNode = node.parent;
          if (parentNode && PlacesUtils.nodeIsTagQuery(parentNode)) {
            nodeData.link_bookmark_tag = true;
          }
        }
        break;
    }
    return nodeData;
  },

  /**
   * Determines if a context-menu item should be shown
   *
   * @param {object} aMenuItem
   *        the context menu item
   * @param {object} aMetaData
   *        meta data about the selection
   * @returns {boolean} true if the conditions (see buildContextMenu) are satisfied
   *          and the item can be displayed, false otherwise.
   */
  _shouldShowMenuItem(aMenuItem, aMetaData) {
    if (PlacesUIUtils.shouldHideOpenMenuItem(aMenuItem)) {
      return false;
    }

    let selectiontype =
      aMenuItem.getAttribute("selection-type") || "single|multiple";

    var selectionTypes = selectiontype.split("|");
    if (selectionTypes.includes("any")) {
      return true;
    }
    var count = aMetaData.length;
    if (count > 1 && !selectionTypes.includes("multiple")) {
      return false;
    }
    if (count == 1 && !selectionTypes.includes("single")) {
      return false;
    }
    // If there is no selection and selectionType doesn't include `none`
    // hide the item, otherwise try to use the root node to extract valid
    // metadata to compare against.
    if (count == 0) {
      if (!selectionTypes.includes("none")) {
        return false;
      }
      aMetaData = [this._selectionMetadataForNode(this._view.result.root)];
    }

    let attr = aMenuItem.getAttribute("hide-if-node-type");
    if (attr) {
      let rules = attr.split("|");
      if (aMetaData.some(d => rules.some(r => r in d))) {
        return false;
      }
    }

    attr = aMenuItem.getAttribute("hide-if-node-type-is-only");
    if (attr) {
      let rules = attr.split("|");
      if (rules.some(r => aMetaData.every(d => r in d))) {
        return false;
      }
    }

    attr = aMenuItem.getAttribute("node-type");
    if (!attr) {
      return true;
    }

    let anyMatched = false;
    let rules = attr.split("|");
    for (let metaData of aMetaData) {
      if (rules.some(r => r in metaData)) {
        anyMatched = true;
      } else {
        return false;
      }
    }
    return anyMatched;
  },

  /**
   * Uses meta-data rules set as attributes on the menuitems, representing the
   * current selection in the view (see `_buildSelectionMetadata`) and sets the
   * visibility state for each menuitem according to the following rules:
   *  1) The visibility state is unchanged if none of the attributes are set.
   *  2) Attributes should not be set on menuseparators.
   *  3) The boolean `ignore-item` attribute may be set when this code should
   *     not handle that menuitem.
   *  4) The `selection-type` attribute may be set to:
   *      - `single` if it should be visible only when there is a single node
   *         selected
   *      - `multiple` if it should be visible only when multiple nodes are
   *         selected
   *      - `none` if it should be visible when there are no selected nodes
   *      - `any` if it should be visible for any kind of selection
   *      - a `|` separated combination of the above.
   *  5) The `node-type` attribute may be set to values representing the
   *     type of the node triggering the context menu. The menuitem will be
   *     visible when one of the rules (separated by `|`) matches.
   *     In case of multiple selection, the menuitem is visible only if all of
   *     the selected nodes match one of the rule.
   *  6) The `hide-if-node-type` accepts the same rules as `node-type`, but
   *     hides the menuitem if the nodes match at least one of the rules.
   *     It takes priority over `nodetype`.
   *  7) The `hide-if-node-type-is-only` accepts the same rules as `node-type`, but
   *     hides the menuitem if any of the rules match all of the nodes.
   *  8) The boolean `hide-if-no-insertion-point` attribute may be set to hide a
   *     menuitem when there's no insertion point. An insertion point represents
   *     a point in the view where a new item can be inserted.
   *  9) The boolean `hide-if-private-browsing` attribute may be set to hide a
   *     menuitem in private browsing mode.
   * 10) The boolean `hide-if-disabled-private-browsing` attribute may be set to
   *     hide a menuitem if private browsing is not enabled.
   * 11) The boolean `hide-if-usercontext-disabled` attribute may be set to
   *     hide a menuitem if containers are disabled.
   * 12) The boolean `hide-if-single-click-opens` attribute may be set to hide a
   *     menuitem in views opening entries with a single click.
   *
   * @param {object} aPopup
   *        The menupopup to build children into.
   * @returns {boolean} true if at least one item is visible, false otherwise.
   */
  buildContextMenu(aPopup) {
    var metadata = this._buildSelectionMetadata();
    var ip = this._view.insertionPoint;
    var noIp = !ip || ip.isTag;

    var separator = null;
    var visibleItemsBeforeSep = false;
    var usableItemCount = 0;
    for (var i = 0; i < aPopup.children.length; ++i) {
      var item = aPopup.children[i];
      if (item.getAttribute("ignore-item") == "true") {
        continue;
      }
      if (item.localName != "menuseparator") {
        // We allow pasting into tag containers, so special case that.
        let hideIfNoIP =
          item.getAttribute("hide-if-no-insertion-point") == "true" &&
          noIp &&
          !(ip && ip.isTag && item.id == "placesContext_paste");
        // Hide `Open` if the primary action on click is opening.
        let hideIfSingleClickOpens =
          item.getAttribute("hide-if-single-click-opens") == "true" &&
          !PlacesUIUtils.loadBookmarksInBackground &&
          !PlacesUIUtils.loadBookmarksInTabs &&
          this._view.singleClickOpens;
        let hideIfNotSearch =
          item.getAttribute("hide-if-not-search") == "true" &&
          (!this._view.selectedNode ||
            !this._view.selectedNode.parent ||
            !PlacesUtils.nodeIsQuery(this._view.selectedNode.parent));

        let shouldHideItem =
          hideIfNoIP ||
          hideIfSingleClickOpens ||
          hideIfNotSearch ||
          !this._shouldShowMenuItem(item, metadata);
        item.hidden = shouldHideItem;
        item.disabled =
          shouldHideItem || item.getAttribute("start-disabled") == "true";

        if (!item.hidden) {
          visibleItemsBeforeSep = true;
          usableItemCount++;

          // Show the separator above the menu-item if any
          if (separator) {
            separator.hidden = false;
            separator = null;
          }
        }
      } else {
        // menuseparator
        // Initially hide it. It will be unhidden if there will be at least one
        // visible menu-item above and below it.
        item.hidden = true;

        // We won't show the separator at all if no items are visible above it
        if (visibleItemsBeforeSep) {
          separator = item;
        }

        // New separator, count again:
        visibleItemsBeforeSep = false;
      }

      if (item.id === "placesContext_deleteBookmark") {
        document.l10n.setAttributes(item, "places-delete-bookmark", {
          count: metadata.length,
        });
      }
      if (item.id === "placesContext_deleteFolder") {
        document.l10n.setAttributes(item, "places-delete-folder", {
          count: metadata.length,
        });
      }
    }

    // Set Open Folder/Links In Tabs or Open Bookmark item's enabled state if they're visible
    if (usableItemCount > 0) {
      let openContainerInTabsItem = document.getElementById(
        "placesContext_openContainer:tabs"
      );
      let openBookmarksItem = document.getElementById(
        "placesContext_openBookmarkContainer:tabs"
      );
      for (let menuItem of [openContainerInTabsItem, openBookmarksItem]) {
        if (!menuItem.hidden) {
          var containerToUse =
            this._view.selectedNode || this._view.result.root;
          if (PlacesUtils.nodeIsContainer(containerToUse)) {
            if (!PlacesUtils.hasChildURIs(containerToUse)) {
              menuItem.disabled = true;
              // Ensure that we don't display the menu if nothing is enabled:
              usableItemCount--;
            }
          }
        }
      }
    }

    const deleteHistoryItem = document.getElementById(
      "placesContext_delete_history"
    );
    document.l10n.setAttributes(deleteHistoryItem, "places-delete-page", {
      count: metadata.length,
    });

    const createBookmarkItem = document.getElementById(
      "placesContext_createBookmark"
    );
    document.l10n.setAttributes(createBookmarkItem, "places-create-bookmark", {
      count: metadata.length,
    });

    return usableItemCount > 0;
  },

  /**
   * Select all links in the current view.
   */
  selectAll: function PC_selectAll() {
    this._view.selectAll();
  },

  /**
   * Opens the bookmark properties for the selected URI Node.
   */
  showBookmarkPropertiesForSelection() {
    let node = this._view.selectedNode;
    if (!node) {
      return;
    }

    PlacesUIUtils.showBookmarkDialog(
      { action: "edit", node, hiddenRows: ["folderPicker"] },
      window.top
    );
  },

  /**
   * Opens the links in the selected folder, or the selected links in new tabs.
   *
   * @param {object} aEvent
   *   The associated event.
   */
  openSelectionInTabs: function PC_openLinksInTabs(aEvent) {
    var node = this._view.selectedNode;
    var nodes = this._view.selectedNodes;
    // In the case of no selection, open the root node:
    if (!node && !nodes.length) {
      node = this._view.result.root;
    }
    PlacesUIUtils.openMultipleLinksInTabs(
      node ? node : nodes,
      aEvent,
      this._view
    );
  },

  /**
   * Shows the Add Bookmark UI for the current insertion point.
   *
   * @param {string} aType
   *        the type of the new item (bookmark/folder)
   */
  async newItem(aType) {
    let ip = this._view.insertionPoint;
    if (!ip) {
      throw Components.Exception("", Cr.NS_ERROR_NOT_AVAILABLE);
    }

    let bookmarkGuid = await PlacesUIUtils.showBookmarkDialog(
      {
        action: "add",
        type: aType,
        defaultInsertionPoint: ip,
        hiddenRows: ["folderPicker"],
      },
      window.top
    );
    if (bookmarkGuid) {
      this._view.selectItems([bookmarkGuid], false);
    }
  },

  /**
   * Create a new Bookmark separator somewhere.
   */
  async newSeparator() {
    var ip = this._view.insertionPoint;
    if (!ip) {
      throw Components.Exception("", Cr.NS_ERROR_NOT_AVAILABLE);
    }

    let index = await ip.getIndex();
    let txn = PlacesTransactions.NewSeparator({ parentGuid: ip.guid, index });
    let guid = await txn.transact();
    // Select the new item.
    this._view.selectItems([guid], false);
  },

  /**
   * Sort the selected folder by name
   */
  async sortFolderByName() {
    let guid = PlacesUtils.getConcreteItemGuid(this._view.selectedNode);
    await PlacesTransactions.SortByName(guid).transact();
  },

  /**
   * Walk the list of folders we're removing in this delete operation, and
   * see if the selected node specified is already implicitly being removed
   * because it is a child of that folder.
   *
   * @param {object} node
   *        Node to check for containment.
   * @param {Array} pastFolders
   *        List of folders the calling function has already traversed
   * @returns {boolean} true if the node should be skipped, false otherwise.
   */
  _shouldSkipNode: function PC_shouldSkipNode(node, pastFolders) {
    /**
     * Determines if a node is contained by another node within a resultset.
     *
     * @param {object} parent
     *        The parent container to check for containment in
     * @returns {boolean} true if node is a member of parent's children, false otherwise.
     */
    function isNodeContainedBy(parent) {
      var cursor = node.parent;
      while (cursor) {
        if (cursor == parent) {
          return true;
        }
        cursor = cursor.parent;
      }
      return false;
    }

    for (var j = 0; j < pastFolders.length; ++j) {
      if (isNodeContainedBy(pastFolders[j])) {
        return true;
      }
    }
    return false;
  },

  /**
   * Creates a set of transactions for the removal of a range of items.
   * A range is an array of adjacent nodes in a view.
   *
   * @param {Array} range
   *          An array of nodes to remove. Should all be adjacent.
   * @param {Array} transactions
   *          An array of transactions (returned)
   * @param  {Array} [removedFolders]
   *          An array of folder nodes that have already been removed.
   * @returns {number} The total number of items affected.
   */
  async _removeRange(range, transactions, removedFolders) {
    if (!(transactions instanceof Array)) {
      throw new Error("Must pass a transactions array");
    }
    if (!removedFolders) {
      removedFolders = [];
    }

    let bmGuidsToRemove = [];
    let totalItems = 0;

    for (var i = 0; i < range.length; ++i) {
      var node = range[i];
      if (this._shouldSkipNode(node, removedFolders)) {
        continue;
      }

      totalItems++;

      if (PlacesUtils.nodeIsTagQuery(node.parent)) {
        // This is a uri node inside a tag container.  It needs a special
        // untag transaction.
        let tag = node.parent.title || "";
        if (!tag) {
          // The parent may be the root node, that doesn't have a title.
          tag = node.parent.query.tags[0];
        }
        transactions.push(PlacesTransactions.Untag({ urls: [node.uri], tag }));
      } else if (
        PlacesUtils.nodeIsTagQuery(node) &&
        node.parent &&
        PlacesUtils.nodeIsQuery(node.parent) &&
        PlacesUtils.asQuery(node.parent).queryOptions.resultType ==
          Ci.nsINavHistoryQueryOptions.RESULTS_AS_TAGS_ROOT
      ) {
        // This is a tag container.
        // Untag all URIs tagged with this tag only if the tag container is
        // child of the "Tags" query in the library, in all other places we
        // must only remove the query node.
        let tag = node.title;
        let urls = new Set();
        await PlacesUtils.bookmarks.fetch({ tags: [tag] }, b =>
          urls.add(b.url)
        );
        transactions.push(
          PlacesTransactions.Untag({ tag, urls: Array.from(urls) })
        );
      } else if (
        PlacesUtils.nodeIsURI(node) &&
        PlacesUtils.nodeIsQuery(node.parent) &&
        PlacesUtils.asQuery(node.parent).queryOptions.queryType ==
          Ci.nsINavHistoryQueryOptions.QUERY_TYPE_HISTORY
      ) {
        // This is a uri node inside an history query.
        await PlacesUtils.history.remove(node.uri).catch(console.error);
        // History deletes are not undoable, so we don't have a transaction.
      } else if (
        node.itemId == -1 &&
        PlacesUtils.nodeIsQuery(node) &&
        PlacesUtils.asQuery(node).queryOptions.queryType ==
          Ci.nsINavHistoryQueryOptions.QUERY_TYPE_HISTORY
      ) {
        // This is a dynamically generated history query, like queries
        // grouped by site, time or both.  Dynamically generated queries don't
        // have an itemId even if they are descendants of a bookmark.
        await this._removeHistoryContainer(node).catch(console.error);
        // History deletes are not undoable, so we don't have a transaction.
      } else {
        // This is a common bookmark item.
        if (PlacesUtils.nodeIsFolderOrShortcut(node)) {
          // If this is a folder we add it to our array of folders, used
          // to skip nodes that are children of an already removed folder.
          removedFolders.push(node);
        }
        bmGuidsToRemove.push(node.bookmarkGuid);
      }
    }
    if (bmGuidsToRemove.length) {
      transactions.push(PlacesTransactions.Remove({ guids: bmGuidsToRemove }));
    }
    return totalItems;
  },

  async _removeRowsFromBookmarks() {
    let ranges = this._view.removableSelectionRanges;
    let transactions = [];
    let removedFolders = [];
    let totalItems = 0;

    for (let range of ranges) {
      totalItems += await this._removeRange(
        range,
        transactions,
        removedFolders
      );
    }

    if (transactions.length) {
      await PlacesUIUtils.batchUpdatesForNode(
        this._view.result,
        totalItems,
        async () => {
          await PlacesTransactions.batch(
            transactions,
            "PlacesController::removeRowsFromBookmarks"
          );
        }
      );
    }
  },

  /**
   * Removes the set of selected ranges from history, asynchronously. History
   * deletes are not undoable.
   */
  async _removeRowsFromHistory() {
    let nodes = this._view.selectedNodes;
    let URIs = new Set();
    for (let i = 0; i < nodes.length; ++i) {
      let node = nodes[i];
      if (PlacesUtils.nodeIsURI(node)) {
        URIs.add(node.uri);
      } else if (
        PlacesUtils.nodeIsQuery(node) &&
        PlacesUtils.asQuery(node).queryOptions.queryType ==
          Ci.nsINavHistoryQueryOptions.QUERY_TYPE_HISTORY
      ) {
        await this._removeHistoryContainer(node).catch(console.error);
      }
    }

    if (URIs.size) {
      await PlacesUIUtils.batchUpdatesForNode(
        this._view.result,
        URIs.size,
        async () => {
          await PlacesUtils.history.remove([...URIs]);
        }
      );
    }
  },

  /**
   * Removes history visits for an history container node. History deletes are
   * not undoable.
   *
   * @param {object} aContainerNode
   *        The container node to remove.
   */
  async _removeHistoryContainer(aContainerNode) {
    if (PlacesUtils.nodeIsHost(aContainerNode)) {
      // This is a site container.
      // Check if it's the container for local files (don't be fooled by the
      // bogus string name, this is "(local files)").
      let host =
        "." +
        (aContainerNode.title == PlacesUtils.getString("localhost")
          ? ""
          : aContainerNode.title);
      // Will update faster if all children hidden before removing
      aContainerNode.containerOpen = false;
      await PlacesUtils.history.removeByFilter({ host });
    } else if (PlacesUtils.nodeIsDay(aContainerNode)) {
      // This is a day container.
      let query = aContainerNode.query;
      let beginTime = query.beginTime;
      let endTime = query.endTime;
      if (!query || !beginTime || !endTime) {
        throw new Error("A valid date container query should exist!");
      }
      // Will update faster if all children hidden before removing
      aContainerNode.containerOpen = false;
      // We want to exclude beginTime from the removal because
      // removePagesByTimeframe includes both extremes, while date containers
      // exclude the lower extreme.  So, if we would not exclude it, we would
      // end up removing more history than requested.
      await PlacesUtils.history.removeByFilter({
        beginDate: PlacesUtils.toDate(beginTime + 1000),
        endDate: PlacesUtils.toDate(endTime),
      });
    }
  },

  /**
   * Removes the selection
   */
  async remove() {
    if (!this._hasRemovableSelection()) {
      return;
    }

    // Sometimes we get repeated remove operation requests, because the user is
    // holding down the DEL key. Since removal operations are asynchronous
    // that would cause duplicated remove transactions that perform badly,
    // increase memory usage (duplicate data), and cause failures (trying to
    // act on already removed nodes).
    if (this._isRepeatedRemoveOperation()) {
      return;
    }

    var root = this._view.result.root;

    if (PlacesUtils.nodeIsFolderOrShortcut(root)) {
      await this._removeRowsFromBookmarks();
    } else if (PlacesUtils.nodeIsQuery(root)) {
      var queryType = PlacesUtils.asQuery(root).queryOptions.queryType;
      if (queryType == Ci.nsINavHistoryQueryOptions.QUERY_TYPE_BOOKMARKS) {
        await this._removeRowsFromBookmarks();
      } else if (queryType == Ci.nsINavHistoryQueryOptions.QUERY_TYPE_HISTORY) {
        await this._removeRowsFromHistory();
      } else {
        throw new Error("Unknown query type");
      }
    } else {
      throw new Error("unexpected root");
    }
  },

  /**
   * Fills a DataTransfer object with the content of the selection that can be
   * dropped elsewhere.
   *
   * @param {object} aEvent
   *        The dragstart event.
   */
  setDataTransfer: function PC_setDataTransfer(aEvent) {
    let dt = aEvent.dataTransfer;

    let result = this._view.result;
    let didSuppressNotifications = result.suppressNotifications;
    if (!didSuppressNotifications) {
      result.suppressNotifications = true;
    }

    function addData(type, index) {
      let wrapNode = PlacesUtils.wrapNode(node, type);
      dt.mozSetDataAt(type, wrapNode, index);
    }

    function addURIData(index) {
      addData(PlacesUtils.TYPE_X_MOZ_URL, index);
      addData(PlacesUtils.TYPE_PLAINTEXT, index);
      addData(PlacesUtils.TYPE_HTML, index);
    }

    try {
      let nodes = this._view.draggableSelection;
      for (let i = 0; i < nodes.length; ++i) {
        var node = nodes[i];

        // This order is _important_! It controls how this and other
        // applications select data to be inserted based on type.
        addData(PlacesUtils.TYPE_X_MOZ_PLACE, i);
        if (node.uri) {
          addURIData(i);
        }
      }
    } finally {
      if (!didSuppressNotifications) {
        result.suppressNotifications = false;
      }
    }
  },

  get clipboardAction() {
    let action = {};
    let actionOwner;
    try {
      let xferable = Cc["@mozilla.org/widget/transferable;1"].createInstance(
        Ci.nsITransferable
      );
      xferable.init(null);
      xferable.addDataFlavor(PlacesUtils.TYPE_X_MOZ_PLACE_ACTION);
      Services.clipboard.getData(xferable, Ci.nsIClipboard.kGlobalClipboard);
      xferable.getTransferData(PlacesUtils.TYPE_X_MOZ_PLACE_ACTION, action);
      [action, actionOwner] = action.value
        .QueryInterface(Ci.nsISupportsString)
        .data.split(",");
    } catch (ex) {
      // Paste from external sources don't have any associated action, just
      // fallback to a copy action.
      return "copy";
    }
    // For cuts also check who inited the action, since cuts across different
    // instances should instead be handled as copies (The sources are not
    // available for this instance).
    if (action == "cut" && actionOwner != this.profileName) {
      action = "copy";
    }

    return action;
  },

  _releaseClipboardOwnership: function PC__releaseClipboardOwnership() {
    if (this.cutNodes.length) {
      // This clears the logical clipboard, doesn't remove data.
      Services.clipboard.emptyClipboard(Ci.nsIClipboard.kGlobalClipboard);
    }
  },

  _clearClipboard: function PC__clearClipboard() {
    let xferable = Cc["@mozilla.org/widget/transferable;1"].createInstance(
      Ci.nsITransferable
    );
    xferable.init(null);
    // Empty transferables may cause crashes, so just add an unknown type.
    const TYPE = "text/x-moz-place-empty";
    xferable.addDataFlavor(TYPE);
    xferable.setTransferData(TYPE, PlacesUtils.toISupportsString(""));
    Services.clipboard.setData(
      xferable,
      null,
      Ci.nsIClipboard.kGlobalClipboard
    );
  },

  _populateClipboard: function PC__populateClipboard(aNodes, aAction) {
    // This order is _important_! It controls how this and other applications
    // select data to be inserted based on type.
    let contents = [
      { type: PlacesUtils.TYPE_X_MOZ_PLACE, entries: [] },
      { type: PlacesUtils.TYPE_X_MOZ_URL, entries: [] },
      { type: PlacesUtils.TYPE_HTML, entries: [] },
      { type: PlacesUtils.TYPE_PLAINTEXT, entries: [] },
    ];

    // Avoid handling descendants of a copied node, the transactions take care
    // of them automatically.
    let copiedFolders = [];
    aNodes.forEach(function (node) {
      if (this._shouldSkipNode(node, copiedFolders)) {
        return;
      }
      if (PlacesUtils.nodeIsFolderOrShortcut(node)) {
        copiedFolders.push(node);
      }

      contents.forEach(function (content) {
        content.entries.push(PlacesUtils.wrapNode(node, content.type));
      });
    }, this);

    function addData(type, data) {
      xferable.addDataFlavor(type);
      xferable.setTransferData(type, PlacesUtils.toISupportsString(data));
    }

    let xferable = Cc["@mozilla.org/widget/transferable;1"].createInstance(
      Ci.nsITransferable
    );
    xferable.init(null);
    let hasData = false;
    // This order matters here!  It controls how this and other applications
    // select data to be inserted based on type.
    contents.forEach(function (content) {
      if (content.entries.length) {
        hasData = true;
        let glue =
          content.type == PlacesUtils.TYPE_X_MOZ_PLACE ? "," : PlacesUtils.endl;
        addData(content.type, content.entries.join(glue));
      }
    });

    // Track the exected action in the xferable.  This must be the last flavor
    // since it's the least preferred one.
    // Enqueue a unique instance identifier to distinguish operations across
    // concurrent instances of the application.
    addData(
      PlacesUtils.TYPE_X_MOZ_PLACE_ACTION,
      aAction + "," + this.profileName
    );

    if (hasData) {
      Services.clipboard.setData(
        xferable,
        aAction == "cut" ? this : null,
        Ci.nsIClipboard.kGlobalClipboard
      );
    }
  },

  _cutNodes: [],
  get cutNodes() {
    return this._cutNodes;
  },
  set cutNodes(aNodes) {
    let self = this;
    function updateCutNodes(aValue) {
      self._cutNodes.forEach(function (aNode) {
        self._view.toggleCutNode(aNode, aValue);
      });
    }

    updateCutNodes(false);
    this._cutNodes = aNodes;
    updateCutNodes(true);
  },

  /**
   * Copy Bookmarks and Folders to the clipboard
   */
  copy: function PC_copy() {
    let result = this._view.result;
    let didSuppressNotifications = result.suppressNotifications;
    if (!didSuppressNotifications) {
      result.suppressNotifications = true;
    }
    try {
      this._populateClipboard(this._view.selectedNodes, "copy");
    } finally {
      if (!didSuppressNotifications) {
        result.suppressNotifications = false;
      }
    }
  },

  /**
   * Cut Bookmarks and Folders to the clipboard
   */
  cut: function PC_cut() {
    let result = this._view.result;
    let didSuppressNotifications = result.suppressNotifications;
    if (!didSuppressNotifications) {
      result.suppressNotifications = true;
    }
    try {
      this._populateClipboard(this._view.selectedNodes, "cut");
      this.cutNodes = this._view.selectedNodes;
    } finally {
      if (!didSuppressNotifications) {
        result.suppressNotifications = false;
      }
    }
  },

  /**
   * Paste Bookmarks and Folders from the clipboard
   */
  async paste() {
    // No reason to proceed if there isn't a valid insertion point.
    let ip = this._view.insertionPoint;
    if (!ip) {
      throw Components.Exception("", Cr.NS_ERROR_NOT_AVAILABLE);
    }

    let action = this.clipboardAction;

    let xferable = Cc["@mozilla.org/widget/transferable;1"].createInstance(
      Ci.nsITransferable
    );
    xferable.init(null);
    // This order matters here!  It controls the preferred flavors for this
    // paste operation.
    [
      PlacesUtils.TYPE_X_MOZ_PLACE,
      PlacesUtils.TYPE_X_MOZ_URL,
      PlacesUtils.TYPE_PLAINTEXT,
    ].forEach(type => xferable.addDataFlavor(type));

    Services.clipboard.getData(xferable, Ci.nsIClipboard.kGlobalClipboard);

    // Now get the clipboard contents, in the best available flavor.
    let validNodes, invalidNodes;

    try {
      let data = {},
        type = {};
      xferable.getAnyTransferData(type, data);
      ({ validNodes, invalidNodes } = PlacesUtils.unwrapNodes(
        data.value.QueryInterface(Ci.nsISupportsString).data,
        type.value
      ));
    } catch (ex) {
      // No supported data exists, just bail out.
      return;
    }

    let doCopy = action == "copy";
    let itemsToSelect = await PlacesUIUtils.handleTransferItems(
      validNodes,
      ip,
      doCopy,
      this._view
    );

    // Cut/past operations are not repeatable, so clear the clipboard.
    if (action == "cut") {
      this._clearClipboard();
    }

    if (itemsToSelect.length) {
      this._view.selectItems(itemsToSelect, false);
    }

    if (invalidNodes.length) {
      let [title, body] = PlacesUIUtils.promptLocalization.formatValuesSync([
        "places-bookmarks-paste-error-title",
        "places-bookmarks-paste-error-message-header",
      ]);

      const MAX_URI_LENGTH = 100;
      const MAX_URI_COUNT = 20;

      let invalidUrlList = invalidNodes
        .slice(0, MAX_URI_COUNT)
        .map(item => {
          let encodedUri = encodeURI(item.uri);
          if (encodedUri.length > MAX_URI_LENGTH) {
            encodedUri = encodedUri.slice(0, MAX_URI_LENGTH) + "…";
          }
          return "\n  • " + encodedUri;
        })
        .join("");

      if (invalidNodes.length > MAX_URI_COUNT) {
        invalidUrlList += "\n  • …";
      }

      body = `${body}${invalidUrlList}`;
      Services.prompt.alert(window, title, body);
    }
  },

  /**
   * Checks if we can insert into a container.
   *
   * @param {object} container
   *          The container were we are want to drop
   * @returns {boolean}
   */
  disallowInsertion(container) {
    if (!container) {
      throw new Error("empty container");
    }
    // Allow dropping into Tag containers and editable folders.
    return (
      !PlacesUtils.nodeIsTagQuery(container) &&
      (!PlacesUtils.nodeIsFolderOrShortcut(container) ||
        PlacesUIUtils.isFolderReadOnly(container))
    );
  },

  /**
   * Determines if a node can be moved.
   *
   * @param {object} node
   *        A nsINavHistoryResultNode node.
   * @returns {boolean} True if the node can be moved, false otherwise.
   */
  canMoveNode(node) {
    // Only bookmark items are movable.
    if (node.itemId == -1) {
      return false;
    }

    // Once tags and bookmarked are divorced, the tag-query check should be
    // removed.
    let parentNode = node.parent;
    if (!parentNode) {
      return false;
    }

    // Once tags and bookmarked are divorced, the tag-query check should be
    // removed.
    if (PlacesUtils.nodeIsTagQuery(parentNode)) {
      return false;
    }

    return (
      (PlacesUtils.nodeIsFolderOrShortcut(parentNode) &&
        !PlacesUIUtils.isFolderReadOnly(parentNode)) ||
      PlacesUtils.nodeIsQuery(parentNode)
    );
  },
  async forgetAboutThisSite() {
    let host;
    if (PlacesUtils.nodeIsHost(this._view.selectedNode)) {
      host = this._view.selectedNode.query.domain;
    } else {
      host = Services.io.newURI(this._view.selectedNode.uri).host;
    }
    let baseDomain;
    try {
      baseDomain = Services.eTLD.getBaseDomainFromHost(host);
    } catch (e) {
      // If there is no baseDomain we fall back to host
    }
    let params = { host, hostOrBaseDomain: baseDomain ?? host };
    if (window.gDialogBox) {
      await window.gDialogBox.open(
        "chrome://browser/content/places/clearDataForSite.xhtml",
        params
      );
    } else {
      await window.openDialog(
        "chrome://browser/content/places/clearDataForSite.xhtml",
        null,
        "modal,centerscreen",
        params
      );
    }
  },

  showInFolder(aBookmarkGuid) {
    // Open containing folder in left pane/sidebar bookmark tree
    let documentUrl = document.documentURI.toLowerCase();
    if (documentUrl.endsWith("browser.xhtml")) {
      // We're in a menu or a panel.
      window.SidebarController._show("viewBookmarksSidebar").then(() => {
        let theSidebar = document.getElementById("sidebar");
        theSidebar.contentDocument
          .getElementById("bookmarks-view")
          .selectItems([aBookmarkGuid]);
      });
    } else if (documentUrl.includes("sidebar")) {
      // We're in the sidebar - clear the search box first
      let searchBox = document.getElementById("search-box");
      searchBox.value = "";
      searchBox.doCommand();

      // And go to the node
      this._view.selectItems([aBookmarkGuid], true);
    } else {
      // We're in the bookmark library/manager
      PlacesUtils.bookmarks
        .fetch(aBookmarkGuid, null, { includePath: true })
        .then(b => {
          let containers = b.path.map(obj => {
            return obj.guid;
          });
          // selectLeftPane looks for literal "AllBookmarks" as a "built-in"
          containers.splice(0, 0, "AllBookmarks");
          PlacesOrganizer.selectLeftPaneContainerByHierarchy(containers);
          this._view.selectItems([aBookmarkGuid], false);
        });
    }
  },
};

/**
 * Handles drag and drop operations for views. Note that this is view agnostic!
 * You should not use PlacesController._view within these methods, since
 * the view that the item(s) have been dropped on was not necessarily active.
 * Drop functions are passed the view that is being dropped on.
 */
var PlacesControllerDragHelper = {
  /**
   * For views using DOM nodes like toolbars, menus and panels, this is the DOM
   * element currently being dragged over. For other views not handling DOM
   * nodes, like trees, it is a Places result node instead.
   */
  currentDropTarget: null,

  /**
   * Determines if the mouse is currently being dragged over a child node of
   * this menu. This is necessary so that the menu doesn't close while the
   * mouse is dragging over one of its submenus
   *
   * @param {object} node
   *        The container node
   * @returns {boolean} true if the user is dragging over a node within the hierarchy of
   *         the container, false otherwise.
   */
  draggingOverChildNode: function PCDH_draggingOverChildNode(node) {
    let currentNode = this.currentDropTarget;
    while (currentNode) {
      if (currentNode == node) {
        return true;
      }
      currentNode = currentNode.parentNode;
    }
    return false;
  },

  /**
   * @returns {object|null} The current active drag session for the window.
   * Returns null if there is none.
   */
  getSession: function PCDH__getSession() {
    return this.dragService.getCurrentSession(window);
  },

  /**
   * Extract the most relevant flavor from a list of flavors.
   *
   * @param {DOMStringList} flavors The flavors list.
   * @returns {string} The most relevant flavor, or undefined.
   */
  getMostRelevantFlavor(flavors) {
    // The DnD API returns a DOMStringList, but tests may pass an Array.
    flavors = Array.from(flavors);
    return PlacesUIUtils.SUPPORTED_FLAVORS.find(f => flavors.includes(f));
  },

  /**
   * Determines whether or not the data currently being dragged can be dropped
   * on a places view.
   *
   * @param {object} ip
   *        The insertion point where the items should be dropped.
   * @param {object} dt
   *        The data transfer object.
   * @returns {boolean}
   */
  canDrop: function PCDH_canDrop(ip, dt) {
    let dropCount = dt.mozItemCount;

    // Check every dragged item.
    for (let i = 0; i < dropCount; i++) {
      let flavor = this.getMostRelevantFlavor(dt.mozTypesAt(i));
      if (!flavor) {
        return false;
      }

      // Urls can be dropped on any insertionpoint.
      // XXXmano: remember that this method is called for each dragover event!
      // Thus we shouldn't use unwrapNodes here at all if possible.
      // I think it would be OK to accept bogus data here (e.g. text which was
      // somehow wrapped as TAB_DROP_TYPE, this is not in our control, and
      // will just case the actual drop to be a no-op), and only rule out valid
      // expected cases, which are either unsupported flavors, or items which
      // cannot be dropped in the current insertionpoint. The last case will
      // likely force us to use unwrapNodes for the private data types of
      // places.
      if (flavor == TAB_DROP_TYPE) {
        continue;
      }

      let data = dt.mozGetDataAt(flavor, i);
      let validNodes;
      try {
        ({ validNodes } = PlacesUtils.unwrapNodes(data, flavor));
      } catch (e) {
        return false;
      }

      for (let dragged of validNodes) {
        // Only bookmarks and urls can be dropped into tag containers.
        if (
          ip.isTag &&
          dragged.type != PlacesUtils.TYPE_X_MOZ_URL &&
          (dragged.type != PlacesUtils.TYPE_X_MOZ_PLACE ||
            (dragged.uri && dragged.uri.startsWith("place:")))
        ) {
          return false;
        }

        // Disallow dropping of a folder on itself or any of its descendants.
        // This check is done to show an appropriate drop indicator, a stricter
        // check is done later by the bookmarks API.
        if (
          dragged.type == PlacesUtils.TYPE_X_MOZ_PLACE_CONTAINER ||
          (dragged.uri && dragged.uri.startsWith("place:"))
        ) {
          let dragOverPlacesNode = this.currentDropTarget;
          if (!(dragOverPlacesNode instanceof Ci.nsINavHistoryResultNode)) {
            // If it's a DOM node, it should have a _placesNode expando, or it
            // may be a static element in a places container, like the [empty]
            // menuitem.
            dragOverPlacesNode =
              dragOverPlacesNode._placesNode ??
              dragOverPlacesNode.parentNode?._placesNode;
          }

          // If we couldn't get a target Places result node then we can't check
          // whether the drag is allowed, just let it go through.
          if (dragOverPlacesNode) {
            let guid = dragged.concreteGuid ?? dragged.itemGuid;
            // Dragging over itself.
            if (PlacesUtils.getConcreteItemGuid(dragOverPlacesNode) == guid) {
              return false;
            }
            // Dragging over a descendant.
            for (let ancestor of PlacesUtils.nodeAncestors(
              dragOverPlacesNode
            )) {
              if (PlacesUtils.getConcreteItemGuid(ancestor) == guid) {
                return false;
              }
            }
          }
        }

        // Disallow the dropping of multiple bookmarks if they include
        // a javascript: bookmarklet
        if (
          !flavor.startsWith("text/x-moz-place") &&
          (validNodes.length > 1 || dropCount > 1) &&
          validNodes.some(n => n.uri?.startsWith("javascript:"))
        ) {
          return false;
        }
      }
    }
    return true;
  },

  /**
   * Handles the drop of one or more items onto a view.
   *
   * @param {object} insertionPoint The insertion point where the items should
   *                                be dropped.
   * @param {object} dt             The dataTransfer information for the drop.
   * @param {object} [view]         The view or the tree element. This allows
   *                                batching to take place.
   */
  async onDrop(insertionPoint, dt, view) {
    let doCopy = ["copy", "link"].includes(dt.dropEffect);

    let dropCount = dt.mozItemCount;

    // Following flavors may contain duplicated data.
    let duplicable = new Map();
    duplicable.set(PlacesUtils.TYPE_PLAINTEXT, new Set());
    duplicable.set(PlacesUtils.TYPE_X_MOZ_URL, new Set());

    // Collect all data from the DataTransfer before processing it, as the
    // DataTransfer is only valid during the synchronous handling of the `drop`
    // event handler callback.
    let nodes = [];
    let externalDrag = false;
    for (let i = 0; i < dropCount; ++i) {
      let flavor = this.getMostRelevantFlavor(dt.mozTypesAt(i));
      if (!flavor) {
        return;
      }

      let data = dt.mozGetDataAt(flavor, i);
      if (duplicable.has(flavor)) {
        let handled = duplicable.get(flavor);
        if (handled.has(data)) {
          continue;
        }
        handled.add(data);
      }

      // Check that the drag/drop is not internal
      if (i == 0 && !flavor.startsWith("text/x-moz-place")) {
        externalDrag = true;
      }

      if (flavor != TAB_DROP_TYPE) {
        nodes = [...nodes, ...PlacesUtils.unwrapNodes(data, flavor).validNodes];
      } else if (
        XULElement.isInstance(data) &&
        data.localName == "tab" &&
        data.ownerGlobal.isChromeWindow
      ) {
        let uri = data.linkedBrowser.currentURI;
        let spec = uri ? uri.spec : "about:blank";
        nodes.push({
          uri: spec,
          title: data.label,
          type: PlacesUtils.TYPE_X_MOZ_URL,
        });
      } else {
        throw new Error("bogus data was passed as a tab");
      }
    }

    // If a multiple urls are being dropped from the urlbar or an external source,
    // and they include javascript url, not bookmark any of them
    if (
      externalDrag &&
      (nodes.length > 1 || dropCount > 1) &&
      nodes.some(n => n.uri?.startsWith("javascript:"))
    ) {
      throw new Error("Javascript bookmarklet passed with uris");
    }

    // If a single javascript url is being dropped from the urlbar or an external source,
    // show the bookmark dialog as a speedbump protection against malicious cases.
    if (
      nodes.length == 1 &&
      externalDrag &&
      nodes[0].uri?.startsWith("javascript")
    ) {
      let uri;
      try {
        uri = Services.io.newURI(nodes[0].uri);
      } catch (ex) {
        // Invalid uri, we skip this code and the entry will be discarded later.
      }

      if (uri) {
        let bookmarkGuid = await PlacesUIUtils.showBookmarkDialog(
          {
            action: "add",
            type: "bookmark",
            defaultInsertionPoint: insertionPoint,
            hiddenRows: ["folderPicker"],
            title: nodes[0].title,
            uri,
          },
          BrowserWindowTracker.getTopWindow() // `window` may be the Library.
        );

        if (bookmarkGuid && view) {
          view.selectItems([bookmarkGuid], false);
        }

        return;
      }
    }

    await PlacesUIUtils.handleTransferItems(
      nodes,
      insertionPoint,
      doCopy,
      view
    );
  },
};

XPCOMUtils.defineLazyServiceGetter(
  PlacesControllerDragHelper,
  "dragService",
  "@mozilla.org/widget/dragservice;1",
  "nsIDragService"
);
