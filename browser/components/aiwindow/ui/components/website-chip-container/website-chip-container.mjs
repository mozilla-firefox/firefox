/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import {
  CONTEXT_MENTION_TYPE,
  getTabGroupMentionId,
} from "chrome://browser/content/urlbar/SmartbarMentionUtils.mjs";
import {
  html,
  nothing,
  repeat,
} from "chrome://global/content/vendor/lit.all.mjs";
import { SmartwindowOverflowRowMixin } from "chrome://browser/content/aiwindow/components/SmartwindowOverflowRow.mjs";

// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/smartwindow-chip.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/ai-grouped-chip-container.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/smartwindow-panel-list.mjs";

/** @typedef {import("chrome://browser/content/urlbar/SmartbarInput.mjs").ContextWebsite} ContextWebsite */

/**
 * Container for rendering website chips
 */
export class WebsiteChipContainer extends SmartwindowOverflowRowMixin(
  MozLitElement
) {
  static properties = {
    websites: { type: Array },
    removable: { type: Boolean },
    shouldGroupChips: { type: Boolean }, // true if we want 3 or more chips to display as ai-grouped-chip-container
    autoOverflow: { type: Boolean },
    visibleChipCount: { type: Number },
    isPanelOpen: { type: Boolean, state: true },
  };

  constructor() {
    super();
    /** @type {ContextWebsite[]} */
    this.websites = [];
    this.removable = false;
    this.shouldGroupChips = false;
    this.autoOverflow = false;
    this.visibleChipCount = null;
    this.isPanelOpen = false;
  }

  get overflowContainerSelector() {
    return ".chip-container-scroller";
  }

  get inlineItemCount() {
    return this.visibleChipCount;
  }

  get overflowItems() {
    return this.#isAutoOverflowing ? this.websites : [];
  }

  get #isGrouped() {
    return this.shouldGroupChips && this.websites.length > 2;
  }

  get #isAutoOverflowing() {
    return this.autoOverflow && !this.#isGrouped;
  }

  #panel() {
    return this.renderRoot.querySelector("smartwindow-panel-list");
  }

  #onToggleClick(event) {
    const panel = this.#panel();
    if (panel) {
      panel.anchor = event.currentTarget;
      panel.toggle(event);
    }
  }

  #onOverflowItemSelected(event) {
    const { id: url, type } = event.detail ?? {};
    this.#panel()?.hide();
    // A tab group row has an id so the panel can key it, but no page to open.
    if (!url || type == CONTEXT_MENTION_TYPE.TAB_GROUP) {
      return;
    }
    this.dispatchEvent(
      new CustomEvent("AIChatContent:OpenLink", {
        bubbles: true,
        composed: true,
        detail: { url, preferSwitchToTab: true },
      })
    );
  }

  #onRemoveWebsite(website, event) {
    event.stopPropagation();
    this.dispatchEvent(
      new CustomEvent("smartwindow-chip:remove", {
        bubbles: true,
        composed: true,
        detail: {
          url: website.url,
          groupId: website.groupId,
          label: website.label,
        },
      })
    );
  }

  #renderStackedChips(website) {
    return html`<smartwindow-chip
      type="context"
      .label=${website.label}
      .href=${website.url ?? ""}
      .iconSrc=${website.iconSrc ?? ""}
      .isTabGroup=${website.type == CONTEXT_MENTION_TYPE.TAB_GROUP}
      .tabGroupColor=${website.color ?? ""}
      .tabGroupId=${website.groupId ?? ""}
      .removable=${this.removable}
      @smartwindow-chip:remove=${e => this.#onRemoveWebsite(website, e)}
    ></smartwindow-chip>`;
  }

  #renderGroupedChips(chips) {
    return html`<ai-grouped-chip-container
      .chips=${chips}
    ></ai-grouped-chip-container>`;
  }

  #renderChip(website) {
    return website.historyDeleted
      ? html`<div class="chip-history-deleted">
          <img
            class="chip-history-deleted-icon"
            src="chrome://global/skin/icons/defaultFavicon.svg"
            alt=""
          />
          <span data-l10n-id="aiwindow-website-chip-history-deleted"></span>
        </div>`
      : this.#renderStackedChips(website);
  }

  #renderSmartwindowOverflowRow() {
    const visibleCount = Math.min(this.visibleCount, this.websites.length);
    const overflow = this.websites.slice(visibleCount);
    const hasOverflow = !!overflow.length;

    return html`<div
      class="chip-container-scroller smartwindow-overflow-row"
      role="list"
    >
      ${repeat(
        this.websites,
        website => website,
        (website, index) =>
          html`<span role="listitem" ?data-overflow=${index >= visibleCount}
            >${this.#renderChip(website)}</span
          >`
      )}
      <moz-button
        class="overflow-more"
        type="ghost"
        ?data-overflow=${!hasOverflow}
        .ariaHasPopup=${"menu"}
        .ariaExpanded=${String(this.isPanelOpen)}
        data-l10n-id="smartwindow-assistant-citations-more-label"
        data-l10n-args=${JSON.stringify({ count: overflow.length })}
        @click=${this.#onToggleClick}
      ></moz-button>
      ${hasOverflow
        ? html`<smartwindow-panel-list
            .groups=${[
              {
                items: overflow.map(website => ({
                  id: website.url ?? getTabGroupMentionId(website.groupId),
                  type: website.type,
                  label: website.label,
                  icon: website.iconSrc,
                  color: website.color,
                })),
              },
            ]}
            @shown=${() => (this.isPanelOpen = true)}
            @hidden=${() => (this.isPanelOpen = false)}
            @item-selected=${this.#onOverflowItemSelected}
          ></smartwindow-panel-list>`
        : nothing}
    </div>`;
  }

  #renderScrollerRow() {
    return html`<div class="chip-container-scroller" role="list">
      ${repeat(
        this.websites,
        website => website,
        website =>
          html`<span role="listitem">${this.#renderChip(website)}</span>`
      )}
    </div>`;
  }

  #renderChips() {
    if (this.#isGrouped) {
      return this.#renderGroupedChips(this.websites);
    }
    return this.#isAutoOverflowing
      ? this.#renderSmartwindowOverflowRow()
      : this.#renderScrollerRow();
  }

  render() {
    if (!this.websites.length) {
      return nothing;
    }

    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/smartwindow-overflow-row.css"
      />
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/website-chip-container.css"
      />
      <div class="chip-container">${this.#renderChips()}</div>
    `;
  }
}

customElements.define("website-chip-container", WebsiteChipContainer);
