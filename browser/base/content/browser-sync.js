/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const {
  FX_MONITOR_OAUTH_CLIENT_ID,
  FX_RELAY_OAUTH_CLIENT_ID,
  SCOPE_APP_SYNC,
  VPN_OAUTH_CLIENT_ID,
} = ChromeUtils.importESModule(
  "resource://gre/modules/FxAccountsCommon.sys.mjs"
);

const { UIState } = ChromeUtils.importESModule(
  "resource://services-sync/UIState.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
  EnsureFxAccountsWebChannel:
    "resource://gre/modules/FxAccountsWebChannel.sys.mjs",

  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  FxAccounts: "resource://gre/modules/FxAccounts.sys.mjs",
  MenuMessage: "resource:///modules/asrouter/MenuMessage.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  Referrals: "resource:///modules/referrals/Referrals.sys.mjs",
  SyncedTabs: "resource://services-sync/SyncedTabs.sys.mjs",
  SyncedTabsManagement: "resource://services-sync/SyncedTabs.sys.mjs",
  Weave: "resource://services-sync/main.sys.mjs",
});

var { DEVICE_TYPE_MOBILE, DEVICE_TYPE_TABLET } = ChromeUtils.importESModule(
  "resource://services-sync/constants.sys.mjs"
);

const MIN_STATUS_ANIMATION_DURATION = 1600;

// Campaign params shared by every product CTA in the Mozilla account toolbar
// panel. The per-variant utm_content is added by gSync._ctaURL.
const FXA_CTA_UTM_PARAMS = {
  utm_medium: "referral",
  utm_source: "firefox-desktop",
  utm_campaign: "toolbar",
};

// Locales are intentionally omitted: all three sites redirect to the visitor's
// locale, and hardcoding one would send non-English users to English pages.
const MONITOR_NEW_USER_URL = "https://monitor.mozilla.org/";
const MONITOR_EXISTING_USER_URL = "https://monitor.mozilla.org/user/dashboard/";
const RELAY_NEW_USER_URL = "https://relay.firefox.com/";
const RELAY_EXISTING_USER_URL = "https://relay.firefox.com/accounts/profile/";
const VPN_NEW_USER_URL = "https://www.mozilla.org/products/vpn/";
const VPN_EXISTING_USER_URL = "https://www.mozilla.org/products/vpn/download/";

this.SyncedTabsPanelList = class SyncedTabsPanelList {
  static sRemoteTabsDeckIndices = {
    DECKINDEX_TABS: 0,
    DECKINDEX_FETCHING: 1,
    DECKINDEX_TABSDISABLED: 2,
    DECKINDEX_NOCLIENTS: 3,
  };

  static sRemoteTabsPerPage = 25;
  static sRemoteTabsNextPageMinTabs = 5;

  constructor(panelview, deck, tabsList, separator) {
    this.QueryInterface = ChromeUtils.generateQI([
      "nsIObserver",
      "nsISupportsWeakReference",
    ]);

    Services.obs.addObserver(this, SyncedTabs.TOPIC_TABS_CHANGED, true);
    this.deck = deck;
    this.tabsList = tabsList;
    this.separator = separator;
    this._showSyncedTabsPromise = Promise.resolve();

    this.createSyncedTabs();
  }

  observe(subject, topic) {
    if (topic == SyncedTabs.TOPIC_TABS_CHANGED) {
      this._showSyncedTabs();
    }
  }

  createSyncedTabs() {
    if (SyncedTabs.isConfiguredToSyncTabs) {
      if (SyncedTabs.hasSyncedThisSession) {
        this.deck.selectedIndex =
          SyncedTabsPanelList.sRemoteTabsDeckIndices.DECKINDEX_TABS;
      } else {
        // Sync hasn't synced tabs yet, so show the "fetching" panel.
        this.deck.selectedIndex =
          SyncedTabsPanelList.sRemoteTabsDeckIndices.DECKINDEX_FETCHING;
      }
      // force a background sync.
      SyncedTabs.syncTabs().catch(ex => {
        console.error(ex);
      });
      this.deck.toggleAttribute("syncingtabs", true);
      // show the current list - it will be updated by our observer.
      this._showSyncedTabs();
      if (this.separator) {
        this.separator.hidden = false;
      }
    } else {
      // not configured to sync tabs, so no point updating the list.
      this.deck.selectedIndex =
        SyncedTabsPanelList.sRemoteTabsDeckIndices.DECKINDEX_TABSDISABLED;
      this.deck.toggleAttribute("syncingtabs", false);
      if (this.separator) {
        this.separator.hidden = true;
      }
    }
  }

  // Update the synced tab list after any existing in-flight updates are complete.
  _showSyncedTabs(paginationInfo) {
    this._showSyncedTabsPromise = this._showSyncedTabsPromise.then(
      () => {
        return this.__showSyncedTabs(paginationInfo);
      },
      e => {
        console.error(e);
      }
    );
  }

  // Return a new promise to update the tab list.
  __showSyncedTabs(paginationInfo) {
    if (!this.tabsList) {
      // Closed between the previous `this._showSyncedTabsPromise`
      // resolving and now.
      return undefined;
    }
    return SyncedTabs.getTabClients()
      .then(clients => {
        let noTabs = !UIState.get().syncEnabled || !clients.length;
        this.deck.toggleAttribute("syncingtabs", !noTabs);
        if (this.separator) {
          this.separator.hidden = noTabs;
        }

        // The view may have been hidden while the promise was resolving.
        if (!this.tabsList) {
          return;
        }
        if (clients.length === 0 && !SyncedTabs.hasSyncedThisSession) {
          // the "fetching tabs" deck is being shown - let's leave it there.
          // When that first sync completes we'll be notified and update.
          return;
        }

        if (clients.length === 0) {
          this.deck.selectedIndex =
            SyncedTabsPanelList.sRemoteTabsDeckIndices.DECKINDEX_NOCLIENTS;
          return;
        }
        this.deck.selectedIndex =
          SyncedTabsPanelList.sRemoteTabsDeckIndices.DECKINDEX_TABS;
        this._clearSyncedTabList();
        SyncedTabs.sortTabClientsByLastUsed(clients);
        let fragment = document.createDocumentFragment();

        let clientNumber = 0;
        for (let client of clients) {
          // add a menu separator for all clients other than the first.
          if (fragment.lastElementChild) {
            let separator = document.createXULElement("toolbarseparator");
            fragment.appendChild(separator);
          }
          // We add the client's elements to a container, and indicate which
          // element labels it.
          let labelId = `synced-tabs-client-${clientNumber++}`;
          let container = document.createXULElement("vbox");
          container.classList.add("PanelUI-remotetabs-clientcontainer");
          container.setAttribute("role", "group");
          container.setAttribute("aria-labelledby", labelId);
          let clientPaginationInfo =
            paginationInfo && paginationInfo.clientId == client.id
              ? paginationInfo
              : { clientId: client.id };
          this._appendSyncClient(
            client,
            container,
            labelId,
            clientPaginationInfo
          );
          fragment.appendChild(container);
        }
        this.tabsList.appendChild(fragment);
      })
      .catch(err => {
        console.error(err);
      })
      .then(() => {
        // an observer for tests.
        Services.obs.notifyObservers(
          null,
          "synced-tabs-menu:test:tabs-updated"
        );
      });
  }

  _clearSyncedTabList() {
    let list = this.tabsList;
    while (list.lastChild) {
      list.lastChild.remove();
    }
  }

  _createNoSyncedTabsElement(messageAttr, appendTo = null) {
    if (!appendTo) {
      appendTo = this.tabsList;
    }

    let messageLabel = document.createXULElement("label");
    document.l10n.setAttributes(
      messageLabel,
      this.tabsList.getAttribute(messageAttr)
    );
    appendTo.appendChild(messageLabel);
    return messageLabel;
  }

  _appendSyncClient(client, container, labelId, paginationInfo) {
    let { maxTabs = SyncedTabsPanelList.sRemoteTabsPerPage } = paginationInfo;
    // Create the element for the remote client.
    let clientItem = document.createXULElement("label");
    clientItem.setAttribute("id", labelId);
    clientItem.className = "subview-subheader";
    clientItem.setAttribute("itemtype", "client");
    clientItem.setAttribute(
      "tooltiptext",
      gSync.fluentStrings.formatValueSync("appmenu-fxa-last-sync", {
        time: gSync.formatLastSyncDate(new Date(client.lastModified)),
      })
    );
    clientItem.textContent = client.name;

    container.appendChild(clientItem);

    if (!client.tabs.length) {
      let label = this._createNoSyncedTabsElement(
        "notabsforclientlabel",
        container
      );
      label.setAttribute("class", "PanelUI-remotetabs-notabsforclient-label");
    } else {
      // We have the client obj but we need the FxA device obj so we use the clients
      // engine to get us the FxA device
      let device =
        fxAccounts.device.recentDeviceList &&
        fxAccounts.device.recentDeviceList.find(
          d =>
            d.id === Weave.Service.clientsEngine.getClientFxaDeviceId(client.id)
        );
      let remoteTabCloseAvailable =
        device && fxAccounts.commands.closeTab.isDeviceCompatible(device);

      let tabs = client.tabs.filter(t => !t.inactive);
      let hasInactive = tabs.length != client.tabs.length;

      if (hasInactive) {
        container.append(this._createShowInactiveTabsElement(client, device));
      }
      // If this page isn't displaying all (regular, active) tabs, show a "Show More" button.
      let hasNextPage = tabs.length > maxTabs;
      let nextPageIsLastPage =
        hasNextPage &&
        maxTabs + SyncedTabsPanelList.sRemoteTabsPerPage >= tabs.length;
      if (nextPageIsLastPage) {
        // When the user clicks "Show More", try to have at least sRemoteTabsNextPageMinTabs more tabs
        // to display in order to avoid user frustration
        maxTabs = Math.min(
          tabs.length - SyncedTabsPanelList.sRemoteTabsNextPageMinTabs,
          maxTabs
        );
      }
      if (hasNextPage) {
        tabs = tabs.slice(0, maxTabs);
      }
      for (let [index, tab] of tabs.entries()) {
        let tabEnt = this._createSyncedTabElement(
          tab,
          index,
          device,
          remoteTabCloseAvailable
        );
        container.appendChild(tabEnt);
      }
      if (hasNextPage) {
        let showAllEnt = this._createShowMoreSyncedTabsElement(paginationInfo);
        container.appendChild(showAllEnt);
      }
    }
  }

  _createShowMoreSyncedTabsElement(paginationInfo) {
    let showMoreItem = document.createXULElement("toolbarbutton");
    showMoreItem.setAttribute("itemtype", "showmorebutton");
    showMoreItem.setAttribute("closemenu", "none");
    showMoreItem.classList.add("subviewbutton", "subviewbutton-nav-down");
    document.l10n.setAttributes(showMoreItem, "appmenu-remote-tabs-showmore");

    paginationInfo.maxTabs = Infinity;
    showMoreItem.addEventListener("click", e => {
      e.preventDefault();
      e.stopPropagation();
      this._showSyncedTabs(paginationInfo);
    });
    return showMoreItem;
  }

  _createShowInactiveTabsElement(client, device) {
    let showItem = document.createXULElement("toolbarbutton");
    showItem.setAttribute("itemtype", "showinactivebutton");
    showItem.setAttribute("closemenu", "none");
    showItem.classList.add("subviewbutton", "subviewbutton-nav");
    document.l10n.setAttributes(
      showItem,
      "appmenu-remote-tabs-show-inactive-tabs"
    );

    let canClose =
      device && fxAccounts.commands.closeTab.isDeviceCompatible(device);

    showItem.addEventListener("click", e => {
      let node = PanelMultiView.getViewNode(
        document,
        "PanelUI-fxa-menu-inactive-tabs"
      );

      // device name.
      let label = node.querySelector("label[itemtype='client']");
      label.textContent = client.name;

      // Update the tab list.
      let container = node.querySelector(".panel-subview-body");
      container.replaceChildren(
        ...client.tabs
          .filter(t => t.inactive)
          .map((tab, index) =>
            this._createSyncedTabElement(tab, index, device, canClose)
          )
      );
      PanelUI.showSubView("PanelUI-fxa-menu-inactive-tabs", showItem, e);
    });
    return showItem;
  }

  _createSyncedTabElement(tabInfo, index, device, canCloseTabs) {
    let tabContainer = document.createXULElement("toolbaritem");
    tabContainer.setAttribute(
      "class",
      "PanelUI-tabitem-container all-tabs-item"
    );

    let item = document.createXULElement("toolbarbutton");
    let tooltipText = (tabInfo.title ? tabInfo.title + "\n" : "") + tabInfo.url;
    item.setAttribute("itemtype", "tab");
    item.setAttribute("flex", "1");
    item.classList.add(
      "all-tabs-button",
      "subviewbutton",
      "subviewbutton-iconic"
    );
    item.setAttribute("targetURI", tabInfo.url);
    item.setAttribute(
      "label",
      tabInfo.title != "" ? tabInfo.title : tabInfo.url
    );
    if (tabInfo.icon) {
      item.setAttribute("image", tabInfo.icon);
    }
    item.setAttribute("tooltiptext", tooltipText);
    // We need to use "click" instead of "command" here so openUILink
    // respects different buttons (eg, to open in a new tab).
    item.addEventListener("click", e => {
      // We want to differentiate between when the fxa panel is within the app menu/hamburger bar
      let object = window.gSync._getEntryPointForElement(e.currentTarget);
      SyncedTabs.recordSyncedTabsTelemetry(object, "click", {
        tab_pos: index.toString(),
      });
      document.defaultView.openUILink(tabInfo.url, e, {
        triggeringPrincipal: Services.scriptSecurityManager.createNullPrincipal(
          {}
        ),
      });
      if (BrowserUtils.whereToOpenLink(e) != "current") {
        e.preventDefault();
        e.stopPropagation();
      } else {
        CustomizableUI.hidePanelForNode(item);
      }
    });
    tabContainer.appendChild(item);
    // We should only add an X button next to tabs if the device
    // is broadcasting that it can remotely close tabs
    if (canCloseTabs) {
      let closeBtn = this._createCloseTabElement(tabInfo.url, device);
      closeBtn.tab = item;
      tabContainer.appendChild(closeBtn);
      let undoBtn = this._createUndoCloseTabElement(tabInfo.url, device);
      undoBtn.tab = item;
      tabContainer.appendChild(undoBtn);
    }
    return tabContainer;
  }

  _createCloseTabElement(url, device) {
    let closeBtn = document.createXULElement("toolbarbutton");
    closeBtn.classList.add(
      "remote-tabs-close-button",
      "all-tabs-close-button",
      "subviewbutton"
    );
    closeBtn.setAttribute("closemenu", "none");
    closeBtn.setAttribute(
      "tooltiptext",
      gSync.fluentStrings.formatValueSync("synced-tabs-context-close-tab", {
        deviceName: device.name,
      })
    );
    closeBtn.addEventListener("click", e => {
      e.stopPropagation();

      let tabContainer = closeBtn.parentNode;
      let tabList = tabContainer.parentNode;

      let undoBtn = tabContainer.querySelector(".remote-tabs-undo-button");

      let prevClose = tabList.querySelector(
        ".remote-tabs-undo-button:not([hidden])"
      );
      if (prevClose) {
        let prevCloseContainer = prevClose.parentNode;
        prevCloseContainer.classList.add("tabitem-removed");
        prevCloseContainer.addEventListener("transitionend", () => {
          prevCloseContainer.remove();
        });
      }
      closeBtn.hidden = true;
      undoBtn.hidden = false;
      // This tab has been closed so we prevent the user from
      // interacting with it
      if (closeBtn.tab) {
        closeBtn.tab.disabled = true;
      }
      // The user could be hitting multiple tabs across multiple devices, with a few
      // seconds in-between -- we should not immediately fire off pushes, so we
      // add it to a queue and send in bulk at a later time
      SyncedTabsManagement.enqueueTabToClose(device.id, url);
    });
    return closeBtn;
  }

  _createUndoCloseTabElement(url, device) {
    let undoBtn = document.createXULElement("toolbarbutton");
    undoBtn.classList.add("remote-tabs-undo-button", "subviewbutton");
    undoBtn.setAttribute("closemenu", "none");
    undoBtn.setAttribute("data-l10n-id", "text-action-undo");
    undoBtn.hidden = true;

    undoBtn.addEventListener("click", function (e) {
      e.stopPropagation();

      undoBtn.hidden = true;
      let closeBtn = undoBtn.parentNode.querySelector(".all-tabs-close-button");
      closeBtn.hidden = false;
      if (undoBtn.tab) {
        undoBtn.tab.disabled = false;
      }

      // remove this tab from being remotely closed
      SyncedTabsManagement.removePendingTabToClose(device.id, url);
    });
    return undoBtn;
  }

  destroy() {
    Services.obs.removeObserver(this, SyncedTabs.TOPIC_TABS_CHANGED);
    this.tabsList = null;
    this.deck = null;
    this.separator = null;
  }
};

