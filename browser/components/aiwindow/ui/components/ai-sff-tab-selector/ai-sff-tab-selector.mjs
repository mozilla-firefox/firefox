/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import {
  html,
  repeat,
  nothing,
} from "chrome://global/content/vendor/lit.all.mjs";
import { MAX_SELECTED_TABS } from "chrome://browser/content/aiwindow/modules/SmartFormFillConstants.mjs";

/**
 * Interface for choosing tabs to use in Smart Form Fill
 */
export class AiSffTabSelector extends MozLitElement {
  static properties = {
    suggestedTabs: { type: Array },
    otherTabs: { type: Array },
  };

  constructor() {
    super();

    this.suggestedTabs = [];
    this.otherTabs = [];
  }

  get #selectedTabIds() {
    return [...this.suggestedTabs, ...this.otherTabs]
      .filter(tab => tab.pressed)
      .map(tab => tab.id);
  }

  #updateTab(tabs, tabId, pressed) {
    return tabs.map(tab => (tab.id === tabId ? { ...tab, pressed } : tab));
  }

  #handleTabToggle(event, tabId) {
    const { pressed } = event.currentTarget;

    this.suggestedTabs = this.#updateTab(this.suggestedTabs, tabId, pressed);
    this.otherTabs = this.#updateTab(this.otherTabs, tabId, pressed);
  }

  #handleAccept() {
    const selectedTabIds = this.#selectedTabIds;
    if (!selectedTabIds.length || selectedTabIds.length > MAX_SELECTED_TABS) {
      return;
    }

    this.dispatchEvent(
      new CustomEvent("tabs-selected", {
        bubbles: true,
        composed: true,
        detail: { selectedTabIds },
      })
    );
  }

  #handleCancel() {
    this.dispatchEvent(
      new CustomEvent("cancel", {
        bubbles: true,
        composed: true,
      })
    );
  }

  #renderTabList(tabLabelFluentId, tabs, selectedTabCount, scrollable = false) {
    return html`
      <moz-box-group
        type="list"
        class=${scrollable
          ? "tab-selector-tabs-scrollable ai-smart-form-fill-other-tabs"
          : "ai-smart-form-fill-suggested-tabs"}
      >
        <moz-box-item slot="header">
          <h2
            class="tab-selector-list-heading"
            data-l10n-id=${tabLabelFluentId}
          ></h2>
        </moz-box-item>

        ${repeat(
          tabs,
          tab => tab.id,
          tab => html`
            <moz-box-item
              .label=${tab.title || tab.url}
              .description=${tab.url}
              .iconSrc=${tab.favicon}
            >
              <moz-toggle
                slot="actions"
                data-l10n-id="ai-smart-form-fill-tab-select-toggle"
                data-l10n-args=${JSON.stringify({
                  tabTitle: tab.title,
                })}
                .pressed=${tab.pressed}
                .disabled=${selectedTabCount >= MAX_SELECTED_TABS &&
                !tab.pressed}
                @toggle=${event => this.#handleTabToggle(event, tab.id)}
              ></moz-toggle>
            </moz-box-item>
          `
        )}
      </moz-box-group>
    `;
  }

  render() {
    const selectedTabCount = this.#selectedTabIds.length;

    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/aiwindow/components/ai-sff-tab-selector.css"
      />

      <div class="tab-selector-dialog">
        <h1
          class="tab-selector-title"
          data-l10n-id="ai-smart-form-fill-edit-sources-title"
        ></h1>

        ${this.suggestedTabs?.length
          ? this.#renderTabList(
              "ai-smart-form-fill-suggested-tabs-heading",
              this.suggestedTabs,
              selectedTabCount
            )
          : nothing}
        ${this.otherTabs?.length
          ? this.#renderTabList(
              "ai-smart-form-fill-other-tabs-heading",
              this.otherTabs,
              selectedTabCount,
              true
            )
          : nothing}

        <moz-button-group class="tab-selector-actions">
          <moz-button
            data-l10n-id="ai-smart-form-fill-cancel-tab-select-button"
            size="large"
            @click=${this.#handleCancel}
          ></moz-button>
          <moz-button
            data-l10n-id="ai-smart-form-fill-accept-tab-select-button"
            type="primary"
            size="large"
            .disabled=${selectedTabCount == 0 ||
            selectedTabCount > MAX_SELECTED_TABS}
            @click=${this.#handleAccept}
          ></moz-button>
        </moz-button-group>
      </div>
    `;
  }
}

customElements.define("ai-sff-tab-selector", AiSffTabSelector);
