/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  classMap,
  html,
  ifDefined,
  when,
} from "chrome://global/content/vendor/lit.all.mjs";
import { MAX_TABS_FOR_RECENT_BROWSING } from "./helpers.mjs";
import { searchTabList } from "./search-helpers.mjs";
import { ViewPage } from "./viewpage.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://browser/content/firefoxview/card-container.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://browser/content/firefoxview/fxview-tab-list.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  AppConstants: "resource://gre/modules/AppConstants.sys.mjs",
});

const SS_NOTIFY_CLOSED_OBJECTS_CHANGED = "sessionstore-closed-objects-changed";
const SS_NOTIFY_BROWSER_SHUTDOWN_FLUSH = "sessionstore-browser-shutdown-flush";
const NEVER_REMEMBER_HISTORY_PREF = "browser.privatebrowsing.autostart";
const INCLUDE_CLOSED_TABS_FROM_CLOSED_WINDOWS =
  "browser.sessionstore.closedTabsFromClosedWindows";

function getWindow() {
  return window.browsingContext.embedderWindowGlobal.browsingContext.window;
}

class RecentlyClosedTabsInView extends ViewPage {
  constructor() {
    super();
    this._started = false;
    this.boundObserve = (...args) => this.observe(...args);
    this.firstUpdateComplete = false;
    this.fullyUpdated = false;
    this.maxTabsLength = this.recentBrowsing
      ? MAX_TABS_FOR_RECENT_BROWSING
      : -1;
    this.recentlyClosedTabs = [];
    this.searchQuery = "";
    this.searchResults = null;
    this.showAll = false;
    this.cumulativeSearches = 0;
  }

  static properties = {
    ...ViewPage.properties,
    searchResults: { type: Array },
    showAll: { type: Boolean },
    cumulativeSearches: { type: Number },
  };

  static queries = {
    cardEl: "card-container",
    emptyState: "fxview-empty-state",
    searchTextbox: "moz-input-search",
    tabList: "fxview-tab-list",
  };

  observe(subject, topic) {
    if (
      topic == SS_NOTIFY_CLOSED_OBJECTS_CHANGED ||
      (topic == SS_NOTIFY_BROWSER_SHUTDOWN_FLUSH &&
        subject.documentGlobal == getWindow())
    ) {
      this.updateRecentlyClosedTabs();
    }
  }

  start() {
    if (this._started) {
      return;
    }
    this._started = true;
    this.paused = false;
    this.updateRecentlyClosedTabs();

    Services.obs.addObserver(
      this.boundObserve,
      SS_NOTIFY_CLOSED_OBJECTS_CHANGED
    );
    Services.obs.addObserver(
      this.boundObserve,
      SS_NOTIFY_BROWSER_SHUTDOWN_FLUSH
    );

    if (this.recentBrowsing) {
      this.recentBrowsingElement.addEventListener(
        "MozInputSearch:search",
        this
      );
    }

    this.toggleVisibilityInCardContainer();
  }

  stop() {
    if (!this._started) {
      return;
    }
    this._started = false;

    Services.obs.removeObserver(
      this.boundObserve,
      SS_NOTIFY_CLOSED_OBJECTS_CHANGED
    );
    Services.obs.removeObserver(
      this.boundObserve,
      SS_NOTIFY_BROWSER_SHUTDOWN_FLUSH
    );

    if (this.recentBrowsing) {
      this.recentBrowsingElement.removeEventListener(
        "MozInputSearch:search",
        this
      );
    }

    this.toggleVisibilityInCardContainer();
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.stop();
  }

  handleEvent(event) {
    if (this.recentBrowsing && event.type === "MozInputSearch:search") {
      this.onSearchQuery(event);
    }
  }

  // We remove all the observers when the instance is not visible to the user
  viewHiddenCallback() {
    this.stop();
  }

  // We add observers and check for changes to the session store once the user return to this tab.
  // or the instance becomes visible to the user
  viewVisibleCallback() {
    this.start();
  }

  firstUpdated() {
    this.firstUpdateComplete = true;
  }

  getTabStateValue(tab, key) {
    let value = "";
    const tabEntries = tab.state.entries;
    const activeIndex = tab.state.index - 1;

    if (activeIndex >= 0 && tabEntries[activeIndex]) {
      value = tabEntries[activeIndex][key];
    }

    return value;
  }

