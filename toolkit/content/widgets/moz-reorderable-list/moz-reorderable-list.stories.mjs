/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { LitElement, html, ifDefined } from "../vendor/lit.all.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "./moz-reorderable-list.mjs";

const DEFAULT = "Default";
const SHADOW_DOM = "Shadow DOM";
const DRAG_SELECTOR = "Drag selector";

export default {
  title: "UI Widgets/Reorderable List",
  component: "moz-reorderable-list",
  argTypes: {
    demoType: {
      options: [DEFAULT, SHADOW_DOM, DRAG_SELECTOR],
      control: { type: "select" },
    },
  },
  parameters: {
    status: "stable",
    actions: {
      handles: ["reorder"],
    },
  },
};

class ShadowDemo extends LitElement {
  static shadowRootOptions = {
    ...LitElement.shadowRootOptions,
    delegatesFocus: true,
  };

  static properties = {
    item: { type: String },
  };

  render() {
    return html`<style>
        #shadowed {
          border: var(--border-width) solid var(--border-color);
          border-radius: var(--border-radius-small);
          background-color: var(--background-color-box);
          display: flex;
          align-items: center;
          padding: var(--space-small);
        }
      </style>
      <button id="shadowed">${this.item}</button>`;
  }
}
customElements.define("shadow-demo", ShadowDemo);

class ReorderableDemo extends LitElement {
  static properties = {
    items: { type: Array, state: true },
    type: { type: String },
  };

  // Choosing not to use Shadow DOM here for demo purposes.
  createRenderRoot() {
    return this;
  }

  constructor() {
    super();
    this.items = ["Item 1", "Item 2", "Item 3", "Item 4"];
  }

  async reorderItems(draggedElement, targetElement, before = false) {
    const draggedIndex = this.items.indexOf(draggedElement.textContent.trim());
    const targetIndex = this.items.indexOf(targetElement.textContent.trim());

    let nextItems = [...this.items];
    const [draggedItem] = nextItems.splice(draggedIndex, 1);

    let adjustedTargetIndex = targetIndex;
    if (draggedIndex < targetIndex) {
      adjustedTargetIndex--;
    }

    if (!before) {
      adjustedTargetIndex = adjustedTargetIndex + 1;
    }
    nextItems.splice(adjustedTargetIndex, 0, draggedItem);

    this.items = nextItems;
    await this.updateComplete;
    let movedItem = this.querySelectorAll("li")[adjustedTargetIndex];
    let focusableEl = this.getFocusableEl(movedItem);
    focusableEl?.focus();
  }

  getFocusableEl(item) {
    if (this.type == DRAG_SELECTOR) {
      return item.querySelector(this.selectors.dragSelector);
    }

    // Look for shadow DOM first, fallback to firstElementChild
    return (
      item.shadowRoot?.querySelector(this.selectors.itemSelector) ??
      item.firstElementChild
    );
  }

  handleReorder(e) {
    const { draggedElement, targetElement, position } = e.detail;
    this.reorderItems(draggedElement, targetElement, position === -1);
  }

  handleKeydown(e) {
    e.stopPropagation();
    const result = this.children[1].evaluateKeyDownEvent(e);
    if (!result) {
      return;
    }
    this.handleReorder({ detail: result });
  }

  addItem() {
    this.items = [...this.items, `Item ${this.items.length + 1}`];
  }

  get selectors() {
    switch (this.type) {
      case DEFAULT:
        return { itemSelector: "li" };
      case SHADOW_DOM:
        return { itemSelector: "#shadowed" };
      case DRAG_SELECTOR:
        return { itemSelector: "li", dragSelector: ".handle" };
      default:
        return {};
    }
  }

  contentTemplate(item) {
    if (this.type == DEFAULT) {
      return html`<button>${item}</button>`;
    } else if (this.type == DRAG_SELECTOR) {
      return html`<div class="draggable">
        <div class="handle" tabindex="0"></div>
        <span>${item}</span>
      </div>`;
    }
    return html`<shadow-demo item=${item}></shadow-demo>`;
  }

  render() {
    let { itemSelector, dragSelector } = this.selectors;
    return html`
      <style>
        ul {
          padding: 0;
          display: flex;
          flex-direction: column;
          gap: var(--space-small);
        }
        li {
          list-style: none;
        }
        .handle {
          width: var(--button-size-icon);
          height: var(--button-size-icon);
          cursor: pointer;
          background-image: url("chrome://global/skin/icons/more.svg");
          background-position: center;
          background-repeat: no-repeat;
          border-radius: var(--button-border-radius);
          -moz-context-properties: fill;
          fill: currentColor;
        }
        .draggable {
          border: var(--border-width) solid var(--border-color);
          border-radius: var(--border-radius-small);
          background-color: var(--background-color-box);
          display: flex;
          align-items: center;
          gap: var(--space-small);
        }
      </style>
      <moz-reorderable-list
        itemselector=${ifDefined(itemSelector)}
        dragselector=${ifDefined(dragSelector)}
        @reorder=${this.handleReorder}
        @keydown=${this.handleKeydown}
      >
        <ul>
          ${this.items.map(
            item => html` <li>${this.contentTemplate(item)}</li> `
          )}
        </ul>
      </moz-reorderable-list>
      <button @click=${this.addItem}>Add another item</button>
    `;
  }
}
customElements.define("reorderable-demo", ReorderableDemo);

const Template = ({ demoType }) => html`
  <reorderable-demo type=${demoType}></reorderable-demo>
`;

export const Default = Template.bind({});
Default.args = {
  demoType: DEFAULT,
};

export const ShadowDOM = Template.bind({});
ShadowDOM.args = {
  demoType: SHADOW_DOM,
};

export const DragSelector = Template.bind({});
DragSelector.args = {
  demoType: DRAG_SELECTOR,
};
