/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ObjectUtils: "resource://gre/modules/ObjectUtils.sys.mjs",
  SyncedTabs: "resource://services-sync/SyncedTabs.sys.mjs",
  SyncedTabsManagement: "resource://services-sync/SyncedTabs.sys.mjs",
  COMMAND_CLOSETAB: "resource://gre/modules/FxAccountsCommon.sys.mjs",
});

import { SyncedTabsErrorHandler } from "resource:///modules/firefox-view-synced-tabs-error-handler.sys.mjs";
import { TabsSetupFlowManager } from "resource:///modules/firefox-view-tabs-setup-manager.sys.mjs";
import { searchTabList } from "chrome://browser/content/firefoxview/search-helpers.mjs";

const SYNCED_TABS_CHANGED = "services.sync.tabs.changed";
const TOPIC_SETUPSTATE_CHANGED = "firefox-view.setupstate.changed";

/**
 * The controller for synced tabs components.
 *
 * @implements {ReactiveController}
 */
export class SyncedTabsController {
  /**
   * @type {boolean}
   */
  contextMenu;
  currentSetupStateIndex = -1;
  currentSyncedTabs = [];
  devices = [];
  /**
   * The current error state as determined by `SyncedTabsErrorHandler`.
   *
   * @type {number}
   */
  errorState = null;
  /**
   * Component associated with this controller.
   *
   * @type {ReactiveControllerHost}
   */
  host;
  /**
   * @type {Function}
   */
  pairDeviceCallback;
  searchQuery = "";
  /**
   * @type {Function}
   */
  signupCallback;

  /**
   * Construct a new SyncedTabsController.
   *
   * @param {ReactiveControllerHost} host
   * @param {object} options
   * @param {boolean} [options.contextMenu]
   *   Whether synced tab items have a secondary context menu.
   * @param {Function} [options.pairDeviceCallback]
   *   The function to call when the pair device window is opened.
   * @param {Function} [options.signupCallback]
   *   The function to call when the signup window is opened.
   */
  constructor(host, { contextMenu, pairDeviceCallback, signupCallback } = {}) {
    this.contextMenu = contextMenu;
    this.pairDeviceCallback = pairDeviceCallback;
    this.signupCallback = signupCallback;
    this.observe = this.observe.bind(this);
    this.host = host;
    this.host.addController(this);
    // Track tabs requested close per device but not-yet-sent,
    // it'll be in the form of {fxaDeviceId: Set(urls)}
    this._pendingCloseTabs = new Map();
    // The last closed URL, for undo purposes
    this.lastClosedURL = null;
  }

  hostConnected() {
    this.host.addEventListener("click", this);
  }

  hostDisconnected() {
    this.host.removeEventListener("click", this);
  }

  get isSyncedTabsLoaded() {
    return this.currentSetupStateIndex === 4;
  }

  addSyncObservers() {
    Services.obs.addObserver(this.observe, SYNCED_TABS_CHANGED);
    Services.obs.addObserver(this.observe, TOPIC_SETUPSTATE_CHANGED);
  }

  removeSyncObservers() {
    Services.obs.removeObserver(this.observe, SYNCED_TABS_CHANGED);
    Services.obs.removeObserver(this.observe, TOPIC_SETUPSTATE_CHANGED);
  }

  handleEvent(event) {
    if (event.type == "click" && event.target.dataset.action) {
      const { ErrorType } = SyncedTabsErrorHandler;
      switch (event.target.dataset.action) {
        case `${ErrorType.SYNC_ERROR}`:
        case `${ErrorType.NETWORK_OFFLINE}`:
        case `${ErrorType.PASSWORD_LOCKED}`: {
          TabsSetupFlowManager.tryToClearError();
          break;
        }
        case `${ErrorType.SIGNED_OUT}`:
        case "sign-in": {
          TabsSetupFlowManager.openFxASignup(event.target.documentGlobal);
          this.signupCallback?.();
          break;
        }
        case "add-device": {
          TabsSetupFlowManager.openFxAPairDevice(event.target.documentGlobal);
          this.pairDeviceCallback?.();
          break;
        }
        case "sync-tabs-disabled": {
          TabsSetupFlowManager.syncOpenTabs(event.target);
          break;
        }
        case `${ErrorType.SYNC_DISCONNECTED}`: {
          const win = event.target.documentGlobal;
          const { switchToTabHavingURI } =
            win.docShell.chromeEventHandler.documentGlobal;
          switchToTabHavingURI(
            "about:preferences?action=choose-what-to-sync#sync",
            true,
            {}
          );
          break;
        }
      }
    } else if (event.type == "click" && event.composedTarget.href) {
      event.preventDefault();
      const { switchToTabHavingURI } =
        event.view.browsingContext.topChromeWindow;
      switchToTabHavingURI(event.composedTarget.href, true, {
        ignoreFragment: "whenComparingAndReplace",
      });
    }
  }

