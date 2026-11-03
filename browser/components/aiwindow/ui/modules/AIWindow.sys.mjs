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
const PREF_SMARTWINDOW_LAST_USAGE_TIME =
  "browser.smartwindow.lastSmartWindowUsageTime";
const PREF_AI_CONTROL_SMARTWINDOW = "browser.ai.control.smartWindow";
const PREF_AI_CONTROL_DEFAULT = "browser.ai.control.default";
const PREF_MEMORIES_CONVERSATION =
  "browser.smartwindow.memories.generateFromConversation";
const PREF_MEMORIES_HISTORY =
  "browser.smartwindow.memories.generateFromHistory";
const PREF_SEMANTIC_HISTORY_SMARTWINDOW_FEATURE_GATE =
  "places.semanticHistory.smartwindow.featureGate";
const PREF_AUTO_TAB_GROUPING = "browser.smartwindow.autoTabGrouping.enabled";
const PREF_AUTO_TAB_GROUPING_LOCALES =
  "browser.smartwindow.autoTabGrouping.supportedLocales";
const PREF_FIRSTRUN_HAS_COMPLETED = "browser.smartwindow.firstrun.hasCompleted";
const PREF_AGENT = "browser.smartwindow.agent.enabled";
const PREF_AGENT_TOOLBAR = "browser.smartwindow.agent.toolbar.enabled";
const PREF_AGENT_SUPPORTED_REGIONS =
  "browser.smartwindow.agent.supportedRegions";
const MONITOR_WIDGET_ID = "smartwindow-monitor-button";
const GROUP_TABS_BUTTON_ID = "smartwindow-group-tabs-button";
// Nimbus-controlled, on the default branch: whether to announce the feature as
// new. Dismissing writes false to the user branch, which outranks the default
// branch, so the announcement does not come back when Nimbus re-applies it.
const PREF_MONITOR_ANNOUNCEMENT =
  "browser.smartwindow.agent.monitorAnnouncement";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AITab: "moz-src:///browser/components/aiwindow/models/aitab/AITab.sys.mjs",
  getAllModelsData:
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  AIWindowTabStatesManager:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowTabStatesManager.sys.mjs",
  AIWindowMenu:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowMenu.sys.mjs",
  AuthImplSelector:
    "moz-src:///browser/components/aiwindow/models/auth/AuthImplSelector.sys.mjs",
  AuthProvider:
    "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs",
  FxaSignedInWatcher:
    "moz-src:///browser/components/aiwindow/models/auth/FxaSignedInWatcher.sys.mjs",
  AutoTabGroupingSuggestions:
    "moz-src:///browser/components/aiwindow/ui/modules/AutoTabGroupingSuggestions.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  AIWindowUI:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs",
  ChatStore:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs",
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  MemoryStore:
    "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs",
  NewTabPagePreloading:
    "moz-src:///browser/components/tabbrowser/NewTabPagePreloading.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  PanelMultiView:
    "moz-src:///browser/components/customizableui/PanelMultiView.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  SearchUIUtils: "moz-src:///browser/components/search/SearchUIUtils.sys.mjs",
  SessionStartup:
    "moz-src:///browser/components/sessionstore/SessionStartup.sys.mjs",
  MemoriesSchedulers:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesSchedulers.sys.mjs",
  MonitorAgent:
    "moz-src:///browser/components/aiwindow/models/agents/MonitorAgent.sys.mjs",
  MONITOR_CONDITION_MET_TOPIC:
    "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs",
  MONITOR_RUN_FAILED_TOPIC:
    "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs",
  MonitorAttention:
    "moz-src:///browser/components/aiwindow/ui/modules/MonitorAttention.sys.mjs",
  MonitorUIUtils:
    "moz-src:///browser/components/aiwindow/ui/modules/MonitorUIUtils.sys.mjs",
  SmartWindowLocation:
    "moz-src:///browser/components/aiwindow/models/SmartWindowLocation.sys.mjs",
  SmartWindowTelemetry:
    "moz-src:///browser/components/aiwindow/ui/modules/SmartWindowTelemetry.sys.mjs",
  TelemetryScheduler:
    "moz-src:///browser/components/aiwindow/models/TelemetryManager.sys.mjs",
  URILoadingHelper: "resource:///modules/URILoadingHelper.sys.mjs",
});

