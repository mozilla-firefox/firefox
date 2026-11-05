/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, when } from "chrome://global/content/vendor/lit.all.mjs";
import { escapeHtmlEntities } from "./helpers.mjs";
import { ViewPage } from "./viewpage.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/firefoxview/chats-tab-list.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  AIWindowUI:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs",
  ChatsController: "resource:///modules/ChatsController.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", function () {
  return console.createInstance({
    logModule: "ChatHistory",
    maxLogLevelPref: "browser.smartwindow.chatHistory.loglevel",
  });
});

const SEARCH_RESULTS_LIMIT = 300;

class ChatsInView extends ViewPage {
  static properties = {};

  static queries = {
    cards: { all: "card-container:not([hidden])" },
    emptyState: "fxview-empty-state",
    lists: { all: "chats-tab-list" },
    searchTextbox: "moz-input-search",
    panelList: "panel-list",
  };

  constructor() {
    super();
    this._started = false;
    // Setting maxTabsLength to -1 for no max
    this.maxTabsLength = -1;
    this.fullyUpdated = false;
    this.cumulativeSearches = 0;
  }

  controller = new lazy.ChatsController(this, {
    searchResultsLimit: SEARCH_RESULTS_LIMIT,
  });

  disconnectedCallback() {
    super.disconnectedCallback();
    this.stop();
  }

  viewVisibleCallback() {
    this.start();
  }

  viewHiddenCallback() {
    this.stop();
  }

  willUpdate() {
    this.fullyUpdated = false;
  }

  updated() {
    this.fullyUpdated = true;
    if (this.lists?.length) {
      this.toggleVisibilityInCardContainer();
    }
  }

  async getUpdateComplete() {
    await super.getUpdateComplete();
    await Promise.all(Array.from(this.cards).map(card => card.updateComplete));
  }

  start() {
    if (this._started) {
      return;
    }
    this._started = true;

    this.controller.updateCache();

    this.toggleVisibilityInCardContainer();
  }

  stop() {
    if (!this._started) {
      return;
    }
    this._started = false;

    this.toggleVisibilityInCardContainer();
  }

  async onPrimaryAction(event) {
    event.preventDefault();

    const item = event.detail?.item;
    const convId = item?.convId;

    if (!convId) {
      lazy.log.error(
        "No conversation ID found in clicked item",
        event.message,
        event.stack
      );
      return;
    }

    const conversation =
      await lazy.AIWindow.chatStore.findConversationById(convId);

    if (!conversation) {
      lazy.log.error("Conversation not found:", convId);
      return;
    }

    lazy.AIWindowUI.reopenConversationInTab(this.getWindow(), conversation);

    if (this.controller.searchQuery) {
      this.cumulativeSearches = 0;
    }
  }

  onSecondaryAction(e) {
    this.triggerNode = e.originalTarget;
    this.panelList.toggle(e.detail.originalEvent);
  }

  deleteChat(e) {
    this.controller
      .deleteChat()
      .catch(
        lazy.log.error("Could not delete conversation.", e.message, e.stack)
      );
  }

  onSearchQuery(e) {
    this.controller.onSearchQuery(e);
    this.cumulativeSearches = this.controller.searchQuery
      ? this.cumulativeSearches + 1
      : 0;
  }

  panelListTemplate() {
    return html`
      <panel-list slot="menu" data-tab-type="chat">
        <panel-item
          @click=${this.deleteChat}
          data-l10n-id="firefoxview-chat-context-delete"
          data-l10n-attrs="accesskey"
        ></panel-item>
      </panel-list>
    `;
  }

  /**
   * The template to use for cards-container.
   */
  get cardsTemplate() {
    if (this.controller.searchQuery) {
      return this.#searchResultsTemplate();
    } else if (!this.controller.isChatEmpty) {
      return this.#chatCardsTemplate();
    }
    return this.#emptyMessageTemplate();
  }

  #chatCardsTemplate() {
    return this.controller.totalChats.map(chat => {
      let dateArg = JSON.stringify({ date: chat.items[0].time });
      return html`<card-container>
        <h3
          slot="header"
          data-l10n-id=${chat.l10nId}
          data-l10n-args=${dateArg}
        ></h3>
        <chats-tab-list
          slot="main"
          .updatesPaused=${false}
          secondaryActionClass="options-button"
          dateTimeFormat=${chat.l10nId.includes("prev-month")
            ? "dateTime"
            : "time"}
          hasPopup="menu"
          maxTabsLength=${this.maxTabsLength}
          .tabItems=${chat.items}
          @fxview-tab-list-primary-action=${this.onPrimaryAction}
          @fxview-tab-list-secondary-action=${this.onSecondaryAction}
        >
        </chats-tab-list>
      </card-container>`;
    });
  }

  #emptyMessageTemplate() {
    const descriptionHeader = "firefoxview-chats-empty-header";
    const descriptionLabels = ["firefoxview-chats-empty-description"];

    let asset = Services.prefs.getBoolPref("browser.nova.enabled", false)
      ? "chrome://browser/skin/sidebar/kit-page-history.svg"
      : "chrome://browser/content/firefoxview/history-empty.svg";

    return html`
      <fxview-empty-state
        headerLabel=${descriptionHeader}
        .descriptionLabels=${descriptionLabels}
        class="empty-state chats"
        ?isSelectedTab=${this.selectedTab}
        mainImageUrl=${asset}
      >
      </fxview-empty-state>
    `;
  }

  #searchResultsTemplate() {
    return html` <card-container toggleDisabled>
      <h3
        slot="header"
        data-l10n-id="firefoxview-search-results-header"
        data-l10n-args=${JSON.stringify({
          query: escapeHtmlEntities(this.controller.searchQuery),
        })}
      ></h3>
      ${when(
        this.controller.searchResults.length,
        () =>
          html`<h3
            slot="secondary-header"
            data-l10n-id="firefoxview-search-chat-results-count"
            data-l10n-args=${JSON.stringify({
              count: this.controller.searchResults.length,
            })}
          ></h3>`
      )}
      <chats-tab-list
        slot="main"
        .updatesPaused=${false}
        secondaryActionClass="options-button"
        dateTimeFormat="dateTime"
        hasPopup="menu"
        maxTabsLength="-1"
        .searchQuery=${this.controller.searchQuery}
        .tabItems=${this.controller.searchResults}
        @fxview-tab-list-primary-action=${this.onPrimaryAction}
        @fxview-tab-list-secondary-action=${this.onSecondaryAction}
      >
      </chats-tab-list>
    </card-container>`;
  }

  render() {
    if (!this.selectedTab) {
      return null;
    }
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/firefoxview/firefoxview.css"
      />
      ${this.panelListTemplate()}
      <div class="sticky-container bottom-fade">
        <h2 class="page-header" data-l10n-id="firefoxview-chats-header"></h2>
        <moz-input-search
          data-l10n-id="firefoxview-search-text-box-chats"
          data-l10n-attrs="placeholder"
          @MozInputSearch:search=${this.onSearchQuery}
        ></moz-input-search>
      </div>
      <div class="cards-container">${this.cardsTemplate}</div>
    `;
  }
}
customElements.define("view-chats", ChatsInView);
