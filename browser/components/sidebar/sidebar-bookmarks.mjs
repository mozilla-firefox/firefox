/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

import { html, when } from "chrome://global/content/vendor/lit.all.mjs";

import { SidebarPage } from "./sidebar-page.mjs";
import {
  navigateToLink,
  escapeHtmlEntities,
} from "chrome://browser/content/firefoxview/helpers.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/sidebar/sidebar-bookmark-list.mjs";

let XPCOMUtils;

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  OpenInTabsUtils:
    "moz-src:///browser/components/tabbrowser/OpenInTabsUtils.sys.mjs",
  PlacesTransactions: "resource://gre/modules/PlacesTransactions.sys.mjs",
  PlacesUIUtils: "moz-src:///browser/components/places/PlacesUIUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SidebarTreeView:
    "moz-src:///browser/components/sidebar/SidebarTreeView.sys.mjs",
});

const bookmarkFolderLocalization = new Localization(
  ["browser/sidebar.ftl"],
  true
);

XPCOMUtils = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
).XPCOMUtils;
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "virtualListEnabledPref",
  "browser.firefox-view.virtual-list.enabled"
);

export class SidebarBookmarks extends SidebarPage {
  static properties = {
    bookmarks: { type: Object },
    searchQuery: { type: String },
    searchResults: { type: Array },
  };

  static queries = {
    panelHeader: "sidebar-panel-header",
    searchInput: "moz-input-search",
    bookmarkList: "sidebar-bookmark-list",
  };

