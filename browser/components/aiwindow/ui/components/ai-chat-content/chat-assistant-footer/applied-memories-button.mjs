/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://global/content/elements/moz-box-group.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://global/content/elements/moz-box-item.mjs";

/**
 * AppliedMemoriesButton
 *
 * TODO: Currently using placeholder "Highlights" icon which will be replaced
 * with the memories icon once ready
 *
 * Custom element that renders the "Memories applied" pill and popover for
 * a single assistant message. The popover shows a list of applied
 * memories and allows the user to:
 *   - Remove an individual applied memory.
 *   - Retry the message without any applied memories.
 *   - Manage memories (links to about:preferences#manageMemories).
 *
 * @property {string|null} messageId
 *   Identifier for the assistant message this control belongs to.
 *
 * @property {Array<object>} appliedMemories
 *   List of applied memories for the message. The component will render up
 *   to the first 5 items in the popover.
 *
 * @property {boolean} open
 *   Whether the popover is currently open. This is typically controlled
 *   internally when the button is clicked and also reflected via the
 *   "toggle-applied-memories" event.
 *
 * @property {boolean} showCallout
 *   When true, the popover opens automatically and displays a callout banner.
 *   Set by the parent on the first message where memories are applied.
 *
 * Events dispatched:
 *   - "toggle-applied-memories"
 *       detail: { messageId, open }
 *   - "remove-applied-memory"
 *       detail: { memoryId }
 *   - "retry-without-memories"
 *       detail: { messageId }
 *   - "manage-memories"
 *   - "open-memories-learn-more"
 */
export class AppliedMemoriesButton extends MozLitElement {
  static properties = {
    messageId: { type: String, attribute: "message-id" },
    appliedMemories: { attribute: false },
    open: { type: Boolean, reflect: false },
    showCallout: { type: Boolean },
  };

  #showCalloutState = false;

  constructor() {
    super();
    this.messageId = null;
    this.appliedMemories = [];
    this.open = false;
    this.showCallout = false;

    this._onDocumentClick = this._onDocumentClick.bind(this);
    this._onKeyDown = this._onKeyDown.bind(this);
  }

  connectedCallback() {
    super.connectedCallback();
    document.addEventListener("click", this._onDocumentClick);
    this.addEventListener("keydown", this._onKeyDown);
  }

  disconnectedCallback() {
    document.removeEventListener("click", this._onDocumentClick);
    this.removeEventListener("keydown", this._onKeyDown);
    super.disconnectedCallback();
  }

  willUpdate(changedProperties) {
    super.willUpdate(changedProperties);
    if (changedProperties.has("showCallout") && this.showCallout) {
      this.#showCalloutState = true;
    }
  }

  updated(changedProperties) {
    super.updated(changedProperties);

    if (changedProperties.has("showCallout")) {
      this.#syncCalloutOpenState();
    }
  }

  #syncCalloutOpenState() {
    // If showCallout is true and the popover is not already open, force it open.
    if (!this.showCallout || this.open) {
      return;
    }

    this.open = true;
    this.toggleAttribute("data-open", true);
    this.updateComplete.then(() => this.#focusItemAt(0));
    this.#dispatchToggleAppliedMemories({ isOpen: true });
  }

