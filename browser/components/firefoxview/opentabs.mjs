/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  classMap,
  html,
  map,
  nothing,
  styleMap,
  when,
} from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { getLogger, MAX_TABS_FOR_RECENT_BROWSING } from "./helpers.mjs";
import { searchTabList } from "./search-helpers.mjs";
import { ViewPage, ViewPageContent } from "./viewpage.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://browser/content/firefoxview/opentabs-tab-list.mjs";
// eslint-disable-next-line import/no-unassigned-import
import "chrome://global/content/elements/moz-label.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BookmarkList: "resource://gre/modules/BookmarkList.sys.mjs",
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  FxAccounts: "resource://gre/modules/FxAccounts.sys.mjs",
  NonPrivateTabs: "resource:///modules/OpenTabs.sys.mjs",
  OpenTabsController: "resource:///modules/OpenTabsController.sys.mjs",
  getTabsTargetForWindow: "resource:///modules/OpenTabs.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SpecialMessageActions:
    "resource://messaging-system/lib/SpecialMessageActions.sys.mjs",
  TabGroupMenu: "moz-src:///browser/components/tabbrowser/TabGroupMenu.sys.mjs",
  TabMetrics: "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "fxAccounts", () => {
  return ChromeUtils.importESModule(
    "resource://gre/modules/FxAccounts.sys.mjs"
  ).getFxAccountsSingleton();
});

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "FXA_ENABLED",
  "identity.fxaccounts.enabled"
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "tabGroupsEnabled",
  "browser.tabs.groups.enabled",
  false
);

const TOPIC_DEVICESTATE_CHANGED = "firefox-view.devicestate.changed";
const TOPIC_DEVICELIST_UPDATED = "fxaccounts:devicelist_updated";

/**
 * A collection of open tabs grouped by window.
 *
 * @property {Array<Window>} windows
 *   A list of windows with the same privateness
 * @property {string} sortOption
 *   The sorting order of open tabs:
 *   - "recency": Sorted by recent activity. (For recent browsing, this is the only option.)
 *   - "tabStripOrder": Match the order in which they appear on the tab strip.
 */
class OpenTabsInView extends ViewPage {
  static properties = {
    ...ViewPage.properties,
    windows: { type: Array },
    searchQuery: { type: String },
    sortOption: { type: String },
  };
  static queries = {
    viewCards: { all: "view-opentabs-card" },
    optionsContainer: ".open-tabs-options",
    searchTextbox: "moz-input-search",
  };

  initialWindowsReady = false;
  currentWindow = null;
  openTabsTarget = null;

  constructor() {
    super();
    this._started = false;
    this.windows = [];
    this.currentWindow = this.getWindow();
    if (lazy.PrivateBrowsingUtils.isWindowPrivate(this.currentWindow)) {
      this.openTabsTarget = lazy.getTabsTargetForWindow(this.currentWindow);
    } else {
      this.openTabsTarget = lazy.NonPrivateTabs;
    }
    this.searchQuery = "";
    this.sortOption = this.recentBrowsing
      ? "recency"
      : Services.prefs.getStringPref(
          "browser.tabs.firefox-view.ui-state.opentabs.sort-option",
          "recency"
        );
  }

  start() {
    if (this._started) {
      return;
    }
    this._started = true;
    this.#setupTabChangeListener();

    // To resolve the race between this component wanting to render all the windows'
    // tabs, while those windows are still potentially opening, flip this property
    // once the promise resolves and we'll bail out of rendering until then.
    this.openTabsTarget.readyWindowsPromise.finally(() => {
      this.initialWindowsReady = true;
      this._updateWindowList();
    });

    for (let card of this.viewCards) {
      card.paused = false;
      card.viewVisibleCallback?.();
    }

    if (this.recentBrowsing) {
      this.recentBrowsingElement.addEventListener(
        "MozInputSearch:search",
        this
      );
    }

    this.bookmarkList = new lazy.BookmarkList(this.#getAllTabUrls(), () =>
      this.viewCards.forEach(card => card.requestUpdate())
    );
  }

  shouldUpdate(changedProperties) {
    if (!this.initialWindowsReady) {
      return false;
    }
    return super.shouldUpdate(changedProperties);
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.stop();
  }

  stop() {
    if (!this._started) {
      return;
    }
    this._started = false;
    this.paused = true;

    this.openTabsTarget.removeEventListener("TabChange", this);
    this.openTabsTarget.removeEventListener("TabRecencyChange", this);

    for (let card of this.viewCards) {
      card.paused = true;
      card.viewHiddenCallback?.();
    }

    if (this.recentBrowsing) {
      this.recentBrowsingElement.removeEventListener(
        "MozInputSearch:search",
        this
      );
    }

    this.bookmarkList.removeListeners();
  }