this.FxAMenuDeviceList = class FxAMenuDeviceList {
  static MAX_RECENT_TABS = 5;
  static MAX_DEVICES = 3;

  constructor(devicesList) {
    this.QueryInterface = ChromeUtils.generateQI([
      "nsIObserver",
      "nsISupportsWeakReference",
    ]);

    Services.obs.addObserver(this, SyncedTabs.TOPIC_TABS_CHANGED, true);
    this.devicesList = devicesList;
    this._updateDevicesPromise = Promise.resolve();

    this._initDeviceList();

    // Refresh the FxA devices list.  This won't affect the current menu items,
    // but it helps ensure the menu is up-to-date the next time the menu loads.
    // This can help the user get unstuck when the device list is stale (#1664954)
    gSync.refreshFxaDevices();
  }

  observe(subject, topic) {
    if (topic == SyncedTabs.TOPIC_TABS_CHANGED) {
      this._updateDeviceList();
    }
  }

  _initDeviceList() {
    if (SyncedTabs.isConfiguredToSyncTabs) {
      SyncedTabs.syncTabs().catch(ex => {
        console.error(ex);
      });
    }
    this._updateDeviceList();
  }

  _updateDeviceList() {
    this._updateDevicesPromise = this._updateDevicesPromise.then(
      () => this._doUpdateDeviceList(),
      e => console.error(e)
    );
  }

  async _doUpdateDeviceList() {
    if (!this.devicesList) {
      return;
    }

    let clients;
    try {
      clients = await this._getMergedDeviceList();
    } catch (err) {
      console.error(err);
      return;
    }

    if (!this.devicesList) {
      return;
    }

    if (!UIState.get().syncEnabled || !clients.length) {
      this.devicesList.hidden = true;
      Services.obs.notifyObservers(null, "synced-tabs-menu:test:tabs-updated");
      return;
    }

    SyncedTabs.sortTabClientsByLastUsed(clients);

    while (this.devicesList.lastChild) {
      this.devicesList.lastChild.remove();
    }

    // The FxA panel is shared between the account (toolbar) menu and the app
    // (hamburger) menu. In the app menu each device is shown as its own inline
    // section; in the account menu each device is a button that opens the
    // per-device recent tabs subpanel.
    let inAppMenu = document
      .getElementById("appMenu-popup")
      ?.contains(this.devicesList);

    if (inAppMenu) {
      for (let client of clients) {
        let device = this._getDeviceForClient(client);
        // A separator precedes each section, including the first, so the list
        // of devices is separated from the content above it.
        this.devicesList.appendChild(
          document.createXULElement("toolbarseparator")
        );
        this._appendDeviceSection(client, device);
      }
    } else {
      // Only the first few devices are shown inline; the rest are reachable
      // through the "All Devices" button.
      for (let client of clients.slice(0, FxAMenuDeviceList.MAX_DEVICES)) {
        let device = this._getDeviceForClient(client);
        this.devicesList.appendChild(this._createDeviceEntry(client, device));
      }
      if (clients.length > FxAMenuDeviceList.MAX_DEVICES) {
        this.devicesList.appendChild(this._createAllDevicesButton(clients));
      }
    }

    this.devicesList.hidden = false;

    Services.obs.notifyObservers(null, "synced-tabs-menu:test:tabs-updated");
  }

  _getDeviceForClient(client) {
    let devices = fxAccounts.device.recentDeviceList;
    if (!devices) {
      return undefined;
    }
    // Real Sync clients are keyed by a Sync client id that maps to an FxA
    // device id; the synthesized stand-ins built by _getMergedDeviceList are
    // already keyed by the FxA device id, in which case getClientFxaDeviceId
    // returns nothing and we fall back to the client id itself.
    let fxaDeviceId =
      Weave.Service.clientsEngine.getClientFxaDeviceId(client.id) || client.id;
    return devices.find(d => d.id === fxaDeviceId);
  }

  /**
   * Builds the list of devices to display. It is based on the full FxA account
   * device list (`fxAccounts.device.recentDeviceList`). A device is shown when
   * it either has a Sync tab-client record (so it can show recent tabs) or is
   * compatible with Send Tab (so the user can still send a tab to it); devices
   * with neither are omitted, as there is nothing to show or do for them.
   * Devices that have a Sync tab-client record reuse it (and therefore keep
   * their recent tabs); Send Tab-only devices get a lightweight stand-in with
   * an empty tab list. Any Sync client that doesn't map to a current account
   * device is still appended so the list never shows fewer devices than the
   * tabs engine knows about (e.g. when the cached device list is stale or not
   * yet fetched).
   */
  async _getMergedDeviceList() {
    let clients = await SyncedTabs.getTabClients();
    let devices = fxAccounts.device.recentDeviceList;
    if (!devices) {
      return clients;
    }

    let clientByFxaId = new Map();
    for (let client of clients) {
      let fxaDeviceId = Weave.Service.clientsEngine.getClientFxaDeviceId(
        client.id
      );
      if (fxaDeviceId) {
        clientByFxaId.set(fxaDeviceId, client);
      }
    }

    let merged = [];
    let matchedClients = new Set();
    for (let device of devices) {
      if (device.isCurrentDevice) {
        continue;
      }
      let client = clientByFxaId.get(device.id);
      if (client) {
        matchedClients.add(client);
        merged.push(client);
      } else if (fxAccounts.commands.sendTab.isDeviceCompatible(device)) {
        merged.push({
          id: device.id,
          name: device.name,
          lastModified: device.lastAccessTime,
          tabs: [],
        });
      }
    }
    for (let client of clients) {
      if (!matchedClients.has(client)) {
        merged.push(client);
      }
    }
    return merged;
  }

  _createAllDevicesButton(clients) {
    let btn = document.createXULElement("toolbarbutton");
    btn.id = "PanelUI-fxa-menu-all-devices-button";
    btn.classList.add("subviewbutton", "subviewbutton-nav");
    btn.setAttribute("closemenu", "none");
    btn.setAttribute("data-l10n-id", "fxa-menu-all-devices");
    btn.addEventListener("click", e => {
      this._showAllDevices(clients, btn, e);
    });
    return btn;
  }

  _showAllDevices(clients, anchor, event) {
    let list = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-all-devices-list"
    );
    list.replaceChildren();
    for (let client of clients) {
      let device = this._getDeviceForClient(client);
      list.appendChild(this._createDeviceEntry(client, device));
    }
    PanelUI.showSubView("PanelUI-fxa-menu-all-devices", anchor, event);
  }

  _getDeviceClientType(device) {
    if (!device) {
      return "desktop";
    }
    if (device.type === DEVICE_TYPE_MOBILE) {
      return "phone";
    }
    if (device.type === DEVICE_TYPE_TABLET) {
      return "tablet";
    }
    return "desktop";
  }

  _createDeviceEntry(client, device) {
    let btn = document.createXULElement("toolbarbutton");
    btn.classList.add(
      "subviewbutton",
      "subviewbutton-iconic",
      "subviewbutton-nav",
      "PanelUI-fxa-menu-device-entry"
    );
    btn.setAttribute("label", client.name);
    btn.setAttribute("clientType", this._getDeviceClientType(device));
    btn.setAttribute("closemenu", "none");
    btn.setAttribute(
      "tooltiptext",
      gSync.fluentStrings.formatValueSync("appmenu-fxa-last-sync", {
        time: gSync.formatLastSyncDate(new Date(client.lastModified)),
      })
    );
    btn.addEventListener("click", e => {
      // Re-resolve the device at click time: the FxA device list is refreshed
      // asynchronously, so it may not have been populated yet when this entry
      // was created (e.g. on the first menu open after signing in). Resolving
      // it now ensures the "Send Current Page to This Device" button is shown
      // when the device is sendTab-capable, instead of only on a later open.
      this._showDeviceRecentTabs(
        client,
        this._getDeviceForClient(client) ?? device,
        btn,
        e
      );
    });
    return btn;
  }

  _getRecentTabs(client) {
    return client.tabs
      .filter(t => !t.inactive)
      .slice(0, FxAMenuDeviceList.MAX_RECENT_TABS);
  }

  /**
   * Appends the given tabs (with their close/undo controls when the device
   * supports closing tabs) to a tabs list container.
   */
  _populateRecentTabs(tabsList, recentTabs, device) {
    let canCloseTabs =
      device && fxAccounts.commands.closeTab.isDeviceCompatible(device);

    for (let [index, tab] of recentTabs.entries()) {
      let tabContainer = this._createSyncedTabElement(tab, index, device);
      tabsList.appendChild(tabContainer);
      let item = tabContainer.querySelector(".all-tabs-button");
      // Force render() now so that toolbarbutton-icon and toolbarbutton-text are
      // created even when the panelview is still in the template DocumentFragment
      // and connectedCallback has not yet fired against the live document.
      item.render();
      if (canCloseTabs) {
        let closeBtn = this._createCloseTabElement(tab.url, device);
        closeBtn.tab = item;
        let undoBtn = this._createUndoCloseTabElement(tab.url, device);
        undoBtn.tab = item;
        tabContainer.append(closeBtn, undoBtn);
      }
    }
  }

  _configureViewAllTabsButton(viewAllBtn, client) {
    let [viewAllMessage] = gSync.fluentStrings.formatMessagesSync([
      {
        id: "fxa-menu-device-view-all-synced-tabs",
        args: { tabCount: client.tabs.length },
      },
    ]);
    viewAllBtn.setAttribute(
      "label",
      viewAllMessage.attributes?.find(attr => attr.name === "label")?.value
    );
    viewAllBtn.onclick = () => {
      CustomizableUI.hidePanelForNode(viewAllBtn);
      SidebarController.show("viewTabsSidebar");
    };
  }

  _canSendTabToDevice(device) {
    return (
      device &&
      fxAccounts.commands.sendTab.isDeviceCompatible(device) &&
      !!BrowserUtils.getShareableURL(gBrowser.selectedBrowser.currentURI)
    );
  }

  /**
   * Wires up a "Send Current Page to This Device" button: records the exposure
   * telemetry and sends the current (or selected) tabs on click. `anchor` is
   * used to resolve the telemetry entry point, so it must be an element whose
   * position identifies the menu (app menu vs account menu).
   */
  _configureSendPageButton(sendPageBtn, device, anchor) {
    const targets = gSync.getSendTabTargets();
    gSync.emitFxaToolbarTelemetry("send_tab_opened", anchor, {
      device_count: String(targets.length),
    });
    gSync.emitFxaToolbarTelemetry("send_tab_exposed", anchor, {
      device_count: String(targets.length),
    });
    sendPageBtn.onclick = () => {
      gSync.emitFxaToolbarTelemetry("send_tab", anchor, {
        device_count: String(targets.length),
        action: "device",
      });
      CustomizableUI.hidePanelForNode(sendPageBtn);
      let isPrivate = PrivateBrowsingUtils.isBrowserPrivate(gBrowser);
      let tabsToSend = gBrowser.selectedTab.multiselected
        ? gBrowser.selectedTabs.map(t => ({
            url: t.linkedBrowser.currentURI.spec,
            title: t.linkedBrowser.contentTitle,
            private: isPrivate,
          }))
        : [
            {
              url: BrowserUtils.getShareableURL(
                gBrowser.selectedBrowser.currentURI
              ).spec,
              title: gBrowser.selectedBrowser.contentTitle,
              private: isPrivate,
            },
          ];
      gSync.sendTabsAndConfirm(tabsToSend, [device]);
    };
  }

  _showDeviceRecentTabs(client, device, anchor, event) {
    let panelNode = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-device-recent-tabs"
    );

    let tabsList = panelNode.querySelector(
      "#PanelUI-fxa-device-recent-tabs-list"
    );
    tabsList.replaceChildren();

    let recentTabs = this._getRecentTabs(client);
    this._populateRecentTabs(tabsList, recentTabs, device);

    let hasTabs = !!recentTabs.length;
    let noTabsLabel = panelNode.querySelector(
      "#PanelUI-fxa-device-no-open-tabs"
    );
    let footerSeparator = panelNode.querySelector(
      "#PanelUI-fxa-device-recent-tabs-footer-separator"
    );
    let viewAllBtn = panelNode.querySelector(
      "#PanelUI-fxa-device-view-all-tabs"
    );

    let sendPageBtn = panelNode.querySelector(
      "#PanelUI-fxa-device-send-current-page"
    );
    let canSendTab = this._canSendTabToDevice(device);

    tabsList.hidden = !hasTabs;
    noTabsLabel.hidden = hasTabs;
    viewAllBtn.hidden = !hasTabs;
    this._configureViewAllTabsButton(viewAllBtn, client);
    // The separator sits above the footer buttons, so keep it whenever the
    // footer has a button below it - either the "view all" button (when there
    // are tabs) or the "send current page" button (when we can send a tab).
    footerSeparator.hidden = !hasTabs && !canSendTab;

    sendPageBtn.hidden = !canSendTab;
    if (canSendTab) {
      this._configureSendPageButton(sendPageBtn, device, anchor);
    }

    PanelUI.showSubView("PanelUI-fxa-device-recent-tabs", anchor, event);
  }

  /**
   * Appends a device's section to the FxA menu devices list: a header with the
   * device name, its most recent tabs (or a "No open tabs" label), a "view all
   * synced tabs" button and a "Send Current Page to This Device" button.
   */
  _appendDeviceSection(client, device) {
    let list = this.devicesList;

    let header = document.createXULElement("label");
    header.classList.add("subview-subheader", "PanelUI-fxa-menu-device-header");
    header.setAttribute("itemtype", "client");
    header.setAttribute(
      "tooltiptext",
      gSync.fluentStrings.formatValueSync("appmenu-fxa-last-sync", {
        time: gSync.formatLastSyncDate(new Date(client.lastModified)),
      })
    );
    header.textContent = client.name;
    list.appendChild(header);

    let recentTabs = this._getRecentTabs(client);
    if (recentTabs.length) {
      let tabsList = document.createXULElement("vbox");
      tabsList.classList.add("PanelUI-fxa-menu-device-tabs-list");
      this._populateRecentTabs(tabsList, recentTabs, device);
      list.appendChild(tabsList);

      let viewAllBtn = document.createXULElement("toolbarbutton");
      viewAllBtn.classList.add("subviewbutton");
      viewAllBtn.setAttribute("closemenu", "none");
      this._configureViewAllTabsButton(viewAllBtn, client);
      list.appendChild(viewAllBtn);
    } else {
      let noTabsLabel = document.createXULElement("label");
      noTabsLabel.classList.add("PanelUI-remotetabs-notabsforclient-label");
      noTabsLabel.setAttribute(
        "value",
        gSync.fluentStrings.formatValueSync("appmenu-remote-tabs-notabs")
      );
      list.appendChild(noTabsLabel);
    }

    if (this._canSendTabToDevice(device)) {
      let sendPageBtn = document.createXULElement("toolbarbutton");
      sendPageBtn.classList.add("subviewbutton");
      sendPageBtn.setAttribute(
        "data-l10n-id",
        "fxa-menu-device-send-current-page"
      );
      list.appendChild(sendPageBtn);
      // The button lives inside the app-menu devices list, so it identifies the
      // menu itself as the telemetry entry point.
      this._configureSendPageButton(sendPageBtn, device, sendPageBtn);
    }
  }

  /**
   * Creates a toolbarbutton used in the per-device recent tabs list (the tab
   * entry itself and its close/undo controls all share the same shape).
   *
   * @param {string[]} classes - classes to add to the button.
   * @param {function} onClick - the click event handler.
   * @param {object} [options]
   * @param {boolean} [options.closemenu] - when true, sets closemenu="none" so
   *   clicking the button doesn't close the panel.
   * @returns {Element} the created toolbarbutton.
   */
  _createTabToolbarButton(classes, onClick, { closemenu = false } = {}) {
    let btn = document.createXULElement("toolbarbutton");
    btn.classList.add(...classes);
    if (closemenu) {
      btn.setAttribute("closemenu", "none");
    }
    btn.addEventListener("click", onClick);
    return btn;
  }

  _createSyncedTabElement(tabInfo, index, _device) {
    let tooltipText = (tabInfo.title ? tabInfo.title + "\n" : "") + tabInfo.url;
    let tabContainer = document.createXULElement("toolbaritem");
    tabContainer.setAttribute(
      "class",
      "PanelUI-tabitem-container all-tabs-item"
    );

    let item = this._createTabToolbarButton(
      ["all-tabs-button", "subviewbutton", "subviewbutton-iconic"],
      e => {
        let object = window.gSync._getEntryPointForElement(e.currentTarget);
        SyncedTabs.recordSyncedTabsTelemetry(object, "click", {
          tab_pos: index.toString(),
        });
        document.defaultView.openUILink(tabInfo.url, e, {
          triggeringPrincipal:
            Services.scriptSecurityManager.createNullPrincipal({}),
        });
        if (BrowserUtils.whereToOpenLink(e) != "current") {
          e.preventDefault();
          e.stopPropagation();
        } else {
          CustomizableUI.hidePanelForNode(item);
        }
      }
    );
    item.setAttribute("itemtype", "tab");
    item.setAttribute("flex", "1");
    item.setAttribute("targetURI", tabInfo.url);
    item.setAttribute(
      "label",
      tabInfo.title != "" ? tabInfo.title : tabInfo.url
    );
    if (tabInfo.icon) {
      item.setAttribute("image", tabInfo.icon);
    }
    item.setAttribute("tooltiptext", tooltipText);
    tabContainer.appendChild(item);
    return tabContainer;
  }

  _createCloseTabElement(url, device) {
    let closeBtn = this._createTabToolbarButton(
      ["remote-tabs-close-button", "all-tabs-close-button", "subviewbutton"],
      e => {
        e.stopPropagation();

        let tabContainer = closeBtn.parentNode;
        let tabList = tabContainer.parentNode;

        let undoBtn = tabContainer.querySelector(".remote-tabs-undo-button");

        let prevClose = tabList.querySelector(
          ".remote-tabs-undo-button:not([hidden])"
        );
        if (prevClose) {
          let prevContainer = prevClose.parentNode;
          prevContainer.classList.add("tabitem-removed");
          prevContainer.addEventListener("transitionend", () => {
            prevContainer.remove();
          });
        }
        closeBtn.hidden = true;
        undoBtn.hidden = false;
        // The Undo button is a sibling of the tab button, so disabling the tab
        // button no longer prunes Undo from the keyboard walker or the
        // accessibility tree.
        if (closeBtn.tab) {
          closeBtn.tab.disabled = true;
        }
        SyncedTabsManagement.enqueueTabToClose(device.id, url);
      },
      { closemenu: true }
    );
    closeBtn.setAttribute(
      "tooltiptext",
      gSync.fluentStrings.formatValueSync("synced-tabs-context-close-tab", {
        deviceName: device.name,
      })
    );
    return closeBtn;
  }

  _createUndoCloseTabElement(url, device) {
    let undoBtn = this._createTabToolbarButton(
      ["remote-tabs-undo-button", "subviewbutton"],
      e => {
        e.stopPropagation();

        undoBtn.hidden = true;
        let closeBtn = undoBtn.parentNode.querySelector(
          ".all-tabs-close-button"
        );
        closeBtn.hidden = false;
        if (undoBtn.tab) {
          undoBtn.tab.disabled = false;
        }

        SyncedTabsManagement.removePendingTabToClose(device.id, url);
      },
      { closemenu: true }
    );
    undoBtn.setAttribute("data-l10n-id", "text-action-undo");
    undoBtn.hidden = true;
    return undoBtn;
  }

  destroy() {
    Services.obs.removeObserver(this, SyncedTabs.TOPIC_TABS_CHANGED);
    this.devicesList = null;
  }
};