ChromeUtils.defineLazyGetter(
  lazy,
  "l10n",
  () => new Localization(["preview/aiWindow.ftl"], true)
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "hasFirstrunCompleted",
  PREF_FIRSTRUN_HAS_COMPLETED,
  false,
  (_pref, _previous, hasCompleted) =>
    hasCompleted && AIWindow._startSchedulers()
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "agentEnabled",
  PREF_AGENT,
  false,
  () => AIWindow._updateMonitorWidgetRegistration()
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "agentToolbarEnabled",
  PREF_AGENT_TOOLBAR,
  false,
  () => AIWindow._updateMonitorWidgetRegistration()
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "autoTabGroupingEnabled",
  PREF_AUTO_TAB_GROUPING,
  true,
  () => AIWindow._updateGroupTabsWidgetRegistration()
);

// Enrolling mid-session has to light the dot without waiting for a restart.
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "monitorAnnouncement",
  PREF_MONITOR_ANNOUNCEMENT,
  false,
  () =>
    AIWindow._forEachWindow(win => AIWindow._updateMonitorButtonForWindow(win))
);

/**
 * AI Window Service
 */

export const AIWindow = {
  _initialized: false,
  _windowStates: new WeakMap(),
  _aiWindowMenu: null,
  _switcherWidgetCreated: false,
  _monitorWidgetCreated: false,
  _groupTabsWidgetCreated: false,

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
      this._updateHamburgerMenuPosition(win);
      this._initializeAskButtonOnToolbox(win);
      this._updateMonitorButtonForWindow(win);
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

      // Check scheduler startup for every AI window. Otherwise, if a non-AI
      // window initialized first (e.g. on startup), the first AI window
      // would never start the memories schedulers. Defer until delayed startup
      // so MemoriesManager sees this window as ready before starting the schedulers.
      win.delayedStartupPromise.then(() => this._startSchedulers());
    }

    if (this._initialized) {
      return;
    }

    lazy.PlacesUtils.observers.addListener(
      ["page-removed", "history-cleared"],
      this.handlePlacesEvents
    );

    ChromeUtils.defineLazyGetter(AIWindow, "chatStore", () => lazy.ChatStore);
    lazy.AuthProvider.addEventListener(
      "AuthProvider:Logout",
      this.onAuthProviderLogout
    );
    Services.obs.addObserver(this, "tabstrip-orientation-change");
    Services.obs.addObserver(this, "browser-region-updated");
    Services.prefs.addObserver(PREF_AGENT_SUPPORTED_REGIONS, this);
    Services.prefs.addObserver(PREF_AUTO_TAB_GROUPING_LOCALES, this);
    lazy.SmartWindowTelemetry.init();
    lazy.getAllModelsData(); // loads model data into cache for about:preferences
    lazy.NimbusFeatures.smartWindow.onUpdate(this.onNimbusUpdate);
    this._initialized = true;
    this._updateGroupTabsWidgetRegistration();
    this._updateSwitcherWidgetRegistration();
    this._updateMonitorWidgetRegistration();
  },

  _startSchedulers() {
    lazy.MemoriesSchedulers.maybeRunAndSchedule();
    lazy.TelemetryScheduler.maybeInit();
    lazy.SmartWindowLocation.refresh();
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
    lazy.AuthProvider.removeEventListener(
      "AuthProvider:Logout",
      this.onAuthProviderLogout
    );
    Services.obs.removeObserver(this, "tabstrip-orientation-change");
    Services.obs.removeObserver(this, "browser-region-updated");
    Services.prefs.removeObserver(PREF_AGENT_SUPPORTED_REGIONS, this);
    Services.prefs.removeObserver(PREF_AUTO_TAB_GROUPING_LOCALES, this);

    lazy.PlacesUtils.observers.removeListener(
      ["page-removed", "history-cleared"],
      this.handlePlacesEvents
    );

    lazy.NimbusFeatures.smartWindow.offUpdate(this.onNimbusUpdate);

    this._initialized = false;
  },

  onNimbusUpdate() {
    if (lazy.NimbusFeatures.smartWindow.getVariable("enabled")) {
      Services.prefs.setBoolPref(PREF_SMARTWINDOW_ENABLED, true);
    }
    lazy.AuthImplSelector.applyNimbusAuthImpl();
  },

  onAuthProviderLogout() {
    AIWindow._onAccountLogout();
  },

  observe(_subject, topic, data) {
    if (topic === "tabstrip-orientation-change") {
      this._onTabstripOrientationChange();
    } else if (
      topic === "nsPref:changed" &&
      data === PREF_AUTO_TAB_GROUPING_LOCALES
    ) {
      // Nimbus applies this pref after session restore, so windows that built
      // the button before then have to catch up.
      this._updateGroupTabsButtons();
    } else if (
      topic === "browser-region-updated" ||
      topic === "nsPref:changed"
    ) {
      // Both inputs to the region gate on the monitor button.
      this._updateMonitorWidgetRegistration();
    } else if (topic === lazy.MONITOR_CONDITION_MET_TOPIC) {
      this.showMonitorAttention(data);
    } else if (topic === lazy.MONITOR_RUN_FAILED_TOPIC) {
      this.showMonitorErrorAttention(data);
    }
  },

  // Switches all active AI Windows back to classic mode when the user signs out.
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
    this._updateGroupTabsWidgetRegistration();
    this._updateSwitcherWidgetRegistration();
    this._updateMonitorWidgetRegistration();
    const widget = lazy.CustomizableUI.getWidget("ai-window-toggle");
    for (const { node } of widget?.instances ?? []) {
      this._updateButtonVisibility(node);
    }
    Services.prefs.setBoolPref(
      PREF_SEMANTIC_HISTORY_SMARTWINDOW_FEATURE_GATE,
      this.isAvailable
    );
    if (!this.isAvailable) {
      this._onAccountLogout();
    }
    lazy.MonitorAgent.onBlockedStateChanged().catch(console.error);
  },

  _updateButtonVisibility(node) {
    if (node) {
      node.hidden = !this.isAIWindowEnabled();
    }
  },

  _onTabstripOrientationChange() {
    this._forEachWindow(win => this._updateHamburgerMenuPosition(win));
  },

  /**
   * Move the hamburger menu next to the window controls on Smart Window and Vertical
   * tabs, and restores it to its default nav-bar position for Classic.
   * The mode switcher button itself is a customizable widget, so its
   * placement is owned by CustomizableUI and not touched here.
   *
   * @param {Window} win The window to update.
   * @param {object} [options]
   * @param {boolean} [options.isToggling]
   */
  _updateHamburgerMenuPosition(win, { isToggling = false } = {}) {
    const hamburgerMenu = win.document.getElementById("PanelUI-button");
    const targetToolbar = win.document.getElementById(
      this.verticalTabsEnabled ? "nav-bar" : "TabsToolbar"
    );
    const titlebarContainer = targetToolbar.querySelector(
      ".titlebar-buttonbox-container"
    );

    if (this.isAIWindowActive(win) || this.verticalTabsEnabled) {
      titlebarContainer.after(hamburgerMenu);
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
   * Shows the "Organize Tabs" button only in a Smart Window.
   *
   * @param {Element} [node] This window's button, if it has one: the user can
   *   remove it from the toolbar.
   */
  _updateGroupTabsButtonVisibility(node) {
    if (!node) {
      return;
    }
    node.hidden = !(
      lazy.autoTabGroupingEnabled &&
      this.isAIWindowActive(node.documentGlobal) &&
      lazy.AutoTabGroupingSuggestions.isAvailable
    );
    if (!node.hidden) {
      lazy.AutoTabGroupingSuggestions.preloadModels();
    }
  },

  _updateGroupTabsButtons() {
    const widget = lazy.CustomizableUI.getWidget(GROUP_TABS_BUTTON_ID);
    for (const { node } of widget?.instances ?? []) {
      this._updateGroupTabsButtonVisibility(node);
    }
  },

  /**
   * Whether the monitor toolbar button is enabled. It is the toolbar surface of
   * the Smart Window agent, so it needs Smart Window to be enabled, the
   * agent feature and its own toolbar gate, and the same region gate the rest of
   * the feature uses. Where the button cannot create a monitor
   * it should not appear
   *
   * @returns {boolean}
   */
  get monitorButtonEnabled() {
    return (
      this.isAIWindowEnabled() &&
      lazy.agentEnabled &&
      lazy.agentToolbarEnabled &&
      lazy.MonitorUIUtils.isMonitorRegionSupported()
    );
  },

  /**
   * The monitor button is a CustomizableUI button, it must not exist
   * when the feature is off or when Smart Window is blocked by browser.ai.control.
   */
  _updateMonitorWidgetRegistration() {
    if (this.monitorButtonEnabled && !this.isBlocked) {
      this._createMonitorWidget();
      return;
    }
    this._destroyMonitorWidget();
  },

  _createMonitorWidget() {
    if (this._monitorWidgetCreated) {
      return;
    }

    lazy.CustomizableUI.createWidget({
      id: MONITOR_WIDGET_ID,
      l10nId: "smartwindow-monitor-button",
      defaultArea: lazy.CustomizableUI.AREA_NAVBAR,
      removable: true,
      showInPrivateBrowsing: false,
      onCreated: node => {
        node.setAttribute("aria-haspopup", "dialog");
        node.setAttribute("aria-expanded", "false");
        // Must be set before the node is connected, the toolbarbutton custom
        // element only builds the badge stack once, on its first render.
        node.setAttribute("badged", "true");
        node.hidden = !this._shouldShowMonitorButton(node.documentGlobal);
        this._updateMonitorAttentionForNode(node);
      },
      onCommand: event => {
        lazy.AIWindowUI.toggleMonitorPanel(event.view);
      },
    });
    Services.obs.addObserver(this, lazy.MONITOR_CONDITION_MET_TOPIC);
    Services.obs.addObserver(this, lazy.MONITOR_RUN_FAILED_TOPIC);
    this._monitorWidgetCreated = true;
  },

  _destroyMonitorWidget() {
    if (!this._monitorWidgetCreated) {
      return;
    }

    Services.obs.removeObserver(this, lazy.MONITOR_CONDITION_MET_TOPIC);
    Services.obs.removeObserver(this, lazy.MONITOR_RUN_FAILED_TOPIC);
    lazy.CustomizableUI.destroyWidget(MONITOR_WIDGET_ID);
    this._monitorWidgetCreated = false;
  },

  /**
   * Placement is global across windows, so the button is hidden per window to
   * keep it out of Classic Windows. Unlike the ask button it stays visible in
   * the immersive view.
   *
   * @param {Window} win
   */
  _updateMonitorButtonForWindow(win) {
    const button = win.document.getElementById(MONITOR_WIDGET_ID);
    if (!button) {
      return;
    }
    button.hidden = !this._shouldShowMonitorButton(win);
    this._updateMonitorAttentionForNode(button);
  },

  /**
   * Monitors that matched their condition and that the user has not been shown
   * the panel for since, newest match first. A monitor that only failed to
   * check is not here: it lights the dot but the panel leaves it where it is.
   *
   * @returns {string[]} Monitor ids.
   */
  get monitorAttentionIds() {
    return lazy.MonitorAttention.matchedIds;
  },

  /**
   * Whether the feature is still being announced as new. How long the
   * announcement runs is the rollout's business: unenrolling restores the
   * default branch and the dot goes with it.
   *
   * @returns {boolean}
   */
  get hasMonitorAnnouncement() {
    return lazy.monitorAnnouncement;
  },

  /**
   * The dot means "there is something here for you" whichever reason put it
   * there, so the button does not need to tell the two apart.
   *
   * @returns {boolean} Whether the monitor button should carry the dot.
   */
  get hasMonitorAttention() {
    return lazy.MonitorAttention.hasAttention || this.hasMonitorAnnouncement;
  },

  /**
   * The ids to highlight, clearing them so the next panel opening starts fresh.
   * Reading and clearing are one step because the two must not drift: clearing
   * first loses the highlight, reading first risks never clearing the dot.
   *
   * @returns {string[]} Monitor ids, newest match first.
   */
  takeMonitorAttentionIds() {
    const ids = this.monitorAttentionIds;
    this.clearMonitorAttention();
    return ids;
  },

  /**
   * @param {string} monitorId - The monitor whose run met its condition.
   */
  showMonitorAttention(monitorId) {
    lazy.MonitorAttention.recordMatch(monitorId);
    this._forEachWindow(win => this._updateMonitorButtonForWindow(win));
  },

  /**
   * A monitor that could not check is worth the same dot as one that matched:
   * either way there is something in the panel the user has not seen.
   *
   * @param {string} monitorId - The monitor whose run failed.
   */
  showMonitorErrorAttention(monitorId) {
    lazy.MonitorAttention.recordError(monitorId);
    this._forEachWindow(win => this._updateMonitorButtonForWindow(win));
  },

  /**
   * Retires every reason the dot is showing. Opening the panel answers the
   * announcement as much as it answers a match, so both go at once.
   */
  clearMonitorAttention() {
    lazy.MonitorAttention.clearAttention();
    // Only dismiss an announcement that is actually running so we do not mask
    // a rollout that starts later.
    if (lazy.monitorAnnouncement) {
      Services.prefs.setBoolPref(PREF_MONITOR_ANNOUNCEMENT, false);
      return;
    }
    this._forEachWindow(win => this._updateMonitorButtonForWindow(win));
  },

  _updateMonitorAttentionForNode(node) {
    node.toggleAttribute("monitor-attention", this.hasMonitorAttention);
  },

  _shouldShowMonitorButton(win) {
    return this.monitorButtonEnabled && this.isAIWindowActive(win);
  },

  get isDefaultWindow() {
    return (
      this.AIWindowEnabledPref &&
      this.isAIWindowEnabled() &&
      Services.prefs.getBoolPref("browser.smartwindow.isDefaultWindow", false)
    );
  },

  shouldOpenAsSmartWindow() {
    if (
      !this.isDefaultWindow ||
      lazy.PrivateBrowsingUtils.permanentPrivateBrowsing
    ) {
      return false;
    }
    return true;
  },

  /**
   * Registered under the `browser-first-window-ready` category, so it runs
   * exactly once per session after the first browser window finishes loading.
   * When the user has chosen Smart Window as their default, promote that
   * first window to Smart (prompting for sign-in if needed).
   *
   * @param {Window} win The first browser window for the session.
   */
  async onFirstWindowReady(win) {
    if (
      !this.shouldOpenAsSmartWindow() ||
      lazy.PrivateBrowsingUtils.isWindowPrivate(win) ||
      lazy.SessionStartup.willRestore()
    ) {
      return;
    }

    this.recordLaunchCommandTelemetry("startup", false);

    if (this.isAIWindowActive(win)) {
      // Window already opened as Smart via the BrowserContentHandler
      // startup gate, which uses ToU consentTime as a synchronous proxy for
      // "previously signed in". Verify the actual auth state now and prompt
      // sign-in if the user has since logged out — without this, signed-out
      // users would get a Smart Window with broken auth-gated features.
      await lazy.AuthProvider.maybeSignIn(win.gBrowser.selectedBrowser);
      return;
    }
    await this._authorizeAndToggleWindow(win, "startup");
  },

  /**
   * Records the current time (in seconds) as the last time a Smart Window was
   * in use. Called whenever a Smart Window goes away — either by switching back
   * to classic or by closing the window — so message targeting can measure how
   * long it has been since the user last had a Smart Window open.
   */
  _recordSmartWindowUsage() {
    Services.prefs.setIntPref(
      PREF_SMARTWINDOW_LAST_USAGE_TIME,
      Math.floor(Date.now() / 1000)
    );
  },

  /**
   * Sets options for new AI Window if new or inherited conditions are met
   *
   * @param {object} options Used in BrowserWindowTracker.openWindow
   * @param {object} options.openerWindow Window making the BrowserWindowTracker.openWindow call
   * @param {nsIMutableArray} [options.args] Array of arguments to pass to new window
   * @param {boolean} [options.aiWindow] Should new window be AI Window (true), Classic Window (false), or inherited from opener (undefined, default)
   * @param {boolean} [options.private] Should new window be Private Window
   * @param {string} [options.restoreSessionURL] URL of the selected tab being restored
   *
   * @returns {nsIMutableArray}
   *   Updated arguments array with additional `extraOptions` added for
   *   the AI Window.
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
        initialURL = this.initialStartupURL;
      }
      aiWindowURI.data = initialURL;
      args.appendElement(aiWindowURI);
    } else if (!restoreSessionURL) {
      // args was already populated by the caller (e.g. BrowserContentHandler
      // at startup). Extract the URL so willOpenImmersive can match it.
      try {
        const firstArg = args.queryElementAt(0, Ci.nsISupportsString);
        initialURL = firstArg.data.split("|")[0] ?? "";
      } catch (e) {}
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
   * Is the given URI the Smart Window new tab page. Unlike
   * isAIWindowContentPage, this excludes the firstrun page.
   *
   * @param {nsIURI} uri current URI
   * @returns {boolean} whether the URI is the Smart Window new tab page
   */
  isAIWindowNewTabPage(uri) {
    return AIWINDOW_URI.equalsExceptRef(uri);
  },

  /**
   * @param {MozTabbrowserTab} tab
   * @returns {?string}
   */
  getChatTabConversationId(tab) {
    const uri = tab.linkedBrowser?.currentURI;
    if (!uri || !this.isAIWindowNewTabPage(uri)) {
      return null;
    }
    return (
      this._aiWindowTabStateManagers
        .get(tab.documentGlobal)
        ?.getTabConversationId(tab) ?? null
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
   * The URL to load when opening an AI Window from scratch — either the
   * firstrun page (if the user hasn't been through it yet) or the regular
   * Smart Window new tab.
   *
   * @returns {string}
   */
  get initialStartupURL() {
    return lazy.hasFirstrunCompleted ? AIWINDOW_URL : FIRSTRUN_URL;
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
      avoidBrowserFocus: true,
    });
    await lazy.AIWindowUI.focusSidebar(window);
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

  focusSidebar(win) {
    return lazy.AIWindowUI.focusSidebar(win);
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
    lazy.AIWindowUI.focusSidebar(win);

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

  /**
   * Opens a new background tab whose chat asks for an AI Tab page built from
   * the given tabs, as if the user had typed the request there. The current
   * tab and its sidebar are left alone; the new tab decides for itself how to
   * show its chat once selected.
   *
   * @param {Window} win
   * @param {string[]} urls - Non-http(s) URLs are dropped since the model
   *   cannot read them.
   * @param {"tab_list"|"tab_group"} source - Where the page was requested
   *   from, for telemetry.
   */
  createAITab(win, urls, source) {
    const pageUrls = lazy.AITab.selectContextUrls(urls);
    if (!pageUrls.length) {
      return;
    }

    const text = [
      lazy.l10n.formatValueSync("ai-tab-create-page-prompt", {
        tabCount: pageUrls.length,
      }),
      ...pageUrls,
    ].join("\n");

    lazy.URILoadingHelper.openTrustedLinkIn(win, AIWINDOW_URL, "tab", {
      inBackground: true,
      resolveOnContentBrowserCreated: browser => {
        const submit = () =>
          browser.contentDocument.querySelector("ai-window").submitChatMessage({
            text,
            submitType: "menu",
            contextPageUrl: null,
            aitabSource: source,
          });

        // AIWINDOW_URL is the Smart Window's new tab page, so this may be the
        // preloaded new tab browser, whose ai-window connected before it had a
        // tab and will not connect again.
        if (browser.contentDocument?.querySelector("ai-window")?.conversation) {
          submit();
          return;
        }

        // Otherwise the chat page is still loading; submit once its element
        // connects, or give up if the tab is closed first.
        const tab = win.gBrowser.getTabForBrowser(browser);
        const controller = new AbortController();
        const { signal } = controller;
        tab.addEventListener("TabClose", () => controller.abort(), { signal });
        win.addEventListener(
          "ai-window:connected",
          event => {
            if (event.detail.tab !== tab) {
              return;
            }
            controller.abort();
            submit();
          },
          { signal }
        );
      },
    });
  },

  /**
   * Records that a Smart Window was opened.
   *
   * @param {string} trigger - The reason the Smart Window was opened: switch,
   *   menu, new_window, open_browser, undo_close, keyboard_shortcut, settings,
   *   bedrock, asrouter, or other. Messaging surfaces may supply their own
   *   value instead of asrouter
   * @param {ChromeWindow} [win] - The window that was opened. Omit when it is
   *   not available yet, in which case the tab count is reported as 0
   */
  recordOpenWindowTelemetry(trigger, win) {
    const opened_tabs = win?.gBrowser?.tabs.length ?? 0;
    this.getAuthTelemetryExtras().then(({ fxa, provider }) => {
      Glean.smartWindow.openWindow.record({
        trigger,
        fxa,
        onboarding: !lazy.hasFirstrunCompleted,
        opened_tabs,
        auth_provider: provider,
      });
    });
  },

  /**
   * Records the intent to launch a Smart Window, before the sign-in flow that
   * launchWindow may trigger.
   *
   * @param {string} trigger - The entry point requesting the launch: switch,
   *   menu, keyboard_shortcut, startup, settings, bedrock, asrouter, or other.
   *   Messaging surfaces may supply their own value instead of asrouter
   * @param {boolean} openNewWindow - Whether a new window will be opened
   */
  recordLaunchCommandTelemetry(trigger, openNewWindow) {
    this.getAuthTelemetryExtras().then(({ fxa, provider }) => {
      Glean.smartWindow.launchCommandInvoked.record({
        trigger,
        fxa,
        new_window: openNewWindow,
        auth_provider: provider,
      });
    });
  },

  /**
   * Reads the fxa and provider extras recorded on open_window and
   * launch_command_invoked. fxa reflects the Mozilla account only, whichever
   * provider is active.
   *
   * @returns {Promise<{fxa: boolean, provider: string|undefined}>}
   */
  async getAuthTelemetryExtras() {
    const [fxa, provider] = await Promise.all([
      lazy.FxaSignedInWatcher.isFxaSignedIn(),
      lazy.AuthProvider.getProviderName().catch(() => undefined),
    ]);
    return { fxa, provider };
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
      this._updateHamburgerMenuPosition(win, { isToggling: true });
      this._initializeAskButtonOnToolbox(win);
      this._updateMonitorButtonForWindow(win);
      this._updateGroupTabsButtonVisibility(
        win.document.getElementById(GROUP_TABS_BUTTON_ID)
      );
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

        this._startSchedulers();

        this._markActiveStart(win);
        this.recordOpenWindowTelemetry(trigger, win);
      } else {
        const duration_ms = this._consumeActiveDuration(win);
        const opened_tabs = win.gBrowser.tabs.length;
        // Uninit the manager first so #onSidebarToggle doesn't clear the
        // SessionStore entry before closeSidebar fires.
        this._uninitTabStateManager(win);
        lazy.AIWindowUI.closeSidebar(win);
        // Classic windows don't need the hidden sidebar document;
        // ensureBrowserIsAppended recreates it.
        win.document.getElementById(lazy.AIWindowUI.BROWSER_ID)?.remove();
        this._recordSmartWindowUsage();
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

  _getTabStateManager(win) {
    return this._aiWindowTabStateManagers.get(win) ?? null;
  },

  /**
   * Restore a conversation with the tab of the given browser
   *
   * @param {MozBrowser} browser The browser whose tab is being updated
   * @param {ChatConversation} conversation
   */
  restoreTabConversation(browser, conversation) {
    const win = browser.documentGlobal;
    const tab = win?.gBrowser.getTabForBrowser(browser);
    if (!tab) {
      return;
    }
    this._getTabStateManager(win)?.setTabStateConversation(tab, conversation);
  },

  unloadWindow(win) {
    if (this.isAIWindowActive(win)) {
      const duration_ms = this._consumeActiveDuration(win);
      const opened_tabs = win.gBrowser?.tabs.length ?? 0;
      Glean.smartWindow.closeWindow.record({ duration_ms, opened_tabs });
      this._recordSmartWindowUsage();
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
    const authorized = await lazy.AuthProvider.maybeSignIn(
      win.gBrowser.selectedBrowser
    );

    if (!authorized) {
      return false;
    }

    this.toggleAIWindow(win, true, trigger);

    const triggeringPrincipal =
      Services.scriptSecurityManager.getSystemPrincipal();

    if (!lazy.hasFirstrunCompleted) {
      win.gBrowser.loadURI(FIRSTRUN_URI, { triggeringPrincipal });
    } else if (trigger === "startup") {
      // Startup navigation to about:newtab may still be pending when
      // _reconcileNewTabPages runs. Redirect any affected tabs to the
      // AI window URL to ensure they open as Smart Windows.
      const aiWindowURI = Services.io.newURI(AIWINDOW_URL);
      for (const tab of win.gBrowser.tabs) {
        if (tab.linkedBrowser?.currentURI?.spec === "about:blank") {
          tab.linkedBrowser.loadURI(aiWindowURI, { triggeringPrincipal });
        }
      }
    }

    return true;
  },

  async launchWindow(browser, openNewWindow = false, trigger = "other") {
    try {
      // Early return when Smart Window is blocked from AI Control
      if (this.isBlocked) {
        return false;
      }

      this.recordLaunchCommandTelemetry(trigger, openNewWindow);

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

      const isAuthorized = await lazy.AuthProvider.hasAccess();
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
   * Launches the sign-in auth flow for the given browser.
   *
   * @param {Browser} browser
   * @returns {Promise<boolean>} Whether the user signed in successfully
   */
  async launchSignInFlow(browser) {
    try {
      return await lazy.AuthProvider.promptSignIn(browser);
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
      root.removeAttribute("aiwindow-first-run");
      this._updateMonitorButtonForWindow(win);
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
    const isFirstRunView = isFirstRun && isImmersiveView;
    // Leaving first run reveals the previously hidden nav-bar; notify the urlbar
    // so it can recompute its popover layout for the now-visible bar.
    if (root.hasAttribute("aiwindow-first-run") && !isFirstRunView) {
      Services.obs.notifyObservers(
        win,
        "ai-window-state-changed",
        "nav-bar-visible"
      );
    }
    root.toggleAttribute("aiwindow-first-run", isFirstRunView);
    root.toggleAttribute("aiwindow-immersive-view", isImmersiveView);

    const askButton = win.document.getElementById("smartwindow-ask-button");
    if (askButton) {
      askButton.hidden = isImmersiveView;
    }

    this._updateMonitorButtonForWindow(win);

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

  _createSwitcherWidget() {
    if (this._switcherWidgetCreated) {
      return;
    }

    lazy.CustomizableUI.createWidget({
      id: "ai-window-toggle",
      l10nId: "toolbar-switcher-customizable-label-v2",
      type: "view",
      viewId: "ai-window-toggle-view",
      defaultArea: lazy.CustomizableUI.AREA_TABSTRIP,
      defaultAreaVerticalTabs: lazy.CustomizableUI.AREA_NAVBAR,
      removable: true,
      showInPrivateBrowsing: false,
      onCreated: node => {
        node.setAttribute("badged", "true");
        node.classList.add("subviewbutton-nav");
        node.setAttribute("aria-haspopup", "true");
        this._updateButtonVisibility(node);
      },
      onViewShowing: event => {
        const win = event.target.documentGlobal;
        this.handleAIWindowSwitcher(win);
      },
    });
    this._switcherWidgetCreated = true;
  },

  _createGroupTabsWidget() {
    if (this._groupTabsWidgetCreated) {
      return;
    }

    lazy.CustomizableUI.createWidget({
      id: GROUP_TABS_BUTTON_ID,
      l10nId: "smartwindow-organize-tabs-button",
      defaultArea: lazy.CustomizableUI.AREA_TABSTRIP,
      defaultAreaVerticalTabs: lazy.CustomizableUI.AREA_NAVBAR,
      removable: true,
      showInPrivateBrowsing: false,
      onCreated: node => {
        node.setAttribute("aria-haspopup", "dialog");
        node.setAttribute("aria-expanded", "false");
        this._updateGroupTabsButtonVisibility(node);
      },
      onCommand: event => {
        lazy.AIWindowUI.toggleGroupTabsPanel(event.target.documentGlobal);
      },
    });
    this._groupTabsWidgetCreated = true;
  },

  _destroyGroupTabsWidget() {
    if (!this._groupTabsWidgetCreated) {
      return;
    }

    lazy.CustomizableUI.destroyWidget(GROUP_TABS_BUTTON_ID);
    this._groupTabsWidgetCreated = false;
  },

  /**
   * The "Organize Tabs" button is a CustomizableUI button, it must not exist
   * when the feature is off or when Smart Window is blocked by
   * browser.ai.control.
   */
  _updateGroupTabsWidgetRegistration() {
    if (lazy.autoTabGroupingEnabled && !this.isBlocked) {
      this._createGroupTabsWidget();
      return;
    }
    this._destroyGroupTabsWidget();
  },

  _destroySwitcherWidget() {
    if (!this._switcherWidgetCreated) {
      return;
    }

    lazy.CustomizableUI.destroyWidget("ai-window-toggle");
    this._switcherWidgetCreated = false;
  },

  // The window switcher must not appear anywhere in the customizable UI (toolbar or
  // customize palette) when Smart Window is blocked by the browser.ai.control
  // or browser.ai.control.smartWindow.
  _updateSwitcherWidgetRegistration() {
    if (this.isBlocked) {
      this._destroySwitcherWidget();
    } else {
      this._createSwitcherWidget();
    }
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
    lazy.AuthProvider.clearToUConsent();
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
