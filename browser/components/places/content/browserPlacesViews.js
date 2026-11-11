/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "NATIVE_MENU_MAX_RESULTS",
  "browser.places.nativeMenu.maxResults",
  100
);

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "NATIVE_MENU_MAX_FOLDER_RESULTS",
  "browser.places.nativeMenu.maxFolderResults",
  500
);

/**
 * The base view implements everything that's common to all the views.
 * It should not be instanced directly, use a derived class instead.
 */
class PlacesViewBase {
  /**
   * @param {string} placesUrl
   *   The query string associated with the view.
   * @param {DOMElement} rootElt
   *   The root element for the view.
   * @param {DOMElement} viewElt
   *   The view element.
   */
  constructor(placesUrl, rootElt, viewElt) {
    this._rootElt = rootElt;
    this._viewElt = viewElt;
    // Do initialization in subclass now that `this` exists.
    this._init?.();
    this._controller = new PlacesController(this);
    this.place = placesUrl;
    this._viewElt.controllers.appendController(this._controller);
  }

  // The xul element that holds the entire view.
  _viewElt = null;

  get associatedElement() {
    return this._viewElt;
  }

  get controllers() {
    return this._viewElt.controllers;
  }

  // The xul element that represents the root container.
  _rootElt = null;

  get rootElement() {
    return this._rootElt;
  }

  // Set to true for views that are represented by native widgets (i.e.
  // the native mac menu).
  _nativeView = false;

  static interfaces = [
    Ci.nsINavHistoryResultObserver,
    Ci.nsISupportsWeakReference,
  ];

  QueryInterface = ChromeUtils.generateQI(PlacesViewBase.interfaces);

  _place = "";
  get place() {
    return this._place;
  }
  set place(val) {
    this._place = val;

    let history = PlacesUtils.history;
    let query = {},
      options = {};
    history.queryStringToQuery(val, query, options);
    let result = history.executeQuery(query.value, options.value);
    result.addObserver(this);
  }

  _result = null;
  get result() {
    return this._result;
  }
  set result(val) {
    if (this._result == val) {
      return;
    }

    if (this._result) {
      this._result.removeObserver(this);
      this._resultNode.containerOpen = false;
    }

    if (this._rootElt.localName == "menupopup") {
      this._rootElt._built = false;
    }

    this._result = val;
    if (val) {
      this._resultNode = val.root;
      this._rootElt._placesNode = this._resultNode;
      this._domNodes = new Map();
      this._domNodes.set(this._resultNode, this._rootElt);

      // This calls _rebuild through invalidateContainer.
      this._resultNode.containerOpen = true;
    } else {
      this._resultNode = null;
      delete this._domNodes;
    }
  }

  /**
   * Gets the DOM node used for the given places node.
   *
   * @param {object} aPlacesNode
   *        a places result node.
   * @returns {object|undefined} The associated DOM node, if there is one.
   */
  _getDOMNodeForPlacesNode(aPlacesNode) {
    return this._domNodes.get(aPlacesNode);
  }

  get controller() {
    return this._controller;
  }

  get selType() {
    return "single";
  }
  selectItems() {}
  selectAll() {}

  get selectedNode() {
    if (this._contextMenuShown) {
      let anchor = this._contextMenuShown.triggerNode;
      if (!anchor) {
        return null;
      }

      if (anchor._placesNode) {
        return this._rootElt == anchor ? null : anchor._placesNode;
      }

      anchor = anchor.parentNode;
      return this._rootElt == anchor ? null : anchor._placesNode || null;
    }
    return null;
  }

  get hasSelection() {
    return this.selectedNode != null;
  }

  get selectedNodes() {
    let selectedNode = this.selectedNode;
    return selectedNode ? [selectedNode] : [];
  }

  get singleClickOpens() {
    return true;
  }

  get removableSelectionRanges() {
    // On static content the current selectedNode would be the selection's
    // parent node. We don't want to allow removing a node when the
    // selection is not explicit.
    let popupNode = PlacesUIUtils.lastContextMenuTriggerNode;
    if (
      popupNode &&
      (popupNode.localName == "menupopup" || !popupNode._placesNode)
    ) {
      return [];
    }

    return [this.selectedNodes];
  }

  get draggableSelection() {
    return [this._draggedElt];
  }

  get insertionPoint() {
    // There is no insertion point for history queries, so bail out now and
    // save a lot of work when updating commands.
    let resultNode = this._resultNode;
    if (
      PlacesUtils.nodeIsQuery(resultNode) &&
      PlacesUtils.asQuery(resultNode).queryOptions.queryType ==
        Ci.nsINavHistoryQueryOptions.QUERY_TYPE_HISTORY
    ) {
      return null;
    }

    // By default, the insertion point is at the top level, at the end.
    let index = PlacesUtils.bookmarks.DEFAULT_INDEX;
    let container = this._resultNode;
    let orientation = Ci.nsITreeView.DROP_BEFORE;
    let tagName = null;

    let selectedNode = this.selectedNode;
    if (selectedNode) {
      let popupNode = PlacesUIUtils.lastContextMenuTriggerNode;
      if (
        !popupNode._placesNode ||
        popupNode._placesNode == this._resultNode ||
        popupNode._placesNode.itemId == -1 ||
        !selectedNode.parent
      ) {
        // If a static menuitem is selected, or if the root node is selected,
        // the insertion point is inside the folder, at the end.
        container = selectedNode;
        orientation = Ci.nsITreeView.DROP_ON;
      } else {
        // In all other cases the insertion point is before that node.
        container = selectedNode.parent;
        index = container.getChildIndex(selectedNode);
        if (PlacesUtils.nodeIsTagQuery(container)) {
          tagName = PlacesUtils.asQuery(container).query.tags[0];
        }
      }
    }

    if (this.controller.disallowInsertion(container)) {
      return null;
    }

    return new PlacesInsertionPoint({
      parentGuid: PlacesUtils.getConcreteItemGuid(container),
      index,
      orientation,
      tagName,
    });
  }

  buildContextMenu(aPopup) {
    // When right-clicking on the gutter of a non-empty folder popup, don't
    // show any context menu.
    let triggerNode = aPopup.triggerNode;
    // childCount > 0 distinguishes non-empty folders (suppress menu) from
    // empty ones (show folder options).
    if (
      triggerNode?.localName == "menupopup" &&
      triggerNode._placesNode?.childCount > 0
    ) {
      return false;
    }

    this._contextMenuShown = aPopup;
    window.updateCommands("places");

    // Ensure that an existing "Show Other Bookmarks" item is removed before adding it
    // again.
    let existingOtherBookmarksItem = aPopup.querySelector(
      "#show-other-bookmarks_PersonalToolbar"
    );
    existingOtherBookmarksItem?.remove();

    let manageBookmarksMenu = aPopup.querySelector(
      "#placesContext_showAllBookmarks"
    );
    // Add the View menu for the Bookmarks Toolbar and "Show Other Bookmarks" menu item
    // if the click originated from the Bookmarks Toolbar.
    let existingSubmenu = aPopup.querySelector("#toggle_PersonalToolbar");
    existingSubmenu?.remove();
    let bookmarksToolbar = document.getElementById("PersonalToolbar");
    if (bookmarksToolbar?.contains(aPopup.triggerNode)) {
      manageBookmarksMenu.removeAttribute("hidden");

      let menu = BookmarkingUI.buildBookmarksToolbarSubmenu(bookmarksToolbar);
      aPopup.insertBefore(menu, manageBookmarksMenu);

      if (
        aPopup.triggerNode.id === "OtherBookmarks" ||
        aPopup.triggerNode.id === "PlacesChevron" ||
        aPopup.triggerNode.id === "PlacesToolbarItems" ||
        aPopup.triggerNode.parentNode.id === "PlacesToolbarItems"
      ) {
        let otherBookmarksMenuItem =
          BookmarkingUI.buildShowOtherBookmarksMenuItem();

        if (otherBookmarksMenuItem) {
          aPopup.insertBefore(otherBookmarksMenuItem, menu.nextElementSibling);
        }
      }
    } else {
      manageBookmarksMenu.setAttribute("hidden", "true");
    }

    return this.controller.buildContextMenu(aPopup);
  }

  destroyContextMenu() {
    this._contextMenuShown = null;
  }

  clearAllContents(aPopup) {
    let kid = aPopup.firstElementChild;
    while (kid) {
      let next = kid.nextElementSibling;
      if (!kid.classList.contains("panel-header")) {
        kid.remove();
      }
      kid = next;
    }
    aPopup._emptyMenuitem = aPopup._startMarker = aPopup._endMarker = null;
  }

  /**
   * Drops the _domNodes entries of an element that is being removed, and of
   * the elements in its popup, so they don't keep stale nodes alive.
   *
   * @param {Element} aElt
   *   A menu, menuitem, separator or button created for a Places node.
   */
  _forgetDOMNodes(aElt) {
    let placesNode = aElt._placesNode;
    if (placesNode) {
      let mapped = this._domNodes.get(placesNode);
      if (mapped === aElt || mapped?.parentNode === aElt) {
        this._domNodes.delete(placesNode);
      }
    }
    for (let child of aElt.menupopup?.children ?? []) {
      if (child._placesNode) {
        this._forgetDOMNodes(child);
      }
    }
  }

  _cleanPopup(aPopup) {
    // Ensure markers are here when `invalidateContainer` is called before the
    // popup is shown, which may the case for panelviews, for example.
    this._ensureMarkers(aPopup);
    // Remove Places nodes from the popup.
    let child = aPopup._startMarker;
    while (child.nextElementSibling != aPopup._endMarker) {
      let sibling = child.nextElementSibling;
      if (sibling._placesNode) {
        this._forgetDOMNodes(sibling);
        aPopup.removeChild(sibling);
      } else {
        child = child.nextElementSibling;
      }
    }
  }

  _rebuildPopup(aPopup, maxResults = 0) {
    let resultNode = aPopup._placesNode;
    if (!resultNode.containerOpen) {
      return;
    }

    this._cleanPopup(aPopup);

    let cc =
      maxResults > 0
        ? Math.min(resultNode.childCount, maxResults)
        : resultNode.childCount;
    if (cc > 0) {
      this._setEmptyPopupStatus(aPopup, false);
      let fragment = document.createDocumentFragment();
      for (let i = 0; i < cc; ++i) {
        let child = resultNode.getChild(i);
        this._insertNewItemToPopup(child, fragment);
      }
      aPopup.insertBefore(fragment, aPopup._endMarker);
    } else {
      this._setEmptyPopupStatus(aPopup, true);
    }
    aPopup._built = true;
  }