  #placesEventTypes = [
    "bookmark-added",
    "bookmark-removed",
    "bookmark-moved",
    "bookmark-title-changed",
    "bookmark-url-changed",
  ];

  #onPlacesEvents = async () => {
    this.bookmarks = await this.getBookmarksList();
    if (this.searchQuery) {
      this.searchResults = this.#searchBookmarks(
        this.bookmarks,
        this.searchQuery.toLowerCase()
      );
    }
  };

  #expandedFolderGuids = new Set();

  #contextMenuItems = null;

  #initContextMenuItems() {
    const q = id => this._contextMenu.querySelector(id);
    const openAllBookmarks = q("#sidebar-bookmarks-context-open-all-bookmarks");
    const sepOpenAll = q("#sidebar-bookmarks-context-sep-open-all");
    const sortByName = q("#sidebar-bookmarks-context-sort-by-name");
    const sepSort = q("#sidebar-bookmarks-context-sep-sort");
    const openInTab = q("#sidebar-bookmarks-context-open-in-tab");
    const openInWindow = q("#sidebar-bookmarks-context-open-in-window");
    const sepOpenOptions = q("#sidebar-bookmarks-context-sep-open-options");
    const sepEditCopy = q("#sidebar-bookmarks-context-sep-edit-copy");
    const copyLink = q("#sidebar-bookmarks-context-copy-link");
    const sepCutCopy = q("#sidebar-bookmarks-context-sep-cut-copy");
    const cut = q("#sidebar-bookmarks-context-cut");
    const copy = q("#sidebar-bookmarks-context-copy");
    const openInContainerTab = q(
      "#sidebar-bookmarks-context-open-in-container-tab"
    );
    const openInPrivateWindow = q(
      "#sidebar-bookmarks-context-open-in-private-window"
    );
    const editBookmark = q("#sidebar-bookmarks-context-edit-bookmark");
    const deleteBookmark = q("#sidebar-bookmarks-context-delete-bookmark");
    const showInFolder = q("#sidebar-bookmarks-context-show-in-folder");
    const sepAdd = q("#sidebar-bookmarks-context-sep-add");
    const addBookmark = q("#sidebar-bookmarks-context-add-bookmark");
    const addFolder = q("#sidebar-bookmarks-context-add-folder");
    const addSeparator = q("#sidebar-bookmarks-context-add-separator");
    this.#contextMenuItems = {
      folderItems: [openAllBookmarks, sepOpenAll, sepSort, sortByName],
      bookmarkItems: [
        openInTab,
        openInWindow,
        sepOpenOptions,
        sepEditCopy,
        copyLink,
      ],
      alwaysShownItems: [sepCutCopy, cut, copy],
      disabledForRootItems: [editBookmark, cut, deleteBookmark],
      openAllBookmarks,
      sepOpenAll,
      sortByName,
      sepSort,
      openInTab,
      openInWindow,
      sepOpenOptions,
      sepEditCopy,
      copyLink,
      sepCutCopy,
      cut,
      copy,
      openInContainerTab,
      openInPrivateWindow,
      editBookmark,
      deleteBookmark,
      showInFolder,
      sepAdd,
      addBookmark,
      addFolder,
      addSeparator,
      paste: q("#sidebar-bookmarks-context-paste"),
    };
  }

  constructor() {
    super();
    this.bookmarks = [];
    this.searchQuery = "";
    this.searchResults = [];
    this.onSearchQuery = this.onSearchQuery.bind(this);
    this.treeView = new lazy.SidebarTreeView(this);
  }

  connectedCallback() {
    super.connectedCallback();
    lazy.PlacesUtils.observers.addListener(
      this.#placesEventTypes,
      this.#onPlacesEvents
    );
    this.addContextMenuListeners();
    this.addSidebarFocusedListeners();
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    lazy.PlacesUtils.observers.removeListener(
      this.#placesEventTypes,
      this.#onPlacesEvents
    );
    this.removeContextMenuListeners();
    this.removeSidebarFocusedListeners();
  }

  async firstUpdated() {
    for (const guid of this.sidebarController._state.bookmarksExpandedFolders) {
      this.#expandedFolderGuids.add(guid);
    }
    this.bookmarks = await this.getBookmarksList();
    this.requestUpdate();
  }

  handleSidebarFocusedEvent() {
    this.searchInput?.focus();
  }

  getNodesInOrder() {
    const nodes = [];
    this.#collectNodesFromList(this.bookmarkList, nodes);
    return nodes;
  }

  #collectNodesFromList(list, nodes) {
    for (const [index, item] of list.tabItems.entries()) {
      const isFolder = Array.isArray(item.children);
      if (isFolder) {
        this.#collectNodesFromFolder(item, index, list, nodes);
      } else {
        nodes.push({
          list,
          item,
          index,
          type: item.url ? "row" : "separator",
          get domNode() {
            return list.shadowRoot.querySelector(
              `[data-guid="${CSS.escape(item.guid)}"]`
            );
          },
        });
      }
    }
  }

  #collectNodesFromFolder(folder, index, list, nodes) {
    const isExpanded = this.#expandedFolderGuids.has(folder.guid);
    if (folder.children.length) {
      nodes.push({
        list,
        item: folder,
        index,
        type: "folder",
        get domNode() {
          return list.shadowRoot.querySelector(
            `summary[data-guid="${CSS.escape(folder.guid)}"]`
          );
        },
      });
      if (isExpanded) {
        const sublist = list.findSublistForGuid(folder.guid);
        if (sublist) {
          this.#collectNodesFromList(sublist, nodes);
        }
      }
    } else {
      nodes.push({
        list,
        item: folder,
        index,
        type: "empty-folder",
        get domNode() {
          return list.shadowRoot.querySelector(
            `[data-guid="${CSS.escape(folder.guid)}"]`
          );
        },
      });
    }
  }

  setExpanded(node, expanded) {
    if (node.type === "folder") {
      const sublist = node.list?.findSublistForGuid(node.item.guid);
      const details = sublist?.closest("details");
      if (details && details.open !== expanded) {
        details.open = expanded;
        if (expanded) {
          this.#expandedFolderGuids.add(node.item.guid);
        } else {
          this.#expandedFolderGuids.delete(node.item.guid);
        }
        return true;
      }
      return false;
    }
    return super.setExpanded(node, expanded);
  }

  onPrimaryAction(e) {
    const { originalEvent } = e.detail;
    const row = e.originalTarget;
    const list = row.getRootNode().host;

    if (originalEvent.shiftKey) {
      list.dispatchEvent(
        new CustomEvent("shift-select", {
          bubbles: true,
          composed: true,
          detail: { row },
        })
      );
      return;
    }

    const anchorEvent = new CustomEvent("set-anchor", {
      bubbles: true,
      composed: true,
      detail: { guid: row.guid },
    });

    if (
      (originalEvent.type === "click" &&
        originalEvent.getModifierState("Accel")) ||
      (originalEvent.type === "keydown" && originalEvent.code === "Space")
    ) {
      list.toggleRowSelection(row.guid);
      list.dispatchEvent(anchorEvent);
      return;
    }

    this.treeView.resetSelection();
    list.dispatchEvent(anchorEvent);
    navigateToLink(e, row.url, { forceNewTab: false });
    Glean.sidebar.link.bookmarks.add(1);
  }

  handleContextMenuEvent(e) {
    this.triggerNode = this.findTriggerNode(e, "sidebar-bookmark-row");
    if (!this.triggerNode) {
      const separatorEl = this.#findSeparatorElement(e);
      if (separatorEl) {
        this.triggerNode = { guid: separatorEl.guid, isSeparator: true };
      } else {
        const folderEl = this.#findFolderElement(e);
        if (folderEl) {
          const isEmpty = folderEl.classList.contains("bookmark-folder-label");
          const title =
            folderEl.querySelector("summary")?.textContent?.trim() ??
            folderEl.textContent?.trim() ??
            "";
          const folderKind = folderEl.dataset.folderKind;
          const children = folderEl.querySelector(
            "sidebar-bookmark-list"
          )?.tabItems;
          this.triggerNode = {
            guid: folderEl.guid,
            title,
            children,
            isFolder: true,
            isEmpty,
            isRootFolder: lazy.PlacesUtils.isRootItem(folderEl.guid),
            isPlaceContainer:
              folderKind === "place-container" || folderKind === "tags-root",
            isTagContainer: folderKind === "tag-container",
            isTagsRoot: folderKind === "tags-root",
          };
        } else if (this.findTriggerNode(e, "moz-input-search")) {
          return;
        } else {
          e.preventDefault();
          return;
        }
      }
    }
    const isFolder = !!this.triggerNode.isFolder;
    const isSeparator = !!this.triggerNode.isSeparator;
    const isBookmark = !isFolder && !isSeparator;
    const isEmpty = !!this.triggerNode.isEmpty;
    const isRootFolder = !!this.triggerNode.isRootFolder;

    if (!this.#contextMenuItems) {
      this.#initContextMenuItems();
    }

    const selectedItems = this.treeView.getSelectedTabItems();
    const isMultiSelect =
      isBookmark &&
      selectedItems.length > 1 &&
      selectedItems.some(item => item.guid === this.triggerNode.guid);
    this.selectedItems = isMultiSelect ? selectedItems : null;

    if (isMultiSelect) {
      this.#configureMultiSelectContextMenu(selectedItems);
      return;
    }

    if (this.triggerNode.isPlaceContainer || this.triggerNode.isTagContainer) {
      this.#configureSmartFolderContextMenu(this.triggerNode);
      return;
    }

    const {
      folderItems,
      bookmarkItems,
      alwaysShownItems,
      disabledForRootItems,
      openAllBookmarks,
      sortByName,
      openInContainerTab,
      openInPrivateWindow,
      editBookmark,
      deleteBookmark,
      showInFolder,
      copyLink,
      sepEditCopy,
      sepAdd,
      addBookmark,
      addFolder,
      addSeparator,
      paste,
    } = this.#contextMenuItems;

    for (const el of folderItems) {
      el.hidden = !isFolder;
    }
    for (const el of bookmarkItems) {
      el.hidden = !isBookmark;
    }
    for (const el of alwaysShownItems) {
      el.hidden = false;
    }
    for (const el of disabledForRootItems) {
      el.disabled = isRootFolder;
    }

    openInContainerTab.hidden =
      !isBookmark ||
      lazy.PrivateBrowsingUtils.isWindowPrivate(this.topWindow) ||
      !Services.prefs.getBoolPref("privacy.userContext.enabled", false);
    openInPrivateWindow.hidden =
      !isBookmark || !lazy.PrivateBrowsingUtils.enabled;
    editBookmark.hidden = isSeparator;
    paste.hidden = !this.#hasClipboardData();

    const isSearchResult = isBookmark && !!this.searchQuery;
    showInFolder.hidden = !isSearchResult;
    if (isSearchResult) {
      copyLink.hidden = true;
      paste.hidden = true;
      sepEditCopy.hidden = true;
      sepAdd.hidden = true;
      addBookmark.hidden = true;
      addFolder.hidden = true;
      addSeparator.hidden = true;
    } else {
      sepAdd.hidden = false;
      addBookmark.hidden = false;
      addFolder.hidden = false;
      addSeparator.hidden = false;
    }

    if (isFolder) {
      const hasBookmarkItems = !!this.triggerNode.children?.some(
        child => child.url && !child.isPlaceContainer
      );
      openAllBookmarks.disabled = isEmpty || !hasBookmarkItems;
    } else {
      openAllBookmarks.disabled = isEmpty;
    }
    openAllBookmarks.setAttribute("data-l10n-id", "places-open-all-bookmarks");
    sortByName.disabled = isEmpty;

    let deleteLabelId;
    if (isFolder) {
      deleteLabelId = "places-delete-folder";
    } else if (isSeparator) {
      deleteLabelId = "sidebar-bookmarks-context-menu-delete-separator";
    } else {
      deleteLabelId = "sidebar-bookmarks-context-menu-delete-bookmark";
    }
    deleteBookmark.setAttribute("data-l10n-id", deleteLabelId);
    if (isFolder) {
      deleteBookmark.setAttribute(
        "data-l10n-args",
        JSON.stringify({ count: 1 })
      );
    } else {
      deleteBookmark.removeAttribute("data-l10n-args");
    }
    editBookmark.setAttribute(
      "data-l10n-id",
      isFolder
        ? "places-edit-folder2"
        : "sidebar-bookmarks-context-menu-edit-bookmark"
    );
  }

  #configureSmartFolderContextMenu(node) {
    const isPlaceContainer = !!node.isPlaceContainer;
    const isTagContainer = !!node.isTagContainer;
    const isTagsRoot = !!node.isTagsRoot;
    const {
      openAllBookmarks,
      sepOpenAll,
      sortByName,
      sepSort,
      openInTab,
      openInWindow,
      sepOpenOptions,
      sepEditCopy,
      copyLink,
      sepCutCopy,
      cut,
      copy,
      openInContainerTab,
      openInPrivateWindow,
      editBookmark,
      deleteBookmark,
      showInFolder,
      sepAdd,
      addBookmark,
      addFolder,
      addSeparator,
      paste,
    } = this.#contextMenuItems;

    openAllBookmarks.hidden = false;
    openAllBookmarks.disabled = isTagsRoot || !!node.isEmpty;
    openAllBookmarks.setAttribute(
      "data-l10n-id",
      isTagContainer ? "places-open-all-bookmarks" : "places-open-all-in-tabs"
    );
    sepOpenAll.hidden = false;

    openInTab.hidden = true;
    openInContainerTab.hidden = true;
    openInWindow.hidden = true;
    openInPrivateWindow.hidden = true;
    sepOpenOptions.hidden = true;
    showInFolder.hidden = true;

    editBookmark.hidden = false;
    editBookmark.disabled = isPlaceContainer;
    editBookmark.setAttribute("data-l10n-id", "places-edit-generic");

    deleteBookmark.hidden = false;
    deleteBookmark.setAttribute("data-l10n-id", "text-action-delete");
    deleteBookmark.removeAttribute("data-l10n-args");

    sepSort.hidden = true;
    sortByName.hidden = true;

    sepCutCopy.hidden = false;
    cut.hidden = isTagContainer;
    copy.hidden = false;
    paste.hidden = true;

    sepEditCopy.hidden = true;
    copyLink.hidden = true;

    sepAdd.hidden = true;
    addBookmark.hidden = true;
    addFolder.hidden = true;
    addSeparator.hidden = true;
  }

  #configureMultiSelectContextMenu(selectedItems) {
    const {
      openAllBookmarks,
      sepOpenAll,
      sortByName,
      sepSort,
      openInTab,
      openInWindow,
      sepOpenOptions,
      sepEditCopy,
      copyLink,
      sepCutCopy,
      cut,
      copy,
      openInContainerTab,
      openInPrivateWindow,
      editBookmark,
      deleteBookmark,
      showInFolder,
      paste,
    } = this.#contextMenuItems;

    openAllBookmarks.hidden = false;
    openAllBookmarks.disabled = false;
    openAllBookmarks.setAttribute("data-l10n-id", "places-open-all-bookmarks");
    sepOpenAll.hidden = false;

    openInTab.hidden = true;
    openInContainerTab.hidden = true;
    openInWindow.hidden = true;
    openInPrivateWindow.hidden = true;
    sepOpenOptions.hidden = true;
    showInFolder.hidden = true;

    editBookmark.hidden = false;
    editBookmark.disabled = true;
    editBookmark.setAttribute(
      "data-l10n-id",
      "sidebar-bookmarks-context-menu-edit-bookmark"
    );

    deleteBookmark.hidden = false;
    deleteBookmark.setAttribute("data-l10n-id", "places-delete-bookmark");
    deleteBookmark.setAttribute(
      "data-l10n-args",
      JSON.stringify({ count: selectedItems.length })
    );

    sepSort.hidden = true;
    sortByName.hidden = true;

    sepCutCopy.hidden = false;
    cut.hidden = false;
    copy.hidden = false;
    paste.hidden = !this.#hasClipboardData();

    sepEditCopy.hidden = true;
    copyLink.hidden = true;
  }

  #findSeparatorElement(e) {
    const candidates = [
      e.explicitOriginalTarget,
      e.originalTarget.flattenedTreeParentNode,
      e.explicitOriginalTarget.flattenedTreeParentNode?.getRootNode().host,
      e.originalTarget.flattenedTreeParentNode?.getRootNode().host,
    ];
    for (const el of candidates) {
      if (el?.classList?.contains("bookmark-separator")) {
        return el;
      }
    }
    return null;
  }

  #findFolderElement(e) {
    const candidates = [
      e.explicitOriginalTarget,
      e.originalTarget.flattenedTreeParentNode,
      e.explicitOriginalTarget.flattenedTreeParentNode?.getRootNode().host,
      e.originalTarget.flattenedTreeParentNode?.getRootNode().host,
    ];
    for (const el of candidates) {
      if (el?.localName === "summary") {
        const details = el.parentElement;
        if (details?.guid) {
          return details;
        }
      }
      if (el?.localName === "details" && el.guid) {
        return el;
      }
    }
    for (const el of e.composedPath()) {
      if (el?.classList?.contains("bookmark-folder-label")) {
        return el;
      }
    }
    return null;
  }

  handleCommandEvent(e) {
    if (e.target.hasAttribute("data-usercontextid")) {
      const userContextId = parseInt(
        e.target.getAttribute("data-usercontextid")
      );
      this.topWindow.openTrustedLinkIn(this.triggerNode.url, "tab", {
        userContextId,
        eventDetail: { containerSource: "sidebar_bookmarks_context_menu" },
      });
      Glean.browserUiInteraction.sidebarBookmarks.open_in_new_container_tab.add(
        1
      );
      return;
    }
    let label;
    switch (e.target.id) {
      case "sidebar-bookmarks-context-open-all-bookmarks":
        this.#openBookmarks(this.selectedItems ?? [this.triggerNode]);
        break;
      case "sidebar-bookmarks-context-sort-by-name":
        this.#sortByName();
        label = "sort_bookmarks_by_name";
        break;
      case "sidebar-bookmarks-context-open-in-tab":
        this.topWindow.openTrustedLinkIn(this.triggerNode.url, "tab");
        label = "open_in_new_tab";
        break;
      case "sidebar-bookmarks-context-open-in-window":
        this.topWindow.openTrustedLinkIn(this.triggerNode.url, "window", {
          private: false,
        });
        label = "open_in_new_window";
        break;
      case "sidebar-bookmarks-context-open-in-private-window":
        this.topWindow.openTrustedLinkIn(this.triggerNode.url, "window", {
          private: true,
        });
        label = "open_in_private_window";
        break;
      case "sidebar-bookmarks-context-edit-bookmark":
        this.#editBookmarkOrFolder(this.triggerNode);
        break;
      case "sidebar-bookmarks-context-delete-bookmark":
        this.#deleteBookmarks(this.selectedItems ?? [this.triggerNode]);
        break;
      case "sidebar-bookmarks-context-show-in-folder":
        this.showInFolder(this.triggerNode.guid).catch(console.error);
        break;
      case "sidebar-bookmarks-context-copy-link":
        lazy.BrowserUtils.copyLink(
          this.triggerNode.url,
          this.triggerNode.title
        );
        label = "copy_bookmark_url";
        break;
      case "sidebar-bookmarks-context-add-bookmark":
        this.#addItem("bookmark");
        break;
      case "sidebar-bookmarks-context-add-folder":
        this.#addItem("folder");
        break;
      case "sidebar-bookmarks-context-add-separator":
        this.#addSeparator();
        label = "add_separator";
        break;
      case "sidebar-bookmarks-context-cut":
        this.#cutBookmarks(this.selectedItems ?? [this.triggerNode]);
        label = "cut_bookmark";
        break;
      case "sidebar-bookmarks-context-copy":
        this.#copyBookmarks(this.selectedItems ?? [this.triggerNode]);
        break;
      case "sidebar-bookmarks-context-paste":
        this.#paste();
        break;
    }
    if (label) {
      Glean.browserUiInteraction.sidebarBookmarks[label].add(1);
    }
  }

  onSecondaryAction(e) {
    this.triggerNode = e.detail.item;
    this.#deleteBookmarks([this.triggerNode]);
  }

  async #editBookmarkOrFolder(bookmark) {
    if (bookmark.isRootFolder) {
      throw new Error("It's not possible to edit Places root folders.");
    }
    const fetchInfo = await lazy.PlacesUtils.bookmarks.fetch({
      guid: bookmark.guid,
    });
    if (!fetchInfo) {
      return;
    }
    const node =
      await lazy.PlacesUIUtils.promiseNodeLikeFromFetchInfo(fetchInfo);
    const guid = await lazy.PlacesUIUtils.showBookmarkDialog(
      { action: "edit", node },
      this.topWindow
    );
    const outcome = guid ? "confirmed" : "cancelled";
    const labelPrefix = bookmark.isFolder
      ? "rename_bookmark_folder"
      : "edit_bookmark";
    Glean.browserUiInteraction.sidebarBookmarks[
      `${labelPrefix}_${outcome}`
    ].add(1);
  }

  async #deleteBookmarks(bookmarks) {
    if (bookmarks.some(({ isRootFolder }) => isRootFolder)) {
      throw new Error("It's not possible to delete Places root folders.");
    }
    await lazy.PlacesTransactions.Remove({
      guids: bookmarks.map(b => b.guid),
    }).transact();
  }

  async showInFolder(guid) {
    this.searchQuery = "";
    this.searchResults = [];
    if (this.searchInput) {
      this.searchInput.value = "";
    }

    const fetchInfo = await lazy.PlacesUtils.bookmarks.fetch({ guid }, null, {
      includePath: true,
    });
    if (!fetchInfo) {
      return;
    }
    for (const ancestor of fetchInfo.path ?? []) {
      this.#expandedFolderGuids.add(ancestor.guid);
    }
    this.#expandedFolderGuids.add(fetchInfo.parentGuid);
    this.sidebarController._state.bookmarksExpandedFolders = [
      ...this.#expandedFolderGuids,
    ];

    // #expandedFolderGuids is mutated in place, so Lit can't detect the
    // change; request an update explicitly so the tree re-renders with the
    // ancestor folders expanded before we scroll to the row.
    this.requestUpdate();
    await this.updateComplete;
    await this.#renderAncestorChain(fetchInfo, guid);
    await this.#scrollAndFocusBookmarkRow(guid);
  }

  /**
   * Walk from the root list down to the target's parent, forcing each list to
   * render the virtual-list chunk holding the next folder in the path. Without
   * this, a folder or row far enough down a long list never renders and can't
   * be found, selected or scrolled to.
   *
   * @param {object} fetchInfo A Places fetch result with `path` included.
   * @param {string} guid The guid of the bookmark being revealed.
   */
  async #renderAncestorChain(fetchInfo, guid) {
    // `path` runs from a folder the sidebar shows at top level down to and
    // including the bookmark's parent.
    let list = this.bookmarkList;
    for (const ancestor of fetchInfo.path ?? []) {
      // A folder Places reports can be missing from the rendered tree (see
      // renderItemForGuid), leaving nothing deeper to descend into.
      if (!(await list?.renderItemForGuid(ancestor.guid))) {
        return;
      }
      list = list.findSublistForGuid(ancestor.guid);
    }
    await list?.renderItemForGuid(guid);
  }

  async #scrollAndFocusBookmarkRow(guid) {
    const findRow = list => {
      if (!list) {
        return null;
      }
      for (const row of list.rowEls ?? []) {
        if (row.guid === guid) {
          return { row, list };
        }
      }
      for (const details of list.folderEls ?? []) {
        const sublist = details.querySelector("sidebar-bookmark-list");
        const found = findRow(sublist);
        if (found) {
          return found;
        }
      }
      return null;
    };

    const found = await this.#waitForElement(() => findRow(this.bookmarkList));
    if (!found) {
      return;
    }

    const { row, list } = found;
    this.treeView.resetSelection();
    this.treeView.selectRowInList(list, row.guid);
    await list.requestVirtualListUpdate();
    row.scrollIntoView({ block: "nearest" });
    row.mainEl?.focus?.();
  }

  async #waitForElement(probe, { maxFrames = 60 } = {}) {
    for (let i = 0; i < maxFrames; i++) {
      const found = probe();
      if (found) {
        return found;
      }
      await this.bookmarkList?.updateComplete;
      await new Promise(resolve =>
        this.documentGlobal.requestAnimationFrame(resolve)
      );
    }
    return probe();
  }

  async #addItem(type) {
    const dialogInfo = { action: "add", type };
    const defaultInsertionPoint = await this.#getInsertionPoint();
    if (defaultInsertionPoint) {
      dialogInfo.defaultInsertionPoint = defaultInsertionPoint;
      // The folder picker ignores the insertion index, so hide it to honor the
      // position derived from the right-clicked node.
      dialogInfo.hiddenRows = ["folderPicker"];
    }
    const guid = await lazy.PlacesUIUtils.showBookmarkDialog(
      dialogInfo,
      this.topWindow
    );
    const outcome = guid ? "confirmed" : "cancelled";
    const label =
      type === "folder"
        ? `add_bookmark_folder_${outcome}`
        : `add_bookmark_${outcome}`;
    Glean.browserUiInteraction.sidebarBookmarks[label].add(1);
  }

  /**
   * Builds the insertion point for a new item added from the context menu,
   * based on the right-clicked node. Right-clicking a folder inserts at the
   * end of that folder, while right-clicking a bookmark or separator inserts
   * just before it. Returns undefined when the right-clicked bookmark can no
   * longer be fetched (e.g. it was removed between opening the menu and
   * confirming the dialog), so the dialog falls back to its default parent.
   *
   * @returns {Promise<?{guid: string, getIndex: function(): number}>}
   */
  async #getInsertionPoint() {
    const node = this.triggerNode;
    if (node.isFolder) {
      return {
        guid: node.guid,
        getIndex: () => lazy.PlacesUtils.bookmarks.DEFAULT_INDEX,
      };
    }
    const fetchInfo = await lazy.PlacesUtils.bookmarks.fetch({
      guid: node.guid,
    });
    if (!fetchInfo) {
      return undefined;
    }
    return {
      guid: fetchInfo.parentGuid,
      getIndex: () => fetchInfo.index,
    };
  }

  async #addSeparator() {
    const fetchInfo = await lazy.PlacesUtils.bookmarks.fetch({
      guid: this.triggerNode.guid,
    });
    if (!fetchInfo) {
      return;
    }
    await lazy.PlacesTransactions.NewSeparator({
      parentGuid: fetchInfo.parentGuid,
      index: fetchInfo.index,
    }).transact();
  }

  async #openBookmarks(bookmarks) {
    const urls = [];
    for (const item of bookmarks) {
      if (item.isFolder) {
        const tree = await lazy.PlacesUtils.promiseBookmarksTree(item.guid);
        urls.push(...this.#collectBookmarkUrls(tree));
      } else if (item.url) {
        urls.push(item.url);
      }
    }
    if (!lazy.OpenInTabsUtils.confirmOpenInTabs(urls.length, this.topWindow)) {
      return;
    }
    Glean.browserUiInteraction.sidebarBookmarks.open_all_bookmarks.add(1);
    for (const url of urls) {
      this.topWindow.openTrustedLinkIn(url, "tab", { inBackground: true });
    }
  }

  #collectBookmarkUrls(node) {
    const urls = [];
    for (const child of node.children ?? []) {
      if (child.uri) {
        urls.push(child.uri);
      }
    }
    return urls;
  }

  async #sortByName() {
    await lazy.PlacesTransactions.SortByName(this.triggerNode.guid).transact();
  }

  #cutBookmarks(bookmarks) {
    this.#copyBookmarksToClipboard(bookmarks, "cut");
  }

  #copyBookmarks(bookmarks) {
    this.#copyBookmarksToClipboard(bookmarks, "copy");
  }

  /**
   * Builds and passes text/x-moz-place, text/x-moz-url and text/plain flavors
   * to the clipboard based on selected bookmark nodes and their data.
   *
   * Flavors:
   * - text/x-moz-place: comma-separated JSON objects, one per bookmark
   * - text/x-moz-url: bookmark URL + newline + title, one per bookmark
   * - text/plain: bookmark URL as raw text, one per bookmark
   *
   * @param {Array} bookmarks array of sidebar-bookmark-row nodes
   * @param {"copy"|"cut"} action type of action taken (cut or copy)
   */
  #copyBookmarksToClipboard(bookmarks, action) {
    const xMozPlaceEntries = bookmarks.map(item => {
      if (item.isSeparator) {
        return JSON.stringify({
          type: lazy.PlacesUtils.TYPE_X_MOZ_PLACE_SEPARATOR,
        });
      }
      if (item.isFolder) {
        if (lazy.PlacesUtils.isRootItem(item.guid)) {
          return JSON.stringify(
            lazy.PlacesUtils.bookmarks.createVirtualLinkToRoot(item)
          );
        }
        return JSON.stringify({
          type: lazy.PlacesUtils.TYPE_X_MOZ_PLACE_CONTAINER,
          itemGuid: item.guid,
          instanceId: lazy.PlacesUtils.instanceId,
          title: item.title,
        });
      }
      return JSON.stringify({
        type: lazy.PlacesUtils.TYPE_X_MOZ_PLACE,
        itemGuid: item.guid,
        instanceId: lazy.PlacesUtils.instanceId,
        title: item.title,
        uri: item.url,
      });
    });

    const xMozPlaceFlavor = xMozPlaceEntries.join(",");

    const uriItems = bookmarks.filter(item => item.url);
    const xMozUrlFlavor = uriItems
      .map(item => `${item.url}${lazy.PlacesUtils.endl}${item.title ?? ""}`)
      .join(lazy.PlacesUtils.endl);
    const plainTextFlavor = uriItems
      .map(item => item.url)
      .join(lazy.PlacesUtils.endl);

    const flavors = new Map([
      [lazy.PlacesUtils.TYPE_X_MOZ_PLACE, xMozPlaceFlavor],
    ]);

    if (uriItems.length) {
      flavors.set(lazy.PlacesUtils.TYPE_X_MOZ_URL, xMozUrlFlavor);
      flavors.set(lazy.PlacesUtils.TYPE_PLAINTEXT, plainTextFlavor);
    }

    this.#setClipboard(flavors, action);
  }

  #setClipboard(flavors, action) {
    const xferable = Cc["@mozilla.org/widget/transferable;1"].createInstance(
      Ci.nsITransferable
    );
    xferable.init(null);
    function toISupports(str) {
      const s = Cc["@mozilla.org/supports-string;1"].createInstance(
        Ci.nsISupportsString
      );
      s.data = str;
      return s;
    }
    for (const [type, data] of flavors) {
      xferable.addDataFlavor(type);
      xferable.setTransferData(type, toISupports(data));
    }
    xferable.addDataFlavor(lazy.PlacesUtils.TYPE_X_MOZ_PLACE_ACTION);
    xferable.setTransferData(
      lazy.PlacesUtils.TYPE_X_MOZ_PLACE_ACTION,
      toISupports(action + "," + Services.appinfo.name)
    );
    Services.clipboard.setData(
      xferable,
      null,
      Ci.nsIClipboard.kGlobalClipboard
    );
  }

  #hasClipboardData() {
    return Services.clipboard.hasDataMatchingFlavors(
      [
        lazy.PlacesUtils.TYPE_X_MOZ_PLACE,
        lazy.PlacesUtils.TYPE_X_MOZ_URL,
        lazy.PlacesUtils.TYPE_PLAINTEXT,
      ],
      Ci.nsIClipboard.kGlobalClipboard
    );
  }

  async #paste() {
    const fetchInfo = await lazy.PlacesUtils.bookmarks.fetch({
      guid: this.triggerNode.guid,
    });
    if (!fetchInfo) {
      return;
    }
    const xferable = Cc["@mozilla.org/widget/transferable;1"].createInstance(
      Ci.nsITransferable
    );
    xferable.init(null);
    [
      lazy.PlacesUtils.TYPE_X_MOZ_PLACE,
      lazy.PlacesUtils.TYPE_X_MOZ_URL,
      lazy.PlacesUtils.TYPE_PLAINTEXT,
    ].forEach(type => xferable.addDataFlavor(type));
    Services.clipboard.getData(xferable, Ci.nsIClipboard.kGlobalClipboard);
    let data = {};
    let type = {};
    try {
      xferable.getAnyTransferData(type, data);
    } catch (e) {
      return;
    }
    let isCut = false;
    try {
      const actionXferable = Cc[
        "@mozilla.org/widget/transferable;1"
      ].createInstance(Ci.nsITransferable);
      actionXferable.init(null);
      actionXferable.addDataFlavor(lazy.PlacesUtils.TYPE_X_MOZ_PLACE_ACTION);
      Services.clipboard.getData(
        actionXferable,
        Ci.nsIClipboard.kGlobalClipboard
      );
      let actionValue = {};
      actionXferable.getTransferData(
        lazy.PlacesUtils.TYPE_X_MOZ_PLACE_ACTION,
        actionValue
      );
      const [clipAction] = actionValue.value
        .QueryInterface(Ci.nsISupportsString)
        .data.split(",");
      isCut = clipAction === "cut";
    } catch (e) {
      // Default to copy
    }
    let validNodes;
    try {
      ({ validNodes } = lazy.PlacesUtils.unwrapNodes(
        data.value.QueryInterface(Ci.nsISupportsString).data,
        type.value
      ));
    } catch (e) {
      return;
    }
    if (!validNodes.length) {
      return;
    }
    // When the target is a folder, paste inside it and append at the end.
    // Otherwise, paste as a sibling right after the target bookmark.
    const insertionPoint = this.triggerNode.isFolder
      ? {
          guid: this.triggerNode.guid,
          isTag: false,
          getIndex: async () => lazy.PlacesUtils.bookmarks.DEFAULT_INDEX,
        }
      : {
          guid: fetchInfo.parentGuid,
          isTag: false,
          getIndex: async () => fetchInfo.index + 1,
        };
    await lazy.PlacesUIUtils.handleTransferItems(
      validNodes,
      insertionPoint,
      !isCut,
      null
    );
    if (isCut) {
      Services.clipboard.emptyClipboard(Ci.nsIClipboard.kGlobalClipboard);
    }
  }

  #onFolderToggle(e) {
    const { guid, open: isOpen } = e.detail;
    if (isOpen) {
      this.#expandedFolderGuids.add(guid);
    } else {
      this.#expandedFolderGuids.delete(guid);
    }
    this.sidebarController._state.bookmarksExpandedFolders = [
      ...this.#expandedFolderGuids,
    ];
  }

  onSearchQuery(e) {
    this.searchQuery = e.detail.query;
    this.searchResults = this.searchQuery
      ? this.#searchBookmarks(this.bookmarks, this.searchQuery.toLowerCase())
      : [];
    if (this.searchQuery) {
      Glean.browserUiInteraction.sidebarBookmarks.search.add(1);
    }
    this.treeView.resetActiveNode();
  }

  #searchBookmarks(node, query) {
    const results = [];
    for (const child of node.children ?? []) {
      // Smart bookmark (place:) results duplicate real bookmarks or include
      // history entries, so don't search inside them.
      if (child.isPlaceContainer) {
        continue;
      }
      if (child.children) {
        results.push(...this.#searchBookmarks(child, query));
      } else if (
        child.title?.toLowerCase().includes(query) ||
        child.url?.toLowerCase().includes(query)
      ) {
        results.push(child);
      }
    }
    return results;
  }

  bookmarkItemTemplate = bookmark => {
    if (bookmark.children) {
      return html`
        ${when(
          lazy.virtualListEnabledPref,
          () => html`
            <details>
              <summary part="summary">${bookmark.title}</summary>
              <div id="content">
                <virtual-list
                  .activeIndex=${0}
                  .items=${bookmark.children}
                  .template=${this.bookmarkItemTemplate}
                ></virtual-list>
              </div>
            </details>
          `,
          () =>
            html`${this.getBookmarkList.map(bookmarkItem =>
              this.bookmarkItemTemplate(bookmarkItem)
            )}`
        )}
      `;
    }

    return html`
      <div class="bookmark-item">
        <span>${bookmark.title}</span>
      </div>
    `;
  };

  async getBookmarksList() {
    const tree = await lazy.PlacesUtils.promiseBookmarksTree("root________");
    const { bookmarks } = lazy.PlacesUtils;
    const guidToL10nId = {
      [bookmarks.menuGuid]: "sidebar-bookmarks-folder-menu",
      [bookmarks.toolbarGuid]: "sidebar-bookmarks-folder-toolbar",
      [bookmarks.unfiledGuid]: "sidebar-bookmarks-folder-other",
      [bookmarks.mobileGuid]: "sidebar-bookmarks-folder-mobile",
    };
    await this.#normalizeBookmarkNode(tree, guidToL10nId);
    tree.children?.sort((a, b) => {
      if (a.guid === bookmarks.toolbarGuid) {
        return -1;
      }
      if (b.guid === bookmarks.toolbarGuid) {
        return 1;
      }
      return 0;
    });
    return tree;
  }

  async #normalizeBookmarkNode(node, guidToL10nId) {
    if (node.iconUri && !node.iconUri.startsWith("fake-favicon-uri:")) {
      node.icon = node.iconUri;
    } else if (node.uri) {
      node.icon = `page-icon:${node.uri}`;
    }
    if (node.uri) {
      node.url = node.uri;
    }
    if (node.type === lazy.PlacesUtils.TYPE_X_MOZ_PLACE_CONTAINER) {
      node.children ??= [];
    } else if (
      node.type === lazy.PlacesUtils.TYPE_X_MOZ_PLACE &&
      node.uri?.startsWith("place:")
    ) {
      await this.#expandPlaceQuery(node);
    }
    const l10nId = guidToL10nId?.[node.guid];
    if (l10nId) {
      const [msg] = bookmarkFolderLocalization.formatMessagesSync([
        { id: l10nId },
      ]);
      node.title = msg.value;
    }
    // allSettled so that a single child failing to normalize (e.g. an
    // unexpected rejection while expanding a nested query) doesn't discard the
    // whole tree; the worst case is an incomplete branch.
    await Promise.allSettled(
      (node.children ?? []).map(child =>
        this.#normalizeBookmarkNode(child, guidToL10nId)
      )
    );
  }

  /**
   * Execute a smart-bookmark (place:) query off the main thread and attach its
   * results as children, so it renders as a folder like the legacy bookmarks
   * sidebar.
   *
   * Uses asyncExecuteLegacyQuery to avoid main-thread I/O. That API delivers
   * flat result rows rather than a node tree, so the container/leaf structure
   * is rebuilt here (see #runPlaceQueryAsync / #convertPlaceRow).
   *
   * @param {object} node
   *   The bookmark tree node whose `uri` is a `place:` query.
   */
  async #expandPlaceQuery(node) {
    const queryRef = {};
    const optionsRef = {};
    try {
      lazy.PlacesUtils.history.queryStringToQuery(
        node.uri,
        queryRef,
        optionsRef
      );
    } catch (e) {
      return;
    }
    node.isPlaceContainer = true;
    node.isTagsRoot =
      optionsRef.value.resultType ===
      Ci.nsINavHistoryQueryOptions.RESULTS_AS_TAGS_ROOT;
    // Seed the loop guard with the target folder of a simple folder query, so
    // children that link back to it don't recurse infinitely.
    const targetGuid = this.#simpleFolderTargetGuid(
      queryRef.value,
      optionsRef.value
    );
    node.isTagContainer =
      !targetGuid &&
      (optionsRef.value.resultType ===
        Ci.nsINavHistoryQueryOptions.RESULTS_AS_TAGS_ROOT ||
        queryRef.value.tags?.length === 1);
    const ancestorKeys = targetGuid ? new Set([targetGuid]) : new Set();
    try {
      node.children = await this.#runPlaceQueryAsync(
        queryRef.value,
        optionsRef.value,
        ancestorKeys
      );
    } catch (e) {
      node.children = [];
    }
  }

  /**
   * Run a place: query off the main thread and build child nodes from the flat
   * result rows, recursing into nested query/folder containers.
   *
   * @param {nsINavHistoryQuery} query
   * @param {nsINavHistoryQueryOptions} options
   * @param {Set<string>} ancestorKeys
   *   Guids (or uris, for keyless rows) of ancestor containers, used to break
   *   query loops.
   * @returns {Promise<object[]>}
   */
  #runPlaceQueryAsync(query, options, ancestorKeys) {
    return new Promise((resolve, reject) => {
      const entries = [];
      lazy.PlacesUtils.history.asyncExecuteLegacyQuery(query, options, {
        handleResult: resultSet => {
          for (let row; (row = resultSet.getNextRow()); ) {
            const entry = this.#convertPlaceRow(row, options, ancestorKeys);
            if (entry) {
              entries.push(entry);
            }
          }
        },
        handleError: error => {
          reject(
            new Error(
              `Async place query error (${error.result}): ${error.message}`
            )
          );
        },
        handleCompletion: () => resolve(entries),
      });
    }).then(async entries => {
      // Recurse only after each level's rows are drained, so the storage
      // callbacks never block on a nested async query.
      await Promise.all(
        entries
          .filter(entry => entry.recurse)
          .map(async entry => {
            entry.node.children = await this.#runPlaceQueryAsync(
              entry.recurse.query,
              entry.recurse.options,
              entry.recurse.ancestorKeys
            );
          })
      );
      return entries.map(entry => entry.node);
    });
  }

  /**
   * Convert a flat asyncExecuteLegacyQuery result row into a bookmark tree
   * node, classifying place: children as leaf URIs or as containers to recurse
   * into (queries, folders, and folder shortcuts).
   *
   * @param {mozIStorageRow} row
   * @param {nsINavHistoryQueryOptions} parentOptions
   *   Options of the query that produced this row, used to detect tag
   *   containers under a tags-root query.
   * @param {Set<string>} ancestorKeys
   * @returns {?{node: object, recurse: ?object}}
   */
  #convertPlaceRow(row, parentOptions, ancestorKeys) {
    // Column indices match the SELECT in nsNavHistory::ConstructQueryString
    // (the kGetInfoIndex_* constants in nsNavHistory.cpp): 1 = URL, 2 = title,
    // 14 = page guid. Bookmark queries append further columns, with the
    // bookmark guid (b.guid) at 18; it's null for history queries, so fall
    // back to the page guid to mirror the old bookmarkGuid || pageGuid.
    const url = row.getResultByIndex(1);
    const title = row.getResultByIndex(2);
    const guid = row.getResultByIndex(18) || row.getResultByIndex(14);

    if (!url?.startsWith("place:")) {
      return {
        node: {
          title,
          uri: url,
          guid,
          type: lazy.PlacesUtils.TYPE_X_MOZ_PLACE,
          isPlaceChild: true,
        },
        recurse: null,
      };
    }

    const queryRef = {};
    const optionsRef = {};
    try {
      lazy.PlacesUtils.history.queryStringToQuery(url, queryRef, optionsRef);
    } catch (e) {
      // Unparseable nested query: surface it as an empty, read-only container
      // rather than dropping it. Keeping it a container (with children already
      // set) means normalizeBookmarkNode won't try to expand it again.
      return {
        node: {
          title,
          uri: url,
          guid: guid || url,
          type: lazy.PlacesUtils.TYPE_X_MOZ_PLACE_CONTAINER,
          isPlaceContainer: true,
          isPlaceChild: true,
          children: [],
        },
        recurse: null,
      };
    }

    const isTagContainer =
      !this.#simpleFolderTargetGuid(queryRef.value, optionsRef.value) &&
      (parentOptions.resultType ===
        Ci.nsINavHistoryQueryOptions.RESULTS_AS_TAGS_ROOT ||
        queryRef.value.tags?.length === 1);
    const key = guid || url;
    const node = {
      title,
      uri: url,
      guid: key,
      type: lazy.PlacesUtils.TYPE_X_MOZ_PLACE_CONTAINER,
      isPlaceContainer: true,
      isPlaceChild: true,
      isTagContainer,
    };
    // Guard against query results or folder shortcuts that loop back into an
    // ancestor.
    const targetGuid = this.#simpleFolderTargetGuid(
      queryRef.value,
      optionsRef.value
    );
    if (ancestorKeys.has(key) || (targetGuid && ancestorKeys.has(targetGuid))) {
      node.children = [];
      return { node, recurse: null };
    }
    const nextKeys = new Set(ancestorKeys).add(key);
    if (targetGuid) {
      nextKeys.add(targetGuid);
    }
    return {
      node,
      recurse: {
        query: queryRef.value,
        options: optionsRef.value,
        ancestorKeys: nextKeys,
      },
    };
  }

  /**
   * Return the concrete target folder guid of a simple bookmarks folder query
   * (or folder shortcut), or null if it isn't one. Mirrors
   * GetSimpleBookmarksQueryParent in nsNavHistory.cpp; the flat result rows
   * don't carry the node type, so this is derived from the query parameters.
   *
   * @param {nsINavHistoryQuery} query
   * @param {nsINavHistoryQueryOptions} options
   * @returns {?string}
   */
  #simpleFolderTargetGuid(query, options) {
    if (query.parentCount !== 1) {
      return null;
    }
    if (
      query.hasBeginTime ||
      query.hasEndTime ||
      query.hasDomain ||
      query.hasUri ||
      query.hasSearchTerms ||
      query.tags?.length > 0 ||
      options.maxResults > 0
    ) {
      return null;
    }
    const parentGuid = query.getParents()[0];
    return lazy.PlacesUtils.isValidGuid(parentGuid) ? parentGuid : null;
  }

  #searchResultsTemplate() {
    return html`
      <h3
        data-l10n-id="firefoxview-search-results-header"
        data-l10n-args=${JSON.stringify({
          query: escapeHtmlEntities(this.searchQuery),
        })}
      ></h3>
      <span
        data-l10n-id="firefoxview-search-results-count"
        data-l10n-args=${JSON.stringify({ count: this.searchResults.length })}
      ></span>
      <sidebar-bookmark-list
        maxTabsLength="-1"
        secondaryActionClass="delete-button"
        .tabItems=${this.searchResults}
        @fxview-tab-list-primary-action=${this.onPrimaryAction}
        @fxview-tab-list-secondary-action=${this.onSecondaryAction}
        @fxview-tab-list-middleclick-action=${this.onPrimaryAction}
      ></sidebar-bookmark-list>
    `;
  }

  render() {
    return html`
      ${this.stylesheet()}
      <div class="sidebar-panel">
        <div class="options-container">
          <moz-input-search
            data-l10n-id="firefoxview-search-text-box-bookmarks"
            data-l10n-attrs="placeholder"
            @MozInputSearch:search=${this.onSearchQuery}
          ></moz-input-search>
        </div>
        <div class="sidebar-panel-scrollable-content" tabindex="-1">
          ${when(
            this.searchQuery,
            () => this.#searchResultsTemplate(),
            () =>
              html`<sidebar-bookmark-list
                maxTabsLength="-1"
                secondaryActionClass="delete-button"
                .tabItems=${this.bookmarks.children?.filter(
                  b =>
                    b.children &&
                    (b.children.length ||
                      b.guid !== lazy.PlacesUtils.bookmarks.mobileGuid)
                ) ?? []}
                .expandedFolderGuids=${this.#expandedFolderGuids}
                @fxview-tab-list-primary-action=${this.onPrimaryAction}
                @fxview-tab-list-secondary-action=${this.onSecondaryAction}
                @fxview-tab-list-middleclick-action=${this.onPrimaryAction}
                @folder-toggle=${this.#onFolderToggle}
                @bookmark-folder-middleclick=${({ detail }) =>
                  this.#openBookmarks([detail])}
              ></sidebar-bookmark-list>`
          )}
        </div>
      </div>
    `;
  }
}

customElements.define("sidebar-bookmarks", SidebarBookmarks);
