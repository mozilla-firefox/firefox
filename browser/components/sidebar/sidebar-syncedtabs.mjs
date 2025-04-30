/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  SyncedTabsController: "resource:///modules/SyncedTabsController.sys.mjs",
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
    searchTextbox: "fxview-search-textbox",
  };

  constructor() {
    super();
    this.onSearchQuery = this.onSearchQuery.bind(this);
    this.onSecondaryAction = this.onSecondaryAction.bind(this);
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

  handleContextMenuEvent(e) {
    this.triggerNode = this.findTriggerNode(e, "sidebar-tab-row");
    if (!this.triggerNode) {
      e.preventDefault();
      return;
    }
    const contextMenu = this._contextMenu;
    const closeTabMenuItem = contextMenu.querySelector(
      "#sidebar-context-menu-close-remote-tab"
    );
    closeTabMenuItem.setAttribute(
      "data-l10n-args",
      this.triggerNode.secondaryL10nArgs
    );
    // Enable the feature only if the device supports it
    closeTabMenuItem.disabled = !this.triggerNode.canClose;
  }

  handleCommandEvent(e) {
    switch (e.target.id) {
      case "sidebar-context-menu-close-remote-tab":
        this.requestOrRemoveTabToClose(
          this.triggerNode.url,
          this.triggerNode.fxaDeviceId,
          this.triggerNode.secondaryActionClass
        );
        break;
      default:
        super.handleCommandEvent(e);
        break;
    }
  }

  handleSidebarFocusedEvent() {
    this.searchTextbox?.focus();
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
      icon
      class=${deviceType}
    >
      <sidebar-tab-list
        compactRows
        maxTabsLength="-1"
        .tabItems=${tabItems}
        .updatesPaused=${false}
        .searchQuery=${this.controller.searchQuery}
        @fxview-tab-list-primary-action=${navigateToLink}
        @fxview-tab-list-secondary-action=${this.onSecondaryAction}
      />
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
      icon
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
      icon
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
        </sidebar-panel-header>
        <fxview-search-textbox
          data-l10n-id="firefoxview-search-text-box-tabs"
          data-l10n-attrs="placeholder"
          @fxview-search-textbox-query=${this.onSearchQuery}
          size="15"
        ></fxview-search-textbox>
        ${when(
          messageCard,
          () => this.messageCardTemplate(messageCard),
          () => html`${this.deviceListTemplate()}`
        )}
      </div>
    `;
  }

  onSearchQuery(e) {
    this.controller.searchQuery = e.detail.query;
    this.requestUpdate();
  }
}

customElements.define("sidebar-syncedtabs", SyncedTabsInSidebar);
