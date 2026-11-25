/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  html,
  ifDefined,
  when,
} from "chrome://global/content/vendor/lit.all.mjs";
import { escapeHtmlEntities, navigateToLink } from "./helpers.mjs";
import { ViewPage } from "./viewpage.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/migration/migration-wizard.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-button.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://global/content/elements/moz-label.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  HistoryController: "resource:///modules/HistoryController.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
});

const NEVER_REMEMBER_HISTORY_PREF = "browser.privatebrowsing.autostart";

const SEARCH_RESULTS_LIMIT = 300;

class HistoryInView extends ViewPage {
  constructor() {
    super();
    this._started = false;
    // Setting maxTabsLength to -1 for no max
    this.maxTabsLength = -1;
    this.fullyUpdated = false;
    this.cumulativeSearches = 0;
  }

  controller = new lazy.HistoryController(this, {
    searchResultsLimit: SEARCH_RESULTS_LIMIT,
  });

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

  disconnectedCallback() {
    super.disconnectedCallback();
    this.stop();
    this.migrationWizardDialog?.removeEventListener(
      "MigrationWizard:Close",
      this.migrationWizardDialog
    );
  }

  viewVisibleCallback() {
    this.start();
  }

  viewHiddenCallback() {
    this.stop();
  }

  static queries = {
    cards: { all: "card-container:not([hidden])" },
    migrationWizardDialog: "#migrationWizardDialog",
    emptyState: "fxview-empty-state",
    lists: { all: "fxview-tab-list" },
    showAllHistoryBtn: ".show-all-history-button",
    searchTextbox: "moz-input-search",
    sortInputs: { all: "input[name=history-sort-option]" },
    panelList: "panel-list",
  };

  async getUpdateComplete() {
    await super.getUpdateComplete();
    await Promise.all(Array.from(this.cards).map(card => card.updateComplete));
  }

  onPrimaryAction(e) {
    navigateToLink(e);
    // Record telemetry
    Glean.firefoxviewNext.historyVisits.record();

    if (this.controller.searchQuery) {
      Glean.firefoxview.cumulativeSearches.history.accumulateSingleSample(
        this.cumulativeSearches
      );
      this.cumulativeSearches = 0;
    }
  }

  async forgetAboutThisSite(e) {
    let host = Services.io.newURI(this.triggerNode.url).host;
    let baseDomain;
    try {
      baseDomain = Services.eTLD.getBaseDomainFromHost(host);
    } catch (ex) {
      // If there is no baseDomain we fall back to host
    }
    await this.getWindow().gDialogBox.open(
      "chrome://browser/content/places/clearDataForSite.xhtml",
      { host, hostOrBaseDomain: baseDomain ?? host }
    );
    this.recordContextMenuTelemetry("forget-about-this-site", e, "history");
  }

  onSecondaryAction(e) {
    this.triggerNode = e.originalTarget;
    this.panelList.toggle(e.detail.originalEvent);
  }

  deleteFromHistory(e) {
    this.controller.deleteFromHistory().catch(console.error);
    this.recordContextMenuTelemetry("delete-from-history", e);
  }

  onChangeSortOption(e) {
    this.controller.onChangeSortOption(e);
    Glean.firefoxviewNext.sortHistoryTabs.record({
      sort_type: this.controller.sortOption,
      search_start: this.controller.searchQuery ? "true" : "false",
    });
  }

  onSearchQuery(e) {
    if (!this.recentBrowsing) {
      Glean.firefoxviewNext.searchInitiatedSearch.record({
        page: "history",
      });
    }
    this.controller.onSearchQuery(e);
    this.cumulativeSearches = this.controller.searchQuery
      ? this.cumulativeSearches + 1
      : 0;
  }

  showAllHistory() {
    // Record telemetry
    Glean.firefoxviewNext.showAllHistoryTabs.record();

    // Open History view in Library window
    this.getWindow().PlacesCommandHook.showPlacesOrganizer("History");
  }