  updateRecentlyClosedTabs() {
    let recentlyClosedTabsData =
      lazy.SessionStore.getClosedTabData(getWindow());
    if (Services.prefs.getBoolPref(INCLUDE_CLOSED_TABS_FROM_CLOSED_WINDOWS)) {
      recentlyClosedTabsData.push(
        ...lazy.SessionStore.getClosedTabDataFromClosedWindows()
      );
    }
    // sort the aggregated list to most-recently-closed first
    recentlyClosedTabsData.sort((a, b) => a.closedAt < b.closedAt);
    this.recentlyClosedTabs = recentlyClosedTabsData;
    this.normalizeRecentlyClosedData();
    if (this.searchQuery) {
      this.#updateSearchResults();
    }
    this.requestUpdate();
  }

  normalizeRecentlyClosedData() {
    // Normalize data for fxview-tabs-list
    this.recentlyClosedTabs.forEach(recentlyClosedItem => {
      const targetURI = this.getTabStateValue(recentlyClosedItem, "url");
      recentlyClosedItem.time = recentlyClosedItem.closedAt;
      recentlyClosedItem.icon = recentlyClosedItem.image;
      recentlyClosedItem.primaryL10nId = "fxviewtabrow-tabs-list-tab";
      recentlyClosedItem.primaryL10nArgs = JSON.stringify({
        targetURI: typeof targetURI === "string" ? targetURI : "",
      });
      recentlyClosedItem.secondaryL10nId =
        "firefoxview-closed-tabs-dismiss-tab";
      recentlyClosedItem.secondaryL10nArgs = JSON.stringify({
        tabTitle: recentlyClosedItem.title,
      });
      recentlyClosedItem.url = targetURI;
    });
  }

  onReopenTab(e) {
    const closedId = parseInt(e.originalTarget.closedId, 10);
    const sourceClosedId = parseInt(e.originalTarget.sourceClosedId, 10);

    const originalEvent = e.detail.originalEvent;
    const isModifierClick =
      lazy.AppConstants.platform == "macosx"
        ? originalEvent.metaKey
        : originalEvent.ctrlKey;

    const win = getWindow();
    const activeTabBeforeRestore = isModifierClick
      ? win.gBrowser?.selectedTab
      : null;

    if (isNaN(sourceClosedId)) {
      lazy.SessionStore.undoCloseById(closedId, win);
    } else {
      lazy.SessionStore.undoClosedTabFromClosedWindow(
        { sourceClosedId },
        closedId,
        win
      );
    }

    if (
      activeTabBeforeRestore &&
      win.gBrowser?.selectedTab !== activeTabBeforeRestore
    ) {
      win.gBrowser.selectedTab = activeTabBeforeRestore;
    }

    // Record telemetry
    let tabClosedAt = parseInt(e.originalTarget.time);
    const position =
      Array.from(this.tabList.rowEls).indexOf(e.originalTarget) + 1;

    let now = Date.now();
    let deltaSeconds = (now - tabClosedAt) / 1000;
    Glean.firefoxviewNext.recentlyClosedTabs.record({
      position,
      delta: deltaSeconds,
      page: this.recentBrowsing ? "recentbrowsing" : "recentlyclosed",
    });
    if (this.searchQuery) {
      Glean.firefoxview.cumulativeSearches[
        this.recentBrowsing ? "recentbrowsing" : "recentlyclosed"
      ].accumulateSingleSample(this.cumulativeSearches);
      this.cumulativeSearches = 0;
    }
  }

  onDismissTab(e) {
    const closedId = parseInt(e.originalTarget.closedId, 10);
    const sourceClosedId = parseInt(e.originalTarget.sourceClosedId, 10);
    const sourceWindowId = e.originalTarget.sourceWindowId;
    if (!isNaN(sourceClosedId)) {
      // the sourceClosedId is an identifier for a now-closed window the tab
      // was closed in.
      lazy.SessionStore.forgetClosedTabById(closedId, {
        sourceClosedId,
      });
    } else if (sourceWindowId) {
      // the sourceWindowId is an identifier for a currently-open window the tab
      // was closed in.
      lazy.SessionStore.forgetClosedTabById(closedId, {
        sourceWindowId,
      });
    } else {
      // without either identifier, SessionStore will need to walk its window collections
      // to find the close tab with matching closedId
      lazy.SessionStore.forgetClosedTabById(closedId);
    }

    // Record telemetry
    let tabClosedAt = parseInt(e.originalTarget.time);
    const position =
      Array.from(this.tabList.rowEls).indexOf(e.originalTarget) + 1;

    let now = Date.now();
    let deltaSeconds = (now - tabClosedAt) / 1000;
    Glean.firefoxviewNext.dismissClosedTabTabs.record({
      position,
      delta: deltaSeconds,
      page: this.recentBrowsing ? "recentbrowsing" : "recentlyclosed",
    });
  }

  willUpdate() {
    this.fullyUpdated = false;
  }

  updated() {
    this.fullyUpdated = true;
    this.toggleVisibilityInCardContainer();
  }

