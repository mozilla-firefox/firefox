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

let activeDropList = null;

export class SidebarBookmarkList extends SidebarTabList {
  static properties = {
    ...SidebarTabList.properties,
    expandedFolderGuids: { type: Object },
  };

  #draggedGuid = null;
  #dropTarget = null;

  constructor() {
    super();
    this.bookmarksContext = true;
    this.expandedFolderGuids = new Set();
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
    for (const details of this.shadowRoot.querySelectorAll("details")) {
      if (details.guid === guid) {
        return details.querySelector("sidebar-bookmark-list");
      }
    }
    return null;
  }

  willUpdate(changes) {
    super.willUpdate(changes);
    if (changes.has("expandedFolderGuids")) {
      // Reassign to a new function reference so Lit detects the change and
      // propagates it to sublists, causing them to reset their cached heights.
      this.getItemHeight = (item, h) => this.#itemHeightGetter(item, h);
    }
  }

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

  static #getFocusableItemsInList(listEl) {
    const container = listEl.shadowRoot?.querySelector("#fxview-tab-list");
    if (!container) {
      return [];
    }
    const items = [];
    const walker = document.createTreeWalker(
      container,
      NodeFilter.SHOW_ELEMENT
    );
    let node = walker.nextNode();
    while (node) {
      if (
        node.localName === "summary" ||
        node.localName === "sidebar-bookmark-row" ||
        node.classList.contains("bookmark-separator") ||
        node.classList.contains("bookmark-folder-label")
      ) {
        items.push(node);
      }
      node = walker.nextNode();
    }
    return items;
  }

  #focusParentSummary() {
    this.closest("details")?.querySelector("summary")?.focus();
  }

  #focusLastVisibleItem(item) {
    if (item.localName !== "summary" || !item.parentElement?.open) {
      item.focus();
      return;
    }
    const nestedList = item.parentElement.querySelector(
      "sidebar-bookmark-list"
    );
    if (!nestedList) {
      item.focus();
      return;
    }
    const nestedItems =
      SidebarBookmarkList.#getFocusableItemsInList(nestedList);
    if (!nestedItems.length) {
      item.focus();
      return;
    }
    this.#focusLastVisibleItem(nestedItems[nestedItems.length - 1]);
  }

  #focusNextItemAfterFolder() {
    const parentDetails = this.closest("details");
    if (!parentDetails) {
      return;
    }
    const parentList = parentDetails.getRootNode().host;
    if (parentList?.localName !== "sidebar-bookmark-list") {
      return;
    }
    const parentItems =
      SidebarBookmarkList.#getFocusableItemsInList(parentList);
    const idx = parentItems.indexOf(parentDetails.querySelector("summary"));
    if (idx >= 0 && idx < parentItems.length - 1) {
      parentItems[idx + 1].focus();
    } else {
      parentList.#focusNextItemAfterFolder();
    }
  }

  handleFocusElementInRow(e) {
    if (
      e.getModifierState("Accel") &&
      e.key.toUpperCase() === this.selectAllShortcut
    ) {
      e.preventDefault();
      this.selectAll();
      return;
    }
    if (
      e.code !== "ArrowUp" &&
      e.code !== "ArrowDown" &&
      e.code !== "ArrowLeft" &&
      e.code !== "ArrowRight"
    ) {
      return;
    }
    // Events from nested lists are retargeted to the nested list element; ignore them.
    if (e.target.localName === "sidebar-bookmark-list") {
      return;
    }
    e.preventDefault();
    const { target } = e;
    const isSummary = target.localName === "summary";
    let nextFocusedRow = null;
    switch (e.code) {
      case "ArrowLeft":
        if (isSummary && target.parentElement?.open) {
          target.parentElement.open = false;
        } else {
          this.#focusParentSummary();
        }
        break;
      case "ArrowRight":
        if (isSummary) {
          const details = target.parentElement;
          if (!details.open) {
            details.open = true;
            const nestedList = details.querySelector("sidebar-bookmark-list");
            if (nestedList) {
              nestedList.updateComplete.then(() => {
                SidebarBookmarkList.#getFocusableItemsInList(
                  nestedList
                )[0]?.focus();
              });
            }
          } else {
            const nestedList = details.querySelector("sidebar-bookmark-list");
            if (nestedList) {
              SidebarBookmarkList.#getFocusableItemsInList(
                nestedList
              )[0]?.focus();
            }
          }
        }
        break;
      case "ArrowDown": {
        if (isSummary && target.parentElement?.open) {
          const nestedList = target.parentElement.querySelector(
            "sidebar-bookmark-list"
          );
          if (nestedList) {
            const nestedItems =
              SidebarBookmarkList.#getFocusableItemsInList(nestedList);
            if (nestedItems.length) {
              nestedItems[0].focus();
              break;
            }
          }
        }
        const items = SidebarBookmarkList.#getFocusableItemsInList(this);
        const idx = items.indexOf(target);
        if (idx < items.length - 1) {
          items[idx + 1].focus();
          if (
            !isSummary &&
            items[idx + 1].localName === "sidebar-bookmark-row"
          ) {
            nextFocusedRow = items[idx + 1];
          }
        } else {
          this.#focusNextItemAfterFolder();
        }
        break;
      }
      case "ArrowUp": {
        const items = SidebarBookmarkList.#getFocusableItemsInList(this);
        const idx = items.indexOf(target);
        if (idx > 0) {
          this.#focusLastVisibleItem(items[idx - 1]);
          if (
            !isSummary &&
            items[idx - 1].localName === "sidebar-bookmark-row"
          ) {
            nextFocusedRow = items[idx - 1];
          }
        } else {
          this.#focusParentSummary();
        }
        break;
      }
    }
    if (
      (e.code === "ArrowDown" || e.code === "ArrowUp") &&
      !e.getModifierState("Accel") &&
      nextFocusedRow
    ) {
      if (e.shiftKey) {
        this.dispatchEvent(
          new CustomEvent("shift-select", {
            bubbles: true,
            composed: true,
            detail: { row: nextFocusedRow },
          })
        );
      } else {
        this.clearSelection();
        this.dispatchEvent(
          new CustomEvent("clear-selection", {
            bubbles: true,
            composed: true,
          })
        );
        this.dispatchEvent(
          new CustomEvent("set-anchor", {
            bubbles: true,
            composed: true,
            detail: { guid: nextFocusedRow.guid },
          })
        );
      }
    }
  }

  itemTemplate = (tabItem, i) => {
    let tabIndex = -1;
    if ((this.searchQuery || this.sortOption == "lastvisited") && i == 0) {
      tabIndex = 0;
    } else if (!this.searchQuery) {
      tabIndex = 0;
    }
    if (!tabItem.url && !tabItem.children) {
      return html`<div
        class="bookmark-separator"
        draggable="true"
        role="separator"
        tabindex="0"
        .guid=${tabItem.guid}
      ></div>`;
    }
    if (tabItem.children !== undefined) {
      if (!tabItem.children.length) {
        return html`<div
          class="bookmark-folder-label"
          tabindex="0"
          draggable="true"
          .guid=${tabItem.guid}
        >
          ${tabItem.title}
        </div>`;
      }
      return html`
        <details
          ?open=${this.expandedFolderGuids.has(tabItem.guid)}
          @toggle=${e => this.#onFolderToggle(e, tabItem.guid)}
          .guid=${tabItem.guid}
        >
          <summary draggable="true" part="summary">${tabItem.title}</summary>
          <div id="content">
            <sidebar-bookmark-list
              maxTabsLength="-1"
              secondaryActionClass="delete-button"
              .tabItems=${tabItem.children}
              .expandedFolderGuids=${this.expandedFolderGuids}
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
        .primaryL10nArgs=${ifDefined(tabItem.primaryL10nArgs)}
        .primaryL10nId=${tabItem.primaryL10nId}
        role="listitem"
        .searchQuery=${ifDefined(this.searchQuery)}
        .secondaryActionClass=${ifDefined(
          this.secondaryActionClass ?? tabItem.secondaryActionClass
        )}
        .secondaryL10nArgs=${ifDefined(tabItem.secondaryL10nArgs)}
        .secondaryL10nId=${tabItem.secondaryL10nId}
        .selected=${this.isTabItemSelected(tabItem)}
        .tabElement=${ifDefined(tabItem.tabElement)}
        tabindex=${tabIndex}
        .title=${tabItem.title}
        .url=${tabItem.url}
        @keydown=${e => e.currentTarget.primaryActionHandler(e)}
      ></sidebar-bookmark-row>
    `;
  };

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
        role="list"
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
      new CustomEvent("bookmark-folder-toggle", {
        bubbles: true,
        composed: true,
        detail: { guid, open: e.target.open },
      })
    );
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
      activeDropList.#cleanupIndicator();
      activeDropList.#dropTarget = null;
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
  }

  #onDragStart(e) {
    const item = this.#findBookmarkElement(e.composedPath());
    if (!item) {
      e.preventDefault();
      return;
    }
    this.#draggedGuid = item.guid;
    let data;
    if (item.isSeparator) {
      data = JSON.stringify({
        type: lazy.PlacesUtils.TYPE_X_MOZ_PLACE_SEPARATOR,
        itemGuid: item.guid,
        instanceId: lazy.PlacesUtils.instanceId,
      });
    } else if (item.isFolder) {
      data = JSON.stringify({
        type: lazy.PlacesUtils.TYPE_X_MOZ_PLACE_CONTAINER,
        itemGuid: item.guid,
        guid: item.guid,
        instanceId: lazy.PlacesUtils.instanceId,
        title: item.title,
      });
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
    if (item.url) {
      e.dataTransfer.setData(
        lazy.PlacesUtils.TYPE_X_MOZ_URL,
        item.url + "\n" + item.title
      );
      e.dataTransfer.setData(lazy.PlacesUtils.TYPE_PLAINTEXT, item.url);
    }
    e.dataTransfer.effectAllowed = "copyMove";
    e.stopPropagation();
  }

  #onDragOver(e) {
    e.stopPropagation();
    const flavor = this.#getSupportedFlavor(e.dataTransfer);
    if (!flavor) {
      return;
    }
    let target = this.#findDropTarget(e.composedPath(), e.clientY);
    if (!target) {
      target = this.#getFolderDropTarget();
    }
    if (!target || target.guid === this.#draggedGuid) {
      this.#cleanupIndicator();
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

export class SidebarBookmarkRow extends SidebarTabRow {
  get tooltipText() {
    return this.url ? `${this.title}\n${this.url}` : null;
  }
}
customElements.define("sidebar-bookmark-row", SidebarBookmarkRow);