  viewVisibleCallback() {
    this.start();
  }

  viewHiddenCallback() {
    this.stop();
  }

  #setupTabChangeListener() {
    if (this.sortOption === "recency") {
      this.openTabsTarget.addEventListener("TabRecencyChange", this);
      this.openTabsTarget.removeEventListener("TabChange", this);
    } else {
      this.openTabsTarget.removeEventListener("TabRecencyChange", this);
      this.openTabsTarget.addEventListener("TabChange", this);
    }
  }

  #getAllTabUrls() {
    return this.openTabsTarget
      .getAllTabs()
      .map(({ linkedBrowser }) => linkedBrowser?.currentURI?.spec)
      .filter(Boolean);
  }

  render() {
    if (this.recentBrowsing) {
      return this.getRecentBrowsingTemplate();
    }
    let currentWindowIndex, currentWindowTabs;
    let index = 1;
    const otherWindows = [];
    this.windows.forEach(win => {
      const tabs = this.openTabsTarget.getTabsForWindow(
        win,
        this.sortOption === "recency"
      );
      if (win === this.currentWindow) {
        currentWindowIndex = index++;
        currentWindowTabs = tabs;
      } else {
        otherWindows.push([index++, tabs, win]);
      }
    });

    const cardClasses = classMap({
      "height-limited": this.windows.length > 3,
      "width-limited": this.windows.length > 1,
    });
    let cardCount;
    if (this.windows.length <= 1) {
      cardCount = "one";
    } else if (this.windows.length === 2) {
      cardCount = "two";
    } else {
      cardCount = "three-or-more";
    }
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/firefoxview/view-opentabs.css"
      />
      <link
        rel="stylesheet"
        href="chrome://browser/content/firefoxview/firefoxview.css"
      />
      <div class="sticky-container bottom-fade">
        <h2 class="page-header" data-l10n-id="firefoxview-opentabs-header"></h2>
        <div class="open-tabs-options">
          <moz-input-search
            data-l10n-id="firefoxview-search-text-box-opentabs"
            data-l10n-attrs="placeholder"
            @MozInputSearch:search=${this.onSearchQuery}
          ></moz-input-search>
          <div class="open-tabs-sort-wrapper">
            <div class="open-tabs-sort-option">
              <input
                type="radio"
                id="sort-by-recency"
                name="open-tabs-sort-option"
                value="recency"
                ?checked=${this.sortOption === "recency"}
                @click=${this.onChangeSortOption}
              />
              <label
                is="moz-label"
                for="sort-by-recency"
                data-l10n-id="firefoxview-sort-open-tabs-by-recency-label"
              ></label>
            </div>
            <div class="open-tabs-sort-option">
              <input
                type="radio"
                id="sort-by-order"
                name="open-tabs-sort-option"
                value="tabStripOrder"
                ?checked=${this.sortOption === "tabStripOrder"}
                @click=${this.onChangeSortOption}
              />
              <label
                is="moz-label"
                for="sort-by-order"
                data-l10n-id="firefoxview-sort-open-tabs-by-order-label"
              ></label>
            </div>
          </div>
        </div>
      </div>
      <div
        card-count=${cardCount}
        class="view-opentabs-card-container cards-container"
      >
        ${when(
          currentWindowIndex && currentWindowTabs,
          () => html`
            <view-opentabs-card
              class=${cardClasses}
              .tabs=${currentWindowTabs}
              .paused=${this.paused}
              data-inner-id=${this.currentWindow.windowGlobalChild
                .innerWindowId}
              data-l10n-id="firefoxview-opentabs-current-window-header"
              data-l10n-args=${JSON.stringify({
                winID: currentWindowIndex,
              })}
              .searchQuery=${this.searchQuery}
              .bookmarkList=${this.bookmarkList}
            ></view-opentabs-card>
          `
        )}
        ${map(
          otherWindows,
          ([winID, tabs, win]) => html`
            <view-opentabs-card
              class=${cardClasses}
              .tabs=${tabs}
              .paused=${this.paused}
              data-inner-id=${win.windowGlobalChild.innerWindowId}
              data-l10n-id="firefoxview-opentabs-window-header"
              data-l10n-args=${JSON.stringify({ winID })}
              .searchQuery=${this.searchQuery}
              .bookmarkList=${this.bookmarkList}
            ></view-opentabs-card>
          `
        )}
      </div>
    `;
  }

  onSearchQuery(e) {
    if (!this.recentBrowsing) {
      Glean.firefoxviewNext.searchInitiatedSearch.record({
        page: "opentabs",
      });
    }
    this.searchQuery = e.detail.query;
  }

  onChangeSortOption(e) {
    this.sortOption = e.target.value;
    this.#setupTabChangeListener();
    if (!this.recentBrowsing) {
      Services.prefs.setStringPref(
        "browser.tabs.firefox-view.ui-state.opentabs.sort-option",
        this.sortOption
      );
    }
  }

  /**
   * Render a template for the 'Recent browsing' page, which shows a shorter list of
   * open tabs in the current window.
   *
   * @returns {TemplateResult}
   *   The recent browsing template.
   */
  getRecentBrowsingTemplate() {
    const tabs = this.openTabsTarget.getRecentTabs();
    return html`<view-opentabs-card
      .tabs=${tabs}
      .recentBrowsing=${true}
      .paused=${this.paused}
      .searchQuery=${this.searchQuery}
      .bookmarkList=${this.bookmarkList}
    ></view-opentabs-card>`;
  }

  handleEvent({ detail, type }) {
    if (this.recentBrowsing && type === "MozInputSearch:search") {
      this.onSearchQuery({ detail });
      return;
    }
    let windowIds;
    switch (type) {
      case "TabRecencyChange":
      case "TabChange":
        windowIds = detail.windowIds;
        this._updateWindowList();
        this.bookmarkList.setTrackedUrls(this.#getAllTabUrls());
        break;
    }
    if (this.recentBrowsing) {
      return;
    }
    if (windowIds?.length) {
      // there were tab changes to one or more windows
      for (let winId of windowIds) {
        const cardForWin = this.shadowRoot.querySelector(
          `view-opentabs-card[data-inner-id="${winId}"]`
        );
        if (this.searchQuery) {
          cardForWin?.updateSearchResults();
        }
        cardForWin?.requestUpdate();
      }
    } else {
      let winId = window.windowGlobalChild.innerWindowId;
      let cardForWin = this.shadowRoot.querySelector(
        `view-opentabs-card[data-inner-id="${winId}"]`
      );
      if (this.searchQuery) {
        cardForWin?.updateSearchResults();
      }
    }
  }

  async _updateWindowList() {
    this.windows = this.openTabsTarget.currentWindows;
  }
}
customElements.define("view-opentabs", OpenTabsInView);

/**
 * A card which displays a list of open tabs for a window.
 *
 * @property {boolean} showMore
 *   Whether to force all tabs to be shown, regardless of available space.
 * @property {MozTabbrowserTab[]} tabs
 *   The open tabs to show.
 * @property {string} title
 *   The window title.
 */
class OpenTabsInViewCard extends ViewPageContent {
  static properties = {
    showMore: { type: Boolean },
    tabs: { type: Array },
    title: { type: String },
    recentBrowsing: { type: Boolean },
    searchQuery: { type: String },
    searchResults: { type: Array },
    showAll: { type: Boolean },
    cumulativeSearches: { type: Number },
    bookmarkList: { type: Object },
  };
  static MAX_TABS_FOR_COMPACT_HEIGHT = 7;

  constructor() {
    super();
    this.showMore = false;
    this.tabs = [];
    this.title = "";
    this.recentBrowsing = false;
    this.devices = [];
    this.searchQuery = "";
    this.searchResults = null;
    this.showAll = false;
    this.cumulativeSearches = 0;
    this.controller = new lazy.OpenTabsController(this, {});
  }

  static queries = {
    cardEl: "card-container",
    tabContextMenu: "view-opentabs-contextmenu",
    tabList: "opentabs-tab-list",
  };

  openContextMenu(e) {
    let { originalEvent } = e.detail;
    this.tabContextMenu.toggle({
      triggerNode: e.originalTarget,
      originalEvent,
    });
  }

  getMaxTabsLength() {
    if (this.recentBrowsing && !this.showAll) {
      return MAX_TABS_FOR_RECENT_BROWSING;
    } else if (this.classList.contains("height-limited") && !this.showMore) {
      return OpenTabsInViewCard.MAX_TABS_FOR_COMPACT_HEIGHT;
    }
    return -1;
  }

  isShowAllLinkVisible() {
    return (
      this.recentBrowsing &&
      this.searchQuery &&
      this.searchResults.length > MAX_TABS_FOR_RECENT_BROWSING &&
      !this.showAll
    );
  }

  isShowMoreLinkVisible() {
    if (!this.classList.contains("height-limited")) {
      return false;
    }

    let tabCount = (this.searchQuery ? this.searchResults : this.tabs).length;
    return tabCount > OpenTabsInViewCard.MAX_TABS_FOR_COMPACT_HEIGHT;
  }

  toggleShowMore(event) {
    if (
      event.type == "click" ||
      (event.type == "keydown" && event.code == "Enter") ||
      (event.type == "keydown" && event.code == "Space")
    ) {
      event.preventDefault();
      this.showMore = !this.showMore;
    }
  }

  enableShowAll(event) {
    if (
      event.type == "click" ||
      (event.type == "keydown" && event.code == "Enter") ||
      (event.type == "keydown" && event.code == "Space")
    ) {
      event.preventDefault();
      Glean.firefoxviewNext.searchShowAllShowallbutton.record({
        section: "opentabs",
      });
      this.showAll = true;
    }
  }

  onTabListRowClick(event) {
    // Don't open pinned tab if mute/unmute indicator button selected
    if (
      Array.from(event.explicitOriginalTarget.classList).includes(
        "fxview-tab-row-pinned-media-button"
      )
    ) {
      return;
    }
    const tab = event.originalTarget.tabElement;
    const browserWindow = tab.documentGlobal;
    browserWindow.focus();
    browserWindow.gBrowser.selectedTab = tab;

    Glean.firefoxviewNext.openTabTabs.record({
      page: this.recentBrowsing ? "recentbrowsing" : "opentabs",
      window: this.title || "Window 1 (Current)",
    });
    if (this.searchQuery) {
      Glean.firefoxview.cumulativeSearches[
        this.recentBrowsing ? "recentbrowsing" : "opentabs"
      ].accumulateSingleSample(this.cumulativeSearches);
      this.cumulativeSearches = 0;
    }
  }

  closeTab(event) {
    const tab = event.originalTarget.tabElement;
    tab?.documentGlobal.gBrowser.removeTab(tab, {
      metricsContext: lazy.TabMetrics.userTriggeredContext(),
    });

    Glean.firefoxviewNext.closeOpenTabTabs.record();
  }

  viewVisibleCallback() {
    this.getRootNode().host.toggleVisibilityInCardContainer(true);
  }

  viewHiddenCallback() {
    this.getRootNode().host.toggleVisibilityInCardContainer(true);
  }

  firstUpdated() {
    this.getRootNode().host.toggleVisibilityInCardContainer(true);
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/firefoxview/firefoxview.css"
      />
      <card-container
        ?preserveCollapseState=${this.recentBrowsing}
        shortPageName=${this.recentBrowsing ? "opentabs" : null}
        ?showViewAll=${this.recentBrowsing}
        ?removeBlockEndMargin=${!this.recentBrowsing}
      >
        ${when(
          this.recentBrowsing,
          () =>
            html`<h3
              slot="header"
              data-l10n-id="firefoxview-opentabs-header"
            ></h3>`,
          () => html`<h3 slot="header">${this.title}</h3>`
        )}
        <div class="fxview-tab-list-container" slot="main">
          <opentabs-tab-list
            .hasPopup=${"menu"}
            ?compactRows=${this.classList.contains("width-limited")}
            @fxview-tab-list-primary-action=${this.onTabListRowClick}
            @fxview-tab-list-secondary-action=${this.openContextMenu}
            @fxview-tab-list-tertiary-action=${this.closeTab}
            secondaryActionClass="options-button"
            tertiaryActionClass="dismiss-button"
            .maxTabsLength=${this.getMaxTabsLength()}
            .tabItems=${this.searchResults ||
            this.controller.getTabListItems(this.tabs, this.recentBrowsing)}
            .searchQuery=${this.searchQuery}
            .pinnedTabsGridView=${!this.recentBrowsing}
            ><view-opentabs-contextmenu slot="menu"></view-opentabs-contextmenu>
          </opentabs-tab-list>
        </div>
        ${when(
          this.recentBrowsing,
          () =>
            html` <div
              @click=${this.enableShowAll}
              @keydown=${this.enableShowAll}
              data-l10n-id="firefoxview-show-all"
              ?hidden=${!this.isShowAllLinkVisible()}
              slot="footer"
              tabindex="0"
              role="link"
            ></div>`,
          () =>
            html` <div
              @click=${this.toggleShowMore}
              @keydown=${this.toggleShowMore}
              data-l10n-id=${this.showMore
                ? "firefoxview-show-less"
                : "firefoxview-show-more"}
              ?hidden=${!this.isShowMoreLinkVisible()}
              slot="footer"
              tabindex="0"
              role="link"
            ></div>`
        )}
      </card-container>
    `;
  }

  willUpdate(changedProperties) {
    if (changedProperties.has("searchQuery")) {
      this.showAll = false;
      this.cumulativeSearches = this.searchQuery
        ? this.cumulativeSearches + 1
        : 0;
    }
    if (changedProperties.has("searchQuery") || changedProperties.has("tabs")) {
      this.updateSearchResults();
    }
  }

  updateSearchResults() {
    this.searchResults = this.searchQuery
      ? searchTabList(
          this.searchQuery,
          this.controller.getTabListItems(this.tabs)
        )
      : null;
  }

  updated() {
    this.updateBookmarkStars();
  }

  async updateBookmarkStars() {
    const tabItems = [...this.tabList.tabItems];
    for (const row of tabItems) {
      const isBookmark = await this.bookmarkList.isBookmark(row.url);
      if (isBookmark && !row.indicators.includes("bookmark")) {
        row.indicators.push("bookmark");
      }
      if (!isBookmark && row.indicators.includes("bookmark")) {
        row.indicators = row.indicators.filter(i => i !== "bookmark");
      }
      row.primaryL10nId = this.controller.getPrimaryL10nId(
        this.isRecentBrowsing,
        row.indicators
      );
    }
    this.tabList.tabItems = tabItems;
  }
}
customElements.define("view-opentabs-card", OpenTabsInViewCard);

