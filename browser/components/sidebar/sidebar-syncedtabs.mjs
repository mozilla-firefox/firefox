/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SyncedTabsController: "resource:///modules/SyncedTabsController.sys.mjs",
  SidebarTreeView:
    "moz-src:///browser/components/sidebar/SidebarTreeView.sys.mjs",
});

import {
  html,
  ifDefined,
  when,
} from "chrome://global/content/vendor/lit.all.mjs";
import {
  escapeHtmlEntities,
  navigateToLink,
} from "chrome://browser/content/firefoxview/helpers.mjs";

import { SidebarPage } from "./sidebar-page.mjs";

class SyncedTabsInSidebar extends SidebarPage {
  controller = new lazy.SyncedTabsController(this);

  static queries = {
    cards: { all: "moz-card" },
    lists: { all: "sidebar-tab-list" },
    searchTextbox: "moz-input-search",
  };

  constructor() {
    super();
    this.onSearchQuery = this.onSearchQuery.bind(this);
    this.onSecondaryAction = this.onSecondaryAction.bind(this);
    this.treeView = new lazy.SidebarTreeView(this, { multiSelect: false });
  }

  connectedCallback() {
    super.connectedCallback();
    this.controller.addSyncObservers();
    this.controller.updateStates().then(() =>
      Glean.syncedTabs.sidebarToggle.record({
        opened: true,
        synced_tabs_loaded: this.controller.isSyncedTabsLoaded,
        version: "new",
      })
    );
    this.addContextMenuListeners();
    this.addSidebarFocusedListeners();
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.controller.removeSyncObservers();
    Glean.syncedTabs.sidebarToggle.record({
      opened: false,
      synced_tabs_loaded: this.controller.isSyncedTabsLoaded,
      version: "new",
    });
    this.removeContextMenuListeners();
    this.removeSidebarFocusedListeners();
  }