  _removeChild(aChild) {
    aChild.remove();
  }

  _setEmptyPopupStatus(aPopup, aEmpty) {
    if (!aPopup._emptyMenuitem) {
      aPopup._emptyMenuitem = document.createXULElement("menuitem");
      aPopup._emptyMenuitem.setAttribute("disabled", true);
      aPopup._emptyMenuitem.className = "bookmark-item";
      document.l10n.setAttributes(
        aPopup._emptyMenuitem,
        "places-empty-bookmarks-folder"
      );
    }

    if (aEmpty) {
      aPopup.setAttribute("emptyplacesresult", "true");
      // Don't add the menuitem if there is static content.
      if (
        !aPopup._startMarker.previousElementSibling &&
        !aPopup._endMarker.nextElementSibling
      ) {
        aPopup.insertBefore(aPopup._emptyMenuitem, aPopup._endMarker);
      }
    } else {
      aPopup.removeAttribute("emptyplacesresult");
      try {
        aPopup.removeChild(aPopup._emptyMenuitem);
      } catch (ex) {}
    }
  }

  _createDOMNodeForPlacesNode(aPlacesNode) {
    this._domNodes.delete(aPlacesNode);

    let element;
    let type = aPlacesNode.type;
    if (type == Ci.nsINavHistoryResultNode.RESULT_TYPE_SEPARATOR) {
      element = document.createXULElement("menuseparator");
    } else {
      if (type == Ci.nsINavHistoryResultNode.RESULT_TYPE_URI) {
        element = document.createXULElement("menuitem");
        element.className =
          "menuitem-iconic bookmark-item menuitem-with-favicon";
        element.setAttribute(
          "scheme",
          PlacesUIUtils.guessUrlSchemeForUI(aPlacesNode.uri)
        );
      } else if (PlacesUtils.containerTypes.includes(type)) {
        element = document.createXULElement("menu");
        element.setAttribute("container", "true");

        if (aPlacesNode.type == Ci.nsINavHistoryResultNode.RESULT_TYPE_QUERY) {
          element.setAttribute("query", "true");
          if (PlacesUtils.nodeIsTagQuery(aPlacesNode)) {
            element.setAttribute("tagContainer", "true");
          } else if (PlacesUtils.nodeIsDay(aPlacesNode)) {
            element.setAttribute("dayContainer", "true");
          } else if (PlacesUtils.nodeIsHost(aPlacesNode)) {
            element.setAttribute("hostContainer", "true");
          }
        }

        let popup = document.createXULElement("menupopup", {
          is: "places-popup",
        });
        popup._placesNode = PlacesUtils.asContainer(aPlacesNode);
        popup.setAttribute("accesskey-conflicts-bug", "2073900");

        if (!this._nativeView) {
          popup.setAttribute("placespopup", "true");
          popup.toggleAttribute("nonnative", true);
        }

        element.appendChild(popup);
        element.className = "menu-iconic bookmark-item";

        this._domNodes.set(aPlacesNode, popup);
      } else {
        throw new Error("Unexpected node");
      }

      element.setAttribute("label", PlacesUIUtils.getBestTitle(aPlacesNode));

      let icon = aPlacesNode.icon;
      if (icon) {
        element.setAttribute("image", ChromeUtils.encodeURIForSrcset(icon));
      }
    }

    element._placesNode = aPlacesNode;
    if (!this._domNodes.has(aPlacesNode)) {
      this._domNodes.set(aPlacesNode, element);
    }

    return element;
  }

  _insertNewItemToPopup(aNewChild, aInsertionNode, aBefore = null) {
    let element = this._createDOMNodeForPlacesNode(aNewChild);

    aInsertionNode.insertBefore(element, aBefore);
    return element;
  }

  toggleCutNode(aPlacesNode, aValue) {
    let elt = this._getDOMNodeForPlacesNode(aPlacesNode);
    if (!elt) {
      return;
    }

    // We may get the popup for menus, but we need the menu itself.
    if (elt.localName == "menupopup") {
      elt = elt.parentNode;
    }
    if (aValue) {
      elt.setAttribute("cutting", "true");
    } else {
      elt.removeAttribute("cutting");
    }
  }

  nodeURIChanged(aPlacesNode) {
    let elt = this._getDOMNodeForPlacesNode(aPlacesNode);

    // There's no DOM node, thus there's nothing to be done when the URI changes.
    if (!elt) {
      return;
    }

    // Here we need the <menu>.
    if (elt.localName == "menupopup") {
      elt = elt.parentNode;
    }

    elt.setAttribute(
      "scheme",
      PlacesUIUtils.guessUrlSchemeForUI(aPlacesNode.uri)
    );
  }

  nodeIconChanged(aPlacesNode) {
    let elt = this._getDOMNodeForPlacesNode(aPlacesNode);

    // There's no UI representation for the root node, or there's no DOM node,
    // thus there's nothing to be done when the icon changes.
    if (!elt || elt == this._rootElt) {
      return;
    }

    // Here we need the <menu>.
    if (elt.localName == "menupopup") {
      elt = elt.parentNode;
    }
    // We must remove and reset the attribute to force an update.
    elt.removeAttribute("image");
    elt.setAttribute("image", ChromeUtils.encodeURIForSrcset(aPlacesNode.icon));
  }

  nodeTitleChanged(aPlacesNode, aNewTitle) {
    let elt = this._getDOMNodeForPlacesNode(aPlacesNode);

    // There's no UI representation for the root node or for nodes that have no
    // DOM node, thus there's nothing to be done when the title changes.
    if (!elt || elt == this._rootElt) {
      return;
    }

    // Here we need the <menu>.
    if (elt.localName == "menupopup") {
      elt = elt.parentNode;
    }

    if (!aNewTitle && elt.localName != "toolbarbutton") {
      // Many users consider toolbars as shortcuts containers, so explicitly
      // allow empty labels on toolbarbuttons.  For any other element try to be
      // smarter, guessing a title from the uri.
      elt.setAttribute("label", PlacesUIUtils.getBestTitle(aPlacesNode));
    } else {
      elt.setAttribute("label", aNewTitle);
    }
  }

  nodeRemoved(aParentPlacesNode, aPlacesNode) {
    let parentElt = this._getDOMNodeForPlacesNode(aParentPlacesNode);
    if (!parentElt?._built) {
      return;
    }

    let elt = this._getDOMNodeForPlacesNode(aPlacesNode);
    if (!elt) {
      throw new Error(
        `No DOM node for the removed node, type: ${aPlacesNode.type}`
      );
    }

    // Here we need the <menu>.
    if (elt.localName == "menupopup") {
      elt = elt.parentNode;
    }

    this._forgetDOMNodes(elt);
    parentElt.removeChild(elt);

    // Figure out if we need to show the "<Empty>" menu-item.
    // TODO Bug 517701: This doesn't seem to handle the case of an empty
    // root.
    if (parentElt._startMarker.nextElementSibling == parentElt._endMarker) {
      this._mayAddCommandsItems(parentElt);
      this._setEmptyPopupStatus(parentElt, true);
    }
  }

  // Opt-out of history details updates, since all the views derived from this
  // are not showing them.
  skipHistoryDetailsNotifications = true;
  nodeHistoryDetailsChanged() {}
  nodeTagsChanged() {}
  nodeDateAddedChanged() {}
  nodeLastModifiedChanged() {}
  nodeKeywordChanged() {}
  sortingChanged() {}
  batching() {}

  nodeInserted(aParentPlacesNode, aPlacesNode, aIndex) {
    let parentElt = this._getDOMNodeForPlacesNode(aParentPlacesNode);
    if (!parentElt?._built) {
      return;
    }

    let index =
      Array.prototype.indexOf.call(parentElt.children, parentElt._startMarker) +
      aIndex +
      1;
    this._insertNewItemToPopup(
      aPlacesNode,
      parentElt,
      parentElt.children[index] || parentElt._endMarker
    );
    this._mayAddCommandsItems(parentElt);
    this._setEmptyPopupStatus(parentElt, false);
  }

  nodeMoved(
    aPlacesNode,
    aOldParentPlacesNode,
    aOldIndex,
    aNewParentPlacesNode,
    aNewIndex
  ) {
    // Note: the current implementation of moveItem does not actually
    // use this notification when the item in question is moved from one
    // folder to another.  Instead, it calls nodeRemoved and nodeInserted
    // for the two folders.  Thus, we can assume old-parent == new-parent.
    let elt = this._getDOMNodeForPlacesNode(aPlacesNode);
    if (!elt) {
      return;
    }

    // Here we need the <menu>.
    if (elt.localName == "menupopup") {
      elt = elt.parentNode;
    }

    // If our root node is a folder, it might be moved. There's nothing
    // we need to do in that case.
    if (elt == this._rootElt) {
      return;
    }

    let parentElt = this._getDOMNodeForPlacesNode(aNewParentPlacesNode);
    if (parentElt?._built) {
      // Move the node.
      parentElt.removeChild(elt);
      let index =
        Array.prototype.indexOf.call(
          parentElt.children,
          parentElt._startMarker
        ) +
        aNewIndex +
        1;
      parentElt.insertBefore(elt, parentElt.children[index]);
    }
  }

  containerStateChanged(aPlacesNode, aOldState, aNewState) {
    if (
      aNewState == Ci.nsINavHistoryContainerResultNode.STATE_OPENED ||
      aNewState == Ci.nsINavHistoryContainerResultNode.STATE_CLOSED
    ) {
      this.invalidateContainer(aPlacesNode);
    }
  }

  /**
   * Checks whether the popup associated with the provided element is open.
   * This method may be overridden by classes that extend this base class.
   *
   * @param  {Element} elt
   *   The element to check.
   * @returns {boolean}
   */
  _isPopupOpen(elt) {
    return !!elt.parentNode.open;
  }

  invalidateContainer(aPlacesNode) {
    let elt = this._getDOMNodeForPlacesNode(aPlacesNode);
    if (!elt) {
      return;
    }
    elt._built = false;

    // If the menupopup is open we should live-update it.
    if (this._isPopupOpen(elt)) {
      this._rebuildPopup(elt);
    }
  }

