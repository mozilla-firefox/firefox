/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * SidebarController handles logic such as toggling sidebar panels,
 * dynamically adding menubar menu items for the View -> Sidebar menu,
 * and provides APIs for sidebar extensions, etc.
 */

const { DeferredTask } = ChromeUtils.importESModule(
  "resource://gre/modules/DeferredTask.sys.mjs"
);

const toolsNameMap = {
  viewOpenTabsSidebar: "opentabs",
  viewBookmarksSidebar: "bookmarks",
  viewGenaiChatSidebar: "aichat",
  viewGenaiPageAssistSidebar: "aipageassist",
  viewHistorySidebar: "history",
  viewTabsSidebar: "syncedtabs",
  viewCPMSidebar: "passwords",
  viewResourceMonitorSidebar: "resourcemonitor",
  viewWidgetsSidebar: "widgets",
};
const EXPAND_ON_HOVER_DEBOUNCE_TIMEOUT_MS = 1000;
const LAUNCHER_SPLITTER_WIDTH = 4;

var SidebarController = {
  makeSidebar({ elementId, ...rest }, commandID) {
    const sidebar = {
      get sourceL10nEl() {
        return document.getElementById(elementId);
      },
      get title() {
        let element = document.getElementById(elementId);
        return element?.getAttribute("label");
      },
      ...rest,
    };

    const toolID = toolsNameMap[commandID];
    if (toolID) {
      XPCOMUtils.defineLazyPreferenceGetter(
        sidebar,
        "attention",
        `sidebar.notification.badge.${toolID}`,
        false,
        (_pref, _prev) => this.handleToolBadges(toolID)
      );
      sidebar.attention;
    }

    return sidebar;
  },

  registerPrefSidebar(pref, commandID, config) {
    const sidebar = this.makeSidebar(config, commandID);
    this._sidebars.set(commandID, sidebar);

    let switcherMenuitem;
    sidebar._updateMenus = () => {
      // The pref observer outlives the window.
      if (this.uninitializing) {
        return;
      }
      const { visible } = sidebar;
      // Hide the sidebar if it is open and should not be visible,
      // and unset the current command and lastOpenedId so they do not
      // re-open the next time the sidebar does.
      if (!visible && this._state.command == commandID) {
        this._state.command = "";
        this.lastOpenedId = null;
        this.hide();
      }

      // Update visibility of View -> Sidebar menu item.
      const viewItem = document.getElementById(sidebar.menuId);
      if (viewItem) {
        viewItem.hidden = !visible;
      }

      let menuItem = document.getElementById(config.elementId);
      // Add/remove switcher menu item.
      if (visible && !menuItem) {
        switcherMenuitem = this.createMenuItem(commandID, sidebar);
        switcherMenuitem.setAttribute("id", config.elementId);
        switcherMenuitem.removeAttribute("type");
        const separator = this._switcherPanel.querySelector("menuseparator");
        separator.parentNode.insertBefore(switcherMenuitem, separator);
      } else {
        switcherMenuitem?.remove();
      }

      window.dispatchEvent(new CustomEvent("SidebarItemChanged"));
    };

    // Detect pref changes and handle initial state.
    XPCOMUtils.defineLazyPreferenceGetter(
      sidebar,
      "_prefVisible",
      pref,
      false,
      () => sidebar._updateMenus?.()
    );
    Object.defineProperty(sidebar, "visible", {
      get: () =>
        sidebar._prefVisible && !(config.hideInAIWindow && this.isAIWindow()),
      configurable: true,
    });
    this.promiseInitialized.then(() => sidebar._updateMenus?.());
  },

  isAIWindow() {
    return this.AIWindow.isAIWindowActive(window);
  },

  get sidebars() {
    if (this._sidebars) {
      return this._sidebars;
    }

    return this.generateSidebarsMap();
  },

  generateSidebarsMap() {
    this._sidebars = new Map([
      [
        "viewHistorySidebar",
        this.makeSidebar({
          name: "history",
          elementId: "sidebar-switcher-history",
          url: this.sidebarRevampEnabled
            ? "chrome://browser/content/sidebar/sidebar-history.html"
            : "chrome://browser/content/places/historySidebar.xhtml",
          menuId: "menu_historySidebar",
          triggerButtonId: "appMenuViewHistorySidebar",
          keyId: "key_gotoHistory",
          menuL10nId: "menu-view-history-button",
          revampL10nId: "sidebar-menu-history-label",
          iconUrl: "chrome://browser/skin/history.svg",
          contextMenuId: this.sidebarRevampEnabled
            ? "sidebar-history-context-menu"
            : undefined,
          gleanEvent: Glean.history.sidebarToggle,
          gleanClickEvent: Glean.sidebar.historyIconClick,
          recordSidebarVersion: true,
        }),
      ],
      [
        "viewTabsSidebar",
        this.makeSidebar({
          name: "syncedtabs",
          elementId: "sidebar-switcher-tabs",
          url: this.sidebarRevampEnabled
            ? "chrome://browser/content/sidebar/sidebar-syncedtabs.html"
            : "chrome://browser/content/syncedtabs/sidebar.html",
          menuId: "menu_tabsSidebar",
          classAttribute: "sync-ui-item",
          menuL10nId: "menu-view-synced-tabs-sidebar",
          revampL10nId: "sidebar-menu-synced-tabs-label",
          iconUrl: "chrome://browser/skin/synced-tabs.svg",
          contextMenuId: this.sidebarRevampEnabled
            ? "sidebar-synced-tabs-context-menu"
            : undefined,
          gleanClickEvent: Glean.sidebar.syncedTabsIconClick,
        }),
      ],
      [
        "viewBookmarksSidebar",
        this.makeSidebar({
          name: "bookmarks",
          elementId: "sidebar-switcher-bookmarks",
          url:
            this.sidebarRevampEnabled && this.updatedBookmarksPanelEnabled
              ? "chrome://browser/content/sidebar/sidebar-bookmarks.html"
              : "chrome://browser/content/places/bookmarksSidebar.xhtml",
          menuId: "menu_bookmarksSidebar",
          keyId: "viewBookmarksSidebarKb",
          menuL10nId: "menu-view-bookmarks",
          revampL10nId: "sidebar-menu-bookmarks-label",
          iconUrl: "chrome://browser/skin/bookmark-hollow.svg",
          contextMenuId: this.sidebarRevampEnabled
            ? "sidebar-bookmarks-context-menu"
            : undefined,
          disabled: true,
          gleanEvent: Glean.bookmarks.sidebarToggle,
          gleanClickEvent: Glean.sidebar.bookmarksIconClick,
          recordSidebarVersion: true,
        }),
      ],
    ]);

    this.registerPrefSidebar(
      "browser.ml.chat.enabled",
      "viewGenaiChatSidebar",
      {
        name: "aichat",
        elementId: "sidebar-switcher-genai-chat",
        url: "chrome://browser/content/genai/chat.html",
        keyId: "viewGenaiChatSidebarKb",
        menuId: "menu_genaiChatSidebar",
        menuL10nId: "menu-view-genai-chat",
        // Bug 1900915 to expose as conditional tool
        revampL10nId: "sidebar-menu-genai-chat-label",
        iconUrl: "chrome://global/skin/icons/highlights.svg",
        gleanClickEvent: Glean.sidebar.chatbotIconClick,
        toolContextMenuId: "aichat",
        hasCustomHeader: true,
        permissions: true,
        hideInAIWindow: true,
      }
    );

    this.registerPrefSidebar(
      "browser.ml.pageAssist.enabled",
      "viewGenaiPageAssistSidebar",
      {
        name: "aipageassist",
        elementId: "sidebar-switcher-genai-page-assist",
        url: "chrome://browser/content/genai/pageAssist.html",
        menuId: "menu_genaiPageAssistSidebar",
        menuL10nId: "menu-view-genai-page-assist",
        revampL10nId: "sidebar-menu-genai-page-assist-label",
        iconUrl: "chrome://browser/skin/reader-mode.svg",
      }
    );

    this.registerPrefSidebar(
      "browser.contextual-password-manager.enabled",
      "viewCPMSidebar",
      {
        name: "passwords",
        elementId: "sidebar-switcher-megalist",
        url: "chrome://global/content/megalist/megalist.html",
        menuId: "menu_megalistSidebar",
        menuL10nId: "menu-view-contextual-password-manager",
        revampL10nId: "sidebar-menu-contextual-password-manager-label",
        iconUrl: "chrome://browser/skin/login.svg",
        gleanEvent: Glean.contextualManager.sidebarToggle,
        gleanClickEvent: Glean.sidebar.passwordsIconClick,
        recordSidebarVersion: true,
      }
    );

    this.registerPrefSidebar(
      "browser.resourceMonitor.enabled",
      "viewResourceMonitorSidebar",
      {
        name: "resourcemonitor",
        elementId: "sidebar-switcher-resourcemonitor",
        url: "about:processes?groupby=tab",
        menuId: "menu_resourceMonitorSidebar",
        menuL10nId: "menu-view-resource-monitor",
        revampL10nId: "sidebar-menu-resource-monitor-label",
        iconUrl: "chrome://browser/skin/lightning-bolt.svg",
        appendOpenType: true,
      }
    );

    if (this.sidebarRevampEnabled) {
      this.registerPrefSidebar(
        "sidebar.openTabsPanel.enabled",
        "viewOpenTabsSidebar",
        {
          name: "opentabs",
          elementId: "sidebar-switcher-opentabs",
          url: "chrome://browser/content/sidebar/sidebar-opentabs.html",
          menuId: "menu_openTabsSidebar",
          keyId: "viewOpenTabsSidebarKb",
          menuL10nId: "menu-view-open-tabs",
          revampL10nId: "sidebar-menu-open-tabs-label",
          iconUrl: "chrome://browser/skin/open-tabs.svg",
          gleanClickEvent: Glean.sidebar.openTabsIconClick,
          contextMenuId: "sidebar-opentabs-context-menu",
        }
      );
      this.registerPrefSidebar(
        "browser.widgets.generated.enabled",
        "viewWidgetsSidebar",
        {
          name: "widgets",
          elementId: "sidebar-switcher-widgets",
          url: "chrome://browser/content/widgets/sidebar-widget-studio.html",
          menuId: "menu_widgetsSidebar",
          menuL10nId: "menu-view-widget-studio",
          revampL10nId: "sidebar-menu-widget-studio-label",
          iconUrl: "chrome://browser/skin/topsites.svg",
          hasCustomHeader: true,
        }
      );
    }

    if (this.sidebarRevampEnabled) {
      this._sidebars.set("viewCustomizeSidebar", {
        url: "chrome://browser/content/sidebar/sidebar-customize.html",
        revampL10nId: "sidebar-menu-customize-label",
        iconUrl: "chrome://global/skin/icons/settings.svg",
        gleanEvent: Glean.sidebarCustomize.panelToggle,
      });
    }

    return this._sidebars;
  },

  /**
   * Returns a map of tools and extensions for use in the sidebar
   */
  get toolsAndExtensions() {
    if (this._toolsAndExtensions) {
      return this._toolsAndExtensions;
    }

    this._toolsAndExtensions = new Map();
    this.getTools().forEach(tool => {
      this._toolsAndExtensions.set(tool.commandID, tool);
    });
    this.getExtensions().forEach(extension => {
      this._toolsAndExtensions.set(extension.commandID, extension);
    });
    return this._toolsAndExtensions;
  },

  // Avoid getting the browser element from init() to avoid triggering the
  // <browser> constructor during startup if the sidebar is hidden.
  get browser() {
    if (this._browser) {
      return this._browser;
    }
    return (this._browser = document.getElementById("sidebar"));
  },
  POSITION_START_PREF: "sidebar.position_start",
  DEFAULT_SIDEBAR_ID: "viewBookmarksSidebar",
  VISIBILITY_PREF: "sidebar.visibility",
  TOOLS_PREF: "sidebar.main.tools",
  INSTALLED_EXTENSIONS: "sidebar.installed.extensions",

  // lastOpenedId is set in show() but unlike currentID it's not cleared out on hide
  // and isn't persisted across windows
  lastOpenedId: null,

  _box: null,
  _pinnedTabsContainerNode: null,
  _pinnedTabsItemsWrapperNode: null,
  _pinnedTabsSplitterNode: null,

  /**
   * The pinned tabs container.
   *
   * @returns {Element}
   */
  get _pinnedTabsContainer() {
    this._pinnedTabsContainerNode ??= document.getElementById(
      "pinned-tabs-container"
    );
    return this._pinnedTabsContainerNode;
  },

  /**
   * The items wrapper which holds the collection of pinned tabs.
   *
   * @returns {Element}
   */
  get _pinnedTabsItemsWrapper() {
    this._pinnedTabsItemsWrapperNode ??=
      this._pinnedTabsContainer.shadowRoot.querySelector(
        "[part=items-wrapper]"
      );
    return this._pinnedTabsItemsWrapperNode;
  },

  /**
   * The splitter that separates pinned + unpinned tabs in vertical mode.
   *
   * @returns {Element}
   */
  get _pinnedTabsSplitter() {
    this._pinnedTabsSplitterNode ??= document.getElementById(
      "vertical-pinned-tabs-splitter"
    );
    return this._pinnedTabsSplitterNode;
  },

  // The constructor of this label accesses the browser element due to the
  // control="sidebar" attribute, so avoid getting this label during startup.
  get _title() {
    if (this.__title) {
      return this.__title;
    }
    return (this.__title = document.getElementById("sidebar-title"));
  },
  _splitter: null,
  _reversePositionButton: null,
  _switcherPanel: null,
  _switcherTarget: null,
  _switcherArrow: null,
  _inited: false,
  _uninitializing: false,
  _switcherListenersAdded: false,
  _verticalNewTabListenerAdded: false,
  _localesObserverAdded: false,
  _mainResizeObserverAdded: false,
  _aiWindowObserverAdded: false,
  _windowRestoredObserverAdded: false,
  _mainResizeObserver: null,
  _activeViewTransition: null,
  _viewTransitionPromise: null,
  _inViewTransitionUpdate: false,
  _collapsedWidthMeasurementID: 0,
  _expandOnHoverToggleID: 0,

  /**
   * @type {MutationObserver | null}
   */
  _observer: null,

  _initDeferred: Promise.withResolvers(),

  _initialUIStateUpdated: false,

  /**
   * Latched true synchronously by SessionStore.restoreSidebar before it
   * kicks off updateUIState. Tells startDelayedLoad that session restore is
   * providing state for this window and it should wait for the per-window
   * sessionstore-single-window-restored notification instead of loading
   * backup state. Once set, stays set for the life of this controller.
   */
  _sessionRestoreStateReceived: false,

  markSessionRestoreStateReceived() {
    this._sessionRestoreStateReceived = true;
  },

  // A promise resolved once the Sidebar has rendered its initial state.
  get promiseInitialized() {
    return this._initDeferred.promise;
  },

  get initialized() {
    return this._inited;
  },

  get uninitializing() {
    return this._uninitializing;
  },

  get inSingleTabWindow() {
    return (
      !window.toolbar.visible ||
      window.document.documentElement.hasAttribute("taskbartab") ||
      window.document.documentElement.hasAttribute("mini-window")
    );
  },

  get sidebarContainer() {
    if (!this._sidebarContainer) {
      // This is the *parent* of the `sidebar-main` component. Its ID is "sidebar-container
      this._sidebarContainer = document.getElementById("sidebar-container");
    }
    return this._sidebarContainer;
  },

  get sidebarMain() {
    if (!this._sidebarMain) {
      this._sidebarMain = document.querySelector("sidebar-main");
    }
    return this._sidebarMain;
  },

  get contentArea() {
    if (!this._contentArea) {
      this._contentArea = document.getElementById("tabbrowser-tabbox");
    }
    return this._contentArea;
  },

  get toolbarButton() {
    if (!this._toolbarButton) {
      this._toolbarButton = document.getElementById("sidebar-button");
    }
    return this._toolbarButton;
  },

  get isLauncherDragging() {
    return this._launcherSplitter.getAttribute("state") === "dragging";
  },

  get isPinnedTabsDragging() {
    return this._pinnedTabsSplitter.getAttribute("state") === "dragging";
  },

  get sidebarTools() {
    return this.sidebarRevampTools ? this.sidebarRevampTools.split(",") : [];
  },

  get sidebarExtensions() {
    return this.installedExtensions ? this.installedExtensions.split(",") : [];
  },

  get launcherSplitter() {
    return this._launcherSplitter;
  },

  /**
   * The sidebar header, which also functions as a switcher when the launcher
   * is hidden. (Revamp only)
   *
   * @return {SidebarPanelHeader}
   */
  get panelHeader() {
    return (this._panelHeader ??= document.getElementById(
      "sidebar-panel-header"
    ));
  },

  init() {
    // Initialize global state manager.
    this.SidebarManager;

    // Initialize per-window state manager.
    if (!this._state) {
      this._state = new this.SidebarState(this);
    }

    // Watch for the nav toolbox being hidden in fullscreen (both DOM fullscreen
    // and F11 autohide set fullscreenNavToolboxHidden) to sync the sidebar.
    this._fullscreenObserver = new MutationObserver(() => {
      this._state.navToolboxCollapsed = document.documentElement.hasAttribute(
        "fullscreenNavToolboxHidden"
      );
    });

    this._fullscreenObserver.observe(document.documentElement, {
      attributes: true,
      attributeFilter: ["fullscreenNavToolboxHidden"],
    });
    this._state.navToolboxCollapsed = document.documentElement.hasAttribute(
      "fullscreenNavToolboxHidden"
    );

    this._box = document.getElementById("sidebar-box");
    this._splitter = document.getElementById("sidebar-splitter");
    this._launcherSplitter = document.getElementById(
      "sidebar-launcher-splitter"
    );
    if (!this._splitterAriaUpdateTask) {
      this._splitterAriaUpdateTask = new DeferredTask(
        () => this._updateSplitterAriaAttributes(),
        0
      );
    }
    this._reversePositionButton = document.getElementById(
      "sidebar-reverse-position"
    );
    this._switcherPanel = document.getElementById("sidebarMenu-popup");
    this._switcherTarget = document.getElementById("sidebar-switcher-target");
    this._switcherArrow = document.getElementById("sidebar-switcher-arrow");
    this._openPopups = new Set();
    this._escapedWhileHovered = false;
    this._mouseLeftSinceEscape = false;
    if (
      Services.prefs.getBoolPref(
        "browser.tabs.allow_transparent_browser",
        false
      )
    ) {
      this.browser.setAttribute("transparent", "true");
    }

    const menubar = document.getElementById("viewSidebarMenu");
    const currentMenuItems = new Set(
      Array.from(menubar.childNodes, item => item.id)
    );
    for (const [commandID, sidebar] of this.sidebars.entries()) {
      if (
        !Object.hasOwn(sidebar, "extensionId") &&
        commandID !== "viewCustomizeSidebar" &&
        !currentMenuItems.has(sidebar.menuId)
      ) {
        // registerExtension() already creates menu items for extensions.
        const menuitem = this.createMenuItem(commandID, sidebar);
        menubar.appendChild(menuitem);
      }
    }
    if (this._mainResizeObserver) {
      this._mainResizeObserver.disconnect();
      this._mainResizeObserverAdded = false;
    }
    this._mainResizeObserver = new ResizeObserver(([entry]) =>
      this._handleLauncherResize(entry)
    );

    if (this.sidebarRevampEnabled && !BrowserHandler.kiosk) {
      if (!customElements.get("sidebar-main")) {
        ChromeUtils.importESModule(
          "chrome://browser/content/sidebar/sidebar-main.mjs",
          { global: "current" }
        );
      }
      if (!customElements.get("sidebar-panel-header")) {
        ChromeUtils.importESModule(
          "chrome://browser/content/sidebar/sidebar-panel-header.mjs",
          { global: "current" }
        );
      }
      this._sidebarMainKeydownHandler = e => {
        if (e.key === "Escape") {
          this.collapseOnEscape();
        }
      };
      window.addEventListener("keydown", this._sidebarMainKeydownHandler);
      this.revampComponentsLoaded = true;
      this._state.initializeState();

      // Revamp panels each provide their own header (the sidebar-panel-header
      // Lit element), including the "hide-launcher" panel switcher dropdown, so
      // the shared chrome-level header stays hidden.
      document.getElementById("sidebar-header").hidden = true;
      if (!this._mainResizeObserverAdded) {
        this._mainResizeObserver.observe(this.sidebarContainer);
        this._mainResizeObserverAdded = true;
      }
      if (!this._browserResizeObserver) {
        this._browserResizeObserver = () => {
          // Report resize events to Glean.
          const current = this.browser.getBoundingClientRect().width;
          const previous = this._browserWidth;
          const percentage = (current / window.innerWidth) * 100;
          Glean.sidebar.resize.record({
            current: Math.round(current),
            previous: Math.round(previous),
            percentage: Math.round(percentage),
          });
          this._recordBrowserSize();
        };
        this._splitter.addEventListener("command", this._browserResizeObserver);
      }
      this._enablePinnedTabsSplitterDragging();

      // Record Glean metrics.
      this.recordVisibilitySetting();
      this.recordPositionSetting();
      this.recordTabsLayoutSetting();
    } else {
      this._switcherCloseButton = document.getElementById("sidebar-close");
      if (!this._switcherListenersAdded) {
        this._switcherCloseButton.addEventListener("command", () => {
          this.hide();
        });
        this._switcherTarget.addEventListener("command", () => {
          this.toggleSwitcherPanel();
        });
        this._switcherTarget.addEventListener("keydown", event => {
          this.handleKeydown(event);
        });
        this._switcherListenersAdded = true;
      }
      this._disableLauncherDragging();
      this._disablePinnedTabsDragging();
    }
    // We need to update the tab strip for vertical tabs during init
    // as there will be no tabstrip-orientation-change event
    if (CustomizableUI.verticalTabsEnabled) {
      this.toggleTabstrip();
    }

    // sets the sidebar to the left or right, based on a pref
    this.setPosition();

    this._inited = true;

    if (!this._localesObserverAdded) {
      Services.obs.addObserver(this, "intl:app-locales-changed");
      this._localesObserverAdded = true;
    }
    if (!this._tabstripOrientationObserverAdded) {
      Services.obs.addObserver(this, "tabstrip-orientation-change");
      this._tabstripOrientationObserverAdded = true;
    }
    if (!this._aiWindowObserverAdded) {
      Services.obs.addObserver(this, "ai-window-state-changed");
      this._aiWindowObserverAdded = true;
    }
    if (!this._windowRestoredObserverAdded) {
      Services.obs.addObserver(this, "sessionstore-single-window-restored");
      this._windowRestoredObserverAdded = true;
    }
  },

  uninit() {
    // Set a flag to allow us to ignore pref changes while the host document is being unloaded.
    this._uninitializing = true;
    if (this._fullscreenObserver) {
      this._fullscreenObserver.disconnect();
      this._fullscreenObserver = null;
    }

    // If this is the last browser window, persist various values that should be
    // remembered for after a restart / reopening a browser window.
    let enumerator = Services.wm.getEnumerator("navigator:browser");
    if (!enumerator.hasMoreElements()) {
      let xulStore = Services.xulStore;
      xulStore.persist(this._title, "value");

      const currentState = this.getUIState();
      this.SidebarManager.setBackupState(currentState);
    }

    if (this._sidebarMainKeydownHandler) {
      window.removeEventListener("keydown", this._sidebarMainKeydownHandler);
      this._sidebarMainKeydownHandler = null;
    }
    this._sidebarMainKeydownHandler = null;
    Services.obs.removeObserver(this, "intl:app-locales-changed");
    Services.obs.removeObserver(this, "tabstrip-orientation-change");
    Services.obs.removeObserver(this, "ai-window-state-changed");
    Services.obs.removeObserver(this, "sessionstore-single-window-restored");
    delete this._tabstripOrientationObserverAdded;
    delete this._aiWindowObserverAdded;
    delete this._windowRestoredObserverAdded;

    CustomizableUI.removeListener(this);

    if (this._observer) {
      this._observer.disconnect();
      this._observer = null;
    }

    if (this._mainResizeObserver) {
      this._mainResizeObserver.disconnect();
      this._mainResizeObserver = null;
    }

    if (this._maxWidthUpdateTask) {
      this._maxWidthUpdateTask.finalize();
      this._maxWidthUpdateTask = null;
    }

    if (this.revampComponentsLoaded) {
      // Explicitly disconnect the revamp elements so that listeners
      // setup by reactive controllers will also be removed.
      this.sidebarMain.remove();
      this.panelHeader.remove();
      this._panelHeader = null;
    }
    this._splitter.removeEventListener("command", this._browserResizeObserver);
    this._splitterAriaUpdateTask.finalize();
    this._splitterAriaUpdateTask = null;
    this._disableLauncherDragging();
    this._disablePinnedTabsDragging();
  },

  /**
   * Handle the launcher being resized (either manually or programmatically).
   *
   * @param {ResizeObserverEntry} entry
   */
  _handleLauncherResize(entry) {
    this._state.launcherWidth = entry.borderBoxSize[0].inlineSize;
    if (this.isLauncherDragging) {
      this._state.launcherDragActive = true;
    }
    if (this._launcherCollapsedWidthStale) {
      this.refreshLauncherCollapsedWidth();
    }
  },

  requestMaxWidthUpdate() {
    if (!this._maxWidthUpdateTask) {
      this._maxWidthUpdateTask = new DeferredTask(
        () => this._updateLauncherAndPanelMaxWidths(),
        0
      );
    }
    this._maxWidthUpdateTask.arm();
  },

  /**
   * Update the max widths on the sidebar launcher and panel, so that their
   * combined widths don't exceed 75% of the viewport width.
   */
  async _updateLauncherAndPanelMaxWidths() {
    const launcherEl = this.sidebarContainer;
    const panelEl = this._box;
    const expandOnHoverEnabled = document.documentElement.hasAttribute(
      "sidebar-expand-on-hover"
    );
    if (
      expandOnHoverEnabled ||
      !this._state.launcherExpanded ||
      !this._state.panelOpen
    ) {
      // We don't have both the launcher + panel open. Fallback to css max widths.
      launcherEl.style.removeProperty("max-width");
      panelEl.style.removeProperty("max-width");
      return;
    }
    const { launcherWidth, panelMinWidth } =
      await window.promiseDocumentFlushed(() => ({
        launcherWidth: launcherEl.getBoundingClientRect().width,
        panelMinWidth: parseFloat(getComputedStyle(panelEl).minWidth),
      }));
    launcherEl.style.maxWidth = `calc(75vw - ${panelMinWidth}px)`;
    panelEl.style.maxWidth = `calc(75vw - ${launcherWidth}px)`;
  },

  getUIState() {
    if (this.inSingleTabWindow) {
      return null;
    }
    return this._state.getProperties();
  },

  /**
   * Load the UI state information given by session store, backup state, or
   * adopted window.
   *
   * @param {SidebarStateProps} state
   */
  async updateUIState(state = {}) {
    const isValidSidebar = !state.command || this.sidebars.has(state.command);
    if (!isValidSidebar) {
      state.command = "";
    }
    await this.waitUntilStable(); // Finish currently scheduled tasks.
    await this._state.loadCurrentState(state);
    await this.waitUntilStable(); // Finish newly scheduled tasks.
    this.updateToolbarButton();
    if (this.sidebarRevampVisibility === "expand-on-hover") {
      await this.toggleExpandOnHover(true);
    }
    this._initialUIStateUpdated = true;
  },

  /**
   * Toggle the vertical tabs preference.
   */
  toggleVerticalTabs() {
    Services.prefs.setBoolPref(
      "sidebar.verticalTabs",
      !this.sidebarVerticalTabsEnabled
    );
  },

  /**
   * The handler for Services.obs.addObserver.
   */
  observe(subject, topic, _data) {
    switch (topic) {
      case "intl:app-locales-changed": {
        if (this.isOpen) {
          // The <tree> component used in history and bookmarks, but it does not
          // support live switching the app locale. Reload the entire sidebar to
          // invalidate any old text.
          this.hide({ dismissPanel: false });
          this.showInitially(this.lastOpenedId);
          break;
        }
        if (this.revampComponentsLoaded) {
          this.sidebarMain.requestUpdate();
        }
        break;
      }
      case "sessionstore-single-window-restored": {
        // If Session restore was going to call updateUIState it would have
        // done so by now.
        if (subject == window) {
          this._initDeferred.resolve();
        }
        break;
      }
      case "tabstrip-orientation-change": {
        this.promiseInitialized.then(() => this.toggleTabstrip());
        break;
      }
      case "ai-window-state-changed": {
        if (subject == window) {
          // Update menus and hide sidebars that depend on the type of window
          for (const sidebar of this.sidebars.values()) {
            sidebar._updateMenus?.();
          }
        }
        break;
      }
    }
  },

  /**
   * Ensure the title stays in sync with the source element, which updates for
   * l10n changes.
   *
   * @param {HTMLElement} [element]
   */
  observeTitleChanges(element) {
    if (!element) {
      return;
    }
    let observer = this._observer;
    if (!observer) {
      observer = new MutationObserver(() => {
        // it's possible for lastOpenedId to be null here
        this.title = this.sidebars.get(this.lastOpenedId)?.title;
      });
      // Re-use the observer.
      this._observer = observer;
    }
    observer.disconnect();
    observer.observe(element, {
      attributes: true,
      attributeFilter: ["label"],
    });
  },

  /**
   * Opens the switcher panel if it's closed, or closes it if it's open.
   */
  toggleSwitcherPanel() {
    if (
      this._switcherPanel.state == "open" ||
      this._switcherPanel.state == "showing"
    ) {
      this.hideSwitcherPanel();
    } else if (this._switcherPanel.state == "closed") {
      this.showSwitcherPanel();
    }
  },

  /**
   * Build the list of panels for the "hide-launcher" panel switcher dropdown
   * rendered by the sidebar-panel-header Lit element. Lists the enabled tools,
   * enabled extension panels and the Customize panel; deliberately excludes
   * "close sidebar" and "move to other side". Tool labels are attribute-style
   * Fluent messages, so resolve their `.label` to plain strings for display.
   *
   * @returns {Promise<Array<{view: string, label: string}>>}
   */
  async getRevampSwitcherItems() {
    const resolveLabel = async l10nId => {
      const [message] = await document.l10n.formatMessages([{ id: l10nId }]);
      return message?.attributes?.find(a => a.name === "label")?.value ?? "";
    };
    const items = [];
    // Only filter out tools that are hidden (disabled by pref)
    for (const tool of this.getTools().filter(t => !t.hidden)) {
      items.push({ view: tool.view, label: await resolveLabel(tool.l10nId) });
    }
    for (const ext of this.getExtensions()) {
      items.push({ view: ext.view, label: ext.tooltiptext ?? "" });
    }
    const customize = this.sidebars.get("viewCustomizeSidebar");
    if (customize) {
      items.push({
        view: "viewCustomizeSidebar",
        label: await resolveLabel(customize.revampL10nId),
      });
    }
    return items;
  },

  /**
   * Handles keydown on the the switcherTarget button
   *
   * @param  {Event} event
   */
  handleKeydown(event) {
    switch (event.key) {
      case "Enter":
      case " ": {
        this.toggleSwitcherPanel();
        event.stopPropagation();
        event.preventDefault();
        break;
      }
      case "Escape": {
        this.hideSwitcherPanel();
        event.stopPropagation();
        event.preventDefault();
        break;
      }
    }
  },

  hideSwitcherPanel() {
    this._switcherPanel.hidePopup();
  },

  showSwitcherPanel() {
    this._switcherPanel.addEventListener(
      "popuphiding",
      () => {
        this._switcherTarget.classList.remove("active");
        this._switcherTarget.setAttribute("aria-expanded", false);
      },
      { once: true }
    );

    // Combine start/end position with ltr/rtl to set the label in the popup appropriately.
    let label =
      this._positionStart == RTL_UI
        ? gNavigatorBundle.getString("sidebar.moveToLeft")
        : gNavigatorBundle.getString("sidebar.moveToRight");
    this._reversePositionButton.setAttribute("label", label);

    // Open the sidebar switcher popup, anchored off the switcher toggle
    this._switcherPanel.hidden = false;
    this._switcherPanel.openPopup(this._switcherTarget);

    this._switcherTarget.classList.add("active");
    this._switcherTarget.setAttribute("aria-expanded", true);
  },

  updateShortcut({ keyId }) {
    let menuitem = this._switcherPanel?.querySelector(`[key="${keyId}"]`);
    if (!menuitem) {
      // If the menu item doesn't exist yet then the accel text will be set correctly
      // upon creation so there's nothing to do now.
      return;
    }
    menuitem.removeAttribute("acceltext");
  },

  /**
   * Change the pref that will trigger a call to setPosition
   */
  reversePosition() {
    Services.prefs.setBoolPref(this.POSITION_START_PREF, !this._positionStart);
  },

  /**
   * Read the positioning pref and position the sidebar and the splitter
   * appropriately within the browser container.
   */
  setPosition() {
    // First reset all ordinals to match DOM ordering.
    let contentArea = document.getElementById("tabbrowser-tabbox");
    let browser = document.getElementById("browser");
    [...browser.children].forEach((node, i, children) => {
      node.style.order = this._positionStart ? i + 1 : children.length - i;
    });
    let sidebarContainer = document.getElementById("sidebar-container");
    let sidebarMain = document.querySelector("sidebar-main");

    // Indicate we've switched ordering to the box
    document.documentElement.toggleAttribute(
      "sidebar-positionend",
      !this._positionStart
    );
    // TODO(bug 2078353): remove (can be superseded by the :root one).
    this._box.toggleAttribute("sidebar-positionend", !this._positionStart);
    sidebarMain.toggleAttribute("sidebar-positionend", !this._positionStart);
    // The launcher reads the position to place the Customize button.
    sidebarMain.requestUpdate?.();
    contentArea.toggleAttribute("sidebar-positionend", !this._positionStart);
    sidebarContainer.toggleAttribute(
      "sidebar-positionend",
      !this._positionStart
    );
    this.toolbarButton &&
      this.toolbarButton.toggleAttribute(
        "sidebar-positionend",
        !this._positionStart
      );

    this.hideSwitcherPanel();

    let content = SidebarController.browser.contentWindow;
    if (content && content.updatePosition) {
      content.updatePosition();
    }
  },

  /**
   * Show/hide new sidebar based on sidebar.revamp pref
   */
  async toggleRevampSidebar(newValue) {
    SidebarController._state.revampEnabled = newValue;
    await this.promiseInitialized;
    let wasOpen = this.isOpen;
    if (wasOpen) {
      this.hide({ dismissPanel: false });
    }
    // Reset sidebars map but preserve any existing extensions
    let extensionsArr = [];
    for (const [commandID, sidebar] of this.sidebars.entries()) {
      if (sidebar.hasOwnProperty("extensionId")) {
        extensionsArr.push({ commandID, sidebar });
      }
    }
    this.sidebars = this.generateSidebarsMap();
    for (const extension of extensionsArr) {
      this.sidebars.set(extension.commandID, extension.sidebar);
    }
    if (!this.sidebarRevampEnabled) {
      // The legacy sidebar has no launcher.
      this._state.applyLauncherVisibility();
      document.getElementById("sidebar-header").hidden = false;

      // Ensure CPM isn't shown.
      const cpmMenuItem = document.querySelector("#sidebar-switcher-megalist");
      this.lastOpenedId = this.DEFAULT_SIDEBAR_ID;
      // The menu item can get removed or may not be created yet
      if (cpmMenuItem) {
        cpmMenuItem.hidden = true;
      }
    }
    if (!this._sidebars.get(this.lastOpenedId)) {
      this.lastOpenedId = this.DEFAULT_SIDEBAR_ID;
      wasOpen = false;
    }
    this.updateToolbarButton();
    this._inited = false;
    this.init();

    // Reopen the panel in the new or old sidebar now that we've inited
    if (wasOpen) {
      this.toggle();
    }
  },

  /**
   * Try and adopt the status of the sidebar from another window.
   *
   * @param {Window} sourceWindow - Window to use as a source for sidebar status.
   * @returns {boolean} true if we adopted the state, or false if the caller should
   * initialize the state itself.
   */
  getAdoptedStateFromWindow(sourceWindow) {
    // If the opener had a sidebar, open the same sidebar in our window.
    // The opener can be the hidden window too, if we're coming from the state
    // where no windows are open, and the hidden window has no sidebar box.
    let sourceController = sourceWindow.SidebarController;
    if (!sourceController || !sourceController._box) {
      // no source UI or no _box means we also can't adopt the state.
      return null;
    }

    // Adopt the other window's UI state (it too could be a popup)
    // We get the properties directly forom the SidebarState instance as in this case
    // we need the command property even if no panel is currently open.
    const sourceState = sourceController.inPopup
      ? null
      : sourceController._state?.getProperties();
    return sourceState;
  },

  windowPrivacyMatches(w1, w2) {
    return (
      PrivateBrowsingUtils.isWindowPrivate(w1) ===
      PrivateBrowsingUtils.isWindowPrivate(w2)
    );
  },

  /**
   * If loading a sidebar was delayed on startup, start the load now.
   */
  async startDelayedLoad() {
    if (this.inSingleTabWindow) {
      this._state.applyLauncherVisibility();
      this._initDeferred.resolve();
      return;
    }

    let sourceWindow = window.opener;
    // No source window means this is the initial window.  If we're being
    // opened from another window, check that it is one we might open a sidebar
    // for.
    if (sourceWindow) {
      if (
        sourceWindow.closed ||
        sourceWindow.location.protocol != "chrome:" ||
        (!this.sidebarRevampEnabled &&
          !this.windowPrivacyMatches(sourceWindow, window))
      ) {
        this._initDeferred.resolve();
        return;
      }
      // Case: opened from a chrome window — adopt state from opener.
      let stateToApply = this.getAdoptedStateFromWindow(sourceWindow);
      if (stateToApply) {
        await this.updateUIState(stateToApply);
        this._initDeferred.resolve();
        return;
      }
    }

    // Case: SessionStore has handed us state via restoreSidebar. The
    // sessionstore-single-window-restored observer will resolve
    // _initDeferred once SessionStore has finished applying it.
    if (this._sessionRestoreStateReceived) {
      return;
    }

    // Case: SessionStore already finished restoring us before startDelayedLoad
    // ran (single-window startup, session restore slot-fill).
    if (this._initialUIStateUpdated) {
      this._initDeferred.resolve();
      return;
    }

    // Case: no upstream source. Load backup state as fallback.
    const backupState = this.SidebarManager.getBackupState();

    // If we're not adopting settings from a parent window, set them now.
    let wasOpen = this._box.getAttribute("checked");
    if (wasOpen) {
      let commandID = this._state.command;

      if (wasOpen && commandID && this.sidebars.has(commandID)) {
        this.showInitially(commandID, "restored");
      } else {
        this._box.removeAttribute("checked");
        // Update the state, because the element it
        // refers to no longer exists, so we should assume this sidebar
        // panel has been uninstalled. (249883)
        this._state.command = "";
        // On a startup in which the startup cache was invalidated (e.g. app update)
        // extensions will not be started prior to delayedLoad, thus the
        // sidebarcommand element will not exist yet.  Store the commandID so
        // extensions may reopen if necessary.  A startup cache invalidation
        // can be forced (for testing) by deleting compatibility.ini from the
        // profile.
        this.lastOpenedId = commandID;
      }
    }
    await this.updateUIState(backupState);
    this._initDeferred.resolve();
  },

  /**
   * Fire a "SidebarShown" event on the sidebar to give any interested parties
   * a chance to update the button or whatever.
   */
  _fireShowEvent() {
    let event = new CustomEvent("SidebarShown", { bubbles: true });
    this._switcherTarget.dispatchEvent(event);
  },

  /**
   * Report the current browser width to Glean, and store it internally.
   */
  _recordBrowserSize() {
    this._browserWidth = this.browser.getBoundingClientRect().width;
    Glean.sidebar.width.set(this._browserWidth);
    this._splitterAriaUpdateTask.arm();
  },

  /**
   * Update the `aria-valuemin`, `aria-valuemax`, and `aria-valuenow`
   * attributes of the panel splitter to account for resizes and state changes.
   */
  _updateSplitterAriaAttributes() {
    const splitter = this._splitter;
    if (!this._state.panelOpen) {
      splitter.removeAttribute("aria-valuemin");
      splitter.removeAttribute("aria-valuemax");
      splitter.removeAttribute("aria-valuenow");
      return;
    }
    const style = window.getComputedStyle(this._box);
    const minWidth = parseFloat(style.minWidth);
    const maxWidth = parseFloat(style.maxWidth);
    const currentWidth =
      this._state.panelWidth ??
      window.windowUtils.getBoundsWithoutFlushing(this._box).width;

    splitter.setAttribute("aria-valuemin", Math.round(minWidth));
    splitter.setAttribute("aria-valuemax", Math.round(maxWidth));
    splitter.setAttribute("aria-valuenow", Math.round(currentWidth));
  },

  /**
   * Fire a "SidebarFocused" event on the sidebar's |window| to give the sidebar
   * a chance to adjust focus as needed. An additional event is needed, because
   * we don't want to focus the sidebar when it's opened on startup or in a new
   * window, only when the user opens the sidebar.
   */
  _fireFocusedEvent() {
    let event = new CustomEvent("SidebarFocused", { bubbles: true });
    this.browser.contentWindow.dispatchEvent(event);
  },

  /**
   * True if the sidebar is currently open.
   */
  get isOpen() {
    return this._box ? !this._box.hidden : false;
  },

  /**
   * The ID of the current sidebar.
   */
  get currentID() {
    return this.isOpen ? this._state.command : "";
  },

  /**
   * The context menu of the current sidebar.
   */
  get currentContextMenu() {
    const sidebar = this.sidebars.get(this.currentID);
    if (!sidebar) {
      return null;
    }
    return document.getElementById(sidebar.contextMenuId);
  },

  get launcherVisible() {
    return this._state?.launcherVisible;
  },

  get title() {
    return this._title.value;
  },

  set title(value) {
    this._title.value = value;
  },

  /**
   * Toggle the visibility of the sidebar. If the sidebar is hidden or is open
   * with a different commandID, then the sidebar will be opened using the
   * specified commandID. Otherwise the sidebar will be hidden.
   *
   * @param  {string}  commandID     ID of the sidebar.
   * @param  {DOMNode} [triggerNode] Node, usually a button, that triggered the
   *                                 visibility toggling of the sidebar.
   * @returns {Promise}
   */
  toggle(commandID = this.lastOpenedId, triggerNode) {
    if (
      CustomizationHandler.isCustomizing() ||
      CustomizationHandler.isExitingCustomizeMode
    ) {
      return Promise.resolve();
    }
    // First priority for a default value is this.lastOpenedId which is set during show()
    // and not reset in hide(), unlike currentID. If show() hasn't been called and we don't
    // have a persisted command either, or the command doesn't exist anymore, then
    // fallback to a default sidebar.
    if (!commandID) {
      commandID = this._state.command;
    }
    if (!commandID || !this.sidebars.has(commandID)) {
      if (this.sidebarRevampEnabled && this.sidebars.size) {
        commandID = this.sidebars.keys().next().value;
      } else {
        commandID = this.DEFAULT_SIDEBAR_ID;
      }
    }

    if (this.isOpen && commandID == this.currentID) {
      // Revamp sidebar: this case is a dismissal of the current sidebar panel. The launcher should stay open
      // For legacy sidebar, this is a "sidebar" toggle and the current panel should be remembered.
      // In horizontal "hide-launcher" mode there is no launcher to return to, so
      // keep the panel remembered (dismissPanel: false) like the toolbar button
      // and close button do, rather than revealing the launcher.
      return this._maybeAnimate(() => {
        this.hide({
          triggerNode,
          dismissPanel:
            this.sidebarRevampEnabled && !this._state.launcherHiddenWithPanel,
        });
        this.updateToolbarButton();
      });
    }

    if (!this.sidebarRevampEnabled) {
      const cpmMenuItem = document.querySelector("#sidebar-switcher-megalist");
      this.lastOpenedId = this.DEFAULT_SIDEBAR_ID;
      // The menu item can get removed or may not be created yet
      if (cpmMenuItem) {
        cpmMenuItem.hidden = true;
      }
    }

    return this._showAnimated(commandID, triggerNode);
  },

  /**
   * Show the sidebar within a view transition, without holding the transition
   * until the panel's document has loaded.
   *
   * @param {string}  commandID
   * @param {DOMNode} [triggerNode]
   * @returns {Promise<boolean>}
   */
  async _showAnimated(commandID, triggerNode) {
    let shown;
    await this._maybeAnimate(() => {
      shown = this.show(commandID, triggerNode);
    });
    return shown;
  },

  /**
   * Wait for Lit updates and any ongoing view transition to complete.
   *
   * @returns {Promise}
   */
  async waitUntilStable() {
    if (!this.sidebarRevampEnabled) {
      // Legacy sidebar doesn't have animations, nothing to await.
      return null;
    }
    const tasks = [this.sidebarMain.updateComplete];
    // A transition can't finish until its update callback has, so don't wait
    // on it from inside that callback.
    if (this._viewTransitionPromise && !this._inViewTransitionUpdate) {
      tasks.push(this._viewTransitionPromise);
    }
    return Promise.allSettled(tasks);
  },

  /**
   * For sidebar.revamp=true only, handle the keyboard or sidebar-button command to toggle the sidebar state
   */
  async handleToolbarButtonClick() {
    if (this.inSingleTabWindow || this.uninitializing) {
      return;
    }

    const initialExpandedValue = this._state.launcherExpanded;

    // What toggle means depends on the sidebar.visibility pref: where the
    // launcher always shows there is nothing to toggle but its expanded state.
    const expandOnToggle = this._state.toolbarButtonTogglesExpanded;

    // when the launcher is toggled open by the user, we disable expand-on-hover interactions.
    if (this.sidebarRevampVisibility === "expand-on-hover") {
      await this.toggleExpandOnHover(initialExpandedValue);
    }

    const update = () => {
      if (expandOnToggle) {
        // just expand/collapse the launcher
        this._state.launcherExpanded = !initialExpandedValue;
        this.updateToolbarButton();
        return;
      }

      if (this._state.launcherHiddenWithPanel) {
        // Horizontal-tabs "hide sidebar" mode: the launcher stays hidden and
        // the toolbar button toggles only the panel, preserving the last panel
        // so it re-opens.
        if (this.isOpen) {
          this.hide({ dismissPanel: false });
        } else {
          let commandID = this._state.command || this.lastOpenedId;
          if (!commandID || !this.sidebars.has(commandID)) {
            commandID = this.sidebars.keys().next().value;
          }
          this.show(commandID);
        }
        this.updateToolbarButton();
        return;
      }

      const shouldShowLauncher = !this._state.launcherVisible;
      // show/hide the launcher
      this._state.userLauncherVisible = shouldShowLauncher;
      if (this._state.launcherExpandable) {
        this._state.launcherExpanded = shouldShowLauncher;
      }
      this.updateToolbarButton();
      if (shouldShowLauncher && !this.isOpen && this._state.command) {
        // Nothing is open, so bring back the panel the launcher last had. A
        // panel that is already open is left alone: revealing the launcher only
        // changes the launcher.
        this.show(this._state.command);
      } else if (!shouldShowLauncher) {
        // hide the open panel. It will re-open next time as we don't change the command value
        this.hide({ dismissPanel: false });
      }
    };
    await this._maybeAnimate(update);
  },

  /**
   * Update `checked` state and tooltip text of the toolbar button.
   */
  updateToolbarButton(toolbarButton = this.toolbarButton) {
    if (!toolbarButton || this.inSingleTabWindow) {
      return;
    }
    if (!this.sidebarRevampEnabled) {
      toolbarButton.dataset.l10nId = "show-sidebars";
      toolbarButton.checked = this.isOpen;
    } else {
      let sidebarToggleKey = document.getElementById("toggleSidebarKb");
      const shortcut = ShortcutUtils.prettifyShortcut(sidebarToggleKey);
      toolbarButton.dataset.l10nArgs = JSON.stringify({ shortcut });
      // we need to use the pref rather than SidebarController's getter here
      // as the getter might not have the new value yet
      const isVerticalTabs = Services.prefs.getBoolPref("sidebar.verticalTabs");
      if (isVerticalTabs) {
        toolbarButton.toggleAttribute("expanded", this.sidebarMain.expanded);
      } else {
        toolbarButton.toggleAttribute("expanded", false);
      }
      this.handleToolBadges();
      switch (this.sidebarRevampVisibility) {
        case "always-show":
        case "expand-on-hover": {
          // Vertical tabs: the toolbar button controls the expanded state.
          const isExpanded = this.sidebarMain.expanded;
          toolbarButton.checked = isExpanded;
          toolbarButton.dataset.l10nId = isExpanded
            ? "sidebar-widget-collapse-sidebar2"
            : "sidebar-widget-expand-sidebar2";
          break;
        }
        case "hide-sidebar": {
          // Vertical tabs: the toolbar button controls the launcher's hidden
          // state.
          const isVisible = !this.sidebarContainer.hidden;
          toolbarButton.checked = isVisible;
          toolbarButton.dataset.l10nId = isVisible
            ? "sidebar-widget-hide-sidebar2"
            : "sidebar-widget-show-sidebar2";
          break;
        }
        case "hide-on-close": {
          // Horizontal default: the button shows/hides the collapsed launcher
          // and, as before, is never highlighted in horizontal mode.
          const isVisible = !this.sidebarContainer.hidden;
          toolbarButton.checked = false;
          toolbarButton.dataset.l10nId = isVisible
            ? "sidebar-widget-hide-sidebar2"
            : "sidebar-widget-show-sidebar2";
          break;
        }
        case "hide-launcher": {
          // Horizontal switcher-only: the launcher stays hidden and the button
          // controls the panel's open state.
          const isVisible = this.isOpen;
          toolbarButton.checked = isVisible;
          toolbarButton.dataset.l10nId = isVisible
            ? "sidebar-widget-hide-sidebar2"
            : "sidebar-widget-show-sidebar2";
          break;
        }
      }
    }
  },

  /**
   * Handles badges display for the toolbar and sidebar.
   * Check if a tool(toolID) has requested a badge from pref (i.e) sidebar.notification.badge.{toolID})
   * Ensure that badges are shown or cleared based on the sidebar visibility and user interaction.
   *
   * @param {string|null} toolID
   */
  handleToolBadges(toolID = null) {
    const toolPrefList = this.SidebarManager.getBadgeTools();

    for (const pref of toolPrefList) {
      if (toolID && toolID !== pref) {
        continue;
      }

      const badgePref = Services.prefs.getBoolPref(
        `sidebar.notification.badge.${pref}`,
        false
      );
      const commandID = [...this.toolsAndExtensions.keys()].find(
        id => toolsNameMap[id] === pref
      );

      if (!commandID) {
        continue;
      }

      const isSidebarClosed = !this._state?.launcherVisible;
      const isCurrentView = this._state?.command === commandID;

      // Don't show sidebar badge if sidebar is open and user is already viewing the tool panel
      if (badgePref && isCurrentView && this.isOpen) {
        this.dismissSidebarBadge(commandID);
      }

      // Show badge on toolbar if we would have shown it on a visible tool but sidebar is closed
      const tool = this.toolsAndExtensions.get(commandID);
      if (
        this.sidebarRevampEnabled &&
        badgePref &&
        !tool.disabled &&
        !tool.hidden &&
        isSidebarClosed
      ) {
        this._showToolbarButtonBadge();
      } else {
        this._clearToolbarButtonBadge();
      }

      window.dispatchEvent(new CustomEvent("SidebarItemChanged"));
    }
  },

  _isMenuPopupOpen() {
    for (const popup of this._openPopups) {
      if (popup.state !== "open" && popup.state !== "showing") {
        this._openPopups.delete(popup);
      }
    }
    return this._openPopups.size > 0;
  },

  /**
   * Re-evaluate hover state and expand or collapse the launcher to match. Used
   * to recover transitions that were ignored while suppressed (during an
   * animation or while a popup was open).
   */
  _reconcileHoverState() {
    if (this._activeViewTransition || this._isMenuPopupOpen()) {
      return;
    }
    let isHovered = this._checkIsHoveredOverLauncher();
    if (isHovered && !this._state.launcherExpanded) {
      this.onMouseEnter();
    } else if (!isHovered && this._state.launcherExpanded) {
      this._collapseLauncher();
    }
  },

  _showToolbarButtonBadge() {
    const badgeEl = this.toolbarButton?.querySelector(".toolbarbutton-badge");
    return badgeEl?.classList.add("feature-callout");
  },

  _clearToolbarButtonBadge() {
    const badgeEl = this.toolbarButton?.querySelector(".toolbarbutton-badge");
    return badgeEl?.classList.remove("feature-callout");
  },

  /**
   * Set badge toolID pref false on clicking the tool icon
   *
   * @param {string} view
   */
  dismissSidebarBadge(view) {
    const prefName = `sidebar.notification.badge.${toolsNameMap[view]}`;
    if (Services.prefs.getBoolPref(prefName, false)) {
      Services.prefs.setBoolPref(prefName, false);
    }
  },

  /**
   * Enable the splitter which can be used to resize the launcher.
   */
  _enableLauncherDragging() {
    if (this._launcherDropHandler) {
      // Already set up observers and handler.
      return;
    }
    if (!this._panelResizeObserver) {
      this._panelResizeObserver = new ResizeObserver(
        ([entry]) =>
          (this._state.panelWidth = entry.borderBoxSize[0].inlineSize)
      );
    }
    this._panelResizeObserver.observe(this._box);

    this._launcherDropHandler = () => {
      this._state.launcherDragActive = false;
      this.updatePinnedTabsHeightOnResize();
    };
    this._launcherSplitter.addEventListener(
      "command",
      this._launcherDropHandler
    );
  },

  /**
   * Enable the splitter which can be used to resize the pinned tabs container.
   */
  _enablePinnedTabsSplitterDragging() {
    if (!this._pinnedTabsSplitter.hidden) {
      // Already showing the launcher splitter with observers connected.
      // Nothing to do.
      return;
    }
    this._pinnedTabsResizeObserver = new ResizeObserver(() => {
      if (this.isPinnedTabsDragging) {
        this._state.pinnedTabsDragActive = true;
      }
    });

    this._itemsWrapperResizeObserver = new ResizeObserver(async ([entry]) => {
      await window.promiseDocumentFlushed(() => {
        requestAnimationFrame(() => {
          if (this._pinnedTabsContainer.hasAttribute("dragActive")) {
            return;
          }
          // Only respond to WIDTH changes (sidebar being resized wider/narrower).
          // Ignoring height changes prevents an infinite resize loop.
          const newWidth = entry.contentBoxSize[0].inlineSize;
          if (newWidth === this._pinnedTabsItemsWrapperWidth) {
            return;
          }
          this._pinnedTabsItemsWrapperWidth = newWidth;
          this.updatePinnedTabsHeightOnResize();
        });
      });
    });
    this._pinnedTabsResizeObserver.observe(this._pinnedTabsContainer);
    this._itemsWrapperResizeObserver.observe(this._pinnedTabsItemsWrapper);

    this._pinnedTabsDropHandler = () =>
      (this._state.pinnedTabsDragActive = false);
    this._pinnedTabsSplitter.addEventListener(
      "command",
      this._pinnedTabsDropHandler
    );

    this._pinnedTabsSplitter.hidden = false;
  },

  /**
   * Disable the launcher splitter and remove any active observers.
   */
  _disableLauncherDragging() {
    if (this._panelResizeObserver) {
      this._panelResizeObserver.disconnect();
    }
    this._launcherSplitter.removeEventListener(
      "command",
      this._launcherDropHandler
    );
    delete this._launcherDropHandler;
  },

  /**
   * Disable the pinned tabs splitter and remove any active observers.
   */
  _disablePinnedTabsDragging() {
    if (this._pinnedTabsResizeObserver) {
      this._pinnedTabsResizeObserver.disconnect();
    }
    if (this._itemsWrapperResizeObserver) {
      this._itemsWrapperResizeObserver.disconnect();
    }
    if (this._pinnedTabsSplitter) {
      this._pinnedTabsSplitter.hidden = true;
    }
  },

  _loadSidebarExtension(commandID) {
    let sidebar = this.sidebars.get(commandID);
    if (typeof sidebar?.onload === "function") {
      sidebar.onload();
    }
  },

  updatePinnedTabsHeightOnResize() {
    // Skip during any splitter drag to avoid flickering.
    if (this.isLauncherDragging || this.isPinnedTabsDragging) {
      return;
    }

    if (!this._pinnedTabsContainer.childElementCount) {
      return;
    }

    this._state.updatePinnedTabsHeight();
  },

  async updatePinnedTabsHeightAfterReflow() {
    if (!this.sidebarVerticalTabsEnabled || !this._pinnedTabsContainer) {
      return;
    }
    await window.promiseDocumentFlushed(() => {});
    if (!this.uninitializing) {
      this.updatePinnedTabsHeightOnResize();
    }
  },

  /**
   * Ensure tools reflect the current pref state
   */
  refreshTools() {
    let changed = false;
    const tools = new Set(this.sidebarRevampTools.split(","));
    this.toolsAndExtensions.forEach(tool => {
      const expected = !tools.has(tool.name);
      if (tool.disabled != expected) {
        tool.disabled = expected;
        changed = true;
      }
    });
    if (changed) {
      window.dispatchEvent(new CustomEvent("SidebarItemChanged"));
    }
  },

  /**
   * Sets the disabled property for a tool when customizing sidebar options
   *
   * @param {string} commandID
   */
  toggleTool(commandID) {
    const toggledTool = this.toolsAndExtensions.get(commandID);
    const toolName = toggledTool.name;
    toggledTool.disabled = !toggledTool.disabled;

    if (!toggledTool.disabled) {
      // If re-enabling tool, remove from the map and add it to the end
      this.toolsAndExtensions.delete(commandID);
      this.toolsAndExtensions.set(commandID, toggledTool);
    }

    this.SidebarManager.updateToolsPref(toolName, toggledTool.disabled);

    if (toggledTool.disabled) {
      this.dismissSidebarBadge(commandID);
    }
    window.dispatchEvent(new CustomEvent("SidebarItemChanged"));
  },

  addOrUpdateExtension(commandID, extension) {
    if (this.inSingleTabWindow) {
      return;
    }
    if (this.toolsAndExtensions.has(commandID)) {
      // Update existing extension
      let extensionToUpdate = this.toolsAndExtensions.get(commandID);
      extensionToUpdate.iconUrl = extension.iconUrl;
      extensionToUpdate.tooltiptext = extension.label;
      window.dispatchEvent(new CustomEvent("SidebarItemChanged"));
    } else {
      // Add new extension
      const name = extension.extensionId;
      this.toolsAndExtensions.set(commandID, {
        view: commandID,
        extensionId: extension.extensionId,
        iconUrl: extension.iconUrl,
        tooltiptext: extension.label,
        disabled: !this.sidebarTools.includes(name), // name is the extensionID
        name,
      });
      window.dispatchEvent(new CustomEvent("SidebarItemAdded"));
    }
  },

  /**
   * Add menu items for a browser extension. Add the extension to the
   * `sidebars` map.
   *
   * @param {string} commandID
   * @param {object} props
   */
  registerExtension(commandID, props) {
    const sidebarTools = this.sidebarTools;
    const installedExtensions = this.sidebarExtensions;
    const name = props.extensionId;

    // An extension that is newly installed will be added to the sidebar.main.tools
    // pref by default until a user deselects it; separately we update our list of
    // sidebar extensions to ensure it keeps track of what's been installed.
    if (!installedExtensions.includes(name) && !sidebarTools.includes(name)) {
      sidebarTools.push(name);
      installedExtensions.push(name);
      Services.prefs.setStringPref(this.TOOLS_PREF, sidebarTools.join());
      Services.prefs.setStringPref(
        this.INSTALLED_EXTENSIONS,
        installedExtensions.join()
      );
    }

    const sidebar = {
      title: props.title,
      url: "chrome://browser/content/webext-panels.xhtml",
      menuId: props.menuId,
      switcherMenuId: `sidebarswitcher_menu_${commandID}`,
      keyId: `ext-key-id-${commandID}`,
      label: props.title,
      iconUrl: props.iconUrl,
      classAttribute: "menuitem-iconic webextension-menuitem",
      // The following properties are specific to extensions
      extensionId: props.extensionId,
      onload: props.onload,
      name,
    };
    this.sidebars.set(commandID, sidebar);

    // Insert a menuitem for View->Show Sidebars.
    const menuitem = this.createMenuItem(commandID, sidebar);
    document.getElementById("viewSidebarMenu").appendChild(menuitem);
    this.addOrUpdateExtension(commandID, sidebar);

    if (!this.sidebarRevampEnabled) {
      // Insert a toolbarbutton for the sidebar dropdown selector.
      let switcherMenuitem = this.createMenuItem(commandID, sidebar);
      switcherMenuitem.setAttribute("id", sidebar.switcherMenuId);
      switcherMenuitem.removeAttribute("type");

      let separator = document.getElementById("sidebar-extensions-separator");
      separator.parentNode.insertBefore(switcherMenuitem, separator);
    }
    this._setExtensionAttributes(
      commandID,
      { iconUrl: props.iconUrl, label: props.title },
      sidebar
    );
  },

  /**
   * Create a menu item for the View>Sidebars submenu in the menubar.
   *
   * @param {string} commandID
   * @param {object} sidebar
   * @returns {Element}
   */
  createMenuItem(commandID, sidebar) {
    const menuitem = document.createXULElement("menuitem");
    menuitem.setAttribute("id", sidebar.menuId);
    menuitem.setAttribute("type", "checkbox");
    // Some menu items get checkbox type removed, so should show the sidebar
    menuitem.addEventListener("command", () =>
      this[menuitem.hasAttribute("type") ? "toggle" : "show"](commandID)
    );
    if (sidebar.classAttribute) {
      menuitem.setAttribute("class", sidebar.classAttribute);
    }
    if (sidebar.keyId) {
      menuitem.setAttribute("key", sidebar.keyId);
    }
    if (sidebar.menuL10nId) {
      menuitem.dataset.l10nId = sidebar.menuL10nId;
    }
    if (this.inSingleTabWindow) {
      menuitem.setAttribute("disabled", "true");
    }
    return menuitem;
  },

  /**
   * Update attributes on all existing menu items for a browser extension.
   *
   * @param {string} commandID
   * @param {object} attributes
   * @param {string} attributes.iconUrl
   * @param {string} attributes.label
   * @param {boolean} needsRefresh
   */
  setExtensionAttributes(commandID, attributes, needsRefresh) {
    const sidebar = this.sidebars.get(commandID);
    this._setExtensionAttributes(commandID, attributes, sidebar, needsRefresh);
    this.addOrUpdateExtension(commandID, sidebar);
  },

  _setExtensionAttributes(
    commandID,
    { iconUrl, label },
    sidebar,
    needsRefresh = false
  ) {
    sidebar.iconUrl = iconUrl;
    sidebar.label = label;

    const updateAttributes = el => {
      // TODO Bug 1996762 - Add support for dark-theme sidebar icons
      // --webextension-menuitem-image-dark is used in dark themes
      el.style.setProperty(
        "--webextension-menuitem-image",
        `url("${sidebar.iconUrl}")`
      );
      el.setAttribute("label", sidebar.label);
      el.toggleAttribute("auto-accesskey", true);
    };

    updateAttributes(document.getElementById(sidebar.menuId), sidebar);
    const switcherMenu = document.getElementById(sidebar.switcherMenuId);
    if (switcherMenu) {
      updateAttributes(switcherMenu, sidebar);
    }
    if (this.initialized && this.currentID === commandID) {
      // Update the sidebar title if this extension is the current sidebar.
      this.title = label;
      if (this.panelHeader) {
        this.panelHeader.heading = label;
      }
      if (this.isOpen && needsRefresh) {
        this.show(commandID);
      }
    }
  },

  /**
   * Retrieve the list of registered browser extensions.
   *
   * @returns {Array}
   */
  getExtensions() {
    const extensions = [];
    for (const [commandID, sidebar] of this.sidebars.entries()) {
      if (Object.hasOwn(sidebar, "extensionId")) {
        const disabled = !this.sidebarTools.includes(sidebar.name);

        extensions.push({
          commandID,
          view: commandID,
          extensionId: sidebar.extensionId,
          iconUrl: sidebar.iconUrl,
          tooltiptext: sidebar.label,
          disabled,
          name: sidebar.name,
        });
      }
    }

    return extensions;
  },

  /**
   * Retrieve the list of tools in the sidebar
   *
   * @returns {Array}
   */
  getTools() {
    return Object.keys(toolsNameMap)
      .filter(commandID => this.sidebars.get(commandID))
      .map(commandID => {
        const sidebar = this.sidebars.get(commandID);
        const disabled = !this.sidebarTools.includes(toolsNameMap[commandID]);
        return {
          commandID,
          view: commandID,
          name: sidebar.name,
          iconUrl: sidebar.iconUrl,
          l10nId: sidebar.revampL10nId,
          disabled,
          // Reflect the current tool state defaulting to visible
          get hidden() {
            return !(sidebar.visible ?? true);
          },
          get attention() {
            return sidebar.attention ?? false;
          },
          contextMenu: sidebar.toolContextMenuId,
        };
      });
  },

  /**
   * Remove a browser extension.
   *
   * @param {string} commandID
   */
  removeExtension(commandID) {
    if (this.inSingleTabWindow) {
      return;
    }
    const sidebar = this.sidebars.get(commandID);
    if (!sidebar) {
      return;
    }
    if (this.currentID === commandID) {
      // If the extension removal is a update, we don't want to forget this panel.
      // So, let the sidebarAction extension API code remove the lastOpenedId as needed
      this.hide({ dismissPanel: false });
    }
    document.getElementById(sidebar.menuId)?.remove();
    document.getElementById(sidebar.switcherMenuId)?.remove();

    this.sidebars.delete(commandID);
    this.toolsAndExtensions.delete(commandID);
    window.dispatchEvent(new CustomEvent("SidebarItemRemoved"));
  },

  /**
   * Check whether a sidebar panel can be shown in the current window.
   *
   * @param {string} commandID
   * @returns {boolean}
   */
  _canShow(commandID) {
    // Extensions without private window access wont be in the sidebars map.
    // Some sidebars can be conditionally visible depending on the window.
    const sidebar = this.sidebars.get(commandID);
    return !!sidebar && (sidebar.visible ?? true);
  },

  /**
   * Show the sidebar.
   *
   * This wraps the internal method, including a ping to telemetry.
   *
   * @param {string}  commandID     ID of the sidebar to use.
   * @param {DOMNode} [triggerNode] Node, usually a button, that triggered the
   *                                showing of the sidebar.
   * @returns {Promise<boolean>}
   */
  async show(commandID, triggerNode) {
    if (this.inSingleTabWindow) {
      return false;
    }
    if (this.currentID && commandID !== this.currentID) {
      // If there is currently a panel open, we are about to hide it in order
      // to show another one, so record a "hide" event on the current panel.
      this._recordPanelToggle(this.currentID, false);
    }
    this._recordPanelToggle(commandID, true);

    if (!this._canShow(commandID)) {
      return false;
    }
    // show() is the user-launch path, so any entry point records as sidebar_launch.
    return this._show(commandID, "sidebar_launch").then(() => {
      this._loadSidebarExtension(commandID);

      if (triggerNode) {
        updateToggleControlLabel(triggerNode);
      }
      this._panelTriggerNode = triggerNode;
      this.updateToolbarButton();
      this.dismissSidebarBadge(commandID);

      this._fireFocusedEvent();
      return true;
    });
  },

  /**
   * Show the sidebar, without firing the focused event or logging telemetry.
   * This is intended to be used when the sidebar is opened automatically
   * when a window opens (not triggered by user interaction).
   *
   * @param {string} commandID ID of the sidebar.
   * @param {string} [openType] Why the sidebar is being shown, see _show().
   *   Defaults to "other": most callers aren't restoring a panel.
   * @returns {Promise<boolean>}
   */
  async showInitially(commandID, openType = "other") {
    if (this.inSingleTabWindow) {
      return false;
    }
    this._recordPanelToggle(commandID, true);

    if (!this._canShow(commandID)) {
      return false;
    }
    return this._show(commandID, openType).then(() => {
      this._loadSidebarExtension(commandID);
      return true;
    });
  },

  /**
   * Implementation for show. Also used internally for sidebars that are shown
   * when a window is opened and we don't want to ping telemetry.
   *
   * @param {string} commandID ID of the sidebar.
   * @param {string} [openType] Why the sidebar is being shown. Appended to
   *   the URL of panels registered with appendOpenType; others ignore it.
   * @returns {Promise<void>}
   */
  _show(commandID, openType) {
    return new Promise(resolve => {
      const willShowEvent = new CustomEvent("SidebarWillShow");
      this.browser.contentWindow?.dispatchEvent(willShowEvent);

      this._state.panelOpen = true;
      if (this.sidebarRevampEnabled) {
        this._box.dispatchEvent(
          new CustomEvent("sidebar-show", { detail: { viewId: commandID } })
        );
        this.panelHeader.setView(commandID);
      } else {
        this.hideSwitcherPanel();
      }

      this.selectMenuItem(commandID);
      this._box.hidden = this._splitter.hidden = false;

      this._box.setAttribute("checked", "true");
      this._state.command = commandID;

      let { iconUrl, url, title, sourceL10nEl, contextMenuId, appendOpenType } =
        this.sidebars.get(commandID);
      if (appendOpenType && openType) {
        url += `${url.includes("?") ? "&" : "?"}openType=${openType}`;
      }
      if (iconUrl) {
        this._switcherTarget.style.setProperty(
          "--webextension-menuitem-image",
          `url("${iconUrl}")`
        );
      } else {
        this._switcherTarget.style.removeProperty(
          "--webextension-menuitem-image"
        );
      }

      if (contextMenuId) {
        this._box.setAttribute("context", contextMenuId);
      } else {
        this._box.removeAttribute("context");
      }

      // use to live update <tree> elements if the locale changes
      this.lastOpenedId = commandID;
      // These title changes only apply to the legacy sidebar; revamp panels
      // render their own header (the sidebar-panel-header Lit element).
      if (!this.sidebarRevampEnabled) {
        this.title = title;
        // Keep the title element in the switcher in sync with any l10n changes.
        this.observeTitleChanges(sourceL10nEl);
      }

      this.browser.setAttribute("src", url); // kick off async load

      if (this.browser.contentDocument.location.href != url) {
        // make sure to clear the timeout if the load is aborted
        this.browser.addEventListener("unload", () => {
          if (this.browser.loadingTimerID) {
            clearTimeout(this.browser.loadingTimerID);
            delete this.browser.loadingTimerID;
            resolve();
          }
        });
        this.browser.addEventListener(
          "load",
          () => {
            // We're handling the 'load' event before it bubbles up to the usual
            // (non-capturing) event handlers. Let it bubble up before resolving.
            this.browser.loadingTimerID = setTimeout(() => {
              delete this.browser.loadingTimerID;
              resolve();

              // Now that the currentId is updated, fire a show event.
              this._fireShowEvent();
              this._recordBrowserSize();

              const sidebar = this.sidebars.get(commandID);
              // Initialize sidebar permissions UI
              if (sidebar?.permissions) {
                if (!this._permissions) {
                  this._permissions = new this.SidebarPermissions(window);
                }
                this._permissions.init(this.browser);
              }
            }, 0);
          },
          { capture: true, once: true }
        );
      } else {
        resolve();

        // Now that the currentId is updated, fire a show event.
        this._fireShowEvent();
        this._recordBrowserSize();
      }
    });
  },

  /**
   * Hide the sidebar.
   *
   * @param {object} options - Parameter object.
   * @param {DOMNode} options.triggerNode - Node, usually a button, that triggered the
   *                                        hiding of the sidebar.
   * @param {boolean} options.dismissPanel - Only close the panel or close the whole sidebar (the default.)
   * @param {boolean} options.restoreFocus - Return focus to the control that opened the panel.
   */
  hide({
    triggerNode,
    dismissPanel = this.sidebarRevampEnabled,
    restoreFocus = false,
  } = {}) {
    if (!this.isOpen) {
      return;
    }

    const willHideEvent = new CustomEvent("SidebarWillHide", {
      cancelable: true,
    });
    this.browser.contentWindow?.dispatchEvent(willHideEvent);
    if (willHideEvent.defaultPrevented) {
      return;
    }

    this.hideSwitcherPanel();
    this._recordPanelToggle(this.currentID, false);
    this._state.panelOpen = false;
    if (dismissPanel) {
      // The user is explicitly closing this panel so we don't want it to
      // automatically re-open next time the sidebar is shown
      this._state.command = "";
      this.lastOpenedId = null;
    }

    if (this.sidebarRevampEnabled) {
      this._box.dispatchEvent(new CustomEvent("sidebar-hide"));
    }
    this.selectMenuItem("");

    // Replace the document currently displayed in the sidebar with about:blank
    // so that we can free memory by unloading the page. We need to explicitly
    // create a new content viewer because the old one doesn't get destroyed
    // until about:blank has loaded (which does not happen as long as the
    // element is hidden).
    this.browser.setAttribute("src", "about:blank");
    this.browser.docShell?.createAboutBlankDocumentViewer(null, null);

    this._box.removeAttribute("checked");
    this._box.removeAttribute("context");
    this._box.hidden = this._splitter.hidden = true;
    this._splitterAriaUpdateTask.arm();

    let isFocusRestored = false;
    if (restoreFocus) {
      if (this._panelTriggerNode?.isConnected) {
        this._panelTriggerNode.focus();
        isFocusRestored =
          this._panelTriggerNode.getRootNode().activeElement ===
          this._panelTriggerNode;
      }
      if (!isFocusRestored && this.launcherVisible) {
        isFocusRestored = this.sidebarMain.focusButtonGroup();
      }
    }
    if (!isFocusRestored) {
      gBrowser.selectedBrowser.focus();
    }
    if (triggerNode) {
      updateToggleControlLabel(triggerNode);
    }
    this._panelTriggerNode = null;
    this.updateToolbarButton();
  },

  /**
   * Record to Glean when any of the sidebar panels is loaded or unloaded.
   *
   * @param {string} commandID
   * @param {boolean} opened
   */
  _recordPanelToggle(commandID, opened) {
    const sidebar = this.sidebars.get(commandID);
    if (!sidebar) {
      return;
    }
    const isExtension = sidebar && Object.hasOwn(sidebar, "extensionId");
    const version = this.sidebarRevampEnabled ? "new" : "old";
    if (isExtension) {
      const addonId = sidebar.extensionId;
      const addonName = WebExtensionPolicy.getByID(addonId)?.name;
      Glean.extension.sidebarToggle.record({
        opened,
        version,
        addon_id: AMTelemetry.getTrimmedString(addonId),
        addon_name: addonName && AMTelemetry.getTrimmedString(addonName),
      });
    } else if (sidebar.gleanEvent && sidebar.recordSidebarVersion) {
      sidebar.gleanEvent.record({ opened, version });
    } else if (sidebar.gleanEvent) {
      sidebar.gleanEvent.record({ opened });
    }
  },

  /**
   * Use MousePosTracker to manually check for hover state over launcher
   */
  _checkIsHoveredOverLauncher() {
    // Manually check mouse position
    let isHovered;
    MousePosTracker._callListener({
      onMouseEnter: () => (isHovered = true),
      onMouseLeave: () => (isHovered = false),
      getMouseTargetRect: () => this.getMouseTargetRect(),
    });
    return isHovered;
  },

  /**
   * Record to Glean when any of the sidebar icons are clicked.
   *
   * @param {string} commandID - Command ID of the icon.
   * @param {boolean} expanded - Whether the sidebar was expanded when clicked.
   */
  recordIconClick(commandID, expanded) {
    const sidebar = this.sidebars.get(commandID);
    const isExtension = sidebar && Object.hasOwn(sidebar, "extensionId");
    if (isExtension) {
      const addonId = sidebar.extensionId;
      Glean.sidebar.addonIconClick.record({
        sidebar_open: expanded,
        addon_id: AMTelemetry.getTrimmedString(addonId),
      });
    } else if (sidebar.gleanClickEvent) {
      sidebar.gleanClickEvent.record({
        sidebar_open: expanded,
      });
    }
  },

  /**
   * Sets the checked state only on the menu items of the specified sidebar, or
   * none if the argument is an empty string.
   */
  selectMenuItem(commandID) {
    for (let [id, { menuId, triggerButtonId }] of this.sidebars) {
      let menu = document.getElementById(menuId);
      if (!menu) {
        continue;
      }
      let triggerbutton =
        triggerButtonId && document.getElementById(triggerButtonId);
      if (id == commandID) {
        menu.setAttribute("checked", "true");
        if (triggerbutton) {
          triggerbutton.setAttribute("checked", "true");
          updateToggleControlLabel(triggerbutton);
        }
      } else {
        menu.removeAttribute("checked");
        if (triggerbutton) {
          triggerbutton.removeAttribute("checked");
          updateToggleControlLabel(triggerbutton);
        }
      }
    }
  },

  toggleTabstrip() {
    let toVerticalTabs = CustomizableUI.verticalTabsEnabled;
    let tabStrip = gBrowser.tabContainer;
    let arrowScrollbox = tabStrip.arrowScrollbox;
    let currentScrollOrientation = arrowScrollbox.getAttribute("orient");

    if (
      (!toVerticalTabs && currentScrollOrientation !== "vertical") ||
      (toVerticalTabs && currentScrollOrientation === "vertical")
    ) {
      // Nothing to update
      return;
    }

    if (toVerticalTabs) {
      arrowScrollbox.setAttribute("orient", "vertical");
      tabStrip.setAttribute("orient", "vertical");
      this._clearToolbarButtonBadge();
    } else {
      arrowScrollbox.setAttribute("orient", "horizontal");
      tabStrip.removeAttribute("expanded");
      tabStrip.setAttribute("orient", "horizontal");
    }

    let verticalToolbar = document.getElementById(
      CustomizableUI.AREA_VERTICAL_TABSTRIP
    );
    verticalToolbar.toggleAttribute("visible", toVerticalTabs);
    // Re-render sidebar-main so that templating is updated
    // for proper keyboard navigation for Tools
    this.sidebarMain.requestUpdate();
    if (!toVerticalTabs) {
      // Horizontal tabs have no expanded launcher state. If the visibility pref
      // isn't changing as part of this orientation switch, launcherExpanded
      // won't be updated by its observer, so un-expand the launcher here.
      this._state.launcherExpanded = false;
    }
  },

  /**
   * Run callback as the update of a view transition, if animations are enabled.
   * The callback must not wait on a panel's document load, as rendering is
   * suspended until it resolves.
   *
   * @param {Function} callback
   * @param {string[]} [types]
   *   Transition types that must already apply when the old state is captured.
   * @returns {Promise}
   */
  async _maybeAnimate(callback, types = []) {
    if (
      !this.sidebarRevampEnabled ||
      !this._animationEnabled ||
      window.gReduceMotion ||
      this._inViewTransitionUpdate
    ) {
      return callback();
    }
    // Set synchronously, so waitUntilStable() called right after this also
    // waits for a transition that hasn't started yet.
    let promise = this._startViewTransition(callback, types);
    this._viewTransitionPromise = promise;
    try {
      return await promise;
    } finally {
      if (this._viewTransitionPromise == promise) {
        this._viewTransitionPromise = null;
      }
    }
  },

  /**
   * @param {Function} callback
   *   The update callback of the view transition.
   * @param {string[]} types
   *   Types to start the transition with.
   * @returns {Promise}
   *   Resolves once the transition has finished.
   */
  async _startViewTransition(callback, types) {
    await this.sidebarMain.updateComplete;
    let wasOpen = this.isOpen;
    let previousID = this.currentID;
    let wasLauncherVisible = this._state.launcherVisible;
    let wasLauncherExpanded = this._state.launcherExpanded;
    let wasContentCornerRounded = this._isContentCornerRounded();
    let transition = document.startViewTransition({
      types,
      update: async () => {
        this._inViewTransitionUpdate = true;
        try {
          await callback();
        } finally {
          this._inViewTransitionUpdate = false;
        }
        let { launcherVisible, launcherExpanded } = this._state;
        if (launcherExpanded != wasLauncherExpanded) {
          transition.types.add(launcherExpanded ? "expand" : "collapse");
        }
        if (launcherVisible != wasLauncherVisible) {
          transition.types.add(
            launcherVisible ? "launcher-enter" : "launcher-exit"
          );
          transition.types.add(
            launcherVisible ? "launcher-show" : "launcher-hide"
          );
        } else if (launcherVisible && launcherExpanded != wasLauncherExpanded) {
          transition.types.add(
            launcherExpanded ? "launcher-enter" : "launcher-exit"
          );
        }
        if (this.isOpen != wasOpen) {
          transition.types.add(this.isOpen ? "panel-open" : "panel-close");
        } else if (this.isOpen && this.currentID != previousID) {
          transition.types.add("panel-switch");
        }
        let contentCornerRounded = this._isContentCornerRounded();
        if (contentCornerRounded != wasContentCornerRounded) {
          transition.types.add(
            contentCornerRounded
              ? "content-corner-round"
              : "content-corner-square"
          );
          if (this._isOppositeContentCornerRounded()) {
            transition.types.add("content-corner-opposite-round");
          }
          // The view transition pseudo-elements inherit from the root, while
          // themes such as the Smart Window one set the separator color lower.
          document.documentElement.style.setProperty(
            "--sidebar-content-separator-color",
            getComputedStyle(this.contentArea).getPropertyValue(
              "--chrome-content-separator-color"
            )
          );
        }
        await this.sidebarMain.updateComplete;
      },
    });
    this._activeViewTransition = transition;
    // Starting another transition skips this one and rejects its ready promise.
    transition.ready.catch(() => {});
    return transition.finished.finally(() => {
      if (this._activeViewTransition == transition) {
        this._activeViewTransition = null;
        document.documentElement.style.removeProperty(
          "--sidebar-content-separator-color"
        );
        if (document.documentElement.hasAttribute("sidebar-expand-on-hover")) {
          this._reconcileHoverState();
        }
      }
    });
  },

  /**
   * Whether content-area.css rounds the corner of the content area that faces
   * the sidebar, which it does while the launcher or a panel is next to it.
   * An open AI window sidebar also keeps the end corner rounded. Split view
   * panels are always rounded.
   *
   * @returns {boolean}
   */
  _isContentCornerRounded() {
    const contentArea = document.getElementById("tabbrowser-tabbox");
    return (
      contentArea.hasAttribute("sidebar-shown") ||
      contentArea.hasAttribute("sidebar-panel-open") ||
      (contentArea.hasAttribute("sidebar-positionend") &&
        contentArea.hasAttribute("ai-window-open")) ||
      !!gBrowser.selectedBrowser.closest(".split-view-panel")
    );
  },

  /**
   * Whether content-area.css rounds the corner of the content area opposite
   * the sidebar, which it does while the AI window sidebar is docked there.
   *
   * @returns {boolean}
   */
  _isOppositeContentCornerRounded() {
    return document
      .getElementById("tabbrowser-tabbox")
      .hasAttribute("ai-window-open");
  },

  debouncedMouseEnter() {
    const contentArea = document.getElementById("tabbrowser-tabbox");
    this._box.toggleAttribute("sidebar-launcher-hovered", true);
    contentArea.toggleAttribute("sidebar-launcher-hovered", true);
    this._maybeAnimate(() => {
      this._state.launcherHoverActive = true;
      this._state.launcherExpanded = true;
    }, ["expand-on-hover"]);
    this._mouseEnterDeferred.resolve();
  },

  _collapseLauncher() {
    this.mouseEnterTask?.disarm();
    this._mouseEnterDeferred?.resolve();
    const contentArea = document.getElementById("tabbrowser-tabbox");
    this._box.toggleAttribute("sidebar-launcher-hovered", false);
    contentArea.toggleAttribute("sidebar-launcher-hovered", false);
    this._maybeAnimate(() => {
      this._state.launcherHoverActive = false;
      this._state.launcherExpanded = false;
    }, ["expand-on-hover"]);
  },

  collapseOnEscape() {
    if (
      this.sidebarRevampVisibility !== "expand-on-hover" ||
      !this._state.launcherExpanded
    ) {
      return;
    }
    this._escapedWhileHovered = true;
    this._mouseLeftSinceEscape = false;
    this._collapseLauncher();
  },

  onMouseLeave() {
    // Ignore hover changes while animating or while a popup is open; the state
    // is reconciled once the animation settles or the popup closes.
    if (this._activeViewTransition || this._isMenuPopupOpen()) {
      return;
    }
    if (this._escapedWhileHovered) {
      this._mouseLeftSinceEscape = true;
      return;
    }
    if (!this._state.launcherExpanded) {
      return;
    }
    this._collapseLauncher();
  },

  onMouseEnter() {
    if (this._state.launcherExpanded) {
      return;
    }
    if (this._activeViewTransition || this._isMenuPopupOpen()) {
      return;
    }
    if (this._escapedWhileHovered) {
      if (this._mouseLeftSinceEscape) {
        this._escapedWhileHovered = false;
        this._mouseLeftSinceEscape = false;
      } else {
        return;
      }
    }
    this._mouseEnterDeferred = Promise.withResolvers();
    this.mouseEnterTask = new DeferredTask(
      () => {
        let isHovered = this._checkIsHoveredOverLauncher();
        // Only expand sidebar if mouse is still hovering over sidebar launcher
        if (isHovered) {
          this.debouncedMouseEnter();
        }
      },
      this._animationExpandOnHoverDelayDurationMs,
      EXPAND_ON_HOVER_DEBOUNCE_TIMEOUT_MS
    );
    this.mouseEnterTask?.arm();
  },

  get expandOnHoverComplete() {
    return this._mouseEnterDeferred?.promise || Promise.resolve();
  },

  refreshLauncherCollapsedWidth() {
    if (
      !document.documentElement.hasAttribute("sidebar-expand-on-hover") ||
      !this._state
    ) {
      this._launcherCollapsedWidthStale = false;
      return;
    }
    if (this.getUIState()?.launcherExpanded) {
      this._launcherCollapsedWidthStale = true;
      return;
    }
    this._launcherCollapsedWidthStale = false;
    this.setLauncherCollapsedWidth();
  },

  /**
   * Record the launcher's collapsed width, which the content area's
   * compensating margins are derived from while the launcher is expanded and
   * therefore out of flow.
   *
   * The resize observer that drives this watches #sidebar-container, so a burst
   * of resizes (e.g. a uidensity change) starts several overlapping
   * runs, with each awaiting before measuring. Unless every run checks that
   * it is still the most recent one, they can resolve out of order and leave a
   * stale width recorded.
   */
  async setLauncherCollapsedWidth() {
    let browserEl = document.getElementById("browser");
    const measurementID = ++this._collapsedWidthMeasurementID;
    if (this.getUIState().launcherExpanded) {
      this._state.launcherExpanded = false;
    }
    await this.waitUntilStable();
    let collapsedWidth = await window.promiseDocumentFlushed(
      () => this.sidebarContainer.getBoundingClientRect().width
    );

    if (measurementID !== this._collapsedWidthMeasurementID) {
      // A later run superseded us while we were waiting.
      return;
    }
    if (this._state.launcherExpanded) {
      // The launcher expanded again while we were waiting, so what we just
      // measured isn't a collapsed width.
      return;
    }

    browserEl.style.setProperty(
      "--sidebar-launcher-collapsed-width",
      `${collapsedWidth}px`
    );
  },

  getMouseTargetRect() {
    let launcherRect = window.windowUtils.getBoundsWithoutFlushing(
      SidebarController.sidebarMain
    );
    return {
      top: launcherRect.top,
      bottom: launcherRect.bottom,
      left: this._positionStart
        ? launcherRect.left
        : launcherRect.left + LAUNCHER_SPLITTER_WIDTH,
      right: this._positionStart
        ? launcherRect.right - LAUNCHER_SPLITTER_WIDTH
        : launcherRect.right,
    };
  },

  handleEvent(e) {
    switch (e.type) {
      case "popupshown":
        if (e.composedTarget.tagName !== "tooltip") {
          this._openPopups.add(e.composedTarget);
        }
        break;
      case "popuphidden":
        if (e.composedTarget.tagName !== "tooltip") {
          this._openPopups.delete(e.composedTarget);
          this._reconcileHoverState();
        }
        break;
      case "uidensitychanged":
        this.refreshLauncherCollapsedWidth();
        break;
      default:
        break;
    }
  },

  _updateExpandOnHoverDuration() {
    document.documentElement.style.setProperty(
      "--sidebar-expand-on-hover-duration",
      `${this._animationExpandOnHoverDurationMs}ms`
    );
  },

  async toggleExpandOnHover(isEnabled, isDragEnded) {
    const toggleID = ++this._expandOnHoverToggleID;
    if (isEnabled) {
      this._updateExpandOnHoverDuration();
    }
    document.documentElement.toggleAttribute(
      "sidebar-expand-on-hover",
      isEnabled
    );
    if (isEnabled) {
      if (!this._state) {
        this._state = new this.SidebarState(this);
      }
      await this.waitUntilStable();
      if (toggleID !== this._expandOnHoverToggleID) {
        // A later call superseded us while we were awaiting. It has already put
        // the attribute and the listeners into its own state, so stop rather
        // than reinstating ours over it.
        return;
      }
      MousePosTracker.addListener(this);
      if (!isDragEnded) {
        await this.setLauncherCollapsedWidth();
        if (toggleID !== this._expandOnHoverToggleID) {
          return;
        }
      }
      document.addEventListener("popupshown", this);
      document.addEventListener("popuphidden", this);
      window.addEventListener("uidensitychanged", this);
      // Reset user-preferred height
      this.sidebarMain.buttonsWrapper.style.height = this._state
        .launcherExpanded
        ? ""
        : "0";
    } else {
      this._openPopups.clear();
      MousePosTracker.removeListener(this);
      if (!this.mouseOverTask?.isFinalized) {
        this.mouseOverTask?.finalize();
      }
      document.removeEventListener("popupshown", this);
      document.removeEventListener("popuphidden", this);
      window.removeEventListener("uidensitychanged", this);
      this._launcherCollapsedWidthStale = false;
      this._state.updateToolsHeight();
    }

    document.documentElement.toggleAttribute(
      "sidebar-expand-on-hover",
      isEnabled
    );
  },

  /**
   * Report visibility preference to Glean.
   *
   * @param {string} [value] - The preference value.
   */
  recordVisibilitySetting(value = this.sidebarRevampVisibility) {
    let visibilitySetting = "hide";
    if (value === "always-show") {
      visibilitySetting = "always";
    } else if (value === "expand-on-hover") {
      visibilitySetting = "expand-on-hover";
    }
    Glean.sidebar.displaySettings.set(visibilitySetting);
  },

  /**
   * Report position preference to Glean.
   *
   * @param {boolean} [value] - The preference value.
   */
  recordPositionSetting(value = this._positionStart) {
    Glean.sidebar.positionSettings.set(value !== RTL_UI ? "left" : "right");
  },

  /**
   * Report tabs layout preference to Glean.
   *
   * @param {boolean} [value] - The preference value.
   */
  recordTabsLayoutSetting(value = this.sidebarVerticalTabsEnabled) {
    Glean.sidebar.tabsLayout.set(value ? "vertical" : "horizontal");
  },
};

