/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * SidebarController handles logic such as toggling sidebar panels,
 * dynamically adding menubar menu items for the View -> Sidebar menu,
 * and provides APIs for sidebar extensions, etc.
 */

var { ShoppingUtils } = ChromeUtils.importESModule(
  "resource:///modules/ShoppingUtils.sys.mjs"
);
const { DeferredTask } = ChromeUtils.importESModule(
  "resource://gre/modules/DeferredTask.sys.mjs"
);

const toolsNameMap = {
  viewGenaiChatSidebar: "aichat",
  viewTabsSidebar: "syncedtabs",
  viewHistorySidebar: "history",
  viewBookmarksSidebar: "bookmarks",
  viewReviewCheckerSidebar: "reviewchecker",
  viewCPMSidebar: "passwords",
};
const EXPAND_ON_HOVER_DEBOUNCE_RATE_MS = 200;
const EXPAND_ON_HOVER_DEBOUNCE_TIMEOUT_MS = 1000;
const LAUNCHER_SPLITTER_WIDTH = 4;

var SidebarController = {
  makeSidebar({ elementId, ...rest }) {
    return {
      get sourceL10nEl() {
        return document.getElementById(elementId);
      },
      get title() {
        let element = document.getElementById(elementId);
        return element?.getAttribute("label");
      },
      ...rest,
    };
  },

  registerPrefSidebar(pref, commandID, config) {
    const sidebar = this.makeSidebar(config);
    this._sidebars.set(commandID, sidebar);

    let switcherMenuitem;
    const updateMenus = visible => {
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
      "visible",
      pref,
      false,
      (_pref, _prev, val) => updateMenus(val)
    );
    this.promiseInitialized.then(() => updateMenus(sidebar.visible));
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
          elementId: "sidebar-switcher-tabs",
          url: this.sidebarRevampEnabled
            ? "chrome://browser/content/sidebar/sidebar-syncedtabs.html"
            : "chrome://browser/content/syncedtabs/sidebar.xhtml",
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
          elementId: "sidebar-switcher-bookmarks",
          url: "chrome://browser/content/places/bookmarksSidebar.xhtml",
          menuId: "menu_bookmarksSidebar",
          keyId: "viewBookmarksSidebarKb",
          menuL10nId: "menu-view-bookmarks",
          revampL10nId: "sidebar-menu-bookmarks-label",
          iconUrl: "chrome://browser/skin/bookmark-hollow.svg",
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
        elementId: "sidebar-switcher-genai-chat",
        url: "chrome://browser/content/genai/chat.html",
        keyId: "viewGenaiChatSidebarKb",
        menuId: "menu_genaiChatSidebar",
        menuL10nId: "menu-view-genai-chat",
        // Bug 1900915 to expose as conditional tool
        revampL10nId: "sidebar-menu-genai-chat-label",
        iconUrl: "chrome://global/skin/icons/highlights.svg",
        gleanClickEvent: Glean.sidebar.chatbotIconClick,
      }
    );

    if (!PrivateBrowsingUtils.isWindowPrivate(window)) {
      this.registerPrefSidebar(
        "browser.shopping.experience2023.integratedSidebar",
        "viewReviewCheckerSidebar",
        {
          elementId: "sidebar-switcher-review-checker",
          url: "chrome://browser/content/shopping/review-checker.xhtml",
          menuId: "menu_reviewCheckerSidebar",
          menuL10nId: "menu-view-review-checker",
          revampL10nId: "sidebar-menu-review-checker-label",
          iconUrl: "chrome://browser/content/shopping/assets/shopping.svg",
          gleanEvent: Glean.shopping.sidebarToggle,
          gleanClickEvent: Glean.sidebar.shoppingReviewCheckerIconClick,
          recordSidebarVersion: true,
        }
      );
    }

    this.registerPrefSidebar(
      "browser.contextual-password-manager.enabled",
      "viewCPMSidebar",
      {
        elementId: "sidebar-switcher-megalist",
        url: "chrome://global/content/megalist/megalist.html",
        menuId: "menu_megalistSidebar",
        menuL10nId: "menu-view-contextual-password-manager",
        revampL10nId: "sidebar-menu-contextual-password-manager-label",
        iconUrl: "chrome://browser/skin/login.svg",
        gleanEvent: Glean.contextualManager.sidebarToggle,
      }
    );

    if (this.sidebarRevampEnabled) {
      this._sidebars.set("viewCustomizeSidebar", {
        url: "chrome://browser/content/sidebar/sidebar-customize.html",
        revampL10nId: "sidebar-menu-customize-label",
        iconUrl: "chrome://global/skin/icons/settings.svg",
        gleanEvent: Glean.sidebarCustomize.panelToggle,
        visible: false,
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
  TOOLS_PREF: "sidebar.main.tools",
  VISIBILITY_PREF: "sidebar.visibility",

  // lastOpenedId is set in show() but unlike currentID it's not cleared out on hide
  // and isn't persisted across windows
  lastOpenedId: null,

  _box: null,
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
  _mainResizeObserver: null,
  _ongoingAnimations: [],

  /**
   * @type {MutationObserver | null}
   */
  _observer: null,

  _initDeferred: Promise.withResolvers(),

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
      window.document.documentElement.hasAttribute("taskbartab")
    );
  },

  get sidebarContainer() {
    if (!this._sidebarContainer) {
      // This is the *parent* of the `sidebar-main` component.
      // TODO: Rename this element in the markup in order to avoid confusion. (Bug 1904860)
      this._sidebarContainer = document.getElementById("sidebar-main");
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

  init() {
    // Initialize global state manager.
    this.SidebarManager;

    // Initialize per-window state manager.
    if (!this._state) {
      this._state = new this.SidebarState(this);
    }

    this._box = document.getElementById("sidebar-box");
    this._splitter = document.getElementById("sidebar-splitter");
    this._launcherSplitter = document.getElementById(
      "sidebar-launcher-splitter"
    );
    this._reversePositionButton = document.getElementById(
      "sidebar-reverse-position"
    );
    this._switcherPanel = document.getElementById("sidebarMenu-popup");
    this._switcherTarget = document.getElementById("sidebar-switcher-target");
    this._switcherArrow = document.getElementById("sidebar-switcher-arrow");
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

    if (this.sidebarRevampEnabled) {
      if (!customElements.get("sidebar-main")) {
        ChromeUtils.importESModule(
          "chrome://browser/content/sidebar/sidebar-main.mjs",
          { global: "current" }
        );
      }
      this.revampComponentsLoaded = true;
      this._state.initializeState();
      document.getElementById("sidebar-header").hidden = true;
      if (!this._mainResizeObserverAdded) {
        this._mainResizeObserver.observe(this.sidebarMain);
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
      this._enableLauncherDragging();

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

    requestIdleCallback(() => {
      const windowPrivacyMatches =
        !window.opener || this.windowPrivacyMatches(window.opener, window);
      // If other sources (like session store or source window) haven't set the
      // UI state at this point, load the backup state. (Do not load the backup
      // state if this is a popup, or we are coming from a window of a different
      // privacy level.)
      if (
        !this.uiStateInitialized &&
        !this.inSingleTabWindow &&
        (this.sidebarRevampEnabled || windowPrivacyMatches)
      ) {
        const backupState = this.SidebarManager.getBackupState();
        this.initializeUIState(backupState);
      }
    });
    this._initDeferred.resolve();
  },

  uninit() {
    // Set a flag to allow us to ignore pref changes while the host document is being unloaded.
    this._uninitializing = true;

    // If this is the last browser window, persist various values that should be
    // remembered for after a restart / reopening a browser window.
    let enumerator = Services.wm.getEnumerator("navigator:browser");
    if (!enumerator.hasMoreElements()) {
      let xulStore = Services.xulStore;
      xulStore.persist(this._title, "value");

      const currentState = this.getUIState();
      this.SidebarManager.setBackupState(currentState);
    }

    Services.obs.removeObserver(this, "intl:app-locales-changed");
    Services.obs.removeObserver(this, "tabstrip-orientation-change");
    delete this._tabstripOrientationObserverAdded;

    CustomizableUI.removeListener(this);

    if (this._observer) {
      this._observer.disconnect();
      this._observer = null;
    }

    if (this._mainResizeObserver) {
      this._mainResizeObserver.disconnect();
      this._mainResizeObserver = null;
    }

    if (this.revampComponentsLoaded) {
      // Explicitly disconnect the `sidebar-main` element so that listeners
      // setup by reactive controllers will also be removed.
      this.sidebarMain.remove();
    }
    this._splitter.removeEventListener("command", this._browserResizeObserver);
    this._disableLauncherDragging();
  },

  /**
   * Handle the launcher being resized (either manually or programmatically).
   *
   * @param {ResizeObserverEntry} entry
   */
  _handleLauncherResize(entry) {
    this._state.launcherWidth = entry.contentBoxSize[0].inlineSize;
    if (this.isLauncherDragging) {
      this._state.launcherDragActive = true;
    }
    if (this._state.visibilitySetting === "expand-on-hover") {
      this.setLauncherCollapsedWidth();
    }
  },

  getUIState() {
    if (this.inSingleTabWindow) {
      return null;
    }
    let snapshot = this._state.getProperties();
    // we don't persist the sidebar command when the panel is closed
    if (!this._state.panelOpen) {
      delete snapshot.command;
    }
    return snapshot;
  },

  /**
   * Load the UI state information given by session store, backup state, or
   * adopted window.
   *
   * @param {SidebarStateProps} state
   */
  async initializeUIState(state) {
    if (!state) {
      return;
    }
    const isValidSidebar = !state.command || this.sidebars.has(state.command);
    if (!isValidSidebar) {
      state.command = "";
    }

    const hasOpenPanel =
      state.panelOpen &&
      state.command &&
      this.sidebars.has(state.command) &&
      this.currentID !== state.command;
    if (hasOpenPanel) {
      // There's a panel to show, so ignore the contradictory hidden property.
      delete state.hidden;
    }
    await this.promiseInitialized;
    await this.waitUntilStable(); // Finish currently scheduled tasks.
    await this._state.loadInitialState(state);
    await this.waitUntilStable(); // Finish newly scheduled tasks.
    this.updateToolbarButton();
    if (this.sidebarRevampVisibility === "expand-on-hover") {
      await this.toggleExpandOnHover(true);
    }
    this.uiStateInitialized = true;
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
  observe(_subject, topic, _data) {
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
      case "tabstrip-orientation-change": {
        this.promiseInitialized.then(() => this.toggleTabstrip());
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
    [...browser.children].forEach((node, i) => {
      node.style.order = i + 1;
    });
    let sidebarContainer = document.getElementById("sidebar-main");
    let sidebarMain = document.querySelector("sidebar-main");
    if (!this._positionStart) {
      // DOM ordering is:     sidebar-main | launcher-splitter | sidebar-box | splitter | tabbrowser-tabbox
      // Want to display as:  tabbrowser-tabbox | splitter |  sidebar-box  | launcher-splitter | sidebar-main
      // First switch order of sidebar-main and tabbrowser-tabbox
      let mainOrdinal = this.sidebarContainer.style.order;
      this.sidebarContainer.style.order = contentArea.style.order;
      contentArea.style.order = mainOrdinal;
      // Then swap launcher-splitter and splitter
      let splitterOrdinal = this._splitter.style.order;
      this._splitter.style.order = this._launcherSplitter.style.order;
      this._launcherSplitter.style.order = splitterOrdinal;
    }
    // Indicate we've switched ordering to the box
    this._box.toggleAttribute("sidebar-positionend", !this._positionStart);
    sidebarMain.toggleAttribute("sidebar-positionend", !this._positionStart);
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
  async toggleRevampSidebar() {
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
      this._state.launcherVisible = false;
      document.getElementById("sidebar-header").hidden = false;
      // Disable vertical tabs if revamped sidebar is turned off
      if (this.sidebarVerticalTabsEnabled) {
        Services.prefs.setBoolPref("sidebar.verticalTabs", false);
      }
    } else {
      // initial launcher visibleness with sidebar.revamp is is one of the
      // default properties managed by SidebarState
      this._state.launcherVisible = this._state.defaultLauncherVisible;
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
  async adoptFromWindow(sourceWindow) {
    // If the opener had a sidebar, open the same sidebar in our window.
    // The opener can be the hidden window too, if we're coming from the state
    // where no windows are open, and the hidden window has no sidebar box.
    let sourceController = sourceWindow.SidebarController;
    if (!sourceController || !sourceController._box) {
      // no source UI or no _box means we also can't adopt the state.
      return false;
    }

    // If window is a popup, hide the sidebar
    if (this.inSingleTabWindow && this.sidebarRevampEnabled) {
      document.getElementById("sidebar-main").hidden = true;
      return false;
    }
    // Adopt the other window's UI state (it too could be a popup)
    // We get the properties directly forom the SidebarState instance as in this case
    // we need the command property even if no panel is currently open.
    const sourceState = sourceController.inPopup
      ? null
      : sourceController._state?.getProperties();
    await this.initializeUIState(sourceState);

    return true;
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
      this._state.launcherVisible = false;
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
        return;
      }
      // Try to adopt the sidebar state from the source window
      if (await this.adoptFromWindow(sourceWindow)) {
        this.uiStateInitialized = true;
        return;
      }
    }

    // If we're not adopting settings from a parent window, set them now.
    let wasOpen = this._box.getAttribute("checked");
    if (!wasOpen) {
      return;
    }

    let commandID = this._state.command;
    if (commandID && this.sidebars.has(commandID)) {
      this.showInitially(commandID);
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
    this.uiStateInitialized = true;
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

  get launcherEverVisible() {
    return this._state?.launcherEverVisible;
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
      // For legacy sidebar, this is a "sidebar" toggle and the current panel should be remembered
      this.hide({ triggerNode, dismissPanel: this.sidebarRevampEnabled });
      this.updateToolbarButton();
      return Promise.resolve();
    }
    return this.show(commandID, triggerNode);
  },

  _getRects(animatingElements) {
    return animatingElements.map(e => [
      e.hidden,
      e.getBoundingClientRect().toJSON(),
    ]);
  },

  /**
   * Wait for Lit updates and ongoing animations to complete.
   *
   * @returns {Promise}
   */
  async waitUntilStable() {
    if (!this.sidebarRevampEnabled) {
      // Legacy sidebar doesn't have animations, nothing to await.
      return null;
    }
    const tasks = [this.sidebarMain.updateComplete];
    if (this._ongoingAnimations?.length) {
      tasks.push(
        ...this._ongoingAnimations.map(animation => animation.finished)
      );
    }
    return Promise.allSettled(tasks);
  },

  async _animateSidebarMain() {
    let tabbox = document.getElementById("tabbrowser-tabbox");
    let animatingElements;
    if (document.documentElement.hasAttribute("sidebar-expand-on-hover")) {
      animatingElements = [this.sidebarContainer];
    } else {
      animatingElements = [
        this.sidebarContainer,
        this._box,
        this._splitter,
        tabbox,
      ];
    }
    let resetElements = () => {
      for (let el of animatingElements) {
        el.style.minWidth =
          el.style.maxWidth =
          el.style.marginLeft =
          el.style.marginRight =
          el.style.display =
            "";
      }
      this.sidebarContainer.toggleAttribute(
        "sidebar-ongoing-animations",
        false
      );
      this._box.toggleAttribute("sidebar-ongoing-animations", false);
      tabbox.toggleAttribute("sidebar-ongoing-animations", false);
    };
    if (this._ongoingAnimations.length) {
      this._ongoingAnimations.forEach(a => a.cancel());
      this._ongoingAnimations = [];
      resetElements();
    }

    let fromRects = this._getRects(animatingElements);

    // We need to wait for lit to re-render, and us to get the final width.
    // This is a bit unfortunate but alas...
    await new Promise(resolve => {
      queueMicrotask(() => resolve(this.sidebarMain.updateComplete));
    });
    let toRects = this._getRects(animatingElements);

    const options = {
      duration: document.documentElement.hasAttribute("sidebar-expand-on-hover")
        ? this._animationExpandOnHoverDurationMs
        : this._animationDurationMs,
      easing: "ease-in-out",
    };
    let animations = [];
    let sidebarOnLeft = this._positionStart != RTL_UI;
    let sidebarShift = 0;
    for (let i = 0; i < animatingElements.length; ++i) {
      const el = animatingElements[i];
      const [wasHidden, from] = fromRects[i];
      const [isHidden, to] = toRects[i];

      // For the sidebar, we need some special cases to make the animation
      // nicer (keeping the icon positions).
      const isSidebar = el === this.sidebarContainer;

      if (wasHidden != isHidden) {
        if (wasHidden) {
          from.left = from.right = sidebarOnLeft ? to.left : to.right;
        } else {
          to.left = to.right = sidebarOnLeft ? from.left : from.right;
        }
      }
      const widthGrowth = to.width - from.width;
      if (isSidebar) {
        sidebarShift = widthGrowth;
      }

      let fromTranslate = sidebarOnLeft
        ? from.left - to.left
        : from.right - to.right;
      let toTranslate = 0;

      // We fix the element to the larger width during the animation if needed,
      // but keeping the right flex width, and thus our original position, with
      // a negative margin.
      el.style.minWidth =
        el.style.maxWidth =
        el.style.marginLeft =
        el.style.marginRight =
        el.style.display =
          "";
      if (isHidden && !wasHidden) {
        el.style.display = "flex";
      }

      if (widthGrowth < 0) {
        el.style.minWidth = el.style.maxWidth = from.width + "px";
        el.style["margin-" + (sidebarOnLeft ? "right" : "left")] =
          widthGrowth + "px";
        if (isSidebar) {
          toTranslate = sidebarOnLeft ? widthGrowth : -widthGrowth;
        } else if (el === this._box) {
          // This is very hacky, but this code doesn't deal well with
          // more than two elements moving, and this is the less invasive change.
          // It would be better to treat "sidebar + sidebar-box" as a unit.
          // We only hit this when completely hiding the box.
          fromTranslate = sidebarOnLeft ? -sidebarShift : sidebarShift;
          toTranslate = sidebarOnLeft
            ? fromTranslate + widthGrowth
            : fromTranslate - widthGrowth;
        }
      } else if (isSidebar) {
        fromTranslate += sidebarOnLeft ? -widthGrowth : widthGrowth;
      }

      animations.push(
        el.animate(
          [
            { translate: `${fromTranslate}px 0 0` },
            { translate: `${toTranslate}px 0 0` },
          ],
          options
        )
      );
      if (!isSidebar || !this._positionStart) {
        continue;
      }
      // We want to keep the buttons in place during the animation, for which
      // we might need to compensate.
      if (!this._state.launcherExpanded) {
        animations.push(
          this.sidebarMain.animate(
            [{ translate: "0" }, { translate: `${-toTranslate}px 0 0` }],
            options
          )
        );
      } else {
        animations.push(
          this.sidebarMain.animate(
            [{ translate: `${-fromTranslate}px 0 0` }, { translate: "0" }],
            options
          )
        );
      }
    }
    this._ongoingAnimations = animations;
    this.sidebarContainer.toggleAttribute("sidebar-ongoing-animations", true);
    this._box.toggleAttribute("sidebar-ongoing-animations", true);
    tabbox.toggleAttribute("sidebar-ongoing-animations", true);
    await Promise.allSettled(animations.map(a => a.finished));
    if (this._ongoingAnimations === animations) {
      this._ongoingAnimations = [];
      resetElements();
    }
  },

  /**
   * For sidebar.revamp=true only, handle the keyboard or sidebar-button command to toggle the sidebar state
   */
  async handleToolbarButtonClick() {
    if (this.inSingleTabWindow || this.uninitializing) {
      return;
    }

    const initialExpandedValue = this._state.launcherExpanded;

    // What toggle means depends on the sidebar.visibility pref.
    const expandOnToggle = ["always-show", "expand-on-hover"].includes(
      this.sidebarRevampVisibility
    );

    // when the launcher is toggled open by the user, we disable expand-on-hover interactions.
    if (this.sidebarRevampVisibility === "expand-on-hover") {
      await this.toggleExpandOnHover(initialExpandedValue);
    }

    if (this._animationEnabled && !window.gReduceMotion) {
      this._animateSidebarMain();
    }

    if (expandOnToggle) {
      // just expand/collapse the launcher
      this._state.updateVisibility(true, !initialExpandedValue);
      this.updateToolbarButton();
      return;
    }

    const shouldShowLauncher = !this._state.launcherVisible;
    // show/hide the launcher
    this._state.updateVisibility(shouldShowLauncher);
    // if we're showing and there was panel open, open it again
    if (shouldShowLauncher && this._state.command) {
      await this.show(this._state.command);
    } else if (!shouldShowLauncher) {
      // hide will only update the toolbar button state if the panel was open
      if (!this.isOpen) {
        this.updateToolbarButton();
      }
      // hide the open panel. It will re-open next time as we don't change the command value
      this.hide({ dismissPanel: false });
    }
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
      switch (this.sidebarRevampVisibility) {
        case "always-show":
        case "expand-on-hover":
          // Toolbar button controls expanded state.
          toolbarButton.checked = this.sidebarMain.expanded;
          toolbarButton.dataset.l10nId = toolbarButton.checked
            ? "sidebar-widget-collapse-sidebar2"
            : "sidebar-widget-expand-sidebar2";
          break;
        case "hide-sidebar":
          // Toolbar button controls hidden state.
          toolbarButton.checked = !this.sidebarContainer.hidden;
          toolbarButton.dataset.l10nId = toolbarButton.checked
            ? "sidebar-widget-hide-sidebar2"
            : "sidebar-widget-show-sidebar2";
          break;
      }
    }
  },

  /**
   * Enable the splitter which can be used to resize the launcher.
   */
  _enableLauncherDragging() {
    if (!this._launcherSplitter.hidden) {
      // Already showing the launcher splitter with observers connected.
      // Nothing to do.
      return;
    }
    this._panelResizeObserver = new ResizeObserver(
      ([entry]) => (this._state.panelWidth = entry.contentBoxSize[0].inlineSize)
    );
    this._panelResizeObserver.observe(this._box);

    this._launcherDropHandler = () => (this._state.launcherDragActive = false);
    this._launcherSplitter.addEventListener(
      "command",
      this._launcherDropHandler
    );

    this._launcherSplitter.hidden = false;
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

    this._launcherSplitter.hidden = true;
  },

  _loadSidebarExtension(commandID) {
    let sidebar = this.sidebars.get(commandID);
    if (typeof sidebar?.onload === "function") {
      sidebar.onload();
    }
  },

  /**
   * Ensure tools reflect the current pref state
   */
  refreshTools() {
    let changed = false;
    const tools = new Set(this.sidebarRevampTools.split(","));
    this.toolsAndExtensions.forEach((tool, commandID) => {
      const toolID = toolsNameMap[commandID];
      if (toolID) {
        const expected = !tools.has(toolID);
        if (tool.disabled != expected) {
          tool.disabled = expected;
          changed = true;
        }
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
    let toggledTool = this.toolsAndExtensions.get(commandID);
    toggledTool.disabled = !toggledTool.disabled;
    if (!toggledTool.disabled) {
      // If re-enabling tool, remove from the map and add it to the end
      this.toolsAndExtensions.delete(commandID);
      this.toolsAndExtensions.set(commandID, toggledTool);
    }
    // Tools are persisted via a pref.
    if (!Object.hasOwn(toggledTool, "extensionId")) {
      const tools = new Set(this.sidebarRevampTools.split(","));
      const updatedTools = tools.has(toolsNameMap[commandID])
        ? Array.from(tools).filter(
            tool => !!tool && tool != toolsNameMap[commandID]
          )
        : [
            ...Array.from(tools).filter(tool => !!tool),
            toolsNameMap[commandID],
          ];
      Services.prefs.setStringPref(this.TOOLS_PREF, updatedTools.join());
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
      extensionToUpdate.icon = extension.icon;
      extensionToUpdate.iconUrl = extension.iconUrl;
      extensionToUpdate.tooltiptext = extension.label;
      window.dispatchEvent(new CustomEvent("SidebarItemChanged"));
    } else {
      // Add new extension
      this.toolsAndExtensions.set(commandID, {
        view: commandID,
        extensionId: extension.extensionId,
        icon: extension.icon,
        iconUrl: extension.iconUrl,
        tooltiptext: extension.label,
        disabled: false,
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
    const sidebar = {
      title: props.title,
      url: "chrome://browser/content/webext-panels.xhtml",
      menuId: props.menuId,
      switcherMenuId: `sidebarswitcher_menu_${commandID}`,
      keyId: `ext-key-id-${commandID}`,
      label: props.title,
      icon: props.icon,
      iconUrl: props.iconUrl,
      classAttribute: "menuitem-iconic webextension-menuitem",
      // The following properties are specific to extensions
      extensionId: props.extensionId,
      onload: props.onload,
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
      { icon: props.icon, iconUrl: props.iconUrl, label: props.title },
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
   * @param {string} attributes.icon
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
    { icon, iconUrl, label },
    sidebar,
    needsRefresh = false
  ) {
    sidebar.icon = icon;
    sidebar.iconUrl = iconUrl;
    sidebar.label = label;

    const updateAttributes = el => {
      el.style.setProperty("--webextension-menuitem-image", sidebar.icon);
      el.setAttribute("label", sidebar.label);
    };

    updateAttributes(document.getElementById(sidebar.menuId), sidebar);
    const switcherMenu = document.getElementById(sidebar.switcherMenuId);
    if (switcherMenu) {
      updateAttributes(switcherMenu, sidebar);
    }
    if (this.initialized && this.currentID === commandID) {
      // Update the sidebar title if this extension is the current sidebar.
      this.title = label;
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
        extensions.push({
          commandID,
          view: commandID,
          extensionId: sidebar.extensionId,
          iconUrl: sidebar.iconUrl,
          tooltiptext: sidebar.label,
          disabled: false,
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
        const disabled = !this.sidebarRevampTools
          .split(",")
          .includes(toolsNameMap[commandID]);
        return {
          commandID,
          view: commandID,
          iconUrl: sidebar.iconUrl,
          l10nId: sidebar.revampL10nId,
          disabled,
          // Reflect the current tool state defaulting to visible
          get hidden() {
            return !(sidebar.visible ?? true);
          },
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

    // Extensions without private window access wont be in the
    // sidebars map.
    if (!this.sidebars.has(commandID)) {
      return false;
    }
    return this._show(commandID).then(() => {
      this._loadSidebarExtension(commandID);

      if (triggerNode) {
        updateToggleControlLabel(triggerNode);
      }
      this.updateToolbarButton();

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
   * @returns {Promise<boolean>}
   */
  async showInitially(commandID) {
    if (this.inSingleTabWindow) {
      return false;
    }
    this._recordPanelToggle(commandID, true);

    // Extensions without private window access wont be in the
    // sidebars map.
    if (!this.sidebars.has(commandID)) {
      return false;
    }
    return this._show(commandID).then(() => {
      this._loadSidebarExtension(commandID);
      return true;
    });
  },

  /**
   * Implementation for show. Also used internally for sidebars that are shown
   * when a window is opened and we don't want to ping telemetry.
   *
   * @param {string} commandID ID of the sidebar.
   * @returns {Promise<void>}
   */
  _show(commandID) {
    return new Promise(resolve => {
      this._state.panelOpen = true;
      if (this.sidebarRevampEnabled) {
        this._box.dispatchEvent(
          new CustomEvent("sidebar-show", { detail: { viewId: commandID } })
        );
      } else {
        this.hideSwitcherPanel();
      }

      this.selectMenuItem(commandID);
      this._box.hidden = this._splitter.hidden = false;

      this._box.setAttribute("checked", "true");
      this._state.command = commandID;

      let { icon, url, title, sourceL10nEl, contextMenuId } =
        this.sidebars.get(commandID);
      if (icon) {
        this._switcherTarget.style.setProperty(
          "--webextension-menuitem-image",
          icon
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
      // These title changes only apply to the old sidebar menu
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
   * @param {boolean} options.dismissPanel -Only close the panel or close the whole sidebar (the default.)
   */
  hide({ triggerNode, dismissPanel = this.sidebarRevampEnabled } = {}) {
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

    let selBrowser = gBrowser.selectedBrowser;
    selBrowser.focus();
    if (triggerNode) {
      updateToggleControlLabel(triggerNode);
    }
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
    if (
      !this.verticalTabsEnabled &&
      this.sidebarRevampVisibility == "hide-sidebar"
    ) {
      // the sidebar.visibility pref didn't change so updateVisbility hasn't
      // been called; we need to call it here to un-expand the launcher
      this._state.updateVisibility(undefined, false);
    }
  },

  debouncedMouseEnter() {
    const contentArea = document.getElementById("tabbrowser-tabbox");
    this._box.toggleAttribute("sidebar-launcher-hovered", true);
    contentArea.toggleAttribute("sidebar-launcher-hovered", true);
    this._state.launcherHoverActive = true;
    if (this._animationEnabled && !window.gReduceMotion) {
      this._animateSidebarMain();
    }
    this._state.launcherExpanded = true;
  },

  onMouseLeave() {
    this.mouseEnterTask.disarm();
    const contentArea = document.getElementById("tabbrowser-tabbox");
    this._box.toggleAttribute("sidebar-launcher-hovered", false);
    contentArea.toggleAttribute("sidebar-launcher-hovered", false);
    this._state.launcherHoverActive = false;
    if (this._animationEnabled && !window.gReduceMotion) {
      this._animateSidebarMain();
    }
    this._state.launcherExpanded = false;
  },

  onMouseEnter() {
    this.mouseEnterTask = new DeferredTask(
      () => {
        this.debouncedMouseEnter();
      },
      EXPAND_ON_HOVER_DEBOUNCE_RATE_MS,
      EXPAND_ON_HOVER_DEBOUNCE_TIMEOUT_MS
    );
    this.mouseEnterTask?.arm();
  },

  async setLauncherCollapsedWidth() {
    let browserEl = document.getElementById("browser");
    if (this.getUIState().launcherExpanded) {
      this._state.launcherExpanded = false;
    }
    await this.waitUntilStable();
    let collapsedWidth = await new Promise(resolve => {
      requestAnimationFrame(() => {
        resolve(this._getRects([this.sidebarMain])[0][1].width);
      });
    });

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

  async toggleExpandOnHover(isEnabled, isDragEnded) {
    document.documentElement.toggleAttribute(
      "sidebar-expand-on-hover",
      isEnabled
    );
    if (isEnabled) {
      if (!this._state) {
        this._state = new this.SidebarState(this);
      }
      await this.waitUntilStable();
      MousePosTracker.addListener(this);
      if (!isDragEnded) {
        await this.setLauncherCollapsedWidth();
      }
    } else {
      MousePosTracker.removeListener(this);
      if (!this.mouseOverTask?.isFinalized) {
        this.mouseOverTask?.finalize();
      }
    }
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
  SidebarManager: "resource:///modules/SidebarManager.sys.mjs",
  SidebarState: "resource:///modules/SidebarState.sys.mjs",
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
      SidebarController.setPosition();
      SidebarController.recordPositionSetting(newValue);
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
  "_animationDurationMs",
  "sidebar.animation.duration-ms",
  200
);
XPCOMUtils.defineLazyPreferenceGetter(
  SidebarController,
  "_animationExpandOnHoverDurationMs",
  "sidebar.animation.expand-on-hover.duration-ms",
  400
);
XPCOMUtils.defineLazyPreferenceGetter(
  SidebarController,
  "sidebarRevampEnabled",
  "sidebar.revamp",
  false,
  (_aPreference, _previousValue, newValue) => {
    if (!SidebarController.uninitializing) {
      SidebarController.toggleRevampSidebar();
      SidebarController._state.revampEnabled = newValue;
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
        // we need to use the pref rather than SidebarController's getter here
        // as the getter might not have the new value yet
        const isVerticalTabs = Services.prefs.getBoolPref(
          "sidebar.verticalTabs"
        );
        SidebarController._state.revampVisibility = newValue;
        if (
          SidebarController._animationEnabled &&
          !window.gReduceMotion &&
          newValue !== "expand-on-hover"
        ) {
          SidebarController._animateSidebarMain();
        }

        // launcher is always initially expanded with vertical tabs unless we're doing expand-on-hover
        let forceExpand = false;
        if (
          isVerticalTabs &&
          ["always-show", "hide-sidebar"].includes(newValue)
        ) {
          forceExpand = true;
        }

        // horizontal tabs and hide-sidebar = visible initially.
        // vertical tab and hide-sidebar = not visible initially
        let showLauncher = true;
        if (newValue == "hide-sidebar" && isVerticalTabs) {
          showLauncher = false;
        }
        SidebarController._state.updateVisibility(showLauncher, forceExpand);
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
    }
  }
);

XPCOMUtils.defineLazyPreferenceGetter(
  SidebarController,
  "revampDefaultLauncherVisible",
  "sidebar.revamp.defaultLauncherVisible",
  false,
  (_aPreference, _previousValue, _newValue) => {
    if (
      !SidebarController.uninitializing &&
      !SidebarController.inSingleTabWindow
    ) {
      SidebarController._state.updateVisibility();
    }
  }
);