  uninit() {
    if (this._result) {
      this._result.removeObserver(this);
      this._resultNode.containerOpen = false;
      this._resultNode = null;
      this._result = null;
    }

    if (this._controller) {
      this._controller.terminate();
      // Removing the controller will fail if it is already no longer there.
      // This can happen if the view element was removed/reinserted without
      // our knowledge. There is no way to check for that having happened
      // without the possibility of an exception. :-(
      try {
        this._viewElt.controllers.removeController(this._controller);
      } catch (ex) {
      } finally {
        this._controller = null;
      }
    }

    delete this._viewElt._placesView;
  }

  get isRTL() {
    if ("_isRTL" in this) {
      return this._isRTL;
    }

    return (this._isRTL =
      document.defaultView.getComputedStyle(this._viewElt).direction == "rtl");
  }

  get ownerWindow() {
    return window;
  }

  /**
   * Adds an "Open All in Tabs" menuitem to the bottom of the popup.
   *
   * @param {object} aPopup
   *        a Places popup.
   */
  _mayAddCommandsItems(aPopup) {
    // The command items are never added to the root popup.
    if (aPopup == this._rootElt) {
      return;
    }

    let hasMultipleURIs = false;
    let numURINodes = 0;

    // Check if the popup contains at least 2 menuitems with places nodes.
    // We don't currently support opening multiple uri nodes when they are not
    // populated by the result.
    if (aPopup._placesNode.childCount > 0) {
      let currentChild = aPopup.firstElementChild;
      while (currentChild) {
        if (currentChild.localName == "menuitem" && currentChild._placesNode) {
          if (++numURINodes == 2) {
            break;
          }
        }
        currentChild = currentChild.nextElementSibling;
      }
      hasMultipleURIs = numURINodes > 1;
    }

    if (!hasMultipleURIs) {
      // We don't have to show any option.
      if (aPopup._endOptOpenAllInTabs) {
        aPopup.removeChild(aPopup._endOptOpenAllInTabs);
        aPopup._endOptOpenAllInTabs = null;

        aPopup.removeChild(aPopup._endOptSeparator);
        aPopup._endOptSeparator = null;
      }
    } else if (!aPopup._endOptOpenAllInTabs) {
      // Create a separator before options.
      aPopup._endOptSeparator = document.createXULElement("menuseparator");
      aPopup._endOptSeparator.className = "bookmarks-actions-menuseparator";
      aPopup.appendChild(aPopup._endOptSeparator);

      // Add the "Open All in Tabs" menuitem.
      aPopup._endOptOpenAllInTabs = document.createXULElement("menuitem");
      aPopup._endOptOpenAllInTabs.className = "openintabs-menuitem";
      aPopup._endOptOpenAllInTabs.setAttribute(
        "label",
        gNavigatorBundle.getString("menuOpenAllInTabs.label")
      );
      aPopup._endOptOpenAllInTabs.addEventListener("command", event => {
        PlacesUIUtils.openMultipleLinksInTabs(
          event.currentTarget.parentNode._placesNode,
          event,
          PlacesUIUtils.getViewForNode(event.currentTarget)
        );
      });
      aPopup.appendChild(aPopup._endOptOpenAllInTabs);
    }

    // Share Folder should be visible if there is at least one uri and the feature is enabled.
    if (
      numURINodes > 0 &&
      ContentSharingUtils.isEnabled &&
      !aPopup._endOptShareFolder
    ) {
      // Add the "Share Folder" menuitem.
      aPopup._endOptShareFolder = document.createXULElement("menuitem");
      aPopup._endOptShareFolder.className = "openintabs-menuitem badge-new";
      aPopup._endOptShareFolder.setAttribute(
        "data-l10n-id",
        "places-share-folder3"
      );
      aPopup._endOptShareFolder.setAttribute("data-l10n-attrs", "badge");

      aPopup._endOptShareFolder.addEventListener("command", event => {
        ContentSharingUtils.createShareableLinkFromBookmarkFolders([
          PlacesUtils.getConcreteItemGuid(
            event.currentTarget.parentNode._placesNode
          ),
        ]);
      });
      aPopup.appendChild(aPopup._endOptShareFolder);
    } else if (
      aPopup._endOptShareFolder &&
      (!ContentSharingUtils.isEnabled || !numURINodes)
    ) {
      aPopup.removeChild(aPopup._endOptShareFolder);
      aPopup._endOptShareFolder = null;
    }
  }

  _ensureMarkers(aPopup) {
    if (aPopup._startMarker) {
      return;
    }

    // Places nodes are appended between _startMarker and _endMarker, that
    // are hidden menuseparators. By default they take the whole panel...
    aPopup._startMarker = document.createXULElement("menuseparator");
    aPopup._startMarker.hidden = true;
    aPopup.insertBefore(aPopup._startMarker, aPopup.firstElementChild);
    aPopup._endMarker = document.createXULElement("menuseparator");
    aPopup._endMarker.hidden = true;
    aPopup.appendChild(aPopup._endMarker);

    // ...but there can be static content before or after the places nodes, thus
    // we move the markers to the right position, by checking for static content
    // at the beginning of the view, and for an element with "afterplacescontent"
    // attribute.
    // TODO: In the future we should just use a container element.
    let firstNonStaticNodeFound = false;
    for (let child of aPopup.children) {
      if (child.hasAttribute("afterplacescontent")) {
        aPopup.insertBefore(aPopup._endMarker, child);
        break;
      }

      // Check for the first Places node that is not a view.
      if (child._placesNode && !child._placesView && !firstNonStaticNodeFound) {
        firstNonStaticNodeFound = true;
        aPopup.insertBefore(aPopup._startMarker, child);
      }
    }
    if (!firstNonStaticNodeFound) {
      // Just put the start marker before the end marker.
      aPopup.insertBefore(aPopup._startMarker, aPopup._endMarker);
    }
  }

  _onPopupShowing(aEvent) {
    // Avoid handling popupshowing of inner views.
    let popup = aEvent.originalTarget;

    this._ensureMarkers(popup);

    if (popup._placesNode && PlacesUIUtils.getViewForNode(popup) == this) {
      if (this.#isPopupForRecursiveFolderShortcut(popup)) {
        // Show as an empty container for now. We may want to show a better
        // message in the future, but since we are likely to remove recursive
        // shortcuts in maintenance at a certain point, this should be enough.
        this._setEmptyPopupStatus(popup, true);
        popup._built = true;
        return;
      }

      if (!popup._placesNode.containerOpen) {
        popup._placesNode.containerOpen = true;
      }
      if (!popup._built) {
        this._rebuildPopup(popup);
      }

      this._mayAddCommandsItems(popup);
    }
  }

  _addEventListeners(aObject, aEventNames, aCapturing = false) {
    for (let i = 0; i < aEventNames.length; i++) {
      aObject.addEventListener(aEventNames[i], this, aCapturing);
    }
  }

  _removeEventListeners(aObject, aEventNames, aCapturing = false) {
    for (let i = 0; i < aEventNames.length; i++) {
      aObject.removeEventListener(aEventNames[i], this, aCapturing);
    }
  }

  /**
   * Walks up the parent chain to detect whether a folder shortcut resolves to
   * a folder already present in the ancestry.
   *
   * @param {DOMElement} popup
   * @returns {boolean} Whether this popup is for a recursive folder shortcut.
   */
  #isPopupForRecursiveFolderShortcut(popup) {
    if (
      !popup._placesNode ||
      !PlacesUtils.nodeIsFolderOrShortcut(popup._placesNode)
    ) {
      return false;
    }
    let guid = PlacesUtils.getConcreteItemGuid(popup._placesNode);
    for (
      let parentView = popup.parentNode?.parentNode;
      parentView?._placesNode;
      parentView = parentView.parentNode?.parentNode
    ) {
      if (PlacesUtils.getConcreteItemGuid(parentView._placesNode) == guid) {
        return true;
      }
    }
    return false;
  }
}

/**
 * Toolbar View implementation.
 */
class PlacesToolbar extends PlacesViewBase {
  /** Whether we can retry updating nodes visibility. */
  #pendingVisibilityRetry = false;
  /** Whether we are currently updating nodes visibility. */
  #updatingNodesVisibility = false;

  constructor(placesUrl, rootElt, viewElt) {
    let timerId = Glean.bookmarksToolbar.init.start();
    super(placesUrl, rootElt, viewElt);
    this._addEventListeners(this._dragRoot, this._cbEvents, false);
    this._addEventListeners(
      this._rootElt,
      ["popupshowing", "popuphidden"],
      true
    );
    this._addEventListeners(this._rootElt, ["overflow", "underflow"], true);
    this._addEventListeners(window, ["unload"], false);

    this._resizeObserver = new ResizeObserver(() => {
      this.updateNodesVisibility();
    });
    this._resizeObserver.observe(this._rootElt);

    // If personal-bookmarks has been dragged to the tabs toolbar,
    // we have to track addition and removals of tabs, to properly
    // recalculate the available space for bookmarks.
    // TODO (bug 734730): Use a performant mutation listener when available.
    if (
      this._viewElt.parentNode.parentNode ==
      document.getElementById("TabsToolbar")
    ) {
      this._addEventListeners(
        gBrowser.tabContainer,
        ["TabOpen", "TabClose"],
        false
      );
    }

    Glean.bookmarksToolbar.init.stopAndAccumulate(timerId);
  }

  // Called by PlacesViewBase so we can init properties that class
  // initialization depends on. PlacesViewBase will assign this.place which
  // calls which sets `this.result` through its places observer, which changes
  // containerOpen, which calls invalidateContainer(), which calls rebuild(),
  // which needs `_overFolder`, `_chevronPopup` and various other things to
  // exist.
  _init() {
    this._overFolder = {
      elt: null,
      openTimer: null,
      hoverTime: 350,
      closeTimer: null,
    };

    // Add some smart getters for our elements.
    let thisView = this;
    [
      ["_dropIndicator", "PlacesToolbarDropIndicator"],
      ["_chevron", "PlacesChevron"],
      ["_chevronPopup", "PlacesChevronPopup"],
    ].forEach(function (elementGlobal) {
      let [name, id] = elementGlobal;
      thisView.__defineGetter__(name, function () {
        let element = document.getElementById(id);
        if (!element) {
          return null;
        }

        delete thisView[name];
        return (thisView[name] = element);
      });
    });

    this._viewElt._placesView = this;

    this._dragRoot = BookmarkingUI.toolbar.contains(this._viewElt)
      ? BookmarkingUI.toolbar
      : this._viewElt;
  }

