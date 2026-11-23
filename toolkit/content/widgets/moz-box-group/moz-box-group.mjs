/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-box-group.tagmap.d.ts" />

import { html, ifDefined, staticHtml, literal } from "../vendor/lit.all.mjs";
import { MozLitElement, hasModifierKey } from "../lit-utils.mjs";
import { MozBoxItem } from "chrome://global/content/elements/moz-box-item.mjs";

export const GROUP_TYPES = {
  list: "list",
  reorderable: "reorderable-list",
};

/**
 * An element used to group combinations of moz-box-item, moz-box-link, and
 * moz-box-button elements and provide the expected styles.
 *
 * @tagname moz-box-group
 * @slot default - Slot for rendering various moz-box-* elements.
 * @slot static - Slot for rendering non-reorderable moz-box-item elements.
 * @slot <index> - Slots used to assign moz-box-* elements to <li> elements when
 *   the group is type="list".
 * @slot <static-index>
 *   Slots used to render moz-box-item elements that are not intended to be reorderable
 *   when the group is type="reorderable-list".
 * @fires reorder
 *  Fired when items are reordered via drag-and-drop or keyboard shortcuts.
 *  The detail object contains draggedElement, targetElement, position, draggedIndex, targetIndex, and insertAt.
 * @fires scroll
 *  Re-dispatched on the host when a scroll container in the shadow tree
 *  scrolls, so light DOM listeners can observe scrolls that wouldn't otherwise
 *  cross the shadow boundary.
 */

export class MozBoxGroup extends MozLitElement {
  #tabbable = true;

  static properties = {
    type: { type: String },
    ariaLabel: { type: String, fluent: true, mapped: true },
    listItems: { type: Array, state: true },
    staticItems: { type: Array, state: true },
  };

  static queries = {
    reorderableList: "moz-reorderable-list",
    headerSlot: "slot[name='header']",
    footerSlot: "slot[name='footer']",
  };

  constructor() {
    super();

    /**
     * The type of the group. Note that "reorderable-list" only works with
     * moz-box-item elements for now.
     *
     * @type {"list" | "reorderable-list" | undefined}
     */
    this.type = undefined;

    /**
     * Accessible name for the reorderable listbox.
     *
     * @type {string | undefined}
     */
    this.ariaLabel = undefined;

    /**
     * Visible children assigned to the default slot.
     *
     * @type {Element[]}
     */
    this.listItems = [];

    /**
     * Visible children assigned to the static slots.
     *
     * @type {Element[]}
     */
    this.staticItems = [];

    /** @internal */
    this.listMutationObserver = new MutationObserver(
      this.updateItems.bind(this)
    );
  }