/**
 * A context menu of actions available for open tab list items.
 */
class OpenTabsContextMenu extends MozLitElement {
  static properties = {
    devices: { type: Array },
    triggerNode: { hasChanged: () => true, type: Object },
  };

  static queries = {
    panelList: "panel-list",
  };

  constructor() {
    super();
    this.triggerNode = null;
    this.boundObserve = (...args) => this.observe(...args);
    this.devices = [];
  }

  get logger() {
    return getLogger("OpenTabsContextMenu");
  }

  get ownerViewPage() {
    return this.ownerDocument.querySelector("view-opentabs");
  }

  connectedCallback() {
    super.connectedCallback();
    this.fetchDevicesPromise = this.fetchDevices();
    Services.obs.addObserver(this.boundObserve, TOPIC_DEVICELIST_UPDATED);
    Services.obs.addObserver(this.boundObserve, TOPIC_DEVICESTATE_CHANGED);
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    Services.obs.removeObserver(this.boundObserve, TOPIC_DEVICELIST_UPDATED);
    Services.obs.removeObserver(this.boundObserve, TOPIC_DEVICESTATE_CHANGED);
  }

  observe(_subject, topic, _data) {
    if (
      topic == TOPIC_DEVICELIST_UPDATED ||
      topic == TOPIC_DEVICESTATE_CHANGED
    ) {
      this.fetchDevicesPromise = this.fetchDevices();
    }
  }