  async observe(_, topic, errorState) {
    if (topic == TOPIC_SETUPSTATE_CHANGED) {
      await this.updateStates(errorState);
    }
    if (topic == SYNCED_TABS_CHANGED) {
      // Usually this means we performed a sync, so clear the
      // "in-queue" things as those most likely got flushed
      this._pendingCloseTabs = new Map();
      this.lastClosedURL = null;
      await this.getSyncedTabData();
    }
  }

  async updateStates(errorState) {
    let stateIndex = TabsSetupFlowManager.uiStateIndex;
    errorState = errorState || SyncedTabsErrorHandler.getErrorType();

    if (stateIndex == 4 && this.currentSetupStateIndex !== stateIndex) {
      // trigger an initial request for the synced tabs list
      await this.getSyncedTabData();
    }

    this.currentSetupStateIndex = stateIndex;
    this.errorState = errorState;
    this.host.requestUpdate();
  }

  /**
   * @typedef {object} ActionMapping
   * @property {string} [asset]
   *   URL for an illustration to accompany the action card
   * @property {string} header
   *   Fluent ID for header string
   * @property {string} description
   *   Fluent ID for description string
   * @property {string} [buttonLabel]
   *   Fluent ID for button string (if this state has a button)
   * @property {{name: string, url: string}} [descriptionLink]
   *   Additional localization properties for the `description` string if it
   *   has an inline link; `name` should match the `data-l10n-name` of the
   *   <a> tag and `url` will be used as the href of the <a> tag.
   */

  /** @typedef {"sign-in"|"add-device"|"sync-tabs-disabled"|"loading"} Action */

  /** @type {{[A in Action]: ActionMapping}} */
  actionMappings = {
    "sign-in": {
      asset: "chrome://browser/content/firefoxview/synced-tabs-empty.svg",
      header: "firefoxview-syncedtabs-signin-header-2",
      description: "firefoxview-syncedtabs-signin-description-2",
      buttonLabel: "firefoxview-syncedtabs-signin-primarybutton-2",
    },
    "add-device": {
      asset: "chrome://browser/content/firefoxview/synced-tabs-empty.svg",
      header: "firefoxview-syncedtabs-adddevice-header-2",
      description: "firefoxview-syncedtabs-adddevice-description-2",
      buttonLabel: "firefoxview-syncedtabs-adddevice-primarybutton",
      descriptionLink: {
        name: "url",
        url: "https://support.mozilla.org/kb/how-do-i-set-sync-my-computer#w_connect-additional-devices-to-sync",
      },
    },
    "sync-tabs-disabled": {
      asset: "chrome://browser/content/firefoxview/synced-tabs-empty.svg",
      header: "firefoxview-syncedtabs-synctabs-header",
      description: "firefoxview-syncedtabs-synctabs-description",
      buttonLabel: "firefoxview-tabpickup-synctabs-primarybutton",
    },
    loading: {
      asset: "chrome://browser/content/firefoxview/synced-tabs-empty.svg",
      header: "firefoxview-syncedtabs-loading-header",
      description: "firefoxview-syncedtabs-loading-description",
    },
  };