  firstUpdated(changedProperties) {
    super.firstUpdated(changedProperties);
    this.listMutationObserver.observe(this, {
      attributeFilter: ["hidden"],
      subtree: true,
      childList: true,
    });
    // Capture scrolls from any scroll container in component shadow tree.
    this.renderRoot.addEventListener("scroll", this.#forwardScroll, {
      capture: true,
    });
    this.updateItems();
  }

  // Re-dispatch on the host so light DOM listeners (e.g. an anchored
  // panel-list's hide-on-scroll) see scrolls that can't cross the shadow root.
  #forwardScroll = () => {
    this.dispatchEvent(new Event("scroll", { composed: true }));
  };

  /**
   * Whether this group renders its items as a list.
   *
   * @internal
   * @returns {boolean}
   */
  get isListType() {
    return (
      this.type == GROUP_TYPES.list || this.type == GROUP_TYPES.reorderable
    );
  }

  contentTemplate() {
    if (this.type == GROUP_TYPES.reorderable) {
      return html`<moz-reorderable-list
        class="scroll-container"
        itemselector="moz-box-item:not([static])"
        dragselector=".handle"
        @reorder=${this.handleReorder}
      >
        ${this.slotTemplate()}
      </moz-reorderable-list>`;
    }
    return this.slotTemplate();
  }

  slotTemplate() {
    let isReorderable = this.type == GROUP_TYPES.reorderable;
    if (this.isListType) {
      let listTag = isReorderable ? literal`ol` : literal`ul`;
      return staticHtml`<${listTag}
          tabindex="-1"
          role=${ifDefined(isReorderable ? "listbox" : undefined)}
          aria-label=${ifDefined(isReorderable ? this.ariaLabel : undefined)}
          class="list scroll-container"
          aria-orientation="vertical"
          @keydown=${this.handleKeydown}
          @focusin=${this.handleFocus}
          @focusout=${this.handleBlur}
        >
          ${this.listItems.map((_, i) => {
            return html`<li
              role=${ifDefined(isReorderable ? "presentation" : undefined)}
            >
              <slot name=${i}></slot>
            </li> `;
          })}
          ${this.staticItems?.map((_, i) => {
            return html`<li
              role=${ifDefined(isReorderable ? "presentation" : undefined)}
            >
              <slot name=${`static-${i}`}></slot>
            </li> `;
          })}
        </${listTag}>
        <slot hidden></slot>
        ${isReorderable ? html`<slot name="static" hidden></slot>` : ""}`;
    }
    return html`<div class="scroll-container" tabindex="-1">
      <slot></slot>
    </div>`;
  }

  /**
   * Returns the moz-box element for a list item: either the item itself
   * if it matches, or the first moz-box descendant (e.g. when the item is
   * a setting-control wrapper). Returns null if neither is a moz-box element.
   *
   * @param {Element} listItem
   * @returns {Element | null}
   */
  getMozBoxElement(listItem) {
    let selector = "moz-box-item, moz-box-link, moz-box-button";
    if (listItem.matches(selector)) {
      return listItem;
    }
    return listItem.querySelector(selector);
  }

  /**
   * Resets the tabindex on an item so it participates in focus traversal
   * again. moz-box-item in list is made focusable directly; other items defer to
   * their inner focusable descendants.
   *
   * @param {Element} item
   */
  restoreTabindex(item) {
    let element = this.getMozBoxElement(item);
    if (element?.localName === "moz-box-item") {
      if (this.isListType) {
        element.setAttribute("tabindex", "0");
      } else {
        element.removeAttribute("tabindex");
      }
    } else {
      item.removeAttribute("tabindex");
    }
  }

  /**
   * Sets role option on the item's underlying moz-box element when
   * the group renders as a reorderable list, and removes it otherwise.
   *
   * @param {Element} item
   */
  updateOptionRole(item) {
    let option = this.getMozBoxElement(item);
    if (option && this.type == GROUP_TYPES.reorderable) {
      option.setAttribute("role", "option");
    } else {
      option?.removeAttribute("role");
    }
  }

  /**
   * Reorder an array based on the reorder event. For consumers that store
   * the items in an array, you can use this to get the new ordering after the
   * reorder operation.
   *
   * @example
   * onReorder(event) {
   *   this.items = event.target.reorderArrayFromEvent(this.items, event);
   *   this.saveItems(this.items);
   * }
   *
   * @param {any[]} array The array of items that represent this group.
   * @param {CustomEvent} event The reorder event dispatched from this group.
   * @returns {any[]} The reordered array.
   */
  reorderArrayFromEvent(array, event) {
    let { draggedIndex, insertAt } = event.detail;
    array = Array.from(array);
    let [moved] = array.splice(draggedIndex, 1);
    array.splice(insertAt, 0, moved);
    return array;
  }

  /**
   * Handles reordering of items in the list.
   *
   * @param {object} event - Event object or wrapper containing detail from moz-reorderable-list.
   * @param {object} event.detail - Detail object from moz-reorderable-list.evaluateKeyDownEvent or drag-and-drop event.
   * @param {Element} event.detail.draggedElement - The element being reordered.
   * @param {Element} event.detail.targetElement - The target element to reorder relative to.
   * @param {number} event.detail.position - Position relative to target (-1 for before, 0 for after).
   * @param {number} event.detail.draggedIndex - The index of the element being reordered.
   * @param {number} event.detail.targetIndex - The index of the target element.
   * @param {number} event.detail.insertAt - The index at which to insert the draggedElement after removing it from its original position.
   */
  handleReorder(event) {
    let { targetIndex } = event.detail;

    this.dispatchEvent(
      new CustomEvent("reorder", {
        bubbles: true,
        detail: event.detail,
      })
    );

    /**
     * Without requesting an animation frame, we will lose focus within
     * the box group when using Ctrl + Shift + ArrowDown. The focus will
     * move to the browser chrome which is unexpected.
     *
     */
    requestAnimationFrame(() => {
      this.listItems[targetIndex]?.focus();
      this.announceReorder(targetIndex);
    });
  }

  /**
   * Announces the new position of a reordered item to assistive technology.
   *
   * @param {number} index - The index the item was moved to.
   */
  async announceReorder(index) {
    let message = await this.ownerDocument.l10n.formatValue(
      "moz-box-group-reorder-announcement",
      {
        position: index + 1,
        total: this.listItems.length + this.staticItems.length,
      }
    );
    this.ownerDocument.ariaNotify(message);
  }

  handleKeydown(event) {
    let item = event.originalTarget;
    if (item.localName === "moz-box-item" && item.isDraggable) {
      let detail = this.reorderableList.evaluateKeyDownEvent(event);
      if (detail) {
        event.preventDefault();
        event.stopPropagation();
        this.handleReorder({ detail });
        return;
      }
    }

    // Plain arrows are for navigation between rows. Any modifier
    // means this isn't a navigation key.
    if (hasModifierKey(event)) {
      return;
    }

    let positionElement = event.target.closest("[position]");
    if (!positionElement) {
      // If the user has clicked on the MozBoxGroup it may get keydown events
      // even if there is no focused element within it. Then the event target
      // will be the <ul> and we won't find an element with [position].
      return;
    }
    let positionAttr = positionElement.getAttribute("position");
    let currentPosition = parseInt(positionAttr);

    let allItems = [...this.listItems, ...this.staticItems];

    switch (event.key) {
      case "Down":
      case "ArrowDown": {
        event.preventDefault();
        let nextItem = allItems[currentPosition + 1];
        this.#focusItem(nextItem, event);
        break;
      }
      case "Up":
      case "ArrowUp": {
        event.preventDefault();
        let prevItem = allItems[currentPosition - 1];
        this.#focusItem(prevItem, event);
        break;
      }
    }
  }

  /**
   * @param {Element | undefined} item
   * @param {KeyboardEvent} event
   */
  #focusItem(item, event) {
    if (item instanceof MozBoxItem) {
      item.focusFromEvent(event);
    } else if (item instanceof HTMLElement) {
      item.focus();
    }
  }

  handleFocus(event) {
    if (this.#tabbable) {
      let activeElement = event.target.closest("[position]");
      if (!activeElement) {
        // Focus landed on the list container itself (e.g. clicking on
        // whitespace)
        return;
      }
      this.#tabbable = false;
      let activeMozBox = this.getMozBoxElement(activeElement);
      let allItems = [...this.listItems, ...this.staticItems];
      allItems.forEach(item => {
        let element = this.getMozBoxElement(item);
        // For moz-box-item, tabindex applies directly to it. Other elements
        // delegate focus to an inner control.
        if (element?.localName === "moz-box-item") {
          element.setAttribute(
            "tabindex",
            element === activeMozBox ? "0" : "-1"
          );
        } else {
          item.setAttribute("tabindex", "-1");
        }
      });
    }
  }

  handleBlur() {
    if (!this.#tabbable) {
      this.#tabbable = true;
      let allItems = [...this.listItems, ...this.staticItems];
      allItems.forEach(item => {
        this.restoreTabindex(item);
      });
    }
  }

  updateItems() {
    /** @type {Element[]} */
    let listItems = [];
    /** @type {Element[]} */
    let staticItems = [];
    [...this.children].forEach(child => {
      if (child.slot === "header" || child.slot === "footer" || child.hidden) {
        return;
      }
      if (child.slot.includes("static")) {
        staticItems.push(child);
      } else {
        listItems.push(child);
      }
    });
    this.listItems = listItems;
    this.staticItems = staticItems;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-box-group.css"
      />
      <slot name="header"></slot>
      ${this.contentTemplate()}
      <slot name="footer"></slot>
    `;
  }

  updated(changedProperties) {
    let headerNode = this.headerSlot.assignedNodes()[0];
    let footerNode = this.footerSlot.assignedNodes().at(-1);
    headerNode?.classList.add("first");
    footerNode?.classList.add("last");

    if (changedProperties.has("listItems") && this.listItems.length) {
      this.listItems.forEach((item, i) => {
        if (this.isListType) {
          item.slot = i;
        }
        item.setAttribute("position", i);
        item.classList.toggle("first", i == 0 && !headerNode);
        item.classList.toggle(
          "last",
          i == this.listItems.length - 1 &&
            !this.staticItems.length &&
            !footerNode
        );
        this.restoreTabindex(item);
        this.updateOptionRole(item);
      });
      if (!this.#tabbable) {
        this.#tabbable = true;
      }
    }

    if (changedProperties.has("staticItems") && this.staticItems.length) {
      this.staticItems.forEach((item, i) => {
        item.slot = `static-${i}`;
        item.setAttribute("position", this.listItems.length + i);
        let staticEl = item.querySelector("moz-box-item") ?? item;
        staticEl.setAttribute("static", "");
        item.classList.toggle(
          "first",
          i == 0 && !this.listItems.length && !headerNode
        );
        item.classList.toggle(
          "last",
          i == this.staticItems.length - 1 && !footerNode
        );
        this.restoreTabindex(item);
        this.updateOptionRole(item);
      });
    }

    if (changedProperties.has("type") && this.isListType) {
      this.updateItems();
    }
  }
}
customElements.define("moz-box-group", MozBoxGroup);