  async fetchDevices() {
    const currentWindow = this.ownerViewPage.getWindow();
    if (currentWindow?.gSync) {
      try {
        await lazy.fxAccounts.device.refreshDeviceList();
      } catch (e) {
        this.logger.warn("Could not refresh the FxA device list", e);
      }
      this.devices = currentWindow.gSync.getSendTabTargets();
    }
  }

  async toggle({ triggerNode, originalEvent }) {
    if (this.panelList?.open) {
      // the menu will close so avoid all the other work to update its contents
      this.panelList.toggle(originalEvent);
      return;
    }
    this.triggerNode = triggerNode;
    await this.fetchDevicesPromise;
    await this.getUpdateComplete();
    this.panelList.toggle(originalEvent);

    if (this.devices.length >= 1) {
      Glean.firefoxview.sendTabExposed.record({
        device_count: String(this.devices.length),
      });
    }
  }

  copyLink(e) {
    lazy.BrowserUtils.copyLink(this.triggerNode.url, this.triggerNode.title);
    this.ownerViewPage.recordContextMenuTelemetry("copy-link", e);
  }

  closeTab(e) {
    const tab = this.triggerNode.tabElement;
    tab?.documentGlobal.gBrowser.removeTab(tab);
    this.ownerViewPage.recordContextMenuTelemetry("close-tab", e);
  }

