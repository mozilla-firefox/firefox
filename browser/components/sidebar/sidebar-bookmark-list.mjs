/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined } from "chrome://global/content/vendor/lit.all.mjs";

import {
  SidebarTabList,
  SidebarTabRow,
} from "chrome://browser/content/sidebar/sidebar-tab-list.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  PlacesUIUtils: "moz-src:///browser/components/places/PlacesUIUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
});

const TAB_DROP_TYPE = "application/x-moz-tabbrowser-tab";

const DROP_BEFORE = -1;
const DROP_ON = 0;
const DROP_AFTER = 1;

// Matches the legacy bookmarks sidebar tree's drag-hover-to-expand delay,
// which uses LookAndFeel::IntID::TreeOpenDelay (1000ms on all platforms).
const DRAG_HOVER_EXPAND_DELAY_MS = 1000;

let activeDropList = null;

/**
 * A row in the sidebar bookmarks tree.
 *
 * @typedef {object} SidebarBookmarkItem
 *
 * @property {string} guid
 * @property {string} title
 * @property {string} [uri]
 * @property {string} [url]
 * @property {string} [icon]
 * @property {number} type
 * @property {SidebarBookmarkItem[]} [children]
 * @property {boolean} [isPlaceContainer]
 * @property {boolean} [isTagContainer]
 * @property {boolean} [isTagsRoot]
 */

export class SidebarBookmarkList extends SidebarTabList {
  static properties = {
    ...SidebarTabList.properties,
    expandedFolderGuids: { type: Object },
    readOnly: { type: Boolean },
  };

  #draggedGuid = null;
  #dropTarget = null;
  #hoverFolderGuid = null;
  #hoverFolderTimer = null;

  constructor() {
    super();
    this.bookmarksContext = true;
    this.expandedFolderGuids = new Set();
    // True when this list shows the contents of a fixed-sort query folder
    // (e.g. Recently Bookmarked or a tag under Recent Tags). Items in such a
    // folder can't be reordered, so dragging from or dropping into the list is
    // disabled, matching the legacy bookmarks sidebar.
    this.readOnly = false;
    this.getItemHeight = (item, h) => this.#itemHeightGetter(item, h);
  }