ChromeUtils.defineESModuleGetters(SidebarController, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  SidebarManager:
    "moz-src:///browser/components/sidebar/SidebarManager.sys.mjs",
  SidebarState: "moz-src:///browser/components/sidebar/SidebarState.sys.mjs",
  SidebarPermissions:
    "chrome://browser/content/sidebar/sidebar-permissions.mjs",
});

// Add getters related to the position here, since we will want them
// available for both startDelayedLoad and init.
XPCOMUtils.defineLazyPreferenceGetter(
  SidebarController,
  "_positionStart",
  SidebarController.POSITION_START_PREF,
  true,
  (_aPreference, _previousValue, newValue) => {
    if (
      !SidebarController.uninitializing &&
      !SidebarController.inSingleTabWindow
    ) {
      SidebarController.recordPositionSetting(newValue);
      // When the user changes the setting, this runs within their input event,
      // so it animates.
      SidebarController._maybeAnimate(() => {
        SidebarController.setPosition();
      });
    }
  }
);
XPCOMUtils.defineLazyPreferenceGetter(
  SidebarController,
  "_animationEnabled",
  "sidebar.animation.enabled",
  true
);
XPCOMUtils.defineLazyPreferenceGetter(
  SidebarController,
  "_animationExpandOnHoverDurationMs",
  "sidebar.animation.expand-on-hover.duration-ms",
  200,
  () => SidebarController._updateExpandOnHoverDuration()
);
XPCOMUtils.defineLazyPreferenceGetter(
  SidebarController,
  "_animationExpandOnHoverDelayDurationMs",
  "sidebar.animation.expand-on-hover.delay-duration-ms",
  200
);
XPCOMUtils.defineLazyPreferenceGetter(
  SidebarController,
  "sidebarRevampEnabled",
  "sidebar.revamp",
  false,
  (_aPreference, _previousValue, newValue) => {
    if (!SidebarController.uninitializing) {
      SidebarController.toggleRevampSidebar(newValue);
    }
  }
);
XPCOMUtils.defineLazyPreferenceGetter(
  SidebarController,
  "updatedBookmarksPanelEnabled",
  "sidebar.updatedBookmarks.enabled",
  false,
  (_aPreference, _previousValue, newValue) => {
    if (!SidebarController.uninitializing) {
      const bookmarksSidebar = SidebarController.sidebars.get(
        "viewBookmarksSidebar"
      );
      if (bookmarksSidebar) {
        bookmarksSidebar.url =
          SidebarController.sidebarRevampEnabled && newValue
            ? "chrome://browser/content/sidebar/sidebar-bookmarks.html"
            : "chrome://browser/content/places/bookmarksSidebar.xhtml";
        if (SidebarController.currentID === "viewBookmarksSidebar") {
          SidebarController.show("viewBookmarksSidebar");
        }
      }
    }
  }
);
XPCOMUtils.defineLazyPreferenceGetter(
  SidebarController,
  "sidebarRevampTools",
  "sidebar.main.tools",
  "",
  () => {
    if (
      !SidebarController.inSingleTabWindow &&
      !SidebarController.uninitializing
    ) {
      SidebarController.refreshTools();
    }
  }
);
XPCOMUtils.defineLazyPreferenceGetter(
  SidebarController,
  "installedExtensions",
  "sidebar.installed.extensions",
  ""
);