  /** @type {{[A in Action]: ActionMapping}} */
  novaActionMappings = {
    "sign-in": {
      asset: "chrome://browser/skin/sidebar/kit-tabs-devices.svg",
      header: "firefoxview-syncedtabs-signin-header-3",
      description: "firefoxview-syncedtabs-signin-description-3",
      buttonLabel: "firefoxview-syncedtabs-signin-primarybutton-2",
    },
    "add-device": {
      asset: "chrome://browser/skin/sidebar/kit-qr-tabs-devices-empty.svg",
      header: "firefoxview-syncedtabs-adddevice-header-3",
      description: "firefoxview-syncedtabs-adddevice-description-3",
      descriptionLink: {
        name: "url",
        url: "https://support.mozilla.org/kb/how-do-i-set-sync-my-computer#w_connect-additional-devices-to-sync",
      },
    },
    "sync-tabs-disabled": {
      asset: "chrome://browser/skin/sidebar/kit-tabs-devices.svg",
      header: "firefoxview-syncedtabs-synctabs-header-2",
      description: "firefoxview-syncedtabs-synctabs-description-2",
      buttonLabel: "firefoxview-tabpickup-synctabs-primarybutton-2",
    },
    loading: {
      asset: "chrome://browser/skin/sidebar/kit-tabs-devices.svg",
      header: "firefoxview-syncedtabs-loading-header-2",
      description: "firefoxview-syncedtabs-loading-description-2",
    },
  };

  #getMessageCardForState({ error = false, action, errorState }) {
    errorState = errorState || this.errorState;
    const nova = Services.prefs.getBoolPref("browser.nova.enabled", false);
    let header, description, descriptionLink, buttonLabel, mainImageUrl;
    let descriptionArray;
    let mappings;
    if (error) {
      mappings =
        SyncedTabsErrorHandler.getFluentStringsForErrorType(errorState);
      action = `${errorState}`;
    } else {
      mappings = nova
        ? this.novaActionMappings[action]
        : this.actionMappings[action];
    }
    header = mappings.header;
    description = mappings.description;
    buttonLabel = mappings.buttonLabel;
    descriptionLink = mappings.descriptionLink;
    mainImageUrl = mappings.asset;
    descriptionArray = [description];

