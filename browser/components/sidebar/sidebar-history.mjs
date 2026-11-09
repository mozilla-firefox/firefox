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
  MigrationUtils:
    "moz-src:///browser/components/migration/MigrationUtils.sys.mjs",
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
    this._menuSortByMostVisited = doc.getElementById(
      "sidebar-history-sort-by-most-visited"
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
   * The date or site card whose heading opened the context menu, if any.
   *
   * @type {MozCard | null}
   */
  get #triggerCard() {
    return this.triggerNode?.localName === "moz-card" ? this.triggerNode : null;
  }

  /**
   * Show card commands for a date or site heading, multiselect commands when
   * multiple rows are selected, and single row commands otherwise.
   */
  updateContextMenu() {
    const card = this.#triggerCard;
    const isMultiSelect = !card && this.isMultipleRowsSelected;
    for (const child of this._contextMenu.children) {
      let shouldHide;
      const isCardCommand = child.classList.contains(
        "sidebar-history-card-command"
      );
      const isMultiSelectCommand = child.classList.contains(
        "sidebar-history-multiselect-command"
      );
      const isPrivateWindowMenuItem =
        child.id === "sidebar-history-context-open-in-private-window";
      if (card) {
        shouldHide = !isCardCommand;
      } else if (isMultiSelect) {
        shouldHide = !isMultiSelectCommand;
      } else {
        shouldHide = isCardCommand || isMultiSelectCommand;
      }
      if (
        child.id === "sidebar-history-context-forget-card-site" &&
        !this.#getCardSiteUrl(card)
      ) {
        shouldHide = true;
      }
      if (isPrivateWindowMenuItem && !lazy.PrivateBrowsingUtils.enabled) {
        shouldHide = true;
      }
      if (lazy.PlacesUIUtils.shouldHideOpenMenuItem(child)) {
        shouldHide = true;
      }
      child.hidden = shouldHide;
    }
  }

  handleContextMenuEvent(e) {
    this.triggerNode =
      this.findTriggerNode(e, "sidebar-tab-row") ||
      this.#findCardHeadingTrigger(e) ||
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
        this.treeView.selectRowInList(list, row.guid);
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

  /**
   * Find the date or site card whose heading this event comes from.
   *
   * @param {Event} e
   * @returns {MozCard | null}
   */
  #findCardHeadingTrigger(e) {
    const path = e.composedPath();
    const card = path.find(el => el.localName === "moz-card");
    return card?.historyItems && path.includes(card.summaryEl) ? card : null;
  }

  /**
   * @param {MozCard} card
   * @returns {HistoryVisit[]}
   *   All visits listed under a date or site card, including those of nested
   *   site cards.
   */
  #getCardVisits(card) {
    return card.historyItems.flatMap(item =>
      Array.isArray(item) ? item[1] : item
    );
  }

  /**
   * @param {MozCard | null} card
   * @returns {string | null}
   *   A URL of the website listed under a site card, or null if the card is
   *   not a site card or groups local files.
   */
  #getCardSiteUrl(card) {
    if (!card?.classList.contains("site-card")) {
      return null;
    }
    const url = this.#getCardVisits(card)[0]?.url;
    return URL.parse(url)?.hostname ? url : null;
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
      case "sidebar-history-sort-by-most-visited":
        this.#changeSortOption(e, "mostvisited");
        break;
      case "sidebar-history-clear": {
        const button = await lazy.Sanitizer.showUI(this.topWindow, undefined, {
          entryPoint: lazy.Sanitizer.DIALOG_ENTRY_POINTS.SIDEBAR_HISTORY,
        });
        const outcome = button === "accept" ? "confirmed" : "cancelled";
        Glean.browserUiInteraction.sidebarHistory[
          `clear_history_${outcome}`
        ].add(1);
        break;
      }
      case "sidebar-history-context-open-all-in-tabs": {
        const card = this.#triggerCard;
        this.#openAllInTabs(
          e,
          card ? this.#getCardVisits(card) : this.treeView.getSelectedTabItems()
        );
        break;
      }
      case "sidebar-history-context-delete-page":
        this.controller.deleteFromHistory().catch(console.error);
        label = "delete_from_history";
        break;
      case "sidebar-history-context-delete-pages":
        this.#deletePagesFromHistory(this.treeView.getSelectedTabItems()).catch(
          console.error
        );
        label = "delete_from_history";
        break;
      case "sidebar-history-context-delete-card":
        this.#deleteCardFromHistory(this.#triggerCard).catch(console.error);
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
      case "sidebar-history-context-forget-site":
      case "sidebar-history-context-forget-card-site": {
        const card = this.#triggerCard;
        const button = await this.forgetAboutThisSite(
          card ? this.#getCardSiteUrl(card) : this.triggerNode.url
        );
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
      mostvisited: "most_visited",
    };
    Glean.browserUiInteraction.sidebarSortHistory.record({
      sort_type: sortTypeMap[sortOption],
    });
  }

  #openAllInTabs(e, items) {
    const urls = [...new Set(items.map(item => item.url))];
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

  #deletePagesFromHistory(items) {
    const pageGuids = new Set(items.map(item => item.pageGuid));
    return lazy.PlacesUtils.history.remove([...pageGuids]);
  }

  /**
   * Delete what a card lists. Date cards, and site cards nested in them, only
   * remove visits from the days they list, so pages visited on other days stay
   * in history. Site cards remove their pages.
   *
   * @param {MozCard} card
   */
  async #deleteCardFromHistory(card) {
    const visits = this.#getCardVisits(card);
    const isDateCard = card.classList.contains("date-card");
    if (!isDateCard && !card.classList.contains("nested-card")) {
      await this.#deletePagesFromHistory(visits);
      return;
    }
    let earliest = Infinity;
    let latest = -Infinity;
    for (const { time } of visits) {
      earliest = Math.min(earliest, time);
      latest = Math.max(latest, time);
    }
    const beginDate = new Date(earliest);
    beginDate.setHours(0, 0, 0, 0);
    const endDate = new Date(latest);
    endDate.setHours(23, 59, 59, 999);
    const placesHistory = lazy.PlacesUtils.history;
    if (isDateCard) {
      await placesHistory.removeVisitsByFilter({ beginDate, endDate });
      return;
    }
    const urls = new Set(visits.map(({ url }) => url));
    await Promise.all(
      [...urls].map(url =>
        placesHistory.removeVisitsByFilter({ url, beginDate, endDate })
      )
    );
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
    this.treeView.selectRowInList(e.currentTarget, e.originalTarget.guid);
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
    if (e.code === "Delete" || e.code === "Backspace") {
      const row = e
        .composedPath()
        .find(el => el.localName === "sidebar-tab-row");
      if (!row) {
        return;
      }

      e.preventDefault();
      if (this.isMultipleRowsSelected) {
        this.#deletePagesFromHistory(this.treeView.getSelectedTabItems()).catch(
          console.error
        );
        return;
      }
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

  get #hasAccordionCards() {
    return (
      !this.controller.searchResults &&
      !this.controller.isHistoryPending &&
      !this.controller.isHistoryEmpty &&
      ["date", "site", "datesite"].includes(this.controller.sortOption)
    );
  }

  #historyCardsTemplate() {
    const { historyVisits } = this.controller;
    switch (this.controller.sortOption) {
      case "date":
        return historyVisits.map(({ l10nId, items }, i) =>
          this.#dateCardTemplate(l10nId, i, items, {
            setSize: historyVisits.length,
          })
        );
      case "site":
        return historyVisits.map(({ domain, items }, i) =>
          this.#siteCardTemplate(domain, i, items, {
            setSize: historyVisits.length,
          })
        );
      case "datesite":
        return historyVisits.map(({ l10nId, items }, i) =>
          this.#dateCardTemplate(l10nId, i, items, {
            isDateSite: true,
            setSize: historyVisits.length,
          })
        );
      case "lastvisited":
      case "mostvisited":
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

  #dateCardTemplate(l10nId, index, items, { isDateSite = false, setSize }) {
    return html` <moz-card
      type="accordion"
      class="date-card"
      treeItem
      aria-level="1"
      aria-posinset=${index + 1}
      aria-setsize=${setSize}
      ?expanded=${index < DAYS_EXPANDED_INITIALLY}
      .historyItems=${items}
      data-l10n-id=${l10nId}
      data-l10n-args=${JSON.stringify({
        date: isDateSite ? items[0][1][0].time : items[0].time,
      })}
      @keydown=${this.keydownHandler}
      @toggle=${this.#onCardToggle}
    >
      ${isDateSite
        ? html`<div role="group">
            ${items.map(([domain, visits], i) =>
              this.#siteCardTemplate(domain, i, visits, {
                isDateSite: true,
                isLastCard: i == items.length - 1,
                setSize: items.length,
              })
            )}
          </div>`
        : this.#tabListTemplate(this.getTabItems(items), { treeLevel: 2 })}
    </moz-card>`;
  }

  #siteCardTemplate(
    domain,
    index,
    items,
    { isDateSite = false, isLastCard = false, setSize }
  ) {
    return html` <moz-card
      class=${classMap({
        "last-card": isLastCard,
        "nested-card": isDateSite,
        "site-card": true,
      })}
      type="accordion"
      treeItem
      aria-level=${isDateSite ? 2 : 1}
      aria-posinset=${index + 1}
      aria-setsize=${setSize}
      ?expanded=${!isDateSite}
      .historyItems=${items}
      heading=${domain}
      @keydown=${this.keydownHandler}
      @toggle=${this.#onCardToggle}
      data-l10n-id=${domain ? nothing : "sidebar-history-site-localhost"}
      data-l10n-attrs=${domain ? nothing : "heading"}
    >
      ${this.#tabListTemplate(this.getTabItems(items), {
        treeLevel: isDateSite ? 3 : 2,
      })}
    </moz-card>`;
  }

  #onCardToggle() {
    this.dispatchEvent(new CustomEvent("folder-toggle"));
  }

  #importHistoryPromoTemplate() {
    return html`<moz-promo
      class="import-history-promo"
      type="vibrant"
      imageSrc="chrome://browser/skin/sidebar/kit-history-import.svg"
      dismissable
      data-l10n-id="sidebar-history-import-promo"
      @promo:user-dismissed=${this.#onImportHistoryPromoDismissed}
    >
      <moz-button
        slot="actions"
        type="primary"
        data-l10n-id="sidebar-history-import-promo-button"
        @click=${this.#openMigrationWizard}
      ></moz-button>
    </moz-promo>`;
  }

  #onImportHistoryPromoDismissed(e) {
    // Let the pref change re-render without the promo rather than having
    // moz-promo remove itself from a Lit-managed tree.
    e.preventDefault();
    this.controller.dismissImportHistory();
  }

  #openMigrationWizard() {
    lazy.MigrationUtils.showMigrationWizard(this.topWindow, {});
  }

  #emptyMessageTemplate() {
    const nova = Services.prefs.getBoolPref("browser.nova.enabled", false);
    let descriptionHeader;
    let descriptionLabels;
    let descriptionLink;

    if (Services.prefs.getBoolPref(NEVER_REMEMBER_HISTORY_PREF, false)) {
      // History pref set to never remember history
      descriptionHeader = nova
        ? "firefoxview-dont-remember-history-empty-header-3"
        : "firefoxview-dont-remember-history-empty-header-2";
      descriptionLabels = [
        nova
          ? "firefoxview-dont-remember-history-empty-description-2"
          : "firefoxview-dont-remember-history-empty-description-one",
      ];

      descriptionLink = {
        url: "about:preferences#privacy",
        name: "history-settings-url-two",
      };
    } else {
      descriptionHeader = nova
        ? "firefoxview-history-empty-header-2"
        : "firefoxview-history-empty-header";
      descriptionLabels = nova
        ? ["firefoxview-history-empty-description-2"]
        : [
            "firefoxview-history-empty-description",
            "firefoxview-history-empty-description-two",
          ];
      descriptionLink = {
        url: "about:preferences#privacy",
        name: "history-settings-url",
      };
    }
    let asset = nova
      ? "chrome://browser/skin/sidebar/kit-page-history.svg"
      : "chrome://browser/content/firefoxview/history-empty.svg";

    return html`
      <fxview-empty-state
        headerLabel=${descriptionHeader}
        .descriptionLabels=${descriptionLabels}
        .descriptionLink=${descriptionLink}
        class="empty-state history"
        isSelectedTab
        mainImageUrl=${asset}
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
          { searchQuery: this.controller.searchQuery }
        )}
      </div>
    </moz-card>`;
  }

  #tabListTemplate(tabItems, { searchQuery, treeLevel = 1 } = {}) {
    return html`<sidebar-tab-list
      rowsDraggable
      .handleFocusElementToCard=${this.handleFocusElementToCard}
      inTree
      .level=${treeLevel}
      maxTabsLength="-1"
      .searchQuery=${searchQuery}
      secondaryActionClass="delete-button"
      .sortOption=${this.controller.sortOption}
      .tabItems=${tabItems}
      @fxview-tab-list-primary-action=${this.onPrimaryAction}
      @fxview-tab-list-secondary-action=${this.onSecondaryAction}
      @fxview-tab-list-middleclick-action=${this.onMiddleClickAction}
      @keydown=${this.onKeyDown}
      @dragstart=${this.#onDragStart}
    >
    </sidebar-tab-list>`;
  }

  onSearchQuery(e) {
    this.controller.onSearchQuery(e);
    Glean.browserUiInteraction.sidebarHistory.search.add(1);
    this.treeView.resetActiveNode();
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

  #onDragStart(e) {
    const row = e.composedPath().find(el => el.localName === "sidebar-tab-row");
    if (!row?.url) {
      e.preventDefault();
      return;
    }
    const { url } = row;
    const title = row.title || url;
    e.dataTransfer.clearData();
    e.dataTransfer.setData(lazy.PlacesUtils.TYPE_X_MOZ_URL, url + "\n" + title);
    e.dataTransfer.setData(lazy.PlacesUtils.TYPE_PLAINTEXT, url);
    e.dataTransfer.effectAllowed = "copyLink";
    e.stopPropagation();
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
    this._menuSortByMostVisited.toggleAttribute(
      "checked",
      this.controller.sortOption == "mostvisited"
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
        <div
          class="sidebar-panel-scrollable-content"
          role=${ifDefined(this.#hasAccordionCards ? "tree" : undefined)}
          tabindex="-1"
        >
          ${when(this.controller.shouldShowImportBanner, () =>
            this.#importHistoryPromoTemplate()
          )}
          ${this.cardsTemplate}
        </div>
      </div>
    `;
  }
}

customElements.define("sidebar-history", SidebarHistory);
