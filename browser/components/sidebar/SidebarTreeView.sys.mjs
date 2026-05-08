/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * A controller that enables selection and keyboard navigation within a "tree"
 * view in the sidebar. This tree represents any hierarchical structure of
 * URLs, such as those from synced tabs, history visits, or bookmarks.
 *
 * The host component should expose:
 * - `cards` — `<moz-card>` instances of collapsible containers.
 * - `getRowsInOrder()` — returns `{ list, item }` pairs in visual
 *    order.
 *
 * Selection is keyed on `(list, guid)` because guids are not globally unique
 * across the tree. (The same URL appears in multiple cards, and each card has
 * its own list.) Storing per-list keeps a click in a card from also marking
 * the same URL "selected" in other cards.
 *
 * @implements {ReactiveController}
 */
export class SidebarTreeView {
  /**
   * Selected guids per list.
   *
   * @type {Map<SidebarTabList, Set<string>>}
   */
  selectedRows;

  /**
   * The anchor row for shift-click range selection. Holds the list and GUID of
   * the last row selected without Shift, defining one end of the range when the
   * user shift-clicks another row.
   *
   * @type {{ list: SidebarTabList, guid: string }}
   */
  #selectionAnchor = { list: null, guid: null };

  constructor(host, { multiSelect = true } = {}) {
    this.host = host;
    host.addController(this);

    this.multiSelect = multiSelect;
    this.selectedRows = new Map();
  }

  get cards() {
    return this.host.cards;
  }

  hostConnected() {
    this.host.addEventListener("clear-selection", this);
    this.host.addEventListener("set-anchor", this);
    this.host.addEventListener("shift-select", this);
    this.host.addEventListener("focus-row", this);
  }

  hostDisconnected() {
    this.host.removeEventListener("clear-selection", this);
    this.host.removeEventListener("set-anchor", this);
    this.host.removeEventListener("shift-select", this);
    this.host.removeEventListener("focus-row", this);
  }

  /**
   * Handle events bubbling up from `<sidebar-tab-list>` elements.
   *
   * @param {CustomEvent} event
   */
  handleEvent(event) {
    switch (event.type) {
      case "clear-selection":
        this.#clearSelection();
        break;
      case "set-anchor":
        this.#setAnchor(event.originalTarget, event.detail.guid);
        break;
      case "shift-select":
        this.#handleShiftSelect(event.detail.row);
        break;
      case "focus-row":
        this.#handleFocusRow(event);
        break;
    }
  }