var gSync = {
  _initialized: false,
  _isCurrentlySyncing: false,
  // The last sync start time. Used to calculate the leftover animation time
  // once syncing completes (bug 1239042).
  _syncStartTime: 0,
  _syncAnimationTimer: 0,
  _obs: ["weave:engine:sync:finish", "quit-application", UIState.ON_UPDATE],
  // Track whether send tab exposure events have been recorded for current context menu session
  _sendTabExposureRecorded: new Set(),

  get log() {
    if (!this._log) {
      const { Log } = ChromeUtils.importESModule(
        "resource://gre/modules/Log.sys.mjs"
      );
      let syncLog = Log.repository.getLogger("Sync.Browser");
      syncLog.manageLevelFromPref("services.sync.log.logger.browser");
      this._log = syncLog;
    }
    return this._log;
  },

  get fluentStrings() {
    delete this.fluentStrings;
    return (this.fluentStrings = new Localization(
      [
        "branding/brand.ftl",
        "browser/accounts.ftl",
        "browser/appmenu.ftl",
        "browser/browserContext.ftl",
        "browser/sync.ftl",
        "browser/syncedTabs.ftl",
        "browser/newtab/asrouter.ftl",
        "browser/aiWindow.ftl",
      ],
      true
    ));
  },

  // Returns true if FxA is configured, but the send tab targets list isn't
  // ready yet.
  get sendTabConfiguredAndLoading() {
    const state = UIState.get();
    return (
      state.status == UIState.STATUS_SIGNED_IN &&
      state.syncEnabled &&
      !fxAccounts.device.recentDeviceList
    );
  },

  get isSignedIn() {
    return UIState.get().status == UIState.STATUS_SIGNED_IN;
  },

  get isUnverified() {
    return UIState.get().status == UIState.STATUS_NOT_VERIFIED;
  },

  get isSignedInWithSyncDisabled() {
    const state = UIState.get();
    return state.status == UIState.STATUS_SIGNED_IN && !state.syncEnabled;
  },

  get hasNoSendTabTargets() {
    return this.getSendTabTargets().length === 0;
  },

  // Returns the call to action ("signin", "turnonsync", or "connectdevice") for
  // showing the remote tabs promo, or null when the promo should be hidden.
  // Disabled `requiredEngines` also select "turnonsync" when provided.
  getSyncPromoState(requiredEngines = []) {
    if (!this.FXA_ENABLED) {
      return null;
    }
    const state = UIState.get();
    switch (state.status) {
      case UIState.STATUS_NOT_CONFIGURED:
      case UIState.STATUS_NOT_VERIFIED:
        return "signin";
      case UIState.STATUS_SIGNED_IN: {
        const engineDisabled = requiredEngines.some(
          engine =>
            !Services.prefs.getBoolPref(`services.sync.engine.${engine}`, true)
        );
        if (!state.syncEnabled || engineDisabled) {
          return "turnonsync";
        }
        // A null list means it's still loading, so defer to the existing
        // synced-tabs menuitem rather than promoting "connect a device". The
        // current device isn't always present (e.g. just after signing in
        // again), so look for any device other than this one.
        const devices = fxAccounts.device.recentDeviceList;
        const hasOtherDevice = devices?.some(d => !d.isCurrentDevice);
        if (devices && !hasOtherDevice) {
          return "connectdevice";
        }
        return null;
      }
      default:
        return null;
    }
  },

  // Performs the state-specific action for a sync promo (see getSyncPromoState).
  // `action` is one of the promo states, `entryPoint` identifies the surface for
  // FxA telemetry/URL building.
  handleSyncPromoAction(action, entryPoint) {
    switch (action) {
      case "signin":
        this.openFxAEmailFirstPage(entryPoint);
        break;
      case "turnonsync":
        this.openSyncSetupForEntryPoint(entryPoint);
        break;
      case "connectdevice":
        this.openConnectAnotherDevice(entryPoint);
        break;
    }
  },

  shouldHideSendContextMenuItems(enabled) {
    return !enabled || !this.FXA_ENABLED;
  },

  getSendTabTargets() {
    const targets = [];
    const state = UIState.get();
    if (
      state.status != UIState.STATUS_SIGNED_IN ||
      !state.syncEnabled ||
      !fxAccounts.device.recentDeviceList
    ) {
      return targets;
    }
    for (let d of fxAccounts.device.recentDeviceList) {
      if (d.isCurrentDevice) {
        continue;
      }

      if (fxAccounts.commands.sendTab.isDeviceCompatible(d)) {
        targets.push(d);
      }
    }
    return targets.sort((a, b) => b.lastAccessTime - a.lastAccessTime);
  },

  hasOnlyMobileSendTabTargets(targets = this.getSendTabTargets()) {
    return (
      !targets.length ||
      targets.every(
        target =>
          target.type == DEVICE_TYPE_MOBILE || target.type == DEVICE_TYPE_TABLET
      )
    );
  },

  _definePrefGetters() {
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "FXA_ENABLED",
      "identity.fxaccounts.enabled"
    );
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "FXA_CTA_MENU_ENABLED",
      "identity.fxaccounts.toolbar.pxiToolbarEnabled"
    );
  },

  maybeUpdateUIState() {
    // Update the UI.
    if (UIState.isReady()) {
      const state = UIState.get();
      // If we are not configured, the UI is already in the right state when
      // we open the window. We can avoid a repaint.
      if (state.status != UIState.STATUS_NOT_CONFIGURED) {
        this.updateAllUI(state);
      }
    }
  },

  init() {
    if (this._initialized) {
      return;
    }

    this._definePrefGetters();

    if (!this.FXA_ENABLED) {
      this.onFxaDisabled();
      return;
    }

    MozXULElement.insertFTLIfNeeded("browser/sync.ftl");
    MozXULElement.insertFTLIfNeeded("browser/newtab/asrouter.ftl");

    // Label for the sync buttons.
    const appMenuLabel = PanelMultiView.getViewNode(
      document,
      "appMenu-fxa-label2"
    );
    if (!appMenuLabel) {
      // We are in a window without our elements - just abort now, without
      // setting this._initialized, so we don't attempt to remove observers.
      return;
    }
    // We start with every menuitem hidden (except for the "setup sync" state),
    // so that we don't need to init the sync UI on windows like pageInfo.xhtml
    // (see bug 1384856).
    // maybeUpdateUIState() also optimizes for this - if we should be in the
    // "setup sync" state, that function assumes we are already in it and
    // doesn't re-initialize the UI elements.
    document.getElementById("sync-setup").hidden = false;
    PanelMultiView.getViewNode(
      document,
      "PanelUI-remotetabs-setupsync"
    ).hidden = false;

    const appMenuHeaderTitle = PanelMultiView.getViewNode(
      document,
      "appMenu-header-title"
    );
    const appMenuHeaderDescription = PanelMultiView.getViewNode(
      document,
      "appMenu-header-description"
    );
    const appMenuHeaderText = PanelMultiView.getViewNode(
      document,
      "appMenu-fxa-text"
    );
    appMenuHeaderTitle.hidden = true;
    // We must initialize the label attribute here instead of the markup
    // due to a timing error. The fluent label attribute was being applied
    // after we had updated appMenuLabel and thus displayed an incorrect
    // label for signed in users.
    const novaFxaLabel = PanelMultiView.getViewNode(
      document,
      "appMenu-nova-fxa-label"
    );
    const [headerDesc, headerText, novaSignIn] =
      this.fluentStrings.formatValuesSync([
        "appmenu-fxa-signed-in-label",
        "appmenu-fxa-sync-and-save-data2",
        "appmenu-nova-fxa-sign-in",
      ]);
    appMenuHeaderDescription.value = headerDesc;
    appMenuHeaderText.textContent = headerText;
    if (novaFxaLabel) {
      novaFxaLabel.label = novaSignIn;
    }

    for (let topic of this._obs) {
      Services.obs.addObserver(this, topic, true);
    }

    this.maybeUpdateUIState();

    EnsureFxAccountsWebChannel();

    // Sign-in promo shown in the app menu (main view) when signed out.
    PanelMultiView.getViewNode(
      document,
      "appMenu-fxa-sign-in-promo-button"
    ).addEventListener("click", this);

    let fxaPanelView = PanelMultiView.getViewNode(document, "PanelUI-fxa");
    fxaPanelView.addEventListener("ViewShowing", this);
    fxaPanelView.addEventListener("ViewHiding", this);
    fxaPanelView.addEventListener("command", this);
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sign-in-promo-button"
    ).addEventListener("click", this);
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-signed-out-sign-in-button"
    ).addEventListener("click", this);
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sendtab-sign-in-button"
    ).addEventListener("click", this);
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sendtab-enable-sync-button"
    ).addEventListener("click", this);
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sendtab-verify-account-button"
    ).addEventListener("click", this);
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sendtab-connect-phone-button"
    ).addEventListener("click", this);
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sendtab-no-phone-button"
    ).addEventListener("click", this);

    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sync-status-button"
    ).addEventListener("click", e =>
      this._onSyncStatusButtonClick(e.currentTarget, e)
    );
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sync-status-off-button"
    ).addEventListener("click", e =>
      this.openPrefsFromFxaMenu("sync_settings", e.currentTarget)
    );
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-secure-sync-subpanel"
    ).addEventListener("command", this);
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-all-devices"
    ).addEventListener("command", this);

    PanelUI.mainView.addEventListener("ViewShowing", this);

    // If the experiment is enabled, we'll need to update the panels
    // to show some different text to the user
    if (this.FXA_CTA_MENU_ENABLED) {
      this.updateFxAPanel(UIState.get());
      this.updateCTAPanel();
    }

    const avatarIconVariant =
      NimbusFeatures.fxaButtonVisibility.getVariable("avatarIconVariant");
    if (avatarIconVariant) {
      this.applyAvatarIconVariant(avatarIconVariant);
    }

    this._initialized = true;
  },

  uninit() {
    if (!this._initialized) {
      return;
    }

    for (let topic of this._obs) {
      Services.obs.removeObserver(this, topic);
    }

    this._initialized = false;
  },

  handleEvent(event) {
    switch (event.type) {
      case "mouseover":
        this.refreshSyncButtonsTooltip();
        break;
      case "command":
      case "click": {
        this.onCommand(event.target);
        break;
      }
      case "ViewShowing": {
        if (event.target == PanelUI.mainView) {
          this.onAppMenuShowing();
        } else {
          this.onFxAPanelViewShowing(event.target);
        }
        break;
      }
      case "ViewHiding": {
        this.onFxAPanelViewHiding(event.target);
      }
    }
  },

  onAppMenuShowing() {
    const appMenuHeaderText = PanelMultiView.getViewNode(
      document,
      "appMenu-fxa-text"
    );

    const ctaDefaultStringID = "appmenu-fxa-sync-and-save-data2";
    const ctaStringID = this.getMenuCtaCopy(NimbusFeatures.fxaAppMenuItem);

    document.l10n.setAttributes(
      appMenuHeaderText,
      ctaStringID || ctaDefaultStringID
    );

    if (NimbusFeatures.fxaAppMenuItem.getVariable("ctaCopyVariant")) {
      NimbusFeatures.fxaAppMenuItem.recordExposureEvent();
    }
  },

  onFxAPanelViewShowing(panelview) {
    let messageId = panelview.getAttribute(
      MenuMessage.SHOWING_FXA_MENU_MESSAGE_ATTR
    );
    if (messageId) {
      MenuMessage.recordMenuMessageTelemetry(
        "IMPRESSION",
        MenuMessage.SOURCES.PXI_MENU,
        messageId
      );
      let message = ASRouter.getMessageById(messageId);
      ASRouter.addImpression(message);
    }

    const syncEnabled = UIState.get().syncEnabled;
    if (!syncEnabled) {
      this._disableSyncOffIndicator();
    }

    // We should ensure that we do not show the sign out button
    // if the user is not signed in
    const signOutButtonEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-account-signout-button"
    );
    signOutButtonEl.hidden = !this.isSignedIn;

    const devicesListEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-devices-list"
    );
    const signOutSeparatorEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-sign-out-separator"
    );

    // The FxA panelview is shared between the account (toolbar) menu and the
    // app (hamburger) menu, so the sign-out button's position is set per show.
    // In the app menu the sign-out button sits directly below the sync status
    // section and above the connected devices list, with a separator either
    // side. In the account menu it stays at the bottom, below the devices list.
    const inAppMenu = document
      .getElementById("appMenu-popup")
      ?.contains(devicesListEl);
    if (inAppMenu) {
      signOutButtonEl.after(devicesListEl);
    } else {
      signOutSeparatorEl.before(devicesListEl);
    }

    panelview.syncedTabsPanelList = new FxAMenuDeviceList(devicesListEl);

    // Any variant on the CTA will have been applied inside of updateFxAPanel,
    // but now that the panel is showing, we record exposure.
    const ctaCopyVariant =
      NimbusFeatures.fxaAvatarMenuItem.getVariable("ctaCopyVariant");
    if (ctaCopyVariant) {
      NimbusFeatures.fxaAvatarMenuItem.recordExposureEvent();
    }
  },

  onFxAPanelViewHiding(panelview) {
    MenuMessage.hidePxiMenuMessage(gBrowser.selectedBrowser);
    panelview.syncedTabsPanelList.destroy();
    panelview.syncedTabsPanelList = null;
  },

  _showSecureSyncSubpanel(anchor, event) {
    const deviceName = fxAccounts.device.getLocalName();
    const labelEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-secure-sync-now-label"
    );
    labelEl.setAttribute(
      "value",
      this.fluentStrings.formatValueSync("fxa-menu-sync-device-now", {
        deviceName,
      })
    );
    PanelUI.showSubView("PanelUI-fxa-menu-secure-sync-subpanel", anchor, event);
  },

  /**
   * Configures the sync status button, which sits where "Sync is On" appears
   * when sync is enabled. It has three variants:
   *  - signed in with sync on: "Sync is On" with the last sync time, a chevron,
   *    and opens the secure sync subpanel.
   *  - signed in with sync off: "Sync is Off" with an error-colored
   *    "Your data isn't syncing" and opens sync preferences.
   *  - never signed in: "Sync Your Data" with no description and opens the
   *    sign-in page.
   *  - signed in but needing (re-)authentication: "Sync is Off" with an
   *    error-colored "Sign in to sync" and opens the sign-in page.
   */
  _updateSyncStatusButton(state) {
    const btn = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sync-status-button"
    );
    const titleEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sync-status-title"
    );
    const descEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sync-status-description"
    );
    const offCard = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sync-status-off-card"
    );
    const offTitleEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sync-status-off-title"
    );
    const offDescEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sync-status-off-description"
    );
    const mobileBtn = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-get-firefox-mobile"
    );

    const syncOn =
      state.status == UIState.STATUS_SIGNED_IN && state.syncEnabled;
    const signedIn = state.status == UIState.STATUS_SIGNED_IN;
    // Signed in with sync off uses a dedicated card with a "Turn on" button
    // instead of the navigable status button.
    const syncOffCard = signedIn && !state.syncEnabled;

    offCard.hidden = !syncOffCard;
    if (syncOffCard) {
      btn.hidden = true;
      offTitleEl.setAttribute(
        "value",
        this.fluentStrings.formatValueSync("fxa-menu-sync-status-off")
      );
      offDescEl.setAttribute(
        "value",
        this.fluentStrings.formatValueSync("fxa-menu-sync-off-data-description")
      );
      offCard.after(mobileBtn);
      mobileBtn.hidden = false;
      return;
    }

    // A user who has never signed in gets a call-to-action title with no
    // description instead of the "Sync is Off" / "Sign in to sync" copy.
    const neverSignedIn = state.status == UIState.STATUS_NOT_CONFIGURED;

    // The chevron is only meaningful when the button navigates to the secure
    // sync subpanel (sync on).
    btn.classList.toggle("subviewbutton-nav", syncOn);

    let neverSignedInId = neverSignedIn
      ? "fxa-menu-sync-your-data"
      : "fxa-menu-sync-status-off";
    let titleId = syncOn ? "fxa-menu-sync-status-on" : neverSignedInId;
    titleEl.setAttribute("value", this.fluentStrings.formatValueSync(titleId));

    if (syncOn) {
      descEl.classList.remove("fxa-menu-sync-status-description-error");
      let lastSyncDate = this.formatLastSyncDate(state.lastSync);
      if (lastSyncDate) {
        descEl.setAttribute(
          "value",
          this.fluentStrings.formatValueSync("appmenu-fxa-last-sync", {
            time: lastSyncDate,
          })
        );
      } else {
        descEl.removeAttribute("value");
      }
    } else if (neverSignedIn) {
      descEl.classList.remove("fxa-menu-sync-status-description-error");
      descEl.removeAttribute("value");
    } else {
      descEl.classList.add("fxa-menu-sync-status-description-error");
      descEl.setAttribute(
        "value",
        this.fluentStrings.formatValueSync(
          "fxa-menu-sync-off-signin-description"
        )
      );
    }

    // Don't render the description label when there's nothing to show.
    descEl.hidden = !descEl.hasAttribute("value");

    btn.hidden = false;

    // "Get Firefox for mobile" sits directly under the sync status button and
    // is only offered while sync is off.
    btn.after(mobileBtn);
    mobileBtn.hidden = syncOn;
  },

  _onSyncStatusButtonClick(anchor, event) {
    const state = UIState.get();
    if (state.status == UIState.STATUS_SIGNED_IN && state.syncEnabled) {
      this._showSecureSyncSubpanel(anchor, event);
    } else if (state.status == UIState.STATUS_SIGNED_IN) {
      // Signed in with sync off: open preferences to turn sync on.
      this.openPrefsFromFxaMenu("sync_settings", anchor);
    } else {
      // Needs (re-)authentication: open the sign-in page.
      this.openFxAEmailFirstPageFromFxaMenu(anchor);
    }
  },

  onCommand(button) {
    switch (button.id) {
      case "PanelUI-fxa-menu-setup-sync-button":
        this.openSyncSetup("sync_settings", button);
        break;
      case "PanelUI-fxa-menu-get-firefox-mobile":
        this.openGetFirefoxMobile();
        break;

      case "PanelUI-fxa-menu-sendtab-connect-device-button":
      // fall through
      case "PanelUI-fxa-menu-secure-sync-add-device":
      case "PanelUI-fxa-menu-all-devices-add-device":
        this.clickOpenConnectAnotherDevice(button);
        break;
      case "PanelUI-fxa-menu-secure-sync-manage-devices":
      case "PanelUI-fxa-menu-all-devices-manage-devices":
        this.openDevicesManagementPage(this._getEntryPointForElement(button));
        break;
      case "PanelUI-fxa-menu-secure-sync-device-missing":
      case "PanelUI-fxa-menu-all-devices-device-missing":
        this.openDeviceMissingHelp();
        break;

      case "PanelUI-fxa-menu-secure-sync-now":
        this.doSyncFromFxaMenu(button);
        break;
      case "PanelUI-fxa-menu-secure-sync-settings":
        this.openPrefsFromFxaMenu("sync_settings", button);
        break;

      case "PanelUI-fxa-menu-signed-out-sign-in-button":
      case "PanelUI-fxa-menu-manage-account-button":
        this.clickFxAMenuHeaderButton(button);
        break;
      case "PanelUI-fxa-menu-sign-in-promo-button":
        this.openFxAEmailFirstPageFromFxaMenu(button);
        break;
      case "appMenu-fxa-sign-in-promo-button":
        // Sign-in promo in the app menu: go to the sign-in page, close the menu.
        this.openFxAEmailFirstPageFromFxaMenu(button);
        PanelUI.hide();
        break;
      case "PanelUI-fxa-menu-account-signout-button":
        this.disconnect();
        break;
      case "PanelUI-fxa-menu-monitor-button":
        this.openMonitorLink(button);
        break;
      case "PanelUI-fxa-menu-relay-button":
        this.openRelayLink(button);
        break;
      case "PanelUI-fxa-menu-vpn-button":
        this.openVPNLink(button);
        break;
      case "PanelUI-fxa-menu-share-firefox":
        this.openShareFirefoxLink();
        break;
      case "PanelUI-fxa-menu-sendtab-sign-in-button":
        this.signInToSync(button);
        break;
      case "PanelUI-fxa-menu-sendtab-enable-sync-button":
        this.enableSync();
        break;
      case "PanelUI-fxa-menu-sendtab-verify-account-button":
        this.verifyAccount();
        break;
      case "PanelUI-fxa-menu-sendtab-connect-phone-button":
        this.openPairDevice(button);
        break;
      case "PanelUI-fxa-menu-sendtab-no-phone-button":
        this.openSendTabHelp();
        break;
    }
  },

  observe(subject, topic, data) {
    if (!this._initialized) {
      console.error("browser-sync observer called after unload: ", topic);
      return;
    }
    switch (topic) {
      case UIState.ON_UPDATE: {
        const state = UIState.get();
        this.updateAllUI(state);
        break;
      }
      case "quit-application":
        // Stop the animation timer on shutdown, since we can't update the UI
        // after this.
        clearTimeout(this._syncAnimationTimer);
        break;
      case "weave:engine:sync:finish":
        if (data != "clients") {
          return;
        }
        this.onClientsSynced();
        this.updateFxAPanel(UIState.get());
        break;
    }
  },

  updateAllUI(state) {
    this.updatePanelPopup(state);
    this.updateState(state);
    this.updateSyncButtonsTooltip(state);
    this.updateSyncStatus(state);
    this.updateFxAPanel(state);
    this.ensureFxaDevices();
    this.fetchListOfOAuthClients();
  },

  // Ensure we have *something* in `fxAccounts.device.recentDeviceList` as some
  // of our UI logic depends on it not being null. When FxA is notified of a
  // device change it will auto refresh `recentDeviceList`, and all UI which
  // shows the device list will start with `recentDeviceList`, but should also
  // force a refresh, both of which should mean in the worst-case, the UI is up
  // to date after a very short delay.
  async ensureFxaDevices() {
    if (UIState.get().status != UIState.STATUS_SIGNED_IN) {
      console.info("Skipping device list refresh; not signed in");
      return;
    }
    if (!fxAccounts.device.recentDeviceList) {
      if (await this.refreshFxaDevices()) {
        // Assuming we made the call successfully it should be impossible to end
        // up with a falsey recentDeviceList, so make noise if that's false.
        if (!fxAccounts.device.recentDeviceList) {
          console.warn("Refreshing device list didn't find any devices.");
        }
      }
    }
  },

  // Force a refresh of the fxa device list.  Note that while it's theoretically
  // OK to call `fxAccounts.device.refreshDeviceList` multiple times concurrently
  // and regularly, this call tells it to avoid those protections, so will always
  // hit the FxA servers - therefore, you should be very careful how often you
  // call this.
  // Returns Promise<bool> to indicate whether a refresh was actually done.
  async refreshFxaDevices() {
    if (UIState.get().status != UIState.STATUS_SIGNED_IN) {
      console.info("Skipping device list refresh; not signed in");
      return false;
    }
    try {
      // Do the actual refresh telling it to avoid the "flooding" protections.
      await fxAccounts.device.refreshDeviceList({ ignoreCached: true });
      return true;
    } catch (e) {
      this.log.error("Refreshing device list failed.", e);
      return false;
    }
  },

  /**
   * Potential network call. Fetch the list of OAuth clients attached to the current Mozilla account.
   *
   * @returns {Promise<boolean>} - Resolves to true if successful, false otherwise.
   */
  async fetchListOfOAuthClients() {
    if (!this.isSignedIn) {
      console.info("Skipping fetching other attached clients");
      return false;
    }
    try {
      this._attachedClients = await fxAccounts.listAttachedOAuthClients();
      return true;
    } catch (e) {
      this.log.error("Could not fetch attached OAuth clients", e);
      return false;
    }
  },

  async toggleAccountPanel(anchor = null, aEvent) {
    // Don't show the panel if the window is in customization mode.
    if (document.documentElement.hasAttribute("customizing")) {
      return;
    }

    if (
      (aEvent.type == "mousedown" && aEvent.button != 0) ||
      (aEvent.type == "keypress" &&
        aEvent.charCode != KeyEvent.DOM_VK_SPACE &&
        aEvent.keyCode != KeyEvent.DOM_VK_RETURN)
    ) {
      return;
    }

    const fxaToolbarMenuBtn = document.getElementById(
      "fxa-toolbar-menu-button"
    );

    if (anchor === null) {
      anchor = fxaToolbarMenuBtn;
    }

    if (anchor == fxaToolbarMenuBtn && anchor.getAttribute("open") != "true") {
      if (ASRouter.initialized) {
        await ASRouter.sendTriggerMessage({
          browser: gBrowser.selectedBrowser,
          id: "menuOpened",
          context: { source: MenuMessage.SOURCES.PXI_MENU },
        });
      }
    }

    // We read the state that's been set on the root node, since that makes
    // it easier to test the various front-end states without having to actually
    // have UIState know about it.
    let fxaStatus = document.documentElement.getAttribute("fxastatus");

    if (fxaStatus == "not_configured") {
      // sign in button in app (hamburger) menu
      // should take you straight to fxa sign in page
      if (
        anchor.id == "appMenu-fxa-label2" ||
        anchor.id == "appMenu-nova-fxa-label"
      ) {
        this.openFxAEmailFirstPageFromFxaMenu(anchor);
        PanelUI.hide();
        return;
      }

      // If we're signed out but have the PXI pref enabled
      // we should show the PXI panel instead of taking the user
      // straight to FxA sign-in
      if (this.FXA_CTA_MENU_ENABLED) {
        this.updateFxAPanel(UIState.get());
        this.updateCTAPanel(anchor);
        PanelUI.showSubView("PanelUI-fxa", anchor, aEvent);
      } else {
        this.updateFxAPanel(UIState.get());
        PanelUI.showSubView("PanelUI-fxa", anchor, aEvent);
      }
      return;
    }
    // If the user is signed in and we have the PXI pref enabled then add
    // the pxi panel to the existing toolbar
    if (this.FXA_CTA_MENU_ENABLED) {
      this.updateCTAPanel(anchor);
    }

    if (!gFxaToolbarAccessed) {
      Services.prefs.setBoolPref("identity.fxaccounts.toolbar.accessed", true);
    }

    if (anchor.getAttribute("open") == "true") {
      PanelUI.hide();
    } else {
      this.emitFxaToolbarTelemetry("toolbar_icon", anchor);
      PanelUI.showSubView("PanelUI-fxa", anchor, aEvent);
    }
  },

  _disableSyncOffIndicator() {
    const SYNC_PANEL_ACCESSED_PREF =
      "identity.fxaccounts.toolbar.syncSetup.panelAccessed";
    if (!Services.prefs.getBoolPref(SYNC_PANEL_ACCESSED_PREF, false)) {
      // Turn off the indicator so the user doesn't see it in subsequent openings
      Services.prefs.setBoolPref(SYNC_PANEL_ACCESSED_PREF, true);
    }
  },

  updateFxAPanel(state = {}) {
    const expandedSignInCopy =
      NimbusFeatures.expandSignInButton.getVariable("ctaCopyVariant");
    const mainWindowEl = document.documentElement;

    const menuHeaderTitleEl = PanelMultiView.getViewNode(
      document,
      "fxa-menu-header-title"
    );
    const menuHeaderDescriptionEl = PanelMultiView.getViewNode(
      document,
      "fxa-menu-header-description"
    );
    const manageAccountButtonEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-manage-account-button"
    );
    const signInPromoEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sign-in-promo"
    );
    const signedOutCardEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-signed-out-card"
    );
    const signedOutSeparatorEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-signed-out-separator"
    );
    const signedInContainer = PanelMultiView.getViewNode(
      document,
      "PanelUI-signedin-panel"
    );
    const signOutSeparator = PanelMultiView.getViewNode(
      document,
      "PanelUI-sign-out-separator"
    );
    const manageAccountSeparator = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-manage-account-separator"
    );
    const syncSetupEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-setup-sync-container"
    );
    const syncStatusBtn = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sync-status-button"
    );
    const fxaToolbarMenuButton = document.getElementById(
      "fxa-toolbar-menu-button"
    );
    const syncSetupSeparator = PanelMultiView.getViewNode(
      document,
      "PanelUI-set-up-sync-separator"
    );

    let fxaAvatarLabelEl = document.getElementById("fxa-avatar-label");

    // Reset FxA/Sync UI elements to default, which is signed out
    syncStatusBtn.hidden = true;
    signedInContainer.prepend(syncStatusBtn);
    syncSetupEl.setAttribute("hidden", "true");
    signedInContainer.hidden = false;
    manageAccountButtonEl.hidden = true;
    manageAccountSeparator.hidden = true;
    signInPromoEl.hidden = true;
    signedOutCardEl.hidden = true;
    signedOutSeparatorEl.hidden = true;
    menuHeaderDescriptionEl.hidden = false;

    // Expanded sign in copy experiment is only for signed out users
    // so if a text variant has been provided then we show the expanded label
    // otherwise it'll be the default avatar icon
    // fxaToolbarMenuButton can be null in certain testing scenarios
    if (fxaToolbarMenuButton) {
      if (
        state.status === UIState.STATUS_NOT_CONFIGURED &&
        expandedSignInCopy
      ) {
        fxaAvatarLabelEl.setAttribute(
          "value",
          this.fluentStrings.formatValueSync(expandedSignInCopy)
        );
        fxaAvatarLabelEl.removeAttribute("hidden");
        fxaToolbarMenuButton.setAttribute("data-l10n-id", "fxa-avatar-tooltip");
        fxaToolbarMenuButton.classList.add("avatar-button-background");
      } else {
        // Either signed in, or experiment not enabled
        fxaToolbarMenuButton.setAttribute(
          "data-l10n-id",
          "toolbar-button-account"
        );
        fxaToolbarMenuButton.classList.remove("avatar-button-background");
        fxaAvatarLabelEl.hidden = true;
      }
    }

    // The Firefox Account toolbar currently handles 3 different states for
    // users. The default `not_configured` state shows an empty avatar, `unverified`
    // state shows an avatar with an email icon, `login-failed` state shows an avatar
    // with a danger icon and the `verified` state will show the users
    // custom profile image or a filled avatar.
    let stateValue = "not_configured";
    let headerTitleL10nId;
    let headerDescription;

    switch (state.status) {
      case UIState.STATUS_NOT_CONFIGURED:
        signOutSeparator.hidden = true;
        mainWindowEl.style.removeProperty("--avatar-image-url");

        // When signed out, show the sign-in promo. A previous account may be
        // remembered as a hashed UID, but the email can't be recovered from it,
        // so the promo is shown regardless. The signed-out card (with the
        // remembered email) is only used for the login-failed and not-verified
        // states.
        signInPromoEl.hidden = false;

        headerTitleL10nId = this.FXA_CTA_MENU_ENABLED
          ? "synced-tabs-fxa-sign-in"
          : "appmenuitem-sign-in-account";
        headerDescription = this.fluentStrings.formatValueSync(
          this.FXA_CTA_MENU_ENABLED
            ? "fxa-menu-sync-description"
            : "appmenu-fxa-signed-in-label"
        );
        if (this.FXA_CTA_MENU_ENABLED) {
          const ctaCopy = this.getMenuCtaCopy(NimbusFeatures.fxaAvatarMenuItem);
          if (ctaCopy) {
            headerTitleL10nId = ctaCopy.headerTitleL10nId;
            headerDescription = ctaCopy.headerDescription;
          }
        }

        this._positionSecureSyncSection(signedInContainer);

        break;

      case UIState.STATUS_LOGIN_FAILED:
        signOutSeparator.hidden = true;
        stateValue = "login-failed";
        headerTitleL10nId = "account-disconnected2";
        headerDescription = state.displayName || state.email;
        mainWindowEl.style.removeProperty("--avatar-image-url");
        this._showFxASignedOutCard(signedOutCardEl, state);
        this._positionSecureSyncSection(signedInContainer);
        break;

      case UIState.STATUS_NOT_VERIFIED:
        signOutSeparator.hidden = true;
        stateValue = "unverified";
        headerTitleL10nId = "account-finish-account-setup";
        headerDescription = state.displayName || state.email;
        this._showFxASignedOutCard(signedOutCardEl, state);
        this._positionSecureSyncSection(signedInContainer);
        break;

      case UIState.STATUS_SIGNED_IN:
        stateValue = "signedin";
        headerTitleL10nId = "appmenuitem-fxa-manage-account";
        headerDescription = state.displayName || state.email;
        this.updateAvatarURL(
          mainWindowEl,
          state.avatarURL,
          state.avatarIsDefault
        );
        // Show the signed-in account button with the avatar, the account email
        // and a "Manage account" affordance.
        PanelMultiView.getViewNode(
          document,
          "PanelUI-fxa-menu-manage-account-email"
        ).value = state.displayName || state.email;
        manageAccountButtonEl.hidden = false;
        signOutSeparator.hidden = false;
        signedInContainer.hidden = false;
        syncSetupSeparator.setAttribute("hidden", "true");

        // Reposition profiles elements
        manageAccountSeparator.remove();
        this._positionSecureSyncSection(manageAccountButtonEl);
        // Single separator below the manage account button, above whichever
        // section comes next (profiles when shown, otherwise secure sync).
        manageAccountSeparator.hidden = false;
        // Inserted last so it lands directly below the manage account button,
        // separating it from the profiles section.
        manageAccountButtonEl.after(manageAccountSeparator);

        break;

      default:
        // Unknown/empty state: fall back to the signed-out promo.
        signInPromoEl.hidden = false;
        headerTitleL10nId = this.FXA_CTA_MENU_ENABLED
          ? "synced-tabs-fxa-sign-in"
          : "appmenuitem-sign-in-account";
        headerDescription = this.fluentStrings.formatValueSync(
          "fxa-menu-turn-on-sync-default"
        );
        break;
    }

    this._updateSyncStatusButton(state);

    // Update UI elements with determined values
    mainWindowEl.setAttribute("fxastatus", stateValue);
    menuHeaderTitleEl.value =
      this.fluentStrings.formatValueSync(headerTitleL10nId);
    // If we description is empty, we hide it
    menuHeaderDescriptionEl.hidden = !headerDescription;
    menuHeaderDescriptionEl.value = headerDescription;
    // We remove the data-l10n-id attribute here to prevent the node's value
    // attribute from being overwritten by Fluent when the panel is moved
    // around in the DOM.
    menuHeaderTitleEl.removeAttribute("data-l10n-id");
    menuHeaderDescriptionEl.removeAttribute("data-l10n-id");
  },

  // Moves the Profiles and Secure sync sections directly below the header
  // anchored by anchorEl, so the visible sync status button lands under the
  // "Secure sync" header instead of above the Profiles section.
  _positionSecureSyncSection(anchorEl) {
    const profilesHeaderLabel = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-profiles-header-label"
    );
    const profileButtonsContainer = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-profile-buttons"
    );
    const profilesSeparator = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-profiles-separator"
    );
    const secureSyncHeader = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-secure-sync-header"
    );
    const syncStatusBtn = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sync-status-button"
    );

    profilesHeaderLabel.remove();
    profileButtonsContainer.remove();
    profilesSeparator.remove();
    secureSyncHeader.remove();

    secureSyncHeader.hidden = false;

    anchorEl.after(secureSyncHeader);
    anchorEl.after(profilesSeparator);
    anchorEl.after(profileButtonsContainer);
    anchorEl.after(profilesHeaderLabel);

    secureSyncHeader.after(syncStatusBtn);
  },

  // Shows a card with the remembered account's email, a status-specific reason,
  // and a button to sign back in.
  _showFxASignedOutCard(cardEl, state) {
    const emailEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-signed-out-email"
    );
    const messageEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-signed-out-message"
    );
    const separatorEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-signed-out-separator"
    );

    emailEl.value = state.email ?? "";
    document.l10n.setAttributes(
      messageEl,
      state.status === UIState.STATUS_NOT_VERIFIED
        ? "fxa-menu-signed-out-message-unverified"
        : "fxa-menu-signed-out-message-login-failed"
    );

    cardEl.hidden = false;
    separatorEl.hidden = false;
  },

  updateAvatarURL(mainWindowEl, avatarURL, avatarIsDefault) {
    if (avatarURL && !avatarIsDefault) {
      const bgImage = `url("${avatarURL}")`;
      const img = new Image();
      img.onload = () => {
        mainWindowEl.style.setProperty("--avatar-image-url", bgImage);
      };
      img.onerror = () => {
        mainWindowEl.style.removeProperty("--avatar-image-url");
      };
      img.src = avatarURL;
    } else {
      mainWindowEl.style.removeProperty("--avatar-image-url");
    }
  },

  /**
   * Record an FxA menu telemetry event. The entry point (fxa_avatar_menu vs
   * fxa_app_menu) is resolved from where `sourceElement` lives in the DOM at
   * call time, so make sure `sourceElement` is in its source popup when this
   * fires. For events fired after panel navigation (e.g. clicking a device in
   * the Send Tab submenu), pass an element whose DOM position is stable, like
   * the FxA menu's Send Tab button.
   *
   * @param {string} type - event type, e.g. "sync_now", "send_tab_opened"
   * @param {Element} sourceElement - element used to resolve the entry point
   * @param {object} [extraOpts] - additional Glean event extra keys
   */
  emitFxaToolbarTelemetry(type, sourceElement, extraOpts = {}) {
    if (!UIState.isReady()) {
      return;
    }
    const entryPoint = this._getEntryPointForElement(sourceElement);
    let category = null;
    if (entryPoint == "fxa_avatar_menu") {
      category = "fxaAvatarMenu";
    } else if (entryPoint == "fxa_app_menu") {
      category = "fxaAppMenu";
    } else {
      return;
    }

    const state = UIState.get();
    const hasAvatar = state.avatarURL && !state.avatarIsDefault;
    const extraOptions = {
      fxa_status: state.status,
      fxa_avatar: hasAvatar ? "true" : "false",
      fxa_sync_on: state.syncEnabled,
      ...extraOpts,
    };

    // send_tab_exposed -> sendTabExposed, send_tab_opened -> sendTabOpened.
    // All other types are legacy click events: sync_now -> clickSyncNow,
    // send_tab -> clickSendTab, etc.
    const cap = w => w[0].toUpperCase() + w.slice(1);
    const parts = type.split("_");
    const methodName = type.startsWith("send_tab_")
      ? parts[0] + parts.slice(1).map(cap).join("")
      : "click" + parts.map(cap).join("");

    Glean[category][methodName]?.record(extraOptions);
  },

  updatePanelPopup({ email, displayName, status }) {
    const appMenuStatus = PanelMultiView.getViewNode(
      document,
      "appMenu-fxa-status2"
    );
    const appMenuLabel = PanelMultiView.getViewNode(
      document,
      "appMenu-fxa-label2"
    );
    const appMenuHeaderText = PanelMultiView.getViewNode(
      document,
      "appMenu-fxa-text"
    );
    const appMenuHeaderTitle = PanelMultiView.getViewNode(
      document,
      "appMenu-header-title"
    );
    const appMenuHeaderDescription = PanelMultiView.getViewNode(
      document,
      "appMenu-header-description"
    );
    const fxaPanelView = PanelMultiView.getViewNode(document, "PanelUI-fxa");

    let defaultLabel = this.fluentStrings.formatValueSync(
      "appmenu-fxa-signed-in-label"
    );
    // Reset the status bar to its original state.
    appMenuLabel.setAttribute("label", defaultLabel);
    appMenuLabel.removeAttribute("aria-labelledby");
    appMenuStatus.removeAttribute("fxastatus");

    if (status == UIState.STATUS_NOT_CONFIGURED) {
      appMenuHeaderText.hidden = false;
      appMenuStatus.classList.add("toolbaritem-combined-buttons");
      appMenuLabel.classList.remove("subviewbutton-nav");
      appMenuHeaderTitle.hidden = true;
      appMenuHeaderDescription.value = defaultLabel;
      return;
    }
    appMenuLabel.classList.remove("subviewbutton-nav");

    appMenuHeaderText.hidden = true;
    appMenuStatus.classList.remove("toolbaritem-combined-buttons");

    // While we prefer the display name in most case, in some strings
    // where the context is something like "Verify %s", the email
    // is used even when there's a display name.
    if (status == UIState.STATUS_LOGIN_FAILED) {
      const [tooltipDescription, errorLabel] =
        this.fluentStrings.formatValuesSync([
          { id: "account-reconnect", args: { email } },
          { id: "account-disconnected2" },
        ]);
      appMenuStatus.setAttribute("fxastatus", "login-failed");
      appMenuStatus.setAttribute("tooltiptext", tooltipDescription);
      appMenuLabel.classList.add("subviewbutton-nav");
      appMenuHeaderTitle.hidden = false;
      appMenuHeaderTitle.value = errorLabel;
      appMenuHeaderDescription.value = displayName || email;

      appMenuLabel.removeAttribute("label");
      appMenuLabel.setAttribute(
        "aria-labelledby",
        `${appMenuHeaderTitle.id},${appMenuHeaderDescription.id}`
      );
      return;
    } else if (status == UIState.STATUS_NOT_VERIFIED) {
      const [tooltipDescription, unverifiedLabel] =
        this.fluentStrings.formatValuesSync([
          { id: "account-verify", args: { email } },
          { id: "account-finish-account-setup" },
        ]);
      appMenuStatus.setAttribute("fxastatus", "unverified");
      appMenuStatus.setAttribute("tooltiptext", tooltipDescription);
      appMenuLabel.classList.add("subviewbutton-nav");
      appMenuHeaderTitle.hidden = false;
      appMenuHeaderTitle.value = unverifiedLabel;
      appMenuHeaderDescription.value = email;

      appMenuLabel.removeAttribute("label");
      appMenuLabel.setAttribute(
        "aria-labelledby",
        `${appMenuHeaderTitle.id},${appMenuHeaderDescription.id}`
      );
      return;
    }

    appMenuHeaderTitle.hidden = true;
    appMenuHeaderDescription.value = displayName || email;
    appMenuStatus.setAttribute("fxastatus", "signedin");
    appMenuLabel.setAttribute("label", displayName || email);
    appMenuLabel.classList.add("subviewbutton-nav");
    fxaPanelView.setAttribute(
      "title",
      this.fluentStrings.formatValueSync("appmenu-account-header")
    );
    appMenuStatus.removeAttribute("tooltiptext");
  },

  updateState(state) {
    for (let [shown, menuId, boxId] of [
      [
        state.status == UIState.STATUS_NOT_CONFIGURED,
        "sync-setup",
        "PanelUI-remotetabs-setupsync",
      ],
      [
        state.status == UIState.STATUS_SIGNED_IN && !state.syncEnabled,
        "sync-enable",
        "PanelUI-remotetabs-syncdisabled",
      ],
      [
        state.status == UIState.STATUS_LOGIN_FAILED,
        "sync-reauthitem",
        "PanelUI-remotetabs-reauthsync",
      ],
      [
        state.status == UIState.STATUS_NOT_VERIFIED,
        "sync-unverifieditem",
        "PanelUI-remotetabs-unverified",
      ],
      [
        state.status == UIState.STATUS_SIGNED_IN && state.syncEnabled,
        "sync-syncnowitem",
        "PanelUI-remotetabs-main",
      ],
    ]) {
      document.getElementById(menuId).hidden = PanelMultiView.getViewNode(
        document,
        boxId
      ).hidden = !shown;
    }
  },

  updateSyncStatus(state) {
    let syncNow =
      document.querySelector(".syncNowBtn") ||
      document
        .getElementById("appMenu-viewCache")
        .content.querySelector(".syncNowBtn");
    const syncingUI = syncNow.getAttribute("syncstatus") == "active";
    if (state.syncing != syncingUI) {
      // Do we need to update the UI?
      state.syncing ? this.onActivityStart() : this.onActivityStop();
    }
  },

  async openSignInAgainPage(entryPoint) {
    if (!(await FxAccounts.canConnectAccount())) {
      return;
    }
    const url = await FxAccounts.config.promiseConnectAccountURI(
      "sync",
      entryPoint
    );
    switchToTabHavingURI(url, true, {
      replaceQueryString: true,
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });
  },

  async openDevicesManagementPage(entryPoint) {
    let url = await FxAccounts.config.promiseManageDevicesURI(entryPoint);
    switchToTabHavingURI(url, true, {
      replaceQueryString: true,
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });
  },

  async openConnectAnotherDevice(entryPoint) {
    const url = await FxAccounts.config.promiseConnectDeviceURI(
      "sync",
      entryPoint
    );
    openTrustedLinkIn(url, "tab");
  },

  async clickOpenConnectAnotherDevice(sourceElement) {
    this.emitFxaToolbarTelemetry("cad", sourceElement);
    let entryPoint = this._getEntryPointForElement(sourceElement);
    this.openConnectAnotherDevice(entryPoint);
  },

  openSendToDevicePromo() {
    const url = Services.urlFormatter.formatURLPref(
      "identity.sendtabpromo.url"
    );
    switchToTabHavingURI(url, true, { replaceQueryString: true });
  },

  async clickFxAMenuHeaderButton(sourceElement) {
    // Depending on the current logged in state of a user,
    // clicking the FxA header will either open
    // a sign-in page, account management page, or sync
    // preferences page.
    const { status } = UIState.get();
    switch (status) {
      case UIState.STATUS_NOT_CONFIGURED:
        this.openFxAEmailFirstPageFromFxaMenu(sourceElement);
        break;
      case UIState.STATUS_LOGIN_FAILED:
        this.openPrefsFromFxaMenu("sync_settings", sourceElement);
        break;
      case UIState.STATUS_NOT_VERIFIED:
        this.openFxAEmailFirstPage("fxa_app_menu_reverify");
        break;
      case UIState.STATUS_SIGNED_IN:
        this._openFxAManagePageFromElement(sourceElement);
    }
  },

  // Gets the telemetry "entry point" we should use for a given UI element.
  // This entry-point is recorded in both client telemetry (typically called the "object")
  // and where applicable, also communicated to the server for server telemetry via a URL query param.
  //
  // It inspects the parent elements to determine if the element is within one of our "well known"
  // UI groups, in which case it will return a string for that group (eg, "fxa_app_menu", "fxa_toolbar_button").
  // Otherwise (eg, the item might be directly on the context menu), it will return "fxa_discoverability_native".
  _getEntryPointForElement(sourceElement) {
    // Note that when an element is in either the app menu or the toolbar button menu,
    // in both cases it *will* have a parent with ID "PanelUI-fxa-menu". But when
    // in the app menu, it will also have a grand-parent with ID "appMenu-popup".
    // So we must check for that outer grandparent first.
    const appMenuPanel = document.getElementById("appMenu-popup");
    if (appMenuPanel.contains(sourceElement)) {
      return "fxa_app_menu";
    }
    // If it *is* the toolbar button...
    if (sourceElement.id == "fxa-toolbar-menu-button") {
      return "fxa_avatar_menu";
    }
    // ... or is in the panel shown by that button (PanelUI-fxa-menu) or one
    // of its sibling Send Tab panelviews (PanelUI-fxa-menu-sendtab-*).
    if (sourceElement.closest?.('[id^="PanelUI-fxa-menu"]')) {
      return "fxa_avatar_menu";
    }
    return "fxa_discoverability_native";
  },

  async openFxAEmailFirstPage(entryPoint, extraParams = {}) {
    if (!(await FxAccounts.canConnectAccount())) {
      return;
    }
    const url = await FxAccounts.config.promiseConnectAccountURI(
      "sync",
      entryPoint,
      extraParams
    );
    switchToTabHavingURI(url, true, { replaceQueryString: true });
  },

  async openFxAEmailFirstPageFromFxaMenu(sourceElement, extraParams = {}) {
    this.emitFxaToolbarTelemetry("login", sourceElement);
    this.openFxAEmailFirstPage(
      this._getEntryPointForElement(sourceElement),
      extraParams
    );
  },

  async openFxAManagePage(entryPoint) {
    const url = await FxAccounts.config.promiseManageURI(entryPoint);
    switchToTabHavingURI(url, true, { replaceQueryString: true });
  },

  async _openFxAManagePageFromElement(sourceElement) {
    this.emitFxaToolbarTelemetry("account_settings", sourceElement);
    this.openFxAManagePage(this._getEntryPointForElement(sourceElement));
  },

  // Returns true if we managed to send the tab to any targets, false otherwise.
  async sendTabToDevice(tab, targets) {
    const fxaCommandsDevices = [];
    for (const target of targets) {
      if (fxAccounts.commands.sendTab.isDeviceCompatible(target)) {
        fxaCommandsDevices.push(target);
      } else {
        this.log.error(`Target ${target.id} unsuitable for send tab.`);
      }
    }
    // If a primary-password is enabled then it must be unlocked so FxA can get
    // the encryption keys from the login manager. (If we end up using the "sync"
    // fallback that would end up prompting by itself, but the FxA command route
    // will not) - so force that here.
    let cryptoSDR = Cc["@mozilla.org/login-manager/crypto/SDR;1"].getService(
      Ci.nsILoginManagerCrypto
    );
    if (!cryptoSDR.isLoggedIn) {
      if (cryptoSDR.uiBusy) {
        this.log.info("Master password UI is busy - not sending the tabs");
        return false;
      }
      try {
        cryptoSDR.encrypt("bacon"); // forces the mp prompt.
      } catch (e) {
        this.log.info(
          "Master password remains unlocked - not sending the tabs"
        );
        return false;
      }
    }
    let numFailed = 0;
    if (fxaCommandsDevices.length) {
      this.log.info(
        `Sending a tab to ${fxaCommandsDevices
          .map(d => d.id)
          .join(", ")} using FxA commands.`
      );
      const report = await fxAccounts.commands.sendTab.send(
        fxaCommandsDevices,
        tab
      );
      for (let { device, error } of report.failed) {
        this.log.error(
          `Failed to send a tab with FxA commands for ${device.id}.`,
          error
        );
        numFailed++;
      }
    }
    return numFailed < targets.length; // Good enough.
  },

  /**
   * Sends the given tabs to the target devices and, if any send succeeds,
   * shows the "Sent!" confirmation hint anchored to the FxA toolbar button
   * (falling back to the app menu button when it isn't available).
   *
   * @param {object[]} tabsToSend - tabs (as passed to sendTabToDevice) to send.
   * @param {object[]} targets - the devices to send the tabs to.
   * @returns {Promise<boolean[]>} the per-tab send results.
   */
  async sendTabsAndConfirm(tabsToSend, targets) {
    let results = await Promise.all(
      // sendTabToDevice does not reject.
      tabsToSend.map(t => this.sendTabToDevice(t, targets))
    );
    // Show the Sent! confirmation if any of the sends succeeded.
    if (results.includes(true)) {
      // FxA button could be hidden with CSS since the user is logged out,
      // although it seems likely this would only happen in testing...
      let fxastatus = document.documentElement.getAttribute("fxastatus");
      let anchorNode =
        (fxastatus &&
          fxastatus != "not_configured" &&
          document.getElementById("fxa-toolbar-menu-button")?.parentNode?.id !=
            "widget-overflow-list" &&
          document.getElementById("fxa-toolbar-menu-button")) ||
        document.getElementById("PanelUI-menu-button");
      ConfirmationHint.show(anchorNode, "confirmation-hint-send-to-device");
    }
    fxAccounts.flushLogFile();
    return results;
  },

  populateSendTabToDevicesMenu(devicesPopup, uri, title, options = {}) {
    const {
      multiselected = false,
      createDeviceNodeFn = (targetId, name) => {
        let eltName = name ? "menuitem" : "menuseparator";
        return document.createXULElement(eltName);
      },
      isFxaMenu = false,
      contextMenuType = null,
    } = options;
    uri = BrowserUtils.getShareableURL(uri);
    if (!uri) {
      // log an error as everyone should have already checked this.
      this.log.error("Ignoring request to share a non-sharable URL");
      return;
    }

    // remove existing menu items
    for (let i = devicesPopup.children.length - 1; i >= 0; --i) {
      let child = devicesPopup.children[i];
      if (child.classList.contains("sync-menuitem")) {
        child.remove();
      }
    }

    if (gSync.sendTabConfiguredAndLoading) {
      // We can only be in this case in the page action menu.
      return;
    }

    const fragment = document.createDocumentFragment();

    const state = UIState.get();
    if (this.isSignedInWithSyncDisabled) {
      this._appendSignedInSyncDisabled(
        fragment,
        createDeviceNodeFn,
        contextMenuType
      );
    } else if (state.status == UIState.STATUS_SIGNED_IN) {
      const targets = this.getSendTabTargets();
      if (targets.length) {
        this._appendSendTabDeviceList(
          targets,
          fragment,
          createDeviceNodeFn,
          uri.spec,
          title,
          multiselected,
          isFxaMenu,
          contextMenuType
        );

        if (contextMenuType) {
          this._recordSendTabTelemetry(
            "send_tab_opened",
            targets.length,
            contextMenuType
          );
        }
      } else {
        this._appendSendTabSingleDevice(
          fragment,
          createDeviceNodeFn,
          contextMenuType
        );
      }
    } else if (
      state.status == UIState.STATUS_NOT_VERIFIED ||
      state.status == UIState.STATUS_LOGIN_FAILED
    ) {
      this._appendSendTabVerify(fragment, createDeviceNodeFn);
    } else {
      this._appendSendTabSignedOut(
        fragment,
        createDeviceNodeFn,
        contextMenuType
      );
    }

    devicesPopup.appendChild(fragment);

    // Refresh the FxA devices list.  This won't affect the current menu items,
    // but it helps ensure the menu is up-to-date the next time the menu loads.
    // This can help the user get unstuck when the device list is stale (#1664954)
    this.refreshFxaDevices();
  },

  _appendSendTabDeviceList(
    targets,
    fragment,
    createDeviceNodeFn,
    url,
    title,
    multiselected,
    isFxaMenu = false,
    contextMenuType = null
  ) {
    let isPrivate = PrivateBrowsingUtils.isBrowserPrivate(gBrowser);
    let tabsToSend = multiselected
      ? gBrowser.selectedTabs.map(t => {
          return {
            url: t.linkedBrowser.currentURI.spec,
            title: t.linkedBrowser.contentTitle,
            private: isPrivate,
          };
        })
      : [{ url, title, private: isPrivate }];

    const send = to => this.sendTabsAndConfirm(tabsToSend, to);
    const onSendAllCommand = () => {
      send(targets);
      if (contextMenuType) {
        this._recordSendTabTelemetry(
          "click_send_tab",
          targets.length,
          contextMenuType,
          "all_devices"
        );
      }
    };
    const onTargetDeviceCommand = event => {
      const targetId = event.target.getAttribute("clientId");
      const target = targets.find(t => t.id == targetId);
      send([target]);
      if (contextMenuType) {
        this._recordSendTabTelemetry(
          "click_send_tab",
          targets.length,
          contextMenuType,
          "device"
        );
      }
    };

    function addTargetDevice(targetId, name, targetType, lastModified) {
      const targetDevice = createDeviceNodeFn(
        targetId,
        name,
        targetType,
        lastModified
      );
      targetDevice.addEventListener(
        "command",
        targetId ? onTargetDeviceCommand : onSendAllCommand,
        true
      );
      targetDevice.classList.add("sync-menuitem", "sendtab-target");
      targetDevice.setAttribute("clientId", targetId);
      targetDevice.setAttribute("clientType", targetType);
      targetDevice.setAttribute("label", name);
      fragment.appendChild(targetDevice);
    }

    for (let target of targets) {
      let type, lastModified;
      if (target.clientRecord) {
        type = Weave.Service.clientsEngine.getClientType(
          target.clientRecord.id
        );
        lastModified = new Date(target.clientRecord.serverLastModified * 1000);
      } else {
        // For phones, FxA uses "mobile" and Sync clients uses "phone".
        type = target.type == "mobile" ? "phone" : target.type;
        lastModified = target.lastAccessTime
          ? new Date(target.lastAccessTime)
          : null;
      }
      addTargetDevice(target.id, target.name, type, lastModified);
    }

    if (targets.length > 1) {
      // "Send to All Devices" menu item
      const separator = createDeviceNodeFn();
      separator.classList.add("sync-menuitem");
      fragment.appendChild(separator);
      const [allDevicesLabel, manageDevicesLabel] =
        this.fluentStrings.formatValuesSync(
          isFxaMenu
            ? ["account-send-to-all-devices", "account-manage-devices"]
            : [
                "account-send-to-all-devices-titlecase",
                "account-manage-devices-titlecase",
              ]
        );
      addTargetDevice("", allDevicesLabel, "");

      // "Manage devices" menu item
      // We piggyback on the createDeviceNodeFn implementation,
      // it's a big disgusting.
      const targetDevice = createDeviceNodeFn(
        null,
        manageDevicesLabel,
        null,
        null
      );
      targetDevice.addEventListener(
        "command",
        () => {
          gSync.openDevicesManagementPage("sendtab");
          if (contextMenuType) {
            this._recordSendTabTelemetry(
              "click_send_tab",
              targets.length,
              contextMenuType,
              "manage_devices"
            );
          }
        },
        true
      );
      targetDevice.classList.add("sync-menuitem", "sendtab-target");
      targetDevice.setAttribute("label", manageDevicesLabel);
      fragment.appendChild(targetDevice);
    }
  },

  _resetSendTabExposureTracking() {
    this._sendTabExposureRecorded.clear();
  },

  _recordSendTabTelemetry(eventType, deviceCount, contextType, action = null) {
    const extraParams = {
      device_count: String(deviceCount),
    };

    if (action) {
      extraParams.action = action;
    }

    // Map context types to Glean categories
    const categoryMap = {
      tab: "tabContextMenu",
      page: "pageContextMenu",
      link: "pageContextMenu",
      toolbar: "sendTabToolbar",
    };

    // Map event types to method names
    const methodMap = {
      send_tab_exposed: "sendTabExposed",
      send_tab_opened: "sendTabOpened",
      click_send_tab: "clickSendTab",
    };

    const category = categoryMap[contextType];
    const method = methodMap[eventType];

    if (
      !category ||
      !method ||
      (category == "sendTabToolbar" && method == "sendTabExposed")
    ) {
      this.log.error(
        `Invalid telemetry parameters: eventType=${eventType}, contextType=${contextType}`
      );
      return;
    }

    // Add context_type for page/link context menus
    if (contextType === "page" || contextType === "link") {
      extraParams.context_type = contextType;
    }

    Glean[category][method].record(extraParams);
  },

  _appendSignedInSyncDisabled(fragment, createDeviceNodeFn, contextMenuType) {
    let enableSyncLabel;
    if (contextMenuType == "link") {
      enableSyncLabel = this.fluentStrings.formatValueSync(
        "main-context-menu-send-to-mobile-enable-sync-from-link"
      );
    } else if (contextMenuType == "page") {
      enableSyncLabel = this.fluentStrings.formatValueSync(
        "main-context-menu-send-to-mobile-enable-sync-from-page"
      );
    } else {
      enableSyncLabel = this.fluentStrings.formatValueSync(
        "main-context-menu-send-to-mobile-enable-sync3"
      );
    }

    const enableSyncMenuItem = createDeviceNodeFn(null, enableSyncLabel, null);
    enableSyncMenuItem.setAttribute("label", enableSyncLabel);
    enableSyncMenuItem.classList.add("sync-menuitem");
    enableSyncMenuItem.addEventListener(
      "command",
      () => this.enableSync(),
      true
    );
    fragment.appendChild(enableSyncMenuItem);
  },

  _appendSendTabSingleDevice(fragment, createDeviceNodeFn, contextMenuType) {
    let entryPoint = {
      toolbar: "send-tab-toolbar-icon",
      link: "send-tab-link-context-menu",
      page: "send-tab-page-context-menu",
      tab: "send-tab-tab-context-menu",
    }[contextMenuType];

    let connectPhoneLabel, deviceMissingLabel;
    if (contextMenuType == "link") {
      [connectPhoneLabel, deviceMissingLabel] =
        this.fluentStrings.formatValuesSync([
          "main-context-menu-send-to-mobile-connect-phone-from-link",
          "main-context-menu-send-to-mobile-device-missing2",
        ]);
    } else if (contextMenuType == "page") {
      [connectPhoneLabel, deviceMissingLabel] =
        this.fluentStrings.formatValuesSync([
          "main-context-menu-send-to-mobile-connect-phone-from-page",
          "main-context-menu-send-to-mobile-device-missing2",
        ]);
    } else {
      [connectPhoneLabel, deviceMissingLabel] =
        this.fluentStrings.formatValuesSync([
          "main-context-menu-send-to-mobile-connect-phone3",
          "main-context-menu-send-to-mobile-device-missing2",
        ]);
    }

    const connectPhoneMenuItem = createDeviceNodeFn(
      null,
      connectPhoneLabel,
      null
    );
    connectPhoneMenuItem.setAttribute("label", connectPhoneLabel);
    connectPhoneMenuItem.classList.add("sync-menuitem");
    connectPhoneMenuItem.addEventListener(
      "command",
      async () => {
        const uri = await FxAccounts.config.promisePairingURI({
          entrypoint: entryPoint,
        });
        switchToTabHavingURI(uri, true, {});
      },
      true
    );
    fragment.appendChild(connectPhoneMenuItem);

    const separator = createDeviceNodeFn(null, null, null);
    separator.classList.add("sync-menuitem");
    fragment.appendChild(separator);

    const deviceMissingMenuItem = createDeviceNodeFn(
      null,
      deviceMissingLabel,
      null
    );
    deviceMissingMenuItem.setAttribute("label", deviceMissingLabel);
    deviceMissingMenuItem.classList.add("sync-menuitem");
    deviceMissingMenuItem.addEventListener(
      "command",
      () => this.openSendTabHelp(),
      true
    );
    fragment.appendChild(deviceMissingMenuItem);
  },

  _appendSendTabVerify(fragment, createDeviceNodeFn) {
    const [notVerified, verifyAccount] = this.fluentStrings.formatValuesSync([
      "account-send-tab-to-device-verify-status",
      "account-send-tab-to-device-verify2",
    ]);
    const actions = [
      { label: verifyAccount, command: () => this.openPrefs("sendtab") },
    ];
    this._appendSendTabInfoItems(
      fragment,
      createDeviceNodeFn,
      notVerified,
      actions
    );
  },

  _appendSendTabInfoItems(fragment, createDeviceNodeFn, statusLabel, actions) {
    const status = createDeviceNodeFn(null, statusLabel, null);
    status.setAttribute("label", statusLabel);
    status.setAttribute("disabled", true);
    status.classList.add("sync-menuitem");
    fragment.appendChild(status);

    const separator = createDeviceNodeFn(null, null, null);
    separator.classList.add("sync-menuitem");
    fragment.appendChild(separator);

    for (let { label, command } of actions) {
      const actionItem = createDeviceNodeFn(null, label, null);
      actionItem.addEventListener("command", command, true);
      actionItem.classList.add("sync-menuitem");
      actionItem.setAttribute("label", label);
      fragment.appendChild(actionItem);
    }
  },

  _appendSendTabSignedOut(fragment, createDeviceNodeFn, contextMenuType) {
    let entryPoint = {
      toolbar: "send-tab-toolbar-icon",
      link: "send-tab-link-context-menu",
      page: "send-tab-page-context-menu",
      tab: "send-tab-tab-context-menu",
    }[contextMenuType];

    let signInLabel;
    if (contextMenuType == "link") {
      signInLabel = this.fluentStrings.formatValueSync(
        "main-context-menu-send-to-mobile-sign-in-from-link"
      );
    } else if (contextMenuType == "page") {
      signInLabel = this.fluentStrings.formatValueSync(
        "main-context-menu-send-to-mobile-sign-in-from-page"
      );
    } else {
      signInLabel = this.fluentStrings.formatValueSync(
        "main-context-menu-send-to-mobile-sign-in"
      );
    }

    const signInMenuItem = createDeviceNodeFn(null, signInLabel, null);
    signInMenuItem.setAttribute("label", signInLabel);
    signInMenuItem.classList.add("sync-menuitem");
    signInMenuItem.addEventListener(
      "command",
      async () => await this.openSignInAgainPage(entryPoint),
      true
    );
    fragment.appendChild(signInMenuItem);
  },

  // "Send Tab to Device" menu item
  updateTabContextMenu(aPopupMenu, aTargetTab) {
    // We may get here before initialisation. This situation
    // can lead to a empty label for 'Send To Device' Menu.
    this.init();

    if (!this.FXA_ENABLED) {
      // These items are hidden in onFxaDisabled(). No need to do anything.
      return;
    }
    let hasASendableURI = false;
    for (let tab of aTargetTab.multiselected
      ? gBrowser.selectedTabs
      : [aTargetTab]) {
      if (BrowserUtils.getShareableURL(tab.linkedBrowser.currentURI)) {
        hasASendableURI = true;
        break;
      }
    }
    const enabled = !this.sendTabConfiguredAndLoading && hasASendableURI;
    const hideItems = this.shouldHideSendContextMenuItems(enabled);

    let sendTabsToDevice = document.getElementById("context_sendTabToDevice");
    sendTabsToDevice.disabled = !enabled;
    let sendTabToDeviceSeparator = document.getElementById(
      "context_sendTabToDeviceSeparator"
    );

    if (hideItems || !hasASendableURI) {
      sendTabsToDevice.hidden = true;
      sendTabToDeviceSeparator.hidden = true;
    } else {
      if (this.hasOnlyMobileSendTabTargets()) {
        sendTabsToDevice.setAttribute(
          "data-l10n-id",
          "tab-context-send-to-mobile"
        );
      } else {
        sendTabsToDevice.setAttribute(
          "data-l10n-id",
          "tab-context-send-to-device"
        );
      }
      let tabCount = aTargetTab.multiselected
        ? gBrowser.multiSelectedTabsCount
        : 1;
      sendTabsToDevice.setAttribute(
        "data-l10n-args",
        JSON.stringify({ tabCount })
      );
      sendTabsToDevice.hidden = false;
      sendTabToDeviceSeparator.hidden = false;

      if (enabled) {
        const targets = this.getSendTabTargets();
        const exposureKey = "tab-context";
        if (targets.length && !this._sendTabExposureRecorded.has(exposureKey)) {
          this._recordSendTabTelemetry(
            "send_tab_exposed",
            targets.length,
            "tab"
          );
          this._sendTabExposureRecorded.add(exposureKey);
        }
      }
    }
  },

  // "Send Page to Device" and "Send Link to Device" menu items
  updateContentContextMenu(contextMenu) {
    if (!this.FXA_ENABLED) {
      // These items are hidden by default. No need to do anything.
      return false;
    }
    // showSendLink and showSendPage are mutually exclusive
    const showSendLink =
      contextMenu.onSaveableLink || contextMenu.onPlainTextLink;
    const showSendPage =
      !showSendLink &&
      !(
        contextMenu.isContentSelected ||
        contextMenu.onImage ||
        contextMenu.onCanvas ||
        contextMenu.onVideo ||
        contextMenu.onAudio ||
        contextMenu.onLink ||
        contextMenu.onTextInput
      );

    const targetURI = showSendLink
      ? contextMenu.getLinkURI()
      : contextMenu.browser.currentURI;
    const enabled =
      !this.sendTabConfiguredAndLoading &&
      BrowserUtils.getShareableURL(targetURI);
    const hideItems = this.shouldHideSendContextMenuItems(enabled);

    contextMenu.showItem(
      "context-sendpagetodevice",
      !hideItems && showSendPage
    );

    let hasOnlyMobileTargets = this.hasOnlyMobileSendTabTargets();
    let sendLinkToDevice = document.getElementById("context-sendlinktodevice");
    let sendPageToDevice = document.getElementById("context-sendpagetodevice");

    sendLinkToDevice.setAttribute(
      "data-l10n-id",
      hasOnlyMobileTargets
        ? "main-context-menu-link-send-to-mobile"
        : "main-context-menu-link-send-to-device"
    );
    sendPageToDevice.setAttribute(
      "data-l10n-id",
      hasOnlyMobileTargets
        ? "main-context-menu-send-to-mobile-2"
        : "main-context-menu-send-to-device-2"
    );

    for (const id of [
      "context-sendlinktodevice",
      "context-sep-sendlinktodevice",
    ]) {
      contextMenu.showItem(id, !hideItems && showSendLink);
    }

    if (!showSendLink && !showSendPage) {
      return false;
    }

    contextMenu.setItemAttr(
      showSendPage ? "context-sendpagetodevice" : "context-sendlinktodevice",
      "disabled",
      !enabled || null
    );

    if (!hideItems && enabled) {
      const targets = this.getSendTabTargets();
      const exposureKey = showSendLink ? "link-context" : "page-context";
      if (targets.length && !this._sendTabExposureRecorded.has(exposureKey)) {
        this._recordSendTabTelemetry(
          "send_tab_exposed",
          targets.length,
          showSendLink ? "link" : "page"
        );
        this._sendTabExposureRecorded.add(exposureKey);
      }
    }

    // return true if context menu items are visible
    return !hideItems && (showSendPage || showSendLink);
  },

  // Functions called by observers
  onActivityStart() {
    this._isCurrentlySyncing = true;
    clearTimeout(this._syncAnimationTimer);
    this._syncStartTime = Date.now();

    document.querySelectorAll(".syncnow-label").forEach(el => {
      let l10nId = el.getAttribute("syncing-data-l10n-id");
      document.l10n.setAttributes(el, l10nId);
    });

    document.querySelectorAll(".syncNowBtn").forEach(el => {
      el.setAttribute("syncstatus", "active");
    });

    document
      .getElementById("appMenu-viewCache")
      .content.querySelectorAll(".syncNowBtn")
      .forEach(el => {
        el.setAttribute("syncstatus", "active");
      });
  },

  _onActivityStop() {
    this._isCurrentlySyncing = false;
    if (!gBrowser) {
      return;
    }

    document.querySelectorAll(".syncnow-label").forEach(el => {
      let l10nId = el.getAttribute("sync-now-data-l10n-id");
      document.l10n.setAttributes(el, l10nId);
    });

    document.querySelectorAll(".syncNowBtn").forEach(el => {
      el.removeAttribute("syncstatus");
    });

    document
      .getElementById("appMenu-viewCache")
      .content.querySelectorAll(".syncNowBtn")
      .forEach(el => {
        el.removeAttribute("syncstatus");
      });

    Services.obs.notifyObservers(null, "test:browser-sync:activity-stop");
  },

  onActivityStop() {
    let now = Date.now();
    let syncDuration = now - this._syncStartTime;

    if (syncDuration < MIN_STATUS_ANIMATION_DURATION) {
      let animationTime = MIN_STATUS_ANIMATION_DURATION - syncDuration;
      clearTimeout(this._syncAnimationTimer);
      this._syncAnimationTimer = setTimeout(
        () => this._onActivityStop(),
        animationTime
      );
    } else {
      this._onActivityStop();
    }
  },

  // Disconnect from sync, and optionally disconnect from the FxA account.
  // Returns true if the disconnection happened (ie, if the user didn't decline
  // when asked to confirm)
  async disconnect({ confirm = true, disconnectAccount = true } = {}) {
    if (disconnectAccount) {
      let deleteLocalData = false;
      if (confirm) {
        let options = await this._confirmFxaAndSyncDisconnect();
        if (!options.userConfirmedDisconnect) {
          return false;
        }
        deleteLocalData = options.deleteLocalData;
      }
      return this._disconnectFxaAndSync(deleteLocalData);
    }

    if (confirm && !(await this._confirmSyncDisconnect())) {
      return false;
    }
    return this._disconnectSync();
  },

  // Prompt the user to confirm disconnect from FxA and sync with the option
  // to delete syncable data from the device.
  async _confirmFxaAndSyncDisconnect() {
    let options = {
      userConfirmedDisconnect: false,
      deleteLocalData: false,
    };

    const bodyId = AIWindow.hasActiveAIWindows()
      ? "fxa-signout-dialog-body-aiwindow"
      : "fxa-signout-dialog-body";

    let [title, body, button, checkbox] = await document.l10n.formatValues([
      { id: "fxa-signout-dialog-title2" },
      { id: bodyId },
      { id: "fxa-signout-dialog2-button" },
      { id: "fxa-signout-dialog2-checkbox" },
    ]);

    const flags =
      Services.prompt.BUTTON_TITLE_IS_STRING * Services.prompt.BUTTON_POS_0 +
      Services.prompt.BUTTON_TITLE_CANCEL * Services.prompt.BUTTON_POS_1;

    if (!UIState.get().syncEnabled) {
      checkbox = null;
    }

    const result = await Services.prompt.asyncConfirmEx(
      window.browsingContext,
      Services.prompt.MODAL_TYPE_INTERNAL_WINDOW,
      title,
      body,
      flags,
      button,
      null,
      null,
      checkbox,
      false
    );
    const propBag = result.QueryInterface(Ci.nsIPropertyBag2);
    options.userConfirmedDisconnect = propBag.get("buttonNumClicked") == 0;
    options.deleteLocalData = propBag.get("checked");

    return options;
  },

  async _disconnectFxaAndSync(deleteLocalData) {
    const { SyncDisconnect } = ChromeUtils.importESModule(
      "resource://services-sync/SyncDisconnect.sys.mjs"
    );
    // Record telemetry.
    await fxAccounts.telemetry.recordDisconnection(null, "ui");

    await SyncDisconnect.disconnect(deleteLocalData).catch(e => {
      console.error("Failed to disconnect.", e);
    });

    // Clear the attached clients list upon successfully disconnecting
    this._attachedClients = null;

    return true;
  },

  // Prompt the user to confirm disconnect from sync. In this case the data
  // on the device is not deleted.
  async _confirmSyncDisconnect() {
    const [title, body, button] = await document.l10n.formatValues([
      { id: `sync-disconnect-dialog-title2` },
      { id: `sync-disconnect-dialog-body` },
      { id: "sync-disconnect-dialog-button" },
    ]);

    const flags =
      Services.prompt.BUTTON_TITLE_IS_STRING * Services.prompt.BUTTON_POS_0 +
      Services.prompt.BUTTON_TITLE_CANCEL * Services.prompt.BUTTON_POS_1;

    // buttonPressed will be 0 for disconnect, 1 for cancel.
    const buttonPressed = Services.prompt.confirmEx(
      window,
      title,
      body,
      flags,
      button,
      null,
      null,
      null,
      {}
    );
    return buttonPressed == 0;
  },

  async _disconnectSync() {
    await fxAccounts.telemetry.recordDisconnection("sync", "ui");

    await Weave.Service.promiseInitialized;
    await Weave.Service.startOver();

    return true;
  },

  // doSync forces a sync - it *does not* return a promise as it is called
  // via the various UI components.
  doSync() {
    if (!UIState.isReady()) {
      return;
    }
    // Note we don't bother checking if sync is actually enabled - none of the
    // UI which calls this function should be visible in that case.
    const state = UIState.get();
    if (state.status == UIState.STATUS_SIGNED_IN) {
      this.updateSyncStatus({ syncing: true });
      Services.tm.dispatchToMainThread(() => {
        // We are pretty confident that push helps us pick up all FxA commands,
        // but some users might have issues with push, so let's unblock them
        // by fetching the missed FxA commands on manual sync.
        fxAccounts.commands.pollDeviceCommands().catch(e => {
          this.log.error("Fetching missed remote commands failed.", e);
        });
        Weave.Service.sync();
      });
    }
  },

  doSyncFromFxaMenu(sourceElement) {
    this.doSync();
    this.emitFxaToolbarTelemetry("sync_now", sourceElement);
  },

  openPrefs(entryPoint = "syncbutton", origin = undefined, urlParams = {}) {
    window.openPreferences("paneSync", {
      origin,
      urlParams: { ...urlParams, entrypoint: entryPoint },
    });
  },

  openPrefsFromFxaMenu(type, sourceElement) {
    this.emitFxaToolbarTelemetry(type, sourceElement);
    let entryPoint = this._getEntryPointForElement(sourceElement);
    this.openPrefs(entryPoint);
  },

  openChooseWhatToSync(type, sourceElement) {
    this.emitFxaToolbarTelemetry(type, sourceElement);
    let entryPoint = this._getEntryPointForElement(sourceElement);
    this.openPrefs(entryPoint, null, { action: "choose-what-to-sync" });
  },

  openSyncSetup(type, sourceElement, extraParams = {}) {
    this.emitFxaToolbarTelemetry(type, sourceElement);
    const entryPoint = this._getEntryPointForElement(sourceElement);
    return this.openSyncSetupForEntryPoint(entryPoint, extraParams);
  },

  /**
   * Opens the right flow to turn on Sync for the given entry point. Users who
   * already have sync keys go to "Choose what to sync"; passwordless
   * (third-party auth) users go to FxA to create a password, which
   *  allows sync keys to get generated.
   */
  async openSyncSetupForEntryPoint(entryPoint, extraParams = {}) {
    try {
      const hasKeys = await fxAccounts.keys.hasKeysForScope(SCOPE_APP_SYNC);

      if (hasKeys) {
        this.openPrefs(entryPoint, null, { action: "choose-what-to-sync" });
      } else {
        if (!(await FxAccounts.canConnectAccount())) {
          return;
        }
        const url = await FxAccounts.config.promiseSetPasswordURI(
          entryPoint,
          extraParams
        );
        switchToTabHavingURI(url, true, { replaceQueryString: true });
      }
    } catch (err) {
      this.log.error("Failed to determine sync setup flow", err);
      this.openPrefs(entryPoint);
    }
  },

  async signInToSync(sourceElement) {
    const entryPoint =
      this._getEntryPointForElement(sourceElement) === "fxa_app_menu"
        ? "send-tab-app-menu"
        : "send-tab-account-menu";
    var url = await FxAccounts.config.promiseConnectAccountURI(
      "sync",
      entryPoint,
      {}
    );
    switchToTabHavingURI(url, true, {});
  },

  enableSync() {
    openTrustedLinkIn("about:preferences#sync", "tab");
  },

  async openPairDevice(sourceElement) {
    const entryPoint =
      this._getEntryPointForElement(sourceElement) === "fxa_app_menu"
        ? "send-tab-app-menu"
        : "send-tab-account-menu";
    const url = await FxAccounts.config.promisePairingURI({
      entrypoint: entryPoint,
    });
    switchToTabHavingURI(url, true, {});
  },

  async verifyAccount() {
    openTrustedLinkIn("about:preferences#sync", "tab");
  },

  openSendTabHelp() {
    const url = Services.urlFormatter.formatURLPref(
      "identity.sendtab.deviceissues.url"
    );
    switchToTabHavingURI(url, true, { replaceQueryString: true });
  },

  openDeviceMissingHelp() {
    switchToTabHavingURI(
      "https://support.mozilla.org/kb/fxa-managing-devices",
      true,
      { replaceQueryString: true }
    );
  },

  openGetFirefoxMobile() {
    switchToTabHavingURI(
      "https://www.firefox.com/mobile/?utm_medium=firefox-desktop&utm_source=toolbar&utm_campaign=desktop-account-menu",
      true,
      {
        replaceQueryString: true,
      }
    );
  },

  openSyncedTabsPanel() {
    let placement = CustomizableUI.getPlacementOfWidget("sync-button");
    let area = placement?.area;
    let anchor = document.getElementById("sync-button");
    if (area == CustomizableUI.AREA_FIXED_OVERFLOW_PANEL) {
      // The button is in the overflow panel, so we need to show the panel,
      // then show our subview.
      let navbar = document.getElementById(CustomizableUI.AREA_NAVBAR);
      navbar.overflowable.show().then(() => {
        PanelUI.showSubView("PanelUI-remotetabs", anchor);
      }, console.error);
    } else {
      if (
        !anchor?.checkVisibility({ checkVisibilityCSS: true, flush: false })
      ) {
        anchor = document.getElementById("PanelUI-menu-button");
      }
      // It is placed somewhere else - just try and show it.
      PanelUI.showSubView("PanelUI-remotetabs", anchor);
    }
  },

  refreshSyncButtonsTooltip() {
    const state = UIState.get();
    this.updateSyncButtonsTooltip(state);
  },

  /* Update the tooltip for the sync icon in the main menu and in Synced Tabs.
     If Sync is configured, the tooltip is when the last sync occurred,
     otherwise the tooltip reflects the fact that Sync needs to be
     (re-)configured.
  */
  updateSyncButtonsTooltip(state) {
    // Sync buttons are 1/2 Sync related and 1/2 FxA related
    let l10nId, l10nArgs;
    switch (state.status) {
      case UIState.STATUS_NOT_VERIFIED:
        // "needs verification"
        l10nId = "account-verify";
        l10nArgs = { email: state.email };
        break;
      case UIState.STATUS_LOGIN_FAILED:
        // "need to reconnect/re-enter your password"
        l10nId = "account-reconnect";
        l10nArgs = { email: state.email };
        break;
      case UIState.STATUS_NOT_CONFIGURED:
        // Button is not shown in this state
        break;
      default: {
        // Sync appears configured - format the "last synced at" time.
        let lastSyncDate = this.formatLastSyncDate(state.lastSync);
        if (lastSyncDate) {
          l10nId = "appmenu-fxa-last-sync";
          l10nArgs = { time: lastSyncDate };
        }
      }
    }
    const tooltiptext = l10nId
      ? this.fluentStrings.formatValueSync(l10nId, l10nArgs)
      : null;

    let el = PanelMultiView.getViewNode(document, "PanelUI-remotetabs-syncnow");
    if (tooltiptext) {
      el.setAttribute("tooltiptext", tooltiptext);
    } else {
      el.removeAttribute("tooltiptext");
    }
  },

  get relativeTimeFormat() {
    delete this.relativeTimeFormat;
    return (this.relativeTimeFormat = new Services.intl.RelativeTimeFormat(
      undefined,
      { style: "long" }
    ));
  },

  formatLastSyncDate(date) {
    if (!date) {
      // Date can be null before the first sync!
      return null;
    }
    try {
      let adjustedDate = new Date(Date.now() - 1000);
      let relativeDateStr = this.relativeTimeFormat.formatBestUnit(
        date < adjustedDate ? date : adjustedDate
      );
      return relativeDateStr;
    } catch (ex) {
      // shouldn't happen, but one client having an invalid date shouldn't
      // break the entire feature.
      this.log.warn("failed to format lastSync time", date, ex);
      return null;
    }
  },

  onClientsSynced() {
    // Note that this element is only shown if Sync is enabled.
    let element = PanelMultiView.getViewNode(
      document,
      "PanelUI-remotetabs-main"
    );
    if (element) {
      if (Weave.Service.clientsEngine.stats.numClients > 1) {
        element.setAttribute("devices-status", "multi");
      } else {
        element.setAttribute("devices-status", "single");
      }
    }
  },

  onFxaDisabled() {
    document.documentElement.setAttribute("fxadisabled", true);

    const toHide = [...document.querySelectorAll(".sync-ui-item")];
    for (const item of toHide) {
      item.hidden = true;
    }
  },

  /**
   * Checks if the current list of attached clients to the Mozilla account
   * has a service associated with the passed in Id
   *
   *  @param {string} clientId
   *   A known static Id from FxA that identifies the service it's associated with
   *  @returns {boolean}
   *   Returns true/false whether the current account has the associated client
   */
  hasClientForId(clientId) {
    return this._attachedClients?.some(c => !!c.id && c.id === clientId);
  },

  updateCTAPanel(anchor) {
    const mainPanelEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-cta-menu"
    );

    // If we're not in the experiment or in the app menu (hamburger)
    // do not show this CTA panel
    if (
      !this.FXA_CTA_MENU_ENABLED ||
      (anchor &&
        (anchor.id === "appMenu-fxa-label2" ||
          anchor.id === "appMenu-nova-fxa-label"))
    ) {
      // If we've previously shown this but got disabled
      // we should ensure we hide the panel
      mainPanelEl.hidden = true;
      return;
    }

    // Monitor checks
    let monitorPanelEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-monitor-button"
    );
    let monitorEnabled = Services.prefs.getBoolPref(
      "identity.fxaccounts.toolbar.pxiToolbarEnabled.monitorEnabled",
      false
    );
    let monitorInUse =
      this.isSignedIn && this.hasClientForId(FX_MONITOR_OAUTH_CLIENT_ID);
    monitorPanelEl.hidden = !(monitorEnabled || monitorInUse);
    this.updateCTAButtonStrings(monitorPanelEl, {
      inUse: monitorInUse,
      titleId: "appmenuitem-monitor-title2",
      inUseTitleId: "appmenuitem-monitor-title-signed-in",
      descriptionId: "appmenuitem-monitor-description2",
    });

    // Relay checks
    let relayPanelEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-relay-button"
    );
    let relayEnabled =
      BrowserUtils.shouldShowPromo(BrowserUtils.PromoType.RELAY) &&
      Services.prefs.getBoolPref(
        "identity.fxaccounts.toolbar.pxiToolbarEnabled.relayEnabled",
        false
      );
    let relayInUse =
      this.isSignedIn && this.hasClientForId(FX_RELAY_OAUTH_CLIENT_ID);
    relayPanelEl.hidden = !(relayEnabled || relayInUse);
    this.updateCTAButtonStrings(relayPanelEl, {
      inUse: relayInUse,
      titleId: "appmenuitem-relay-title2",
      inUseTitleId: "appmenuitem-relay-title-signed-in",
      descriptionId: "appmenuitem-relay-description2",
    });

    // VPN checks
    let VpnPanelEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-vpn-button"
    );
    let vpnEnabled =
      BrowserUtils.shouldShowPromo(BrowserUtils.PromoType.VPN) &&
      Services.prefs.getBoolPref(
        "identity.fxaccounts.toolbar.pxiToolbarEnabled.vpnEnabled",
        false
      );
    let vpnInUse = this.isSignedIn && this.hasClientForId(VPN_OAUTH_CLIENT_ID);
    VpnPanelEl.hidden = !(vpnEnabled || vpnInUse);
    this.updateCTAButtonStrings(VpnPanelEl, {
      inUse: vpnInUse,
      titleId: "appmenuitem-vpn-title2",
      inUseTitleId: "appmenuitem-vpn-title-signed-in1",
      descriptionId: "appmenuitem-vpn-description5",
    });

    // Share Firefox checks
    let shareFirefoxPanelEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-share-firefox"
    );
    shareFirefoxPanelEl.hidden = !Services.prefs.getBoolPref(
      "browser.referrals.enabled",
      false
    );

    let privacyToolsSeparatorEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-privacy-tools-separator"
    );
    privacyToolsSeparatorEl.hidden = false;

    mainPanelEl.hidden = false;
  },

  /**
   * Updates the title and description of a privacy tools CTA button depending
   * on whether the associated tool is already in use by the signed-in account.
   * When the tool is in use we swap in a shorter, action-oriented title and
   * hide the description.
   *
   * @param {Element} buttonEl
   *   The CTA toolbarbutton to update.
   * @param {object} options
   * @param {boolean} options.inUse
   *   Whether the account has already signed up for this tool.
   * @param {string} options.titleId
   *   Fluent id for the default (promo) title.
   * @param {string} options.inUseTitleId
   *   Fluent id for the title shown when the tool is in use.
   * @param {string} options.descriptionId
   *   Fluent id for the default (promo) description.
   */
  updateCTAButtonStrings(
    buttonEl,
    { inUse, titleId, inUseTitleId, descriptionId }
  ) {
    let titleEl = buttonEl.querySelector(".cta-menu-title");
    let descriptionEl = buttonEl.querySelector(".cta-menu-description");
    document.l10n.setAttributes(titleEl, inUse ? inUseTitleId : titleId);
    descriptionEl.hidden = inUse;
    if (!inUse) {
      document.l10n.setAttributes(descriptionEl, descriptionId);
    }
  },

  openMonitorLink(sourceElement) {
    this.emitFxaToolbarTelemetry("monitor_cta", sourceElement);
    this.openCtaLink(
      FX_MONITOR_OAUTH_CLIENT_ID,
      this._ctaURL(MONITOR_NEW_USER_URL, "new-user-global"),
      this._ctaURL(MONITOR_EXISTING_USER_URL, "existing-user-global")
    );
  },

  openRelayLink(sourceElement) {
    this.emitFxaToolbarTelemetry("relay_cta", sourceElement);
    this.openCtaLink(
      FX_RELAY_OAUTH_CLIENT_ID,
      this._ctaURL(RELAY_NEW_USER_URL, "new-user-global"),
      this._ctaURL(RELAY_EXISTING_USER_URL, "existing-user-global")
    );
  },

  openVPNLink(sourceElement) {
    this.emitFxaToolbarTelemetry("vpn_cta", sourceElement);
    this.openCtaLink(
      VPN_OAUTH_CLIENT_ID,
      this._ctaURL(VPN_NEW_USER_URL, "new-user-global"),
      this._ctaURL(VPN_EXISTING_USER_URL, "existing-user-global")
    );
  },

  openShareFirefoxLink() {
    Referrals.openReferralsTab(window, "accounts_menu");
    PanelUI.hide();
  },

  /**
   * Builds a product CTA URL, attaching the shared campaign params plus a
   * variant-specific utm_content.
   *
   * @param {string} baseUrl
   * @param {string} utmContent
   *   Identifies which CTA variant the user saw, e.g. "new-user-global".
   * @returns {URL}
   */
  _ctaURL(baseUrl, utmContent) {
    const url = new URL(baseUrl);
    for (const [key, value] of Object.entries(FXA_CTA_UTM_PARAMS)) {
      url.searchParams.set(key, value);
    }
    url.searchParams.set("utm_content", utmContent);
    return url;
  },

  /**
   * Opens a product CTA, deep-linking users who already have the service
   * attached to their Mozilla account and sending everyone else to the
   * product's landing page.
   *
   * @param {string} clientId
   *   The FxA OAuth client Id for the product being opened.
   * @param {URL} defaultUrl
   * @param {URL} signedInUrl
   */
  openCtaLink(clientId, defaultUrl, signedInUrl) {
    const url =
      this.isSignedIn && this.hasClientForId(clientId)
        ? signedInUrl
        : defaultUrl;

    this.openLink(url);
    PanelUI.hide();
  },

  /**
   * Returns any experimental copy that we want to try for FxA sign-in CTAs in
   * the event that the user is enrolled in an experiment.
   *
   * The only ctaCopyVariant's that are expected are:
   *
   *  - control
   *  - sync-devices
   *  - backup-data
   *  - backup-sync
   *  - mobile
   *
   * If "control" is set, `null` is returned to indicate default strings,
   * but impressions will still be recorded.
   *
   * @param {NimbusFeature} feature
   *   One of either NimbusFeatures.fxaAppMenuItem or
   *   NimbusFeatures.fxaAvatarMenuItem.
   * @returns {object|string|null}
   *   If feature is NimbusFeatures.fxaAppMenuItem, this will return the Fluent
   *   string ID for the App Menu CTA to appear for users to sign in.
   *
   *   If feature is NimbusFeatures.fxaAvatarMenuItem, this will return an
   *   object with two properties:
   *
   *   headerTitleL10nId (string):
   *     The Fluent ID for the header string for the avatar menu CTA.
   *   headerDescription (string):
   *     The raw string for the description for the avatar menu CTA.
   *
   *   If there is no copy variant being tested, this will return null.
   */
  getMenuCtaCopy(feature) {
    const ctaCopyVariant = feature.getVariable("ctaCopyVariant");
    let headerTitleL10nId;
    let headerDescription;
    switch (ctaCopyVariant) {
      case "sync-devices": {
        if (feature === NimbusFeatures.fxaAppMenuItem) {
          return "fxa-menu-message-sync-devices-collapsed-text";
        }
        headerTitleL10nId = "fxa-menu-message-sync-devices-primary-text";
        headerDescription = this.fluentStrings.formatValueSync(
          "fxa-menu-message-sync-devices-secondary-text"
        );
        break;
      }
      case "backup-data": {
        if (feature === NimbusFeatures.fxaAppMenuItem) {
          return "fxa-menu-message-backup-data-collapsed-text";
        }
        headerTitleL10nId = "fxa-menu-message-backup-data-primary-text";
        headerDescription = this.fluentStrings.formatValueSync(
          "fxa-menu-message-backup-data-secondary-text"
        );
        break;
      }
      case "backup-sync": {
        if (feature === NimbusFeatures.fxaAppMenuItem) {
          return "fxa-menu-message-backup-sync-collapsed-text";
        }
        headerTitleL10nId = "fxa-menu-message-backup-sync-primary-text";
        headerDescription = this.fluentStrings.formatValueSync(
          "fxa-menu-message-backup-sync-secondary-text"
        );
        break;
      }
      case "mobile": {
        if (feature === NimbusFeatures.fxaAppMenuItem) {
          return "fxa-menu-message-mobile-collapsed-text";
        }
        headerTitleL10nId = "fxa-menu-message-mobile-primary-text";
        headerDescription = this.fluentStrings.formatValueSync(
          "fxa-menu-message-mobile-secondary-text"
        );
        break;
      }
      default: {
        return null;
      }
    }

    return { headerTitleL10nId, headerDescription };
  },

  /**
   * Updates the FxA button to show the right avatar variant in the event that
   * this client is not currently signed into an account.
   *
   * @param {string} variant
   *   One of the string constants for the avatarIconVariant variable on the
   *   fxaButtonVisibility feature.
   */
  applyAvatarIconVariant(variant) {
    const ICON_VARIANTS = ["control", "human-circle", "fox-circle"];

    if (!ICON_VARIANTS.includes(variant)) {
      return;
    }

    document.documentElement.setAttribute("fxa-avatar-icon-variant", variant);
  },

  openLink(url) {
    switchToTabHavingURI(url, true, { replaceQueryString: true });
  },

  sendTabToolbarButtonShouldBeEnabled(uri) {
    this.init();

    if (!this.FXA_ENABLED) {
      // Sync is fully disabled and the toolbar button shouldn't even be in the
      // toolbar.
      return false;
    }

    if (this.sendTabConfiguredAndLoading) {
      // Don't enable the button until we are ready to actually send a tab.
      return false;
    }

    return !!BrowserUtils.getShareableURL(uri);
  },

  async populateSendTabToolbarButton(menuPopup) {
    this.populateSendTabToDevicesMenu(
      menuPopup,
      menuPopup.documentGlobal.gBrowser.currentURI,
      menuPopup.documentGlobal.gBrowser.contentTitle,
      { contextMenuType: "toolbar" }
    );
  },

  QueryInterface: ChromeUtils.generateQI([
    "nsIObserver",
    "nsISupportsWeakReference",
  ]),
};