  pinTab(e) {
    const tab = this.triggerNode.tabElement;
    tab?.documentGlobal.gBrowser.pinTab(tab);
    this.ownerViewPage.recordContextMenuTelemetry("pin-tab", e);
  }

  unpinTab(e) {
    const tab = this.triggerNode.tabElement;
    tab?.documentGlobal.gBrowser.unpinTab(tab);
    this.ownerViewPage.recordContextMenuTelemetry("unpin-tab", e);
  }

  toggleAudio(e) {
    const tab = this.triggerNode.tabElement;
    tab.toggleMuteAudio();
    this.ownerViewPage.recordContextMenuTelemetry(
      `${
        this.triggerNode.indicators.includes("muted") ? "unmute" : "mute"
      }-tab`,
      e
    );
  }

  moveTabsToStart(e) {
    const tab = this.triggerNode.tabElement;
    tab?.documentGlobal.gBrowser.moveTabsToStart(tab);
    this.ownerViewPage.recordContextMenuTelemetry("move-tab-start", e);
  }

  moveTabsToEnd(e) {
    const tab = this.triggerNode.tabElement;
    tab?.documentGlobal.gBrowser.moveTabsToEnd(tab);
    this.ownerViewPage.recordContextMenuTelemetry("move-tab-end", e);
  }

  moveTabsToWindow(e) {
    const tab = this.triggerNode.tabElement;
    tab?.documentGlobal.gBrowser.replaceTabsWithWindow(tab);
    this.ownerViewPage.recordContextMenuTelemetry("move-tab-window", e);
  }

