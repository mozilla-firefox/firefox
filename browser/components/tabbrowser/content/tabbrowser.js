/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-env mozilla/browser-window */

{
  // start private scope for Tabbrowser
  /**
   * A set of known icons to use for internal pages. These are hardcoded so we can
   * start loading them faster than FaviconLoader would normally find them.
   */
  const FAVICON_DEFAULTS = {
    "about:newtab": "chrome://branding/content/icon32.png",
    "about:home": "chrome://branding/content/icon32.png",
    "about:welcome": "chrome://branding/content/icon32.png",
    "about:privatebrowsing":
      "chrome://browser/skin/privatebrowsing/favicon.svg",
  };

  const {
    LOAD_FLAGS_NONE,
    LOAD_FLAGS_FROM_EXTERNAL,
    LOAD_FLAGS_FIRST_LOAD,
    LOAD_FLAGS_DISALLOW_INHERIT_PRINCIPAL,
    LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP,
    LOAD_FLAGS_FIXUP_SCHEME_TYPOS,
    LOAD_FLAGS_FORCE_ALLOW_DATA_URI,
    LOAD_FLAGS_DISABLE_TRR,
  } = Ci.nsIWebNavigation;

  const DIRECTION_FORWARD = 1;
  const DIRECTION_BACKWARD = -1;

  /**
   * Updates the User Context UI indicators if the browser is in a non-default context
   */
  function updateUserContextUIIndicator() {
    function replaceContainerClass(classType, element, value) {
      let prefix = "identity-" + classType + "-";
      if (value && element.classList.contains(prefix + value)) {
        return;
      }
      for (let className of element.classList) {
        if (className.startsWith(prefix)) {
          element.classList.remove(className);
        }
      }
      if (value) {
        element.classList.add(prefix + value);
      }
    }

    let hbox = document.getElementById("userContext-icons");

    let userContextId = gBrowser.selectedBrowser.getAttribute("usercontextid");
    if (!userContextId) {
      replaceContainerClass("color", hbox, "");
      hbox.hidden = true;
      return;
    }

    let identity =
      ContextualIdentityService.getPublicIdentityFromId(userContextId);
    if (!identity) {
      replaceContainerClass("color", hbox, "");
      hbox.hidden = true;
      return;
    }

    replaceContainerClass("color", hbox, identity.color);

    let label = ContextualIdentityService.getUserContextLabel(userContextId);
    document.getElementById("userContext-label").textContent = label;
    // Also set the container label as the tooltip so we can only show the icon
    // in small windows.
    hbox.setAttribute("tooltiptext", label);

    let indicator = document.getElementById("userContext-indicator");
    replaceContainerClass("icon", indicator, identity.icon);

    hbox.hidden = false;
  }

  async function getTotalMemoryUsage() {
    const procInfo = await ChromeUtils.requestProcInfo();
    let totalMemoryUsage = procInfo.memory;
    for (const child of procInfo.children) {
      totalMemoryUsage += child.memory;
    }
    return totalMemoryUsage;
  }

  window.Tabbrowser = class {
    init() {
      this.tabContainer = document.getElementById("tabbrowser-tabs");
      this.tabGroupMenu = document.getElementById("tab-group-editor");
      this.tabbox = document.getElementById("tabbrowser-tabbox");
      this.tabpanels = document.getElementById("tabbrowser-tabpanels");
      this.verticalPinnedTabsContainer = document.getElementById(
        "vertical-pinned-tabs-container"
      );

      ChromeUtils.defineESModuleGetters(this, {
        AsyncTabSwitcher:
          "moz-src:///browser/components/tabbrowser/AsyncTabSwitcher.sys.mjs",
        PictureInPicture: "resource://gre/modules/PictureInPicture.sys.mjs",
        SmartTabGroupingManager:
          "moz-src:///browser/components/tabbrowser/SmartTabGrouping.sys.mjs",
        TabMetrics:
          "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs",
        TabStateFlusher:
          "resource:///modules/sessionstore/TabStateFlusher.sys.mjs",
        UrlbarProviderOpenTabs:
          "resource:///modules/UrlbarProviderOpenTabs.sys.mjs",
      });
      ChromeUtils.defineLazyGetter(this, "tabLocalization", () => {
        return new Localization(
          ["browser/tabbrowser.ftl", "branding/brand.ftl"],
          true
        );
      });
      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "_shouldExposeContentTitle",
        "privacy.exposeContentTitleInWindow",
        true
      );
      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "_shouldExposeContentTitlePbm",
        "privacy.exposeContentTitleInWindow.pbm",
        true
      );
      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "_showTabCardPreview",
        "browser.tabs.hoverPreview.enabled",
        true
      );
      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "_allowTransparentBrowser",
        "browser.tabs.allow_transparent_browser",
        false
      );
      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "_tabGroupsEnabled",
        "browser.tabs.groups.enabled",
        false
      );
      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "showPidAndActiveness",
        "browser.tabs.tooltipsShowPidAndActiveness",
        false
      );
      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "_unloadTabInContextMenu",
        "browser.tabs.unloadTabInContextMenu",
        false
      );
      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "_notificationEnableDelay",
        "security.notification_enable_delay",
        500
      );

      if (AppConstants.MOZ_CRASHREPORTER) {
        ChromeUtils.defineESModuleGetters(this, {
          TabCrashHandler: "resource:///modules/ContentCrashHandlers.sys.mjs",
        });
      }

      Services.obs.addObserver(this, "contextual-identity-updated");

      document.addEventListener("keydown", this, { mozSystemGroup: true });
      document.addEventListener("keypress", this, { mozSystemGroup: true });
      document.addEventListener("visibilitychange", this);
      window.addEventListener("framefocusrequested", this);
      window.addEventListener("activate", this);
      window.addEventListener("deactivate", this);
      window.addEventListener("TabGroupCreate", this);

      this.tabContainer.init();
      this._setupInitialBrowserAndTab();

      if (
        Services.prefs.getIntPref("browser.display.document_color_use") == 2
      ) {
        this.tabpanels.style.backgroundColor = Services.prefs.getCharPref(
          "browser.display.background_color"
        );
      }

      this._setFindbarData();

      // We take over setting the document title, so remove the l10n id to
      // avoid it being re-translated and overwriting document content if
      // we ever switch languages at runtime. After a language change, the
      // window title will update at the next tab or location change.
      document.querySelector("title").removeAttribute("data-l10n-id");

      this._setupEventListeners();
      this._initialized = true;
    }

    ownerGlobal = window;

    ownerDocument = document;

    closingTabsEnum = {
      ALL: 0,
      OTHER: 1,
      TO_START: 2,
      TO_END: 3,
      MULTI_SELECTED: 4,
      DUPLICATES: 6,
      ALL_DUPLICATES: 7,
    };

    _lastRelatedTabMap = new WeakMap();

    mProgressListeners = [];

    mTabsProgressListeners = [];

    _tabListeners = new Map();

    _tabFilters = new Map();

    _isBusy = false;

    _awaitingToggleCaretBrowsingPrompt = false;

    _previewMode = false;

    _lastFindValue = "";

    _contentWaitingCount = 0;

    _tabLayerCache = [];

    tabAnimationsInProgress = 0;

    /**
     * Binding from browser to tab
     */
    _tabForBrowser = new WeakMap();

    /**
     * `_createLazyBrowser` will define properties on the unbound lazy browser
     * which correspond to properties defined in MozBrowser which will be bound to
     * the browser when it is inserted into the document.  If any of these
     * properties are accessed by consumers, `_insertBrowser` is called and
     * the browser is inserted to ensure that things don't break.  This list
     * provides the names of properties that may be called while the browser
     * is in its unbound (lazy) state.
     */
    _browserBindingProperties = [
      "canGoBack",
      "canGoForward",
      "goBack",
      "goForward",
      "permitUnload",
      "reload",
      "reloadWithFlags",
      "stop",
      "loadURI",
      "fixupAndLoadURIString",
      "gotoIndex",
      "currentURI",
      "documentURI",
      "remoteType",
      "preferences",
      "imageDocument",
      "isRemoteBrowser",
      "messageManager",
      "getTabBrowser",
      "finder",
      "fastFind",
      "sessionHistory",
      "contentTitle",
      "characterSet",
      "fullZoom",
      "textZoom",
      "tabHasCustomZoom",
      "webProgress",
      "addProgressListener",
      "removeProgressListener",
      "audioPlaybackStarted",
      "audioPlaybackStopped",
      "resumeMedia",
      "mute",
      "unmute",
      "blockedPopups",
      "lastURI",
      "purgeSessionHistory",
      "stopScroll",
      "startScroll",
      "userTypedValue",
      "userTypedClear",
      "didStartLoadSinceLastUserTyping",
      "audioMuted",
    ];

    _removingTabs = new Set();

    _multiSelectedTabsSet = new WeakSet();

    _lastMultiSelectedTabRef = null;

    _clearMultiSelectionLocked = false;

    _clearMultiSelectionLockedOnce = false;

    _multiSelectChangeStarted = false;

    _multiSelectChangeAdditions = new Set();

    _multiSelectChangeRemovals = new Set();

    _multiSelectChangeSelected = false;

    /**
     * Tab close requests are ignored if the window is closing anyway,
     * e.g. when holding Ctrl+W.
     */
    _windowIsClosing = false;

    preloadedBrowser = null;

    /**
     * This defines a proxy which allows us to access browsers by
     * index without actually creating a full array of browsers.
     */
    browsers = new Proxy([], {
      has: (target, name) => {
        if (typeof name == "string" && Number.isInteger(parseInt(name))) {
          return name in gBrowser.tabs;
        }
        return false;
      },
      get: (target, name) => {
        if (name == "length") {
          return gBrowser.tabs.length;
        }
        if (typeof name == "string" && Number.isInteger(parseInt(name))) {
          if (!(name in gBrowser.tabs)) {
            return undefined;
          }
          return gBrowser.tabs[name].linkedBrowser;
        }
        return target[name];
      },
    });

    /**
     * List of browsers whose docshells must be active in order for print preview
     * to work.
     */
    _printPreviewBrowsers = new Set();

    _switcher = null;

    _soundPlayingAttrRemovalTimer = 0;

    _hoverTabTimer = null;

    get tabs() {
      return this.tabContainer.allTabs;
    }

    get tabGroups() {
      return this.tabContainer.allGroups;
    }

    get tabsInCollapsedTabGroups() {
      return this.tabGroups
        .filter(tabGroup => tabGroup.collapsed)
        .flatMap(tabGroup => tabGroup.tabs)
        .filter(tab => !tab.hidden && !tab.closing);
    }

    addEventListener(...args) {
      this.tabpanels.addEventListener(...args);
    }

    removeEventListener(...args) {
      this.tabpanels.removeEventListener(...args);
    }

    dispatchEvent(...args) {
      return this.tabpanels.dispatchEvent(...args);
    }

    /**
     * Returns all tabs in the current window, including hidden tabs and tabs
     * in collapsed groups, but excluding closing tabs and the Firefox View tab.
     */
    get openTabs() {
      return this.tabContainer.openTabs;
    }

    /**
     * Same as `openTabs` but excluding hidden tabs and tabs in collapsed groups.
     */
    get visibleTabs() {
      return this.tabContainer.visibleTabs;
    }

    get pinnedTabCount() {
      for (var i = 0; i < this.tabs.length; i++) {
        if (!this.tabs[i].pinned) {
          break;
        }
      }
      return i;
    }

    set selectedTab(val) {
      if (
        gSharedTabWarning.willShowSharedTabWarning(val) ||
        document.documentElement.hasAttribute("window-modal-open") ||
        (gNavToolbox.collapsed && !this._allowTabChange)
      ) {
        return;
      }
      // Update the tab
      this.tabbox.selectedTab = val;
    }

    get selectedTab() {
      return this._selectedTab;
    }

    get selectedBrowser() {
      return this._selectedBrowser;
    }

    _setupInitialBrowserAndTab() {
      // See browser.js for the meaning of window.arguments.
      // Bug 1485961 covers making this more sane.
      let userContextId = window.arguments && window.arguments[5];

      let openWindowInfo = window.docShell.treeOwner
        .QueryInterface(Ci.nsIInterfaceRequestor)
        .getInterface(Ci.nsIAppWindow).initialOpenWindowInfo;

      if (!openWindowInfo && window.arguments && window.arguments[11]) {
        openWindowInfo = window.arguments[11];
      }

      let extraOptions;
      if (window.arguments?.[1] instanceof Ci.nsIPropertyBag2) {
        extraOptions = window.arguments[1];
      }

      // If our opener provided a remoteType which was responsible for creating
      // this pop-up window, we'll fall back to using that remote type when no
      // other remote type is available.
      let triggeringRemoteType;
      if (extraOptions?.hasKey("triggeringRemoteType")) {
        triggeringRemoteType = extraOptions.getPropertyAsACString(
          "triggeringRemoteType"
        );
      }

      let tabArgument = gBrowserInit.getTabToAdopt();

      // If we have a tab argument with browser, we use its remoteType. Otherwise,
      // if e10s is disabled or there's a parent process opener (e.g. parent
      // process about: page) for the content tab, we use a parent
      // process remoteType. Otherwise, we check the URI to determine
      // what to do - if there isn't one, we default to the default remote type.
      //
      // When adopting a tab, we'll also use that tab's browsingContextGroupId,
      // if available, to ensure we don't spawn a new process.
      let remoteType;
      let initialBrowsingContextGroupId;

      if (tabArgument && tabArgument.hasAttribute("usercontextid")) {
        // The window's first argument is a tab if and only if we are swapping tabs.
        // We must set the browser's usercontextid so that the newly created remote
        // tab child has the correct usercontextid.
        userContextId = parseInt(tabArgument.getAttribute("usercontextid"), 10);
      }

      if (tabArgument && tabArgument.linkedBrowser) {
        remoteType = tabArgument.linkedBrowser.remoteType;
        initialBrowsingContextGroupId =
          tabArgument.linkedBrowser.browsingContext?.group.id;
      } else if (openWindowInfo) {
        userContextId = openWindowInfo.originAttributes.userContextId;
        if (openWindowInfo.isRemote) {
          remoteType = triggeringRemoteType ?? E10SUtils.DEFAULT_REMOTE_TYPE;
        } else {
          remoteType = E10SUtils.NOT_REMOTE;
        }
      } else {
        let uriToLoad = gBrowserInit.uriToLoadPromise;
        if (uriToLoad && Array.isArray(uriToLoad)) {
          uriToLoad = uriToLoad[0]; // we only care about the first item
        }

        if (uriToLoad && typeof uriToLoad == "string") {
          let oa = E10SUtils.predictOriginAttributes({
            window,
            userContextId,
          });
          remoteType = E10SUtils.getRemoteTypeForURI(
            uriToLoad,
            gMultiProcessBrowser,
            gFissionBrowser,
            triggeringRemoteType ?? E10SUtils.DEFAULT_REMOTE_TYPE,
            null,
            oa
          );
        } else {
          // If we reach here, we don't have the url to load. This means that
          // `uriToLoad` is most likely a promise which is waiting on SessionStore
          // initialization. We can't delay setting up the browser here, as that
          // would mean that `gBrowser.selectedBrowser` might not always exist,
          // which is the current assumption.

          if (Cu.isInAutomation) {
            ChromeUtils.releaseAssert(
              !triggeringRemoteType,
              "Unexpected triggeringRemoteType with no uriToLoad"
            );
          }

          // In this case we default to the privileged about process as that's
          // the best guess we can make, and we'll likely need it eventually.
          remoteType = E10SUtils.PRIVILEGEDABOUT_REMOTE_TYPE;
        }
      }

      let createOptions = {
        uriIsAboutBlank: false,
        userContextId,
        initialBrowsingContextGroupId,
        remoteType,
        openWindowInfo,
      };
      let browser = this.createBrowser(createOptions);
      browser.setAttribute("primary", "true");
      if (gBrowserAllowScriptsToCloseInitialTabs) {
        browser.setAttribute("allowscriptstoclose", "true");
      }
      browser.droppedLinkHandler = handleDroppedLink;
      browser.loadURI = URILoadingWrapper.loadURI.bind(
        URILoadingWrapper,
        browser
      );
      browser.fixupAndLoadURIString =
        URILoadingWrapper.fixupAndLoadURIString.bind(
          URILoadingWrapper,
          browser
        );

      let uniqueId = this._generateUniquePanelID();
      let panel = this.getPanel(browser);
      panel.id = uniqueId;
      this.tabpanels.appendChild(panel);

      let tab = this.tabs[0];
      tab.linkedPanel = uniqueId;
      this._selectedTab = tab;
      this._selectedBrowser = browser;
      tab.permanentKey = browser.permanentKey;
      tab._tPos = 0;
      tab._fullyOpen = true;
      tab.linkedBrowser = browser;

      if (userContextId) {
        tab.setAttribute("usercontextid", userContextId);
        ContextualIdentityService.setTabStyle(tab);
      }

      this._tabForBrowser.set(browser, tab);

      this._appendStatusPanel();

      // This is the initial browser, so it's usually active; the default is false
      // so we have to update it:
      browser.docShellIsActive = this.shouldActivateDocShell(browser);

      // Hook the browser up with a progress listener.
      let tabListener = new TabProgressListener(tab, browser, true, false);
      let filter = Cc[
        "@mozilla.org/appshell/component/browser-status-filter;1"
      ].createInstance(Ci.nsIWebProgress);
      filter.addProgressListener(tabListener, Ci.nsIWebProgress.NOTIFY_ALL);
      this._tabListeners.set(tab, tabListener);
      this._tabFilters.set(tab, filter);
      browser.webProgress.addProgressListener(
        filter,
        Ci.nsIWebProgress.NOTIFY_ALL
      );
    }

    /**
     * BEGIN FORWARDED BROWSER PROPERTIES.  IF YOU ADD A PROPERTY TO THE BROWSER ELEMENT
     * MAKE SURE TO ADD IT HERE AS WELL.
     */
    get canGoBack() {
      return this.selectedBrowser.canGoBack;
    }

    get canGoBackIgnoringUserInteraction() {
      return this.selectedBrowser.canGoBackIgnoringUserInteraction;
    }

    get canGoForward() {
      return this.selectedBrowser.canGoForward;
    }

    goBack(requireUserInteraction) {
      return this.selectedBrowser.goBack(requireUserInteraction);
    }

    goForward(requireUserInteraction) {
      return this.selectedBrowser.goForward(requireUserInteraction);
    }

    reload() {
      return this.selectedBrowser.reload();
    }

    reloadWithFlags(aFlags) {
      return this.selectedBrowser.reloadWithFlags(aFlags);
    }

    stop() {
      return this.selectedBrowser.stop();
    }

    /**
     * throws exception for unknown schemes
     */
    loadURI(uri, params) {
      return this.selectedBrowser.loadURI(uri, params);
    }
    /**
     * throws exception for unknown schemes
     */
    fixupAndLoadURIString(uriString, params) {
      return this.selectedBrowser.fixupAndLoadURIString(uriString, params);
    }

    gotoIndex(aIndex) {
      return this.selectedBrowser.gotoIndex(aIndex);
    }

    get currentURI() {
      return this.selectedBrowser.currentURI;
    }

    get finder() {
      return this.selectedBrowser.finder;
    }

    get docShell() {
      return this.selectedBrowser.docShell;
    }

    get webNavigation() {
      return this.selectedBrowser.webNavigation;
    }

    get webProgress() {
      return this.selectedBrowser.webProgress;
    }

    get contentWindow() {
      return this.selectedBrowser.contentWindow;
    }

    get sessionHistory() {
      return this.selectedBrowser.sessionHistory;
    }

    get contentDocument() {
      return this.selectedBrowser.contentDocument;
    }

    get contentTitle() {
      return this.selectedBrowser.contentTitle;
    }

    get contentPrincipal() {
      return this.selectedBrowser.contentPrincipal;
    }

    get securityUI() {
      return this.selectedBrowser.securityUI;
    }

    set fullZoom(val) {
      this.selectedBrowser.fullZoom = val;
    }

    get fullZoom() {
      return this.selectedBrowser.fullZoom;
    }

    set textZoom(val) {
      this.selectedBrowser.textZoom = val;
    }

    get textZoom() {
      return this.selectedBrowser.textZoom;
    }

    get isSyntheticDocument() {
      return this.selectedBrowser.isSyntheticDocument;
    }

    set userTypedValue(val) {
      this.selectedBrowser.userTypedValue = val;
    }

    get userTypedValue() {
      return this.selectedBrowser.userTypedValue;
    }

    _setFindbarData() {
      // Ensure we know what the find bar key is in the content process:
      let { sharedData } = Services.ppmm;
      if (!sharedData.has("Findbar:Shortcut")) {
        let keyEl = document.getElementById("key_find");
        let mods = keyEl
          .getAttribute("modifiers")
          .replace(
            /accel/i,
            AppConstants.platform == "macosx" ? "meta" : "control"
          );
        sharedData.set("Findbar:Shortcut", {
          key: keyEl.getAttribute("key"),
          shiftKey: mods.includes("shift"),
          ctrlKey: mods.includes("control"),
          altKey: mods.includes("alt"),
          metaKey: mods.includes("meta"),
        });
      }
    }

    isFindBarInitialized(aTab) {
      return (aTab || this.selectedTab)._findBar != undefined;
    }

    /**
     * Get the already constructed findbar
     */
    getCachedFindBar(aTab = this.selectedTab) {
      return aTab._findBar;
    }

    /**
     * Get the findbar, and create it if it doesn't exist.
     * @return the find bar (or null if the window or tab is closed/closing in the interim).
     */
    async getFindBar(aTab = this.selectedTab) {
      let findBar = this.getCachedFindBar(aTab);
      if (findBar) {
        return findBar;
      }

      // Avoid re-entrancy by caching the promise we're about to return.
      if (!aTab._pendingFindBar) {
        aTab._pendingFindBar = this._createFindBar(aTab);
      }
      return aTab._pendingFindBar;
    }

    /**
     * Create a findbar instance.
     * @param aTab the tab to create the find bar for.
     * @return the created findbar, or null if the window or tab is closed/closing.
     */
    async _createFindBar(aTab) {
      let findBar = document.createXULElement("findbar");
      let browser = this.getBrowserForTab(aTab);

      browser.parentNode.insertAdjacentElement("afterend", findBar);

      await new Promise(r => requestAnimationFrame(r));
      delete aTab._pendingFindBar;
      if (window.closed || aTab.closing) {
        return null;
      }

      findBar.browser = browser;
      findBar._findField.value = this._lastFindValue;

      aTab._findBar = findBar;

      let event = document.createEvent("Events");
      event.initEvent("TabFindInitialized", true, false);
      aTab.dispatchEvent(event);

      return findBar;
    }

    _appendStatusPanel() {
      this.selectedBrowser.insertAdjacentElement("afterend", StatusPanel.panel);
    }

    _updateTabBarForPinnedTabs() {
      this.tabContainer._unlockTabSizing();
      this.tabContainer._positionPinnedTabs();
      this.tabContainer._updateCloseButtons();
    }

    _notifyPinnedStatus(aTab) {
      // browsingContext is expected to not be defined on discarded tabs.
      if (aTab.linkedBrowser.browsingContext) {
        aTab.linkedBrowser.browsingContext.isAppTab = aTab.pinned;
      }

      let event = document.createEvent("Events");
      event.initEvent(aTab.pinned ? "TabPinned" : "TabUnpinned", true, false);
      aTab.dispatchEvent(event);
    }

    pinTab(aTab) {
      if (aTab.pinned || aTab == FirefoxViewHandler.tab) {
        return;
      }

      this.showTab(aTab);
      if (this.tabContainer.verticalMode) {
        this.#handleTabMove(aTab, () =>
          this.verticalPinnedTabsContainer.appendChild(aTab)
        );
      } else {
        this.moveTabTo(aTab, {
          tabIndex: this.pinnedTabCount,
          forceUngrouped: true,
        });
      }
      aTab.setAttribute("pinned", "true");
      this._updateTabBarForPinnedTabs();
      this._notifyPinnedStatus(aTab);
    }

    unpinTab(aTab) {
      if (!aTab.pinned) {
        return;
      }

      if (this.tabContainer.verticalMode) {
        this.#handleTabMove(aTab, () => {
          // we remove this attribute first, so that allTabs represents
          // the moving of a tab from the vertical pinned tabs container
          // and back into arrowscrollbox.
          aTab.removeAttribute("pinned");
          this.tabContainer.arrowScrollbox.prepend(aTab);
        });
      } else {
        this.moveTabTo(aTab, {
          tabIndex: this.pinnedTabCount - 1,
          forceUngrouped: true,
        });
        aTab.removeAttribute("pinned");
      }
      aTab.style.marginInlineStart = "";
      aTab._pinnedUnscrollable = false;
      this._updateTabBarForPinnedTabs();
      this._notifyPinnedStatus(aTab);
    }

    previewTab(aTab, aCallback) {
      let currentTab = this.selectedTab;
      try {
        // Suppress focus, ownership and selected tab changes
        this._previewMode = true;
        this.selectedTab = aTab;
        aCallback();
      } finally {
        this.selectedTab = currentTab;
        this._previewMode = false;
      }
    }

    getBrowserAtIndex(aIndex) {
      return this.browsers[aIndex];
    }

    getBrowserForOuterWindowID(aID) {
      for (let b of this.browsers) {
        if (b.outerWindowID == aID) {
          return b;
        }
      }

      return null;
    }

    getTabForBrowser(aBrowser) {
      return this._tabForBrowser.get(aBrowser);
    }

    getPanel(aBrowser) {
      return this.getBrowserContainer(aBrowser).parentNode;
    }

    getBrowserContainer(aBrowser) {
      return (aBrowser || this.selectedBrowser).parentNode.parentNode;
    }

    getTabNotificationDeck() {
      if (!this._tabNotificationDeck) {
        let template = document.getElementById(
          "tab-notification-deck-template"
        );
        template.replaceWith(template.content);
        this._tabNotificationDeck = document.getElementById(
          "tab-notification-deck"
        );
      }
      return this._tabNotificationDeck;
    }

    _nextNotificationBoxId = 0;
    getNotificationBox(aBrowser) {
      let browser = aBrowser || this.selectedBrowser;
      if (!browser._notificationBox) {
        browser._notificationBox = new MozElements.NotificationBox(element => {
          element.setAttribute("notificationside", "top");
          element.setAttribute(
            "name",
            `tab-notification-box-${this._nextNotificationBoxId++}`
          );
          this.getTabNotificationDeck().append(element);
          if (browser == this.selectedBrowser) {
            this._updateVisibleNotificationBox(browser);
          }
        }, this._notificationEnableDelay);
      }
      return browser._notificationBox;
    }

    readNotificationBox(aBrowser) {
      let browser = aBrowser || this.selectedBrowser;
      return browser._notificationBox || null;
    }

    _updateVisibleNotificationBox(aBrowser) {
      if (!this._tabNotificationDeck) {
        // If the deck hasn't been created we don't need to create it here.
        return;
      }
      let notificationBox = this.readNotificationBox(aBrowser);
      this.getTabNotificationDeck().selectedViewName = notificationBox
        ? notificationBox.stack.getAttribute("name")
        : "";
    }

    getTabDialogBox(aBrowser) {
      if (!aBrowser) {
        throw new Error("aBrowser is required");
      }
      if (!aBrowser.tabDialogBox) {
        aBrowser.tabDialogBox = new TabDialogBox(aBrowser);
      }
      return aBrowser.tabDialogBox;
    }

    getTabFromAudioEvent(aEvent) {
      if (!aEvent.isTrusted) {
        return null;
      }

      var browser = aEvent.originalTarget;
      var tab = this.getTabForBrowser(browser);
      return tab;
    }

    _callProgressListeners(
      aBrowser,
      aMethod,
      aArguments,
      aCallGlobalListeners = true,
      aCallTabsListeners = true
    ) {
      var rv = true;

      function callListeners(listeners, args) {
        for (let p of listeners) {
          if (aMethod in p) {
            try {
              if (!p[aMethod].apply(p, args)) {
                rv = false;
              }
            } catch (e) {
              // don't inhibit other listeners
              console.error(e);
            }
          }
        }
      }

      aBrowser = aBrowser || this.selectedBrowser;

      if (aCallGlobalListeners && aBrowser == this.selectedBrowser) {
        callListeners(this.mProgressListeners, aArguments);
      }

      if (aCallTabsListeners) {
        aArguments.unshift(aBrowser);

        callListeners(this.mTabsProgressListeners, aArguments);
      }

      return rv;
    }

    /**
     * Sets an icon for the tab if the URI is defined in FAVICON_DEFAULTS.
     */
    setDefaultIcon(aTab, aURI) {
      if (aURI && aURI.spec in FAVICON_DEFAULTS) {
        this.setIcon(aTab, FAVICON_DEFAULTS[aURI.spec]);
      }
    }

    setIcon(
      aTab,
      aIconURL = "",
      aOriginalURL = aIconURL,
      aLoadingPrincipal = null,
      aClearImageFirst = false
    ) {
      let makeString = url => (url instanceof Ci.nsIURI ? url.spec : url);

      aIconURL = makeString(aIconURL);
      aOriginalURL = makeString(aOriginalURL);

      let LOCAL_PROTOCOLS = ["chrome:", "about:", "resource:", "data:"];

      if (
        aIconURL &&
        !aLoadingPrincipal &&
        !LOCAL_PROTOCOLS.some(protocol => aIconURL.startsWith(protocol))
      ) {
        console.error(
          `Attempt to set a remote URL ${aIconURL} as a tab icon without a loading principal.`
        );
        return;
      }

      let browser = this.getBrowserForTab(aTab);
      browser.mIconURL = aIconURL;

      if (aIconURL != aTab.getAttribute("image")) {
        if (aClearImageFirst) {
          aTab.removeAttribute("image");
        }
        if (aIconURL) {
          if (aLoadingPrincipal) {
            aTab.setAttribute("iconloadingprincipal", aLoadingPrincipal);
          } else {
            aTab.removeAttribute("iconloadingprincipal");
          }
          aTab.setAttribute("image", aIconURL);
        } else {
          aTab.removeAttribute("image");
          aTab.removeAttribute("iconloadingprincipal");
        }
        this._tabAttrModified(aTab, ["image"]);
      }

      // The aOriginalURL argument is currently only used by tests.
      this._callProgressListeners(browser, "onLinkIconAvailable", [
        aIconURL,
        aOriginalURL,
      ]);
    }

    getIcon(aTab) {
      let browser = aTab ? this.getBrowserForTab(aTab) : this.selectedBrowser;
      return browser.mIconURL;
    }

    setPageInfo(tab, aURL, aDescription, aPreviewImage) {
      if (aURL) {
        let pageInfo = {
          url: aURL,
          description: aDescription,
          previewImageURL: aPreviewImage,
        };
        PlacesUtils.history.update(pageInfo).catch(console.error);
      }
      if (tab) {
        tab.description = aDescription;
      }
    }

    getWindowTitleForBrowser(aBrowser) {
      let docElement = document.documentElement;
      let title = "";
      let dataSuffix =
        docElement.getAttribute("privatebrowsingmode") == "temporary"
          ? "Private"
          : "Default";

      if (
        SelectableProfileService?.isEnabled &&
        SelectableProfileService.currentProfile
      ) {
        dataSuffix += "WithProfile";
      }
      let defaultTitle = docElement.dataset["title" + dataSuffix].replace(
        "PROFILENAME",
        () => SelectableProfileService.currentProfile.name.replace(/\0/g, "")
      );

      if (
        !this._shouldExposeContentTitle ||
        (PrivateBrowsingUtils.isWindowPrivate(window) &&
          !this._shouldExposeContentTitlePbm)
      ) {
        return defaultTitle;
      }

      // If location bar is hidden and the URL type supports a host,
      // add the scheme and host to the title to prevent spoofing.
      // XXX https://bugzilla.mozilla.org/show_bug.cgi?id=22183#c239
      try {
        if (docElement.getAttribute("chromehidden").includes("location")) {
          const uri = Services.io.createExposableURI(aBrowser.currentURI);
          let prefix = uri.prePath;
          if (uri.scheme == "about") {
            prefix = uri.spec;
          } else if (uri.scheme == "moz-extension") {
            const ext = WebExtensionPolicy.getByHostname(uri.host);
            if (ext && ext.name) {
              let extensionLabel = document.getElementById(
                "urlbar-label-extension"
              );
              prefix = `${extensionLabel.value} (${ext.name})`;
            }
          }
          title = prefix + " - ";
        }
      } catch (e) {
        // ignored
      }

      if (docElement.hasAttribute("titlepreface")) {
        title += docElement.getAttribute("titlepreface");
      }

      let tab = this.getTabForBrowser(aBrowser);
      if (tab._labelIsContentTitle) {
        // Strip out any null bytes in the content title, since the
        // underlying widget implementations of nsWindow::SetTitle pass
        // null-terminated strings to system APIs.
        title += tab.getAttribute("label").replace(/\0/g, "");
      }

      if (title) {
        // We're using a function rather than just using `title` as the
        // new substring to avoid `$$`, `$'` etc. having a special
        // meaning to `replace`.
        // See https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/String/replace#specifying_a_string_as_a_parameter
        // and the documentation for functions for more info about this.
        return docElement.dataset["contentTitle" + dataSuffix]
          .replace("CONTENTTITLE", () => title)
          .replace(
            "PROFILENAME",
            () =>
              SelectableProfileService?.currentProfile?.name.replace(
                /\0/g,
                ""
              ) ?? ""
          );
      }

      return defaultTitle;
    }

    updateTitlebar() {
      document.title = this.getWindowTitleForBrowser(this.selectedBrowser);
    }

    updateCurrentBrowser(aForceUpdate) {
      let newBrowser = this.getBrowserAtIndex(this.tabContainer.selectedIndex);
      if (this.selectedBrowser == newBrowser && !aForceUpdate) {
        return;
      }

      let oldBrowser = this.selectedBrowser;
      // Once the async switcher starts, it's unpredictable when it will touch
      // the address bar, thus we store its state immediately.
      gURLBar?.saveSelectionStateForBrowser(oldBrowser);

      let newTab = this.getTabForBrowser(newBrowser);

      let timerId;
      if (!aForceUpdate) {
        timerId = Glean.browserTabswitch.update.start();

        if (gMultiProcessBrowser) {
          this._asyncTabSwitching = true;
          this._getSwitcher().requestTab(newTab);
          this._asyncTabSwitching = false;
        }

        document.commandDispatcher.lock();
      }

      let oldTab = this.selectedTab;

      // Preview mode should not reset the owner
      if (!this._previewMode && !oldTab.selected) {
        oldTab.owner = null;
      }

      let lastRelatedTab = this._lastRelatedTabMap.get(oldTab);
      if (lastRelatedTab) {
        if (!lastRelatedTab.selected) {
          lastRelatedTab.owner = null;
        }
      }
      this._lastRelatedTabMap = new WeakMap();

      if (!gMultiProcessBrowser) {
        oldBrowser.removeAttribute("primary");
        oldBrowser.docShellIsActive = false;
        newBrowser.setAttribute("primary", "true");
        newBrowser.docShellIsActive = !document.hidden;
      }

      this._selectedBrowser = newBrowser;
      this._selectedTab = newTab;
      this.showTab(newTab);

      this._appendStatusPanel();

      this._updateVisibleNotificationBox(newBrowser);

      let oldBrowserPopupsBlocked =
        oldBrowser.popupBlocker.getBlockedPopupCount();
      let newBrowserPopupsBlocked =
        newBrowser.popupBlocker.getBlockedPopupCount();
      if (oldBrowserPopupsBlocked != newBrowserPopupsBlocked) {
        newBrowser.popupBlocker.updateBlockedPopupsUI();
      }

      // Update the URL bar.
      let webProgress = newBrowser.webProgress;
      this._callProgressListeners(
        null,
        "onLocationChange",
        [webProgress, null, newBrowser.currentURI, 0, true],
        true,
        false
      );

      let securityUI = newBrowser.securityUI;
      if (securityUI) {
        this._callProgressListeners(
          null,
          "onSecurityChange",
          [webProgress, null, securityUI.state],
          true,
          false
        );
        // Include the true final argument to indicate that this event is
        // simulated (instead of being observed by the webProgressListener).
        this._callProgressListeners(
          null,
          "onContentBlockingEvent",
          [webProgress, null, newBrowser.getContentBlockingEvents(), true],
          true,
          false
        );
      }

      let listener = this._tabListeners.get(newTab);
      if (listener && listener.mStateFlags) {
        this._callProgressListeners(
          null,
          "onUpdateCurrentBrowser",
          [
            listener.mStateFlags,
            listener.mStatus,
            listener.mMessage,
            listener.mTotalProgress,
          ],
          true,
          false
        );
      }

      if (!this._previewMode) {
        newTab.recordTimeFromUnloadToReload();
        newTab.updateLastAccessed();
        oldTab.updateLastAccessed();
        // if this is the foreground window, update the last-seen timestamps.
        if (this.ownerGlobal == BrowserWindowTracker.getTopWindow()) {
          newTab.updateLastSeenActive();
          oldTab.updateLastSeenActive();
        }

        let oldFindBar = oldTab._findBar;
        if (
          oldFindBar &&
          oldFindBar.findMode == oldFindBar.FIND_NORMAL &&
          !oldFindBar.hidden
        ) {
          this._lastFindValue = oldFindBar._findField.value;
        }

        this.updateTitlebar();

        newTab.removeAttribute("titlechanged");
        newTab.attention = false;

        // The tab has been selected, it's not unselected anymore.
        // Call the current browser's unselectedTabHover() with false
        // to dispatch an event.
        newBrowser.unselectedTabHover(false);
      }

      // If the new tab is busy, and our current state is not busy, then
      // we need to fire a start to all progress listeners.
      if (newTab.hasAttribute("busy") && !this._isBusy) {
        this._isBusy = true;
        this._callProgressListeners(
          null,
          "onStateChange",
          [
            webProgress,
            null,
            Ci.nsIWebProgressListener.STATE_START |
              Ci.nsIWebProgressListener.STATE_IS_NETWORK,
            0,
          ],
          true,
          false
        );
      }

      // If the new tab is not busy, and our current state is busy, then
      // we need to fire a stop to all progress listeners.
      if (!newTab.hasAttribute("busy") && this._isBusy) {
        this._isBusy = false;
        this._callProgressListeners(
          null,
          "onStateChange",
          [
            webProgress,
            null,
            Ci.nsIWebProgressListener.STATE_STOP |
              Ci.nsIWebProgressListener.STATE_IS_NETWORK,
            0,
          ],
          true,
          false
        );
      }

      // TabSelect events are suppressed during preview mode to avoid confusing extensions and other bits of code
      // that might rely upon the other changes suppressed.
      // Focus is suppressed in the event that the main browser window is minimized - focusing a tab would restore the window
      if (!this._previewMode) {
        // We've selected the new tab, so go ahead and notify listeners.
        let event = new CustomEvent("TabSelect", {
          bubbles: true,
          cancelable: false,
          detail: {
            previousTab: oldTab,
          },
        });
        newTab.dispatchEvent(event);

        this._tabAttrModified(oldTab, ["selected"]);
        this._tabAttrModified(newTab, ["selected"]);

        this._startMultiSelectChange();
        this._multiSelectChangeSelected = true;
        this.clearMultiSelectedTabs();
        if (this._multiSelectChangeAdditions.size) {
          // Some tab has been multiselected just before switching tabs.
          // The tab that was selected at that point should also be multiselected.
          this.addToMultiSelectedTabs(oldTab);
        }

        if (!gMultiProcessBrowser) {
          this._adjustFocusBeforeTabSwitch(oldTab, newTab);
          this._adjustFocusAfterTabSwitch(newTab);
        }

        // Bug 1781806 - A forced update can indicate the tab was already
        // selected. To ensure the internal state of the Urlbar is kept in
        // sync, notify it as if focus changed. Alternatively, if there is no
        // force update but the load context is not using remote tabs, there
        // can be a focus change due to the _adjustFocus above.
        if (aForceUpdate || !gMultiProcessBrowser) {
          gURLBar.afterTabSwitchFocusChange();
        }
      }

      updateUserContextUIIndicator();
      gPermissionPanel.updateSharingIndicator();

      // Enable touch events to start a native dragging
      // session to allow the user to easily drag the selected tab.
      // This is currently only supported on Windows.
      oldTab.removeAttribute("touchdownstartsdrag");
      newTab.setAttribute("touchdownstartsdrag", "true");

      if (!gMultiProcessBrowser) {
        document.commandDispatcher.unlock();

        let event = new CustomEvent("TabSwitchDone", {
          bubbles: true,
          cancelable: true,
        });
        this.dispatchEvent(event);
      }

      if (!aForceUpdate) {
        Glean.browserTabswitch.update.stopAndAccumulate(timerId);
      }
    }

    _adjustFocusBeforeTabSwitch(oldTab, newTab) {
      if (this._previewMode) {
        return;
      }

      let oldBrowser = oldTab.linkedBrowser;
      let newBrowser = newTab.linkedBrowser;

      gURLBar.getBrowserState(oldBrowser).urlbarFocused = gURLBar.focused;

      if (this._asyncTabSwitching) {
        newBrowser._userTypedValueAtBeforeTabSwitch = newBrowser.userTypedValue;
      }

      if (this.isFindBarInitialized(oldTab)) {
        let findBar = this.getCachedFindBar(oldTab);
        oldTab._findBarFocused =
          !findBar.hidden &&
          findBar._findField.getAttribute("focused") == "true";
      }

      let activeEl = document.activeElement;
      // If focus is on the old tab, move it to the new tab.
      if (activeEl == oldTab) {
        newTab.focus();
      } else if (
        gMultiProcessBrowser &&
        activeEl != newBrowser &&
        activeEl != newTab
      ) {
        // In e10s, if focus isn't already in the tabstrip or on the new browser,
        // and the new browser's previous focus wasn't in the url bar but focus is
        // there now, we need to adjust focus further.
        let keepFocusOnUrlBar =
          newBrowser &&
          gURLBar.getBrowserState(newBrowser).urlbarFocused &&
          gURLBar.focused;
        if (!keepFocusOnUrlBar) {
          // Clear focus so that _adjustFocusAfterTabSwitch can detect if
          // some element has been focused and respect that.
          document.activeElement.blur();
        }
      }
    }

    _adjustFocusAfterTabSwitch(newTab) {
      // Don't steal focus from the tab bar.
      if (document.activeElement == newTab) {
        return;
      }

      let newBrowser = this.getBrowserForTab(newTab);

      if (newBrowser.hasAttribute("tabDialogShowing")) {
        newBrowser.tabDialogBox.focus();
        return;
      }
      // Focus the location bar if it was previously focused for that tab.
      // In full screen mode, only bother making the location bar visible
      // if the tab is a blank one.
      if (gURLBar.getBrowserState(newBrowser).urlbarFocused) {
        let selectURL = () => {
          if (this._asyncTabSwitching) {
            // Set _awaitingSetURI flag to suppress popup notification
            // explicitly while tab switching asynchronously.
            newBrowser._awaitingSetURI = true;

            // The onLocationChange event called in updateCurrentBrowser() will
            // be captured in browser.js, then it calls gURLBar.setURI(). In case
            // of that doing processing of here before doing above processing,
            // the selection status that gURLBar.select() does will be releasing
            // by gURLBar.setURI(). To resolve it, we call gURLBar.select() after
            // finishing gURLBar.setURI().
            const currentActiveElement = document.activeElement;
            gURLBar.inputField.addEventListener(
              "SetURI",
              () => {
                delete newBrowser._awaitingSetURI;

                // If the user happened to type into the URL bar for this browser
                // by the time we got here, focusing will cause the text to be
                // selected which could cause them to overwrite what they've
                // already typed in.
                let userTypedValueAtBeforeTabSwitch =
                  newBrowser._userTypedValueAtBeforeTabSwitch;
                delete newBrowser._userTypedValueAtBeforeTabSwitch;
                if (
                  newBrowser.userTypedValue &&
                  newBrowser.userTypedValue != userTypedValueAtBeforeTabSwitch
                ) {
                  return;
                }

                if (currentActiveElement != document.activeElement) {
                  return;
                }
                gURLBar.restoreSelectionStateForBrowser(newBrowser);
              },
              { once: true }
            );
          } else {
            gURLBar.restoreSelectionStateForBrowser(newBrowser);
          }
        };

        // This inDOMFullscreen attribute indicates that the page has something
        // such as a video in fullscreen mode. Opening a new tab will cancel
        // fullscreen mode, so we need to wait for that to happen and then
        // select the url field.
        if (window.document.documentElement.hasAttribute("inDOMFullscreen")) {
          window.addEventListener("MozDOMFullscreen:Exited", selectURL, {
            once: true,
            wantsUntrusted: false,
          });
          return;
        }

        if (!window.fullScreen || newTab.isEmpty) {
          selectURL();
          return;
        }
      }

      // Focus the find bar if it was previously focused for that tab.
      if (
        gFindBarInitialized &&
        !gFindBar.hidden &&
        this.selectedTab._findBarFocused
      ) {
        gFindBar._findField.focus();
        return;
      }

      // Don't focus the content area if something has been focused after the
      // tab switch was initiated.
      if (gMultiProcessBrowser && document.activeElement != document.body) {
        return;
      }

      // We're now committed to focusing the content area.
      let fm = Services.focus;
      let focusFlags = fm.FLAG_NOSCROLL;

      if (!gMultiProcessBrowser) {
        let newFocusedElement = fm.getFocusedElementForWindow(
          window.content,
          true,
          {}
        );

        // for anchors, use FLAG_SHOWRING so that it is clear what link was
        // last clicked when switching back to that tab
        if (
          newFocusedElement &&
          (HTMLAnchorElement.isInstance(newFocusedElement) ||
            newFocusedElement.getAttributeNS(
              "http://www.w3.org/1999/xlink",
              "type"
            ) == "simple")
        ) {
          focusFlags |= fm.FLAG_SHOWRING;
        }
      }

      fm.setFocus(newBrowser, focusFlags);
    }

    _tabAttrModified(aTab, aChanged) {
      if (aTab.closing) {
        return;
      }

      let event = new CustomEvent("TabAttrModified", {
        bubbles: true,
        cancelable: false,
        detail: {
          changed: aChanged,
        },
      });
      aTab.dispatchEvent(event);
    }

    resetBrowserSharing(aBrowser) {
      let tab = this.getTabForBrowser(aBrowser);
      if (!tab) {
        return;
      }
      // If WebRTC was used, leave object to enable tracking of grace periods.
      tab._sharingState = tab._sharingState?.webRTC ? { webRTC: {} } : {};
      tab.removeAttribute("sharing");
      this._tabAttrModified(tab, ["sharing"]);
      if (aBrowser == this.selectedBrowser) {
        gPermissionPanel.updateSharingIndicator();
      }
    }

    updateBrowserSharing(aBrowser, aState) {
      let tab = this.getTabForBrowser(aBrowser);
      if (!tab) {
        return;
      }
      if (tab._sharingState == null) {
        tab._sharingState = {};
      }
      tab._sharingState = Object.assign(tab._sharingState, aState);

      if ("webRTC" in aState) {
        if (tab._sharingState.webRTC?.sharing) {
          if (tab._sharingState.webRTC.paused) {
            tab.removeAttribute("sharing");
          } else {
            tab.setAttribute("sharing", aState.webRTC.sharing);
          }
        } else {
          tab.removeAttribute("sharing");
        }
        this._tabAttrModified(tab, ["sharing"]);
      }

      if (aBrowser == this.selectedBrowser) {
        gPermissionPanel.updateSharingIndicator();
      }
    }

    getTabSharingState(aTab) {
      // Normalize the state object for consumers (ie.extensions).
      let state = Object.assign(
        {},
        aTab._sharingState && aTab._sharingState.webRTC
      );
      return {
        camera: !!state.camera,
        microphone: !!state.microphone,
        screen: state.screen && state.screen.replace("Paused", ""),
      };
    }

    setInitialTabTitle(aTab, aTitle, aOptions = {}) {
      // Convert some non-content title (actually a url) to human readable title
      if (!aOptions.isContentTitle && isBlankPageURL(aTitle)) {
        aTitle = this.tabContainer.emptyTabTitle;
      }

      if (aTitle) {
        if (!aTab.getAttribute("label")) {
          aTab._labelIsInitialTitle = true;
        }

        this._setTabLabel(aTab, aTitle, aOptions);
      }
    }

    _dataURLRegEx = /^data:[^,]+;base64,/i;

    // Regex to test if a string (potential tab label) consists of only non-
    // printable characters. We consider Unicode categories Separator
    // (spaces & line-breaks) and Other (control chars, private use, non-
    // character codepoints) to be unprintable, along with a few specific
    // characters whose expected rendering is blank:
    //   U+2800 BRAILLE PATTERN BLANK (category So)
    //   U+115F HANGUL CHOSEONG FILLER (category Lo)
    //   U+1160 HANGUL JUNGSEONG FILLER (category Lo)
    //   U+3164 HANGUL FILLER (category Lo)
    //   U+FFA0 HALFWIDTH HANGUL FILLER (category Lo)
    // We also ignore combining marks, as in the absence of a printable base
    // character they are unlikely to be usefully rendered, and may well be
    // clipped away entirely.
    _nonPrintingRegEx =
      /^[\p{Z}\p{C}\p{M}\u{115f}\u{1160}\u{2800}\u{3164}\u{ffa0}]*$/u;

    setTabTitle(aTab) {
      var browser = this.getBrowserForTab(aTab);
      var title = browser.contentTitle;

      if (aTab.hasAttribute("customizemode")) {
        title = this.tabLocalization.formatValueSync(
          "tabbrowser-customizemode-tab-title"
        );
      }

      // Don't replace an initially set label with the URL while the tab
      // is loading.
      if (aTab._labelIsInitialTitle) {
        if (!title) {
          return false;
        }
        delete aTab._labelIsInitialTitle;
      }

      let isURL = false;

      // Trim leading and trailing whitespace from the title.
      title = title.trim();

      // If the title contains only non-printing characters (or only combining
      // marks, but no base character for them), we won't use it.
      if (this._nonPrintingRegEx.test(title)) {
        title = "";
      }

      let isContentTitle = !!title;
      if (!title) {
        // See if we can use the URI as the title.
        if (browser.currentURI.displaySpec) {
          try {
            title = Services.io.createExposableURI(
              browser.currentURI
            ).displaySpec;
          } catch (ex) {
            title = browser.currentURI.displaySpec;
          }
        }

        if (title && !isBlankPageURL(title)) {
          isURL = true;
          if (title.length <= 500 || !this._dataURLRegEx.test(title)) {
            // Try to unescape not-ASCII URIs using the current character set.
            try {
              let characterSet = browser.characterSet;
              title = Services.textToSubURI.unEscapeNonAsciiURI(
                characterSet,
                title
              );
            } catch (ex) {
              /* Do nothing. */
            }
          }
        } else {
          // No suitable URI? Fall back to our untitled string.
          title = this.tabContainer.emptyTabTitle;
        }
      }

      return this._setTabLabel(aTab, title, { isContentTitle, isURL });
    }

    // While an auth prompt from a base domain different than the current sites is open, we do not want to show the tab title of the current site,
    // but of the origin that is requesting authentication.
    // This is to prevent possible auth spoofing scenarios.
    // See bug 791594 for reference.
    setTabLabelForAuthPrompts(aTab, aLabel) {
      return this._setTabLabel(aTab, aLabel);
    }

    _setTabLabel(aTab, aLabel, { beforeTabOpen, isContentTitle, isURL } = {}) {
      if (!aLabel || aLabel.includes("about:reader?")) {
        return false;
      }

      // If it's a long data: URI that uses base64 encoding, truncate to a
      // reasonable length rather than trying to display the entire thing,
      // which can hang or crash the browser.
      // We can't shorten arbitrary URIs like this, as bidi etc might mean
      // we need the trailing characters for display. But a base64-encoded
      // data-URI is plain ASCII, so this is OK for tab-title display.
      // (See bug 1408854.)
      if (isURL && aLabel.length > 500 && this._dataURLRegEx.test(aLabel)) {
        aLabel = aLabel.substring(0, 500) + "\u2026";
      }

      aTab._fullLabel = aLabel;

      if (!isContentTitle) {
        // Remove protocol and "www."
        if (!("_regex_shortenURLForTabLabel" in this)) {
          this._regex_shortenURLForTabLabel = /^[^:]+:\/\/(?:www\.)?/;
        }
        aLabel = aLabel.replace(this._regex_shortenURLForTabLabel, "");
      }

      aTab._labelIsContentTitle = isContentTitle;

      if (aTab.getAttribute("label") == aLabel) {
        return false;
      }

      let dwu = window.windowUtils;
      let isRTL =
        dwu.getDirectionFromText(aLabel) == Ci.nsIDOMWindowUtils.DIRECTION_RTL;

      aTab.setAttribute("label", aLabel);
      aTab.setAttribute("labeldirection", isRTL ? "rtl" : "ltr");
      aTab.toggleAttribute("labelendaligned", isRTL != (document.dir == "rtl"));

      // Dispatch TabAttrModified event unless we're setting the label
      // before the TabOpen event was dispatched.
      if (!beforeTabOpen) {
        this._tabAttrModified(aTab, ["label"]);
      }

      if (aTab.selected) {
        this.updateTitlebar();
      }

      return true;
    }

    loadTabs(
      aURIs,
      {
        allowInheritPrincipal,
        allowThirdPartyFixup,
        inBackground,
        newIndex,
        elementIndex,
        postDatas,
        replace,
        tabGroup,
        targetTab,
        triggeringPrincipal,
        csp,
        userContextId,
        fromExternal,
      } = {}
    ) {
      if (!aURIs.length) {
        return;
      }

      // The tab selected after this new tab is closed (i.e. the new tab's
      // "owner") is the next adjacent tab (i.e. not the previously viewed tab)
      // when several urls are opened here (i.e. closing the first should select
      // the next of many URLs opened) or if the pref to have UI links opened in
      // the background is set (i.e. the link is not being opened modally)
      //
      // i.e.
      //    Number of URLs    Load UI Links in BG       Focus Last Viewed?
      //    == 1              false                     YES
      //    == 1              true                      NO
      //    > 1               false/true                NO
      var multiple = aURIs.length > 1;
      var owner = multiple || inBackground ? null : this.selectedTab;
      var firstTabAdded = null;
      var targetTabIndex = -1;

      if (typeof elementIndex == "number") {
        newIndex = this.#elementIndexToTabIndex(elementIndex);
      }
      if (typeof newIndex != "number") {
        newIndex = -1;
      }

      // When bulk opening tabs, such as from a bookmark folder, we want to insertAfterCurrent
      // if necessary, but we also will set the bulkOrderedOpen flag so that the bookmarks
      // open in the same order they are in the folder.
      if (
        multiple &&
        newIndex < 0 &&
        Services.prefs.getBoolPref("browser.tabs.insertAfterCurrent")
      ) {
        newIndex = this.selectedTab._tPos + 1;
      }

      if (replace) {
        if (this.isTabGroupLabel(targetTab)) {
          throw new Error(
            "Replacing a tab group label with a tab is not supported"
          );
        }
        let browser;
        if (targetTab) {
          browser = this.getBrowserForTab(targetTab);
          targetTabIndex = targetTab._tPos;
        } else {
          browser = this.selectedBrowser;
          targetTabIndex = this.tabContainer.selectedIndex;
        }
        let loadFlags = LOAD_FLAGS_NONE;
        if (allowThirdPartyFixup) {
          loadFlags |=
            LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP | LOAD_FLAGS_FIXUP_SCHEME_TYPOS;
        }
        if (!allowInheritPrincipal) {
          loadFlags |= LOAD_FLAGS_DISALLOW_INHERIT_PRINCIPAL;
        }
        if (fromExternal) {
          loadFlags |= LOAD_FLAGS_FROM_EXTERNAL;
        }
        try {
          browser.fixupAndLoadURIString(aURIs[0], {
            loadFlags,
            postData: postDatas && postDatas[0],
            triggeringPrincipal,
            csp,
          });
        } catch (e) {
          // Ignore failure in case a URI is wrong, so we can continue
          // opening the next ones.
        }
      } else {
        let params = {
          allowInheritPrincipal,
          ownerTab: owner,
          skipAnimation: multiple,
          allowThirdPartyFixup,
          postData: postDatas && postDatas[0],
          userContextId,
          triggeringPrincipal,
          bulkOrderedOpen: multiple,
          csp,
          fromExternal,
          tabGroup,
        };
        if (newIndex > -1) {
          params.index = newIndex;
        }
        firstTabAdded = this.addTab(aURIs[0], params);
        if (newIndex > -1) {
          targetTabIndex = firstTabAdded._tPos;
        }
      }

      let tabNum = targetTabIndex;
      for (let i = 1; i < aURIs.length; ++i) {
        let params = {
          allowInheritPrincipal,
          skipAnimation: true,
          allowThirdPartyFixup,
          postData: postDatas && postDatas[i],
          userContextId,
          triggeringPrincipal,
          bulkOrderedOpen: true,
          csp,
          fromExternal,
          tabGroup,
        };
        if (targetTabIndex > -1) {
          params.index = ++tabNum;
        }
        this.addTab(aURIs[i], params);
      }

      if (firstTabAdded && !inBackground) {
        this.selectedTab = firstTabAdded;
      }
    }

    updateBrowserRemoteness(aBrowser, { newFrameloader, remoteType } = {}) {
      let isRemote = aBrowser.getAttribute("remote") == "true";

      // We have to be careful with this here, as the "no remote type" is null,
      // not a string. Make sure to check only for undefined, since null is
      // allowed.
      if (remoteType === undefined) {
        throw new Error("Remote type must be set!");
      }

      let shouldBeRemote = remoteType !== E10SUtils.NOT_REMOTE;

      if (!gMultiProcessBrowser && shouldBeRemote) {
        throw new Error(
          "Cannot switch to remote browser in a window " +
            "without the remote tabs load context."
        );
      }

      // Abort if we're not going to change anything
      let oldRemoteType = aBrowser.remoteType;
      if (
        isRemote == shouldBeRemote &&
        !newFrameloader &&
        (!isRemote || oldRemoteType == remoteType)
      ) {
        return false;
      }

      let tab = this.getTabForBrowser(aBrowser);
      // aBrowser needs to be inserted now if it hasn't been already.
      this._insertBrowser(tab);

      let evt = document.createEvent("Events");
      evt.initEvent("BeforeTabRemotenessChange", true, false);
      tab.dispatchEvent(evt);

      // Unhook our progress listener.
      let filter = this._tabFilters.get(tab);
      let listener = this._tabListeners.get(tab);
      aBrowser.webProgress.removeProgressListener(filter);
      filter.removeProgressListener(listener);

      // We'll be creating a new listener, so destroy the old one.
      listener.destroy();

      let oldDroppedLinkHandler = aBrowser.droppedLinkHandler;
      let oldUserTypedValue = aBrowser.userTypedValue;
      let hadStartedLoad = aBrowser.didStartLoadSinceLastUserTyping();

      // Change the "remote" attribute.

      // Make sure the browser is destroyed so it unregisters from observer notifications
      aBrowser.destroy();

      if (shouldBeRemote) {
        aBrowser.setAttribute("remote", "true");
        aBrowser.setAttribute("remoteType", remoteType);
      } else {
        aBrowser.setAttribute("remote", "false");
        aBrowser.removeAttribute("remoteType");
      }

      // This call actually switches out our frameloaders. Do this as late as
      // possible before rebuilding the browser, as we'll need the new browser
      // state set up completely first.
      aBrowser.changeRemoteness({
        remoteType,
      });

      // Once we have new frameloaders, this call sets the browser back up.
      aBrowser.construct();

      aBrowser.userTypedValue = oldUserTypedValue;
      if (hadStartedLoad) {
        aBrowser.urlbarChangeTracker.startedLoad();
      }

      aBrowser.droppedLinkHandler = oldDroppedLinkHandler;

      // This shouldn't really be necessary, however, this has the side effect
      // of sending MozLayerTreeReady / MozLayerTreeCleared events for remote
      // frames, which the tab switcher depends on.
      //
      // eslint-disable-next-line no-self-assign
      aBrowser.docShellIsActive = aBrowser.docShellIsActive;

      // Create a new tab progress listener for the new browser we just injected,
      // since tab progress listeners have logic for handling the initial about:blank
      // load
      listener = new TabProgressListener(tab, aBrowser, true, false);
      this._tabListeners.set(tab, listener);
      filter.addProgressListener(listener, Ci.nsIWebProgress.NOTIFY_ALL);

      // Restore the progress listener.
      aBrowser.webProgress.addProgressListener(
        filter,
        Ci.nsIWebProgress.NOTIFY_ALL
      );

      // Restore the securityUI state.
      let securityUI = aBrowser.securityUI;
      let state = securityUI
        ? securityUI.state
        : Ci.nsIWebProgressListener.STATE_IS_INSECURE;
      this._callProgressListeners(
        aBrowser,
        "onSecurityChange",
        [aBrowser.webProgress, null, state],
        true,
        false
      );
      let event = aBrowser.getContentBlockingEvents();
      // Include the true final argument to indicate that this event is
      // simulated (instead of being observed by the webProgressListener).
      this._callProgressListeners(
        aBrowser,
        "onContentBlockingEvent",
        [aBrowser.webProgress, null, event, true],
        true,
        false
      );

      if (shouldBeRemote) {
        // Switching the browser to be remote will connect to a new child
        // process so the browser can no longer be considered to be
        // crashed.
        tab.removeAttribute("crashed");
      }

      // If the findbar has been initialised, reset its browser reference.
      if (this.isFindBarInitialized(tab)) {
        this.getCachedFindBar(tab).browser = aBrowser;
      }

      evt = document.createEvent("Events");
      evt.initEvent("TabRemotenessChange", true, false);
      tab.dispatchEvent(evt);

      return true;
    }

    updateBrowserRemotenessByURL(aBrowser, aURL, aOptions = {}) {
      if (!gMultiProcessBrowser) {
        return this.updateBrowserRemoteness(aBrowser, {
          remoteType: E10SUtils.NOT_REMOTE,
        });
      }

      let oldRemoteType = aBrowser.remoteType;

      let oa = E10SUtils.predictOriginAttributes({ browser: aBrowser });

      aOptions.remoteType = E10SUtils.getRemoteTypeForURI(
        aURL,
        gMultiProcessBrowser,
        gFissionBrowser,
        oldRemoteType,
        aBrowser.currentURI,
        oa
      );

      // If this URL can't load in the current browser then flip it to the
      // correct type.
      if (oldRemoteType != aOptions.remoteType || aOptions.newFrameloader) {
        return this.updateBrowserRemoteness(aBrowser, aOptions);
      }

      return false;
    }

    createBrowser({
      isPreloadBrowser,
      name,
      openWindowInfo,
      remoteType,
      initialBrowsingContextGroupId,
      uriIsAboutBlank,
      userContextId,
      skipLoad,
    } = {}) {
      let b = document.createXULElement("browser");
      // Use the JSM global to create the permanentKey, so that if the
      // permanentKey is held by something after this window closes, it
      // doesn't keep the window alive.
      b.permanentKey = new (Cu.getGlobalForObject(Services).Object)();

      const defaultBrowserAttributes = {
        contextmenu: "contentAreaContextMenu",
        message: "true",
        messagemanagergroup: "browsers",
        tooltip: "aHTMLTooltip",
        type: "content",
        manualactiveness: "true",
      };
      for (let attribute in defaultBrowserAttributes) {
        b.setAttribute(attribute, defaultBrowserAttributes[attribute]);
      }

      if (gMultiProcessBrowser || remoteType) {
        b.setAttribute("maychangeremoteness", "true");
      }

      if (userContextId) {
        b.setAttribute("usercontextid", userContextId);
      }

      if (remoteType) {
        b.setAttribute("remoteType", remoteType);
        b.setAttribute("remote", "true");
      }

      if (!isPreloadBrowser) {
        b.setAttribute("autocompletepopup", "PopupAutoComplete");
      }

      /*
       * This attribute is meant to describe if the browser is the
       * preloaded browser. When the preloaded browser is created, the
       * 'preloadedState' attribute for that browser is set to "preloaded", and
       * when a new tab is opened, and it is time to show that preloaded
       * browser, the 'preloadedState' attribute for that browser is removed.
       *
       * See more details on Bug 1420285.
       */
      if (isPreloadBrowser) {
        b.setAttribute("preloadedState", "preloaded");
      }

      // Ensure that the browser will be created in a specific initial
      // BrowsingContextGroup. This may change the process selection behaviour
      // of the newly created browser, and is often used in combination with
      // "remoteType" to ensure that the initial about:blank load occurs
      // within the same process as another window.
      if (initialBrowsingContextGroupId) {
        b.setAttribute(
          "initialBrowsingContextGroupId",
          initialBrowsingContextGroupId
        );
      }

      // Propagate information about the opening content window to the browser.
      if (openWindowInfo) {
        b.openWindowInfo = openWindowInfo;
      }

      // This will be used by gecko to control the name of the opened
      // window.
      if (name) {
        // XXX: The `name` property is special in HTML and XUL. Should
        // we use a different attribute name for this?
        b.setAttribute("name", name);
      }

      if (this._allowTransparentBrowser) {
        b.setAttribute("transparent", "true");
      }

      let stack = document.createXULElement("stack");
      stack.className = "browserStack";
      stack.appendChild(b);

      let browserContainer = document.createXULElement("vbox");
      browserContainer.className = "browserContainer";
      browserContainer.appendChild(stack);

      let browserSidebarContainer = document.createXULElement("hbox");
      browserSidebarContainer.className = "browserSidebarContainer";
      browserSidebarContainer.appendChild(browserContainer);

      // Prevent the superfluous initial load of a blank document
      // if we're going to load something other than about:blank.
      if (!uriIsAboutBlank || skipLoad) {
        b.setAttribute("nodefaultsrc", "true");
      }

      return b;
    }

    _createLazyBrowser(aTab) {
      let browser = aTab.linkedBrowser;

      let names = this._browserBindingProperties;

      for (let i = 0; i < names.length; i++) {
        let name = names[i];
        let getter;
        let setter;
        switch (name) {
          case "audioMuted":
            getter = () => aTab.hasAttribute("muted");
            break;
          case "contentTitle":
            getter = () => SessionStore.getLazyTabValue(aTab, "title");
            break;
          case "currentURI":
            getter = () => {
              // Avoid recreating the same nsIURI object over and over again...
              if (browser._cachedCurrentURI) {
                return browser._cachedCurrentURI;
              }
              let url =
                SessionStore.getLazyTabValue(aTab, "url") || "about:blank";
              return (browser._cachedCurrentURI = Services.io.newURI(url));
            };
            break;
          case "didStartLoadSinceLastUserTyping":
            getter = () => () => false;
            break;
          case "fullZoom":
          case "textZoom":
            getter = () => 1;
            break;
          case "tabHasCustomZoom":
            getter = () => false;
            break;
          case "getTabBrowser":
            getter = () => () => this;
            break;
          case "isRemoteBrowser":
            getter = () => browser.getAttribute("remote") == "true";
            break;
          case "permitUnload":
            getter = () => () => ({ permitUnload: true });
            break;
          case "reload":
          case "reloadWithFlags":
            getter = () => params => {
              // Wait for load handler to be instantiated before
              // initializing the reload.
              aTab.addEventListener(
                "SSTabRestoring",
                () => {
                  browser[name](params);
                },
                { once: true }
              );
              gBrowser._insertBrowser(aTab);
            };
            break;
          case "remoteType":
            getter = () => {
              let url =
                SessionStore.getLazyTabValue(aTab, "url") || "about:blank";
              // Avoid recreating the same nsIURI object over and over again...
              let uri;
              if (browser._cachedCurrentURI) {
                uri = browser._cachedCurrentURI;
              } else {
                uri = browser._cachedCurrentURI = Services.io.newURI(url);
              }
              let oa = E10SUtils.predictOriginAttributes({
                browser,
                userContextId: aTab.getAttribute("usercontextid"),
              });
              return E10SUtils.getRemoteTypeForURI(
                url,
                gMultiProcessBrowser,
                gFissionBrowser,
                undefined,
                uri,
                oa
              );
            };
            break;
          case "userTypedValue":
          case "userTypedClear":
            getter = () => SessionStore.getLazyTabValue(aTab, name);
            break;
          default:
            getter = () => {
              if (AppConstants.NIGHTLY_BUILD) {
                let message = `[bug 1345098] Lazy browser prematurely inserted via '${name}' property access:\n`;
                Services.console.logStringMessage(message + new Error().stack);
              }
              this._insertBrowser(aTab);
              return browser[name];
            };
            setter = value => {
              if (AppConstants.NIGHTLY_BUILD) {
                let message = `[bug 1345098] Lazy browser prematurely inserted via '${name}' property access:\n`;
                Services.console.logStringMessage(message + new Error().stack);
              }
              this._insertBrowser(aTab);
              return (browser[name] = value);
            };
        }
        Object.defineProperty(browser, name, {
          get: getter,
          set: setter,
          configurable: true,
          enumerable: true,
        });
      }
    }

    _insertBrowser(aTab, aInsertedOnTabCreation) {
      "use strict";

      // If browser is already inserted or window is closed don't do anything.
      if (aTab.linkedPanel || window.closed) {
        return;
      }

      let browser = aTab.linkedBrowser;

      // If browser is a lazy browser, delete the substitute properties.
      if (this._browserBindingProperties[0] in browser) {
        for (let name of this._browserBindingProperties) {
          delete browser[name];
        }
      }

      let { uriIsAboutBlank, usingPreloadedContent } = aTab._browserParams;
      delete aTab._browserParams;
      delete browser._cachedCurrentURI;

      let panel = this.getPanel(browser);
      let uniqueId = this._generateUniquePanelID();
      panel.id = uniqueId;
      aTab.linkedPanel = uniqueId;

      // Inject the <browser> into the DOM if necessary.
      if (!panel.parentNode) {
        // NB: this appendChild call causes us to run constructors for the
        // browser element, which fires off a bunch of notifications. Some
        // of those notifications can cause code to run that inspects our
        // state, so it is important that the tab element is fully
        // initialized by this point.
        this.tabpanels.appendChild(panel);
      }

      // wire up a progress listener for the new browser object.
      let tabListener = new TabProgressListener(
        aTab,
        browser,
        uriIsAboutBlank,
        usingPreloadedContent
      );
      const filter = Cc[
        "@mozilla.org/appshell/component/browser-status-filter;1"
      ].createInstance(Ci.nsIWebProgress);
      filter.addProgressListener(tabListener, Ci.nsIWebProgress.NOTIFY_ALL);
      browser.webProgress.addProgressListener(
        filter,
        Ci.nsIWebProgress.NOTIFY_ALL
      );
      this._tabListeners.set(aTab, tabListener);
      this._tabFilters.set(aTab, filter);

      browser.droppedLinkHandler = handleDroppedLink;
      browser.loadURI = URILoadingWrapper.loadURI.bind(
        URILoadingWrapper,
        browser
      );
      browser.fixupAndLoadURIString =
        URILoadingWrapper.fixupAndLoadURIString.bind(
          URILoadingWrapper,
          browser
        );

      // Most of the time, we start our browser's docShells out as inactive,
      // and then maintain activeness in the tab switcher. Preloaded about:newtab's
      // are already created with their docShell's as inactive, but then explicitly
      // render their layers to ensure that we can switch to them quickly. We avoid
      // setting docShellIsActive to false again in this case, since that'd cause
      // the layers for the preloaded tab to be dropped, and we'd see a flash
      // of empty content instead.
      //
      // So for all browsers except for the preloaded case, we set the browser
      // docShell to inactive.
      if (!usingPreloadedContent) {
        browser.docShellIsActive = false;
      }

      // If we transitioned from one browser to two browsers, we need to set
      // hasSiblings=false on both the existing browser and the new browser.
      if (this.tabs.length == 2) {
        this.tabs[0].linkedBrowser.browsingContext.hasSiblings = true;
        this.tabs[1].linkedBrowser.browsingContext.hasSiblings = true;
      } else {
        aTab.linkedBrowser.browsingContext.hasSiblings = this.tabs.length > 1;
      }

      if (aTab.userContextId) {
        browser.setAttribute("usercontextid", aTab.userContextId);
      }

      browser.browsingContext.isAppTab = aTab.pinned;

      // We don't want to update the container icon and identifier if
      // this is not the selected browser.
      if (aTab.selected) {
        updateUserContextUIIndicator();
      }

      // Only fire this event if the tab is already in the DOM
      // and will be handled by a listener.
      if (aTab.isConnected) {
        var evt = new CustomEvent("TabBrowserInserted", {
          bubbles: true,
          detail: { insertedOnTabCreation: aInsertedOnTabCreation },
        });
        aTab.dispatchEvent(evt);
      }
    }

    _mayDiscardBrowser(aTab, aForceDiscard) {
      let browser = aTab.linkedBrowser;
      let action = aForceDiscard ? "unload" : "dontUnload";

      if (
        !aTab ||
        aTab.selected ||
        aTab.closing ||
        this._windowIsClosing ||
        !browser.isConnected ||
        !browser.isRemoteBrowser ||
        !browser.permitUnload(action).permitUnload
      ) {
        return false;
      }

      return true;
    }

    discardBrowser(aTab, aForceDiscard) {
      "use strict";
      let browser = aTab.linkedBrowser;

      if (!this._mayDiscardBrowser(aTab, aForceDiscard)) {
        return false;
      }

      // Reset sharing state.
      if (aTab._sharingState) {
        this.resetBrowserSharing(browser);
      }
      webrtcUI.forgetStreamsFromBrowserContext(browser.browsingContext);

      // Set browser parameters for when browser is restored.  Also remove
      // listeners and set up lazy restore data in SessionStore. This must
      // be done before browser is destroyed and removed from the document.
      aTab._browserParams = {
        uriIsAboutBlank: browser.currentURI.spec == "about:blank",
        remoteType: browser.remoteType,
        usingPreloadedContent: false,
      };

      SessionStore.resetBrowserToLazyState(aTab);
      // Indicate that this tab was explicitly unloaded (i.e. not
      // from a session restore) in case we want to style that
      // differently.
      if (aForceDiscard) {
        aTab.toggleAttribute("discarded", true);
      }

      // Remove the tab's filter and progress listener.
      let filter = this._tabFilters.get(aTab);
      let listener = this._tabListeners.get(aTab);
      browser.webProgress.removeProgressListener(filter);
      filter.removeProgressListener(listener);
      listener.destroy();

      this._tabListeners.delete(aTab);
      this._tabFilters.delete(aTab);

      // Reset the findbar and remove it if it is attached to the tab.
      if (aTab._findBar) {
        aTab._findBar.close(true);
        aTab._findBar.remove();
        delete aTab._findBar;
      }

      // Remove potentially stale attributes.
      let attributesToRemove = [
        "activemedia-blocked",
        "busy",
        "pendingicon",
        "progress",
        "soundplaying",
      ];
      let removedAttributes = [];
      for (let attr of attributesToRemove) {
        if (aTab.hasAttribute(attr)) {
          removedAttributes.push(attr);
          aTab.removeAttribute(attr);
        }
      }
      if (removedAttributes.length) {
        this._tabAttrModified(aTab, removedAttributes);
      }

      browser.destroy();
      this.getPanel(browser).remove();
      aTab.removeAttribute("linkedpanel");

      this._createLazyBrowser(aTab);

      let evt = new CustomEvent("TabBrowserDiscarded", { bubbles: true });
      aTab.dispatchEvent(evt);
      return true;
    }

    /**
     * Loads a tab with a default null principal unless specified
     */
    addWebTab(aURI, params = {}) {
      if (!params.triggeringPrincipal) {
        params.triggeringPrincipal =
          Services.scriptSecurityManager.createNullPrincipal({
            userContextId: params.userContextId,
          });
      }
      if (params.triggeringPrincipal.isSystemPrincipal) {
        throw new Error(
          "System principal should never be passed into addWebTab()"
        );
      }
      return this.addTab(aURI, params);
    }

    addAdjacentNewTab(tab) {
      Services.obs.notifyObservers(
        {
          wrappedJSObject: new Promise(resolve => {
            this.selectedTab = this.addTrustedTab(BROWSER_NEW_TAB_URL, {
              index: tab._tPos + 1,
              userContextId: tab.userContextId,
              tabGroup: tab.group,
              focusUrlBar: true,
            });
            resolve(this.selectedBrowser);
          }),
        },
        "browser-open-newtab-start"
      );
    }

    /**
     * Must only be used sparingly for content that came from Chrome context
     * If in doubt use addWebTab
     * @param {string} aURI
     * @param {object} [options]
     * @see this.addTab options
     * @returns {MozTabbrowserTab|null}
     */
    addTrustedTab(aURI, options = {}) {
      options.triggeringPrincipal =
        Services.scriptSecurityManager.getSystemPrincipal();
      return this.addTab(aURI, options);
    }

    /**
     * @param {string} uriString
     * @param {object} options
     * @param {MozTabbrowserTabGroup} [options.tabGroup]
     *   A related tab group where this tab should be added, when applicable.
     *   When present, the tab is expected to reside in this tab group. When
     *   absent, the tab is expected to be a standalone tab.
     * @returns {MozTabbrowserTab|null}
     *    The new tab. The return value will be null if the tab couldn't be
     *    created; this shouldn't normally happen, and an error will be logged
     *    to the console if it does.
     */
    addTab(
      uriString,
      {
        allowInheritPrincipal,
        allowThirdPartyFixup,
        bulkOrderedOpen,
        charset,
        createLazyBrowser,
        disableTRR,
        eventDetail,
        focusUrlBar,
        forceNotRemote,
        forceAllowDataURI,
        fromExternal,
        inBackground = true,
        index,
        elementIndex,
        lazyTabTitle,
        name,
        noInitialLabel,
        openWindowInfo,
        openerBrowser,
        originPrincipal,
        originStoragePrincipal,
        ownerTab,
        pinned,
        postData,
        preferredRemoteType,
        referrerInfo,
        relatedToCurrent,
        initialBrowsingContextGroupId,
        skipAnimation,
        skipBackgroundNotify,
        tabGroup,
        triggeringPrincipal,
        userContextId,
        csp,
        skipLoad = createLazyBrowser,
        insertTab = true,
        globalHistoryOptions,
        triggeringRemoteType,
        schemelessInput,
        hasValidUserGestureActivation = false,
        textDirectiveUserActivation = false,
      } = {}
    ) {
      // all callers of addTab that pass a params object need to pass
      // a valid triggeringPrincipal.
      if (!triggeringPrincipal) {
        throw new Error(
          "Required argument triggeringPrincipal missing within addTab"
        );
      }

      if (!UserInteraction.running("browser.tabs.opening", window)) {
        UserInteraction.start("browser.tabs.opening", "initting", window);
      }

      // If we're opening a foreground tab, set the owner by default.
      ownerTab ??= inBackground ? null : this.selectedTab;

      // if we're adding tabs, we're past interrupt mode, ditch the owner
      if (this.selectedTab.owner) {
        this.selectedTab.owner = null;
      }

      // Find the tab that opened this one, if any. This is used for
      // determining positioning, and inherited attributes such as the
      // user context ID.
      //
      // If we have a browser opener (which is usually the browser
      // element from a remote window.open() call), use that.
      //
      // Otherwise, if the tab is related to the current tab (e.g.,
      // because it was opened by a link click), use the selected tab as
      // the owner. If referrerInfo is set, and we don't have an
      // explicit relatedToCurrent arg, we assume that the tab is
      // related to the current tab, since referrerURI is null or
      // undefined if the tab is opened from an external application or
      // bookmark (i.e. somewhere other than an existing tab).
      if (relatedToCurrent == null) {
        relatedToCurrent = !!(referrerInfo && referrerInfo.originalReferrer);
      }
      let openerTab =
        (openerBrowser && this.getTabForBrowser(openerBrowser)) ||
        (relatedToCurrent && this.selectedTab) ||
        null;

      // When overflowing, new tabs are scrolled into view smoothly, which
      // doesn't go well together with the width transition. So we skip the
      // transition in that case.
      let animate =
        !skipAnimation &&
        !pinned &&
        !this.tabContainer.verticalMode &&
        !this.tabContainer.overflowing &&
        !gReduceMotion;

      let uriInfo = this._determineURIToLoad(uriString, createLazyBrowser);
      let { uri, uriIsAboutBlank, lazyBrowserURI } = uriInfo;
      // Have to overwrite this if we're lazy-loading. Should go away
      // with bug 1818777.
      ({ uriString } = uriInfo);

      let usingPreloadedContent = false;
      let b, t;

      try {
        t = this._createTab({
          uriString,
          animate,
          userContextId,
          openerTab,
          pinned,
          noInitialLabel,
          skipBackgroundNotify,
        });
        if (insertTab) {
          if (typeof index == "number") {
            elementIndex = this.#tabIndexToElementIndex(index);
          }
          // insert the tab into the tab container in the correct position
          this.#insertTabAtElementIndex(t, elementIndex, {
            ownerTab,
            openerTab,
            pinned,
            bulkOrderedOpen,
            tabGroup: tabGroup ?? openerTab?.group,
          });
        }

        ({ browser: b, usingPreloadedContent } = this._createBrowserForTab(t, {
          uriString,
          uri,
          preferredRemoteType,
          openerBrowser,
          uriIsAboutBlank,
          referrerInfo,
          forceNotRemote,
          name,
          initialBrowsingContextGroupId,
          openWindowInfo,
          skipLoad,
          triggeringRemoteType,
        }));

        if (focusUrlBar) {
          gURLBar.getBrowserState(b).urlbarFocused = true;
        }

        // If the caller opts in, create a lazy browser.
        if (createLazyBrowser) {
          this._createLazyBrowser(t);

          if (lazyBrowserURI) {
            // Lazy browser must be explicitly registered so tab will appear as
            // a switch-to-tab candidate in autocomplete.
            this.UrlbarProviderOpenTabs.registerOpenTab(
              lazyBrowserURI.spec,
              t.userContextId,
              PrivateBrowsingUtils.isWindowPrivate(window)
            );
            b.registeredOpenURI = lazyBrowserURI;
          }
          // If we're not inserting the tab into the DOM, we can't set the tab
          // state meaningfully. Session restore (the only caller who does this)
          // will have to do this work itself later, when the tabs have been
          // inserted.
          if (insertTab) {
            SessionStore.setTabState(t, {
              entries: [
                {
                  url: lazyBrowserURI?.spec || "about:blank",
                  title: lazyTabTitle,
                  triggeringPrincipal_base64:
                    E10SUtils.serializePrincipal(triggeringPrincipal),
                },
              ],
              // Make sure to store the userContextId associated to the lazy tab
              // otherwise it would be created as a default tab when recreated on a
              // session restore (See Bug 1819794).
              userContextId,
            });
          }
        } else {
          this._insertBrowser(t, true);
          // If we were called by frontend and don't have openWindowInfo,
          // but we were opened from another browser, set the cross group
          // opener ID:
          if (openerBrowser && !openWindowInfo) {
            b.browsingContext.crossGroupOpener = openerBrowser.browsingContext;
          }
        }
      } catch (e) {
        console.error("Failed to create tab");
        console.error(e);
        t?.remove();
        if (t?.linkedBrowser) {
          this._tabFilters.delete(t);
          this._tabListeners.delete(t);
          this.getPanel(t.linkedBrowser).remove();
        }
        return null;
      }

      if (insertTab) {
        // Fire a TabOpen event
        this._fireTabOpen(t, eventDetail);

        this._kickOffBrowserLoad(b, {
          uri,
          uriString,
          usingPreloadedContent,
          triggeringPrincipal,
          originPrincipal,
          originStoragePrincipal,
          uriIsAboutBlank,
          allowInheritPrincipal,
          allowThirdPartyFixup,
          fromExternal,
          disableTRR,
          forceAllowDataURI,
          skipLoad,
          referrerInfo,
          charset,
          postData,
          csp,
          globalHistoryOptions,
          triggeringRemoteType,
          schemelessInput,
          hasValidUserGestureActivation:
            hasValidUserGestureActivation ||
            !!openWindowInfo?.hasValidUserGestureActivation,
          textDirectiveUserActivation:
            textDirectiveUserActivation ||
            !!openWindowInfo?.textDirectiveUserActivation,
        });
      }

      // This field is updated regardless if we actually animate
      // since it's important that we keep this count correct in all cases.
      this.tabAnimationsInProgress++;

      if (animate) {
        // Kick the animation off.
        // TODO: we should figure out a better solution here. We use RAF
        // to avoid jank of the animation due to synchronous work happening
        // on tab open.
        // With preloaded content though a single RAF happens too early. and
        // both the transition and the transitionend event don't happen.
        if (usingPreloadedContent) {
          requestAnimationFrame(() => {
            requestAnimationFrame(() => {
              t.setAttribute("fadein", "true");
            });
          });
        } else {
          requestAnimationFrame(() => {
            t.setAttribute("fadein", "true");
          });
        }
      }

      // Additionally send pinned tab events
      if (pinned) {
        this._notifyPinnedStatus(t);
      }

      gSharedTabWarning.tabAdded(t);

      if (!inBackground) {
        this.selectedTab = t;
      }
      return t;
    }

    #elementIndexToTabIndex(elementIndex) {
      if (elementIndex < 0) {
        return -1;
      }
      if (elementIndex >= this.tabContainer.ariaFocusableItems.length) {
        return this.tabs.length;
      }
      let element = this.tabContainer.ariaFocusableItems[elementIndex];
      if (this.isTabGroupLabel(element)) {
        element = element.group.tabs[0];
      }
      return element._tPos;
    }

    #tabIndexToElementIndex(tabIndex) {
      if (tabIndex < 0) {
        return -1;
      }
      let tab;
      while ((tab = this.tabs.at(tabIndex)) && !tab.visible) {
        tabIndex++;
      }
      return tab?.visible
        ? tab.elementIndex
        : this.tabContainer.ariaFocusableItems.length;
    }

    /**
     * @param {string} id
     * @param {string} color
     * @param {boolean} collapsed
     * @param {string} [label=]
     * @returns {MozTabbrowserTabGroup}
     */
    _createTabGroup(id, color, collapsed, label = "") {
      let group = document.createXULElement("tab-group", { is: "tab-group" });
      group.id = id;
      group.collapsed = collapsed;
      group.color = color;
      group.label = label;
      return group;
    }

    /**
     * Adds a new tab group.
     *
     * @param {object[]} tabs
     *   The set of tabs to include in the group.
     * @param {object} [options]
     * @param {string} [options.id]
     *   Optionally assign an ID to the tab group. Useful when rebuilding an
     *   existing group e.g. when restoring. A pseudorandom string will be
     *   generated if not set.
     * @param {string} [options.color]
     *   Color for the group label. See tabgroup-menu.js for possible values.
     *   If no color specified, will attempt to assign an unused group color.
     * @param {string} [options.label]
     *   Label for the group.
     * @param {MozTabbrowserTab} [options.insertBefore]
     *   An optional argument that accepts a single tab, which, if passed, will
     *   cause the group to be inserted just before this tab in the tab strip. By
     *   default, the group will be created at the end of the tab strip.
     * @param {boolean} [options.isAdoptingGroup]
     *   Whether the tab group was created because a tab group with the same
     *   properties is being adopted from a different window.
     * @param {boolean} [options.isUserTriggered]
     *   Should be true if this group is being created in response to an
     *   explicit request from the user (as opposed to a group being created
     *   for technical reasons, such as when an already existing group
     *   switches windows).
     *   Causes the group create UI to be displayed and telemetry events to be fired.
     * @param {string} [options.telemetryUserCreateSource]
     *   The means by which the tab group was created.
     *   @see TabMetrics.METRIC_SOURCE for possible values.
     *   Defaults to "unknown".
     */
    addTabGroup(
      tabs,
      {
        id = null,
        color = null,
        label = "",
        insertBefore = null,
        isAdoptingGroup = false,
        isUserTriggered = false,
        telemetryUserCreateSource = "unknown",
      } = {}
    ) {
      if (!tabs?.length) {
        throw new Error("Cannot create tab group with zero tabs");
      }

      if (!color) {
        color = this.tabGroupMenu.nextUnusedColor;
      }

      if (!id) {
        // Note: If this changes, make sure to also update the
        // getExtTabGroupIdForInternalTabGroupId implementation in
        // browser/components/extensions/parent/ext-browser.js.
        // See: Bug 1960104 - Improve tab group ID generation in addTabGroup
        id = `${Date.now()}-${Math.round(Math.random() * 100)}`;
      }
      let group = this._createTabGroup(id, color, false, label);
      this.tabContainer.insertBefore(
        group,
        insertBefore?.group ?? insertBefore
      );
      group.addTabs(tabs);

      // Bail out if the group is empty at this point. This can happen if all
      // provided tabs are pinned and therefore cannot be grouped.
      if (!group.tabs.length) {
        group.remove();
        return null;
      }

      group.dispatchEvent(
        new CustomEvent("TabGroupCreate", {
          bubbles: true,
          detail: {
            isAdoptingGroup,
            isUserTriggered,
            telemetryUserCreateSource,
          },
        })
      );

      // Fixes bug1953801 and bug1954689
      // Ensure that the tab state cache is updated immediately after creating
      // a group. This is necessary because we consider group creation a
      // deliberate user action indicating the tab has importance for the user.
      // Without this, it is not possible to save and close a tab group with
      // a short lifetime.
      group.tabs.forEach(tab => {
        this.TabStateFlusher.flush(tab.linkedBrowser);
      });

      return group;
    }

    /**
     * Removes the tab group. This has the effect of closing all the tabs
     * in the group.
     *
     * @param {MozTabbrowserTabGroup} [group]
     *   The tab group to remove.
     * @param {object} [options]
     *   Options to use when removing tabs. @see removeTabs for more info.
     * @param {boolean} [options.isUserTriggered=false]
     *   Should be true if this group is being removed by an explicit
     *   request from the user (as opposed to a group being removed
     *   for technical reasons, such as when an already existing group
     *   switches windows). This causes telemetry events to fire.
     * @param {string} [options.telemetrySource="unknown"]
     *   The means by which the tab group was removed.
     *   @see TabMetrics.METRIC_SOURCE for possible values.
     *   Defaults to "unknown".
     */
    async removeTabGroup(
      group,
      options = {
        isUserTriggered: false,
        telemetrySource: this.TabMetrics.METRIC_SOURCE.UNKNOWN,
      }
    ) {
      if (this.tabGroupMenu.panel.state != "closed") {
        this.tabGroupMenu.panel.hidePopup(options.animate);
      }

      if (!options.skipPermitUnload) {
        // Process permit unload handlers and allow user cancel
        let cancel = await this.runBeforeUnloadForTabs(group.tabs);
        if (cancel) {
          if (SessionStore.getSavedTabGroup(group.id)) {
            // If this group is currently saved, it's being removed as part of a
            // save & close operation. We need to forget the saved group
            // if the close is canceled.
            SessionStore.forgetSavedTabGroup(group.id);
          }
          return;
        }
        options.skipPermitUnload = true;
      }

      if (group.tabs.length == this.tabs.length) {
        // explicit calls to removeTabGroup are not expected to save groups.
        // if removing this group closes a window, we need to tell the window
        // not to save the group.
        group.saveOnWindowClose = false;
      }

      // This needs to be fired before tabs are removed because session store
      // needs to respond to this while tabs are still part of the group
      group.dispatchEvent(
        new CustomEvent("TabGroupRemoveRequested", {
          bubbles: true,
          detail: {
            skipSessionStore: options.skipSessionStore,
            isUserTriggered: options.isUserTriggered,
            telemetrySource: options.telemetrySource,
          },
        })
      );

      // Skip session store on a per-tab basis since these tabs will get
      // recorded as part of a group
      options.skipSessionStore = true;

      // tell removeTabs not to subprocess groups since we're removing a group.
      options.skipGroupCheck = true;

      this.removeTabs(group.tabs, options);
    }

    /**
     * Removes a tab from a group. This has no effect on tabs that are not
     * already in a group.
     *
     * @param tab The tab to ungroup
     */
    ungroupTab(tab) {
      if (!tab.group) {
        return;
      }

      this.#handleTabMove(tab, () =>
        gBrowser.tabContainer.insertBefore(tab, tab.group.nextElementSibling)
      );
    }

    /**
     * @param {MozTabbrowserTabGroup} group
     * @param {number} elementIndex
     * @returns {MozTabbrowserTabGroup}
     */
    adoptTabGroup(group, elementIndex) {
      if (group.ownerDocument == document) {
        return group;
      }
      group.removedByAdoption = true;
      group.saveOnWindowClose = false;

      let newTabs = [];
      for (let tab of group.tabs) {
        newTabs.push(this.adoptTab(tab, { elementIndex }));
        ++elementIndex;
      }

      return this.addTabGroup(newTabs, {
        id: group.id,
        label: group.label,
        color: group.color,
        insertBefore: newTabs[0],
        isAdoptingGroup: true,
      });
    }

    getAllTabGroups() {
      return BrowserWindowTracker.getOrderedWindows({
        private: PrivateBrowsingUtils.isWindowPrivate(window),
      }).reduce(
        (acc, thisWindow) => acc.concat(thisWindow.gBrowser.tabGroups),
        []
      );
    }

    getTabGroupById(id) {
      for (const win of BrowserWindowTracker.getOrderedWindows({
        private: PrivateBrowsingUtils.isWindowPrivate(window),
      })) {
        for (const group of win.gBrowser.tabGroups) {
          if (group.id === id) {
            return group;
          }
        }
      }
      return null;
    }

    _determineURIToLoad(uriString, createLazyBrowser) {
      uriString = uriString || "about:blank";
      let aURIObject = null;
      try {
        aURIObject = Services.io.newURI(uriString);
      } catch (ex) {
        /* we'll try to fix up this URL later */
      }

      let lazyBrowserURI;
      if (createLazyBrowser && uriString != "about:blank") {
        lazyBrowserURI = aURIObject;
        uriString = "about:blank";
      }

      let uriIsAboutBlank = uriString == "about:blank";
      return { uri: aURIObject, uriIsAboutBlank, lazyBrowserURI, uriString };
    }

    /**
     * @param {object} options
     * @returns {MozTabbrowserTab}
     */
    _createTab({
      uriString,
      userContextId,
      openerTab,
      pinned,
      noInitialLabel,
      skipBackgroundNotify,
      animate,
    }) {
      var t = document.createXULElement("tab", { is: "tabbrowser-tab" });
      // Tag the tab as being created so extension code can ignore events
      // prior to TabOpen.
      t.initializingTab = true;
      t.openerTab = openerTab;

      // Related tab inherits current tab's user context unless a different
      // usercontextid is specified
      if (userContextId == null && openerTab) {
        userContextId = openerTab.getAttribute("usercontextid") || 0;
      }

      if (!noInitialLabel) {
        if (isBlankPageURL(uriString)) {
          t.setAttribute("label", this.tabContainer.emptyTabTitle);
        } else {
          // Set URL as label so that the tab isn't empty initially.
          this.setInitialTabTitle(t, uriString, {
            beforeTabOpen: true,
            isURL: true,
          });
        }
      }

      if (userContextId) {
        t.setAttribute("usercontextid", userContextId);
        ContextualIdentityService.setTabStyle(t);
      }

      if (skipBackgroundNotify) {
        t.setAttribute("skipbackgroundnotify", true);
      }

      if (pinned) {
        t.setAttribute("pinned", "true");
      }

      t.classList.add("tabbrowser-tab");

      this.tabContainer._unlockTabSizing();

      if (!animate) {
        UserInteraction.update("browser.tabs.opening", "not-animated", window);
        t.setAttribute("fadein", "true");

        // Call _handleNewTab asynchronously as it needs to know if the
        // new tab is selected.
        setTimeout(
          function (tabContainer) {
            tabContainer._handleNewTab(t);
          },
          0,
          this.tabContainer
        );
      } else {
        UserInteraction.update("browser.tabs.opening", "animated", window);
      }

      return t;
    }

    _createBrowserForTab(
      tab,
      {
        uriString,
        uri,
        name,
        preferredRemoteType,
        openerBrowser,
        uriIsAboutBlank,
        referrerInfo,
        forceNotRemote,
        initialBrowsingContextGroupId,
        openWindowInfo,
        skipLoad,
        triggeringRemoteType,
      }
    ) {
      // If we don't have a preferred remote type (or it is `NOT_REMOTE`), and
      // we have a remote triggering remote type, use that instead.
      if (!preferredRemoteType && triggeringRemoteType) {
        preferredRemoteType = triggeringRemoteType;
      }

      // If we don't have a preferred remote type, and we have a remote
      // opener, use the opener's remote type.
      if (!preferredRemoteType && openerBrowser) {
        preferredRemoteType = openerBrowser.remoteType;
      }

      let { userContextId } = tab;

      var oa = E10SUtils.predictOriginAttributes({ window, userContextId });

      // If URI is about:blank and we don't have a preferred remote type,
      // then we need to use the referrer, if we have one, to get the
      // correct remote type for the new tab.
      if (
        uriIsAboutBlank &&
        !preferredRemoteType &&
        referrerInfo &&
        referrerInfo.originalReferrer
      ) {
        preferredRemoteType = E10SUtils.getRemoteTypeForURI(
          referrerInfo.originalReferrer.spec,
          gMultiProcessBrowser,
          gFissionBrowser,
          E10SUtils.DEFAULT_REMOTE_TYPE,
          null,
          oa
        );
      }

      let remoteType = forceNotRemote
        ? E10SUtils.NOT_REMOTE
        : E10SUtils.getRemoteTypeForURI(
            uriString,
            gMultiProcessBrowser,
            gFissionBrowser,
            preferredRemoteType,
            null,
            oa
          );

      let b,
        usingPreloadedContent = false;
      // If we open a new tab with the newtab URL in the default
      // userContext, check if there is a preloaded browser ready.
      if (uriString == BROWSER_NEW_TAB_URL && !userContextId) {
        b = NewTabPagePreloading.getPreloadedBrowser(window);
        if (b) {
          usingPreloadedContent = true;
        }
      }

      if (!b) {
        // No preloaded browser found, create one.
        b = this.createBrowser({
          remoteType,
          uriIsAboutBlank,
          userContextId,
          initialBrowsingContextGroupId,
          openWindowInfo,
          name,
          skipLoad,
        });
      }

      tab.linkedBrowser = b;

      this._tabForBrowser.set(b, tab);
      tab.permanentKey = b.permanentKey;
      tab._browserParams = {
        uriIsAboutBlank,
        remoteType,
        usingPreloadedContent,
      };

      // Hack to ensure that the about:newtab, and about:welcome favicon is loaded
      // instantaneously, to avoid flickering and improve perceived performance.
      this.setDefaultIcon(tab, uri);

      return { browser: b, usingPreloadedContent };
    }

    _kickOffBrowserLoad(
      browser,
      {
        uri,
        uriString,
        usingPreloadedContent,
        triggeringPrincipal,
        originPrincipal,
        originStoragePrincipal,
        uriIsAboutBlank,
        allowInheritPrincipal,
        allowThirdPartyFixup,
        fromExternal,
        disableTRR,
        forceAllowDataURI,
        skipLoad,
        referrerInfo,
        charset,
        postData,
        csp,
        globalHistoryOptions,
        triggeringRemoteType,
        schemelessInput,
        hasValidUserGestureActivation,
        textDirectiveUserActivation,
      }
    ) {
      if (
        !usingPreloadedContent &&
        originPrincipal &&
        originStoragePrincipal &&
        uriString
      ) {
        let { URI_INHERITS_SECURITY_CONTEXT } = Ci.nsIProtocolHandler;
        // Unless we know for sure we're not inheriting principals,
        // force the about:blank viewer to have the right principal:
        if (!uri || doGetProtocolFlags(uri) & URI_INHERITS_SECURITY_CONTEXT) {
          browser.createAboutBlankDocumentViewer(
            originPrincipal,
            originStoragePrincipal
          );
        }
      }

      // If we didn't swap docShells with a preloaded browser
      // then let's just continue loading the page normally.
      if (
        !usingPreloadedContent &&
        (!uriIsAboutBlank || !allowInheritPrincipal) &&
        !skipLoad
      ) {
        // pretend the user typed this so it'll be available till
        // the document successfully loads
        if (uriString && !gInitialPages.includes(uriString)) {
          browser.userTypedValue = uriString;
        }

        let loadFlags = LOAD_FLAGS_NONE;
        if (allowThirdPartyFixup) {
          loadFlags |=
            LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP | LOAD_FLAGS_FIXUP_SCHEME_TYPOS;
        }
        if (fromExternal) {
          loadFlags |= LOAD_FLAGS_FROM_EXTERNAL;
        } else if (!triggeringPrincipal.isSystemPrincipal) {
          // XXX this code must be reviewed and changed when bug 1616353
          // lands.
          loadFlags |= LOAD_FLAGS_FIRST_LOAD;
        }
        if (!allowInheritPrincipal) {
          loadFlags |= LOAD_FLAGS_DISALLOW_INHERIT_PRINCIPAL;
        }
        if (disableTRR) {
          loadFlags |= LOAD_FLAGS_DISABLE_TRR;
        }
        if (forceAllowDataURI) {
          loadFlags |= LOAD_FLAGS_FORCE_ALLOW_DATA_URI;
        }
        try {
          browser.fixupAndLoadURIString(uriString, {
            loadFlags,
            triggeringPrincipal,
            referrerInfo,
            charset,
            postData,
            csp,
            globalHistoryOptions,
            triggeringRemoteType,
            schemelessInput,
            hasValidUserGestureActivation,
            textDirectiveUserActivation,
          });
        } catch (ex) {
          console.error(ex);
        }
      }
    }

    /**
     * @typedef {object} TabGroupWorkingData
     * @property {TabGroupStateData} stateData
     * @property {MozTabbrowserTabGroup|undefined} node
     * @property {DocumentFragment} containingTabsFragment
     */

    /**
     * @param {boolean} restoreTabsLazily
     * @param {number} selectTab see SessionStoreInternal.restoreTabs { aSelectTab }
     * @param {TabStateData[]} tabDataList
     * @param {TabGroupStateData[]} tabGroupDataList
     * @returns {MozTabbrowserTab[]}
     */
    createTabsForSessionRestore(
      restoreTabsLazily,
      selectTab,
      tabDataList,
      tabGroupDataList
    ) {
      let tabs = [];
      let tabsFragment = document.createDocumentFragment();
      let tabToSelect = null;
      let hiddenTabs = new Map();
      let shouldUpdateForPinnedTabs = false;
      /** @type {Map<TabGroupStateData['id'], TabGroupWorkingData>} */
      let tabGroupWorkingData = new Map();

      for (const tabGroupData of tabGroupDataList) {
        tabGroupWorkingData.set(tabGroupData.id, {
          stateData: tabGroupData,
          node: undefined,
          containingTabsFragment: document.createDocumentFragment(),
        });
      }

      // We create each tab and browser, but only insert them
      // into a document fragment so that we can insert them all
      // together. This prevents synch reflow for each tab
      // insertion.
      for (var i = 0; i < tabDataList.length; i++) {
        let tabData = tabDataList[i];

        let userContextId = tabData.userContextId;
        let select = i == selectTab - 1;
        let tab;
        let tabWasReused = false;

        // Re-use existing selected tab if possible to avoid the overhead of
        // selecting a new tab. For now, we only do this for horizontal tabs;
        // we'll let tabs.js handle pinning for vertical tabs until we unify
        // the logic for both horizontal and vertical tabs in bug 1910097.
        if (
          select &&
          this.selectedTab.userContextId == userContextId &&
          !SessionStore.isTabRestoring(this.selectedTab) &&
          !this.tabContainer.verticalMode
        ) {
          tabWasReused = true;
          tab = this.selectedTab;
          if (!tabData.pinned) {
            this.unpinTab(tab);
          } else {
            this.pinTab(tab);
          }
        }

        // Add a new tab if needed.
        if (!tab) {
          let createLazyBrowser =
            restoreTabsLazily && !select && !tabData.pinned;

          let url = "about:blank";
          if (tabData.entries?.length) {
            let activeIndex = (tabData.index || tabData.entries.length) - 1;
            // Ensure the index is in bounds.
            activeIndex = Math.min(activeIndex, tabData.entries.length - 1);
            activeIndex = Math.max(activeIndex, 0);
            url = tabData.entries[activeIndex].url;
          }

          let preferredRemoteType = E10SUtils.getRemoteTypeForURI(
            url,
            gMultiProcessBrowser,
            gFissionBrowser,
            E10SUtils.DEFAULT_REMOTE_TYPE,
            null,
            E10SUtils.predictOriginAttributes({ window, userContextId })
          );

          // If we're creating a lazy browser, let tabbrowser know the future
          // URI because progress listeners won't get onLocationChange
          // notification before the browser is inserted.
          //
          // Setting noInitialLabel is a perf optimization. Rendering tab labels
          // would make resizing the tabs more expensive as we're adding them.
          // Each tab will get its initial label set in restoreTab.
          tab = this.addTrustedTab(createLazyBrowser ? url : "about:blank", {
            createLazyBrowser,
            skipAnimation: true,
            noInitialLabel: true,
            userContextId,
            skipBackgroundNotify: true,
            bulkOrderedOpen: true,
            insertTab: false,
            skipLoad: true,
            preferredRemoteType,
          });

          if (select) {
            tabToSelect = tab;
          }
        }

        tabs.push(tab);

        if (tabData.pinned) {
          // Calling `pinTab` calls `moveTabTo`, which assumes the tab is
          // inserted in the DOM. If the tab is not yet in the DOM,
          // just insert it in the right place from the start.
          if (!tab.parentNode) {
            tab._tPos = this.pinnedTabCount;
            this.tabContainer.insertBefore(tab, this.tabs[this.pinnedTabCount]);
            tab.toggleAttribute("pinned", true);
            this.tabContainer._invalidateCachedTabs();
            // Then ensure all the tab open/pinning information is sent.
            this._fireTabOpen(tab, {});
            this._notifyPinnedStatus(tab);
            // Once we're done adding all tabs, _updateTabBarForPinnedTabs
            // needs calling:
            shouldUpdateForPinnedTabs = true;
          }
        } else if (tabData.groupId) {
          let { groupId } = tabData;
          const tabGroup = tabGroupWorkingData.get(groupId);
          // if a tab refers to a tab group we don't know, skip any group
          // processing
          if (tabGroup) {
            tabGroup.containingTabsFragment.appendChild(tab);
            // if this is the first time encountering a tab group, create its
            // DOM node once and place it in the tabs bar fragment
            if (!tabGroup.node) {
              tabGroup.node = this._createTabGroup(
                tabGroup.stateData.id,
                tabGroup.stateData.color,
                tabGroup.stateData.collapsed,
                tabGroup.stateData.name
              );
              tabsFragment.appendChild(tabGroup.node);
            }
          }
        } else {
          if (tab.hidden) {
            tab.hidden = true;
            hiddenTabs.set(tab, tabData.extData && tabData.extData.hiddenBy);
          }

          tabsFragment.appendChild(tab);
          if (tabWasReused) {
            this.tabContainer._invalidateCachedTabs();
          }
        }

        tab.initialize();
      }

      // inject the top-level tab and tab group DOM nodes
      this.tabContainer.appendChild(tabsFragment);

      // inject tab DOM nodes into the now-connected tab group DOM nodes
      for (const tabGroup of tabGroupWorkingData.values()) {
        if (tabGroup.node) {
          tabGroup.node.appendChild(tabGroup.containingTabsFragment);
        }
      }

      for (let [tab, hiddenBy] of hiddenTabs) {
        let event = document.createEvent("Events");
        event.initEvent("TabHide", true, false);
        tab.dispatchEvent(event);
        if (hiddenBy) {
          SessionStore.setCustomTabValue(tab, "hiddenBy", hiddenBy);
        }
      }

      this.tabContainer._invalidateCachedTabs();
      if (shouldUpdateForPinnedTabs) {
        this._updateTabBarForPinnedTabs();
      }

      // We need to wait until after all tabs have been appended to the DOM
      // to remove the old selected tab.
      if (tabToSelect) {
        let leftoverTab = this.selectedTab;
        this.selectedTab = tabToSelect;
        this.removeTab(leftoverTab);
      }

      if (tabs.length > 1 || !tabs[0].selected) {
        this._updateTabsAfterInsert();
        TabBarVisibility.update();

        for (let tab of tabs) {
          // If tabToSelect is a tab, we didn't reuse the selected tab.
          if (tabToSelect || !tab.selected) {
            // Fire a TabOpen event for all unpinned tabs, except reused selected
            // tabs.
            if (!tab.pinned) {
              this._fireTabOpen(tab, {});
            }

            // Fire a TabBrowserInserted event on all tabs that have a connected,
            // real browser, except for reused selected tabs.
            if (tab.linkedPanel) {
              var evt = new CustomEvent("TabBrowserInserted", {
                bubbles: true,
                detail: { insertedOnTabCreation: true },
              });
              tab.dispatchEvent(evt);
            }
          }
        }
      }

      return tabs;
    }

    moveTabsToStart(contextTab) {
      let tabs = contextTab.multiselected ? this.selectedTabs : [contextTab];
      // Walk the array in reverse order so the tabs are kept in order.
      for (let i = tabs.length - 1; i >= 0; i--) {
        this.moveTabToStart(tabs[i]);
      }
    }

    moveTabsToEnd(contextTab) {
      let tabs = contextTab.multiselected ? this.selectedTabs : [contextTab];
      for (let tab of tabs) {
        this.moveTabToEnd(tab);
      }
    }

    warnAboutClosingTabs(tabsToClose, aCloseTabs) {
      // We want to warn about closing duplicates even if there was only a
      // single duplicate, so we intentionally place this above the check for
      // tabsToClose <= 1.
      const shownDupeDialogPref =
        "browser.tabs.haveShownCloseAllDuplicateTabsWarning";
      var ps = Services.prompt;
      if (
        aCloseTabs == this.closingTabsEnum.ALL_DUPLICATES &&
        !Services.prefs.getBoolPref(shownDupeDialogPref, false)
      ) {
        // The first time a user closes all duplicate tabs, tell them what will
        // happen and give them a chance to back away.
        Services.prefs.setBoolPref(shownDupeDialogPref, true);

        window.focus();
        const [title, text, button] = this.tabLocalization.formatValuesSync([
          { id: "tabbrowser-confirm-close-all-duplicate-tabs-title" },
          { id: "tabbrowser-confirm-close-all-duplicate-tabs-text" },
          {
            id: "tabbrowser-confirm-close-all-duplicate-tabs-button-closetabs",
          },
        ]);

        const flags =
          ps.BUTTON_POS_0 * ps.BUTTON_TITLE_IS_STRING +
          ps.BUTTON_POS_1 * ps.BUTTON_TITLE_CANCEL +
          ps.BUTTON_POS_0_DEFAULT;

        // buttonPressed will be 0 for close tabs, 1 for cancel.
        const buttonPressed = ps.confirmEx(
          window,
          title,
          text,
          flags,
          button,
          null,
          null,
          null,
          {}
        );
        return buttonPressed == 0;
      }

      if (tabsToClose <= 1) {
        return true;
      }

      const pref =
        aCloseTabs == this.closingTabsEnum.ALL
          ? "browser.tabs.warnOnClose"
          : "browser.tabs.warnOnCloseOtherTabs";
      var shouldPrompt = Services.prefs.getBoolPref(pref);
      if (!shouldPrompt) {
        return true;
      }

      const maxTabsUndo = Services.prefs.getIntPref(
        "browser.sessionstore.max_tabs_undo"
      );
      if (
        aCloseTabs != this.closingTabsEnum.ALL &&
        tabsToClose <= maxTabsUndo
      ) {
        return true;
      }

      // Our prompt to close this window is most important, so replace others.
      gDialogBox.replaceDialogIfOpen();

      // default to true: if it were false, we wouldn't get this far
      var warnOnClose = { value: true };

      // focus the window before prompting.
      // this will raise any minimized window, which will
      // make it obvious which window the prompt is for and will
      // solve the problem of windows "obscuring" the prompt.
      // see bug #350299 for more details
      window.focus();
      const [title, button, checkbox] = this.tabLocalization.formatValuesSync([
        {
          id: "tabbrowser-confirm-close-tabs-title",
          args: { tabCount: tabsToClose },
        },
        { id: "tabbrowser-confirm-close-tabs-button" },
        { id: "tabbrowser-ask-close-tabs-checkbox" },
      ]);
      let flags =
        ps.BUTTON_TITLE_IS_STRING * ps.BUTTON_POS_0 +
        ps.BUTTON_TITLE_CANCEL * ps.BUTTON_POS_1;
      let checkboxLabel =
        aCloseTabs == this.closingTabsEnum.ALL ? checkbox : null;
      var buttonPressed = ps.confirmEx(
        window,
        title,
        null,
        flags,
        button,
        null,
        null,
        checkboxLabel,
        warnOnClose
      );

      var reallyClose = buttonPressed == 0;

      // don't set the pref unless they press OK and it's false
      if (
        aCloseTabs == this.closingTabsEnum.ALL &&
        reallyClose &&
        !warnOnClose.value
      ) {
        Services.prefs.setBoolPref(pref, false);
      }

      return reallyClose;
    }

    /**
     * This determines where the tab should be inserted within the tabContainer,
     * and inserts it.
     *
     * @param {MozTabbrowserTab} tab
     * @param {number} index
     * @param {object} [options]
     * @param {MozTabbrowserTabGroup} [options.tabGroup]
     *   A related tab group where this tab should be added, when applicable.
     */
    #insertTabAtElementIndex(
      tab,
      index,
      { ownerTab, openerTab, pinned, bulkOrderedOpen, tabGroup } = {}
    ) {
      // If this new tab is owned by another, assert that relationship
      if (ownerTab) {
        tab.owner = ownerTab;
      }

      // Ensure we have an index if one was not provided.
      if (typeof index != "number") {
        // Move the new tab after another tab if needed, to the end otherwise.
        index = Infinity;
        if (
          !bulkOrderedOpen &&
          ((openerTab &&
            Services.prefs.getBoolPref(
              "browser.tabs.insertRelatedAfterCurrent"
            )) ||
            Services.prefs.getBoolPref("browser.tabs.insertAfterCurrent"))
        ) {
          let lastRelatedTab =
            openerTab && this._lastRelatedTabMap.get(openerTab);
          let previousTab = lastRelatedTab || openerTab || this.selectedTab;
          if (!tabGroup) {
            tabGroup = previousTab.group;
          }
          if (
            Services.prefs.getBoolPref(
              "browser.tabs.insertAfterCurrentExceptPinned"
            ) &&
            previousTab.pinned
          ) {
            index = Infinity;
          } else if (previousTab.visible) {
            index = previousTab.elementIndex + 1;
          } else if (previousTab == FirefoxViewHandler.tab) {
            index = 0;
          }

          if (lastRelatedTab) {
            lastRelatedTab.owner = null;
          } else if (openerTab) {
            tab.owner = openerTab;
          }
          // Always set related map if opener exists.
          if (openerTab) {
            this._lastRelatedTabMap.set(openerTab, tab);
          }
        }
      }

      // Prevent a flash of unstyled content by setting up the tab content
      // and inherited attributes before appending it (see Bug 1592054):
      tab.initialize();

      // Ensure index is within bounds.
      if (tab.pinned) {
        index = Math.max(index, 0);
        index = Math.min(index, this.pinnedTabCount);
      } else {
        index = Math.max(index, this.pinnedTabCount);
        index = Math.min(index, this.tabContainer.ariaFocusableItems.length);
      }

      /** @type {MozTabbrowserTab|undefined} */
      let itemAfter = this.tabContainer.ariaFocusableItems.at(index);
      this.tabContainer._invalidateCachedTabs();

      if (tabGroup) {
        if (this.isTab(itemAfter) && itemAfter.group == tabGroup) {
          // Place at the front of, or between tabs in, the same tab group
          this.tabContainer.insertBefore(tab, itemAfter);
        } else {
          // Place tab at the end of the contextual tab group because one of:
          // 1) no `itemAfter` so `tab` should be the last tab in the tab strip
          // 2) `itemAfter` is in a different tab group
          tabGroup.appendChild(tab);
        }
      } else if (
        (this.isTab(itemAfter) && itemAfter.group?.tabs[0] == itemAfter) ||
        this.isTabGroupLabel(itemAfter)
      ) {
        // If there is ambiguity around whether or not a tab should be inserted
        // into a group (i.e. because the new tab is being inserted on the
        // edges of the group), prefer not to insert the tab into the group.
        //
        // We only need to handle the case where the tab is being inserted at
        // the starting boundary of a group because `insertBefore` called on
        // the tab just after a tab group will not add it to the group by
        // default.
        this.tabContainer.insertBefore(tab, itemAfter.group);
      } else {
        // Place ungrouped tab before `itemAfter` by default
        this.tabContainer.insertBefore(tab, itemAfter);
      }

      this._updateTabsAfterInsert();

      if (pinned) {
        this._updateTabBarForPinnedTabs();
      }

      TabBarVisibility.update();
    }

    /**
     * Dispatch a new tab event. This should be called when things are in a
     * consistent state, such that listeners of this event can again open
     * or close tabs.
     */
    _fireTabOpen(tab, eventDetail) {
      delete tab.initializingTab;
      let evt = new CustomEvent("TabOpen", {
        bubbles: true,
        detail: eventDetail || {},
      });
      tab.dispatchEvent(evt);
    }

    /**
     * @param {MozTabbrowserTab} aTab
     * @returns {MozTabbrowserTab[]}
     */
    _getTabsToTheStartFrom(aTab) {
      let tabsToStart = [];
      if (!aTab.visible) {
        return tabsToStart;
      }
      let tabs = this.openTabs;
      for (let i = 0; i < tabs.length; ++i) {
        if (tabs[i] == aTab) {
          break;
        }
        // Ignore pinned and hidden tabs.
        if (tabs[i].pinned || tabs[i].hidden) {
          continue;
        }
        // In a multi-select context, select all unselected tabs
        // starting from the context tab.
        if (aTab.multiselected && tabs[i].multiselected) {
          continue;
        }
        tabsToStart.push(tabs[i]);
      }
      return tabsToStart;
    }

    /**
     * @param {MozTabbrowserTab} aTab
     * @returns {MozTabbrowserTab[]}
     */
    _getTabsToTheEndFrom(aTab) {
      let tabsToEnd = [];
      if (!aTab.visible) {
        return tabsToEnd;
      }
      let tabs = this.openTabs;
      for (let i = tabs.length - 1; i >= 0; --i) {
        if (tabs[i] == aTab) {
          break;
        }
        // Ignore pinned and hidden tabs.
        if (tabs[i].pinned || tabs[i].hidden) {
          continue;
        }
        // In a multi-select context, select all unselected tabs
        // starting from the context tab.
        if (aTab.multiselected && tabs[i].multiselected) {
          continue;
        }
        tabsToEnd.push(tabs[i]);
      }
      return tabsToEnd;
    }

    getDuplicateTabsToClose(aTab) {
      // One would think that a set is better, but it would need to copy all
      // the strings instead of just keeping references to the nsIURI objects,
      // and the array is presumed to be small anyways.
      let keys = [];
      let keyForTab = tab => {
        let uri = tab.linkedBrowser?.currentURI;
        if (!uri) {
          return null;
        }
        return {
          uri,
          userContextId: tab.userContextId,
        };
      };
      let keyEquals = (a, b) => {
        return a.userContextId == b.userContextId && a.uri.equals(b.uri);
      };
      if (aTab.multiselected) {
        for (let tab of this.selectedTabs) {
          let key = keyForTab(tab);
          if (key) {
            keys.push(key);
          }
        }
      } else {
        let key = keyForTab(aTab);
        if (key) {
          keys.push(key);
        }
      }

      if (!keys.length) {
        return [];
      }

      let duplicateTabs = [];
      for (let tab of this.tabs) {
        if (tab == aTab || tab.pinned) {
          continue;
        }
        if (aTab.multiselected && tab.multiselected) {
          continue;
        }
        let key = keyForTab(tab);
        if (key && keys.some(k => keyEquals(k, key))) {
          duplicateTabs.push(tab);
        }
      }

      return duplicateTabs;
    }

    getAllDuplicateTabsToClose() {
      let lastSeenTabs = this.tabs.toSorted(
        (a, b) => b.lastSeenActive - a.lastSeenActive
      );
      let duplicateTabs = [];
      let keys = [];
      for (let tab of lastSeenTabs) {
        const uri = tab.linkedBrowser?.currentURI;
        if (!uri) {
          // Can't tell if it's a duplicate without a URI.
          // Safest to leave it be.
          continue;
        }

        const key = {
          uri,
          userContextId: tab.userContextId,
        };
        if (
          !tab.pinned &&
          keys.some(
            k => k.userContextId == key.userContextId && k.uri.equals(key.uri)
          )
        ) {
          duplicateTabs.push(tab);
        }
        keys.push(key);
      }
      return duplicateTabs;
    }

    removeDuplicateTabs(aTab) {
      this._removeDuplicateTabs(
        aTab,
        this.getDuplicateTabsToClose(aTab),
        this.closingTabsEnum.DUPLICATES
      );
    }

    _removeDuplicateTabs(aConfirmationAnchor, tabs, aCloseTabs) {
      if (!tabs.length) {
        return;
      }

      if (!this.warnAboutClosingTabs(tabs.length, aCloseTabs)) {
        return;
      }

      this.removeTabs(tabs);
      ConfirmationHint.show(
        aConfirmationAnchor,
        "confirmation-hint-duplicate-tabs-closed",
        { l10nArgs: { tabCount: tabs.length } }
      );
    }

    removeAllDuplicateTabs() {
      // I would like to have the caller provide this target,
      // but the caller lives in a different document.
      let alltabsButton = document.getElementById("alltabs-button");
      this._removeDuplicateTabs(
        alltabsButton,
        this.getAllDuplicateTabsToClose(),
        this.closingTabsEnum.ALL_DUPLICATES
      );
    }

    /**
     * In a multi-select context, the tabs (except pinned tabs) that are located to the
     * left of the leftmost selected tab will be removed.
     */
    removeTabsToTheStartFrom(aTab) {
      let tabs = this._getTabsToTheStartFrom(aTab);
      if (
        !this.warnAboutClosingTabs(tabs.length, this.closingTabsEnum.TO_START)
      ) {
        return;
      }

      this.removeTabs(tabs);
    }

    /**
     * In a multi-select context, the tabs (except pinned tabs) that are located to the
     * right of the rightmost selected tab will be removed.
     */
    removeTabsToTheEndFrom(aTab) {
      let tabs = this._getTabsToTheEndFrom(aTab);
      if (
        !this.warnAboutClosingTabs(tabs.length, this.closingTabsEnum.TO_END)
      ) {
        return;
      }

      this.removeTabs(tabs);
    }

    /**
     * Remove all tabs but `aTab`. By default, in a multi-select context, all
     * unpinned and unselected tabs are removed. Otherwise all unpinned tabs
     * except aTab are removed. This behavior can be changed using the the bool
     * flags below.
     *
     * @param {MozTabbrowserTab} aTab
     *   The tab we will skip removing
     * @param {object} [aParams]
     *   An optional set of parameters that will be passed to the
     *   `removeTabs` function.
     * @param {boolean} [aParams.skipWarnAboutClosingTabs=false]
     *   Skip showing the tab close warning prompt.
     * @param {boolean} [aParams.skipPinnedOrSelectedTabs=true]
     *   Skip closing tabs that are selected or pinned.
     */
    removeAllTabsBut(aTab, aParams = {}) {
      let {
        skipWarnAboutClosingTabs = false,
        skipPinnedOrSelectedTabs = true,
      } = aParams;

      /** @type {function(MozTabbrowserTab):boolean} */
      let filterFn;

      // If enabled also filter by selected or pinned state.
      if (skipPinnedOrSelectedTabs) {
        if (aTab?.multiselected) {
          filterFn = tab => !tab.multiselected && !tab.pinned && !tab.hidden;
        } else {
          filterFn = tab => tab != aTab && !tab.pinned && !tab.hidden;
        }
      } else {
        // Exclude just aTab from being removed.
        filterFn = tab => tab != aTab;
      }

      let tabsToRemove = this.openTabs.filter(filterFn);

      // If enabled show the tab close warning.
      if (
        !skipWarnAboutClosingTabs &&
        !this.warnAboutClosingTabs(
          tabsToRemove.length,
          this.closingTabsEnum.OTHER
        )
      ) {
        return;
      }

      this.removeTabs(tabsToRemove, aParams);
    }

    removeMultiSelectedTabs({ telemetrySource } = {}) {
      let selectedTabs = this.selectedTabs;
      if (
        !this.warnAboutClosingTabs(
          selectedTabs.length,
          this.closingTabsEnum.MULTI_SELECTED
        )
      ) {
        return;
      }

      this.removeTabs(selectedTabs, { telemetrySource });
    }

    /**
     * @typedef {object} _startRemoveTabsReturnValue
     * @property {Promise<void>} beforeUnloadComplete
     *   A promise that is resolved once all the beforeunload handlers have been
     *   called.
     * @property {object[]} tabsWithBeforeUnloadPrompt
     *   An array of tabs with unload prompts that need to be handled.
     * @property {object} [lastToClose]
     *   The last tab to be closed, if appropriate.
     */

    /**
     * Starts to remove tabs from the UI: checking for beforeunload handlers,
     * closing tabs where possible and triggering running of the unload handlers.
     *
     * @param {object[]} tabs
     *   The set of tabs to remove.
     * @param {object} options
     * @param {boolean} options.animate
     *   Whether or not to animate closing.
     * @param {boolean} options.suppressWarnAboutClosingWindow
     *   This will supress the warning about closing a window with the last tab.
     * @param {boolean} options.skipPermitUnload
     *   Skips the before unload checks for the tabs. Only set this to true when
     *   using it in tandem with `runBeforeUnloadForTabs`.
     * @param {boolean} options.skipRemoves
     *   Skips actually removing the tabs. The beforeunload handlers still run.
     * @param {boolean} options.skipSessionStore
     *   If true, don't record the closed tabs in SessionStore.
     * @returns {_startRemoveTabsReturnValue}
     */
    _startRemoveTabs(
      tabs,
      {
        animate,
        // See bug 1883051
        // eslint-disable-next-line no-unused-vars
        suppressWarnAboutClosingWindow,
        skipPermitUnload,
        skipRemoves,
        skipSessionStore,
        telemetrySource,
      }
    ) {
      // Note: if you change any of the unload algorithm, consider also
      // changing `runBeforeUnloadForTabs` above.
      /** @type {MozTabbrowserTab[]} */
      let tabsWithBeforeUnloadPrompt = [];
      /** @type {MozTabbrowserTab[]} */
      let tabsWithoutBeforeUnload = [];
      /** @type {Promise<void>[]} */
      let beforeUnloadPromises = [];
      /** @type {MozTabbrowserTab|undefined} */
      let lastToClose;

      for (let tab of tabs) {
        if (!skipRemoves) {
          tab._closedInMultiselection = true;
        }
        if (!skipRemoves && tab.selected) {
          lastToClose = tab;
          let toBlurTo = this._findTabToBlurTo(lastToClose, tabs);
          if (toBlurTo) {
            this._getSwitcher().warmupTab(toBlurTo);
          }
        } else if (!skipPermitUnload && this._hasBeforeUnload(tab)) {
          let timerId = Glean.browserTabclose.permitUnloadTime.start();
          // We need to block while calling permitUnload() because it
          // processes the event queue and may lead to another removeTab()
          // call before permitUnload() returns.
          tab._pendingPermitUnload = true;
          beforeUnloadPromises.push(
            // To save time, we first run the beforeunload event listeners in all
            // content processes in parallel. Tabs that would have shown a prompt
            // will be handled again later.
            tab.linkedBrowser.asyncPermitUnload("dontUnload").then(
              ({ permitUnload }) => {
                tab._pendingPermitUnload = false;
                Glean.browserTabclose.permitUnloadTime.stopAndAccumulate(
                  timerId
                );
                if (tab.closing) {
                  // The tab was closed by the user while we were in permitUnload, don't
                  // attempt to close it a second time.
                } else if (permitUnload) {
                  if (!skipRemoves) {
                    // OK to close without prompting, do it immediately.
                    this.removeTab(tab, {
                      animate,
                      prewarmed: true,
                      skipPermitUnload: true,
                      skipSessionStore,
                    });
                  }
                } else {
                  // We will need to prompt, queue it so it happens sequentially.
                  tabsWithBeforeUnloadPrompt.push(tab);
                }
              },
              err => {
                console.error("error while calling asyncPermitUnload", err);
                tab._pendingPermitUnload = false;
                Glean.browserTabclose.permitUnloadTime.stopAndAccumulate(
                  timerId
                );
              }
            )
          );
        } else {
          tabsWithoutBeforeUnload.push(tab);
        }
      }

      // Now that all the beforeunload IPCs have been sent to content processes,
      // we can queue unload messages for all the tabs without beforeunload listeners.
      // Doing this first would cause content process main threads to be busy and delay
      // beforeunload responses, which would be user-visible.
      if (!skipRemoves) {
        for (let tab of tabsWithoutBeforeUnload) {
          this.removeTab(tab, {
            animate,
            prewarmed: true,
            skipPermitUnload,
            skipSessionStore,
            telemetrySource,
          });
        }
      }

      return {
        beforeUnloadComplete: Promise.all(beforeUnloadPromises),
        tabsWithBeforeUnloadPrompt,
        lastToClose,
      };
    }

    /**
     * Runs the before unload handler for the provided tabs, waiting for them
     * to complete.
     *
     * This can be used in tandem with removeTabs to allow any before unload
     * prompts to happen before any tab closures. This should only be used
     * in the case where any prompts need to happen before other items before
     * the actual tabs are closed.
     *
     * When using this function alongside removeTabs, specify the `skipUnload`
     * option to removeTabs.
     *
     * @param {object[]} tabs
     *   An array of tabs to remove.
     * @returns {Promise<boolean>}
     *   Returns true if the unload has been blocked by the user. False if tabs
     *   may be subsequently closed.
     */
    async runBeforeUnloadForTabs(tabs) {
      try {
        let { beforeUnloadComplete, tabsWithBeforeUnloadPrompt } =
          this._startRemoveTabs(tabs, {
            animate: false,
            suppressWarnAboutClosingWindow: false,
            skipPermitUnload: false,
            skipRemoves: true,
          });

        await beforeUnloadComplete;

        // Now run again sequentially the beforeunload listeners that will result in a prompt.
        for (let tab of tabsWithBeforeUnloadPrompt) {
          tab._pendingPermitUnload = true;
          let { permitUnload } = this.getBrowserForTab(tab).permitUnload();
          tab._pendingPermitUnload = false;
          if (!permitUnload) {
            return true;
          }
        }
      } catch (e) {
        console.error(e);
      }
      return false;
    }

    /**
     * Given an array of tabs, returns a tuple [groups, leftoverTabs] such that:
     *  - groups contains all groups whose tabs are a subset of the initial array
     *  - leftoverTabs contains the remaining tabs
     * @param {Array} tabs list of tabs
     * @returns {Array} a tuple where the first element is an array of groups
     *                  and the second is an array of tabs
     */
    #separateWholeGroups(tabs) {
      /**
       * Map of tab group to surviving tabs in the group.
       * If any of the `tabs` to be removed belong to a tab group, keep track
       * of how many tabs in the tab group will be left after removing `tabs`.
       * For any tab group with 0 surviving tabs, we can know that that tab
       * group will be removed as a consequence of removing these `tabs`.
       * @type {Map<MozTabbrowserTabGroup, Set<MozTabbrowserTab>>}
       */
      let tabGroupSurvivingTabs = new Map();
      let wholeGroups = [];
      for (let tab of tabs) {
        if (tab.group) {
          if (!tabGroupSurvivingTabs.has(tab.group)) {
            tabGroupSurvivingTabs.set(tab.group, new Set(tab.group.tabs));
          }
          tabGroupSurvivingTabs.get(tab.group).delete(tab);
        }
      }

      for (let [tabGroup, survivingTabs] of tabGroupSurvivingTabs.entries()) {
        if (!survivingTabs.size) {
          wholeGroups.push(tabGroup);
          tabs = tabs.filter(t => !tabGroup.tabs.includes(t));
        }
      }

      return [wholeGroups, tabs];
    }

    /**
     * Removes multiple tabs from the tab browser.
     *
     * @param {MozTabbrowserTab[]} tabs
     *   The set of tabs to remove.
     * @param {object} [options]
     * @param {boolean} [options.animate]
     *   Whether or not to animate closing, defaults to true.
     * @param {boolean} [options.suppressWarnAboutClosingWindow]
     *   This will supress the warning about closing a window with the last tab.
     * @param {boolean} [options.skipPermitUnload]
     *   Skips the before unload checks for the tabs. Only set this to true when
     *   using it in tandem with `runBeforeUnloadForTabs`.
     * @param {boolean}  [options.skipSessionStore]
     *   If true, don't record the closed tabs in SessionStore.
     * @param {boolean} [options.skipGroupCheck]
     *   Skip separate processing of whole tab groups from the set of tabs.
     *   Used by removeTabGroup.
     */
    removeTabs(
      tabs,
      {
        animate = true,
        suppressWarnAboutClosingWindow = false,
        skipPermitUnload = false,
        skipSessionStore = false,
        skipGroupCheck = false,
        telemetrySource,
      } = {}
    ) {
      // When 'closeWindowWithLastTab' pref is enabled, closing all tabs
      // can be considered equivalent to closing the window.
      if (
        this.tabs.length == tabs.length &&
        Services.prefs.getBoolPref("browser.tabs.closeWindowWithLastTab")
      ) {
        window.closeWindow(
          true,
          suppressWarnAboutClosingWindow ? null : window.warnAboutClosingWindow,
          "close-last-tab"
        );
        return;
      }

      if (!skipSessionStore) {
        SessionStore.resetLastClosedTabCount(window);
      }
      this._clearMultiSelectionLocked = true;

      // Guarantee that _clearMultiSelectionLocked lock gets released.
      try {
        // If selection includes entire groups, we might want to save them
        if (!skipGroupCheck) {
          let [groups, leftoverTabs] = this.#separateWholeGroups(tabs);
          groups.forEach(group => {
            if (!skipSessionStore) {
              group.save();
            }
            this.removeTabGroup(group, {
              animate,
              skipSessionStore,
              skipPermitUnload,
            });
          });
          tabs = leftoverTabs;
        }

        let { beforeUnloadComplete, tabsWithBeforeUnloadPrompt, lastToClose } =
          this._startRemoveTabs(tabs, {
            animate,
            suppressWarnAboutClosingWindow,
            skipPermitUnload,
            skipRemoves: false,
            skipSessionStore,
            telemetrySource,
          });

        // Wait for all the beforeunload events to have been processed by content processes.
        // The permitUnload() promise will, alas, not call its resolution
        // callbacks after the browser window the promise lives in has closed,
        // so we have to check for that case explicitly.
        let done = false;
        beforeUnloadComplete.then(() => {
          done = true;
        });
        Services.tm.spinEventLoopUntilOrQuit(
          "tabbrowser.js:removeTabs",
          () => done || window.closed
        );
        if (!done) {
          return;
        }

        let aParams = {
          animate,
          prewarmed: true,
          skipPermitUnload,
          skipSessionStore,
        };

        // Now run again sequentially the beforeunload listeners that will result in a prompt.
        for (let tab of tabsWithBeforeUnloadPrompt) {
          this.removeTab(tab, aParams);
          if (!tab.closing) {
            // If we abort the closing of the tab.
            tab._closedInMultiselection = false;
          }
        }

        // Avoid changing the selected browser several times by removing it,
        // if appropriate, lastly.
        if (lastToClose) {
          this.removeTab(lastToClose, aParams);
        }
      } catch (e) {
        console.error(e);
      }

      this._clearMultiSelectionLocked = false;
      this._avoidSingleSelectedTab();
    }

    removeCurrentTab(aParams) {
      this.removeTab(this.selectedTab, aParams);
    }

    removeTab(
      aTab,
      {
        animate,
        triggeringEvent,
        skipPermitUnload,
        closeWindowWithLastTab,
        prewarmed,
        skipSessionStore,
        telemetrySource,
      } = {}
    ) {
      if (UserInteraction.running("browser.tabs.opening", window)) {
        UserInteraction.finish("browser.tabs.opening", window);
      }

      // Telemetry stopwatches may already be running if removeTab gets
      // called again for an already closing tab.
      if (!aTab._closeTimeAnimTimerId && !aTab._closeTimeNoAnimTimerId) {
        // Speculatevely start both stopwatches now. We'll cancel one of
        // the two later depending on whether we're animating.
        aTab._closeTimeAnimTimerId = Glean.browserTabclose.timeAnim.start();
        aTab._closeTimeNoAnimTimerId = Glean.browserTabclose.timeNoAnim.start();
      }

      // Handle requests for synchronously removing an already
      // asynchronously closing tab.
      if (!animate && aTab.closing) {
        this._endRemoveTab(aTab);
        return;
      }

      let isVisibleTab = aTab.visible;
      let isLastTab = isVisibleTab && this.visibleTabs.length == 1;
      // We have to sample the tab width now, since _beginRemoveTab might
      // end up modifying the DOM in such a way that aTab gets a new
      // frame created for it (for example, by updating the visually selected
      // state).
      let tabWidth = window.windowUtils.getBoundsWithoutFlushing(aTab).width;

      if (
        !this._beginRemoveTab(aTab, {
          closeWindowFastpath: true,
          skipPermitUnload,
          closeWindowWithLastTab,
          prewarmed,
          skipSessionStore,
          telemetrySource,
        })
      ) {
        Glean.browserTabclose.timeAnim.cancel(aTab._closeTimeAnimTimerId);
        aTab._closeTimeAnimTimerId = null;
        Glean.browserTabclose.timeNoAnim.cancel(aTab._closeTimeNoAnimTimerId);
        aTab._closeTimeNoAnimTimerId = null;
        return;
      }

      let lockTabSizing =
        !this.tabContainer.verticalMode &&
        !aTab.pinned &&
        isVisibleTab &&
        aTab._fullyOpen &&
        triggeringEvent?.inputSource == MouseEvent.MOZ_SOURCE_MOUSE &&
        triggeringEvent?.target.closest(".tabbrowser-tab");
      if (lockTabSizing) {
        this.tabContainer._lockTabSizing(aTab, tabWidth);
      } else {
        this.tabContainer._unlockTabSizing();
      }

      if (
        !animate /* the caller didn't opt in */ ||
        gReduceMotion ||
        isLastTab ||
        aTab.pinned ||
        !isVisibleTab ||
        this.tabContainer.verticalMode ||
        this._removingTabs.size >
          3 /* don't want lots of concurrent animations */ ||
        !aTab.hasAttribute(
          "fadein"
        ) /* fade-in transition hasn't been triggered yet */ ||
        tabWidth == 0 /* fade-in transition hasn't moved yet */
      ) {
        // We're not animating, so we can cancel the animation stopwatch.
        Glean.browserTabclose.timeAnim.cancel(aTab._closeTimeAnimTimerId);
        aTab._closeTimeAnimTimerId = null;
        this._endRemoveTab(aTab);
        return;
      }

      // We're animating, so we can cancel the non-animation stopwatch.
      Glean.browserTabclose.timeNoAnim.cancel(aTab._closeTimeNoAnimTimerId);
      aTab._closeTimeNoAnimTimerId = null;

      aTab.style.maxWidth = ""; // ensure that fade-out transition happens
      aTab.removeAttribute("fadein");
      aTab.removeAttribute("bursting");

      setTimeout(
        function (tab, tabbrowser) {
          if (
            tab.container &&
            window.getComputedStyle(tab).maxWidth == "0.1px"
          ) {
            console.assert(
              false,
              "Giving up waiting for the tab closing animation to finish (bug 608589)"
            );
            tabbrowser._endRemoveTab(tab);
          }
        },
        3000,
        aTab,
        this
      );
    }

    _hasBeforeUnload(aTab) {
      let browser = aTab.linkedBrowser;
      if (browser.isRemoteBrowser && browser.frameLoader) {
        return browser.hasBeforeUnload;
      }
      return false;
    }

    _beginRemoveTab(
      aTab,
      {
        adoptedByTab,
        closeWindowWithLastTab,
        closeWindowFastpath,
        skipPermitUnload,
        prewarmed,
        skipSessionStore = false,
        telemetrySource,
      } = {}
    ) {
      if (aTab.closing || this._windowIsClosing) {
        return false;
      }

      var browser = this.getBrowserForTab(aTab);
      if (
        !skipPermitUnload &&
        !adoptedByTab &&
        aTab.linkedPanel &&
        !aTab._pendingPermitUnload &&
        (!browser.isRemoteBrowser || this._hasBeforeUnload(aTab))
      ) {
        if (!prewarmed) {
          let blurTab = this._findTabToBlurTo(aTab);
          if (blurTab) {
            this.warmupTab(blurTab);
          }
        }

        let timerId = Glean.browserTabclose.permitUnloadTime.start();

        // We need to block while calling permitUnload() because it
        // processes the event queue and may lead to another removeTab()
        // call before permitUnload() returns.
        aTab._pendingPermitUnload = true;
        let { permitUnload } = browser.permitUnload();
        aTab._pendingPermitUnload = false;

        Glean.browserTabclose.permitUnloadTime.stopAndAccumulate(timerId);

        // If we were closed during onbeforeunload, we return false now
        // so we don't (try to) close the same tab again. Of course, we
        // also stop if the unload was cancelled by the user:
        if (aTab.closing || !permitUnload) {
          return false;
        }
      }

      aTab.closing = true;
      this.tabContainer._invalidateCachedVisibleTabs();

      // this._switcher would normally cover removing a tab from this
      // cache, but we may not have one at this time.
      let tabCacheIndex = this._tabLayerCache.indexOf(aTab);
      if (tabCacheIndex != -1) {
        this._tabLayerCache.splice(tabCacheIndex, 1);
      }

      // Delay hiding the the active tab if we're screen sharing.
      // See Bug 1642747.
      let screenShareInActiveTab =
        aTab == this.selectedTab && aTab._sharingState?.webRTC?.screen;

      if (!screenShareInActiveTab) {
        this._blurTab(aTab);
      }

      var closeWindow = false;
      var newTab = false;
      if (!this.visibleTabs.length && !this.tabsInCollapsedTabGroups.length) {
        closeWindow =
          closeWindowWithLastTab != null
            ? closeWindowWithLastTab
            : !window.toolbar.visible ||
              Services.prefs.getBoolPref("browser.tabs.closeWindowWithLastTab");

        if (closeWindow) {
          // We've already called beforeunload on all the relevant tabs if we get here,
          // so avoid calling it again:
          window.skipNextCanClose = true;
        }

        // Closing the tab and replacing it with a blank one is notably slower
        // than closing the window right away. If the caller opts in, take
        // the fast path.
        if (closeWindow && closeWindowFastpath && !this._removingTabs.size) {
          // This call actually closes the window, unless the user
          // cancels the operation.  We are finished here in both cases.
          this._windowIsClosing = window.closeWindow(
            true,
            window.warnAboutClosingWindow,
            "close-last-tab"
          );
          return false;
        }

        newTab = true;
      }
      aTab._endRemoveArgs = [closeWindow, newTab];

      // swapBrowsersAndCloseOther will take care of closing the window without animation.
      if (closeWindow && adoptedByTab) {
        // Remove the tab's filter and progress listener to avoid leaking.
        if (aTab.linkedPanel) {
          const filter = this._tabFilters.get(aTab);
          browser.webProgress.removeProgressListener(filter);
          const listener = this._tabListeners.get(aTab);
          filter.removeProgressListener(listener);
          listener.destroy();
          this._tabListeners.delete(aTab);
          this._tabFilters.delete(aTab);
        }
        return true;
      }

      if (!aTab._fullyOpen) {
        // If the opening tab animation hasn't finished before we start closing the
        // tab, decrement the animation count since _handleNewTab will not get called.
        this.tabAnimationsInProgress--;
      }

      this.tabAnimationsInProgress++;

      // Mute audio immediately to improve perceived speed of tab closure.
      if (!adoptedByTab && aTab.hasAttribute("soundplaying")) {
        // Don't persist the muted state as this wasn't a user action.
        // This lets undo-close-tab return it to an unmuted state.
        aTab.linkedBrowser.mute(true);
      }

      this._removingTabs.add(aTab);
      this.tabContainer._invalidateCachedTabs();

      // Invalidate hovered tab state tracking for this closing tab.
      aTab._mouseleave();

      if (newTab) {
        this.addTrustedTab(BROWSER_NEW_TAB_URL, {
          skipAnimation: true,
        });
      } else {
        TabBarVisibility.update();
      }

      // Splice this tab out of any lines of succession before any events are
      // dispatched.
      this.replaceInSuccession(aTab, aTab.successor);
      this.setSuccessor(aTab, null);

      // We're committed to closing the tab now.
      // Dispatch a notification.
      // We dispatch it before any teardown so that event listeners can
      // inspect the tab that's about to close.
      let evt = new CustomEvent("TabClose", {
        bubbles: true,
        detail: { adoptedBy: adoptedByTab, skipSessionStore, telemetrySource },
      });
      aTab.dispatchEvent(evt);

      if (this.tabs.length == 2) {
        // We're closing one of our two open tabs, inform the other tab that its
        // sibling is going away.
        for (let tab of this.tabs) {
          let bc = tab.linkedBrowser.browsingContext;
          if (bc) {
            bc.hasSiblings = false;
          }
        }
      }

      let notificationBox = this.readNotificationBox(browser);
      notificationBox?._stack?.remove();

      if (aTab.linkedPanel) {
        if (!adoptedByTab && !gMultiProcessBrowser) {
          // Prevent this tab from showing further dialogs, since we're closing it
          browser.contentWindow.windowUtils.disableDialogs();
        }

        // Remove the tab's filter and progress listener.
        const filter = this._tabFilters.get(aTab);

        browser.webProgress.removeProgressListener(filter);

        const listener = this._tabListeners.get(aTab);
        filter.removeProgressListener(listener);
        listener.destroy();
      }

      if (browser.registeredOpenURI && !adoptedByTab) {
        let userContextId = browser.getAttribute("usercontextid") || 0;
        this.UrlbarProviderOpenTabs.unregisterOpenTab(
          browser.registeredOpenURI.spec,
          userContextId,
          PrivateBrowsingUtils.isWindowPrivate(window)
        );
        delete browser.registeredOpenURI;
      }

      // We are no longer the primary content area.
      browser.removeAttribute("primary");

      return true;
    }

    _endRemoveTab(aTab) {
      if (!aTab || !aTab._endRemoveArgs) {
        return;
      }

      var [aCloseWindow, aNewTab] = aTab._endRemoveArgs;
      aTab._endRemoveArgs = null;

      if (this._windowIsClosing) {
        aCloseWindow = false;
        aNewTab = false;
      }

      this.tabAnimationsInProgress--;

      this._lastRelatedTabMap = new WeakMap();

      // update the UI early for responsiveness
      aTab.collapsed = true;
      this._blurTab(aTab);

      this._removingTabs.delete(aTab);

      if (aCloseWindow) {
        this._windowIsClosing = true;
        for (let tab of this._removingTabs) {
          this._endRemoveTab(tab);
        }
      } else if (!this._windowIsClosing) {
        if (aNewTab) {
          gURLBar.select();
        }
      }

      // We're going to remove the tab and the browser now.
      this._tabFilters.delete(aTab);
      this._tabListeners.delete(aTab);

      var browser = this.getBrowserForTab(aTab);

      if (aTab.linkedPanel) {
        // Because of the fact that we are setting JS properties on
        // the browser elements, and we have code in place
        // to preserve the JS objects for any elements that have
        // JS properties set on them, the browser element won't be
        // destroyed until the document goes away.  So we force a
        // cleanup ourselves.
        // This has to happen before we remove the child since functions
        // like `getBrowserContainer` expect the browser to be parented.
        browser.destroy();
      }

      var wasPinned = aTab.pinned;

      // Remove the tab ...
      aTab.remove();
      this.tabContainer._invalidateCachedTabs();

      // ... and fix up the _tPos properties immediately.
      for (let i = aTab._tPos; i < this.tabs.length; i++) {
        this.tabs[i]._tPos = i;
      }

      if (!this._windowIsClosing) {
        if (wasPinned) {
          this.tabContainer._positionPinnedTabs();
        }

        // update tab close buttons state
        this.tabContainer._updateCloseButtons();

        setTimeout(
          function (tabs) {
            tabs._lastTabClosedByMouse = false;
          },
          0,
          this.tabContainer
        );
      }

      // update tab positional properties and attributes
      this.selectedTab._selected = true;

      // Removing the panel requires fixing up selectedPanel immediately
      // (see below), which would be hindered by the potentially expensive
      // browser removal. So we remove the browser and the panel in two
      // steps.

      var panel = this.getPanel(browser);

      // In the multi-process case, it's possible an asynchronous tab switch
      // is still underway. If so, then it's possible that the last visible
      // browser is the one we're in the process of removing. There's the
      // risk of displaying preloaded browsers that are at the end of the
      // deck if we remove the browser before the switch is complete, so
      // we alert the switcher in order to show a spinner instead.
      if (this._switcher) {
        this._switcher.onTabRemoved(aTab);
      }

      // This will unload the document. An unload handler could remove
      // dependant tabs, so it's important that the tabbrowser is now in
      // a consistent state (tab removed, tab positions updated, etc.).
      browser.remove();

      // Release the browser in case something is erroneously holding a
      // reference to the tab after its removal.
      this._tabForBrowser.delete(aTab.linkedBrowser);
      aTab.linkedBrowser = null;

      panel.remove();

      // closeWindow might wait an arbitrary length of time if we're supposed
      // to warn about closing the window, so we'll just stop the tab close
      // stopwatches here instead.
      if (aTab._closeTimeAnimTimerId) {
        Glean.browserTabclose.timeAnim.stopAndAccumulate(
          aTab._closeTimeAnimTimerId
        );
        aTab._closeTimeAnimTimerId = null;
      }
      if (aTab._closeTimeNoAnimTimerId) {
        Glean.browserTabclose.timeNoAnim.stopAndAccumulate(
          aTab._closeTimeNoAnimTimerId
        );
        aTab._closeTimeNoAnimTimerId = null;
      }

      if (aCloseWindow) {
        this._windowIsClosing = closeWindow(
          true,
          window.warnAboutClosingWindow,
          "close-last-tab"
        );
      }
    }

    /**
     * Closes all tabs matching the list of nsURIs.
     * This does not close any tabs that have a beforeUnload prompt.
     *
     * @param {nsURI[]} urisToClose
     *   The set of uris to remove.
     * @returns {number} The count of successfully closed tabs.
     */
    async closeTabsByURI(urisToClose) {
      let tabsToRemove = [];
      for (let tab of this.tabs) {
        let currentURI = tab.linkedBrowser.currentURI;
        // Find any URI that matches the current tab's URI
        const matchedIndex = urisToClose.findIndex(uriToClose =>
          uriToClose.equals(currentURI)
        );

        if (matchedIndex > -1) {
          tabsToRemove.push(tab);
        }
      }

      let closedCount = 0;

      if (tabsToRemove.length) {
        const { beforeUnloadComplete, lastToClose } = this._startRemoveTabs(
          tabsToRemove,
          {
            animate: false,
            suppressWarnAboutClosingWindow: true,
            skipPermitUnload: false,
            skipRemoves: false,
            skipSessionStore: false,
          }
        );

        // Wait for the beforeUnload handlers to complete.
        await beforeUnloadComplete;

        closedCount = tabsToRemove.length - (lastToClose ? 1 : 0);

        // _startRemoveTabs doesn't close the last tab in the window
        // for this use case, we simply close it
        if (lastToClose) {
          this.removeTab(lastToClose);
          closedCount++;
        }
      }
      return closedCount;
    }

    async explicitUnloadTabs(tabs) {
      let unloadBlocked = await this.runBeforeUnloadForTabs(tabs);
      if (unloadBlocked) {
        return;
      }
      let unloadSelectedTab = false;
      let allTabsUnloaded = false;
      if (tabs.some(tab => tab.selected)) {
        // Unloading the currently selected tab.
        // Need to select a different one before unloading.
        // Avoid selecting any tab we're unloading now or
        // any tab that is already unloaded.
        unloadSelectedTab = true;
        const tabsToExclude = tabs.concat(
          this.tabContainer.allTabs.filter(tab => !tab.linkedPanel)
        );
        let newTab = this._findTabToBlurTo(this.selectedTab, tabsToExclude);
        if (newTab) {
          this.selectedTab = newTab;
        } else {
          allTabsUnloaded = true;
          // all tabs are unloaded - show Firefox View if it's present, otherwise open a new tab
          if (FirefoxViewHandler.tab || FirefoxViewHandler.button) {
            FirefoxViewHandler.openTab("opentabs");
          } else {
            this.selectedTab = this.addTrustedTab(BROWSER_NEW_TAB_URL, {
              skipAnimation: true,
            });
          }
        }
      }
      let memoryUsageBeforeUnload = await getTotalMemoryUsage();
      let timeBeforeUnload = performance.now();
      let numberOfTabsUnloaded = 0;
      for (let tab of tabs) {
        numberOfTabsUnloaded += this.discardBrowser(tab, true) ? 1 : 0;
      }
      let timeElapsed = Math.floor(performance.now() - timeBeforeUnload);
      Glean.browserEngagement.tabExplicitUnload.record({
        unload_selected_tab: unloadSelectedTab,
        all_tabs_unloaded: allTabsUnloaded,
        tabs_unloaded: numberOfTabsUnloaded,
        memory_before: memoryUsageBeforeUnload,
        memory_after: await getTotalMemoryUsage(),
        time_to_unload_in_ms: timeElapsed,
      });
    }

    /**
     * Handles opening a new tab with mouse middleclick.
     * @param node
     * @param event
     *        The click event
     */
    handleNewTabMiddleClick(node, event) {
      // We should be using the disabled property here instead of the attribute,
      // but some elements that this function is used with don't support it (e.g.
      // menuitem).
      if (node.getAttribute("disabled") == "true") {
        return;
      } // Do nothing

      if (event.button == 1) {
        BrowserCommands.openTab({ event });
        // Stop the propagation of the click event, to prevent the event from being
        // handled more than once.
        // E.g. see https://bugzilla.mozilla.org/show_bug.cgi?id=1657992#c4
        event.stopPropagation();
        event.preventDefault();
      }
    }

    /**
     * Finds the tab that we will blur to if we blur aTab.
     * @param   {MozTabbrowserTab} aTab
     *          The tab we would blur
     * @param   {MozTabbrowserTab[]} [aExcludeTabs=[]]
     *          Tabs to exclude from our search (i.e., because they are being
     *          closed along with aTab)
     */
    _findTabToBlurTo(aTab, aExcludeTabs = []) {
      if (!aTab.selected) {
        return null;
      }
      if (FirefoxViewHandler.tab) {
        aExcludeTabs.push(FirefoxViewHandler.tab);
      }

      let excludeTabs = new Set(aExcludeTabs);

      // If this tab has a successor, it should be selectable, since
      // hiding or closing a tab removes that tab as a successor.
      if (aTab.successor && !excludeTabs.has(aTab.successor)) {
        return aTab.successor;
      }

      if (
        aTab.owner?.visible &&
        !excludeTabs.has(aTab.owner) &&
        Services.prefs.getBoolPref("browser.tabs.selectOwnerOnClose")
      ) {
        return aTab.owner;
      }

      // Try to find a remaining tab that comes after the given tab
      let remainingTabs = Array.prototype.filter.call(
        this.visibleTabs,
        tab => !excludeTabs.has(tab)
      );

      let tab = this.tabContainer.findNextTab(aTab, {
        direction: 1,
        filter: _tab => remainingTabs.includes(_tab),
      });

      if (!tab) {
        tab = this.tabContainer.findNextTab(aTab, {
          direction: -1,
          filter: _tab => remainingTabs.includes(_tab),
        });
      }

      if (tab) {
        return tab;
      }

      // If no qualifying visible tab was found, see if there is a tab in
      // a collapsed tab group that could be selected.
      let eligibleTabs = new Set(this.tabsInCollapsedTabGroups).difference(
        excludeTabs
      );

      tab = this.tabContainer.findNextTab(aTab, {
        direction: 1,
        filter: _tab => eligibleTabs.has(_tab),
      });

      if (!tab) {
        tab = this.tabContainer.findNextTab(aTab, {
          direction: -1,
          filter: _tab => eligibleTabs.has(_tab),
        });
      }

      return tab;
    }

    _blurTab(aTab) {
      this.selectedTab = this._findTabToBlurTo(aTab);
    }

    /**
     * @returns {boolean}
     *   False if swapping isn't permitted, true otherwise.
     */
    swapBrowsersAndCloseOther(aOurTab, aOtherTab) {
      // Do not allow transfering a private tab to a non-private window
      // and vice versa.
      if (
        PrivateBrowsingUtils.isWindowPrivate(window) !=
        PrivateBrowsingUtils.isWindowPrivate(aOtherTab.ownerGlobal)
      ) {
        return false;
      }

      // Do not allow transfering a useRemoteSubframes tab to a
      // non-useRemoteSubframes window and vice versa.
      if (gFissionBrowser != aOtherTab.ownerGlobal.gFissionBrowser) {
        return false;
      }

      let ourBrowser = this.getBrowserForTab(aOurTab);
      let otherBrowser = aOtherTab.linkedBrowser;

      // Can't swap between chrome and content processes.
      if (ourBrowser.isRemoteBrowser != otherBrowser.isRemoteBrowser) {
        return false;
      }

      // Keep the userContextId if set on other browser
      if (otherBrowser.hasAttribute("usercontextid")) {
        ourBrowser.setAttribute(
          "usercontextid",
          otherBrowser.getAttribute("usercontextid")
        );
      }

      // That's gBrowser for the other window, not the tab's browser!
      var remoteBrowser = aOtherTab.ownerGlobal.gBrowser;
      var isPending = aOtherTab.hasAttribute("pending");

      let otherTabListener = remoteBrowser._tabListeners.get(aOtherTab);
      let stateFlags = 0;
      if (otherTabListener) {
        stateFlags = otherTabListener.mStateFlags;
      }

      // Expedite the removal of the icon if it was already scheduled.
      if (aOtherTab._soundPlayingAttrRemovalTimer) {
        clearTimeout(aOtherTab._soundPlayingAttrRemovalTimer);
        aOtherTab._soundPlayingAttrRemovalTimer = 0;
        aOtherTab.removeAttribute("soundplaying");
        remoteBrowser._tabAttrModified(aOtherTab, ["soundplaying"]);
      }

      // First, start teardown of the other browser.  Make sure to not
      // fire the beforeunload event in the process.  Close the other
      // window if this was its last tab.
      if (
        !remoteBrowser._beginRemoveTab(aOtherTab, {
          adoptedByTab: aOurTab,
          closeWindowWithLastTab: true,
        })
      ) {
        return false;
      }

      // If this is the last tab of the window, hide the window
      // immediately without animation before the docshell swap, to avoid
      // about:blank being painted.
      let [closeWindow] = aOtherTab._endRemoveArgs;
      if (closeWindow) {
        let win = aOtherTab.ownerGlobal;
        win.windowUtils.suppressAnimation(true);
        // Only suppressing window animations isn't enough to avoid
        // an empty content area being painted.
        let baseWin = win.docShell.treeOwner.QueryInterface(Ci.nsIBaseWindow);
        baseWin.visibility = false;
      }

      let modifiedAttrs = [];
      if (aOtherTab.hasAttribute("muted")) {
        aOurTab.toggleAttribute("muted", true);
        aOurTab.muteReason = aOtherTab.muteReason;
        // For non-lazy tabs, mute() must be called.
        if (aOurTab.linkedPanel) {
          ourBrowser.mute();
        }
        modifiedAttrs.push("muted");
      }
      if (aOtherTab.hasAttribute("undiscardable")) {
        aOurTab.toggleAttribute("undiscardable", true);
        modifiedAttrs.push("undiscardable");
      }
      if (aOtherTab.hasAttribute("soundplaying")) {
        aOurTab.toggleAttribute("soundplaying", true);
        modifiedAttrs.push("soundplaying");
      }
      if (aOtherTab.hasAttribute("usercontextid")) {
        aOurTab.setUserContextId(aOtherTab.getAttribute("usercontextid"));
        modifiedAttrs.push("usercontextid");
      }
      if (aOtherTab.hasAttribute("sharing")) {
        aOurTab.setAttribute("sharing", aOtherTab.getAttribute("sharing"));
        modifiedAttrs.push("sharing");
        aOurTab._sharingState = aOtherTab._sharingState;
        webrtcUI.swapBrowserForNotification(otherBrowser, ourBrowser);
      }
      if (aOtherTab.hasAttribute("pictureinpicture")) {
        aOurTab.toggleAttribute("pictureinpicture", true);
        modifiedAttrs.push("pictureinpicture");

        let event = new CustomEvent("TabSwapPictureInPicture", {
          detail: aOurTab,
        });
        aOtherTab.dispatchEvent(event);
      }

      if (otherBrowser.isDistinctProductPageVisit) {
        ourBrowser.isDistinctProductPageVisit = true;
      }

      if (otherBrowser.reviewCheckerWasClosed) {
        ourBrowser.reviewCheckerWasClosed = true;
      }

      SitePermissions.copyTemporaryPermissions(otherBrowser, ourBrowser);

      // If the other tab is pending (i.e. has not been restored, yet)
      // then do not switch docShells but retrieve the other tab's state
      // and apply it to our tab.
      if (isPending) {
        // Tag tab so that the extension framework can ignore tab events that
        // are triggered amidst the tab/browser restoration process
        // (TabHide, TabPinned, TabUnpinned, "muted" attribute changes, etc.).
        aOurTab.initializingTab = true;
        delete ourBrowser._cachedCurrentURI;
        SessionStore.setTabState(aOurTab, SessionStore.getTabState(aOtherTab));
        delete aOurTab.initializingTab;

        // Make sure to unregister any open URIs.
        this._swapRegisteredOpenURIs(ourBrowser, otherBrowser);
      } else {
        // Workarounds for bug 458697
        // Icon might have been set on DOMLinkAdded, don't override that.
        if (!ourBrowser.mIconURL && otherBrowser.mIconURL) {
          this.setIcon(aOurTab, otherBrowser.mIconURL);
        }
        var isBusy = aOtherTab.hasAttribute("busy");
        if (isBusy) {
          aOurTab.setAttribute("busy", "true");
          modifiedAttrs.push("busy");
          if (aOurTab.selected) {
            this._isBusy = true;
          }
        }

        this._swapBrowserDocShells(aOurTab, otherBrowser, stateFlags);
      }

      // Unregister the previously opened URI
      if (otherBrowser.registeredOpenURI) {
        let userContextId = otherBrowser.getAttribute("usercontextid") || 0;
        this.UrlbarProviderOpenTabs.unregisterOpenTab(
          otherBrowser.registeredOpenURI.spec,
          userContextId,
          PrivateBrowsingUtils.isWindowPrivate(window)
        );
        delete otherBrowser.registeredOpenURI;
      }

      // Handle findbar data (if any)
      let otherFindBar = aOtherTab._findBar;
      if (otherFindBar && otherFindBar.findMode == otherFindBar.FIND_NORMAL) {
        let oldValue = otherFindBar._findField.value;
        let wasHidden = otherFindBar.hidden;
        let ourFindBarPromise = this.getFindBar(aOurTab);
        ourFindBarPromise.then(ourFindBar => {
          if (!ourFindBar) {
            return;
          }
          ourFindBar._findField.value = oldValue;
          if (!wasHidden) {
            ourFindBar.onFindCommand();
          }
        });
      }

      // Finish tearing down the tab that's going away.
      if (closeWindow) {
        aOtherTab.ownerGlobal.close();
      } else {
        remoteBrowser._endRemoveTab(aOtherTab);
      }

      this.setTabTitle(aOurTab);

      // If the tab was already selected (this happens in the scenario
      // of replaceTabWithWindow), notify onLocationChange, etc.
      if (aOurTab.selected) {
        this.updateCurrentBrowser(true);
      }

      if (modifiedAttrs.length) {
        this._tabAttrModified(aOurTab, modifiedAttrs);
      }

      return true;
    }

    swapBrowsers(aOurTab, aOtherTab) {
      let otherBrowser = aOtherTab.linkedBrowser;
      let otherTabBrowser = otherBrowser.getTabBrowser();

      // We aren't closing the other tab so, we also need to swap its tablisteners.
      let filter = otherTabBrowser._tabFilters.get(aOtherTab);
      let tabListener = otherTabBrowser._tabListeners.get(aOtherTab);
      otherBrowser.webProgress.removeProgressListener(filter);
      filter.removeProgressListener(tabListener);

      // Perform the docshell swap through the common mechanism.
      this._swapBrowserDocShells(aOurTab, otherBrowser);

      // Restore the listeners for the swapped in tab.
      tabListener = new otherTabBrowser.ownerGlobal.TabProgressListener(
        aOtherTab,
        otherBrowser,
        false,
        false
      );
      otherTabBrowser._tabListeners.set(aOtherTab, tabListener);

      const notifyAll = Ci.nsIWebProgress.NOTIFY_ALL;
      filter.addProgressListener(tabListener, notifyAll);
      otherBrowser.webProgress.addProgressListener(filter, notifyAll);
    }

    _swapBrowserDocShells(aOurTab, aOtherBrowser, aStateFlags) {
      // aOurTab's browser needs to be inserted now if it hasn't already.
      this._insertBrowser(aOurTab);

      // Unhook our progress listener
      const filter = this._tabFilters.get(aOurTab);
      let tabListener = this._tabListeners.get(aOurTab);
      let ourBrowser = this.getBrowserForTab(aOurTab);
      ourBrowser.webProgress.removeProgressListener(filter);
      filter.removeProgressListener(tabListener);

      // Make sure to unregister any open URIs.
      this._swapRegisteredOpenURIs(ourBrowser, aOtherBrowser);

      let remoteBrowser = aOtherBrowser.ownerGlobal.gBrowser;

      // If switcher is active, it will intercept swap events and
      // react as needed.
      if (!this._switcher) {
        aOtherBrowser.docShellIsActive =
          this.shouldActivateDocShell(ourBrowser);
      }

      let ourBrowserContainer =
        ourBrowser.ownerDocument.getElementById("browser");
      let otherBrowserContainer =
        aOtherBrowser.ownerDocument.getElementById("browser");
      let ourBrowserContainerWasHidden = ourBrowserContainer.hidden;
      let otherBrowserContainerWasHidden = otherBrowserContainer.hidden;

      // #browser is hidden in Customize Mode; this breaks docshell swapping,
      // so we need to toggle 'hidden' to make swapping work in this case.
      ourBrowserContainer.hidden = otherBrowserContainer.hidden = false;

      // Swap the docshells
      ourBrowser.swapDocShells(aOtherBrowser);

      ourBrowserContainer.hidden = ourBrowserContainerWasHidden;
      otherBrowserContainer.hidden = otherBrowserContainerWasHidden;

      // Swap permanentKey properties.
      let ourPermanentKey = ourBrowser.permanentKey;
      ourBrowser.permanentKey = aOtherBrowser.permanentKey;
      aOtherBrowser.permanentKey = ourPermanentKey;
      aOurTab.permanentKey = ourBrowser.permanentKey;
      if (remoteBrowser) {
        let otherTab = remoteBrowser.getTabForBrowser(aOtherBrowser);
        if (otherTab) {
          otherTab.permanentKey = aOtherBrowser.permanentKey;
        }
      }

      // Restore the progress listener
      tabListener = new TabProgressListener(
        aOurTab,
        ourBrowser,
        false,
        false,
        aStateFlags
      );
      this._tabListeners.set(aOurTab, tabListener);

      const notifyAll = Ci.nsIWebProgress.NOTIFY_ALL;
      filter.addProgressListener(tabListener, notifyAll);
      ourBrowser.webProgress.addProgressListener(filter, notifyAll);
    }

    _swapRegisteredOpenURIs(aOurBrowser, aOtherBrowser) {
      // Swap the registeredOpenURI properties of the two browsers
      let tmp = aOurBrowser.registeredOpenURI;
      delete aOurBrowser.registeredOpenURI;
      if (aOtherBrowser.registeredOpenURI) {
        aOurBrowser.registeredOpenURI = aOtherBrowser.registeredOpenURI;
        delete aOtherBrowser.registeredOpenURI;
      }
      if (tmp) {
        aOtherBrowser.registeredOpenURI = tmp;
      }
    }

    reloadMultiSelectedTabs() {
      this.reloadTabs(this.selectedTabs);
    }

    reloadTabs(tabs) {
      for (let tab of tabs) {
        try {
          this.getBrowserForTab(tab).reload();
        } catch (e) {
          // ignore failure to reload so others will be reloaded
        }
      }
    }

    reloadTab(aTab) {
      let browser = this.getBrowserForTab(aTab);
      // Reset temporary permissions on the current tab. This is done here
      // because we only want to reset permissions on user reload.
      SitePermissions.clearTemporaryBlockPermissions(browser);
      // Also reset DOS mitigations for the basic auth prompt on reload.
      delete browser.authPromptAbuseCounter;
      gIdentityHandler.hidePopup();
      gPermissionPanel.hidePopup();
      browser.reload();
    }

    addProgressListener(aListener) {
      if (arguments.length != 1) {
        console.error(
          "gBrowser.addProgressListener was " +
            "called with a second argument, " +
            "which is not supported. See bug " +
            "608628. Call stack: ",
          new Error().stack
        );
      }

      this.mProgressListeners.push(aListener);
    }

    removeProgressListener(aListener) {
      this.mProgressListeners = this.mProgressListeners.filter(
        l => l != aListener
      );
    }

    addTabsProgressListener(aListener) {
      this.mTabsProgressListeners.push(aListener);
    }

    removeTabsProgressListener(aListener) {
      this.mTabsProgressListeners = this.mTabsProgressListeners.filter(
        l => l != aListener
      );
    }

    getBrowserForTab(aTab) {
      return aTab.linkedBrowser;
    }

    showTab(aTab) {
      if (!aTab.hidden || aTab == FirefoxViewHandler.tab) {
        return;
      }
      aTab.removeAttribute("hidden");
      this.tabContainer._invalidateCachedVisibleTabs();

      this.tabContainer._updateCloseButtons();
      if (aTab.multiselected) {
        this._updateMultiselectedTabCloseButtonTooltip();
      }

      let event = document.createEvent("Events");
      event.initEvent("TabShow", true, false);
      aTab.dispatchEvent(event);
      SessionStore.deleteCustomTabValue(aTab, "hiddenBy");
    }

    hideTab(aTab, aSource) {
      if (
        aTab.hidden ||
        aTab.pinned ||
        aTab.selected ||
        aTab.closing ||
        // Tabs that are sharing the screen, microphone or camera cannot be hidden.
        aTab._sharingState?.webRTC?.sharing
      ) {
        return;
      }
      aTab.setAttribute("hidden", "true");
      this.tabContainer._invalidateCachedVisibleTabs();

      this.tabContainer._updateCloseButtons();
      if (aTab.multiselected) {
        this._updateMultiselectedTabCloseButtonTooltip();
      }

      // Splice this tab out of any lines of succession before any events are
      // dispatched.
      this.replaceInSuccession(aTab, aTab.successor);
      this.setSuccessor(aTab, null);

      let event = document.createEvent("Events");
      event.initEvent("TabHide", true, false);
      aTab.dispatchEvent(event);
      if (aSource) {
        SessionStore.setCustomTabValue(aTab, "hiddenBy", aSource);
      }
    }

    selectTabAtIndex(aIndex, aEvent) {
      let tabs = this.visibleTabs;

      // count backwards for aIndex < 0
      if (aIndex < 0) {
        aIndex += tabs.length;
        // clamp at index 0 if still negative.
        if (aIndex < 0) {
          aIndex = 0;
        }
      } else if (aIndex >= tabs.length) {
        // clamp at right-most tab if out of range.
        aIndex = tabs.length - 1;
      }

      this.selectedTab = tabs[aIndex];

      if (aEvent) {
        aEvent.preventDefault();
        aEvent.stopPropagation();
      }
    }

    /**
     * Moves a tab to a new browser window, unless it's already the only tab
     * in the current window, in which case this will do nothing.
     */
    replaceTabWithWindow(aTab, aOptions) {
      if (this.tabs.length == 1) {
        return null;
      }

      var options = "chrome,dialog=no,all";
      for (var name in aOptions) {
        options += "," + name + "=" + aOptions[name];
      }

      if (PrivateBrowsingUtils.isWindowPrivate(window)) {
        options += ",private=1";
      }

      // Play the tab closing animation to give immediate feedback while
      // waiting for the new window to appear.
      if (!gReduceMotion) {
        aTab.style.maxWidth = ""; // ensure that fade-out transition happens
        aTab.removeAttribute("fadein");
      }

      // tell a new window to take the "dropped" tab
      return window.openDialog(
        AppConstants.BROWSER_CHROME_URL,
        "_blank",
        options,
        aTab
      );
    }

    /**
     * Move contextTab (or selected tabs in a mutli-select context)
     * to a new browser window, unless it is (they are) already the only tab(s)
     * in the current window, in which case this will do nothing.
     */
    replaceTabsWithWindow(contextTab, aOptions = {}) {
      if (this.isTabGroupLabel(contextTab)) {
        return this.replaceTabWithWindow(contextTab, aOptions);
      }

      let tabs;
      if (contextTab.multiselected) {
        tabs = this.selectedTabs;
      } else {
        tabs = [contextTab];
      }

      if (this.tabs.length == tabs.length) {
        return null;
      }

      if (tabs.length == 1) {
        return this.replaceTabWithWindow(tabs[0], aOptions);
      }

      // Play the closing animation for all selected tabs to give
      // immediate feedback while waiting for the new window to appear.
      if (!gReduceMotion) {
        for (let tab of tabs) {
          tab.style.maxWidth = ""; // ensure that fade-out transition happens
          tab.removeAttribute("fadein");
        }
      }

      // Create a new window and make it adopt the tabs, preserving their relative order.
      // The initial tab of the new window will be selected, so it should adopt the
      // selected tab of the original window, if applicable, or else the first moving tab.
      // This avoids tab-switches in the new window, preserving tab laziness.
      // However, to avoid multiple tab-switches in the original window, the other tabs
      // should be adopted before the selected one.
      let { selectedTab } = gBrowser;
      if (!tabs.includes(selectedTab)) {
        selectedTab = tabs[0];
      }
      let win = this.replaceTabWithWindow(selectedTab, aOptions);
      win.addEventListener(
        "before-initial-tab-adopted",
        () => {
          let tabIndex = 0;
          for (let tab of tabs) {
            if (tab !== selectedTab) {
              const newTab = win.gBrowser.adoptTab(tab, { tabIndex });
              if (!newTab) {
                // The adoption failed. Restore "fadein" and don't increase the index.
                tab.setAttribute("fadein", "true");
                continue;
              }
            }
            ++tabIndex;
          }
          // Restore tab selection
          let winVisibleTabs = win.gBrowser.visibleTabs;
          let winTabLength = winVisibleTabs.length;
          win.gBrowser.addRangeToMultiSelectedTabs(
            winVisibleTabs[0],
            winVisibleTabs[winTabLength - 1]
          );
          win.gBrowser.lockClearMultiSelectionOnce();
        },
        { once: true }
      );
      return win;
    }

    /**
     * Moves group to a new window.
     *
     * @param {MozTabbrowserTabGroup} group
     *   The tab group to move.
     */
    replaceGroupWithWindow(group) {
      // The first tab added to the new window will be selected.
      // If a tab in the group is selected, adopt it first.
      let selectedIndex = group.tabs.indexOf(gBrowser.selectedTab);
      if (selectedIndex < 0) {
        // Otherwise, we'll first adopt the first tab in the group
        selectedIndex = 0;
      }
      let firstTab = group.tabs[selectedIndex];
      let newWindow = this.replaceTabWithWindow(firstTab);

      newWindow.addEventListener(
        "before-initial-tab-adopted",
        () => {
          let tabsToGroup = group.tabs.map((tab, i) => {
            // addtabGroup will handle adopting the other tabs, but we already
            // started adopting the tab at selectedIndex so we need to swap
            // the old tab out for the new one.
            if (i == selectedIndex) {
              return newWindow.gBrowser.visibleTabs[0];
            }
            return tab;
          });
          // The initial tab isn't fully adopted yet, but the tab object has been
          // instantiated, so we can make a group now.
          newWindow.gBrowser.addTabGroup(tabsToGroup, {
            color: group.color,
            label: group.label,
            id: group.id,
          });
          Glean.tabgroup.groupInteractions.move_window.add(1);
        },
        { once: true }
      );
      return newWindow;
    }

    /**
     * @param {Element} element
     * @returns {boolean}
     *   `true` if element is a `<tab>`
     */
    isTab(element) {
      return !!(element?.tagName == "tab");
    }

    /**
     * @param {Element} element
     * @returns {boolean}
     *   `true` if element is a `<tab-group>`
     */
    isTabGroup(element) {
      return !!(element?.tagName == "tab-group");
    }

    /**
     * @param {Element} element
     * @returns {boolean}
     *   `true` if element is the `<label>` in a `<tab-group>`
     */
    isTabGroupLabel(element) {
      return !!element?.classList?.contains("tab-group-label");
    }

    _updateTabsAfterInsert() {
      for (let i = 0; i < this.tabs.length; i++) {
        this.tabs[i]._tPos = i;
        this.tabs[i]._selected = false;
      }

      // If we're in the midst of an async tab switch while calling
      // moveTabTo, we can get into a case where _visuallySelected
      // is set to true on two different tabs.
      //
      // What we want to do in moveTabTo is to remove logical selection
      // from all tabs, and then re-add logical selection to selectedTab
      // (and visual selection as well if we're not running with e10s, which
      // setting _selected will do automatically).
      //
      // If we're running with e10s, then the visual selection will not
      // be changed, which is fine, since if we weren't in the midst of a
      // tab switch, the previously visually selected tab should still be
      // correct, and if we are in the midst of a tab switch, then the async
      // tab switcher will set the visually selected tab once the tab switch
      // has completed.
      this.selectedTab._selected = true;
    }

    /**
     * @param {MozTabbrowserTab|MozTabbrowserTabGroup} element
     *   The tab or tab group to move. Also accepts a tab group label as a
     *   stand-in for its group.
     * @param {object} [options]
     * @param {number} [options.tabIndex]
     *   The desired position, expressed as the index within the `tabs` array.
     * @param {number} [options.elementIndex]
     *   The desired position, expressed as the index within the
     *   `MozTabbrowserTabs::ariaFocusableItems` array.
     * @param {boolean} [options.forceUngrouped=false]
     *   Force `element` to move into position as a standalone tab, overriding
     *   any possibility of entering a tab group. For example, setting `true`
     *   ensures that a pinned tab will not accidentally be placed inside of
     *   a tab group, since pinned tabs are presently not allowed in tab groups.
     * @property {boolean} [options.isUserTriggered=false]
     *   Should be true if there was an explicit action/request from the user
     *   (as opposed to some action being taken internally or for technical
     *   bookkeeping reasons alone) to move the tab. This causes telemetry
     *   events to fire.
     * @property {string} [options.telemetrySource="unknown"]
     *   The system, surface, or control the user used to move the tab.
     *   @see TabMetrics.METRIC_SOURCE for possible values.
     *   Defaults to "unknown".
     */
    moveTabTo(
      element,
      {
        elementIndex,
        tabIndex,
        forceUngrouped = false,
        isUserTriggered = false,
        telemetrySource = this.TabMetrics.METRIC_SOURCE.UNKNOWN,
      } = {}
    ) {
      if (typeof elementIndex == "number") {
        tabIndex = this.#elementIndexToTabIndex(elementIndex);
      }

      // Don't allow mixing pinned and unpinned tabs.
      if (this.isTab(element) && element.pinned) {
        tabIndex = Math.min(tabIndex, this.pinnedTabCount - 1);
      } else {
        tabIndex = Math.max(tabIndex, this.pinnedTabCount);
      }

      // Return early if the tab is already in the right spot.
      if (
        this.isTab(element) &&
        element._tPos == tabIndex &&
        !(element.group && forceUngrouped)
      ) {
        return;
      }

      // When asked to move a tab group label, we need to move the whole group
      // instead.
      if (this.isTabGroupLabel(element)) {
        element = element.group;
      }
      if (this.isTabGroup(element)) {
        forceUngrouped = true;
      }

      this.#handleTabMove(
        element,
        () => {
          let neighbor = this.tabs[tabIndex];
          if (forceUngrouped && neighbor.group) {
            neighbor = neighbor.group;
          }
          if (neighbor && this.isTab(element) && tabIndex > element._tPos) {
            neighbor.after(element);
          } else {
            this.tabContainer.insertBefore(element, neighbor);
          }
        },
        { isUserTriggered, telemetrySource }
      );
    }

    /**
     * @param {MozTabbrowserTab|MozTabbrowserTabGroup} element
     * @param {MozTabbrowserTab|MozTabbrowserTabGroup} targetElement
     * @param {TabMetricsContext} [metricsContext]
     */
    moveTabBefore(element, targetElement, metricsContext) {
      this.#moveTabNextTo(element, targetElement, true, metricsContext);
    }

    /**
     * @param {MozTabbrowserTab|MozTabbrowserTabGroup[]} elements
     * @param {MozTabbrowserTab|MozTabbrowserTabGroup} targetElement
     * @param {TabMetricsContext} [metricsContext]
     */
    moveTabsBefore(elements, targetElement, metricsContext) {
      this.#moveTabsNextTo(elements, targetElement, true, metricsContext);
    }

    /**
     * @param {MozTabbrowserTab|MozTabbrowserTabGroup} element
     * @param {MozTabbrowserTab|MozTabbrowserTabGroup} targetElement
     * @param {TabMetricsContext} [metricsContext]
     */
    moveTabAfter(element, targetElement, metricsContext) {
      this.#moveTabNextTo(element, targetElement, false, metricsContext);
    }

    /**
     * @param {MozTabbrowserTab|MozTabbrowserTabGroup[]} elements
     * @param {MozTabbrowserTab|MozTabbrowserTabGroup} targetElement
     * @param {TabMetricsContext} [metricsContext]
     */
    moveTabsAfter(elements, targetElement, metricsContext) {
      this.#moveTabsNextTo(elements, targetElement, false, metricsContext);
    }

    /**
     * @param {MozTabbrowserTab|MozTabbrowserTabGroup} element
     *   The tab or tab group to move. Also accepts a tab group label as a
     *   stand-in for its group.
     * @param {MozTabbrowserTab|MozTabbrowserTabGroup} targetElement
     * @param {boolean} [moveBefore=false]
     * @param {TabMetricsContext} [metricsContext]
     */
    #moveTabNextTo(element, targetElement, moveBefore = false, metricsContext) {
      if (this.isTabGroupLabel(targetElement)) {
        targetElement = targetElement.group;
        if (!moveBefore) {
          targetElement = targetElement.tabs[0];
          moveBefore = true;
        }
      }
      if (this.isTabGroupLabel(element)) {
        element = element.group;
        if (targetElement?.group) {
          targetElement = targetElement.group;
        }
      }

      // Don't allow mixing pinned and unpinned tabs.
      if (element.pinned && !targetElement?.pinned) {
        targetElement = this.tabs[this.pinnedTabCount - 1];
        moveBefore = false;
      } else if (!element.pinned && targetElement && targetElement.pinned) {
        targetElement = this.tabs[this.pinnedTabCount];
        moveBefore = true;
      }

      let getContainer = () => {
        if (element.pinned && this.tabContainer.verticalMode) {
          return this.tabContainer.verticalPinnedTabsContainer;
        }
        return this.tabContainer;
      };

      this.#handleTabMove(
        element,
        () => {
          if (moveBefore) {
            getContainer().insertBefore(element, targetElement);
          } else if (targetElement) {
            targetElement.after(element);
          } else {
            getContainer().appendChild(element);
          }
        },
        metricsContext
      );
    }

    /**
     * @param {MozTabbrowserTab[]} elements
     * @param {MozTabbrowserTab|MozTabbrowserTabGroup} targetElement
     * @param {boolean} [moveBefore=false]
     * @param {TabMetricsContext} [metricsContext]
     */
    #moveTabsNextTo(
      elements,
      targetElement,
      moveBefore = false,
      metricsContext
    ) {
      this.#moveTabNextTo(
        elements[0],
        targetElement,
        moveBefore,
        metricsContext
      );
      for (let i = 1; i < elements.length; i++) {
        this.#moveTabNextTo(
          elements[i],
          elements[i - 1],
          false,
          metricsContext
        );
      }
    }

    /**
     *
     * @param {MozTabbrowserTab} aTab
     * @param {MozTabbrowserTabGroup} aGroup
     * @param {TabMetricsContext} [metricsContext]
     */
    moveTabToGroup(aTab, aGroup, metricsContext) {
      if (!this.isTab(aTab)) {
        throw new Error("Can only move a tab into a tab group");
      }
      if (aTab.pinned) {
        return;
      }
      if (aTab.group && aTab.group.id === aGroup.id) {
        return;
      }

      aGroup.collapsed = false;
      this.#handleTabMove(aTab, () => aGroup.appendChild(aTab), metricsContext);
      this.removeFromMultiSelectedTabs(aTab);
      this.tabContainer._notifyBackgroundTab(aTab);
    }

    /**
     * @typedef {object} TabMoveState
     * @property {number} tabIndex
     * @property {number} [elementIndex]
     * @property {string} [tabGroupId]
     */

    /**
     * @param {MozTabbrowserTab} tab
     * @returns {TabMoveState|undefined}
     */
    #getTabMoveState(tab) {
      if (!this.isTab(tab)) {
        return undefined;
      }

      let state = {
        tabIndex: tab._tPos,
      };
      if (tab.visible) {
        state.elementIndex = tab.elementIndex;
      }
      if (tab.group) {
        state.tabGroupId = tab.group.id;
      }
      return state;
    }

    /**
     * @param {MozTabbrowserTab} tab
     * @param {TabMoveState} [previousTabState]
     * @param {TabMoveState} [currentTabState]
     * @param {TabMetricsContext} [metricsContext]
     */
    #notifyOnTabMove(tab, previousTabState, currentTabState, metricsContext) {
      if (!this.isTab(tab) || !previousTabState || !currentTabState) {
        return;
      }

      let changedPosition =
        previousTabState.tabIndex != currentTabState.tabIndex;
      let changedTabGroup =
        previousTabState.tabGroupId != currentTabState.tabGroupId;

      if (changedPosition || changedTabGroup) {
        tab.dispatchEvent(
          new CustomEvent("TabMove", {
            bubbles: true,
            detail: {
              previousTabState,
              currentTabState,
              isUserTriggered: metricsContext?.isUserTriggered ?? false,
              telemetrySource:
                metricsContext?.telemetrySource ??
                this.TabMetrics.METRIC_SOURCE.UNKNOWN,
            },
          })
        );
      }
    }

    /**
     * @param {MozTabbrowserTab|MozTabbrowserTabGroup} element
     * @param {function():void} moveActionCallback
     * @param {TabMetricsContext} [metricsContext]
     */
    #handleTabMove(element, moveActionCallback, metricsContext) {
      let tabs;
      if (this.isTab(element)) {
        tabs = [element];
      } else if (this.isTabGroup(element)) {
        tabs = element.tabs;
      } else {
        throw new Error("Can only move a tab or tab group within the tab bar");
      }

      let wasFocused = document.activeElement == this.selectedTab;
      let previousTabStates = tabs.map(tab => this.#getTabMoveState(tab));

      moveActionCallback();

      // Clear tabs cache after moving nodes because the order of tabs may have
      // changed.
      this.tabContainer._invalidateCachedTabs();
      this._lastRelatedTabMap = new WeakMap();
      this._updateTabsAfterInsert();

      if (wasFocused) {
        this.selectedTab.focus();
      }

      for (let i = 0; i < tabs.length; i++) {
        let tab = tabs[i];
        if (tab.selected) {
          this.tabContainer._handleTabSelect(true);
        }
        if (tab.pinned) {
          this.tabContainer._positionPinnedTabs();
        }

        let currentTabState = this.#getTabMoveState(tab);
        this.#notifyOnTabMove(
          tab,
          previousTabStates[i],
          currentTabState,
          metricsContext
        );
      }

      let currentFirst = this.#getTabMoveState(tabs[0]);
      if (
        this.isTabGroup(element) &&
        previousTabStates[0].tabIndex != currentFirst.tabIndex
      ) {
        let event = new CustomEvent("TabGroupMoved", { bubbles: true });
        element.dispatchEvent(event);
      }
    }

    /**
     * Adopts a tab from another browser window, and inserts it at the given index.
     *
     * @returns {object}
     *    The new tab in the current window, null if the tab couldn't be adopted.
     */
    adoptTab(aTab, { elementIndex, tabIndex, selectTab = false } = {}) {
      // Swap the dropped tab with a new one we create and then close
      // it in the other window (making it seem to have moved between
      // windows). We also ensure that the tab we create to swap into has
      // the same remote type and process as the one we're swapping in.
      // This makes sure we don't get a short-lived process for the new tab.
      if (typeof tabIndex == "number") {
        elementIndex = this.#tabIndexToElementIndex(tabIndex);
      }
      let linkedBrowser = aTab.linkedBrowser;
      let createLazyBrowser = !aTab.linkedPanel;
      let nextElement = this.tabContainer.ariaFocusableItems.at(elementIndex);
      let tabInGroup = !!aTab.group;
      let params = {
        eventDetail: { adoptedTab: aTab },
        preferredRemoteType: linkedBrowser.remoteType,
        initialBrowsingContextGroupId: linkedBrowser.browsingContext?.group.id,
        skipAnimation: true,
        elementIndex,
        tabGroup: this.isTab(nextElement) && nextElement.group,
        createLazyBrowser,
      };

      let numPinned = this.pinnedTabCount;
      if (
        elementIndex < numPinned ||
        (aTab.pinned && elementIndex == numPinned)
      ) {
        params.pinned = true;
      }

      if (aTab.hasAttribute("usercontextid")) {
        // new tab must have the same usercontextid as the old one
        params.userContextId = aTab.getAttribute("usercontextid");
      }
      let newTab = this.addWebTab("about:blank", params);
      let newBrowser = this.getBrowserForTab(newTab);

      aTab.container.finishAnimateTabMove();

      if (!createLazyBrowser) {
        // Stop the about:blank load.
        newBrowser.stop();
      }

      if (!this.swapBrowsersAndCloseOther(newTab, aTab)) {
        // Swapping wasn't permitted. Bail out.
        this.removeTab(newTab);
        return null;
      }

      if (selectTab) {
        this.selectedTab = newTab;
      }

      if (tabInGroup) {
        Glean.tabgroup.tabInteractions.remove_other_window.add();
      }

      return newTab;
    }

    moveTabForward() {
      let { selectedTab } = this;
      let nextTab = this.tabContainer.findNextTab(selectedTab, {
        direction: DIRECTION_FORWARD,
        filter: tab => !tab.hidden && selectedTab.pinned == tab.pinned,
      });
      if (nextTab) {
        this.#handleTabMove(selectedTab, () => {
          if (!selectedTab.group && nextTab.group) {
            if (nextTab.group.collapsed) {
              // Skip over collapsed tab group.
              nextTab.group.after(selectedTab);
            } else {
              // Enter first position of tab group.
              nextTab.group.insertBefore(selectedTab, nextTab);
            }
          } else if (selectedTab.group != nextTab.group) {
            // Standalone tab after tab group.
            selectedTab.group.after(selectedTab);
          } else {
            nextTab.after(selectedTab);
          }
        });
      } else if (selectedTab.group) {
        // selectedTab is the last tab and is grouped.
        // remove it from its group.
        selectedTab.group.after(selectedTab);
      }
    }

    moveTabBackward() {
      let { selectedTab } = this;

      let previousTab = this.tabContainer.findNextTab(selectedTab, {
        direction: DIRECTION_BACKWARD,
        filter: tab => !tab.hidden && selectedTab.pinned == tab.pinned,
      });

      if (previousTab) {
        this.#handleTabMove(selectedTab, () => {
          if (!selectedTab.group && previousTab.group) {
            if (previousTab.group.collapsed) {
              // Skip over collapsed tab group.
              previousTab.group.before(selectedTab);
            } else {
              // Enter last position of tab group.
              previousTab.group.append(selectedTab);
            }
          } else if (selectedTab.group != previousTab.group) {
            // Standalone tab before tab group.
            selectedTab.group.before(selectedTab);
          } else {
            previousTab.before(selectedTab);
          }
        });
      } else if (selectedTab.group) {
        // selectedTab is the first tab and is grouped.
        // remove it from its group.
        selectedTab.group.before(selectedTab);
      }
    }

    moveTabToStart(aTab = this.selectedTab) {
      this.moveTabTo(aTab, { tabIndex: 0, forceUngrouped: true });
    }

    moveTabToEnd(aTab = this.selectedTab) {
      this.moveTabTo(aTab, {
        tabIndex: this.tabs.length - 1,
        forceUngrouped: true,
      });
    }

    /**
     * @param   aTab
     *          Can be from a different window as well
     * @param   aRestoreTabImmediately
     *          Can defer loading of the tab contents
     * @param   aOptions
     *          The new index of the tab
     */
    duplicateTab(aTab, aRestoreTabImmediately, aOptions) {
      let newTab = SessionStore.duplicateTab(
        window,
        aTab,
        0,
        aRestoreTabImmediately,
        aOptions
      );
      if (aTab.group) {
        Glean.tabgroup.tabInteractions.duplicate.add();
      }
      return newTab;
    }

    /**
     * Update accessible names of close buttons in the (multi) selected tabs
     * collection with how many tabs they will close
     */
    _updateMultiselectedTabCloseButtonTooltip() {
      const tabCount = gBrowser.selectedTabs.length;
      gBrowser.selectedTabs.forEach(selectedTab => {
        document.l10n.setArgs(selectedTab.querySelector(".tab-close-button"), {
          tabCount,
        });
      });
    }

    addToMultiSelectedTabs(aTab) {
      if (aTab.multiselected) {
        return;
      }

      aTab.setAttribute("multiselected", "true");
      aTab.setAttribute("aria-selected", "true");
      this._multiSelectedTabsSet.add(aTab);
      this._startMultiSelectChange();
      if (this._multiSelectChangeRemovals.has(aTab)) {
        this._multiSelectChangeRemovals.delete(aTab);
      } else {
        this._multiSelectChangeAdditions.add(aTab);
      }

      this._updateMultiselectedTabCloseButtonTooltip();
    }

    /**
     * Adds two given tabs and all tabs between them into the (multi) selected tabs collection
     */
    addRangeToMultiSelectedTabs(aTab1, aTab2) {
      if (aTab1 == aTab2) {
        return;
      }

      const tabs = this.visibleTabs;
      const indexOfTab1 = tabs.indexOf(aTab1);
      const indexOfTab2 = tabs.indexOf(aTab2);

      const [lowerIndex, higherIndex] =
        indexOfTab1 < indexOfTab2
          ? [Math.max(0, indexOfTab1), indexOfTab2]
          : [Math.max(0, indexOfTab2), indexOfTab1];

      for (let i = lowerIndex; i <= higherIndex; i++) {
        this.addToMultiSelectedTabs(tabs[i]);
      }

      this._updateMultiselectedTabCloseButtonTooltip();
    }

    removeFromMultiSelectedTabs(aTab) {
      if (!aTab.multiselected) {
        return;
      }
      aTab.removeAttribute("multiselected");
      aTab.removeAttribute("aria-selected");
      this._multiSelectedTabsSet.delete(aTab);
      this._startMultiSelectChange();
      if (this._multiSelectChangeAdditions.has(aTab)) {
        this._multiSelectChangeAdditions.delete(aTab);
      } else {
        this._multiSelectChangeRemovals.add(aTab);
      }
      // Update labels for Close buttons of the remaining multiselected tabs:
      this._updateMultiselectedTabCloseButtonTooltip();
      // Update the label for the Close button of the tab being removed
      // from the multiselection:
      document.l10n.setArgs(aTab.querySelector(".tab-close-button"), {
        tabCount: 1,
      });
    }

    clearMultiSelectedTabs() {
      if (this._clearMultiSelectionLocked) {
        if (this._clearMultiSelectionLockedOnce) {
          this._clearMultiSelectionLockedOnce = false;
          this._clearMultiSelectionLocked = false;
        }
        return;
      }

      if (this.multiSelectedTabsCount < 1) {
        return;
      }

      for (let tab of this.selectedTabs) {
        this.removeFromMultiSelectedTabs(tab);
      }
      this._lastMultiSelectedTabRef = null;
    }

    selectAllTabs() {
      let visibleTabs = this.visibleTabs;
      gBrowser.addRangeToMultiSelectedTabs(
        visibleTabs[0],
        visibleTabs[visibleTabs.length - 1]
      );
    }

    allTabsSelected() {
      return (
        this.visibleTabs.length == 1 ||
        this.visibleTabs.every(t => t.multiselected)
      );
    }

    lockClearMultiSelectionOnce() {
      this._clearMultiSelectionLockedOnce = true;
      this._clearMultiSelectionLocked = true;
    }

    unlockClearMultiSelection() {
      this._clearMultiSelectionLockedOnce = false;
      this._clearMultiSelectionLocked = false;
    }

    /**
     * Remove a tab from the multiselection if it's the only one left there.
     *
     * In fact, some scenario may lead to only one single tab multi-selected,
     * this is something to avoid (Chrome does the same)
     * Consider 4 tabs A,B,C,D with A having the focus
     * 1. select C with Ctrl
     * 2. Right-click on B and "Close Tabs to The Right"
     *
     * Expected result
     * C and D closing
     * A being the only multi-selected tab, selection should be cleared
     *
     *
     * Single selected tab could even happen with a none-focused tab.
     * For exemple with the menu "Close other tabs", it could happen
     * with a multi-selected pinned tab.
     * For illustration, consider 4 tabs A,B,C,D with B active
     * 1. pin A and Ctrl-select it
     * 2. Ctrl-select C
     * 3. right-click on D and click "Close Other Tabs"
     *
     * Expected result
     * B and C closing
     * A[pinned] being the only multi-selected tab, selection should be cleared.
     */
    _avoidSingleSelectedTab() {
      if (this.multiSelectedTabsCount == 1) {
        this.clearMultiSelectedTabs();
      }
    }

    _switchToNextMultiSelectedTab() {
      this._clearMultiSelectionLocked = true;

      // Guarantee that _clearMultiSelectionLocked lock gets released.
      try {
        let lastMultiSelectedTab = this.lastMultiSelectedTab;
        if (!lastMultiSelectedTab.selected) {
          this.selectedTab = lastMultiSelectedTab;
        } else {
          let selectedTabs = ChromeUtils.nondeterministicGetWeakSetKeys(
            this._multiSelectedTabsSet
          ).filter(this._mayTabBeMultiselected);
          this.selectedTab = selectedTabs.at(-1);
        }
      } catch (e) {
        console.error(e);
      }

      this._clearMultiSelectionLocked = false;
    }

    set selectedTabs(tabs) {
      this.clearMultiSelectedTabs();
      this.selectedTab = tabs[0];
      if (tabs.length > 1) {
        for (let tab of tabs) {
          this.addToMultiSelectedTabs(tab);
        }
      }
    }

    get selectedTabs() {
      let { selectedTab, _multiSelectedTabsSet } = this;
      let tabs = ChromeUtils.nondeterministicGetWeakSetKeys(
        _multiSelectedTabsSet
      ).filter(this._mayTabBeMultiselected);
      if (
        (!_multiSelectedTabsSet.has(selectedTab) &&
          this._mayTabBeMultiselected(selectedTab)) ||
        !tabs.length
      ) {
        tabs.push(selectedTab);
      }
      return tabs.sort((a, b) => a._tPos > b._tPos);
    }

    get multiSelectedTabsCount() {
      return ChromeUtils.nondeterministicGetWeakSetKeys(
        this._multiSelectedTabsSet
      ).filter(this._mayTabBeMultiselected).length;
    }

    get lastMultiSelectedTab() {
      let tab = this._lastMultiSelectedTabRef
        ? this._lastMultiSelectedTabRef.get()
        : null;
      if (tab && tab.isConnected && this._multiSelectedTabsSet.has(tab)) {
        return tab;
      }
      let selectedTab = this.selectedTab;
      this.lastMultiSelectedTab = selectedTab;
      return selectedTab;
    }

    set lastMultiSelectedTab(aTab) {
      this._lastMultiSelectedTabRef = Cu.getWeakReference(aTab);
    }

    _mayTabBeMultiselected(aTab) {
      return aTab.visible;
    }

    _startMultiSelectChange() {
      if (!this._multiSelectChangeStarted) {
        this._multiSelectChangeStarted = true;
        Promise.resolve().then(() => this._endMultiSelectChange());
      }
    }

    _endMultiSelectChange() {
      let noticeable = false;
      let { selectedTab } = this;
      if (this._multiSelectChangeAdditions.size) {
        if (!selectedTab.multiselected) {
          this.addToMultiSelectedTabs(selectedTab);
        }
        noticeable = true;
      }
      if (this._multiSelectChangeRemovals.size) {
        if (this._multiSelectChangeRemovals.has(selectedTab)) {
          this._switchToNextMultiSelectedTab();
        }
        this._avoidSingleSelectedTab();
        noticeable = true;
      }
      this._multiSelectChangeStarted = false;
      if (noticeable || this._multiSelectChangeSelected) {
        this._multiSelectChangeSelected = false;
        this._multiSelectChangeAdditions.clear();
        this._multiSelectChangeRemovals.clear();
        this.dispatchEvent(
          new CustomEvent("TabMultiSelect", { bubbles: true })
        );
      }
    }

    toggleMuteAudioOnMultiSelectedTabs(aTab) {
      let tabMuted = aTab.linkedBrowser.audioMuted;
      let tabsToToggle = this.selectedTabs.filter(
        tab => tab.linkedBrowser.audioMuted == tabMuted
      );
      for (let tab of tabsToToggle) {
        tab.toggleMuteAudio();
      }
    }

    resumeDelayedMediaOnMultiSelectedTabs() {
      for (let tab of this.selectedTabs) {
        tab.resumeDelayedMedia();
      }
    }

    pinMultiSelectedTabs() {
      for (let tab of this.selectedTabs) {
        this.pinTab(tab);
      }
    }

    unpinMultiSelectedTabs() {
      // The selectedTabs getter returns the tabs
      // in visual order. We need to unpin in reverse
      // order to maintain visual order.
      let selectedTabs = this.selectedTabs;
      for (let i = selectedTabs.length - 1; i >= 0; i--) {
        let tab = selectedTabs[i];
        this.unpinTab(tab);
      }
    }

    activateBrowserForPrintPreview(aBrowser) {
      this._printPreviewBrowsers.add(aBrowser);
      if (this._switcher) {
        this._switcher.activateBrowserForPrintPreview(aBrowser);
      }
      aBrowser.docShellIsActive = true;
    }

    deactivatePrintPreviewBrowsers() {
      let browsers = this._printPreviewBrowsers;
      this._printPreviewBrowsers = new Set();
      for (let browser of browsers) {
        browser.docShellIsActive = this.shouldActivateDocShell(browser);
      }
    }

    /**
     * Returns true if a given browser's docshell should be active.
     */
    shouldActivateDocShell(aBrowser) {
      if (this._switcher) {
        return this._switcher.shouldActivateDocShell(aBrowser);
      }
      return (
        (aBrowser == this.selectedBrowser && !document.hidden) ||
        this._printPreviewBrowsers.has(aBrowser) ||
        this.PictureInPicture.isOriginatingBrowser(aBrowser)
      );
    }

    _getSwitcher() {
      if (!this._switcher) {
        this._switcher = new this.AsyncTabSwitcher(this);
      }
      return this._switcher;
    }

    warmupTab(aTab) {
      if (gMultiProcessBrowser) {
        this._getSwitcher().warmupTab(aTab);
      }
    }

    /**
     * _maybeRequestReplyFromRemoteContent may call
     * aEvent.requestReplyFromRemoteContent if necessary.
     *
     * @param aEvent    The handling event.
     * @return          true if the handler should wait a reply event.
     *                  false if the handle can handle the immediately.
     */
    _maybeRequestReplyFromRemoteContent(aEvent) {
      if (aEvent.defaultPrevented) {
        return false;
      }
      // If the event target is a remote browser, and the event has not been
      // handled by the remote content yet, we should wait a reply event
      // from the content.
      if (aEvent.isWaitingReplyFromRemoteContent) {
        return true; // Somebody called requestReplyFromRemoteContent already.
      }
      if (
        !aEvent.isReplyEventFromRemoteContent &&
        aEvent.target?.isRemoteBrowser === true
      ) {
        aEvent.requestReplyFromRemoteContent();
        return true;
      }
      return false;
    }

    _handleKeyDownEvent(aEvent) {
      if (!aEvent.isTrusted) {
        // Don't let untrusted events mess with tabs.
        return;
      }

      // Skip this only if something has explicitly cancelled it.
      if (aEvent.defaultCancelled) {
        return;
      }

      // Skip if chrome code has cancelled this:
      if (aEvent.defaultPreventedByChrome) {
        return;
      }

      // Don't check if the event was already consumed because tab
      // navigation should always work for better user experience.

      switch (ShortcutUtils.getSystemActionForEvent(aEvent)) {
        case ShortcutUtils.TOGGLE_CARET_BROWSING:
          this._maybeRequestReplyFromRemoteContent(aEvent);
          return;
        case ShortcutUtils.MOVE_TAB_BACKWARD:
          this.moveTabBackward();
          aEvent.preventDefault();
          return;
        case ShortcutUtils.MOVE_TAB_FORWARD:
          this.moveTabForward();
          aEvent.preventDefault();
          return;
        case ShortcutUtils.CLOSE_TAB:
          if (gBrowser.multiSelectedTabsCount) {
            gBrowser.removeMultiSelectedTabs();
          } else if (!this.selectedTab.pinned) {
            this.removeCurrentTab({ animate: true });
          }
          aEvent.preventDefault();
      }
    }

    toggleCaretBrowsing() {
      const kPrefShortcutEnabled =
        "accessibility.browsewithcaret_shortcut.enabled";
      const kPrefWarnOnEnable = "accessibility.warn_on_browsewithcaret";
      const kPrefCaretBrowsingOn = "accessibility.browsewithcaret";

      var isEnabled = Services.prefs.getBoolPref(kPrefShortcutEnabled);
      if (!isEnabled || this._awaitingToggleCaretBrowsingPrompt) {
        return;
      }

      // Toggle browse with caret mode
      var browseWithCaretOn = Services.prefs.getBoolPref(
        kPrefCaretBrowsingOn,
        false
      );
      var warn = Services.prefs.getBoolPref(kPrefWarnOnEnable, true);
      if (warn && !browseWithCaretOn) {
        var checkValue = { value: false };
        var promptService = Services.prompt;

        try {
          this._awaitingToggleCaretBrowsingPrompt = true;
          const [title, message, checkbox] =
            this.tabLocalization.formatValuesSync([
              "tabbrowser-confirm-caretbrowsing-title",
              "tabbrowser-confirm-caretbrowsing-message",
              "tabbrowser-confirm-caretbrowsing-checkbox",
            ]);
          var buttonPressed = promptService.confirmEx(
            window,
            title,
            message,
            // Make "No" the default:
            promptService.STD_YES_NO_BUTTONS |
              promptService.BUTTON_POS_1_DEFAULT,
            null,
            null,
            null,
            checkbox,
            checkValue
          );
        } catch (ex) {
          return;
        } finally {
          this._awaitingToggleCaretBrowsingPrompt = false;
        }
        if (buttonPressed != 0) {
          if (checkValue.value) {
            try {
              Services.prefs.setBoolPref(kPrefShortcutEnabled, false);
            } catch (ex) {}
          }
          return;
        }
        if (checkValue.value) {
          try {
            Services.prefs.setBoolPref(kPrefWarnOnEnable, false);
          } catch (ex) {}
        }
      }

      // Toggle the pref
      try {
        Services.prefs.setBoolPref(kPrefCaretBrowsingOn, !browseWithCaretOn);
      } catch (ex) {}
    }

    _handleKeyPressEvent(aEvent) {
      if (!aEvent.isTrusted) {
        // Don't let untrusted events mess with tabs.
        return;
      }

      // Skip this only if something has explicitly cancelled it.
      if (aEvent.defaultCancelled) {
        return;
      }

      // Skip if chrome code has cancelled this:
      if (aEvent.defaultPreventedByChrome) {
        return;
      }

      switch (ShortcutUtils.getSystemActionForEvent(aEvent, { rtl: RTL_UI })) {
        case ShortcutUtils.TOGGLE_CARET_BROWSING:
          if (
            aEvent.defaultPrevented ||
            this._maybeRequestReplyFromRemoteContent(aEvent)
          ) {
            break;
          }
          this.toggleCaretBrowsing();
          break;

        case ShortcutUtils.NEXT_TAB:
          if (AppConstants.platform == "macosx") {
            this.tabContainer.advanceSelectedTab(DIRECTION_FORWARD, true);
            aEvent.preventDefault();
          }
          break;
        case ShortcutUtils.PREVIOUS_TAB:
          if (AppConstants.platform == "macosx") {
            this.tabContainer.advanceSelectedTab(DIRECTION_BACKWARD, true);
            aEvent.preventDefault();
          }
          break;
      }
    }

    /**
     *
     * @param {MozTabbrowserTab} tab
     */
    #isFirstOrLastInTabGroup(tab) {
      if (tab.group) {
        let groupTabs = tab.group.tabs;
        if (groupTabs.at(0) == tab || groupTabs.at(-1) == tab) {
          return true;
        }
      }
      return false;
    }

    getTabPids(tab) {
      if (!tab.linkedBrowser) {
        return [];
      }

      // Get the PIDs of the content process and remote subframe processes
      let [contentPid, ...framePids] = E10SUtils.getBrowserPids(
        tab.linkedBrowser,
        gFissionBrowser
      );
      let pids = contentPid ? [contentPid] : [];
      return pids.concat(framePids.sort());
    }

    /**
     * @param {MozTabbrowserTab} tab
     * @param {boolean} [includeLabel=true]
     *   Include the tab's title/full label in the tooltip. Defaults to true,
     *   Can be disabled for contexts where including the title in the tooltip
     *   string would be duplicative would already available information,
     *   e.g. accessibility descriptions.
     * @returns {string}
     */
    getTabTooltip(tab, includeLabel = true) {
      let labelArray = [];
      if (includeLabel) {
        labelArray.push(tab._fullLabel || tab.getAttribute("label"));
      }
      if (this.showPidAndActiveness) {
        const pids = this.getTabPids(tab);
        let debugStringArray = [];
        if (pids.length) {
          let pidLabel = pids.length > 1 ? "pids" : "pid";
          debugStringArray.push(`(${pidLabel} ${pids.join(", ")})`);
        }

        if (tab.linkedBrowser.docShellIsActive) {
          debugStringArray.push("[A]");
        }

        if (debugStringArray.length) {
          labelArray.push(debugStringArray.join(" "));
        }
      }

      // Add a line to the tooltip with additional tab context (e.g. container
      // membership, tab group membership) when applicable.
      let containerName = tab.userContextId
        ? ContextualIdentityService.getUserContextLabel(tab.userContextId)
        : "";
      let tabGroupName = this.#isFirstOrLastInTabGroup(tab)
        ? tab.group.name ||
          this.tabLocalization.formatValueSync("tab-group-name-default")
        : "";

      if (containerName || tabGroupName) {
        let tabContextString;
        if (containerName && tabGroupName) {
          tabContextString = this.tabLocalization.formatValueSync(
            "tabbrowser-tab-tooltip-tab-group-container",
            {
              tabGroupName,
              containerName,
            }
          );
        } else if (tabGroupName) {
          tabContextString = this.tabLocalization.formatValueSync(
            "tabbrowser-tab-tooltip-tab-group",
            {
              tabGroupName,
            }
          );
        } else {
          tabContextString = this.tabLocalization.formatValueSync(
            "tabbrowser-tab-tooltip-container",
            {
              containerName,
            }
          );
        }
        labelArray.push(tabContextString);
      }

      if (tab.soundPlaying) {
        let audioPlayingString = this.tabLocalization.formatValueSync(
          "tabbrowser-tab-audio-playing-description"
        );
        labelArray.push(audioPlayingString);
      }
      return labelArray.join("\n");
    }

    createTooltip(event) {
      event.stopPropagation();
      let tab = event.target.triggerNode?.closest("tab");
      if (!tab) {
        if (event.target.triggerNode?.getRootNode()?.host?.closest("tab")) {
          // Check if triggerNode is within shadowRoot of moz-button
          tab = event.target.triggerNode?.getRootNode().host.closest("tab");
        } else {
          event.preventDefault();
          return;
        }
      }

      const tooltip = event.target;
      tooltip.removeAttribute("data-l10n-id");

      const tabCount = this.selectedTabs.includes(tab)
        ? this.selectedTabs.length
        : 1;
      if (tab._overPlayingIcon || tab._overAudioButton) {
        let l10nId;
        const l10nArgs = { tabCount };
        if (tab.selected) {
          l10nId = tab.linkedBrowser.audioMuted
            ? "tabbrowser-unmute-tab-audio-tooltip"
            : "tabbrowser-mute-tab-audio-tooltip";
          const keyElem = document.getElementById("key_toggleMute");
          l10nArgs.shortcut = ShortcutUtils.prettifyShortcut(keyElem);
        } else if (tab.hasAttribute("activemedia-blocked")) {
          l10nId = "tabbrowser-unblock-tab-audio-tooltip";
        } else {
          l10nId = tab.linkedBrowser.audioMuted
            ? "tabbrowser-unmute-tab-audio-background-tooltip"
            : "tabbrowser-mute-tab-audio-background-tooltip";
        }
        tooltip.label = "";
        document.l10n.setAttributes(tooltip, l10nId, l10nArgs);
      } else {
        // Prevent the tooltip from appearing if card preview is enabled, but
        // only if the user is not hovering over the media play icon or the
        // close button
        if (this._showTabCardPreview) {
          event.preventDefault();
          return;
        }
        tooltip.label = this.getTabTooltip(tab, true);
      }
    }

    handleEvent(aEvent) {
      switch (aEvent.type) {
        case "keydown":
          this._handleKeyDownEvent(aEvent);
          break;
        case "keypress":
          this._handleKeyPressEvent(aEvent);
          break;
        case "framefocusrequested": {
          let tab = this.getTabForBrowser(aEvent.target);
          if (!tab || tab == this.selectedTab) {
            // Let the focus manager try to do its thing by not calling
            // preventDefault(). It will still raise the window if appropriate.
            break;
          }
          this.selectedTab = tab;
          window.focus();
          aEvent.preventDefault();
          break;
        }
        case "visibilitychange": {
          const inactive = document.hidden;
          if (!this._switcher) {
            this.selectedBrowser.preserveLayers(inactive);
            this.selectedBrowser.docShellIsActive = !inactive;
          }
          break;
        }
        case "TabGroupCreate":
          if (aEvent.detail.isUserTriggered) {
            this.tabGroupMenu.openCreateModal(aEvent.target);
          }
          break;
        case "activate":
        // Intentional fallthrough
        case "deactivate":
          this.selectedTab.updateLastSeenActive();
          break;
      }
    }

    observe(aSubject, aTopic) {
      switch (aTopic) {
        case "contextual-identity-updated": {
          let identity = aSubject.wrappedJSObject;
          for (let tab of this.tabs) {
            if (tab.getAttribute("usercontextid") == identity.userContextId) {
              ContextualIdentityService.setTabStyle(tab);
            }
          }
          break;
        }
      }
    }

    refreshBlocked(actor, browser, data) {
      // The data object is expected to contain the following properties:
      //  - URI (string)
      //     The URI that a page is attempting to refresh or redirect to.
      //  - delay (int)
      //     The delay (in milliseconds) before the page was going to
      //     reload or redirect.
      //  - sameURI (bool)
      //     true if we're refreshing the page. false if we're redirecting.

      let notificationBox = this.getNotificationBox(browser);
      let notification =
        notificationBox.getNotificationWithValue("refresh-blocked");

      let l10nId = data.sameURI
        ? "refresh-blocked-refresh-label"
        : "refresh-blocked-redirect-label";
      if (notification) {
        notification.label = { "l10n-id": l10nId };
      } else {
        const buttons = [
          {
            "l10n-id": "refresh-blocked-allow",
            callback() {
              actor.sendAsyncMessage("RefreshBlocker:Refresh", data);
            },
          },
        ];

        notificationBox.appendNotification(
          "refresh-blocked",
          {
            label: { "l10n-id": l10nId },
            image: "chrome://browser/skin/notification-icons/popup.svg",
            priority: notificationBox.PRIORITY_INFO_MEDIUM,
          },
          buttons
        );
      }
    }

    _generateUniquePanelID() {
      if (!this._uniquePanelIDCounter) {
        this._uniquePanelIDCounter = 0;
      }

      let outerID = window.docShell.outerWindowID;

      // We want panel IDs to be globally unique, that's why we include the
      // window ID. We switched to a monotonic counter as Date.now() lead
      // to random failures because of colliding IDs.
      return "panel-" + outerID + "-" + ++this._uniquePanelIDCounter;
    }

    destroy() {
      this.tabContainer.destroy();
      Services.obs.removeObserver(this, "contextual-identity-updated");

      for (let tab of this.tabs) {
        let browser = tab.linkedBrowser;
        if (browser.registeredOpenURI) {
          let userContextId = browser.getAttribute("usercontextid") || 0;
          this.UrlbarProviderOpenTabs.unregisterOpenTab(
            browser.registeredOpenURI.spec,
            userContextId,
            PrivateBrowsingUtils.isWindowPrivate(window)
          );
          delete browser.registeredOpenURI;
        }

        let filter = this._tabFilters.get(tab);
        if (filter) {
          browser.webProgress.removeProgressListener(filter);

          let listener = this._tabListeners.get(tab);
          if (listener) {
            filter.removeProgressListener(listener);
            listener.destroy();
          }

          this._tabFilters.delete(tab);
          this._tabListeners.delete(tab);
        }
      }

      document.removeEventListener("keydown", this, { mozSystemGroup: true });
      if (AppConstants.platform == "macosx") {
        document.removeEventListener("keypress", this, {
          mozSystemGroup: true,
        });
      }
      document.removeEventListener("visibilitychange", this);
      window.removeEventListener("framefocusrequested", this);
      window.removeEventListener("activate", this);
      window.removeEventListener("deactivate", this);

      if (gMultiProcessBrowser) {
        if (this._switcher) {
          this._switcher.destroy();
        }
      }
    }

    _setupEventListeners() {
      this.tabpanels.addEventListener("select", event => {
        if (event.target == this.tabpanels) {
          this.updateCurrentBrowser();
        }
      });

      this.addEventListener("DOMWindowClose", event => {
        let browser = event.target;
        if (!browser.isRemoteBrowser) {
          if (!event.isTrusted) {
            // If the browser is not remote, then we expect the event to be trusted.
            // In the remote case, the DOMWindowClose event is captured in content,
            // a message is sent to the parent, and another DOMWindowClose event
            // is re-dispatched on the actual browser node. In that case, the event
            // won't  be marked as trusted, since it's synthesized by JavaScript.
            return;
          }
          // In the parent-process browser case, it's possible that the browser
          // that fired DOMWindowClose is actually a child of another browser. We
          // want to find the top-most browser to determine whether or not this is
          // for a tab or not. The chromeEventHandler will be the top-most browser.
          browser = event.target.docShell.chromeEventHandler;
        }

        if (this.tabs.length == 1) {
          // We already did PermitUnload in the content process
          // for this tab (the only one in the window). So we don't
          // need to do it again for any tabs.
          window.skipNextCanClose = true;
          // In the parent-process browser case, the nsCloseEvent will actually take
          // care of tearing down the window, but we need to do this ourselves in the
          // content-process browser case. Doing so in both cases doesn't appear to
          // hurt.
          window.close();
          return;
        }

        let tab = this.getTabForBrowser(browser);
        if (tab) {
          // Skip running PermitUnload since it already happened in
          // the content process.
          this.removeTab(tab, { skipPermitUnload: true });
          // If we don't preventDefault on the DOMWindowClose event, then
          // in the parent-process browser case, we're telling the platform
          // to close the entire window. Calling preventDefault is our way of
          // saying we took care of this close request by closing the tab.
          event.preventDefault();
        }
      });

      this.addEventListener("pagetitlechanged", event => {
        let browser = event.target;
        let tab = this.getTabForBrowser(browser);
        if (!tab || tab.hasAttribute("pending")) {
          return;
        }

        // Ignore empty title changes on internal pages. This prevents the title
        // from changing while Fluent is populating the (initially-empty) title
        // element.
        if (
          !browser.contentTitle &&
          browser.contentPrincipal.isSystemPrincipal
        ) {
          return;
        }

        let titleChanged = this.setTabTitle(tab);
        if (titleChanged && !tab.selected && !tab.hasAttribute("busy")) {
          tab.setAttribute("titlechanged", "true");
        }
      });

      this.addEventListener(
        "DOMWillOpenModalDialog",
        event => {
          if (!event.isTrusted) {
            return;
          }

          let targetIsWindow = Window.isInstance(event.target);

          // We're about to open a modal dialog, so figure out for which tab:
          // If this is a same-process modal dialog, then we're given its DOM
          // window as the event's target. For remote dialogs, we're given the
          // browser, but that's in the originalTarget and not the target,
          // because it's across the tabbrowser's XBL boundary.
          let tabForEvent = targetIsWindow
            ? this.getTabForBrowser(event.target.docShell.chromeEventHandler)
            : this.getTabForBrowser(event.originalTarget);

          // Focus window for beforeunload dialog so it is seen but don't
          // steal focus from other applications.
          if (
            event.detail &&
            event.detail.tabPrompt &&
            event.detail.inPermitUnload &&
            Services.focus.activeWindow
          ) {
            window.focus();
          }

          // Don't need to act if the tab is already selected or if there isn't
          // a tab for the event (e.g. for the webextensions options_ui remote
          // browsers embedded in the "about:addons" page):
          if (!tabForEvent || tabForEvent.selected) {
            return;
          }

          // We always switch tabs for beforeunload tab-modal prompts.
          if (
            event.detail &&
            event.detail.tabPrompt &&
            !event.detail.inPermitUnload
          ) {
            let docPrincipal = targetIsWindow
              ? event.target.document.nodePrincipal
              : null;
            // At least one of these should/will be non-null:
            let promptPrincipal =
              event.detail.promptPrincipal ||
              docPrincipal ||
              tabForEvent.linkedBrowser.contentPrincipal;

            // For null principals, we bail immediately and don't show the checkbox:
            if (!promptPrincipal || promptPrincipal.isNullPrincipal) {
              tabForEvent.attention = true;
              return;
            }

            // For non-system/expanded principals without permission, we bail and show the checkbox.
            if (promptPrincipal.URI && !promptPrincipal.isSystemPrincipal) {
              let permission = Services.perms.testPermissionFromPrincipal(
                promptPrincipal,
                "focus-tab-by-prompt"
              );
              if (permission != Services.perms.ALLOW_ACTION) {
                // Tell the prompt box we want to show the user a checkbox:
                let tabPrompt = this.getTabDialogBox(tabForEvent.linkedBrowser);
                tabPrompt.onNextPromptShowAllowFocusCheckboxFor(
                  promptPrincipal
                );
                tabForEvent.attention = true;
                return;
              }
            }
            // ... so system and expanded principals, as well as permitted "normal"
            // URI-based principals, always get to steal focus for the tab when prompting.
          }

          // If permissions/origins dictate so, bring tab to the front.
          this.selectedTab = tabForEvent;
        },
        true
      );

      // When cancelling beforeunload tabmodal dialogs, reset the URL bar to
      // avoid spoofing risks.
      this.addEventListener(
        "DOMModalDialogClosed",
        event => {
          if (
            event.detail?.promptType != "beforeunload" ||
            event.detail.areLeaving ||
            event.target.nodeName != "browser"
          ) {
            return;
          }
          event.target.userTypedValue = null;
          if (event.target == this.selectedBrowser) {
            gURLBar.setURI();
          }
        },
        true
      );

      let onTabCrashed = event => {
        if (!event.isTrusted) {
          return;
        }

        let browser = event.originalTarget;

        if (!event.isTopFrame) {
          TabCrashHandler.onSubFrameCrash(browser, event.childID);
          return;
        }

        // Preloaded browsers do not actually have any tabs. If one crashes,
        // it should be released and removed.
        if (browser === this.preloadedBrowser) {
          NewTabPagePreloading.removePreloadedBrowser(window);
          return;
        }

        let isRestartRequiredCrash =
          event.type == "oop-browser-buildid-mismatch";

        let icon = browser.mIconURL;
        let tab = this.getTabForBrowser(browser);

        if (this.selectedBrowser == browser) {
          TabCrashHandler.onSelectedBrowserCrash(
            browser,
            isRestartRequiredCrash
          );
        } else {
          TabCrashHandler.onBackgroundBrowserCrash(
            browser,
            isRestartRequiredCrash
          );
        }

        tab.removeAttribute("soundplaying");
        this.setIcon(tab, icon);
      };

      this.addEventListener("oop-browser-crashed", onTabCrashed);
      this.addEventListener("oop-browser-buildid-mismatch", onTabCrashed);

      this.addEventListener("DOMAudioPlaybackStarted", event => {
        var tab = this.getTabFromAudioEvent(event);
        if (!tab) {
          return;
        }

        clearTimeout(tab._soundPlayingAttrRemovalTimer);
        tab._soundPlayingAttrRemovalTimer = 0;

        let modifiedAttrs = [];
        if (tab.hasAttribute("soundplaying-scheduledremoval")) {
          tab.removeAttribute("soundplaying-scheduledremoval");
          modifiedAttrs.push("soundplaying-scheduledremoval");
        }

        if (!tab.hasAttribute("soundplaying")) {
          tab.toggleAttribute("soundplaying", true);
          modifiedAttrs.push("soundplaying");
        }

        if (modifiedAttrs.length) {
          // Flush style so that the opacity takes effect immediately, in
          // case the media is stopped before the style flushes naturally.
          getComputedStyle(tab).opacity;
        }

        this._tabAttrModified(tab, modifiedAttrs);
      });

      this.addEventListener("DOMAudioPlaybackStopped", event => {
        var tab = this.getTabFromAudioEvent(event);
        if (!tab) {
          return;
        }

        if (tab.hasAttribute("soundplaying")) {
          let removalDelay = Services.prefs.getIntPref(
            "browser.tabs.delayHidingAudioPlayingIconMS"
          );

          tab.style.setProperty(
            "--soundplaying-removal-delay",
            `${removalDelay - 300}ms`
          );
          tab.toggleAttribute("soundplaying-scheduledremoval", true);
          this._tabAttrModified(tab, ["soundplaying-scheduledremoval"]);

          tab._soundPlayingAttrRemovalTimer = setTimeout(() => {
            tab.removeAttribute("soundplaying-scheduledremoval");
            tab.removeAttribute("soundplaying");
            this._tabAttrModified(tab, [
              "soundplaying",
              "soundplaying-scheduledremoval",
            ]);
          }, removalDelay);
        }
      });

      this.addEventListener("DOMAudioPlaybackBlockStarted", event => {
        var tab = this.getTabFromAudioEvent(event);
        if (!tab) {
          return;
        }

        if (!tab.hasAttribute("activemedia-blocked")) {
          tab.setAttribute("activemedia-blocked", true);
          this._tabAttrModified(tab, ["activemedia-blocked"]);
        }
      });

      this.addEventListener("DOMAudioPlaybackBlockStopped", event => {
        var tab = this.getTabFromAudioEvent(event);
        if (!tab) {
          return;
        }

        if (tab.hasAttribute("activemedia-blocked")) {
          tab.removeAttribute("activemedia-blocked");
          this._tabAttrModified(tab, ["activemedia-blocked"]);
        }
      });

      this.addEventListener("GloballyAutoplayBlocked", event => {
        let browser = event.originalTarget;
        let tab = this.getTabForBrowser(browser);
        if (!tab) {
          return;
        }

        SitePermissions.setForPrincipal(
          browser.contentPrincipal,
          "autoplay-media",
          SitePermissions.BLOCK,
          SitePermissions.SCOPE_GLOBAL,
          browser
        );
      });

      let tabContextFTLInserter = () => {
        this.translateTabContextMenu();
        this.tabContainer.removeEventListener(
          "contextmenu",
          tabContextFTLInserter,
          true
        );
        this.tabContainer.removeEventListener(
          "mouseover",
          tabContextFTLInserter
        );
        this.tabContainer.removeEventListener(
          "focus",
          tabContextFTLInserter,
          true
        );
      };
      this.tabContainer.addEventListener(
        "contextmenu",
        tabContextFTLInserter,
        true
      );
      this.tabContainer.addEventListener("mouseover", tabContextFTLInserter);
      this.tabContainer.addEventListener("focus", tabContextFTLInserter, true);

      // Fired when Gecko has decided a <browser> element will change
      // remoteness. This allows persisting some state on this element across
      // process switches.
      this.addEventListener("WillChangeBrowserRemoteness", event => {
        let browser = event.originalTarget;
        let tab = this.getTabForBrowser(browser);
        if (!tab) {
          return;
        }

        // Dispatch the `BeforeTabRemotenessChange` event, allowing other code
        // to react to this tab's process switch.
        let evt = document.createEvent("Events");
        evt.initEvent("BeforeTabRemotenessChange", true, false);
        tab.dispatchEvent(evt);

        // Unhook our progress listener.
        let filter = this._tabFilters.get(tab);
        let oldListener = this._tabListeners.get(tab);
        browser.webProgress.removeProgressListener(filter);
        filter.removeProgressListener(oldListener);
        let stateFlags = oldListener.mStateFlags;
        let requestCount = oldListener.mRequestCount;

        // We'll be creating a new listener, so destroy the old one.
        oldListener.destroy();

        let oldDroppedLinkHandler = browser.droppedLinkHandler;
        let oldUserTypedValue = browser.userTypedValue;
        let hadStartedLoad = browser.didStartLoadSinceLastUserTyping();

        let didChange = () => {
          browser.userTypedValue = oldUserTypedValue;
          if (hadStartedLoad) {
            browser.urlbarChangeTracker.startedLoad();
          }

          browser.droppedLinkHandler = oldDroppedLinkHandler;

          // This shouldn't really be necessary, however, this has the side effect
          // of sending MozLayerTreeReady / MozLayerTreeCleared events for remote
          // frames, which the tab switcher depends on.
          //
          // eslint-disable-next-line no-self-assign
          browser.docShellIsActive = browser.docShellIsActive;

          // Create a new tab progress listener for the new browser we just
          // injected, since tab progress listeners have logic for handling the
          // initial about:blank load
          let listener = new TabProgressListener(
            tab,
            browser,
            false,
            false,
            stateFlags,
            requestCount
          );
          this._tabListeners.set(tab, listener);
          filter.addProgressListener(listener, Ci.nsIWebProgress.NOTIFY_ALL);

          // Restore the progress listener.
          browser.webProgress.addProgressListener(
            filter,
            Ci.nsIWebProgress.NOTIFY_ALL
          );

          let cbEvent = browser.getContentBlockingEvents();
          // Include the true final argument to indicate that this event is
          // simulated (instead of being observed by the webProgressListener).
          this._callProgressListeners(
            browser,
            "onContentBlockingEvent",
            [browser.webProgress, null, cbEvent, true],
            true,
            false
          );

          if (browser.isRemoteBrowser) {
            // Switching the browser to be remote will connect to a new child
            // process so the browser can no longer be considered to be
            // crashed.
            tab.removeAttribute("crashed");
          }

          if (this.isFindBarInitialized(tab)) {
            this.getCachedFindBar(tab).browser = browser;
          }

          evt = document.createEvent("Events");
          evt.initEvent("TabRemotenessChange", true, false);
          tab.dispatchEvent(evt);
        };
        browser.addEventListener("DidChangeBrowserRemoteness", didChange, {
          once: true,
        });
      });

      this.addEventListener("pageinfo", event => {
        let browser = event.originalTarget;
        let tab = this.getTabForBrowser(browser);
        if (!tab) {
          return;
        }
        const { url, description, previewImageURL } = event.detail;
        this.setPageInfo(tab, url, description, previewImageURL);
      });
    }

    translateTabContextMenu() {
      if (this._tabContextMenuTranslated) {
        return;
      }
      MozXULElement.insertFTLIfNeeded("browser/tabContextMenu.ftl");
      // Un-lazify the l10n-ids now that the FTL file has been inserted.
      document
        .getElementById("tabContextMenu")
        .querySelectorAll("[data-lazy-l10n-id]")
        .forEach(el => {
          el.setAttribute("data-l10n-id", el.getAttribute("data-lazy-l10n-id"));
          el.removeAttribute("data-lazy-l10n-id");
        });
      this._tabContextMenuTranslated = true;
    }

    setSuccessor(aTab, successorTab) {
      if (aTab.ownerGlobal != window) {
        throw new Error("Cannot set the successor of another window's tab");
      }
      if (successorTab == aTab) {
        successorTab = null;
      }
      if (successorTab && successorTab.ownerGlobal != window) {
        throw new Error("Cannot set the successor to another window's tab");
      }
      if (aTab.successor) {
        aTab.successor.predecessors.delete(aTab);
      }
      aTab.successor = successorTab;
      if (successorTab) {
        if (!successorTab.predecessors) {
          successorTab.predecessors = new Set();
        }
        successorTab.predecessors.add(aTab);
      }
    }

    /**
     * For all tabs with aTab as a successor, set the successor to aOtherTab
     * instead.
     */
    replaceInSuccession(aTab, aOtherTab) {
      if (aTab.predecessors) {
        for (const predecessor of Array.from(aTab.predecessors)) {
          this.setSuccessor(predecessor, aOtherTab);
        }
      }
    }

    /**
     * Get the triggering principal for the last navigation in the session history.
     */
    _getTriggeringPrincipalFromHistory(aBrowser) {
      let sessionHistory = aBrowser?.browsingContext?.sessionHistory;
      if (
        !sessionHistory ||
        !sessionHistory.index ||
        sessionHistory.count == 0
      ) {
        return undefined;
      }
      let currentEntry = sessionHistory.getEntryAtIndex(sessionHistory.index);
      let triggeringPrincipal = currentEntry?.triggeringPrincipal;
      return triggeringPrincipal;
    }

    clearRelatedTabs() {
      this._lastRelatedTabMap = new WeakMap();
    }
  };

  /**
   * A web progress listener object definition for a given tab.
   */
  class TabProgressListener {
    constructor(
      aTab,
      aBrowser,
      aStartsBlank,
      aWasPreloadedBrowser,
      aOrigStateFlags,
      aOrigRequestCount
    ) {
      let stateFlags = aOrigStateFlags || 0;
      // Initialize mStateFlags to non-zero e.g. when creating a progress
      // listener for preloaded browsers as there was no progress listener
      // around when the content started loading. If the content didn't
      // quite finish loading yet, mStateFlags will very soon be overridden
      // with the correct value and end up at STATE_STOP again.
      if (aWasPreloadedBrowser) {
        stateFlags =
          Ci.nsIWebProgressListener.STATE_STOP |
          Ci.nsIWebProgressListener.STATE_IS_REQUEST;
      }

      this.mTab = aTab;
      this.mBrowser = aBrowser;
      this.mBlank = aStartsBlank;

      // cache flags for correct status UI update after tab switching
      this.mStateFlags = stateFlags;
      this.mStatus = 0;
      this.mMessage = "";
      this.mTotalProgress = 0;

      // count of open requests (should always be 0 or 1)
      this.mRequestCount = aOrigRequestCount || 0;
    }

    destroy() {
      delete this.mTab;
      delete this.mBrowser;
    }

    _callProgressListeners(...args) {
      args.unshift(this.mBrowser);
      return gBrowser._callProgressListeners.apply(gBrowser, args);
    }

    _shouldShowProgress(aRequest) {
      if (this.mBlank) {
        return false;
      }

      // Don't show progress indicators in tabs for about: URIs
      // pointing to local resources.
      if (
        aRequest instanceof Ci.nsIChannel &&
        aRequest.originalURI.schemeIs("about")
      ) {
        return false;
      }

      return true;
    }

    _isForInitialAboutBlank(aWebProgress, aStateFlags, aLocation) {
      if (!this.mBlank || !aWebProgress.isTopLevel) {
        return false;
      }

      // If the state has STATE_STOP, and no requests were in flight, then this
      // must be the initial "stop" for the initial about:blank document.
      if (
        aStateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
        this.mRequestCount == 0 &&
        !aLocation
      ) {
        return true;
      }

      let location = aLocation ? aLocation.spec : "";
      return location == "about:blank";
    }

    onProgressChange(
      aWebProgress,
      aRequest,
      aCurSelfProgress,
      aMaxSelfProgress,
      aCurTotalProgress,
      aMaxTotalProgress
    ) {
      this.mTotalProgress = aMaxTotalProgress
        ? aCurTotalProgress / aMaxTotalProgress
        : 0;

      if (!this._shouldShowProgress(aRequest)) {
        return;
      }

      if (this.mTotalProgress && this.mTab.hasAttribute("busy")) {
        this.mTab.setAttribute("progress", "true");
        gBrowser._tabAttrModified(this.mTab, ["progress"]);
      }

      this._callProgressListeners("onProgressChange", [
        aWebProgress,
        aRequest,
        aCurSelfProgress,
        aMaxSelfProgress,
        aCurTotalProgress,
        aMaxTotalProgress,
      ]);
    }

    onProgressChange64(
      aWebProgress,
      aRequest,
      aCurSelfProgress,
      aMaxSelfProgress,
      aCurTotalProgress,
      aMaxTotalProgress
    ) {
      return this.onProgressChange(
        aWebProgress,
        aRequest,
        aCurSelfProgress,
        aMaxSelfProgress,
        aCurTotalProgress,
        aMaxTotalProgress
      );
    }

    /* eslint-disable complexity */
    onStateChange(aWebProgress, aRequest, aStateFlags, aStatus) {
      if (!aRequest) {
        return;
      }

      let location, originalLocation;
      try {
        aRequest.QueryInterface(Ci.nsIChannel);
        location = aRequest.URI;
        originalLocation = aRequest.originalURI;
      } catch (ex) {}

      let ignoreBlank = this._isForInitialAboutBlank(
        aWebProgress,
        aStateFlags,
        location
      );

      const { STATE_START, STATE_STOP, STATE_IS_NETWORK } =
        Ci.nsIWebProgressListener;

      // If we were ignoring some messages about the initial about:blank, and we
      // got the STATE_STOP for it, we'll want to pay attention to those messages
      // from here forward. Similarly, if we conclude that this state change
      // is one that we shouldn't be ignoring, then stop ignoring.
      if (
        (ignoreBlank &&
          aStateFlags & STATE_STOP &&
          aStateFlags & STATE_IS_NETWORK) ||
        (!ignoreBlank && this.mBlank)
      ) {
        this.mBlank = false;
      }

      if (aStateFlags & STATE_START && aStateFlags & STATE_IS_NETWORK) {
        this.mRequestCount++;

        if (aWebProgress.isTopLevel) {
          // Need to use originalLocation rather than location because things
          // like about:home and about:privatebrowsing arrive with nsIRequest
          // pointing to their resolved jar: or file: URIs.
          if (
            !(
              originalLocation &&
              gInitialPages.includes(originalLocation.spec) &&
              originalLocation != "about:blank" &&
              this.mBrowser.initialPageLoadedFromUserAction !=
                originalLocation.spec &&
              this.mBrowser.currentURI &&
              this.mBrowser.currentURI.spec == "about:blank"
            )
          ) {
            // Indicating that we started a load will allow the location
            // bar to be cleared when the load finishes.
            // In order to not overwrite user-typed content, we avoid it
            // (see if condition above) in a very specific case:
            // If the load is of an 'initial' page (e.g. about:privatebrowsing,
            // about:newtab, etc.), was not explicitly typed in the location
            // bar by the user, is not about:blank (because about:blank can be
            // loaded by websites under their principal), and the current
            // page in the browser is about:blank (indicating it is a newly
            // created or re-created browser, e.g. because it just switched
            // remoteness or is a new tab/window).
            this.mBrowser.urlbarChangeTracker.startedLoad();

            // To improve the user experience and perceived performance when
            // opening links in new tabs, we show the url and tab title sooner,
            // but only if it's safe (from a phishing point of view) to do so,
            // thus there's no session history and the load starts from a
            // non-web-controlled blank page.
            if (
              this.mBrowser.browsingContext.sessionHistory?.count === 0 &&
              BrowserUIUtils.checkEmptyPageOrigin(
                this.mBrowser,
                originalLocation
              )
            ) {
              gBrowser.setInitialTabTitle(this.mTab, originalLocation.spec, {
                isURL: true,
              });

              this.mBrowser.browsingContext.nonWebControlledBlankURI =
                originalLocation;
              if (this.mTab.selected && !gBrowser.userTypedValue) {
                gURLBar.setURI();
              }
            }
          }
          delete this.mBrowser.initialPageLoadedFromUserAction;
          // If the browser is loading it must not be crashed anymore
          this.mTab.removeAttribute("crashed");
        }

        if (this._shouldShowProgress(aRequest)) {
          if (
            !(aStateFlags & Ci.nsIWebProgressListener.STATE_RESTORING) &&
            aWebProgress &&
            aWebProgress.isTopLevel
          ) {
            this.mTab.setAttribute("busy", "true");
            gBrowser._tabAttrModified(this.mTab, ["busy"]);
            this.mTab._notselectedsinceload = !this.mTab.selected;
          }

          if (this.mTab.selected) {
            gBrowser._isBusy = true;
          }
        }
      } else if (aStateFlags & STATE_STOP && aStateFlags & STATE_IS_NETWORK) {
        // since we (try to) only handle STATE_STOP of the last request,
        // the count of open requests should now be 0
        this.mRequestCount = 0;

        let modifiedAttrs = [];
        if (this.mTab.hasAttribute("busy")) {
          this.mTab.removeAttribute("busy");
          modifiedAttrs.push("busy");

          // Only animate the "burst" indicating the page has loaded if
          // the top-level page is the one that finished loading.
          if (
            aWebProgress.isTopLevel &&
            !aWebProgress.isLoadingDocument &&
            Components.isSuccessCode(aStatus) &&
            !gBrowser.tabAnimationsInProgress &&
            !gReduceMotion
          ) {
            if (this.mTab._notselectedsinceload) {
              this.mTab.setAttribute("notselectedsinceload", "true");
            } else {
              this.mTab.removeAttribute("notselectedsinceload");
            }

            this.mTab.setAttribute("bursting", "true");
          }
        }

        if (this.mTab.hasAttribute("progress")) {
          this.mTab.removeAttribute("progress");
          modifiedAttrs.push("progress");
        }

        if (modifiedAttrs.length) {
          gBrowser._tabAttrModified(this.mTab, modifiedAttrs);
        }

        if (aWebProgress.isTopLevel) {
          let isSuccessful = Components.isSuccessCode(aStatus);
          if (!isSuccessful && !this.mTab.isEmpty) {
            // Restore the current document's location in case the
            // request was stopped (possibly from a content script)
            // before the location changed.

            this.mBrowser.userTypedValue = null;
            // When browser.tabs.documentchannel.parent-controlled pref and SHIP
            // are enabled and a load gets cancelled due to another one
            // starting, the error is NS_BINDING_CANCELLED_OLD_LOAD.
            // When these prefs are not enabled, the error is different and
            // that's why we still want to look at the isNavigating flag.
            // We could add a workaround and make sure that in the alternative
            // codepaths we would also omit the same error, but considering
            // how we will be enabling fission by default soon, we can keep
            // using isNavigating for now, and remove it when the
            // parent-controlled pref and SHIP are enabled by default.
            // Bug 1725716 has been filed to consider removing isNavigating
            // field alltogether.
            let isNavigating = this.mBrowser.isNavigating;
            if (
              this.mTab.selected &&
              aStatus != Cr.NS_BINDING_CANCELLED_OLD_LOAD &&
              !isNavigating
            ) {
              gURLBar.setURI();
            }
          } else if (isSuccessful) {
            this.mBrowser.urlbarChangeTracker.finishedLoad();
          }
        }

        // If we don't already have an icon for this tab then clear the tab's
        // icon. Don't do this on the initial about:blank load to prevent
        // flickering. Don't clear the icon if we already set it from one of the
        // known defaults. Note we use the original URL since about:newtab
        // redirects to a prerendered page.
        if (
          !this.mBrowser.mIconURL &&
          !ignoreBlank &&
          !(originalLocation.spec in FAVICON_DEFAULTS)
        ) {
          this.mTab.removeAttribute("image");
        } else {
          // Bug 1804166: Allow new tabs to set the favicon correctly if the
          // new tabs behavior is set to open a blank page
          // This is a no-op unless this.mBrowser._documentURI is in
          // FAVICON_DEFAULTS.
          gBrowser.setDefaultIcon(this.mTab, this.mBrowser._documentURI);
        }

        // For keyword URIs clear the user typed value since they will be changed into real URIs
        if (location.scheme == "keyword") {
          this.mBrowser.userTypedValue = null;
        }

        if (this.mTab.selected) {
          gBrowser._isBusy = false;
        }
      }

      if (ignoreBlank) {
        this._callProgressListeners(
          "onUpdateCurrentBrowser",
          [aStateFlags, aStatus, "", 0],
          true,
          false
        );
      } else {
        this._callProgressListeners(
          "onStateChange",
          [aWebProgress, aRequest, aStateFlags, aStatus],
          true,
          false
        );
      }

      this._callProgressListeners(
        "onStateChange",
        [aWebProgress, aRequest, aStateFlags, aStatus],
        false
      );

      if (aStateFlags & (STATE_START | STATE_STOP)) {
        // reset cached temporary values at beginning and end
        this.mMessage = "";
        this.mTotalProgress = 0;
      }
      this.mStateFlags = aStateFlags;
      this.mStatus = aStatus;
    }
    /* eslint-enable complexity */

    onLocationChange(aWebProgress, aRequest, aLocation, aFlags) {
      // OnLocationChange is called for both the top-level content
      // and the subframes.
      let topLevel = aWebProgress.isTopLevel;

      let isSameDocument = !!(
        aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT
      );
      if (topLevel) {
        let isReload = !!(
          aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_RELOAD
        );
        let isErrorPage = !!(
          aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_ERROR_PAGE
        );

        // We need to clear the typed value
        // if the document failed to load, to make sure the urlbar reflects the
        // failed URI (particularly for SSL errors). However, don't clear the value
        // if the error page's URI is about:blank, because that causes complete
        // loss of urlbar contents for invalid URI errors (see bug 867957).
        // Another reason to clear the userTypedValue is if this was an anchor
        // navigation initiated by the user.
        // Finally, we do insert the URL if this is a same-document navigation
        // and the user cleared the URL manually.
        if (
          this.mBrowser.didStartLoadSinceLastUserTyping() ||
          (isErrorPage && aLocation.spec != "about:blank") ||
          (isSameDocument && this.mBrowser.isNavigating) ||
          (isSameDocument && !this.mBrowser.userTypedValue)
        ) {
          this.mBrowser.userTypedValue = null;
        }

        // If the tab has been set to "busy" outside the stateChange
        // handler below (e.g. by sessionStore.navigateAndRestore), and
        // the load results in an error page, it's possible that there
        // isn't any (STATE_IS_NETWORK & STATE_STOP) state to cause busy
        // attribute being removed. In this case we should remove the
        // attribute here.
        if (isErrorPage && this.mTab.hasAttribute("busy")) {
          this.mTab.removeAttribute("busy");
          gBrowser._tabAttrModified(this.mTab, ["busy"]);
        }

        if (!isSameDocument) {
          // If the browser was playing audio, we should remove the playing state.
          if (this.mTab.hasAttribute("soundplaying")) {
            clearTimeout(this.mTab._soundPlayingAttrRemovalTimer);
            this.mTab._soundPlayingAttrRemovalTimer = 0;
            this.mTab.removeAttribute("soundplaying");
            gBrowser._tabAttrModified(this.mTab, ["soundplaying"]);
          }

          // If the browser was previously muted, we should restore the muted state.
          if (this.mTab.hasAttribute("muted")) {
            this.mTab.linkedBrowser.mute();
          }

          if (gBrowser.isFindBarInitialized(this.mTab)) {
            let findBar = gBrowser.getCachedFindBar(this.mTab);

            // Close the Find toolbar if we're in old-style TAF mode
            if (findBar.findMode != findBar.FIND_NORMAL) {
              findBar.close();
            }
          }

          // Note that we're not updating for same-document loads, despite
          // the `title` argument to `history.pushState/replaceState`. For
          // context, see https://bugzilla.mozilla.org/show_bug.cgi?id=585653
          // and https://github.com/whatwg/html/issues/2174
          if (!isReload) {
            gBrowser.setTabTitle(this.mTab);
          }

          // Don't clear the favicon if this tab is in the pending
          // state, as SessionStore will have set the icon for us even
          // though we're pointed at an about:blank. Also don't clear it
          // if the tab is in customize mode, to keep the one set by
          // gCustomizeMode.setTab (bug 1551239). Also don't clear it
          // if onLocationChange was triggered by a pushState or a
          // replaceState (bug 550565) or a hash change (bug 408415).
          if (
            !this.mTab.hasAttribute("pending") &&
            !this.mTab.hasAttribute("customizemode") &&
            aWebProgress.isLoadingDocument
          ) {
            // Removing the tab's image here causes flickering, wait until the
            // load is complete.
            this.mBrowser.mIconURL = null;
          }

          if (!isReload && aWebProgress.isLoadingDocument) {
            let triggerer = gBrowser._getTriggeringPrincipalFromHistory(
              this.mBrowser
            );
            // Typing a url, searching or clicking a bookmark will load a new
            // document that is no longer tied to a navigation from the previous
            // content and will have a system principal as the triggerer.
            if (triggerer && triggerer.isSystemPrincipal) {
              // Reset the related tab map so that the next tab opened will be related
              // to this new document and not to tabs opened by the previous one.
              gBrowser.clearRelatedTabs();
            }
          }

          if (
            aRequest instanceof Ci.nsIChannel &&
            !isBlankPageURL(aRequest.originalURI.spec)
          ) {
            this.mBrowser.originalURI = aRequest.originalURI;
          }
        }

        let userContextId = this.mBrowser.getAttribute("usercontextid") || 0;
        if (this.mBrowser.registeredOpenURI) {
          let uri = this.mBrowser.registeredOpenURI;
          gBrowser.UrlbarProviderOpenTabs.unregisterOpenTab(
            uri.spec,
            userContextId,
            PrivateBrowsingUtils.isWindowPrivate(window)
          );
          delete this.mBrowser.registeredOpenURI;
        }
        if (!isBlankPageURL(aLocation.spec)) {
          gBrowser.UrlbarProviderOpenTabs.registerOpenTab(
            aLocation.spec,
            userContextId,
            PrivateBrowsingUtils.isWindowPrivate(window)
          );
          this.mBrowser.registeredOpenURI = aLocation;
        }

        if (this.mTab != gBrowser.selectedTab) {
          let tabCacheIndex = gBrowser._tabLayerCache.indexOf(this.mTab);
          if (tabCacheIndex != -1) {
            gBrowser._tabLayerCache.splice(tabCacheIndex, 1);
            gBrowser._getSwitcher().cleanUpTabAfterEviction(this.mTab);
          }
        }
      }

      if (!this.mBlank || this.mBrowser.hasContentOpener) {
        this._callProgressListeners("onLocationChange", [
          aWebProgress,
          aRequest,
          aLocation,
          aFlags,
        ]);
        if (topLevel && !isSameDocument) {
          // Include the true final argument to indicate that this event is
          // simulated (instead of being observed by the webProgressListener).
          this._callProgressListeners("onContentBlockingEvent", [
            aWebProgress,
            null,
            0,
            true,
          ]);
        }
      }

      if (topLevel) {
        this.mBrowser.lastURI = aLocation;
        this.mBrowser.lastLocationChange = Date.now();
      }
    }

    onStatusChange(aWebProgress, aRequest, aStatus, aMessage) {
      if (this.mBlank) {
        return;
      }

      this._callProgressListeners("onStatusChange", [
        aWebProgress,
        aRequest,
        aStatus,
        aMessage,
      ]);

      this.mMessage = aMessage;
    }

    onSecurityChange(aWebProgress, aRequest, aState) {
      this._callProgressListeners("onSecurityChange", [
        aWebProgress,
        aRequest,
        aState,
      ]);
    }

    onContentBlockingEvent(aWebProgress, aRequest, aEvent) {
      this._callProgressListeners("onContentBlockingEvent", [
        aWebProgress,
        aRequest,
        aEvent,
      ]);
    }

    onRefreshAttempted(aWebProgress, aURI, aDelay, aSameURI) {
      return this._callProgressListeners("onRefreshAttempted", [
        aWebProgress,
        aURI,
        aDelay,
        aSameURI,
      ]);
    }
  }
  TabProgressListener.prototype.QueryInterface = ChromeUtils.generateQI([
    "nsIWebProgressListener",
    "nsIWebProgressListener2",
    "nsISupportsWeakReference",
  ]);

  let URILoadingWrapper = {
    _normalizeLoadURIOptions(browser, loadURIOptions) {
      if (!loadURIOptions.triggeringPrincipal) {
        throw new Error("Must load with a triggering Principal");
      }

      if (
        loadURIOptions.userContextId &&
        loadURIOptions.userContextId != browser.getAttribute("usercontextid")
      ) {
        throw new Error("Cannot load with mismatched userContextId");
      }

      loadURIOptions.loadFlags |= loadURIOptions.flags | LOAD_FLAGS_NONE;
      delete loadURIOptions.flags;
      loadURIOptions.hasValidUserGestureActivation ??=
        document.hasValidTransientUserGestureActivation;
    },

    _loadFlagsToFixupFlags(browser, loadFlags) {
      // Attempt to perform URI fixup to see if we can handle this URI in chrome.
      let fixupFlags = Ci.nsIURIFixup.FIXUP_FLAG_NONE;
      if (loadFlags & LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP) {
        fixupFlags |= Ci.nsIURIFixup.FIXUP_FLAG_ALLOW_KEYWORD_LOOKUP;
      }
      if (loadFlags & LOAD_FLAGS_FIXUP_SCHEME_TYPOS) {
        fixupFlags |= Ci.nsIURIFixup.FIXUP_FLAG_FIX_SCHEME_TYPOS;
      }
      if (PrivateBrowsingUtils.isBrowserPrivate(browser)) {
        fixupFlags |= Ci.nsIURIFixup.FIXUP_FLAG_PRIVATE_CONTEXT;
      }
      return fixupFlags;
    },

    _fixupURIString(browser, uriString, loadURIOptions) {
      let fixupFlags = this._loadFlagsToFixupFlags(
        browser,
        loadURIOptions.loadFlags
      );

      // XXXgijs: If we switch to loading the URI we return from this method,
      // rather than redoing fixup in docshell (see bug 1815509), we need to
      // ensure that the loadURIOptions have the fixup flag removed here for
      // loads where `uriString` already parses if just passed immediately
      // to `newURI`.
      // Right now this happens in nsDocShellLoadState code.
      try {
        let fixupInfo = Services.uriFixup.getFixupURIInfo(
          uriString,
          fixupFlags
        );
        return fixupInfo.preferredURI;
      } catch (e) {
        // getFixupURIInfo may throw. Just return null, our caller will deal.
      }
      return null;
    },

    /**
     * Handles URIs when we want to deal with them in chrome code rather than pass
     * them down to a content browser. This can avoid unnecessary process switching
     * for the browser.
     * @param aBrowser the browser that is attempting to load the URI
     * @param aUri the nsIURI that is being loaded
     * @returns true if the URI is handled, otherwise false
     */
    _handleUriInChrome(aBrowser, aUri) {
      if (aUri.scheme == "file") {
        try {
          let mimeType = Cc["@mozilla.org/mime;1"]
            .getService(Ci.nsIMIMEService)
            .getTypeFromURI(aUri);
          if (mimeType == "application/x-xpinstall") {
            let systemPrincipal =
              Services.scriptSecurityManager.getSystemPrincipal();
            AddonManager.getInstallForURL(aUri.spec, {
              telemetryInfo: { source: "file-url" },
            }).then(install => {
              AddonManager.installAddonFromWebpage(
                mimeType,
                aBrowser,
                systemPrincipal,
                install
              );
            });
            return true;
          }
        } catch (e) {
          return false;
        }
      }

      return false;
    },

    _updateTriggerMetadataForLoad(
      browser,
      uriString,
      { loadFlags, globalHistoryOptions }
    ) {
      if (globalHistoryOptions?.triggeringSponsoredURL) {
        try {
          // Browser may access URL after fixing it up, then store the URL into DB.
          // To match with it, fix the link up explicitly.
          const triggeringSponsoredURL = Services.uriFixup.getFixupURIInfo(
            globalHistoryOptions.triggeringSponsoredURL,
            this._loadFlagsToFixupFlags(browser, loadFlags)
          ).fixedURI.spec;
          browser.setAttribute(
            "triggeringSponsoredURL",
            triggeringSponsoredURL
          );
          const time =
            globalHistoryOptions.triggeringSponsoredURLVisitTimeMS ||
            Date.now();
          browser.setAttribute("triggeringSponsoredURLVisitTimeMS", time);
        } catch (e) {}
      }

      if (globalHistoryOptions?.triggeringSearchEngine) {
        browser.setAttribute(
          "triggeringSearchEngine",
          globalHistoryOptions.triggeringSearchEngine
        );
        browser.setAttribute("triggeringSearchEngineURL", uriString);
      } else {
        browser.removeAttribute("triggeringSearchEngine");
        browser.removeAttribute("triggeringSearchEngineURL");
      }
    },

    // Both of these are used to override functions on browser-custom-element.
    fixupAndLoadURIString(browser, uriString, loadURIOptions = {}) {
      this._internalMaybeFixupLoadURI(browser, uriString, null, loadURIOptions);
    },
    loadURI(browser, uri, loadURIOptions = {}) {
      this._internalMaybeFixupLoadURI(browser, "", uri, loadURIOptions);
    },

    // A shared function used by both remote and non-remote browsers to
    // load a string URI or redirect it to the correct process.
    _internalMaybeFixupLoadURI(browser, uriString, uri, loadURIOptions) {
      this._normalizeLoadURIOptions(browser, loadURIOptions);
      // Some callers pass undefined/null when calling
      // loadURI/fixupAndLoadURIString. Just load about:blank instead:
      if (!uriString && !uri) {
        uri = Services.io.newURI("about:blank");
      }

      // We need a URI in frontend code for checking various things. Ideally
      // we would then also pass that URI to webnav/browsingcontext code
      // for loading, but we historically haven't. Changing this would alter
      // fixup scenarios in some non-obvious cases.
      let startedWithURI = !!uri;
      if (!uri) {
        // Note: this may return null if we can't make a URI out of the input.
        uri = this._fixupURIString(browser, uriString, loadURIOptions);
      }

      if (uri && this._handleUriInChrome(browser, uri)) {
        // If we've handled the URI in chrome, then just return here.
        return;
      }

      this._updateTriggerMetadataForLoad(
        browser,
        uriString || uri.spec,
        loadURIOptions
      );

      // XXX(nika): Is `browser.isNavigating` necessary anymore?
      // XXX(gijs): Unsure. But it mirrors docShell.isNavigating, but in the parent process
      // (and therefore imperfectly so).
      browser.isNavigating = true;

      try {
        // Should more generally prefer loadURI here - see bug 1815509.
        if (startedWithURI) {
          browser.webNavigation.loadURI(uri, loadURIOptions);
        } else {
          browser.webNavigation.fixupAndLoadURIString(
            uriString,
            loadURIOptions
          );
        }
      } finally {
        browser.isNavigating = false;
      }
    },
  };
} // end private scope for gBrowser