  _cbEvents = [
    "dragstart",
    "dragover",
    "dragleave",
    "dragend",
    "drop",
    "mousemove",
    "mouseover",
    "mouseout",
    "mousedown",
  ];

  QueryInterface = ChromeUtils.generateQI([
    "nsINamed",
    "nsITimerCallback",
    ...PlacesViewBase.interfaces,
  ]);

  uninit() {
    if (this._dragRoot) {
      this._removeEventListeners(this._dragRoot, this._cbEvents, false);
    }
    this._removeEventListeners(
      this._rootElt,
      ["popupshowing", "popuphidden"],
      true
    );
    this._removeEventListeners(this._rootElt, ["overflow", "underflow"], true);
    this._removeEventListeners(window, ["unload"], false);

    if (this._resizeObserver) {
      this._resizeObserver.disconnect();
      this._resizeObserver = null;
    }
    this._removeEventListeners(
      gBrowser.tabContainer,
      ["TabOpen", "TabClose"],
      false
    );

    if (this._chevron._placesView) {
      this._chevron._placesView.uninit();
    }

    this._chevronPopup.uninit();

    if (this._otherBookmarks?._placesView) {
      this._otherBookmarks._placesView.uninit();
    }

    super.uninit();
  }

  _openedMenuButton = null;
  _allowPopupShowing = true;

  promiseRebuilt() {
    return this._rebuilding?.promise;
  }

  get _isAlive() {
    return this._resultNode && this._rootElt;
  }

  _runBeforeFrameRender(callback) {
    return new Promise((resolve, reject) => {
      window.requestAnimationFrame(() => {
        try {
          resolve(callback());
        } catch (err) {
          reject(err);
        }
      });
    });
  }

  async _rebuild() {
    // Clear out references to existing nodes, since they will be removed
    // and re-added.
    if (this._overFolder.elt) {
      this._clearOverFolder();
    }

    this._openedMenuButton = null;
    while (this._rootElt.hasChildNodes()) {
      this._forgetDOMNodes(this._rootElt.firstChild);
      this._rootElt.firstChild.remove();
    }

    let cc = this._resultNode.childCount;
    if (cc > 0) {
      // There could be a lot of nodes, but we only want to build the ones that
      // are more likely to be shown, not all of them.
      // We also don't want to wait for reflows at every node insertion, to
      // calculate a precise number of visible items, thus we guess a size from
      // the first non-separator node (because separators have flexible size).
      let startIndex = 0;
      let limit = await this._runBeforeFrameRender(() => {
        if (!this._isAlive) {
          return cc;
        }

        // Look for the first non-separator node.
        let elt;
        while (startIndex < cc) {
          elt = this._insertNewItem(
            this._resultNode.getChild(startIndex),
            this._rootElt
          );
          ++startIndex;
          if (elt.localName != "toolbarseparator") {
            break;
          }
        }
        if (!elt) {
          return cc;
        }

        return window.promiseDocumentFlushed(() => {
          // We assume a button with just the icon will be more or less a square,
          // then compensate the measurement error by considering a larger screen
          // width. Moreover the window could be bigger than the screen.
          let size = elt.clientHeight || 1; // Sanity fallback.
          return Math.min(cc, parseInt((window.screen.width * 1.5) / size));
        });
      });

      if (!this._isAlive) {
        return;
      }

      let fragment = document.createDocumentFragment();
      for (let i = startIndex; i < limit; ++i) {
        this._insertNewItem(this._resultNode.getChild(i), fragment);
      }
      await new Promise(resolve => window.requestAnimationFrame(resolve));
      if (!this._isAlive) {
        return;
      }
      this._rootElt.appendChild(fragment);
      this.updateNodesVisibility();
    }

    if (this._chevronPopup.hasAttribute("type")) {
      // Chevron has already been initialized, but since we are forcing
      // a rebuild of the toolbar, it has to be rebuilt.
      // Otherwise, it will be initialized when the toolbar overflows.
      this._chevronPopup.place = this.place;
    }

    // Rebuild the "Other Bookmarks" folder if it already exists.
    let otherBookmarks = document.getElementById("OtherBookmarks");
    otherBookmarks?.remove();

    BookmarkingUI.maybeShowOtherBookmarksFolder().catch(console.error);
  }

  _insertNewItem(aChild, aInsertionNode, aBefore = null) {
    this._domNodes.delete(aChild);

    let type = aChild.type;
    let button;
    if (type == Ci.nsINavHistoryResultNode.RESULT_TYPE_SEPARATOR) {
      button = document.createXULElement("toolbarseparator");
    } else {
      button = document.createXULElement("toolbarbutton");
      button.className = "bookmark-item";
      button.setAttribute("label", aChild.title || "");

      if (PlacesUtils.containerTypes.includes(type)) {
        button.setAttribute("type", "menu");
        button.setAttribute("container", "true");

        if (PlacesUtils.nodeIsQuery(aChild)) {
          button.setAttribute("query", "true");
          if (PlacesUtils.nodeIsTagQuery(aChild)) {
            button.setAttribute("tagContainer", "true");
          }
        }

        let popup = document.createXULElement("menupopup", {
          is: "places-popup",
        });
        popup.setAttribute("placespopup", "true");
        popup.toggleAttribute("nonnative", true);
        popup.classList.add("toolbar-menupopup");
        popup.setAttribute("accesskey-conflicts-bug", "2073900");
        button.appendChild(popup);
        popup._placesNode = PlacesUtils.asContainer(aChild);
        popup.setAttribute("context", "placesContext");

        this._domNodes.set(aChild, popup);
      } else if (PlacesUtils.nodeIsURI(aChild)) {
        button.setAttribute(
          "scheme",
          PlacesUIUtils.guessUrlSchemeForUI(aChild.uri)
        );
      }
    }

    button._placesNode = aChild;
    let { icon } = button._placesNode;
    if (icon) {
      button.setAttribute("image", icon);
    }
    if (!this._domNodes.has(aChild)) {
      this._domNodes.set(aChild, button);
    }

    if (aBefore) {
      aInsertionNode.insertBefore(button, aBefore);
    } else {
      aInsertionNode.appendChild(button);
    }
    return button;
  }

  _updateChevronPopupNodesVisibility() {
    // Note the toolbar by default builds less nodes than the chevron popup.
    for (
      let toolbarNode = this._rootElt.firstElementChild,
        node = this._chevronPopup._startMarker.nextElementSibling;
      toolbarNode && node;
      toolbarNode = toolbarNode.nextElementSibling,
        node = node.nextElementSibling
    ) {
      node.hidden = toolbarNode.style.visibility != "hidden";
    }
  }

  _onChevronPopupShowing(aEvent) {
    // Handle popupshowing only for the chevron popup, not for nested ones.
    if (aEvent.target != this._chevronPopup) {
      return;
    }

    if (!this._chevron._placesView) {
      this._chevron._placesView = new PlacesMenu(aEvent, this.place);
    }

    this._updateChevronPopupNodesVisibility();
  }

  _onOtherBookmarksPopupShowing(aEvent) {
    if (aEvent.target != this._otherBookmarksPopup) {
      return;
    }

    if (!this._otherBookmarks._placesView) {
      this._otherBookmarks._placesView = new PlacesMenu(
        aEvent,
        "place:parent=" + PlacesUtils.bookmarks.unfiledGuid
      );
    }
  }

  handleEvent(aEvent) {
    switch (aEvent.type) {
      case "unload":
        this.uninit();
        break;
      case "overflow":
        if (!this._isOverflowStateEventRelevant(aEvent)) {
          return;
        }
        // Avoid triggering overflow in containers if possible
        aEvent.stopPropagation();
        this._onOverflow();
        break;
      case "underflow":
        if (!this._isOverflowStateEventRelevant(aEvent)) {
          return;
        }
        // Avoid triggering underflow in containers if possible
        aEvent.stopPropagation();
        this._onUnderflow();
        break;
      case "TabOpen":
      case "TabClose":
        this.updateNodesVisibility();
        break;
      case "dragstart":
        this._onDragStart(aEvent);
        break;
      case "dragover":
        this._onDragOver(aEvent);
        break;
      case "dragleave":
        this._onDragLeave(aEvent);
        break;
      case "dragend":
        this._onDragEnd(aEvent);
        break;
      case "drop":
        this._onDrop(aEvent);
        break;
      case "mouseover":
        this._onMouseOver(aEvent);
        break;
      case "mousemove":
        this._onMouseMove(aEvent);
        break;
      case "mouseout":
        this._onMouseOut(aEvent);
        break;
      case "mousedown":
        this._onMouseDown(aEvent);
        break;
      case "popupshowing":
        this._onPopupShowing(aEvent);
        break;
      case "popuphidden":
        this._onPopupHidden(aEvent);
        break;
      default:
        throw new Error("Trying to handle unexpected event.");
    }
  }

  _isOverflowStateEventRelevant(aEvent) {
    // Ignore events not aimed at ourselves, as well as purely vertical ones:
    return aEvent.target == aEvent.currentTarget && aEvent.detail > 0;
  }

  _onOverflow() {
    // Attach the popup binding to the chevron popup if it has not yet
    // been initialized.
    if (!this._chevronPopup.hasAttribute("type")) {
      this._chevronPopup.setAttribute("place", this.place);
      this._chevronPopup.setAttribute("type", "places");
    }
    this._chevron.collapsed = false;
    this.updateNodesVisibility();
  }

  _onUnderflow() {
    this.updateNodesVisibility();
    this._chevron.collapsed = true;
  }

  updateNodesVisibility() {
    // Update the chevron on a timer.  This will avoid repeated work when
    // lot of changes happen in a small timeframe.
    if (this._updateNodesVisibilityTimer) {
      this._updateNodesVisibilityTimer.cancel();
    }

    this._updateNodesVisibilityTimer = this._setTimer(100);
  }