  tabMetricsContext() {
    // No Firefox View source exists on TabMetrics. TAB_MENU matches the tab
    // strip's group actions.
    return lazy.TabMetrics.userTriggeredContext(
      lazy.TabMetrics.METRIC_SOURCE.TAB_MENU
    );
  }

  moveTabToNewGroup(e) {
    const tab = this.triggerNode?.tabElement;
    if (!tab?.documentGlobal?.gBrowser) {
      return;
    }
    // The group editor opens in the tab's window. Focus that window first so
    // the panel isn't buried when the tab lives in another window. Unlike the
    // tab strip, Firefox View doesn't select the tab, so this view stays put.
    tab.documentGlobal.focus();
    lazy.TabGroupMenu.createGroupFromTabs([tab], {
      metricsContext: this.tabMetricsContext(),
    });
    this.ownerViewPage.recordContextMenuTelemetry("move-tab-new-group", e);
  }

  moveTabToGroup(e) {
    const tab = this.triggerNode?.tabElement;
    const groupId = e.currentTarget.getAttribute("tab-group-id");
    const group = tab?.documentGlobal?.gBrowser?.getTabGroupById(groupId);
    if (!group) {
      return;
    }
    lazy.TabGroupMenu.addTabsToGroup([tab], group, this.tabMetricsContext());
    this.ownerViewPage.recordContextMenuTelemetry("move-tab-group", e);
  }

  moveTabToSavedGroup(e) {
    const tab = this.triggerNode?.tabElement;
    const groupId = e.currentTarget.getAttribute("tab-group-id");
    if (!tab?.documentGlobal?.gBrowser || !groupId) {
      return;
    }
    lazy.TabGroupMenu.addTabsToSavedGroup(
      [tab],
      groupId,
      this.tabMetricsContext()
    );
    this.ownerViewPage.recordContextMenuTelemetry("move-tab-saved-group", e);
  }

  ungroupTab(e) {
    const tab = this.triggerNode?.tabElement;
    if (!tab?.group) {
      return;
    }
    lazy.TabGroupMenu.ungroupTabs([tab]);
    this.ownerViewPage.recordContextMenuTelemetry("ungroup-tab", e);
  }

  groupMenuItemTemplate(group, isSaved) {
    const label = group.label ?? group.name;
    return html`<panel-item
      class=${isSaved
        ? "tab-group-icon tab-group-icon-closed"
        : "tab-group-icon"}
      style=${styleMap(lazy.TabGroupMenu.colorStyles(group.color))}
      tab-group-id=${group.id}
      data-l10n-id=${label ? nothing : "fxviewtabrow-unnamed-group"}
      @click=${isSaved ? this.moveTabToSavedGroup : this.moveTabToGroup}
      >${label ?? ""}</panel-item
    >`;
  }

  /**
   * Saved groups sit in this submenu under a heading. A nested panel-list
   * submenu resolves its owning panel to the outermost list, so Closed Groups
   * is not a further submenu.
   *
   * @param {MozTabbrowserTabGroup[]} openGroups
   * @param {object[]} savedGroups
   */
  tabGroupSubmenuTemplate(openGroups, savedGroups) {
    return html`<panel-list slot="submenu" id="move-tab-group-menu">
      <panel-item
        data-l10n-id="fxviewtabrow-move-tab-group-new"
        data-l10n-attrs="accesskey"
        @click=${this.moveTabToNewGroup}
      ></panel-item>
      ${when(
        openGroups.length,
        () => html`
          <hr />
          ${map(openGroups, group => this.groupMenuItemTemplate(group, false))}
        `
      )}
      ${when(
        savedGroups.length,
        () => html`
          <hr />
          <panel-item
            data-l10n-id="fxviewtabrow-move-tab-group-closed"
            disabled="true"
          ></panel-item>
          ${map(savedGroups, group => this.groupMenuItemTemplate(group, true))}
        `
      )}
    </panel-list>`;
  }

  tabGroupMenuTemplate() {
    const tab = this.triggerNode?.tabElement;
    if (!tab || !lazy.tabGroupsEnabled) {
      return null;
    }
    const { openGroups, savedGroups } = lazy.TabGroupMenu.getGroupsToMoveTo([
      tab,
    ]);
    if (!openGroups.length && !savedGroups.length) {
      return html`<panel-item
        data-l10n-id="fxviewtabrow-move-tab-new-group"
        data-l10n-attrs="accesskey"
        @click=${this.moveTabToNewGroup}
      ></panel-item>`;
    }
    return html`<panel-item
      data-l10n-id="fxviewtabrow-move-tab-group"
      data-l10n-attrs="accesskey"
      submenu="move-tab-group-menu"
      >${this.tabGroupSubmenuTemplate(openGroups, savedGroups)}</panel-item
    >`;
  }