var StatusPanel = {
  // This is useful for debugging (set to `true` in the interesting state for
  // the panel to remain in that state).
  _frozen: false,

  get panel() {
    delete this.panel;
    this.panel = document.getElementById("statuspanel");
    this.panel.addEventListener(
      "transitionend",
      this._onTransitionEnd.bind(this)
    );
    this.panel.addEventListener(
      "transitioncancel",
      this._onTransitionEnd.bind(this)
    );
    return this.panel;
  },

  get isVisible() {
    return !this.panel.hasAttribute("inactive");
  },

  update() {
    if (BrowserHandler.kiosk || this._frozen) {
      return;
    }
    let text;
    let type;
    let types = ["overLink"];
    if (XULBrowserWindow.busyUI) {
      types.push("status");
    }
    types.push("defaultStatus");
    for (type of types) {
      if ((text = XULBrowserWindow[type])) {
        break;
      }
    }

    // If it's a long data: URI that uses base64 encoding, truncate to
    // a reasonable length rather than trying to display the entire thing.
    // We can't shorten arbitrary URIs like this, as bidi etc might mean
    // we need the trailing characters for display. But a base64-encoded
    // data-URI is plain ASCII, so this is OK for status panel display.
    // (See bug 1484071.)
    let textCropped = false;
    if (text.length > 500 && text.match(/^data:[^,]+;base64,/)) {
      text = text.substring(0, 500) + "\u2026";
      textCropped = true;
    }

    if (this._labelElement.value != text || (text && !this.isVisible)) {
      this.panel.setAttribute("previoustype", this.panel.getAttribute("type"));
      this.panel.setAttribute("type", type);

      this._label = text;
      this._labelElement.setAttribute(
        "crop",
        type == "overLink" && !textCropped ? "center" : "end"
      );
    }
  },

  get _labelElement() {
    delete this._labelElement;
    return (this._labelElement = document.getElementById("statuspanel-label"));
  },

  set _label(val) {
    if (!this.isVisible) {
      this.panel.removeAttribute("mirror");
      this.panel.removeAttribute("sizelimit");
    }

    if (
      this.panel.getAttribute("type") == "status" &&
      this.panel.getAttribute("previoustype") == "status"
    ) {
      // Before updating the label, set the panel's current width as its
      // min-width to let the panel grow but not shrink and prevent
      // unnecessary flicker while loading pages. We only care about the
      // panel's width once it has been painted, so we can do this
      // without flushing layout.
      this.panel.style.minWidth =
        window.windowUtils.getBoundsWithoutFlushing(this.panel).width + "px";
    } else {
      this.panel.style.minWidth = "";
    }

    if (val) {
      this._labelElement.value = val;
      if (this.panel.hidden) {
        this.panel.hidden = false;
        // This ensures that the "inactive" attribute removal triggers a
        // transition.
        getComputedStyle(this.panel).display;
      }
      this.panel.removeAttribute("inactive");
      MousePosTracker.addListener(this);
    } else {
      this.panel.setAttribute("inactive", "true");
      MousePosTracker.removeListener(this);
    }
  },

  _onTransitionEnd() {
    if (!this.isVisible) {
      this.panel.hidden = true;
    }
  },

  getMouseTargetRect() {
    let container = this.panel.parentNode;
    let panelRect = window.windowUtils.getBoundsWithoutFlushing(this.panel);
    let containerRect = window.windowUtils.getBoundsWithoutFlushing(container);

    return {
      top: panelRect.top,
      bottom: panelRect.bottom,
      left: RTL_UI ? containerRect.right - panelRect.width : containerRect.left,
      right: RTL_UI
        ? containerRect.right
        : containerRect.left + panelRect.width,
    };
  },

  onMouseEnter() {
    this._mirror();
  },

  onMouseLeave() {
    this._mirror();
  },

  _mirror() {
    if (this._frozen) {
      return;
    }
    if (this.panel.hasAttribute("mirror")) {
      this.panel.removeAttribute("mirror");
    } else {
      this.panel.setAttribute("mirror", "true");
    }

    if (!this.panel.hasAttribute("sizelimit")) {
      this.panel.setAttribute("sizelimit", "true");
    }
  },
};

