/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

import {
  classMap,
  html,
  ifDefined,
  when,
} from "chrome://global/content/vendor/lit.all.mjs";
import { navigateToLink } from "chrome://browser/content/firefoxview/helpers.mjs";

import { SidebarPage } from "./sidebar-page.mjs";

ChromeUtils.defineESModuleGetters(lazy, {
  HistoryController: "resource:///modules/HistoryController.sys.mjs",
  Sanitizer: "resource:///modules/Sanitizer.sys.mjs",
});

const NEVER_REMEMBER_HISTORY_PREF = "browser.privatebrowsing.autostart";
const DAYS_EXPANDED_INITIALLY = 2;

export class SidebarHistory extends SidebarPage {
  static queries = {
    cards: { all: "moz-card" },
    emptyState: "fxview-empty-state",
    lists: { all: "sidebar-tab-list" },
    menuButton: ".menu-button",
    searchTextbox: "fxview-search-textbox",
  };

  constructor() {
    super();
    this.handlePopupEvent = this.handlePopupEvent.bind(this);
  }

  controller = new lazy.HistoryController(this, {
    component: "sidebar",
  });

  connectedCallback() {
    super.connectedCallback();
    const { document: doc } = this.topWindow;
    this._menu = doc.getElementById("sidebar-history-menu");
    this._menuSortByDate = doc.getElementById("sidebar-history-sort-by-date");
    this._menuSortBySite = doc.getElementById("sidebar-history-sort-by-site");
    this._menuSortByDateSite = doc.getElementById(
      "sidebar-history-sort-by-date-and-site"
    );
    this._menuSortByLastVisited = doc.getElementById(
      "sidebar-history-sort-by-last-visited"
    );
    this._menu.addEventListener("command", this);
    this._menu.addEventListener("popuphidden", this.handlePopupEvent);
    this.addContextMenuListeners();
    this.addSidebarFocusedListeners();
    this.controller.updateCache();
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this._menu.removeEventListener("command", this);
    this._menu.removeEventListener("popuphidden", this.handlePopupEvent);
    this.removeContextMenuListeners();
    this.removeSidebarFocusedListeners();
  }

  handleContextMenuEvent(e) {
    this.triggerNode = this.findTriggerNode(e, "sidebar-tab-row");
    if (!this.triggerNode) {
      e.preventDefault();
    }
  }

  handleCommandEvent(e) {
    switch (e.target.id) {
      case "sidebar-history-sort-by-date":
        this.controller.onChangeSortOption(e, "date");
        break;
      case "sidebar-history-sort-by-site":
        this.controller.onChangeSortOption(e, "site");
        break;
      case "sidebar-history-sort-by-date-and-site":
        this.controller.onChangeSortOption(e, "datesite");
        break;
      case "sidebar-history-sort-by-last-visited":
        this.controller.onChangeSortOption(e, "lastvisited");
        break;
      case "sidebar-history-clear":
        lazy.Sanitizer.showUI(this.topWindow);
        break;
      case "sidebar-history-context-delete-page":
        this.controller.deleteFromHistory();
        break;
      default:
        super.handleCommandEvent(e);
        break;
    }
  }

  // We should let moz-button handle this, see bug 1875374.
  handlePopupEvent(e) {
    if (e.type == "popuphidden") {
      this.menuButton.setAttribute("aria-expanded", false);
    }
  }

  handleSidebarFocusedEvent() {
    this.searchTextbox?.focus();
  }

  onPrimaryAction(e) {
    navigateToLink(e);
  }

  onSecondaryAction(e) {
    this.triggerNode = e.detail.item;
    this.controller.deleteFromHistory();
  }

