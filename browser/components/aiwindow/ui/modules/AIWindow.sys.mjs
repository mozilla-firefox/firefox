/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * @import {SmartbarInput} from "chrome://browser/content/urlbar/SmartbarInput.mjs"
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { AIFeature } from "chrome://global/content/ml/AIFeature.sys.mjs";

export const AIWINDOW_URL = "chrome://browser/content/aiwindow/aiWindow.html";
const AIWINDOW_URI = Services.io.newURI(AIWINDOW_URL);
const FIRSTRUN_URL = "chrome://browser/content/aiwindow/firstrun.html";
const FIRSTRUN_URI = Services.io.newURI(FIRSTRUN_URL);
const PREF_SMARTWINDOW_ENABLED = "browser.smartwindow.enabled";
const PREF_SMARTWINDOW_CONSENT_TIME = "browser.smartwindow.tos.consentTime";
const PREF_AI_CONTROL_SMARTWINDOW = "browser.ai.control.smartWindow";
const PREF_AI_CONTROL_DEFAULT = "browser.ai.control.default";
const PREF_MEMORIES_CONVERSATION =
  "browser.smartwindow.memories.generateFromConversation";
const PREF_MEMORIES_HISTORY =
  "browser.smartwindow.memories.generateFromHistory";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AIWindowTabStatesManager:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowTabStatesManager.sys.mjs",
  AIWindowAccountAuth:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowAccountAuth.sys.mjs",
  AIWindowMenu:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowMenu.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  AIWindowUI:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs",
  ChatStore:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs",
  MemoryStore:
    "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs",
  NewTabPagePreloading:
    "moz-src:///browser/components/tabbrowser/NewTabPagePreloading.sys.mjs",
  ONLOGOUT_NOTIFICATION: "resource://gre/modules/FxAccountsCommon.sys.mjs",
  PanelMultiView:
    "moz-src:///browser/components/customizableui/PanelMultiView.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  SearchUIUtils: "moz-src:///browser/components/search/SearchUIUtils.sys.mjs",
  MemoriesSchedulers:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesSchedulers.sys.mjs",
  SmartWindowTelemetry:
    "moz-src:///browser/components/aiwindow/ui/modules/SmartWindowTelemetry.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "hasFirstrunCompleted",
  "browser.smartwindow.firstrun.hasCompleted"
);

/**
 * AI Window Service
 */