  async _updateNodesVisibilityTimerCallback() {
    if (this.#updatingNodesVisibility || window.closed || !this._isAlive) {
      return;
    }
    this.#updatingNodesVisibility = true;

    let dwu = window.windowUtils;

    let { visibleCount, measuredCount, scrollWidth } =
      await window.promiseDocumentFlushed(() => {
        let scrollRect = dwu.getBoundsWithoutFlushing(this._rootElt);
        let count = 0;
        for (let child of this._rootElt.children) {
          let childRect = dwu.getBoundsWithoutFlushing(child);
          let overflowed = this.isRTL
            ? childRect.left < scrollRect.left
            : childRect.right > scrollRect.right;
          if (overflowed) {
            // Once a child overflows, all the next ones will.
            break;
          }
          count++;
        }
        return {
          visibleCount: count,
          measuredCount: this._rootElt.children.length,
          scrollWidth: scrollRect.width,
        };
      });

    this.#updatingNodesVisibility = false;
    if (!this._isAlive) {
      return;
    }

    if (!scrollWidth) {
      // The element may have no layout frame (display:none ancestor) yet.
      // Reschedule once to allow layout to complete. If it's still frameless
      // on the retry, give up to avoid looping indefinitely.
      if (!this.#pendingVisibilityRetry) {
        this.#pendingVisibilityRetry = true;
        window.requestAnimationFrame(() => {
          if (this._isAlive) {
            this.updateNodesVisibility();
          }
        });
      }
      return;
    }

    this.#pendingVisibilityRetry = false;
    window.requestAnimationFrame(() => {
      if (!this._isAlive) {
        return;
      }
      // If the length of the child list has changed, skip the update and
      // re-queue a fresh measurement.
      if (this._rootElt.children.length != measuredCount) {
        this.updateNodesVisibility();
        return;
      }
      this._applyChildVisibility(visibleCount);
    });
  }

  _applyChildVisibility(visibleCount) {
    let children = this._rootElt.children;
    for (let i = 0; i < children.length; i++) {
      let child = children[i];
      if (i < visibleCount) {
        let icon = child._placesNode.icon;
        if (icon) {
          child.setAttribute("image", icon);
        }
        child.style.removeProperty("visibility");
      } else {
        child.removeAttribute("image");
        child.style.visibility = "hidden";
      }
    }

    if (!this._chevron.collapsed && this._chevron.open) {
      this._updateChevronPopupNodesVisibility();
    }

    let event = new CustomEvent("BookmarksToolbarVisibilityUpdated", {
      bubbles: true,
    });
    this._viewElt.dispatchEvent(event);
  }

  nodeInserted(aParentPlacesNode, aPlacesNode, aIndex) {
    let parentElt = this._getDOMNodeForPlacesNode(aParentPlacesNode);
    // Nothing to do if the parent is a never-visible node.
    if (!parentElt) {
      return;
    }
    if (parentElt == this._rootElt) {
      // Node is on the toolbar.
      let children = this._rootElt.children;
      // Nothing to do if it's a never-visible node, but note it's possible
      // we are appending.
      if (aIndex > children.length) {
        return;
      }

      // Note that childCount is already accounting for the node being added,
      // thus we must subtract one node from it.
      if (this._resultNode.childCount - 1 > children.length) {
        if (aIndex == children.length) {
          // If we didn't build all the nodes and new node is being appended,
          // we can skip it as well.
          return;
        }
        // Keep the number of built nodes consistent.
        let lastElt = this._rootElt.lastElementChild;
        this._forgetDOMNodes(lastElt);
        this._rootElt.removeChild(lastElt);
      }

      let button = this._insertNewItem(
        aPlacesNode,
        this._rootElt,
        children[aIndex] || null
      );
      let prevSiblingOverflowed =
        aIndex > 0 &&
        aIndex <= children.length &&
        children[aIndex - 1].style.visibility == "hidden";
      if (prevSiblingOverflowed) {
        button.style.visibility = "hidden";
      } else {
        let icon = aPlacesNode.icon;
        if (icon) {
          button.setAttribute("image", ChromeUtils.encodeURIForSrcset(icon));
        }
        this.updateNodesVisibility();
      }
      return;
    }

    super.nodeInserted(aParentPlacesNode, aPlacesNode, aIndex);
  }

  nodeRemoved(aParentPlacesNode, aPlacesNode, aIndex) {
    let parentElt = this._getDOMNodeForPlacesNode(aParentPlacesNode);
    if (!parentElt) {
      return;
    }
    if (parentElt == this._rootElt) {
      // Node is on the toolbar.
      let elt = this._getDOMNodeForPlacesNode(aPlacesNode);
      // Nothing to do if it's a never-visible node.
      if (!elt) {
        return;
      }

      // Here we need the <menu>.
      if (elt.localName == "menupopup") {
        elt = elt.parentNode;
      }

      let overflowed = elt.style.visibility == "hidden";
      this._forgetDOMNodes(elt);
      this._removeChild(elt);
      if (this._resultNode.childCount > this._rootElt.children.length) {
        // A new node should be built to keep a coherent number of children.
        this._insertNewItem(
          this._resultNode.getChild(this._rootElt.children.length),
          this._rootElt
        );
      }
      if (!overflowed) {
        this.updateNodesVisibility();
      }
      return;
    }

    super.nodeRemoved(aParentPlacesNode, aPlacesNode, aIndex);
  }

  nodeMoved(
    aPlacesNode,
    aOldParentPlacesNode,
    aOldIndex,
    aNewParentPlacesNode,
    aNewIndex
  ) {
    let parentElt = this._getDOMNodeForPlacesNode(aNewParentPlacesNode);
    if (!parentElt) {
      return;
    }
    if (parentElt == this._rootElt) {
      // Node is on the toolbar.
      // Do nothing if the node will never be visible.
      let lastBuiltIndex = this._rootElt.children.length - 1;
      if (aOldIndex > lastBuiltIndex && aNewIndex > lastBuiltIndex + 1) {
        return;
      }

      let elt = this._getDOMNodeForPlacesNode(aPlacesNode);
      if (elt) {
        // Here we need the <menu>.
        if (elt.localName == "menupopup") {
          elt = elt.parentNode;
        }
        this._removeChild(elt);
      }

      if (aNewIndex > lastBuiltIndex + 1) {
        if (elt) {
          this._forgetDOMNodes(elt);
        }
        if (this._resultNode.childCount > this._rootElt.children.length) {
          // If the element was built and becomes non built, another node should
          // be built to keep a coherent number of children.
          this._insertNewItem(
            this._resultNode.getChild(this._rootElt.children.length),
            this._rootElt
          );
        }
        return;
      }

      if (!elt) {
        // The node has not been inserted yet, so we must create it.
        elt = this._insertNewItem(
          aPlacesNode,
          this._rootElt,
          this._rootElt.children[aNewIndex]
        );
        let icon = aPlacesNode.icon;
        if (icon) {
          elt.setAttribute("image", ChromeUtils.encodeURIForSrcset(icon));
        }
      } else {
        this._rootElt.insertBefore(elt, this._rootElt.children[aNewIndex]);
      }

      // The chevron view may get nodeMoved after the toolbar.  In such a case,
      // we should ensure (by manually swapping menuitems) that the actual nodes
      // are in the final position before updateNodesVisibility tries to update
      // their visibility, or the chevron may go out of sync.
      // Luckily updateNodesVisibility runs on a timer, so, by the time it updates
      // nodes, the menu has already handled the notification.

      this.updateNodesVisibility();
      return;
    }

    super.nodeMoved(
      aPlacesNode,
      aOldParentPlacesNode,
      aOldIndex,
      aNewParentPlacesNode,
      aNewIndex
    );
  }

  nodeTitleChanged(aPlacesNode, aNewTitle) {
    let elt = this._getDOMNodeForPlacesNode(aPlacesNode);

    // Nothing to do if it's a never-visible node.
    if (!elt || elt == this._rootElt) {
      return;
    }

    super.nodeTitleChanged(aPlacesNode, aNewTitle);

    // Here we need the <menu>.
    if (elt.localName == "menupopup") {
      elt = elt.parentNode;
    }

    if (elt.parentNode == this._rootElt) {
      // Node is on the toolbar.
      if (elt.style.visibility != "hidden") {
        this.updateNodesVisibility();
      }
    }
  }

  invalidateContainer(aPlacesNode) {
    let elt = this._getDOMNodeForPlacesNode(aPlacesNode);
    // Nothing to do if it's a never-visible node.
    if (!elt) {
      return;
    }

    if (elt == this._rootElt) {
      // Container is the toolbar itself.
      let instance = (this._rebuildingInstance = {});
      if (!this._rebuilding) {
        this._rebuilding = Promise.withResolvers();
      }
      this._rebuild()
        .catch(console.error)
        .finally(() => {
          if (instance == this._rebuildingInstance) {
            this._rebuilding.resolve();
            this._rebuilding = null;
          }
        });
      return;
    }

    super.invalidateContainer(aPlacesNode);
  }

  _clearOverFolder() {
    // The mouse is no longer dragging over the stored menubutton.
    // Close the menubutton, clear out drag styles, and clear all
    // timers for opening/closing it.
    if (this._overFolder.elt && this._overFolder.elt.menupopup) {
      if (!this._overFolder.elt.menupopup.hasAttribute("dragover")) {
        this._overFolder.elt.menupopup.hidePopup();
      }
      this._overFolder.elt.removeAttribute("dragover");
      this._overFolder.elt = null;
    }
    if (this._overFolder.openTimer) {
      this._overFolder.openTimer.cancel();
      this._overFolder.openTimer = null;
    }
    if (this._overFolder.closeTimer) {
      this._overFolder.closeTimer.cancel();
      this._overFolder.closeTimer = null;
    }
  }