var TabBarVisibility = {
  _initialUpdateDone: false,

  update(force = false) {
    let isPopup = !window.toolbar.visible;
    let isTaskbarTab = document.documentElement.hasAttribute("taskbartab");
    let isSingleTabWindow = isPopup || isTaskbarTab;

    let hasVerticalTabs =
      !isSingleTabWindow &&
      Services.prefs.getBoolPref("sidebar.verticalTabs", false);

    // When `gBrowser` has not been initialized, we're opening a new window and
    // assume only a single tab is loading.
    let hasSingleTab = !gBrowser || gBrowser.visibleTabs.length == 1;

    // To prevent tabs being lost, hiding the tabs toolbar should only work
    // when only a single tab is visible or tabs are displayed elsewhere.
    let hideTabsToolbar =
      (isSingleTabWindow && hasSingleTab) || hasVerticalTabs;

    // We only want a non-customized titlebar for popups. It should not be the
    // case, but if a popup window contains more than one tab we re-enable
    // titlebar customization and display tabs.
    CustomTitlebar.allowedBy("non-popup", !(isPopup && hasSingleTab));

    // Update the browser chrome.

    let tabsToolbar = document.getElementById("TabsToolbar");
    let navbar = document.getElementById("nav-bar");

    gNavToolbox.toggleAttribute("tabs-hidden", hideTabsToolbar);
    // Should the nav-bar look and function like a titlebar?
    navbar.classList.toggle(
      "browser-titlebar",
      CustomTitlebar.enabled && hideTabsToolbar
    );

    document
      .getElementById("browser")
      .classList.toggle(
        "browser-toolbox-background",
        CustomTitlebar.enabled && hasVerticalTabs
      );

    if (
      hideTabsToolbar == tabsToolbar.collapsed &&
      !force &&
      this._initialUpdateDone
    ) {
      // No further updates needed, `TabsToolbar` already matches the expected
      // visibilty.
      return;
    }
    this._initialUpdateDone = true;

    tabsToolbar.collapsed = hideTabsToolbar;

    // Stylize close menu items based on tab visibility. When a window will only
    // ever have a single tab, only show the option to close the tab, and
    // simplify the text since we don't need to disambiguate from closing the window.
    document.getElementById("menu_closeWindow").hidden = hideTabsToolbar;
    document.l10n.setAttributes(
      document.getElementById("menu_close"),
      hideTabsToolbar
        ? "tabbrowser-menuitem-close"
        : "tabbrowser-menuitem-close-tab"
    );
  },
};