export const AIWindow = {
  _initialized: false,
  _windowStates: new WeakMap(),
  _aiWindowMenu: null,

  /**
   * A WeakMap<window, AIWindowTabStatesManager> that keeps references
   * of AIWindowTabStatesManager per window.
   */
  _aiWindowTabStateManagers: new WeakMap(),

  /**
   * Handles startup tasks
   */

  init(win) {
    if (!this._windowStates.has(win)) {
      this._windowStates.set(win, {});
      this.initializeAITabsToolbar(win);
      this._updateToolbarButtonPositions(win);
      this._initializeAskButtonOnToolbox(win);
      const windowArgs = win?.arguments?.[1];
      if (
        windowArgs instanceof Ci.nsIPropertyBag2 &&
        windowArgs.hasKey("aiwindow-trigger")
      ) {
        this.recordOpenWindowTelemetry(
          windowArgs.getPropertyAsAString("aiwindow-trigger"),
          win
        );
      }
    }

    if (
      !this._aiWindowTabStateManagers.has(win) &&
      this.isAIWindowActive(win)
    ) {
      this._aiWindowTabStateManagers.set(
        win,
        new lazy.AIWindowTabStatesManager(win)
      );
      this._markActiveStart(win);
    }

    if (this._initialized) {
      return;
    }

    lazy.PlacesUtils.observers.addListener(
      ["page-removed", "history-cleared"],
      this.handlePlacesEvents
    );

    ChromeUtils.defineLazyGetter(AIWindow, "chatStore", () => lazy.ChatStore);
    Services.obs.addObserver(this, lazy.ONLOGOUT_NOTIFICATION);
    Services.obs.addObserver(this, "tabstrip-orientation-change");
    lazy.SmartWindowTelemetry.init();
    this._initialized = true;

    // On startup/restart, if the first window initialized is an
    // AI window, we need to start the memories schedulers.
    if (this.isAIWindowActive(win)) {
      lazy.MemoriesSchedulers.maybeRunAndSchedule();
    }
  },

  handlePlacesEvents(events) {
    for (const event of events) {
      switch (event.type) {
        case "page-removed":
          // NOTE: event.isPartialVisistsRemoval is not mispelled, there's a typo
          // in tools/@types/generated/lib.gecko.dom.d.ts:~2932 (interface PlacesVisitRemovedInit)
          if (
            event.reason == PlacesVisitRemoved.REASON_DELETED &&
            !event.isPartialVisistsRemoval
          ) {
            lazy.ChatStore.deleteUrlFromMessages(event.url);
          }
          break;

        case "history-cleared":
          lazy.ChatStore.deleteAllUrlsFromMessages();
          break;
      }
    }
  },

  uninit() {
    if (!this._initialized) {
      return;
    }
    Services.obs.removeObserver(this, lazy.ONLOGOUT_NOTIFICATION);
    Services.obs.removeObserver(this, "tabstrip-orientation-change");

    lazy.PlacesUtils.observers.removeListener(
      ["page-removed", "history-cleared"],
      this.handlePlacesEvents
    );

    this._initialized = false;
  },

  observe(_subject, topic) {
    if (topic === lazy.ONLOGOUT_NOTIFICATION) {
      this._onAccountLogout();
    } else if (topic === "tabstrip-orientation-change") {
      this._onTabstripOrientationChange();
    }
  },

  // Switches all active AI Windows back to classic mode when the user signs out
  // of their Firefox Account.
  _onAccountLogout() {
    for (const win of Services.wm.getEnumerator("navigator:browser")) {
      if (!win.closed && this.isAIWindowActive(win)) {
        this.toggleAIWindow(win, false);
      }
    }
  },

  // Checks if there are any open AI Windows. It's used to determine if certain
  // operations (like Account sign-out warnings) need to account for active AI
  // Window sessions.
  hasActiveAIWindows() {
    for (const win of Services.wm.getEnumerator("navigator:browser")) {
      if (!win.closed && this.isAIWindowActiveAndEnabled(win)) {
        return true;
      }
    }
    return false;
  },

  _reconcileNewTabPages(win, newTabPref, homePagePref) {
    const newTabURI = Services.io.newURI(win.BROWSER_NEW_TAB_URL);
    const aboutNewTabURI = Services.io.newURI("about:newtab");
    const aboutHomeURI = Services.io.newURI("about:home");

    const newTabPrefURI = Services.io.newURI(newTabPref);
    const homePagePrefURIs = lazy.HomePage.parseCustomHomepageURLs(
      homePagePref
    ).flatMap(url => {
      try {
        return [Services.io.newURI(url)];
      } catch {
        return [];
      }
    });

    const triggeringPrincipal =
      Services.scriptSecurityManager.getSystemPrincipal();

    for (let tab of win.gBrowser.tabs) {
      const browser = tab.linkedBrowser;
      if (!browser?.currentURI) {
        continue;
      }

      const currentURI = browser.currentURI;

      if (
        currentURI.equalsExceptRef(newTabPrefURI) ||
        currentURI.equalsExceptRef(aboutNewTabURI) ||
        currentURI.equalsExceptRef(aboutHomeURI) ||
        homePagePrefURIs.some(uri => currentURI.equalsExceptRef(uri))
      ) {
        if (this.hasActiveChatInBrowser(browser)) {
          continue;
        }
        browser.loadURI(newTabURI, { triggeringPrincipal });
      }
    }
  },

  hasActiveChatInBrowser(browser) {
    const aiWindowElement =
      browser?.contentDocument?.querySelector("ai-window");
    if (!aiWindowElement) {
      return false;
    }
    return aiWindowElement.classList.contains("chat-active");
  },

  _forEachWindow(callback) {
    ChromeUtils.nondeterministicGetWeakMapKeys(this._windowStates).forEach(
      win => {
        if (win && !win.closed) {
          callback(win);
        }
      }
    );
  },

  _onAIWindowEnabledPrefChange() {
    this._forEachWindow(win => this._updateButtonVisibility(win));
    if (!this.isAvailable) {
      this._onAccountLogout();
    }
  },

  _updateButtonVisibility(win) {
    const isPrivateWindow = lazy.PrivateBrowsingUtils.isWindowPrivate(win);
    const modeSwitcherButton = win.document.getElementById("ai-window-toggle");
    if (modeSwitcherButton) {
      modeSwitcherButton.hidden = !this.isAIWindowEnabled() || isPrivateWindow;
    }
  },

  _onTabstripOrientationChange() {
    this._forEachWindow(win => this._updateToolbarButtonPositions(win));
  },

  _updateToolbarButtonPositions(win, { isToggling = false } = {}) {
    const modeSwitcherButton = win.document.getElementById("ai-window-toggle");
    const hamburgerMenu = win.document.getElementById("PanelUI-button");

    const targetToolbar = win.document.getElementById(
      this.verticalTabsEnabled ? "nav-bar" : "TabsToolbar"
    );
    const titlebarContainer = targetToolbar.querySelector(
      ".titlebar-buttonbox-container"
    );

    titlebarContainer.after(modeSwitcherButton);

    if (this.isAIWindowActive(win) || this.verticalTabsEnabled) {
      modeSwitcherButton.after(hamburgerMenu);
    } else if (isToggling) {
      // Restore hamburger menu to its original position in nav-bar.
      const postTabsSpacer = win.document
        .getElementById("nav-bar")
        .querySelector('.titlebar-spacer[type="post-tabs"]');
      postTabsSpacer.before(hamburgerMenu);
    }
  },

  /*
   * Initializes the toolbox button that opens the assistant sidebar.
   */
  _initializeAskButtonOnToolbox(win) {
    const askButton = win.document.getElementById("smartwindow-ask-button");
    if (!askButton) {
      return;
    }
    askButton.hidden = !this.isAIWindowActive(win);
  },

  /**
   * Sets options for new AI Window if new or inherited conditions are met
   *
   * @param {object} options Used in BrowserWindowTracker.openWindow
   * @param {object} options.openerWindow Window making the BrowserWindowTracker.openWindow call
   * @param {object} options.args Array of arguments to pass to new window
   * @param {boolean} [options.aiWindow] Should new window be AI Window (true), Classic Window (false), or inherited from opener (undefined, default)
   * @param {boolean} [options.private] Should new window be Private Window
   * @param {string} [options.restoreSessionURL] URL of the selected tab being restored
   *
   * @returns {object} Modified arguments appended to the options object
   */
  handleAIWindowOptions({
    openerWindow,
    args,
    aiWindow = undefined,
    private: isPrivate = false,
    restoreSessionURL = "",
  } = {}) {
    // Indicates whether the new window should inherit AI Window state from opener window
    const canInheritAIWindow =
      this.isAIWindowActiveAndEnabled(openerWindow) &&
      !isPrivate &&
      typeof aiWindow === "undefined";

    const willOpenAIWindow =
      (aiWindow && this.isAIWindowEnabled()) || canInheritAIWindow;

    if (!willOpenAIWindow) {
      return args;
    }

    args ??= Cc["@mozilla.org/array;1"].createInstance(Ci.nsIMutableArray);

    let initialURL = "";
    if (!args.length) {
      const aiWindowURI = Cc["@mozilla.org/supports-string;1"].createInstance(
        Ci.nsISupportsString
      );
      if (!restoreSessionURL) {
        initialURL = lazy.hasFirstrunCompleted ? AIWINDOW_URL : FIRSTRUN_URL;
      }
      aiWindowURI.data = initialURL;
      args.appendElement(aiWindowURI);
    }

    let propBag;
    try {
      propBag = args.length > 1 && args.queryElementAt(1, Ci.nsIPropertyBag2);
    } catch (e) {
      console.error(
        new Error(
          "Tried to create AI window but property bag argument is wrong"
        ),
        propBag
      );
      return args;
    }
    if (!propBag) {
      propBag = Cc["@mozilla.org/hash-property-bag;1"].createInstance(
        Ci.nsIWritablePropertyBag2
      );
      args.appendElement(propBag);
    }

    propBag.setPropertyAsBool("ai-window", true);
    if (canInheritAIWindow) {
      propBag.setPropertyAsAString("aiwindow-trigger", "new_window");
    }
    const willOpenImmersive = this.immersiveViewURIs.some(
      uri => uri.spec == (initialURL || restoreSessionURL)
    );
    if (willOpenImmersive) {
      propBag.setPropertyAsBool("aiwindow-immersive-view", true);
    }

    return args;
  },

  /**
   * Show Window Switcher button in tabs toolbar
   *
   * @param {object} win caller window
   */
  handleAIWindowSwitcher(win) {
    let view = lazy.PanelMultiView.getViewNode(
      win.document,
      "ai-window-toggle-view"
    );

    const isPrivateWindow = lazy.PrivateBrowsingUtils.isWindowPrivate(win);
    const isAIActive = this.isAIWindowActive(win);

    if (!isPrivateWindow) {
      let classicSwitchButton = view.querySelector("#ai-window-switch-classic");
      let smartSwitchButton = view.querySelector("#ai-window-switch-ai");
      classicSwitchButton.hidden = false;
      smartSwitchButton.hidden = false;
      classicSwitchButton.toggleAttribute("checked", !isAIActive);
      smartSwitchButton.toggleAttribute("checked", isAIActive);
    }

    let windowState = this._windowStates.get(win);
    if (!windowState) {
      windowState = {};
      this._windowStates.set(win, windowState);
    }

    if (windowState.viewInitialized) {
      return;
    }

    view.addEventListener("command", event => {
      switch (event.target.id) {
        case "ai-window-switch-classic":
          this.toggleAIWindow(win, false);
          break;
        case "ai-window-switch-ai":
          this.launchWindow(win.gBrowser.selectedBrowser, false, "switch");
          break;
      }
    });

    windowState.viewInitialized = true;
  },

  /**
   * Show Window Switcher button in tabs toolbar
   *
   * @param {Window} win caller window
   */
  initializeAITabsToolbar(win) {
    const modeSwitcherButton = win.document.getElementById("ai-window-toggle");
    if (!modeSwitcherButton) {
      return;
    }

    this._updateButtonVisibility(win);

    modeSwitcherButton.addEventListener("command", event => {
      if (win.PanelUI.panel.state == "open") {
        win.PanelUI.hide();
      } else if (win.PanelUI.panel.state == "closed") {
        this.handleAIWindowSwitcher(win);
        win.PanelUI.showSubView("ai-window-toggle-view", event.target, event);
      }
    });
  },

  /**
   * Is current window an AI Window
   *
   * @param {Window} win current Window
   * @returns {boolean} whether current Window is an AI Window
   */
  isAIWindowActive(win) {
    return !!win && win.document.documentElement.hasAttribute("ai-window");
  },

  /**
   * Whether AI Window is allowed by the feature pref and not blocked by AI Controls.
   *
   * @returns {boolean} whether AI Window is enabled
   */
  isAIWindowEnabled() {
    return this.isAvailable;
  },

  isAIWindowActiveAndEnabled(win) {
    return this.isAIWindowActive(win) && this.isAIWindowEnabled();
  },

  /**
   * Check if window is being opened as an AI Window.
   *
   * @param {Window} win - The window to check
   * @returns {boolean} whether the window is being opened as an AI Window
   */
  isOpeningAIWindow(win) {
    const windowArgs = win?.arguments?.[1];
    if (!(windowArgs instanceof Ci.nsIPropertyBag2)) {
      return false;
    }

    return windowArgs.hasKey("ai-window");
  },

  /**
   * Is AI Window content page active
   *
   * @param {nsIURI} uri current URI
   * @returns {boolean} whether AI Window content page is active
   */
  isAIWindowContentPage(uri) {
    return (
      AIWINDOW_URI.equalsExceptRef(uri) || FIRSTRUN_URI.equalsExceptRef(uri)
    );
  },

  /**
   * Adds the AI Window app menu options
   *
   * @param {Event} event - History menu click event
   * @param {Window} win - current Window reference
   *
   * @returns {Promise} - Resolves when menu is done being added
   */
  appMenu(event, win) {
    if (!this._aiWindowMenu) {
      this._aiWindowMenu = new lazy.AIWindowMenu();
    }

    return this._aiWindowMenu.addMenuitems(event, win);
  },

  get newTabURL() {
    return AIWINDOW_URL;
  },

  get firstrunURL() {
    return FIRSTRUN_URL;
  },

  /**
   * Performs a search in the default search engine with
   * passed query in the current tab.
   *
   * @param {string} query
   * @param {Window} window
   */
  async performSearch(query, window) {
    let engine = null;
    try {
      engine = await lazy.SearchService.getDefault();
    } catch (error) {
      console.error(`Failed to get default search engine:`, error);
    }

    const triggeringPrincipal =
      Services.scriptSecurityManager.getSystemPrincipal();

    await lazy.SearchUIUtils.loadSearch({
      window,
      searchText: query,
      where: "current",
      usePrivate: false,
      triggeringPrincipal,
      policyContainer: null,
      engine,
      searchUrlType: null,
      sapSource: "smartwindow_assistant",
    });
  },

  /**
   * Moves a full-page AI Window conversation into the sidebar.
   *
   * @param {Window} win
   * @param {object} tab
   * @returns {Promise<XULElement|null>}
   */
  async moveConversationToSidebar(win, tab) {
    return lazy.AIWindowUI.moveFullPageToSidebar(win, tab);
  },

  /**
   * Opens the sidebar with the given conversation and continues streaming
   * the model response after a tool result.
   *
   * @param {Window} win
   * @param {ChatConversation} conversation
   */
  openSidebarAndContinue(win, conversation) {
    lazy.AIWindowUI.openSidebar(win, conversation);

    try {
      const sidebar = win.document.getElementById("ai-window-box");
      const aiBrowser = sidebar?.querySelector("#ai-window-browser");
      const aiWindow = aiBrowser?.contentDocument?.querySelector("ai-window");
      if (aiWindow?.reloadAndContinue) {
        aiWindow.reloadAndContinue(conversation);
        return;
      }
    } catch {
      // Content may not be loaded yet
    }

    // Sidebar content isn't ready; set a flag for it to pick up on load
    try {
      const sidebar = win.document.getElementById("ai-window-box");
      const aiBrowser = sidebar?.querySelector("#ai-window-browser");
      if (aiBrowser) {
        aiBrowser.setAttribute("data-continue-streaming", "true");
      }
    } catch {
      // Sidebar may not be available
    }
  },

  recordOpenWindowTelemetry(trigger, win) {
    let signedIn = false;
    const opened_tabs = win?.gBrowser?.tabs.length ?? 0;
    lazy.AIWindowAccountAuth.isSignedIn()
      .then(result => {
        signedIn = result;
      })
      .finally(() => {
        Glean.smartWindow.openWindow.record({
          trigger,
          fxa: signedIn,
          onboarding: !lazy.hasFirstrunCompleted,
          opened_tabs,
        });
      });
  },

  /**
   * Toggles a window between Smart Window and classic browser mode.
   * Records an open_window telemetry event when activating if a trigger
   * is provided.
   *
   * @param {ChromeWindow} win
   * @param {boolean} isTogglingToAIWindow - true to activate, false to deactivate
   * @param {string} [trigger] - The open reason (e.g. "menu", "switch",
   *   "undo_close", "open_browser").
   */
  toggleAIWindow(win, isTogglingToAIWindow, trigger) {
    let isActive = this.isAIWindowActive(win);
    if (isActive != isTogglingToAIWindow) {
      lazy.NewTabPagePreloading.removePreloadedBrowser(win);

      const newTabPref = win.BROWSER_NEW_TAB_URL;
      const homePagePref = Services.prefs.getStringPref(
        "browser.startup.homepage",
        ""
      );

      win.document.documentElement.toggleAttribute("ai-window");

      this._reconcileNewTabPages(win, newTabPref, homePagePref);
      this._updateToolbarButtonPositions(win, { isToggling: true });
      this._initializeAskButtonOnToolbox(win);
      Services.obs.notifyObservers(
        win,
        "ai-window-state-changed",
        isTogglingToAIWindow ? "smart" : "classic"
      );

      if (isTogglingToAIWindow) {
        if (!this._aiWindowTabStateManagers.has(win)) {
          this._aiWindowTabStateManagers.set(
            win,
            new lazy.AIWindowTabStatesManager(win)
          );
        }

        if (lazy.hasFirstrunCompleted) {
          this._aiWindowTabStateManagers
            .get(win)
            ?.openSidebarForReturningUser();
        }

        lazy.MemoriesSchedulers.maybeRunAndSchedule();

        this._markActiveStart(win);
        this.recordOpenWindowTelemetry(trigger, win);
      } else {
        const duration_ms = this._consumeActiveDuration(win);
        const opened_tabs = win.gBrowser.tabs.length;
        // Uninit the manager first so #onSidebarToggle doesn't clear the
        // SessionStore entry before closeSidebar fires.
        this._uninitTabStateManager(win);
        lazy.AIWindowUI.closeSidebar(win);
        Glean.smartWindow.classicSwitch.record({ duration_ms, opened_tabs });
      }
    }
  },

  _uninitTabStateManager(win) {
    const manager = this._aiWindowTabStateManagers.get(win);
    if (!manager) {
      return;
    }
    manager.uninit();
    this._aiWindowTabStateManagers.delete(win);
  },

  getActiveConversation(win) {
    return (
      this._aiWindowTabStateManagers.get(win)?.getActiveConversation() ?? null
    );
  },

  unloadWindow(win) {
    if (this.isAIWindowActive(win)) {
      const duration_ms = this._consumeActiveDuration(win);
      const opened_tabs = win.gBrowser?.tabs.length ?? 0;
      Glean.smartWindow.closeWindow.record({ duration_ms, opened_tabs });
    }
    this._uninitTabStateManager(win);
    this._windowStates.delete(win);
  },

  _markActiveStart(win) {
    const windowState = this._windowStates.get(win);
    if (windowState) {
      windowState.aiActiveStartTime = Date.now();
    }
  },

  _consumeActiveDuration(win) {
    const windowState = this._windowStates.get(win);
    const startTime = windowState?.aiActiveStartTime;
    if (windowState) {
      delete windowState.aiActiveStartTime;
    }
    return startTime ? Date.now() - startTime : 0;
  },

  async _authorizeAndToggleWindow(win, trigger) {
    const authorized = await lazy.AIWindowAccountAuth.ensureAIWindowAccess(
      win.gBrowser.selectedBrowser
    );

    if (!authorized) {
      return false;
    }

    this.toggleAIWindow(win, true, trigger);

    if (!lazy.hasFirstrunCompleted) {
      win.gBrowser.loadURI(FIRSTRUN_URI, {
        triggeringPrincipal:
          Services.scriptSecurityManager.getSystemPrincipal(),
      });
    }

    return true;
  },

  async launchWindow(browser, openNewWindow = false, trigger = "other") {
    try {
      // Early return when Smart Window is blocked from AI Control
      if (this.isBlocked) {
        return false;
      }

      // if browser.smartwindow.enabled is false
      // set the pref explicitly true
      if (!this.isAllowed) {
        Services.prefs.setBoolPref(PREF_SMARTWINDOW_ENABLED, true);
      }

      if (!browser && !openNewWindow) {
        return false;
      }

      if (!openNewWindow) {
        return this._authorizeAndToggleWindow(browser.documentGlobal, trigger);
      }

      const isAuthorized = await lazy.AIWindowAccountAuth.canAccessAIWindow();
      const windowPromise = lazy.BrowserWindowTracker.promiseOpenWindow({
        aiWindow: isAuthorized,
        openerWindow: browser?.documentGlobal,
      });

      const newWin = await windowPromise;

      if (!isAuthorized) {
        return this._authorizeAndToggleWindow(newWin, trigger);
      }

      // The new window already has the ai-window attribute; toggleAIWindow
      // would skip the state change and therefore skip recording telemetry.
      this.recordOpenWindowTelemetry(trigger, newWin);
      return true;
    } catch (e) {
      console.error("Error launching AI window:", e);
      return false;
    }
  },

  /**
   * Launches the FxA sign-in auth flow for the given browser.
   *
   * @param {Browser} browser
   * @returns {Promise<boolean>} Whether the user signed in successfully
   */
  async launchSignInFlow(browser) {
    try {
      return await lazy.AIWindowAccountAuth.promptSignIn(browser);
    } catch (e) {
      console.error("Error launching sign-in flow:", e);
      return false;
    }
  },

  /**
   * Toggles the immersive view (hidden address bar and disabled tabs) depending on the URL passed
   *
   * @param {nsIURI} currentURI
   * @param {Window} win
   */
  updateImmersiveView(currentURI, win) {
    const root = win.document.getElementById("main-window");

    if (!currentURI) {
      return;
    }

    const aboutNewtabURI = Services.io.newURI("about:newtab");
    const aboutHomeURI = Services.io.newURI("about:home");
    const shouldHideSidebarForNewtab =
      currentURI.equalsExceptRef(aboutNewtabURI) ||
      currentURI.equalsExceptRef(aboutHomeURI);

    if (!this.isAIWindowActiveAndEnabled(win)) {
      root.toggleAttribute("hide-ai-sidebar", shouldHideSidebarForNewtab);
      root.removeAttribute("aiwindow-immersive-view");
      root.removeAttribute("aiwindow-has-nav-forward");
      return;
    }

    /* any URL that should have the immersive view */
    const isImmersiveView = this.shouldUseImmersiveView(currentURI);

    root.toggleAttribute("hide-ai-sidebar", isImmersiveView);

    if (isImmersiveView) {
      lazy.AIWindowUI.closeSidebar(win);
    }

    /* sets attr only for first run for css reasons */
    const isFirstRun = currentURI.equalsExceptRef(FIRSTRUN_URI);
    root.toggleAttribute("aiwindow-first-run", isFirstRun && isImmersiveView);
    root.toggleAttribute("aiwindow-immersive-view", isImmersiveView);

    const canGoForward =
      isImmersiveView &&
      !isFirstRun &&
      (win.gBrowser.selectedBrowser?.webNavigation?.canGoForward ?? false);
    root.toggleAttribute("aiwindow-has-nav-forward", canGoForward);

    // Set attr on the specific browser that has content to override color scheme
    win.gBrowser.selectedBrowser?.toggleAttribute(
      "smartwindow-content",
      isImmersiveView
    );

    /* disabling the current tab from being clicked from the keyboard */
    const selectedTab = win.gBrowser.selectedTab;
    if (isFirstRun) {
      selectedTab?.setAttribute("tabindex", -1);
    } else {
      selectedTab?.removeAttribute("tabindex");
    }
  },

  immersiveViewURIs: [FIRSTRUN_URI, AIWINDOW_URI],
  /**
   * Whether the URI should trigger immersive view (hiding the address bar and disabling tabs)
   *
   * @param {nsIURI} uri
   */
  shouldUseImmersiveView(uri) {
    return (
      !!uri &&
      this.immersiveViewURIs.some(immersiveURI =>
        immersiveURI.equalsExceptRef(uri)
      )
    );
  },

  /**
   * Optimistically try to get the smartbar for the currently selected
   * browser in the window.
   *
   * @param {Window} window
   * @returns {SmartbarInput | null}
   */
  getSmartbarForWindow(window) {
    // In principle we could be called when some other tab is loaded, even in
    // a remote process, which means contentDocument would be null.
    // Even if we _do_ have aiWindow.html loaded, the smartbar might not be in
    // the DOM yet (it gets constructed lazily) - hence the nullchecks.
    let { contentDocument } = window.gBrowser.selectedBrowser;
    let aiWindowCE = contentDocument?.querySelector("ai-window");
    return aiWindowCE?.shadowRoot.getElementById("ai-window-smartbar");
  },

  // AIFeature interface implementation
  // (see toolkit/components/ml/AIFeature.sys.mjs and
  // browser/components/preferences/OnDeviceModelManager.mjs)
  /**
   * Returns the unique identifier
   *
   * @returns {string}
   */
  get id() {
    return "smartWindow";
  },

  /**
   * Returns whether Smart Window exposes a distinct "Enabled" AI Controls state.
   *
   * @returns {boolean}
   */
  get hasDistinctEnabledState() {
    // Smart Window requires the user to log in, in order to enable the experience.
    return true;
  },

  /**
   * Check if the feature is blocked by AI controls
   *
   * @returns {boolean}
   */
  get isBlocked() {
    if (this.AIControlSmartWindow === "default") {
      return this.AIControlDefault === "blocked";
    }
    return this.AIControlSmartWindow === "blocked";
  },

  /**
   * Check if the feature is enabled and the user has consented
   *
   * @returns {boolean}
   */
  get isEnabled() {
    return (
      this.isAvailable &&
      Services.prefs.prefHasUserValue(PREF_SMARTWINDOW_CONSENT_TIME)
    );
  },

  get isAvailable() {
    return this.isAllowed && !this.isBlocked;
  },

  /**
   * Check if the feature is allowed to be enabled
   *
   * @returns {boolean}
   */
  get isAllowed() {
    return this.AIWindowEnabledPref;
  },

  /**
   * Returns whether the current device can run Smart Window.
   *
   * @returns {boolean}
   */
  get canRunOnDevice() {
    // There are no known hardware restrictions for smart window.
    return true;
  },

  /**
   * Check if the feature is managed by enterprise policy
   *
   * @returns {boolean}
   */
  get isManagedByPolicy() {
    return (
      Services.prefs.prefIsLocked(PREF_AI_CONTROL_SMARTWINDOW) ||
      Services.prefs.prefIsLocked(PREF_SMARTWINDOW_ENABLED)
    );
  },

  /**
   * Reset the feature to available state - deleted all memories and clear consent
   *
   * @returns {Promise<void>}
   */
  async makeAvailable() {
    Services.prefs.clearUserPref(PREF_SMARTWINDOW_CONSENT_TIME);
    // Set memory generation pref to default
    Services.prefs.setBoolPref(PREF_MEMORIES_CONVERSATION, true);
    Services.prefs.setBoolPref(PREF_MEMORIES_HISTORY, true);
  },

  /**
   * Set the feature as available
   *
   * @returns {Promise<void>}
   */
  async enable() {
    Services.prefs.setBoolPref(PREF_SMARTWINDOW_ENABLED, true);
    Services.prefs.setStringPref(PREF_AI_CONTROL_SMARTWINDOW, "enabled");
  },

  /**
   * Set the feature as disable
   *
   * @returns {Promise<void>}
   */
  async block() {
    // Leave PREF_SMARTWINDOW_ENABLED alone, since PREF_AI_CONTROL_SMARTWINDOW
    // will block the feature anyways.
    Services.prefs.setStringPref(PREF_AI_CONTROL_SMARTWINDOW, "blocked");
    await lazy.ChatStore.deleteAllConversations();
    await this._removeMemories();
  },

  /**
   * Delete all memories generated by Smart Window
   * Called when the feature is disabled via AI control
   *
   * @returns {Promise<void>}
   */
  async _removeMemories() {
    const memories = await lazy.MemoryStore.getMemories();
    for (const memory of memories) {
      try {
        await lazy.MemoryStore.hardDeleteMemory(memory.id);
      } catch (err) {
        console.error("Failed to delete memory:", memory.id, err);
      }
    }
    // Turn off the memory generation
    Services.prefs.setBoolPref(PREF_MEMORIES_CONVERSATION, false);
    Services.prefs.setBoolPref(PREF_MEMORIES_HISTORY, false);
  },
};

Object.setPrototypeOf(AIWindow, AIFeature);

XPCOMUtils.defineLazyPreferenceGetter(
  AIWindow,
  "AIWindowEnabledPref",
  PREF_SMARTWINDOW_ENABLED,
  false,
  AIWindow._onAIWindowEnabledPrefChange.bind(AIWindow)
);

XPCOMUtils.defineLazyPreferenceGetter(
  AIWindow,
  "AIControlSmartWindow",
  PREF_AI_CONTROL_SMARTWINDOW,
  "default",
  AIWindow._onAIWindowEnabledPrefChange.bind(AIWindow)
);

XPCOMUtils.defineLazyPreferenceGetter(
  AIWindow,
  "AIControlDefault",
  PREF_AI_CONTROL_DEFAULT,
  "available",
  AIWindow._onAIWindowEnabledPrefChange.bind(AIWindow)
);

XPCOMUtils.defineLazyPreferenceGetter(
  AIWindow,
  "verticalTabsEnabled",
  "sidebar.verticalTabs",
  false
);