  async scheduleUpdate() {
    // Only defer initial update
    if (!this.firstUpdateComplete) {
      await new Promise(resolve => setTimeout(resolve));
    }
    super.scheduleUpdate();
  }

  emptyMessageTemplate() {
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
      descriptionHeader = "firefoxview-recentlyclosed-empty-header";
      descriptionLabels = [
        "firefoxview-recentlyclosed-empty-description",
        "firefoxview-recentlyclosed-empty-description-two",
      ];
      descriptionLink = {
        url: "about:firefoxview#history",
        name: "history-url",
        sameTarget: "true",
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
        class="empty-state recentlyclosed"
        ?isInnerCard=${this.recentBrowsing}
        ?isSelectedTab=${this.selectedTab}
        mainImageUrl=${asset}
      >
      </fxview-empty-state>
    `;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/firefoxview/firefoxview.css"
      />
      ${when(
        !this.recentBrowsing,
        () =>
          html`<div
            class="sticky-container bottom-fade"
            ?hidden=${!this.selectedTab}
          >
            <h2
              class="page-header"
              data-l10n-id="firefoxview-recently-closed-header"
            ></h2>
            <div>
              <moz-input-search
                data-l10n-id="firefoxview-search-text-box-recentlyclosed"
                data-l10n-attrs="placeholder"
                @MozInputSearch:search=${this.onSearchQuery}
              ></moz-input-search>
            </div>
          </div>`
      )}
      <div class=${classMap({ "cards-container": this.selectedTab })}>
        <card-container
          shortPageName=${this.recentBrowsing ? "recentlyclosed" : null}
          ?showViewAll=${this.recentBrowsing && this.recentlyClosedTabs.length}
          ?preserveCollapseState=${this.recentBrowsing ? true : null}
          ?hideHeader=${this.selectedTab}
          ?hidden=${!this.recentlyClosedTabs.length && !this.recentBrowsing}
          ?isEmptyState=${!this.recentlyClosedTabs.length}
        >
          <h3
            slot="header"
            data-l10n-id="firefoxview-recently-closed-header"
          ></h3>
          ${when(
            this.recentlyClosedTabs.length,
            () => html`
              <fxview-tab-list
                slot="main"
                .maxTabsLength=${!this.recentBrowsing || this.showAll
                  ? -1
                  : MAX_TABS_FOR_RECENT_BROWSING}
                .searchQuery=${ifDefined(
                  this.searchResults && this.searchQuery
                )}
                .tabItems=${this.searchResults || this.recentlyClosedTabs}
                @fxview-tab-list-secondary-action=${this.onDismissTab}
                @fxview-tab-list-primary-action=${this.onReopenTab}
                secondaryActionClass="dismiss-button"
              ></fxview-tab-list>
            `
          )}
          ${when(
            this.recentBrowsing && !this.recentlyClosedTabs.length,
            () => html` <div slot="main">${this.emptyMessageTemplate()}</div> `
          )}
          ${when(
            this.isShowAllLinkVisible(),
            () =>
              html` <div
                @click=${this.enableShowAll}
                @keydown=${this.enableShowAll}
                data-l10n-id="firefoxview-show-all"
                ?hidden=${!this.isShowAllLinkVisible()}
                slot="footer"
                tabindex="0"
                role="link"
              ></div>`
          )}
        </card-container>
        ${when(
          this.selectedTab && !this.recentlyClosedTabs.length,
          () => html` <div>${this.emptyMessageTemplate()}</div> `
        )}
      </div>
    `;
  }

  onSearchQuery(e) {
    if (!this.recentBrowsing) {
      Glean.firefoxviewNext.searchInitiatedSearch.record({
        page: "recentlyclosed",
      });
    }
    this.searchQuery = e.detail.query;
    this.showAll = false;
    this.cumulativeSearches = this.searchQuery
      ? this.cumulativeSearches + 1
      : 0;
    this.#updateSearchResults();
  }

  #updateSearchResults() {
    this.searchResults = this.searchQuery
      ? searchTabList(this.searchQuery, this.recentlyClosedTabs)
      : null;
  }

  isShowAllLinkVisible() {
    return (
      this.recentBrowsing &&
      this.searchQuery &&
      this.searchResults.length > MAX_TABS_FOR_RECENT_BROWSING &&
      !this.showAll
    );
  }

  enableShowAll(event) {
    if (
      event.type == "click" ||
      (event.type == "keydown" && event.code == "Enter") ||
      (event.type == "keydown" && event.code == "Space")
    ) {
      event.preventDefault();
      this.showAll = true;
      Glean.firefoxviewNext.searchShowAllShowallbutton.record({
        section: "recentlyclosed",
      });
    }
  }
}
customElements.define("view-recentlyclosed", RecentlyClosedTabsInView);