  handleCardKeydown(e) {
    if (e.originalTarget != e.target.summaryEl) {
      return;
    }
    let nextSibling = e.target.nextElementSibling;
    let prevSibling = e.target.previousElementSibling;
    switch (e.code) {
      case "Tab":
        if (prevSibling.localName == "moz-card") {
          e.preventDefault();
        }
        break;
      case "ArrowUp":
        if (!prevSibling || prevSibling.localName !== "moz-card") {
          const { classList, parentElement: dateCard } = e.target;
          if (classList.contains("nested-card")) {
            // Going up from the first site card. Focus the date header.
            dateCard.summaryEl.focus();
          }
          return;
        }
        if (prevSibling.expanded) {
          let innerElement = prevSibling.contentSlotEl.assignedElements()[0];
          if (innerElement.classList.contains("nested-card")) {
            // Going up from a date header. Focus the last site card from the
            // date card above this one.
            const prevSite = prevSibling.lastElementChild;
            if (prevSite.expanded) {
              const prevTabList = prevSite.contentSlotEl.assignedElements()[0];
              const lastRow = prevTabList.rowEls[prevTabList.rowEls.length - 1];
              lastRow.focus();
            } else {
              prevSite.summaryEl.focus();
            }
          } else {
            // Not sorted by Date & Site, innerElement is a SidebarTabList.
            let lastRow = innerElement.rowEls[innerElement.rowEls.length - 1];
            lastRow.focus();
          }
        } else {
          prevSibling.summaryEl.focus();
        }
        break;
      case "ArrowDown":
        if (e.target.expanded) {
          let innerElement = e.target.contentSlotEl.assignedElements()[0];
          if (innerElement.classList.contains("nested-card")) {
            // Going down from a date header. Focus the first site card.
            innerElement.summaryEl.focus();
          } else {
            // Not sorted by Date & Site, innerElement is a SidebarTabList.
            innerElement.rowEls[0].focus();
          }
        } else if (nextSibling && nextSibling.localName == "moz-card") {
          nextSibling.summaryEl.focus();
        } else if (e.target.classList.contains("last-card")) {
          // Going down from the last site card. Focus the next date header.
          const dateCard = e.target.parentElement;
          const nextDate = dateCard.nextElementSibling;
          nextDate?.summaryEl.focus();
        }
        break;
      case "ArrowLeft":
        e.target.expanded = false;
        break;
      case "ArrowRight":
        e.target.expanded = true;
        break;
    }
  }

  /**
   * The template to use for cards-container.
   */
  get cardsTemplate() {
    if (this.controller.isHistoryPending) {
      // don't render cards until initial history visits entries are available
      return "";
    } else if (this.controller.searchResults) {
      return this.#searchResultsTemplate();
    } else if (!this.controller.isHistoryEmpty) {
      return this.#historyCardsTemplate();
    }
    return this.#emptyMessageTemplate();
  }

