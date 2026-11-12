/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, when } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { escapeHtmlEntities } from "chrome://browser/content/firefoxview/helpers.mjs";

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const BROWSER_NEW_TAB_URL = "about:newtab";
const BROWSER_OPEN_TABS_URL = "about:opentabs";

const lazy = XPCOMUtils.declareLazy({
  OpenTabsController: "resource:///modules/OpenTabsController.sys.mjs",
  NonPrivateTabs: "resource:///modules/OpenTabs.sys.mjs",
  getTabsTargetForWindow: "resource:///modules/OpenTabs.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
});

/**
 * A collection of open, unpinned, unsplit tabs for the current by window.
 */
class OpenTabsInSplitView extends MozLitElement {
  static properties = {
    searchQuery: { type: String },
  };

  static queries = {
    sidebarTabList: "sidebar-tab-list",
    searchTextbox: "moz-input-search",
  };

  currentWindow = null;
  openTabsTarget = null;

  constructor() {
    super();
    this.currentWindow = /** @type {CanonicalBrowsingContext} */ (
      this.documentGlobal.top.browsingContext
    ).embedderWindowGlobal.browsingContext.window;
    if (lazy.PrivateBrowsingUtils.isWindowPrivate(this.currentWindow)) {
      this.openTabsTarget = lazy.getTabsTargetForWindow(this.currentWindow);
    } else {
      this.openTabsTarget = lazy.NonPrivateTabs;
    }
    this.controller = new lazy.OpenTabsController();
    this.listenersAdded = false;
    this.searchQuery = "";
  }

  connectedCallback() {
    super.connectedCallback();
    this.addListeners(true);
    this.currentWindow.addEventListener("TabSelect", this);
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.removeListeners();
    this.currentWindow.removeEventListener("TabSelect", this);
  }

  addListeners(skipUpdate) {
    if (!this.listenersAdded) {
      this.openTabsTarget.addEventListener("TabChange", this);
      if (!skipUpdate) {
        this.requestUpdate();
      }
      this.listenersAdded = true;
    }
  }

  removeListeners() {
    if (this.listenersAdded) {
      this.openTabsTarget.removeEventListener("TabChange", this);
      this.listenersAdded = false;
    }
  }

  handleEvent(e) {
    switch (e.type) {
      case "TabChange":
        this.requestUpdate();
        break;
      case "TabSelect":
        if (this.currentSplitView) {
          this.addListeners();
          this.requestUpdate();
        } else {
          this.removeListeners();
        }
        break;
    }
  }

  getWindow() {
    return /** @type {CanonicalBrowsingContext} */ (window.browsingContext)
      .embedderWindowGlobal.browsingContext.window;
  }

  get currentSplitView() {
    const { gBrowser } = this.getWindow();
    return gBrowser.selectedTab.splitview;
  }

  onTabListRowClick(event) {
    const { gBrowser } = this.getWindow();
    const tab = event.originalTarget.tabElement;
    if (this.currentSplitView) {
      let aboutOpenTabsTab = gBrowser.getTabForBrowser(
        window.browsingContext.embedderElement
      );
      this.currentSplitView.replaceTab(aboutOpenTabsTab, tab);
    }
  }

  get allAvailableTabs() {
    const { gBrowser } = this.getWindow();
    return gBrowser.visibleTabs.filter(tab => {
      return (
        !tab.pinned &&
        !tab.splitview &&
        tab?.linkedBrowser?.currentURI?.spec !== BROWSER_OPEN_TABS_URL
      );
    });
  }

  get nonSplitViewUnpinnedTabs() {
    let tabs = this.allAvailableTabs;

    if (this.searchQuery) {
      const query = this.searchQuery.toLowerCase();
      tabs = tabs.filter(tab => {
        const title = tab.label?.toLowerCase() || "";
        const url = tab.linkedBrowser?.currentURI?.spec?.toLowerCase() || "";
        return title.includes(query) || url.includes(query);
      });
    }

    return tabs;
  }

  onSearchQuery(e) {
    this.searchQuery = e.detail.query;
  }

  render() {
    const { gBrowser } = this.getWindow();
    const allTabs = this.allAvailableTabs;
    const filteredTabs = this.nonSplitViewUnpinnedTabs;
    const isSearching = !!this.searchQuery;

    if (
      !allTabs.length ||
      (gBrowser.selectedTab.linkedBrowser.currentURI.spec ===
        BROWSER_OPEN_TABS_URL &&
        !this.currentSplitView)
    ) {
      // If there are no unpinned, unsplit tabs to display or about:opentabs
      // is opened outside of a split view, open about:newtab instead.
      //
      // Given this is still during the initialization, wait for the
      // next microtask checkpoint.
      queueMicrotask(() => {
        this.getWindow().openTrustedLinkIn(BROWSER_NEW_TAB_URL, "current");
      });
      return null;
    }
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/tabbrowser/opentabs-splitview.css"
      />
      <link
        rel="stylesheet"
        href="chrome://browser/content/firefoxview/firefoxview.css"
      />
      <div class="sticky-header">
        <h3 data-l10n-id="opentabs-page-title"></h3>
        <moz-input-search
          data-l10n-id="opentabs-search-text-box"
          data-l10n-attrs="placeholder"
          @MozInputSearch:search=${this.onSearchQuery}
        ></moz-input-search>
      </div>
      ${isSearching
        ? html`<moz-card
            data-l10n-id="opentabs-search-results-header"
            data-l10n-attrs="heading"
            data-l10n-args=${JSON.stringify({
              query: this.searchQuery,
            })}
          >
            ${when(
              filteredTabs.length,
              () => html`
                <sidebar-tab-list
                  maxTabsLength="-1"
                  .tabItems=${this.controller.getTabListItems(
                    filteredTabs,
                    false
                  )}
                  @fxview-tab-list-primary-action=${this.onTabListRowClick}
                >
                </sidebar-tab-list>
              `,
              () => html`
                <div
                  class="empty-search-message"
                  data-l10n-id="firefoxview-search-results-empty"
                  data-l10n-args=${JSON.stringify({
                    query: escapeHtmlEntities(this.searchQuery),
                  })}
                ></div>
              `
            )}
          </moz-card>`
        : html`<moz-card>
            <sidebar-tab-list
              maxTabsLength="-1"
              .tabItems=${this.controller.getTabListItems(filteredTabs, false)}
              @fxview-tab-list-primary-action=${this.onTabListRowClick}
            >
            </sidebar-tab-list>
          </moz-card>`}
    `;
  }
}
customElements.define("splitview-opentabs", OpenTabsInSplitView);

window.addEventListener(
  "unload",
  () => {
    // Clear out the document so the disconnectedCallback will trigger
    // properly
    document.body.textContent = "";
  },
  { once: true }
);