  #dispatchToggleAppliedMemories({ isOpen }) {
    this.dispatchEvent(
      new CustomEvent("toggle-applied-memories", {
        bubbles: true,
        composed: true,
        detail: {
          messageId: this.messageId,
          open: isOpen,
        },
      })
    );
  }

  get _hasMemories() {
    return Array.isArray(this.appliedMemories) && !!this.appliedMemories.length;
  }

  get _visibleMemories() {
    return this.appliedMemories.slice(0, 5);
  }

  #onTriggerClick(event) {
    event.stopPropagation();
    if (!this._hasMemories) {
      return;
    }

    this.open = !this.open;
    if (!this.open) {
      this.#showCalloutState = false;
    }
    this.toggleAttribute("data-open", this.open);

    if (this.open) {
      this.updateComplete.then(() => this.#focusItemAt(0));
    }

    this.#dispatchToggleAppliedMemories({ isOpen: this.open });
  }

  _onPopoverClick(event) {
    event.stopPropagation();
  }

  _onDocumentClick() {
    if (!this.open) {
      return;
    }
    this.#closePopover();
  }

  _onKeyDown(event) {
    if (!this.open) {
      return;
    }
    switch (event.key) {
      case "Escape":
        event.preventDefault();
        event.stopPropagation();
        this.#closePopover();
        this.shadowRoot.querySelector(".memories-trigger")?.focus();
        break;
      case "Tab":
        this.#closePopover();
        this.shadowRoot.querySelector(".memories-trigger")?.focus();
        break;
      case "ArrowDown":
        event.preventDefault();
        this.#moveFocus(1);
        break;
      case "ArrowUp":
        event.preventDefault();
        this.#moveFocus(-1);
        break;
      case "Home":
        event.preventDefault();
        this.#focusItemAt(0);
        break;
      case "End":
        event.preventDefault();
        this.#focusItemAt(-1);
        break;
    }
  }

  get #menuItems() {
    const popover = this.shadowRoot.querySelector(".popover");
    return popover ? [...popover.querySelectorAll("[data-focusable]")] : [];
  }

  #moveFocus(direction) {
    const items = this.#menuItems;
    if (!items.length) {
      return;
    }
    const active = this.shadowRoot.activeElement;
    const currentIndex = items.indexOf(active);
    const nextIndex = (currentIndex + direction + items.length) % items.length;
    this.#focusItemAt(nextIndex);
  }

  #focusItemAt(index) {
    const items = this.#menuItems;
    if (!items.length) {
      return;
    }
    if (index < 0) {
      index = items.length + index;
    }
    items.forEach((item, i) => {
      item.tabIndex = i === index ? 0 : -1;
    });
    items[index].focus();
  }

  #closePopover() {
    this.open = false;
    this.#showCalloutState = false;
    this.toggleAttribute("data-open", false);
    this.requestUpdate();

    this.#dispatchToggleAppliedMemories({ isOpen: false });
  }

  _onRemoveMemory(event, memory) {
    event.stopPropagation();

    this.dispatchEvent(
      new CustomEvent("remove-applied-memory", {
        bubbles: true,
        composed: true,
        detail: {
          memory,
          messageId: this.messageId,
        },
      })
    );
  }

  _onRetryWithoutMemories(event) {
    event.stopPropagation();

    this.dispatchEvent(
      new CustomEvent("retry-without-memories", {
        bubbles: true,
        composed: true,
        detail: {
          messageId: this.messageId,
        },
      })
    );
  }

  _onManageMemories() {
    this.dispatchEvent(
      new CustomEvent("manage-memories", {
        bubbles: true,
        composed: true,
      })
    );
  }

  renderCallout() {
    return html`
      <div class="memories-callout">
        <p
          class="memories-callout-description"
          data-l10n-id="aiwindow-memories-callout-description"
        ></p>
        <button
          class="memories-callout-learn-more"
          role="menuitem"
          data-focusable
          data-l10n-id="aiwindow-memories-learn-more"
          @click=${() => {
            this.dispatchEvent(
              new CustomEvent("open-memories-learn-more", {
                bubbles: true,
                composed: true,
              })
            );
          }}
        ></button>
      </div>
    `;
  }

  renderPopover() {
    if (!this._hasMemories) {
      return nothing;
    }

    const isOpen = this.open || this.#showCalloutState;
    const visibleMemories = this._visibleMemories;

    return html`
      <div
        class="popover ${isOpen ? "open" : ""}"
        role="menu"
        data-l10n-id="aiwindow-applied-memories-popover"
        data-l10n-attrs="aria-label"
        ?inert=${!isOpen}
        @click=${event => this._onPopoverClick(event)}
      >
        ${this.#showCalloutState ? this.renderCallout() : nothing}

        <moz-box-group class="memories-list" role="none">
          ${visibleMemories.map(memory => {
            // @todo Bug 2010069
            // Localize aria-label
            return html`
              <moz-box-item
                class="memories-list-item"
                .label=${memory.memory_summary}
              >
                <moz-button
                  class="memories-remove-button"
                  role="menuitem"
                  data-focusable
                  type="ghost"
                  size="small"
                  iconsrc="chrome://global/skin/icons/delete.svg"
                  aria-label="Remove this memory"
                  @click=${event => this._onRemoveMemory(event, memory)}
                  slot="actions"
                ></moz-button>
              </moz-box-item>
            `;
          })}
        </moz-box-group>

        <div id="manage-memories-row" class="popover-action-row" role="none">
          <moz-button
            type="ghost"
            size="default"
            iconsrc="chrome://global/skin/icons/settings.svg"
            iconposition="start"
            role="menuitem"
            data-focusable
            class="popover-action-row-button manage-memories-button"
            data-l10n-id="aiwindow-manage-memories"
            data-l10n-attrs="label"
            @click=${() => this._onManageMemories()}
          ></moz-button>
        </div>

        <div
          id="retry-without-memories-row"
          class="popover-action-row"
          role="none"
        >
          <moz-button
            type="ghost"
            size="default"
            iconsrc="chrome://global/skin/icons/reload.svg"
            iconposition="start"
            role="menuitem"
            data-focusable
            class="popover-action-row-button retry-without-memories-button"
            data-l10n-id="aiwindow-retry-without-memories"
            data-l10n-attrs="label"
            @click=${event => this._onRetryWithoutMemories(event)}
          ></moz-button>
        </div>
      </div>
    `;
  }

  render() {
    if (!this._hasMemories) {
      return null;
    }

    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/applied-memories-button.css"
      />
      <moz-button
        class="memories-trigger"
        type="ghost"
        size="small"
        iconposition="start"
        iconsrc="chrome://browser/content/aiwindow/assets/memories-on.svg"
        aria-haspopup="menu"
        aria-expanded=${this.open && this._hasMemories}
        data-l10n-id="aiwindow-memories-used"
        data-l10n-attrs="label"
        @click=${event => this.#onTriggerClick(event)}
      ></moz-button>

      ${this.renderPopover()}
    `;
  }
}

customElements.define("applied-memories-button", AppliedMemoriesButton);