  /**
   * This function returns information about where to drop when dragging over
   * the toolbar.
   *
   * @param {object} aEvent
   *   The associated event.
   * @returns {object}
   *   - ip: the insertion point for the bookmarks service.
   *   - beforeIndex: child index to drop before, for the drop indicator.
   *   - folderElt: the folder to drop into, if applicable.
   */
  _getDropPoint(aEvent) {
    if (!PlacesUtils.nodeIsFolderOrShortcut(this._resultNode)) {
      return null;
    }

    let dropPoint = { ip: null, beforeIndex: null, folderElt: null };
    let elt = aEvent.target;
    if (
      elt._placesNode &&
      elt != this._rootElt &&
      elt.localName != "menupopup"
    ) {
      let eltRect = elt.getBoundingClientRect();
      let eltIndex = Array.prototype.indexOf.call(this._rootElt.children, elt);
      if (
        PlacesUtils.nodeIsFolderOrShortcut(elt._placesNode) &&
        !PlacesUIUtils.isFolderReadOnly(elt._placesNode)
      ) {
        // This is a folder.
        // If we are in the middle of it, drop inside it.
        // Otherwise, drop before it, with regards to RTL mode.
        let threshold = eltRect.width * 0.25;
        if (
          this.isRTL
            ? aEvent.clientX > eltRect.right - threshold
            : aEvent.clientX < eltRect.left + threshold
        ) {
          // Drop before this folder.
          dropPoint.ip = new PlacesInsertionPoint({
            parentGuid: PlacesUtils.getConcreteItemGuid(this._resultNode),
            index: eltIndex,
            orientation: Ci.nsITreeView.DROP_BEFORE,
          });
          dropPoint.beforeIndex = eltIndex;
        } else if (
          this.isRTL
            ? aEvent.clientX > eltRect.left + threshold
            : aEvent.clientX < eltRect.right - threshold
        ) {
          // Drop inside this folder.
          let tagName = PlacesUtils.nodeIsTagQuery(elt._placesNode)
            ? elt._placesNode.title
            : null;
          dropPoint.ip = new PlacesInsertionPoint({
            parentGuid: PlacesUtils.getConcreteItemGuid(elt._placesNode),
            tagName,
          });
          dropPoint.beforeIndex = eltIndex;
          dropPoint.folderElt = elt;
        } else {
          // Drop after this folder.
          let beforeIndex =
            eltIndex == this._rootElt.children.length - 1 ? -1 : eltIndex + 1;

          dropPoint.ip = new PlacesInsertionPoint({
            parentGuid: PlacesUtils.getConcreteItemGuid(this._resultNode),
            index: beforeIndex,
            orientation: Ci.nsITreeView.DROP_BEFORE,
          });
          dropPoint.beforeIndex = beforeIndex;
        }
      } else {
        // This is a non-folder node or a read-only folder.
        // Drop before it with regards to RTL mode.
        let threshold = eltRect.width * 0.5;
        if (
          this.isRTL
            ? aEvent.clientX > eltRect.left + threshold
            : aEvent.clientX < eltRect.left + threshold
        ) {
          // Drop before this bookmark.
          dropPoint.ip = new PlacesInsertionPoint({
            parentGuid: PlacesUtils.getConcreteItemGuid(this._resultNode),
            index: eltIndex,
            orientation: Ci.nsITreeView.DROP_BEFORE,
          });
          dropPoint.beforeIndex = eltIndex;
        } else {
          // Drop after this bookmark.
          let beforeIndex =
            eltIndex == this._rootElt.children.length - 1 ? -1 : eltIndex + 1;
          dropPoint.ip = new PlacesInsertionPoint({
            parentGuid: PlacesUtils.getConcreteItemGuid(this._resultNode),
            index: beforeIndex,
            orientation: Ci.nsITreeView.DROP_BEFORE,
          });
          dropPoint.beforeIndex = beforeIndex;
        }
      }
    } else if (elt == this._chevron) {
      // If drop on the chevron, insert after the last bookmark.
      dropPoint.ip = new PlacesInsertionPoint({
        parentGuid: PlacesUtils.getConcreteItemGuid(this._resultNode),
        orientation: Ci.nsITreeView.DROP_BEFORE,
      });
      dropPoint.beforeIndex = -1;
    } else {
      dropPoint.ip = new PlacesInsertionPoint({
        parentGuid: PlacesUtils.getConcreteItemGuid(this._resultNode),
        orientation: Ci.nsITreeView.DROP_BEFORE,
      });

      // If could not find an insertion point before bookmark items or empty,
      // drop after the last bookmark.
      dropPoint.beforeIndex = -1;

      let canInsertHere = this.isRTL
        ? (x, rect) => x >= Math.round(rect.right)
        : (x, rect) => x <= Math.round(rect.left);

      // Find the bookmark placed just after the mouse point as the insertion
      // point.
      for (let i = 0; i < this._rootElt.children.length; i++) {
        let childRect = window.windowUtils.getBoundsWithoutFlushing(
          this._rootElt.children[i]
        );
        if (canInsertHere(aEvent.clientX, childRect)) {
          dropPoint.beforeIndex = i;
          dropPoint.ip.index = i;
          break;
        }
      }
    }

    return dropPoint;
  }

  _setTimer(aTime) {
    let timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    timer.initWithCallback(this, aTime, timer.TYPE_ONE_SHOT);
    return timer;
  }

  get name() {
    return "PlacesToolbar";
  }

  notify(aTimer) {
    if (aTimer == this._updateNodesVisibilityTimer) {
      this._updateNodesVisibilityTimer = null;
      this._updateNodesVisibilityTimerCallback();
    } else if (aTimer == this._overFolder.openTimer) {
      // * Timer to open a menubutton that's being dragged over.
      // Set the autoopen attribute on the folder's menupopup so that
      // the menu will automatically close when the mouse drags off of it.
      this._overFolder.elt.menupopup.setAttribute("autoopened", "true");
      this._overFolder.elt.open = true;
      this._overFolder.openTimer = null;
    } else if (aTimer == this._overFolder.closeTimer) {
      // * Timer to close a menubutton that's been dragged off of.
      // Close the menubutton if we are not dragging over it or one of
      // its children.  The autoopened attribute will let the menu know to
      // close later if the menu is still being dragged over.
      let currentPlacesNode = PlacesControllerDragHelper.currentDropTarget;
      let inHierarchy = false;
      while (currentPlacesNode) {
        if (currentPlacesNode == this._rootElt) {
          inHierarchy = true;
          break;
        }
        currentPlacesNode = currentPlacesNode.parentNode;
      }
      // The _clearOverFolder() function will close the menu for
      // _overFolder.elt.  So null it out if we don't want to close it.
      if (inHierarchy) {
        this._overFolder.elt = null;
      }

      // Clear out the folder and all associated timers.
      this._clearOverFolder();
    }
  }

  _onMouseOver(aEvent) {
    let button = aEvent.target;
    if (
      button.parentNode == this._rootElt &&
      button._placesNode &&
      PlacesUtils.nodeIsURI(button._placesNode)
    ) {
      window.XULBrowserWindow.setOverLink(aEvent.target._placesNode.uri);
    }
  }

  _onMouseOut() {
    window.XULBrowserWindow.setOverLink("");
  }

  _onMouseDown(aEvent) {
    let target = aEvent.target;
    if (
      aEvent.button == 0 &&
      target.localName == "toolbarbutton" &&
      target.getAttribute("type") == "menu"
    ) {
      let modifKey = aEvent.shiftKey || aEvent.getModifierState("Accel");
      if (modifKey) {
        // Do not open the popup since BEH_onClick is about to
        // open all child uri nodes in tabs.
        this._allowPopupShowing = false;
      }
    }
    PlacesUIUtils.maybeSpeculativeConnectOnMouseDown(aEvent);
  }

  _cleanupDragDetails() {
    // Called on dragend and drop.
    PlacesControllerDragHelper.currentDropTarget = null;
    this._draggedElt = null;
    this._dropIndicator.collapsed = true;
  }

  _onDragStart(aEvent) {
    // Sub menus have their own d&d handlers.
    let draggedElt = aEvent.target;
    if (draggedElt.parentNode != this._rootElt || !draggedElt._placesNode) {
      return;
    }

    if (
      draggedElt.localName == "toolbarbutton" &&
      draggedElt.getAttribute("type") == "menu"
    ) {
      // If the drag gesture on a container is toward down we open instead
      // of dragging.
      let translateY = this._cachedMouseMoveEvent.clientY - aEvent.clientY;
      let translateX = this._cachedMouseMoveEvent.clientX - aEvent.clientX;
      if (translateY >= Math.abs(translateX / 2)) {
        // Don't start the drag.
        aEvent.preventDefault();
        // Open the menu.
        draggedElt.open = true;
        return;
      }

      // If the menu is open, close it.
      if (draggedElt.open) {
        draggedElt.menupopup.hidePopup();
        draggedElt.open = false;
      }
    }

    // Activate the view and cache the dragged element.
    this._draggedElt = draggedElt._placesNode;
    this._rootElt.focus();

    this._controller.setDataTransfer(aEvent);
    aEvent.stopPropagation();
  }