  #historyCardsTemplate() {
    const { historyVisits } = this.controller;
    switch (this.controller.sortOption) {
      case "date":
        return historyVisits.map(({ l10nId, items }, i) =>
          this.#dateCardTemplate(l10nId, i, items)
        );
      case "site":
        return historyVisits.map(({ domain, items }, i) =>
          this.#siteCardTemplate(domain, i, items)
        );
      case "datesite":
        return historyVisits.map(({ l10nId, items }, i) =>
          this.#dateCardTemplate(l10nId, i, items, true)
        );
      case "lastvisited":
        return historyVisits.map(
          ({ items }) =>
            html`<moz-card>
              ${this.#tabListTemplate(this.getTabItems(items))}
            </moz-card>`
        );
      default:
        return [];
    }
  }

  #dateCardTemplate(l10nId, index, items, isDateSite = false) {
    const tabIndex = index > 0 ? "-1" : undefined;
    return html` <moz-card
      type="accordion"
      class="date-card"
      ?expanded=${index < DAYS_EXPANDED_INITIALLY}
      data-l10n-id=${l10nId}
      data-l10n-args=${JSON.stringify({
        date: isDateSite ? items[0][1][0].time : items[0].time,
      })}
      @keydown=${this.handleCardKeydown}
      tabindex=${ifDefined(tabIndex)}
    >
      ${isDateSite
        ? items.map(([domain, visits], i) =>
            this.#siteCardTemplate(
              domain,
              i,
              visits,
              true,
              i == items.length - 1
            )
          )
        : this.#tabListTemplate(this.getTabItems(items))}
    </moz-card>`;
  }

  #siteCardTemplate(
    domain,
    index,
    items,
    isDateSite = false,
    isLastCard = false
  ) {
    let tabIndex = index > 0 || isDateSite ? "-1" : undefined;
    return html` <moz-card
      class=${classMap({
        "last-card": isLastCard,
        "nested-card": isDateSite,
        "site-card": true,
      })}
      type="accordion"
      ?expanded=${!isDateSite}
      heading=${domain}
      @keydown=${this.handleCardKeydown}
      tabindex=${ifDefined(tabIndex)}
    >
      ${this.#tabListTemplate(this.getTabItems(items))}
    </moz-card>`;
  }

  #emptyMessageTemplate() {
    let descriptionHeader;
    let descriptionLabels;
    let descriptionLink;
    if (Services.prefs.getBoolPref(NEVER_REMEMBER_HISTORY_PREF, false)) {
      // History pref set to never remember history
      descriptionHeader = "firefoxview-dont-remember-history-empty-header-2";
      descriptionLabels = [
        "firefoxview-dont-remember-history-empty-description-one",
      ];
      descriptionLink = {
        url: "about:preferences#privacy",
        name: "history-settings-url-two",
      };
    } else {
      descriptionHeader = "firefoxview-history-empty-header";
      descriptionLabels = [
        "firefoxview-history-empty-description",
        "firefoxview-history-empty-description-two",
      ];
      descriptionLink = {
        url: "about:preferences#privacy",
        name: "history-settings-url",
      };
    }
    return html`
      <fxview-empty-state
        headerLabel=${descriptionHeader}
        .descriptionLabels=${descriptionLabels}
        .descriptionLink=${descriptionLink}
        class="empty-state history"
        isSelectedTab
        mainImageUrl="chrome://browser/content/firefoxview/history-empty.svg"
        openLinkInParentWindow
      >
      </fxview-empty-state>
    `;
  }

  #searchResultsTemplate() {
    return html` <moz-card
      data-l10n-id="sidebar-search-results-header"
      data-l10n-args=${JSON.stringify({
        query: this.controller.searchQuery,
      })}
    >
      <div>
        ${when(
          this.controller.searchResults.length,
          () =>
            html`<h3
              slot="secondary-header"
              data-l10n-id="firefoxview-search-results-count"
              data-l10n-args=${JSON.stringify({
                count: this.controller.searchResults.length,
              })}
            ></h3>`
        )}
        ${this.#tabListTemplate(
          this.getTabItems(this.controller.searchResults),
          this.controller.searchQuery
        )}
      </div>
    </moz-card>`;
  }

  #tabListTemplate(tabItems, searchQuery) {
    return html`<sidebar-tab-list
      .handleFocusElementToCard=${this.handleFocusElementToCard}
      maxTabsLength="-1"
      .searchQuery=${searchQuery}
      secondaryActionClass="delete-button"
      .sortOption=${this.controller.sortOption}
      .tabItems=${tabItems}
      @fxview-tab-list-primary-action=${this.onPrimaryAction}
      @fxview-tab-list-secondary-action=${this.onSecondaryAction}
    >
    </sidebar-tab-list>`;
  }

  onSearchQuery(e) {
    this.controller.onSearchQuery(e);
  }

  getTabItems(items) {
    return items.map(item => ({
      ...item,
      secondaryL10nId: "sidebar-history-delete",
      secondaryL10nArgs: null,
    }));
  }

  openMenu(e) {
    const menuPos = this.sidebarController._positionStart
      ? "after_start" // Sidebar is on the left. Open menu to the right.
      : "after_end"; // Sidebar is on the right. Open menu to the left.
    this._menu.openPopup(e.target, menuPos, 0, 0, false, false, e);
    this.menuButton.setAttribute("aria-expanded", true);
  }

  willUpdate() {
    this._menuSortByDate.setAttribute(
      "checked",
      this.controller.sortOption == "date"
    );
    this._menuSortBySite.setAttribute(
      "checked",
      this.controller.sortOption == "site"
    );
    this._menuSortByDateSite.setAttribute(
      "checked",
      this.controller.sortOption == "datesite"
    );
    this._menuSortByLastVisited.setAttribute(
      "checked",
      this.controller.sortOption == "lastvisited"
    );
  }

  render() {
    return html`
      ${this.stylesheet()}
      <link
        rel="stylesheet"
        href="chrome://browser/content/sidebar/sidebar-history.css"
      />
      <div class="sidebar-panel">
        <sidebar-panel-header
          data-l10n-id="sidebar-menu-history-header"
          data-l10n-attrs="heading"
          view="viewHistorySidebar"
        >
        </sidebar-panel-header>
        <div class="options-container">
          <fxview-search-textbox
            data-l10n-id="firefoxview-search-text-box-history"
            data-l10n-attrs="placeholder"
            @fxview-search-textbox-query=${this.onSearchQuery}
            .size=${15}
          ></fxview-search-textbox>
          <moz-button
            class="menu-button"
            @click=${this.openMenu}
            data-l10n-id="sidebar-options-menu-button"
            aria-haspopup="menu"
            aria-expanded="false"
            view=${this.view}
            type="icon ghost"
            iconsrc="chrome://global/skin/icons/more.svg"
          >
          </moz-button>
        </div>
        ${this.cardsTemplate}
      </div>
    `;
  }
}

customElements.define("sidebar-history", SidebarHistory);
