/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

import {
  classMap,
  html,
  ifDefined,
  when,
  nothing,
} from "chrome://global/content/vendor/lit.all.mjs";
import { navigateToLink } from "chrome://browser/content/firefoxview/helpers.mjs";

import { SidebarPage } from "./sidebar-page.mjs";

ChromeUtils.defineESModuleGetters(lazy, {
  HistoryController: "resource:///modules/HistoryController.sys.mjs",
  Sanitizer: "resource:///modules/Sanitizer.sys.mjs",
  SidebarTreeView:
    "moz-src:///browser/components/sidebar/SidebarTreeView.sys.mjs",
  OpenInTabsUtils:
    "moz-src:///browser/components/tabbrowser/OpenInTabsUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  PlacesUIUtils: "moz-src:///browser/components/places/PlacesUIUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
});

const NEVER_REMEMBER_HISTORY_PREF = "browser.privatebrowsing.autostart";
const SORT_OPTION_PREF = "sidebar.history.sortOption";
const DAYS_EXPANDED_INITIALLY = 2;

export class SidebarHistory extends SidebarPage {
  static queries = {
    cards: { all: "moz-card" },
    emptyState: "fxview-empty-state",
    lists: { all: "sidebar-tab-list" },
    menuButton: ".menu-button",
    searchTextbox: "moz-input-search",
  };

  constructor() {
    super();
    this.handlePopupEvent = this.handlePopupEvent.bind(this);
    this.controller = new lazy.HistoryController(this, {
      component: "sidebar",
      sortOption: Services.prefs.getStringPref(SORT_OPTION_PREF, "date"),
    });
    this.treeView = new lazy.SidebarTreeView(this);
  }

  connectedCallback() {
    super.connectedCallback();
    PlacesObservers.addListener(
      ["page-removed", "history-cleared"],
      this.#placesRemovedObserver
    );
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
    this._contextMenu.addEventListener("popupshowing", this);
    this.addContextMenuListeners();
    this.addSidebarFocusedListeners();
    this.controller.updateCache();
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    PlacesObservers.removeListener(
      ["page-removed", "history-cleared"],
      this.#placesRemovedObserver
    );
    this._menu.removeEventListener("command", this);
    this._menu.removeEventListener("popuphidden", this.handlePopupEvent);
    this._contextMenu.removeEventListener("popupshowing", this);
    this.removeContextMenuListeners();
    this.removeSidebarFocusedListeners();
  }

  handleEvent(e) {
    switch (e.type) {
      case "popupshowing":
        this.updateContextMenu();
        break;
      default:
        super.handleEvent(e);
    }
  }