  async openMigrationWizard() {
    let migrationWizardDialog = this.migrationWizardDialog;

    if (migrationWizardDialog.open) {
      return;
    }

    await customElements.whenDefined("migration-wizard");

    // If we've been opened before, remove the old wizard and insert a
    // new one to put it back into its starting state.
    if (!migrationWizardDialog.firstElementChild) {
      let wizard = document.createElement("migration-wizard");
      wizard.toggleAttribute("dialog-mode", true);
      migrationWizardDialog.appendChild(wizard);
    }
    migrationWizardDialog.firstElementChild.requestState();

    this.migrationWizardDialog.addEventListener(
      "MigrationWizard:Close",
      function (e) {
        e.currentTarget.close();
      }
    );

    migrationWizardDialog.showModal();
  }

  updated() {
    this.fullyUpdated = true;
    if (this.lists?.length) {
      this.toggleVisibilityInCardContainer();
    }
  }

  panelListTemplate() {
    return html`
      <panel-list slot="menu" data-tab-type="history">
        <panel-item
          @click=${this.deleteFromHistory}
          data-l10n-id="firefoxview-history-context-delete"
          data-l10n-attrs="accesskey"
        ></panel-item>
        <panel-item
          @click=${this.forgetAboutThisSite}
          data-l10n-id="firefoxview-history-context-forget-site"
          data-l10n-attrs="accesskey"
        ></panel-item>
        <hr />
        <panel-item
          @click=${this.openInNewWindow}
          data-l10n-id="fxviewtabrow-open-in-window"
          data-l10n-attrs="accesskey"
        ></panel-item>
        <panel-item
          @click=${this.openInNewPrivateWindow}
          data-l10n-id="fxviewtabrow-open-in-private-window"
          data-l10n-attrs="accesskey"
          ?hidden=${!lazy.PrivateBrowsingUtils.enabled}
        ></panel-item>
        <hr />
        <panel-item
          @click=${this.copyLink}
          data-l10n-id="fxviewtabrow-copy-link"
          data-l10n-attrs="accesskey"
        ></panel-item>
      </panel-list>
    `;
  }

  /**
   * The template to use for cards-container.
   */
  get cardsTemplate() {
    if (this.controller.searchResults) {
      return this.#searchResultsTemplate();
    } else if (!this.controller.isHistoryEmpty) {
      return this.#historyCardsTemplate();
    }
    return this.#emptyMessageTemplate();
  }