  /**
   * Finds the last visible toolbar widget before the PlacesToolbar container.
   *
   * @returns {Element|null}
   *   The last visible widget element, or null if none found.
   */
  #findPrecedingToolbarWidget() {
    let toolbar = this._rootElt.closest("toolbar");
    if (!toolbar) {
      return null;
    }
    let placesContainer = this._rootElt.closest("toolbaritem");
    let lastWidget = null;
    for (let child of toolbar.children) {
      if (child == placesContainer) {
        break;
      }
      if (
        !child.hidden &&
        !child.collapsed &&
        child.getBoundingClientRect().width > 0
      ) {
        lastWidget = child;
      }
    }
    return lastWidget;
  }

  _onDragOver(aEvent) {
    // Cache the dataTransfer
    PlacesControllerDragHelper.currentDropTarget = aEvent.target;
    let dt = aEvent.dataTransfer;

    let dropPoint = this._getDropPoint(aEvent);
    if (
      !dropPoint ||
      !dropPoint.ip ||
      !PlacesControllerDragHelper.canDrop(dropPoint.ip, dt)
    ) {
      this._dropIndicator.collapsed = true;
      aEvent.stopPropagation();
      return;
    }

    if (dropPoint.folderElt || aEvent.originalTarget == this._chevron) {
      // Dropping over a menubutton or chevron button.
      // Set styles and timer to open relative menupopup.
      let overElt = dropPoint.folderElt || this._chevron;
      if (this._overFolder.elt != overElt) {
        this._clearOverFolder();
        this._overFolder.elt = overElt;
        this._overFolder.openTimer = this._setTimer(this._overFolder.hoverTime);
      }
      if (!this._overFolder.elt.hasAttribute("dragover")) {
        this._overFolder.elt.setAttribute("dragover", "true");
      }

      this._dropIndicator.collapsed = true;
    } else {
      // Dragging over a normal toolbarbutton,
      // show indicator bar and move it to the appropriate drop point.
      let ind = this._dropIndicator;
      ind.parentNode.collapsed = false;
      let halfInd = ind.clientWidth / 2;
      let translateX;

      if (this.isRTL) {
        halfInd = Math.ceil(halfInd);
        translateX = 0 - this._rootElt.getBoundingClientRect().right - halfInd;
        if (this._rootElt.firstElementChild) {
          if (dropPoint.beforeIndex == -1) {
            translateX +=
              this._rootElt.lastElementChild.getBoundingClientRect().left;
          } else {
            translateX +=
              this._rootElt.children[
                dropPoint.beforeIndex
              ].getBoundingClientRect().right;
          }
        } else {
          // When there are no bookmark items, position the indicator at the
          // edge of any preceding toolbar widgets (e.g., import-bookmarks button)
          // which appear on the right in RTL layout.
          let prevWidget = this.#findPrecedingToolbarWidget();
          if (prevWidget) {
            translateX += prevWidget.getBoundingClientRect().left;
          }
        }
      } else {
        halfInd = Math.floor(halfInd);
        translateX = 0 - this._rootElt.getBoundingClientRect().left + halfInd;
        if (this._rootElt.firstElementChild) {
          if (dropPoint.beforeIndex == -1) {
            translateX +=
              this._rootElt.lastElementChild.getBoundingClientRect().right;
          } else {
            translateX +=
              this._rootElt.children[
                dropPoint.beforeIndex
              ].getBoundingClientRect().left;
          }
        } else {
          // When there are no bookmark items, position the indicator at the
          // edge of any preceding toolbar widgets (e.g., import-bookmarks button).
          let prevWidget = this.#findPrecedingToolbarWidget();
          if (prevWidget) {
            translateX += prevWidget.getBoundingClientRect().right;
          }
        }
      }

      ind.style.transform = "translate(" + Math.round(translateX) + "px)";
      ind.style.marginInlineStart = -ind.clientWidth + "px";
      ind.collapsed = false;

      // Clear out old folder information.
      this._clearOverFolder();
    }

    aEvent.preventDefault();
    aEvent.stopPropagation();
  }

  _onDrop(aEvent) {
    PlacesControllerDragHelper.currentDropTarget = aEvent.target;

    let dropPoint = this._getDropPoint(aEvent);
    if (dropPoint && dropPoint.ip) {
      PlacesControllerDragHelper.onDrop(
        dropPoint.ip,
        aEvent.dataTransfer
      ).catch(console.error);
      aEvent.preventDefault();
    }

    this._cleanupDragDetails();
    aEvent.stopPropagation();
  }

  _onDragLeave() {
    PlacesControllerDragHelper.currentDropTarget = null;

    this._dropIndicator.collapsed = true;

    // If we hovered over a folder, close it now.
    if (this._overFolder.elt) {
      this._overFolder.closeTimer = this._setTimer(this._overFolder.hoverTime);
    }
  }

  _onDragEnd() {
    this._cleanupDragDetails();
  }

  _onPopupShowing(aEvent) {
    if (!this._allowPopupShowing) {
      this._allowPopupShowing = true;
      aEvent.preventDefault();
      return;
    }

    let parent = aEvent.target.parentNode;
    if (parent.localName == "toolbarbutton") {
      this._openedMenuButton = parent;
    }

    super._onPopupShowing(aEvent);
  }

  _onPopupHidden(aEvent) {
    let popup = aEvent.target;
    let placesNode = popup._placesNode;
    // Avoid handling popuphidden of inner views
    if (
      placesNode &&
      PlacesUIUtils.getViewForNode(popup) == this &&
      // UI performance: folder queries are cheap, keep the resultnode open
      // so we don't rebuild its contents whenever the popup is reopened.
      !PlacesUtils.nodeIsFolderOrShortcut(placesNode)
    ) {
      placesNode.containerOpen = false;
    }

    let parent = popup.parentNode;
    if (parent.localName == "toolbarbutton") {
      this._openedMenuButton = null;
      // Clear the dragover attribute if present, if we are dragging into a
      // folder in the hierachy of current opened popup we don't clear
      // this attribute on clearOverFolder.  See Notify for closeTimer.
      if (parent.hasAttribute("dragover")) {
        parent.removeAttribute("dragover");
      }
    }
  }

  _onMouseMove(aEvent) {
    // Used in dragStart to prevent dragging folders when dragging down.
    this._cachedMouseMoveEvent = aEvent;

    if (
      this._openedMenuButton == null ||
      PlacesControllerDragHelper.getSession()
    ) {
      return;
    }

    let target = aEvent.originalTarget;
    if (
      this._openedMenuButton != target &&
      target.localName == "toolbarbutton" &&
      target.type == "menu"
    ) {
      this._openedMenuButton.open = false;
      target.open = true;
    }
  }
}

/**
 * View for Places menus.  This object should be created during the first
 * popupshowing that's dispatched on the menu.
 *
 */
class PlacesMenu extends PlacesViewBase {
  /**
   *
   * @param {Event} popupShowingEvent
   *   The event associated with opening the menu.
   * @param {string} placesUrl
   *   The query associated with the view on the menu.
   */
  constructor(popupShowingEvent, placesUrl) {
    super(
      placesUrl,
      popupShowingEvent.target, // <menupopup>
      popupShowingEvent.target.parentNode // <menu>
    );

    this._addEventListeners(
      this._rootElt,
      ["popupshowing", "popuphidden"],
      true
    );
    this._addEventListeners(window, ["unload"], false);
    this._addEventListeners(this._rootElt, ["mousedown"], false);
    this._onPopupShowing(popupShowingEvent);
  }

  _init() {
    this._viewElt._placesView = this;
    if (AppConstants.platform === "macosx") {
      // Must walk up to support views in sub-menus, like Bookmarks Toolbar menu.
      for (let elt = this._viewElt.parentNode; elt; elt = elt.parentNode) {
        if (elt.localName == "menubar") {
          this._nativeView = true;
          break;
        }
      }
    }
  }

  _rebuildPopup(popup) {
    popup._folderResultLimit =
      this._nativeView && PlacesUtils.nodeIsFolderOrShortcut(popup._placesNode)
        ? Math.max(0, NATIVE_MENU_MAX_FOLDER_RESULTS)
        : 0;
    super._rebuildPopup(popup, popup._folderResultLimit);
  }

  _getMenuItemForPlacesNode(node) {
    let element = this._getDOMNodeForPlacesNode(node);
    return element?.localName == "menupopup" ? element.parentNode : element;
  }

  _updateNativeFolder(node, changedNode, newIndex) {
    if (!this._nativeView || !PlacesUtils.nodeIsFolderOrShortcut(node)) {
      return false;
    }
    let popup = this._getDOMNodeForPlacesNode(node);
    if (!popup) {
      return true;
    }
    if (NATIVE_MENU_MAX_FOLDER_RESULTS <= 0 && !popup._folderResultLimit) {
      return false;
    }
    if (!popup._built || !node.containerOpen) {
      popup._built = false;
      return true;
    }

    let limit = popup._folderResultLimit || NATIVE_MENU_MAX_FOLDER_RESULTS;
    popup._folderResultLimit = limit;
    let changedElement = this._getMenuItemForPlacesNode(changedNode);
    if (!changedElement?.isConnected && (newIndex < 0 || newIndex >= limit)) {
      this._mayAddCommandsItems(popup);
      return true;
    }
    if (
      changedElement?.parentNode == popup &&
      newIndex >= 0 &&
      newIndex < limit
    ) {
      return false;
    }
    let count = Math.min(node.childCount, limit);
    if (count) {
      this._setEmptyPopupStatus(popup, false);
    }
    let visibleNodes = new Set();
    for (let i = 0; i < count; ++i) {
      visibleNodes.add(node.getChild(i));
    }
    let child = popup._startMarker.nextElementSibling;
    while (child != popup._endMarker) {
      let next = child.nextElementSibling;
      if (child._placesNode && !visibleNodes.has(child._placesNode)) {
        this._forgetDOMNodes(child);
        child.remove();
      }
      child = next;
    }
    child = popup._startMarker.nextElementSibling;
    for (let placesNode of visibleNodes) {
      let element = this._getMenuItemForPlacesNode(placesNode);
      if (element?.parentNode != popup) {
        element = this._createDOMNodeForPlacesNode(placesNode);
      }
      if (element != child) {
        popup.insertBefore(element, child);
      }
      child = element.nextElementSibling;
    }
    this._mayAddCommandsItems(popup);
    if (!count) {
      this._setEmptyPopupStatus(popup, true);
    }
    return true;
  }

  nodeInserted(parent, node, index) {
    if (!this._updateNativeFolder(parent, node, index)) {
      super.nodeInserted(parent, node, index);
    }
  }

  nodeRemoved(parent, node, index) {
    if (!this._updateNativeFolder(parent, node, -1)) {
      super.nodeRemoved(parent, node, index);
    }
  }

  nodeMoved(node, oldParent, oldIndex, newParent, newIndex) {
    if (!this._updateNativeFolder(newParent, node, newIndex)) {
      super.nodeMoved(node, oldParent, oldIndex, newParent, newIndex);
    }
  }

  _onPopupShowing(event) {
    let popup = event.originalTarget;
    if (
      this._nativeView &&
      popup._placesNode &&
      PlacesUIUtils.getViewForNode(popup) == this &&
      PlacesUtils.nodeIsFolderOrShortcut(popup._placesNode) &&
      popup._folderResultLimit != Math.max(0, NATIVE_MENU_MAX_FOLDER_RESULTS)
    ) {
      popup._built = false;
    }
    if (
      this._nativeView &&
      popup != this._rootElt &&
      PlacesUIUtils.getViewForNode(popup) == this &&
      popup._placesNode &&
      PlacesUtils.nodeIsQuery(popup._placesNode) &&
      !PlacesUtils.nodeIsFolderOrShortcut(popup._placesNode)
    ) {
      let options = PlacesUtils.asQuery(popup._placesNode).queryOptions;
      if (popup._originalMaxResults === undefined) {
        let originalOptions = {};
        PlacesUtils.history.queryStringToQuery(
          popup._placesNode.uri,
          {},
          originalOptions
        );
        popup._originalMaxResults = originalOptions.value.maxResults;
      }
      let originalMaxResults = popup._originalMaxResults;
      let isLimited = options.maxResults != originalMaxResults;
      let shouldLimit =
        NATIVE_MENU_MAX_RESULTS > 0 &&
        (!originalMaxResults || originalMaxResults > NATIVE_MENU_MAX_RESULTS);
      // Keep the applied limit because the node may still have cached children.
      if (!isLimited && shouldLimit) {
        options.maxResults = NATIVE_MENU_MAX_RESULTS + 1;
        isLimited = true;
      }
      popup._queryResultLimit = isLimited ? options.maxResults : 0;
    }
    super._onPopupShowing(event);
  }