    return {
      action,
      buttonLabel,
      descriptionArray,
      descriptionLink,
      error,
      header,
      mainImageUrl,
    };
  }

  getRenderInfo() {
    let renderInfo = {};
    for (let tab of this.currentSyncedTabs) {
      if (!(tab.client in renderInfo)) {
        renderInfo[tab.client] = {
          name: tab.device,
          deviceType: tab.deviceType,
          canClose: !!tab.availableCommands[lazy.COMMAND_CLOSETAB],
          tabs: [],
        };
      }
      renderInfo[tab.client].tabs.push(tab);
    }

    // Add devices without tabs
    for (let device of this.devices) {
      if (!(device.id in renderInfo)) {
        renderInfo[device.id] = {
          name: device.name,
          deviceType: device.clientType,
          tabs: [],
        };
      }
    }

    for (let id in renderInfo) {
      renderInfo[id].tabItems = this.searchQuery
        ? searchTabList(this.searchQuery, this.getTabItems(renderInfo[id]))
        : this.getTabItems(renderInfo[id]);
    }
    return renderInfo;
  }

  getMessageCard() {
    switch (this.currentSetupStateIndex) {
      case 0 /* error-state */:
        if (this.errorState) {
          return this.#getMessageCardForState({
            error: true,
          });
        }
        return this.#getMessageCardForState({ action: "loading" });
      case 1 /* not-signed-in */:
        if (Services.prefs.prefHasUserValue("services.sync.lastversion")) {
          // If this pref is set, the user has signed out of sync.
          // This path is also taken if we are disconnected from sync. See bug 1784055
          return this.#getMessageCardForState({
            error: true,
            errorState: "signed-out",
          });
        }
        return this.#getMessageCardForState({ action: "sign-in" });
      case 2 /* connect-secondary-device*/:
        return this.#getMessageCardForState({ action: "add-device" });
      case 3 /* disabled-tab-sync */:
        return this.#getMessageCardForState({ action: "sync-tabs-disabled" });
      case 4 /* synced-tabs-loaded*/:
        // There seems to be an edge case where sync says everything worked
        // fine but we have no devices.
        if (!this.devices.length) {
          return this.#getMessageCardForState({ action: "add-device" });
        }
    }
    return null;
  }

  /**
   * Turn renderInfo into a list of tabs for syncedtabs-tab-list
   *
   * @param {object} renderInfo
   * @param {Array<object>} [renderInfo.tabs]
   *   tabs to display to the user
   * @param {string} [renderInfo.name]
   *   The name of the device for use when the user hovers over
   *   the close button for context
   * @param {boolean} [renderInfo.canClose]
   *   Whether the list should support remotely closing tabs
   */
  getTabItems({ tabs, name, canClose }) {
    return tabs
      ?.map(tab => {
        let tabItem = {
          icon: tab.icon,
          title: tab.title,
          time: tab.lastUsed * 1000,
          url: tab.url,
          fxaDeviceId: tab.fxaDeviceId,
          secondaryL10nId: this.contextMenu
            ? "fxviewtabrow-options-menu-button"
            : undefined,
          secondaryL10nArgs: this.contextMenu
            ? JSON.stringify({ tabTitle: tab.title })
            : undefined,
        };
        // We don't want to show the option to close remotely if the
        // device doesn't support it
        if (!canClose) {
          return tabItem;
        }

        // If this item has been requested to be closed, show
        // the undo instead until removed from the list
        if (tabItem.url === this.lastClosedURL) {
          tabItem.tertiaryL10nId = "text-action-undo";
          tabItem.tertiaryActionClass = "undo-button";
          tabItem.tertiaryL10nArgs = null;
          tabItem.closeRequested = true;
        } else {
          // Otherwise default to showing the close/dismiss button
          tabItem.tertiaryL10nId = "synced-tabs-context-close-tab-title";
          tabItem.tertiaryL10nArgs = JSON.stringify({ deviceName: name });
          tabItem.tertiaryActionClass = "dismiss-button";
          tabItem.closeRequested = false;
        }
        return tabItem;
      })
      .filter(
        item =>
          !this.isURLQueuedToClose(item.fxaDeviceId, item.url) ||
          item.url === this.lastClosedURL
      );
  }

  updateTabsList(syncedTabs) {
    if (!syncedTabs.length) {
      this.currentSyncedTabs = syncedTabs;
    }

    const tabsToRender = syncedTabs;

    // Return early if new tabs are the same as previous ones
    if (lazy.ObjectUtils.deepEqual(tabsToRender, this.currentSyncedTabs)) {
      return;
    }

    this.currentSyncedTabs = tabsToRender;
    this.host.requestUpdate();
  }

  async getSyncedTabData() {
    this.devices = await lazy.SyncedTabs.getTabClients();
    let tabs = await lazy.SyncedTabs.createRecentTabsList(this.devices, 5000, {
      removeAllDupes: false,
      removeDeviceDupes: true,
    });

    this.updateTabsList(tabs);
  }

  // Wrappers and helpful methods for SyncedTabManagement
  // so FxView and Sidebar don't need to import
  requestCloseRemoteTab(fxaDeviceId, url) {
    if (!this._pendingCloseTabs.has(fxaDeviceId)) {
      this._pendingCloseTabs.set(fxaDeviceId, new Set());
    }
    this._pendingCloseTabs.get(fxaDeviceId).add(url);
    this.lastClosedURL = url;
    lazy.SyncedTabsManagement.enqueueTabToClose(fxaDeviceId, url);
  }

  removePendingTabToClose(fxaDeviceId, url) {
    const urls = this._pendingCloseTabs.get(fxaDeviceId);
    if (urls) {
      urls.delete(url);
      if (!urls.size) {
        this._pendingCloseTabs.delete(fxaDeviceId);
      }
    }
    this.lastClosedURL = null;
    lazy.SyncedTabsManagement.removePendingTabToClose(fxaDeviceId, url);
  }

  isURLQueuedToClose(fxaDeviceId, url) {
    const urls = this._pendingCloseTabs.get(fxaDeviceId);
    return urls && urls.has(url);
  }
}