  #setContextMenuItemsVisibility(
    contextMenu,
    selectorsToShow = [],
    selectorsToHide = []
  ) {
    for (let selector of selectorsToShow) {
      contextMenu.querySelector(selector).hidden = false;
    }
    for (let selector of selectorsToHide) {
      contextMenu.querySelector(selector).hidden = true;
    }
    // Fix up separators, ensuring we only show them if there's visible items
    // before and after
    let visibleItemBefore = false,
      lastSeparator = null;
    for (let menuChild of contextMenu.children) {
      if (menuChild.localName == "menuseparator") {
        menuChild.hidden = true;
        // hide the separators intially, but mark for possible un-hiding if
        // a visible element follows
        if (visibleItemBefore) {
          lastSeparator = menuChild;
          visibleItemBefore = false;
        }
      } else if (!menuChild.hidden) {
        visibleItemBefore = true;
        if (lastSeparator) {
          lastSeparator.hidden = false;
        }
      }
    }
  }

  handleContextMenuEvent(e) {
    const contextMenu = this._contextMenu;
    const tabItemSelectors = [
      "#sidebar-synced-tabs-context-open-in-window",
      "#sidebar-synced-tabs-context-open-in-private-window",
      "#sidebar-context-menu-close-remote-tab",
      "#sidebar-synced-tabs-context-bookmark-tab",
      "#sidebar-synced-tabs-context-copy-link",
    ];
    const deviceItemSelectors = [
      "#sidebar-synced-tabs-context-open-all-in-tabs",
      "#sidebar-synced-tabs-context-connect-another-device",
      "#sidebar-synced-tabs-context-manage-this-device",
    ];

    let triggerNode = this.findTriggerNode(e, "sidebar-tab-row");
    if (triggerNode) {
      this.triggerNode = triggerNode;
      const closeTabMenuItem = contextMenu.querySelector(
        "#sidebar-context-menu-close-remote-tab"
      );
      closeTabMenuItem.setAttribute(
        "data-l10n-args",
        this.triggerNode.secondaryL10nArgs
      );
      // Enable the feature only if the device supports it
      closeTabMenuItem.disabled = !this.triggerNode.canClose;
      // Show the context menu items for tab-row items and hide the device ones
      this.#setContextMenuItemsVisibility(
        contextMenu,
        tabItemSelectors,
        deviceItemSelectors
      );

      let privateWindowMenuItem = contextMenu.querySelector(
        "#sidebar-synced-tabs-context-open-in-private-window"
      );
      privateWindowMenuItem.hidden = !lazy.PrivateBrowsingUtils.enabled;
    } else if ((triggerNode = e.composedTarget.closest("summary"))) {
      this.triggerNode = triggerNode;
      // Show the context menu items device ones and hide the tab-row ones
      this.#setContextMenuItemsVisibility(
        contextMenu,
        deviceItemSelectors,
        tabItemSelectors
      );
    } else {
      this.triggerNode = this.findTriggerNode(e, "moz-input-search");
      if (!this.triggerNode) {
        e.preventDefault();
      }
    }
  }

  async handleCommandEvent(e) {
    let label;
    switch (e.target.id) {
      case "sidebar-context-menu-close-remote-tab":
        this.requestOrRemoveTabToClose(
          this.triggerNode.url,
          this.triggerNode.fxaDeviceId,
          this.triggerNode.secondaryActionClass
        );
        label = "close_tab_on_connected_device";
        break;
      case "sidebar-synced-tabs-context-open-in-window":
        super.handleCommandEvent(e);
        label = "open_in_new_window";
        break;
      case "sidebar-synced-tabs-context-open-in-private-window":
        super.handleCommandEvent(e);
        label = "open_in_private_window";
        break;
      case "sidebar-synced-tabs-context-bookmark-tab": {
        const guid = await super.handleCommandEvent(e);
        const outcome = guid ? "confirmed" : "cancelled";
        Glean.browserUiInteraction.sidebarSyncedTabs[
          `bookmark_tab_${outcome}`
        ].add(1);
        break;
      }
      case "sidebar-synced-tabs-context-copy-link":
        super.handleCommandEvent(e);
        label = "copy_link";
        break;
      case "sidebar-synced-tabs-context-open-all-in-tabs":
        this.openAllSyncedTabs(e);
        break;
      case "sidebar-synced-tabs-context-connect-another-device":
        this.topWindow.gSync.openConnectAnotherDevice("syncedtabs-sidebar");
        break;
      case "sidebar-synced-tabs-context-manage-this-device":
        this.topWindow.gSync.openDevicesManagementPage("syncedtabs-sidebar");
        break;
      default:
        super.handleCommandEvent(e);
        break;
    }
    if (label) {
      Glean.browserUiInteraction.sidebarSyncedTabs[label].add(1);
    }
  }

  openAllSyncedTabs() {
    let card = this.triggerNode.getRootNode().host;
    let tabList = card.querySelector("sidebar-tab-list");
    let urls = tabList?.tabItems?.map(item => item.url).filter(Boolean);
    if (!urls?.length) {
      return;
    }
    this.topWindow.gBrowser.loadTabs(urls, {
      replace: false,
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });
  }

  handleSidebarFocusedEvent() {
    this.searchTextbox?.focus();
  }

  handleNavigateToLink(e) {
    navigateToLink(e, undefined, { forceNewTab: false });
    // TO DO: update the below to handle multiple links opened at once. Bug 2024639
    Glean.sidebar.link.synced_tabs.add(1);
    this.treeView.resetSelection();
  }

  onSecondaryAction(e) {
    const { url, fxaDeviceId, secondaryActionClass } = e.originalTarget;
    this.requestOrRemoveTabToClose(url, fxaDeviceId, secondaryActionClass);
  }

  requestOrRemoveTabToClose(url, fxaDeviceId, secondaryActionClass) {
    if (secondaryActionClass === "dismiss-button") {
      // Set new pending close tab
      this.controller.requestCloseRemoteTab(fxaDeviceId, url);
    } else if (secondaryActionClass === "undo-button") {
      // User wants to undo
      this.controller.removePendingTabToClose(fxaDeviceId, url);
    }
    this.requestUpdate();
  }

  /**
   * The template shown when the list of synced devices is currently
   * unavailable.
   *
   * @param {object} options
   * @param {string} options.action
   * @param {string} options.buttonLabel
   * @param {string[]} options.descriptionArray
   * @param {string} options.descriptionLink
   * @param {string} options.header
   * @param {string} options.mainImageUrl
   * @returns {TemplateResult}
   */
  messageCardTemplate({
    action,
    buttonLabel,
    descriptionArray,
    descriptionLink,
    header,
    mainImageUrl,
  }) {
    return html`
      <fxview-empty-state
        headerLabel=${header}
        .descriptionLabels=${descriptionArray}
        .descriptionLink=${ifDefined(descriptionLink)}
        class="empty-state synced-tabs error"
        isSelectedTab
        mainImageUrl=${ifDefined(mainImageUrl)}
        id="empty-container"
      >
        <moz-button
          type="primary"
          slot="primary-action"
          ?hidden=${!buttonLabel}
          data-l10n-id=${ifDefined(buttonLabel)}
          data-action=${action}
          @click=${e => this.controller.handleEvent(e)}
        ></moz-button>
      </fxview-empty-state>
    `;
  }

  /**
   * The template shown for a device that has tabs.
   *
   * @param {string} deviceName
   * @param {string} deviceType
   * @param {Array} tabItems
   * @returns {TemplateResult}
   */
  deviceTemplate(deviceName, deviceType, tabItems) {
    return html`<moz-card
      type="accordion"
      expanded
      .heading=${deviceName}
      .iconSrc=${this.getDeviceIconSrc(deviceType)}
      class=${deviceType}
      @keydown=${e => this.treeView.handleCardKeydown(e)}
    >
      <sidebar-tab-list
        compactRows
        maxTabsLength="-1"
        .tabItems=${tabItems}
        .multiSelect=${false}
        .updatesPaused=${false}
        .searchQuery=${this.controller.searchQuery}
        @fxview-tab-list-primary-action=${this.handleNavigateToLink}
        @fxview-tab-list-secondary-action=${this.onSecondaryAction}
        @fxview-tab-list-middleclick-action=${this.handleNavigateToLink}
      ></sidebar-tab-list>
    </moz-card>`;
  }

  /**
   * The template shown for a device that has no tabs.
   *
   * @param {string} deviceName
   * @param {string} deviceType
   * @returns {TemplateResult}
   */
  noDeviceTabsTemplate(deviceName, deviceType) {
    return html`<moz-card
      .heading=${deviceName}
      .iconSrc=${this.getDeviceIconSrc(deviceType)}
      class=${deviceType}
      data-l10n-id="firefoxview-syncedtabs-device-notabs"
    >
    </moz-card>`;
  }

  /**
   * The template shown for a device that has tabs, but no tabs that match the
   * current search query.
   *
   * @param {string} deviceName
   * @param {string} deviceType
   * @returns {TemplateResult}
   */
  noSearchResultsTemplate(deviceName, deviceType) {
    return html`<moz-card
      .heading=${deviceName}
      .iconSrc=${this.getDeviceIconSrc(deviceType)}
      class=${deviceType}
      data-l10n-id="firefoxview-search-results-empty"
      data-l10n-args=${JSON.stringify({
        query: escapeHtmlEntities(this.controller.searchQuery),
      })}
    >
    </moz-card>`;
  }

  /**
   * The template shown for the list of synced devices.
   *
   * @returns {TemplateResult[]}
   */
  deviceListTemplate() {
    return Object.values(this.controller.getRenderInfo()).map(
      ({ name: deviceName, deviceType, tabItems, canClose, tabs }) => {
        if (tabItems.length) {
          return this.deviceTemplate(
            deviceName,
            deviceType,
            this.getTabItems(tabItems, deviceName, canClose)
          );
        } else if (tabs.length) {
          return this.noSearchResultsTemplate(deviceName, deviceType);
        }
        return this.noDeviceTabsTemplate(deviceName, deviceType);
      }
    );
  }

  getTabItems(items, deviceName, canClose) {
    return items
      .map(item => {
        // We always show the option to close remotely on right-click but
        // disable it if the device doesn't support actually closing it
        let secondaryL10nId = "synced-tabs-context-close-tab-title";
        let secondaryL10nArgs = JSON.stringify({ deviceName });
        if (!canClose) {
          return {
            ...item,
            canClose,
            secondaryL10nId,
            secondaryL10nArgs,
          };
        }

        // Default show the close/dismiss button
        let secondaryActionClass = "dismiss-button";
        item.closeRequested = false;

        // If this item has been requested to be closed, show
        // the undo instead
        if (item.url === this.controller.lastClosedURL) {
          secondaryActionClass = "undo-button";
          secondaryL10nId = "text-action-undo";
          secondaryL10nArgs = null;
          item.closeRequested = true;
        }

        return {
          ...item,
          canClose,
          secondaryActionClass,
          secondaryL10nId,
          secondaryL10nArgs,
        };
      })
      .filter(
        item =>
          !this.controller.isURLQueuedToClose(item.fxaDeviceId, item.url) ||
          item.url === this.controller.lastClosedURL
      );
  }

  getDeviceIconSrc(deviceType) {
    const phone = "chrome://browser/skin/device-phone.svg";
    const desktop = "chrome://browser/skin/device-desktop.svg";
    const tablet = "chrome://browser/skin/device-tablet.svg";

    const deviceIcons = {
      desktop,
      mobile: phone,
      phone,
      tablet,
    };

    return deviceIcons[deviceType] || null;
  }

  render() {
    const messageCard = this.controller.getMessageCard();
    return html`
      ${this.stylesheet()}
      <div class="sidebar-panel">
        <sidebar-panel-header
          data-l10n-id="sidebar-menu-syncedtabs-header"
          data-l10n-attrs="heading"
          view="viewTabsSidebar"
        >
          <moz-input-search
            data-l10n-id="firefoxview-search-text-box-tabs"
            data-l10n-attrs="placeholder"
            @MozInputSearch:search=${this.onSearchQuery}
          ></moz-input-search>
        </sidebar-panel-header>
        <div class="sidebar-panel-scrollable-content">
          ${when(
            messageCard,
            () => this.messageCardTemplate(messageCard),
            () => html`${this.deviceListTemplate()}`
          )}
        </div>
      </div>
    `;
  }

  onSearchQuery(e) {
    this.controller.searchQuery = e.detail.query;
    this.requestUpdate();
    Glean.browserUiInteraction.sidebarSyncedTabs.search.add(1);
  }
}

customElements.define("sidebar-syncedtabs", SyncedTabsInSidebar);