var TabContextMenu = {
  contextTab: null,
  _updateToggleMuteMenuItems(aTab, aConditionFn) {
    ["muted", "soundplaying"].forEach(attr => {
      if (!aConditionFn || aConditionFn(attr)) {
        if (aTab.hasAttribute(attr)) {
          aTab.toggleMuteMenuItem.setAttribute(attr, "true");
          aTab.toggleMultiSelectMuteMenuItem.setAttribute(attr, "true");
        } else {
          aTab.toggleMuteMenuItem.removeAttribute(attr);
          aTab.toggleMultiSelectMuteMenuItem.removeAttribute(attr);
        }
      }
    });
  },
  updateContextMenu(aPopupMenu) {
    let triggerTab =
      aPopupMenu.triggerNode &&
      (aPopupMenu.triggerNode.tab || aPopupMenu.triggerNode.closest("tab"));
    this.contextTab = triggerTab || gBrowser.selectedTab;
    this.contextTab.addEventListener("TabAttrModified", this);
    aPopupMenu.addEventListener("popuphidden", this);

    this.multiselected = this.contextTab.multiselected;
    this.contextTabs = this.multiselected
      ? gBrowser.selectedTabs
      : [this.contextTab];

    let disabled = gBrowser.tabs.length == 1;
    let tabCountInfo = JSON.stringify({
      tabCount: this.contextTabs.length,
    });

    var menuItems = aPopupMenu.getElementsByAttribute(
      "tbattr",
      "tabbrowser-multiple"
    );
    for (let menuItem of menuItems) {
      menuItem.disabled = disabled;
    }

    disabled = gBrowser.visibleTabs.length == 1;
    menuItems = aPopupMenu.getElementsByAttribute(
      "tbattr",
      "tabbrowser-multiple-visible"
    );
    for (let menuItem of menuItems) {
      menuItem.disabled = disabled;
    }

    let contextNewTabButton = document.getElementById("context_openANewTab");
    // update context menu item strings for vertical tabs
    document.l10n.setAttributes(
      contextNewTabButton,
      gBrowser.tabContainer?.verticalMode
        ? "tab-context-new-tab-open-vertical"
        : "tab-context-new-tab-open"
    );

    // Session store
    let closedCount = SessionStore.getLastClosedTabCount(window);
    document
      .getElementById("History:UndoCloseTab")
      .setAttribute("disabled", closedCount == 0);
    document.l10n.setArgs(document.getElementById("context_undoCloseTab"), {
      tabCount: closedCount,
    });

    // Show/hide fullscreen context menu items and set the
    // autohide item's checked state to mirror the autohide pref.
    showFullScreenViewContextMenuItems(aPopupMenu);

    // #context_moveTabToNewGroup is a simplified context menu item that only
    // appears if there are no existing tab groups available to move the tab to.
    let contextMoveTabToNewGroup = document.getElementById(
      "context_moveTabToNewGroup"
    );
    let contextMoveTabToGroup = document.getElementById(
      "context_moveTabToGroup"
    );
    let contextUngroupTab = document.getElementById("context_ungroupTab");

    if (gBrowser._tabGroupsEnabled) {
      let groupableTabs = this.contextTabs.filter(t => !t.pinned);
      let selectedGroupCount = new Set(
        // The filter removes the "null" group for ungrouped tabs.
        groupableTabs.map(t => t.group).filter(g => g)
      ).size;

      let availableGroupsToMoveTo = gBrowser
        .getAllTabGroups()
        .sort(
          (group1, group2) => group2.lastSeenActive - group1.lastSeenActive
        );

      // Determine whether or not the "current" tab group should appear in the
      // "move tab to group" context menu.
      if (selectedGroupCount == 1) {
        let groupToFilter = groupableTabs[0].group;
        if (groupToFilter && groupableTabs.every(t => t.group)) {
          availableGroupsToMoveTo = availableGroupsToMoveTo.filter(
            group => group !== groupToFilter
          );
        }
      }

      contextMoveTabToGroup.disabled = !groupableTabs.length;
      contextMoveTabToNewGroup.disabled = !groupableTabs.length;
      if (!availableGroupsToMoveTo.length) {
        contextMoveTabToGroup.hidden = true;
        contextMoveTabToNewGroup.hidden = false;
        contextMoveTabToNewGroup.setAttribute("data-l10n-args", tabCountInfo);
      } else {
        contextMoveTabToNewGroup.hidden = true;
        contextMoveTabToGroup.hidden = false;
        contextMoveTabToGroup.setAttribute("data-l10n-args", tabCountInfo);

        const submenu = contextMoveTabToGroup.querySelector("menupopup");
        submenu.querySelectorAll("[tab-group-id]").forEach(el => el.remove());

        availableGroupsToMoveTo.forEach(group => {
          let item = document.createXULElement("menuitem");
          item.setAttribute("tab-group-id", group.id);
          if (group.label) {
            item.setAttribute("label", group.label);
          } else {
            document.l10n.setAttributes(item, "tab-context-unnamed-group");
          }

          item.classList.add("menuitem-iconic");
          item.classList.add("tab-group-icon");
          item.style.setProperty(
            "--tab-group-color",
            group.style.getPropertyValue("--tab-group-color")
          );
          item.style.setProperty(
            "--tab-group-color-invert",
            group.style.getPropertyValue("--tab-group-color-invert")
          );
          item.style.setProperty(
            "--tab-group-color-pale",
            group.style.getPropertyValue("--tab-group-color-pale")
          );
          submenu.appendChild(item);
        });
      }

      contextUngroupTab.hidden = !selectedGroupCount;
      let groupInfo = JSON.stringify({
        groupCount: selectedGroupCount,
      });
      contextUngroupTab.setAttribute("data-l10n-args", groupInfo);
    } else {
      contextMoveTabToNewGroup.hidden = true;
      contextMoveTabToGroup.hidden = true;
      contextUngroupTab.hidden = true;
    }

    // Only one of Reload_Tab/Reload_Selected_Tabs should be visible.
    document.getElementById("context_reloadTab").hidden = this.multiselected;
    document.getElementById("context_reloadSelectedTabs").hidden =
      !this.multiselected;
    let unloadTabItem = document.getElementById("context_unloadTab");
    if (gBrowser._unloadTabInContextMenu) {
      // linkedPanel is false if the tab is already unloaded
      // Cannot unload about: pages, etc., so skip browsers that are not remote
      let unloadableTabs = this.contextTabs.filter(
        t => t.linkedPanel && t.linkedBrowser?.isRemoteBrowser
      );
      unloadTabItem.hidden = unloadableTabs.length === 0;
      unloadTabItem.setAttribute(
        "data-l10n-args",
        JSON.stringify({ tabCount: unloadableTabs.length })
      );
    } else {
      unloadTabItem.hidden = true;
    }

    // Show Play Tab menu item if the tab has attribute activemedia-blocked
    document.getElementById("context_playTab").hidden = !(
      this.contextTab.activeMediaBlocked && !this.multiselected
    );
    document.getElementById("context_playSelectedTabs").hidden = !(
      this.contextTab.activeMediaBlocked && this.multiselected
    );

    // Only one of pin/unpin/multiselect-pin/multiselect-unpin should be visible
    let contextPinTab = document.getElementById("context_pinTab");
    contextPinTab.hidden = this.contextTab.pinned || this.multiselected;
    let contextUnpinTab = document.getElementById("context_unpinTab");
    contextUnpinTab.hidden = !this.contextTab.pinned || this.multiselected;
    let contextPinSelectedTabs = document.getElementById(
      "context_pinSelectedTabs"
    );
    contextPinSelectedTabs.hidden =
      this.contextTab.pinned || !this.multiselected;
    let contextUnpinSelectedTabs = document.getElementById(
      "context_unpinSelectedTabs"
    );
    contextUnpinSelectedTabs.hidden =
      !this.contextTab.pinned || !this.multiselected;

    // Move Tab items
    let contextMoveTabOptions = document.getElementById(
      "context_moveTabOptions"
    );
    contextMoveTabOptions.setAttribute("data-l10n-args", tabCountInfo);
    contextMoveTabOptions.disabled =
      this.contextTab.hidden || gBrowser.allTabsSelected();
    let selectedTabs = gBrowser.selectedTabs;
    let contextMoveTabToEnd = document.getElementById("context_moveToEnd");
    let allSelectedTabsAdjacent = selectedTabs.every(
      (element, index, array) => {
        return array.length > index + 1
          ? element._tPos + 1 == array[index + 1]._tPos
          : true;
      }
    );
    let visibleTabs = gBrowser.visibleTabs;
    let lastVisibleTab = visibleTabs.at(-1);
    let lastTabToMove = this.contextTabs.at(-1);

    let isLastPinnedTab = false;
    if (lastTabToMove.pinned) {
      let sibling = gBrowser.tabContainer.findNextTab(lastTabToMove);
      isLastPinnedTab = !sibling || !sibling.pinned;
    }
    contextMoveTabToEnd.disabled =
      (lastTabToMove == lastVisibleTab || isLastPinnedTab) &&
      !lastTabToMove.group &&
      allSelectedTabsAdjacent;
    let contextMoveTabToStart = document.getElementById("context_moveToStart");
    let isFirstTab =
      !this.contextTabs[0].group &&
      (this.contextTabs[0] == visibleTabs[0] ||
        this.contextTabs[0] == visibleTabs[gBrowser.pinnedTabCount]);
    contextMoveTabToStart.disabled = isFirstTab && allSelectedTabsAdjacent;

    document.getElementById("context_openTabInWindow").disabled =
      this.contextTab.hasAttribute("customizemode");

    // Only one of "Duplicate Tab"/"Duplicate Tabs" should be visible.
    document.getElementById("context_duplicateTab").hidden = this.multiselected;
    document.getElementById("context_duplicateTabs").hidden =
      !this.multiselected;

    let closeTabsToTheStartItem = document.getElementById(
      "context_closeTabsToTheStart"
    );

    // update context menu item strings for vertical tabs
    document.l10n.setAttributes(
      closeTabsToTheStartItem,
      gBrowser.tabContainer?.verticalMode
        ? "close-tabs-to-the-start-vertical"
        : "close-tabs-to-the-start"
    );

    let closeTabsToTheEndItem = document.getElementById(
      "context_closeTabsToTheEnd"
    );

    // update context menu item strings for vertical tabs
    document.l10n.setAttributes(
      closeTabsToTheEndItem,
      gBrowser.tabContainer?.verticalMode
        ? "close-tabs-to-the-end-vertical"
        : "close-tabs-to-the-end"
    );

    // Disable "Close Tabs to the Left/Right" if there are no tabs
    // preceding/following it.
    let noTabsToStart = !gBrowser._getTabsToTheStartFrom(this.contextTab)
      .length;
    closeTabsToTheStartItem.disabled = noTabsToStart;

    let noTabsToEnd = !gBrowser._getTabsToTheEndFrom(this.contextTab).length;
    closeTabsToTheEndItem.disabled = noTabsToEnd;

    // Disable "Close other Tabs" if there are no unpinned tabs.
    let unpinnedTabsToClose = this.multiselected
      ? gBrowser.openTabs.filter(
          t => !t.multiselected && !t.pinned && !t.hidden
        ).length
      : gBrowser.openTabs.filter(
          t => t != this.contextTab && !t.pinned && !t.hidden
        ).length;
    let closeOtherTabsItem = document.getElementById("context_closeOtherTabs");
    closeOtherTabsItem.disabled = unpinnedTabsToClose < 1;

    // Update the close item with how many tabs will close.
    document
      .getElementById("context_closeTab")
      .setAttribute("data-l10n-args", tabCountInfo);

    let closeDuplicateEnabled = Services.prefs.getBoolPref(
      "browser.tabs.context.close-duplicate.enabled"
    );
    let closeDuplicateTabsItem = document.getElementById(
      "context_closeDuplicateTabs"
    );
    closeDuplicateTabsItem.hidden = !closeDuplicateEnabled;
    closeDuplicateTabsItem.disabled =
      !closeDuplicateEnabled ||
      !gBrowser.getDuplicateTabsToClose(this.contextTab).length;

    // Disable "Close Multiple Tabs" if all sub menuitems are disabled
    document.getElementById("context_closeTabOptions").disabled =
      closeTabsToTheStartItem.disabled &&
      closeTabsToTheEndItem.disabled &&
      closeOtherTabsItem.disabled;

    // Hide "Bookmark Tab…" for multiselection.
    // Update its state if visible.
    let bookmarkTab = document.getElementById("context_bookmarkTab");
    bookmarkTab.hidden = this.multiselected;

    // Show "Bookmark Selected Tabs" in a multiselect context and hide it otherwise.
    let bookmarkMultiSelectedTabs = document.getElementById(
      "context_bookmarkSelectedTabs"
    );
    bookmarkMultiSelectedTabs.hidden = !this.multiselected;

    let toggleMute = document.getElementById("context_toggleMuteTab");
    let toggleMultiSelectMute = document.getElementById(
      "context_toggleMuteSelectedTabs"
    );

    // Only one of mute_unmute_tab/mute_unmute_selected_tabs should be visible
    toggleMute.hidden = this.multiselected;
    toggleMultiSelectMute.hidden = !this.multiselected;

    const isMuted = this.contextTab.hasAttribute("muted");
    document.l10n.setAttributes(
      toggleMute,
      isMuted ? "tabbrowser-context-unmute-tab" : "tabbrowser-context-mute-tab"
    );
    document.l10n.setAttributes(
      toggleMultiSelectMute,
      isMuted
        ? "tabbrowser-context-unmute-selected-tabs"
        : "tabbrowser-context-mute-selected-tabs"
    );

    this.contextTab.toggleMuteMenuItem = toggleMute;
    this.contextTab.toggleMultiSelectMuteMenuItem = toggleMultiSelectMute;
    this._updateToggleMuteMenuItems(this.contextTab);

    let selectAllTabs = document.getElementById("context_selectAllTabs");
    selectAllTabs.disabled = gBrowser.allTabsSelected();

    gSync.updateTabContextMenu(aPopupMenu, this.contextTab);

    let reopenInContainer = document.getElementById(
      "context_reopenInContainer"
    );
    reopenInContainer.hidden =
      !Services.prefs.getBoolPref("privacy.userContext.enabled", false) ||
      PrivateBrowsingUtils.isWindowPrivate(window);
    reopenInContainer.disabled = this.contextTab.hidden;

    SharingUtils.updateShareURLMenuItem(
      this.contextTab.linkedBrowser,
      document.getElementById("context_sendTabToDevice")
    );
  },

  handleEvent(aEvent) {
    switch (aEvent.type) {
      case "popuphidden":
        if (aEvent.target.id == "tabContextMenu") {
          this.contextTab.removeEventListener("TabAttrModified", this);
          this.contextTab = null;
          this.contextTabs = null;
        }
        break;
      case "TabAttrModified": {
        let tab = aEvent.target;
        this._updateToggleMuteMenuItems(tab, attr =>
          aEvent.detail.changed.includes(attr)
        );
        break;
      }
    }
  },

  createReopenInContainerMenu(event) {
    createUserContextMenu(event, {
      isContextMenu: true,
      excludeUserContextId: this.contextTab.getAttribute("usercontextid"),
    });
  },
  duplicateSelectedTabs() {
    let newIndex = this.contextTabs.at(-1)._tPos + 1;
    for (let tab of this.contextTabs) {
      let newTab = SessionStore.duplicateTab(window, tab);
      if (tab.group) {
        Glean.tabgroup.tabInteractions.duplicate.add();
      }
      gBrowser.moveTabTo(newTab, { tabIndex: newIndex++ });
    }
  },
  reopenInContainer(event) {
    let userContextId = parseInt(
      event.target.getAttribute("data-usercontextid")
    );

    for (let tab of this.contextTabs) {
      if (tab.getAttribute("usercontextid") == userContextId) {
        continue;
      }

      /* Create a triggering principal that is able to load the new tab
         For content principals that are about: chrome: or resource: we need system to load them.
         Anything other than system principal needs to have the new userContextId.
      */
      let triggeringPrincipal;

      if (tab.linkedPanel) {
        triggeringPrincipal = tab.linkedBrowser.contentPrincipal;
      } else {
        // For lazy tab browsers, get the original principal
        // from SessionStore
        let tabState = JSON.parse(SessionStore.getTabState(tab));
        try {
          triggeringPrincipal = E10SUtils.deserializePrincipal(
            tabState.triggeringPrincipal_base64
          );
        } catch (ex) {
          continue;
        }
      }

      if (!triggeringPrincipal || triggeringPrincipal.isNullPrincipal) {
        // Ensure that we have a null principal if we couldn't
        // deserialize it (for lazy tab browsers) ...
        // This won't always work however is safe to use.
        triggeringPrincipal =
          Services.scriptSecurityManager.createNullPrincipal({ userContextId });
      } else if (triggeringPrincipal.isContentPrincipal) {
        triggeringPrincipal = Services.scriptSecurityManager.principalWithOA(
          triggeringPrincipal,
          {
            userContextId,
          }
        );
      }

      let newTab = gBrowser.addTab(tab.linkedBrowser.currentURI.spec, {
        userContextId,
        pinned: tab.pinned,
        index: tab._tPos + 1,
        triggeringPrincipal,
      });

      if (gBrowser.selectedTab == tab) {
        gBrowser.selectedTab = newTab;
      }
      if (tab.muted && !newTab.muted) {
        newTab.toggleMuteAudio(tab.muteReason);
      }
    }
  },

  closeContextTabs() {
    if (this.contextTab.multiselected) {
      gBrowser.removeMultiSelectedTabs({
        telemetrySource: gBrowser.TabMetrics.METRIC_SOURCE.TAB_STRIP,
      });
    } else {
      gBrowser.removeTab(this.contextTab, {
        animate: true,
        telemetrySource: gBrowser.TabMetrics.METRIC_SOURCE.TAB_STRIP,
      });
    }
  },

  explicitUnloadTabs() {
    gBrowser.explicitUnloadTabs(this.contextTabs);
  },

  moveTabsToNewGroup() {
    gBrowser.addTabGroup(this.contextTabs, {
      insertBefore: this.contextTab,
      isUserTriggered: true,
      telemetryUserCreateSource: "tab_menu",
    });
    gBrowser.selectedTab = this.contextTabs[0];

    // When using the tab context menu to create a group from the all tabs
    // panel, make sure we close that panel so that it doesn't obscure the tab
    // group creation panel.
    gTabsPanel.hideAllTabsPanel();
  },

  /**
   * @param {MozTabbrowserTabGroup} group
   */
  moveTabsToGroup(group) {
    group.addTabs(
      this.contextTabs,
      gBrowser.TabMetrics.userTriggeredContext(
        gBrowser.TabMetrics.METRIC_SOURCE.TAB_MENU
      )
    );
    group.ownerGlobal.focus();
  },

  ungroupTabs() {
    for (let i = this.contextTabs.length - 1; i >= 0; i--) {
      gBrowser.ungroupTab(this.contextTabs[i]);
    }
  },
};
