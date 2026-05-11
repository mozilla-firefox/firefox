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
// eslint-disable-next-line import/no-unassigned-import
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
    const sortByName = q("#sidebar-bookmarks-context-sort-by-name");
    const openInContainerTab = q(
      "#sidebar-bookmarks-context-open-in-container-tab"
    );
    const openInPrivateWindow = q(
      "#sidebar-bookmarks-context-open-in-private-window"
    );
    const editBookmark = q("#sidebar-bookmarks-context-edit-bookmark");
    const deleteBookmark = q("#sidebar-bookmarks-context-delete-bookmark");
    this.#contextMenuItems = {
      folderItems: [
        openAllBookmarks,
        q("#sidebar-bookmarks-context-sep-open-all"),
        q("#sidebar-bookmarks-context-sep-sort"),
        sortByName,
      ],
      bookmarkItems: [
        q("#sidebar-bookmarks-context-open-in-tab"),
        q("#sidebar-bookmarks-context-open-in-window"),
        q("#sidebar-bookmarks-context-sep-open-options"),
        q("#sidebar-bookmarks-context-sep-edit-copy"),
        q("#sidebar-bookmarks-context-copy-link"),
      ],
      alwaysShownItems: [
        q("#sidebar-bookmarks-context-sep-cut-copy"),
        q("#sidebar-bookmarks-context-cut"),
        q("#sidebar-bookmarks-context-copy"),
      ],
      openAllBookmarks,
      sortByName,
      openInContainerTab,
      openInPrivateWindow,
      editBookmark,
      deleteBookmark,
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
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    lazy.PlacesUtils.observers.removeListener(
      this.#placesEventTypes,
      this.#onPlacesEvents
    );
    this.removeContextMenuListeners();
  }

  async firstUpdated() {
    for (const guid of this.sidebarController._state.bookmarksExpandedFolders) {
      this.#expandedFolderGuids.add(guid);
    }
    this.bookmarks = await this.getBookmarksList();
    this.requestUpdate();
  }

  getRowsInOrder() {
    return this.#getRowsForList(this.bookmarkList);
  }

  #getRowsForList(list) {
    let rows = [];
    for (const item of list.tabItems) {
      const isFolder = Array.isArray(item.children);
      if (!isFolder) {
        rows.push({ list, item });
      } else if (
        item.children.length &&
        this.#expandedFolderGuids.has(item.guid)
      ) {
        const sublist = list.findSublistForGuid(item.guid);
        if (sublist) {
          rows = rows.concat(this.#getRowsForList(sublist));
        }
      }
    }
    return rows;
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
          this.triggerNode = {
            guid: folderEl.guid,
            title,
            isFolder: true,
            isEmpty,
            isRootFolder: lazy.PlacesUtils.isRootItem(folderEl.guid),
          };
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
    const {
      folderItems,
      bookmarkItems,
      alwaysShownItems,
      openAllBookmarks,
      sortByName,
      openInContainerTab,
      openInPrivateWindow,
      editBookmark,
      deleteBookmark,
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

    openInContainerTab.hidden =
      !isBookmark ||
      lazy.PrivateBrowsingUtils.isWindowPrivate(this.topWindow) ||
      !Services.prefs.getBoolPref("privacy.userContext.enabled", false);
    openInPrivateWindow.hidden =
      !isBookmark || !lazy.PrivateBrowsingUtils.enabled;
    editBookmark.hidden = isSeparator;
    paste.hidden = !this.#hasClipboardData();

    openAllBookmarks.disabled = isEmpty;
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
    editBookmark.disabled = isRootFolder;
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
      });
      Glean.browserUiInteraction.sidebarBookmarks.open_in_new_container_tab.add(
        1
      );
      return;
    }
    let label;
    switch (e.target.id) {
      case "sidebar-bookmarks-context-open-all-bookmarks":
        this.#openAllBookmarks();
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
        this.#deleteBookmark(this.triggerNode);
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
        this.#cutItem();
        label = "cut_bookmark";
        break;
      case "sidebar-bookmarks-context-copy":
        this.#copyItem();
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
    this.#deleteBookmark(this.triggerNode);
  }

  async #editBookmarkOrFolder(bookmark) {
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

  async #deleteBookmark(bookmark) {
    await lazy.PlacesTransactions.Remove({ guids: [bookmark.guid] }).transact();
  }

  async #addItem(type) {
    const guid = await lazy.PlacesUIUtils.showBookmarkDialog(
      { action: "add", type },
      this.topWindow
    );
    const outcome = guid ? "confirmed" : "cancelled";
    const label =
      type === "folder"
        ? `add_bookmark_folder_${outcome}`
        : `add_bookmark_${outcome}`;
    Glean.browserUiInteraction.sidebarBookmarks[label].add(1);
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

  async #openAllBookmarks() {
    const tree = await lazy.PlacesUtils.promiseBookmarksTree(
      this.triggerNode.guid
    );
    const urls = this.#collectBookmarkUrls(tree);
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
      } else if (child.children) {
        urls.push(...this.#collectBookmarkUrls(child));
      }
    }
    return urls;
  }

  async #sortByName() {
    await lazy.PlacesTransactions.SortByName(this.triggerNode.guid).transact();
  }

  async #cutItem() {
    this.#copyItemToClipboard("cut");
    await lazy.PlacesTransactions.Remove({
      guids: [this.triggerNode.guid],
    }).transact();
  }

  #copyItem() {
    this.#copyItemToClipboard("copy");
  }

  #copyItemToClipboard(action) {
    let data;
    if (this.triggerNode.isSeparator) {
      data = JSON.stringify({
        type: lazy.PlacesUtils.TYPE_X_MOZ_PLACE_SEPARATOR,
      });
    } else if (this.triggerNode.isFolder) {
      data = JSON.stringify({
        type: lazy.PlacesUtils.TYPE_X_MOZ_PLACE_CONTAINER,
        itemGuid: this.triggerNode.guid,
        instanceId: lazy.PlacesUtils.instanceId,
        title: this.triggerNode.title,
      });
    } else {
      data = JSON.stringify({
        type: lazy.PlacesUtils.TYPE_X_MOZ_PLACE,
        itemGuid: this.triggerNode.guid,
        instanceId: lazy.PlacesUtils.instanceId,
        title: this.triggerNode.title,
        uri: this.triggerNode.url,
      });
    }
    this.#setClipboard(data, action);
  }

  #setClipboard(data, action) {
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
    xferable.addDataFlavor(lazy.PlacesUtils.TYPE_X_MOZ_PLACE);
    xferable.setTransferData(
      lazy.PlacesUtils.TYPE_X_MOZ_PLACE,
      toISupports(data)
    );
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
    const insertionPoint = {
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
  }

  #searchBookmarks(node, query) {
    const results = [];
    for (const child of node.children ?? []) {
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
    this.#normalizeBookmarkNode(tree, guidToL10nId);
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

  #normalizeBookmarkNode(node, guidToL10nId) {
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
    }
    const l10nId = guidToL10nId?.[node.guid];
    if (l10nId) {
      const [msg] = bookmarkFolderLocalization.formatMessagesSync([
        { id: l10nId },
      ]);
      node.title = msg.value;
    }
    for (const child of node.children ?? []) {
      this.#normalizeBookmarkNode(child, guidToL10nId);
    }
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
      ></sidebar-bookmark-list>
    `;
  }

  render() {
    return html`
      ${this.stylesheet()}
      <div class="sidebar-panel">
        <sidebar-panel-header
          data-l10n-id="sidebar-menu-bookmarks-header"
          data-l10n-attrs="heading"
          view="viewBookmarksSidebar"
        >
          <div class="options-container">
            <moz-input-search
              data-l10n-id="firefoxview-search-text-box-bookmarks"
              data-l10n-attrs="placeholder"
              @MozInputSearch:search=${this.onSearchQuery}
            ></moz-input-search>
          </div>
        </sidebar-panel-header>
        <div class="sidebar-panel-scrollable-content">
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
                @bookmark-folder-toggle=${this.#onFolderToggle}
              ></sidebar-bookmark-list>`
          )}
        </div>
      </div>
    `;
  }
}

customElements.define("sidebar-bookmarks", SidebarBookmarks);