  _mayAddCommandsItems(popup) {
    super._mayAddCommandsItems(popup);
    if (!this._nativeView) {
      return;
    }
    let node = popup._placesNode;
    let hasMore =
      (popup._folderResultLimit > 0 &&
        node.childCount > popup._folderResultLimit) ||
      (popup != this._rootElt &&
        popup._queryResultLimit > 0 &&
        node.childCount >= popup._queryResultLimit);
    if (hasMore && !popup._moreMenuitem) {
      popup._moreMenuitem = document.createXULElement("menuitem");
      document.l10n.setAttributes(
        popup._moreMenuitem,
        "menu-places-show-more-in-sidebar"
      );
      popup._moreMenuitem.addEventListener("command", () => {
        PlacesUIUtils.showSidebarForNode(popup._placesNode).catch(
          console.error
        );
      });
      // Keep the command after removals. Recheck on the next popupshowing.
      popup.insertBefore(
        popup._moreMenuitem,
        popup._endMarker.nextElementSibling
      );
    } else if (!hasMore && popup._moreMenuitem) {
      popup._moreMenuitem.remove();
      popup._moreMenuitem = null;
      if (!node.childCount) {
        this._setEmptyPopupStatus(popup, true);
      }
    }
  }

  uninit() {
    this._removeEventListeners(
      this._rootElt,
      ["popupshowing", "popuphidden"],
      true
    );
    this._removeEventListeners(window, ["unload"], false);
    this._removeEventListeners(this._rootElt, ["mousedown"], false);

    super.uninit();
  }

  handleEvent(aEvent) {
    switch (aEvent.type) {
      case "unload":
        this.uninit();
        break;
      case "popupshowing":
        this._onPopupShowing(aEvent);
        break;
      case "popuphidden":
        this._onPopupHidden(aEvent);
        break;
      case "mousedown":
        this._onMouseDown(aEvent);
        break;
    }
  }

  _onPopupHidden(aEvent) {
    // Avoid handling popuphidden of inner views.
    let popup = aEvent.originalTarget;
    let placesNode = popup._placesNode;
    if (!placesNode || PlacesUIUtils.getViewForNode(popup) != this) {
      return;
    }

    // UI performance: folder queries are cheap, keep the resultnode open
    // so we don't rebuild its contents whenever the popup is reopened.
    if (!PlacesUtils.nodeIsFolderOrShortcut(placesNode)) {
      placesNode.containerOpen = false;
    }

    // The autoopened attribute is set for folders which have been
    // automatically opened when dragged over.  Turn off this attribute
    // when the folder closes because it is no longer applicable.
    popup.removeAttribute("autoopened");
    popup.removeAttribute("dragstart");
  }

  // We don't have a facility for catch "mousedown" events on the native
  // Mac menus because Mac doesn't expose it
  _onMouseDown(aEvent) {
    PlacesUIUtils.maybeSpeculativeConnectOnMouseDown(aEvent);
  }
}

// This is used from CustomizableWidgets.sys.mjs using a `window` reference,
// so we have to expose this on the global.
this.PlacesPanelview = class PlacesPanelview extends PlacesViewBase {
  constructor(placeUrl, rootElt, viewElt) {
    super(placeUrl, rootElt, viewElt);
    this._viewElt._placesView = this;
    // We're simulating a popup show, because a panelview may only be shown when
    // its containing popup is already shown.
    this._onPopupShowing({ originalTarget: this._rootElt });
    this._addEventListeners(window, ["unload"]);
    this._rootElt.setAttribute("context", "placesContext");
  }

  get events() {
    if (this._events) {
      return this._events;
    }
    return (this._events = [
      "click",
      "command",
      "dragend",
      "dragstart",
      "ViewHiding",
      "ViewShown",
      "mousedown",
    ]);
  }

  handleEvent(event) {
    switch (event.type) {
      case "click":
        // For middle clicks, fall through to the command handler.
        if (event.button != 1) {
          break;
        }
      // fall through
      case "command":
        this._onCommand(event);
        break;
      case "dragend":
        this._onDragEnd(event);
        break;
      case "dragstart":
        this._onDragStart(event);
        break;
      case "unload":
        this.uninit(event);
        break;
      case "ViewHiding":
        this._onPopupHidden(event);
        break;
      case "ViewShown":
        this._onViewShown(event);
        break;
      case "mousedown":
        this._onMouseDown(event);
        break;
    }
  }

  _onCommand(event) {
    event = BrowserUtils.getRootEvent(event);
    let button = event.originalTarget;
    if (!button._placesNode) {
      return;
    }

    let modifKey =
      AppConstants.platform === "macosx" ? event.metaKey : event.ctrlKey;
    if (!PlacesUIUtils.openInTabClosesMenu && modifKey) {
      // If 'Recent Bookmarks' in Bookmarks Panel.
      if (button.parentNode.id == "panelMenu_bookmarksMenu") {
        button.setAttribute("closemenu", "none");
      }
    } else {
      button.removeAttribute("closemenu");
    }
    PlacesUIUtils.openNodeWithEvent(button._placesNode, event);
    // Unlike left-click, middle-click requires manual menu closing.
    if (
      button.parentNode.id != "panelMenu_bookmarksMenu" ||
      (event.type == "click" &&
        event.button == 1 &&
        PlacesUIUtils.openInTabClosesMenu)
    ) {
      this.panelMultiView.closest("panel").hidePopup();
    }
  }

  destroyContextMenu() {
    super.destroyContextMenu();
    this.maybeClosePanel(PlacesUIUtils.lastContextMenuCommand);
  }

  /**
   * Closes the view depending on the command.
   *
   * This is necessary because PlacesPanelview's buttons are not
   * XUL menuitems and are not affected by the closemenu attribute.
   *
   * @param {string} command the placesCommands command
   */
  maybeClosePanel(command) {
    switch (command) {
      // placesCmd_open:newcontainertab is not a placesCommand but it
      // is set by PlacesUIUtils.openInContainerTab to close the panel.
      case "placesCmd_open:newcontainertab":
      case "placesCmd_open:tab":
        if (
          this._viewElt.id != "PanelUI-bookmarks" ||
          PlacesUIUtils.openInTabClosesMenu
        ) {
          this.panelMultiView.closest("panel").hidePopup();
        }
        break;
      case "placesCmd_createBookmark":
      case "placesCmd_deleteDataHost":
        this.panelMultiView.closest("panel").hidePopup();
        break;
    }
  }

  _onDragEnd() {
    this._draggedElt = null;
  }

  _onDragStart(event) {
    let draggedElt = event.originalTarget;
    if (draggedElt.parentNode != this._rootElt || !draggedElt._placesNode) {
      return;
    }

    // Activate the view and cache the dragged element.
    this._draggedElt = draggedElt._placesNode;
    this._rootElt.focus();

    this._controller.setDataTransfer(event);
    event.stopPropagation();
  }

  uninit(event) {
    this._removeEventListeners(this.panelMultiView, this.events);
    this._removeEventListeners(window, ["unload"]);
    delete this.panelMultiView;
    super.uninit(event);
  }

  _createDOMNodeForPlacesNode(placesNode) {
    this._domNodes.delete(placesNode);

    let element;
    let type = placesNode.type;
    if (type == Ci.nsINavHistoryResultNode.RESULT_TYPE_SEPARATOR) {
      element = document.createXULElement("toolbarseparator");
    } else {
      if (type != Ci.nsINavHistoryResultNode.RESULT_TYPE_URI) {
        throw new Error("Unexpected node");
      }

      element = document.createXULElement("toolbarbutton");
      element.classList.add(
        "subviewbutton",
        "subviewbutton-iconic",
        "bookmark-item"
      );
      element.setAttribute(
        "scheme",
        PlacesUIUtils.guessUrlSchemeForUI(placesNode.uri)
      );
      element.setAttribute("label", PlacesUIUtils.getBestTitle(placesNode));

      let icon = placesNode.icon;
      if (icon) {
        element.setAttribute("image", icon);
      }
    }

    element._placesNode = placesNode;
    if (!this._domNodes.has(placesNode)) {
      this._domNodes.set(placesNode, element);
    }

    return element;
  }

  _setEmptyPopupStatus(panelview, empty = false) {
    if (!panelview._emptyMenuitem) {
      panelview._emptyMenuitem = document.createXULElement("toolbarbutton");
      panelview._emptyMenuitem.setAttribute("disabled", true);
      panelview._emptyMenuitem.className = "subviewbutton";
      document.l10n.setAttributes(
        panelview._emptyMenuitem,
        "places-empty-bookmarks-folder"
      );
    }

    if (empty) {
      panelview.setAttribute("emptyplacesresult", "true");
      // Don't add the menuitem if there is static content.
      // We also support external usage for custom crafted panels - which'll have
      // no markers present.
      if (
        !panelview._startMarker ||
        (!panelview._startMarker.previousElementSibling &&
          !panelview._endMarker.nextElementSibling)
      ) {
        panelview.insertBefore(panelview._emptyMenuitem, panelview._endMarker);
      }
    } else {
      panelview.removeAttribute("emptyplacesresult");
      try {
        panelview.removeChild(panelview._emptyMenuitem);
      } catch (ex) {}
    }
  }

  _isPopupOpen() {
    return PanelView.forNode(this._viewElt).active;
  }

  _onPopupHidden(event) {
    let panelview = event.originalTarget;
    let placesNode = panelview._placesNode;
    // Avoid handling ViewHiding of inner views
    if (
      placesNode &&
      PlacesUIUtils.getViewForNode(panelview) == this &&
      // UI performance: folder queries are cheap, keep the resultnode open
      // so we don't rebuild its contents whenever the popup is reopened.
      !PlacesUtils.nodeIsFolderOrShortcut(placesNode)
    ) {
      placesNode.containerOpen = false;
    }
  }

  _onPopupShowing(event) {
    // If the event came from the root element, this is the first time
    // we ever get here.
    if (event.originalTarget == this._rootElt) {
      // Start listening for events from all panels inside the panelmultiview.
      this.panelMultiView = this._viewElt.panelMultiView;
      this._addEventListeners(this.panelMultiView, this.events);
    }
    super._onPopupShowing(event);
  }

  _onViewShown(event) {
    if (event.originalTarget != this._viewElt) {
      return;
    }

    // Because PanelMultiView reparents the panelview internally, the controller
    // may get lost. In that case we'll append it again, because we certainly
    // need it later!
    if (!this.controllers.getControllerCount() && this._controller) {
      this.controllers.appendController(this._controller);
    }
  }

  _onMouseDown(aEvent) {
    PlacesUIUtils.maybeSpeculativeConnectOnMouseDown(aEvent);
  }
};