  #historyCardsTemplate() {
    let cardsTemplate = [];
    switch (this.controller.sortOption) {
      case "date":
        cardsTemplate = this.controller.historyVisits.map(historyItem => {
          let dateArg = JSON.stringify({ date: historyItem.items[0].time });
          return html`<card-container>
            <h3
              slot="header"
              data-l10n-id=${historyItem.l10nId}
              data-l10n-args=${dateArg}
            ></h3>
            <fxview-tab-list
              slot="main"
              secondaryActionClass="options-button"
              dateTimeFormat=${historyItem.l10nId.includes("prev-month")
                ? "dateTime"
                : "time"}
              hasPopup="menu"
              maxTabsLength=${this.maxTabsLength}
              .tabItems=${historyItem.items}
              @fxview-tab-list-primary-action=${this.onPrimaryAction}
              @fxview-tab-list-secondary-action=${this.onSecondaryAction}
            >
            </fxview-tab-list>
          </card-container>`;
        });
        break;
      case "site":
        cardsTemplate = this.controller.historyVisits.map(historyItem => {
          return html`<card-container>
            <h3 slot="header" data-l10n-id=${ifDefined(historyItem.l10nId)}>
              ${historyItem.domain}
            </h3>
            <fxview-tab-list
              slot="main"
              secondaryActionClass="options-button"
              dateTimeFormat="dateTime"
              hasPopup="menu"
              maxTabsLength=${this.maxTabsLength}
              .tabItems=${historyItem.items}
              @fxview-tab-list-primary-action=${this.onPrimaryAction}
              @fxview-tab-list-secondary-action=${this.onSecondaryAction}
            >
            </fxview-tab-list>
          </card-container>`;
        });
        break;
    }
    return cardsTemplate;
  }

  #emptyMessageTemplate() {
    let descriptionHeader;
    let descriptionLabels;
    let descriptionLink;
    if (Services.prefs.getBoolPref(NEVER_REMEMBER_HISTORY_PREF, false)) {
      // History pref set to never remember history
      descriptionHeader = "firefoxview-dont-remember-history-empty-header-3";
      descriptionLabels = [
        "firefoxview-dont-remember-history-empty-description-2",
      ];
      descriptionLink = {
        url: "about:preferences#privacy",
        name: "history-settings-url-two",
      };
    } else {
      descriptionHeader = "firefoxview-history-empty-header-2";
      descriptionLabels = ["firefoxview-history-empty-description-2"];
      descriptionLink = {
        url: "about:preferences#privacy",
        name: "history-settings-url",
      };
    }
    let asset = "chrome://browser/skin/sidebar/kit-page-history.svg";
    return html`
      <fxview-empty-state
        headerLabel=${descriptionHeader}
        .descriptionLabels=${descriptionLabels}
        .descriptionLink=${descriptionLink}
        class="empty-state history"
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
            data-l10n-id="firefoxview-search-results-count"
            data-l10n-args=${JSON.stringify({
              count: this.controller.searchResults.length,
            })}
          ></h3>`
      )}
      <fxview-tab-list
        slot="main"
        secondaryActionClass="options-button"
        dateTimeFormat="dateTime"
        hasPopup="menu"
        maxTabsLength="-1"
        .searchQuery=${this.controller.searchQuery}
        .tabItems=${this.controller.searchResults}
        @fxview-tab-list-primary-action=${this.onPrimaryAction}
        @fxview-tab-list-secondary-action=${this.onSecondaryAction}
      >
      </fxview-tab-list>
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
      <link
        rel="stylesheet"
        href="chrome://browser/content/firefoxview/history.css"
      />
      <dialog id="migrationWizardDialog"></dialog>
      ${this.panelListTemplate()}
      <div class="sticky-container bottom-fade">
        <h2 class="page-header" data-l10n-id="firefoxview-history-header"></h2>
        <div class="history-sort-options">
          <div class="history-sort-option">
            <moz-input-search
              data-l10n-id="firefoxview-search-text-box-history"
              data-l10n-attrs="placeholder"
              @MozInputSearch:search=${this.onSearchQuery}
            ></moz-input-search>
          </div>
          <div class="history-sort-option">
            <input
              type="radio"
              id="sort-by-date"
              name="history-sort-option"
              value="date"
              ?checked=${this.controller.sortOption === "date"}
              @click=${this.onChangeSortOption}
            />
            <label
              is="moz-label"
              for="sort-by-date"
              data-l10n-id="firefoxview-sort-history-by-date-label"
            ></label>
          </div>
          <div class="history-sort-option">
            <input
              type="radio"
              id="sort-by-site"
              name="history-sort-option"
              value="site"
              ?checked=${this.controller.sortOption === "site"}
              @click=${this.onChangeSortOption}
            />
            <label
              is="moz-label"
              for="sort-by-site"
              data-l10n-id="firefoxview-sort-history-by-site-label"
            ></label>
          </div>
        </div>
      </div>
      <div class="cards-container">
        <card-container
          class="import-history-banner"
          hideHeader="true"
          ?hidden=${!this.controller.shouldShowImportBanner}
          role="group"
          aria-labelledby="header"
          aria-describedby="description"
        >
          <div slot="main">
            <div class="banner-text">
              <span
                data-l10n-id="firefoxview-import-history-header"
                id="header"
              ></span>
              <span
                data-l10n-id="firefoxview-import-history-description"
                id="description"
              ></span>
            </div>
            <div class="buttons">
              <moz-button
                type="primary"
                class="choose-browser"
                data-l10n-id="firefoxview-choose-browser-button"
                @click=${this.openMigrationWizard}
              ></moz-button>
              <moz-button
                class="close"
                type="icon ghost"
                data-l10n-id="firefoxview-import-history-close-button"
                @click=${() => this.controller.dismissImportHistory()}
              ></moz-button>
            </div>
          </div>
        </card-container>
        ${this.cardsTemplate}
      </div>
      <div
        class="show-all-history-footer"
        ?hidden=${this.controller.isHistoryEmpty}
      >
        <moz-button
          class="show-all-history-button"
          data-l10n-id="firefoxview-show-all-history"
          @click=${this.showAllHistory}
          ?hidden=${this.controller.searchResults}
        ></moz-button>
      </div>
    `;
  }

  willUpdate() {
    this.fullyUpdated = false;
  }
}
customElements.define("view-history", HistoryInView);