  #placesRemovedObserver = () => {
    this.treeView.resetSelection();
  };

  get isMultipleRowsSelected() {
    return this.treeView.getSelectedTabItems().length > 1;
  }

  /**
   * Only show multiselect commands when multiple items are selected.
   */
  updateContextMenu() {
    for (const child of this._contextMenu.children) {
      let shouldHide = false;
      const isMultiSelectCommand = child.classList.contains(
        "sidebar-history-multiselect-command"
      );
      const isPrivateWindowMenuItem =
        child.id === "sidebar-history-context-open-in-private-window";
      if (this.isMultipleRowsSelected !== isMultiSelectCommand) {
        shouldHide = true;
      }
      if (isPrivateWindowMenuItem && !lazy.PrivateBrowsingUtils.enabled) {
        shouldHide = true;
      }
      child.hidden = shouldHide;
    }
  }

  handleContextMenuEvent(e) {
    this.triggerNode =
      this.findTriggerNode(e, "sidebar-tab-row") ||
      this.findTriggerNode(e, "moz-input-search");
    if (!this.triggerNode) {
      e.preventDefault();
      return;
    }
    // If the right-clicked row is not already part of the selection, move
    // the selection and anchor to it so the context menu operates on the
    // correct item.
    if (this.triggerNode.localName === "sidebar-tab-row") {
      const row = this.triggerNode;
      const list = row.getRootNode().host;
      if (!list.isTabItemSelected(row)) {
        this.treeView.resetSelection();
        this.treeView.selectRowInList(row, list);
        list.dispatchEvent(
          new CustomEvent("set-anchor", {
            bubbles: true,
            composed: true,
            detail: { guid: row.guid },
          })
        );
      }
    }
  }

  async handleCommandEvent(e) {
    let label;
    switch (e.target.id) {
      case "sidebar-history-sort-by-date":
        this.#changeSortOption(e, "date");
        break;
      case "sidebar-history-sort-by-site":
        this.#changeSortOption(e, "site");
        break;
      case "sidebar-history-sort-by-date-and-site":
        this.#changeSortOption(e, "datesite");
        break;
      case "sidebar-history-sort-by-last-visited":
        this.#changeSortOption(e, "lastvisited");
        break;
      case "sidebar-history-clear": {
        const button = await lazy.Sanitizer.showUI(this.topWindow);
        const outcome = button === "accept" ? "confirmed" : "cancelled";
        Glean.browserUiInteraction.sidebarHistory[
          `clear_history_${outcome}`
        ].add(1);
        break;
      }
      case "sidebar-history-context-open-all-in-tabs":
        this.#openAllInTabs(e);
        break;
      case "sidebar-history-context-delete-page":
        this.controller.deleteFromHistory().catch(console.error);
        label = "delete_from_history";
        break;
      case "sidebar-history-context-delete-pages":
        this.#deleteMultipleFromHistory().catch(console.error);
        label = "delete_from_history";
        break;
      case "sidebar-history-context-open-in-tab":
        super.handleCommandEvent(e);
        label = "open_in_new_tab";
        break;
      case "sidebar-history-context-open-in-window":
        super.handleCommandEvent(e);
        label = "open_in_new_window";
        break;
      case "sidebar-history-context-open-in-private-window":
        super.handleCommandEvent(e);
        label = "open_in_private_window";
        break;
      case "sidebar-history-context-forget-site": {
        const button = await this.forgetAboutThisSite();
        const outcome = button === "accept" ? "confirmed" : "cancelled";
        Glean.browserUiInteraction.sidebarHistory[
          `clear_all_website_data_${outcome}`
        ].add(1);
        break;
      }
      case "sidebar-history-context-bookmark-page": {
        const guid = await super.handleCommandEvent(e);
        const outcome = guid ? "confirmed" : "cancelled";
        Glean.browserUiInteraction.sidebarHistory[
          `bookmark_tab_${outcome}`
        ].add(1);
        break;
      }
      case "sidebar-history-context-copy-link":
        super.handleCommandEvent(e);
        label = "copy_link";
        break;
      default:
        super.handleCommandEvent(e);
        break;
    }
    if (label) {
      Glean.browserUiInteraction.sidebarHistory[label].add(1);
    }
  }

  #changeSortOption(e, sortOption) {
    this.treeView.resetSelection();
    Services.prefs.setStringPref(SORT_OPTION_PREF, sortOption);
    this.controller.onChangeSortOption(e, sortOption);
    const sortTypeMap = {
      date: "date",
      site: "site",
      datesite: "date_and_site",
      lastvisited: "last_visited",
    };
    Glean.browserUiInteraction.sidebarSortHistory.record({
      sort_type: sortTypeMap[sortOption],
    });
  }

  #openAllInTabs(e) {
    const urls = this.treeView.getSelectedTabItems().map(item => item.url);
    if (!lazy.OpenInTabsUtils.confirmOpenInTabs(urls.length, this.topWindow)) {
      return;
    }
    const tabset = [];
    for (const uri of urls) {
      // The only reason to know if a url is bookmarked is for calling
      // markPageAsFollowedBookmark, that will annotate the visit with TRANSITION_BOOKMARK.
      // But the new frecency doesn't need that info, it can derive it iself,
      // so we can just pass isBookmark: false and lose nothing
      tabset.push({ uri, isBookmark: false });
    }
    lazy.PlacesUIUtils.openTabset(tabset, e, this.topWindow);
  }

  #deleteMultipleFromHistory() {
    const pageGuids = this.treeView
      .getSelectedTabItems()
      .map(item => item.guid);
    return lazy.PlacesUtils.history.remove(pageGuids);
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

  handleNavigateToLink(e) {
    navigateToLink(e, e.originalTarget.url, { forceNewTab: false });
    Glean.sidebar.link.history.add(1);
    this.treeView.resetSelection();
    this.treeView.selectRowInList(e.originalTarget, e.currentTarget);
  }

  onPrimaryAction(e) {
    const { originalEvent } = e.detail;
    const list = e.currentTarget;
    const row = e.originalTarget;
    if (originalEvent.shiftKey) {
      list.dispatchEvent(
        new CustomEvent("shift-select", {
          bubbles: true,
          composed: true,
          detail: { row },
        })
      );
      return;
    }
    const anchorEvent = new CustomEvent("set-anchor", {
      bubbles: true,
      composed: true,
      detail: { guid: row.guid },
    });
    if (
      (originalEvent.type === "click" &&
        originalEvent.getModifierState("Accel")) ||
      (originalEvent.type === "keydown" && originalEvent.code === "Space")
    ) {
      list.toggleRowSelection(row.guid);
      list.dispatchEvent(anchorEvent);
      return;
    }
    list.dispatchEvent(anchorEvent);
    this.handleNavigateToLink(e);
  }

  onSecondaryAction(e) {
    this.triggerNode = e.detail.item;
    this.controller.deleteFromHistory().catch(console.error);
  }

  onMiddleClickAction(e) {
    this.handleNavigateToLink(e);
  }

  onKeyDown(e) {
    if (
      (e.code === "Delete" || e.code === "Backspace") &&
      e.composedTarget.localName === "sidebar-tab-row"
    ) {
      e.preventDefault();
      this.triggerNode = e.composedTarget;
      this.controller.deleteFromHistory().catch(console.error);
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
      @keydown=${e => this.treeView.handleCardKeydown(e)}
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
      @keydown=${e => this.treeView.handleCardKeydown(e)}
      tabindex=${ifDefined(tabIndex)}
      data-l10n-id=${domain ? nothing : "sidebar-history-site-localhost"}
      data-l10n-attrs=${domain ? nothing : "heading"}
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
      @fxview-tab-list-middleclick-action=${this.onMiddleClickAction}
      @keydown=${this.onKeyDown}
    >
    </sidebar-tab-list>`;
  }

  onSearchQuery(e) {
    this.controller.onSearchQuery(e);
    Glean.browserUiInteraction.sidebarHistory.search.add(1);
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
    this._menuSortByDate.toggleAttribute(
      "checked",
      this.controller.sortOption == "date"
    );
    this._menuSortBySite.toggleAttribute(
      "checked",
      this.controller.sortOption == "site"
    );
    this._menuSortByDateSite.toggleAttribute(
      "checked",
      this.controller.sortOption == "datesite"
    );
    this._menuSortByLastVisited.toggleAttribute(
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
          <div class="options-container">
            <moz-input-search
              data-l10n-id="firefoxview-search-text-box-history"
              data-l10n-attrs="placeholder"
              @MozInputSearch:search=${this.onSearchQuery}
            ></moz-input-search>
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
        </sidebar-panel-header>
        <div class="sidebar-panel-scrollable-content">
          ${this.cardsTemplate}
        </div>
      </div>
    `;
  }
}

customElements.define("sidebar-history", SidebarHistory);