XPCOMUtils.defineLazyPreferenceGetter(
  SidebarController,
  "sidebarRevampVisibility",
  "sidebar.visibility",
  "always-show",
  (_aPreference, _previousValue, newValue) => {
    if (
      !SidebarController.inSingleTabWindow &&
      !SidebarController.uninitializing
    ) {
      SidebarController.toggleExpandOnHover(newValue === "expand-on-hover");
      SidebarController.recordVisibilitySetting(newValue);
      if (SidebarController._state) {
        SidebarController._state.enterVisibilityMode(newValue);
        SidebarController.updatePinnedTabsHeightAfterReflow();
      }
      SidebarController.updateToolbarButton();
    }
  }
);

XPCOMUtils.defineLazyPreferenceGetter(
  SidebarController,
  "sidebarVerticalTabsEnabled",
  "sidebar.verticalTabs",
  false,
  (_aPreference, _previousValue, newValue) => {
    if (
      !SidebarController.uninitializing &&
      !SidebarController.inSingleTabWindow
    ) {
      SidebarController.recordTabsLayoutSetting(newValue);
      if (newValue) {
        SidebarController._enablePinnedTabsSplitterDragging();
      } else {
        SidebarController._disablePinnedTabsDragging();
      }
      SidebarController._state.updatePinnedTabsHeight();
      SidebarController._state.updateToolsHeight();
      SidebarController.updateToolbarButton();
    }
  }
);