  #containingDetails = null;
  #onContainingDetailsToggle = () => {
    if (this.#containingDetails?.open) {
      // The inner <virtual-list> was first observed by its IntersectionObserver
      // while the containing <details> had no layout box, so it never flipped
      // its `isVisible` flag and skipped rendering rows. Re-observe now that
      // the details has opened and a layout box exists.
      this.shadowRoot
        ?.querySelector("virtual-list")
        ?.triggerIntersectionObserver();
    } else {
      this.treeView.clearSelectionForList(this);
    }
  };

  connectedCallback() {
    super.connectedCallback();
    this.#containingDetails = this.closest("details");
    this.#containingDetails?.addEventListener(
      "toggle",
      this.#onContainingDetailsToggle
    );
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.#containingDetails?.removeEventListener(
      "toggle",
      this.#onContainingDetailsToggle
    );
    this.#containingDetails = null;
    this.#clearHoverFolderExpand();
  }

  /**
   * Find the rendered nested `<sidebar-bookmark-list>` for a folder guid.
   * Returns null if the folder isn't currently rendered (e.g. virtual-list
   * has it scrolled out of view).
   *
   * @param {string} guid
   * @returns {SidebarBookmarkList}
   */
  findSublistForGuid(guid) {
    const folder = this.shadowRoot.querySelector(
      `summary[data-guid="${CSS.escape(guid)}"]`
    );
    return folder?.parentElement?.querySelector("sidebar-bookmark-list");
  }

  /**
   * Force the virtual-list chunk holding an item to render, so the item's row
   * exists in the DOM even while it is scrolled out of view. Sublists are
   * otherwise only rendered once they intersect the viewport, which leaves
   * items in a long list unreachable until the user scrolls to them.
   *
   * @param {string} guid
   * @returns {Promise<boolean>} False if this list has no item with that guid.
   *   The lists render from a snapshot of the bookmarks tree, so a folder that
   *   Places reports in a path can be absent from them; a caller walking that
   *   path uses this to learn it can't descend any further.
   */
  async renderItemForGuid(guid) {
    const index = this.tabItems?.findIndex(item => item.guid === guid) ?? -1;
    if (index < 0) {
      return false;
    }
    // A nested list is created by its parent list's render, so on the first
    // call it hasn't rendered its own shadow root yet and the <virtual-list>
    // queried below is still missing. That's also why the null check on it
    // can't move any higher.
    await this.updateComplete;
    const virtualList = this.rootVirtualListEl;
    if (!virtualList) {
      return false;
    }
    // A list that has never intersected the viewport renders no chunks at all,
    // so make it render them before asking for the one holding the item.
    virtualList.isVisible = true;
    await virtualList.updateComplete;
    const subList = virtualList.getSubListForItem(index);
    if (!subList) {
      return false;
    }
    subList.isVisible = true;
    await subList.updateComplete;
    return true;
  }

  willUpdate(changes) {
    super.willUpdate(changes);
    if (changes.has("expandedFolderGuids")) {
      // Reassign to a new function reference so Lit detects the change and
      // propagates it to sublists, causing them to reset their cached heights.
      this.getItemHeight = (item, h) => this.#itemHeightGetter(item, h);
    }
  }

  /**
   * Calculates a bookmark item's height, including visible descendants.
   *
   * @param {SidebarBookmarkItem} item
   * @param {number} defaultHeight
   * @returns {number}
   */
  #itemHeightGetter = (item, defaultHeight) => {
    if (!item.children || !this.expandedFolderGuids.has(item.guid)) {
      return defaultHeight;
    }
    return item.children.reduce(
      (sum, child) => sum + this.#itemHeightGetter(child, defaultHeight),
      defaultHeight
    );
  };

  static queries = {
    ...SidebarTabList.queries,
    rowEls: { all: "sidebar-bookmark-row" },
    folderEls: { all: "details" },
    folderLabelEl: ".bookmark-folder-label",
  };

  /**
   * Renders a bookmark item as a row, folder, or separator.
   *
   * Folders deliberately have no explicit aria-expanded: the summary's expanded
   * state comes from its details element, and an attribute updated by a later
   * render would briefly contradict it when a folder collapses.
   *
   * @param {SidebarBookmarkItem} tabItem
   * @param {number} i
   * @returns {TemplateResult}
   */
  itemTemplate = (tabItem, i) => {
    const tabIndex = this.treeView?.isActiveNode(this, tabItem.guid) ? 0 : -1;
    if (!tabItem.url && !tabItem.children) {
      return html`<div
        class="bookmark-separator"
        draggable="true"
        role="separator"
        tabindex=${tabIndex}
        data-guid=${tabItem.guid}
        .guid=${tabItem.guid}
      ></div>`;
    }
    const title = this.#getBestTitleForItem(tabItem);
    if (tabItem.children !== undefined) {
      let folderKind = null;
      if (tabItem.isTagsRoot) {
        folderKind = "tags-root";
      } else if (tabItem.isTagContainer) {
        folderKind = "tag-container";
      } else if (tabItem.isPlaceContainer) {
        folderKind = "place-container";
      }
      if (!tabItem.children.length) {
        return html`<div
          class="bookmark-folder-label"
          role="treeitem"
          aria-label=${title}
          aria-level=${this.level}
          aria-posinset=${i + 1}
          aria-setsize=${this.tabItems.length}
          data-folder-kind=${ifDefined(folderKind)}
          tabindex=${tabIndex}
          draggable="true"
          data-guid=${tabItem.guid}
          @auxclick=${e => this.#onFolderAuxClick(e, tabItem.guid)}
          @mouseenter=${e => this.#updateFolderTooltip(e, title)}
          .guid=${tabItem.guid}
        >
          <span
            class="bookmark-folder-title text-truncated-ellipsis"
            dir="auto"
          >
            ${title}
          </span>
        </div>`;
      }
      return html`
        <details
          ?open=${this.expandedFolderGuids.has(tabItem.guid)}
          @toggle=${e => this.#onFolderToggle(e, tabItem.guid)}
          data-folder-kind=${ifDefined(folderKind)}
          .guid=${tabItem.guid}
        >
          <summary
            draggable="true"
            part="summary"
            role="treeitem"
            aria-level=${this.level}
            aria-posinset=${i + 1}
            aria-setsize=${this.tabItems.length}
            tabindex=${tabIndex}
            data-guid=${tabItem.guid}
            @auxclick=${e => this.#onFolderAuxClick(e, tabItem.guid)}
            @mouseenter=${e => this.#updateFolderTooltip(e, title)}
          >
            <span
              class="bookmark-folder-title text-truncated-ellipsis"
              dir="auto"
            >
              ${title}
            </span>
          </summary>
          <div id="content">
            <sidebar-bookmark-list
              .level=${this.level + 1}
              maxTabsLength="-1"
              secondaryActionClass="delete-button"
              .tabItems=${tabItem.children}
              .expandedFolderGuids=${this.expandedFolderGuids}
              .readOnly=${this.readOnly || !!tabItem.isPlaceContainer}
              @fxview-tab-list-primary-action=${this.onPrimaryAction}
              @fxview-tab-list-secondary-action=${this.onSecondaryAction}
            >
            </sidebar-bookmark-list>
          </div>
        </details>
      `;
    }
    return html`
      <sidebar-bookmark-row
        ?active=${i == this.activeIndex}
        draggable="true"
        .canClose=${ifDefined(tabItem.canClose)}
        .closedId=${ifDefined(tabItem.closedId)}
        compact
        .currentActiveElementId=${this.currentActiveElementId}
        .closeRequested=${tabItem.closeRequested}
        .favicon=${tabItem.icon}
        .guid=${ifDefined(tabItem.guid)}
        .hasPopup=${this.hasPopup}
        .indicators=${tabItem.indicators}
        role="treeitem"
        aria-level=${this.level}
        aria-posinset=${i + 1}
        aria-setsize=${this.tabItems.length}
        .searchQuery=${ifDefined(this.searchQuery)}
        .secondaryActionClass=${ifDefined(
          this.secondaryActionClass ?? tabItem.secondaryActionClass
        )}
        .secondaryL10nArgs=${ifDefined(tabItem.secondaryL10nArgs)}
        .secondaryL10nId=${tabItem.secondaryL10nId}
        .selected=${this.isTabItemSelected(tabItem)}
        .tabElement=${ifDefined(tabItem.tabElement)}
        tabindex=${tabIndex}
        .title=${title}
        .url=${tabItem.url}
        @keydown=${e => e.currentTarget.primaryActionHandler(e)}
      ></sidebar-bookmark-row>
    `;
  };

  /**
   * Gets a display title for a bookmark item.
   *
   * @param {SidebarBookmarkItem} tabItem
   * @returns {string}
   */
  #getBestTitleForItem(tabItem) {
    return (
      tabItem.title ||
      lazy.PlacesUIUtils.getBestTitleForUri(tabItem.uri, true) ||
      lazy.PlacesUIUtils.promptLocalization.formatValueSync("places-no-title")
    );
  }

  stylesheets() {
    return [
      super.stylesheets(),
      html`<link
        rel="stylesheet"
        href="chrome://browser/content/sidebar/sidebar-bookmark-list.css"
      />`,
    ];
  }

  render() {
    if (this.searchQuery && !this.tabItems.length) {
      return this.emptySearchResultsTemplate();
    }
    return html`
      ${this.stylesheets()}
      <div
        id="fxview-tab-list"
        class="fxview-tab-list"
        role=${this.level > 1 ? "group" : "tree"}
        @keydown=${this.handleFocusElementInRow}
        @dragstart=${this.#onDragStart}
        @dragover=${this.#onDragOver}
        @dragleave=${this.#onDragLeave}
        @drop=${this.#onDrop}
        @dragend=${this.#onDragEnd}
      >
        <div class="drag-indicator"></div>
        <virtual-list
          .activeIndex=${this.activeIndex}
          .items=${this.tabItems}
          .template=${this.itemTemplate}
          .getItemHeight=${this.getItemHeight}
        ></virtual-list>
      </div>
      <slot name="menu"></slot>
    `;
  }

  #onFolderToggle(e, guid) {
    this.dispatchEvent(
      new CustomEvent("folder-toggle", {
        bubbles: true,
        composed: true,
        detail: { guid, open: e.target.open },
      })
    );
  }

  #onFolderAuxClick(e, guid) {
    if (e.button !== 1) {
      return;
    }
    e.preventDefault();
    this.dispatchEvent(
      new CustomEvent("bookmark-folder-middleclick", {
        bubbles: true,
        composed: true,
        detail: { guid, isFolder: true },
      })
    );
  }

  #updateFolderTooltip(e, title) {
    let el = e.currentTarget;
    if (el.scrollWidth > el.clientWidth) {
      el.title = title;
    } else {
      el.removeAttribute("title");
    }
  }

  #findBookmarkElement(composedPath) {
    for (const el of composedPath) {
      if (!el || el.nodeType !== Node.ELEMENT_NODE) {
        continue;
      }
      if (el.localName === "sidebar-bookmark-row" && el.guid) {
        return { guid: el.guid, url: el.url, title: el.title };
      }
      if (el.localName === "summary") {
        const details = el.parentElement;
        if (details?.guid) {
          return {
            guid: details.guid,
            title: el.textContent.trim(),
            isFolder: true,
          };
        }
      }
      if (el.classList?.contains("bookmark-folder-label") && el.guid) {
        return { guid: el.guid, title: el.textContent.trim(), isFolder: true };
      }
      if (el.classList?.contains("bookmark-separator") && el.guid) {
        return { guid: el.guid, isSeparator: true };
      }
    }
    return null;
  }

  #findDropTarget(composedPath, clientY) {
    for (const el of composedPath) {
      if (!el || el.nodeType !== Node.ELEMENT_NODE) {
        continue;
      }
      if (el.localName === "sidebar-bookmark-row" && el.guid) {
        const rect = el.getBoundingClientRect();
        const orientation =
          clientY < rect.top + rect.height / 2 ? DROP_BEFORE : DROP_AFTER;
        return { element: el, guid: el.guid, orientation };
      }
      if (el.localName === "summary") {
        const details = el.parentElement;
        if (details?.guid) {
          const rect = el.getBoundingClientRect();
          const relY = (clientY - rect.top) / rect.height;
          let orientation;
          if (relY < 0.25) {
            orientation = DROP_BEFORE;
          } else if (relY > 0.75) {
            orientation = DROP_AFTER;
          } else {
            orientation = DROP_ON;
          }
          return {
            element: details,
            guid: details.guid,
            orientation,
            isFolder: true,
          };
        }
      }
      if (el.classList?.contains("bookmark-folder-label") && el.guid) {
        const rect = el.getBoundingClientRect();
        const relY = (clientY - rect.top) / rect.height;
        let orientation;
        if (relY < 0.25) {
          orientation = DROP_BEFORE;
        } else if (relY > 0.75) {
          orientation = DROP_AFTER;
        } else {
          orientation = DROP_ON;
        }
        return { element: el, guid: el.guid, orientation, isFolder: true };
      }
      if (el.classList?.contains("bookmark-separator") && el.guid) {
        const rect = el.getBoundingClientRect();
        const orientation =
          clientY < rect.top + rect.height / 2 ? DROP_BEFORE : DROP_AFTER;
        return { element: el, guid: el.guid, orientation };
      }
      if (el.id === "fxview-tab-list") {
        break;
      }
    }
    return null;
  }

  #getSupportedFlavor(dataTransfer) {
    const types = [...dataTransfer.types];
    for (const flavor of lazy.PlacesUIUtils.SUPPORTED_FLAVORS) {
      if (types.includes(flavor)) {
        return flavor;
      }
    }
    return null;
  }

  #showDropIndicator(target) {
    if (activeDropList && activeDropList !== this) {
      const previousList = activeDropList;
      previousList.#cleanupIndicator();
      previousList.#dropTarget = null;
    }
    activeDropList = this;
    const listEl = this.shadowRoot?.querySelector("#fxview-tab-list");
    if (!listEl) {
      return;
    }
    const indicator = listEl.querySelector(".drag-indicator");
    if (!indicator) {
      return;
    }
    if (target.orientation === DROP_ON) {
      target.element.setAttribute("drag-over", "");
      indicator.classList.remove("visible");
      return;
    }
    const listRect = listEl.getBoundingClientRect();
    const itemRect = target.element.getBoundingClientRect();
    const indicatorTop =
      itemRect.top -
      listRect.top +
      (target.orientation === DROP_AFTER ? itemRect.height : 0);
    indicator.style.top = `${indicatorTop}px`;
    indicator.classList.add("visible");
  }

  #cleanupIndicator() {
    if (this.#dropTarget?.orientation === DROP_ON) {
      this.#dropTarget.element.removeAttribute("drag-over");
    }
    const listEl = this.shadowRoot?.querySelector("#fxview-tab-list");
    listEl?.querySelector(".drag-indicator")?.classList.remove("visible");
    if (activeDropList === this) {
      activeDropList = null;
    }
    this.#clearHoverFolderExpand();
  }

  // Arms a timer that expands a collapsed folder when the user dwells over it
  // during a drag, so they can drop into nested folders without first opening
  // them by hand. Only non-empty folders (rendered as <details>) participate.
  #scheduleHoverFolderExpand(target) {
    const shouldArm =
      target?.isFolder &&
      target.orientation === DROP_ON &&
      target.element.localName === "details" &&
      !target.element.open;
    if (!shouldArm) {
      this.#clearHoverFolderExpand();
      return;
    }
    if (this.#hoverFolderGuid === target.guid) {
      return;
    }
    this.#clearHoverFolderExpand();
    this.#hoverFolderGuid = target.guid;
    const detailsEl = target.element;
    this.#hoverFolderTimer = setTimeout(() => {
      this.#hoverFolderTimer = null;
      this.#hoverFolderGuid = null;
      if (detailsEl.isConnected && !detailsEl.open) {
        detailsEl.open = true;
      }
    }, DRAG_HOVER_EXPAND_DELAY_MS);
  }

  #clearHoverFolderExpand() {
    if (this.#hoverFolderTimer) {
      clearTimeout(this.#hoverFolderTimer);
      this.#hoverFolderTimer = null;
    }
    this.#hoverFolderGuid = null;
  }

  #onDragStart(e) {
    if (this.readOnly) {
      e.preventDefault();
      e.stopPropagation();
      return;
    }
    const item = this.#findBookmarkElement(e.composedPath());
    if (!item) {
      e.preventDefault();
      return;
    }
    this.#draggedGuid = item.guid;
    let data;
    let url = item.url;
    if (item.isSeparator) {
      data = JSON.stringify({
        type: lazy.PlacesUtils.TYPE_X_MOZ_PLACE_SEPARATOR,
        itemGuid: item.guid,
        instanceId: lazy.PlacesUtils.instanceId,
      });
    } else if (item.isFolder) {
      if (lazy.PlacesUtils.isRootItem(item.guid)) {
        const payload =
          lazy.PlacesUtils.bookmarks.createVirtualLinkToRoot(item);
        data = JSON.stringify(payload);
        url = payload.uri;
      } else {
        data = JSON.stringify({
          type: lazy.PlacesUtils.TYPE_X_MOZ_PLACE_CONTAINER,
          itemGuid: item.guid,
          guid: item.guid,
          instanceId: lazy.PlacesUtils.instanceId,
          title: item.title,
        });
      }
    } else {
      data = JSON.stringify({
        type: lazy.PlacesUtils.TYPE_X_MOZ_PLACE,
        itemGuid: item.guid,
        guid: item.guid,
        instanceId: lazy.PlacesUtils.instanceId,
        title: item.title,
        uri: item.url,
      });
    }
    e.dataTransfer.clearData();
    e.dataTransfer.setData(lazy.PlacesUtils.TYPE_X_MOZ_PLACE, data);
    if (url) {
      e.dataTransfer.setData(
        lazy.PlacesUtils.TYPE_X_MOZ_URL,
        url + "\n" + item.title
      );
      e.dataTransfer.setData(lazy.PlacesUtils.TYPE_PLAINTEXT, url);
    }
    e.dataTransfer.effectAllowed = "copyMove";
    e.stopPropagation();
  }

  #onDragOver(e) {
    e.stopPropagation();
    if (this.readOnly) {
      this.#cleanupIndicator();
      this.#dropTarget = null;
      return;
    }
    const flavor = this.#getSupportedFlavor(e.dataTransfer);
    if (!flavor) {
      return;
    }
    let target = this.#findDropTarget(e.composedPath(), e.clientY);
    if (!target) {
      target = this.#getFolderDropTarget();
    }
    if (
      !target ||
      target.guid === this.#draggedGuid ||
      this.#isFixedSortFolderTarget(target)
    ) {
      this.#cleanupIndicator();
      this.#dropTarget = null;
      return;
    }
    e.preventDefault();
    if (
      this.#dropTarget?.orientation === DROP_ON &&
      (this.#dropTarget.element !== target.element ||
        target.orientation !== DROP_ON)
    ) {
      this.#dropTarget.element.removeAttribute("drag-over");
    }
    this.#showDropIndicator(target);
    this.#dropTarget = target;
    this.#scheduleHoverFolderExpand(target);
    e.dataTransfer.dropEffect = lazy.PlacesUIUtils.PLACES_FLAVORS.includes(
      flavor
    )
      ? "move"
      : "copy";
  }

  #getFolderDropTarget() {
    const parentDetails = this.closest("details");
    if (parentDetails?.guid) {
      return {
        element: parentDetails,
        guid: parentDetails.guid,
        orientation: DROP_ON,
        isFolder: true,
      };
    }
    return null;
  }

  // A query folder (Recently Bookmarked, Recent Tags, or a tag container) has
  // a forced sort order, so we can't drop into it. Such folders are the only
  // ones tagged with `data-folder-kind`.
  #isFixedSortFolderTarget(target) {
    return (
      target.isFolder &&
      target.orientation === DROP_ON &&
      !!target.element?.dataset?.folderKind
    );
  }

  #onDragLeave(e) {
    let node = e.relatedTarget;
    while (node) {
      const root = node.getRootNode?.();
      if (ShadowRoot.isInstance(root)) {
        node = root.host;
      } else {
        node = node.parentNode;
      }
      if (node === this) {
        return;
      }
    }
    this.#cleanupIndicator();
    this.#dropTarget = null;
  }

  #onDrop(e) {
    e.preventDefault();
    e.stopPropagation();
    if (this.readOnly) {
      this.#cleanupIndicator();
      this.#dropTarget = null;
      return;
    }
    const target = this.#dropTarget;
    this.#cleanupIndicator();
    this.#dropTarget = null;
    if (!target) {
      return;
    }
    const flavor = this.#getSupportedFlavor(e.dataTransfer);
    if (!flavor) {
      return;
    }
    let validNodes;
    if (flavor === TAB_DROP_TYPE) {
      validNodes = this.#getNodesFromTabDrop(e.dataTransfer);
    } else {
      const data = e.dataTransfer.getData(flavor);
      if (!data) {
        return;
      }
      try {
        ({ validNodes } = lazy.PlacesUtils.unwrapNodes(data, flavor));
      } catch (ex) {
        return;
      }
    }
    if (!validNodes?.length) {
      return;
    }
    const doCopy =
      !lazy.PlacesUIUtils.PLACES_FLAVORS.includes(flavor) ||
      e.dataTransfer.dropEffect === "copy";
    this.#doInsert(validNodes, target, doCopy);
  }

  #getNodesFromTabDrop(dataTransfer) {
    const nodes = [];
    const dropCount = dataTransfer.mozItemCount || 1;
    for (let i = 0; i < dropCount; i++) {
      const data = dataTransfer.mozGetDataAt(TAB_DROP_TYPE, i);
      if (!data) {
        continue;
      }
      if (
        XULElement.isInstance(data) &&
        data.localName === "tab" &&
        data.documentGlobal.isChromeWindow
      ) {
        const uri = data.linkedBrowser.currentURI;
        nodes.push({
          uri: uri?.spec ?? "about:blank",
          title: data.label,
          type: lazy.PlacesUtils.TYPE_X_MOZ_URL,
        });
      } else if (
        XULElement.isInstance(data) &&
        data.localName === "tab-split-view-wrapper" &&
        data.documentGlobal.isChromeWindow
      ) {
        for (const tab of data.tabs) {
          nodes.push({
            uri: tab.linkedBrowser.currentURI?.spec ?? "about:blank",
            title: tab.label,
            type: lazy.PlacesUtils.TYPE_X_MOZ_URL,
          });
        }
      }
    }
    return nodes;
  }

  async #doInsert(validNodes, target, doCopy) {
    let insertionPoint;
    if (target.orientation === DROP_ON) {
      insertionPoint = {
        guid: target.guid,
        isTag: false,
        getIndex: async () => lazy.PlacesUtils.bookmarks.DEFAULT_INDEX,
      };
    } else {
      let fetchInfo;
      try {
        fetchInfo = await lazy.PlacesUtils.bookmarks.fetch({
          guid: target.guid,
        });
      } catch (ex) {
        return;
      }
      if (!fetchInfo) {
        return;
      }
      insertionPoint = {
        guid: fetchInfo.parentGuid,
        isTag: false,
        getIndex: async () =>
          target.orientation === DROP_BEFORE
            ? fetchInfo.index
            : fetchInfo.index + 1,
      };
    }
    await lazy.PlacesUIUtils.handleTransferItems(
      validNodes,
      insertionPoint,
      doCopy,
      null
    );
  }

  #onDragEnd() {
    this.#cleanupIndicator();
    this.#dropTarget = null;
    this.#draggedGuid = null;
  }
}
customElements.define("sidebar-bookmark-list", SidebarBookmarkList);

export class SidebarBookmarkRow extends SidebarTabRow {}
customElements.define("sidebar-bookmark-row", SidebarBookmarkRow);