  ungroupTabTemplate() {
    const tab = this.triggerNode?.tabElement;
    if (!tab?.group || !lazy.tabGroupsEnabled) {
      return null;
    }
    return html`<panel-item
      data-l10n-id="fxviewtabrow-ungroup-tab"
      data-l10n-attrs="accesskey"
      @click=${this.ungroupTab}
    ></panel-item>`;
  }

  moveMenuTemplate() {
    const tab = this.triggerNode?.tabElement;
    if (!tab) {
      return null;
    }
    const browserWindow = tab.documentGlobal;
    const tabs = browserWindow?.gBrowser.visibleTabs || [];
    const position = tabs.indexOf(tab);

    return html`
      <panel-list slot="submenu" id="move-tab-menu">
        ${position > 0
          ? html`<panel-item
              @click=${this.moveTabsToStart}
              data-l10n-id="fxviewtabrow-move-tab-start"
              data-l10n-attrs="accesskey"
            ></panel-item>`
          : null}
        ${position < tabs.length - 1
          ? html`<panel-item
              @click=${this.moveTabsToEnd}
              data-l10n-id="fxviewtabrow-move-tab-end"
              data-l10n-attrs="accesskey"
            ></panel-item>`
          : null}
        <panel-item
          @click=${this.moveTabsToWindow}
          data-l10n-id="fxviewtabrow-move-tab-window"
          data-l10n-attrs="accesskey"
        ></panel-item>
      </panel-list>
    `;
  }

  async sendTabToDevice(e) {
    let deviceId = e.target.getAttribute("device-id");
    let device = this.devices.find(dev => dev.id == deviceId);
    const viewPage = this.ownerViewPage;
    viewPage.recordContextMenuTelemetry("send-tab-device", e);

    Glean.firefoxview.clickSendTab.record({
      device_count: String(this.devices.length),
      action: "device",
    });

    if (device && this.triggerNode) {
      let chromeWindow = viewPage.getWindow();
      let tab = {
        url: this.triggerNode.url,
        title: this.triggerNode.title,
        private: lazy.PrivateBrowsingUtils.isWindowPrivate(chromeWindow),
      };
      await chromeWindow.gSync.sendTabToDevice(tab, [device]);
    }
  }

  onSendTabSubmenuClick() {
    Glean.firefoxview.sendTabOpened.record({
      device_count: String(this.devices.length),
    });
  }

  onSendTabSignedOutItemClick() {
    (async () => {
      let browser = this.ownerViewPage.getWindow().gBrowser;
      let data = {
        entrypoint: "send-tab-firefox-view-three-dots",
      };

      await lazy.SpecialMessageActions.fxaSignInFlow(data, browser);
    })();
  }

  onSendTabSyncDisabledItemClick() {
    this.ownerViewPage
      .getWindow()
      .openTrustedLinkIn("about:preferences#sync", "tab");
  }

  onSendTabConnectPhoneItemClick() {
    (async () => {
      const url = await lazy.FxAccounts.config.promisePairingURI({
        entrypoint: "send-tab-firefox-view-three-dots",
      });

      this.ownerViewPage.getWindow().switchToTabHavingURI(url, true, {});
    })();
  }

  onSendTabNoDeviceItemClick() {
    const url = Services.urlFormatter.formatURLPref(
      "identity.sendtab.deviceissues.url"
    );

    this.ownerViewPage.getWindow().switchToTabHavingURI(url, true, {
      replaceQueryString: true,
    });
  }

  onSendTabVerifyAccountClick() {
    this.ownerViewPage
      .getWindow()
      .openTrustedLinkIn("about:preferences#sync", "tab");
  }

  sendTabDevicesTemplate() {
    return html` <panel-list slot="submenu" id="send-tab-menu">
      ${this.devices.map(device => {
        return html`
          <panel-item @click=${this.sendTabToDevice} device-id=${device.id}
            >${device.name}</panel-item
          >
        `;
      })}
    </panel-list>`;
  }

  sendTabAccountUnverifiedTemplate() {
    return html`<panel-item
      data-l10n-id="fxviewtabrow-send-to-mobile"
      data-l10n-attrs="accesskey"
      submenu="send-tab-menu4"
    >
      <panel-list slot="submenu" id="send-tab-menu4">
        <panel-item
          data-l10n-id="fxviewtabrow-send-to-mobile-not-verified"
          disabled="true"
        >
        </panel-item>
        <hr />
        <panel-item
          data-l10n-id="fxviewtabrow-send-to-mobile-verify-account"
          @click=${this.onSendTabVerifyAccountClick}
        >
        </panel-item>
      </panel-list>
    </panel-item>`;
  }

