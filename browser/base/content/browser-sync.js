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
  SyncedTabs: "resource://services-sync/SyncedTabs.sys.mjs",
  SyncedTabsManagement: "resource://services-sync/SyncedTabs.sys.mjs",
  Weave: "resource://services-sync/main.sys.mjs",
});

var { DEVICE_TYPE_MOBILE, DEVICE_TYPE_TABLET } = ChromeUtils.importESModule(
  "resource://services-sync/constants.sys.mjs"
);

const MIN_STATUS_ANIMATION_DURATION = 1600;

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

  _createSyncedTabElement(tabInfo, index, device, canCloseTabs) {
    let tabContainer = document.createXULElement("hbox");
    tabContainer.setAttribute(
      "class",
      "PanelUI-tabitem-container all-tabs-item"
    );

    let item = document.createXULElement("toolbarbutton");
    let tooltipText = (tabInfo.title ? tabInfo.title + "\n" : "") + tabInfo.url;
    item.setAttribute("itemtype", "tab");
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
        "preview/aiWindow.ftl",
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

  get isSignedInWithSyncDisabled() {
    const state = UIState.get();
    return state.status == UIState.STATUS_SIGNED_IN && !state.syncEnabled;
  },

  get hasNoSendTabTargets() {
    return this.getSendTabTargets().length === 0;
  },

  shouldHideSendContextMenuItems(enabled) {
    return (
      !enabled ||
      !this.FXA_ENABLED ||
      UIState.get().status == UIState.STATUS_NOT_VERIFIED
    );
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
    const [headerDesc, headerText] = this.fluentStrings.formatValuesSync([
      "appmenu-fxa-signed-in-label",
      "appmenu-fxa-sync-and-save-data2",
    ]);
    appMenuHeaderDescription.value = headerDesc;
    appMenuHeaderText.textContent = headerText;

    for (let topic of this._obs) {
      Services.obs.addObserver(this, topic, true);
    }

    this.maybeUpdateUIState();

    EnsureFxAccountsWebChannel();

    let fxaPanelView = PanelMultiView.getViewNode(document, "PanelUI-fxa");
    fxaPanelView.addEventListener("ViewShowing", this);
    fxaPanelView.addEventListener("ViewHiding", this);
    fxaPanelView.addEventListener("command", this);
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-syncnow-button"
    ).addEventListener("mouseover", this);
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sendtab-enable-sync-button"
    ).addEventListener("click", this);
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sendtab-connect-phone-button"
    ).addEventListener("click", this);
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sendtab-no-phone-button"
    ).addEventListener("click", this);

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

    let syncNowBtn = panelview.querySelector(".syncnow-label");
    let l10nId = syncNowBtn.getAttribute(
      this._isCurrentlySyncing
        ? "syncing-data-l10n-id"
        : "sync-now-data-l10n-id"
    );
    document.l10n.setAttributes(syncNowBtn, l10nId);

    // This needs to exist because if the user is signed in
    // but the user disabled or disconnected sync we should not show the button
    const syncPrefsButtonEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sync-prefs-button"
    );
    const syncEnabled = UIState.get().syncEnabled;
    syncPrefsButtonEl.hidden = !syncEnabled;
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

    panelview.syncedTabsPanelList = new SyncedTabsPanelList(
      panelview,
      PanelMultiView.getViewNode(document, "PanelUI-fxa-remotetabs-deck"),
      PanelMultiView.getViewNode(document, "PanelUI-fxa-remotetabs-tabslist"),
      PanelMultiView.getViewNode(document, "PanelUI-remote-tabs-separator")
    );

    // Any variant on the CTA will have been applied inside of updateFxAPanel,
    // but now that the panel is showing, we record exposure.
    const ctaCopyVariant =
      NimbusFeatures.fxaAvatarMenuItem.getVariable("ctaCopyVariant");
    if (ctaCopyVariant) {
      NimbusFeatures.fxaAvatarMenuItem.recordExposureEvent();
    }

    // Record Send Tab exposure if the option is visible
    const sendTabButton = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sendtab-button"
    );
    if (sendTabButton && !sendTabButton.hidden) {
      const targets = this.getSendTabTargets();
      this.emitFxaToolbarTelemetry("send_tab_exposed", sendTabButton, {
        device_count: String(targets.length),
      });
    }
  },

  onFxAPanelViewHiding(panelview) {
    MenuMessage.hidePxiMenuMessage(gBrowser.selectedBrowser);
    panelview.syncedTabsPanelList.destroy();
    panelview.syncedTabsPanelList = null;
  },

  onCommand(button) {
    switch (button.id) {
      case "PanelUI-fxa-menu-sync-prefs-button":
        this.openPrefsFromFxaMenu("sync_settings", button);
        break;
      case "PanelUI-fxa-menu-setup-sync-button":
        this.openSyncSetup("sync_settings", button);
        break;

      case "PanelUI-fxa-menu-sendtab-connect-device-button":
      // fall through
      case "PanelUI-fxa-menu-connect-device-button":
        this.clickOpenConnectAnotherDevice(button);
        break;

      case "fxa-manage-account-button":
        this.clickFxAMenuHeaderButton(button);
        break;
      case "PanelUI-fxa-menu-syncnow-button":
        this.doSyncFromFxaMenu(button);
        break;
      case "PanelUI-fxa-menu-sendtab-button":
        this.showSendToDeviceViewFromFxaMenu(button);
        break;
      case "PanelUI-fxa-menu-account-signout-button":
        this.disconnect();
        break;
      case "PanelUI-fxa-menu-monitor-button":
        this.openMonitorLink(button);
        break;
      case "PanelUI-services-menu-relay-button":
      case "PanelUI-fxa-menu-relay-button":
        this.openRelayLink(button);
        break;
      case "PanelUI-fxa-menu-vpn-button":
        this.openVPNLink(button);
        break;
      case "PanelUI-fxa-menu-sendtab-enable-sync-button":
        this.enableSync();
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

  updateSendToDeviceTitle() {
    const tabCount = gBrowser.selectedTab.multiselected
      ? gBrowser.selectedTabs.length
      : 1;
    document.l10n.setArgs(
      PanelMultiView.getViewNode(document, "PanelUI-fxa-menu-sendtab-button"),
      { tabCount }
    );
  },

  showSendToDeviceView(anchor) {
    PanelUI.showSubView("PanelUI-sendTabToDevice", anchor);
    let panelViewNode = document.getElementById("PanelUI-sendTabToDevice");
    this._populateSendTabToDevicesView(panelViewNode);
  },

  showSendToDeviceViewFromFxaMenu(anchor) {
    (async () => {
      const entryPoint =
        this._getEntryPointForElement(anchor) === "fxa_app_menu"
          ? "send-tab-app-menu"
          : "send-tab-account-menu";

      switch (true) {
        case this.isSignedIn === false:
          var url = await FxAccounts.config.promiseConnectAccountURI(
            entryPoint,
            {}
          );
          switchToTabHavingURI(url, true, {});
          return;
        case this.isSignedInWithSyncDisabled:
          PanelUI.showSubView("PanelUI-fxa-menu-sendtab-enable-sync", anchor);
          return;
        case this.hasNoSendTabTargets:
          PanelUI.showSubView("PanelUI-fxa-menu-sendtab-connect-phone", anchor);
          return;
        default:
          this.showSendToDeviceView(anchor);
          // Record that the user opened the Send Tab submenu
          var targets = this.sendTabConfiguredAndLoading
            ? []
            : this.getSendTabTargets();
          this.emitFxaToolbarTelemetry("send_tab_opened", anchor, {
            device_count: String(targets.length),
          });
      }
    })();
  },

  _populateSendTabToDevicesView(panelViewNode, reloadDevices = true) {
    let bodyNode = panelViewNode.querySelector(".panel-subview-body");
    let panelNode = panelViewNode.closest("panel");
    let browser = gBrowser.selectedBrowser;
    let uri = browser.currentURI;
    let title = browser.contentTitle;
    let multiselected = gBrowser.selectedTab.multiselected;

    // This is on top because it also clears the device list between state
    // changes.
    this.populateSendTabToDevicesMenu(bodyNode, uri, title, {
      multiselected,
      createDeviceNodeFn: (clientId, name, clientType, lastModified) => {
        if (!name) {
          return document.createXULElement("toolbarseparator");
        }
        let item = document.createXULElement("toolbarbutton");
        item.setAttribute("wrap", true);
        item.setAttribute("align", "start");
        item.classList.add("sendToDevice-device", "subviewbutton");
        if (clientId) {
          item.classList.add("subviewbutton-iconic");
          if (lastModified) {
            let lastSyncDate = gSync.formatLastSyncDate(lastModified);
            if (lastSyncDate) {
              item.setAttribute(
                "tooltiptext",
                this.fluentStrings.formatValueSync("appmenu-fxa-last-sync", {
                  time: lastSyncDate,
                })
              );
            }
          }
        }

        item.addEventListener("command", () => {
          if (panelNode) {
            PanelMultiView.hidePopup(panelNode);
          }
        });
        return item;
      },
      isFxaMenu: true,
    });

    bodyNode.removeAttribute("state");
    // If the app just started, we won't have fetched the device list yet. Sync
    // does this automatically ~10 sec after startup, but there's no trigger for
    // this if we're signed in to FxA, but not Sync.
    if (gSync.sendTabConfiguredAndLoading) {
      bodyNode.setAttribute("state", "notready");
    }
    if (reloadDevices) {
      // Force a refresh of the fxa device list in case the user connected a new
      // device, and is waiting for it to show up.
      this.refreshFxaDevices().then(_ => {
        if (!window.closed) {
          this._populateSendTabToDevicesView(panelViewNode, false);
        }
      });
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
    const sendTabButton = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sendtab-button"
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
      if (anchor.id == "appMenu-fxa-label2") {
        this.openFxAEmailFirstPageFromFxaMenu(anchor);
        PanelUI.hide();
        return;
      }

      sendTabButton.setAttribute("data-l10n-id", "fxa-menu-send-to-mobile");
      sendTabButton.classList.remove("subviewbutton-nav");

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
      this.enableSendTabIfValidTab();
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

    this.enableSendTabIfValidTab();
    let sendTabTargets = this.getSendTabTargets();

    if (
      !sendTabTargets.length ||
      this.hasOnlyMobileSendTabTargets(sendTabTargets)
    ) {
      sendTabButton.setAttribute("data-l10n-id", "fxa-menu-send-to-mobile");
    } else {
      sendTabButton.setAttribute("data-l10n-id", "fxa-menu-send-to-device");
    }
    sendTabButton.classList.add("subviewbutton-nav");

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

  _shouldShowSyncOffIndicator() {
    // We only ever want to show the user the dot once, once they've clicked into the panel
    // we do not show them the dot anymore
    return !Services.prefs.getBoolPref(
      "identity.fxaccounts.toolbar.syncSetup.panelAccessed",
      false
    );
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
    const cadButtonEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-connect-device-button"
    );
    const syncNowButtonEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-syncnow-button"
    );
    const fxaMenuAccountButtonEl = PanelMultiView.getViewNode(
      document,
      "fxa-manage-account-button"
    );
    const signedInContainer = PanelMultiView.getViewNode(
      document,
      "PanelUI-signedin-panel"
    );
    const signOutSeparator = PanelMultiView.getViewNode(
      document,
      "PanelUI-sign-out-separator"
    );
    const emptyProfilesButton = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-empty-profiles-button"
    );
    const profilesButton = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-profiles-button"
    );
    const profilesSeparator = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-profiles-separator"
    );
    const syncSetupEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-setup-sync-container"
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
    cadButtonEl.setAttribute("disabled", true);
    syncNowButtonEl.hidden = true;
    signedInContainer.hidden = false;
    cadButtonEl.hidden = true;
    fxaMenuAccountButtonEl.classList.remove("subviewbutton-nav");
    fxaMenuAccountButtonEl.removeAttribute("closemenu");
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

        // Reposition profiles elements
        emptyProfilesButton.remove();
        profilesButton.remove();
        profilesSeparator.remove();

        profilesSeparator.hidden = true;

        signedInContainer.after(profilesSeparator);
        signedInContainer.after(profilesButton);
        signedInContainer.after(emptyProfilesButton);

        break;

      case UIState.STATUS_LOGIN_FAILED:
        signOutSeparator.hidden = true;
        stateValue = "login-failed";
        headerTitleL10nId = "account-disconnected2";
        headerDescription = state.displayName || state.email;
        mainWindowEl.style.removeProperty("--avatar-image-url");
        break;

      case UIState.STATUS_NOT_VERIFIED:
        signOutSeparator.hidden = true;
        stateValue = "unverified";
        headerTitleL10nId = "account-finish-account-setup";
        headerDescription = state.displayName || state.email;
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
        cadButtonEl.hidden = false;
        signOutSeparator.hidden = false;
        signedInContainer.hidden = false;
        cadButtonEl.removeAttribute("disabled");

        if (state.syncEnabled) {
          // Always show sync now and connect another device button when sync is enabled
          syncNowButtonEl.removeAttribute("hidden");
          cadButtonEl.removeAttribute("hidden");
          syncSetupEl.setAttribute("hidden", "true");
        } else {
          if (this._shouldShowSyncOffIndicator()) {
            fxaToolbarMenuButton?.setAttribute("badge-status", "sync-disabled");
          }
          syncSetupEl.removeAttribute("hidden");
        }

        if (state.syncEnabled) {
          cadButtonEl.removeAttribute("hidden");
          syncSetupSeparator.removeAttribute("hidden");
        } else {
          cadButtonEl.setAttribute("hidden", "true");
          syncSetupSeparator.setAttribute("hidden", "true");
        }

        // Reposition profiles elements
        emptyProfilesButton.remove();
        profilesButton.remove();
        profilesSeparator.remove();

        profilesSeparator.hidden = false;

        fxaMenuAccountButtonEl.after(profilesSeparator);
        fxaMenuAccountButtonEl.after(profilesButton);
        fxaMenuAccountButtonEl.after(emptyProfilesButton);

        break;

      default:
        headerTitleL10nId = this.FXA_CTA_MENU_ENABLED
          ? "synced-tabs-fxa-sign-in"
          : "appmenuitem-sign-in-account";
        headerDescription = this.fluentStrings.formatValueSync(
          "fxa-menu-turn-on-sync-default"
        );
        break;
    }

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

  enableSendTabIfValidTab() {
    // All tabs selected must be sendable for the Send Tab button to be enabled
    // on the FxA menu.
    let canSendAllURIs = gBrowser.selectedTabs.every(
      t => !!BrowserUtils.getShareableURL(t.linkedBrowser.currentURI)
    );

    for (const id of [
      "PanelUI-fxa-menu-sendtab-button",
      "PanelUI-fxa-menu-sendtab-separator",
    ]) {
      PanelMultiView.getViewNode(document, id).hidden = !canSendAllURIs;
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
    const url = await FxAccounts.config.promiseConnectAccountURI(entryPoint);
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
    const url = await FxAccounts.config.promiseConnectDeviceURI(entryPoint);
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
      this._appendSignedInSyncDisabled(fragment, createDeviceNodeFn);
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

    const send = to => {
      Promise.all(
        tabsToSend.map(t =>
          // sendTabToDevice does not reject.
          this.sendTabToDevice(t, to)
        )
      ).then(results => {
        // Show the Sent! confirmation if any of the sends succeeded.
        if (results.includes(true)) {
          // FxA button could be hidden with CSS since the user is logged out,
          // although it seems likely this would only happen in testing...
          let fxastatus = document.documentElement.getAttribute("fxastatus");
          let anchorNode =
            (fxastatus &&
              fxastatus != "not_configured" &&
              document.getElementById("fxa-toolbar-menu-button")?.parentNode
                ?.id != "widget-overflow-list" &&
              document.getElementById("fxa-toolbar-menu-button")) ||
            document.getElementById("PanelUI-menu-button");
          ConfirmationHint.show(anchorNode, "confirmation-hint-send-to-device");
        }
        fxAccounts.flushLogFile();
      });
    };
    const onSendAllCommand = () => {
      send(targets);
      // Record Send Tab clicked telemetry for "Send to All Devices"
      if (isFxaMenu) {
        const sendTabButton = PanelMultiView.getViewNode(
          document,
          "PanelUI-fxa-menu-sendtab-button"
        );
        this.emitFxaToolbarTelemetry("send_tab", sendTabButton, {
          device_count: String(targets.length),
          action: "all_devices",
        });
      } else if (contextMenuType) {
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
      // Record Send Tab clicked telemetry for specific device
      if (isFxaMenu) {
        const sendTabButton = PanelMultiView.getViewNode(
          document,
          "PanelUI-fxa-menu-sendtab-button"
        );
        this.emitFxaToolbarTelemetry("send_tab", sendTabButton, {
          device_count: String(targets.length),
          action: "device",
        });
      } else if (contextMenuType) {
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
          // Record Send Tab clicked telemetry for "Manage Devices"
          if (isFxaMenu) {
            const sendTabButton = PanelMultiView.getViewNode(
              document,
              "PanelUI-fxa-menu-sendtab-button"
            );
            this.emitFxaToolbarTelemetry("send_tab", sendTabButton, {
              device_count: String(targets.length),
              action: "manage_devices",
            });
          } else if (contextMenuType) {
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

  _appendSignedInSyncDisabled(fragment, createDeviceNodeFn) {
    const enableSyncLabel = this.fluentStrings.formatValueSync(
      "main-context-menu-send-to-mobile-enable-sync2"
    );

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

    const [connectPhoneLabel, deviceMissingLabel] =
      this.fluentStrings.formatValuesSync([
        "main-context-menu-send-to-mobile-connect-phone2",
        "main-context-menu-send-to-mobile-device-missing2",
      ]);

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
      "account-send-tab-to-device-verify",
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
    const signInLabel = this.fluentStrings.formatValueSync(
      "main-context-menu-send-to-mobile-sign-in"
    );
    const signInMenuItem = createDeviceNodeFn(null, signInLabel, null);
    signInMenuItem.setAttribute("label", signInLabel);
    signInMenuItem.classList.add("sync-menuitem");
    signInMenuItem.addEventListener(
      "command",
      async () => await this.openSignInAgainPage(contextMenuType),
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

  /**
   * Opens the appropriate sync setup flow based on whether the user has sync keys.
   * - If the user has sync keys: opens sync preferences to configure what to sync
   * - If the user doesn't have sync keys (third-party auth): opens FxA to create password
   */
  async openSyncSetup(type, sourceElement, extraParams = {}) {
    this.emitFxaToolbarTelemetry(type, sourceElement);
    const entryPoint = this._getEntryPointForElement(sourceElement);

    try {
      // Check if the user has sync keys
      const hasKeys = await fxAccounts.keys.hasKeysForScope(SCOPE_APP_SYNC);

      if (hasKeys) {
        // User has keys - go to prefs to configure what to sync
        this.openPrefs(entryPoint, null, { action: "choose-what-to-sync" });
      } else {
        // User doesn't have keys (third-party auth) - go to FxA to create password
        // This will request SCOPE_APP_SYNC so FxA knows to generate sync keys
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
      // Fall back to opening prefs
      this.openPrefs(entryPoint);
    }
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

  openSendTabHelp() {
    const url = Services.urlFormatter.formatURLPref(
      "identity.sendtab.deviceissues.url"
    );
    switchToTabHavingURI(url, true, { replaceQueryString: true });
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

    let syncNowBtns = [
      "PanelUI-remotetabs-syncnow",
      "PanelUI-fxa-menu-syncnow-button",
    ];
    syncNowBtns.forEach(id => {
      let el = PanelMultiView.getViewNode(document, id);
      if (tooltiptext) {
        el.setAttribute("tooltiptext", tooltiptext);
      } else {
        el.removeAttribute("tooltiptext");
      }
    });
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
      (anchor && anchor.id === "appMenu-fxa-label2")
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
    monitorPanelEl.hidden = !monitorEnabled;

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
    let myServicesRelayPanelEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-services-menu-relay-button"
    );
    let servicesContainerEl = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-services"
    );
    if (this.isSignedIn) {
      const hasRelayClient = this.hasClientForId(FX_RELAY_OAUTH_CLIENT_ID);
      relayPanelEl.hidden = hasRelayClient;
      // Right now only relay is under "my services" so if we don't have, we turn it off
      myServicesRelayPanelEl.hidden = !hasRelayClient;
      servicesContainerEl.hidden = !hasRelayClient;
    } else {
      relayPanelEl.hidden = !relayEnabled;
      // We'll never show my services when signed out
      myServicesRelayPanelEl.hidden = true;
      servicesContainerEl.hidden = true;
    }

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
    VpnPanelEl.hidden = !vpnEnabled;

    // We should only the show the separator if we have at least one CTA enabled
    PanelMultiView.getViewNode(document, "PanelUI-products-separator").hidden =
      !monitorEnabled && !relayEnabled && !vpnEnabled;
    mainPanelEl.hidden = false;
  },

  async openMonitorLink(sourceElement) {
    this.emitFxaToolbarTelemetry("monitor_cta", sourceElement);
    await this.openCtaLink(
      FX_MONITOR_OAUTH_CLIENT_ID,
      new URL("https://monitor.firefox.com"),
      new URL("https://monitor.firefox.com/user/breaches")
    );
  },

  async openRelayLink(sourceElement) {
    this.emitFxaToolbarTelemetry("relay_cta", sourceElement);
    await this.openCtaLink(
      FX_RELAY_OAUTH_CLIENT_ID,
      new URL("https://relay.firefox.com"),
      new URL("https://relay.firefox.com/accounts/profile")
    );
  },

  async openVPNLink(sourceElement) {
    this.emitFxaToolbarTelemetry("vpn_cta", sourceElement);
    await this.openCtaLink(
      VPN_OAUTH_CLIENT_ID,
      new URL("https://www.mozilla.org/en-US/products/vpn/"),
      new URL("https://www.mozilla.org/en-US/products/vpn/")
    );
  },

  // A generic opening based on
  async openCtaLink(clientId, defaultUrl, signedInUrl) {
    const params = {
      utm_medium: "firefox-desktop",
      utm_source: "toolbar",
      utm_campaign: "discovery",
    };
    const searchParams = new URLSearchParams(params);

    if (!this.isSignedIn) {
      // Add the base params + not signed in
      defaultUrl.search = searchParams.toString();
      defaultUrl.searchParams.append("utm_content", "notsignedin");
      this.openLink(defaultUrl);
      PanelUI.hide();
      return;
    }

    const url = this.hasClientForId(clientId) ? signedInUrl : defaultUrl;
    // Add base params + signed in
    url.search = searchParams.toString();
    url.searchParams.append("utm_content", "signedIn");

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