  #setAnchor(list, guid) {
    this.#selectionAnchor = { list, guid };
  }

  #resetAnchor() {
    this.#setAnchor(null, null);
  }

  isSelected(list, guid) {
    const selection = this.selectedRows.get(list);
    return selection?.has(guid);
  }

  toggleSelection(list, guid) {
    const selection = this.#getSelectedGuids(list);
    if (selection.has(guid)) {
      selection.delete(guid);
      if (!selection.size) {
        this.selectedRows.delete(list);
      }
    } else {
      selection.add(guid);
    }
    list.requestVirtualListUpdate();
  }

  selectAllInList(list) {
    const selection = this.#getSelectedGuids(list);
    for (const { guid } of list.tabItems) {
      selection.add(guid);
    }
    list.requestVirtualListUpdate();
  }

  #getSelectedGuids(list) {
    let selection = this.selectedRows.get(list);
    if (!selection) {
      selection = new Set();
      this.selectedRows.set(list, selection);
    }
    return selection;
  }

  /**
   * Handle keydown event originating from the card header.
   *
   * @param {KeyboardEvent} event
   */
  handleCardKeydown(event) {
    if (!this.#shouldHandleEvent(event)) {
      return;
    }
    const nextSibling = event.target.nextElementSibling;
    const prevSibling = event.target.previousElementSibling;
    let focusedRow = null;
    switch (event.code) {
      case "Tab":
        if (prevSibling?.localName === "moz-card") {
          event.preventDefault();
        }
        break;
      case "ArrowUp":
        event.preventDefault();
        if (prevSibling?.localName !== "moz-card") {
          this.#focusParentHeader(event.target);
          break;
        }
        if (prevSibling?.expanded) {
          focusedRow = this.#focusLastRow(prevSibling);
        } else if (prevSibling) {
          this.#focusHeader(prevSibling);
        }
        break;
      case "ArrowDown":
        event.preventDefault();
        if (event.target.expanded) {
          focusedRow = this.#focusFirstRow(event.target);
        } else if (nextSibling?.localName === "moz-card") {
          this.#focusHeader(nextSibling);
        } else if (event.target.classList.contains("last-card")) {
          const outerCard = event.target.parentElement;
          const nextOuterCard = outerCard?.nextElementSibling;
          if (nextOuterCard?.localName === "moz-card") {
            this.#focusHeader(nextOuterCard);
          }
        }
        break;
      case "ArrowLeft":
        if (!event.target.expanded) {
          this.#focusParentHeader(event.target);
        } else {
          event.target.expanded = false;
        }
        break;
      case "ArrowRight":
        if (event.target.expanded) {
          focusedRow = this.#focusFirstRow(event.target);
        } else {
          event.target.expanded = true;
        }
        break;
      case "Home":
        if (this.cards[0]) {
          this.#focusHeader(this.cards[0]);
        }
        break;
      case "End":
        this.#focusLastVisibleRow();
        break;
    }
    if (this.multiSelect && !event.getModifierState("Accel")) {
      this.#updateSelection(event, focusedRow);
    }
  }

  /**
   * Check if we should handle this event, or if it should be handled by a
   * child element such as `<sidebar-tab-list>`.
   *
   * @param {KeyboardEvent} event
   * @returns {boolean}
   */
  #shouldHandleEvent(event) {
    if (event.code === "Home" || event.code === "End") {
      // Keys that scroll the entire tree should always be handled.
      return true;
    }
    const headerIsSelected = event.originalTarget === event.target.summaryEl;
    return headerIsSelected;
  }

  /**
   * Focus the first row of this card (either a URL or nested card header).
   *
   * @param {MozCard} card
   * @returns {SidebarTabRow}
   */
  #focusFirstRow(card) {
    let focusedRow = null;
    let innerElement = card.contentSlotEl.assignedElements()[0];
    if (innerElement.classList.contains("nested-card")) {
      // Focus the first nested card header.
      this.#focusHeader(innerElement);
    } else {
      // Focus the first URL.
      focusedRow = innerElement.rowEls[0];
      if (focusedRow) {
        this.#focusRow(focusedRow);
      }
    }
    return focusedRow;
  }

  /**
   * Focus the last row of this card (either a URL or nested card header).
   *
   * @param {MozCard} card
   * @returns {SidebarTabRow}
   */
  #focusLastRow(card) {
    let focusedRow = null;
    let innerElement = card.contentSlotEl.assignedElements()[0];
    if (innerElement.classList.contains("nested-card")) {
      // Focus the last nested card header (or URL, if nested card is expanded).
      const lastNestedCard = card.lastElementChild;
      if (lastNestedCard.expanded) {
        focusedRow = this.#focusLastRow(lastNestedCard);
      } else {
        this.#focusHeader(lastNestedCard);
      }
    } else {
      // Focus the last URL.
      focusedRow = innerElement.rowEls[innerElement.rowEls.length - 1];
      if (focusedRow) {
        this.#focusRow(focusedRow);
      }
    }
    return focusedRow;
  }

  /**
   * Focus the last visible row of the entire tree.
   */
  #focusLastVisibleRow() {
    const lastCard = this.cards[this.cards.length - 1];
    if (
      lastCard.classList.contains("nested-card") &&
      !lastCard.parentElement.expanded
    ) {
      // If this is an inner card, and the outer card is collapsed, then focus
      // the outer header.
      this.#focusHeader(lastCard.parentElement);
    } else if (lastCard.expanded) {
      this.#focusLastRow(lastCard);
    } else {
      this.#focusHeader(lastCard);
    }
  }

  /**
   * If we're currently on a nested card, focus the "outer" card's header.
   *
   * @param {MozCard} card
   */
  #focusParentHeader(card) {
    if (card.classList.contains("nested-card")) {
      this.#focusHeader(card.parentElement);
    }
  }

  /**
   * Focus a card's header without triggering unnecessary scrolling.
   *
   * @param {MozCard} card
   */
  #focusHeader(card) {
    card.summaryEl.focus({ preventScroll: true });
    card.summaryEl.scrollIntoView({ block: "nearest" });
  }

  /**
   * Focus a tab row without triggering unnecessary scrolling.
   *
   * @param {SidebarTabRow} row
   */
  #focusRow(row) {
    row.focus({ preventScroll: true });
    row.scrollIntoView({ block: "nearest" });
  }

  /**
   * When a row is focused while the shift key is held down, add it to the
   * selection. If shift key was not held down, clear the selection.
   *
   * @param {KeyboardEvent} event
   * @param {SidebarTabRow} rowEl
   */
  #updateSelection(event, rowEl) {
    if (!rowEl || (event.code !== "ArrowUp" && event.code !== "ArrowDown")) {
      return;
    }
    if (event.shiftKey) {
      this.#handleShiftSelect(rowEl);
      return;
    }
    this.#clearSelection();
    this.#setAnchor(rowEl.getRootNode().host, rowEl.guid);
  }

  selectRowInList(row, list) {
    this.#getSelectedGuids(list).add(row.guid);
    list.requestVirtualListUpdate();
  }

  /**
   * Select all items between current anchor and clicked row, in visual order.
   *
   * If no anchor has been set, fall back to selecting just the clicked row
   * and making it the new anchor.
   *
   * @param {SidebarTabRow} clickedRow
   */
  #handleShiftSelect(clickedRow) {
    const clickedList = clickedRow.getRootNode().host;
    const { list: anchorList, guid: anchorGuid } = this.#selectionAnchor;

    if (!anchorList || !anchorGuid) {
      this.#setAnchor(clickedList, clickedRow.guid);
      this.selectRowInList(clickedRow, clickedList);
      return;
    }

    const rows = this.host.getRowsInOrder();
    const anchorIndex = rows.findIndex(
      row => row.list === anchorList && row.item.guid === anchorGuid
    );
    const clickedIndex = rows.findIndex(
      row => row.list === clickedList && row.item.guid === clickedRow.guid
    );

    if (anchorIndex === -1 || clickedIndex === -1) {
      // Anchor or clicked target isn't reachable in visual order (e.g. anchor's
      // list was destroyed, or the sublist isn't currently rendered). Reset
      // and treat the click as a fresh anchor.
      this.#clearSelection();
      this.#setAnchor(clickedList, clickedRow.guid);
      this.selectRowInList(clickedRow, clickedList);
      return;
    }

    const selectedLists = [...this.selectedRows.keys()];
    const listsToUpdate = new Set();
    this.selectedRows.clear();

    let start, end;
    if (anchorIndex <= clickedIndex) {
      start = anchorIndex;
      end = clickedIndex;
    } else {
      start = clickedIndex;
      end = anchorIndex;
    }
    for (let i = start; i <= end; i++) {
      const { list, item } = rows[i];
      this.#getSelectedGuids(list).add(item.guid);
      listsToUpdate.add(list);
    }
    for (const list of selectedLists) {
      listsToUpdate.add(list);
    }
    for (const list of listsToUpdate) {
      list.requestVirtualListUpdate();
    }
  }

  /**
   * Set the anchor to the focused row if no anchor is currently set.
   *
   * @param {CustomEvent} event
   */
  #handleFocusRow(event) {
    if (this.#selectionAnchor.guid) {
      return;
    }
    this.#setAnchor(event.originalTarget, event.detail.guid);
  }

  /**
   * Get all selected tab items across all lists.
   *
   * @returns {object[]}
   */
  getSelectedTabItems() {
    const items = [];
    for (const [list, guids] of this.selectedRows) {
      for (const item of list.tabItems) {
        if (guids.has(item.guid)) {
          items.push(item);
        }
      }
    }
    return items;
  }

  clearSelectionForList(list) {
    if (this.#selectionAnchor.list === list) {
      this.#resetAnchor();
    }
    if (this.selectedRows.delete(list)) {
      list.requestVirtualListUpdate();
    }
  }

  #clearSelection() {
    const listsToUpdate = [...this.selectedRows.keys()];
    this.selectedRows.clear();
    for (const list of listsToUpdate) {
      list.requestVirtualListUpdate();
    }
  }

  resetSelection() {
    this.#clearSelection();
    this.#resetAnchor();
  }
}