  sendTabSignedOutTemplate() {
    return html`<panel-item
      data-l10n-id="fxviewtabrow-send-to-mobile"
      data-l10n-attrs="accesskey"
      submenu="send-tab-menu3"
    >
      <panel-list slot="submenu" id="send-tab-menu3">
        <panel-item
          data-l10n-id="fxviewtabrow-send-to-mobile-sign-in"
          @click=${this.onSendTabSignedOutItemClick}
        >
        </panel-item>
      </panel-list>
    </panel-item>`;
  }

  sendTabSyncDisabledTemplate() {
    return html`<panel-item
      data-l10n-id="fxviewtabrow-send-to-mobile"
      data-l10n-attrs="accesskey"
      submenu="send-tab-menu1"
    >
      <panel-list slot="submenu" id="send-tab-menu1">
        <panel-item
          data-l10n-id="fxviewtabrow-send-to-mobile-turn-on-sync"
          @click=${this.onSendTabSyncDisabledItemClick}
        >
        </panel-item>
      </panel-list>
    </panel-item>`;
  }

  sendTabSingleDeviceTemplate() {
    return html`<panel-item
      data-l10n-id="fxviewtabrow-send-to-mobile"
      data-l10n-attrs="accesskey"
      submenu="send-tab-menu2"
    >
      <panel-list slot="submenu" id="send-tab-menu2">
        <panel-item
          data-l10n-id="fxviewtabrow-send-to-mobile-connect-device"
          @click=${this.onSendTabConnectPhoneItemClick}
        >
        </panel-item>
        <hr />
        <panel-item
          data-l10n-id="fxviewtabrow-send-to-mobile-device-missing2"
          @click=${this.onSendTabNoDeviceItemClick}
        >
        </panel-item>
      </panel-list>
    </panel-item>`;
  }

  sendTabMultiDeviceTemplate(hasMobileOnlyTargets) {
    let multiDevicePanelItemId = hasMobileOnlyTargets
      ? "fxviewtabrow-send-to-mobile"
      : "fxviewtabrow-send-to-device";
    return html`<panel-item
      data-l10n-id=${multiDevicePanelItemId}
      data-l10n-attrs="accesskey"
      submenu="send-tab-menu"
      @click=${this.onSendTabSubmenuClick}
      >${this.sendTabDevicesTemplate()}</panel-item
    >`;
  }

  sendTabTemplate() {
    var sendTabPanel = null;
    let gSync = this.ownerViewPage.getWindow().gSync;

    // Ensure send tab is not displayed when FxA is disabled, particularly for Enterprise.
    if (!lazy.FXA_ENABLED) {
      return sendTabPanel;
    }

    switch (true) {
      case gSync.isUnverified:
        sendTabPanel = this.sendTabAccountUnverifiedTemplate();
        break;
      case gSync.isSignedIn === false:
        sendTabPanel = this.sendTabSignedOutTemplate();
        break;
      case gSync.isSignedInWithSyncDisabled:
        sendTabPanel = this.sendTabSyncDisabledTemplate();
        break;
      case gSync.hasNoSendTabTargets:
        sendTabPanel = this.sendTabSingleDeviceTemplate();
        break;
      default:
        var hasMobileOnlyTargets = gSync.hasOnlyMobileSendTabTargets();
        sendTabPanel = this.sendTabMultiDeviceTemplate(hasMobileOnlyTargets);
    }

    return sendTabPanel;
  }

  render() {
    const tab = this.triggerNode?.tabElement;
    if (!tab) {
      return null;
    }

    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/firefoxview/firefoxview.css"
      />
      <panel-list data-tab-type="opentabs">
        <panel-item
          data-l10n-id="fxviewtabrow-move-tab"
          data-l10n-attrs="accesskey"
          submenu="move-tab-menu"
          >${this.moveMenuTemplate()}</panel-item
        >
        ${this.tabGroupMenuTemplate()} ${this.ungroupTabTemplate()}
        <panel-item
          data-l10n-id=${tab.pinned
            ? "fxviewtabrow-unpin-tab"
            : "fxviewtabrow-pin-tab"}
          data-l10n-attrs="accesskey"
          @click=${tab.pinned ? this.unpinTab : this.pinTab}
        ></panel-item>
        <panel-item
          data-l10n-id=${tab.hasAttribute("muted")
            ? "fxviewtabrow-unmute-tab"
            : "fxviewtabrow-mute-tab"}
          data-l10n-attrs="accesskey"
          @click=${this.toggleAudio}
        ></panel-item>
        <hr />
        <panel-item
          data-l10n-id="fxviewtabrow-copy-link"
          data-l10n-attrs="accesskey"
          @click=${this.copyLink}
        ></panel-item>
        ${this.sendTabTemplate()}
      </panel-list>
    `;
  }
}
customElements.define("view-opentabs-contextmenu", OpenTabsContextMenu);
